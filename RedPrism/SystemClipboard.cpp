#include "SystemClipboard.h"

#include "ImageCodecs.h"

#include <algorithm>
#include <climits>
#include <cstring>
#include <new>
#include <vector>

namespace
{
[[nodiscard]] UINT PngFormat() noexcept
{
    static const UINT format = RegisterClipboardFormatW(L"PNG");
    return format;
}

// Opens the clipboard for the call's lifetime; a busy clipboard (another app holds it) is retried briefly.
class ClipboardScope
{
  public:
    explicit ClipboardScope(HWND owner) noexcept
    {
        for (int attempt = 0; attempt < 5 && !_open; ++attempt)
        {
            if (attempt > 0)
            {
                Sleep(10);
            }
            _open = OpenClipboard(owner) != FALSE;
        }
    }
    ~ClipboardScope()
    {
        if (_open)
        {
            (void)CloseClipboard();
        }
    }
    ClipboardScope(const ClipboardScope&) = delete;
    ClipboardScope& operator=(const ClipboardScope&) = delete;
    [[nodiscard]] bool IsOpen() const noexcept
    {
        return _open;
    }

  private:
    bool _open = false;
};

// Copies bytes into a movable global block for SetClipboardData (the clipboard owns it once placed).
[[nodiscard]] HGLOBAL GlobalCopy(const void* bytes, std::size_t size) noexcept
{
    HGLOBAL handle = GlobalAlloc(GMEM_MOVEABLE, size);
    if (handle == nullptr)
    {
        return nullptr;
    }
    void* target = GlobalLock(handle);
    if (target == nullptr)
    {
        (void)GlobalFree(handle);
        return nullptr;
    }
    std::memcpy(target, bytes, size);
    (void)GlobalUnlock(handle);
    return handle;
}

// A 32-bit BGRA DIB (V5 header, bitfields, bottom-up) with straight alpha, the form other editors write and read.
[[nodiscard]] bool MakeDibV5(const Raster& image, std::vector<std::uint8_t>& out) noexcept
{
    const int width = image.Width();
    const int height = image.Height();
    if (width < 1 || height < 1)
    {
        return false;
    }
    const std::size_t rowBytes = static_cast<std::size_t>(width) * 4u;
    const std::size_t pixelBytes = rowBytes * static_cast<std::size_t>(height);
    std::vector<std::uint8_t> pixels;
    try
    {
        pixels.resize(pixelBytes);
        out.resize(sizeof(BITMAPV5HEADER) + pixelBytes);
    }
    catch (const std::bad_alloc&)
    {
        return false;
    }
    image.ReadRegion(0, 0, width, height, pixels.data(), width * 4, 0);
    BITMAPV5HEADER header{};
    header.bV5Size = sizeof(BITMAPV5HEADER);
    header.bV5Width = width;
    header.bV5Height = height; // bottom-up
    header.bV5Planes = 1;
    header.bV5BitCount = 32;
    header.bV5Compression = BI_BITFIELDS;
    header.bV5SizeImage = static_cast<DWORD>(pixelBytes);
    header.bV5RedMask = 0x00FF0000u;
    header.bV5GreenMask = 0x0000FF00u;
    header.bV5BlueMask = 0x000000FFu;
    header.bV5AlphaMask = 0xFF000000u;
    header.bV5CSType = LCS_sRGB;
    header.bV5Intent = LCS_GM_IMAGES;
    std::memcpy(out.data(), &header, sizeof(header));
    std::uint8_t* rows = out.data() + sizeof(BITMAPV5HEADER);
    for (int y = 0; y < height; ++y)
    {
        const std::uint8_t* source = pixels.data() + static_cast<std::size_t>(y) * rowBytes;
        std::uint8_t* target = rows + static_cast<std::size_t>(height - 1 - y) * rowBytes;
        for (int x = 0; x < width; ++x)
        {
            const std::uint8_t* in = source + static_cast<std::size_t>(x) * 4u;
            std::uint8_t* px = target + static_cast<std::size_t>(x) * 4u;
            const int alpha = in[3];
            if (alpha == 0)
            {
                px[0] = px[1] = px[2] = px[3] = 0;
                continue;
            }
            // Premultiplied to straight, rounded.
            px[0] = static_cast<std::uint8_t>(std::min(255, (in[0] * 255 + alpha / 2) / alpha));
            px[1] = static_cast<std::uint8_t>(std::min(255, (in[1] * 255 + alpha / 2) / alpha));
            px[2] = static_cast<std::uint8_t>(std::min(255, (in[2] * 255 + alpha / 2) / alpha));
            px[3] = static_cast<std::uint8_t>(alpha);
        }
    }
    return true;
}

// A DIB (V5 with an alpha mask, or any 24/32-bit header without one) as premultiplied BGRA.
[[nodiscard]] bool DecodeDib(const std::uint8_t* bytes, std::size_t size, std::int64_t pixelBudget,
                             Raster& image) noexcept
{
    if (bytes == nullptr || size < sizeof(BITMAPINFOHEADER))
    {
        return false;
    }
    BITMAPINFOHEADER header{};
    std::memcpy(&header, bytes, sizeof(header));
    if (header.biSize < sizeof(BITMAPINFOHEADER) || header.biSize > size || header.biPlanes != 1 ||
        (header.biBitCount != 32 && header.biBitCount != 24) ||
        (header.biCompression != BI_RGB && header.biCompression != BI_BITFIELDS))
    {
        return false;
    }
    const bool topDown = header.biHeight < 0;
    const std::int64_t width = header.biWidth;
    const std::int64_t height = topDown ? -static_cast<std::int64_t>(header.biHeight) : header.biHeight;
    if (width < 1 || height < 1 || width > kMaxCanvasSide || height > kMaxCanvasSide || width * height > pixelBudget)
    {
        return false;
    }
    std::uint32_t redMask = 0x00FF0000u;
    std::uint32_t greenMask = 0x0000FF00u;
    std::uint32_t blueMask = 0x000000FFu;
    std::uint32_t alphaMask = 0;
    std::size_t pixelOffset = header.biSize;
    if (header.biCompression == BI_BITFIELDS)
    {
        if (header.biSize >= sizeof(BITMAPV4HEADER))
        {
            BITMAPV4HEADER v4{};
            std::memcpy(&v4, bytes, sizeof(v4));
            redMask = v4.bV4RedMask;
            greenMask = v4.bV4GreenMask;
            blueMask = v4.bV4BlueMask;
            alphaMask = v4.bV4AlphaMask;
        }
        else
        {
            if (size < header.biSize + 12)
            {
                return false;
            }
            std::memcpy(&redMask, bytes + header.biSize, 4);
            std::memcpy(&greenMask, bytes + header.biSize + 4, 4);
            std::memcpy(&blueMask, bytes + header.biSize + 8, 4);
            pixelOffset += 12;
        }
    }
    else if (header.biSize >= sizeof(BITMAPV4HEADER) && header.biBitCount == 32)
    {
        // Some writers leave the alpha mask set with BI_RGB; honor it, otherwise the fourth byte is padding.
        BITMAPV4HEADER v4{};
        std::memcpy(&v4, bytes, sizeof(v4));
        alphaMask = v4.bV4AlphaMask;
    }
    // A palette (biClrUsed entries) sits before the pixels for BI_RGB headers that carry one; 24/32-bit images
    // rarely do, but the offset must skip it.
    if (header.biCompression == BI_RGB && header.biClrUsed > 0)
    {
        pixelOffset += static_cast<std::size_t>(header.biClrUsed) * sizeof(RGBQUAD);
    }
    const std::size_t bytesPerPixel = header.biBitCount / 8u;
    const std::size_t stride = ((static_cast<std::size_t>(width) * bytesPerPixel + 3u) / 4u) * 4u;
    if (size < pixelOffset + stride * static_cast<std::size_t>(height))
    {
        return false;
    }
    if (header.biBitCount == 32 && (redMask != 0x00FF0000u || greenMask != 0x0000FF00u || blueMask != 0x000000FFu ||
                                    (alphaMask != 0 && alphaMask != 0xFF000000u)))
    {
        return false;
    }
    std::vector<std::uint8_t> out;
    try
    {
        out.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u);
    }
    catch (const std::bad_alloc&)
    {
        return false;
    }
    const std::uint8_t* rows = bytes + pixelOffset;
    for (std::int64_t y = 0; y < height; ++y)
    {
        const std::int64_t sourceRow = topDown ? y : height - 1 - y;
        const std::uint8_t* source = rows + static_cast<std::size_t>(sourceRow) * stride;
        std::uint8_t* target = out.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(width) * 4u;
        for (std::int64_t x = 0; x < width; ++x)
        {
            const std::uint8_t* in = source + static_cast<std::size_t>(x) * bytesPerPixel;
            std::uint8_t* px = target + static_cast<std::size_t>(x) * 4u;
            const int alpha = (bytesPerPixel == 4 && alphaMask != 0) ? in[3] : 255;
            // Straight to premultiplied.
            px[0] = static_cast<std::uint8_t>((in[0] * alpha + 127) / 255);
            px[1] = static_cast<std::uint8_t>((in[1] * alpha + 127) / 255);
            px[2] = static_cast<std::uint8_t>((in[2] * alpha + 127) / 255);
            px[3] = static_cast<std::uint8_t>(alpha);
        }
    }
    image =
        Raster::Make(static_cast<int>(width), static_cast<int>(height), RasterFormat::Bgra8888, out.data(), out.size());
    return image.HasStorage();
}
} // namespace

namespace SystemClipboard
{
bool WriteImage(HWND owner, const Raster& image) noexcept
{
    if (!image.HasStorage() || image.Format() != RasterFormat::Bgra8888)
    {
        return false;
    }
    std::vector<std::uint8_t> png;
    if (EncodePng(image, 72.0, png) != ImageCodecStatus::Ok || png.empty())
    {
        return false;
    }
    std::vector<std::uint8_t> dib;
    const bool hasDib = MakeDibV5(image, dib);
    const ClipboardScope scope(owner);
    if (!scope.IsOpen() || !EmptyClipboard())
    {
        return false;
    }
    bool placed = false;
    if (HGLOBAL handle = GlobalCopy(png.data(), png.size()); handle != nullptr)
    {
        if (SetClipboardData(PngFormat(), handle) != nullptr)
        {
            placed = true;
        }
        else
        {
            (void)GlobalFree(handle);
        }
    }
    if (hasDib)
    {
        if (HGLOBAL handle = GlobalCopy(dib.data(), dib.size()); handle != nullptr)
        {
            if (SetClipboardData(CF_DIBV5, handle) != nullptr)
            {
                placed = true;
            }
            else
            {
                (void)GlobalFree(handle);
            }
        }
    }
    return placed;
}

bool HasImage() noexcept
{
    return IsClipboardFormatAvailable(PngFormat()) != FALSE || IsClipboardFormatAvailable(CF_DIBV5) != FALSE ||
           IsClipboardFormatAvailable(CF_DIB) != FALSE;
}

bool ReadImage(HWND owner, std::int64_t pixelBudget, Raster& image) noexcept
{
    const ClipboardScope scope(owner);
    if (!scope.IsOpen())
    {
        return false;
    }
    const auto locked = [](UINT format, const std::uint8_t*& bytes, std::size_t& size) -> HGLOBAL
    {
        HGLOBAL handle = GetClipboardData(format);
        if (handle == nullptr)
        {
            return nullptr;
        }
        bytes = static_cast<const std::uint8_t*>(GlobalLock(handle));
        if (bytes == nullptr)
        {
            return nullptr;
        }
        size = GlobalSize(handle);
        return handle;
    };
    const std::uint8_t* bytes = nullptr;
    std::size_t size = 0;
    if (HGLOBAL handle = locked(PngFormat(), bytes, size); handle != nullptr)
    {
        DecodedImage decoded;
        const bool ok =
            size > 0 && pixelBudget > 0 &&
            DecodeProjectPng(bytes, size, false, static_cast<int>(std::min<std::int64_t>(pixelBudget, INT_MAX)),
                             decoded) == ImageCodecStatus::Ok &&
            decoded.image.HasStorage();
        (void)GlobalUnlock(handle);
        if (ok)
        {
            image = decoded.image;
            return true;
        }
    }
    for (const UINT format : {static_cast<UINT>(CF_DIBV5), static_cast<UINT>(CF_DIB)})
    {
        bytes = nullptr;
        size = 0;
        if (HGLOBAL handle = locked(format, bytes, size); handle != nullptr)
        {
            const bool ok = DecodeDib(bytes, size, pixelBudget, image);
            (void)GlobalUnlock(handle);
            if (ok)
            {
                return true;
            }
        }
    }
    return false;
}

DWORD SequenceNumber() noexcept
{
    return GetClipboardSequenceNumber();
}
} // namespace SystemClipboard
