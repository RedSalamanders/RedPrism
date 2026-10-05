#include "GpuResidencyBudget.h"

#include <algorithm>
#include <cassert>
#include <exception>
#include <limits>
#include <utility>
#include <windows.h>

GpuResidencyReservation::~GpuResidencyReservation()
{
    ResetOwner();
}

GpuResidencyReservation::GpuResidencyReservation(GpuResidencyReservation&& other) noexcept
    : _budget(std::exchange(other._budget, nullptr)), _slot(std::exchange(other._slot, GpuResidencyBudget::kCapacity)),
      _ticket(std::exchange(other._ticket, 0)), _bytes(std::exchange(other._bytes, 0)),
      _deviceGeneration(std::exchange(other._deviceGeneration, 0))
{
}

GpuResidencyReservation& GpuResidencyReservation::operator=(GpuResidencyReservation&& other) noexcept
{
    if (this != &other)
    {
        ResetOwner();
        _budget = std::exchange(other._budget, nullptr);
        _slot = std::exchange(other._slot, GpuResidencyBudget::kCapacity);
        _ticket = std::exchange(other._ticket, 0);
        _bytes = std::exchange(other._bytes, 0);
        _deviceGeneration = std::exchange(other._deviceGeneration, 0);
    }
    return *this;
}

void GpuResidencyReservation::ResetOwner() noexcept
{
    if (_budget)
    {
        const bool released = _budget->ReleaseOwner(_slot, _ticket, _bytes);
        assert(released);
        if (!released)
            std::terminate(); // ownership invariant; never silently lose the complete reservation
    }
    _budget = nullptr;
    _slot = GpuResidencyBudget::kCapacity;
    _ticket = 0;
    _bytes = 0;
    _deviceGeneration = 0;
}

GpuResidencyBudget::GpuResidencyBudget() noexcept : _ownerThread(GetCurrentThreadId()) {}
GpuResidencyBudget::~GpuResidencyBudget()
{
    const bool drained = IsOwner() && _reserved == 0 && _active == 0;
    assert(drained);
    if (!drained)
        std::terminate();
}
bool GpuResidencyBudget::IsOwner() const noexcept
{
    return _ownerThread == GetCurrentThreadId();
}

GpuResidencyReservation GpuResidencyBudget::ReserveOwner(std::uint64_t bytes, GpuResidencySnapshot snapshot,
                                                         GpuResidencyAdmission& result) noexcept
{
    result = GpuResidencyAdmission::Invalid;
    if (!IsOwner() || !bytes || !snapshot.deviceGeneration || !snapshot.budgetBytes)
        return {};
    result = GpuResidencyAdmission::Pressure;
    const auto headroom = (std::max)(kMinimumHeadroom, snapshot.budgetBytes / 8);
    if (snapshot.currentUsageBytes > snapshot.budgetBytes ||
        headroom > snapshot.budgetBytes - snapshot.currentUsageBytes)
        return {};
    const auto available = snapshot.budgetBytes - snapshot.currentUsageBytes - headroom;
    if (_reserved > available || bytes > available - _reserved)
        return {};
    if (_nextTicket == (std::numeric_limits<std::uint64_t>::max)())
    {
        result = GpuResidencyAdmission::Exhausted;
        return {};
    }
    const auto slot =
        std::find_if(_slots.begin(), _slots.end(), [](const Slot& value) noexcept { return value.bytes == 0; });
    if (slot == _slots.end())
    {
        result = GpuResidencyAdmission::Capacity;
        return {};
    }
    GpuResidencyReservation reservation;
    reservation._budget = this;
    reservation._slot = static_cast<std::size_t>(slot - _slots.begin());
    reservation._ticket = ++_nextTicket;
    reservation._bytes = bytes;
    reservation._deviceGeneration = snapshot.deviceGeneration;
    *slot = {_nextTicket, bytes};
    _reserved += bytes; // checked available ceiling proves no overflow
    ++_active;
    result = GpuResidencyAdmission::Ready;
    return reservation;
}

bool GpuResidencyBudget::ReleaseOwner(std::size_t slot, std::uint64_t ticket, std::uint64_t bytes) noexcept
{
    if (!IsOwner() || slot >= _slots.size() || !ticket || !bytes || _slots[slot].ticket != ticket ||
        _slots[slot].bytes != bytes || bytes > _reserved || !_active)
        return false;
    _reserved -= bytes;
    --_active;
    _slots[slot] = {};
    return true;
}

std::uint64_t GpuResidencyBudget::ReservedOwner() const noexcept
{
    return IsOwner() ? _reserved : 0;
}
std::size_t GpuResidencyBudget::ActiveOwner() const noexcept
{
    return IsOwner() ? _active : 0;
}
bool GpuResidencyBudget::ExhaustTicketsForTest() noexcept
{
    if (!IsOwner() || _active)
        return false;
    _nextTicket = (std::numeric_limits<std::uint64_t>::max)();
    return true;
}
