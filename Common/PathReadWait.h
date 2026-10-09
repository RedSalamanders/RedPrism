#pragma once

#include "Jobs.h"
#include "OwnerWorkMultiplexer.h"

#include <cstdint>
#include <memory>

enum class PathReadWaitFault : std::uint8_t
{
    EventCreation,
    WaitRegistration,
    RouteAttachment,
};
// Reached startup-resource refusal and harness wait observation. Context survives acknowledged Shutdown.
struct PathReadWaitObserver final
{
    void* context = nullptr;
    std::int32_t (*fault)(void*, PathReadWaitFault) noexcept = nullptr;
    // Harness-only acknowledgement that the actual wait callback published readiness. Never IO completion.
    void (*signalled)(void*, std::uint64_t readCycle) noexcept = nullptr;
    // Harness-only initial identity; ordinary startup begins at zero.
    std::uint64_t initialReadCycleForTest = 0;
};

class PathReadWaitReceiver
{
  public:
    virtual ~PathReadWaitReceiver() = default;
    // Owner pump only. Cancellation requests a worker retirement stage without releasing the read slot.
    // A normal event merely requests an actual nonblocking GetOverlappedResult on the original worker.
    virtual void PathReadReady(JobTicket ticket, bool cancellationOnly) noexcept = 0;
};

// One native read belongs to the selected ordered host path request. Its startup registration shares the
// scheduler wait pool. Immediate or refused issue still consumes readiness; an incomplete native read
// can rearm that registration while retaining its identity. A new read cannot reuse the event until the
// preceding registered readiness and original worker/native acknowledgements have joined.
class PathReadWait final : public OwnerWorkPump
{
  public:
    PathReadWait() noexcept;
    ~PathReadWait();
    PathReadWait(const PathReadWait&) = delete;
    PathReadWait& operator=(const PathReadWait&) = delete;
    [[nodiscard]] std::int32_t Initialize(JobScheduler&, OwnerWorkMultiplexer&, PathReadWaitObserver = {}) noexcept;
    // Owner joined-stage callback, before the issuing worker resumes. Reset only with no pending IO.
    // For an incomplete result after a consumed event, the worker resets and rechecks before this call;
    // resetEvent=false preserves a completion which arrived between that recheck and this rearm.
    // Rearm retains this same native read identity; it must match the original ticket. Only a new
    // resetEvent=true operation consumes an identity and can refuse counter exhaustion before issue.
    [[nodiscard]] std::int32_t PrepareRead(JobTicket, CancelToken, PathReadWaitReceiver&, bool resetEvent) noexcept;
    // Exact worker joined stage. Readiness cannot Resume a still-running producer.
    [[nodiscard]] bool AwaitRead(JobTicket) noexcept;
    [[nodiscard]] void* EventForWorker() const noexcept;
    // Owner harness observation only; never IO completion or continuation authority.
    [[nodiscard]] std::uint64_t ReadCycleForTest() const noexcept;
    [[nodiscard]] bool HasPending() const noexcept override;
    [[nodiscard]] bool HasReady() const noexcept override;
    void Pump() noexcept override;
    void StopNotifications() noexcept;
    // Joined owner shutdown hook: clear borrowed wrappers/detach but retain the event for the final worker's
    // real CancelIoEx/GetOverlappedResult acknowledgement. The event closes only after Jobs::Close returns.
    void DetachJoinedOwner() noexcept;
    void CloseAfterFinalWorkers() noexcept;

  private:
    struct Impl;
    std::unique_ptr<Impl> _impl;
};
