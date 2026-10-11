#pragma once

#include "WorkerWakeCounts.h"

#include <atomic>
#include <cstdint>
#include <optional>
#include <windows.h>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/resource.h>
#pragma warning(pop)

namespace WorkerResources
{
// Payload-free owner endpoint. The borrowed pool and callback context outlive Close; Close precedes parent
// dispatch cleanup. One owner serializes Initialize, Request and Close. Actual work is published before Request.
class WorkerWake final
{
  public:
    using Dispatch = void (*)(void*, WakeLane, std::uint32_t) noexcept;
    enum class Phase : std::uint32_t
    {
        Entered,
        CountsTaken,
        BeforeRearm,
        RearmLocked
    };
    struct Observer final
    {
        void* context = nullptr;
        void (*checkpoint)(void*, Phase) noexcept = nullptr; // deterministic test gates only
    };
    WorkerWake() = default;
    ~WorkerWake();
    WorkerWake(const WorkerWake&) = delete;
    WorkerWake& operator=(const WorkerWake&) = delete;
    [[nodiscard]] HRESULT Initialize(PTP_POOL pool, std::uint32_t capacity, Dispatch dispatch, void* context,
                                     Observer observer = {}) noexcept;
    [[nodiscard]] HRESULT Request(WakeLane channel) noexcept;
    void Close() noexcept;

  private:
    using UniqueWait = wil::unique_any<PTP_WAIT, decltype(&CloseThreadpoolWait), CloseThreadpoolWait>;
    static void CALLBACK Callback(PTP_CALLBACK_INSTANCE, void*, PTP_WAIT, TP_WAIT_RESULT) noexcept;
    void Observe(Phase phase) noexcept;
    DWORD _ownerThread = 0;
    bool _open = false; // owner only; never read by the callback
    std::atomic<bool> _stopping{false};
    SRWLOCK _rearmLock = SRWLOCK_INIT; // callback/Close only; Request never acquires this lock
    std::optional<OwnerWakeCounts> _counts;
    Dispatch _dispatch = nullptr;
    void* _context = nullptr;
    Observer _observer{};
    wil::unique_handle _event;
    UniqueWait _wait;
};
} // namespace WorkerResources
