#include "UtilityReadbackOwnerService.h"

#include "OwnerWorkMultiplexer.h"
#include "SharedPublicationMemory.h"

#include <Windows.h>
#include <bit>
#include <exception>
#include <utility>

UtilityReadbackOwnerService::~UtilityReadbackOwnerService()
{
    if (_scheduler || HasPending())
        std::terminate();
}

bool UtilityReadbackOwnerService::IsOwner() const noexcept
{
    return _ownerThread == GetCurrentThreadId() && _scheduler;
}

std::int32_t UtilityReadbackOwnerService::InitializeOwner(JobScheduler& scheduler,
                                                          OwnerWorkMultiplexer& routes) noexcept
{
    if (_ownerThread || !scheduler.WakeHandle() || scheduler.InUiCallback())
        return E_INVALIDARG;
    if (!routes.Attach(*this))
        return HRESULT_FROM_WIN32(ERROR_NOT_ENOUGH_QUOTA);
    _ownerThread = GetCurrentThreadId();
    _scheduler = &scheduler;
    _routes = &routes;
    return S_OK;
}

bool UtilityReadbackOwnerService::MatchesOwner(JobTicket original, const UtilityReadbackOwner& parent) const noexcept
{
    return IsOwner() && original.slot < _parents.size() && original.generation &&
           _parents[original.slot].parent == &parent && _parents[original.slot].original == original;
}

bool UtilityReadbackOwnerService::AttachOwner(JobTicket original, UtilityReadbackOwner& parent) noexcept
{
    if (!IsOwner() || _stopped || _pumping || original.slot >= _parents.size() || !original.generation ||
        !_scheduler->InAcknowledgedJobCallback(original) || _parents[original.slot].parent ||
        _joinedCharges[original.slot])
        return false;
    for (const auto& entry : _parents)
        if (entry.parent == &parent)
            return false;
    _parents[original.slot] = {original, &parent};
    _activeMasks[original.slot / 64] |= std::uint64_t{1} << (original.slot % 64);
    ++_activeCount;
    _scheduler->NotifyOwnerWork();
    return true;
}

bool UtilityReadbackOwnerService::DetachOwner(JobTicket original, UtilityReadbackOwner& parent) noexcept
{
    if (!IsOwner() || _pumping || !MatchesOwner(original, parent) || parent.HasUtilityOwnerWork() ||
        parent.IsUtilityOwnerReady())
        return false;
    _parents[original.slot] = {};
    _activeMasks[original.slot / 64] &= ~(std::uint64_t{1} << (original.slot % 64));
    --_activeCount;
    _scheduler->NotifyOwnerWork();
    return true;
}

bool UtilityReadbackOwnerService::RetainJoinedChargeOwner(JobTicket original, UtilityReadbackOwner& parent,
                                                          GpuResidencyReservation& originalCharge,
                                                          const SharedPublicationMemory& boundMemory) noexcept
{
    if (!IsOwner() || !_stopped || _pumping || !MatchesOwner(original, parent) || !originalCharge ||
        _joinedCharges[original.slot] || !boundMemory.BoundTo(*_scheduler, original))
        return false;
    _joinedCharges[original.slot] = std::move(originalCharge);
    ++_joinedChargeCount;
    return true;
}

void UtilityReadbackOwnerService::StopOwner() noexcept
{
    if (!IsOwner() || _pumping || _stopped)
        return;
    _stopped = true;
    _pumping = true; // Stop callbacks also cannot mutate the borrowed fixed array.
    for (const auto& entry : _parents)
        if (entry.parent)
            entry.parent->StopUtilityOwner();
    _pumping = false;
}

bool UtilityReadbackOwnerService::ReleaseJoinedChargesOwner() noexcept
{
    if (!IsOwner() || !_stopped || _pumping || _scheduler->WakeHandle())
        return false;
    for (const auto& entry : _parents)
        if (entry.parent)
            return false;
    for (auto& charge : _joinedCharges)
        charge.ResetOwner();
    _joinedChargeCount = 0;
    return true;
}

bool UtilityReadbackOwnerService::CloseOwner() noexcept
{
    if (!IsOwner() || _pumping || _scheduler->InUiCallback() || HasPending() || HasReady() || !_routes->Detach(*this))
        return false;
    _routes = nullptr;
    _scheduler = nullptr;
    return true;
}

bool UtilityReadbackOwnerService::HasPending() const noexcept
{
    return _activeCount != 0 || _joinedChargeCount != 0;
}

bool UtilityReadbackOwnerService::HasReady() const noexcept
{
    if (!IsOwner() || _stopped || _pumping || !_activeCount)
        return false;
    for (std::size_t word = 0; word < _activeMasks.size(); ++word)
    {
        auto bits = _activeMasks[word];
        while (bits)
        {
            const auto index = word * 64 + std::countr_zero(bits);
            if (_parents[index].parent->IsUtilityOwnerReady())
                return true;
            bits &= bits - 1;
        }
    }
    return false;
}

void UtilityReadbackOwnerService::Pump() noexcept
{
    if (!HasReady())
        return;
    _pumping = true;
    for (std::size_t offset = 0; offset < _parents.size(); ++offset)
    {
        const auto index = (_next + offset) % _parents.size();
        const auto& entry = _parents[index];
        if (entry.parent && entry.parent->IsUtilityOwnerReady())
        {
            _next = (index + 1) % _parents.size();
            entry.parent->PumpUtilityOwner();
            break;
        }
    }
    _pumping = false;
}
