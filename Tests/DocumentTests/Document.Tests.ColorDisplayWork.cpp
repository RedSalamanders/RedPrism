#include "Document.Tests.ColorDisplayWork.h"

#include "../../Common/ColorDisplayWork.h"
#include "../Support/Support.Tests.DocumentFixture.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <new>
#include <utility>
#include <vector>

namespace
{
int gFailures = 0;
int gChecks = 0;

void Expect(bool condition, const char* name) noexcept
{
    ++gChecks;
    std::printf("%s color-display-work.%s\n", condition ? "PASS" : "FAIL", name);
    if (!condition)
        ++gFailures;
}

Raster Pixels(int width = 2, int height = 2, RasterFormat format = RasterFormat::Bgra8888)
{
    const int channels = format == RasterFormat::Gray8 ? 1 : 4;
    std::vector<std::uint8_t> bytes(
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * static_cast<std::size_t>(channels), 37);
    return Raster::Make(width, height, format, bytes.data(), bytes.size());
}

LayerTransform Placement(double x, double y, double width = 2, double height = 2) noexcept
{
    LayerTransform result;
    result.origin = {x, y};
    result.size = {width, height};
    result.sampling = LayerSampling::Nearest;
    return result;
}

ImageLayer Image(Guid id, std::wstring name, Raster raster)
{
    ImageLayer layer =
        ImageLayer::Blank(std::move(name), {static_cast<double>(raster.Width()), static_cast<double>(raster.Height())});
    layer.id = id;
    layer.asset = RasterAsset{raster, Pixels(1, 1), L"retained asset"};
    layer.transform = Placement(3, 4, static_cast<double>(raster.Width()), static_cast<double>(raster.Height()));
    layer.opacity = 0.625;
    layer.blendMode = LayerBlendMode::Multiply;
    return layer;
}

CanvasDocument Document(std::vector<ImageLayer> layers, std::optional<DocumentSelection> selection = std::nullopt)
{
    DocumentFixture fixture;
    fixture.id = Guid::Generate();
    fixture.width = 64;
    fixture.height = 48;
    fixture.resolution = 144;
    fixture.layers = std::move(layers);
    fixture.selection = std::move(selection);
    return std::move(fixture).Seal();
}

ColorDisplayLayer Preview(const ImageLayer& layer, Raster raster, LayerTransform transform)
{
    return ColorDisplayLayer{layer.id, std::move(raster), transform};
}

ColorDisplayResult Prepare(const CanvasDocument& source, const ImageLayer& target, Raster raster,
                           LayerTransform transform, CancelToken cancellation = {}, ColorDisplayObserver observer = {})
{
    return PrepareColorDisplay(source, Preview(target, std::move(raster), transform), cancellation, observer);
}

struct CancelAt final
{
    CancelSource* source = nullptr;
    ColorDisplayStage stage = ColorDisplayStage::Validation;
    std::size_t stageCalls = 0;
    std::size_t cancelOnCall = 1;
};

void CancelCheckpoint(void* context, ColorDisplayStage stage)
{
    auto& state = *static_cast<CancelAt*>(context);
    if (stage == state.stage && ++state.stageCalls == state.cancelOnCall)
        (void)state.source->Cancel();
}

struct ThrowAt final
{
    ColorDisplayStage stage = ColorDisplayStage::Draft;
    bool thrown = false;
};

void ThrowCheckpoint(void* context, ColorDisplayStage stage)
{
    auto& state = *static_cast<ThrowAt*>(context);
    if (stage == state.stage && !state.thrown)
    {
        state.thrown = true;
        throw std::bad_alloc{};
    }
}

void SuccessfulCopyAndSharing()
{
    const Guid targetId = Guid::Generate();
    const Guid otherId = Guid::Generate();
    const Raster sourcePixels = Pixels();
    const Raster otherSparse = Raster::MakeSparse(13, 9, RasterFormat::Bgra8888, Pixels(2, 2), 1, 2, {}, 19);
    ImageLayer target = Image(targetId, L"Target name remains", sourcePixels);
    target.asset.value().name = L"Target asset name remains";
    target.asset.value().thumbnail = Pixels(1, 1);
    target.shape = LayerShapeStyle{ShapeKind::Ellipse, 0.2, 0.4, 0.6, 0.75};
    target.shapeImage = sourcePixels;
    target.visible = false;
    ImageLayer other = Image(otherId, L"Other remains", otherSparse);
    const DocumentSelection selection{true, SelectionLoops{{Vec2{2, 3}, Vec2{8, 3}, Vec2{8, 9}, Vec2{2, 9}}}};
    const CanvasDocument source = Document({target, other}, selection);
    const auto originalBound = source.layers.OwnershipBounds();
    const Raster rendered = Pixels(3, 5);
    LayerTransform changed = Placement(17, 21, 6, 10);
    changed.rotation = 15;
    changed.flipX = true;
    changed.sampling = LayerSampling::Smooth;
    const ColorDisplayResult result = Prepare(source, target, rendered, changed);

    Expect(ValidateDocument(source) == DocumentStatus::Ok, "opening-fixture-validates");
    Expect(result.status == ColorDisplayStatus::Ready && result.document.has_value(), "valid-preview-prepares");
    if (!result.document)
        return;
    const auto& display = result.document.value();
    const ImageLayer& shown = display.layers[0];
    Expect(display.layers.size() == source.layers.size() && display.id == source.id && display.width == source.width &&
               display.height == source.height && display.resolution == source.resolution,
           "record-count-order-and-document-metadata-retained");
    Expect(shown.id == target.id && shown.asset.value().image == rendered && shown.transform == changed,
           "only-requested-image-and-transform-replaced");
    Expect(shown.asset.value().thumbnail == target.asset.value().thumbnail &&
               shown.asset.value().name == L"Target asset name remains",
           "asset-thumbnail-and-name-retained");
    Expect(shown.name == L"Target name remains" && shown.shape == target.shape && shown.shapeImage == target.shapeImage,
           "layer-name-shape-and-shape-image-retained");
    Expect(shown.visible == target.visible && shown.parentID == target.parentID && shown.opacity == target.opacity &&
               shown.blendMode == target.blendMode && shown.maskSourceID == target.maskSourceID &&
               shown.adjustment == target.adjustment,
           "explicit-layer-fields-retained-despite-equality-omissions");
    Expect(display.layers[1] == source.layers[1] && display.layers[1].asset.value().name == L"retained asset",
           "unmodified-layer-and-asset-name-retained");
    Expect(display.layers[1].asset.value().image == source.layers[1].asset.value().image &&
               display.layers[1].asset.value().image.IsSparse() &&
               !display.layers[1].asset.value().image.IsMaterialized(),
           "other-sparse-raster-shared-without-assembly");
    Expect(display.selection == source.selection &&
               display.selection.value().loops.Identity() == source.selection.value().loops.Identity(),
           "selection-value-and-loop-storage-shared");
    Expect(source.layers[0].asset.value().image == sourcePixels && source.layers.OwnershipBounds() == originalBound,
           "published-display-does-not-mutate-source-root");
    Expect(ValidateDocument(display) == DocumentStatus::Ok, "prepared-display-validates");
}

void MaskPlacementCases()
{
    const std::array<bool, 2> linkedValues{false, true};
    for (const bool linked : linkedValues)
    {
        ImageLayer target = Image(Guid::Generate(), linked ? L"linked" : L"unlinked", Pixels());
        const Raster mask = Pixels(2, 2, RasterFormat::Gray8);
        target.mask = LayerMask{RasterAsset{mask, {}, L"mask stays"}, true, std::nullopt, linked};
        const CanvasDocument source = Document({target});
        const LayerTransform movedGrown = Placement(11, 13, 4, 6);
        const auto moved = Prepare(source, target, Pixels(4, 6), movedGrown);
        const bool hasMoved = moved.document && moved.document.value().layers[0].mask.value().placement.has_value();
        Expect(moved.status == ColorDisplayStatus::Ready && hasMoved,
               linked ? "linked-mask-moved-placement-recorded" : "unlinked-mask-moved-placement-recorded");
        if (hasMoved)
            Expect(moved.document.value().layers[0].mask.value().placement.value() == target.transform,
                   linked ? "linked-mask-pins-original-placement" : "unlinked-mask-pins-original-placement");
        Expect(moved.document && moved.document.value().layers[0].mask.value().linked == linked &&
                   moved.document.value().layers[0].mask.value().asset.name == L"mask stays" &&
                   moved.document.value().layers[0].mask.value().asset.image == mask,
               linked ? "linked-mask-properties-retained" : "unlinked-mask-properties-retained");

        const LayerTransform downsampledTransform = Placement(19, -5, 1, 1);
        const auto downsampled = Prepare(source, target, Pixels(1, 1), downsampledTransform);
        Expect(downsampled.status == ColorDisplayStatus::Ready && downsampled.document &&
                   downsampled.document.value().layers[0].mask.value().placement == target.transform,
               linked ? "linked-downsample-pins-original-mask-placement"
                      : "unlinked-downsample-pins-original-mask-placement");

        const auto samePlacement = Prepare(source, target, Pixels(), target.transform);
        Expect(samePlacement.status == ColorDisplayStatus::Ready && samePlacement.document &&
                   !samePlacement.document.value().layers[0].mask.value().placement,
               linked ? "linked-unchanged-transform-keeps-implicit-mask"
                      : "unlinked-unchanged-transform-keeps-implicit-mask");

        LayerTransform explicitMaskPlacement = Placement(-7, 29, 2, 2);
        target.mask.value().placement = explicitMaskPlacement;
        const CanvasDocument explicitSource = Document({target});
        const auto explicitResult = Prepare(explicitSource, target, Pixels(3, 3), movedGrown);
        Expect(explicitResult.status == ColorDisplayStatus::Ready && explicitResult.document &&
                   explicitResult.document.value().layers[0].mask.value().placement == explicitMaskPlacement,
               linked ? "linked-explicit-placement-retained" : "unlinked-explicit-placement-retained");
    }
}

void RejectionsAndLargeSparseBudget()
{
    ImageLayer target = Image(Guid::Generate(), L"Valid target", Pixels());
    const CanvasDocument valid = Document({target});
    const LayerTransform moved = Placement(8, 9, 3, 3);
    Expect(Prepare(valid, Image(Guid::Generate(), L"missing", Pixels()), Pixels(3, 3), moved).status ==
               ColorDisplayStatus::Invalid,
           "missing-target-rejected");

    ImageLayer blank = ImageLayer::Blank(L"Blank", {2, 2});
    Expect(Prepare(Document({blank}), blank, Pixels(), blank.transform).status == ColorDisplayStatus::Invalid,
           "blank-target-rejected");
    ImageLayer group = ImageLayer::Blank(L"Group", {2, 2});
    group.isGroup = true;
    Expect(Prepare(Document({group}), group, Pixels(), moved).status == ColorDisplayStatus::Invalid,
           "group-target-rejected");
    ImageLayer adjustment = ImageLayer::Blank(L"Adjustment", {2, 2});
    adjustment.adjustment.emplace();
    adjustment.adjustment.value().kind = AdjustmentKind::Curves;
    Expect(Prepare(Document({adjustment}), adjustment, Pixels(), moved).status == ColorDisplayStatus::Invalid,
           "adjustment-target-rejected");

    ImageLayer invalidSource = target;
    invalidSource.transform.size.x = 0;
    Expect(Prepare(Document({invalidSource}), invalidSource, Pixels(), moved).status == ColorDisplayStatus::Invalid,
           "invalid-source-transform-rejected");
    Expect(Prepare(valid, target, Raster{}, moved).status == ColorDisplayStatus::Invalid,
           "storage-less-preview-rejected");
    Expect(Prepare(valid, target, Pixels(2, 2, RasterFormat::Gray8), moved).status == ColorDisplayStatus::Invalid,
           "non-bgra-preview-rejected");
    LayerTransform invalidPreview = moved;
    invalidPreview.rotation = std::numeric_limits<double>::infinity();
    Expect(Prepare(valid, target, Pixels(3, 3), invalidPreview).status == ColorDisplayStatus::Invalid,
           "non-finite-preview-transform-rejected");
    ImageLayer graySource = Image(target.id, L"Gray source", Pixels(2, 2, RasterFormat::Gray8));
    Expect(Prepare(Document({graySource}), graySource, Pixels(), graySource.transform).status ==
               ColorDisplayStatus::Invalid,
           "non-bgra-source-target-rejected");

    const Raster virtualLarge = Raster::MakeSparse(30000, 3333, RasterFormat::Bgra8888, {}, 0, 0, {}, 0);
    ImageLayer large = Image(Guid::Generate(), L"Virtual large", virtualLarge);
    const CanvasDocument nearLimit = Document({large, target});
    const Raster boundaryPreview = Pixels(100, 100);
    const Raster modestPreview = Pixels(101, 100);
    Expect(virtualLarge.PixelCount() == 99'990'000 && !virtualLarge.IsMaterialized(),
           "budget-fixture-uses-virtual-sparse-large-raster");
    Expect(ValidateDocument(nearLimit) == DocumentStatus::Ok, "near-limit-opening-is-valid");
    Expect(Prepare(nearLimit, target, boundaryPreview, target.transform).status == ColorDisplayStatus::Ready,
           "preview-at-exact-combined-pixel-limit-is-accepted");
    Expect(Prepare(nearLimit, target, modestPreview, target.transform).status == ColorDisplayStatus::Invalid,
           "preview-rejected-when-combined-source-pixels-exceed-limit");
    Expect(!virtualLarge.IsMaterialized(), "budget-rejection-does-not-assemble-large-source");
}

void CachedPlanningEnvelope()
{
    const Guid id = Guid::Generate();
    const ImageLayer target = Image(id, L"Plan", Pixels());
    const CanvasDocument source = Document({target});
    const Raster previewPixels = Pixels(3, 4);
    const ColorDisplayLayer preview = Preview(target, previewPixels, target.transform);
    const auto bounds = source.layers.OwnershipBounds();
    const auto selectionBytes = source.selection ? source.selection.value().loops.StorageBytes() : 0;
    const auto planned = ColorDisplayMemoryBytes(source, preview);
    Expect(bounds.has_value() && planned.has_value(), "cached-plan-has-published-bound");
    if (bounds && planned)
    {
        const std::uint64_t expectedAdditional = 65536 + 3 * bounds.value().metadataBytes + 512;
        const std::uint64_t expectedRetirement =
            2 * bounds.value().ownershipBytes + previewPixels.RetirementMemoryBytes() + 2 * selectionBytes + 256;
        const std::uint64_t expectedNodes = 2 * bounds.value().rasterNodes + previewPixels.RetirementNodeCount();
        Expect(planned.value().additionalBytes == expectedAdditional, "additional-envelope-matches-literal-components");
        Expect(planned.value().retirementBytes == expectedRetirement, "retirement-envelope-matches-literal-components");
        Expect(planned.value().retirementRasterNodes == expectedNodes, "node-envelope-matches-literal-components");
    }
    const auto withSelection = ColorDisplayMemoryFromBounds({100, 1000, 4}, 2, 24, 3, 40, 2);
    Expect(withSelection && withSelection.value() == ColorDisplayMemory{66860, 2616, 11},
           "selection-and-loop-envelope-counted-literally");
    Expect(!ColorDisplayMemoryFromBounds(bounds.value(), 0, 1, 1), "zero-record-plan-refused");
    Expect(!ColorDisplayMemoryFromBounds(bounds.value(), static_cast<std::uint64_t>(kMaxLayerCount) + 1, 1, 1),
           "excess-record-plan-refused");
    Expect(!ColorDisplayMemoryFromBounds(RecordOwnershipBounds{}, 1, 1, 1), "zero-metadata-bound-refused");
    Expect(!ColorDisplayMemoryFromBounds(RecordOwnershipBounds{10, 9, 1}, 1, 1, 1),
           "ownership-smaller-than-metadata-refused");
    Expect(!ColorDisplayMemoryFromBounds(bounds.value(), 1, 0, 1), "zero-preview-bytes-refused");
    Expect(!ColorDisplayMemoryFromBounds(bounds.value(), 1, 1, 0), "zero-preview-nodes-refused");

    constexpr std::uint64_t max = (std::numeric_limits<std::uint64_t>::max)();
    Expect(!ColorDisplayMemoryFromBounds({max / 3 + 1, max / 3 + 1, 0}, 1, 1, 1),
           "metadata-times-three-overflow-refused");
    Expect(!ColorDisplayMemoryFromBounds({1, max / 2 + 1, 0}, 1, 1, 1), "ownership-times-two-overflow-refused");
    Expect(!ColorDisplayMemoryFromBounds(bounds.value(), 1, max, 1), "preview-addition-overflow-refused");
    Expect(!ColorDisplayMemoryFromBounds(bounds.value(), 1, 1, 1, max / 2 + 1, 0),
           "selection-times-two-overflow-refused");
    Expect(!ColorDisplayMemoryFromBounds(bounds.value(), 1, 1, 1, 0, max / 128 + 1),
           "selection-loop-times-128-overflow-refused");
    Expect(!ColorDisplayMemoryFromBounds({1, 1, max / 2 + 1}, 1, 1, 1), "raster-node-times-two-overflow-refused");
    Expect(!ColorDisplayMemoryFromBounds(bounds.value(), 1, 1, max), "preview-node-addition-overflow-refused");

    LayerTransform invalid = target.transform;
    invalid.size.y = 0;
    Expect(!ColorDisplayMemoryBytes(source, Preview(target, previewPixels, invalid)),
           "cached-plan-rejects-invalid-preview-transform");
    Expect(!ColorDisplayMemoryBytes(source, Preview(target, Raster{}, target.transform)),
           "cached-plan-rejects-missing-preview-storage");

    Raster deep = Pixels(1, 1);
    for (int depth = 0; depth < 130; ++depth)
        deep = Raster::MakeSparse(1, 1, RasterFormat::Bgra8888, {}, 0, 0, {RasterTile{0, 0, deep}}, 0);
    ImageLayer unmeasurable = Image(id, L"Unmeasurable", deep);
    const CanvasDocument noBound = Document({unmeasurable});
    Expect(!noBound.layers.OwnershipBounds(), "deep-raster-record-bound-unavailable");
    Expect(!ColorDisplayMemoryBytes(noBound, Preview(unmeasurable, previewPixels, unmeasurable.transform)),
           "cached-plan-refuses-unavailable-bound");
    Expect(!deep.IsMaterialized(), "cached-plan-never-assembles-sparse-preview-source");

    const Raster sparse = Raster::MakeSparse(20, 10, RasterFormat::Bgra8888, Pixels(2, 2), 4, 3, {}, 0);
    ImageLayer sparseLayer = Image(Guid::Generate(), L"Sparse", sparse);
    const CanvasDocument sparseDocument = Document({sparseLayer});
    const auto before = sparseDocument.layers.OwnershipBounds();
    const auto first =
        ColorDisplayMemoryBytes(sparseDocument, Preview(sparseLayer, previewPixels, sparseLayer.transform));
    const auto second =
        ColorDisplayMemoryBytes(sparseDocument, Preview(sparseLayer, previewPixels, sparseLayer.transform));
    Expect(first && second && first == second && sparseDocument.layers.OwnershipBounds() == before,
           "repeated-sparse-plans-stay-identical-and-cached");
    Expect(!sparse.IsMaterialized(), "repeated-sparse-plans-do-not-materialize-source");
}

void CancellationAndThrowingObservers()
{
    ImageLayer first = Image(Guid::Generate(), L"First", Pixels());
    ImageLayer second = Image(Guid::Generate(), L"Second", Pixels());
    ImageLayer third = Image(Guid::Generate(), L"Third", Pixels());
    const CanvasDocument source = Document({first, second, third});
    const auto originalBound = source.layers.OwnershipBounds();
    const Raster rendered = Pixels(3, 3);
    const LayerTransform moved = Placement(10, 12, 3, 3);
    const std::array<ColorDisplayStage, 5> stages{ColorDisplayStage::Validation, ColorDisplayStage::Draft,
                                                  ColorDisplayStage::Layer, ColorDisplayStage::Seal,
                                                  ColorDisplayStage::Publication};
    for (const ColorDisplayStage stage : stages)
    {
        CancelSource cancellation;
        CancelAt state{&cancellation, stage};
        const ColorDisplayObserver observer{&state, CancelCheckpoint};
        const auto stopped =
            PrepareColorDisplay(source, Preview(first, rendered, moved), cancellation.Token(), observer);
        const bool sourceUnchanged = source.layers.OwnershipBounds() == originalBound &&
                                     source.layers[0].asset.value().image == first.asset.value().image;
        const auto retry = PrepareColorDisplay(source, Preview(first, rendered, moved));
        Expect(stopped.status == ColorDisplayStatus::Cancelled && !stopped.document && sourceUnchanged &&
                   retry.status == ColorDisplayStatus::Ready && retry.document,
               stage == ColorDisplayStage::Validation ? "validation-cancel-is-atomic-and-retryable"
               : stage == ColorDisplayStage::Draft    ? "draft-cancel-is-atomic-and-retryable"
               : stage == ColorDisplayStage::Layer    ? "layer-cancel-is-atomic-and-retryable"
               : stage == ColorDisplayStage::Seal     ? "seal-cancel-is-atomic-and-retryable"
                                                      : "publication-cancel-is-atomic-and-retryable");
    }
    CancelSource preCancelled;
    (void)preCancelled.Cancel();
    CancelAt preState{&preCancelled, ColorDisplayStage::Validation};
    const auto pre = PrepareColorDisplay(source, Preview(first, rendered, moved), preCancelled.Token(),
                                         ColorDisplayObserver{&preState, CancelCheckpoint});
    Expect(pre.status == ColorDisplayStatus::Cancelled && !pre.document && preState.stageCalls == 0,
           "pre-cancel-stops-before-observer");

    CancelSource perLayer;
    CancelAt layerState{&perLayer, ColorDisplayStage::Layer, 0, 2};
    const auto secondLayerStop = PrepareColorDisplay(source, Preview(third, rendered, moved), perLayer.Token(),
                                                     ColorDisplayObserver{&layerState, CancelCheckpoint});
    Expect(layerState.stageCalls == 2 && secondLayerStop.status == ColorDisplayStatus::Cancelled &&
               !secondLayerStop.document,
           "layer-checkpoint-runs-on-each-record-and-stops-at-second");

    const std::array<ColorDisplayStage, 4> throwingStages{ColorDisplayStage::Draft, ColorDisplayStage::Layer,
                                                          ColorDisplayStage::Seal, ColorDisplayStage::Publication};
    for (const ColorDisplayStage stage : throwingStages)
    {
        ThrowAt state{stage};
        bool propagated = false;
        try
        {
            (void)PrepareColorDisplay(source, Preview(first, rendered, moved), {},
                                      ColorDisplayObserver{&state, ThrowCheckpoint});
        }
        catch (const std::bad_alloc&)
        {
            propagated = true;
        }
        const bool sourceUnchanged = source.layers.OwnershipBounds() == originalBound &&
                                     source.layers[0].transform == first.transform &&
                                     source.layers[0].asset.value().image == first.asset.value().image;
        const auto retry = PrepareColorDisplay(source, Preview(first, rendered, moved));
        Expect(propagated && state.thrown && sourceUnchanged && retry.status == ColorDisplayStatus::Ready &&
                   retry.document,
               stage == ColorDisplayStage::Draft   ? "draft-bad-alloc-propagates-atomic-and-retryable"
               : stage == ColorDisplayStage::Layer ? "layer-bad-alloc-propagates-atomic-and-retryable"
               : stage == ColorDisplayStage::Seal  ? "seal-bad-alloc-propagates-atomic-and-retryable"
                                                   : "publication-bad-alloc-propagates-atomic-and-retryable");
    }
}

void HierarchyAndUniformMask()
{
    ImageLayer folder = ImageLayer::Blank(L"Folder", {64, 48});
    folder.isGroup = true;
    ImageLayer target = Image(Guid::Generate(), L"Masked target", Pixels());
    ImageLayer other = Image(Guid::Generate(), L"Clipping source", Pixels());
    target.parentID = folder.id;
    other.parentID = folder.id;
    target.maskSourceID = other.id;
    const Raster uniformMask = Pixels(1, 1, RasterFormat::Gray8);
    const Raster maskThumbnail = Pixels(1, 1, RasterFormat::Gray8);
    target.mask = LayerMask{RasterAsset{uniformMask, maskThumbnail, L"semantic mask"}, false, std::nullopt, false};
    ImageLayer adjustment = ImageLayer::Blank(L"Unchanged curves", {64, 48});
    adjustment.adjustment.emplace();
    adjustment.adjustment->curves.channels[0][0].y = 19;
    const CanvasDocument source = Document({folder, target, other, adjustment});
    const Raster rendered = Pixels(4, 4);
    const auto changed = Placement(10, 20, 4, 4);
    const auto result = Prepare(source, target, rendered, changed);
    Expect(ValidateDocument(source) == DocumentStatus::Ok && result.status == ColorDisplayStatus::Ready &&
               result.document,
           "nested-and-live-mask-fixture-prepares");
    if (!result.document)
        return;
    const auto& display = result.document.value();
    Expect(display.layers[1].parentID == folder.id && display.layers[1].maskSourceID == other.id &&
               display.layers[0] == folder && display.layers[2] == other,
           "hierarchy-clipping-and-order-retained");
    Expect(display.layers[3].adjustment == adjustment.adjustment &&
               display.layers[3].adjustment->curves.channels[0][0].y == 19,
           "unrelated-adjustment-curve-metadata-retained");
    const auto& mask = display.layers[1].mask.value();
    Expect(mask.asset.image == uniformMask && mask.asset.thumbnail == maskThumbnail && !mask.enabled && !mask.linked &&
               mask.asset.name == L"semantic mask" && mask.placement == target.transform,
           "uniform-disabled-mask-and-semantic-thumbnail-retained");
    Expect(source.layers[1].mask->placement == std::nullopt && source.layers[1].asset->image == target.asset->image,
           "nested-source-remains-unchanged");
}
} // namespace

int RunColorDisplayWorkTests()
{
    gFailures = 0;
    gChecks = 0;
    SuccessfulCopyAndSharing();
    HierarchyAndUniformMask();
    MaskPlacementCases();
    RejectionsAndLargeSparseBudget();
    CachedPlanningEnvelope();
    CancellationAndThrowingObservers();
    std::printf("COLOR DISPLAY WORK TESTS checks=%d failures=%d\n", gChecks, gFailures);
    return gFailures;
}
