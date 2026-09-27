#pragma once

#include "MemoryAdmission.h"

struct MemorySnapshot final
{
    std::uint64_t availablePhysicalBytes = 0;
    std::uint64_t availableCommitBytes = 0;
};

// Optional snapshot source for deterministic admission tests. Production samples GlobalMemoryStatusEx on the
// owner thread at each admission attempt. Failure is an operation failure, never fabricated available memory.
struct MemorySource final
{
    bool (*sample)(void*, MemorySnapshot&) noexcept = nullptr;
    void* context = nullptr;
};

// UI-owned accounting for declared whole-graph peaks. It never takes partial reservations, waits, queries the OS,
// or allocates. Workers borrow their already-admitted peak; child stages do not reserve a second time.
class MemoryBudget final
{
  public:
    MemoryBudget() noexcept = default;
    MemoryBudget(const MemoryBudget&) = delete;
    MemoryBudget& operator=(const MemoryBudget&) = delete;

    [[nodiscard]] MemoryAdmissionResult Reserve(std::uint64_t bytes, MemorySnapshot snapshot,
                                                std::uint64_t releasingBytes = 0) noexcept
    {
        const auto decision = EvaluateMemoryAdmission(bytes, snapshot.availablePhysicalBytes,
                                                      snapshot.availableCommitBytes, _reserved, releasingBytes);
        if (decision.admission == MemoryAdmission::Ready)
        {
            // The saturating availability test proves this sum is at most the sampled ceiling minus headroom.
            _reserved += bytes;
        }
        return decision;
    }

    [[nodiscard]] bool Release(std::uint64_t bytes) noexcept
    {
        if (bytes > _reserved)
        {
            return false;
        }
        _reserved -= bytes;
        return true;
    }

    [[nodiscard]] std::uint64_t Reserved() const noexcept
    {
        return _reserved;
    }

  private:
    std::uint64_t _reserved = 0;
};
