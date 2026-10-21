#include "Host.Tests.CancellationCheck.h"

#include "Executor.h"
#include "Jobs.h"
#include "WorkerPool.h"

#include <array>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <new>
#include <stop_token>

#include <windows.h>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/resource.h>
#pragma warning(pop)

namespace
{
constexpr DWORD kWaitMs = 5000;
unsigned int gChecks = 0;
unsigned int gFailures = 0;

void Expect(bool condition, const char* name) noexcept
{
    ++gChecks;
    std::printf("%s cancellation-check.%s\n", condition ? "PASS" : "FAIL", name);
    if (!condition)
        ++gFailures;
}

wil::unique_handle Event()
{
    wil::unique_handle result{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    if (!result)
    {
        std::printf("FAIL cancellation-check.create-event\n");
        std::fflush(stdout);
        std::_Exit(1);
    }
    return result;
}

bool Wait(HANDLE event) noexcept
{
    return WaitForSingleObject(event, kWaitMs) == WAIT_OBJECT_0;
}

struct Calls final
{
    std::atomic<unsigned int> count{0};
    static void Notify(void* context) noexcept
    {
        static_cast<Calls*>(context)->count.fetch_add(1, std::memory_order_relaxed);
    }
    CancellationObservation::Binding Binding() noexcept
    {
        return {Notify, this};
    }
};

void ScopedTokenSemantics()
{
    Calls outer;
    Calls inner;
    std::stop_source local;
    std::stop_source shutdown;
    const CancelToken token{local.get_token(), shutdown.get_token()};
    {
        const CancellationObservation::Scope scope{outer.Binding()};
        bool unchanged = true;
        for (unsigned int index = 0; index < 1000; ++index)
            unchanged = unchanged && !token.IsCancelled();
        Expect(unchanged && outer.count.load() == 0, "false-checks-never-notify");
        (void)local.request_stop();
        Expect(token.IsCancelled() && outer.count.load() == 1, "local-stop-preserves-result-and-notifies");
        {
            const CancellationObservation::Scope nested{inner.Binding()};
            Expect(token.IsCancelled() && inner.count.load() == 1 && outer.count.load() == 1,
                   "nested-scope-uses-only-current-observer");
            {
                const CancellationObservation::Scope disabled{{}};
                Expect(token.IsCancelled() && inner.count.load() == 1 && outer.count.load() == 1,
                       "empty-nested-scope-suppresses-diagnostics-only");
            }
            Expect(token.IsCancelled() && inner.count.load() == 2, "empty-scope-restores-inner-binding");
        }
        Expect(token.IsCancelled() && outer.count.load() == 2 && inner.count.load() == 2,
               "inner-scope-restores-outer-binding");
    }
    Expect(token.IsCancelled() && outer.count.load() == 2 && inner.count.load() == 2,
           "retained-token-after-run-does-not-retain-borrowed-observer");
    {
        const CancellationObservation::Scope next{inner.Binding()};
        Expect(token.IsCancelled() && inner.count.load() == 3 && outer.count.load() == 2,
               "retained-token-in-next-run-uses-next-run-binding");
    }
    std::stop_source nextLocal;
    const CancelToken shuttingDown{nextLocal.get_token(), shutdown.get_token()};
    (void)shutdown.request_stop();
    {
        const CancellationObservation::Scope scope{outer.Binding()};
        Expect(shuttingDown.IsCancelled() && outer.count.load() == 3 && !nextLocal.stop_requested(),
               "shutdown-stop-preserves-independent-local-state");
    }
    Expect(!CancellationObservation::Current().notify, "all-scopes-restore-empty-thread-binding");
}

void InheritedJoinedParticipants()
{
    WorkerPool pool;
    const HRESULT initialized = pool.Initialize(2);
    Expect(SUCCEEDED(initialized), "participant-pool-initializes");
    if (FAILED(initialized))
        return;
    auto helperDone = Event();
    Calls calls;
    CancelSource stop;
    (void)stop.Cancel();
    struct Context final
    {
        CancelToken token;
        DWORD caller;
        HANDLE helperDone;
        std::atomic<bool> helperObserved{false};
        std::atomic<bool> callerObserved{false};
    } context{stop.Token(), GetCurrentThreadId(), helperDone.get()};
    {
        const CancellationObservation::Scope scope{calls.Binding()};
        pool.RunParticipants(
            &context,
            [](void* raw) noexcept
            {
                auto& state = *static_cast<Context*>(raw);
                if (GetCurrentThreadId() == state.caller)
                    state.callerObserved.store(Wait(state.helperDone) && state.token.IsCancelled());
                else
                {
                    state.helperObserved.store(state.token.IsCancelled());
                    (void)SetEvent(state.helperDone);
                }
            },
            2);
    }
    Expect(context.helperObserved.load() && context.callerObserved.load() && calls.count.load() == 2,
           "real-helper-inherits-binding-and-caller-remains-bound-through-join");
    Expect(stop.Token().IsCancelled() && calls.count.load() == 2, "joined-participant-token-has-no-retained-callback");
    pool.Close();
}

struct Log final
{
    std::array<JobExecutionNotice, 3> notices{};
    std::atomic<unsigned int> count{0};
    std::atomic<unsigned int> overflow{0};
    std::atomic<DWORD> checkThread{0};
    std::atomic<DWORD> runThread{0};
    wil::unique_handle helperEntered{Event()};
    wil::unique_handle release{Event()};
    wil::unique_handle checkObserved{Event()};
    CancelToken retained;
    JobResult completed{};
    bool retirementHadEmptyBinding = false;
    static void Notify(void* context, JobExecutionNotice notice) noexcept
    {
        auto& self = *static_cast<Log*>(context);
        const auto index = self.count.fetch_add(1, std::memory_order_relaxed);
        if (index < self.notices.size())
            self.notices[index] = notice;
        else
            self.overflow.fetch_add(1, std::memory_order_relaxed);
        if (notice.boundary == JobExecutionBoundary::CancellationObserved)
        {
            self.checkThread.store(GetCurrentThreadId(), std::memory_order_release);
            (void)SetEvent(self.checkObserved.get());
        }
    }
};

class ParallelStopJob final : public Job
{
  public:
    explicit ParallelStopJob(Log& log) noexcept : _log(log) {}
    JobResult Run(JobContext& context) override
    {
        _log.runThread.store(GetCurrentThreadId(), std::memory_order_release);
        _log.retained = context.cancellation;
        const bool complete =
            ParallelFor(context.executor, {2, 1, 1, 1}, context.cancellation,
                        [this](Tile, CancelToken token) noexcept
                        {
                            const bool helper = GetCurrentThreadId() != _log.runThread.load(std::memory_order_acquire);
                            if (helper)
                                (void)SetEvent(_log.helperEntered.get());
                            if (!Wait(_log.release.get()) || (!helper && !Wait(_log.checkObserved.get())))
                                return false;
                            const bool first = token.IsCancelled();
                            const bool repeated = token.IsCancelled();
                            return !first && !repeated;
                        });
        return {complete ? JobStatus::Finished : JobStatus::Cancelled};
    }
    void Completed(JobResult result) noexcept override
    {
        _log.completed = result;
    }

  private:
    Log& _log;
};

class AllocationFailureJob final : public Job
{
  public:
    explicit AllocationFailureJob(Log& log) noexcept : _log(log) {}
    JobResult Run(JobContext& context) override
    {
        _log.retained = context.cancellation;
        (void)SetEvent(_log.helperEntered.get());
        if (!Wait(_log.release.get()) || !context.cancellation.IsCancelled())
            return {JobStatus::Failed, static_cast<std::int32_t>(E_FAIL)};
        throw std::bad_alloc{};
    }
    void RetireWorkerStage(JobContext&, JobResult) noexcept override
    {
        // Retirement is outside the useful Run. The unwound borrowed stack binding must already be gone.
        _log.retirementHadEmptyBinding = !CancellationObservation::Current().notify;
        (void)_log.retained.IsCancelled();
    }
    void Completed(JobResult result) noexcept override
    {
        _log.completed = result;
    }

  private:
    Log& _log;
};

void AllocationUnwindRestoresBinding()
{
    Log log;
    JobScheduler scheduler;
    const auto initialized = scheduler.Initialize(1, {}, {}, {Log::Notify, &log, Log::Notify});
    Expect(initialized >= 0, "allocation.scheduler-initializes");
    if (initialized < 0)
        return;
    const auto releaseOnFailure = wil::scope_exit(
        [&]() noexcept
        {
            (void)SetEvent(log.release.get());
            scheduler.Close();
        });
    std::unique_ptr<Job> job = std::make_unique<AllocationFailureJob>(log);
    JobTicket original;
    JobOptions options;
    options.kind = JobClass::Commit;
    options.deferredRetirement = true;
    const auto submitted = scheduler.Submit(job, options, original);
    const bool entered = submitted >= 0 && Wait(log.helperEntered.get());
    Expect(entered && !job, "allocation.original-useful-run-enters");
    const bool accepted = entered && scheduler.Cancel(original);
    (void)SetEvent(log.release.get());
    const bool settled = SettleJobs(scheduler, kWaitMs) == SettleResult::Settled;
    Expect(accepted && settled, "allocation.original-stop-settles");
    const bool sequence = settled && log.count.load() == 3 && log.overflow.load() == 0 &&
                          log.notices[0].boundary == JobExecutionBoundary::BodyEntered &&
                          log.notices[1].boundary == JobExecutionBoundary::CancellationObserved &&
                          log.notices[2].boundary == JobExecutionBoundary::BodyExited;
    Expect(sequence, "allocation.check-precedes-original-unwound-body-exit");
    Expect(sequence && log.notices[1].ticket == original && !log.notices[1].result && log.notices[2].result &&
               log.notices[2].result->status == JobStatus::Failed &&
               log.notices[2].result->error == static_cast<std::int32_t>(E_OUTOFMEMORY),
           "allocation.raw-body-failure-is-not-normalized-cancellation");
    Expect(settled && log.retirementHadEmptyBinding, "allocation.retirement-has-no-unwound-binding");
    Expect(settled && log.completed.status == JobStatus::Cancelled && log.completed.error == 0,
           "allocation.normalized-cancel-completion-unchanged");
    const auto before = log.count.load();
    Expect(settled && log.retained.IsCancelled() && log.count.load() == before,
           "allocation.retained-token-never-republishes-unwound-check");
}

void OriginalJobFirstCheck(bool shutdown)
{
    Log log;
    JobScheduler scheduler;
    const auto initialized = scheduler.Initialize(2, {}, {}, {Log::Notify, &log, Log::Notify});
    Expect(initialized >= 0, shutdown ? "shutdown.scheduler-initializes" : "local.scheduler-initializes");
    if (initialized < 0)
        return;
    const auto releaseOnFailure = wil::scope_exit(
        [&]() noexcept
        {
            (void)SetEvent(log.release.get());
            scheduler.Close();
        });
    std::unique_ptr<Job> job = std::make_unique<ParallelStopJob>(log);
    JobTicket original;
    JobOptions options;
    options.kind = JobClass::Commit;
    const auto submitted = scheduler.Submit(job, options, original);
    const bool entered = submitted >= 0 && Wait(log.helperEntered.get());
    Expect(entered && !job, shutdown ? "shutdown.real-helper-enters" : "local.real-helper-enters");
    bool accepted = false;
    if (entered)
    {
        if (shutdown)
        {
            scheduler.RequestStop();
            accepted = true;
        }
        else
            accepted = scheduler.Cancel(original);
    }
    (void)SetEvent(log.release.get());
    const bool settled = SettleJobs(scheduler, kWaitMs) == SettleResult::Settled;
    Expect(accepted && settled, shutdown ? "shutdown.original-stop-settles" : "local.original-stop-settles");
    const bool sequence = settled && log.count.load() == 3 && log.overflow.load() == 0 &&
                          log.notices[0].boundary == JobExecutionBoundary::BodyEntered &&
                          log.notices[1].boundary == JobExecutionBoundary::CancellationObserved &&
                          log.notices[2].boundary == JobExecutionBoundary::BodyExited;
    Expect(sequence,
           shutdown ? "shutdown.check-is-separate-before-body-exit" : "local.check-is-separate-before-body-exit");
    const bool identity = sequence && log.notices[1].ticket == original && log.notices[1].kind == JobClass::Commit &&
                          log.notices[1].lane == JobExecutionLane::Compute && !log.notices[1].result;
    Expect(identity,
           shutdown ? "shutdown.exact-original-identity-no-raw-result" : "local.exact-original-identity-no-raw-result");
    Expect(sequence && log.checkThread.load() != log.runThread.load() && log.checkThread.load() != 0,
           shutdown ? "shutdown.first-check-runs-on-real-helper" : "local.first-check-runs-on-real-helper");
    Expect(settled && log.completed.status == JobStatus::Cancelled && log.completed.error == 0,
           shutdown ? "shutdown.original-completion-semantics-unchanged"
                    : "local.original-completion-semantics-unchanged");
    const auto before = log.count.load();
    Expect(settled && log.retained.IsCancelled() && log.count.load() == before,
           shutdown ? "shutdown.retained-token-does-not-publish-after-run"
                    : "local.retained-token-does-not-publish-after-run");
}
} // namespace

bool RunCancellationCheckTests() noexcept
{
    gChecks = gFailures = 0;
    ScopedTokenSemantics();
    InheritedJoinedParticipants();
    OriginalJobFirstCheck(false);
    OriginalJobFirstCheck(true);
    AllocationUnwindRestoresBinding();
    const bool passed = gFailures == 0 && gChecks == 37;
    std::printf("%s Cancellation check binding: %u checks, %u failures.\n", passed ? "PASS" : "FAIL", gChecks,
                gFailures);
    return passed;
}
