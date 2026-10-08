#include "Host.Tests.PublicationCallback.h"

// Jobs.h is supplied by the single staged Common include root for PublicationCallbackIdentity-r1.
#include "../../Common/Jobs.h"

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <memory>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/resource.h>
#pragma warning(pop)

namespace
{
constexpr DWORD kTimeoutMs = 10000;
constexpr std::uint64_t kAvailableBytes = kMemoryAdmissionHeadroomBytes + 64ull * 1024 * 1024;
unsigned int gPassed = 0;
unsigned int gFailed = 0;

void Expect(bool condition, const char* name) noexcept
{
    std::printf("%s publication-callback.%s\n", condition ? "PASS" : "FAIL", name);
    std::fflush(stdout);
    condition ? ++gPassed : ++gFailed;
}

[[noreturn]] void SetupFailure(const char* name, DWORD error = 0) noexcept
{
    std::printf("FAIL publication-callback.setup.%s error=%lu\n", name, error);
    std::fflush(stdout);
    std::_Exit(1);
}

wil::unique_handle MakeEvent(const char* name)
{
    wil::unique_handle event{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    if (!event)
        SetupFailure(name, GetLastError());
    return event;
}

void WaitGate(HANDLE event, const char* name) noexcept
{
    const DWORD result = event ? WaitForSingleObject(event, kTimeoutMs) : WAIT_FAILED;
    if (result != WAIT_OBJECT_0)
        SetupFailure(name, result == WAIT_FAILED ? GetLastError() : result);
}

void Signal(HANDLE event, const char* name) noexcept
{
    if (!event || !SetEvent(event))
        SetupFailure(name, GetLastError());
}

bool StableMemory(void*, MemorySnapshot& snapshot) noexcept
{
    snapshot = {kAvailableBytes, kAvailableBytes};
    return true;
}

JobTicket WrongSlot(JobTicket ticket) noexcept
{
    ticket.slot = (ticket.slot + 1) % JobScheduler::kCapacity;
    return ticket;
}

JobTicket WrongGeneration(JobTicket ticket) noexcept
{
    ticket.generation = ticket.generation == (std::numeric_limits<std::uint64_t>::max)() ? 0 : ticket.generation + 1;
    return ticket;
}

struct Probe final
{
    JobScheduler* scheduler = nullptr;
    HANDLE runEntered = nullptr;
    HANDLE releaseRun = nullptr;
    HANDLE progressDelivered = nullptr;
    HANDLE retirementObserved = nullptr;
    JobTicket ticket{};
    DWORD ownerThread = 0;
    std::atomic<unsigned int> runs{0};
    std::atomic<unsigned int> runEnds{0};
    std::atomic<unsigned int> workerRetirements{0};
    std::atomic<DWORD> runThread{0};
    std::atomic<bool> cancellationObserved{false};
    unsigned int retirementCalls = 0;
    unsigned int completionHookCalls = 0;
    unsigned int completedCalls = 0;
    unsigned int progressCalls = 0;
    JobStatus retirementStatus = JobStatus::Failed;
    bool progressIdentityFalse = false;
    bool firstRetirementExact = false;
    bool firstRetirementWrongSlotFalse = false;
    bool firstRetirementWrongGenerationFalse = false;
    bool resumedRetirementExact = false;
    bool resumedRetirementWrongSlotFalse = false;
    bool resumedRetirementWrongGenerationFalse = false;
    bool completionHookExact = false;
    bool completionHookWrongSlotFalse = false;
    bool completionHookWrongGenerationFalse = false;
    bool negativeRetirementIdentityFalse = false;
    bool negativeRetirementWrongSlotFalse = false;
    bool negativeRetirementWrongGenerationFalse = false;
    bool completedIdentityFalse = false;
    bool originalTicketAtHooks = true;
    bool workerReturnedBeforeHooks = true;
};

enum class RunMode
{
    SuccessfulTwoStage,
    Failed,
    Cancelled
};

class CallbackIdentityJob final : public Job
{
  public:
    CallbackIdentityJob(Probe& probe, RunMode mode) noexcept : _probe(probe), _mode(mode) {}

    JobResult Run(JobContext& context) override
    {
        const unsigned int ordinal = _probe.runs.fetch_add(1, std::memory_order_relaxed);
        _probe.runThread.store(GetCurrentThreadId(), std::memory_order_relaxed);
        if (ordinal == 0)
        {
            if (_mode == RunMode::SuccessfulTwoStage)
                context.ReportProgress(37);
            Signal(_probe.runEntered, "worker-entered-signal");
            WaitGate(_probe.releaseRun, "worker-release-gate");
        }
        if (_mode == RunMode::Cancelled)
        {
            const bool cancelled = context.cancellation.IsCancelled();
            _probe.cancellationObserved.store(cancelled, std::memory_order_relaxed);
            _probe.runEnds.fetch_add(1, std::memory_order_release);
            return cancelled ? JobResult{JobStatus::Cancelled, 0} : JobResult{JobStatus::Failed, E_UNEXPECTED};
        }
        _probe.runEnds.fetch_add(1, std::memory_order_release);
        if (_mode == RunMode::Failed)
            return {JobStatus::Failed, E_FAIL};
        return {};
    }

    void RetireWorkerStage(JobContext&, JobResult) noexcept override
    {
        _probe.workerRetirements.fetch_add(1, std::memory_order_relaxed);
    }

    bool DeferRetirement(JobResult result, JobTicket ticket) noexcept override
    {
        ++_probe.retirementCalls;
        _probe.originalTicketAtHooks = _probe.originalTicketAtHooks && ticket == _probe.ticket;
        _probe.workerReturnedBeforeHooks =
            _probe.workerReturnedBeforeHooks &&
            _probe.runEnds.load(std::memory_order_acquire) == _probe.runs.load(std::memory_order_relaxed) &&
            _probe.runThread.load(std::memory_order_relaxed) != _probe.ownerThread;
        _probe.retirementStatus = result.status;

        if (result.status != JobStatus::Finished)
        {
            const JobTicket wrongSlot = WrongSlot(ticket);
            const JobTicket wrongGeneration = WrongGeneration(ticket);
            _probe.negativeRetirementIdentityFalse = !_probe.scheduler->InAcknowledgedJobCallback(ticket);
            _probe.negativeRetirementWrongSlotFalse = !_probe.scheduler->InAcknowledgedJobCallback(wrongSlot);
            _probe.negativeRetirementWrongGenerationFalse =
                !_probe.scheduler->InAcknowledgedJobCallback(wrongGeneration);
            Signal(_probe.retirementObserved, "negative-retirement-observed-signal");
            return false;
        }

        const bool exact = _probe.scheduler->InAcknowledgedJobCallback(ticket);
        const bool wrongSlotFalse = !_probe.scheduler->InAcknowledgedJobCallback(WrongSlot(ticket));
        const bool wrongGenerationFalse = !_probe.scheduler->InAcknowledgedJobCallback(WrongGeneration(ticket));
        if (_probe.retirementCalls == 1 && _mode == RunMode::SuccessfulTwoStage)
        {
            _probe.firstRetirementExact = exact;
            _probe.firstRetirementWrongSlotFalse = wrongSlotFalse;
            _probe.firstRetirementWrongGenerationFalse = wrongGenerationFalse;
            Signal(_probe.retirementObserved, "successful-retirement-observed-signal");
            return true;
        }
        _probe.resumedRetirementExact = exact;
        _probe.resumedRetirementWrongSlotFalse = wrongSlotFalse;
        _probe.resumedRetirementWrongGenerationFalse = wrongGenerationFalse;
        return false;
    }

    bool DeferCompletion(JobResult result, JobTicket ticket) noexcept override
    {
        ++_probe.completionHookCalls;
        _probe.originalTicketAtHooks = _probe.originalTicketAtHooks && ticket == _probe.ticket;
        _probe.workerReturnedBeforeHooks =
            _probe.workerReturnedBeforeHooks &&
            _probe.runEnds.load(std::memory_order_acquire) == _probe.runs.load(std::memory_order_relaxed) &&
            _probe.runThread.load(std::memory_order_relaxed) != _probe.ownerThread;
        _probe.completionHookExact =
            result.status == JobStatus::Finished && _probe.scheduler->InAcknowledgedJobCallback(ticket);
        _probe.completionHookWrongSlotFalse = !_probe.scheduler->InAcknowledgedJobCallback(WrongSlot(ticket));
        _probe.completionHookWrongGenerationFalse =
            !_probe.scheduler->InAcknowledgedJobCallback(WrongGeneration(ticket));
        return false;
    }

    void Completed(JobResult) noexcept override
    {
        ++_probe.completedCalls;
        _probe.completedIdentityFalse = !_probe.scheduler->InAcknowledgedJobCallback(_probe.ticket);
    }

    void Progress(unsigned int) noexcept override
    {
        ++_probe.progressCalls;
        _probe.progressIdentityFalse = !_probe.scheduler->InAcknowledgedJobCallback(_probe.ticket);
        Signal(_probe.progressDelivered, "progress-observed-signal");
    }

  private:
    Probe& _probe;
    RunMode _mode;
};

struct Fixture final
{
    wil::unique_handle runEntered = MakeEvent("run-entered-event");
    wil::unique_handle releaseRun = MakeEvent("run-release-event");
    wil::unique_handle progressDelivered = MakeEvent("progress-delivered-event");
    wil::unique_handle retirementObserved = MakeEvent("retirement-observed-event");
    Probe probe;
    JobScheduler scheduler;

    Fixture()
    {
        probe.scheduler = &scheduler;
        probe.runEntered = runEntered.get();
        probe.releaseRun = releaseRun.get();
        probe.progressDelivered = progressDelivered.get();
        probe.retirementObserved = retirementObserved.get();
        probe.ownerThread = GetCurrentThreadId();
        if (scheduler.Initialize(1, {}, {&StableMemory, nullptr}) != S_OK)
            SetupFailure("scheduler-initialize");
    }
};

void Submit(Fixture& fixture, RunMode mode)
{
    std::unique_ptr<Job> job = std::make_unique<CallbackIdentityJob>(fixture.probe, mode);
    JobOptions options{};
    options.kind = JobClass::Background;
    options.deferredRetirement = true;
    if (fixture.scheduler.Submit(job, options, fixture.probe.ticket) != S_OK || job)
        SetupFailure("scheduler-submit");
}

void PumpUntil(JobScheduler& scheduler, HANDLE observed, const char* timeoutName)
{
    const ULONGLONG deadline = GetTickCount64() + kTimeoutMs;
    while (WaitForSingleObject(observed, 0) != WAIT_OBJECT_0)
    {
        (void)DrainCompletions(scheduler);
        if (WaitForSingleObject(observed, 0) == WAIT_OBJECT_0)
            return;
        const ULONGLONG now = GetTickCount64();
        if (now >= deadline)
            SetupFailure(timeoutName, ERROR_TIMEOUT);
        if (scheduler.HasUiWork())
            continue;
        const DWORD result =
            WaitForSingleObject(static_cast<HANDLE>(scheduler.WakeHandle()), static_cast<DWORD>(deadline - now));
        if (result != WAIT_OBJECT_0)
            SetupFailure(timeoutName, result == WAIT_FAILED ? GetLastError() : result);
    }
}

void Settle(Fixture& fixture, const char* failureName)
{
    if (SettleJobs(fixture.scheduler, kTimeoutMs) != SettleResult::Settled)
        SetupFailure(failureName, ERROR_TIMEOUT);
}

void SuccessfulRetirementAndCompletionIdentity()
{
    Fixture fixture;
    Submit(fixture, RunMode::SuccessfulTwoStage);
    WaitGate(fixture.runEntered.get(), "successful-run-entered");

    Expect(!fixture.scheduler.InAcknowledgedJobCallback(fixture.probe.ticket),
           "outside-running-ticket-has-no-acknowledged-callback-identity");
    PumpUntil(fixture.scheduler, fixture.progressDelivered.get(), "progress-callback-timeout");
    Expect(fixture.probe.progressCalls == 1 && fixture.probe.progressIdentityFalse,
           "real-report-progress-callback-has-no-acknowledged-job-identity");
    Signal(fixture.releaseRun.get(), "successful-run-release-signal");
    PumpUntil(fixture.scheduler, fixture.retirementObserved.get(), "successful-retirement-timeout");
    Expect(fixture.probe.firstRetirementExact && fixture.probe.firstRetirementWrongSlotFalse &&
               fixture.probe.firstRetirementWrongGenerationFalse && fixture.probe.originalTicketAtHooks &&
               fixture.probe.workerReturnedBeforeHooks,
           "successful-defer-retirement-matches-only-original-joined-ticket");
    Expect(!fixture.scheduler.InAcknowledgedJobCallback(fixture.probe.ticket),
           "outside-suspended-record-has-no-acknowledged-callback-identity");
    Expect(fixture.scheduler.Resume(fixture.probe.ticket), "real-suspended-ticket-resumes-on-owner");
    Settle(fixture, "successful-two-stage-settle-timeout");

    Expect(fixture.probe.runs.load(std::memory_order_relaxed) == 2 &&
               fixture.probe.workerRetirements.load(std::memory_order_relaxed) == 2 &&
               fixture.probe.retirementCalls == 2 && fixture.probe.resumedRetirementExact &&
               fixture.probe.resumedRetirementWrongSlotFalse && fixture.probe.resumedRetirementWrongGenerationFalse,
           "resumed-real-worker-stage-keeps-original-ticket-identity");
    Expect(fixture.probe.completionHookCalls == 1 && fixture.probe.completionHookExact &&
               fixture.probe.completionHookWrongSlotFalse && fixture.probe.completionHookWrongGenerationFalse,
           "successful-defer-completion-matches-only-original-ticket");
    Expect(fixture.probe.completedCalls == 1 && fixture.probe.completedIdentityFalse &&
               !fixture.scheduler.InAcknowledgedJobCallback(fixture.probe.ticket),
           "completed-and-post-final-ticket-have-no-acknowledged-callback-identity");
    fixture.scheduler.Close();
}

void FailedRetirementHasNoAcknowledgedIdentity()
{
    Fixture fixture;
    Submit(fixture, RunMode::Failed);
    WaitGate(fixture.runEntered.get(), "failed-run-entered");
    Expect(!fixture.scheduler.InAcknowledgedJobCallback(fixture.probe.ticket),
           "failed-running-ticket-outside-hook-is-not-acknowledged");
    Signal(fixture.releaseRun.get(), "failed-run-release-signal");
    PumpUntil(fixture.scheduler, fixture.retirementObserved.get(), "failed-retirement-timeout");
    Settle(fixture, "failed-job-settle-timeout");

    Expect(fixture.probe.runs.load(std::memory_order_relaxed) == 1 && fixture.probe.retirementCalls == 1 &&
               fixture.probe.retirementStatus == JobStatus::Failed && fixture.probe.negativeRetirementIdentityFalse &&
               fixture.probe.negativeRetirementWrongSlotFalse && fixture.probe.negativeRetirementWrongGenerationFalse &&
               fixture.probe.workerReturnedBeforeHooks,
           "failed-defer-retirement-and-wrong-tickets-have-no-acknowledged-identity");
    Expect(fixture.probe.completionHookCalls == 0 && fixture.probe.completedCalls == 1 &&
               fixture.probe.completedIdentityFalse &&
               !fixture.scheduler.InAcknowledgedJobCallback(fixture.probe.ticket),
           "failed-completed-callback-and-final-ticket-have-no-acknowledged-identity");
    fixture.scheduler.Close();
}

void CancelledRetirementHasNoAcknowledgedIdentity()
{
    Fixture fixture;
    Submit(fixture, RunMode::Cancelled);
    WaitGate(fixture.runEntered.get(), "cancelled-run-entered");
    Expect(!fixture.scheduler.InAcknowledgedJobCallback(fixture.probe.ticket),
           "cancelled-running-ticket-outside-hook-is-not-acknowledged");
    Expect(fixture.scheduler.Cancel(fixture.probe.ticket), "actual-running-ticket-cancellation-accepted");
    Signal(fixture.releaseRun.get(), "cancelled-run-release-signal");
    PumpUntil(fixture.scheduler, fixture.retirementObserved.get(), "cancelled-retirement-timeout");
    Settle(fixture, "cancelled-job-settle-timeout");

    Expect(fixture.probe.cancellationObserved.load(std::memory_order_relaxed) &&
               fixture.probe.runs.load(std::memory_order_relaxed) == 1 && fixture.probe.retirementCalls == 1 &&
               fixture.probe.retirementStatus == JobStatus::Cancelled &&
               fixture.probe.negativeRetirementIdentityFalse && fixture.probe.negativeRetirementWrongSlotFalse &&
               fixture.probe.negativeRetirementWrongGenerationFalse && fixture.probe.workerReturnedBeforeHooks,
           "actual-cancelled-defer-retirement-has-no-acknowledged-identity");
    Expect(fixture.probe.completionHookCalls == 0 && fixture.probe.completedCalls == 1 &&
               fixture.probe.completedIdentityFalse &&
               !fixture.scheduler.InAcknowledgedJobCallback(fixture.probe.ticket),
           "cancelled-completed-callback-and-final-ticket-have-no-acknowledged-identity");
    fixture.scheduler.Close();
}
} // namespace

int RunPublicationCallbackTests()
{
    SuccessfulRetirementAndCompletionIdentity();
    FailedRetirementHasNoAcknowledgedIdentity();
    CancelledRetirementHasNoAcknowledgedIdentity();
    std::printf("SUMMARY publication-callback passed=%u failed=%u controls=%u\n", gPassed, gFailed, gPassed + gFailed);
    std::fflush(stdout);
    return gFailed == 0 ? 0 : 1;
}
