#include "PathReadWait.h"

#include <atomic>
#include <cassert>
#include <exception>
#include <new>
#include <utility>
#include <windows.h>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/resource.h>
#pragma warning(pop)

// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027)
struct PathReadWait::Impl final
{
    JobScheduler* scheduler = nullptr;
    OwnerWorkMultiplexer* routes = nullptr;
    wil::unique_event_nothrow event;
    JobEventTicket wait;
    std::atomic<bool> ready{false};
    std::atomic<std::uint64_t> readCycle{0};
    JobTicket ticket;
    CancelToken cancellation;
    PathReadWaitReceiver* receiver = nullptr;
    PathReadWaitObserver observer;
    const DWORD ownerThread = GetCurrentThreadId();
    bool attached = false, stopped = false, awaiting = false, cancellationDelivered = false, pumping = false;

    void CheckOwner() const noexcept
    {
        assert(GetCurrentThreadId() == ownerThread);
    }
    static void Signalled(void* opaque) noexcept
    {
        auto& self = *static_cast<Impl*>(opaque);
        auto* scheduler = self.scheduler;
        const auto observer = self.observer;
        const auto readCycle = self.readCycle.load(std::memory_order_relaxed);
        self.ready.store(true, std::memory_order_release);
        if (observer.signalled)
            observer.signalled(observer.context, readCycle);
        // No request/event/slot access after publication. StopNotifications joins this callback before close.
        scheduler->NotifyOwnerWork();
    }
};
#pragma warning(pop)

PathReadWait::PathReadWait() noexcept = default;
PathReadWait::~PathReadWait()
{
    assert(!_impl); // Application closes after final worker acknowledgement, including partial startup.
}
std::int32_t PathReadWait::Initialize(JobScheduler& scheduler, OwnerWorkMultiplexer& routes,
                                      PathReadWaitObserver observer) noexcept
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
    self.readCycle.store(observer.initialReadCycleForTest, std::memory_order_relaxed);
    const auto fault = [&](PathReadWaitFault point) noexcept -> std::int32_t
    { return observer.fault ? observer.fault(observer.context, point) : S_OK; };
    const auto eventFault = fault(PathReadWaitFault::EventCreation);
    if (FAILED(eventFault))
        return eventFault;
    const auto created = self.event.create(wil::EventOptions::ManualReset);
    if (FAILED(created))
        return created;
    const auto registrationFault = fault(PathReadWaitFault::WaitRegistration);
    if (FAILED(registrationFault))
        return registrationFault;
    const auto registered = scheduler.RegisterEventWait(self.event.get(), Impl::Signalled, &self, self.wait);
    if (FAILED(registered))
        return registered;
    const auto attachmentFault = fault(PathReadWaitFault::RouteAttachment);
    if (FAILED(attachmentFault))
        return attachmentFault;
    if (!routes.Attach(*this))
        return HRESULT_FROM_WIN32(ERROR_INVALID_STATE);
    self.attached = true;
    return S_OK;
}
std::int32_t PathReadWait::PrepareRead(JobTicket ticket, CancelToken cancellation, PathReadWaitReceiver& receiver,
                                       bool resetEvent) noexcept
{
    if (!_impl || _impl->stopped || !_impl->attached || ticket.slot >= JobScheduler::kCapacity || !ticket.generation)
        return HRESULT_FROM_WIN32(ERROR_INVALID_STATE);
    auto& self = *_impl;
    self.CheckOwner();
    if (self.receiver)
        return HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS);
    const auto cycle = self.readCycle.load(std::memory_order_relaxed);
    if (resetEvent && cycle == UINT64_MAX)
        return HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW);
    if (!resetEvent && (!cycle || self.ticket != ticket))
        return HRESULT_FROM_WIN32(ERROR_INVALID_STATE);
    if (resetEvent && !ResetEvent(self.event.get()))
        return HRESULT_FROM_WIN32(GetLastError());
    self.ready.store(false, std::memory_order_relaxed);
    if (resetEvent)
        self.readCycle.store(cycle + 1, std::memory_order_relaxed);
    self.ticket = ticket;
    self.cancellation = cancellation;
    self.receiver = &receiver;
    self.awaiting = false;
    self.cancellationDelivered = false;
    if (!self.scheduler->RearmEventWait(self.wait))
    {
        self.receiver = nullptr;
        self.cancellation = {};
        return HRESULT_FROM_WIN32(ERROR_INVALID_STATE);
    }
    return S_OK;
}
bool PathReadWait::AwaitRead(JobTicket ticket) noexcept
{
    if (!_impl || _impl->stopped)
        return false;
    auto& self = *_impl;
    self.CheckOwner();
    if (!self.receiver || self.ticket != ticket)
        return false;
    self.awaiting = true;
    if (HasReady())
        self.scheduler->NotifyOwnerWork();
    return true;
}
void* PathReadWait::EventForWorker() const noexcept
{
    if (!_impl || _impl->stopped)
        return nullptr;
    _impl->CheckOwner();
    return _impl->event.get();
}
std::uint64_t PathReadWait::ReadCycleForTest() const noexcept
{
    if (!_impl)
        return 0;
    _impl->CheckOwner();
    return _impl->readCycle.load(std::memory_order_relaxed);
}
bool PathReadWait::HasPending() const noexcept
{
    if (!_impl)
        return false;
    _impl->CheckOwner();
    return _impl->receiver != nullptr;
}
bool PathReadWait::HasReady() const noexcept
{
    if (!_impl || _impl->stopped)
        return false;
    _impl->CheckOwner();
    return _impl->receiver && _impl->awaiting &&
           (_impl->ready.load(std::memory_order_acquire) ||
            (!_impl->cancellationDelivered && _impl->cancellation.IsCancelled()));
}
void PathReadWait::Pump() noexcept
{
    if (!HasReady() || _impl->pumping)
        return;
    auto& self = *_impl;
    self.pumping = true;
    const auto finish = wil::scope_exit([&]() noexcept { self.pumping = false; });
    auto* receiver = self.receiver;
    const auto ticket = self.ticket;
    self.awaiting = false;
    if (self.ready.load(std::memory_order_acquire))
    {
        self.receiver = nullptr;
        self.cancellation = {};
        self.ready.store(false, std::memory_order_relaxed);
        receiver->PathReadReady(ticket, false);
        return;
    }
    // Do not signal OVERLAPPED.hEvent to emulate completion. The worker cancels the actual operation;
    // this original slot remains live until Windows queues its real completion event.
    self.cancellationDelivered = true;
    receiver->PathReadReady(ticket, true);
}
void PathReadWait::StopNotifications() noexcept
{
    if (!_impl || _impl->stopped)
        return;
    auto& self = *_impl;
    self.CheckOwner();
    assert(!self.pumping && !self.scheduler->InUiCallback());
    if (self.pumping || self.scheduler->InUiCallback())
        return;
    self.stopped = true;
    if (self.wait.generation)
    {
        (void)self.scheduler->RemoveEventWait(self.wait);
        self.wait = {};
    }
}
void PathReadWait::DetachJoinedOwner() noexcept
{
    if (!_impl)
        return;
    auto& self = *_impl;
    self.CheckOwner();
    assert(self.stopped && !self.pumping);
    if (!self.stopped || self.pumping)
        return;
    self.receiver = nullptr;
    self.cancellation = {};
    self.awaiting = false;
    self.ready.store(false, std::memory_order_relaxed);
    if (self.attached)
    {
        const bool detached = self.routes->Detach(*this);
        assert(detached);
        if (!detached)
            std::terminate();
        self.attached = false;
    }
}
void PathReadWait::CloseAfterFinalWorkers() noexcept
{
    if (!_impl)
        return;
    _impl->CheckOwner();
    assert(_impl->stopped && !_impl->attached && !_impl->receiver && !_impl->pumping);
    if (!_impl->stopped || _impl->attached || _impl->receiver || _impl->pumping)
        return;
    _impl.reset();
}
