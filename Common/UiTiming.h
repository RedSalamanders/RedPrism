#pragma once

#include "UiTimingHistogram.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

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

struct UiTimingStats final
{
    std::array<UiTimingHistogram, static_cast<std::size_t>(UiTimingKind::Count)> durations;
    std::uint64_t clockFailures = 0;
    UiTimingHistogram frameLatency;
    UiFrameLatency frameLatencyInterval;
    std::uint64_t abandonedFrameIntervals = 0;
};

// QPC durations round up to microseconds (zero ticks stays zero). Invalid clocks are refused; durations beyond
// uint64 microseconds saturate. Kept pure so boundary expectations do not depend on host scheduling.
[[nodiscard]] std::optional<std::uint64_t> UiDurationMicroseconds(std::int64_t begin, std::int64_t end,
                                                                  std::int64_t frequency) noexcept;

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
    explicit ScopedUiTiming(UiTimingKind kind, std::uint32_t detail = 0) noexcept;
    ~ScopedUiTiming();
    ScopedUiTiming(const ScopedUiTiming&) = delete;
    ScopedUiTiming& operator=(const ScopedUiTiming&) = delete;

  private:
    UiTimingStats* _stats = nullptr;
    UiTimingKind _kind;
    std::uint32_t _detail;
    std::int64_t _begin = 0;
    bool _active = false;
    bool _trace = false;
};

// Capture-only owner hooks. Disabled calls read no clock, allocate nothing and own no timer/event.
// Presentation is reported only after a real non-occluded Present increment; failures abandon an interval.
void NoteUiFrameInvalidation() noexcept;
void NoteUiFramePresentation() noexcept;
void AbandonUiFrameLatency() noexcept;
