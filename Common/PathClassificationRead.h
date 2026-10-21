#pragma once

#include "PathClassificationWork.h"

#include <cstdint>
#include <memory>
#include <string_view>

enum class PathClassificationReadNotice : std::uint8_t
{
    Pending,
    Incomplete,
    Acknowledged,
    CancellationRequested,
    Immediate,
    IssueFailed,
    Unissued,
    Destroyed,
    Count,
};

// Test-only file-open seam. Absent in production. The callback transfers an owned real overlapped-read
// handle on the worker; ReadFile, cancellation and completion acknowledgement remain the Windows APIs.
struct PathClassificationReadSource final
{
    void* context = nullptr;
    void* (*openRead)(void*, const wchar_t*) noexcept = nullptr;
    // Worker-only observations, strictly after the actual named API boundary. Never emulates readiness or
    // acknowledgement. The borrowed context survives the original job and final worker shutdown.
    void (*observe)(void*, PathClassificationReadNotice) noexcept = nullptr;
};
enum class PathClassificationReadStep : std::uint8_t
{
    Complete,
    ReadyToIssue,
    Pending,
    Cancelled,
    InvalidInput,
};

// Worker-only native read state. The original job retains this object, file, 4-byte buffer and OVERLAPPED
// through each owner/event continuation. The stable startup wait owns event until final worker cleanup.
class PathClassificationRead final
{
  public:
    PathClassificationRead() noexcept;
    ~PathClassificationRead();
    PathClassificationRead(const PathClassificationRead&) = delete;
    PathClassificationRead& operator=(const PathClassificationRead&) = delete;
    // Worker allocation; bad_alloc propagates to the original scheduler runner's existing translation.
    void Initialize(void* event, PathClassificationReadSource source = {});
    [[nodiscard]] PathClassificationReadStep Prepare(std::wstring_view path, const CancelToken&) noexcept;
    // Each issued cycle publishes its event, including immediate completion/API failure/no-issue cancellation.
    [[nodiscard]] PathClassificationReadStep Issue(const CancelToken&) noexcept;
    // Never waits. A premature event resets/rechecks the same operation before rearm, avoiding a lost signal.
    [[nodiscard]] PathClassificationReadStep Acknowledge(const CancelToken&, bool retireRead = false) noexcept;
    // Skipped/cancelled issuing stage: close without issuing and acknowledge its registered no-IO cycle.
    void RetireUnissuedCycleWorker() noexcept;
    [[nodiscard]] PathClassification Classification() const noexcept;
    [[nodiscard]] bool ReadyToIssue() const noexcept;
    [[nodiscard]] bool HasPendingRead() const noexcept;
    // Destruction-only final worker exception: cancellation plus actual Windows acknowledgement before free.
    // No bound is claimed for a driver which does not complete an in-flight native API call.
    void CancelAndJoinForShutdownWorker() noexcept;

  private:
    struct Impl;
    std::unique_ptr<Impl> _impl;
};
