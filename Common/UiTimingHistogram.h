#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

// Owner-thread-only fixed histogram for UI durations in microseconds. Record and all accessors are allocation-free
// and perform no synchronization. Once the sample count saturates, later records are ignored so bucket counts and
// the sample count remain consistent.
class UiTimingHistogram final
{
  public:
    static constexpr std::size_t kBucketCount = 22;
    inline static constexpr std::array<std::uint64_t, kBucketCount> kUpperBounds{
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

    void Record(std::uint64_t durationMicroseconds) noexcept
    {
        constexpr std::uint64_t maximumCount = (std::numeric_limits<std::uint64_t>::max)();
        if (_sampleCount == maximumCount)
        {
            return;
        }

        ++_sampleCount;
        if (durationMicroseconds > maximumCount - _totalMicroseconds)
        {
            _totalMicroseconds = maximumCount;
            _totalOverflowed = true;
        }
        else
        {
            _totalMicroseconds += durationMicroseconds;
        }
        if (durationMicroseconds > _maximumMicroseconds)
        {
            _maximumMicroseconds = durationMicroseconds;
        }

        for (std::size_t index = 0; index < kUpperBounds.size(); ++index)
        {
            if (durationMicroseconds <= kUpperBounds[index])
            {
                ++_bucketCounts[index];
                break;
            }
        }
    }

    void Reset() noexcept
    {
        _bucketCounts.fill(0);
        _sampleCount = 0;
        _totalMicroseconds = 0;
        _totalOverflowed = false;
        _maximumMicroseconds = 0;
    }

    [[nodiscard]] std::uint64_t SampleCount() const noexcept
    {
        return _sampleCount;
    }

    [[nodiscard]] std::uint64_t TotalMicroseconds() const noexcept
    {
        return _totalMicroseconds;
    }

    [[nodiscard]] bool TotalOverflowed() const noexcept
    {
        return _totalOverflowed;
    }

    [[nodiscard]] std::uint64_t MaximumMicroseconds() const noexcept
    {
        return _maximumMicroseconds;
    }

    [[nodiscard]] const std::array<std::uint64_t, kBucketCount>& BucketCounts() const noexcept
    {
        return _bucketCounts;
    }

    [[nodiscard]] std::uint64_t PercentileUpperBound(unsigned int percentile) const noexcept
    {
        if (_sampleCount == 0)
        {
            return 0;
        }
        if (percentile < 1)
        {
            percentile = 1;
        }
        else if (percentile > 100)
        {
            percentile = 100;
        }
        if (percentile == 100)
        {
            return _maximumMicroseconds;
        }

        // ceil(percentile * sampleCount / 100), split before multiplication to keep the rank calculation safe.
        const std::uint64_t quotient = _sampleCount / 100;
        const std::uint64_t remainder = _sampleCount % 100;
        const std::uint64_t remainderProduct = remainder * percentile;
        const std::uint64_t rank = quotient * percentile + (remainderProduct + 99) / 100;

        std::uint64_t cumulativeCount = 0;
        for (std::size_t index = 0; index < _bucketCounts.size(); ++index)
        {
            cumulativeCount += _bucketCounts[index];
            if (cumulativeCount >= rank)
            {
                // The final bucket is open-ended; its configured upper limit is not a useful observed percentile.
                return index == kBucketCount - 1 ? _maximumMicroseconds : kUpperBounds[index];
            }
        }
        return _maximumMicroseconds;
    }

  private:
    std::array<std::uint64_t, kBucketCount> _bucketCounts{};
    std::uint64_t _sampleCount = 0;
    std::uint64_t _totalMicroseconds = 0;
    bool _totalOverflowed = false;
    std::uint64_t _maximumMicroseconds = 0;
};
