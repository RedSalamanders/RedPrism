#pragma once

#include "LocallyOwnedPublication.h"
#include "RecordRoot.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

// Captures retain immutable stack roots. Detached preparation (or the legacy inline reference API) constructs
// replacement roots. Reads never clone, including non-const range iteration; captured roots remain immutable.
template <typename Handle> class HistoryEntries final
{
  public:
    using Container = std::vector<Handle>;
    using const_iterator = typename Container::const_iterator;
    HistoryEntries() noexcept = default;
    HistoryEntries(const HistoryEntries&) noexcept = default;
    HistoryEntries& operator=(const HistoryEntries&) noexcept = default;
    HistoryEntries(HistoryEntries&&) noexcept = default;
    HistoryEntries& operator=(HistoryEntries&&) noexcept = default;

    [[nodiscard]] std::size_t size() const noexcept
    {
        return Read().size();
    }
    [[nodiscard]] std::size_t capacity() const noexcept
    {
        return Read().capacity();
    }
    [[nodiscard]] bool empty() const noexcept
    {
        return Read().empty();
    }
    [[nodiscard]] const Handle& back() const noexcept
    {
        return Read().back();
    }
    [[nodiscard]] const_iterator begin() const noexcept
    {
        return Read().begin();
    }
    [[nodiscard]] const_iterator end() const noexcept
    {
        return Read().end();
    }
    [[nodiscard]] const void* Identity() const noexcept
    {
        return _root.get();
    }

    // Construction-cached complete entry ownership. The default root owns nothing; unavailable child bounds
    // propagate as nullopt. Readers never measure entries or expand their document/raster owners.
    [[nodiscard]] std::optional<RecordOwnershipBounds> OwnershipBounds() const noexcept
    {
        return _root ? _root->bounds : std::optional(RecordOwnershipBounds{});
    }
    // This immutable stack allocation only, from its sealed capacity in constant time. A complete unique
    // index charges the separately owned entries/snapshots instead of summing their occurrence bounds here.
    [[nodiscard]] std::optional<std::uint64_t> StorageMetadataBytes() const noexcept
    {
        if (!_root)
            return std::uint64_t{};
        // The original reference storage layout remains unchanged. The immutable shared control's
        // deleter identifies the selected owned factory in constant time, without another header field.
        const bool owned = std::get_deleter<AccountedPublicationDetail::Retire<Storage>>(_root) != nullptr;
        const auto allowance = owned ? LocallyOwnedPublicationAllowance<Storage>() : sizeof(Storage) + 64;
        const auto padding = owned ? std::uint64_t{128} : std::uint64_t{};
        constexpr auto maximum = (std::numeric_limits<std::uint64_t>::max)();
        if (_root->values.capacity() > (maximum - allowance - padding) / sizeof(Handle))
            return std::nullopt;
        return allowance + padding + _root->values.capacity() * sizeof(Handle);
    }

    void clear() noexcept
    {
        _root.reset();
    }
    void swap(HistoryEntries& other) noexcept
    {
        _root.swap(other._root);
    }
    void push_back(Handle value, std::shared_ptr<const SharedPublicationMemory> memory = {})
    {
        auto next = Clone();
        next->values.push_back(std::move(value));
        Publish(std::move(next), std::move(memory));
    }
    void pop_back()
    {
        auto next = Clone();
        next->values.pop_back();
        Publish(std::move(next));
    }
    void erase(const_iterator first, const_iterator last, std::shared_ptr<const SharedPublicationMemory> memory = {})
    {
        if (first == last)
            return;
        const auto offset = first - begin();
        const auto count = last - first;
        auto next = Clone();
        next->values.erase(next->values.begin() + offset, next->values.begin() + offset + count);
        Publish(std::move(next), std::move(memory));
    }

  private:
    struct Storage final
    {
        explicit Storage(const Container& source) : values(source) {}
        Container values;
        std::optional<RecordOwnershipBounds> bounds;
    };
    [[nodiscard]] const Container& Read() const noexcept
    {
        static const Container empty;
        return _root ? _root->values : empty;
    }
    [[nodiscard]] std::shared_ptr<Storage> Clone(std::shared_ptr<const SharedPublicationMemory> memory = {}) const
    {
        // A relaxed shared_ptr use_count is not a synchronization proof that another reader has finished.
        // Never write a formerly shared root in place; publication exposes only the completed private clone.
        return MakePublicationObject<Storage>(std::move(memory), kDeferredReleaseThreshold, Read());
    }
    void Publish(std::shared_ptr<Storage> next, std::shared_ptr<const SharedPublicationMemory> memory = {})
    {
        constexpr auto maximum = (std::numeric_limits<std::uint64_t>::max)();
        const auto allowance = memory ? LocallyOwnedPublicationAllowance<Storage>() : sizeof(Storage) + 64;
        const auto padding = memory ? std::uint64_t{128} : std::uint64_t{};
        if (next->values.capacity() <= (maximum - allowance - padding) / sizeof(Handle))
        {
            const auto dynamicBytes = next->values.capacity() * sizeof(Handle) + padding;
            const auto rootBytes = allowance + dynamicBytes;
            RecordOwnershipBounds total{rootBytes, rootBytes, 0};
            bool measurable = true;
            if constexpr (requires(const Handle& entry) { entry->OwnershipBounds(); })
            {
                const auto add = [](std::uint64_t& bytes, std::uint64_t amount) noexcept
                {
                    if (amount > (std::numeric_limits<std::uint64_t>::max)() - bytes)
                        return false;
                    bytes += amount;
                    return true;
                };
                for (const auto& entry : next->values)
                {
                    static_assert(noexcept(entry->OwnershipBounds()));
                    const auto child = entry ? entry->OwnershipBounds() : std::nullopt;
                    if (!child || child->ownershipBytes < child->metadataBytes ||
                        !add(total.metadataBytes, child->metadataBytes) ||
                        !add(total.ownershipBytes, child->ownershipBytes) ||
                        !add(total.rasterNodes, child->rasterNodes))
                    {
                        measurable = false;
                        break;
                    }
                }
            }
            else
                measurable = next->values.empty();
            if (measurable)
                next->bounds = total;
        }
        if (memory)
        {
            // Only the completed private vector is exposed. Constructor failure keeps the old stack intact.
            // Borrowed entry/snapshot roots are already charged by their own controls, not this stack.
            if (!next->bounds)
                throw std::bad_alloc{};
            const auto dynamicBytes = next->values.capacity() * sizeof(Handle) + 128;
            const auto retiredBytes = next->bounds->ownershipBytes;
            _root = MakeLocallyOwnedPublicationObject<Storage>(std::move(memory), retiredBytes, dynamicBytes,
                                                               std::move(*next));
        }
        else
            _root = std::move(next);
    }
    std::shared_ptr<const Storage> _root;
};
