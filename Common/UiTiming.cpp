#include "UiTiming.h"

#include "PipelineTrace.h"

#include <limits>
#include <windows.h>

namespace
{
thread_local UiTimingStats* g_uiTimingStats = nullptr;

[[nodiscard]] std::int64_t Frequency() noexcept
{
    static const std::int64_t frequency = []() noexcept
    {
        LARGE_INTEGER value{};
        return QueryPerformanceFrequency(&value) ? value.QuadPart : 0;
    }();
    return frequency;
}

void ClockFailed(UiTimingStats* stats) noexcept
{
    if (stats && stats->clockFailures < (std::numeric_limits<std::uint64_t>::max)())
    {
        ++stats->clockFailures;
    }
}
} // namespace

std::optional<std::uint64_t> UiDurationMicroseconds(std::int64_t begin, std::int64_t end,
                                                    std::int64_t frequency) noexcept
{
    if (begin < 0 || end < begin || frequency <= 0)
    {
        return std::nullopt;
    }
    constexpr std::uint64_t scale = 1'000'000;
    constexpr auto maximum = (std::numeric_limits<std::uint64_t>::max)();
    const auto ticks = static_cast<std::uint64_t>(end - begin);
    const auto rate = static_cast<std::uint64_t>(frequency);
    const auto seconds = ticks / rate;
    const auto remainder = ticks % rate;
    if (seconds > maximum / scale)
    {
        return maximum;
    }
    std::uint64_t fraction = 0;
    if (remainder <= maximum / scale)
    {
        const auto scaled = remainder * scale;
        fraction = scaled / rate + (scaled % rate != 0 ? 1 : 0);
    }
    else
    {
        // Only an artificial frequency above 18 THz needs this path. Divide while multiplying by the
        // 20-bit scale. Reducing after each addition keeps both operands below rate <= INT64_MAX, so every
        // addition fits uint64. This remains exact even where MSVC long double has only 53 fraction bits.
        std::uint64_t residue = 0;
        for (std::uint64_t bit = 1U << 19; bit != 0; bit >>= 1)
        {
            fraction *= 2;
            residue *= 2;
            if (residue >= rate)
            {
                residue -= rate;
                ++fraction;
            }
            if ((scale & bit) != 0)
            {
                residue += remainder;
                if (residue >= rate)
                {
                    residue -= rate;
                    ++fraction;
                }
            }
        }
        fraction += residue != 0 ? 1 : 0;
    }
    const auto whole = seconds * scale;
    return fraction > maximum - whole ? maximum : whole + fraction;
}

UiTimingCapture::UiTimingCapture(UiTimingStats& stats) noexcept : _previous(g_uiTimingStats)
{
    g_uiTimingStats = &stats;
}

UiTimingCapture::~UiTimingCapture()
{
    g_uiTimingStats = _previous;
}

ScopedUiTiming::ScopedUiTiming(UiTimingKind kind, std::uint32_t detail) noexcept : _kind(kind), _detail(detail)
{
    if (kind >= UiTimingKind::Count)
    {
        return;
    }
    _stats = g_uiTimingStats;
    _trace = PipelineUiTimingEnabled();
    if (!_stats && !_trace)
    {
        return;
    }
    LARGE_INTEGER begin{};
    _active = QueryPerformanceCounter(&begin) != FALSE && Frequency() > 0;
    if (!_active)
    {
        ClockFailed(_stats);
        return;
    }
    _begin = begin.QuadPart;
}

ScopedUiTiming::~ScopedUiTiming()
{
    if (!_active)
    {
        return;
    }
    LARGE_INTEGER end{};
    if (!QueryPerformanceCounter(&end))
    {
        ClockFailed(_stats);
        return;
    }
    const auto duration = UiDurationMicroseconds(_begin, end.QuadPart, Frequency());
    if (!duration.has_value())
    {
        ClockFailed(_stats);
        return;
    }
    if (_stats)
    {
        _stats->durations[static_cast<std::size_t>(_kind)].Record(duration.value());
    }
    if (_trace)
    {
        TracePipelineUi(_kind, duration.value(), _detail);
    }
}

bool UiFrameLatency::Begin(std::int64_t ticks) noexcept
{
    if (ticks < 0)
        return false;
    if (!Pending())
        _begin = ticks;
    return true;
}

bool UiFrameLatency::Pending() const noexcept
{
    return _begin >= 0;
}

std::optional<std::uint64_t> UiFrameLatency::Complete(std::int64_t end, std::int64_t frequency) noexcept
{
    if (!Pending())
        return std::nullopt;
    const auto begin = _begin;
    Reset();
    return UiDurationMicroseconds(begin, end, frequency);
}

void UiFrameLatency::Reset() noexcept
{
    _begin = -1;
}

void NoteUiFrameInvalidation() noexcept
{
    auto* stats = g_uiTimingStats;
    if (!stats || stats->frameLatencyInterval.Pending())
        return;
    LARGE_INTEGER begin{};
    if (!QueryPerformanceCounter(&begin) || Frequency() <= 0 || !stats->frameLatencyInterval.Begin(begin.QuadPart))
        ClockFailed(stats);
}

void NoteUiFramePresentation() noexcept
{
    auto* stats = g_uiTimingStats;
    if (!stats || !stats->frameLatencyInterval.Pending())
        return;
    LARGE_INTEGER end{};
    if (!QueryPerformanceCounter(&end))
    {
        stats->frameLatencyInterval.Reset();
        ClockFailed(stats);
        return;
    }
    const auto duration = stats->frameLatencyInterval.Complete(end.QuadPart, Frequency());
    if (!duration)
    {
        ClockFailed(stats);
        return;
    }
    stats->frameLatency.Record(*duration);
}

void AbandonUiFrameLatency() noexcept
{
    auto* stats = g_uiTimingStats;
    if (!stats || !stats->frameLatencyInterval.Pending())
        return;
    stats->frameLatencyInterval.Reset();
    if (stats->abandonedFrameIntervals < (std::numeric_limits<std::uint64_t>::max)())
        ++stats->abandonedFrameIntervals;
}
