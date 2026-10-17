#include "Document.Tests.SelectionGestureParent.h"
#include "../Support/DocumentFixture.h"
#include "Document.h"
#include "Jobs.h"
#include "SelectionCommandWork.h"
#include "SelectionMoveOpeningWork.h"
#include "SelectionMoveWork.h"
#include "SharedPublicationMemory.h"
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <optional>
#include <windows.h>
namespace
{
constexpr std::uint64_t kTotal = 8 * 1024 * 1024, kOpeningLimit = 64 * 1024, kFinal = 64 * 1024;
constexpr std::array<Vec2, 4> kOrigin{{{1, 2}, {3, 2}, {3, 4}, {1, 4}}}, kMoved{{{-1, 5}, {1, 5}, {1, 7}, {-1, 7}}};
unsigned checks = 0, failures = 0;
const char* caseName = "setup";
void Expect(bool value, const char* name)
{
    ++checks;
    failures += value ? 0 : 1;
    std::printf("%s selection-gesture-parent.%s.%s\n", value ? "PASS" : "FAIL", caseName, name);
}
[[noreturn]] void BadSetup(const char* name)
{
    std::printf("FAIL selection-gesture-parent.setup.%s\n", name);
    std::_Exit(1);
}
bool Literal(const std::optional<DocumentSelection>& s, const std::array<Vec2, 4>& expected)
{
    if (!s || s->antialiased || s->loops.size() != 1 || s->loops.Get()[0].size() != 4)
        return false;
    for (std::size_t i = 0; i < 4; ++i)
        if (s->loops.Get()[0][i] != expected[i])
            return false;
    return true;
}
bool Sample(void*, MemorySnapshot& out) noexcept
{
    out = {kMemoryAdmissionHeadroomBytes + 512 * 1024 * 1024, kMemoryAdmissionHeadroomBytes + 512 * 1024 * 1024};
    return true;
}
void Settle(JobScheduler& scheduler)
{
    if (SettleJobs(scheduler, 10000) != SettleResult::Settled)
        BadSetup("settle-original-parent");
}
enum class Mode
{
    Moved,
    ClickDeselect,
    Lost,
    LostMoved,
    Failed,
    Cancelled,
    JoinedClose
};
struct State final
{
    JobScheduler& scheduler;
    DocumentSession& session;
    Mode mode;
    JobTicket original;
    unsigned runs = 0;
    bool captured = false, opened = false, resumed = false, sameTicket = true, completed = false;
    bool closed = false, clicked = false, aborted = false, joined = false, partition = false;
    std::uint64_t openingBytes = 0, secondBudget = 0, terminalBytes = 0, observerBytes = 0;
    JobResult outcome{};
    std::weak_ptr<const SharedPublicationMemory> openingOwner, terminalOwner;
    std::shared_ptr<const SharedPublicationMemory> joinedAccountingWitness;
};
class ParentJob final : public Job
{
  public:
    explicit ParentJob(State& state) : _state(state) {}
    JobResult Run(JobContext& context) override
    {
        auto& s = _state;
        ++s.runs;
        if (s.runs == 1)
        {
            _openingMemory = SharedPublicationMemory::CreateForWork(kOpeningLimit);
            _finalMemory = SharedPublicationMemory::CreateForWork(kTotal);
            _temporary = SharedPublicationMemory::CreateForWork(kTotal);
            s.openingOwner = _openingMemory;
            s.terminalOwner = _temporary;
            return {};
        }
        if (s.runs == 2)
        {
            if (!_input)
                return {JobStatus::Failed, E_UNEXPECTED};
            auto value = DocumentSession::PrepareDeferredSelectionMoveOpening(
                std::move(_input.value()), context.cancellation, {}, _openingMemory, true);
            _input.reset();
            if (!value)
                return {JobStatus::Failed, E_FAIL};
            _opening.emplace(std::move(value.value()));
            return {};
        }
        s.secondBudget = context.reservedMemoryBytes;
        if (s.mode == Mode::Failed)
            return {JobStatus::Failed, E_ACCESSDENIED};
        if (s.runs == 3)
        {
            if (!_closeInput)
                return {JobStatus::Failed, E_UNEXPECTED};
            // Click's zero-offset End publishes no new graph and retains the original admission for useful follow-up.
            auto memory =
                s.mode == Mode::ClickDeselect ? std::shared_ptr<const SharedPublicationMemory>{} : _finalMemory;
            auto value = DocumentSession::PrepareSelectionMoveClosurePublication(
                std::move(_closeInput.value()), context.cancellation, {}, nullptr, {}, memory);
            _closeInput.reset();
            if (!value)
                return {JobStatus::Failed, E_FAIL};
            _close.emplace(std::move(value.value()));
            return {};
        }
        if (!_commandInput)
            return {JobStatus::Failed, E_UNEXPECTED};
        auto command = DocumentSession::PrepareSelectionCommandPublication(std::move(_commandInput.value()),
                                                                           context.cancellation, {}, _finalMemory);
        _commandInput.reset();
        if (!command)
            return {JobStatus::Failed, E_FAIL};
        _command.emplace(std::move(command.value()));
        return {};
    }
    bool DeferCompletion(JobResult result, JobTicket ticket) noexcept override
    {
        auto& s = _state;
        s.sameTicket = s.sameTicket && ticket == s.original;
        if (result.status != JobStatus::Finished)
            return false;
        if (s.runs == 1)
        {
            auto input = s.session.PrepareDeferredSelectionMoveOpeningInput();
            if (!input)
                return false;
            _input.emplace(std::move(input.value()));
            const auto& source = s.session.Selection().value().loops;
            _geometryEnvelope = source.StorageBytes() + SelectionLoops::WorkOwnershipAllowance(source.size());
            s.captured = true;
            return s.scheduler.Resume(ticket);
        }
        if (s.runs == 2)
        {
            if (!_opening)
                return false;
            s.openingBytes = _openingMemory->LocalAllocationBytes() + SharedPublicationMemory::OwnershipAllowance();
            if (_openingMemory->BindContinuingOutputForPublication(s.scheduler, ticket, s.openingBytes) != S_OK)
                return false;
            s.partition = s.scheduler.ReservedMemoryBytes() == kTotal + s.observerBytes &&
                          _openingMemory->BoundTo(s.scheduler, ticket);
            s.opened = s.session.ApplyPreparedDeferredSelectionMoveOpeningIfCurrent(_opening.value());
            if (!s.opened)
                return false;
            if (s.mode == Mode::Moved || s.mode == Mode::LostMoved || s.mode == Mode::Failed ||
                s.mode == Mode::Cancelled || s.mode == Mode::JoinedClose)
                if (!s.session.MovePreparedDeferredSelectionMoveIfCurrent(_opening.value(), -2.0, 3.0))
                    return false;
            if (s.mode == Mode::JoinedClose)
                return true;
            if (s.mode == Mode::Cancelled)
            {
                s.resumed = s.scheduler.Cancel(ticket) && s.scheduler.Resume(ticket);
                return s.resumed;
            }
            if (s.mode != Mode::Failed)
            {
                auto input = s.session.PrepareSelectionMoveClosureInput(false, s.session.SelectionMoveOpening());
                if (!input)
                    return false;
                _closeInput.emplace(std::move(input.value()));
            }
            s.resumed = s.scheduler.Resume(ticket);
            return s.resumed;
        }
        if (s.runs == 3 && s.mode == Mode::ClickDeselect)
        {
            s.closed = _close && s.session.ApplyPreparedSelectionMoveClosureIfCurrent(_close.value());
            if (!s.closed)
                return false;
            // Capture only after real End changed generation, while the same original admission is still held.
            auto input = s.session.PrepareSelectionCommandInput(SelectionCommandKind::Deselect);
            if (!input)
                return false;
            _commandInput.emplace(std::move(input.value()));
            return s.scheduler.Resume(ticket);
        }
        return false;
    }
    void Completed(JobResult result) noexcept override
    {
        auto& s = _state;
        s.completed = true;
        s.outcome = result;
        if (result.status == JobStatus::Finished && s.opened &&
            (s.mode == Mode::Moved || s.mode == Mode::Lost || s.mode == Mode::LostMoved ||
             s.mode == Mode::ClickDeselect))
        {
            const auto required = _command ? _command->OutputMemoryBytes()
                                           : _finalMemory->LocalAllocationBytes() +
                                                 SharedPublicationMemory::OwnershipAllowance() + _geometryEnvelope;
            if (!required || required > kFinal ||
                _finalMemory->BindPersistentOutputAfterCompletedWork(s.scheduler, s.original, kFinal, *_temporary) !=
                    S_OK)
                return;
            if (s.mode == Mode::ClickDeselect)
                s.clicked = _command && s.session.ApplyPreparedSelectionCommandIfCurrent(_command.value());
            else
                s.closed = _close && s.session.ApplyPreparedSelectionMoveClosureIfCurrent(_close.value());
        }
        else
        {
            if (s.opened && _opening)
                s.aborted = s.session.AbortPreparedDeferredSelectionMoveIfCurrent(_opening.value());
            if (_temporary->BindForRetirement(s.scheduler, s.original) != S_OK)
                return;
        }
        s.terminalBytes = _temporary->Bytes();
    }
    void RetainJoinedRetirement(JobScheduler&, JobTicket ticket) noexcept override
    {
        auto& s = _state;
        s.sameTicket = s.sameTicket && ticket == s.original;
        s.joined = true;
        if (s.opened && _opening)
            s.aborted = s.session.AbortPreparedDeferredSelectionMoveIfCurrent(_opening.value());
        if (_temporary->BindForRetirement(s.scheduler, ticket) == S_OK)
            s.terminalBytes = _temporary->Bytes();
        s.joinedAccountingWitness = _openingMemory;
    }

  private:
    State& _state;
    std::uint64_t _geometryEnvelope = 0;
    std::shared_ptr<SharedPublicationMemory> _openingMemory, _finalMemory, _temporary;
    std::optional<SelectionMoveOpeningInput> _input;
    std::optional<PreparedSelectionMoveOpening> _opening;
    std::optional<SelectionMoveClosureInput> _closeInput;
    std::optional<PreparedSelectionMoveClosure> _close;
    std::optional<SelectionCommandInput> _commandInput;
    std::optional<PreparedSelectionCommand> _command;
};
constexpr std::uint64_t kObserverBytes = 8192;
// A settled independent read-only accounting owner observes the shared endpoint after Close. Its own
// explicit charge stays outside the gesture's admission, so final parent refund has an observable oracle.
class EndpointObserverJob final : public Job
{
  public:
    EndpointObserverJob(JobScheduler& scheduler, std::shared_ptr<SharedPublicationMemory>& output)
        : _scheduler(scheduler), _output(output)
    {
    }
    JobResult Run(JobContext&) override
    {
        _memory = SharedPublicationMemory::CreateForWork(kObserverBytes);
        return {};
    }
    bool DeferRetirement(JobResult, JobTicket ticket) noexcept override
    {
        _ticket = ticket;
        return false;
    }
    void Completed(JobResult result) noexcept override
    {
        if (result.status == JobStatus::Finished && _memory &&
            _memory->BindAfterCompletedWork(_scheduler, _ticket) == S_OK)
            _output = std::move(_memory);
    }

  private:
    JobScheduler& _scheduler;
    std::shared_ptr<SharedPublicationMemory>& _output;
    std::shared_ptr<SharedPublicationMemory> _memory;
    JobTicket _ticket;
};
void WaitOpening(State& s)
{
    const auto deadline = GetTickCount64() + 10000;
    while (!s.opened)
    {
        if (PumpMessages().quit)
            BadSetup("opening-quit");
        (void)DrainCompletions(s.scheduler);
        if (s.opened)
            break;
        auto now = GetTickCount64();
        if (now >= deadline)
            BadSetup("opening-deadline");
        HANDLE wake = s.scheduler.WakeHandle();
        if (MsgWaitForMultipleObjectsEx(1, &wake, static_cast<DWORD>(deadline - now), QS_ALLINPUT,
                                        MWMO_INPUTAVAILABLE) == WAIT_FAILED)
            BadSetup("opening-event");
    }
}
void RunCase(Mode mode, const char* name)
{
    caseName = name;
    auto session = std::make_unique<DocumentSession>();
    DocumentFixture document;
    document.id = Guid::Generate();
    document.width = 8;
    document.height = 8;
    document.selection =
        DocumentSelection{false, SelectionLoops{SelectionLoop{kOrigin[0], kOrigin[1], kOrigin[2], kOrigin[3]}}};
    auto layer = ImageLayer::Blank(L"Gesture parent", Vec2{8, 8});
    const Guid active = layer.id;
    document.layers.push_back(std::move(layer));
    session->ReplaceDocument(std::move(document).Seal(), active);
    session->SetSelection(DocumentSelection{false, SelectionLoops{SelectionLoop{{0, 0}, {1, 0}, {1, 1}, {0, 1}}}},
                          L"Seed redo");
    session->Undo();
    const auto before = session->history.CaptureState();
    const auto generation = session->Generation();
    JobScheduler scheduler;
    if (scheduler.Initialize(2, {}, {Sample, nullptr}) != S_OK || !scheduler.EnableDeferredRelease())
        BadSetup("scheduler");
    std::shared_ptr<SharedPublicationMemory> closedEndpointObserver;
    if (mode == Mode::JoinedClose)
    {
        JobOptions observerOptions;
        observerOptions.memoryBytes = kObserverBytes;
        observerOptions.deferredRetirement = true;
        observerOptions.reserveRetainedMemory = true;
        std::unique_ptr<Job> observer = std::make_unique<EndpointObserverJob>(scheduler, closedEndpointObserver);
        JobTicket observerTicket;
        if (scheduler.Submit(observer, observerOptions, observerTicket) != S_OK)
            BadSetup("closed-endpoint-observer-admission");
        Settle(scheduler);
        if (!closedEndpointObserver || closedEndpointObserver->Bytes() != kObserverBytes)
            BadSetup("closed-endpoint-observer-publication");
    }
    State state{scheduler, *session, mode};
    state.observerBytes = closedEndpointObserver ? kObserverBytes : 0;
    JobOptions options;
    options.kind = JobClass::Selection;
    options.serialKey = 1;
    options.memoryBytes = kTotal;
    options.deferredRetirement = true;
    options.reserveRetainedMemory = true;
    options.allowMemoryWaiting = false;
    options.continuingOutputLimit = kOpeningLimit;
    std::unique_ptr<Job> parent = std::make_unique<ParentJob>(state);
    if (scheduler.Submit(parent, options, state.original) != S_OK)
        BadSetup("original-admission");
    if (mode == Mode::JoinedClose)
    {
        WaitOpening(state);
        scheduler.Close();
    }
    else
        Settle(scheduler);
    Expect(state.captured && state.opened && state.partition && state.sameTicket,
           "opening-and-terminal-keep-original-ticket-and-complete-admission");
    const bool abort = mode == Mode::Failed || mode == Mode::Cancelled || mode == Mode::JoinedClose;
    if (abort)
    {
        const bool expected =
            mode == Mode::Failed
                ? state.completed && state.outcome.status == JobStatus::Failed && state.outcome.error == E_ACCESSDENIED
            : mode == Mode::Cancelled ? state.completed && state.outcome.status == JobStatus::Cancelled
                                      : state.joined && !state.completed;
        Expect(expected && state.aborted && !session->IsMovingSelection() && Literal(session->Selection(), kOrigin),
               "failed-cancelled-or-joined-close-cleans-exact-published-gesture");
        Expect(session->history.CaptureState().Depth() == before.Depth() &&
                   session->history.CurrentRevision() == before.Revision() && session->history.CanRedo() &&
                   session->Generation() == generation + 3,
               "rollback-preserves-redo-and-reference-move-plus-two-cancel-invalidations");
    }
    else
    {
        Expect(state.completed && state.outcome.status == JobStatus::Finished && state.closed &&
                   (mode != Mode::ClickDeselect || state.clicked) && !session->IsMovingSelection(),
               "successful-terminal-acknowledges-end-and-optional-click");
        const bool literal =
            mode == Mode::ClickDeselect
                ? !session->Selection()
                : Literal(session->Selection(), (mode == Mode::Moved || mode == Mode::LostMoved) ? kMoved : kOrigin);
        Expect(literal && session->history.CaptureState().Depth() == before.Depth() &&
                   (mode == Mode::Lost
                        ? session->history.CanRedo() && session->history.CurrentRevision() == before.Revision()
                        : !session->history.CanRedo() &&
                              session->history.CaptureState().PastCount() == before.PastCount() + 1),
               "moved-click-and-lost-use-literal-reference-history-semantics");
        Expect(state.secondBudget == kTotal - state.openingBytes && state.terminalBytes > 0,
               "useful-terminal-work-receives-only-original-prepaid-remainder");
    }
    session.reset();
    if (mode != Mode::JoinedClose)
        Settle(scheduler);
    if (mode == Mode::JoinedClose)
    {
        // Close intentionally zeroes the scheduler's former admission counter. Foreign live controls retain
        // a separate endpoint, which is observable through ResidentBytes on this explicit outside witness.
        Expect(state.joinedAccountingWitness &&
                   state.joinedAccountingWitness->ResidentBytes() == kTotal + kObserverBytes &&
                   state.terminalOwner.expired() && !state.openingOwner.expired(),
               "joined-close-retains-complete-original-endpoint-charge-after-owner-teardown");
        state.joinedAccountingWitness.reset();
        Expect(state.openingOwner.expired() && state.terminalOwner.expired() && closedEndpointObserver &&
                   closedEndpointObserver->ResidentBytes() == kTotal + kObserverBytes,
               "joined-close-expired-parent-controls-keep-charge-after-last-strong-witness");
    }
    else
        Expect(state.openingOwner.expired() && state.terminalOwner.expired() && scheduler.ReservedMemoryBytes() > 0,
               "expired-owner-weak-controls-keep-original-memory-charged");
    state.openingOwner.reset();
    state.terminalOwner.reset();
    if (mode == Mode::JoinedClose)
    {
        Expect(closedEndpointObserver && closedEndpointObserver->Bytes() == kObserverBytes &&
                   closedEndpointObserver->ResidentBytes() == kObserverBytes,
               "joined-close-last-original-weak-controls-refund-exact-parent-bytes");
        closedEndpointObserver.reset();
    }
    if (mode != Mode::JoinedClose)
        Settle(scheduler);
    Expect(scheduler.ReservedMemoryBytes() == 0 && scheduler.ReleasingMemoryBytes() == 0,
           "all-original-owners-retire-and-refund-after-final-reader");
    scheduler.Close();
}
} // namespace
int RunDocumentSelectionGestureParentTests()
{
    RunCase(Mode::Moved, "moved");
    RunCase(Mode::ClickDeselect, "click-deselect");
    RunCase(Mode::Lost, "lost");
    RunCase(Mode::LostMoved, "lost-moved");
    RunCase(Mode::Failed, "failed");
    RunCase(Mode::Cancelled, "cancelled");
    RunCase(Mode::JoinedClose, "joined-close");
    std::printf("SUMMARY selection-gesture-parent checks=%u failures=%u\n", checks, failures);
    return failures ? 1 : 0;
}
