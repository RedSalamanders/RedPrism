#include "RetainedJobMemoryTests.h"

#include "../../Common/Jobs.h"

#include <windows.h>

#include <array>
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
constexpr std::uint64_t kBytes = 4096;
constexpr std::uint64_t kAvailable = kMemoryAdmissionHeadroomBytes + 1024 * 1024;

void Expect(bool condition, const char* name, int& failures) noexcept
{
    std::printf("%s retained-job-memory.%s\n", condition ? "PASS" : "FAIL", name);
    failures += condition ? 0 : 1;
}

[[noreturn]] void SetupFailure(const char* name) noexcept
{
    std::printf("FAIL retained-job-memory.%s\n", name);
    std::fflush(stdout);
    std::_Exit(1);
}

wil::unique_handle Event()
{
    wil::unique_handle event{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    if (!event)
    {
        std::printf("FAIL retained-job-memory.create-event\n");
        std::fflush(stdout);
        std::_Exit(1);
    }
    return event;
}

void Wait(HANDLE event, const char* name) noexcept
{
    if (!event || WaitForSingleObject(event, 10000) != WAIT_OBJECT_0)
    {
        std::printf("FAIL retained-job-memory.timeout.%s\n", name);
        std::fflush(stdout);
        std::_Exit(1);
    }
}

void Settle(JobScheduler& scheduler, const char* name, int& failures) noexcept
{
    static_cast<void>(failures);
    const SettleResult result = SettleJobs(scheduler, 10000);
    if (result != SettleResult::Settled)
    {
        std::printf("FAIL retained-job-memory.%s\n", name);
        std::fflush(stdout);
        std::_Exit(1);
    }
}

struct MemoryProbe final
{
    unsigned int samples = 0;
};

bool SampleMemory(void* context, MemorySnapshot& snapshot) noexcept
{
    if (context)
        ++static_cast<MemoryProbe*>(context)->samples;
    snapshot = {kAvailable, kAvailable};
    return true;
}

struct CallbackState final
{
    JobScheduler* scheduler = nullptr;
    JobTicket ticket{};
    RetainedMemoryTicket retained{};
    RetainedMemoryTicket second{};
    std::int32_t result = E_UNEXPECTED;
    std::int32_t secondResult = E_UNEXPECTED;
    unsigned int callbacks = 0;
    unsigned int runs = 0;
    JobResult completedResult{};
    unsigned int samplesBeforeCallback = 0;
    unsigned int* samples = nullptr;
    bool attempt = true;
    bool attemptWrongGeneration = false;
    bool attemptTwice = false;
    bool sawReservation = false;
    bool noAdditionalSample = false;
};

class BasicJob final : public Job
{
  public:
    BasicJob(CallbackState& state, JobResult result = {}) noexcept : _state(state), _result(result) {}
    JobResult Run(JobContext&) override
    {
        ++_state.runs;
        return _result;
    }
    void Completed(JobResult result) noexcept override
    {
        ++_state.callbacks;
        _state.completedResult = result;
        _state.sawReservation = _state.scheduler->ReservedMemoryBytes() == kBytes;
        if (_state.attempt)
        {
            JobTicket exact = _state.ticket;
            if (_state.attemptWrongGeneration)
                ++exact.generation;
            _state.result = _state.scheduler->RetainMemory(exact, _state.retained);
            if (_state.attemptTwice)
                _state.secondResult = _state.scheduler->RetainMemory(_state.ticket, _state.second);
            if (_state.samples)
                _state.noAdditionalSample = *_state.samples == _state.samplesBeforeCallback;
        }
        _state.sawReservation = _state.sawReservation || (result.status != JobStatus::Finished &&
                                                          _state.scheduler->ReservedMemoryBytes() == kBytes);
    }

  private:
    CallbackState& _state;
    JobResult _result;
};

bool Submit(JobScheduler& scheduler, CallbackState& state, std::uint64_t bytes, int& failures, const char* name,
            bool allowMemoryWaiting = true) noexcept
{
    std::unique_ptr<Job> job = std::make_unique<BasicJob>(state);
    JobTicket ticket;
    const auto hr = scheduler.Submit(job, {JobClass::Background, 0, bytes, false, allowMemoryWaiting}, ticket);
    static_cast<void>(name);
    static_cast<void>(failures);
    if (SUCCEEDED(hr))
        state.ticket = ticket;
    return SUCCEEDED(hr) && !job;
}

void TestTransferAndCallbackScope(int& failures)
{
    JobScheduler scheduler;
    MemoryProbe memory;
    if (FAILED(scheduler.Initialize(1, {}, {SampleMemory, &memory})))
    {
        SetupFailure("transfer.initialize");
    }
    CallbackState state;
    state.scheduler = &scheduler;
    state.samples = &memory.samples;
    const bool submitted = Submit(scheduler, state, kBytes, failures, "transfer.submit");
    Expect(submitted, "transfer.submit", failures);
    if (!submitted)
    {
        scheduler.Close();
        return;
    }
    state.samplesBeforeCallback = memory.samples;
    Settle(scheduler, "transfer.settles", failures);
    Expect(state.callbacks == 1 && state.result == S_OK && state.retained.owner != 0,
           "transfer.successful-completion-creates-token", failures);
    Expect(state.sawReservation && scheduler.ReservedMemoryBytes() == kBytes, "transfer-moves-the-original-reservation",
           failures);
    Expect(scheduler.IsIdle(), "transfer-retained-bytes-do-not-prevent-idle", failures);
    Expect(state.noAdditionalSample && memory.samples == state.samplesBeforeCallback,
           "transfer-does-not-sample-memory-again", failures);
    const auto outsideResult = scheduler.RetainMemory(state.ticket, state.second);
    Expect(FAILED(outsideResult) && state.second.owner == 0, "transfer-outside-completion-callback-refused", failures);
    Expect(scheduler.ReleaseRetainedMemory(state.retained) && scheduler.ReservedMemoryBytes() == 0,
           "transfer-release-drops-original-accounting", failures);
    Expect(!scheduler.ReleaseRetainedMemory(state.retained) && scheduler.ReservedMemoryBytes() == 0,
           "transfer-double-release-is-harmless", failures);
    scheduler.Close();
}

// A single worker's third, held job proves both predecessors are completed before owner callbacks begin.
// The forbidden calls each use a real OTHER completed record; mere "inCallback" is not transfer authority.
void TestExactFinalCallbackAuthority(int& failures)
{
    for (unsigned int kind = 0; kind != 4; ++kind)
    {
        JobScheduler scheduler;
        if (FAILED(scheduler.Initialize(1, {}, {SampleMemory, nullptr})))
            SetupFailure("callback-authority.initialize");
        auto entered = Event();
        auto release = Event();
        struct State final
        {
            JobScheduler& scheduler;
            unsigned int kind;
            JobTicket source, target;
            RetainedMemoryTicket illegal, sourceCache, targetCache;
            unsigned int attempts = 0;
            std::int32_t illegalResult = E_UNEXPECTED, sourceResult = E_UNEXPECTED, targetResult = E_UNEXPECTED;
            void Attempt() noexcept
            {
                ++attempts;
                illegalResult = scheduler.RetainMemory(target, illegal);
            }
        } state{scheduler, kind};
        class Source final : public Job
        {
          public:
            explicit Source(State& state) noexcept : _state(state) {}
            JobResult Run(JobContext& context) override
            {
                if (_state.kind == 1)
                    context.ReportProgress(20);
                return {};
            }
            void Progress(unsigned int) noexcept override
            {
                _state.Attempt();
            }
            void Completed(JobResult) noexcept override
            {
                if (_state.kind >= 2)
                    _state.Attempt();
                _state.sourceResult = _state.scheduler.RetainMemory(_state.source, _state.sourceCache);
            }

          private:
            State& _state;
        };
        class Target final : public Job
        {
          public:
            explicit Target(State& state) noexcept : _state(state) {}
            JobResult Run(JobContext&) override
            {
                return {};
            }
            void Completed(JobResult) noexcept override
            {
                _state.targetResult = _state.scheduler.RetainMemory(_state.target, _state.targetCache);
            }

          private:
            State& _state;
        };
        class OwnerPump final : public OwnerWorkPump
        {
          public:
            explicit OwnerPump(State& state) noexcept : _state(state), _ready(state.kind == 0) {}
            bool HasPending() const noexcept override
            {
                return _ready;
            }
            bool HasReady() const noexcept override
            {
                return _ready;
            }
            void Pump() noexcept override
            {
                _ready = false;
                _state.Attempt();
            }

          private:
            State& _state;
            bool _ready;
        } pump{state};
        struct Gate final
        {
            HANDLE entered, release;
        } gate{entered.get(), release.get()};
        class Barrier final : public Job
        {
          public:
            explicit Barrier(Gate& gate) noexcept : _gate(gate) {}
            JobResult Run(JobContext&) override
            {
                (void)SetEvent(_gate.entered);
                Wait(_gate.release, "callback-authority.barrier");
                return {};
            }
            void Completed(JobResult) noexcept override {}

          private:
            Gate& _gate;
        };
        if (!scheduler.RegisterOwnerWork(pump))
            SetupFailure("callback-authority.register-owner");
        std::unique_ptr<Job> source = std::make_unique<Source>(state);
        std::unique_ptr<Job> target = std::make_unique<Target>(state);
        std::unique_ptr<Job> barrier = std::make_unique<Barrier>(gate);
        JobTicket barrierTicket;
        if (FAILED(scheduler.Submit(source, {JobClass::Background, 0, kBytes}, state.source)) ||
            FAILED(scheduler.Submit(target, {JobClass::Background, 0, kBytes}, state.target)) ||
            FAILED(scheduler.Submit(barrier, {JobClass::Background, 0, 0}, barrierTicket)))
            SetupFailure("callback-authority.submit");
        Wait(entered.get(), "callback-authority.both-predecessors-completed");
        if (kind == 3)
            (void)DrainCompletions(scheduler,
                                   [](void*, Job& job, JobResult result) noexcept { job.Completed(result); });
        else
            (void)DrainCompletions(scheduler);
        (void)SetEvent(release.get());
        Settle(scheduler, "callback-authority.settles", failures);
        if (!scheduler.UnregisterOwnerWork(pump))
            SetupFailure("callback-authority.unregister-owner");
        const auto check = [&](bool condition, const char* ownerName, const char* progressName, const char* foreignName,
                               const char* applyName) noexcept
        {
            Expect(condition,
                   kind == 0   ? ownerName
                   : kind == 1 ? progressName
                   : kind == 2 ? foreignName
                               : applyName,
                   failures);
        };
        check(state.attempts == 1 && FAILED(state.illegalResult) && state.illegal.owner == 0,
              "callback-authority.owner-pump-cannot-transfer-completed-record",
              "callback-authority.progress-cannot-transfer-completed-record",
              "callback-authority.final-cannot-transfer-another-completed-record",
              "callback-authority.custom-apply-cannot-transfer-another-completed-record");
        check(state.sourceResult == S_OK && state.sourceCache.owner != 0,
              "callback-authority.owner-refusal-preserves-source-transfer",
              "callback-authority.progress-refusal-preserves-source-transfer",
              "callback-authority.foreign-refusal-preserves-source-transfer",
              "callback-authority.custom-apply-refusal-preserves-source-transfer");
        check(state.targetResult == S_OK && state.targetCache.owner != 0,
              "callback-authority.owner-refusal-preserves-target-transfer",
              "callback-authority.progress-refusal-preserves-target-transfer",
              "callback-authority.foreign-refusal-preserves-target-transfer",
              "callback-authority.custom-apply-refusal-preserves-target-transfer");
        check(scheduler.ReservedMemoryBytes() == 2 * kBytes && scheduler.IsIdle(),
              "callback-authority.owner-preserves-two-original-charges-and-idle",
              "callback-authority.progress-preserves-two-original-charges-and-idle",
              "callback-authority.foreign-preserves-two-original-charges-and-idle",
              "callback-authority.custom-apply-preserves-two-original-charges-and-idle");
        for (const auto token : {state.illegal, state.sourceCache, state.targetCache})
            if (token.owner && !scheduler.ReleaseRetainedMemory(token))
                SetupFailure("callback-authority.cleanup-token");
        check(scheduler.ReservedMemoryBytes() == 0, "callback-authority.owner-cleanup-zero",
              "callback-authority.progress-cleanup-zero", "callback-authority.foreign-cleanup-zero",
              "callback-authority.custom-apply-cleanup-zero");
        scheduler.Close();
    }
}

void TestRefusedTransferStates(int& failures)
{
    JobScheduler scheduler;
    if (FAILED(scheduler.Initialize(1, {}, {SampleMemory, nullptr})))
    {
        SetupFailure("refusal.initialize");
    }
    CallbackState failed;
    failed.scheduler = &scheduler;
    std::unique_ptr<Job> failedJob = std::make_unique<BasicJob>(failed, JobResult{JobStatus::Failed, E_FAIL});
    JobTicket failedTicket;
    (void)scheduler.Submit(failedJob, {JobClass::Background, 0, kBytes}, failedTicket);
    failed.ticket = failedTicket;
    Settle(scheduler, "refusal.failed-job-settles", failures);
    Expect(FAILED(failed.result) && failed.sawReservation && failed.retained.owner == 0 &&
               scheduler.ReservedMemoryBytes() == 0,
           "refusal.failed-result-keeps-no-token-or-reservation", failures);

    CallbackState zero;
    zero.scheduler = &scheduler;
    std::unique_ptr<Job> zeroJob = std::make_unique<BasicJob>(zero);
    JobTicket zeroTicket;
    (void)scheduler.Submit(zeroJob, {JobClass::Background, 0, 0}, zeroTicket);
    zero.ticket = zeroTicket;
    Settle(scheduler, "refusal.zero-job-settles", failures);
    Expect(FAILED(zero.result) && zero.retained.owner == 0, "refusal.zero-byte-reservation-cannot-be-retained",
           failures);

    CallbackState wrongGeneration;
    wrongGeneration.scheduler = &scheduler;
    wrongGeneration.attemptWrongGeneration = true;
    Expect(Submit(scheduler, wrongGeneration, kBytes, failures, "refusal.generation-submit"),
           "refusal.generation-ticket-ready", failures);
    Settle(scheduler, "refusal.generation-settles", failures);
    Expect(FAILED(wrongGeneration.result) && wrongGeneration.retained.owner == 0 &&
               scheduler.ReservedMemoryBytes() == 0,
           "refusal.wrong-job-generation-does-not-transfer", failures);

    CallbackState repeat;
    repeat.scheduler = &scheduler;
    repeat.attemptTwice = true;
    Expect(Submit(scheduler, repeat, kBytes, failures, "refusal.repeat-submit"), "refusal.repeat-ticket-ready",
           failures);
    Settle(scheduler, "refusal.repeat-settles", failures);
    Expect(repeat.result == S_OK && FAILED(repeat.secondResult) && repeat.second.owner == 0 &&
               scheduler.ReservedMemoryBytes() == kBytes,
           "refusal.repeat-transfer-does-not-create-an-extra-token", failures);
    Expect(scheduler.ReleaseRetainedMemory(repeat.retained), "refusal.repeat-cleanup", failures);
    scheduler.Close();

    JobScheduler cancelledScheduler;
    if (FAILED(cancelledScheduler.Initialize(1, {}, {SampleMemory, nullptr})))
    {
        SetupFailure("refusal.cancel.initialize");
    }
    auto entered = Event();
    auto release = Event();
    struct Gate final
    {
        HANDLE entered;
        HANDLE release;
    } gate{entered.get(), release.get()};
    class GateJob final : public Job
    {
      public:
        explicit GateJob(Gate& gate) noexcept : _gate(gate) {}
        JobResult Run(JobContext&) override
        {
            (void)SetEvent(_gate.entered);
            Wait(_gate.release, "cancel.blocker");
            return {};
        }
        void Completed(JobResult) noexcept override {}

      private:
        Gate& _gate;
    };
    std::unique_ptr<Job> blocker = std::make_unique<GateJob>(gate);
    JobTicket blockerTicket;
    (void)cancelledScheduler.Submit(blocker, {JobClass::Background, 0, 0}, blockerTicket);
    Wait(entered.get(), "cancel.blocker-entered");
    CallbackState cancelled;
    cancelled.scheduler = &cancelledScheduler;
    Expect(Submit(cancelledScheduler, cancelled, kBytes, failures, "refusal.cancel.job-submit"),
           "refusal.cancel.ticket-ready", failures);
    Expect(cancelledScheduler.Cancel(cancelled.ticket), "refusal.cancel.request-accepted", failures);
    (void)SetEvent(release.get());
    Settle(cancelledScheduler, "refusal.cancel.settles", failures);
    Expect(FAILED(cancelled.result) && cancelled.retained.owner == 0 && cancelledScheduler.ReservedMemoryBytes() == 0,
           "refusal.cancelled-result-cannot-transfer", failures);
    cancelledScheduler.Close();
}

void TestTokenOwnershipAndGeneration(int& failures)
{
    JobScheduler first;
    JobScheduler second;
    if (FAILED(first.Initialize(1, {}, {SampleMemory, nullptr})) ||
        FAILED(second.Initialize(1, {}, {SampleMemory, nullptr})))
    {
        SetupFailure("token.initialize-pair");
    }
    CallbackState source;
    source.scheduler = &first;
    (void)Submit(first, source, kBytes, failures, "token.source-submit");
    Settle(first, "token.source-settles", failures);
    const auto sourceToken = source.retained;

    CallbackState foreign;
    foreign.scheduler = &second;
    (void)Submit(second, foreign, kBytes, failures, "token.foreign-submit");
    Settle(second, "token.foreign.settles", failures);
    Expect(!second.ReleaseRetainedMemory(sourceToken) && second.ReservedMemoryBytes() == kBytes,
           "token.foreign-scheduler-cannot-release", failures);
    Expect(first.ReservedMemoryBytes() == kBytes && second.ReservedMemoryBytes() == kBytes,
           "token.foreign-refusal-preserves-both-schedulers", failures);
    Expect(first.ReleaseRetainedMemory(sourceToken), "token.source-release", failures);

    CallbackState recycled;
    recycled.scheduler = &first;
    (void)Submit(first, recycled, kBytes, failures, "token.recycled-submit");
    Settle(first, "token.recycled-settles", failures);
    Expect(recycled.retained.slot == sourceToken.slot && recycled.retained.generation > sourceToken.generation,
           "token.recycled-slot-advances-generation", failures);
    Expect(!first.ReleaseRetainedMemory(sourceToken) && first.ReservedMemoryBytes() == kBytes,
           "token.stale-generation-cannot-release-recycled-slot", failures);
    RetainedMemoryTicket defaults{};
    Expect(!first.ReleaseRetainedMemory(defaults) && first.ReservedMemoryBytes() == kBytes,
           "token.default-token-cannot-release", failures);
    RetainedMemoryTicket wrongGeneration = recycled.retained;
    ++wrongGeneration.generation;
    Expect(!first.ReleaseRetainedMemory(wrongGeneration) && first.ReservedMemoryBytes() == kBytes,
           "token.future-generation-cannot-release", failures);
    Expect(first.ReleaseRetainedMemory(recycled.retained), "token.recycled-valid-release", failures);
    Expect(!first.ReleaseRetainedMemory(recycled.retained) && first.ReservedMemoryBytes() == 0,
           "token.recycled-double-release-preserves-zero", failures);
    first.Close();
    second.Close();
}

struct CapacityState final
{
    JobScheduler* scheduler = nullptr;
    std::array<RetainedMemoryTicket, JobScheduler::kRetainedMemoryCapacity> tokens{};
    std::size_t count = 0;
    bool overflowRefused = false;
    bool overflowKeptReservation = false;
};

class CapacityJob final : public Job
{
  public:
    CapacityJob(CapacityState& state, std::size_t ordinal) noexcept : _state(state), _ordinal(ordinal) {}
    JobResult Run(JobContext&) override
    {
        return {};
    }
    void Completed(JobResult) noexcept override
    {
        if (_ordinal < _state.tokens.size())
        {
            if (SUCCEEDED(_state.scheduler->RetainMemory(_ticket, _state.tokens[_ordinal])))
                ++_state.count;
        }
        else
        {
            RetainedMemoryTicket overflow{};
            _state.overflowRefused = FAILED(_state.scheduler->RetainMemory(_ticket, overflow)) && overflow.owner == 0;
            _state.overflowKeptReservation =
                _state.scheduler->ReservedMemoryBytes() == (JobScheduler::kRetainedMemoryCapacity + 1) * kBytes;
        }
    }
    void SetTicket(JobTicket ticket) noexcept
    {
        _ticket = ticket;
    }

  private:
    CapacityState& _state;
    std::size_t _ordinal;
    JobTicket _ticket{};
};

void TestCapacityRefusal(int& failures)
{
    JobScheduler scheduler;
    if (FAILED(scheduler.Initialize(2, {}, {SampleMemory, nullptr})))
    {
        SetupFailure("capacity.initialize");
    }
    CapacityState state;
    state.scheduler = &scheduler;
    bool submittedAll = true;
    for (std::size_t i = 0; i < JobScheduler::kRetainedMemoryCapacity; ++i)
    {
        std::unique_ptr<Job> job = std::make_unique<CapacityJob>(state, i);
        auto* const jobPointer = static_cast<CapacityJob*>(job.get());
        JobTicket ticket;
        const auto hr = scheduler.Submit(job, {JobClass::Background, 0, kBytes}, ticket);
        if (FAILED(hr))
        {
            submittedAll = false;
            break;
        }
        jobPointer->SetTicket(ticket);
    }
    Expect(submittedAll, "capacity.submit-first-64-reserved-jobs", failures);
    Settle(scheduler, "capacity.first-64-completions-settle", failures);
    Expect(state.count == JobScheduler::kRetainedMemoryCapacity &&
               scheduler.ReservedMemoryBytes() == JobScheduler::kRetainedMemoryCapacity * kBytes,
           "capacity.first-64-fill-live-accounting", failures);
    std::unique_ptr<Job> overflowJob = std::make_unique<CapacityJob>(state, JobScheduler::kRetainedMemoryCapacity);
    auto* const overflowJobPointer = static_cast<CapacityJob*>(overflowJob.get());
    JobTicket overflowTicket;
    const auto overflowSubmit = scheduler.Submit(overflowJob, {JobClass::Background, 0, kBytes}, overflowTicket);
    Expect(SUCCEEDED(overflowSubmit), "capacity.submit-overflow-job", failures);
    if (SUCCEEDED(overflowSubmit))
        overflowJobPointer->SetTicket(overflowTicket);
    Settle(scheduler, "capacity.overflow-completion-settles", failures);
    Expect(state.overflowRefused, "capacity-65th-retain-refused", failures);
    Expect(state.overflowKeptReservation &&
               scheduler.ReservedMemoryBytes() == JobScheduler::kRetainedMemoryCapacity * kBytes,
           "capacity.refusal-preserves-active-reservation-then-normal-retirement-releases-it", failures);
    bool releasedAll = true;
    for (const auto token : state.tokens)
        releasedAll = scheduler.ReleaseRetainedMemory(token) && releasedAll;
    Expect(releasedAll, "capacity.release-all-retained-slots", failures);
    Expect(scheduler.ReservedMemoryBytes() == 0, "capacity.all-accounting-released", failures);
    scheduler.Close();
}

void TestReleaseWakesWaitingAdmission(int& failures)
{
    JobScheduler scheduler;
    MemoryProbe memory;
    const auto sample = [](void* context, MemorySnapshot& snapshot) noexcept
    {
        ++static_cast<MemoryProbe*>(context)->samples;
        snapshot = {kMemoryAdmissionHeadroomBytes + kBytes, kMemoryAdmissionHeadroomBytes + kBytes};
        return true;
    };
    if (FAILED(scheduler.Initialize(1, {}, {sample, &memory})))
    {
        SetupFailure("wake.initialize");
    }
    CallbackState retained;
    retained.scheduler = &scheduler;
    (void)Submit(scheduler, retained, kBytes, failures, "wake.retain-job-submit");
    Settle(scheduler, "wake.retain-job-settles", failures);
    CallbackState waiter;
    waiter.scheduler = &scheduler;
    waiter.attempt = false;
    (void)Submit(scheduler, waiter, kBytes, failures, "wake.waiter-submit");
    Expect(scheduler.ReservedMemoryBytes() == kBytes && waiter.callbacks == 0,
           "wake.waiter-stays-admission-blocked-by-retained-reservation", failures);
    Expect(scheduler.ReleaseRetainedMemory(retained.retained), "wake.release-retained-reservation", failures);
    Settle(scheduler, "wake.notification-retries-waiting-admission", failures);
    Expect(waiter.callbacks == 1 && waiter.result == E_UNEXPECTED && scheduler.ReservedMemoryBytes() == 0,
           "wake.waiter-completes-after-release-notification", failures);
    scheduler.Close();
}

void TestReplacementRefusesRetainedCacheWait(int& failures)
{
    JobScheduler scheduler;
    MemoryProbe memory;
    const auto sample = [](void* context, MemorySnapshot& snapshot) noexcept
    {
        ++static_cast<MemoryProbe*>(context)->samples;
        snapshot = {kMemoryAdmissionHeadroomBytes + kBytes, kMemoryAdmissionHeadroomBytes + kBytes};
        return true;
    };
    if (FAILED(scheduler.Initialize(1, {}, {sample, &memory})))
        SetupFailure("replacement.initialize");
    CallbackState retained;
    retained.scheduler = &scheduler;
    if (!Submit(scheduler, retained, kBytes, failures, "replacement.cache-submit"))
        SetupFailure("replacement.cache-submit");
    Settle(scheduler, "replacement.cache-settles", failures);
    Expect(retained.retained.owner != 0 && retained.runs == 1 && scheduler.ReservedMemoryBytes() == kBytes,
           "replacement.original-cache-keeps-its-full-reservation", failures);
    CallbackState replacement;
    replacement.scheduler = &scheduler;
    replacement.attempt = false;
    const auto before = memory.samples;
    Expect(Submit(scheduler, replacement, kBytes, failures, "replacement.submit", false),
           "replacement.insufficient-cache-replacement-accepts-one-original-ticket", failures);
    Settle(scheduler, "replacement.refusal-settles", failures);
    Expect(replacement.runs == 0 && replacement.callbacks == 1 &&
               replacement.completedResult.status == JobStatus::Failed &&
               replacement.completedResult.error == E_OUTOFMEMORY && memory.samples == before + 1,
           "replacement.refuses-once-without-running-or-reserving", failures);
    Expect(scheduler.IsIdle() && scheduler.ReservedMemoryBytes() == kBytes && scheduler.ReleasingMemoryBytes() == 0,
           "replacement.refusal-preserves-the-original-cache-and-idle", failures);
    scheduler.NotifyMemoryChanged();
    Settle(scheduler, "replacement.memory-event-settles", failures);
    Expect(replacement.callbacks == 1 && replacement.runs == 0 && memory.samples == before + 1,
           "replacement.memory-event-never-rearms-a-refused-ticket", failures);
    Expect(scheduler.ReleaseRetainedMemory(retained.retained), "replacement.exact-cache-release", failures);
    CallbackState retry;
    retry.scheduler = &scheduler;
    retry.attempt = false;
    if (!Submit(scheduler, retry, kBytes, failures, "replacement.explicit-retry-submit", false))
        SetupFailure("replacement.explicit-retry-submit");
    Settle(scheduler, "replacement.explicit-retry-settles", failures);
    Expect(retry.runs == 1 && retry.callbacks == 1 && retry.completedResult.status == JobStatus::Finished &&
               scheduler.ReservedMemoryBytes() == 0 && scheduler.IsIdle(),
           "replacement.new-explicit-ticket-runs-when-whole-peak-fits", failures);
    scheduler.Close();
}

struct CloseState final
{
    JobScheduler* scheduler = nullptr;
    RetainedMemoryTicket released{};
    bool ownerCalled = false;
    bool releaseWorked = false;
    std::uint64_t reservationDuringHook = 0;
};

void ReleaseOnClose(void* context) noexcept
{
    auto& state = *static_cast<CloseState*>(context);
    state.ownerCalled = true;
    state.reservationDuringHook = state.scheduler->ReservedMemoryBytes();
    state.releaseWorked = state.scheduler->ReleaseRetainedMemory(state.released);
}

void TestCloseCleanup(int& failures)
{
    JobScheduler scheduler;
    if (FAILED(scheduler.Initialize(1, {}, {SampleMemory, nullptr})))
    {
        SetupFailure("close.initialize");
    }
    CallbackState released;
    released.scheduler = &scheduler;
    (void)Submit(scheduler, released, kBytes, failures, "close.owner-cleanup-job-submit");
    Settle(scheduler, "close.owner-cleanup-job-settles", failures);
    CallbackState residual;
    residual.scheduler = &scheduler;
    (void)Submit(scheduler, residual, kBytes, failures, "close.residual-job-submit");
    Settle(scheduler, "close.residual-job-settles", failures);
    CloseState state{&scheduler, released.retained};
    scheduler.Close({ReleaseOnClose, nullptr, &state});
    Expect(state.ownerCalled && state.releaseWorked && state.reservationDuringHook == 2 * kBytes,
           "close.owner-hook-can-release-after-producer-cleanup", failures);
    Expect(scheduler.ReservedMemoryBytes() == 0 && !scheduler.ReleaseRetainedMemory(released.retained) &&
               !scheduler.ReleaseRetainedMemory(residual.retained),
           "close.invalidates-released-and-residual-tokens", failures);
}
} // namespace

int RunRetainedJobMemoryTests()
{
    int failures = 0;
    TestTransferAndCallbackScope(failures);
    TestExactFinalCallbackAuthority(failures);
    TestRefusedTransferStates(failures);
    TestTokenOwnershipAndGeneration(failures);
    TestCapacityRefusal(failures);
    TestReleaseWakesWaitingAdmission(failures);
    TestReplacementRefusesRetainedCacheWait(failures);
    TestCloseCleanup(failures);
    std::printf("%s retained-job-memory.summary failures=%d\n", failures == 0 ? "PASS" : "FAIL", failures);
    return failures;
}
