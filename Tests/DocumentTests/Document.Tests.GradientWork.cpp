#include "Document.Tests.GradientWork.h"

#include "BrushStroke.h"
#include "Document.h"
#include "RasterEditWork.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace
{
int gFailures = 0;

void Expect(bool condition, const char* name)
{
    std::printf("%s gradientwork.%s\n", condition ? "PASS" : "FAIL", name);
    gFailures += condition ? 0 : 1;
}

Raster Bgra(int width, int height, std::span<const std::uint8_t> bytes)
{
    return Raster::Make(width, height, RasterFormat::Bgra8888, bytes.data(), bytes.size());
}

Raster Gray(int width, int height, std::span<const std::uint8_t> bytes)
{
    return Raster::Make(width, height, RasterFormat::Gray8, bytes.data(), bytes.size());
}

ImageLayer BlankLayer(int width, int height)
{
    return ImageLayer::Blank(L"Gradient work", Vec2{static_cast<double>(width), static_cast<double>(height)});
}

ImageLayer ImageLayerOf(const Raster& image, int width, int height)
{
    ImageLayer layer = BlankLayer(width, height);
    layer.asset = RasterAsset{image, Raster::MakeThumbnail(image), layer.name};
    return layer;
}

DocumentSelection Rect(double x, double y, double width, double height)
{
    return DocumentSelection{
        false, SelectionLoops{{Vec2{x, y}, Vec2{x + width, y}, Vec2{x + width, y + height}, Vec2{x, y + height}}}};
}

RasterEditInput GradientInput(ImageLayer layer, int canvasWidth, int canvasHeight, Vec2 start, Vec2 end,
                              std::array<double, 4> color, std::array<double, 4> endColor, double opacity = 1.0,
                              GradientShape shape = GradientShape::Linear,
                              std::optional<DocumentSelection> selection = std::nullopt,
                              std::int64_t pixelLimit = kMaxLayerPixels, bool mask = false)
{
    RasterEditInput input;
    input.layer = std::move(layer);
    input.selection = std::move(selection);
    input.canvasWidth = canvasWidth;
    input.canvasHeight = canvasHeight;
    input.pixelLimit = pixelLimit;
    input.mask = mask;
    input.kind = RasterEditKind::Gradient;
    input.color = color;
    input.start = start;
    input.end = end;
    input.shape = shape;
    input.endColor = endColor;
    input.opacity = opacity;
    return input;
}

bool RasterEquals(const Raster& raster, RasterFormat format, int width, int height,
                  std::span<const std::uint8_t> expected)
{
    const int channels = format == RasterFormat::Gray8 ? 1 : 4;
    if (!raster.HasStorage() || raster.Format() != format || raster.Width() != width || raster.Height() != height ||
        expected.size() !=
            static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * static_cast<std::size_t>(channels))
        return false;
    std::vector<std::uint8_t> actual(expected.size());
    raster.ReadRegion(0, 0, width, height, actual.data(), width * channels, 0);
    return std::equal(expected.begin(), expected.end(), actual.begin());
}

bool LayerPixelsEqual(const RasterEditResult& result, RasterFormat format, int width, int height,
                      std::span<const std::uint8_t> expected)
{
    return result.status == RasterEditStatus::Ready && result.layer &&
           (format == RasterFormat::Gray8
                ? result.layer->mask && RasterEquals(result.layer->mask->asset.image, format, width, height, expected)
                : result.layer->asset && RasterEquals(result.layer->asset->image, format, width, height, expected));
}

bool PreviewPixelsEqual(const RasterEditPreviewResult& result, RasterFormat format, int width, int height,
                        std::span<const std::uint8_t> expected)
{
    if (result.status != RasterEditStatus::Ready || !result.stroke)
        return false;
    const std::optional<BrushStroke::Commit> snapshot = result.stroke->PaintSnapshot();
    return snapshot && RasterEquals(snapshot->image, format, width, height, expected);
}

bool PixelEquals(const Raster& raster, int x, int y, const std::array<std::uint8_t, 4>& expected)
{
    std::array<std::uint8_t, 4> actual{};
    raster.ReadRegion(x, y, 1, 1, actual.data(), 4, 0);
    return actual == expected;
}

bool Near(int value, int target, int tolerance)
{
    return std::abs(value - target) <= tolerance;
}

bool GrayPixel(const Raster& raster, int x, int y, std::uint8_t expected)
{
    std::uint8_t actual = 0;
    raster.ReadRegion(x, y, 1, 1, &actual, 1, 0);
    return actual == expected;
}

struct MidTileCancel final
{
    CancelSource* source = nullptr;
    std::size_t completed = 0;
    std::size_t total = 0;
};

void CancelAtFirstCompletedTile(void* context, std::size_t completed, std::size_t total) noexcept
{
    auto& state = *static_cast<MidTileCancel*>(context);
    if (completed == 1)
    {
        state.completed = completed;
        state.total = total;
        (void)state.source->Cancel();
    }
}

void LinearLiteralAndPreview()
{
    const ImageLayer layer = BlankLayer(4, 1);
    RasterEditInput input = GradientInput(layer, 4, 1, Vec2{0.5, 0.5}, Vec2{3.5, 0.5}, {0, 0, 0, 1}, {1, 1, 1, 1});
    constexpr std::array<std::uint8_t, 16> expected{0,   0,   0,   255, 85,  85,  85,  255,
                                                    170, 170, 170, 255, 255, 255, 255, 255};
    const RasterEditResult committed = RunRasterEdit(input);
    Expect(LayerPixelsEqual(committed, RasterFormat::Bgra8888, 4, 1, expected),
           "linear.pixel-centers-four-pixel-literal-bgra");
    const RasterEditPreviewResult preview = RunRasterEditPreview(input);
    Expect(PreviewPixelsEqual(preview, RasterFormat::Bgra8888, 4, 1, expected),
           "linear.preview-paint-snapshot-matches-independent-literal");

    // Legacy foregroundToBackgroundFillsCanvasAndCommitsOneUndo: Core Graphics allowed ±3.
    RasterEditInput legacy =
        GradientInput(BlankLayer(101, 4), 101, 4, Vec2{0.5, 2}, Vec2{100.5, 2}, {0, 0, 0, 1}, {1, 1, 1, 1});
    const RasterEditResult legacyResult = RunRasterEdit(legacy);
    Expect(legacyResult.status == RasterEditStatus::Ready && legacyResult.layer && legacyResult.layer->asset,
           "legacy.linear-fixture-prepared");
    if (legacyResult.layer && legacyResult.layer->asset)
    {
        const Raster& image = legacyResult.layer->asset->image;
        Expect(PixelEquals(image, 0, 0, {0, 0, 0, 255}) && PixelEquals(image, 100, 3, {255, 255, 255, 255}),
               "legacy.linear-endpoints-within-three");
        std::array<std::uint8_t, 4> middle{};
        image.ReadRegion(50, 1, 1, 1, middle.data(), 4, 0);
        Expect(Near(middle[0], 128, 3) && middle[0] == middle[1] && middle[1] == middle[2] && middle[3] == 255,
               "legacy.linear-midpoint-within-three");
    }
}

void RadialLiteralAndLegacy()
{
    RasterEditInput input = GradientInput(BlankLayer(3, 3), 3, 3, Vec2{1.5, 1.5}, Vec2{3.5, 1.5}, {0, 0, 0, 1},
                                          {1, 1, 1, 1}, 1.0, GradientShape::Radial);
    constexpr std::array<std::uint8_t, 36> expected{180, 180, 180, 255, 128, 128, 128, 255, 180, 180, 180, 255,
                                                    128, 128, 128, 255, 0,   0,   0,   255, 128, 128, 128, 255,
                                                    180, 180, 180, 255, 128, 128, 128, 255, 180, 180, 180, 255};
    const RasterEditResult result = RunRasterEdit(input);
    Expect(LayerPixelsEqual(result, RasterFormat::Bgra8888, 3, 3, expected),
           "radial.pixel-center-distance-literal-bgra");

    // Legacy radialSpreadsFromStartToRimInEveryDirection; midpoint tolerance ±5, symmetry tolerance ±1.
    RasterEditInput legacy = GradientInput(BlankLayer(101, 101), 101, 101, Vec2{50.5, 50.5}, Vec2{90.5, 50.5},
                                           {0, 0, 0, 1}, {1, 1, 1, 1}, 1.0, GradientShape::Radial);
    const RasterEditResult legacyResult = RunRasterEdit(legacy);
    Expect(legacyResult.status == RasterEditStatus::Ready && legacyResult.layer && legacyResult.layer->asset,
           "legacy.radial-fixture-prepared");
    if (legacyResult.layer && legacyResult.layer->asset)
    {
        const Raster& image = legacyResult.layer->asset->image;
        std::array<std::uint8_t, 4> samples[4]{};
        constexpr std::array<std::array<int, 2>, 4> points{{{70, 50}, {30, 50}, {50, 70}, {50, 30}}};
        for (std::size_t i = 0; i < points.size(); ++i)
            image.ReadRegion(points[i][0], points[i][1], 1, 1, samples[i].data(), 4, 0);
        bool midpoint = true;
        for (const auto& sample : samples)
            midpoint = midpoint && Near(sample[0], 128, 5) && sample[0] == sample[1] && sample[1] == sample[2] &&
                       sample[3] == 255;
        Expect(midpoint, "legacy.radial-midpoints-within-five");
        bool symmetric = true;
        for (std::size_t i = 1; i < points.size(); ++i)
            symmetric = symmetric && Near(samples[i][0], samples[0][0], 1);
        Expect(symmetric && PixelEquals(image, 50, 50, {0, 0, 0, 255}) &&
                   PixelEquals(image, 100, 50, {255, 255, 255, 255}) && PixelEquals(image, 0, 0, {255, 255, 255, 255}),
               "legacy.radial-center-rim-and-beyond-rim");
    }
}

void TransparentRgbaAndOpacityRounding()
{
    constexpr std::array<std::uint8_t, 16> opaqueRed{0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255};
    const Raster source = Bgra(4, 1, opaqueRed);
    if (!source.HasStorage())
    {
        Expect(false, "rgba.source-allocation");
        return;
    }
    RasterEditInput input = GradientInput(ImageLayerOf(source, 4, 1), 4, 1, Vec2{0.5, 0.5}, Vec2{3.5, 0.5},
                                          {0, 0, 1, 0}, {0, 0, 1, 1}, 0.5);
    // BGRA output; source-over rounds each premultiplied channel with lround.
    constexpr std::array<std::uint8_t, 16> expected{0, 0, 255, 255, 43, 0, 213, 255, 85, 0, 170, 255, 128, 0, 128, 255};
    const RasterEditResult result = RunRasterEdit(input);
    Expect(LayerPixelsEqual(result, RasterFormat::Bgra8888, 4, 1, expected),
           "rgba.transparent-to-opaque-blue-half-opacity-over-red-literal-bgra");

    // Legacy reverseOpacityAndDirectionFollowSettings: endpoints remain within Core Graphics' ±3 levels.
    RasterEditInput legacy =
        GradientInput(BlankLayer(101, 4), 101, 4, Vec2{0.5, 2}, Vec2{100.5, 2}, {0, 0, 0, 1}, {1, 1, 1, 1}, 0.5);
    std::swap(legacy.color, legacy.endColor);
    const RasterEditResult legacyResult = RunRasterEdit(legacy);
    Expect(legacyResult.status == RasterEditStatus::Ready && legacyResult.layer && legacyResult.layer->asset,
           "legacy.opacity-fixture-prepared");
    if (legacyResult.layer && legacyResult.layer->asset)
    {
        const Raster& image = legacyResult.layer->asset->image;
        std::array<std::uint8_t, 4> start{};
        std::array<std::uint8_t, 4> end{};
        image.ReadRegion(0, 2, 1, 1, start.data(), 4, 0);
        image.ReadRegion(100, 2, 1, 1, end.data(), 4, 0);
        Expect(Near(start[3], 128, 3) && Near(start[0], start[3], 0) && Near(start[1], start[3], 0) &&
                   Near(start[2], start[3], 0),
               "legacy.reversed-opacity-start-white-alpha-within-three");
        Expect(Near(end[0], 0, 3) && Near(end[1], 0, 3) && Near(end[2], 0, 3) && Near(end[3], 128, 3),
               "legacy.reversed-opacity-end-black-alpha-within-three");
    }
}

void SelectedExteriorAndMaskGrids()
{
    constexpr std::array<std::uint8_t, 16> sourceBytes{0, 0, 20, 255, 0, 0, 40, 255, 0, 0, 60, 255, 0, 0, 80, 255};
    const Raster source = Bgra(4, 1, sourceBytes);
    if (!source.HasStorage())
    {
        Expect(false, "selection.source-allocation");
        return;
    }
    const RasterEditResult selected =
        RunRasterEdit(GradientInput(ImageLayerOf(source, 4, 1), 4, 1, Vec2{0.5, 0.5}, Vec2{3.5, 0.5}, {0, 0, 0, 1},
                                    {1, 1, 1, 1}, 1.0, GradientShape::Linear, Rect(1, 0, 2, 1)));
    constexpr std::array<std::uint8_t, 16> selectedExpected{0,   0,   20,  255, 85, 85, 85, 255,
                                                            170, 170, 170, 255, 0,  0,  80, 255};
    Expect(LayerPixelsEqual(selected, RasterFormat::Bgra8888, 4, 1, selectedExpected),
           "selection.exterior-source-pixels-preserved-literal");

    constexpr std::array<std::uint8_t, 16> imageBytes{0, 0, 70, 255, 0, 0, 80, 255, 0, 0, 90, 255, 0, 0, 100, 255};
    constexpr std::array<std::uint8_t, 2> shortMaskBytes{10, 20};
    const Raster maskSource = Gray(2, 1, shortMaskBytes);
    const Raster maskImage = Bgra(4, 1, imageBytes);
    if (!maskSource.HasStorage() || !maskImage.HasStorage())
    {
        Expect(false, "mask.stretched-fixture-allocation");
        return;
    }
    ImageLayer stretched = ImageLayerOf(maskImage, 4, 1);
    stretched.mask = LayerMask{RasterAsset{maskSource, Raster::MakeThumbnail(maskSource), L"Stretched mask"}};
    const RasterEditResult stretchedResult =
        RunRasterEdit(GradientInput(stretched, 4, 1, Vec2{0.5, 0.5}, Vec2{3.5, 0.5}, {0, 0, 0, 1}, {1, 1, 1, 1}, 1.0,
                                    GradientShape::Linear, std::nullopt, kMaxLayerPixels, true));
    constexpr std::array<std::uint8_t, 4> stretchedExpected{0, 85, 170, 255};
    Expect(LayerPixelsEqual(stretchedResult, RasterFormat::Gray8, 4, 1, stretchedExpected),
           "mask.stretched-gray8-document-pixel-centers-literal");

    constexpr std::array<std::uint8_t, 16> placedImageBytes{0, 0, 70, 255, 0, 0, 80,  255,
                                                            0, 0, 90, 255, 0, 0, 100, 255};
    constexpr std::array<std::uint8_t, 2> placedMaskBytes{31, 219};
    const Raster placedImage = Bgra(4, 1, placedImageBytes);
    const Raster placedMask = Gray(2, 1, placedMaskBytes);
    if (!placedImage.HasStorage() || !placedMask.HasStorage())
    {
        Expect(false, "mask.placed-fixture-allocation");
        return;
    }
    ImageLayer placed = ImageLayerOf(placedImage, 4, 1);
    LayerTransform placement;
    placement.origin = Vec2{1, 0};
    placement.size = Vec2{2, 1};
    placed.mask =
        LayerMask{RasterAsset{placedMask, Raster::MakeThumbnail(placedMask), L"Placed mask"}, true, placement, false};
    const RasterEditResult placedResult =
        RunRasterEdit(GradientInput(placed, 4, 1, Vec2{1.5, 0.5}, Vec2{2.5, 0.5}, {0, 0, 0, 1}, {1, 1, 1, 1}, 1.0,
                                    GradientShape::Linear, std::nullopt, kMaxLayerPixels, true));
    constexpr std::array<std::uint8_t, 2> placedExpected{0, 255};
    Expect(LayerPixelsEqual(placedResult, RasterFormat::Gray8, 2, 1, placedExpected),
           "mask.placed-gray8-gradient-uses-mask-document-placement");
}

void SparseBaseFillAndTileStayUnmaterialized()
{
    constexpr std::array<std::uint8_t, 8> baseBytes{1, 2, 3, 10, 4, 5, 6, 20};
    constexpr std::array<std::uint8_t, 4> tileBytes{7, 8, 9, 30};
    const Raster base = Bgra(2, 1, baseBytes);
    const Raster tile = Bgra(1, 1, tileBytes);
    if (!base.HasStorage() || !tile.HasStorage())
    {
        Expect(false, "sparse.fixture-allocation");
        return;
    }
    const Raster sparse = Raster::MakeSparse(4, 1, RasterFormat::Bgra8888, base, 0, 0, {RasterTile{3, 0, tile}}, 0x40);
    if (!sparse.HasStorage() || !sparse.IsSparse())
    {
        Expect(false, "sparse.fixture-construction");
        return;
    }
    ImageLayer layer = ImageLayerOf(sparse, 4, 1);
    RasterEditInput input = GradientInput(layer, 4, 1, Vec2{0.5, 0.5}, Vec2{3.5, 0.5}, {0, 0, 0, 1}, {1, 1, 1, 1}, 1.0,
                                          GradientShape::Linear, Rect(0, 0, 1, 1));
    constexpr std::array<std::uint8_t, 16> expected{0, 0, 0, 255, 4, 5, 6, 20, 64, 64, 64, 64, 7, 8, 9, 30};
    const RasterEditResult result = RunRasterEdit(input);
    Expect(LayerPixelsEqual(result, RasterFormat::Bgra8888, 4, 1, expected),
           "sparse.gradient-preserves-base-fill-and-tile-by-literal-output");
    Expect(!sparse.IsMaterialized() && sparse.IsSparse() && sparse.Fill() == 0x40 && sparse.Base() == base &&
               sparse.Tiles().size() == 1 && sparse.Tiles()[0].pixels == tile,
           "sparse.gradient-source-remains-unmaterialized-with-original-base-fill-tile");
}

void CapturedValuesAndNoLine()
{
    // The input is the captured value record. Mutating a separate session after capture cannot change its colors,
    // shape, or opacity and the worker has no session/palette reference.
    RasterEditInput captured =
        GradientInput(BlankLayer(4, 1), 4, 1, Vec2{0.5, 0.5}, Vec2{3.5, 0.5}, {0, 0, 0, 1}, {1, 1, 1, 1});
    DocumentSession otherSession;
    otherSession.SetForegroundColor(0xFFFF0000u);
    otherSession.SetBackgroundColor(0xFF0000FFu);
    otherSession.gradientSettings.shape = GradientShape::Radial;
    otherSession.gradientSettings.reversed = true;
    otherSession.gradientSettings.opacity = 0.25;
    constexpr std::array<std::uint8_t, 16> expected{0,   0,   0,   255, 85,  85,  85,  255,
                                                    170, 170, 170, 255, 255, 255, 255, 255};
    const RasterEditResult result = RunRasterEdit(captured);
    Expect(LayerPixelsEqual(result, RasterFormat::Bgra8888, 4, 1, expected),
           "capture.input-colors-settings-independent-of-later-session-palette");

    RasterEditInput noLine =
        GradientInput(BlankLayer(4, 1), 4, 1, Vec2{0.5, 0.5}, Vec2{0.99, 0.5}, {0, 0, 0, 1}, {1, 1, 1, 1});
    const RasterEditResult unchanged = RunRasterEdit(noLine);
    const RasterEditPreviewResult preview = RunRasterEditPreview(noLine);
    Expect(unchanged.status == RasterEditStatus::Unchanged && !unchanged.layer &&
               preview.status == RasterEditStatus::Unchanged && !preview.stroke,
           "status.sub-half-pixel-no-line-is-unchanged-without-layer-or-stroke");
}

bool BothEntryPointsReject(const RasterEditInput& input)
{
    const RasterEditResult committed = RunRasterEdit(input);
    const RasterEditPreviewResult preview = RunRasterEditPreview(input);
    return committed.status == RasterEditStatus::Invalid && !committed.layer &&
           preview.status == RasterEditStatus::Invalid && !preview.stroke;
}

void InvalidInputsAndBudgetBoundaries()
{
    const auto input = []
    { return GradientInput(BlankLayer(4, 1), 4, 1, Vec2{0.5, 0.5}, Vec2{3.5, 0.5}, {0, 0, 0, 1}, {1, 1, 1, 1}); };
    RasterEditInput bad = input();
    bad.start.x = std::numeric_limits<double>::quiet_NaN();
    Expect(BothEntryPointsReject(bad), "invalid.nan-start");
    bad = input();
    bad.end.y = std::numeric_limits<double>::infinity();
    Expect(BothEntryPointsReject(bad), "invalid.infinite-end");
    bad = input();
    bad.shape = static_cast<GradientShape>(255);
    Expect(BothEntryPointsReject(bad), "invalid.unknown-shape-enum");
    bad = input();
    bad.opacity = std::numeric_limits<double>::quiet_NaN();
    Expect(BothEntryPointsReject(bad), "invalid.nan-opacity");
    bad = input();
    bad.opacity = -0.01;
    Expect(BothEntryPointsReject(bad), "invalid.opacity-below-zero");
    bad = input();
    bad.opacity = 1.01;
    Expect(BothEntryPointsReject(bad), "invalid.opacity-above-one");
    bad = input();
    bad.color[2] = std::numeric_limits<double>::infinity();
    Expect(BothEntryPointsReject(bad), "invalid.infinite-start-color-channel");
    bad = input();
    bad.color[1] = std::numeric_limits<double>::quiet_NaN();
    Expect(BothEntryPointsReject(bad), "invalid.nan-start-color-channel");
    bad = input();
    bad.endColor[0] = 1.01;
    Expect(BothEntryPointsReject(bad), "invalid.end-color-out-of-range");
    bad = input();
    constexpr double far = 1.0e168;
    bad.start = Vec2{far, 0.5};
    bad.end = Vec2{std::nextafter(far, std::numeric_limits<double>::infinity()), 0.5};
    Expect(std::isfinite(bad.end.x - bad.start.x) &&
               std::isfinite((bad.end.x - bad.start.x) * (bad.end.x - bad.start.x)) && BothEntryPointsReject(bad),
           "invalid.finite-length-far-origin-projection-overflows");

    RasterEditInput exact = input();
    exact.pixelLimit = 4;
    constexpr std::array<std::uint8_t, 16> expected{0,   0,   0,   255, 85,  85,  85,  255,
                                                    170, 170, 170, 255, 255, 255, 255, 255};
    const RasterEditResult exactResult = RunRasterEdit(exact);
    Expect(LayerPixelsEqual(exactResult, RasterFormat::Bgra8888, 4, 1, expected),
           "budget.exact-four-pixel-limit-succeeds-literal");
    exact.pixelLimit = 3;
    const RasterEditResult lowerResult = RunRasterEdit(exact);
    Expect(lowerResult.status == RasterEditStatus::TooLarge && !lowerResult.layer,
           "budget.lower-three-pixel-limit-refuses-without-layer");
    const std::uint64_t exactMemory = RasterEditMemoryBytes(exact.layer, std::nullopt, 4, 1, 4, false);
    const std::uint64_t lowerMemory = RasterEditMemoryBytes(exact.layer, std::nullopt, 4, 1, 3, false);
    Expect(exactMemory > lowerMemory && lowerMemory > 0,
           "budget.memory-reservation-positive-and-monotonic-at-exact-and-lower-pixel-limits");
}

void CancellationDoesNotPublishPartialResults()
{
    RasterEditInput small =
        GradientInput(BlankLayer(4, 1), 4, 1, Vec2{0.5, 0.5}, Vec2{3.5, 0.5}, {0, 0, 0, 1}, {1, 1, 1, 1});
    CancelSource preCancelled;
    (void)preCancelled.Cancel();
    const RasterEditResult stopped = RunRasterEdit(small, preCancelled.Token());
    const RasterEditPreviewResult stoppedPreview = RunRasterEditPreview(small, preCancelled.Token());
    Expect(stopped.status == RasterEditStatus::Cancelled && !stopped.layer &&
               stoppedPreview.status == RasterEditStatus::Cancelled && !stoppedPreview.stroke,
           "cancel.pre-cancelled-commit-and-preview-publish-no-result");

    constexpr int width = 257;
    ImageLayer layer = BlankLayer(width, 1);
    RasterEditInput input = GradientInput(std::move(layer), width, 1, Vec2{0.5, 0.5}, Vec2{256.5, 0.5}, {0, 0, 0, 1},
                                          {1, 1, 1, 1}, 1.0, GradientShape::Linear, std::nullopt, width);
    CancelSource cancellation;
    MidTileCancel progress{&cancellation};
    input.progress = CancelAtFirstCompletedTile;
    input.progressContext = &progress;
    const RasterEditResult midTile = RunRasterEdit(input, cancellation.Token());
    Expect(midTile.status == RasterEditStatus::Cancelled && !midTile.layer && progress.completed == 1 &&
               progress.total == 2 && cancellation.Token().IsCancelled(),
           "cancel.width-257-after-first-tile-returns-no-layer");

    CancelSource previewCancellation;
    MidTileCancel previewProgress{&previewCancellation};
    input.progressContext = &previewProgress;
    const RasterEditPreviewResult midTilePreview = RunRasterEditPreview(input, previewCancellation.Token());
    Expect(midTilePreview.status == RasterEditStatus::Cancelled && !midTilePreview.stroke &&
               previewProgress.completed == 1 && previewProgress.total == 2 &&
               previewCancellation.Token().IsCancelled(),
           "cancel.width-257-preview-after-first-tile-returns-no-stroke");
}
} // namespace

int RunGradientWorkTests()
{
    gFailures = 0;
    LinearLiteralAndPreview();
    RadialLiteralAndLegacy();
    TransparentRgbaAndOpacityRounding();
    SelectedExteriorAndMaskGrids();
    SparseBaseFillAndTileStayUnmaterialized();
    CapturedValuesAndNoLine();
    InvalidInputsAndBudgetBoundaries();
    CancellationDoesNotPublishPartialResults();
    return gFailures;
}
