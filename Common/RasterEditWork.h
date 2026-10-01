#pragma once

#include "Document.h"
#include "Executor.h"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>

enum class RasterEditKind : std::uint8_t
{
    Fill,
    Clear,
    Invert,
    Gradient,
};

enum class RasterEditStatus : std::uint8_t
{
    Ready,
    Unchanged,
    Invalid,
    TooLarge,
    Memory,
    Cancelled,
};

// Captured under the complete reservation, after earlier history work settles. Only value metadata and shared
// immutable raster/selection owners cross the worker boundary. Clear on a mask is the captured background fill.
struct RasterEditInput final
{
    ImageLayer layer;
    std::optional<DocumentSelection> selection;
    int canvasWidth = 0;
    int canvasHeight = 0;
    std::int64_t pixelLimit = 0;
    bool mask = false;
    RasterEditKind kind = RasterEditKind::Fill;
    std::array<double, 4> color{};
    // Gradient shades in document coordinates, with captured straight endpoints and opacity. Reverse/style
    // are resolved into these colors at capture; workers never consult a mutable palette.
    Vec2 start{};
    Vec2 end{};
    GradientShape shape = GradientShape::Linear;
    std::array<double, 4> endColor{};
    double opacity = 1;
    // Borrowed only during RunRasterEdit, never retained by its result. Observe completed tile checkpoints;
    // callbacks publish progress/cancellation state and never mutate the input or application objects.
    void (*progress)(void*, std::size_t completed, std::size_t total) noexcept = nullptr;
    void* progressContext = nullptr;
};

struct RasterEditResult final
{
    RasterEditStatus status = RasterEditStatus::Invalid;
    std::optional<ImageLayer> layer;
    // Worker-computed crop coordinates for the installed raster. Adoption is allowed only when an acknowledged
    // gradient preview has the same settings and mutation generation; this metadata alone does not authorize it.
    std::optional<BrushCommitInfo> previewCommit;
};

struct RasterEditPreviewResult final
{
    RasterEditStatus status = RasterEditStatus::Invalid;
    std::shared_ptr<BrushStroke> stroke;
};

// Allocation-free planning over borrowed metadata, before owning capture. Zero refuses invalid/overflowing
// input. Existing published input rasters/selection storage are not charged twice. Include job size separately.
[[nodiscard]] std::uint64_t RasterEditMemoryBytes(const ImageLayer& layer,
                                                  const std::optional<DocumentSelection>& selection, int canvasWidth,
                                                  int canvasHeight, std::int64_t pixelLimit, bool mask) noexcept;

// Uses only a private CPU raster path. Allocations may throw bad_alloc to the shared runner. Cancellation,
// failed growing-mask preparation and pixel-limit refusal publish no layer. Caller owns eligibility checks
// (active target, visible ancestors, settlement) and exact token/generation validation at application.
[[nodiscard]] RasterEditResult RunRasterEdit(const RasterEditInput& input, CancelToken cancellation = {});

// A private prepared CPU stroke; exclusive to the worker until returned and subsequently to the owner.
// Source owners, tiles and coverage retire together through the release sink. No caller may mutate a stroke
// while a worker borrows it. Commit preparation uses RunRasterEdit and returns a complete target layer instead.
[[nodiscard]] RasterEditPreviewResult RunRasterEditPreview(const RasterEditInput& input, CancelToken cancellation = {});
