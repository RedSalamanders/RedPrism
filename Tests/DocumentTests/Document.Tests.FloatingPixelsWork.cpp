#include "Document.Tests.FloatingPixelsWork.h"

#include "../Support/Support.Tests.DocumentFixture.h"
#include "Document.h"
#include "Executor.h"
#include "FloatingPixelsWork.h"
#include "RasterEditWork.h"
#include "Selection.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <span>
#include <utility>

namespace
{
int gFailures = 0;
int gAssertions = 0;

void Expect(bool condition, const char* name) noexcept
{
    ++gAssertions;
    std::printf("%s floating-work.%s\n", condition ? "PASS" : "FAIL", name);
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

DocumentSelection Rect(double x, double y, double width, double height)
{
    return DocumentSelection{
        false, SelectionLoops{{Vec2{x, y}, Vec2{x + width, y}, Vec2{x + width, y + height}, Vec2{x, y + height}}}};
}

ImageLayer Layer(const wchar_t* name, const Raster& image)
{
    ImageLayer layer =
        ImageLayer::Blank(name, Vec2{static_cast<double>(image.Width()), static_cast<double>(image.Height())});
    layer.asset = RasterAsset{image, {}, name};
    return layer;
}

CanvasDocument Document(int width, int height, ImageLayer layer, DocumentSelection selection)
{
    DocumentFixture document;
    document.id = Guid::Generate();
    document.width = width;
    document.height = height;
    document.layers.push_back(std::move(layer));
    document.selection = std::move(selection);
    return std::move(document).Seal();
}

bool BytesEqual(const Raster& image, int width, int height, RasterFormat format,
                std::span<const std::uint8_t> expected) noexcept
{
    if (!image.HasStorage() || image.Width() != width || image.Height() != height || image.Format() != format ||
        !image.Data())
        return false;
    const std::size_t channels = format == RasterFormat::Bgra8888 ? 4u : 1u;
    return expected.size() == static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * channels &&
           std::equal(expected.begin(), expected.end(), image.Data());
}

const ImageLayer* Find(const CanvasDocument& document, const Guid& id) noexcept
{
    const auto found = std::find_if(document.layers.begin(), document.layers.end(),
                                    [&](const ImageLayer& layer) { return layer.id == id; });
    return found == document.layers.end() ? nullptr : &*found;
}

bool Open(CanvasDocument source, Guid active, CanvasDocument& opened, TransformEdit& edit)
{
    InlineExecutor executor;
    auto result = PrepareFloatingOpeningPixels(source, active, executor);
    if (result.status != FloatingPixelsStatus::Ready || !result.document || !result.edit)
        return false;
    opened = std::move(result.document.value());
    edit = std::move(result.edit.value());
    return true;
}

void LegacySelectionLiftAndRestore()
{
    // Numeric companion to legacy transformSelectionMovesPixelsAndOutlineAsOneUndo.
    constexpr std::array<std::uint8_t, 16> pixels{10, 20, 30, 255, 40,  50,  60,  255,
                                                  70, 80, 90, 255, 100, 110, 120, 255};
    ImageLayer sourceLayer = Layer(L"source", Bgra(4, 1, pixels));
    const Guid sourceID = sourceLayer.id;
    const Raster sourceImage = sourceLayer.asset.value().image;
    ImageLayer sibling = Layer(L"sibling", Bgra(1, 1, std::array<std::uint8_t, 4>{3, 4, 5, 255}));
    const Guid siblingID = sibling.id;
    CanvasDocument source = Document(4, 1, std::move(sourceLayer), Rect(1, 0, 2, 1));
    EditDocumentFixture(source, [&](DocumentFixture& document) { document.layers.push_back(std::move(sibling)); });
    const CanvasDocument before = source;

    InlineExecutor executor;
    auto opening = PrepareFloatingOpeningPixels(source, sourceID, executor);
    Expect(opening.status == FloatingPixelsStatus::Ready && opening.document && opening.edit,
           "legacy.selection-lift-ready");
    if (!opening.document || !opening.edit)
        return;
    const CanvasDocument& floatingDoc = opening.document.value();
    const TransformEdit& edit = opening.edit.value();
    const ImageLayer* cleared = Find(floatingDoc, sourceID);
    const ImageLayer* floating = Find(floatingDoc, edit.layerID);
    const ImageLayer* keptSibling = Find(floatingDoc, siblingID);
    constexpr std::array<std::uint8_t, 8> selected{40, 50, 60, 255, 70, 80, 90, 255};
    constexpr std::array<std::uint8_t, 16> hole{10, 20, 30, 255, 0, 0, 0, 0, 0, 0, 0, 0, 100, 110, 120, 255};
    Expect(edit.floating && edit.floating->before && *edit.floating->before == before &&
               edit.floating->sourceID == sourceID && edit.floating->beforeActive == sourceID &&
               edit.floating->pixelWidth == 2 && edit.floating->pixelHeight == 1,
           "legacy.selection-opening-restore-identity-origin");
    Expect(floatingDoc.layers.size() == 3 && cleared && cleared->asset && cleared->asset->image != sourceImage &&
               BytesEqual(cleared->asset->image, 4, 1, RasterFormat::Bgra8888, hole) && floating && floating->asset &&
               floating->transform.origin == Vec2{1, 0} &&
               BytesEqual(floating->asset->image, 2, 1, RasterFormat::Bgra8888, selected) && keptSibling &&
               keptSibling->asset && keptSibling->asset->image == Find(before, siblingID)->asset->image &&
               floatingDoc.selection == source.selection,
           "legacy.selection-hole-copy-sibling-outline-retained");

    auto unchanged = PrepareFloatingMergePixels(floatingDoc, edit, executor);
    Expect(unchanged.status == FloatingPixelsStatus::Unchanged && unchanged.document &&
               unchanged.document.value() == before && unchanged.active == sourceID,
           "legacy.unchanged-draft-restores-exact-opening-document");
    Expect(source == before && sourceImage == Find(source, sourceID)->asset->image,
           "opening-and-restore-do-not-mutate-input-graph");
}

void TranslatedMergeAndNegativeOrigin()
{
    constexpr std::array<std::uint8_t, 16> pixels{10, 20, 30, 255, 40,  50,  60,  255,
                                                  70, 80, 90, 255, 100, 110, 120, 255};
    ImageLayer layer = Layer(L"source", Bgra(4, 1, pixels));
    const Guid sourceID = layer.id;
    CanvasDocument source = Document(8, 1, std::move(layer), Rect(1, 0, 1, 1));
    CanvasDocument opened;
    TransformEdit edit;
    Expect(Open(source, sourceID, opened, edit), "translated.open-prepared");
    if (!edit.floating)
        return;
    InlineExecutor executor;

    edit.draft.origin = Vec2{5, 0};
    auto right = PrepareFloatingMergePixels(opened, edit, executor);
    constexpr std::array<std::uint8_t, 24> rightPixels{10,  20,  30,  255, 0, 0, 0, 0, 70, 80, 90, 255,
                                                       100, 110, 120, 255, 0, 0, 0, 0, 40, 50, 60, 255};
    const ImageLayer* rightLayer =
        right.document && right.active ? Find(right.document.value(), right.active.value()) : nullptr;
    Expect(right.status == FloatingPixelsStatus::Ready && right.document && right.active == sourceID && rightLayer &&
               rightLayer->asset && rightLayer->transform.origin == Vec2{0, 0} &&
               rightLayer->asset->image.Width() == 6 &&
               BytesEqual(rightLayer->asset->image, 6, 1, RasterFormat::Bgra8888, rightPixels),
           "translated.right-merge-expands-source-in-document-coordinates");

    edit.draft.origin = Vec2{-1, 0};
    auto left = PrepareFloatingMergePixels(opened, edit, executor);
    constexpr std::array<std::uint8_t, 20> leftPixels{40, 50, 60, 255, 10, 20,  30,  255, 0,   0,
                                                      0,  0,  70, 80,  90, 255, 100, 110, 120, 255};
    const ImageLayer* leftLayer =
        left.document && left.active ? Find(left.document.value(), left.active.value()) : nullptr;
    Expect(left.status == FloatingPixelsStatus::Ready && leftLayer && leftLayer->asset &&
               leftLayer->asset->image.Width() == 5 && leftLayer->transform.origin == Vec2{-1, 0} &&
               BytesEqual(leftLayer->asset->image, 5, 1, RasterFormat::Bgra8888, leftPixels),
           "translated.negative-origin-grows-left-with-unsigned-safe-indexing");
}

void SourceOverLiteralRounding()
{
    constexpr std::array<std::uint8_t, 8> pixels{50, 25, 10, 128, 100, 80, 60, 255};
    ImageLayer layer = Layer(L"premultiplied", Bgra(2, 1, pixels));
    const Guid sourceID = layer.id;
    CanvasDocument source = Document(4, 1, std::move(layer), Rect(0, 0, 1, 1));
    CanvasDocument opened;
    TransformEdit edit;
    Expect(Open(source, sourceID, opened, edit), "source-over.open-prepared");
    if (!edit.floating)
        return;
    edit.draft.origin = Vec2{1, 0};
    InlineExecutor executor;
    const auto merged = PrepareFloatingMergePixels(opened, edit, executor);
    const ImageLayer* layerAfter =
        merged.document && merged.active ? Find(merged.document.value(), merged.active.value()) : nullptr;
    // Premultiplied BGRA source-over: round(50+100*(1-128/255)), round(25+80*(1-128/255)),
    // round(10+60*(1-128/255)), with alpha remaining 255.
    constexpr std::array<std::uint8_t, 8> expected{0, 0, 0, 0, 100, 65, 40, 255};
    Expect(merged.status == FloatingPixelsStatus::Ready && layerAfter && layerAfter->asset &&
               BytesEqual(layerAfter->asset->image, 2, 1, RasterFormat::Bgra8888, expected),
           "source-over.literal-premultiplied-bgra-lround");
}

void MaskCarryAndGrowth()
{
    constexpr std::array<std::uint8_t, 8> pixels{10, 20, 30, 255, 40, 50, 60, 255};
    constexpr std::array<std::uint8_t, 2> maskValues{0, 127};
    ImageLayer layer = Layer(L"masked", Bgra(2, 1, pixels));
    layer.mask = LayerMask{RasterAsset{Gray(2, 1, maskValues), {}, L"mask"}, true, std::nullopt, true};
    const Guid sourceID = layer.id;
    CanvasDocument source = Document(4, 1, std::move(layer), Rect(0, 0, 1, 1));
    CanvasDocument opened;
    TransformEdit edit;
    Expect(Open(source, sourceID, opened, edit), "mask-growth.open-prepared");
    if (!edit.floating)
        return;
    edit.draft.origin = Vec2{-1, 0};
    InlineExecutor executor;
    auto result = PrepareFloatingMergePixels(opened, edit, executor);
    const ImageLayer* grownLayer =
        result.document && result.active ? Find(result.document.value(), result.active.value()) : nullptr;
    constexpr std::array<std::uint8_t, 3> grownMask{255, 0, 127};
    Expect(result.status == FloatingPixelsStatus::Ready && grownLayer && grownLayer->mask &&
               grownLayer->mask->asset.image.Width() == 3 && grownLayer->mask->asset.image.Height() == 1 &&
               BytesEqual(grownLayer->mask->asset.image, 3, 1, RasterFormat::Gray8, grownMask) &&
               grownLayer->mask->asset.thumbnail.HasStorage() &&
               grownLayer->mask->asset.thumbnail.Format() == RasterFormat::Gray8,
           "mask-growth-new-area-reveals-255-and-thumbnail-is-eager");

    for (const bool uniform : {true, false})
    {
        const std::array<std::uint8_t, 1> uniformValue{73};
        ImageLayer retained = Layer(L"retained-mask", Bgra(2, 1, pixels));
        const Raster mask = uniform ? Gray(1, 1, uniformValue) : Gray(2, 1, maskValues);
        std::optional<LayerTransform> separate;
        if (!uniform)
        {
            LayerTransform placement;
            placement.origin = Vec2{8, 3};
            placement.size = Vec2{4, 2};
            separate = placement;
        }
        retained.mask = LayerMask{RasterAsset{mask, {}, L"retained"}, true, separate, true};
        const Guid retainedID = retained.id;
        CanvasDocument retainedSource = Document(4, 1, std::move(retained), Rect(0, 0, 1, 1));
        CanvasDocument retainedOpened;
        TransformEdit retainedEdit;
        const bool prepared = Open(retainedSource, retainedID, retainedOpened, retainedEdit);
        Expect(prepared, uniform ? "mask.uniform-open-prepared" : "mask.separate-open-prepared");
        if (!prepared || !retainedEdit.floating)
            continue;
        retainedEdit.draft.origin = Vec2{-1, 0};
        const auto retainedResult = PrepareFloatingMergePixels(retainedOpened, retainedEdit, executor);
        const ImageLayer* retainedLayer = retainedResult.document && retainedResult.active
                                              ? Find(retainedResult.document.value(), retainedResult.active.value())
                                              : nullptr;
        Expect(retainedResult.status == FloatingPixelsStatus::Ready && retainedLayer && retainedLayer->mask &&
                   retainedLayer->mask->asset.image == mask && retainedLayer->mask->placement == separate,
               uniform ? "mask.uniform-raster-retained-without-growth"
                       : "mask.separate-placement-retained-without-growth");
    }
}

void OutlineAndCancellation()
{
    constexpr std::array<std::uint8_t, 8> pixels{1, 2, 3, 255, 4, 5, 6, 255};
    ImageLayer layer = Layer(L"outline", Bgra(2, 1, pixels));
    const Guid sourceID = layer.id;
    CanvasDocument source = Document(5, 2, std::move(layer), Rect(0, 0, 1, 1));
    CanvasDocument opened;
    TransformEdit edit;
    Expect(Open(source, sourceID, opened, edit), "outline.open-prepared");
    if (!edit.floating)
        return;
    edit.draft.origin = Vec2{2, 1};
    InlineExecutor executor;
    const auto moved = PrepareFloatingMergePixels(opened, edit, executor);
    const SelectionLoop movedRect{Vec2{2, 1}, Vec2{3, 1}, Vec2{3, 2}, Vec2{2, 2}};
    Expect(moved.status == FloatingPixelsStatus::Ready && moved.document && moved.document->selection &&
               moved.document->selection->loops.size() == 1 && moved.document->selection->loops[0] == movedRect,
           "outline.affine-transform-moves-known-rectangle-corners");

    edit.corners = std::array<Vec2, 4>{Vec2{1, 0}, Vec2{3, 0}, Vec2{3, 2}, Vec2{1, 2}};
    const auto distorted = PrepareFloatingMergePixels(opened, edit, executor);
    const SelectionLoop distortedRect{Vec2{1, 0}, Vec2{3, 0}, Vec2{3, 2}, Vec2{1, 2}};
    Expect(distorted.status == FloatingPixelsStatus::Ready && distorted.document && distorted.document->selection &&
               distorted.document->selection->loops.size() == 1 &&
               distorted.document->selection->loops[0] == distortedRect,
           "outline.distort-homography-maps-known-corners");

    CancelSource cancellation;
    static_cast<void>(cancellation.Cancel());
    const auto cancelledOpen = PrepareFloatingOpeningPixels(source, sourceID, executor, cancellation.Token());
    const auto cancelledMerge = PrepareFloatingMergePixels(opened, edit, executor, cancellation.Token());
    Expect(cancelledOpen.status == FloatingPixelsStatus::Cancelled && !cancelledOpen.document && !cancelledOpen.edit &&
               cancelledMerge.status == FloatingPixelsStatus::Cancelled && !cancelledMerge.document &&
               !cancelledMerge.active,
           "cancelled-preparations-publish-no-output");
}

void RefusalsAndInvalidDistortion()
{
    InlineExecutor executor;
    const std::array<std::uint8_t, 4> pixel{0, 0, 0, 255};
    for (int kind = 0; kind < 4; ++kind)
    {
        ImageLayer layer = Layer(L"candidate", Bgra(1, 1, pixel));
        if (kind == 1)
            layer.asset.reset();
        else if (kind == 2)
        {
            layer.isGroup = true;
            layer.asset.reset();
        }
        else if (kind == 3)
        {
            layer.asset.reset();
            layer.adjustment = LayerAdjustment{};
        }
        const Guid active = layer.id;
        auto selection = kind == 0 ? DocumentSelection{false, SelectionLoops{}} : Rect(0, 0, 1, 1);
        const auto result = PrepareFloatingOpeningPixels(Document(1, 1, std::move(layer), selection), active, executor);
        const char* names[]{"explicit-empty-selection-refused", "no-asset-refused", "group-refused",
                            "adjustment-refused"};
        Expect(result.status == FloatingPixelsStatus::Invalid && !result.document && !result.edit, names[kind]);
    }

    ImageLayer layer = Layer(
        L"distort", Bgra(2, 2, std::array<std::uint8_t, 16>{0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0, 255}));
    const Guid sourceID = layer.id;
    CanvasDocument source = Document(4, 4, std::move(layer), Rect(0, 0, 1, 1));
    CanvasDocument opened;
    TransformEdit edit;
    const bool didOpen = Open(source, sourceID, opened, edit);
    Expect(didOpen, "invalid-distort.open-prepared");
    if (!didOpen)
        return;
    edit.corners = std::array<Vec2, 4>{Vec2{0, 0}, Vec2{2, 2}, Vec2{0, 2}, Vec2{2, 0}}; // bow-tie
    const auto bad = PrepareFloatingMergePixels(opened, edit, executor);
    Expect(bad.status == FloatingPixelsStatus::Invalid && !bad.document && !bad.active,
           "invalid-distort-bowtie-has-no-output-document");
}

void MemoryPlanningAndSparseRefusal()
{
    const Raster sparse = Raster::MakeSparse(4000, 4000, RasterFormat::Bgra8888, Raster{}, 0, 0, {}, 0);
    ImageLayer large = Layer(L"virtual", sparse);
    const Guid active = large.id;
    const CanvasDocument source = Document(4000, 4000, std::move(large), Rect(0, 0, 1, 1));
    const std::uint64_t openingBytes = FloatingOpeningPixelsMemoryBytes(source, active);
    Expect(openingBytes >= 4000ull * 4000ull * 4ull && openingBytes < 4ull * 1024ull * 1024ull * 1024ull &&
               !sparse.IsMaterialized(),
           "planner.opening-covers-canvas-peak-without-materializing-sparse-source");

    constexpr std::array<std::uint8_t, 4> onePixel{1, 2, 3, 255};
    ImageLayer target = Layer(
        L"target", Bgra(2, 2, std::array<std::uint8_t, 16>{1, 2, 3, 255, 1, 2, 3, 255, 1, 2, 3, 255, 1, 2, 3, 255}));
    const Guid targetID = target.id;
    ImageLayer budget =
        Layer(L"budget", Raster::MakeSparse(9999, 9999, RasterFormat::Bgra8888, Bgra(1, 1, onePixel), 0, 0, {}, 0));
    // The huge sibling is virtual. A 512 canvas also proves temporary 256-square Clear patches cannot be
    // charged as committed layer growth; selection coverage itself remains a cheap 256 KiB plane.
    CanvasDocument budgetSource = Document(512, 512, std::move(target), Rect(0, 0, 1, 1));
    EditDocumentFixture(budgetSource, [&](DocumentFixture& document) { document.layers.push_back(std::move(budget)); });
    auto smallSource = budgetSource;
    smallSource.width = smallSource.height = 8;
    CanvasDocument smallOpened;
    TransformEdit smallEdit;
    Expect(Open(smallSource, targetID, smallOpened, smallEdit), "planner.small-canvas-budget-case-open-prepared");
    CanvasDocument opened;
    TransformEdit edit;
    const bool didOpen = Open(budgetSource, targetID, opened, edit);
    Expect(didOpen, "planner.budget-case-open-prepared");
    if (!didOpen || !edit.floating)
        return;
    const auto* cleared = Find(opened, targetID);
    constexpr std::array<std::uint8_t, 16> hole{0, 0, 0, 0, 1, 2, 3, 255, 1, 2, 3, 255, 1, 2, 3, 255};
    Expect(cleared && cleared->asset && BytesEqual(cleared->asset->image, 2, 2, RasterFormat::Bgra8888, hole) &&
               !budgetSource.layers.back().asset->image.IsMaterialized(),
           "planner.clear-retains-exact-source-grid-and-virtual-sibling");
    edit.draft.origin = Vec2{150, 150};
    const Raster sparseBudget = Find(opened, budgetSource.layers.back().id)->asset->image;
    const std::uint64_t planned = FloatingMergePixelsMemoryBytes(opened, edit);
    InlineExecutor executor;
    const auto result = PrepareFloatingMergePixels(opened, edit, executor);
    Expect(planned > 0 && result.status == FloatingPixelsStatus::TooLarge && result.budgetRefused && !result.document &&
               !result.active && !sparseBudget.IsMaterialized(),
           "planner.merge-budget-refusal-keeps-virtual-sparse-sibling");
    RasterEditInput refused;
    refused.layer = budgetSource.layers.front();
    refused.selection = budgetSource.selection;
    refused.canvasWidth = budgetSource.width;
    refused.canvasHeight = budgetSource.height;
    refused.pixelLimit = 3; // The retained 2x2 committed source itself cannot fit this explicit caller limit.
    refused.kind = RasterEditKind::Clear;
    const auto tooSmall = RunRasterEdit(refused);
    Expect(tooSmall.status == RasterEditStatus::TooLarge && !tooSmall.layer,
           "planner.clear-still-refuses-committed-source-beyond-caller-budget");
}

void ClearAcrossTileBoundary()
{
    constexpr std::array<std::uint8_t, 16> pixels{10, 20, 30, 255, 40,  50,  60,  255,
                                                  70, 80, 90, 255, 100, 110, 120, 255};
    auto layer = Layer(L"offset", Bgra(2, 2, pixels));
    layer.transform.origin = {255, 255};
    const auto original = layer.asset->image;
    RasterEditInput input;
    input.layer = layer;
    input.selection = Rect(255, 255, 2, 1);
    input.canvasWidth = input.canvasHeight = 512;
    input.pixelLimit = 4;
    input.kind = RasterEditKind::Clear;
    const auto result = RunRasterEdit(input);
    constexpr std::array<std::uint8_t, 16> expected{0, 0, 0, 0, 0, 0, 0, 0, 70, 80, 90, 255, 100, 110, 120, 255};
    Expect(result.status == RasterEditStatus::Ready && result.layer && result.layer->asset &&
               result.layer->transform == layer.transform &&
               BytesEqual(result.layer->asset->image, 2, 2, RasterFormat::Bgra8888, expected),
           "clear-offset-source-crosses-tile-boundary-with-exact-committed-budget");
    Expect(BytesEqual(original, 2, 2, RasterFormat::Bgra8888, pixels),
           "clear-offset-source-leaves-opening-bytes-immutable");
}
} // namespace

int RunFloatingPixelsWorkTests()
{
    gFailures = 0;
    gAssertions = 0;
    LegacySelectionLiftAndRestore();
    TranslatedMergeAndNegativeOrigin();
    SourceOverLiteralRounding();
    MaskCarryAndGrowth();
    OutlineAndCancellation();
    RefusalsAndInvalidDistortion();
    MemoryPlanningAndSparseRefusal();
    ClearAcrossTileBoundary();
    std::printf("floating-work: %d assertions, %d failures\n", gAssertions, gFailures);
    return gFailures == 0 ? 0 : 1;
}
