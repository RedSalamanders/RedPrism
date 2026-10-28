#include "Document.Tests.SelectionPhysicalPrefix.h"
#include "Document.h"
#include "Jobs.h"
#include "RedPrism/HostSelectionPressInput.h"
#include "RedPrism/SelectionOutlineGestureJob.h"
#include "SelectionCommandWork.h"
#include "SelectionHitWork.h"
#include "SelectionMoveOpeningWork.h"
#include "SelectionMoveWork.h"
#include "SharedPublicationMemory.h"
#include "Tests/Support/Support.Tests.DocumentFixture.h"
#include "TransformEdit.h"
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <memory>
#include <optional>
#include <windows.h>
namespace
{
constexpr std::uint64_t kTotal = 8 * 1024 * 1024, kOpeningLimit = 64 * 1024, kFinal = 64 * 1024, kOpening = 17;
constexpr std::array<Vec2, 4> kOrigin{{{1, 2}, {3, 2}, {3, 4}, {1, 4}}};
constexpr std::array<Vec2, 4> kMoved{{{-1, 5}, {1, 5}, {1, 7}, {-1, 7}}};
constexpr std::array<Vec2, 4> kShifted{{{5, 2}, {7, 2}, {7, 4}, {5, 4}}};
constexpr std::array<Vec2, 4> kOverflow{{{512, 2}, {514, 2}, {514, 4}, {512, 4}}};
constexpr std::array<Vec2, 4> kFull{{{0, 0}, {8, 0}, {8, 8}, {0, 8}}};
unsigned checks = 0, failures = 0;
const char* currentCase = "setup";
void Expect(bool value, const char* label)
{
    ++checks;
    failures += value ? 0 : 1;
    std::printf("%s selection-physical-prefix.%s.%s\n", value ? "PASS" : "FAIL", currentCase, label);
}
[[noreturn]] void SetupFailure(const char* label)
{
    std::printf("FAIL selection-physical-prefix.setup.%s\n", label);
    std::_Exit(1);
}
bool Literal(const std::optional<DocumentSelection>& value, const std::array<Vec2, 4>& points, bool aa = false)
{
    if (!value || value->antialiased != aa || value->loops.size() != 1 || value->loops.Get()[0].size() != 4)
        return false;
    for (std::size_t i = 0; i < 4; ++i)
        if (value->loops.Get()[0][i] != points[i])
            return false;
    return true;
}
// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027)
struct MemoryProbe final
{
    std::atomic<std::uint64_t> queries{0};
    std::atomic<bool> scarce{false};
    static bool Sample(void* opaque, MemorySnapshot& out) noexcept
    {
        auto& self = *static_cast<MemoryProbe*>(opaque);
        ++self.queries;
        const std::uint64_t available = self.scarce ? 0 : kMemoryAdmissionHeadroomBytes + 512 * 1024 * 1024;
        out = {available, available};
        return true;
    }
};
#pragma warning(pop)

void Settle(JobScheduler& scheduler)
{
    if (SettleJobs(scheduler, 10000) != SettleResult::Settled)
        SetupFailure("settle");
}
template <class Predicate> void Until(JobScheduler& scheduler, Predicate done)
{
    const auto deadline = GetTickCount64() + 10000;
    while (!done())
    {
        if (PumpMessages().quit)
            SetupFailure("unexpected-quit");
        (void)DrainCompletions(scheduler);
        if (done())
            break;
        const auto now = GetTickCount64();
        if (now >= deadline)
            SetupFailure("event-deadline");
        const HANDLE wake = scheduler.WakeHandle();
        if (MsgWaitForMultipleObjectsEx(1, &wake, static_cast<DWORD>(deadline - now), QS_ALLINPUT,
                                        MWMO_INPUTAVAILABLE) == WAIT_FAILED)
            SetupFailure("event-wait");
    }
}
using PrefixParent = SelectionOutlineGestureJob;
// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027)
struct State final : SelectionOutlineGestureState
{
    MemoryProbe& memory;
    std::uint64_t queryBaseline = 0, queriesAtEnd = 0;
    bool writerRan = false, writerSawDeselect = false;
    unsigned movedCallbacks = 0;
    bool openTransform = false, foreignOpened = false;
    const TransformEdit* foreignTransformAddress = nullptr;
    std::optional<LayerTransform> foreignDraft;

    std::atomic<bool> writerEnteredBeforeParent{false};
    State(JobScheduler& scheduler, DocumentSession& session, HostSelectionPressInput& prefix, MemoryProbe& probe)
        : SelectionOutlineGestureState{scheduler, session, prefix, kOpening, {kTotal, kOpeningLimit, kFinal}},
          memory(probe)
    {
        observer = this;
        notify = [](void* raw, Event event) noexcept
        {
            auto& state = *static_cast<State*>(raw);
            if (event == Event::Opened)
                state.queryBaseline = state.memory.queries;
            if (event == Event::Moved)
            {
                ++state.movedCallbacks;
                if (state.openTransform)
                {
                    state.openTransform = false;
                    const auto generation = state.session.Generation();
                    const auto revision = state.session.history.CurrentRevision();
                    const auto depth = state.session.history.CaptureState().Depth();
                    state.session.BeginTransform(true);
                    const auto& pending = state.session.PendingTransformEdit();
                    state.foreignOpened = pending && state.session.Generation() == generation &&
                                          state.session.history.CurrentRevision() == revision &&
                                          state.session.history.CaptureState().Depth() == depth;
                    if (pending)
                    {
                        state.foreignTransformAddress = &pending.value();
                        state.foreignDraft = pending->draft;
                    }
                }
            }
            if (event == Event::Terminal)
                state.queriesAtEnd = state.memory.queries;
        };
    }
};
#pragma warning(pop)

// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4626 5027)
class YoungerWriter final : public Job
{
  public:
    explicit YoungerWriter(State& state) : _state(state) {}
    JobResult Run(JobContext&) override
    {
        if (!_state.parentAcknowledged.load(std::memory_order_acquire))
            _state.writerEnteredBeforeParent.store(true);
        return {};
    }
    void Completed(JobResult result) noexcept override
    {
        _state.writerRan = result.status == JobStatus::Finished;
        _state.writerSawDeselect = !_state.session.Selection() && _state.session.history.UndoName() == L"Deselect";
        if (_state.writerRan)
            _state.session.SelectAll(); // Tiny numeric reference: independent younger-writer oracle.
    }

  private:
    State& _state;
};
#pragma warning(pop)

enum class Mode
{
    EarlyClick,
    AsyncShift,
    LostMoved,
    Overflow,
    OrdinalExhaust,
    QueuedWriter,
    CancelCollect,
    ForeignSession,
    ForeignNotification
};
void RunCase(Mode mode, const char* name)
{
    currentCase = name;
    auto session = std::make_unique<DocumentSession>();
    DocumentFixture document;
    document.id = Guid::Generate();
    document.width = 8;
    document.height = 8;
    document.selection =
        DocumentSelection{false, SelectionLoops{SelectionLoop{kOrigin[0], kOrigin[1], kOrigin[2], kOrigin[3]}}};
    auto layer = ImageLayer::Blank(L"Physical prefix", Vec2{8, 8});
    if (mode == Mode::ForeignNotification)
        layer.asset =
            RasterAsset{Raster::MakeSparse(8, 8, RasterFormat::Bgra8888, Raster{}, 0, 0, {}, 0), {}, layer.name};

    const Guid active = layer.id;
    document.layers.push_back(std::move(layer));
    session->ReplaceDocument(std::move(document).Seal(), active);
    session->SetSelection(DocumentSelection{false, SelectionLoops{SelectionLoop{{0, 0}, {1, 0}, {1, 1}, {0, 1}}}},
                          L"Seed redo");
    session->Undo();
    const auto before = session->history.CaptureState();
    const auto generation = session->Generation();
    // Fixed owner-side application input storage exists before descriptor admission, without reading geometry.
    auto prefix = std::make_unique<HostSelectionPressInput>();
    MemoryProbe memory;
    JobScheduler scheduler;
    if (scheduler.Initialize(2, {}, {MemoryProbe::Sample, &memory}) != S_OK || !scheduler.EnableDeferredRelease())
        SetupFailure("scheduler");
    HostSelectionPressInput::Context arrival;
    arrival.document = session->CaptureDocumentReadAuthority().value();
    arrival.active = active;
    if (mode == Mode::ForeignSession)
        arrival.document.sessionIdentity = Guid::Generate();
    arrival.ordinal = mode == Mode::OrdinalExhaust ? (std::numeric_limits<std::uint64_t>::max)() - 1 : 1;
    arrival.tool = HostSelectionPressInput::Tool::Marquee;
    arrival.kind = LassoKind::Rectangle;
    arrival.mode = SelectionMode::New;
    if (!prefix->Begin(kOpening, arrival, {2, 3}, {}, true))
        SetupFailure("record-down");
    if (mode == Mode::EarlyClick)
    {
        if (prefix->Finish(kOpening, false, 2) != HostSelectionPressInput::Admission::Recorded)
            SetupFailure("early-up");
    }
    if (mode == Mode::Overflow)
    {
        static_assert(HostSelectionPressInput::kCapacity == 512);
        for (std::uint64_t i = 1; i <= 511; ++i)
            if (prefix->Move(kOpening, {2.0 + static_cast<double>(i), 3}, {}, true, i + 1) !=
                HostSelectionPressInput::Admission::Recorded)
                SetupFailure("accepted-capacity-prefix");
        if (prefix->Move(kOpening, {9999, 3}, {}, true, 513) != HostSelectionPressInput::Admission::Overflow)
            SetupFailure("capacity-seals-lost");
    }
    if (mode == Mode::OrdinalExhaust)
    {
        if (prefix->Move(kOpening, {0, 6}, {}, true, (std::numeric_limits<std::uint64_t>::max)()) !=
                HostSelectionPressInput::Admission::Recorded ||
            prefix->Finish(kOpening, true, 0) != HostSelectionPressInput::Admission::Invalid ||
            prefix->SealLost(kOpening) != HostSelectionPressInput::Admission::Recorded)
            SetupFailure("ordinal-exhaustion-keeps-last-prefix");
    }
    State state{scheduler, *session, *prefix, memory};
    state.openTransform = mode == Mode::ForeignNotification;
    JobOptions options;
    options.kind = JobClass::Selection;
    options.serialKey = 1;
    options.memoryBytes = kTotal;
    options.deferredRetirement = true;
    options.reserveRetainedMemory = true;
    options.allowMemoryWaiting = false;
    options.continuingOutputLimit = kOpeningLimit;
    std::unique_ptr<Job> parent = std::make_unique<PrefixParent>(state);
    if (scheduler.Submit(parent, options, state.original) != S_OK)
        SetupFailure("original-admission");
    if (mode == Mode::ForeignSession)
    {
        Settle(scheduler);
        Expect(state.completed && !state.opened && state.runs == 1 && state.captures == 0 && state.hits == 0,
               "foreign-session-refused-before-source-capture-or-hit");
        Expect(state.sameTicket && state.outcome.status == JobStatus::Failed && state.outcome.error == E_ABORT,
               "foreign-session-fails-exact-original-ticket");
        Expect(Literal(session->Selection(), kOrigin) && session->Generation() == generation &&
                   session->history.CaptureState().Revision() == before.Revision() && session->history.CanRedo(),
               "foreign-session-preserves-selection-history-generation-and-redo");
        Expect(state.inputAcknowledged && !prefix->Busy() && !state.controller,
               "foreign-session-drops-only-its-bounded-prefix");
        Expect(state.movedCallbacks == 0, "one-display-notification-per-consumed-move-batch");
        session.reset();
        Settle(scheduler);
        Expect(scheduler.ReservedMemoryBytes() == 0 && scheduler.ReleasingMemoryBytes() == 0 && scheduler.IsIdle(),
               "foreign-session-refunds-complete-original-admission");
        scheduler.Close();
        return;
    }
    Until(scheduler, [&] { return state.opened; });
    if (mode == Mode::AsyncShift)
    {
        if (prefix->Move(kOpening, {0, 6}, {true, false, false}, false, 2) !=
                HostSelectionPressInput::Admission::Recorded ||
            !state.controller || !state.controller->NotifyInput())
            SetupFailure("first-shift-input");
        Until(scheduler, [&] { return state.moves == 1 && state.waiting; });
        if (session->DisplayedSelectionValue().translation != Vec2{0, 3})
            SetupFailure("first-literal-shift-preview");
        // Later live header/keyboard settings cannot rewrite either accepted sample.
        session->selectionAntialiased = false;
        if (prefix->Move(kOpening, {6, 4}, {true, false, true}, true, 3) !=
                HostSelectionPressInput::Admission::Recorded ||
            prefix->Finish(kOpening, false, 4) != HostSelectionPressInput::Admission::Recorded ||
            !state.controller->NotifyInput())
            SetupFailure("second-shift-and-up");
    }
    else if (mode == Mode::LostMoved || mode == Mode::CancelCollect || mode == Mode::ForeignNotification)
    {
        if (prefix->Move(kOpening, {0, 6}, {}, true, 2) != HostSelectionPressInput::Admission::Recorded)
            SetupFailure("moved-prefix");
        if (mode == Mode::LostMoved && prefix->SealLost(kOpening) != HostSelectionPressInput::Admission::Recorded)
            SetupFailure("lost-seal");
        if (mode == Mode::ForeignNotification &&
            prefix->Finish(kOpening, false, 3) != HostSelectionPressInput::Admission::Recorded)
            SetupFailure("foreign-callback-up");
        if (!state.controller || !state.controller->NotifyInput())
            SetupFailure("moved-ready");
        if (mode == Mode::CancelCollect)
        {
            Until(scheduler, [&] { return state.moves == 1 && state.waiting; });
            if (!scheduler.Cancel(state.original) || !scheduler.Resume(state.original))
                SetupFailure("cancel-original-suspended-parent");
        }
    }
    else if (mode == Mode::QueuedWriter)
    {
        std::unique_ptr<Job> writer = std::make_unique<YoungerWriter>(state);
        auto writerOptions = options;
        writerOptions.continuingOutputLimit = 0;
        writerOptions.memoryBytes = 1024 * 1024;
        writerOptions.deferredRetirement = false;
        writerOptions.reserveRetainedMemory = false;
        JobTicket writerTicket;
        if (scheduler.Submit(writer, writerOptions, writerTicket) != S_OK)
            SetupFailure("younger-writer-admission");
        state.queryBaseline = memory.queries;
        if (prefix->Finish(kOpening, true, 2) != HostSelectionPressInput::Admission::Recorded || !state.controller ||
            !state.controller->NotifyInput())
            SetupFailure("writer-ordered-up");
    }
    if (mode == Mode::OrdinalExhaust)
        memory.scarce = true; // No closing admission/query may depend on newly available memory.
    Settle(scheduler);
    if (mode == Mode::ForeignNotification)
    {
        Expect(state.opened && state.captures == 1 && state.hits == 1 && state.moves == 1 &&
                   state.movedCallbacks == 1 && state.foreignOpened,
               "external-notification-opens-foreign-transform-without-generation-or-history-change");
        Expect(state.completed && state.sameTicket && state.outcome.status == JobStatus::Failed &&
                   state.outcome.error == E_ABORT && !state.closed && !state.aborted && state.closurePrepares == 0 &&
                   state.clickPrepares == 0,
               "post-callback-guard-refuses-before-capturing-or-publishing-closure");
        const auto& pending = session->PendingTransformEdit();
        Expect(pending && &pending.value() == state.foreignTransformAddress && state.foreignDraft &&
                   pending->draft == *state.foreignDraft && pending->persistent && !pending->deferred &&
                   Literal(session->Selection(), kOrigin) && session->Generation() == generation + 1 &&
                   session->history.CurrentRevision() == before.Revision() &&
                   session->history.CaptureState().Depth() == before.Depth() + 1 && !session->history.CanRedo() &&
                   before.FutureCount() == 1 && session->history.CaptureState().FutureCount() == before.FutureCount() &&
                   session->history.CaptureState().RedoName() == before.RedoName(),
               "refusal-preserves-foreign-transform-original-selection-open-history-and-redo");
        Expect(state.inputAcknowledged && !prefix->Busy() && !state.controller &&
                   state.queriesAtEnd == state.queryBaseline,
               "original-prefix-acknowledges-failure-without-new-admission-or-query");
        session.reset();
        Settle(scheduler);
        Expect(scheduler.ReservedMemoryBytes() == 0 && scheduler.ReleasingMemoryBytes() == 0 && scheduler.IsIdle(),
               "final-foreign-state-and-original-owners-refund-after-last-reader");
        scheduler.Close();
        return;
    }
    Expect(state.opened && state.partition && state.sourceCurrent && state.captures == 1 && state.hits == 1,
           "original-admission-captures-exact-hit-and-opening-once");
    const bool cancelled = mode == Mode::CancelCollect;
    Expect(state.completed && state.sameTicket &&
               (cancelled ? state.outcome.status == JobStatus::Cancelled && state.aborted
                          : state.outcome.status == JobStatus::Finished && state.closed &&
                                ((mode != Mode::EarlyClick && mode != Mode::QueuedWriter) || state.clicked)),
           "same-parent-acknowledges-captured-terminal-or-cancel");
    {
        const auto now = session->history.CaptureState();
        const bool geometry = mode == Mode::EarlyClick ? !session->Selection()
                              : mode == Mode::QueuedWriter
                                  ? Literal(session->Selection(), kFull, true)
                                  : Literal(session->Selection(), cancelled                  ? kOrigin
                                                                  : mode == Mode::AsyncShift ? kShifted
                                                                  : mode == Mode::Overflow   ? kOverflow
                                                                                             : kMoved);
        const std::uint64_t mutations = static_cast<std::uint64_t>(cancelled                    ? 3
                                                                   : mode == Mode::EarlyClick   ? 2
                                                                   : mode == Mode::QueuedWriter ? 3
                                                                   : mode == Mode::AsyncShift   ? 3
                                                                   : mode == Mode::Overflow     ? 512
                                                                                                : 2);
        Expect(geometry && !session->IsMovingSelection() && session->Generation() == generation + mutations &&
                   now.Depth() == 0 &&
                   (cancelled ? now.Revision() == before.Revision() && session->history.CanRedo()
                              : now.PastCount() == before.PastCount() + (mode == Mode::QueuedWriter ? 2 : 1) &&
                                    !session->history.CanRedo()),
               "literal-final-geometry-aa-history-generation-and-redo");
    } // Release the result-history reader before checking last-owner refund.
    Expect(state.firstBudget == kTotal && state.remainingBudget == kTotal - state.openingBytes &&
               state.queriesAtEnd == state.queryBaseline,
           "prepaid-closing-and-retirement-never-query-or-readmit");
    const unsigned expectedMoves = static_cast<unsigned>(mode == Mode::Overflow                                   ? 511
                                                         : mode == Mode::AsyncShift                               ? 2
                                                         : mode == Mode::EarlyClick || mode == Mode::QueuedWriter ? 0
                                                                                                                  : 1);
    const bool lost = mode == Mode::LostMoved || mode == Mode::Overflow || mode == Mode::OrdinalExhaust;
    const std::uint64_t expectedTerminal = cancelled                      ? 0
                                           : mode == Mode::Overflow       ? 512
                                           : mode == Mode::OrdinalExhaust ? (std::numeric_limits<std::uint64_t>::max)()
                                           : mode == Mode::AsyncShift     ? 4
                                                                          : 2;
    Expect(state.moves == expectedMoves && state.terminalLost == lost && state.terminalOrdinal == expectedTerminal &&
               state.overflowObserved == (mode == Mode::Overflow) && state.inputAcknowledged && !prefix->Busy(),
           "bounded-prefix-retains-accepted-points-modifiers-and-terminal");
    const unsigned expectedRuns = static_cast<unsigned>(mode == Mode::AsyncShift || mode == Mode::QueuedWriter ? 5
                                                        : mode == Mode::EarlyClick || mode == Mode::LostMoved  ? 4
                                                                                                               : 3);
    const unsigned expectedCollects =
        static_cast<unsigned>(mode == Mode::AsyncShift                                             ? 2
                              : mode == Mode::LostMoved || mode == Mode::QueuedWriter || cancelled ? 1
                                                                                                   : 0);
    Expect(state.runs == expectedRuns && state.collectAcks == expectedCollects &&
               state.closurePrepares == (cancelled ? 0u : 1u) &&
               state.clickPrepares == ((mode == Mode::EarlyClick || mode == Mode::QueuedWriter) ? 1u : 0u),
           "event-driven-body-and-capture-counts-match-acknowledged-stages");
    const unsigned notifications =
        mode == Mode::AsyncShift ? 2u : (mode == Mode::EarlyClick || mode == Mode::QueuedWriter ? 0u : 1u);
    Expect(state.movedCallbacks == notifications, "one-display-notification-per-consumed-move-batch");
    if (mode == Mode::QueuedWriter)
        Expect(state.writerRan && state.writerSawDeselect && !state.writerEnteredBeforeParent.load(),
               "younger-writer-observes-only-completed-parent");
    session.reset();
    Settle(scheduler);
    Expect(scheduler.ReservedMemoryBytes() == 0 && scheduler.ReleasingMemoryBytes() == 0 && scheduler.IsIdle(),
           "final-readers-refund-all-original-parent-and-writer-ownership");
    scheduler.Close();
}
} // namespace
int RunDocumentSelectionPhysicalPrefixTests()
{
    RunCase(Mode::EarlyClick, "early-up");
    RunCase(Mode::AsyncShift, "async-shift");
    RunCase(Mode::LostMoved, "lost-moved");
    RunCase(Mode::Overflow, "overflow");
    RunCase(Mode::OrdinalExhaust, "ordinal-exhaustion");
    RunCase(Mode::QueuedWriter, "queued-writer");
    RunCase(Mode::CancelCollect, "cancel-collect");
    RunCase(Mode::ForeignSession, "foreign-session");
    RunCase(Mode::ForeignNotification, "foreign-notification");
    std::printf("SUMMARY selection-physical-prefix checks=%u failures=%u\n", checks, failures);
    return failures ? 1 : 0;
}
