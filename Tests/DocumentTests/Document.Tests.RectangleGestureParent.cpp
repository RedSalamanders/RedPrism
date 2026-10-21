#include "Document.Tests.RectangleGestureParent.h"
#include "../../RedPrism/SelectionOutlineGestureJob.h"
#include "../../RedPrism/SelectionOutlineGestureMemory.h"
#include "../Support/Support.Tests.DocumentFixture.h"
#include "DocumentDraft.h"
#include "LassoFinishGeometry.h"

#include <atomic>
#include <cmath>
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
    std::uint64_t capacity = 2ull * 1024 * 1024 * 1024;
    static bool Sample(void* raw, MemorySnapshot& value) noexcept
    {
        auto& self = *static_cast<Memory*>(raw);
        ++self.queries;
        const auto available = self.scarce ? 0 : kMemoryAdmissionHeadroomBytes + self.capacity;
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
            std::optional<LassoKind> admissionKind = std::nullopt, SelectionMode mode = SelectionMode::New,
            std::optional<SelectionMode> admissionMode = std::nullopt, Vec2 down = {20, 30},
            bool preservingEnabled = true, HostSelectionPressInput::Modifiers downModifiers = {})
    {
        Install(*session, initial);
        probe.session = session.get();
        memory.scarce = scarce;
        if (jobs.Initialize(2, {}, {Memory::Sample, &memory}) != S_OK || !jobs.EnableDeferredRelease())
            Broken("scheduler");
        admitted =
            DescribeSelectionOutlineGestureMemory(*session, admissionKind.value_or(kind), admissionMode.value_or(mode));
        if (!admitted || !probe.entered || !probe.proceed)
            Broken("complete-admission");
        HostSelectionPressInput::Context arrival;
        arrival.document = admitted->source.document;
        arrival.ordinal = 1;
        arrival.kind = kind;
        arrival.tool =
            kind == LassoKind::Freehand ? HostSelectionPressInput::Tool::Lasso : HostSelectionPressInput::Tool::Marquee;
        arrival.mode = mode;
        arrival.active = session->ActiveLayerID();
        arrival.crop = session->CropFrame();
        arrival.mask = session->IsMaskSelected();
        if (!prefix->Begin(51, arrival, down, downModifiers, false))
            Broken("accepted-down");
        state = std::make_unique<SelectionOutlineGestureState>(jobs, *session, *prefix, 51, admitted->limits);
        state->admissionSource = admitted->source;
        if (kind == LassoKind::Freehand)
            state->drawingPlan = admitted.value().drawing;
        state->allowNewRectangle = kind == LassoKind::Rectangle;
        state->allowNewEllipse = kind == LassoKind::Ellipse;
        state->allowPreservingMarquee = preservingEnabled && mode != SelectionMode::New;
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
    void End(bool lost = false, std::uint64_t ordinal = 5)
    {
        if (prefix->Finish(51, true, ordinal, lost) != HostSelectionPressInput::Admission::Recorded)
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
void CancelAndForeign(int mode, const char* name, LassoKind kind = LassoKind::Rectangle,
                      SelectionMode selectionMode = SelectionMode::New)
{
    scenarioName = name;
    auto f = std::make_unique<Fixture>(Initial::Old, false, kind, std::nullopt, selectionMode);
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
// Literal independent winding at probes deliberately far from clip-grid boundaries.
bool ProbeFilled(const DocumentSelection& selection, Vec2 point)
{
    int winding = 0;
    for (const auto& loop : selection.loops.Get())
    {
        if (loop.empty())
            continue;
        auto previous = loop.back();
        for (const auto next : loop)
        {
            const double side =
                (next.x - previous.x) * (point.y - previous.y) - (point.x - previous.x) * (next.y - previous.y);
            if (previous.y <= point.y && next.y > point.y && side > 0)
                ++winding;
            else if (previous.y > point.y && next.y <= point.y && side < 0)
                --winding;
            previous = next;
        }
    }
    return winding != 0;
}
void PreservingNumeric(LassoKind kind, SelectionMode mode, Initial initial, const char* name)
{
    scenarioName = name;
    // (3,3) is inside the literal old rectangle. Add/Subtract must draw there, never move it.
    auto f = std::make_unique<Fixture>(initial, false, kind, std::nullopt, mode, std::nullopt, Vec2{3, 3});
    const auto generation = f->session->Generation();
    const auto original = f->session->Selection();
    const auto revision = f->session->history.CurrentRevision();
    f->Move({9, 8}, {}, 2);
    f->End();
    f->Submit();
    f->session->marqueeKind = kind == LassoKind::Rectangle ? LassoKind::Ellipse : LassoKind::Rectangle;
    f->session->selectionModeChoice = SelectionMode::New;
    f->session->selectionAntialiased = false;
    Settle(f->jobs);
    const auto& state = *f->state;
    const auto reservationCeiling = initial == Initial::Large ? 512ull * 1024 * 1024 : 32ull * 1024 * 1024;
    Expect(f->memory.capacity == 2ull * 1024 * 1024 * 1024 &&
               f->admitted.value().limits.totalBytes < reservationCeiling &&
               state.firstBudget == f->admitted.value().limits.totalBytes,
           initial == Initial::Large ? "large-source-original-parent-under512mib-with-normal2gib-query"
                                     : "small-source-original-parent-under32mib-with-normal2gib-query");
    if (initial == Initial::Large)
        Expect(original && original.value().loops.PointCount() == 65540 && original.value().loops.size() == 1,
               "original-65540-point-collinear-rectangle-count-reaches-real-parent");
    Expect(state.completed && state.outcome.status == JobStatus::Finished && state.drawing && state.opened &&
               state.closed && state.sameTicket && state.captures == 1 && state.hits == 0 &&
               state.closurePrepares == 1 && state.inputAcknowledged && !f->prefix->Busy(),
           "captured-preserving-mode-always-draws-one-original-parent-without-hit-test");
    const auto& selected = f->session->Selection();
    const bool keep = mode == SelectionMode::Subtract && (initial == Initial::Absent || initial == Initial::NoLayers);
    if (keep)
    {
        Expect(!selected && f->session->Generation() == generation &&
                   f->session->history.CurrentRevision() == revision && !f->session->history.CanUndo(),
               "subtract-absent-keeps-absence-and-exact-revision-with-no-history");
    }
    else
    {
        const bool empty = mode == SelectionMode::Subtract && initial == Initial::Empty;
        const bool hasOld = initial == Initial::Old || initial == Initial::Large;
        const SelectionBounds bounds =
            hasOld ? (mode == SelectionMode::Add ? SelectionBounds{1, 2, 9, 8} : SelectionBounds{1, 2, 7, 6})
                   : SelectionBounds{3, 3, 9, 8};
        Expect(selected && selected->antialiased &&
                   (empty ? selected->IsEmpty() : selected->Bounds() == std::optional(bounds)) &&
                   !f->session->PendingLasso() && !f->session->IsMovingSelection(),
               "literal-bounds-terminal-aa-and-present-empty-contract");
        if (hasOld)
        {
            // Old-only (2,3), overlap (6,4), marquee-only (8,5.5), neither (8,2).
            // These points work for the six-by-five rectangle and ellipse centered at (6,5.5).
            Expect(selected && ProbeFilled(selected.value(), {2, 3}) &&
                       ProbeFilled(selected.value(), {6, 4}) == (mode == SelectionMode::Add) &&
                       ProbeFilled(selected.value(), {8, 5.5}) == (mode == SelectionMode::Add) &&
                       !ProbeFilled(selected.value(), {8, 2}),
                   "independent-four-region-union-or-difference-winding");
            if (kind == LassoKind::Rectangle)
            {
                double twiceArea = 0;
                for (const auto& loop : selected->loops.Get())
                {
                    if (loop.empty())
                        continue;
                    auto previous = loop.back();
                    for (const auto next : loop)
                    {
                        twiceArea += previous.x * next.y - next.x * previous.y;
                        previous = next;
                    }
                }
                // Old 24 + new 30 - overlap12 =42; old24 - overlap12 =12.
                Expect(std::abs(twiceArea) == (mode == SelectionMode::Add ? 84 : 24),
                       "independent-literal-rectangle-area-42-or12");
            }
        }
        Expect(f->session->Generation() == generation + 1 && f->session->history.UndoCount() == 1 &&
                   f->session->history.UndoName() ==
                       (kind == LassoKind::Rectangle ? L"Rectangular Marquee" : L"Elliptical Marquee"),
               "one-atomic-named-edit-and-one-generation");
        f->session->Undo();
        Expect(f->session->Selection() == original && f->session->history.CanRedo(),
               "undo-restores-absent-empty-or-original-selection");
        f->session->Redo();
        Expect(f->session->Selection() && f->session->Selection()->antialiased &&
                   (empty ? f->session->Selection()->IsEmpty()
                          : f->session->Selection()->Bounds() == std::optional(bounds)),
               "redo-restores-complete-preserving-result");
    }
    Expect(f->memory.queries == 1 && state.firstBudget == f->admitted->limits.totalBytes && f->probe.opened == 1 &&
               f->probe.moved == 1 && f->probe.terminal == 1,
           "one-complete-admission-and-batched-move-presentation");
    f->ReleaseReaders();
    Expect(f->jobs.ReservedMemoryBytes() == 0 && f->jobs.ReleasingMemoryBytes() == 0,
           "last-readers-return-original-preserving-reservation");
}
void RefusePreserving(LassoKind kind, SelectionMode mode, bool wrongAdmission)
{
    scenarioName = wrongAdmission ? "new-only-admission" : "disabled-preserving-gate";
    auto f = std::make_unique<Fixture>(Initial::Old, false, kind, std::nullopt, mode,
                                       wrongAdmission ? std::optional{SelectionMode::New} : std::nullopt, Vec2{3, 3},
                                       wrongAdmission);
    const auto original = f->session->Selection();
    const auto generation = f->session->Generation();
    const auto history = f->session->history.CaptureState();
    f->Move({9, 8}, {}, 2);
    f->End();
    f->Submit();
    Settle(f->jobs);
    Expect(f->state->completed && f->state->outcome.status == JobStatus::Failed && f->state->outcome.error == E_ABORT &&
               !f->state->captures && !f->state->hits && !f->session->PendingLasso() && !f->prefix->Busy() &&
               f->session->Selection() == original && f->session->Generation() == generation &&
               f->session->history.IsCurrent(history),
           "refuses-before-source-capture-and-preserves-exact-original");
    f->ReleaseReaders();
    Expect(f->jobs.ReservedMemoryBytes() == 0 && f->jobs.ReleasingMemoryBytes() == 0,
           "refused-preserving-mode-fully-refunds-original-ticket");
}

double FreehandArea(const DocumentSelection& selection)
{
    double area = 0;
    for (const auto& loop : selection.loops.Get())
        for (std::size_t i = 0; i < loop.size(); ++i)
        {
            const auto a = loop[i], b = loop[(i + 1) % loop.size()];
            area += a.x * b.y - b.x * a.y;
        }
    return std::abs(area) / 2;
}
bool FreehandPixels(const DocumentSelection& selection, SelectionMode mode)
{
    const auto coverage = SelectionGeometry::Coverage(selection.loops.Get(), 12, 9, false);
    if (coverage.Width() != 12 || coverage.Height() != 9)
        return false;
    for (int y = 0; y < 9; ++y)
        for (int x = 0; x < 12; ++x)
        {
            const bool before = x >= 1 && x < 7 && y >= 2 && y < 6;
            const bool draw = x >= 4 && x < 10 && y >= 3 && y < 7;
            const bool expected = mode == SelectionMode::New   ? draw
                                  : mode == SelectionMode::Add ? before || draw
                                                               : before && !draw;
            if (coverage.Data()[static_cast<std::size_t>(y) * coverage.Stride() + x] != (expected ? 255 : 0))
                return false;
        }
    return true;
}
void FreehandNumeric(SelectionMode mode)
{
    scenarioName = "literal";
    const bool replace = mode == SelectionMode::New;
    auto f = std::make_unique<Fixture>(Initial::Old, false, LassoKind::Freehand, std::nullopt, mode, std::nullopt,
                                       replace ? Vec2{10, 7} : Vec2{4, 3});
    const auto original = f->session->Selection();
    const auto generation = f->session->Generation();
    if (replace)
    {
        f->Move({4, 7}, {}, 2);
        f->Move({4, 3}, {}, 3);
        f->Move({10, 3}, {}, 4);
    }
    else
    {
        f->Move({10, 3}, {}, 2);
        f->Move({10, 7}, {}, 3);
        f->Move({4, 7}, {}, 4);
    }
    f->End();
    f->Submit();
    f->session->lassoKind = LassoKind::Polygonal;
    f->session->selectionModeChoice = SelectionMode::New;
    Settle(f->jobs);
    const auto& state = *f->state;
    const auto& result = f->session->Selection();
    Expect(state.completed && state.outcome.status == JobStatus::Finished && state.drawing && state.closed &&
               state.sameTicket && state.captures == 1 && state.hits == (replace ? 1u : 0u) && state.inputAcknowledged,
           "captured-kind-mode-original-parent-and-one-source");
    const double expected = replace ? 24 : mode == SelectionMode::Add ? 39 : 15;
    Expect(result && result.value().antialiased && FreehandArea(result.value()) == expected &&
               FreehandPixels(result.value(), mode),
           "literal-area24-39-15-and-all108-pixel-centers");
    Expect(f->session->Generation() == generation + 1 && f->session->history.UndoCount() == 1 &&
               f->session->history.UndoName() == L"Lasso",
           "one-atomic-generation-and-lasso-history-entry");
    Expect(state.moves == 3 && state.maximumDrawingSamples == 3 && f->memory.queries == 1 &&
               state.firstBudget == f->admitted.value().limits.totalBytes &&
               state.openingBytes <= f->admitted.value().limits.continuingOutputBytes,
           "three-bounded-metadata-updates-no-readmission-and-original-opening-partition");
    f->session->Undo();
    Expect(f->session->Selection() == original && f->session->history.CanRedo(), "undo-restores-original-root");
    f->session->Redo();
    Expect(f->session->Selection() && FreehandPixels(f->session->Selection().value(), mode) &&
               f->session->Selection().value().antialiased,
           "redo-restores-complete-terminal-selection");
    f->ReleaseReaders();
    Expect(!f->jobs.ReservedMemoryBytes() && !f->jobs.ReleasingMemoryBytes(), "final-reader-refunds-original-ticket");
}
void FreehandCancellation(SelectionMode mode, bool joined)
{
    scenarioName = joined ? "joined-close" : "cancel-waiting";
    auto f = std::make_unique<Fixture>(Initial::Old, false, LassoKind::Freehand, std::nullopt, mode, std::nullopt,
                                       Vec2{10, 7});
    const auto original = f->session->Selection();
    const auto generation = f->session->Generation();
    const auto history = f->session->history.CaptureState();
    f->Move({4, 7}, {}, 2);
    f->Submit();
    Until(f->jobs, [&] { return f->state->waiting; });
    Expect(f->state->drawing && f->state->opened && f->session->PendingLasso() && f->state->moves == 1 &&
               f->jobs.ReservedMemoryBytes() >= f->admitted.value().limits.totalBytes,
           "live-original-waits-on-events-with-entire-admission-held");
    if (joined)
        f->jobs.Close();
    else
    {
        if (!f->jobs.Cancel(f->state->original))
            Broken("freehand-cancel");
        f->state->waiting = false;
        if (!f->jobs.Resume(f->state->original))
            Broken("freehand-cancel-resume");
        Settle(f->jobs);
    }
    Expect(f->state->outcome.status == JobStatus::Cancelled && f->state->aborted && f->state->completed == !joined &&
               !f->state->controller && f->state->inputAcknowledged,
           "cancel-or-joined-close-acknowledges-exact-original-and-aborts-writer");
    Expect(!f->prefix->Busy() && !f->session->PendingLasso() && f->session->Selection() == original &&
               f->session->Generation() == generation && f->session->history.IsCurrent(history),
           "no-partial-selection-history-or-generation-publication");
    f->ReleaseReaders();
    Expect(!f->jobs.ReservedMemoryBytes() && !f->jobs.ReleasingMemoryBytes(),
           "joined-final-readers-return-all-charges");
}
bool FreehandRectanglePixels(const DocumentSelection& selection)
{
    // Collinear vertices are permitted by the clipper contract. Test the complete literal rectangle,
    // independently of how many redundant edge vertices survive, with the serial coverage reference.
    const auto coverage = SelectionGeometry::Coverage(selection.loops.Get(), 36, 8, false);
    if (coverage.Width() != 36 || coverage.Height() != 8)
        return false;
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 36; ++x)
        {
            const bool expected = x >= 1 && x < 33 && y >= 1 && y < 5;
            if (coverage.Data()[static_cast<std::size_t>(y) * coverage.Stride() + x] != (expected ? 255 : 0))
                return false;
        }
    return true;
}
void FreehandMultiblock()
{
    scenarioName = "131-points";
    auto f = std::make_unique<Fixture>(Initial::Absent, false, LassoKind::Freehand, std::nullopt, SelectionMode::New,
                                       std::nullopt, Vec2{1, 1});
    for (std::uint64_t i = 1; i <= 128; ++i)
        f->Move({1.0 + .25 * i, 1}, {}, i + 1);
    f->Move({33, 5}, {}, 130);
    f->Move({1, 5}, {}, 131);
    f->End(false, 132);
    f->Submit();
    Settle(f->jobs);
    const auto& state = *f->state;
    const auto& result = f->session->Selection();
    Expect(state.completed && state.closed && state.outcome.status == JobStatus::Finished && state.moves == 130 &&
               state.sameTicket && state.captures == 1 && state.inputAcknowledged,
           "all130-physical-moves-complete-under-one-original-parent");
    Expect(result && result.value().Bounds() == std::optional(SelectionBounds{1, 1, 33, 5}) &&
               FreehandArea(result.value()) == 128 && FreehandRectanglePixels(result.value()),
           "two-sealed-blocks-and-inline-tail-produce-literal-area128");
    Expect(state.maximumDrawingSamples == 16 && state.collectAcks >= 8 && f->memory.queries == 1,
           "owner-continuations-cap-at16-and-rearm-only-known-pending-input");
    Expect(f->session->history.UndoCount() == 1 && f->session->history.UndoName() == L"Lasso" &&
               !f->session->PendingLasso(),
           "one-atomic-history-publication-after-worker-geometry");
    f->ReleaseReaders();
    Expect(!f->jobs.ReservedMemoryBytes() && !f->jobs.ReleasingMemoryBytes(),
           "multiblock-final-retirement-refunds-admission");
}
void FreehandInsideMove()
{
    scenarioName = "inside-move";
    auto f = std::make_unique<Fixture>(Initial::Old, false, LassoKind::Freehand, std::nullopt, SelectionMode::New,
                                       std::nullopt, Vec2{2, 3});
    f->Move({6, 5}, {}, 2);
    f->End(false, 3);
    f->Submit();
    Settle(f->jobs);
    const auto& state = *f->state;
    Expect(state.completed && state.closed && !state.drawing && state.hits == 1 && state.sameTicket &&
               state.outcome.status == JobStatus::Finished,
           "inside-new-keeps-exact-worker-hit-outline-move-branch");
    Expect(f->session->Selection() &&
               f->session->Selection().value().Bounds() == std::optional(SelectionBounds{5, 4, 11, 8}) &&
               !f->session->Selection().value().antialiased && f->session->history.UndoCount() == 1,
           "literal-offset4-2-preserves-original-selection-aa");
    Expect(!f->session->PendingLasso() && state.captures == 1 && state.inputAcknowledged && f->memory.queries == 1,
           "move-branch-never-opens-freehand-or-readmits");
    f->ReleaseReaders();
    Expect(!f->jobs.ReservedMemoryBytes() && !f->jobs.ReleasingMemoryBytes(),
           "move-branch-returns-complete-original-charge");
}
void FreehandShort(SelectionMode mode)
{
    scenarioName = "short";
    auto f = std::make_unique<Fixture>(Initial::Old, false, LassoKind::Freehand, std::nullopt, mode, std::nullopt,
                                       Vec2{10, 7});
    const auto original = f->session->Selection();
    const auto generation = f->session->Generation();
    f->End(false, 2);
    f->Submit();
    Settle(f->jobs);
    const bool clear = mode == SelectionMode::New;
    Expect(f->state->completed && f->state->closed && f->state->outcome.status == JobStatus::Finished &&
               f->state->drawing && !f->state->moves,
           "single-point-terminal-is-accepted-without-invented-up-sample");
    Expect(clear ? !f->session->Selection() : f->session->Selection() == original,
           "short-new-clears-and-preserving-modes-keep-original-selection");
    Expect(f->session->Generation() == generation + (clear ? 1u : 0u) &&
               f->session->history.UndoCount() == (clear ? 1 : 0),
           "short-terminal-preserves-noop-history-semantics");
    f->ReleaseReaders();
    Expect(!f->jobs.ReservedMemoryBytes() && !f->jobs.ReleasingMemoryBytes(), "short-terminal-final-charge-refund");
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

int RunMarqueeModeParentTests()
{
    checks = failures = 0;
    for (const auto kind : {LassoKind::Rectangle, LassoKind::Ellipse})
        for (const auto mode : {SelectionMode::Add, SelectionMode::Subtract})
        {
            family = kind == LassoKind::Rectangle
                         ? (mode == SelectionMode::Add ? "rectangle-add-parent" : "rectangle-subtract-parent")
                         : (mode == SelectionMode::Add ? "ellipse-add-parent" : "ellipse-subtract-parent");
            PreservingNumeric(kind, mode, Initial::Old, "inside-old");
            PreservingNumeric(kind, mode, Initial::Large, "large-prior");
            PreservingNumeric(kind, mode, Initial::Absent, "absent");
            PreservingNumeric(kind, mode, Initial::Empty, "empty");
            PreservingNumeric(kind, mode, Initial::NoLayers, "no-layers");
            CancelAndForeign(0, "cancel-waiting", kind, mode);
            CancelAndForeign(1, "joined-close", kind, mode);
            CancelAndForeign(2, "foreign-history", kind, mode);
            CancelAndForeign(3, "foreign-transform", kind, mode);
            CancelAndForeign(4, "cancel-prepared", kind, mode);
            RefusePreserving(kind, mode, true);
            RefusePreserving(kind, mode, false);
        }
    std::printf("SUMMARY marquee-mode-parent checks=%u failures=%u\n", checks, failures);
    return failures ? 1 : 0;
}

int RunFreehandGestureParentTests()
{
    checks = failures = 0;
    for (const auto mode : {SelectionMode::New, SelectionMode::Add, SelectionMode::Subtract})
    {
        family = mode == SelectionMode::New   ? "freehand-new-parent"
                 : mode == SelectionMode::Add ? "freehand-add-parent"
                                              : "freehand-subtract-parent";
        FreehandNumeric(mode);
        FreehandCancellation(mode, false);
        FreehandCancellation(mode, true);
        FreehandShort(mode);
        CancelAndForeign(2, "foreign-history", LassoKind::Freehand, mode);
        CancelAndForeign(3, "foreign-transform", LassoKind::Freehand, mode);
        CancelAndForeign(4, "cancel-prepared", LassoKind::Freehand, mode);
        if (mode != SelectionMode::New)
        {
            RefusePreserving(LassoKind::Freehand, mode, true);
            RefusePreserving(LassoKind::Freehand, mode, false);
        }
    }
    family = "freehand-new-parent";
    FreehandMultiblock();
    FreehandInsideMove();
    std::printf("SUMMARY freehand-gesture-parent checks=%u failures=%u expected=110\n", checks, failures);
    return failures || checks != 110 ? 1 : 0;
}
