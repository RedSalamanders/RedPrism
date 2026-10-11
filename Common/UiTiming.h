#pragma once

#include "UiTimingHistogram.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <source_location>

enum class UiTimingKind : std::uint8_t
{
    MessageDispatch,
    WindowMessage,
    CompletionDrain,
    ResultApply,
    UiAction,
    Frame,
    TextureCreate,
    ShaderViewCreate,
    MipGeneration,
    RasterUpload,
    CompositionRecord,
    ChromeSync,
    Count
};

// Passive interval state. Clock values are supplied by the caller; tests use independent synthetic
// timestamps. Repeated invalidations preserve the first accepted timestamp until presentation or abandonment.
class UiFrameLatency final
{
  public:
    [[nodiscard]] bool Begin(std::int64_t ticks) noexcept;
    [[nodiscard]] bool Pending() const noexcept;
    [[nodiscard]] std::optional<std::uint64_t> Complete(std::int64_t end, std::int64_t frequency) noexcept;
    void Reset() noexcept;

  private:
    std::int64_t _begin = -1;
};

inline constexpr std::size_t kUiTimingAttributionCapacity = 32;
inline constexpr std::uint64_t kUiTimingAttributionMinimumMicroseconds = 1000;

enum class UiTimingAttributionPhase : std::uint8_t
{
    Operation,
    DeferRetirement,
    DeferCompletion,
    CompletedCallback,
    PayloadDestruction,
    MemoryRefund,
    StateTransition,
    RetirementNotice,
    ActionRun,
    ActionDestruction,
    WorkerWake,
    CancellationSourceDestruction,
    Count
};

struct UiTimingAttributionIdentity final
{
    std::size_t slot;
    std::uint64_t generation;
};

// Owner-thread diagnostic ancestry only; disabled scopes and other threads have no inherited job identity.
[[nodiscard]] UiTimingAttributionIdentity CurrentUiTimingAttributionIdentity() noexcept;

// Plain fixed-size diagnostic data. Source pointers refer to source_location static strings.
struct UiTimingAttributionRecord final
{
    std::uint64_t wallMicroseconds;
    std::int64_t qpcBegin;
    std::int64_t qpcEnd;
    std::int64_t qpcFrequency;
    std::uint64_t threadCycleDelta;
    bool threadCycleKnown;
    UiTimingKind kind;
    std::uint32_t detail;
    UiTimingAttributionIdentity identity;
    UiTimingAttributionPhase phase;
    const char* file;
    const char* function;
    std::uint_least32_t line;
};

struct UiTimingStats final
{
    std::array<UiTimingHistogram, static_cast<std::size_t>(UiTimingKind::Count)> durations;
    std::uint64_t clockFailures = 0;
    UiTimingHistogram frameLatency;
    UiFrameLatency frameLatencyInterval;
    std::uint64_t abandonedFrameIntervals = 0;

    bool attributionEnabled = false;
    std::array<UiTimingAttributionRecord, kUiTimingAttributionCapacity> attributionRecords{};
    std::uint32_t attributionCursor = 0;
    std::uint64_t attributionOverflowCount = 0;
    std::uint64_t attributionClockFailures = 0;
};

// QPC durations round up to microseconds (zero ticks stays zero). Invalid clocks are refused; durations beyond
// uint64 microseconds saturate. Kept pure so boundary expectations do not depend on host scheduling.
[[nodiscard]] std::optional<std::uint64_t> UiDurationMicroseconds(std::int64_t begin, std::int64_t end,
                                                                  std::int64_t frequency) noexcept;

// Pure admission helper: synthetic values allow deterministic boundary and overflow tests.
[[nodiscard]] bool TryRecordUiTimingAttribution(
    UiTimingStats& stats, UiTimingKind kind, std::uint32_t detail, std::int64_t qpcBegin, std::int64_t qpcEnd,
    std::int64_t qpcFrequency, bool threadCycleSampleValid, std::uint64_t threadCycleBegin,
    std::uint64_t threadCycleEnd, std::source_location location = std::source_location::current(),
    UiTimingAttributionIdentity identity = {},
    UiTimingAttributionPhase phase = UiTimingAttributionPhase::Operation) noexcept;

// Optional owner-thread capture, scoped outside every operation measured into it. Nested captures restore the
// previous sink. It owns no timer or worker and never changes the application's wait/wakeup behavior.
class UiTimingCapture final
{
  public:
    explicit UiTimingCapture(UiTimingStats& stats) noexcept;
    ~UiTimingCapture();
    UiTimingCapture(const UiTimingCapture&) = delete;
    UiTimingCapture& operator=(const UiTimingCapture&) = delete;

  private:
    UiTimingStats* _previous;
};

// Disabled fast path does not read QPC. Active scopes measure inclusive elapsed wall time on their thread, including
// any nested dispatch/modal wait. Benchmarks must identify modal/device-recovery exceptions before gating it.
// The capture must outlive the scope; scopes cannot move between threads.
class ScopedUiTiming final
{
  public:
    explicit ScopedUiTiming(UiTimingKind kind, std::uint32_t detail = 0, UiTimingAttributionIdentity identity = {},
                            std::source_location location = std::source_location::current()) noexcept;
    ~ScopedUiTiming();
    ScopedUiTiming(const ScopedUiTiming&) = delete;
    ScopedUiTiming& operator=(const ScopedUiTiming&) = delete;

  private:
    UiTimingStats* _stats = nullptr;
    UiTimingKind _kind;
    std::uint32_t _detail;
    std::source_location _location;
    UiTimingAttributionIdentity _identity;
    UiTimingAttributionIdentity _previousIdentity{};
    bool _identityEntered = false;
    std::int64_t _begin = 0;
    std::uint64_t _cycleBegin = 0;
    bool _attribution = false;
    bool _cycleBeginKnown = false;
    bool _active = false;
    bool _trace = false;
};

// Shares the opt-in sink and CPU/wall recorder, without legacy histograms or PipelineTrace events.
class ScopedUiTimingAttribution final
{
  public:
    explicit ScopedUiTimingAttribution(UiTimingKind kind, UiTimingAttributionPhase phase, std::uint32_t detail = 0,
                                       UiTimingAttributionIdentity identity = {},
                                       std::source_location location = std::source_location::current()) noexcept;
    ~ScopedUiTimingAttribution();
    ScopedUiTimingAttribution(const ScopedUiTimingAttribution&) = delete;
    ScopedUiTimingAttribution& operator=(const ScopedUiTimingAttribution&) = delete;

  private:
    UiTimingStats* _stats = nullptr;
    UiTimingKind _kind;
    std::uint32_t _detail;
    UiTimingAttributionPhase _phase;
    std::source_location _location;
    UiTimingAttributionIdentity _identity;
    std::int64_t _begin = 0;
    std::uint64_t _cycleBegin = 0;
    bool _active = false;
    bool _attribution = false;
    bool _cycleBeginKnown = false;
};

// Capture-only owner hooks. Disabled calls read no clock, allocate nothing and own no timer/event.
// Presentation is reported only after a real non-occluded Present increment; failures abandon an interval.
void NoteUiFrameInvalidation() noexcept;
void NoteUiFramePresentation() noexcept;
void AbandonUiFrameLatency() noexcept;
