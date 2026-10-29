#include "ClipboardDibWork.h"

#include "RasterWork.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <utility>

namespace
{
constexpr std::size_t kInfoHeaderBytes = 40;
constexpr std::size_t kV4HeaderBytes = 108;
constexpr std::size_t kV5HeaderBytes = 124;
constexpr std::uint32_t kBiRgb = 0;
constexpr std::uint32_t kBiBitfields = 3;
constexpr std::uint32_t kRedMask = 0x00FF0000u;
constexpr std::uint32_t kGreenMask = 0x0000FF00u;
constexpr std::uint32_t kBlueMask = 0x000000FFu;
constexpr std::uint32_t kAlphaMask = 0xFF000000u;
constexpr std::uint32_t kSrgb = 0x73524742u;
constexpr std::uint32_t kImagesIntent = 4;
constexpr std::uint64_t kMaxDibPixels = 100'000'000;

std::uint16_t ReadU16(std::span<const std::uint8_t> bytes, std::size_t offset) noexcept
{
    return static_cast<std::uint16_t>(static_cast<std::uint32_t>(bytes[offset]) |
                                      (static_cast<std::uint32_t>(bytes[offset + 1]) << 8));
}

std::uint32_t ReadU32(std::span<const std::uint8_t> bytes, std::size_t offset) noexcept
{
    return static_cast<std::uint32_t>(bytes[offset]) | (static_cast<std::uint32_t>(bytes[offset + 1]) << 8) |
           (static_cast<std::uint32_t>(bytes[offset + 2]) << 16) |
           (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
}

std::int32_t ReadI32(std::span<const std::uint8_t> bytes, std::size_t offset) noexcept
{
    const std::uint32_t value = ReadU32(bytes, offset);
    if (value <= static_cast<std::uint32_t>((std::numeric_limits<std::int32_t>::max)()))
    {
        return static_cast<std::int32_t>(value);
    }
    const std::int64_t signedValue = static_cast<std::int64_t>(value) - 0x1'0000'0000ll;
    return static_cast<std::int32_t>(signedValue);
}

void WriteU16(std::span<std::uint8_t> bytes, std::size_t offset, std::uint16_t value) noexcept
{
    bytes[offset] = static_cast<std::uint8_t>(value & 0xFFu);
    bytes[offset + 1] = static_cast<std::uint8_t>(value >> 8);
}

void WriteU32(std::span<std::uint8_t> bytes, std::size_t offset, std::uint32_t value) noexcept
{
    bytes[offset] = static_cast<std::uint8_t>(value & 0xFFu);
    bytes[offset + 1] = static_cast<std::uint8_t>((value >> 8) & 0xFFu);
    bytes[offset + 2] = static_cast<std::uint8_t>((value >> 16) & 0xFFu);
    bytes[offset + 3] = static_cast<std::uint8_t>(value >> 24);
}

struct DibLayout final
{
    int width = 0;
    int height = 0;
    std::uint64_t pixels = 0;
    std::size_t pixelOffset = 0;
    std::size_t stride = 0;
    std::size_t bytesPerPixel = 0;
    bool topDown = false;
    bool hasAlpha = false;
};

ClipboardDibStatus Inspect(std::span<const std::uint8_t> bytes, std::int64_t pixelBudget, DibLayout& layout,
                           const CancelToken& cancellation) noexcept
{
    layout = {};
    if (cancellation.IsCancelled())
    {
        return ClipboardDibStatus::Cancelled;
    }
    if (pixelBudget < 0 || !ClipboardDibEncodedSizeAllowed(bytes.size()))
    {
        return ClipboardDibStatus::TooLarge;
    }
    if (bytes.size() < kInfoHeaderBytes)
    {
        return ClipboardDibStatus::Invalid;
    }

    const std::uint32_t headerBytes = ReadU32(bytes, 0);
    const std::int32_t signedWidth = ReadI32(bytes, 4);
    const std::int32_t signedHeight = ReadI32(bytes, 8);
    const std::uint16_t planes = ReadU16(bytes, 12);
    const std::uint16_t bitCount = ReadU16(bytes, 14);
    const std::uint32_t compression = ReadU32(bytes, 16);
    const std::uint32_t colorsUsed = ReadU32(bytes, 32);
    if (headerBytes < kInfoHeaderBytes || headerBytes > bytes.size() || planes != 1 ||
        (bitCount != 24 && bitCount != 32) || (compression != kBiRgb && compression != kBiBitfields))
    {
        return ClipboardDibStatus::Invalid;
    }

    const std::int64_t width = signedWidth;
    const std::int64_t signedHeight64 = signedHeight;
    const std::int64_t height = signedHeight64 < 0 ? -signedHeight64 : signedHeight64;
    if (width < 1 || height < 1)
    {
        return ClipboardDibStatus::Invalid;
    }
    if (width > kMaxCanvasSide || height > kMaxCanvasSide)
    {
        return ClipboardDibStatus::TooLarge;
    }
    const std::uint64_t pixels = static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height);
    if (pixels > kMaxDibPixels || pixels > static_cast<std::uint64_t>(pixelBudget))
    {
        return ClipboardDibStatus::TooLarge;
    }

    std::uint64_t pixelOffset = headerBytes;
    std::uint32_t redMask = kRedMask;
    std::uint32_t greenMask = kGreenMask;
    std::uint32_t blueMask = kBlueMask;
    std::uint32_t alphaMask = 0;
    if (compression == kBiBitfields)
    {
        if (headerBytes >= kV4HeaderBytes)
        {
            redMask = ReadU32(bytes, 40);
            greenMask = ReadU32(bytes, 44);
            blueMask = ReadU32(bytes, 48);
            alphaMask = ReadU32(bytes, 52);
        }
        else
        {
            if (pixelOffset > bytes.size() || bytes.size() - static_cast<std::size_t>(pixelOffset) < 12)
            {
                return ClipboardDibStatus::Invalid;
            }
            redMask = ReadU32(bytes, static_cast<std::size_t>(pixelOffset));
            greenMask = ReadU32(bytes, static_cast<std::size_t>(pixelOffset) + 4);
            blueMask = ReadU32(bytes, static_cast<std::size_t>(pixelOffset) + 8);
            pixelOffset += 12;
        }
    }
    else if (headerBytes >= kV4HeaderBytes && bitCount == 32)
    {
        alphaMask = ReadU32(bytes, 52);
    }

    // Preserve the host's legacy rule: palette entries affect BI_RGB offsets only.
    if (compression == kBiRgb && colorsUsed != 0)
    {
        const std::uint64_t paletteBytes = static_cast<std::uint64_t>(colorsUsed) * 4u;
        if (paletteBytes > (std::numeric_limits<std::uint64_t>::max)() - pixelOffset)
        {
            return ClipboardDibStatus::Invalid;
        }
        pixelOffset += paletteBytes;
    }

    const std::uint64_t bytesPerPixel = bitCount / 8u;
    const std::uint64_t unalignedStride = static_cast<std::uint64_t>(width) * bytesPerPixel;
    if (unalignedStride > (std::numeric_limits<std::uint64_t>::max)() - 3u)
    {
        return ClipboardDibStatus::Invalid;
    }
    const std::uint64_t stride = (unalignedStride + 3u) & ~std::uint64_t{3};
    if (stride > (std::numeric_limits<std::uint64_t>::max)() / static_cast<std::uint64_t>(height))
    {
        return ClipboardDibStatus::Invalid;
    }
    const std::uint64_t imageBytes = stride * static_cast<std::uint64_t>(height);
    if (pixelOffset > bytes.size() || imageBytes > bytes.size() - static_cast<std::size_t>(pixelOffset))
    {
        return ClipboardDibStatus::Invalid;
    }
    if (bitCount == 32 && (redMask != kRedMask || greenMask != kGreenMask || blueMask != kBlueMask ||
                           (alphaMask != 0 && alphaMask != kAlphaMask)))
    {
        return ClipboardDibStatus::Invalid;
    }
    if (pixelOffset > (std::numeric_limits<std::size_t>::max)() || stride > (std::numeric_limits<std::size_t>::max)() ||
        bytesPerPixel > (std::numeric_limits<std::size_t>::max)())
    {
        return ClipboardDibStatus::Invalid;
    }

    layout.width = static_cast<int>(width);
    layout.height = static_cast<int>(height);
    layout.pixels = pixels;
    layout.pixelOffset = static_cast<std::size_t>(pixelOffset);
    layout.stride = static_cast<std::size_t>(stride);
    layout.bytesPerPixel = static_cast<std::size_t>(bytesPerPixel);
    layout.topDown = signedHeight < 0;
    layout.hasAlpha = bitCount == 32 && alphaMask != 0;
    return cancellation.IsCancelled() ? ClipboardDibStatus::Cancelled : ClipboardDibStatus::Ok;
}

void Unpremultiply(std::uint8_t* pixel) noexcept
{
    const int alpha = pixel[3];
    if (alpha == 0)
    {
        pixel[0] = 0;
        pixel[1] = 0;
        pixel[2] = 0;
        return;
    }
    for (std::size_t channel = 0; channel < 3; ++channel)
    {
        pixel[channel] =
            static_cast<std::uint8_t>(std::min(255, (static_cast<int>(pixel[channel]) * 255 + alpha / 2) / alpha));
    }
}
} // namespace

ClipboardDibStatus ProbeClipboardDib(std::span<const std::uint8_t> bytes, std::int64_t pixelBudget,
                                     ClipboardDibMetadata& metadata, CancelToken cancellation) noexcept
{
    metadata = {};
    DibLayout layout;
    const ClipboardDibStatus status = Inspect(bytes, pixelBudget, layout, cancellation);
    if (status == ClipboardDibStatus::Ok)
    {
        metadata = {layout.width, layout.height, layout.pixels};
    }
    return status;
}

ClipboardDibStatus DecodeClipboardDib(std::span<const std::uint8_t> bytes, std::int64_t pixelBudget, Raster& image,
                                      CancelToken cancellation)
{
    image = {};
    DibLayout layout;
    const ClipboardDibStatus inspected = Inspect(bytes, pixelBudget, layout, cancellation);
    if (inspected != ClipboardDibStatus::Ok)
    {
        return inspected;
    }

    const std::uint64_t outputBytes64 = layout.pixels * 4u;
    if (outputBytes64 > (std::numeric_limits<std::size_t>::max)())
    {
        return ClipboardDibStatus::TooLarge;
    }
    PixelBuffer output = PixelBuffer::TryAllocate(static_cast<std::size_t>(outputBytes64));
    if (!output)
    {
        return ClipboardDibStatus::OutOfMemory;
    }
    const std::size_t outputStride = static_cast<std::size_t>(layout.width) * 4u;
    for (int y = 0; y < layout.height; ++y)
    {
        // Cancellation is observed between rows; the scalar pixel loop for an active row is not interruptible.
        if (cancellation.IsCancelled())
        {
            return ClipboardDibStatus::Cancelled;
        }
        const int sourceY = layout.topDown ? y : layout.height - 1 - y;
        const std::uint8_t* source =
            bytes.data() + layout.pixelOffset + static_cast<std::size_t>(sourceY) * layout.stride;
        std::uint8_t* target = output.Data() + static_cast<std::size_t>(y) * outputStride;
        for (int x = 0; x < layout.width; ++x)
        {
            const std::uint8_t* input = source + static_cast<std::size_t>(x) * layout.bytesPerPixel;
            std::uint8_t* pixel = target + static_cast<std::size_t>(x) * 4u;
            const int alpha = layout.hasAlpha ? input[3] : 255;
            pixel[3] = static_cast<std::uint8_t>(alpha);
            pixel[0] = static_cast<std::uint8_t>((static_cast<int>(input[0]) * alpha + 127) / 255);
            pixel[1] = static_cast<std::uint8_t>((static_cast<int>(input[1]) * alpha + 127) / 255);
            pixel[2] = static_cast<std::uint8_t>((static_cast<int>(input[2]) * alpha + 127) / 255);
        }
    }
    if (cancellation.IsCancelled())
    {
        return ClipboardDibStatus::Cancelled;
    }
    Raster result = Raster::Adopt(layout.width, layout.height, RasterFormat::Bgra8888, std::move(output));
    if (!result.HasStorage())
    {
        return ClipboardDibStatus::Invalid;
    }
    if (cancellation.IsCancelled())
    {
        return ClipboardDibStatus::Cancelled;
    }
    image = std::move(result);
    return ClipboardDibStatus::Ok;
}

ClipboardDibStatus EncodeClipboardDibV5(const Raster& image, std::vector<std::uint8_t>& bytes, CancelToken cancellation)
{
    bytes.clear();
    if (cancellation.IsCancelled())
    {
        return ClipboardDibStatus::Cancelled;
    }
    const int width = image.Width();
    const int height = image.Height();
    if (!image.HasStorage() || image.Format() != RasterFormat::Bgra8888 || width < 1 || height < 1)
    {
        return ClipboardDibStatus::Invalid;
    }
    if (width > kMaxCanvasSide || height > kMaxCanvasSide)
    {
        return ClipboardDibStatus::TooLarge;
    }
    const std::uint64_t pixels = static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height);
    if (pixels > kMaxDibPixels)
    {
        return ClipboardDibStatus::TooLarge;
    }
    const std::uint64_t rowBytes = static_cast<std::uint64_t>(width) * 4u;
    if (pixels > ((std::numeric_limits<std::uint64_t>::max)() - kV5HeaderBytes) / 4u)
    {
        return ClipboardDibStatus::TooLarge;
    }
    const std::uint64_t totalBytes = kV5HeaderBytes + pixels * 4u;
    if (!ClipboardDibEncodedSizeAllowed(totalBytes) || totalBytes > (std::numeric_limits<std::size_t>::max)() ||
        rowBytes > (std::numeric_limits<std::size_t>::max)())
    {
        return ClipboardDibStatus::TooLarge;
    }

    bytes.resize(static_cast<std::size_t>(totalBytes));
    std::span<std::uint8_t> output{bytes};
    WriteU32(output, 0, static_cast<std::uint32_t>(kV5HeaderBytes));
    WriteU32(output, 4, static_cast<std::uint32_t>(width));
    WriteU32(output, 8, static_cast<std::uint32_t>(height));
    WriteU16(output, 12, 1);
    WriteU16(output, 14, 32);
    WriteU32(output, 16, kBiBitfields);
    WriteU32(output, 20, static_cast<std::uint32_t>(pixels * 4u));
    WriteU32(output, 40, kRedMask);
    WriteU32(output, 44, kGreenMask);
    WriteU32(output, 48, kBlueMask);
    WriteU32(output, 52, kAlphaMask);
    WriteU32(output, 56, kSrgb);
    WriteU32(output, 108, kImagesIntent);

    const std::size_t stride = static_cast<std::size_t>(rowBytes);
    for (int y = 0; y < height; ++y)
    {
        // ReadRasterRegionForWork also checks within its row copy. The conversion of an active row is not
        // interruptible.
        if (cancellation.IsCancelled())
        {
            bytes.clear();
            return ClipboardDibStatus::Cancelled;
        }
        const int dibY = height - 1 - y;
        std::span<std::uint8_t> row{bytes.data() + kV5HeaderBytes + static_cast<std::size_t>(dibY) * stride, stride};
        if (!ReadRasterRegionForWork(image, {0, y, width, 1}, row, stride, 0, cancellation))
        {
            bytes.clear();
            return cancellation.IsCancelled() ? ClipboardDibStatus::Cancelled : ClipboardDibStatus::Invalid;
        }
        for (int x = 0; x < width; ++x)
        {
            Unpremultiply(row.data() + static_cast<std::size_t>(x) * 4u);
        }
    }
    if (cancellation.IsCancelled())
    {
        bytes.clear();
        return ClipboardDibStatus::Cancelled;
    }
    return ClipboardDibStatus::Ok;
}
