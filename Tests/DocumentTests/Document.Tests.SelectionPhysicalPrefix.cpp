#include "Document.Tests.SelectionPhysicalPrefix.h"
#include "../../RedPrism/HostSelectionPressInput.h"
#include "../Support/DocumentFixture.h"
#include "Document.h"
#include "Jobs.h"
#include "SelectionCommandWork.h"
#include "SelectionHitWork.h"
#include "SelectionMoveOpeningWork.h"
#include "SelectionMoveWork.h"
#include "SharedPublicationMemory.h"
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
class PrefixParent;
struct State final
{
    JobScheduler& scheduler;
    DocumentSession& session;
    HostSelectionPressInput& prefix;
    MemoryProbe& memory;
    PrefixParent* controller = nullptr;
    JobTicket original;
    unsigned runs = 0, captures = 0, hits = 0, collectAcks = 0, closurePrepares = 0, clickPrepares = 0, moves = 0;
    std::uint64_t firstBudget = 0, remainingBudget = 0, openingBytes = 0, queryBaseline = 0, queriesAtEnd = 0;
    bool opened = false, waiting = false, completed = false, closed = false, clicked = false, aborted = false;
    bool sameTicket = true, partition = false, inputAcknowledged = false, sourceCurrent = false, terminalLost = false,
         overflowObserved = false;
    std::uint64_t terminalOrdinal = 0;
    bool writerRan = false, writerSawDeselect = false;
    std::atomic<bool> parentAcknowledged{false}, writerEnteredBeforeParent{false};
    JobResult outcome{};
};
class PrefixParent final : public Job
{
    enum class Phase
    {
        Allocate,
        Open,
        Collect,
        Close,
        Click
    };

  public:
    explicit PrefixParent(State& state) : _state(state)
    {
        state.controller = this;
    }
    JobResult Run(JobContext& context) override
    {
        auto& s = _state;
        ++s.runs;
        if (_phase == Phase::Allocate)
        {
            s.firstBudget = context.reservedMemoryBytes;
            _output = SharedPublicationMemory::CreateForWork(kOpeningLimit);
            _final = SharedPublicationMemory::CreateForWork(kTotal);
            _temporary = SharedPublicationMemory::CreateForWork(kTotal);
            return {};
        }
        if (_phase == Phase::Open)
        {
            ++s.hits;
            const auto hit = ContainsSelectionForWork(_display, _point, context.cancellation);
            if (!hit || !hit.value() || !_input)
                return {JobStatus::Failed, E_INVALIDARG};
            auto prepared = DocumentSession::PrepareDeferredSelectionMoveOpening(
                std::move(_input.value()), context.cancellation, {}, _output, true);
            _input.reset();
            if (!prepared)
                return {JobStatus::Failed, E_FAIL};
            _opening.emplace(std::move(prepared.value()));
            return {};
        }
        s.remainingBudget = context.reservedMemoryBytes;
        if (_phase == Phase::Collect)
            return {}; // Event-triggered acknowledgement only; never read mutable input/session on workers.
        if (_phase == Phase::Close)
        {
            ++s.closurePrepares;
            if (!_closeInput)
                return {JobStatus::Failed, E_UNEXPECTED};
            auto memory = _click ? std::shared_ptr<const SharedPublicationMemory>{} : _final;
            auto prepared = DocumentSession::PrepareSelectionMoveClosurePublication(
                std::move(_closeInput.value()), context.cancellation, {}, nullptr, {}, memory);
            _closeInput.reset();
            if (!prepared)
                return {JobStatus::Failed, E_FAIL};
            _close.emplace(std::move(prepared.value()));
            return {};
        }
        ++s.clickPrepares;
        if (!_commandInput)
            return {JobStatus::Failed, E_UNEXPECTED};
        auto prepared = DocumentSession::PrepareSelectionCommandPublication(std::move(_commandInput.value()),
                                                                            context.cancellation, {}, _final);
        _commandInput.reset();
        if (!prepared)
            return {JobStatus::Failed, E_FAIL};
        _command.emplace(std::move(prepared.value()));
        return {};
    }
    bool DeferCompletion(JobResult result, JobTicket ticket) noexcept override
    {
        auto& s = _state;
        s.sameTicket = s.sameTicket && ticket == s.original;
        if (result.status != JobStatus::Finished)
            return false;
        if (_phase == Phase::Allocate)
        {
            const auto* arrival = s.prefix.CapturedContext(kOpening);
            const auto down = s.prefix.Peek(kOpening);
            if (!arrival || !down || down->action != HostSelectionPressInput::Action::Down ||
                arrival->document.documentSerial != s.session.DocumentSerial())
                return Refuse(E_ABORT);
            // Geometry/history and full source authority are captured only here, after this original admission.
            _source = s.session.CaptureDocumentReadAuthority();
            if (!_source || arrival->document.sessionIdentity != _source->sessionIdentity)
                return Refuse(E_ABORT);
            _display = s.session.DisplayedSelectionValue();
            _point = down->sample.point;
            auto input = s.session.PrepareDeferredSelectionMoveOpeningInput();
            if (!input || !_source)
                return Refuse(E_ABORT);
            _input.emplace(std::move(input.value()));
            const auto& loops = _display.selection.value().loops;
            _geometryEnvelope = loops.StorageBytes() + SelectionLoops::WorkOwnershipAllowance(loops.size());
            ++s.captures;
            _phase = Phase::Open;
            return Resume();
        }
        if (_phase == Phase::Open)
        {
            s.sourceCurrent = _source && s.session.IsDocumentReadCurrent(_source.value());
            if (!s.sourceCurrent || !_opening)
                return Refuse(E_ABORT);
            s.openingBytes = _output->LocalAllocationBytes() + SharedPublicationMemory::OwnershipAllowance();
            if (_output->BindContinuingOutputForPublication(s.scheduler, ticket, s.openingBytes) != S_OK)
                return Refuse(E_FAIL);
            s.partition = s.scheduler.ReservedMemoryBytes() >= kTotal && _output->BoundTo(s.scheduler, ticket);
            s.opened = s.session.ApplyPreparedDeferredSelectionMoveOpeningIfCurrent(_opening.value());
            if (!s.opened)
                return Refuse(E_ABORT);
            s.queryBaseline = s.memory.queries;
            _phase = Phase::Collect;
            return Consume();
        }
        if (_phase == Phase::Collect)
        {
            ++s.collectAcks;
            return Consume();
        }
        if (_phase == Phase::Close && _click)
        {
            s.closed = _close && s.session.ApplyPreparedSelectionMoveClosureIfCurrent(_close.value());
            if (!s.closed)
                return Refuse(E_ABORT);
            auto input = s.session.PrepareSelectionCommandInput(SelectionCommandKind::Deselect);
            if (!input)
                return Refuse(E_ABORT);
            _commandInput.emplace(std::move(input.value()));
            _phase = Phase::Click;
            return Resume();
        }
        return false;
    }
    bool NotifyInput() noexcept
    {
        auto& s = _state;
        if (!s.waiting || s.prefix.PendingCount() == 0)
            return false;
        s.waiting = false;
        if (s.scheduler.Resume(s.original))
            return true;
        s.waiting = true;
        return false;
    }
    void Completed(JobResult result) noexcept override
    {
        auto& s = _state;
        s.completed = true;
        s.waiting = false;
        s.outcome = result;
        if (_error && result.status == JobStatus::Finished)
            s.outcome = {JobStatus::Failed, _error};
        if (s.outcome.status == JobStatus::Finished)
        {
            const auto required = _command ? _command->OutputMemoryBytes()
                                           : _final->LocalAllocationBytes() +
                                                 SharedPublicationMemory::OwnershipAllowance() + _geometryEnvelope;
            if (!required || required > kFinal ||
                _final->BindPersistentOutputAfterCompletedWork(s.scheduler, s.original, kFinal, *_temporary) != S_OK)
                s.outcome = {JobStatus::Failed, E_FAIL};
            else if (_click)
                s.clicked = _command && s.session.ApplyPreparedSelectionCommandIfCurrent(_command.value());
            else
                s.closed = _close && s.session.ApplyPreparedSelectionMoveClosureIfCurrent(_close.value());
            if (!s.closed || (_click && !s.clicked))
                s.outcome = {JobStatus::Failed, E_ABORT};
        }
        if (s.outcome.status != JobStatus::Finished)
        {
            if (s.opened && !s.closed && _opening)
                s.aborted = s.session.AbortPreparedDeferredSelectionMoveIfCurrent(_opening.value());
            if (_temporary && !_temporary->IsBound() && _temporary->BindForRetirement(s.scheduler, s.original) != S_OK)
                std::terminate();
        }
        s.overflowObserved = s.prefix.Overflowed();
        s.inputAcknowledged = _terminal ? s.prefix.Acknowledge(kOpening) : s.prefix.Drop(kOpening);
        s.queriesAtEnd = s.memory.queries;
        s.controller = nullptr;
        s.parentAcknowledged.store(true, std::memory_order_release);
    }
    void RetainJoinedRetirement(JobScheduler&, JobTicket ticket) noexcept override
    {
        auto& s = _state;
        s.sameTicket = s.sameTicket && ticket == s.original;
        s.waiting = false;
        if (s.opened && !s.closed && _opening)
            s.aborted = s.session.AbortPreparedDeferredSelectionMoveIfCurrent(_opening.value());
        if (_temporary && !_temporary->IsBound() && _temporary->BindForRetirement(s.scheduler, ticket) != S_OK)
            std::terminate();
        s.inputAcknowledged = s.prefix.Drop(kOpening);
        s.controller = nullptr;
        s.outcome = {JobStatus::Cancelled};
    }

  private:
    bool Refuse(std::int32_t error) noexcept
    {
        _error = error;
        return false;
    }
    bool Resume() noexcept
    {
        if (!_state.scheduler.Resume(_state.original))
            return Refuse(E_UNEXPECTED);
        return true;
    }
    bool Consume() noexcept
    {
        auto& s = _state;
        if (!_opening || !s.session.IsPreparedDeferredSelectionMoveGestureCurrent(_opening.value()))
            return Refuse(E_ABORT);
        while (auto event = s.prefix.Take(kOpening))
        {
            if (event->action == HostSelectionPressInput::Action::Down)
                continue;
            if (event->action == HostSelectionPressInput::Action::Move)
            {
                double dx = event->sample.point.x - _point.x, dy = event->sample.point.y - _point.y;
                if (event->sample.modifiers.shift)
                {
                    if (std::fabs(dx) >= std::fabs(dy))
                        dy = 0;
                    else
                        dx = 0;
                }
                if (!s.session.MovePreparedDeferredSelectionMoveIfCurrent(_opening.value(), dx, dy))
                    return Refuse(E_ABORT);
                _offset = {std::round(dx), std::round(dy)};
                ++s.moves;
                continue;
            }
            _terminal = true;
            s.terminalLost = event->action == HostSelectionPressInput::Action::Lost;
            s.terminalOrdinal = event->sample.ordinal;
            _click = !s.terminalLost && _offset == Vec2{};
            auto input = s.session.PrepareSelectionMoveClosureInput(false, s.session.SelectionMoveOpening());
            if (!input)
                return Refuse(E_ABORT);
            _closeInput.emplace(std::move(input.value()));
            _phase = Phase::Close;
            return Resume();
        }
        s.waiting = true;
        return true; // No self-rearm or periodic work. The next accepted input supplies readiness.
    }
    State& _state;
    Phase _phase = Phase::Allocate;
    std::shared_ptr<SharedPublicationMemory> _output, _final, _temporary;
    std::optional<DocumentReadAuthority> _source;
    SelectionDisplayValue _display;
    Vec2 _point{}, _offset{};
    std::uint64_t _geometryEnvelope = 0;
    std::int32_t _error = 0;
    bool _click = false, _terminal = false;
    std::optional<SelectionMoveOpeningInput> _input;
    std::optional<PreparedSelectionMoveOpening> _opening;
    std::optional<SelectionMoveClosureInput> _closeInput;
    std::optional<PreparedSelectionMoveClosure> _close;
    std::optional<SelectionCommandInput> _commandInput;
    std::optional<PreparedSelectionCommand> _command;
};
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
enum class Mode
{
    EarlyClick,
    AsyncShift,
    LostMoved,
    Overflow,
    OrdinalExhaust,
    QueuedWriter,
    CancelCollect,
    ForeignSession
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
    else if (mode == Mode::LostMoved || mode == Mode::CancelCollect)
    {
        if (prefix->Move(kOpening, {0, 6}, {}, true, 2) != HostSelectionPressInput::Admission::Recorded)
            SetupFailure("moved-prefix");
        if (mode == Mode::LostMoved && prefix->SealLost(kOpening) != HostSelectionPressInput::Admission::Recorded)
            SetupFailure("lost-seal");
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
        const std::uint64_t mutations = cancelled                    ? 3
                                        : mode == Mode::EarlyClick   ? 2
                                        : mode == Mode::QueuedWriter ? 3
                                        : mode == Mode::AsyncShift   ? 3
                                        : mode == Mode::Overflow     ? 512
                                                                     : 2;
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
    const unsigned expectedMoves = mode == Mode::Overflow                                   ? 511
                                   : mode == Mode::AsyncShift                               ? 2
                                   : mode == Mode::EarlyClick || mode == Mode::QueuedWriter ? 0
                                                                                            : 1;
    const bool lost = mode == Mode::LostMoved || mode == Mode::Overflow || mode == Mode::OrdinalExhaust;
    const std::uint64_t expectedTerminal = cancelled                      ? 0
                                           : mode == Mode::Overflow       ? 512
                                           : mode == Mode::OrdinalExhaust ? (std::numeric_limits<std::uint64_t>::max)()
                                           : mode == Mode::AsyncShift     ? 4
                                                                          : 2;
    Expect(state.moves == expectedMoves && state.terminalLost == lost && state.terminalOrdinal == expectedTerminal &&
               state.overflowObserved == (mode == Mode::Overflow) && state.inputAcknowledged && !prefix->Busy(),
           "bounded-prefix-retains-accepted-points-modifiers-and-terminal");
    const unsigned expectedRuns = mode == Mode::AsyncShift || mode == Mode::QueuedWriter ? 5
                                  : mode == Mode::EarlyClick || mode == Mode::LostMoved  ? 4
                                                                                         : 3;
    const unsigned expectedCollects = mode == Mode::AsyncShift                                             ? 2
                                      : mode == Mode::LostMoved || mode == Mode::QueuedWriter || cancelled ? 1
                                                                                                           : 0;
    Expect(state.runs == expectedRuns && state.collectAcks == expectedCollects &&
               state.closurePrepares == (cancelled ? 0u : 1u) &&
               state.clickPrepares == ((mode == Mode::EarlyClick || mode == Mode::QueuedWriter) ? 1u : 0u),
           "event-driven-body-and-capture-counts-match-acknowledged-stages");
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
    std::printf("SUMMARY selection-physical-prefix checks=%u failures=%u\n", checks, failures);
    return failures ? 1 : 0;
}
