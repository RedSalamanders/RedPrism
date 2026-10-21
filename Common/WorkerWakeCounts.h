#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <limits>

namespace WorkerResources
{

enum class WakeLane : std::uint32_t
{
    High = 0,
    Normal = 1,
    Background = 2,
};

class OwnerWakeCounts final
{
  public:
    static constexpr std::uint32_t kChannelCount = 3;
    static constexpr std::uint32_t kMaximumCapacity = 1024;

    static_assert(std::atomic<std::uint32_t>::is_always_lock_free);
    static_assert(std::numeric_limits<std::uint32_t>::max() >= kMaximumCapacity);

    explicit OwnerWakeCounts(std::uint32_t capacity) noexcept : capacity_(capacity) {}

    [[nodiscard]] bool IsValid() const noexcept
    {
        return capacity_ >= 1 && capacity_ <= kMaximumCapacity;
    }

    [[nodiscard]] std::uint32_t Capacity() const noexcept
    {
        return capacity_;
    }

    // One owner-thread producer calls Request. Consumers may only call Take.
    // A true result includes a request coalesced at capacity; callers must
    // still perform any external event signaling for every true result.
    [[nodiscard]] bool Request(WakeLane channel) noexcept
    {
        const std::uint32_t index = static_cast<std::uint32_t>(channel);
        if (!IsValid() || index >= kChannelCount)
        {
            return false;
        }

        std::atomic<std::uint32_t>& count = counts_[index];
        std::uint32_t expected = count.load(std::memory_order_relaxed);
        const std::uint32_t desired = expected < capacity_ ? expected + 1 : capacity_;

        if (count.compare_exchange_strong(expected, desired, std::memory_order_relaxed, std::memory_order_relaxed))
        {
            return true;
        }

        // Under the single-producer/reset-only-consumer contract, the only
        // allowed intervening write is exchange(nonzero, 0). Strong CAS then
        // updates expected to zero, so this one retry restores a pending count.
        if (expected != 0)
        {
            return false;
        }
        return count.compare_exchange_strong(expected, 1, std::memory_order_relaxed, std::memory_order_relaxed);
    }

    [[nodiscard]] std::uint32_t Take(WakeLane channel) noexcept
    {
        const std::uint32_t index = static_cast<std::uint32_t>(channel);
        if (!IsValid() || index >= kChannelCount)
        {
            return 0;
        }
        return counts_[index].exchange(0, std::memory_order_relaxed);
    }

  private:
    const std::uint32_t capacity_;
    std::array<std::atomic<std::uint32_t>, kChannelCount> counts_{};
};

} // namespace WorkerResources
