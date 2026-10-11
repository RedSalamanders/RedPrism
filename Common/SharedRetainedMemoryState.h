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
struct SharedRetainedMemoryLease::State final
{
    std::array<std::atomic<std::uint64_t>, 2 * JobScheduler::kRetainedMemoryCapacity> acknowledged{};
    std::array<std::atomic<std::uint64_t>, 2> pending{};
    [[nodiscard]] bool HasPending() const noexcept
    {
        for (const auto& word : pending)
            if (word.load(std::memory_order_acquire) != 0)
                return true;
        return false;
    }
    std::atomic<std::uint64_t> residentBytes{0};
    wil::unique_handle wake; // Duplicate of the kernel event, never a borrowed scheduler handle.
};
