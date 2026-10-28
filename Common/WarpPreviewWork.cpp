#include "WarpPreviewWork.h"

#include "SharedPublicationMemory.h"

#include "DeferredRelease.h"
#include "PublicationGraphAllocator.h"
#include "RasterWork.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace
{
bool Add(std::uint64_t& total, std::uint64_t count, std::uint64_t unit = 1) noexcept
{
    if ((unit && count > UINT64_MAX / unit) || count * unit > UINT64_MAX - total)
        return false;
    total += count * unit;
    return true;
}

#pragma warning(push)
#pragma warning(disable : 4324)
// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027)
struct OwnedWarpPreview final : DeferredReleaseNode
{
    PreparedWarpPreview value;
    OwnedWarpPreview() noexcept
    {
        destroy = [](DeferredReleaseNode* node) noexcept
        { const std::unique_ptr<OwnedWarpPreview> owned(static_cast<OwnedWarpPreview*>(node)); };
    }
};
#pragma warning(pop)

#pragma warning(pop)

struct RetireWarpPreview final
{
    void operator()(OwnedWarpPreview* node) const noexcept
    {
        if (node && !TryDeferRelease(*node, DeferredReleasePolicy::AlwaysOnOwner))
        {
            const std::unique_ptr<OwnedWarpPreview> owned(node);
        }
    }
};
} // namespace

std::uint64_t WarpPreviewMetadataMemoryBytes(int width, int height) noexcept
{
    if (width <= 0 || height <= 0 || width > kMaxCanvasSide || height > kMaxCanvasSide ||
        static_cast<std::int64_t>(width) * height > kMaxLayerPixels)
        return 0;
    auto bytes = std::uint64_t{1024 * 1024};
    const auto columns = (static_cast<std::uint64_t>(width) + 255) / 256;
    const auto rows = (static_cast<std::uint64_t>(height) + 255) / 256;
    return Add(bytes, columns * rows, 2 * sizeof(RasterTile) + sizeof(std::int32_t)) ? bytes : 0;
}

std::shared_ptr<const PreparedWarpPreview> PrepareWarpPreviewForWork(
    WarpStroke& stroke, std::thread::id ownerThread, CancelToken cancellation,
    std::shared_ptr<const SharedPublicationMemory> memory)
{
    if (ownerThread == std::thread::id{} || ownerThread == std::this_thread::get_id() ||
        ownerThread != stroke.WorkOwner() || !memory || memory->IsBound() || cancellation.IsCancelled())
        return {};
    const auto* source = stroke.SourceViewForWork();
    if (!source || !WarpPreviewMetadataMemoryBytes(source->width, source->height))
        return {};
    auto owned = std::make_unique<OwnedWarpPreview>();
    auto& output = owned->value;
    output.memory = memory;
    output.ownerThread = ownerThread;
    output.source = *source;
    output.tiles = stroke.AllTouchedTilesForWork(cancellation);
    if (!stroke.IsValid() || cancellation.IsCancelled())
        return {};
    output._columns = (source->width + 255) / 256;
    output._indices.assign(static_cast<std::size_t>(output._columns) * ((source->height + 255) / 256), -1);
    auto bytes = static_cast<std::uint64_t>(sizeof(OwnedWarpPreview)) + 65536;
    if (!Add(bytes, SharedPublicationMemory::OwnershipAllowance()) ||
        !Add(bytes, output.tiles.capacity(), sizeof(RasterTile)) ||
        !Add(bytes, output._indices.capacity(), sizeof(std::int32_t)) ||
        (source->image.HasStorage() &&
         (!source->image.RetirementMemoryBytes() || !Add(bytes, source->image.RetirementMemoryBytes()))))
        return {};
    for (std::size_t i = 0; i < output.tiles.size(); ++i)
    {
        const auto& tile = output.tiles[i];
        if (cancellation.IsCancelled() || !tile.pixels.HasStorage() || tile.pixels.Format() != RasterFormat::Bgra8888 ||
            !tile.pixels.RetirementMemoryBytes() || tile.x < 0 || tile.y < 0 || tile.x >= source->width ||
            tile.y >= source->height || tile.x % 256 || tile.y % 256 || tile.sourceX || tile.sourceY ||
            tile.Width() != std::min(256, source->width - tile.x) ||
            tile.Height() != std::min(256, source->height - tile.y) || tile.pixels.Width() != tile.Width() ||
            tile.pixels.Height() != tile.Height() ||
            i > static_cast<std::size_t>((std::numeric_limits<std::int32_t>::max)()) ||
            !Add(bytes, tile.pixels.RetirementMemoryBytes()))
            return {};
        auto& entry = output._indices[static_cast<std::size_t>(tile.y / 256) * output._columns + tile.x / 256];
        if (entry >= 0)
            return {};
        entry = static_cast<std::int32_t>(i);
    }
    output.ownershipBytes = bytes;
    output._complete = true;
    owned->bytes = bytes;
    auto shared = std::shared_ptr<OwnedWarpPreview>(owned.release(), RetireWarpPreview{},
                                                    PublicationGraphAllocator<OwnedWarpPreview>{memory});
    return std::shared_ptr<const PreparedWarpPreview>(shared, &shared->value);
}

WarpPreviewReader::WarpPreviewReader(std::shared_ptr<const PreparedWarpPreview> preview) noexcept
    : _preview(std::move(preview)),
      _placed(_preview ? _preview->source.image : Raster{}, _preview ? _preview->source.placement : LayerTransform{})
{
}

bool WarpPreviewReader::CanRead(CancelToken cancellation) const noexcept
{
    return _preview && _preview->_complete && _preview->memory && _preview->ownerThread != std::thread::id{} &&
           _preview->ownerThread != std::this_thread::get_id() && !cancellation.IsCancelled() &&
           WarpPreviewMetadataMemoryBytes(_preview->source.width, _preview->source.height);
}

bool WarpPreviewReader::Read(GridRect region, std::span<std::uint8_t> output, std::size_t stride,
                             CancelToken cancellation) noexcept
{
    if (!CanRead(cancellation) || region.Empty() || region.x < 0 || region.y < 0 || region.x > _preview->source.width ||
        region.y > _preview->source.height || region.width > _preview->source.width - region.x ||
        region.height > _preview->source.height - region.y)
        return false;
    const auto rowBytes = static_cast<std::size_t>(region.width) * 4;
    if (stride < rowBytes || output.size() < rowBytes ||
        static_cast<std::size_t>(region.height - 1) > (output.size() - rowBytes) / stride)
        return false;
    // Work in tile rectangles, so an unchanged sparse source does not reload a full-width row's windows.
    // The immutable producer-built directory gives O(1) lookup, with no per-reader hash/grid allocation.
    for (int y = region.y; y < region.y + region.height;)
    {
        const int height = std::min(region.y + region.height - y, 256 - y % 256);
        for (int x = region.x; x < region.x + region.width;)
        {
            if (cancellation.IsCancelled())
                return false;
            const int width = std::min(region.x + region.width - x, 256 - x % 256);
            const int originX = x / 256 * 256, originY = y / 256 * 256;
            const auto entry =
                _preview->_indices[static_cast<std::size_t>(originY / 256) * _preview->_columns + originX / 256];
            const auto offset =
                static_cast<std::size_t>(y - region.y) * stride + static_cast<std::size_t>(x - region.x) * 4;
            auto destination = output.subspan(offset);
            if (entry >= 0)
            {
                const auto& tile = _preview->tiles[static_cast<std::size_t>(entry)];
                if (!ReadRasterRegionForWork(tile.pixels, {x - originX, y - originY, width, height}, destination,
                                             stride, 0, cancellation))
                    return false;
            }
            else if (!_placed.Read({x, y, width, height}, destination, stride, cancellation))
                return false;
            x += width;
        }
        y += height;
    }
    return !cancellation.IsCancelled();
}

bool WarpPreviewReader::Sample(Vec2 point, std::array<double, 4>& output, CancelToken cancellation) noexcept
{
    if (!CanRead(cancellation) || !std::isfinite(point.x) || !std::isfinite(point.y) || !(point.x >= 0) ||
        !(point.y >= 0) || !(point.x < _preview->source.width) || !(point.y < _preview->source.height))
        return false;
    const double sx = std::clamp(point.x - 0.5, 0.0, static_cast<double>(_preview->source.width - 1));
    const double sy = std::clamp(point.y - 0.5, 0.0, static_cast<double>(_preview->source.height - 1));
    const double fx = std::floor(sx), fy = std::floor(sy);
    const double tx = sx - fx, ty = sy - fy;
    const int ix = static_cast<int>(fx), iy = static_cast<int>(fy);
    const int right = std::min(ix + 1, _preview->source.width - 1);
    const int bottom = std::min(iy + 1, _preview->source.height - 1);
    std::array<std::array<std::uint8_t, 4>, 4> taps{};
    const std::array<std::pair<int, int>, 4> positions{{{ix, iy}, {right, iy}, {ix, bottom}, {right, bottom}}};
    for (std::size_t tap = 0; tap < taps.size(); ++tap)
        if (!Read({positions[tap].first, positions[tap].second, 1, 1}, taps[tap], 4, cancellation))
            return false;
    std::array<double, 4> result{};
    for (std::size_t channel = 0; channel < 4; ++channel)
    {
        const double top = taps[0][channel] + (taps[1][channel] - taps[0][channel]) * tx;
        const double lower = taps[2][channel] + (taps[3][channel] - taps[2][channel]) * tx;
        result[channel] = top + (lower - top) * ty;
    }
    if (cancellation.IsCancelled())
        return false;
    output = result;
    return true;
}

std::uint64_t WarpPreviewReader::RetirementMemoryBytes() const noexcept
{
    const auto source = _placed.RetirementMemoryBytes();
    auto bytes = static_cast<std::uint64_t>(sizeof(*this)) + 1024;
    if (!source || !Add(bytes, source) || (_preview && !Add(bytes, _preview->ownershipBytes)))
        return 0;
    return bytes;
}
