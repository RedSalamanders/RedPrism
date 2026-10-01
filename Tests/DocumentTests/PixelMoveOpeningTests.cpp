#include "PixelMoveOpeningTests.h"

#include "BrushStroke.h"
#include "Selection.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace PixelMoveOpeningTests
{
namespace
{
void Expect(int& failures, bool condition, const char* name)
{
    std::printf("%s pixelmoveopening.%s\n", condition ? "PASS" : "FAIL", name);
    failures += condition ? 0 : 1;
}

Raster Bgra(int width, int height, std::span<const std::uint8_t> bytes)
{
    return Raster::Make(width, height, RasterFormat::Bgra8888, bytes.data(), bytes.size());
}

ImageLayer Image(const Raster& pixels)
{
    ImageLayer layer = ImageLayer::Blank(
        L"Pixel move opening", Vec2{static_cast<double>(pixels.Width()), static_cast<double>(pixels.Height())});
    layer.asset = RasterAsset{pixels, Raster{}, L"Pixel move opening"};
    return layer;
}

Raster Coverage(int canvasWidth, int canvasHeight, double x, double y, double width, double height)
{
    const std::vector<SelectionLoop> loops{
        {Vec2{x, y}, Vec2{x + width, y}, Vec2{x + width, y + height}, Vec2{x, y + height}}};
    return SelectionGeometry::Coverage(loops, canvasWidth, canvasHeight, false);
}

bool Configure(BrushStroke& stroke, int canvasWidth, int canvasHeight, double x, double y, double width, double height)
{
    if (!stroke.ConfigureWorkerRasterEdit({}))
        return false;
    Raster coverage = Coverage(canvasWidth, canvasHeight, x, y, width, height);
    if (!coverage.HasStorage())
        return false;
    stroke.SetSelectionClip(std::move(coverage));
    return true;
}

bool SameRaster(const Raster& left, const Raster& right)
{
    if (!left.HasStorage() || !right.HasStorage() || left.Width() != right.Width() || left.Height() != right.Height() ||
        left.Format() != right.Format())
        return false;
    const int channels = left.Channels();
    std::vector<std::uint8_t> a(static_cast<std::size_t>(left.Width()) * static_cast<std::size_t>(left.Height()) *
                                static_cast<std::size_t>(channels));
    std::vector<std::uint8_t> b(a.size());
    left.ReadRegion(0, 0, left.Width(), left.Height(), a.data(), left.Width() * channels, 0);
    right.ReadRegion(0, 0, right.Width(), right.Height(), b.data(), right.Width() * channels, 0);
    return a == b;
}

std::optional<BrushStroke::Commit> FreshMove(const ImageLayer& layer, int canvasWidth, int canvasHeight, double x,
                                             double y, double selectionWidth, double selectionHeight, Vec2 offset,
                                             bool duplicate)
{
    BrushStroke stroke(layer, false, BrushSettings{}, canvasWidth, canvasHeight, nullptr);
    if (!Configure(stroke, canvasWidth, canvasHeight, x, y, selectionWidth, selectionHeight) ||
        !stroke.LiftSelection() || !stroke.MoveLifted(offset, duplicate))
        return std::nullopt;
    return stroke.PaintSnapshot();
}

std::optional<BrushStroke::Commit> CachedMove(const ImageLayer& layer, int canvasWidth, int canvasHeight, double x,
                                              double y, double selectionWidth, double selectionHeight, Vec2 offset,
                                              bool duplicate, bool captureAfterMove)
{
    BrushStroke openingStroke(layer, false, BrushSettings{}, canvasWidth, canvasHeight, nullptr);
    if (!Configure(openingStroke, canvasWidth, canvasHeight, x, y, selectionWidth, selectionHeight) ||
        !openingStroke.LiftSelection())
        return std::nullopt;
    if (captureAfterMove && !openingStroke.MoveLifted(offset, duplicate))
        return std::nullopt;
    const std::optional<BrushStrokeOpening> opening = openingStroke.CaptureLiftedOpening();
    if (!opening.has_value() || opening.value().RetirementMemoryBytes() == 0)
        return std::nullopt;

    BrushStroke resumed(layer, false, BrushSettings{}, canvasWidth, canvasHeight, nullptr);
    if (!resumed.ConfigureWorkerRasterEdit({}) || !resumed.UseLiftedOpening(opening.value()))
        return std::nullopt;
    if (!resumed.MoveLifted(offset, duplicate))
        return std::nullopt;
    return resumed.PaintSnapshot();
}

void ExistingLiteralMoveAndDuplicateGoldens(int& failures)
{
    constexpr std::array<std::uint8_t, 16> pixels{10, 20, 30, 255, 40,  50,  60,  255,
                                                  70, 80, 90, 255, 100, 110, 120, 255};
    constexpr std::array<std::uint8_t, 16> moved{10, 20, 30, 255, 0, 0, 0, 0, 70, 80, 90, 255, 40, 50, 60, 255};
    const Raster source = Bgra(4, 1, pixels);
    const ImageLayer layer = Image(source);
    const auto fresh = FreshMove(layer, 4, 1, 1, 0, 1, 1, Vec2{2, 0}, false);
    const auto cached = CachedMove(layer, 4, 1, 1, 0, 1, 1, Vec2{2, 0}, false, false);
    const Raster expected = Bgra(4, 1, moved);
    Expect(failures,
           fresh.has_value() && cached.has_value() && SameRaster(fresh.value().image, expected) &&
               SameRaster(cached.value().image, expected) && SameRaster(fresh.value().image, cached.value().image),
           "literal-hole-and-landing-golden-cached-and-fresh");

    constexpr std::array<std::uint8_t, 8> translucent{17, 33, 65, 128, 90, 80, 70, 255};
    constexpr std::array<std::uint8_t, 8> duplicate{17, 33, 65, 128, 62, 73, 100, 255};
    const Raster translucentSource = Bgra(2, 1, translucent);
    const ImageLayer translucentLayer = Image(translucentSource);
    const auto duplicateFresh = FreshMove(translucentLayer, 2, 1, 0, 0, 1, 1, Vec2{1, 0}, true);
    const auto duplicateCached = CachedMove(translucentLayer, 2, 1, 0, 0, 1, 1, Vec2{1, 0}, true, false);
    const Raster duplicateExpected = Bgra(2, 1, duplicate);
    Expect(failures,
           duplicateFresh.has_value() && duplicateCached.has_value() &&
               SameRaster(duplicateFresh.value().image, duplicateExpected) &&
               SameRaster(duplicateCached.value().image, duplicateExpected) &&
               SameRaster(duplicateFresh.value().image, duplicateCached.value().image),
           "literal-partial-alpha-duplicate-golden");
}

void PlacedAndSparseSources(int& failures)
{
    constexpr std::array<std::uint8_t, 8> placedPixels{5, 15, 25, 255, 80, 90, 100, 255};
    const Raster placedSource = Bgra(2, 1, placedPixels);
    ImageLayer placedLayer = Image(placedSource);
    placedLayer.transform.size = Vec2{4, 2};
    const auto placedFresh = FreshMove(placedLayer, 4, 2, 0, 0, 2, 2, Vec2{2, 0}, false);
    const auto placedCached = CachedMove(placedLayer, 4, 2, 0, 0, 2, 2, Vec2{2, 0}, false, true);
    constexpr std::array<std::uint8_t, 8> placedExpected{0, 0, 0, 0, 5, 15, 25, 255};
    const Raster expectedPlaced = Bgra(2, 1, placedExpected);
    Expect(failures,
           placedFresh.has_value() && placedCached.has_value() &&
               SameRaster(placedFresh.value().image, expectedPlaced) &&
               SameRaster(placedCached.value().image, expectedPlaced) &&
               SameRaster(placedFresh.value().image, placedCached.value().image),
           "placed-transform-capture-after-move-matches-literal-and-fresh");

    constexpr std::array<std::uint8_t, 8> basePixels{1, 2, 3, 255, 4, 5, 6, 255};
    constexpr std::array<std::uint8_t, 4> tilePixels{7, 8, 9, 255};
    const Raster base = Bgra(2, 1, basePixels);
    const Raster tile = Bgra(1, 1, tilePixels);
    const Raster sparse = Raster::MakeSparse(4, 1, RasterFormat::Bgra8888, base, 0, 0,
                                             std::vector<RasterTile>{RasterTile{3, 0, tile}}, 0);
    ImageLayer sparseLayer = Image(sparse);
    const auto sparseFresh = FreshMove(sparseLayer, 4, 1, 0, 0, 1, 1, Vec2{1, 0}, false);
    const auto sparseCached = CachedMove(sparseLayer, 4, 1, 0, 0, 1, 1, Vec2{1, 0}, false, false);
    constexpr std::array<std::uint8_t, 16> sparseExpected{0, 0, 0, 0, 1, 2, 3, 255, 0, 0, 0, 0, 7, 8, 9, 255};
    const Raster expectedSparse = Bgra(4, 1, sparseExpected);
    Expect(failures,
           sparse.HasStorage() && !sparse.IsMaterialized() && sparseFresh.has_value() && sparseCached.has_value() &&
               SameRaster(sparseFresh.value().image, expectedSparse) &&
               SameRaster(sparseCached.value().image, expectedSparse) &&
               SameRaster(sparseFresh.value().image, sparseCached.value().image) && !sparse.IsMaterialized(),
           "sparse-source-literal-golden-remains-unmaterialized-and-matches-fresh");
}

void RefusalsDoNotInstallState(int& failures)
{
    constexpr std::array<std::uint8_t, 8> pixels{1, 2, 3, 255, 4, 5, 6, 255};
    const Raster source = Bgra(2, 1, pixels);
    const ImageLayer original = Image(source);
    BrushStroke openingStroke(original, false, BrushSettings{}, 2, 1, nullptr);
    const bool opened = Configure(openingStroke, 2, 1, 0, 0, 1, 1) && openingStroke.LiftSelection();
    std::optional<BrushStrokeOpening> opening;
    if (opened)
        opening = openingStroke.CaptureLiftedOpening();

    const Raster otherSource = Bgra(2, 1, pixels);
    const ImageLayer otherImage = Image(otherSource);
    BrushStroke wrongSource(otherImage, false, BrushSettings{}, 2, 1, nullptr);
    const bool wrongSourceRejected = wrongSource.ConfigureWorkerRasterEdit({}) && opening.has_value() &&
                                     !wrongSource.UseLiftedOpening(opening.value()) &&
                                     !wrongSource.CaptureLiftedOpening().has_value();

    BrushStroke wrongCanvas(original, false, BrushSettings{}, 3, 1, nullptr);
    const bool wrongCanvasRejected = wrongCanvas.ConfigureWorkerRasterEdit({}) && opening.has_value() &&
                                     !wrongCanvas.UseLiftedOpening(opening.value()) &&
                                     !wrongCanvas.CaptureLiftedOpening().has_value();

    ImageLayer shiftedLayer = original;
    shiftedLayer.transform.origin.x = 1;
    BrushStroke wrongTransform(shiftedLayer, false, BrushSettings{}, 2, 1, nullptr);
    const bool wrongTransformRejected = wrongTransform.ConfigureWorkerRasterEdit({}) && opening.has_value() &&
                                        !wrongTransform.UseLiftedOpening(opening.value()) &&
                                        !wrongTransform.CaptureLiftedOpening().has_value();

    BrushStroke notFresh(original, false, BrushSettings{}, 2, 1, nullptr);
    const bool notFreshRejected = Configure(notFresh, 2, 1, 0, 0, 1, 1) && opening.has_value() &&
                                  !notFresh.UseLiftedOpening(opening.value()) &&
                                  !notFresh.CaptureLiftedOpening().has_value();
    BrushStroke installed(original, false, BrushSettings{}, 2, 1, nullptr);
    const bool secondInstallRejected = installed.ConfigureWorkerRasterEdit({}) && opening.has_value() &&
                                       installed.UseLiftedOpening(opening.value()) &&
                                       !installed.UseLiftedOpening(opening.value());
    constexpr std::array<std::uint8_t, 8> movedPixels{0, 0, 0, 0, 1, 2, 3, 255};
    const Raster expectedMoved = Bgra(2, 1, movedPixels);
    const bool secondInstallPreservedState = secondInstallRejected && installed.MoveLifted(Vec2{1, 0}, false);
    const std::optional<BrushStroke::Commit> installedSnapshot =
        secondInstallPreservedState ? installed.PaintSnapshot() : std::nullopt;
    Expect(failures,
           wrongSourceRejected && wrongCanvasRejected && wrongTransformRejected && notFreshRejected &&
               secondInstallRejected && secondInstallPreservedState && installedSnapshot.has_value() &&
               SameRaster(installedSnapshot.value().image, expectedMoved),
           "wrong-source-transform-canvas-and-nonfresh-stroke-refuse-without-installed-lift");

    constexpr std::array<std::uint8_t, 12> widerPixels{1, 2, 3, 255, 4, 5, 6, 255, 7, 8, 9, 255};
    ImageLayer wrongGridImage = Image(Bgra(3, 1, widerPixels));
    BrushStroke wrongGrid(wrongGridImage, false, BrushSettings{}, 2, 1, nullptr);
    const bool wrongGridRejected = wrongGrid.ConfigureWorkerRasterEdit({}) && opening.has_value() &&
                                   !wrongGrid.UseLiftedOpening(opening.value()) &&
                                   !wrongGrid.CaptureLiftedOpening().has_value();
    Expect(failures, wrongGridRejected, "wrong-grid-refuses-without-installed-lift");
}

void CaptureRefusals(int& failures)
{
    constexpr std::array<std::uint8_t, 8> pixels{1, 2, 3, 255, 4, 5, 6, 255};
    const ImageLayer layer = Image(Bgra(2, 1, pixels));

    BrushStroke notLifted(layer, false, BrushSettings{}, 2, 1, nullptr);
    const bool notLiftedRejected =
        notLifted.ConfigureWorkerRasterEdit({}) && !notLifted.CaptureLiftedOpening().has_value();
    Expect(failures, notLiftedRejected, "capture-refuses-before-selection-is-lifted");

    CancelSource cancellation;
    BrushStroke cancelled(layer, false, BrushSettings{}, 2, 1, nullptr);
    const bool cancelledSetup = Configure(cancelled, 2, 1, 0, 0, 1, 1);
    const bool cancelledLift =
        cancelledSetup && cancelled.ConfigureWorkerRasterEdit(cancellation.Token()) && cancelled.LiftSelection();
    (void)cancellation.Cancel();
    Expect(failures, cancelledLift && !cancelled.CaptureLiftedOpening().has_value(),
           "capture-refuses-after-worker-cancellation");

    BrushStroke mask(layer, true, BrushSettings{}, 2, 1, nullptr);
    const bool maskConfigured = mask.ConfigureWorkerRasterEdit({});
    Expect(failures, maskConfigured && !mask.CaptureLiftedOpening().has_value(),
           "mask-stroke-cannot-capture-image-opening");

    ImageLayer noPixels = ImageLayer::Blank(L"Empty", Vec2{2, 1});
    BrushStroke withoutSource(noPixels, false, BrushSettings{}, 2, 1, nullptr);
    const bool noSourceConfigured = withoutSource.ConfigureWorkerRasterEdit({});
    Expect(failures, withoutSource.IsValid() && noSourceConfigured && !withoutSource.CaptureLiftedOpening().has_value(),
           "stroke-without-image-source-cannot-capture-opening");

    ImageLayer badTransform = noPixels;
    badTransform.transform.size.x = 0;
    BrushStroke invalid(badTransform, false, BrushSettings{}, 2, 1, nullptr);
    const bool invalidConfigured = invalid.ConfigureWorkerRasterEdit({});
    Expect(failures, !invalid.IsValid() && !invalidConfigured && !invalid.CaptureLiftedOpening().has_value(),
           "invalid-stroke-cannot-capture-opening");
}

void MovedFromOpeningRefusalPreservesFreshStroke(int& failures)
{
    constexpr std::array<std::uint8_t, 8> pixels{11, 22, 33, 255, 44, 55, 66, 255};
    constexpr std::array<std::uint8_t, 8> expectedPixels{0, 0, 0, 0, 11, 22, 33, 255};
    const ImageLayer layer = Image(Bgra(2, 1, pixels));
    BrushStroke source(layer, false, BrushSettings{}, 2, 1, nullptr);
    std::optional<BrushStrokeOpening> movedFrom;
    if (Configure(source, 2, 1, 0, 0, 1, 1) && source.LiftSelection())
        movedFrom = source.CaptureLiftedOpening();

    bool rejected = false;
    BrushStroke target(layer, false, BrushSettings{}, 2, 1, nullptr);
    if (movedFrom.has_value())
    {
        BrushStrokeOpening validOpening = std::move(movedFrom).value();
        rejected = validOpening.RetirementMemoryBytes() != 0 && target.ConfigureWorkerRasterEdit({}) &&
                   !target.UseLiftedOpening(movedFrom.value());
    }
    const bool freshOperationContinues = rejected && Configure(target, 2, 1, 0, 0, 1, 1) && target.LiftSelection() &&
                                         target.MoveLifted(Vec2{1, 0}, false);
    const std::optional<BrushStroke::Commit> result = freshOperationContinues ? target.PaintSnapshot() : std::nullopt;
    const Raster expected = Bgra(2, 1, expectedPixels);
    Expect(failures,
           rejected && freshOperationContinues && result.has_value() && SameRaster(result.value().image, expected),
           "moved-from-opening-refusal-leaves-fresh-stroke-usable");
}
} // namespace

void Run(int& failures)
{
    ExistingLiteralMoveAndDuplicateGoldens(failures);
    PlacedAndSparseSources(failures);
    RefusalsDoNotInstallState(failures);
    CaptureRefusals(failures);
    MovedFromOpeningRefusalPreservesFreshStroke(failures);
}
} // namespace PixelMoveOpeningTests
