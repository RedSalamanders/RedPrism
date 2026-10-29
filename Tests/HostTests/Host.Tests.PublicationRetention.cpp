#include "Host.Tests.PublicationRetention.h"

#include "Common/Jobs.h"
#include "Common/SharedPublicationMemory.h"

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <memory>
#include <optional>
#include <thread>
#include <utility>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/resource.h>
#pragma warning(pop)

namespace PublicationBoundary
{
int RunSharedPublicationMemoryBoundaryProbe();
int RunReconstructedSchedulerEndpointProbe();
} // namespace PublicationBoundary
namespace PublicationOwner
{
int RunPublicationOwnerTests();
}

namespace
{
constexpr DWORD kTimeoutMs = 10000;
constexpr std::uint64_t kAdmissionBytes = 1024;
constexpr std::uint64_t kWideAvailable = kMemoryAdmissionHeadroomBytes + 1024 * 1024;
unsigned int gPassed = 0;
unsigned int gFailed = 0;

void Expect(bool condition, const char* name) noexcept
{
    std::printf("%s publication-retention.%s\n", condition ? "PASS" : "FAIL", name);
    std::fflush(stdout);
    condition ? ++gPassed : ++gFailed;
}

[[noreturn]] void SetupFailure(const char* name, DWORD error = 0) noexcept
{
    std::printf("FAIL publication-retention.setup.%s error=%lu\n", name, error);
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

void Wait(HANDLE event, const char* name) noexcept
{
    if (!event)
        SetupFailure(name, ERROR_INVALID_HANDLE);
    const DWORD result = WaitForSingleObject(event, kTimeoutMs);
    if (result != WAIT_OBJECT_0)
        SetupFailure(name, result == WAIT_FAILED ? GetLastError() : result);
}

// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027)
struct MemorySourceState final
{
    std::atomic<unsigned int> calls{0};
};
#pragma warning(pop)

bool SampleMemory(void* context, MemorySnapshot& snapshot) noexcept
{
    static_cast<MemorySourceState*>(context)->calls.fetch_add(1, std::memory_order_relaxed);
    snapshot = {kWideAvailable, kWideAvailable};
    return true;
}

void Start(JobScheduler& scheduler, MemorySourceState& source)
{
    const std::int32_t result = scheduler.Initialize(1, {}, {&SampleMemory, &source});
    if (result != S_OK)
        SetupFailure("initialize", static_cast<DWORD>(result));
}

JobOptions Options(bool deferredRetirement = true, bool reserveRetainedMemory = true) noexcept
{
    JobOptions options{};
    options.kind = JobClass::Stroke;
    options.memoryBytes = kAdmissionBytes;
    options.deferredRetirement = deferredRetirement;
    options.reserveRetainedMemory = reserveRetainedMemory;
    return options;
}

enum class PublicationMode
{
    TransferAndResume,
    TransferAndHold,
    AttemptOnly
};

// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4625 4626)
struct PublicationState final
{
    JobScheduler* scheduler = nullptr;
    MemorySourceState* source = nullptr;
    JobTicket ticket{};
    SharedRetainedMemoryLease lease;
    SharedRetainedMemoryLease wrongTicketOutput;
    SharedRetainedMemoryLease secondOutput;
    SharedRetainedMemoryLease outsideOutput;
    SharedRetainedMemoryLease legacyOutput;
    PublicationMode mode = PublicationMode::AttemptOnly;
    std::int32_t publicationResult = E_UNEXPECTED;
    std::int32_t legacyResult = E_UNEXPECTED;
    std::int32_t wrongTicketResult = E_UNEXPECTED;
    std::int32_t occupiedOutputResult = E_UNEXPECTED;
    std::int32_t secondTransferResult = E_UNEXPECTED;
    std::int32_t outsideResult = E_UNEXPECTED;
    std::uint64_t chargeBeforeTransfer = 0;
    std::uint64_t chargeAfterRefusals = 0;
    std::uint64_t chargeAfterTransfer = 0;
    std::uint64_t finalCallbackCharge = 0;
    unsigned int samplerCallsBeforeTransfer = 0;
    unsigned int samplerCallsAfterTransfer = 0;
    unsigned int runCount = 0;
    unsigned int retireWorkerStageCount = 0;
    unsigned int deferRetirementCount = 0;
    unsigned int deferCompletionCount = 0;
    unsigned int completionCount = 0;
    bool resumeAccepted = false;
    bool sameTicketAcrossStages = true;
    JobResult completedResult{JobStatus::Failed};
};
#pragma warning(pop)

// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4626 5027)
class PublicationJob final : public Job
{
  public:
    explicit PublicationJob(PublicationState& state) noexcept : _state(state) {}

    JobResult Run(JobContext&) override
    {
        ++_state.runCount;
        return {};
    }

    void RetireWorkerStage(JobContext&, JobResult) noexcept override
    {
        ++_state.retireWorkerStageCount;
    }

    bool DeferRetirement(JobResult, JobTicket ticket) noexcept override
    {
        ++_state.deferRetirementCount;
        _state.sameTicketAcrossStages = _state.sameTicketAcrossStages && ticket == _state.ticket;
        return false;
    }

    bool DeferCompletion(JobResult, JobTicket ticket) noexcept override
    {
        ++_state.deferCompletionCount;
        _state.sameTicketAcrossStages = _state.sameTicketAcrossStages && ticket == _state.ticket;
        if (_state.deferCompletionCount > 1)
            return false;
        _state.chargeBeforeTransfer = _state.scheduler->ReservedMemoryBytes();
        _state.samplerCallsBeforeTransfer = _state.source->calls.load(std::memory_order_relaxed);

        // The unchanged Completed-only overload is an independent control for the new API.
        _state.legacyResult = _state.scheduler->RetainMemory(ticket, _state.legacyOutput);
        JobTicket wrong = ticket;
        ++wrong.generation;
        _state.wrongTicketResult = _state.scheduler->RetainPublicationMemory(wrong, _state.wrongTicketOutput);
        _state.chargeAfterRefusals = _state.scheduler->ReservedMemoryBytes();
        _state.publicationResult = _state.scheduler->RetainPublicationMemory(ticket, _state.lease);
        _state.samplerCallsAfterTransfer = _state.source->calls.load(std::memory_order_relaxed);
        _state.chargeAfterTransfer = _state.scheduler->ReservedMemoryBytes();

        if (_state.publicationResult == S_OK)
        {
            _state.occupiedOutputResult = _state.scheduler->RetainPublicationMemory(ticket, _state.lease);
            _state.secondTransferResult = _state.scheduler->RetainPublicationMemory(ticket, _state.secondOutput);
            if (_state.mode == PublicationMode::TransferAndResume && _state.deferCompletionCount == 1)
                _state.resumeAccepted = _state.scheduler->Resume(ticket);
        }

        if (_state.mode == PublicationMode::AttemptOnly)
            return false;
        return _state.deferCompletionCount == 1;
    }

    void Completed(JobResult result) noexcept override
    {
        ++_state.completionCount;
        _state.completedResult = result;
        _state.finalCallbackCharge = _state.scheduler->ReservedMemoryBytes();
    }

  private:
    PublicationState& _state;
};
#pragma warning(pop)

void WaitForPublication(JobScheduler& scheduler, PublicationState& state)
{
    const ULONGLONG deadline = GetTickCount64() + kTimeoutMs;
    while (state.deferCompletionCount == 0)
    {
        (void)DrainCompletions(scheduler);
        if (state.deferCompletionCount != 0)
            return;
        const auto now = GetTickCount64();
        if (now >= deadline)
            SetupFailure("publication-acknowledgement-deadline", ERROR_TIMEOUT);
        if (scheduler.HasUiWork())
            continue;
        if (WaitForSingleObject(static_cast<HANDLE>(scheduler.WakeHandle()), static_cast<DWORD>(deadline - now)) !=
            WAIT_OBJECT_0)
            SetupFailure("publication-acknowledgement-wake");
    }
}

void Submit(JobScheduler& scheduler, std::unique_ptr<Job>& job, JobOptions options, JobTicket& ticket, const char* name)
{
    const std::int32_t result = scheduler.Submit(job, options, ticket);
    if (result != S_OK || job)
        SetupFailure(name, static_cast<DWORD>(result));
}

void TestExactTransferResumeAndForeignDrain()
{
    JobScheduler scheduler;
    MemorySourceState source;
    Start(scheduler, source);

    PublicationState state;
    state.scheduler = &scheduler;
    state.source = &source;
    state.mode = PublicationMode::TransferAndResume;
    std::unique_ptr<Job> job = std::make_unique<PublicationJob>(state);
    Submit(scheduler, job, Options(), state.ticket, "successful-submit");
    if (SettleJobs(scheduler, kTimeoutMs) != SettleResult::Settled)
        SetupFailure("successful-settle");

    Expect(state.completedResult.status == JobStatus::Finished && state.completionCount == 1 &&
               state.deferCompletionCount == 2 && state.deferRetirementCount == 2 && state.runCount == 2 &&
               state.retireWorkerStageCount == 2 && state.resumeAccepted && state.sameTicketAcrossStages,
           "resume-keeps-original-ticket-through-second-worker-retirement-and-final-callback");
    Expect(state.publicationResult == S_OK && state.lease && state.lease.Bytes() == kAdmissionBytes &&
               state.chargeBeforeTransfer == kAdmissionBytes && state.chargeAfterTransfer == kAdmissionBytes &&
               state.chargeAfterRefusals == kAdmissionBytes && state.finalCallbackCharge == kAdmissionBytes &&
               scheduler.ReservedMemoryBytes() == kAdmissionBytes,
           "successful-publication-transfers-whole-1024-charge-through-final-callback");
    Expect(state.samplerCallsBeforeTransfer == state.samplerCallsAfterTransfer && state.legacyResult == E_INVALIDARG &&
               state.wrongTicketResult == E_INVALIDARG && state.wrongTicketOutput.Bytes() == 0 &&
               state.occupiedOutputResult == E_INVALIDARG && state.lease.Bytes() == kAdmissionBytes &&
               state.secondTransferResult == E_INVALIDARG && state.secondOutput.Bytes() == 0 &&
               scheduler.ReservedMemoryBytes() == kAdmissionBytes,
           "callback-refusals-preserve-output-and-transfer-does-not-resample");

    state.outsideResult = scheduler.RetainPublicationMemory(state.ticket, state.outsideOutput);
    Expect(state.outsideResult == E_INVALIDARG && !state.outsideOutput &&
               scheduler.ReservedMemoryBytes() == kAdmissionBytes,
           "outside-callback-refusal-preserves-output-and-original-charge");

    auto released = MakeEvent("foreign-release-event");
    std::uint64_t foreignResidentBeforeReset = 0;
    std::thread foreign(
        [lease = std::move(state.lease), done = released.get(), &foreignResidentBeforeReset]() mutable noexcept
        {
            foreignResidentBeforeReset = lease.ResidentBytes();
            lease.Reset();
            if (!SetEvent(done))
                SetupFailure("foreign-release-set", GetLastError());
        });
    Wait(released.get(), "foreign-release-wait");
    foreign.join();
    Expect(foreignResidentBeforeReset == kAdmissionBytes && scheduler.ReservedMemoryBytes() == kAdmissionBytes,
           "foreign-final-lease-release-remains-charged-before-owner-drain");
    (void)DrainCompletions(scheduler);
    Expect(scheduler.ReservedMemoryBytes() == 0 && scheduler.IsIdle(),
           "owner-drain-acknowledges-foreign-final-lease-release");
    scheduler.Close();
}

void TestRefusalPrerequisites()
{
    constexpr auto invalidState = static_cast<std::int32_t>(HRESULT_FROM_WIN32(ERROR_INVALID_STATE));
    for (const bool deferredRetirement : {false, true})
    {
        JobScheduler scheduler;
        MemorySourceState source;
        Start(scheduler, source);
        PublicationState state;
        state.scheduler = &scheduler;
        state.source = &source;
        state.mode = PublicationMode::AttemptOnly;
        std::unique_ptr<Job> job = std::make_unique<PublicationJob>(state);
        Submit(scheduler, job, Options(deferredRetirement, false), state.ticket, "unreserved-submit");
        if (SettleJobs(scheduler, kTimeoutMs) != SettleResult::Settled)
            SetupFailure("unreserved-settle");
        Expect(state.publicationResult == invalidState && !state.lease &&
                   state.chargeBeforeTransfer == kAdmissionBytes && state.chargeAfterTransfer == kAdmissionBytes &&
                   scheduler.ReservedMemoryBytes() == 0,
               deferredRetirement ? "preclaim-required-for-publication-transfer"
                                  : "preclaim-and-deferred-retirement-required-for-publication-transfer");
        scheduler.Close();
    }

    JobScheduler scheduler;
    MemorySourceState source;
    Start(scheduler, source);
    PublicationState state;
    state.scheduler = &scheduler;
    state.source = &source;
    state.mode = PublicationMode::AttemptOnly;
    std::unique_ptr<Job> job = std::make_unique<PublicationJob>(state);
    Submit(scheduler, job, Options(false, true), state.ticket, "nondeferred-preclaimed-submit");
    if (SettleJobs(scheduler, kTimeoutMs) != SettleResult::Settled)
        SetupFailure("nondeferred-preclaimed-settle");
    Expect(state.publicationResult == invalidState && !state.lease && state.chargeBeforeTransfer == kAdmissionBytes &&
               state.chargeAfterTransfer == kAdmissionBytes && scheduler.ReservedMemoryBytes() == 0,
           "deferred-retirement-required-even-with-preclaimed-slot");
    scheduler.Close();
}

// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4625 4626)
struct TerminalState final
{
    JobScheduler* scheduler = nullptr;
    JobTicket ticket{};
    SharedRetainedMemoryLease attemptedLease;
    std::uint64_t callbackCharge = 0;
    std::int32_t outsidePublicationResult = E_UNEXPECTED;
    unsigned int retireCount = 0;
    unsigned int deferRetirementCount = 0;
    unsigned int deferCompletionCount = 0;
    JobResult deferRetirementResult{JobStatus::Finished};
    JobResult completedResult{JobStatus::Finished};
    unsigned int completedCount = 0;
};
#pragma warning(pop)

// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4626 5027)
class TerminalJob final : public Job
{
  public:
    TerminalJob(TerminalState& state, HANDLE entered = nullptr, HANDLE release = nullptr,
                JobStatus runStatus = JobStatus::Failed) noexcept
        : _state(state), _entered(entered), _release(release), _runStatus(runStatus)
    {
    }
    JobResult Run(JobContext&) override
    {
        if (_entered)
        {
            if (!SetEvent(_entered) || WaitForSingleObject(_release, kTimeoutMs) != WAIT_OBJECT_0)
                return {JobStatus::Failed, E_ABORT};
        }
        return {_runStatus, _runStatus == JobStatus::Failed ? E_FAIL : 0};
    }
    void RetireWorkerStage(JobContext&, JobResult) noexcept override
    {
        ++_state.retireCount;
    }
    bool DeferRetirement(JobResult result, JobTicket ticket) noexcept override
    {
        _state.ticket = ticket;
        _state.deferRetirementResult = result;
        ++_state.deferRetirementCount;
        return false;
    }
    bool DeferCompletion(JobResult, JobTicket) noexcept override
    {
        ++_state.deferCompletionCount;
        return false;
    }
    void Completed(JobResult result) noexcept override
    {
        ++_state.completedCount;
        _state.completedResult = result;
        _state.callbackCharge = _state.scheduler->ReservedMemoryBytes();
        _state.outsidePublicationResult =
            _state.scheduler->RetainPublicationMemory(_state.ticket, _state.attemptedLease);
    }

  private:
    TerminalState& _state;
    HANDLE _entered;
    HANDLE _release;
    JobStatus _runStatus;
};
#pragma warning(pop)

void TestFailedAndCancelledStagesSkipPublication()
{
    {
        JobScheduler scheduler;
        MemorySourceState source;
        Start(scheduler, source);
        TerminalState state;
        state.scheduler = &scheduler;
        std::unique_ptr<Job> job = std::make_unique<TerminalJob>(state);
        Submit(scheduler, job, Options(), state.ticket, "failed-stage-submit");
        if (SettleJobs(scheduler, kTimeoutMs) != SettleResult::Settled)
            SetupFailure("failed-stage-settle");
        Expect(state.completedResult.status == JobStatus::Failed &&
                   state.deferRetirementResult.status == JobStatus::Failed && state.retireCount == 1 &&
                   state.deferRetirementCount == 1 && state.deferCompletionCount == 0 && state.completedCount == 1 &&
                   state.outsidePublicationResult == E_INVALIDARG && !state.attemptedLease &&
                   state.callbackCharge == kAdmissionBytes && scheduler.ReservedMemoryBytes() == 0,
               "failed-stage-skips-publication-hook-and-refunds-untransferred-admission");
        scheduler.Close();
    }

    {
        JobScheduler scheduler;
        MemorySourceState source;
        Start(scheduler, source);
        auto entered = MakeEvent("cancel-stage-entered");
        auto release = MakeEvent("cancel-stage-release");
        TerminalState state;
        state.scheduler = &scheduler;
        std::unique_ptr<Job> job =
            std::make_unique<TerminalJob>(state, entered.get(), release.get(), JobStatus::Finished);
        Submit(scheduler, job, Options(), state.ticket, "cancel-stage-submit");
        Wait(entered.get(), "cancel-stage-run-entered");
        const bool cancelled = scheduler.Cancel(state.ticket);
        if (!SetEvent(release.get()))
            SetupFailure("cancel-stage-release-set", GetLastError());
        if (SettleJobs(scheduler, kTimeoutMs) != SettleResult::Settled)
            SetupFailure("cancel-stage-settle");
        Expect(cancelled && state.completedResult.status == JobStatus::Cancelled &&
                   state.deferRetirementResult.status == JobStatus::Cancelled && state.retireCount == 1 &&
                   state.deferRetirementCount == 1 && state.deferCompletionCount == 0 && state.completedCount == 1 &&
                   state.outsidePublicationResult == E_INVALIDARG && !state.attemptedLease &&
                   state.callbackCharge == kAdmissionBytes && scheduler.ReservedMemoryBytes() == 0,
               "cancelled-stage-skips-publication-hook-and-refunds-untransferred-admission");
        scheduler.Close();
    }
}

void TestCancellationAfterTransferKeepsChargeUntilLeaseRetirement()
{
    JobScheduler scheduler;
    MemorySourceState source;
    Start(scheduler, source);
    PublicationState state;
    state.scheduler = &scheduler;
    state.source = &source;
    state.mode = PublicationMode::TransferAndHold;
    std::unique_ptr<Job> job = std::make_unique<PublicationJob>(state);
    Submit(scheduler, job, Options(), state.ticket, "post-transfer-cancel-submit");
    WaitForPublication(scheduler, state);
    if (!state.lease || state.deferCompletionCount != 1)
        SetupFailure("post-transfer-cancel-publication-not-suspended");

    const bool cancelled = scheduler.Cancel(state.ticket);
    const bool resumed = scheduler.Resume(state.ticket);
    if (!cancelled || !resumed || SettleJobs(scheduler, kTimeoutMs) != SettleResult::Settled)
        SetupFailure("post-transfer-cancel-resume-settle");
    Expect(state.completedResult.status == JobStatus::Cancelled && state.completionCount == 1 && state.runCount == 1 &&
               state.retireWorkerStageCount == 2 && state.deferCompletionCount == 1 &&
               state.finalCallbackCharge == kAdmissionBytes && scheduler.ReservedMemoryBytes() == kAdmissionBytes,
           "cancel-after-transfer-runs-only-retirement-stage-and-retains-original-1024-charge");
    state.lease.Reset();
    Expect(scheduler.ReservedMemoryBytes() == kAdmissionBytes,
           "owner-final-lease-reset-stays-charged-until-acknowledgement-drain");
    (void)DrainCompletions(scheduler);
    Expect(scheduler.ReservedMemoryBytes() == 0,
           "cancelled-publication-charge-returns-after-independent-lease-retirement");
    scheduler.Close();
}

// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027)
struct CloseState final
{
    std::atomic<unsigned int> ownerHookCount{0};
    std::atomic<unsigned int> workerHookCount{0};
};
#pragma warning(pop)

void CloseOwnerHook(void* context) noexcept
{
    static_cast<CloseState*>(context)->ownerHookCount.fetch_add(1, std::memory_order_relaxed);
}

void CloseWorkerHook(void* context) noexcept
{
    static_cast<CloseState*>(context)->workerHookCount.fetch_add(1, std::memory_order_relaxed);
}

// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027)
struct CloseWorkerState final
{
    HANDLE entered = nullptr;
    std::atomic<unsigned int> completedCount{0};
    std::atomic<unsigned int> retiredCount{0};
};
#pragma warning(pop)

// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4626 5027)
class CloseWorkerJob final : public Job
{
  public:
    explicit CloseWorkerJob(CloseWorkerState& state) noexcept : _state(state) {}
    JobResult Run(JobContext& context) override
    {
        if (!SetEvent(_state.entered))
            return {JobStatus::Failed, HRESULT_FROM_WIN32(GetLastError())};
        const DWORD waited = WaitForSingleObject(static_cast<HANDLE>(context.shutdownEvent), kTimeoutMs);
        return waited == WAIT_OBJECT_0 ? JobResult{} : JobResult{JobStatus::Failed, E_ABORT};
    }
    void RetireWorkerStage(JobContext&, JobResult) noexcept override
    {
        _state.retiredCount.fetch_add(1, std::memory_order_relaxed);
    }
    void Completed(JobResult) noexcept override
    {
        _state.completedCount.fetch_add(1, std::memory_order_relaxed);
    }

  private:
    CloseWorkerState& _state;
};
#pragma warning(pop)

void TestCloseDropsCallbacksAndPreservesForeignEndpoint()
{
    JobScheduler scheduler;
    MemorySourceState source;
    Start(scheduler, source);
    PublicationState publication;
    publication.scheduler = &scheduler;
    publication.source = &source;
    publication.mode = PublicationMode::TransferAndHold;
    std::unique_ptr<Job> publicationJob = std::make_unique<PublicationJob>(publication);
    Submit(scheduler, publicationJob, Options(), publication.ticket, "close-publication-submit");
    WaitForPublication(scheduler, publication);
    if (!publication.lease || publication.deferCompletionCount != 1)
        SetupFailure("close-publication-not-suspended");

    auto foreignMayRelease = MakeEvent("close-foreign-release-gate");
    auto foreignDone = MakeEvent("close-foreign-release-done");
    std::uint64_t residentAfterClose = 0;
    std::thread foreign(
        [lease = std::move(publication.lease), gate = foreignMayRelease.get(), done = foreignDone.get(),
         &residentAfterClose]() mutable noexcept
        {
            const DWORD waited = WaitForSingleObject(gate, kTimeoutMs);
            if (waited != WAIT_OBJECT_0)
                SetupFailure("close-foreign-gate-wait", waited == WAIT_FAILED ? GetLastError() : waited);
            residentAfterClose = lease.ResidentBytes();
            lease.Reset();
            if (!SetEvent(done))
                SetupFailure("close-foreign-done-set", GetLastError());
        });

    auto workerEntered = MakeEvent("close-active-worker-entered");
    CloseWorkerState workerState;
    workerState.entered = workerEntered.get();
    std::unique_ptr<Job> activeJob = std::make_unique<CloseWorkerJob>(workerState);
    JobTicket activeTicket{};
    JobOptions activeOptions = Options(true, false);
    activeOptions.kind = JobClass::Background;
    Submit(scheduler, activeJob, activeOptions, activeTicket, "close-active-worker-submit");
    Wait(workerEntered.get(), "close-active-worker-wait");

    CloseState closeState;
    scheduler.Close({&CloseOwnerHook, &CloseWorkerHook, &closeState});
    if (!SetEvent(foreignMayRelease.get()))
        SetupFailure("close-foreign-gate-set", GetLastError());
    Wait(foreignDone.get(), "close-foreign-release-wait");
    foreign.join();

    Expect(publication.completionCount == 0 && workerState.completedCount.load(std::memory_order_relaxed) == 0,
           "close-suppresses-final-callbacks-for-suspended-and-joined-jobs");
    Expect(closeState.ownerHookCount.load(std::memory_order_relaxed) == 1 &&
               closeState.workerHookCount.load(std::memory_order_relaxed) == 1 &&
               workerState.retiredCount.load(std::memory_order_relaxed) == 1,
           "close-runs-owner-cleanup-and-final-worker-retirement-after-active-worker-joins");
    Expect(residentAfterClose == kAdmissionBytes && scheduler.ReservedMemoryBytes() == 0,
           "foreign-shared-endpoint-retains-resident-1024-bytes-across-actual-close");
}
} // namespace

int RunPublicationRetentionTests()
{
    gPassed = 0;
    gFailed = 0;
    TestExactTransferResumeAndForeignDrain();
    TestRefusalPrerequisites();
    TestFailedAndCancelledStagesSkipPublication();
    TestCancellationAfterTransferKeepsChargeUntilLeaseRetirement();
    TestCloseDropsCallbacksAndPreservesForeignEndpoint();
    std::printf("%s publication-retention.summary passed=%u failed=%u\n", gFailed == 0 ? "PASS" : "FAIL", gPassed,
                gFailed);
    const int boundaryFailures = PublicationBoundary::RunSharedPublicationMemoryBoundaryProbe();
    const int ownerFailures = PublicationOwner::RunPublicationOwnerTests();
    const int endpointFailures = PublicationBoundary::RunReconstructedSchedulerEndpointProbe();
    return static_cast<int>(gFailed) + boundaryFailures + ownerFailures + endpointFailures;
}

namespace PublicationBoundary
{
namespace
{
unsigned int gPassed = 0;
unsigned int gFailed = 0;
constexpr std::uint32_t kTimeoutMs = 10000;
constexpr std::uint64_t kExtraAdmission = 1024 * 1024;

struct MemoryState final
{
    std::shared_ptr<SharedPublicationMemory> publication;
    std::uint64_t admittedBytes = 0;
    bool createdUnbound = false;
    std::int32_t bindResult = E_UNEXPECTED;
    unsigned int completionCount = 0;
    std::uint64_t callbackCharge = 0;
};

bool SampleMemory(void*, MemorySnapshot& snapshot) noexcept
{
    snapshot = {kMemoryAdmissionHeadroomBytes + kExtraAdmission, kMemoryAdmissionHeadroomBytes + kExtraAdmission};
    return true;
}

// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4626 5027)
class PublicationBoundaryJob final : public Job
{
  public:
    PublicationBoundaryJob(JobScheduler& scheduler, MemoryState& state) noexcept : _scheduler(scheduler), _state(state)
    {
    }

    JobResult Run(JobContext&) override
    {
        _state.publication = SharedPublicationMemory::CreateForWork();
        _state.createdUnbound =
            _state.publication && !_state.publication->IsBound() && _state.publication->Bytes() == 0;
        return _state.publication ? JobResult{} : JobResult{JobStatus::Failed, E_OUTOFMEMORY};
    }

    bool DeferCompletion(JobResult, JobTicket ticket) noexcept override
    {
        _state.bindResult =
            _state.publication ? _state.publication->BindForPublication(_scheduler, ticket) : E_UNEXPECTED;
        return false;
    }

    void Completed(JobResult) noexcept override
    {
        ++_state.completionCount;
        _state.callbackCharge = _scheduler.ReservedMemoryBytes();
    }

  private:
    JobScheduler& _scheduler;
    MemoryState& _state;
};
#pragma warning(pop)

void Expect(bool condition, const char* name) noexcept
{
    std::printf("%s publication-retention.shared-memory.%s\n", condition ? "PASS" : "FAIL", name);
    condition ? ++gPassed : ++gFailed;
    std::fflush(stdout);
}
} // namespace

int RunSharedPublicationMemoryBoundaryProbe()
{
    JobScheduler scheduler;
    MemoryState state;
    state.admittedBytes = SharedPublicationMemory::OwnershipAllowance();
    if (state.admittedBytes == 0 || scheduler.Initialize(1, {}, {&SampleMemory, nullptr}) != S_OK)
    {
        std::printf("FAIL publication-retention.shared-memory.initialize\n");
        return 1;
    }

    auto job = std::make_unique<PublicationBoundaryJob>(scheduler, state);
    std::unique_ptr<Job> pending = std::move(job);
    JobOptions options{};
    options.kind = JobClass::Background;
    options.memoryBytes = state.admittedBytes;
    options.deferredRetirement = true;
    options.reserveRetainedMemory = true;
    JobTicket ticket{};
    if (scheduler.Submit(pending, options, ticket) != S_OK || pending ||
        SettleJobs(scheduler, kTimeoutMs) != SettleResult::Settled)
    {
        std::printf("FAIL publication-retention.shared-memory.settle\n");
        scheduler.Close();
        return 1;
    }

    const bool bound =
        state.publication && state.publication->IsBound() && state.publication->Bytes() == state.admittedBytes;
    const bool retained =
        scheduler.ReservedMemoryBytes() == state.admittedBytes && state.callbackCharge == state.admittedBytes;
    Expect(state.createdUnbound, "worker-allocation-starts-unbound");
    Expect(state.bindResult == S_OK && bound, "successful-defer-completion-binds-original-publication-ticket");
    Expect(state.completionCount == 1 && retained, "bound-publication-lease-retains-whole-charge-through-completion");

    state.publication.reset();
    (void)DrainCompletions(scheduler);
    const bool released = scheduler.ReservedMemoryBytes() == 0;
    Expect(released, "last-publication-owner-release-returns-charge-on-owner-drain");
    scheduler.Close();
    std::printf("%s publication-retention.shared-memory.summary passed=%u failed=%u controls=%u\n",
                gFailed == 0 ? "PASS" : "FAIL", gPassed, gFailed, gPassed + gFailed);
    return state.createdUnbound && state.bindResult == S_OK && bound && state.completionCount == 1 && retained &&
                   released
               ? 0
               : 1;
}

int RunReconstructedSchedulerEndpointProbe()
{
    gPassed = 0;
    gFailed = 0;
    std::optional<JobScheduler> lifetime;
    lifetime.emplace();
    const auto close = wil::scope_exit(
        [&]() noexcept
        {
            if (lifetime)
                lifetime.value().Close();
        });
    MemoryState first;
    MemoryState second;
    first.admittedBytes = SharedPublicationMemory::OwnershipAllowance();
    second.admittedBytes = first.admittedBytes;
    const auto admit = [&](MemoryState& state, JobTicket& ticket) -> bool
    {
        auto& scheduler = lifetime.value();
        if (scheduler.Initialize(1, {}, {&SampleMemory, nullptr}) != S_OK)
            return false;
        std::unique_ptr<Job> pending = std::make_unique<PublicationBoundaryJob>(scheduler, state);
        JobOptions options{};
        options.memoryBytes = state.admittedBytes;
        options.deferredRetirement = true;
        options.reserveRetainedMemory = true;
        return scheduler.Submit(pending, options, ticket) == S_OK && !pending &&
               SettleJobs(scheduler, kTimeoutMs) == SettleResult::Settled && state.publication &&
               state.bindResult == S_OK && state.completionCount == 1;
    };
    const auto endpointExpect = [&](bool condition, const char* control) noexcept
    {
        std::printf("%s publication-endpoint.%s\n", condition ? "PASS" : "FAIL", control);
        condition ? ++gPassed : ++gFailed;
        std::fflush(stdout);
    };
    JobTicket firstTicket{};
    if (!admit(first, firstTicket))
    {
        endpointExpect(false, "setup.first-original-publication");
        return 1;
    }
    const auto* const firstAddress = std::addressof(lifetime.value());
    endpointExpect(first.publication->BoundTo(lifetime.value(), firstTicket) &&
                       !first.publication->BoundTo(lifetime.value(), {firstTicket.slot, firstTicket.generation + 1}),
                   "live-original-endpoint-matches-only-original-ticket");
    lifetime.value().Close();
    endpointExpect(first.publication->IsBound() && first.publication->ResidentBytes() == first.admittedBytes &&
                       !first.publication->BoundTo(lifetime.value(), firstTicket),
                   "actual-close-keeps-old-charge-without-live-publication-authority");
    lifetime.reset();
    lifetime.emplace(); // Reconstruct the real scheduler at exactly the same wrapper address.
    JobTicket secondTicket{};
    if (!admit(second, secondTicket))
    {
        endpointExpect(false, "setup.reconstructed-original-publication");
        return 1;
    }
    endpointExpect(std::addressof(lifetime.value()) == firstAddress && secondTicket == firstTicket &&
                       !first.publication->BoundTo(lifetime.value(), secondTicket) &&
                       second.publication->BoundTo(lifetime.value(), secondTicket),
                   "same-address-and-same-ticket-reconstruction-refuses-old-endpoint");
    endpointExpect(first.publication->ResidentBytes() == first.admittedBytes &&
                       second.publication->ResidentBytes() == second.admittedBytes &&
                       lifetime.value().ReservedMemoryBytes() == second.admittedBytes,
                   "both-original-charges-remain-on-distinct-held-endpoints");
    first.publication.reset();
    (void)DrainCompletions(lifetime.value());
    endpointExpect(lifetime.value().ReservedMemoryBytes() == second.admittedBytes &&
                       second.publication->ResidentBytes() == second.admittedBytes,
                   "old-final-owner-release-cannot-refund-reconstructed-endpoint");
    second.publication.reset();
    (void)DrainCompletions(lifetime.value());
    endpointExpect(lifetime.value().ReservedMemoryBytes() == 0,
                   "new-final-owner-release-refunds-only-its-original-endpoint");
    std::printf("%s publication-endpoint.summary passed=%u failed=%u controls=%u\n", gFailed ? "FAIL" : "PASS", gPassed,
                gFailed, gPassed + gFailed);
    return static_cast<int>(gFailed);
}

} // namespace PublicationBoundary

namespace PublicationOwner
{
namespace
{
constexpr DWORD kTimeoutMs = 10000;
constexpr std::uint64_t kAdmissionBytes = 16384;
constexpr std::uint64_t kWideAvailable = kMemoryAdmissionHeadroomBytes + 1024 * 1024;
unsigned int gPassed = 0;
unsigned int gFailed = 0;

void Expect(bool condition, const char* name) noexcept
{
    std::printf("%s publication-owner.%s\n", condition ? "PASS" : "FAIL", name);
    std::fflush(stdout);
    condition ? ++gPassed : ++gFailed;
}

[[noreturn]] void SetupFailure(const char* name, DWORD error = 0) noexcept
{
    std::printf("FAIL publication-owner.setup.%s error=%lu\n", name, error);
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

void Wait(HANDLE event, const char* name) noexcept
{
    if (!event)
        SetupFailure(name, ERROR_INVALID_HANDLE);
    const DWORD result = WaitForSingleObject(event, kTimeoutMs);
    if (result != WAIT_OBJECT_0)
        SetupFailure(name, result == WAIT_FAILED ? GetLastError() : result);
}

// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027)
struct MemorySourceState final
{
    std::atomic<unsigned int> calls{0};
};
#pragma warning(pop)

bool SampleMemory(void* context, MemorySnapshot& snapshot) noexcept
{
    static_cast<MemorySourceState*>(context)->calls.fetch_add(1, std::memory_order_relaxed);
    snapshot = {kWideAvailable, kWideAvailable};
    return true;
}

void Start(JobScheduler& scheduler, MemorySourceState& source)
{
    const std::int32_t result = scheduler.Initialize(1, {}, {&SampleMemory, &source});
    if (result != S_OK)
        SetupFailure("initialize", static_cast<DWORD>(result));
}

JobOptions Options() noexcept
{
    JobOptions options{};
    options.kind = JobClass::Stroke;
    options.memoryBytes = kAdmissionBytes;
    options.deferredRetirement = true;
    options.reserveRetainedMemory = true;
    return options;
}

// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027)
struct OwnerState final
{
    JobScheduler* scheduler = nullptr;
    MemorySourceState* source = nullptr;
    HANDLE workerEntered = nullptr;
    HANDLE allowFirstStageReturn = nullptr;
    JobTicket ticket{};
    std::weak_ptr<SharedPublicationMemory> prebindOwner;
    std::shared_ptr<const SharedPublicationMemory> published;
    std::weak_ptr<const SharedPublicationMemory> weakPublished;
    std::atomic<DWORD> runThread{0};
    std::atomic<DWORD> retireThread{0};
    std::atomic<DWORD> wrapperDestructorThread{0};
    std::atomic<unsigned int> runCount{0};
    std::atomic<unsigned int> retireCount{0};
    std::atomic<unsigned int> wrapperDestructorCount{0};
    std::atomic<DWORD> strongReleaseThread{0};
    std::atomic<DWORD> weakReleaseThread{0};
    unsigned int deferCompletionCount = 0;
    unsigned int completionCount = 0;
    std::int32_t outsideUnboundBindResult = E_UNEXPECTED;
    std::int32_t bindResult = E_UNEXPECTED;
    std::int32_t occupiedBindResult = E_UNEXPECTED;
    bool outsideUnboundUnchanged = false;
    bool unboundBeforeCallback = false;
    bool aliasesPublishedOwner = false;
    bool sameTicketAcrossCallbacks = true;
    bool resumeAccepted = false;
    bool successfulResultAtBind = false;
    std::uint64_t chargeBeforeBind = 0;
    std::uint64_t chargeAfterBind = 0;
    std::uint64_t chargeAfterOccupiedRefusal = 0;
    std::uint64_t samplerCallsBeforeBind = 0;
    std::uint64_t samplerCallsAfterBind = 0;
    std::uint64_t finalCallbackCharge = 0;
    JobResult completedResult{JobStatus::Failed};
};
#pragma warning(pop)

// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4626)
struct WorkerAlias final
{
    std::shared_ptr<SharedPublicationMemory> owner;
    OwnerState& state;

    WorkerAlias(std::shared_ptr<SharedPublicationMemory>&& value, OwnerState& ownerState) noexcept
        : owner(std::move(value)), state(ownerState)
    {
    }

    ~WorkerAlias()
    {
        state.wrapperDestructorThread.store(GetCurrentThreadId(), std::memory_order_relaxed);
        state.wrapperDestructorCount.fetch_add(1, std::memory_order_relaxed);
    }
};
#pragma warning(pop)

// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4625 4626 5027)
class PublicationOwnerJob final : public Job
{
  public:
    explicit PublicationOwnerJob(OwnerState& state) noexcept : _state(state) {}

    JobResult Run(JobContext&) override
    {
        const unsigned int run = _state.runCount.fetch_add(1, std::memory_order_relaxed) + 1;
        if (run == 1)
        {
            _state.runThread.store(GetCurrentThreadId(), std::memory_order_relaxed);
            auto owner = SharedPublicationMemory::CreateForWork();
            _wrapper = std::make_unique<WorkerAlias>(std::move(owner), _state);
            _state.prebindOwner = _wrapper->owner;
            if (!SetEvent(_state.workerEntered))
                return {JobStatus::Failed, HRESULT_FROM_WIN32(GetLastError())};
            const DWORD waited = WaitForSingleObject(_state.allowFirstStageReturn, kTimeoutMs);
            if (waited != WAIT_OBJECT_0)
                return {JobStatus::Failed, waited == WAIT_FAILED ? HRESULT_FROM_WIN32(GetLastError()) : E_ABORT};
        }
        return {};
    }

    void RetireWorkerStage(JobContext&, JobResult) noexcept override
    {
        const unsigned int retired = _state.retireCount.fetch_add(1, std::memory_order_relaxed) + 1;
        _state.retireThread.store(GetCurrentThreadId(), std::memory_order_relaxed);
        if (retired == 2)
            _wrapper.reset();
    }

    bool DeferCompletion(JobResult result, JobTicket ticket) noexcept override
    {
        ++_state.deferCompletionCount;
        _state.sameTicketAcrossCallbacks = _state.sameTicketAcrossCallbacks && ticket == _state.ticket;
        if (_state.deferCompletionCount != 1)
            return false;

        if (!_wrapper || !_wrapper->owner)
            return false;
        _state.successfulResultAtBind = result.status == JobStatus::Finished;
        _state.unboundBeforeCallback = !_wrapper->owner->IsBound() && _wrapper->owner->Bytes() == 0;
        _state.chargeBeforeBind = _state.scheduler->ReservedMemoryBytes();
        _state.samplerCallsBeforeBind = _state.source->calls.load(std::memory_order_relaxed);
        _state.bindResult = _wrapper->owner->BindForPublication(*_state.scheduler, ticket);
        _state.samplerCallsAfterBind = _state.source->calls.load(std::memory_order_relaxed);
        _state.chargeAfterBind = _state.scheduler->ReservedMemoryBytes();
        if (_state.bindResult != S_OK)
            return false;

        _state.published = _wrapper->owner;
        _state.weakPublished = _state.published;
        _state.aliasesPublishedOwner = _state.published.get() == _wrapper->owner.get();
        _state.occupiedBindResult = _wrapper->owner->BindForPublication(*_state.scheduler, ticket);
        _state.chargeAfterOccupiedRefusal = _state.scheduler->ReservedMemoryBytes();
        _state.resumeAccepted = _state.scheduler->Resume(ticket);
        return true;
    }

    void Completed(JobResult result) noexcept override
    {
        ++_state.completionCount;
        _state.completedResult = result;
        _state.finalCallbackCharge = _state.scheduler->ReservedMemoryBytes();
    }

  private:
    OwnerState& _state;
    std::unique_ptr<WorkerAlias> _wrapper;
};
#pragma warning(pop)

void WaitForPublication(JobScheduler& scheduler, OwnerState& state)
{
    const ULONGLONG deadline = GetTickCount64() + kTimeoutMs;
    while (state.deferCompletionCount == 0)
    {
        (void)DrainCompletions(scheduler);
        if (state.deferCompletionCount != 0)
            return;
        const auto now = GetTickCount64();
        if (now >= deadline)
            SetupFailure("publication-acknowledgement-deadline", ERROR_TIMEOUT);
        if (scheduler.HasUiWork())
            continue;
        if (WaitForSingleObject(static_cast<HANDLE>(scheduler.WakeHandle()), static_cast<DWORD>(deadline - now)) !=
            WAIT_OBJECT_0)
            SetupFailure("publication-acknowledgement-wake");
    }
}

void Submit(JobScheduler& scheduler, std::unique_ptr<Job>& job, JobTicket& ticket)
{
    const std::int32_t result = scheduler.Submit(job, Options(), ticket);
    if (result != S_OK || job)
        SetupFailure("submit", static_cast<DWORD>(result));
}

void TestPublicationOwnerWeakRetirement()
{
    JobScheduler scheduler;
    MemorySourceState source;
    Start(scheduler, source);
    auto workerEntered = MakeEvent("worker-entered-event");
    auto allowFirstStageReturn = MakeEvent("allow-first-stage-return-event");

    OwnerState state;
    state.scheduler = &scheduler;
    state.source = &source;
    state.workerEntered = workerEntered.get();
    state.allowFirstStageReturn = allowFirstStageReturn.get();
    std::unique_ptr<Job> job = std::make_unique<PublicationOwnerJob>(state);
    Submit(scheduler, job, state.ticket);
    Wait(workerEntered.get(), "worker-created-owner");

    const auto chargeBeforeOutsideBind = scheduler.ReservedMemoryBytes();
    const auto samplesBeforeOutsideBind = source.calls.load(std::memory_order_relaxed);
    auto unbound = state.prebindOwner.lock();
    if (!unbound)
        SetupFailure("prebind-owner-lock");
    const bool factoryWasUnbound = !unbound->IsBound() && unbound->Bytes() == 0;
    state.outsideUnboundBindResult = unbound->BindForPublication(scheduler, state.ticket);
    state.outsideUnboundUnchanged = !unbound->IsBound() && unbound->Bytes() == 0 &&
                                    scheduler.ReservedMemoryBytes() == chargeBeforeOutsideBind &&
                                    source.calls.load(std::memory_order_relaxed) == samplesBeforeOutsideBind;
    unbound.reset();
    state.prebindOwner.reset(); // Do not leave a second weak owner behind the final-control-block fixture.

    if (!SetEvent(allowFirstStageReturn.get()))
        SetupFailure("allow-first-stage-return", GetLastError());
    WaitForPublication(scheduler, state);
    const auto settle = SettleJobs(scheduler, kTimeoutMs);
    const auto chargeAfterFinalCallback = scheduler.ReservedMemoryBytes();
    const bool publicationStillBound = state.published && state.published->IsBound() &&
                                       state.published->Bytes() == kAdmissionBytes &&
                                       state.published->ResidentBytes() == kAdmissionBytes;

    std::thread strongRelease(
        [strong = std::move(state.published), &state]() mutable noexcept
        {
            state.strongReleaseThread.store(GetCurrentThreadId(), std::memory_order_relaxed);
            strong.reset();
        });
    strongRelease.join();
    const bool weakExpiredWithCharge =
        state.weakPublished.expired() && scheduler.ReservedMemoryBytes() == kAdmissionBytes;
    (void)DrainCompletions(scheduler);
    const bool chargeSurvivedOwnerDrain = scheduler.ReservedMemoryBytes() == kAdmissionBytes;

    std::thread weakRelease(
        [weak = std::move(state.weakPublished), &state]() mutable noexcept
        {
            state.weakReleaseThread.store(GetCurrentThreadId(), std::memory_order_relaxed);
            weak.reset();
        });
    weakRelease.join();
    const auto chargeBeforeAcknowledgement = scheduler.ReservedMemoryBytes();
    (void)DrainCompletions(scheduler);

    Expect(SharedPublicationMemory::OwnershipAllowance() > 0 &&
               SharedPublicationMemory::OwnershipAllowance() <= kAdmissionBytes &&
               state.completedResult.status == JobStatus::Finished && settle == SettleResult::Settled &&
               state.completionCount == 1 && state.deferCompletionCount == 2 && state.sameTicketAcrossCallbacks &&
               state.resumeAccepted,
           "preclaimed-deferred-ticket-resumes-same-record-to-final-success-callback");
    Expect(state.runCount.load(std::memory_order_relaxed) == 2 &&
               state.retireCount.load(std::memory_order_relaxed) == 2 &&
               state.runThread.load(std::memory_order_relaxed) != GetCurrentThreadId() &&
               state.retireThread.load(std::memory_order_relaxed) != GetCurrentThreadId() &&
               state.wrapperDestructorCount.load(std::memory_order_relaxed) == 1 &&
               state.wrapperDestructorThread.load(std::memory_order_relaxed) != GetCurrentThreadId() &&
               state.wrapperDestructorThread.load(std::memory_order_relaxed) ==
                   state.retireThread.load(std::memory_order_relaxed),
           "real-worker-runs-and-allocation-free-retirement-drop-original-wrapper-alias-off-owner");
    Expect(factoryWasUnbound && state.unboundBeforeCallback && state.outsideUnboundBindResult == E_INVALIDARG &&
               state.outsideUnboundUnchanged && state.successfulResultAtBind,
           "worker-factory-starts-unbound-and-outside-bind-refusal-preserves-state");
    Expect(state.bindResult == S_OK && state.aliasesPublishedOwner && state.occupiedBindResult == E_INVALIDARG &&
               state.chargeBeforeBind == kAdmissionBytes && state.chargeAfterBind == kAdmissionBytes &&
               state.chargeAfterOccupiedRefusal == kAdmissionBytes && state.finalCallbackCharge == kAdmissionBytes &&
               state.samplerCallsBeforeBind == state.samplerCallsAfterBind &&
               chargeAfterFinalCallback == kAdmissionBytes && publicationStillBound,
           "exact-whole-16384-charge-binds-once-without-query-and-published-alias-survives-final-callback");
    Expect(state.strongReleaseThread.load(std::memory_order_relaxed) != GetCurrentThreadId() && weakExpiredWithCharge &&
               chargeSurvivedOwnerDrain,
           "expired-weak-control-block-retains-16384-charge-through-owner-drain");
    Expect(state.weakReleaseThread.load(std::memory_order_relaxed) != GetCurrentThreadId() &&
               chargeBeforeAcknowledgement == kAdmissionBytes && scheduler.ReservedMemoryBytes() == 0,
           "real-worker-final-weak-release-stays-charged-until-owner-drain-acknowledges-exact-zero");
}
} // namespace

int RunPublicationOwnerTests()
{
    gPassed = 0;
    gFailed = 0;
    TestPublicationOwnerWeakRetirement();
    std::printf("%s publication-owner.summary scenarios=1 assertions=%u passed=%u failed=%u\n",
                gFailed == 0 ? "PASS" : "FAIL", gPassed + gFailed, gPassed, gFailed);
    return static_cast<int>(gFailed);
}

} // namespace PublicationOwner
