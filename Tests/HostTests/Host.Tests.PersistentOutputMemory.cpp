#include "Host.Tests.PersistentOutputMemory.h"

#include "Jobs.h"
#include "SharedPublicationMemory.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <thread>
#include <windows.h>
#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/resource.h>
#pragma warning(pop)

namespace
{
constexpr std::uint64_t kWhole = 128 * 1024;
constexpr std::uint64_t kOutput = 96 * 1024;
constexpr std::size_t kLiveOutputs = 110;
int failures = 0;
std::uint64_t memoryQueries = 0;

void Expect(bool condition, const char* label)
{
    std::printf("%s persistent-output.%s\n", condition ? "PASS" : "FAIL", label);
    failures += condition ? 0 : 1;
}
[[noreturn]] void SetupFailure(const char* label)
{
    std::printf("FAIL persistent-output.setup.%s\n", label);
    std::_Exit(1);
}
bool Sample(void*, MemorySnapshot& snapshot) noexcept
{
    ++memoryQueries;
    snapshot = {kMemoryAdmissionHeadroomBytes + 512 * 1024 * 1024, kMemoryAdmissionHeadroomBytes + 512 * 1024 * 1024};
    return true;
}
void Start(JobScheduler& scheduler)
{
    if (scheduler.Initialize(2, {}, {Sample, nullptr}) != S_OK || !scheduler.EnableDeferredRelease())
        SetupFailure("scheduler-start");
}
void Settle(JobScheduler& scheduler)
{
    if (SettleJobs(scheduler, 10000) != SettleResult::Settled)
        SetupFailure("scheduler-settle");
}

enum class Authority
{
    Completed,
    Publication,
    Retirement,
    JoinedClose
};
enum class Invalid
{
    None,
    Zero,
    Whole,
    Alias,
    WrongTicket,
    StaleTicket,
    Repeat,
    Failure,
    Cancelled
};
struct State final
{
    JobScheduler* scheduler = nullptr;
    JobTicket ticket{};
    JobTicket stale{};
    std::shared_ptr<SharedPublicationMemory> output;
    std::shared_ptr<SharedPublicationMemory> temporary;
    Authority authority = Authority::Completed;
    Invalid invalid = Invalid::None;
    std::uint64_t outputBytes = kOutput;
    bool holdWorker = false;
    HANDLE entered = nullptr;
    HANDLE release = nullptr;
    std::int32_t bindResult = E_UNEXPECTED;
    std::int32_t secondResult = S_OK;
    std::int32_t workerError = S_OK;
    JobResult completed{};
    std::uint64_t queriesBefore = 0;
    std::uint64_t queriesAfter = 0;
    SharedRetainedMemoryLease directOutput;
    SharedRetainedMemoryLease directTemporary;
    bool directLease = false;
    bool callbackSeen = false;
    bool joinedSeen = false;
};

class PersistentOutputJob final : public Job
{
  public:
    explicit PersistentOutputJob(State& state) : _state(state) {}
    JobResult Run(JobContext&) override
    {
        _state.output = SharedPublicationMemory::CreateForWork();
        _state.temporary = SharedPublicationMemory::CreateForWork();
        if (_state.holdWorker)
        {
            (void)SetEvent(_state.entered);
            if (WaitForSingleObject(_state.release, 10000) != WAIT_OBJECT_0)
                SetupFailure("worker-release-gate");
        }
        return _state.workerError == S_OK ? JobResult{} : JobResult{JobStatus::Failed, _state.workerError};
    }
    bool DeferRetirement(JobResult, JobTicket ticket) noexcept override
    {
        _state.ticket = ticket;
        return false;
    }
    bool DeferCompletion(JobResult, JobTicket ticket) noexcept override
    {
        _state.ticket = ticket;
        if (_state.authority != Authority::Publication)
            return false;
        Bind(Authority::Publication);
        return false;
    }
    void Completed(JobResult result) noexcept override
    {
        _state.completed = result;
        if (_state.authority == Authority::Completed || _state.authority == Authority::Retirement)
            Bind(_state.authority);
    }
    void RetainJoinedRetirement(JobScheduler&, JobTicket ticket) noexcept override
    {
        _state.ticket = ticket;
        _state.joinedSeen = true;
        Bind(Authority::JoinedClose);
    }

  private:
    void Bind(Authority authority) noexcept
    {
        _state.callbackSeen = true;
        const JobTicket ticket = _state.invalid == Invalid::StaleTicket ? _state.stale
                                 : _state.invalid == Invalid::WrongTicket
                                     ? JobTicket{_state.ticket.slot, _state.ticket.generation + 1}
                                     : _state.ticket;
        auto& temporary = _state.invalid == Invalid::Alias ? *_state.output : *_state.temporary;
        if (authority == Authority::Completed && _state.completed.status != JobStatus::Finished)
        {
            _state.queriesBefore = memoryQueries;
            _state.bindResult = _state.output->BindPersistentOutputAfterCompletedWork(*_state.scheduler, ticket,
                                                                                      _state.outputBytes, temporary);
            _state.queriesAfter = memoryQueries;
            return;
        }
        if (authority == Authority::Completed)
        {
            _state.queriesBefore = memoryQueries;
            _state.bindResult = _state.directLease
                                    ? _state.scheduler->RetainPersistentOutputMemory(
                                          ticket, _state.outputBytes, _state.directOutput, _state.directTemporary)
                                    : _state.output->BindPersistentOutputAfterCompletedWork(
                                          *_state.scheduler, ticket, _state.outputBytes, temporary);
            _state.queriesAfter = memoryQueries;
        }
        else if (authority == Authority::Publication)
            _state.bindResult = _state.output->BindPersistentOutputForPublication(*_state.scheduler, ticket,
                                                                                  _state.outputBytes, temporary);
        else if (authority == Authority::Retirement)
            _state.bindResult = _state.output->BindPersistentOutputForRetirement(*_state.scheduler, ticket,
                                                                                 _state.outputBytes, temporary);
        else
            _state.bindResult = _state.output->BindPersistentOutputForRetirement(*_state.scheduler, ticket,
                                                                                 _state.outputBytes, temporary);

        if (_state.invalid == Invalid::Repeat && _state.bindResult == S_OK)
            _state.secondResult = _state.output->BindPersistentOutputAfterCompletedWork(
                *_state.scheduler, _state.ticket, _state.outputBytes, *_state.temporary);
    }
    State& _state;
};

void Submit(JobScheduler& scheduler, State& state)
{
    state.scheduler = &scheduler;
    std::unique_ptr<Job> job = std::make_unique<PersistentOutputJob>(state);
    JobOptions options;
    options.memoryBytes = kWhole;
    options.reserveRetainedMemory = true;
    options.deferredRetirement = true;
    if (scheduler.Submit(job, options, state.ticket) != S_OK || job)
        SetupFailure("submit");
}
void Drop(State& state, JobScheduler& scheduler)
{
    state.temporary.reset();
    state.output.reset();
    Settle(scheduler);
}

void TestSequentialSlotReuse()
{
    JobScheduler scheduler;
    Start(scheduler);
    std::array<State, kLiveOutputs> states{};
    for (auto& state : states)
    {
        Submit(scheduler, state);
        Settle(scheduler);
        if (state.bindResult != S_OK || !state.output || !state.temporary)
            SetupFailure("sequential-bind");
        state.temporary.reset();
        Settle(scheduler);
    }
    Expect(scheduler.ReservedMemoryBytes() == kLiveOutputs * kOutput,
           "110-live-outputs-reuse-64-retained-slots-after-temporary-drain");
    bool allResident = true;
    for (const auto& state : states)
        allResident = allResident && state.output->Bytes() == kOutput;
    Expect(allResident && states[0].output->ResidentBytes() == kLiveOutputs * kOutput,
           "every-output-retains-exact-bytes-and-aggregate-residency-after-temp-drain");
    Expect(states.back().queriesBefore == states.back().queriesAfter, "persistent-transfer-does-not-resample-memory");

    wil::unique_handle start{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    wil::unique_handle done{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    wil::unique_handle readyEvent{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    if (!start || !done || !readyEvent)
        SetupFailure("release-events");
    std::array<std::thread, 4> releasers;
    std::atomic<unsigned int> ready{0};
    for (std::size_t worker = 0; worker < releasers.size(); ++worker)
    {
        releasers[worker] = std::thread(
            [&, worker]
            {
                if (ready.fetch_add(1, std::memory_order_acq_rel) + 1 == releasers.size())
                    (void)SetEvent(readyEvent.get());
                if (WaitForSingleObject(start.get(), 10000) != WAIT_OBJECT_0)
                    SetupFailure("release-start-gate");
                for (std::size_t index = worker; index < states.size(); index += releasers.size())
                    states[index].output.reset();
                if (ready.fetch_add(1, std::memory_order_acq_rel) + 1 == 2 * releasers.size())
                    (void)SetEvent(done.get());
            });
    }
    // Event gate: wait for every releaser to arrive without polling or sleeping.
    if (WaitForSingleObject(readyEvent.get(), 10000) != WAIT_OBJECT_0)
        SetupFailure("release-ready-gate");
    (void)SetEvent(start.get());
    if (WaitForSingleObject(done.get(), 10000) != WAIT_OBJECT_0)
        SetupFailure("release-done-gate");
    for (auto& worker : releasers)
        worker.join();
    const auto beforeAck = scheduler.ReservedMemoryBytes();
    Settle(scheduler);
    Expect(beforeAck == kLiveOutputs * kOutput, "off-owner-last-releases-do-not-refund-before-owner-ack");
    Expect(scheduler.ReservedMemoryBytes() == 0 && scheduler.IsIdle(),
           "concurrent-off-owner-output-releases-drain-to-exact-zero");

    // Reuse the old temporary slot, then release its older output while the new generation is live.
    State old;
    Submit(scheduler, old);
    Settle(scheduler);
    old.temporary.reset();
    Settle(scheduler);
    State newer;
    Submit(scheduler, newer);
    Settle(scheduler);
    old.output.reset();
    Settle(scheduler);
    Expect(scheduler.ReservedMemoryBytes() == kWhole && newer.output->ResidentBytes() == kWhole,
           "old-output-ack-after-temporary-slot-reuse-preserves-new-generation");
    newer.temporary.reset();
    Settle(scheduler);
    Expect(scheduler.ReservedMemoryBytes() == kOutput, "reused-slot-temporary-ack-leaves-new-output-only");
    Drop(newer, scheduler);
    Expect(scheduler.ReservedMemoryBytes() == 0, "reused-generation-cleanup-returns-exact-zero");
    scheduler.Close();
}

void TestAuthoritiesAndJoinedClose()
{
    for (const auto authority : {Authority::Completed, Authority::Publication, Authority::Retirement})
    {
        JobScheduler scheduler;
        Start(scheduler);
        State state;
        state.authority = authority;
        if (authority == Authority::Retirement)
            state.workerError = E_FAIL;
        Submit(scheduler, state);
        Settle(scheduler);
        Expect(state.callbackSeen && state.bindResult == S_OK && state.output->Bytes() == kOutput,
               authority == Authority::Completed     ? "completed-authority-retains-output"
               : authority == Authority::Publication ? "publication-authority-retains-output"
                                                     : "retirement-authority-retains-failed-work");
        const char* transferLabel =
            authority == Authority::Completed     ? "completed-transfer-does-not-refund-before-temporary-ack"
            : authority == Authority::Publication ? "publication-transfer-does-not-refund-before-temporary-ack"
                                                  : "retirement-transfer-does-not-refund-before-temporary-ack";
        Expect(scheduler.ReservedMemoryBytes() == kWhole && state.output->ResidentBytes() == kWhole, transferLabel);
        state.temporary.reset();
        Settle(scheduler);
        const char* temporaryLabel =
            authority == Authority::Completed     ? "completed-temporary-ack-keeps-persistent-output-charge"
            : authority == Authority::Publication ? "publication-temporary-ack-keeps-persistent-output-charge"
                                                  : "retirement-temporary-ack-keeps-persistent-output-charge";
        Expect(scheduler.ReservedMemoryBytes() == kOutput && state.output->ResidentBytes() == kOutput, temporaryLabel);
        Drop(state, scheduler);
        const char* finalLabel = authority == Authority::Completed ? "completed-final-output-acknowledges-exact-refund"
                                 : authority == Authority::Publication
                                     ? "publication-final-output-acknowledges-exact-refund"
                                     : "retirement-final-output-acknowledges-exact-refund";
        Expect(scheduler.ReservedMemoryBytes() == 0, finalLabel);
        scheduler.Close();
    }
    {
        JobScheduler scheduler;
        Start(scheduler);
        State state;
        state.authority = Authority::JoinedClose;
        Submit(scheduler, state);
        if (WaitForSingleObject(static_cast<HANDLE>(scheduler.WakeHandle()), 10000) != WAIT_OBJECT_0)
            SetupFailure("close-worker-wake");
        scheduler.Close(); // joins worker, then invokes the real RetainJoinedRetirement hook
        Expect(state.joinedSeen && state.bindResult == S_OK && state.output->Bytes() == kOutput,
               "actual-joined-close-retirement-authority-binds-output");
        Expect(state.output->ResidentBytes() == kWhole, "joined-close-output-keeps-independent-residency");
        state.temporary.reset();
        Expect(state.output->ResidentBytes() == kOutput, "joined-close-temp-release-preserves-output");
        state.output.reset();
    }
}

void TestRefusalsAndGenerationIsolation()
{
    const std::array<Invalid, 6> invalids{Invalid::Zero,        Invalid::Whole,   Invalid::Alias,
                                          Invalid::WrongTicket, Invalid::Failure, Invalid::Repeat};
    for (const auto invalid : invalids)
    {
        JobScheduler scheduler;
        Start(scheduler);
        State state;
        state.invalid = invalid;
        if (invalid == Invalid::Zero)
            state.outputBytes = 0;
        if (invalid == Invalid::Whole)
            state.outputBytes = kWhole;
        if (invalid == Invalid::Failure)
            state.workerError = E_FAIL;
        Submit(scheduler, state);
        Settle(scheduler);
        const bool expectedSuccess = invalid == Invalid::Repeat;
        const std::int32_t expectedFailure = static_cast<std::int32_t>(
            invalid == Invalid::Zero || invalid == Invalid::Alias ? E_INVALIDARG
                                                                  : HRESULT_FROM_WIN32(ERROR_INVALID_STATE));
        Expect(expectedSuccess ? state.bindResult == S_OK && state.secondResult == E_INVALIDARG
                               : state.bindResult == expectedFailure,
               invalid == Invalid::Zero          ? "zero-output-refused"
               : invalid == Invalid::Whole       ? "whole-reservation-output-refused"
               : invalid == Invalid::Alias       ? "aliased-output-temporary-refused"
               : invalid == Invalid::WrongTicket ? "wrong-generation-ticket-refused"
               : invalid == Invalid::Failure     ? "successful-authority-refuses-failed-work"
               : invalid == Invalid::Repeat      ? "nonempty-output-destination-refused"
                                                 : "cancelled-work-refused");
        Expect(invalid == Invalid::Repeat ? scheduler.ReservedMemoryBytes() == kWhole
                                          : scheduler.ReservedMemoryBytes() == 0,
               invalid == Invalid::Zero          ? "zero-refusal-preserves-accounting"
               : invalid == Invalid::Whole       ? "whole-refusal-preserves-accounting"
               : invalid == Invalid::Alias       ? "alias-refusal-preserves-accounting"
               : invalid == Invalid::WrongTicket ? "wrong-ticket-refusal-preserves-accounting"
               : invalid == Invalid::Failure     ? "failed-work-refusal-preserves-accounting"
                                                 : "repeat-refusal-preserves-accounting");
        Drop(state, scheduler);
        scheduler.Close();
    }

    {
        JobScheduler cancelledScheduler;
        Start(cancelledScheduler);
        wil::unique_handle entered{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
        wil::unique_handle release{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
        if (!entered || !release)
            SetupFailure("cancel-events");
        State cancelled;
        cancelled.holdWorker = true;
        cancelled.entered = entered.get();
        cancelled.release = release.get();
        Submit(cancelledScheduler, cancelled);
        if (WaitForSingleObject(entered.get(), 10000) != WAIT_OBJECT_0)
            SetupFailure("cancel-worker-entered");
        Expect(cancelledScheduler.Cancel(cancelled.ticket), "running-original-cancellation-accepted");
        (void)SetEvent(release.get());
        Settle(cancelledScheduler);
        Expect(cancelled.completed.status == JobStatus::Cancelled && cancelled.bindResult < 0 &&
                   cancelledScheduler.ReservedMemoryBytes() == 0,
               "successful-authority-refuses-cancelled-work");
        Drop(cancelled, cancelledScheduler);
        cancelledScheduler.Close();
    }

    JobScheduler scheduler;
    Start(scheduler);
    State old;
    Submit(scheduler, old);
    Settle(scheduler);
    old.stale = old.ticket;
    Drop(old, scheduler);
    State current;
    current.invalid = Invalid::StaleTicket;
    current.stale = old.ticket;
    Submit(scheduler, current);
    Settle(scheduler);
    Expect(current.bindResult < 0 && !current.output->IsBound() && scheduler.ReservedMemoryBytes() == 0,
           "stale-ticket-from-retired-job-refused");
    Drop(current, scheduler);
    scheduler.Close();
}

void TestClosedEndpointIsolationAndGrowth()
{
    std::shared_ptr<SharedPublicationMemory> oldOutput;
    std::shared_ptr<SharedPublicationMemory> oldTemporary;
    {
        JobScheduler oldScheduler;
        Start(oldScheduler);
        State old;
        Submit(oldScheduler, old);
        Settle(oldScheduler);
        oldOutput = old.output;
        oldTemporary = old.temporary;
        oldScheduler.Close();
        old.output.reset();
        old.temporary.reset();
    }
    JobScheduler replacement;
    Start(replacement);
    State fresh;
    fresh.directLease = true;
    Submit(replacement, fresh);
    Settle(replacement);
    Expect(fresh.bindResult == S_OK && fresh.directOutput.Bytes() == kOutput && oldOutput->ResidentBytes() == kWhole,
           "closed-endpoint-foreign-output-cannot-alias-new-budget");
    const auto queries = memoryQueries;
    Expect(fresh.bindResult == S_OK && replacement.IncreaseRetainedReservation(fresh.directOutput, kOutput + 1) < 0 &&
               memoryQueries == queries && fresh.directOutput.Bytes() == kOutput,
           "persistent-output-growth-refuses-without-query-or-charge-change");
    oldOutput.reset();
    oldTemporary.reset();
    Expect(replacement.ReservedMemoryBytes() == kWhole && fresh.directOutput.ResidentBytes() == kWhole,
           "foreign-old-endpoint-release-cannot-change-reinitialized-budget");
    fresh.directOutput.Reset();
    fresh.directTemporary.Reset();
    Settle(replacement);
    replacement.Close();
}
} // namespace

int RunPersistentOutputMemoryTests()
{
    failures = 0;
    TestSequentialSlotReuse();
    TestAuthoritiesAndJoinedClose();
    TestRefusalsAndGenerationIsolation();
    TestClosedEndpointIsolationAndGrowth();
    std::printf("%s persistent-output.summary failures=%d\n", failures ? "FAIL" : "PASS", failures);
    return failures;
}
