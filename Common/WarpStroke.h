#pragma once

#include "Document.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <thread>
#include <vector>

class SharedPublicationMemory;
class PlacedRasterReader;

struct WarpSourceView final
{
    Raster image; // immutable layer pixels, empty for a transparent blank layer
    LayerTransform placement;
    int width = 0; // document grid
    int height = 0;
};

struct WarpSampleMemoryPlan final
{
    Vec2 point;
    std::uint64_t sequence = 0;
    std::uint64_t maximumTiles = 0;
    std::uint64_t maximumChangedTiles = 0;
    std::uint64_t maximumPoints = 0;
    std::uint64_t pointCapacity = 0;
    std::uint64_t persistentCpuBytes = 0;
    std::uint64_t transientCpuBytes = 0;

  private:
    friend class WarpStroke;
    std::weak_ptr<WarpStroke> _producer; // scalar plan never permits freed-address reuse
    Vec2 _acceptedPoint;
};

// A Smudge or Liquify stroke in progress (C11, Specs/Document/Document_Tools.md), the legacy WarpStroke. It works on
// the active layer as the canvas shows it, at document size, changing it dab by dab; the host shows that working
// copy in place of the layer. When the stroke ends the session paints the result into the layer's own pixels along
// the stroke's path (`DocumentSession::FinishWarp`).
class WarpStroke
{
  public:
    static constexpr int kTileSize = 256;

    // `image` is the layer rendered at document size (premultiplied BGRA); `settings` give the tip (diameter at
    // least 2, hardness at most 0.98) and the strength (the opacity, 0.01–1).
    // Copies into private working storage without assembling any shared sparse source. An incomplete or
    // cancelled read leaves the stroke invalid; a worker caller supplies its operation/shutdown token.
    WarpStroke(const Raster& image, SmearMode mode, const BrushSettings& settings, CancelToken cancellation = {});
    // Exclusive worker construction. Only tile metadata is allocated initially; displayed source pixels and
    // writable 256-square tiles are read on first access. No document-sized render or mutable pixel allocation.
    WarpStroke(WarpSourceView source, SmearMode mode, const BrushSettings& settings, CancelToken cancellation = {});
    ~WarpStroke();

    // Exclusive admitted worker factory. The original job owns complete opening/live/snapshot peaks.
    // The preallocated release node retires the entire mutable CPU draft off owner, even below 16 MiB.
    // Its original accounting owner survives queued graph destruction and the last weak control block.
    // No graphics ownership or document mutation. A bound immutable accounting owner cannot be reused.
    [[nodiscard]] static std::shared_ptr<WarpStroke> MakeForWork(const Raster& image, SmearMode mode,
                                                                 const BrushSettings& settings,
                                                                 std::thread::id ownerThread, CancelToken cancellation,
                                                                 std::shared_ptr<const SharedPublicationMemory> memory);
    [[nodiscard]] static std::shared_ptr<WarpStroke> MakePlacedForWork(
        WarpSourceView source, SmearMode mode, const BrushSettings& settings, std::thread::id ownerThread,
        CancelToken cancellation, std::shared_ptr<const SharedPublicationMemory> memory);

    [[nodiscard]] bool IsValid() const noexcept
    {
        return _valid;
    }
    // Retained CPU storage bound for retirement/admission accounting; zero means arithmetic overflow.
    [[nodiscard]] std::uint64_t RetirementMemoryBytes() const noexcept;
    // Continues the stroke to `point` (document pixels), dabbing along the way.
    void Append(Vec2 point);
    // Worker mutation is exclusive. Any cancellation/refusal invalidates the private draft, so neither
    // Image nor changed tiles can publish partially changed pixels. A caller admits all construction first.
    enum class WorkStage : std::uint8_t
    {
        Sample,
        SourceRow,
        PickUpRow,
        SmudgeRow,
        ScratchRow,
        PushRow,
        ImageRow,
        TileRow,
        Complete
    };
    struct WorkObserver
    {
        void (*checkpoint)(void*, WorkStage) = nullptr;
        void* context = nullptr;
    };
    [[nodiscard]] bool AppendForWork(Vec2 point, CancelToken cancellation, WorkObserver observer = {});
    using SampleMemoryPlan = WarpSampleMemoryPlan;
    // Known factory owner only, before exclusive transfer. O(1) geometry/capacity/cached-byte arithmetic,
    // no tile traversal, retained source capture, scratch or allocation. Caller grows the original complete
    // opening/processing reservation before accepting the point and capturing its private input.
    [[nodiscard]] std::optional<SampleMemoryPlan> PlanSampleMemory(Vec2 point) const noexcept;
    [[nodiscard]] bool IsSamplePlanCurrentForOwner(const SampleMemoryPlan& plan) const noexcept;
    [[nodiscard]] bool AppendPlannedForWork(const SampleMemoryPlan& plan, CancelToken cancellation,
                                            WorkObserver observer = {});
    [[nodiscard]] std::thread::id WorkOwner() const noexcept
    {
        return _workOwner;
    }
    // Every dab's center, for painting the result into the layer.
    [[nodiscard]] const std::vector<Vec2>& Points() const noexcept
    {
        return _points;
    }
    // The working copy as a raster (document size); a copy of the pixels.
    [[nodiscard]] Raster Image() const;
    // Exclusive worker snapshots: one adopted allocation per image/tile; every row is initialized before
    // output publication. Failure/cancellation invalidates the private draft and returns no partial output.
    [[nodiscard]] Raster ImageForWork(CancelToken cancellation, WorkObserver observer = {});
    // Tiles of the working copy (256² of the document grid) whose pixels changed since the last call, for the live
    // preview.
    [[nodiscard]] std::vector<RasterTile> TakeChangedTiles();
    [[nodiscard]] std::vector<RasterTile> TakeChangedTilesForWork(CancelToken cancellation, WorkObserver observer = {});
    // Complete immutable snapshot of every touched tile. Unchanged prior tile versions are shared; a recorder
    // can build from the original source without depending on a previously displayed preview. Caller admits
    // the old/new overlap and retires the complete vector and its original accounting owner off the UI thread.
    [[nodiscard]] std::vector<RasterTile> AllTouchedTilesForWork(CancelToken cancellation, WorkObserver observer = {});
    [[nodiscard]] SmearMode Mode() const noexcept
    {
        return _mode;
    }
    [[nodiscard]] double Diameter() const noexcept
    {
        return _diameter;
    }
    [[nodiscard]] int Width() const noexcept
    {
        return _width;
    }
    [[nodiscard]] int Height() const noexcept
    {
        return _height;
    }

  private:
    [[nodiscard]] int Radius() const noexcept;
    // How much a dab moves pixels at a distance `u` (0 center, 1 rim) from its center.
    [[nodiscard]] float Weight(float u) const noexcept;
    [[nodiscard]] bool PickUp(Vec2 center, CancelToken cancellation, WorkObserver observer);
    [[nodiscard]] bool Smudge(Vec2 center, CancelToken cancellation, WorkObserver observer);
    [[nodiscard]] bool Push(Vec2 from, Vec2 to, CancelToken cancellation, WorkObserver observer);
    [[nodiscard]] bool Checkpoint(WorkStage stage, CancelToken cancellation, WorkObserver observer) noexcept;
    void MarkDirty(int x0, int y0, int x1, int y1) noexcept;
    [[nodiscard]] bool EnsureRegion(int x0, int y0, int x1, int y1, CancelToken cancellation, WorkObserver observer);
    [[nodiscard]] std::uint8_t* PixelAt(int x, int y) noexcept;
    [[nodiscard]] bool CopyCurrentRow(int row, std::uint8_t* output, CancelToken cancellation) const noexcept;
    [[nodiscard]] std::vector<RasterTile> SnapshotTilesForWork(bool allTouched, CancelToken cancellation,
                                                               WorkObserver observer);
    [[nodiscard]] std::optional<SampleMemoryPlan> PlanSampleMemoryImpl(Vec2 point) const noexcept;
    [[nodiscard]] bool IsSamplePlanCurrent(const SampleMemoryPlan& plan) const noexcept;

    struct Tile final
    {
        PixelBuffer pixels;
        Raster published;
    };

    SmearMode _mode = SmearMode::Liquify;
    double _diameter = 2;
    double _hardness = 0;
    double _strength = 1;
    int _width = 0;
    int _height = 0;
    int _columns = 0;
    std::vector<std::uint8_t> _pixels;
    std::unique_ptr<PlacedRasterReader> _reader;
    std::vector<Tile> _tiles;
    std::uint64_t _tileBytes = 0; // cached mutable/published tile ownership; no owner directory walk
    std::weak_ptr<WarpStroke> _self;
    std::thread::id _workOwner;
    std::uint64_t _sampleSequence = 0;
    std::uint64_t _planningBase = 0;
    int _allocatedLeft = kMaxCanvasSide;
    int _allocatedTop = kMaxCanvasSide;
    int _allocatedRight = -1;
    int _allocatedBottom = -1;
    std::vector<Vec2> _points;
    std::optional<Vec2> _last;
    std::vector<float> _carried; // Smudge: the color the brush carries, a (2r+1)² BGRA square
    std::vector<float> _scratch;
    std::vector<std::uint8_t> _dirty; // bit 0 changed, bit 1 ever touched
    bool _valid = false;
};
