#include "CopyPixelsTransformTests.h"

#include "CopyPixelsWork.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <span>
#include <utility>

namespace
{
int gFailures = 0;

void Expect(bool condition, const char* name) noexcept
{
    std::printf("%s copypixels.transform.%s\n", condition ? "PASS" : "FAIL", name);
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
    const std::uint8_t* const pixels = image.Data();
    return pixels && std::equal(expected.begin(), expected.end(), pixels);
}

DocumentSelection Rect(double x, double y, double width, double height)
{
    return DocumentSelection{
        false, SelectionLoops{{Vec2{x, y}, Vec2{x + width, y}, Vec2{x + width, y + height}, Vec2{x, y + height}}}};
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
    return result.status == CopyPixelsStatus::Ready && result.clipboard && result.clipboard->origin == origin &&
           Equals(result.clipboard->image, width, height, expected);
}

void ClockwiseRotationKeepsPixelSquares()
{
    constexpr std::array<std::uint8_t, 16> source{10, 20, 30, 255, 40,  50,  60,  255,
                                                  70, 80, 90, 255, 100, 110, 120, 255};
    CopyPixelsInput input = Input(Bgra(2, 2, source), 4, 4);
    input.placement.origin = Vec2{1, 1};
    input.placement.rotation = 90;

    // The unit-square map rotates clockwise about (2,2): source TL/TR/BL/BR land at target TR/BR/TL/BL.
    constexpr std::array<std::uint8_t, 64> expected{0, 0, 0, 0, 0,   0,   0,   0,   0,  0,  0,  0,   0, 0, 0, 0,
                                                    0, 0, 0, 0, 70,  80,  90,  255, 10, 20, 30, 255, 0, 0, 0, 0,
                                                    0, 0, 0, 0, 100, 110, 120, 255, 40, 50, 60, 255, 0, 0, 0, 0,
                                                    0, 0, 0, 0, 0,   0,   0,   0,   0,  0,  0,  0,   0, 0, 0, 0};
    Expect(ReadyEquals(input, 4, 4, Vec2{0, 0}, expected), "clockwise-rotation-literal-grid");
}

void HorizontalFlipKeepsPixelCenters()
{
    constexpr std::array<std::uint8_t, 16> source{11, 21, 31, 255, 41,  51,  61,  255,
                                                  71, 81, 91, 255, 101, 111, 121, 255};
    CopyPixelsInput input = Input(Bgra(2, 2, source), 2, 2);
    input.placement.flipX = true;

    // Mirroring the unit square swaps each row's source pixels without changing their center samples.
    constexpr std::array<std::uint8_t, 16> expected{41,  51,  61,  255, 11, 21, 31, 255,
                                                    101, 111, 121, 255, 71, 81, 91, 255};
    Expect(ReadyEquals(input, 2, 2, Vec2{0, 0}, expected), "flip-x-literal-grid");
}

void ScaledNearestAndSmoothUseTargetPixelCenters()
{
    constexpr std::array<std::uint8_t, 8> source{20, 60, 100, 255, 100, 140, 180, 255};
    CopyPixelsInput input = Input(Bgra(2, 1, source), 3, 1);
    input.placement.size = Vec2{3, 1};

    // Target centers map to source x = 1/3, 1, 5/3. floor selects source indices 0, 1, 1.
    constexpr std::array<std::uint8_t, 12> nearest{20, 60, 100, 255, 100, 140, 180, 255, 100, 140, 180, 255};
    Expect(ReadyEquals(input, 3, 1, Vec2{0, 0}, nearest), "scaled-nearest-literal-pixel-centers");

    input.placement.sampling = LayerSampling::Smooth;
    // Smooth samples source center coordinates -1/6, 1/2, 7/6, clamped to [0,1]: first, midpoint, second.
    constexpr std::array<std::uint8_t, 12> smooth{20, 60, 100, 255, 60, 100, 140, 255, 100, 140, 180, 255};
    Expect(ReadyEquals(input, 3, 1, Vec2{0, 0}, smooth), "scaled-smooth-literal-center-interpolation");

    input.placement.sampling = LayerSampling::High;
    Expect(ReadyEquals(input, 3, 1, Vec2{0, 0}, smooth), "high-collapses-to-smooth-for-transient-copy");
}

void PlacedMaskSelectionIncludesOutsideBackground()
{
    constexpr std::array<std::uint8_t, 1> source{201};
    CopyPixelsInput input = Input(Gray(1, 1, source), 3, 1);
    input.mask = true;
    input.background = 37;
    input.placement.origin = Vec2{1, 0};
    input.selection = Rect(1, 0, 2, 1);

    // The selection crops columns 1..2. The placed one-pixel mask supplies 201 at column 1;
    // column 2 lies outside its unit square and retains the separately captured edge tone 37.
    constexpr std::array<std::uint8_t, 8> expected{201, 201, 201, 255, 37, 37, 37, 255};
    Expect(ReadyEquals(input, 2, 1, Vec2{1, 0}, expected), "placed-mask-selection-crop-retains-outside-background");
}
} // namespace

int RunCopyPixelsTransformTests()
{
    gFailures = 0;
    ClockwiseRotationKeepsPixelSquares();
    HorizontalFlipKeepsPixelCenters();
    ScaledNearestAndSmoothUseTargetPixelCenters();
    PlacedMaskSelectionIncludesOutsideBackground();
    return gFailures == 0 ? 0 : 1;
}
