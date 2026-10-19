#include "RasterEditWorkTests.h"
#include "../Support/DocumentFixture.h"

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
#if defined(_DEBUG) && !defined(__SANITIZE_ADDRESS__)
#include <atomic>
#include <crtdbg.h>
#endif

namespace
{
int gFailures = 0;

void Expect(bool condition, const char* name)
{
    std::printf("%s rastereditwork.%s\n", condition ? "PASS" : "FAIL", name);
    gFailures += condition ? 0 : 1;
}

#if defined(_DEBUG) && !defined(__SANITIZE_ADDRESS__)
thread_local bool gTrackCrtAllocations = false;
thread_local std::size_t gTrackedCrtAllocationCount = 0;
std::atomic<_CRT_ALLOC_HOOK> gPreviousCrtAllocationHook = nullptr;

int AllocationHook(int allocationType, void* userData, std::size_t size, int blockUse, long requestNumber,
                   const unsigned char* fileName, int lineNumber) noexcept
{
    if (gTrackCrtAllocations && (allocationType == _HOOK_ALLOC || allocationType == _HOOK_REALLOC))
        ++gTrackedCrtAllocationCount;
    const _CRT_ALLOC_HOOK previous = gPreviousCrtAllocationHook.load(std::memory_order_acquire);
    return previous != nullptr ? previous(allocationType, userData, size, blockUse, requestNumber, fileName, lineNumber)
                               : 1;
}

class ScopedCrtAllocationHook final
{
  public:
    ScopedCrtAllocationHook() noexcept : _previous(_CrtGetAllocHook())
    {
        // This test is the sole hook installer. Publish the chain before exposing our callback.
        gPreviousCrtAllocationHook.store(_previous, std::memory_order_release);
        (void)_CrtSetAllocHook(&AllocationHook);
    }
    ~ScopedCrtAllocationHook()
    {
        gTrackCrtAllocations = false;
        (void)_CrtSetAllocHook(_previous);
        // Keep the chain valid for an already-entered callback on another thread after restoration.
    }
    ScopedCrtAllocationHook(const ScopedCrtAllocationHook&) = delete;
    ScopedCrtAllocationHook& operator=(const ScopedCrtAllocationHook&) = delete;

  private:
    _CRT_ALLOC_HOOK _previous;
};

void BeginCrtAllocationMeasurement() noexcept
{
    gTrackedCrtAllocationCount = 0;
    gTrackCrtAllocations = true;
}

std::size_t EndCrtAllocationMeasurement() noexcept
{
    gTrackCrtAllocations = false;
    return gTrackedCrtAllocationCount;
}
#endif

Raster Bgra(int width, int height, std::span<const std::uint8_t> bytes)
{
    return Raster::Make(width, height, RasterFormat::Bgra8888, bytes.data(), bytes.size());
}

Raster Gray(int width, int height, std::span<const std::uint8_t> bytes)
{
    return Raster::Make(width, height, RasterFormat::Gray8, bytes.data(), bytes.size());
}

ImageLayer Image(const Raster& pixels, int width, int height)
{
    ImageLayer layer = ImageLayer::Blank(L"Raster edit", Vec2{static_cast<double>(width), static_cast<double>(height)});
    layer.asset = RasterAsset{pixels, Raster::MakeThumbnail(pixels), L"Raster edit"};
    return layer;
}

DocumentSelection Rect(double x, double y, double width, double height, bool antialiased = false)
{
    return DocumentSelection{antialiased, SelectionLoops{{Vec2{x, y}, Vec2{x + width, y}, Vec2{x + width, y + height},
                                                          Vec2{x, y + height}}}};
}

RasterEditInput Input(ImageLayer layer, int canvasWidth, int canvasHeight, RasterEditKind kind,
                      std::array<double, 4> color = {}, std::optional<DocumentSelection> selection = std::nullopt,
                      bool mask = false, std::int64_t pixelLimit = 100)
{
    RasterEditInput input;
    input.layer = std::move(layer);
    input.selection = std::move(selection);
    input.canvasWidth = canvasWidth;
    input.canvasHeight = canvasHeight;
    input.pixelLimit = pixelLimit;
    input.mask = mask;
    input.kind = kind;
    input.color = color;
    return input;
}

bool PixelsEqual(const Raster& raster, RasterFormat format, int width, int height,
                 std::span<const std::uint8_t> expected)
{
    const std::size_t channels = format == RasterFormat::Gray8 ? 1u : 4u;
    if (!raster.HasStorage() || raster.Format() != format || raster.Width() != width || raster.Height() != height ||
        expected.size() != static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * channels)
        return false;
    const std::uint8_t* const actual = raster.Data();
    return actual != nullptr && std::equal(expected.begin(), expected.end(), actual);
}

bool LayerImageEquals(const RasterEditResult& result, RasterFormat format, int width, int height,
                      std::span<const std::uint8_t> expected)
{
    return result.status == RasterEditStatus::Ready && result.layer && result.layer->asset &&
           PixelsEqual(result.layer->asset->image, format, width, height, expected);
}

bool LayerMaskEquals(const RasterEditResult& result, int width, int height, std::span<const std::uint8_t> expected)
{
    return result.status == RasterEditStatus::Ready && result.layer && result.layer->mask &&
           PixelsEqual(result.layer->mask->asset.image, RasterFormat::Gray8, width, height, expected);
}

struct MidKernelCancel final
{
    CancelSource* source = nullptr;
    std::size_t completedAtCancel = 0;
    std::size_t totalAtCancel = 0;
};

void CancelAfterFirstTile(void* context, std::size_t completed, std::size_t total) noexcept
{
    auto& state = *static_cast<MidKernelCancel*>(context);
    if (completed == 1)
    {
        state.completedAtCancel = completed;
        state.totalAtCancel = total;
        (void)state.source->Cancel();
    }
}

constexpr std::array<std::uint8_t, 16> kOwnerImageBytes{10, 20, 30, 255, 40,  50,  60,  255,
                                                        70, 80, 90, 255, 100, 110, 120, 255};
constexpr std::array<std::uint8_t, 4> kOwnerMaskBytes{16, 64, 144, 240};

bool InitializeOwnerSession(DocumentSession& session, bool withMask, bool withSelection)
{
    session.CreateDocument(2, 2, true);
    const std::optional<Guid> active = session.ActiveLayerID();
    if (!active || !session.Document() || session.Document()->layers.empty())
    {
        Expect(false, "owner.fixture-create-document");
        return false;
    }
    const Raster source = Bgra(2, 2, kOwnerImageBytes);
    if (!source.HasStorage())
    {
        Expect(false, "owner.fixture-image-allocation");
        return false;
    }
    const Raster thumbnail = Raster::MakeThumbnail(source);
    if (!thumbnail.HasStorage())
    {
        Expect(false, "owner.fixture-image-thumbnail");
        return false;
    }
    Raster mask;
    Raster maskThumbnail;
    if (withMask)
    {
        mask = Gray(2, 2, kOwnerMaskBytes);
        if (!mask.HasStorage())
        {
            Expect(false, "owner.fixture-mask-allocation");
            return false;
        }
        maskThumbnail = Raster::MakeThumbnail(mask);
        if (!maskThumbnail.HasStorage())
        {
            Expect(false, "owner.fixture-mask-thumbnail");
            return false;
        }
    }
    EditLayerRecordsForTest(session,
                            [&](DocumentFixture& document)
                            {
                                ImageLayer& layer = document.layers.front();
                                layer.asset = RasterAsset{source, thumbnail, layer.name};
                                if (withMask)
                                {
                                    layer.mask = LayerMask{RasterAsset{mask, maskThumbnail, L"Owner mask"}};
                                }
                                if (withSelection)
                                {
                                    document.selection = Rect(0, 0, 1, 1);
                                }
                            });
    session.SelectLayerTarget(active.value(), withMask);
    if (session.ActiveLayerID() != active || session.IsMaskSelected() != withMask || session.ActiveLayer() == nullptr)
    {
        Expect(false, "owner.fixture-target-selection");
        return false;
    }
    return true;
}

bool PrepareOwnerRasterEdit(DocumentSession& session, bool mask, JobToken& token, ImageLayer& prepared)
{
    const std::optional<Guid> target = session.ActiveLayerID();
    if (!target || !session.Document())
    {
        return false;
    }
    session.SelectLayerTarget(target.value(), mask);
    const std::optional<JobToken> captured = session.CaptureJobToken(target);
    const ImageLayer* const layer = session.ActiveLayer();
    if (!captured || layer == nullptr || !session.Document())
    {
        return false;
    }
    const CanvasDocument& document = session.Document().value();
    const std::array<double, 4> color = mask ? std::array<double, 4>{1, 1, 1, 1} : std::array<double, 4>{1, 0, 0, 1};
    RasterEditResult result = RunRasterEdit(Input(*layer, document.width, document.height, RasterEditKind::Fill, color,
                                                  document.selection, mask, kMaxLayerPixels));
    if (result.status != RasterEditStatus::Ready || !result.layer)
    {
        return false;
    }
    token = captured.value();
    prepared = std::move(result.layer.value());
    return true;
}

bool CurrentImageEquals(const DocumentSession& session, std::span<const std::uint8_t> expected)
{
    const ImageLayer* const layer = session.ActiveLayer();
    return layer != nullptr && layer->asset && PixelsEqual(layer->asset->image, RasterFormat::Bgra8888, 2, 2, expected);
}

bool CurrentMaskEquals(const DocumentSession& session, std::span<const std::uint8_t> expected)
{
    const ImageLayer* const layer = session.ActiveLayer();
    return layer != nullptr && layer->mask &&
           PixelsEqual(layer->mask->asset.image, RasterFormat::Gray8, 2, 2, expected);
}

void ExpectOwnerRefusal(DocumentSession& session, ImageLayer& prepared, bool mask, const JobToken& token,
                        const char* name)
{
    if (!session.Document())
    {
        Expect(false, name);
        return;
    }
    const CanvasDocument before = session.Document().value();
    const int undoCount = session.history.UndoCount();
    const Raster retained = mask ? (prepared.mask ? prepared.mask->asset.image : Raster{})
                                 : (prepared.asset ? prepared.asset->image : Raster{});
    const bool accepted = session.ApplyRasterEditIfCurrent(std::move(prepared), mask, L"Prepared raster edit", token);
    const bool stillOwned = mask ? (prepared.mask && prepared.mask->asset.image == retained)
                                 : (prepared.asset && prepared.asset->image == retained);
    Expect(!accepted && session.Document() && session.Document().value() == before &&
               session.history.UndoCount() == undoCount && stillOwned,
           name);
}

void ImageFillSelectionAndWholeLayer()
{
    constexpr std::array<std::uint8_t, 12> transparent{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    const Raster source = Bgra(3, 1, transparent);
    if (!source.HasStorage())
    {
        Expect(false, "fill.fixture-allocation");
        return;
    }
    const ImageLayer layer = Image(source, 3, 1);
    if (!layer.asset || !layer.asset->thumbnail.HasStorage())
    {
        Expect(false, "fill.thumbnail-fixture");
        return;
    }

    constexpr std::array<std::uint8_t, 12> redSelected{0, 0, 0, 0, 0, 0, 255, 255, 0, 0, 0, 0};
    const auto red = RunRasterEdit(Input(layer, 3, 1, RasterEditKind::Fill, {1, 0, 0, 1}, Rect(1, 0, 1, 1)));
    Expect(LayerImageEquals(red, RasterFormat::Bgra8888, 3, 1, redSelected),
           "fill.foreground-inside-selection-literal-bgra");

    constexpr std::array<std::uint8_t, 12> whiteSelected{255, 255, 255, 255, 0, 0, 0, 0, 0, 0, 0, 0};
    const auto white = RunRasterEdit(Input(layer, 3, 1, RasterEditKind::Fill, {1, 1, 1, 1}, Rect(0, 0, 1, 1)));
    Expect(LayerImageEquals(white, RasterFormat::Bgra8888, 3, 1, whiteSelected),
           "fill.background-inside-selection-literal-bgra");

    constexpr std::array<std::uint8_t, 12> redWhole{0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255};
    const auto redAll = RunRasterEdit(Input(layer, 3, 1, RasterEditKind::Fill, {1, 0, 0, 1}));
    Expect(LayerImageEquals(redAll, RasterFormat::Bgra8888, 3, 1, redWhole),
           "fill.foreground-whole-layer-literal-bgra");

    constexpr std::array<std::uint8_t, 12> whiteWhole{255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255};
    const auto whiteAll = RunRasterEdit(Input(layer, 3, 1, RasterEditKind::Fill, {1, 1, 1, 1}));
    Expect(LayerImageEquals(whiteAll, RasterFormat::Bgra8888, 3, 1, whiteWhole),
           "fill.background-whole-layer-literal-bgra");
}

void ClearAndInvertGoldens()
{
    // Legacy deleteClearsSelectedPixelsOrDeletesTheLayerWithoutASelection: selected center is transparent;
    // untouched red pixels retain their exact premultiplied BGRA bytes.
    constexpr std::array<std::uint8_t, 12> redThree{0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255};
    const Raster redPixels = Bgra(3, 1, redThree);
    if (!redPixels.HasStorage())
    {
        Expect(false, "clear.fixture-allocation");
        return;
    }
    const ImageLayer redLayer = Image(redPixels, 3, 1);
    if (!redLayer.asset || !redLayer.asset->thumbnail.HasStorage())
    {
        Expect(false, "clear.thumbnail-fixture");
        return;
    }
    constexpr std::array<std::uint8_t, 12> cleared{0, 0, 255, 255, 0, 0, 0, 0, 0, 0, 255, 255};
    const auto clear = RunRasterEdit(Input(redLayer, 3, 1, RasterEditKind::Clear, {}, Rect(1, 0, 1, 1)));
    Expect(LayerImageEquals(clear, RasterFormat::Bgra8888, 3, 1, cleared),
           "clear.selected-destination-out-literal-bgra");

    // Legacy invertKeepsTransparencyStaysInSelectionAndWorksOnMasks: alpha 128 is retained while RGB is
    // complemented in premultiplied space (red 128 becomes zero).
    constexpr std::array<std::uint8_t, 12> alphaPixels{10, 20, 30, 255, 20, 40, 80, 128, 30, 60, 90, 255};
    const Raster alphaSource = Bgra(3, 1, alphaPixels);
    if (!alphaSource.HasStorage())
    {
        Expect(false, "invert.fixture-allocation");
        return;
    }
    const ImageLayer alphaLayer = Image(alphaSource, 3, 1);
    if (!alphaLayer.asset || !alphaLayer.asset->thumbnail.HasStorage())
    {
        Expect(false, "invert.thumbnail-fixture");
        return;
    }
    constexpr std::array<std::uint8_t, 12> invertedSelection{10, 20, 30, 255, 108, 88, 48, 128, 30, 60, 90, 255};
    const auto invert = RunRasterEdit(Input(alphaLayer, 3, 1, RasterEditKind::Invert, {}, Rect(1, 0, 1, 1)));
    Expect(LayerImageEquals(invert, RasterFormat::Bgra8888, 3, 1, invertedSelection),
           "invert.selection-preserves-alpha-and-outside-literal-bgra");

    constexpr std::array<std::uint8_t, 12> invertedAll{245, 235, 225, 255, 108, 88, 48, 128, 225, 195, 165, 255};
    const auto invertAll = RunRasterEdit(Input(alphaLayer, 3, 1, RasterEditKind::Invert));
    Expect(LayerImageEquals(invertAll, RasterFormat::Bgra8888, 3, 1, invertedAll),
           "invert.whole-layer-preserves-alpha-literal-bgra");
}

void TransformedCoordinateControls()
{
    // Independent reference: two source pixels span four document pixels. The 1×1 selection covers
    // half of document columns 0 and 1, so the first source pixel (center at document x=1) receives
    // 128/255 coverage and the second pixel center (x=3) remains outside.
    constexpr std::array<std::uint8_t, 8> transparent{0, 0, 0, 0, 0, 0, 0, 0};
    const Raster scaledPixels = Bgra(2, 1, transparent);
    if (!scaledPixels.HasStorage())
    {
        Expect(false, "transform.scaled-antialias-fixture-allocation");
        return;
    }
    ImageLayer scaled = Image(scaledPixels, 4, 1);
    if (!scaled.asset || !scaled.asset->thumbnail.HasStorage())
    {
        Expect(false, "transform.scaled-antialias-thumbnail");
        return;
    }
    scaled.transform.origin = Vec2{0, 0};
    scaled.transform.size = Vec2{4, 1};
    constexpr std::array<std::uint8_t, 8> halfCoverageRed{0, 0, 128, 128, 0, 0, 0, 0};
    const auto antialiased =
        RunRasterEdit(Input(scaled, 4, 1, RasterEditKind::Fill, {1, 0, 0, 1}, Rect(0.5, 0, 1, 1, true)));
    Expect(LayerImageEquals(antialiased, RasterFormat::Bgra8888, 2, 1, halfCoverageRed),
           "transform.scaled-layer-antialiased-selection-half-coverage-literal-bgra");

    // Independent coordinate reference: translated bounds are [1,3], and horizontal flip maps source
    // pixel 1's center to document x=1.5 while source pixel 0 maps to x=2.5. The [1,2) selection
    // therefore inverts only source pixel 1, regardless of its storage order.
    constexpr std::array<std::uint8_t, 8> translatedSource{5, 15, 25, 255, 30, 40, 50, 255};
    const Raster translatedPixels = Bgra(2, 1, translatedSource);
    if (!translatedPixels.HasStorage())
    {
        Expect(false, "transform.translated-flipped-fixture-allocation");
        return;
    }
    ImageLayer translated = Image(translatedPixels, 4, 1);
    if (!translated.asset || !translated.asset->thumbnail.HasStorage())
    {
        Expect(false, "transform.translated-flipped-thumbnail");
        return;
    }
    translated.transform.origin = Vec2{1, 0};
    translated.transform.size = Vec2{2, 1};
    translated.transform.flipX = true;
    constexpr std::array<std::uint8_t, 8> translatedFlippedExpected{5, 15, 25, 255, 225, 215, 205, 255};
    const auto flipped = RunRasterEdit(Input(translated, 4, 1, RasterEditKind::Invert, {}, Rect(1, 0, 1, 1)));
    Expect(LayerImageEquals(flipped, RasterFormat::Bgra8888, 2, 1, translatedFlippedExpected),
           "transform.translated-horizontal-flip-selects-documented-pixel-literal-bgra");
}

void UniformAndPlacedMasks()
{
    constexpr std::array<std::uint8_t, 8> opaque{0, 0, 90, 255, 0, 0, 90, 255};
    const Raster image = Bgra(2, 1, opaque);
    constexpr std::array<std::uint8_t, 1> reveal{255};
    const Raster uniform = Gray(1, 1, reveal);
    if (!image.HasStorage() || !uniform.HasStorage())
    {
        Expect(false, "mask.uniform-fixture-allocation");
        return;
    }
    ImageLayer layer = Image(image, 2, 1);
    if (!layer.asset || !layer.asset->thumbnail.HasStorage())
    {
        Expect(false, "mask.uniform-image-thumbnail");
        return;
    }
    layer.mask = LayerMask{RasterAsset{uniform, Raster::MakeThumbnail(uniform), L"Uniform mask"}};
    if (!layer.mask->asset.thumbnail.HasStorage())
    {
        Expect(false, "mask.uniform-thumbnail");
        return;
    }
    constexpr std::array<std::uint8_t, 2> hidden{0, 255};
    const auto invertUniform = RunRasterEdit(Input(layer, 2, 1, RasterEditKind::Invert, {}, Rect(0, 0, 1, 1), true));
    Expect(LayerMaskEquals(invertUniform, 2, 1, hidden), "mask.uniform-reveal-invert-selection-literal-gray8");

    // Clear on a mask is the captured background grayscale fill, not DestinationOut.
    constexpr std::array<std::uint8_t, 2> capturedBackground{64, 255};
    const auto clearMask =
        RunRasterEdit(Input(layer, 2, 1, RasterEditKind::Clear, {0.25, 0.25, 0.25, 1}, Rect(0, 0, 1, 1), true));
    Expect(LayerMaskEquals(clearMask, 2, 1, capturedBackground), "mask.clear-uses-captured-background-gray8");

    // Independent reference: the unlinked mask has its own transform. A one-pixel selection at document x=1
    // edits mask pixel 0 (whose placed center is x=1.5), leaving mask pixel 1 untouched.
    constexpr std::array<std::uint8_t, 8> fourOpaque{0, 0, 90, 255, 0, 0, 90, 255};
    const Raster placedImage = Bgra(2, 1, fourOpaque);
    constexpr std::array<std::uint8_t, 2> placedBytes{17, 203};
    const Raster placedMask = Gray(2, 1, placedBytes);
    if (!placedImage.HasStorage() || !placedMask.HasStorage())
    {
        Expect(false, "mask.placed-fixture-allocation");
        return;
    }
    ImageLayer placedLayer = Image(placedImage, 4, 1);
    if (!placedLayer.asset || !placedLayer.asset->thumbnail.HasStorage())
    {
        Expect(false, "mask.placed-image-thumbnail");
        return;
    }
    placedLayer.transform.origin = Vec2{0, 0};
    placedLayer.transform.size = Vec2{2, 1};
    LayerTransform placement;
    placement.origin = Vec2{1, 0};
    placement.size = Vec2{2, 1};
    placedLayer.mask =
        LayerMask{RasterAsset{placedMask, Raster::MakeThumbnail(placedMask), L"Placed mask"}, true, placement, false};
    if (!placedLayer.mask->asset.thumbnail.HasStorage())
    {
        Expect(false, "mask.placed-thumbnail");
        return;
    }
    constexpr std::array<std::uint8_t, 2> placedExpected{255, 203};
    const auto placedFill =
        RunRasterEdit(Input(placedLayer, 4, 1, RasterEditKind::Fill, {1, 1, 1, 1}, Rect(1, 0, 1, 1), true));
    Expect(LayerMaskEquals(placedFill, 2, 1, placedExpected), "mask.separate-placement-maps-selection-to-mask-grid");
}

void SparseInputsStayImmutable()
{
    constexpr std::array<std::uint8_t, 8> baseBytes{1, 2, 3, 4, 5, 6, 7, 8};
    constexpr std::array<std::uint8_t, 4> tileBytes{9, 10, 11, 12};
    const Raster base = Bgra(2, 1, baseBytes);
    const Raster tile = Bgra(1, 1, tileBytes);
    if (!base.HasStorage() || !tile.HasStorage())
    {
        Expect(false, "sparse.fixture-allocation");
        return;
    }
    const Raster sparse = Raster::MakeSparse(4, 1, RasterFormat::Bgra8888, base, 0, 0, {RasterTile{3, 0, tile}}, 0x55);
    if (!sparse.HasStorage() || !sparse.IsSparse())
    {
        Expect(false, "sparse.fixture-construction");
        return;
    }
    ImageLayer layer = Image(sparse, 4, 1);
    if (!layer.asset || !layer.asset->thumbnail.HasStorage())
    {
        Expect(false, "sparse.thumbnail-fixture");
        return;
    }
    constexpr std::array<std::uint8_t, 16> expected{1, 2, 3, 4, 5, 6, 7, 8, 0, 0, 255, 255, 9, 10, 11, 12};
    const auto result = RunRasterEdit(Input(layer, 4, 1, RasterEditKind::Fill, {1, 0, 0, 1}, Rect(2, 0, 1, 1)));
    Expect(LayerImageEquals(result, RasterFormat::Bgra8888, 4, 1, expected) && sparse.IsSparse() &&
               !sparse.IsMaterialized() && sparse.Base() == base && sparse.Tiles().size() == 1 &&
               sparse.Tiles()[0].pixels == tile,
           "sparse.base-tile-fill-edit-literal-output-and-source-remains-unmaterialized");
}

void GrowingImageCarriesMask()
{
    constexpr std::array<std::uint8_t, 8> sourceBytes{0, 0, 40, 255, 0, 0, 80, 255};
    const Raster pixels = Bgra(2, 1, sourceBytes);
    constexpr std::array<std::uint8_t, 2> maskBytes{31, 219};
    const Raster maskPixels = Gray(2, 1, maskBytes);
    if (!pixels.HasStorage() || !maskPixels.HasStorage())
    {
        Expect(false, "growth.fixture-allocation");
        return;
    }
    ImageLayer layer = Image(pixels, 4, 1);
    if (!layer.asset || !layer.asset->thumbnail.HasStorage())
    {
        Expect(false, "growth.image-thumbnail");
        return;
    }
    layer.transform.origin = Vec2{1, 0};
    layer.transform.size = Vec2{2, 1};
    layer.mask = LayerMask{RasterAsset{maskPixels, Raster::MakeThumbnail(maskPixels), L"Carried mask"}};
    if (!layer.mask->asset.thumbnail.HasStorage())
    {
        Expect(false, "growth.mask-thumbnail");
        return;
    }
    constexpr std::array<std::uint8_t, 12> grownImage{0, 0, 255, 255, 0, 0, 40, 255, 0, 0, 80, 255};
    constexpr std::array<std::uint8_t, 3> grownMask{255, 31, 219};
    const auto result = RunRasterEdit(Input(layer, 4, 1, RasterEditKind::Fill, {1, 0, 0, 1}, Rect(0, 0, 1, 1)));
    const bool imageMatches = result.status == RasterEditStatus::Ready && result.layer && result.layer->asset &&
                              PixelsEqual(result.layer->asset->image, RasterFormat::Bgra8888, 3, 1, grownImage);
    Expect(imageMatches && LayerMaskEquals(result, 3, 1, grownMask),
           "growth.outside-fill-carries-nonuniform-mask-and-literal-pixels");
}

void StatusCancellationAndBounds()
{
    constexpr std::array<std::uint8_t, 16> pixels{0, 0, 1, 255, 0, 0, 2, 255, 0, 0, 3, 255, 0, 0, 4, 255};
    const Raster raster = Bgra(2, 2, pixels);
    if (!raster.HasStorage())
    {
        Expect(false, "status.fixture-allocation");
        return;
    }
    const ImageLayer layer = Image(raster, 2, 2);
    if (!layer.asset || !layer.asset->thumbnail.HasStorage())
    {
        Expect(false, "status.thumbnail-fixture");
        return;
    }
    constexpr std::array<std::uint8_t, 16> filled{0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255};
    const auto exact = RunRasterEdit(Input(layer, 2, 2, RasterEditKind::Fill, {1, 0, 0, 1}, std::nullopt, false, 4));
    const auto lower = RunRasterEdit(Input(layer, 2, 2, RasterEditKind::Fill, {1, 0, 0, 1}, std::nullopt, false, 3));
    const auto negative =
        RunRasterEdit(Input(layer, 2, 2, RasterEditKind::Fill, {1, 0, 0, 1}, std::nullopt, false, -1));
    Expect(LayerImageEquals(exact, RasterFormat::Bgra8888, 2, 2, filled), "budget.exact-pixel-limit-succeeds-literal");
    Expect(lower.status == RasterEditStatus::TooLarge && !lower.layer,
           "budget.lower-pixel-limit-refuses-without-result");
    Expect(negative.status == RasterEditStatus::Invalid && !negative.layer,
           "budget.negative-pixel-limit-invalid-without-result");

    CancelSource canceled;
    (void)canceled.Cancel();
    const auto stopped = RunRasterEdit(Input(layer, 2, 2, RasterEditKind::Invert), canceled.Token());
    Expect(stopped.status == RasterEditStatus::Cancelled && !stopped.layer, "cancel.pre-cancelled-has-no-result");

    const auto noSelectionClear = RunRasterEdit(Input(layer, 2, 2, RasterEditKind::Clear));
    Expect(noSelectionClear.status == RasterEditStatus::Unchanged && !noSelectionClear.layer,
           "status.clear-without-selection-is-unchanged");
    const auto emptySelection = RunRasterEdit(Input(layer, 2, 2, RasterEditKind::Invert, {}, Rect(1, 1, 0, 0)));
    Expect(emptySelection.status == RasterEditStatus::Unchanged && !emptySelection.layer,
           "status.empty-selection-is-unchanged");
}

void MidKernelCancellationDiscardsPrivateOutput()
{
    // Width 257 crosses the 256-pixel tile boundary, so progress(1, 2) cancels deterministically
    // between kernel tiles without relying on elapsed time or thread scheduling.
    constexpr int width = 257;
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(width) * 4u, 0);
    for (int x = 0; x < width; ++x)
    {
        const std::size_t offset = static_cast<std::size_t>(x) * 4u;
        bytes[offset] = static_cast<std::uint8_t>(x % 251);
        bytes[offset + 1] = 19;
        bytes[offset + 2] = 73;
        bytes[offset + 3] = 255;
    }
    const Raster source = Bgra(width, 1, bytes);
    if (!source.HasStorage())
    {
        Expect(false, "cancel-between-tiles.fixture-allocation");
        return;
    }
    ImageLayer layer = Image(source, width, 1);
    if (!layer.asset || !layer.asset->thumbnail.HasStorage())
    {
        Expect(false, "cancel-between-tiles.thumbnail-fixture");
        return;
    }
    CancelSource cancellation;
    MidKernelCancel progress{&cancellation};
    RasterEditInput input = Input(std::move(layer), width, 1, RasterEditKind::Invert, {}, std::nullopt, false, width);
    input.progress = CancelAfterFirstTile;
    input.progressContext = &progress;
    const void* const sourceIdentity = source.Identity();
    const auto result = RunRasterEdit(input, cancellation.Token());
    const std::uint8_t* const sourceAfter = source.Data();
    const bool unchanged = sourceAfter != nullptr && std::equal(bytes.begin(), bytes.end(), sourceAfter);
    Expect(result.status == RasterEditStatus::Cancelled && !result.layer && progress.completedAtCancel == 1 &&
               progress.totalAtCancel >= 2 && cancellation.Token().IsCancelled(),
           "cancel-between-tiles-reports-cancelled-without-prepared-layer");
    Expect(source.Identity() == sourceIdentity && unchanged,
           "cancel-between-tiles-discards-private-work-and-preserves-source-bytes");
}

void OwnerApplySuccessAndHistory()
{
    constexpr std::array<std::uint8_t, 16> filledImage{0,  0,  255, 255, 40,  50,  60,  255,
                                                       70, 80, 90,  255, 100, 110, 120, 255};
    {
        DocumentSession session;
        if (!InitializeOwnerSession(session, false, true))
            return;
        JobToken token;
        ImageLayer prepared;
        if (!PrepareOwnerRasterEdit(session, false, token, prepared))
        {
            Expect(false, "owner.image-prepare-pure-raster-edit");
            return;
        }
        const int initialUndoCount = session.history.UndoCount();
        const bool accepted = session.ApplyRasterEditIfCurrent(std::move(prepared), false, L"Fill", token);
        Expect(accepted && session.history.UndoCount() == initialUndoCount + 1 &&
                   session.history.UndoName() == L"Fill" && CurrentImageEquals(session, filledImage),
               "owner.image-apply-one-entry-literal-result");
        session.Undo();
        Expect(session.history.UndoCount() == initialUndoCount && session.history.CanRedo() &&
                   CurrentImageEquals(session, kOwnerImageBytes),
               "owner.image-undo-restores-literal-source");
        session.Redo();
        Expect(session.history.UndoCount() == initialUndoCount + 1 && !session.history.CanRedo() &&
                   CurrentImageEquals(session, filledImage),
               "owner.image-redo-restores-literal-prepared-result");
    }
    {
        DocumentSession session;
        if (!InitializeOwnerSession(session, true, true))
            return;
        JobToken token;
        ImageLayer prepared;
        if (!PrepareOwnerRasterEdit(session, true, token, prepared))
        {
            Expect(false, "owner.mask-prepare-pure-raster-edit");
            return;
        }
        constexpr std::array<std::uint8_t, 4> filledMask{255, 64, 144, 240};
        const int initialUndoCount = session.history.UndoCount();
        const bool accepted = session.ApplyRasterEditIfCurrent(std::move(prepared), true, L"Fill Mask", token);
        Expect(accepted && session.history.UndoCount() == initialUndoCount + 1 &&
                   session.history.UndoName() == L"Fill Mask" && CurrentMaskEquals(session, filledMask) &&
                   CurrentImageEquals(session, kOwnerImageBytes),
               "owner.mask-apply-one-entry-literal-mask-and-preserved-image");
        session.Undo();
        Expect(session.history.UndoCount() == initialUndoCount && session.history.CanRedo() &&
                   CurrentMaskEquals(session, kOwnerMaskBytes),
               "owner.mask-undo-restores-literal-source");
        session.Redo();
        Expect(session.history.UndoCount() == initialUndoCount + 1 && !session.history.CanRedo() &&
                   CurrentMaskEquals(session, filledMask),
               "owner.mask-redo-restores-literal-prepared-result");
    }
}

template <typename Mutation>
void CheckOwnerStaleTarget(const char* caseName, bool withMask, bool withSelection, bool maskTarget, Mutation&& mutate)
{
    DocumentSession session;
    if (!InitializeOwnerSession(session, withMask, withSelection))
        return;
    JobToken token;
    ImageLayer prepared;
    if (!PrepareOwnerRasterEdit(session, maskTarget, token, prepared))
    {
        Expect(false, caseName);
        return;
    }
    mutate(session);
    ExpectOwnerRefusal(session, prepared, maskTarget, token, caseName);
}

void OwnerStaleResultRefusals()
{
    CheckOwnerStaleTarget("owner.stale-source-image-refused", false, false, false,
                          [](DocumentSession& session)
                          {
                              constexpr std::array<std::uint8_t, 16> replacement{1, 1, 1, 255, 2, 2, 2, 255,
                                                                                 3, 3, 3, 255, 4, 4, 4, 255};
                              const Raster image = Bgra(2, 2, replacement);
                              const Raster thumbnail = Raster::MakeThumbnail(image);
                              EditLayerRecordsForTest(session,
                                                      [&](DocumentFixture& document)
                                                      {
                                                          document.layers.front().asset->image = image;
                                                          document.layers.front().asset->thumbnail = thumbnail;
                                                      });
                          });
    CheckOwnerStaleTarget("owner.stale-layer-transform-refused", false, false, false,
                          [](DocumentSession& session)
                          {
                              EditLayerRecordsForTest(session, [](DocumentFixture& document)
                                                      { document.layers.front().transform.origin.x += 1.0; });
                          });
    CheckOwnerStaleTarget("owner.stale-mask-enabled-refused", true, false, false,
                          [](DocumentSession& session)
                          {
                              EditLayerRecordsForTest(session, [](DocumentFixture& document)
                                                      { document.layers.front().mask->enabled = false; });
                          });
    CheckOwnerStaleTarget("owner.stale-mask-link-refused", true, false, false,
                          [](DocumentSession& session)
                          {
                              EditLayerRecordsForTest(session, [](DocumentFixture& document)
                                                      { document.layers.front().mask->linked = false; });
                          });
    CheckOwnerStaleTarget("owner.stale-mask-placement-refused", true, false, false,
                          [](DocumentSession& session)
                          {
                              LayerTransform placement;
                              placement.origin = Vec2{0.5, 0};
                              EditLayerRecordsForTest(session, [&](DocumentFixture& document)
                                                      { document.layers.front().mask->placement = placement; });
                          });
    CheckOwnerStaleTarget("owner.stale-mask-image-refused", true, false, false,
                          [](DocumentSession& session)
                          {
                              constexpr std::array<std::uint8_t, 4> replacement{240, 180, 120, 60};
                              const Raster image = Gray(2, 2, replacement);
                              const Raster thumbnail = Raster::MakeThumbnail(image);
                              EditLayerRecordsForTest(session,
                                                      [&](DocumentFixture& document)
                                                      {
                                                          document.layers.front().mask->asset.image = image;
                                                          document.layers.front().mask->asset.thumbnail = thumbnail;
                                                      });
                          });
    CheckOwnerStaleTarget("owner.stale-selection-refused", false, true, false,
                          [](DocumentSession& session)
                          {
                              EditLayerRecordsForTest(session, [](DocumentFixture& document)
                                                      { document.selection = Rect(1, 1, 1, 1); });
                          });
    {
        DocumentSession session;
        if (!InitializeOwnerSession(session, false, false))
            return;
        JobToken token;
        ImageLayer prepared;
        if (!PrepareOwnerRasterEdit(session, false, token, prepared))
        {
            Expect(false, "owner.whole-generation-prepare");
            return;
        }
        EditLayerRecordsForTest(session, [](DocumentFixture& document) { document.resolution = 144; });
        Expect(session.CanApplyJobResult(token, token.sequence),
               "owner.target-token-still-matches-after-unrelated-document-metadata-change");
        ExpectOwnerRefusal(session, prepared, false, token,
                           "owner.whole-generation-unrelated-resolution-refused-without-history");
    }
}

void OwnerForeignSessionTokenRefusal()
{
    DocumentSession sourceSession;
    DocumentSession targetSession;
    if (!InitializeOwnerSession(sourceSession, false, false))
        return;
    const std::optional<Guid> target = sourceSession.ActiveLayerID();
    const std::optional<JobToken> sourceToken = sourceSession.CaptureJobToken(target);
    const std::optional<CanvasDocument>& sourceDocument = sourceSession.Document();
    if (!target || !sourceToken || !sourceDocument)
    {
        Expect(false, "owner.foreign-session-token-fixture");
        return;
    }
    targetSession.ReplaceDocument(sourceDocument.value(), target);
    const ImageLayer* const sourceLayer = sourceSession.ActiveLayer();
    const ImageLayer* const targetLayer = targetSession.ActiveLayer();
    if (!targetSession.Document() || targetSession.ActiveLayerID() != target || sourceLayer == nullptr ||
        targetLayer == nullptr || sourceLayer->asset != targetLayer->asset)
    {
        Expect(false, "owner.foreign-session-matching-document-fixture");
        return;
    }
    JobToken targetToken;
    ImageLayer prepared;
    if (!PrepareOwnerRasterEdit(targetSession, false, targetToken, prepared))
    {
        Expect(false, "owner.foreign-session-target-prepare");
        return;
    }
    const bool sameTarget = sourceToken->target && targetToken.target &&
                            sourceToken->target->layerId == targetToken.target->layerId &&
                            sourceToken->target->image == targetToken.target->image &&
                            sourceToken->target->transform == targetToken.target->transform &&
                            sourceToken->target->mask == targetToken.target->mask;
    Expect(sameTarget && sourceToken->sessionIdentity != targetToken.sessionIdentity,
           "owner.foreign-session-tokens-share-target-but-not-session-identity");
    ExpectOwnerRefusal(targetSession, prepared, false, sourceToken.value(),
                       "owner.foreign-session-token-refused-without-history");
}

void OwnerUndoRedoAndDocumentReplacementRefusals()
{
    {
        DocumentSession session;
        if (!InitializeOwnerSession(session, false, false))
            return;
        session.SetOpacity(0.5);
        JobToken token;
        ImageLayer prepared;
        if (!PrepareOwnerRasterEdit(session, false, token, prepared))
        {
            Expect(false, "owner.undo-redo-prepare");
            return;
        }
        const int undoCount = session.history.UndoCount();
        session.Undo();
        session.Redo();
        const bool restoredSameTarget = session.ActiveLayer() && session.ActiveLayer()->opacity == 0.5;
        Expect(restoredSameTarget && session.Generation() > token.generation,
               "owner.undo-redo-returns-to-same-target-with-new-generation");
        ExpectOwnerRefusal(session, prepared, false, token, "owner.undo-redo-token-refused-without-new-history");
        Expect(session.history.UndoCount() == undoCount, "owner.undo-redo-refusal-keeps-history-count");
    }
    {
        DocumentSession session;
        if (!InitializeOwnerSession(session, false, false))
            return;
        JobToken token;
        ImageLayer prepared;
        if (!PrepareOwnerRasterEdit(session, false, token, prepared))
        {
            Expect(false, "owner.same-id-reopen-prepare");
            return;
        }
        const Guid documentId = session.Document()->id;
        const std::uint64_t serial = session.DocumentSerial();
        CanvasDocument reopened = session.Document().value();
        const std::optional<Guid> active = session.ActiveLayerID();
        session.ReplaceDocument(std::move(reopened), active);
        Expect(session.Document() && session.Document()->id == documentId && session.DocumentSerial() > serial,
               "owner.same-document-id-reopen-advances-document-serial");
        ExpectOwnerRefusal(session, prepared, false, token, "owner.same-id-reopen-token-refused-without-history");
        Expect(session.history.UndoCount() == 0, "owner.same-id-reopen-refusal-keeps-reopened-history-empty");
    }
}

void OwnerActiveTargetRefusals()
{
    {
        DocumentSession session;
        if (!InitializeOwnerSession(session, false, false))
            return;
        ImageLayer other = ImageLayer::Blank(L"Other", Vec2{2, 2});
        const Guid otherId = other.id;
        EditLayerRecordsForTest(session,
                                [&](DocumentFixture& document) { document.layers.push_back(std::move(other)); });
        JobToken token;
        ImageLayer prepared;
        if (!PrepareOwnerRasterEdit(session, false, token, prepared))
        {
            Expect(false, "owner.active-layer-prepare");
            return;
        }
        const std::uint64_t generation = session.Generation();
        session.SelectLayer(otherId);
        Expect(session.Generation() == generation, "owner.active-layer-switch-does-not-edit-document");
        ExpectOwnerRefusal(session, prepared, false, token, "owner.changed-active-layer-refused-without-history");
    }
    {
        DocumentSession session;
        if (!InitializeOwnerSession(session, true, false))
            return;
        JobToken token;
        ImageLayer prepared;
        if (!PrepareOwnerRasterEdit(session, false, token, prepared))
        {
            Expect(false, "owner.active-mask-prepare");
            return;
        }
        const std::uint64_t generation = session.Generation();
        const Guid target = session.ActiveLayerID().value();
        session.SelectLayerTarget(target, true);
        Expect(session.Generation() == generation && session.IsMaskSelected(),
               "owner.mask-target-switch-is-session-state");
        ExpectOwnerRefusal(session, prepared, false, token, "owner.changed-active-mask-target-refused-without-history");
    }
}

using PendingStart = bool (*)(DocumentSession&);

bool StartPendingTransform(DocumentSession& session)
{
    session.BeginTransform();
    return session.PendingTransformEdit().has_value();
}

bool StartPendingGradient(DocumentSession& session)
{
    return session.BeginGradient(Vec2{0.5, 0.5}) && session.PendingGradient() != nullptr;
}

bool StartPendingColorEdit(DocumentSession& session)
{
    session.BeginColorEdit(FilterKind::Exposure);
    return session.HasColorEdit();
}

bool StartPendingBrush(DocumentSession& session)
{
    return session.BeginBrush(Vec2{0.5, 0.5});
}

bool StartPendingWarp(DocumentSession& session)
{
    return session.BeginWarp(Vec2{0.5, 0.5}) && session.PendingWarp() != nullptr;
}

bool StartPendingPixelMove(DocumentSession& session)
{
    return session.BeginPixelMove() && session.PendingPixelMove() != nullptr;
}

void CheckOwnerPendingRefusal(const char* caseName, PendingStart start, bool selection)
{
    DocumentSession session;
    if (!InitializeOwnerSession(session, false, selection))
        return;
    JobToken token;
    ImageLayer prepared;
    if (!PrepareOwnerRasterEdit(session, false, token, prepared))
    {
        Expect(false, caseName);
        return;
    }
    if (!start(session))
    {
        Expect(false, caseName);
        return;
    }
    ExpectOwnerRefusal(session, prepared, false, token, caseName);
}

void OwnerPendingOperationRefusals()
{
    CheckOwnerPendingRefusal("owner.pending-transform-refused-without-history", StartPendingTransform, false);
    CheckOwnerPendingRefusal("owner.pending-gradient-refused-without-history", StartPendingGradient, false);
    CheckOwnerPendingRefusal("owner.pending-color-panel-refused-without-history", StartPendingColorEdit, false);
    CheckOwnerPendingRefusal("owner.pending-brush-refused-without-history", StartPendingBrush, false);
    CheckOwnerPendingRefusal("owner.pending-warp-refused-without-history", StartPendingWarp, false);
    CheckOwnerPendingRefusal("owner.pending-pixel-move-refused-without-history", StartPendingPixelMove, true);
}

void OwnerCandidateValidationRefusals()
{
    {
        DocumentSession session;
        if (!InitializeOwnerSession(session, false, false))
            return;
        JobToken token;
        ImageLayer prepared;
        if (!PrepareOwnerRasterEdit(session, false, token, prepared))
        {
            Expect(false, "owner.unrelated-metadata-prepare");
            return;
        }
        prepared.visible = false;
        ExpectOwnerRefusal(session, prepared, false, token, "owner.candidate-unrelated-visibility-refused");
    }
    {
        DocumentSession session;
        if (!InitializeOwnerSession(session, false, false))
            return;
        JobToken token;
        ImageLayer prepared;
        if (!PrepareOwnerRasterEdit(session, false, token, prepared))
        {
            Expect(false, "owner.invalid-format-prepare");
            return;
        }
        constexpr std::array<std::uint8_t, 4> grayBytes{1, 2, 3, 4};
        const Raster invalidFormat = Gray(2, 2, grayBytes);
        if (!invalidFormat.HasStorage())
        {
            Expect(false, "owner.invalid-format-fixture");
            return;
        }
        prepared.asset->image = invalidFormat;
        ExpectOwnerRefusal(session, prepared, false, token, "owner.candidate-gray-image-refused-without-history");
    }
    {
        DocumentSession session;
        if (!InitializeOwnerSession(session, true, false))
            return;
        JobToken token;
        ImageLayer prepared;
        if (!PrepareOwnerRasterEdit(session, true, token, prepared))
        {
            Expect(false, "owner.changed-mask-candidate-prepare");
            return;
        }
        prepared.mask->linked = false;
        ExpectOwnerRefusal(session, prepared, true, token, "owner.candidate-changed-mask-metadata-refused");
    }
    {
        DocumentSession session;
        if (!InitializeOwnerSession(session, false, false))
            return;
        constexpr int peerSide = 100;
        constexpr int peerCount = 9'999;
        std::vector<std::uint8_t> peerBytes(static_cast<std::size_t>(peerSide * peerSide) * 4u, 0);
        const Raster peerImage = Bgra(peerSide, peerSide, peerBytes);
        if (!peerImage.HasStorage())
        {
            Expect(false, "owner.budget-peer-image-allocation");
            return;
        }
        const Raster peerThumbnail = Raster::MakeThumbnail(peerImage);
        if (!peerThumbnail.HasStorage())
        {
            Expect(false, "owner.budget-peer-thumbnail");
            return;
        }
        EditLayerRecordsForTest(session,
                                [&](DocumentFixture& document)
                                {
                                    document.layers.reserve(static_cast<std::size_t>(peerCount + 1));
                                    for (int index = 0; index < peerCount; ++index)
                                    {
                                        ImageLayer peer = ImageLayer::Blank(L"Budget peer", Vec2{peerSide, peerSide});
                                        peer.asset = RasterAsset{peerImage, peerThumbnail, peer.name};
                                        document.layers.push_back(std::move(peer));
                                    }
                                });
        JobToken token;
        ImageLayer prepared;
        if (!PrepareOwnerRasterEdit(session, false, token, prepared))
        {
            Expect(false, "owner.budget-candidate-prepare");
            return;
        }
        constexpr int candidateWidth = 101;
        constexpr int candidateHeight = 100;
        std::vector<std::uint8_t> candidateBytes(static_cast<std::size_t>(candidateWidth * candidateHeight) * 4u, 0);
        const Raster overBudget = Bgra(candidateWidth, candidateHeight, candidateBytes);
        if (!overBudget.HasStorage())
        {
            Expect(false, "owner.budget-candidate-raster");
            return;
        }
        const Raster overBudgetThumbnail = Raster::MakeThumbnail(overBudget);
        if (!overBudgetThumbnail.HasStorage())
        {
            Expect(false, "owner.budget-candidate-thumbnail");
            return;
        }
        prepared.asset = RasterAsset{overBudget, overBudgetThumbnail, prepared.name};
        ExpectOwnerRefusal(session, prepared, false, token, "owner.candidate-over-remaining-pixel-budget-refused");
    }
    {
        DocumentSession session;
        if (!InitializeOwnerSession(session, true, false))
            return;
        constexpr int peerSide = 100;
        constexpr int peerCount = 9'999;
        std::vector<std::uint8_t> peerBytes(static_cast<std::size_t>(peerSide * peerSide), 0);
        const Raster peerMask = Gray(peerSide, peerSide, peerBytes);
        if (!peerMask.HasStorage())
        {
            Expect(false, "owner.mask-budget-peer-allocation");
            return;
        }
        const Raster peerThumbnail = Raster::MakeThumbnail(peerMask);
        if (!peerThumbnail.HasStorage())
        {
            Expect(false, "owner.mask-budget-peer-thumbnail");
            return;
        }
        EditLayerRecordsForTest(session,
                                [&](DocumentFixture& document)
                                {
                                    document.layers.reserve(static_cast<std::size_t>(peerCount + 1));
                                    for (int index = 0; index < peerCount; ++index)
                                    {
                                        ImageLayer peer =
                                            ImageLayer::Blank(L"Mask budget peer", Vec2{peerSide, peerSide});
                                        peer.mask = LayerMask{RasterAsset{peerMask, peerThumbnail, L"Peer mask"}};
                                        document.layers.push_back(std::move(peer));
                                    }
                                });
        JobToken token;
        ImageLayer prepared;
        if (!PrepareOwnerRasterEdit(session, true, token, prepared))
        {
            Expect(false, "owner.mask-budget-candidate-prepare");
            return;
        }
        constexpr int candidateWidth = 101;
        constexpr int candidateHeight = 100;
        std::vector<std::uint8_t> candidateBytes(static_cast<std::size_t>(candidateWidth * candidateHeight), 0);
        const Raster overBudget = Gray(candidateWidth, candidateHeight, candidateBytes);
        if (!overBudget.HasStorage())
        {
            Expect(false, "owner.mask-budget-candidate-raster");
            return;
        }
        const Raster overBudgetThumbnail = Raster::MakeThumbnail(overBudget);
        if (!overBudgetThumbnail.HasStorage())
        {
            Expect(false, "owner.mask-budget-candidate-thumbnail");
            return;
        }
        prepared.mask->asset.image = overBudget;
        prepared.mask->asset.thumbnail = overBudgetThumbnail;
        ExpectOwnerRefusal(session, prepared, true, token, "owner.candidate-over-remaining-mask-budget-refused");
    }
}

void InvalidOptionsAndResourceFloors()
{
    constexpr std::array<std::uint8_t, 16> pixels{0, 0, 1, 255, 0, 0, 2, 255, 0, 0, 3, 255, 0, 0, 4, 255};
    const Raster raster = Bgra(2, 2, pixels);
    if (!raster.HasStorage())
    {
        Expect(false, "invalid.fixture-allocation");
        return;
    }
    const ImageLayer layer = Image(raster, 2, 2);
    if (!layer.asset || !layer.asset->thumbnail.HasStorage())
    {
        Expect(false, "invalid.thumbnail-fixture");
        return;
    }
    const auto invalid = [](const RasterEditInput& input)
    {
        const auto result = RunRasterEdit(input);
        return result.status == RasterEditStatus::Invalid && !result.layer;
    };
    auto bad = Input(layer, 0, 2, RasterEditKind::Invert);
    Expect(invalid(bad), "invalid.zero-canvas-dimension");
    bad = Input(layer, 2, -1, RasterEditKind::Invert);
    Expect(invalid(bad), "invalid.negative-canvas-dimension");
    bad = Input(layer, kMaxCanvasSide + 1, 1, RasterEditKind::Invert);
    Expect(invalid(bad), "invalid.canvas-side-over-limit");
    bad = Input(layer, 2, 2, RasterEditKind::Invert, {}, std::nullopt, false,
                static_cast<std::int64_t>(kMaxLayerPixels) + 1);
    Expect(invalid(bad), "invalid.pixel-budget-over-hard-limit");
    bad = Input(layer, 2, 2, static_cast<RasterEditKind>(255));
    Expect(invalid(bad), "invalid.unknown-edit-kind");
    bad = Input(layer, 2, 2, RasterEditKind::Fill, {0, std::numeric_limits<double>::quiet_NaN(), 0, 1});
    Expect(invalid(bad), "invalid.nan-color-option");
    bad = Input(layer, 2, 2, RasterEditKind::Fill, {1.01, 0, 0, 1});
    Expect(invalid(bad), "invalid.out-of-range-color-option");
    bad = Input(layer, 2, 2, RasterEditKind::Invert, {}, std::nullopt, true);
    Expect(invalid(bad), "invalid.missing-mask-target");
    ImageLayer wrongFormat = layer;
    constexpr std::array<std::uint8_t, 4> gray{1, 2, 3, 4};
    const Raster wrong = Gray(2, 2, gray);
    if (!wrong.HasStorage())
    {
        Expect(false, "invalid.gray-fixture-allocation");
        return;
    }
    wrongFormat.asset->image = wrong;
    bad = Input(wrongFormat, 2, 2, RasterEditKind::Invert);
    Expect(invalid(bad), "invalid.image-target-rejects-gray8");
    ImageLayer wrongMaskFormat = layer;
    wrongMaskFormat.mask = LayerMask{RasterAsset{raster, Raster::MakeThumbnail(raster), L"Invalid BGRA mask"}};
    if (!wrongMaskFormat.mask->asset.thumbnail.HasStorage())
    {
        Expect(false, "invalid.mask-thumbnail");
        return;
    }
    bad = Input(wrongMaskFormat, 2, 2, RasterEditKind::Invert, {}, std::nullopt, true);
    Expect(invalid(bad), "invalid.mask-target-rejects-bgra");
    ImageLayer badTransform = layer;
    badTransform.transform.size.x = std::numeric_limits<double>::quiet_NaN();
    bad = Input(badTransform, 2, 2, RasterEditKind::Invert);
    Expect(invalid(bad), "invalid.nonfinite-transform");

    const std::uint64_t base = RasterEditMemoryBytes(layer, std::nullopt, 2, 2, 4, false);
    constexpr std::array<std::uint8_t, 32> largerFixture{};
    const Raster largerRaster = Bgra(4, 2, largerFixture);
    if (!largerRaster.HasStorage())
    {
        Expect(false, "memory.large-raster-allocation");
        return;
    }
    ImageLayer bigger = Image(largerRaster, 4, 2);
    if (!bigger.asset || !bigger.asset->image.HasStorage() || !bigger.asset->thumbnail.HasStorage())
    {
        Expect(false, "memory.large-fixture-allocation");
        return;
    }
    const std::uint64_t larger = RasterEditMemoryBytes(bigger, std::nullopt, 4, 2, 8, false);
    const std::uint64_t selected = RasterEditMemoryBytes(layer, Rect(0, 0, 1, 1), 2, 2, 4, false);
    ImageLayer withMask = layer;
    constexpr std::array<std::uint8_t, 4> nonuniform{10, 80, 170, 240};
    const Raster mask = Gray(2, 2, nonuniform);
    if (!mask.HasStorage())
    {
        Expect(false, "memory.mask-fixture-allocation");
        return;
    }
    withMask.mask = LayerMask{RasterAsset{mask, Raster::MakeThumbnail(mask), L"Memory mask"}};
    if (!withMask.mask->asset.thumbnail.HasStorage())
    {
        Expect(false, "memory.mask-thumbnail");
        return;
    }
    const std::uint64_t carried = RasterEditMemoryBytes(withMask, std::nullopt, 2, 2, 4, false);
    const std::uint64_t sparse = RasterEditMemoryBytes(layer, std::nullopt, 2, 2, 4, false);
    Expect(base >= 1024u * 1024u && larger > base, "memory.fixed-floor-and-image-pixel-monotonicity");
    Expect(selected > base, "memory.selection-coverage-increases-reservation");
    Expect(carried > sparse, "memory.carried-nonuniform-mask-increases-reservation");
    Expect(RasterEditMemoryBytes(layer, std::nullopt, 2, 2, -1, false) == 0 &&
               RasterEditMemoryBytes(layer, std::nullopt, 0, 2, 4, false) == 0,
           "memory.invalid-inputs-have-no-admissible-reservation");
}

void PreflightAllocationControls()
{
    constexpr std::array<std::uint8_t, 16> imageBytes{3, 7, 11, 255, 13, 17, 19, 255, 23, 29, 31, 255, 37, 41, 43, 255};
    constexpr std::array<std::uint8_t, 4> maskBytes{17, 73, 151, 239};
    const Raster image = Bgra(2, 2, imageBytes);
    const Raster mask = Gray(2, 2, maskBytes);
    if (!image.HasStorage() || !mask.HasStorage())
    {
        Expect(false, "preflight.fixtures-have-storage");
        return;
    }

    const ImageLayer imageLayer = Image(image, 2, 2);
    if (!imageLayer.asset || !imageLayer.asset->thumbnail.HasStorage())
    {
        Expect(false, "preflight.image-thumbnail-fixture");
        return;
    }
    ImageLayer unplacedMaskLayer = imageLayer;
    const Raster maskThumbnail = Raster::MakeThumbnail(mask);
    if (!maskThumbnail.HasStorage())
    {
        Expect(false, "preflight.mask-thumbnail-fixture");
        return;
    }
    unplacedMaskLayer.mask = LayerMask{RasterAsset{mask, maskThumbnail, L"Preflight mask"}};
    ImageLayer placedMaskLayer = unplacedMaskLayer;
    LayerTransform placement;
    placement.origin = Vec2{0.5, 0};
    placement.size = Vec2{2, 2};
    placedMaskLayer.mask->placement = placement;
    const ImageLayer blankLayer = ImageLayer::Blank(L"Blank preflight", Vec2{2, 2});
    const DocumentSelection selection = Rect(0, 0, 1, 1);
    const std::uint8_t* const imageDataBefore = imageLayer.asset->image.Data();
    const std::uint8_t* const maskDataBefore = unplacedMaskLayer.mask->asset.image.Data();
    const std::uint8_t* const placedMaskDataBefore = placedMaskLayer.mask->asset.image.Data();
    const auto imageIdentityBefore = imageLayer.asset->image.Identity();
    const auto maskIdentityBefore = unplacedMaskLayer.mask->asset.image.Identity();
    const auto placedMaskIdentityBefore = placedMaskLayer.mask->asset.image.Identity();

    // The measured budget functions have not run on these fixtures. A one-time lazy allocation must count.
    Expect(image.HasStorage() && mask.HasStorage() && imageLayer.asset && unplacedMaskLayer.mask &&
               placedMaskLayer.mask && !blankLayer.asset,
           "preflight.cold-source-fixtures-ready-before-first-memory-query");

    std::uint64_t imageBudget = 0;
    std::uint64_t moveBudget = 0;
    std::uint64_t unplacedMaskBudget = 0;
    std::uint64_t placedMaskBudget = 0;
    std::uint64_t blankBudget = 0;
    std::uint64_t invalidBudget = 0;
    std::uint64_t blankMoveBudget = 0;
#if defined(_DEBUG) && !defined(__SANITIZE_ADDRESS__)
    std::size_t hookProbeAllocations = 0;
    std::size_t imageAllocations = 0;
    std::size_t moveAllocations = 0;
    std::size_t unplacedMaskAllocations = 0;
    std::size_t placedMaskAllocations = 0;
    std::size_t blankAllocations = 0;
    std::size_t invalidAllocations = 0;
    std::size_t blankMoveAllocations = 0;
    {
        ScopedCrtAllocationHook hook;
        std::vector<int> hookProbe;
        BeginCrtAllocationMeasurement();
        hookProbe.reserve(32);
        hookProbeAllocations = EndCrtAllocationMeasurement();

        BeginCrtAllocationMeasurement();
        imageBudget = RasterEditMemoryBytes(imageLayer, std::nullopt, 2, 2, 4, false);
        imageAllocations = EndCrtAllocationMeasurement();
        BeginCrtAllocationMeasurement();
        moveBudget = PixelMoveMemoryBytes(imageLayer, selection, 2, 2, 4);
        moveAllocations = EndCrtAllocationMeasurement();
        BeginCrtAllocationMeasurement();
        unplacedMaskBudget = RasterEditMemoryBytes(unplacedMaskLayer, std::nullopt, 2, 2, 4, true);
        unplacedMaskAllocations = EndCrtAllocationMeasurement();
        BeginCrtAllocationMeasurement();
        placedMaskBudget = RasterEditMemoryBytes(placedMaskLayer, std::nullopt, 2, 2, 4, true);
        placedMaskAllocations = EndCrtAllocationMeasurement();
        BeginCrtAllocationMeasurement();
        blankBudget = RasterEditMemoryBytes(blankLayer, std::nullopt, 2, 2, 4, false);
        blankAllocations = EndCrtAllocationMeasurement();
        BeginCrtAllocationMeasurement();
        invalidBudget = RasterEditMemoryBytes(imageLayer, std::nullopt, 0, 2, 4, false);
        invalidAllocations = EndCrtAllocationMeasurement();
        BeginCrtAllocationMeasurement();
        blankMoveBudget = PixelMoveMemoryBytes(blankLayer, selection, 2, 2, 4);
        blankMoveAllocations = EndCrtAllocationMeasurement();
    }
#else
    imageBudget = RasterEditMemoryBytes(imageLayer, std::nullopt, 2, 2, 4, false);
    moveBudget = PixelMoveMemoryBytes(imageLayer, selection, 2, 2, 4);
    unplacedMaskBudget = RasterEditMemoryBytes(unplacedMaskLayer, std::nullopt, 2, 2, 4, true);
    placedMaskBudget = RasterEditMemoryBytes(placedMaskLayer, std::nullopt, 2, 2, 4, true);
    blankBudget = RasterEditMemoryBytes(blankLayer, std::nullopt, 2, 2, 4, false);
    invalidBudget = RasterEditMemoryBytes(imageLayer, std::nullopt, 0, 2, 4, false);
    blankMoveBudget = PixelMoveMemoryBytes(blankLayer, selection, 2, 2, 4);
#endif

    Expect(imageBudget > 0 && moveBudget > 0 && unplacedMaskBudget > 0 && placedMaskBudget > 0 && blankBudget > 0,
           "preflight.valid-image-masks-and-blank-geometry-have-positive-budgets");
    Expect(invalidBudget == 0 && blankMoveBudget == 0, "preflight.invalid-and-blank-pixel-move-geometry-refuse-budget");
    Expect(imageLayer.asset->image.Data() == imageDataBefore &&
               unplacedMaskLayer.mask->asset.image.Data() == maskDataBefore &&
               placedMaskLayer.mask->asset.image.Data() == placedMaskDataBefore &&
               imageLayer.asset->image.Identity() == imageIdentityBefore &&
               unplacedMaskLayer.mask->asset.image.Identity() == maskIdentityBefore &&
               placedMaskLayer.mask->asset.image.Identity() == placedMaskIdentityBefore &&
               std::equal(imageBytes.begin(), imageBytes.end(), imageLayer.asset->image.Data()) &&
               std::equal(maskBytes.begin(), maskBytes.end(), unplacedMaskLayer.mask->asset.image.Data()) &&
               std::equal(maskBytes.begin(), maskBytes.end(), placedMaskLayer.mask->asset.image.Data()),
           "preflight.source-raster-storage-and-pixels-unchanged");
#if defined(_DEBUG) && !defined(__SANITIZE_ADDRESS__)
    Expect(hookProbeAllocations > 0, "preflight.debug-hook-observes-vector-reserve");
    Expect(hookProbeAllocations > 0 && imageAllocations == 0,
           "preflight.debug-image-budget-window-has-no-crt-allocations");
    Expect(hookProbeAllocations > 0 && moveAllocations == 0,
           "preflight.debug-pixel-move-budget-window-has-no-crt-allocations");
    Expect(hookProbeAllocations > 0 && unplacedMaskAllocations == 0,
           "preflight.debug-unplaced-mask-budget-window-has-no-crt-allocations");
    Expect(hookProbeAllocations > 0 && placedMaskAllocations == 0,
           "preflight.debug-placed-mask-budget-window-has-no-crt-allocations");
    Expect(hookProbeAllocations > 0 && blankAllocations == 0 && invalidAllocations == 0 && blankMoveAllocations == 0,
           "preflight.debug-blank-and-invalid-budget-windows-have-no-crt-allocations");
#endif
}
} // namespace

int RunRasterEditWorkTests()
{
    gFailures = 0;
    ImageFillSelectionAndWholeLayer();
    ClearAndInvertGoldens();
    TransformedCoordinateControls();
    UniformAndPlacedMasks();
    SparseInputsStayImmutable();
    GrowingImageCarriesMask();
    StatusCancellationAndBounds();
    MidKernelCancellationDiscardsPrivateOutput();
    OwnerApplySuccessAndHistory();
    OwnerStaleResultRefusals();
    OwnerForeignSessionTokenRefusal();
    OwnerUndoRedoAndDocumentReplacementRefusals();
    OwnerActiveTargetRefusals();
    OwnerPendingOperationRefusals();
    OwnerCandidateValidationRefusals();
    InvalidOptionsAndResourceFloors();
    PreflightAllocationControls();
    return gFailures;
}
