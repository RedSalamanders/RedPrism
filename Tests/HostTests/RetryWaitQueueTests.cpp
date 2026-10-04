#include "RetryWaitQueueTests.h"

#include "RetryWaitQueue.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <windows.h>

namespace
{
int gFailures = 0;

void Expect(bool condition, const char* name) noexcept
{
    std::printf("%s retrywait.%s\n", condition ? "PASS" : "FAIL", name);
    gFailures += condition ? 0 : 1;
}

constexpr std::int32_t kInjectedFailure = HRESULT_FROM_WIN32(ERROR_ACCESS_DENIED);

struct FaultPlan final
{
    RetryWaitFault target{};
    std::size_t calls = 0;
};

std::int32_t InjectFault(void* context, RetryWaitFault fault, std::size_t) noexcept
{
    auto& plan = *static_cast<FaultPlan*>(context);
    if (fault == plan.target && ++plan.calls == 1)
        return kInjectedFailure;
    return S_OK;
}

struct ArmedLog final
{
    std::array<unsigned int, 8> retries{};
    std::array<std::uint32_t, 8> delays{};
    std::size_t count = 0;
    RetryWaitQueue* signalQueue = nullptr;
    bool signalWhenArmed = false;
    bool signalSucceeded = false;
};

void RecordArmed(void* context, JobTicket ticket, unsigned int retry, std::uint32_t delay) noexcept
{
    auto& log = *static_cast<ArmedLog*>(context);
    if (log.count < log.retries.size())
    {
        log.retries[log.count] = retry;
        log.delays[log.count] = delay;
        ++log.count;
    }
    if (log.signalWhenArmed && log.signalQueue)
        log.signalSucceeded = log.signalQueue->SignalDeadlineForTest(ticket);
}

struct Receiver final : RetryWaitReceiver
{
    std::size_t calls = 0;
    JobTicket received{};
    RetryWaitStatus status = RetryWaitStatus::Failed;
    std::int32_t error = E_UNEXPECTED;
    JobScheduler* resumeScheduler = nullptr;
    bool resume = false;
    bool resumed = false;

    void RetryWaitReady(JobTicket ticket, RetryWaitStatus result, std::int32_t resultError) noexcept override
    {
        ++calls;
        received = ticket;
        status = result;
        error = resultError;
        if (resume && resumeScheduler)
            resumed = resumeScheduler->Resume(ticket);
    }
};

struct Rig final
{
    JobScheduler scheduler;
    OwnerWorkMultiplexer routes;
    RetryWaitQueue queue;
    bool schedulerInitialized = false;
    bool routesRegistered = false;

    [[nodiscard]] bool Open(RetryWaitObserver observer = {}) noexcept
    {
        const auto initialized = scheduler.Initialize(1);
        if (FAILED(initialized))
            return false;
        schedulerInitialized = true;
        routesRegistered = scheduler.RegisterOwnerWork(routes);
        return routesRegistered && SUCCEEDED(queue.Initialize(scheduler, routes, observer));
    }

    void Close() noexcept
    {
        queue.StopNotifications();
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
        queue.CloseAfterWorkers();
    }

    ~Rig()
    {
        Close();
    }
};

bool RunFaultCase(RetryWaitFault target, bool initializeFault) noexcept
{
    Rig rig;
    FaultPlan plan{target};
    RetryWaitObserver observer{};
    observer.context = &plan;
    observer.fault = InjectFault;
    const auto schedulerResult = rig.scheduler.Initialize(1);
    if (FAILED(schedulerResult))
        return false;
    rig.schedulerInitialized = true;

    const auto result = rig.queue.Initialize(rig.scheduler, rig.routes, observer);
    if (initializeFault)
    {
        const bool failedAtBoundary = result == kInjectedFailure && plan.calls == 1;
        rig.Close();
        return failedAtBoundary;
    }

    if (FAILED(result))
    {
        rig.Close();
        return false;
    }
    Receiver receiver;
    const auto startResult = rig.queue.Start({3, 9}, 1, {}, receiver);
    const bool failedAtBoundary = startResult == kInjectedFailure && plan.calls == 1 && !rig.queue.HasPending();
    rig.Close();
    return failedAtBoundary;
}

struct DeferredEvidence final
{
    JobTicket ticket{};
    unsigned int runs = 0;
    std::array<std::uint64_t, 2> reservations{};
    std::size_t readyCalls = 0;
    bool readyTicketMatches = false;
    bool resumeSucceeded = false;
    bool completed = false;
    JobResult finalResult{};
};

class DeferredRetryJob final : public Job, public RetryWaitReceiver
{
  public:
    RetryWaitQueue& queue;
    JobScheduler& scheduler;
    DeferredEvidence& evidence;

    DeferredRetryJob(RetryWaitQueue& retryQueue, JobScheduler& jobs, DeferredEvidence& result) noexcept
        : queue(retryQueue), scheduler(jobs), evidence(result)
    {
    }

    JobResult Run(JobContext& context) override
    {
        if (evidence.runs < evidence.reservations.size())
            evidence.reservations[evidence.runs] = context.reservedMemoryBytes;
        ++evidence.runs;
        return {};
    }

    bool DeferCompletion(JobResult result, JobTicket current) noexcept override
    {
        if (result.status != JobStatus::Finished || evidence.runs != 1)
            return false;
        evidence.ticket = current;
        return SUCCEEDED(queue.Start(current, 1, {}, *this));
    }

    void RetryWaitReady(JobTicket current, RetryWaitStatus status, std::int32_t error) noexcept override
    {
        ++evidence.readyCalls;
        evidence.readyTicketMatches =
            current == evidence.ticket && status == RetryWaitStatus::Ready && SUCCEEDED(error);
        evidence.resumeSucceeded = scheduler.Resume(current);
    }

    void Completed(JobResult result) noexcept override
    {
        evidence.completed = true;
        evidence.finalResult = result;
    }
};

void TestRetryIntervalsAndValidation() noexcept
{
    constexpr std::array<std::uint32_t, 5> expectedDelays{50, 100, 200, 400, 800};
    ArmedLog log;
    RetryWaitObserver observer{};
    observer.context = &log;
    observer.armed = RecordArmed;
    Rig rig;
    const bool opened = rig.Open(observer);
    Expect(opened, "intervals.initialize");
    if (!opened)
    {
        rig.Close();
        return;
    }
    Expect(rig.queue.Initialize(rig.scheduler, rig.routes) ==
               static_cast<std::int32_t>(HRESULT_FROM_WIN32(ERROR_INVALID_STATE)),
           "initialize.duplicate_rejected");

    Receiver receiver;
    const JobTicket ticket{7, 41};
    Expect(rig.queue.Start(ticket, 0, {}, receiver) ==
               static_cast<std::int32_t>(HRESULT_FROM_WIN32(ERROR_INVALID_STATE)),
           "retry.zero_rejected");
    Expect(rig.queue.Start(ticket, 6, {}, receiver) ==
               static_cast<std::int32_t>(HRESULT_FROM_WIN32(ERROR_INVALID_STATE)),
           "retry.six_rejected");

    for (unsigned int retry = 1; retry <= expectedDelays.size(); ++retry)
    {
        const auto start = rig.queue.Start(ticket, retry, {}, receiver);
        Expect(SUCCEEDED(start), "interval.start");
        if (FAILED(start))
            break;
        Expect(rig.queue.Start(ticket, retry, {}, receiver) ==
                   static_cast<std::int32_t>(HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS)),
               "ticket.duplicate_rejected");
        Expect(rig.queue.SignalDeadlineForTest(ticket), "interval.signal_real_wait");
        Expect(SettleJobs(rig.scheduler, 5000) == SettleResult::Settled, "interval.acknowledged");
        Expect(receiver.calls == retry && receiver.received == ticket && receiver.status == RetryWaitStatus::Ready &&
                   SUCCEEDED(receiver.error),
               "interval.receiver");
    }

    Expect(log.count == expectedDelays.size(), "interval.arm_count");
    for (std::size_t index = 0; index < expectedDelays.size() && index < log.count; ++index)
    {
        Expect(log.retries[index] == index + 1, "interval.retry_number");
        Expect(log.delays[index] == expectedDelays[index], "interval.literal_delay_ms");
    }
    rig.Close();
}

void TestFaultBoundaries() noexcept
{
    Expect(RunFaultCase(RetryWaitFault::EventCreation, true), "fault.create_event");
    Expect(RunFaultCase(RetryWaitFault::RegisterWait, true), "fault.register_wait");
    Expect(RunFaultCase(RetryWaitFault::ResetEvent, false), "fault.reset_event");
    Expect(RunFaultCase(RetryWaitFault::ArmWait, false), "fault.arm_wait");
}

void TestFixedCapacityAndMuxRegistration() noexcept
{
    Rig rig;
    const bool opened = rig.Open();
    Expect(opened, "capacity.initialize");
    if (!opened)
    {
        rig.Close();
        return;
    }

    Receiver receiver;
    for (std::size_t index = 0; index < RetryWaitQueue::kCapacity; ++index)
    {
        const JobTicket ticket{index, index + 1};
        Expect(SUCCEEDED(rig.queue.Start(ticket, 1, {}, receiver)), "capacity.slot_admitted");
    }
    Expect(rig.routes.HasPending(), "capacity.multiplexer_sees_pending");
    Expect(rig.queue.Start({RetryWaitQueue::kCapacity, 99}, 1, {}, receiver) ==
               static_cast<std::int32_t>(HRESULT_FROM_WIN32(ERROR_NOT_ENOUGH_QUOTA)),
           "capacity.ninth_refused");
    for (std::size_t index = 0; index < RetryWaitQueue::kCapacity; ++index)
        Expect(rig.queue.SignalDeadlineForTest({index, index + 1}), "capacity.signal_real_wait");
    Expect(SettleJobs(rig.scheduler, 5000) == SettleResult::Settled, "capacity.all_acknowledged");
    Expect(receiver.calls == RetryWaitQueue::kCapacity, "capacity.eight_receipts");
    Expect(!rig.routes.HasPending(), "capacity.multiplexer_quiescent");
    rig.Close();
}

void TestDeferredJobResume() noexcept
{
    Rig rig;
    ArmedLog log;
    log.signalQueue = &rig.queue;
    log.signalWhenArmed = false;
    RetryWaitObserver observer{};
    observer.context = &log;
    observer.armed = RecordArmed;
    const bool opened = rig.Open(observer);
    Expect(opened, "defer.initialize");
    if (!opened)
    {
        rig.Close();
        return;
    }

    DeferredEvidence evidence{};
    std::unique_ptr<Job> job = std::make_unique<DeferredRetryJob>(rig.queue, rig.scheduler, evidence);
    JobTicket submitted{};
    constexpr std::uint64_t reservation = 4096;
    const auto submitResult = rig.scheduler.Submit(job, {JobClass::Background, 0, reservation}, submitted);
    Expect(SUCCEEDED(submitResult), "defer.submit");
    if (FAILED(submitResult))
    {
        rig.Close();
        return;
    }

    Expect(SettleJobs(rig.scheduler, 5000) == SettleResult::Settled, "defer.settled");
    Expect(!log.signalSucceeded, "defer.real-relative-deadline-fired-without-test-signal");
    Expect(evidence.completed && evidence.finalResult.status == JobStatus::Finished, "defer.completed");
    Expect(evidence.runs == 2, "defer.same_job_ran_twice");
    Expect(evidence.ticket == submitted && evidence.readyTicketMatches, "defer.same_ticket_acknowledged");
    Expect(evidence.readyCalls == 1 && evidence.resumeSucceeded, "defer.resumed_once");
    Expect(evidence.reservations[0] == reservation && evidence.reservations[1] == reservation,
           "defer.reservation_retained");
    rig.Close();
}

} // namespace

int RunRetryWaitQueueTests()
{
    const int failuresBefore = gFailures;
    TestRetryIntervalsAndValidation();
    TestFaultBoundaries();
    TestFixedCapacityAndMuxRegistration();
    TestDeferredJobResume();

    return gFailures - failuresBefore;
}
