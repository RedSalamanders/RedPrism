#pragma once

#include "Document.h"
#include "Executor.h"

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
// Exclusive worker buffers. Checks cancellation between rows and exposes no successful partial result.
[[nodiscard]] bool RenderTileForWork(const BrushKernelTile& tile, std::span<const BrushSegment> settled,
                                     std::span<const BrushSegment> tail, const BrushKernelParams& params,
                                     const CancelToken& cancellation) noexcept;
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

// `placed` places a `width` × `height` pixel grid on the document through `pixelToDocument`
// (`PixelToDocument(placed, width, height)`); the placement of just `rect` of that grid (a crop when smaller, a
// grow when it extends past the grid), keeping `placed`'s rotation, flips and sampling: `rect` scales `placed`'s
// size by its own share of the grid, and `rect`'s center lands where `pixelToDocument` puts it. Shared by a
// stroke's own grid (construction, `TransformFor`, merging a floating selection) and a trim to alpha
// (Content-Aware Fill's grown grid, `Adjust::TrimmedToAlpha`). The overload without `pixelToDocument` computes it,
// for a caller that does not already have it.
[[nodiscard]] LayerTransform PlacementOfRect(const LayerTransform& placed, int width, int height,
                                             const Affine2& pixelToDocument, const GridRect& rect) noexcept;
[[nodiscard]] LayerTransform PlacementOfRect(const LayerTransform& placed, int width, int height,
                                             const GridRect& rect) noexcept;

// Immutable worker-side inputs for repeating the existing LiftSelection/MoveLifted computation. The
// conservative byte bound includes the retained source, selection coverage and lifted raster graphs.
class BrushStrokeOpening
{
  public:
    BrushStrokeOpening(const BrushStrokeOpening&) noexcept = default;
    BrushStrokeOpening& operator=(const BrushStrokeOpening&) noexcept = default;
    BrushStrokeOpening(BrushStrokeOpening&&) noexcept = default;
    BrushStrokeOpening& operator=(BrushStrokeOpening&&) noexcept = default;

    [[nodiscard]] std::uint64_t RetirementMemoryBytes() const noexcept
    {
        return _retirementMemoryBytes;
    }

  private:
    friend class BrushStroke;
    BrushStrokeOpening() noexcept = default;

    Raster _source;
    Raster _coverage;
    Raster _lifted;
    GridRect _sourceRect;
    GridRect _liftedRect;
    Affine2 _pixelToDocument;
    LayerTransform _sourceTransform;
    LayerTransform _paintTransform;
    int _width = 0;
    int _height = 0;
    int _canvasWidth = 0;
    int _canvasHeight = 0;
    std::uint64_t _retirementMemoryBytes = 0;
};

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
        std::uint64_t ownershipBytes = 0; // producer-cached buffers and immutable raster owners
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
    // `kind` and `healingMode` are the stroke's own kind and (for Heal) its algorithm; Erasing() and Healing() are
    // derived from them, never stored redundantly.
    BrushStroke(const ImageLayer& layer, bool mask, const BrushSettings& settings, int canvasWidth, int canvasHeight,
                BrushCoverageEngine* engine, BrushKind kind = BrushKind::Paint,
                SpotHealingMode healingMode = SpotHealingMode::ContentAware);
    ~BrushStroke();
    // Owner-only, once publication has discarded this stroke. Detach the borrowed engine before CPU state
    // transfers to a worker; later destruction cannot mutate a graphics engine. No further stroke calls follow.
    void ReleaseCoverageEngine() noexcept;
    // Owner-only exclusive transfer. Unlike discard, this does NOT end the borrowed engine:
    // the final job must retain its GPU density until every final coverage copy is recorded,
    // then call EndStroke on the owner. The detached CPU draft owns no graphics pointer.
    [[nodiscard]] BrushCoverageEngine* DetachCoverageForFinish() noexcept;

    struct FinalCoverage final
    {
        std::vector<BrushSegment> settled;
        std::vector<BrushKernelTile> tiles;
        bool needsGpu = false;
    };
    // Exclusive worker-only final curve/tile preparation; no borrowed engine calls.
    // Returned kernel tile pointers borrow this private draft through final coverage acknowledgement.
    [[nodiscard]] std::optional<FinalCoverage> PrepareFinalCoverage(CancelToken cancellation = {});
    // Worker copies exact structured uint values to the matching private coverage tile.
    // Cancellation may leave a private prefix written; failure discards the entire final draft.
    [[nodiscard]] bool InstallFinalCoverage(std::uint64_t key, std::span<const std::uint32_t> values);
    // Once every required coverage copy is acknowledged, compose and seal the private CPU tiles.
    [[nodiscard]] bool ComposeFinalCoverage(const FinalCoverage& final);
    // Exclusive worker after final composition. Failed/cancelled healing makes this draft permanently
    // unpublishable; no partially healed snapshot can escape. Allocations propagate to the job runner.
    [[nodiscard]] bool HealForFinish(std::uint32_t seed);

    // Owner-only harness observation; the engine remains borrowed.
    [[nodiscard]] bool HasCoverageEngineForTest() const noexcept
    {
        return _engine != nullptr;
    }
    BrushStroke(const BrushStroke&) = delete;
    BrushStroke& operator=(const BrushStroke&) = delete;

    [[nodiscard]] bool IsValid() const noexcept
    {
        return _valid;
    }
    // Conservative retained CPU storage bound for retirement/admission accounting; zero means unknown or overflow.
    [[nodiscard]] std::uint64_t RetirementMemoryBytes() const noexcept;
    void SetPixelLimit(std::int64_t pixels) noexcept
    {
        _pixelLimit = pixels;
    }
    // Private CPU-only Fill/Clear/Invert preparation, before any samples/tiles. It allocates no brush density
    // buffers, uses cancellable private reads and propagates allocation failure to the worker runner.
    [[nodiscard]] bool ConfigureWorkerRasterEdit(CancelToken cancellation,
                                                 void (*progress)(void*, std::size_t, std::size_t) noexcept = nullptr,
                                                 void* progressContext = nullptr) noexcept;
    [[nodiscard]] bool PixelLimitRefused() const noexcept
    {
        return _pixelLimitRefused;
    }
    void EndWorkerPreparation() noexcept
    {
        _workProgress = nullptr;
        _workProgressContext = nullptr;
        _workCancellation = {};
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
    // The stroke committed: a sparse raster sharing the layer's pixels, the eager semantic mask thumbnail (an
    // image thumbnail may be empty), the transform placing it and
    // the grid bounds it covers (painting only adds alpha, so bounds come from the touched tiles).
    [[nodiscard]] std::optional<Commit> PaintSnapshot() const;
    [[nodiscard]] LayerTransform TransformFor(const GridRect& bounds) const noexcept;

    // Clone Stamp, Blur and warp results paint from `sample` (document-size premultiplied BGRA, or Gray8 on a mask)
    // read at the grid pixel's document position shifted by the offset, through the coverage: over the pixels under
    // the tip, or replacing them (`replaces`).
    void SetSample(Raster sample, int offsetX, int offsetY, bool replaces);
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
    // Optional emptiness classification distinguishes a valid no-pixel selection from failed preparation.
    [[nodiscard]] bool LiftSelection(bool* noPixels = nullptr);
    [[nodiscard]] std::optional<BrushStrokeOpening> CaptureLiftedOpening() const noexcept;
    [[nodiscard]] bool UseLiftedOpening(const BrushStrokeOpening& opening) noexcept;
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
    // Erase: the stroke clears the layer's pixels instead of painting color on them; never true on a mask (an
    // erase brush there paints black or white like Paint, per the mask palette).
    [[nodiscard]] bool Erasing() const noexcept
    {
        return _kind == BrushKind::Erase && !_mask;
    }
    // Spot Healing: the stroke uses nearby source pixels instead of the foreground color once it ends (Heal()).
    [[nodiscard]] bool Healing() const noexcept
    {
        return _kind == BrushKind::Heal;
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
    [[nodiscard]] bool AllocateTile(std::uint64_t key, bool sourceOnly = false);
    // The tile's image from its base, coverage and the settings; false (the stroke fails) when the base cannot be read.
    [[nodiscard]] bool ComposeTile(Tile& tile);
    void RefreshTileOwnership(Tile& tile) noexcept;
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
    // Borrowed from the application; this stroke never owns or destroys the engine pointer. Its destructor only
    // calls the engine's EndStroke lifetime hook when the stroke was begun.
    BrushCoverageEngine* _engine = nullptr;
    BrushKind _kind = BrushKind::Paint;
    SpotHealingMode _healingMode = SpotHealingMode::ContentAware;
    bool _valid = false;
    bool _failed = false;
    bool _workerRasterEdit = false;
    bool _detachedFinish = false;
    bool _finalCoveragePrepared = false;
    bool _usesCpuPermanent = true;
    bool _finalCoverageComplete = false;
    std::size_t _finalCoverageRemaining = 0;
    std::vector<std::uint64_t> _finalCoverageOrder;
    std::vector<std::uint8_t> _finalCoverageReceived;
    bool _pixelLimitRefused = false;
    CancelToken _workCancellation;
    void (*_workProgress)(void*, std::size_t, std::size_t) noexcept = nullptr;
    void* _workProgressContext = nullptr;
    std::int64_t _pixelLimit = 100'000'000;
    std::optional<Raster> _selection;
    std::optional<GridRect> _allocated;
    std::vector<Vec2> _samples;
    std::unordered_set<std::uint64_t> _tailKeys;
    std::vector<Tile> _tiles;
    std::uint64_t _tileOwnershipBytes = 0;
    bool _tileOwnershipValid = true;
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
    // The pixel move last rendered: its offset and mode, and the tiles its lifted pixels landed on.
    struct MoveState
    {
        Vec2 offset;
        bool duplicate = false;
    };
    std::optional<MoveState> _moved;
    std::vector<std::uint64_t> _landingTiles;
    // Allocates every tile `rect` (grid pixels) touches and appends their keys; false when one cannot be allocated.
    [[nodiscard]] bool AllocateTilesIn(const GridRect& rect, std::vector<std::uint64_t>& keys);
};
