#include "Document.Tests.SelectionHistoryBoundLifetime.h"

#include "../Support/Support.Tests.DocumentFixture.h"
#include "Document.h"
#include "Jobs.h"
#include "SelectionMoveOpeningWork.h"
#include "SelectionMoveWork.h"
#include "SharedPublicationMemory.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <memory>
#include <new>
#include <optional>
#include <string>
#include <utility>
#include <windows.h>

namespace
{
constexpr std::uint64_t kSlotBytes = 8ull * 1024ull * 1024ull;
constexpr std::uint64_t kHeadroomBytes = 16ull * 1024ull * 1024ull * 1024ull;
constexpr std::array<Vec2, 4> kOrigin{{{1, 2}, {3, 2}, {3, 4}, {1, 4}}};
constexpr std::array<Vec2, 4> kMoved{{{-1, 5}, {1, 5}, {1, 7}, {-1, 7}}};

unsigned int gAssertions = 0;
unsigned int gFailures = 0;

void Expect(bool condition, const char* label) noexcept
{
    ++gAssertions;
    std::printf("%s selection-history-bound-lifetime.%s\n", condition ? "PASS" : "FAIL", label);
    gFailures += condition ? 0u : 1u;
}

DocumentSelection SquareSelection()
{
    return DocumentSelection{false, SelectionLoops{SelectionLoop{kOrigin[0], kOrigin[1], kOrigin[2], kOrigin[3]}}};
}

bool HasLiteral(const std::optional<DocumentSelection>& selection, const std::array<Vec2, 4>& expected)
{
    if (!selection || selection->antialiased || selection->loops.size() != 1)
        return false;
    const auto& points = selection->loops.Get()[0];
    if (points.size() != expected.size())
        return false;
    for (std::size_t index = 0; index < expected.size(); ++index)
    {
        if (points[index] != expected[index])
            return false;
    }
    return true;
}

struct Fixture final
{
    std::unique_ptr<DocumentSession> session = std::make_unique<DocumentSession>();
    bool ready = false;

    Fixture()
    {
        DocumentFixture source;
        source.id = Guid::Generate();
        source.width = 8;
        source.height = 8;
        source.selection = SquareSelection();
        auto layer = ImageLayer::Blank(L"Bound selection history", Vec2{8, 8});
        const std::array<std::uint8_t, 4> pixel{0, 0, 0, 255};
        layer.asset =
            RasterAsset{Raster::Make(1, 1, RasterFormat::Bgra8888, pixel.data(), pixel.size()), {}, layer.name};
        const Guid active = layer.id;
        source.layers.push_back(std::move(layer));
        session->ReplaceDocument(std::move(source).Seal(), active);
        session->SetSelection(DocumentSelection{false, SelectionLoops{SelectionLoop{{0, 0}, {1, 0}, {1, 1}, {0, 1}}}},
                              L"Seed redo");
        session->Undo();
        ready = session->Document().has_value() && HasLiteral(session->Selection(), kOrigin) &&
                session->history.CanRedo() && session->history.RedoName() == L"Seed redo";
    }
};

struct MemoryProbe final
{
    MemorySnapshot snapshot{kMemoryAdmissionHeadroomBytes + kHeadroomBytes,
                            kMemoryAdmissionHeadroomBytes + kHeadroomBytes};
};

bool SampleMemory(void* context, MemorySnapshot& snapshot) noexcept
{
    snapshot = static_cast<MemoryProbe*>(context)->snapshot;
    return true;
}

bool StartScheduler(JobScheduler& scheduler, MemoryProbe& probe) noexcept
{
    return scheduler.Initialize(1, {}, {SampleMemory, &probe}) == S_OK && scheduler.EnableDeferredRelease();
}

bool Settle(JobScheduler& scheduler) noexcept
{
    return SettleJobs(scheduler, 10000) == SettleResult::Settled;
}

enum class Operation : std::uint8_t
{
    Opening,
    Closure
};

struct PublicationState final
{
    JobTicket ticket{};
    JobResult result{};
    std::weak_ptr<const SharedPublicationMemory> output;
    std::weak_ptr<const SharedPublicationMemory> temporary;
    std::uint64_t outputBytes = 0;
    std::uint64_t temporaryBytes = 0;
    std::uint32_t workerStages = 0;
    bool captured = false;
    bool resumedOriginal = false;
    bool outputBoundToOriginal = false;
    bool temporaryBoundToOriginal = false;
    bool completionSeen = false;
    bool applied = false;
};

class SelectedHistoryJob final : public Job
{
  public:
    SelectedHistoryJob(DocumentSession& session, JobScheduler& scheduler, PublicationState& state, Operation operation)
        : _session(session), _scheduler(scheduler), _state(state), _operation(operation)
    {
    }

    JobResult Run(JobContext& context) override
    {
        if (context.cancellation.IsCancelled())
            return {JobStatus::Cancelled};
        ++_state.workerStages;
        if (!_started)
        {
            _output = SharedPublicationMemory::CreateForWork();
            _temporary = SharedPublicationMemory::CreateForWork();
            _state.output = _output;
            _state.temporary = _temporary;
            _started = true;
            return {};
        }
        if (_operation == Operation::Opening)
        {
            if (!_openingInput)
                return {JobStatus::Failed, E_UNEXPECTED};
            auto prepared = DocumentSession::PrepareDeferredSelectionMoveOpening(std::move(_openingInput.value()),
                                                                                 context.cancellation, {}, _output);
            _openingInput.reset();
            if (!prepared)
                return {context.cancellation.IsCancelled() ? JobStatus::Cancelled : JobStatus::Failed,
                        context.cancellation.IsCancelled() ? 0 : E_FAIL};
            _opening.emplace(std::move(prepared.value()));
        }
        else
        {
            if (!_closureInput)
                return {JobStatus::Failed, E_UNEXPECTED};
            auto prepared = DocumentSession::PrepareSelectionMoveClosurePublication(
                std::move(_closureInput.value()), context.cancellation, {}, nullptr, {}, _output);
            _closureInput.reset();
            if (!prepared)
                return {context.cancellation.IsCancelled() ? JobStatus::Cancelled : JobStatus::Failed,
                        context.cancellation.IsCancelled() ? 0 : E_FAIL};
            _closure.emplace(std::move(prepared.value()));
        }
        return {};
    }

    bool DeferCompletion(JobResult result, JobTicket ticket) noexcept override
    {
        if (!(ticket == _state.ticket) || result.status != JobStatus::Finished)
            return false;
        if (!_state.captured)
        {
            try
            {
                if (_operation == Operation::Opening)
                {
                    auto input = _session.PrepareDeferredSelectionMoveOpeningInput();
                    if (!input)
                        return false;
                    _openingInput.emplace(std::move(input.value()));
                }
                else
                {
                    auto input = _session.PrepareSelectionMoveClosureInput(false);
                    if (!input)
                        return false;
                    _closureInput.emplace(std::move(input.value()));
                }
            }
            catch (const std::bad_alloc&)
            {
                return false;
            }
            _state.captured = true;
            _state.resumedOriginal = _scheduler.Resume(ticket) && ticket == _state.ticket;
            return _state.resumedOriginal;
        }

        if (_state.outputBoundToOriginal || !_output || !_temporary)
            return false;
        const std::uint64_t localBytes = _output->LocalAllocationBytes();
        const std::uint64_t allowance = SharedPublicationMemory::OwnershipAllowance();
        if (localBytes > (std::numeric_limits<std::uint64_t>::max)() - allowance)
            return false;
        _state.outputBytes = localBytes + allowance;
        if (_state.outputBytes == 0 || _state.outputBytes >= kSlotBytes)
            return false;
        if (_output->BindPersistentOutputForPublication(_scheduler, ticket, _state.outputBytes, *_temporary) != S_OK)
            return false;
        _state.temporaryBytes = kSlotBytes - _state.outputBytes;
        _state.outputBoundToOriginal = _output->BoundTo(_scheduler, ticket) && _output->Bytes() == _state.outputBytes &&
                                       _output->LocalAllocationBytes() + allowance == _state.outputBytes;
        _state.temporaryBoundToOriginal =
            _temporary->BoundTo(_scheduler, ticket) && _temporary->Bytes() == _state.temporaryBytes;
        return false;
    }

    void Completed(JobResult result) noexcept override
    {
        _state.result = result;
        _state.completionSeen = true;
        if (result.status != JobStatus::Finished || !_state.outputBoundToOriginal || !_state.temporaryBoundToOriginal)
            return;
        if (_operation == Operation::Opening && _opening)
            _state.applied = _session.ApplyPreparedDeferredSelectionMoveOpeningIfCurrent(_opening.value());
        else if (_operation == Operation::Closure && _closure)
            _state.applied = _session.ApplyPreparedSelectionMoveClosureIfCurrent(_closure.value());
    }

  private:
    DocumentSession& _session;
    JobScheduler& _scheduler;
    PublicationState& _state;
    const Operation _operation;
    std::shared_ptr<SharedPublicationMemory> _output;
    std::shared_ptr<SharedPublicationMemory> _temporary;
    std::optional<SelectionMoveOpeningInput> _openingInput;
    std::optional<PreparedSelectionMoveOpening> _opening;
    std::optional<SelectionMoveClosureInput> _closureInput;
    std::optional<PreparedSelectionMoveClosure> _closure;
    bool _started = false;
};

std::uint64_t SubmitSelectedHistory(JobScheduler& scheduler, DocumentSession& session, PublicationState& state,
                                    Operation operation)
{
    std::unique_ptr<Job> job = std::make_unique<SelectedHistoryJob>(session, scheduler, state, operation);
    JobOptions options;
    options.kind = JobClass::Selection;
    options.serialKey = 1;
    options.memoryBytes = kSlotBytes;
    options.deferredRetirement = true;
    options.reserveRetainedMemory = true;
    options.allowMemoryWaiting = false;
    const std::int32_t submitted = scheduler.Submit(job, options, state.ticket);
    return submitted == S_OK && !job ? kSlotBytes : 0;
}

bool CheckBoundOwner(const PublicationState& state, const JobScheduler& scheduler) noexcept
{
    const auto memory = state.output.lock();
    return memory && memory->BoundTo(scheduler, state.ticket) && memory->IsBound() &&
           memory->Bytes() == state.outputBytes &&
           memory->LocalAllocationBytes() + SharedPublicationMemory::OwnershipAllowance() == state.outputBytes;
}

void TestBoundSelectedBeginEndUndoRedoAndWeakControl()
{
    MemoryProbe probe;
    JobScheduler scheduler;
    const bool schedulerReady = StartScheduler(scheduler, probe);
    Expect(schedulerReady, "scheduler-initializes-with-deferred-release");
    if (!schedulerReady)
    {
        scheduler.Close();
        return;
    }
    Fixture fixture;
    Expect(fixture.ready, "literal-origin-fixture-retains-seed-redo");
    if (!fixture.ready)
    {
        scheduler.Close();
        return;
    }

    auto beforeOpening = fixture.session->history.CaptureState();
    const Guid initialRevision = beforeOpening.Revision();
    const int initialDepth = beforeOpening.Depth();
    const std::size_t initialFuture = beforeOpening.FutureCount();
    const auto generationBeforeOpening = fixture.session->Generation();
    PublicationState opening;
    const auto openingReservation = SubmitSelectedHistory(scheduler, *fixture.session, opening, Operation::Opening);
    Expect(openingReservation == kSlotBytes, "selected-begin-submits-real-retained-original-ticket");
    const bool openingSettled = openingReservation != 0 && Settle(scheduler);
    Expect(openingSettled && opening.completionSeen && opening.result.status == JobStatus::Finished &&
               opening.workerStages == 2 && opening.captured && opening.resumedOriginal &&
               opening.outputBoundToOriginal && opening.temporaryBoundToOriginal && opening.applied,
           "selected-begin-captures-resumes-binds-persistent-output-before-publication");
    if (!openingSettled || !opening.applied)
    {
        scheduler.Close();
        return;
    }
    auto beginState = fixture.session->history.CaptureState();
    Expect(!fixture.session->history.IsCurrent(beforeOpening) &&
               beginState.Revision() == fixture.session->history.CurrentRevision() &&
               beginState.Revision() == initialRevision && beginState.Depth() == initialDepth + 1 &&
               beginState.FutureCount() == initialFuture && fixture.session->Generation() == generationBeforeOpening &&
               fixture.session->IsDeferredSelectionMove() && HasLiteral(fixture.session->Selection(), kOrigin),
           "selected-begin-publishes-literal-origin-and-advances-only-history-epoch");
    opening.temporary.reset();
    const bool openingTemporaryRefunded =
        Settle(scheduler) && scheduler.ReservedMemoryBytes() == opening.outputBytes && scheduler.IsIdle();
    Expect(openingTemporaryRefunded, "selected-begin-temporary-partition-refunds-after-job-retirement");
    Expect(CheckBoundOwner(opening, scheduler) && scheduler.ReservedMemoryBytes() == opening.outputBytes,
           "selected-begin-output-remains-on-its-original-reservation-after-worker-retirement");
    beforeOpening = {};

    fixture.session->MoveSelection(-2, 3);
    Expect(fixture.session->IsMovingSelection() && HasLiteral(fixture.session->Selection(), kOrigin) &&
               fixture.session->DisplayedSelectionValue().translation == Vec2{-2, 3},
           "selected-end-input-keeps-origin-geometry-and-literal-pending-translation");
    const auto openRevision = beginState.Revision();
    const int openDepth = beginState.Depth();
    PublicationState closure;
    const auto closureReservation = SubmitSelectedHistory(scheduler, *fixture.session, closure, Operation::Closure);
    Expect(closureReservation == kSlotBytes, "selected-end-submits-distinct-real-retained-original-ticket");
    const bool closureSettled = closureReservation != 0 && Settle(scheduler);
    Expect(closureSettled && closure.completionSeen && closure.result.status == JobStatus::Finished &&
               closure.workerStages == 2 && closure.captured && closure.resumedOriginal &&
               closure.outputBoundToOriginal && closure.temporaryBoundToOriginal && closure.applied &&
               !(opening.ticket == closure.ticket),
           "selected-end-binds-second-output-to-its-own-original-ticket-before-publication");
    if (!closureSettled || !closure.applied)
    {
        scheduler.Close();
        return;
    }
    auto endState = fixture.session->history.CaptureState();
    const Guid movedRevision = fixture.session->history.CurrentRevision();
    closure.temporary.reset();
    const bool closureTemporaryRefunded =
        Settle(scheduler) && scheduler.ReservedMemoryBytes() == opening.outputBytes + closure.outputBytes &&
        scheduler.IsIdle();
    Expect(closureTemporaryRefunded, "selected-end-temporary-partition-refunds-after-job-retirement");
    Expect(!fixture.session->history.IsCurrent(beginState) && endState.Revision() == movedRevision &&
               endState.Depth() == 0 && endState.FutureCount() == 0 && fixture.session->history.CanUndo() &&
               fixture.session->history.UndoName() == L"Move Selection" &&
               HasLiteral(fixture.session->Selection(), kMoved) && !fixture.session->IsMovingSelection(),
           "selected-end-commits-exact-moved-square-and-one-history-entry");
    Expect(CheckBoundOwner(opening, scheduler) && CheckBoundOwner(closure, scheduler) &&
               scheduler.ReservedMemoryBytes() == opening.outputBytes + closure.outputBytes &&
               beginState.Depth() == openDepth && beginState.Revision() == openRevision,
           "prior-and-current-history-owners-remain-on-their-original-reservations");

    fixture.session->Undo();
    Expect(HasLiteral(fixture.session->Selection(), kOrigin) && fixture.session->history.CanRedo() &&
               fixture.session->history.RedoName() == L"Move Selection" &&
               !fixture.session->history.IsCurrent(endState),
           "literal-undo-restores-origin-and-invalidates-published-snapshot-epoch");
    fixture.session->Redo();
    Expect(HasLiteral(fixture.session->Selection(), kMoved) && fixture.session->history.CanUndo() &&
               !fixture.session->history.CanRedo() && fixture.session->history.CurrentRevision() == movedRevision &&
               !fixture.session->history.IsCurrent(endState),
           "literal-redo-restores-moved-square-and-saved-revision-lineage");
    Expect(CheckBoundOwner(opening, scheduler) && CheckBoundOwner(closure, scheduler) &&
               scheduler.ReservedMemoryBytes() == opening.outputBytes + closure.outputBytes,
           "undo-redo-and-live-history-snapshots-do-not-rebind-or-refund-prior-owners");

    const std::uint64_t retainedBytes = opening.outputBytes + closure.outputBytes;
    fixture.session.reset();
    beginState = {};
    endState = {};
    Expect(Settle(scheduler) && opening.output.expired() && closure.output.expired() &&
               scheduler.ReservedMemoryBytes() == retainedBytes,
           "retired-history-data-leaves-original-charge-held-by-last-weak-controls");
    opening.output.reset();
    closure.output.reset();
    opening.temporary.reset();
    closure.temporary.reset();
    Expect(Settle(scheduler) && scheduler.ReservedMemoryBytes() == 0 && scheduler.IsIdle(),
           "last-weak-control-release-refunds-each-original-slot-exactly-once");
    scheduler.Close();
}

void TestUnboundSelectedBeginAndEndRefuseBeforeMutation()
{
    Fixture fixture;
    Expect(fixture.ready, "unbound-literal-origin-fixture-retains-seed-redo");
    if (!fixture.ready)
        return;
    auto beforeOpening = fixture.session->history.CaptureState();
    const auto generation = fixture.session->Generation();
    const void* const selectionIdentity = fixture.session->Selection()->loops.Identity();
    auto openingInput = fixture.session->PrepareDeferredSelectionMoveOpeningInput();
    auto openingMemory = SharedPublicationMemory::CreateForWork();
    std::optional<PreparedSelectionMoveOpening> opening;
    if (openingInput)
    {
        auto prepared = DocumentSession::PrepareDeferredSelectionMoveOpening(std::move(openingInput.value()), {}, {},
                                                                             openingMemory);
        if (prepared)
            opening.emplace(std::move(prepared.value()));
    }
    const bool openingRefused =
        opening && !fixture.session->ApplyPreparedDeferredSelectionMoveOpeningIfCurrent(opening.value());
    Expect(openingRefused && fixture.session->history.IsCurrent(beforeOpening) &&
               fixture.session->history.CurrentRevision() == beforeOpening.Revision() &&
               fixture.session->Generation() == generation &&
               fixture.session->Selection()->loops.Identity() == selectionIdentity &&
               !fixture.session->IsMovingSelection() && fixture.session->history.CanRedo() &&
               fixture.session->history.RedoName() == L"Seed redo" && !openingMemory->IsBound(),
           "unbound-selected-begin-refuses-before-history-session-or-redo-mutation");
    opening.reset();
    openingInput.reset();
    Expect(openingMemory->LocalAllocationBytes() == 0, "unbound-selected-begin-discard-refunds-private-allocations");

    const bool began = fixture.session->BeginDeferredSelectionMove();
    fixture.session->MoveSelection(-2, 3);
    auto beforeClosure = fixture.session->history.CaptureState();
    const auto closureRevision = beforeClosure.Revision();
    const auto closureGeneration = fixture.session->Generation();
    auto closureInput = fixture.session->PrepareSelectionMoveClosureInput(false);
    auto closureMemory = SharedPublicationMemory::CreateForWork();
    std::optional<PreparedSelectionMoveClosure> closure;
    if (closureInput)
    {
        auto prepared = DocumentSession::PrepareSelectionMoveClosurePublication(std::move(closureInput.value()), {}, {},
                                                                                nullptr, {}, closureMemory);
        if (prepared)
            closure.emplace(std::move(prepared.value()));
    }
    const bool closureRefused =
        closure && !fixture.session->ApplyPreparedSelectionMoveClosureIfCurrent(closure.value());
    Expect(began && closureRefused && fixture.session->history.IsCurrent(beforeClosure) &&
               fixture.session->history.CurrentRevision() == closureRevision &&
               fixture.session->Generation() == closureGeneration && fixture.session->IsMovingSelection() &&
               HasLiteral(fixture.session->Selection(), kOrigin) &&
               fixture.session->DisplayedSelectionValue().translation == Vec2{-2, 3} && !closureMemory->IsBound(),
           "unbound-selected-end-refuses-before-document-history-or-pending-geometry-mutation");
    closure.reset();
    closureInput.reset();
    Expect(closureMemory->LocalAllocationBytes() == 0, "unbound-selected-end-discard-refunds-private-allocations");
}
} // namespace

int RunDocumentSelectionHistoryBoundLifetimeTests()
{
    gAssertions = 0;
    gFailures = 0;
    TestBoundSelectedBeginEndUndoRedoAndWeakControl();
    TestUnboundSelectedBeginAndEndRefuseBeforeMutation();
    std::printf("SUMMARY selection-history-bound-lifetime assertions=%u failures=%u\n", gAssertions, gFailures);
    return gFailures == 0 ? 0 : 1;
}
