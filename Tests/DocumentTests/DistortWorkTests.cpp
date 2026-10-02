#include "DistortWorkTests.h"

#include "DistortWork.h"
#include "PixelBuffer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <new>
#include <span>
#include <stop_token>
#include <vector>

namespace
{
int gFailures = 0;
int gAssertions = 0;

void Expect(bool condition, const char* name)
{
    ++gAssertions;
    std::printf("%s distort-work.%s\n", condition ? "PASS" : "FAIL", name);
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

DistortWorkInput Input(const Raster& image, std::array<Vec2, 4> corners, bool mask = false)
{
    DistortWorkInput input;
    input.image = image;
    input.placement.origin = Vec2{0, 0};
    input.placement.size = Vec2{static_cast<double>(image.Width()), static_cast<double>(image.Height())};
    input.placement.sampling = LayerSampling::Nearest;
    input.corners = corners;
    input.mask = mask;
    return input;
}

bool PixelsEqual(const Raster& actual, std::span<const std::uint8_t> expected, int width, int height, const char* name)
{
    const std::size_t expectedSize = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) *
                                     static_cast<std::size_t>(actual.Channels());
    bool equal = actual.HasStorage() && actual.Width() == width && actual.Height() == height &&
                 actual.Format() == RasterFormat::Bgra8888 && expected.size() == expectedSize;
    const std::uint8_t* const data = equal ? actual.Data() : nullptr;
    equal = equal && data != nullptr && std::equal(expected.begin(), expected.end(), data);
    if (!equal)
    {
        std::printf("  %s actual=%dx%d format=%s pixels=", name, actual.Width(), actual.Height(),
                    actual.Format() == RasterFormat::Bgra8888 ? "BGRA" : "Gray8");
        if (!actual.HasStorage() || data == nullptr)
        {
            std::printf("<none>\n");
        }
        else
        {
            for (int y = 0; y < actual.Height(); ++y)
            {
                for (int x = 0; x < actual.Width(); ++x)
                {
                    const std::size_t offset =
                        static_cast<std::size_t>(y) * static_cast<std::size_t>(actual.Stride()) +
                        static_cast<std::size_t>(x) * static_cast<std::size_t>(actual.Channels());
                    if (actual.Format() == RasterFormat::Bgra8888)
                    {
                        std::printf(" (%d,%d)=[%u,%u,%u,%u]", x, y, data[offset], data[offset + 1], data[offset + 2],
                                    data[offset + 3]);
                    }
                    else
                    {
                        std::printf(" (%d,%d)=%u", x, y, data[offset]);
                    }
                }
            }
            std::printf("\n");
        }
        std::printf("  %s expected=", name);
        for (const std::uint8_t value : expected)
        {
            std::printf("%u,", value);
        }
        std::printf(" (%dx%d)\n", width, height);
    }
    Expect(equal, name);
    return equal;
}

bool GrayEquals(const Raster& actual, std::span<const std::uint8_t> expected, int width, int height, const char* name)
{
    bool equal = actual.HasStorage() && actual.Width() == width && actual.Height() == height &&
                 actual.Format() == RasterFormat::Gray8 &&
                 expected.size() == static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    const std::uint8_t* const data = equal ? actual.Data() : nullptr;
    equal = equal && data != nullptr;
    if (equal)
    {
        for (int y = 0; y < height && equal; ++y)
        {
            const std::size_t offset = static_cast<std::size_t>(y) * static_cast<std::size_t>(actual.Stride());
            const std::size_t expectedOffset = static_cast<std::size_t>(y) * static_cast<std::size_t>(width);
            equal = std::equal(expected.begin() + static_cast<std::ptrdiff_t>(expectedOffset),
                               expected.begin() + static_cast<std::ptrdiff_t>(expectedOffset + width), data + offset);
        }
    }
    if (!equal)
    {
        std::printf("  %s actual=%dx%d format=%s pixels=", name, actual.Width(), actual.Height(),
                    actual.Format() == RasterFormat::Gray8 ? "Gray8" : "BGRA");
        if (!actual.HasStorage() || data == nullptr)
        {
            std::printf("<none>\n");
        }
        else
        {
            for (int y = 0; y < actual.Height(); ++y)
            {
                for (int x = 0; x < actual.Width(); ++x)
                {
                    const std::size_t offset =
                        static_cast<std::size_t>(y) * static_cast<std::size_t>(actual.Stride()) +
                        static_cast<std::size_t>(x) * static_cast<std::size_t>(actual.Channels());
                    std::printf(" (%d,%d)=%u", x, y, data[offset]);
                }
            }
            std::printf("\n");
        }
        std::printf("  %s expected=", name);
        for (const std::uint8_t value : expected)
        {
            std::printf("%u,", value);
        }
        std::printf(" (%dx%d)\n", width, height);
    }
    Expect(equal, name);
    return equal;
}

void IdentityNearestAndFlips()
{
    constexpr std::array<std::uint8_t, 16> sourcePixels{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
    const Raster source = Bgra(2, 2, sourcePixels);
    constexpr std::array<std::uint8_t, 16> ordinary{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
    constexpr std::array<std::uint8_t, 16> flipX{5, 6, 7, 8, 1, 2, 3, 4, 13, 14, 15, 16, 9, 10, 11, 12};
    constexpr std::array<std::uint8_t, 16> flipY{9, 10, 11, 12, 13, 14, 15, 16, 1, 2, 3, 4, 5, 6, 7, 8};
    constexpr std::array<std::uint8_t, 16> flipXY{13, 14, 15, 16, 9, 10, 11, 12, 5, 6, 7, 8, 1, 2, 3, 4};
    constexpr std::array<std::array<std::uint8_t, 16>, 4> expected{ordinary, flipX, flipY, flipXY};
    InlineExecutor executor;
    int combination = 0;
    for (const bool flipYValue : {false, true})
    {
        for (const bool flipXValue : {false, true})
        {
            DistortWorkInput input = Input(source, Rect(0, 0, 2, 2));
            input.placement.flipX = flipXValue;
            input.placement.flipY = flipYValue;
            DistortWorkResult result = RunDistortWork(input, executor);
            const char* const name = combination == 0   ? "identity.nearest-no-flips-literal-bgra"
                                     : combination == 1 ? "identity.nearest-flip-x-literal-bgra"
                                     : combination == 2 ? "identity.nearest-flip-y-literal-bgra"
                                                        : "identity.nearest-flip-xy-literal-bgra";
            Expect(result.status == DistortWorkStatus::Ready && result.placement.origin == Vec2{0, 0} &&
                       result.placement.size == Vec2{2, 2} && result.cropX == 0 && result.cropY == 0,
                   "identity.nearest-status-placement-and-crop");
            if (result.status == DistortWorkStatus::Ready)
            {
                static_cast<void>(
                    PixelsEqual(result.image, expected[static_cast<std::size_t>(combination)], 2, 2, name));
            }
            ++combination;
        }
    }
}

void BilinearLiteralSamples()
{
    constexpr std::array<std::uint8_t, 16> sourcePixels{0,   0,   0,   0,   100, 100, 100, 100,
                                                        200, 200, 200, 200, 255, 255, 255, 255};
    constexpr std::array<std::uint8_t, 36> expected{0,   0,   0,   0,   50,  50,  50,  50,  100, 100, 100, 100,
                                                    100, 100, 100, 100, 139, 139, 139, 139, 178, 178, 178, 178,
                                                    200, 200, 200, 200, 228, 228, 228, 228, 255, 255, 255, 255};
    const Raster source = Bgra(2, 2, sourcePixels);
    DistortWorkInput input = Input(source, Rect(0, 0, 3, 3));
    input.placement.sampling = LayerSampling::Smooth;
    InlineExecutor executor;
    DistortWorkResult result = RunDistortWork(input, executor);
    Expect(result.status == DistortWorkStatus::Ready && result.image.Width() == 3 && result.image.Height() == 3,
           "sampling.bilinear-3x3-status-dimensions");
    if (result.status == DistortWorkStatus::Ready)
    {
        static_cast<void>(PixelsEqual(result.image, expected, 3, 3, "sampling.bilinear-independent-literal-bgra"));
    }
}

void MaskBackgroundAndEdgeCoverage()
{
    constexpr std::array<std::uint8_t, 4> values{64, 64, 64, 64};
    const Raster source = Gray(2, 2, values);
    const std::array<Vec2, 4> fractionalRect = Rect(0.25, 0, 2.25, 2);
    constexpr std::array<std::uint8_t, 6> backgroundZero{64, 64, 32, 64, 64, 32};
    constexpr std::array<std::uint8_t, 6> backgroundFull{64, 64, 160, 64, 64, 160};
    InlineExecutor executor;
    DistortWorkInput zero = Input(source, fractionalRect, true);
    zero.background = 0.0;
    DistortWorkResult zeroResult = RunDistortWork(zero, executor);
    Expect(zeroResult.status == DistortWorkStatus::Ready, "mask.zero-background-edge-status");
    if (zeroResult.status == DistortWorkStatus::Ready)
    {
        static_cast<void>(
            GrayEquals(zeroResult.image, backgroundZero, 3, 2, "mask.zero-background-half-covered-right-edge-literal"));
    }
    DistortWorkInput full = Input(source, fractionalRect, true);
    full.background = 1.0;
    DistortWorkResult fullResult = RunDistortWork(full, executor);
    Expect(fullResult.status == DistortWorkStatus::Ready, "mask.full-background-edge-status");
    if (fullResult.status == DistortWorkStatus::Ready)
    {
        static_cast<void>(
            GrayEquals(fullResult.image, backgroundFull, 3, 2, "mask.full-background-half-covered-right-edge-literal"));
    }
}

void AlphaTrimCropAndPlacement()
{
    constexpr std::array<std::uint8_t, 24> sourcePixels{0, 0, 0, 0, 11, 12, 13, 255, 21, 22, 23, 128,
                                                        0, 0, 0, 0, 0,  0,  0,  0,   0,  0,  0,  0};
    constexpr std::array<std::uint8_t, 8> expected{11, 12, 13, 255, 21, 22, 23, 128};
    const Raster source = Bgra(3, 2, sourcePixels);
    DistortWorkInput input = Input(source, Rect(5, 7, 8, 9));
    input.placement.origin = Vec2{5, 7};
    input.trim = true;
    InlineExecutor executor;
    DistortWorkResult result = RunDistortWork(input, executor);
    Expect(result.status == DistortWorkStatus::Ready && result.cropX == 1 && result.cropY == 0 &&
               result.image.Width() == 2 && result.image.Height() == 1 && result.placement.origin == Vec2{6, 7} &&
               result.placement.size == Vec2{2, 1},
           "trim.transparent-border-crop-and-document-placement-literal");
    if (result.status == DistortWorkStatus::Ready)
    {
        static_cast<void>(PixelsEqual(result.image, expected, 2, 1, "trim.visible-bgra-pixels-literal"));
    }
}

void UniformMaskSharingAndTrimRefusal()
{
    constexpr std::array<std::uint8_t, 1> pixel{77};
    const Raster uniform = Gray(1, 1, pixel);
    DistortWorkInput input = Input(uniform, {Vec2{1, 0}, Vec2{5, 1}, Vec2{4, 4}, Vec2{0, 3}}, true);
    input.background = 1.0;
    InlineExecutor executor;
    DistortWorkResult result = RunDistortWork(input, executor);
    Expect(result.status == DistortWorkStatus::Ready && result.image == uniform &&
               result.image.Identity() == uniform.Identity() && result.placement.origin == Vec2{0, 0} &&
               result.placement.size == Vec2{5, 4},
           "mask.uniform-one-by-one-shares-image-and-reports-warp-bounds");

    input.trim = true;
    DistortWorkResult trimResult = RunDistortWork(input, executor);
    Expect(trimResult.status == DistortWorkStatus::Invalid && !trimResult.image.HasStorage(),
           "trim.mask-refused-without-output");
}

void SparseInputIdentityParityWithoutMaterialization()
{
    constexpr std::array<std::uint8_t, 16> pixels{9, 8, 7, 6, 19, 18, 17, 16, 29, 28, 27, 26, 39, 38, 37, 36};
    const Raster dense = Bgra(2, 2, pixels);
    const Raster sparse =
        Raster::MakeSparse(2, 2, RasterFormat::Bgra8888, Raster{}, 0, 0, {RasterTile{0, 0, Bgra(2, 2, pixels)}}, 0);
    DistortWorkInput denseInput = Input(dense, Rect(0, 0, 2, 2));
    DistortWorkInput sparseInput = Input(sparse, Rect(0, 0, 2, 2));
    InlineExecutor executor;
    DistortWorkResult denseResult = RunDistortWork(denseInput, executor);
    DistortWorkResult sparseResult = RunDistortWork(sparseInput, executor);
    Expect(denseResult.status == DistortWorkStatus::Ready && sparseResult.status == DistortWorkStatus::Ready,
           "sparse.identity-dense-and-sparse-ready");
    if (denseResult.status == DistortWorkStatus::Ready && sparseResult.status == DistortWorkStatus::Ready)
    {
        Expect(denseResult.image.Width() == 2 && denseResult.image.Height() == 2 && sparseResult.image.Width() == 2 &&
                   sparseResult.image.Height() == 2,
               "sparse.identity-parity-dimensions");
        static_cast<void>(PixelsEqual(denseResult.image, pixels, 2, 2, "sparse.identity-dense-literal-pixels"));
        static_cast<void>(PixelsEqual(sparseResult.image, pixels, 2, 2, "sparse.identity-sparse-literal-pixels"));
        const std::uint8_t* const denseBytes = denseResult.image.Data();
        const std::uint8_t* const sparseBytes = sparseResult.image.Data();
        const bool same = denseBytes != nullptr && sparseBytes != nullptr &&
                          std::equal(pixels.begin(), pixels.end(), denseBytes) &&
                          std::equal(pixels.begin(), pixels.end(), sparseBytes);
        if (!same)
        {
            std::printf("  sparse.identity parity actual dense/sparse pixel streams differ (2x2 BGRA)\n");
        }
        Expect(same, "sparse.identity-literal-parity-pixels");
    }
    Expect(sparse.IsSparse() && !sparse.IsMaterialized(), "sparse.identity-original-never-materialized");
}

void TileBoundaryPixels()
{
    constexpr int width = 257;
    constexpr int height = 3;
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * height * 4u);
    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            const std::size_t offset = (static_cast<std::size_t>(y) * width + static_cast<std::size_t>(x)) * 4u;
            pixels[offset] = static_cast<std::uint8_t>(x % 251);
            pixels[offset + 1] = static_cast<std::uint8_t>(y * 31);
            pixels[offset + 2] = static_cast<std::uint8_t>((x + y) % 253);
            pixels[offset + 3] = 255;
        }
    }
    const Raster source = Bgra(width, height, pixels);
    DistortWorkInput input = Input(source, Rect(0, 0, width, height));
    InlineExecutor executor;
    DistortWorkResult result = RunDistortWork(input, executor);
    Expect(result.status == DistortWorkStatus::Ready && result.image.Width() == width &&
               result.image.Height() == height,
           "tiles.257x3-output-dimensions");
    if (result.status == DistortWorkStatus::Ready)
    {
        const std::uint8_t* const actual = result.image.Data();
        bool boundariesMatch = actual != nullptr;
        for (const int y : {0, 1, 2})
        {
            for (const int x : {0, 254, 255, 256})
            {
                const std::size_t offset =
                    static_cast<std::size_t>(y) * static_cast<std::size_t>(result.image.Stride()) +
                    static_cast<std::size_t>(x) * 4u;
                const std::array<std::uint8_t, 4> expected{static_cast<std::uint8_t>(x % 251),
                                                           static_cast<std::uint8_t>(y * 31),
                                                           static_cast<std::uint8_t>((x + y) % 253), 255};
                boundariesMatch = boundariesMatch && std::equal(expected.begin(), expected.end(), actual + offset);
            }
        }
        if (!boundariesMatch)
        {
            std::printf("  tiles.257x3 actual=%dx%d samples=", result.image.Width(), result.image.Height());
            if (actual != nullptr)
            {
                for (const int y : {0, 1, 2})
                {
                    for (const int x : {254, 255, 256})
                    {
                        const std::size_t offset =
                            static_cast<std::size_t>(y) * static_cast<std::size_t>(result.image.Stride()) +
                            static_cast<std::size_t>(x) * 4u;
                        std::printf(" (%d,%d)=[%u,%u,%u,%u]", x, y, actual[offset], actual[offset + 1],
                                    actual[offset + 2], actual[offset + 3]);
                    }
                }
            }
            std::printf(" expected=[x%%251,y*31,(x+y)%%253,255]\n");
        }
        Expect(boundariesMatch, "tiles.257x3-coordinate-literals-across-256-boundary");
    }
}

void RefusalsAndPreviewBounds()
{
    constexpr std::array<std::uint8_t, 4> bgraPixel{1, 2, 3, 255};
    constexpr std::array<std::uint8_t, 1> grayPixel{90};
    const Raster color = Bgra(1, 1, bgraPixel);
    const Raster mask = Gray(1, 1, grayPixel);
    InlineExecutor executor;

    DistortWorkInput wrongFormat = Input(mask, Rect(0, 0, 1, 1));
    DistortWorkResult wrongFormatResult = RunDistortWork(wrongFormat, executor);
    Expect(wrongFormatResult.status == DistortWorkStatus::Invalid && !wrongFormatResult.image.HasStorage(),
           "refusal.gray-source-for-color-has-no-output");

    DistortWorkInput wrongMaskFormat = Input(color, Rect(0, 0, 1, 1), true);
    DistortWorkResult wrongMaskFormatResult = RunDistortWork(wrongMaskFormat, executor);
    Expect(wrongMaskFormatResult.status == DistortWorkStatus::Invalid && !wrongMaskFormatResult.image.HasStorage(),
           "refusal.bgra-source-for-mask-has-no-output");

    DistortWorkInput bowTie = Input(color, {Vec2{0, 0}, Vec2{2, 2}, Vec2{0, 2}, Vec2{2, 0}});
    DistortWorkResult bowTieResult = RunDistortWork(bowTie, executor);
    Expect(bowTieResult.status == DistortWorkStatus::Invalid && !bowTieResult.image.HasStorage(),
           "refusal.bow-tie-has-no-output");

    DistortWorkInput collapsed = Input(color, {Vec2{0, 0}, Vec2{2, 0}, Vec2{1, 0}, Vec2{0, 2}});
    DistortWorkResult collapsedResult = RunDistortWork(collapsed, executor);
    Expect(collapsedResult.status == DistortWorkStatus::Invalid && !collapsedResult.image.HasStorage(),
           "refusal.collapsed-corner-has-no-output");

    DistortWorkInput nonfinite = Input(color, Rect(0, 0, 2, 2));
    nonfinite.corners[1].x = std::numeric_limits<double>::infinity();
    DistortWorkResult nonfiniteResult = RunDistortWork(nonfinite, executor);
    Expect(nonfiniteResult.status == DistortWorkStatus::Invalid && !nonfiniteResult.image.HasStorage(),
           "refusal.nonfinite-coordinate-has-no-output");

    DistortWorkInput sideLimit = Input(color, Rect(0, 0, 30001, 1));
    sideLimit.previewLimit = 1;
    DistortWorkResult sideLimitResult = RunDistortWork(sideLimit, executor);
    Expect(sideLimitResult.status == DistortWorkStatus::TooLarge && !sideLimitResult.image.HasStorage(),
           "refusal.over-30000-side-before-preview-limit-has-no-output");

    DistortWorkInput areaLimit = Input(color, Rect(0, 0, 10001, 10000));
    areaLimit.previewLimit = 1;
    DistortWorkResult areaLimitResult = RunDistortWork(areaLimit, executor);
    Expect(areaLimitResult.status == DistortWorkStatus::TooLarge && !areaLimitResult.image.HasStorage(),
           "refusal.over-100-million-pixels-before-preview-limit-has-no-output");

    DistortWorkInput missing = Input(Raster{}, Rect(0, 0, 1, 1));
    DistortWorkResult missingResult = RunDistortWork(missing, executor);
    Expect(missingResult.status == DistortWorkStatus::Invalid && !missingResult.image.HasStorage(),
           "refusal.missing-source-has-no-output");

    DistortWorkInput negativePreview = Input(color, Rect(0, 0, 2, 2));
    negativePreview.previewLimit = -1.0;
    DistortWorkResult negativePreviewResult = RunDistortWork(negativePreview, executor);
    Expect(negativePreviewResult.status == DistortWorkStatus::Invalid && !negativePreviewResult.image.HasStorage(),
           "refusal.negative-preview-limit-has-no-output");

    DistortWorkInput nonfinitePreview = Input(color, Rect(0, 0, 2, 2));
    nonfinitePreview.previewLimit = std::numeric_limits<double>::quiet_NaN();
    DistortWorkResult nonfinitePreviewResult = RunDistortWork(nonfinitePreview, executor);
    Expect(nonfinitePreviewResult.status == DistortWorkStatus::Invalid && !nonfinitePreviewResult.image.HasStorage(),
           "refusal.nonfinite-preview-limit-has-no-output");

    DistortWorkInput nonfiniteBackground = Input(mask, Rect(0, 0, 2, 2), true);
    nonfiniteBackground.background = std::numeric_limits<double>::quiet_NaN();
    DistortWorkResult nonfiniteBackgroundResult = RunDistortWork(nonfiniteBackground, executor);
    Expect(nonfiniteBackgroundResult.status == DistortWorkStatus::Invalid &&
               !nonfiniteBackgroundResult.image.HasStorage(),
           "refusal.nonfinite-mask-background-has-no-output");
}

void ValidPreviewCap()
{
    constexpr std::array<std::uint8_t, 4> pixel{31, 47, 59, 255};
    constexpr std::array<std::uint8_t, 8> expected{31, 47, 59, 255, 31, 47, 59, 255};
    const Raster source = Bgra(1, 1, pixel);
    DistortWorkInput input = Input(source, Rect(0, 0, 4, 2));
    input.previewLimit = 2.0;
    InlineExecutor executor;
    DistortWorkResult result = RunDistortWork(input, executor);
    Expect(result.status == DistortWorkStatus::Ready && result.image.Width() == 2 && result.image.Height() == 1 &&
               result.placement.origin == Vec2{0, 0} && result.placement.size == Vec2{4, 2},
           "preview.valid-cap-limits-raster-while-retaining-full-bounds-placement");
    if (result.status == DistortWorkStatus::Ready)
    {
        static_cast<void>(PixelsEqual(result.image, expected, 2, 1, "preview.valid-cap-literal-bgra"));
    }
}

void LegacyPerspectiveAndReferenceParity()
{
    // retained numeric golden cases distortingWarpsTheLayerIntoTheShapeAsOneUndoStep.
    std::vector<std::uint8_t> pixels(20 * 20 * 4);
    for (std::size_t offset = 0; offset < pixels.size(); offset += 4)
    {
        pixels[offset + 2] = 255;
        pixels[offset + 3] = 255;
    }
    const auto source = Bgra(20, 20, pixels);
    auto input = Input(source, {Vec2{10, 10}, Vec2{60, 10}, Vec2{30, 30}, Vec2{10, 30}});
    input.placement.origin = {10, 10};
    input.placement.sampling = LayerSampling::Smooth;
    InlineExecutor executor;
    const auto result = RunDistortWork(input, executor);
    Expect(result.status == DistortWorkStatus::Ready && result.image.HasStorage() && result.image.Width() == 50 &&
               result.image.Height() == 20 && result.placement.origin == Vec2{10, 10} &&
               result.placement.size == Vec2{50, 20} && result.placement.rotation == 0,
           "legacy.perspective-bounds-and-upright-placement");
    if (result.status != DistortWorkStatus::Ready || !result.image.HasStorage() || result.image.Width() != 50 ||
        result.image.Height() != 20)
        return;
    const auto alpha = [&](int x, int y)
    { return result.image.Data()[static_cast<std::size_t>(y) * result.image.Stride() + x * 4 + 3]; };
    Expect(alpha(40, 2) == 255, "legacy.perspective-stretched-top-right-alpha-255");
    Expect(alpha(5, 15) == 255, "legacy.perspective-bottom-left-alpha-255");
    Expect(alpha(40, 18) == 0, "legacy.perspective-outside-slanted-right-alpha-zero");
    const auto reference = DistortWarp::Warp(source, input.placement, input.corners, false);
    Expect(reference && reference->image.Width() == result.image.Width() &&
               reference->image.Height() == result.image.Height() && reference->transform == result.placement &&
               reference->image.Data() && result.image.Data() &&
               std::equal(reference->image.Data(), reference->image.Data() + 50 * 20 * 4, result.image.Data()),
           "parity.perspective-all-scalar-reference-bytes-and-placement");
}

void ReservationIncludesSourceOutputAndTrimOverlap()
{
    const Raster source = Raster::MakeSparse(1024, 1024, RasterFormat::Bgra8888, Raster{}, 0, 0, {}, 0);
    DistortWorkInput input = Input(source, Rect(0, 0, 1000, 1500));
    const std::uint64_t sourceBytes = 1024ull * 1024ull * 4ull;
    const std::uint64_t outputBytes = 1000ull * 1500ull * 4ull;
    const std::uint64_t trimCopyReservation = PixelBuffer::RequiredBytes(static_cast<std::size_t>(outputBytes));
    input.trim = false;
    const std::uint64_t untrimmedReservation = DistortWorkMemoryBytes(input);
    input.trim = true;
    const std::uint64_t trimmedReservation = DistortWorkMemoryBytes(input);
    Expect(trimmedReservation >= sourceBytes + 2ull * outputBytes,
           "memory.sparse-source-and-two-output-buffers-exceed-independent-logical-floor");
    Expect(trimCopyReservation > 0 && trimmedReservation >= untrimmedReservation + trimCopyReservation,
           "memory.trim-reservation-adds-full-output-allocation-class");
    Expect(source.IsSparse() && !source.IsMaterialized(), "memory.reservation-planning-does-not-materialize-source");
    Expect(DistortWorkMemoryBytes(Input(source, Rect(0, 0, 30001, 1))) == 0,
           "memory.invalid-bounds-have-no-reservation");
}

struct CheckpointCounts final
{
    std::array<int, 3> counts{};
    CancelSource* cancellation = nullptr;
    DistortWorkStage cancelAt = DistortWorkStage::Trim;
};

std::size_t StageIndex(DistortWorkStage stage) noexcept
{
    return static_cast<std::size_t>(stage);
}

void CountCheckpoint(void* opaque, DistortWorkStage stage)
{
    auto& state = *static_cast<CheckpointCounts*>(opaque);
    ++state.counts[StageIndex(stage)];
    if (state.cancellation != nullptr && state.cancelAt == stage)
    {
        static_cast<void>(state.cancellation->Cancel());
    }
}

class ShutdownExecutor final : public Executor
{
  public:
    ShutdownExecutor() noexcept
    {
        static_cast<void>(_shutdown.request_stop());
    }
    [[nodiscard]] unsigned int Concurrency() const noexcept override
    {
        return 1;
    }
    [[nodiscard]] std::stop_token ShutdownToken() const noexcept override
    {
        return _shutdown.get_token();
    }
    void RunParticipants(void* context, Participant run, unsigned int) noexcept override
    {
        run(context);
    }

  private:
    std::stop_source _shutdown;
};

void ObserverStagesAndCancellation()
{
    constexpr std::array<std::uint8_t, 16> pixels{0, 0, 0, 0, 80, 80, 80, 80, 160, 160, 160, 160, 255, 255, 255, 255};
    const Raster source = Bgra(2, 2, pixels);
    DistortWorkInput input = Input(source, {Vec2{0, 0}, Vec2{3, 0}, Vec2{2, 3}, Vec2{0, 2}});
    input.trim = true;
    InlineExecutor executor;

    CheckpointCounts completeCounts;
    DistortWorkResult complete =
        RunDistortWork(input, executor, {}, DistortWorkObserver{&completeCounts, &CountCheckpoint});
    Expect(complete.status == DistortWorkStatus::Ready && completeCounts.counts == std::array<int, 3>{1, 1, 1},
           "observer.success-reaches-source-pixels-trim-once");

    constexpr std::array<std::array<int, 3>, 3> expectedReached{{{1, 0, 0}, {1, 1, 0}, {1, 1, 1}}};
    constexpr std::array<DistortWorkStage, 3> stages{DistortWorkStage::Source, DistortWorkStage::Pixels,
                                                     DistortWorkStage::Trim};
    constexpr std::array<const char*, 3> names{"cancel.source", "cancel.pixels", "cancel.trim"};
    for (std::size_t index = 0; index < stages.size(); ++index)
    {
        CancelSource cancellation;
        CheckpointCounts counts;
        counts.cancellation = &cancellation;
        counts.cancelAt = stages[index];
        DistortWorkResult result =
            RunDistortWork(input, executor, cancellation.Token(), DistortWorkObserver{&counts, &CountCheckpoint});
        Expect(result.status == DistortWorkStatus::Cancelled && !result.image.HasStorage() &&
                   counts.counts == expectedReached[index],
               names[index]);
    }

    ShutdownExecutor shutdown;
    CheckpointCounts shutdownCounts;
    DistortWorkResult shutdownResult =
        RunDistortWork(input, shutdown, {}, DistortWorkObserver{&shutdownCounts, &CountCheckpoint});
    Expect(shutdownResult.status == DistortWorkStatus::Cancelled && !shutdownResult.image.HasStorage() &&
               shutdownCounts.counts == std::array<int, 3>{0, 0, 0},
           "cancel.executor-shutdown-token-cancels-before-observer-work");

    for (const auto stage : stages)
    {
        struct Failure final
        {
            DistortWorkStage stage;
            bool reached = false;
        } failure{stage};
        bool propagated = false;
        try
        {
            (void)RunDistortWork(input, executor, {},
                                 {&failure, [](void* raw, DistortWorkStage current)
                                  {
                                      auto& state = *static_cast<Failure*>(raw);
                                      if (state.stage == current)
                                      {
                                          state.reached = true;
                                          throw std::bad_alloc{};
                                      }
                                  }});
        }
        catch (const std::bad_alloc&)
        {
            propagated = true; // Test boundary observes the job runner's required allocation-failure input.
        }
        Expect(propagated && failure.reached && source.HasStorage() && source.Width() == 2 && source.Height() == 2 &&
                   source.Data() && std::equal(pixels.begin(), pixels.end(), source.Data()),
               "allocation.reached-checkpoint-propagates-with-source-retained");
    }
}

} // namespace

int RunDistortWorkTests()
{
    gFailures = 0;
    gAssertions = 0;
    IdentityNearestAndFlips();
    BilinearLiteralSamples();
    MaskBackgroundAndEdgeCoverage();
    AlphaTrimCropAndPlacement();
    UniformMaskSharingAndTrimRefusal();
    SparseInputIdentityParityWithoutMaterialization();
    TileBoundaryPixels();
    RefusalsAndPreviewBounds();
    ValidPreviewCap();
    ReservationIncludesSourceOutputAndTrimOverlap();
    LegacyPerspectiveAndReferenceParity();
    ObserverStagesAndCancellation();
    std::printf("%d distort-work assertion(s), %d failure(s).\n", gAssertions, gFailures);
    return gFailures;
}
