#pragma once

#include "BrushStroke.h"
#include "ImageImportHistory.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <utility>

// Event-time policy. A delayed accepted press cannot read a newer mutable tool or palette setting.
struct StrokeOpeningOptions final
{
    BrushSettings settings;
    BrushKind kind = BrushKind::Paint;
    SpotHealingMode healing = SpotHealingMode::ContentAware;
    bool maskWhite = false;
    std::optional<Vec2> cloneOffset;
    Raster cloneSample;
};

class StrokeOpeningInput final
{
  public:
    StrokeOpeningInput(const StrokeOpeningInput&) = delete;
    StrokeOpeningInput& operator=(const StrokeOpeningInput&) = delete;
    StrokeOpeningInput(StrokeOpeningInput&&) noexcept = default;
    StrokeOpeningInput& operator=(StrokeOpeningInput&&) = delete;

  private:
    friend class DocumentSession;
    friend class PreparedStrokeOpening;
    StrokeOpeningInput(ImageImportAuthority authority, DocumentHistory::Authority history, CanvasDocument source,
                       Guid target, bool mask, std::int64_t pixelLimit, StrokeOpeningOptions options) noexcept
        : _authority(std::move(authority)), _history(history), _source(std::move(source)), _target(target), _mask(mask),
          _pixelLimit(pixelLimit), _options(std::move(options))
    {
    }
    ImageImportAuthority _authority;
    DocumentHistory::Authority _history;
    CanvasDocument _source;
    Guid _target;
    bool _mask;
    std::int64_t _pixelLimit;
    StrokeOpeningOptions _options;
};

// Worker-built draft. Graphics attachment and first preview update happen only after current owner authority.
// The complete CPU draft uses its producer-cached ownership bound for off-owner retirement, even on refusal.
class PreparedStrokeOpening final
{
  public:
    PreparedStrokeOpening(const PreparedStrokeOpening&) = delete;
    PreparedStrokeOpening& operator=(const PreparedStrokeOpening&) = delete;
    PreparedStrokeOpening(PreparedStrokeOpening&&) noexcept = default;
    PreparedStrokeOpening& operator=(PreparedStrokeOpening&&) = delete;
    // Bind accounting before current owner publication. Refusal retains the caller's lease unchanged.
    [[nodiscard]] bool RetainMemory(SharedRetainedMemoryLease&& lease) noexcept;
    // Only after owner apply. Retain this empty graphics borrow on the owner while the original worker
    // retires the source aggregate; it must detach before any failed/shutdown CPU transfer.
    [[nodiscard]] std::shared_ptr<BrushStroke> TakeAppliedDraftForOwner() noexcept;

  private:
    friend class DocumentSession;
    PreparedStrokeOpening(StrokeOpeningInput input, std::shared_ptr<BrushStroke> stroke) noexcept
        : _input(std::move(input)), _stroke(std::move(stroke))
    {
    }
    StrokeOpeningInput _input;
    std::shared_ptr<BrushStroke> _stroke;
    bool _applied = false;
};

enum class StrokeOpeningStatus : std::uint8_t
{
    Ready,
    Invalid,
    TooLarge,
    Memory,
    Cancelled
};

enum class StrokeOpeningStage : std::uint8_t
{
    Draft,
    Selection,
    BlurSample,
    Complete
};

struct StrokeOpeningObserver final
{
    void (*checkpoint)(void*, StrokeOpeningStage) = nullptr;
    void* context = nullptr;
};

// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4625 4626 5027)
struct StrokeOpeningResult final
{
    StrokeOpeningStatus status = StrokeOpeningStatus::Invalid;
    std::optional<PreparedStrokeOpening> prepared;
};
#pragma warning(pop)

// Complete CPU opening/live-draft declaration from cached owners and scalar grid bounds. This deliberately
// reserves the supported live tile envelope, not just an empty draft; the exact lease survives its final reader.
// Final history, final coverage scratch, GPU residency and publication have separate additional admissions.
[[nodiscard]] std::uint64_t StrokeOpeningMemoryBytes(const CanvasDocument& source, const ImageLayer& layer, bool mask,
                                                     std::int64_t pixelLimit, const BrushSettings& settings,
                                                     BrushKind kind, const Raster* cloneSample = nullptr) noexcept;
