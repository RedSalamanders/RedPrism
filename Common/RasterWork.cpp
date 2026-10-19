#include "RasterWork.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <memory>
#include <utility>
#include <vector>

namespace
{
// Coordinates use 64 bits until clipped into a valid image, so a request near either int boundary cannot wrap.
struct Bounds final
{
    std::int64_t x;
    std::int64_t y;
    std::int64_t right;
    std::int64_t bottom;
    [[nodiscard]] bool Empty() const noexcept
    {
        return right <= x || bottom <= y;
    }
    [[nodiscard]] Bounds Intersect(Bounds other) const noexcept
    {
        return {std::max(x, other.x), std::max(y, other.y), std::min(right, other.right),
                std::min(bottom, other.bottom)};
    }
};

Bounds Rectangle(std::int64_t x, std::int64_t y, int width, int height) noexcept
{
    return {x, y, x + width, y + height};
}

// A single copy/fill never spans more than 64 KiB between checks, including a maximum-width BGRA row.
bool Transfer(std::uint8_t* destination, const std::uint8_t* source, std::size_t bytes, std::uint8_t fill,
              const CancelToken& cancellation) noexcept
{
    constexpr std::size_t kChunkBytes = 64 * 1024;
    for (std::size_t offset = 0; offset < bytes;)
    {
        if (cancellation.IsCancelled())
        {
            return false;
        }
        const std::size_t count = std::min(kChunkBytes, bytes - offset);
        if (source != nullptr)
        {
            std::memcpy(destination + offset, source + offset, count);
        }
        else
        {
            std::memset(destination + offset, fill, count);
        }
        offset += count;
    }
    return !cancellation.IsCancelled();
}

struct Frame final
{
    Raster image;
    Bounds request{};
    std::uint8_t* output = nullptr;
    std::uint8_t outsideFill = 0;
    unsigned int stage = 0;
    std::size_t tile = 0;
};

std::uint8_t* Destination(const Frame& frame, Bounds part, std::size_t stride, std::size_t channels) noexcept
{
    return frame.output + static_cast<std::size_t>(part.y - frame.request.y) * stride +
           static_cast<std::size_t>(part.x - frame.request.x) * channels;
}

bool FillRows(const Frame& frame, Bounds part, std::size_t stride, std::size_t channels, std::uint8_t fill,
              const CancelToken& cancellation) noexcept
{
    if (part.Empty())
    {
        return true;
    }
    std::uint8_t* firstRow = Destination(frame, part, stride, channels);
    const std::size_t bytes = static_cast<std::size_t>(part.right - part.x) * channels;
    for (std::int64_t y = part.y; y < part.bottom; ++y)
    {
        std::uint8_t* row = firstRow + static_cast<std::size_t>(y - part.y) * stride;
        if (!Transfer(row, nullptr, bytes, fill, cancellation))
        {
            return false;
        }
    }
    return true;
}
} // namespace

bool ReadRasterRegionForWork(const Raster& image, GridRect region, std::span<std::uint8_t> output,
                             std::size_t outputStride, std::uint8_t outsideFill, CancelToken cancellation) noexcept
{
    if (!image.HasStorage() || region.width <= 0 || region.height <= 0 || region.width > kMaxCanvasSide ||
        region.height > kMaxCanvasSide || cancellation.IsCancelled())
    {
        return false;
    }
    const std::size_t channels = static_cast<std::size_t>(image.Channels());
    const std::size_t rowBytes = static_cast<std::size_t>(region.width) * channels;
    if (outputStride < rowBytes || output.size() < rowBytes ||
        static_cast<std::size_t>(region.height - 1) > (output.size() - rowBytes) / outputStride)
    {
        return false;
    }
    std::array<Frame, kRasterWorkDepthLimit> frames{};
    frames[0] = {image, Rectangle(region.x, region.y, region.width, region.height), output.data(), outsideFill};
    std::size_t count = 1;
    while (count != 0)
    {
        if (cancellation.IsCancelled())
        {
            return false;
        }
        Frame& frame = frames[count - 1];
        const Bounds inside = frame.request.Intersect(Rectangle(0, 0, frame.image.Width(), frame.image.Height()));
        if (frame.stage == 0)
        {
            const bool whollyInside = inside.x == frame.request.x && inside.y == frame.request.y &&
                                      inside.right == frame.request.right && inside.bottom == frame.request.bottom;
            if (!whollyInside &&
                !FillRows(frame, frame.request, outputStride, channels, frame.outsideFill, cancellation))
            {
                return false;
            }
            if (inside.Empty())
            {
                frame = {};
                --count;
                continue;
            }
            if (frame.image.IsMaterialized())
            {
                // IsMaterialized publishes once and never reverts. Data cannot enter its assembly path here.
                const std::uint8_t* pixels = frame.image.Data();
                if (!inside.Empty())
                {
                    std::uint8_t* destination = Destination(frame, inside, outputStride, channels);
                    const std::size_t sourceStride = static_cast<std::size_t>(frame.image.Stride());
                    const std::uint8_t* source = pixels + static_cast<std::size_t>(inside.y) * sourceStride +
                                                 static_cast<std::size_t>(inside.x) * channels;
                    const std::size_t bytes = static_cast<std::size_t>(inside.right - inside.x) * channels;
                    for (std::int64_t y = inside.y; y < inside.bottom; ++y)
                    {
                        const std::size_t row = static_cast<std::size_t>(y - inside.y);
                        if (!Transfer(destination + row * outputStride, source + row * sourceStride, bytes, 0,
                                      cancellation))
                        {
                            return false;
                        }
                    }
                }
                frame = {};
                --count;
                continue;
            }
            if (!FillRows(frame, inside, outputStride, channels, frame.image.Fill(), cancellation))
            {
                return false;
            }
            frame.stage = 1;
            Raster base = frame.image.Base();
            const Bounds visible =
                inside.Intersect(Rectangle(frame.image.BaseX(), frame.image.BaseY(), base.Width(), base.Height()));
            if (base.HasStorage() && !visible.Empty())
            {
                if (count == frames.size())
                {
                    return false;
                }
                const Bounds request{visible.x - frame.image.BaseX(), visible.y - frame.image.BaseY(),
                                     visible.right - frame.image.BaseX(), visible.bottom - frame.image.BaseY()};
                frames[count++] = {std::move(base), request, Destination(frame, visible, outputStride, channels), 0};
                continue;
            }
        }
        const auto& tiles = frame.image.Tiles();
        if (frame.tile == tiles.size())
        {
            frame = {};
            --count;
            continue;
        }
        const RasterTile& tile = tiles[frame.tile++];
        const Bounds visible = inside.Intersect(Rectangle(tile.x, tile.y, tile.Width(), tile.Height()));
        if (!visible.Empty())
        {
            if (count == frames.size())
            {
                return false;
            }
            const Bounds request{visible.x - tile.x + tile.sourceX, visible.y - tile.y + tile.sourceY,
                                 visible.right - tile.x + tile.sourceX, visible.bottom - tile.y + tile.sourceY};
            frames[count++] = {tile.pixels, request, Destination(frame, visible, outputStride, channels), 0};
        }
    }
    return !cancellation.IsCancelled();
}

PixelBuffer CopyRasterPixelsForWork(const Raster& image, CancelToken cancellation) noexcept
{
    if (!image.HasStorage() || cancellation.IsCancelled())
    {
        return {};
    }
    const auto stride = static_cast<std::size_t>(image.Stride());
    PixelBuffer bytes = PixelBuffer::TryAllocate(stride * static_cast<std::size_t>(image.Height()));
    if (!bytes || !ReadRasterRegionForWork(image, {0, 0, image.Width(), image.Height()}, {bytes.Data(), bytes.Size()},
                                           stride, 0, cancellation))
    {
        return {};
    }
    return bytes;
}

Raster ContiguousRasterForWork(const Raster& image, CancelToken cancellation)
{
    if (!image.HasStorage() || cancellation.IsCancelled())
    {
        return {};
    }
    if (image.IsMaterialized())
    {
        return image;
    }
    PixelBuffer bytes = CopyRasterPixelsForWork(image, cancellation);
    return bytes && !cancellation.IsCancelled()
               ? Raster::Adopt(image.Width(), image.Height(), image.Format(), std::move(bytes))
               : Raster{};
}

Raster CropRasterForWork(const Raster& image, GridRect region, std::uint8_t outsideFill, CancelToken cancellation,
                         const std::shared_ptr<const void>& outputLifetime)
{
    if (!image.HasStorage() || region.width <= 0 || region.height <= 0 || region.width > kMaxCanvasSide ||
        region.height > kMaxCanvasSide || cancellation.IsCancelled())
    {
        return {};
    }
    const std::size_t stride = static_cast<std::size_t>(region.width) * static_cast<std::size_t>(image.Channels());
    PixelBuffer bytes = PixelBuffer::TryAllocate(stride * static_cast<std::size_t>(region.height));
    if (!bytes ||
        !ReadRasterRegionForWork(image, region, {bytes.Data(), bytes.Size()}, stride, outsideFill, cancellation))
    {
        return {};
    }
    return Raster::Adopt(region.width, region.height, image.Format(), std::move(bytes), outputLifetime);
}

Raster StretchedRasterForWork(const Raster& source, int width, int height, CancelToken cancellation)
{
    if (!source.HasStorage() || width < 1 || height < 1 || width > kMaxCanvasSide || height > kMaxCanvasSide ||
        cancellation.IsCancelled())
        return {};
    if (width == source.Width() && height == source.Height())
        return cancellation.IsCancelled() ? Raster{} : source;
    const int channels = source.Channels();
    const int stride = width * channels;
    PixelBuffer output = PixelBuffer::TryAllocate(static_cast<std::size_t>(stride) * height);
    if (!output)
        return {};
    std::vector<std::uint8_t> row(static_cast<std::size_t>(source.Stride()));
    const auto columns = std::make_unique_for_overwrite<int[]>(static_cast<std::size_t>(width));
    for (int x = 0; x < width; ++x)
    {
        if (x % 256 == 0 && cancellation.IsCancelled())
            return {};
        columns[static_cast<std::size_t>(x)] = static_cast<int>(static_cast<std::int64_t>(x) * source.Width() / width);
    }
    int loaded = -1;
    for (int y = 0; y < height; ++y)
    {
        if (cancellation.IsCancelled())
            return {};
        const int sourceY = static_cast<int>(static_cast<std::int64_t>(y) * source.Height() / height);
        if (sourceY != loaded)
        {
            if (!ReadRasterRegionForWork(source, {0, sourceY, source.Width(), 1}, row, row.size(), source.Fill(),
                                         cancellation))
                return {};
            loaded = sourceY;
        }
        std::uint8_t* const to = output.Data() + static_cast<std::size_t>(y) * stride;
        for (int x = 0; x < width; ++x)
        {
            if (x % 256 == 0 && cancellation.IsCancelled())
                return {};
            const std::uint8_t* const from =
                row.data() + static_cast<std::size_t>(columns[static_cast<std::size_t>(x)]) * channels;
            for (int channel = 0; channel < channels; ++channel)
                to[static_cast<std::size_t>(x) * channels + channel] = from[channel];
        }
    }
    return cancellation.IsCancelled() ? Raster{} : Raster::Adopt(width, height, source.Format(), std::move(output));
}

Raster ThumbnailRasterForWork(const Raster& source, int maxSide, CancelToken cancellation,
                              const std::shared_ptr<const void>& outputLifetime)
{
    if (cancellation.IsCancelled() || !source.HasStorage() || maxSide < 1)
    {
        return {};
    }
    const int width = source.Width();
    const int height = source.Height();
    const int largest = std::max(width, height);
    if (largest <= maxSide)
    {
        // Semantic mask thumbnails are eager dense pixels even when no downsampling is necessary. Copy
        // privately; later MaskEdgeBackground reads cannot change the shared mask's resident history charge.
        if (source.Format() == RasterFormat::Gray8 && !source.IsMaterialized())
        {
            auto pixels = CopyRasterPixelsForWork(source, cancellation);
            if (!pixels || cancellation.IsCancelled())
                return {};
            return Raster::Adopt(width, height, source.Format(), std::move(pixels), outputLifetime);
        }
        return cancellation.IsCancelled() ? Raster{} : source;
    }

    // Since the source side is bounded by kMaxCanvasSide and this branch has maxSide < largest, these products
    // fit in int, matching Raster::MakeThumbnail's round-to-nearest dimensions exactly.
    const int thumbWidth = std::max(1, (width * maxSide + largest / 2) / largest);
    const int thumbHeight = std::max(1, (height * maxSide + largest / 2) / largest);
    const int channels = source.Channels();
    const int dstStride = thumbWidth * channels;
    const std::size_t outputBytes = static_cast<std::size_t>(dstStride) * static_cast<std::size_t>(thumbHeight);
    PixelBuffer output = PixelBuffer::TryAllocate(outputBytes);
    if (!output)
    {
        return {};
    }

    // At most one source row coexists with the thumbnail, even for an unassembled sparse frame. Sampling
    // one row per output row scans its tile list at most maxSide times instead of once per output pixel, and
    // avoids a full-frame private copy just to produce a 96-pixel semantic mask thumbnail.
    const bool dense = source.IsMaterialized();
    const std::uint8_t* const input = dense ? source.Data() : nullptr;
    if (dense && input == nullptr)
        return {};
    const std::size_t sourceStride = static_cast<std::size_t>(source.Stride());
    std::vector<std::uint8_t> row;
    if (!dense)
        row.resize(sourceStride);
    int loaded = -1;
    constexpr int kPixelCheckInterval = 256;
    for (int y = 0; y < thumbHeight; ++y)
    {
        if (cancellation.IsCancelled())
        {
            return {};
        }
        const int sourceY = (y * height) / thumbHeight;
        if (!dense && loaded != sourceY)
        {
            if (!ReadRasterRegionForWork(source, {0, sourceY, width, 1}, row, sourceStride, source.Fill(),
                                         cancellation))
                return {};
            loaded = sourceY;
        }
        const std::uint8_t* const inputRow =
            dense ? input + static_cast<std::size_t>(sourceY) * sourceStride : row.data();
        std::uint8_t* const outputRow =
            output.Data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(dstStride);
        for (int x = 0; x < thumbWidth; ++x)
        {
            if (x % kPixelCheckInterval == 0 && cancellation.IsCancelled())
            {
                return {};
            }
            const int sourceX = (x * width) / thumbWidth;
            const std::uint8_t* const from =
                inputRow + static_cast<std::size_t>(sourceX) * static_cast<std::size_t>(channels);
            std::uint8_t* const to = outputRow + static_cast<std::size_t>(x) * static_cast<std::size_t>(channels);
            for (int channel = 0; channel < channels; ++channel)
            {
                to[channel] = from[channel];
            }
        }
    }
    if (cancellation.IsCancelled())
    {
        return {};
    }
    return Raster::Adopt(thumbWidth, thumbHeight, source.Format(), std::move(output), outputLifetime);
}

Raster CarriedMaskForWork(const Raster& mask, int oldWidth, int oldHeight, int x, int y, int width, int height,
                          std::uint8_t outside, CancelToken cancellation,
                          const std::shared_ptr<const void>& outputLifetime)
{
    if (cancellation.IsCancelled() || !mask.HasStorage() || oldWidth < 1 || oldHeight < 1 || width < 1 || height < 1 ||
        oldWidth > kMaxCanvasSide || oldHeight > kMaxCanvasSide || width > kMaxCanvasSide || height > kMaxCanvasSide)
    {
        return {};
    }

    // CarriedMask is Stretched(oldWidth, oldHeight), followed by Replacing at (x,y) and crop (0,0,width,height).
    // Read one source row per sampled row. A separately sized sparse mask may be much larger than its layer;
    // carrying it must not allocate its entire original frame just to sample a small destination grid.
    const bool dense = mask.IsMaterialized();
    const std::uint8_t* const input = dense ? mask.Data() : nullptr;
    if ((dense && input == nullptr) || cancellation.IsCancelled())
    {
        return {};
    }

    const int channels = mask.Channels();
    const int stride = width * channels;
    PixelBuffer output = PixelBuffer::TryAllocate(static_cast<std::size_t>(stride) * static_cast<std::size_t>(height));
    if (!output)
    {
        return {};
    }
    const std::size_t sourceStride = static_cast<std::size_t>(mask.Stride());
    std::vector<std::uint8_t> sourceBytes;
    if (!dense)
        sourceBytes.resize(sourceStride);
    int loaded = -1;
    const int sourceWidth = mask.Width();
    const int sourceHeight = mask.Height();
    constexpr int kPixelCheckInterval = 256;
    const auto columns = std::make_unique_for_overwrite<int[]>(static_cast<std::size_t>(width));
    for (int column = 0; column < width; ++column)
    {
        if (column % kPixelCheckInterval == 0 && cancellation.IsCancelled())
        {
            return {};
        }
        const std::int64_t stretchedX = static_cast<std::int64_t>(column) - x;
        columns[static_cast<std::size_t>(column)] =
            stretchedX >= 0 && stretchedX < oldWidth ? static_cast<int>(stretchedX * sourceWidth / oldWidth) : -1;
    }
    for (int destinationY = 0; destinationY < height; ++destinationY)
    {
        if (cancellation.IsCancelled())
        {
            return {};
        }
        const std::int64_t stretchedY = static_cast<std::int64_t>(destinationY) - y;
        std::uint8_t* const row =
            output.Data() + static_cast<std::size_t>(destinationY) * static_cast<std::size_t>(stride);
        if (stretchedY < 0 || stretchedY >= oldHeight)
        {
            if (!Transfer(row, nullptr, static_cast<std::size_t>(stride), outside, cancellation))
            {
                return {};
            }
            continue;
        }
        const int sourceY = static_cast<int>(stretchedY * sourceHeight / oldHeight);
        if (!dense && sourceY != loaded)
        {
            if (!ReadRasterRegionForWork(mask, {0, sourceY, sourceWidth, 1}, sourceBytes, sourceStride, mask.Fill(),
                                         cancellation))
                return {};
            loaded = sourceY;
        }
        const std::uint8_t* const sourceRow =
            dense ? input + static_cast<std::size_t>(sourceY) * sourceStride : sourceBytes.data();
        for (int destinationX = 0; destinationX < width; ++destinationX)
        {
            if (destinationX % kPixelCheckInterval == 0 && cancellation.IsCancelled())
            {
                return {};
            }
            const int sourceX = columns[static_cast<std::size_t>(destinationX)];
            std::uint8_t* const to = row + static_cast<std::size_t>(destinationX) * static_cast<std::size_t>(channels);
            for (int channel = 0; channel < channels; ++channel)
            {
                to[channel] = sourceX < 0
                                  ? outside
                                  : sourceRow[static_cast<std::size_t>(sourceX) * static_cast<std::size_t>(channels) +
                                              static_cast<std::size_t>(channel)];
            }
        }
    }
    if (cancellation.IsCancelled())
    {
        return {};
    }
    return Raster::Adopt(width, height, mask.Format(), std::move(output), outputLifetime);
}
