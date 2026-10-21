#include "SelectionOutlineWork.h"

#include "PublicationObject.h"
#include "WandLayerSelectionWork.h"

#include <algorithm>

#include <limits>
#include <new>
#include <utility>

struct PreparedSelectionOutline::Storage final
{
    // Destruction order drops buffers before source ownership. The publication node keeps the output charge.
    SelectionLoops source;
    Loops points;
    Vec2 anchor;
    std::uint64_t pointCount = 0;
    std::uint64_t outputBytes = 0;
};

std::optional<std::uint64_t> PreparedSelectionOutline::OutputMemory(const SelectionLoops& source) noexcept
{
    return OutputMemory(source.size(), source.PointCount());
}

std::optional<std::uint64_t> PreparedSelectionOutline::AdmissionMemory(
    const WandLayerSelectionMemoryPlan& plan) noexcept
{
    constexpr auto maximum = (std::numeric_limits<std::uint64_t>::max)();
    const auto buffers = std::max(plan.geometryGrowthBytes, plan.booleanOutputBytes);
    const auto fixed = PublicationObjectAllowance<Storage>();
    if (buffers < 4096 || buffers > maximum - fixed)
        return std::nullopt;
    return buffers + fixed;
}

std::optional<std::uint64_t> PreparedSelectionOutline::OutputMemory(std::uint64_t loopCount,
                                                                    std::uint64_t pointCount) noexcept
{
    constexpr auto maximum = (std::numeric_limits<std::uint64_t>::max)();
    std::uint64_t bytes = PublicationObjectAllowance<Storage>();
    const auto add = [&](std::uint64_t count, std::uint64_t width)
    {
        if (count > maximum / width || count * width > maximum - bytes)
            return false;
        bytes += count * width;
        return true;
    };
    // Supported MSVC exact reserve plus conservative per-loop allocator/Debug iterator bookkeeping.
    if (!add(loopCount, sizeof(Loop) + 256) || !add(pointCount, sizeof(D2D1_POINT_2F)))
        return std::nullopt;
    return bytes;
}

std::optional<PreparedSelectionOutline> PreparedSelectionOutline::Prepare(
    SelectionLoops source, std::shared_ptr<const SharedPublicationMemory> outputMemory, CancelToken cancellation,
    Observer observer)
{
    // An empty owner would select MakePublicationObject's ordinary reference allocator/deleter. This
    // prepared display transport must keep an explicit owner through deferred destruction and weak controls.
    if (outputMemory.use_count() == 0 || cancellation.IsCancelled())
        return std::nullopt;
    const auto declared = OutputMemory(source);
    if (!declared)
        return std::nullopt;
    Vec2 anchor;
    if (const auto bounds = source.Bounds())
        anchor = {bounds->minX, bounds->minY};
    Loops loops;
    loops.reserve(source.size());
    std::uint64_t work = 0;
    const auto step = [&]()
    {
        const auto current = work++;
        if ((current & 63) != 0)
            return true;
        if (observer.checkpoint)
            observer.checkpoint(observer.context, current);
        return !cancellation.IsCancelled();
    };
    for (const auto& loop : source)
    {
        if (!step())
            return std::nullopt;
        Loop points;
        points.reserve(loop.size());
        for (const auto& point : loop)
        {
            if (!step())
                return std::nullopt;
            // Exact current SyncSelectionOverlay order: subtract the double anchor before narrowing.
            points.push_back(
                D2D1_POINT_2F{static_cast<float>(point.x - anchor.x), static_cast<float>(point.y - anchor.y)});
        }
        loops.push_back(std::move(points));
    }
    if (cancellation.IsCancelled())
        return std::nullopt;
    PreparedSelectionOutline result;
    auto storage = MakePublicationObject<Storage>(std::move(outputMemory), *declared);
    storage->anchor = anchor;
    storage->pointCount = source.PointCount();
    storage->outputBytes = *declared;
    storage->source = std::move(source);
    storage->points = std::move(loops);
    result._storage = std::move(storage);
    return cancellation.IsCancelled() ? std::nullopt : std::optional(std::move(result));
}

const PreparedSelectionOutline::Loops& PreparedSelectionOutline::Points() const noexcept
{
    static const Loops empty;
    return _storage ? _storage->points : empty;
}
const void* PreparedSelectionOutline::SourceIdentity() const noexcept
{
    return _storage ? _storage->source.Identity() : nullptr;
}
Vec2 PreparedSelectionOutline::Anchor() const noexcept
{
    return _storage ? _storage->anchor : Vec2{};
}
std::uint64_t PreparedSelectionOutline::PointCount() const noexcept
{
    return _storage ? _storage->pointCount : 0;
}
std::uint64_t PreparedSelectionOutline::DeclaredOutputBytes() const noexcept
{
    return _storage ? _storage->outputBytes : 0;
}
std::weak_ptr<const void> PreparedSelectionOutline::WeakStorageForTest() const noexcept
{
    return _storage;
}
