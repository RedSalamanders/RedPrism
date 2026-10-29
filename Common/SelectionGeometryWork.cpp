#include "Document.h"
#include "SelectionNudgeWork.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <new>
#include <vector>

namespace
{
// Lifetime is declared first so geometry dies before its last accounting owner.
#pragma warning(push)
#pragma warning(disable : 4324)
struct GeometryNode final : DeferredReleaseNode
{
    std::shared_ptr<const void> lifetime;
    std::vector<SelectionLoop> loops;
};
#pragma warning(pop)
struct FreeGeometry final
{
    void operator()(GeometryNode* node) const noexcept
    {
        node->destroy = [](DeferredReleaseNode* value) noexcept
        { const std::unique_ptr<GeometryNode> owned(static_cast<GeometryNode*>(value)); };
        if (!TryDeferRelease(*node, DeferredReleasePolicy::AlwaysOnOwner))
            node->destroy(node);
    }
};
template <class T> struct GeometryAllocator final
{
    using value_type = T;
    std::shared_ptr<const void> lifetime;
    explicit GeometryAllocator(std::shared_ptr<const void> value) noexcept : lifetime(std::move(value)) {}
    template <class U> GeometryAllocator(const GeometryAllocator<U>& other) noexcept : lifetime(other.lifetime) {}
    [[nodiscard]] T* allocate(std::size_t count)
    {
        if (count > 4096 / sizeof(T))
            throw std::bad_alloc{};
        return std::allocator<T>{}.allocate(count);
    }
    void deallocate(T* value, std::size_t count) noexcept
    {
        auto held = lifetime;
        std::allocator<T>{}.deallocate(value, count);
    }
    template <class U> bool operator==(const GeometryAllocator<U>& other) const noexcept
    {
        return !lifetime.owner_before(other.lifetime) && !other.lifetime.owner_before(lifetime);
    }
};
} // namespace

std::uint64_t SelectionLoops::WorkOwnershipAllowance(std::uint64_t loopCount) noexcept
{
    constexpr std::uint64_t fixed = sizeof(GeometryNode) + 4096 + 256;
    constexpr auto maximum = (std::numeric_limits<std::uint64_t>::max)();
    // Additional allowance for the supported allocator's per-vector bookkeeping, including Debug proxies.
    return loopCount > (maximum - fixed) / 256 ? 0 : fixed + loopCount * 256;
}

std::optional<SelectionLoops> SelectionLoops::AdoptForWork(std::vector<SelectionLoop> loops, CancelToken cancellation,
                                                           std::shared_ptr<const void> lifetime,
                                                           void (*checkpoint)(void*, std::uint64_t) noexcept,
                                                           void* context)
{
    if (cancellation.IsCancelled())
        return std::nullopt;
    SelectionLoops result;
    if (loops.empty())
        return result;
    constexpr auto maximum = (std::numeric_limits<std::uint64_t>::max)();
    const auto add = [&](std::uint64_t count, std::uint64_t size)
    {
        if (count > maximum / size || count * size > maximum - result._storageBytes)
            return false;
        result._storageBytes += count * size;
        return true;
    };
    if (!add(1, sizeof(loops)) || !add(loops.capacity(), sizeof(SelectionLoop)))
        return std::nullopt;
    std::uint64_t work = 0;
    const auto step = [&]()
    {
        const auto current = work++;
        if ((current & 63) != 0)
            return true;
        if (checkpoint)
            checkpoint(context, current);
        return !cancellation.IsCancelled();
    };
    for (const auto& loop : loops)
    {
        if (!step())
            return std::nullopt;
        if (!add(loop.capacity(), sizeof(Vec2)) || loop.size() > maximum - result._pointCount)
            return std::nullopt;
        result._pointCount += loop.size();
        for (const auto& point : loop)
        {
            if (!step())
                return std::nullopt;
            // Matches LoopsBounds: non-finite lasso/transform draft points do not contribute to bounds.
            if (!std::isfinite(point.x) || !std::isfinite(point.y))
                continue;
            if (!result._bounds)
                result._bounds = SelectionBounds{point.x, point.y, point.x, point.y};
            else
            {
                auto& bounds = *result._bounds;
                bounds.minX = std::min(bounds.minX, point.x);
                bounds.minY = std::min(bounds.minY, point.y);
                bounds.maxX = std::max(bounds.maxX, point.x);
                bounds.maxY = std::max(bounds.maxY, point.y);
            }
        }
    }
    if (cancellation.IsCancelled() || !result.SealPreparedStorageForWork(std::move(loops), std::move(lifetime)))
        return std::nullopt;
    return cancellation.IsCancelled() ? std::nullopt : std::optional(std::move(result));
}

bool SelectionLoops::SealPreparedStorageForWork(std::vector<SelectionLoop> loops, std::shared_ptr<const void> lifetime)
{
    const auto allowance = WorkOwnershipAllowance(loops.size());
    if (_loops || loops.empty() || !allowance ||
        allowance > (std::numeric_limits<std::uint64_t>::max)() - _storageBytes)
        return false;
    auto owned = std::make_unique<GeometryNode>();
    owned->lifetime = lifetime;
    owned->loops = std::move(loops);
    owned->bytes = _storageBytes + allowance;
    auto storage =
        std::shared_ptr<GeometryNode>(owned.release(), FreeGeometry{}, GeometryAllocator<GeometryNode>{lifetime});
    const auto* view = &storage->loops;
    _loops = std::shared_ptr<const std::vector<SelectionLoop>>{std::move(storage), view};
    return true;
}

// Only this producer may adopt its construction-cached geometry. No uncancellable second path traversal.
class SelectionNudgeBuilder final
{
  public:
    static std::optional<SelectionLoops> Build(const SelectionLoops& source, double dx, double dy,
                                               CancelToken cancellation, SelectionNudgeObserver observer,
                                               std::shared_ptr<const void> geometryLifetime = {})
    {
        if (!std::isfinite(dx) || !std::isfinite(dy))
            return std::nullopt;
        std::uint64_t work = 0;
        const auto checkpoint = [&](SelectionNudgeStage stage)
        {
            if (observer.checkpoint)
                observer.checkpoint(observer.context, stage, work);
            return !cancellation.IsCancelled();
        };
        if (!checkpoint(SelectionNudgeStage::Begin))
            return std::nullopt;
        if (dx == 0 && dy == 0)
            return checkpoint(SelectionNudgeStage::Complete) ? std::optional(source) : std::nullopt;
        std::vector<SelectionLoop> loops;
        loops.reserve(source.size());
        SelectionLoops result;
        for (const auto& loop : source)
        {
            if ((work++ % 1024) == 0 && !checkpoint(SelectionNudgeStage::Translate))
                return std::nullopt;
            SelectionLoop translated;
            translated.reserve(loop.size());
            for (const auto& point : loop)
            {
                if ((work++ % 1024) == 0 && !checkpoint(SelectionNudgeStage::Translate))
                    return std::nullopt;
                const Vec2 next{point.x + static_cast<double>(dx), point.y + static_cast<double>(dy)};
                translated.push_back(next);
                if (std::isfinite(next.x) && std::isfinite(next.y))
                {
                    if (!result._bounds)
                        result._bounds = SelectionBounds{next.x, next.y, next.x, next.y};
                    else
                    {
                        result._bounds.value().minX = std::min(result._bounds.value().minX, next.x);
                        result._bounds.value().minY = std::min(result._bounds.value().minY, next.y);
                        result._bounds.value().maxX = std::max(result._bounds.value().maxX, next.x);
                        result._bounds.value().maxY = std::max(result._bounds.value().maxY, next.y);
                    }
                }
            }
            result._pointCount += translated.size();
            result._storageBytes += translated.capacity() * sizeof(Vec2);
            loops.push_back(std::move(translated));
        }
        if (!checkpoint(SelectionNudgeStage::Complete))
            return std::nullopt;
        if (!loops.empty())
        {
            result._storageBytes += sizeof(loops) + loops.capacity() * sizeof(SelectionLoop);
            if (geometryLifetime)
            {
                if (!result.SealPreparedStorageForWork(std::move(loops), std::move(geometryLifetime)))
                    return std::nullopt;
            }
            else
                result._loops = std::make_shared<const std::vector<SelectionLoop>>(std::move(loops));
        }
        return !cancellation.IsCancelled() ? std::optional(std::move(result)) : std::nullopt;
    }
};

std::optional<SelectionLoops> TranslateSelectionLoopsForWork(const SelectionLoops& source, double dx, double dy,
                                                             CancelToken cancellation, SelectionNudgeObserver observer,
                                                             std::shared_ptr<const void> geometryLifetime)
{
    return SelectionNudgeBuilder::Build(source, dx, dy, cancellation, observer, std::move(geometryLifetime));
}
