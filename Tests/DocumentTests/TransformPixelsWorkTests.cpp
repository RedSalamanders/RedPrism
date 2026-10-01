#include "TransformPixelsWorkTests.h"

#include "Document.h"
#include "Executor.h"
#include "ShapeEditWork.h"
#include "TransformPixelsWork.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <span>
#include <stop_token>
#include <utility>
#include <vector>

namespace
{
int gFailures = 0;
int gAssertions = 0;

void Expect(bool condition, const char* name)
{
    ++gAssertions;
    std::printf("%s transform-pixels.%s\n", condition ? "PASS" : "FAIL", name);
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

constexpr std::array<Vec2, 4> Rect(double left, double top, double right, double bottom)
{
    return {Vec2{left, top}, Vec2{right, top}, Vec2{right, bottom}, Vec2{left, bottom}};
}

ImageLayer Layer(std::wstring name, const Raster& image, LayerTransform transform)
{
    ImageLayer layer = ImageLayer::Blank(std::move(name), transform.size);
    layer.transform = transform;
    layer.asset = RasterAsset{image, Raster::MakeThumbnail(image), layer.name};
    return layer;
}

CanvasDocument Document(std::vector<ImageLayer> layers, int width = 32, int height = 24)
{
    CanvasDocument document;
    document.id = Guid::Generate();
    document.width = width;
    document.height = height;
    document.layers = std::move(layers);
    document.selection =
        DocumentSelection{true, SelectionLoops{SelectionLoop{Vec2{1, 1}, Vec2{6, 1}, Vec2{6, 5}, Vec2{1, 5}}}};
    return document;
}

bool PixelsEqual(const Raster& actual, std::span<const std::uint8_t> expected, int width, int height)
{
    if (!actual.HasStorage() || actual.Width() != width || actual.Height() != height ||
        actual.Format() != RasterFormat::Bgra8888 || actual.Data() == nullptr ||
        expected.size() != static_cast<std::size_t>(width) * height * 4u)
        return false;
    for (int y = 0; y < height; ++y)
    {
        const auto* row = actual.Data() + static_cast<std::size_t>(y) * actual.Stride();
        const auto expectedOffset = static_cast<std::size_t>(y) * width * 4u;
        if (!std::equal(expected.begin() + static_cast<std::ptrdiff_t>(expectedOffset),
                        expected.begin() + static_cast<std::ptrdiff_t>(expectedOffset + width * 4u), row))
            return false;
    }
    return expected.size() == static_cast<std::size_t>(width) * height * 4u;
}

bool GrayEquals(const Raster& actual, std::span<const std::uint8_t> expected, int width, int height)
{
    if (!actual.HasStorage() || actual.Width() != width || actual.Height() != height ||
        actual.Format() != RasterFormat::Gray8 || actual.Data() == nullptr ||
        expected.size() != static_cast<std::size_t>(width) * height)
        return false;
    for (int y = 0; y < height; ++y)
    {
        const auto* row = actual.Data() + static_cast<std::size_t>(y) * actual.Stride();
        const auto expectedOffset = static_cast<std::size_t>(y) * width;
        if (!std::equal(expected.begin() + static_cast<std::ptrdiff_t>(expectedOffset),
                        expected.begin() + static_cast<std::ptrdiff_t>(expectedOffset + width), row))
            return false;
    }
    return true;
}

const ImageLayer* FindLayer(const CanvasDocument& document, Guid id) noexcept
{
    const auto found = std::find_if(document.layers.begin(), document.layers.end(),
                                    [&](const ImageLayer& layer) { return layer.id == id; });
    return found == document.layers.end() ? nullptr : &*found;
}

void AffineRetainsIdentityAndDocumentMetadata()
{
    constexpr std::array<std::uint8_t, 16> pixels{1, 2, 3, 255, 5, 6, 7, 255, 9, 10, 11, 255, 13, 14, 15, 255};
    const Raster image = Bgra(2, 2, pixels);
    LayerTransform original;
    original.origin = {3, 4};
    original.size = {2, 2};
    original.sampling = LayerSampling::Nearest;
    ImageLayer layer = Layer(L"paint", image, original);
    layer.opacity = 0.625;
    layer.blendMode = LayerBlendMode::Multiply;
    layer.visible = false;
    layer.shape = LayerShapeStyle{ShapeKind::Rectangle, 0.2, 0.4, 0.6, 0};
    const auto id = layer.id;
    CanvasDocument source = Document({layer});
    InlineExecutor executor;

    TransformEdit identity;
    identity.layerID = id;
    identity.draft = original;
    auto same = PrepareTransformPixels(source, identity, executor);
    const auto* sameLayer = same.document ? FindLayer(same.document.value(), id) : nullptr;
    Expect(same.status == TransformPixelsStatus::Ready && same.document && same.historyName == L"Transform Layer" &&
               sameLayer && sameLayer->asset && sameLayer->asset->image.Identity() == image.Identity() &&
               sameLayer->transform == original,
           "affine.identity-keeps-image-storage-and-reports-history");

    TransformEdit moved = identity;
    moved.draft.origin = {11, 9};
    auto result = PrepareTransformPixels(source, moved, executor);
    const auto* output = result.document ? FindLayer(result.document.value(), id) : nullptr;
    Expect(result.status == TransformPixelsStatus::Ready && result.document &&
               result.document->selection == source.selection && output && output->asset &&
               output->asset->image.Identity() == image.Identity() && output->transform == moved.draft &&
               output->name == L"paint" && output->opacity == 0.625 && output->blendMode == LayerBlendMode::Multiply &&
               !output->visible && output->shape == layer.shape,
           "affine.move-retains-pixels-selection-and-layer-metadata");
    Expect(source.layers.front().transform == original &&
               source.layers.front().asset->image.Identity() == image.Identity(),
           "affine.source-document-remains-unchanged");
    Expect(output && output->asset && result.historyName == L"Transform Layer" &&
               TransformPixelsMemoryBytes(source, moved) >= output->asset->image.RetirementMemoryBytes(),
           "affine.named-history-and-memory-covers-actual-image-storage");
}

void PlacedMaskFollowingRules()
{
    constexpr std::array<std::uint8_t, 16> colorPixels{10, 20, 30, 255, 40,  50,  60,  255,
                                                       70, 80, 90, 255, 100, 110, 120, 255};
    constexpr std::array<std::uint8_t, 4> maskPixels{10, 80, 170, 240};
    const Raster image = Bgra(2, 2, colorPixels);
    const Raster maskImage = Gray(2, 2, maskPixels);
    LayerTransform transform;
    transform.origin = {2, 3};
    transform.size = {2, 2};
    transform.sampling = LayerSampling::Nearest;
    ImageLayer linked = Layer(L"linked", image, transform);
    LayerMask linkedMask;
    linkedMask.asset = RasterAsset{maskImage, Raster::MakeThumbnail(maskImage), L"linked mask"};
    linkedMask.placement = LayerTransform{Vec2{8, 9}, Vec2{2, 2}, 0, false, false, LayerSampling::Nearest};
    linkedMask.linked = true;
    linked.mask = linkedMask;
    ImageLayer unlinked = linked;
    unlinked.id = Guid::Generate();
    unlinked.name = L"unlinked";
    unlinked.mask->linked = false;
    const auto linkedID = linked.id;
    const auto unlinkedID = unlinked.id;
    CanvasDocument source = Document({linked, unlinked});

    InlineExecutor executor;
    const auto transformMask = [&](Guid target)
    {
        TransformEdit edit;
        edit.layerID = target;
        edit.draft = transform;
        edit.draft.origin = {12, 13};
        edit.corners = Rect(12, 13, 14, 15);
        return PrepareTransformPixels(source, edit, executor);
    };
    auto linkedResult = transformMask(linkedID);
    const auto* linkedOut = linkedResult.document ? FindLayer(linkedResult.document.value(), linkedID) : nullptr;
    Expect(linkedResult.status == TransformPixelsStatus::Ready && linkedOut && linkedOut->mask,
           "mask-placement.linked-result-available");
    if (linkedOut && linkedOut->mask)
    {
        LayerTransform movedTransform = transform;
        movedTransform.origin = {12, 13};
        const auto expectedLinked = linkedMask.placement->Following(transform, movedTransform);
        Expect(linkedOut->mask->placement && linkedOut->mask->placement->origin == expectedLinked.origin &&
                   linkedOut->mask->placement->size == expectedLinked.size &&
                   linkedOut->mask->placement->origin == Vec2{18, 19},
               "mask-placement.linked-placed-mask-follows-layer-map");
    }
    auto unlinkedResult = transformMask(unlinkedID);
    const auto* unlinkedOut =
        unlinkedResult.document ? FindLayer(unlinkedResult.document.value(), unlinkedID) : nullptr;
    Expect(unlinkedResult.status == TransformPixelsStatus::Ready && unlinkedOut && unlinkedOut->mask &&
               unlinkedOut->mask->placement == linkedMask.placement &&
               unlinkedOut->mask->asset.image.Identity() == maskImage.Identity(),
           "mask-placement.unlinked-placed-mask-retains-position-and-image");
    Expect(source.layers.front().mask->placement == linkedMask.placement &&
               source.layers.front().asset->image.Identity() == image.Identity(),
           "mask-placement.source-mask-and-image-unchanged");
}

void LiveShapeRedrawAndCurvedNumericReferences()
{
    constexpr std::array<std::uint8_t, 16> oldPixels{0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255};
    constexpr std::array<std::uint8_t, 48> red4x3{0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255,
                                                  0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255,
                                                  0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255};
    const Raster oldImage = Bgra(2, 2, oldPixels);
    LayerTransform transform;
    transform.origin = {1, 2};
    transform.size = {2, 2};
    transform.sampling = LayerSampling::Nearest;
    ImageLayer rectangle = Layer(L"Rectangle 1", oldImage, transform);
    rectangle.shape = LayerShapeStyle{ShapeKind::Rectangle, 1, 0, 0, 0};
    rectangle.shapeImage = oldImage;
    const auto id = rectangle.id;
    CanvasDocument source = Document({rectangle});
    TransformEdit edit;
    edit.layerID = id;
    edit.draft = transform;
    edit.draft.size = {4, 3};
    InlineExecutor executor;
    auto result = PrepareTransformPixels(source, edit, executor);
    const auto* output = result.document ? FindLayer(result.document.value(), id) : nullptr;
    Expect(result.status == TransformPixelsStatus::Ready && output && output->asset &&
               output->shapeImage.HasStorage() && output->asset->image.Identity() == output->shapeImage.Identity() &&
               output->asset->image.Width() == 4 && output->asset->image.Height() == 3 &&
               output->transform.size == Vec2{4, 3},
           "shape.rectangle-redraw-4x3-keeps-live-shape-image-identity");
    if (output && output->asset)
    {
        Expect(PixelsEqual(output->asset->image, red4x3, 4, 3), "shape.rectangle-redraw-literal-opaque-red-bgra");
        Expect(TransformPixelsMemoryBytes(source, edit) >= output->asset->image.RetirementMemoryBytes(),
               "shape.redraw-memory-declaration-covers-created-output-raster");
    }

    constexpr std::array<std::uint8_t, 4> black{0, 0, 0, 255};
    const Raster seed = Bgra(1, 1, black);
    const auto checkCurve = [&](ShapeKind kind, int width, int height, double radius, const char* label)
    {
        LayerTransform curveTransform;
        curveTransform.origin = {0, 0};
        curveTransform.size = {1, 1};
        curveTransform.sampling = LayerSampling::Nearest;
        ImageLayer curve = Layer(L"Curve", seed, curveTransform);
        curve.shape = LayerShapeStyle{kind, 0, 0, 0, radius};
        curve.shapeImage = seed;
        const auto curveId = curve.id;
        CanvasDocument curveSource = Document({curve});
        TransformEdit curveEdit;
        curveEdit.layerID = curveId;
        curveEdit.draft = curveTransform;
        curveEdit.draft.size = {static_cast<double>(width), static_cast<double>(height)};
        auto curveResult = PrepareTransformPixels(curveSource, curveEdit, executor);
        const auto* curveOutput = curveResult.document ? FindLayer(curveResult.document.value(), curveId) : nullptr;
        Expect(curveResult.status == TransformPixelsStatus::Ready && curveOutput && curveOutput->asset &&
                   curveOutput->asset->image.Width() == width && curveOutput->asset->image.Height() == height,
               label);
        if (curveOutput && curveOutput->asset)
        {
            const auto reference = ShapeImageForWork(kind, width, height, 0xff000000u, radius, executor);
            const auto* expected = reference.Data();
            const auto* actual = curveOutput->asset->image.Data();
            bool equal = expected && actual && reference.Width() == width && reference.Height() == height &&
                         curveOutput->asset->image.Width() == width && curveOutput->asset->image.Height() == height;
            if (equal)
                equal = std::equal(expected, expected + static_cast<std::size_t>(width) * height * 4u, actual);
            const auto alphaAt = [&](int x, int y)
            { return actual[static_cast<std::size_t>(y) * curveOutput->asset->image.Stride() + x * 4u + 3]; };
            Expect(equal && alphaAt(0, 0) == 0 && alphaAt(width / 2, height / 2) == 255 &&
                       alphaAt(width - 1, height - 1) == 0,
                   "shape.curved-redraw-matches-generator-and-literal-legacy-alpha-samples");
        }
    };
    checkCurve(ShapeKind::Ellipse, 20, 20, 0, "shape.ellipse-redraw-20x20-status-and-dimensions");
    checkCurve(ShapeKind::Rectangle, 40, 30, 8, "shape.rounded-redraw-40x30-status-and-dimensions");
}

void DistortionLiteralPixelsTrimAndMasks()
{
    constexpr std::array<std::uint8_t, 16> sourcePixels{0, 0, 0, 0, 11, 12, 13, 255, 0, 0, 0, 0, 21, 22, 23, 128};
    constexpr std::array<std::uint8_t, 8> trimmedPixels{11, 12, 13, 255, 21, 22, 23, 128};
    const Raster image = Bgra(2, 2, sourcePixels);
    LayerTransform transform;
    transform.origin = {5, 7};
    transform.size = {2, 2};
    transform.sampling = LayerSampling::Nearest;
    ImageLayer layer = Layer(L"trim", image, transform);
    const auto id = layer.id;
    CanvasDocument source = Document({layer}, 32, 24);
    TransformEdit identity;
    identity.layerID = id;
    identity.draft = transform;
    identity.corners = Rect(5, 7, 7, 9);
    InlineExecutor executor;
    auto result = PrepareTransformPixels(source, identity, executor);
    const auto* output = result.document ? FindLayer(result.document.value(), id) : nullptr;
    Expect(result.status == TransformPixelsStatus::Ready && result.historyName == L"Distort" && output &&
               output->asset && output->asset->image.Width() == 1 && output->asset->image.Height() == 2 &&
               output->transform.origin == Vec2{6, 7} && output->transform.size == Vec2{1, 2},
           "distortion.identity-perspective-trims-transparent-border-and-positions-output");
    if (output && output->asset)
        Expect(PixelsEqual(output->asset->image, trimmedPixels, 1, 2), "distortion.trim-retains-literal-bgra-values");

    constexpr std::array<std::uint8_t, 4> maskPixels{64, 64, 64, 64};
    // Four legacy sub-samples include the left boundary; the rightmost pixel is half covered.
    constexpr std::array<std::uint8_t, 6> zeroEdge{64, 64, 32, 64, 64, 32};
    const Raster maskRaster = Gray(2, 2, maskPixels);
    constexpr std::array<std::uint8_t, 16> opaquePixels{0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0, 255};
    ImageLayer masked = Layer(L"mask edge", Bgra(2, 2, opaquePixels), transform);
    LayerMask mask;
    mask.asset = RasterAsset{maskRaster, Raster::MakeThumbnail(maskRaster), L"edge"};
    mask.placement = transform;
    masked.mask = mask;
    const auto maskID = masked.id;
    CanvasDocument maskSource = Document({masked});
    TransformEdit moveMask;
    moveMask.layerID = maskID;
    moveMask.draft = transform;
    moveMask.draft.origin = {5.25, 7};
    moveMask.draft.size = {2, 2};
    moveMask.corners = Rect(5.25, 7, 7.25, 9);
    auto maskResult = PrepareTransformPixels(maskSource, moveMask, executor);
    const auto* maskOutput = maskResult.document ? FindLayer(maskResult.document.value(), maskID) : nullptr;
    Expect(maskResult.status == TransformPixelsStatus::Ready && maskOutput && maskOutput->mask &&
               maskOutput->mask->placement && maskOutput->mask->placement->origin == Vec2{5, 7} &&
               maskOutput->mask->placement->size == Vec2{3, 2},
           "distortion.mask-placement-records-fractional-bounds");
    if (maskOutput && maskOutput->mask)
        Expect(GrayEquals(maskOutput->mask->asset.image, zeroEdge, 3, 2),
               "distortion.mask-background-edge-tone-independent-literal");

    std::vector<std::uint8_t> perspectivePixels(20u * 20u * 4u);
    for (std::size_t offset = 0; offset < perspectivePixels.size(); offset += 4)
    {
        perspectivePixels[offset + 2] = 255;
        perspectivePixels[offset + 3] = 255;
    }
    const Raster perspectiveImage = Bgra(20, 20, perspectivePixels);
    LayerTransform perspectiveTransform;
    perspectiveTransform.origin = {10, 10};
    perspectiveTransform.size = {20, 20};
    perspectiveTransform.sampling = LayerSampling::Smooth;
    ImageLayer perspectiveLayer = Layer(L"perspective", perspectiveImage, perspectiveTransform);
    const auto perspectiveID = perspectiveLayer.id;
    CanvasDocument perspectiveSource = Document({perspectiveLayer}, 80, 60);
    TransformEdit perspectiveEdit;
    perspectiveEdit.layerID = perspectiveID;
    perspectiveEdit.draft = perspectiveTransform;
    perspectiveEdit.draft.size = {50, 20};
    perspectiveEdit.corners = Rect(10, 10, 60, 30);
    perspectiveEdit.corners.value()[2] = {30, 30};
    auto perspectiveResult = PrepareTransformPixels(perspectiveSource, perspectiveEdit, executor);
    const auto* perspectiveOut =
        perspectiveResult.document ? FindLayer(perspectiveResult.document.value(), perspectiveID) : nullptr;
    Expect(perspectiveResult.status == TransformPixelsStatus::Ready && perspectiveOut && perspectiveOut->asset &&
               perspectiveOut->asset->image.Width() == 50 && perspectiveOut->asset->image.Height() == 20,
           "distortion.simple-perspective-bounds-and-output");
    if (perspectiveOut && perspectiveOut->asset && perspectiveOut->asset->image.Width() == 50 &&
        perspectiveOut->asset->image.Height() == 20)
    {
        const Raster& warped = perspectiveOut->asset->image;
        const auto alphaAt = [&](int x, int y)
        { return warped.Data()[static_cast<std::size_t>(y) * warped.Stride() + static_cast<std::size_t>(x) * 4u + 3]; };
        Expect(warped.Data() && alphaAt(40, 2) == 255 && alphaAt(5, 15) == 255 && alphaAt(40, 18) == 0,
               "distortion.simple-perspective-independent-alpha-samples");
    }
}

void SparseSourceAndGroupPlacement()
{
    constexpr std::array<std::uint8_t, 16> tilePixels{9, 8, 7, 255, 19, 18, 17, 255, 29, 28, 27, 255, 39, 38, 37, 255};
    const Raster tile = Bgra(2, 2, tilePixels);
    const Raster sparse = Raster::MakeSparse(2, 2, RasterFormat::Bgra8888, Raster{}, 0, 0, {RasterTile{0, 0, tile}}, 0);
    LayerTransform firstTransform;
    firstTransform.origin = {1, 1};
    firstTransform.size = {1, 1};
    firstTransform.sampling = LayerSampling::Nearest;
    LayerTransform secondTransform;
    secondTransform.origin = {2, 0};
    secondTransform.size = {1, 2};
    secondTransform.sampling = LayerSampling::Nearest;
    ImageLayer first = Layer(L"first", sparse, firstTransform);
    ImageLayer second = Layer(L"second", tile, secondTransform);
    const auto firstID = first.id;
    const auto secondID = second.id;
    CanvasDocument source = Document({first, second});
    TransformEdit edit;
    edit.layerID = firstID;
    TransformGroup group;
    group.box.origin = {0, 0};
    group.box.size = {4, 4};
    group.originals = {{firstID, firstTransform}, {secondID, secondTransform}};
    edit.group = group;
    edit.draft = group.box;
    edit.draft.origin = {10, 20};
    edit.draft.size = {8, 4};
    InlineExecutor executor;
    auto result = PrepareTransformPixels(source, edit, executor);
    const auto* firstOut = result.document ? FindLayer(result.document.value(), firstID) : nullptr;
    const auto* secondOut = result.document ? FindLayer(result.document.value(), secondID) : nullptr;
    Expect(result.status == TransformPixelsStatus::Ready && result.historyName == L"Transform Layers" && firstOut &&
               secondOut && firstOut->transform.origin == Vec2{12, 21} && firstOut->transform.size == Vec2{2, 1} &&
               secondOut->transform.origin == Vec2{14, 20} && secondOut->transform.size == Vec2{2, 2},
           "group.following-transforms-match-independent-numeric-coordinate-reference");
    Expect(sparse.IsSparse() && !sparse.IsMaterialized() && firstOut && firstOut->asset &&
               firstOut->asset->image.IsSparse() && !firstOut->asset->image.IsMaterialized() &&
               firstOut->asset->image.Identity() == sparse.Identity() &&
               source.layers[0].asset->image.Identity() == sparse.Identity(),
           "sparse.identity-transform-retains-unmaterialized-cache-and-original-raster");
    Expect(result.document && result.document->selection == source.selection &&
               source.layers[0].transform == firstTransform && source.layers[1].transform == secondTransform,
           "group.original-document-and-selection-remain-unchanged");
    edit.group->originals.push_back(edit.group->originals.front());
    auto duplicate = PrepareTransformPixels(source, edit, executor);
    Expect(duplicate.status == TransformPixelsStatus::Invalid && !duplicate.document && !sparse.IsMaterialized(),
           "group.duplicate-authority-refuses-before-pixels-without-materialization");
    edit.group->originals.clear();
    Expect(TransformPixelsMemoryBytes(source, edit) == 0, "group.empty-authority-refuses-admission");
}

void RefusalsCancellationAndBudget()
{
    constexpr std::array<std::uint8_t, 4> pixel{31, 47, 59, 255};
    const Raster image = Bgra(1, 1, pixel);
    LayerTransform transform;
    transform.size = {1, 1};
    transform.sampling = LayerSampling::Nearest;
    ImageLayer layer = Layer(L"one", image, transform);
    const auto id = layer.id;
    CanvasDocument source = Document({layer});
    TransformEdit edit;
    edit.layerID = id;
    edit.draft = transform;
    InlineExecutor executor;

    edit.floating = FloatingTransform{};
    const auto floating = PrepareTransformPixels(source, edit, executor);
    Expect(floating.status == TransformPixelsStatus::Unsupported && !floating.document,
           "refusal.floating-transform-unsupported-without-document");
    edit.floating.reset();

    TransformEdit invalid = edit;
    invalid.draft.size.x = (std::numeric_limits<double>::quiet_NaN)();
    const auto invalidResult = PrepareTransformPixels(source, invalid, executor);
    Expect(invalidResult.status == TransformPixelsStatus::Invalid && !invalidResult.document &&
               TransformPixelsMemoryBytes(source, invalid) == 0,
           "refusal.invalid-transform-no-output-and-zero-reservation");

    CancelSource cancellation;
    static_cast<void>(cancellation.Cancel());
    const auto cancelled = PrepareTransformPixels(source, edit, executor, cancellation.Token());
    Expect(cancelled.status == TransformPixelsStatus::Cancelled && !cancelled.document,
           "refusal.pre-cancelled-transform-no-document");

    class Shutdown final : public Executor
    {
      public:
        Shutdown() noexcept
        {
            static_cast<void>(_source.request_stop());
        }
        [[nodiscard]] unsigned int Concurrency() const noexcept override
        {
            return 1;
        }
        [[nodiscard]] std::stop_token ShutdownToken() const noexcept override
        {
            return _source.get_token();
        }
        void RunParticipants(void* context, Participant run, unsigned int) noexcept override
        {
            run(context);
        }

      private:
        std::stop_source _source;
    } shutdown;
    const auto stopped = PrepareTransformPixels(source, edit, shutdown);
    Expect(stopped.status == TransformPixelsStatus::Cancelled && !stopped.document,
           "refusal.executor-shutdown-token-no-document");

    ImageLayer shape = layer;
    shape.shape = LayerShapeStyle{ShapeKind::Rectangle, 0, 1, 0, 0};
    shape.shapeImage = image;
    CanvasDocument budgetSource = Document({shape});
    TransformEdit tooLarge = edit;
    tooLarge.layerID = shape.id;
    tooLarge.draft.size = {10'001, 10'000};
    const auto memory = TransformPixelsMemoryBytes(budgetSource, tooLarge);
    const auto bounded = PrepareTransformPixels(budgetSource, tooLarge, executor);
    const auto* retained = bounded.document ? FindLayer(bounded.document.value(), shape.id) : nullptr;
    Expect(memory > 0 && bounded.status == TransformPixelsStatus::Ready && retained && retained->asset &&
               retained->asset->image.Identity() == image.Identity() && retained->asset->image.Width() == 1 &&
               retained->asset->image.Height() == 1 && retained->transform.size == Vec2{10'001, 10'000},
           "budget.synthetic-oversize-live-shape-keeps-old-image-without-large-allocation");
    TransformEdit invalidSize = edit;
    invalidSize.draft.size = {0, 1};
    Expect(TransformPixelsMemoryBytes(source, invalidSize) == 0, "budget.invalid-size-does-not-reserve");

    CanvasDocument metadataOnly = source;
    metadataOnly.layers.front().name.assign(2048, L'x');
    Expect(TransformPixelsMemoryBytes(metadataOnly, edit) == TransformPixelsMemoryBytes(source, edit),
           "budget-document-metadata-remains-caller-reservation");
}
} // namespace

int RunTransformPixelsWorkTests()
{
    gFailures = 0;
    gAssertions = 0;
    AffineRetainsIdentityAndDocumentMetadata();
    PlacedMaskFollowingRules();
    LiveShapeRedrawAndCurvedNumericReferences();
    DistortionLiteralPixelsTrimAndMasks();
    SparseSourceAndGroupPlacement();
    RefusalsCancellationAndBudget();
    std::printf("transform-pixels assertions=%d failures=%d\n", gAssertions, gFailures);
    return gFailures;
}
