#include "Host.Tests.WorkerStageRetirement.h"

#include "Jobs.h"

#include <atomic>
#include <combaseapi.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <new>
#include <windows.h>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/resource.h>
#pragma warning(pop)

namespace
{
constexpr std::uint64_t kReservation = 0x51A7;
int gFailures = 0;

void Expect(bool condition, const char* name) noexcept
{
    std::printf("%s worker-stage-retirement.%s\n", condition ? "PASS" : "FAIL", name);
    gFailures += condition ? 0 : 1;
}

wil::unique_handle Event(const char* name)
{
    wil::unique_handle event{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    if (!event)
    {
        std::printf("FAIL worker-stage-retirement.create-event name=%s\n", name);
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
        std::printf("FAIL worker-stage-retirement.gate-timeout name=%s wait=0x%08lx\n", name, result);
        std::fflush(stdout);
        std::_Exit(1);
    }
}

bool StableMemory(void*, MemorySnapshot& snapshot) noexcept
{
    constexpr std::uint64_t available = 8ULL * 1024 * 1024 * 1024;
    snapshot = {available, available};
    return true;
}

JobOptions Options(JobClass kind, std::uint64_t serialKey, std::uint64_t memoryBytes, bool deferredRetirement,
                   bool allowMemoryWaiting) noexcept
{
    return {.kind = kind,
            .serialKey = serialKey,
            .memoryBytes = memoryBytes,
            .deferredRetirement = deferredRetirement,
            .allowMemoryWaiting = allowMemoryWaiting};
}

bool Start(JobScheduler& scheduler, const char* name) noexcept
{
    const bool started = scheduler.Initialize(1, {}, {&StableMemory, nullptr}) == S_OK;
    Expect(started, name);
    return started;
}

bool Start(JobScheduler& scheduler, unsigned int threads, const char* name) noexcept
{
    const bool started = scheduler.Initialize(threads, {}, {&StableMemory, nullptr}) == S_OK;
    Expect(started, name);
    return started;
}

void DrainUntilOwnerDeferral(JobScheduler& scheduler, HANDLE deferred, const char* name) noexcept
{
    const ULONGLONG deadline = GetTickCount64() + 10000;
    while (WaitForSingleObject(deferred, 0) != WAIT_OBJECT_0)
    {
        (void)DrainCompletions(scheduler);
        if (WaitForSingleObject(deferred, 0) == WAIT_OBJECT_0)
            return;
        const ULONGLONG now = GetTickCount64();
        const HANDLE wake = static_cast<HANDLE>(scheduler.WakeHandle());
        if (now >= deadline || !wake || WaitForSingleObject(wake, static_cast<DWORD>(deadline - now)) != WAIT_OBJECT_0)
        {
            std::printf("FAIL worker-stage-retirement.owner-deferral-timeout name=%s\n", name);
            std::fflush(stdout);
            std::_Exit(1);
        }
    }
}

enum class RunMode
{
    Finish,
    Fail,
    BadAlloc,
    GateFinish,
    WaitForShutdown
};

// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027)
struct State final
{
    HANDLE entered = nullptr;
    HANDLE release = nullptr;
    HANDLE firstRetirement = nullptr;
    HANDLE secondRetirement = nullptr;
    HANDLE secondRetirementRelease = nullptr;
    HANDLE ownerDeferred = nullptr;
    RunMode mode = RunMode::Finish;
    JobResult failure{JobStatus::Failed, E_ACCESSDENIED};
    unsigned int retainOwnerDeferrals = 0;
    const DWORD ownerThread = GetCurrentThreadId();
    std::atomic<unsigned int> usefulRuns{0};
    std::atomic<unsigned int> runReturns{0};
    std::atomic<unsigned int> producersEntered{0};
    std::atomic<unsigned int> producersActive{0};
    std::atomic<unsigned int> producersCompleted{0};
    std::atomic<unsigned int> expectedProducers{0};
    std::atomic<unsigned int> workerHooks{0};
    std::atomic<DWORD> workerThread{0};
    std::atomic<std::uint64_t> workerReservation{0};
    std::atomic<bool> workerDeferredFlag{false};
    std::atomic<bool> workerIsMta{false};
    std::atomic<bool> workerSawRunReturned{false};
    std::atomic<bool> workerSawAllProducersJoined{false};
    std::atomic<JobStatus> workerStatus{JobStatus::Finished};
    std::atomic<std::int32_t> workerError{0};
    unsigned int ownerDeferrals = 0;
    unsigned int completionCalls = 0;
    JobTicket firstOwnerTicket{};
    JobTicket ownerTicket{};
    JobStatus ownerStatus = JobStatus::Finished;
    std::int32_t ownerError = 0;
    JobStatus completedStatus = JobStatus::Finished;
    std::int32_t completedError = 0;
    bool ownerSawWorkerHook = false;
    bool completedSawWorkerHook = false;
};
#pragma warning(pop)

// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4626 5027)
class WorkerStageJob final : public Job
{
  public:
    explicit WorkerStageJob(State& state) noexcept : _state(state) {}

    JobResult Run(JobContext& context) override
    {
        _state.usefulRuns.fetch_add(1, std::memory_order_relaxed);
        [[maybe_unused]] const auto returned =
            wil::scope_exit([&]() noexcept { _state.runReturns.fetch_add(1, std::memory_order_release); });
        if (_state.entered && _state.mode != RunMode::GateFinish)
            (void)SetEvent(_state.entered);

        JobResult result{};
        switch (_state.mode)
        {
        case RunMode::Finish:
            break;
        case RunMode::Fail:
            result = _state.failure;
            break;
        case RunMode::BadAlloc:
            throw std::bad_alloc{};
        case RunMode::GateFinish:
        {
            const unsigned int participants = context.executor.Concurrency();
            _state.expectedProducers.store(participants, std::memory_order_relaxed);
            const auto producer = [](void* opaque) noexcept
            {
                auto& state = *static_cast<State*>(opaque);
                state.producersActive.fetch_add(1, std::memory_order_relaxed);
                const unsigned int entered = state.producersEntered.fetch_add(1, std::memory_order_acq_rel) + 1;
                if (entered == state.expectedProducers.load(std::memory_order_relaxed))
                    (void)SetEvent(state.entered);
                Wait(state.release, "run-release");
                state.producersCompleted.fetch_add(1, std::memory_order_release);
                state.producersActive.fetch_sub(1, std::memory_order_relaxed);
            };
            context.executor.RunParticipants(&_state, producer, participants);
            break;
        }
        case RunMode::WaitForShutdown:
        {
            const DWORD wait = WaitForSingleObject(static_cast<HANDLE>(context.shutdownEvent), 10000);
            result = wait == WAIT_OBJECT_0 ? JobResult{JobStatus::Cancelled} : JobResult{JobStatus::Failed, E_FAIL};
            break;
        }
        }
        return result;
    }

    void RetireWorkerStage(JobContext& context, JobResult result) noexcept override
    {
        const unsigned int ordinal = _state.workerHooks.fetch_add(1, std::memory_order_relaxed);
        _state.workerThread.store(GetCurrentThreadId(), std::memory_order_relaxed);
        _state.workerReservation.store(context.reservedMemoryBytes, std::memory_order_relaxed);
        _state.workerDeferredFlag.store(context.deferredRetirement, std::memory_order_relaxed);
        _state.workerStatus.store(result.status, std::memory_order_relaxed);
        _state.workerError.store(result.error, std::memory_order_relaxed);
        _state.workerSawRunReturned.store(_state.runReturns.load(std::memory_order_acquire) ==
                                              _state.usefulRuns.load(std::memory_order_relaxed),
                                          std::memory_order_relaxed);
        _state.workerSawAllProducersJoined.store(_state.producersCompleted.load(std::memory_order_acquire) ==
                                                         _state.expectedProducers.load(std::memory_order_relaxed) &&
                                                     _state.producersActive.load(std::memory_order_relaxed) == 0,
                                                 std::memory_order_relaxed);
        APTTYPE apartment{};
        APTTYPEQUALIFIER qualifier{};
        _state.workerIsMta.store(SUCCEEDED(CoGetApartmentType(&apartment, &qualifier)) && apartment == APTTYPE_MTA,
                                 std::memory_order_relaxed);
        HANDLE ready = ordinal == 0 ? _state.firstRetirement : _state.secondRetirement;
        if (ready)
            (void)SetEvent(ready);
        // Test-only event gate: owner Resume requires a drain to promote the stage. Keep its publication
        // behind an explicit release so that drain cannot race the pre-acknowledgement assertions.
        if (ordinal == 1 && _state.secondRetirementRelease)
            Wait(_state.secondRetirementRelease, "second-retirement-release");
    }

    bool DeferRetirement(JobResult result, JobTicket ticket) noexcept override
    {
        ++_state.ownerDeferrals;
        if (_state.ownerDeferrals == 1)
            _state.firstOwnerTicket = ticket;
        _state.ownerTicket = ticket;
        _state.ownerStatus = result.status;
        _state.ownerError = result.error;
        _state.ownerSawWorkerHook = _state.workerHooks.load(std::memory_order_acquire) != 0;
        if (_state.ownerDeferred)
            (void)SetEvent(_state.ownerDeferred);
        return _state.ownerDeferrals <= _state.retainOwnerDeferrals;
    }

    void Completed(JobResult result) noexcept override
    {
        ++_state.completionCalls;
        _state.completedStatus = result.status;
        _state.completedError = result.error;
        _state.completedSawWorkerHook = _state.workerHooks.load(std::memory_order_acquire) != 0;
    }

  private:
    State& _state;
};
#pragma warning(pop)

void TestFinishedFailureAndAllocationFailure()
{
    struct Scenario final
    {
        const char* initialize;
        const char* settle;
        RunMode mode;
        JobResult expected;
        const char* submit;
        const char* workerResult;
        const char* workerThread;
        const char* context;
        const char* ownerOrder;
        const char* completion;
        const char* release;
    };
    const Scenario scenarios[] = {
        {"finished.initialize",
         "finished.settle",
         RunMode::Finish,
         {},
         "finished.submit",
         "finished.worker-result",
         "finished.off-owner-mta-after-run",
         "finished.context-preserves-reservation-and-flag",
         "finished.owner-defer-follows-worker-hook",
         "finished.completion-follows-worker-hook",
         "finished.reservation-released"},
        {"failed.initialize",
         "failed.settle",
         RunMode::Fail,
         {JobStatus::Failed, E_ACCESSDENIED},
         "failed.submit",
         "failed.exact-failure-result",
         "failed.off-owner-mta-after-run",
         "failed.original-context",
         "failed.owner-defer-follows-worker-hook",
         "failed.exact-completion-result",
         "failed.reservation-released"},
        {"badalloc.initialize",
         "badalloc.settle",
         RunMode::BadAlloc,
         {JobStatus::Failed, E_OUTOFMEMORY},
         "badalloc.submit",
         "badalloc.translated-out-of-memory-result",
         "badalloc.off-owner-mta-after-throw",
         "badalloc.original-context",
         "badalloc.owner-defer-follows-worker-hook",
         "badalloc.exact-completion-result",
         "badalloc.reservation-released"},
    };

    for (const Scenario& scenario : scenarios)
    {
        JobScheduler scheduler;
        if (!Start(scheduler, scenario.initialize))
            continue;
        auto retired = Event(scenario.initialize);
        State state;
        state.mode = scenario.mode;
        state.firstRetirement = retired.get();
        std::unique_ptr<Job> job = std::make_unique<WorkerStageJob>(state);
        JobTicket ticket;
        const std::int32_t submitted =
            scheduler.Submit(job, Options(JobClass::Background, 0, kReservation, true, true), ticket);
        Expect(submitted == S_OK && !job, scenario.submit);
        if (submitted == S_OK)
        {
            Wait(retired.get(), scenario.initialize);
            Expect(SettleJobs(scheduler, 10000) == SettleResult::Settled, scenario.settle);
            Expect(state.workerHooks.load() == 1 && state.workerStatus.load() == scenario.expected.status &&
                       state.workerError.load() == scenario.expected.error,
                   scenario.workerResult);
            Expect(state.workerThread.load() != state.ownerThread && state.workerIsMta.load() &&
                       state.workerSawRunReturned.load(),
                   scenario.workerThread);
            Expect(state.workerReservation.load() == kReservation && state.workerDeferredFlag.load(), scenario.context);
            Expect(state.ownerDeferrals == 1 && state.ownerTicket == ticket && state.ownerSawWorkerHook &&
                       state.ownerStatus == scenario.expected.status && state.ownerError == scenario.expected.error,
                   scenario.ownerOrder);
            Expect(state.completionCalls == 1 && state.completedStatus == scenario.expected.status &&
                       state.completedError == scenario.expected.error && state.completedSawWorkerHook,
                   scenario.completion);
            Expect(scheduler.ReservedMemoryBytes() == 0 && scheduler.IsIdle(), scenario.release);
        }
        scheduler.Close();
    }
}

void TestCancellationDuringRunIsNormalizedBeforeWorkerHook()
{
    JobScheduler scheduler;
    if (!Start(scheduler, 2, "run-cancel.initialize"))
        return;
    auto entered = Event("run-cancel-entered");
    auto release = Event("run-cancel-release");
    auto retired = Event("run-cancel-retired");
    State state;
    state.mode = RunMode::GateFinish;
    state.entered = entered.get();
    state.release = release.get();
    state.firstRetirement = retired.get();
    std::unique_ptr<Job> job = std::make_unique<WorkerStageJob>(state);
    JobTicket ticket;
    const std::int32_t submitted =
        scheduler.Submit(job, Options(JobClass::Background, 0, kReservation, true, true), ticket);
    Expect(submitted == S_OK, "run-cancel.submit");
    if (submitted == S_OK)
    {
        Wait(entered.get(), "run-cancel.entered");
        Expect(scheduler.Cancel(ticket), "run-cancel.cancel-running-stage");
        (void)SetEvent(release.get());
        Wait(retired.get(), "run-cancel.worker-hook");
        Expect(state.workerStatus.load() == JobStatus::Cancelled && state.workerError.load() == 0 &&
                   state.workerSawRunReturned.load() && state.workerThread.load() != state.ownerThread &&
                   state.workerIsMta.load(),
               "run-cancel.cancelled-result-after-run-return");
        Expect(state.expectedProducers.load() == 2 && state.producersEntered.load() >= 1 &&
                   state.workerSawAllProducersJoined.load(),
               "run-cancel.retirement-waits-for-all-producers-to-join");
        Expect(SettleJobs(scheduler, 10000) == SettleResult::Settled, "run-cancel.settle");
        Expect(state.usefulRuns.load() == 1 && state.workerHooks.load() == 1 && state.completionCalls == 1 &&
                   state.completedStatus == JobStatus::Cancelled,
               "run-cancel.one-useful-run-one-retirement-one-callback");
    }
    scheduler.Close();
}

void TestSuspendedCancellationSkipsUsefulStageAndHoldsFifo()
{
    JobScheduler scheduler;
    if (!Start(scheduler, "resume-cancel.initialize"))
        return;
    auto firstRetired = Event("resume-cancel-first-retirement");
    auto ownerDeferred = Event("resume-cancel-owner-deferred");
    auto secondRetired = Event("resume-cancel-second-retirement");
    auto secondRelease = Event("resume-cancel-second-release");
    State first;
    first.firstRetirement = firstRetired.get();
    first.secondRetirement = secondRetired.get();
    first.secondRetirementRelease = secondRelease.get();
    first.ownerDeferred = ownerDeferred.get();
    first.retainOwnerDeferrals = 1;
    std::unique_ptr<Job> firstJob = std::make_unique<WorkerStageJob>(first);
    JobTicket firstTicket;
    const std::int32_t firstSubmitted =
        scheduler.Submit(firstJob, Options(JobClass::Commit, 0xC1, kReservation, true, true), firstTicket);
    Expect(firstSubmitted == S_OK, "resume-cancel.first-submit");

    State successor;
    std::unique_ptr<Job> successorJob = std::make_unique<WorkerStageJob>(successor);
    JobTicket successorTicket;
    const std::int32_t successorSubmitted =
        scheduler.Submit(successorJob, Options(JobClass::Commit, 0xC1, 0, true, true), successorTicket);
    Expect(successorSubmitted == S_OK, "resume-cancel.successor-submit");

    if (firstSubmitted == S_OK && successorSubmitted == S_OK)
    {
        Wait(firstRetired.get(), "resume-cancel.first-worker-retirement");
        DrainUntilOwnerDeferral(scheduler, ownerDeferred.get(), "resume-cancel.owner-suspension");
        Expect(first.ownerDeferrals == 1 && first.firstOwnerTicket == firstTicket && first.ownerTicket == firstTicket &&
                   first.ownerSawWorkerHook,
               "resume-cancel.owner-suspends-after-worker-retirement");
        Expect(first.workerHooks.load() == 1 && first.workerReservation.load() == kReservation &&
                   first.workerDeferredFlag.load() && successor.usefulRuns.load() == 0,
               "resume-cancel.suspension-retains-original-stage-and-fifo");
        Expect(scheduler.ReservedMemoryBytes() == kReservation, "resume-cancel.original-reservation-retained");
        Expect(scheduler.Cancel(firstTicket), "resume-cancel.cancel-suspended-stage");
        Expect(scheduler.Resume(firstTicket), "resume-cancel.resume-exact-original-ticket");
        Expect(!scheduler.Resume(firstTicket), "resume-cancel.duplicate-resume-refused");
        (void)DrainCompletions(scheduler); // Promote the original resumed ticket on its owner.
        Wait(secondRetired.get(), "resume-cancel.cancelled-retirement-stage");
        Expect(first.usefulRuns.load() == 1 && first.workerHooks.load() == 2 &&
                   first.workerStatus.load() == JobStatus::Cancelled && first.workerError.load() == 0,
               "resume-cancel.skipped-useful-run-still-retires-worker-stage");
        Expect(first.workerReservation.load() == kReservation && first.workerDeferredFlag.load() &&
                   first.workerSawRunReturned.load() && first.workerThread.load() != first.ownerThread &&
                   first.workerIsMta.load() && first.ownerDeferrals == 1 && first.ownerTicket == firstTicket,
               "resume-cancel.retirement-stage-keeps-context-and-ticket");
        Expect(successor.usefulRuns.load() == 0 && first.completionCalls == 0,
               "resume-cancel.fifo-successor-waits-for-owner-retirement-ack");
        (void)SetEvent(secondRelease.get());
        Expect(SettleJobs(scheduler, 10000) == SettleResult::Settled, "resume-cancel.settle-through-fifo-successor");
        Expect(first.completionCalls == 1 && first.completedStatus == JobStatus::Cancelled &&
                   first.ownerDeferrals == 2 && first.firstOwnerTicket == firstTicket &&
                   first.ownerTicket == firstTicket && first.ownerStatus == JobStatus::Cancelled &&
                   first.ownerError == 0,
               "resume-cancel.original-ticket-completes-once-after-cleanup-ack");
        Expect(successor.usefulRuns.load() == 1 && successor.completionCalls == 1 &&
                   scheduler.ReservedMemoryBytes() == 0,
               "resume-cancel.successor-runs-after-retirement-ack");
    }
    scheduler.Close();
}

// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027)
struct CloseState final
{
    std::atomic<unsigned int> ownerCleanup{0};
    std::atomic<unsigned int> finalWorkerCleanup{0};
    std::atomic<DWORD> ownerThread{0};
    std::atomic<DWORD> workerThread{0};
    std::atomic<bool> workerAfterOwner{false};
    std::atomic<bool> workerMta{false};
};
#pragma warning(pop)

void OwnerClose(void* context) noexcept
{
    auto& state = *static_cast<CloseState*>(context);
    state.ownerThread.store(GetCurrentThreadId(), std::memory_order_relaxed);
    state.ownerCleanup.fetch_add(1, std::memory_order_release);
}

void WorkerClose(void* context) noexcept
{
    auto& state = *static_cast<CloseState*>(context);
    state.workerThread.store(GetCurrentThreadId(), std::memory_order_relaxed);
    state.workerAfterOwner.store(state.ownerCleanup.load(std::memory_order_acquire) == 1, std::memory_order_relaxed);
    APTTYPE apartment{};
    APTTYPEQUALIFIER qualifier{};
    state.workerMta.store(SUCCEEDED(CoGetApartmentType(&apartment, &qualifier)) && apartment == APTTYPE_MTA,
                          std::memory_order_relaxed);
    state.finalWorkerCleanup.fetch_add(1, std::memory_order_release);
}

void TestShutdownUsesJoinedCleanupForUndispatchedWork()
{
    JobScheduler scheduler;
    if (!Start(scheduler, "shutdown.initialize"))
        return;
    auto entered = Event("shutdown.entered");
    State running;
    running.mode = RunMode::WaitForShutdown;
    running.entered = entered.get();
    std::unique_ptr<Job> runningJob = std::make_unique<WorkerStageJob>(running);
    JobTicket runningTicket;
    const std::int32_t runningSubmitted =
        scheduler.Submit(runningJob, Options(JobClass::Background, 0, 0, true, true), runningTicket);
    Expect(runningSubmitted == S_OK, "shutdown.running-submit");
    if (runningSubmitted != S_OK)
    {
        scheduler.Close();
        return;
    }
    Wait(entered.get(), "shutdown.running-entered");

    State queued;
    std::unique_ptr<Job> queuedJob = std::make_unique<WorkerStageJob>(queued);
    JobTicket queuedTicket;
    const std::int32_t queuedSubmitted =
        scheduler.Submit(queuedJob, Options(JobClass::Background, 0, 0, true, true), queuedTicket);
    Expect(queuedSubmitted == S_OK, "shutdown.undispatched-submit");
    if (queuedSubmitted != S_OK)
    {
        scheduler.Close();
        return;
    }

    CloseState close;
    scheduler.Close({&OwnerClose, &WorkerClose, &close});
    Expect(running.workerHooks.load() == 1 && running.workerStatus.load() == JobStatus::Cancelled &&
               running.runReturns.load() == 1 && running.workerSawRunReturned.load() &&
               running.workerThread.load() != running.ownerThread && running.workerIsMta.load(),
           "shutdown.dispatched-stage-joins-and-retires");
    Expect(queued.usefulRuns.load() == 0 && queued.workerHooks.load() == 0 && queued.completionCalls == 0,
           "shutdown.undispatched-stage-does-not-fabricate-worker-hook");
    Expect(close.ownerCleanup.load() == 1 && close.finalWorkerCleanup.load() == 1 &&
               close.ownerThread.load() == running.ownerThread && close.workerThread.load() != running.ownerThread &&
               close.workerAfterOwner.load() && close.workerMta.load(),
           "shutdown.existing-joined-owner-and-final-worker-hooks-run-once");
}

void TestOptionOffPreservesNoHookBehavior()
{
    JobScheduler scheduler;
    if (!Start(scheduler, "optout.initialize"))
        return;
    State state;
    std::unique_ptr<Job> job = std::make_unique<WorkerStageJob>(state);
    JobTicket ticket;
    const std::int32_t submitted =
        scheduler.Submit(job, Options(JobClass::Background, 0, kReservation, false, true), ticket);
    Expect(submitted == S_OK, "optout.submit-explicit-false");
    if (submitted == S_OK)
    {
        Expect(SettleJobs(scheduler, 10000) == SettleResult::Settled, "optout.settle");
        Expect(state.usefulRuns.load() == 1 && state.workerHooks.load() == 0 && state.ownerDeferrals == 0 &&
                   state.completionCalls == 1 && state.completedStatus == JobStatus::Finished,
               "optout.no-worker-or-owner-retirement-hook");
        Expect(scheduler.ReservedMemoryBytes() == 0 && scheduler.IsIdle(), "optout.original-completion-release");
    }
    scheduler.Close();
}
} // namespace

int RunHostWorkerStageRetirementTests()
{
    gFailures = 0;
    TestFinishedFailureAndAllocationFailure();
    TestCancellationDuringRunIsNormalizedBeforeWorkerHook();
    TestSuspendedCancellationSkipsUsefulStageAndHoldsFifo();
    TestShutdownUsesJoinedCleanupForUndispatchedWork();
    TestOptionOffPreservesNoHookBehavior();
    std::printf("%s worker-stage-retirement.summary failures=%d\n", gFailures == 0 ? "PASS" : "FAIL", gFailures);
    return gFailures;
}
