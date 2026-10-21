#include "Document.Tests.DistortRead.h"

#include "TransformEdit.h"

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

void Expect(bool condition, const char* name) noexcept
{
    std::printf("%s distort-private-read.%s\n", condition ? "PASS" : "FAIL", name);
    ++gChecks;
    gFailures += condition ? 0 : 1;
}

[[nodiscard]] constexpr std::array<Vec2, 4> IdentityCorners() noexcept
{
    return {Vec2{0, 0}, Vec2{2, 0}, Vec2{2, 2}, Vec2{0, 2}};
}

[[nodiscard]] LayerTransform NearestPlacement(int width, int height) noexcept
{
    LayerTransform transform;
    transform.size = Vec2{static_cast<double>(width), static_cast<double>(height)};
    transform.sampling = LayerSampling::Nearest;
    return transform;
}

[[nodiscard]] bool PixelsEqual(const Raster& image, int width, int height, RasterFormat format,
                               std::span<const std::uint8_t> expected) noexcept
{
    if (!image.HasStorage() || image.Width() != width || image.Height() != height || image.Format() != format ||
        image.ByteSize() != expected.size())
    {
        return false;
    }
    const std::uint8_t* actual = image.Data();
    if (actual == nullptr)
    {
        return false;
    }
    for (std::size_t index = 0; index < expected.size(); ++index)
    {
        if (actual[index] != expected[index])
        {
            return false;
        }
    }
    return true;
}

struct NestedSparseFixture
{
    // Keep leaf and sparse owners alive while the root's base and tile graph is sampled.
    Raster firstLeafPixel;
    Raster secondLeafPixel;
    Raster nestedSparse;
    Raster nestedSparseTileGrid;
    Raster outerOverridePixel;
    Raster source;
};

[[nodiscard]] NestedSparseFixture MakeNestedBgra()
{
    constexpr std::array<std::uint8_t, 4> firstBytes{11, 22, 33, 44};
    constexpr std::array<std::uint8_t, 4> secondBytes{55, 66, 77, 88};
    constexpr std::array<std::uint8_t, 4> overrideBytes{99, 111, 123, 135};
    Raster firstLeafPixel = Raster::Make(1, 1, RasterFormat::Bgra8888, firstBytes.data(), firstBytes.size());
    Raster secondLeafPixel = Raster::Make(1, 1, RasterFormat::Bgra8888, secondBytes.data(), secondBytes.size());
    std::vector<RasterTile> nestedTiles;
    nestedTiles.push_back(RasterTile{0, 0, firstLeafPixel});
    nestedTiles.push_back(RasterTile{1, 0, secondLeafPixel});
    // Sparse bases are forbidden. This nested raster owns leaf tiles over an empty base and has literal fill 5.
    Raster nestedSparse = Raster::MakeSparse(2, 2, RasterFormat::Bgra8888, Raster{}, 0, 0, std::move(nestedTiles), 5);
    // Preserve the nested sparse raster as a tile in a valid empty-base sparse grid.
    std::vector<RasterTile> nestedGridTiles;
    nestedGridTiles.push_back(RasterTile{0, 0, nestedSparse});
    Raster nestedSparseTileGrid =
        Raster::MakeSparse(2, 2, RasterFormat::Bgra8888, Raster{}, 0, 0, std::move(nestedGridTiles), 5);
    Raster outerOverridePixel = Raster::Make(1, 1, RasterFormat::Bgra8888, overrideBytes.data(), overrideBytes.size());
    // Replacing splits the nested sparse tile into disjoint retained windows around the bottom-left override.
    std::vector<RasterTile> overrides;
    overrides.push_back(RasterTile{0, 1, outerOverridePixel});
    Raster source = Raster::Replacing(nestedSparseTileGrid, 0, 0, std::move(overrides), 0, 0, 2, 2, 5);
    return {std::move(firstLeafPixel),       std::move(secondLeafPixel),    std::move(nestedSparse),
            std::move(nestedSparseTileGrid), std::move(outerOverridePixel), std::move(source)};
}

[[nodiscard]] NestedSparseFixture MakeNestedGray()
{
    constexpr std::array<std::uint8_t, 1> firstByte{17};
    constexpr std::array<std::uint8_t, 1> secondByte{29};
    constexpr std::array<std::uint8_t, 1> overrideByte{41};
    Raster firstLeafPixel = Raster::Make(1, 1, RasterFormat::Gray8, firstByte.data(), firstByte.size());
    Raster secondLeafPixel = Raster::Make(1, 1, RasterFormat::Gray8, secondByte.data(), secondByte.size());
    std::vector<RasterTile> nestedTiles;
    nestedTiles.push_back(RasterTile{0, 0, firstLeafPixel});
    nestedTiles.push_back(RasterTile{1, 0, secondLeafPixel});
    Raster nestedSparse = Raster::MakeSparse(2, 2, RasterFormat::Gray8, Raster{}, 0, 0, std::move(nestedTiles), 3);
    std::vector<RasterTile> nestedGridTiles;
    nestedGridTiles.push_back(RasterTile{0, 0, nestedSparse});
    Raster nestedSparseTileGrid =
        Raster::MakeSparse(2, 2, RasterFormat::Gray8, Raster{}, 0, 0, std::move(nestedGridTiles), 3);
    Raster outerOverridePixel = Raster::Make(1, 1, RasterFormat::Gray8, overrideByte.data(), overrideByte.size());
    std::vector<RasterTile> overrides;
    overrides.push_back(RasterTile{0, 1, outerOverridePixel});
    Raster source = Raster::Replacing(nestedSparseTileGrid, 0, 0, std::move(overrides), 0, 0, 2, 2, 3);
    return {std::move(firstLeafPixel),       std::move(secondLeafPixel),    std::move(nestedSparse),
            std::move(nestedSparseTileGrid), std::move(outerOverridePixel), std::move(source)};
}

[[nodiscard]] bool IsNestedSparseFixtureValid(const NestedSparseFixture& fixture, RasterFormat format,
                                              std::uint8_t fill) noexcept
{
    if (!fixture.firstLeafPixel.HasStorage() || fixture.firstLeafPixel.Format() != format ||
        fixture.firstLeafPixel.IsSparse() || !fixture.secondLeafPixel.HasStorage() ||
        fixture.secondLeafPixel.Format() != format || fixture.secondLeafPixel.IsSparse() ||
        !fixture.nestedSparse.HasStorage() || fixture.nestedSparse.Format() != format ||
        !fixture.nestedSparse.IsSparse() || fixture.nestedSparse.Base().HasStorage() ||
        fixture.nestedSparse.Fill() != fill || fixture.nestedSparse.Tiles().size() != 2 ||
        !fixture.nestedSparse.Tiles()[0].pixels.HasStorage() ||
        fixture.nestedSparse.Tiles()[0].pixels.Format() != format ||
        fixture.nestedSparse.Tiles()[0].pixels.IsSparse() ||
        fixture.nestedSparse.Tiles()[0].pixels != fixture.firstLeafPixel || fixture.nestedSparse.Tiles()[0].x != 0 ||
        fixture.nestedSparse.Tiles()[0].y != 0 || fixture.nestedSparse.Tiles()[0].Width() != 1 ||
        fixture.nestedSparse.Tiles()[0].Height() != 1 ||
        fixture.nestedSparse.Tiles()[1].pixels != fixture.secondLeafPixel ||
        fixture.nestedSparse.Tiles()[1].pixels.Format() != format || fixture.nestedSparse.Tiles()[1].x != 1 ||
        fixture.nestedSparse.Tiles()[1].y != 0 || fixture.nestedSparse.Tiles()[1].Width() != 1 ||
        fixture.nestedSparse.Tiles()[1].Height() != 1 || !fixture.nestedSparseTileGrid.HasStorage() ||
        !fixture.nestedSparseTileGrid.IsSparse() || fixture.nestedSparseTileGrid.Format() != format ||
        fixture.nestedSparseTileGrid.Base().HasStorage() || fixture.nestedSparseTileGrid.Fill() != fill ||
        fixture.nestedSparseTileGrid.Tiles().size() != 1 || fixture.nestedSparseTileGrid.Tiles()[0].x != 0 ||
        fixture.nestedSparseTileGrid.Tiles()[0].y != 0 || fixture.nestedSparseTileGrid.Tiles()[0].Width() != 2 ||
        fixture.nestedSparseTileGrid.Tiles()[0].Height() != 2 ||
        fixture.nestedSparseTileGrid.Tiles()[0].pixels != fixture.nestedSparse ||
        fixture.nestedSparseTileGrid.Tiles()[0].pixels.Format() != format ||
        !fixture.nestedSparseTileGrid.Tiles()[0].pixels.IsSparse() || !fixture.outerOverridePixel.HasStorage() ||
        fixture.outerOverridePixel.Format() != format || fixture.outerOverridePixel.IsSparse() ||
        !fixture.source.HasStorage() || fixture.source.Format() != format || !fixture.source.IsSparse() ||
        fixture.source.Base().HasStorage() || fixture.source.Fill() != fill)
    {
        return false;
    }
    bool retainedNestedSparseTile = false;
    bool retainedOuterOverride = false;
    for (const RasterTile& tile : fixture.source.Tiles())
    {
        retainedNestedSparseTile =
            retainedNestedSparseTile || (tile.pixels == fixture.nestedSparse && tile.pixels.IsSparse());
        retainedOuterOverride = retainedOuterOverride || (tile.pixels == fixture.outerOverridePixel && tile.x == 0 &&
                                                          tile.y == 1 && tile.Width() == 1 && tile.Height() == 1);
    }
    return retainedNestedSparseTile && retainedOuterOverride;
}

[[nodiscard]] bool SparseCachesCold(const NestedSparseFixture& fixture) noexcept
{
    return !fixture.source.IsMaterialized() && !fixture.nestedSparseTileGrid.IsMaterialized() &&
           !fixture.nestedSparse.IsMaterialized();
}

void LiteralContiguousSampling()
{
    constexpr std::array<std::uint8_t, 16> bgra{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
    const Raster color = Raster::Make(2, 2, RasterFormat::Bgra8888, bgra.data(), bgra.size());
    LayerTransform transform = NearestPlacement(2, 2);
    const auto identity = DistortWarp::Warp(color, transform, IdentityCorners(), false);
    constexpr std::array<std::uint8_t, 16> expected{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
    Expect(identity.has_value() && PixelsEqual(identity->image, 2, 2, RasterFormat::Bgra8888, expected) &&
               identity->transform.origin == Vec2{0, 0} && identity->transform.size == Vec2{2, 2},
           "bgra-nearest-identity-literal-bytes-and-2x2-placement");

    transform.flipX = true;
    const auto flippedX = DistortWarp::Warp(color, transform, IdentityCorners(), false);
    constexpr std::array<std::uint8_t, 16> expectedFlipX{5, 6, 7, 8, 1, 2, 3, 4, 13, 14, 15, 16, 9, 10, 11, 12};
    Expect(flippedX.has_value() && PixelsEqual(flippedX->image, 2, 2, RasterFormat::Bgra8888, expectedFlipX),
           "bgra-nearest-flip-x-literal-bytes");

    transform.flipX = false;
    transform.flipY = true;
    const auto flippedY = DistortWarp::Warp(color, transform, IdentityCorners(), false);
    constexpr std::array<std::uint8_t, 16> expectedFlipY{9, 10, 11, 12, 13, 14, 15, 16, 1, 2, 3, 4, 5, 6, 7, 8};
    Expect(flippedY.has_value() && PixelsEqual(flippedY->image, 2, 2, RasterFormat::Bgra8888, expectedFlipY),
           "bgra-nearest-flip-y-literal-bytes");

    constexpr std::array<std::uint8_t, 4> gray{17, 34, 51, 68};
    const Raster mask = Raster::Make(2, 2, RasterFormat::Gray8, gray.data(), gray.size());
    const auto warpedMask = DistortWarp::WarpMask(mask, NearestPlacement(2, 2), IdentityCorners(), 0.0);
    Expect(warpedMask.has_value() && PixelsEqual(warpedMask->image, 2, 2, RasterFormat::Gray8, gray),
           "gray8-nearest-mask-identity-literal-bytes");

    const std::array<Vec2, 4> offsetCorners{Vec2{-1.2, -2.3}, Vec2{1.1, -2.3}, Vec2{1.1, 1.2}, Vec2{-1.2, 1.2}};
    const auto offset = DistortWarp::Warp(color, NearestPlacement(2, 2), offsetCorners, false);
    Expect(offset.has_value() && offset->image.Width() == 4 && offset->image.Height() == 5 &&
               offset->transform.origin == Vec2{-2, -3} && offset->transform.size == Vec2{4, 5},
           "fractional-corners-floor-ceil-bounds-and-negative-placement-literal");
}

void NestedSparsePrivateReads()
{
    NestedSparseFixture color = MakeNestedBgra();
    const bool colorFixtureValid = IsNestedSparseFixtureValid(color, RasterFormat::Bgra8888, 5);
    const bool colorWasCold = SparseCachesCold(color);
    const auto warped = DistortWarp::Warp(color.source, NearestPlacement(2, 2), IdentityCorners(), false);
    constexpr std::array<std::uint8_t, 16> expectedBgra{11, 22, 33, 44, 55, 66, 77, 88, 99, 111, 123, 135, 5, 5, 5, 5};
    Expect(colorFixtureValid && colorWasCold && warped.has_value() &&
               PixelsEqual(warped->image, 2, 2, RasterFormat::Bgra8888, expectedBgra) && SparseCachesCold(color),
           "nested-bgra-base-and-overlapping-tiles-warp-exactly-with-original-caches-cold");

    NestedSparseFixture mask = MakeNestedGray();
    const bool maskFixtureValid = IsNestedSparseFixtureValid(mask, RasterFormat::Gray8, 3);
    const bool maskWasCold = SparseCachesCold(mask);
    const auto warpedMask = DistortWarp::WarpMask(mask.source, NearestPlacement(2, 2), IdentityCorners(), 0.0);
    constexpr std::array<std::uint8_t, 4> expectedGray{17, 29, 41, 3};
    Expect(maskFixtureValid && maskWasCold && warpedMask.has_value() &&
               PixelsEqual(warpedMask->image, 2, 2, RasterFormat::Gray8, expectedGray) && SparseCachesCold(mask),
           "nested-gray-base-and-overlapping-tiles-warp-mask-exactly-with-original-caches-cold");
}

void RefusalsAndUniformMask()
{
    NestedSparseFixture color = MakeNestedBgra();
    const bool colorWasColdBeforeFormatRefusal = SparseCachesCold(color);
    const auto colorAsMask = DistortWarp::Warp(color.source, NearestPlacement(2, 2), IdentityCorners(), true);
    Expect(IsNestedSparseFixtureValid(color, RasterFormat::Bgra8888, 5) && colorWasColdBeforeFormatRefusal &&
               !colorAsMask.has_value() && SparseCachesCold(color),
           "bgra-mask-format-refusal-keeps-original-caches-cold");

    NestedSparseFixture mask = MakeNestedGray();
    const bool maskWasColdBeforeFormatRefusal = SparseCachesCold(mask);
    const auto grayAsColor = DistortWarp::Warp(mask.source, NearestPlacement(2, 2), IdentityCorners(), false);
    Expect(IsNestedSparseFixtureValid(mask, RasterFormat::Gray8, 3) && maskWasColdBeforeFormatRefusal &&
               !grayAsColor.has_value() && SparseCachesCold(mask),
           "gray-color-format-refusal-keeps-original-caches-cold");

    const bool colorWasColdBeforeWarpMaskRefusal = SparseCachesCold(color);
    const auto maskFromColor = DistortWarp::WarpMask(color.source, NearestPlacement(2, 2), IdentityCorners(), 0.0);
    Expect(colorWasColdBeforeWarpMaskRefusal && !maskFromColor.has_value() && SparseCachesCold(color),
           "warp-mask-rejects-bgra-without-original-cache-assembly");

    const std::array<Vec2, 4> invalidCorners{Vec2{0, 0}, Vec2{2, 2}, Vec2{0, 2}, Vec2{2, 0}};
    const bool colorWasColdBeforeInvalid = SparseCachesCold(color);
    const bool maskWasColdBeforeInvalid = SparseCachesCold(mask);
    const auto invalidColor = DistortWarp::Warp(color.source, NearestPlacement(2, 2), invalidCorners, false);
    const auto invalidMask = DistortWarp::WarpMask(mask.source, NearestPlacement(2, 2), invalidCorners, 0.0);
    Expect(colorWasColdBeforeInvalid && maskWasColdBeforeInvalid && !invalidColor.has_value() &&
               !invalidMask.has_value() && SparseCachesCold(color) && SparseCachesCold(mask),
           "twisted-corners-refuse-color-and-mask-before-original-cache-assembly");

    constexpr std::array<std::uint8_t, 1> uniformByte{173};
    const Raster uniformPixel = Raster::Make(1, 1, RasterFormat::Gray8, uniformByte.data(), uniformByte.size());
    const Raster uniformMask = Raster::MakeSparse(1, 1, RasterFormat::Gray8, uniformPixel, 0, 0, {}, 0);
    std::array<std::uint8_t, 1> observedUniformByte{};
    uniformMask.ReadRegion(0, 0, 1, 1, observedUniformByte.data(), 1, 0);
    const auto shortcut = DistortWarp::WarpMask(uniformMask, NearestPlacement(1, 1), IdentityCorners(), 0.0);
    Expect(shortcut.has_value() && shortcut->image.Identity() == uniformMask.Identity() &&
               observedUniformByte[0] == 173 && uniformPixel.IsMaterialized() && !uniformMask.IsMaterialized(),
           "uniform-1x1-mask-returns-source-identity-byte-173-with-correct-cache-state");
}
} // namespace

int RunDistortReadTests()
{
    gFailures = gChecks = 0;
    LiteralContiguousSampling();
    NestedSparsePrivateReads();
    RefusalsAndUniformMask();
    std::printf("SUMMARY distort-private-read checks=%d failures=%d\n", gChecks, gFailures);
    return gFailures;
}