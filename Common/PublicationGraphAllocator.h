#pragma once

#include <cstddef>
#include <memory>
#include <new>
#include <utility>

class SharedPublicationMemory;

// Shared preview control blocks retain the original accounting owner through their last weak reader too.
// This allocator owns no pixels and performs no admission. Allocate only on the admitted worker.
template <class T> struct PublicationGraphAllocator final
{
    using value_type = T;
    std::shared_ptr<const SharedPublicationMemory> memory;

    explicit PublicationGraphAllocator(std::shared_ptr<const SharedPublicationMemory> value) noexcept
        : memory(std::move(value))
    {
    }
    template <class U>
    PublicationGraphAllocator(const PublicationGraphAllocator<U>& other) noexcept : memory(other.memory)
    {
    }
    [[nodiscard]] T* allocate(std::size_t count)
    {
        if (count > 4096 / sizeof(T))
            throw std::bad_alloc{};
        return std::allocator<T>{}.allocate(count);
    }
    void deallocate(T* value, std::size_t count) noexcept
    {
        // Preserve the accounting owner while the allocator's original control block storage disappears.
        auto held = std::move(memory);
        std::allocator<T>{}.deallocate(value, count);
        held.reset();
    }
    template <class U> [[nodiscard]] bool operator==(const PublicationGraphAllocator<U>& other) const noexcept
    {
        return memory == other.memory;
    }
};
