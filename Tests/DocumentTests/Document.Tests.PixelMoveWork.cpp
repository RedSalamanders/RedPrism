#include "Document.Tests.PixelMoveWork.h"

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

namespace PixelMoveWorkTests
{
namespace
{
void Expect(int& failures, bool condition, const char* name)
{
    std::printf("%s pixelmovework.%s\n", condition ? "PASS" : "FAIL", name);
    failures += condition ? 0 : 1;
}

Raster Bgra(int width, int height, std::span<const std::uint8_t> bytes)
{
    return Raster::Make(width, height, RasterFormat::Bgra8888, bytes.data(), bytes.size());
}

ImageLayer Image(const Raster& pixels, int width, int height)
{
    ImageLayer layer = ImageLayer::Blank(L"Pixel move", Vec2{static_cast<double>(width), static_cast<double>(height)});
    layer.asset = RasterAsset{pixels, Raster::MakeThumbnail(pixels), L"Pixel move"};
    return layer;
}

DocumentSelection Rect(double x, double y, double width, double height, bool antialiased = false)
{
    return DocumentSelection{antialiased, SelectionLoops{{Vec2{x, y}, Vec2{x + width, y}, Vec2{x + width, y + height},
                                                          Vec2{x, y + height}}}};
}

RasterEditInput Input(ImageLayer layer, int canvasWidth, int canvasHeight, DocumentSelection selection)
{
    RasterEditInput input;
    input.layer = std::move(layer);
    input.selection = std::move(selection);
    input.canvasWidth = canvasWidth;
    input.canvasHeight = canvasHeight;
    input.pixelLimit = kMaxLayerPixels;
    input.kind = RasterEditKind::PixelMove;
    input.offset = Vec2{1, 0};
    return input;
}

bool PixelsEqual(const Raster& raster, int width, int height, std::span<const std::uint8_t> expected)
{
    if (!raster.HasStorage() || raster.Format() != RasterFormat::Bgra8888 || raster.Width() != width ||
        raster.Height() != height ||
        expected.size() != static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u)
        return false;
    std::vector<std::uint8_t> actual(expected.size());
    raster.ReadRegion(0, 0, width, height, actual.data(), width * 4, 0);
    return std::equal(expected.begin(), expected.end(), actual.begin());
}

bool ImageIs(const RasterEditResult& result, int width, int height, std::span<const std::uint8_t> expected)
{
    return result.status == RasterEditStatus::Ready && result.layer && result.layer->asset &&
           PixelsEqual(result.layer->asset->image, width, height, expected);
}

bool OutlineIs(const RasterEditResult& result, const DocumentSelection& expected)
{
    return result.status == RasterEditStatus::Ready && result.selection && result.selection.value() == expected;
}

constexpr std::array<std::uint8_t, 16> kFourPixels{10, 20, 30, 255, 40,  50,  60,  255,
                                                   70, 80, 90, 255, 100, 110, 120, 255};

void HoleAndLanding(int& failures)
{
    const Raster source = Bgra(4, 1, kFourPixels);
    if (!source.HasStorage())
    {
        Expect(failures, false, "literal.fixture-allocation");
        return;
    }
    RasterEditInput input = Input(Image(source, 4, 1), 4, 1, Rect(1, 0, 1, 1));
    input.offset = Vec2{2, 0};
    constexpr std::array<std::uint8_t, 16> expected{10, 20, 30, 255, 0, 0, 0, 0, 70, 80, 90, 255, 40, 50, 60, 255};
    const RasterEditResult result = RunRasterEdit(input);
    const bool commitMetadata = result.previewCommit && result.previewCommit->image.HasStorage() &&
                                result.previewCommit->width > 0 && result.previewCommit->height > 0;
    Expect(failures,
           ImageIs(result, 4, 1, expected) && result.layer && result.layer->id == input.layer.id && commitMetadata,
           "literal.cut-hole-land-target-layer-and-commit-metadata");
}

void DuplicatePartialAlpha(int& failures)
{
    constexpr std::array<std::uint8_t, 8> sourceBytes{17, 33, 65, 128, 90, 80, 70, 255};
    const Raster source = Bgra(2, 1, sourceBytes);
    if (!source.HasStorage())
    {
        Expect(failures, false, "duplicate.fixture-allocation");
        return;
    }
    RasterEditInput input = Input(Image(source, 2, 1), 2, 1, Rect(0, 0, 1, 1));
    input.offset = Vec2{1, 0};
    input.duplicate = true;
    // Premultiplied source-over on the opaque destination: src + dst * (1 - 128/255), rounded to nearest.
    constexpr std::array<std::uint8_t, 8> expected{17, 33, 65, 128, 62, 73, 100, 255};
    const RasterEditResult result = RunRasterEdit(input);
    Expect(failures, ImageIs(result, 2, 1, expected), "duplicate.partial-alpha-source-over-existing-pixel");
}

void StretchedPixelGridAndTranslatedOutline(int& failures)
{
    constexpr std::array<std::uint8_t, 8> sourceBytes{5, 15, 25, 255, 80, 90, 100, 255};
    const Raster source = Bgra(2, 1, sourceBytes);
    if (!source.HasStorage())
    {
        Expect(failures, false, "mapping.fixture-allocation");
        return;
    }
    ImageLayer layer = Image(source, 2, 1);
    layer.transform.size = Vec2{4, 2};
    RasterEditInput input = Input(std::move(layer), 4, 2, Rect(0, 0, 2, 2));
    input.offset = Vec2{2, 0};
    const RasterEditResult result = RunRasterEdit(input);
    const DocumentSelection moved = Rect(2, 0, 2, 2);
    constexpr std::array<std::uint8_t, 8> expectedPixels{0, 0, 0, 0, 5, 15, 25, 255};
    Expect(failures, OutlineIs(result, moved) && ImageIs(result, 2, 1, expectedPixels),
           "mapping.document-offset-translates-outline-and-pixels-on-stretched-grid");
}

void PositiveAndNegativeRounding(int& failures)
{
    const Raster source = Bgra(4, 1, kFourPixels);
    if (!source.HasStorage())
    {
        Expect(failures, false, "rounding.fixture-allocation");
        return;
    }
    RasterEditInput positive = Input(Image(source, 4, 1), 4, 1, Rect(1, 0, 1, 1));
    positive.offset = Vec2{0.5, 0};
    const RasterEditResult positiveResult = RunRasterEdit(positive);
    constexpr std::array<std::uint8_t, 16> positiveExpected{10, 20, 30, 255, 0,   0,   0,   0,
                                                            40, 50, 60, 255, 100, 110, 120, 255};
    RasterEditInput negative = Input(Image(source, 4, 1), 4, 1, Rect(1, 0, 1, 1));
    negative.offset = Vec2{-0.5, 0};
    constexpr std::array<std::uint8_t, 16> negativeExpected{40, 50, 60, 255, 0,   0,   0,   0,
                                                            70, 80, 90, 255, 100, 110, 120, 255};
    const RasterEditResult negativeResult = RunRasterEdit(negative);
    Expect(failures, ImageIs(positiveResult, 4, 1, positiveExpected) && ImageIs(negativeResult, 4, 1, negativeExpected),
           "rounding-half-away-from-zero-in-document-pixel-space");
}

void OffCanvasStillMovesOutline(int& failures)
{
    constexpr std::array<std::uint8_t, 8> sourceBytes{11, 22, 33, 255, 44, 55, 66, 255};
    const Raster source = Bgra(2, 1, sourceBytes);
    if (!source.HasStorage())
    {
        Expect(failures, false, "offcanvas.fixture-allocation");
        return;
    }
    RasterEditInput input = Input(Image(source, 2, 1), 2, 1, Rect(0, 0, 1, 1));
    input.offset = Vec2{3, 0};
    constexpr std::array<std::uint8_t, 8> expected{0, 0, 0, 0, 44, 55, 66, 255};
    const RasterEditResult result = RunRasterEdit(input);
    Expect(failures, ImageIs(result, 2, 1, expected) && OutlineIs(result, Rect(3, 0, 1, 1)),
           "offcanvas-cut-remains-and-outline-translates-without-clipping");
}

void ExplicitEmptyAndOutsideSelection(int& failures)
{
    const Raster source = Bgra(4, 1, kFourPixels);
    if (!source.HasStorage())
    {
        Expect(failures, false, "empty.fixture-allocation");
        return;
    }
    RasterEditInput empty = Input(Image(source, 4, 1), 4, 1, Rect(1, 0, 0, 1));
    const RasterEditResult emptyResult = RunRasterEdit(empty);
    RasterEditInput outside = Input(Image(source, 4, 1), 4, 1, Rect(10, 0, 1, 1));
    const RasterEditResult outsideResult = RunRasterEdit(outside);
    Expect(failures,
           emptyResult.status == RasterEditStatus::Unchanged && !emptyResult.layer &&
               outsideResult.status == RasterEditStatus::Unchanged && !outsideResult.layer,
           "selection-explicit-empty-and-outside-canvas-touch-nothing");
}

void InvalidInputs(int& failures)
{
    const Raster source = Bgra(4, 1, kFourPixels);
    if (!source.HasStorage())
    {
        Expect(failures, false, "invalid.fixture-allocation");
        return;
    }
    const ImageLayer layer = Image(source, 4, 1);
    RasterEditInput nan = Input(layer, 4, 1, Rect(0, 0, 1, 1));
    nan.offset.x = std::numeric_limits<double>::quiet_NaN();
    RasterEditInput infinite = Input(layer, 4, 1, Rect(0, 0, 1, 1));
    infinite.offset.y = std::numeric_limits<double>::infinity();
    RasterEditInput huge = Input(layer, 4, 1, Rect(0, 0, 1, 1));
    huge.offset.x = 2.0e9;
    RasterEditInput mask = Input(layer, 4, 1, Rect(0, 0, 1, 1));
    mask.mask = true;
    RasterEditInput missingImage = Input(ImageLayer::Blank(L"No pixels", Vec2{4, 1}), 4, 1, Rect(0, 0, 1, 1));
    const RasterEditResult nanResult = RunRasterEdit(nan);
    const RasterEditResult infiniteResult = RunRasterEdit(infinite);
    const RasterEditResult hugeResult = RunRasterEdit(huge);
    const RasterEditResult maskResult = RunRasterEdit(mask);
    const RasterEditResult missingImageResult = RunRasterEdit(missingImage);
    Expect(failures,
           nanResult.status == RasterEditStatus::Invalid && infiniteResult.status == RasterEditStatus::Invalid &&
               hugeResult.status == RasterEditStatus::Invalid && maskResult.status == RasterEditStatus::Invalid &&
               missingImageResult.status == RasterEditStatus::Invalid && !nanResult.layer && !infiniteResult.layer &&
               !hugeResult.layer && !maskResult.layer && !missingImageResult.layer,
           "invalid-nonfinite-or-unrepresentable-offset-mask-target-and-missing-image");
}

struct ProgressCancel final
{
    CancelSource* source = nullptr;
    std::size_t completed = 0;
    std::size_t total = 0;
};

void CancelAfterFirstCompletedTile(void* context, std::size_t completed, std::size_t total) noexcept
{
    auto& state = *static_cast<ProgressCancel*>(context);
    // This fixture lifts one row first, then rebuilds two tiles. Hold cancellation until the first moved tile
    // so this case actually reaches the second stage instead of stopping in the earlier lift row.
    if (completed == 1 && total == 2)
    {
        state.completed = completed;
        state.total = total;
        (void)state.source->Cancel();
    }
}

void PreCancelledDoesNotPublish(int& failures)
{
    constexpr std::array<std::uint8_t, 8> sourceBytes{10, 20, 30, 255, 40, 50, 60, 255};
    const Raster source = Bgra(2, 1, sourceBytes);
    if (!source.HasStorage())
    {
        Expect(failures, false, "cancel-pre.fixture-allocation");
        return;
    }
    RasterEditInput input = Input(Image(source, 2, 1), 2, 1, Rect(0, 0, 1, 1));
    CancelSource cancellation;
    (void)cancellation.Cancel();
    const RasterEditResult commit = RunRasterEdit(input, cancellation.Token());
    const RasterEditPreviewResult preview = RunRasterEditPreview(input, cancellation.Token());
    Expect(failures,
           commit.status == RasterEditStatus::Cancelled && !commit.layer &&
               preview.status == RasterEditStatus::Cancelled && !preview.stroke,
           "cancel-pre-cancelled-commit-and-preview-return-no-owned-result");
}

void ProgressCancellationDoesNotPublish(int& failures)
{
    constexpr int width = 257;
    std::vector<std::uint8_t> opaqueBytes(static_cast<std::size_t>(width) * 4u);
    for (int x = 0; x < width; ++x)
    {
        const std::size_t offset = static_cast<std::size_t>(x) * 4u;
        opaqueBytes[offset] = 12;
        opaqueBytes[offset + 1] = 24;
        opaqueBytes[offset + 2] = 36;
        opaqueBytes[offset + 3] = 255;
    }
    const Raster source = Bgra(width, 1, opaqueBytes);
    if (!source.HasStorage())
    {
        Expect(failures, false, "cancel-progress.fixture-allocation");
        return;
    }
    RasterEditInput input = Input(Image(source, width, 1), width, 1, Rect(0, 0, width, 1));
    CancelSource cancellation;
    ProgressCancel progress{&cancellation};
    input.progress = CancelAfterFirstCompletedTile;
    input.progressContext = &progress;
    const RasterEditResult commit = RunRasterEdit(input, cancellation.Token());
    CancelSource previewCancellation;
    ProgressCancel previewProgress{&previewCancellation};
    input.progressContext = &previewProgress;
    const RasterEditPreviewResult preview = RunRasterEditPreview(input, previewCancellation.Token());
    Expect(failures,
           commit.status == RasterEditStatus::Cancelled && !commit.layer && progress.completed == 1 &&
               progress.total == 2 && cancellation.Token().IsCancelled() &&
               preview.status == RasterEditStatus::Cancelled && !preview.stroke && previewProgress.completed == 1 &&
               previewProgress.total == 2 && previewCancellation.Token().IsCancelled(),
           "cancel-progress-after-first-move-tile-publishes-no-commit-or-preview");
}

void SparseInputStaysUnmaterialized(int& failures)
{
    constexpr std::array<std::uint8_t, 8> baseBytes{1, 2, 3, 255, 4, 5, 6, 255};
    constexpr std::array<std::uint8_t, 4> tileBytes{7, 8, 9, 255};
    const Raster base = Bgra(2, 1, baseBytes);
    const Raster tile = Bgra(1, 1, tileBytes);
    if (!base.HasStorage() || !tile.HasStorage())
    {
        Expect(failures, false, "sparse.fixture-allocation");
        return;
    }
    const Raster sparse = Raster::MakeSparse(4, 1, RasterFormat::Bgra8888, base, 0, 0,
                                             std::vector<RasterTile>{RasterTile{3, 0, tile}}, 0x00);
    if (!sparse.HasStorage() || !sparse.IsSparse())
    {
        Expect(failures, false, "sparse.fixture-construction");
        return;
    }
    ImageLayer sparseLayer = ImageLayer::Blank(L"Sparse pixel move", Vec2{4, 1});
    const Raster sparseThumbnail = Raster::Make(1, 1, RasterFormat::Bgra8888);
    if (!sparseThumbnail.HasStorage())
    {
        Expect(failures, false, "sparse.thumbnail-allocation");
        return;
    }
    sparseLayer.asset = RasterAsset{sparse, sparseThumbnail, L"Sparse pixel move"};
    RasterEditInput input = Input(std::move(sparseLayer), 4, 1, Rect(0, 0, 1, 1));
    input.offset = Vec2{1, 0};
    const RasterEditResult result = RunRasterEdit(input);
    Expect(failures,
           result.status == RasterEditStatus::Ready && result.layer && result.layer->asset && sparse.IsSparse() &&
               !sparse.IsMaterialized() && sparse.Tiles().size() == 1,
           "sparse-source-remains-unmaterialized-and-retains-tile-structure");
}

void LiftRowCancellationDoesNotPublish(int& failures)
{
    // A 4-row lift stops after its first row, before lifted pixels or landing tiles can publish.
    constexpr std::array<std::uint8_t, 16> row{10, 20, 30, 255, 40, 50, 60, 255, 70, 80, 90, 255, 100, 110, 120, 255};
    std::array<std::uint8_t, 64> pixels{};
    for (std::size_t y = 0; y < 4; ++y)
        std::copy(row.begin(), row.end(), pixels.begin() + static_cast<std::ptrdiff_t>(y * row.size()));
    const auto source = Bgra(4, 4, pixels);
    auto input = Input(Image(source, 4, 4), 4, 4, Rect(0, 0, 4, 4));
    CancelSource cancellation;
    ProgressCancel progress{&cancellation};
    input.progressContext = &progress;
    input.progress = [](void* context, std::size_t completed, std::size_t total) noexcept
    {
        auto& state = *static_cast<ProgressCancel*>(context);
        if (completed == 1 && total == 4)
        {
            state.completed = completed;
            state.total = total;
            (void)state.source->Cancel();
        }
    };
    const auto result = RunRasterEdit(input, cancellation.Token());
    Expect(failures,
           result.status == RasterEditStatus::Cancelled && !result.layer && !result.selection &&
               progress.completed == 1 && progress.total == 4 && PixelsEqual(source, 4, 4, pixels),
           "cancel-during-first-lift-row-preserves-source-and-publishes-no-graph");
}

void PreviewCommitParitySupplement(int& failures)
{
    const Raster source = Bgra(4, 1, kFourPixels);
    if (!source.HasStorage())
    {
        Expect(failures, false, "parity.fixture-allocation");
        return;
    }
    RasterEditInput input = Input(Image(source, 4, 1), 4, 1, Rect(1, 0, 2, 1));
    input.offset = Vec2{-1, 0};
    const RasterEditResult commit = RunRasterEdit(input);
    const RasterEditPreviewResult preview = RunRasterEditPreview(input);
    bool equal = false;
    if (commit.status == RasterEditStatus::Ready && commit.layer && commit.layer->asset && preview.stroke)
    {
        const std::optional<BrushStroke::Commit> snapshot = preview.stroke->PaintSnapshot();
        equal = snapshot && snapshot->image.Width() == commit.layer->asset->image.Width() &&
                snapshot->image.Height() == commit.layer->asset->image.Height();
        if (equal)
        {
            const Raster& actual = commit.layer->asset->image;
            const Raster& shown = snapshot.value().image;
            std::vector<std::uint8_t> actualBytes(static_cast<std::size_t>(actual.Width()) * actual.Height() * 4u);
            std::vector<std::uint8_t> shownBytes(actualBytes.size());
            actual.ReadRegion(0, 0, actual.Width(), actual.Height(), actualBytes.data(), actual.Width() * 4, 0);
            shown.ReadRegion(0, 0, shown.Width(), shown.Height(), shownBytes.data(), shown.Width() * 4, 0);
            equal = actualBytes == shownBytes;
        }
    }
    Expect(failures, equal, "supplement.preview-paint-snapshot-matches-commit-numerically");
}

void MemoryIncludesPrivateLiftAndOutlineFloor(int& failures)
{
    const Raster source = Bgra(4, 1, kFourPixels);
    if (!source.HasStorage())
    {
        Expect(failures, false, "memory.fixture-allocation");
        return;
    }
    const ImageLayer layer = Image(source, 4, 1);
    const DocumentSelection selection = Rect(0, 0, 2, 1);
    const std::uint64_t base = RasterEditMemoryBytes(layer, selection, 4, 1, kMaxLayerPixels, false);
    const std::uint64_t move = PixelMoveMemoryBytes(layer, selection, 4, 1, kMaxLayerPixels);
    const std::uint64_t independentFloor =
        base + static_cast<std::uint64_t>(source.PixelCount()) * 8u +
        static_cast<std::uint64_t>(selection.loops.PointCount()) * 2u * sizeof(Vec2) +
        static_cast<std::uint64_t>(selection.loops.size()) * 2u * sizeof(SelectionLoop);
    Expect(failures, base > 0 && move >= independentFloor && move > base,
           "memory-reservation-covers-lift-plane-and-private-translated-outline-capacities");
}

void RoundedZeroIsUnchanged(int& failures)
{
    constexpr std::array<std::uint8_t, 8> sourceBytes{10, 20, 30, 255, 40, 50, 60, 255};
    const Raster source = Bgra(2, 1, sourceBytes);
    if (!source.HasStorage())
    {
        Expect(failures, false, "zero.fixture-allocation");
        return;
    }
    RasterEditInput input = Input(Image(source, 2, 1), 2, 1, Rect(0, 0, 1, 1));
    input.offset = Vec2{0.49, -0.49};
    const RasterEditResult commit = RunRasterEdit(input);
    const RasterEditPreviewResult preview = RunRasterEditPreview(input);
    Expect(failures,
           commit.status == RasterEditStatus::Unchanged && !commit.layer &&
               preview.status == RasterEditStatus::Unchanged && !preview.stroke,
           "zero-rounded-document-offset-is-unchanged-for-commit-and-preview");
}
} // namespace

void Run(int& failures)
{
    HoleAndLanding(failures);
    DuplicatePartialAlpha(failures);
    StretchedPixelGridAndTranslatedOutline(failures);
    PositiveAndNegativeRounding(failures);
    OffCanvasStillMovesOutline(failures);
    ExplicitEmptyAndOutsideSelection(failures);
    InvalidInputs(failures);
    PreCancelledDoesNotPublish(failures);
    ProgressCancellationDoesNotPublish(failures);
    SparseInputStaysUnmaterialized(failures);
    LiftRowCancellationDoesNotPublish(failures);
    PreviewCommitParitySupplement(failures);
    MemoryIncludesPrivateLiftAndOutlineFloor(failures);
    RoundedZeroIsUnchanged(failures);
}
} // namespace PixelMoveWorkTests
