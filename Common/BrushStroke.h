#pragma once

#include "Document.h"

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// Brush strokes (C10, Specs/Rendering/Rendering_Brush.md): the continuous-tip stroke model ported from legacy
// BrushStroke.swift and the MetalBrushCoverage kernel. A stroke edits one layer's pixels (or its mask) in 256-pixel
// tiles of the layer's own grid; a coverage engine (the compute shader in the app, the CPU here) accumulates paint
// density per tile, and the stroke composes each tile's pixels from its original content, the coverage and the
// settings. Nothing outside the touched tiles is ever allocated, and a commit shares every untouched tile.

struct GridRect
{
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    [[nodiscard]] bool Empty() const noexcept
    {
        return width <= 0 || height <= 0;
    }
    [[nodiscard]] bool operator==(const GridRect&) const noexcept = default;
    [[nodiscard]] GridRect Intersect(const GridRect& other) const noexcept;
    [[nodiscard]] GridRect Union(const GridRect& other) const noexcept;
};

// A straight piece of the stroke's centerline, in document pixels.
struct BrushSegment
{
    Vec2 from;
    Vec2 to;
};

// What the kernel needs beside the segments: the tile grid's placement and the tip.
struct BrushKernelParams
{
    Affine2 pixelToDocument; // grid pixel (top-left corner) → document
    double radius = 20;      // document pixels
    double hardness = 1;     // 0 soft … 1 hard
    double canvasWidth = 1;
    double canvasHeight = 1;
    double antialiasWidth = 1; // document pixels per grid pixel: the hard tip's edge width
    double spacing = 1;        // deposition spacing in document pixels (the soft integral's unit)
};

// One tile's buffers as the kernel sees them: `permanent` holds the settled density (soft) or coverage (hard),
// `preview` receives the coverage of settled paint plus the provisional tail, 0–255.
struct BrushKernelTile
{
    std::uint64_t key = 0;
    int x = 0; // grid position of the tile's top-left corner
    int y = 0;
    int width = 0;
    int height = 0;
    float* permanent = nullptr;
    std::uint8_t* preview = nullptr;
};

namespace BrushKernel
{
// The tip's coverage at `distance` from its center: a hard tip has an antialiased edge, a soft one the legacy
// Gaussian falloff between the hardness radius and the rim.
[[nodiscard]] double TipCoverage(double distance, const BrushKernelParams& params) noexcept;
// Optical density of the tip: coverage is 1 - exp(-density), so densities add along a stroke.
[[nodiscard]] double TipDensity(double distanceSquared, const BrushKernelParams& params) noexcept;
[[nodiscard]] double SegmentDistanceSquared(Vec2 point, const BrushSegment& segment) noexcept;
// Density a segment deposits at `point`: the tip integrated along the part of the segment within reach (8-point
// Gauss-Legendre), per deposition spacing, so sparse and dense pointer events lay down the same paint.
[[nodiscard]] double SegmentDensity(Vec2 point, const BrushSegment& segment, const BrushKernelParams& params) noexcept;
// Renders one tile exactly as the compute kernel does (the CPU engine and the tests use it).
void RenderTile(const BrushKernelTile& tile, std::span<const BrushSegment> settled, std::span<const BrushSegment> tail,
                const BrushKernelParams& params) noexcept;
} // namespace BrushKernel

// Accumulates settled segments into every tile's permanent buffer and writes each preview from the permanent paint
// plus the provisional tail. The tail is never accumulated, so replacing it leaves nothing behind.
class BrushCoverageEngine
{
  public:
    virtual ~BrushCoverageEngine() = default;
    [[nodiscard]] virtual bool Render(std::span<const BrushKernelTile> tiles, std::span<const BrushSegment> settled,
                                      std::span<const BrushSegment> tail, const BrushKernelParams& params) noexcept = 0;
    // Engines holding state per tile (GPU buffers) see the stroke's lifetime.
    virtual void BeginStroke() noexcept {}
    virtual void EndStroke() noexcept {}
    // Whether the stroke must keep a CPU permanent buffer per tile (the CPU engine reads and writes it).
    [[nodiscard]] virtual bool UsesCpuPermanent() const noexcept
    {
        return true;
    }
};

class CpuBrushCoverage final : public BrushCoverageEngine
{
  public:
    [[nodiscard]] bool Render(std::span<const BrushKernelTile> tiles, std::span<const BrushSegment> settled,
                              std::span<const BrushSegment> tail, const BrushKernelParams& params) noexcept override;
    [[nodiscard]] static CpuBrushCoverage& Shared() noexcept;
};

// The centripetal Catmull-Rom curve from `start` to `end` (through `before` and `after`) as straight segments
// within 0.2 document pixels of the spline; straight movement needs one segment even at 4K.
[[nodiscard]] std::vector<BrushSegment> BrushCurveSegments(Vec2 before, Vec2 start, Vec2 end, Vec2 after);

// Half-open bounds of nonzero alpha in premultiplied BGRA (legacy brush_alpha_bounds); empty when none.
[[nodiscard]] GridRect AlphaBounds(const std::uint8_t* bgra, int width, int height, int stride) noexcept;

class BrushStroke
{
  public:
    static constexpr int kTileSize = 256;
    static constexpr int kMaxGridSide = 1'000'000'000;

    struct Tile
    {
        std::uint64_t key = 0;
        GridRect rect;
        std::vector<float> permanent;       // settled density (soft) or coverage (hard); CPU engines only
        std::vector<std::uint8_t> coverage; // preview coverage, 0–255
        Raster base;                        // the layer's pixels under the tile (fill outside them)
        Raster image;                       // the tile as it looks now: base, paint and coverage composed
        bool touched = false;
        bool changed = false;
    };

    struct Commit
    {
        Raster image;
        Raster thumbnail;
        LayerTransform transform;
        GridRect bounds; // grid rectangle the image covers
    };

    // A stroke on `layer`'s pixels, or with `mask` on its mask (in the mask's own grid when it has a placement).
    // The grid is the layer's pixel grid grown to cover the canvas, so paint beyond the layer's pixels extends it.
    BrushStroke(const ImageLayer& layer, bool mask, const BrushSettings& settings, int canvasWidth, int canvasHeight,
                BrushCoverageEngine* engine);
    ~BrushStroke();
    BrushStroke(const BrushStroke&) = delete;
    BrushStroke& operator=(const BrushStroke&) = delete;

    [[nodiscard]] bool IsValid() const noexcept
    {
        return _valid;
    }
    void SetPixelLimit(std::int64_t pixels) noexcept
    {
        _pixelLimit = pixels;
    }
    // Gray8 coverage at document resolution that limits the edit (nullopt: the whole canvas).
    void SetSelectionClip(std::optional<Raster> coverage) noexcept
    {
        _selection = std::move(coverage);
    }
    // Adds a pointer sample: the settled curve piece before it and a straight provisional tail to it. False when a
    // tile could not be allocated within the pixel budget or the engine failed; the stroke is then unusable.
    [[nodiscard]] bool Append(Vec2 point);
    // Replaces the provisional tail with the stroke's final curve piece. Safe to repeat.
    [[nodiscard]] bool Flush();
    [[nodiscard]] const std::vector<Tile>& Tiles() const noexcept
    {
        return _tiles;
    }
    // Indexes of the tiles whose image changed since the last call.
    [[nodiscard]] std::vector<std::size_t> TakeChanged();
    [[nodiscard]] bool HasPaint() const noexcept;
    // The touched tiles as replacement tiles of the grid.
    [[nodiscard]] std::vector<RasterTile> Patches() const;
    // The stroke committed: a sparse raster sharing the layer's pixels, its thumbnail, the transform placing it and
    // the grid bounds it covers (painting only adds alpha, so bounds come from the touched tiles).
    [[nodiscard]] std::optional<Commit> PaintSnapshot() const;
    [[nodiscard]] LayerTransform TransformFor(const GridRect& bounds) const noexcept;

    // Clone Stamp, Blur and warp results paint from `sample` (document-size premultiplied BGRA, or Gray8 on a mask)
    // read at the grid pixel's document position shifted by the offset, through the coverage: over the pixels under
    // the tip, or replacing them (`replaces`).
    void SetSample(Raster sample, int offsetX, int offsetY, bool replaces);
    [[nodiscard]] bool HasSample() const noexcept
    {
        return _sample.has_value();
    }
    // The history entry the commit records (empty: the default per kind).
    void SetEditName(std::wstring name)
    {
        _editName = std::move(name);
    }
    [[nodiscard]] const std::wstring& EditName() const noexcept
    {
        return _editName;
    }
    // Spot Healing once the stroke ends: rebuilds the painted area from nearby texture (`SpotHealing::Heal`) into the
    // tiles, so the usual commit applies it as one entry. Reads the layer's original pixels, never the dark wash
    // shown while painting. False when memory runs out.
    [[nodiscard]] bool Heal(std::uint32_t seed);
    // Replaces this edit with a gradient over the whole canvas (or the selection) composited onto the original
    // pixels; redrawing restarts from each tile's original content, so moving the line never accumulates. Colors
    // are straight red, green, blue, alpha in 0…1 (a mask takes the first component as its gray).
    [[nodiscard]] bool FillGradient(GradientShape shape, Vec2 start, Vec2 end, const std::array<double, 4>& from,
                                    const std::array<double, 4>& to, double opacity);
    // Fills the selection (or the whole canvas) with a straight color.
    [[nodiscard]] bool Fill(const std::array<double, 4>& color);
    // Erases image pixels to transparency inside the selection, only where pixels exist.
    [[nodiscard]] bool ClearPixels();
    // Cuts the selected pixels out of the original image; false when nothing is lifted (no selection, a mask, no
    // pixels under it).
    [[nodiscard]] bool LiftSelection();
    // Rebuilds the affected tiles from the original: the selection becomes a transparent hole (unless `duplicate`)
    // and the lifted pixels are placed `offset` document pixels away.
    [[nodiscard]] bool MoveLifted(Vec2 offset, bool duplicate);
    // Inverts the pixels (transparency kept; a mask's gray) inside the selection, or everywhere the layer has pixels.
    [[nodiscard]] bool Invert();

    [[nodiscard]] const Guid& LayerID() const noexcept
    {
        return _layerID;
    }
    [[nodiscard]] bool IsMask() const noexcept
    {
        return _mask;
    }
    [[nodiscard]] const BrushSettings& Settings() const noexcept
    {
        return _settings;
    }
    [[nodiscard]] int Width() const noexcept
    {
        return _width;
    }
    [[nodiscard]] int Height() const noexcept
    {
        return _height;
    }
    [[nodiscard]] const GridRect& SourceRect() const noexcept
    {
        return _sourceRect;
    }
    [[nodiscard]] const Affine2& PixelToDocument() const noexcept
    {
        return _pixelToDocument;
    }
    // The transform placing the whole grid (what the live preview draws).
    [[nodiscard]] const LayerTransform& PaintTransform() const noexcept
    {
        return _paintTransform;
    }
    [[nodiscard]] const Raster& SourceImage() const noexcept
    {
        return _source;
    }
    [[nodiscard]] const LayerTransform& SourceTransform() const noexcept
    {
        return _sourceTransform;
    }
    [[nodiscard]] GridRect CommittedBounds() const noexcept;
    [[nodiscard]] BrushKernelParams KernelParams() const noexcept;
    [[nodiscard]] std::uint8_t FillValue() const noexcept
    {
        return _mask ? 255 : 0;
    }

  private:
    [[nodiscard]] bool Render(const std::vector<BrushSegment>& settled, const std::vector<BrushSegment>& tail);
    void TileKeys(std::span<const BrushSegment> segments, std::unordered_set<std::uint64_t>& keys) const;
    [[nodiscard]] bool AllocateTile(std::uint64_t key);
    void ComposeTile(Tile& tile);
    [[nodiscard]] double SelectionAt(Vec2 document) const noexcept;
    // Runs `shade` in document coordinates over every tile the canvas and selection cover (only the layer's existing
    // pixels with `withinSource`), starting from each tile's original content; `shade` returns false to leave a
    // pixel alone. `destinationOut` takes the shaded alpha out of the pixels instead of painting.
    enum class PaintMode
    {
        Over,
        DestinationOut,
        Invert,
    };
    [[nodiscard]] bool PaintCanvas(bool withinSource, double opacity, PaintMode mode,
                                   const std::function<bool(Vec2, std::array<double, 4>&)>& shade);

    Guid _layerID;
    bool _mask = false;
    BrushSettings _settings;
    int _canvasWidth = 1;
    int _canvasHeight = 1;
    int _width = 1;
    int _height = 1;
    GridRect _sourceRect;
    Affine2 _pixelToDocument;
    LayerTransform _paintTransform;
    LayerTransform _sourceTransform;
    Raster _source;
    BrushCoverageEngine* _engine = nullptr;
    bool _valid = false;
    bool _failed = false;
    std::int64_t _pixelLimit = 100'000'000;
    std::optional<Raster> _selection;
    std::optional<GridRect> _allocated;
    std::vector<Vec2> _samples;
    std::unordered_set<std::uint64_t> _tailKeys;
    std::vector<Tile> _tiles;
    std::unordered_map<std::uint64_t, std::size_t> _index;
    std::optional<Raster> _sample;
    int _sampleX = 0;
    int _sampleY = 0;
    bool _replaces = false;
    std::wstring _editName;
    struct Lifted
    {
        Raster image;
        GridRect rect;
    };
    std::optional<Lifted> _lifted;
    std::vector<std::uint64_t> _moveTiles;
};
