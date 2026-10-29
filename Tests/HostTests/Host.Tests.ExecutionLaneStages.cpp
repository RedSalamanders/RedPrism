#include "Host.Tests.ExecutionLaneStages.h"

#include "Jobs.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <stop_token>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/resource.h>
#pragma warning(pop)

#include <windows.h>

namespace
{
constexpr DWORD kFailureTimeoutMs = 10000;
constexpr std::uint64_t kChainCharge = 4096;
constexpr std::uint64_t kHandoffCharge = 2048;
unsigned int gFailures = 0;

void Expect(bool passed, const char* name) noexcept
{
    std::printf("%s execution-lane-stages.%s\n", passed ? "PASS" : "FAIL", name);
    if (!passed)
    {
        ++gFailures;
    }
    std::fflush(stdout);
}

template <typename Predicate> bool Until(JobScheduler& scheduler, Predicate ready) noexcept
{
    const ULONGLONG started = GetTickCount64();
    while (!ready())
    {
        (void)DrainCompletions(scheduler);
        if (ready())
        {
            return true;
        }
        const ULONGLONG elapsed = GetTickCount64() - started;
        if (elapsed >= kFailureTimeoutMs)
        {
            return false;
        }
        const DWORD remaining = static_cast<DWORD>(kFailureTimeoutMs - elapsed);
        if (WaitForSingleObject(static_cast<HANDLE>(scheduler.WakeHandle()), remaining) != WAIT_OBJECT_0)
        {
            return false;
        }
    }
    return true;
}

JobResult WaitForOwnerRelease(JobContext& context, HANDLE release) noexcept
{
    const auto wake = [release]() noexcept { (void)SetEvent(release); };
    std::stop_callback localCancellation{context.cancellation.LocalToken(), wake};
    const std::array<HANDLE, 2> events{release, static_cast<HANDLE>(context.shutdownEvent)};
    const DWORD wait = WaitForMultipleObjects(static_cast<DWORD>(events.size()), events.data(), FALSE, INFINITE);
    return wait == WAIT_OBJECT_0 || wait == WAIT_OBJECT_0 + 1 ? JobResult{} : JobResult{JobStatus::Failed, E_FAIL};
}

// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027)
struct ChainState final
{
    explicit ChainState(JobScheduler& owner) noexcept
        : scheduler(owner), release(CreateEventW(nullptr, TRUE, FALSE, nullptr))
    {
    }

    JobScheduler& scheduler;
    wil::unique_handle release;
    std::atomic<unsigned int> stage{1};
    std::array<std::atomic<unsigned int>, 3> runs{};
    std::array<std::atomic<unsigned int>, 3> retirements{};
    std::array<std::atomic<DWORD>, 3> runThreads{};
    std::array<std::atomic<DWORD>, 3> retirementThreads{};
    JobTicket original{};
    unsigned int ownerHooks = 0;
    std::array<JobTicket, 3> hookTickets{};
    std::array<std::uint64_t, 3> hookCharges{};
    std::array<JobStatus, 3> hookResults{};
    bool completed = false;
    JobResult terminalResult{};
    std::uint64_t completionCharge = 0;
    std::int32_t retainResult = E_PENDING;
    RetainedMemoryTicket retained{};
    std::atomic<bool> predecessorFinalized{false};
    std::atomic<bool> successorEntered{false};
    std::atomic<bool> successorSawFinalized{false};
    bool successorCompleted = false;
};
#pragma warning(pop)

// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4626 5027)
class ThreeStageJob final : public Job
{
  public:
    explicit ThreeStageJob(ChainState& state) noexcept : _state(state) {}

    JobResult Run(JobContext& context) override
    {
        const unsigned int stage = _state.stage.load(std::memory_order_acquire);
        if (stage < 1 || stage > 3)
        {
            return {JobStatus::Failed, E_UNEXPECTED};
        }
        const std::size_t index = stage - 1;
        _state.runs[index].fetch_add(1, std::memory_order_relaxed);
        _state.runThreads[index].store(GetCurrentThreadId(), std::memory_order_release);
        _state.scheduler.NotifyOwnerWork();
        if (context.reservedMemoryBytes != kChainCharge)
        {
            return {JobStatus::Failed, E_UNEXPECTED};
        }
        return stage == 1 ? JobResult{} : WaitForOwnerRelease(context, _state.release.get());
    }

    void RetireWorkerStage(JobContext&, JobResult) noexcept override
    {
        const unsigned int stage = _state.stage.load(std::memory_order_acquire);
        if (stage >= 1 && stage <= 3)
        {
            const std::size_t index = stage - 1;
            _state.retirements[index].fetch_add(1, std::memory_order_relaxed);
            _state.retirementThreads[index].store(GetCurrentThreadId(), std::memory_order_release);
        }
    }

    bool DeferRetirement(JobResult result, JobTicket ticket) noexcept override
    {
        const unsigned int stage = _state.stage.load(std::memory_order_acquire);
        if (stage >= 1 && stage <= 3)
        {
            const std::size_t index = stage - 1;
            _state.hookTickets[index] = ticket;
            _state.hookCharges[index] = _state.scheduler.ReservedMemoryBytes();
            _state.hookResults[index] = result.status;
        }
        ++_state.ownerHooks;
        return stage < 3 && result.status == JobStatus::Finished;
    }

    void Completed(JobResult result) noexcept override
    {
        _state.terminalResult = result;
        _state.completionCharge = _state.scheduler.ReservedMemoryBytes();
        if (result.status == JobStatus::Finished)
        {
            _state.retainResult = _state.scheduler.RetainMemory(_state.original, _state.retained);
        }
        _state.completed = true;
        _state.predecessorFinalized.store(result.status == JobStatus::Finished, std::memory_order_release);
    }

  private:
    ChainState& _state;
};
#pragma warning(pop)

// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4626 5027)
class SerialSuccessor final : public Job
{
  public:
    explicit SerialSuccessor(ChainState& state) noexcept : _state(state) {}

    JobResult Run(JobContext&) override
    {
        _state.successorSawFinalized.store(_state.predecessorFinalized.load(std::memory_order_acquire),
                                           std::memory_order_release);
        _state.successorEntered.store(true, std::memory_order_release);
        return {};
    }

    void Completed(JobResult result) noexcept override
    {
        _state.successorCompleted = result.status == JobStatus::Finished;
    }

  private:
    ChainState& _state;
};
#pragma warning(pop)

// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4626 5027)
class IndependentCompute final : public Job
{
  public:
    explicit IndependentCompute(bool& completed) noexcept : _completed(completed) {}
    JobResult Run(JobContext&) override
    {
        return {};
    }
    void Completed(JobResult result) noexcept override
    {
        _completed = result.status == JobStatus::Finished;
    }

  private:
    bool& _completed;
};
#pragma warning(pop)

void RunThreeStageChain()
{
    JobScheduler scheduler;
    const HRESULT initialized = static_cast<HRESULT>(scheduler.Initialize(1));
    Expect(SUCCEEDED(initialized), "chain-initializes-general-worker");
    if (FAILED(initialized))
    {
        scheduler.Close();
        return;
    }
    const HRESULT enabled = static_cast<HRESULT>(scheduler.EnableGpuRecordingLane());
    Expect(SUCCEEDED(enabled) && scheduler.WorkerCount() == 1, "chain-enables-one-private-recorder");
    if (FAILED(enabled))
    {
        scheduler.Close();
        return;
    }

    ChainState state{scheduler};
    const auto close = wil::scope_exit([&]() noexcept { scheduler.Close(); });
    std::unique_ptr<Job> predecessor = std::make_unique<ThreeStageJob>(state);
    JobOptions chainOptions{};
    chainOptions.kind = JobClass::Commit;
    chainOptions.serialKey = 0x53544745;
    chainOptions.memoryBytes = kChainCharge;
    chainOptions.deferredRetirement = true;
    chainOptions.reserveRetainedMemory = true;
    JobTicket original{};
    const HRESULT submitted = static_cast<HRESULT>(scheduler.Submit(predecessor, chainOptions, original));
    state.original = original;
    Expect(bool(state.release) && SUCCEEDED(submitted) && !predecessor && original.generation != 0,
           "chain-submits-one-original-ticket-and-record");
    if (FAILED(submitted))
    {
        scheduler.Close();
        return;
    }
    Expect(!scheduler.Resume(original, JobExecutionLane::GpuRecording), "running-stage-cannot-resume-before-owner-ack");

    std::unique_ptr<Job> successor = std::make_unique<SerialSuccessor>(state);
    JobOptions successorOptions{};
    successorOptions.kind = chainOptions.kind;
    successorOptions.serialKey = chainOptions.serialKey;
    // AdmitWaiting leaves a same-key successor in WaitingAdmission before reserving its bytes.
    successorOptions.memoryBytes = 1024;
    JobTicket successorTicket{};
    const HRESULT successorSubmit =
        static_cast<HRESULT>(scheduler.Submit(successor, successorOptions, successorTicket));

    const bool firstAcknowledged =
        SUCCEEDED(successorSubmit) && Until(scheduler, [&]() { return state.ownerHooks == 1; });
    Expect(SUCCEEDED(successorSubmit) && !successor && firstAcknowledged && state.hookTickets[0] == original &&
               state.hookResults[0] == JobStatus::Finished && state.hookCharges[0] == kChainCharge &&
               state.runs[0].load(std::memory_order_acquire) == 1 &&
               state.retirements[0].load(std::memory_order_acquire) == 1,
           "compute-owner-ack-keeps-original-ticket-and-full-charge");
    if (!firstAcknowledged)
    {
        scheduler.Close();
        return;
    }

    (void)ResetEvent(state.release.get());
    state.stage.store(2, std::memory_order_release);
    const bool toRecorder = scheduler.Resume(original, JobExecutionLane::GpuRecording);
    Expect(toRecorder, "owner-resumes-same-record-on-private-recorder");
    if (!toRecorder)
    {
        scheduler.Close();
        return;
    }
    const bool recorderEntered = Until(scheduler, [&]() { return state.runs[1].load(std::memory_order_acquire) == 1; });
    Expect(recorderEntered, "recording-stage-enters-on-event-wake");
    if (!recorderEntered)
    {
        scheduler.Close();
        return;
    }
    Expect(state.runThreads[0].load(std::memory_order_acquire) != state.runThreads[1].load(std::memory_order_acquire),
           "recorder-stage-uses-distinct-private-pool-thread");
    Expect(!scheduler.Resume(original, JobExecutionLane::Compute), "running-recorder-cannot-resume-ahead-of-ack");

    bool independentFinished = false;
    std::unique_ptr<Job> independent = std::make_unique<IndependentCompute>(independentFinished);
    JobTicket independentTicket{};
    const HRESULT independentSubmit = static_cast<HRESULT>(
        scheduler.Submit(independent, {.kind = JobClass::Background, .memoryBytes = 512}, independentTicket));
    const bool independentDone =
        SUCCEEDED(independentSubmit) && Until(scheduler, [&]() { return independentFinished; });
    Expect(SUCCEEDED(independentSubmit) && !independent && independentDone && !state.completed &&
               scheduler.ReservedMemoryBytes() == kChainCharge,
           "ordinary-compute-finishes-while-recorder-holds-original-charge");
    if (!independentDone)
    {
        scheduler.Close();
        return;
    }

    (void)SetEvent(state.release.get());
    const bool secondAcknowledged = Until(scheduler, [&]() { return state.ownerHooks == 2; });
    Expect(secondAcknowledged && state.hookTickets[1] == original && state.hookResults[1] == JobStatus::Finished &&
               state.hookCharges[1] == kChainCharge && state.retirements[1].load(std::memory_order_acquire) == 1,
           "recorder-owner-ack-keeps-same-ticket-and-full-charge");
    if (!secondAcknowledged)
    {
        scheduler.Close();
        return;
    }

    (void)ResetEvent(state.release.get());
    state.stage.store(3, std::memory_order_release);
    const bool toCompute = scheduler.Resume(original, JobExecutionLane::Compute);
    Expect(toCompute, "owner-resumes-same-record-back-to-compute");
    if (!toCompute)
    {
        scheduler.Close();
        return;
    }
    const bool finalStageEntered =
        Until(scheduler, [&]() { return state.runs[2].load(std::memory_order_acquire) == 1; });
    Expect(finalStageEntered, "final-compute-stage-enters");
    if (!finalStageEntered)
    {
        scheduler.Close();
        return;
    }
    Expect(state.runThreads[0].load(std::memory_order_acquire) == state.runThreads[2].load(std::memory_order_acquire),
           "compute-stages-return-to-one-general-worker");
    Expect(!state.successorEntered.load(std::memory_order_acquire) && scheduler.ReservedMemoryBytes() == kChainCharge,
           "same-key-successor-waits-through-final-worker-stage");

    (void)SetEvent(state.release.get());
    const bool ownerFinalized = Until(scheduler, [&]() { return state.completed && state.successorCompleted; });
    Expect(ownerFinalized && state.ownerHooks == 3 && state.hookTickets[2] == original &&
               state.hookResults[2] == JobStatus::Finished && state.hookCharges[2] == kChainCharge &&
               state.completionCharge == kChainCharge && state.terminalResult.status == JobStatus::Finished,
           "final-owner-drop-sees-original-ticket-and-complete-charge");
    Expect(ownerFinalized && state.retainResult == S_OK && state.retained.generation != 0 &&
               scheduler.ReservedMemoryBytes() == kChainCharge,
           "reserved-retained-memory-slot-transfers-only-at-final-owner-callback");
    Expect(ownerFinalized && state.successorEntered.load(std::memory_order_acquire) &&
               state.successorSawFinalized.load(std::memory_order_acquire),
           "same-key-successor-enters-after-predecessor-final-owner-drop");
    const bool released = ownerFinalized && scheduler.ReleaseRetainedMemory(state.retained);
    // Release refunds the bytes synchronously and publishes one memory-readiness transition. Consume that
    // real outer-loop notification before asserting complete settlement, just as the application does.
    (void)DrainCompletions(scheduler);
    Expect(released && scheduler.ReservedMemoryBytes() == 0 && scheduler.IsIdle(),
           "retained-original-charge-refunds-after-explicit-release");
}

// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027)
struct SuspendedState final
{
    explicit SuspendedState(JobScheduler& owner) noexcept : scheduler(owner) {}

    JobScheduler& scheduler;
    std::atomic<unsigned int> runs{0};
    std::atomic<unsigned int> retirements{0};
    unsigned int ownerHooks = 0;
    JobTicket ticket{};
    std::uint64_t callbackCharge = 0;
    bool completed = false;
    JobResult result{};
};
#pragma warning(pop)

// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4626 5027)
class SuspendOnceJob final : public Job
{
  public:
    explicit SuspendOnceJob(SuspendedState& state) noexcept : _state(state) {}
    JobResult Run(JobContext&) override
    {
        _state.runs.fetch_add(1, std::memory_order_release);
        return {};
    }
    void RetireWorkerStage(JobContext&, JobResult) noexcept override
    {
        _state.retirements.fetch_add(1, std::memory_order_release);
    }
    bool DeferRetirement(JobResult, JobTicket ticket) noexcept override
    {
        _state.ticket = ticket;
        _state.callbackCharge = _state.scheduler.ReservedMemoryBytes();
        return ++_state.ownerHooks == 1;
    }
    void Completed(JobResult result) noexcept override
    {
        _state.result = result;
        _state.completed = true;
    }

  private:
    SuspendedState& _state;
};
#pragma warning(pop)

void RunDisabledAndInvalidResumeCase()
{
    JobScheduler scheduler;
    const HRESULT initialized = static_cast<HRESULT>(scheduler.Initialize(1));
    Expect(SUCCEEDED(initialized), "disabled-case-initializes-compute-only-scheduler");
    if (FAILED(initialized))
    {
        scheduler.Close();
        return;
    }
    SuspendedState state{scheduler};
    const auto close = wil::scope_exit([&]() noexcept { scheduler.Close(); });
    std::unique_ptr<Job> job = std::make_unique<SuspendOnceJob>(state);
    JobOptions options{};
    options.kind = JobClass::Background;
    options.memoryBytes = 1024;
    options.deferredRetirement = true;
    JobTicket ticket{};
    const HRESULT submit = static_cast<HRESULT>(scheduler.Submit(job, options, ticket));
    Expect(SUCCEEDED(submit) && !job && ticket.generation != 0, "disabled-case-admits-original-compute-record");
    if (FAILED(submit))
    {
        scheduler.Close();
        return;
    }
    const bool suspended = Until(scheduler, [&]() { return state.ownerHooks == 1; });
    Expect(suspended && state.ticket == ticket && state.callbackCharge == 1024 && !state.completed,
           "invalid-lane-controls-start-from-acknowledged-suspension");
    if (!suspended)
    {
        scheduler.Close();
        return;
    }

    const bool countRejected = !scheduler.Resume(ticket, JobExecutionLane::Count);
    Expect(countRejected && !state.completed && scheduler.ReservedMemoryBytes() == 1024,
           "count-lane-refusal-keeps-original-suspended-and-charged");
    const bool disabledRejected = !scheduler.Resume(ticket, JobExecutionLane::GpuRecording);
    Expect(disabledRejected && !state.completed && state.ownerHooks == 1 && scheduler.ReservedMemoryBytes() == 1024,
           "disabled-recorder-refusal-keeps-original-suspended-and-charged");
    JobTicket stale = ticket;
    ++stale.generation;
    Expect(!scheduler.Resume(stale, JobExecutionLane::Compute) && !state.completed,
           "stale-generation-cannot-resume-suspended-record");

    const bool originalResume = scheduler.Resume(ticket);
    Expect(originalResume, "original-resume-overload-still-resumes-same-record");
    const bool completed = originalResume && Until(scheduler, [&]() { return state.completed; });
    Expect(completed && state.result.status == JobStatus::Finished && state.runs.load(std::memory_order_acquire) == 2 &&
               state.retirements.load(std::memory_order_acquire) == 2 && state.ownerHooks == 2 &&
               scheduler.ReservedMemoryBytes() == 0,
           "valid-original-resume-completes-and-refunds-after-invalid-attempts");
}

// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027)
struct CancelHandoffState final
{
    explicit CancelHandoffState(JobScheduler& owner) noexcept : scheduler(owner) {}

    JobScheduler& scheduler;
    std::atomic<unsigned int> stage{1};
    std::atomic<unsigned int> runs[2]{};
    std::atomic<unsigned int> retirements[2]{};
    std::atomic<DWORD> runThreads[2]{};
    std::atomic<DWORD> retirementThreads[2]{};
    unsigned int ownerHooks = 0;
    JobTicket ticket{};
    bool completed = false;
    JobResult result{};
    std::uint64_t completionCharge = 0;
};
#pragma warning(pop)

// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4626 5027)
class CancelDuringHandoffJob final : public Job
{
  public:
    explicit CancelDuringHandoffJob(CancelHandoffState& state) noexcept : _state(state) {}
    JobResult Run(JobContext&) override
    {
        const unsigned int stage = _state.stage.load(std::memory_order_acquire);
        if (stage >= 1 && stage <= 2)
        {
            const std::size_t index = stage - 1;
            _state.runs[index].fetch_add(1, std::memory_order_relaxed);
            _state.runThreads[index].store(GetCurrentThreadId(), std::memory_order_release);
        }
        return {};
    }
    void RetireWorkerStage(JobContext&, JobResult) noexcept override
    {
        const unsigned int stage = _state.stage.load(std::memory_order_acquire);
        if (stage >= 1 && stage <= 2)
        {
            const std::size_t index = stage - 1;
            _state.retirements[index].fetch_add(1, std::memory_order_relaxed);
            _state.retirementThreads[index].store(GetCurrentThreadId(), std::memory_order_release);
        }
    }
    bool DeferRetirement(JobResult result, JobTicket ticket) noexcept override
    {
        _state.ticket = ticket;
        ++_state.ownerHooks;
        return _state.ownerHooks == 1 && result.status == JobStatus::Finished;
    }
    void Completed(JobResult result) noexcept override
    {
        _state.result = result;
        _state.completionCharge = _state.scheduler.ReservedMemoryBytes();
        _state.completed = true;
    }

  private:
    CancelHandoffState& _state;
};
#pragma warning(pop)

void RunCancellationHandoffCase()
{
    JobScheduler scheduler;
    const HRESULT initialized = static_cast<HRESULT>(scheduler.Initialize(1));
    const HRESULT enabled = SUCCEEDED(initialized) ? static_cast<HRESULT>(scheduler.EnableGpuRecordingLane()) : E_FAIL;
    Expect(SUCCEEDED(initialized) && SUCCEEDED(enabled), "cancel-case-enables-both-execution-pools");
    if (FAILED(initialized) || FAILED(enabled))
    {
        scheduler.Close();
        return;
    }

    CancelHandoffState state{scheduler};
    const auto close = wil::scope_exit([&]() noexcept { scheduler.Close(); });
    std::unique_ptr<Job> job = std::make_unique<CancelDuringHandoffJob>(state);
    JobOptions options{};
    options.kind = JobClass::Background;
    options.memoryBytes = kHandoffCharge;
    options.deferredRetirement = true;
    JobTicket ticket{};
    const HRESULT submit = static_cast<HRESULT>(scheduler.Submit(job, options, ticket));
    Expect(SUCCEEDED(submit) && !job && ticket.generation != 0, "cancel-case-admits-original-compute-stage");
    if (FAILED(submit))
    {
        scheduler.Close();
        return;
    }
    const bool suspended = Until(scheduler, [&]() { return state.ownerHooks == 1; });
    Expect(suspended && state.ticket == ticket && state.runs[0].load(std::memory_order_acquire) == 1 &&
               scheduler.ReservedMemoryBytes() == kHandoffCharge,
           "cancel-handoff-begins-after-first-stage-owner-ack");
    if (!suspended)
    {
        scheduler.Close();
        return;
    }

    const bool cancelled = scheduler.Cancel(ticket);
    state.stage.store(2, std::memory_order_release);
    const bool handedOff = cancelled && scheduler.Resume(ticket, JobExecutionLane::GpuRecording);
    Expect(cancelled && handedOff, "cancel-during-handoff-resumes-retirement-on-recorder-lane");
    if (!handedOff)
    {
        scheduler.Close();
        return;
    }
    const bool completed = Until(scheduler, [&]() { return state.completed; });
    Expect(completed && state.runs[1].load(std::memory_order_acquire) == 0,
           "cancelled-recorder-stage-skips-useful-work");
    Expect(completed && state.retirements[1].load(std::memory_order_acquire) == 1 &&
               state.result.status == JobStatus::Cancelled && state.completionCharge == kHandoffCharge &&
               state.retirementThreads[1].load(std::memory_order_acquire) !=
                   state.retirementThreads[0].load(std::memory_order_acquire),
           "cancelled-handoff-retires-on-recorder-pool-before-owner-cancel-callback");
    Expect(completed && scheduler.ReservedMemoryBytes() == 0 && !scheduler.Resume(ticket, JobExecutionLane::Compute),
           "cancelled-original-refunds-and-cannot-resume-again");
}

// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027)
struct ShutdownLaneState final
{
    explicit ShutdownLaneState(JobScheduler& owner) noexcept : scheduler(owner) {}

    JobScheduler& scheduler;
    std::atomic<bool> entered{false};
    std::atomic<bool> runExited{false};
    std::atomic<bool> retired{false};
    std::atomic<DWORD> runThread{0};
    std::atomic<DWORD> retireThread{0};
    bool completed = false;
};
#pragma warning(pop)

// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4625 4626 5027)
class ShutdownBlockedJob final : public Job
{
  public:
    explicit ShutdownBlockedJob(ShutdownLaneState& state) noexcept : _state(state) {}
    JobResult Run(JobContext& context) override
    {
        _state.runThread.store(GetCurrentThreadId(), std::memory_order_release);
        _state.entered.store(true, std::memory_order_release);
        _state.scheduler.NotifyOwnerWork();
        const JobResult result = WaitForOwnerRelease(context, _release.get());
        _state.runExited.store(true, std::memory_order_release);
        return result;
    }
    void RetireWorkerStage(JobContext&, JobResult) noexcept override
    {
        _state.retireThread.store(GetCurrentThreadId(), std::memory_order_release);
        _state.retired.store(true, std::memory_order_release);
    }
    void Completed(JobResult) noexcept override
    {
        _state.completed = true;
    }

  private:
    ShutdownLaneState& _state;
    wil::unique_handle _release{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
};
#pragma warning(pop)

void RunShutdownPoolJoinCase()
{
    JobScheduler scheduler;
    const HRESULT initialized = static_cast<HRESULT>(scheduler.Initialize(1));
    const HRESULT enabled = SUCCEEDED(initialized) ? static_cast<HRESULT>(scheduler.EnableGpuRecordingLane()) : E_FAIL;
    Expect(SUCCEEDED(initialized) && SUCCEEDED(enabled) && scheduler.WorkerCount() == 1,
           "close-case-initializes-independent-pools");
    if (FAILED(initialized) || FAILED(enabled))
    {
        scheduler.Close();
        return;
    }

    ShutdownLaneState computeState{scheduler};
    ShutdownLaneState recorderState{scheduler};
    const auto close = wil::scope_exit([&]() noexcept { scheduler.Close(); });
    std::unique_ptr<Job> compute = std::make_unique<ShutdownBlockedJob>(computeState);
    std::unique_ptr<Job> recorder = std::make_unique<ShutdownBlockedJob>(recorderState);
    JobOptions computeOptions{};
    computeOptions.kind = JobClass::Background;
    computeOptions.deferredRetirement = true;
    JobOptions recorderOptions = computeOptions;
    recorderOptions.kind = JobClass::Commit;
    recorderOptions.executionLane = JobExecutionLane::GpuRecording;
    JobTicket computeTicket{};
    JobTicket recorderTicket{};
    const HRESULT computeSubmit = static_cast<HRESULT>(scheduler.Submit(compute, computeOptions, computeTicket));
    const HRESULT recorderSubmit = static_cast<HRESULT>(scheduler.Submit(recorder, recorderOptions, recorderTicket));
    Expect(SUCCEEDED(computeSubmit) && !compute && SUCCEEDED(recorderSubmit) && !recorder,
           "close-case-admits-one-running-job-per-pool");
    if (FAILED(computeSubmit) || FAILED(recorderSubmit))
    {
        scheduler.Close();
        return;
    }
    const bool bothEntered = Until(scheduler,
                                   [&]()
                                   {
                                       return computeState.entered.load(std::memory_order_acquire) &&
                                              recorderState.entered.load(std::memory_order_acquire);
                                   });
    Expect(bothEntered && computeState.runThread.load(std::memory_order_acquire) !=
                              recorderState.runThread.load(std::memory_order_acquire),
           "close-case-jobs-block-in-their-distinct-pools");
    if (!bothEntered)
    {
        scheduler.Close();
        return;
    }

    scheduler.Close();
    Expect(computeState.runExited.load(std::memory_order_acquire) &&
               recorderState.runExited.load(std::memory_order_acquire) &&
               computeState.retired.load(std::memory_order_acquire) &&
               recorderState.retired.load(std::memory_order_acquire) &&
               computeState.retireThread.load(std::memory_order_acquire) ==
                   computeState.runThread.load(std::memory_order_acquire) &&
               recorderState.retireThread.load(std::memory_order_acquire) ==
                   recorderState.runThread.load(std::memory_order_acquire),
           "close-joins-compute-and-recorder-through-worker-retirement");
    Expect(!computeState.completed && !recorderState.completed, "close-suppresses-both-owner-completions");
    Expect(scheduler.WorkerCount() == 0 && scheduler.ReservedMemoryBytes() == 0 && scheduler.IsIdle(),
           "close-tears-down-both-pools-and-original-records");
}
} // namespace

int RunExecutionLaneStagesTests()
{
    gFailures = 0;
    RunThreeStageChain();
    RunDisabledAndInvalidResumeCase();
    RunCancellationHandoffCase();
    RunShutdownPoolJoinCase();
    return static_cast<int>(gFailures);
}
