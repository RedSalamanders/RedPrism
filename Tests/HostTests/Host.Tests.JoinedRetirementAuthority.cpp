#include "Host.Tests.JoinedRetirementAuthority.h"

#include "../../Common/Jobs.h"

#include <Windows.h>
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
constexpr std::uint64_t kReservationBytes = 4096;
constexpr std::uint64_t kAvailableBytes = 2ull * 1024 * 1024 * 1024;
constexpr std::uint64_t kFirstSerialKey = 0x4A524101;
constexpr std::uint64_t kSecondSerialKey = 0x4A524102;

void Expect(bool condition, const char* name, int& failures) noexcept
{
    std::printf("%s joined-retirement-authority.%s\n", condition ? "PASS" : "FAIL", name);
    failures += condition ? 0 : 1;
}

[[noreturn]] void SetupFailure(const char* name) noexcept
{
    std::printf("FAIL joined-retirement-authority.%s\n", name);
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
        SetupFailure(name); // A bounded wait reports a stuck state transition; elapsed time is not an assertion.
}

bool SampleMemory(void*, MemorySnapshot& snapshot) noexcept
{
    snapshot = {kAvailableBytes, kAvailableBytes};
    return true;
}

struct JoinedState final
{
    JobScheduler* scheduler = nullptr;
    JoinedState* otherState = nullptr;
    JobTicket ticket{};
    JobTicket otherTicket{};
    std::uint64_t serialKey = 0;
    HANDLE entered = nullptr;
    HANDLE release = nullptr;
    HANDLE ended = nullptr;
    const DWORD ownerThread = GetCurrentThreadId();
    std::atomic<unsigned int> runs{0};
    std::atomic<unsigned int> runEnds{0};
    std::atomic<unsigned int> deferRetirementCalls{0};
    std::atomic<unsigned int> joinedHooks{0};
    std::atomic<unsigned int> completed{0};
    std::atomic<DWORD> hookThread{0};
    std::atomic<bool> workerQueryFalse{false};
    std::atomic<bool> deferRetirementQueryFalse{false};
    std::atomic<bool> joinedQueryTrue{false};
    std::atomic<bool> joinedTicketMatchesOriginal{false};
    std::atomic<bool> wrongTicketFalse{false};
    std::atomic<bool> otherTicketFalse{false};
    std::atomic<bool> acknowledgedQueryFalse{false};
    std::atomic<bool> hookSawBothWorkersEnded{false};
    std::atomic<bool> hookSawBothReservations{false};
    std::atomic<bool> hookActive{false};
    std::atomic<bool> offOwnerQueryFalse{false};
    std::atomic<bool> offOwnerSawHookActive{false};
};

DWORD WINAPI QueryJoinedAuthorityFromForeignThread(void* opaque) noexcept
{
    auto& state = *static_cast<JoinedState*>(opaque);
    state.offOwnerSawHookActive.store(state.hookActive.load(std::memory_order_acquire), std::memory_order_relaxed);
    state.offOwnerQueryFalse.store(!state.scheduler->InJoinedRetirementCallback(state.ticket),
                                   std::memory_order_relaxed);
    return 0;
}

class SuspendedJoinedJob final : public Job
{
  public:
    explicit SuspendedJoinedJob(JoinedState& state) noexcept : _state(state) {}

    JobResult Run(JobContext&) override
    {
        _state.runs.fetch_add(1, std::memory_order_relaxed);
        (void)SetEvent(_state.entered);
        Wait(_state.release, "worker-release");
        _state.workerQueryFalse.store(!_state.scheduler->InJoinedRetirementCallback(_state.ticket),
                                      std::memory_order_relaxed);
        _state.runEnds.fetch_add(1, std::memory_order_release);
        (void)SetEvent(_state.ended);
        return {};
    }

    bool DeferRetirement(JobResult, JobTicket) noexcept override
    {
        _state.deferRetirementQueryFalse.store(!_state.scheduler->InJoinedRetirementCallback(_state.ticket),
                                               std::memory_order_relaxed);
        _state.deferRetirementCalls.fetch_add(1, std::memory_order_release);
        return true; // The successful real worker stage remains suspended until actual Close.
    }

    void RetainJoinedRetirement(JobScheduler& scheduler, JobTicket original) noexcept override
    {
        _state.hookActive.store(true, std::memory_order_release);
        _state.hookThread.store(GetCurrentThreadId(), std::memory_order_relaxed);
        _state.joinedTicketMatchesOriginal.store(original == _state.ticket, std::memory_order_relaxed);
        _state.joinedQueryTrue.store(scheduler.InJoinedRetirementCallback(original), std::memory_order_relaxed);
        const JobTicket wrong{original.slot, original.generation + 1};
        _state.wrongTicketFalse.store(!scheduler.InJoinedRetirementCallback(wrong), std::memory_order_relaxed);
        _state.otherTicketFalse.store(!scheduler.InJoinedRetirementCallback(_state.otherTicket),
                                      std::memory_order_relaxed);
        _state.acknowledgedQueryFalse.store(!scheduler.InAcknowledgedJobCallback(original), std::memory_order_relaxed);
        _state.hookSawBothWorkersEnded.store(
            _state.runs.load(std::memory_order_acquire) == 1 && _state.runEnds.load(std::memory_order_acquire) == 1 &&
                _state.otherState != nullptr && _state.otherState->runs.load(std::memory_order_acquire) == 1 &&
                _state.otherState->runEnds.load(std::memory_order_acquire) == 1,
            std::memory_order_relaxed);
        _state.hookSawBothReservations.store(scheduler.ReservedMemoryBytes() == 2 * kReservationBytes,
                                             std::memory_order_relaxed);
        wil::unique_handle foreignThread{
            CreateThread(nullptr, 0, QueryJoinedAuthorityFromForeignThread, &_state, 0, nullptr)};
        if (!foreignThread)
            SetupFailure("create-foreign-query-thread");
        Wait(foreignThread.get(), "foreign-query-thread-join-inside-hook");
        _state.hookActive.store(false, std::memory_order_release);
        _state.joinedHooks.fetch_add(1, std::memory_order_relaxed);
    }

    bool DeferCompletion(JobResult, JobTicket) noexcept override
    {
        return false;
    }

    void Completed(JobResult) noexcept override
    {
        _state.completed.fetch_add(1, std::memory_order_relaxed);
    }

  private:
    JoinedState& _state;
};

struct NormalState final
{
    JobScheduler* scheduler = nullptr;
    JobTicket ticket{};
    std::atomic<bool> deferRetirementFalse{false};
    std::atomic<bool> deferCompletionFalse{false};
    std::atomic<bool> completedFalse{false};
    std::atomic<unsigned int> completed{0};
};

class NormalCallbackJob final : public Job
{
  public:
    explicit NormalCallbackJob(NormalState& state) noexcept : _state(state) {}

    JobResult Run(JobContext&) override
    {
        return {};
    }

    bool DeferRetirement(JobResult, JobTicket ticket) noexcept override
    {
        _state.deferRetirementFalse.store(!_state.scheduler->InJoinedRetirementCallback(ticket),
                                          std::memory_order_relaxed);
        return false;
    }

    bool DeferCompletion(JobResult, JobTicket ticket) noexcept override
    {
        _state.deferCompletionFalse.store(!_state.scheduler->InJoinedRetirementCallback(ticket),
                                          std::memory_order_relaxed);
        return false;
    }

    void Completed(JobResult) noexcept override
    {
        _state.completedFalse.store(!_state.scheduler->InJoinedRetirementCallback(_state.ticket),
                                    std::memory_order_relaxed);
        _state.completed.fetch_add(1, std::memory_order_relaxed);
    }

  private:
    NormalState& _state;
};

void SubmitSuspended(JobScheduler& scheduler, JoinedState& state) noexcept
{
    state.scheduler = &scheduler;
    std::unique_ptr<Job> job = std::make_unique<SuspendedJoinedJob>(state);
    const JobOptions options{.kind = JobClass::Commit,
                             .serialKey = state.serialKey,
                             .memoryBytes = kReservationBytes,
                             .deferredRetirement = true,
                             .allowMemoryWaiting = false,
                             .reserveRetainedMemory = true};
    if (scheduler.Submit(job, options, state.ticket) != S_OK || job)
        SetupFailure("suspended-submit");
}

void SubmitNormal(JobScheduler& scheduler, NormalState& state) noexcept
{
    state.scheduler = &scheduler;
    std::unique_ptr<Job> job = std::make_unique<NormalCallbackJob>(state);
    const JobOptions options{.kind = JobClass::Background,
                             .serialKey = 0,
                             .memoryBytes = 0,
                             .deferredRetirement = true,
                             .allowMemoryWaiting = false,
                             .reserveRetainedMemory = false};
    if (scheduler.Submit(job, options, state.ticket) != S_OK || job)
        SetupFailure("normal-submit");
}

void DrainUntilRetirementCallbacks(JobScheduler& scheduler, JoinedState& first, JoinedState& second) noexcept
{
    constexpr ULONGLONG kFailureDeadlineMs = 10000;
    const ULONGLONG deadline = GetTickCount64() + kFailureDeadlineMs;
    while (first.deferRetirementCalls.load(std::memory_order_acquire) != 1 ||
           second.deferRetirementCalls.load(std::memory_order_acquire) != 1)
    {
        if (GetTickCount64() >= deadline)
            SetupFailure("retirement-callback-witness-deadline");
        (void)DrainCompletions(scheduler);
        if (first.deferRetirementCalls.load(std::memory_order_acquire) == 1 &&
            second.deferRetirementCalls.load(std::memory_order_acquire) == 1)
            return;
        if (scheduler.HasUiWork())
            continue;

        const ULONGLONG now = GetTickCount64();
        if (now >= deadline)
            SetupFailure("retirement-callback-witness-deadline");
        const DWORD remaining = static_cast<DWORD>(deadline - now);
        const HANDLE wake = static_cast<HANDLE>(scheduler.WakeHandle());
        if (!wake || WaitForSingleObject(wake, remaining) != WAIT_OBJECT_0)
            SetupFailure("retirement-callback-witness-wake");
    }
}

void RunJoinedAuthorityScenario(int& failures)
{
    JoinedState first;
    JoinedState second;
    auto firstEntered = Event();
    auto firstRelease = Event();
    auto firstEnded = Event();
    auto secondEntered = Event();
    auto secondRelease = Event();
    auto secondEnded = Event();
    first.entered = firstEntered.get();
    first.release = firstRelease.get();
    first.ended = firstEnded.get();
    second.entered = secondEntered.get();
    second.release = secondRelease.get();
    second.ended = secondEnded.get();
    first.otherState = &second;
    second.otherState = &first;
    first.serialKey = kFirstSerialKey;
    second.serialKey = kSecondSerialKey;

    JobScheduler scheduler;
    if (scheduler.Initialize(2, {}, {SampleMemory, nullptr}) != S_OK)
        SetupFailure("initialize");
    NormalState normal;
    SubmitNormal(scheduler, normal);
    Expect(SettleJobs(scheduler, 10000) == SettleResult::Settled, "normal-callbacks-settle", failures);
    Expect(normal.completed.load(std::memory_order_relaxed) == 1 && normal.deferRetirementFalse.load() &&
               normal.deferCompletionFalse.load() && normal.completedFalse.load(),
           "normal-retirement-completion-and-completed-callbacks-lack-joined-authority", failures);

    SubmitSuspended(scheduler, first);
    Wait(firstEntered.get(), "first-worker-entered");
    SubmitSuspended(scheduler, second);
    Wait(secondEntered.get(), "second-worker-entered");
    first.otherTicket = second.ticket;
    second.otherTicket = first.ticket;
    const bool firstReleased = SetEvent(firstRelease.get()) != FALSE;
    const bool secondReleased = SetEvent(secondRelease.get()) != FALSE;
    Expect(firstReleased && secondReleased, "worker-gates-released", failures);
    Wait(firstEnded.get(), "first-worker-ended");
    Wait(secondEnded.get(), "second-worker-ended");
    DrainUntilRetirementCallbacks(scheduler, first, second);

    Expect(first.runs.load() == 1 && second.runs.load() == 1 && first.runEnds.load() == 1 &&
               second.runEnds.load() == 1 && first.workerQueryFalse.load() && second.workerQueryFalse.load(),
           "actual-worker-stages-finish-and-cannot-query-joined-authority", failures);
    Expect(first.serialKey != 0 && second.serialKey != 0 && first.serialKey != second.serialKey &&
               first.ticket != second.ticket && scheduler.ReservedMemoryBytes() == 2 * kReservationBytes,
           "distinct-nonzero-serial-keys-original-tickets-and-fixed-reservations", failures);
    Expect(first.deferRetirementCalls.load() == 1 && second.deferRetirementCalls.load() == 1 &&
               first.joinedHooks.load() == 0 && second.joinedHooks.load() == 0 &&
               !scheduler.InJoinedRetirementCallback(first.ticket) &&
               !scheduler.InJoinedRetirementCallback(second.ticket) && first.deferRetirementQueryFalse.load() &&
               second.deferRetirementQueryFalse.load(),
           "suspended-defer-retirement-is-outside-joined-close-authority", failures);

    scheduler.Close(); // Real Close joins scheduler producers before invoking each suspended job's hook.

    Expect(first.joinedHooks.load() == 1 && second.joinedHooks.load() == 1,
           "actual-close-invokes-each-original-hook-once", failures);
    Expect(first.joinedQueryTrue.load() && second.joinedQueryTrue.load() && first.joinedTicketMatchesOriginal.load() &&
               second.joinedTicketMatchesOriginal.load() && first.wrongTicketFalse.load() &&
               second.wrongTicketFalse.load(),
           "joined-authority-is-true-only-for-exact-original-ticket", failures);
    Expect(first.offOwnerQueryFalse.load() && second.offOwnerQueryFalse.load() && first.offOwnerSawHookActive.load() &&
               second.offOwnerSawHookActive.load(),
           "foreign-thread-query-is-false-while-each-owner-hook-is-active", failures);
    Expect(first.otherTicketFalse.load() && second.otherTicketFalse.load(), "neither-jobs-hook-proves-the-other-ticket",
           failures);
    Expect(first.acknowledgedQueryFalse.load() && second.acknowledgedQueryFalse.load(),
           "joined-retirement-is-not-acknowledged-job-callback", failures);
    Expect(first.hookThread.load() == first.ownerThread && second.hookThread.load() == second.ownerThread &&
               first.hookSawBothWorkersEnded.load() && second.hookSawBothWorkersEnded.load(),
           "joined-hooks-run-on-owner-after-both-real-worker-stages-end", failures);
    Expect(first.hookSawBothReservations.load() && second.hookSawBothReservations.load(),
           "both-original-reservations-remain-held-through-their-hooks", failures);
    Expect(first.completed.load() == 0 && second.completed.load() == 0,
           "close-drops-useful-completed-callbacks-for-suspended-jobs", failures);
    Expect(!scheduler.InJoinedRetirementCallback(first.ticket) && !scheduler.InJoinedRetirementCallback(second.ticket),
           "joined-authority-clears-after-close", failures);
    scheduler.Close();
    Expect(first.joinedHooks.load() == 1 && second.joinedHooks.load() == 1,
           "repeated-close-does-not-reinvoke-joined-hooks", failures);
}
} // namespace

int RunJoinedRetirementAuthorityTests()
{
    int failures = 0;
    RunJoinedAuthorityScenario(failures);
    std::printf("%s joined-retirement-authority.summary failures=%d\n", failures == 0 ? "PASS" : "FAIL", failures);
    return failures;
}
