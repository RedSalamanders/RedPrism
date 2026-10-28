#include "Host.Tests.GpuRecordingLane.h"

#include "Jobs.h"

#include <array>
#include <atomic>
#include <cstdio>
#include <memory>
#include <new>
#include <stop_token>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/resource.h>
#pragma warning(pop)

#include <windows.h>

namespace
{
constexpr DWORD kFailureTimeoutMs = 10000;

struct Checks final
{
    unsigned int failures = 0;

    void Expect(const char* name, bool passed) noexcept
    {
        std::printf("%s recording-lane.%s\n", passed ? "PASS" : "FAIL", name);
        std::fflush(stdout);
        if (!passed)
        {
            ++failures;
        }
    }
};

// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027)
struct JobState final
{
    explicit JobState(JobScheduler& owner) noexcept : scheduler(owner) {}

    JobScheduler& scheduler;
    wil::unique_handle release{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    std::atomic<bool> entered{false};
    std::atomic<bool> retired{false};
    std::atomic<std::uint64_t> reserved{0};
    bool terminal = false;
    JobResult result{};
};
#pragma warning(pop)

// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4626 5027)
class ProbeJob final : public Job
{
  public:
    ProbeJob(JobState& state, bool block, bool throwBadAlloc = false) noexcept
        : _state(state), _block(block), _throwBadAlloc(throwBadAlloc)
    {
    }

    JobResult Run(JobContext& context) override
    {
        _state.reserved.store(context.reservedMemoryBytes, std::memory_order_relaxed);
        _state.entered.store(true, std::memory_order_release);
        _state.scheduler.NotifyOwnerWork();
        if (_throwBadAlloc)
        {
            throw std::bad_alloc{};
        }
        if (_block)
        {
            const auto wake = [event = _state.release.get()]() noexcept { (void)SetEvent(event); };
            std::stop_callback localCancellation{context.cancellation.LocalToken(), wake};
            const std::array<HANDLE, 2> events{_state.release.get(), static_cast<HANDLE>(context.shutdownEvent)};
            const DWORD wait =
                WaitForMultipleObjects(static_cast<DWORD>(events.size()), events.data(), FALSE, INFINITE);
            if (wait != WAIT_OBJECT_0 && wait != WAIT_OBJECT_0 + 1)
            {
                return {JobStatus::Failed, E_FAIL};
            }
        }
        return {};
    }

    void RetireWorkerStage(JobContext&, JobResult) noexcept override
    {
        _state.retired.store(true, std::memory_order_release);
    }

    void Completed(JobResult result) noexcept override
    {
        _state.result = result;
        _state.terminal = true;
    }

  private:
    JobState& _state;
    bool _block;
    bool _throwBadAlloc;
};
#pragma warning(pop)

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

JobOptions RecordingOptions() noexcept
{
    JobOptions options{};
    options.kind = JobClass::Background;
    options.memoryBytes = 4096;
    options.deferredRetirement = true;
    options.executionLane = JobExecutionLane::GpuRecording;
    return options;
}

// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027)
struct SerialState final
{
    explicit SerialState(JobScheduler& owner) noexcept
        : scheduler(owner), externalAck(CreateEventW(nullptr, TRUE, FALSE, nullptr))
    {
    }

    JobScheduler& scheduler;
    wil::unique_handle externalAck;
    std::atomic<bool> predecessorEntered{false};
    std::atomic<bool> predecessorRetired{false};
    std::atomic<bool> successorEntered{false};
    std::atomic<bool> successorSawFinalizedPredecessor{false};
    std::atomic<bool> predecessorFinalized{false};
    bool retirementHeld = false;
    bool predecessorTerminal = false;
    bool successorTerminal = false;
    unsigned int retirementHooks = 0;
    JobTicket predecessorTicket{};
};
#pragma warning(pop)

// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4626 5027)
class DeferredSerialJob final : public Job
{
  public:
    explicit DeferredSerialJob(SerialState& state) noexcept : _state(state) {}

    JobResult Run(JobContext&) override
    {
        _state.predecessorEntered.store(true, std::memory_order_release);
        return {};
    }

    void RetireWorkerStage(JobContext&, JobResult) noexcept override
    {
        _state.predecessorRetired.store(true, std::memory_order_release);
    }

    bool DeferRetirement(JobResult result, JobTicket ticket) noexcept override
    {
        if (!_state.retirementHeld)
        {
            _state.predecessorTicket = ticket;
            _state.retirementHeld = true;
            ++_state.retirementHooks;
            return result.status == JobStatus::Finished;
        }
        ++_state.retirementHooks;
        return false;
    }

    void Completed(JobResult result) noexcept override
    {
        _state.predecessorTerminal = result.status == JobStatus::Finished;
        _state.predecessorFinalized.store(_state.predecessorTerminal, std::memory_order_release);
    }

  private:
    SerialState& _state;
};
#pragma warning(pop)

// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4626 5027)
class SerialSuccessorJob final : public Job
{
  public:
    explicit SerialSuccessorJob(SerialState& state) noexcept : _state(state) {}

    JobResult Run(JobContext&) override
    {
        _state.successorSawFinalizedPredecessor.store(_state.predecessorFinalized.load(std::memory_order_acquire),
                                                      std::memory_order_release);
        _state.successorEntered.store(true, std::memory_order_release);
        return {};
    }

    void Completed(JobResult result) noexcept override
    {
        _state.successorTerminal = result.status == JobStatus::Finished;
    }

  private:
    SerialState& _state;
};
#pragma warning(pop)

void RunSerialRetirementCase(Checks& checks)
{
    JobScheduler scheduler;
    const HRESULT initialized = static_cast<HRESULT>(scheduler.Initialize(1));
    const HRESULT enabled = SUCCEEDED(initialized) ? static_cast<HRESULT>(scheduler.EnableGpuRecordingLane()) : E_FAIL;
    checks.Expect("serial-case-initializes-one-worker-and-recorder",
                  SUCCEEDED(initialized) && SUCCEEDED(enabled) && scheduler.WorkerCount() == 1);
    if (FAILED(initialized) || FAILED(enabled))
    {
        scheduler.Close();
        return;
    }

    SerialState state{scheduler};
    std::unique_ptr<Job> predecessor = std::make_unique<DeferredSerialJob>(state);
    JobTicket predecessorTicket{};
    JobOptions predecessorOptions{};
    predecessorOptions.kind = JobClass::Commit;
    predecessorOptions.serialKey = 0x47505531;
    predecessorOptions.memoryBytes = 4096;
    predecessorOptions.deferredRetirement = true;
    predecessorOptions.executionLane = JobExecutionLane::GpuRecording;
    const HRESULT predecessorSubmit =
        static_cast<HRESULT>(scheduler.Submit(predecessor, predecessorOptions, predecessorTicket));
    checks.Expect("serial-predecessor-admitted-on-recording-lane", SUCCEEDED(predecessorSubmit) && !predecessor);
    if (FAILED(predecessorSubmit))
    {
        scheduler.Close();
        return;
    }

    std::unique_ptr<Job> successor = std::make_unique<SerialSuccessorJob>(state);
    JobTicket successorTicket{};
    JobOptions successorOptions = predecessorOptions;
    successorOptions.executionLane = JobExecutionLane::Compute;
    successorOptions.deferredRetirement = false;
    successorOptions.memoryBytes = 1024;
    const HRESULT successorSubmit =
        static_cast<HRESULT>(scheduler.Submit(successor, successorOptions, successorTicket));
    checks.Expect("same-serial-key-compute-successor-admitted", SUCCEEDED(successorSubmit) && !successor);

    const bool held = Until(scheduler, [&]() { return state.retirementHeld; });
    checks.Expect("owner-final-drain-observes-deferred-retirement",
                  held && state.predecessorTicket == predecessorTicket &&
                      state.predecessorRetired.load(std::memory_order_acquire) && !state.predecessorTerminal);
    checks.Expect("cross-lane-fifo-and-charge-held-during-delayed-retirement",
                  !state.successorEntered.load(std::memory_order_acquire) && scheduler.ReservedMemoryBytes() == 4096);

    const bool gateValid = bool(state.externalAck);
    checks.Expect("external-retirement-gate-created", gateValid);
    const bool ackSignaled = gateValid && SetEvent(state.externalAck.get()) != FALSE &&
                             WaitForSingleObject(state.externalAck.get(), kFailureTimeoutMs) == WAIT_OBJECT_0;
    checks.Expect("owner-acknowledges-retained-stage-by-event", ackSignaled);
    const bool resumed = ackSignaled && scheduler.Resume(predecessorTicket);
    checks.Expect("exact-suspended-ticket-resumes-retirement", resumed);

    const bool drained =
        resumed && Until(scheduler, [&]() { return state.predecessorTerminal && state.successorTerminal; });
    checks.Expect("successor-runs-only-after-predecessor-owner-finalization",
                  drained && state.successorSawFinalizedPredecessor.load(std::memory_order_acquire));
    checks.Expect("owner-final-drain-releases-both-reservations",
                  drained && scheduler.ReservedMemoryBytes() == 0 && scheduler.IsIdle());
    scheduler.Close();
}

void Run(Checks& checks)
{
    JobScheduler scheduler;
    checks.Expect("uninitialized-enable-refused",
                  static_cast<HRESULT>(scheduler.EnableGpuRecordingLane()) == HRESULT_FROM_WIN32(ERROR_INVALID_STATE));
    const HRESULT initialized = static_cast<HRESULT>(scheduler.Initialize(1));
    checks.Expect("initialize-one-general-worker", SUCCEEDED(initialized) && scheduler.WorkerCount() == 1);
    if (FAILED(initialized))
    {
        scheduler.Close();
        return;
    }

    JobState blocked{scheduler};
    JobState ordinary{scheduler};
    JobState queued{scheduler};
    JobState oom{scheduler};
    const auto close = wil::scope_exit([&]() noexcept { scheduler.Close(); });
    const JobOptions recording = RecordingOptions();

    std::unique_ptr<Job> first = std::make_unique<ProbeJob>(blocked, true);
    JobTicket firstTicket{};
    const HRESULT disabledSubmit = static_cast<HRESULT>(scheduler.Submit(first, recording, firstTicket));
    checks.Expect("disabled-recording-preserves-descriptor",
                  disabledSubmit == HRESULT_FROM_WIN32(ERROR_INVALID_STATE) && bool(first) &&
                      firstTicket.generation == 0);

    JobOptions invalidLane = recording;
    invalidLane.executionLane = static_cast<JobExecutionLane>(static_cast<unsigned int>(JobExecutionLane::Count));
    JobTicket invalidTicket{7, 9};
    const HRESULT invalidResult = static_cast<HRESULT>(scheduler.Submit(first, invalidLane, invalidTicket));
    checks.Expect("invalid-lane-enum-preserves-descriptor",
                  invalidResult == E_INVALIDARG && bool(first) && invalidTicket.generation == 0);

    checks.Expect("enable-single-lane", SUCCEEDED(static_cast<HRESULT>(scheduler.EnableGpuRecordingLane())));
    checks.Expect("duplicate-enable-refused",
                  static_cast<HRESULT>(scheduler.EnableGpuRecordingLane()) == HRESULT_FROM_WIN32(ERROR_INVALID_STATE));
    const HRESULT recordingSubmit = static_cast<HRESULT>(scheduler.Submit(first, recording, firstTicket));
    checks.Expect("recording-admitted", SUCCEEDED(recordingSubmit) && !first);
    if (FAILED(recordingSubmit))
    {
        scheduler.Close();
        return;
    }
    checks.Expect("recording-entered-by-event",
                  Until(scheduler, [&]() { return blocked.entered.load(std::memory_order_acquire); }));
    checks.Expect("recording-borrows-original-admission",
                  blocked.reserved.load(std::memory_order_relaxed) == 4096 && scheduler.ReservedMemoryBytes() == 4096);
    checks.Expect("general-worker-count-unchanged", scheduler.WorkerCount() == 1);

    std::unique_ptr<Job> second = std::make_unique<ProbeJob>(ordinary, false);
    JobTicket secondTicket{};
    const HRESULT computeSubmit =
        static_cast<HRESULT>(scheduler.Submit(second, {.kind = JobClass::Commit, .memoryBytes = 256}, secondTicket));
    checks.Expect("independent-compute-admitted", SUCCEEDED(computeSubmit) && !second);
    const bool computeFinished = SUCCEEDED(computeSubmit) && Until(scheduler, [&]() { return ordinary.terminal; });
    checks.Expect("compute-completes-under-recording-pressure",
                  computeFinished && ordinary.result.status == JobStatus::Finished && !blocked.terminal);
    checks.Expect("recording-charge-retained-under-pressure", scheduler.ReservedMemoryBytes() == 4096);

    std::unique_ptr<Job> third = std::make_unique<ProbeJob>(queued, false);
    JobTicket thirdTicket{};
    const HRESULT thirdSubmit = static_cast<HRESULT>(scheduler.Submit(third, recording, thirdTicket));
    checks.Expect("second-recorder-admitted-to-shared-queue", SUCCEEDED(thirdSubmit) && !third);
    checks.Expect("single-recorder-prevents-concurrent-entry", !queued.entered.load(std::memory_order_acquire));
    const bool queuedCancelled = SUCCEEDED(thirdSubmit) && scheduler.Cancel(thirdTicket);
    checks.Expect("cancel-queued-recorder", queuedCancelled);
    const bool runningCancelled = scheduler.Cancel(firstTicket);
    checks.Expect("cancel-blocked-recorder", runningCancelled);
    const bool cancellationsDrained =
        queuedCancelled && runningCancelled && Until(scheduler, [&]() { return blocked.terminal && queued.terminal; });
    checks.Expect("cancel-and-retirement-acknowledged",
                  cancellationsDrained && blocked.result.status == JobStatus::Cancelled &&
                      queued.result.status == JobStatus::Cancelled && blocked.retired.load(std::memory_order_acquire) &&
                      queued.retired.load(std::memory_order_acquire));
    checks.Expect("queued-cancel-skips-useful-work", !queued.entered.load(std::memory_order_acquire));
    checks.Expect("cancelled-records-return-all-admission", scheduler.ReservedMemoryBytes() == 0);

    std::unique_ptr<Job> failure = std::make_unique<ProbeJob>(oom, false, true);
    JobTicket failureTicket{};
    const HRESULT failureSubmit = static_cast<HRESULT>(scheduler.Submit(failure, recording, failureTicket));
    checks.Expect("oom-descriptor-admitted", SUCCEEDED(failureSubmit) && !failure);
    const bool oomDrained = SUCCEEDED(failureSubmit) && Until(scheduler, [&]() { return oom.terminal; });
    checks.Expect("same-runner-translates-bad-alloc", oomDrained && oom.result.status == JobStatus::Failed &&
                                                          static_cast<HRESULT>(oom.result.error) == E_OUTOFMEMORY &&
                                                          oom.retired.load(std::memory_order_acquire));
    (void)DrainCompletions(scheduler);
    checks.Expect("settled-has-no-polling-work", scheduler.IsIdle() && scheduler.ReservedMemoryBytes() == 0);

    JobState shutdown{scheduler};
    std::unique_ptr<Job> finalJob = std::make_unique<ProbeJob>(shutdown, true);
    JobTicket finalTicket{};
    const HRESULT finalSubmit = static_cast<HRESULT>(scheduler.Submit(finalJob, recording, finalTicket));
    checks.Expect("shutdown-recorder-admitted", SUCCEEDED(finalSubmit) && !finalJob);
    const bool finalEntered =
        SUCCEEDED(finalSubmit) && Until(scheduler, [&]() { return shutdown.entered.load(std::memory_order_acquire); });
    checks.Expect("shutdown-recorder-entered", finalEntered);
    scheduler.Close();
    checks.Expect("shutdown-joins-recorder-and-retires", shutdown.retired.load(std::memory_order_acquire));
    checks.Expect("shutdown-drops-owner-completion", !shutdown.terminal);
    checks.Expect("shutdown-releases-budget-and-pools",
                  scheduler.ReservedMemoryBytes() == 0 && scheduler.WorkerCount() == 0 && scheduler.IsIdle());
}

void RunStartupBoundaryCase(Checks& checks)
{
    JobScheduler scheduler;
    const HRESULT initialized = static_cast<HRESULT>(scheduler.Initialize(1));
    checks.Expect("startup-boundary-initialized", SUCCEEDED(initialized));
    if (FAILED(initialized))
        return;
    JobState state{scheduler};
    const auto close = wil::scope_exit([&]() noexcept { scheduler.Close(); });
    std::unique_ptr<Job> job = std::make_unique<ProbeJob>(state, false);
    JobTicket ticket{};
    const auto submitted = static_cast<HRESULT>(scheduler.Submit(job, {}, ticket));
    checks.Expect("startup-boundary-ordinary-admitted", SUCCEEDED(submitted) && !job);
    checks.Expect("enable-after-first-admission-refused",
                  static_cast<HRESULT>(scheduler.EnableGpuRecordingLane()) == HRESULT_FROM_WIN32(ERROR_INVALID_STATE));
    const bool drained = SUCCEEDED(submitted) && Until(scheduler, [&]() { return state.terminal; });
    checks.Expect("startup-boundary-ordinary-finishes", drained && state.result.status == JobStatus::Finished);
    checks.Expect("enable-after-idle-still-refused",
                  static_cast<HRESULT>(scheduler.EnableGpuRecordingLane()) == HRESULT_FROM_WIN32(ERROR_INVALID_STATE));
    std::unique_ptr<Job> recording = std::make_unique<ProbeJob>(state, false);
    JobTicket rejected{};
    checks.Expect("late-enable-cannot-admit-recorder",
                  static_cast<HRESULT>(scheduler.Submit(recording, RecordingOptions(), rejected)) ==
                          HRESULT_FROM_WIN32(ERROR_INVALID_STATE) &&
                      recording && rejected.generation == 0);
    scheduler.Close();
    checks.Expect("enable-after-close-refused",
                  static_cast<HRESULT>(scheduler.EnableGpuRecordingLane()) == HRESULT_FROM_WIN32(ERROR_INVALID_STATE));
}
} // namespace

int RunGpuRecordingLaneTests() noexcept
{
    Checks checks{};
    Run(checks);
    RunSerialRetirementCase(checks);
    RunStartupBoundaryCase(checks);
    return static_cast<int>(checks.failures);
}
