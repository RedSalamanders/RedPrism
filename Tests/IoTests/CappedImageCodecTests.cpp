#include "CappedImageCodecTests.h"

#include "ImageCodecs.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <utility>
#include <vector>

namespace
{
struct Checks final
{
    int total = 0;
    int failed = 0;

    void Expect(bool condition, const char* name) noexcept
    {
        ++total;
        if (condition)
        {
            std::printf("PASS capped-codec.%s\n", name);
            return;
        }
        ++failed;
        std::printf("FAIL capped-codec.%s\n", name);
    }
};

[[nodiscard]] bool SameRaster(const Raster& left, const Raster& right) noexcept
{
    if (!left.HasStorage() || !right.HasStorage() || left.Width() != right.Width() || left.Height() != right.Height() ||
        left.Format() != right.Format() || left.ByteSize() != right.ByteSize())
    {
        return false;
    }
    return std::memcmp(left.Data(), right.Data(), left.ByteSize()) == 0;
}

[[nodiscard]] bool PixelEquals(const Raster& image, int x, int y, const std::array<std::uint8_t, 4>& expected) noexcept
{
    if (!image.HasStorage() || image.Format() != RasterFormat::Bgra8888 || x < 0 || y < 0 || x >= image.Width() ||
        y >= image.Height())
    {
        return false;
    }
    const std::uint8_t* pixel =
        image.Data() + static_cast<std::size_t>(y) * image.Stride() + static_cast<std::size_t>(x) * 4;
    return pixel[0] == expected[0] && pixel[1] == expected[1] && pixel[2] == expected[2] && pixel[3] == expected[3];
}

void CheckCapacity(Checks& checks)
{
    constexpr std::uint64_t mib = 1024ull * 1024ull;
    constexpr std::uint64_t hardCap = 512ull * mib;
    const Raster empty;
    const Raster gray = Raster::Make(1, 1, RasterFormat::Gray8);
    const Raster bgra = Raster::Make(1, 1, RasterFormat::Bgra8888);
    const Raster hugeSparse = Raster::MakeSparse(10000, 10000, RasterFormat::Bgra8888, Raster{}, 0, 0, {}, 0);

    checks.Expect(EncodeMemoryCapacity(empty) == 0, "capacity.empty-is-zero");
    checks.Expect(gray.HasStorage() && EncodeMemoryCapacity(gray) == 4ull * mib + 2, "capacity.one-gray-pixel-budget");
    checks.Expect(bgra.HasStorage() && EncodeMemoryCapacity(bgra) == 4ull * mib + 8, "capacity.one-bgra-pixel-budget");
    checks.Expect(hugeSparse.IsSparse() && !hugeSparse.IsMaterialized() && EncodeMemoryCapacity(hugeSparse) == hardCap,
                  "capacity.100mp-sparse-clips-without-materializing");
}

void CheckCappedPng(Checks& checks, const Raster& source, const char* name, bool gray)
{
    std::vector<std::uint8_t> baseline;
    const ImageCodecStatus baselineStatus = EncodePng(source, 72, baseline);
    checks.Expect(baselineStatus == ImageCodecStatus::Ok && !baseline.empty(), name);
    if (baselineStatus != ImageCodecStatus::Ok || baseline.empty())
    {
        return;
    }

    std::vector<std::uint8_t> encoded;
    ImageCodecStatus status = EncodePng(source, 72, encoded, {}, static_cast<std::uint64_t>(baseline.size()));
    checks.Expect(status == ImageCodecStatus::Ok && encoded == baseline, "png.exact-cap-succeeds-identically");

    encoded.assign({1, 2, 3});
    status = EncodePng(source, 72, encoded, {}, static_cast<std::uint64_t>(baseline.size() - 1));
    checks.Expect(status == ImageCodecStatus::TooLarge && encoded.empty(), "png.one-byte-short-cap-clears-output");

    encoded.assign({1, 2, 3});
    status = EncodePng(source, 72, encoded, {}, 1);
    checks.Expect(status == ImageCodecStatus::TooLarge && encoded.empty(), "png.one-byte-cap-clears-output");

    encoded.assign({1, 2, 3});
    status = EncodePng(source, 72, encoded, {}, 0);
    checks.Expect(status == ImageCodecStatus::TooLarge && encoded.empty(), "png.zero-cap-clears-output");

    encoded.clear();
    status = EncodePng(source, 72, encoded, {}, UINT64_MAX);
    checks.Expect(status == ImageCodecStatus::Ok && encoded == baseline, "png.uint64-max-clips-to-ordinary-budget");

    DecodedImage decoded;
    status = DecodeProjectPng(encoded.data(), encoded.size(), gray, source.PixelCount(), decoded);
    const bool decodedCorrectly =
        gray ? status == ImageCodecStatus::Ok && decoded.image.Format() == RasterFormat::Gray8 &&
                   SameRaster(decoded.image, source)
             : status == ImageCodecStatus::Ok && decoded.image.Format() == RasterFormat::Bgra8888 &&
                   PixelEquals(decoded.image, 0, 0, {0, 0, 0, 0}) &&
                   PixelEquals(decoded.image, 1, 0, {17, 34, 51, 85}) &&
                   PixelEquals(decoded.image, 2, 0, {64, 32, 16, 128}) &&
                   PixelEquals(decoded.image, 3, 0, {0, 255, 127, 255});
    checks.Expect(decodedCorrectly, gray ? "png.gray-existing-roundtrip-expectations"
                                         : "png.premultiplied-existing-literal-expectations");
}

void CheckCappedJpeg(Checks& checks, const Raster& source)
{
    constexpr JpegExportOptions options{1.0, 72};
    std::vector<std::uint8_t> baseline;
    const ImageCodecStatus baselineStatus = EncodeJpeg(source, options, baseline);
    checks.Expect(baselineStatus == ImageCodecStatus::Ok && !baseline.empty(), "jpeg.gray-baseline-encode");
    if (baselineStatus != ImageCodecStatus::Ok || baseline.empty())
    {
        return;
    }

    std::vector<std::uint8_t> encoded;
    ImageCodecStatus status = EncodeJpeg(source, options, encoded, {}, static_cast<std::uint64_t>(baseline.size()));
    checks.Expect(status == ImageCodecStatus::Ok && encoded == baseline, "jpeg.exact-cap-succeeds-identically");

    encoded.assign({1, 2, 3});
    status = EncodeJpeg(source, options, encoded, {}, static_cast<std::uint64_t>(baseline.size() - 1));
    checks.Expect(status == ImageCodecStatus::TooLarge && encoded.empty(), "jpeg.one-byte-short-cap-clears-output");

    encoded.assign({1, 2, 3});
    status = EncodeJpeg(source, options, encoded, {}, 1);
    checks.Expect(status == ImageCodecStatus::TooLarge && encoded.empty(), "jpeg.one-byte-cap-clears-output");

    encoded.assign({1, 2, 3});
    status = EncodeJpeg(source, options, encoded, {}, 0);
    checks.Expect(status == ImageCodecStatus::TooLarge && encoded.empty(), "jpeg.zero-cap-clears-output");

    encoded.clear();
    status = EncodeJpeg(source, options, encoded, {}, UINT64_MAX);
    checks.Expect(status == ImageCodecStatus::Ok && encoded == baseline, "jpeg.uint64-max-clips-to-ordinary-budget");
}

void CheckSparseMixedAlpha(Checks& checks)
{
    constexpr std::array<std::uint8_t, 12> tileBytes{
        17, 34, 51, 85, 64, 32, 16, 128, 0, 255, 127, 255,
    };
    const Raster tilePixels = Raster::Make(3, 1, RasterFormat::Bgra8888, tileBytes.data(), tileBytes.size());
    RasterTile tile;
    tile.pixels = tilePixels;
    tile.x = 1;
    tile.width = 3;
    tile.height = 1;
    std::vector<RasterTile> tiles;
    tiles.push_back(std::move(tile));
    const Raster sparse = Raster::MakeSparse(4, 1, RasterFormat::Bgra8888, Raster{}, 0, 0, std::move(tiles), 0);
    checks.Expect(sparse.IsSparse() && !sparse.IsMaterialized(), "png.mixed-alpha-source-is-sparse");
    if (!sparse.IsSparse())
    {
        return;
    }

    std::vector<std::uint8_t> encoded;
    const ImageCodecStatus encodeStatus = EncodePng(sparse, 72, encoded);
    checks.Expect(encodeStatus == ImageCodecStatus::Ok && !encoded.empty(), "png.mixed-alpha-sparse-encode");
    checks.Expect(!sparse.IsMaterialized(), "png.mixed-alpha-encode-keeps-sparse-source");
    if (encodeStatus != ImageCodecStatus::Ok)
    {
        return;
    }

    DecodedImage decoded;
    const ImageCodecStatus decodeStatus = DecodeProjectPng(encoded.data(), encoded.size(), false, 4, decoded);
    checks.Expect(
        decodeStatus == ImageCodecStatus::Ok && decoded.image.HasStorage() &&
            PixelEquals(decoded.image, 0, 0, {0, 0, 0, 0}) && PixelEquals(decoded.image, 1, 0, {17, 34, 51, 85}) &&
            PixelEquals(decoded.image, 2, 0, {64, 32, 16, 128}) && PixelEquals(decoded.image, 3, 0, {0, 255, 127, 255}),
        "png.mixed-alpha-sparse-literal-pixels");
    checks.Expect(!sparse.IsMaterialized(), "png.mixed-alpha-roundtrip-keeps-sparse-source");
}

void CheckFailures(Checks& checks, const Raster& validBgra)
{
    std::vector<std::uint8_t> output{9, 8, 7};
    const ImageCodecStatus zeroStatus = EncodePng(validBgra, 72, output, {}, 0);
    checks.Expect(zeroStatus == ImageCodecStatus::TooLarge && output.empty(),
                  "failure.valid-raster-zero-cap-is-too-large");

    CancelSource cancelled;
    (void)cancelled.Cancel();
    output.assign({9, 8, 7});
    const ImageCodecStatus pngCancelled = EncodePng(validBgra, 72, output, cancelled.Token(), UINT64_MAX);
    checks.Expect(pngCancelled == ImageCodecStatus::Cancelled && output.empty(), "failure.cancelled-png-clears-output");

    output.assign({9, 8, 7});
    const ImageCodecStatus jpegCancelled =
        EncodeJpeg(validBgra, JpegExportOptions{1.0, 72}, output, cancelled.Token(), UINT64_MAX);
    checks.Expect(jpegCancelled == ImageCodecStatus::Cancelled && output.empty(),
                  "failure.cancelled-jpeg-clears-output");

    const Raster invalid;
    output.assign({9, 8, 7});
    const ImageCodecStatus invalidPng = EncodePng(invalid, 72, output);
    checks.Expect(invalidPng == ImageCodecStatus::Encode && output.empty(), "failure.invalid-png-clears-output");

    output.assign({9, 8, 7});
    const ImageCodecStatus invalidJpeg = EncodeJpeg(invalid, JpegExportOptions{1.0, 72}, output);
    checks.Expect(invalidJpeg == ImageCodecStatus::Encode && output.empty(), "failure.invalid-jpeg-clears-output");
}
} // namespace

int RunCappedImageCodecTests()
{
    Checks checks;
    constexpr std::array<std::uint8_t, 16> bgraPixels{
        0, 0, 0, 0, 17, 34, 51, 85, 64, 32, 16, 128, 0, 255, 127, 255,
    };
    constexpr std::array<std::uint8_t, 4> grayPixels{0, 7, 128, 255};
    const Raster bgra = Raster::Make(4, 1, RasterFormat::Bgra8888, bgraPixels.data(), bgraPixels.size());
    const Raster gray = Raster::Make(2, 2, RasterFormat::Gray8, grayPixels.data(), grayPixels.size());

    CheckCapacity(checks);
    CheckCappedPng(checks, bgra, "png.bgra-baseline-encode", false);
    CheckCappedPng(checks, gray, "png.gray-baseline-encode", true);
    CheckCappedJpeg(checks, gray);
    CheckSparseMixedAlpha(checks);
    CheckFailures(checks, bgra);

    std::printf("CAPPED-IMAGE-CODEC checks total=%d passed=%d failed=%d\n", checks.total, checks.total - checks.failed,
                checks.failed);
    return checks.failed;
}
