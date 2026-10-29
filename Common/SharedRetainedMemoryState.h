#pragma once

#include "Jobs.h"

#include <array>
#include <atomic>

#include <windows.h>
#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/resource.h>
#pragma warning(pop)

// Private endpoint shared by scheduler admission and standalone graph lifetime acknowledgements.
// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027)
struct SharedRetainedMemoryLease::State final
{
    std::array<std::atomic<std::uint64_t>, 2 * JobScheduler::kRetainedMemoryCapacity> acknowledged{};
    std::array<std::atomic<std::uint64_t>, 2> pending{};
    std::atomic<std::uint64_t> persistentReleasedBytes{0};
    [[nodiscard]] bool HasPending() const noexcept
    {
        if (persistentReleasedBytes.load(std::memory_order_acquire) != 0)
            return true;
        for (const auto& word : pending)
            if (word.load(std::memory_order_acquire) != 0)
                return true;
        return false;
    }
    std::atomic<std::uint64_t> residentBytes{0};
    wil::unique_handle wake; // Duplicate of the kernel event, never a borrowed scheduler handle.
};
#pragma warning(pop)
