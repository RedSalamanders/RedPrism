#include "Document.Tests.FilterCancellation.h"

#include "Adjust.h"
#include "ImageFilters.h"
#include "RasterWork.h"
#include "Selection.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <span>
#include <vector>

namespace
{
int gFailures = 0;

void Expect(bool condition, const char* name)
{
    std::printf("%s filtercancel.%s\n", condition ? "PASS" : "FAIL", name);
    gFailures += condition ? 0 : 1;
}

Raster Gray(int width, int height, std::span<const std::uint8_t> bytes)
{
    return Raster::Make(width, height, RasterFormat::Gray8, bytes.data(), bytes.size());
}

Raster Bgra(int width, int height, std::span<const std::uint8_t> bytes)
{
    return Raster::Make(width, height, RasterFormat::Bgra8888, bytes.data(), bytes.size());
}

bool Matches(const Raster& image, int width, int height, RasterFormat format, std::span<const std::uint8_t> expected)
{
    const bool metadataMatches =
        image.HasStorage() && image.Width() == width && image.Height() == height && image.Format() == format;
    const PixelBuffer pixels = image.HasStorage() ? CopyRasterPixelsForWork(image) : PixelBuffer{};
    const bool bytesMatch =
        pixels && pixels.Size() == expected.size() && std::equal(expected.begin(), expected.end(), pixels.Data());
    if (!metadataMatches || !bytesMatch)
    {
        std::printf("  raster expected=%dx%d format=%u bytes=[", width, height, static_cast<unsigned>(format));
        for (std::size_t index = 0; index < expected.size(); ++index)
        {
            std::printf("%s%u", index == 0 ? "" : ",", static_cast<unsigned>(expected[index]));
        }
        std::printf("] actual=%dx%d format=%u bytes=[", image.Width(), image.Height(),
                    static_cast<unsigned>(image.Format()));
        if (pixels)
        {
            for (std::size_t index = 0; index < pixels.Size(); ++index)
            {
                std::printf("%s%u", index == 0 ? "" : ",", static_cast<unsigned>(pixels.Data()[index]));
            }
        }
        else
        {
            std::printf("unavailable");
        }
        std::printf("]\n");
        return false;
    }
    return true;
}

void PreCancelledPublicOperations()
{
    constexpr std::array<std::uint8_t, 16> sourceBytes{20, 30, 40,  255, 50,  60,  70,  255,
                                                       80, 90, 100, 255, 110, 120, 130, 255};
    constexpr std::array<std::uint8_t, 4> maskBytes{255, 255, 255, 255};
    const Raster source = Bgra(2, 2, sourceBytes);
    const Raster mask = Gray(2, 2, maskBytes);
    CancelSource cancelSource;
    (void)cancelSource.Cancel();
    const CancelToken cancellation = cancelSource.Token();

    constexpr std::array<FilterKind, 11> kinds{
        FilterKind::GaussianBlur,   FilterKind::MotionBlur,       FilterKind::AddNoise,
        FilterKind::LensCorrection, FilterKind::ContentAwareFill, FilterKind::Curves,
        FilterKind::Exposure,       FilterKind::GradientMap,      FilterKind::Grain,
        FilterKind::Levels,         FilterKind::HueSaturation};
    bool filtersCancelled = true;
    bool noSourceRemainsFalse = true;
    for (const FilterKind kind : kinds)
    {
        bool noSource = true;
        const Raster result =
            Adjust::RunFilter(kind, source, FilterSettings{}, 1, 17,
                              kind == FilterKind::ContentAwareFill ? &mask : nullptr, &noSource, cancellation);
        filtersCancelled = filtersCancelled && !result.HasStorage();
        noSourceRemainsFalse = noSourceRemainsFalse && !noSource;
    }
    Expect(filtersCancelled && noSourceRemainsFalse, "cancel.all-eleven-filter-kinds-discard-and-clear-no-source");

    const auto histogram = Adjust::Histogram(source, nullptr, cancellation);
    const std::vector<float> cube = Adjust::ColorCube(HueSaturationSettings{}, cancellation);
    bool emptyHistogram = true;
    for (const auto& channel : histogram)
    {
        emptyHistogram =
            emptyHistogram && std::all_of(channel.begin(), channel.end(), [](double value) { return value == 0; });
    }
    const Raster blended = Adjust::BlendThrough(source, source, mask, cancellation);
    const Raster downscaled = Adjust::Downscaled(source, 1, cancellation);
    const Adjust::Trimmed trimmed = Adjust::TrimmedToAlpha(source, LayerTransform{}, cancellation);
    const Raster gaussian = Adjust::GaussianBlurred(source, 1, cancellation);
    const Raster motion = Adjust::MotionBlurred(source, 0, 2, cancellation);
    const Raster gaussianFilter = ImageFilters::GaussianBlur(source, 1, cancellation);
    const Raster coverage = SelectionGeometry::Coverage(SelectionGeometry::Rect(0, 0, 2, 2), 2, 2, true, cancellation);
    const Raster transformedCoverage = SelectionGeometry::CoverageTransformed(SelectionGeometry::Rect(0, 0, 2, 2),
                                                                              Affine2{}, 2, 2, true, cancellation);
    Expect(emptyHistogram && cube.empty() && !blended.HasStorage() && !downscaled.HasStorage() &&
               !trimmed.image.HasStorage() && !gaussian.HasStorage() && !motion.HasStorage() &&
               !gaussianFilter.HasStorage() && !coverage.HasStorage() && !transformedCoverage.HasStorage(),
           "cancel.histogram-and-raster-producing-helpers");
}

void DonorlessFillAndDeterministicPlumbing()
{
    constexpr std::array<std::uint8_t, 16> transparentBytes{};
    constexpr std::array<std::uint8_t, 4> selectedBytes{255, 255, 255, 255};
    const Raster transparent = Bgra(2, 2, transparentBytes);
    const Raster selected = Gray(2, 2, selectedBytes);
    bool noSource = false;
    const Raster noDonors =
        Adjust::RunFilter(FilterKind::ContentAwareFill, transparent, FilterSettings{}, 1, 1, &selected, &noSource);
    Expect(!noDonors.HasStorage() && noSource, "fill.donorless-is-distinct-from-cancellation");

    constexpr std::array<std::uint8_t, 16> initial{128, 128, 128, 255, 128, 128, 128, 255,
                                                   128, 128, 128, 255, 128, 128, 128, 255};
    std::array<std::uint8_t, initial.size()> directNoise = initial;
    Adjust::AddNoise(directNoise.data(), 2, 2, 8, 10, false, false, 23);
    FilterSettings noiseSettings;
    noiseSettings.amount = 10;
    const Raster throughFilter = Adjust::RunFilter(FilterKind::AddNoise, Bgra(2, 2, initial), noiseSettings, 1, 23);
    Expect(Matches(throughFilter, 2, 2, RasterFormat::Bgra8888, directNoise),
           "noise.same-seed-bytes-match-direct-kernel-reference");

    constexpr std::array<std::uint8_t, 16> coordinateBytes{0, 0,  200, 255, 30, 0,  180, 255,
                                                           0, 30, 200, 255, 30, 30, 180, 255};
    const Raster coordinateSource = Bgra(2, 2, coordinateBytes);
    FilterSettings grainSettings;
    grainSettings.grain = GrainSettings{60, 2, 40, 9};
    const Raster viaFilter = Adjust::RunFilter(FilterKind::Grain, coordinateSource, grainSettings, 0.5, 9);
    LayerAdjustment grain;
    grain.kind = AdjustmentKind::Grain;
    grain.grainSettings = GrainSettings{60, 2, 40, 9};
    const Raster direct = Adjust::ApplyAdjustment(grain, coordinateSource, 0, 0, 2);
    const PixelBuffer viaBytes = CopyRasterPixelsForWork(viaFilter);
    const PixelBuffer directBytes = CopyRasterPixelsForWork(direct);
    Expect(viaBytes && directBytes && viaBytes.Size() == directBytes.Size() &&
               std::equal(viaBytes.Data(), viaBytes.Data() + viaBytes.Size(), directBytes.Data()),
           "grain.same-seed-and-document-coordinates-match-adjustment-reference");
}

void CoverageLiteralGoldens()
{
    constexpr std::array<std::uint8_t, 4> partialSquare{143, 143, 143, 143};
    const auto square = SelectionGeometry::Rect(0, 0, 1, 1);
    const Raster partial = SelectionGeometry::Coverage(SelectionGeometry::Rect(0.25, 0.25, 1.5, 1.5), 2, 2, true);
    Expect(Matches(partial, 2, 2, RasterFormat::Gray8, partialSquare), "coverage.square-partial-area-literal");

    const Affine2 partialMap = Affine2::Scale(1.5, 1.5).Concat(Affine2::Translation(0.25, 0.25));
    const Raster transformed = SelectionGeometry::CoverageTransformed(square, partialMap, 2, 2, true);
    Expect(Matches(transformed, 2, 2, RasterFormat::Gray8, partialSquare),
           "coverage.transformed-square-matches-literal-area");

    const auto ring =
        SelectionGeometry::Subtract(SelectionGeometry::Rect(0, 0, 4, 4), SelectionGeometry::Rect(1, 1, 2, 2));
    constexpr std::array<std::uint8_t, 16> ringExpected{255, 255, 255, 255, 255, 0,   0,   255,
                                                        255, 0,   0,   255, 255, 255, 255, 255};
    const Raster hardRing = SelectionGeometry::Coverage(ring, 4, 4, false);
    const Affine2 identityScale = Affine2::Translation(0, 0).Concat(Affine2::Scale(1, 1));
    const Raster transformedHardRing = SelectionGeometry::CoverageTransformed(ring, identityScale, 4, 4, false);
    Expect(Matches(hardRing, 4, 4, RasterFormat::Gray8, ringExpected) &&
               Matches(transformedHardRing, 4, 4, RasterFormat::Gray8, ringExpected),
           "coverage.non-aa-hole-literal-and-transformed-equivalent");

    const auto farPositive = SelectionGeometry::Rect(3'000'000'000.0, 0, 1, 2);
    const auto farNegative = SelectionGeometry::Rect(-4'000'000'000.0, -1, 4'000'000'002.0, 4);
    constexpr std::array<std::uint8_t, 4> zero{0, 0, 0, 0};
    constexpr std::array<std::uint8_t, 4> full{255, 255, 255, 255};
    Expect(Matches(SelectionGeometry::Coverage(farPositive, 2, 2, true), 2, 2, RasterFormat::Gray8, zero) &&
               Matches(SelectionGeometry::Coverage(farNegative, 2, 2, true), 2, 2, RasterFormat::Gray8, full) &&
               Matches(SelectionGeometry::Coverage(farNegative, 2, 2, false), 2, 2, RasterFormat::Gray8, full),
           "coverage.extreme-outside-x-zero-and-full-small-grid");

    const std::vector<SelectionLoop> overflowingX{{{-1e308, 0}, {1e308, 2}, {0, 2}}};
    const std::vector<SelectionLoop> overflowingY{{{0, -1e308}, {2, 1e308}, {0, 2}}};
    const Affine2 overflowingMap = Affine2::Scale(1e308, 1e308);
    for (const bool antialiased : {false, true})
    {
        Expect(!SelectionGeometry::Coverage(overflowingX, 2, 2, antialiased).HasStorage() &&
                   !SelectionGeometry::Coverage(overflowingY, 2, 2, antialiased).HasStorage() &&
                   !SelectionGeometry::CoverageTransformed(SelectionGeometry::Rect(0, 0, 4, 4), overflowingMap, 2, 2,
                                                           antialiased)
                        .HasStorage(),
               antialiased ? "coverage.aa-rejects-overflow-without-partial-publication"
                           : "coverage.hard-rejects-overflow-without-partial-publication");
    }
}

void RasterWorkLiteralGoldens()
{
    constexpr std::array<std::uint8_t, 12> thumbnailSourceBytes{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
    const Raster thumbnailSource = Gray(4, 3, thumbnailSourceBytes);
    constexpr std::array<std::uint8_t, 4> thumbnailExpected{1, 3, 5, 7};
    const Raster thumbnail = ThumbnailRasterForWork(thumbnailSource, 2);
    Expect(Matches(thumbnail, 2, 2, RasterFormat::Gray8, thumbnailExpected), "thumbnail.nearest-literal");

    constexpr std::array<std::uint8_t, 2> fittingBytes{17, 29};
    const Raster fitting = Gray(2, 1, fittingBytes);
    const Raster alias = ThumbnailRasterForWork(fitting, 96);
    Expect(alias == fitting, "thumbnail.max-side-alias-preserves-storage");

    constexpr std::array<std::uint8_t, 3> thumbnailTileBytes{21, 22, 23};
    const Raster thumbnailTile = Gray(3, 1, thumbnailTileBytes);
    const Raster sparseThumbnail =
        Raster::MakeSparse(4, 3, RasterFormat::Gray8, Raster{}, 0, 0, {RasterTile{1, 1, thumbnailTile}}, 90);
    constexpr std::array<std::uint8_t, 4> sparseThumbnailExpected{90, 90, 90, 22};
    Expect(Matches(ThumbnailRasterForWork(sparseThumbnail, 2), 2, 2, RasterFormat::Gray8, sparseThumbnailExpected) &&
               !sparseThumbnail.IsMaterialized(),
           "thumbnail.sparse-fill-and-offset-tile-literal");

    constexpr std::array<std::uint8_t, 8> colorBytes{1, 2, 3, 4, 5, 6, 7, 8};
    const Raster color = Bgra(2, 1, colorBytes);
    constexpr std::array<std::uint8_t, 4> colorThumbnailExpected{1, 2, 3, 4};
    Expect(Matches(ThumbnailRasterForWork(color, 1), 1, 1, RasterFormat::Bgra8888, colorThumbnailExpected),
           "thumbnail.bgra-channel-order-preserved");

    constexpr std::array<std::uint8_t, 6> maskBytes{10, 20, 30, 40, 50, 60};
    const Raster maskTile = Gray(3, 2, maskBytes);
    const Raster sparseMask =
        Raster::MakeSparse(3, 2, RasterFormat::Gray8, Raster{}, 0, 0, {RasterTile{0, 0, maskTile}}, 77);
    constexpr std::array<std::uint8_t, 12> negativeExpected{10, 20, 30, 99, 40, 50, 60, 99, 99, 99, 99, 99};
    constexpr std::array<std::uint8_t, 12> positiveExpected{99, 10, 10, 20, 99, 10, 10, 20, 99, 40, 40, 50};
    const Raster negative = CarriedMaskForWork(sparseMask, 4, 3, -1, -1, 4, 3, 99);
    const Raster positive = CarriedMaskForWork(sparseMask, 4, 3, 1, 0, 4, 3, 99);
    Expect(Matches(negative, 4, 3, RasterFormat::Gray8, negativeExpected) &&
               Matches(positive, 4, 3, RasterFormat::Gray8, positiveExpected) && !sparseMask.IsMaterialized(),
           "carried-mask.offset-crop-stretch-and-outside-literals");

    constexpr std::array<std::uint8_t, 1> offsetTileBytes{20};
    const Raster offsetTile = Gray(1, 1, offsetTileBytes);
    const Raster offsetSparseMask =
        Raster::MakeSparse(3, 2, RasterFormat::Gray8, Raster{}, 0, 0, {RasterTile{1, 0, offsetTile}}, 77);
    constexpr std::array<std::uint8_t, 12> offsetSparseExpected{77, 77, 20, 77, 77, 77, 20, 77, 77, 77, 77, 77};
    Expect(Matches(CarriedMaskForWork(offsetSparseMask, 4, 3, 0, 0, 4, 3, 99), 4, 3, RasterFormat::Gray8,
                   offsetSparseExpected) &&
               !offsetSparseMask.IsMaterialized(),
           "carried-mask.sparse-fill-offset-tile-remains-unmaterialized");

    const Raster colorCarried = CarriedMaskForWork(color, 2, 1, 1, 0, 3, 1, 99);
    constexpr std::array<std::uint8_t, 12> colorCarriedExpected{99, 99, 99, 99, 1, 2, 3, 4, 5, 6, 7, 8};
    Expect(Matches(colorCarried, 3, 1, RasterFormat::Bgra8888, colorCarriedExpected),
           "carried-mask.bgra-outside-repeats-each-channel");

    CancelSource cancelSource;
    (void)cancelSource.Cancel();
    const CancelToken cancellation = cancelSource.Token();
    Expect(!ThumbnailRasterForWork(fitting, 96, cancellation).HasStorage() &&
               !CarriedMaskForWork(sparseMask, 4, 3, 0, 0, 4, 3, 99, cancellation).HasStorage() &&
               !sparseMask.IsMaterialized(),
           "raster-work.pre-cancel-even-for-alias-and-sparse-mask");
}

void SparseImageHelpers()
{
    constexpr std::array<std::uint8_t, 16> tileBytes{20, 30, 40,  255, 50,  60,  70,  255,
                                                     80, 90, 100, 255, 110, 120, 130, 255};
    constexpr std::array<std::uint8_t, 4> maskBytes{255, 128, 0, 255};
    const Raster tile = Bgra(2, 2, tileBytes);
    const Raster sparse = Raster::MakeSparse(2, 2, RasterFormat::Bgra8888, Raster{}, 0, 0, {RasterTile{0, 0, tile}}, 0);
    const Raster maskTile = Gray(2, 2, maskBytes);
    const Raster sparseMask =
        Raster::MakeSparse(2, 2, RasterFormat::Gray8, Raster{}, 0, 0, {RasterTile{0, 0, maskTile}}, 0);
    const LevelsHistogram histogram = Adjust::Histogram(sparse, nullptr);
    const Raster downscaled = Adjust::Downscaled(sparse, 1);
    const Raster trimmed = Adjust::TrimmedToAlpha(sparse, LayerTransform{}).image;
    const Raster gaussian = Adjust::GaussianBlurred(sparse, 0.5);
    const Raster motion = Adjust::MotionBlurred(sparse, 0, 2);
    const Raster imageGaussian = ImageFilters::GaussianBlur(sparse, 0.5);
    const Raster blended = Adjust::BlendThrough(sparse, sparse, sparseMask);
    const PixelBuffer downscaledPixels = CopyRasterPixelsForWork(downscaled);
    bool histogramHasSamples = false;
    for (const auto& channel : histogram)
    {
        histogramHasSamples =
            histogramHasSamples || std::any_of(channel.begin(), channel.end(), [](double value) { return value != 0; });
    }
    Expect(histogramHasSamples && downscaledPixels && trimmed.HasStorage() && gaussian.HasStorage() &&
               motion.HasStorage() && imageGaussian.HasStorage() && blended.HasStorage() && !sparse.IsMaterialized() &&
               !sparseMask.IsMaterialized(),
           "sparse.image-helpers-read-without-materializing-inputs");
}

void CurveSlopesWithoutTemporaryAllocation()
{
    // Independent cubic-Hermite values: opposite neighboring secants give a zero tangent at each turning point.
    CurvesSettings settings;
    settings.channels[0] = {{0, 0}, {64, 128}, {128, 0}, {255, 255}};
    Expect(Adjust::CurveValue(settings, 32, 0) == 80 && Adjust::CurveValue(settings, 96, 0) == 64 &&
               std::abs(Adjust::CurveValue(settings, 191.5, 0) - 95.625) < 1e-12,
           "curves.opposing-secants-and-endpoint-tangents");
    settings.channels[0] = {{0, 0}, {64, 64}, {128, 192}, {255, 255}};
    // First secant is 1, next is 2: the shared tangent is 4/3 and the first interval's midpoint is 88/3.
    Expect(std::abs(Adjust::CurveValue(settings, 32, 0) - 88.0 / 3.0) < 1e-12,
           "curves.harmonic-tangent-independent-midpoint");
}
} // namespace

int RunFilterCancellationTests()
{
    gFailures = 0;
    PreCancelledPublicOperations();
    DonorlessFillAndDeterministicPlumbing();
    CoverageLiteralGoldens();
    RasterWorkLiteralGoldens();
    SparseImageHelpers();
    CurveSlopesWithoutTemporaryAllocation();
    return gFailures;
}
