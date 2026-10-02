#include "ClipboardMergedBudgetTests.h"

#include "CopyPixelsWork.h"
#include "PixelBuffer.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <optional>

namespace
{
int gFailures = 0;

void Expect(bool condition, const char* name)
{
    std::printf("%s clipboard-merged-budget.%s\n", condition ? "PASS" : "FAIL", name);
    gFailures += condition ? 0 : 1;
}

DocumentSelection Rect(double minX, double minY, double maxX, double maxY, bool antialiased = false)
{
    return DocumentSelection{antialiased,
                             SelectionLoops{{Vec2{minX, minY}, Vec2{maxX, minY}, Vec2{maxX, maxY}, Vec2{minX, maxY}}}};
}

std::uint64_t Expected(int canvasWidth, int canvasHeight, int regionWidth, int regionHeight,
                       const std::optional<DocumentSelection>& selection)
{
    const std::size_t regionPixels = static_cast<std::size_t>(regionWidth) * regionHeight;
    const std::size_t canvasPixels = static_cast<std::size_t>(canvasWidth) * canvasHeight;
    std::uint64_t bytes = 4096 + sizeof(CopyPixelsInput) + sizeof(CopyPixelsResult);
    bytes += PixelBuffer::RequiredBytes(regionPixels * 4u);
    bytes += static_cast<std::uint64_t>(regionWidth) * 5u;
    if (selection.has_value())
    {
        bytes += PixelBuffer::RequiredBytes(canvasPixels);
        if (selection.value().antialiased)
        {
            bytes += (static_cast<std::uint64_t>(canvasWidth) + 2u) * canvasHeight * 2u * sizeof(float);
        }
        else
        {
            bytes += selection.value().loops.PointCount() * 32u;
        }
    }
    return bytes;
}

void BasicAndBoundaryCases()
{
    Expect(CopyMergedPixelsMemoryBytes(1, 1, std::nullopt) ==
               4096u + sizeof(CopyPixelsInput) + sizeof(CopyPixelsResult) + 68u + 5u,
           "one-pixel-full-canvas-literal-allocation-sizes");
    Expect(CopyMergedPixelsMemoryBytes(2, 3, std::nullopt) == Expected(2, 3, 2, 3, std::nullopt),
           "small-full-canvas-output-and-row-metadata");
    Expect(CopyMergedPixelsMemoryBytes(2, 3, std::optional<DocumentSelection>{}) ==
               CopyMergedPixelsMemoryBytes(2, 3, std::nullopt),
           "empty-optional-matches-no-selection");
    Expect(CopyMergedPixelsMemoryBytes(0, 1, std::nullopt) == 0 && CopyMergedPixelsMemoryBytes(1, 0, std::nullopt) == 0,
           "zero-dimension-refused");
    Expect(CopyMergedPixelsMemoryBytes(-1, 2, std::nullopt) == 0 &&
               CopyMergedPixelsMemoryBytes(2, -1, std::nullopt) == 0,
           "negative-dimension-refused");
    Expect(CopyMergedPixelsMemoryBytes(30001, 1, std::nullopt) == 0 &&
               CopyMergedPixelsMemoryBytes(1, 30001, std::nullopt) == 0,
           "side-over-30000-refused");
    Expect(CopyMergedPixelsMemoryBytes(30000, 1, std::nullopt) == Expected(30000, 1, 30000, 1, std::nullopt),
           "maximum-width-side-accepted");
    Expect(CopyMergedPixelsMemoryBytes(1, 30000, std::nullopt) == Expected(1, 30000, 1, 30000, std::nullopt),
           "maximum-height-side-accepted");
    Expect(CopyMergedPixelsMemoryBytes(10000, 10000, std::nullopt) ==
               Expected(10000, 10000, 10000, 10000, std::nullopt),
           "exactly-100-million-full-canvas-accepted");
    Expect(CopyMergedPixelsMemoryBytes(10001, 10000, std::nullopt) == 0, "full-canvas-over-100-million-refused");
}

void SelectionRegionCases()
{
    const std::optional<DocumentSelection> empty{DocumentSelection{}};
    Expect(CopyMergedPixelsMemoryBytes(8, 6, empty) == 0, "explicit-empty-selection-refused");
    Expect(CopyMergedPixelsMemoryBytes(8, 6, Rect(9, 1, 12, 4)) == 0, "selection-outside-right-edge-refused");
    Expect(CopyMergedPixelsMemoryBytes(8, 6, Rect(-5, 1, -1, 4)) == 0, "selection-outside-left-edge-refused");
    Expect(CopyMergedPixelsMemoryBytes(8, 6, Rect(1, 7, 4, 9)) == 0, "selection-outside-bottom-edge-refused");
    Expect(CopyMergedPixelsMemoryBytes(8, 6, Rect(1, -5, 4, -1)) == 0, "selection-outside-top-edge-refused");
    Expect(CopyMergedPixelsMemoryBytes(8, 6, Rect(-2.2, 1, 3.2, 3)) ==
               Expected(8, 6, 4, 2, std::optional<DocumentSelection>{Rect(-2.2, 1, 3.2, 3)}),
           "negative-edge-selection-clipped-to-canvas");
    Expect(CopyMergedPixelsMemoryBytes(8, 6, Rect(5, 3, 10, 8)) ==
               Expected(8, 6, 3, 3, std::optional<DocumentSelection>{Rect(5, 3, 10, 8)}),
           "right-and-bottom-selection-clipped-to-canvas");
    Expect(CopyMergedPixelsMemoryBytes(8, 6, Rect(2.25, 1.25, 5.75, 4.75)) ==
               Expected(8, 6, 4, 4, std::optional<DocumentSelection>{Rect(2.25, 1.25, 5.75, 4.75)}),
           "fractional-bounds-floor-and-ceil-with-tolerance");
    Expect(CopyMergedPixelsMemoryBytes(8, 6, Rect(2, 2, 2.0005, 4)) == 0, "subpixel-width-collapsed-by-copy-tolerance");

    const double notANumber = (std::numeric_limits<double>::quiet_NaN)();
    const double infinity = (std::numeric_limits<double>::infinity)();
    const std::optional<DocumentSelection> nonfinite{Rect(notANumber, 1, notANumber, 4)};
    const std::optional<DocumentSelection> infinite{Rect(1, 1, infinity, 4)};
    Expect(CopyMergedPixelsMemoryBytes(8, 6, nonfinite) == 0, "nonfinite-selection-bounds-refused");
    Expect(CopyMergedPixelsMemoryBytes(8, 6, infinite) == 0, "infinite-selection-bounds-refused");
}

void SelectionStorageCases()
{
    const std::optional<DocumentSelection> antialiased{Rect(1, 1, 4, 3, true)};
    const std::optional<DocumentSelection> hard{Rect(1, 1, 4, 3, false)};
    const std::uint64_t expectedAA = Expected(8, 6, 3, 2, antialiased);
    const std::uint64_t expectedHard = Expected(8, 6, 3, 2, hard);
    Expect(CopyMergedPixelsMemoryBytes(8, 6, antialiased) == expectedAA,
           "antialiased-selection-adds-canvas-coverage-and-float-scratch");
    Expect(CopyMergedPixelsMemoryBytes(8, 6, hard) == expectedHard,
           "hard-selection-adds-canvas-coverage-and-point-storage");
    Expect(expectedAA - expectedHard == 120u * sizeof(float) - 4u * 32u,
           "aa-and-hard-selection-costs-follow-distinct-stated-allocations");

    const std::optional<DocumentSelection> twoLoops{
        DocumentSelection{false, SelectionLoops{{Vec2{1, 1}, Vec2{3, 1}, Vec2{3, 3}, Vec2{1, 3}},
                                                {Vec2{5, 2}, Vec2{7, 2}, Vec2{7, 4}, Vec2{5, 4}}}}};
    Expect(CopyMergedPixelsMemoryBytes(8, 6, twoLoops) == Expected(8, 6, 6, 3, twoLoops),
           "hard-selection-multiple-loops-charge-all-eight-points");
    const std::optional<DocumentSelection> twoLoopsAA{
        DocumentSelection{true, SelectionLoops{{Vec2{1, 1}, Vec2{3, 1}, Vec2{3, 3}, Vec2{1, 3}},
                                               {Vec2{5, 2}, Vec2{7, 2}, Vec2{7, 4}, Vec2{5, 4}}}}};
    Expect(CopyMergedPixelsMemoryBytes(8, 6, twoLoopsAA) == Expected(8, 6, 6, 3, twoLoopsAA),
           "antialiased-multiple-loops-charge-canvas-scratch-not-points");
    Expect(CopyMergedPixelsMemoryBytes(30000, 2, Rect(29990, 0, 30000, 2, true)) ==
               Expected(30000, 2, 10, 2, std::optional<DocumentSelection>{Rect(29990, 0, 30000, 2, true)}),
           "large-wide-canvas-narrow-aa-selection");
}

void PixelBufferControlCases()
{
    constexpr std::size_t kSixteenMiB = 16u * 1024u * 1024u;
    Expect(PixelBuffer::RequiredBytes(1) == 65u && PixelBuffer::RequiredBytes(4) == 68u &&
               PixelBuffer::RequiredBytes(17) == 81u,
           "required-bytes-live-function-small-literal-allocation-sizes");
    Expect(PixelBuffer::RequiredBytes(kSixteenMiB) == 16u * 1024u * 1024u + 64u &&
               PixelBuffer::RequiredBytes(kSixteenMiB + 1u) == 32u * 1024u * 1024u + 64u,
           "required-bytes-live-function-size-class-boundary-literals");
    Expect(CopyMergedPixelsMemoryBytes(3, 1, std::nullopt) ==
               4096u + sizeof(CopyPixelsInput) + sizeof(CopyPixelsResult) + 76u + 15u,
           "three-pixel-output-uses-fixed-request-plus-64-byte-record");
}
} // namespace

int RunClipboardMergedBudgetTests()
{
    BasicAndBoundaryCases();
    SelectionRegionCases();
    SelectionStorageCases();
    PixelBufferControlCases();
    return gFailures;
}
