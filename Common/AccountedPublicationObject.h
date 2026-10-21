#pragma once

#include "PublicationAllocationLedger.h"
#include "PublicationObject.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>

// Opt-in local allocation accounting, separate from the immutable scheduler reservation. The original
// admission covers construction before any of these allocations. Every newly owned child allocation must
// be declared by its producer; strongly retained older owners are excluded. Never use a partial graph's
// counter as a complete output bound. Binding is allowed only after useful producers have joined.
namespace AccountedPublicationDetail
{
class Allocation final
{
  public:
    Allocation(std::shared_ptr<const SharedPublicationMemory> memory, std::uint64_t bytes)
        : _memory(std::move(memory)), _bytes(bytes)
    {
        if (!TryAccountPublicationAllocation(_memory, _bytes))
            throw std::bad_alloc{};
    }
    ~Allocation() noexcept
    {
        ReleasePublicationAllocation(_memory, _bytes);
    }
    Allocation(const Allocation&) = delete;
    Allocation& operator=(const Allocation&) = delete;

  private:
    std::shared_ptr<const SharedPublicationMemory> _memory;
    const std::uint64_t _bytes;
};

template <class T> struct ControlAllocator final
{
    using value_type = T;
    std::shared_ptr<const SharedPublicationMemory> memory;

    explicit ControlAllocator(std::shared_ptr<const SharedPublicationMemory> original) noexcept
        : memory(std::move(original))
    {
    }
    template <class U> ControlAllocator(const ControlAllocator<U>& original) noexcept : memory(original.memory) {}
    [[nodiscard]] T* allocate(std::size_t count)
    {
        if (!memory || count > 4096 / sizeof(T))
            throw std::bad_alloc{};
        auto* result = std::allocator<T>{}.allocate(count);
        if (!TryAccountPublicationAllocation(memory, count * sizeof(T) + 64))
        {
            std::allocator<T>{}.deallocate(result, count);
            throw std::bad_alloc{};
        }
        return result;
    }
    void deallocate(T* block, std::size_t count) noexcept
    {
        // Copies retain the same owner. Only real deallocation refunds this control allowance, including
        // after the last strong publication reference has gone and only a weak reader survives.
        auto original = std::move(memory);
        std::allocator<T>{}.deallocate(block, count);
        ReleasePublicationAllocation(original, count * sizeof(T) + 64);
        original.reset();
    }
    template <class U> [[nodiscard]] bool operator==(const ControlAllocator<U>& other) const noexcept
    {
        return memory == other.memory;
    }
};

#pragma warning(push)
#pragma warning(disable : 4324)
template <class T> struct Node final : DeferredReleaseNode
{
    template <class... Args>
    Node(std::shared_ptr<const SharedPublicationMemory> memory, std::uint64_t retirementBytes,
         std::uint64_t allocationBytes, Args&&... args)
        : allocation(std::move(memory), allocationBytes), value(std::forward<Args>(args)...)
    {
        bytes = retirementBytes;
    }
    Allocation allocation;
    T value;
};
#pragma warning(pop)

template <class T> struct Retire final
{
    void operator()(Node<T>* node) const noexcept
    {
        node->destroy = [](DeferredReleaseNode* retired) noexcept
        { const std::unique_ptr<Node<T>> owned(static_cast<Node<T>*>(retired)); };
        if (!TryDeferRelease(*node, DeferredReleasePolicy::AlwaysOnOwner))
            node->destroy(node);
    }
};
} // namespace AccountedPublicationDetail

template <class T> [[nodiscard]] constexpr std::uint64_t AccountedPublicationObjectAllowance() noexcept
{
    static_assert(std::is_object_v<T> && !std::is_array_v<T>);
    using Node = AccountedPublicationDetail::Node<T>;
    static_assert(sizeof(Node) <= (std::numeric_limits<std::uint64_t>::max)() - 4224);
    return sizeof(Node) + 64 + 4096 + 64;
}

template <class T, class... Args>
[[nodiscard]] std::shared_ptr<T> MakeAccountedPublicationObject(std::shared_ptr<const SharedPublicationMemory> memory,
                                                                std::uint64_t retirementBytes,
                                                                std::uint64_t newlyOwnedChildren, Args&&... args)
{
    static_assert(std::is_object_v<T> && !std::is_array_v<T>);
    static_assert(std::is_nothrow_destructible_v<T>);
    using Node = AccountedPublicationDetail::Node<T>;
    constexpr auto self = static_cast<std::uint64_t>(sizeof(Node)) + 64;
    if (!memory || newlyOwnedChildren > (std::numeric_limits<std::uint64_t>::max)() - self)
        throw std::bad_alloc{};

    AccountedPublicationDetail::ControlAllocator<Node> allocator{memory};
    auto node = std::make_unique<Node>(std::move(memory), retirementBytes, self + newlyOwnedChildren,
                                       std::forward<Args>(args)...);
    std::shared_ptr<Node> owner{node.release(), AccountedPublicationDetail::Retire<T>{}, std::move(allocator)};
    auto* value = &owner->value;
    return std::shared_ptr<T>{std::move(owner), value};
}
