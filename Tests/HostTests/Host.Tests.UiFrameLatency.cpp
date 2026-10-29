#include "Host.Tests.UiFrameLatency.h"

#include "Common/UiTiming.h"

#include <cstdio>
#include <limits>
#include <optional>
#include <thread>

namespace
{
int g_failures = 0;

void Expect(bool condition, const char* name) noexcept
{
    std::printf("%s host-ui-frame-latency.%s\n", condition ? "PASS" : "FAIL", name);
    g_failures += condition ? 0 : 1;
}

void TestSyntheticIntervals() noexcept
{
    UiTimingStats untouched;
    {
        const UiTimingCapture capture{untouched};
        NoteUiFramePresentation();
        NoteUiFramePresentation();
        Expect(!untouched.frameLatencyInterval.Pending() && untouched.frameLatency.SampleCount() == 0,
               "no-invalidation-no-sample");
    }

    UiFrameLatency interval;
    Expect(interval.Begin(100), "first-invalidation-accepted");
    Expect(interval.Begin(150), "repeat-invalidation-accepted");
    Expect(interval.Complete(250, 1'000'000) == std::optional<std::uint64_t>{150},
           "repeat-invalidation-retains-first-tick");
    Expect(!interval.Pending(), "complete-clears-pending");

    Expect(interval.Begin(0), "zero-tick-start-accepted");
    Expect(interval.Pending(), "zero-tick-start-is-pending");
    Expect(interval.Complete(0, 1'000'000) == std::optional<std::uint64_t>{0}, "zero-duration-is-valid");

    Expect(!interval.Begin(-1), "negative-first-clock-rejected");
    Expect(!interval.Pending(), "negative-first-clock-leaves-no-pending-interval");
    Expect(interval.Begin(11), "valid-start-after-negative-clock");
    Expect(!interval.Complete(10, 1'000'000).has_value(), "backward-end-refused");
    Expect(!interval.Pending(), "backward-end-clears-pending");
    Expect(interval.Begin(21), "start-after-backward-end");
    Expect(!interval.Complete(22, 0).has_value(), "zero-frequency-refused");
    Expect(!interval.Pending(), "zero-frequency-clears-pending");

    Expect(interval.Begin(31), "explicit-reset-start");
    interval.Reset();
    Expect(!interval.Pending(), "explicit-reset-removes-pending");
    Expect(interval.Begin(41), "reuse-start-accepted");
    Expect(interval.Complete(43, 1'000'000) == std::optional<std::uint64_t>{2},
           "reuse-after-complete-produces-new-duration");
}

void TestDurationBoundaries() noexcept
{
    UiFrameLatency interval;
    Expect(!interval.Complete(100, 1'000'000), "completion-without-invalidation-refuses");
    const bool fractional = interval.Begin(0);
    Expect(fractional && interval.Complete(1, 3) == std::optional<std::uint64_t>{333334},
           "completed-interval-rounds-fractional-microseconds-up");
    const bool saturation = interval.Begin(0);
    Expect(saturation && interval.Complete((std::numeric_limits<std::int64_t>::max)(), 1) ==
                             std::optional<std::uint64_t>{(std::numeric_limits<std::uint64_t>::max)()},
           "completed-interval-saturates-without-wrap");
}

void TestCaptureHooksAreStructural()
{
    UiTimingStats noCapture;
    NoteUiFrameInvalidation();
    NoteUiFramePresentation();
    AbandonUiFrameLatency();
    Expect(noCapture.frameLatency.SampleCount() == 0 && noCapture.clockFailures == 0 &&
               noCapture.abandonedFrameIntervals == 0,
           "disabled-hooks-do-not-change-unrelated-stats");

    UiTimingStats outer;
    UiTimingStats inner;
    {
        const UiTimingCapture outerCapture{outer};
        NoteUiFrameInvalidation();
        NoteUiFrameInvalidation();
        Expect(outer.frameLatencyInterval.Pending(), "outer-hook-pending-after-invalidation");
        {
            const UiTimingCapture innerCapture{inner};
            NoteUiFrameInvalidation();
            AbandonUiFrameLatency();
            NoteUiFramePresentation();
        }
        Expect(inner.frameLatency.SampleCount() == 0 && inner.abandonedFrameIntervals == 1 &&
                   !inner.frameLatencyInterval.Pending(),
               "nested-hook-capture-is-independent");
        NoteUiFramePresentation();
        NoteUiFramePresentation();
    }
    Expect(outer.frameLatency.SampleCount() == 1 && !outer.frameLatencyInterval.Pending() &&
               outer.abandonedFrameIntervals == 0,
           "nested-capture-restores-outer-hook-and-presents-once");
    Expect(outer.clockFailures == 0 && inner.clockFailures == 0, "successful-hook-path-has-no-clock-failures");

    UiTimingStats worker;
    {
        const UiTimingCapture ownerCapture{outer};
        NoteUiFrameInvalidation();
        std::thread thread(
            [&worker]() noexcept
            {
                // These uncaptured worker hooks must not complete the live owner interval.
                NoteUiFrameInvalidation();
                NoteUiFramePresentation();
                {
                    const UiTimingCapture capture{worker};
                    NoteUiFrameInvalidation();
                    NoteUiFramePresentation();
                }
                NoteUiFramePresentation();
            });
        thread.join();
        Expect(worker.frameLatency.SampleCount() == 1 && !worker.frameLatencyInterval.Pending(),
               "worker-hook-capture-is-thread-local");
        Expect(outer.frameLatency.SampleCount() == 1 && outer.frameLatencyInterval.Pending(),
               "worker-does-not-write-owner-capture");
        NoteUiFramePresentation();
    }
    Expect(outer.frameLatency.SampleCount() == 2 && !outer.frameLatencyInterval.Pending(),
           "owner-completes-original-live-interval-after-worker-joins");
}

void TestAbandonmentAndInvalidIntervals() noexcept
{
    UiTimingStats stats;
    {
        const UiTimingCapture capture{stats};
        NoteUiFrameInvalidation();
        AbandonUiFrameLatency();
        AbandonUiFrameLatency();
        NoteUiFramePresentation();
    }
    Expect(stats.frameLatency.SampleCount() == 0, "abandoned-frame-does-not-record-fake-zero-or-presentation");
    Expect(stats.abandonedFrameIntervals == 1, "abandonment-has-separate-counter");

    UiFrameLatency failedClock;
    Expect(failedClock.Begin(90), "invalid-clock-case-start");
    const auto invalidDuration = failedClock.Complete(89, 1'000'000);
    Expect(!invalidDuration.has_value() && !failedClock.Pending(), "invalid-clock-refusal-clears-interval");
    Expect(stats.frameLatency.SampleCount() == 0 && stats.abandonedFrameIntervals == 1,
           "invalid-clock-is-not-counted-as-presentation-or-abandonment");
    Expect(stats.clockFailures == 0, "synthetic-refusal-does-not-claim-qpc-failure");
}
} // namespace

int RunHostUiFrameLatencyTests()
{
    g_failures = 0;
    TestSyntheticIntervals();
    TestDurationBoundaries();
    TestCaptureHooksAreStructural();
    TestAbandonmentAndInvalidIntervals();
    return g_failures;
}
