#include "Host.Tests.RetainedSlotAdmission.h"

#include "Jobs.h"

#include <windows.h>

#include <array>
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
constexpr std::uint64_t kReservation = 4096;
constexpr std::uint64_t kAvailable = kMemoryAdmissionHeadroomBytes + 16 * 1024 * 1024;
int gFailures = 0;

void Expect(bool condition, const char* name) noexcept
{
    std::printf("%s retained-slot-admission.%s\n", condition ? "PASS" : "FAIL", name);
    gFailures += condition ? 0 : 1;
}

[[noreturn]] void SetupFailure(const char* name) noexcept
{
    std::printf("FAIL retained-slot-admission.setup.%s\n", name);
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

bool SampleMemory(void*, MemorySnapshot& snapshot) noexcept
{
    snapshot = {kAvailable, kAvailable};
    return true;
}

void Start(JobScheduler& scheduler, unsigned int threads = 4)
{
    if (scheduler.Initialize(threads, {}, {&SampleMemory, nullptr}) != S_OK)
        SetupFailure("initialize");
}

void DrainUntil(JobScheduler& scheduler, HANDLE event, const char* name) noexcept
{
    const ULONGLONG deadline = GetTickCount64() + 10000;
    while (WaitForSingleObject(event, 0) != WAIT_OBJECT_0)
    {
        (void)DrainCompletions(scheduler);
        if (WaitForSingleObject(event, 0) == WAIT_OBJECT_0)
            return;
        const ULONGLONG now = GetTickCount64();
        const HANDLE wake = static_cast<HANDLE>(scheduler.WakeHandle());
        if (now >= deadline || !wake || WaitForSingleObject(wake, static_cast<DWORD>(deadline - now)) != WAIT_OBJECT_0)
            SetupFailure(name);
    }
}

void DrainUntilAll(JobScheduler& scheduler, const std::array<wil::unique_handle, 64>& gates) noexcept
{
    const ULONGLONG deadline = GetTickCount64() + 10000;
    for (;;)
    {
        (void)DrainCompletions(scheduler);
        bool all = true;
        for (const auto& gate : gates)
            all = all && WaitForSingleObject(gate.get(), 0) == WAIT_OBJECT_0;
        if (all)
            return;
        const ULONGLONG now = GetTickCount64();
        const HANDLE wake = static_cast<HANDLE>(scheduler.WakeHandle());
        if (now >= deadline || !wake || WaitForSingleObject(wake, static_cast<DWORD>(deadline - now)) != WAIT_OBJECT_0)
            SetupFailure("all-suspended-gates");
    }
}

struct State final
{
    JobScheduler* scheduler = nullptr;
    JobTicket ticket{};
    RetainedMemoryTicket retained{};
    SharedRetainedMemoryLease shared;
    HANDLE deferred = nullptr;
    HANDLE completedEvent = nullptr;
    HANDLE runEntered = nullptr;
    HANDLE runRelease = nullptr;
    unsigned int* completionCount = nullptr;
    unsigned int completionTarget = 0;
    HANDLE completionGroup = nullptr;
    std::atomic<unsigned int> runs{0};
    unsigned int deferrals = 0;
    unsigned int completions = 0;
    bool keepSuspended = true;
    bool transfer = false;
    bool sharedTransfer = false;
    JobResult runResult{};
    JobResult completed{};
    std::int32_t transferResult = E_UNEXPECTED;
    std::uint64_t reservedAtCallback = 0;
};

class AdmissionJob final : public Job
{
  public:
    explicit AdmissionJob(State& state) noexcept : _state(state) {}

    JobResult Run(JobContext&) override
    {
        _state.runs.fetch_add(1, std::memory_order_relaxed);
        if (_state.runEntered)
        {
            (void)SetEvent(_state.runEntered);
            Wait(_state.runRelease, "run-release");
        }
        return _state.runResult;
    }

    bool DeferRetirement(JobResult, JobTicket ticket) noexcept override
    {
        _state.ticket = ticket;
        ++_state.deferrals;
        if (_state.deferred)
            (void)SetEvent(_state.deferred);
        return _state.keepSuspended;
    }

    void Completed(JobResult result) noexcept override
    {
        ++_state.completions;
        if (_state.completionCount && ++(*_state.completionCount) == _state.completionTarget)
            (void)SetEvent(_state.completionGroup);
        _state.completed = result;
        _state.reservedAtCallback = _state.scheduler->ReservedMemoryBytes();
        if (result.status != JobStatus::Finished || (!_state.transfer && !_state.sharedTransfer))
        {
            if (_state.completedEvent)
                (void)SetEvent(_state.completedEvent);
            return;
        }
        _state.transferResult = _state.sharedTransfer ? _state.scheduler->RetainMemory(_state.ticket, _state.shared)
                                                      : _state.scheduler->RetainMemory(_state.ticket, _state.retained);
        if (_state.completedEvent)
            (void)SetEvent(_state.completedEvent);
    }

  private:
    State& _state;
};

JobOptions Options(std::uint64_t bytes, bool reserveSlot, bool deferred = false) noexcept
{
    return {JobClass::Background, 0, bytes, deferred, true, reserveSlot};
}

bool SlotCanBeReservedAgain(JobScheduler& scheduler, const char* name) noexcept
{
    State state;
    state.scheduler = &scheduler;
    std::unique_ptr<Job> job = std::make_unique<AdmissionJob>(state);
    JobTicket ticket{};
    if (scheduler.Submit(job, Options(kReservation, true), ticket) != S_OK || job ||
        SettleJobs(scheduler, 10000) != SettleResult::Settled)
        SetupFailure(name);
    return state.completions == 1 && state.completed.status == JobStatus::Finished &&
           scheduler.ReservedMemoryBytes() == 0;
}

void WaitForSuspended(JobScheduler& scheduler, State& state, const char* name) noexcept
{
    DrainUntil(scheduler, state.deferred, name);
}

void TestAdmissionCapacityAndTransfer() noexcept
{
    JobScheduler scheduler;
    Start(scheduler);
    auto firstRunEntered = Event();
    auto firstRunRelease = Event();
    std::array<wil::unique_handle, 64> gates;
    std::array<State, 64> states;
    std::array<JobTicket, 64> tickets{};
    std::array<std::unique_ptr<Job>, 64> jobs;
    bool allAdmitted = true;
    for (std::size_t i = 0; i < jobs.size(); ++i)
    {
        gates[i] = Event();
        states[i].scheduler = &scheduler;
        states[i].deferred = gates[i].get();
        if (i == 0)
        {
            states[i].runEntered = firstRunEntered.get();
            states[i].runRelease = firstRunRelease.get();
        }
        jobs[i] = std::make_unique<AdmissionJob>(states[i]);
        const auto submitted = scheduler.Submit(jobs[i], Options(kReservation, true, true), tickets[i]);
        allAdmitted = allAdmitted && submitted == S_OK && !jobs[i] && tickets[i].generation != 0;
    }
    Expect(allAdmitted, "capacity.all-64-opt-in-admissions-claim-before-job-capture");
    Wait(firstRunEntered.get(), "capacity.first-run-entered");
    Expect(scheduler.ReservedMemoryBytes() == 64 * kReservation,
           "capacity.all-original-memory-reservations-exist-before-initial-run-release");
    (void)SetEvent(firstRunRelease.get());
    DrainUntilAll(scheduler, gates);

    auto legacyCompleted = Event();
    State legacy;
    legacy.scheduler = &scheduler;
    legacy.transfer = true;
    legacy.completedEvent = legacyCompleted.get();
    std::unique_ptr<Job> legacyJob = std::make_unique<AdmissionJob>(legacy);
    JobTicket legacyTicket{};
    if (scheduler.Submit(legacyJob, Options(kReservation, false), legacyTicket) != S_OK)
        SetupFailure("capacity-legacy-submit");
    legacy.ticket = legacyTicket;
    DrainUntil(scheduler, legacyCompleted.get(), "capacity.legacy-completion");
    Expect(static_cast<HRESULT>(legacy.transferResult) == HRESULT_FROM_WIN32(ERROR_NOT_ENOUGH_QUOTA) &&
               scheduler.ReservedMemoryBytes() == 64 * kReservation,
           "capacity.ordinary-callback-cannot-steal-any-preclaimed-slot");

    State overflowState;
    overflowState.scheduler = &scheduler;
    std::unique_ptr<Job> overflow = std::make_unique<AdmissionJob>(overflowState);
    JobTicket overflowTicket{};
    const auto overflowResult = scheduler.Submit(overflow, Options(kReservation, true), overflowTicket);
    Expect(static_cast<HRESULT>(overflowResult) == HRESULT_FROM_WIN32(ERROR_NOT_ENOUGH_QUOTA) && overflow &&
               overflowTicket == JobTicket{},
           "capacity.65th-opt-in-refusal-consumes-no-job-or-ticket");

    bool allResumed = true;
    for (std::size_t i = 0; i < states.size(); ++i)
    {
        states[i].keepSuspended = false;
        allResumed = allResumed && scheduler.Resume(tickets[i]);
    }
    if (!allResumed)
        SetupFailure("capacity-resume");
    for (auto& state : states)
        state.transfer = true;
    // The final drain promotes each resumed ticket before its normal Completed callback.
    (void)DrainCompletions(scheduler);
    if (SettleJobs(scheduler, 10000) != SettleResult::Settled)
        SetupFailure("capacity-settle-after-resume");
    bool allTransferred = true;
    for (const auto& state : states)
        allTransferred =
            allTransferred && state.completions == 1 && state.transferResult == S_OK && state.retained.generation != 0;
    Expect(allTransferred && scheduler.ReservedMemoryBytes() == 64 * kReservation,
           "capacity.all-preclaimed-slots-transfer-through-completed-with-original-byte-charge");
    bool allReleased = true;
    for (auto& state : states)
        allReleased = allReleased && scheduler.ReleaseRetainedMemory(state.retained);
    Expect(allReleased && scheduler.ReservedMemoryBytes() == 0,
           "capacity.owner-release-returns-each-transferred-slot-and-byte-charge");
    scheduler.Close();
}

void TestLegacyCallbacksCannotStealPreclaimedSlot() noexcept
{
    JobScheduler scheduler;
    Start(scheduler, 2);
    auto deferred = Event();
    State reserved;
    reserved.scheduler = &scheduler;
    reserved.deferred = deferred.get();
    std::unique_ptr<Job> reservedJob = std::make_unique<AdmissionJob>(reserved);
    JobTicket reservedTicket{};
    if (scheduler.Submit(reservedJob, Options(kReservation, true, true), reservedTicket) != S_OK)
        SetupFailure("steal-preclaim-submit");
    DrainUntil(scheduler, deferred.get(), "steal.preclaim-suspended");

    auto legacyGroupDone = Event();
    unsigned int legacyCompletedCount = 0;
    std::array<State, 63> legacy;
    std::array<std::unique_ptr<Job>, 63> jobs;
    bool allSubmitted = true;
    for (std::size_t i = 0; i < jobs.size(); ++i)
    {
        legacy[i].scheduler = &scheduler;
        legacy[i].transfer = true;
        legacy[i].completionCount = &legacyCompletedCount;
        legacy[i].completionTarget = 63;
        legacy[i].completionGroup = legacyGroupDone.get();
        jobs[i] = std::make_unique<AdmissionJob>(legacy[i]);
        JobTicket ticket{};
        const auto submitted = scheduler.Submit(jobs[i], Options(kReservation, false), ticket);
        allSubmitted = allSubmitted && submitted == S_OK && !jobs[i];
        legacy[i].ticket = ticket;
    }
    Expect(allSubmitted, "steal.submit-all-63-legacy-jobs");
    // The reserved original record remains suspended; only its 63 independent callbacks may settle here.
    DrainUntil(scheduler, legacyGroupDone.get(), "steal-legacy-callbacks");
    bool allLegacyTransferred = true;
    for (const auto& state : legacy)
        allLegacyTransferred = allLegacyTransferred && state.completions == 1 && state.transferResult == S_OK;
    Expect(allLegacyTransferred, "steal.legacy-callbacks-consume-every-unreserved-slot");

    reserved.transfer = true;
    reserved.keepSuspended = false;
    if (!scheduler.Resume(reservedTicket))
        SetupFailure("steal-resume-preclaimed-ticket");
    (void)DrainCompletions(scheduler);
    if (SettleJobs(scheduler, 10000) != SettleResult::Settled)
        SetupFailure("steal-preclaimed-settle");
    Expect(reserved.completions == 1 && reserved.transferResult == S_OK && reserved.retained.generation != 0,
           "steal.legacy-callbacks-cannot-take-preclaimed-slot");
    bool legacyReleased = true;
    for (auto& state : legacy)
        legacyReleased = legacyReleased && scheduler.ReleaseRetainedMemory(state.retained);
    const bool reservedReleased = scheduler.ReleaseRetainedMemory(reserved.retained);
    Expect(legacyReleased && reservedReleased && scheduler.ReservedMemoryBytes() == 0,
           "steal.release-all-legacy-and-preclaimed-cache-slots");
    scheduler.Close();
}

void TestValidationFailureCancellationAndUntransferredSuccess() noexcept
{
    JobScheduler scheduler;
    Start(scheduler, 2);
    State invalidState;
    invalidState.scheduler = &scheduler;
    std::unique_ptr<Job> invalid = std::make_unique<AdmissionJob>(invalidState);
    JobTicket invalidTicket{};
    Expect(scheduler.Submit(invalid, Options(0, true), invalidTicket) == E_INVALIDARG && invalid &&
               invalidTicket == JobTicket{},
           "validation.zero-byte-opt-in-is-e-invalidarg-without-capture");

    auto deferred = Event();
    State cancelled;
    cancelled.scheduler = &scheduler;
    cancelled.deferred = deferred.get();
    cancelled.keepSuspended = true;
    std::unique_ptr<Job> cancelJob = std::make_unique<AdmissionJob>(cancelled);
    JobTicket cancelTicket{};
    if (scheduler.Submit(cancelJob, Options(kReservation, true, true), cancelTicket) != S_OK)
        SetupFailure("cancel-submit");
    WaitForSuspended(scheduler, cancelled, "cancel.suspended");
    Expect(scheduler.ReservedMemoryBytes() == kReservation, "cancel.original-charge-held-while-suspended");
    if (!scheduler.Cancel(cancelTicket))
        SetupFailure("cancel-request");
    cancelled.keepSuspended = false;
    if (!scheduler.Resume(cancelTicket))
        SetupFailure("cancel-resume");
    (void)DrainCompletions(scheduler);
    if (SettleJobs(scheduler, 10000) != SettleResult::Settled)
        SetupFailure("cancel-settle");
    Expect(cancelled.completions == 1 && cancelled.completed.status == JobStatus::Cancelled &&
               scheduler.ReservedMemoryBytes() == 0 && SlotCanBeReservedAgain(scheduler, "cancel-slot-reuse"),
           "cancel.slot-and-original-charge-return-after-final-cancel-callback");

    State untransferred;
    untransferred.scheduler = &scheduler;
    std::unique_ptr<Job> success = std::make_unique<AdmissionJob>(untransferred);
    JobTicket successTicket{};
    if (scheduler.Submit(success, Options(kReservation, true), successTicket) != S_OK)
        SetupFailure("untransferred-submit");
    if (SettleJobs(scheduler, 10000) != SettleResult::Settled)
        SetupFailure("untransferred-settle");
    Expect(untransferred.completed.status == JobStatus::Finished && untransferred.completions == 1 &&
               untransferred.reservedAtCallback == kReservation && scheduler.ReservedMemoryBytes() == 0 &&
               SlotCanBeReservedAgain(scheduler, "untransferred-slot-reuse"),
           "untransferred.success-keeps-charge-through-callback-then-returns-slot");
    scheduler.Close();
}

void TestFailedRunAndClosePreserveSharedEndpoint() noexcept
{
    JobScheduler scheduler;
    Start(scheduler, 1);
    State failed;
    failed.scheduler = &scheduler;
    failed.runResult = {JobStatus::Failed, E_ACCESSDENIED};
    std::unique_ptr<Job> failedJob = std::make_unique<AdmissionJob>(failed);
    JobTicket failedTicket{};
    if (scheduler.Submit(failedJob, Options(kReservation, true), failedTicket) != S_OK)
        SetupFailure("failed-submit");
    if (SettleJobs(scheduler, 10000) != SettleResult::Settled)
        SetupFailure("failed-settle");
    Expect(failed.completed.status == JobStatus::Failed && failed.completed.error == E_ACCESSDENIED &&
               failed.completions == 1 && failed.reservedAtCallback == kReservation &&
               scheduler.ReservedMemoryBytes() == 0 && SlotCanBeReservedAgain(scheduler, "failed-slot-reuse"),
           "failed.run-returns-slot-after-final-failed-callback");

    State retained;
    retained.scheduler = &scheduler;
    retained.sharedTransfer = true;
    std::unique_ptr<Job> retainedJob = std::make_unique<AdmissionJob>(retained);
    JobTicket retainedTicket{};
    if (scheduler.Submit(retainedJob, Options(kReservation, true), retainedTicket) != S_OK)
        SetupFailure("close-shared-submit");
    retained.ticket = retainedTicket;
    if (SettleJobs(scheduler, 10000) != SettleResult::Settled)
        SetupFailure("close-shared-settle");
    const auto oldResident = retained.shared.ResidentBytes();
    Expect(retained.transferResult == S_OK && retained.shared && oldResident == kReservation,
           "close.shared-lease-owns-original-graph-charge");
    auto closeDeferred = Event();
    State pendingClose;
    pendingClose.scheduler = &scheduler;
    pendingClose.deferred = closeDeferred.get();
    std::unique_ptr<Job> pendingJob = std::make_unique<AdmissionJob>(pendingClose);
    JobTicket pendingTicket{};
    if (scheduler.Submit(pendingJob, Options(kReservation, true, true), pendingTicket) != S_OK)
        SetupFailure("close-pending-submit");
    DrainUntil(scheduler, closeDeferred.get(), "close.pending-preclaim-suspended");
    scheduler.Close();
    Expect(pendingClose.completions == 0 && retained.completions == 1 &&
               retained.shared.ResidentBytes() == oldResident && scheduler.ReservedMemoryBytes() == 0,
           "close.returns-pending-slot-without-callback-and-keeps-old-shared-endpoint-alive");
    retained.shared.Reset();
    Expect(!retained.shared && retained.shared.ResidentBytes() == 0,
           "close.old-endpoint-remains-safe-after-scheduler-close");
}

void TestSharedGraphAcknowledgementRetainsCharge() noexcept
{
    JobScheduler scheduler;
    Start(scheduler, 1);
    State state;
    state.scheduler = &scheduler;
    state.sharedTransfer = true;
    std::unique_ptr<Job> job = std::make_unique<AdmissionJob>(state);
    JobTicket ticket{};
    if (scheduler.Submit(job, Options(kReservation, true), ticket) != S_OK)
        SetupFailure("ack-submit");
    state.ticket = ticket;
    if (SettleJobs(scheduler, 10000) != SettleResult::Settled)
        SetupFailure("ack-settle-transfer");
    const bool transferred = state.transferResult == S_OK && state.shared.Bytes() == kReservation;
    state.shared.Reset();
    const bool heldUntilOwnerAck =
        scheduler.ReservedMemoryBytes() == kReservation && scheduler.UiReadinessForTest().memory;
    (void)DrainCompletions(scheduler);
    Expect(transferred && heldUntilOwnerAck && scheduler.ReservedMemoryBytes() == 0,
           "ack.shared-graph-charge-remains-until-owner-drains-reset-ack");
    scheduler.Close();
}

void TestOptOutKeepsLegacyBehavior() noexcept
{
    JobScheduler scheduler;
    Start(scheduler, 1);
    State legacy;
    legacy.scheduler = &scheduler;
    legacy.transfer = true;
    std::unique_ptr<Job> job = std::make_unique<AdmissionJob>(legacy);
    JobTicket ticket{};
    const JobOptions legacyOptions{JobClass::Background, 0, kReservation, false, true};
    if (scheduler.Submit(job, legacyOptions, ticket) != S_OK || job)
        SetupFailure("optout-submit");
    legacy.ticket = ticket;
    if (SettleJobs(scheduler, 10000) != SettleResult::Settled)
        SetupFailure("optout-settle");
    Expect(legacy.completions == 1 && legacy.completed.status == JobStatus::Finished && legacy.transferResult == S_OK &&
               scheduler.ReservedMemoryBytes() == kReservation,
           "optout.default-false-preserves-legacy-completed-transfer-semantics");
    Expect(scheduler.ReleaseRetainedMemory(legacy.retained) && scheduler.ReservedMemoryBytes() == 0,
           "optout.release-legacy-ticket-preserves-old-accounting");
    scheduler.Close();
}
} // namespace

int RunRetainedSlotAdmissionTests()
{
    gFailures = 0;
    TestAdmissionCapacityAndTransfer();
    TestLegacyCallbacksCannotStealPreclaimedSlot();
    TestValidationFailureCancellationAndUntransferredSuccess();
    TestFailedRunAndClosePreserveSharedEndpoint();
    TestSharedGraphAcknowledgementRetainsCharge();
    TestOptOutKeepsLegacyBehavior();
    std::printf("%s retained-slot-admission.summary failures=%d\n", gFailures == 0 ? "PASS" : "FAIL", gFailures);
    return gFailures;
}
