#include "../../Common/DeferredRelease.h"
#include "../../Common/Jobs.h"
#include "../../Common/PixelBuffer.h"

#include <windows.h>

#include "JobMemoryTests.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <new>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/resource.h>
#pragma warning(pop)

namespace
{
constexpr std::uint64_t kGiB = 1'073'741'824ULL;
constexpr std::uint64_t kHeadroom = kGiB;

int g_failures = 0;

void Expect(bool condition, const char* name) noexcept
{
    std::printf("%s job-memory.%s\n", condition ? "PASS" : "FAIL", name);
    g_failures += condition ? 0 : 1;
}

wil::unique_handle Event()
{
    wil::unique_handle event{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    if (!event)
    {
        std::printf("FAIL job-memory.create-event\n");
        std::fflush(stdout);
        std::_Exit(1);
    }
    return event;
}

// Wait bounds detect a stuck test; elapsed time is never an assertion.
void Wait(HANDLE event) noexcept
{
    if (!event || WaitForSingleObject(event, 10000) != WAIT_OBJECT_0)
    {
        std::printf("FAIL job-memory.gate-timeout\n");
        std::fflush(stdout);
        std::_Exit(1);
    }
}

void Settle(JobScheduler& scheduler, const char* name) noexcept
{
    const SettleResult result = SettleJobs(scheduler, 10000);
    Expect(result == SettleResult::Settled, name);
    if (result != SettleResult::Settled)
    {
        std::fflush(stdout);
        std::_Exit(1);
    }
}

struct Snapshots final
{
    MemorySnapshot value{kHeadroom, kHeadroom};
    bool succeeds = true;
    unsigned int samples = 0;
};

bool Sample(void* context, MemorySnapshot& snapshot) noexcept
{
    auto& source = *static_cast<Snapshots*>(context);
    ++source.samples;
    snapshot = source.value;
    return source.succeeds;
}

struct Probe final
{
    JobScheduler* scheduler = nullptr;
    HANDLE started = nullptr;
    HANDLE release = nullptr;
    std::atomic<unsigned int> runs{0};
    std::atomic<unsigned int> callbacks{0};
    std::atomic<unsigned int> resultStatus{static_cast<unsigned int>(JobStatus::Failed)};
    std::atomic<std::int32_t> resultError{0};
    std::atomic<std::uint64_t> reservedInRun{0};
    std::atomic<std::uint64_t> reservedInCompleted{0};
    std::atomic<std::uint64_t> reservedInDestructor{0};
    std::atomic<DWORD> runThread{0};
    std::atomic<DWORD> completedThread{0};
    std::atomic<DWORD> destroyedThread{0};
    std::atomic<std::uint64_t> childReserved{0};
    bool waitForRelease = false;
    bool waitForShutdown = false;
    bool throwBadAlloc = false;
};

class ProbeJob final : public Job
{
  public:
    explicit ProbeJob(Probe& probe) noexcept : _probe(probe) {}

    ~ProbeJob() override
    {
        _probe.destroyedThread.store(GetCurrentThreadId(), std::memory_order_relaxed);
        if (_probe.scheduler)
        {
            _probe.reservedInDestructor.store(_probe.scheduler->ReservedMemoryBytes(), std::memory_order_relaxed);
        }
    }

    JobResult Run(JobContext& context) override
    {
        _probe.runThread.store(GetCurrentThreadId(), std::memory_order_relaxed);
        _probe.reservedInRun.store(context.reservedMemoryBytes, std::memory_order_relaxed);
        _probe.runs.fetch_add(1, std::memory_order_relaxed);
        // Model a child stage borrowing the admitted whole-graph peak before the parent blocks or allocates.
        _probe.childReserved.store(context.reservedMemoryBytes, std::memory_order_relaxed);
        if (_probe.started)
        {
            (void)SetEvent(_probe.started);
        }
        if (_probe.throwBadAlloc)
        {
            throw std::bad_alloc{};
        }
        if (_probe.waitForShutdown)
        {
            if (WaitForSingleObject(static_cast<HANDLE>(context.shutdownEvent), 10000) != WAIT_OBJECT_0)
            {
                std::printf("FAIL job-memory.shutdown-event-timeout\n");
                std::fflush(stdout);
                std::_Exit(1);
            }
        }
        else if (_probe.waitForRelease)
        {
            Wait(_probe.release);
        }
        return {};
    }

    void Completed(JobResult result) noexcept override
    {
        _probe.completedThread.store(GetCurrentThreadId(), std::memory_order_relaxed);
        if (_probe.scheduler)
        {
            _probe.reservedInCompleted.store(_probe.scheduler->ReservedMemoryBytes(), std::memory_order_relaxed);
        }
        _probe.resultStatus.store(static_cast<unsigned int>(result.status), std::memory_order_relaxed);
        _probe.resultError.store(result.error, std::memory_order_relaxed);
        _probe.callbacks.fetch_add(1, std::memory_order_relaxed);
    }

  private:
    Probe& _probe;
};

bool Submit(JobScheduler& scheduler, Probe& probe, JobOptions options, JobTicket& ticket) noexcept
{
    std::unique_ptr<Job> job = std::make_unique<ProbeJob>(probe);
    return scheduler.Submit(job, options, ticket) == S_OK && !job;
}

constexpr std::uint64_t Available(std::uint64_t usable) noexcept
{
    return kHeadroom + usable;
}

void TestReservationLifetimeAndContext()
{
    JobScheduler scheduler;
    Snapshots memory{{Available(2 * kGiB), Available(2 * kGiB)}};
    Expect(scheduler.Initialize(1, {}, {Sample, &memory}) == S_OK, "lifetime-initialize");

    auto started = Event();
    auto release = Event();
    Probe probe;
    probe.scheduler = &scheduler;
    probe.started = started.get();
    probe.release = release.get();
    probe.waitForRelease = true;
    constexpr JobOptions options{JobClass::Background, 0, 2 * kGiB};
    JobTicket ticket;
    Expect(Submit(scheduler, probe, options, ticket), "lifetime-submit");
    Wait(started.get());
    Expect(memory.samples != 0 && scheduler.ReservedMemoryBytes() == 2 * kGiB &&
               probe.reservedInRun.load(std::memory_order_relaxed) == 2 * kGiB &&
               probe.childReserved.load(std::memory_order_relaxed) == 2 * kGiB,
           "lifetime-whole-peak-and-child-borrow-context");
    (void)SetEvent(release.get());
    Settle(scheduler, "lifetime-settled");
    const DWORD ownerThread = GetCurrentThreadId();
    Expect(probe.callbacks.load(std::memory_order_relaxed) == 1 &&
               probe.resultStatus.load(std::memory_order_relaxed) == static_cast<unsigned int>(JobStatus::Finished),
           "lifetime-completed-once");
    Expect(probe.completedThread.load(std::memory_order_relaxed) == ownerThread &&
               probe.destroyedThread.load(std::memory_order_relaxed) == ownerThread &&
               probe.runThread.load(std::memory_order_relaxed) != ownerThread,
           "lifetime-worker-run-owner-completion-and-destruction");
    Expect(probe.reservedInCompleted.load(std::memory_order_relaxed) == 2 * kGiB &&
               probe.reservedInDestructor.load(std::memory_order_relaxed) == 2 * kGiB &&
               scheduler.ReservedMemoryBytes() == 0,
           "lifetime-reservation-through-completed-and-job-destruction");
    scheduler.Close();
}

void TestWaitOutsideWorkersAndWholeReservations()
{
    JobScheduler scheduler;
    Snapshots memory{{Available(2 * kGiB), Available(2 * kGiB)}};
    Expect(scheduler.Initialize(2, {}, {Sample, &memory}) == S_OK, "waiting-initialize");

    auto holderStarted = Event();
    auto holderRelease = Event();
    Probe holder;
    holder.scheduler = &scheduler;
    holder.started = holderStarted.get();
    holder.release = holderRelease.get();
    holder.waitForRelease = true;
    JobTicket holderTicket;
    Expect(Submit(scheduler, holder, {JobClass::Background, 0, 2 * kGiB}, holderTicket), "waiting-holder-submit");
    Wait(holderStarted.get());

    Probe waiting;
    waiting.scheduler = &scheduler;
    auto waitingStarted = Event();
    waiting.started = waitingStarted.get();
    JobTicket waitingTicket;
    Expect(Submit(scheduler, waiting, {JobClass::Stroke, 0xA1, 2 * kGiB}, waitingTicket), "waiting-large-submit");
    Expect(scheduler.ReservedMemoryBytes() == 2 * kGiB && waiting.runs.load() == 0, "waiting-no-partial-reservation");
    const auto samplesBeforeIdleDrain = memory.samples;
    (void)DrainCompletions(scheduler);
    (void)DrainCompletions(scheduler);
    Expect(memory.samples == samplesBeforeIdleDrain, "waiting-empty-frame-drains-do-not-poll-memory");

    // A memory waiter must not consume a worker. With one worker already gated, the zero-byte job must still run
    // on the remaining worker despite the unavailable snapshot.
    Probe zero;
    zero.scheduler = &scheduler;
    auto zeroStarted = Event();
    zero.started = zeroStarted.get();
    JobTicket zeroTicket;
    Expect(Submit(scheduler, zero, {JobClass::Background, 0, 0}, zeroTicket), "waiting-zero-byte-submit");
    Wait(zeroStarted.get());
    Expect(zero.runs.load() == 1 && waiting.runs.load() == 0 && scheduler.ReservedMemoryBytes() == 2 * kGiB,
           "waiting-outside-workers-zero-byte-bypasses-pressure");

    // Capacity is global, while FIFO serialization is per key: a fitting independent job can use the remaining GiB.
    memory.value = {Available(3 * kGiB), Available(3 * kGiB)};
    scheduler.NotifyMemoryChanged();
    Probe independent;
    independent.scheduler = &scheduler;
    auto independentStarted = Event();
    independent.started = independentStarted.get();
    JobTicket independentTicket;
    Expect(Submit(scheduler, independent, {JobClass::Stroke, 0xB2, kGiB}, independentTicket),
           "waiting-independent-fit-submit");
    Wait(independentStarted.get());
    Expect(independent.runs.load() == 1 && scheduler.ReservedMemoryBytes() == 3 * kGiB,
           "waiting-independent-key-runs-without-partial-waiter-reservation");

    (void)SetEvent(holderRelease.get());
    Settle(scheduler, "waiting-after-holder-settled");
    Expect(waiting.runs.load() == 1 && waiting.reservedInRun.load() == 2 * kGiB && scheduler.ReservedMemoryBytes() == 0,
           "waiting-release-permits-whole-reservation");
    scheduler.Close();
}

void TestNotifyCapacityAndRefusal()
{
    JobScheduler scheduler;
    Snapshots memory{{Available(kGiB), Available(kGiB)}};
    Expect(scheduler.Initialize(2, {}, {Sample, &memory}) == S_OK, "notify-initialize");
    auto holderStarted = Event();
    auto holderRelease = Event();
    Probe holder;
    holder.scheduler = &scheduler;
    holder.started = holderStarted.get();
    holder.release = holderRelease.get();
    holder.waitForRelease = true;
    JobTicket holderTicket;
    Expect(Submit(scheduler, holder, {JobClass::Background, 0, kGiB}, holderTicket), "notify-holder-submit");
    Wait(holderStarted.get());
    Probe waiting;
    waiting.scheduler = &scheduler;
    auto waitingStarted = Event();
    waiting.started = waitingStarted.get();
    JobTicket waitingTicket;
    Expect(Submit(scheduler, waiting, {JobClass::Background, 0, 2 * kGiB}, waitingTicket), "notify-waiting-submit");
    Expect(waiting.runs.load() == 0 && scheduler.ReservedMemoryBytes() == kGiB, "notify-waiting-unreserved");
    const unsigned int samplesBeforeChange = memory.samples;
    memory.value = {Available(3 * kGiB), Available(3 * kGiB)};
    scheduler.NotifyMemoryChanged();
    (void)DrainCompletions(scheduler); // the documented owner drain retries from a fresh snapshot
    Wait(waitingStarted.get());
    (void)SetEvent(holderRelease.get());
    Settle(scheduler, "notify-capacity-settled");
    Expect(memory.samples > samplesBeforeChange && waiting.runs.load() == 1 && scheduler.ReservedMemoryBytes() == 0,
           "notify-retries-with-fresh-capacity");
    scheduler.Close();

    JobScheduler refusedScheduler;
    Snapshots refusedMemory{{Available(kGiB), Available(kGiB)}};
    Expect(refusedScheduler.Initialize(1, {}, {Sample, &refusedMemory}) == S_OK, "refusal-initialize");
    Probe tooLarge;
    tooLarge.scheduler = &refusedScheduler;
    JobTicket tooLargeTicket;
    Expect(Submit(refusedScheduler, tooLarge, {JobClass::Background, 0, 2 * kGiB}, tooLargeTicket), "refusal-submit");
    Settle(refusedScheduler, "refusal-settled");
    Expect(tooLarge.runs.load() == 0 && tooLarge.callbacks.load() == 1 &&
               tooLarge.resultStatus.load() == static_cast<unsigned int>(JobStatus::Failed) &&
               tooLarge.resultError.load() == static_cast<std::int32_t>(E_OUTOFMEMORY) &&
               refusedScheduler.ReservedMemoryBytes() == 0,
           "refusal-fails-without-run-or-reservation");

    JobScheduler sampleScheduler;
    Snapshots sampleMemory{{Available(kGiB), Available(kGiB)}};
    Expect(sampleScheduler.Initialize(2, {}, {Sample, &sampleMemory}) == S_OK, "sample-failure-initialize");
    auto sampleHolderStarted = Event();
    auto sampleHolderRelease = Event();
    Probe sampleHolder;
    sampleHolder.scheduler = &sampleScheduler;
    sampleHolder.started = sampleHolderStarted.get();
    sampleHolder.release = sampleHolderRelease.get();
    sampleHolder.waitForRelease = true;
    JobTicket sampleHolderTicket;
    Expect(Submit(sampleScheduler, sampleHolder, {JobClass::Background, 0, kGiB}, sampleHolderTicket),
           "sample-failure-holder-submit");
    Wait(sampleHolderStarted.get());
    sampleMemory.succeeds = false;
    Probe sampleFailure;
    sampleFailure.scheduler = &sampleScheduler;
    JobTicket sampleFailureTicket;
    Expect(Submit(sampleScheduler, sampleFailure, {JobClass::Background, 0, kGiB}, sampleFailureTicket),
           "sample-failure-submit");
    sampleMemory.succeeds = true;
    (void)SetEvent(sampleHolderRelease.get());
    Settle(sampleScheduler, "sample-failure-settled");
    Expect(sampleFailure.runs.load() == 0 && sampleFailure.callbacks.load() == 1 &&
               sampleFailure.resultStatus.load() == static_cast<unsigned int>(JobStatus::Failed) &&
               sampleFailure.resultError.load() == static_cast<std::int32_t>(E_OUTOFMEMORY) &&
               sampleHolder.resultStatus.load() == static_cast<unsigned int>(JobStatus::Finished),
           "sample-failure-fails-without-wait-or-run-despite-outstanding-reservation");
    sampleScheduler.Close();

    Probe allocationFailure;
    allocationFailure.scheduler = &refusedScheduler;
    allocationFailure.throwBadAlloc = true;
    refusedMemory.succeeds = true;
    refusedMemory.value = {Available(kGiB), Available(kGiB)};
    JobTicket allocationFailureTicket;
    Expect(Submit(refusedScheduler, allocationFailure, {JobClass::Background, 0, kGiB}, allocationFailureTicket),
           "allocation-failure-submit");
    Settle(refusedScheduler, "allocation-failure-settled");
    Expect(allocationFailure.runs.load() == 1 && allocationFailure.callbacks.load() == 1 &&
               allocationFailure.resultStatus.load() == static_cast<unsigned int>(JobStatus::Failed) &&
               allocationFailure.resultError.load() == static_cast<std::int32_t>(E_OUTOFMEMORY) &&
               refusedScheduler.ReservedMemoryBytes() == 0,
           "allocation-failure-after-admission-releases-reservation");
    refusedScheduler.Close();
}

void TestCancellationAndShutdownWhileWaiting()
{
    JobScheduler scheduler;
    Snapshots memory{{Available(2 * kGiB), Available(2 * kGiB)}};
    Expect(scheduler.Initialize(1, {}, {Sample, &memory}) == S_OK, "cancel-waiting-initialize");
    auto holderStarted = Event();
    auto holderRelease = Event();
    Probe holder;
    holder.scheduler = &scheduler;
    holder.started = holderStarted.get();
    holder.release = holderRelease.get();
    holder.waitForRelease = true;
    JobTicket holderTicket;
    Expect(Submit(scheduler, holder, {JobClass::Background, 0, 2 * kGiB}, holderTicket),
           "cancel-waiting-holder-submit");
    Wait(holderStarted.get());
    Probe cancelled;
    cancelled.scheduler = &scheduler;
    JobTicket cancelledTicket;
    Expect(Submit(scheduler, cancelled, {JobClass::Background, 0, 2 * kGiB}, cancelledTicket), "cancel-waiting-submit");
    Expect(scheduler.Cancel(cancelledTicket), "cancel-waiting-cancel-accepted");
    (void)SetEvent(holderRelease.get());
    Settle(scheduler, "cancel-waiting-settled");
    Expect(cancelled.runs.load() == 0 && cancelled.callbacks.load() == 1 &&
               cancelled.resultStatus.load() == static_cast<unsigned int>(JobStatus::Cancelled),
           "cancel-waiting-drains-cancelled-never-runs");
    scheduler.Close();

    JobScheduler closing;
    Snapshots closeMemory{{Available(2 * kGiB), Available(2 * kGiB)}};
    Expect(closing.Initialize(1, {}, {Sample, &closeMemory}) == S_OK, "close-waiting-initialize");
    auto shutdownStarted = Event();
    Probe shutdownHolder;
    shutdownHolder.scheduler = &closing;
    shutdownHolder.started = shutdownStarted.get();
    shutdownHolder.waitForShutdown = true;
    JobTicket shutdownHolderTicket;
    Expect(Submit(closing, shutdownHolder, {JobClass::Background, 0, 2 * kGiB}, shutdownHolderTicket),
           "close-waiting-holder-submit");
    Wait(shutdownStarted.get());
    Probe shutdownWaiter;
    shutdownWaiter.scheduler = &closing;
    JobTicket shutdownWaiterTicket;
    Expect(Submit(closing, shutdownWaiter, {JobClass::Background, 0, 2 * kGiB}, shutdownWaiterTicket),
           "close-waiting-waiter-submit");
    closing.Close();
    Expect(shutdownHolder.callbacks.load() == 0 && shutdownWaiter.callbacks.load() == 0 &&
               shutdownWaiter.runs.load() == 0 && shutdownWaiter.destroyedThread.load() == GetCurrentThreadId(),
           "close-waiting-joins-drops-callbacks-and-destroys-on-owner");
}

struct SerialState final
{
    HANDLE firstStarted = nullptr;
    HANDLE firstRelease = nullptr;
    HANDLE secondStarted = nullptr;
    std::atomic<std::uint64_t> secondReserved{0};
    std::atomic<unsigned int> secondRanBeforeFirstDestroyed{0};
    std::atomic<unsigned int> firstRetired{0};
    std::atomic<unsigned int> firstDestroyed{0};
};

class SerialProbeJob final : public Job
{
  public:
    SerialProbeJob(SerialState& state, bool first) noexcept : _state(state), _first(first) {}
    ~SerialProbeJob() override
    {
        if (_first)
        {
            _state.firstDestroyed.store(1, std::memory_order_relaxed);
        }
    }

    JobResult Run(JobContext& context) override
    {
        if (_first)
        {
            (void)SetEvent(_state.firstStarted);
            Wait(_state.firstRelease);
        }
        else
        {
            _state.secondReserved.store(context.reservedMemoryBytes, std::memory_order_relaxed);
            if (_state.firstDestroyed.load(std::memory_order_relaxed) == 0)
            {
                _state.secondRanBeforeFirstDestroyed.fetch_add(1, std::memory_order_relaxed);
            }
            (void)SetEvent(_state.secondStarted);
        }
        return {};
    }
    void Completed(JobResult) noexcept override
    {
        if (_first)
        {
            _state.firstRetired.store(1, std::memory_order_relaxed);
        }
    }

  private:
    SerialState& _state;
    bool _first;
};

void TestSerialSuccessorReservesAfterRetirement()
{
    JobScheduler scheduler;
    Snapshots memory{{Available(2 * kGiB), Available(2 * kGiB)}};
    Expect(scheduler.Initialize(2, {}, {Sample, &memory}) == S_OK, "serial-memory-initialize");
    auto firstStarted = Event();
    auto firstRelease = Event();
    auto secondStarted = Event();
    SerialState state{firstStarted.get(), firstRelease.get(), secondStarted.get()};
    constexpr JobOptions options{JobClass::Stroke, 0xA5, kGiB};
    JobTicket firstTicket;
    JobTicket secondTicket;
    std::unique_ptr<Job> first = std::make_unique<SerialProbeJob>(state, true);
    std::unique_ptr<Job> second = std::make_unique<SerialProbeJob>(state, false);
    Expect(scheduler.Submit(first, options, firstTicket) == S_OK && !first, "serial-memory-first-submit");
    Wait(firstStarted.get());
    Expect(scheduler.Submit(second, options, secondTicket) == S_OK && !second, "serial-memory-second-submit");
    Expect(scheduler.ReservedMemoryBytes() == kGiB, "serial-memory-successor-not-partially-reserved");
    (void)SetEvent(firstRelease.get());
    Settle(scheduler, "serial-memory-settled");
    Expect(state.secondReserved.load(std::memory_order_relaxed) == kGiB &&
               state.firstRetired.load(std::memory_order_relaxed) == 1 &&
               state.firstDestroyed.load(std::memory_order_relaxed) == 1 &&
               state.secondRanBeforeFirstDestroyed.load(std::memory_order_relaxed) == 0,
           "serial-memory-successor-reserves-only-after-predecessor-ui-retire");
    scheduler.Close();
}

struct DeferredPayload final
{
    DeferredReleaseNode node;
    HANDLE started = nullptr;
    HANDLE release = nullptr;
    std::atomic<unsigned int> destroyed{0};
    std::atomic<DWORD> destroyThread{0};
};

static_assert(offsetof(DeferredPayload, node) == 0);

void DestroyDeferredPayload(DeferredReleaseNode* node) noexcept
{
    auto& payload = *reinterpret_cast<DeferredPayload*>(node);
    if (payload.started)
    {
        (void)SetEvent(payload.started);
    }
    if (payload.release)
    {
        Wait(payload.release);
    }
    payload.destroyThread.store(GetCurrentThreadId(), std::memory_order_relaxed);
    payload.destroyed.fetch_add(1, std::memory_order_relaxed);
}

void TestDeferredReleaseIntegration()
{
    JobScheduler scheduler;
    Expect(scheduler.Initialize(1) == S_OK, "deferred-initialize");
    Expect(scheduler.EnableDeferredRelease(), "deferred-bind-owner");
    DeferredPayload payload;
    payload.node.bytes = 16ull * 1024 * 1024;
    payload.node.destroy = DestroyDeferredPayload;
    auto releaseStarted = Event();
    auto releaseGate = Event();
    payload.started = releaseStarted.get();
    payload.release = releaseGate.get();
    const DWORD ownerThread = GetCurrentThreadId();

    Expect(TryDeferRelease(payload.node), "deferred-enqueue-metadata-only-16mib");
    Wait(releaseStarted.get());
    Expect(!scheduler.IsIdle() && payload.destroyed.load(std::memory_order_relaxed) == 0,
           "deferred-release-running-keeps-idle-false");
    Expect(scheduler.ReleasingMemoryBytes() == 16ull * 1024 * 1024,
           "deferred-release-bytes-remain-accounted-until-destroy");
    (void)SetEvent(releaseGate.get());
    Settle(scheduler, "deferred-single-release-drained");
    Expect(payload.destroyed.load(std::memory_order_relaxed) == 1 &&
               payload.destroyThread.load(std::memory_order_relaxed) != ownerThread,
           "deferred-single-release-worker-destroys-once");

    // Fill every scheduler record while the sole worker is gated. Deferred release must still enqueue through its
    // intrusive lane; it cannot consume a job slot or be rejected because the job queue is full.
    DeferredPayload fullQueuePayload;
    fullQueuePayload.node.bytes = 16ull * 1024 * 1024;
    fullQueuePayload.node.destroy = DestroyDeferredPayload;
    std::array<Probe, JobScheduler::kCapacity> probes{};
    std::array<JobTicket, JobScheduler::kCapacity> tickets{};
    auto firstStarted = Event();
    auto firstRelease = Event();
    probes[0].scheduler = &scheduler;
    probes[0].started = firstStarted.get();
    probes[0].release = firstRelease.get();
    probes[0].waitForRelease = true;
    Expect(Submit(scheduler, probes[0], {}, tickets[0]), "deferred-full-queue-first-submit");
    Wait(firstStarted.get());
    for (std::size_t index = 1; index < probes.size(); ++index)
    {
        probes[index].scheduler = &scheduler;
        Expect(Submit(scheduler, probes[index], {}, tickets[index]), "deferred-full-queue-submit");
    }
    Expect(TryDeferRelease(fullQueuePayload.node), "deferred-enqueue-while-job-queue-full");
    (void)SetEvent(firstRelease.get());
    Settle(scheduler, "deferred-drained");
    bool everyJobRanOnce = true;
    for (const Probe& probe : probes)
    {
        everyJobRanOnce = everyJobRanOnce && probe.runs.load(std::memory_order_relaxed) == 1;
    }
    Expect(fullQueuePayload.destroyed.load(std::memory_order_relaxed) == 1 &&
               fullQueuePayload.destroyThread.load(std::memory_order_relaxed) != ownerThread && scheduler.IsIdle() &&
               everyJobRanOnce,
           "deferred-full-queue-release-not-starved-and-idle-after-drain");
    scheduler.Close();
}

void TestReleaseEventRetriesAdmission()
{
    JobScheduler scheduler;
    Snapshots memory;
    Expect(scheduler.Initialize(1, {}, {Sample, &memory}) == S_OK && scheduler.EnableDeferredRelease(),
           "release-event-initialize");
    auto started = Event();
    auto release = Event();
    DeferredPayload payload;
    payload.node.bytes = 16ull * 1024 * 1024;
    payload.node.destroy = DestroyDeferredPayload;
    payload.started = started.get();
    payload.release = release.get();
    Expect(TryDeferRelease(payload.node), "release-event-enqueue");
    Wait(started.get());
    Probe waiter;
    waiter.scheduler = &scheduler;
    JobTicket ticket;
    Expect(Submit(scheduler, waiter, {JobClass::Background, 0, kGiB}, ticket), "release-event-waiter-submit");
    Expect(waiter.runs.load() == 0 && scheduler.ReservedMemoryBytes() == 0,
           "release-event-waiter-unreserved-while-memory-is-returning");
    memory.value = {Available(kGiB), Available(kGiB)};
    (void)SetEvent(release.get());
    Settle(scheduler, "release-event-settled");
    Expect(waiter.runs.load() == 1 && waiter.reservedInRun.load() == kGiB && payload.destroyed.load() == 1,
           "release-event-without-job-retirement-admits-waiter");
    scheduler.Close();
}

bool SampleAfterTrim(void* context, MemorySnapshot& snapshot) noexcept
{
    ++*static_cast<unsigned int*>(context);
    const auto available = Available(PixelBufferRecycleBytes() == 0 ? 1024 : 0);
    snapshot = {available, available};
    return true;
}

void TestTrimBeforeRefusal()
{
    constexpr std::size_t bytes = 16 * 1024 * 1024;
    Expect(InitializePixelBufferRecycle(PixelBuffer::RequiredBytes(bytes)), "trim-before-refusal-pool-initialize");
    const auto closePool = wil::scope_exit([]() noexcept { ClosePixelBufferRecycle(); });
    auto cached = PixelBuffer::TryAllocate(bytes);
    Expect(static_cast<bool>(cached), "trim-before-refusal-buffer-allocated");
    cached = {};
    Expect(PixelBufferRecycleBytes() == PixelBuffer::RequiredBytes(bytes), "trim-before-refusal-cache-full");
    JobScheduler scheduler;
    unsigned int samples = 0;
    Expect(scheduler.Initialize(1, {}, {SampleAfterTrim, &samples}) == S_OK, "trim-before-refusal-initialize");
    Probe waiter;
    waiter.scheduler = &scheduler;
    JobTicket ticket;
    Expect(Submit(scheduler, waiter, {JobClass::Background, 0, 512}, ticket), "trim-before-refusal-submit");
    Settle(scheduler, "trim-before-refusal-settled");
    Expect(samples >= 2 && waiter.runs.load() == 1 && waiter.reservedInRun.load() == 512 &&
               waiter.resultStatus.load() == static_cast<unsigned int>(JobStatus::Finished) &&
               PixelBufferRecycleBytes() == 0,
           "trim-before-refusal-resamples-after-worker-trim");
    scheduler.Close();
}

void TestSettledReadiness()
{
    JobScheduler scheduler;
    Snapshots memory;
    Expect(scheduler.Initialize(1, {}, {Sample, &memory}) == S_OK, "settled-readiness-initialize");
    Settle(scheduler, "settled-readiness-initial-drain");
    Expect(scheduler.IsIdle() && !scheduler.HasUiWork(), "settled-readiness-initially-empty");
    scheduler.NotifyMemoryChanged();
    Expect(scheduler.HasUiWork() && !scheduler.IsIdle(), "settled-readiness-memory-notification-is-outstanding");
    Settle(scheduler, "settled-readiness-notification-drained");
    Expect(scheduler.IsIdle() && !scheduler.HasUiWork(), "settled-readiness-no-undelivered-owner-work");
    scheduler.Close();
    Expect(scheduler.IsIdle() && !scheduler.HasUiWork(), "settled-readiness-closed-is-empty");
}
} // namespace

int RunJobMemoryTests()
{
    g_failures = 0;
    TestReservationLifetimeAndContext();
    TestWaitOutsideWorkersAndWholeReservations();
    TestNotifyCapacityAndRefusal();
    TestCancellationAndShutdownWhileWaiting();
    TestSerialSuccessorReservesAfterRetirement();
    TestDeferredReleaseIntegration();
    TestReleaseEventRetriesAdmission();
    TestTrimBeforeRefusal();
    TestSettledReadiness();
    std::fflush(stdout);
    return g_failures;
}
