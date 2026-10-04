#include "OptionalImageThumbnailTests.h"

#include "../Support/DocumentFixture.h"
#include "CanvasSize.h"
#include "ColorEditHistory.h"
#include "ColorEditWork.h"
#include "Document.h"
#include "Executor.h"
#include "RasterEditWork.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <span>
#include <utility>
#include <vector>

namespace
{
int gFailures = 0;

void Expect(bool condition, const char* name) noexcept
{
    std::printf("%s document-thumbnail.%s\n", condition ? "PASS" : "FAIL", name);
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

bool EqualsPixels(const Raster& raster, RasterFormat format, std::span<const std::uint8_t> expected)
{
    if (!raster.HasStorage() || raster.Format() != format || raster.Width() < 1 || raster.Height() < 1 ||
        expected.size() != static_cast<std::size_t>(raster.Width()) * static_cast<std::size_t>(raster.Height()) *
                               static_cast<std::size_t>(raster.Channels()))
        return false;

    std::vector<std::uint8_t> actual(expected.size());
    raster.ReadRegion(0, 0, raster.Width(), raster.Height(), actual.data(), raster.Width() * raster.Channels(), 0);
    return std::equal(actual.begin(), actual.end(), expected.begin(), expected.end());
}

CanvasDocument ImageDocument(const Raster& image)
{
    DocumentFixture document;
    document.width = image.Width();
    document.height = image.Height();
    ImageLayer layer =
        ImageLayer::Blank(L"Image", Vec2{static_cast<double>(image.Width()), static_cast<double>(image.Height())});
    layer.asset = RasterAsset{image, Raster{}, layer.name};
    document.layers.push_back(std::move(layer));
    return std::move(document).Seal();
}

void EmptyImageThumbnailAndHistoryEquality()
{
    constexpr std::array<std::uint8_t, 8> sourcePixels{10, 20, 30, 255, 40, 50, 60, 255};
    const Raster image = Bgra(2, 1, sourcePixels);
    CanvasDocument withEmptyThumbnail = ImageDocument(image);
    Expect(image.HasStorage() && !withEmptyThumbnail.layers.front().asset->thumbnail.HasStorage(),
           "image-empty-thumbnail-is-valid-state");
    Expect(EqualsPixels(withEmptyThumbnail.layers.front().asset->image, RasterFormat::Bgra8888, sourcePixels),
           "image-empty-thumbnail-keeps-literal-bgra");

    DocumentSession session;
    session.ReplaceDocument(withEmptyThumbnail, withEmptyThumbnail.layers.front().id);
    session.BeginEdit(L"Visibility");
    EditLayerRecordsForTest(session, [](DocumentFixture& document) { document.layers.front().visible = false; });
    session.EndEdit();
    session.Undo();
    const CanvasDocument beforeThumbnail = session.Document().value();
    const Guid revision = session.history.CurrentRevision();
    const int undoCount = session.history.UndoCount();
    const std::wstring redoName = session.history.RedoName();

    const Raster explicitThumbnail = Raster::MakeThumbnail(image);
    session.BeginEdit(L"Derive image thumbnail");
    EditLayerRecordsForTest(session, [&](DocumentFixture& document)
                            { document.layers.front().asset->thumbnail = explicitThumbnail; });
    session.EndEdit();
    Expect(explicitThumbnail.HasStorage() && session.Document().value() == beforeThumbnail,
           "image-explicit-thumbnail-derivation-preserves-document-equality");
    Expect(session.history.CanRedo() && session.history.RedoName() == redoName &&
               session.history.CurrentRevision() == revision && session.history.UndoCount() == undoCount,
           "image-thumbnail-no-op-preserves-redo-revision-and-count");
    session.Redo();
    Expect(!session.Document().value().layers.front().visible && session.history.UndoName() == L"Visibility",
           "image-preserved-redo-restores-original-edit");
}

void SparseImageThumbnailDoesNotMaterializeSource()
{
    constexpr std::array<std::uint8_t, 16> basePixels{11, 21, 31, 255, 41,  51,  61,  255,
                                                      71, 81, 91, 255, 101, 111, 121, 255};
    const Raster base = Bgra(2, 2, basePixels);
    const Raster sparse = Raster::MakeSparse(4, 4, RasterFormat::Bgra8888, base, 1, 1, {}, 0);
    const bool baseWasMaterialized = base.IsMaterialized();
    const Raster thumbnail = Raster::MakeThumbnail(sparse, 2);
    Expect(sparse.IsSparse() && thumbnail.HasStorage() && thumbnail.Format() == RasterFormat::Bgra8888,
           "sparse-image-explicit-thumbnail-exists");
    Expect(!sparse.IsMaterialized() && base.IsMaterialized() == baseWasMaterialized,
           "sparse-image-thumbnail-never-materializes-source");
    constexpr std::array<std::uint8_t, 16> expectedThumbnail{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 101, 111, 121, 255};
    Expect(EqualsPixels(thumbnail, RasterFormat::Bgra8888, expectedThumbnail),
           "sparse-image-thumbnail-keeps-independent-nearest-samples");
    std::array<std::uint8_t, 16> sampled{};
    sparse.ReadRegion(1, 1, 2, 2, sampled.data(), 8, 0);
    Expect(sampled == basePixels, "sparse-image-read-region-retains-literal-base-bgra");
}

void MaskThumbnailRemainsGrayAndSemantic()
{
    constexpr std::array<std::uint8_t, 4> blackPixels{0, 0, 0, 0};
    constexpr std::array<std::uint8_t, 4> whitePixels{255, 255, 255, 255};
    const Raster black = Gray(2, 2, blackPixels);
    const Raster white = Gray(2, 2, whitePixels);
    const Raster blackThumbnail = Raster::MakeThumbnail(black);
    const Raster whiteThumbnail = Raster::MakeThumbnail(white);
    Expect(blackThumbnail.HasStorage() && blackThumbnail.Format() == RasterFormat::Gray8 &&
               EqualsPixels(blackThumbnail, RasterFormat::Gray8, blackPixels),
           "mask-thumbnail-keeps-gray8-literal-bytes");
    Expect(MaskEdgeBackground(blackThumbnail) == 0.0 && MaskEdgeBackground(whiteThumbnail) == 1.0,
           "mask-thumbnail-edge-background-remains-semantic");

    LayerMask mask;
    mask.asset = RasterAsset{black, blackThumbnail, L"mask"};
    LayerMask sameImageDifferentThumbnail = mask;
    sameImageDifferentThumbnail.asset.thumbnail = whiteThumbnail;
    Expect(mask == sameImageDifferentThumbnail, "mask-equality-retains-existing-image-based-semantics");
}

void RasterEditOutputHasNoImageThumbnail()
{
    constexpr std::array<std::uint8_t, 4> sourcePixels{10, 20, 30, 255};
    constexpr std::array<std::uint8_t, 4> invertedPixels{245, 235, 225, 255};
    ImageLayer layer = ImageLayer::Blank(L"Image", Vec2{1, 1});
    layer.asset = RasterAsset{Bgra(1, 1, sourcePixels), Raster{}, layer.name};

    RasterEditInput input;
    input.layer = layer;
    input.canvasWidth = 1;
    input.canvasHeight = 1;
    input.pixelLimit = kMaxLayerPixels;
    input.kind = RasterEditKind::Invert;
    RasterEditResult result = RunRasterEdit(input);
    Expect(result.status == RasterEditStatus::Ready && result.layer && result.layer->asset,
           "raster-edit-invert-succeeds");
    if (!result.layer || !result.layer->asset)
        return;
    Expect(!result.layer->asset->thumbnail.HasStorage(), "raster-edit-image-output-thumbnail-is-empty");
    Expect(EqualsPixels(result.layer->asset->image, RasterFormat::Bgra8888, invertedPixels),
           "raster-edit-invert-keeps-literal-bgra-result");
}

void ResizeImageOutputHasNoImageThumbnail()
{
    constexpr std::array<std::uint8_t, 4> sourcePixels{12, 34, 56, 255};
    constexpr std::array<std::uint8_t, 8> resizedPixels{12, 34, 56, 255, 12, 34, 56, 255};
    CanvasDocument source = ImageDocument(Bgra(1, 1, sourcePixels));
    ImageSizeOptions options;
    options.width = 2;
    options.height = 1;
    options.resolution = source.resolution;
    options.sampling = LayerSampling::Nearest;
    CanvasDocument resized;
    const ResizeStatus status = ResizeImage(source, options, resized);
    Expect(status == ResizeStatus::Ok && resized.layers.size() == 1 && resized.layers.front().asset.has_value(),
           "resize-image-nearest-succeeds");
    if (status != ResizeStatus::Ok || resized.layers.empty() || !resized.layers.front().asset)
        return;
    Expect(!resized.layers.front().asset->thumbnail.HasStorage(), "resize-image-output-thumbnail-is-empty");
    Expect(EqualsPixels(resized.layers.front().asset->image, RasterFormat::Bgra8888, resizedPixels),
           "resize-image-nearest-keeps-literal-bgra-result");
}

void PreparedColorEditOutputHasNoImageThumbnail()
{
    constexpr std::array<std::uint8_t, 16> sourcePixels{0,   0,   0,   255, 64,  64,  64,  255,
                                                        128, 128, 128, 255, 255, 255, 255, 255};
    constexpr std::array<std::uint8_t, 16> gammaPixels{0,   0,   0,   255, 128, 128, 128, 255,
                                                       181, 181, 181, 255, 255, 255, 255, 255};
    const Raster sourceImage = Bgra(4, 1, sourcePixels);
    CanvasDocument source = ImageDocument(sourceImage);
    const Guid active = source.layers.front().id;
    DocumentSession session;
    session.ReplaceDocument(source, active);
    session.BeginColorEdit(FilterKind::Levels);
    FilterSettings settings;
    settings.levels.ranges[0].gamma = 2.0;
    session.UpdateColorEdit(settings, false);
    const auto input = session.PrepareColorEditCommit();
    Expect(input.has_value(), "prepared-color-levels-input-is-ready");
    if (!input)
        return;

    InlineExecutor executor;
    const ColorEditJobResult result = RunColorEditJob(input.value(), executor);
    Expect(result.status == ColorEditJobStatus::Ready && !result.thumbnail.HasStorage() &&
               EqualsPixels(result.image, RasterFormat::Bgra8888, gammaPixels),
           "prepared-color-levels-keeps-literal-gamma-pixels-without-thumbnail");
    auto prepared = PrepareColorEditPublication(session.Document(), session.ActiveLayerID(), session.Generation(),
                                                session.history.CaptureState(), result);
    Expect(prepared.has_value(), "prepared-color-publication-is-ready");
    if (!prepared)
        return;
    const bool applied =
        session.IsPreparedColorEditCurrent(prepared.value()) && session.ApplyPreparedColorEditResult(prepared.value());
    Expect(applied && session.ActiveLayer() && session.ActiveLayer()->asset &&
               !session.ActiveLayer()->asset->thumbnail.HasStorage() &&
               EqualsPixels(session.ActiveLayer()->asset->image, RasterFormat::Bgra8888, gammaPixels) &&
               EqualsPixels(sourceImage, RasterFormat::Bgra8888, sourcePixels),
           "prepared-color-apply-keeps-empty-thumbnail-and-original-source");
}

void PreparedMergeOutputHasNoImageThumbnail()
{
    constexpr std::array<std::uint8_t, 4> lowerPixels{11, 22, 33, 255};
    constexpr std::array<std::uint8_t, 4> upperPixels{44, 55, 66, 255};
    constexpr std::array<std::uint8_t, 4> compositePixels{77, 88, 99, 255};
    const Raster lowerImage = Bgra(1, 1, lowerPixels);
    const Raster upperImage = Bgra(1, 1, upperPixels);
    CanvasDocument source = ImageDocument(lowerImage);
    ImageLayer upper = ImageLayer::Blank(L"Upper", Vec2{1, 1});
    const Guid active = upper.id;
    upper.asset = RasterAsset{upperImage, Raster{}, upper.name};
    EditDocumentFixture(source, [&](DocumentFixture& document) { document.layers.push_back(std::move(upper)); });
    DocumentSession session;
    session.ReplaceDocument(source, active);
    const auto plan = session.PlanMerge();
    const Raster composite = Bgra(1, 1, compositePixels);
    const auto token = session.CaptureJobToken(std::nullopt, 1);
    DocumentSession::MergePublicationStatus status = DocumentSession::MergePublicationStatus::Invalid;
    auto prepared =
        plan && token
            ? DocumentSession::PrepareMergePublication(session.Document(), session.ActiveLayerID(), token.value(),
                                                       session.history.CaptureState(), plan.value(), composite, status)
            : std::nullopt;
    Expect(prepared.has_value() && status == DocumentSession::MergePublicationStatus::Ready,
           "prepared-merge-publication-is-ready");
    if (!prepared)
        return;
    const bool applied = session.ApplyPreparedMergeIfCurrent(prepared.value());
    Expect(applied && session.ActiveLayer() && session.ActiveLayer()->asset &&
               !session.ActiveLayer()->asset->thumbnail.HasStorage() &&
               EqualsPixels(session.ActiveLayer()->asset->image, RasterFormat::Bgra8888, compositePixels),
           "prepared-merge-keeps-literal-composite-without-thumbnail");
    Expect(EqualsPixels(lowerImage, RasterFormat::Bgra8888, lowerPixels) &&
               EqualsPixels(upperImage, RasterFormat::Bgra8888, upperPixels),
           "prepared-merge-preserves-literal-source-images");
}

void CpuBrushImageCommitHasNoImageThumbnail()
{
    constexpr std::array<std::uint8_t, 4> paintedPixel{0, 0, 255, 255};
    DocumentSession session;
    session.CreateDocument(1, 1, false);
    session.AddBlankLayer();
    session.SetBrushEngine(nullptr); // Explicitly select the CPU brush path.
    session.brushSettings = BrushSettings{1, 1, 1, 0, 0, 1};
    session.SetForegroundColor(0xFFFF0000u);
    const bool began = session.BeginBrush(Vec2{0.5, 0.5});
    session.FinishBrush();
    const ImageLayer* layer = session.ActiveLayer();
    Expect(began && layer && layer->asset && !layer->asset->thumbnail.HasStorage(),
           "cpu-brush-image-commit-keeps-thumbnail-empty");
    Expect(layer && layer->asset && EqualsPixels(layer->asset->image, RasterFormat::Bgra8888, paintedPixel),
           "cpu-brush-image-commit-matches-literal-red-pixel");
}

void CpuMaskBrushCommitKeepsEagerSemanticThumbnail()
{
    constexpr std::array<std::uint8_t, 1> blackMaskPixel{0};
    DocumentSession session;
    session.CreateDocument(1, 1, false);
    session.AddBlankLayer();
    session.AddLayerMask(true);
    Expect(session.ActiveLayer() && session.ActiveLayer()->mask &&
               session.ActiveLayer()->mask->asset.thumbnail.HasStorage() &&
               session.ActiveLayer()->mask->asset.thumbnail.Data()[0] == 255,
           "cpu-mask-brush-starts-from-independent-white-semantic-thumbnail");
    const auto before = session.history.UndoCount();
    session.SetBrushEngine(nullptr); // Keep the semantic mask case on the CPU brush path too.
    session.maskPaintWhite = false;
    session.brushSettings = BrushSettings{1, 1, 1, 0, 0, 1};
    const bool began = session.BeginBrush(Vec2{0.5, 0.5});
    session.FinishBrush();
    const ImageLayer* layer = session.ActiveLayer();
    const Raster* thumbnail = layer && layer->mask ? &layer->mask->asset.thumbnail : nullptr;
    Expect(began && session.history.UndoCount() == before + 1 && thumbnail && thumbnail->HasStorage() &&
               thumbnail->Format() == RasterFormat::Gray8 &&
               EqualsPixels(*thumbnail, RasterFormat::Gray8, blackMaskPixel),
           "cpu-mask-brush-commit-keeps-eager-gray8-thumbnail");
    Expect(thumbnail && MaskEdgeBackground(*thumbnail) == 0.0,
           "cpu-mask-brush-thumbnail-keeps-legacy-black-edge-background");
}
} // namespace

int RunOptionalImageThumbnailTests()
{
    gFailures = 0;
    EmptyImageThumbnailAndHistoryEquality();
    SparseImageThumbnailDoesNotMaterializeSource();
    MaskThumbnailRemainsGrayAndSemantic();
    RasterEditOutputHasNoImageThumbnail();
    ResizeImageOutputHasNoImageThumbnail();
    PreparedColorEditOutputHasNoImageThumbnail();
    PreparedMergeOutputHasNoImageThumbnail();
    CpuBrushImageCommitHasNoImageThumbnail();
    CpuMaskBrushCommitKeepsEagerSemanticThumbnail();
    return gFailures;
}
