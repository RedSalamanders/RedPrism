#include "Document.Tests.PlacedRasterWork.h"

#include "PlacedRasterWork.h"

#include "CanvasSize.h"
#include "Selection.h"

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
int gChecks = 0;

void Expect(bool condition, const char* label) noexcept
{
    std::printf("%s placed-raster-work.%s\n", condition ? "PASS" : "FAIL", label);
    ++gChecks;
    gFailures += condition ? 0 : 1;
}

constexpr std::array<std::uint8_t, 16> kRamp{
    10, 20, 30, 40, 50, 60, 70, 80, 90, 100, 110, 120, 130, 140, 150, 160,
};

Raster Bgra(int width, int height, std::span<const std::uint8_t> pixels)
{
    return Raster::Make(width, height, RasterFormat::Bgra8888, pixels.data(), pixels.size());
}

Raster Ramp()
{
    return Bgra(2, 2, kRamp);
}

LayerTransform Placement(double x, double y, double width, double height, LayerSampling sampling)
{
    LayerTransform transform;
    transform.origin = Vec2{x, y};
    transform.size = Vec2{width, height};
    transform.sampling = sampling;
    return transform;
}

bool ReadEquals(PlacedRasterReader& reader, GridRect region, std::span<const std::uint8_t> expected, std::size_t stride)
{
    std::vector<std::uint8_t> actual(stride * static_cast<std::size_t>(region.height), 0xCD);
    if (!reader.Read(region, actual, stride))
        return false;
    for (int row = 0; row < region.height; ++row)
    {
        for (int column = 0; column < region.width * 4; ++column)
        {
            const auto offset = static_cast<std::size_t>(row) * stride + static_cast<std::size_t>(column);
            if (actual[offset] != expected[static_cast<std::size_t>(row * region.width * 4 + column)])
                return false;
        }
    }
    return true;
}

bool ReadPixelEquals(PlacedRasterReader& reader, int x, int y, const std::array<std::uint8_t, 4>& expected)
{
    std::array<std::uint8_t, 4> actual{0xCD, 0xCD, 0xCD, 0xCD};
    return reader.Read(GridRect{x, y, 1, 1}, actual, 4) && actual == expected;
}

std::array<std::uint8_t, 4> PixelAt(const Raster& image, int x, int y) noexcept
{
    std::array<std::uint8_t, 4> pixel{};
    image.ReadRegion(x, y, 1, 1, pixel.data(), 4, 0);
    return pixel;
}

Raster CoordinateRaster(int width, int height)
{
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4);
    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            const auto offset =
                (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)) * 4;
            pixels[offset] = static_cast<std::uint8_t>(x);
            pixels[offset + 1] = static_cast<std::uint8_t>(y);
            pixels[offset + 2] = static_cast<std::uint8_t>((x + y) / 2);
            pixels[offset + 3] = 255;
        }
    }
    return Bgra(width, height, pixels);
}

Raster SparseOver(const Raster& pixels, int width, int height)
{
    return Raster::MakeSparse(width, height, RasterFormat::Bgra8888, {}, 0, 0, {RasterTile{0, 0, pixels}}, 0);
}

void LiteralSamplingAndCoordinates()
{
    const Raster source = Ramp();
    Expect(source.HasStorage(), "fixture.literal-premultiplied-2x2");
    {
        PlacedRasterReader reader(source, Placement(0, 0, 2, 2, LayerSampling::Nearest));
        Expect(ReadEquals(reader, {0, 0, 2, 2}, kRamp, 8), "nearest.identity-literal-ramp");
        constexpr std::array<std::uint8_t, 4> translated{50, 60, 70, 80};
        Expect(ReadPixelEquals(reader, 1, 0, translated), "nearest.region-offset-is-global-document-coordinate");
    }
    {
        PlacedRasterReader reader(source, Placement(5, 7, 2, 2, LayerSampling::Nearest));
        constexpr std::array<std::uint8_t, 4> translated{10, 20, 30, 40};
        Expect(ReadPixelEquals(reader, 5, 7, translated), "nearest.translated-placement");
    }
    {
        PlacedRasterReader reader(source, Placement(-0.25, -0.25, 2, 2, LayerSampling::Smooth));
        // The source coordinate is (.75,.75): four literal taps weighted 9/16, 3/16, 3/16, 1/16.
        // Their channels yield (40,50,60,70) exactly; a separate literal half-tie checks rounding.
        constexpr std::array<std::uint8_t, 4> fractional{40, 50, 60, 70};
        Expect(ReadPixelEquals(reader, 0, 0, fractional), "smooth.literal-quarter-fraction");
    }
    {
        constexpr std::array<std::uint8_t, 8> tiePixels{10, 20, 30, 40, 11, 21, 31, 41};
        const Raster tie = Bgra(2, 1, tiePixels);
        PlacedRasterReader reader(tie, Placement(-0.5, 0, 2, 1, LayerSampling::Smooth));
        constexpr std::array<std::uint8_t, 4> rounded{11, 21, 31, 41};
        Expect(ReadPixelEquals(reader, 0, 0, rounded), "smooth.literal-half-tie-rounds-up");
    }
    {
        PlacedRasterReader reader(source, Placement(-0.25, -0.25, 2, 2, LayerSampling::High));
        constexpr std::array<std::uint8_t, 4> high{40, 50, 60, 70};
        Expect(ReadPixelEquals(reader, 0, 0, high), "high.collapses-to-single-bilinear-literal");
    }
    {
        PlacedRasterReader reader(source, Placement(0, 0, 2, 2, LayerSampling::Smooth));
        constexpr std::array<std::uint8_t, 4> topLeft{10, 20, 30, 40};
        constexpr std::array<std::uint8_t, 4> bottomRight{130, 140, 150, 160};
        Expect(ReadPixelEquals(reader, 0, 0, topLeft) && ReadPixelEquals(reader, 1, 1, bottomRight),
               "smooth.clamps-bilinear-taps-at-source-edges");
    }
    {
        LayerTransform transform = Placement(0, 0, 2, 2, LayerSampling::Nearest);
        transform.flipX = true;
        PlacedRasterReader reader(source, transform);
        constexpr std::array<std::uint8_t, 16> expected{
            50, 60, 70, 80, 10, 20, 30, 40, 130, 140, 150, 160, 90, 100, 110, 120,
        };
        Expect(ReadEquals(reader, {0, 0, 2, 2}, expected, 8), "nearest.horizontal-flip");
    }
    {
        LayerTransform transform = Placement(0, 0, 2, 2, LayerSampling::Nearest);
        transform.rotation = 90;
        PlacedRasterReader reader(source, transform);
        constexpr std::array<std::uint8_t, 16> expected{
            90, 100, 110, 120, 10, 20, 30, 40, 130, 140, 150, 160, 50, 60, 70, 80,
        };
        Expect(ReadEquals(reader, {0, 0, 2, 2}, expected, 8), "nearest.clockwise-rotation");
    }
    {
        constexpr std::array<std::uint8_t, 4> onePixel{7, 11, 13, 17};
        const Raster single = Bgra(1, 1, onePixel);
        PlacedRasterReader reader(single, Placement(0, 0, 3, 3, LayerSampling::Smooth));
        constexpr std::array<std::uint8_t, 36> expected{
            7,  11, 13, 17, 7,  11, 13, 17, 7,  11, 13, 17, 7,  11, 13, 17, 7,  11,
            13, 17, 7,  11, 13, 17, 7,  11, 13, 17, 7,  11, 13, 17, 7,  11, 13, 17,
        };
        Expect(ReadEquals(reader, {0, 0, 3, 3}, expected, 12), "smooth.single-pixel-clamps-across-placement");
    }
}

void TransparentAndBufferContracts()
{
    constexpr std::array<std::uint8_t, 4> transparent{};
    {
        PlacedRasterReader reader(Raster{}, Placement(0, 0, 2, 2, LayerSampling::Smooth));
        Expect(ReadPixelEquals(reader, 0, 0, transparent), "empty-source.transparent");
    }
    {
        const Raster blank = Raster::MakeSparse(2, 2, RasterFormat::Bgra8888, {}, 0, 0, {}, 0);
        PlacedRasterReader reader(blank, Placement(0, 0, 2, 2, LayerSampling::Smooth));
        Expect(blank.IsSparse() && ReadPixelEquals(reader, 1, 1, transparent) && !blank.IsMaterialized(),
               "blank-sparse-source.transparent-without-materialization");
    }
    {
        PlacedRasterReader reader(Ramp(), Placement(2, 2, 2, 2, LayerSampling::Nearest));
        Expect(ReadPixelEquals(reader, 0, 0, transparent), "nearest.outside-placement-transparent");
    }
    {
        constexpr std::array<std::uint8_t, 4> grayBytes{1, 2, 3, 4};
        const Raster gray = Raster::Make(2, 2, RasterFormat::Gray8, grayBytes.data(), grayBytes.size());
        PlacedRasterReader reader(gray, Placement(0, 0, 2, 2, LayerSampling::Smooth));
        Expect(ReadPixelEquals(reader, 0, 0, transparent), "wrong-format.transparent");
    }
    {
        LayerTransform invalid = Placement(0, 0, 0, 2, LayerSampling::Nearest);
        PlacedRasterReader reader(Ramp(), invalid);
        Expect(ReadPixelEquals(reader, 0, 0, transparent), "invalid-placement.transparent");
    }
    {
        PlacedRasterReader reader(Ramp(), Placement(0, 0, 2, 2, LayerSampling::Nearest));
        constexpr std::size_t stride = 8;
        std::array<std::uint8_t, stride * 2> padded;
        padded.fill(0xCD);
        const bool read = reader.Read({0, 0, 1, 2}, padded, stride);
        Expect(read && padded[0] == 10 && padded[3] == 40 && padded[8] == 90 && padded[11] == 120 &&
                   padded[4] == 0xCD && padded[7] == 0xCD && padded[12] == 0xCD && padded[15] == 0xCD,
               "output.writes-pixels-and-preserves-row-padding");
    }
    {
        PlacedRasterReader reader(Ramp(), Placement(0, 0, 2, 2, LayerSampling::Nearest));
        std::array<std::uint8_t, 16> output;
        output.fill(0xCD);
        const bool refused = !reader.Read({0, 0, 1, 1}, output, 3);
        Expect(refused && output[0] == 0xCD && output[15] == 0xCD, "invalid.short-stride-refused-before-write");
    }
    {
        PlacedRasterReader reader(Ramp(), Placement(0, 0, 2, 2, LayerSampling::Nearest));
        std::array<std::uint8_t, 3> output{0xCD, 0xCD, 0xCD};
        const bool refused = !reader.Read({0, 0, 1, 1}, output, 4);
        Expect(refused && output == std::array<std::uint8_t, 3>{0xCD, 0xCD, 0xCD},
               "invalid.short-output-refused-before-write");
    }
    {
        PlacedRasterReader reader(Ramp(), Placement(0, 0, 2, 2, LayerSampling::Nearest));
        std::array<std::uint8_t, 4> output{0xCD, 0xCD, 0xCD, 0xCD};
        const bool empty = !reader.Read({0, 0, 0, 1}, output, 4);
        const bool negative = !reader.Read({-1, 0, 1, 1}, output, 4);
        const bool outside = !reader.Read({kMaxCanvasSide, 0, 1, 1}, output, 4);
        const bool tooWide = !reader.Read({0, 0, kMaxCanvasSide + 1, 1}, output, 4);
        Expect(empty && negative && outside && tooWide && output == std::array<std::uint8_t, 4>{0xCD, 0xCD, 0xCD, 0xCD},
               "invalid.empty-negative-outside-and-oversize-regions-refused-before-write");
    }
}

void SparseWindowBehavior()
{
    const Raster backing = CoordinateRaster(130, 130);
    const Raster sparse = SparseOver(backing, 130, 130);
    Expect(sparse.IsSparse() && !sparse.IsMaterialized(), "sparse.fixture-unmaterialized");
    {
        LayerTransform transform = Placement(-0.5, -0.5, 130, 130, LayerSampling::Smooth);
        PlacedRasterReader reader(sparse, transform);
        std::vector<std::uint8_t> before(static_cast<std::size_t>(backing.Stride()) * 130);
        backing.ReadRegion(0, 0, 130, 130, before.data(), backing.Stride(), 0);
        // At global (63,63), the four source taps are (63,63), (64,63), (63,64), (64,64).
        // R/G average to 63.5 and round to 64; the integer (x+y)/2 plane averages to 63.25 and rounds to 63.
        constexpr std::array<std::uint8_t, 4> crossing{64, 64, 63, 255};
        Expect(ReadPixelEquals(reader, 63, 63, crossing), "sparse.bilinear-taps-straddle-63-64-corner");
        Expect(!sparse.IsMaterialized(), "sparse.corner-read-never-materializes-source");
        std::vector<std::uint8_t> after(before.size());
        backing.ReadRegion(0, 0, 130, 130, after.data(), backing.Stride(), 0);
        Expect(before == after, "sparse.source-pixels-unchanged-after-corner-read");
    }
    {
        const Raster evictionBacking = CoordinateRaster(320, 8);
        const Raster evictionSource = SparseOver(evictionBacking, 320, 8);
        LayerTransform transform = Placement(0, 0, 320, 8, LayerSampling::Nearest);
        PlacedRasterReader reader(evictionSource, transform);
        constexpr std::array<int, 5> xs{0, 64, 128, 192, 256};
        bool windowsRead = true;
        for (const int x : xs)
        {
            const std::array<std::uint8_t, 4> expected{static_cast<std::uint8_t>(x), 0,
                                                       static_cast<std::uint8_t>(x / 2), 255};
            windowsRead = windowsRead && ReadPixelEquals(reader, x, 0, expected);
        }
        Expect(windowsRead, "sparse.reads-five-source-windows-through-four-slot-cache");
        constexpr std::array<std::uint8_t, 4> revisit{0, 0, 0, 255};
        Expect(ReadPixelEquals(reader, 0, 0, revisit), "sparse.evicted-window-can-be-read-again");
        Expect(!evictionSource.IsMaterialized(), "sparse.eviction-keeps-source-unmaterialized");
    }
}

void OwnershipCancellationAndRetirement()
{
    {
        Raster callerOwner = Ramp();
        const void* const identity = callerOwner.Identity();
        PlacedRasterReader reader(callerOwner, Placement(0, 0, 2, 2, LayerSampling::Nearest));
        callerOwner = {};
        constexpr std::array<std::uint8_t, 4> expected{10, 20, 30, 40};
        Expect(identity != nullptr && ReadPixelEquals(reader, 0, 0, expected),
               "source-cache-identity-retained-after-caller-release");
    }
    {
        const Raster backing = CoordinateRaster(320, 320);
        const Raster source = SparseOver(backing, 320, 320);
        PlacedRasterReader reader(source, Placement(0, 0, 320, 320, LayerSampling::Nearest));
        const std::uint64_t before = reader.RetirementMemoryBytes();
        bool touched = true;
        constexpr std::array<int, 4> xs{0, 64, 128, 192};
        for (const int x : xs)
        {
            const std::array<std::uint8_t, 4> expected{static_cast<std::uint8_t>(x), 0,
                                                       static_cast<std::uint8_t>(x / 2), 255};
            touched = touched && ReadPixelEquals(reader, x, 0, expected);
        }
        const std::uint64_t after = reader.RetirementMemoryBytes();
        Expect(touched && before != 0 && after >= before + PlacedRasterReader::kCacheSlots * 64u * 64u * 4u,
               "retirement.accounts-for-all-four-touched-cache-windows");
    }
    {
        PlacedRasterReader reader(Ramp(), Placement(0, 0, 2, 2, LayerSampling::Nearest));
        CancelSource cancellation;
        (void)cancellation.Cancel();
        std::array<std::uint8_t, 4> output{0xCD, 0xCD, 0xCD, 0xCD};
        const bool refused = !reader.Read({0, 0, 1, 1}, output, 4, cancellation.Token());
        Expect(refused && output == std::array<std::uint8_t, 4>{0xCD, 0xCD, 0xCD, 0xCD},
               "cancel.pre-requested-refuses-with-private-output-untouched");
    }
}

void SupplementalLegacyParity()
{
    const Raster source = Ramp();
    const LayerTransform transform = Placement(-0.25, -0.25, 2, 2, LayerSampling::Smooth);
    PlacedRasterReader reader(source, transform);
    const Raster reference = RenderLayerPixels(source, transform, 2, 2);
    constexpr std::array<std::uint8_t, 16> expectedIndependent{
        40, 50, 60, 70, 70, 80, 90, 100, 100, 110, 120, 130, 130, 140, 150, 160,
    };
    std::array<std::uint8_t, 16> actual{};
    const bool read = reader.Read({0, 0, 2, 2}, actual, 8);
    std::array<std::uint8_t, 16> legacy{};
    const bool referenceValid = reference.HasStorage() && reference.Width() == 2 && reference.Height() == 2;
    if (referenceValid)
        reference.ReadRegion(0, 0, 2, 2, legacy.data(), 8, 0);
    Expect(read && referenceValid && actual == expectedIndependent && actual == legacy,
           "parity.extra-check-against-unchanged-renderlayerpixels");
}
} // namespace

bool RunPlacedRasterWorkTests()
{
    gFailures = 0;
    gChecks = 0;
    LiteralSamplingAndCoordinates();
    TransparentAndBufferContracts();
    SparseWindowBehavior();
    OwnershipCancellationAndRetirement();
    SupplementalLegacyParity();
    std::printf("SUMMARY placed-raster-work checks=%d failures=%d\n", gChecks, gFailures);
    return gFailures == 0;
}
