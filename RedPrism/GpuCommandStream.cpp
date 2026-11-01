#include "GpuCommandStream.h"

#include "GpuWork.h"

#include <cassert>
#include <exception>
#include <limits>

GpuCommandStream::~GpuCommandStream()
{
    // Neither owner destruction nor a cancellation may release an outstanding command graph.
    for (const auto& slot : _slots)
    {
        if (slot.command)
            std::terminate();
    }
}

HRESULT GpuCommandStream::InitializeOwner(JobScheduler& scheduler, JobTicket source,
                                          std::uint64_t deviceGeneration) noexcept
{
    if (_scheduler || !scheduler.WakeHandle() || source.slot >= JobScheduler::kCapacity || source.generation == 0 ||
        deviceGeneration == 0)
        return E_INVALIDARG;
    wil::unique_handle available(CreateEventW(nullptr, FALSE, FALSE, nullptr));
    if (!available)
        return HRESULT_FROM_WIN32(GetLastError());
    wil::unique_handle stopped(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    if (!stopped)
        return HRESULT_FROM_WIN32(GetLastError());
    _available = std::move(available);
    _stopped = std::move(stopped);
    _source = source;
    _deviceGeneration = deviceGeneration;
    _ownerThread = GetCurrentThreadId();
    _scheduler = &scheduler;
    _productionEpoch.store(1, std::memory_order_release);
    return S_OK;
}

HRESULT GpuCommandStream::InitializeOwner(JobScheduler& scheduler, JobTicket source, const GpuWorkQueue& queue,
                                          const GpuCompletionContext& current) noexcept
{
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* immediate = nullptr;
    const HRESULT borrowed = queue.BorrowCurrentContextOwner(current, device, immediate);
    if (FAILED(borrowed))
        return borrowed;
    const HRESULT initialized = InitializeOwner(scheduler, source, current.DeviceGeneration());
    if (SUCCEEDED(initialized))
        _queueOrigin = current._origin;
    return initialized;
}

bool GpuCommandStream::IsOwner() const noexcept
{
    return _scheduler && _ownerThread == GetCurrentThreadId();
}

bool GpuCommandStream::IsStopped() const noexcept
{
    return _stopped && WaitForSingleObject(_stopped.get(), 0) == WAIT_OBJECT_0;
}

bool GpuCommandStream::CanRecordForWork() const noexcept
{
    return SUCCEEDED(RecordingStatusForWork());
}

HRESULT GpuCommandStream::RecordingStatusForWork() const noexcept
{
    if (!_scheduler || IsOwner() || _finished.load(std::memory_order_acquire) ||
        _retirementAuthorized.load(std::memory_order_acquire))
        return E_INVALIDARG;
    return IsStopped() ? E_ABORT : S_OK;
}

std::uint64_t GpuCommandStream::ProductionEpochForWork() const noexcept
{
    return _scheduler && !IsOwner() ? _productionEpoch.load(std::memory_order_acquire) : 0;
}

bool GpuCommandStream::BelongsToOwner(const JobScheduler& scheduler, JobTicket source,
                                      std::uint64_t deviceGeneration) const noexcept
{
    return IsOwner() && _scheduler == &scheduler && _source == source && _deviceGeneration == deviceGeneration &&
           !_retirementAuthorized.load(std::memory_order_acquire);
}

HRESULT GpuCommandStream::PublishForWork(wil::com_ptr_nothrow<ID3D11CommandList> command,
                                         CancelToken cancellation) noexcept
{
    const auto thread = GetCurrentThreadId();
    if (!_scheduler || thread == _ownerThread || !command || _finished.load(std::memory_order_acquire))
        return E_INVALIDARG;
    std::uint32_t unset = 0;
    (void)_producerThread.compare_exchange_strong(unset, thread, std::memory_order_relaxed);
    if (_producerThread.load(std::memory_order_relaxed) != thread)
        return HRESULT_FROM_WIN32(ERROR_INVALID_THREAD_ID);
    // The registrations own no thread or allocation and join before this producer returns. Stop only
    // wakes capacity waits; it never acknowledges, destroys or refunds a submitted command graph.
    const auto stop = [event = _stopped.get()]() noexcept { (void)SetEvent(event); };
    std::stop_callback localStop{cancellation.LocalToken(), stop};
    std::stop_callback shutdownStop{cancellation.ShutdownToken(), stop};
    if (_produced == (std::numeric_limits<std::uint64_t>::max)())
        return HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW);
    const auto index = static_cast<std::size_t>(_produced % kCapacity);
    auto& slot = _slots[index];
    for (;;)
    {
        if (IsStopped())
            return E_ABORT;
        const auto state = slot.state.load(std::memory_order_acquire);
        if (state == State::Free || state == State::Acknowledged)
            break;
        if (!_backpressured.exchange(true, std::memory_order_acq_rel))
            _scheduler->NotifyOwnerWork(); // One changed-state witness, never an unchanged self-rearm.
        const HANDLE events[] = {_stopped.get(), _available.get()};
        const auto wait = WaitForMultipleObjects(2, events, FALSE, INFINITE);
        if (wait == WAIT_OBJECT_0)
            return E_ABORT;
        if (wait != WAIT_OBJECT_0 + 1)
            return wait == WAIT_FAILED ? HRESULT_FROM_WIN32(GetLastError()) : E_UNEXPECTED;
    }
    // The matching owner acknowledgment precedes this acquire. Release the old graph on the producer.
    slot.command.reset();
    slot.command = std::move(command);
    slot.lease = {_source, _deviceGeneration, ++_produced, index};
    slot.state.store(State::Ready, std::memory_order_release);
    _scheduler->NotifyOwnerWork();
    return S_OK;
}

void GpuCommandStream::FinishProducerForWork(HRESULT result) noexcept
{
    if (!_scheduler || IsOwner() || _finished.load(std::memory_order_relaxed))
        std::terminate();
    std::uint32_t unset = 0;
    (void)_producerThread.compare_exchange_strong(unset, GetCurrentThreadId(), std::memory_order_relaxed);
    if (_producerThread.load(std::memory_order_relaxed) != GetCurrentThreadId())
        std::terminate(); // Only this same producer can publish its terminal state, including before first batch.
    _result = result;
    _finished.store(true, std::memory_order_release);
    _scheduler->NotifyOwnerWork();
}

GpuCommandStream::Ready GpuCommandStream::NextOwner() const noexcept
{
    if (!IsOwner() || IsStopped() || _retirementAuthorized.load(std::memory_order_acquire))
        return {};
    const auto& slot = _slots[static_cast<std::size_t>(_consumed % kCapacity)];
    if (slot.state.load(std::memory_order_acquire) != State::Ready)
        return {};
    return {slot.command.get(), slot.lease};
}

bool GpuCommandStream::Matches(const Slot& slot, GpuCommandLease lease) const noexcept
{
    return lease.source == _source && lease.deviceGeneration == _deviceGeneration && lease.ordinal != 0 &&
           slot.lease == lease;
}

bool GpuCommandStream::BeginExecuteOwner(GpuCommandLease lease) noexcept
{
    if (!IsOwner() || IsStopped() || _retirementAuthorized.load(std::memory_order_acquire) || lease.slot >= kCapacity ||
        lease.ordinal != _consumed + 1)
        return false;
    auto& slot = _slots[lease.slot];
    if (slot.state.load(std::memory_order_acquire) != State::Ready || !Matches(slot, lease))
        return false;
    slot.state.store(State::Executing, std::memory_order_release);
    ++_consumed;
    return true;
}

bool GpuCommandStream::AcknowledgeOwner(GpuCommandLease lease) noexcept
{
    if (!IsOwner() || lease.slot >= kCapacity)
        return false;
    auto& slot = _slots[lease.slot];
    if (slot.state.load(std::memory_order_acquire) != State::Executing || !Matches(slot, lease))
        return false;
    slot.state.store(State::Acknowledged, std::memory_order_release);
    (void)SetEvent(_available.get());
    _scheduler->NotifyOwnerWork();
    return true;
}

void GpuCommandStream::RequestStopOwner() noexcept
{
    if (IsOwner())
        (void)SetEvent(_stopped.get());
}

bool GpuCommandStream::ProducerFinishedOwner(HRESULT& result) const noexcept
{
    if (!IsOwner() || !_finished.load(std::memory_order_acquire))
        return false;
    result = _result;
    return true;
}

bool GpuCommandStream::CanRetireOwner() const noexcept
{
    if (!IsOwner() || !_finished.load(std::memory_order_acquire))
        return false;
    for (const auto& slot : _slots)
    {
        const auto state = slot.state.load(std::memory_order_acquire);
        if (state == State::Executing || (state == State::Ready && !IsStopped()))
            return false;
    }
    return true;
}

bool GpuCommandStream::RetireForWork() noexcept
{
    if (!_scheduler || IsOwner() || !_retirementAuthorized.load(std::memory_order_acquire) ||
        _retired.load(std::memory_order_relaxed))
        return false;
    // The owner schedules this only after CanRetireOwner. No owner may execute another command afterwards.
    for (const auto& slot : _slots)
    {
        const auto state = slot.state.load(std::memory_order_acquire);
        if (state == State::Executing || (state == State::Ready && !IsStopped()))
            return false;
    }
    for (auto& slot : _slots)
    {
        slot.command.reset();
        slot.lease = {};
        slot.state.store(State::Free, std::memory_order_relaxed);
    }
    _retired.store(true, std::memory_order_release);
    return true;
}

bool GpuCommandStream::AuthorizeRetirementOwner() noexcept
{
    if (!CanRetireOwner() || _retirementAuthorized.load(std::memory_order_relaxed))
        return false;
    _retirementAuthorized.store(true, std::memory_order_release);
    return true;
}

HRESULT GpuCommandStream::RestartOwner() noexcept
{
    if (!IsOwner())
        return E_INVALIDARG;
    return RestartOwnerImpl(_deviceGeneration, false);
}

HRESULT GpuCommandStream::RestartDeviceOwner(const GpuWorkQueue& queue, const GpuCompletionContext& current) noexcept
{
    if (!IsOwner() || !_scheduler->InAcknowledgedJobCallback(_source) || !_queueOrigin ||
        _queueOrigin != current._origin)
        return E_INVALIDARG;
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* immediate = nullptr;
    const HRESULT borrowed = queue.BorrowCurrentContextOwner(current, device, immediate);
    if (FAILED(borrowed))
        return borrowed;
    // Borrow validates actual origin/device/context/generation without a query or AddRef. A new numeric
    // value alone never authorizes a replacement or acknowledges any original work.
    return RestartOwnerImpl(current.DeviceGeneration(), true);
}

HRESULT GpuCommandStream::RestartOwnerImpl(std::uint64_t nextGeneration, bool deviceReplacement) noexcept
{
    if (!IsOwner() || !_scheduler->InAcknowledgedJobCallback(_source) || !_retired.load(std::memory_order_acquire) ||
        !_finished.load(std::memory_order_acquire) || !_retirementAuthorized.load(std::memory_order_acquire) ||
        (deviceReplacement ? _consumed > _produced : _consumed != _produced))
        return E_INVALIDARG;
    const bool retryable = _result == S_OK || _result == E_ABORT || _result == DXGI_ERROR_DEVICE_REMOVED ||
                           _result == DXGI_ERROR_DEVICE_RESET || _result == DXGI_ERROR_DEVICE_HUNG ||
                           _result == DXGI_ERROR_DRIVER_INTERNAL_ERROR;
    if (deviceReplacement ? (!retryable || nextGeneration <= _deviceGeneration)
                          : (_result != S_OK || IsStopped() || nextGeneration != _deviceGeneration))
        return E_INVALIDARG;
    const auto epoch = _productionEpoch.load(std::memory_order_relaxed);
    if (_produced == (std::numeric_limits<std::uint64_t>::max)() ||
        epoch == (std::numeric_limits<std::uint64_t>::max)())
        return HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW);
    for (const auto& slot : _slots)
        if (slot.state.load(std::memory_order_acquire) != State::Free || slot.command)
            return E_UNEXPECTED;
    if (!ResetEvent(_available.get()) || !ResetEvent(_stopped.get()))
        return HRESULT_FROM_WIN32(GetLastError());
    // Actual worker retirement has destroyed every unsubmitted stopped slot. Skip those discarded
    // ordinals without acknowledging a GPU use; previously executing slots had to acknowledge first.
    // A late previous lease still fails its generation and ordinal checks.
    if (deviceReplacement)
        _consumed = _produced;
    _deviceGeneration = nextGeneration;
    _producerThread.store(0, std::memory_order_relaxed);
    _productionEpoch.store(epoch + 1, std::memory_order_release);
    _result = E_PENDING;
    _backpressured.store(false, std::memory_order_relaxed);
    _retirementAuthorized.store(false, std::memory_order_relaxed);
    _finished.store(false, std::memory_order_relaxed);
    _retired.store(false, std::memory_order_release);
    // Original source and monotonically increasing produced/consumed ordinals are unchanged.
    return S_OK;
}

bool GpuCommandStream::AcknowledgeEmptyJoinedShutdownOwner() noexcept
{
    if (!IsOwner() || !IsStopped() || _finished.load(std::memory_order_acquire) ||
        _producerThread.load(std::memory_order_acquire) != 0 || _retirementAuthorized.load(std::memory_order_acquire))
        return false;
    // External actual producer join is mandatory; no concurrent producer may claim/publish after this check.
    for (const auto& slot : _slots)
        if (slot.state.load(std::memory_order_acquire) != State::Free || slot.command)
            return false;
    _result = E_ABORT;
    _finished.store(true, std::memory_order_release);
    return true;
}
