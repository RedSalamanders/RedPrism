#pragma once

#include "Executor.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

// Allocation identities never wrap. Zero is invalid/exhausted. Assign at raster construction, before
// publishing any reader; failed allocations may consume a serial. No pixel/owner pointer is retained here.
class RasterStorageSerial final
{
  public:
    explicit RasterStorageSerial(std::uint64_t next = 1) noexcept : _next(next) {}
    RasterStorageSerial(const RasterStorageSerial&) = delete;
    RasterStorageSerial& operator=(const RasterStorageSerial&) = delete;
    [[nodiscard]] std::uint64_t Allocate() noexcept
    {
        auto observed = _next.load(std::memory_order_relaxed);
        while (observed)
        {
            const auto next = observed == (std::numeric_limits<std::uint64_t>::max)() ? 0 : observed + 1;
            if (_next.compare_exchange_weak(observed, next, std::memory_order_relaxed))
                return observed;
        }
        return 0;
    }

  private:
    static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
    std::atomic<std::uint64_t> _next;
};

struct RasterResidencyChange final
{
    std::uint64_t storageSerial = 0;
    std::uint64_t epoch = 0;
    std::uint64_t residentSelfBytes = 0;
    [[nodiscard]] bool operator==(const RasterResidencyChange&) const noexcept = default;
};

// No synchronization is owned here. Actual raster integration must hold the existing same-thread
// exclusive residency lease for every Append, Epoch and CopySince, with no borrowed view escaping it.
// The fixed journal owns no raster, storage address, weak publication control or accounting owner.
// Raster materialization and prepared history use this journal under their existing residency lease.
template <std::size_t Capacity> class RasterResidencyJournal final
{
    static_assert(Capacity > 0 && Capacity <= 4096);

  public:
    enum class CopyResult : std::uint8_t
    {
        Complete,
        InvalidWitness,
        Overwritten,
        BufferTooSmall,
        Cancelled,
    };
    struct CopyObserver final
    {
        void* context = nullptr;
        void (*copied)(void*, std::size_t) noexcept = nullptr;
    };
    explicit RasterResidencyJournal(std::uint64_t epoch = 1) noexcept : _epoch(epoch) {}
    RasterResidencyJournal(const RasterResidencyJournal&) = delete;
    RasterResidencyJournal& operator=(const RasterResidencyJournal&) = delete;
    [[nodiscard]] std::uint64_t Epoch() const noexcept
    {
        return _epoch;
    }
    [[nodiscard]] bool Append(std::uint64_t storageSerial, std::uint64_t residentSelfBytes) noexcept
    {
        if (!_epoch || _epoch == (std::numeric_limits<std::uint64_t>::max)() || !storageSerial || !residentSelfBytes)
            return false;
        const auto next = _epoch + 1;
        _records[static_cast<std::size_t>(next % Capacity)] = {storageSerial, next, residentSelfBytes};
        _epoch = next;
        return true;
    }
    // Output is worker scratch: refused/cancelled copies may write a prefix, but publishedCount stays zero.
    // Only Complete exposes the entire exact contiguous suffix through publishedCount. Empty suffix costs
    // no allocation or scan. Copying checks cancellation at every bounded record.
    [[nodiscard]] CopyResult CopySince(std::uint64_t afterEpoch, std::span<RasterResidencyChange> output,
                                       std::size_t& publishedCount, CancelToken cancellation = {},
                                       CopyObserver observer = {}) const noexcept
    {
        publishedCount = 0;
        if (cancellation.IsCancelled())
            return CopyResult::Cancelled;
        if (!_epoch || !afterEpoch || afterEpoch > _epoch)
            return CopyResult::InvalidWitness;
        const auto needed = _epoch - afterEpoch;
        if (needed > Capacity)
            return CopyResult::Overwritten;
        if (needed > output.size())
            return CopyResult::BufferTooSmall;
        for (std::size_t index = 0; index < static_cast<std::size_t>(needed); ++index)
        {
            if (cancellation.IsCancelled())
                return CopyResult::Cancelled;
            const auto epoch = afterEpoch + index + 1;
            const auto& record = _records[static_cast<std::size_t>(epoch % Capacity)];
            if (record.epoch != epoch || !record.storageSerial || !record.residentSelfBytes)
                return CopyResult::Overwritten;
            output[index] = record;
            if (observer.copied)
                observer.copied(observer.context, index + 1);
        }
        if (cancellation.IsCancelled())
            return CopyResult::Cancelled;
        publishedCount = static_cast<std::size_t>(needed);
        return CopyResult::Complete;
    }

  private:
    std::array<RasterResidencyChange, Capacity> _records{};
    std::uint64_t _epoch;
};
