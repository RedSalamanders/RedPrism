#include "JobActivityTests.h"

#include "../../Common/JobActivity.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <new>

#include <windows.h>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/resource.h>
#pragma warning(pop)

namespace
{
void Expect(bool condition, const char* name, int& failures)
{
    std::printf("%s job-activity.%s\n", condition ? "PASS" : "FAIL", name);
    failures += condition ? 0 : 1;
}

JobNotice Notice(JobNoticeKind event, std::size_t slot, std::uint64_t generation, JobClass kind,
                 unsigned int percent = 0) noexcept
{
    return {event, {slot, generation}, kind, percent};
}

void TestProjection(int& failures)
{
    JobActivity activity;
    constexpr std::uint64_t qpcFrequency = 1000;

    activity.Observe(Notice(JobNoticeKind::Submitted, 4, 10, JobClass::Commit), 100);
    Expect(activity.Selected() && activity.Selected()->ticket == JobTicket{4, 10} &&
               activity.Selected()->startQpc == 100 && !activity.Selected()->percent,
           "submit-starts-foreground-clock", failures);
    Expect(!activity.Visible(349, qpcFrequency) && activity.Visible(350, qpcFrequency),
           "literal-250ms-visibility-boundary", failures);

    activity.Observe(Notice(JobNoticeKind::Submitted, 1, 20, JobClass::Stroke), 110);
    Expect(activity.Selected() && activity.Selected()->ticket == JobTicket{4, 10},
           "oldest-foreground-selected-by-generation", failures);

    activity.Observe(Notice(JobNoticeKind::Progress, 4, 10, JobClass::Commit, 30), 200);
    activity.Observe(Notice(JobNoticeKind::Progress, 4, 10, JobClass::Commit, 15), 210);
    activity.Observe(Notice(JobNoticeKind::Progress, 4, 10, JobClass::Commit, 101), 220);
    activity.Observe(Notice(JobNoticeKind::Progress, 1, 20, JobClass::Stroke, 70), 230);
    Expect(activity.Selected() && activity.Selected()->ticket == JobTicket{4, 10} &&
               activity.Selected()->percent == 30u,
           "progress-monotonic-per-job", failures);

    activity.Observe(Notice(JobNoticeKind::Submitted, 2, 30, JobClass::InteractivePreview), 240);
    activity.Observe(Notice(JobNoticeKind::Submitted, 3, 31, JobClass::Refinement), 241);
    activity.Observe(Notice(JobNoticeKind::Submitted, 5, 32, JobClass::Background), 242);
    Expect(activity.Selected() && activity.Selected()->ticket == JobTicket{4, 10},
           "preview-refinement-background-excluded", failures);

    activity.Observe(Notice(JobNoticeKind::Retired, 4, 9, JobClass::Commit), 350);
    activity.Observe(Notice(JobNoticeKind::Progress, 4, 9, JobClass::Commit, 99), 351);
    Expect(activity.Selected() && activity.Selected()->ticket == JobTicket{4, 10} &&
               activity.Selected()->percent == 30u,
           "stale-progress-and-retirement-ignored", failures);

    activity.Observe(Notice(JobNoticeKind::Retired, 4, 10, JobClass::Commit), 360);
    Expect(activity.Selected() && activity.Selected()->ticket == JobTicket{1, 20} &&
               activity.Selected()->startQpc == 110 && activity.Selected()->percent == 70u &&
               activity.Visible(360, qpcFrequency),
           "next-start-time-prevents-250ms-flicker", failures);

    activity.Observe(Notice(JobNoticeKind::Retired, 1, 20, JobClass::Stroke), 400);
    Expect(!activity.Selected() && !activity.Visible(1000, qpcFrequency), "hidden-after-all-retired", failures);

    activity.Observe(Notice(JobNoticeKind::Submitted, 6, 40, JobClass::Io), 500);
    activity.Clear();
    Expect(!activity.Selected() && !activity.Visible(1000, qpcFrequency), "clear-hides-activity", failures);

    activity.Observe(Notice(JobNoticeKind::Submitted, JobScheduler::kCapacity, 50, JobClass::Io), 600);
    activity.Observe(Notice(JobNoticeKind::Submitted, 7, 0, JobClass::Io), 601);
    activity.Observe(Notice(JobNoticeKind::Progress, 7, 0, JobClass::Io, 50), 602);
    activity.Observe(Notice(JobNoticeKind::Retired, 7, 0, JobClass::Io), 603);
    Expect(!activity.Selected(), "invalid-slot-and-generation-ignored", failures);

    activity.Observe(Notice(JobNoticeKind::Submitted, 7, 50, JobClass::Io), 610);
    activity.Observe(Notice(JobNoticeKind::Retired, 7, 49, JobClass::Io), 611);
    Expect(activity.Selected() && activity.Selected()->ticket == JobTicket{7, 50}, "slot-generation-reuse-protected",
           failures);
    activity.Observe(Notice(JobNoticeKind::Submitted, 7, 60, JobClass::Commit), 620);
    activity.Observe(Notice(JobNoticeKind::Progress, 7, 50, JobClass::Io, 99), 621);
    activity.Observe(Notice(JobNoticeKind::Retired, 7, 50, JobClass::Io), 622);
    Expect(activity.Selected() && activity.Selected()->ticket == JobTicket{7, 60} && !activity.Selected()->percent,
           "stale-notices-after-slot-reuse-ignored", failures);
}

enum class TraceKind : unsigned int
{
    Submitted,
    Progress,
    Completed,
    Retired
};

struct TraceEvent final
{
    TraceKind kind;
    JobTicket ticket;
};

struct ObserverTrace final
{
    DWORD ownerThread = GetCurrentThreadId();
    std::array<TraceEvent, 32> events{};
    std::size_t count = 0;
    bool overflow = false;
    bool wrongThread = false;
    bool contextAlive = true;
};

void Append(ObserverTrace& trace, TraceKind kind, JobTicket ticket) noexcept
{
    if (trace.count == trace.events.size())
    {
        trace.overflow = true;
        return;
    }
    trace.events[trace.count++] = {kind, ticket};
    trace.wrongThread = trace.wrongThread || GetCurrentThreadId() != trace.ownerThread;
    trace.wrongThread = trace.wrongThread || !trace.contextAlive;
}

void Observe(void* context, JobNotice notice) noexcept
{
    auto& trace = *static_cast<ObserverTrace*>(context);
    TraceKind kind = TraceKind::Submitted;
    switch (notice.event)
    {
    case JobNoticeKind::Submitted:
        kind = TraceKind::Submitted;
        break;
    case JobNoticeKind::Progress:
        kind = TraceKind::Progress;
        break;
    case JobNoticeKind::Retired:
        kind = TraceKind::Retired;
        break;
    }
    Append(trace, kind, notice.ticket);
}

wil::unique_handle Event()
{
    wil::unique_handle event{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    if (!event)
    {
        std::printf("FAIL job-activity.create-event\n");
        std::fflush(stdout);
        std::_Exit(1);
    }
    return event;
}

void Wait(HANDLE event, const char* name)
{
    if (WaitForSingleObject(event, 10000) != WAIT_OBJECT_0)
    {
        std::printf("FAIL job-activity.%s\n", name);
        std::fflush(stdout);
        std::_Exit(1);
    }
}

struct EventJobState final
{
    JobScheduler* scheduler = nullptr;
    ObserverTrace* trace = nullptr;
    HANDLE started = nullptr;
    HANDLE release = nullptr;
    JobTicket firstTicket{};
    JobTicket successorTicket{};
    bool successorSubmitted = false;
    bool callbacksOnOwner = true;
};

class EventJob final : public Job
{
  public:
    EventJob(EventJobState& state, bool gated, bool submitSuccessor, bool successor = false) noexcept
        : _state(state), _gated(gated), _submitSuccessor(submitSuccessor), _successor(successor)
    {
    }

    JobResult Run(JobContext& context) override
    {
        if (_gated)
        {
            (void)SetEvent(_state.started);
            context.ReportProgress(42);
            const HANDLE gates[] = {static_cast<HANDLE>(context.shutdownEvent), _state.release};
            const DWORD wait = WaitForMultipleObjects(2, gates, FALSE, 10000);
            if (wait == WAIT_TIMEOUT || wait == WAIT_FAILED)
            {
                std::printf("FAIL job-activity.worker-gate-timeout\n");
                std::fflush(stdout);
                std::_Exit(1);
            }
            if (wait == WAIT_OBJECT_0)
            {
                return {JobStatus::Cancelled};
            }
        }
        else if (_state.started)
        {
            (void)SetEvent(_state.started);
        }
        return {};
    }

    void Completed(JobResult) noexcept override
    {
        _state.callbacksOnOwner = _state.callbacksOnOwner && GetCurrentThreadId() == _state.trace->ownerThread;
        Append(*_state.trace, TraceKind::Completed, _successor ? _state.successorTicket : _state.firstTicket);
        if (_submitSuccessor)
        {
            std::unique_ptr<Job> next = std::make_unique<EventJob>(_state, false, false, true);
            _state.successorSubmitted =
                next && _state.scheduler->Submit(next, {JobClass::Commit}, _state.successorTicket) >= 0 && !next;
        }
    }

    void Progress(unsigned int) noexcept override
    {
        _state.callbacksOnOwner = _state.callbacksOnOwner && GetCurrentThreadId() == _state.trace->ownerThread;
    }

  private:
    EventJobState& _state;
    bool _gated;
    bool _submitSuccessor;
    bool _successor;
};

void TestSchedulerObserver(int& failures)
{
    JobScheduler scheduler;
    ObserverTrace trace;
    const HRESULT initialized = scheduler.Initialize(1, {Observe, &trace});
    Expect(SUCCEEDED(initialized), "observer-scheduler-initializes", failures);
    if (FAILED(initialized))
    {
        return;
    }

    auto started = Event();
    auto release = Event();
    EventJobState state;
    state.scheduler = &scheduler;
    state.trace = &trace;
    state.started = started.get();
    state.release = release.get();
    std::unique_ptr<Job> job = std::make_unique<EventJob>(state, true, true);
    JobTicket firstTicket;
    const HRESULT submitted = static_cast<HRESULT>(scheduler.Submit(job, {JobClass::Commit}, firstTicket));
    Expect(SUCCEEDED(submitted) && !job, "observer-first-submit", failures);
    if (FAILED(submitted))
    {
        scheduler.Close();
        return;
    }
    state.firstTicket = firstTicket;

    Wait(started.get(), "worker-started");
    Wait(static_cast<HANDLE>(scheduler.WakeHandle()), "progress-publication-wake");
    (void)DrainCompletions(scheduler);
    (void)SetEvent(release.get());
    const SettleResult settled = SettleJobs(scheduler, 10000);

    // The first completion submits a successor from the owner-thread callback. Its Submitted notice occurs
    // before the first job's Retired notice; the successor then completes and retires through the same pump.
    const bool firstLifecycle =
        trace.count >= 5 && trace.events[0].kind == TraceKind::Submitted &&
        trace.events[1].kind == TraceKind::Progress && trace.events[2].kind == TraceKind::Completed &&
        trace.events[3].kind == TraceKind::Submitted && trace.events[4].kind == TraceKind::Retired &&
        trace.events[0].ticket == firstTicket && trace.events[1].ticket == firstTicket &&
        trace.events[2].ticket == firstTicket && trace.events[4].ticket == firstTicket;
    Expect(firstLifecycle && state.successorSubmitted && settled == SettleResult::Settled,
           "observer-submit-progress-complete-successor-retire-order", failures);
    Expect(state.callbacksOnOwner && !trace.wrongThread && !trace.overflow,
           "observer-and-job-callbacks-owner-thread-context-live", failures);
    Expect(trace.count == 7 && trace.events[5].kind == TraceKind::Completed &&
               trace.events[6].kind == TraceKind::Retired && trace.events[3].ticket == state.successorTicket &&
               trace.events[5].ticket == state.successorTicket && trace.events[6].ticket == state.successorTicket,
           "successor-completes-and-each-ticket-retires", failures);
    scheduler.Close();

    // A second event-gated worker proves the observer context remains alive through Close. Close signals the
    // worker's shutdown handle, joins it, and drops its pending completion without sending observer notices.
    ObserverTrace closeTrace;
    JobScheduler closingScheduler;
    const HRESULT closeInitialized = closingScheduler.Initialize(1, {Observe, &closeTrace});
    Expect(SUCCEEDED(closeInitialized), "close-observer-scheduler-initializes", failures);
    if (FAILED(closeInitialized))
    {
        return;
    }
    auto closeStarted = Event();
    auto closeRelease = Event();
    EventJobState closeState{&closingScheduler, &closeTrace, closeStarted.get(), closeRelease.get()};
    std::unique_ptr<Job> closeJob = std::make_unique<EventJob>(closeState, true, false);
    JobTicket closeTicket;
    const HRESULT closeSubmitted = static_cast<HRESULT>(closingScheduler.Submit(closeJob, {JobClass::Io}, closeTicket));
    Expect(SUCCEEDED(closeSubmitted) && !closeJob, "close-observer-job-submitted", failures);
    if (FAILED(closeSubmitted))
    {
        closingScheduler.Close();
        return;
    }
    Wait(closeStarted.get(), "close-worker-started");
    const std::size_t noticesBeforeClose = closeTrace.count;
    closingScheduler.Close();
    closeTrace.contextAlive = false;
    Expect(noticesBeforeClose == 1 && closeTrace.count == noticesBeforeClose && !closeTrace.wrongThread,
           "close-joins-worker-and-drops-pending-notices", failures);
}
} // namespace

int RunJobActivityTests()
{
    int failures = 0;
    TestProjection(failures);
    TestSchedulerObserver(failures);
    return failures;
}
