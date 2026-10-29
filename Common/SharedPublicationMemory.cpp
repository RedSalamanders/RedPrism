#include "SharedPublicationMemory.h"

#include "PublicationAllocationLedger.h"

#include <cstddef>
#include <exception>
#include <limits>
#include <new>
#include <utility>
#include <windows.h>

// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4625 4626)
struct PublicationMemoryNode final
{
    SharedRetainedMemoryLease lease;
    JobTicket original;
    bool allocated = false; // allocate_shared performs the one bounded control-block allocation.
};
#pragma warning(pop)

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

std::uint64_t SharedPublicationMemory::LocalAllocationBytes() const noexcept
{
    return _localAllocationBytes.load(std::memory_order_acquire);
}

bool SharedPublicationMemory::SealLocalAllocations() const noexcept
{
    return !_localAllocationSealed.exchange(true, std::memory_order_acq_rel);
}

bool SharedPublicationMemory::TryAccountLocalAllocation(std::uint64_t bytes) const noexcept
{
    if (!bytes || _localAllocationSealed.load(std::memory_order_acquire))
        return false;
    auto previous = _localAllocationBytes.load(std::memory_order_relaxed);
    do
    {
        if (previous > _localAllocationLimit || bytes > _localAllocationLimit - previous)
            return false;
    } while (!_localAllocationBytes.compare_exchange_weak(previous, previous + bytes, std::memory_order_release,
                                                          std::memory_order_relaxed));
    // A racing seal cannot authorize a new published allocation. Original binding still requires all useful
    // producers to have joined; this rollback makes accidental late construction refuse and unwind.
    if (_localAllocationSealed.load(std::memory_order_acquire))
    {
        ReleaseLocalAllocation(bytes);
        return false;
    }
    return true;
}

void SharedPublicationMemory::ReleaseLocalAllocation(std::uint64_t bytes) const noexcept
{
    auto previous = _localAllocationBytes.load(std::memory_order_relaxed);
    do
    {
        if (!bytes || bytes > previous)
            std::terminate(); // An accounting mismatch cannot refund live ownership.
    } while (!_localAllocationBytes.compare_exchange_weak(previous, previous - bytes, std::memory_order_release,
                                                          std::memory_order_relaxed));
}

std::uint64_t SharedPublicationMemory::OwnershipAllowance() noexcept
{
    return kControlBlockLimit + sizeof(PublicationMemoryNode);
}

std::shared_ptr<SharedPublicationMemory> SharedPublicationMemory::CreateForWork(std::uint64_t localLimit)
{
    auto node = std::make_unique<PublicationMemoryNode>();
    auto result = std::allocate_shared<SharedPublicationMemory>(
        PublicationAllocator<SharedPublicationMemory>{node.get()}, ConstructionTag{}, node.get(), localLimit);
    (void)node.release(); // One allocator now owns it. Construction after allocation is nonthrowing.
    return result;
}

std::int32_t SharedPublicationMemory::BindForPublication(JobScheduler& scheduler, JobTicket original) noexcept
{
    const auto result = scheduler.RetainPublicationMemory(original, _node->lease);
    if (result >= 0)
    {
        _node->original = original;
        (void)SealLocalAllocations();
    }
    return result;
}

std::int32_t SharedPublicationMemory::BindContinuingOutputForPublication(JobScheduler& scheduler, JobTicket original,
                                                                         std::uint64_t outputBytes) noexcept
{
    const auto local = LocalAllocationBytes();
    const auto allowance = OwnershipAllowance();
    if (local > (std::numeric_limits<std::uint64_t>::max)() - allowance || outputBytes < local + allowance)
        return E_INVALIDARG;
    const auto result = scheduler.RetainContinuingOutputMemory(original, outputBytes, _node->lease);
    if (result >= 0)
    {
        _node->original = original;
        (void)SealLocalAllocations();
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
    {
        _node->original = original;
        (void)SealLocalAllocations();
    }
    return result;
}
std::uint64_t SharedPublicationMemory::Bytes() const noexcept
{
    return _node->lease.Bytes();
}

std::int32_t SharedPublicationMemory::BindForRetirement(JobScheduler& scheduler, JobTicket original) noexcept
{
    const auto result = scheduler.RetainRetirementMemory(original, _node->lease);
    if (result >= 0)
    {
        _node->original = original;
        (void)SealLocalAllocations();
    }
    return result;
}
std::uint64_t SharedPublicationMemory::ResidentBytes() const noexcept
{
    return _node->lease.ResidentBytes();
}

bool SharedPublicationMemory::BoundTo(const JobScheduler& scheduler, JobTicket original) const noexcept
{
    return IsBound() && _node->original == original && scheduler.OwnsRetainedMemoryEndpoint(_node->lease);
}

std::int32_t SharedPublicationMemory::BindPartitionedAfterCompletedWork(JobScheduler& scheduler, JobTicket original,
                                                                        std::uint64_t outputBytes,
                                                                        SharedPublicationMemory& temporary) noexcept
{
    const auto result = scheduler.RetainPartitionedMemory(original, outputBytes, _node->lease, temporary._node->lease);
    if (result >= 0)
    {
        _node->original = original;
        temporary._node->original = original;
        (void)SealLocalAllocations();
        (void)temporary.SealLocalAllocations();
    }
    return result;
}

std::int32_t SharedPublicationMemory::BindPartitionedForPublication(JobScheduler& scheduler, JobTicket original,
                                                                    std::uint64_t outputBytes,
                                                                    SharedPublicationMemory& temporary) noexcept
{
    const auto result =
        scheduler.RetainPartitionedPublicationMemory(original, outputBytes, _node->lease, temporary._node->lease);
    if (result >= 0)
    {
        _node->original = original;
        temporary._node->original = original;
        (void)SealLocalAllocations();
        (void)temporary.SealLocalAllocations();
    }
    return result;
}

std::int32_t SharedPublicationMemory::BindPartitionedForRetirement(JobScheduler& scheduler, JobTicket original,
                                                                   std::uint64_t outputBytes,
                                                                   SharedPublicationMemory& temporary) noexcept
{
    const auto result =
        scheduler.RetainPartitionedRetirementMemory(original, outputBytes, _node->lease, temporary._node->lease);
    if (result >= 0)
    {
        _node->original = original;
        temporary._node->original = original;
        (void)SealLocalAllocations();
        (void)temporary.SealLocalAllocations();
    }
    return result;
}

std::int32_t SharedPublicationMemory::BindPersistentOutputAfterCompletedWork(
    JobScheduler& scheduler, JobTicket original, std::uint64_t outputBytes, SharedPublicationMemory& temporary) noexcept
{
    const auto result =
        scheduler.RetainPersistentOutputMemory(original, outputBytes, _node->lease, temporary._node->lease);
    if (result >= 0)
    {
        _node->original = original;
        temporary._node->original = original;
        (void)SealLocalAllocations();
        (void)temporary.SealLocalAllocations();
    }
    return result;
}

std::int32_t SharedPublicationMemory::BindPersistentOutputForPublication(JobScheduler& scheduler, JobTicket original,
                                                                         std::uint64_t outputBytes,
                                                                         SharedPublicationMemory& temporary) noexcept
{
    const auto result =
        scheduler.RetainPersistentOutputPublicationMemory(original, outputBytes, _node->lease, temporary._node->lease);
    if (result >= 0)
    {
        _node->original = original;
        temporary._node->original = original;
        (void)SealLocalAllocations();
        (void)temporary.SealLocalAllocations();
    }
    return result;
}

std::int32_t SharedPublicationMemory::BindPersistentOutputForRetirement(JobScheduler& scheduler, JobTicket original,
                                                                        std::uint64_t outputBytes,
                                                                        SharedPublicationMemory& temporary) noexcept
{
    const auto result =
        scheduler.RetainPersistentOutputRetirementMemory(original, outputBytes, _node->lease, temporary._node->lease);
    if (result >= 0)
    {
        _node->original = original;
        temporary._node->original = original;
        (void)SealLocalAllocations();
        (void)temporary.SealLocalAllocations();
    }
    return result;
}

bool TryAccountPublicationAllocation(const std::shared_ptr<const SharedPublicationMemory>& memory,
                                     std::uint64_t bytes) noexcept
{
    return memory && memory->TryAccountLocalAllocation(bytes);
}

void ReleasePublicationAllocation(const std::shared_ptr<const SharedPublicationMemory>& memory,
                                  std::uint64_t bytes) noexcept
{
    if (!memory)
        std::terminate();
    memory->ReleaseLocalAllocation(bytes);
}
