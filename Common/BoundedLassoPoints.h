#pragma once

#include "Document.h"
#include "LocallyOwnedPublication.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

// One gesture's admitted worker-created arena. After owner acquisition only the writer seals fresh blocks;
// snapshots read previously sealed blocks and their own inline tail. They never inspect the writer cursor.
// Nodes are never overwritten/recycled while this arena exists. Backspace/re-extension can exhaust the node
// budget before the point limit; refusal preserves every accepted point and every outstanding snapshot.
class BoundedLassoPoints final
{
    static constexpr std::uint32_t kChunkPoints = 64;
    static constexpr std::uint32_t kNoBlock = (std::numeric_limits<std::uint32_t>::max)();
    struct Block final
    {
        std::array<Vec2, kChunkPoints> points;
        std::uint32_t previous;
        std::uint32_t count;
        SelectionBounds bounds;
    };
    static_assert(std::is_trivially_destructible_v<Block>);
    struct Arena final
    {
        explicit Arena(std::uint32_t limit) : blocks(std::allocator<Block>{}.allocate(limit)), capacity(limit) {}
        ~Arena() noexcept
        {
            // Trivial nodes have no recursive ownership. The enclosing accounted publication routes the
            // entire allocation to the original worker release sink, including tiny arenas on the owner.
            std::allocator<Block>{}.deallocate(blocks, capacity);
        }
        Arena(const Arena&) = delete;
        Arena& operator=(const Arena&) = delete;
        [[nodiscard]] bool PreparePagesForWork(CancelToken cancel) noexcept
        {
            // Move demand-zero page faults to the admitted worker before publishing an owner writer.
            // One byte per 4 KiB also touches every page on systems with a larger native page size.
            auto* bytes = reinterpret_cast<volatile unsigned char*>(blocks);
            const auto length = static_cast<std::size_t>(capacity) * sizeof(Block);
            for (std::size_t offset = 0; offset < length; offset += 4096)
            {
                if (cancel.IsCancelled())
                    return false;
                bytes[offset] = 0;
            }
            return !cancel.IsCancelled();
        }
        [[nodiscard]] bool SealOwner(const std::array<Vec2, kChunkPoints>& points, std::uint32_t previous,
                                     std::uint32_t count, SelectionBounds bounds, std::uint32_t& result) noexcept
        {
            if (used == capacity)
                return false;
            result = used;
            std::construct_at(blocks + used, Block{points, previous, count, bounds});
            ++used;
            return true;
        }
        Block* const blocks;
        const std::uint32_t capacity;
        std::uint32_t used = 0; // Single writer only; snapshots never read this field.
    };

  public:
    static constexpr std::uint32_t kMaxPoints = 8u * 1024u * 1024u;
    static constexpr std::uint32_t kMaxBlocks = (kMaxPoints - 1u) / kChunkPoints;
    class Snapshot final
    {
      public:
        Snapshot() noexcept = default;
        Snapshot(const Snapshot&) noexcept = default;
        Snapshot& operator=(const Snapshot&) noexcept = default;
        Snapshot(Snapshot&& source) noexcept
            : _arena(std::move(source._arena)), _prefix(std::exchange(source._prefix, kNoBlock)), _tail(source._tail),
              _tailCount(std::exchange(source._tailCount, 0)), _count(std::exchange(source._count, 0)),
              _bounds(std::exchange(source._bounds, std::nullopt))
        {
        }
        Snapshot& operator=(Snapshot&& source) noexcept
        {
            if (this != &source)
            {
                _arena = std::move(source._arena);
                _prefix = std::exchange(source._prefix, kNoBlock);
                _tail = source._tail;
                _tailCount = std::exchange(source._tailCount, 0);
                _count = std::exchange(source._count, 0);
                _bounds = std::exchange(source._bounds, std::nullopt);
            }
            return *this;
        }
        [[nodiscard]] std::uint32_t Count() const noexcept
        {
            return _count;
        }
        [[nodiscard]] bool Empty() const noexcept
        {
            return _count == 0;
        }
        [[nodiscard]] std::optional<SelectionBounds> Bounds() const noexcept
        {
            return _bounds;
        }
        [[nodiscard]] const void* ArenaIdentity() const noexcept
        {
            return _arena.get();
        }
        [[nodiscard]] bool SameCapture(const Snapshot& other) const noexcept
        {
            if (_arena != other._arena || _prefix != other._prefix || _tailCount != other._tailCount ||
                _count != other._count || _bounds != other._bounds)
                return false;
            for (std::uint32_t index = 0; index < _tailCount; ++index)
                if (_tail[index] != other._tail[index])
                    return false;
            return true;
        }
        [[nodiscard]] std::uint64_t RetainedArenaBytes() const noexcept
        {
            return _arena ? MemoryBytes(_arena->capacity) : 0;
        }
        [[nodiscard]] std::uint64_t TraversalScratchBytes() const noexcept
        {
            return static_cast<std::uint64_t>((_count - _tailCount) / kChunkPoints) * sizeof(std::uint32_t);
        }
        // Worker only. Admission covers this exact prefix stack plus output capacity/control overhead.
        // Cancellation is checked at every block; a visitor processes at most 64 points per callback.
        template <class Visitor> [[nodiscard]] bool VisitForWork(Visitor&& visitor, CancelToken cancel = {}) const
        {
            if (cancel.IsCancelled())
                return false;
            std::vector<std::uint32_t> order;
            order.reserve((_count - _tailCount) / kChunkPoints);
            for (auto index = _prefix; index != kNoBlock; index = _arena->blocks[index].previous)
            {
                if (cancel.IsCancelled())
                    return false;
                order.push_back(index);
            }
            for (auto sealedBlock = order.rbegin(); sealedBlock != order.rend(); ++sealedBlock)
            {
                if (cancel.IsCancelled() || !visitor(std::span<const Vec2>(_arena->blocks[*sealedBlock].points)))
                    return false;
            }
            return !cancel.IsCancelled() &&
                   (_tailCount == 0 || visitor(std::span<const Vec2>(_tail.data(), _tailCount))) &&
                   !cancel.IsCancelled();
        }
        [[nodiscard]] bool CopyToForWork(std::vector<Vec2>& output, CancelToken cancel = {}) const
        {
            if (cancel.IsCancelled())
                return false;
            output.clear();
            output.reserve(_count);
            return VisitForWork(
                [&](std::span<const Vec2> chunk)
                {
                    output.insert(output.end(), chunk.begin(), chunk.end());
                    return true;
                },
                cancel);
        }

      private:
        friend class BoundedLassoPoints;
        std::shared_ptr<const Arena> _arena;
        std::uint32_t _prefix = kNoBlock;
        std::array<Vec2, kChunkPoints> _tail{};
        std::uint32_t _tailCount = 0;
        std::uint32_t _count = 0;
        std::optional<SelectionBounds> _bounds;
    };
    BoundedLassoPoints(const BoundedLassoPoints&) = delete;
    BoundedLassoPoints& operator=(const BoundedLassoPoints&) = delete;
    BoundedLassoPoints(BoundedLassoPoints&& source) noexcept
        : _arena(std::move(source._arena)), _prefix(std::exchange(source._prefix, kNoBlock)), _tail(source._tail),
          _tailCount(std::exchange(source._tailCount, 0)), _count(std::exchange(source._count, 0)),
          _bounds(std::exchange(source._bounds, std::nullopt))
    {
    }
    BoundedLassoPoints& operator=(BoundedLassoPoints&& source) noexcept
    {
        if (this != &source)
        {
            _arena = std::move(source._arena);
            _prefix = std::exchange(source._prefix, kNoBlock);
            _tail = source._tail;
            _tailCount = std::exchange(source._tailCount, 0);
            _count = std::exchange(source._count, 0);
            _bounds = std::exchange(source._bounds, std::nullopt);
        }
        return *this;
    }
    // Original complete job admission must precede creation; bind its accounting owner after successful
    // Completed before exposing a writer/snapshot. A null accounting owner is not a production fallback.
    [[nodiscard]] static std::optional<BoundedLassoPoints> CreateForWork(
        std::shared_ptr<const SharedPublicationMemory> memory, std::uint32_t blocks, CancelToken cancel = {})
    {
        const auto bytes = MemoryBytes(blocks);
        if (!memory || !bytes || cancel.IsCancelled())
            return std::nullopt;
        auto arena = MakeLocallyOwnedPublicationObject<Arena>(
            std::move(memory), bytes, static_cast<std::uint64_t>(blocks) * sizeof(Block), blocks);
        if (!arena->PreparePagesForWork(cancel))
            return std::nullopt;
        return BoundedLassoPoints(std::move(arena));
    }
    [[nodiscard]] static constexpr std::uint64_t MemoryBytes(std::uint32_t blocks) noexcept
    {
        return blocks && blocks <= kMaxBlocks
                   ? LocallyOwnedPublicationAllowance<Arena>() + static_cast<std::uint64_t>(blocks) * sizeof(Block)
                   : 0;
    }
    [[nodiscard]] Snapshot CaptureOwner() const noexcept
    {
        Snapshot result;
        result._arena = _arena;
        result._prefix = _prefix;
        result._tail = _tail;
        result._tailCount = _tailCount;
        result._count = _count;
        result._bounds = _bounds;
        return result;
    }
    [[nodiscard]] bool PushOwner(Vec2 point) noexcept
    {
        if (!_arena || _count == kMaxPoints || !std::isfinite(point.x) || !std::isfinite(point.y))
            return false;
        if (_tailCount == kChunkPoints)
        {
            std::uint32_t prefix = kNoBlock;
            if (!_arena->SealOwner(_tail, _prefix, _count, _bounds.value(), prefix))
                return false;
            _prefix = prefix;
            _tailCount = 0;
        }
        _tail[_tailCount++] = point;
        ++_count;
        Include(_bounds, point);
        return true;
    }
    [[nodiscard]] bool PopOwner() noexcept
    {
        if (!_arena || _count == 0)
            return false;
        if (_tailCount == 0)
        {
            const auto& block = _arena->blocks[_prefix];
            _tail = block.points;
            _tailCount = kChunkPoints;
            _prefix = block.previous;
        }
        --_tailCount;
        --_count;
        _bounds = _prefix == kNoBlock ? std::optional<SelectionBounds>{} : _arena->blocks[_prefix].bounds;
        for (std::uint32_t index = 0; index < _tailCount; ++index)
            Include(_bounds, _tail[index]);
        return true;
    }
    [[nodiscard]] std::optional<Vec2> LastOwner() const noexcept
    {
        if (!_count)
            return std::nullopt;
        return _tailCount ? std::optional(_tail[_tailCount - 1])
                          : std::optional(_arena->blocks[_prefix].points[kChunkPoints - 1]);
    }
    [[nodiscard]] std::uint32_t CountOwner() const noexcept
    {
        return _count;
    }
    [[nodiscard]] std::uint32_t SealedBlocksOwner() const noexcept
    {
        return _arena ? _arena->used : 0;
    }
    void ClearOwner() noexcept
    {
        _prefix = kNoBlock;
        _tailCount = 0;
        _count = 0;
        _bounds.reset();
        // Do not reset the arena allocation cursor: outstanding snapshots still own its old immutable nodes.
    }

  private:
    explicit BoundedLassoPoints(std::shared_ptr<Arena> arena) noexcept : _arena(std::move(arena)) {}
    static void Include(std::optional<SelectionBounds>& bounds, Vec2 point) noexcept
    {
        if (!bounds)
            bounds = SelectionBounds{point.x, point.y, point.x, point.y};
        else
        {
            bounds->minX = (std::min)(bounds->minX, point.x);
            bounds->minY = (std::min)(bounds->minY, point.y);
            bounds->maxX = (std::max)(bounds->maxX, point.x);
            bounds->maxY = (std::max)(bounds->maxY, point.y);
        }
    }
    std::shared_ptr<Arena> _arena;
    std::uint32_t _prefix = kNoBlock;
    std::array<Vec2, kChunkPoints> _tail{};
    std::uint32_t _tailCount = 0;
    std::uint32_t _count = 0;
    std::optional<SelectionBounds> _bounds;
};
