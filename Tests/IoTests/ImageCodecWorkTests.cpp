#include "ImageCodecWorkTests.h"

#include "ImageCodecs.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <windows.h>
#include <winioctl.h>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/resource.h>
#pragma warning(pop)

namespace
{
int gFailures = 0;
unsigned long long gScratchSequence = 0;

void Expect(bool condition, const char* name) noexcept
{
    std::printf("%s imagecodecwork.%s\n", condition ? "PASS" : "FAIL", name);
    gFailures += condition ? 0 : 1;
}

class ScratchDirectory final
{
  public:
    ScratchDirectory()
    {
        std::array<wchar_t, 32768> tempPath{};
        const DWORD length = GetTempPathW(static_cast<DWORD>(tempPath.size()), tempPath.data());
        if (length == 0 || static_cast<std::size_t>(length) >= tempPath.size())
        {
            return;
        }

        for (unsigned int attempt = 0; attempt < 32; ++attempt)
        {
            _path.assign(tempPath.data(), length);
            _path.append(L"RedPrism.ImageCodecWork.");
            _path.append(std::to_wstring(GetCurrentProcessId()));
            _path.push_back(L'.');
            _path.append(std::to_wstring(++gScratchSequence));
            if (CreateDirectoryW(_path.c_str(), nullptr))
            {
                _created = true;
                return;
            }
            if (GetLastError() != ERROR_ALREADY_EXISTS)
            {
                return;
            }
        }
    }

    ScratchDirectory(const ScratchDirectory&) = delete;
    ScratchDirectory& operator=(const ScratchDirectory&) = delete;

    ~ScratchDirectory()
    {
        if (_created)
        {
            std::error_code error;
            std::filesystem::remove_all(_path, error);
        }
    }

    [[nodiscard]] bool IsCreated() const noexcept
    {
        return _created;
    }

    [[nodiscard]] std::wstring Child(std::wstring_view name) const
    {
        std::wstring path = _path;
        path.push_back(L'\\');
        path.append(name);
        return path;
    }

    [[nodiscard]] const std::wstring& Path() const noexcept
    {
        return _path;
    }

  private:
    std::wstring _path;
    bool _created = false;
};

[[nodiscard]] bool WriteBytes(const std::wstring& path, std::span<const std::uint8_t> bytes) noexcept
{
    wil::unique_hfile file{
        CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr)};
    if (!file || bytes.size() > MAXDWORD)
    {
        return false;
    }
    DWORD written = 0;
    return WriteFile(file.get(), bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) != FALSE &&
           static_cast<std::size_t>(written) == bytes.size();
}

[[nodiscard]] bool ReadBytes(const std::wstring& path, std::vector<std::uint8_t>& bytes)
{
    wil::unique_hfile file{CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                       FILE_ATTRIBUTE_NORMAL, nullptr)};
    if (!file)
    {
        return false;
    }
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file.get(), &size) || size.QuadPart < 0 ||
        static_cast<unsigned long long>(size.QuadPart) > static_cast<unsigned long long>(MAXDWORD))
    {
        return false;
    }
    bytes.resize(static_cast<std::size_t>(size.QuadPart));
    DWORD read = 0;
    return bytes.empty() ||
           (ReadFile(file.get(), bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr) != FALSE &&
            static_cast<std::size_t>(read) == bytes.size());
}

[[nodiscard]] bool HasOnlySentinelTemp(const ScratchDirectory& scratch, std::wstring_view destinationName,
                                       std::wstring_view sentinelName)
{
    const std::wstring prefix = scratch.Child(destinationName) + L".~";
    const std::wstring sentinel = scratch.Child(sentinelName);
    std::error_code error;
    std::size_t matches = 0;
    for (std::filesystem::directory_iterator it(scratch.Path(), error), end; !error && it != end; it.increment(error))
    {
        const std::wstring current = it->path().native();
        if (current.starts_with(prefix))
        {
            ++matches;
            if (current != sentinel)
            {
                return false;
            }
        }
    }
    return !error && matches == 1;
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

[[nodiscard]] bool JpegPixelNear(const Raster& image, int x, int y, const std::array<std::uint8_t, 3>& expectedBgr,
                                 int tolerance) noexcept
{
    if (!image.HasStorage() || image.Format() != RasterFormat::Bgra8888 || x < 0 || y < 0 || x >= image.Width() ||
        y >= image.Height())
    {
        return false;
    }
    const std::uint8_t* pixel =
        image.Data() + static_cast<std::size_t>(y) * image.Stride() + static_cast<std::size_t>(x) * 4;
    return std::abs(static_cast<int>(pixel[0]) - expectedBgr[0]) <= tolerance &&
           std::abs(static_cast<int>(pixel[1]) - expectedBgr[1]) <= tolerance &&
           std::abs(static_cast<int>(pixel[2]) - expectedBgr[2]) <= tolerance && pixel[3] == 255;
}

void PremultipliedPngRoundTrip()
{
    constexpr std::array<std::uint8_t, 16> sourcePixels{
        0,  0,  0,  0, // fully transparent
        17, 34, 51, 85, 64, 32, 16, 128, 0, 255, 127, 255,
    };
    const Raster source = Raster::Make(4, 1, RasterFormat::Bgra8888, sourcePixels.data(), sourcePixels.size());
    std::vector<std::uint8_t> encoded;
    const ImageCodecStatus encodeStatus = EncodePng(source, 72, encoded);
    Expect(encodeStatus == ImageCodecStatus::Ok && !encoded.empty(), "png.bgra-encode");
    if (encodeStatus != ImageCodecStatus::Ok)
    {
        return;
    }

    DecodedImage decoded;
    const ImageCodecStatus decodeStatus = DecodeProjectPng(encoded.data(), encoded.size(), false, 16, decoded);
    Expect(decodeStatus == ImageCodecStatus::Ok && decoded.image.HasStorage(), "png.bgra-decode");
    if (decodeStatus == ImageCodecStatus::Ok)
    {
        Expect(PixelEquals(decoded.image, 0, 0, {0, 0, 0, 0}), "png.bgra-transparent-literal");
        Expect(PixelEquals(decoded.image, 1, 0, {17, 34, 51, 85}), "png.bgra-alpha-85-literal");
        Expect(PixelEquals(decoded.image, 2, 0, {64, 32, 16, 128}), "png.bgra-alpha-128-literal");
        Expect(PixelEquals(decoded.image, 3, 0, {0, 255, 127, 255}), "png.bgra-opaque-literal");
    }
}

void SparseGrayBandRoundTrip()
{
    constexpr int width = 256;
    constexpr int height = 300;
    std::vector<std::uint8_t> tilePixels(static_cast<std::size_t>(width) * width);
    for (int y = 0; y < width; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            tilePixels[static_cast<std::size_t>(y) * width + x] = static_cast<std::uint8_t>((x * 13 + y * 29) % 251);
        }
    }
    RasterTile tile;
    tile.pixels = Raster::Make(width, width, RasterFormat::Gray8, tilePixels.data(), tilePixels.size());
    tile.width = width;
    tile.height = width;
    std::vector<RasterTile> tiles;
    tiles.push_back(std::move(tile));
    const Raster sparse = Raster::MakeSparse(width, height, RasterFormat::Gray8, Raster{}, 0, 0, std::move(tiles), 7);
    Expect(sparse.IsSparse() && !sparse.IsMaterialized(), "png.gray-source-sparse");

    std::vector<std::uint8_t> encoded;
    const ImageCodecStatus encodeStatus = EncodePng(sparse, 72, encoded);
    Expect(encodeStatus == ImageCodecStatus::Ok && !encoded.empty(), "png.gray-multiband-encode");
    Expect(!sparse.IsMaterialized(), "png.gray-encode-keeps-source-sparse");
    if (encodeStatus != ImageCodecStatus::Ok)
    {
        return;
    }

    DecodedImage decoded;
    const ImageCodecStatus decodeStatus =
        DecodeProjectPng(encoded.data(), encoded.size(), true, width * height, decoded);
    Expect(decodeStatus == ImageCodecStatus::Ok && decoded.image.Format() == RasterFormat::Gray8,
           "png.gray-multiband-decode");
    if (decodeStatus == ImageCodecStatus::Ok)
    {
        const std::uint8_t* bytes = decoded.image.Data();
        bool matches = bytes != nullptr;
        for (int y = 0; matches && y < height; ++y)
        {
            for (int x = 0; x < width; ++x)
            {
                const std::uint8_t expected = y < width ? static_cast<std::uint8_t>((x * 13 + y * 29) % 251) : 7;
                if (bytes[static_cast<std::size_t>(y) * width + x] != expected)
                {
                    matches = false;
                    break;
                }
            }
        }
        Expect(matches, "png.gray-independent-formula-pixels");
    }
}

void SparseBgraMultiBandRoundTrip()
{
    constexpr int width = 4;
    constexpr int height = 4097;
    constexpr std::array<std::uint8_t, 4> expectedPixel{17, 34, 51, 85};
    constexpr std::size_t byteCount = static_cast<std::size_t>(width) * height * expectedPixel.size();
    static_assert(byteCount > 64 * 1024);
    std::vector<std::uint8_t> tileBytes(byteCount);
    for (std::size_t offset = 0; offset < tileBytes.size(); offset += expectedPixel.size())
    {
        for (std::size_t channel = 0; channel < expectedPixel.size(); ++channel)
        {
            tileBytes[offset + channel] = expectedPixel[channel];
        }
    }

    RasterTile tile;
    tile.pixels = Raster::Make(width, height, RasterFormat::Bgra8888, tileBytes.data(), tileBytes.size());
    tile.width = width;
    tile.height = height;
    std::vector<RasterTile> tiles;
    tiles.push_back(std::move(tile));
    const Raster sparse =
        Raster::MakeSparse(width, height, RasterFormat::Bgra8888, Raster{}, 0, 0, std::move(tiles), 0);
    Expect(sparse.IsSparse() && !sparse.IsMaterialized(), "png.bgra-source-sparse-multiband");

    std::vector<std::uint8_t> encoded;
    const ImageCodecStatus encodeStatus = EncodePng(sparse, 72, encoded);
    Expect(encodeStatus == ImageCodecStatus::Ok && !encoded.empty(), "png.bgra-sparse-multiband-encode");
    Expect(!sparse.IsMaterialized(), "png.bgra-sparse-encode-keeps-source-sparse");
    if (encodeStatus != ImageCodecStatus::Ok)
    {
        return;
    }

    DecodedImage decoded;
    const ImageCodecStatus decodeStatus =
        DecodeProjectPng(encoded.data(), encoded.size(), false, width * height, decoded);
    Expect(decodeStatus == ImageCodecStatus::Ok && decoded.image.Format() == RasterFormat::Bgra8888,
           "png.bgra-sparse-multiband-decode");
    if (decodeStatus != ImageCodecStatus::Ok || !decoded.image.HasStorage())
    {
        return;
    }

    bool matches = decoded.image.Width() == width && decoded.image.Height() == height;
    for (int y = 0; matches && y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            if (!PixelEquals(decoded.image, x, y, {17, 34, 51, 85}))
            {
                matches = false;
                break;
            }
        }
    }
    Expect(matches, "png.bgra-sparse-multiband-all-pixels-literal");

    ScratchDirectory scratch;
    Expect(scratch.IsCreated(), "png.bgra-file-scratch-created");
    if (!scratch.IsCreated())
        return;
    const std::wstring path = scratch.Child(L"sparse.png");
    Expect(EncodePngFile(sparse, 72, path.c_str()) == ImageCodecStatus::Ok, "png.bgra-direct-file-encode");
    Expect(!sparse.IsMaterialized(), "png.bgra-direct-file-keeps-source-sparse");
    DecodedImage fileDecoded;
    const auto fileStatus = DecodeProjectPngFile(path.c_str(), false, width * height, fileDecoded);
    matches = fileStatus == ImageCodecStatus::Ok && fileDecoded.image.Width() == width &&
              fileDecoded.image.Height() == height;
    for (int y = 0; matches && y < height; ++y)
        for (int x = 0; matches && x < width; ++x)
            matches = PixelEquals(fileDecoded.image, x, y, {17, 34, 51, 85});
    Expect(matches, "png.bgra-direct-file-multiband-literal-pixels");
}

void SparseJpegMultiBandWhiteMatte()
{
    constexpr int width = 64;
    constexpr int height = 520;
    constexpr int stripeStart = 232;
    constexpr int stripeEnd = 280;
    constexpr int bytesPerPixel = 4;
    constexpr int bandRows = (64 * 1024) / (width * bytesPerPixel);
    static_assert(bandRows == 256);
    static_assert(stripeStart < bandRows && stripeEnd > bandRows);

    std::vector<RasterTile> tiles;
    const auto appendStripe = [&](int y, int stripeHeight, const std::array<std::uint8_t, 4>& pixel)
    {
        std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * stripeHeight * bytesPerPixel);
        for (std::size_t offset = 0; offset < pixels.size(); offset += bytesPerPixel)
        {
            std::copy(pixel.begin(), pixel.end(), pixels.begin() + static_cast<std::ptrdiff_t>(offset));
        }
        RasterTile tile;
        tile.pixels = Raster::Make(width, stripeHeight, RasterFormat::Bgra8888, pixels.data(), pixels.size());
        tile.y = y;
        tile.width = width;
        tile.height = stripeHeight;
        tiles.push_back(std::move(tile));
    };
    appendStripe(0, stripeStart, {40, 80, 120, 255});
    appendStripe(stripeStart, stripeEnd - stripeStart, {16, 32, 48, 128});
    appendStripe(stripeEnd, height - stripeEnd, {0, 80, 0, 128});

    const Raster sparse =
        Raster::MakeSparse(width, height, RasterFormat::Bgra8888, Raster{}, 0, 0, std::move(tiles), 0);
    Expect(sparse.IsSparse() && !sparse.IsMaterialized(), "jpeg.source-sparse-multiband");

    std::vector<std::uint8_t> encoded;
    const ImageCodecStatus encodeStatus = EncodeJpeg(sparse, JpegExportOptions{1.0, 72}, encoded);
    Expect(encodeStatus == ImageCodecStatus::Ok && !encoded.empty(), "jpeg.sparse-multiband-encode");
    Expect(!sparse.IsMaterialized(), "jpeg.encode-keeps-source-sparse");
    if (encodeStatus != ImageCodecStatus::Ok)
    {
        return;
    }

    ScratchDirectory scratch;
    Expect(scratch.IsCreated(), "jpeg.sparse-multiband-scratch-created");
    if (!scratch.IsCreated())
    {
        return;
    }
    const std::wstring path = scratch.Child(L"banded.jpg");
    Expect(WriteBytes(path, encoded), "jpeg.sparse-multiband-bytes-written");
    DecodedImage decoded;
    const ImageCodecStatus decodeStatus = ImportImageFile(path.c_str(), width * height, decoded);
    Expect(decodeStatus == ImageCodecStatus::Ok && decoded.image.Width() == width && decoded.image.Height() == height,
           "jpeg.sparse-multiband-decode-dimensions");
    if (decodeStatus != ImageCodecStatus::Ok || !decoded.image.HasStorage())
    {
        return;
    }

    // Literal white-matte values are B+255-A per channel. WIC quality 1 JPEG remains lossy (including possible
    // chroma subsampling), so central samples in broad uniform stripes allow 12/255 per RGB channel. The two center
    // samples straddle the 256-row conversion-band boundary while staying at least 22 rows from either color edge.
    constexpr int kJpegLossyRgbTolerance = 12;
    Expect(JpegPixelNear(decoded.image, 32, 112, {40, 80, 120}, kJpegLossyRgbTolerance),
           "jpeg.white-matte-opaque-stripe-literal");
    Expect(JpegPixelNear(decoded.image, 32, 254, {143, 159, 175}, kJpegLossyRgbTolerance),
           "jpeg.white-matte-before-band-boundary-literal");
    Expect(JpegPixelNear(decoded.image, 32, 258, {143, 159, 175}, kJpegLossyRgbTolerance),
           "jpeg.white-matte-after-band-boundary-literal");
    Expect(JpegPixelNear(decoded.image, 32, 400, {127, 207, 127}, kJpegLossyRgbTolerance),
           "jpeg.white-matte-alpha-stripe-literal");

    const std::wstring filePath = scratch.Child(L"direct.jpg");
    Expect(EncodeJpegFile(sparse, JpegExportOptions{1.0, 72}, filePath.c_str()) == ImageCodecStatus::Ok,
           "jpeg.direct-file-encode");
    Expect(!sparse.IsMaterialized(), "jpeg.direct-file-keeps-source-sparse");
    DecodedImage fileDecoded;
    Expect(ImportImageFile(filePath.c_str(), width * height, fileDecoded) == ImageCodecStatus::Ok &&
               fileDecoded.image.Width() == width && fileDecoded.image.Height() == height &&
               JpegPixelNear(fileDecoded.image, 32, 112, {40, 80, 120}, kJpegLossyRgbTolerance) &&
               JpegPixelNear(fileDecoded.image, 32, 254, {143, 159, 175}, kJpegLossyRgbTolerance) &&
               JpegPixelNear(fileDecoded.image, 32, 258, {143, 159, 175}, kJpegLossyRgbTolerance) &&
               JpegPixelNear(fileDecoded.image, 32, 400, {127, 207, 127}, kJpegLossyRgbTolerance),
           "jpeg.direct-file-multiband-white-matte-literal");
}

void PreCancelledCalls()
{
    constexpr std::array<std::uint8_t, 4> pixel{0, 0, 0, 255};
    const Raster source = Raster::Make(1, 1, RasterFormat::Bgra8888, pixel.data(), pixel.size());
    std::vector<std::uint8_t> encoded;
    const ImageCodecStatus initialEncode = EncodePng(source, 72, encoded);
    Expect(initialEncode == ImageCodecStatus::Ok, "cancel.fixture-encode");
    if (initialEncode != ImageCodecStatus::Ok)
    {
        return;
    }

    CancelSource cancelled;
    (void)cancelled.Cancel();
    DecodedImage decoded;
    decoded.image = source;
    decoded.thumbnail = source;
    const ImageCodecStatus decodeStatus =
        DecodeProjectPng(encoded.data(), encoded.size(), false, 1, decoded, cancelled.Token());
    Expect(decodeStatus == ImageCodecStatus::Cancelled && !decoded.image.HasStorage() &&
               !decoded.thumbnail.HasStorage() && decoded.name.empty(),
           "cancel.decode-clears-output");

    std::vector<std::uint8_t> output{1, 2, 3};
    const ImageCodecStatus encodeStatus = EncodePng(source, 72, output, cancelled.Token());
    Expect(encodeStatus == ImageCodecStatus::Cancelled && output.empty(), "cancel.encode-clears-output");
}

void PreCancelledFileEncodesPreserveFiles()
{
    ScratchDirectory scratch;
    Expect(scratch.IsCreated(), "cancel.file-scratch-created");
    if (!scratch.IsCreated())
    {
        return;
    }

    constexpr std::array<std::uint8_t, 4> priorPixel{9, 8, 7, 255};
    constexpr std::array<std::uint8_t, 4> replacementPixel{1, 2, 3, 255};
    constexpr std::array<std::uint8_t, 8> sentinelBytes{0x53, 0x45, 0x4E, 0x54, 0x49, 0x4E, 0x45, 0x4C};
    const Raster prior = Raster::Make(1, 1, RasterFormat::Bgra8888, priorPixel.data(), priorPixel.size());
    const Raster replacement =
        Raster::Make(1, 1, RasterFormat::Bgra8888, replacementPixel.data(), replacementPixel.size());
    std::vector<std::uint8_t> previousPng;
    const ImageCodecStatus fixtureStatus = EncodePng(prior, 72, previousPng);
    Expect(fixtureStatus == ImageCodecStatus::Ok && !previousPng.empty(), "cancel.file-prior-fixture-encoded");
    if (fixtureStatus != ImageCodecStatus::Ok)
    {
        return;
    }

    CancelSource cancelled;
    (void)cancelled.Cancel();
    const JpegExportOptions jpegOptions{};
    const std::array<std::wstring_view, 2> destinations{L"cancel.png", L"cancel.jpg"};
    const std::array<std::wstring_view, 2> sentinels{L"cancel.png.~00000000-0000-4000-8000-000000000001",
                                                     L"cancel.jpg.~00000000-0000-4000-8000-000000000002"};

    for (std::size_t index = 0; index < destinations.size(); ++index)
    {
        const std::wstring destination = scratch.Child(destinations[index]);
        const std::wstring sentinel = scratch.Child(sentinels[index]);
        Expect(WriteBytes(destination, previousPng),
               index == 0 ? "cancel.png-prior-created" : "cancel.jpeg-prior-created");
        Expect(WriteBytes(sentinel, sentinelBytes),
               index == 0 ? "cancel.png-sentinel-created" : "cancel.jpeg-sentinel-created");
        std::vector<std::uint8_t> before;
        Expect(ReadBytes(destination, before) && before == previousPng,
               index == 0 ? "cancel.png-prior-readable" : "cancel.jpeg-prior-readable");

        const ImageCodecStatus status =
            index == 0 ? EncodePngFile(replacement, 72, destination.c_str(), cancelled.Token())
                       : EncodeJpegFile(replacement, jpegOptions, destination.c_str(), cancelled.Token());
        Expect(status == ImageCodecStatus::Cancelled,
               index == 0 ? "cancel.png-file-status" : "cancel.jpeg-file-status");
        std::vector<std::uint8_t> after;
        Expect(ReadBytes(destination, after) && after == before,
               index == 0 ? "cancel.png-file-preserves-destination" : "cancel.jpeg-file-preserves-destination");
        std::vector<std::uint8_t> sentinelAfter;
        Expect(ReadBytes(sentinel, sentinelAfter) &&
                   std::equal(sentinelAfter.begin(), sentinelAfter.end(), sentinelBytes.begin(), sentinelBytes.end()) &&
                   HasOnlySentinelTemp(scratch, destinations[index], sentinels[index]),
               index == 0 ? "cancel.png-file-preserves-unrelated-temp" : "cancel.jpeg-file-preserves-unrelated-temp");
    }
}

void LockedDestinationFailurePreservesFiles()
{
    ScratchDirectory scratch;
    Expect(scratch.IsCreated(), "overwrite.scratch-created");
    if (!scratch.IsCreated())
    {
        return;
    }

    constexpr std::array<std::uint8_t, 4> priorPixel{9, 8, 7, 255};
    constexpr std::array<std::uint8_t, 4> replacementPixel{1, 2, 3, 255};
    constexpr std::array<std::uint8_t, 9> sentinelBytes{0x55, 0x4E, 0x52, 0x45, 0x4C, 0x41, 0x54, 0x45, 0x44};
    const Raster prior = Raster::Make(1, 1, RasterFormat::Bgra8888, priorPixel.data(), priorPixel.size());
    const Raster replacement =
        Raster::Make(1, 1, RasterFormat::Bgra8888, replacementPixel.data(), replacementPixel.size());
    std::vector<std::uint8_t> previousBytes;
    std::vector<std::uint8_t> replacementBytes;
    Expect(EncodePng(prior, 72, previousBytes) == ImageCodecStatus::Ok &&
               EncodePng(replacement, 72, replacementBytes) == ImageCodecStatus::Ok &&
               previousBytes != replacementBytes,
           "overwrite.distinguishable-encoded-fixtures");
    if (previousBytes.empty() || replacementBytes.empty() || previousBytes == replacementBytes)
    {
        return;
    }

    const std::wstring destination = scratch.Child(L"locked.png");
    const std::wstring sentinel = scratch.Child(L"locked.png.~00000000-0000-4000-8000-000000000003");
    Expect(WriteBytes(destination, previousBytes), "overwrite.prior-destination-created");
    Expect(WriteBytes(sentinel, sentinelBytes), "overwrite.sentinel-created");

    ImageCodecStatus status = ImageCodecStatus::Ok;
    {
        wil::unique_hfile lock{CreateFileW(destination.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                                           FILE_ATTRIBUTE_NORMAL, nullptr)};
        Expect(static_cast<bool>(lock), "overwrite.destination-locked-exclusive");
        if (!lock)
        {
            return;
        }
        status = EncodePngFile(replacement, 72, destination.c_str());
    }

    Expect(status == ImageCodecStatus::Encode, "overwrite.locked-destination-encode-status");
    std::vector<std::uint8_t> after;
    Expect(ReadBytes(destination, after) && after == previousBytes,
           "overwrite.failed-replacement-preserves-original-bytes");
    DecodedImage decoded;
    const ImageCodecStatus decodeStatus = DecodeProjectPngFile(destination.c_str(), false, 1, decoded);
    Expect(decodeStatus == ImageCodecStatus::Ok && PixelEquals(decoded.image, 0, 0, {9, 8, 7, 255}),
           "overwrite.failed-replacement-preserves-original-content");
    std::vector<std::uint8_t> sentinelAfter;
    Expect(ReadBytes(sentinel, sentinelAfter) &&
               std::equal(sentinelAfter.begin(), sentinelAfter.end(), sentinelBytes.begin(), sentinelBytes.end()) &&
               HasOnlySentinelTemp(scratch, L"locked.png", L"locked.png.~00000000-0000-4000-8000-000000000003"),
           "overwrite.failure-cleans-owned-temp-preserves-unrelated-temp");
}

constexpr std::array<std::uint8_t, 58> kUnsupportedBmp{
    0x42, 0x4D, 0x3A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x36, 0x00, 0x00, 0x00, 0x28,
    0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x18, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x13, 0x0B, 0x00, 0x00, 0x13, 0x0B, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x49, 0x6F, 0x9D, 0x00,
};
constexpr std::array<std::uint8_t, 77> kProbePng{
    0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52,
    0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x03, 0x08, 0x02, 0x00, 0x00, 0x00, 0x36, 0x88, 0x49,
    0xD6, 0x00, 0x00, 0x00, 0x14, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9C, 0x63, 0xF4, 0xCC, 0x9F, 0xCB,
    0xC0, 0xC0, 0xC0, 0xC4, 0x00, 0x06, 0x50, 0x0A, 0x00, 0x19, 0x4F, 0x01, 0x5B, 0x54, 0x13, 0x85,
    0x0A, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82,
};

// Literal baseline fixture: 3x2 RGB JPEG, emitted by Pillow 12.1.1 with quality=90,
// subsampling=0, optimize=false, progressive=false; the encoded bytes are stored below, not generated in-test.
constexpr std::array<std::uint8_t, 631> kProbeJpeg{
    0xFF, 0xD8, 0xFF, 0xE0, 0x00, 0x10, 0x4A, 0x46, 0x49, 0x46, 0x00, 0x01, 0x01, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00,
    0x00, 0xFF, 0xDB, 0x00, 0x43, 0x00, 0x03, 0x02, 0x02, 0x03, 0x02, 0x02, 0x03, 0x03, 0x03, 0x03, 0x04, 0x03, 0x03,
    0x04, 0x05, 0x08, 0x05, 0x05, 0x04, 0x04, 0x05, 0x0A, 0x07, 0x07, 0x06, 0x08, 0x0C, 0x0A, 0x0C, 0x0C, 0x0B, 0x0A,
    0x0B, 0x0B, 0x0D, 0x0E, 0x12, 0x10, 0x0D, 0x0E, 0x11, 0x0E, 0x0B, 0x0B, 0x10, 0x16, 0x10, 0x11, 0x13, 0x14, 0x15,
    0x15, 0x15, 0x0C, 0x0F, 0x17, 0x18, 0x16, 0x14, 0x18, 0x12, 0x14, 0x15, 0x14, 0xFF, 0xDB, 0x00, 0x43, 0x01, 0x03,
    0x04, 0x04, 0x05, 0x04, 0x05, 0x09, 0x05, 0x05, 0x09, 0x14, 0x0D, 0x0B, 0x0D, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14,
    0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14,
    0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14,
    0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0xFF, 0xC0, 0x00, 0x11, 0x08, 0x00, 0x02, 0x00, 0x03, 0x03, 0x01, 0x11, 0x00,
    0x02, 0x11, 0x01, 0x03, 0x11, 0x01, 0xFF, 0xC4, 0x00, 0x1F, 0x00, 0x00, 0x01, 0x05, 0x01, 0x01, 0x01, 0x01, 0x01,
    0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A,
    0x0B, 0xFF, 0xC4, 0x00, 0xB5, 0x10, 0x00, 0x02, 0x01, 0x03, 0x03, 0x02, 0x04, 0x03, 0x05, 0x05, 0x04, 0x04, 0x00,
    0x00, 0x01, 0x7D, 0x01, 0x02, 0x03, 0x00, 0x04, 0x11, 0x05, 0x12, 0x21, 0x31, 0x41, 0x06, 0x13, 0x51, 0x61, 0x07,
    0x22, 0x71, 0x14, 0x32, 0x81, 0x91, 0xA1, 0x08, 0x23, 0x42, 0xB1, 0xC1, 0x15, 0x52, 0xD1, 0xF0, 0x24, 0x33, 0x62,
    0x72, 0x82, 0x09, 0x0A, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2A, 0x34, 0x35, 0x36, 0x37,
    0x38, 0x39, 0x3A, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4A, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5A,
    0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6A, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7A, 0x83, 0x84, 0x85,
    0x86, 0x87, 0x88, 0x89, 0x8A, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9A, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6,
    0xA7, 0xA8, 0xA9, 0xAA, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6, 0xB7, 0xB8, 0xB9, 0xBA, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7,
    0xC8, 0xC9, 0xCA, 0xD2, 0xD3, 0xD4, 0xD5, 0xD6, 0xD7, 0xD8, 0xD9, 0xDA, 0xE1, 0xE2, 0xE3, 0xE4, 0xE5, 0xE6, 0xE7,
    0xE8, 0xE9, 0xEA, 0xF1, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7, 0xF8, 0xF9, 0xFA, 0xFF, 0xC4, 0x00, 0x1F, 0x01, 0x00,
    0x03, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x02, 0x03,
    0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0xFF, 0xC4, 0x00, 0xB5, 0x11, 0x00, 0x02, 0x01, 0x02, 0x04, 0x04,
    0x03, 0x04, 0x07, 0x05, 0x04, 0x04, 0x00, 0x01, 0x02, 0x77, 0x00, 0x01, 0x02, 0x03, 0x11, 0x04, 0x05, 0x21, 0x31,
    0x06, 0x12, 0x41, 0x51, 0x07, 0x61, 0x71, 0x13, 0x22, 0x32, 0x81, 0x08, 0x14, 0x42, 0x91, 0xA1, 0xB1, 0xC1, 0x09,
    0x23, 0x33, 0x52, 0xF0, 0x15, 0x62, 0x72, 0xD1, 0x0A, 0x16, 0x24, 0x34, 0xE1, 0x25, 0xF1, 0x17, 0x18, 0x19, 0x1A,
    0x26, 0x27, 0x28, 0x29, 0x2A, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3A, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4A,
    0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5A, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6A, 0x73, 0x74, 0x75,
    0x76, 0x77, 0x78, 0x79, 0x7A, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8A, 0x92, 0x93, 0x94, 0x95, 0x96,
    0x97, 0x98, 0x99, 0x9A, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7, 0xA8, 0xA9, 0xAA, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6, 0xB7,
    0xB8, 0xB9, 0xBA, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7, 0xC8, 0xC9, 0xCA, 0xD2, 0xD3, 0xD4, 0xD5, 0xD6, 0xD7, 0xD8,
    0xD9, 0xDA, 0xE2, 0xE3, 0xE4, 0xE5, 0xE6, 0xE7, 0xE8, 0xE9, 0xEA, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7, 0xF8, 0xF9,
    0xFA, 0xFF, 0xDA, 0x00, 0x0C, 0x03, 0x01, 0x00, 0x02, 0x11, 0x03, 0x11, 0x00, 0x3F, 0x00, 0xE0, 0xAB, 0xF4, 0xD3,
    0xE0, 0x8F, 0xFF, 0xD9,
};

[[nodiscard]] bool SetFileLengthForProbeTest(const std::wstring& path, std::uint64_t length) noexcept
{
    wil::unique_hfile file{
        CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)};
    if (!file || length > static_cast<std::uint64_t>(INT64_MAX))
    {
        return false;
    }
    FILE_SET_SPARSE_BUFFER sparse{};
    sparse.SetSparse = TRUE;
    DWORD returned = 0;
    if (!DeviceIoControl(file.get(), FSCTL_SET_SPARSE, &sparse, sizeof(sparse), nullptr, 0, &returned, nullptr))
    {
        return false;
    }
    LARGE_INTEGER end{};
    end.QuadPart = static_cast<std::int64_t>(length);
    return SetFilePointerEx(file.get(), end, nullptr, FILE_BEGIN) && SetEndOfFile(file.get());
}

[[nodiscard]] bool RewriteProbeBytes(const std::wstring& path, std::span<const std::uint8_t> bytes) noexcept
{
    wil::unique_hfile file{
        CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr)};
    if (!file || bytes.size() > MAXDWORD)
    {
        return false;
    }
    DWORD written = 0;
    return WriteFile(file.get(), bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) != FALSE &&
           static_cast<std::size_t>(written) == bytes.size();
}

void WriteBigEndian32(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint32_t value) noexcept
{
    bytes[offset] = static_cast<std::uint8_t>(value >> 24);
    bytes[offset + 1] = static_cast<std::uint8_t>(value >> 16);
    bytes[offset + 2] = static_cast<std::uint8_t>(value >> 8);
    bytes[offset + 3] = static_cast<std::uint8_t>(value);
}

[[nodiscard]] std::uint32_t PngCrc32(std::span<const std::uint8_t> bytes) noexcept
{
    std::uint32_t crc = 0xFFFFFFFFu;
    for (const std::uint8_t value : bytes)
    {
        crc ^= value;
        for (unsigned int bit = 0; bit < 8; ++bit)
        {
            crc = (crc & 1u) != 0 ? (crc >> 1) ^ 0xEDB88320u : crc >> 1;
        }
    }
    return crc ^ 0xFFFFFFFFu;
}

[[nodiscard]] std::vector<std::uint8_t> MakeOversizedPngHeader()
{
    std::vector<std::uint8_t> bytes(kProbePng.begin(), kProbePng.end());
    // A structurally valid IHDR with 100,010,000 pixels: metadata readers must reject it before CopyPixels.
    WriteBigEndian32(bytes, 16, 10'000);
    WriteBigEndian32(bytes, 20, 10'001);
    WriteBigEndian32(bytes, 29, PngCrc32(std::span<const std::uint8_t>(bytes).subspan(12, 17)));
    return bytes;
}

[[nodiscard]] bool DecodedImageIsEmpty(const DecodedImage& decoded) noexcept
{
    return !decoded.image.HasStorage() && !decoded.thumbnail.HasStorage() && decoded.name.empty();
}

void ClipboardPngMetadataProbe()
{
    const auto empty = [](const ImageImportMetadata& value) noexcept
    { return value.rawWidth == 0 && value.rawHeight == 0 && value.pixels == 0 && value.encodedBytes == 0; };
    ImageImportMetadata metadata{9, 8, 72, 44};
    Expect(ProbeProjectPng(kProbePng.data(), kProbePng.size(), 6, metadata) == ImageCodecStatus::Ok &&
               metadata.rawWidth == 2 && metadata.rawHeight == 3 && metadata.pixels == 6 && metadata.encodedBytes == 77,
           "clipboard-png-probe.literal-dimensions-and-equal-budget");
    Expect(ProbeProjectPng(kProbePng.data(), kProbePng.size(), 5, metadata) == ImageCodecStatus::TooLarge &&
               empty(metadata),
           "clipboard-png-probe.lower-budget-clears-metadata");
    metadata = {9, 8, 72, 44};
    Expect(ProbeProjectPng(kProbePng.data(), kProbePng.size(), -1, metadata) == ImageCodecStatus::TooLarge &&
               empty(metadata),
           "clipboard-png-probe.negative-budget-clears-metadata");
    Expect(ProbeProjectPng(kProbePng.data(), kProbePng.size(), 0, metadata) == ImageCodecStatus::TooLarge &&
               empty(metadata),
           "clipboard-png-probe.zero-budget-refuses-nonempty-frame");
    metadata = {9, 8, 72, 44};
    Expect(ProbeProjectPng(nullptr, 0, 6, metadata) == ImageCodecStatus::Unreadable && empty(metadata),
           "clipboard-png-probe.empty-input-clears-metadata");
    Expect(ProbeProjectPng(nullptr, 1, 6, metadata) == ImageCodecStatus::Unreadable && empty(metadata),
           "clipboard-png-probe.null-input-refused");
    Expect(ProbeProjectPng(kProbePng.data(), 4, 6, metadata) == ImageCodecStatus::Unreadable && empty(metadata),
           "clipboard-png-probe.truncated-signature-refused");
    Expect(ProbeProjectPng(kProbeJpeg.data(), kProbeJpeg.size(), 6, metadata) == ImageCodecStatus::Unreadable &&
               empty(metadata),
           "clipboard-png-probe.other-container-refused");
    Expect(ProbeProjectPng(nullptr, static_cast<std::size_t>(kMaxImageEncodedBytes + 1), 6, metadata) ==
                   ImageCodecStatus::TooLarge &&
               empty(metadata),
           "clipboard-png-probe.encoded-cap-before-reading-input");
    const auto oversized = MakeOversizedPngHeader();
    Expect(ProbeProjectPng(oversized.data(), oversized.size(), INT64_MAX, metadata) == ImageCodecStatus::TooLarge &&
               empty(metadata),
           "clipboard-png-probe.independent-hard-pixel-limit");
    std::stop_source stopped;
    stopped.request_stop();
    metadata = {9, 8, 72, 44};
    Expect(ProbeProjectPng(kProbePng.data(), kProbePng.size(), 6, metadata, CancelToken{stopped.get_token()}) ==
                   ImageCodecStatus::Cancelled &&
               empty(metadata),
           "clipboard-png-probe.precancel-clears-metadata");
}

void ImportTiffUsesFirstFrame()
{
    ScratchDirectory scratch;
    Expect(scratch.IsCreated(), "import-tiff.scratch-created");
    if (!scratch.IsCreated())
        return;
    // Independent classic TIFF layout: two uncompressed RGB strips, little-endian IFDs. Frame zero is 2x1
    // red/green, frame one is 1x2 blue/yellow. No RedPrism encoder is involved in producing these bytes.
    std::vector<std::uint8_t> bytes(260, 0);
    const auto put16 = [&](std::size_t offset, std::uint16_t value)
    {
        bytes[offset] = static_cast<std::uint8_t>(value);
        bytes[offset + 1] = static_cast<std::uint8_t>(value >> 8);
    };
    const auto put32 = [&](std::size_t offset, std::uint32_t value)
    {
        put16(offset, static_cast<std::uint16_t>(value));
        put16(offset + 2, static_cast<std::uint16_t>(value >> 16));
    };
    bytes[0] = 'I';
    bytes[1] = 'I';
    put16(2, 42);
    put32(4, 8);
    const auto ifd = [&](std::size_t offset, std::uint16_t width, std::uint16_t height, std::uint32_t bitsOffset,
                         std::uint32_t dataOffset, std::uint32_t next)
    {
        put16(offset, 9);
        std::size_t entry = offset + 2;
        const auto tag = [&](std::uint16_t key, std::uint16_t type, std::uint32_t count, std::uint32_t value)
        {
            put16(entry, key);
            put16(entry + 2, type);
            put32(entry + 4, count);
            put32(entry + 8, value);
            entry += 12;
        };
        tag(256, 3, 1, width);
        tag(257, 3, 1, height);
        tag(258, 3, 3, bitsOffset);
        tag(259, 3, 1, 1);
        tag(262, 3, 1, 2);
        tag(273, 4, 1, dataOffset);
        tag(277, 3, 1, 3);
        tag(278, 4, 1, height);
        tag(279, 4, 1, 6);
        put32(entry, next);
        for (std::size_t channel = 0; channel < 3; ++channel)
            put16(bitsOffset + 2 * channel, 8);
    };
    ifd(8, 2, 1, 122, 128, 134);
    ifd(134, 1, 2, 248, 254, 0);
    constexpr std::array<std::uint8_t, 6> first{255, 0, 0, 0, 255, 0};
    constexpr std::array<std::uint8_t, 6> second{0, 0, 255, 255, 255, 0};
    std::copy(first.begin(), first.end(), bytes.begin() + 128);
    std::copy(second.begin(), second.end(), bytes.begin() + 254);
    const auto path = scratch.Child(L"two-frames.tif");
    Expect(WriteBytes(path, bytes), "import-tiff.two-frame-literal-written");
    ImageImportMetadata metadata;
    Expect(ProbeImportImageFile(path.c_str(), 2, metadata) == ImageCodecStatus::Ok && metadata.rawWidth == 2 &&
               metadata.rawHeight == 1 && metadata.pixels == 2 && metadata.encodedBytes == 260,
           "import-tiff.metadata-is-frame-zero");
    DecodedImage decoded;
    constexpr std::array<std::uint8_t, 8> expected{0, 0, 255, 255, 0, 255, 0, 255};
    Expect(ImportImageFile(path.c_str(), 2, decoded, {}, 260) == ImageCodecStatus::Ok && decoded.image.Width() == 2 &&
               decoded.image.Height() == 1 && std::equal(expected.begin(), expected.end(), decoded.image.Data()),
           "import-tiff.pixels-are-red-green-frame-zero");
}

void ImageImportCodecProbeAndBudgetGuards()
{
    ScratchDirectory scratch;
    Expect(scratch.IsCreated(), "import-codec.scratch-created");
    if (!scratch.IsCreated())
    {
        return;
    }

    const std::wstring pngPath = scratch.Child(L"literal.png");
    const std::wstring jpegPath = scratch.Child(L"literal.jpg");
    Expect(WriteBytes(pngPath, kProbePng), "import-codec.png-written");
    Expect(WriteBytes(jpegPath, kProbeJpeg), "import-codec.jpeg-written");

    ImageImportMetadata png{};
    Expect(ProbeImportImageFile(pngPath.c_str(), 6, png) == ImageCodecStatus::Ok && png.rawWidth == 2 &&
               png.rawHeight == 3 && png.pixels == 6 && png.encodedBytes == kProbePng.size(),
           "import-codec.png-metadata-literal-2x3");
    ImageImportMetadata jpeg{};
    Expect(ProbeImportImageFile(jpegPath.c_str(), 6, jpeg) == ImageCodecStatus::Ok && jpeg.rawWidth == 3 &&
               jpeg.rawHeight == 2 && jpeg.pixels == 6 && jpeg.encodedBytes == kProbeJpeg.size(),
           "import-codec.jpeg-first-frame-metadata-literal-3x2");

    DecodedImage equalCap{};
    Expect(ImportImageFile(pngPath.c_str(), 6, equalCap, {}, static_cast<std::uint64_t>(kProbePng.size())) ==
                   ImageCodecStatus::Ok &&
               equalCap.image.Width() == 2 && equalCap.image.Height() == 3,
           "import-codec.caller-encoded-cap-equality-accepted");
    DecodedImage lowerCap{};
    Expect(ImportImageFile(pngPath.c_str(), 6, lowerCap, {}, static_cast<std::uint64_t>(kProbePng.size() - 1)) ==
                   ImageCodecStatus::TooLarge &&
               DecodedImageIsEmpty(lowerCap),
           "import-codec.caller-encoded-cap-lower-refused-empty");

    ImageImportMetadata negativeProbe{9, 8, 72, 44};
    Expect(ProbeImportImageFile(pngPath.c_str(), -1, negativeProbe) == ImageCodecStatus::TooLarge &&
               negativeProbe.rawWidth == 0 && negativeProbe.rawHeight == 0 && negativeProbe.pixels == 0 &&
               negativeProbe.encodedBytes == 0,
           "import-codec.negative-probe-budget-too-large-clears-output");
    DecodedImage negativeDecode = equalCap;
    Expect(ImportImageFile(pngPath.c_str(), -1, negativeDecode) == ImageCodecStatus::TooLarge &&
               DecodedImageIsEmpty(negativeDecode),
           "import-codec.negative-decode-budget-too-large-empty");
    DecodedImage negativeProject = equalCap;
    Expect(DecodeProjectPng(kProbePng.data(), kProbePng.size(), false, -1, negativeProject) ==
                   ImageCodecStatus::TooLarge &&
               DecodedImageIsEmpty(negativeProject),
           "import-codec.negative-project-memory-budget-empty");
    negativeProject = equalCap;
    Expect(DecodeProjectPngFile(pngPath.c_str(), false, -1, negativeProject) == ImageCodecStatus::TooLarge &&
               DecodedImageIsEmpty(negativeProject),
           "import-codec.negative-project-file-budget-empty");

    const std::wstring oversizedPath = scratch.Child(L"oversized-header.png");
    const std::vector<std::uint8_t> oversizedPng = MakeOversizedPngHeader();
    Expect(WriteBytes(oversizedPath, oversizedPng), "import-codec.oversized-header-written");
    ImageImportMetadata oversizedProbe{9, 8, 72, 44};
    Expect(ProbeImportImageFile(oversizedPath.c_str(), INT64_MAX, oversizedProbe) == ImageCodecStatus::TooLarge &&
               oversizedProbe.rawWidth == 0 && oversizedProbe.rawHeight == 0 && oversizedProbe.pixels == 0 &&
               oversizedProbe.encodedBytes == 0,
           "import-codec.hard-pixel-limit-probe-before-publication");
    DecodedImage oversizedDecode{};
    Expect(ImportImageFile(oversizedPath.c_str(), INT_MAX, oversizedDecode) == ImageCodecStatus::TooLarge &&
               DecodedImageIsEmpty(oversizedDecode),
           "import-codec.hard-pixel-limit-decode-before-allocation");

    CancelSource cancelled;
    (void)cancelled.Cancel();
    ImageImportMetadata cancelledMetadata{9, 8, 72, 44};
    Expect(ProbeImportImageFile(pngPath.c_str(), 6, cancelledMetadata, cancelled.Token()) ==
                   ImageCodecStatus::Cancelled &&
               cancelledMetadata.rawWidth == 0 && cancelledMetadata.rawHeight == 0 && cancelledMetadata.pixels == 0 &&
               cancelledMetadata.encodedBytes == 0,
           "import-codec.precancel-probe-clears-metadata");
    DecodedImage cancelledDecode{};
    Expect(ImportImageFile(pngPath.c_str(), 6, cancelledDecode, cancelled.Token()) == ImageCodecStatus::Cancelled &&
               DecodedImageIsEmpty(cancelledDecode),
           "import-codec.precancel-decode-clears-output");

    constexpr std::array<std::uint8_t, 8> truncatedSignature{0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
    const std::wstring truncatedPath = scratch.Child(L"truncated.png");
    Expect(WriteBytes(truncatedPath, truncatedSignature), "import-codec.truncated-written");
    ImageImportMetadata truncated{9, 8, 72, 44};
    Expect(ProbeImportImageFile(truncatedPath.c_str(), 6, truncated) == ImageCodecStatus::Unreadable &&
               truncated.rawWidth == 0 && truncated.rawHeight == 0 && truncated.pixels == 0 &&
               truncated.encodedBytes == 0,
           "import-codec.truncated-unreadable-clears-metadata");
    const std::wstring missingPath = scratch.Child(L"missing.png");
    ImageImportMetadata missing{9, 8, 72, 44};
    Expect(ProbeImportImageFile(missingPath.c_str(), 6, missing) == ImageCodecStatus::Unreadable &&
               missing.rawWidth == 0 && missing.rawHeight == 0 && missing.pixels == 0 && missing.encodedBytes == 0,
           "import-codec.missing-unreadable-clears-metadata");
    const std::wstring unsupportedPath = scratch.Child(L"unsupported.bmp");
    Expect(WriteBytes(unsupportedPath, kUnsupportedBmp), "import-codec.unsupported-bmp-written");
    ImageImportMetadata unsupported{9, 8, 72, 44};
    Expect(ProbeImportImageFile(unsupportedPath.c_str(), 6, unsupported) == ImageCodecStatus::Unsupported &&
               unsupported.rawWidth == 0 && unsupported.rawHeight == 0 && unsupported.pixels == 0 &&
               unsupported.encodedBytes == 0,
           "import-codec.unsupported-container-clears-metadata");

    constexpr std::uint64_t kEncodedCeiling = kMaxImageEncodedBytes;
    const std::wstring exactHardCapPath = scratch.Child(L"exact-hard-cap.png");
    Expect(WriteBytes(exactHardCapPath, kProbePng) && SetFileLengthForProbeTest(exactHardCapPath, kEncodedCeiling),
           "import-codec.exact-hard-cap-sparse-file-created");
    ImageImportMetadata exactHardCap{};
    Expect(ProbeImportImageFile(exactHardCapPath.c_str(), 6, exactHardCap) == ImageCodecStatus::Ok &&
               exactHardCap.rawWidth == 2 && exactHardCap.rawHeight == 3 &&
               exactHardCap.encodedBytes == kEncodedCeiling,
           "import-codec.exact-hard-encoded-cap-accepted");
    const std::wstring overHardCapPath = scratch.Child(L"over-hard-cap.png");
    Expect(WriteBytes(overHardCapPath, kProbePng) && SetFileLengthForProbeTest(overHardCapPath, kEncodedCeiling + 1),
           "import-codec.over-hard-cap-sparse-file-created");
    ImageImportMetadata overHardCap{9, 8, 72, 44};
    Expect(ProbeImportImageFile(overHardCapPath.c_str(), 6, overHardCap) == ImageCodecStatus::TooLarge &&
               overHardCap.rawWidth == 0 && overHardCap.rawHeight == 0 && overHardCap.pixels == 0 &&
               overHardCap.encodedBytes == 0,
           "import-codec.over-hard-encoded-cap-refused-before-decoder");

    ImageImportMetadata growthBaseline{};
    Expect(ProbeImportImageFile(pngPath.c_str(), 6, growthBaseline) == ImageCodecStatus::Ok &&
               growthBaseline.encodedBytes == kProbePng.size(),
           "import-codec.growth-probe-baseline");
    Expect(RewriteProbeBytes(pngPath, kProbeJpeg), "import-codec.source-rewritten-larger-after-probe");
    DecodedImage growthDecode{};
    Expect(ImportImageFile(pngPath.c_str(), 6, growthDecode, {}, growthBaseline.encodedBytes) ==
                   ImageCodecStatus::TooLarge &&
               DecodedImageIsEmpty(growthDecode),
           "import-codec.actual-open-stream-growth-cap-refused-empty");
    Expect(DecodeProjectPngFile(pngPath.c_str(), false, 6, growthDecode, {},
                                static_cast<std::size_t>(growthBaseline.encodedBytes)) == ImageCodecStatus::TooLarge &&
               DecodedImageIsEmpty(growthDecode),
           "import-codec.project-opened-stream-growth-cap-refused-empty");
}

} // namespace

int RunImageCodecWorkTests()
{
    gFailures = 0;
    PremultipliedPngRoundTrip();
    SparseGrayBandRoundTrip();
    SparseBgraMultiBandRoundTrip();
    SparseJpegMultiBandWhiteMatte();
    PreCancelledCalls();
    PreCancelledFileEncodesPreserveFiles();
    LockedDestinationFailurePreservesFiles();
    ImageImportCodecProbeAndBudgetGuards();
    ImportTiffUsesFirstFrame();
    ClipboardPngMetadataProbe();
    return gFailures;
}
