#pragma once

#include "AccountedPublicationObject.h"
#include "Executor.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

struct RecordOwnershipBounds final
{
    std::uint64_t metadataBytes = 0;
    std::uint64_t ownershipBytes = 0;
    std::uint64_t rasterNodes = 0;
    std::uint64_t imagePixels = 0;
    std::uint64_t maskPixels = 0;
    [[nodiscard]] bool operator==(const RecordOwnershipBounds&) const noexcept = default;
};

// A record policy measures only owned children: inline records and the root allocation are added by Seal.
// Unregistered types have no complete bound. A missing bound never invents a zero-byte admission estimate.
template <typename Record> struct RecordOwnershipPolicy
{
    static constexpr bool enabled = false;
};

// Opt-in immutable key index, prepared only while sealing an exclusive builder. Duplicate keys retain
// the first record's index, matching the linear lookup reference; owning document validation rejects duplicates.
template <typename Record> struct RecordLookupPolicy
{
    static constexpr bool enabled = false;
    using Key = std::uint64_t;
};

// Immutable save admission projection, prepared with the records. Invalid serialization inputs keep a
// failed projection without changing the owning root's general validation/capture semantics.
struct RecordSavePlan final
{
    std::uint64_t metadataBytes = 0;
    std::uint64_t largestAssetBytes = 0;
    std::uint64_t assetCount = 0;
    bool valid = true;
    [[nodiscard]] bool operator==(const RecordSavePlan&) const noexcept = default;
};
template <typename Record> struct RecordSavePolicy
{
    static constexpr bool enabled = false;
};

// Published records are immutable. Capture and every read retain or borrow only this root; editing never
// obtains a mutable reference into it. Record's owned children must follow the same immutable/shared contract.
// Construction, cloning, sealing and destruction of the last root belong to the admitted worker/retirement
// path. This primitive alone does not bound a record's child allocations or replace whole-job admission.
template <typename Record, typename Allocator = std::allocator<Record>> class RecordRoot final
{
    static_assert(std::is_nothrow_move_constructible_v<Record>);
    static_assert(std::is_nothrow_move_assignable_v<Record>);
    static_assert(std::is_nothrow_destructible_v<Record>);
    static_assert(!RecordLookupPolicy<Record>::enabled ||
                  (std::is_nothrow_move_constructible_v<typename RecordLookupPolicy<Record>::Key> &&
                   std::is_nothrow_move_assignable_v<typename RecordLookupPolicy<Record>::Key> &&
                   std::is_nothrow_destructible_v<typename RecordLookupPolicy<Record>::Key>));

    using Key = typename RecordLookupPolicy<Record>::Key;
    using KeyEntry = std::pair<Key, std::size_t>;
    using KeyAllocator = typename std::allocator_traits<Allocator>::template rebind_alloc<KeyEntry>;
    using KeyIndex = std::vector<KeyEntry, KeyAllocator>;
    struct NoLookup final
    {
    };
    struct NoSavePlan final
    {
    };
    using SavePlan = std::conditional_t<RecordSavePolicy<Record>::enabled, RecordSavePlan, NoSavePlan>;

    using Lookup = std::conditional_t<RecordLookupPolicy<Record>::enabled, KeyIndex, NoLookup>;
    static constexpr bool kAllowUnindexedOverflow = []() consteval
    {
        if constexpr (requires { RecordLookupPolicy<Record>::allowUnindexedOverflow; })
            return RecordLookupPolicy<Record>::allowUnindexedOverflow;
        else
            return false;
    }();

  public:
    using Container = std::vector<Record, Allocator>;
    using const_iterator = typename Container::const_iterator;
    static_assert(std::is_nothrow_move_constructible_v<Container>);
    static_assert(std::is_nothrow_move_assignable_v<Container>);

    RecordRoot() noexcept = default;
    RecordRoot(const RecordRoot&) noexcept = default;
    RecordRoot& operator=(const RecordRoot&) noexcept = default;
    RecordRoot(RecordRoot&&) noexcept = default;
    RecordRoot& operator=(RecordRoot&&) noexcept = default;

    [[nodiscard]] const Container& Read() const noexcept
    {
        if (_root)
            return _root->records;
        static const Container empty;
        return empty;
    }
    [[nodiscard]] std::size_t size() const noexcept
    {
        return _root ? _root->records.size() : 0;
    }
    [[nodiscard]] std::size_t capacity() const noexcept
    {
        return _root ? _root->records.capacity() : 0;
    }
    [[nodiscard]] bool empty() const noexcept
    {
        return size() == 0;
    }
    [[nodiscard]] const Record& operator[](std::size_t index) const noexcept
    {
        return Read()[index];
    }
    [[nodiscard]] const_iterator begin() const noexcept
    {
        return _root ? _root->records.begin() : const_iterator{};
    }
    [[nodiscard]] const_iterator end() const noexcept
    {
        return _root ? _root->records.end() : const_iterator{};
    }
    [[nodiscard]] const Record* data() const noexcept
    {
        return _root ? _root->records.data() : nullptr;
    }
    [[nodiscard]] const Record& front() const noexcept
    {
        return Read().front();
    }
    [[nodiscard]] const Record& back() const noexcept
    {
        return Read().back();
    }
    [[nodiscard]] auto rbegin() const noexcept
    {
        return std::make_reverse_iterator(end());
    }
    [[nodiscard]] auto rend() const noexcept
    {
        return std::make_reverse_iterator(begin());
    }
    [[nodiscard]] const void* Identity() const noexcept
    {
        return _root.get();
    }
    [[nodiscard]] const SavePlan& SaveMemoryPlan() const noexcept
        requires(RecordSavePolicy<Record>::enabled)
    {
        if (_root)
            return _root->savePlan;
        static const SavePlan empty;
        return empty;
    }
    [[nodiscard]] std::weak_ptr<const void> WeakStorageForTest() const noexcept
    {
        return _root;
    }
    [[nodiscard]] std::optional<RecordOwnershipBounds> OwnershipBounds() const noexcept
    {
        return _root ? _root->bounds : std::nullopt;
    }
    // Borrow/copy no record children and allocate nothing. The source root owns index and records together.
    [[nodiscard]] std::optional<std::size_t> FindIndex(const Key& key) const noexcept
        requires(RecordLookupPolicy<Record>::enabled)
    {
        if (!_root)
            return std::nullopt;
        const auto& index = _root->lookup;
        const auto found =
            std::lower_bound(index.begin(), index.end(), key,
                             [](const KeyEntry& entry, const Key& value) noexcept { return entry.first < value; });
        return found != index.end() && !(key < found->first) ? std::optional(found->second) : std::nullopt;
    }
    [[nodiscard]] const Record* FindKey(const Key& key) const noexcept
        requires(RecordLookupPolicy<Record>::enabled)
    {
        const auto index = FindIndex(key);
        return index ? &_root->records[index.value()] : nullptr;
    }
    [[nodiscard]] bool HasUniqueKeys() const noexcept
        requires(RecordLookupPolicy<Record>::enabled)
    {
        return !_root || _root->uniqueKeys;
    }
    void swap(RecordRoot& other) noexcept
    {
        _root.swap(other._root);
    }
    // Identity is a fast path, never the value-equality contract of independently prepared roots.
    [[nodiscard]] bool operator==(const RecordRoot& other) const
        noexcept(noexcept(std::declval<const Record&>() == std::declval<const Record&>()))
    {
        if (_root == other._root)
            return true;
        if (size() != other.size())
            return false;
        for (std::size_t index = 0; index < size(); ++index)
            if (Read()[index] != other.Read()[index])
                return false;
        return true;
    }

    class Builder final
    {
      public:
        // The caller admits the requested record capacity plus children and the sealing allocation first.
        // No insertion grows this buffer. Cached capacity exposes the actual allocator-provided capacity.
        explicit Builder(std::size_t limit, const Allocator& allocator = {})
            : _records(allocator), _limit(limit), _sealable(true)
        {
            if (limit > _records.max_size())
            {
                _limit = 0;
                _sealable = false;
                return;
            }
            _records.reserve(limit);
        }
        Builder(const Builder&) = delete;
        Builder& operator=(const Builder&) = delete;
        Builder(Builder&& other) noexcept
            : _records(std::move(other._records)), _limit(std::exchange(other._limit, 0)),
              _sealable(std::exchange(other._sealable, false))
        {
        }
        Builder& operator=(Builder&& other) noexcept
        {
            if (this != &other)
            {
                _records = std::move(other._records);
                _limit = std::exchange(other._limit, 0);
                _sealable = std::exchange(other._sealable, false);
            }
            return *this;
        }

        [[nodiscard]] bool CanSeal() const noexcept
        {
            return _sealable;
        }
        [[nodiscard]] const Container& Read() const noexcept
        {
            static const Container empty;
            return _sealable ? _records : empty;
        }
        [[nodiscard]] std::size_t Limit() const noexcept
        {
            return _limit;
        }
        [[nodiscard]] bool Append(const Record& record)
        {
            if (!_sealable || _records.size() >= _limit)
                return false;
            _records.push_back(record);
            return true;
        }
        [[nodiscard]] bool Append(Record&& record) noexcept
        {
            if (!_sealable || _records.size() >= _limit)
                return false;
            _records.push_back(std::move(record));
            return true;
        }
        [[nodiscard]] bool Replace(std::size_t index, Record&& record) noexcept
        {
            if (!_sealable || index >= _records.size())
                return false;
            _records[index] = std::move(record);
            return true;
        }
        [[nodiscard]] bool Insert(std::size_t index, Record&& record) noexcept
        {
            if (!_sealable || index > _records.size() || _records.size() >= _limit)
                return false;
            _records.insert(_records.begin() + static_cast<std::ptrdiff_t>(index), std::move(record));
            return true;
        }
        [[nodiscard]] bool Erase(std::size_t index) noexcept
        {
            if (!_sealable || index >= _records.size())
                return false;
            _records.erase(_records.begin() + static_cast<std::ptrdiff_t>(index));
            return true;
        }
        // Transfer an exclusively owned worker buffer. All mutable borrows must end before this call.
        // Refusal preserves the input, including its allocation owner. Admit old/new reserve overlap first.
        [[nodiscard]] static std::optional<Builder> Adopt(Container&& records, std::size_t limit)
        {
            if (records.size() > limit || records.capacity() > limit || limit > records.max_size())
                return std::nullopt;
            records.reserve(limit);
            return Builder(std::move(records), limit, AdoptTag{});
        }
        [[nodiscard]] std::optional<RecordRoot> Seal(CancelToken cancellation = {})
        {
            return SealImpl(cancellation, {});
        }
        // Worker-only under the original complete reservation. The owning route requires checked complete
        // bounds. Once allocation starts it consumes this builder; failure ends the selected operation.
        // The reference Seal retains its existing allocation-failure retry contract.
        [[nodiscard]] std::optional<RecordRoot> SealOwned(std::shared_ptr<const SharedPublicationMemory> memory,
                                                          CancelToken cancellation = {})
        {
            return memory && memory.use_count() ? SealImpl(cancellation, std::move(memory)) : std::nullopt;
        }

      private:
        [[nodiscard]] std::optional<RecordRoot> SealImpl(CancelToken cancellation,
                                                         std::shared_ptr<const SharedPublicationMemory> memory)
        {
            if (!_sealable || cancellation.IsCancelled())
                return std::nullopt;
            bool uniqueKeys = true;
            [[maybe_unused]] bool buildIndex = true;
            if constexpr (RecordLookupPolicy<Record>::enabled)
            {
                static_assert(RecordLookupPolicy<Record>::maximumRecords > 0);
                if (_records.size() > RecordLookupPolicy<Record>::maximumRecords)
                {
                    if constexpr (!kAllowUnindexedOverflow)
                        return std::nullopt;
                    else
                    {
                        // Preserve malformed input for the owning validator without
                        // building an index beyond its supported admission ceiling.
                        buildIndex = false;
                        uniqueKeys = false; // Unavailable uniqueness cannot authorize an edit.
                    }
                }
            }
            Lookup lookup = [&]()
            {
                if constexpr (RecordLookupPolicy<Record>::enabled)
                    return Lookup(KeyAllocator(_records.get_allocator()));
                else
                    return NoLookup{};
            }();
            if constexpr (RecordLookupPolicy<Record>::enabled)
            {
                static_assert(noexcept(RecordLookupPolicy<Record>::KeyOf(std::declval<const Record&>())));
                static_assert(noexcept(std::declval<const Key&>() < std::declval<const Key&>()));
                if (buildIndex)
                {
                    lookup.reserve(_records.size());
                    for (std::size_t i = 0; i < _records.size(); ++i)
                    {
                        if (cancellation.IsCancelled())
                            return std::nullopt;
                        lookup.emplace_back(RecordLookupPolicy<Record>::KeyOf(_records[i]), i);
                    }
                    std::sort(lookup.begin(), lookup.end(),
                              [](const KeyEntry& left, const KeyEntry& right) noexcept
                              {
                                  return left.first < right.first ||
                                         (!(right.first < left.first) && left.second < right.second);
                              });
                    for (std::size_t i = 1; i < lookup.size(); ++i)
                    {
                        if (cancellation.IsCancelled())
                            return std::nullopt;
                        if (!(lookup[i - 1].first < lookup[i].first))
                            uniqueKeys = false;
                    }
                }
            }
            auto bounds = MeasureRecords(_records, lookup, cancellation);
            const auto savePlan = MeasureSavePlan(_records, cancellation);
            if (cancellation.IsCancelled())
                return std::nullopt;
            std::shared_ptr<const Storage> root;
            if (memory.use_count())
            {
                constexpr auto extra = AccountedPublicationObjectAllowance<Storage>() - sizeof(Storage) - 64;
                constexpr auto maximum = (std::numeric_limits<std::uint64_t>::max)();
                if (!bounds || bounds->ownershipBytes < bounds->metadataBytes ||
                    bounds->metadataBytes > maximum - extra || bounds->ownershipBytes > maximum - extra)
                    return std::nullopt;
                bounds->metadataBytes += extra;
                bounds->ownershipBytes += extra;
                // Seal exact complete bounds before constructing shared controls. No formerly published
                // storage becomes writable. Both data and its last weak control retain the original owner.
                _sealable = false;
                _limit = 0;
                const auto localChildren = bounds->metadataBytes - extra - sizeof(Storage) - 64;
                root = MakeAccountedPublicationObject<const Storage>(std::move(memory), bounds->ownershipBytes,
                                                                     localChildren, std::move(_records),
                                                                     std::move(lookup), bounds, uniqueKeys, savePlan);
            }
            else
            {
                // Reference route: allocation precedes moving private records and remains retryable.
                root = std::allocate_shared<const Storage>(_records.get_allocator(), std::move(_records),
                                                           std::move(lookup), bounds, uniqueKeys, savePlan);
            }
            _sealable = false;
            _limit = 0;
            return RecordRoot(std::move(root));
        }

      public:
        [[nodiscard]] static std::optional<Builder> Clone(const RecordRoot& source, std::size_t limit,
                                                          CancelToken cancellation = {},
                                                          const Allocator& allocator = {})
        {
            if (cancellation.IsCancelled() || source.size() > limit)
                return std::nullopt;
            Builder copy(limit, allocator);
            if (!copy.CanSeal())
                return std::nullopt;
            for (const auto& record : source)
            {
                if (cancellation.IsCancelled())
                    return std::nullopt;
                if (!copy.Append(record))
                    return std::nullopt;
            }
            return cancellation.IsCancelled() ? std::nullopt : std::optional<Builder>(std::move(copy));
        }

      private:
        struct AdoptTag
        {
        };
        Builder(Container&& records, std::size_t limit, AdoptTag) noexcept
            : _records(std::move(records)), _limit(limit), _sealable(true)
        {
        }
        Container _records;
        std::size_t _limit = 0;
        bool _sealable = false;
    };

  private:
    struct Storage final
    {
        Storage(Container&& values, Lookup&& preparedLookup, std::optional<RecordOwnershipBounds> measured, bool unique,
                SavePlan preparedSavePlan) noexcept
            : records(std::move(values)), lookup(std::move(preparedLookup)), bounds(measured), uniqueKeys(unique),
              savePlan(preparedSavePlan)
        {
        }
        Container records;
        [[no_unique_address]] Lookup lookup;
        std::optional<RecordOwnershipBounds> bounds;
        bool uniqueKeys = true;
        [[no_unique_address]] SavePlan savePlan;
    };
    [[nodiscard]] static SavePlan MeasureSavePlan(const Container& records, CancelToken cancellation) noexcept
    {
        SavePlan plan;
        static_cast<void>(records);
        static_cast<void>(cancellation);
        if constexpr (RecordSavePolicy<Record>::enabled)
        {
            static_assert(noexcept(RecordSavePolicy<Record>::Accumulate(plan, std::declval<const Record&>())));
            for (const auto& record : records)
            {
                if (cancellation.IsCancelled() || !RecordSavePolicy<Record>::Accumulate(plan, record))
                {
                    plan.valid = false;
                    break;
                }
            }
        }
        return plan;
    }
    [[nodiscard]] static std::optional<RecordOwnershipBounds> MeasureRecords(const Container& records,
                                                                             const Lookup& lookup,
                                                                             CancelToken cancellation) noexcept
    {
        if (cancellation.IsCancelled())
            return std::nullopt;
        if constexpr (!RecordOwnershipPolicy<Record>::enabled)
        {
            return std::nullopt;
        }
        else
        {
            static_assert(noexcept(RecordOwnershipPolicy<Record>::Measure(std::declval<const Record&>())));
            constexpr auto maximum = (std::numeric_limits<std::uint64_t>::max)();
            if (records.capacity() > (maximum - sizeof(Storage) - 64) / sizeof(Record))
                return std::nullopt;
            std::uint64_t rootBytes = sizeof(Storage) + 64 + records.capacity() * sizeof(Record);
            if constexpr (RecordLookupPolicy<Record>::enabled)
            {
                // Actual one-vector capacity, control/allocator rounding and Debug proxy. No per-key node heap.
                if (rootBytes > maximum - 128 || lookup.capacity() > (maximum - rootBytes - 128) / sizeof(KeyEntry))
                    return std::nullopt;
                rootBytes += 128 + lookup.capacity() * sizeof(KeyEntry);
            }
            RecordOwnershipBounds total{rootBytes, rootBytes, 0};
            const auto add = [](std::uint64_t& value, std::uint64_t amount) noexcept
            {
                if (amount > (std::numeric_limits<std::uint64_t>::max)() - value)
                    return false;
                value += amount;
                return true;
            };
            for (const auto& record : records)
            {
                if (cancellation.IsCancelled())
                    return std::nullopt;
                const auto bounds = RecordOwnershipPolicy<Record>::Measure(record);
                if (!bounds || bounds.value().ownershipBytes < bounds.value().metadataBytes ||
                    !add(total.metadataBytes, bounds.value().metadataBytes) ||
                    !add(total.ownershipBytes, bounds.value().ownershipBytes) ||
                    !add(total.rasterNodes, bounds.value().rasterNodes) ||
                    !add(total.imagePixels, bounds.value().imagePixels) ||
                    !add(total.maskPixels, bounds.value().maskPixels))
                    return std::nullopt;
            }
            return total;
        }
    }
    explicit RecordRoot(std::shared_ptr<const Storage> root) noexcept : _root(std::move(root)) {}
    std::shared_ptr<const Storage> _root;
};
