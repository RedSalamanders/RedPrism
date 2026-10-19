#include "Document.Tests.ContinuingPublication.h"

#include "Jobs.h"
#include "LocallyOwnedPublication.h"
#include "SharedPublicationMemory.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <utility>
#include <windows.h>

namespace
{
constexpr std::uint64_t kWhole = 256 * 1024;
constexpr std::uint64_t kOpening = 32 * 1024;
constexpr std::uint64_t kFinal = 64 * 1024;
unsigned int failures = 0, checks = 0;
const char* currentCase = "descriptor";
std::uint64_t queries = 0;
void Expect(bool value, const char* label)
{
    ++checks;
    std::printf("%s continuing-publication.%s.%s\n", value ? "PASS" : "FAIL", currentCase, label);
    failures += value ? 0 : 1;
}
[[noreturn]] void SetupFailure(const char* label)
{
    std::printf("FAIL continuing-publication.setup.%s\n", label);
    std::_Exit(1);
}
bool Sample(void*, MemorySnapshot& snapshot) noexcept
{
    ++queries;
    snapshot = {kMemoryAdmissionHeadroomBytes + 512 * 1024 * 1024, kMemoryAdmissionHeadroomBytes + 512 * 1024 * 1024};
    return true;
}
void Settle(JobScheduler& scheduler)
{
    if (SettleJobs(scheduler, 10000) != SettleResult::Settled)
        SetupFailure("bounded-settle");
}
enum class Mode
{
    Success,
    FailSecond,
    CancelAfterOpening,
    CloseSuspended
};
struct State final
{
    JobScheduler& scheduler;
    Mode mode;
    JobTicket original;
    std::shared_ptr<SharedPublicationMemory> opening, finalOutput, temporary;
    std::shared_ptr<const int> openingValue, finalValue;
    std::uint64_t firstBudget = 0, secondBudget = 0;
    std::uint64_t queriesBefore = 0, queriesAfter = 0;
    std::uint64_t reservationAfterOpening = 0;
    std::uint64_t terminalBytes = 0;
    std::int32_t wrongTicket = S_OK, overLimit = S_OK, undercharged = S_OK;
    std::int32_t openingResult = E_UNEXPECTED, repeated = S_OK, growth = S_OK;
    std::int32_t terminalResult = E_UNEXPECTED;
    unsigned int runs = 0;
    bool openingAck = false, resumed = false, openingSealed = false, sameTicket = true;
    bool joinedClose = false, completed = false;
    JobResult outcome;
};
class ContinuingJob final : public Job
{
  public:
    explicit ContinuingJob(State& state) noexcept : _state(state) {}
    JobResult Run(JobContext& context) override
    {
        auto& value = _state;
        if (++value.runs == 1)
        {
            value.firstBudget = context.reservedMemoryBytes;
            value.opening = SharedPublicationMemory::CreateForWork(kOpening);
            value.finalOutput = SharedPublicationMemory::CreateForWork(kWhole - kOpening);
            value.temporary = SharedPublicationMemory::CreateForWork(kWhole - kOpening);
            value.openingValue = MakeLocallyOwnedPublicationObject<const int>(value.opening, sizeof(int), 0, 17);
            return {};
        }
        value.secondBudget = context.reservedMemoryBytes;
        value.finalValue = MakeLocallyOwnedPublicationObject<const int>(value.finalOutput, sizeof(int), 0, 41);
        return value.mode == Mode::FailSecond ? JobResult{JobStatus::Failed, E_ACCESSDENIED} : JobResult{};
    }
    bool DeferRetirement(JobResult, JobTicket ticket) noexcept override
    {
        _state.sameTicket = _state.sameTicket && ticket == _state.original;
        return false;
    }
    bool DeferCompletion(JobResult, JobTicket ticket) noexcept override
    {
        auto& value = _state;
        if (value.openingAck)
            return false;
        value.queriesBefore = queries;
        SharedRetainedMemoryLease refused;
        value.wrongTicket = value.scheduler.RetainContinuingOutputMemory(JobTicket{ticket.slot, ticket.generation + 1},
                                                                         kOpening, refused);
        value.overLimit = value.scheduler.RetainContinuingOutputMemory(ticket, kOpening + 1, refused);
        value.undercharged = value.opening->BindContinuingOutputForPublication(
            value.scheduler, ticket,
            value.opening->LocalAllocationBytes() + SharedPublicationMemory::OwnershipAllowance() - 1);
        value.openingResult = value.opening->BindContinuingOutputForPublication(value.scheduler, ticket, kOpening);
        value.repeated = value.scheduler.RetainContinuingOutputMemory(ticket, kOpening, refused);
        value.growth = value.scheduler.IncreaseReservation(ticket, kWhole);
        value.openingSealed = !value.opening->TryAccountLocalAllocation(1);
        value.reservationAfterOpening = value.scheduler.ReservedMemoryBytes();
        value.queriesAfter = queries;
        value.openingAck = true;
        if (FAILED(value.openingResult))
            return false;
        if (value.mode == Mode::CloseSuspended)
            return true;
        if (value.mode == Mode::CancelAfterOpening)
        {
            value.resumed = value.scheduler.Cancel(ticket) && value.scheduler.Resume(ticket);
            return true;
        }
        value.resumed = value.scheduler.Resume(ticket);
        return value.resumed;
    }
    void Completed(JobResult result) noexcept override
    {
        auto& value = _state;
        value.completed = true;
        value.outcome = result;
        if (result.status == JobStatus::Finished && value.runs == 2)
            value.terminalResult = value.finalOutput->BindPersistentOutputAfterCompletedWork(
                value.scheduler, value.original, kFinal, *value.temporary);
        else
            value.terminalResult = value.temporary->BindForRetirement(value.scheduler, value.original);
        value.terminalBytes = value.temporary->Bytes();
    }
    void RetainJoinedRetirement(JobScheduler&, JobTicket ticket) noexcept override
    {
        auto& value = _state;
        value.sameTicket = value.sameTicket && ticket == value.original;
        value.joinedClose = true;
        value.terminalResult = value.temporary->BindForRetirement(value.scheduler, ticket);
        value.terminalBytes = value.temporary->Bytes();
    }

  private:
    State& _state;
};
void WaitForOpening(State& state)
{
    const auto deadline = GetTickCount64() + 10000;
    while (!state.openingAck)
    {
        if (PumpMessages().quit)
            SetupFailure("unexpected-quit");
        (void)DrainCompletions(state.scheduler);
        if (state.openingAck)
            break;
        const auto now = GetTickCount64();
        if (now >= deadline)
            SetupFailure("opening-ack-deadline");
        const HANDLE wake = state.scheduler.WakeHandle();
        if (MsgWaitForMultipleObjectsEx(1, &wake, static_cast<DWORD>(deadline - now), QS_ALLINPUT,
                                        MWMO_INPUTAVAILABLE) == WAIT_FAILED)
            SetupFailure("opening-ack-event");
    }
}
class NeverSubmittedJob final : public Job
{
  public:
    JobResult Run(JobContext&) override
    {
        return {};
    }
    void Completed(JobResult) noexcept override {}
};
void DescriptorRefusals()
{
    JobScheduler scheduler;
    if (scheduler.Initialize(2, {}, {Sample, nullptr}) != S_OK)
        SetupFailure("descriptor-scheduler");
    std::unique_ptr<Job> job = std::make_unique<NeverSubmittedJob>();
    const auto* original = job.get();
    JobTicket ticket;
    JobOptions options;
    options.memoryBytes = kWhole;
    options.continuingOutputLimit = kOpening;
    const auto before = queries;
    Expect(scheduler.Submit(job, options, ticket) == E_INVALIDARG && job.get() == original &&
               scheduler.ReservedMemoryBytes() == 0,
           "descriptor-without-joined-retirement-and-terminal-slot-refuses-before-admission");
    options.deferredRetirement = true;
    Expect(scheduler.Submit(job, options, ticket) == E_INVALIDARG && job.get() == original &&
               scheduler.ReservedMemoryBytes() == 0,
           "descriptor-without-preclaimed-terminal-slot-refuses-before-admission");
    options.reserveRetainedMemory = true;
    options.continuingOutputLimit = kWhole;
    Expect(scheduler.Submit(job, options, ticket) == E_INVALIDARG && job.get() == original &&
               scheduler.ReservedMemoryBytes() == 0,
           "descriptor-without-remaining-terminal-bytes-refuses-before-admission");
    Expect(queries == before && scheduler.IsIdle(),
           "invalid-descriptors-never-query-memory-or-allocate-a-worker-record");
    scheduler.Close();
}
void RunCase(Mode mode)
{
    currentCase = mode == Mode::Success              ? "success"
                  : mode == Mode::FailSecond         ? "worker-failure"
                  : mode == Mode::CancelAfterOpening ? "cancel-after-opening"
                                                     : "joined-close";
    JobScheduler scheduler;
    if (scheduler.Initialize(2, {}, {Sample, nullptr}) != S_OK || !scheduler.EnableDeferredRelease())
        SetupFailure("actual-two-worker-scheduler");
    State state{scheduler, mode};
    JobOptions options;
    options.kind = JobClass::Selection;
    options.serialKey = 27;
    options.memoryBytes = kWhole;
    options.deferredRetirement = true;
    options.reserveRetainedMemory = true;
    options.allowMemoryWaiting = false;
    options.continuingOutputLimit = kOpening;
    std::unique_ptr<Job> job = std::make_unique<ContinuingJob>(state);
    if (scheduler.Submit(job, options, state.original) != S_OK)
        SetupFailure("original-whole-admission");
    WaitForOpening(state);
    Expect(state.openingResult == S_OK && state.openingAck && state.openingValue && *state.openingValue == 17,
           "joined-opening-publishes-literal-under-original-admission");
    Expect(FAILED(state.wrongTicket) && FAILED(state.overLimit) && FAILED(state.undercharged) &&
               FAILED(state.repeated) && FAILED(state.growth) && state.openingSealed,
           "wrong-ticket-bound-undercharge-repeat-and-growth-refuse-with-sealed-output");
    Expect(state.firstBudget == kWhole && state.reservationAfterOpening == kWhole &&
               state.opening->Bytes() == kOpening && state.queriesBefore == state.queriesAfter &&
               state.opening->BoundTo(scheduler, state.original),
           "opening-transfer-keeps-whole-charge-and-performs-no-new-query");
    if (mode == Mode::CloseSuspended)
    {
        scheduler.Close();
        Expect(state.joinedClose && state.sameTicket && state.terminalResult == S_OK &&
                   state.terminalBytes == kWhole - kOpening && state.opening->IsBound() && state.temporary->IsBound() &&
                   state.opening->ResidentBytes() == kWhole,
               "joined-close-keeps-original-terminal-slot-and-complete-live-root-charge");
        state.openingValue.reset();
        state.finalValue.reset();
        state.finalOutput.reset();
        state.opening.reset();
        state.temporary.reset();
        return;
    }
    Settle(scheduler);
    const bool success = mode == Mode::Success;
    const auto status = success                    ? JobStatus::Finished
                        : mode == Mode::FailSecond ? JobStatus::Failed
                                                   : JobStatus::Cancelled;
    Expect(state.completed && state.sameTicket && state.resumed && state.outcome.status == status &&
               state.terminalResult == S_OK,
           "same-original-ticket-acknowledges-success-failure-or-cancel-and-retains-terminal-memory");
    if (mode != Mode::CancelAfterOpening)
        Expect(state.runs == 2 && state.secondBudget == kWhole - kOpening && state.finalValue &&
                   *state.finalValue == 41,
               "resumed-useful-worker-receives-only-prepaid-remaining-reservation");
    else
        Expect(state.runs == 1 && !state.finalValue, "cancel-after-opening-never-enters-another-useful-body");
    const auto finalBytes = success ? kFinal : 0;
    Expect(scheduler.ReservedMemoryBytes() == kWhole && state.terminalBytes == kWhole - kOpening - finalBytes,
           "terminal-accounting-partitions-exact-original-remainder-without-another-slot");
    if (!success)
    {
        state.finalValue.reset();
        state.finalOutput.reset();
    }
    state.temporary.reset();
    Settle(scheduler);
    Expect(scheduler.ReservedMemoryBytes() == kOpening + finalBytes,
           "temporary-refund-keeps-each-still-readable-output-charged");
    std::weak_ptr<const int> weak = state.openingValue;
    state.openingValue.reset();
    state.opening.reset();
    Settle(scheduler);
    Expect(weak.expired() && scheduler.ReservedMemoryBytes() == kOpening + finalBytes,
           "last-weak-object-control-keeps-opening-charge-after-all-strong-readers-release");
    weak.reset();
    Settle(scheduler);
    Expect(scheduler.ReservedMemoryBytes() == finalBytes, "last-weak-control-refunds-only-original-opening-output");
    state.finalValue.reset();
    state.finalOutput.reset();
    Settle(scheduler);
    Expect(scheduler.ReservedMemoryBytes() == 0 && scheduler.ReleasingMemoryBytes() == 0 && scheduler.IsIdle(),
           "last-terminal-root-and-real-retirement-acknowledgement-refund-all-bytes");
    scheduler.Close();
}

class SlotHolderJob final : public Job
{
  public:
    explicit SlotHolderJob(unsigned int& acknowledgements) noexcept : _acknowledgements(acknowledgements) {}
    JobResult Run(JobContext&) override
    {
        return {};
    }
    bool DeferCompletion(JobResult, JobTicket) noexcept override
    {
        if (_held)
            return false;
        _held = true;
        ++_acknowledgements;
        return true;
    }
    void Completed(JobResult) noexcept override {}

  private:
    unsigned int& _acknowledgements;
    bool _held = false;
};
void TestAllSlotsPreclaimed()
{
    currentCase = "slot-pressure";
    JobScheduler scheduler;
    if (scheduler.Initialize(2, {}, {Sample, nullptr}) != S_OK || !scheduler.EnableDeferredRelease())
        SetupFailure("slot-pressure-scheduler");
    constexpr std::uint64_t holderBytes = 8192;
    std::array<JobTicket, JobScheduler::kRetainedMemoryCapacity - 1> holders{};
    unsigned int acknowledgements = 0;
    for (auto& ticket : holders)
    {
        std::unique_ptr<Job> holder = std::make_unique<SlotHolderJob>(acknowledgements);
        JobOptions options;
        options.memoryBytes = holderBytes;
        options.deferredRetirement = true;
        options.reserveRetainedMemory = true;
        if (scheduler.Submit(holder, options, ticket) != S_OK)
            SetupFailure("preclaim-sixty-three-terminal-slots");
    }
    State state{scheduler, Mode::CloseSuspended};
    JobOptions options;
    options.kind = JobClass::Selection;
    options.memoryBytes = kWhole;
    options.deferredRetirement = true;
    options.reserveRetainedMemory = true;
    options.continuingOutputLimit = kOpening;
    std::unique_ptr<Job> original = std::make_unique<ContinuingJob>(state);
    if (scheduler.Submit(original, options, state.original) != S_OK)
        SetupFailure("preclaim-last-terminal-slot-before-opening");
    WaitForOpening(state);
    const auto deadline = GetTickCount64() + 10000;
    while (acknowledgements != holders.size())
    {
        if (PumpMessages().quit)
            SetupFailure("holder-unexpected-quit");
        (void)DrainCompletions(scheduler);
        if (acknowledgements == holders.size())
            break;
        const auto now = GetTickCount64();
        if (now >= deadline)
            SetupFailure("holder-acknowledgement-deadline");
        const HANDLE wake = scheduler.WakeHandle();
        if (MsgWaitForMultipleObjectsEx(1, &wake, static_cast<DWORD>(deadline - now), QS_ALLINPUT,
                                        MWMO_INPUTAVAILABLE) == WAIT_FAILED)
            SetupFailure("holder-acknowledgement-event");
    }
    Expect(state.openingResult == S_OK && state.opening->BoundTo(scheduler, state.original) &&
               state.reservationAfterOpening == kWhole + holders.size() * holderBytes,
           "opening-partitions-only-original-bytes-with-all-sixty-four-terminal-slots-preclaimed");
    std::unique_ptr<Job> overflow = std::make_unique<NeverSubmittedJob>();
    const auto* untouched = overflow.get();
    JobTicket refusedTicket;
    auto overflowOptions = options;
    overflowOptions.continuingOutputLimit = 0;
    const auto before = queries;
    const auto overflowStatus = scheduler.Submit(overflow, overflowOptions, refusedTicket);
    Expect(overflowStatus == static_cast<std::int32_t>(HRESULT_FROM_WIN32(ERROR_NOT_ENOUGH_QUOTA)) &&
               overflow.get() == untouched && queries == before,
           "sixty-fifth-slot-refuses-without-query-or-original-ticket-mutation");
    SharedRetainedMemoryLease refused;
    Expect(scheduler.RetainContinuingOutputMemory(state.original, kOpening, refused) == E_INVALIDARG && !refused,
           "owner-call-outside-joined-acknowledgement-cannot-transfer-an-output");
    if (!scheduler.Resume(state.original))
        SetupFailure("resume-original-under-full-slot-pressure");
    for (const auto ticket : holders)
        if (!scheduler.Resume(ticket))
            SetupFailure("release-original-slot-holder");
    Settle(scheduler);
    Expect(state.completed && state.runs == 2 && state.sameTicket && state.outcome.status == JobStatus::Finished &&
               state.secondBudget == kWhole - kOpening && state.terminalResult == S_OK,
           "same-original-job-resumes-useful-work-and-binds-its-preclaimed-terminal-slot");
    state.temporary.reset();
    Settle(scheduler);
    Expect(scheduler.ReservedMemoryBytes() == kOpening + kFinal && scheduler.IsIdle(),
           "all-other-slots-and-original-temporary-retire-with-both-outputs-still-charged");
    state.openingValue.reset();
    state.finalValue.reset();
    state.opening.reset();
    state.finalOutput.reset();
    Settle(scheduler);
    Expect(scheduler.ReservedMemoryBytes() == 0 && scheduler.ReleasingMemoryBytes() == 0 && scheduler.IsIdle(),
           "final-readers-refund-every-original-reservation-after-slot-pressure");
    scheduler.Close();
}
} // namespace
int RunDocumentContinuingPublicationTests()
{
    failures = checks = 0;
    queries = 0;
    DescriptorRefusals();
    RunCase(Mode::Success);
    RunCase(Mode::FailSecond);
    RunCase(Mode::CancelAfterOpening);
    RunCase(Mode::CloseSuspended);
    TestAllSlotsPreclaimed();
    std::printf("SUMMARY continuing-publication checks=%u failures=%u\n", checks, failures);
    return static_cast<int>(failures);
}
