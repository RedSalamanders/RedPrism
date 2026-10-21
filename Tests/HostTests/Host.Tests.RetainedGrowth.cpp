#include "Host.Tests.RetainedGrowth.h"

#include "Jobs.h"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <new>
#include <thread>
#include <utility>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/resource.h>
#pragma warning(pop)

#include <windows.h>

namespace
{
constexpr DWORD kTimeoutMs = 10000;
constexpr std::uint64_t kInitialBytes = 4096;
constexpr std::uint64_t kObserverBytes = 2048;
constexpr std::uint64_t kGrownBytes = 8192;
constexpr std::uint64_t kFinalBytes = 12288;
constexpr std::uint64_t kWideAvailable = kMemoryAdmissionHeadroomBytes + 16 * 1024 * 1024;

struct Checks final
{
    int failures = 0;

    void Expect(const char* name, bool passed) noexcept
    {
        std::printf("%s retained-growth.%s\n", passed ? "PASS" : "FAIL", name);
        std::fflush(stdout);
        failures += passed ? 0 : 1;
    }
};

[[noreturn]] void SetupFailure(const char* name) noexcept
{
    std::printf("FAIL retained-growth.setup.%s\n", name);
    std::fflush(stdout);
    std::_Exit(1);
}

wil::unique_handle Event(const char* name)
{
    wil::unique_handle event{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    if (!event)
        SetupFailure(name);
    return event;
}

void Wait(HANDLE event, const char* name) noexcept
{
    if (!event || WaitForSingleObject(event, kTimeoutMs) != WAIT_OBJECT_0)
        SetupFailure(name);
}

void Settle(JobScheduler& scheduler, const char* name) noexcept
{
    if (SettleJobs(scheduler, kTimeoutMs) != SettleResult::Settled)
        SetupFailure(name);
}

void Drain(JobScheduler& scheduler) noexcept
{
    (void)DrainCompletions(scheduler);
}

[[nodiscard]] std::int32_t InvalidState() noexcept
{
    return HRESULT_FROM_WIN32(ERROR_INVALID_STATE);
}

struct MemoryProbe final
{
    MemorySnapshot snapshot{kWideAvailable, kWideAvailable};
    unsigned int samples = 0;
    bool succeeds = true;
    SharedRetainedMemoryLease* resetLease = nullptr;
    bool resetDuringNextSample = false;

    static bool Sample(void* context, MemorySnapshot& output) noexcept
    {
        auto& probe = *static_cast<MemoryProbe*>(context);
        ++probe.samples;
        if (probe.resetDuringNextSample)
        {
            probe.resetDuringNextSample = false;
            if (probe.resetLease)
                probe.resetLease->Reset();
        }
        if (!probe.succeeds)
            return false;
        output = probe.snapshot;
        return true;
    }
};

struct AcquireState final
{
    JobScheduler* scheduler = nullptr;
    SharedRetainedMemoryLease* lease = nullptr;
    MemoryProbe* memory = nullptr;
    std::uint64_t growTo = 0;
    JobTicket ticket{};
    std::int32_t retainResult = E_UNEXPECTED;
    std::int32_t growResult = E_UNEXPECTED;
    std::int32_t noOpResult = E_UNEXPECTED;
    std::int32_t shrinkResult = E_UNEXPECTED;
    unsigned int samplesBeforeGrow = 0;
    unsigned int samplesAfterGrow = 0;
    unsigned int samplesAfterNoOp = 0;
    unsigned int samplesAfterShrink = 0;
    std::uint64_t bytesAfterGrow = 0;
    std::uint64_t residentAfterGrow = 0;
    std::uint64_t reservedAfterGrow = 0;
    unsigned int callbacks = 0;
    bool exerciseGrowth = true;
};

class AcquireJob final : public Job
{
  public:
    explicit AcquireJob(AcquireState& state) noexcept : _state(state) {}

    JobResult Run(JobContext&) override
    {
        return {};
    }

    void Completed(JobResult result) noexcept override
    {
        ++_state.callbacks;
        if (result.status != JobStatus::Finished)
            return;
        _state.samplesBeforeGrow = _state.memory->samples;
        _state.retainResult = _state.scheduler->RetainMemory(_state.ticket, *_state.lease);
        if (_state.retainResult != S_OK)
            return;
        if (!_state.exerciseGrowth)
            return;
        _state.samplesBeforeGrow = _state.memory->samples;
        _state.growResult = _state.scheduler->IncreaseRetainedReservation(*_state.lease, _state.growTo);
        _state.samplesAfterGrow = _state.memory->samples;
        _state.bytesAfterGrow = _state.lease->Bytes();
        _state.residentAfterGrow = _state.lease->ResidentBytes();
        _state.reservedAfterGrow = _state.scheduler->ReservedMemoryBytes();
        _state.noOpResult = _state.scheduler->IncreaseRetainedReservation(*_state.lease, _state.growTo);
        _state.samplesAfterNoOp = _state.memory->samples;
        _state.shrinkResult = _state.scheduler->IncreaseRetainedReservation(*_state.lease, kInitialBytes);
        _state.samplesAfterShrink = _state.memory->samples;
    }

  private:
    AcquireState& _state;
};

void SubmitAcquire(JobScheduler& scheduler, AcquireState& state, std::uint64_t bytes, const char* name)
{
    state.scheduler = &scheduler;
    std::unique_ptr<Job> job;
    try
    {
        job = std::make_unique<AcquireJob>(state);
    }
    catch (const std::bad_alloc&)
    {
        SetupFailure(name);
    }
    JobTicket ticket{};
    const auto submitted = scheduler.Submit(job, {JobClass::Background, 0, bytes}, ticket);
    if (submitted != S_OK || job)
        SetupFailure(name);
    state.ticket = ticket;
}

void Acquire(JobScheduler& scheduler, SharedRetainedMemoryLease& lease, MemoryProbe& memory, std::uint64_t bytes,
             std::uint64_t growTo, AcquireState& state, const char* submitName, const char* settleName)
{
    state.lease = &lease;
    state.memory = &memory;
    state.growTo = growTo;
    SubmitAcquire(scheduler, state, bytes, submitName);
    Settle(scheduler, settleName);
}

void TestSuccessfulGrowthAndRefusals(Checks& checks)
{
    JobScheduler scheduler;
    MemoryProbe memory;
    if (scheduler.Initialize(1, {}, {&MemoryProbe::Sample, &memory}) != S_OK)
        SetupFailure("success.initialize");

    SharedRetainedMemoryLease observer;
    AcquireState observerState;
    observerState.exerciseGrowth = false;
    Acquire(scheduler, observer, memory, kObserverBytes, kObserverBytes, observerState, "success.observer-submit",
            "success.observer-settle");

    SharedRetainedMemoryLease lease;
    AcquireState state;
    Acquire(scheduler, lease, memory, kInitialBytes, kGrownBytes, state, "success.submit", "success.settle");

    checks.Expect("success.callback-runs-once-and-retains-original-lease",
                  state.callbacks == 1 && state.retainResult == S_OK && lease && lease.Bytes() == kGrownBytes);
    checks.Expect("success.growth-samples-once-and-preserves-original-live-charge",
                  state.growResult == S_OK && state.samplesAfterGrow == state.samplesBeforeGrow + 1 &&
                      state.bytesAfterGrow == kGrownBytes && state.residentAfterGrow == kObserverBytes + kGrownBytes &&
                      state.reservedAfterGrow == kObserverBytes + kGrownBytes &&
                      scheduler.ReservedMemoryBytes() == kObserverBytes + kGrownBytes);
    checks.Expect("success.no-op-does-not-sample",
                  state.noOpResult == S_OK && state.samplesAfterNoOp == state.samplesAfterGrow);
    checks.Expect("success.shrink-refuses-with-e-invalidarg-without-sampling-or-mutation",
                  state.shrinkResult == E_INVALIDARG && state.samplesAfterShrink == state.samplesAfterNoOp &&
                      lease.Bytes() == kGrownBytes && lease.ResidentBytes() == kObserverBytes + kGrownBytes &&
                      observer.ResidentBytes() == kObserverBytes + kGrownBytes &&
                      scheduler.ReservedMemoryBytes() == kObserverBytes + kGrownBytes);
    checks.Expect("success.live-lease-does-not-prevent-idle",
                  scheduler.IsIdle() && !scheduler.UiReadinessForTest().memory);

    JobScheduler foreign;
    MemoryProbe foreignMemory;
    if (foreign.Initialize(1, {}, {&MemoryProbe::Sample, &foreignMemory}) != S_OK)
        SetupFailure("foreign.initialize");
    const auto foreignSamples = foreignMemory.samples;
    const auto foreignResult = foreign.IncreaseRetainedReservation(lease, kFinalBytes);
    checks.Expect("foreign.scheduler-endpoint-refuses-with-e-invalidarg-before-sampling",
                  foreignResult == E_INVALIDARG && foreignMemory.samples == foreignSamples &&
                      lease.Bytes() == kGrownBytes && scheduler.ReservedMemoryBytes() == kObserverBytes + kGrownBytes);
    foreign.Close();

    SharedRetainedMemoryLease moved{std::move(lease)};
    const auto movedFromResult = scheduler.IncreaseRetainedReservation(lease, kFinalBytes);
    checks.Expect("move.moved-from-lease-refuses-with-e-invalidarg",
                  movedFromResult == E_INVALIDARG && !lease && lease.Bytes() == 0 &&
                      scheduler.ReservedMemoryBytes() == kObserverBytes + kGrownBytes);
    const auto movedGrowthResult = scheduler.IncreaseRetainedReservation(moved, kFinalBytes);
    checks.Expect("move.live-moved-lease-keeps-original-slot-and-grows",
                  movedGrowthResult == S_OK && moved.Bytes() == kFinalBytes &&
                      moved.ResidentBytes() == kObserverBytes + kFinalBytes &&
                      scheduler.ReservedMemoryBytes() == kObserverBytes + kFinalBytes);

    moved.Reset();
    checks.Expect("reuse.last-reset-keeps-grown-charge-until-owner-drain",
                  scheduler.ReservedMemoryBytes() == kObserverBytes + kFinalBytes &&
                      scheduler.UiReadinessForTest().memory);
    Drain(scheduler);
    checks.Expect("reuse.owner-drain-releases-grown-charge",
                  scheduler.ReservedMemoryBytes() == kObserverBytes && scheduler.IsIdle());

    SharedRetainedMemoryLease replacement;
    AcquireState replacementState;
    Acquire(scheduler, replacement, memory, kInitialBytes, kInitialBytes, replacementState, "reuse.replacement-submit",
            "reuse.replacement-settle");
    const auto staleEmptyResult = scheduler.IncreaseRetainedReservation(lease, kFinalBytes);
    checks.Expect("reuse.old-empty-lease-cannot-authorize-reused-slot",
                  staleEmptyResult == E_INVALIDARG && !lease && replacement && replacement.Bytes() == kInitialBytes &&
                      scheduler.ReservedMemoryBytes() == kObserverBytes + kInitialBytes);
    replacement.Reset();
    observer.Reset();
    Drain(scheduler);
    scheduler.Close();
}

void TestAdmissionRefusals(Checks& checks)
{
    for (unsigned int bottleneck = 0; bottleneck != 2; ++bottleneck)
    {
        JobScheduler scheduler;
        MemoryProbe memory;
        if (scheduler.Initialize(1, {}, {&MemoryProbe::Sample, &memory}) != S_OK)
            SetupFailure(bottleneck == 0 ? "physical.initialize" : "commit.initialize");
        SharedRetainedMemoryLease lease;
        AcquireState state;
        Acquire(scheduler, lease, memory, kInitialBytes, kInitialBytes, state,
                bottleneck == 0 ? "physical.submit" : "commit.submit",
                bottleneck == 0 ? "physical.settle" : "commit.settle");
        const unsigned int samplesBefore = memory.samples;
        const std::uint64_t narrow = kMemoryAdmissionHeadroomBytes + kInitialBytes + (kGrownBytes - kInitialBytes) - 1;
        memory.snapshot =
            bottleneck == 0 ? MemorySnapshot{narrow, kWideAvailable} : MemorySnapshot{kWideAvailable, narrow};
        const auto result = scheduler.IncreaseRetainedReservation(lease, kGrownBytes);
        checks.Expect(bottleneck == 0 ? "admission.physical-headroom-refuses-atomically"
                                      : "admission.commit-headroom-refuses-atomically",
                      result == E_OUTOFMEMORY && memory.samples == samplesBefore + 1 &&
                          lease.Bytes() == kInitialBytes && lease.ResidentBytes() == kInitialBytes &&
                          scheduler.ReservedMemoryBytes() == kInitialBytes);
        scheduler.Close();
    }

    JobScheduler scheduler;
    MemoryProbe memory;
    if (scheduler.Initialize(1, {}, {&MemoryProbe::Sample, &memory}) != S_OK)
        SetupFailure("snapshot.initialize");
    SharedRetainedMemoryLease lease;
    AcquireState state;
    Acquire(scheduler, lease, memory, kInitialBytes, kInitialBytes, state, "snapshot.submit", "snapshot.settle");
    const unsigned int samplesBefore = memory.samples;
    memory.succeeds = false;
    const auto result = scheduler.IncreaseRetainedReservation(lease, kGrownBytes);
    checks.Expect("snapshot.failure-returns-e-outofmemory-without-partial-charge",
                  result == E_OUTOFMEMORY && memory.samples == samplesBefore + 1 && lease.Bytes() == kInitialBytes &&
                      lease.ResidentBytes() == kInitialBytes && scheduler.ReservedMemoryBytes() == kInitialBytes);
    scheduler.Close();
}

void TestSamplerRevalidation(Checks& checks)
{
    JobScheduler scheduler;
    MemoryProbe memory;
    if (scheduler.Initialize(1, {}, {&MemoryProbe::Sample, &memory}) != S_OK)
        SetupFailure("revalidation.initialize");
    SharedRetainedMemoryLease lease;
    AcquireState state;
    Acquire(scheduler, lease, memory, kInitialBytes, kInitialBytes, state, "revalidation.submit",
            "revalidation.settle");

    memory.resetLease = &lease;
    memory.resetDuringNextSample = true;
    const unsigned int samplesBefore = memory.samples;
    const auto result = scheduler.IncreaseRetainedReservation(lease, kGrownBytes);
    checks.Expect("revalidation.sampler-reset-invalidates-exact-lease-before-accounting",
                  result == E_INVALIDARG && !memory.resetDuringNextSample && memory.samples == samplesBefore + 1 &&
                      !lease && lease.Bytes() == 0 && scheduler.ReservedMemoryBytes() == kInitialBytes &&
                      scheduler.UiReadinessForTest().memory);
    Drain(scheduler);
    checks.Expect("revalidation.owner-drain-releases-only-original-reservation",
                  scheduler.ReservedMemoryBytes() == 0 && scheduler.IsIdle());
    scheduler.Close();
}

struct ReleaseState final
{
    HANDLE entered = nullptr;
    HANDLE release = nullptr;
    HANDLE finished = nullptr;
};

void TestWorkerFinalReset(Checks& checks)
{
    JobScheduler scheduler;
    MemoryProbe memory;
    if (scheduler.Initialize(1, {}, {&MemoryProbe::Sample, &memory}) != S_OK)
        SetupFailure("worker-reset.initialize");
    SharedRetainedMemoryLease lease;
    AcquireState state;
    Acquire(scheduler, lease, memory, kInitialBytes, kGrownBytes, state, "worker-reset.submit", "worker-reset.settle");
    checks.Expect("worker-reset.precondition-grown-charge",
                  lease.Bytes() == kGrownBytes && scheduler.ReservedMemoryBytes() == kGrownBytes);

    auto entered = Event("worker-reset.entered-event");
    auto release = Event("worker-reset.release-event");
    auto finished = Event("worker-reset.finished-event");
    ReleaseState gates{entered.get(), release.get(), finished.get()};
    std::thread worker(
        [owned = std::move(lease), gates]() mutable noexcept
        {
            (void)SetEvent(gates.entered);
            if (WaitForSingleObject(gates.release, kTimeoutMs) != WAIT_OBJECT_0)
                SetupFailure("worker-reset.release-timeout");
            owned.Reset();
            (void)SetEvent(gates.finished);
        });
    Wait(entered.get(), "worker-reset.entered-timeout");
    (void)SetEvent(release.get());
    Wait(finished.get(), "worker-reset.finished-timeout");
    worker.join();

    checks.Expect("worker-reset.last-reader-publishes-ack-with-full-owner-charge",
                  scheduler.ReservedMemoryBytes() == kGrownBytes && scheduler.UiReadinessForTest().memory);
    Drain(scheduler);
    checks.Expect("worker-reset.owner-drain-releases-complete-grown-charge",
                  scheduler.ReservedMemoryBytes() == 0 && scheduler.IsIdle());

    SharedRetainedMemoryLease replacement;
    AcquireState replacementState;
    Acquire(scheduler, replacement, memory, kInitialBytes, kInitialBytes, replacementState,
            "worker-reset.replacement-submit", "worker-reset.replacement-settle");
    const auto staleResult = scheduler.IncreaseRetainedReservation(lease, kFinalBytes);
    checks.Expect("worker-reset.reused-slot-does-not-authorize-old-empty-lease",
                  staleResult == E_INVALIDARG && !lease && replacement.Bytes() == kInitialBytes &&
                      scheduler.ReservedMemoryBytes() == kInitialBytes);
    replacement.Reset();
    Drain(scheduler);
    scheduler.Close();
}

struct RetainedGraph final
{
    explicit RetainedGraph(SharedRetainedMemoryLease&& value) noexcept : lease(std::move(value)) {}
    void Reset() noexcept
    {
        readableMarker = 0;
        lease.Reset();
    }

    std::uint64_t readableMarker = 0x52455441494E4544ULL;
    SharedRetainedMemoryLease lease;
};

void TestCloseWithGrownGraph(Checks& checks)
{
    JobScheduler scheduler;
    MemoryProbe memory;
    if (scheduler.Initialize(1, {}, {&MemoryProbe::Sample, &memory}) != S_OK)
        SetupFailure("close.initialize");
    SharedRetainedMemoryLease lease;
    AcquireState state;
    Acquire(scheduler, lease, memory, kInitialBytes, kGrownBytes, state, "close.submit", "close.settle");

    std::shared_ptr<RetainedGraph> graph;
    try
    {
        graph = std::make_shared<RetainedGraph>(std::move(lease));
    }
    catch (const std::bad_alloc&)
    {
        SetupFailure("close.graph-allocation");
    }
    std::shared_ptr<RetainedGraph> reader = graph;
    graph.reset();
    scheduler.Close();
    checks.Expect("close.live-grown-endpoint-remains-readable-after-close",
                  reader && reader->readableMarker == 0x52455441494E4544ULL && reader->lease.Bytes() == kGrownBytes &&
                      reader->lease.ResidentBytes() == kGrownBytes && scheduler.ReservedMemoryBytes() == 0);
    const auto closedGrowth = scheduler.IncreaseRetainedReservation(reader->lease, kFinalBytes);
    checks.Expect("close.closed-scheduler-refuses-growth-with-invalid-state",
                  closedGrowth == InvalidState() && reader->lease.Bytes() == kGrownBytes &&
                      reader->lease.ResidentBytes() == kGrownBytes);
    reader->Reset();
    checks.Expect("close.final-graph-reset-releases-live-endpoint-charge",
                  reader->readableMarker == 0 && !reader->lease && reader->lease.Bytes() == 0 &&
                      reader->lease.ResidentBytes() == 0);
    reader.reset();
}

} // namespace

int RunRetainedGrowthTests()
{
    Checks checks;
    TestSuccessfulGrowthAndRefusals(checks);
    TestAdmissionRefusals(checks);
    TestSamplerRevalidation(checks);
    TestWorkerFinalReset(checks);
    TestCloseWithGrownGraph(checks);
    std::printf("%s retained-growth.summary failures=%d\n", checks.failures == 0 ? "PASS" : "FAIL", checks.failures);
    std::fflush(stdout);
    return checks.failures;
}
