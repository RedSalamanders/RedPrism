#include "Document.Tests.SparseWindowBounds.h"

#include "Document.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

namespace
{
constexpr std::uint64_t kExpectedRasterTileBytes = 40;
static_assert(sizeof(RasterTile) == kExpectedRasterTileBytes);

int gSparseWindowFailures = 0;

void Expect(bool condition, const char* label) noexcept
{
    std::printf("%s sparse-window.%s\n", condition ? "PASS" : "FAIL", label);
    if (!condition)
        ++gSparseWindowFailures;
}

Raster SolidBgra(int width, int height, std::uint8_t blue, std::uint8_t green, std::uint8_t red, std::uint8_t alpha)
{
    const std::size_t pixelCount = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    std::vector<std::uint8_t> pixels(pixelCount * 4);
    for (std::size_t pixel = 0; pixel < pixelCount; ++pixel)
    {
        const std::size_t offset = pixel * 4;
        pixels[offset] = blue;
        pixels[offset + 1] = green;
        pixels[offset + 2] = red;
        pixels[offset + 3] = alpha;
    }
    return Raster::Make(width, height, RasterFormat::Bgra8888, pixels.data(), pixels.size());
}

std::array<std::uint8_t, 4> ReadPixel(const Raster& raster, int x, int y) noexcept
{
    std::array<std::uint8_t, 4> pixel{};
    raster.ReadRegion(x, y, 1, 1, pixel.data(), 4, 0);
    return pixel;
}

void ExpectBytes(const Raster& raster, std::uint64_t expected, const char* label)
{
    const std::optional<std::uint64_t> actual = raster.GridReplacementWorkspaceBytes();
    Expect(actual.has_value() && actual.value() == expected, label);
}

// Independent public-window traversal. Count 256-pixel spans by consuming each window's dimensions,
// then add the two boundary slots on each axis and eight possible pieces per cell. It does not call the cache API.
std::optional<std::uint64_t> PublicWindowReference(const Raster& raster) noexcept
{
    std::uint64_t total = 0;
    for (const RasterTile& tile : raster.Tiles())
    {
        if (tile.Width() < 1 || tile.Height() < 1)
            return std::nullopt;
        std::uint64_t columns = 2;
        for (int remaining = tile.Width(); remaining > 0; remaining -= (remaining < 256 ? remaining : 256))
            ++columns;
        std::uint64_t rows = 2;
        for (int remaining = tile.Height(); remaining > 0; remaining -= (remaining < 256 ? remaining : 256))
            ++rows;
        const std::uint64_t bytes = columns * rows * 8 * kExpectedRasterTileBytes;
        if (bytes > ((std::numeric_limits<std::uint64_t>::max)() - total))
            return std::nullopt;
        total += bytes;
    }
    return total;
}

void EmptyContiguousAndTilelessSparseHaveNoOldWindowWorkspace()
{
    const Raster empty{};
    ExpectBytes(empty, 0, "empty.zero");

    const Raster contiguous = SolidBgra(2, 3, 16, 32, 48, 64);
    Expect(contiguous.HasStorage() && !contiguous.IsSparse(), "contiguous.fixture");
    ExpectBytes(contiguous, 0, "contiguous.zero");

    const Raster tileless = Raster::MakeSparse(31, 17, RasterFormat::Bgra8888, {}, 0, 0, {}, 7);
    Expect(tileless.HasStorage() && tileless.IsSparse() && tileless.Tiles().empty(), "tileless-sparse.fixture");
    ExpectBytes(tileless, 0, "tileless-sparse.zero");
}

void SingleWindowBoundsUseTheFixed256Grid()
{
    const Raster backing = SolidBgra(513, 257, 12, 24, 36, 48);
    const Raster aligned =
        Raster::MakeSparse(1024, 768, RasterFormat::Bgra8888, {}, 0, 0, {RasterTile{0, 0, backing, 0, 0, 256, 256}}, 0);
    Expect(aligned.HasStorage() && aligned.IsSparse() && aligned.Tiles().size() == 1, "aligned.fixture");
    // (1 + 2) * (1 + 2) * 8 * 40 bytes.
    ExpectBytes(aligned, 2880, "aligned-256x256.literal-2880");

    const Raster offset = Raster::MakeSparse(1024, 768, RasterFormat::Bgra8888, {}, 0, 0,
                                             {RasterTile{73, 91, backing, 0, 0, 256, 256}}, 0);
    ExpectBytes(offset, 2880, "offset-equal-dimensions.literal-2880");

    const Raster large = Raster::MakeSparse(1024, 512, RasterFormat::Bgra8888, {}, 0, 0,
                                            {RasterTile{11, 29, backing, 0, 0, 513, 257}}, 0);
    ExpectBytes(large, 6400, "large-513x257.literal-6400");
}

void ClippedWindowsKeepPixelsAndAdjustedSourceOrigin()
{
    const Raster backing = SolidBgra(320, 280, 11, 22, 33, 44);
    const void* const backingIdentity = backing.Identity();
    const Raster clipped =
        Raster::MakeSparse(400, 400, RasterFormat::Bgra8888, {}, 0, 0, {RasterTile{-32, -16, backing}}, 0);
    const auto& tiles = clipped.Tiles();
    Expect(clipped.HasStorage() && clipped.IsSparse() && tiles.size() == 1, "clipped.fixture");

    bool clippedWindowMatches = false;
    if (tiles.size() == 1)
    {
        const RasterTile& tile = tiles.front();
        clippedWindowMatches = tile.pixels.Identity() == backingIdentity && tile.x == 0 && tile.y == 0 &&
                               tile.Width() == 288 && tile.Height() == 264;
    }
    Expect(clippedWindowMatches, "clipped.kept-window-and-owner");

    bool sourceOriginMatches = false;
    if (tiles.size() == 1)
    {
        const RasterTile& tile = tiles.front();
        sourceOriginMatches = tile.sourceX == 32 && tile.sourceY == 16;
    }
    Expect(sourceOriginMatches, "clipped.source-origin-advanced");
    // (ceil(288 / 256) + 2) * (ceil(264 / 256) + 2) * 8 * 40 bytes.
    ExpectBytes(clipped, 5120, "clipped-288x264.literal-5120");

    constexpr std::array<std::uint8_t, 4> kClippedExpected{11, 22, 33, 44};
    Expect(ReadPixel(clipped, 0, 0) == kClippedExpected, "clipped.pixel-at-kept-origin");
    Expect(ReadPixel(clipped, 287, 263) == kClippedExpected, "clipped.pixel-at-kept-far-edge");
    Expect(clipped.IsSparse() && !clipped.IsMaterialized(), "clipped.readregion-keeps-sparse-unmaterialized");
}

void MultipleAndIrregularWindowsUsePublicTraversal()
{
    const Raster firstPixels = SolidBgra(256, 256, 17, 34, 51, 68);
    const Raster largePixels = SolidBgra(513, 257, 18, 36, 54, 72);
    const Raster multiple = Raster::MakeSparse(2048, 1024, RasterFormat::Bgra8888, {}, 0, 0,
                                               {RasterTile{13, 17, firstPixels}, RasterTile{700, 300, largePixels}}, 0);
    ExpectBytes(multiple, 9280, "multiple-windows.literal-sum-9280");

    const Raster arena = SolidBgra(2048, 2048, 19, 38, 57, 76);
    const std::array<RasterTile, 6> generated{{
        RasterTile{3, 9, arena, 1, 2, 1, 1},
        RasterTile{251, 255, arena, 20, 30, 255, 256},
        RasterTile{511, 101, arena, 300, 400, 257, 511},
        RasterTile{800, 700, arena, 800, 900, 768, 17},
        RasterTile{1021, 1000, arena, 1000, 1100, 513, 257},
        RasterTile{1400, 1200, arena, 1200, 1300, 400, 300},
    }};
    const Raster irregular = Raster::MakeSparse(2048, 2048, RasterFormat::Bgra8888, {}, 0, 0,
                                                std::vector<RasterTile>(generated.begin(), generated.end()), 0);
    const std::optional<std::uint64_t> reference = PublicWindowReference(irregular);
    const std::optional<std::uint64_t> cached = irregular.GridReplacementWorkspaceBytes();
    Expect(irregular.HasStorage() && irregular.IsSparse() && irregular.Tiles().size() == 6,
           "irregular-generated.fixture");
    Expect(reference.has_value() && cached.has_value() && reference.value() == cached.value(),
           "irregular-generated.public-traversal-matches-constructor-cache");
}

void ArbitraryRectangleReplacementChecksResultTopologyAndCachedAccounting()
{
    const Raster leftPixels = SolidBgra(256, 256, 21, 42, 63, 84);
    const Raster middlePixels = SolidBgra(256, 256, 22, 44, 66, 88);
    const Raster rightPixels = SolidBgra(256, 256, 23, 46, 69, 92);
    const Raster partialPixels = SolidBgra(128, 256, 24, 48, 72, 96);
    const Raster fullPixels = SolidBgra(256, 256, 25, 50, 75, 100);
    const Raster source = Raster::MakeSparse(
        1024, 512, RasterFormat::Bgra8888, {}, 0, 0,
        {RasterTile{0, 0, leftPixels}, RasterTile{256, 0, middlePixels}, RasterTile{512, 0, rightPixels}}, 0);
    Expect(source.HasStorage() && source.IsSparse() && source.Tiles().size() == 3, "replace-source.fixture");
    ExpectBytes(source, 8640, "replace-source.literal-sum-8640");

    std::vector<RasterTile> replacements;
    replacements.push_back(RasterTile{64, 0, partialPixels});
    replacements.push_back(RasterTile{256, 0, fullPixels});
    const Raster result = Raster::Replacing(source, 0, 0, std::move(replacements), 0, 0, 1024, 512, 0);
    const auto& resultTiles = result.Tiles();
    Expect(result.HasStorage() && result.IsSparse(), "replace-result.fixture");
    Expect(resultTiles.size() == 5, "replace-result.five-windows-after-remove-and-split");
    ExpectBytes(result, 14400, "replace-result.constructor-cache-literal-14400");

    bool foundLeft = false;
    bool foundRight = false;
    bool foundUnchanged = false;
    bool foundPartial = false;
    bool foundFull = false;
    for (const RasterTile& tile : resultTiles)
    {
        foundLeft |= tile.x == 0 && tile.y == 0 && tile.Width() == 64 && tile.Height() == 256 &&
                     tile.pixels.Identity() == leftPixels.Identity() && tile.sourceX == 0;
        foundRight |= tile.x == 192 && tile.y == 0 && tile.Width() == 64 && tile.Height() == 256 &&
                      tile.pixels.Identity() == leftPixels.Identity() && tile.sourceX == 192;
        foundUnchanged |= tile.x == 512 && tile.y == 0 && tile.Width() == 256 && tile.Height() == 256 &&
                          tile.pixels.Identity() == rightPixels.Identity();
        foundPartial |= tile.x == 64 && tile.y == 0 && tile.Width() == 128 && tile.Height() == 256 &&
                        tile.pixels.Identity() == partialPixels.Identity();
        foundFull |= tile.x == 256 && tile.y == 0 && tile.Width() == 256 && tile.Height() == 256 &&
                     tile.pixels.Identity() == fullPixels.Identity();
    }
    Expect(foundLeft && foundRight && foundUnchanged && foundPartial && foundFull,
           "replace-result.identities-and-window-topology-retained");

    const std::optional<std::uint64_t> reference = PublicWindowReference(result);
    const std::optional<std::uint64_t> cached = result.GridReplacementWorkspaceBytes();
    Expect(reference.has_value() && cached.has_value() && reference.value() == cached.value(),
           "replace-result.public-traversal-matches-postconstruction-cache");

    constexpr std::array<std::uint8_t, 4> kLeftExpected{21, 42, 63, 84};
    constexpr std::array<std::uint8_t, 4> kPartialExpected{24, 48, 72, 96};
    constexpr std::array<std::uint8_t, 4> kFullExpected{25, 50, 75, 100};
    constexpr std::array<std::uint8_t, 4> kRightExpected{23, 46, 69, 92};
    Expect(ReadPixel(result, 0, 7) == kLeftExpected, "replace-result.left-split-pixel-from-retained-owner");
    Expect(ReadPixel(result, 192, 7) == kLeftExpected, "replace-result.right-split-pixel-from-retained-owner");
    Expect(ReadPixel(result, 64, 7) == kPartialExpected, "replace-result.partial-replacement-pixel");
    Expect(ReadPixel(result, 256, 7) == kFullExpected, "replace-result.full-replacement-pixel");
    Expect(ReadPixel(result, 512, 7) == kRightExpected, "replace-result.unchanged-retained-pixel");
    Expect(result.IsSparse() && !result.IsMaterialized(), "replace-result.readregion-keeps-sparse-unmaterialized");
}

void Fixed256GridReplacingChecksActualGridTopologyAndAccounting()
{
    const Raster originalPixels = SolidBgra(768, 768, 31, 62, 93, 124);
    const Raster replacementPixels = SolidBgra(256, 256, 32, 64, 96, 128);
    const Raster source =
        Raster::MakeSparse(768, 768, RasterFormat::Bgra8888, {}, 0, 0, {RasterTile{0, 0, originalPixels}}, 0);
    Expect(source.HasStorage() && source.IsSparse() && source.Tiles().size() == 1, "grid-replace-source.fixture");
    ExpectBytes(source, 8000, "grid-replace-source-768x768.literal-8000");

    std::vector<RasterTile> replacement;
    replacement.push_back(RasterTile{256, 256, replacementPixels});
    const Raster result = Raster::Replacing(source, 0, 0, std::move(replacement), 0, 0, 768, 768, 0);
    Expect(result.HasStorage() && result.IsSparse(), "grid-replace-result.fixture");
    Expect(result.Tiles().size() == 5, "grid-replace-result.four-retained-pieces-plus-fresh-grid-cell");
    ExpectBytes(result, 18240, "grid-replace-result.constructor-cache-literal-18240");

    const std::optional<std::uint64_t> reference = PublicWindowReference(result);
    const std::optional<std::uint64_t> cached = result.GridReplacementWorkspaceBytes();
    Expect(reference.has_value() && cached.has_value() && reference.value() == cached.value(),
           "grid-replace-result.public-traversal-matches-postconstruction-cache");

    constexpr std::array<std::uint8_t, 4> kOriginalExpected{31, 62, 93, 124};
    constexpr std::array<std::uint8_t, 4> kReplacementExpected{32, 64, 96, 128};
    Expect(ReadPixel(result, 7, 7) == kOriginalExpected, "grid-replace-result.top-retained-pixel");
    Expect(ReadPixel(result, 7, 300) == kOriginalExpected, "grid-replace-result.left-retained-pixel");
    Expect(ReadPixel(result, 600, 300) == kOriginalExpected, "grid-replace-result.right-retained-pixel");
    Expect(ReadPixel(result, 300, 300) == kReplacementExpected, "grid-replace-result.fresh-grid-cell-pixel");
    Expect(ReadPixel(result, 7, 600) == kOriginalExpected, "grid-replace-result.bottom-retained-pixel");
    Expect(result.IsSparse() && !result.IsMaterialized(), "grid-replace-result.readregion-keeps-sparse-unmaterialized");
}
} // namespace

int RunSparseWindowBoundsTests()
{
    gSparseWindowFailures = 0;
    EmptyContiguousAndTilelessSparseHaveNoOldWindowWorkspace();
    SingleWindowBoundsUseTheFixed256Grid();
    ClippedWindowsKeepPixelsAndAdjustedSourceOrigin();
    MultipleAndIrregularWindowsUsePublicTraversal();
    ArbitraryRectangleReplacementChecksResultTopologyAndCachedAccounting();
    Fixed256GridReplacingChecksActualGridTopologyAndAccounting();
    return gSparseWindowFailures;
}
