#include "Document.Tests.SelectionOutlineRasterViews.h"

#include "Common/SelectionOutlineRasterWork.h"
#include "Common/SharedPublicationMemory.h"

#include <objbase.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <utility>
#include <vector>

namespace
{
unsigned int gPassed = 0;
unsigned int gFailed = 0;

void Expect(bool condition, const char* name) noexcept
{
    std::printf("%s selection-outline-raster-views.%s\n", condition ? "PASS" : "FAIL", name);
    condition ? ++gPassed : ++gFailed;
}

class ComApartment final
{
  public:
    ComApartment() noexcept : _result(CoInitializeEx(nullptr, COINIT_MULTITHREADED)) {}
    ~ComApartment()
    {
        if (SUCCEEDED(_result))
            CoUninitialize();
    }
    [[nodiscard]] bool Ready() const noexcept
    {
        return SUCCEEDED(_result);
    }

  private:
    HRESULT _result;
};

using Raster = PreparedSelectionOutlineRaster;
using Grid = Raster::GridKey;

[[nodiscard]] SelectionLoops Rectangle(double left = 0.0, double top = 0.0, double right = 40.0, double bottom = 32.0)
{
    return SelectionLoops{std::vector<SelectionLoop>{{{left, top}, {right, top}, {right, bottom}, {left, bottom}}}};
}

[[nodiscard]] bool Prepare(const SelectionLoops& source, Grid grid, Raster& output)
{
    auto publicationMemory = SharedPublicationMemory::CreateForWork();
    return publicationMemory && Raster::Prepare(source, grid, std::move(publicationMemory), {}, {}, output) == S_OK;
}

[[nodiscard]] std::uint8_t At(const Raster& raster, std::size_t plane, std::uint32_t x, std::uint32_t y) noexcept
{
    const auto bytes = raster.Plane(plane);
    const auto& key = raster.Key();
    return plane < Raster::PlaneCount && x < key.width && y < key.height &&
                   bytes.size() == static_cast<std::size_t>(key.width) * key.height
               ? bytes[static_cast<std::size_t>(y) * key.width + x]
               : std::uint8_t{0};
}

[[nodiscard]] std::uint32_t PixelAt(double pixel) noexcept
{
    return static_cast<std::uint32_t>(std::floor(pixel));
}

[[nodiscard]] bool NearByte(std::uint8_t actual, int expected) noexcept
{
    return std::abs(static_cast<int>(actual) - expected) <= 1;
}

void TestDpiWidthAndDashReferences()
{
    // Independent strip-area reference for a horizontal 1-DIP stroke centered at y=32.25 px.
    // Rows are [31,32,33]; coverage is intersection area of each unit pixel square and the strip.
    constexpr std::array<float, 3> dpi{96.0f, 144.0f, 192.0f};
    constexpr std::array<double, 3> scale{1.0, 1.5, 2.0};
    constexpr std::array<std::array<int, 3>, 3> rowCoverage{{{{64, 191, 0}}, {{128, 255, 0}}, {{191, 255, 64}}}};
    const auto source = Rectangle();
    bool allWidths = true;
    bool allDashPhases = true;
    for (std::size_t dpiIndex = 0; dpiIndex < dpi.size(); ++dpiIndex)
    {
        const Grid grid{64, 64, scale[dpiIndex], 16.25, 32.25, dpi[dpiIndex]};
        Raster raster;
        if (!Prepare(source, grid, raster))
        {
            allWidths = false;
            allDashPhases = false;
            continue;
        }
        const auto solidColumn = PixelAt(grid.originPixelX + grid.documentScalePixel * 20.0);
        for (std::size_t row = 0; row < rowCoverage[dpiIndex].size(); ++row)
            allWidths = allWidths && NearByte(At(raster, 0, solidColumn, static_cast<std::uint32_t>(31 + row)),
                                              rowCoverage[dpiIndex][row]);

        // Plane p (1..8) uses offset k=p-1 DIP and a 4-on/4-off period. For each k,
        // s=10-k places the whole sampled pixel inside the on interval because (s+k) mod 8=2;
        // s+4 places it inside the off interval because (s+4+k) mod 8=6. Grid scale equals
        // DPI/96 here, so each target pixel spans at most one DIP and stays away from dash ends.
        for (std::size_t phase = 0; phase < 8; ++phase)
        {
            const double onDistance = 10.0 - static_cast<double>(phase);
            const auto onColumn = PixelAt(grid.originPixelX + grid.documentScalePixel * onDistance);
            const auto offColumn = PixelAt(grid.originPixelX + grid.documentScalePixel * (onDistance + 4.0));
            const auto plane = phase + 1;
            allDashPhases = allDashPhases && NearByte(At(raster, plane, onColumn, 32), rowCoverage[dpiIndex][1]) &&
                            At(raster, plane, offColumn, 32) <= 1;
        }
    }
    Expect(allWidths, "one-dip-width-has-literal-area-coverage-at-96-144-192-dpi");
    Expect(allDashPhases, "eight-independent-four-on-four-off-phases-at-96-144-192-dpi");
}

void TestZoomDoesNotScaleStrokeWidth()
{
    constexpr std::array<double, 3> zoom{0.5, 1.0, 2.0};
    constexpr std::array<int, 3> expected{128, 255, 0};
    const auto source = Rectangle();
    bool sameWidthAtAllZooms = true;
    for (const auto documentScale : zoom)
    {
        const Grid grid{128, 64, documentScale, 24.25, 32.25, 144.0f};
        Raster raster;
        if (!Prepare(source, grid, raster))
        {
            sameWidthAtAllZooms = false;
            continue;
        }
        const auto x = PixelAt(grid.originPixelX + grid.documentScalePixel * 20.0);
        for (std::size_t row = 0; row < expected.size(); ++row)
            sameWidthAtAllZooms =
                sameWidthAtAllZooms && NearByte(At(raster, 0, x, static_cast<std::uint32_t>(31 + row)), expected[row]);
    }
    Expect(sameWidthAtAllZooms, "144-dpi-one-dip-width-is-zoom-invariant-at-half-one-and-double-scale");
}

void TestClipAndTranslatedView()
{
    constexpr std::array<float, 3> dpi{96.0f, 144.0f, 192.0f};
    constexpr std::array<double, 3> scale{1.0, 1.5, 2.0};
    constexpr std::array<std::array<int, 2>, 3> clippedRows{{{{191, 0}}, {{255, 0}}, {{255, 64}}}};
    const auto source = Rectangle();
    bool clippedCorrectly = true;
    for (std::size_t i = 0; i < dpi.size(); ++i)
    {
        const Grid grid{64, 64, scale[i], 8.25, 0.25, dpi[i]};
        Raster raster;
        if (!Prepare(source, grid, raster))
        {
            clippedCorrectly = false;
            continue;
        }
        const auto x = PixelAt(grid.originPixelX + grid.documentScalePixel * 20.0);
        clippedCorrectly = clippedCorrectly && NearByte(At(raster, 0, x, 0), clippedRows[i][0]) &&
                           NearByte(At(raster, 0, x, 1), clippedRows[i][1]);
    }
    Expect(clippedCorrectly, "viewport-clips-negative-half-of-rational-top-edge-at-three-dpis");

    const Grid firstGrid{64, 64, 1.0, 8.25, 8.25, 96.0f};
    const Grid translatedGrid{64, 64, 1.0, 25.25, 19.25, 96.0f};
    Raster first;
    Raster translated;
    bool translatedCorrectly = Prepare(source, firstGrid, first) && Prepare(source, translatedGrid, translated);
    if (translatedCorrectly)
    {
        for (std::uint32_t dx = 0; dx < 2; ++dx)
        {
            translatedCorrectly = translatedCorrectly && At(first, 0, 28 + dx, 7) == At(translated, 0, 45 + dx, 18) &&
                                  At(first, 0, 28 + dx, 8) == At(translated, 0, 45 + dx, 19);
        }
        translatedCorrectly = translatedCorrectly && At(translated, 0, 28, 7) == 0 && At(translated, 0, 28, 8) == 0;
    }
    Expect(translatedCorrectly, "integer-view-translation-moves-the-same-literal-edge-and-clears-old-position");
}

void TestLargeDoubleAnchorAndContours()
{
    constexpr double anchorX = 1099511627776.0; // 2^40; local 0..40 deltas are exactly representable.
    constexpr double anchorY = -549755813888.0; // -2^39.
    const auto largeSource = Rectangle(anchorX, anchorY, anchorX + 40.0, anchorY + 32.0);
    const Grid grid{64, 64, 1.0, 16.25, 32.25, 96.0f};
    Raster raster;
    const auto largeAnchorPrepared = Prepare(largeSource, grid, raster);
    const auto x = PixelAt(grid.originPixelX + grid.documentScalePixel * 20.0);
    Expect(largeAnchorPrepared && raster.SourceIdentity() == largeSource.Identity() &&
               NearByte(At(raster, 0, x, 31), 64) && NearByte(At(raster, 0, x, 32), 191),
           "large-double-anchor-subtracts-before-float-with-literal-local-coverage");

    SelectionLoops contours{std::vector<SelectionLoop>{
        {{4.0, 4.0}, {60.0, 4.0}, {60.0, 60.0}, {4.0, 60.0}},
        {{20.0, 20.0}, {44.0, 20.0}, {44.0, 44.0}, {20.0, 44.0}},
    }};
    const Grid contourGrid{64, 64, 1.0, 8.25, 24.25, 96.0f};
    Raster contourRaster;
    const bool holesPrepared = Prepare(contours, contourGrid, contourRaster);
    const auto innerTopX = PixelAt(contourGrid.originPixelX + 24.0);
    const auto holeCenterX = PixelAt(contourGrid.originPixelX + 36.0);
    const auto innerTopY = PixelAt(contourGrid.originPixelY + 16.0);
    Expect(holesPrepared && NearByte(At(contourRaster, 0, innerTopX, innerTopY - 1), 64) &&
               NearByte(At(contourRaster, 0, innerTopX, innerTopY), 191) &&
               At(contourRaster, 0, holeCenterX, PixelAt(contourGrid.originPixelY + 28.0)) == 0,
           "closed-inner-hole-contour-is-outlined-while-hole-interior-stays-clear");
}

void TestEmptyInputAndKeyIdentity()
{
    const auto source = Rectangle();
    const Grid grid{64, 64, 1.0, 16.25, 32.25, 96.0f};
    Raster retained;
    if (!Prepare(source, grid, retained))
    {
        Expect(false, "key-fixture-raster-prepares");
        return;
    }
    const auto retainedIdentity = retained.SourceIdentity();
    const auto retainedKey = retained.Key();
    auto memory = SharedPublicationMemory::CreateForWork();
    const SelectionLoops empty{};
    const auto emptyResult = Raster::Prepare(empty, grid, std::move(memory), {}, {}, retained);
    Expect(emptyResult == E_INVALIDARG && retained.SourceIdentity() == retainedIdentity &&
               retained.Key() == retainedKey,
           "empty-contour-input-refuses-without-replacing-existing-raster");

    Raster sameSourceDifferentView;
    Grid changedView = grid;
    changedView.originPixelX += 1.0;
    changedView.dpi = 144.0f;
    const bool secondPrepared = Prepare(source, changedView, sameSourceDifferentView);
    Expect(secondPrepared && retained.SourceIdentity() == sameSourceDifferentView.SourceIdentity() &&
               retained.Key() != sameSourceDifferentView.Key(),
           "same-source-identity-with-changed-dpi-or-view-is-a-distinct-grid-key");

    const auto equalLookingSource = Rectangle();
    Raster equalLooking;
    const bool equalPrepared = Prepare(equalLookingSource, grid, equalLooking);
    Expect(equalPrepared && equalLookingSource.Get() == source.Get() &&
               equalLookingSource.Identity() != source.Identity() && equalLooking.Key() == retained.Key(),
           "equal-coordinate-contours-with-distinct-source-identity-do-not-match-cache-authority");
}
} // namespace

int RunSelectionOutlineRasterViewsTests()
{
    gPassed = 0;
    gFailed = 0;
    ComApartment apartment;
    Expect(apartment.Ready(), "runner-com-apartment-initialized");
    if (apartment.Ready())
    {
        TestDpiWidthAndDashReferences();
        TestZoomDoesNotScaleStrokeWidth();
        TestClipAndTranslatedView();
        TestLargeDoubleAnchorAndContours();
        TestEmptyInputAndKeyIdentity();
    }
    std::printf("%s selection-outline-raster-views.summary passed=%u failed=%u controls=%u\n",
                gFailed == 0 ? "PASS" : "FAIL", gPassed, gFailed, gPassed + gFailed);
    return static_cast<int>(gFailed);
}
