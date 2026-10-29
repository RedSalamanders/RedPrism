#include "Host.Tests.ReservationGrowth.h"

#include "Jobs.h"

#include <array>
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

#include <windows.h>

namespace
{
constexpr DWORD kFailureTimeoutMs = 10000;
constexpr std::uint64_t kInitialReservation = 4096;
constexpr std::uint64_t kGrownReservation = 8192;
constexpr std::uint64_t kFinalReservation = 12288;
constexpr std::uint64_t kWideAvailable = kMemoryAdmissionHeadroomBytes + 4 * 1024 * 1024;
constexpr std::uint64_t kSerialKey = 0x52455347524F5754ULL;

struct Checks final
{
    int failures = 0;

    void Expect(const char* name, bool passed) noexcept
    {
        std::printf("%s reservation-growth.%s\n", passed ? "PASS" : "FAIL", name);
        std::fflush(stdout);
        failures += passed ? 0 : 1;
    }

    void Setup(const char* name, bool passed) noexcept
    {
        std::printf("%s reservation-growth.setup.%s\n", passed ? "PASS" : "FAIL", name);
        std::fflush(stdout);
        failures += passed ? 0 : 1;
    }
};

wil::unique_handle Event()
{
    return wil::unique_handle{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
}

// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027)
struct MemoryProbe final
{
    MemorySnapshot snapshot{kWideAvailable, kWideAvailable};
    std::atomic<unsigned int> samples{0};
    bool fail = false; // Owner-only: MemorySource is sampled only by owner operations.
    JobScheduler* resumeScheduler = nullptr;
    JobTicket resumeTicket{};
    bool resumeDuringNextSample = false;
    bool resumeResult = false;

    static bool Sample(void* context, MemorySnapshot& output) noexcept
    {
        auto& probe = *static_cast<MemoryProbe*>(context);
        probe.samples.fetch_add(1, std::memory_order_relaxed);
        if (probe.resumeDuringNextSample)
        {
            probe.resumeDuringNextSample = false;
            probe.resumeResult = probe.resumeScheduler && probe.resumeScheduler->Resume(probe.resumeTicket);
        }
        if (probe.fail)
            return false;
        output = probe.snapshot;
        return true;
    }
};
#pragma warning(pop)

// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027)
struct StageState final
{
    StageState()
        : firstEntered(Event()), firstRelease(Event()), suspended(Event()), secondEntered(Event()),
          secondRelease(Event()), completed(Event()), successorEntered(Event()), successorCompleted(Event())
    {
    }

    [[nodiscard]] bool EventsValid() const noexcept
    {
        return firstEntered && firstRelease && suspended && secondEntered && secondRelease && completed &&
               successorEntered && successorCompleted;
    }

    JobScheduler* scheduler = nullptr;
    wil::unique_handle firstEntered;
    wil::unique_handle firstRelease;
    wil::unique_handle suspended;
    wil::unique_handle secondEntered;
    wil::unique_handle secondRelease;
    wil::unique_handle completed;
    wil::unique_handle successorEntered;
    wil::unique_handle successorCompleted;
    std::atomic<unsigned int> runs{0};
    std::atomic<std::uint64_t> producedBytes{0};
    std::atomic<std::uint64_t> secondStageReservation{0};
    std::atomic<bool> acknowledgementObserved{false};
    std::atomic<bool> finalCallbackPublished{false};
    JobTicket ticket{};
    JobStatus finalStatus = JobStatus::Failed;
    unsigned int deferrals = 0;
    unsigned int completions = 0;
    std::int32_t retainResult = E_UNEXPECTED;
    RetainedMemoryTicket retained{};
    std::uint64_t chargeBeforeRetain = 0;
    std::uint64_t chargeAfterRetain = 0;
};
#pragma warning(pop)

// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4626 5027)
class TwoStageJob final : public Job
{
  public:
    explicit TwoStageJob(StageState& state) noexcept : _state(state) {}

    JobResult Run(JobContext& context) override
    {
        const unsigned int ordinal = _state.runs.fetch_add(1, std::memory_order_relaxed);
        if (ordinal == 0)
        {
            (void)SetEvent(_state.firstEntered.get());
            if (WaitForSingleObject(_state.firstRelease.get(), kFailureTimeoutMs) != WAIT_OBJECT_0)
                return {JobStatus::Failed, E_ABORT};
            _state.producedBytes.store(0xA17E5EEDULL, std::memory_order_release);
            _state.scheduler->NotifyOwnerWork();
            return {};
        }

        _state.secondStageReservation.store(context.reservedMemoryBytes, std::memory_order_release);
        (void)SetEvent(_state.secondEntered.get());
        if (WaitForSingleObject(_state.secondRelease.get(), kFailureTimeoutMs) != WAIT_OBJECT_0)
            return {JobStatus::Failed, E_ABORT};
        return {};
    }

    bool DeferCompletion(JobResult result, JobTicket ticket) noexcept override
    {
        if (result.status != JobStatus::Finished || _state.runs.load(std::memory_order_acquire) != 1)
            return false;
        ++_state.deferrals;
        _state.ticket = ticket;
        _state.acknowledgementObserved.store(_state.producedBytes.load(std::memory_order_acquire) == 0xA17E5EEDULL,
                                             std::memory_order_release);
        (void)SetEvent(_state.suspended.get());
        return true;
    }

    void Completed(JobResult result) noexcept override
    {
        ++_state.completions;
        _state.finalStatus = result.status;
        _state.chargeBeforeRetain = _state.scheduler->ReservedMemoryBytes();
        if (result.status == JobStatus::Finished)
            _state.retainResult = _state.scheduler->RetainMemory(_state.ticket, _state.retained);
        _state.chargeAfterRetain = _state.scheduler->ReservedMemoryBytes();
        _state.finalCallbackPublished.store(true, std::memory_order_release);
        (void)SetEvent(_state.completed.get());
    }

  private:
    StageState& _state;
};
#pragma warning(pop)

// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027)
struct SuccessorState final
{
    explicit SuccessorState(StageState& predecessor) noexcept : predecessor_(predecessor) {}
    StageState& predecessor_;
    std::atomic<bool> entered{false};
    std::atomic<bool> sawFinalCallback{false};
    std::atomic<unsigned int> completions{0};
};
#pragma warning(pop)

// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4626 5027)
class SuccessorJob final : public Job
{
  public:
    explicit SuccessorJob(SuccessorState& state) noexcept : _state(state) {}
    JobResult Run(JobContext&) override
    {
        _state.sawFinalCallback.store(_state.predecessor_.finalCallbackPublished.load(std::memory_order_acquire),
                                      std::memory_order_release);
        _state.entered.store(true, std::memory_order_release);
        (void)SetEvent(_state.predecessor_.successorEntered.get());
        return {};
    }
    void Completed(JobResult) noexcept override
    {
        _state.completions.fetch_add(1, std::memory_order_relaxed);
        (void)SetEvent(_state.predecessor_.successorCompleted.get());
    }

  private:
    SuccessorState& _state;
};
#pragma warning(pop)

JobOptions OriginalOptions() noexcept
{
    JobOptions options{};
    options.kind = JobClass::Commit;
    options.serialKey = kSerialKey;
    options.memoryBytes = kInitialReservation;
    options.reserveRetainedMemory = true;
    return options;
}

bool WaitForGate(JobScheduler& scheduler, HANDLE gate, Checks& checks, const char* name) noexcept
{
    const ULONGLONG deadline = GetTickCount64() + kFailureTimeoutMs;
    const std::array<HANDLE, 2> handles{gate, static_cast<HANDLE>(scheduler.WakeHandle())};
    for (;;)
    {
        const ULONGLONG now = GetTickCount64();
        if (now >= deadline)
        {
            checks.Setup(name, false);
            return false;
        }
        const DWORD remaining = static_cast<DWORD>(deadline - now);
        const DWORD waited =
            WaitForMultipleObjects(static_cast<DWORD>(handles.size()), handles.data(), FALSE, remaining);
        if (waited == WAIT_OBJECT_0)
        {
            checks.Setup(name, true);
            return true;
        }
        if (waited == WAIT_OBJECT_0 + 1)
        {
            (void)DrainCompletions(scheduler);
            continue;
        }
        checks.Setup(name, false);
        return false;
    }
}

bool WaitForCompletion(JobScheduler& scheduler, StageState& state, Checks& checks, const char* name) noexcept
{
    return WaitForGate(scheduler, state.completed.get(), checks, name);
}

void ReleaseAll(StageState& state) noexcept
{
    (void)SetEvent(state.firstRelease.get());
    (void)SetEvent(state.secondRelease.get());
}

bool StartSuspended(JobScheduler& scheduler, StageState& state, Checks& checks, JobTicket& ticket)
{
    state.scheduler = &scheduler;
    std::unique_ptr<Job> job = std::make_unique<TwoStageJob>(state);
    const std::int32_t submitted = scheduler.Submit(job, OriginalOptions(), ticket);
    checks.Setup("initial-job-admitted-with-original-ticket", submitted == S_OK && !job && ticket.generation != 0);
    if (submitted != S_OK)
        return false;
    if (!WaitForGate(scheduler, state.firstEntered.get(), checks, "first-stage-entered-by-event"))
        return false;
    (void)SetEvent(state.firstRelease.get());
    if (!WaitForGate(scheduler, state.suspended.get(), checks, "joined-first-stage-reaches-owner-suspension"))
        return false;
    checks.Expect("producer-ack-published-before-suspension",
                  state.acknowledgementObserved.load(std::memory_order_acquire) && state.deferrals == 1 &&
                      state.ticket == ticket);
    return true;
}

void RunGrowthAndFifo(Checks& checks)
{
    const HRESULT invalidState = HRESULT_FROM_WIN32(ERROR_INVALID_STATE);
    JobScheduler scheduler;
    MemoryProbe memory;
    const std::int32_t initialized = scheduler.Initialize(1, {}, {&MemoryProbe::Sample, &memory});
    checks.Setup("success.initialize-one-worker", initialized == S_OK && scheduler.WorkerCount() == 1);
    if (initialized != S_OK)
        return;
    StageState state;
    checks.Setup("success-events-created", state.EventsValid());
    JobTicket ticket{};
    if (!StartSuspended(scheduler, state, checks, ticket))
    {
        ReleaseAll(state);
        scheduler.Close();
        return;
    }

    const unsigned int initialSamples = memory.samples.load(std::memory_order_relaxed);
    checks.Expect("success.grow-to-eight-kib-with-one-fresh-sample",
                  scheduler.IncreaseReservation(ticket, kGrownReservation) == S_OK &&
                      scheduler.ReservedMemoryBytes() == kGrownReservation &&
                      memory.samples.load(std::memory_order_relaxed) == initialSamples + 1);
    checks.Expect("success.grow-to-twelve-kib-with-next-fresh-sample",
                  scheduler.IncreaseReservation(ticket, kFinalReservation) == S_OK &&
                      scheduler.ReservedMemoryBytes() == kFinalReservation &&
                      memory.samples.load(std::memory_order_relaxed) == initialSamples + 2);
    const unsigned int beforeNoOp = memory.samples.load(std::memory_order_relaxed);
    checks.Expect("success.same-total-is-no-op-without-sample",
                  scheduler.IncreaseReservation(ticket, kFinalReservation) == S_OK &&
                      memory.samples.load(std::memory_order_relaxed) == beforeNoOp &&
                      scheduler.ReservedMemoryBytes() == kFinalReservation);
    checks.Expect("success.shrink-refused-without-changing-charge",
                  scheduler.IncreaseReservation(ticket, kGrownReservation) == E_INVALIDARG &&
                      scheduler.ReservedMemoryBytes() == kFinalReservation);
    const JobTicket staleGeneration{ticket.slot, ticket.generation + 1};
    checks.Expect("success.stale-generation-refused", static_cast<HRESULT>(scheduler.IncreaseReservation(
                                                          staleGeneration, kFinalReservation + 4096)) == invalidState &&
                                                          scheduler.ReservedMemoryBytes() == kFinalReservation);
    const JobTicket invalidSlot{JobScheduler::kCapacity, ticket.generation};
    checks.Expect("success.invalid-slot-is-invalid-argument",
                  scheduler.IncreaseReservation(invalidSlot, kFinalReservation) == E_INVALIDARG);

    SuccessorState successorState{state};
    std::unique_ptr<Job> successor = std::make_unique<SuccessorJob>(successorState);
    JobTicket successorTicket{};
    JobOptions successorOptions{};
    successorOptions.kind = JobClass::Commit;
    successorOptions.serialKey = kSerialKey;
    checks.Expect("success.same-key-successor-admitted-but-blocked",
                  scheduler.Submit(successor, successorOptions, successorTicket) == S_OK && !successor &&
                      !successorState.entered.load(std::memory_order_acquire));

    const bool firstResume = scheduler.Resume(ticket);
    const bool duplicateResumeRefused = !scheduler.Resume(ticket);
    checks.Expect("success.original-ticket-resumes-once", firstResume && duplicateResumeRefused);
    checks.Expect("success.resuming-stage-cannot-grow",
                  static_cast<HRESULT>(scheduler.IncreaseReservation(ticket, kFinalReservation + 1)) == invalidState &&
                      scheduler.ReservedMemoryBytes() == kFinalReservation);
    if (!WaitForGate(scheduler, state.secondEntered.get(), checks, "success-second-stage-enters-after-resume"))
    {
        ReleaseAll(state);
        scheduler.Close();
        return;
    }
    checks.Expect("success-second-context-borrows-grown-total",
                  state.secondStageReservation.load(std::memory_order_acquire) == kFinalReservation);
    checks.Expect("success-running-stage-cannot-grow", static_cast<HRESULT>(scheduler.IncreaseReservation(
                                                           ticket, kFinalReservation + 4096)) == invalidState &&
                                                           scheduler.ReservedMemoryBytes() == kFinalReservation);
    (void)SetEvent(state.secondRelease.get());
    if (!WaitForCompletion(scheduler, state, checks, "success-final-callback-drained"))
    {
        ReleaseAll(state);
        scheduler.Close();
        return;
    }
    if (!WaitForGate(scheduler, state.successorEntered.get(), checks, "success-successor-enters-after-final-callback"))
    {
        scheduler.Close();
        return;
    }
    if (!WaitForGate(scheduler, state.successorCompleted.get(), checks, "success-successor-final-callback-drained"))
    {
        scheduler.Close();
        return;
    }
    checks.Expect("success-final-retain-transfers-entire-grown-charge",
                  state.finalStatus == JobStatus::Finished && state.retainResult == S_OK &&
                      state.chargeBeforeRetain == kFinalReservation && state.chargeAfterRetain == kFinalReservation &&
                      scheduler.ReservedMemoryBytes() == kFinalReservation);
    checks.Expect("success-successor-observes-predecessor-final-callback",
                  successorState.sawFinalCallback.load(std::memory_order_acquire) &&
                      successorState.completions.load(std::memory_order_relaxed) == 1);
    checks.Expect("success-retained-ticket-releases-grown-charge",
                  scheduler.ReleaseRetainedMemory(state.retained) && scheduler.ReservedMemoryBytes() == 0);
    scheduler.Close();
}

void RunRefusalAndRecovery(Checks& checks)
{
    JobScheduler scheduler;
    MemoryProbe memory;
    const std::int32_t initialized = scheduler.Initialize(1, {}, {&MemoryProbe::Sample, &memory});
    checks.Setup("refusal.initialize-one-worker", initialized == S_OK && scheduler.WorkerCount() == 1);
    if (initialized != S_OK)
        return;
    StageState state;
    checks.Setup("refusal-events-created", state.EventsValid());
    JobTicket ticket{};
    if (!StartSuspended(scheduler, state, checks, ticket))
    {
        ReleaseAll(state);
        scheduler.Close();
        return;
    }
    const auto unchanged = [&]() noexcept
    {
        return scheduler.ReservedMemoryBytes() == kInitialReservation && state.deferrals == 1 &&
               state.ticket == ticket && state.runs.load(std::memory_order_relaxed) == 1;
    };

    memory.fail = true;
    checks.Expect("refusal.failed-fresh-sample-is-out-of-memory-and-atomic",
                  scheduler.IncreaseReservation(ticket, kGrownReservation) == E_OUTOFMEMORY && unchanged());
    memory.fail = false;
    memory.snapshot = {kMemoryAdmissionHeadroomBytes + kInitialReservation + 4095, kWideAvailable};
    checks.Expect("refusal.physical-capacity-shortfall-is-atomic",
                  scheduler.IncreaseReservation(ticket, kGrownReservation) == E_OUTOFMEMORY && unchanged());
    memory.snapshot = {kWideAvailable, kMemoryAdmissionHeadroomBytes + kInitialReservation + 4095};
    checks.Expect("refusal.commit-capacity-shortfall-is-atomic",
                  scheduler.IncreaseReservation(ticket, kGrownReservation) == E_OUTOFMEMORY && unchanged());
    memory.snapshot = {kWideAvailable, kWideAvailable};
    checks.Expect("refusal.uint64-maximum-is-refused-without-overflow",
                  scheduler.IncreaseReservation(ticket, (std::numeric_limits<std::uint64_t>::max)()) == E_OUTOFMEMORY &&
                      unchanged());
    checks.Expect("refusal.valid-recovery-after-faults-keeps-original-ticket",
                  scheduler.IncreaseReservation(ticket, kGrownReservation) == S_OK &&
                      scheduler.ReservedMemoryBytes() == kGrownReservation && state.ticket == ticket);
    checks.Expect("refusal.recovery-resumes-original-stage", scheduler.Resume(ticket));
    (void)WaitForGate(scheduler, state.secondEntered.get(), checks, "refusal.recovered-stage-enters");
    checks.Expect("refusal.recovered-context-gets-whole-new-total",
                  state.secondStageReservation.load(std::memory_order_acquire) == kGrownReservation);
    (void)SetEvent(state.secondRelease.get());
    if (WaitForCompletion(scheduler, state, checks, "refusal.recovered-job-finalizes"))
    {
        checks.Expect("refusal.recovered-final-transfer-keeps-new-total",
                      state.finalStatus == JobStatus::Finished && state.retainResult == S_OK &&
                          state.chargeBeforeRetain == kGrownReservation &&
                          state.chargeAfterRetain == kGrownReservation);
        checks.Expect("refusal.recovered-retained-charge-releases",
                      scheduler.ReleaseRetainedMemory(state.retained) && scheduler.ReservedMemoryBytes() == 0);
    }
    else
    {
        ReleaseAll(state);
    }
    scheduler.Close();
}

void RunCancellationAndShutdown(Checks& checks)
{
    {
        JobScheduler scheduler;
        MemoryProbe memory;
        const std::int32_t initialized = scheduler.Initialize(1, {}, {&MemoryProbe::Sample, &memory});
        checks.Setup("cancel.initialize-one-worker", initialized == S_OK);
        if (initialized == S_OK)
        {
            StageState state;
            checks.Setup("cancel-events-created", state.EventsValid());
            JobTicket ticket{};
            if (StartSuspended(scheduler, state, checks, ticket))
            {
                checks.Expect("cancel.grow-before-cancellation",
                              scheduler.IncreaseReservation(ticket, kFinalReservation) == S_OK &&
                                  scheduler.ReservedMemoryBytes() == kFinalReservation);
                checks.Expect("cancel.request-on-suspended-ticket", scheduler.Cancel(ticket));
                checks.Expect("cancel.growth-returns-abort-and-keeps-grown-charge",
                              scheduler.IncreaseReservation(ticket, kFinalReservation + 4096) == E_ABORT &&
                                  scheduler.ReservedMemoryBytes() == kFinalReservation);
                checks.Expect("cancel.cancelled-growth-does-not-sample-memory",
                              memory.samples.load(std::memory_order_relaxed) == 2);
                checks.Expect("cancel.suspended-ticket-can-be-resumed-to-acknowledge-cancel", scheduler.Resume(ticket));
                if (WaitForCompletion(scheduler, state, checks, "cancel.final-callback-arrives"))
                {
                    checks.Expect("cancel.skips-next-useful-stage-and-calls-back-once",
                                  state.finalStatus == JobStatus::Cancelled && state.completions == 1 &&
                                      state.runs.load(std::memory_order_relaxed) == 1 &&
                                      !state.secondStageReservation.load(std::memory_order_acquire));
                    checks.Expect("cancel.returns-complete-grown-reservation", scheduler.ReservedMemoryBytes() == 0);
                }
            }
            ReleaseAll(state);
            scheduler.Close();
        }
    }

    {
        JobScheduler scheduler;
        MemoryProbe memory;
        const std::int32_t initialized = scheduler.Initialize(1, {}, {&MemoryProbe::Sample, &memory});
        checks.Setup("close.initialize-one-worker", initialized == S_OK);
        if (initialized != S_OK)
            return;
        StageState state;
        checks.Setup("close-events-created", state.EventsValid());
        JobTicket ticket{};
        if (!StartSuspended(scheduler, state, checks, ticket))
        {
            ReleaseAll(state);
            scheduler.Close();
            return;
        }
        checks.Expect("close.grow-before-shutdown", scheduler.IncreaseReservation(ticket, kFinalReservation) == S_OK &&
                                                        scheduler.ReservedMemoryBytes() == kFinalReservation);
        scheduler.Close();
        checks.Expect("close.refunds-complete-grown-reservation", scheduler.ReservedMemoryBytes() == 0);
        checks.Expect("close.suppresses-final-callback",
                      state.completions == 0 && !state.finalCallbackPublished.load(std::memory_order_acquire));
        checks.Expect("close.does-not-run-another-useful-stage", state.runs.load(std::memory_order_relaxed) == 1);
        checks.Expect("close.closed-ticket-is-invalid-state",
                      static_cast<HRESULT>(scheduler.IncreaseReservation(ticket, kFinalReservation + 4096)) ==
                          HRESULT_FROM_WIN32(ERROR_INVALID_STATE));
    }
}

void RunInputBoundaries(Checks& checks)
{
    JobScheduler uninitialized;
    checks.Expect("boundary.uninitialized-is-invalid-argument",
                  uninitialized.IncreaseReservation({}, kInitialReservation) == E_INVALIDARG);

    JobScheduler scheduler;
    MemoryProbe memory;
    const std::int32_t initialized = scheduler.Initialize(1, {}, {&MemoryProbe::Sample, &memory});
    checks.Setup("boundary.initialize-one-worker", initialized == S_OK);
    if (initialized == S_OK)
    {
        scheduler.Close();
    }
}

void RunSnapshotRevalidation(Checks& checks)
{
    JobScheduler scheduler;
    MemoryProbe memory;
    const std::int32_t initialized = scheduler.Initialize(1, {}, {&MemoryProbe::Sample, &memory});
    checks.Setup("revalidation.initialize-one-worker", initialized == S_OK && scheduler.WorkerCount() == 1);
    if (initialized != S_OK)
        return;

    StageState state;
    checks.Setup("revalidation-events-created", state.EventsValid());
    JobTicket ticket{};
    if (!StartSuspended(scheduler, state, checks, ticket))
    {
        ReleaseAll(state);
        scheduler.Close();
        return;
    }

    memory.resumeScheduler = &scheduler;
    memory.resumeTicket = ticket;
    memory.resumeDuringNextSample = true;
    checks.Expect("revalidation.same-owner-sample-resumes-exact-suspended-ticket",
                  static_cast<HRESULT>(scheduler.IncreaseReservation(ticket, kGrownReservation)) ==
                          HRESULT_FROM_WIN32(ERROR_INVALID_STATE) &&
                      memory.resumeResult && !memory.resumeDuringNextSample &&
                      memory.samples.load(std::memory_order_relaxed) == 2 &&
                      scheduler.ReservedMemoryBytes() == kInitialReservation);

    if (!WaitForGate(scheduler, state.secondEntered.get(), checks, "revalidation.resumed-stage-enters"))
    {
        ReleaseAll(state);
        scheduler.Close();
        return;
    }
    checks.Expect("revalidation.resumed-context-keeps-original-total",
                  state.secondStageReservation.load(std::memory_order_acquire) == kInitialReservation);
    (void)SetEvent(state.secondRelease.get());
    if (WaitForCompletion(scheduler, state, checks, "revalidation.final-callback-drained"))
    {
        checks.Expect("revalidation.final-retain-keeps-original-total",
                      state.finalStatus == JobStatus::Finished && state.retainResult == S_OK &&
                          state.chargeBeforeRetain == kInitialReservation &&
                          state.chargeAfterRetain == kInitialReservation &&
                          scheduler.ReservedMemoryBytes() == kInitialReservation);
        checks.Expect("revalidation.retained-original-charge-releases",
                      scheduler.ReleaseRetainedMemory(state.retained) && scheduler.ReservedMemoryBytes() == 0);
    }
    else
    {
        ReleaseAll(state);
    }
    scheduler.Close();
}
} // namespace

int RunReservationGrowthTests()
{
    Checks checks;
    RunInputBoundaries(checks);
    RunGrowthAndFifo(checks);
    RunRefusalAndRecovery(checks);
    RunCancellationAndShutdown(checks);
    RunSnapshotRevalidation(checks);
    std::printf("%s reservation-growth.summary failures=%d\n", checks.failures == 0 ? "PASS" : "FAIL", checks.failures);
    std::fflush(stdout);
    return checks.failures;
}
