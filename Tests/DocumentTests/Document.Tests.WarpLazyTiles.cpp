#include "Document.Tests.WarpLazyTiles.h"

#include "RasterWork.h"
#include "Selection.h"
#include "WarpStroke.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <stop_token>
#include <thread>
#include <vector>

namespace
{
int gFailures = 0;
int gChecks = 0;
void Expect(bool condition, const char* name) noexcept
{
    std::printf("%s warp-lazy-tiles.%s\n", condition ? "PASS" : "FAIL", name);
    ++gChecks;
    gFailures += condition ? 0 : 1;
}

LayerTransform Placement(int width, int height)
{
    LayerTransform result;
    result.size = {static_cast<double>(width), static_cast<double>(height)};
    result.sampling = LayerSampling::Nearest;
    return result;
}

Raster Uniform(int width, int height)
{
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(width) * height * 4, 64);
    return Raster::Make(width, height, RasterFormat::Bgra8888, bytes.data(), bytes.size());
}

bool UniformTile(const RasterTile& tile)
{
    const auto* bytes = tile.pixels.Data();
    return bytes && std::all_of(bytes, bytes + tile.pixels.ByteSize(), [](std::uint8_t value) { return value == 64; });
}

void CompleteSnapshots()
{
    const auto source = Uniform(300, 300);
    const BrushSettings settings{2, 1, 0, 0, 0, 1};
    WarpStroke stroke(WarpSourceView{source, Placement(300, 300), 300, 300}, SmearMode::Liquify, settings);
    const auto initial = stroke.RetirementMemoryBytes();
    Expect(stroke.IsValid() && initial > source.RetirementMemoryBytes() &&
               initial - source.RetirementMemoryBytes() < 64 * 1024,
           "construction-retains-source-and-small-directory-without-full-mutable-copy");
    Expect(stroke.AppendForWork({10, 10}, {}) && stroke.Points().empty() && stroke.AllTouchedTilesForWork({}).empty(),
           "first-liquify-origin-does-not-record-a-dab-or-touched-tile");
    Expect(stroke.AppendForWork({11, 10}, {}), "first-serial-dab-accepted");
    auto first = stroke.AllTouchedTilesForWork({});
    Expect(first.size() == 1 && first[0].x == 0 && first[0].y == 0 && first[0].pixels.Width() == 256 &&
               first[0].pixels.Height() == 256 && UniformTile(first[0]),
           "first-complete-snapshot-is-one-literal-uniform-256-square");
    auto unchanged = stroke.AllTouchedTilesForWork({});
    Expect(unchanged.size() == 1 && !first.empty() && unchanged[0].pixels == first[0].pixels,
           "unchanged-complete-snapshot-shares-immutable-version");
    Expect(stroke.TakeChangedTilesForWork({}).empty(), "successful-complete-publication-consumes-changed-set");
    Expect(stroke.AppendForWork({258, 10}, {}), "ordered-crossing-dabs-accepted");
    auto crossing = stroke.AllTouchedTilesForWork({});
    Expect(crossing.size() == 2 && crossing[0].x == 0 && crossing[1].x == 256 && crossing[1].y == 0 &&
               crossing[1].pixels.Width() == 44 && crossing[1].pixels.Height() == 256 && UniformTile(crossing[0]) &&
               UniformTile(crossing[1]),
           "crossing-complete-snapshot-includes-original-and-clipped-edge-tile");
    Expect(stroke.AppendForWork({259, 10}, {}), "later-edge-only-dab-accepted");
    auto later = stroke.AllTouchedTilesForWork({});
    Expect(later.size() == 2 && crossing.size() == 2 && later[0].pixels == crossing[0].pixels &&
               !(later[1].pixels == crossing[1].pixels) && UniformTile(later[0]) && UniformTile(later[1]) &&
               UniformTile(crossing[1]),
           "later-snapshot-reuses-untouched-version-and-preserves-previous-reader-bytes");
    Expect(stroke.AppendForWork({259, 258}, {}), "later-row-crossing-dabs-accepted");
    Expect(stroke.AppendForWork({10, 258}, {}), "final-horizontal-row-dabs-accepted");
    auto full = stroke.AllTouchedTilesForWork({});
    Expect(full.size() == 4 && full[0].x == 0 && full[0].y == 0 && full[1].x == 256 && full[1].y == 0 &&
               full[2].x == 0 && full[2].y == 256 && full[2].pixels.Width() == 256 && full[2].pixels.Height() == 44 &&
               full[3].x == 256 && full[3].y == 256 && full[3].pixels.Width() == 44 && full[3].pixels.Height() == 44 &&
               std::all_of(full.begin(), full.end(), UniformTile),
           "all-four-literal-grid-extents-survive-independent-preview-publication");
    Expect(stroke.TakeChangedTilesForWork({}).empty() && stroke.IsValid(),
           "complete-snapshot-clears-only-generation-dirt");
}

void LargeOpeningBounds()
{
    constexpr int side = 10000;
    constexpr std::array<std::uint8_t, 4> literal{12, 24, 48, 96};
    const auto one = Raster::Make(1, 1, RasterFormat::Bgra8888, literal.data(), literal.size());
    const auto source = Raster::MakeSparse(side, side, RasterFormat::Bgra8888, one, 0, 0, {}, 0);
    const BrushSettings settings{2, 1, 0, 0, 0, 1};
    WarpStroke stroke(WarpSourceView{source, Placement(side, side), side, side}, SmearMode::Smudge, settings);
    const auto before = stroke.RetirementMemoryBytes();
    Expect(stroke.IsValid() && source.IsSparse() && !source.IsMaterialized() &&
               before > source.RetirementMemoryBytes() && before - source.RetirementMemoryBytes() < 1024 * 1024,
           "100mp-construction-holds-sparse-source-with-less-than-one-mib-private-storage");
    Expect(stroke.AppendForWork({5000, 5000}, {}) && stroke.Points().empty() && !source.IsMaterialized() &&
               stroke.RetirementMemoryBytes() - before < 1024 * 1024,
           "100mp-first-smudge-pickup-reads-only-one-tip-tile-with-bounded-scratch");
    Expect(stroke.AppendForWork({5001, 5000}, {}) && !source.IsMaterialized(), "100mp-dab-does-not-assemble-source");
    auto snapshot = stroke.AllTouchedTilesForWork({});
    const auto* pixels = snapshot.empty() ? nullptr : snapshot[0].pixels.Data();
    Expect(snapshot.size() == 1 && snapshot[0].x == 4864 && snapshot[0].y == 4864 && pixels &&
               std::all_of(pixels, pixels + snapshot[0].pixels.ByteSize(), [](std::uint8_t v) { return v == 0; }) &&
               !source.IsMaterialized() && stroke.RetirementMemoryBytes() - before < 2 * 1024 * 1024,
           "100mp-complete-preview-is-one-literal-zero-tile-without-full-render");
    WarpStroke blank(WarpSourceView{{}, Placement(side, side), side, side}, SmearMode::Smudge, settings);
    const auto empty = blank.RetirementMemoryBytes();
    Expect(blank.IsValid() && empty < 1024 * 1024 && blank.AppendForWork({5000, 5000}, {}) &&
               blank.RetirementMemoryBytes() - empty < 1024 * 1024,
           "100mp-blank-opening-has-no-full-transparent-canvas-allocation");
}

struct CancelAt final
{
    std::stop_source& stop;
    WarpStroke::WorkStage stage;
    bool reached = false;
    static void Check(void* pointer, WarpStroke::WorkStage stage)
    {
        auto& self = *static_cast<CancelAt*>(pointer);
        if (stage == self.stage)
        {
            self.reached = true;
            static_cast<void>(self.stop.request_stop());
        }
    }
};

void CancellationAndParity()
{
    const auto source = Raster::MakeSparse(300, 300, RasterFormat::Bgra8888, Uniform(1, 1), 0, 0, {}, 64);
    const BrushSettings settings{2, 1, 0, 0, 0, 1};
    std::stop_source cancelled;
    CancelAt checkpoint{cancelled, WarpStroke::WorkStage::SourceRow};
    WarpStroke stroke(WarpSourceView{source, Placement(300, 300), 300, 300}, SmearMode::Smudge, settings);
    Expect(!stroke.AppendForWork({10, 10}, CancelToken{cancelled.get_token()}, {&CancelAt::Check, &checkpoint}) &&
               checkpoint.reached && !stroke.IsValid() && !stroke.Image().HasStorage() &&
               stroke.AllTouchedTilesForWork({}).empty() && !source.IsMaterialized(),
           "source-row-cancellation-invalidates-private-draft-and-publishes-no-pixels");
    WarpStroke partial(WarpSourceView{source, Placement(300, 300), 300, 300}, SmearMode::Liquify, settings);
    Expect(partial.AppendForWork({10, 10}, {}) && partial.AppendForWork({11, 10}, {}),
           "snapshot-cancellation-fixture-ready");
    std::stop_source tileStop;
    CancelAt tileCheckpoint{tileStop, WarpStroke::WorkStage::TileRow};
    Expect(partial.AllTouchedTilesForWork(CancelToken{tileStop.get_token()}, {&CancelAt::Check, &tileCheckpoint})
                   .empty() &&
               tileCheckpoint.reached && !partial.IsValid() && !partial.ImageForWork({}).HasStorage() &&
               !source.IsMaterialized(),
           "tile-row-cancellation-publishes-no-complete-snapshot-or-reusable-draft");
    // Additional parity against the unchanged existing placed-source and contiguous-dab reference. Literal
    // tests above remain independent of this comparison; no legacy numeric expectation is rebaselined.
    std::vector<std::uint8_t> ramp(16 * 12 * 4);
    for (std::size_t pixel = 0; pixel < ramp.size() / 4; ++pixel)
    {
        ramp[pixel * 4] = static_cast<std::uint8_t>(pixel % 96);
        ramp[pixel * 4 + 1] = 96;
        ramp[pixel * 4 + 2] = 128;
        ramp[pixel * 4 + 3] = 192;
    }
    const auto image = Raster::Make(16, 12, RasterFormat::Bgra8888, ramp.data(), ramp.size());
    auto placement = Placement(16, 12);
    placement.origin = {2.25, 1.5};
    placement.rotation = 17;
    placement.flipX = true;
    placement.sampling = LayerSampling::High;
    const auto rendered = RenderLayerPixels(image, placement, 24, 20);
    for (const auto mode : {SmearMode::Smudge, SmearMode::Liquify})
    {
        const BrushSettings tip{7, 0.65, 0, 0, 0, 0.75};
        WarpStroke reference(rendered, mode, tip);
        WarpStroke tiled(WarpSourceView{image, placement, 24, 20}, mode, tip);
        bool accepted = rendered.HasStorage();
        for (const Vec2 point : {Vec2{8, 8}, Vec2{10, 9}, Vec2{12, 11}, Vec2{6, 9}})
        {
            reference.Append(point);
            accepted = tiled.AppendForWork(point, {}) && accepted;
        }
        const auto old = reference.Image();
        const auto now = tiled.ImageForWork({});
        Expect(accepted && reference.IsValid() && tiled.IsValid() && reference.Points() == tiled.Points() &&
                   old.HasStorage() && now.HasStorage() && old.ByteSize() == now.ByteSize() &&
                   std::memcmp(old.Data(), now.Data(), old.ByteSize()) == 0,
               mode == SmearMode::Smudge ? "placed-smudge-preserves-existing-sampling-and-serial-dab-reference"
                                         : "placed-liquify-preserves-existing-sampling-and-serial-dab-reference");
    }
}
} // namespace

int RunWarpLazyTilesTests()
{
    gFailures = gChecks = 0;
    // Complete CPU fixture construction, sampling and destruction stays on a joined worker.
    std::thread worker(
        []
        {
            CompleteSnapshots();
            LargeOpeningBounds();
            CancellationAndParity();
        });
    worker.join();
    std::printf("SUMMARY warp-lazy-tiles checks=%d failures=%d\n", gChecks, gFailures);
    return gFailures;
}
