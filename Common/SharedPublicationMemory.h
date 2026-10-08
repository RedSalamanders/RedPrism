#pragma once

#include "Jobs.h"

#include <cstdint>
#include <memory>

struct PublicationMemoryNode;

// Worker-preallocated accounting owner shared by one immutable publication and all its retiring CPU wrappers.
// It owns no pixels, UI or device. Bind once on the original owner after useful producers joined, before any
// public reader. After binding its accounting is immutable until the last containing graph has been destroyed.
// No caller can reset/grow the private lease while a row/display/history reader still retains this owner.
class SharedPublicationMemory final
{
    struct ConstructionTag final
    {
    };

  public:
    explicit SharedPublicationMemory(ConstructionTag, PublicationMemoryNode* node) noexcept : _node(node) {}
    SharedPublicationMemory(const SharedPublicationMemory&) = delete;
    SharedPublicationMemory& operator=(const SharedPublicationMemory&) = delete;
    SharedPublicationMemory(SharedPublicationMemory&&) = delete;
    SharedPublicationMemory& operator=(SharedPublicationMemory&&) = delete;
    [[nodiscard]] static std::uint64_t OwnershipAllowance() noexcept;
    // Worker only, under the original complete admission. Allocation failure reaches the Job runner.
    [[nodiscard]] static std::shared_ptr<SharedPublicationMemory> CreateForWork();
    [[nodiscard]] std::int32_t BindForPublication(JobScheduler& scheduler, JobTicket original) noexcept;
    [[nodiscard]] bool IsBound() const noexcept;
    // Owner-only provenance against the live scheduler endpoint and original ticket. Closed/reconstructed
    // schedulers refuse; this grants no callback, document or mutation authority.
    [[nodiscard]] bool BoundTo(const JobScheduler& scheduler, JobTicket original) const noexcept;
    [[nodiscard]] std::uint64_t Bytes() const noexcept;
    [[nodiscard]] std::uint64_t ResidentBytes() const noexcept;

  private:
    // The allocator releases this accounting node after the shared control block itself is freed.
    // A surviving weak reference still owns charged control-block memory after all graph data dies.
    PublicationMemoryNode* _node;
};
