#include "Document.Tests.PolygonalDispatcher.h"
#include "SelectionHitWork.h"
#include "SelectionOutlineGestureJob.h"
#include "SelectionOutlineGestureMemory.h"
#include "Tests/Support/Support.Tests.DocumentFixture.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <type_traits>
#include <vector>
#include <wil/resource.h>
#include <windows.h>

static_assert(!std::is_default_constructible_v<SelectionOutlineGestureTransition>);
static_assert(!std::is_copy_constructible_v<SelectionOutlineGestureTransition>);
static_assert(std::is_nothrow_move_constructible_v<SelectionOutlineGestureTransition>);

namespace
{
unsigned checks = 0, failures = 0;
const char* scenario = "setup";
void Expect(bool value, const char* fact)
{
    ++checks;
    failures += value ? 0 : 1;
    std::printf("%s polygonal-dispatcher.%s.%s\n", value ? "PASS" : "FAIL", scenario, fact);
}
[[noreturn]] void Broken(const char* fact)
{
    std::printf("FAIL polygonal-dispatcher.setup.%s\n", fact);
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
        const auto bytes = self.scarce ? 0 : kMemoryAdmissionHeadroomBytes + 2ull * 1024 * 1024 * 1024;
        value = {bytes, bytes};
        return true;
    }
};
void Install(DocumentSession& session)
{
    DocumentFixture document;
    document.id = Guid::Generate();
    document.width = 12;
    document.height = 10;
    document.selection =
        DocumentSelection{false, SelectionLoops{std::vector<SelectionLoop>{{{1, 1}, {5, 1}, {5, 5}, {1, 5}}}}};
    auto layer = ImageLayer::Blank(L"Polygon dispatcher", {12, 10});
    const auto active = layer.id;
    document.layers.push_back(std::move(layer));
    session.ReplaceDocument(std::move(document).Seal(), active);
    session.history.Reset();
}
// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027)
struct Probe final
{
    unsigned opened = 0, moved = 0, terminal = 0, corners = 0, closes = 0;
    bool replay = true, finish = true, gate = false, foreignHistory = false;
    bool acknowledgementVisible = false;
    std::uint64_t replayOrdinal = 0, finishOrdinal = 0, cornerOrdinal = 0;
    SelectionOutlineGestureState* state = nullptr;
    DocumentSession* session = nullptr;
    std::atomic<bool> entered{false};
    DWORD owner = GetCurrentThreadId();
    wil::unique_handle proceed{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    static void Owner(void* raw, SelectionOutlineGestureState::Event event) noexcept
    {
        auto& self = *static_cast<Probe*>(raw);
        if (event == SelectionOutlineGestureState::Event::Opened)
        {
            ++self.opened;
            if (self.foreignHistory)
                self.session->history.Begin(L"Foreign", self.session->Document(), self.session->ActiveLayerID());
        }
        else if (event == SelectionOutlineGestureState::Event::Moved)
            ++self.moved;
        else
        {
            ++self.terminal;
            self.acknowledgementVisible =
                self.state && !self.state->controller && self.state->parentAcknowledged.load(std::memory_order_acquire);
        }
    }
    static bool Replay(void* raw, std::uint64_t ordinal) noexcept
    {
        auto& self = *static_cast<Probe*>(raw);
        self.replayOrdinal = ordinal;
        return self.replay;
    }
    static bool Finish(void* raw, std::uint64_t ordinal, bool) noexcept
    {
        auto& self = *static_cast<Probe*>(raw);
        self.finishOrdinal = ordinal;
        return self.finish;
    }
    bool terminalAdmission = true, admissionBeforeCapture = true, progressOnOwner = true, progressMonotonic = true;
    unsigned terminalAdmissions = 0, progressNotifications = 0;
    std::uint64_t admissionOrdinal = 0, progressOrdinal = 0;
    static bool Admit(void* raw, std::uint64_t ordinal, bool) noexcept
    {
        auto& self = *static_cast<Probe*>(raw);
        ++self.terminalAdmissions;
        self.admissionOrdinal = ordinal;
        self.admissionBeforeCapture = self.admissionBeforeCapture && self.state && !self.state->closurePrepares &&
                                      !self.state->terminalOrdinal && GetCurrentThreadId() == self.owner;
        return self.terminalAdmission;
    }
    static void Progress(void* raw, std::uint64_t ordinal) noexcept
    {
        auto& self = *static_cast<Probe*>(raw);
        ++self.progressNotifications;
        self.progressOnOwner = self.progressOnOwner && GetCurrentThreadId() == self.owner;
        self.progressMonotonic = self.progressMonotonic && ordinal >= self.progressOrdinal;
        self.progressOrdinal = ordinal;
    }
    static void Press(void* raw, std::uint64_t ordinal, bool closes) noexcept
    {
        auto& self = *static_cast<Probe*>(raw);
        self.cornerOrdinal = ordinal;
        closes ? ++self.closes : ++self.corners;
    }
    static void Work(void* raw, SelectionOutlineGestureState::WorkStage stage) noexcept
    {
        auto& self = *static_cast<Probe*>(raw);
        if (!self.gate || stage != SelectionOutlineGestureState::WorkStage::DrawingPrepared)
            return;
        self.entered.store(true, std::memory_order_release);
        if (!PostThreadMessageW(self.owner, WM_APP + 0x6d1, 0, 0) ||
            WaitForSingleObject(self.proceed.get(), 10000) != WAIT_OBJECT_0)
            Broken("worker-gate");
    }
};
#pragma warning(pop)

// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4625 4626)
struct Fixture final
{
    Memory memory;
    JobScheduler jobs;
    std::unique_ptr<DocumentSession> session = std::make_unique<DocumentSession>();
    std::unique_ptr<HostPolygonalInputJournal> journal = std::make_unique<HostPolygonalInputJournal>();
    std::optional<SelectionOutlineGestureMemory> admitted;
    std::unique_ptr<SelectionOutlineGestureState> state;
    Probe probe;
    HostSelectionPressInput::Context arrival;
    explicit Fixture(Vec2 down = {7, 1}, SelectionMode mode = SelectionMode::New, bool scarce = false,
                     unsigned forged = 0)
    {
        Install(*session);
        memory.scarce = scarce;
        if (!probe.proceed || jobs.Initialize(2, {}, {Memory::Sample, &memory}) != S_OK ||
            !jobs.EnableDeferredRelease())
            Broken("scheduler");
        admitted = DescribeSelectionOutlineGestureMemory(*session, LassoKind::Polygonal, mode);
        if (!admitted || !admitted->drawing)
            Broken("complete-plan");
        arrival.document = session->CaptureDocumentReadAuthority().value();
        arrival.ordinal = 1;
        arrival.tool = HostSelectionPressInput::Tool::Lasso;
        arrival.kind = LassoKind::Polygonal;
        arrival.mode = mode;
        arrival.active = session->ActiveLayerID();
        arrival.crop = session->CropFrame();
        arrival.mask = session->IsMaskSelected();
        switch (forged)
        {
        case 1:
            ++arrival.document.generation;
            break;
        case 2:
            ++arrival.document.restoreSerial;
            break;
        case 3:
            arrival.active.reset();
            break;
        case 4:
            arrival.crop = CropRect{0, 0, 4, 4};
            break;
        case 5:
            arrival.mask = true;
            break;
        }
        if (!journal->Begin(81, arrival, Sample(down, 1)))
            Broken("down");
        state = std::make_unique<SelectionOutlineGestureState>(jobs, *session, *journal, 81, admitted->limits);
        state->admissionSource = admitted->source;
        state->drawingPlan = admitted->drawing;
        state->allowPreservingMarquee = mode != SelectionMode::New;
        state->requireCapturedTarget = true;
        state->observer = state->hostObserver = state->workObserver = &probe;
        state->notify = Probe::Owner;
        state->mayReplay = Probe::Replay;
        state->mayFinish = Probe::Finish;
        state->pressResolved = Probe::Press;
        state->workCheckpoint = Probe::Work;
        probe.state = state.get();
        probe.session = session.get();
    }
    ~Fixture()
    {
        (void)SetEvent(probe.proceed.get());
        if (state && state->controller)
            jobs.Close();
        Release();
        jobs.Close();
    }
    static HostPolygonalInputJournal::Sample Sample(Vec2 point, std::uint64_t ordinal)
    {
        HostPolygonalInputJournal::Sample sample;
        sample.point = point;
        sample.ordinal = ordinal;
        sample.viewScale = 32; // Large physical view: distinct small-document corners exceed 8 DIPs.
        sample.viewPoint = {point.x * 32, point.y * 32};
        sample.antialiased = false;
        return sample;
    }
    void Record(HostPolygonalInputJournal::Action action, Vec2 point, std::uint64_t ordinal)
    {
        if (journal->Record(81, action, Sample(point, ordinal)) != HostPolygonalInputJournal::Admission::Recorded)
            Broken("accepted-record");
    }
    void Down(Vec2 point, std::uint64_t ordinal, bool twice = false)
    {
        auto context = arrival;
        context.ordinal = ordinal;
        auto sample = Sample(point, ordinal);
        sample.doubleClick = twice;
        if (journal->Down(81, context, sample) != HostPolygonalInputJournal::Admission::Recorded)
            Broken("accepted-corner");
    }
    void Rectangle(bool terminal = true, bool disjoint = false)
    {
        const double left = arrival.mode == SelectionMode::Subtract && !disjoint ? 3 : 7;
        using A = HostPolygonalInputJournal::Action;
        Record(A::Up, {left, 1}, 2);
        Down({left + 4, 1}, 3);
        Record(A::Up, {left + 4, 1}, 4);
        Down({left + 4, 5}, 5);
        Record(A::Move, {10, 7}, 6);
        Record(A::Leave, {10, 7}, 7);
        Record(A::Up, {left + 4, 5}, 8);
        Down({left, 5}, 9);
        Record(A::Up, {left, 5}, 10);
        if (terminal)
            Record(A::Enter, {left, 5}, 11);
    }
    void NextParent(std::optional<SelectionOutlineGestureTransition> predecessor = std::nullopt,
                    HostPolygonalInputJournal* input = nullptr)
    {
        state.reset();
        admitted = DescribeSelectionOutlineGestureMemory(*session, LassoKind::Polygonal, SelectionMode::New);
        if (!admitted)
            Broken("successor-admission-description");
        auto& source = input ? *input : *journal;
        state =
            std::make_unique<SelectionOutlineGestureState>(jobs, *session, source, source.Stream(), admitted->limits);
        state->admissionSource = admitted->source;
        state->drawingPlan = admitted->drawing;
        state->requireCapturedTarget = true;
        state->observer = state->hostObserver = state->workObserver = &probe;
        state->notify = Probe::Owner;
        state->mayReplay = Probe::Replay;
        state->mayFinish = Probe::Finish;
        state->pressResolved = Probe::Press;
        state->workCheckpoint = Probe::Work;
        if (predecessor)
            state->predecessor.emplace(std::move(*predecessor));
        probe.state = state.get();
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
    void Wake()
    {
        if (!state->controller || !state->controller->NotifyInput())
            Broken("wake-original");
    }
    void Release()
    {
        if (!session)
            return;
        session->CancelLasso();
        state.reset();
        probe.state = nullptr;
        journal.reset();
        session.reset();
        Settle(jobs);
    }
};
#pragma warning(pop)

bool Pixels(const DocumentSession& session, SelectionMode mode, int movedX = 0, int movedY = 0)
{
    if (!session.Selection())
        return false;
    for (int y = 0; y < 10; ++y)
        for (int x = 0; x < 12; ++x)
        {
            const bool prior = x >= 1 + movedX && x < 5 + movedX && y >= 1 + movedY && y < 5 + movedY;
            const bool polygon =
                (mode == SelectionMode::Subtract ? x >= 3 && x < 7 : x >= 7 && x < 11) && y >= 1 && y < 5;
            const bool wanted = movedX || movedY             ? prior
                                : mode == SelectionMode::New ? polygon
                                : mode == SelectionMode::Add ? prior || polygon
                                                             : prior && !polygon;
            const auto actual =
                ContainsSelectionForWork(SelectionDisplayValue{session.Selection()}, {x + 0.5, y + 0.5}, {});
            if (!actual || actual.value() != wanted)
                return false;
        }
    return true;
}
void Numeric(SelectionMode mode, const char* name)
{
    scenario = name;
    auto f = std::make_unique<Fixture>(Vec2{mode == SelectionMode::Subtract ? 3.0 : 7.0, 1}, mode);
    const auto before = f->session->Selection();
    const auto generation = f->session->Generation();
    f->Rectangle();
    f->Submit();
    f->session->lassoKind = LassoKind::Freehand;
    f->session->selectionAntialiased = true;
    Settle(f->jobs);
    auto& s = *f->state;
    Expect(s.completed && s.drawing && s.opened && s.closed && s.outcome.status == JobStatus::Finished &&
               s.sameTicket && s.captures == 1 && s.hits == (mode == SelectionMode::New ? 1u : 0u),
           "original-admission-selects-exact-containment-or-preserving-branch");
    Expect(s.closurePrepares == 1 && f->memory.queries == 1 && s.maximumDrawingSamples <= 16 &&
               s.firstBudget == f->admitted->limits.totalBytes && s.partition,
           "one-reservation-and-bounded-replay-through-final-publication");
    Expect(Pixels(*f->session, mode) && !f->session->Selection()->antialiased,
           "all-120-independent-centers-and-terminal-aa");
    Expect(s.terminalOrdinal == 11 && s.inputAcknowledged && f->journal->Busy() && !f->journal->PendingCount() &&
               !s.controller && f->probe.acknowledgementVisible,
           "parent-terminal-acknowledges-borrow-without-retiring-family");
    Expect(f->probe.corners == 3 && f->probe.closes == 0 && f->probe.cornerOrdinal == 9 && f->probe.opened == 1 &&
               f->probe.terminal == 1,
           "up-hover-and-leave-never-become-corners-or-terminals");
    Expect(f->session->Generation() == generation + 1 && f->session->history.UndoCount() == 1 &&
               f->session->history.UndoName() == L"Polygonal Lasso" && f->session->history.CaptureState().Depth() == 0,
           "one-atomic-history-transition");
    f->session->Undo();
    Expect(f->session->Selection() == before && f->session->history.CanRedo(), "undo-restores-exact-prior-root");
    f->session->Redo();
    Expect(Pixels(*f->session, mode), "redo-restores-independent-pixels");
    Expect(f->journal->CloseInput(81) && f->journal->Finish(81), "host-retires-drained-unborrowed-family");
    f->Release();
    Expect(f->jobs.ReservedMemoryBytes() == 0 && f->jobs.ReleasingMemoryBytes() == 0,
           "joined-last-reader-refunds-original-charge");
}
void Inside(bool missingUp = false, bool overflow = false)
{
    scenario = overflow    ? "inside-up-before-family-overflow"
               : missingUp ? "inside-missing-up"
                           : "inside-up-younger-down";
    auto f = std::make_unique<Fixture>(Vec2{2, 2});
    const auto generation = f->session->Generation();
    f->Record(HostPolygonalInputJournal::Action::Move, {5, 4}, 2);
    if (!missingUp)
        f->Record(HostPolygonalInputJournal::Action::Up, {5, 4}, 3);
    f->Down({9, 8}, 4);
    if (overflow)
    {
        for (std::uint64_t ordinal = 5; ordinal <= 512; ++ordinal)
            f->Record(HostPolygonalInputJournal::Action::Move, {9, 8}, ordinal);
        Expect(f->journal->Record(81, HostPolygonalInputJournal::Action::Move, Fixture::Sample({10, 8}, 513)) ==
                   HostPolygonalInputJournal::Admission::Overflow,
               "overflow-preserves-all-512-accepted-prefix-records");
    }
    f->Submit();
    Settle(f->jobs);
    const auto next = f->journal->Peek(81);
    Expect(f->state->completed && !f->state->drawing && f->state->closed &&
               f->state->outcome.status == JobStatus::Finished && f->state->captures == 1 && f->state->hits == 1 &&
               f->state->sameTicket && f->state->terminalOrdinal == (missingUp ? 2u : 3u),
           "one-original-inside-parent-selects-own-physical-terminal");
    Expect(Pixels(*f->session, SelectionMode::New, 3, 2) && f->session->Generation() == generation + 2 &&
               f->session->history.UndoCount() == 1,
           "inside-drag-moves-only-existing-outline-and-publishes-once");
    Expect(next && next->action == HostPolygonalInputJournal::Action::Down && next->sample.ordinal == 4 &&
               next->sample.point == Vec2{9, 8} && next->down && next->down->ordinal == 4 &&
               f->journal->PendingCount() == (overflow ? 510u : 1u) && f->journal->Busy() &&
               f->state->inputAcknowledged && f->probe.acknowledgementVisible,
           "acknowledging-inside-parent-preserves-younger-down-and-family-tail");
    Expect(f->state->terminalLost == missingUp && f->state->overflowObserved == overflow && f->probe.corners == 0 &&
               f->probe.closes == 0,
           "later-family-overflow-never-retroactively-cancels-accepted-up");
    f->session->Undo();
    const auto bounds = f->session->Selection()->Bounds();
    Expect(bounds && bounds->minX == 1 && bounds->minY == 1 && bounds->maxX == 5 && bounds->maxY == 5,
           "one-undo-restores-original-outline");
    f->session->Redo();
    Expect(Pixels(*f->session, SelectionMode::New, 3, 2), "one-redo-restores-move");
    f->Release();
    Expect(f->jobs.ReservedMemoryBytes() == 0 && f->jobs.ReleasingMemoryBytes() == 0,
           "family-tail-destruction-after-join-refunds-original-charge");
}
void Readiness()
{
    scenario = "ordinal-and-terminal-readiness";
    auto f = std::make_unique<Fixture>();
    f->probe.replay = false;
    f->Rectangle();
    f->Submit();
    Until(f->jobs, [&] { return f->state->waiting; });
    const auto runs = f->state->runs;
    const auto next = f->journal->Peek(81);
    Expect(f->state->waitReason == SelectionOutlineGestureState::WaitReason::Ordinal && next &&
               next->sample.ordinal == 2 && f->session->PendingLasso()->PointCount() == 1 &&
               f->session->history.UndoCount() == 0,
           "older-host-ordinal-parks-before-consuming-next-physical-record");
    Expect(!f->state->controller->NotifyInput() && f->state->runs == runs, "unready-ordinal-does-not-self-rearm");
    f->probe.replay = true;
    f->probe.finish = false;
    f->Wake();
    Until(f->jobs, [&] { return f->state->waiting; });
    const auto terminal = f->journal->Peek(81);
    Expect(f->state->waitReason == SelectionOutlineGestureState::WaitReason::Terminal && terminal &&
               terminal->sample.ordinal == 11 && f->session->PendingLasso()->PointCount() == 4 &&
               f->state->closurePrepares == 0 && f->session->history.UndoCount() == 0,
           "terminal-gate-parks-before-taking-or-capturing-finish");
    const auto parkedRuns = f->state->runs;
    Expect(!f->state->controller->NotifyInput() && f->state->runs == parkedRuns &&
               f->jobs.ReservedMemoryBytes() >= f->admitted->limits.totalBytes,
           "unready-terminal-keeps-original-reservation-without-polling");
    f->probe.finish = true;
    f->Wake();
    Settle(f->jobs);
    Expect(f->state->closed && f->state->sameTicket && f->state->captures == 1 && f->state->terminalOrdinal == 11 &&
               Pixels(*f->session, SelectionMode::New),
           "single-readiness-event-resumes-same-parent-through-publication");
}
void Cancellation(unsigned kind, const char* name)
{
    scenario = name;
    auto f = std::make_unique<Fixture>(Vec2{7, 1}, SelectionMode::Add);
    const auto revision = f->session->history.CurrentRevision();
    const auto generation = f->session->Generation();
    f->probe.gate = kind == 3;
    f->Rectangle(kind == 3);
    if (kind == 0)
        f->Record(HostPolygonalInputJournal::Action::Escape, {7, 5}, 11);
    if (kind == 4)
    {
        for (std::uint64_t ordinal = 11; ordinal <= 14; ++ordinal)
            f->Record(HostPolygonalInputJournal::Action::Backspace, {7, 5}, ordinal);
    }
    f->Submit();
    if (kind == 3)
    {
        Until(f->jobs, [&] { return f->probe.entered.load(std::memory_order_acquire); });
        Expect(f->session->history.UndoCount() == 0 && !f->state->closed,
               "worker-prepared-result-has-no-early-owner-publication");
        if (!f->jobs.Cancel(f->state->original))
            Broken("cancel-running");
        (void)SetEvent(f->probe.proceed.get());
    }
    else if (kind == 1 || kind == 2)
    {
        Until(f->jobs, [&] { return f->state->waiting; });
        if (kind == 1)
        {
            if (!f->jobs.Cancel(f->state->original) || !f->jobs.Resume(f->state->original))
                Broken("cancel-parked");
        }
        else
            f->jobs.Close();
    }
    if (kind != 2)
        Settle(f->jobs);
    Expect(f->state->outcome.status == (kind == 4 ? JobStatus::Finished : JobStatus::Cancelled) && !f->state->closed &&
               f->state->inputAcknowledged && !f->state->controller && !f->session->PendingLasso(),
           "selected-cancel-or-backspace-zero-clears-only-owned-draft");
    Expect(f->session->Generation() == generation && f->session->history.CurrentRevision() == revision &&
               f->session->history.UndoCount() == 0 && f->session->history.CaptureState().Depth() == 0,
           "cancel-or-zero-does-not-publish-history-or-generation");
    Expect(f->journal->Busy() && f->probe.terminal == (kind == 2 ? 0u : 1u),
           "joined-close-suppresses-useful-callback-and-does-not-retire-family");
    f->Release();
    Expect(f->jobs.ReservedMemoryBytes() == 0 && f->jobs.ReleasingMemoryBytes() == 0,
           "cancelled-original-readers-refund-all-charges");
}
void Refusals(unsigned kind, const char* name)
{
    scenario = name;
    auto f = std::make_unique<Fixture>(Vec2{7, 1}, SelectionMode::New, kind == 0, kind > 1 ? kind - 1 : 0);
    const auto revision = f->session->history.CurrentRevision();
    if (kind == 1)
        f->session->history.Begin(L"Foreign Begin", f->session->Document(), f->session->ActiveLayerID());
    f->Rectangle();
    f->Submit();
    Settle(f->jobs);
    Expect(f->state->outcome.status != JobStatus::Finished && !f->state->opened && f->state->captures == 0 &&
               f->state->inputAcknowledged && f->session->history.CurrentRevision() == revision &&
               f->session->history.UndoCount() == 0,
           "refusal-precedes-source-capture-and-does-not-mutate-history");
    if (kind == 1)
        Expect(f->session->history.CaptureState().Depth() == 1,
               "original-refusal-preserves-real-foreign-pending-history");
    f->Release();
    Expect(f->jobs.ReservedMemoryBytes() == 0 && f->jobs.ReleasingMemoryBytes() == 0,
           "refused-original-retirement-refunds");
}
void ShortClose(SelectionMode mode, const char* name)
{
    scenario = name;
    auto f = std::make_unique<Fixture>(Vec2{7, 1}, mode);
    const auto revision = f->session->history.CurrentRevision();
    const auto generation = f->session->Generation();
    f->Record(HostPolygonalInputJournal::Action::Up, {7, 1}, 2);
    f->Down({11, 9}, 3, true);
    f->Submit();
    Settle(f->jobs);
    const bool clears = mode == SelectionMode::New;
    Expect(f->state->closed && f->state->outcome.status == JobStatus::Finished && f->state->sameTicket &&
               f->state->terminalOrdinal == 3 && f->probe.closes == 1 && !f->probe.corners &&
               f->state->inputAcknowledged,
           "double-click-selects-original-terminal-without-appending-closing-point");
    Expect(clears ? !f->session->Selection() && f->session->Generation() == generation + 1 &&
                        f->session->history.UndoCount() == 1
                  : f->session->Selection() && !f->session->Selection()->antialiased &&
                        f->session->Selection()->Bounds() == std::optional<SelectionBounds>{{1, 1, 5, 5}} &&
                        f->session->Generation() == generation && f->session->history.CurrentRevision() == revision &&
                        f->session->history.UndoCount() == 0,
           "short-new-clears-once-and-preserving-modes-keep-exact-no-op-history");
    f->Release();
    Expect(!f->jobs.ReservedMemoryBytes() && !f->jobs.ReleasingMemoryBytes(), "short-terminal-last-reader-refunds");
}
void ReplayQuantum()
{
    scenario = "bounded-hover-replay";
    auto f = std::make_unique<Fixture>();
    for (std::uint64_t ordinal = 2; ordinal <= 100; ++ordinal)
        f->Record(HostPolygonalInputJournal::Action::Move, {11, 9}, ordinal);
    f->Record(HostPolygonalInputJournal::Action::Enter, {11, 9}, 101);
    f->Submit();
    Settle(f->jobs);
    Expect(f->state->closed && f->state->sameTicket && f->state->captures == 1 &&
               f->state->maximumDrawingSamples == 16 && f->state->collectAcks >= 6 && f->memory.queries == 1,
           "known-prefix-yields-after-sixteen-records-without-new-admission");
    Expect(!f->session->Selection() && f->session->history.UndoCount() == 1 && f->probe.corners == 0 &&
               f->journal->PendingCount() == 0,
           "ninety-nine-hovers-never-add-corners-and-original-short-terminal-clears-once");
}
bool RectanglePixels(const DocumentSession& session, int left, int top, int right, int bottom)
{
    if (!session.Selection())
        return false;
    for (int y = 0; y < 10; ++y)
        for (int x = 0; x < 12; ++x)
        {
            const bool expected = x >= left && x < right && y >= top && y < bottom;
            const auto actual =
                ContainsSelectionForWork(SelectionDisplayValue{session.Selection()}, {x + 0.5, y + 0.5}, {});
            if (!actual || actual.value() != expected)
                return false;
        }
    return true;
}
void QueueInsideSuccessor(Fixture& f, bool third = false)
{
    using A = HostPolygonalInputJournal::Action;
    f.Record(A::Move, {5, 4}, 2);
    f.Record(A::Up, {5, 4}, 3);
    f.Down({5, 4}, 4);
    f.Record(A::Move, {7, 5}, 5);
    f.Record(A::Up, {7, 5}, 6);
    if (third)
    {
        f.Down({7, 5}, 7);
        f.Record(A::Move, {6, 4}, 8);
        f.Record(A::Up, {6, 4}, 9);
    }
}
void OwnedChain()
{
    scenario = "three-owned-original-parents";
    auto f = std::make_unique<Fixture>(Vec2{2, 2});
    const auto generation = f->session->Generation();
    const auto captured = f->arrival.document;
    Expect(!f->state->TakeSuccessor(), "uncompleted-parent-cannot-issue-transition");
    QueueInsideSuccessor(*f, true);
    f->Submit();
    Settle(f->jobs);
    auto first = f->state->TakeSuccessor();
    Expect(first && !f->state->TakeSuccessor() && f->state->closed &&
               f->journal->Peek(81)->down->document == captured && f->journal->Peek(81)->sample.ordinal == 4,
           "completed-original-issues-one-transition-with-immutable-younger-authority");
    f->NextParent(std::move(first));
    f->Submit();
    Settle(f->jobs);
    auto second = f->state->TakeSuccessor();
    Expect(second && f->state->closed && f->state->captures == 1 && f->state->sameTicket &&
               f->state->terminalOrdinal == 6 && !f->state->predecessor && RectanglePixels(*f->session, 6, 4, 10, 8) &&
               f->session->Generation() == generation + 4 && f->session->history.UndoCount() == 2,
           "second-original-consumes-transition-before-capture-and-publishes-exact-move");
    Expect(f->journal->Peek(81)->down->document == captured && f->journal->Peek(81)->sample.ordinal == 7,
           "second-parent-never-rewrites-third-physical-arrival");
    f->NextParent(std::move(second));
    f->Submit();
    Settle(f->jobs);
    auto third = f->state->TakeSuccessor();
    Expect(third && f->state->closed && f->state->sameTicket && f->state->captures == 1 &&
               RectanglePixels(*f->session, 5, 3, 9, 7) && f->session->Generation() == generation + 6 &&
               f->session->history.UndoCount() == 3 && f->memory.queries == 3,
           "third-original-retains-contiguous-owned-lineage-with-three-independent-admissions");
    f->session->Undo();
    Expect(RectanglePixels(*f->session, 6, 4, 10, 8), "third-undo-restores-second-literal-result");
    f->session->Undo();
    Expect(RectanglePixels(*f->session, 4, 3, 8, 7), "second-undo-restores-first-literal-result");
    f->session->Undo();
    Expect(RectanglePixels(*f->session, 1, 1, 5, 5), "first-undo-restores-original-literal-outline");
    f->session->Redo();
    f->session->Redo();
    f->session->Redo();
    Expect(RectanglePixels(*f->session, 5, 3, 9, 7), "three-redos-restore-complete-result");
    third.reset();
    f->Release();
    Expect(!f->jobs.ReservedMemoryBytes() && !f->jobs.ReleasingMemoryBytes(), "entire-chain-final-readers-refund");
}
void DrawingSuccessor()
{
    scenario = "owned-close-then-new-original";
    auto f = std::make_unique<Fixture>();
    const auto generation = f->session->Generation();
    f->Rectangle();
    f->Down({8, 2}, 12);
    f->Record(HostPolygonalInputJournal::Action::Move, {9, 3}, 13);
    f->Record(HostPolygonalInputJournal::Action::Up, {9, 3}, 14);
    f->Submit();
    Settle(f->jobs);
    auto token = f->state->TakeSuccessor();
    Expect(token && f->state->drawing && f->state->closed && f->journal->Peek(81)->sample.ordinal == 12 &&
               f->journal->Peek(81)->down->document == f->arrival.document,
           "chosen-close-issues-scalar-credit-and-preserves-accepted-next-down");
    f->NextParent(std::move(token));
    f->Submit();
    Settle(f->jobs);
    Expect(f->state->closed && !f->state->drawing && f->state->captures == 1 && f->state->sameTicket &&
               RectanglePixels(*f->session, 8, 2, 12, 6) && f->session->Generation() == generation + 3 &&
               f->session->history.UndoCount() == 2,
           "next-original-hit-uses-owned-committed-polygon-without-rebasing-down");
}
void PreviewArrival()
{
    scenario = "arrival-during-owned-preview";
    auto f = std::make_unique<Fixture>(Vec2{2, 2});
    const auto generation = f->session->Generation();
    f->Record(HostPolygonalInputJournal::Action::Move, {5, 4}, 2);
    f->Submit();
    Until(f->jobs, [&] { return f->state->waiting; });
    auto context = f->arrival;
    context.document = f->session->CaptureDocumentReadAuthority().value();
    context.ordinal = 4;
    f->Record(HostPolygonalInputJournal::Action::Up, {5, 4}, 3);
    if (f->journal->Down(81, context, Fixture::Sample({5, 4}, 4)) != HostPolygonalInputJournal::Admission::Recorded)
        Broken("preview-down");
    f->Record(HostPolygonalInputJournal::Action::Move, {7, 5}, 5);
    f->Record(HostPolygonalInputJournal::Action::Up, {7, 5}, 6);
    f->Wake();
    Settle(f->jobs);
    auto token = f->state->TakeSuccessor();
    Expect(token && context.document.generation == generation + 1 &&
               f->journal->Peek(81)->down->document == context.document && f->session->Generation() == generation + 2,
           "younger-down-captures-real-intermediate-owned-generation");
    f->NextParent(std::move(token));
    f->Submit();
    Settle(f->jobs);
    Expect(f->state->closed && f->state->captures == 1 && RectanglePixels(*f->session, 6, 4, 10, 8) &&
               f->session->history.UndoCount() == 2,
           "contiguous-owned-preview-authority-validates-without-rewriting-event");
}
void TransitionRefusal(unsigned kind, const char* name)
{
    scenario = name;
    auto f = std::make_unique<Fixture>(Vec2{2, 2});
    QueueInsideSuccessor(*f);
    f->Submit();
    Settle(f->jobs);
    const auto generation = f->session->Generation();
    const auto revision = f->session->history.CurrentRevision();
    auto token = f->state->TakeSuccessor();
    if (!token)
        Broken("issued-transition");
    std::unique_ptr<HostPolygonalInputJournal> other;
    if (kind == 1)
    {
        other = std::make_unique<HostPolygonalInputJournal>();
        auto context = f->arrival;
        context.ordinal = 4;
        if (!other->Begin(81, context, Fixture::Sample({5, 4}, 4)))
            Broken("foreign-family");
    }
    if (kind == 2)
    {
        while (f->journal->Take(81))
        {
        }
        if (!f->journal->CloseInput(81) || !f->journal->Finish(81))
            Broken("retired-family");
        auto context = f->arrival;
        context.ordinal = 4;
        if (!f->journal->Begin(82, context, Fixture::Sample({5, 4}, 4)))
            Broken("new-family");
    }
    if (kind == 3)
        f->session->history.Begin(L"Foreign after publication", f->session->Document(), f->session->ActiveLayerID());
    if (kind == 4)
        f->session->SetCropFrame(CropRect{0, 0, 4, 4});
    if (kind == 5)
        f->session->SelectLayer(std::nullopt);
    if (kind == 0)
        token.reset();
    f->NextParent(std::move(token), other.get());
    f->Submit();
    Settle(f->jobs);
    Expect(f->state->outcome.status == JobStatus::Failed && f->state->outcome.error == E_ABORT &&
               f->state->captures == 0 && !f->state->opened && !f->state->TakeSuccessor(),
           "missing-stale-or-foreign-transition-refuses-before-roots");
    Expect(RectanglePixels(*f->session, 4, 3, 8, 7) && f->session->Generation() == generation &&
               f->session->history.CurrentRevision() == revision && f->session->history.UndoCount() == 1 &&
               (kind != 3 || f->session->history.CaptureState().Depth() == 1),
           "refusal-preserves-exact-predecessor-result-and-foreign-history");
    f->Release();
    other.reset();
    Expect(!f->jobs.ReservedMemoryBytes() && !f->jobs.ReleasingMemoryBytes(), "refused-transition-final-refund");
}
void MoveOnlyTransition()
{
    scenario = "move-only-transition";
    auto f = std::make_unique<Fixture>(Vec2{2, 2});
    QueueInsideSuccessor(*f);
    f->Submit();
    Settle(f->jobs);
    auto issued = f->state->TakeSuccessor();
    if (!issued)
        Broken("move-issued");
    SelectionOutlineGestureTransition carried{std::move(*issued)};
    f->NextParent(std::move(issued));
    f->Submit();
    Settle(f->jobs);
    Expect(f->state->outcome.status == JobStatus::Failed && f->state->captures == 0 &&
               f->journal->Peek(81)->sample.ordinal == 4,
           "moved-from-receipt-cannot-authorize-or-consume-younger-down");
    f->NextParent(std::optional<SelectionOutlineGestureTransition>{std::move(carried)});
    f->Submit();
    Settle(f->jobs);
    Expect(f->state->closed && RectanglePixels(*f->session, 6, 4, 10, 8) && !f->state->predecessor &&
               f->session->history.UndoCount() == 2,
           "single-valid-move-authorizes-the-same-untouched-younger-down");
}
void PhysicalView()
{
    scenario = "event-time-view-close";
    auto f = std::make_unique<Fixture>();
    f->Rectangle(false);
    auto context = f->arrival;
    context.ordinal = 11;
    auto sample = Fixture::Sample({7, 1}, 11);
    sample.documentOrigin = {1125899906842624.25, 0};
    sample.viewScale = 0.3;
    sample.dipScale = 2;
    sample.viewPoint = {sample.documentOrigin.x + 7 * sample.viewScale + 16, 0.3};
    if (f->journal->Down(81, context, sample) != HostPolygonalInputJournal::Admission::Recorded)
        Broken("view-close");
    f->Submit();
    Settle(f->jobs);
    Expect(f->state->closed && f->state->terminalOrdinal == 11 && f->probe.closes == 1 && f->probe.corners == 3 &&
               Pixels(*f->session, SelectionMode::New),
           "actual-event-view-and-dpi-close-radius-survive-borrow-and-delayed-hit");
}
void TerminalAdmission(bool refuse)
{
    scenario = refuse ? "selected-terminal-admission-refusal" : "selected-terminal-readiness-then-admission";
    auto f = std::make_unique<Fixture>();
    const auto generation = f->session->Generation();
    const auto revision = f->session->history.CurrentRevision();
    f->Rectangle();
    f->state->admitTerminal = Probe::Admit;
    f->state->inputProgress = Probe::Progress;
    f->probe.finish = false;
    f->probe.terminalAdmission = !refuse;
    f->Submit();
    Until(f->jobs, [&]()
          { return f->state->waiting && f->state->waitReason == SelectionOutlineGestureState::WaitReason::Terminal; });
    Expect(!f->probe.terminalAdmissions && f->journal->Peek(81)->sample.ordinal == 11 && !f->state->terminalOrdinal &&
               f->state->lastConsumedOrdinal == 10 && f->session->Generation() == generation &&
               f->session->history.CurrentRevision() == revision,
           "pure-terminal-readiness-parks-without-admission-consumption-or-history");
    Expect(f->probe.progressNotifications && f->probe.progressOrdinal == 10 && f->probe.progressOnOwner &&
               f->probe.progressMonotonic,
           "park-signals-exact-consumed-prefix-on-owner");
    f->probe.finish = true;
    f->Wake();
    Settle(f->jobs);
    Expect(f->probe.terminalAdmissions == 1 && f->probe.admissionOrdinal == 11 && f->probe.admissionBeforeCapture &&
               f->probe.acknowledgementVisible,
           "selected-admission-runs-once-before-terminal-consumption-capture-and-ack");
    if (refuse)
    {
        Expect(f->state->outcome.status == JobStatus::Failed && f->state->outcome.error == E_ABORT &&
                   !f->state->closurePrepares && !f->state->terminalOrdinal && f->state->lastConsumedOrdinal == 10 &&
                   f->journal->Peek(81)->sample.ordinal == 11,
               "refused-admission-keeps-exact-terminal-unconsumed-and-captures-no-closure");
        Expect(f->session->Generation() == generation && f->session->history.CurrentRevision() == revision &&
                   !f->session->history.UndoCount() && !f->session->PendingLasso(),
               "refused-selected-terminal-preserves-prior-selection-and-history");
    }
    else
    {
        Expect(f->state->closed && f->state->terminalOrdinal == 11 && f->state->lastConsumedOrdinal == 11 &&
                   f->probe.progressOrdinal == 11 && Pixels(*f->session, SelectionMode::New),
               "admitted-terminal-publishes-exact-shape-and-progress-after-replay");
        Expect(f->session->Generation() == generation + 1 && f->session->history.UndoCount() == 1,
               "admitted-terminal-records-one-atomic-selection");
    }
    f->Release();
    Expect(!f->jobs.ReservedMemoryBytes() && !f->jobs.ReleasingMemoryBytes(), "selected-admission-final-refund");
}
void NoChangeProgress()
{
    scenario = "duplicate-corner-progress";
    auto f = std::make_unique<Fixture>();
    const auto generation = f->session->Generation();
    f->state->admitTerminal = Probe::Admit;
    f->state->inputProgress = Probe::Progress;
    f->Record(HostPolygonalInputJournal::Action::Up, {7, 1}, 2);
    f->Down({7, 1}, 3);
    f->Record(HostPolygonalInputJournal::Action::Up, {7, 1}, 4);
    f->Submit();
    Until(f->jobs, [&]() { return f->state->waiting && f->state->lastConsumedOrdinal == 4; });
    Expect(f->state->drawing && f->probe.corners == 1 && !f->probe.closes && !f->probe.terminalAdmissions &&
               f->probe.progressNotifications && f->probe.progressOrdinal == 4 && f->probe.progressOnOwner,
           "duplicate-corner-and-up-acknowledge-progress-without-closing-or-admitting-history");
    Expect(f->session->Generation() == generation && !f->session->history.UndoCount(),
           "no-change-prefix-does-not-publish-selection");
    f->Record(HostPolygonalInputJournal::Action::Escape, {7, 1}, 5);
    f->Wake();
    Settle(f->jobs);
    Expect(f->state->cancelled && f->state->terminalOrdinal == 5 && f->probe.terminalAdmissions == 1 &&
               f->probe.admissionBeforeCapture && f->probe.progressOrdinal == 5 && f->probe.progressMonotonic,
           "later-escape-admits-and-acknowledges-exact-original-parent");
    f->Release();
    Expect(!f->jobs.ReservedMemoryBytes() && !f->jobs.ReleasingMemoryBytes(), "duplicate-progress-final-refund");
}
void InsideKeyQuantum()
{
    scenario = "inside-ignored-key-quantum";
    auto f = std::make_unique<Fixture>(Vec2{2, 2});
    f->state->admitTerminal = Probe::Admit;
    f->state->inputProgress = Probe::Progress;
    for (std::uint64_t ordinal = 2; ordinal <= 34; ++ordinal)
        f->Record((ordinal % 2) ? HostPolygonalInputJournal::Action::Enter
                                : HostPolygonalInputJournal::Action::Backspace,
                  {2, 2}, ordinal);
    f->Record(HostPolygonalInputJournal::Action::Move, {5, 4}, 35);
    f->Record(HostPolygonalInputJournal::Action::Up, {5, 4}, 36);
    f->Submit();
    Settle(f->jobs);
    Expect(f->state->closed && !f->state->drawing && f->state->terminalOrdinal == 36 &&
               f->state->lastConsumedOrdinal == 36 && Pixels(*f->session, SelectionMode::New, 3, 2),
           "ignored-keys-preserve-atomic-inside-move-and-physical-up");
    Expect(f->probe.progressNotifications >= 3 && f->probe.progressNotifications <= 5 &&
               f->probe.progressOrdinal == 36 && f->probe.progressOnOwner && f->probe.progressMonotonic,
           "all-key-records-count-toward-sixteen-record-owner-quantum");
    Expect(f->probe.terminalAdmissions == 1 && f->probe.admissionOrdinal == 36 && f->probe.admissionBeforeCapture,
           "ignored-inside-keys-never-admit-a-terminal");
    f->Release();
    Expect(!f->jobs.ReservedMemoryBytes() && !f->jobs.ReleasingMemoryBytes(), "inside-key-quantum-final-refund");
}
} // namespace
int RunPolygonalDispatcherTests()
{
    checks = failures = 0;
    Numeric(SelectionMode::New, "outside-new-nonempty-prior");
    Numeric(SelectionMode::Add, "outside-add");
    Numeric(SelectionMode::Subtract, "outside-subtract");
    Inside();
    Inside(true);
    Inside(false, true);
    Readiness();
    Cancellation(0, "escape");
    Cancellation(1, "cancel-parked");
    Cancellation(2, "joined-close");
    Cancellation(3, "cancel-prepared-worker");
    Cancellation(4, "backspace-to-zero");
    Refusals(0, "memory-refusal");
    Refusals(1, "foreign-editor-refusal");
    Refusals(2, "forged-generation-refusal");
    Refusals(3, "forged-restore-refusal");
    Refusals(4, "forged-active-refusal");
    Refusals(5, "forged-crop-refusal");
    Refusals(6, "forged-mask-refusal");
    ShortClose(SelectionMode::New, "short-double-click-new");
    ShortClose(SelectionMode::Add, "short-double-click-add");
    ShortClose(SelectionMode::Subtract, "short-double-click-subtract");
    ReplayQuantum();
    OwnedChain();
    DrawingSuccessor();
    PreviewArrival();
    TransitionRefusal(0, "missing-transition");
    TransitionRefusal(1, "foreign-journal-transition");
    TransitionRefusal(2, "reused-family-transition");
    TransitionRefusal(3, "foreign-history-after-transition");
    TransitionRefusal(4, "foreign-crop-after-transition");
    TransitionRefusal(5, "foreign-active-after-transition");
    MoveOnlyTransition();
    PhysicalView();
    TerminalAdmission(false);
    TerminalAdmission(true);
    NoChangeProgress();
    InsideKeyQuantum();
    std::printf("%s polygonal-dispatcher (%u assertions)\n", failures ? "FAIL" : "PASS", checks);
    return static_cast<int>(failures);
}
