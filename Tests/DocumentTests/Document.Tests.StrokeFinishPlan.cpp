#include "Document.Tests.StrokeFinishPlan.h"

#include "BrushStroke.h"
#include "Document.h"
#include "Executor.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <optional>
#include <stop_token>
#include <thread>
#include <vector>

namespace
{
int gFailures = 0;

void Expect(bool condition, const char* name) noexcept
{
    std::printf("%s stroke-finish-plan.%s\n", condition ? "PASS" : "FAIL", name);
    gFailures += condition ? 0 : 1;
}

void ExpectGeometry(bool condition, const char* placement, std::size_t accepted, const char* check) noexcept
{
    std::printf("%s stroke-finish-plan.geometry.%s.%zu.%s\n", condition ? "PASS" : "FAIL", placement, accepted, check);
    gFailures += condition ? 0 : 1;
}

constexpr std::uint64_t kBgra400MCacheCharge = 402'653'184u + 64u;
constexpr std::uint64_t kGray100MCacheCharge = 100'663'296u + 64u;

Raster SolidSource(int width = 24, int height = 24)
{
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u);
    for (std::size_t i = 0; i < bytes.size(); i += 4)
    {
        bytes[i] = 31;
        bytes[i + 1] = 47;
        bytes[i + 2] = 59;
        bytes[i + 3] = 255;
    }
    return Raster::Make(width, height, RasterFormat::Bgra8888, bytes.data(), bytes.size());
}

ImageLayer BlankLayer(const wchar_t* name, int width, int height)
{
    return ImageLayer::Blank(name, Vec2{static_cast<double>(width), static_cast<double>(height)});
}

ImageLayer SourceLayer(const Raster& source)
{
    ImageLayer layer = BlankLayer(L"finish plan source", source.Width(), source.Height());
    layer.asset = RasterAsset{source, {}, layer.name};
    return layer;
}

[[nodiscard]] std::uint64_t HashSmallRaster(const Raster& raster) noexcept
{
    if (!raster.HasStorage() || raster.Width() != 24 || raster.Height() != 24 ||
        raster.Format() != RasterFormat::Bgra8888)
        return 0;
    std::array<std::uint8_t, 24u * 24u * 4u> bytes{};
    raster.ReadRegion(0, 0, 24, 24, bytes.data(), 24 * 4, 0);
    std::uint64_t hash = 14695981039346656037ull;
    for (const std::uint8_t byte : bytes)
    {
        hash ^= byte;
        hash *= 1099511628211ull;
    }
    return hash;
}

[[nodiscard]] bool Contains(const GridRect& outer, int x, int y, int width, int height) noexcept
{
    const auto right = static_cast<std::int64_t>(outer.x) + outer.width;
    const auto bottom = static_cast<std::int64_t>(outer.y) + outer.height;
    return !outer.Empty() && width > 0 && height > 0 && x >= outer.x && y >= outer.y &&
           static_cast<std::int64_t>(x) + width <= right && static_cast<std::int64_t>(y) + height <= bottom;
}

struct PlacementCase final
{
    const char* name;
    Vec2 origin;
    Vec2 size;
    double rotation;
    bool flipX;
    bool flipY;
};

constexpr std::array<PlacementCase, 4> kPlacements{{
    {"translated", Vec2{5, 7}, Vec2{24, 24}, 0, false, false},
    {"rotated", Vec2{3, 4}, Vec2{26, 20}, 0.43, false, false},
    {"flipped", Vec2{4, 5}, Vec2{24, 24}, -0.19, true, true},
    {"anisotropic", Vec2{2, 8}, Vec2{34, 17}, 0.27, true, false},
}};

void GeometryPlanAndDetachedCpuCoverage()
{
    constexpr std::array<Vec2, 4> points{{Vec2{9, 9}, Vec2{15, 12}, Vec2{21, 16}, Vec2{26, 20}}};
    for (const PlacementCase& placement : kPlacements)
    {
        for (std::size_t accepted = 1; accepted <= points.size(); ++accepted)
        {
            ImageLayer layer = BlankLayer(L"plan geometry", 24, 24);
            layer.transform.origin = placement.origin;
            layer.transform.size = placement.size;
            layer.transform.rotation = placement.rotation;
            layer.transform.flipX = placement.flipX;
            layer.transform.flipY = placement.flipY;
            const BrushSettings settings{2, 1, 0.2, 0, 0, 1};
            BrushStroke stroke(layer, false, settings, 48, 48, &CpuBrushCoverage::Shared());
            bool appended = stroke.IsValid();
            for (std::size_t i = 0; i < accepted && appended; ++i)
                appended = stroke.Append(points[i]);
            const std::optional<BrushStroke::FinishMemoryPlan> plan = stroke.PlanFinishMemory();
            const char* prefix = placement.name;
            // Append deposits the first point as a settled dab. With one point
            // there is no final spline tail to rasterize; the snapshot still
            // contains that already painted dab.
            const bool planned =
                appended && plan.has_value() && !plan.value().commitBounds.Empty() &&
                (accepted == 1 ? plan.value().maximumChangedTiles == 0 : plan.value().maximumChangedTiles > 0) &&
                plan.value().additionalCpuBytes > 0;
            std::printf("%s stroke-finish-plan.geometry.%s.%zu.plan-before-detach\n", planned ? "PASS" : "FAIL", prefix,
                        accepted);
            gFailures += planned ? 0 : 1;
            if (!planned)
                continue;

            const auto plannedBounds = plan.value().commitBounds;
            const auto maximumChanged = plan.value().maximumChangedTiles;
            const BrushCoverageEngine* const detached = stroke.DetachCoverageForFinish();
            const bool detachedCpu = detached == &CpuBrushCoverage::Shared() && !stroke.HasCoverageEngineForTest();
            std::printf("%s stroke-finish-plan.geometry.%s.%zu.detached-cpu-engine\n", detachedCpu ? "PASS" : "FAIL",
                        prefix, accepted);
            gFailures += detachedCpu ? 0 : 1;
            if (!detachedCpu)
                continue;

            std::optional<BrushStroke::FinalCoverage> final;
            bool composed = false;
            std::optional<BrushStroke::Commit> snapshot;
            std::thread worker(
                [&]
                {
                    final = stroke.PrepareFinalCoverage();
                    if (final.has_value() && !final->needsGpu)
                    {
                        composed = stroke.ComposeFinalCoverage(final.value());
                        if (composed)
                            snapshot = stroke.PaintSnapshot();
                    }
                });
            worker.join();
            const bool finalCpu =
                final.has_value() && !final->needsGpu &&
                (accepted == 1 ? final->tiles.empty() && final->settled.empty() : !final->tiles.empty());
            std::printf("%s stroke-finish-plan.geometry.%s.%zu.final-cpu-coverage\n", finalCpu ? "PASS" : "FAIL",
                        prefix, accepted);
            gFailures += finalCpu ? 0 : 1;
            if (!finalCpu)
                continue;
            bool rectanglesFit = true;
            for (const BrushKernelTile& tile : final->tiles)
                rectanglesFit = rectanglesFit && Contains(plannedBounds, tile.x, tile.y, tile.width, tile.height);
            ExpectGeometry(rectanglesFit, prefix, accepted, "final-rectangles-within-plan");
            ExpectGeometry(final->tiles.size() <= maximumChanged, prefix, accepted, "final-count-within-plan");
            ExpectGeometry(composed && snapshot.has_value(), prefix, accepted, "cpu-compose-publishes-snapshot");
        }
    }
}

void SplineBendOutsideEndpointChord()
{
    ImageLayer layer = BlankLayer(L"finish spline hull", 40, 40);
    BrushStroke stroke(layer, false, BrushSettings{2, 1, 0, 0, 0, 1}, 40, 40, &CpuBrushCoverage::Shared());
    const bool appended = stroke.Append(Vec2{22, 8}) && stroke.Append(Vec2{22, 24}) && stroke.Append(Vec2{8, 24}) &&
                          stroke.Append(Vec2{8, 8});
    const auto plan = stroke.PlanFinishMemory();
    Expect(appended && plan.has_value(), "spline-bend.plan-exists-before-detach");
    if (!appended || !plan)
        return;
    const GridRect plannedBounds = plan.value().commitBounds;
    const std::uint64_t maximumChanged = plan.value().maximumChangedTiles;
    Expect(stroke.DetachCoverageForFinish() == &CpuBrushCoverage::Shared(), "spline-bend.detach-cpu-engine");

    std::optional<BrushStroke::FinalCoverage> final;
    bool composed = false;
    std::optional<BrushStroke::Commit> snapshot;
    std::thread worker(
        [&]
        {
            final = stroke.PrepareFinalCoverage();
            if (final.has_value() && !final->needsGpu)
            {
                composed = stroke.ComposeFinalCoverage(final.value());
                if (composed)
                    snapshot = stroke.PaintSnapshot();
            }
        });
    worker.join();
    Expect(final.has_value() && !final->needsGpu && !final->settled.empty(), "spline-bend.final-cpu-curve-prepared");
    if (!final || final->settled.empty())
        return;
    bool outsideEndpointChordBox = false;
    for (const BrushSegment& segment : final->settled)
    {
        outsideEndpointChordBox = outsideEndpointChordBox || segment.from.x < 8.0 || segment.to.x < 8.0;
    }
    Expect(outsideEndpointChordBox, "spline-bend.literal-final-curve-exceeds-endpoint-chord-box");
    bool rectanglesFit = true;
    for (const BrushKernelTile& tile : final->tiles)
        rectanglesFit = rectanglesFit && Contains(plannedBounds, tile.x, tile.y, tile.width, tile.height);
    Expect(rectanglesFit && final->tiles.size() <= maximumChanged,
           "spline-bend.final-tile-rectangles-and-count-fit-plan");
    Expect(composed && snapshot.has_value(), "spline-bend.cpu-compose-publishes-snapshot");
}

void MultiTileFinalHullAddsPreviouslyAbsentTile()
{
    ImageLayer layer = BlankLayer(L"finish multi-tile spline hull", 1024, 1024);
    BrushStroke stroke(layer, false, BrushSettings{2, 1, 0, 0, 0, 1}, 1024, 1024, &CpuBrushCoverage::Shared());
    const bool appended =
        stroke.Append(Vec2{600, 800}) && stroke.Append(Vec2{270, 800}) && stroke.Append(Vec2{270, 400});
    const auto plan = stroke.PlanFinishMemory();
    Expect(appended && plan.has_value(), "multi-tile.plan-exists-before-detach");
    if (!appended || !plan)
        return;

    const GridRect plannedBounds = plan.value().commitBounds;
    const std::uint64_t maximumChanged = plan.value().maximumChangedTiles;
    std::vector<std::uint64_t> provisionalKeys;
    bool provisionalHasLeftTile = false;
    for (const BrushStroke::Tile& tile : stroke.Tiles())
    {
        provisionalKeys.push_back(tile.key);
        provisionalHasLeftTile = provisionalHasLeftTile || tile.rect.x < BrushStroke::kTileSize;
    }
    Expect(!provisionalHasLeftTile, "multi-tile.provisional-and-settled-prefix-have-no-column-zero");
    Expect(stroke.DetachCoverageForFinish() == &CpuBrushCoverage::Shared(), "multi-tile.detach-cpu-engine");

    std::optional<BrushStroke::FinalCoverage> final;
    bool composed = false;
    std::optional<BrushStroke::Commit> snapshot;
    std::thread worker(
        [&]
        {
            final = stroke.PrepareFinalCoverage();
            if (final.has_value() && !final->needsGpu)
            {
                composed = stroke.ComposeFinalCoverage(final.value());
                if (composed)
                    snapshot = stroke.PaintSnapshot();
            }
        });
    worker.join();
    Expect(final.has_value() && !final->needsGpu && !final->settled.empty(), "multi-tile.final-cpu-coverage-prepared");
    if (!final)
        return;

    bool novelLeftTile = false;
    bool rectanglesFit = true;
    bool everyFinalKeyWasProvisional = true;
    for (const BrushKernelTile& tile : final->tiles)
    {
        rectanglesFit = rectanglesFit && Contains(plannedBounds, tile.x, tile.y, tile.width, tile.height);
        const bool existed =
            std::find(provisionalKeys.begin(), provisionalKeys.end(), tile.key) != provisionalKeys.end();
        everyFinalKeyWasProvisional = everyFinalKeyWasProvisional && existed;
        if (tile.x < BrushStroke::kTileSize && !existed)
            novelLeftTile = true;
    }
    Expect(novelLeftTile, "multi-tile.final-curve-adds-novel-column-zero-tile");
    Expect(!everyFinalKeyWasProvisional, "multi-tile.final-coverage-key-set-exceeds-provisional-set");
    Expect(rectanglesFit && final->tiles.size() <= maximumChanged, "multi-tile.plan-bound-and-count-cover-final-tiles");
    Expect(composed && snapshot.has_value(), "multi-tile.cpu-compose-publishes-snapshot");
}

void CancellationAndPixelLimitRefusalPreserveSource()
{
    const Raster source = SolidSource();
    const std::uint64_t beforeHash = HashSmallRaster(source);
    ImageLayer layer = SourceLayer(source);
    BrushStroke cancelled(layer, false, BrushSettings{2, 1, 0.3, 0, 0, 1}, 32, 32, &CpuBrushCoverage::Shared());
    const bool appended = cancelled.Append(Vec2{10, 10}) && cancelled.Append(Vec2{14, 12});
    const auto plan = cancelled.PlanFinishMemory();
    Expect(appended && plan.has_value(), "cancel.plan-admitted-before-transfer");
    Expect(cancelled.DetachCoverageForFinish() == &CpuBrushCoverage::Shared(), "cancel.detach-cpu-engine");
    CancelSource stop;
    const bool requested = stop.Cancel();
    std::optional<BrushStroke::FinalCoverage> final;
    std::optional<BrushStroke::Commit> snapshot;
    std::thread worker(
        [&]
        {
            final = cancelled.PrepareFinalCoverage(stop.Token());
            snapshot = cancelled.PaintSnapshot();
        });
    worker.join();
    Expect(requested && !final.has_value() && !snapshot.has_value(), "cancel.cancelled-prepare-and-snapshot-refused");
    Expect(HashSmallRaster(source) == beforeHash && source.Identity() == layer.asset->image.Identity(),
           "cancel.source-public-content-hash-and-identity-unchanged");

    ImageLayer refusedLayer = SourceLayer(source);
    BrushStroke refused(refusedLayer, false, BrushSettings{2, 1, 0.3, 0, 0, 1}, 32, 32, &CpuBrushCoverage::Shared());
    refused.SetPixelLimit(1);
    const bool accepted = refused.Append(Vec2{10, 10});
    const auto refusedPlan = refused.PlanFinishMemory();
    Expect(!accepted && refused.PixelLimitRefused() && !refusedPlan.has_value(),
           "refusal.projected-pixel-limit-refuses-before-tile-admission");
    Expect(HashSmallRaster(source) == beforeHash && source.Identity() == refusedLayer.asset->image.Identity(),
           "refusal.source-public-content-hash-and-identity-unchanged");
}

void InvalidDimensionsRefuseBeforeRounding()
{
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double infinity = std::numeric_limits<double>::infinity();
    const std::array<double, 4> invalidWidths{nan, infinity, 30'001.0, 1.0e100};
    for (std::size_t i = 0; i < invalidWidths.size(); ++i)
    {
        ImageLayer layer = BlankLayer(L"invalid finish dimension", 1, 1);
        layer.transform.size.x = invalidWidths[i];
        BrushStroke stroke(layer, false, BrushSettings{2, 1, 0, 0, 0, 1}, 32, 32, &CpuBrushCoverage::Shared());
        const bool rejected = !stroke.IsValid() && !stroke.PlanFinishMemory().has_value();
        std::printf("%s stroke-finish-plan.invalid-dimension.case-%zu-refused-before-unsafe-rounding\n",
                    rejected ? "PASS" : "FAIL", i);
        gFailures += rejected ? 0 : 1;
    }
}

void TinyValidTransformRefusesLargeInverseGeometry()
{
    // A valid one-unit placement of a 1000-pixel source gives a large inverse
    // transform without violating LayerTransform's minimum placement size.
    const Raster source = Raster::MakeSparse(1000, 1000, RasterFormat::Bgra8888, Raster{}, 0, 0, {}, 0);
    ImageLayer layer = BlankLayer(L"tiny transform", 1, 1);
    layer.asset = RasterAsset{source, {}, layer.name};
    layer.transform.size = Vec2{1, 1};
    const BrushSettings settings{2000, 1, 0, 0, 0, 1};
    BrushStroke stroke(layer, false, settings, 10'000, 10'000, &CpuBrushCoverage::Shared());
    const bool constructorBounded = layer.transform.IsValid() && stroke.IsValid();
    const bool appended = constructorBounded && stroke.Append(Vec2{5000, 5000});
    Expect(constructorBounded, "tiny-inverse.transform-itself-is-finite-and-valid");
    Expect(!appended && stroke.PixelLimitRefused(), "tiny-inverse.geometry-refused-before-expanded-key-work");
    Expect(stroke.Tiles().empty(), "tiny-inverse.refusal-allocates-no-tile");
}

void LargeSparseGridSizeClassCharges()
{
    constexpr int side = 10'000;
    const Raster bgra = Raster::MakeSparse(side, side, RasterFormat::Bgra8888, Raster{}, 0, 0, {}, 0);
    ImageLayer imageLayer = BlankLayer(L"400M sparse BGRA", side, side);
    imageLayer.asset = RasterAsset{bgra, {}, imageLayer.name};
    BrushStroke imageStroke(imageLayer, false, BrushSettings{2, 1, 0, 0, 0, 1}, side, side,
                            &CpuBrushCoverage::Shared());
    const auto bgraPlan = imageStroke.PlanFinishMemory();
    Expect(bgra.HasStorage() && bgra.IsSparse() && !bgra.IsMaterialized(), "size-class.bgra-grid-valid-empty-sparse");
    Expect(bgraPlan.has_value() && bgraPlan.value().commitBounds == GridRect{0, 0, side, side},
           "size-class.bgra-plan-keeps-literal-100m-pixel-bounds");
    Expect(bgraPlan.has_value() && bgraPlan.value().additionalCpuBytes >= kBgra400MCacheCharge,
           "size-class.bgra-plan-covers-literal-402653184-plus-64-charge");

    const Raster gray = Raster::MakeSparse(side, side, RasterFormat::Gray8, Raster{}, 0, 0, {}, 0);
    ImageLayer maskLayer = BlankLayer(L"100M sparse Gray8", side, side);
    LayerMask mask;
    mask.asset.image = gray;
    maskLayer.mask = mask;
    BrushStroke maskStroke(maskLayer, true, BrushSettings{2, 1, 0, 0, 0, 1}, side, side, &CpuBrushCoverage::Shared());
    const auto grayPlan = maskStroke.PlanFinishMemory();
    Expect(gray.HasStorage() && gray.IsSparse() && !gray.IsMaterialized(), "size-class.gray-grid-valid-empty-sparse");
    Expect(grayPlan.has_value() && grayPlan.value().commitBounds == GridRect{0, 0, side, side},
           "size-class.gray-plan-keeps-literal-100m-pixel-bounds");
    Expect(grayPlan.has_value() && grayPlan.value().additionalCpuBytes >= kGray100MCacheCharge,
           "size-class.gray-plan-covers-literal-100663296-plus-64-charge");
}
} // namespace

int RunStrokeFinishPlanTests()
{
    gFailures = 0;
    GeometryPlanAndDetachedCpuCoverage();
    SplineBendOutsideEndpointChord();
    MultiTileFinalHullAddsPreviouslyAbsentTile();
    CancellationAndPixelLimitRefusalPreserveSource();
    InvalidDimensionsRefuseBeforeRounding();
    TinyValidTransformRefusesLargeInverseGeometry();
    LargeSparseGridSizeClassCharges();
    return gFailures;
}
