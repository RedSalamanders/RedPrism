#include "UiTimingTests.h"

#include "../../Common/Jobs.h"
#include "../../Common/PipelineTrace.h"
#include "../../Common/UiTiming.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <limits>
#include <memory>
#include <thread>
#include <windows.h>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/resource.h>
#pragma warning(pop)

namespace
{
int g_failures = 0;

void Expect(bool condition, const char* name) noexcept
{
    std::printf("%s ui-timing.%s\n", condition ? "PASS" : "FAIL", name);
    g_failures += condition ? 0 : 1;
}

std::uint64_t Count(const UiTimingStats& stats, UiTimingKind kind) noexcept
{
    return stats.durations[static_cast<std::size_t>(kind)].SampleCount();
}

void RecordOne(UiTimingKind kind) noexcept
{
    const ScopedUiTiming timing{kind};
}

struct SchedulerState final
{
    JobScheduler scheduler;
    unsigned int completed = 0;
    unsigned int actions = 0;
    unsigned int nestedDrains = 0;
    bool failed = false;
};

class CountAction final : public UiAction
{
  public:
    explicit CountAction(SchedulerState& state) noexcept : _state(state) {}
    void Run() noexcept override
    {
        ++_state.actions;
        _state.nestedDrains += static_cast<unsigned int>(DrainCompletions(_state.scheduler));
        _state.failed |= RunUiActions(_state.scheduler);
    }

  private:
    SchedulerState& _state;
};

class CountJob final : public Job
{
  public:
    explicit CountJob(SchedulerState& state) noexcept : _state(state) {}
    JobResult Run(JobContext&) override
    {
        // A worker scope must not write to the owner's thread-local capture.
        RecordOne(UiTimingKind::Frame);
        return {};
    }
    void Completed(JobResult result) noexcept override
    {
        ++_state.completed;
        _state.failed |= result.status != JobStatus::Finished;
        _state.nestedDrains += static_cast<unsigned int>(DrainCompletions(_state.scheduler));
    }

  private:
    SchedulerState& _state;
};
} // namespace

int RunUiTimingTests()
{
    g_failures = 0;
    constexpr auto maxSigned = (std::numeric_limits<std::int64_t>::max)();
    constexpr auto maxUnsigned = (std::numeric_limits<std::uint64_t>::max)();
    Expect(UiDurationMicroseconds(7, 7, 10'000'000) == 0, "zero-ticks");
    Expect(UiDurationMicroseconds(7, 8, 10'000'000) == 1, "sub-microsecond-rounds-up");
    Expect(UiDurationMicroseconds(100, 40100, 10'000'000) == 4000, "four-ms-exact");
    Expect(UiDurationMicroseconds(100, 40101, 10'000'000) == 4001, "four-ms-plus-one-tick");
    Expect(UiDurationMicroseconds(0, 1, 3) == 333334, "fractional-frequency");
    Expect(UiDurationMicroseconds(0, 3, 3) == 1000000, "one-second");
    Expect(!UiDurationMicroseconds(-1, 2, 10).has_value(), "negative-clock-refused");
    Expect(!UiDurationMicroseconds(2, 1, 10).has_value(), "reversed-clock-refused");
    Expect(!UiDurationMicroseconds(1, 2, 0).has_value(), "zero-frequency-refused");
    Expect(!UiDurationMicroseconds(1, 2, -10).has_value(), "negative-frequency-refused");
    Expect(UiDurationMicroseconds(0, maxSigned, 1) == maxUnsigned, "overflow-saturates");
    Expect(UiDurationMicroseconds(0, maxSigned - 1, maxSigned) == 1000000, "overflow-safe-fraction-rounds-up");
    // The ratio is exactly one half. One extra tick must survive conversion above double's precision.
    constexpr std::int64_t frequency = 8'000'000'000'000'000'000LL;
    Expect(UiDurationMicroseconds(0, frequency / 2, frequency) == 500000, "large-exact-fraction");
    Expect(UiDurationMicroseconds(0, frequency / 2 + 1, frequency) == 500001, "large-fraction-single-tick-preserved");

    UiTimingStats outer;
    UiTimingStats inner;
    UiTimingStats workerStats;
    RecordOne(UiTimingKind::Frame);
    {
        const UiTimingCapture capture{outer};
        RecordOne(UiTimingKind::Frame);
        {
            const UiTimingCapture nested{inner};
            RecordOne(UiTimingKind::Frame);
            RecordOne(UiTimingKind::Count);
            RecordOne(static_cast<UiTimingKind>(255));
        }
        std::thread worker(
            [&workerStats]() noexcept
            {
                RecordOne(UiTimingKind::Frame);
                const UiTimingCapture capture{workerStats};
                RecordOne(UiTimingKind::Frame);
            });
        worker.join();
        RecordOne(UiTimingKind::ResultApply);
        RecordOne(UiTimingKind::Frame);
    }
    RecordOne(UiTimingKind::Frame);
    Expect(Count(outer, UiTimingKind::Frame) == 2, "capture-scope-and-worker-isolation");
    Expect(Count(outer, UiTimingKind::ResultApply) == 1, "nested-capture-restores-parent");
    Expect(Count(inner, UiTimingKind::Frame) == 1, "nested-capture-own-sample");
    Expect(Count(workerStats, UiTimingKind::Frame) == 1, "worker-independent-capture");
    Expect(outer.clockFailures == 0 && inner.clockFailures == 0 && workerStats.clockFailures == 0, "real-qpc-valid");

    MSG message{};
    (void)PeekMessageW(&message, nullptr, 0, 0, PM_NOREMOVE); // establish this thread's queue
    constexpr UINT sentinel = WM_APP + 0x417;
    Expect(PostThreadMessageW(GetCurrentThreadId(), sentinel, 0, 0) != FALSE, "post-message");
    UiTimingStats pumpStats;
    unsigned int handled = 0;
    MessagePumpResult pump;
    {
        const UiTimingCapture capture{pumpStats};
        pump = PumpMessages(
            [](void* context, void* native) noexcept
            {
                auto& msg = *static_cast<MSG*>(native);
                if (msg.message == sentinel)
                {
                    ++*static_cast<unsigned int*>(context);
                }
                else
                {
                    TranslateMessage(&msg);
                    DispatchMessageW(&msg);
                }
            },
            &handled);
    }
    Expect(!pump.quit && handled == 1, "real-pump-dispatches-message");
    Expect(Count(pumpStats, UiTimingKind::MessageDispatch) == pump.dispatched, "real-pump-each-dispatch-counted");

    SchedulerState state;
    Expect(state.scheduler.Initialize(1) == S_OK, "scheduler-initialize");
    std::unique_ptr<Job> job = std::make_unique<CountJob>(state);
    JobTicket ticket;
    Expect(state.scheduler.Submit(job, {JobClass::Commit}, ticket) == S_OK, "submit-job");
    std::unique_ptr<UiAction> action = std::make_unique<CountAction>(state);
    Expect(state.scheduler.PostUiAction(action), "post-action");
    UiTimingStats schedulerStats;
    {
        const UiTimingCapture capture{schedulerStats};
        Expect(SettleJobs(state.scheduler, 10000) == SettleResult::Settled, "real-pump-settles-job-and-action");
        Expect(!RunUiActions(state.scheduler), "empty-action-is-no-op");
    }
    Expect(state.completed == 1 && state.actions == 1 && !state.failed, "callbacks-ran-once");
    Expect(state.nestedDrains == 0, "callbacks-cannot-reenter-drain");
    Expect(Count(schedulerStats, UiTimingKind::ResultApply) == 1, "one-apply-measured");
    Expect(Count(schedulerStats, UiTimingKind::UiAction) == 1, "one-action-measured");
    Expect(Count(schedulerStats, UiTimingKind::CompletionDrain) >= 1, "real-drain-measured");
    Expect(Count(schedulerStats, UiTimingKind::Frame) == 0, "job-worker-does-not-write-owner-sink");
    Expect(schedulerStats.clockFailures == 0, "scheduler-qpc-valid");
    state.scheduler.Close();
    return g_failures;
}

int RunUiTimingBenchmark()
{
    // Instrumentation overhead only: QPC brackets 31 batches of 20,000 empty scopes. Includes loop cost;
    // no pass/fail timing threshold or claim about actual image/frame latency. No ETW session is enabled.
    LARGE_INTEGER frequency{};
    if (!QueryPerformanceFrequency(&frequency) || frequency.QuadPart <= 0 ||
        FAILED(static_cast<HRESULT>(InitializePipelineTrace())))
    {
        return 1;
    }
    const auto close = wil::scope_exit([]() noexcept { ClosePipelineTrace(); });
    if (PipelineUiTimingEnabled())
    {
        std::printf("UI TIMING BENCH refused: external UI tracing is enabled\n");
        return 1;
    }
    constexpr unsigned int iterations = 20000;
    for (const bool enabled : {false, true})
    {
        UiTimingStats stats;
        std::array<double, 31> samples{};
        for (auto& sample : samples)
        {
            LARGE_INTEGER start{};
            LARGE_INTEGER end{};
            if (!QueryPerformanceCounter(&start))
            {
                return 1;
            }
            if (enabled)
            {
                const UiTimingCapture capture{stats};
                for (unsigned int i = 0; i < iterations; ++i)
                {
                    RecordOne(UiTimingKind::ResultApply);
                }
            }
            else
            {
                for (unsigned int i = 0; i < iterations; ++i)
                {
                    RecordOne(UiTimingKind::ResultApply);
                }
            }
            if (!QueryPerformanceCounter(&end))
            {
                return 1;
            }
            sample = static_cast<double>(end.QuadPart - start.QuadPart) * 1e9 /
                     static_cast<double>(frequency.QuadPart) / iterations;
        }
        std::sort(samples.begin(), samples.end());
        const auto count = Count(stats, UiTimingKind::ResultApply);
        std::printf(
            "UI TIMING BENCH capture=%s iterations=%u batches=%zu medianNs=%.1f p95Ns=%.1f maxNs=%.1f samples=%llu\n",
            enabled ? "on" : "off", iterations, samples.size(), samples[15], samples[29], samples.back(),
            static_cast<unsigned long long>(count));
        if (count != (enabled ? iterations * samples.size() : 0) || stats.clockFailures != 0)
        {
            return 1;
        }
    }
    return 0;
}
