#include "RetryWaitCancellationTests.h"
#include "RetryWaitQueue.h"
#include <atomic>
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
int gFailures = 0;
const char* gScenario = "cancel";
void Expect(bool condition, const char* name) noexcept
{
    std::printf("%s retrywait.%s.%s\n", condition ? "PASS" : "FAIL", gScenario, name);
    gFailures += condition ? 0 : 1;
}
struct State final
{
    JobScheduler* scheduler = nullptr;
    RetryWaitQueue* queue = nullptr;
    wil::unique_handle release{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    std::atomic<unsigned int> callbacks{0};
    JobTicket ticket;
    CancelToken cancellation;
    unsigned int runs = 0, readyCalls = 0;
    bool cancelAccepted = false, resumed = false, completed = false, submittedUnrelated = false;
    bool unrelatedRan = false, pendingBeforeAcknowledgement = false, reservationHeld = false;
    bool duplicateRefused = false, unrelatedSignalled = false;
    JobResult final;
    RetryWaitStatus readyStatus = RetryWaitStatus::Failed;
    bool requestCancel = true, signalFailure = false;
    unsigned int signalFaultVisits = 0;
    std::int32_t readyError = S_OK;
};
void BeforePublication(void* context) noexcept
{
    auto& state = *static_cast<State*>(context);
    state.callbacks.fetch_add(1);
    if (WaitForSingleObject(state.release.get(), 5000) != WAIT_OBJECT_0)
    {
        std::printf("FAIL retrywait.cancel.callback-gate-timeout\n");
        std::fflush(stdout);
        RaiseFailFastException(nullptr, nullptr, 0);
        std::abort();
    }
}
class Unrelated final : public Job
{
    State& _state;

  public:
    explicit Unrelated(State& state) noexcept : _state(state) {}
    JobResult Run(JobContext&) override
    {
        return {};
    }
    void Completed(JobResult result) noexcept override
    {
        _state.unrelatedRan = result.status == JobStatus::Finished;
        _state.pendingBeforeAcknowledgement = _state.queue->HasPending() && !_state.completed && _state.readyCalls == 0;
        _state.reservationHeld = _state.scheduler->ReservedMemoryBytes() == 4096;
        if (!_state.signalFailure)
            _state.unrelatedSignalled = _state.queue->SignalDeadlineForTest(_state.ticket);
        (void)SetEvent(_state.release.get());
    }
};
class CancelledRetry final : public Job, public RetryWaitReceiver
{
    State& _state;

  public:
    explicit CancelledRetry(State& state) noexcept : _state(state) {}
    JobResult Run(JobContext& context) override
    {
        ++_state.runs;
        _state.cancellation = context.cancellation;
        return {};
    }
    bool DeferCompletion(JobResult, JobTicket ticket) noexcept override
    {
        if (_state.runs != 1)
            return false;
        _state.ticket = ticket;
        if (FAILED(_state.queue->Start(ticket, 5, _state.cancellation, *this)))
            return false;
        _state.duplicateRefused =
            _state.queue->Start(ticket, 5, _state.cancellation, *this) == HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS);
        if (_state.requestCancel)
            _state.cancelAccepted = _state.scheduler->Cancel(ticket);
        std::unique_ptr<Job> unrelated = std::make_unique<Unrelated>(_state);
        JobTicket unrelatedTicket;
        _state.submittedUnrelated =
            SUCCEEDED(_state.scheduler->Submit(unrelated, {JobClass::Background, 0, 0}, unrelatedTicket));
        if (!_state.submittedUnrelated)
            (void)SetEvent(_state.release.get());
        return true;
    }
    void RetryWaitReady(JobTicket ticket, RetryWaitStatus status, std::int32_t error) noexcept override
    {
        ++_state.readyCalls;
        _state.readyStatus = status;
        _state.readyError = error;
        _state.resumed = ticket == _state.ticket && _state.scheduler->Resume(ticket);
    }
    void Completed(JobResult result) noexcept override
    {
        _state.final = result;
        _state.completed = true;
    }
};
std::int32_t SignalFault(void* context, RetryWaitFault fault, std::size_t) noexcept
{
    auto& state = *static_cast<State*>(context);
    if (state.signalFailure && fault == RetryWaitFault::SignalCancel)
    {
        ++state.signalFaultVisits;
        return E_OUTOFMEMORY;
    }
    return S_OK;
}
void RunScenario(bool cancel, bool signalFailure)
{
    State state;
    state.requestCancel = cancel;
    state.signalFailure = signalFailure;
    gScenario = signalFailure ? "signal-failure" : cancel ? "cancel" : "wait";
    JobScheduler scheduler;
    OwnerWorkMultiplexer routes;
    RetryWaitQueue queue;
    state.scheduler = &scheduler;
    state.queue = &queue;
    const bool initialized =
        state.release && SUCCEEDED(scheduler.Initialize(1)) && scheduler.RegisterOwnerWork(routes) &&
        SUCCEEDED(queue.Initialize(scheduler, routes, {&state, SignalFault, nullptr, BeforePublication}));
    Expect(initialized, "initialize");
    if (initialized)
    {
        std::unique_ptr<Job> job = std::make_unique<CancelledRetry>(state);
        JobTicket ticket;
        Expect(SUCCEEDED(scheduler.Submit(job, {JobClass::Background, 0, 4096}, ticket)), "submit");
        const auto result = SettleJobs(scheduler, 5000);
        Expect(result == SettleResult::Settled, "settled");
        Expect(state.ticket == ticket && state.cancelAccepted == cancel, "original-ticket-and-cancellation");
        Expect(state.submittedUnrelated && state.unrelatedRan, "single-compute-worker-remains-available");
        Expect(state.pendingBeforeAcknowledgement, "cancel-retains-job-until-real-wait-acknowledges");
        Expect(state.reservationHeld, "complete-reservation-survives-cancel-wait");
        Expect(state.duplicateRefused, "unacknowledged-slot-cannot-be-relabelled");
        Expect(state.unrelatedSignalled == !signalFailure && state.callbacks.load() == 1, "one-real-wait-callback");
        Expect(state.readyCalls == 1 &&
                   state.readyStatus == (cancel ? RetryWaitStatus::Cancelled : RetryWaitStatus::Ready),
               "one-acknowledgement");
        Expect(state.resumed && state.runs == (cancel ? 1u : 2u), "same-ticket-resume-or-cancelled-body-skip");
        Expect(state.completed && state.final.status == (cancel ? JobStatus::Cancelled : JobStatus::Finished),
               "one-final-result");
        Expect(state.signalFaultVisits == (signalFailure ? 1u : 0u) &&
                   state.readyError == (signalFailure ? E_OUTOFMEMORY : S_OK),
               "signal-error-retained-until-real-deadline-acknowledgement");
        Expect(!queue.HasPending() && !queue.HasReady() && scheduler.ReservedMemoryBytes() == 0,
               "no-leaked-wait-or-reservation");
    }
    (void)SetEvent(state.release.get());
    queue.StopNotifications();
    scheduler.Close();
    queue.CloseAfterWorkers();
}
} // namespace
int RunRetryWaitCancellationTests()
{
    RunScenario(false, false);
    RunScenario(true, false);
    RunScenario(true, true);
    return gFailures;
}
