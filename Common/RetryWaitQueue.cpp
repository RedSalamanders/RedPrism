#include "RetryWaitQueue.h"

#include <windows.h>

#include <array>
#include <atomic>
#include <cassert>
#include <new>
#include <utility>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/resource.h>
#pragma warning(pop)

struct RetryWaitQueue::Impl final
{
    struct Slot final
    {
        Impl* owner = nullptr;
        wil::unique_handle event;
        JobEventTicket wait;
        std::atomic<bool> ready{false};
        JobTicket ticket;
        CancelToken cancellation;
        RetryWaitReceiver* receiver = nullptr;
        std::int32_t error = 0;
        bool cancelSignalled = false;
    };

    JobScheduler* scheduler = nullptr;
    OwnerWorkMultiplexer* routes = nullptr;
    RetryWaitObserver observer;
    std::array<Slot, kCapacity> slots;
    DWORD ownerThread = GetCurrentThreadId();
    std::size_t next = 0;
    bool attached = false, stopped = false, pumping = false;

    void CheckOwner() const noexcept
    {
        assert(GetCurrentThreadId() == ownerThread);
    }
    std::int32_t Fault(RetryWaitFault fault, std::size_t slot) const noexcept
    {
        return observer.fault ? observer.fault(observer.context, fault, slot) : S_OK;
    }
    static void Signalled(void* context) noexcept
    {
        auto& slot = *static_cast<Slot*>(context);
        // Read stable owners before publication. After the release-store, the owner may deliver/reuse this
        // slot while this callback finishes NotifyOwnerWork. No subsequent callback access to slot metadata.
        auto* scheduler = slot.owner->scheduler;
        const auto observer = slot.owner->observer;
        if (observer.beforePublication)
            observer.beforePublication(observer.context);
        slot.ready.store(true, std::memory_order_release);
        scheduler->NotifyOwnerWork();
    }
};

RetryWaitQueue::RetryWaitQueue() noexcept = default;
RetryWaitQueue::~RetryWaitQueue()
{
    if (_impl)
    {
        assert(!HasPending());
        StopNotifications();
        CloseAfterWorkers();
    }
}

std::int32_t RetryWaitQueue::Initialize(JobScheduler& scheduler, OwnerWorkMultiplexer& routes,
                                        RetryWaitObserver observer) noexcept
{
    if (_impl || !scheduler.WakeHandle() || scheduler.InUiCallback())
        return HRESULT_FROM_WIN32(ERROR_INVALID_STATE);
    try
    {
        _impl = std::make_unique<Impl>();
    }
    catch (const std::bad_alloc&)
    {
        return E_OUTOFMEMORY;
    }
    auto& self = *_impl;
    self.scheduler = &scheduler;
    self.routes = &routes;
    self.observer = observer;
    for (std::size_t index = 0; index < kCapacity; ++index)
    {
        auto& slot = self.slots[index];
        slot.owner = &self;
        auto result = self.Fault(RetryWaitFault::EventCreation, index);
        if (FAILED(result))
            return result;
        slot.event.reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
        if (!slot.event)
            return HRESULT_FROM_WIN32(GetLastError());
        result = self.Fault(RetryWaitFault::RegisterWait, index);
        if (FAILED(result))
            return result;
        result = scheduler.RegisterEventWait(slot.event.get(), Impl::Signalled, &slot, slot.wait);
        if (FAILED(result))
            return result;
    }
    if (!routes.Attach(*this))
        return HRESULT_FROM_WIN32(ERROR_INVALID_STATE);
    self.attached = true;
    return S_OK;
}

std::int32_t RetryWaitQueue::Start(JobTicket ticket, unsigned int retry, CancelToken cancellation,
                                   RetryWaitReceiver& receiver) noexcept
{
    if (!_impl || _impl->stopped || !_impl->attached || ticket.slot >= JobScheduler::kCapacity ||
        ticket.generation == 0 || DelayMs(retry) == 0)
        return HRESULT_FROM_WIN32(ERROR_INVALID_STATE);
    auto& self = *_impl;
    self.CheckOwner();
    for (const auto& slot : self.slots)
        if (slot.receiver && slot.ticket == ticket)
            return HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS);
    for (std::size_t index = 0; index < kCapacity; ++index)
    {
        auto& slot = self.slots[index];
        if (slot.receiver)
            continue;
        auto result = self.Fault(RetryWaitFault::ResetEvent, index);
        if (FAILED(result))
            return result;
        if (!ResetEvent(slot.event.get()))
            return HRESULT_FROM_WIN32(GetLastError());
        result = self.Fault(RetryWaitFault::ArmWait, index);
        if (FAILED(result))
            return result;
        slot.ready.store(false, std::memory_order_relaxed);
        slot.ticket = ticket;
        slot.cancellation = cancellation;
        slot.receiver = &receiver;
        slot.error = S_OK;
        slot.cancelSignalled = false;
        // Startup waits have never fired; reused waits have published their sole prior callback. Resetting
        // the event and arming a new relative timeout cannot recall/relabel an unacknowledged old callback.
        if (!self.scheduler->RearmEventWait(slot.wait, DelayMs(retry)))
        {
            slot.receiver = nullptr;
            slot.cancellation = {};
            return HRESULT_FROM_WIN32(ERROR_INVALID_STATE);
        }
        if (self.observer.armed)
            self.observer.armed(self.observer.context, ticket, retry, DelayMs(retry));
        // A pre-cancelled token must request a pump even when Start is used outside the completion drain.
        if (cancellation.IsCancelled())
            self.scheduler->NotifyOwnerWork();
        return S_OK;
    }
    return HRESULT_FROM_WIN32(ERROR_NOT_ENOUGH_QUOTA);
}

bool RetryWaitQueue::HasPending() const noexcept
{
    if (_impl)
    {
        _impl->CheckOwner();
        for (const auto& slot : _impl->slots)
            if (slot.receiver)
                return true;
    }
    return false;
}

bool RetryWaitQueue::HasReady() const noexcept
{
    if (_impl && !_impl->stopped)
    {
        _impl->CheckOwner();
        for (const auto& slot : _impl->slots)
            if (slot.receiver && (slot.ready.load(std::memory_order_acquire) ||
                                  (!slot.cancelSignalled && slot.cancellation.IsCancelled())))
                return true;
    }
    return false;
}

void RetryWaitQueue::Pump() noexcept
{
    if (!_impl || _impl->stopped || _impl->pumping)
        return;
    auto& self = *_impl;
    self.CheckOwner();
    self.pumping = true;
    const auto finish = wil::scope_exit([&]() noexcept { self.pumping = false; });
    for (std::size_t offset = 0; offset < kCapacity; ++offset)
    {
        const auto index = (self.next + offset) % kCapacity;
        auto& slot = self.slots[index];
        if (!slot.receiver)
            continue;
        if (slot.ready.load(std::memory_order_acquire))
        {
            self.next = (index + 1) % kCapacity;
            auto* receiver = std::exchange(slot.receiver, nullptr);
            const auto ticket = slot.ticket;
            const auto error = slot.error;
            const auto status = slot.cancellation.IsCancelled() ? RetryWaitStatus::Cancelled
                                : FAILED(error)                 ? RetryWaitStatus::Failed
                                                                : RetryWaitStatus::Ready;
            slot.cancellation = {};
            slot.ready.store(false, std::memory_order_relaxed);
            // No slot accesses after delivery: the receiver may arm its next retry in this same slot.
            receiver->RetryWaitReady(ticket, status, error);
            return;
        }
        if (!slot.cancelSignalled && slot.cancellation.IsCancelled())
        {
            self.next = (index + 1) % kCapacity;
            slot.cancelSignalled = true;
            slot.error = self.Fault(RetryWaitFault::SignalCancel, index);
            if (SUCCEEDED(slot.error) && !SetEvent(slot.event.get()))
                slot.error = HRESULT_FROM_WIN32(GetLastError());
            // Even a signal failure retains the slot/receiver until the original deadline acknowledges.
            // HasReady becomes false until that callback publishes; no failed-signal busy loop.
            return;
        }
    }
}

void RetryWaitQueue::StopNotifications() noexcept
{
    if (_impl && !_impl->stopped)
    {
        _impl->CheckOwner();
        assert(!_impl->pumping && !_impl->scheduler->InUiCallback());
        if (_impl->pumping || _impl->scheduler->InUiCallback())
            return;
        _impl->stopped = true;
        for (auto& slot : _impl->slots)
            if (slot.wait.generation != 0)
            {
                (void)_impl->scheduler->RemoveEventWait(slot.wait);
                slot.wait = {};
            }
    }
}

void RetryWaitQueue::CloseAfterWorkers() noexcept
{
    if (!_impl)
        return;
    auto& self = *_impl;
    self.CheckOwner();
    assert(self.stopped && !self.pumping);
    if (!self.stopped || self.pumping)
        return;
    for (auto& slot : self.slots)
    {
        slot.receiver = nullptr;
        slot.cancellation = {};
        slot.ready.store(false, std::memory_order_relaxed);
    }
    if (self.attached)
    {
        const bool detached = self.routes->Detach(*this);
        assert(detached);
        (void)detached;
    }
    _impl.reset();
}

bool RetryWaitQueue::SignalDeadlineForTest(JobTicket ticket) noexcept
{
    if (!_impl || _impl->stopped)
        return false;
    _impl->CheckOwner();
    for (auto& slot : _impl->slots)
        if (slot.receiver && slot.ticket == ticket)
            return SetEvent(slot.event.get()) != FALSE;
    return false;
}
