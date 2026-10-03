#pragma once

#include "Executor.h"

#include <cstddef>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

// Published records are immutable. Capture and every read retain or borrow only this root; editing never
// obtains a mutable reference into it. Record's owned children must follow the same immutable/shared contract.
// Construction, cloning, sealing and destruction of the last root belong to the admitted worker/retirement
// path. This primitive alone does not bound a record's child allocations or replace whole-job admission.
template <typename Record, typename Allocator = std::allocator<Record>> class RecordRoot final
{
    static_assert(std::is_nothrow_move_constructible_v<Record>);
    static_assert(std::is_nothrow_move_assignable_v<Record>);
    static_assert(std::is_nothrow_destructible_v<Record>);

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
        static const Container empty;
        return _root ? *_root : empty;
    }
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
    [[nodiscard]] const Record& operator[](std::size_t index) const noexcept
    {
        return Read()[index];
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
        [[nodiscard]] std::optional<RecordRoot> Seal()
        {
            if (!_sealable)
                return std::nullopt;
            // Allocation precedes the vector move. A failed allocation leaves the builder sealable for retry.
            auto root = std::allocate_shared<const Container>(_records.get_allocator(), std::move(_records));
            _sealable = false;
            _limit = 0;
            return RecordRoot(std::move(root));
        }

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
    explicit RecordRoot(std::shared_ptr<const Container> root) noexcept : _root(std::move(root)) {}
    std::shared_ptr<const Container> _root;
};
