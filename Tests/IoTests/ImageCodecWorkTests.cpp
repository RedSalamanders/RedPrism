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
    return gFailures;
}
