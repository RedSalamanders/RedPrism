#include "JobSuspendedRetirementTests.h"

#include "../../Common/DeferredRelease.h"
#include "../../Common/Jobs.h"

#include <atomic>
#include <combaseapi.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>
#include <windows.h>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/resource.h>
#pragma warning(pop)

namespace
{
constexpr std::uint64_t kMiB = 1024ull * 1024;
constexpr std::uint64_t kPayloadBytes = 16 * kMiB;
constexpr std::uint64_t kReservationBytes = 32 * kMiB;
constexpr std::uint8_t kPayloadPattern = 0xA7;
int gFailures = 0;

void Expect(bool condition, const char* name) noexcept
{
    std::printf("%s job-suspended-retirement.%s\n", condition ? "PASS" : "FAIL", name);
    gFailures += condition ? 0 : 1;
}

wil::unique_handle Event(const char* name)
{
    wil::unique_handle event{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    if (!event)
    {
        std::printf("FAIL job-suspended-retirement.create-event name=%s\n", name);
        std::fflush(stdout);
        std::_Exit(1);
    }
    return event;
}

void Wait(HANDLE event, const char* name) noexcept
{
    const DWORD result = event ? WaitForSingleObject(event, 10000) : WAIT_FAILED;
    if (result != WAIT_OBJECT_0)
    {
        std::printf("FAIL job-suspended-retirement.gate-timeout name=%s wait=0x%08lx\n", name, result);
        std::fflush(stdout);
        std::_Exit(1);
    }
}

bool SampleMemory(void*, MemorySnapshot& snapshot) noexcept
{
    constexpr std::uint64_t available = 2ull * 1024 * 1024 * 1024;
    snapshot = {available, available};
    return true;
}

struct Probe final
{
    JobScheduler* scheduler = nullptr;
    const DWORD ownerThread = GetCurrentThreadId();
    std::atomic<unsigned int> runs{0};
    std::atomic<unsigned int> runEnds{0};
    std::atomic<unsigned int> deferrals{0};
    std::atomic<unsigned int> completions{0};
    std::atomic<unsigned int> wrappersDestroyed{0};
    std::atomic<unsigned int> payloadsDestroyed{0};
    std::atomic<unsigned int> closeGateRuns{0};
    std::atomic<unsigned int> closeGateEnds{0};
    std::atomic<DWORD> wrapperThread{0};
    std::atomic<DWORD> payloadThread{0};
    std::atomic<std::uint64_t> payloadBytes{0};
    std::atomic<std::uint64_t> reservationAtWrapper{0};
    std::atomic<std::uint64_t> reservationAtPayload{0};
    std::atomic<std::uint64_t> reservationAtOwnerCleanup{0};
    std::atomic<bool> wrapperSawWake{false};
    std::atomic<bool> wrapperSawCloseGateJoined{false};
    std::atomic<bool> payloadSawWake{false};
    std::atomic<bool> payloadMta{false};
    std::atomic<bool> payloadIntact{false};
    std::atomic<bool> cancelledAtRunEnd{false};
    std::atomic<bool> finalWorkerEntered{false};
    std::atomic<bool> payloadInFinalWorker{false};
};

#pragma warning(push)
#pragma warning(disable : 4324)
struct CpuPayload final : DeferredReleaseNode
{
    Probe& probe;
    std::vector<std::uint8_t> bytesOwned;

    explicit CpuPayload(Probe& value)
        : probe(value), bytesOwned(static_cast<std::size_t>(kPayloadBytes), kPayloadPattern)
    {
        bytes = kPayloadBytes;
        destroy = [](DeferredReleaseNode* node) noexcept
        { const std::unique_ptr<CpuPayload> owned(static_cast<CpuPayload*>(node)); };
    }

    ~CpuPayload()
    {
        probe.payloadBytes.store(bytesOwned.size(), std::memory_order_relaxed);
        probe.payloadIntact.store(bytes == kPayloadBytes && bytesOwned.size() == kPayloadBytes &&
                                      bytesOwned.front() == kPayloadPattern && bytesOwned.back() == kPayloadPattern,
                                  std::memory_order_relaxed);
        const HANDLE wake = static_cast<HANDLE>(probe.scheduler->WakeHandle());
        probe.payloadSawWake.store(wake != nullptr && WaitForSingleObject(wake, 0) != WAIT_FAILED,
                                   std::memory_order_relaxed);
        // ReservedMemoryBytes is owner-only. Snapshot it at joined owner cleanup, then observe the final
        // retirement boundary here; Close must acknowledge this destruction before releasing that reservation.
        probe.reservationAtPayload.store(probe.reservationAtOwnerCleanup.load(), std::memory_order_relaxed);
        APTTYPE apartment{};
        APTTYPEQUALIFIER qualifier{};
        probe.payloadMta.store(SUCCEEDED(CoGetApartmentType(&apartment, &qualifier)) && apartment == APTTYPE_MTA,
                               std::memory_order_relaxed);
        probe.payloadThread.store(GetCurrentThreadId(), std::memory_order_relaxed);
        probe.payloadInFinalWorker.store(probe.finalWorkerEntered.load(), std::memory_order_relaxed);
        probe.payloadsDestroyed.fetch_add(1, std::memory_order_relaxed);
    }
};
#pragma warning(pop)

struct RetirePayload final
{
    void operator()(CpuPayload* payload) const noexcept
    {
        if (payload && !TryDeferRelease(*payload))
        {
            const std::unique_ptr<CpuPayload> owned(payload);
        }
    }
};

struct SuspendedState final
{
    Probe& probe;
    HANDLE entered = nullptr;
    HANDLE release = nullptr;
    HANDLE deferred = nullptr;
};

class SuspendedPayloadJob final : public Job
{
  public:
    explicit SuspendedPayloadJob(SuspendedState& state)
        : _state(state), _payload(std::make_unique<CpuPayload>(state.probe).release())
    {
    }

    ~SuspendedPayloadJob() override
    {
        Probe& probe = _state.probe;
        probe.wrapperThread.store(GetCurrentThreadId(), std::memory_order_relaxed);
        const HANDLE wake = static_cast<HANDLE>(probe.scheduler->WakeHandle());
        probe.wrapperSawWake.store(wake != nullptr && WaitForSingleObject(wake, 0) != WAIT_FAILED,
                                   std::memory_order_relaxed);
        probe.reservationAtWrapper.store(probe.scheduler->ReservedMemoryBytes(), std::memory_order_relaxed);
        probe.wrapperSawCloseGateJoined.store(probe.closeGateRuns.load(std::memory_order_relaxed) == 1 &&
                                                  probe.closeGateEnds.load(std::memory_order_relaxed) == 1,
                                              std::memory_order_relaxed);
        probe.wrappersDestroyed.fetch_add(1, std::memory_order_relaxed);
    }

    JobResult Run(JobContext&) override
    {
        _state.probe.runs.fetch_add(1, std::memory_order_relaxed);
        (void)SetEvent(_state.entered);
        Wait(_state.release, "suspended-worker-release");
        _state.probe.runEnds.fetch_add(1, std::memory_order_relaxed);
        return {};
    }

    bool DeferCompletion(JobResult result, JobTicket) noexcept override
    {
        if (result.status != JobStatus::Finished)
        {
            return false;
        }
        _state.probe.deferrals.fetch_add(1, std::memory_order_relaxed);
        (void)SetEvent(_state.deferred);
        return true;
    }

    void Completed(JobResult) noexcept override
    {
        _state.probe.completions.fetch_add(1, std::memory_order_relaxed);
    }

  private:
    SuspendedState& _state;
    std::unique_ptr<CpuPayload, RetirePayload> _payload;
};

class CloseGateJob final : public Job
{
  public:
    CloseGateJob(Probe& probe, HANDLE entered) noexcept : _probe(probe), _entered(entered) {}

    JobResult Run(JobContext& context) override
    {
        _probe.closeGateRuns.fetch_add(1, std::memory_order_relaxed);
        (void)SetEvent(_entered);
        Wait(static_cast<HANDLE>(context.shutdownEvent), "suspended-close-joins-running-gate");
        _probe.closeGateEnds.fetch_add(1, std::memory_order_relaxed);
        return {JobStatus::Cancelled};
    }

    void Completed(JobResult) noexcept override
    {
        _probe.completions.fetch_add(1, std::memory_order_relaxed);
    }

  private:
    Probe& _probe;
    HANDLE _entered;
};

void WaitForSuspension(JobScheduler& scheduler, HANDLE deferred) noexcept
{
    const ULONGLONG deadline = GetTickCount64() + 10000;
    for (;;)
    {
        (void)DrainCompletions(scheduler);
        if (WaitForSingleObject(deferred, 0) == WAIT_OBJECT_0)
        {
            Expect(!scheduler.HasUiWork() && !scheduler.IsIdle(), "suspended-record-retained-after-defer-hook");
            return;
        }

        const ULONGLONG now = GetTickCount64();
        if (now >= deadline)
        {
            std::printf("FAIL job-suspended-retirement.suspension-timeout\n");
            std::fflush(stdout);
            std::_Exit(1);
        }
        const HANDLE wake = static_cast<HANDLE>(scheduler.WakeHandle());
        const DWORD waitMs = static_cast<DWORD>(deadline - now);
        const DWORD result = wake ? WaitForSingleObject(wake, waitMs) : WAIT_FAILED;
        if (result != WAIT_OBJECT_0)
        {
            std::printf("FAIL job-suspended-retirement.suspension-wake-timeout wait=0x%08lx\n", result);
            std::fflush(stdout);
            std::_Exit(1);
        }
    }
}

void TestSuspendedPayloadRetiresDuringClose()
{
    Expect(!DeferredReleaseDetail::sink.enqueue, "suspended.no-surrogate-outer-sink");
    JobScheduler scheduler;
    const bool initialized =
        scheduler.Initialize(1, {}, {&SampleMemory, nullptr}) == S_OK && scheduler.EnableDeferredRelease();
    Expect(initialized, "suspended.initialize-and-own-tls-sink");
    if (!initialized)
        return;
    Probe probe;
    probe.scheduler = &scheduler;
    auto entered = Event("suspended-entered");
    auto release = Event("suspended-release");
    auto deferred = Event("suspended-deferred");
    SuspendedState state{probe, entered.get(), release.get(), deferred.get()};
    std::unique_ptr<Job> job = std::make_unique<SuspendedPayloadJob>(state);
    JobTicket ticket;
    Expect(scheduler.Submit(job, {JobClass::Commit, 0, kReservationBytes}, ticket) == S_OK && !job,
           "suspended-job-submitted-with-payload-and-reservation");
    Wait(entered.get(), "suspended-worker-entered");
    Expect(scheduler.ReservedMemoryBytes() == kReservationBytes, "suspended-reservation-acquired");
    Expect(SetEvent(release.get()) != FALSE, "suspended-worker-gate-released");
    WaitForSuspension(scheduler, deferred.get());
    Expect(probe.runs.load(std::memory_order_relaxed) == 1 && probe.runEnds.load(std::memory_order_relaxed) == 1 &&
               probe.deferrals.load(std::memory_order_relaxed) == 1,
           "suspended-successful-stage-deferred-once-before-close");
    Expect(scheduler.ReservedMemoryBytes() == kReservationBytes, "suspended-reservation-retained-until-close");

    auto closeGateEntered = Event("suspended-close-gate-entered");
    std::unique_ptr<Job> closeGate = std::make_unique<CloseGateJob>(probe, closeGateEntered.get());
    JobTicket closeGateTicket;
    Expect(scheduler.Submit(closeGate, {JobClass::Background}, closeGateTicket) == S_OK && !closeGate,
           "suspended-close-join-gate-submitted");
    Wait(closeGateEntered.get(), "suspended-close-gate-entered");

    scheduler.Close({[](void* opaque) noexcept
                     {
                         auto& state = *static_cast<Probe*>(opaque);
                         state.reservationAtOwnerCleanup.store(state.scheduler->ReservedMemoryBytes());
                     },
                     [](void* opaque) noexcept { static_cast<Probe*>(opaque)->finalWorkerEntered.store(true); },
                     &probe});

    Expect(probe.wrappersDestroyed.load(std::memory_order_relaxed) == 1 &&
               probe.wrapperThread.load(std::memory_order_relaxed) == probe.ownerThread &&
               probe.wrapperSawWake.load(std::memory_order_relaxed) &&
               probe.reservationAtWrapper.load(std::memory_order_relaxed) == kReservationBytes &&
               probe.wrapperSawCloseGateJoined.load(std::memory_order_relaxed),
           "suspended-close-joins-worker-before-owner-wrapper-destruction");
    Expect(probe.completions.load(std::memory_order_relaxed) == 0, "suspended-close-suppresses-completion");
    Expect(probe.payloadsDestroyed.load(std::memory_order_relaxed) == 1 &&
               probe.payloadThread.load(std::memory_order_relaxed) != probe.ownerThread &&
               probe.payloadMta.load(std::memory_order_relaxed),
           "suspended-cpu-payload-retired-once-off-owner-on-mta");
    Expect(probe.payloadInFinalWorker.load(), "suspended-payload-destroyed-in-final-retirement-stage");
    Expect(probe.payloadBytes.load(std::memory_order_relaxed) == kPayloadBytes &&
               probe.payloadIntact.load(std::memory_order_relaxed),
           "suspended-payload-exact-bytes-and-content");
    Expect(probe.payloadSawWake.load(std::memory_order_relaxed) &&
               probe.reservationAtPayload.load(std::memory_order_relaxed) == kReservationBytes,
           "suspended-payload-retirement-precedes-wake-and-reservation-teardown");
    Expect(scheduler.ReservedMemoryBytes() == 0 && scheduler.ReleasingMemoryBytes() == 0 &&
               scheduler.WakeHandle() == nullptr && scheduler.WorkerCount() == 0,
           "suspended-close-acknowledges-retirement-before-teardown");
    Expect(!DeferredReleaseDetail::sink.enqueue, "suspended.close-clears-owned-sink");
    scheduler.Close();
    Expect(probe.payloadsDestroyed.load(std::memory_order_relaxed) == 1 &&
               probe.wrappersDestroyed.load(std::memory_order_relaxed) == 1,
           "suspended-repeated-close-does-not-retire-again");
}

class CancelledAtStopJob final : public Job
{
  public:
    CancelledAtStopJob(Probe& probe, HANDLE entered, HANDLE release) noexcept
        : _probe(probe), _entered(entered), _release(release)
    {
    }

    ~CancelledAtStopJob() override
    {
        _probe.wrapperThread.store(GetCurrentThreadId(), std::memory_order_relaxed);
        _probe.wrappersDestroyed.fetch_add(1, std::memory_order_relaxed);
    }

    JobResult Run(JobContext& context) override
    {
        _probe.runs.fetch_add(1, std::memory_order_relaxed);
        (void)SetEvent(_entered);
        Wait(static_cast<HANDLE>(context.shutdownEvent), "cancelled-job-shutdown");
        // Keep the sole worker occupied after RequestStop so the owner can inspect the pending release
        // deterministically before permitting the joined callback to return.
        Wait(_release, "cancelled-job-return-acknowledgement");
        _probe.cancelledAtRunEnd.store(context.cancellation.IsCancelled(), std::memory_order_relaxed);
        _probe.runEnds.fetch_add(1, std::memory_order_relaxed);
        return {JobStatus::Cancelled};
    }

    void Completed(JobResult) noexcept override
    {
        _probe.completions.fetch_add(1, std::memory_order_relaxed);
    }

  private:
    Probe& _probe;
    HANDLE _entered;
    HANDLE _release;
};

void TestPrequeuedReleaseDrainsAfterStopAndCancellation()
{
    Expect(!DeferredReleaseDetail::sink.enqueue, "prequeued.no-surrogate-outer-sink");
    JobScheduler scheduler;
    const bool initialized =
        scheduler.Initialize(1, {}, {&SampleMemory, nullptr}) == S_OK && scheduler.EnableDeferredRelease();
    Expect(initialized, "prequeued.initialize-and-own-tls-sink");
    if (!initialized)
        return;
    Probe probe;
    probe.scheduler = &scheduler;
    auto entered = Event("cancelled-entered");
    auto release = Event("cancelled-return");
    std::unique_ptr<Job> job = std::make_unique<CancelledAtStopJob>(probe, entered.get(), release.get());
    JobTicket ticket;
    Expect(scheduler.Submit(job, {JobClass::InteractivePreview, 0, kReservationBytes}, ticket) == S_OK && !job,
           "prequeued-cancellable-job-submitted");
    Wait(entered.get(), "cancelled-worker-entered");

    std::unique_ptr<CpuPayload, RetirePayload> payload{std::make_unique<CpuPayload>(probe).release()};
    payload.reset();
    Expect(scheduler.ReleasingMemoryBytes() == kPayloadBytes, "prequeued-intrusive-release-pending-before-stop");
    Expect(scheduler.Cancel(ticket), "prequeued-running-job-cancel-accepted");
    Expect(scheduler.ReleasingMemoryBytes() == kPayloadBytes, "prequeued-release-still-pending-at-cancellation");
    scheduler.RequestStop();
    Expect(scheduler.ReleasingMemoryBytes() == kPayloadBytes, "prequeued-release-still-pending-after-request-stop");

    Expect(SetEvent(release.get()) != FALSE, "prequeued-joined-worker-return-permitted-after-stop-observation");
    scheduler.Close({[](void* opaque) noexcept
                     {
                         auto& state = *static_cast<Probe*>(opaque);
                         state.reservationAtOwnerCleanup.store(state.scheduler->ReservedMemoryBytes());
                     },
                     [](void* opaque) noexcept { static_cast<Probe*>(opaque)->finalWorkerEntered.store(true); },
                     &probe});

    Expect(probe.runs.load(std::memory_order_relaxed) == 1 && probe.runEnds.load(std::memory_order_relaxed) == 1 &&
               probe.cancelledAtRunEnd.load(std::memory_order_relaxed) &&
               probe.completions.load(std::memory_order_relaxed) == 0,
           "prequeued-cancelled-work-joins-without-completion");
    Expect(probe.wrappersDestroyed.load(std::memory_order_relaxed) == 1 &&
               probe.wrapperThread.load(std::memory_order_relaxed) == probe.ownerThread,
           "prequeued-cancelled-wrapper-destroyed-on-owner");
    Expect(probe.payloadsDestroyed.load(std::memory_order_relaxed) == 1 &&
               probe.payloadThread.load(std::memory_order_relaxed) != probe.ownerThread &&
               probe.payloadMta.load(std::memory_order_relaxed),
           "prequeued-release-drained-once-off-owner-on-mta");
    Expect(probe.payloadInFinalWorker.load(), "prequeued-payload-destroyed-in-final-retirement-stage");
    Expect(probe.payloadBytes.load(std::memory_order_relaxed) == kPayloadBytes &&
               probe.payloadIntact.load(std::memory_order_relaxed),
           "prequeued-payload-exact-bytes-and-content");
    Expect(probe.payloadSawWake.load(std::memory_order_relaxed) &&
               probe.reservationAtPayload.load(std::memory_order_relaxed) == kReservationBytes,
           "prequeued-final-drain-precedes-wake-and-reservation-teardown");
    Expect(scheduler.ReservedMemoryBytes() == 0 && scheduler.ReleasingMemoryBytes() == 0 &&
               scheduler.WakeHandle() == nullptr && scheduler.WorkerCount() == 0,
           "prequeued-close-acknowledges-release-before-teardown");
    Expect(!DeferredReleaseDetail::sink.enqueue, "prequeued.close-clears-owned-sink");
}
} // namespace

int RunJobSuspendedRetirementTests()
{
    gFailures = 0;
    TestSuspendedPayloadRetiresDuringClose();
    TestPrequeuedReleaseDrainsAfterStopAndCancellation();
    std::printf("%s job-suspended-retirement.summary failures=%d\n", gFailures == 0 ? "PASS" : "FAIL", gFailures);
    return gFailures;
}
