#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

struct GpuResidencySnapshot final
{
    std::uint64_t deviceGeneration = 0;
    std::uint64_t budgetBytes = 0;
    std::uint64_t currentUsageBytes = 0;
};

enum class GpuResidencyAdmission : std::uint8_t
{
    Ready,
    Invalid,
    Pressure,
    Capacity,
    Exhausted,
};

class GpuResidencyBudget;

// Move-only owner reservation for one complete overlapping GPU graph. Immutable fields may be read by
// workers; move/reset/destruction stay on the admitting owner after all device users acknowledge. It holds
// neither device nor CPU owners. The budget outlives all reservations, including retired device generations.
class GpuResidencyReservation final
{
  public:
    GpuResidencyReservation() noexcept = default;
    ~GpuResidencyReservation();
    GpuResidencyReservation(const GpuResidencyReservation&) = delete;
    GpuResidencyReservation& operator=(const GpuResidencyReservation&) = delete;
    GpuResidencyReservation(GpuResidencyReservation&& other) noexcept;
    GpuResidencyReservation& operator=(GpuResidencyReservation&& other) noexcept;
    [[nodiscard]] explicit operator bool() const noexcept
    {
        return _budget != nullptr;
    }
    [[nodiscard]] std::uint64_t Bytes() const noexcept
    {
        return _bytes;
    }
    [[nodiscard]] std::uint64_t DeviceGeneration() const noexcept
    {
        return _deviceGeneration;
    }
    void ResetOwner() noexcept;

  private:
    friend class GpuResidencyBudget;
    GpuResidencyBudget* _budget = nullptr;
    std::size_t _slot = 128;
    std::uint64_t _ticket = 0;
    std::uint64_t _bytes = 0;
    std::uint64_t _deviceGeneration = 0;
};

// Fixed owner-only accounting. Snapshot comes from the actual device's successful DXGI budget query; zero
// or unknown budget/generation never admits. Charge all live and retired graph reservations concurrently.
// CurrentUsage and declared reservations deliberately overlap conservatively. Reserve CPU separately before
// capturing document roots; release this reservation if CPU admission/submission refuses. No query, wait,
// allocation, overwrite or partial reservation occurs here. This is not a responsiveness or capacity proof.
class GpuResidencyBudget final
{
  public:
    static constexpr std::size_t kCapacity = 128;
    static constexpr std::uint64_t kMinimumHeadroom = 64ull * 1024 * 1024;
    GpuResidencyBudget() noexcept;
    ~GpuResidencyBudget();
    GpuResidencyBudget(const GpuResidencyBudget&) = delete;
    GpuResidencyBudget& operator=(const GpuResidencyBudget&) = delete;
    [[nodiscard]] GpuResidencyReservation ReserveOwner(std::uint64_t bytes, GpuResidencySnapshot snapshot,
                                                       GpuResidencyAdmission& result) noexcept;
    [[nodiscard]] std::uint64_t ReservedOwner() const noexcept;
    [[nodiscard]] std::size_t ActiveOwner() const noexcept;
    [[nodiscard]] bool ExhaustTicketsForTest() noexcept;

  private:
    friend class GpuResidencyReservation;
    struct Slot final
    {
        std::uint64_t ticket = 0;
        std::uint64_t bytes = 0;
    };
    [[nodiscard]] bool ReleaseOwner(std::size_t slot, std::uint64_t ticket, std::uint64_t bytes) noexcept;
    [[nodiscard]] bool IsOwner() const noexcept;
    std::array<Slot, kCapacity> _slots{};
    std::uint64_t _reserved = 0;
    std::uint64_t _nextTicket = 0;
    std::size_t _active = 0;
    std::uint32_t _ownerThread = 0;
};
