#pragma once

#include "DocumentFixture.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>

// Deterministic test input for large pinned-Tree cost and UIA scenarios. This fixture is not performance proof:
// callers construct and seal it before timed owner measurements, then independently qualify the measured path.
struct PreparedLayerRowsStressFixture final
{
    struct LayerReference final
    {
        Guid id;
        std::size_t storageIndex = 0;
        std::size_t topFirstRow = 0;
    };

    static constexpr std::size_t kLayerCount = 10'000;
    static constexpr std::size_t kNameUnits = 1'024;
    static constexpr std::size_t kFirstStorageIndex = 0;
    static constexpr std::size_t kMiddleStorageIndex = 5'000;
    static constexpr std::size_t kLastStorageIndex = 9'999;
    static constexpr std::size_t kFirstTopFirstRow = 9'999;
    static constexpr std::size_t kMiddleTopFirstRow = 4'999;
    static constexpr std::size_t kLastTopFirstRow = 0;

    inline static constexpr Guid kDocumentId{{0x76, 0, 0, 0, 0, 0x40, 0, 0x80, 0, 0, 0, 0, 0, 0, 0, 1}};
    inline static constexpr Guid kFirstLayerId{{0x77, 0, 0, 0, 0, 0x40, 0, 0x80, 0, 0, 0, 0, 0, 0, 0x00, 0x00}};
    inline static constexpr Guid kMiddleLayerId{{0x77, 0, 0, 0, 0, 0x40, 0, 0x80, 0, 0, 0, 0, 0, 0, 0x13, 0x88}};
    inline static constexpr Guid kLastLayerId{{0x77, 0, 0, 0, 0, 0x40, 0, 0x80, 0, 0, 0, 0, 0, 0, 0x27, 0x0f}};

    inline static constexpr std::array<std::uint8_t, 8> kFirstExpectedBgra{0x10, 0x20, 0x30, 0xff,
                                                                           0x40, 0x50, 0x60, 0xff};
    inline static constexpr std::array<std::uint8_t, 8> kLastExpectedBgra{0x70, 0x80, 0x90, 0xff,
                                                                          0xa0, 0xb0, 0xc0, 0xff};
    inline static constexpr std::array<std::uint8_t, 2> kFirstExpectedMask{0x3f, 0xd1};

    DocumentFixture builder;
    LayerReference first{kFirstLayerId, kFirstStorageIndex, kFirstTopFirstRow};
    LayerReference middle{kMiddleLayerId, kMiddleStorageIndex, kMiddleTopFirstRow};
    LayerReference last{kLastLayerId, kLastStorageIndex, kLastTopFirstRow};
};

namespace PreparedLayerRowsStressFixtureDetail
{
[[nodiscard]] constexpr Guid LayerId(std::size_t storageIndex) noexcept
{
    Guid id{{0x77, 0, 0, 0, 0, 0x40, 0, 0x80, 0, 0, 0, 0, 0, 0, 0, 0}};
    id.bytes[14] = static_cast<std::uint8_t>(storageIndex >> 8);
    id.bytes[15] = static_cast<std::uint8_t>(storageIndex & 0xff);
    return id;
}

static_assert(LayerId(PreparedLayerRowsStressFixture::kFirstStorageIndex) ==
              PreparedLayerRowsStressFixture::kFirstLayerId);
static_assert(LayerId(PreparedLayerRowsStressFixture::kMiddleStorageIndex) ==
              PreparedLayerRowsStressFixture::kMiddleLayerId);
static_assert(LayerId(PreparedLayerRowsStressFixture::kLastStorageIndex) ==
              PreparedLayerRowsStressFixture::kLastLayerId);

[[nodiscard]] inline std::wstring LayerName(std::size_t storageIndex)
{
    constexpr std::size_t ordinalDigits = 5;
    constexpr std::size_t suffixUnits = ordinalDigits + 1;
    std::wstring name(PreparedLayerRowsStressFixture::kNameUnits, L'N');
    name[name.size() - suffixUnits] = L'#';
    for (std::size_t digit = 0; digit < ordinalDigits; ++digit)
    {
        name[name.size() - 1 - digit] = static_cast<wchar_t>(L'0' + storageIndex % 10);
        storageIndex /= 10;
    }
    return name; // The independent fixed-width suffix is #00000 through #09999 in storage order.
}
} // namespace PreparedLayerRowsStressFixtureDetail

[[nodiscard]] inline PreparedLayerRowsStressFixture MakePreparedLayerRowsStressFixture()
{
    PreparedLayerRowsStressFixture result;
    result.builder.id = PreparedLayerRowsStressFixture::kDocumentId;
    result.builder.width = 2;
    result.builder.height = 1;
    result.builder.layers.reserve(PreparedLayerRowsStressFixture::kLayerCount);
    for (std::size_t index = 0; index < PreparedLayerRowsStressFixture::kLayerCount; ++index)
    {
        ImageLayer layer;
        layer.id = PreparedLayerRowsStressFixtureDetail::LayerId(index);
        layer.transform.size = Vec2{2, 1};
        layer.name = PreparedLayerRowsStressFixtureDetail::LayerName(index);
        result.builder.layers.push_back(std::move(layer));
    }

    const Raster firstImage =
        Raster::Make(2, 1, RasterFormat::Bgra8888, PreparedLayerRowsStressFixture::kFirstExpectedBgra.data(),
                     PreparedLayerRowsStressFixture::kFirstExpectedBgra.size());
    const Raster lastImage =
        Raster::Make(2, 1, RasterFormat::Bgra8888, PreparedLayerRowsStressFixture::kLastExpectedBgra.data(),
                     PreparedLayerRowsStressFixture::kLastExpectedBgra.size());
    const Raster mask =
        Raster::Make(2, 1, RasterFormat::Gray8, PreparedLayerRowsStressFixture::kFirstExpectedMask.data(),
                     PreparedLayerRowsStressFixture::kFirstExpectedMask.size());
    result.builder.layers.front().asset = RasterAsset{firstImage, {}, L"Literal first storage pixels"};
    result.builder.layers.front().mask =
        LayerMask{RasterAsset{mask, Raster::MakeThumbnail(mask), L"Literal semantic mask"}, true, std::nullopt, true};
    result.builder.layers.back().asset = RasterAsset{lastImage, {}, L"Literal last storage pixels"};
    return result;
}
