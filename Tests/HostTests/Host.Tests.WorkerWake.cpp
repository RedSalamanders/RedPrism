#include "Host.Tests.WorkerWake.h"
#include "../../Common/WorkerPool.h"

#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <memory>
#include <thread>

namespace
{
unsigned int failures = 0;
void Expect(bool value, const char* name) noexcept
{
    std::printf("%s owner-wake-pool.%s\n", value ? "PASS" : "FAIL", name);
    failures += value ? 0u : 1u;
}
void TestCountBounds()
{
    using WorkerResources::OwnerWakeCounts;
    using WorkerResources::WakeLane;
    OwnerWakeCounts zero{0};
    Expect(!zero.IsValid() && !zero.Request(WakeLane::High) && zero.Take(WakeLane::High) == 0,
           "zero-capacity-refuses-with-no-pending-hint");
    OwnerWakeCounts oversized{1025};
    Expect(!oversized.IsValid() && !oversized.Request(WakeLane::Normal), "oversized-capacity-refuses");
    OwnerWakeCounts single{1};
    bool valid = true;
    for (std::uint32_t index = 0; index < 3; ++index)
    {
        const auto lane = static_cast<WakeLane>(index);
        valid = single.Request(lane) && single.Request(lane) && single.Take(lane) == 1 && single.Take(lane) == 0 &&
                single.Request(lane) && single.Take(lane) == 1 && valid;
    }
    valid = !single.Request(static_cast<WakeLane>(3)) && single.Take(static_cast<WakeLane>(3)) == 0 && valid;
    Expect(valid, "three-literal-lanes-saturate-reset-and-preserve-later-request");
    OwnerWakeCounts maximum{1024};
    valid = maximum.IsValid();
    for (std::uint32_t index = 0; index < 1025; ++index)
        valid = maximum.Request(WakeLane::Background) && valid;
    Expect(valid && maximum.Take(WakeLane::Background) == 1024 && maximum.Take(WakeLane::Background) == 0,
           "maximum-capacity-coalesces-one-extra-request");
}
wil::unique_handle Event()
{
    wil::unique_handle event{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    if (!event)
        std::_Exit(90);
    return event;
}
void Wait(HANDLE event)
{
    // A failure bound, not a required latency or polling interval.
    if (WaitForSingleObject(event, 5000) != WAIT_OBJECT_0)
        std::_Exit(91);
}
struct Queue final
{
    DWORD owner = GetCurrentThreadId();
    std::array<std::atomic<unsigned int>, 3> pending{};
    std::atomic<unsigned int> completed{0};
    std::atomic<bool> offOwnerMta{true};
    wil::unique_handle done = Event();
    static void Dispatch(void* context, WorkPriority priority, CancelToken) noexcept
    {
        auto& queue = *static_cast<Queue*>(context);
        APTTYPE type{};
        APTTYPEQUALIFIER qualifier{};
        if (GetCurrentThreadId() == queue.owner || FAILED(CoGetApartmentType(&type, &qualifier)) || type != APTTYPE_MTA)
            queue.offOwnerMta.store(false);
        const unsigned int taken = queue.pending[static_cast<std::size_t>(priority)].exchange(0);
        if (queue.completed.fetch_add(taken) + taken == 48)
            (void)SetEvent(queue.done.get());
    }
};
void TestQueues()
{
    WorkerPool pool;
    Queue queue;
    const HRESULT initialize = pool.Initialize(2, Queue::Dispatch, &queue, true);
    Expect(SUCCEEDED(initialize), "actual-pool-initializes-opt-in-owner-endpoint");
    if (FAILED(initialize))
        return;
    bool accepted = true;
    for (unsigned int request = 0; request < 47; ++request)
    {
        const unsigned int lane = request % 3;
        queue.pending[lane].fetch_add(1); // real queue publishes before the hint
        accepted = pool.Wake(static_cast<WorkPriority>(lane)) && accepted;
    }
    bool foreignAccepted = false;
    std::thread foreign(
        [&]
        {
            queue.pending[2].fetch_add(1);
            foreignAccepted = pool.Wake(WorkPriority::Background); // original thread-safe backend remains available
        });
    foreign.join();
    Wait(queue.done.get());
    Expect(accepted && queue.completed.load() == 48, "three-priorities-drain48-real-bodies-under-coalescing");
    Expect(foreignAccepted, "foreign-producer-keeps-original-thread-safe-wake-contract");
    Expect(queue.offOwnerMta.load(), "broker-native-dispatch-retains-off-owner-mta");
    Expect(!pool.Wake(WorkPriority::Count), "invalid-priority-refuses-before-hint");
    pool.Close();
    Expect(!pool.Wake(WorkPriority::Normal) && FAILED(pool.Initialize(1)),
           "closed-endpoint-refuses-wake-and-reinitialize");
}
struct Closing final
{
    DWORD owner = GetCurrentThreadId();
    wil::unique_handle entered = Event();
    wil::unique_handle release = Event();
    wil::unique_handle stopped = Event();
    std::atomic<bool> borrowedAlive{true};
    std::atomic<bool> dispatchAlive{false};
    std::atomic<unsigned int> dispatches{0};
    std::atomic<bool> ownerHookValid{false};
    std::atomic<bool> workerHookValid{false};
    std::atomic<unsigned int> destroyed{0};
    struct Payload final
    {
        Closing* state;
        ~Payload()
        {
            if (state->borrowedAlive.load() && GetCurrentThreadId() != state->owner)
                state->destroyed.fetch_add(1);
        }
    };
    std::unique_ptr<Payload> payload;
    Closing() : payload(std::make_unique<Payload>(this)) {}
    static void Dispatch(void* context, WorkPriority, CancelToken) noexcept
    {
        auto& state = *static_cast<Closing*>(context);
        state.dispatches.fetch_add(1);
        (void)SetEvent(state.entered.get());
        Wait(state.release.get());
        state.dispatchAlive.store(state.borrowedAlive.load() && GetCurrentThreadId() != state.owner);
    }
    static void Owner(void* context) noexcept
    {
        auto& state = *static_cast<Closing*>(context);
        state.ownerHookValid.store(state.borrowedAlive.load() && state.dispatchAlive.load() &&
                                   GetCurrentThreadId() == state.owner && state.destroyed.load() == 0);
    }
    static void Worker(void* context) noexcept
    {
        auto& state = *static_cast<Closing*>(context);
        state.workerHookValid.store(state.borrowedAlive.load() && state.ownerHookValid.load() &&
                                    GetCurrentThreadId() != state.owner);
        state.payload.reset(); // actual off-owner C++ destruction, not bookkeeping substitution
    }
};
void TestClose()
{
    WorkerPool pool;
    Closing state;
    const HRESULT initialize = pool.Initialize(1, Closing::Dispatch, &state, true);
    Expect(SUCCEEDED(initialize), "held-dispatch-initializes");
    if (FAILED(initialize))
        return;
    Expect(pool.Wake(WorkPriority::Normal), "held-dispatch-owner-wake-accepted");
    Wait(state.entered.get());
    Expect(pool.Wake(WorkPriority::High) && pool.Wake(WorkPriority::Background),
           "close-joins-with-pending-broker-hints-and-occupied-single-worker");
    std::stop_callback stopped{pool.ShutdownToken(), [&] { (void)SetEvent(state.stopped.get()); }};
    // Close joins the held real dispatch. Its borrowed frame/payload survive cancellation until this gate releases.
    std::thread releaser(
        [&]
        {
            Wait(state.stopped.get());
            if (!state.borrowedAlive.load() || state.destroyed.load() != 0)
                std::_Exit(92);
            (void)SetEvent(state.release.get());
        });
    pool.Close({Closing::Owner, Closing::Worker, &state});
    releaser.join();
    Expect(state.dispatchAlive.load() && state.ownerHookValid.load() && state.dispatches.load() == 1,
           "joined-dispatch-precedes-owner-close-hook");
    Expect(state.workerHookValid.load() && state.destroyed.load() == 1 && !state.payload,
           "actual-payload-destruction-runs-on-native-close-worker-before-context-release");
    state.borrowedAlive.store(false);
    Expect(!pool.Wake(WorkPriority::Normal), "post-close-owner-wake-refuses");
}
} // namespace
int RunWorkerWakeTests()
{
    failures = 0;
    TestCountBounds();
    TestQueues();
    TestClose();
    std::printf("%s owner-wake-pool.summary failures=%u\n", failures ? "FAIL" : "PASS", failures);
    return static_cast<int>(failures);
}
