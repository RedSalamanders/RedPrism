#include "Composition.Tests.GpuUploadInitialization.h"

#include "GpuUploadWork.h"
#include "WorkerPool.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/resource.h>
#pragma warning(pop)

namespace
{
int gFailures = 0;

void Expect(bool condition, const char* label) noexcept
{
    std::printf("%s gpu-upload-init.%s\n", condition ? "PASS" : "FAIL", label);
    gFailures += condition ? 0 : 1;
}

bool StableMemory(void*, MemorySnapshot& snapshot) noexcept
{
    constexpr std::uint64_t available = 8ULL * 1024 * 1024 * 1024;
    snapshot = {available, available};
    return true;
}

void NoopWait(void*) noexcept {}

// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4625 4626)
struct WaitProbe final
{
    std::array<wil::unique_handle, WaitPool::kCapacity> events;
    std::array<JobEventTicket, WaitPool::kCapacity> tickets{};
    wil::unique_handle overflowEvent;

    bool CreateEvents() noexcept
    {
        for (auto& event : events)
        {
            event.reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
            if (!event)
                return false;
        }
        overflowEvent.reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
        return overflowEvent.get() != nullptr;
    }

    std::size_t Available(JobScheduler& scheduler, bool& capacityError) noexcept
    {
        std::size_t count = 0;
        capacityError = false;
        for (; count < events.size(); ++count)
        {
            const HRESULT result = static_cast<HRESULT>(
                scheduler.RegisterEventWait(events[count].get(), NoopWait, nullptr, tickets[count]));
            if (FAILED(result))
            {
                capacityError = result == HRESULT_FROM_WIN32(ERROR_NOT_ENOUGH_QUOTA);
                break;
            }
        }
        if (count == events.size())
        {
            JobEventTicket overflowTicket{};
            const HRESULT overflow = static_cast<HRESULT>(
                scheduler.RegisterEventWait(overflowEvent.get(), NoopWait, nullptr, overflowTicket));
            capacityError = overflow == HRESULT_FROM_WIN32(ERROR_NOT_ENOUGH_QUOTA);
            if (SUCCEEDED(overflow))
                capacityError = scheduler.RemoveEventWait(overflowTicket) && capacityError;
        }
        capacityError = Remove(scheduler, count) && capacityError;
        return count;
    }

    bool Hold(JobScheduler& scheduler, std::size_t count) noexcept
    {
        std::size_t held = 0;
        for (; held < count; ++held)
        {
            if (FAILED(static_cast<HRESULT>(
                    scheduler.RegisterEventWait(events[held].get(), NoopWait, nullptr, tickets[held]))))
                break;
        }
        if (held != count)
            Remove(scheduler, held);
        return held == count;
    }

    bool Remove(JobScheduler& scheduler, std::size_t count) noexcept
    {
        bool removed = true;
        for (std::size_t index = 0; index < count; ++index)
            removed = scheduler.RemoveEventWait(tickets[index]) && removed;
        return removed;
    }
};
#pragma warning(pop)

struct DummyOwner final : OwnerWorkPump
{
    [[nodiscard]] bool HasPending() const noexcept override
    {
        return false;
    }
    [[nodiscard]] bool HasReady() const noexcept override
    {
        return false;
    }
    void Pump() noexcept override {}
};

bool CheckAvailable(WaitProbe& probe, JobScheduler& scheduler, std::size_t expected, const char* label) noexcept
{
    bool capacityError = false;
    const auto available = probe.Available(scheduler, capacityError);
    const bool complete = capacityError && available == expected && available >= GpuUploadQueue::kCapacity;
    Expect(complete, label);
    return complete;
}

void CloseQueue(GpuUploadQueue& queue, const char* label) noexcept
{
    queue.StopNotifications();
    queue.CloseAfterWorkers();
    queue.CloseAfterWorkers();
    queue.StopNotifications();
    Expect(!queue.HasPending() && !queue.HasReady(), label);
}

void CheckSuccessfulRetry(JobScheduler& scheduler, WaitProbe& probe, std::size_t baseline, const char* label)
{
    GpuUploadQueue queue;
    const HRESULT initialized = queue.Initialize(scheduler);
    const bool initiallyIdle = !queue.HasPending() && !queue.HasReady();
    if (SUCCEEDED(initialized))
        queue.Pump();
    queue.StopNotifications();
    queue.CloseAfterWorkers();
    queue.CloseAfterWorkers();
    bool capacityError = false;
    const auto available = probe.Available(scheduler, capacityError);
    Expect(initialized == S_OK && initiallyIdle && !queue.HasPending() && !queue.HasReady() && capacityError &&
               available == baseline,
           label);
}

void CheckInjectedFailure(JobScheduler& scheduler, WaitProbe& probe, std::size_t baseline, GpuUploadFailurePoint point,
                          HRESULT injected, unsigned int skip, HRESULT expected, const char* label)
{
    GpuUploadQueue queue;
    queue.SetFailureForTest(point, injected, skip);
    const HRESULT actual = queue.Initialize(scheduler);
    Expect(actual == expected, label);
    Expect(queue.FailureHitsForTest() == 1, "fault-hit-once");
    CloseQueue(queue, "fault-cleanup-idle-idempotent");
    CheckAvailable(probe, scheduler, baseline, "fault-all-waits-reclaimed");
    CheckSuccessfulRetry(scheduler, probe, baseline, "fault-followed-by-successful-init");
}

void TestCapacityExhaustion(JobScheduler& scheduler, WaitProbe& probe, std::size_t baseline)
{
    Expect(baseline >= 3 + GpuUploadQueue::kCapacity, "capacity-room-for-partial-init");
    if (baseline < 3 + GpuUploadQueue::kCapacity)
        return;

    const std::size_t heldCount = baseline - 3;
    const bool held = probe.Hold(scheduler, heldCount);
    Expect(held, "capacity-hold-except-three");
    if (!held)
        return;
    GpuUploadQueue queue;
    queue.SetFailureForTest(GpuUploadFailurePoint::None);
    const HRESULT actual = queue.Initialize(scheduler);
    Expect(actual == HRESULT_FROM_WIN32(ERROR_NOT_ENOUGH_QUOTA), "capacity-fourth-registration-refused");
    Expect(queue.FailureHitsForTest() == 0, "capacity-real-error-not-injected");
    CloseQueue(queue, "capacity-partial-cleanup-idle");
    Expect(probe.Remove(scheduler, heldCount), "capacity-held-waits-remain-valid");
    CheckAvailable(probe, scheduler, baseline, "capacity-restored-after-held-waits-removed");
    CheckSuccessfulRetry(scheduler, probe, baseline, "capacity-followed-by-successful-init");
}

void TestOwnerConflict(JobScheduler& scheduler, WaitProbe& probe, std::size_t baseline)
{
    DummyOwner dummy;
    const bool registered = scheduler.RegisterOwnerWork(dummy);
    Expect(registered, "owner-conflict-dummy-registered");
    if (!registered)
        return;
    GpuUploadQueue queue;
    const HRESULT actual = queue.Initialize(scheduler);
    Expect(actual == HRESULT_FROM_WIN32(ERROR_INVALID_STATE), "owner-conflict-real-registration-refused");
    Expect(queue.FailureHitsForTest() == 0, "owner-conflict-no-injected-failure");
    CloseQueue(queue, "owner-conflict-partial-cleanup-idle");
    CheckAvailable(probe, scheduler, baseline, "owner-conflict-all-waits-reclaimed");
    Expect(scheduler.UnregisterOwnerWork(dummy), "owner-conflict-dummy-still-registered");
    CheckSuccessfulRetry(scheduler, probe, baseline, "owner-conflict-followed-by-successful-init");
}

void RunInitializationCases(JobScheduler& scheduler, WaitProbe& probe, std::size_t baseline)
{
    CheckInjectedFailure(scheduler, probe, baseline, GpuUploadFailurePoint::ImplementationAllocation, E_FAIL, 0,
                         E_OUTOFMEMORY, "allocation-boundary-out-of-memory");

    constexpr std::array<unsigned int, 3> skips{0, 3, 7};
    constexpr std::array<const char*, 3> eventLabels{"event-create-skip-0", "event-create-skip-3",
                                                     "event-create-skip-7"};
    constexpr std::array<const char*, 3> waitLabels{"wait-register-skip-0", "wait-register-skip-3",
                                                    "wait-register-skip-7"};
    for (std::size_t index = 0; index < skips.size(); ++index)
        CheckInjectedFailure(scheduler, probe, baseline, GpuUploadFailurePoint::EventCreation, E_ACCESSDENIED,
                             skips[index], E_ACCESSDENIED, eventLabels[index]);
    for (std::size_t index = 0; index < skips.size(); ++index)
        CheckInjectedFailure(scheduler, probe, baseline, GpuUploadFailurePoint::WaitRegistration, E_FAIL, skips[index],
                             E_FAIL, waitLabels[index]);

    CheckInjectedFailure(scheduler, probe, baseline, GpuUploadFailurePoint::OwnerRegistration, E_FAIL, 0, E_FAIL,
                         "owner-registration-failure-exact");
    TestCapacityExhaustion(scheduler, probe, baseline);
    TestOwnerConflict(scheduler, probe, baseline);
}
} // namespace

int RunGpuUploadInitializationTests()
{
    gFailures = 0;
    JobScheduler scheduler;
    const HRESULT initialized = static_cast<HRESULT>(scheduler.Initialize(1, {}, {StableMemory, nullptr}));
    Expect(initialized == S_OK, "scheduler-initialized");
    if (FAILED(initialized))
        return gFailures;

    WaitProbe probe;
    const bool eventsCreated = probe.CreateEvents();
    Expect(eventsCreated, "wait-probe-events-created");
    if (!eventsCreated)
    {
        scheduler.Close();
        return gFailures;
    }

    bool capacityError = false;
    const std::size_t baseline = probe.Available(scheduler, capacityError);
    Expect(capacityError && baseline >= GpuUploadQueue::kCapacity, "wait-pool-baseline-measured");
    if (capacityError && baseline >= GpuUploadQueue::kCapacity)
        RunInitializationCases(scheduler, probe, baseline);

    scheduler.Close();
    return gFailures;
}
