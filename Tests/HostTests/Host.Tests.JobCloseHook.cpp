#include "Host.Tests.JobCloseHook.h"

#include "../../Common/Jobs.h"

#include <atomic>
#include <combaseapi.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>

#include <windows.h>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/resource.h>
#pragma warning(pop)

namespace
{
constexpr std::uint64_t kReservationBytes = 32ull * 1024 * 1024;

void Expect(bool condition, const char* name, int& failures) noexcept
{
    std::printf("%s job-close-hook.%s\n", condition ? "PASS" : "FAIL", name);
    failures += condition ? 0 : 1;
}

wil::unique_handle Event()
{
    wil::unique_handle event{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    if (!event)
    {
        std::printf("FAIL job-close-hook.create-event\n");
        std::fflush(stdout);
        std::_Exit(1);
    }
    return event;
}

void Wait(HANDLE event, const char* name) noexcept
{
    if (WaitForSingleObject(event, 10000) != WAIT_OBJECT_0)
    {
        std::printf("FAIL job-close-hook.%s\n", name);
        std::fflush(stdout);
        std::_Exit(1);
    }
}

bool SampleMemory(void*, MemorySnapshot& snapshot) noexcept
{
    snapshot = {1024ull * 1024 * 1024 + 64ull * 1024 * 1024, 1024ull * 1024 * 1024 + 64ull * 1024 * 1024};
    return true;
}

struct HookState final
{
    JobScheduler* scheduler = nullptr;
    const DWORD ownerThread = GetCurrentThreadId();
    std::atomic<unsigned int> ownerCalls{0};
    std::atomic<unsigned int> workerCalls{0};
    std::atomic<unsigned int> wrappersDestroyed{0};
    std::atomic<unsigned int> runsStarted{0};
    std::atomic<unsigned int> runsEnded{0};
    std::atomic<unsigned int> completedCalls{0};
    std::atomic<DWORD> wrapperThread{0};
    std::atomic<DWORD> workerThread{0};
    std::atomic<bool> contextAlive{true};
    std::atomic<bool> ownerSawContext{false};
    std::atomic<bool> ownerOnOwnerThread{false};
    std::atomic<bool> ownerSawWrapperDestroyed{false};
    std::atomic<bool> ownerSawWakeHandle{false};
    std::atomic<bool> ownerSawReservation{false};
    std::atomic<bool> ownerSawWorkerJoined{false};
    std::atomic<bool> ownerSawNoCompletion{false};
    std::atomic<bool> workerAfterOwner{false};
    std::atomic<bool> workerOnMta{false};
    std::atomic<bool> workerNotOwner{false};
    std::atomic<bool> workerSawContext{false};
};

void OwnerCloseHook(void* context) noexcept
{
    auto& state = *static_cast<HookState*>(context);
    state.ownerSawContext.store(state.contextAlive.load());
    state.ownerOnOwnerThread.store(GetCurrentThreadId() == state.ownerThread);
    state.ownerSawWrapperDestroyed.store(state.wrappersDestroyed.load() == 1);
    const HANDLE wake = static_cast<HANDLE>(state.scheduler->WakeHandle());
    state.ownerSawWakeHandle.store(wake != nullptr && WaitForSingleObject(wake, 0) != WAIT_FAILED);
    state.ownerSawReservation.store(state.scheduler->ReservedMemoryBytes() == kReservationBytes);
    state.ownerSawWorkerJoined.store(state.runsStarted.load() == state.runsEnded.load());
    state.ownerSawNoCompletion.store(state.completedCalls.load() == 0);
    state.ownerCalls.fetch_add(1);
}

void WorkerCloseHook(void* context) noexcept
{
    auto& state = *static_cast<HookState*>(context);
    APTTYPE apartment{};
    APTTYPEQUALIFIER qualifier{};
    state.workerAfterOwner.store(state.ownerCalls.load() == 1);
    state.workerOnMta.store(SUCCEEDED(CoGetApartmentType(&apartment, &qualifier)) && apartment == APTTYPE_MTA);
    const DWORD thread = GetCurrentThreadId();
    state.workerThread.store(thread);
    state.workerNotOwner.store(thread != state.ownerThread);
    state.workerSawContext.store(state.contextAlive.load());
    state.workerCalls.fetch_add(1);
}

JobScheduler::CloseHooks Hooks(HookState& state) noexcept
{
    return {OwnerCloseHook, WorkerCloseHook, &state};
}

class GatedCloseJob final : public Job
{
  public:
    GatedCloseJob(HookState& state, HANDLE entered) noexcept : _state(state), _entered(entered) {}

    ~GatedCloseJob() override
    {
        _state.wrapperThread.store(GetCurrentThreadId());
        _state.wrappersDestroyed.fetch_add(1);
    }

    JobResult Run(JobContext& context) override
    {
        _state.runsStarted.fetch_add(1);
        (void)SetEvent(_entered);
        if (WaitForSingleObject(static_cast<HANDLE>(context.shutdownEvent), 10000) != WAIT_OBJECT_0)
        {
            std::printf("FAIL job-close-hook.shutdown-gate-timeout\n");
            std::fflush(stdout);
            std::_Exit(1);
        }
        _state.runsEnded.fetch_add(1);
        return {JobStatus::Cancelled};
    }

    void Completed(JobResult) noexcept override
    {
        _state.completedCalls.fetch_add(1);
    }

  private:
    HookState& _state;
    HANDLE _entered;
};

void CheckHooks(const HookState& state, const char* prefix, int& failures) noexcept
{
    Expect(state.ownerCalls.load() == 1, prefix, failures);
    Expect(state.ownerSawContext.load(), "owner-hook-sees-live-context", failures);
    Expect(state.workerCalls.load() == 1, "worker-hook-called-once", failures);
    Expect(state.workerAfterOwner.load(), "worker-hook-follows-owner-hook", failures);
    Expect(state.workerOnMta.load() && state.workerNotOwner.load(), "worker-hook-runs-on-final-mta-thread", failures);
    Expect(state.workerSawContext.load(), "hook-context-remains-alive-through-close", failures);
}

void TestIdleStoppedPool(int& failures)
{
    JobScheduler scheduler;
    const HRESULT initialized = scheduler.Initialize(1);
    Expect(SUCCEEDED(initialized), "idle-scheduler-initializes", failures);
    if (FAILED(initialized))
        return;

    scheduler.RequestStop();
    HookState state;
    state.scheduler = &scheduler;
    scheduler.Close(Hooks(state));
    Expect(state.ownerOnOwnerThread.load(), "idle-owner-hook-runs-on-owner", failures);
    CheckHooks(state, "idle-owner-hook-called-once", failures);
    scheduler.Close(Hooks(state));
    Expect(state.ownerCalls.load() == 1 && state.workerCalls.load() == 1, "idle-repeated-close-ignores-hooks",
           failures);
}

void TestRunningJob(int& failures)
{
    JobScheduler scheduler;
    const HRESULT initialized = scheduler.Initialize(1, {}, {SampleMemory, nullptr});
    Expect(SUCCEEDED(initialized), "gated-scheduler-initializes", failures);
    if (FAILED(initialized))
        return;

    auto entered = Event();
    HookState state;
    state.scheduler = &scheduler;
    std::unique_ptr<Job> job = std::make_unique<GatedCloseJob>(state, entered.get());
    JobTicket ticket;
    const HRESULT submitted =
        static_cast<HRESULT>(scheduler.Submit(job, {JobClass::Commit, 7, kReservationBytes}, ticket));
    Expect(SUCCEEDED(submitted) && !job, "gated-job-submitted-with-reservation", failures);
    if (FAILED(submitted))
    {
        scheduler.Close();
        return;
    }

    Wait(entered.get(), "gated-job-entered");
    Expect(scheduler.ReservedMemoryBytes() == kReservationBytes, "reservation-held-while-job-runs", failures);
    scheduler.Close(Hooks(state));

    Expect(state.wrapperThread.load() == state.ownerThread, "job-wrapper-destroyed-on-owner", failures);
    Expect(state.ownerOnOwnerThread.load() && state.ownerSawWrapperDestroyed.load(),
           "owner-hook-follows-owner-wrapper-destruction", failures);
    Expect(state.ownerSawWakeHandle.load() && state.ownerSawReservation.load(),
           "owner-hook-sees-live-wake-handle-and-reservation", failures);
    Expect(state.ownerSawWorkerJoined.load(), "owner-hook-runs-after-gated-worker-joins", failures);
    Expect(state.ownerSawNoCompletion.load() && state.completedCalls.load() == 0,
           "shutdown-drops-completion-without-callback", failures);
    CheckHooks(state, "gated-owner-hook-called-once", failures);
    Expect(state.workerThread.load() != state.ownerThread, "worker-hook-thread-is-distinct-from-owner", failures);

    scheduler.Close(Hooks(state));
    Expect(state.ownerCalls.load() == 1 && state.workerCalls.load() == 1, "gated-repeated-close-ignores-hooks",
           failures);
}

void TestUninitializedClose(int& failures)
{
    JobScheduler scheduler;
    HookState state;
    state.scheduler = &scheduler;
    scheduler.Close(Hooks(state));
    scheduler.Close(Hooks(state));
    Expect(state.ownerCalls.load() == 0 && state.workerCalls.load() == 0, "uninitialized-close-invokes-neither-hook",
           failures);
}
} // namespace

int RunJobCloseHookTests()
{
    int failures = 0;
    TestIdleStoppedPool(failures);
    TestRunningJob(failures);
    TestUninitializedClose(failures);
    std::printf("%s job-close-hook.summary failures=%d\n", failures == 0 ? "PASS" : "FAIL", failures);
    return failures;
}
