#include "Document.Tests.RectangleGestureParent.h"
#include "../../RedPrism/SelectionOutlineGestureJob.h"
#include "../../RedPrism/SelectionOutlineGestureMemory.h"
#include "../Support/DocumentFixture.h"
#include "DocumentDraft.h"
#include "LassoFinishGeometry.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>
#include <wil/resource.h>
#include <windows.h>

namespace
{
unsigned checks = 0, failures = 0;
const char* scenarioName = "setup";
const char* family = "rectangle-gesture-parent";
void Expect(bool value, const char* label)
{
    ++checks;
    failures += value ? 0 : 1;
    std::printf("%s %s.%s.%s\n", value ? "PASS" : "FAIL", family, scenarioName, label);
}
[[noreturn]] void Broken(const char* label)
{
    std::printf("FAIL %s.setup.%s\n", family, label);
    std::_Exit(1);
}
void Settle(JobScheduler& jobs)
{
    if (SettleJobs(jobs, 10000) != SettleResult::Settled)
        Broken("settlement");
}
template <class Predicate> void Until(JobScheduler& jobs, Predicate done)
{
    const auto deadline = GetTickCount64() + 10000;
    while (!done())
    {
        if (PumpMessages().quit)
            Broken("unexpected-quit");
        (void)DrainCompletions(jobs);
        (void)RunUiActions(jobs);
        if (done())
            break;
        const auto now = GetTickCount64();
        if (now >= deadline)
            Broken("event-deadline");
        const HANDLE wake = jobs.WakeHandle();
        if (MsgWaitForMultipleObjectsEx(1, &wake, static_cast<DWORD>(deadline - now), QS_ALLINPUT,
                                        MWMO_INPUTAVAILABLE) == WAIT_FAILED)
            Broken("event-wait");
    }
}
struct Memory final
{
    unsigned queries = 0;
    bool scarce = false;
    static bool Sample(void* raw, MemorySnapshot& value) noexcept
    {
        auto& self = *static_cast<Memory*>(raw);
        ++self.queries;
        const auto available = self.scarce ? 0 : kMemoryAdmissionHeadroomBytes + 2ull * 1024 * 1024 * 1024;
        value = {available, available};
        return true;
    }
};
enum class Initial
{
    Absent,
    Empty,
    Old,
    Large,
    NoLayers
};
void Install(DocumentSession& session, Initial initial)
{
    DocumentFixture document;
    document.id = Guid::Generate();
    document.width = document.height = 100;
    if (initial == Initial::Empty)
        document.selection = DocumentSelection{false, SelectionLoops{}};
    if (initial == Initial::Old || initial == Initial::Large)
    {
        SelectionLoop loop;
        if (initial == Initial::Large)
        {
            loop.reserve(65540);
            for (unsigned i = 0; i < 65536; ++i)
                loop.push_back({1.0 + 6.0 * i / 65535.0, 2});
            loop.insert(loop.end(), {{7, 2}, {7, 6}, {1, 6}, {1, 2}});
        }
        else
            loop = {{1, 2}, {7, 2}, {7, 6}, {1, 6}};
        document.selection = DocumentSelection{false, SelectionLoops{std::vector<SelectionLoop>{std::move(loop)}}};
    }
    std::optional<Guid> active;
    if (initial != Initial::NoLayers)
    {
        auto layer = ImageLayer::Blank(L"Rectangle parent", {100, 100});
        layer.asset =
            RasterAsset{Raster::MakeSparse(100, 100, RasterFormat::Bgra8888, Raster{}, 0, 0, {}, 0), {}, layer.name};
        active = layer.id;
        document.layers.push_back(std::move(layer));
    }
    session.ReplaceDocument(std::move(document).Seal(), active);
    session.history.Reset();
}
struct Probe final
{
    DocumentSession* session = nullptr;
    bool foreignHistory = false, foreignTransform = false, gateFinish = false;
    unsigned opened = 0, moved = 0, terminal = 0;
    const LassoDraft* draft = nullptr;
    std::optional<DocumentHistory::StateView> foreign;
    wil::unique_handle entered{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    wil::unique_handle proceed{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    std::atomic<bool> workerEntered{false};
    DWORD owner = GetCurrentThreadId();
    static void Owner(void* raw, SelectionOutlineGestureState::Event event) noexcept
    {
        auto& self = *static_cast<Probe*>(raw);
        if (event == SelectionOutlineGestureState::Event::Opened)
        {
            ++self.opened;
            self.draft = self.session->PendingLasso() ? &*self.session->PendingLasso() : nullptr;
            if (self.foreignHistory)
            {
                self.session->history.Begin(L"Foreign Begin", self.session->Document(), self.session->ActiveLayerID());
                self.foreign = self.session->history.CaptureState();
            }
            if (self.foreignTransform)
                self.session->BeginTransform(true);
        }
        else if (event == SelectionOutlineGestureState::Event::Moved)
            ++self.moved;
        else
            ++self.terminal;
    }
    static void Work(void* raw, SelectionOutlineGestureState::WorkStage stage) noexcept
    {
        auto& self = *static_cast<Probe*>(raw);
        if (!self.gateFinish || stage != SelectionOutlineGestureState::WorkStage::DrawingPrepared)
            return;
        self.workerEntered.store(true, std::memory_order_release);
        if (!PostThreadMessageW(self.owner, WM_APP + 0x6c7, 0, 0) ||
            WaitForSingleObject(self.proceed.get(), 10000) != WAIT_OBJECT_0)
            Broken("finish-gate");
    }
};
struct Fixture final
{
    Memory memory;
    JobScheduler jobs;
    std::unique_ptr<DocumentSession> session = std::make_unique<DocumentSession>();
    std::unique_ptr<HostSelectionPressInput> prefix = std::make_unique<HostSelectionPressInput>();
    std::unique_ptr<SelectionOutlineGestureState> state;
    std::optional<SelectionOutlineGestureMemory> admitted;
    Probe probe;
    Fixture(Initial initial, bool scarce = false, LassoKind kind = LassoKind::Rectangle,
            std::optional<LassoKind> admissionKind = std::nullopt)
    {
        Install(*session, initial);
        probe.session = session.get();
        memory.scarce = scarce;
        if (jobs.Initialize(2, {}, {Memory::Sample, &memory}) != S_OK || !jobs.EnableDeferredRelease())
            Broken("scheduler");
        admitted = DescribeSelectionOutlineGestureMemory(*session, admissionKind.value_or(kind));
        if (!admitted || !probe.entered || !probe.proceed)
            Broken("complete-admission");
        HostSelectionPressInput::Context arrival;
        arrival.document = admitted->source.document;
        arrival.ordinal = 1;
        arrival.kind = kind;
        arrival.active = session->ActiveLayerID();
        arrival.crop = session->CropFrame();
        arrival.mask = session->IsMaskSelected();
        if (!prefix->Begin(51, arrival, {20, 30}, {}, false))
            Broken("accepted-down");
        state = std::make_unique<SelectionOutlineGestureState>(jobs, *session, *prefix, 51, admitted->limits);
        state->admissionSource = admitted->source;
        state->allowNewRectangle = kind == LassoKind::Rectangle;
        state->allowNewEllipse = kind == LassoKind::Ellipse;
        state->requireCapturedTarget = true;
        state->observer = &probe;
        state->notify = Probe::Owner;
        state->workObserver = &probe;
        state->workCheckpoint = Probe::Work;
    }
    ~Fixture()
    {
        (void)SetEvent(probe.proceed.get());
        if (state && state->controller)
            jobs.Close();
        ReleaseReaders();
        jobs.Close();
    }
    void Submit()
    {
        std::unique_ptr<Job> job = std::make_unique<SelectionOutlineGestureJob>(*state);
        if (jobs.Submit(job,
                        {.kind = JobClass::Selection,
                         .serialKey = session->DocumentSerial(),
                         .memoryBytes = admitted->limits.totalBytes,
                         .deferredRetirement = true,
                         .allowMemoryWaiting = false,
                         .reserveRetainedMemory = true,
                         .continuingOutputLimit = admitted->limits.continuingOutputBytes},
                        state->original) != S_OK)
            Broken("submit-original");
    }
    void Move(Vec2 point, HostSelectionPressInput::Modifiers modifiers, std::uint64_t ordinal)
    {
        if (prefix->Move(51, point, modifiers, false, ordinal) != HostSelectionPressInput::Admission::Recorded)
            Broken("accepted-move");
    }
    void End(bool lost = false)
    {
        if (prefix->Finish(51, true, 5, lost) != HostSelectionPressInput::Admission::Recorded)
            Broken("accepted-terminal");
    }
    void ReleaseReaders()
    {
        if (session)
            session->CancelLasso();
        probe.foreign.reset();
        state.reset();
        prefix.reset();
        session.reset();
        Settle(jobs);
    }
};
void Numeric(Initial initial, const char* name, Vec2 last = {70, 80}, HostSelectionPressInput::Modifiers modifiers = {},
             SelectionBounds wanted = {20, 30, 70, 80}, bool lost = false, LassoKind kind = LassoKind::Rectangle,
             std::uint64_t expectedPoints = 4)
{
    scenarioName = name;
    auto f = std::make_unique<Fixture>(initial, false, kind);
    const auto generation = f->session->Generation();
    const auto history = f->session->history.CaptureState();
    const auto revision = f->session->history.CurrentRevision();
    const auto original = f->session->Selection();
    f->Move({40, 50}, {}, 2);
    f->Move(last, modifiers, 3);
    f->End(lost);
    f->Submit();
    f->session->marqueeKind = kind == LassoKind::Rectangle ? LassoKind::Ellipse : LassoKind::Rectangle;
    f->session->selectionAntialiased = false;
    Settle(f->jobs);
    const auto& s = *f->state;
    Expect(s.completed && s.drawing && s.opened && s.sameTicket && s.captures == 1 && s.hits == 1 &&
               s.inputAcknowledged && !f->prefix->Busy(),
           "one-original-parent-hit-and-descriptor-acknowledgement");
    const auto& result = f->session->Selection();
    if (lost)
    {
        Expect(s.outcome.status == JobStatus::Cancelled && s.aborted && !s.closed && s.closurePrepares == 0 &&
                   f->session->Generation() == generation && f->session->history.IsCurrent(history) &&
                   f->session->history.CurrentRevision() == revision && result == original,
               "loss-aborts-draft-and-preserves-exact-history-source");
    }
    else
    {
        Expect(s.outcome.status == JobStatus::Finished && s.closed && s.closurePrepares == 1 && result &&
                   result->antialiased && result->Bounds() == std::optional(wanted) &&
                   result->loops.PointCount() == expectedPoints && result->loops.size() == 1,
               kind == LassoKind::Rectangle ? "event-time-rectangle-modifiers-kind-and-terminal-aa"
                                            : "event-time-ellipse-modifiers-kind-and-terminal-aa");
        Expect(f->session->Generation() == generation + 1 && f->session->history.UndoCount() == 1 &&
                   f->session->history.UndoName() ==
                       (kind == LassoKind::Rectangle ? L"Rectangular Marquee" : L"Elliptical Marquee") &&
                   f->session->history.CaptureState().Depth() == 0,
               "single-atomic-history-edit-with-one-generation");
        f->session->Undo();
        Expect(f->session->Selection() == original && f->session->history.CanRedo(),
               "undo-restores-literal-prior-absent-empty-or-large-selection");
        f->session->Redo();
        Expect(f->session->Selection() && f->session->Selection()->Bounds() == std::optional(wanted) &&
                   f->session->Selection()->antialiased,
               "redo-restores-terminal-selection");
    }
    Expect(!f->session->PendingLasso() && f->probe.opened == 1 && f->probe.moved == 1 && f->probe.terminal == 1 &&
               f->memory.queries == 1 && s.firstBudget == f->admitted->limits.totalBytes &&
               s.openingBytes <= f->admitted->limits.continuingOutputBytes,
           "one-admission-opening-partition-and-one-move-presentation");
    f->ReleaseReaders();
    Expect(f->jobs.ReservedMemoryBytes() == 0 && f->jobs.ReleasingMemoryBytes() == 0,
           "last-readers-refund-original-ticket");
}
void CancelAndForeign(int mode, const char* name, LassoKind kind = LassoKind::Rectangle)
{
    scenarioName = name;
    auto f = std::make_unique<Fixture>(Initial::Old, false, kind);
    const auto generation = f->session->Generation();
    const auto history = f->session->history.CaptureState();
    const auto revision = f->session->history.CurrentRevision();
    f->probe.foreignHistory = mode == 2;
    f->probe.foreignTransform = mode == 3;
    f->probe.gateFinish = mode == 4;
    f->Move({40, 50}, {}, 2);
    if (mode >= 2)
        f->End();
    f->Submit();
    if (mode == 0 || mode == 1)
    {
        Until(f->jobs, [&] { return f->state->waiting && f->state->moves == 1; });
        Expect(f->state->drawing && f->session->PendingLasso() &&
                   f->jobs.ReservedMemoryBytes() >= f->admitted->limits.totalBytes,
               "waiting-draft-retains-complete-original-reservation");
        if (mode == 0)
        {
            if (!f->jobs.Cancel(f->state->original))
                Broken("cancel");
            f->state->waiting = false;
            if (!f->jobs.Resume(f->state->original))
                Broken("cancel-resume");
            Settle(f->jobs);
        }
        else
            f->jobs.Close();
        Expect(f->state->aborted && f->state->outcome.status == JobStatus::Cancelled && !f->session->PendingLasso() &&
                   !f->state->controller && !f->prefix->Busy() && f->session->history.IsCurrent(history),
               "cancel-or-joined-close-aborts-only-owned-draft");
        Expect(f->probe.terminal == (mode == 0 ? 1u : 0u), "joined-close-does-not-run-useful-owner-callbacks");
    }
    else if (mode == 4)
    {
        Until(f->jobs, [&] { return f->probe.workerEntered.load(std::memory_order_acquire); });
        Expect(!f->session->PendingLasso() && f->state->closurePrepares == 1 && f->session->history.IsCurrent(history),
               "prepared-finish-awaits-original-owner-publication");
        if (!f->jobs.Cancel(f->state->original))
            Broken("cancel-prepared");
        (void)SetEvent(f->probe.proceed.get());
        Settle(f->jobs);
        Expect(f->state->outcome.status == JobStatus::Cancelled && !f->state->closed &&
                   f->session->history.IsCurrent(history) && f->session->SetLassoOpeningCountersForTest(1, 1),
               "cancelled-finish-clears-only-original-pending-intent");
    }
    else
    {
        Settle(f->jobs);
        Expect(f->state->outcome.status == JobStatus::Failed && f->state->outcome.error == E_ABORT &&
                   !f->state->aborted && !f->state->closed && f->session->PendingLasso() &&
                   &*f->session->PendingLasso() == f->probe.draft,
               "foreign-editor-refusal-preserves-exact-draft");
        Expect(mode == 2 ? f->probe.foreign && f->session->history.IsCurrent(*f->probe.foreign) &&
                               f->session->history.CaptureState().Depth() == 1
                         : f->session->PendingTransformEdit().has_value(),
               "foreign-history-or-transform-survives-refusal");
    }
    Expect(f->session->Generation() == generation && f->session->history.CurrentRevision() == revision &&
               f->session->Selection()->Bounds() == std::optional<SelectionBounds>{{1, 2, 7, 6}},
           "failed-or-cancelled-path-preserves-source-geometry-and-revision");
    f->ReleaseReaders();
    Expect(f->jobs.ReservedMemoryBytes() == 0 && f->jobs.ReleasingMemoryBytes() == 0,
           "terminal-or-joined-last-readers-refund");
}
} // namespace
int RunRectangleGestureParentTests()
{
    family = "rectangle-gesture-parent";
    checks = failures = 0;
    Numeric(Initial::Absent, "absent");
    Numeric(Initial::Empty, "empty");
    Numeric(Initial::Old, "outside");
    Numeric(Initial::Large, "large-prior");
    Numeric(Initial::NoLayers, "no-layers");
    Numeric(Initial::Old, "shift", {60, 45}, {true, false, false}, {20, 30, 60, 70});
    // EditorCanvas.dragMarqueeDraft and Selection.swift139-140: Option never requests fromCenter.
    Numeric(Initial::Old, "alt", {25, 40}, {false, false, true}, {20, 30, 25, 40});
    Numeric(Initial::Old, "lost", {70, 80}, {}, {20, 30, 70, 80}, true);
    CancelAndForeign(0, "cancel-waiting");
    CancelAndForeign(1, "joined-close");
    CancelAndForeign(2, "foreign-history");
    CancelAndForeign(3, "foreign-transform");
    CancelAndForeign(4, "cancel-prepared");
    std::printf("SUMMARY rectangle-gesture-parent checks=%u failures=%u\n", checks, failures);
    return failures ? 1 : 0;
}

int RunEllipseGestureParentTests()
{
    family = "ellipse-gesture-parent";
    checks = failures = 0;
    constexpr auto kind = LassoKind::Ellipse;
    for (const auto initial : {Initial::Absent, Initial::Empty, Initial::Old, Initial::Large, Initial::NoLayers})
    {
        const char* name = initial == Initial::Absent  ? "absent"
                           : initial == Initial::Empty ? "empty"
                           : initial == Initial::Old   ? "outside"
                           : initial == Initial::Large ? "large-prior"
                                                       : "no-layers";
        // Radius25: ceil(2*pi*25/2)=79, rounded to80 vertices by the legacy rule.
        Numeric(initial, name, {70, 80}, {}, {20, 30, 70, 80}, false, kind, 80);
    }
    Numeric(Initial::Old, "shift", {60, 45}, {true, false, false}, {20, 30, 60, 70}, false, kind, 64);
    Numeric(Initial::Old, "alt", {25, 40}, {false, false, true}, {20, 30, 25, 40}, false, kind, 32);
    Numeric(Initial::Old, "lost", {70, 80}, {}, {20, 30, 70, 80}, true, kind, 80);
    CancelAndForeign(0, "cancel-waiting", kind);
    CancelAndForeign(1, "joined-close", kind);
    CancelAndForeign(2, "foreign-history", kind);
    CancelAndForeign(3, "foreign-transform", kind);
    CancelAndForeign(4, "cancel-prepared", kind);
    {
        scenarioName = "rectangle-admission";
        auto f = std::make_unique<Fixture>(Initial::Old, false, kind, LassoKind::Rectangle);
        const auto generation = f->session->Generation();
        const auto history = f->session->history.CaptureState();
        const auto original = f->session->Selection();
        f->Move({70, 80}, {}, 2);
        f->End();
        f->Submit();
        Settle(f->jobs);
        Expect(f->state->completed && f->state->outcome.status == JobStatus::Failed &&
                   f->state->outcome.error == E_ABORT && !f->state->captures && !f->state->hits &&
                   !f->session->PendingLasso() && !f->prefix->Busy() && f->session->Generation() == generation &&
                   f->session->history.IsCurrent(history) && f->session->Selection() == original,
               "ellipse-refuses-rectangle-envelope-before-source-capture-or-mutation");
        f->ReleaseReaders();
        Expect(f->jobs.ReservedMemoryBytes() == 0 && f->jobs.ReleasingMemoryBytes() == 0,
               "refused-kind-original-ticket-fully-refunds");
    }
    std::printf("SUMMARY ellipse-gesture-parent checks=%u failures=%u\n", checks, failures);
    return failures ? 1 : 0;
}
