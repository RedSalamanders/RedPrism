#include "Host.Tests.SharedRetainedJobMemory.h"

#include "../../Common/Jobs.h"

#include <objbase.h>
#include <windows.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <thread>
#include <utility>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/resource.h>
#pragma warning(pop)

namespace
{
constexpr std::uint64_t kBytes = 4096;
constexpr std::uint64_t kDeclaredBytes = 3 * kBytes;
constexpr std::uint64_t kAvailable = kMemoryAdmissionHeadroomBytes + 8 * 1024 * 1024;

void Expect(bool condition, const char* name, int& failures) noexcept
{
    std::printf("%s shared-retained-job-memory.%s\n", condition ? "PASS" : "FAIL", name);
    failures += condition ? 0 : 1;
}

[[noreturn]] void SetupFailure(const char* name) noexcept
{
    std::printf("FAIL shared-retained-job-memory.%s\n", name);
    std::fflush(stdout);
    std::_Exit(1);
}

wil::unique_handle Event()
{
    wil::unique_handle event{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    if (!event)
        SetupFailure("create-event");
    return event;
}

void Wait(HANDLE event, const char* name) noexcept
{
    if (!event || WaitForSingleObject(event, 10000) != WAIT_OBJECT_0)
        SetupFailure(name);
}

void Settle(JobScheduler& scheduler, const char* name) noexcept
{
    if (SettleJobs(scheduler, 10000) != SettleResult::Settled)
        SetupFailure(name);
}

void Drain(JobScheduler& scheduler) noexcept
{
    (void)DrainCompletions(scheduler);
}

[[nodiscard]] std::int32_t InvalidStateResult() noexcept
{
    return HRESULT_FROM_WIN32(ERROR_INVALID_STATE);
}

[[nodiscard]] std::int32_t QuotaResult() noexcept
{
    return HRESULT_FROM_WIN32(ERROR_NOT_ENOUGH_QUOTA);
}

struct MemoryProbe final
{
    std::uint64_t available = kAvailable;
    unsigned int samples = 0;
};

bool SampleMemory(void* context, MemorySnapshot& snapshot) noexcept
{
    auto& probe = *static_cast<MemoryProbe*>(context);
    ++probe.samples;
    snapshot = {probe.available, probe.available};
    return true;
}

struct TransferState final
{
    JobScheduler* scheduler = nullptr;
    SharedRetainedMemoryLease* target = nullptr;
    SharedRetainedMemoryLease* invalidTarget = nullptr;
    SharedRetainedMemoryLease* repeatTarget = nullptr;
    JobTicket ticket{};
    JobResult completedResult{};
    std::int32_t result = E_UNEXPECTED;
    std::int32_t invalidTargetResult = E_UNEXPECTED;
    std::int32_t repeatResult = E_UNEXPECTED;
    std::uint64_t reservedDuringCallback = 0;
    unsigned int samplesBeforeTransfer = 0;
    unsigned int samplesAfterTransfer = 0;
    unsigned int* samples = nullptr;
    std::atomic<unsigned int> runs{0};
    unsigned int callbacks = 0;
    bool attempt = true;
    JobResult workerResult{};
    HANDLE runEntered = nullptr;
    HANDLE runRelease = nullptr;
};

class TransferJob final : public Job
{
  public:
    explicit TransferJob(TransferState& state) noexcept : _state(state) {}

    JobResult Run(JobContext&) override
    {
        _state.runs.fetch_add(1, std::memory_order_relaxed);
        if (_state.runEntered)
        {
            (void)SetEvent(_state.runEntered);
            Wait(_state.runRelease, "transfer.worker-release");
        }
        return _state.workerResult;
    }

    void Completed(JobResult result) noexcept override
    {
        ++_state.callbacks;
        _state.completedResult = result;
        _state.reservedDuringCallback = _state.scheduler->ReservedMemoryBytes();
        if (!_state.attempt)
            return;
        if (_state.invalidTarget)
            _state.invalidTargetResult = _state.scheduler->RetainMemory(_state.ticket, *_state.invalidTarget);
        if (_state.samples)
            _state.samplesBeforeTransfer = *_state.samples;
        _state.result = _state.scheduler->RetainMemory(_state.ticket, *_state.target);
        if (_state.samples)
            _state.samplesAfterTransfer = *_state.samples;
        if (_state.repeatTarget)
            _state.repeatResult = _state.scheduler->RetainMemory(_state.ticket, *_state.repeatTarget);
    }

  private:
    TransferState& _state;
};

void SubmitTransfer(JobScheduler& scheduler, TransferState& state, std::uint64_t bytes, const char* name,
                    bool allowMemoryWaiting = true)
{
    state.scheduler = &scheduler;
    std::unique_ptr<Job> job = std::make_unique<TransferJob>(state);
    JobTicket ticket;
    const auto result = scheduler.Submit(job, {JobClass::Background, 0, bytes, false, allowMemoryWaiting}, ticket);
    if (result != S_OK || job)
        SetupFailure(name);
    state.ticket = ticket;
}

void Acquire(JobScheduler& scheduler, SharedRetainedMemoryLease& lease, std::uint64_t bytes, TransferState& state,
             const char* submitName, const char* settleName)
{
    state.target = &lease;
    SubmitTransfer(scheduler, state, bytes, submitName);
    Settle(scheduler, settleName);
    if (state.result != S_OK || !lease || lease.Bytes() != bytes)
        SetupFailure("acquire-lease");
}

void TestTransferContract(int& failures)
{
    SharedRetainedMemoryLease empty;
    Expect(!empty && empty.Bytes() == 0 && empty.ResidentBytes() == 0, "default-empty", failures);
    empty.Reset();
    Expect(!empty && empty.Bytes() == 0 && empty.ResidentBytes() == 0, "default-reset-remains-empty", failures);

    JobScheduler scheduler;
    MemoryProbe memory;
    if (scheduler.Initialize(1, {}, {SampleMemory, &memory}) != S_OK)
        SetupFailure("transfer.initialize");

    SharedRetainedMemoryLease occupied;
    TransferState occupiedState;
    Acquire(scheduler, occupied, kBytes, occupiedState, "transfer.occupied-submit", "transfer.occupied-settle");

    SharedRetainedMemoryLease retained;
    SharedRetainedMemoryLease repeated;
    TransferState state;
    state.target = &retained;
    state.invalidTarget = &occupied;
    state.repeatTarget = &repeated;
    state.samples = &memory.samples;
    SubmitTransfer(scheduler, state, kDeclaredBytes, "transfer.submit");
    Settle(scheduler, "transfer.settle");

    Expect(state.invalidTargetResult == E_INVALIDARG && occupied.Bytes() == kBytes,
           "transfer.nonempty-target-refused-with-e-invalidarg", failures);
    Expect(state.result == S_OK && retained && retained.Bytes() == kDeclaredBytes,
           "transfer.exact-successful-callback-transfers-full-declared-bytes", failures);
    Expect(state.repeatResult == InvalidStateResult() && !repeated,
           "transfer.repeated-exact-callback-refused-with-invalid-state", failures);
    Expect(state.samplesBeforeTransfer == state.samplesAfterTransfer, "transfer.success-does-not-query-memory-source",
           failures);
    Expect(state.reservedDuringCallback == kBytes + kDeclaredBytes &&
               scheduler.ReservedMemoryBytes() == kBytes + kDeclaredBytes,
           "transfer.refusals-preserve-original-charges", failures);
    Expect(retained.ResidentBytes() == kBytes + kDeclaredBytes && occupied.ResidentBytes() == kBytes + kDeclaredBytes,
           "transfer.endpoint-reports-all-live-shared-graph-bytes", failures);
    Expect(scheduler.IsIdle(), "transfer.live-lease-keeps-settled-scheduler-idle", failures);

    SharedRetainedMemoryLease outside;
    const auto outsideResult = scheduler.RetainMemory(state.ticket, outside);
    Expect(outsideResult == E_INVALIDARG && !outside && scheduler.ReservedMemoryBytes() == kBytes + kDeclaredBytes,
           "transfer.outside-callback-refused-with-e-invalidarg-and-charge-preserved", failures);

    retained.Reset();
    Expect(!retained && occupied.ResidentBytes() == kBytes &&
               scheduler.ReservedMemoryBytes() == kBytes + kDeclaredBytes && scheduler.UiReadinessForTest().memory,
           "transfer.reset-publishes-ack-without-releasing-owner-ledger", failures);
    Drain(scheduler);
    Expect(scheduler.ReservedMemoryBytes() == kBytes && occupied.ResidentBytes() == kBytes,
           "transfer.owner-drain-consumes-reset-ack", failures);
    occupied.Reset();
    Expect(scheduler.ReservedMemoryBytes() == kBytes && scheduler.UiReadinessForTest().memory,
           "transfer.final-reset-leaves-charge-until-owner-drain", failures);
    Drain(scheduler);
    Expect(scheduler.ReservedMemoryBytes() == 0 && scheduler.IsIdle(), "transfer.final-owner-drain-releases-all",
           failures);
    scheduler.Close();
}

void TestFailedAndCancelledTransfers(int& failures)
{
    JobScheduler scheduler;
    MemoryProbe memory;
    if (scheduler.Initialize(1, {}, {SampleMemory, &memory}) != S_OK)
        SetupFailure("terminal.initialize");
    SharedRetainedMemoryLease failedLease;
    TransferState failed;
    failed.target = &failedLease;
    failed.workerResult = {JobStatus::Failed, E_ACCESSDENIED};
    SubmitTransfer(scheduler, failed, kDeclaredBytes, "terminal.failed-submit");
    Settle(scheduler, "terminal.failed-settle");
    Expect(failed.completedResult.status == JobStatus::Failed && failed.completedResult.error == E_ACCESSDENIED &&
               failed.callbacks == 1,
           "terminal.failed-result-preserved-once", failures);
    Expect(failed.result == InvalidStateResult() && !failedLease, "terminal.failed-transfer-refused-with-invalid-state",
           failures);
    Expect(failed.reservedDuringCallback == kDeclaredBytes && scheduler.ReservedMemoryBytes() == 0,
           "terminal.failed-reservation-survives-callback-and-retires", failures);

    auto entered = Event();
    auto release = Event();
    SharedRetainedMemoryLease cancelledLease;
    TransferState cancelled;
    cancelled.target = &cancelledLease;
    cancelled.runEntered = entered.get();
    cancelled.runRelease = release.get();
    SubmitTransfer(scheduler, cancelled, kDeclaredBytes, "terminal.cancelled-submit");
    Wait(entered.get(), "terminal.worker-entered");
    Expect(scheduler.Cancel(cancelled.ticket), "terminal.running-cancellation-accepted", failures);
    (void)SetEvent(release.get());
    Settle(scheduler, "terminal.cancelled-settle");
    Expect(cancelled.completedResult.status == JobStatus::Cancelled && cancelled.callbacks == 1,
           "terminal.cancelled-result-delivered-once", failures);
    Expect(cancelled.result == InvalidStateResult() && !cancelledLease,
           "terminal.cancelled-transfer-refused-with-invalid-state", failures);
    Expect(cancelled.reservedDuringCallback == kDeclaredBytes && scheduler.ReservedMemoryBytes() == 0,
           "terminal.cancelled-reservation-survives-callback-and-retires", failures);
    scheduler.Close();
}

void TestMoveAcknowledgement(int& failures)
{
    JobScheduler scheduler;
    MemoryProbe memory;
    if (scheduler.Initialize(1, {}, {SampleMemory, &memory}) != S_OK)
        SetupFailure("move.initialize");

    SharedRetainedMemoryLease first;
    SharedRetainedMemoryLease second;
    TransferState firstState;
    TransferState secondState;
    Acquire(scheduler, first, kBytes, firstState, "move.first-submit", "move.first-settle");
    Acquire(scheduler, second, 2 * kBytes, secondState, "move.second-submit", "move.second-settle");

    SharedRetainedMemoryLease moved{std::move(first)};
    Expect(!first && first.Bytes() == 0 && first.ResidentBytes() == 0 && moved.Bytes() == kBytes &&
               moved.ResidentBytes() == 3 * kBytes && scheduler.ReservedMemoryBytes() == 3 * kBytes &&
               !scheduler.UiReadinessForTest().memory,
           "move.construction-transfers-without-ack", failures);

    auto* const self = &moved;
    *self = std::move(*self);
    Expect(moved && moved.Bytes() == kBytes && moved.ResidentBytes() == 3 * kBytes &&
               scheduler.ReservedMemoryBytes() == 3 * kBytes && !scheduler.UiReadinessForTest().memory,
           "move.self-assignment-preserves-lease-without-ack", failures);

    second = std::move(moved);
    Expect(!moved && second && second.Bytes() == kBytes && second.ResidentBytes() == kBytes &&
               scheduler.ReservedMemoryBytes() == 3 * kBytes && scheduler.UiReadinessForTest().memory,
           "move.assignment-publishes-one-overwritten-lease-ack", failures);
    Drain(scheduler);
    Expect(scheduler.ReservedMemoryBytes() == kBytes && second.ResidentBytes() == kBytes,
           "move.owner-consumes-only-overwritten-lease-ack", failures);

    first.Reset();
    moved.Reset();
    Drain(scheduler);
    Expect(scheduler.ReservedMemoryBytes() == kBytes && second.Bytes() == kBytes &&
               !scheduler.UiReadinessForTest().memory,
           "move.moved-empty-reset-cannot-ack-current-lease", failures);
    second.Reset();
    Expect(scheduler.ReservedMemoryBytes() == kBytes && scheduler.UiReadinessForTest().memory,
           "move.final-reset-publishes-single-current-lease-ack", failures);
    Drain(scheduler);
    Expect(scheduler.ReservedMemoryBytes() == 0, "move.final-owner-drain-releases-current-lease", failures);
    scheduler.Close();
}

struct GraphGates final
{
    HANDLE bodyEntered = nullptr;
    HANDLE payloadEntered = nullptr;
    HANDLE payloadRelease = nullptr;
    std::atomic<bool> payloadSawBody{false};
};

struct GatedPayload final
{
    explicit GatedPayload(GraphGates& gates) noexcept : _gates(gates) {}
    ~GatedPayload()
    {
        _gates.payloadSawBody.store(WaitForSingleObject(_gates.bodyEntered, 0) == WAIT_OBJECT_0,
                                    std::memory_order_release);
        (void)SetEvent(_gates.payloadEntered);
        Wait(_gates.payloadRelease, "graph.payload-release");
    }

  private:
    GraphGates& _gates;
};

struct GatedGraph final
{
    GatedGraph(SharedRetainedMemoryLease&& retained, GraphGates& gates) noexcept
        : lease(std::move(retained)), payload(gates), _gates(gates)
    {
    }
    ~GatedGraph()
    {
        (void)SetEvent(_gates.bodyEntered);
    }

    SharedRetainedMemoryLease lease; // First declaration: payload is destroyed before this accounting lease.
    GatedPayload payload;

  private:
    GraphGates& _gates;
};

void TestForeignGraphDestructionOrder(int& failures)
{
    JobScheduler scheduler;
    MemoryProbe memory;
    if (scheduler.Initialize(1, {}, {SampleMemory, &memory}) != S_OK)
        SetupFailure("graph.initialize");

    SharedRetainedMemoryLease graphLease;
    SharedRetainedMemoryLease observer;
    TransferState graphState;
    TransferState observerState;
    Acquire(scheduler, graphLease, kDeclaredBytes, graphState, "graph.lease-submit", "graph.lease-settle");
    Acquire(scheduler, observer, kBytes, observerState, "graph.observer-submit", "graph.observer-settle");

    auto bodyEntered = Event();
    auto payloadEntered = Event();
    auto payloadRelease = Event();
    auto start = Event();
    auto finished = Event();
    GraphGates gates{bodyEntered.get(), payloadEntered.get(), payloadRelease.get()};
    auto graph = std::make_shared<GatedGraph>(std::move(graphLease), gates);
    auto foreignReader = graph;
    graph.reset();
    std::atomic<std::int32_t> apartmentResult{E_UNEXPECTED};
    std::thread foreign(
        [reader = std::move(foreignReader), startHandle = start.get(), finishedHandle = finished.get(),
         &apartmentResult]() mutable noexcept
        {
            const HRESULT initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            apartmentResult.store(initialized, std::memory_order_release);
            Wait(startHandle, "graph.foreign-start");
            reader.reset();
            if (SUCCEEDED(initialized))
                CoUninitialize();
            (void)SetEvent(finishedHandle);
        });

    (void)SetEvent(start.get());
    Wait(bodyEntered.get(), "graph.body-entered");
    Wait(payloadEntered.get(), "graph.payload-entered");
    Expect(gates.payloadSawBody.load(std::memory_order_acquire), "graph.destructor-body-precedes-payload-member",
           failures);
    Expect(observer.ResidentBytes() == kDeclaredBytes + kBytes &&
               scheduler.ReservedMemoryBytes() == kDeclaredBytes + kBytes,
           "graph.foreign-last-reader-blocked-payload-keeps-resident-and-owner-charges", failures);
    Expect(!scheduler.UiReadinessForTest().memory, "graph.lease-ack-not-published-before-payload-destruction",
           failures);

    (void)SetEvent(payloadRelease.get());
    Wait(finished.get(), "graph.foreign-finished");
    foreign.join();
    Expect(apartmentResult.load(std::memory_order_acquire) == S_OK, "graph.last-reader-destroys-on-fresh-mta",
           failures);
    Expect(observer.ResidentBytes() == kBytes && scheduler.ReservedMemoryBytes() == kDeclaredBytes + kBytes &&
               scheduler.UiReadinessForTest().memory,
           "graph.lease-destructor-publishes-ack-after-payload-destruction", failures);
    Drain(scheduler);
    Expect(scheduler.ReservedMemoryBytes() == kBytes && observer.ResidentBytes() == kBytes,
           "graph.owner-drain-consumes-post-destruction-ack", failures);
    observer.Reset();
    Drain(scheduler);
    Expect(scheduler.ReservedMemoryBytes() == 0, "graph.observer-cleanup", failures);
    scheduler.Close();
}

struct MixedCapacityState final
{
    explicit MixedCapacityState(JobScheduler& owner) noexcept : scheduler(owner)
    {
        results.fill(E_UNEXPECTED);
    }

    JobScheduler& scheduler;
    std::array<SharedRetainedMemoryLease, JobScheduler::kRetainedMemoryCapacity - 1> shared;
    RetainedMemoryTicket ownerOnly{};
    std::array<std::int32_t, JobScheduler::kRetainedMemoryCapacity> results{};
};

class MixedCapacityJob final : public Job
{
  public:
    MixedCapacityJob(MixedCapacityState& state, std::size_t ordinal) noexcept : _state(state), _ordinal(ordinal) {}

    JobResult Run(JobContext&) override
    {
        return {};
    }

    void Completed(JobResult) noexcept override
    {
        if (_ordinal < _state.shared.size())
            _state.results[_ordinal] = _state.scheduler.RetainMemory(_ticket, _state.shared[_ordinal]);
        else
            _state.results[_ordinal] = _state.scheduler.RetainMemory(_ticket, _state.ownerOnly);
    }

    void SetTicket(JobTicket ticket) noexcept
    {
        _ticket = ticket;
    }

  private:
    MixedCapacityState& _state;
    std::size_t _ordinal;
    JobTicket _ticket{};
};

void TestSharedCapacityAndReuse(int& failures)
{
    JobScheduler scheduler;
    MemoryProbe memory;
    if (scheduler.Initialize(2, {}, {SampleMemory, &memory}) != S_OK)
        SetupFailure("capacity.initialize");
    MixedCapacityState state{scheduler};
    for (std::size_t index = 0; index < JobScheduler::kRetainedMemoryCapacity; ++index)
    {
        std::unique_ptr<Job> job = std::make_unique<MixedCapacityJob>(state, index);
        auto* const capacityJob = static_cast<MixedCapacityJob*>(job.get());
        JobTicket ticket;
        const auto result = scheduler.Submit(job, {JobClass::Background, 0, kBytes}, ticket);
        if (result != S_OK || job)
            SetupFailure("capacity.submit-first-64");
        capacityJob->SetTicket(ticket);
    }
    Settle(scheduler, "capacity.settle-first-64");
    bool allTransferred = true;
    for (const auto result : state.results)
        allTransferred = allTransferred && result == S_OK;
    Expect(allTransferred && state.ownerOnly.owner != 0,
           "capacity.shared-and-owner-only-transfers-fill-same-64-slot-pool", failures);
    Expect(state.shared.front().ResidentBytes() == (JobScheduler::kRetainedMemoryCapacity - 1) * kBytes &&
               scheduler.ReservedMemoryBytes() == JobScheduler::kRetainedMemoryCapacity * kBytes,
           "capacity.first-64-preserve-complete-ledger-and-shared-residency", failures);

    SharedRetainedMemoryLease overflow;
    TransferState overflowState;
    overflowState.target = &overflow;
    SubmitTransfer(scheduler, overflowState, kBytes, "capacity.overflow-submit");
    Settle(scheduler, "capacity.overflow-settle");
    Expect(overflowState.result == QuotaResult() && !overflow,
           "capacity.65th-shared-transfer-refused-with-not-enough-quota", failures);
    Expect(scheduler.ReservedMemoryBytes() == JobScheduler::kRetainedMemoryCapacity * kBytes,
           "capacity.65th-refusal-retires-only-active-job-reservation", failures);

    SharedRetainedMemoryLease released{std::move(state.shared[0])};
    Expect(!state.shared[0] && released.Bytes() == kBytes, "capacity.move-exposes-one-slot-release-owner", failures);
    released.Reset();
    Expect(scheduler.ReservedMemoryBytes() == JobScheduler::kRetainedMemoryCapacity * kBytes,
           "capacity.reset-ack-does-not-release-before-owner-drain", failures);
    Drain(scheduler);
    Expect(scheduler.ReservedMemoryBytes() == (JobScheduler::kRetainedMemoryCapacity - 1) * kBytes,
           "capacity.owner-drain-frees-exactly-one-slot", failures);

    SharedRetainedMemoryLease replacement;
    TransferState replacementState;
    Acquire(scheduler, replacement, kBytes, replacementState, "capacity.reuse-submit", "capacity.reuse-settle");
    Expect(scheduler.ReservedMemoryBytes() == JobScheduler::kRetainedMemoryCapacity * kBytes &&
               replacement.Bytes() == kBytes,
           "capacity.acknowledged-slot-reused-by-new-shared-lease", failures);
    released.Reset();
    state.shared[0].Reset();
    Drain(scheduler);
    Expect(scheduler.ReservedMemoryBytes() == JobScheduler::kRetainedMemoryCapacity * kBytes &&
               replacement.Bytes() == kBytes && !scheduler.UiReadinessForTest().memory,
           "capacity.old-and-moved-empty-release-cannot-change-reused-reservation", failures);

    Expect(scheduler.ReleaseRetainedMemory(state.ownerOnly), "capacity.owner-only-ticket-releases-its-slot", failures);
    for (auto& lease : state.shared)
        lease.Reset();
    replacement.Reset();
    Drain(scheduler);
    Expect(scheduler.ReservedMemoryBytes() == 0, "capacity.cleanup-releases-all-mixed-slots", failures);
    scheduler.Close();
}

void TestForeignAckWakesAdmissionOnce(int& failures)
{
    constexpr std::uint64_t kLargeBytes = 64 * 1024;
    JobScheduler scheduler;
    MemoryProbe memory{kMemoryAdmissionHeadroomBytes + kLargeBytes};
    if (scheduler.Initialize(1, {}, {SampleMemory, &memory}) != S_OK)
        SetupFailure("wake.initialize");

    SharedRetainedMemoryLease retained;
    TransferState retainedState;
    Acquire(scheduler, retained, kLargeBytes, retainedState, "wake.retained-submit", "wake.retained-settle");
    TransferState waiting;
    waiting.attempt = false;
    SubmitTransfer(scheduler, waiting, kLargeBytes, "wake.waiting-submit");
    Expect(waiting.runs.load(std::memory_order_acquire) == 0 && waiting.callbacks == 0 &&
               scheduler.ReservedMemoryBytes() == kLargeBytes,
           "wake.large-job-remains-blocked-by-live-shared-charge", failures);

    auto start = Event();
    auto resetFinished = Event();
    std::thread foreign(
        [&retained, startHandle = start.get(), finishedHandle = resetFinished.get()]() noexcept
        {
            Wait(startHandle, "wake.foreign-start");
            retained.Reset();
            (void)SetEvent(finishedHandle);
        });
    (void)SetEvent(start.get());
    Wait(resetFinished.get(), "wake.foreign-reset-finished");
    foreign.join();
    Expect(!retained && scheduler.ReservedMemoryBytes() == kLargeBytes &&
               waiting.runs.load(std::memory_order_acquire) == 0 && waiting.callbacks == 0 &&
               scheduler.UiReadinessForTest().memory,
           "wake.foreign-reset-keeps-ledger-and-job-blocked-until-owner-ack", failures);

    Drain(scheduler);
    Settle(scheduler, "wake.waiting-settle");
    Expect(waiting.runs.load(std::memory_order_acquire) == 1 && waiting.callbacks == 1 &&
               waiting.completedResult.status == JobStatus::Finished && scheduler.ReservedMemoryBytes() == 0,
           "wake.owner-ack-wakes-waiting-job-exactly-once", failures);
    scheduler.Close();
}

struct ReadablePayload final
{
    std::uint64_t marker = 0;
};

struct ReadableGraph final
{
    ReadableGraph(SharedRetainedMemoryLease&& retained, std::uint64_t marker) noexcept
        : lease(std::move(retained)), payload{marker}
    {
    }

    SharedRetainedMemoryLease lease; // First declaration: readable payload retires before endpoint acknowledgement.
    ReadablePayload payload;
};

void TestEndpointOutlivesScheduler(int& failures)
{
    constexpr std::uint64_t kFirstBytes = 5 * kBytes;
    constexpr std::uint64_t kSecondBytes = 7 * kBytes;
    std::shared_ptr<ReadableGraph> first;
    std::shared_ptr<ReadableGraph> second;
    {
        auto scheduler = std::make_unique<JobScheduler>();
        MemoryProbe memory;
        if (scheduler->Initialize(1, {}, {SampleMemory, &memory}) != S_OK)
            SetupFailure("close.initialize");
        SharedRetainedMemoryLease firstLease;
        SharedRetainedMemoryLease secondLease;
        TransferState firstState;
        TransferState secondState;
        Acquire(*scheduler, firstLease, kFirstBytes, firstState, "close.first-submit", "close.first-settle");
        Acquire(*scheduler, secondLease, kSecondBytes, secondState, "close.second-submit", "close.second-settle");
        first = std::make_shared<ReadableGraph>(std::move(firstLease), 0x1111222233334444ull);
        second = std::make_shared<ReadableGraph>(std::move(secondLease), 0xaaaabbbbccccddddull);
        Expect(first->lease.ResidentBytes() == kFirstBytes + kSecondBytes &&
                   second->lease.ResidentBytes() == kFirstBytes + kSecondBytes &&
                   first->payload.marker == 0x1111222233334444ull && second->payload.marker == 0xaaaabbbbccccddddull,
               "close.two-readable-graphs-share-complete-endpoint-residency", failures);
        scheduler->Close();
        Expect(scheduler->ReservedMemoryBytes() == 0 && first->lease.ResidentBytes() == kFirstBytes + kSecondBytes &&
                   second->lease.ResidentBytes() == kFirstBytes + kSecondBytes,
               "close.drops-ledger-while-two-graph-leases-own-endpoint", failures);
        scheduler.reset();
    }
    Expect(first->payload.marker == 0x1111222233334444ull && second->payload.marker == 0xaaaabbbbccccddddull &&
               second->lease.ResidentBytes() == kFirstBytes + kSecondBytes,
           "close.graphs-and-endpoint-remain-readable-after-scheduler-destruction", failures);

    auto firstReleased = Event();
    std::atomic<std::int32_t> apartmentResult{E_UNEXPECTED};
    std::atomic<std::uint64_t> foreignMarker{0};
    std::thread foreign(
        [graph = std::move(first), released = firstReleased.get(), &apartmentResult, &foreignMarker]() mutable noexcept
        {
            const HRESULT initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            apartmentResult.store(initialized, std::memory_order_release);
            foreignMarker.store(graph->payload.marker, std::memory_order_release);
            graph.reset();
            if (SUCCEEDED(initialized))
                CoUninitialize();
            (void)SetEvent(released);
        });
    Wait(firstReleased.get(), "close.first-foreign-release");
    foreign.join();
    Expect(apartmentResult.load(std::memory_order_acquire) == S_OK &&
               foreignMarker.load(std::memory_order_acquire) == 0x1111222233334444ull,
           "close.first-graph-readable-and-released-on-fresh-mta", failures);
    Expect(second->lease.ResidentBytes() == kSecondBytes && second->payload.marker == 0xaaaabbbbccccddddull,
           "close.surviving-graph-reports-one-live-charge", failures);

    JobScheduler unrelated;
    MemoryProbe unrelatedMemory;
    if (unrelated.Initialize(1, {}, {SampleMemory, &unrelatedMemory}) != S_OK)
        SetupFailure("close.unrelated-initialize");
    SharedRetainedMemoryLease unrelatedLease;
    TransferState unrelatedState;
    Acquire(unrelated, unrelatedLease, kBytes, unrelatedState, "close.unrelated-submit", "close.unrelated-settle");
    second.reset();
    Expect(unrelated.ReservedMemoryBytes() == kBytes && unrelatedLease.ResidentBytes() == kBytes &&
               !unrelated.UiReadinessForTest().memory,
           "close.late-old-endpoint-release-cannot-touch-unrelated-scheduler", failures);
    Drain(unrelated);
    Expect(unrelated.ReservedMemoryBytes() == kBytes && unrelatedLease.ResidentBytes() == kBytes,
           "close.unrelated-owner-drain-ignores-old-endpoint-ack", failures);
    unrelatedLease.Reset();
    Drain(unrelated);
    Expect(unrelated.ReservedMemoryBytes() == 0, "close.unrelated-cleanup", failures);
    unrelated.Close();
}
} // namespace

int RunSharedRetainedJobMemoryTests()
{
    int failures = 0;
    TestTransferContract(failures);
    TestFailedAndCancelledTransfers(failures);
    TestMoveAcknowledgement(failures);
    TestForeignGraphDestructionOrder(failures);
    TestSharedCapacityAndReuse(failures);
    TestForeignAckWakesAdmissionOnce(failures);
    TestEndpointOutlivesScheduler(failures);
    std::printf("%s shared-retained-job-memory.summary failures=%d\n", failures == 0 ? "PASS" : "FAIL", failures);
    return failures;
}
