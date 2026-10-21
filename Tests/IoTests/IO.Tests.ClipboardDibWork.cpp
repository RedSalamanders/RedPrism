#include "IO.Tests.ClipboardDibWork.h"

#include "ClipboardDibWork.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <span>
#include <stop_token>
#include <vector>

namespace
{
int gFailures = 0;

void Expect(bool condition, const char* name) noexcept
{
    std::printf("%s clipboarddib.%s\n", condition ? "PASS" : "FAIL", name);
    gFailures += condition ? 0 : 1;
}

template <std::size_t Size> std::vector<std::uint8_t> Bytes(const std::array<std::uint8_t, Size>& source)
{
    return {source.begin(), source.end()};
}

[[nodiscard]] bool Equals(const Raster& image, int width, int height, std::span<const std::uint8_t> expected) noexcept
{
    if (!image.HasStorage() || image.Width() != width || image.Height() != height ||
        image.Format() != RasterFormat::Bgra8888 || expected.size() != static_cast<std::size_t>(width) * height * 4u)
    {
        return false;
    }
    const std::uint8_t* pixels = image.Data();
    return pixels && std::equal(expected.begin(), expected.end(), pixels);
}

constexpr std::array<std::uint8_t, 56> kRgb24BottomUp{40, 0,  0, 0, 2, 0, 0, 0, 2, 0, 0, 0, 1, 0,  24, 0,  0, 0, 0,
                                                      0,  16, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  0,  0,  0, 0, 0,
                                                      0,  0,  1, 2, 3, 4, 5, 6, 0, 0, 7, 8, 9, 10, 11, 12, 0, 0};
constexpr std::array<std::uint8_t, 48> kRgb24TopDown{40, 0, 0, 0, 1, 0, 0, 0, 254, 255, 255, 255, 1,  0,  24, 0,
                                                     0,  0, 0, 0, 8, 0, 0, 0, 0,   0,   0,   0,   0,  0,  0,  0,
                                                     0,  0, 0, 0, 0, 0, 0, 0, 9,   8,   7,   0,   12, 11, 10, 0};
constexpr std::array<std::uint8_t, 44> kRgb32AlphaIgnored{40, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0,  0,  1,  0, 32,
                                                          0,  0, 0, 0, 0, 4, 0, 0, 0, 0, 0,  0,  0,  0, 0,
                                                          0,  0, 0, 0, 0, 0, 0, 0, 0, 0, 10, 20, 30, 7};
constexpr std::array<std::uint8_t, 108> kV4AlphaBitfields{
    108, 0,   0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 32, 0,   3, 0, 0,   0, 4, 0,   0, 0, 0, 0, 0,
    0,   0,   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  255, 0, 0, 255, 0, 0, 255, 0, 0, 0, 0, 0,
    0,   255, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  0,   0, 0, 0,   0, 0, 0,   0, 0, 0, 0, 0,
    0,   0,   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  0,   0, 0, 0,   0, 0, 0,   0, 0, 0, 0, 0};
constexpr std::array<std::uint8_t, 48> kRgbPaletteOffset{40, 0, 0, 0, 1, 0, 0, 0, 1,  0,  0,  0,  1, 0, 24, 0,
                                                         0,  0, 0, 0, 4, 0, 0, 0, 0,  0,  0,  0,  0, 0, 0,  0,
                                                         1,  0, 0, 0, 0, 0, 0, 0, 90, 91, 92, 93, 6, 7, 8,  0};
constexpr std::array<std::uint8_t, 56> kBitfields40ByteHeader{
    40, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 32,  0, 3, 0,   0, 0, 4,   0, 0, 0, 0, 0, 0, 0,
    0,  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 255, 0, 0, 255, 0, 0, 255, 0, 0, 0, 3, 4, 0, 0};
constexpr std::array<std::uint8_t, 56> kRgb24Bitfields40ByteHeader{
    40, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 24, 0, 3, 0, 0, 0, 4, 0,  0,  0,  0, 0, 0, 0,
    0,  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 3,  4, 5, 6, 7, 8, 9, 10, 11, 12, 6, 7, 8, 0};

constexpr std::array<std::uint8_t, 16> kExpectedRgb24BottomUp{7, 8, 9, 255, 10, 11, 12, 255,
                                                              1, 2, 3, 255, 4,  5,  6,  255};
constexpr std::array<std::uint8_t, 8> kExpectedRgb24TopDown{9, 8, 7, 255, 12, 11, 10, 255};
constexpr std::array<std::uint8_t, 4> kExpectedRgb32{10, 20, 30, 255};
constexpr std::array<std::uint8_t, 4> kExpectedV4Alpha{100, 50, 25, 128};
constexpr std::array<std::uint8_t, 4> kExpectedPalette{6, 7, 8, 255};
constexpr std::array<std::uint8_t, 4> kExpectedBitfields40{3, 4, 0, 255};

void DecodeLiteral(const std::vector<std::uint8_t>& dib, int width, int height, std::span<const std::uint8_t> expected,
                   const char* name)
{
    Raster image;
    ClipboardDibMetadata metadata;
    const ClipboardDibStatus probe = ProbeClipboardDib(dib, 100, metadata);
    const ClipboardDibStatus decoded = DecodeClipboardDib(dib, 100, image);
    Expect(probe == ClipboardDibStatus::Ok && metadata.width == width && metadata.height == height &&
               metadata.pixels == static_cast<std::uint64_t>(width) * height,
           name);
    Expect(decoded == ClipboardDibStatus::Ok && Equals(image, width, height, expected), name);
}

[[nodiscard]] std::array<std::uint8_t, 124> ExpectedV5Header(int width, int height, std::uint32_t imageBytes) noexcept
{
    // Literal V5 header layout: 124-byte header, positive bottom-up dimensions, BGRA bitfields, sRGB, images intent.
    std::array<std::uint8_t, 124> header{};
    header[0] = 124;
    header[4] = static_cast<std::uint8_t>(width);
    header[8] = static_cast<std::uint8_t>(height);
    header[12] = 1;
    header[14] = 32;
    header[16] = 3;
    header[20] = static_cast<std::uint8_t>(imageBytes);
    header[40] = 0;
    header[41] = 0;
    header[42] = 255;
    header[44] = 0;
    header[45] = 255;
    header[48] = 255;
    header[55] = 255;
    header[56] = 66;
    header[57] = 71;
    header[58] = 82;
    header[59] = 115;
    header[108] = 4;
    return header;
}
} // namespace

int RunClipboardDibWorkTests()
{
    gFailures = 0;
    DecodeLiteral(Bytes(kRgb24BottomUp), 2, 2, kExpectedRgb24BottomUp, "rgb24.bottom-up-padding");
    DecodeLiteral(Bytes(kRgb24TopDown), 1, 2, kExpectedRgb24TopDown, "rgb24.top-down");
    DecodeLiteral(Bytes(kRgb32AlphaIgnored), 1, 1, kExpectedRgb32, "rgb32.bi-rgb-alpha-ignored");
    auto v4 = Bytes(kV4AlphaBitfields);
    constexpr std::array<std::uint8_t, 4> v4Pixel{200, 100, 50, 128};
    v4.insert(v4.end(), v4Pixel.begin(), v4Pixel.end());
    DecodeLiteral(v4, 1, 1, kExpectedV4Alpha, "rgb32.v4-alpha-premultiply");
    auto v4Rgb = Bytes(kV4AlphaBitfields);
    v4Rgb[16] = 0;
    v4Rgb[17] = 0;
    v4Rgb.insert(v4Rgb.end(), v4Pixel.begin(), v4Pixel.end());
    DecodeLiteral(v4Rgb, 1, 1, kExpectedV4Alpha, "rgb32.bi-rgb-v4-alpha-mask-honored");
    DecodeLiteral(Bytes(kRgbPaletteOffset), 1, 1, kExpectedPalette, "rgb24.bi-rgb-palette-offset");
    DecodeLiteral(Bytes(kBitfields40ByteHeader), 1, 1, kExpectedBitfields40, "rgb32.bi-bitfields-40-byte-header");
    DecodeLiteral(Bytes(kRgb24Bitfields40ByteHeader), 1, 1, kExpectedPalette, "rgb24.bi-bitfields-40-byte-header");

    {
        const auto v5 = ExpectedV5Header(1, 1, 4);
        Raster image;
        constexpr std::array<std::uint8_t, 4> expected{64, 32, 16, 128};
        std::vector<std::uint8_t> dib{v5.begin(), v5.end()};
        constexpr std::array<std::uint8_t, 4> v5Pixel{128, 64, 32, 128};
        dib.insert(dib.end(), v5Pixel.begin(), v5Pixel.end());
        Expect(DecodeClipboardDib(dib, 1, image) == ClipboardDibStatus::Ok && Equals(image, 1, 1, expected),
               "rgb32.v5-alpha-premultiply");
    }

    {
        const auto dib = Bytes(kRgb24BottomUp);
        ClipboardDibMetadata metadata;
        Raster image;
        Expect(ProbeClipboardDib(dib, 4, metadata) == ClipboardDibStatus::Ok && metadata.pixels == 4, "budget.exact");
        Expect(ProbeClipboardDib(dib, 3, metadata) == ClipboardDibStatus::TooLarge && metadata.width == 0 &&
                   metadata.height == 0 && metadata.pixels == 0,
               "budget.one-below-clears-metadata");
        Expect(DecodeClipboardDib(dib, 3, image) == ClipboardDibStatus::TooLarge && !image.HasStorage(),
               "budget.decode-clears-raster");
        metadata = {7, 8, 56};
        image = Raster::Make(1, 1, RasterFormat::Bgra8888);
        Expect(ProbeClipboardDib(dib, -1, metadata) == ClipboardDibStatus::TooLarge && metadata.width == 0 &&
                   metadata.height == 0 && metadata.pixels == 0,
               "budget.negative-clears-metadata");
        Expect(DecodeClipboardDib(dib, -1, image) == ClipboardDibStatus::TooLarge && !image.HasStorage(),
               "budget.negative-clears-preloaded-raster");
    }

    {
        auto malformed = Bytes(kRgb24BottomUp);
        malformed.resize(54);
        ClipboardDibMetadata metadata{1, 2, 3};
        Raster image = Raster::Make(1, 1, RasterFormat::Bgra8888);
        Expect(ProbeClipboardDib(malformed, 4, metadata) == ClipboardDibStatus::Invalid && metadata.width == 0 &&
                   metadata.height == 0 && metadata.pixels == 0,
               "malformed.truncated-rows-clears-metadata");
        Expect(DecodeClipboardDib(malformed, 4, image) == ClipboardDibStatus::Invalid && !image.HasStorage(),
               "malformed.decode-clears-raster");
    }

    {
        auto wrongMasks = Bytes(kBitfields40ByteHeader);
        wrongMasks[40] = 1;
        ClipboardDibMetadata metadata;
        Expect(ProbeClipboardDib(wrongMasks, 1, metadata) == ClipboardDibStatus::Invalid && metadata.pixels == 0,
               "masks.reject-non-bgra-32-bitfields");
        auto incompleteMasks = Bytes(kBitfields40ByteHeader);
        incompleteMasks.resize(48);
        Expect(ProbeClipboardDib(incompleteMasks, 1, metadata) == ClipboardDibStatus::Invalid,
               "masks.reject-truncated-40-byte-mask-table");
    }

    {
        const auto base = Bytes(kRgb24BottomUp);
        ClipboardDibMetadata metadata;
        auto planes = base;
        planes[12] = 2;
        Expect(ProbeClipboardDib(planes, 4, metadata) == ClipboardDibStatus::Invalid, "malformed.planes");
        auto bitDepth = base;
        bitDepth[14] = 16;
        Expect(ProbeClipboardDib(bitDepth, 4, metadata) == ClipboardDibStatus::Invalid, "malformed.bit-depth");
        auto compression = base;
        compression[16] = 1;
        Expect(ProbeClipboardDib(compression, 4, metadata) == ClipboardDibStatus::Invalid, "malformed.compression");
        auto headerOffset = base;
        headerOffset[0] = 0xFF;
        headerOffset[1] = 0xFF;
        Expect(ProbeClipboardDib(headerOffset, 4, metadata) == ClipboardDibStatus::Invalid,
               "malformed.header-past-input");
        auto paletteBounds = Bytes(kRgbPaletteOffset);
        paletteBounds[32] = 0xFF;
        paletteBounds[33] = 0xFF;
        paletteBounds[34] = 0xFF;
        paletteBounds[35] = 0xFF;
        Expect(ProbeClipboardDib(paletteBounds, 1, metadata) == ClipboardDibStatus::Invalid,
               "malformed.palette-offset-overrun");
    }

    {
        auto extreme = Bytes(kRgb24BottomUp);
        extreme[8] = 0;
        extreme[9] = 0;
        extreme[10] = 0;
        extreme[11] = 128; // INT32_MIN height; negation must be done after widening.
        ClipboardDibMetadata metadata;
        Expect(ProbeClipboardDib(extreme, (std::numeric_limits<std::int64_t>::max)(), metadata) ==
                       ClipboardDibStatus::TooLarge &&
                   metadata.width == 0,
               "dimensions.int32-min-height-safe-too-large");
        extreme[8] = 0xFF;
        extreme[9] = 0xFF;
        extreme[10] = 0xFF;
        extreme[11] = 0x7F; // INT32_MAX height.
        Expect(ProbeClipboardDib(extreme, (std::numeric_limits<std::int64_t>::max)(), metadata) ==
                   ClipboardDibStatus::TooLarge,
               "dimensions.int32-max-height-too-large");
    }

    {
        // A structurally valid 10,001 × 10,000 header exceeds the independent 100M-pixel hard cap.
        constexpr std::array<std::uint8_t, 40> oversizedHeader{40, 0, 0, 0, 17, 39, 0, 0, 16, 39, 0, 0, 1, 0,
                                                               24, 0, 0, 0, 0,  0,  0, 0, 0,  0,  0, 0, 0, 0,
                                                               0,  0, 0, 0, 0,  0,  0, 0, 0,  0,  0, 0};
        ClipboardDibMetadata metadata;
        Expect(ProbeClipboardDib(oversizedHeader, (std::numeric_limits<std::int64_t>::max)(), metadata) ==
                       ClipboardDibStatus::TooLarge &&
                   metadata.pixels == 0,
               "limits.independent-100m-pixel-cap");
        const std::array<std::uint8_t, 1> oneByte{0};
        Expect(ProbeClipboardDib(oneByte, 1, metadata) == ClipboardDibStatus::Invalid,
               "limits.short-input-does-not-read-past-span");
        Expect(ClipboardDibEncodedSizeAllowed(512ull * 1024ull * 1024ull) &&
                   !ClipboardDibEncodedSizeAllowed(512ull * 1024ull * 1024ull + 1),
               "limits.512mib-encoded-size-exact-vs-over");
    }

    {
        std::stop_source stopped;
        stopped.request_stop();
        ClipboardDibMetadata metadata{7, 8, 56};
        Raster image = Raster::Make(1, 1, RasterFormat::Bgra8888);
        const auto dib = Bytes(kRgb24BottomUp);
        const CancelToken token{stopped.get_token()};
        Expect(ProbeClipboardDib(dib, 4, metadata, token) == ClipboardDibStatus::Cancelled && metadata.width == 0 &&
                   metadata.height == 0 && metadata.pixels == 0,
               "cancel.probe-clears-metadata");
        Expect(DecodeClipboardDib(dib, 4, image, token) == ClipboardDibStatus::Cancelled && !image.HasStorage(),
               "cancel.decode-clears-raster");
        std::vector<std::uint8_t> output{1};
        Expect(EncodeClipboardDibV5(Raster{}, output, token) == ClipboardDibStatus::Cancelled && output.empty(),
               "cancel.encode-before-validation");
    }

    {
        constexpr std::array<std::uint8_t, 16> source{1, 2, 3, 0, 1, 2, 3, 128, 10, 20, 30, 255, 64, 1, 0, 128};
        constexpr std::array<std::uint8_t, 16> expectedPixels{10, 20, 30, 255, 128, 2, 0, 128,
                                                              0,  0,  0,  0,   2,   4, 6, 128};
        const Raster image = Raster::Make(2, 2, RasterFormat::Bgra8888, source.data(), source.size());
        std::vector<std::uint8_t> encoded;
        const ClipboardDibStatus status = EncodeClipboardDibV5(image, encoded);
        const auto expectedHeader = ExpectedV5Header(2, 2, 16);
        std::vector<std::uint8_t> expected{expectedHeader.begin(), expectedHeader.end()};
        expected.insert(expected.end(), expectedPixels.begin(), expectedPixels.end());
        Expect(status == ClipboardDibStatus::Ok && encoded == expected, "encode.literal-v5-header-bottom-up-rounding");
    }

    {
        constexpr std::array<std::uint8_t, 8> source{15, 30, 45, 128, 0, 0, 0, 0};
        const Raster base = Raster::Make(2, 1, RasterFormat::Bgra8888, source.data(), source.size());
        const Raster sparse = Raster::MakeSparse(2, 1, RasterFormat::Bgra8888, base, 0, 0, {}, 0);
        std::vector<std::uint8_t> encoded;
        const ClipboardDibStatus status = EncodeClipboardDibV5(sparse, encoded);
        const auto expectedHeader = ExpectedV5Header(2, 1, 8);
        constexpr std::array<std::uint8_t, 8> expectedPixels{30, 60, 90, 128, 0, 0, 0, 0};
        std::vector<std::uint8_t> expected{expectedHeader.begin(), expectedHeader.end()};
        expected.insert(expected.end(), expectedPixels.begin(), expectedPixels.end());
        Expect(status == ClipboardDibStatus::Ok && encoded == expected && !sparse.IsMaterialized(),
               "encode.sparse-row-read-does-not-materialize");
    }

    {
        const Raster gray = Raster::Make(1, 1, RasterFormat::Gray8);
        std::vector<std::uint8_t> encoded{1, 2, 3};
        Expect(EncodeClipboardDibV5(gray, encoded) == ClipboardDibStatus::Invalid && encoded.empty(),
               "encode.gray-raster-rejected-clears-output");
    }

    return gFailures;
}
