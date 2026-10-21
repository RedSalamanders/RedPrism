#include "Document.Tests.CopyPixels.h"

#include "CopyPixelsWork.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <new>
#include <span>
#include <utility>
#include <vector>

namespace
{
int gFailures = 0;

void Expect(bool condition, const char* name) noexcept
{
    std::printf("%s copypixels.%s\n", condition ? "PASS" : "FAIL", name);
    gFailures += condition ? 0 : 1;
}

Raster Bgra(int width, int height, std::span<const std::uint8_t> pixels)
{
    return Raster::Make(width, height, RasterFormat::Bgra8888, pixels.data(), pixels.size());
}

Raster Gray(int width, int height, std::span<const std::uint8_t> pixels)
{
    return Raster::Make(width, height, RasterFormat::Gray8, pixels.data(), pixels.size());
}

template <std::size_t Size> Raster Bgra(int width, int height, const std::array<std::uint8_t, Size>& pixels)
{
    return Bgra(width, height, std::span<const std::uint8_t>{pixels});
}

template <std::size_t Size> Raster Gray(int width, int height, const std::array<std::uint8_t, Size>& pixels)
{
    return Gray(width, height, std::span<const std::uint8_t>{pixels});
}

bool Equals(const Raster& image, int width, int height, std::span<const std::uint8_t> expected) noexcept
{
    if (!image.HasStorage() || image.Width() != width || image.Height() != height ||
        image.Format() != RasterFormat::Bgra8888 ||
        expected.size() != static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u)
        return false;
    const std::uint8_t* pixels = image.Data();
    return pixels && std::equal(expected.begin(), expected.end(), pixels);
}

DocumentSelection Rect(double x, double y, double width, double height, bool antialiased = false)
{
    return DocumentSelection{antialiased, SelectionLoops{{Vec2{x, y}, Vec2{x + width, y}, Vec2{x + width, y + height},
                                                          Vec2{x, y + height}}}};
}

CopyPixelsInput Input(Raster source, int canvasWidth, int canvasHeight)
{
    CopyPixelsInput input;
    input.source = std::move(source);
    input.placement.size = Vec2{static_cast<double>(input.source.Width()), static_cast<double>(input.source.Height())};
    input.placement.sampling = LayerSampling::Nearest;
    input.canvasWidth = canvasWidth;
    input.canvasHeight = canvasHeight;
    return input;
}

bool ReadyEquals(const CopyPixelsInput& input, int width, int height, Vec2 origin,
                 std::span<const std::uint8_t> expected)
{
    const CopyPixelsResult result = RunCopyPixels(input);
    return result.status == CopyPixelsStatus::Ready && result.clipboard.has_value() &&
           result.clipboard->origin == origin && Equals(result.clipboard->image, width, height, expected);
}

void FullCanvasLiteralAndSampling()
{
    constexpr std::array<std::uint8_t, 16> pixels{11, 21,  31,  41,  51,  61,  71,  81,
                                                  91, 101, 111, 121, 131, 141, 151, 161};
    const CopyPixelsInput full = Input(Bgra(2, 2, pixels), 2, 2);
    Expect(ReadyEquals(full, 2, 2, Vec2{0, 0}, pixels), "literal-bgra-full-grid-origin-zero");

    constexpr std::array<std::uint8_t, 8> ramp{10, 20, 30, 255, 30, 40, 50, 255};
    CopyPixelsInput sampled = Input(Bgra(2, 1, ramp), 6, 1);
    sampled.placement.origin = Vec2{1, 0};
    sampled.placement.size = Vec2{4, 1};
    sampled.placement.sampling = LayerSampling::Nearest;
    constexpr std::array<std::uint8_t, 24> nearest{0,  0,  0,  0,   10, 20, 30, 255, 10, 20, 30, 255,
                                                   30, 40, 50, 255, 30, 40, 50, 255, 0,  0,  0,  0};
    Expect(ReadyEquals(sampled, 6, 1, Vec2{0, 0}, nearest), "translated-scaled-nearest-pixel-centers");

    sampled.canvasWidth = 6;
    sampled.placement.sampling = LayerSampling::Smooth;
    constexpr std::array<std::uint8_t, 24> smooth{0,  0,  0,  0,   10, 20, 30, 255, 15, 25, 35, 255,
                                                  25, 35, 45, 255, 30, 40, 50, 255, 0,  0,  0,  0};
    Expect(ReadyEquals(sampled, 6, 1, Vec2{0, 0}, smooth), "translated-scaled-smooth-pixel-centers");
    sampled.placement.sampling = LayerSampling::High;
    Expect(ReadyEquals(sampled, 6, 1, Vec2{0, 0}, smooth), "high-matches-smooth-for-enlargement");
}

void SelectionCropAndCoverage()
{
    constexpr std::array<std::uint8_t, 48> canvas{1,  2,  3,  255, 4,  5,  6,  255, 7,  8,  9,  255, 10, 11, 12, 255,
                                                  13, 14, 15, 255, 16, 17, 18, 255, 19, 20, 21, 255, 22, 23, 24, 255,
                                                  25, 26, 27, 255, 28, 29, 30, 255, 31, 32, 33, 255, 34, 35, 36, 255};
    CopyPixelsInput input = Input(Bgra(4, 3, canvas), 4, 3);
    input.selection = Rect(-1.2, 1.0, 3.2, 1.0);
    constexpr std::array<std::uint8_t, 8> clipped{13, 14, 15, 255, 16, 17, 18, 255};
    Expect(ReadyEquals(input, 2, 1, Vec2{0, 1}, clipped), "selection-clipped-region-exact-origin");

    constexpr std::array<std::uint8_t, 16> solid{100, 50, 25, 200, 100, 50, 25, 200,
                                                 100, 50, 25, 200, 100, 50, 25, 200};
    CopyPixelsInput soft = Input(Bgra(2, 2, solid), 2, 2);
    soft.composite = true;
    soft.selection = Rect(0.25, 0.25, 1.5, 1.5, true);
    // Independent rectangle/pixel intersection: each cell has area 0.75*0.75=0.5625; rounding
    // 255*0.5625 gives coverage 143. Each byte below is rounded from source*143/255, including alpha.
    constexpr std::array<std::uint8_t, 16> covered{56, 28, 14, 112, 56, 28, 14, 112, 56, 28, 14, 112, 56, 28, 14, 112};
    Expect(ReadyEquals(soft, 2, 2, Vec2{0, 0}, covered), "fractional-aa-143-multiplies-bgra-including-alpha");
}

void GrayMaskAndSparseSources()
{
    constexpr std::array<std::uint8_t, 2> maskBytes{55, 201};
    CopyPixelsInput mask = Input(Gray(2, 1, maskBytes), 4, 1);
    mask.mask = true;
    mask.background = 37;
    mask.placement.origin = Vec2{1, 0};
    mask.placement.size = Vec2{2, 1};
    constexpr std::array<std::uint8_t, 16> opaqueMask{37,  37,  37,  255, 55, 55, 55, 255,
                                                      201, 201, 201, 255, 37, 37, 37, 255};
    Expect(ReadyEquals(mask, 4, 1, Vec2{0, 0}, opaqueMask), "gray-mask-opaque-bgra-placed-background-37");

    constexpr std::array<std::uint8_t, 4> gray{80, 160, 80, 160};
    CopyPixelsInput softMask = Input(Gray(2, 2, gray), 2, 2);
    softMask.mask = true;
    softMask.background = 9;
    softMask.selection = Rect(0.25, 0.25, 1.5, 1.5, true);
    constexpr std::array<std::uint8_t, 16> softMaskExpected{45, 45, 45, 143, 90, 90, 90, 143,
                                                            45, 45, 45, 143, 90, 90, 90, 143};
    Expect(ReadyEquals(softMask, 2, 2, Vec2{0, 0}, softMaskExpected), "gray-mask-selection-scales-gray-and-alpha");

    constexpr std::array<std::uint8_t, 8> sparseBaseBytes{7, 8, 9, 255, 17, 18, 19, 255};
    const Raster base = Bgra(2, 1, sparseBaseBytes);
    const Raster sparse = Raster::MakeSparse(4, 2, RasterFormat::Bgra8888, base, 1, 0, {}, 0);
    CopyPixelsInput sparseInput = Input(sparse, 4, 2);
    constexpr std::array<std::uint8_t, 32> sparseExpected{0, 0, 0, 0, 7, 8, 9, 255, 17, 18, 19, 255, 0, 0, 0, 0,
                                                          0, 0, 0, 0, 0, 0, 0, 0,   0,  0,  0,  0,   0, 0, 0, 0};
    const CopyPixelsResult sparseResult = RunCopyPixels(sparseInput);
    Expect(sparseResult.status == CopyPixelsStatus::Ready && sparseResult.clipboard &&
               sparseResult.clipboard->origin == Vec2{0, 0} &&
               Equals(sparseResult.clipboard->image, 4, 2, sparseExpected) && sparse.IsSparse() &&
               !sparse.IsMaterialized(),
           "sparse-source-read-never-materializes-source");
}

void MergedCompositeAndInvalidInputs()
{
    constexpr std::array<std::uint8_t, 48> pixels{1,  2,  3,  255, 4,  5,  6,  255, 7,  8,  9,  255, 10, 11, 12, 255,
                                                  13, 14, 15, 255, 16, 17, 18, 255, 19, 20, 21, 255, 22, 23, 24, 255,
                                                  25, 26, 27, 255, 28, 29, 30, 255, 31, 32, 33, 255, 34, 35, 36, 255};
    const Raster composite = Raster::MakeSparse(4, 3, RasterFormat::Bgra8888, Bgra(4, 3, pixels), 0, 0, {}, 0);
    CopyPixelsInput merged = Input(composite, 4, 3);
    merged.composite = true;
    merged.selection = Rect(1, 1, 2, 1);
    constexpr std::array<std::uint8_t, 8> crop{16, 17, 18, 255, 19, 20, 21, 255};
    const CopyPixelsResult mergedResult = RunCopyPixels(merged);
    Expect(mergedResult.status == CopyPixelsStatus::Ready && mergedResult.clipboard &&
               mergedResult.clipboard->origin == Vec2{1, 1} && Equals(mergedResult.clipboard->image, 2, 1, crop) &&
               !composite.IsMaterialized(),
           "merged-input-sparse-complete-grid-exact-crop");

    CopyPixelsInput invalid = merged;
    invalid.mask = true;
    Expect(RunCopyPixels(invalid).status == CopyPixelsStatus::Invalid, "invalid-mask-composite-combination");
    invalid = Input(Gray(4, 3, std::array<std::uint8_t, 12>{}), 4, 3);
    invalid.composite = true;
    Expect(RunCopyPixels(invalid).status == CopyPixelsStatus::Invalid, "invalid-composite-gray-format");
    invalid = Input(Bgra(2, 2, std::array<std::uint8_t, 16>{}), 4, 3);
    invalid.composite = true;
    Expect(RunCopyPixels(invalid).status == CopyPixelsStatus::Invalid, "invalid-composite-wrong-document-grid");
    invalid = Input(Bgra(1, 1, std::array<std::uint8_t, 4>{}), 4, 3);
    invalid.mask = true;
    Expect(RunCopyPixels(invalid).status == CopyPixelsStatus::Invalid, "invalid-bgra-mask-format");
    invalid = Input(Gray(1, 1, std::array<std::uint8_t, 1>{}), 4, 3);
    Expect(RunCopyPixels(invalid).status == CopyPixelsStatus::Invalid, "invalid-gray-image-format");
    invalid = Input({}, 4, 3);
    Expect(RunCopyPixels(invalid).status == CopyPixelsStatus::Invalid, "invalid-empty-source");
    invalid = Input(Bgra(1, 1, std::array<std::uint8_t, 4>{}), 4, 3);
    invalid.selection = DocumentSelection{};
    Expect(RunCopyPixels(invalid).status == CopyPixelsStatus::Invalid, "invalid-empty-selection-outline");
    invalid.selection = Rect(-4, -4, 1, 1);
    Expect(RunCopyPixels(invalid).status == CopyPixelsStatus::Invalid, "invalid-selection-outside-canvas");
    invalid = Input(Bgra(1, 1, std::array<std::uint8_t, 4>{}), 0, 3);
    Expect(RunCopyPixels(invalid).status == CopyPixelsStatus::Invalid, "invalid-empty-canvas-dimension");
}

struct CancelAt final
{
    CopyPixelsStage target{};
    CancelSource source;
    int hits = 0;

    static void Visit(void* context, CopyPixelsStage stage)
    {
        auto& self = *static_cast<CancelAt*>(context);
        if (stage == self.target)
        {
            ++self.hits;
            (void)self.source.Cancel();
        }
    }
};

void CancellationAndObserverAllocationFailure()
{
    constexpr std::array<std::uint8_t, 16> sourceBytes{1, 2, 3, 255, 4, 5, 6, 255, 7, 8, 9, 255, 10, 11, 12, 255};
    const Raster source = Bgra(2, 2, sourceBytes);
    CopyPixelsInput input = Input(source, 2, 2);
    CancelSource preCancelled;
    (void)preCancelled.Cancel();
    const CopyPixelsResult beforeWork = RunCopyPixels(input, preCancelled.Token());
    Expect(beforeWork.status == CopyPixelsStatus::Cancelled && !beforeWork.clipboard &&
               std::equal(sourceBytes.begin(), sourceBytes.end(), source.Data()),
           "cancel-before-work-empty-result-source-unchanged");

    constexpr std::array<CopyPixelsStage, 4> stages{CopyPixelsStage::Source, CopyPixelsStage::Coverage,
                                                    CopyPixelsStage::Rows, CopyPixelsStage::Publication};
    for (const CopyPixelsStage stage : stages)
    {
        CancelAt cancel{stage};
        const CopyPixelsResult result =
            RunCopyPixels(input, cancel.source.Token(), CopyPixelsObserver{&cancel, &CancelAt::Visit});
        const char* const name = stage == CopyPixelsStage::Source     ? "cancel-source"
                                 : stage == CopyPixelsStage::Coverage ? "cancel-coverage"
                                 : stage == CopyPixelsStage::Rows     ? "cancel-rows"
                                                                      : "cancel-publication";
        Expect(result.status == CopyPixelsStatus::Cancelled && !result.clipboard && cancel.hits == 1 &&
                   std::equal(sourceBytes.begin(), sourceBytes.end(), source.Data()),
               name);
    }

    struct ThrowAtCoverage final
    {
        static void Visit(void*, CopyPixelsStage stage)
        {
            if (stage == CopyPixelsStage::Coverage)
                throw std::bad_alloc{};
        }
    };
    bool propagated = false;
    try
    {
        (void)RunCopyPixels(input, {}, CopyPixelsObserver{nullptr, &ThrowAtCoverage::Visit});
    }
    catch (const std::bad_alloc&)
    {
        propagated = true;
    }
    Expect(propagated && std::equal(sourceBytes.begin(), sourceBytes.end(), source.Data()),
           "observer-bad-alloc-propagates-with-source-unchanged");
}

void MemoryPlannerIndependentLowerFloors()
{
    constexpr std::array<std::uint8_t, 16> sourceBytes{};
    const Raster source = Raster::MakeSparse(2, 2, RasterFormat::Bgra8888, Bgra(2, 2, sourceBytes), 0, 0, {}, 0);
    CopyPixelsInput plain = Input(source, 4, 3);
    const std::uint64_t sourcePrivateFloor = 2u * 2u * 4u;
    const std::uint64_t canvasPlaneFloor = 4u * 3u * 4u;
    const std::uint64_t croppedFloor = 4u * 3u * 4u;
    const std::uint64_t plainFloor = sourcePrivateFloor + canvasPlaneFloor + croppedFloor;
    const std::uint64_t plainPlan = CopyPixelsMemoryBytes(plain);
    Expect(plainPlan >= plainFloor && plainPlan < 64u * 1024u,
           "memory-lower-floor-source-private-canvas-and-cropped-output");

    constexpr std::array<std::uint8_t, 12> grayBytes{};
    CopyPixelsInput mask = Input(Raster::MakeSparse(2, 2, RasterFormat::Gray8,
                                                    Gray(2, 2, std::span<const std::uint8_t>{grayBytes}), 0, 0, {}, 0),
                                 4, 3);
    mask.mask = true;
    const std::uint64_t maskFloor = 2u * 2u + 4u * 3u + 4u * 3u * 4u;
    Expect(CopyPixelsMemoryBytes(mask) >= maskFloor, "memory-lower-floor-gray-source-mask-canvas-and-bgra-crop");

    CopyPixelsInput aa = plain;
    aa.selection = Rect(0.25, 0.25, 2.5, 2.0, true);
    const std::uint64_t aaStorageFloor = plainFloor + 4u * 3u + (4u + 2u) * 3u * 2u * sizeof(float);
    Expect(CopyPixelsMemoryBytes(aa) >= aaStorageFloor, "memory-lower-floor-aa-coverage-and-crossing-scratch");

    std::vector<Vec2> crossing;
    crossing.reserve(32);
    for (int index = 0; index < 16; ++index)
    {
        crossing.emplace_back(0.0, static_cast<double>(index) / 4.0);
        crossing.emplace_back(4.0, static_cast<double>(index) / 4.0 + 0.125);
    }
    CopyPixelsInput hard = plain;
    hard.selection = DocumentSelection{false, SelectionLoops{{std::move(crossing)}}};
    const std::uint64_t hardCrossingFloor = plainFloor + 4u * 3u + 32u * 32u;
    Expect(CopyPixelsMemoryBytes(hard) >= hardCrossingFloor, "memory-lower-floor-hard-edge-crossing-storage-32-points");

    CopyPixelsInput merged = plain;
    merged.source = Raster::MakeSparse(4, 3, RasterFormat::Bgra8888, Raster{}, 0, 0, {}, 0);
    merged.composite = true;
    const std::uint64_t mergedFloor = croppedFloor;
    Expect(CopyPixelsMemoryBytes(merged) >= mergedFloor && CopyPixelsMemoryBytes(merged) < plainPlan,
           "memory-lower-floor-merged-crop-excludes-resampled-source-planes");
    Expect(!source.IsMaterialized(), "memory-planner-does-not-materialize-source");

    // Metadata-only 16 MP inputs make missing planes visible above fixed bookkeeping headroom.
    CopyPixelsInput large =
        Input(Raster::MakeSparse(4000, 4000, RasterFormat::Bgra8888, Raster{}, 0, 0, {}, 0), 4000, 4000);
    const auto largePlan = CopyPixelsMemoryBytes(large);
    constexpr std::uint64_t pixels = 4000ull * 4000ull;
    Expect(largePlan >= pixels * 12u && !large.source.IsMaterialized(),
           "memory-large-source-canvas-output-three-independent-bgra-planes");
    large.selection = Rect(0, 0, 4000, 4000, true);
    const auto aaPlan = CopyPixelsMemoryBytes(large);
    constexpr auto aaFloor = pixels + 4002ull * 4000ull * 2u * sizeof(float);
    Expect(aaPlan >= largePlan + aaFloor && !large.source.IsMaterialized(),
           "memory-large-aa-coverage-plus-two-padded-cell-planes");
}
} // namespace

int RunCopyPixelsTests()
{
    gFailures = 0;
    FullCanvasLiteralAndSampling();
    SelectionCropAndCoverage();
    GrayMaskAndSparseSources();
    MergedCompositeAndInvalidInputs();
    CancellationAndObserverAllocationFailure();
    MemoryPlannerIndependentLowerFloors();
    return gFailures == 0 ? 0 : 1;
}
