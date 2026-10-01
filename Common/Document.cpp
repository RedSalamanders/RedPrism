#include "Document.h"

#include "PipelineTrace.h"

#include "Adjust.h"
#include "CanvasSize.h"
#include "Crop.h"
#include "TransformEdit.h"
#include "Utf8.h"

#include <algorithm>
#include <cstring>
#include <exception>
#include <format>
#include <iterator>
#include <limits>
#include <new>
#include <rpc.h>
#include <string>
#include <string_view>
#include <windows.h>

#pragma comment(lib, "rpcrt4.lib")
#pragma comment(lib, "ole32.lib")

Guid Guid::Generate() noexcept
{
    GUID value{};
    const RPC_STATUS status = UuidCreate(&value);
    if (status != RPC_S_OK && status != RPC_S_UUID_LOCAL_ONLY)
    {
        std::terminate();
    }
    Guid id;
    std::memcpy(id.bytes.data(), &value, sizeof(value));
    return id;
}

std::wstring Guid::ToString() const
{
    GUID value{};
    std::memcpy(&value, bytes.data(), sizeof(value));
    return std::format(L"{:08X}-{:04X}-{:04X}-{:02X}{:02X}-{:02X}{:02X}{:02X}{:02X}{:02X}{:02X}", value.Data1,
                       value.Data2, value.Data3, value.Data4[0], value.Data4[1], value.Data4[2], value.Data4[3],
                       value.Data4[4], value.Data4[5], value.Data4[6], value.Data4[7]);
}

std::string Guid::ToUtf8() const
{
    return WideToUtf8(ToString());
}

std::optional<Guid> Guid::Parse(std::string_view text) noexcept
{
    if (text.empty() || text.size() > 48)
    {
        return std::nullopt;
    }
    std::string copy(text);
    UUID value{};
    if (UuidFromStringA(reinterpret_cast<RPC_CSTR>(copy.data()), &value) != RPC_S_OK)
    {
        return std::nullopt;
    }
    Guid id;
    std::memcpy(id.bytes.data(), &value, sizeof(value));
    return id;
}

namespace
{
[[nodiscard]] const char* NameAt(const char* const* names, int count, int index) noexcept
{
    if (index < 0 || index >= count || names[index] == nullptr)
    {
        return nullptr;
    }
    return names[index];
}

template <typename T>
[[nodiscard]] std::optional<T> ValueFromTable(const char* const* names, int count, std::string_view name) noexcept
{
    for (int index = 0; index < count; ++index)
    {
        if (names[index] != nullptr && name == names[index])
        {
            return static_cast<T>(index);
        }
    }
    return std::nullopt;
}

constexpr const char* kBlendNames[] = {"Normal",     "Multiply",   "Screen",      "Overlay",    "Darken",
                                       "Lighten",    "Difference", "Color Dodge", "Color Burn", "Hue",
                                       "Saturation", "Color",      "Luminosity"};
constexpr const char* kSamplingNames[] = {"Nearest", "Smooth", "High quality"};
constexpr const char* kAdjustmentNames[] = {"Hue/Saturation", "Levels", "Curves", "Exposure", "Gradient Map", "Grain"};
constexpr const char* kLevelsChannelNames[] = {"RGB", "Red", "Green", "Blue"};
constexpr const char* kColorRangeNames[] = {"Master", "Reds", "Yellows", "Greens", "Cyans", "Blues", "Magentas"};
constexpr const char* kShapeKindNames[] = {"Rectangle", "Ellipse"};
} // namespace

const char* LayerBlendModeName(LayerBlendMode mode) noexcept
{
    return NameAt(kBlendNames, static_cast<int>(std::size(kBlendNames)), static_cast<int>(mode));
}

std::optional<LayerBlendMode> LayerBlendModeFromName(std::string_view name) noexcept
{
    return ValueFromTable<LayerBlendMode>(kBlendNames, static_cast<int>(std::size(kBlendNames)), name);
}

const char* LayerSamplingName(LayerSampling sampling) noexcept
{
    return NameAt(kSamplingNames, static_cast<int>(std::size(kSamplingNames)), static_cast<int>(sampling));
}

std::optional<LayerSampling> LayerSamplingFromName(std::string_view name) noexcept
{
    return ValueFromTable<LayerSampling>(kSamplingNames, static_cast<int>(std::size(kSamplingNames)), name);
}

const char* AdjustmentKindName(AdjustmentKind kind) noexcept
{
    return NameAt(kAdjustmentNames, static_cast<int>(std::size(kAdjustmentNames)), static_cast<int>(kind));
}

std::optional<AdjustmentKind> AdjustmentKindFromName(std::string_view name) noexcept
{
    return ValueFromTable<AdjustmentKind>(kAdjustmentNames, static_cast<int>(std::size(kAdjustmentNames)), name);
}

const char* LevelsChannelName(LevelsChannel channel) noexcept
{
    return NameAt(kLevelsChannelNames, static_cast<int>(std::size(kLevelsChannelNames)), static_cast<int>(channel));
}

std::optional<LevelsChannel> LevelsChannelFromName(std::string_view name) noexcept
{
    return ValueFromTable<LevelsChannel>(kLevelsChannelNames, static_cast<int>(std::size(kLevelsChannelNames)), name);
}

const char* ColorRangeName(ColorRange range) noexcept
{
    return NameAt(kColorRangeNames, static_cast<int>(std::size(kColorRangeNames)), static_cast<int>(range));
}

std::optional<ColorRange> ColorRangeFromName(std::string_view name) noexcept
{
    return ValueFromTable<ColorRange>(kColorRangeNames, static_cast<int>(std::size(kColorRangeNames)), name);
}

const char* ShapeKindName(ShapeKind kind) noexcept
{
    return NameAt(kShapeKindNames, static_cast<int>(std::size(kShapeKindNames)), static_cast<int>(kind));
}

std::optional<ShapeKind> ShapeKindFromName(std::string_view name) noexcept
{
    return ValueFromTable<ShapeKind>(kShapeKindNames, static_cast<int>(std::size(kShapeKindNames)), name);
}

Raster Raster::Make(int width, int height, RasterFormat format)
{
    return Make(width, height, format, nullptr, 0);
}

std::shared_ptr<Raster::Storage> Raster::AllocateStorage()
{
    // make_unique establishes exception-safe ownership before the custom shared_ptr control block is made.
    // If that second allocation fails, FreeStorage still returns the already-owned payload exactly once.
    std::unique_ptr<Storage, FreeStorage> storage{std::make_unique<Storage>().release()};
    return std::shared_ptr<Storage>{std::move(storage)};
}

void Raster::CacheRetirementMemory(Storage& storage) noexcept
{
    const auto logical = static_cast<std::uint64_t>(storage.stride) * static_cast<std::uint64_t>(storage.height);
    if (logical > (std::numeric_limits<std::size_t>::max)())
        return;
    auto bytes = PixelBuffer::RequiredBytes(static_cast<std::size_t>(logical));
    const auto add = [&](std::uint64_t count, std::uint64_t size) noexcept
    {
        if ((size && count > (std::numeric_limits<std::uint64_t>::max)() / size) ||
            count * size > (std::numeric_limits<std::uint64_t>::max)() - bytes)
            return false;
        bytes += count * size;
        return true;
    };
    const auto child = [&](const std::shared_ptr<const Storage>& value) noexcept
    {
        if (!value)
            return true;
        storage.graphDepth = (std::max)(storage.graphDepth, (std::min)(129u, value->graphDepth + 1));
        if (!value->retirementBytes || storage.graphDepth > 128 ||
            value->graphNodes > (std::numeric_limits<std::uint64_t>::max)() - storage.graphNodes ||
            !add(1, value->retirementBytes))
            return false;
        storage.graphNodes += value->graphNodes;
        return true;
    };
    // Sum already cached child bounds while the producer owns this record. Even a repeatedly shared nested
    // sparse DAG then has an O(1) bound read; admission never recursively expands it or materializes pixels.
    if (!bytes || !add(1, sizeof(Storage) + 64) || !add(storage.tiles.capacity(), sizeof(RasterTile)) ||
        !child(storage.base))
        return;
    for (const auto& tile : storage.tiles)
        if (!child(tile.pixels._storage))
            return;
    storage.retirementBytes = bytes;
}

void Raster::FreeStorage::operator()(Storage* storage) const noexcept
{
    storage->DeferredReleaseNode::bytes = storage->bytes.capacity() + storage->adoptedBytes.AllocationBytes() +
                                          storage->tiles.capacity() * sizeof(RasterTile);
    storage->destroy = [](DeferredReleaseNode* node) noexcept
    {
        std::unique_ptr<Storage> owned{static_cast<Storage*>(node)};
        TracePipelineFree(owned->bytes.capacity());
    };
    if (!TryDeferRelease(*storage))
    {
        storage->destroy(storage);
    }
}

Raster Raster::Make(int width, int height, RasterFormat format, const std::uint8_t* pixels, std::size_t byteCount)
{
    Raster raster;
    if (width < 1 || height < 1 || width > kMaxCanvasSide || height > kMaxCanvasSide)
    {
        return raster;
    }
    const int stride = format == RasterFormat::Gray8 ? width : width * 4;
    const std::uint64_t bytes = static_cast<std::uint64_t>(stride) * static_cast<std::uint64_t>(height);
    if (bytes > SIZE_MAX)
    {
        return raster;
    }
    const std::size_t size = static_cast<std::size_t>(bytes);
    if (pixels != nullptr && byteCount != size)
    {
        return raster;
    }
    auto storage = AllocateStorage();
    storage->width = width;
    storage->height = height;
    storage->stride = stride;
    storage->format = format;
    if (pixels == nullptr)
    {
        storage->bytes.assign(size, 0);
    }
    else
    {
        storage->bytes.assign(pixels, pixels + size);
    }
    storage->assembled.store(storage->bytes.data(), std::memory_order_release);
    CacheRetirementMemory(*storage);
    raster._storage = std::move(storage);
    return raster;
}

Raster Raster::Adopt(int width, int height, RasterFormat format, PixelBuffer&& pixels)
{
    if (width < 1 || height < 1 || width > kMaxCanvasSide || height > kMaxCanvasSide ||
        (format != RasterFormat::Gray8 && format != RasterFormat::Bgra8888))
    {
        return {};
    }
    const int stride = format == RasterFormat::Gray8 ? width : width * 4;
    const std::uint64_t size = static_cast<std::uint64_t>(stride) * static_cast<std::uint64_t>(height);
    if (!pixels || size != pixels.Size())
    {
        return {};
    }
    auto storage = AllocateStorage();
    storage->width = width;
    storage->height = height;
    storage->stride = stride;
    storage->format = format;
    storage->adoptedBytes = std::move(pixels);
    storage->assembled.store(storage->adoptedBytes.Data(), std::memory_order_release);
    CacheRetirementMemory(*storage);
    return Raster(std::move(storage));
}

Raster Raster::MakeThumbnail(const Raster& source, int maxSide)
{
    if (!source.HasStorage() || maxSide < 1)
    {
        return {};
    }
    const int width = source.Width();
    const int height = source.Height();
    const int largest = std::max(width, height);
    if (largest <= maxSide)
    {
        return source;
    }
    const int thumbWidth = std::max(1, (width * maxSide + largest / 2) / largest);
    const int thumbHeight = std::max(1, (height * maxSide + largest / 2) / largest);
    const RasterFormat format = source.Format();
    const int dstStride = format == RasterFormat::Gray8 ? thumbWidth : thumbWidth * 4;
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(dstStride) * static_cast<std::size_t>(thumbHeight), 0);
    const int channels = format == RasterFormat::Gray8 ? 1 : 4;
    if (source.IsSparse() && !source.IsMaterialized())
    {
        // A painted layer's thumbnail samples its tiles and base directly; assembling the pixels is never needed.
        for (int y = 0; y < thumbHeight; ++y)
        {
            const int srcY = std::min(height - 1, (y * height) / thumbHeight);
            for (int x = 0; x < thumbWidth; ++x)
            {
                const int srcX = std::min(width - 1, (x * width) / thumbWidth);
                source.ReadRegion(srcX, srcY, 1, 1,
                                  bytes.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(dstStride) +
                                      static_cast<std::size_t>(x) * static_cast<std::size_t>(channels),
                                  channels, source.Fill());
            }
        }
        return Make(thumbWidth, thumbHeight, format, bytes.data(), bytes.size());
    }
    const int srcStride = source.Stride();
    const std::uint8_t* src = source.Data();
    if (src == nullptr)
    {
        return {};
    }
    for (int y = 0; y < thumbHeight; ++y)
    {
        const int srcY = std::min(height - 1, (y * height) / thumbHeight);
        for (int x = 0; x < thumbWidth; ++x)
        {
            const int srcX = std::min(width - 1, (x * width) / thumbWidth);
            const std::uint8_t* from = src + static_cast<std::size_t>(srcY) * static_cast<std::size_t>(srcStride) +
                                       static_cast<std::size_t>(srcX) * static_cast<std::size_t>(channels);
            std::uint8_t* to = bytes.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(dstStride) +
                               static_cast<std::size_t>(x) * static_cast<std::size_t>(channels);
            std::memcpy(to, from, static_cast<std::size_t>(channels));
        }
    }
    return Make(thumbWidth, thumbHeight, format, bytes.data(), bytes.size());
}

namespace
{
// Copies the overlap of a source rectangle onto a destination rectangle, both with their own strides.
void BlitRegion(std::uint8_t* dst, int dstStride, int dstX, int dstY, int dstWidth, int dstHeight,
                const std::uint8_t* src, int srcStride, int srcX, int srcY, int srcWidth, int srcHeight,
                int channels) noexcept
{
    // `src` covers [srcX, srcX + srcWidth) in the shared coordinate space; `dst` covers [dstX, ...).
    const int left = std::max(dstX, srcX);
    const int top = std::max(dstY, srcY);
    const int right = std::min(dstX + dstWidth, srcX + srcWidth);
    const int bottom = std::min(dstY + dstHeight, srcY + srcHeight);
    if (right <= left || bottom <= top)
    {
        return;
    }
    const std::size_t rowBytes = static_cast<std::size_t>(right - left) * static_cast<std::size_t>(channels);
    for (int y = top; y < bottom; ++y)
    {
        std::memcpy(dst + static_cast<std::size_t>(y - dstY) * static_cast<std::size_t>(dstStride) +
                        static_cast<std::size_t>(left - dstX) * static_cast<std::size_t>(channels),
                    src + static_cast<std::size_t>(y - srcY) * static_cast<std::size_t>(srcStride) +
                        static_cast<std::size_t>(left - srcX) * static_cast<std::size_t>(channels),
                    rowBytes);
    }
}

// A tile's window blitted onto a destination rectangle of the grid.
void BlitTile(std::uint8_t* dst, int dstStride, int dstX, int dstY, int dstWidth, int dstHeight, const RasterTile& tile,
              int channels) noexcept
{
    const std::uint8_t* pixels = tile.pixels.Data();
    if (pixels == nullptr)
    {
        return;
    }
    const int stride = tile.pixels.Stride();
    const std::uint8_t* origin = pixels + static_cast<std::size_t>(tile.sourceY) * static_cast<std::size_t>(stride) +
                                 static_cast<std::size_t>(tile.sourceX) * static_cast<std::size_t>(channels);
    BlitRegion(dst, dstStride, dstX, dstY, dstWidth, dstHeight, origin, stride, tile.x, tile.y, tile.Width(),
               tile.Height(), channels);
}

struct IntRect
{
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    [[nodiscard]] bool Empty() const noexcept
    {
        return width <= 0 || height <= 0;
    }
    [[nodiscard]] IntRect Intersect(const IntRect& other) const noexcept
    {
        const int left = std::max(x, other.x);
        const int top = std::max(y, other.y);
        const int right = std::min(x + width, other.x + other.width);
        const int bottom = std::min(y + height, other.y + other.height);
        return IntRect{left, top, std::max(0, right - left), std::max(0, bottom - top)};
    }
};

// `rect` less `hole` (which lies inside it): the rows above and below, then the columns beside.
void Subtract(const IntRect& rect, const IntRect& hole, std::vector<IntRect>& out)
{
    if (hole.y > rect.y)
    {
        out.push_back(IntRect{rect.x, rect.y, rect.width, hole.y - rect.y});
    }
    if (hole.y + hole.height < rect.y + rect.height)
    {
        out.push_back(IntRect{rect.x, hole.y + hole.height, rect.width, rect.y + rect.height - hole.y - hole.height});
    }
    if (hole.x > rect.x)
    {
        out.push_back(IntRect{rect.x, hole.y, hole.x - rect.x, hole.height});
    }
    if (hole.x + hole.width < rect.x + rect.width)
    {
        out.push_back(IntRect{hole.x + hole.width, hole.y, rect.x + rect.width - hole.x - hole.width, hole.height});
    }
}
} // namespace

const std::uint8_t* Raster::Data() const noexcept
{
    if (!_storage)
    {
        return nullptr;
    }
    const std::uint8_t* pixels = _storage->assembled.load(std::memory_order_acquire);
    if (pixels == nullptr)
    {
        Materialize(*_storage);
        pixels = _storage->assembled.load(std::memory_order_acquire);
    }
    return pixels;
}

const std::vector<RasterTile>& Raster::Tiles() const noexcept
{
    static const std::vector<RasterTile> kNone;
    return _storage ? _storage->tiles : kNone;
}

void Raster::Materialize(const Storage& storage) noexcept
{
    // Serialize builders, not readers. A failed allocation leaves the cache unpublished and retryable.
    const std::lock_guard lock(storage.assemblyMutex);
    if (storage.assembled.load(std::memory_order_acquire) != nullptr)
    {
        return;
    }
    const std::size_t size = static_cast<std::size_t>(storage.stride) * static_cast<std::size_t>(storage.height);
    std::vector<std::uint8_t> bytes;
    try
    {
        bytes.assign(size, storage.fill);
    }
    catch (const std::bad_alloc&)
    {
        return;
    }
    const int channels = storage.format == RasterFormat::Gray8 ? 1 : 4;
    const std::uint8_t* base = storage.base ? storage.base->assembled.load(std::memory_order_acquire) : nullptr;
    if (base != nullptr)
    {
        BlitRegion(bytes.data(), storage.stride, 0, 0, storage.width, storage.height, base, storage.base->stride,
                   storage.baseX, storage.baseY, storage.base->width, storage.base->height, channels);
    }
    for (const RasterTile& tile : storage.tiles)
    {
        BlitTile(bytes.data(), storage.stride, 0, 0, storage.width, storage.height, tile, channels);
    }
    storage.bytes = std::move(bytes);
    storage.assembled.store(storage.bytes.data(), std::memory_order_release);
}

void Raster::ReadRegion(int x, int y, int width, int height, std::uint8_t* out, int outStride,
                        std::uint8_t fill) const noexcept
{
    if (out == nullptr || width <= 0 || height <= 0)
    {
        return;
    }
    const int channels = Channels();
    const std::uint8_t* pixels = _storage ? _storage->assembled.load(std::memory_order_acquire) : nullptr;
    if (pixels != nullptr && x >= 0 && y >= 0 && width <= _storage->width && height <= _storage->height &&
        x <= _storage->width - width && y <= _storage->height - height)
    {
        // Every requested pixel is present. Skip the fill pass, including for a published sparse cache.
        BlitRegion(out, outStride, x, y, width, height, pixels, _storage->stride, 0, 0, _storage->width,
                   _storage->height, channels);
        return;
    }
    for (int row = 0; row < height; ++row)
    {
        std::memset(out + static_cast<std::size_t>(row) * static_cast<std::size_t>(outStride), fill,
                    static_cast<std::size_t>(width) * static_cast<std::size_t>(channels));
    }
    if (!_storage)
    {
        return;
    }
    const Storage& storage = *_storage;
    if (pixels != nullptr)
    {
        BlitRegion(out, outStride, x, y, width, height, pixels, storage.stride, 0, 0, storage.width, storage.height,
                   channels);
        return;
    }
    // Un-assembled sparse storage: the raster's own fill inside its bounds, then the base, then the tiles.
    const IntRect inside = IntRect{x, y, width, height}.Intersect(IntRect{0, 0, storage.width, storage.height});
    if (!inside.Empty() && storage.fill != fill)
    {
        for (int row = inside.y; row < inside.y + inside.height; ++row)
        {
            std::memset(out + static_cast<std::size_t>(row - y) * static_cast<std::size_t>(outStride) +
                            static_cast<std::size_t>(inside.x - x) * static_cast<std::size_t>(channels),
                        storage.fill, static_cast<std::size_t>(inside.width) * static_cast<std::size_t>(channels));
        }
    }
    const std::uint8_t* base = storage.base ? storage.base->assembled.load(std::memory_order_acquire) : nullptr;
    if (base != nullptr)
    {
        // Only the part of the base inside this raster's grid shows.
        const IntRect visible =
            IntRect{storage.baseX, storage.baseY, storage.base->width, storage.base->height}.Intersect(
                IntRect{0, 0, storage.width, storage.height});
        if (!visible.Empty())
        {
            BlitRegion(out, outStride, x, y, width, height,
                       base +
                           static_cast<std::size_t>(visible.y - storage.baseY) *
                               static_cast<std::size_t>(storage.base->stride) +
                           static_cast<std::size_t>(visible.x - storage.baseX) * static_cast<std::size_t>(channels),
                       storage.base->stride, visible.x, visible.y, visible.width, visible.height, channels);
        }
    }
    for (const RasterTile& tile : storage.tiles)
    {
        BlitTile(out, outStride, x, y, width, height, tile, channels);
    }
}

Raster Raster::Crop(int x, int y, int width, int height, std::uint8_t fill) const
{
    if (width < 1 || height < 1 || width > kMaxCanvasSide || height > kMaxCanvasSide)
    {
        return {};
    }
    const int channels = Channels();
    const int stride = width * channels;
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(stride) * static_cast<std::size_t>(height), fill);
    ReadRegion(x, y, width, height, bytes.data(), stride, fill);
    return Make(width, height, Format(), bytes.data(), bytes.size());
}

void Raster::ForEachStorage(const std::function<void(const Raster&)>& visit) const
{
    if (!_storage || !visit)
    {
        return;
    }
    visit(*this);
    if (_storage->base)
    {
        visit(Raster(_storage->base));
    }
    for (const RasterTile& tile : _storage->tiles)
    {
        visit(tile.pixels);
    }
}

Raster Raster::MakeSparse(int width, int height, RasterFormat format, const Raster& base, int baseX, int baseY,
                          std::vector<RasterTile> tiles, std::uint8_t fill)
{
    if (width < 1 || height < 1 || width > kMaxCanvasSide || height > kMaxCanvasSide)
    {
        return {};
    }
    if (base.HasStorage() && (base.Format() != format || base.IsSparse()))
    {
        return {};
    }
    auto storage = AllocateStorage();
    storage->width = width;
    storage->height = height;
    storage->stride = format == RasterFormat::Gray8 ? width : width * 4;
    storage->format = format;
    storage->sparse = true;
    storage->fill = fill;
    storage->base = base._storage;
    storage->baseX = baseX;
    storage->baseY = baseY;
    const IntRect grid{0, 0, width, height};
    for (RasterTile& tile : tiles)
    {
        if (!tile.pixels.HasStorage() || tile.pixels.Format() != format)
        {
            continue;
        }
        if (tile.sourceX < 0 || tile.sourceY < 0 || tile.sourceX + tile.Width() > tile.pixels.Width() ||
            tile.sourceY + tile.Height() > tile.pixels.Height())
        {
            continue;
        }
        const IntRect rect{tile.x, tile.y, tile.Width(), tile.Height()};
        const IntRect kept = rect.Intersect(grid);
        if (kept.Empty())
        {
            continue;
        }
        // Clipping moves the window; the pixels stay shared.
        tile.sourceX += kept.x - rect.x;
        tile.sourceY += kept.y - rect.y;
        tile.x = kept.x;
        tile.y = kept.y;
        tile.width = kept.width;
        tile.height = kept.height;
        storage->tiles.push_back(std::move(tile));
    }
    CacheRetirementMemory(*storage);
    Raster raster;
    raster._storage = std::move(storage);
    return raster;
}

namespace
{
// The part of `area` outside `inside`, as windows of one shared block of `value` no larger than a brush tile, so
// padding a grown grid costs one small allocation however much area it adds.
[[nodiscard]] std::vector<RasterTile> UniformTiles(const IntRect& area, const IntRect& inside, RasterFormat format,
                                                   std::uint8_t value, const CancelToken& cancellation)
{
    constexpr int kBlock = 256;
    std::vector<IntRect> pieces;
    const IntRect overlap = area.Intersect(inside);
    if (overlap.Empty())
    {
        pieces.push_back(area);
    }
    else
    {
        Subtract(area, overlap, pieces);
    }
    std::vector<RasterTile> tiles;
    int blockWidth = 0;
    int blockHeight = 0;
    for (const IntRect& piece : pieces)
    {
        blockWidth = std::max(blockWidth, std::min(kBlock, piece.width));
        blockHeight = std::max(blockHeight, std::min(kBlock, piece.height));
    }
    if (blockWidth < 1 || blockHeight < 1)
    {
        return tiles;
    }
    const int channels = format == RasterFormat::Gray8 ? 1 : 4;
    const std::vector<std::uint8_t> bytes(static_cast<std::size_t>(blockWidth) * static_cast<std::size_t>(blockHeight) *
                                              static_cast<std::size_t>(channels),
                                          value);
    const Raster block = Raster::Make(blockWidth, blockHeight, format, bytes.data(), bytes.size());
    for (const IntRect& piece : pieces)
    {
        for (int y = piece.y; y < piece.y + piece.height; y += blockHeight)
        {
            if (cancellation.IsCancelled())
                return {};
            for (int x = piece.x; x < piece.x + piece.width; x += blockWidth)
            {
                tiles.push_back(RasterTile{x, y, block, 0, 0, std::min(blockWidth, piece.x + piece.width - x),
                                           std::min(blockHeight, piece.y + piece.height - y)});
            }
        }
    }
    return tiles;
}
} // namespace

Raster Raster::Replacing(const Raster& source, int sourceX, int sourceY, std::vector<RasterTile> tiles, int cropX,
                         int cropY, int cropWidth, int cropHeight, std::uint8_t fill, CancelToken cancellation)
{
    if (cropWidth < 1 || cropHeight < 1 || cancellation.IsCancelled())
    {
        return {};
    }
    const RasterFormat format = source.HasStorage()
                                    ? source.Format()
                                    : (tiles.empty() ? RasterFormat::Bgra8888 : tiles.front().pixels.Format());
    Raster base;
    int baseX = sourceX;
    int baseY = sourceY;
    std::uint8_t gridFill = fill;
    std::vector<RasterTile> older;
    if (source.IsSparse())
    {
        base = source.Base();
        baseX = sourceX + source.BaseX();
        baseY = sourceY + source.BaseY();
        // What neither the base nor a tile reaches keeps the source's own fill (a hide-all mask painted once stays
        // hidden elsewhere); only area the crop adds beyond the source reads `fill`, as the oldest tiles.
        gridFill = source.Fill();
        if (gridFill != fill)
        {
            older =
                UniformTiles(IntRect{cropX, cropY, cropWidth, cropHeight},
                             IntRect{sourceX, sourceY, source.Width(), source.Height()}, format, fill, cancellation);
        }
        for (RasterTile tile : source.Tiles())
        {
            if (cancellation.IsCancelled())
                return {};
            tile.x += sourceX;
            tile.y += sourceY;
            older.push_back(std::move(tile));
        }
    }
    else if (source.HasStorage())
    {
        base = source;
    }
    // Newer tiles replace what they cover: split every older tile around them so the list stays disjoint.
    std::vector<RasterTile> result;
    for (const RasterTile& old : older)
    {
        if (cancellation.IsCancelled())
            return {};
        std::vector<IntRect> pieces{IntRect{old.x, old.y, old.Width(), old.Height()}};
        for (const RasterTile& fresh : tiles)
        {
            if (cancellation.IsCancelled())
                return {};
            const IntRect cover{fresh.x, fresh.y, fresh.Width(), fresh.Height()};
            std::vector<IntRect> next;
            for (const IntRect& piece : pieces)
            {
                if (cancellation.IsCancelled())
                    return {};
                const IntRect overlap = piece.Intersect(cover);
                if (overlap.Empty())
                {
                    next.push_back(piece);
                    continue;
                }
                Subtract(piece, overlap, next);
            }
            pieces = std::move(next);
        }
        for (const IntRect& piece : pieces)
        {
            // A piece is a window into the old tile's pixels: nothing is copied.
            RasterTile view = old;
            view.sourceX = old.sourceX + piece.x - old.x;
            view.sourceY = old.sourceY + piece.y - old.y;
            view.x = piece.x;
            view.y = piece.y;
            view.width = piece.width;
            view.height = piece.height;
            result.push_back(std::move(view));
        }
    }
    for (RasterTile& fresh : tiles)
    {
        if (cancellation.IsCancelled())
            return {};
        result.push_back(std::move(fresh));
    }
    for (RasterTile& tile : result)
    {
        if (cancellation.IsCancelled())
            return {};
        tile.x -= cropX;
        tile.y -= cropY;
    }
    Raster prepared =
        MakeSparse(cropWidth, cropHeight, format, base, baseX - cropX, baseY - cropY, std::move(result), gridFill);
    return cancellation.IsCancelled() ? Raster{} : prepared;
}

Raster Raster::Stretched(int width, int height) const
{
    if (!_storage || width < 1 || height < 1 || width > kMaxCanvasSide || height > kMaxCanvasSide)
    {
        return {};
    }
    if (width == Width() && height == Height())
    {
        return *this;
    }
    const int channels = Channels();
    const int sourceWidth = Width();
    const int sourceHeight = Height();
    std::vector<int> columns(static_cast<std::size_t>(width));
    for (int x = 0; x < width; ++x)
    {
        columns[static_cast<std::size_t>(x)] =
            static_cast<int>(static_cast<std::int64_t>(x) * sourceWidth / static_cast<std::int64_t>(width));
    }
    std::vector<std::uint8_t> row(static_cast<std::size_t>(sourceWidth) * static_cast<std::size_t>(channels));
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) *
                                    static_cast<std::size_t>(channels));
    int loaded = -1;
    for (int y = 0; y < height; ++y)
    {
        const int sourceY =
            static_cast<int>(static_cast<std::int64_t>(y) * sourceHeight / static_cast<std::int64_t>(height));
        if (sourceY != loaded)
        {
            // One source row per run of target rows; a sparse raster reads its tiles without assembling.
            ReadRegion(0, sourceY, sourceWidth, 1, row.data(), sourceWidth * channels, Fill());
            loaded = sourceY;
        }
        std::uint8_t* out = bytes.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(width) *
                                               static_cast<std::size_t>(channels);
        for (int x = 0; x < width; ++x)
        {
            std::memcpy(out + static_cast<std::size_t>(x) * static_cast<std::size_t>(channels),
                        row.data() + static_cast<std::size_t>(columns[static_cast<std::size_t>(x)]) *
                                         static_cast<std::size_t>(channels),
                        static_cast<std::size_t>(channels));
        }
    }
    return Make(width, height, Format(), bytes.data(), bytes.size());
}

Raster CarriedMask(const Raster& mask, int oldWidth, int oldHeight, int x, int y, int width, int height,
                   std::uint8_t outside)
{
    const Raster onGrid = mask.Stretched(oldWidth, oldHeight);
    if (!onGrid.HasStorage())
    {
        return {};
    }
    return Raster::Replacing(onGrid, x, y, {}, 0, 0, width, height, outside);
}

bool LayerTransform::IsValid() const noexcept
{
    return std::isfinite(origin.x) && std::isfinite(origin.y) && std::isfinite(size.x) && std::isfinite(size.y) &&
           std::isfinite(rotation) && size.x >= 1 && size.x <= kMaxTransformSide && size.y >= 1 &&
           size.y <= kMaxTransformSide && std::abs(origin.x) <= kMaxTransformOrigin &&
           std::abs(origin.y) <= kMaxTransformOrigin;
}

LevelRange LevelRange::Normalized() const noexcept
{
    const auto clamp = [](double value, double lo, double hi, double fallback)
    {
        if (!std::isfinite(value))
        {
            return fallback;
        }
        return std::min(hi, std::max(lo, value));
    };
    LevelRange result = *this;
    result.black = clamp(black, 0, 254, 0);
    result.white = clamp(white, result.black + 1, 255, 255);
    result.gamma = clamp(gamma, 0.1, 9.99, 1);
    result.outputBlack = clamp(outputBlack, 0, 255, 0);
    result.outputWhite = clamp(outputWhite, 0, 255, 255);
    return result;
}

bool CurvesSettings::IsValid() const noexcept
{
    for (const std::vector<CurvePoint>& points : channels)
    {
        if (points.size() < 2 || points.size() > 32)
        {
            return false;
        }
        if (points.front().x != 0 || points.back().x != 255)
        {
            return false;
        }
        for (std::size_t i = 0; i < points.size(); ++i)
        {
            const CurvePoint& point = points[i];
            if (!std::isfinite(point.x) || !std::isfinite(point.y) || point.x < 0 || point.x > 255 || point.y < 0 ||
                point.y > 255)
            {
                return false;
            }
            if (i > 0 && !(points[i - 1].x < point.x))
            {
                return false;
            }
        }
    }
    return true;
}

std::array<HueBand, static_cast<std::size_t>(ColorRange::Count)> HueSaturationSettings::DefaultBands() noexcept
{
    return {
        HueBand{0, 0, 360, 360},     HueBand{315, 345, 15, 45},   HueBand{15, 45, 75, 105},
        HueBand{75, 105, 135, 165},  HueBand{135, 165, 195, 225}, HueBand{195, 225, 255, 285},
        HueBand{255, 285, 315, 345},
    };
}

HueSaturationSettings LayerAdjustment::ResolvedHsv() const noexcept
{
    if (hsvSettings.has_value())
    {
        return hsvSettings.value();
    }
    HueSaturationSettings settings;
    settings.colorize = colorize;
    settings.adjustments[static_cast<std::size_t>(ColorRange::Master)] = RangeAdjustment{hue, saturation, lightness};
    return settings;
}

bool LayerAdjustment::IsValid() const noexcept
{
    if (!std::isfinite(hue) || !std::isfinite(saturation) || !std::isfinite(lightness) || std::abs(hue) > 360 ||
        std::abs(saturation) > 100 || std::abs(lightness) > 100)
    {
        return false;
    }
    const HueSaturationSettings hsv = ResolvedHsv();
    for (const RangeAdjustment& adjustment : hsv.adjustments)
    {
        if (!std::isfinite(adjustment.hue) || std::abs(adjustment.hue) > 360 || !std::isfinite(adjustment.saturation) ||
            std::abs(adjustment.saturation) > 100 || !std::isfinite(adjustment.lightness) ||
            std::abs(adjustment.lightness) > 100)
        {
            return false;
        }
    }
    for (const HueBand& band : hsv.bands)
    {
        for (const double handle : band.Handles())
        {
            if (!std::isfinite(handle))
            {
                return false;
            }
        }
    }
    for (const LevelRange& range : levels.ranges)
    {
        if (!(range == range.Normalized()))
        {
            return false;
        }
    }
    return curves.IsValid() && Exposure().IsValid() && GradientMap().IsValid() && Grain().IsValid();
}

bool LayerMask::IsValid(const Raster& image) noexcept
{
    return image.HasStorage() && image.Format() == RasterFormat::Gray8 && image.Width() > 0 && image.Height() > 0;
}

bool ImageLayer::operator==(const ImageLayer& other) const noexcept
{
    const bool assetEqual =
        (!asset.has_value() && !other.asset.has_value()) ||
        (asset.has_value() && other.asset.has_value() && asset.value().image == other.asset.value().image);
    return id == other.id && name == other.name && visible == other.visible && transform == other.transform &&
           assetEqual && parentID == other.parentID && isGroup == other.isGroup && opacity == other.opacity &&
           blendMode == other.blendMode && mask == other.mask && maskSourceID == other.maskSourceID &&
           adjustment == other.adjustment && shape == other.shape;
}

ImageLayer ImageLayer::Blank(std::wstring layerName, Vec2 size)
{
    ImageLayer layer;
    layer.id = Guid::Generate();
    layer.transform.size = size;
    layer.name = std::move(layerName);
    return layer;
}

std::wstring TrimWide(std::wstring_view text)
{
    std::size_t begin = 0;
    while (begin < text.size() && iswspace(text[begin]))
    {
        ++begin;
    }
    std::size_t end = text.size();
    while (end > begin && iswspace(text[end - 1]))
    {
        --end;
    }
    return std::wstring(text.substr(begin, end - begin));
}

bool NameUtf8LengthOk(std::wstring_view name) noexcept
{
    if (name.empty())
    {
        return true;
    }
    const int bytes =
        WideCharToMultiByte(CP_UTF8, 0, name.data(), static_cast<int>(name.size()), nullptr, 0, nullptr, nullptr);
    return bytes >= 0 && bytes <= kMaxNameUtf8Bytes;
}

namespace
{
[[nodiscard]] const ImageLayer* LayerById(const std::vector<ImageLayer>& layers, const Guid& id) noexcept
{
    for (const ImageLayer& layer : layers)
    {
        if (layer.id == id)
        {
            return &layer;
        }
    }
    return nullptr;
}

[[nodiscard]] ImageLayer* LayerById(std::vector<ImageLayer>& layers, const Guid& id) noexcept
{
    for (ImageLayer& layer : layers)
    {
        if (layer.id == id)
        {
            return &layer;
        }
    }
    return nullptr;
}

} // namespace

std::optional<std::size_t> IndexOf(const std::vector<ImageLayer>& layers, const Guid& id) noexcept
{
    for (std::size_t i = 0; i < layers.size(); ++i)
    {
        if (layers[i].id == id)
        {
            return i;
        }
    }
    return std::nullopt;
}

DocumentStatus ValidateHierarchy(const std::vector<ImageLayer>& layers)
{
    std::unordered_map<Guid, const ImageLayer*> byId;
    byId.reserve(layers.size());
    for (const ImageLayer& layer : layers)
    {
        if (!byId.emplace(layer.id, &layer).second)
        {
            return DocumentStatus::Invalid;
        }
        if (layer.isGroup && layer.asset.has_value())
        {
            return DocumentStatus::Invalid;
        }
    }
    for (const ImageLayer& layer : layers)
    {
        std::unordered_set<Guid> seen{layer.id};
        std::optional<Guid> parent = layer.parentID;
        while (parent.has_value())
        {
            if (static_cast<int>(seen.size()) > kMaxGroupDepth)
            {
                return DocumentStatus::Invalid;
            }
            if (!seen.insert(parent.value()).second)
            {
                return DocumentStatus::Invalid;
            }
            const auto found = byId.find(parent.value());
            if (found == byId.end() || !found->second->isGroup)
            {
                return DocumentStatus::Invalid;
            }
            parent = found->second->parentID;
        }
        if (layer.isGroup && static_cast<int>(seen.size()) > kMaxGroupDepth)
        {
            return DocumentStatus::Invalid;
        }
    }
    return DocumentStatus::Ok;
}

DocumentStatus ValidateLiveMaskGraph(const std::vector<ImageLayer>& layers)
{
    std::unordered_map<Guid, const ImageLayer*> records;
    records.reserve(layers.size());
    for (const ImageLayer& layer : layers)
    {
        if (!records.emplace(layer.id, &layer).second)
        {
            return DocumentStatus::Invalid;
        }
    }
    for (const ImageLayer& layer : layers)
    {
        std::unordered_set<Guid> path;
        std::optional<Guid> current = layer.id;
        while (current.has_value())
        {
            if (static_cast<int>(path.size()) >= kMaxLiveMaskPath || !path.insert(current.value()).second)
            {
                return DocumentStatus::Invalid;
            }
            const auto found = records.find(current.value());
            if (found == records.end())
            {
                return DocumentStatus::Invalid;
            }
            const ImageLayer* record = found->second;
            if (record->maskSourceID.has_value())
            {
                if (record->isGroup)
                {
                    return DocumentStatus::Invalid;
                }
                const auto source = records.find(record->maskSourceID.value());
                if (source == records.end() || source->second->isGroup || source->second->adjustment.has_value())
                {
                    return DocumentStatus::Invalid;
                }
            }
            current = record->maskSourceID;
        }
    }
    return DocumentStatus::Ok;
}

DocumentStatus ValidateDocument(const CanvasDocument& document, int formatVersion)
{
    if (formatVersion < kMinFormatVersion || formatVersion > kMaxFormatVersion)
    {
        return DocumentStatus::UnsupportedVersion;
    }
    if (!std::isfinite(document.resolution) || document.resolution < 1 || document.resolution > 9600)
    {
        return DocumentStatus::Invalid;
    }
    if (document.width < 1 || document.width > kMaxCanvasSide || document.height < 1 ||
        document.height > kMaxCanvasSide || static_cast<int>(document.layers.size()) > kMaxLayerCount)
    {
        return DocumentStatus::TooLarge;
    }
    int imagePixels = 0;
    int maskPixels = 0;
    std::unordered_set<Guid> ids;
    ids.reserve(document.layers.size());
    for (const ImageLayer& layer : document.layers)
    {
        if (layer.adjustment.has_value())
        {
            if (formatVersion < 7 || layer.isGroup || layer.asset.has_value() || !layer.adjustment.value().IsValid())
            {
                return DocumentStatus::Invalid;
            }
        }
        const int maskVersion = layer.isGroup ? 6 : 4;
        if (layer.mask.has_value())
        {
            // Pixels arrive after manifest validation (ProjectStore attaches assets later), so the format check
            // applies once storage exists; the decoder rejects non-Gray8 masks on the way in.
            const Raster& maskImage = layer.mask.value().asset.image;
            if (formatVersion < maskVersion || (maskImage.HasStorage() && !LayerMask::IsValid(maskImage)))
            {
                return DocumentStatus::Invalid;
            }
            if (layer.mask.value().placement.has_value() &&
                (!layer.mask.value().placement.value().IsValid() || !layer.mask.has_value()))
            {
                return DocumentStatus::Invalid;
            }
            maskPixels += layer.mask.value().asset.image.PixelCount();
            if (maskPixels > kMaxMaskPixels)
            {
                return DocumentStatus::TooLarge;
            }
        }
        if (!std::isfinite(layer.opacity) || layer.opacity < 0 || layer.opacity > 1)
        {
            return DocumentStatus::Invalid;
        }
        if ((formatVersion < 3 || layer.isGroup) && (layer.opacity != 1 || layer.blendMode != LayerBlendMode::Normal))
        {
            return DocumentStatus::Invalid;
        }
        if (layer.asset.has_value())
        {
            imagePixels += layer.asset.value().image.PixelCount();
            if (imagePixels > kMaxLayerPixels)
            {
                return DocumentStatus::TooLarge;
            }
        }
        if (!ids.insert(layer.id).second || !layer.transform.IsValid())
        {
            return DocumentStatus::Invalid;
        }
        if (TrimWide(layer.name).empty() || !NameUtf8LengthOk(layer.name))
        {
            return DocumentStatus::Invalid;
        }
        if (formatVersion < 5 && layer.maskSourceID.has_value())
        {
            return DocumentStatus::Invalid;
        }
        if (formatVersion == 1 && (layer.parentID.has_value() || layer.isGroup))
        {
            return DocumentStatus::Invalid;
        }
    }
    const DocumentStatus hierarchy = ValidateHierarchy(document.layers);
    if (hierarchy != DocumentStatus::Ok)
    {
        return hierarchy;
    }
    return ValidateLiveMaskGraph(document.layers);
}

std::vector<HierarchyEntry> HierarchyEntries(const std::vector<ImageLayer>& layers, bool topFirst,
                                             const std::unordered_set<Guid>& collapsed)
{
    std::unordered_map<Guid, std::vector<std::size_t>> children;
    std::vector<std::size_t> roots;
    for (std::size_t i = 0; i < layers.size(); ++i)
    {
        if (layers[i].parentID.has_value())
        {
            children[layers[i].parentID.value()].push_back(i);
        }
        else
        {
            roots.push_back(i);
        }
    }
    std::vector<HierarchyEntry> result;
    const auto visit = [&](auto&& self, const std::optional<Guid>& parent, int depth, bool visible) -> void
    {
        if (depth > kMaxGroupDepth)
        {
            return;
        }
        std::vector<std::size_t> siblings = parent.has_value() ? children[parent.value()] : roots;
        if (topFirst)
        {
            std::reverse(siblings.begin(), siblings.end());
        }
        for (const std::size_t index : siblings)
        {
            const ImageLayer& layer = layers[index];
            const bool effective = visible && layer.visible;
            const bool isCollapsed = layer.isGroup && collapsed.find(layer.id) != collapsed.end();
            result.push_back(HierarchyEntry{layer.id, layer.name, depth, effective, layer.isGroup, isCollapsed,
                                            layer.mask.has_value(), !layer.mask.has_value() || layer.mask->enabled});
            if (layer.isGroup && !isCollapsed)
            {
                self(self, layer.id, depth + 1, effective);
            }
        }
    };
    visit(visit, std::nullopt, 0, true);
    return result;
}

std::vector<Guid> RenderLayerIds(const CanvasDocument& document)
{
    std::vector<Guid> ids;
    for (const HierarchyEntry& entry : HierarchyEntries(document.layers))
    {
        if (entry.visible && !entry.isGroup)
        {
            ids.push_back(entry.id);
        }
    }
    return ids;
}

std::unordered_set<Guid> DescendantIds(const std::vector<ImageLayer>& layers, const Guid& id)
{
    std::unordered_map<Guid, std::vector<Guid>> children;
    for (const ImageLayer& layer : layers)
    {
        if (layer.parentID.has_value())
        {
            children[layer.parentID.value()].push_back(layer.id);
        }
    }
    std::unordered_set<Guid> result;
    std::vector<Guid> pending{id};
    while (!pending.empty())
    {
        const Guid parent = pending.back();
        pending.pop_back();
        const auto found = children.find(parent);
        if (found == children.end())
        {
            continue;
        }
        for (const Guid& child : found->second)
        {
            if (result.insert(child).second)
            {
                pending.push_back(child);
            }
        }
    }
    return result;
}

const ImageLayer* DocumentSession::FindLayer(const Guid& id) const noexcept
{
    if (!_document.has_value())
    {
        return nullptr;
    }
    return LayerById(_document.value().layers, id);
}

ImageLayer* DocumentSession::MutableLayer(const Guid& id) noexcept
{
    if (!_document.has_value())
    {
        return nullptr;
    }
    return LayerById(_document.value().layers, id);
}

const ImageLayer* DocumentSession::ActiveLayer() const noexcept
{
    if (!_activeLayerID.has_value())
    {
        return nullptr;
    }
    return FindLayer(_activeLayerID.value());
}

ImageLayer* DocumentSession::MutableActiveLayer() noexcept
{
    if (!_activeLayerID.has_value())
    {
        return nullptr;
    }
    return MutableLayer(_activeLayerID.value());
}

void DocumentSession::BeginEdit(std::wstring name)
{
    history.Begin(std::move(name), _document, _activeLayerID);
}

void DocumentSession::EndEdit()
{
    history.End(_document, _activeLayerID);
    // Every document edit ends here, so the display copy kept while a transform or panel preview is pending never
    // outlives a change it did not see (opacity, blend mode, a moved, deleted or merged layer, a flip, a resize).
    NoteDocumentMutation();
}

std::optional<JobToken> DocumentSession::CaptureJobToken(const std::optional<Guid>& target,
                                                         std::uint64_t sequence) const
{
    if (!_document.has_value())
    {
        return std::nullopt;
    }
    JobToken token{_tokenIdentity.value, _documentSerial,  _generation,       _restoreSerial, std::nullopt,
                   _document->selection, _document->width, _document->height, sequence};
    if (target.has_value())
    {
        const ImageLayer* layer = FindLayer(target.value());
        if (layer == nullptr)
        {
            return std::nullopt;
        }
        token.target = TargetIdentity{layer->id,        layer->asset.has_value() ? layer->asset->image : Raster{},
                                      layer->transform, layer->mask,
                                      layer->isGroup,   layer->adjustment};
    }
    return token;
}

bool DocumentSession::CanApplyJobResult(const JobToken& token, std::uint64_t latestSequence) const noexcept
{
    if (!_document.has_value() || token.sessionIdentity != _tokenIdentity.value ||
        token.documentSerial != _documentSerial || token.restoreSerial != _restoreSerial ||
        token.sequence != latestSequence || token.canvasWidth != _document->width ||
        token.canvasHeight != _document->height)
    {
        return false;
    }
    if (!token.target.has_value())
    {
        return token.generation == _generation;
    }
    const TargetIdentity& target = token.target.value();
    const ImageLayer* layer = FindLayer(target.layerId);
    // A selection can have millions of edges. Validation compares the retained immutable handle in O(1), and
    // conservatively drops a rebuilt outline even when its points happen to be equal.
    const bool sameSelection =
        _document->selection.has_value() == token.selection.has_value() &&
        (!token.selection.has_value() || (_document->selection->antialiased == token.selection->antialiased &&
                                          _document->selection->loops.Identity() == token.selection->loops.Identity()));
    return layer != nullptr && (layer->asset.has_value() ? layer->asset->image : Raster{}) == target.image &&
           layer->transform == target.transform && layer->mask == target.mask && sameSelection &&
           layer->isGroup == target.isGroup && layer->adjustment == target.adjustment;
}

namespace
{
template <typename Excluded>
[[nodiscard]] std::int64_t BudgetLeft(const std::optional<CanvasDocument>& document, bool mask,
                                      const Excluded& excluded) noexcept
{
    if (!document.has_value())
    {
        return 0;
    }
    std::int64_t used = 0;
    for (const ImageLayer& layer : document->layers)
    {
        if (excluded(layer.id))
        {
            continue;
        }
        if (mask && layer.mask.has_value())
        {
            used += static_cast<std::int64_t>(layer.mask->asset.image.PixelCount());
        }
        if (!mask && layer.asset.has_value())
        {
            used += static_cast<std::int64_t>(layer.asset->image.PixelCount());
        }
    }
    return std::max<std::int64_t>(0, static_cast<std::int64_t>(mask ? kMaxMaskPixels : kMaxLayerPixels) - used);
}
} // namespace

std::int64_t DocumentSession::PixelBudgetLeft(bool mask, std::initializer_list<Guid> excluding) const noexcept
{
    return BudgetLeft(_document, mask, [&](const Guid& id)
                      { return std::find(excluding.begin(), excluding.end(), id) != excluding.end(); });
}

std::int64_t DocumentSession::PixelBudgetLeft(bool mask, const std::unordered_set<Guid>& excluding) const noexcept
{
    return BudgetLeft(_document, mask, [&](const Guid& id) { return excluding.contains(id); });
}

void DocumentSession::SetActiveLayerID(const std::optional<Guid>& id)
{
    if (id != _activeLayerID)
    {
        _maskSelected = false;
    }
    _activeLayerID = id;
    _selected.clear();
    if (id.has_value())
    {
        _selected.insert(id.value());
    }
    if (_maskSelected)
    {
        const ImageLayer* active = ActiveLayer();
        _maskSelected = active != nullptr && active->mask.has_value();
    }
}

void DocumentSession::Restore(DocumentHistory::Snapshot snapshot)
{
    CancelCrop();
    ++_restoreSerial;
    const bool keepMaskTarget = _maskSelected && _activeLayerID == snapshot.activeLayerID;
    if (_document.has_value() != snapshot.document.has_value() ||
        (_document.has_value() && _document->id != snapshot.document->id))
    {
        ++_documentSerial;
    }
    _document = std::move(snapshot.document);
    SetActiveLayerID(snapshot.activeLayerID);
    const ImageLayer* active = ActiveLayer();
    _maskSelected = keepMaskTarget && active != nullptr && active->mask.has_value();
    NoteDocumentMutation();
    _displayCopy.reset();
}

bool DocumentSession::CancelPendingEdit()
{
    _lassoDraft.reset();
    // The open panel owns the document until OK or Cancel. The key must not restore history under it.
    if (HasColorEdit())
    {
        return true;
    }
    // Pending edits are not history entries yet. The first Undo or Redo drops them, as Escape would.
    if (_gradient)
    {
        CancelGradient();
        return true;
    }
    if (_transformEdit.has_value())
    {
        CancelTransform();
        return true;
    }
    if (_brush)
    {
        CancelBrush();
        return true;
    }
    if (_warp)
    {
        CancelWarp();
        return true;
    }
    if (_pixelMove)
    {
        CancelPixelMove();
        return true;
    }
    if (_shapeDraft.has_value())
    {
        CancelShape();
        return true;
    }
    // An outline being moved holds its entry open, so history could not move anyway: it goes back where it began.
    if (_selectionMoveOrigin.has_value())
    {
        CancelSelectionMove();
        return true;
    }
    // A crop frame is not an edit in progress (the Crop tool always shows one): Undo and Redo drop it and still move
    // history, as legacy restore() cancels the crop.
    return false;
}

void DocumentSession::Undo()
{
    if (CancelPendingEdit())
    {
        return;
    }
    std::optional<DocumentHistory::Snapshot> snapshot = history.Undo();
    if (snapshot.has_value())
    {
        Restore(std::move(snapshot.value()));
    }
}

void DocumentSession::Redo()
{
    if (CancelPendingEdit())
    {
        return;
    }
    std::optional<DocumentHistory::Snapshot> snapshot = history.Redo();
    if (snapshot.has_value())
    {
        Restore(std::move(snapshot.value()));
    }
}

void DocumentSession::SelectLayer(const std::optional<Guid>& id)
{
    if ((_gradient && _gradient->deferred) || (_pixelMove && _pixelMove->deferred))
        return;
    if (id != _activeLayerID)
    {
        if (!ResolveGradient())
            return;
        CommitTransform();
    }
    if (!id.has_value() || FindLayer(id.value()) == nullptr)
    {
        SetActiveLayerID(std::nullopt);
        return;
    }
    SetActiveLayerID(id);
}

void DocumentSession::CreateDocument(int width, int height, bool emptyLayer)
{
    if ((_gradient && _gradient->deferred) || (_pixelMove && _pixelMove->deferred))
        return;
    if (width < 1 || width > kMaxCanvasSide || height < 1 || height > kMaxCanvasSide)
    {
        return;
    }
    if (!ResolveGradient())
        return;
    _lassoDraft.reset();
    BeginEdit(L"New Canvas");
    CanvasDocument document;
    document.id = Guid::Generate();
    document.width = width;
    document.height = height;
    std::optional<Guid> active;
    if (emptyLayer)
    {
        ImageLayer layer = ImageLayer::Blank(L"Layer 1", Vec2{static_cast<double>(width), static_cast<double>(height)});
        active = layer.id;
        document.layers.push_back(std::move(layer));
    }
    _document = std::move(document);
    ++_documentSerial;
    SetActiveLayerID(active);
    _collapsed.clear();
    EndEdit();
}

void DocumentSession::AddBlankLayer()
{
    if (!_document.has_value())
    {
        return;
    }
    const std::wstring name = NextLayerName();
    ImageLayer layer = ImageLayer::Blank(
        name, Vec2{static_cast<double>(_document.value().width), static_cast<double>(_document.value().height)});
    InsertLayerAboveActive(std::move(layer), L"New Blank Layer", false);
}

void DocumentSession::InsertLayerAboveActive(ImageLayer layer, const std::wstring& editName, bool dropsSelection)
{
    if ((_gradient && _gradient->deferred) || (_pixelMove && _pixelMove->deferred))
        return;
    if (!_document.has_value())
    {
        return;
    }
    // The new layer becomes the target: a pending gradient applies first, as any layer switch does.
    if (!ResolveGradient())
        return;
    const ImageLayer* active = ActiveLayer();
    if (active != nullptr)
    {
        layer.parentID = active->isGroup ? _activeLayerID : active->parentID;
    }
    if (layer.parentID.has_value())
    {
        _collapsed.erase(layer.parentID.value());
    }
    std::size_t insertion = _document.value().layers.size();
    if (_activeLayerID.has_value())
    {
        const std::optional<std::size_t> index = IndexOf(_document.value().layers, _activeLayerID.value());
        if (index.has_value())
        {
            insertion = index.value() + 1;
        }
    }
    if (active != nullptr && active->isGroup && _activeLayerID.has_value())
    {
        const std::unordered_set<Guid> inside = DescendantIds(_document.value().layers, _activeLayerID.value());
        for (std::size_t i = 0; i < _document.value().layers.size(); ++i)
        {
            if (inside.contains(_document.value().layers[i].id))
            {
                insertion = std::max(insertion, i + 1);
            }
        }
    }
    BeginEdit(editName);
    _document.value().layers.insert(_document.value().layers.begin() + static_cast<std::ptrdiff_t>(insertion), layer);
    if (dropsSelection)
    {
        _document.value().selection.reset();
    }
    SetActiveLayerID(layer.id);
    EndEdit();
}

std::wstring DocumentSession::UniqueLayerName(const std::wstring& prefix) const
{
    std::unordered_set<std::wstring> names;
    if (_document.has_value())
    {
        for (const ImageLayer& layer : _document->layers)
        {
            names.insert(layer.name);
        }
    }
    int number = 1;
    std::wstring name;
    do
    {
        name = prefix + std::to_wstring(number);
        ++number;
    } while (names.contains(name));
    return name;
}

void DocumentSession::AddGroup()
{
    if ((_gradient && _gradient->deferred) || (_pixelMove && _pixelMove->deferred))
        return;
    if (!ResolveGradient())
        return;
    if (!_document.has_value() || static_cast<int>(_document.value().layers.size()) >= kMaxLayerCount)
    {
        return;
    }
    const std::wstring name = UniqueLayerName(L"Folder ");
    ImageLayer group = ImageLayer::Blank(
        name, Vec2{static_cast<double>(_document.value().width), static_cast<double>(_document.value().height)});
    group.isGroup = true;
    const ImageLayer* active = ActiveLayer();
    group.parentID =
        (active != nullptr && active->isGroup) ? _activeLayerID : (active != nullptr ? active->parentID : std::nullopt);
    std::size_t insertion = _document.value().layers.size();
    if (_activeLayerID.has_value())
    {
        const std::optional<std::size_t> index = IndexOf(_document.value().layers, _activeLayerID.value());
        if (index.has_value())
        {
            insertion = index.value() + 1;
        }
    }
    std::vector<ImageLayer> layers = _document.value().layers;
    layers.insert(layers.begin() + static_cast<std::ptrdiff_t>(insertion), group);
    if (ValidateHierarchy(layers) != DocumentStatus::Ok)
    {
        return;
    }
    BeginEdit(L"New Folder");
    _document.value().layers = std::move(layers);
    SetActiveLayerID(group.id);
    if (group.parentID.has_value())
    {
        _collapsed.erase(group.parentID.value());
    }
    EndEdit();
}

void DocumentSession::RenameLayer(const Guid& id, std::wstring_view name)
{
    const std::wstring trimmed = TrimWide(name);
    if (trimmed.empty())
    {
        return;
    }
    ImageLayer* layer = MutableLayer(id);
    if (layer == nullptr)
    {
        return;
    }
    BeginEdit(L"Rename Layer");
    layer->name = trimmed;
    EndEdit();
}

void DocumentSession::ToggleVisibility(const Guid& id)
{
    ImageLayer* layer = MutableLayer(id);
    if (layer == nullptr)
    {
        return;
    }
    BeginEdit(layer->visible ? L"Hide Layer" : L"Show Layer");
    layer->visible = !layer->visible;
    EndEdit();
}

bool DocumentSession::CanPlaceLayer(const Guid& id, const std::optional<Guid>& parent) const
{
    if (!_document.has_value() || FindLayer(id) == nullptr)
    {
        return false;
    }
    if (!parent.has_value())
    {
        return true;
    }
    if (parent.value() == id)
    {
        return false;
    }
    if (DescendantIds(_document.value().layers, id).contains(parent.value()))
    {
        return false;
    }
    const ImageLayer* node = FindLayer(parent.value());
    return node != nullptr && node->isGroup;
}

bool DocumentSession::PlaceLayer(const Guid& id, const std::optional<Guid>& parent, const std::optional<Guid>& above,
                                 bool atBottom, bool before)
{
    if ((_gradient && _gradient->deferred) || (_pixelMove && _pixelMove->deferred))
        return false;
    if (!ResolveGradient())
        return false;
    const std::optional<Guid> destination = parent;
    const std::optional<Guid> aboveId = above;
    if (!CanPlaceLayer(id, destination) || !_document.has_value() || (aboveId.has_value() && aboveId.value() == id))
    {
        return false;
    }
    std::vector<ImageLayer> layers = _document.value().layers;
    const std::optional<std::size_t> index = IndexOf(layers, id);
    if (!index.has_value())
    {
        return false;
    }
    ImageLayer layer = layers[index.value()];
    layers.erase(layers.begin() + static_cast<std::ptrdiff_t>(index.value()));
    layer.parentID = destination;
    std::size_t insertion = atBottom ? 0 : layers.size();
    if (aboveId.has_value())
    {
        const std::optional<std::size_t> target = IndexOf(layers, aboveId.value());
        if (!target.has_value() || layers[target.value()].parentID != destination)
        {
            return false;
        }
        insertion = target.value() + (before ? 0 : 1);
    }
    layers.insert(layers.begin() + static_cast<std::ptrdiff_t>(insertion), std::move(layer));
    AdoptClipping(id, layers);
    ReleaseDetachedClipping(layers);
    if (ValidateHierarchy(layers) != DocumentStatus::Ok)
    {
        return false;
    }
    BeginEdit(L"Move Layer");
    _document.value().layers = std::move(layers);
    SetActiveLayerID(id);
    if (destination.has_value())
    {
        _collapsed.erase(destination.value());
    }
    EndEdit();
    return true;
}

void DocumentSession::MoveActiveLayerOutOfGroup()
{
    const ImageLayer* layer = ActiveLayer();
    if (layer == nullptr || !layer->parentID.has_value())
    {
        return;
    }
    const Guid layerId = layer->id;
    const Guid parentId = layer->parentID.value();
    const ImageLayer* group = FindLayer(parentId);
    if (group == nullptr)
    {
        return;
    }
    const std::optional<Guid> destination = group->parentID;
    const Guid above = group->id;
    (void)PlaceLayer(layerId, destination, above);
}

namespace
{
// The layers sharing `parent`, in document order (bottom to top).
[[nodiscard]] std::vector<Guid> SiblingIds(const std::vector<ImageLayer>& layers, const std::optional<Guid>& parent)
{
    std::vector<Guid> ids;
    for (const ImageLayer& layer : layers)
    {
        if (layer.parentID == parent)
        {
            ids.push_back(layer.id);
        }
    }
    return ids;
}

// `id`'s parents from the nearest up, then nullopt for the document root (legacy `ancestors`).
[[nodiscard]] std::vector<std::optional<Guid>> AncestorChain(const std::vector<ImageLayer>& layers, const Guid& id)
{
    std::vector<std::optional<Guid>> result;
    const ImageLayer* layer = LayerById(layers, id);
    std::optional<Guid> parent = layer != nullptr ? layer->parentID : std::nullopt;
    int guard = 0;
    while (parent.has_value() && guard++ <= kMaxGroupDepth)
    {
        result.push_back(parent);
        const ImageLayer* next = LayerById(layers, parent.value());
        parent = next != nullptr ? next->parentID : std::nullopt;
    }
    result.push_back(std::nullopt);
    return result;
}
} // namespace

bool DocumentSession::CanMoveActiveLayer(int offset) const
{
    const ImageLayer* active = ActiveLayer();
    if (active == nullptr || !_document.has_value() || offset == 0)
    {
        return false;
    }
    const std::vector<Guid> siblings = SiblingIds(_document.value().layers, active->parentID);
    const auto position = std::find(siblings.begin(), siblings.end(), active->id);
    if (position == siblings.end())
    {
        return false;
    }
    const std::ptrdiff_t index = (position - siblings.begin()) + offset;
    return index >= 0 && index < static_cast<std::ptrdiff_t>(siblings.size());
}

void DocumentSession::MoveActiveLayer(int offset)
{
    if (!CanMoveActiveLayer(offset))
    {
        return;
    }
    const ImageLayer* active = ActiveLayer();
    const std::vector<Guid> siblings = SiblingIds(_document.value().layers, active->parentID);
    const auto position = std::find(siblings.begin(), siblings.end(), active->id);
    const Guid other = siblings[static_cast<std::size_t>((position - siblings.begin()) + offset)];
    const std::optional<std::size_t> first = IndexOf(_document.value().layers, active->id);
    const std::optional<std::size_t> second = IndexOf(_document.value().layers, other);
    if (!first.has_value() || !second.has_value())
    {
        return;
    }
    BeginEdit(L"Reorder Layers");
    std::vector<ImageLayer>& layers = _document.value().layers;
    std::swap(layers[first.value()], layers[second.value()]);
    EndEdit();
}

void DocumentSession::GroupSelectedLayers()
{
    if ((_gradient && _gradient->deferred) || (_pixelMove && _pixelMove->deferred))
        return;
    if (!ResolveGradient())
        return;
    if (!_document.has_value() || static_cast<int>(_document.value().layers.size()) >= kMaxLayerCount)
    {
        return;
    }
    const std::vector<ImageLayer>& layers = _document.value().layers;
    std::unordered_set<Guid> selected;
    for (const Guid& id : _selected)
    {
        if (LayerById(layers, id) != nullptr)
        {
            selected.insert(id);
        }
    }
    // A selected folder carries its subtree; selected descendants must not be pulled out of it.
    std::unordered_set<Guid> rootIds;
    for (const Guid& id : selected)
    {
        bool inside = false;
        for (const std::optional<Guid>& ancestor : AncestorChain(layers, id))
        {
            if (ancestor.has_value() && selected.contains(ancestor.value()))
            {
                inside = true;
                break;
            }
        }
        if (!inside)
        {
            rootIds.insert(id);
        }
    }
    std::vector<Guid> ordered;
    for (const HierarchyEntry& entry : HierarchyEntries(layers))
    {
        if (rootIds.contains(entry.id))
        {
            ordered.push_back(entry.id);
        }
    }
    // The nearest ancestor every selected root shares (the document root when nothing is selected).
    std::optional<Guid> parent;
    if (!ordered.empty())
    {
        for (const std::optional<Guid>& candidate : AncestorChain(layers, ordered.front()))
        {
            bool common = true;
            for (const Guid& id : ordered)
            {
                const std::vector<std::optional<Guid>> chain = AncestorChain(layers, id);
                if (std::find(chain.begin(), chain.end(), candidate) == chain.end())
                {
                    common = false;
                    break;
                }
            }
            if (common)
            {
                parent = candidate;
                break;
            }
        }
    }
    const std::wstring name = UniqueLayerName(L"Folder ");
    ImageLayer group = ImageLayer::Blank(
        name, Vec2{static_cast<double>(_document.value().width), static_cast<double>(_document.value().height)});
    group.isGroup = true;
    group.parentID = parent;
    // The wrapper goes at the topmost selected branch in the common parent.
    std::vector<Guid> branches;
    for (const Guid& id : ordered)
    {
        Guid branch = id;
        int guard = 0;
        while (guard++ <= kMaxGroupDepth)
        {
            const ImageLayer* layer = LayerById(layers, branch);
            if (layer == nullptr || !layer->parentID.has_value() || layer->parentID == parent)
            {
                break;
            }
            branch = layer->parentID.value();
        }
        branches.push_back(branch);
    }
    std::optional<std::size_t> highest;
    for (std::size_t index = layers.size(); index-- > 0;)
    {
        if (std::find(branches.begin(), branches.end(), layers[index].id) != branches.end())
        {
            highest = index;
            break;
        }
    }
    std::size_t insertion = layers.size();
    if (highest.has_value())
    {
        insertion = 0;
        for (std::size_t index = 0; index <= highest.value(); ++index)
        {
            if (!rootIds.contains(layers[index].id))
            {
                ++insertion;
            }
        }
    }
    std::vector<ImageLayer> next;
    for (const ImageLayer& layer : layers)
    {
        if (!rootIds.contains(layer.id))
        {
            next.push_back(layer);
        }
    }
    next.insert(next.begin() + static_cast<std::ptrdiff_t>(std::min(insertion, next.size())), group);
    for (const Guid& id : ordered)
    {
        const ImageLayer* child = LayerById(layers, id);
        if (child != nullptr)
        {
            ImageLayer copy = *child;
            copy.parentID = group.id;
            next.push_back(std::move(copy));
        }
    }
    if (ValidateHierarchy(next) != DocumentStatus::Ok)
    {
        return;
    }
    BeginEdit(L"Group Layers");
    _document.value().layers = std::move(next);
    SetActiveLayerID(group.id);
    if (parent.has_value())
    {
        _collapsed.erase(parent.value());
    }
    EndEdit();
}

bool DocumentSession::CanToggleClippingMask(const Guid& id) const
{
    const ImageLayer* layer = FindLayer(id);
    if (layer == nullptr || layer->isGroup || !_document.has_value())
    {
        return false;
    }
    if (layer->maskSourceID.has_value())
    {
        return true;
    }
    const std::vector<Guid> siblings = SiblingIds(_document.value().layers, layer->parentID);
    const auto position = std::find(siblings.begin(), siblings.end(), id);
    if (position == siblings.end() || position == siblings.begin())
    {
        return false;
    }
    const ImageLayer* below = FindLayer(*(position - 1));
    if (below == nullptr || below->isGroup)
    {
        return false;
    }
    return CanLinkMask(below->maskSourceID.value_or(below->id), id);
}

void DocumentSession::ToggleClippingMask(const Guid& id)
{
    if (!CanToggleClippingMask(id))
    {
        return;
    }
    ImageLayer* layer = MutableLayer(id);
    if (layer == nullptr)
    {
        return;
    }
    if (layer->maskSourceID.has_value())
    {
        // Releasing a clipped layer releases the run of clipped layers stacked on it that share its base; the ones
        // below it stay clipped (legacy LiveLayerMask.removeLiveMask).
        const Guid source = layer->maskSourceID.value();
        const std::vector<Guid> siblings = SiblingIds(_document.value().layers, layer->parentID);
        std::vector<Guid> releases;
        for (auto sibling = std::find(siblings.begin(), siblings.end(), id); sibling != siblings.end(); ++sibling)
        {
            const ImageLayer* above = FindLayer(*sibling);
            if (above == nullptr || (!(*sibling == id) && above->maskSourceID != source))
            {
                break;
            }
            releases.push_back(*sibling);
        }
        BeginEdit(L"Release Clipping Mask");
        for (const Guid& release : releases)
        {
            if (ImageLayer* released = MutableLayer(release))
            {
                released->maskSourceID.reset();
            }
        }
        EndEdit();
        return;
    }
    // Clips to the next lower sibling, sharing its base when it is already clipped.
    const std::vector<Guid> siblings = SiblingIds(_document.value().layers, layer->parentID);
    const auto position = std::find(siblings.begin(), siblings.end(), id);
    const ImageLayer* below = FindLayer(*(position - 1));
    if (below == nullptr)
    {
        return;
    }
    (void)LinkMask(below->maskSourceID.value_or(below->id), id);
}

void DocumentSession::ClearDocument()
{
    _document.reset();
    ++_documentSerial;
    SetActiveLayerID(std::nullopt);
    _collapsed.clear();
    _transformEdit.reset();
    _transformDuplicate.reset();
    _cropRect.reset();
    _snapGuides = {};
    _maskSelected = false;
    NoteDocumentMutation();
    _displayCopy.reset();
    _lassoDraft.reset();
    _selectionMoveOrigin.reset();
    _brush.reset();
    _lastBrushPoint.reset();
    _pendingOpacityDigit.reset();
    _budgetRefused = false;
    _lastBrushCommit.reset();
    _cloneOffset.reset();
    _warp.reset();
    _warpSource = {};
    _gradient.reset();
    _shapeDraft.reset();
    _pixelMove.reset();
    _colorEdit.reset();
    _adjustmentEditingID.reset();
    _adjustmentOriginal.reset();
    history.Reset();
}

void DocumentSession::ReplaceDocument(CanvasDocument document, const std::optional<Guid>& active)
{
    if (ValidateDocument(document) != DocumentStatus::Ok)
    {
        return;
    }
    ClearDocument();
    _document = std::move(document);
    SetActiveLayerID(active.has_value() && FindLayer(active.value()) != nullptr ? active : std::nullopt);
    history.MarkSaved();
}

void DocumentSession::ApplyDocumentSize(CanvasDocument document, std::wstring name)
{
    if (!_document.has_value() || document.id != _document.value().id ||
        ValidateDocument(document) != DocumentStatus::Ok)
    {
        return;
    }
    _lassoDraft.reset();
    _selectionMoveOrigin.reset();
    BeginEdit(std::move(name));
    _document = std::move(document);
    EndEdit();
}

bool DocumentSession::ApplyDocumentSizeIfCurrent(CanvasDocument document, std::wstring name, const JobToken& token)
{
    if (!CanApplyJobResult(token, token.sequence) || document.id != _document->id ||
        ValidateDocument(document) != DocumentStatus::Ok)
    {
        return false;
    }
    _lassoDraft.reset();
    _selectionMoveOrigin.reset();
    BeginEdit(std::move(name));
    _document = std::move(document);
    EndEdit();
    return true;
}

bool DocumentSession::ApplyImportedDocument(CanvasDocument document, const std::optional<Guid>& active,
                                            std::wstring name)
{
    if (!_document.has_value() || document.id != _document->id || ValidateDocument(document) != DocumentStatus::Ok)
    {
        return false;
    }
    BeginEdit(std::move(name));
    _document = std::move(document);
    SetActiveLayerID(active.has_value() && FindLayer(active.value()) != nullptr ? active : std::nullopt);
    EndEdit();
    return true;
}

std::optional<DocumentSession::MergePlan> DocumentSession::PlanMerge() const
{
    const ImageLayer* active = ActiveLayer();
    if (!_document.has_value() || active == nullptr)
    {
        return std::nullopt;
    }
    const std::vector<ImageLayer>& layers = _document.value().layers;
    MergePlan plan;
    if (_selected.size() > 1)
    {
        std::unordered_set<Guid> picked = _selected;
        for (const Guid& id : _selected)
        {
            const std::unordered_set<Guid> below = DescendantIds(layers, id);
            picked.insert(below.begin(), below.end());
        }
        bool pixels = false;
        const ImageLayer* top = nullptr;
        for (const ImageLayer& layer : layers)
        {
            if (!picked.contains(layer.id))
            {
                continue;
            }
            plan.ids.push_back(layer.id);
            pixels = pixels || !layer.isGroup;
            if (_selected.contains(layer.id))
            {
                top = &layer;
            }
        }
        if (!pixels || top == nullptr)
        {
            return std::nullopt;
        }
        plan.removed = std::move(picked);
        plan.name = top->name;
        plan.parent = top->parentID;
        plan.anchor = top->id;
        plan.action = L"Merge Layers";
        return plan;
    }
    if (active->isGroup)
    {
        const std::unordered_set<Guid> inside = DescendantIds(layers, active->id);
        bool pixels = false;
        for (const ImageLayer& layer : layers)
        {
            if (inside.contains(layer.id) || layer.id == active->id)
            {
                plan.ids.push_back(layer.id);
                pixels = pixels || (inside.contains(layer.id) && !layer.isGroup);
            }
        }
        if (!pixels)
        {
            return std::nullopt;
        }
        plan.removed = std::unordered_set<Guid>(plan.ids.begin(), plan.ids.end());
        plan.name = active->name;
        plan.parent = active->parentID;
        plan.anchor = active->id;
        plan.action = L"Merge Group";
        return plan;
    }
    const std::optional<std::size_t> index = IndexOf(layers, active->id);
    if (!index.has_value())
    {
        return std::nullopt;
    }
    const ImageLayer* below = nullptr;
    for (std::size_t position = index.value(); position-- > 0;)
    {
        if (layers[position].parentID == active->parentID)
        {
            below = &layers[position];
            break;
        }
    }
    if (below == nullptr || below->isGroup)
    {
        return std::nullopt;
    }
    plan.ids = {below->id, active->id};
    plan.removed = {below->id, active->id};
    plan.name = below->name;
    plan.parent = active->parentID;
    plan.anchor = active->id;
    plan.action = L"Merge Down";
    return plan;
}

std::optional<DocumentSession::PreparedMerge> DocumentSession::PrepareMerge(const MergePlan& plan,
                                                                            const Raster& composite)
{
    return PrepareMerge(plan, composite, {});
}

std::optional<DocumentSession::PreparedMerge> DocumentSession::PrepareMerge(const MergePlan& plan,
                                                                            const Raster& composite,
                                                                            const CancelToken& cancellation)
{
    if (cancellation.IsCancelled() || !composite.HasStorage() || composite.Format() != RasterFormat::Bgra8888 ||
        plan.ids.empty())
    {
        return std::nullopt;
    }
    LayerTransform canvas;
    canvas.size = Vec2{static_cast<double>(composite.Width()), static_cast<double>(composite.Height())};
    const Adjust::Trimmed trimmed = Adjust::TrimmedToAlpha(composite, canvas, cancellation);
    if (!trimmed.image.HasStorage())
    {
        return std::nullopt;
    }
    ImageLayer merged = ImageLayer::Blank(plan.name, trimmed.transform.size);
    merged.transform = trimmed.transform;
    merged.asset = RasterAsset{trimmed.image, Raster::MakeThumbnail(trimmed.image), plan.name};
    merged.parentID = plan.parent;
    if (cancellation.IsCancelled())
        return std::nullopt;
    return PreparedMerge{std::move(merged), composite.Width(), composite.Height(), plan};
}

bool DocumentSession::ApplyPreparedMerge(const MergePlan& plan, const PreparedMerge& prepared)
{
    if (_gradient)
        return false; // owner apply cannot settle a raster kernel; the caller prepares after settlement
    const ImageLayer& preparedLayer = prepared._layer;
    if (!_document.has_value() || plan.ids.empty() || prepared._canvasWidth != _document.value().width ||
        prepared._canvasHeight != _document.value().height || prepared._plan.ids != plan.ids ||
        prepared._plan.removed != plan.removed || prepared._plan.name != plan.name ||
        prepared._plan.parent != plan.parent || prepared._plan.anchor != plan.anchor ||
        prepared._plan.action != plan.action || preparedLayer.name != plan.name ||
        preparedLayer.parentID != plan.parent || preparedLayer.isGroup || !preparedLayer.asset.has_value() ||
        !preparedLayer.asset.value().image.HasStorage() ||
        preparedLayer.asset.value().image.Format() != RasterFormat::Bgra8888 ||
        preparedLayer.asset.value().name != plan.name || !preparedLayer.visible || preparedLayer.opacity != 1.0 ||
        preparedLayer.blendMode != LayerBlendMode::Normal || preparedLayer.maskSourceID.has_value() ||
        preparedLayer.mask.has_value() || preparedLayer.adjustment.has_value() || preparedLayer.shape.has_value() ||
        preparedLayer.shapeImage.HasStorage() || !preparedLayer.transform.IsValid())
    {
        return false;
    }
    const std::vector<ImageLayer>& layers = _document.value().layers;
    std::unordered_set<Guid> planIds;
    planIds.reserve(plan.ids.size());
    for (const Guid& id : plan.ids)
    {
        if (!plan.removed.contains(id) || !planIds.insert(id).second || LayerById(layers, id) == nullptr)
        {
            return false;
        }
    }
    if (plan.removed.size() != planIds.size() || !plan.removed.contains(plan.anchor))
    {
        return false;
    }
    // Decision D7: the merged pixels fit the document's budget once the merged layers are gone (layers far apart
    // merge into their whole bounding box).
    if (static_cast<std::int64_t>(preparedLayer.asset.value().image.PixelCount()) >
        PixelBudgetLeft(false, plan.removed))
    {
        _budgetRefused = true;
        return false;
    }
    ImageLayer merged = preparedLayer;
    std::vector<ImageLayer> next;
    for (const ImageLayer& layer : layers)
    {
        if (!plan.removed.contains(layer.id))
        {
            next.push_back(layer);
        }
    }
    // Layers clipped to anything that was merged now clip to the result.
    for (ImageLayer& layer : next)
    {
        if (layer.maskSourceID.has_value() && plan.removed.contains(layer.maskSourceID.value()))
        {
            layer.maskSourceID = merged.id;
        }
    }
    std::size_t slot = layers.size();
    if (const std::optional<std::size_t> anchor = IndexOf(layers, plan.anchor); anchor.has_value())
    {
        slot = anchor.value();
    }
    std::size_t insertion = slot;
    for (std::size_t position = 0; position < slot && position < layers.size(); ++position)
    {
        if (plan.removed.contains(layers[position].id))
        {
            --insertion;
        }
    }
    next.insert(next.begin() + static_cast<std::ptrdiff_t>(std::min(insertion, next.size())), merged);
    if (ValidateHierarchy(next) != DocumentStatus::Ok || ValidateLiveMaskGraph(next) != DocumentStatus::Ok)
    {
        return false;
    }
    BeginEdit(plan.action);
    _document.value().layers = std::move(next);
    SetActiveLayerID(merged.id);
    EndEdit();
    return true;
}

bool DocumentSession::MergeLayers(const MergePlan& plan, const Raster& composite)
{
    if ((_gradient && _gradient->deferred) || (_pixelMove && _pixelMove->deferred))
        return false;
    if (!ResolveGradient())
        return false;
    const std::optional<PreparedMerge> prepared = PrepareMerge(plan, composite);
    return prepared.has_value() && ApplyPreparedMerge(plan, prepared.value());
}

void DocumentSession::SetCropFrame(std::optional<CropRect> rect) noexcept
{
    _cropRect = rect;
}

std::optional<CropRect> DocumentSession::VisibleCropRect() const noexcept
{
    if (!_document.has_value())
    {
        return std::nullopt;
    }
    if (_cropRect.has_value())
    {
        return _cropRect;
    }
    return CropRect{0, 0, static_cast<double>(_document.value().width), static_cast<double>(_document.value().height)};
}

std::optional<double> DocumentSession::CropRatio() const noexcept
{
    switch (cropRatioChoice)
    {
    case 1:
        if (_document.has_value())
        {
            return static_cast<double>(_document.value().width) / static_cast<double>(_document.value().height);
        }
        return std::nullopt;
    case 2:
        return 1.0;
    case 3:
        return 4.0 / 3.0;
    case 4:
        return 16.0 / 9.0;
    default:
        return std::nullopt;
    }
}

void DocumentSession::CancelCrop() noexcept
{
    _cropRect.reset();
}

void DocumentSession::ChangeCropRatio() noexcept
{
    const std::optional<CropRect> rect = VisibleCropRect();
    const std::optional<double> ratio = CropRatio();
    if (!rect.has_value() || !ratio.has_value() || !(ratio.value() > 0.0))
    {
        return;
    }
    const double height = rect->width / ratio.value();
    const CropRect next =
        CropGeometry::Snapped(CropRect{rect->MinX(), rect->MidY() - height / 2.0, rect->width, height});
    if (CropGeometry::Valid(next))
    {
        _cropRect = next;
    }
}

bool DocumentSession::CommitCrop()
{
    if ((_gradient && _gradient->deferred) || (_pixelMove && _pixelMove->deferred))
        return false;
    if (!ResolveGradient())
        return false;
    if (!_document.has_value() || !_cropRect.has_value() || !CropGeometry::Valid(_cropRect.value()))
    {
        return false;
    }
    // Whole pixels, so the content moves without resampling (Decision D6: a frame between pixels never truncates its
    // size or offsets the layers by a fraction).
    const CropRect rect = CropGeometry::Snapped(_cropRect.value());
    CanvasSizeOptions options;
    options.width = static_cast<int>(rect.width);
    options.height = static_cast<int>(rect.height);
    options.contentOffset = Vec2{-rect.MinX(), -rect.MinY()};
    CanvasDocument result;
    const ResizeStatus status = ResizeCanvas(_document.value(), options, result);
    _cropRect.reset();
    if (status == ResizeStatus::Ok)
    {
        ApplyDocumentSize(std::move(result), L"Crop");
        return true;
    }
    return status == ResizeStatus::Unchanged;
}

void DocumentSession::DeleteLayer(const Guid& id)
{
    if ((_gradient && _gradient->deferred) || (_pixelMove && _pixelMove->deferred))
        return;
    if (!ResolveGradient())
        return;
    if (!_document.has_value())
    {
        return;
    }
    const std::optional<std::size_t> index = IndexOf(_document.value().layers, id);
    if (!index.has_value())
    {
        return;
    }
    std::unordered_set<Guid> removed = DescendantIds(_document.value().layers, id);
    removed.insert(id);
    BeginEdit(L"Delete Layer");
    auto& layers = _document.value().layers;
    layers.erase(std::remove_if(layers.begin(), layers.end(),
                                [&](const ImageLayer& layer) { return removed.contains(layer.id); }),
                 layers.end());
    for (ImageLayer& layer : layers)
    {
        if (layer.maskSourceID.has_value() && removed.contains(layer.maskSourceID.value()))
        {
            layer.maskSourceID.reset();
        }
    }
    if (_activeLayerID.has_value() && removed.contains(_activeLayerID.value()))
    {
        SetActiveLayerID(layers.empty() ? std::nullopt
                                        : std::optional<Guid>(layers[std::min(index.value(), layers.size() - 1)].id));
    }
    EndEdit();
}

void DocumentSession::SetOpacity(double opacity)
{
    // An open panel owns the document (legacy canEditAppearance): an opacity would fold into its entry.
    ImageLayer* layer = MutableActiveLayer();
    if (layer == nullptr || layer->isGroup || !std::isfinite(opacity) || HasColorEdit())
    {
        return;
    }
    BeginEdit(L"Layer Opacity");
    layer->opacity = std::min(1.0, std::max(0.0, opacity));
    EndEdit();
}

bool DocumentSession::PreviewLayerOpacity(const Guid& id, double opacity) noexcept
{
    ImageLayer* layer = MutableLayer(id);
    if (layer == nullptr || layer->isGroup || !std::isfinite(opacity))
    {
        return false;
    }
    const double value = std::clamp(opacity, 0.0, 1.0);
    if (layer->opacity != value)
    {
        layer->opacity = value;
        NoteDocumentMutation();
    }
    return true;
}

void DocumentSession::SetBlendMode(LayerBlendMode mode)
{
    ImageLayer* layer = MutableActiveLayer();
    if (layer == nullptr || layer->isGroup)
    {
        return;
    }
    BeginEdit(L"Layer Blend Mode");
    layer->blendMode = mode;
    EndEdit();
}

bool DocumentSession::CanLinkMask(const Guid& source, const Guid& target) const
{
    if (!_document.has_value() || source == target)
    {
        return false;
    }
    const ImageLayer* sourceLayer = FindLayer(source);
    const ImageLayer* targetLayer = FindLayer(target);
    if (sourceLayer == nullptr || targetLayer == nullptr || sourceLayer->isGroup ||
        sourceLayer->adjustment.has_value() || targetLayer->isGroup)
    {
        return false;
    }
    std::vector<ImageLayer> layers = _document.value().layers;
    ImageLayer* writable = LayerById(layers, target);
    if (writable == nullptr)
    {
        return false;
    }
    writable->maskSourceID = source;
    return ValidateLiveMaskGraph(layers) == DocumentStatus::Ok;
}

bool DocumentSession::LinkMask(const Guid& source, const Guid& target)
{
    if (!CanLinkMask(source, target))
    {
        return false;
    }
    ImageLayer* layer = MutableLayer(target);
    if (layer == nullptr)
    {
        return false;
    }
    if (layer->maskSourceID.has_value() && layer->maskSourceID.value() == source)
    {
        return true;
    }
    BeginEdit(L"Create Clipping Mask");
    layer->maskSourceID = source;
    EndEdit();
    return true;
}

std::vector<HierarchyEntry> DocumentSession::LayerRows() const
{
    if (!_document.has_value())
    {
        return {};
    }
    return HierarchyEntries(_document.value().layers, true, _collapsed);
}

void DocumentSession::AdoptClipping(const Guid& id, std::vector<ImageLayer>& layers)
{
    const ImageLayer* layer = LayerById(layers, id);
    if (layer == nullptr || layer->isGroup)
    {
        return;
    }
    std::vector<const ImageLayer*> siblings;
    for (const ImageLayer& candidate : layers)
    {
        if (candidate.parentID == layer->parentID)
        {
            siblings.push_back(&candidate);
        }
    }
    std::size_t index = 0;
    bool found = false;
    for (std::size_t i = 0; i < siblings.size(); ++i)
    {
        if (siblings[i]->id == id)
        {
            index = i;
            found = true;
            break;
        }
    }
    if (!found || index == 0 || index + 1 >= siblings.size() || !siblings[index + 1]->maskSourceID.has_value() ||
        siblings[index + 1]->maskSourceID.value() == id)
    {
        return;
    }
    const Guid source = siblings[index + 1]->maskSourceID.value();
    const ImageLayer* below = siblings[index - 1];
    if (below->id != source && below->maskSourceID != source)
    {
        return;
    }
    ImageLayer* writable = LayerById(layers, id);
    if (writable != nullptr)
    {
        writable->maskSourceID = source;
    }
}

void DocumentSession::ReleaseDetachedClipping(std::vector<ImageLayer>& layers)
{
    std::vector<std::size_t> roots;
    std::unordered_map<Guid, std::vector<std::size_t>> grouped;
    for (std::size_t i = 0; i < layers.size(); ++i)
    {
        if (layers[i].parentID.has_value())
        {
            grouped[layers[i].parentID.value()].push_back(i);
        }
        else
        {
            roots.push_back(i);
        }
    }
    std::unordered_set<Guid> release;
    const auto mark = [&](const std::vector<std::size_t>& stack)
    {
        std::optional<Guid> base;
        for (const std::size_t index : stack)
        {
            ImageLayer& layer = layers[index];
            if (layer.maskSourceID.has_value())
            {
                if (!base.has_value() || layer.maskSourceID.value() != base.value())
                {
                    release.insert(layer.id);
                    base = layer.id;
                }
            }
            else
            {
                base = layer.isGroup ? std::nullopt : std::optional<Guid>(layer.id);
            }
        }
    };
    mark(roots);
    for (const auto& stack : grouped)
    {
        mark(stack.second);
    }
    for (ImageLayer& layer : layers)
    {
        if (release.contains(layer.id))
        {
            layer.maskSourceID.reset();
        }
    }
}

double MaskEdgeBackground(const Raster& thumbnail) noexcept
{
    if (!thumbnail.HasStorage() || thumbnail.Format() != RasterFormat::Gray8 || thumbnail.Data() == nullptr)
    {
        return 1.0;
    }
    const int width = thumbnail.Width();
    const int height = thumbnail.Height();
    const int stride = thumbnail.Stride();
    const std::uint8_t* data = thumbnail.Data();
    std::uint64_t total = 0;
    std::uint64_t count = 0;
    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            if (y == 0 || y == height - 1 || x == 0 || x == width - 1)
            {
                total +=
                    data[static_cast<std::size_t>(y) * static_cast<std::size_t>(stride) + static_cast<std::size_t>(x)];
                ++count;
            }
        }
    }
    return total * 2 >= count * 255 ? 1.0 : 0.0;
}

// ── Transform editing (C8) ───────────────────────────────────────────────

namespace
{
constexpr double kTransformPi = 3.14159265358979323846;

[[nodiscard]] std::unordered_set<Guid> EffectiveVisibleIDs(const CanvasDocument& document)
{
    std::unordered_set<Guid> ids;
    for (const HierarchyEntry& entry : HierarchyEntries(document.layers))
    {
        if (entry.visible)
        {
            ids.insert(entry.id);
        }
    }
    return ids;
}

// A linked mask placed apart from its layer under a distortion that takes the layer from `from` to `transform` and
// that transform's corners to `corners`: carried along with the layer, then warped by the same perspective over its
// own bounds (legacy Distort.distort). Nullopt when the carried shape is unusable (the placement then stays).
[[nodiscard]] std::optional<DistortWarp::Warped> DistortedPlacedMask(const LayerMask& mask, const LayerTransform& from,
                                                                     const LayerTransform& transform,
                                                                     const std::array<Vec2, 4>& corners, double limit)
{
    if (!mask.linked || !mask.placement.has_value())
    {
        return std::nullopt;
    }
    const LayerTransform placement = mask.placement->Following(from, transform);
    const std::array<Vec2, 4> carried = DistortWarp::Carried(placement, transform, corners);
    if (!DistortWarp::IsUsable(carried))
    {
        return std::nullopt;
    }
    return DistortWarp::WarpMask(mask.asset.image, placement, carried, MaskEdgeBackground(mask.asset.thumbnail), limit);
}

} // namespace

SelectionBounds PointBounds(std::span<const Vec2> points) noexcept
{
    SelectionBounds bounds{points[0].x, points[0].y, points[0].x, points[0].y};
    for (const Vec2& point : points)
    {
        bounds.minX = std::min(bounds.minX, point.x);
        bounds.minY = std::min(bounds.minY, point.y);
        bounds.maxX = std::max(bounds.maxX, point.x);
        bounds.maxY = std::max(bounds.maxY, point.y);
    }
    return bounds;
}

std::optional<SelectionBounds> LoopsBounds(const std::vector<SelectionLoop>& loops) noexcept
{
    std::optional<SelectionBounds> bounds;
    for (const SelectionLoop& loop : loops)
    {
        for (const Vec2& point : loop)
        {
            if (!std::isfinite(point.x) || !std::isfinite(point.y))
            {
                continue;
            }
            if (!bounds.has_value())
            {
                bounds = SelectionBounds{point.x, point.y, point.x, point.y};
                continue;
            }
            bounds->minX = std::min(bounds->minX, point.x);
            bounds->minY = std::min(bounds->minY, point.y);
            bounds->maxX = std::max(bounds->maxX, point.x);
            bounds->maxY = std::max(bounds->maxY, point.y);
        }
    }
    return bounds;
}

Affine2 Affine2::Rotation(double radians) noexcept
{
    const double s = std::sin(radians);
    const double co = std::cos(radians);
    return Affine2{co, s, -s, co, 0, 0};
}

Affine2 Affine2::Concat(const Affine2& o) const noexcept
{
    return Affine2{a * o.a + b * o.c, a * o.b + b * o.d,          c * o.a + d * o.c,
                   c * o.b + d * o.d, tx * o.a + ty * o.c + o.tx, tx * o.b + ty * o.d + o.ty};
}

Affine2 Affine2::Inverted() const noexcept
{
    const double determinant = a * d - b * c;
    if (!std::isfinite(determinant) || std::abs(determinant) < 1e-300)
    {
        return Affine2{};
    }
    Affine2 result;
    result.a = d / determinant;
    result.b = -b / determinant;
    result.c = -c / determinant;
    result.d = a / determinant;
    result.tx = (c * ty - d * tx) / determinant;
    result.ty = (b * tx - a * ty) / determinant;
    return result;
}

double LayerTransform::Radians() const noexcept
{
    return std::fmod(rotation, 360.0) * kTransformPi / 180.0;
}

Vec2 LayerTransform::Point(Vec2 unit) const noexcept
{
    const double x = (unit.x - 0.5) * size.x;
    const double y = (unit.y - 0.5) * size.y;
    const double radians = Radians();
    const Vec2 center = Center();
    return Vec2{center.x + x * std::cos(radians) - y * std::sin(radians),
                center.y + x * std::sin(radians) + y * std::cos(radians)};
}

bool LayerTransform::Contains(Vec2 point) const noexcept
{
    const Vec2 center = Center();
    const double x = point.x - center.x;
    const double y = point.y - center.y;
    const double radians = Radians();
    return std::abs(x * std::cos(radians) + y * std::sin(radians)) <= size.x / 2.0 &&
           std::abs(-x * std::sin(radians) + y * std::cos(radians)) <= size.y / 2.0;
}

std::array<Vec2, 4> LayerTransform::Corners() const noexcept
{
    return {Point(Vec2{0, 0}), Point(Vec2{1, 0}), Point(Vec2{1, 1}), Point(Vec2{0, 1})};
}

double LayerTransform::ScalePercent(Vec2 pixelSize) const noexcept
{
    return size.x / std::max(1.0, pixelSize.x) * 100.0;
}

LayerTransform LayerTransform::ScaledToPercent(double percent, Vec2 pixelSize) const noexcept
{
    LayerTransform result = *this;
    const Vec2 center = Center();
    result.size = Vec2{pixelSize.x * percent / 100.0, pixelSize.y * percent / 100.0};
    result.origin = Vec2{center.x - result.size.x / 2.0, center.y - result.size.y / 2.0};
    return result;
}

LayerTransform LayerTransform::Rounded() const noexcept
{
    LayerTransform result = *this;
    result.origin = Vec2{std::round(origin.x), std::round(origin.y)};
    result.size = Vec2{std::max(1.0, std::round(size.x)), std::max(1.0, std::round(size.y))};
    result.rotation = std::round(rotation);
    return result;
}

Affine2 LayerTransform::UnitToDocument() const noexcept
{
    const Vec2 center = Center();
    return Affine2::Translation(-0.5, -0.5)
        .Concat(Affine2::Scale(size.x * (flipX ? -1.0 : 1.0), size.y * (flipY ? -1.0 : 1.0)))
        .Concat(Affine2::Rotation(Radians()))
        .Concat(Affine2::Translation(center.x, center.y));
}

LayerTransform LayerTransform::Placing(const Affine2& map) const noexcept
{
    // Keep this transform's horizontal flip and the rotation nearest its own, so the numbers stay familiar.
    const double sign = flipX ? -1.0 : 1.0;
    const double angle = std::atan2(map.b * sign, map.a * sign);
    const double along = -map.c * std::sin(angle) + map.d * std::cos(angle);
    const Vec2 middle = map.Apply(Vec2{0.5, 0.5});
    LayerTransform result = *this;
    result.size = Vec2{std::hypot(map.a, map.b), std::abs(along)};
    const double degrees = angle * 180.0 / kTransformPi;
    result.rotation = degrees + std::round((rotation - degrees) / 360.0) * 360.0;
    result.flipY = along < 0;
    result.origin = Vec2{middle.x - result.size.x / 2.0, middle.y - result.size.y / 2.0};
    return result;
}

LayerTransform LayerTransform::Following(const LayerTransform& from, const LayerTransform& to) const noexcept
{
    if (from == to)
    {
        return *this;
    }
    // A plain move carries exactly.
    if (from.size == to.size && from.rotation == to.rotation && from.flipX == to.flipX && from.flipY == to.flipY)
    {
        LayerTransform moved = *this;
        moved.origin.x += to.origin.x - from.origin.x;
        moved.origin.y += to.origin.y - from.origin.y;
        return moved;
    }
    return Placing(UnitToDocument().Concat(from.UnitToDocument().Inverted()).Concat(to.UnitToDocument()));
}

bool LayerTransform::SamePlacement(const LayerTransform& other) const noexcept
{
    LayerTransform copy = *this;
    copy.sampling = other.sampling;
    return copy == other;
}

LayerTransform LayerTransform::Mirrored(bool horizontally, double axis) const noexcept
{
    LayerTransform result = *this;
    const Vec2 center = Center();
    if (horizontally)
    {
        result.flipX = !flipX;
        result.origin.x = 2.0 * axis - center.x - size.x / 2.0;
    }
    else
    {
        result.flipY = !flipY;
        result.origin.y = 2.0 * axis - center.y - size.y / 2.0;
    }
    result.rotation = -rotation;
    return result;
}

std::optional<LayerTransform> MaskPlacementMoving(const LayerMask& mask, const LayerTransform& from,
                                                  const LayerTransform& to) noexcept
{
    // A uniform mask looks the same wherever it sits.
    if (mask.asset.image.Width() <= 1 && mask.asset.image.Height() <= 1)
    {
        return std::nullopt;
    }
    std::optional<LayerTransform> moved;
    if (mask.linked)
    {
        if (mask.placement.has_value())
        {
            moved = mask.placement.value().Following(from, to);
        }
    }
    else
    {
        moved = mask.placement.has_value() ? mask.placement.value() : from;
    }
    if (!moved.has_value() || moved.value().SamePlacement(to))
    {
        return std::nullopt;
    }
    return moved;
}

bool DocumentSession::CanTransform() const noexcept
{
    // An open color or filter panel owns the layer until OK or Cancel (its edit is anchored to the layer's
    // placement), so moves, transforms, nudges and flips wait.
    if (!_document.has_value() || HasColorEdit())
    {
        return false;
    }
    if (TransformsAsGroup())
    {
        return !GroupTransformMemberIDs().empty();
    }
    const ImageLayer* layer = ActiveLayer();
    if (layer == nullptr || !layer->asset.has_value() || layer->isGroup)
    {
        return false;
    }
    return EffectiveVisibleIDs(_document.value()).contains(layer->id);
}

bool DocumentSession::TransformsAsGroup() const noexcept
{
    if (_selected.size() > 1)
    {
        return true;
    }
    const ImageLayer* layer = ActiveLayer();
    return _selected.size() == 1 && layer != nullptr && layer->isGroup;
}

std::vector<Guid> DocumentSession::GroupTransformMemberIDs() const
{
    std::vector<Guid> ids;
    if (!TransformsAsGroup() || !_document.has_value())
    {
        return ids;
    }
    const CanvasDocument& document = _document.value();
    const std::unordered_set<Guid> visible = EffectiveVisibleIDs(document);
    for (const ImageLayer& layer : document.layers)
    {
        if (!layer.asset.has_value() || layer.isGroup || !visible.contains(layer.id))
        {
            continue;
        }
        if (_selected.contains(layer.id))
        {
            ids.push_back(layer.id);
            continue;
        }
        for (const std::optional<Guid>& ancestor : AncestorChain(document.layers, layer.id))
        {
            if (ancestor.has_value() && _selected.contains(ancestor.value()))
            {
                ids.push_back(layer.id);
                break;
            }
        }
    }
    return ids;
}

std::optional<LayerTransform> DocumentSession::GroupTransformBox() const
{
    std::optional<SelectionBounds> bounds;
    for (const Guid& id : GroupTransformMemberIDs())
    {
        const ImageLayer* layer = FindLayer(id);
        if (layer == nullptr)
        {
            continue;
        }
        const SelectionBounds layerBounds = PointBounds(layer->transform.Corners());
        if (!bounds.has_value())
        {
            bounds = layerBounds;
            continue;
        }
        bounds->minX = std::min(bounds->minX, layerBounds.minX);
        bounds->minY = std::min(bounds->minY, layerBounds.minY);
        bounds->maxX = std::max(bounds->maxX, layerBounds.maxX);
        bounds->maxY = std::max(bounds->maxY, layerBounds.maxY);
    }
    if (!bounds.has_value())
    {
        return std::nullopt;
    }
    LayerTransform box;
    box.origin = Vec2{bounds->minX, bounds->minY};
    box.size = Vec2{std::max(1.0, bounds->maxX - bounds->minX), std::max(1.0, bounds->maxY - bounds->minY)};
    return box;
}

void DocumentSession::BeginTransform(bool persistent)
{
    if ((_gradient && _gradient->deferred) || (_pixelMove && _pixelMove->deferred))
        return;
    if (!_transformEdit.has_value())
    {
        if (!ResolveGradient())
            return;
    }
    if (_transformEdit.has_value() || !CanTransform())
    {
        return;
    }
    const ImageLayer* layer = ActiveLayer();
    if (layer == nullptr)
    {
        return;
    }
    if (TransformsAsGroup())
    {
        const std::optional<LayerTransform> box = GroupTransformBox();
        if (!box.has_value())
        {
            return;
        }
        TransformGroup group;
        group.box = box.value();
        for (const Guid& id : GroupTransformMemberIDs())
        {
            if (const ImageLayer* member = FindLayer(id))
            {
                group.originals.emplace_back(id, member->transform);
            }
        }
        _transformEdit = TransformEdit{layer->id, box.value(), persistent, std::nullopt, false, std::move(group)};
    }
    else
    {
        // An unlinked mask, when selected, transforms on its own; linked, layer and mask move together.
        const bool maskAlone = _maskSelected && layer->mask.has_value() && !layer->mask.value().linked;
        _transformEdit = TransformEdit{layer->id,  maskAlone ? MaskTransform(*layer) : layer->transform,
                                       persistent, std::nullopt,
                                       maskAlone,  std::nullopt};
    }
    ++_editRevision;
}

void DocumentSession::PreviewTransform(const LayerTransform& value)
{
    if (!value.IsValid() || !_transformEdit.has_value())
    {
        return;
    }
    _transformEdit->draft = value;
    ++_editRevision;
}

void DocumentSession::BeginDistort()
{
    if (!_transformEdit.has_value() || _transformEdit->corners.has_value() || !_transformEdit->draft.IsValid())
    {
        return;
    }
    _transformEdit->corners = _transformEdit->draft.Corners();
    _transformEdit->persistent = true;
    ++_editRevision;
}

void DocumentSession::PreviewCorners(const std::array<Vec2, 4>& corners)
{
    if (!_transformEdit.has_value() || !_transformEdit->corners.has_value() || !DistortWarp::IsUsable(corners))
    {
        return;
    }
    _transformEdit->corners = corners;
    ++_editRevision;
}

void DocumentSession::BeginDuplicateTransform()
{
    if ((_gradient && _gradient->deferred) || (_pixelMove && _pixelMove->deferred))
        return;
    // Alt-drag duplicates a single layer; several selected, or a folder, just move.
    if (_transformDuplicate.has_value() || TransformsAsGroup() || !_activeLayerID.has_value())
    {
        return;
    }
    const Guid source = _activeLayerID.value();
    CommitTransform();
    if (!ResolveGradient())
        return;
    if (!CanTransform())
    {
        return;
    }
    BeginEdit(L"Duplicate Layer");
    DuplicateActiveLayer();
    if (!_activeLayerID.has_value() || _activeLayerID.value() == source)
    {
        EndEdit();
        return;
    }
    _transformDuplicate = std::make_pair(_activeLayerID.value(), source);
    BeginTransform(false);
}

void DocumentSession::CommitTransform()
{
    _snapGuides = {};
    if (!_transformEdit.has_value())
    {
        return;
    }
    const TransformEdit edit = _transformEdit.value();
    _transformEdit.reset();
    _displayCopy.reset();
    ++_editRevision;
    const auto finishDuplicate = [this]
    {
        if (_transformDuplicate.has_value())
        {
            _transformDuplicate.reset();
            EndEdit();
        }
    };
    if (!_document.has_value())
    {
        finishDuplicate();
        return;
    }
    if (edit.floating.has_value())
    {
        // Unchanged: restore exactly, so soft selection edges never pick up a seam.
        if (edit.draft == edit.floating->original && !edit.corners.has_value())
        {
            CancelFloating(edit.floating.value());
        }
        else
        {
            MergeFloating(edit, edit.floating.value());
        }
        finishDuplicate();
        return;
    }
    std::vector<ImageLayer>& layers = _document.value().layers;
    if (edit.mask)
    {
        const std::optional<std::size_t> index = IndexOf(layers, edit.layerID);
        if (edit.draft.IsValid() && index.has_value() && layers[index.value()].mask.has_value())
        {
            ImageLayer& layer = layers[index.value()];
            if (edit.corners.has_value())
            {
                // A distorted mask is resampled into the shape, its background outside it.
                const std::optional<DistortWarp::Warped> moved =
                    DistortWarp::WarpMask(layer.mask->asset.image, edit.draft, edit.corners.value(),
                                          MaskEdgeBackground(layer.mask->asset.thumbnail));
                if (moved.has_value())
                {
                    BeginEdit(L"Distort Layer Mask");
                    if (!(moved->image == layer.mask->asset.image))
                    {
                        layer.mask->asset.image = moved->image;
                        layer.mask->asset.thumbnail = Raster::MakeThumbnail(moved->image);
                    }
                    layer.mask->placement = moved->transform.SamePlacement(layer.transform)
                                                ? std::nullopt
                                                : std::optional<LayerTransform>(moved->transform);
                    EndEdit();
                }
            }
            else
            {
                const std::optional<LayerTransform> placement = edit.draft.SamePlacement(layer.transform)
                                                                    ? std::nullopt
                                                                    : std::optional<LayerTransform>(edit.draft);
                if (placement != layer.mask->placement)
                {
                    BeginEdit(L"Transform Layer Mask");
                    layer.mask->placement = placement;
                    EndEdit();
                }
            }
        }
        finishDuplicate();
        return;
    }
    if (edit.corners.has_value())
    {
        BeginEdit(edit.group.has_value() ? L"Distort Layers" : L"Distort");
        std::vector<Guid> ids;
        if (edit.group.has_value())
        {
            for (const auto& [id, original] : edit.group->originals)
            {
                ids.push_back(id);
            }
        }
        else
        {
            ids.push_back(edit.layerID);
        }
        // Decision D7: the distorted layers' new pixels (and masks) fit the document's budget, their old pixels no
        // longer counted; a layer that would not fit keeps its own and the refusal is reported.
        const std::unordered_set<Guid> distorted(ids.begin(), ids.end());
        std::int64_t imageLeft = PixelBudgetLeft(false, distorted);
        std::int64_t maskLeft = PixelBudgetLeft(true, distorted);
        for (const Guid& id : ids)
        {
            const std::optional<std::size_t> index = IndexOf(layers, id);
            if (!index.has_value())
            {
                continue;
            }
            ImageLayer& layer = layers[index.value()];
            // A layer the distortion leaves as it is still holds its pixels against the budget.
            const auto keep = [&]
            {
                imageLeft -= layer.asset.has_value() ? static_cast<std::int64_t>(layer.asset->image.PixelCount()) : 0;
                maskLeft -=
                    layer.mask.has_value() ? static_cast<std::int64_t>(layer.mask->asset.image.PixelCount()) : 0;
            };
            if (!layer.asset.has_value())
            {
                keep();
                continue;
            }
            LayerTransform transform = edit.draft;
            std::array<Vec2, 4> corners = edit.corners.value();
            if (edit.group.has_value())
            {
                const auto original = std::find_if(edit.group->originals.begin(), edit.group->originals.end(),
                                                   [&](const auto& entry) { return entry.first == id; });
                if (original == edit.group->originals.end())
                {
                    keep();
                    continue;
                }
                transform = original->second.Following(edit.group->box, edit.draft);
                corners = DistortWarp::Carried(transform, edit.draft, edit.corners.value());
                if (!DistortWarp::IsUsable(corners))
                {
                    keep();
                    continue;
                }
            }
            const std::optional<DistortWarp::WarpedTrimmed> warped =
                DistortWarp::WarpTrimmed(layer.asset->image, transform, corners);
            if (!warped.has_value())
            {
                keep();
                continue;
            }
            // The mask as the distortion leaves it (legacy Distort.distort), built apart so the budget can refuse.
            std::optional<LayerMask> mask = layer.mask;
            if (mask.has_value() && !mask->placement.has_value() && mask->linked)
            {
                // A uniform mask passes through; any other is warped and cropped with the pixels.
                const std::optional<DistortWarp::Warped> warpedMask =
                    DistortWarp::Warp(mask->asset.image, transform, corners, true);
                if (warpedMask.has_value() && !(warpedMask->image == mask->asset.image))
                {
                    // The layer's warp and the mask's share the same corners and no preview limit, so they land at
                    // the same size: the layer's alpha crop always lies inside the mask's untrimmed warped image.
                    const Raster cropped = warpedMask->image.Crop(warped->cropX, warped->cropY, warped->image.Width(),
                                                                  warped->image.Height(), 0);
                    if (cropped.HasStorage())
                    {
                        mask->asset.image = cropped;
                        mask->asset.thumbnail = Raster::MakeThumbnail(cropped);
                    }
                }
            }
            else if (mask.has_value() && mask->linked)
            {
                // A linked mask placed apart takes the same perspective over its own bounds.
                if (const std::optional<DistortWarp::Warped> moved =
                        DistortedPlacedMask(mask.value(), layer.transform, transform, corners, 0.0);
                    moved.has_value())
                {
                    if (!(moved->image == mask->asset.image))
                    {
                        mask->asset.image = moved->image;
                        mask->asset.thumbnail = Raster::MakeThumbnail(moved->image);
                    }
                    mask->placement = moved->transform;
                }
            }
            else if (mask.has_value() && !mask->placement.has_value())
            {
                // An unlinked mask keeps its place on the document while its layer is resampled into the shape.
                mask->placement = layer.transform;
            }
            // Decision D7: the new pixels and mask fit the document's budget, or the layer keeps its own.
            const std::int64_t newPixels = static_cast<std::int64_t>(warped->image.PixelCount());
            const std::int64_t newMaskPixels =
                mask.has_value() ? static_cast<std::int64_t>(mask->asset.image.PixelCount()) : 0;
            if (newPixels > imageLeft || newMaskPixels > maskLeft)
            {
                _budgetRefused = true;
                keep();
                continue;
            }
            imageLeft -= newPixels;
            maskLeft -= newMaskPixels;
            layer.mask = std::move(mask);
            layer.asset->image = warped->image;
            layer.asset->thumbnail = Raster::MakeThumbnail(warped->image);
            layer.transform = warped->transform;
        }
        EndEdit();
        finishDuplicate();
        return;
    }
    if (edit.group.has_value())
    {
        if (edit.draft.IsValid())
        {
            BeginEdit(L"Transform Layers");
            for (const auto& [id, original] : edit.group->originals)
            {
                const std::optional<std::size_t> index = IndexOf(layers, id);
                if (!index.has_value())
                {
                    continue;
                }
                const LayerTransform moved = original.Following(edit.group->box, edit.draft);
                if (!moved.IsValid())
                {
                    continue;
                }
                ImageLayer& layer = layers[index.value()];
                if (layer.mask.has_value())
                {
                    layer.mask->placement = MaskPlacementMoving(layer.mask.value(), original, moved);
                }
                layer.transform = moved;
                RedrawShape(index.value());
            }
            EndEdit();
        }
        finishDuplicate();
        return;
    }
    const std::optional<std::size_t> index = IndexOf(layers, edit.layerID);
    if (edit.draft.IsValid() && index.has_value())
    {
        BeginEdit(L"Transform Layer");
        ImageLayer& layer = layers[index.value()];
        if (layer.mask.has_value())
        {
            layer.mask->placement = MaskPlacementMoving(layer.mask.value(), layer.transform, edit.draft);
        }
        layer.transform = edit.draft;
        RedrawShape(index.value());
        EndEdit();
    }
    finishDuplicate();
}

void DocumentSession::CancelTransform()
{
    _snapGuides = {};
    if (!_transformEdit.has_value())
    {
        return;
    }
    const std::optional<FloatingTransform> floating = _transformEdit->floating;
    _transformEdit.reset();
    _displayCopy.reset();
    ++_editRevision;
    if (_transformDuplicate.has_value() && _document.has_value())
    {
        const auto [copy, source] = _transformDuplicate.value();
        std::vector<ImageLayer>& layers = _document.value().layers;
        layers.erase(
            std::remove_if(layers.begin(), layers.end(), [&](const ImageLayer& layer) { return layer.id == copy; }),
            layers.end());
        SetActiveLayerID(source);
        _transformDuplicate.reset();
        EndEdit();
    }
    if (floating.has_value())
    {
        CancelFloating(floating.value());
    }
}

void DocumentSession::NudgeLayer(double dx, double dy)
{
    const bool alreadyEditing = _transformEdit.has_value();
    if (!alreadyEditing)
    {
        BeginTransform(false);
    }
    if (!_transformEdit.has_value())
    {
        return;
    }
    LayerTransform value = _transformEdit->draft;
    value.origin.x += dx;
    value.origin.y += dy;
    PreviewTransform(value);
    if (_transformEdit->corners.has_value())
    {
        std::array<Vec2, 4> corners = _transformEdit->corners.value();
        for (Vec2& corner : corners)
        {
            corner.x += dx;
            corner.y += dy;
        }
        PreviewCorners(corners);
    }
    if (!alreadyEditing)
    {
        CommitTransform();
    }
}

LayerTransform DocumentSession::EditedTransform(const ImageLayer& layer) const
{
    if (_transformEdit.has_value() && _transformEdit->layerID == layer.id)
    {
        return _transformEdit->draft;
    }
    if (!_transformEdit.has_value() && _activeLayerID == layer.id && TransformsAsGroup())
    {
        if (const std::optional<LayerTransform> box = GroupTransformBox())
        {
            return box.value();
        }
    }
    return _activeLayerID == layer.id && TransformTargetsMask() ? MaskTransform(layer) : layer.transform;
}

std::optional<LayerTransform> DocumentSession::PendingTransform(const ImageLayer& layer) const
{
    if (!_transformEdit.has_value() || _transformEdit->mask)
    {
        return std::nullopt;
    }
    if (_transformEdit->group.has_value())
    {
        for (const auto& [id, original] : _transformEdit->group->originals)
        {
            if (id == layer.id)
            {
                return original.Following(_transformEdit->group->box, _transformEdit->draft);
            }
        }
        return std::nullopt;
    }
    return _transformEdit->layerID == layer.id ? std::optional<LayerTransform>(_transformEdit->draft) : std::nullopt;
}

LayerTransform DocumentSession::DisplayedTransform(const ImageLayer& layer) const
{
    const std::optional<LayerTransform> pending = PendingTransform(layer);
    return pending.has_value() ? pending.value() : layer.transform;
}

std::optional<LayerTransform> DocumentSession::DisplayedMaskPlacement(const ImageLayer& layer) const
{
    if (!_transformEdit.has_value())
    {
        return std::nullopt;
    }
    if (_transformEdit->mask)
    {
        return _transformEdit->layerID == layer.id ? std::optional<LayerTransform>(_transformEdit->draft)
                                                   : std::nullopt;
    }
    const std::optional<LayerTransform> pending = PendingTransform(layer);
    if (!pending.has_value() || !layer.mask.has_value())
    {
        return std::nullopt;
    }
    return MaskPlacementMoving(layer.mask.value(), layer.transform, pending.value());
}

std::optional<Vec2> DocumentSession::TransformPixelSize() const
{
    if (_transformEdit.has_value() && _transformEdit->group.has_value())
    {
        return _transformEdit->group->box.size;
    }
    if (!_transformEdit.has_value() && TransformsAsGroup())
    {
        const std::optional<LayerTransform> box = GroupTransformBox();
        return box.has_value() ? std::optional<Vec2>(box->size) : std::nullopt;
    }
    if (TransformTargetsMask())
    {
        return std::nullopt;
    }
    const ImageLayer* layer = ActiveLayer();
    if (layer == nullptr || !layer->asset.has_value() || !layer->asset->image.HasStorage())
    {
        return std::nullopt;
    }
    return Vec2{static_cast<double>(layer->asset->image.Width()), static_cast<double>(layer->asset->image.Height())};
}

bool DocumentSession::TransformTargetsMask() const noexcept
{
    if (_transformEdit.has_value())
    {
        return _transformEdit->mask;
    }
    const ImageLayer* layer = ActiveLayer();
    return _maskSelected && layer != nullptr && layer->mask.has_value() && !layer->mask->linked;
}

SnapTargets DocumentSession::CropSnapTargets() const
{
    SnapTargets targets;
    if (!_document.has_value())
    {
        return targets;
    }
    const CanvasDocument& document = _document.value();
    targets.xs = {0.0, static_cast<double>(document.width)};
    targets.ys = {0.0, static_cast<double>(document.height)};
    for (const Guid& id : RenderLayerIds(document))
    {
        const ImageLayer* layer = FindLayer(id);
        if (layer == nullptr || !layer->asset.has_value())
        {
            continue;
        }
        const SelectionBounds bounds = PointBounds(DisplayedTransform(*layer).Corners());
        targets.xs.insert(targets.xs.end(), {std::round(bounds.minX), std::round(bounds.maxX)});
        targets.ys.insert(targets.ys.end(), {std::round(bounds.minY), std::round(bounds.maxY)});
    }
    return targets;
}

SnapTargets DocumentSession::TransformSnapTargets(const std::unordered_set<Guid>& excluding) const
{
    SnapTargets targets;
    if (!_document.has_value())
    {
        return targets;
    }
    const CanvasDocument& document = _document.value();
    const double width = static_cast<double>(document.width);
    const double height = static_cast<double>(document.height);
    targets.xs = {0.0, width / 2.0, width};
    targets.ys = {0.0, height / 2.0, height};
    for (const Guid& id : RenderLayerIds(document))
    {
        const ImageLayer* layer = FindLayer(id);
        if (layer == nullptr || !layer->asset.has_value() || excluding.contains(id))
        {
            continue;
        }
        const SelectionBounds bounds = PointBounds(DisplayedTransform(*layer).Corners());
        targets.xs.insert(targets.xs.end(), {std::round(bounds.minX), std::round((bounds.minX + bounds.maxX) / 2.0),
                                             std::round(bounds.maxX)});
        targets.ys.insert(targets.ys.end(), {std::round(bounds.minY), std::round((bounds.minY + bounds.maxY) / 2.0),
                                             std::round(bounds.maxY)});
    }
    return targets;
}

LayerTransform DocumentSession::SnappedMove(const LayerTransform& draft, const std::unordered_set<Guid>& moving,
                                            double tolerance)
{
    const SelectionBounds bounds = PointBounds(draft.Corners());
    const SnapTargets targets = TransformSnapTargets(moving);
    const TransformSnap::Result snap =
        TransformSnap::Offset(bounds.minX, bounds.minY, bounds.maxX, bounds.maxY, targets.xs, targets.ys, tolerance);
    _snapGuides = {};
    if (snap.x.has_value())
    {
        _snapGuides.xs.push_back(snap.x.value());
    }
    if (snap.y.has_value())
    {
        _snapGuides.ys.push_back(snap.y.value());
    }
    if (snap.offset == Vec2{})
    {
        return draft;
    }
    LayerTransform snapped = draft;
    snapped.origin.x += snap.offset.x;
    snapped.origin.y += snap.offset.y;
    return snapped;
}

void DocumentSession::FlipLayers(bool horizontally)
{
    if ((_gradient && _gradient->deferred) || (_pixelMove && _pixelMove->deferred))
        return;
    CommitTransform();
    if (!ResolveGradient())
        return;
    if (!CanTransform() || !_document.has_value())
    {
        return;
    }
    std::vector<Guid> members;
    double axis = 0;
    if (TransformsAsGroup())
    {
        const std::optional<LayerTransform> box = GroupTransformBox();
        if (!box.has_value())
        {
            return;
        }
        members = GroupTransformMemberIDs();
        axis = horizontally ? box->Center().x : box->Center().y;
    }
    else
    {
        const ImageLayer* layer = ActiveLayer();
        if (layer == nullptr)
        {
            return;
        }
        members = {layer->id};
        axis = horizontally ? layer->transform.Center().x : layer->transform.Center().y;
    }
    if (members.empty())
    {
        return;
    }
    const std::unordered_set<Guid> ids(members.begin(), members.end());
    BeginEdit(horizontally ? L"Flip Horizontal" : L"Flip Vertical");
    for (ImageLayer& layer : _document.value().layers)
    {
        if (!ids.contains(layer.id))
        {
            continue;
        }
        const LayerTransform flipped = layer.transform.Mirrored(horizontally, axis);
        if (layer.mask.has_value())
        {
            layer.mask->placement = MaskPlacementMoving(layer.mask.value(), layer.transform, flipped);
        }
        layer.transform = flipped;
    }
    EndEdit();
}

void DocumentSession::FlipCanvas(bool horizontally)
{
    if ((_gradient && _gradient->deferred) || (_pixelMove && _pixelMove->deferred))
        return;
    // An open color or filter panel owns the document until OK or Cancel.
    if (!_document.has_value() || HasColorEdit())
    {
        return;
    }
    // Legacy flipCanvas: the pending transform commits and the crop frame goes; a pending gradient applies first
    // (Decision D3).
    CommitTransform();
    CancelCrop();
    if (!ResolveGradient())
        return;
    CanvasDocument& document = _document.value();
    const double axis = horizontally ? document.width / 2.0 : document.height / 2.0;
    BeginEdit(horizontally ? L"Flip Canvas Horizontal" : L"Flip Canvas Vertical");
    for (ImageLayer& layer : document.layers)
    {
        layer.transform = layer.transform.Mirrored(horizontally, axis);
        if (layer.mask.has_value() && layer.mask->placement.has_value())
        {
            layer.mask->placement = layer.mask->placement->Mirrored(horizontally, axis);
        }
    }
    if (document.selection.has_value())
    {
        // Mirrored points run the other way round; the nonzero rule does not mind.
        std::vector<SelectionLoop> loops = document.selection->loops.Get();
        for (SelectionLoop& loop : loops)
        {
            for (Vec2& point : loop)
            {
                if (horizontally)
                {
                    point.x = document.width - point.x;
                }
                else
                {
                    point.y = document.height - point.y;
                }
            }
        }
        document.selection->loops = std::move(loops);
    }
    EndEdit();
}

void DocumentSession::DuplicateActiveLayer()
{
    if ((_gradient && _gradient->deferred) || (_pixelMove && _pixelMove->deferred))
        return;
    if (!ResolveGradient())
        return;
    if (!_document.has_value())
    {
        return;
    }
    const ImageLayer* active = ActiveLayer();
    if (active == nullptr || active->isGroup)
    {
        return;
    }
    std::vector<ImageLayer>& layers = _document.value().layers;
    const std::optional<std::size_t> index = IndexOf(layers, active->id);
    if (!index.has_value())
    {
        return;
    }
    // Decision D7: the copy's pixels count again (it is saved on its own), so it must fit the document's budget.
    const std::int64_t pixels = active->asset.has_value() ? active->asset->image.PixelCount() : 0;
    const std::int64_t maskPixels = active->mask.has_value() ? active->mask->asset.image.PixelCount() : 0;
    if (pixels > PixelBudgetLeft(false) || maskPixels > PixelBudgetLeft(true))
    {
        _budgetRefused = true;
        return;
    }
    ImageLayer copy = *active;
    copy.id = Guid::Generate();
    copy.name = active->name + L" copy";
    BeginEdit(L"Duplicate Layer");
    layers.insert(layers.begin() + static_cast<std::ptrdiff_t>(index.value()) + 1, copy);
    SetActiveLayerID(copy.id);
    EndEdit();
}

void DocumentSession::SelectLayerTarget(const Guid& id, bool mask)
{
    if ((_gradient && _gradient->deferred) || (_pixelMove && _pixelMove->deferred))
        return;
    if (id != _activeLayerID || mask != _maskSelected)
    {
        if (!ResolveGradient())
            return;
    }
    SelectLayer(id);
    const ImageLayer* layer = ActiveLayer();
    _maskSelected = mask && layer != nullptr && layer->mask.has_value();
}

void DocumentSession::ToggleMaskLink(const Guid& id)
{
    ImageLayer* layer = MutableLayer(id);
    if (layer == nullptr || !layer->mask.has_value())
    {
        return;
    }
    CommitTransform();
    BeginEdit(layer->mask->linked ? L"Unlink Layer Mask" : L"Link Layer Mask");
    layer->mask->linked = !layer->mask->linked;
    EndEdit();
}

const CanvasDocument* DocumentSession::DisplayDocument() const
{
    if (!_document.has_value())
    {
        return nullptr;
    }
    const PreviewLayer shown = PreviewedLayer();
    if (!_transformEdit.has_value() && !shown.raster.HasStorage())
    {
        _displayCopy.reset();
        return &_document.value();
    }
    if (_displayCopy.has_value() && _displayRevision == _editRevision)
    {
        return &_displayCopy.value();
    }
    _displayCopy = _document;
    if (shown.raster.HasStorage())
    {
        // A filter or color panel's preview shows in place of the layer's pixels; a mask on the layer's grid keeps
        // its place when the preview's grid is a grown one.
        for (ImageLayer& layer : _displayCopy->layers)
        {
            if (layer.id == shown.layerID && layer.asset.has_value())
            {
                if (layer.mask.has_value() && !layer.mask->placement.has_value() &&
                    !(shown.transform == layer.transform))
                {
                    layer.mask->placement = layer.transform;
                }
                layer.asset->image = shown.raster;
                layer.transform = shown.transform;
            }
        }
    }
    for (ImageLayer& layer : _displayCopy->layers)
    {
        if (!_transformEdit.has_value())
        {
            break;
        }
        const TransformEdit& edit = _transformEdit.value();
        const std::optional<LayerTransform> placement = DisplayedMaskPlacement(layer);
        const std::optional<LayerTransform> pending = PendingTransform(layer);
        if (pending.has_value())
        {
            bool warped = false;
            if (edit.corners.has_value() && layer.asset.has_value())
            {
                std::array<Vec2, 4> corners = edit.corners.value();
                if (edit.group.has_value())
                {
                    corners = DistortWarp::Carried(pending.value(), edit.draft, edit.corners.value());
                }
                if (DistortWarp::IsUsable(corners))
                {
                    // Preview at a bounded size; Apply resamples at full resolution.
                    const std::optional<DistortWarp::Warped> preview =
                        DistortWarp::Warp(layer.asset->image, pending.value(), corners, false, 1024.0);
                    if (preview.has_value())
                    {
                        const LayerTransform original = layer.transform;
                        layer.asset->image = preview->image;
                        layer.transform = preview->transform;
                        if (layer.mask.has_value() && !layer.mask->placement.has_value() && layer.mask->linked)
                        {
                            const std::optional<DistortWarp::Warped> mask =
                                DistortWarp::WarpMask(layer.mask->asset.image, pending.value(), corners,
                                                      MaskEdgeBackground(layer.mask->asset.thumbnail), 1024.0);
                            if (mask.has_value())
                            {
                                layer.mask->asset.image = mask->image;
                                layer.mask->placement = mask->transform.SamePlacement(layer.transform)
                                                            ? std::nullopt
                                                            : std::optional<LayerTransform>(mask->transform);
                            }
                        }
                        else if (layer.mask.has_value() && layer.mask->linked)
                        {
                            // As Apply will: a linked mask placed apart warps over its own bounds.
                            if (const std::optional<DistortWarp::Warped> moved =
                                    DistortedPlacedMask(layer.mask.value(), original, pending.value(), corners, 1024.0);
                                moved.has_value())
                            {
                                layer.mask->asset.image = moved->image;
                                layer.mask->placement = moved->transform;
                            }
                        }
                        else if (layer.mask.has_value() && !layer.mask->placement.has_value())
                        {
                            // An unlinked mask stays where it was on the document.
                            layer.mask->placement = original;
                        }
                        warped = true;
                    }
                }
            }
            if (!warped)
            {
                layer.transform = pending.value();
                if (layer.mask.has_value())
                {
                    layer.mask->placement = placement;
                }
            }
        }
        else if (placement.has_value() && layer.mask.has_value())
        {
            layer.mask->placement = placement;
        }
    }
    _displayRevision = _editRevision;
    return &_displayCopy.value();
}
