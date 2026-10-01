#pragma once

#include <cstddef>
#include <memory>
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

    void clear() noexcept
    {
        _root.reset();
    }
    void swap(HistoryEntries& other) noexcept
    {
        _root.swap(other._root);
    }
    void push_back(Handle value)
    {
        auto next = Clone();
        next->push_back(std::move(value));
        _root = std::move(next);
    }
    void pop_back()
    {
        auto next = Clone();
        next->pop_back();
        _root = std::move(next);
    }
    void erase(const_iterator first, const_iterator last)
    {
        if (first == last)
            return;
        const auto offset = first - begin();
        const auto count = last - first;
        auto next = Clone();
        next->erase(next->begin() + offset, next->begin() + offset + count);
        _root = std::move(next);
    }

  private:
    [[nodiscard]] const Container& Read() const noexcept
    {
        static const Container empty;
        return _root ? *_root : empty;
    }
    [[nodiscard]] std::shared_ptr<Container> Clone() const
    {
        // A relaxed shared_ptr use_count is not a synchronization proof that another reader has finished.
        // Never write a formerly shared root in place; publication exposes only the completed private clone.
        return std::make_shared<Container>(Read());
    }
    std::shared_ptr<const Container> _root;
};
