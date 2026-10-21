#include "Host.Tests.PathReadWaitCycleExhaustion.h"

#include "../../Common/Jobs.h"
#include "../../Common/OwnerWorkMultiplexer.h"
#include "../../Common/PathReadWait.h"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <memory>
#include <windows.h>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/resource.h>
#pragma warning(pop)

namespace
{
int gFailures = 0;

constexpr auto kInvalidState = static_cast<std::int32_t>(HRESULT_FROM_WIN32(ERROR_INVALID_STATE));
constexpr auto kArithmeticOverflow = static_cast<std::int32_t>(HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW));
constexpr std::uint64_t kMaximumReadCycle = std::numeric_limits<std::uint64_t>::max();

void Expect(bool condition, const char* name) noexcept
{
    std::printf("%s path-read-wait-cycle.%s\n", condition ? "PASS" : "FAIL", name);
    if (!condition)
        ++gFailures;
}

struct SignalObservation final
{
    wil::unique_event_nothrow callbackEvent;
    std::atomic<std::uint64_t> cycle{0};
};

void ObserveWaitCallback(void* context, std::uint64_t cycle) noexcept
{
    auto& observation = *static_cast<SignalObservation*>(context);
    observation.cycle.store(cycle, std::memory_order_release);
    (void)SetEvent(observation.callbackEvent.get());
}

class TicketJob final : public Job
{
  public:
    [[nodiscard]] JobResult Run(JobContext&) override
    {
        return {};
    }

    void Completed(JobResult) noexcept override {}
};

class Receiver final : public PathReadWaitReceiver
{
  public:
    void PathReadReady(JobTicket ticket, bool cancellationOnly) noexcept override
    {
        ++calls;
        received = ticket;
        lastWasCancellationOnly = cancellationOnly;
    }

    std::size_t calls = 0;
    JobTicket received{};
    bool lastWasCancellationOnly = true;
};

struct Rig final
{
    JobScheduler scheduler;
    OwnerWorkMultiplexer routes;
    PathReadWait wait;
    SignalObservation observation;
    bool schedulerInitialized = false;
    bool routesRegistered = false;

    [[nodiscard]] bool Open(std::uint64_t initialReadCycle) noexcept
    {
        const auto eventCreated = observation.callbackEvent.create(wil::EventOptions::ManualReset);
        if (FAILED(eventCreated))
            return false;
        if (FAILED(scheduler.Initialize(1)))
            return false;
        schedulerInitialized = true;
        routesRegistered = scheduler.RegisterOwnerWork(routes);
        if (!routesRegistered)
            return false;

        PathReadWaitObserver observer{};
        observer.context = &observation;
        observer.signalled = ObserveWaitCallback;
        observer.initialReadCycleForTest = initialReadCycle;
        return SUCCEEDED(wait.Initialize(scheduler, routes, observer));
    }

    [[nodiscard]] bool SubmitRecord(JobTicket& ticket)
    {
        std::unique_ptr<Job> job = std::make_unique<TicketJob>();
        return SUCCEEDED(scheduler.Submit(job, {}, ticket)) && !job;
    }

    void Close() noexcept
    {
        wait.StopNotifications();
        wait.DetachJoinedOwner();
        if (schedulerInitialized)
        {
            if (routesRegistered && !routes.HasPending() && !routes.HasReady())
            {
                (void)scheduler.UnregisterOwnerWork(routes);
                routesRegistered = false;
            }
            scheduler.Close();
            schedulerInitialized = false;
        }
        wait.CloseAfterFinalWorkers();
    }

    ~Rig()
    {
        Close();
    }
};

[[nodiscard]] bool SignalAndPump(Rig& rig, JobTicket ticket, Receiver& receiver, std::uint64_t expectedCycle,
                                 bool signalPathEvent) noexcept
{
    if (!rig.wait.AwaitRead(ticket))
        return false;
    if (signalPathEvent && !SetEvent(static_cast<HANDLE>(rig.wait.EventForWorker())))
        return false;
    if (WaitForSingleObject(rig.observation.callbackEvent.get(), 5000) != WAIT_OBJECT_0 ||
        rig.observation.cycle.load(std::memory_order_acquire) != expectedCycle || !rig.wait.HasReady())
        return false;
    rig.routes.Pump();
    return receiver.calls > 0 && receiver.received == ticket && !receiver.lastWasCancellationOnly;
}

[[nodiscard]] bool SignalAndObserveStartup(Rig& rig, std::uint64_t expectedCycle) noexcept
{
    if (!ResetEvent(rig.observation.callbackEvent.get()) || !SetEvent(static_cast<HANDLE>(rig.wait.EventForWorker())))
        return false;
    return WaitForSingleObject(rig.observation.callbackEvent.get(), 5000) == WAIT_OBJECT_0 &&
           rig.observation.cycle.load(std::memory_order_acquire) == expectedCycle;
}

void TestMaximumCycleRefusesBeforeResetOrMutation()
{
    Rig rig;
    const bool opened = rig.Open(kMaximumReadCycle);
    Expect(opened, "max-cycle-real-scheduler-wait-pool-and-routes-initialize");
    if (!opened)
        return;

    JobTicket ticket{};
    const bool submitted = rig.SubmitRecord(ticket);
    Expect(submitted, "max-cycle-uses-registered-scheduler-ticket");
    if (!submitted)
        return;

    Receiver receiver;
    const bool eventSeeded = SetEvent(static_cast<HANDLE>(rig.wait.EventForWorker())) != FALSE;
    const auto refused = rig.wait.PrepareRead(ticket, {}, receiver, true);
    Expect(eventSeeded && refused == kArithmeticOverflow && rig.wait.ReadCycleForTest() == kMaximumReadCycle &&
               !rig.wait.HasPending() && receiver.calls == 0 &&
               WaitForSingleObject(static_cast<HANDLE>(rig.wait.EventForWorker()), 0) == WAIT_OBJECT_0,
           "max-new-read-refuses-before-event-reset-or-state-mutation");
}

void TestFinalCycleAndSameTicketRearm()
{
    Rig rig;
    const bool opened = rig.Open(kMaximumReadCycle - 1);
    Expect(opened, "final-cycle-real-scheduler-wait-pool-and-routes-initialize");
    if (!opened)
        return;

    JobTicket ticket{};
    const bool submitted = rig.SubmitRecord(ticket);
    Expect(submitted, "final-cycle-uses-registered-scheduler-ticket");
    if (!submitted)
        return;

    Receiver receiver;
    const bool startupCallbackObserved = SignalAndObserveStartup(rig, kMaximumReadCycle - 1);
    Expect(startupCallbackObserved && rig.wait.ReadCycleForTest() == kMaximumReadCycle - 1 && !rig.wait.HasPending() &&
               receiver.calls == 0,
           "seeded-startup-callback-observed-at-max-minus-one-without-receiver");
    const bool callbackGateReset = ResetEvent(rig.observation.callbackEvent.get()) != FALSE;
    const auto prepared = rig.wait.PrepareRead(ticket, {}, receiver, true);
    Expect(startupCallbackObserved && callbackGateReset && prepared == S_OK &&
               rig.wait.ReadCycleForTest() == kMaximumReadCycle && rig.wait.HasPending() &&
               WaitForSingleObject(static_cast<HANDLE>(rig.wait.EventForWorker()), 0) == WAIT_TIMEOUT,
           "startup-cycle-callback-drained-before-final-new-read-resets-event-and-reaches-max");
    if (FAILED(prepared))
        return;

    const bool firstDelivery = SignalAndPump(rig, ticket, receiver, kMaximumReadCycle, true);
    Expect(firstDelivery && receiver.calls == 1 && receiver.received == ticket &&
               rig.wait.ReadCycleForTest() == kMaximumReadCycle && !rig.wait.HasPending(),
           "final-cycle-event-callback-reaches-original-ticket");
    if (!firstDelivery)
        return;

    const bool rearmGateReset = ResetEvent(rig.observation.callbackEvent.get()) != FALSE;
    const auto rearmed = rig.wait.PrepareRead(ticket, {}, receiver, false);
    Expect(rearmGateReset && rearmed == S_OK && rig.wait.ReadCycleForTest() == kMaximumReadCycle &&
               rig.wait.HasPending() &&
               WaitForSingleObject(static_cast<HANDLE>(rig.wait.EventForWorker()), 0) == WAIT_OBJECT_0,
           "same-ticket-reset-false-rearm-at-max-preserves-cycle-and-signalled-event");
    if (FAILED(rearmed))
        return;

    const bool rearmDelivery = SignalAndPump(rig, ticket, receiver, kMaximumReadCycle, false);
    Expect(rearmDelivery && receiver.calls == 2 && receiver.received == ticket &&
               rig.wait.ReadCycleForTest() == kMaximumReadCycle && !rig.wait.HasPending(),
           "preserved-event-after-same-ticket-rearm-publishes-second-readiness");
}

void TestZeroCycleRearmRefusalIsUnchanged()
{
    Rig rig;
    const bool opened = rig.Open(0);
    Expect(opened, "zero-cycle-real-scheduler-wait-pool-and-routes-initialize");
    if (!opened)
        return;

    JobTicket ticket{};
    const bool submitted = rig.SubmitRecord(ticket);
    Expect(submitted, "zero-cycle-uses-registered-scheduler-ticket");
    if (!submitted)
        return;

    Receiver receiver;
    const bool eventSeeded = SetEvent(static_cast<HANDLE>(rig.wait.EventForWorker())) != FALSE;
    const auto refused = rig.wait.PrepareRead(ticket, {}, receiver, false);
    Expect(eventSeeded && refused == kInvalidState && rig.wait.ReadCycleForTest() == 0 && !rig.wait.HasPending() &&
               receiver.calls == 0 &&
               WaitForSingleObject(static_cast<HANDLE>(rig.wait.EventForWorker()), 0) == WAIT_OBJECT_0,
           "zero-cycle-rearm-refuses-with-cycle-owner-and-event-unchanged");
}

void TestForeignTicketRearmRefusalPreservesOriginal()
{
    Rig rig;
    const bool opened = rig.Open(0);
    Expect(opened, "foreign-rearm-real-scheduler-wait-pool-and-routes-initialize");
    if (!opened)
        return;

    JobTicket originalTicket{};
    JobTicket foreignTicket{};
    const bool bothSubmitted = rig.SubmitRecord(originalTicket) && rig.SubmitRecord(foreignTicket);
    Expect(bothSubmitted, "foreign-rearm-uses-two-registered-scheduler-tickets");
    if (!bothSubmitted)
        return;

    Receiver receiver;
    const auto prepared = rig.wait.PrepareRead(originalTicket, {}, receiver, true);
    Expect(prepared == S_OK && rig.wait.ReadCycleForTest() == 1 && rig.wait.HasPending(),
           "foreign-rearm-original-read-starts-cycle-one");
    if (FAILED(prepared))
        return;

    const bool firstDelivery = SignalAndPump(rig, originalTicket, receiver, 1, true);
    Expect(firstDelivery && receiver.calls == 1 && receiver.received == originalTicket && !rig.wait.HasPending(),
           "foreign-rearm-original-read-reaches-owner-callback");
    if (!firstDelivery)
        return;

    const auto foreignRefused = rig.wait.PrepareRead(foreignTicket, {}, receiver, false);
    Expect(foreignRefused == kInvalidState && rig.wait.ReadCycleForTest() == 1 && !rig.wait.HasPending() &&
               receiver.calls == 1 && receiver.received == originalTicket &&
               WaitForSingleObject(static_cast<HANDLE>(rig.wait.EventForWorker()), 0) == WAIT_OBJECT_0,
           "foreign-ticket-rearm-refuses-with-original-cycle-ticket-and-event-unchanged");

    const bool rearmGateReset = ResetEvent(rig.observation.callbackEvent.get()) != FALSE;
    const auto originalRearmed = rig.wait.PrepareRead(originalTicket, {}, receiver, false);
    Expect(rearmGateReset && originalRearmed == S_OK && rig.wait.ReadCycleForTest() == 1 && rig.wait.HasPending(),
           "foreign-refusal-preserves-same-original-ticket-rearm-authority");
    if (FAILED(originalRearmed))
        return;

    const bool secondDelivery = SignalAndPump(rig, originalTicket, receiver, 1, false);
    Expect(secondDelivery && receiver.calls == 2 && receiver.received == originalTicket &&
               rig.wait.ReadCycleForTest() == 1 && !rig.wait.HasPending(),
           "original-ticket-rearm-after-foreign-refusal-publishes-readiness");
}
} // namespace

[[nodiscard]] int RunHostPathReadWaitCycleExhaustionTests()
{
    gFailures = 0;
    TestMaximumCycleRefusesBeforeResetOrMutation();
    TestFinalCycleAndSameTicketRearm();
    TestZeroCycleRearmRefusalIsUnchanged();
    TestForeignTicketRearmRefusalPreservesOriginal();
    return gFailures;
}
