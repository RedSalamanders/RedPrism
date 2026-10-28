#pragma once

#include "DeferredRelease.h"
#include "PublicationGraphAllocator.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <type_traits>
#include <utility>

namespace PublicationObjectDetail
{
#pragma warning(push)
#pragma warning(disable : 4324)
// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027)
template <class T> struct Node final : DeferredReleaseNode
{
    template <class... Args>
    Node(std::shared_ptr<const SharedPublicationMemory> owner, std::uint64_t retirementBytes, Args&&... args)
        : memory(std::move(owner)), value(std::forward<Args>(args)...)
    {
        bytes = retirementBytes;
    }

    std::shared_ptr<const SharedPublicationMemory> memory;
    T value;
};
#pragma warning(pop)

#pragma warning(pop)

template <class T> struct DeferOrDestroy final
{
    void operator()(Node<T>* node) const noexcept
    {
        node->destroy = [](DeferredReleaseNode* retired) noexcept
        { const std::unique_ptr<Node<T>> owned(static_cast<Node<T>*>(retired)); };
        if (!TryDeferRelease(*node, DeferredReleasePolicy::AlwaysOnOwner))
        {
            node->destroy(node);
        }
    }
};
} // namespace PublicationObjectDetail

template <class T> [[nodiscard]] constexpr std::uint64_t PublicationObjectAllowance() noexcept
{
    static_assert(std::is_object_v<T> && !std::is_array_v<T>);
    using Node = PublicationObjectDetail::Node<T>;
    static_assert(sizeof(Node) <= (std::numeric_limits<std::uint64_t>::max)() - 4096);
    return static_cast<std::uint64_t>(sizeof(Node)) + 4096;
}

template <class T, class... Args>
[[nodiscard]] std::shared_ptr<T> MakePublicationObject(std::shared_ptr<const SharedPublicationMemory> memory,
                                                       std::uint64_t retirementBytes, Args&&... args)
{
    static_assert(std::is_object_v<T> && !std::is_array_v<T>);
    static_assert(std::is_nothrow_destructible_v<T>, "Published values must have non-throwing destructors.");

    if (memory.use_count() == 0)
    {
        return std::make_shared<T>(std::forward<Args>(args)...);
    }

    using Node = PublicationObjectDetail::Node<T>;
    PublicationGraphAllocator<Node> allocator{memory};
    auto node = std::make_unique<Node>(std::move(memory), retirementBytes, std::forward<Args>(args)...);
    std::shared_ptr<Node> owner(node.release(), PublicationObjectDetail::DeferOrDestroy<T>{}, std::move(allocator));
    auto* value = &owner->value;
    return std::shared_ptr<T>{std::move(owner), value};
}
