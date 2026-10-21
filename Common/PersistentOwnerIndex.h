#pragma once

#include "Executor.h"
#include "LocallyOwnedPublication.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <type_traits>
#include <utility>

// Worker-only path copying for an immutable identity index. OwnerPolicy(value) must return the
// identity of an object strongly retained by value; the key can never outlive that owning value.
// OwnerPolicy::RetirementBytes(value) is the positive conservative bound for that complete owned value.
// Every copied node uses the supplied original publication memory. Sharing older subtrees retains
// their original accounting owners; this index retains no previous-index/parent-version chain.
// The caller admits source plus conservative new-node/value/retirement bytes before invocation.
// std::bad_alloc reaches the existing worker runner. Refusal/cancellation leaves output unchanged.
template <class Value, class OwnerPolicy> class PersistentOwnerIndex final
{
    static_assert(std::is_nothrow_destructible_v<Value>);
    static_assert(noexcept(OwnerPolicy{}(std::declval<const Value&>())));
    static_assert(noexcept(OwnerPolicy::RetirementBytes(std::declval<const Value&>())));

    using Key = std::remove_cvref_t<decltype(OwnerPolicy{}(std::declval<const Value&>()))>;
    static_assert(std::is_pointer_v<Key> || std::is_unsigned_v<Key>);

    struct Node;
    using Link = std::shared_ptr<const Node>;
    struct Node final
    {
        Key key;
        Value value;
        Link left;
        Link right;
        std::size_t count;
        unsigned int height;
        std::uint64_t retirementBytes;

        Node(Key identity, const Value& record, Link lower, Link upper, std::uint64_t retiredBytes)
            : key(identity), value(record), left(std::move(lower)), right(std::move(upper)),
              count(1 + Count(left) + Count(right)), height(1 + (std::max)(Height(left), Height(right))),
              retirementBytes(retiredBytes)
        {
        }
    };

  public:
    struct Observer final
    {
        void* context = nullptr;
        void (*visited)(void*) noexcept = nullptr;
        void (*allocating)(void*) = nullptr; // Deterministic test fault before actual allocation.
    };

    [[nodiscard]] std::size_t size() const noexcept
    {
        return Count(_root);
    }
    [[nodiscard]] unsigned int HeightForTest() const noexcept
    {
        return Height(_root);
    }
    [[nodiscard]] const void* RootIdentityForTest() const noexcept
    {
        return _root.get();
    }
    [[nodiscard]] std::uint64_t RetirementBytes() const noexcept
    {
        return _root ? _root->retirementBytes : 0;
    }
    [[nodiscard]] static constexpr std::uint64_t NodeAllocationAllowance() noexcept
    {
        return LocallyOwnedPublicationAllowance<Node>();
    }
    // An AVL update visits at most this many nodes for every representable size_t count. Rotations
    // allocate at most three nodes per level, in addition to the replacement leaf.
    static constexpr unsigned int MaximumHeight = 2 * std::numeric_limits<std::size_t>::digits;

    [[nodiscard]] const Value* Find(Key key) const noexcept
    {
        if (!key)
            return nullptr;
        const Node* node = _root.get();
        while (node)
        {
            if (key == node->key)
                return &node->value;
            node = Less(key, node->key) ? node->left.get() : node->right.get();
        }
        return nullptr;
    }

    [[nodiscard]] bool TrySet(const Value& value, std::shared_ptr<const SharedPublicationMemory> memory,
                              CancelToken cancellation, PersistentOwnerIndex& output, Observer observer = {}) const
    {
        Key key = OwnerPolicy{}(value);
        if (!key || memory.use_count() == 0 || cancellation.IsCancelled() ||
            size() == (std::numeric_limits<std::size_t>::max)())
            return false;
        Builder builder{std::move(memory), cancellation, observer};
        auto root = Set(_root, key, value, builder);
        if (builder.failed || cancellation.IsCancelled())
            return false;
        output._root = std::move(root);
        return true;
    }

    [[nodiscard]] bool TryErase(Key key, std::shared_ptr<const SharedPublicationMemory> memory,
                                CancelToken cancellation, PersistentOwnerIndex& output, Observer observer = {}) const
    {
        if (!key || memory.use_count() == 0 || cancellation.IsCancelled())
            return false;
        Builder builder{std::move(memory), cancellation, observer};
        auto root = Erase(_root, key, builder);
        if (builder.failed || cancellation.IsCancelled())
            return false;
        output._root = std::move(root);
        return true;
    }

    // Sorted read-only visitation borrows only from this retained root. No vector, callable allocation,
    // mutable node or shared-pointer copies. Returning false stops immediately at the current record.
    template <class Visitor> [[nodiscard]] bool Visit(Visitor&& visitor, CancelToken cancellation = {}) const
    {
        const Node* stack[MaximumHeight]{};
        std::size_t depth = 0;
        const Node* node = _root.get();
        while (node || depth)
        {
            if (cancellation.IsCancelled())
                return false;
            while (node)
            {
                if (depth == MaximumHeight)
                    return false;
                stack[depth++] = node;
                node = node->left.get();
            }
            node = stack[--depth];
            if (!visitor(node->key, node->value))
                return false;
            node = node->right.get();
        }
        return !cancellation.IsCancelled();
    }

  private:
    struct Builder final
    {
        std::shared_ptr<const SharedPublicationMemory> memory;
        CancelToken cancellation;
        Observer observer;
        bool failed = false;

        [[nodiscard]] bool Check() noexcept
        {
            if (observer.visited)
                observer.visited(observer.context);
            failed = failed || cancellation.IsCancelled();
            return !failed;
        }
        [[nodiscard]] Link Make(Key key, const Value& value, Link left, Link right)
        {
            constexpr auto maximum = (std::numeric_limits<std::size_t>::max)();
            if (!Check() || Count(left) == maximum || Count(right) > maximum - 1 - Count(left) ||
                (std::max)(Height(left), Height(right)) >= MaximumHeight)
            {
                failed = true;
                return {};
            }
            if (observer.allocating)
                observer.allocating(observer.context);
            const auto valueBytes = OwnerPolicy::RetirementBytes(value);
            constexpr auto maximumBytes = (std::numeric_limits<std::uint64_t>::max)();
            auto bytes = NodeAllocationAllowance();
            const auto add = [&](std::uint64_t amount) noexcept
            {
                if (amount > maximumBytes - bytes)
                    return false;
                bytes += amount;
                return true;
            };
            if (!valueBytes || !add(valueBytes) || !add(left ? left->retirementBytes : 0) ||
                !add(right ? right->retirementBytes : 0))
            {
                failed = true;
                return {};
            }
            return MakeLocallyOwnedPublicationObject<Node>(memory, bytes, 0, key, value, std::move(left),
                                                           std::move(right), bytes);
        }
    };

    [[nodiscard]] static std::size_t Count(const Link& node) noexcept
    {
        return node ? node->count : 0;
    }
    [[nodiscard]] static unsigned int Height(const Link& node) noexcept
    {
        return node ? node->height : 0;
    }
    [[nodiscard]] static bool Less(Key left, Key right) noexcept
    {
        return std::less<Key>{}(left, right);
    }
    [[nodiscard]] static Link Balance(Key key, const Value& value, Link left, Link right, Builder& builder)
    {
        if (builder.failed)
            return {};
        if (Height(left) > Height(right) + 1)
        {
            const auto pivot = left;
            if (Height(pivot->left) >= Height(pivot->right))
            {
                auto upper = builder.Make(key, value, pivot->right, std::move(right));
                return builder.Make(pivot->key, pivot->value, pivot->left, std::move(upper));
            }
            const auto middle = pivot->right;
            auto lower = builder.Make(pivot->key, pivot->value, pivot->left, middle->left);
            auto upper = builder.Make(key, value, middle->right, std::move(right));
            return builder.Make(middle->key, middle->value, std::move(lower), std::move(upper));
        }
        if (Height(right) > Height(left) + 1)
        {
            const auto pivot = right;
            if (Height(pivot->right) >= Height(pivot->left))
            {
                auto lower = builder.Make(key, value, std::move(left), pivot->left);
                return builder.Make(pivot->key, pivot->value, std::move(lower), pivot->right);
            }
            const auto middle = pivot->left;
            auto lower = builder.Make(key, value, std::move(left), middle->left);
            auto upper = builder.Make(pivot->key, pivot->value, middle->right, pivot->right);
            return builder.Make(middle->key, middle->value, std::move(lower), std::move(upper));
        }
        return builder.Make(key, value, std::move(left), std::move(right));
    }
    [[nodiscard]] static Link Set(const Link& node, Key key, const Value& value, Builder& builder)
    {
        if (!builder.Check())
            return {};
        if (!node)
            return builder.Make(key, value, {}, {});
        if (key == node->key)
            return builder.Make(key, value, node->left, node->right);
        if (Less(key, node->key))
        {
            auto left = Set(node->left, key, value, builder);
            return Balance(node->key, node->value, std::move(left), node->right, builder);
        }
        auto right = Set(node->right, key, value, builder);
        return Balance(node->key, node->value, node->left, std::move(right), builder);
    }
    [[nodiscard]] static Link Erase(const Link& node, Key key, Builder& builder)
    {
        if (!builder.Check() || !node)
            return node;
        if (key == node->key)
        {
            if (!node->left)
                return node->right;
            if (!node->right)
                return node->left;
            const Node* successor = node->right.get();
            while (successor->left)
            {
                if (!builder.Check())
                    return {};
                successor = successor->left.get();
            }
            auto right = Erase(node->right, successor->key, builder);
            return Balance(successor->key, successor->value, node->left, std::move(right), builder);
        }
        if (Less(key, node->key))
        {
            auto left = Erase(node->left, key, builder);
            if (left == node->left)
                return node;
            return Balance(node->key, node->value, std::move(left), node->right, builder);
        }
        auto right = Erase(node->right, key, builder);
        if (right == node->right)
            return node;
        return Balance(node->key, node->value, node->left, std::move(right), builder);
    }

    Link _root;
};
