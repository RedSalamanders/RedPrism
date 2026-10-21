#include "Document.Tests.SelectionOffsetNumeric.h"

#include "BoundedClipper/BoundedSelectionOffset.h"
#include "Selection.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <vector>

namespace
{
int gAssertions = 0;
int gFailures = 0;

void Expect(bool condition, const char* name) noexcept
{
    ++gAssertions;
    std::printf("%s %s\n", condition ? "PASS" : "FAIL", name);
    gFailures += condition ? 0 : 1;
}

bool HasBounds(const std::vector<SelectionLoop>& loops, double minX, double minY, double maxX, double maxY)
{
    const auto bounds = SelectionGeometry::Bounds(loops);
    return bounds && bounds->minX == minX && bounds->minY == minY && bounds->maxX == maxX && bounds->maxY == maxY;
}

bool HasPixel(const std::vector<SelectionLoop>& loops, int width, int height, int x, int y, std::uint8_t expected)
{
    const Raster coverage = SelectionGeometry::Coverage(loops, width, height, false);
    return coverage.Data() != nullptr && x >= 0 && y >= 0 && x < width && y < height &&
           coverage.Data()[static_cast<std::size_t>(y) * static_cast<std::size_t>(coverage.Stride()) +
                           static_cast<std::size_t>(x)] == expected;
}

void LegacyNumericGoldens()
{
    const auto rect = SelectionGeometry::Rect(40, 40, 20, 20);
    const auto grown = BoundedSelectionOffset::Run(rect, 5, 100, 100);
    Expect(grown.has_value(), "selection-offset-numeric.expand5-produces-result");
    if (grown)
    {
        Expect(HasBounds(*grown, 35, 35, 65, 65), "selection-offset-numeric.expand5-bounds-35-to-65");
        Expect(HasPixel(*grown, 100, 100, 37, 50, 255) && HasPixel(*grown, 100, 100, 33, 50, 0),
               "selection-offset-numeric.expand5-coverage-37-selected-33-clear");
    }
    else
    {
        Expect(false, "selection-offset-numeric.expand5-bounds-35-to-65");
        Expect(false, "selection-offset-numeric.expand5-coverage-37-selected-33-clear");
    }

    std::optional<std::vector<SelectionLoop>> contracted;
    if (grown)
        contracted = BoundedSelectionOffset::Run(*grown, -8, 100, 100);
    Expect(contracted.has_value(), "selection-offset-numeric.expand5-contract8-produces-result");
    if (contracted)
        Expect(HasBounds(*contracted, 43, 43, 57, 57), "selection-offset-numeric.expand5-contract8-bounds-43-to-57");
    else
        Expect(false, "selection-offset-numeric.expand5-contract8-bounds-43-to-57");

    const auto fullCanvas = SelectionGeometry::Rect(0, 0, 100, 100);
    const auto expandedCanvas = BoundedSelectionOffset::Run(fullCanvas, 10, 100, 100);
    Expect(expandedCanvas.has_value(), "selection-offset-numeric.full-canvas-expand-produces-result");
    if (expandedCanvas)
        Expect(HasBounds(*expandedCanvas, 0, 0, 100, 100),
               "selection-offset-numeric.full-canvas-expand-clipped-to-0-100");
    else
        Expect(false, "selection-offset-numeric.full-canvas-expand-clipped-to-0-100");

    const auto contractedCanvas = BoundedSelectionOffset::Run(fullCanvas, -10, 100, 100);
    Expect(contractedCanvas.has_value(), "selection-offset-numeric.full-canvas-contract10-produces-result");
    if (contractedCanvas)
        Expect(HasPixel(*contractedCanvas, 100, 100, 5, 50, 0) && HasPixel(*contractedCanvas, 100, 100, 50, 50, 255),
               "selection-offset-numeric.full-canvas-contract10-edge-clear-center-selected");
    else
        Expect(false, "selection-offset-numeric.full-canvas-contract10-edge-clear-center-selected");

    const auto empty = contractedCanvas ? BoundedSelectionOffset::Run(*contractedCanvas, -45, 100, 100)
                                        : std::optional<std::vector<SelectionLoop>>{};
    Expect(empty.has_value(), "selection-offset-numeric.full-canvas-contract10-contract45-produces-result");
    if (empty)
        Expect(empty->empty(), "selection-offset-numeric.full-canvas-contract10-contract45-explicit-empty-vector");
    else
        Expect(false, "selection-offset-numeric.full-canvas-contract10-contract45-explicit-empty-vector");
}

void CompareLegacyReference()
{
    const std::vector<std::vector<SelectionLoop>> inputs{
        // Two disjoint components.
        {SelectionLoop{{10, 10}, {24, 10}, {24, 24}, {10, 24}}, SelectionLoop{{40, 12}, {55, 12}, {55, 27}, {40, 27}}},
        // Outer clockwise loop and inner counterclockwise hole.
        {SelectionLoop{{10, 10}, {60, 10}, {60, 60}, {10, 60}}, SelectionLoop{{25, 25}, {25, 45}, {45, 45}, {45, 25}}},
        // Concave L outline.
        {SelectionLoop{{10, 10}, {50, 10}, {50, 22}, {24, 22}, {24, 50}, {10, 50}}},
        // Fractional concave outline.
        {SelectionLoop{{12.25, 11.5}, {48.75, 13.125}, {42.5, 27.25}, {27.75, 23.5}, {23.125, 46.875}, {9.5, 39.25}}},
    };
    constexpr int amounts[]{1, 5, 500, -1, -5, -500};
    const auto canvas = SelectionGeometry::Rect(0, 0, 100, 100);
    unsigned int index = 0;
    for (const auto& input : inputs)
    {
        for (const int amount : amounts)
        {
            auto expected = SelectionGeometry::Offset(input, static_cast<double>(amount));
            if (amount > 0)
                expected = SelectionGeometry::Intersect(expected, canvas);
            const auto actual = BoundedSelectionOffset::Run(input, amount, 100, 100);
            char name[96]{};
            std::snprintf(name, sizeof(name), "selection-offset-numeric.legacy-vector-input%u-amount%+d", index,
                          amount);
            Expect(actual.has_value() && *actual == expected, name);
        }
        ++index;
    }
}
} // namespace

int RunSelectionOffsetNumericTests()
{
    gAssertions = 0;
    gFailures = 0;
    LegacyNumericGoldens();
    CompareLegacyReference();
    return gFailures;
}
