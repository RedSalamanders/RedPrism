#pragma once

#include "Jobs.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>

// Opt-in harness-only write-once storage. Allocate outside measured work and keep it alive through joined Close.
// Up to JobScheduler::kCapacity scheduler bodies may publish concurrently. No storage reset/reuse is permitted.
// The callback supplies its clock and signals any borrowed event only after Publish returns. Readers reset that
// event before draining; an unpublished earlier cell cannot be skipped just because a later writer has returned.
// Captured body notices are diagnostics, never owner/GPU acknowledgement or permission to release input.
template <std::size_t Capacity> class UiCancellationCapture final
{
  public:
    static_assert(Capacity > 0);
    static_assert(Capacity < (std::numeric_limits<std::uint64_t>::max)() - JobScheduler::kCapacity);
    static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
    static_assert(std::atomic<bool>::is_always_lock_free);

    struct Sample final
    {
        JobExecutionNotice notice{};
        std::int64_t ticks = -1; // Invalid caller clock is retained and explicitly refused by interval selection.
    };
    enum class ReadResult
    {
        Published,
        Pending,
        Exhausted,
        Failed
    };

    UiCancellationCapture() noexcept = default;
    UiCancellationCapture(const UiCancellationCapture&) = delete;
    UiCancellationCapture& operator=(const UiCancellationCapture&) = delete;

    [[nodiscard]] bool Publish(JobExecutionNotice notice, std::int64_t ticks) noexcept
    {
        if (_overflow.load(std::memory_order_acquire))
            return false;
        const auto index = _reserved.fetch_add(1, std::memory_order_relaxed);
        if (index >= Capacity)
        {
            _overflow.store(true, std::memory_order_release);
            return false;
        }
        auto& cell = _cells[static_cast<std::size_t>(index)];
        cell.sample = {notice, ticks};
        cell.ready.store(true, std::memory_order_release);
        return true;
    }

    // An owner cursor advances only after Published. Failed means capacity overflow, so no partial result
    // from this log may qualify a trial. Exhausted is a snapshot of current reservation, not producer join.
    [[nodiscard]] ReadResult Read(std::size_t index, Sample& output) const noexcept
    {
        if (_overflow.load(std::memory_order_acquire))
            return ReadResult::Failed;
        if (index >= Capacity || index >= _reserved.load(std::memory_order_acquire))
            return ReadResult::Exhausted;
        const auto& cell = _cells[index];
        if (!cell.ready.load(std::memory_order_acquire))
            return ReadResult::Pending;
        output = cell.sample;
        return ReadResult::Published;
    }

    [[nodiscard]] bool Overflowed() const noexcept
    {
        return _overflow.load(std::memory_order_acquire);
    }

  private:
    struct Cell final
    {
        Sample sample;
        std::atomic<bool> ready{false};
    };
    std::array<Cell, Capacity> _cells{};
    std::atomic<std::uint64_t> _reserved{0};
    std::atomic<bool> _overflow{false};
};
