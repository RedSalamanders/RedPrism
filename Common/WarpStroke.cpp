#include "WarpStroke.h"
#include "DeferredRelease.h"
#include "PixelBuffer.h"
#include "PlacedRasterWork.h"
#include "PublicationGraphAllocator.h"
#include "RasterWork.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <exception>
#include <limits>
#include <new>
#include <utility>

namespace
{
// A thrown allocation also seals a partially changed exclusive draft as invalid. The admitted job runner
// owns exception translation; the kernel does not catch or expose reusable partial pixels after failure.
struct InvalidateWarpAttempt final
{
    bool& valid;
    bool complete = false;
    ~InvalidateWarpAttempt() noexcept
    {
        if (!complete)
            valid = false;
    }
};

// Explicit geometric growth avoids copying the complete dab path after every accepted pointer point.
// Reserve happens once before a sample's mutation, under its declared old/new capacity overlap.
template <typename T> std::size_t GrowthCapacity(const std::vector<T>& values, std::size_t required) noexcept
{
    if (required <= values.capacity())
        return values.capacity();
    const auto maximum = values.max_size();
    const auto doubled = values.capacity() > maximum / 2 ? maximum : values.capacity() * 2;
    return std::max(required, doubled);
}

template <typename T> [[nodiscard]] bool AddRetirementCapacity(std::uint64_t& total, std::size_t capacity) noexcept
{
    const std::uint64_t maximum = std::numeric_limits<std::uint64_t>::max();
    if (capacity > maximum / sizeof(T) || static_cast<std::uint64_t>(capacity) * sizeof(T) > maximum - total)
    {
        return false;
    }
    total += static_cast<std::uint64_t>(capacity) * sizeof(T);
    return true;
}
} // namespace

WarpStroke::WarpStroke(const Raster& image, SmearMode mode, const BrushSettings& settings, CancelToken cancellation)
    : _mode(mode), _diameter(std::max(2.0, settings.diameter)), _hardness(std::clamp(settings.hardness, 0.0, 0.98)),
      _strength(std::clamp(settings.opacity, 0.01, 1.0))
{
    if (!image.HasStorage() || image.Format() != RasterFormat::Bgra8888 ||
        (mode != SmearMode::Liquify && mode != SmearMode::Smudge) || !std::isfinite(settings.diameter) ||
        settings.diameter < 1 || settings.diameter > 2000 || !std::isfinite(settings.hardness) ||
        settings.hardness < 0 || settings.hardness > 1 || !std::isfinite(settings.opacity) || settings.opacity < 0.01 ||
        settings.opacity > 1 || cancellation.IsCancelled())
    {
        return;
    }
    const int width = image.Width();
    const int height = image.Height();
    const int columns = (width + kTileSize - 1) / kTileSize;
    const int rows = (height + kTileSize - 1) / kTileSize;
    try
    {
        std::vector<std::uint8_t> pixels;
        pixels.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u);
        if (!ReadRasterRegionForWork(image, {0, 0, width, height}, pixels, static_cast<std::size_t>(width) * 4u, 0,
                                     cancellation))
            return;
        std::vector<std::uint8_t> dirty;
        dirty.assign(static_cast<std::size_t>(columns) * static_cast<std::size_t>(rows), 0);
        if (cancellation.IsCancelled())
            return;
        _pixels = std::move(pixels);
        _dirty = std::move(dirty);
        _width = width;
        _height = height;
        _columns = columns;
        _tiles.resize(_dirty.size());
    }
    catch (const std::bad_alloc&)
    {
        return;
    }
    _valid = true;
}

WarpStroke::WarpStroke(WarpSourceView source, SmearMode mode, const BrushSettings& settings, CancelToken cancellation)
    : _mode(mode), _diameter(std::max(2.0, settings.diameter)), _hardness(std::clamp(settings.hardness, 0.0, 0.98)),
      _strength(std::clamp(settings.opacity, 0.01, 1.0))
{
    if (source.width < 1 || source.height < 1 || source.width > kMaxCanvasSide || source.height > kMaxCanvasSide ||
        static_cast<std::int64_t>(source.width) * source.height > kMaxLayerPixels ||
        (source.image.HasStorage() && source.image.Format() != RasterFormat::Bgra8888) || !source.placement.IsValid() ||
        (mode != SmearMode::Liquify && mode != SmearMode::Smudge) || !settings.IsValid() || settings.opacity < 0.01 ||
        cancellation.IsCancelled())
        return;
    _width = source.width;
    _height = source.height;
    _columns = (_width + kTileSize - 1) / kTileSize;
    const auto count = static_cast<std::size_t>(_columns) * ((_height + kTileSize - 1) / kTileSize);
    _dirty.assign(count, 0);
    _tiles.resize(count);
    _reader = std::make_unique<PlacedRasterReader>(source.image, source.placement);
    _sourceView = std::move(source);
    _valid = !cancellation.IsCancelled();
}

WarpStroke::~WarpStroke() = default;

namespace
{
#pragma warning(push)
#pragma warning(disable : 4324)
struct OwnedWarpStroke final : DeferredReleaseNode
{
    std::shared_ptr<const SharedPublicationMemory> memory; // dies after the draft's last CPU allocation
    WarpStroke value;
    OwnedWarpStroke(const Raster& image, SmearMode mode, const BrushSettings& settings, CancelToken cancellation,
                    std::shared_ptr<const SharedPublicationMemory> accounting)
        : memory(std::move(accounting)),
          value(WarpSourceView{image,
                               LayerTransform{
                                   {0, 0}, {static_cast<double>(image.Width()), static_cast<double>(image.Height())}},
                               image.Width(), image.Height()},
                mode, settings, cancellation)
    {
        destroy = [](DeferredReleaseNode* node) noexcept
        { const std::unique_ptr<OwnedWarpStroke> owned(static_cast<OwnedWarpStroke*>(node)); };
    }
    OwnedWarpStroke(WarpSourceView source, SmearMode mode, const BrushSettings& settings, CancelToken cancellation,
                    std::shared_ptr<const SharedPublicationMemory> accounting)
        : memory(std::move(accounting)), value(std::move(source), mode, settings, cancellation)
    {
        destroy = [](DeferredReleaseNode* node) noexcept
        { const std::unique_ptr<OwnedWarpStroke> owned(static_cast<OwnedWarpStroke*>(node)); };
    }
};
#pragma warning(pop)

struct RetireWarpStroke final
{
    void operator()(OwnedWarpStroke* node) const noexcept
    {
        if (!node)
            return;
        // Mutation is joined before any owner drops its last strong borrow. Capacity queries are O(1),
        // and include cancelled drafts which still own their complete private allocation prefix.
        auto bytes = node->value.RetirementMemoryBytes();
        constexpr auto wrapper = sizeof(OwnedWarpStroke) + 4096;
        if (!bytes || wrapper > (std::numeric_limits<std::uint64_t>::max)() - bytes ||
            SharedPublicationMemory::OwnershipAllowance() >
                (std::numeric_limits<std::uint64_t>::max)() - bytes - wrapper)
            std::terminate();
        node->bytes = bytes + wrapper + SharedPublicationMemory::OwnershipAllowance();
        if (!TryDeferRelease(*node, DeferredReleasePolicy::AlwaysOnOwner))
        {
            const std::unique_ptr<OwnedWarpStroke> owned(node);
        }
    }
};
} // namespace

std::shared_ptr<WarpStroke> WarpStroke::MakeForWork(const Raster& image, SmearMode mode, const BrushSettings& settings,
                                                    std::thread::id ownerThread, CancelToken cancellation,
                                                    std::shared_ptr<const SharedPublicationMemory> memory)
{
    if (ownerThread == std::thread::id{} || ownerThread == std::this_thread::get_id() || !memory || memory->IsBound() ||
        cancellation.IsCancelled())
        return {};
    auto node = std::make_unique<OwnedWarpStroke>(image, mode, settings, cancellation, memory);
    if (!node->value.IsValid() || !node->value.RetirementMemoryBytes() || cancellation.IsCancelled())
        return {};
    // Control-block allocation invokes this deleter on failure. Transfer once; the factory has no
    // owner-thread failure path, and all unaccepted temporary graph destruction stays on this worker.
    auto shared = std::shared_ptr<OwnedWarpStroke>(node.release(), RetireWarpStroke{},
                                                   PublicationGraphAllocator<OwnedWarpStroke>{memory});
    auto result = std::shared_ptr<WarpStroke>(shared, &shared->value);
    result->_self = result;
    result->_workOwner = ownerThread;
    result->_planningBase = result->RetirementMemoryBytes();
    return result;
}

std::shared_ptr<WarpStroke> WarpStroke::MakePlacedForWork(WarpSourceView source, SmearMode mode,
                                                          const BrushSettings& settings, std::thread::id ownerThread,
                                                          CancelToken cancellation,
                                                          std::shared_ptr<const SharedPublicationMemory> memory)
{
    if (ownerThread == std::thread::id{} || ownerThread == std::this_thread::get_id() || !memory || memory->IsBound() ||
        cancellation.IsCancelled())
        return {};
    auto node = std::make_unique<OwnedWarpStroke>(std::move(source), mode, settings, cancellation, memory);
    if (!node->value.IsValid() || !node->value.RetirementMemoryBytes() || cancellation.IsCancelled())
        return {};
    auto shared = std::shared_ptr<OwnedWarpStroke>(node.release(), RetireWarpStroke{},
                                                   PublicationGraphAllocator<OwnedWarpStroke>{memory});
    auto result = std::shared_ptr<WarpStroke>(shared, &shared->value);
    result->_self = result;
    result->_workOwner = ownerThread;
    result->_planningBase = result->RetirementMemoryBytes();
    return result;
}

std::uint64_t WarpStroke::RetirementMemoryBytes() const noexcept
{
    std::uint64_t total = sizeof(*this) + 1024; // control/allocator headroom
    if (!AddRetirementCapacity<std::uint8_t>(total, _pixels.capacity()) ||
        !AddRetirementCapacity<Vec2>(total, _points.capacity()) ||
        !AddRetirementCapacity<float>(total, _carried.capacity()) ||
        !AddRetirementCapacity<float>(total, _scratch.capacity()) ||
        !AddRetirementCapacity<std::uint8_t>(total, _dirty.capacity()) ||
        !AddRetirementCapacity<Tile>(total, _tiles.capacity()) || _tileBytes > UINT64_MAX - total)
    {
        return 0;
    }
    total += _tileBytes;
    if (_reader)
    {
        const auto bytes = _reader->RetirementMemoryBytes();
        if (!bytes || bytes > UINT64_MAX - total)
            return 0;
        total += bytes;
    }
    return total;
}

std::optional<WarpStroke::SampleMemoryPlan> WarpStroke::PlanSampleMemory(Vec2 point) const noexcept
{
    if (_workOwner == std::thread::id{} || _workOwner != std::this_thread::get_id())
        return std::nullopt;
    return PlanSampleMemoryImpl(point);
}

std::uint64_t WarpStroke::SnapshotMemoryBytesForOwner() const noexcept
{
    if (!_valid || !_reader || !_sourceView || _workOwner == std::thread::id{} ||
        _workOwner != std::this_thread::get_id())
        return 0;
    const auto columns = (static_cast<std::uint64_t>(_width) + 255) / 256;
    const auto rows = (static_cast<std::uint64_t>(_height) + 255) / 256;
    auto bytes = std::uint64_t{1024 * 1024}; // dense indices, tile-vector overlap, reader caches and controls
    const auto add = [&](std::uint64_t count, std::uint64_t unit) noexcept
    {
        if ((unit && count > UINT64_MAX / unit) || count * unit > UINT64_MAX - bytes)
            return false;
        bytes += count * unit;
        return true;
    };
    const auto tileBytes = PixelBuffer::RequiredBytes(kTileSize * kTileSize * 4);
    if (!tileBytes || !add(columns * rows, 2 * sizeof(RasterTile) + sizeof(std::int32_t)))
        return 0;
    if (_allocatedLeft <= _allocatedRight && _allocatedTop <= _allocatedBottom)
    {
        const auto tiles = static_cast<std::uint64_t>(_allocatedRight / kTileSize - _allocatedLeft / kTileSize + 1) *
                           (_allocatedBottom / kTileSize - _allocatedTop / kTileSize + 1);
        if (!add(tiles, tileBytes + 4096))
            return 0;
    }
    return bytes;
}

std::optional<WarpStroke::SampleMemoryPlan> WarpStroke::PlanSampleMemoryImpl(Vec2 point) const noexcept
{
    if (!_valid || !_reader || !_planningBase || _self.expired() || _sampleSequence == UINT64_MAX ||
        !std::isfinite(point.x) || !std::isfinite(point.y))
        return std::nullopt;
    constexpr auto limit = static_cast<double>((std::numeric_limits<int>::max)()) / 2.0;
    if (std::abs(point.x) > limit || std::abs(point.y) > limit)
        return std::nullopt;
    SampleMemoryPlan plan;
    plan.point = point;
    plan._acceptedPoint = point;
    plan.sequence = _sampleSequence + 1;
    plan._producer = _self;
    const auto from = _last.value_or(point);
    const auto distance = std::hypot(point.x - from.x, point.y - from.y);
    const auto spacing = std::max(1.0, _diameter * (_mode == SmearMode::Smudge ? 0.08 : 0.025));
    const auto steps = _last && distance >= spacing ? std::ceil(distance / spacing) : 0.0;
    if (!std::isfinite(steps) || steps >= static_cast<double>((std::numeric_limits<int>::max)()) ||
        steps > static_cast<double>(_points.max_size() - _points.size()))
        return std::nullopt;
    plan.maximumPoints = _points.size() + static_cast<std::size_t>(steps);
    plan.pointCapacity = GrowthCapacity(_points, static_cast<std::size_t>(plan.maximumPoints));
    // One dab moves at most spacing; float rounding gets one extra pixel before the unchanged +2 margin.
    // A conservative swept rectangle covers source scratch and every write without iterating dabs or keys.
    const int reach = Radius() + static_cast<int>(std::ceil(spacing * _strength)) + 3;
    const int left = std::max(0, static_cast<int>(std::floor(std::min(from.x, point.x))) - reach);
    const int top = std::max(0, static_cast<int>(std::floor(std::min(from.y, point.y))) - reach);
    const int right = std::min(_width - 1, static_cast<int>(std::ceil(std::max(from.x, point.x))) + reach);
    const int bottom = std::min(_height - 1, static_cast<int>(std::ceil(std::max(from.y, point.y))) + reach);
    const auto tiles = [](int x0, int y0, int x1, int y1) -> std::uint64_t
    {
        return x1 < x0 || y1 < y0 ? 0ULL
                                  : static_cast<std::uint64_t>(x1 / kTileSize - x0 / kTileSize + 1) *
                                        (y1 / kTileSize - y0 / kTileSize + 1);
    };
    const bool readsPixels = (_last && steps > 0) || (!_last && _mode == SmearMode::Smudge);
    const bool hasNext = readsPixels && left <= right && top <= bottom;
    const bool hasPrevious = _allocatedLeft <= _allocatedRight && _allocatedTop <= _allocatedBottom;
    plan.maximumTiles =
        hasNext ? tiles(hasPrevious ? std::min(left, _allocatedLeft) : left,
                        hasPrevious ? std::min(top, _allocatedTop) : top,
                        hasPrevious ? std::max(right, _allocatedRight) : right,
                        hasPrevious ? std::max(bottom, _allocatedBottom) : bottom)
                : (hasPrevious ? tiles(_allocatedLeft, _allocatedTop, _allocatedRight, _allocatedBottom) : 0);
    // Prior unconsumed dirty versions also need publication. This bounds them without an owner tile walk.
    plan.maximumChangedTiles = plan.maximumTiles;
    // Complete source/directory baseline is fixed at factory construction, not today's growing graph.
    // Revisited points cannot repeatedly accumulate metadata or per-tile allowances.
    plan.persistentCpuBytes = _planningBase;
    const auto add = [](std::uint64_t& total, std::uint64_t count, std::uint64_t unit) noexcept
    {
        if ((unit && count > UINT64_MAX / unit) || count * unit > UINT64_MAX - total)
            return false;
        total += count * unit;
        return true;
    };
    const auto pixelBytes = PixelBuffer::RequiredBytes(kTileSize * kTileSize * 4);
    const auto scratchSide = static_cast<std::uint64_t>(2 * reach + 1);
    const auto scratch = _mode == SmearMode::Liquify && hasNext ? std::min<std::uint64_t>(_width, scratchSide) *
                                                                      std::min<std::uint64_t>(_height, scratchSide) * 4
                                                                : 0;
    const auto carried = _mode == SmearMode::Smudge && (hasNext || !_last)
                             ? static_cast<std::uint64_t>(2 * Radius() + 1) * (2 * Radius() + 1) * 4
                             : 0;
    if (!pixelBytes || !add(plan.persistentCpuBytes, 1, 1024 * 1024) ||
        !add(plan.persistentCpuBytes, plan.maximumTiles, 2 * pixelBytes + 4096) ||
        !add(plan.persistentCpuBytes, plan.pointCapacity, sizeof(Vec2)) ||
        !add(plan.persistentCpuBytes, std::max<std::uint64_t>(_scratch.capacity(), scratch), sizeof(float)) ||
        !add(plan.persistentCpuBytes, std::max<std::uint64_t>(_carried.capacity(), carried), sizeof(float)))
        return std::nullopt;
    plan.transientCpuBytes = 1024 * 1024;
    if (!add(plan.transientCpuBytes, _points.capacity(), sizeof(Vec2)) ||
        !add(plan.transientCpuBytes, _scratch.capacity(), sizeof(float)) ||
        !add(plan.transientCpuBytes, plan.maximumChangedTiles, pixelBytes) ||
        !add(plan.transientCpuBytes, plan.maximumTiles, 2 * sizeof(RasterTile)))
        return std::nullopt;
    return plan;
}

bool WarpStroke::AppendPlannedForWork(const SampleMemoryPlan& plan, CancelToken cancellation, WorkObserver observer)
{
    if (_workOwner == std::thread::id{} || _workOwner == std::this_thread::get_id() || !IsSamplePlanCurrent(plan))
        return false;
    return AppendForWork(plan.point, cancellation, observer);
}

bool WarpStroke::IsSamplePlanCurrentForOwner(const SampleMemoryPlan& plan) const noexcept
{
    return _workOwner != std::thread::id{} && _workOwner == std::this_thread::get_id() && IsSamplePlanCurrent(plan);
}

bool WarpStroke::IsSamplePlanCurrent(const SampleMemoryPlan& plan) const noexcept
{
    if (plan.point != plan._acceptedPoint)
        return false;
    const auto current = PlanSampleMemoryImpl(plan.point);
    if (!current || current->_producer.owner_before(plan._producer) ||
        plan._producer.owner_before(current->_producer) || current->sequence != plan.sequence ||
        current->maximumTiles != plan.maximumTiles || current->maximumChangedTiles != plan.maximumChangedTiles ||
        current->maximumPoints != plan.maximumPoints || current->pointCapacity != plan.pointCapacity ||
        current->persistentCpuBytes != plan.persistentCpuBytes || current->transientCpuBytes != plan.transientCpuBytes)
        return false; // Refused stale/foreign/edited plan mutates neither pixels nor an otherwise valid draft.
    return true;
}

int WarpStroke::Radius() const noexcept
{
    return static_cast<int>(std::ceil(_diameter / 2.0));
}

float WarpStroke::Weight(float u) const noexcept
{
    if (!(u < 1.0f))
    {
        return 0.0f;
    }
    const float h = static_cast<float>(_hardness);
    if (!(u > h))
    {
        return 1.0f;
    }
    const float t = (1.0f - u) / (1.0f - h);
    return t * t * (3.0f - 2.0f * t);
}

void WarpStroke::MarkDirty(int x0, int y0, int x1, int y1) noexcept
{
    x0 = std::clamp(x0, 0, _width - 1);
    x1 = std::clamp(x1, 0, _width - 1);
    y0 = std::clamp(y0, 0, _height - 1);
    y1 = std::clamp(y1, 0, _height - 1);
    for (int ty = y0 / kTileSize; ty <= y1 / kTileSize; ++ty)
    {
        for (int tx = x0 / kTileSize; tx <= x1 / kTileSize; ++tx)
        {
            _dirty[static_cast<std::size_t>(ty) * static_cast<std::size_t>(_columns) + static_cast<std::size_t>(tx)] =
                static_cast<std::uint8_t>(_dirty[static_cast<std::size_t>(ty) * static_cast<std::size_t>(_columns) +
                                                 static_cast<std::size_t>(tx)] |
                                          3);
        }
    }
}

bool WarpStroke::EnsureRegion(int x0, int y0, int x1, int y1, CancelToken cancellation, WorkObserver observer)
{
    if (!_reader)
        return true;
    x0 = std::max(0, x0);
    y0 = std::max(0, y0);
    x1 = std::min(_width - 1, x1);
    y1 = std::min(_height - 1, y1);
    if (x0 > x1 || y0 > y1)
        return true;
    for (int ty = y0 / kTileSize; ty <= y1 / kTileSize; ++ty)
        for (int tx = x0 / kTileSize; tx <= x1 / kTileSize; ++tx)
        {
            auto& tile = _tiles[static_cast<std::size_t>(ty) * _columns + tx];
            if (tile.pixels)
                continue;
            const int x = tx * kTileSize, y = ty * kTileSize;
            const int width = std::min(kTileSize, _width - x), height = std::min(kTileSize, _height - y);
            const auto stride = static_cast<std::size_t>(width) * 4;
            auto pixels = PixelBuffer::TryAllocate(stride * height);
            if (!pixels)
                return false;
            for (int row = 0; row < height; ++row)
                if (!Checkpoint(WorkStage::SourceRow, cancellation, observer) ||
                    !_reader->Read({x, y + row, width, 1},
                                   {pixels.Data() + static_cast<std::size_t>(row) * stride, stride}, stride,
                                   cancellation))
                    return false;
            if (pixels.AllocationBytes() > UINT64_MAX - _tileBytes)
                return false;
            _tileBytes += pixels.AllocationBytes();
            tile.pixels = std::move(pixels);
            _allocatedLeft = std::min(_allocatedLeft, x);
            _allocatedTop = std::min(_allocatedTop, y);
            _allocatedRight = std::max(_allocatedRight, x + width - 1);
            _allocatedBottom = std::max(_allocatedBottom, y + height - 1);
        }
    return !cancellation.IsCancelled();
}

std::uint8_t* WarpStroke::PixelAt(int x, int y) noexcept
{
    if (!_reader)
        return _pixels.data() + (static_cast<std::size_t>(y) * _width + x) * 4;
    const int tx = x / kTileSize, ty = y / kTileSize;
    auto& tile = _tiles[static_cast<std::size_t>(ty) * _columns + tx];
    const int width = std::min(kTileSize, _width - tx * kTileSize);
    return tile.pixels.Data() + (static_cast<std::size_t>(y % kTileSize) * width + x % kTileSize) * 4;
}

bool WarpStroke::CopyCurrentRow(int row, std::uint8_t* output, CancelToken cancellation) const noexcept
{
    const auto stride = static_cast<std::size_t>(_width) * 4;
    if (!_reader)
    {
        std::memcpy(output, _pixels.data() + static_cast<std::size_t>(row) * stride, stride);
        return !cancellation.IsCancelled();
    }
    if (!_reader->Read({0, row, _width, 1}, {output, stride}, stride, cancellation))
        return false;
    for (int tx = 0; tx < _columns; ++tx)
    {
        const auto& tile = _tiles[static_cast<std::size_t>(row / kTileSize) * _columns + tx];
        if (!tile.pixels)
            continue;
        const int x = tx * kTileSize, width = std::min(kTileSize, _width - x);
        const auto bytes = static_cast<std::size_t>(width) * 4;
        std::memcpy(output + static_cast<std::size_t>(x) * 4,
                    tile.pixels.Data() + static_cast<std::size_t>(row % kTileSize) * bytes, bytes);
    }
    return !cancellation.IsCancelled();
}

void WarpStroke::Append(Vec2 point)
{
    // Reference API keeps its finite-input ignore rule and runs the identical dab arithmetic.
    if (!std::isfinite(point.x) || !std::isfinite(point.y))
        return;
    (void)AppendForWork(point, {});
}

bool WarpStroke::Checkpoint(WorkStage stage, CancelToken cancellation, WorkObserver observer) noexcept
{
    if (cancellation.IsCancelled())
        return false;
    if (observer.checkpoint)
        observer.checkpoint(observer.context, stage);
    return !cancellation.IsCancelled();
}

bool WarpStroke::AppendForWork(Vec2 point, CancelToken cancellation, WorkObserver observer)
{
    InvalidateWarpAttempt attempt{_valid};
    const auto refuse = [&]() noexcept
    {
        _valid = false;
        return false;
    };
    const auto complete = [&]() noexcept
    {
        if (!Checkpoint(WorkStage::Complete, cancellation, observer))
            return false;
        ++_sampleSequence;
        attempt.complete = true;
        return true;
    };
    if (!_valid || _sampleSequence == UINT64_MAX || !std::isfinite(point.x) || !std::isfinite(point.y) ||
        !Checkpoint(WorkStage::Sample, cancellation, observer))
        return refuse();
    // Center rounding and radius/margin additions below use int. Reject unsupported coordinates before
    // narrowing or arithmetic; ordinary viewport points are far inside this bound.
    const double centerLimit = static_cast<double>((std::numeric_limits<int>::max)()) / 2.0;
    if (std::abs(point.x) > centerLimit || std::abs(point.y) > centerLimit)
        return refuse();
    if (!_last)
    {
        _last = point;
        if (_mode == SmearMode::Smudge && !PickUp(point, cancellation, observer))
            return refuse();
        return complete();
    }
    const Vec2 from = _last.value();
    const double distance = std::hypot(point.x - from.x, point.y - from.y);
    const double spacing = std::max(1.0, _diameter * (_mode == SmearMode::Smudge ? 0.08 : 0.025));
    if (distance < spacing)
        return complete();
    const double count = std::ceil(distance / spacing);
    if (!std::isfinite(count) || count < 1 || count >= static_cast<double>((std::numeric_limits<int>::max)()))
        return refuse();
    const int steps = static_cast<int>(count);
    if (static_cast<std::size_t>(steps) > _points.max_size() - _points.size())
        return refuse();
    // One declared old/new vector overlap, rather than repeated geometric growth inside the dab loop.
    _points.reserve(GrowthCapacity(_points, _points.size() + static_cast<std::size_t>(steps)));
    Vec2 previous = from;
    for (int step = 1; step <= steps; ++step)
    {
        if (!Checkpoint(WorkStage::Sample, cancellation, observer))
            return refuse();
        const double t = static_cast<double>(step) / steps;
        const Vec2 next{from.x + (point.x - from.x) * t, from.y + (point.y - from.y) * t};
        if (_mode == SmearMode::Smudge ? !Smudge(next, cancellation, observer)
                                       : !Push(previous, next, cancellation, observer))
            return refuse();
        _points.push_back(next);
        previous = next;
    }
    _last = point;
    return complete();
}

bool WarpStroke::PickUp(Vec2 center, CancelToken cancellation, WorkObserver observer)
{
    const int r = Radius();
    const int side = 2 * r + 1;
    _carried.assign(static_cast<std::size_t>(side) * static_cast<std::size_t>(side) * 4u, 0.0f);
    const int cx = static_cast<int>(std::lround(center.x));
    const int cy = static_cast<int>(std::lround(center.y));
    if (!EnsureRegion(cx - r, cy - r, cx + r, cy + r, cancellation, observer))
        return false;
    for (int dy = -r; dy <= r; ++dy)
    {
        if (!Checkpoint(WorkStage::PickUpRow, cancellation, observer))
            return false;
        const int y = cy + dy;
        if (y < 0 || y >= _height)
        {
            continue;
        }
        for (int dx = -r; dx <= r; ++dx)
        {
            const int x = cx + dx;
            if (x < 0 || x >= _width)
            {
                continue;
            }
            auto* pixel = PixelAt(x, y);
            const std::size_t c =
                (static_cast<std::size_t>(dy + r) * static_cast<std::size_t>(side) + static_cast<std::size_t>(dx + r)) *
                4u;
            for (std::size_t k = 0; k < 4; ++k)
            {
                _carried[c + k] = static_cast<float>(pixel[k]);
            }
        }
    }
    return true;
}

bool WarpStroke::Smudge(Vec2 center, CancelToken cancellation, WorkObserver observer)
{
    const int r = Radius();
    const int side = 2 * r + 1;
    if (_carried.size() < static_cast<std::size_t>(side) * static_cast<std::size_t>(side) * 4u)
    {
        if (!PickUp(center, cancellation, observer))
            return false;
    }
    const int cx = static_cast<int>(std::lround(center.x));
    const int cy = static_cast<int>(std::lround(center.y));
    const float keep = static_cast<float>(_strength);
    const float invR = 1.0f / static_cast<float>(_diameter / 2.0);
    if (!EnsureRegion(cx - r, cy - r, cx + r, cy + r, cancellation, observer))
        return false;
    MarkDirty(cx - r, cy - r, cx + r, cy + r);
    for (int dy = -r; dy <= r; ++dy)
    {
        if (!Checkpoint(WorkStage::SmudgeRow, cancellation, observer))
            return false;
        const int y = cy + dy;
        if (y < 0 || y >= _height)
        {
            continue;
        }
        for (int dx = -r; dx <= r; ++dx)
        {
            const int x = cx + dx;
            if (x < 0 || x >= _width)
            {
                continue;
            }
            const float w = Weight(std::sqrt(static_cast<float>(dx * dx + dy * dy)) * invR);
            if (!(w > 0.0f))
            {
                continue;
            }
            auto* pixel = PixelAt(x, y);
            const std::size_t c =
                (static_cast<std::size_t>(dy + r) * static_cast<std::size_t>(side) + static_cast<std::size_t>(dx + r)) *
                4u;
            for (std::size_t k = 0; k < 4; ++k)
            {
                const float under = static_cast<float>(pixel[k]);
                const float painted = under + (_carried[c + k] - under) * w;
                pixel[k] = static_cast<std::uint8_t>(std::max(0.0f, std::min(255.0f, std::round(painted))));
                // The brush picks up some of what it just left, more the weaker the smudge.
                _carried[c + k] = painted + (_carried[c + k] - painted) * keep;
            }
        }
    }
    return true;
}

// Forward warp: pixels under the brush move with it, most at its center, fading to none at its rim.
bool WarpStroke::Push(Vec2 from, Vec2 to, CancelToken cancellation, WorkObserver observer)
{
    const int r = Radius();
    const float moveX = static_cast<float>((to.x - from.x) * _strength);
    const float moveY = static_cast<float>((to.y - from.y) * _strength);
    const int margin = static_cast<int>(std::ceil(std::max(std::fabs(moveX), std::fabs(moveY)))) + 2;
    const int cx = static_cast<int>(std::lround(to.x));
    const int cy = static_cast<int>(std::lround(to.y));
    // A copy of the area as it was before this dab, which the dab samples from.
    const int x0 = std::max(0, cx - r - margin);
    const int x1 = std::min(_width - 1, cx + r + margin);
    const int y0 = std::max(0, cy - r - margin);
    const int y1 = std::min(_height - 1, cy + r + margin);
    if (x0 > x1 || y0 > y1)
    {
        return true;
    }
    if (!EnsureRegion(x0, y0, x1, y1, cancellation, observer))
        return false;
    const int cw = x1 - x0 + 1;
    const int ch = y1 - y0 + 1;
    const std::size_t needed = static_cast<std::size_t>(cw) * static_cast<std::size_t>(ch) * 4u;
    if (_scratch.size() < needed)
    {
        // Exact once-per-growth reserve keeps the declared old/new scratch overlap authoritative.
        if (_scratch.capacity() < needed)
            _scratch.reserve(needed);
        _scratch.assign(needed, 0.0f);
    }
    for (int y = 0; y < ch; ++y)
    {
        if (!Checkpoint(WorkStage::ScratchRow, cancellation, observer))
            return false;
        for (int x = 0; x < cw; ++x)
        {
            auto* pixel = PixelAt(x + x0, y + y0);
            const std::size_t s =
                (static_cast<std::size_t>(y) * static_cast<std::size_t>(cw) + static_cast<std::size_t>(x)) * 4u;
            for (std::size_t k = 0; k < 4; ++k)
            {
                _scratch[s + k] = static_cast<float>(pixel[k]);
            }
        }
    }
    MarkDirty(cx - r, cy - r, cx + r, cy + r);
    const float invR = 1.0f / static_cast<float>(_diameter / 2.0);
    for (int dy = -r; dy <= r; ++dy)
    {
        if (!Checkpoint(WorkStage::PushRow, cancellation, observer))
            return false;
        const int y = cy + dy;
        if (y < y0 || y > y1)
        {
            continue;
        }
        for (int dx = -r; dx <= r; ++dx)
        {
            const int x = cx + dx;
            if (x < x0 || x > x1)
            {
                continue;
            }
            const float w = Weight(std::sqrt(static_cast<float>(dx * dx + dy * dy)) * invR);
            if (!(w > 0.0f))
            {
                continue;
            }
            // Bilinear sample of the old pixels, from behind the brush's travel.
            const float sx =
                std::min(static_cast<float>(cw - 1), std::max(0.0f, static_cast<float>(x - x0) - moveX * w));
            const float sy =
                std::min(static_cast<float>(ch - 1), std::max(0.0f, static_cast<float>(y - y0) - moveY * w));
            const int ix = std::min(cw - 2, static_cast<int>(sx));
            const int iy = std::min(ch - 2, static_cast<int>(sy));
            if (ix < 0 || iy < 0)
            {
                continue;
            }
            const float fx = sx - static_cast<float>(ix);
            const float fy = sy - static_cast<float>(iy);
            auto* pixel = PixelAt(x, y);
            const std::size_t s00 =
                (static_cast<std::size_t>(iy) * static_cast<std::size_t>(cw) + static_cast<std::size_t>(ix)) * 4u;
            const std::size_t s10 = s00 + 4u;
            const std::size_t s01 = s00 + static_cast<std::size_t>(cw) * 4u;
            const std::size_t s11 = s01 + 4u;
            for (std::size_t k = 0; k < 4; ++k)
            {
                const float top = _scratch[s00 + k] + (_scratch[s10 + k] - _scratch[s00 + k]) * fx;
                const float bottom = _scratch[s01 + k] + (_scratch[s11 + k] - _scratch[s01 + k]) * fx;
                pixel[k] =
                    static_cast<std::uint8_t>(std::max(0.0f, std::min(255.0f, std::round(top + (bottom - top) * fy))));
            }
        }
    }
    return true;
}

Raster WarpStroke::Image() const
{
    if (!_valid)
    {
        return {};
    }
    if (!_reader)
        return Raster::Make(_width, _height, RasterFormat::Bgra8888, _pixels.data(), _pixels.size());
    const auto stride = static_cast<std::size_t>(_width) * 4;
    auto pixels = PixelBuffer::TryAllocate(stride * _height);
    if (!pixels)
        return {};
    for (int row = 0; row < _height; ++row)
        if (!CopyCurrentRow(row, pixels.Data() + static_cast<std::size_t>(row) * stride, {}))
            return {};
    return Raster::Adopt(_width, _height, RasterFormat::Bgra8888, std::move(pixels));
}

Raster WarpStroke::ImageForWork(CancelToken cancellation, WorkObserver observer)
{
    InvalidateWarpAttempt attempt{_valid};
    if (!_valid || cancellation.IsCancelled())
        return {};
    auto pixels = PixelBuffer::TryAllocate(static_cast<std::size_t>(_width) * _height * 4);
    if (!pixels)
        return {};
    const auto stride = static_cast<std::size_t>(_width) * 4u;
    for (int row = 0; row < _height; ++row)
    {
        if (!Checkpoint(WorkStage::ImageRow, cancellation, observer))
            return {};
        const auto offset = static_cast<std::size_t>(row) * stride;
        if (!CopyCurrentRow(row, pixels.Data() + offset, cancellation))
            return {};
    }
    auto image = Raster::Adopt(_width, _height, RasterFormat::Bgra8888, std::move(pixels));
    if (!image.HasStorage() || !Checkpoint(WorkStage::Complete, cancellation, observer))
        return {};
    attempt.complete = true;
    return image;
}

std::vector<RasterTile> WarpStroke::TakeChangedTilesForWork(CancelToken cancellation, WorkObserver observer)
{
    return SnapshotTilesForWork(false, cancellation, observer);
}

std::vector<RasterTile> WarpStroke::AllTouchedTilesForWork(CancelToken cancellation, WorkObserver observer)
{
    return SnapshotTilesForWork(true, cancellation, observer);
}

std::vector<RasterTile> WarpStroke::SnapshotTilesForWork(bool allTouched, CancelToken cancellation,
                                                         WorkObserver observer)
{
    InvalidateWarpAttempt attempt{_valid};
    if (!_valid || cancellation.IsCancelled())
        return {};
    const auto mask = static_cast<std::uint8_t>(allTouched ? 2 : 1);
    const auto count = static_cast<std::size_t>(
        std::count_if(_dirty.begin(), _dirty.end(), [mask](std::uint8_t flag) { return (flag & mask) != 0; }));
    std::vector<RasterTile> result;
    result.reserve(count);
    const int rows = (_height + kTileSize - 1) / kTileSize;
    for (int ty = 0; ty < rows; ++ty)
    {
        if (cancellation.IsCancelled())
            return {};
        for (int tx = 0; tx < _columns; ++tx)
        {
            const auto index = static_cast<std::size_t>(ty) * _columns + tx;
            if (!(_dirty[index] & mask))
                continue;
            auto& tile = _tiles[index];
            const int x = tx * kTileSize, y = ty * kTileSize;
            const int width = std::min(kTileSize, _width - x), height = std::min(kTileSize, _height - y);
            if ((_dirty[index] & 1) || !tile.published.HasStorage())
            {
                if (!EnsureRegion(x, y, x + width - 1, y + height - 1, cancellation, observer))
                    return {};
                const auto stride = static_cast<std::size_t>(width) * 4;
                auto pixels = PixelBuffer::TryAllocate(stride * height);
                if (!pixels)
                    return {};
                for (int row = 0; row < height; ++row)
                {
                    if (!Checkpoint(WorkStage::TileRow, cancellation, observer))
                        return {};
                    const auto* from = _reader ? tile.pixels.Data() + static_cast<std::size_t>(row) * stride
                                               : _pixels.data() + (static_cast<std::size_t>(y + row) * _width + x) * 4;
                    std::memcpy(pixels.Data() + static_cast<std::size_t>(row) * stride, from, stride);
                }
                auto published = Raster::Adopt(width, height, RasterFormat::Bgra8888, std::move(pixels));
                const auto bytes = published.RetirementMemoryBytes();
                const auto old = tile.published.RetirementMemoryBytes();
                if (!published.HasStorage() || !bytes || old > _tileBytes || bytes > UINT64_MAX - (_tileBytes - old))
                    return {};
                _tileBytes = _tileBytes - old + bytes;
                tile.published = std::move(published);
            }
            result.push_back({x, y, tile.published});
        }
    }
    if (!Checkpoint(WorkStage::Complete, cancellation, observer))
        return {};
    for (auto& flag : _dirty)
        flag &= 2; // Successful all-touched publication also consumes this generation's changed set.
    attempt.complete = true;
    return result;
}

std::vector<RasterTile> WarpStroke::TakeChangedTiles()
{
    return TakeChangedTilesForWork({});
}
