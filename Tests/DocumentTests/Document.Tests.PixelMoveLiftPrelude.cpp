#include "Document.Tests.PixelMoveLiftPrelude.h"

#include "Document.h"
#include "RasterEditWork.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <span>
#include <utility>
#include <vector>

namespace PixelMoveLiftPreludeTests
{
namespace
{
void Expect(int& failures, bool condition, const char* fact)
{
    std::printf("%s pixelmove-lift-prelude.%s\n", condition ? "PASS" : "FAIL", fact);
    failures += condition ? 0 : 1;
}

Raster MakeBgra(int width, int height, std::span<const std::uint8_t> bytes)
{
    return Raster::Make(width, height, RasterFormat::Bgra8888, bytes.data(), bytes.size());
}

ImageLayer MakeImage(const Raster& image)
{
    ImageLayer layer = ImageLayer::Blank(L"Pixel move lift prelude",
                                         Vec2{static_cast<double>(image.Width()), static_cast<double>(image.Height())});
    layer.asset = RasterAsset{image, Raster::MakeThumbnail(image), L"Pixel move lift prelude"};
    return layer;
}

DocumentSelection Rect(double x, double y, double width, double height)
{
    return DocumentSelection{
        false, SelectionLoops{{Vec2{x, y}, Vec2{x + width, y}, Vec2{x + width, y + height}, Vec2{x, y + height}}}};
}

RasterEditInput MakeInput(const Raster& image, int canvasWidth, int canvasHeight, DocumentSelection selection)
{
    RasterEditInput input;
    input.layer = MakeImage(image);
    input.selection = std::move(selection);
    input.canvasWidth = canvasWidth;
    input.canvasHeight = canvasHeight;
    input.pixelLimit = kMaxLayerPixels;
    input.kind = RasterEditKind::PixelMove;
    return input;
}

bool PixelIs(const Raster& image, int x, int y, const std::array<std::uint8_t, 4>& expected)
{
    if (!image.HasStorage() || image.Format() != RasterFormat::Bgra8888 || x < 0 || y < 0 || x >= image.Width() ||
        y >= image.Height())
        return false;
    std::array<std::uint8_t, 4> actual{};
    image.ReadRegion(x, y, 1, 1, actual.data(), 4, 0);
    return actual == expected;
}

// Independent legacy fixture bytes, not a comparison between aliases of the same Raster storage.
bool InputPixelsUnchanged(const RasterEditInput& input, const Raster& source)
{
    if (!input.layer.asset || input.layer.asset->image.Width() != 100 || input.layer.asset->image.Height() != 40 ||
        source.Width() != 100 || source.Height() != 40)
        return false;
    for (int y = 0; y < 40; ++y)
        for (int x = 0; x < 100; ++x)
        {
            const auto expected =
                x < 50 ? std::array<std::uint8_t, 4>{0, 0, 255, 255} : std::array<std::uint8_t, 4>{255, 0, 0, 255};
            if (!PixelIs(input.layer.asset->image, x, y, expected) || !PixelIs(source, x, y, expected))
                return false;
        }
    return true;
}

bool ResultPixelsAre(const RasterEditResult& result, bool duplicate)
{
    if (result.status != RasterEditStatus::Ready || !result.layer || !result.layer->asset || !result.selection)
        return false;
    const Raster& output = result.layer->asset->image;
    constexpr std::array<std::uint8_t, 4> transparent{0, 0, 0, 0};
    constexpr std::array<std::uint8_t, 4> red{0, 0, 255, 255};
    constexpr std::array<std::uint8_t, 4> blue{255, 0, 0, 255};
    const auto bounds = result.selection->Bounds();
    return (duplicate || PixelIs(output, 15, 15, transparent)) && PixelIs(output, 75, 20, red) &&
           PixelIs(output, 85, 20, blue) && PixelIs(output, 5, 5, red) && bounds &&
           bounds.value() == SelectionBounds{70, 15, 80, 25};
}

Raster TwoColorImage()
{
    constexpr int width = 100;
    constexpr int height = 40;
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u);
    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            const std::size_t offset =
                (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)) * 4u;
            bytes[offset] = x < 50 ? 0 : 255;
            bytes[offset + 1] = 0;
            bytes[offset + 2] = x < 50 ? 255 : 0;
            bytes[offset + 3] = 255;
        }
    }
    return MakeBgra(width, height, bytes);
}

void ZeroOffsetOpeningAndSourcePreservation(int& failures, const Raster& source)
{
    RasterEditInput input = MakeInput(source, 100, 40, Rect(10, 10, 10, 10));
    const PixelMoveLiftResult result = PreparePixelMoveLiftForWork(input);
    Expect(failures, result.status == PixelMoveLiftStatus::Ready && result.opening,
           "zero-offset-opening-ready-with-opaque-owner");
    Expect(failures, InputPixelsUnchanged(input, source), "zero-offset-opening-leaves-source-bytes-unchanged");

    RasterEditInput moved = input;
    moved.offset = Vec2{60, 5};
    moved.pixelMoveOpening = result.opening;
    const RasterEditResult moveResult = RunRasterEdit(moved);
    Expect(failures, ResultPixelsAre(moveResult, false), "move-opening-produces-literal-hole-landing-and-bounds");

    RasterEditInput duplicated = input;
    duplicated.offset = Vec2{60, 5};
    duplicated.duplicate = true;
    duplicated.pixelMoveOpening = result.opening;
    const RasterEditResult duplicateResult = RunRasterEdit(duplicated);
    Expect(failures,
           ResultPixelsAre(duplicateResult, true) &&
               PixelIs(duplicateResult.layer->asset->image, 15, 15, std::array<std::uint8_t, 4>{0, 0, 255, 255}),
           "duplicate-opening-preserves-source-and-produces-literal-landing-and-bounds");
    Expect(failures, InputPixelsUnchanged(input, source), "move-and-duplicate-consumption-leave-source-unchanged");

    RasterEditInput variedOpeningOptions = input;
    variedOpeningOptions.offset = Vec2{60, 5};
    variedOpeningOptions.duplicate = true;
    const PixelMoveLiftResult variedOptions = PreparePixelMoveLiftForWork(variedOpeningOptions);
    Expect(failures, variedOptions.status == PixelMoveLiftStatus::Ready && variedOptions.opening,
           "opening-ignores-offset-and-duplicate-options");
}

void RefusesNoPixels(int& failures)
{
    constexpr std::array<std::uint8_t, 4> transparentPixel{0, 0, 0, 0};
    const Raster transparent = MakeBgra(1, 1, transparentPixel);
    RasterEditInput transparentInput = MakeInput(transparent, 1, 1, Rect(0, 0, 1, 1));
    const PixelMoveLiftResult transparentResult = PreparePixelMoveLiftForWork(transparentInput);
    // Swift BrushStroke.swift:681-697 returns an image for any intersecting region, including all alpha0.
    Expect(failures,
           transparentResult.status == PixelMoveLiftStatus::Ready && transparentResult.opening &&
               PixelIs(transparent, 0, 0, transparentPixel),
           "transparent-selected-source-region-opens-like-legacy");

    constexpr std::array<std::uint8_t, 4> opaquePixel{0, 0, 255, 255};
    const Raster small = MakeBgra(1, 1, opaquePixel);
    RasterEditInput outsideInput = MakeInput(small, 100, 40, Rect(10, 10, 10, 10));
    const PixelMoveLiftResult outsideResult = PreparePixelMoveLiftForWork(outsideInput);
    Expect(failures, outsideResult.status == PixelMoveLiftStatus::NoPixels && !outsideResult.opening,
           "selection-outside-source-returns-no-pixels-without-opening");
}

void RefusesInvalidEligibilityAndCancellation(int& failures, const Raster& source)
{
    RasterEditInput seed = MakeInput(source, 100, 40, Rect(10, 10, 10, 10));
    const auto refused = [&](RasterEditInput input, const char* fact)
    {
        const PixelMoveLiftResult result = PreparePixelMoveLiftForWork(input);
        Expect(failures, result.status == PixelMoveLiftStatus::Invalid && !result.opening, fact);
    };

    RasterEditInput noSelection = seed;
    noSelection.selection.reset();
    refused(std::move(noSelection), "missing-selection-is-invalid-without-opening");
    RasterEditInput emptySelection = seed;
    emptySelection.selection = DocumentSelection{false, SelectionLoops{}};
    refused(std::move(emptySelection), "empty-selection-is-invalid-without-opening");
    RasterEditInput mask = seed;
    mask.mask = true;
    refused(std::move(mask), "mask-target-is-invalid-without-opening");
    RasterEditInput wrongKind = seed;
    wrongKind.kind = RasterEditKind::Fill;
    refused(std::move(wrongKind), "wrong-edit-kind-is-invalid-without-opening");
    RasterEditInput noAsset = seed;
    noAsset.layer.asset.reset();
    refused(std::move(noAsset), "assetless-target-is-invalid-without-opening");
    constexpr std::array<std::uint8_t, 1> grayPixel{255};
    const Raster gray = Raster::Make(1, 1, RasterFormat::Gray8, grayPixel.data(), grayPixel.size());
    RasterEditInput wrongFormat = MakeInput(gray, 1, 1, Rect(0, 0, 1, 1));
    refused(std::move(wrongFormat), "non-bgra-image-is-invalid-without-opening");
    RasterEditInput badDimensions = seed;
    badDimensions.canvasWidth = 0;
    refused(std::move(badDimensions), "bad-canvas-dimensions-are-invalid-without-opening");
    RasterEditInput badPixelLimit = seed;
    badPixelLimit.pixelLimit = 0;
    refused(std::move(badPixelLimit), "bad-pixel-limit-is-invalid-without-opening");

    CancelSource cancellation;
    (void)cancellation.Cancel();
    const PixelMoveLiftResult cancelled = PreparePixelMoveLiftForWork(seed, cancellation.Token());
    Expect(failures, cancelled.status == PixelMoveLiftStatus::Cancelled && !cancelled.opening,
           "pre-cancelled-opening-returns-cancelled-without-opening");
}

void CancelsDuringLift(int& failures, const Raster& source)
{
    struct Progress final
    {
        CancelSource cancellation;
        std::size_t lastCompleted = 0;
        unsigned int visits = 0;
    } progress;
    RasterEditInput input = MakeInput(source, 100, 40, Rect(10, 10, 10, 10));
    input.progressContext = &progress;
    input.progress = [](void* raw, std::size_t completed, std::size_t) noexcept
    {
        auto& state = *static_cast<Progress*>(raw);
        state.lastCompleted = completed;
        ++state.visits;
        if (completed == 5)
            (void)state.cancellation.Cancel();
    };
    const auto result = PreparePixelMoveLiftForWork(input, progress.cancellation.Token());
    Expect(failures,
           progress.visits > 0 && progress.lastCompleted == 5 && result.status == PixelMoveLiftStatus::Cancelled &&
               !result.opening,
           "actual-row-progress-cancellation-publishes-no-opening");
    Expect(failures, InputPixelsUnchanged(input, source), "cancelled-lift-preserves-independent-source-literals");
}

void RefusesExistingOpening(int& failures, const Raster& source)
{
    RasterEditInput input = MakeInput(source, 100, 40, Rect(10, 10, 10, 10));
    const PixelMoveLiftResult prepared = PreparePixelMoveLiftForWork(input);
    if (prepared.status != PixelMoveLiftStatus::Ready || !prepared.opening)
    {
        Expect(failures, false, "existing-opening-fixture-is-prepared");
        return;
    }
    input.pixelMoveOpening = prepared.opening;
    const PixelMoveLiftResult refused = PreparePixelMoveLiftForWork(input);
    Expect(failures, refused.status == PixelMoveLiftStatus::Invalid && !refused.opening,
           "preexisting-opening-is-refused");
    Expect(failures, input.pixelMoveOpening == prepared.opening, "preexisting-opening-refusal-retains-captured-owner");
}
} // namespace

void Run(int& failures)
{
    const Raster source = TwoColorImage();
    if (!source.HasStorage())
    {
        Expect(failures, false, "two-color-legacy-fixture-created");
        return;
    }
    Expect(failures,
           PixelIs(source, 5, 5, std::array<std::uint8_t, 4>{0, 0, 255, 255}) &&
               PixelIs(source, 75, 20, std::array<std::uint8_t, 4>{255, 0, 0, 255}),
           "two-color-fixture-has-literal-red-left-and-blue-right");
    ZeroOffsetOpeningAndSourcePreservation(failures, source);
    RefusesNoPixels(failures);
    RefusesInvalidEligibilityAndCancellation(failures, source);
    RefusesExistingOpening(failures, source);
    CancelsDuringLift(failures, source);
}
} // namespace PixelMoveLiftPreludeTests
