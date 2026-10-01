#include "JobStageTests.h"

#include "../../Common/Jobs.h"
#include "../../Common/WorkerPool.h"

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/resource.h>
#pragma warning(pop)

namespace
{
int g_failures = 0;

void Expect(bool condition, const char* name) noexcept
{
    std::printf("%s job-stage.%s\n", condition ? "PASS" : "FAIL", name);
    g_failures += condition ? 0 : 1;
}

wil::unique_handle Event()
{
    wil::unique_handle event{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    if (!event)
    {
        std::printf("FAIL job-stage.create-event\n");
        std::fflush(stdout);
        std::_Exit(1);
    }
    return event;
}

void Wait(HANDLE event, const char* label = "unlabelled") noexcept
{
    const auto waited = event ? WaitForSingleObject(event, 10000) : WAIT_FAILED;
    if (waited != WAIT_OBJECT_0)
    {
        std::printf("FAIL job-stage.gate-timeout label=%s wait=0x%08lx\n", label, waited);
        std::fflush(stdout);
        std::_Exit(1);
    }
}

// A scheduler wake also carries memory/owner notifications. Only the hook event proves suspension;
// an unrelated wake must drain and block again rather than waiting for an unpumped hook.
void WaitForSuspension(JobScheduler& scheduler, HANDLE deferred) noexcept
{
    const auto deadline = GetTickCount64() + 10000;
    while (WaitForSingleObject(deferred, 0) != WAIT_OBJECT_0)
    {
        (void)DrainCompletions(scheduler);
        if (WaitForSingleObject(deferred, 0) == WAIT_OBJECT_0)
            return;
        const auto now = GetTickCount64();
        if (now >= deadline ||
            WaitForSingleObject(scheduler.WakeHandle(), static_cast<DWORD>(deadline - now)) != WAIT_OBJECT_0)
        {
            std::printf("FAIL job-stage.suspension-timeout\n");
            std::fflush(stdout);
            std::_Exit(1);
        }
    }
}

bool StableMemory(void*, MemorySnapshot& snapshot) noexcept
{
    constexpr std::uint64_t available = 8ULL * 1024 * 1024 * 1024;
    snapshot = {available, available};
    return true;
}

constexpr UINT kReleaseMessage = WM_APP + 0x451;

struct StageState final
{
    HANDLE entered = nullptr;
    HANDLE release = nullptr;
    HANDLE deferred = nullptr;
    std::atomic<unsigned int> runs{0};
    unsigned int deferrals = 0;
    unsigned int completions = 0;
    JobStatus status = JobStatus::Failed;
    JobTicket ticket{};
    bool waitOnFirstRun = false;
    std::atomic<unsigned int>* order = nullptr;
    std::atomic<unsigned int> firstRunOrder{0};
    std::atomic<unsigned int> completionOrder{0};
    std::atomic<std::uint64_t> reservedInSecondRun{0};
};

class TwoStageJob final : public Job
{
  public:
    explicit TwoStageJob(StageState& state) noexcept : _state(state) {}

    JobResult Run(JobContext& context) override
    {
        const unsigned int ordinal = _state.runs.fetch_add(1, std::memory_order_relaxed);
        if (ordinal == 0 && _state.order)
        {
            _state.firstRunOrder.store(_state.order->fetch_add(1, std::memory_order_relaxed) + 1,
                                       std::memory_order_relaxed);
        }
        if (ordinal == 0)
        {
            if (_state.entered)
            {
                (void)SetEvent(_state.entered);
            }
            if (_state.waitOnFirstRun && WaitForSingleObject(_state.release, 10000) != WAIT_OBJECT_0)
            {
                std::printf("FAIL job-stage.worker-gate-timeout\n");
                std::fflush(stdout);
                std::_Exit(1);
            }
        }
        else
        {
            // Resume stages borrow the original reservation rather than reserving the graph again.
            _state.reservedInSecondRun.store(context.reservedMemoryBytes, std::memory_order_relaxed);
        }
        return {};
    }

    bool DeferCompletion(JobResult result, JobTicket ticket) noexcept override
    {
        if (!_state.deferred || _state.deferrals != 0 || result.status != JobStatus::Finished)
        {
            return false;
        }
        ++_state.deferrals;
        _state.ticket = ticket;
        (void)SetEvent(_state.deferred);
        return true;
    }

    void Completed(JobResult result) noexcept override
    {
        ++_state.completions;
        _state.status = result.status;
        if (_state.order)
        {
            _state.completionOrder.store(_state.order->fetch_add(1, std::memory_order_relaxed) + 1,
                                         std::memory_order_relaxed);
        }
    }

  private:
    StageState& _state;
};

struct HookResumeState final
{
    JobScheduler* scheduler = nullptr;
    HANDLE secondRunEntered = nullptr;
    std::atomic<unsigned int> runs{0};
    std::atomic<unsigned int> completions{0};
    std::atomic<bool> firstResume{false};
    std::atomic<bool> duplicateResume{true};
    std::atomic<bool> secondRunObservedInsideHook{true};
};

class HookResumeJob final : public Job
{
  public:
    explicit HookResumeJob(HookResumeState& state) noexcept : _state(state) {}
    JobResult Run(JobContext&) override
    {
        if (_state.runs.fetch_add(1, std::memory_order_relaxed) == 1)
        {
            (void)SetEvent(_state.secondRunEntered);
        }
        return {};
    }
    bool DeferCompletion(JobResult, JobTicket ticket) noexcept override
    {
        if (_state.runs.load(std::memory_order_relaxed) != 1)
            return false;
        _state.firstResume.store(_state.scheduler->Resume(ticket), std::memory_order_relaxed);
        _state.duplicateResume.store(_state.scheduler->Resume(ticket), std::memory_order_relaxed);
        _state.secondRunObservedInsideHook.store(WaitForSingleObject(_state.secondRunEntered, 0) == WAIT_OBJECT_0,
                                                 std::memory_order_relaxed);
        return true;
    }
    void Completed(JobResult) noexcept override
    {
        _state.completions.fetch_add(1, std::memory_order_relaxed);
    }

  private:
    HookResumeState& _state;
};

struct FalseDeferState final
{
    JobScheduler* scheduler = nullptr;
    std::atomic<unsigned int> runs{0};
    std::atomic<unsigned int> completions{0};
    std::atomic<bool> resumeAccepted{false};
    bool cancelBeforeFinalize = false;
    JobStatus finalStatus = JobStatus::Failed;
};

class ResumeThenFinalizeJob final : public Job
{
  public:
    explicit ResumeThenFinalizeJob(FalseDeferState& state) noexcept : _state(state) {}
    JobResult Run(JobContext&) override
    {
        _state.runs.fetch_add(1, std::memory_order_relaxed);
        return {};
    }
    bool DeferCompletion(JobResult, JobTicket ticket) noexcept override
    {
        _state.resumeAccepted.store(_state.scheduler->Resume(ticket), std::memory_order_relaxed);
        if (_state.cancelBeforeFinalize)
            (void)_state.scheduler->Cancel(ticket);
        return false;
    }
    void Completed(JobResult result) noexcept override
    {
        _state.finalStatus = result.status;
        _state.completions.fetch_add(1, std::memory_order_relaxed);
    }

  private:
    FalseDeferState& _state;
};

struct OwnerPump final : OwnerWorkPump
{
    JobScheduler& scheduler;
    JobTicket ticket{};
    bool pending = false;
    bool ready = false;
    unsigned int pumps = 0;
    bool resumeResult = false;
    bool duplicateResult = true;
    bool* destroyed = nullptr;

    explicit OwnerPump(JobScheduler& owner) noexcept : scheduler(owner) {}
    ~OwnerPump() override
    {
        if (destroyed)
        {
            *destroyed = true;
        }
    }

    bool HasPending() const noexcept override
    {
        return pending;
    }

    bool HasReady() const noexcept override
    {
        return ready;
    }

    void Pump() noexcept override
    {
        ++pumps;
        ready = false;
        pending = false;
        resumeResult = scheduler.Resume(ticket);
        duplicateResult = scheduler.Resume(ticket);
    }
};

struct ReleaseDispatch final
{
    OwnerPump* pump = nullptr;
    JobScheduler* scheduler = nullptr;
    unsigned int handled = 0;
};

void DispatchRelease(void* context, void* nativeMessage) noexcept
{
    auto& state = *static_cast<ReleaseDispatch*>(context);
    const auto& message = *static_cast<MSG*>(nativeMessage);
    if (message.message == kReleaseMessage)
    {
        ++state.handled;
        state.pump->ready = true;
        state.scheduler->NotifyOwnerWork();
        return;
    }
    TranslateMessage(&message);
    DispatchMessageW(&message);
}

void TestDeferResumeFifoAndReadiness()
{
    JobScheduler scheduler;
    Expect(scheduler.Initialize(1, {}, {StableMemory, nullptr}) == S_OK, "resume.initialize");
    auto entered = Event();
    auto releaseWorker = Event();
    auto deferred = Event();
    std::atomic<unsigned int> order{0};
    StageState firstState{entered.get(), releaseWorker.get(), deferred.get()};
    firstState.waitOnFirstRun = true;
    firstState.order = &order;
    OwnerPump pump{scheduler};
    pump.pending = true;
    Expect(scheduler.RegisterOwnerWork(pump), "resume.register-source");
    OwnerPump duplicate{scheduler};
    Expect(!scheduler.RegisterOwnerWork(duplicate), "resume.duplicate-source-refused");
    std::unique_ptr<Job> firstJob = std::make_unique<TwoStageJob>(firstState);
    JobTicket firstTicket;
    Expect(scheduler.Submit(firstJob, {JobClass::Commit, 0x51, 4096}, firstTicket) == S_OK, "resume.first-submit");
    Wait(entered.get());

    StageState secondState{};
    secondState.order = &order;
    std::unique_ptr<Job> secondJob = std::make_unique<TwoStageJob>(secondState);
    JobTicket secondTicket;
    Expect(scheduler.Submit(secondJob, {JobClass::Commit, 0x51, 0}, secondTicket) == S_OK,
           "resume.serial-younger-submit");

    // Consume submit readiness; later memory notifications can still precede the worker's result.
    (void)DrainCompletions(scheduler);
    const HANDLE wake = static_cast<HANDLE>(scheduler.WakeHandle());
    (void)WaitForSingleObject(wake, 0);
    (void)SetEvent(releaseWorker.get());
    WaitForSuspension(scheduler, deferred.get());

    pump.ticket = firstState.ticket;
    Expect(!scheduler.UnregisterOwnerWork(pump), "resume.unregister-pending-refused");
    Expect(firstState.ticket == firstTicket && firstState.deferrals == 1, "resume.ticket-retained");
    Expect(scheduler.ReservedMemoryBytes() == 4096, "resume.peak-reservation-retained");
    Expect(firstState.reservedInSecondRun.load(std::memory_order_relaxed) == 0, "resume.second-stage-not-yet-run");
    Expect(!scheduler.IsIdle() && !scheduler.HasUiWork(), "resume.suspended-is-pending-not-ui-work");
    Expect(pump.pumps == 0, "resume.source-not-pumped-before-ready");

    MSG message{};
    (void)PeekMessageW(&message, nullptr, 0, 0, PM_NOREMOVE);
    ReleaseDispatch dispatch{&pump, &scheduler};
    Expect(PostThreadMessageW(GetCurrentThreadId(), kReleaseMessage, 0, 0) != FALSE,
           "resume.post-owner-release-message");
    const SettleResult settled = SettleJobs(scheduler, 10000, DispatchRelease, &dispatch);
    Expect(settled == SettleResult::Settled, "resume.settle-waits-for-and-dispatches-release");
    Expect(dispatch.handled == 1 && pump.pumps == 1 && pump.resumeResult, "resume.ready-source-resumed-on-owner");
    Expect(!pump.duplicateResult && !scheduler.Resume(firstTicket), "resume.duplicate-and-stale-refused");
    Expect(firstState.runs.load(std::memory_order_relaxed) == 2 && firstState.completions == 1 &&
               firstState.status == JobStatus::Finished,
           "resume.two-runs-one-final-completion");
    Expect(secondState.runs.load(std::memory_order_relaxed) == 1 && secondState.completions == 1,
           "resume.younger-serial-runs-after-final-apply");
    Expect(secondState.firstRunOrder.load(std::memory_order_relaxed) >
               firstState.completionOrder.load(std::memory_order_relaxed),
           "resume.serial-younger-starts-after-predecessor-apply");
    Expect(firstState.reservedInSecondRun.load(std::memory_order_relaxed) == 4096,
           "resume.original-peak-borrowed-by-next-stage");
    Expect(scheduler.ReservedMemoryBytes() == 0 && scheduler.IsIdle(), "resume.reservation-released-at-retirement");
    Expect(scheduler.UnregisterOwnerWork(pump), "resume.unregister-after-retirement");
    scheduler.Close();
}

void TestCancellationBeforeResume()
{
    JobScheduler scheduler;
    Expect(scheduler.Initialize(1) == S_OK, "cancel.initialize");
    auto entered = Event();
    auto releaseWorker = Event();
    auto deferred = Event();
    StageState state{entered.get(), releaseWorker.get(), deferred.get()};
    state.waitOnFirstRun = true;
    std::unique_ptr<Job> job = std::make_unique<TwoStageJob>(state);
    JobTicket ticket;
    Expect(scheduler.Submit(job, {JobClass::Background}, ticket) == S_OK, "cancel.submit");
    Wait(entered.get());
    (void)DrainCompletions(scheduler);
    const HANDLE wake = static_cast<HANDLE>(scheduler.WakeHandle());
    (void)WaitForSingleObject(wake, 0);
    (void)SetEvent(releaseWorker.get());
    WaitForSuspension(scheduler, deferred.get());

    OwnerPump pump{scheduler};
    pump.ticket = ticket;
    pump.pending = true;
    Expect(scheduler.RegisterOwnerWork(pump), "cancel.register-source");
    Expect(scheduler.Cancel(ticket), "cancel.suspended-accepted");
    pump.ready = true;
    scheduler.NotifyOwnerWork();
    const SettleResult settled = SettleJobs(scheduler, 10000);
    Expect(settled == SettleResult::Settled, "cancel.settle");
    Expect(state.runs.load(std::memory_order_relaxed) == 1 && state.completions == 1 &&
               state.status == JobStatus::Cancelled,
           "cancel.before-resume-skips-stage-and-completes-once");
    Expect(scheduler.UnregisterOwnerWork(pump), "cancel.unregister");
    scheduler.Close();
}

struct FailureState final
{
    unsigned int runs = 0;
    unsigned int deferCalls = 0;
    unsigned int completions = 0;
    JobStatus status = JobStatus::Finished;
};

class FailureJob final : public Job
{
  public:
    explicit FailureJob(FailureState& state) noexcept : _state(state) {}
    JobResult Run(JobContext&) override
    {
        ++_state.runs;
        return {JobStatus::Failed, E_FAIL};
    }
    bool DeferCompletion(JobResult, JobTicket) noexcept override
    {
        ++_state.deferCalls;
        return false;
    }
    void Completed(JobResult result) noexcept override
    {
        ++_state.completions;
        _state.status = result.status;
    }

  private:
    FailureState& _state;
};

void TestFailureWithoutDeferReleasesReservation()
{
    JobScheduler scheduler;
    Expect(scheduler.Initialize(1, {}, {StableMemory, nullptr}) == S_OK, "failure.initialize");
    FailureState state;
    std::unique_ptr<Job> job = std::make_unique<FailureJob>(state);
    JobTicket ticket;
    Expect(scheduler.Submit(job, {JobClass::Background, 0, 1024}, ticket) == S_OK, "failure.submit");
    Expect(SettleJobs(scheduler, 10000) == SettleResult::Settled, "failure.settle");
    Expect(state.runs == 1 && state.deferCalls == 0 && state.completions == 1 && state.status == JobStatus::Failed,
           "failure.no-defer-finalizes-once");
    Expect(scheduler.ReservedMemoryBytes() == 0 && scheduler.IsIdle(), "failure.no-defer-cleans-reservation");
    scheduler.Close();
}

void TestResumeInsideDeferredHookWaitsForHookReturn()
{
    JobScheduler scheduler;
    Expect(scheduler.Initialize(1) == S_OK, "hook-resume.initialize");
    auto secondRunEntered = Event();
    HookResumeState state;
    state.scheduler = &scheduler;
    state.secondRunEntered = secondRunEntered.get();
    std::unique_ptr<Job> job = std::make_unique<HookResumeJob>(state);
    JobTicket ticket;
    Expect(scheduler.Submit(job, {}, ticket) == S_OK, "hook-resume.submit");
    Expect(SettleJobs(scheduler, 10000) == SettleResult::Settled, "hook-resume.settle");
    Expect(state.firstResume.load(std::memory_order_relaxed) && !state.duplicateResume.load(std::memory_order_relaxed),
           "hook-resume.exactly-one-resume-accepted-inside-hook");
    Expect(!state.secondRunObservedInsideHook.load(std::memory_order_relaxed),
           "hook-resume.second-worker-stage-not-entered-during-hook");
    Expect(WaitForSingleObject(secondRunEntered.get(), 0) == WAIT_OBJECT_0 &&
               state.runs.load(std::memory_order_relaxed) == 2 &&
               state.completions.load(std::memory_order_relaxed) == 1,
           "hook-resume.stage-starts-after-hook-and-finalizes-once");
    scheduler.Close();
}

void TestResumeThenFalseDeferFinalizesWithoutAnotherStage(bool cancel)
{
    JobScheduler scheduler;
    Expect(scheduler.Initialize(1) == S_OK, "false-defer.initialize");
    FalseDeferState state;
    state.scheduler = &scheduler;
    state.cancelBeforeFinalize = cancel;
    std::unique_ptr<Job> job = std::make_unique<ResumeThenFinalizeJob>(state);
    JobTicket ticket;
    Expect(scheduler.Submit(job, {}, ticket) == S_OK, "false-defer.submit");
    Expect(SettleJobs(scheduler, 10000) == SettleResult::Settled, "false-defer.settle");
    Expect(state.resumeAccepted.load(std::memory_order_relaxed), "false-defer.resume-request-accepted-in-hook");
    Expect(state.runs.load(std::memory_order_relaxed) == 1 && state.completions.load(std::memory_order_relaxed) == 1 &&
               !scheduler.Resume(ticket),
           "false-defer.finalizes-once-and-never-runs-resumed-stage");
    Expect(state.finalStatus == (cancel ? JobStatus::Cancelled : JobStatus::Finished),
           "false-defer.hook-cancellation-respected-before-final-apply");
    scheduler.Close();
}

void CompleteFirstStageAsSuspended(JobScheduler& scheduler, StageState& state, JobTicket& ticket,
                                   std::unique_ptr<Job>& job, const char* submitName)
{
    Expect(scheduler.Submit(job, {JobClass::Background}, ticket) == S_OK, submitName);
    Wait(state.entered, submitName);
    (void)DrainCompletions(scheduler);
    // A memory notification is deliberately unrelated to the held first worker stage. Suspension requires
    // the hook itself, even if another such notification arrives after release and before worker completion.
    scheduler.NotifyMemoryChanged();
    Wait(static_cast<HANDLE>(scheduler.WakeHandle()), "generation.unrelated-memory-wake");
    (void)DrainCompletions(scheduler);
    Expect(WaitForSingleObject(state.deferred, 0) == WAIT_TIMEOUT && state.deferrals == 0 &&
               state.runs.load(std::memory_order_relaxed) == 1,
           "generation.unrelated-wake-does-not-prove-suspension");
    (void)SetEvent(state.release);
    WaitForSuspension(scheduler, state.deferred);
}

void TestReusedSlotRejectsOlderGenerationResume()
{
    JobScheduler scheduler;
    Expect(scheduler.Initialize(1) == S_OK, "generation.initialize");
    auto oldEntered = Event();
    auto oldRelease = Event();
    auto oldDeferred = Event();
    StageState oldState{oldEntered.get(), oldRelease.get(), oldDeferred.get()};
    oldState.waitOnFirstRun = true;
    std::unique_ptr<Job> oldJob = std::make_unique<TwoStageJob>(oldState);
    JobTicket oldTicket;
    CompleteFirstStageAsSuspended(scheduler, oldState, oldTicket, oldJob, "generation.old-submit");
    Expect(scheduler.Cancel(oldTicket) && scheduler.Resume(oldTicket), "generation.old-cancel-and-promote");
    Expect(SettleJobs(scheduler, 10000) == SettleResult::Settled, "generation.old-retire");

    auto newEntered = Event();
    auto newRelease = Event();
    auto newDeferred = Event();
    StageState newState{newEntered.get(), newRelease.get(), newDeferred.get()};
    newState.waitOnFirstRun = true;
    std::unique_ptr<Job> newJob = std::make_unique<TwoStageJob>(newState);
    JobTicket newTicket;
    CompleteFirstStageAsSuspended(scheduler, newState, newTicket, newJob, "generation.new-submit");
    Expect(newTicket.slot == oldTicket.slot && newTicket.generation != oldTicket.generation,
           "generation.same-slot-new-generation");
    Expect(!scheduler.Resume(oldTicket) && scheduler.Resume(newTicket),
           "generation.old-ticket-refused-replacement-resumed");
    Expect(SettleJobs(scheduler, 10000) == SettleResult::Settled, "generation.new-retire");
    Expect(newState.runs.load(std::memory_order_relaxed) == 2 && newState.completions == 1,
           "generation.replacement-stage-completes-once");
    scheduler.Close();
}

struct ExternalOwnerState final
{
    JobScheduler* scheduler = nullptr;
    std::atomic<bool> pending{true};
    std::atomic<bool> ready{false};
    std::atomic<unsigned int> pumps{0};
    std::atomic<bool> reentrantDrainIgnored{false};
    std::atomic<bool> reentrantActionIgnored{false};
    std::atomic<bool> unregisterRefused{false};
    std::atomic<bool> registerRefused{false};
    OwnerWorkPump* replacement = nullptr;
};

class ExternalOwnerPump final : public OwnerWorkPump
{
  public:
    explicit ExternalOwnerPump(ExternalOwnerState& state) noexcept : _state(state) {}
    bool HasPending() const noexcept override
    {
        return _state.pending.load(std::memory_order_acquire);
    }
    bool HasReady() const noexcept override
    {
        return _state.ready.load(std::memory_order_acquire);
    }
    void Pump() noexcept override
    {
        _state.pumps.fetch_add(1, std::memory_order_relaxed);
        _state.ready.store(false, std::memory_order_release);
        _state.pending.store(false, std::memory_order_release);
        _state.reentrantDrainIgnored.store(DrainCompletions(*_state.scheduler) == 0, std::memory_order_relaxed);
        _state.reentrantActionIgnored.store(!RunUiActions(*_state.scheduler), std::memory_order_relaxed);
        _state.unregisterRefused.store(!_state.scheduler->UnregisterOwnerWork(*this), std::memory_order_relaxed);
        _state.registerRefused.store(!_state.scheduler->RegisterOwnerWork(*_state.replacement),
                                     std::memory_order_relaxed);
    }

  private:
    ExternalOwnerState& _state;
};

struct WaitNotifyState final
{
    ExternalOwnerState* state = nullptr;
    JobScheduler* scheduler = nullptr;
    HANDLE callbackDone = nullptr;
};

void PublishExternalOwnerReady(void* context) noexcept
{
    auto& state = *static_cast<WaitNotifyState*>(context);
    state.state->ready.store(true, std::memory_order_release);
    state.scheduler->NotifyOwnerWork();
    (void)SetEvent(state.callbackDone);
}

void TestExternalOwnerReadinessAndCallbackReentrancy()
{
    JobScheduler scheduler;
    // OS high/low-memory events legitimately request unrelated owner work. Isolate this readiness state machine
    // from those asynchronous notifications, then exercise an explicit memory wake separately below.
    Expect(scheduler.Initialize(1, {}, {StableMemory, nullptr}) == S_OK, "external-owner.initialize");
    auto callbackDone = Event();
    ExternalOwnerState state;
    state.scheduler = &scheduler;
    ExternalOwnerPump source{state};
    ExternalOwnerPump replacement{state};
    state.replacement = &replacement;
    Expect(scheduler.RegisterOwnerWork(source), "external-owner.register");
    Expect(!scheduler.IsIdle() && !scheduler.HasUiWork(), "external-owner.pending-is-idle-blocker-not-ui-work");
    scheduler.NotifyMemoryChanged();
    Expect(scheduler.HasUiWork() && state.pumps.load() == 0, "external-owner.memory-wake-is-separate-ready-work");
    (void)DrainCompletions(scheduler);
    Expect(!scheduler.IsIdle() && !scheduler.HasUiWork() && state.pumps.load() == 0,
           "external-owner.memory-drain-does-not-pump-unready-source");

    auto signal = Event();
    JobEventTicket waitTicket;
    WaitNotifyState notify{&state, &scheduler, callbackDone.get()};
    Expect(scheduler.RegisterEventWait(signal.get(), &PublishExternalOwnerReady, &notify, waitTicket) == S_OK,
           "external-owner.wait-registration");
    (void)SetEvent(signal.get());
    Wait(callbackDone.get());
    Expect(scheduler.RemoveEventWait(waitTicket), "external-owner.shared-wait-removed-and-joined");
    Expect(!scheduler.RearmEventWait(waitTicket) && !scheduler.RemoveEventWait(waitTicket),
           "external-owner.stale-wait-ticket-refused");
    Expect(scheduler.HasUiWork(), "external-owner.external-notify-requests-owner-pass");
    (void)DrainCompletions(scheduler);
    Expect(state.pumps.load(std::memory_order_relaxed) == 1 && scheduler.IsIdle(),
           "external-owner.ready-pumped-and-pending-cleared");
    Expect(state.reentrantDrainIgnored.load(std::memory_order_relaxed) &&
               state.reentrantActionIgnored.load(std::memory_order_relaxed),
           "external-owner.reentrant-drain-and-actions-ignored");
    Expect(state.unregisterRefused.load(std::memory_order_relaxed) &&
               state.registerRefused.load(std::memory_order_relaxed),
           "external-owner.registration-changes-refused-inside-pump");
    Expect(scheduler.UnregisterOwnerWork(source), "external-owner.unregister-after-callback-joined");
    scheduler.Close();
}

struct SuspendedCloseState final
{
    std::atomic<unsigned int> completions{0};
    std::atomic<unsigned int> destructions{0};
};

class SuspendedCloseJob final : public Job
{
  public:
    SuspendedCloseJob(StageState& stage, SuspendedCloseState& state) noexcept : _stage(stage), _state(state) {}
    ~SuspendedCloseJob() override
    {
        _state.destructions.fetch_add(1, std::memory_order_relaxed);
    }
    JobResult Run(JobContext&) override
    {
        (void)SetEvent(_stage.entered);
        if (WaitForSingleObject(_stage.release, 10000) != WAIT_OBJECT_0)
        {
            std::printf("FAIL job-stage.suspended-close-worker-gate-timeout\n");
            std::fflush(stdout);
            std::_Exit(1);
        }
        return {};
    }
    bool DeferCompletion(JobResult, JobTicket) noexcept override
    {
        (void)SetEvent(_stage.deferred);
        return true;
    }
    void Completed(JobResult) noexcept override
    {
        _state.completions.fetch_add(1, std::memory_order_relaxed);
    }

  private:
    StageState& _stage;
    SuspendedCloseState& _state;
};

void TestCloseSuspendedDropsCallbacksAndDestroysOnce()
{
    JobScheduler scheduler;
    Expect(scheduler.Initialize(1) == S_OK, "suspended-close.initialize");
    auto entered = Event();
    auto release = Event();
    auto deferred = Event();
    StageState stage{entered.get(), release.get(), deferred.get()};
    SuspendedCloseState state;
    std::unique_ptr<Job> job = std::make_unique<SuspendedCloseJob>(stage, state);
    JobTicket ticket;
    Expect(scheduler.Submit(job, {}, ticket) == S_OK, "suspended-close.submit");
    Wait(entered.get());
    (void)DrainCompletions(scheduler);
    const HANDLE wake = static_cast<HANDLE>(scheduler.WakeHandle());
    (void)WaitForSingleObject(wake, 0);
    scheduler.NotifyMemoryChanged();
    Wait(wake);
    (void)DrainCompletions(scheduler);
    Expect(WaitForSingleObject(deferred.get(), 0) != WAIT_OBJECT_0 &&
               state.completions.load(std::memory_order_relaxed) == 0,
           "suspended-close.memory-wake-does-not-prove-suspension");
    (void)SetEvent(release.get());
    WaitForSuspension(scheduler, deferred.get());
    scheduler.Close();
    Expect(state.completions.load(std::memory_order_relaxed) == 0 &&
               state.destructions.load(std::memory_order_relaxed) == 1,
           "suspended-close.drops-callback-and-destroys-payload-once");
}

bool TightSyntheticMemory(void*, MemorySnapshot& snapshot) noexcept
{
    constexpr std::uint64_t available = kMemoryAdmissionHeadroomBytes + 100;
    snapshot = {available, available};
    return true;
}

struct MemoryApplyState final
{
    JobScheduler* scheduler = nullptr;
    Job* firstJob = nullptr;
    StageState* successor = nullptr;
    bool appliedWithReservation = false;
    bool successorNotStarted = false;
};

void CheckReservationDuringApply(void* context, Job& job, JobResult) noexcept
{
    auto& state = *static_cast<MemoryApplyState*>(context);
    if (&job != state.firstJob)
    {
        return;
    }
    state.appliedWithReservation = state.scheduler->ReservedMemoryBytes() == 100;
    state.successorNotStarted = state.successor->runs.load(std::memory_order_relaxed) == 0;
}

void TestReservationRetainedUntilFinalApplyThenWaitingJobAdmitted()
{
    JobScheduler scheduler;
    Expect(scheduler.Initialize(1, {}, {&TightSyntheticMemory, nullptr}) == S_OK, "reservation.initialize");
    auto firstEntered = Event();
    auto firstRelease = Event();
    auto firstDeferred = Event();
    StageState firstState{firstEntered.get(), firstRelease.get(), firstDeferred.get()};
    firstState.waitOnFirstRun = true;
    std::unique_ptr<Job> firstJob = std::make_unique<TwoStageJob>(firstState);
    Job* firstJobRaw = firstJob.get();
    JobTicket firstTicket;
    Expect(scheduler.Submit(firstJob, {JobClass::Background, 0, 100}, firstTicket) == S_OK, "reservation.first-submit");
    Wait(firstEntered.get());
    auto successorEntered = Event();
    StageState successorState{successorEntered.get()};
    std::unique_ptr<Job> successorJob = std::make_unique<TwoStageJob>(successorState);
    JobTicket successorTicket;
    Expect(scheduler.Submit(successorJob, {JobClass::Background, 0, 100}, successorTicket) == S_OK,
           "reservation.successor-submit-waits");
    Expect(scheduler.ReservedMemoryBytes() == 100 && successorState.runs.load(std::memory_order_relaxed) == 0,
           "reservation.successor-blocked-by-first-reservation");
    (void)DrainCompletions(scheduler);
    const HANDLE wake = static_cast<HANDLE>(scheduler.WakeHandle());
    (void)WaitForSingleObject(wake, 0);
    (void)SetEvent(firstRelease.get());
    WaitForSuspension(scheduler, firstDeferred.get());
    Expect(scheduler.ReservedMemoryBytes() == 100 && successorState.runs.load(std::memory_order_relaxed) == 0,
           "reservation.suspended-stage-retains-capacity");
    Expect(scheduler.Resume(firstTicket), "reservation.resume-first");
    MemoryApplyState apply{&scheduler, firstJobRaw, &successorState};
    Expect(SettleJobs(scheduler, 10000, nullptr, nullptr, &CheckReservationDuringApply, &apply) ==
               SettleResult::Settled,
           "reservation.resume-and-settle");
    Expect(apply.appliedWithReservation && apply.successorNotStarted,
           "reservation.retained-through-final-apply-before-successor-start");
    Expect(successorState.runs.load(std::memory_order_relaxed) == 1 &&
               WaitForSingleObject(successorEntered.get(), 0) == WAIT_OBJECT_0 && scheduler.ReservedMemoryBytes() == 0,
           "reservation.successor-admitted-after-final-apply-and-releases");
    scheduler.Close();
}

void TestOwnerSourceBorrowedLifetime()
{
    JobScheduler scheduler;
    Expect(scheduler.Initialize(1) == S_OK, "source-lifetime.initialize");
    bool destroyed = false;
    {
        OwnerPump source{scheduler};
        source.destroyed = &destroyed;
        Expect(scheduler.RegisterOwnerWork(source), "source-lifetime.register");
        Expect(scheduler.UnregisterOwnerWork(source), "source-lifetime.unregister-before-destroy");
    }
    Expect(destroyed, "source-lifetime.destroyed-after-unregister");
    OwnerPump replacement{scheduler};
    replacement.ready = true;
    Expect(scheduler.RegisterOwnerWork(replacement), "source-lifetime.replacement-register");
    scheduler.NotifyOwnerWork();
    (void)DrainCompletions(scheduler);
    Expect(replacement.pumps == 1, "source-lifetime.replacement-pumped");
    Expect(scheduler.UnregisterOwnerWork(replacement), "source-lifetime.replacement-unregister");
    scheduler.Close();
}

struct CloseState final
{
    HANDLE entered = nullptr;
    std::atomic<bool> runReturned{false};
    std::atomic<unsigned int> callbacks{0};
    std::atomic<bool> destroyedAfterRun{false};
};

class CloseJob final : public Job
{
  public:
    explicit CloseJob(CloseState& state) noexcept : _state(state) {}
    ~CloseJob() override
    {
        _state.destroyedAfterRun.store(_state.runReturned.load(std::memory_order_acquire), std::memory_order_release);
    }
    JobResult Run(JobContext& context) override
    {
        (void)SetEvent(_state.entered);
        const auto result = WaitForSingleObject(static_cast<HANDLE>(context.shutdownEvent), 10000);
        _state.runReturned.store(true, std::memory_order_release);
        return result == WAIT_OBJECT_0 ? JobResult{} : JobResult{JobStatus::Failed, E_FAIL};
    }
    void Completed(JobResult) noexcept override
    {
        _state.callbacks.fetch_add(1, std::memory_order_relaxed);
    }

  private:
    CloseState& _state;
};

void TestCloseJoinsBeforeDestroyAndDropsCallbacks()
{
    JobScheduler scheduler;
    Expect(scheduler.Initialize(1) == S_OK, "close.initialize");
    auto entered = Event();
    CloseState state{entered.get()};
    std::unique_ptr<Job> job = std::make_unique<CloseJob>(state);
    JobTicket ticket;
    Expect(scheduler.Submit(job, {}, ticket) == S_OK, "close.submit");
    Wait(entered.get());
    scheduler.Close();
    Expect(state.callbacks.load(std::memory_order_relaxed) == 0, "close.drops-completion-callback");
    Expect(state.runReturned.load(std::memory_order_acquire) && state.destroyedAfterRun.load(std::memory_order_acquire),
           "close.joins-worker-before-payload-destruction");
}
} // namespace

int RunJobStageTests()
{
    g_failures = 0;
    TestDeferResumeFifoAndReadiness();
    TestCancellationBeforeResume();
    TestFailureWithoutDeferReleasesReservation();
    TestResumeInsideDeferredHookWaitsForHookReturn();
    TestResumeThenFalseDeferFinalizesWithoutAnotherStage(false);
    TestResumeThenFalseDeferFinalizesWithoutAnotherStage(true);
    TestReusedSlotRejectsOlderGenerationResume();
    TestExternalOwnerReadinessAndCallbackReentrancy();
    TestCloseSuspendedDropsCallbacksAndDestroysOnce();
    TestReservationRetainedUntilFinalApplyThenWaitingJobAdmitted();
    TestOwnerSourceBorrowedLifetime();
    TestCloseJoinsBeforeDestroyAndDropsCallbacks();
    return g_failures;
}
