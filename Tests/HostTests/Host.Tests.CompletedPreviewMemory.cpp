#include "Host.Tests.CompletedPreviewMemory.h"

#include "../../Common/Jobs.h"
#include "../../Common/SharedPublicationMemory.h"

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>

namespace
{
constexpr std::uint64_t kAdmissionBytes = 1024ull * 1024;
constexpr std::uint64_t kAvailableBytes = kMemoryAdmissionHeadroomBytes + 4ull * 1024 * 1024;
constexpr std::uint32_t kTimeoutMs = 10000;
unsigned int gPassed = 0;
unsigned int gFailed = 0;

void Expect(bool condition, const char* name) noexcept
{
    std::printf("%s completed-preview-memory.%s\n", condition ? "PASS" : "FAIL", name);
    std::fflush(stdout);
    condition ? ++gPassed : ++gFailed;
}

[[noreturn]] void SetupFailure(const char* name, std::int32_t result = 0) noexcept
{
    std::printf("FAIL completed-preview-memory.setup.%s result=%ld\n", name, static_cast<long>(result));
    std::fflush(stdout);
    std::_Exit(1);
}

bool SampleMemory(void*, MemorySnapshot& snapshot) noexcept
{
    snapshot = {kAvailableBytes, kAvailableBytes};
    return true;
}

void Start(JobScheduler& scheduler)
{
    if (scheduler.Initialize(1, {}, {&SampleMemory, nullptr}) != S_OK)
        SetupFailure("scheduler-initialize");
}

JobTicket WrongGeneration(JobTicket ticket) noexcept
{
    ++ticket.generation;
    return ticket;
}

struct SuccessfulState final
{
    JobScheduler* scheduler = nullptr;
    JobTicket ticket{};
    std::shared_ptr<SharedPublicationMemory> publication;
    std::shared_ptr<SharedPublicationMemory> outsideProbe;
    JobResult completion{JobStatus::Failed};
    std::int32_t wrongGenerationResult = E_UNEXPECTED;
    std::int32_t bindResult = E_UNEXPECTED;
    std::int32_t duplicateResult = E_UNEXPECTED;
    std::uint64_t callbackChargeBefore = 0;
    std::uint64_t callbackChargeAfter = 0;
    unsigned int callbacks = 0;
};

class SuccessfulPreviewJob final : public Job
{
  public:
    explicit SuccessfulPreviewJob(SuccessfulState& state) noexcept : _state(state) {}

    JobResult Run(JobContext&) override
    {
        // Exercise the worker-only preallocation path with the real scheduler worker.
        _state.publication = SharedPublicationMemory::CreateForWork();
        _state.outsideProbe = SharedPublicationMemory::CreateForWork();
        return {};
    }

    void Completed(JobResult result) noexcept override
    {
        ++_state.callbacks;
        _state.completion = result;
        _state.callbackChargeBefore = _state.scheduler->ReservedMemoryBytes();
        _state.wrongGenerationResult =
            _state.publication->BindAfterCompletedWork(*_state.scheduler, WrongGeneration(_state.ticket));
        _state.bindResult = _state.publication->BindAfterCompletedWork(*_state.scheduler, _state.ticket);
        _state.duplicateResult = _state.publication->BindAfterCompletedWork(*_state.scheduler, _state.ticket);
        _state.callbackChargeAfter = _state.scheduler->ReservedMemoryBytes();
    }

  private:
    SuccessfulState& _state;
};

struct FailedState final
{
    JobScheduler* scheduler = nullptr;
    JobTicket ticket{};
    std::shared_ptr<SharedPublicationMemory> publication;
    JobResult completion{};
    std::int32_t bindResult = E_UNEXPECTED;
    std::uint64_t callbackCharge = 0;
    unsigned int callbacks = 0;
};

class FailedPreviewJob final : public Job
{
  public:
    explicit FailedPreviewJob(FailedState& state) noexcept : _state(state) {}

    JobResult Run(JobContext&) override
    {
        _state.publication = SharedPublicationMemory::CreateForWork();
        return {JobStatus::Failed, E_FAIL};
    }

    void Completed(JobResult result) noexcept override
    {
        ++_state.callbacks;
        _state.completion = result;
        _state.callbackCharge = _state.scheduler->ReservedMemoryBytes();
        _state.bindResult = _state.publication->BindAfterCompletedWork(*_state.scheduler, _state.ticket);
    }

  private:
    FailedState& _state;
};

void Submit(JobScheduler& scheduler, std::unique_ptr<Job>& job, JobTicket& ticket)
{
    JobOptions options{};
    options.kind = JobClass::Background;
    options.memoryBytes = kAdmissionBytes;
    options.reserveRetainedMemory = true;
    if (scheduler.Submit(job, options, ticket) != S_OK || job)
        SetupFailure("job-submit");
}

void Settle(JobScheduler& scheduler)
{
    if (SettleJobs(scheduler, kTimeoutMs) != SettleResult::Settled)
        SetupFailure("jobs-settle");
}

void TestSuccessfulCompletedTransferAndReaders()
{
    JobScheduler scheduler;
    JobScheduler foreignScheduler;
    Start(scheduler);
    Start(foreignScheduler);

    SuccessfulState state;
    state.scheduler = &scheduler;
    std::unique_ptr<Job> job = std::make_unique<SuccessfulPreviewJob>(state);
    Submit(scheduler, job, state.ticket);
    Settle(scheduler);

    Expect(state.callbacks == 1 && state.completion.status == JobStatus::Finished &&
               state.wrongGenerationResult == E_INVALIDARG && state.bindResult == S_OK &&
               state.duplicateResult == E_INVALIDARG,
           "completed-original-generation-binds-once-after-wrong-generation-refusal");
    Expect(state.callbackChargeBefore == kAdmissionBytes && state.callbackChargeAfter == kAdmissionBytes &&
               state.publication->IsBound() && state.publication->Bytes() == kAdmissionBytes &&
               state.publication->ResidentBytes() == kAdmissionBytes &&
               scheduler.ReservedMemoryBytes() == kAdmissionBytes,
           "successful-completed-callback-transfers-whole-literal-1mib-admission");
    Expect(state.publication->BoundTo(scheduler, state.ticket) &&
               !state.publication->BoundTo(scheduler, WrongGeneration(state.ticket)) &&
               !state.publication->BoundTo(foreignScheduler, state.ticket),
           "bound-provenance-requires-original-ticket-and-live-origin-scheduler");

    const std::uint64_t chargeBeforeOutside = scheduler.ReservedMemoryBytes();
    const std::int32_t outsideResult = state.outsideProbe->BindAfterCompletedWork(scheduler, state.ticket);
    Expect(outsideResult == E_INVALIDARG && !state.outsideProbe->IsBound() && state.outsideProbe->Bytes() == 0 &&
               state.outsideProbe->ResidentBytes() == 0 && scheduler.ReservedMemoryBytes() == chargeBeforeOutside &&
               chargeBeforeOutside == kAdmissionBytes,
           "outside-completed-callback-refusal-preserves-unbound-owner-and-charge");

    std::weak_ptr<SharedPublicationMemory> weakReader = state.publication;
    std::shared_ptr<SharedPublicationMemory> foreignReader = state.publication;
    state.publication.reset();
    Expect(foreignReader->IsBound() && foreignReader->Bytes() == kAdmissionBytes && !weakReader.expired() &&
               scheduler.ReservedMemoryBytes() == kAdmissionBytes,
           "foreign-strong-reader-keeps-whole-accounting-lease-after-original-owner-reset");

    foreignReader.reset();
    Expect(weakReader.expired() && scheduler.ReservedMemoryBytes() == kAdmissionBytes,
           "weak-control-block-keeps-whole-charge-after-last-strong-reader");
    weakReader.reset();
    Expect(scheduler.ReservedMemoryBytes() == kAdmissionBytes,
           "last-weak-control-release-publishes-owner-ack-without-early-refund");
    (void)DrainCompletions(scheduler);
    Expect(scheduler.ReservedMemoryBytes() == 0, "owner-drain-releases-charge-after-final-accounting-control-is-gone");

    state.outsideProbe.reset();
    foreignScheduler.Close();
    scheduler.Close();
}

void TestFailedJobCannotBind()
{
    JobScheduler scheduler;
    Start(scheduler);

    FailedState state;
    state.scheduler = &scheduler;
    std::unique_ptr<Job> job = std::make_unique<FailedPreviewJob>(state);
    Submit(scheduler, job, state.ticket);
    Settle(scheduler);

    Expect(state.callbacks == 1 && state.completion.status == JobStatus::Failed && state.completion.error == E_FAIL &&
               state.bindResult == static_cast<std::int32_t>(HRESULT_FROM_WIN32(ERROR_INVALID_STATE)) &&
               !state.publication->IsBound() && state.publication->Bytes() == 0 &&
               state.publication->ResidentBytes() == 0,
           "failed-completed-job-refuses-bind-and-leaves-owner-empty");
    Expect(state.callbackCharge == kAdmissionBytes && scheduler.ReservedMemoryBytes() == 0,
           "failed-job-reservation-lives-through-callback-then-retires-without-transfer");

    state.publication.reset();
    scheduler.Close();
}
} // namespace

int RunCompletedPreviewMemoryTests()
{
    TestSuccessfulCompletedTransferAndReaders();
    TestFailedJobCannotBind();
    std::printf("SUMMARY completed-preview-memory passed=%u failed=%u controls=%u\n", gPassed, gFailed,
                gPassed + gFailed);
    std::fflush(stdout);
    return gFailed == 0 ? 0 : 1;
}
