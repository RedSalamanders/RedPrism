#include "Host.Tests.UiTimingHistogram.h"

#include "Common/UiTimingHistogram.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <limits>

namespace
{
int g_failures = 0;

void Expect(bool condition, const char* name) noexcept
{
    std::printf("%s ui-timing-histogram.%s\n", condition ? "PASS" : "FAIL", name);
    if (!condition)
    {
        ++g_failures;
    }
}

void TestUpperBounds()
{
    UiTimingHistogram histogram;
    constexpr std::array<std::uint64_t, UiTimingHistogram::kBucketCount> observations{
        0,       1,
        2,       4,
        8,       16,
        32,      64,
        125,     250,
        500,     1000,
        2000,    4000,
        8000,    16000,
        33000,   50000,
        100000,  250000,
        1000000, (std::numeric_limits<std::uint64_t>::max)()};
    constexpr std::array<std::uint64_t, UiTimingHistogram::kBucketCount> expectedCounts{
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};

    for (const std::uint64_t observation : observations)
    {
        histogram.Record(observation);
    }

    Expect(histogram.BucketCounts() == expectedCounts, "every-literal-upper-bound-selects-its-bucket");
    Expect(histogram.SampleCount() == 22, "boundary-observation-sample-count");
}

void TestExactThresholdAndAdjacentValue()
{
    UiTimingHistogram histogram;
    histogram.Record(4000);
    histogram.Record(4001);

    constexpr std::array<std::uint64_t, UiTimingHistogram::kBucketCount> expectedCounts{
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0};
    Expect(histogram.BucketCounts() == expectedCounts, "4000-and-4001-straddle-threshold");
    Expect(histogram.MaximumMicroseconds() == 4001, "maximum-is-exact");
}

void TestPercentiles()
{
    UiTimingHistogram histogram;
    histogram.Record(1);
    histogram.Record(2);
    histogram.Record(4);
    histogram.Record(8);
    histogram.Record(16);

    Expect(histogram.PercentileUpperBound(50) == 4, "median-uses-nearest-rank");
    Expect(histogram.PercentileUpperBound(95) == 16, "p95-uses-ceiling-rank");
    Expect(histogram.PercentileUpperBound(0) == 1, "percentile-zero-clamps-to-one");
    Expect(histogram.PercentileUpperBound(101) == 16, "percentile-over-one-hundred-clamps-to-p100");
    Expect(histogram.PercentileUpperBound(100) == 16, "p100-returns-exact-maximum");

    UiTimingHistogram openBucket;
    openBucket.Record(1);
    openBucket.Record(2);
    openBucket.Record(1000001);
    openBucket.Record(1000002);
    Expect(openBucket.PercentileUpperBound(75) == 1000002, "open-bucket-returns-exact-maximum");
    Expect(openBucket.PercentileUpperBound(100) == 1000002, "p100-open-bucket-returns-exact-maximum");
}

void TestTotalsAndReset()
{
    UiTimingHistogram histogram;
    histogram.Record((std::numeric_limits<std::uint64_t>::max)());
    Expect(histogram.TotalMicroseconds() == (std::numeric_limits<std::uint64_t>::max)(),
           "total-at-maximum-does-not-yet-overflow");
    Expect(!histogram.TotalOverflowed(), "exact-maximum-total-has-no-overflow-indicator");
    histogram.Record(1);
    Expect(histogram.TotalMicroseconds() == (std::numeric_limits<std::uint64_t>::max)(), "overflowing-total-saturates");
    Expect(histogram.TotalOverflowed(), "overflowing-total-sets-indicator");
    Expect(histogram.SampleCount() == 2, "total-overflow-still-records-sample");

    histogram.Reset();
    constexpr std::array<std::uint64_t, UiTimingHistogram::kBucketCount> emptyCounts{};
    Expect(histogram.SampleCount() == 0, "reset-clears-sample-count");
    Expect(histogram.TotalMicroseconds() == 0, "reset-clears-total");
    Expect(!histogram.TotalOverflowed(), "reset-clears-overflow-indicator");
    Expect(histogram.MaximumMicroseconds() == 0, "reset-clears-maximum");
    Expect(histogram.BucketCounts() == emptyCounts, "reset-clears-buckets");
    Expect(histogram.PercentileUpperBound(50) == 0, "empty-percentile-is-zero");
}
} // namespace

int RunUiTimingHistogramTests()
{
    g_failures = 0;
    TestUpperBounds();
    TestExactThresholdAndAdjacentValue();
    TestPercentiles();
    TestTotalsAndReset();
    return g_failures;
}
