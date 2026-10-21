#include "Document.Tests.DocumentSizingWork.h"

#include "../Support/Support.Tests.DocumentFixture.h"
#include "CanvasSize.h"
#include "PixelBuffer.h"
#include "RasterWork.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <span>
#include <utility>

namespace
{
int gFailures = 0;

void Expect(bool condition, const char* name)
{
    std::printf("%s sizingwork.%s\n", condition ? "PASS" : "FAIL", name);
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

bool Equals(const Raster& raster, std::span<const std::uint8_t> expected)
{
    if (!raster.HasStorage())
    {
        return false;
    }
    const Raster contiguous = ContiguousRasterForWork(raster);
    if (!contiguous.HasStorage() ||
        static_cast<std::size_t>(contiguous.Stride()) * static_cast<std::size_t>(contiguous.Height()) !=
            expected.size())
    {
        return false;
    }
    const std::uint8_t* actual = contiguous.Data();
    for (std::size_t index = 0; index < expected.size(); ++index)
    {
        if (actual[index] != expected[index])
        {
            return false;
        }
    }
    return true;
}

std::uint64_t RoundedPayloadBytes(std::uint64_t bytes)
{
    constexpr std::uint64_t sizeClass = 16u * 1024u * 1024u;
    if (bytes == 0 || bytes > (std::numeric_limits<std::uint64_t>::max)() - sizeClass)
    {
        return 0;
    }
    return bytes < sizeClass ? bytes : ((bytes + sizeClass - 1) / sizeClass) * sizeClass;
}

void CanvasExtensionAndAnchor()
{
    constexpr std::array<std::uint8_t, 16> pixels{1, 2, 3, 255, 4, 5, 6, 255, 7, 8, 9, 255, 10, 11, 12, 255};
    DocumentFixture sourceFixture;
    sourceFixture.width = 2;
    sourceFixture.height = 2;
    ImageLayer layer = ImageLayer::Blank(L"Source", Vec2{2, 2});
    layer.asset = RasterAsset{Bgra(2, 2, pixels), {}, layer.name};
    sourceFixture.layers.push_back(layer);
    const CanvasDocument source = std::move(sourceFixture).Seal();

    CanvasSizeOptions options;
    options.width = 4;
    options.height = 3;
    options.anchor = 8;
    options.fill = CanvasExtensionColor{1.0, 0.5, 0.0};
    CanvasDocument result;
    const ResizeStatus status = ResizeCanvas(source, options, result);
    constexpr std::array<std::uint8_t, 48> expected{
        0, 128, 255, 255, 0, 128, 255, 255, 0, 128, 255, 255, 0, 128, 255, 255, 0, 128, 255, 255, 0, 128, 255, 255,
        0, 0,   0,   0,   0, 0,   0,   0,   0, 128, 255, 255, 0, 128, 255, 255, 0, 0,   0,   0,   0, 0,   0,   0};
    const bool placed = result.layers.size() == 2 && result.layers[0].name == L"Canvas Extension" &&
                        result.layers[1].transform.origin == Vec2{2, 1};
    Expect(status == ResizeStatus::Ok && placed && Equals(result.layers[0].asset.value().image, expected),
           "canvas.bottom-right-anchor-keeps-colored-extension-hole");
}

void NearestAndSparseReads()
{
    constexpr std::array<std::uint8_t, 8> bytes{10, 20, 30, 255, 40, 50, 60, 255};
    const Raster dense = Bgra(2, 1, bytes);
    const Raster sparse = Raster::MakeSparse(2, 1, RasterFormat::Bgra8888, dense, 0, 0, {}, 0);
    const Raster rendered = RenderPlacedImage(sparse, Affine2::Scale(2, 2), 4, 2, LayerSampling::Nearest);
    constexpr std::array<std::uint8_t, 32> expected{10, 20, 30, 255, 10, 20, 30, 255, 40, 50, 60, 255, 40, 50, 60, 255,
                                                    10, 20, 30, 255, 10, 20, 30, 255, 40, 50, 60, 255, 40, 50, 60, 255};

    DocumentFixture imageSourceFixture;
    imageSourceFixture.width = 2;
    imageSourceFixture.height = 1;
    ImageLayer imageLayer = ImageLayer::Blank(L"Sparse", Vec2{2, 1});
    imageLayer.asset = RasterAsset{sparse, {}, imageLayer.name};
    imageSourceFixture.layers.push_back(imageLayer);
    const CanvasDocument imageSource = std::move(imageSourceFixture).Seal();
    ImageSizeOptions imageOptions;
    imageOptions.width = 4;
    imageOptions.height = 2;
    imageOptions.sampling = LayerSampling::Nearest;
    CanvasDocument resizedImage;
    const ResizeStatus resizeStatus = ResizeImage(imageSource, imageOptions, resizedImage);

    constexpr std::array<std::uint8_t, 2> maskPixels{25, 200};
    const Raster sparseMask = Raster::MakeSparse(2, 1, RasterFormat::Gray8, Gray(2, 1, maskPixels), 0, 0, {}, 0);
    const Raster mask = RenderPlacedMask(sparseMask, Affine2{}, 3, 1, LayerSampling::Nearest, 9);
    constexpr std::array<std::uint8_t, 3> expectedMask{25, 200, 9};

    Expect(resizeStatus == ResizeStatus::Ok && Equals(resizedImage.layers[0].asset.value().image, expected) &&
               Equals(rendered, expected) && !sparse.IsMaterialized() && Equals(mask, expectedMask) &&
               !sparseMask.IsMaterialized(),
           "image-size-nearest-and-placed-mask-read-sparse-without-publishing-assembly");
}

void CancelledSharedAndNoOpResults()
{
    constexpr std::array<std::uint8_t, 4> pixels{1, 2, 3, 255};
    DocumentFixture sourceFixture;
    sourceFixture.width = 1;
    sourceFixture.height = 1;
    ImageLayer layer = ImageLayer::Blank(L"Shared", Vec2{1, 1});
    const Raster shared = Bgra(1, 1, pixels);
    layer.asset = RasterAsset{shared, shared, layer.name};
    sourceFixture.layers.push_back(layer);
    const CanvasDocument source = std::move(sourceFixture).Seal();

    CancelSource cancelSource;
    (void)cancelSource.Cancel();
    CanvasDocument canvasResult = source;
    CanvasSizeOptions canvasOptions;
    canvasOptions.width = 1;
    canvasOptions.height = 1;
    const ResizeStatus canvasStatus = ResizeCanvas(source, canvasOptions, canvasResult, cancelSource.Token());
    CanvasDocument imageResult = source;
    ImageSizeOptions imageOptions;
    imageOptions.width = 1;
    imageOptions.height = 1;
    const ResizeStatus imageStatus = ResizeImage(source, imageOptions, imageResult, cancelSource.Token());
    const Raster placed = RenderPlacedImage(shared, Affine2{}, 1, 1, LayerSampling::Nearest, cancelSource.Token());
    constexpr std::array<std::uint8_t, 1> maskPixel{128};
    const Raster placedMask =
        RenderPlacedMask(Gray(1, 1, maskPixel), Affine2{}, 1, 1, LayerSampling::Nearest, 0, cancelSource.Token());

    Expect(canvasStatus == ResizeStatus::Cancelled && canvasResult.layers.empty() &&
               imageStatus == ResizeStatus::Cancelled && imageResult.layers.empty() && !placed.HasStorage() &&
               !placedMask.HasStorage() && source.layers[0].asset.value().image == shared && shared.HasStorage(),
           "cancel.pre-cancel-shared-no-op-and-source-preserved");
}

void CheckedDocumentApply()
{
    DocumentSession valid;
    valid.CreateDocument(4, 3);
    const CanvasDocument source = valid.Document().value();
    const auto token = valid.CaptureJobToken();
    CanvasSizeOptions options;
    options.width = 6;
    options.height = 5;
    CanvasDocument resized;
    const ResizeStatus status = ResizeCanvas(source, options, resized);
    const int before = valid.history.UndoCount();
    const bool applied = token.has_value() && status == ResizeStatus::Ok &&
                         valid.ApplyDocumentSizeIfCurrent(std::move(resized), L"Canvas Size", token.value());
    Expect(applied && valid.Document()->width == 6 && valid.Document()->height == 5 &&
               valid.history.UndoCount() == before + 1 && valid.history.UndoName() == L"Canvas Size",
           "checked-apply-valid-result-one-history-entry");
    valid.Undo();
    const bool undoRestored = valid.Document() == std::optional<CanvasDocument>{source} && valid.history.CanRedo();
    valid.Redo();
    Expect(undoRestored && valid.Document()->width == 6 && valid.history.UndoName() == L"Canvas Size",
           "checked-apply-undo-redo-restores-size");

    DocumentSession stale;
    stale.CreateDocument(4, 3, true);
    const auto staleToken = stale.CaptureJobToken();
    CanvasDocument staleResult = stale.Document().value();
    staleResult.width = 7;
    stale.SetOpacity(0.5);
    const auto afterOpacity = stale.Document();
    const int opacityHistory = stale.history.UndoCount();
    const bool opacityApplied =
        staleToken.has_value() &&
        stale.ApplyDocumentSizeIfCurrent(std::move(staleResult), L"Canvas Size", staleToken.value());
    Expect(!opacityApplied && stale.Document() == afterOpacity && stale.history.UndoCount() == opacityHistory &&
               stale.history.UndoName() == L"Layer Opacity",
           "checked-apply-stale-after-opacity-refuses-without-mutation");

    DocumentSession replaced;
    replaced.CreateDocument(4, 3);
    const auto replacedToken = replaced.CaptureJobToken();
    CanvasDocument obsoleteResult = replaced.Document().value();
    obsoleteResult.width = 9;
    DocumentSession replacement;
    replacement.CreateDocument(2, 2);
    replaced.ReplaceDocument(replacement.Document().value(), replacement.ActiveLayerID());
    const auto currentDocument = replaced.Document();
    const int replacementHistory = replaced.history.UndoCount();
    const bool replacedApplied =
        replacedToken.has_value() &&
        replaced.ApplyDocumentSizeIfCurrent(std::move(obsoleteResult), L"Canvas Size", replacedToken.value());
    Expect(!replacedApplied && replaced.Document() == currentDocument &&
               replaced.history.UndoCount() == replacementHistory,
           "checked-apply-replaced-document-refuses-without-mutation");

    DocumentSession invalid;
    invalid.CreateDocument(4, 3);
    const auto invalidToken = invalid.CaptureJobToken();
    CanvasDocument invalidResult = invalid.Document().value();
    invalidResult.width = 0;
    const auto beforeInvalid = invalid.Document();
    const int invalidHistory = invalid.history.UndoCount();
    const bool invalidApplied =
        invalidToken.has_value() &&
        invalid.ApplyDocumentSizeIfCurrent(std::move(invalidResult), L"Canvas Size", invalidToken.value());
    Expect(!invalidApplied && invalid.Document() == beforeInvalid && invalid.history.UndoCount() == invalidHistory,
           "checked-apply-invalid-result-graph-refuses-without-mutation");
}

void SizingReservationLowerBounds()
{
    constexpr std::array<std::uint8_t, 24> imageBytes{1,  2,  3,  255, 4,  5,  6,  255, 7,  8,  9,  255,
                                                      10, 11, 12, 255, 13, 14, 15, 255, 16, 17, 18, 255};
    const Raster dense = Bgra(3, 2, imageBytes);
    const Raster sparse = Raster::MakeSparse(3, 2, RasterFormat::Bgra8888, dense, 0, 0, {}, 0);
    DocumentFixture sourceFixture;
    sourceFixture.width = 3;
    sourceFixture.height = 2;
    ImageLayer layer = ImageLayer::Blank(L"Sparse reservation", Vec2{3, 2});
    layer.asset = RasterAsset{sparse, {}, layer.name};
    sourceFixture.layers.push_back(layer);
    const CanvasDocument source = std::move(sourceFixture).Seal();

    CanvasSizeOptions refusedCanvas;
    refusedCanvas.width = kMaxCanvasSide + 1;
    refusedCanvas.height = 2;
    refusedCanvas.fill = CanvasExtensionColor{};
    const std::uint64_t metadata = SizingMemoryBytes(source, refusedCanvas);
    CanvasSizeOptions canvas;
    canvas.width = 5;
    canvas.height = 4;
    canvas.fill = CanvasExtensionColor{};
    const std::uint64_t canvasBytes = SizingMemoryBytes(source, canvas);
    const std::uint64_t canvasLowerBound = RoundedPayloadBytes(5u * 4u * 4u);

    ImageSizeOptions refusedImage;
    refusedImage.width = 30'001;
    refusedImage.height = 2;
    const std::uint64_t imageMetadata = SizingMemoryBytes(source, refusedImage);
    ImageSizeOptions image;
    image.width = 6;
    image.height = 4;
    image.sampling = LayerSampling::Nearest;
    const std::uint64_t imageBytesReserved = SizingMemoryBytes(source, image);
    const std::uint64_t sparseSourceLowerBound =
        RoundedPayloadBytes(static_cast<std::uint64_t>(sparse.Stride()) * static_cast<std::uint64_t>(sparse.Height()));
    const std::uint64_t imageLowerBound = RoundedPayloadBytes(6u * 4u * 4u) + sparseSourceLowerBound;

    auto multipleSparseFixture = DocumentFixture::Copy(source);
    const Raster largeSparseImage = Raster::MakeSparse(3'000, 3'000, RasterFormat::Bgra8888, {}, 0, 0, {}, 0);
    const Raster smallerSparseMask = Raster::MakeSparse(4'000, 4'000, RasterFormat::Gray8, {}, 0, 0, {}, 0);
    multipleSparseFixture.layers[0].asset->image = largeSparseImage;
    LayerMask sparseMask;
    sparseMask.asset.image = smallerSparseMask;
    multipleSparseFixture.layers[0].mask = sparseMask;
    const CanvasDocument multipleSparse = std::move(multipleSparseFixture).Seal();
    const std::uint64_t multipleSparseMetadata = SizingMemoryBytes(multipleSparse, refusedImage);
    const std::uint64_t multipleSparseReserved = SizingMemoryBytes(multipleSparse, image);
    const std::uint64_t sparseImageBytes =
        RoundedPayloadBytes(static_cast<std::uint64_t>(largeSparseImage.Stride()) * largeSparseImage.Height());
    const std::uint64_t sparseMaskBytes =
        RoundedPayloadBytes(static_cast<std::uint64_t>(smallerSparseMask.Stride()) * smallerSparseMask.Height());
    const std::uint64_t fixedImageBytes =
        RoundedPayloadBytes(6u * 4u * 4u) + RoundedPayloadBytes(6u * 4u) + RoundedPayloadBytes(96u * 96u);
    const std::uint64_t multipleSparseDelta =
        multipleSparseReserved >= multipleSparseMetadata ? multipleSparseReserved - multipleSparseMetadata : 0;

    Expect(metadata > 0 && canvasBytes >= metadata && canvasBytes - metadata >= canvasLowerBound && imageMetadata > 0 &&
               imageMetadata == metadata && imageBytesReserved >= imageMetadata &&
               imageBytesReserved - imageMetadata >= imageLowerBound && !sparse.IsMaterialized() &&
               multipleSparseMetadata > 0 && multipleSparseReserved >= multipleSparseMetadata &&
               multipleSparseDelta >= fixedImageBytes + std::max(sparseImageBytes, sparseMaskBytes) &&
               multipleSparseDelta < fixedImageBytes + sparseImageBytes + sparseMaskBytes,
           "reservation.metadata-rounded-output-mask-thumbnail-max-private-sparse-source-lower-bounds");

    CanvasDocument empty;
    empty.width = 1;
    empty.height = 1;
    CanvasSizeOptions refusedOverflow;
    refusedOverflow.width = (std::numeric_limits<int>::max)();
    refusedOverflow.height = 2;
    refusedOverflow.fill = CanvasExtensionColor{};
    CanvasSizeOptions overflow;
    overflow.width = (std::numeric_limits<int>::max)();
    overflow.height = (std::numeric_limits<int>::max)();
    overflow.fill = CanvasExtensionColor{};
    const std::uint64_t refusedMetadata = SizingMemoryBytes(empty, refusedOverflow);
    Expect(refusedMetadata > 0 && SizingMemoryBytes(empty, overflow) == refusedMetadata &&
               PixelBuffer::RequiredBytes((std::numeric_limits<std::size_t>::max)()) == 0,
           "reservation-refused-bounds-metadata-only-and-rounded-size-overflow-refused");
}

void NonuniformRotationAndUniformMask()
{
    constexpr std::array<std::uint8_t, 16> pixels{20,  40,  60,  255, 80,  100, 120, 255,
                                                  140, 160, 180, 255, 200, 220, 240, 255};
    const Raster image = Bgra(2, 2, pixels);
    DocumentFixture sourceFixture;
    sourceFixture.width = 4;
    sourceFixture.height = 4;
    ImageLayer layer = ImageLayer::Blank(L"Rotated", Vec2{2, 2});
    layer.asset = RasterAsset{image, {}, layer.name};
    layer.transform.origin = Vec2{1, 1};
    layer.transform.rotation = 30;
    LayerMask uniform;
    constexpr std::array<std::uint8_t, 1> gray{173};
    uniform.asset.image = Gray(1, 1, gray);
    layer.mask = uniform;
    sourceFixture.layers.push_back(layer);
    const CanvasDocument source = std::move(sourceFixture).Seal();

    ImageSizeOptions options;
    options.width = 8;
    options.height = 12;
    options.sampling = LayerSampling::Nearest;
    CanvasDocument result;
    const ResizeStatus status = ResizeImage(source, options, result);
    const ImageLayer* resized =
        status == ResizeStatus::Ok && result.layers.size() == 1 ? &result.layers.front() : nullptr;
    const bool uprightBounds = resized != nullptr && resized->transform.rotation == 0 && resized->asset.has_value() &&
                               resized->asset->image.Width() == static_cast<int>(resized->transform.size.x) &&
                               resized->asset->image.Height() == static_cast<int>(resized->transform.size.y) &&
                               resized->transform.size.x != resized->transform.size.y;
    const bool uniformMaskPreserved =
        resized != nullptr && resized->mask.has_value() && !resized->mask->placement.has_value() &&
        resized->mask->asset.image == uniform.asset.image && resized->mask->asset.image.Width() == 1 &&
        resized->mask->asset.image.Height() == 1;
    Expect(status == ResizeStatus::Ok && uprightBounds && uniformMaskPreserved,
           "image-size-nonuniform-rotated-layer-and-uniform-mask");
}
} // namespace

int RunDocumentSizingWorkTests()
{
    gFailures = 0;
    CanvasExtensionAndAnchor();
    NearestAndSparseReads();
    CancelledSharedAndNoOpResults();
    CheckedDocumentApply();
    SizingReservationLowerBounds();
    NonuniformRotationAndUniformMask();
    return gFailures;
}
