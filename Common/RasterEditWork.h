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
    PixelMove,
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
    // PixelMove offsets round in document pixels. Duplicate keeps the original coverage instead of a hole.
    Vec2 offset{};
    bool duplicate = false;
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
    // PixelMove owns the completely prepared translated outline; other kinds leave it absent.
    std::optional<DocumentSelection> selection;
};

// Only the detached factory creates this bound pair. Publication cannot be supplied for unrelated pixels or
// selection geometry. After applying, it owns the retired live layer/selection and history containers.
class PreparedRasterEdit final
{
  public:
    PreparedRasterEdit(const PreparedRasterEdit&) = delete;
    PreparedRasterEdit& operator=(const PreparedRasterEdit&) = delete;
    PreparedRasterEdit(PreparedRasterEdit&&) noexcept = default;
    PreparedRasterEdit& operator=(PreparedRasterEdit&&) noexcept = default;
    [[nodiscard]] const ImageLayer& Layer() const noexcept
    {
        return _raster.layer.value();
    }
    [[nodiscard]] const std::optional<DocumentSelection>& Selection() const noexcept
    {
        return _raster.selection;
    }
    [[nodiscard]] const std::optional<BrushCommitInfo>& PreviewCommit() const noexcept
    {
        return _raster.previewCommit;
    }

  private:
    friend class DocumentSession;
    friend std::optional<PreparedRasterEdit> PrepareRasterEditPublication(
        RasterEditKind, bool, const std::optional<CanvasDocument>&, const std::optional<Guid>&, JobToken,
        DocumentHistory::StateView, RasterEditResult&&, std::wstring, CancelToken, DocumentHistory::PrepareObserver);
    PreparedRasterEdit(RasterEditKind kind, bool mask, JobToken token, RasterEditResult&& raster,
                       DocumentHistory::Publication&& history) noexcept
        : _kind(kind), _mask(mask), _token(std::move(token)), _raster(std::move(raster)), _history(std::move(history))
    {
    }
    RasterEditKind _kind;
    bool _mask;
    JobToken _token;
    RasterEditResult _raster;
    DocumentHistory::Publication _history;
    std::optional<BrushCommitInfo> _retiredBrushCommit;
    std::optional<GradientEdit> _retiredGradient;
    std::optional<PixelMove> _retiredPixelMove;
};

// source/view/token are captured under whole admission. All after-document construction, history snapshots,
// growth and trim occur on this calling worker; failed preparation never mutates live session/history state.
// Refusal leaves `raster` with its caller. Existing source layer records remain value copies until H2.
[[nodiscard]] std::optional<PreparedRasterEdit> PrepareRasterEditPublication(
    RasterEditKind kind, bool mask, const std::optional<CanvasDocument>& source, const std::optional<Guid>& active,
    JobToken token, DocumentHistory::StateView history, RasterEditResult&& raster, std::wstring name,
    CancelToken cancellation = {}, DocumentHistory::PrepareObserver observer = {});

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

// Adds private lifted pixels, landing/hole indexes and moved immutable outline storage to the raster-edit graph.
// Requires an image target and selection. Zero refuses invalid or overflowing geometry.
[[nodiscard]] std::uint64_t PixelMoveMemoryBytes(const ImageLayer& layer, const DocumentSelection& selection,
                                                 int canvasWidth, int canvasHeight, std::int64_t pixelLimit) noexcept;

// Uses only a private CPU raster path. Allocations may throw bad_alloc to the shared runner. Cancellation,
// failed growing-mask preparation and pixel-limit refusal publish no layer. Caller owns eligibility checks
// (active target, visible ancestors, settlement) and exact token/generation validation at application.
[[nodiscard]] RasterEditResult RunRasterEdit(const RasterEditInput& input, CancelToken cancellation = {});

// A private prepared CPU stroke; exclusive to the worker until returned and subsequently to the owner.
// Source owners, tiles and coverage retire together through the release sink. No caller may mutate a stroke
// while a worker borrows it. Commit preparation uses RunRasterEdit and returns a complete target layer instead.
[[nodiscard]] RasterEditPreviewResult RunRasterEditPreview(const RasterEditInput& input, CancelToken cancellation = {});
