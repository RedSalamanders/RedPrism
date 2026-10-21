#include "Host.Tests.JobExecutionObservation.h"

#include "Jobs.h"

#include <array>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <new>
#include <thread>

#include <windows.h>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/resource.h>
#pragma warning(pop)

namespace
{
constexpr DWORD kWaitMs = 10000;
constexpr std::size_t kNoticeCapacity = 128;
unsigned int gFailures = 0;
unsigned int gChecks = 0;

void Expect(bool passed, const char* name) noexcept
{
    ++gChecks;
    std::printf("%s job-execution-observation.%s\n", passed ? "PASS" : "FAIL", name);
    if (!passed)
    {
        ++gFailures;
    }
    std::fflush(stdout);
}

wil::unique_handle Event(bool signaled = false)
{
    wil::unique_handle event{CreateEventW(nullptr, TRUE, signaled ? TRUE : FALSE, nullptr)};
    if (!event)
    {
        std::printf("FAIL job-execution-observation.create-event\n");
        std::fflush(stdout);
        std::_Exit(1);
    }
    return event;
}

// Each stored notice gets one semaphore release; overflow uses the separate bounded failure event.
wil::unique_handle NoticeSemaphore()
{
    wil::unique_handle semaphore{CreateSemaphoreW(nullptr, 0, static_cast<LONG>(kNoticeCapacity), nullptr)};
    if (!semaphore)
    {
        std::printf("FAIL job-execution-observation.create-semaphore\n");
        std::fflush(stdout);
        std::_Exit(1);
    }
    return semaphore;
}

bool WaitOne(HANDLE event) noexcept
{
    return WaitForSingleObject(event, kWaitMs) == WAIT_OBJECT_0;
}

struct Entry final
{
    std::atomic<bool> published{false};
    JobExecutionBoundary boundary = JobExecutionBoundary::BodySkipped;
    JobTicket ticket{};
    JobClass kind = JobClass::Background;
    JobExecutionLane lane = JobExecutionLane::Compute;
    bool hasResult = false;
    JobStatus status = JobStatus::Failed;
    std::int32_t error = 0;
    unsigned int order = 0;
    DWORD callbackThreadId = 0;
    bool bodyLocalsDestroyed = false;
};

// Fixed storage and atomics only: observer callback never allocates, locks, waits, or calls the scheduler.
struct ObservationLog final
{
    ObservationLog(HANDLE wakeEvent, HANDLE overflowEvent) noexcept : wake(wakeEvent), overflowWake(overflowEvent) {}

    std::array<Entry, kNoticeCapacity> entries{};
    std::atomic<unsigned int> count{0};
    std::atomic<unsigned int> overflow{0};
    std::atomic<unsigned int> order{0};
    std::atomic<unsigned int> completedCount{0};
    std::atomic<unsigned int> completedStatus{static_cast<unsigned int>(JobStatus::Failed)};
    std::atomic<std::int32_t> completedError{0};
    std::atomic<unsigned int> completedOrder{0};
    const std::atomic<bool>* bodyLocalsDestroyed = nullptr; // optional immutable fixture witness
    HANDLE wake = nullptr;                                  // borrowed; owner outlives scheduler Close
    HANDLE overflowWake = nullptr; // borrowed bounded failure event; owner outlives scheduler Close
};

void ObserveExecution(void* context, JobExecutionNotice notice) noexcept
{
    auto& log = *static_cast<ObservationLog*>(context);
    const unsigned int index = log.count.fetch_add(1, std::memory_order_relaxed);
    if (index >= log.entries.size())
    {
        log.overflow.store(1, std::memory_order_release);
        (void)SetEvent(log.overflowWake);
        return;
    }

    Entry& entry = log.entries[index];
    entry.boundary = notice.boundary;
    entry.ticket = notice.ticket;
    entry.kind = notice.kind;
    entry.lane = notice.lane;
    entry.hasResult = notice.result.has_value();
    if (notice.result)
    {
        entry.status = notice.result->status;
        entry.error = notice.result->error;
    }
    entry.order = log.order.fetch_add(1, std::memory_order_relaxed) + 1;
    entry.callbackThreadId = GetCurrentThreadId();
    entry.bodyLocalsDestroyed = log.bodyLocalsDestroyed && log.bodyLocalsDestroyed->load(std::memory_order_acquire);
    entry.published.store(true, std::memory_order_release);
    (void)ReleaseSemaphore(log.wake, 1, nullptr);
}

void RecordCompletion(ObservationLog& log, JobResult result) noexcept
{
    log.completedStatus.store(static_cast<unsigned int>(result.status), std::memory_order_relaxed);
    log.completedError.store(result.error, std::memory_order_relaxed);
    log.completedOrder.store(log.order.fetch_add(1, std::memory_order_relaxed) + 1, std::memory_order_relaxed);
    log.completedCount.fetch_add(1, std::memory_order_release);
}

bool WaitNotices(HANDLE semaphore, unsigned int amount) noexcept
{
    for (unsigned int index = 0; index < amount; ++index)
    {
        if (!WaitOne(semaphore))
        {
            return false;
        }
    }
    return true;
}

bool IsNotice(const ObservationLog& log, std::size_t index, JobExecutionBoundary boundary, JobTicket ticket,
              JobClass kind, JobExecutionLane lane, bool hasResult, JobStatus status = JobStatus::Finished,
              std::int32_t error = 0) noexcept
{
    if (index >= log.entries.size())
    {
        return false;
    }
    const Entry& entry = log.entries[index];
    return entry.published.load(std::memory_order_acquire) && entry.boundary == boundary && entry.ticket == ticket &&
           entry.kind == kind && entry.lane == lane && entry.hasResult == hasResult &&
           (!hasResult || (entry.status == status && entry.error == error));
}

bool Settled(JobScheduler& scheduler) noexcept
{
    return SettleJobs(scheduler, kWaitMs) == SettleResult::Settled;
}

class ReturnJob : public Job
{
  public:
    ReturnJob(ObservationLog& log, JobResult value) noexcept : _log(log), _value(value) {}
    JobResult Run(JobContext&) override
    {
        return _value;
    }
    void Completed(JobResult result) noexcept override
    {
        RecordCompletion(_log, result);
    }

  private:
    ObservationLog& _log;
    JobResult _value;
};

class ThrowBadAllocJob final : public Job
{
  public:
    ThrowBadAllocJob(ObservationLog& log, std::atomic<bool>& unwindWitness) noexcept
        : _log(log), _unwindWitness(unwindWitness)
    {
    }
    JobResult Run(JobContext&) override
    {
        const auto witness = wil::scope_exit([&]() noexcept { _unwindWitness.store(true, std::memory_order_release); });
        throw std::bad_alloc{};
    }
    void Completed(JobResult result) noexcept override
    {
        RecordCompletion(_log, result);
    }

  private:
    ObservationLog& _log;
    std::atomic<bool>& _unwindWitness;
};

struct Gate final
{
    Gate() : entered(Event()), release(Event()) {}
    wil::unique_handle entered;
    wil::unique_handle release;
    std::atomic<DWORD> runThreadId{0};
    void Open() noexcept
    {
        (void)SetEvent(release.get());
    }
};

class GatedJob final : public Job
{
  public:
    GatedJob(ObservationLog& log, Gate& gate, JobResult value = {}, std::atomic<bool>* unwindWitness = nullptr) noexcept
        : _log(log), _gate(gate), _value(value), _unwindWitness(unwindWitness)
    {
    }
    JobResult Run(JobContext&) override
    {
        const auto witness = wil::scope_exit(
            [&]() noexcept
            {
                if (_unwindWitness)
                    _unwindWitness->store(true, std::memory_order_release);
            });
        _gate.runThreadId.store(GetCurrentThreadId(), std::memory_order_release);
        (void)SetEvent(_gate.entered.get());
        (void)WaitForSingleObject(_gate.release.get(), kWaitMs);
        return _value;
    }
    void Completed(JobResult result) noexcept override
    {
        RecordCompletion(_log, result);
    }

  private:
    ObservationLog& _log;
    Gate& _gate;
    JobResult _value;
    std::atomic<bool>* _unwindWitness;
};

void TestNormalBoundaryOrderAndIdentity()
{
    auto notices = NoticeSemaphore();
    auto overflow = Event();
    const auto ownedLog = std::make_unique<ObservationLog>(notices.get(), overflow.get());
    auto& log = *ownedLog;
    std::atomic<bool> unwindWitness{false};
    log.bodyLocalsDestroyed = &unwindWitness;
    JobScheduler scheduler;
    const HRESULT initialized =
        static_cast<HRESULT>(scheduler.Initialize(1, {}, {}, JobExecutionObserver{ObserveExecution, &log}));
    Expect(SUCCEEDED(initialized), "normal.initialize-observer");
    if (FAILED(initialized))
    {
        scheduler.Close();
        return;
    }

    Gate gate;
    auto cleanup = wil::scope_exit(
        [&]() noexcept
        {
            gate.Open();
            scheduler.Close();
        });
    std::unique_ptr<Job> job = std::make_unique<GatedJob>(log, gate, JobResult{}, &unwindWitness);
    JobTicket ticket{};
    JobOptions options{};
    options.kind = JobClass::Selection;
    const HRESULT submitted = static_cast<HRESULT>(scheduler.Submit(job, options, ticket));
    const bool entered = SUCCEEDED(submitted) && WaitOne(gate.entered.get()) && WaitNotices(notices.get(), 1);
    Expect(entered && !job && ticket.generation != 0, "normal.body-enters-with-admitted-ticket");
    const bool entryShape = entered && IsNotice(log, 0, JobExecutionBoundary::BodyEntered, ticket, JobClass::Selection,
                                                JobExecutionLane::Compute, false);
    Expect(entryShape, "normal.entry-has-exact-ticket-class-lane-and-no-result");
    gate.Open();
    const bool exited = entered && WaitNotices(notices.get(), 1);
    const bool settled = Settled(scheduler);
    Expect(exited && settled, "normal.body-exit-and-owner-completion-arrive-within-bounds");
    const bool exitShape = exited && IsNotice(log, 1, JobExecutionBoundary::BodyExited, ticket, JobClass::Selection,
                                              JobExecutionLane::Compute, true);
    const unsigned int exitOrder = exitShape ? log.entries[1].order : 0;
    Expect(exitShape && log.completedCount.load(std::memory_order_acquire) == 1 &&
               log.completedStatus.load() == static_cast<unsigned int>(JobStatus::Finished) &&
               log.entries[0].order < exitOrder && exitOrder < log.completedOrder.load(),
           "normal-enter-run-exit-precedes-owner-completion");
    Expect(exitShape && log.entries[0].callbackThreadId == gate.runThreadId.load(std::memory_order_acquire) &&
               log.entries[1].callbackThreadId == gate.runThreadId.load(std::memory_order_acquire),
           "normal-entry-and-exit-observers-run-on-the-job-worker");
    Expect(log.count.load() == 2 && log.overflow.load() == 0, "normal-exactly-one-body-pair");
    Expect(entryShape && exitShape && !log.entries[0].bodyLocalsDestroyed && log.entries[1].bodyLocalsDestroyed,
           "normal-run-local-destruction-precedes-exit-observation");
    scheduler.Close();
}

void TestConcurrentComputeAndRecordingLanes()
{
    auto notices = NoticeSemaphore();
    auto overflow = Event();
    const auto ownedLog = std::make_unique<ObservationLog>(notices.get(), overflow.get());
    auto& log = *ownedLog;
    JobScheduler scheduler;
    const auto initialized = static_cast<HRESULT>(scheduler.Initialize(1, {}, {}, {ObserveExecution, &log}));
    const bool ready = SUCCEEDED(initialized) && scheduler.EnableGpuRecordingLane() >= 0;
    Expect(ready, "lanes.initialize-explicit-compute-and-recording-workers");
    if (!ready)
    {
        scheduler.Close();
        return;
    }
    Gate computeGate;
    Gate recordingGate;
    const auto cleanup = wil::scope_exit(
        [&]() noexcept
        {
            computeGate.Open();
            recordingGate.Open();
            scheduler.Close();
        });
    std::unique_ptr<Job> compute = std::make_unique<GatedJob>(log, computeGate);
    std::unique_ptr<Job> recording = std::make_unique<GatedJob>(log, recordingGate);
    JobTicket computeTicket{};
    JobTicket recordingTicket{};
    JobOptions computeOptions{};
    computeOptions.kind = JobClass::Selection;
    JobOptions recordingOptions{};
    recordingOptions.kind = JobClass::InteractivePreview;
    recordingOptions.executionLane = JobExecutionLane::GpuRecording;
    const bool admitted = scheduler.Submit(compute, computeOptions, computeTicket) >= 0 &&
                          scheduler.Submit(recording, recordingOptions, recordingTicket) >= 0;
    const bool entered = admitted && WaitOne(computeGate.entered.get()) && WaitOne(recordingGate.entered.get()) &&
                         WaitNotices(notices.get(), 2);
    Expect(entered && computeGate.runThreadId.load() != recordingGate.runThreadId.load(),
           "lanes-both-original-bodies-overlap-on-distinct-workers");
    const auto pairMatches = [&](std::size_t first, JobExecutionBoundary boundary, bool hasResult) noexcept
    {
        return (IsNotice(log, first, boundary, computeTicket, JobClass::Selection, JobExecutionLane::Compute,
                         hasResult) &&
                IsNotice(log, first + 1, boundary, recordingTicket, JobClass::InteractivePreview,
                         JobExecutionLane::GpuRecording, hasResult)) ||
               (IsNotice(log, first + 1, boundary, computeTicket, JobClass::Selection, JobExecutionLane::Compute,
                         hasResult) &&
                IsNotice(log, first, boundary, recordingTicket, JobClass::InteractivePreview,
                         JobExecutionLane::GpuRecording, hasResult));
    };
    Expect(entered && pairMatches(0, JobExecutionBoundary::BodyEntered, false),
           "lanes-original-ticket-class-and-lane-survive-concurrent-entry");
    computeGate.Open();
    recordingGate.Open();
    const bool settled = Settled(scheduler) && WaitNotices(notices.get(), 2);
    Expect(settled && pairMatches(2, JobExecutionBoundary::BodyExited, true) && log.count.load() == 4 &&
               log.completedCount.load() == 2 && log.overflow.load() == 0,
           "lanes-fixed-publication-keeps-both-exits-before-original-completions");
    scheduler.Close();
}

void TestRawFailureAndBadAlloc()
{
    auto notices = NoticeSemaphore();
    auto overflow = Event();
    const auto ownedLog = std::make_unique<ObservationLog>(notices.get(), overflow.get());
    auto& log = *ownedLog;
    std::atomic<bool> unwindWitness{false};
    log.bodyLocalsDestroyed = &unwindWitness;
    JobScheduler scheduler;
    const HRESULT initialized =
        static_cast<HRESULT>(scheduler.Initialize(1, {}, {}, JobExecutionObserver{ObserveExecution, &log}));
    Expect(SUCCEEDED(initialized), "failure.initialize-observer");
    if (FAILED(initialized))
    {
        scheduler.Close();
        return;
    }
    auto cleanup = wil::scope_exit([&]() noexcept { scheduler.Close(); });

    const JobResult raw{JobStatus::Failed, E_FAIL};
    std::unique_ptr<Job> failed = std::make_unique<ReturnJob>(log, raw);
    JobTicket failedTicket{};
    JobOptions options{};
    options.kind = JobClass::Io;
    const HRESULT failedSubmit = static_cast<HRESULT>(scheduler.Submit(failed, options, failedTicket));
    const bool failedPair = SUCCEEDED(failedSubmit) && WaitNotices(notices.get(), 2) && Settled(scheduler);
    Expect(failedPair &&
               IsNotice(log, 0, JobExecutionBoundary::BodyEntered, failedTicket, JobClass::Io,
                        JobExecutionLane::Compute, false) &&
               IsNotice(log, 1, JobExecutionBoundary::BodyExited, failedTicket, JobClass::Io, JobExecutionLane::Compute,
                        true, JobStatus::Failed, E_FAIL),
           "failure.raw-return-is-reported-at-exit");
    Expect(log.completedStatus.load() == static_cast<unsigned int>(JobStatus::Failed) &&
               log.completedError.load() == E_FAIL,
           "failure.owner-receives-raw-failed-result");

    std::unique_ptr<Job> allocationFailure = std::make_unique<ThrowBadAllocJob>(log, unwindWitness);
    JobTicket allocationTicket{};
    const HRESULT allocationSubmit =
        static_cast<HRESULT>(scheduler.Submit(allocationFailure, options, allocationTicket));
    const bool allocationPair = SUCCEEDED(allocationSubmit) && WaitNotices(notices.get(), 2) && Settled(scheduler);
    Expect(allocationPair &&
               IsNotice(log, 2, JobExecutionBoundary::BodyEntered, allocationTicket, JobClass::Io,
                        JobExecutionLane::Compute, false) &&
               IsNotice(log, 3, JobExecutionBoundary::BodyExited, allocationTicket, JobClass::Io,
                        JobExecutionLane::Compute, true, JobStatus::Failed, E_OUTOFMEMORY),
           "failure-bad-alloc-unwind-is-reported-before-final-publication");
    Expect(log.completedCount.load(std::memory_order_acquire) == 2 &&
               log.completedStatus.load() == static_cast<unsigned int>(JobStatus::Failed) &&
               log.completedError.load() == E_OUTOFMEMORY && log.count.load() == 4,
           "failure-bad-alloc-normalized-result-reaches-owner-once");
    Expect(allocationPair && !log.entries[2].bodyLocalsDestroyed && log.entries[3].bodyLocalsDestroyed,
           "failure-bad-alloc-local-unwind-precedes-translated-exit-observation");
    scheduler.Close();
}

class CountJob final : public Job
{
  public:
    CountJob(ObservationLog& log, std::atomic<unsigned int>& runs) noexcept : _log(log), _runs(runs) {}
    JobResult Run(JobContext&) override
    {
        _runs.fetch_add(1, std::memory_order_relaxed);
        return {};
    }
    void Completed(JobResult result) noexcept override
    {
        RecordCompletion(_log, result);
    }

  private:
    ObservationLog& _log;
    std::atomic<unsigned int>& _runs;
};

void TestQueuedAndRunningCancellation()
{
    auto notices = NoticeSemaphore();
    auto overflow = Event();
    const auto ownedLog = std::make_unique<ObservationLog>(notices.get(), overflow.get());
    auto& log = *ownedLog;
    JobScheduler scheduler;
    const HRESULT initialized =
        static_cast<HRESULT>(scheduler.Initialize(1, {}, {}, JobExecutionObserver{ObserveExecution, &log}));
    Expect(SUCCEEDED(initialized), "cancel.initialize-observer");
    if (FAILED(initialized))
    {
        scheduler.Close();
        return;
    }
    Gate blocker;
    Gate runningGate;
    auto cleanup = wil::scope_exit(
        [&]() noexcept
        {
            blocker.Open();
            runningGate.Open();
            scheduler.Close();
        });

    std::unique_ptr<Job> blocking = std::make_unique<GatedJob>(log, blocker);
    JobTicket blockerTicket{};
    const HRESULT blockerSubmit = static_cast<HRESULT>(scheduler.Submit(blocking, {}, blockerTicket));
    const bool blockerEntered =
        SUCCEEDED(blockerSubmit) && WaitOne(blocker.entered.get()) && WaitNotices(notices.get(), 1);
    std::atomic<unsigned int> queuedRuns{0};
    std::unique_ptr<Job> queued = std::make_unique<CountJob>(log, queuedRuns);
    JobTicket queuedTicket{};
    const HRESULT queuedSubmit = static_cast<HRESULT>(scheduler.Submit(queued, {}, queuedTicket));
    const bool queuedCancelled = SUCCEEDED(queuedSubmit) && scheduler.Cancel(queuedTicket);
    blocker.Open();
    const bool queuedSettled = blockerEntered && queuedCancelled && Settled(scheduler);
    // Settlement publishes both the blocker's exit and the queued skip. Consume exactly those two remaining
    // semaphore notices before starting the next stage, so no prior token can satisfy a later wait.
    const bool priorNoticesDrained = queuedSettled && WaitNotices(notices.get(), 2);
    const bool skipped =
        priorNoticesDrained && IsNotice(log, 2, JobExecutionBoundary::BodySkipped, queuedTicket, JobClass::Background,
                                        JobExecutionLane::Compute, true, JobStatus::Cancelled);
    Expect(skipped && queuedRuns.load() == 0 &&
               log.completedStatus.load() == static_cast<unsigned int>(JobStatus::Cancelled),
           "cancel-queued-stage-skips-run-and-completes-cancelled");

    std::unique_ptr<Job> running = std::make_unique<GatedJob>(log, runningGate);
    JobTicket runningTicket{};
    JobOptions runningOptions{};
    runningOptions.kind = JobClass::Commit;
    const HRESULT runningSubmit =
        priorNoticesDrained ? static_cast<HRESULT>(scheduler.Submit(running, runningOptions, runningTicket)) : E_FAIL;
    const bool runningEntered =
        SUCCEEDED(runningSubmit) && WaitOne(runningGate.entered.get()) && WaitNotices(notices.get(), 1);
    const bool cancellationRequested = runningEntered && scheduler.Cancel(runningTicket);
    runningGate.Open();
    const bool runningSettled = cancellationRequested && WaitNotices(notices.get(), 1) && Settled(scheduler);
    Expect(runningSettled &&
               IsNotice(log, 4, JobExecutionBoundary::BodyExited, runningTicket, JobClass::Commit,
                        JobExecutionLane::Compute, true, JobStatus::Finished) &&
               log.completedCount.load(std::memory_order_acquire) == 3 &&
               log.completedStatus.load() == static_cast<unsigned int>(JobStatus::Cancelled),
           "cancel-running-exit-keeps-raw-finished-while-owner-result-is-cancelled");
    Expect(log.count.load() == 5 && log.overflow.load() == 0, "cancel-notices-match-one-skip-and-two-run-pairs");
    scheduler.Close();
}

struct ResumedJobState final
{
    std::atomic<unsigned int> stage{0};
    std::atomic<unsigned int> deferredStage{0};
    std::atomic<std::size_t> deferredSlot{0};
    std::atomic<std::uint64_t> deferredGeneration{0};
};

class ResumedJob final : public Job
{
  public:
    ResumedJob(ObservationLog& log, ResumedJobState& state) noexcept : _log(log), _state(state) {}
    JobResult Run(JobContext&) override
    {
        _state.stage.fetch_add(1, std::memory_order_relaxed);
        return {};
    }
    bool DeferCompletion(JobResult, JobTicket ticket) noexcept override
    {
        _state.deferredSlot.store(ticket.slot, std::memory_order_relaxed);
        _state.deferredGeneration.store(ticket.generation, std::memory_order_relaxed);
        const auto stage = _state.stage.load(std::memory_order_relaxed);
        _state.deferredStage.store(stage, std::memory_order_release);
        return stage < 3;
    }
    void Completed(JobResult result) noexcept override
    {
        RecordCompletion(_log, result);
    }

  private:
    ObservationLog& _log;
    ResumedJobState& _state;
};

// Entry/exit observations precede completion publication. A Submit/Resume readiness signal may be stale:
// wait for the exact owner deferral stage, consuming only actual events under one total failure deadline.
bool WaitForOwnerDeferral(JobScheduler& scheduler, const ResumedJobState& state, unsigned int stage) noexcept
{
    const ULONGLONG deadline = GetTickCount64() + kWaitMs;
    for (;;)
    {
        (void)DrainCompletions(scheduler);
        if (state.deferredStage.load(std::memory_order_acquire) == stage)
            return true;
        const ULONGLONG now = GetTickCount64();
        if (now >= deadline ||
            WaitForSingleObject(scheduler.WakeHandle(), static_cast<DWORD>(deadline - now)) != WAIT_OBJECT_0)
            return false;
    }
}

void TestResumedOriginalTicketHasBodyPairs()
{
    auto notices = NoticeSemaphore();
    auto overflow = Event();
    const auto ownedLog = std::make_unique<ObservationLog>(notices.get(), overflow.get());
    auto& log = *ownedLog;
    JobScheduler scheduler;
    const HRESULT initialized =
        static_cast<HRESULT>(scheduler.Initialize(1, {}, {}, JobExecutionObserver{ObserveExecution, &log}));
    Expect(SUCCEEDED(initialized), "resume.initialize-observer");
    if (FAILED(initialized))
    {
        scheduler.Close();
        return;
    }
    ResumedJobState jobState;
    auto cleanup = wil::scope_exit([&]() noexcept { scheduler.Close(); });
    auto job = std::make_unique<ResumedJob>(log, jobState);
    std::unique_ptr<Job> owned = std::move(job);
    JobOptions options{};
    options.kind = JobClass::Refinement;
    options.executionLane = JobExecutionLane::Compute;
    JobTicket ticket{};
    const HRESULT submitted = static_cast<HRESULT>(scheduler.Submit(owned, options, ticket));
    const bool firstPair =
        SUCCEEDED(submitted) && WaitNotices(notices.get(), 2) && WaitForOwnerDeferral(scheduler, jobState, 1);
    const JobTicket firstDeferred{jobState.deferredSlot.load(std::memory_order_relaxed),
                                  jobState.deferredGeneration.load(std::memory_order_relaxed)};
    const bool suspended = firstPair && jobState.stage.load(std::memory_order_relaxed) == 1 &&
                           firstDeferred == ticket && log.completedCount.load(std::memory_order_acquire) == 0;
    Expect(suspended &&
               IsNotice(log, 0, JobExecutionBoundary::BodyEntered, ticket, JobClass::Refinement,
                        JobExecutionLane::Compute, false) &&
               IsNotice(log, 1, JobExecutionBoundary::BodyExited, ticket, JobClass::Refinement,
                        JobExecutionLane::Compute, true),
           "resume-first-body-pair-suspends-before-owner-apply");
    const bool resumedSecond = suspended && scheduler.Resume(ticket);
    // Resume queues an owner promotion. Run the owner drain before waiting for worker body notices;
    // otherwise the fixture waits for a stage that the owner has not made runnable yet.
    if (resumedSecond)
        (void)DrainCompletions(scheduler);
    const bool secondPair =
        resumedSecond && WaitNotices(notices.get(), 2) && WaitForOwnerDeferral(scheduler, jobState, 2);
    const JobTicket secondDeferred{jobState.deferredSlot.load(std::memory_order_relaxed),
                                   jobState.deferredGeneration.load(std::memory_order_relaxed)};
    const bool suspendedAgain = secondPair && jobState.stage.load(std::memory_order_relaxed) == 2 &&
                                secondDeferred == ticket && log.completedCount.load(std::memory_order_acquire) == 0;
    Expect(suspendedAgain &&
               IsNotice(log, 2, JobExecutionBoundary::BodyEntered, ticket, JobClass::Refinement,
                        JobExecutionLane::Compute, false) &&
               IsNotice(log, 3, JobExecutionBoundary::BodyExited, ticket, JobClass::Refinement,
                        JobExecutionLane::Compute, true),
           "resume-same-original-ticket-produces-second-pair-before-owner-apply");
    const bool resumedThird = suspendedAgain && scheduler.Resume(ticket);
    if (resumedThird)
        (void)DrainCompletions(scheduler);
    const bool thirdPair = resumedThird && WaitNotices(notices.get(), 2) && Settled(scheduler);
    Expect(thirdPair &&
               IsNotice(log, 4, JobExecutionBoundary::BodyEntered, ticket, JobClass::Refinement,
                        JobExecutionLane::Compute, false) &&
               IsNotice(log, 5, JobExecutionBoundary::BodyExited, ticket, JobClass::Refinement,
                        JobExecutionLane::Compute, true) &&
               log.completedCount.load(std::memory_order_acquire) == 1 &&
               jobState.stage.load(std::memory_order_relaxed) == 3,
           "resume-third-body-pair-precedes-final-owner-apply");
    Expect(log.count.load() == 6 && log.overflow.load() == 0, "resume-three-pairs-share-original-ticket");
    scheduler.Close();
}

class RetirementGateJob final : public Job
{
  public:
    RetirementGateJob(ObservationLog& log, Gate& retirement) noexcept : _log(log), _retirement(retirement) {}
    JobResult Run(JobContext&) override
    {
        return {};
    }
    void RetireWorkerStage(JobContext&, JobResult) noexcept override
    {
        (void)SetEvent(_retirement.entered.get());
        (void)WaitForSingleObject(_retirement.release.get(), kWaitMs);
    }
    void Completed(JobResult result) noexcept override
    {
        RecordCompletion(_log, result);
    }

  private:
    ObservationLog& _log;
    Gate& _retirement;
};

void TestExitIsVisibleWhileRetirementIsHeld()
{
    auto notices = NoticeSemaphore();
    auto overflow = Event();
    const auto ownedLog = std::make_unique<ObservationLog>(notices.get(), overflow.get());
    auto& log = *ownedLog;
    JobScheduler scheduler;
    const HRESULT initialized =
        static_cast<HRESULT>(scheduler.Initialize(1, {}, {}, JobExecutionObserver{ObserveExecution, &log}));
    Expect(SUCCEEDED(initialized), "retire.initialize-observer");
    if (FAILED(initialized))
    {
        scheduler.Close();
        return;
    }
    Gate retirement;
    auto cleanup = wil::scope_exit(
        [&]() noexcept
        {
            retirement.Open();
            scheduler.Close();
        });
    std::unique_ptr<Job> job = std::make_unique<RetirementGateJob>(log, retirement);
    JobOptions options{};
    options.deferredRetirement = true;
    options.kind = JobClass::Stroke;
    JobTicket ticket{};
    const HRESULT submitted = static_cast<HRESULT>(scheduler.Submit(job, options, ticket));
    const bool boundaries = SUCCEEDED(submitted) && WaitNotices(notices.get(), 2);
    const bool retirementEntered = WaitOne(retirement.entered.get());
    const bool visibleBeforeRelease =
        boundaries && retirementEntered &&
        IsNotice(log, 1, JobExecutionBoundary::BodyExited, ticket, JobClass::Stroke, JobExecutionLane::Compute, true) &&
        log.completedCount.load(std::memory_order_acquire) == 0;
    Expect(visibleBeforeRelease, "retire-body-exit-published-before-held-retirement-and-final-publication");
    retirement.Open();
    const bool settled = Settled(scheduler);
    Expect(settled && log.completedCount.load(std::memory_order_acquire) == 1 && log.count.load() == 2,
           "retire-release-joins-stage-and-allows-owner-completion");
    scheduler.Close();
}

class ShutdownGatedJob final : public Job
{
  public:
    ShutdownGatedJob(ObservationLog& log, Gate& gate, std::atomic<HANDLE>& shutdownHandle) noexcept
        : _log(log), _gate(gate), _shutdownHandle(shutdownHandle)
    {
    }
    JobResult Run(JobContext& context) override
    {
        _shutdownHandle.store(static_cast<HANDLE>(context.shutdownEvent), std::memory_order_release);
        (void)SetEvent(_gate.entered.get());
        (void)WaitForSingleObject(_gate.release.get(), kWaitMs);
        return {};
    }
    void Completed(JobResult result) noexcept override
    {
        RecordCompletion(_log, result);
    }

  private:
    ObservationLog& _log;
    Gate& _gate;
    std::atomic<HANDLE>& _shutdownHandle;
};

void TestDefaultObserverAndJoinedClose()
{
    // Default initialization proves it remains callback-free; the second scheduler proves Close joins a running
    // body and reports BodyExited while dropping owner completion. Observer storage and its borrowed events
    // outlive Close.
    {
        auto notices = NoticeSemaphore();
        auto overflow = Event();
        const auto ownedLog = std::make_unique<ObservationLog>(notices.get(), overflow.get());
        auto& log = *ownedLog;
        JobScheduler scheduler;
        const HRESULT initialized = static_cast<HRESULT>(scheduler.Initialize(1));
        Expect(SUCCEEDED(initialized), "default.initialize-without-observer");
        if (SUCCEEDED(initialized))
        {
            std::atomic<unsigned int> runs{0};
            std::unique_ptr<Job> job = std::make_unique<CountJob>(log, runs);
            JobTicket ticket{};
            const HRESULT submitted = static_cast<HRESULT>(scheduler.Submit(job, {}, ticket));
            const bool settled = SUCCEEDED(submitted) && Settled(scheduler);
            Expect(settled && runs.load() == 1 && log.count.load() == 0 && log.completedCount.load() == 1,
                   "default-no-execution-callbacks-owner-completion-still-runs");
        }
        scheduler.Close();
    }

    auto notices = NoticeSemaphore();
    auto overflow = Event();
    const auto ownedLog = std::make_unique<ObservationLog>(notices.get(), overflow.get());
    auto& log = *ownedLog;
    JobScheduler scheduler;
    const HRESULT initialized =
        static_cast<HRESULT>(scheduler.Initialize(1, {}, {}, JobExecutionObserver{ObserveExecution, &log}));
    Expect(SUCCEEDED(initialized), "close.initialize-observer");
    if (FAILED(initialized))
    {
        scheduler.Close();
        return;
    }
    Gate gate;
    std::atomic<HANDLE> shutdownHandle{nullptr};
    std::atomic<bool> shutdownObservedBeforeRelease{false};
    auto cleanup = wil::scope_exit(
        [&]() noexcept
        {
            gate.Open();
            if (scheduler.WorkerCount() != 0)
            {
                scheduler.Close();
            }
        });
    std::unique_ptr<Job> job = std::make_unique<ShutdownGatedJob>(log, gate, shutdownHandle);
    JobTicket ticket{};
    const HRESULT submitted = static_cast<HRESULT>(scheduler.Submit(job, {}, ticket));
    const bool entered = SUCCEEDED(submitted) && WaitOne(gate.entered.get()) && WaitNotices(notices.get(), 1);
    std::thread releaser;
    if (entered)
    {
        const HANDLE shutdown = shutdownHandle.load(std::memory_order_acquire);
        releaser = std::thread(
            [&gate, &shutdownObservedBeforeRelease, shutdown]() noexcept
            {
                const bool observed = shutdown != nullptr && WaitForSingleObject(shutdown, kWaitMs) == WAIT_OBJECT_0;
                shutdownObservedBeforeRelease.store(observed, std::memory_order_release);
                gate.Open();
            });
    }
    else
    {
        gate.Open();
    }
    // Close is owner-thread-only. The bounded helper releases the worker only after the real scheduler
    // shutdown event is signaled, with no timing delay or polling.
    scheduler.Close();
    if (releaser.joinable())
    {
        releaser.join();
    }
    const bool closeWasRequestedWhileRunning = entered && shutdownObservedBeforeRelease.load(std::memory_order_acquire);
    const bool exited = entered && WaitNotices(notices.get(), 1);
    const bool heldBeforeRelease = exited && IsNotice(log, 1, JobExecutionBoundary::BodyExited, ticket,
                                                      JobClass::Background, JobExecutionLane::Compute, true);
    Expect(closeWasRequestedWhileRunning && heldBeforeRelease, "close-observer-exits-running-body-during-joined-close");
    Expect(log.completedCount.load(std::memory_order_acquire) == 0,
           "close-joined-worker-does-not-invoke-owner-completion");
    Expect(log.completedCount.load(std::memory_order_acquire) == 0 && log.count.load() == 2,
           "close-joins-before-borrowed-observer-context-and-event-expire");
}
} // namespace

int RunJobExecutionObservationTests()
{
    gFailures = 0;
    gChecks = 0;
    TestNormalBoundaryOrderAndIdentity();
    TestConcurrentComputeAndRecordingLanes();
    TestRawFailureAndBadAlloc();
    TestQueuedAndRunningCancellation();
    TestResumedOriginalTicketHasBodyPairs();
    TestExitIsVisibleWhileRetirementIsHeld();
    TestDefaultObserverAndJoinedClose();
    std::printf("%s job-execution-observation.summary controls=%u failed=%u\n",
                gFailures == 0 && gChecks == 36 ? "PASS" : "FAIL", gChecks, gFailures);
    std::fflush(stdout);
    return static_cast<int>(gFailures + (gChecks != 36 ? 1 : 0));
}
