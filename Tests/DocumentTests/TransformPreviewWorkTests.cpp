#include "TransformPreviewWorkTests.h"

#include "../Support/DocumentFixture.h"
#include "DistortWork.h"
#include "Document.h"
#include "Executor.h"
#include "TransformPreviewWork.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <new>
#include <optional>
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
    std::printf("%s transform-preview.%s\n", condition ? "PASS" : "FAIL", name);
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

ImageLayer Layer(const wchar_t* name, const Raster& image, LayerTransform transform)
{
    ImageLayer layer = ImageLayer::Blank(name, transform.size);
    layer.transform = transform;
    layer.asset = RasterAsset{image, Raster::MakeThumbnail(image), name};
    return layer;
}

LayerTransform Transform(Vec2 origin, Vec2 size)
{
    LayerTransform result;
    result.origin = origin;
    result.size = size;
    result.sampling = LayerSampling::Nearest;
    return result;
}

CanvasDocument Document(std::vector<ImageLayer> layers, int width = 64, int height = 48)
{
    DocumentFixture document;
    document.id = Guid::Generate();
    document.width = width;
    document.height = height;
    document.layers = std::move(layers);
    document.selection =
        DocumentSelection{true, SelectionLoops{SelectionLoop{Vec2{2, 3}, Vec2{9, 3}, Vec2{9, 11}, Vec2{2, 11}}}};
    return std::move(document).Seal();
}

const ImageLayer* Find(const CanvasDocument* document, const Guid& id) noexcept
{
    if (document == nullptr)
        return nullptr;
    const auto found = std::find_if(document->layers.begin(), document->layers.end(),
                                    [&](const ImageLayer& layer) { return layer.id == id; });
    return found == document->layers.end() ? nullptr : &*found;
}

const ImageLayer* Find(const CanvasDocument& document, const Guid& id) noexcept
{
    return Find(&document, id);
}

bool GrayPixels(const Raster& raster, std::span<const std::uint8_t> expected, int width, int height)
{
    if (!raster.HasStorage() || raster.Format() != RasterFormat::Gray8 || raster.Width() != width ||
        raster.Height() != height || expected.size() != static_cast<std::size_t>(width) * height ||
        raster.Data() == nullptr)
        return false;
    for (int y = 0; y < height; ++y)
    {
        const auto* const actual = raster.Data() + static_cast<std::size_t>(y) * raster.Stride();
        const auto offset = static_cast<std::size_t>(y) * width;
        if (!std::equal(expected.begin() + static_cast<std::ptrdiff_t>(offset),
                        expected.begin() + static_cast<std::ptrdiff_t>(offset + width), actual))
            return false;
    }
    return true;
}

bool AllTransparent(const Raster& image)
{
    if (!image.HasStorage() || image.Format() != RasterFormat::Bgra8888 || image.Data() == nullptr)
        return false;
    for (int y = 0; y < image.Height(); ++y)
        for (int x = 0; x < image.Width(); ++x)
            if (image.Data()[static_cast<std::size_t>(y) * image.Stride() + static_cast<std::size_t>(x) * 4u + 3u] != 0)
                return false;
    return true;
}

TransformEdit EditFor(const ImageLayer& layer, LayerTransform draft)
{
    TransformEdit edit;
    edit.layerID = layer.id;
    edit.draft = draft;
    return edit;
}

TransformDisplayResult Display(const CanvasDocument& source, const TransformEdit& edit, Executor& executor)
{
    return PrepareTransformDisplay(source, edit, executor);
}

void AffineSharingMetadataAndMasks()
{
    constexpr std::array<std::uint8_t, 16> colorBytes{1, 2, 3, 255, 4, 5, 6, 255, 7, 8, 9, 255, 10, 11, 12, 255};
    constexpr std::array<std::uint8_t, 4> maskBytes{0, 64, 128, 255};
    const Raster image = Bgra(2, 2, colorBytes);
    const Raster mask = Gray(2, 2, maskBytes);
    const LayerTransform original = Transform({3, 4}, {2, 2});
    ImageLayer layer = Layer(L"paint", image, original);
    layer.opacity = 0.625;
    layer.blendMode = LayerBlendMode::Multiply;
    layer.visible = false;
    layer.mask = LayerMask{RasterAsset{mask, mask, L"mask"}, true, std::nullopt, true};
    layer.mask->linked = false;
    layer.mask->placement = Transform({1, 1}, {2, 2});
    const Guid id = layer.id;
    const CanvasDocument source = Document({layer});
    TransformEdit edit = EditFor(layer, original);
    edit.draft.origin = {11, 9};
    InlineExecutor executor;
    const auto result = Display(source, edit, executor);
    const ImageLayer* output = result.document ? Find(result.document.value(), id) : nullptr;
    Expect(result.status == DistortWorkStatus::Ready && result.document && output && output->asset && output->mask &&
               output->transform == edit.draft && output->asset->image.Identity() == image.Identity() &&
               output->mask->asset.image.Identity() == mask.Identity() &&
               output->mask->placement == Transform({1, 1}, {2, 2}) && output->name == L"paint" &&
               output->opacity == 0.625 && output->blendMode == LayerBlendMode::Multiply && !output->visible &&
               output->mask->enabled && !output->mask->linked && result.document->selection == source.selection &&
               result.document->id == source.id && result.document->width == source.width &&
               result.document->height == source.height,
           "affine-shares-color-mask-and-keeps-metadata-document-selection-unlinked-mask-grid");
    Expect(source.layers.front().transform == original &&
               source.layers.front().mask->placement == Transform({1, 1}, {2, 2}) &&
               source.layers.front().asset->image.Identity() == image.Identity(),
           "affine-source-and-mask-placement-remain-immutable");

    ImageLayer linked = layer;
    linked.mask->linked = true;
    linked.mask->placement.reset();
    const auto linkedSource = Document({linked});
    const auto linkedResult = Display(linkedSource, edit, executor);
    const ImageLayer* linkedOutput = linkedResult.document ? Find(linkedResult.document.value(), linked.id) : nullptr;
    Expect(linkedResult.status == DistortWorkStatus::Ready && linkedOutput && linkedOutput->mask &&
               linkedOutput->mask->asset.image.Identity() == mask.Identity() &&
               !linkedOutput->mask->placement.has_value(),
           "affine-linked-unplaced-mask-shares-image-and-remains-unplaced");
    linked.mask->placement = Transform({1, 1}, {2, 2});
    const auto linkedPlacedSource = Document({linked});
    const auto linkedPlacedResult = Display(linkedPlacedSource, edit, executor);
    const ImageLayer* linkedPlacedOutput =
        linkedPlacedResult.document ? Find(linkedPlacedResult.document.value(), linked.id) : nullptr;
    Expect(linkedPlacedResult.status == DistortWorkStatus::Ready && linkedPlacedOutput && linkedPlacedOutput->mask &&
               linkedPlacedOutput->mask->asset.image.Identity() == mask.Identity() &&
               linkedPlacedOutput->mask->placement == Transform({9, 6}, {2, 2}),
           "affine-linked-placed-mask-shares-image-and-follows-layer-movement");
}

void ColorDistortionCapAndUntrimmedGrid()
{
    constexpr std::array<std::uint8_t, 16> transparent{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    const Raster image = Bgra(2, 2, transparent);
    const LayerTransform original = Transform({0, 0}, {2, 2});
    const ImageLayer layer = Layer(L"transparent", image, original);
    const auto source = Document({layer}, 4096, 2048);
    TransformEdit edit = EditFor(layer, original);
    edit.corners = Rect(7, 9, 2055, 1033);
    InlineExecutor executor;
    const auto result = Display(source, edit, executor);
    const ImageLayer* output = result.document ? Find(result.document.value(), layer.id) : nullptr;
    Expect(result.status == DistortWorkStatus::Ready && output && output->asset &&
               output->asset->image.Width() == 1024 && output->asset->image.Height() == 512 &&
               output->transform.origin == Vec2{7, 9} && output->transform.size == Vec2{2048, 1024} &&
               AllTransparent(output->asset->image),
           "distorted-color-caps-both-dimensions-and-keeps-transparent-untrimmed-grid");
    Expect(source.layers.front().asset->image.Identity() == image.Identity() && image.Width() == 2 &&
               image.Height() == 2,
           "distorted-color-keeps-source-raster-immutable");
}

void LinkedMaskDistortionPlacementAndCap()
{
    constexpr std::array<std::uint8_t, 16> colorBytes{0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0, 255};
    constexpr std::array<std::uint8_t, 4> maskBytes{0, 70, 140, 210};
    const Raster image = Bgra(2, 2, colorBytes);
    const Raster mask = Gray(2, 2, maskBytes);
    const LayerTransform original = Transform({0, 0}, {2, 2});
    ImageLayer layer = Layer(L"linked", image, original);
    layer.mask = LayerMask{RasterAsset{mask, mask, L"coverage"}, true, std::nullopt, true};
    const auto source = Document({layer}, 4096, 2048);
    TransformEdit edit = EditFor(layer, original);
    edit.draft = Transform({10, 12}, {2048, 1024});
    edit.corners = Rect(10, 12, 2058, 1036);
    InlineExecutor executor;
    const auto result = Display(source, edit, executor);
    const ImageLayer* output = result.document ? Find(result.document.value(), layer.id) : nullptr;
    Expect(result.status == DistortWorkStatus::Ready && output && output->asset && output->mask &&
               output->asset->image.Width() == 1024 && output->asset->image.Height() == 512 &&
               output->mask->asset.image.Width() == 1024 && output->mask->asset.image.Height() == 512 &&
               output->transform.origin == Vec2{10, 12} && output->transform.size == Vec2{2048, 1024} &&
               !output->mask->placement,
           "linked-unplaced-mask-caps-both-dimensions-and-follows-layer-grid");
    Expect(source.layers.front().mask->asset.image.Identity() == mask.Identity() &&
               source.layers.front().mask->asset.image.Width() == 2 &&
               source.layers.front().mask->placement == std::nullopt,
           "linked-unplaced-source-mask-remains-immutable");

    ImageLayer placed = layer;
    placed.mask->placement = Transform({1, 0}, {2, 2});
    const auto placedSource = Document({placed});
    TransformEdit moved = EditFor(placed, original);
    moved.draft.origin = {5, 6};
    moved.corners = {Vec2{5, 6}, Vec2{9, 6}, Vec2{8, 10}, Vec2{4, 10}};
    const auto placedResult = Display(placedSource, moved, executor);
    const ImageLayer* placedOutput = placedResult.document ? Find(placedResult.document.value(), placed.id) : nullptr;
    Expect(placedResult.status == DistortWorkStatus::Ready && placedOutput && placedOutput->mask &&
               placedOutput->mask->placement && placedOutput->mask->placement->origin == Vec2{6, 6} &&
               placedOutput->mask->asset.image.Format() == RasterFormat::Gray8 &&
               placedOutput->mask->asset.image.Width() == 5 && placedOutput->mask->asset.image.Height() == 4,
           "linked-placed-mask-carries-placement-and-distorts-through-same-perspective");
    const auto carriedPlacement = placed.mask->placement.value().Following(original, moved.draft);
    const auto carriedCorners = DistortWarp::Carried(carriedPlacement, moved.draft, moved.corners.value());
    const auto semanticReference = DistortWarp::WarpMask(mask, carriedPlacement, carriedCorners, 0.0, 1024.0);
    Expect(semanticReference.has_value() && MaskEdgeBackground(mask) == 0.0 && placedOutput && placedOutput->mask &&
               GrayPixels(placedOutput->mask->asset.image,
                          std::span<const std::uint8_t>(semanticReference->image.Data(),
                                                        static_cast<std::size_t>(semanticReference->image.Width()) *
                                                            semanticReference->image.Height()),
                          semanticReference->image.Width(), semanticReference->image.Height()),
           "linked-placed-mask-uses-binary-dark-edge-tone-and-independent-scalar-perspective-reference");
}

void UnlinkedAndUniformMasks()
{
    constexpr std::array<std::uint8_t, 16> colorBytes{0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0, 255};
    constexpr std::array<std::uint8_t, 4> maskBytes{0, 64, 128, 255};
    const Raster image = Bgra(2, 2, colorBytes);
    const Raster mask = Gray(2, 2, maskBytes);
    const LayerTransform original = Transform({3, 4}, {2, 2});
    ImageLayer layer = Layer(L"unlinked", image, original);
    layer.mask = LayerMask{RasterAsset{mask, mask, L"mask"}, true, std::nullopt, true};
    layer.mask->linked = false;
    const auto source = Document({layer});
    TransformEdit edit = EditFor(layer, original);
    edit.draft.origin = {10, 8};
    edit.corners = {Vec2{10, 8}, Vec2{14, 8}, Vec2{13, 12}, Vec2{9, 12}};
    InlineExecutor executor;
    const auto result = Display(source, edit, executor);
    const ImageLayer* output = result.document ? Find(result.document.value(), layer.id) : nullptr;
    Expect(result.status == DistortWorkStatus::Ready && output && output->mask &&
               output->mask->asset.image.Identity() == mask.Identity() && output->mask->placement == original &&
               output->mask->asset.image.Width() == 2 && output->mask->asset.image.Height() == 2,
           "unlinked-mask-keeps-original-document-placement-during-color-distortion");

    constexpr std::array<std::uint8_t, 1> uniformValue{77};
    const Raster uniform = Gray(1, 1, uniformValue);
    ImageLayer uniformLayer = Layer(L"uniform", image, original);
    uniformLayer.mask = LayerMask{RasterAsset{uniform, uniform, L"uniform mask"}, true, std::nullopt, true};
    const auto uniformSource = Document({uniformLayer});
    TransformEdit uniformEdit = EditFor(uniformLayer, original);
    uniformEdit.corners = edit.corners;
    const auto uniformResult = Display(uniformSource, uniformEdit, executor);
    const ImageLayer* uniformOutput =
        uniformResult.document ? Find(uniformResult.document.value(), uniformLayer.id) : nullptr;
    Expect(uniformResult.status == DistortWorkStatus::Ready && uniformOutput && uniformOutput->mask &&
               uniformOutput->mask->asset.image.Identity() == uniform.Identity() && !uniformOutput->mask->placement,
           "uniform-linked-mask-keeps-shared-identity-and-implicit-placement");
}

void StandaloneMaskWarpAndGroupCarriedGeometry()
{
    constexpr std::array<std::uint8_t, 16> colorBytes{0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0, 255};
    constexpr std::array<std::uint8_t, 4> maskBytes{0, 80, 160, 240};
    const Raster image = Bgra(2, 2, colorBytes);
    const Raster mask = Gray(2, 2, maskBytes);
    const LayerTransform original = Transform({0, 0}, {2, 2});
    ImageLayer layer = Layer(L"mask target", image, original);
    layer.mask = LayerMask{RasterAsset{mask, mask, L"mask"}, true, std::nullopt, false};
    const auto source = Document({layer});
    TransformEdit maskEdit = EditFor(layer, Transform({0, 0}, {4, 2}));
    maskEdit.mask = true;
    maskEdit.corners = Rect(0, 0, 4, 2);
    InlineExecutor executor;
    const auto warped = Display(source, maskEdit, executor);
    const ImageLayer* maskOutput = warped.document ? Find(warped.document.value(), layer.id) : nullptr;
    constexpr std::array<std::uint8_t, 8> expectedWarp{0, 0, 80, 80, 160, 160, 240, 240};
    Expect(warped.status == DistortWorkStatus::Ready && maskOutput && maskOutput->mask &&
               maskOutput->mask->asset.image.Identity() != mask.Identity() &&
               GrayPixels(maskOutput->mask->asset.image, expectedWarp, 4, 2) && maskOutput->mask->placement &&
               maskOutput->mask->placement->origin == Vec2{0, 0} && maskOutput->mask->placement->size == Vec2{4, 2},
           "standalone-mask-distortion-resamples-literal-nearest-grid-and-sets-placement");
    Expect(layer.mask->asset.image.Identity() == mask.Identity() && layer.mask->placement == std::nullopt,
           "standalone-mask-warp-does-not-mutate-source-mask");

    ImageLayer first = Layer(L"first", image, Transform({1, 0}, {2, 2}));
    ImageLayer second = Layer(L"second", image, Transform({0, 0}, {4, 2}));
    TransformEdit groupEdit = EditFor(first, Transform({0, 0}, {8, 2}));
    TransformGroup group;
    group.box = Transform({0, 0}, {4, 2});
    group.originals = {{first.id, first.transform}, {second.id, second.transform}};
    groupEdit.group = group;
    groupEdit.corners = {Vec2{0, 0}, Vec2{8, 0}, Vec2{7, 2}, Vec2{1, 2}};
    const auto groupSource = Document({first, second});
    const auto groupResult = Display(groupSource, groupEdit, executor);
    const ImageLayer* firstOut = groupResult.document ? Find(groupResult.document.value(), first.id) : nullptr;
    const ImageLayer* secondOut = groupResult.document ? Find(groupResult.document.value(), second.id) : nullptr;
    Expect(groupResult.status == DistortWorkStatus::Ready && firstOut && secondOut && firstOut->asset &&
               secondOut->asset && firstOut->transform.origin == Vec2{2, 0} && firstOut->transform.size == Vec2{4, 2} &&
               secondOut->transform.origin == Vec2{0, 0} && secondOut->transform.size == Vec2{8, 2},
           "group-distortion-carries-each-members-geometry-through-shared-perspective");
}

void SparseSourceAndCancellation()
{
    constexpr std::array<std::uint8_t, 16> bytes{9, 8, 7, 255, 19, 18, 17, 255, 29, 28, 27, 255, 39, 38, 37, 255};
    const Raster tile = Bgra(2, 2, bytes);
    const Raster sparse = Raster::MakeSparse(2, 2, RasterFormat::Bgra8888, Raster{}, 0, 0, {RasterTile{0, 0, tile}}, 0);
    ImageLayer layer = ImageLayer::Blank(L"sparse", Vec2{2, 2});
    layer.transform = Transform({0, 0}, {2, 2});
    layer.asset = RasterAsset{sparse, tile, L"sparse"};
    const auto source = Document({layer});
    TransformEdit edit = EditFor(layer, Transform({2, 1}, {2, 2}));
    edit.corners = {Vec2{2, 1}, Vec2{5, 1}, Vec2{4, 4}, Vec2{2, 4}};
    InlineExecutor executor;
    const auto result = Display(source, edit, executor);
    const ImageLayer* output = result.document ? Find(result.document.value(), layer.id) : nullptr;
    Expect(result.status == DistortWorkStatus::Ready && output && output->asset && output->asset->image.Width() == 3 &&
               output->asset->image.Height() == 3 && sparse.IsSparse() && !sparse.IsMaterialized() &&
               source.layers.front().asset->image.IsSparse() && !source.layers.front().asset->image.IsMaterialized(),
           "worker-distortion-assembles-sparse-source-privately-without-materializing-original");

    for (const auto stopAt :
         {TransformPreviewStage::Snapshot, TransformPreviewStage::Layer, TransformPreviewStage::Publication})
    {
        struct Gate final
        {
            TransformPreviewStage stopAt;
            CancelSource cancellation;
            bool reached = false;
        } gate{stopAt};
        const auto cancelAt = [](void* opaque, TransformPreviewStage stage)
        {
            auto& value = *static_cast<Gate*>(opaque);
            if (stage == value.stopAt)
            {
                value.reached = true;
                static_cast<void>(value.cancellation.Cancel());
            }
        };
        const auto cancelled =
            PrepareTransformDisplay(source, edit, executor, gate.cancellation.Token(), {&gate, cancelAt});
        Expect(gate.reached && cancelled.status == DistortWorkStatus::Cancelled && !cancelled.document,
               stopAt == TransformPreviewStage::Snapshot ? "cancel-snapshot-stage-has-no-document"
               : stopAt == TransformPreviewStage::Layer  ? "cancel-layer-stage-has-no-document"
                                                         : "cancel-publication-stage-has-no-document");
    }
}

void InvalidFallbacks()
{
    constexpr std::array<std::uint8_t, 16> colorBytes{0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0, 255};
    constexpr std::array<std::uint8_t, 4> maskBytes{0, 64, 128, 255};
    const Raster image = Bgra(2, 2, colorBytes);
    const Raster gray = Gray(2, 2, maskBytes);
    const LayerTransform transform = Transform({0, 0}, {2, 2});
    const ImageLayer layer = Layer(L"valid", image, transform);
    const CanvasDocument source = Document({layer});
    InlineExecutor executor;
    TransformEdit invalid = EditFor(layer, transform);
    invalid.draft.size.x = 0;
    Expect(Display(source, invalid, executor).status == DistortWorkStatus::Invalid, "invalid-edit-is-rejected");
    TransformEdit missing = EditFor(layer, transform);
    missing.group = TransformGroup{transform, {{Guid::Generate(), transform}}};
    Expect(Display(source, missing, executor).status == DistortWorkStatus::Invalid, "group-missing-member-is-rejected");
    TransformEdit duplicate = EditFor(layer, transform);
    duplicate.group = TransformGroup{transform, {{layer.id, transform}, {layer.id, transform}}};
    Expect(Display(source, duplicate, executor).status == DistortWorkStatus::Invalid,
           "group-duplicate-member-is-rejected");
    TransformEdit badCorners = EditFor(layer, transform);
    badCorners.corners = {Vec2{0, 0}, Vec2{3, 3}, Vec2{0, 3}, Vec2{3, 0}};
    const auto fallback = Display(source, badCorners, executor);
    Expect(fallback.status == DistortWorkStatus::Ready && fallback.document &&
               fallback.document->layers.front().asset->image == image &&
               fallback.document->layers.front().transform == badCorners.draft,
           "invalid-distortion-corners-preserve-reference-affine-fallback");
    ImageLayer wrongFormat = Layer(L"wrong format", gray, transform);
    const auto badSource = Document({wrongFormat});
    TransformEdit distortWrongFormat = EditFor(wrongFormat, transform);
    distortWrongFormat.corners = Rect(0, 0, 4, 4);
    Expect(Display(badSource, distortWrongFormat, executor).status == DistortWorkStatus::Invalid,
           "color-distortion-refuses-gray-source-format");
    TransformEdit absentTarget = EditFor(layer, transform);
    absentTarget.layerID = Guid::Generate();
    Expect(TransformDisplayMemoryBytes(source, absentTarget) == 0 &&
               Display(source, absentTarget, executor).status == DistortWorkStatus::Invalid,
           "missing-transform-target-has-zero-admission-and-invalid-result");
}

struct SessionFixture final
{
    DocumentSession session;
    Guid id;
    Guid sibling;
    Raster image;

    SessionFixture()
    {
        constexpr std::array<std::uint8_t, 16> bytes{1, 2, 3, 255, 4, 5, 6, 255, 7, 8, 9, 255, 10, 11, 12, 255};
        constexpr std::array<std::uint8_t, 4> maskBytes{255, 192, 128, 64};
        image = Bgra(2, 2, bytes);
        ImageLayer layer = Layer(L"source", image, Transform({3, 4}, {2, 2}));
        const Raster mask = Gray(2, 2, maskBytes);
        layer.mask = LayerMask{RasterAsset{mask, mask, L"source mask"}, true, std::nullopt, true};
        ImageLayer other = Layer(L"other", image, Transform({4, 0}, {2, 2}));
        id = layer.id;
        sibling = other.id;
        session.ReplaceDocument(Document({layer, other}), id);
        static_cast<void>(session.CopySelection());
        session.SelectLayerTarget(id, true);
        session.BeginTransform();
        auto draft = session.PendingTransformEdit()->draft;
        draft.origin = {3, 2};
        session.PreviewTransform(draft);
    }
};

TransformPreviewResult Prepare(SessionFixture& fixture, Executor& executor, CancelToken cancellation = {},
                               TransformPreviewObserver observer = {})
{
    auto input = fixture.session.PrepareTransformPreviewInput();
    return input ? DocumentSession::PrepareTransformPreviewPublication(std::move(input.value()), executor, cancellation,
                                                                       observer)
                 : TransformPreviewResult{};
}

struct OwnerSnapshot final
{
    CanvasDocument document;
    DocumentHistory::StateView history;
    std::uint64_t generation;
    std::uint64_t clipboardSerial;
    std::optional<PixelClipboard> clipboard;
    std::optional<Guid> active;
    bool maskSelected;

    explicit OwnerSnapshot(DocumentSession& session)
        : document(session.Document().value()), history(session.history.CaptureState()),
          generation(session.Generation()), clipboardSerial(session.ClipboardSerial()), clipboard(session.Clipboard()),
          active(session.ActiveLayerID()), maskSelected(session.IsMaskSelected())
    {
    }
};

bool SameClipboard(const std::optional<PixelClipboard>& a, const std::optional<PixelClipboard>& b)
{
    return a.has_value() == b.has_value() && (!a || (a->image == b->image && a->origin == b->origin));
}

bool SameOwner(DocumentSession& session, const OwnerSnapshot& before)
{
    return session.Document() == before.document && session.history.IsCurrent(before.history) &&
           session.Generation() == before.generation && session.ClipboardSerial() == before.clipboardSerial &&
           SameClipboard(session.Clipboard(), before.clipboard) && session.ActiveLayerID() == before.active &&
           session.IsMaskSelected() == before.maskSelected;
}

void PublicationDoesNotCommitDocument()
{
    SessionFixture fixture;
    InlineExecutor executor;
    const OwnerSnapshot before(fixture.session);
    const LayerTransform pendingDraft = fixture.session.PendingTransformEdit()->draft;
    const auto* shownBefore = fixture.session.DisplayDocument();
    auto ready = Prepare(fixture, executor);
    Expect(ready.status == DistortWorkStatus::Ready && ready.publication && SameOwner(fixture.session, before) &&
               fixture.session.PendingTransformEdit() &&
               fixture.session.PendingTransformEdit()->draft == pendingDraft &&
               fixture.session.DisplayDocument() == shownBefore,
           "prepare-publication-does-not-change-opening-document-history-generation-or-session");
    if (!ready.publication)
        return;
    auto publication = std::move(ready.publication.value());
    Expect(fixture.session.ApplyPreparedTransformPreviewIfCurrent(publication) && SameOwner(fixture.session, before) &&
               fixture.session.PendingTransformEdit() && fixture.session.DisplayDocument() &&
               fixture.session.DisplayDocument()->layers.front().transform.origin == Vec2{3, 2} &&
               fixture.session.Document()->layers.front().transform.origin == Vec2{3, 4},
           "apply-updates-display-only-and-preserves-document-history-generation-clipboard-and-mask-target");
    Expect(!fixture.session.ApplyPreparedTransformPreviewIfCurrent(publication), "publication-is-single-use");
}

void WorkerCancellationShutdownAndBadAllocation()
{
    SessionFixture fixture;
    InlineExecutor executor;
    for (const auto stage :
         {TransformPreviewStage::Snapshot, TransformPreviewStage::Layer, TransformPreviewStage::Publication})
    {
        struct Gate final
        {
            TransformPreviewStage stage;
            CancelSource cancellation;
            bool reached = false;
        } gate{stage};
        const auto checkpoint = [](void* opaque, TransformPreviewStage current)
        {
            auto& value = *static_cast<Gate*>(opaque);
            if (current == value.stage)
            {
                value.reached = true;
                static_cast<void>(value.cancellation.Cancel());
            }
        };
        const OwnerSnapshot before(fixture.session);
        auto result = Prepare(fixture, executor, gate.cancellation.Token(), {&gate, checkpoint});
        Expect(gate.reached && result.status == DistortWorkStatus::Cancelled && !result.publication &&
                   SameOwner(fixture.session, before),
               stage == TransformPreviewStage::Snapshot ? "publication-snapshot-cancellation-no-owner-change"
               : stage == TransformPreviewStage::Layer  ? "publication-layer-cancellation-no-owner-change"
                                                        : "publication-checkpoint-cancellation-no-owner-change");
    }
    CancelSource preCancelled;
    static_cast<void>(preCancelled.Cancel());
    const auto cancelled = Prepare(fixture, executor, preCancelled.Token());
    Expect(cancelled.status == DistortWorkStatus::Cancelled && !cancelled.publication,
           "pre-cancelled-publication-returns-cancelled");

    struct Fault final
    {
        bool reached = false;
    } fault;
    const auto failAtPublication = [](void* opaque, TransformPreviewStage stage)
    {
        auto& value = *static_cast<Fault*>(opaque);
        if (stage == TransformPreviewStage::Publication)
        {
            value.reached = true;
            throw std::bad_alloc{};
        }
    };
    const OwnerSnapshot beforeFault(fixture.session);
    bool caught = false;
    try
    {
        static_cast<void>(Prepare(fixture, executor, {}, {&fault, failAtPublication}));
    }
    catch (const std::bad_alloc&)
    {
        caught = true;
    }
    Expect(fault.reached && caught && SameOwner(fixture.session, beforeFault),
           "bad-allocation-at-publication-observed-and-owner-state-unchanged");

    class CancelledExecutor final : public Executor
    {
      public:
        CancelledExecutor()
        {
            static_cast<void>(_shutdown.request_stop());
        }
        unsigned int Concurrency() const noexcept override
        {
            return 1;
        }
        std::stop_token ShutdownToken() const noexcept override
        {
            return _shutdown.get_token();
        }
        void RunParticipants(void* context, Participant run, unsigned int) noexcept override
        {
            run(context);
        }

      private:
        std::stop_source _shutdown;
    } shutdownExecutor;
    const auto shutdown = Prepare(fixture, shutdownExecutor);
    Expect(shutdown.status == DistortWorkStatus::Cancelled && !shutdown.publication,
           "executor-shutdown-cancels-preview-publication");
}

void RefuseStaleForeignAndInvalidInputs()
{
    InlineExecutor executor;
    {
        SessionFixture fixture;
        auto prepared = Prepare(fixture, executor);
        auto newer = fixture.session.PendingTransformEdit()->draft;
        newer.origin.x += 1;
        fixture.session.PreviewTransform(newer);
        Expect(prepared.publication &&
                   !fixture.session.ApplyPreparedTransformPreviewIfCurrent(prepared.publication.value()) &&
                   fixture.session.PendingTransformEdit()->draft == newer,
               "newer-draft-refuses-old-display-publication");
    }
    {
        SessionFixture fixture;
        auto prepared = Prepare(fixture, executor);
        fixture.session.CancelTransform();
        fixture.session.BeginTransform();
        Expect(prepared.publication &&
                   !fixture.session.ApplyPreparedTransformPreviewIfCurrent(prepared.publication.value()) &&
                   fixture.session.PendingTransformEdit(),
               "closed-and-reopened-transform-refuses-stale-publication");
    }
    {
        SessionFixture fixture;
        auto prepared = Prepare(fixture, executor);
        EditLayerRecordsForTest(fixture.session,
                                [](DocumentFixture& document) { document.layers.front().name = L"changed"; });
        const auto current = fixture.session.Document().value();
        Expect(prepared.publication &&
                   !fixture.session.ApplyPreparedTransformPreviewIfCurrent(prepared.publication.value()) &&
                   fixture.session.Document() == current,
               "changed-source-document-refuses-old-display-publication");
    }
    {
        SessionFixture fixture;
        auto prepared = Prepare(fixture, executor);
        fixture.session.SelectLayer(fixture.sibling);
        const auto active = fixture.session.ActiveLayerID();
        Expect(prepared.publication &&
                   !fixture.session.ApplyPreparedTransformPreviewIfCurrent(prepared.publication.value()) &&
                   fixture.session.ActiveLayerID() == active,
               "changed-active-target-refuses-old-display-publication");
    }
    {
        SessionFixture fixture;
        auto prepared = Prepare(fixture, executor);
        fixture.session.history.Reset();
        Expect(prepared.publication &&
                   !fixture.session.ApplyPreparedTransformPreviewIfCurrent(prepared.publication.value()),
               "changed-history-state-refuses-old-display-publication");
    }
    {
        SessionFixture fixture;
        auto prepared = Prepare(fixture, executor);
        CanvasDocument replacement = fixture.session.Document().value();
        fixture.session.ReplaceDocument(std::move(replacement), fixture.id);
        Expect(prepared.publication &&
                   !fixture.session.ApplyPreparedTransformPreviewIfCurrent(prepared.publication.value()),
               "same-id-document-replacement-refuses-old-display-publication");
    }
    {
        SessionFixture fixture;
        auto prepared = Prepare(fixture, executor);
        SessionFixture foreign;
        Expect(prepared.publication &&
                   !foreign.session.ApplyPreparedTransformPreviewIfCurrent(prepared.publication.value()),
               "foreign-session-refuses-display-publication");
    }
    {
        SessionFixture fixture;
        auto input = fixture.session.PrepareTransformPreviewInput();
        Expect(input.has_value(), "preview-input-captured-for-move-only-check");
        if (input)
        {
            TransformPreviewInput owned = std::move(input.value());
            auto movedFrom =
                DocumentSession::PrepareTransformPreviewPublication(std::move(input.value()), executor, {}, {});
            auto valid = DocumentSession::PrepareTransformPreviewPublication(std::move(owned), executor, {}, {});
            Expect(movedFrom.status == DistortWorkStatus::Invalid && !movedFrom.publication &&
                       valid.status == DistortWorkStatus::Ready && valid.publication,
                   "moved-from-preview-input-refused-and-owned-input-succeeds");
        }
    }
    {
        DocumentSession empty;
        Expect(empty.TransformPreviewPreparationMemoryBytes() == 0 && !empty.PrepareTransformPreviewInput(),
               "preview-input-refused-without-open-transform");
    }
}
} // namespace

int RunTransformPreviewWorkTests()
{
    gFailures = 0;
    gAssertions = 0;
    AffineSharingMetadataAndMasks();
    ColorDistortionCapAndUntrimmedGrid();
    LinkedMaskDistortionPlacementAndCap();
    UnlinkedAndUniformMasks();
    StandaloneMaskWarpAndGroupCarriedGeometry();
    SparseSourceAndCancellation();
    InvalidFallbacks();
    PublicationDoesNotCommitDocument();
    WorkerCancellationShutdownAndBadAllocation();
    RefuseStaleForeignAndInvalidInputs();
    std::printf("transform-preview: %d assertions, %d failures\n", gAssertions, gFailures);
    return gFailures;
}
