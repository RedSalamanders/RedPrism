#include "UiTiming.h"

#include "PipelineTrace.h"

#include <limits>
#include <type_traits>
#include <windows.h>

namespace
{
thread_local UiTimingStats* g_uiTimingStats = nullptr;
thread_local UiTimingAttributionIdentity g_attributionIdentity{};
static_assert(std::is_trivial_v<UiTimingAttributionRecord>);
static_assert(std::is_trivially_copyable_v<UiTimingAttributionRecord>);
static_assert(std::is_standard_layout_v<UiTimingAttributionRecord>);

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
[[nodiscard]] bool ReadThreadCycles(std::uint64_t& cycles) noexcept
{
    ULONG64 value = 0;
    if (!QueryThreadCycleTime(GetCurrentThread(), &value))
        return false;
    cycles = static_cast<std::uint64_t>(value);
    return true;
}

void BeginAttributionScope(UiTimingStats* stats, UiTimingKind kind, bool timingNeeded, bool recordHistogram,
                           std::int64_t& begin, std::uint64_t& cycleBegin, bool& active, bool& attribution,
                           bool& cycleBeginKnown) noexcept
{
    if (kind >= UiTimingKind::Count)
        return;
    attribution = stats && stats->attributionEnabled &&
                  (!recordHistogram || kind == UiTimingKind::ResultApply || kind == UiTimingKind::UiAction ||
                   kind == UiTimingKind::ChromeSync);
    if (!timingNeeded)
        return;
    LARGE_INTEGER value{};
    active = QueryPerformanceCounter(&value) != FALSE && Frequency() > 0;
    if (!active)
    {
        if (recordHistogram)
            ClockFailed(stats);
        else if (stats && stats->attributionClockFailures < (std::numeric_limits<std::uint64_t>::max)())
            ++stats->attributionClockFailures;
        return;
    }
    begin = value.QuadPart;
    if (attribution)
        cycleBeginKnown = ReadThreadCycles(cycleBegin);
}

void FinishAttributionScope(UiTimingStats* stats, UiTimingKind kind, std::uint32_t detail,
                            const std::source_location& location, UiTimingAttributionIdentity identity,
                            UiTimingAttributionPhase phase, std::int64_t begin, std::uint64_t cycleBegin, bool active,
                            bool attribution, bool cycleBeginKnown, bool recordHistogram, bool trace) noexcept
{
    if (!active)
        return;
    LARGE_INTEGER end{};
    if (!QueryPerformanceCounter(&end))
    {
        if (recordHistogram)
            ClockFailed(stats);
        else if (stats && stats->attributionClockFailures < (std::numeric_limits<std::uint64_t>::max)())
            ++stats->attributionClockFailures;
        return;
    }
    std::uint64_t cycleEnd = 0;
    const bool cycleEndKnown = attribution && cycleBeginKnown && ReadThreadCycles(cycleEnd);
    const auto frequency = Frequency();
    const auto duration = UiDurationMicroseconds(begin, end.QuadPart, frequency);
    if (!duration)
    {
        if (recordHistogram)
            ClockFailed(stats);
        else if (stats && stats->attributionClockFailures < (std::numeric_limits<std::uint64_t>::max)())
            ++stats->attributionClockFailures;
        return;
    }
    if (recordHistogram && stats)
        stats->durations[static_cast<std::size_t>(kind)].Record(duration.value());

    if (attribution && stats)
    {
        const bool cycleValid = cycleBeginKnown && cycleEndKnown && cycleEnd >= cycleBegin;
        (void)TryRecordUiTimingAttribution(*stats, kind, detail, begin, end.QuadPart, frequency, cycleValid, cycleBegin,
                                           cycleEnd, location, identity, phase);
    }
    if (trace)
        TracePipelineUi(kind, duration.value(), detail);
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

bool TryRecordUiTimingAttribution(UiTimingStats& stats, UiTimingKind kind, std::uint32_t detail, std::int64_t qpcBegin,
                                  std::int64_t qpcEnd, std::int64_t qpcFrequency, bool threadCycleSampleValid,
                                  std::uint64_t threadCycleBegin, std::uint64_t threadCycleEnd,
                                  std::source_location location, UiTimingAttributionIdentity identity,
                                  UiTimingAttributionPhase phase) noexcept
{
    if (!stats.attributionEnabled || kind >= UiTimingKind::Count || phase >= UiTimingAttributionPhase::Count)
        return false;
    const auto duration = UiDurationMicroseconds(qpcBegin, qpcEnd, qpcFrequency);
    if (!duration || duration.value() < kUiTimingAttributionMinimumMicroseconds)
        return false;

    if (stats.attributionCursor < stats.attributionRecords.size())
    {
        const bool cyclesKnown = threadCycleSampleValid && threadCycleEnd >= threadCycleBegin;
        auto& record = stats.attributionRecords[stats.attributionCursor];
        record.wallMicroseconds = duration.value();
        record.qpcBegin = qpcBegin;
        record.qpcEnd = qpcEnd;
        record.qpcFrequency = qpcFrequency;
        record.threadCycleDelta = cyclesKnown ? threadCycleEnd - threadCycleBegin : 0;
        record.threadCycleKnown = cyclesKnown;
        record.kind = kind;
        record.detail = detail;
        record.identity = identity;
        record.phase = phase;
        record.file = location.file_name();
        record.function = location.function_name();
        record.line = location.line();
        ++stats.attributionCursor;
    }
    else if (stats.attributionOverflowCount < (std::numeric_limits<std::uint64_t>::max)())
    {
        ++stats.attributionOverflowCount;
    }
    return true;
}

UiTimingCapture::UiTimingCapture(UiTimingStats& stats) noexcept : _previous(g_uiTimingStats)
{
    g_uiTimingStats = &stats;
}

UiTimingCapture::~UiTimingCapture()
{
    g_uiTimingStats = _previous;
}

UiTimingAttributionIdentity CurrentUiTimingAttributionIdentity() noexcept
{
    return g_attributionIdentity;
}

ScopedUiTiming::ScopedUiTiming(UiTimingKind kind, std::uint32_t detail, UiTimingAttributionIdentity identity,
                               std::source_location location) noexcept
    : _kind(kind), _detail(detail), _location(location), _identity(identity)
{
    if (kind >= UiTimingKind::Count)
        return;
    _stats = g_uiTimingStats;
    _trace = PipelineUiTimingEnabled();
    BeginAttributionScope(_stats, kind, _stats != nullptr || _trace, true, _begin, _cycleBegin, _active, _attribution,
                          _cycleBeginKnown);
    if (_active && _attribution && _identity.generation != 0)
    {
        _previousIdentity = g_attributionIdentity;
        g_attributionIdentity = _identity;
        _identityEntered = true;
    }
}
ScopedUiTiming::~ScopedUiTiming()
{
    FinishAttributionScope(_stats, _kind, _detail, _location, _identity, UiTimingAttributionPhase::Operation, _begin,
                           _cycleBegin, _active, _attribution, _cycleBeginKnown, true, _trace);
    if (_identityEntered)
        g_attributionIdentity = _previousIdentity;
}

ScopedUiTimingAttribution::ScopedUiTimingAttribution(UiTimingKind kind, UiTimingAttributionPhase phase,
                                                     std::uint32_t detail, UiTimingAttributionIdentity identity,
                                                     std::source_location location) noexcept
    : _kind(kind), _detail(detail), _phase(phase), _location(location),
      _identity(identity.generation != 0 ? identity : CurrentUiTimingAttributionIdentity())
{
    if (kind >= UiTimingKind::Count || phase >= UiTimingAttributionPhase::Count)
        return;
    _stats = g_uiTimingStats;
    BeginAttributionScope(_stats, kind, _stats && _stats->attributionEnabled, false, _begin, _cycleBegin, _active,
                          _attribution, _cycleBeginKnown);
}
ScopedUiTimingAttribution::~ScopedUiTimingAttribution()
{
    FinishAttributionScope(_stats, _kind, _detail, _location, _identity, _phase, _begin, _cycleBegin, _active,
                           _attribution, _cycleBeginKnown, false, false);
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
