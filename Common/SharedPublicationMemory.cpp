#include "SharedPublicationMemory.h"

#include <cstddef>
#include <new>
#include <utility>

struct PublicationMemoryNode final
{
    SharedRetainedMemoryLease lease;
    JobTicket original;
    bool allocated = false; // allocate_shared performs the one bounded control-block allocation.
};

namespace
{
constexpr std::size_t kControlBlockLimit = 4096;
template <class T> struct PublicationAllocator final
{
    using value_type = T;
    PublicationMemoryNode* node;
    explicit PublicationAllocator(PublicationMemoryNode* value) noexcept : node(value) {}
    template <class U> PublicationAllocator(const PublicationAllocator<U>& other) noexcept : node(other.node) {}
    [[nodiscard]] T* allocate(std::size_t count)
    {
        if (node->allocated || count > kControlBlockLimit / sizeof(T))
            throw std::bad_alloc{};
        T* storage = std::allocator<T>{}.allocate(count);
        node->allocated = true;
        return storage;
    }
    void deallocate(T* value, std::size_t count) noexcept
    {
        // The STL copies this allocator before destroying its private control block. Keep the unique lease
        // on this stack until both original allocations are gone, including a block held by weak readers.
        auto lease = std::move(node->lease);
        std::allocator<T>{}.deallocate(value, count);
        delete node;
        lease.Reset();
    }
    template <class U> [[nodiscard]] bool operator==(const PublicationAllocator<U>& other) const noexcept
    {
        return node == other.node;
    }
};
} // namespace

std::uint64_t SharedPublicationMemory::OwnershipAllowance() noexcept
{
    return kControlBlockLimit + sizeof(PublicationMemoryNode);
}

std::shared_ptr<SharedPublicationMemory> SharedPublicationMemory::CreateForWork()
{
    auto node = std::make_unique<PublicationMemoryNode>();
    auto result = std::allocate_shared<SharedPublicationMemory>(
        PublicationAllocator<SharedPublicationMemory>{node.get()}, ConstructionTag{}, node.get());
    (void)node.release(); // One allocator now owns it. Construction after allocation is nonthrowing.
    return result;
}

std::int32_t SharedPublicationMemory::BindForPublication(JobScheduler& scheduler, JobTicket original) noexcept
{
    const auto result = scheduler.RetainPublicationMemory(original, _node->lease);
    if (result >= 0)
    {
        _node->original = original;
    }
    return result;
}

bool SharedPublicationMemory::IsBound() const noexcept
{
    return static_cast<bool>(_node->lease);
}

std::int32_t SharedPublicationMemory::BindAfterCompletedWork(JobScheduler& scheduler, JobTicket original) noexcept
{
    const auto result = scheduler.RetainMemory(original, _node->lease);
    if (result >= 0)
        _node->original = original;
    return result;
}
std::uint64_t SharedPublicationMemory::Bytes() const noexcept
{
    return _node->lease.Bytes();
}
std::uint64_t SharedPublicationMemory::ResidentBytes() const noexcept
{
    return _node->lease.ResidentBytes();
}

bool SharedPublicationMemory::BoundTo(const JobScheduler& scheduler, JobTicket original) const noexcept
{
    return IsBound() && _node->original == original && scheduler.OwnsRetainedMemoryEndpoint(_node->lease);
}
