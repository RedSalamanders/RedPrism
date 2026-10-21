#include "Document.Tests.BoundedLassoTicket.h"

#include "BoundedLassoPoints.h"
#include "DeferredRelease.h"
#include "Jobs.h"
#include "SharedPublicationMemory.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <iterator>
#include <memory>
#include <optional>
#include <span>
#include <thread>
#include <utility>
#include <vector>

#include <windows.h>
#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/resource.h>
#pragma warning(pop)

namespace
{
constexpr std::uint64_t kAdmissionBytes = 4ULL * 1024 * 1024;
constexpr std::uint64_t kMemorySlack = 16ULL * 1024 * 1024;
constexpr DWORD kGateTimeoutMs = 10'000;
unsigned int gChecks = 0;
unsigned int gFailures = 0;

void Expect(bool condition, const char* label) noexcept
{
    ++gChecks;
    gFailures += condition ? 0u : 1u;
    std::printf("%s bounded-lasso-ticket.%s\n", condition ? "PASS" : "FAIL", label);
}

bool SampleMemory(void*, MemorySnapshot& snapshot) noexcept
{
    const auto available = kMemoryAdmissionHeadroomBytes + kAdmissionBytes + kMemorySlack;
    snapshot = {available, available};
    return true;
}

JobOptions OriginalDescriptor() noexcept
{
    JobOptions options{};
    options.kind = JobClass::Selection;
    options.memoryBytes = kAdmissionBytes;
    options.deferredRetirement = true;
    options.reserveRetainedMemory = true;
    options.allowMemoryWaiting = false;
    return options;
}

Vec2 LiteralPoint(std::uint32_t index) noexcept
{
    return {static_cast<double>(index) - 32.0, static_cast<double>(index % 5u) - 2.0};
}

std::vector<Vec2> IndependentPoints()
{
    std::vector<Vec2> points;
    points.reserve(65);
    for (std::uint32_t index = 0; index < 65; ++index)
        points.push_back(
            Vec2{static_cast<double>(index) - 32.0, static_cast<double>(static_cast<int>(index % 5u) - 2)});
    return points;
}

struct DeferredSink final
{
    std::array<DeferredReleaseNode*, 8> nodes{};
    std::size_t count = 0;
    bool overflow = false;
    std::uint32_t destroyed = 0;
    std::thread::id drainThread{};

    static void Enqueue(void* context, DeferredReleaseNode& node) noexcept
    {
        auto& sink = *static_cast<DeferredSink*>(context);
        if (sink.count == sink.nodes.size())
        {
            sink.overflow = true;
            if (node.destroy)
                node.destroy(&node);
            return;
        }
        sink.nodes[sink.count++] = &node;
    }

    void DrainOnWorker() noexcept
    {
        std::thread worker(
            [this]() noexcept
            {
                drainThread = std::this_thread::get_id();
                for (std::size_t index = 0; index < count; ++index)
                {
                    auto* const node = nodes[index];
                    if (node && node->destroy)
                    {
                        node->destroy(node);
                        ++destroyed;
                    }
                    nodes[index] = nullptr;
                }
            });
        worker.join();
        count = 0;
    }
};

// The first stage acknowledges the descriptor on its owner before allocating the arena. Pump the
// same scheduler hooks, then block on either completion readiness or the worker's real event.
[[nodiscard]] bool WaitForArenaEntered(JobScheduler& scheduler, HANDLE entered) noexcept
{
    const auto started = GetTickCount64();
    for (;;)
    {
        const auto messages = PumpMessages();
        if (messages.quit)
        {
            PostQuitMessage(messages.exitCode);
            return false;
        }
        (void)DrainCompletions(scheduler);
        (void)RunUiActions(scheduler);
        const auto elapsed = GetTickCount64() - started;
        if (elapsed >= kGateTimeoutMs)
            return false;
        if (scheduler.HasUiWork())
            continue;
        const HANDLE waits[]{entered, static_cast<HANDLE>(scheduler.WakeHandle())};
        if (!waits[1])
            return false;
        const auto waited = MsgWaitForMultipleObjectsEx(2, waits, static_cast<DWORD>(kGateTimeoutMs - elapsed),
                                                        QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        if (waited == WAIT_OBJECT_0)
            return true;
        if (waited != WAIT_OBJECT_0 + 1 && waited != WAIT_OBJECT_0 + 2)
            return false;
    }
}

struct TicketState final
{
    JobScheduler* scheduler = nullptr;
    JobTicket ticket{};
    HANDLE entered = nullptr;
    HANDLE resume = nullptr;
    std::shared_ptr<SharedPublicationMemory> memory;
    BoundedLassoPoints::Snapshot snapshot;
    JobResult terminal{JobStatus::Failed, E_UNEXPECTED};
    std::int32_t bindResult = E_UNEXPECTED;
    std::uint64_t localCharge = 0;
    std::uint64_t boundBytes = 0;
    std::uint64_t boundResidentBytes = 0;
    std::uint64_t reservedAtCompletion = 0;
    bool admissionHeldBeforeArena = false;
    bool arenaCreated = false;
    bool pointsCopiedOnWorker = false;
    bool enteredAfterSnapshot = false;
    bool cancellationObserved = false;
    bool completionOnOwner = false;
    bool boundToOriginal = false;
    bool joinedRetirementCalled = false;
};

void BindForTerminalRetirement(TicketState& state, JobScheduler& scheduler, JobTicket ticket) noexcept
{
    state.completionOnOwner = scheduler.InUiCallback();
    if (!state.memory)
        return;
    state.bindResult = state.memory->BindForRetirement(scheduler, ticket);
    state.boundToOriginal = state.memory->BoundTo(scheduler, ticket);
    state.boundBytes = state.memory->Bytes();
    state.boundResidentBytes = state.memory->ResidentBytes();
    state.memory.reset();
    state.reservedAtCompletion = scheduler.ReservedMemoryBytes();
}

class CancelledArenaJob final : public Job
{
  public:
    explicit CancelledArenaJob(TicketState& state) noexcept : _state(state) {}

    JobResult Run(JobContext& context) override
    {
        if (!_beginArena)
        {
            _contextAdmissionHeld = context.reservedMemoryBytes == kAdmissionBytes && context.deferredRetirement;
            return {};
        }
        if (!_state.admissionHeldBeforeArena)
            return {JobStatus::Failed, E_OUTOFMEMORY};

        const auto localLimit = BoundedLassoPoints::MemoryBytes(1);
        auto memory = SharedPublicationMemory::CreateForWork(localLimit);
        _state.memory = memory;
        auto writer = BoundedLassoPoints::CreateForWork(memory, 1, context.cancellation);
        if (!writer)
            return {JobStatus::Failed, E_OUTOFMEMORY};
        _state.arenaCreated = true;
        _state.localCharge = memory->LocalAllocationBytes();

        for (std::uint32_t index = 0; index < 65; ++index)
            if (!writer->PushOwner(LiteralPoint(index)))
                return {JobStatus::Failed, E_OUTOFMEMORY};

        _state.snapshot = writer->CaptureOwner();
        std::vector<Vec2> copied;
        const auto expected = IndependentPoints();
        _state.pointsCopiedOnWorker = _state.snapshot.CopyToForWork(copied) && copied == expected;
        writer.reset();
        _state.enteredAfterSnapshot = SetEvent(_state.entered) != FALSE;
        if (!_state.enteredAfterSnapshot)
            return {JobStatus::Failed, HRESULT_FROM_WIN32(GetLastError())};

        const HANDLE waits[]{_state.resume, static_cast<HANDLE>(context.shutdownEvent)};
        const DWORD waited = WaitForMultipleObjects(static_cast<DWORD>(std::size(waits)), waits, FALSE, kGateTimeoutMs);
        _state.cancellationObserved = context.cancellation.IsCancelled();
        if (waited == WAIT_OBJECT_0 && _state.cancellationObserved)
            return {JobStatus::Cancelled, HRESULT_FROM_WIN32(ERROR_CANCELLED)};
        return {JobStatus::Failed, waited == WAIT_TIMEOUT ? HRESULT_FROM_WIN32(ERROR_TIMEOUT) : E_ABORT};
    }

    [[nodiscard]] bool DeferCompletion(JobResult result, JobTicket ticket) noexcept override
    {
        if (_beginArena || result.status != JobStatus::Finished)
            return false;
        _state.admissionHeldBeforeArena =
            _contextAdmissionHeld && _state.scheduler->ReservedMemoryBytes() == kAdmissionBytes;
        _beginArena = true;
        return _state.admissionHeldBeforeArena && _state.scheduler->Resume(ticket);
    }

    void Completed(JobResult result) noexcept override
    {
        _state.terminal = result;
        BindForTerminalRetirement(_state, *_state.scheduler, _state.ticket);
    }

    void RetainJoinedRetirement(JobScheduler& scheduler, JobTicket ticket) noexcept override
    {
        _state.joinedRetirementCalled = true;
        BindForTerminalRetirement(_state, scheduler, ticket);
    }

  private:
    TicketState& _state;
    bool _beginArena = false;
    bool _contextAdmissionHeld = false;
};

struct SuccessfulTicketState final
{
    JobScheduler* scheduler = nullptr;
    JobTicket ticket{};
    std::shared_ptr<SharedPublicationMemory> memory;
    std::optional<BoundedLassoPoints> stagedWriter;
    BoundedLassoPoints::Snapshot stagedSnapshot;
    std::optional<BoundedLassoPoints> writer;
    BoundedLassoPoints::Snapshot snapshot;
    JobResult terminal{JobStatus::Failed, E_UNEXPECTED};
    std::int32_t bindResult = E_UNEXPECTED;
    std::uint64_t localCharge = 0;
    std::uint64_t boundBytes = 0;
    std::uint64_t reservedAtCompletion = 0;
    const void* workerSnapshotIdentity = nullptr;
    bool admissionHeldBeforeArena = false;
    bool copiedIndependentPoints = false;
    bool completionOnOwner = false;
    bool boundToOriginal = false;
    bool allocationsSealed = false;
    bool exposedAfterBinding = false;
};

class SuccessfulArenaJob final : public Job
{
  public:
    explicit SuccessfulArenaJob(SuccessfulTicketState& state) noexcept : _state(state) {}

    JobResult Run(JobContext& context) override
    {
        if (!_beginArena)
        {
            _contextAdmissionHeld = context.reservedMemoryBytes == kAdmissionBytes && context.deferredRetirement;
            return {};
        }
        if (!_state.admissionHeldBeforeArena)
            return {JobStatus::Failed, E_OUTOFMEMORY};

        auto memory = SharedPublicationMemory::CreateForWork(BoundedLassoPoints::MemoryBytes(1));
        _state.memory = memory;
        auto writer = BoundedLassoPoints::CreateForWork(memory, 1, context.cancellation);
        if (!writer)
            return {JobStatus::Failed, E_OUTOFMEMORY};
        _state.localCharge = memory->LocalAllocationBytes();
        for (std::uint32_t index = 0; index < 65; ++index)
            if (!writer->PushOwner(LiteralPoint(index)))
                return {JobStatus::Failed, E_OUTOFMEMORY};

        _state.stagedWriter = std::move(writer);
        _state.stagedSnapshot = _state.stagedWriter->CaptureOwner();
        _state.workerSnapshotIdentity = _state.stagedSnapshot.ArenaIdentity();
        std::vector<Vec2> copied;
        _state.copiedIndependentPoints = _state.stagedSnapshot.CopyToForWork(copied) && copied == IndependentPoints();
        return {JobStatus::Finished, S_OK};
    }

    [[nodiscard]] bool DeferCompletion(JobResult result, JobTicket ticket) noexcept override
    {
        if (_beginArena || result.status != JobStatus::Finished)
            return false;
        _state.admissionHeldBeforeArena =
            _contextAdmissionHeld && _state.scheduler->ReservedMemoryBytes() == kAdmissionBytes;
        _beginArena = true;
        return _state.admissionHeldBeforeArena && _state.scheduler->Resume(ticket);
    }

    void Completed(JobResult result) noexcept override
    {
        _state.terminal = result;
        _state.completionOnOwner = _state.scheduler->InUiCallback();
        if (!_state.memory)
            return;
        if (result.status == JobStatus::Finished && result.error == S_OK)
        {
            _state.bindResult = _state.memory->BindAfterCompletedWork(*_state.scheduler, _state.ticket);
            _state.boundToOriginal = _state.memory->BoundTo(*_state.scheduler, _state.ticket);
            _state.boundBytes = _state.memory->Bytes();
            _state.allocationsSealed = !_state.memory->TryAccountLocalAllocation(1);
            if (_state.bindResult == S_OK && _state.boundToOriginal && _state.allocationsSealed)
            {
                _state.writer = std::move(_state.stagedWriter);
                _state.snapshot = std::move(_state.stagedSnapshot);
                _state.exposedAfterBinding = _state.writer.has_value() && _state.snapshot.Count() == 65;
            }
            _state.reservedAtCompletion = _state.scheduler->ReservedMemoryBytes();
            return;
        }
        _state.bindResult = _state.memory->BindForRetirement(*_state.scheduler, _state.ticket);
        _state.boundToOriginal = _state.memory->BoundTo(*_state.scheduler, _state.ticket);
        _state.boundBytes = _state.memory->Bytes();
        _state.memory.reset();
    }

    void RetainJoinedRetirement(JobScheduler& scheduler, JobTicket ticket) noexcept override
    {
        if (_state.memory)
            static_cast<void>(_state.memory->BindForRetirement(scheduler, ticket));
    }

  private:
    SuccessfulTicketState& _state;
    bool _beginArena = false;
    bool _contextAdmissionHeld = false;
};
void RestoreSink(DeferredSink& sink, DeferredReleaseDetail::Sink prior, bool installed) noexcept
{
    if (installed)
        static_cast<void>(ClearDeferredReleaseSink(&sink));
    DeferredReleaseDetail::sink = prior;
}
} // namespace

int RunBoundedLassoTicketTests()
{
    gChecks = 0;
    gFailures = 0;
    const bool arenaFitsAdmission =
        SharedPublicationMemory::OwnershipAllowance() + BoundedLassoPoints::MemoryBytes(1) < kAdmissionBytes;
    const auto priorSink = DeferredReleaseDetail::sink;
    DeferredReleaseDetail::sink = {};
    DeferredSink sink;
    const bool sinkInstalled = InstallDeferredReleaseSink(&DeferredSink::Enqueue, &sink);
    Expect(sinkInstalled && arenaFitsAdmission, "fixture.installs-isolated-deferred-retirement-sink");
    if (!sinkInstalled)
    {
        DeferredReleaseDetail::sink = priorSink;
        std::printf("FAIL bounded-lasso-ticket.summary checks=%u failures=%u\n", gChecks, gFailures);
        return 1;
    }

    JobScheduler scheduler;
    const auto initialized = scheduler.Initialize(1, {}, {SampleMemory, nullptr});
    Expect(initialized == S_OK, "scheduler.initializes-one-real-worker-with-literal-memory-source");
    if (initialized != S_OK)
    {
        scheduler.Close();
        RestoreSink(sink, priorSink, sinkInstalled);
        std::printf("FAIL bounded-lasso-ticket.summary checks=%u failures=%u\n", gChecks, gFailures);
        return 1;
    }

    wil::unique_handle entered{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    wil::unique_handle resume{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    const bool gatesCreated = entered && resume;
    Expect(gatesCreated, "fixture.creates-event-gates-for-real-worker-cancellation");

    TicketState state;
    state.scheduler = &scheduler;
    state.entered = entered.get();
    state.resume = resume.get();
    std::unique_ptr<Job> job;
    bool submitted = false;
    if (gatesCreated)
    {
        job = std::make_unique<CancelledArenaJob>(state);
        const auto submitResult = scheduler.Submit(job, OriginalDescriptor(), state.ticket);
        submitted = submitResult == S_OK && !job;
        Expect(submitted, "admission.submits-one-original-ticket-with-complete-retirement-reservation");
    }
    else
    {
        Expect(false, "admission.submits-one-original-ticket-with-complete-retirement-reservation");
    }

    bool gateReached = false;
    bool cancellationAccepted = false;
    bool gateReleased = false;
    bool settled = false;
    if (submitted)
    {
        gateReached = WaitForArenaEntered(scheduler, entered.get());
        Expect(gateReached, "worker.reaches-gate-only-after-arena-and-snapshot-are-created");
        if (gateReached)
        {
            cancellationAccepted = scheduler.Cancel(state.ticket);
            Expect(cancellationAccepted, "cancel.owner-cancels-the-exact-original-job-ticket");
            gateReleased = SetEvent(resume.get()) != FALSE;
            Expect(gateReleased, "cancel.owner-releases-worker-gate-after-ticket-cancellation");
        }
        else
        {
            static_cast<void>(scheduler.Cancel(state.ticket));
            if (resume)
                static_cast<void>(SetEvent(resume.get()));
        }
        settled = SettleJobs(scheduler, kGateTimeoutMs) == SettleResult::Settled;
        Expect(settled, "scheduler.joins-cancelled-original-work-and-delivers-terminal-callback");
    }
    else
    {
        Expect(false, "worker.reaches-gate-only-after-arena-and-snapshot-are-created");
        Expect(false, "cancel.owner-cancels-the-exact-original-job-ticket");
        Expect(false, "cancel.owner-releases-worker-gate-after-ticket-cancellation");
        Expect(false, "scheduler.joins-cancelled-original-work-and-delivers-terminal-callback");
    }

    if (!settled)
        scheduler.Close(); // Join before inspecting or releasing worker-published fixture state.

    const auto memoryAllowance =
        SharedPublicationMemory::OwnershipAllowance() + BoundedLassoPoints::MemoryBytes(1) + 65u * sizeof(Vec2) + 128u;
    Expect(state.admissionHeldBeforeArena && state.arenaCreated && state.localCharge != 0 &&
               state.localCharge <= BoundedLassoPoints::MemoryBytes(1) && memoryAllowance < kAdmissionBytes,
           "worker.original-ticket-reservation-covers-memory-owner-arena-and-worker-output-before-creation");
    Expect(state.pointsCopiedOnWorker, "worker.snapshot-copies-65-independent-literal-points-before-return");
    Expect(gateReached && state.enteredAfterSnapshot && cancellationAccepted && gateReleased &&
               state.cancellationObserved,
           "cancel.token-is-observed-by-the-gated-original-worker");
    Expect(settled && state.terminal.status == JobStatus::Cancelled && state.terminal.error == 0,
           "cancel.original-job-completes-with-exact-cancelled-result");
    Expect(settled && state.completionOnOwner && state.bindResult == S_OK && state.boundToOriginal &&
               state.boundBytes == kAdmissionBytes && state.boundResidentBytes == kAdmissionBytes,
           "cancel.terminal-owner-callback-binds-retirement-lease-to-original-ticket");
    const auto expected = IndependentPoints();
    const auto bounds = state.snapshot.Bounds();
    Expect(settled && !state.memory && state.snapshot.Count() == 65 && !state.snapshot.Empty() &&
               state.snapshot.ArenaIdentity() != nullptr && state.snapshot.RetainedArenaBytes() != 0 && bounds &&
               bounds->minX == -32.0 && bounds->minY == -2.0 && bounds->maxX == 32.0 && bounds->maxY == 2.0 &&
               expected.size() == 65,
           "cancel.immutable-snapshot-survives-worker-and-original-terminal-completion");
    Expect(settled && state.reservedAtCompletion == kAdmissionBytes &&
               scheduler.ReservedMemoryBytes() == kAdmissionBytes,
           "cancel.original-complete-admission-remains-charged-while-snapshot-is-last-strong-owner");

    state.snapshot = BoundedLassoPoints::Snapshot{};
    const bool queuedLastSnapshot = sink.count == 1 && !sink.overflow;
    Expect(queuedLastSnapshot && scheduler.ReservedMemoryBytes() == kAdmissionBytes,
           "retirement.last-snapshot-release-defers-arena-and-keeps-original-admission-charged");
    const bool sinkCleared = ClearDeferredReleaseSink(&sink);
    Expect(sinkCleared, "retirement.clears-owner-sink-before-worker-drain");
    sink.DrainOnWorker();
    Expect(sink.destroyed == 1 && sink.drainThread != std::this_thread::get_id(),
           "retirement.worker-destroys-the-single-deferred-arena-node");
    Expect(scheduler.ReservedMemoryBytes() == kAdmissionBytes,
           "retirement.foreign-final-release-awaits-original-owner-acknowledgement");
    (void)DrainCompletions(scheduler);
    Expect(scheduler.ReservedMemoryBytes() == 0,
           "retirement.owner-drain-refunds-exact-original-ticket-admission-after-last-snapshot");

    const bool sinkReinstalled = InstallDeferredReleaseSink(&DeferredSink::Enqueue, &sink);
    Expect(sinkReinstalled, "success.fixture.reinstalls-isolated-deferred-retirement-sink");
    SuccessfulTicketState successful;
    successful.scheduler = &scheduler;
    std::unique_ptr<Job> successfulJob = std::make_unique<SuccessfulArenaJob>(successful);
    const auto successfulSubmit = scheduler.Submit(successfulJob, OriginalDescriptor(), successful.ticket);
    const bool successfulSubmitted = successfulSubmit == S_OK && !successfulJob;
    Expect(successfulSubmitted, "success.admission.submits-original-ticket-before-arena-creation");
    const bool successSettled = successfulSubmitted && SettleJobs(scheduler, kGateTimeoutMs) == SettleResult::Settled;
    Expect(successSettled, "success.scheduler.settles-successful-original-ticket");
    const auto successAllowance =
        SharedPublicationMemory::OwnershipAllowance() + BoundedLassoPoints::MemoryBytes(1) + 65u * sizeof(Vec2) + 128u;
    Expect(successSettled && successful.admissionHeldBeforeArena && successful.localCharge != 0 &&
               successful.localCharge <= BoundedLassoPoints::MemoryBytes(1) && successAllowance < kAdmissionBytes,
           "success.worker.creates-writer-and-snapshot-under-complete-original-reservation");
    Expect(successSettled && successful.copiedIndependentPoints,
           "success.worker.copies-independent-65-point-reference-before-completion");
    Expect(successSettled && successful.completionOnOwner && successful.terminal.status == JobStatus::Finished &&
               successful.terminal.error == S_OK,
           "success.completed.callback-is-owner-and-receives-finished-result");
    Expect(successSettled && successful.bindResult == S_OK && successful.boundToOriginal &&
               successful.boundBytes == kAdmissionBytes,
           "success.completed.binds-after-completed-work-to-exact-original-ticket");
    Expect(successSettled && successful.allocationsSealed && successful.exposedAfterBinding &&
               successful.writer.has_value() &&
               successful.snapshot.ArenaIdentity() == successful.workerSnapshotIdentity,
           "success.completed.seals-allocation-before-marking-writer-and-snapshot-exposed");
    Expect(successSettled && successful.reservedAtCompletion == kAdmissionBytes &&
               scheduler.ReservedMemoryBytes() == kAdmissionBytes,
           "success.completed.original-reservation-remains-whole-while-arena-is-retained");
    const auto successfulBounds = successful.snapshot.Bounds();
    Expect(successSettled && successful.snapshot.Count() == 65 && !successful.snapshot.Empty() &&
               successful.snapshot.ArenaIdentity() != nullptr && successfulBounds && successfulBounds->minX == -32.0 &&
               successfulBounds->minY == -2.0 && successfulBounds->maxX == 32.0 && successfulBounds->maxY == 2.0,
           "success.snapshot.retains-worker-captured-65-point-arena-after-completion");
    const bool completedTicketCancelRefused = successfulSubmitted && !scheduler.Cancel(successful.ticket);
    Expect(completedTicketCancelRefused, "success.ticket.owner-cancel-refuses-already-completed-original-ticket");
    const bool poppedTail = successful.writer && successful.writer->PopOwner() && successful.writer->CountOwner() == 64;
    Expect(poppedTail, "success.writer.pop-removes-only-the-tail-point");
    if (successful.writer)
        successful.writer->ClearOwner();
    Expect(successful.writer && successful.writer->CountOwner() == 0 && successful.writer->SealedBlocksOwner() == 1 &&
               successful.snapshot.Count() == 65,
           "success.writer.clear-resets-live-prefix-without-reclaiming-sealed-node");
    successful.memory.reset();
    successful.writer.reset();
    Expect(completedTicketCancelRefused && successful.snapshot.Count() == 65 &&
               scheduler.ReservedMemoryBytes() == kAdmissionBytes,
           "success.retirement.clear-pop-and-completed-cancel-refusal-keep-charge-under-snapshot");
    successful.snapshot = BoundedLassoPoints::Snapshot{};
    const bool successSnapshotQueued = sink.count == 1 && !sink.overflow;
    Expect(successSnapshotQueued && scheduler.ReservedMemoryBytes() == kAdmissionBytes,
           "success.retirement.last-snapshot-defers-arena-and-still-holds-charge");
    const bool successSinkCleared = ClearDeferredReleaseSink(&sink);
    Expect(successSinkCleared, "success.retirement.clears-owner-sink-before-worker-drain");
    sink.DrainOnWorker();
    Expect(sink.destroyed == 2 && sink.drainThread != std::this_thread::get_id(),
           "success.retirement.worker-destroys-successful-arena-after-last-snapshot");
    Expect(scheduler.ReservedMemoryBytes() == kAdmissionBytes,
           "success.retirement.foreign-destruction-awaits-original-owner-ack");
    (void)DrainCompletions(scheduler);
    Expect(scheduler.ReservedMemoryBytes() == 0, "success.retirement.owner-ack-refunds-original-success-ticket-charge");
    scheduler.Close();
    DeferredReleaseDetail::sink = priorSink;
    std::printf("%s bounded-lasso-ticket.summary checks=%u failures=%u\n", gFailures == 0 ? "PASS" : "FAIL", gChecks,
                gFailures);
    return static_cast<int>(gFailures);
}
