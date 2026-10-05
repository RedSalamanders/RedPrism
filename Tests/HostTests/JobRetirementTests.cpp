#include "JobRetirementTests.h"

#include "../../Common/Jobs.h"

#include <atomic>
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
int g_failures = 0;

void Expect(bool condition, const char* name) noexcept
{
    std::printf("%s job-retirement.%s\n", condition ? "PASS" : "FAIL", name);
    g_failures += condition ? 0 : 1;
}

wil::unique_handle Event(const char* name)
{
    wil::unique_handle event{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    if (!event)
    {
        std::printf("FAIL job-retirement.create-event name=%s\n", name);
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
        std::printf("FAIL job-retirement.gate-timeout name=%s wait=0x%08lx\n", name, result);
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

bool WaitForRetirement(JobScheduler& scheduler, HANDLE ready, const char* name) noexcept
{
    const ULONGLONG deadline = GetTickCount64() + 10000;
    while (WaitForSingleObject(ready, 0) != WAIT_OBJECT_0)
    {
        (void)DrainCompletions(scheduler);
        if (WaitForSingleObject(ready, 0) == WAIT_OBJECT_0)
        {
            return true;
        }
        const ULONGLONG now = GetTickCount64();
        if (now >= deadline)
        {
            std::printf("FAIL job-retirement.hook-timeout name=%s\n", name);
            std::fflush(stdout);
            std::_Exit(1);
        }
        const DWORD waitMs = static_cast<DWORD>(deadline - now);
        if (WaitForSingleObject(scheduler.WakeHandle(), waitMs) != WAIT_OBJECT_0)
        {
            std::printf("FAIL job-retirement.wake-timeout name=%s\n", name);
            std::fflush(stdout);
            std::_Exit(1);
        }
    }
    return true;
}

enum class RunMode
{
    Success,
    Failure,
    BadAlloc,
    GatedSuccess,
    ShutdownWait
};

struct State final
{
    JobScheduler* scheduler = nullptr;
    HANDLE entered = nullptr;
    HANDLE release = nullptr;
    HANDLE hookReady = nullptr;
    RunMode mode = RunMode::Success;
    JobResult gateResult{};
    unsigned int retainHooks = 0;
    bool resumeInHook = false;
    bool requestDuplicateResume = false;
    std::atomic<unsigned int> runs{0};
    std::atomic<unsigned int> runReturns{0};
    unsigned int retirementHooks = 0;
    unsigned int completionHooks = 0;
    unsigned int completions = 0;
    JobStatus hookStatus = JobStatus::Finished;
    std::int32_t hookError = 0;
    JobTicket hookTicket{};
    JobStatus completedStatus = JobStatus::Finished;
    std::int32_t completedError = 0;
    bool hookSawProducerReturned = false;
    bool retirementPrecededCompletionHook = false;
    bool resumeFromHook = false;
    bool duplicateResumeFromHook = false;
    std::atomic<DWORD> destroyedThread{0};
};

class RetirementJob final : public Job
{
  public:
    explicit RetirementJob(State& state) noexcept : _state(state) {}

    ~RetirementJob() override
    {
        _state.destroyedThread.store(GetCurrentThreadId(), std::memory_order_release);
    }

    JobResult Run(JobContext& context) override
    {
        _state.runs.fetch_add(1, std::memory_order_relaxed);
        if (_state.entered)
        {
            (void)SetEvent(_state.entered);
        }
        switch (_state.mode)
        {
        case RunMode::Success:
            _state.runReturns.fetch_add(1, std::memory_order_release);
            return {};
        case RunMode::Failure:
            _state.runReturns.fetch_add(1, std::memory_order_release);
            return {JobStatus::Failed, E_FAIL};
        case RunMode::BadAlloc:
            _state.runReturns.fetch_add(1, std::memory_order_release);
            throw std::bad_alloc{};
        case RunMode::GatedSuccess:
            Wait(_state.release, "worker-release");
            _state.runReturns.fetch_add(1, std::memory_order_release);
            return _state.gateResult;
        case RunMode::ShutdownWait:
        {
            const DWORD result = WaitForSingleObject(static_cast<HANDLE>(context.shutdownEvent), 10000);
            _state.runReturns.fetch_add(1, std::memory_order_release);
            return result == WAIT_OBJECT_0 ? JobResult{} : JobResult{JobStatus::Failed, E_FAIL};
        }
        }
        std::abort();
    }

    bool DeferRetirement(JobResult result, JobTicket ticket) noexcept override
    {
        ++_state.retirementHooks;
        _state.hookStatus = result.status;
        _state.hookError = result.error;
        _state.hookTicket = ticket;
        _state.hookSawProducerReturned =
            _state.runReturns.load(std::memory_order_acquire) == _state.runs.load(std::memory_order_relaxed);
        if (_state.resumeInHook)
        {
            _state.resumeFromHook = _state.scheduler->Resume(ticket);
            if (_state.requestDuplicateResume)
            {
                _state.duplicateResumeFromHook = _state.scheduler->Resume(ticket);
            }
        }
        const bool retain = _state.retirementHooks <= _state.retainHooks;
        if (_state.hookReady && retain)
        {
            (void)SetEvent(_state.hookReady);
        }
        return retain;
    }

    bool DeferCompletion(JobResult, JobTicket) noexcept override
    {
        ++_state.completionHooks;
        _state.retirementPrecededCompletionHook = _state.retirementHooks != 0;
        return false;
    }

    void Completed(JobResult result) noexcept override
    {
        ++_state.completions;
        _state.completedStatus = result.status;
        _state.completedError = result.error;
    }

  private:
    State& _state;
};

bool Start(JobScheduler& scheduler, const char* name) noexcept
{
    const bool initialized = scheduler.Initialize(1, {}, {&StableMemory, nullptr}) == S_OK;
    Expect(initialized, name);
    return initialized;
}

bool Submit(JobScheduler& scheduler, std::unique_ptr<Job>& job, JobOptions options, JobTicket& ticket,
            const char* name) noexcept
{
    const bool submitted = scheduler.Submit(job, options, ticket) == S_OK;
    Expect(submitted, name);
    return submitted;
}

void TestSuccessRetainedAndFinalized()
{
    JobScheduler scheduler;
    if (!Start(scheduler, "success.initialize"))
        return;
    auto hookReady = Event("success-hook");
    State state;
    state.scheduler = &scheduler;
    state.hookReady = hookReady.get();
    state.retainHooks = 1;
    std::unique_ptr<Job> job = std::make_unique<RetirementJob>(state);
    JobTicket ticket;
    if (!Submit(scheduler, job, {JobClass::Commit, 11, 4096, true}, ticket, "success.submit"))
    {
        scheduler.Close();
        return;
    }
    WaitForRetirement(scheduler, hookReady.get(), "success-first-stage");
    Expect(state.runs.load() == 1 && state.retirementHooks == 1 && state.hookStatus == JobStatus::Finished &&
               state.hookTicket == ticket && state.hookSawProducerReturned,
           "success.retained-hook-sees-returned-stage-and-original-ticket");
    Expect(state.completions == 0 && scheduler.ReservedMemoryBytes() == 4096,
           "success.external-hold-keeps-record-and-reservation");
    Expect(scheduler.Resume(ticket), "success.resume-exact-ticket");
    Expect(SettleJobs(scheduler, 10000) == SettleResult::Settled, "success.settle-after-resume");
    Expect(state.runs.load() == 2 && state.retirementHooks == 2 && state.completionHooks == 1 &&
               state.completions == 1 && state.completedStatus == JobStatus::Finished &&
               state.retirementPrecededCompletionHook,
           "success.final-stage-runs-and-completes-once");
    Expect(scheduler.ReservedMemoryBytes() == 0 && !scheduler.Resume(ticket),
           "success.finalization-releases-reservation-and-rejects-stale-ticket");
    scheduler.Close();
}

void TestReturnedFailureRetainedWithError()
{
    JobScheduler scheduler;
    if (!Start(scheduler, "failure.initialize"))
        return;
    auto hookReady = Event("failure-hook");
    State state;
    state.scheduler = &scheduler;
    state.hookReady = hookReady.get();
    state.mode = RunMode::Failure;
    state.retainHooks = 1;
    std::unique_ptr<Job> job = std::make_unique<RetirementJob>(state);
    JobTicket ticket;
    if (!Submit(scheduler, job, {JobClass::Background, 0, 512, true}, ticket, "failure.submit"))
    {
        scheduler.Close();
        return;
    }
    WaitForRetirement(scheduler, hookReady.get(), "failure-first-stage");
    Expect(state.hookStatus == JobStatus::Failed && state.hookError == E_FAIL && state.hookSawProducerReturned,
           "failure.retirement-receives-returned-error");
    Expect(state.completions == 0 && state.completionHooks == 0 && scheduler.ReservedMemoryBytes() == 512,
           "failure.hold-skips-success-hook-and-retains-reservation");
    Expect(scheduler.Resume(ticket), "failure.resume-for-terminal-acknowledgement");
    Expect(SettleJobs(scheduler, 10000) == SettleResult::Settled, "failure.settle");
    Expect(state.runs.load() == 2 && state.retirementHooks == 2 && state.completions == 1 &&
               state.completedStatus == JobStatus::Failed && state.completedError == E_FAIL,
           "failure.error-preserved-through-acknowledgement-stage");
    scheduler.Close();
}

void TestBadAllocTranslatedAndRetained()
{
    JobScheduler scheduler;
    if (!Start(scheduler, "badalloc.initialize"))
        return;
    auto hookReady = Event("badalloc-hook");
    State state;
    state.scheduler = &scheduler;
    state.hookReady = hookReady.get();
    state.mode = RunMode::BadAlloc;
    state.retainHooks = 1;
    std::unique_ptr<Job> job = std::make_unique<RetirementJob>(state);
    JobTicket ticket;
    if (!Submit(scheduler, job, {JobClass::Background, 0, 0, true}, ticket, "badalloc.submit"))
    {
        scheduler.Close();
        return;
    }
    WaitForRetirement(scheduler, hookReady.get(), "badalloc-first-stage");
    Expect(state.hookStatus == JobStatus::Failed && state.hookError == E_OUTOFMEMORY && state.hookSawProducerReturned,
           "badalloc.hook-sees-translated-out-of-memory-after-throw");
    Expect(state.completions == 0 && state.completionHooks == 0,
           "badalloc.retained-failure-skips-success-hook-and-callback");
    Expect(scheduler.Resume(ticket), "badalloc.resume");
    Expect(SettleJobs(scheduler, 10000) == SettleResult::Settled, "badalloc.settle");
    Expect(state.retirementHooks == 2 && state.completions == 1 && state.completedError == E_OUTOFMEMORY,
           "badalloc.translated-error-survives-retirement");
    scheduler.Close();
}

void TestCancelWhileWorkerGated()
{
    JobScheduler scheduler;
    if (!Start(scheduler, "gated-cancel.initialize"))
        return;
    auto entered = Event("gated-cancel-entered");
    auto release = Event("gated-cancel-release");
    auto hookReady = Event("gated-cancel-hook");
    State state;
    state.scheduler = &scheduler;
    state.entered = entered.get();
    state.release = release.get();
    state.hookReady = hookReady.get();
    state.mode = RunMode::GatedSuccess;
    state.gateResult = {};
    state.retainHooks = 1;
    std::unique_ptr<Job> job = std::make_unique<RetirementJob>(state);
    JobTicket ticket;
    if (!Submit(scheduler, job, {JobClass::Background, 0, 0, true}, ticket, "gated-cancel.submit"))
    {
        scheduler.Close();
        return;
    }
    Wait(entered.get(), "gated-cancel-entered");
    Expect(scheduler.Cancel(ticket), "gated-cancel.cancel-running-worker");
    (void)SetEvent(release.get());
    WaitForRetirement(scheduler, hookReady.get(), "gated-cancel-first-stage");
    Expect(state.hookStatus == JobStatus::Cancelled && state.hookSawProducerReturned && state.completions == 0,
           "gated-cancel-terminal-hook-runs-after-producer-returns");
    Expect(scheduler.Resume(ticket), "gated-cancel.resume-for-retirement-ack");
    Expect(SettleJobs(scheduler, 10000) == SettleResult::Settled, "gated-cancel.settle");
    Expect(state.runs.load() == 1 && state.retirementHooks == 2 && state.completions == 1 &&
               state.completedStatus == JobStatus::Cancelled,
           "gated-cancel-cancelled-stage-is-not-restarted");
    scheduler.Close();
}

void TestCancelBeforeRun()
{
    JobScheduler scheduler;
    if (!Start(scheduler, "before-run.initialize"))
        return;
    auto blockerEntered = Event("before-run-blocker");
    auto blockerRelease = Event("before-run-release");
    State blocker;
    blocker.scheduler = &scheduler;
    blocker.entered = blockerEntered.get();
    blocker.release = blockerRelease.get();
    blocker.mode = RunMode::GatedSuccess;
    std::unique_ptr<Job> blockerJob = std::make_unique<RetirementJob>(blocker);
    JobTicket blockerTicket;
    if (!Submit(scheduler, blockerJob, {}, blockerTicket, "before-run.submit-blocker"))
    {
        scheduler.Close();
        return;
    }
    Wait(blockerEntered.get(), "before-run-blocker-entered");
    auto targetHook = Event("before-run-hook");
    State target;
    target.scheduler = &scheduler;
    target.hookReady = targetHook.get();
    target.retainHooks = 1;
    std::unique_ptr<Job> targetJob = std::make_unique<RetirementJob>(target);
    JobTicket targetTicket;
    if (!Submit(scheduler, targetJob, {JobClass::Background, 0, 0, true}, targetTicket, "before-run.submit-target"))
    {
        (void)SetEvent(blockerRelease.get());
        scheduler.Close();
        return;
    }
    Expect(scheduler.Cancel(targetTicket), "before-run.cancel-queued-job");
    (void)SetEvent(blockerRelease.get());
    WaitForRetirement(scheduler, targetHook.get(), "before-run-cancel-hook");
    Expect(target.runs.load() == 0 && target.hookStatus == JobStatus::Cancelled && target.hookSawProducerReturned,
           "before-run-cancelled-job-hooks-without-running");
    Expect(scheduler.Resume(targetTicket), "before-run.resume-cancelled-ticket");
    Expect(SettleJobs(scheduler, 10000) == SettleResult::Settled, "before-run.settle");
    Expect(target.runs.load() == 0 && target.retirementHooks == 2 && target.completions == 1 &&
               target.completedStatus == JobStatus::Cancelled,
           "before-run-ack-skips-worker-and-completes-cancelled");
    scheduler.Close();
}

void TestCancelWhileExternallySuspended()
{
    JobScheduler scheduler;
    if (!Start(scheduler, "suspended-cancel.initialize"))
        return;
    auto hookReady = Event("suspended-cancel-hook");
    State state;
    state.scheduler = &scheduler;
    state.hookReady = hookReady.get();
    state.retainHooks = 1;
    std::unique_ptr<Job> job = std::make_unique<RetirementJob>(state);
    JobTicket ticket;
    if (!Submit(scheduler, job, {JobClass::Background, 0, 0, true}, ticket, "suspended-cancel.submit"))
    {
        scheduler.Close();
        return;
    }
    WaitForRetirement(scheduler, hookReady.get(), "suspended-cancel-first-stage");
    Expect(scheduler.Cancel(ticket), "suspended-cancel.cancel-held-record");
    Expect(scheduler.Resume(ticket), "suspended-cancel-resume-cancelled-record");
    Expect(!scheduler.Resume(ticket), "suspended-cancel-refuse-duplicate-resume");
    Expect(SettleJobs(scheduler, 10000) == SettleResult::Settled, "suspended-cancel.settle");
    Expect(state.runs.load() == 1 && state.retirementHooks == 2 && state.completions == 1 &&
               state.completedStatus == JobStatus::Cancelled,
           "suspended-cancel-final-hook-and-callback-preserve-cancellation");
    scheduler.Close();
}

void TestFalseHookResumeDoesNotRunAgain()
{
    JobScheduler scheduler;
    if (!Start(scheduler, "false-hook.initialize"))
        return;
    State state;
    state.scheduler = &scheduler;
    state.resumeInHook = true;
    state.requestDuplicateResume = true;
    std::unique_ptr<Job> job = std::make_unique<RetirementJob>(state);
    JobTicket ticket;
    if (!Submit(scheduler, job, {JobClass::Background, 0, 0, true}, ticket, "false-hook.submit"))
    {
        scheduler.Close();
        return;
    }
    Expect(SettleJobs(scheduler, 10000) == SettleResult::Settled, "false-hook.settle");
    Expect(state.resumeFromHook && !state.duplicateResumeFromHook,
           "false-hook-accepts-one-resume-and-refuses-duplicate");
    Expect(state.runs.load() == 1 && state.retirementHooks == 1 && state.completions == 1 &&
               state.completedStatus == JobStatus::Finished,
           "false-hook-finalizes-despite-resume-without-second-run");
    Expect(!scheduler.Resume(ticket), "false-hook-refuses-stale-ticket-after-finalization");
    scheduler.Close();
}

void TestDefaultOptOutAndLegacySuccessHook()
{
    JobScheduler scheduler;
    if (!Start(scheduler, "optout.initialize"))
        return;
    State state;
    state.scheduler = &scheduler;
    std::unique_ptr<Job> job = std::make_unique<RetirementJob>(state);
    JobTicket ticket;
    if (!Submit(scheduler, job, {}, ticket, "optout.submit-default-options"))
    {
        scheduler.Close();
        return;
    }
    Expect(SettleJobs(scheduler, 10000) == SettleResult::Settled, "optout.settle");
    Expect(state.retirementHooks == 0 && state.completionHooks == 1 && state.runs.load() == 1 && state.completions == 1,
           "optout-default-skips-new-hook-and-keeps-success-hook");
    scheduler.Close();
}

void TestDefaultOptOutFailureSkipsHooks()
{
    JobScheduler scheduler;
    if (!Start(scheduler, "optout-failure.initialize"))
        return;
    State state;
    state.scheduler = &scheduler;
    state.mode = RunMode::Failure;
    std::unique_ptr<Job> job = std::make_unique<RetirementJob>(state);
    JobTicket ticket;
    if (!Submit(scheduler, job, {}, ticket, "optout-failure.submit"))
    {
        scheduler.Close();
        return;
    }
    Expect(SettleJobs(scheduler, 10000) == SettleResult::Settled, "optout-failure.settle");
    Expect(state.retirementHooks == 0 && state.completionHooks == 0 && state.completions == 1 &&
               state.completedStatus == JobStatus::Failed && state.completedError == E_FAIL,
           "optout-failure-no-hooks-and-one-exact-failure-callback");
    scheduler.Close();
}

void TestDefaultOptOutCancellationSkipsHooks()
{
    JobScheduler scheduler;
    if (!Start(scheduler, "optout-cancel.initialize"))
        return;
    auto entered = Event("optout-cancel-entered");
    auto release = Event("optout-cancel-release");
    State state;
    state.scheduler = &scheduler;
    state.entered = entered.get();
    state.release = release.get();
    state.mode = RunMode::GatedSuccess;
    std::unique_ptr<Job> job = std::make_unique<RetirementJob>(state);
    JobTicket ticket;
    if (!Submit(scheduler, job, {}, ticket, "optout-cancel.submit"))
    {
        scheduler.Close();
        return;
    }
    Wait(entered.get(), "optout-cancel-entered");
    Expect(scheduler.Cancel(ticket), "optout-cancel.cancel-running-job");
    (void)SetEvent(release.get());
    Expect(SettleJobs(scheduler, 10000) == SettleResult::Settled, "optout-cancel.settle");
    Expect(state.retirementHooks == 0 && state.completionHooks == 0 && state.completions == 1 &&
               state.completedStatus == JobStatus::Cancelled && state.completedError == 0,
           "optout-cancel-no-hooks-and-one-exact-cancelled-callback");
    scheduler.Close();
}

void TestReusedSlotRejectsStaleResume()
{
    JobScheduler scheduler;
    if (!Start(scheduler, "reuse.initialize"))
        return;
    State retired;
    retired.scheduler = &scheduler;
    std::unique_ptr<Job> retiredJob = std::make_unique<RetirementJob>(retired);
    JobTicket retiredTicket;
    if (!Submit(scheduler, retiredJob, {}, retiredTicket, "reuse.submit-retired"))
    {
        scheduler.Close();
        return;
    }
    Expect(SettleJobs(scheduler, 10000) == SettleResult::Settled, "reuse.retire-first-job");

    auto hookReady = Event("reuse-current-hook");
    State current;
    current.scheduler = &scheduler;
    current.hookReady = hookReady.get();
    current.retainHooks = 1;
    std::unique_ptr<Job> currentJob = std::make_unique<RetirementJob>(current);
    JobTicket currentTicket;
    if (!Submit(scheduler, currentJob, {JobClass::Background, 0, 0, true}, currentTicket, "reuse.submit-current"))
    {
        scheduler.Close();
        return;
    }
    WaitForRetirement(scheduler, hookReady.get(), "reuse-current-held");
    Expect(currentTicket.slot == retiredTicket.slot && currentTicket.generation != retiredTicket.generation,
           "reuse-same-slot-advances-generation");
    Expect(!scheduler.Resume(retiredTicket) && scheduler.Resume(currentTicket),
           "reuse-reject-old-ticket-and-accept-current-exact-ticket");
    Expect(SettleJobs(scheduler, 10000) == SettleResult::Settled, "reuse.settle-current");
    Expect(current.runs.load() == 2 && current.retirementHooks == 2 && current.completions == 1 &&
               current.completedStatus == JobStatus::Finished,
           "reuse-current-ticket-finalizes-once");
    scheduler.Close();
}

void TestSerialHoldAllowsUnrelatedRunnableJob()
{
    JobScheduler scheduler;
    if (!Start(scheduler, "serial.initialize"))
        return;
    auto heldHook = Event("serial-held-hook");
    auto youngerEntered = Event("serial-younger-entered");
    auto unrelatedEntered = Event("serial-unrelated-entered");
    State held;
    held.scheduler = &scheduler;
    held.hookReady = heldHook.get();
    held.retainHooks = 1;
    std::unique_ptr<Job> heldJob = std::make_unique<RetirementJob>(held);
    JobTicket heldTicket;
    if (!Submit(scheduler, heldJob, {JobClass::Commit, 91, 2048, true}, heldTicket, "serial.submit-held"))
    {
        scheduler.Close();
        return;
    }
    WaitForRetirement(scheduler, heldHook.get(), "serial-held-first-stage");

    State younger;
    younger.scheduler = &scheduler;
    younger.entered = youngerEntered.get();
    std::unique_ptr<Job> youngerJob = std::make_unique<RetirementJob>(younger);
    JobTicket youngerTicket;
    if (!Submit(scheduler, youngerJob, {JobClass::Commit, 91, 0, true}, youngerTicket, "serial.submit-younger"))
    {
        scheduler.Close();
        return;
    }
    State unrelated;
    unrelated.scheduler = &scheduler;
    unrelated.entered = unrelatedEntered.get();
    std::unique_ptr<Job> unrelatedJob = std::make_unique<RetirementJob>(unrelated);
    JobTicket unrelatedTicket;
    if (!Submit(scheduler, unrelatedJob, {JobClass::Background}, unrelatedTicket, "serial.submit-unrelated"))
    {
        scheduler.Close();
        return;
    }
    Wait(unrelatedEntered.get(), "serial-unrelated-runs");
    Expect(younger.runs.load() == 0 && unrelated.runs.load() == 1,
           "serial-held-record-blocks-younger-but-not-unrelated-work");
    Expect(scheduler.ReservedMemoryBytes() == 2048, "serial-external-hold-keeps-original-reservation-unchanged");
    Expect(scheduler.Resume(heldTicket), "serial-resume-held-predecessor");
    Expect(SettleJobs(scheduler, 10000) == SettleResult::Settled, "serial.settle-all");
    Expect(held.completions == 1 && younger.runs.load() == 1 && younger.completions == 1 &&
               younger.entered != nullptr && WaitForSingleObject(youngerEntered.get(), 0) == WAIT_OBJECT_0,
           "serial-younger-starts-after-held-predecessor-retires");
    Expect(scheduler.ReservedMemoryBytes() == 0, "serial-all-reservations-released");
    scheduler.Close();
}

struct ShutdownState final
{
    HANDLE entered = nullptr;
    HANDLE hookEntered = nullptr;
    const DWORD ownerThread = GetCurrentThreadId();
    std::atomic<unsigned int> runs{0};
    std::atomic<unsigned int> runReturns{0};
    std::atomic<unsigned int> retirementHooks{0};
    std::atomic<unsigned int> completions{0};
    std::atomic<DWORD> destroyedThread{0};
    std::atomic<bool> destroyedAfterProducer{false};
};

class ShutdownJob final : public Job
{
  public:
    explicit ShutdownJob(ShutdownState& state) noexcept : _state(state) {}
    ~ShutdownJob() override
    {
        _state.destroyedAfterProducer.store(_state.runReturns.load(std::memory_order_acquire) ==
                                                _state.runs.load(std::memory_order_relaxed),
                                            std::memory_order_release);
        _state.destroyedThread.store(GetCurrentThreadId(), std::memory_order_release);
    }
    JobResult Run(JobContext&) override
    {
        _state.runs.fetch_add(1, std::memory_order_relaxed);
        (void)SetEvent(_state.entered);
        _state.runReturns.fetch_add(1, std::memory_order_release);
        return {};
    }
    bool DeferRetirement(JobResult, JobTicket) noexcept override
    {
        _state.retirementHooks.fetch_add(1, std::memory_order_relaxed);
        (void)SetEvent(_state.hookEntered);
        return true;
    }
    void Completed(JobResult) noexcept override
    {
        _state.completions.fetch_add(1, std::memory_order_relaxed);
    }

  private:
    ShutdownState& _state;
};

void TestShutdownJoinsAndDropsRetirementCallbacks()
{
    JobScheduler scheduler;
    if (!Start(scheduler, "shutdown.initialize"))
        return;
    auto entered = Event("shutdown-worker-entered");
    auto hookEntered = Event("shutdown-retirement-hook");
    ShutdownState state;
    state.entered = entered.get();
    state.hookEntered = hookEntered.get();
    std::unique_ptr<Job> job = std::make_unique<ShutdownJob>(state);
    JobTicket ticket;
    if (!Submit(scheduler, job, {JobClass::Background, 0, 1024, true}, ticket, "shutdown.submit"))
    {
        scheduler.Close();
        return;
    }
    Wait(entered.get(), "shutdown-worker-entered");
    WaitForRetirement(scheduler, state.hookEntered, "shutdown-retained-before-close");
    Expect(state.retirementHooks.load() == 1 && scheduler.ReservedMemoryBytes() == 1024,
           "shutdown-close-starts-with-retained-hook-and-reservation");
    scheduler.Close();
    Expect(state.runs.load() == 1 && state.runReturns.load() == 1, "shutdown-joins-worker-producer-before-cleanup");
    Expect(state.retirementHooks.load() == 1 && state.completions.load() == 0,
           "shutdown-drops-retirement-hook-and-completion-callback");
    Expect(state.destroyedAfterProducer.load(std::memory_order_acquire) &&
               state.destroyedThread.load(std::memory_order_acquire) == state.ownerThread,
           "shutdown-destroys-retained-owner-on-owner-lane-after-join");
    Expect(scheduler.ReservedMemoryBytes() == 0 && !scheduler.Resume(ticket),
           "shutdown-releases-reservation-and-refuses-stale-resume");
}
} // namespace

int RunJobRetirementTests()
{
    g_failures = 0;
    TestSuccessRetainedAndFinalized();
    TestReturnedFailureRetainedWithError();
    TestBadAllocTranslatedAndRetained();
    TestCancelWhileWorkerGated();
    TestCancelBeforeRun();
    TestCancelWhileExternallySuspended();
    TestFalseHookResumeDoesNotRunAgain();
    TestDefaultOptOutAndLegacySuccessHook();
    TestDefaultOptOutFailureSkipsHooks();
    TestDefaultOptOutCancellationSkipsHooks();
    TestReusedSlotRejectsStaleResume();
    TestSerialHoldAllowsUnrelatedRunnableJob();
    TestShutdownJoinsAndDropsRetirementCallbacks();
    return g_failures;
}
