#pragma once

#include <cstdint>

inline constexpr std::uint64_t kMemoryAdmissionHeadroomBytes = 1'073'741'824ULL;

enum class MemoryAdmission : std::uint8_t
{
    Ready,
    Waiting,
    Refused
};

struct MemoryAdmissionResult final
{
    MemoryAdmission admission;
    std::uint64_t availableAfterReservations;
};

// Pure policy from UiNeverBlocks_2026-09-22.md §4.6. The caller supplies a current OS snapshot and tracks
// reservations; deferred frees are evidence that a release event can wake a waiter, but are not yet usable
// capacity because the snapshot still includes their physical usage.
[[nodiscard]] constexpr MemoryAdmissionResult EvaluateMemoryAdmission(std::uint64_t requestBytes,
                                                                      std::uint64_t availablePhysicalBytes,
                                                                      std::uint64_t availableCommitBytes,
                                                                      std::uint64_t outstandingReservedBytes,
                                                                      std::uint64_t releasingBytes) noexcept
{
    const std::uint64_t availableSnapshot =
        availablePhysicalBytes < availableCommitBytes ? availablePhysicalBytes : availableCommitBytes;
    const std::uint64_t afterHeadroom =
        availableSnapshot > kMemoryAdmissionHeadroomBytes ? availableSnapshot - kMemoryAdmissionHeadroomBytes : 0;
    const std::uint64_t availableAfterReservations =
        afterHeadroom > outstandingReservedBytes ? afterHeadroom - outstandingReservedBytes : 0;

    if (requestBytes == 0)
    {
        return {MemoryAdmission::Ready, availableAfterReservations};
    }

    if (requestBytes <= availableAfterReservations)
    {
        return {MemoryAdmission::Ready, availableAfterReservations};
    }
    return {outstandingReservedBytes != 0 || releasingBytes != 0 ? MemoryAdmission::Waiting : MemoryAdmission::Refused,
            availableAfterReservations};
}
