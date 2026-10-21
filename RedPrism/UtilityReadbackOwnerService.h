#pragma once

#include "GpuResidencyBudget.h"
#include "Jobs.h"

#include <array>
#include <cstdint>

class OwnerWorkMultiplexer;
class SharedPublicationMemory;

// Original admitted parent; methods only mark/resume bounded work. No recursion, detach/destruction of
// this receiver from Pump, modal, wait, source capture, pixel copy, GPU creation or document mutation.
class UtilityReadbackOwner
{
  public:
    virtual ~UtilityReadbackOwner() = default;
    [[nodiscard]] virtual bool HasUtilityOwnerWork() const noexcept = 0;
    [[nodiscard]] virtual bool IsUtilityOwnerReady() const noexcept = 0;
    virtual void PumpUtilityOwner() noexcept = 0;
    virtual void StopUtilityOwner() noexcept = 0;
};

// Startup registered leaf for all original utility parents; fixed slots borrow receivers by exact JobTicket.
// Queue/command callbacks publish flags and NotifyOwnerWork. A dormant parent needs no private wait/poll loop.
// Joined-Close reservations remain on the owner until the scheduler's actual final worker cleanup returns.
class UtilityReadbackOwnerService final : public OwnerWorkPump
{
  public:
    UtilityReadbackOwnerService() = default;
    ~UtilityReadbackOwnerService();
    UtilityReadbackOwnerService(const UtilityReadbackOwnerService&) = delete;
    UtilityReadbackOwnerService& operator=(const UtilityReadbackOwnerService&) = delete;
    [[nodiscard]] std::int32_t InitializeOwner(JobScheduler& scheduler, OwnerWorkMultiplexer& routes) noexcept;
    // First admitted original owner continuation only. No source/GPU capture or fresh admission occurs here.
    [[nodiscard]] bool AttachOwner(JobTicket original, UtilityReadbackOwner& parent) noexcept;
    // Parent independently excludes every callback/producer, including destruction-only original Close join.
    // Never call inside Pump. Ordinary removal additionally requires no source work/readiness remains.
    [[nodiscard]] bool DetachOwner(JobTicket original, UtilityReadbackOwner& parent) noexcept;
    // Joined destruction accounting only: preallocated memory must be bound to that exact original record.
    // The parent proves original compute/recorder join, GPU acknowledgment and row release separately.
    [[nodiscard]] bool RetainJoinedChargeOwner(JobTicket original, UtilityReadbackOwner& parent,
                                               GpuResidencyReservation& originalCharge,
                                               const SharedPublicationMemory& boundMemory) noexcept;
    void StopOwner() noexcept;
    // Only after JobScheduler::Close returned from final worker cleanup. Refuses while its wake is live.
    [[nodiscard]] bool ReleaseJoinedChargesOwner() noexcept;
    [[nodiscard]] bool CloseOwner() noexcept;
    [[nodiscard]] bool HasPending() const noexcept override;
    [[nodiscard]] bool HasReady() const noexcept override;
    void Pump() noexcept override;

  private:
    struct Borrow final
    {
        JobTicket original;
        UtilityReadbackOwner* parent = nullptr;
    };
    [[nodiscard]] bool IsOwner() const noexcept;
    [[nodiscard]] bool MatchesOwner(JobTicket original, const UtilityReadbackOwner& parent) const noexcept;
    JobScheduler* _scheduler = nullptr;
    OwnerWorkMultiplexer* _routes = nullptr;
    std::uint32_t _ownerThread = 0;
    std::array<Borrow, JobScheduler::kCapacity> _parents;
    std::array<std::uint64_t, (JobScheduler::kCapacity + 63) / 64> _activeMasks{};
    std::size_t _activeCount = 0;
    std::size_t _joinedChargeCount = 0;
    std::array<GpuResidencyReservation, JobScheduler::kCapacity> _joinedCharges;
    std::size_t _next = 0;
    bool _stopped = false;
    bool _pumping = false;
};
