#pragma once

#include "Jobs.h"
#include "OwnerWorkMultiplexer.h"

#include <cstddef>
#include <cstdint>
#include <memory>

enum class RetryWaitStatus : std::uint8_t
{
    Ready,
    Cancelled,
    Failed,
};

class RetryWaitReceiver
{
  public:
    virtual ~RetryWaitReceiver() = default;
    // Owner pump only. The suspended Job owns this receiver until this acknowledgement. Resume retains the
    // original ticket, reservation and FIFO position. Never create a younger replacement job here.
    virtual void RetryWaitReady(JobTicket ticket, RetryWaitStatus status, std::int32_t error) noexcept = 0;
};

enum class RetryWaitFault : std::uint8_t
{
    EventCreation,
    RegisterWait,
    ResetEvent,
    ArmWait,
    SignalCancel,
};

struct RetryWaitObserver final
{
    void* context = nullptr;
    std::int32_t (*fault)(void*, RetryWaitFault, std::size_t) noexcept = nullptr;
    void (*armed)(void*, JobTicket, unsigned int, std::uint32_t) noexcept = nullptr;
    // Test-only event gating. Production supplies no callback. Its context survives StopNotifications.
    void (*beforePublication)(void*) noexcept = nullptr;
};

// Eight persistent event/deadline waits on the scheduler's existing WaitPool. A deadline or cancellation
// queues one acknowledgement. A slot is never reused until its callback publishes readiness; no hot-path
// join, stop_callback destructor, compute worker, recurring timer or polling belongs to this queue.
class RetryWaitQueue final : public OwnerWorkPump
{
  public:
    static constexpr std::size_t kCapacity = 8;
    [[nodiscard]] static constexpr std::uint32_t DelayMs(unsigned int retry) noexcept
    {
        return retry >= 1 && retry <= 5 ? 50u << (retry - 1) : 0;
    }

    RetryWaitQueue() noexcept;
    ~RetryWaitQueue();
    RetryWaitQueue(const RetryWaitQueue&) = delete;
    RetryWaitQueue& operator=(const RetryWaitQueue&) = delete;
    [[nodiscard]] std::int32_t Initialize(JobScheduler& scheduler, OwnerWorkMultiplexer& routes,
                                          RetryWaitObserver observer = {}) noexcept;
    // Owner only, from successful DeferCompletion. Retry 1..5 follows the initial transfer attempt. The
    // complete suspended Job/reservation already exists. Failure retains all ownership with that Job.
    [[nodiscard]] std::int32_t Start(JobTicket ticket, unsigned int retry, CancelToken cancellation,
                                     RetryWaitReceiver& receiver) noexcept;
    [[nodiscard]] bool HasPending() const noexcept override;
    [[nodiscard]] bool HasReady() const noexcept override;
    void Pump() noexcept override;
    // Stop/join event producers before JobScheduler::Close. Drop receivers only after workers close.
    void StopNotifications() noexcept;
    void CloseAfterWorkers() noexcept;
    // Event-driven correctness control; signals the actual wait, never fakes its callback acknowledgement.
    [[nodiscard]] bool SignalDeadlineForTest(JobTicket ticket) noexcept;

  private:
    struct Impl;
    std::unique_ptr<Impl> _impl;
};
