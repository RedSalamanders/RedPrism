#include "Document.Tests.PolygonalDrawingParent.h"
#include "../../RedPrism/PolygonalDrawingJob.h"
#include "../Support/Support.Tests.DocumentFixture.h"
#include "SelectionHitWork.h"

#include <array>
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
const char* scenario = "setup";
void Expect(bool value, const char* fact)
{
    ++checks;
    failures += value ? 0 : 1;
    std::printf("%s polygonal-drawing-parent.%s.%s\n", value ? "PASS" : "FAIL", scenario, fact);
}
[[noreturn]] void Broken(const char* fact)
{
    std::printf("FAIL polygonal-drawing-parent.setup.%s\n", fact);
    std::_Exit(1);
}
void Settle(JobScheduler& jobs)
{
    if (SettleJobs(jobs, 10000) != SettleResult::Settled)
        Broken("settle-original");
}
template <class Predicate> void Until(JobScheduler& jobs, Predicate ready)
{
    const auto deadline = GetTickCount64() + 10000;
    while (!ready())
    {
        if (PumpMessages().quit)
            Broken("unexpected-quit");
        (void)DrainCompletions(jobs);
        (void)RunUiActions(jobs);
        if (ready())
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
    bool scarce = false;
    unsigned queries = 0;
    static bool Sample(void* raw, MemorySnapshot& snapshot) noexcept
    {
        auto& self = *static_cast<Memory*>(raw);
        ++self.queries;
        const auto bytes = self.scarce ? 0 : kMemoryAdmissionHeadroomBytes + 2ull * 1024 * 1024 * 1024;
        snapshot = {bytes, bytes};
        return true;
    }
};
enum class Initial
{
    Absent,
    Empty,
    Prior
};
void Install(DocumentSession& session, Initial initial)
{
    DocumentFixture document;
    document.id = Guid::Generate();
    document.width = 12;
    document.height = 10;
    if (initial == Initial::Empty)
        document.selection = DocumentSelection{false, SelectionLoops{}};
    if (initial == Initial::Prior)
        document.selection =
            DocumentSelection{false, SelectionLoops{std::vector<SelectionLoop>{{{1, 1}, {5, 1}, {5, 5}, {1, 5}}}}};
    auto layer = ImageLayer::Blank(L"Polygon parent", {12, 10});
    const auto active = layer.id;
    document.layers.push_back(std::move(layer));
    session.ReplaceDocument(std::move(document).Seal(), active);
    session.history.Reset();
}
struct Probe final
{
    unsigned opened = 0, changed = 0, terminal = 0;
    bool gateFinish = false;
    std::atomic<bool> entered{false};
    DWORD owner = GetCurrentThreadId();
    wil::unique_handle proceed{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    static void Owner(void* raw, PolygonalDrawingState::Event event) noexcept
    {
        auto& self = *static_cast<Probe*>(raw);
        if (event == PolygonalDrawingState::Event::Opened)
            ++self.opened;
        else if (event == PolygonalDrawingState::Event::Changed)
            ++self.changed;
        else
            ++self.terminal;
    }
    static void Work(void* raw, PolygonalDrawingState::WorkStage stage) noexcept
    {
        auto& self = *static_cast<Probe*>(raw);
        if (!self.gateFinish || stage != PolygonalDrawingState::WorkStage::FinishPrepared)
            return;
        self.entered.store(true, std::memory_order_release);
        if (!PostThreadMessageW(self.owner, WM_APP + 0x6c8, 0, 0) ||
            WaitForSingleObject(self.proceed.get(), 10000) != WAIT_OBJECT_0)
            Broken("finish-gate");
    }
};
struct Fixture final
{
    Memory memory;
    JobScheduler jobs;
    std::unique_ptr<DocumentSession> session = std::make_unique<DocumentSession>();
    std::unique_ptr<HostLassoInput> input = std::make_unique<HostLassoInput>();
    std::optional<PolygonalDrawingPlan> plan;
    std::unique_ptr<PolygonalDrawingState> state;
    Probe probe;
    explicit Fixture(Initial initial = Initial::Absent, SelectionMode mode = SelectionMode::New, bool scarce = false,
                     bool forgedSource = false)
    {
        Install(*session, initial);
        memory.scarce = scarce;
        if (!probe.proceed || jobs.Initialize(2, {}, {Memory::Sample, &memory}) != S_OK ||
            !jobs.EnableDeferredRelease())
            Broken("scheduler");
        plan = DescribePolygonalDrawing(*session, mode);
        if (!plan)
            Broken("plan");
        HostLassoInput::Context arrival;
        arrival.document = session->CaptureDocumentReadAuthority().value();
        if (forgedSource)
            ++arrival.document.generation;
        arrival.active = session->ActiveLayerID();
        arrival.crop = session->CropFrame();
        arrival.mask = session->IsMaskSelected();
        arrival.ordinal = 1;
        arrival.viewEpoch = 7;
        arrival.kind = LassoKind::Polygonal;
        arrival.mode = mode;
        if (!input->Begin(81, arrival, {2, 2}))
            Broken("down");
        state = std::make_unique<PolygonalDrawingState>(jobs, *session, *input, 81, plan.value(), false);
        state->observer = &probe;
        state->notify = Probe::Owner;
        state->workObserver = &probe;
        state->checkpoint = Probe::Work;
    }
    ~Fixture()
    {
        (void)SetEvent(probe.proceed.get());
        if (state && state->controller)
            jobs.Close();
        Release();
        jobs.Close();
    }
    void Release()
    {
        if (!session)
            return;
        session->CancelLasso();
        state.reset();
        input.reset();
        session.reset();
        Settle(jobs);
    }
    void Submit()
    {
        if (PolygonalDrawingJob::Submit(*state) != S_OK)
            Broken("submit");
    }
    void Point(Vec2 point, std::uint64_t ordinal)
    {
        if (input->Point(81, point, ordinal) != HostLassoInput::Admission::Recorded)
            Broken("point");
    }
    void Press(Vec2 point, std::uint64_t ordinal, bool nearFirst = false, bool twice = false, bool antialiased = true,
               std::uint64_t intent = 0)
    {
        if (input->PolygonPress(81, point, nearFirst, twice, antialiased, intent, ordinal) !=
            HostLassoInput::Admission::Recorded)
            Broken("press");
    }
    void Rectangle()
    {
        Point({8, 2}, 2);
        Point({8, 7}, 3);
        Point({2, 7}, 4);
    }
    void Finish(bool antialiased = true, std::uint64_t ordinal = 10, bool lost = false)
    {
        if (!state->controller || !state->controller->Finish(antialiased, ordinal, lost))
            Broken("finish");
    }
    void Wake()
    {
        if (!state->controller || !state->controller->NotifyInput())
            Broken("wake");
    }
};
bool Pixels(const DocumentSession& session, SelectionMode mode)
{
    const auto& selected = session.Selection();
    if (!selected)
        return false;
    for (int y = 0; y < 10; ++y)
        for (int x = 0; x < 12; ++x)
        {
            const bool shape = x >= 2 && x < 8 && y >= 2 && y < 7;
            const bool prior = x >= 1 && x < 5 && y >= 1 && y < 5;
            const bool wanted = mode == SelectionMode::New   ? shape
                                : mode == SelectionMode::Add ? prior || shape
                                                             : prior && !shape;
            const auto actual = ContainsSelectionForWork(SelectionDisplayValue{selected}, {x + 0.5, y + 0.5}, {});
            if (!actual || actual.value() != wanted)
                return false;
        }
    return true;
}
void Numeric(Initial initial, SelectionMode mode, const char* name, bool conditional = false)
{
    scenario = name;
    auto f = std::make_unique<Fixture>(initial, mode);
    const auto before = f->session->Selection();
    const auto generation = f->session->Generation();
    f->Rectangle();
    if (conditional)
        f->Press({11, 9}, 9, true, false, true, 91);
    f->Submit();
    if (!conditional)
        f->Finish();
    f->session->lassoKind = LassoKind::Freehand;
    f->session->selectionAntialiased = false;
    Settle(f->jobs);
    const auto& state = *f->state;
    Expect(state.completed && state.published && state.outcome.status == JobStatus::Finished && state.sameTicket &&
               state.captures == 1 && state.inputAcknowledged && !f->input->Busy() && !state.controller,
           "one-original-ticket-publishes-and-acknowledges-once");
    Expect(Pixels(*f->session, mode) && f->session->Selection() && f->session->Selection().value().antialiased,
           "all-120-literal-centers-and-terminal-aa-match");
    Expect(f->session->Generation() == generation + 1 && f->session->history.UndoCount() == 1 &&
               f->session->history.CaptureState().Depth() == 0 && f->session->history.UndoName() == L"Polygonal Lasso",
           "single-atomic-history-and-generation");
    Expect(state.closingOrdinal == (conditional ? 9u : 10u) && state.closingIntent == (conditional ? 91u : 0u) &&
               f->probe.opened == 1 && f->probe.terminal == 1,
           "physical-close-identity-survives-selected-terminal-normalization");
    Expect(f->memory.queries == 1 && state.firstBudget == f->plan.value().TotalBytes() && state.openingAllocation > 0 &&
               state.openingAllocation <= f->plan.value().OpeningBytes() &&
               state.finishBudget + state.openingAllocation == state.firstBudget && state.maximumSamples <= 16,
           "one-admission-partitions-original-memory-through-finish");
    f->session->Undo();
    Expect(f->session->Selection() == before && f->session->history.CanRedo(), "undo-restores-prior-root");
    f->session->Redo();
    Expect(Pixels(*f->session, mode), "redo-restores-independent-pixels");
    f->Release();
    Expect(f->jobs.ReservedMemoryBytes() == 0 && f->jobs.ReleasingMemoryBytes() == 0,
           "last-reader-retirement-refunds-original-charge");
}
void Parked()
{
    scenario = "parked-hover-backspace";
    auto f = std::make_unique<Fixture>();
    f->Submit();
    Until(f->jobs, [&] { return f->state->waiting; });
    const auto runs = f->state->runs;
    (void)DrainCompletions(f->jobs);
    (void)RunUiActions(f->jobs);
    Expect(f->state->runs == runs && f->probe.opened == 1 && f->session->PendingLasso() &&
               f->session->CaptureBoundedLassoDisplayForWork() &&
               !f->session->CaptureBoundedLassoDisplayForWork().value().display.antialiased,
           "captured-down-aa-and-event-driven-park");
    f->Rectangle();
    if (f->input->Cursor(81, Vec2{11, 9}, 5) != HostLassoInput::Admission::Recorded ||
        f->input->Backspace(81, 6) != HostLassoInput::Admission::Recorded ||
        f->input->Cursor(81, std::nullopt, 7) != HostLassoInput::Admission::Recorded)
        Broken("cursor-backspace");
    f->Point({2, 7}, 8);
    f->Wake();
    Until(f->jobs, [&] { return f->state->waiting && f->state->backspaces == 1; });
    Expect(f->state->cursors == 2 && f->session->PendingLasso().value().PointCount() == 4 &&
               !f->session->PendingLasso().value().cursor && f->session->history.UndoCount() == 0,
           "hover-never-adds-corner-and-pop-refill-preserves-order");
    f->Finish(false);
    Settle(f->jobs);
    Expect(f->state->published && Pixels(*f->session, SelectionMode::New) &&
               !f->session->Selection().value().antialiased && f->state->sameTicket,
           "resumed-original-parent-publishes-terminal-aa");
    f->Release();
    Expect(f->jobs.ReservedMemoryBytes() == 0, "parked-parent-retirement-refunds");
}
void Loss(int kind, const char* name)
{
    scenario = name;
    auto f = std::make_unique<Fixture>(Initial::Prior, SelectionMode::Add);
    const auto before = f->session->Selection();
    const auto history = f->session->history.CaptureState();
    const auto generation = f->session->Generation();
    f->Rectangle();
    f->probe.gateFinish = kind == 3;
    f->Submit();
    if (kind == 3)
    {
        f->Finish();
        Until(f->jobs, [&] { return f->probe.entered.load(std::memory_order_acquire); });
        Expect(!f->state->published && !f->session->PendingLasso() && f->session->history.IsCurrent(history),
               "prepared-worker-result-has-no-early-owner-publication");
        if (!f->jobs.Cancel(f->state->original))
            Broken("cancel-running");
        (void)SetEvent(f->probe.proceed.get());
    }
    else
    {
        Until(f->jobs, [&] { return f->state->waiting; });
        Expect(f->jobs.ReservedMemoryBytes() >= f->plan.value().TotalBytes(),
               "parked-original-retains-complete-reservation");
        if (kind == 0)
            f->Finish(true, 10, true);
        else if (kind == 1)
        {
            if (!f->jobs.Cancel(f->state->original) || !f->jobs.Resume(f->state->original))
                Broken("cancel-parked");
        }
        else
            f->jobs.Close();
    }
    if (kind != 2)
        Settle(f->jobs);
    Expect(f->state->outcome.status == JobStatus::Cancelled && !f->state->published && f->state->inputAcknowledged &&
               !f->state->controller && !f->input->Busy() && f->session->Selection() == before &&
               f->session->Generation() == generation && f->session->history.IsCurrent(history),
           "cancel-preserves-exact-source-history-and-drops-own-input");
    Expect(!f->session->PendingLasso() && f->probe.terminal == (kind == 2 ? 0u : 1u),
           "joined-close-suppresses-useful-callback-and-clears-owned-draft");
    f->Release();
    Expect(f->jobs.ReservedMemoryBytes() == 0 && f->jobs.ReleasingMemoryBytes() == 0,
           "cancelled-readers-refund-original-accounting");
}
void Refusals()
{
    scenario = "admission-refusals";
    auto session = std::make_unique<DocumentSession>();
    Install(*session, Initial::Prior);
    const auto history = session->history.CaptureState();
    Expect(!DescribePolygonalDrawing(*session, SelectionMode::New) && session->history.IsCurrent(history),
           "new-with-nonempty-prior-needs-separate-containment-dispatcher");
    Expect(!DescribePolygonalDrawing(*session, static_cast<SelectionMode>(99)), "invalid-mode-refuses");
    auto f = std::make_unique<Fixture>(Initial::Absent, SelectionMode::New, true);
    const auto authority = f->session->CaptureDocumentReadAuthority();
    // Descriptor admission precedes the sampler's dispatch refusal. No useful body/source capture runs.
    if (PolygonalDrawingJob::Submit(*f->state) != S_OK)
        Broken("scarce-descriptor-submit");
    Settle(f->jobs);
    Expect(f->state->completed && f->state->outcome.status == JobStatus::Failed &&
               f->state->outcome.error == E_OUTOFMEMORY && !f->state->controller && f->state->runs == 0 &&
               f->state->captures == 0 && f->state->inputAcknowledged && !f->input->Busy() &&
               f->session->CaptureDocumentReadAuthority() == authority && !f->session->PendingLasso(),
           "scarce-memory-fails-before-source-capture-without-dangling-controller");
    f.reset(); // Joined scheduler lifetime uninstalls its thread-local release sink before the next fixture.
    scenario = "forged-descriptor";
    auto forged = std::make_unique<Fixture>(Initial::Absent, SelectionMode::New, false, true);
    forged->Submit();
    Settle(forged->jobs);
    Expect(forged->state->completed && forged->state->outcome.status == JobStatus::Failed &&
               forged->state->captures == 0 && forged->state->inputAcknowledged && !forged->input->Busy(),
           "all-captured-source-scalars-must-match-before-opening");
    forged->Release();
    Expect(forged->jobs.ReservedMemoryBytes() == 0, "pre-capture-refusal-refunds");
}
void BackspaceEnds()
{
    scenario = "last-corner-backspace";
    auto f = std::make_unique<Fixture>(Initial::Prior, SelectionMode::Add);
    const auto before = f->session->Selection();
    const auto history = f->session->history.CaptureState();
    if (f->input->Backspace(81, 2) != HostLassoInput::Admission::Recorded)
        Broken("last-backspace");
    f->Submit();
    Settle(f->jobs);
    Expect(f->state->completed && f->state->endedByBackspace && !f->state->published &&
               f->state->outcome.status == JobStatus::Finished && !f->session->PendingLasso() &&
               f->session->Selection() == before && f->session->history.IsCurrent(history),
           "last-pop-ends-own-draft-without-history");
    Expect(f->state->inputAcknowledged && !f->input->Busy() && f->probe.terminal == 1,
           "last-pop-drops-only-its-original-prefix");
    f->Release();
    Expect(f->jobs.ReservedMemoryBytes() == 0, "last-pop-retirement-refunds");
}
void ForeignHistory()
{
    scenario = "foreign-history-begin";
    auto f = std::make_unique<Fixture>(Initial::Prior, SelectionMode::Add);
    const auto before = f->session->Selection();
    f->Submit();
    Until(f->jobs, [&] { return f->state->waiting; });
    f->session->history.Begin(L"Foreign transaction", f->session->Document(), f->session->ActiveLayerID());
    const auto foreign = f->session->history.CaptureState();
    const auto* draft = &f->session->PendingLasso().value();
    f->Point({8, 2}, 2);
    f->Wake();
    Settle(f->jobs);
    Expect(f->state->outcome.status == JobStatus::Failed && !f->state->published &&
               f->session->history.IsCurrent(foreign) && f->session->Selection() == before &&
               f->session->PendingLasso() && &f->session->PendingLasso().value() == draft,
           "stale-job-does-not-touch-foreign-history-or-writer");
    Expect(f->state->inputAcknowledged && !f->input->Busy() && f->probe.terminal == 1,
           "stale-original-prefix-is-dropped-once");
    f->Release();
    Expect(f->jobs.ReservedMemoryBytes() == 0, "foreign-owner-last-reader-stays-charged-until-release");
}
void Conditional(bool earlyNear, const char* name)
{
    scenario = name;
    auto f = std::make_unique<Fixture>();
    f->Press({8, 2}, 2, earlyNear);
    f->Press({8, 7}, 3);
    f->Press({2, 7}, 4, earlyNear, !earlyNear, false, 94);
    f->Submit();
    Settle(f->jobs);
    bool pixels = true;
    unsigned centers = 0;
    const auto& selection = f->session->Selection();
    for (int y = 0; y < 10; ++y)
        for (int x = 0; x < 12; ++x)
        {
            const bool wanted = x >= 2 && x < 8 && y >= 2 && y < 7 && 5 * (2 * x - 3) > 6 * (2 * y - 3);
            centers += wanted ? 1 : 0;
            const auto actual = ContainsSelectionForWork(SelectionDisplayValue{selection}, {x + 0.5, y + 0.5}, {});
            pixels = pixels && actual && actual.value() == wanted;
        }
    Expect(f->state->published && selection && !selection.value().antialiased && pixels && centers == 15,
           "closing-press-is-not-a-corner-in-120-independent-triangle-centers");
    Expect(f->state->points == 2 && f->state->closingOrdinal == 4 && f->state->closingIntent == 94 &&
               f->state->inputAcknowledged && !f->input->Busy() && f->session->history.UndoCount() == 1,
           "early-near-press-appends-and-selected-close-keeps-event-identity");
    f->Release();
    Expect(f->jobs.ReservedMemoryBytes() == 0, "conditional-parent-last-reader-refund");
}
void EarlierCloseWins()
{
    scenario = "earlier-close-wins";
    auto f = std::make_unique<Fixture>();
    f->Rectangle();
    f->Press({11, 9}, 5, true, false, false, 95);
    f->Submit();
    f->Finish(true, 10);
    Settle(f->jobs);
    Expect(f->state->published && Pixels(*f->session, SelectionMode::New) &&
               !f->session->Selection().value().antialiased && f->state->closingOrdinal == 5 &&
               f->state->closingIntent == 95 && f->state->points == 3,
           "older-conditional-close-beats-younger-explicit-finish-and-aa");
    Expect(f->state->inputAcknowledged && !f->input->Busy() && f->probe.terminal == 1 &&
               f->session->history.UndoCount() == 1,
           "only-one-terminal-and-history-publication");
    f->Release();
    Expect(f->jobs.ReservedMemoryBytes() == 0, "selected-terminal-retires-original-charge");
}
void Burst(bool overflow, const char* name)
{
    scenario = name;
    auto f = std::make_unique<Fixture>();
    const auto history = f->session->history.CaptureState();
    if (overflow)
    {
        for (std::uint64_t i = 1; i < HostLassoInput::kCapacity; ++i)
            f->Point({2.0 + static_cast<double>(i % 6), 2}, i + 1);
        if (f->input->Point(81, {8, 7}, 600) != HostLassoInput::Admission::Overflow)
            Broken("overflow");
    }
    else
    {
        for (std::uint64_t i = 1; i <= 64; ++i)
            f->Point({2 + 6.0 * static_cast<double>(i) / 64, 2}, i + 1);
        f->Point({8, 7}, 66);
        f->Point({2, 7}, 67);
    }
    f->Submit();
    if (!overflow)
        f->Finish(true, 1000);
    Settle(f->jobs);
    Expect(f->state->completed && f->state->sameTicket && f->state->maximumSamples == 16 && f->state->runs >= 6 &&
               f->state->captures == 1 && f->memory.queries == 1,
           "bounded-16-sample-continuations-use-one-admission-and-original-ticket");
    if (overflow)
        Expect(!f->state->published && f->state->outcome.status == JobStatus::Cancelled && f->state->points == 511 &&
                   f->session->history.IsCurrent(history) && !f->session->Selection(),
               "full-ring-retains-accepted-prefix-and-loss-never-publishes-partial-shape");
    else
        Expect(f->state->published && Pixels(*f->session, SelectionMode::New) && f->state->points == 66 &&
                   f->session->history.UndoCount() == 1,
               "all-accepted-collinear-points-replay-before-independent-pixel-result");
    Expect(f->state->inputAcknowledged && !f->input->Busy() && f->probe.terminal == 1,
           "bounded-burst-acknowledges-original-terminal-once");
    f->Release();
    Expect(f->jobs.ReservedMemoryBytes() == 0, "bounded-burst-last-reader-refund");
}
void OrdinalRefusal(bool terminal, const char* name)
{
    scenario = name;
    auto f = std::make_unique<Fixture>();
    const auto history = f->session->history.CaptureState();
    f->Rectangle();
    if (!terminal)
        f->Point({3, 7}, 3);
    f->Submit();
    f->Finish(true, terminal ? 4u : 10u);
    Settle(f->jobs);
    Expect(f->state->outcome.status == JobStatus::Failed && f->state->outcome.error == E_INVALIDARG &&
               !f->state->published && !f->session->Selection() && f->session->history.IsCurrent(history),
           "nonincreasing-physical-order-refuses-without-history");
    Expect(f->state->inputAcknowledged && !f->input->Busy() && !f->session->PendingLasso(),
           "invalid-order-drops-only-own-prefix-and-draft");
    f->Release();
    Expect(f->jobs.ReservedMemoryBytes() == 0, "invalid-order-retirement-refunds");
}
void PreRecordedTerminal(bool lost, const char* name)
{
    scenario = name;
    auto f = std::make_unique<Fixture>();
    f->Rectangle();
    if (f->input->Finish(81, false, lost) != HostLassoInput::Admission::Recorded)
        Broken("physical-terminal-before-admission");
    f->state = std::make_unique<PolygonalDrawingState>(f->jobs, *f->session, *f->input, 81, f->plan.value(), true, 10);
    f->state->observer = &f->probe;
    f->state->notify = Probe::Owner;
    f->Submit();
    Expect(!f->state->controller->Finish(true, 11, !lost), "already-accepted-terminal-rejects-younger-opposite-loss");
    Settle(f->jobs);
    Expect(f->state->completed && f->state->sameTicket && f->state->inputAcknowledged && !f->input->Busy() &&
               !f->state->controller && f->state->closingOrdinal == 10,
           "pre-recorded-physical-order-survives-original-controller-admission");
    Expect(lost ? f->state->outcome.status == JobStatus::Cancelled && !f->state->published &&
                      !f->session->Selection() && f->session->history.UndoCount() == 0
                : f->state->published && f->state->outcome.status == JobStatus::Finished &&
                      Pixels(*f->session, SelectionMode::New) && !f->session->Selection().value().antialiased &&
                      f->session->history.UndoCount() == 1,
           "accepted-terminal-aa-and-loss-policy-govern-publication");
    f->Release();
    Expect(f->jobs.ReservedMemoryBytes() == 0 && f->jobs.ReleasingMemoryBytes() == 0,
           "pre-recorded-terminal-last-reader-refund");
}
void ContradictoryPreRecordedTerminal(bool stillSampling, const char* name)
{
    scenario = name;
    auto f = std::make_unique<Fixture>();
    const auto authority = f->session->CaptureDocumentReadAuthority();
    const auto history = f->session->history.CaptureState();
    f->Rectangle();
    if (!stillSampling && f->input->Finish(81, false) != HostLassoInput::Admission::Recorded)
        Broken("contradictory-recorded-terminal");
    f->state = std::make_unique<PolygonalDrawingState>(f->jobs, *f->session, *f->input, 81, f->plan.value(), true,
                                                       stillSampling ? 10u : 1u);
    f->state->observer = &f->probe;
    f->state->notify = Probe::Owner;
    f->Submit();
    Settle(f->jobs);
    Expect(f->state->outcome.status == JobStatus::Failed && f->state->outcome.error == E_ABORT &&
               f->state->captures == 0 && !f->state->published && !f->session->PendingLasso() &&
               f->session->history.IsCurrent(history),
           "contradictory-terminal-descriptor-refuses-before-source-capture");
    Expect(f->session->CaptureDocumentReadAuthority() == authority && f->state->inputAcknowledged &&
               !f->input->Busy() && f->probe.opened == 0 && f->probe.terminal == 1,
           "descriptor-refusal-preserves-source-and-drops-only-original-prefix");
    f->Release();
    Expect(f->jobs.ReservedMemoryBytes() == 0 && f->jobs.ReleasingMemoryBytes() == 0,
           "descriptor-refusal-retirement-refunds");
}

struct HostGates final
{
    struct Decision final
    {
        std::uint64_t ordinal = 0, intent = 0;
        bool closes = false;
    };
    std::uint64_t limit = 1000, terminalOrdinal = 0, terminalIntent = 0;
    unsigned replayQueries = 0, terminalQueries = 0, decisions = 0;
    bool terminalAllowed = false, terminalCancelled = false, wrongThread = false;
    DWORD owner = GetCurrentThreadId();
    std::array<Decision, 4> resolved{};
    static bool Replay(void* raw, std::uint64_t ordinal) noexcept
    {
        auto& self = *static_cast<HostGates*>(raw);
        self.wrongThread = self.wrongThread || self.owner != GetCurrentThreadId();
        ++self.replayQueries;
        return ordinal <= self.limit;
    }
    static bool Terminal(void* raw, std::uint64_t ordinal, std::uint64_t intent, bool cancelled) noexcept
    {
        auto& self = *static_cast<HostGates*>(raw);
        self.wrongThread = self.wrongThread || self.owner != GetCurrentThreadId();
        ++self.terminalQueries;
        self.terminalOrdinal = ordinal;
        self.terminalIntent = intent;
        self.terminalCancelled = cancelled;
        return self.terminalAllowed;
    }
    static void Resolve(void* raw, std::uint64_t ordinal, std::uint64_t intent, bool closes) noexcept
    {
        auto& self = *static_cast<HostGates*>(raw);
        self.wrongThread = self.wrongThread || self.owner != GetCurrentThreadId();
        if (self.decisions >= self.resolved.size())
            Broken("fixed-close-decisions");
        self.resolved[self.decisions++] = {ordinal, intent, closes};
    }
    void Bind(PolygonalDrawingState& state) noexcept
    {
        state.hostObserver = this;
        state.mayReplay = Replay;
        state.mayFinish = Terminal;
        state.pressResolved = Resolve;
    }
};
void HostOrderingAndSavedMarker()
{
    scenario = "host-ordering-save-marker";
    HostGates gate;
    auto f = std::make_unique<Fixture>();
    gate.limit = 2;
    gate.Bind(*f->state);
    const auto history = f->session->history.CaptureState();
    const auto revision = f->session->history.CurrentRevision();
    f->Rectangle();
    f->Submit();
    Until(f->jobs,
          [&] { return f->state->waiting && f->state->waitReason == PolygonalDrawingState::WaitReason::Ordinal; });
    Expect(f->state->points == 1 && f->input->PendingCount() == 2 &&
               f->session->PendingLasso().value().PointCount() == 2 && f->session->history.IsCurrent(history),
           "older-host-boundary-leaves-younger-corners-unconsumed");
    const auto ticket = f->state->original;
    const auto runs = f->state->runs;
    const auto queries = gate.replayQueries;
    (void)DrainCompletions(f->jobs);
    (void)RunUiActions(f->jobs);
    Expect(f->state->runs == runs && gate.replayQueries == queries && f->state->controller &&
               f->jobs.ReservedMemoryBytes() >= f->plan.value().TotalBytes(),
           "parked-boundary-holds-original-charge-without-polling");
    f->session->history.MarkSaved(revision);
    Expect(!f->session->history.IsModified() && f->session->history.IsCurrent(history) &&
               f->session->PendingLasso().value().PointCount() == 2 && !f->session->Selection(),
           "saved-committed-revision-does-not-end-live-draft-or-mutate-history");
    gate.limit = 4;
    f->Wake();
    Until(f->jobs,
          [&] { return f->state->waiting && f->state->waitReason == PolygonalDrawingState::WaitReason::Input; });
    Expect(f->state->points == 3 && f->input->PendingCount() == 0 &&
               f->session->PendingLasso().value().PointCount() == 4 && f->state->original == ticket,
           "owner-prerequisite-notification-resumes-same-parent-after-save");
    f->Finish(false);
    Until(f->jobs,
          [&] { return f->state->waiting && f->state->waitReason == PolygonalDrawingState::WaitReason::Terminal; });
    Expect(f->state->finishBudget == 0 && f->input->PendingCount() == 1 && f->session->PendingLasso() &&
               f->session->history.IsCurrent(history) && f->state->captures == 1 && !f->state->completed,
           "terminal-stays-unconsumed-before-close-and-finish-history-capture");
    Expect(gate.terminalOrdinal == 10 && gate.terminalIntent == 0 && !gate.terminalCancelled && !gate.wrongThread,
           "readiness-observes-physical-terminal-only-on-owner");
    gate.terminalAllowed = true;
    f->Wake();
    Settle(f->jobs);
    Expect(f->state->published && f->state->sameTicket && f->state->original == ticket && f->memory.queries == 1 &&
               f->session->history.UndoCount() == 1 && f->session->history.IsModified() &&
               Pixels(*f->session, SelectionMode::New) && !f->session->Selection().value().antialiased,
           "ordered-finish-publishes-once-on-original-admission-with-terminal-aa");
    f->session->Undo();
    Expect(f->session->history.CurrentRevision() == revision && !f->session->history.IsModified() &&
               !f->session->Selection(),
           "undo-retains-save-marker-written-while-polygon-was-open");
    f->session->Redo();
    Expect(f->session->history.IsModified() && Pixels(*f->session, SelectionMode::New),
           "redo-restores-finish-and-preserves-saved-revision");
    f->Release();
    Expect(f->jobs.ReservedMemoryBytes() == 0 && f->jobs.ReleasingMemoryBytes() == 0,
           "host-boundary-last-reader-refunds-original-accounting");
}
void HostProvisionalDecision()
{
    scenario = "host-provisional-close";
    HostGates gate;
    auto f = std::make_unique<Fixture>();
    gate.Bind(*f->state);
    f->Point({8, 2}, 2);
    f->Press({8, 7}, 3, true, false, true, 93);
    f->Press({2, 7}, 4);
    f->Press({11, 9}, 5, true, false, false, 95);
    f->Point({11, 9}, 6);
    f->Submit();
    Until(f->jobs,
          [&] { return f->state->waiting && f->state->waitReason == PolygonalDrawingState::WaitReason::Terminal; });
    Expect(gate.decisions == 2 && gate.resolved[0].ordinal == 3 && gate.resolved[0].intent == 93 &&
               !gate.resolved[0].closes && gate.resolved[1].ordinal == 5 && gate.resolved[1].intent == 95 &&
               gate.resolved[1].closes && !gate.wrongThread,
           "provisional-requests-resolve-once-after-older-corners-with-distinct-identities");
    Expect(f->state->points == 3 && f->session->PendingLasso().value().PointCount() == 4 &&
               f->state->closingOrdinal == 5 && f->state->closingIntent == 95 && f->input->PendingCount() == 1,
           "closing-press-is-never-appended-and-younger-corners-are-discarded");
    const auto runs = f->state->runs;
    f->Wake();
    Until(f->jobs, [&] { return f->state->waiting; });
    Expect(gate.decisions == 2 && f->state->runs == runs + 1 && f->state->finishBudget == 0 &&
               gate.terminalOrdinal == 5 && gate.terminalIntent == 95 && !f->state->published,
           "explicit-prerequisite-wake-rechecks-readiness-without-replaying-close-decision");
    gate.terminalAllowed = true;
    f->Wake();
    Settle(f->jobs);
    Expect(f->state->published && Pixels(*f->session, SelectionMode::New) &&
               !f->session->Selection().value().antialiased && f->session->history.UndoCount() == 1,
           "selected-terminal-keeps-physical-aa-and-independent-rectangle-result");
    Expect(f->state->sameTicket && f->state->captures == 1 && f->memory.queries == 1 && f->state->inputAcknowledged &&
               !f->input->Busy() && f->probe.terminal == 1,
           "provisional-arbitration-never-readmits-or-duplicates-terminal");
    f->Release();
    Expect(f->jobs.ReservedMemoryBytes() == 0 && f->jobs.ReleasingMemoryBytes() == 0,
           "provisional-original-charge-retires-after-last-reader");
}
void HostBarrierEnd(int kind, const char* name)
{
    scenario = name;
    HostGates gate;
    auto f = std::make_unique<Fixture>(Initial::Prior, SelectionMode::Add);
    gate.Bind(*f->state);
    const auto before = f->session->Selection();
    f->Rectangle();
    f->Submit();
    f->Finish();
    Until(f->jobs,
          [&] { return f->state->waiting && f->state->waitReason == PolygonalDrawingState::WaitReason::Terminal; });
    Expect(f->session->PendingLasso() && f->state->finishBudget == 0 && f->input->PendingCount() == 1 &&
               f->jobs.ReservedMemoryBytes() >= f->plan.value().TotalBytes(),
           "gated-terminal-holds-live-writer-and-original-reservation");
    if (kind == 2)
    {
        f->session->history.Begin(L"Foreign gated transaction", f->session->Document(), f->session->ActiveLayerID());
        gate.terminalAllowed = true;
        f->Wake();
    }
    else if (kind == 1)
        f->jobs.Close();
    else if (!f->jobs.Cancel(f->state->original) || !f->jobs.Resume(f->state->original))
        Broken("gated-cancel");
    const auto foreign = f->session->history.CaptureState();
    if (kind != 1)
        Settle(f->jobs);
    Expect(!f->state->published && f->session->Selection() == before && f->session->history.IsCurrent(foreign) &&
               (kind == 2 ? f->state->outcome.status == JobStatus::Failed && f->state->outcome.error == E_ABORT &&
                                f->session->PendingLasso().has_value()
                          : f->state->outcome.status == JobStatus::Cancelled && !f->session->PendingLasso()),
           "gated-cancel-shutdown-or-staleness-preserves-source-and-foreign-history");
    Expect(f->state->inputAcknowledged && !f->input->Busy() && !f->state->controller &&
               f->probe.terminal == (kind == 1 ? 0u : 1u),
           "original-input-drops-once-and-joined-close-suppresses-useful-callback");
    f->Release();
    Expect(f->jobs.ReservedMemoryBytes() == 0 && f->jobs.ReleasingMemoryBytes() == 0,
           "gated-terminal-all-owners-refund-on-final-retirement");
}
void HostGatedLoss()
{
    scenario = "host-gated-loss";
    HostGates gate;
    auto f = std::make_unique<Fixture>();
    gate.Bind(*f->state);
    const auto history = f->session->history.CaptureState();
    f->Rectangle();
    f->Submit();
    f->Finish(false, 10, true);
    Until(f->jobs,
          [&] { return f->state->waiting && f->state->waitReason == PolygonalDrawingState::WaitReason::Terminal; });
    Expect(gate.terminalCancelled && gate.terminalOrdinal == 10 && gate.terminalIntent == 0 && !gate.wrongThread,
           "loss-readiness-retains-accepted-physical-terminal-identity");
    Expect(f->input->PendingCount() == 1 && f->session->PendingLasso() && !f->state->completed &&
               !f->state->controller->Finish(true, 11),
           "gated-loss-remains-original-and-refuses-younger-successful-finish");
    gate.terminalAllowed = true;
    f->Wake();
    Settle(f->jobs);
    Expect(f->state->outcome.status == JobStatus::Cancelled && !f->state->published && !f->session->PendingLasso() &&
               !f->session->Selection() && f->session->history.IsCurrent(history),
           "released-loss-cancels-own-draft-without-partial-selection-or-history");
    Expect(f->state->inputAcknowledged && !f->input->Busy() && f->state->sameTicket && f->memory.queries == 1 &&
               f->probe.terminal == 1,
           "gated-loss-acknowledges-and-retires-one-original-operation");
    f->Release();
    Expect(f->jobs.ReservedMemoryBytes() == 0 && f->jobs.ReleasingMemoryBytes() == 0,
           "gated-loss-final-reader-refunds");
}
void HostInvalidOrdinal(bool terminal, const char* name)
{
    scenario = name;
    HostGates gate;
    auto f = std::make_unique<Fixture>();
    gate.Bind(*f->state);
    f->Rectangle();
    if (!terminal)
        f->Point({3, 7}, 3);
    f->Submit();
    f->Finish(true, terminal ? 4u : 10u);
    Settle(f->jobs);
    Expect(f->state->completed && f->state->outcome.status == JobStatus::Failed &&
               f->state->outcome.error == E_INVALIDARG && gate.terminalQueries == 0,
           "malformed-order-refuses-instead-of-parking-behind-readiness");
    Expect(!f->state->published && !f->session->Selection() && f->session->history.UndoCount() == 0 &&
               !f->input->Busy() && f->state->inputAcknowledged,
           "invalid-order-cannot-publish-or-strand-original-prefix");
    f->Release();
    Expect(f->jobs.ReservedMemoryBytes() == 0 && f->jobs.ReleasingMemoryBytes() == 0,
           "malformed-gated-operation-refunds");
}

} // namespace

int RunPolygonalDrawingParentTests()
{
    checks = failures = 0;
    Numeric(Initial::Absent, SelectionMode::New, "new-absent");
    Numeric(Initial::Empty, SelectionMode::New, "new-empty");
    Numeric(Initial::Prior, SelectionMode::Add, "add");
    Numeric(Initial::Prior, SelectionMode::Subtract, "subtract");
    Numeric(Initial::Absent, SelectionMode::New, "near-first-close", true);
    Parked();
    Loss(0, "pointer-loss");
    Loss(1, "cancel-parked");
    Loss(2, "joined-close-parked");
    Loss(3, "cancel-prepared");
    Refusals();
    BackspaceEnds();
    ForeignHistory();
    Conditional(false, "double-click-close");
    Conditional(true, "near-first-needs-three");
    EarlierCloseWins();
    Burst(false, "bounded-burst");
    Burst(true, "ring-overflow");
    OrdinalRefusal(false, "reordered-point");
    OrdinalRefusal(true, "reordered-terminal");
    std::printf("SUMMARY polygonal-drawing-parent checks=%u failures=%u\n", checks, failures);
    return static_cast<int>(failures);
}

int RunPolygonalPreRecordedTerminalTests()
{
    checks = failures = 0;
    PreRecordedTerminal(false, "pre-admission-up");
    PreRecordedTerminal(true, "pre-admission-lost");
    ContradictoryPreRecordedTerminal(true, "claimed-terminal-still-sampling");
    ContradictoryPreRecordedTerminal(false, "terminal-not-after-down");
    std::printf("SUMMARY polygonal-pre-recorded-terminal checks=%u failures=%u\n", checks, failures);
    return failures == 0 && checks == 14 ? 0 : (failures ? static_cast<int>(failures) : 1);
}

int RunPolygonalHostGateTests()
{
    checks = failures = 0;
    HostOrderingAndSavedMarker();
    HostProvisionalDecision();
    HostBarrierEnd(0, "host-gated-cancel");
    HostBarrierEnd(1, "host-gated-joined-close");
    HostBarrierEnd(2, "host-gated-foreign-history");
    HostGatedLoss();
    HostInvalidOrdinal(false, "host-gated-invalid-point");
    HostInvalidOrdinal(true, "host-gated-invalid-terminal");
    std::printf("SUMMARY polygonal-host-gates checks=%u failures=%u\n", checks, failures);
    return failures == 0 && checks == 39 ? 0 : (failures ? static_cast<int>(failures) : 1);
}
