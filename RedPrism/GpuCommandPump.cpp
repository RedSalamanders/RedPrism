#include "GpuCommandPump.h"

#include "OwnerWorkMultiplexer.h"

#include <exception>
#include <limits>
#include <utility>

GpuCommandPump::~GpuCommandPump()
{
    if (_scheduler || HasPending())
        std::terminate();
}

bool GpuCommandPump::IsOwner() const noexcept
{
    return _scheduler && _ownerThread == GetCurrentThreadId();
}

HRESULT GpuCommandPump::InitializeOwner(JobScheduler& scheduler, GpuWorkQueue& queue,
                                        OwnerWorkMultiplexer& routes) noexcept
{
    if (_ownerThread || !scheduler.WakeHandle())
        return E_INVALIDARG;
    if (!routes.Attach(*this))
        return HRESULT_FROM_WIN32(ERROR_NOT_ENOUGH_QUOTA);
    _ownerThread = GetCurrentThreadId();
    _scheduler = &scheduler;
    _queue = &queue;
    _routes = &routes;
    return S_OK;
}

HRESULT GpuCommandPump::AttachOwner(GpuCommandStream& stream, JobTicket ticket, GpuCommandSourceReceiver& receiver,
                                    GpuCommandSourceLease& empty, GpuCommandFailureDisposition failure) noexcept
{
    if ((failure != GpuCommandFailureDisposition::CancelOriginal &&
         failure != GpuCommandFailureDisposition::RetainOriginalForRecovery) ||
        !IsOwner() || _stopped || _inCallback || empty.generation ||
        !stream.BelongsToOwner(*_scheduler, ticket, _queue->DeviceGeneration()))
        return E_INVALIDARG;
    if (_nextGeneration == (std::numeric_limits<std::uint64_t>::max)())
        return HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW);
    for (const auto& existing : _sources)
        if (existing.stream == &stream)
            return E_INVALIDARG;
    std::size_t index = 0;
    while (index < kCapacity && _sources[index].stream)
        ++index;
    if (index == kCapacity)
        return HRESULT_FROM_WIN32(ERROR_NOT_ENOUGH_QUOTA);
    auto& source = _sources[index];
    const HRESULT captured = _queue->CaptureCompletionContext(source.captured);
    if (FAILED(captured))
        return captured;
    source.parent = this;
    source.stream = &stream;
    source.receiver = &receiver;
    source.ticket = ticket;
    source.failure = failure;
    source.generation = ++_nextGeneration;
    empty = {index, source.generation};
    return S_OK;
}

GpuCommandPump::Source* GpuCommandPump::FindOwner(GpuCommandSourceLease lease) noexcept
{
    if (!IsOwner() || _inCallback || lease.slot >= kCapacity || !lease.generation)
        return nullptr;
    auto& source = _sources[lease.slot];
    return source.stream && source.generation == lease.generation ? &source : nullptr;
}

void GpuCommandPump::FailOwner(Source& source, HRESULT error) noexcept
{
    source.error = FAILED(error) ? error : E_UNEXPECTED;
    source.stream->RequestStopOwner();
    if (source.failure == GpuCommandFailureDisposition::CancelOriginal)
        (void)_scheduler->Cancel(source.ticket);
    if (!source.failureReported)
    {
        source.failureReported = true;
        _inCallback = true;
        const auto leave = wil::scope_exit([&]() noexcept { _inCallback = false; });
        source.receiver->CommandSourceFailedOwner(source.ticket, source.error);
    }
}

bool GpuCommandPump::StopSourceOwner(GpuCommandSourceLease lease) noexcept
{
    if (auto* source = FindOwner(lease))
    {
        source->stream->RequestStopOwner();
        return true;
    }
    return false;
}

void GpuCommandPump::AcknowledgeOwner(Source& source, HRESULT result) noexcept
{
    const auto command = source.executing;
    if (!command.ordinal || !source.stream->AcknowledgeOwner(command))
        std::terminate();
    source.executing = {};
    source.waiting = false;
    source.quarantined = false;
    source.recoveryArmed = false;
    if (FAILED(result))
        FailOwner(source, result);
    _inCallback = true;
    const auto leave = wil::scope_exit([&]() noexcept { _inCallback = false; });
    source.receiver->CommandAcknowledgedOwner(source.ticket, command, result);
}

void GpuCommandPump::Source::GpuCompletionReady(JobTicket source, std::uint64_t deviceGeneration,
                                                HRESULT result) noexcept
{
    // Every admitted queue callback acknowledges its actual original stream, including stale E_ABORT.
    if (!parent || !parent->IsOwner() || !stream || !waiting || source != ticket ||
        deviceGeneration != captured.DeviceGeneration())
        std::terminate();
    parent->AcknowledgeOwner(*this, result);
}

bool GpuCommandPump::DetachOwner(GpuCommandSourceLease lease, GpuCompletionContext& empty) noexcept
{
    auto* source = FindOwner(lease);
    if (!source || empty || source->executing.ordinal || source->waiting || !source->stream->CanRetireOwner())
        return false;
    empty = std::move(source->captured);
    source->parent = nullptr;
    source->stream = nullptr;
    source->receiver = nullptr;
    source->ticket = {};
    source->generation = 0;
    source->error = S_OK;
    source->quarantined = false;
    source->recoveryArmed = false;
    source->failureReported = false;
    source->failure = GpuCommandFailureDisposition::CancelOriginal;
    _scheduler->NotifyOwnerWork(); // One capacity transition wakes an original suspended source.
    return true;
}

bool GpuCommandPump::HasSourceSlotOwner() const noexcept
{
    if (!IsOwner() || _stopped || _inCallback || _nextGeneration == (std::numeric_limits<std::uint64_t>::max)())
        return false;
    for (const auto& source : _sources)
        if (!source.stream)
            return true;
    return false;
}

bool GpuCommandPump::HasPending() const noexcept
{
    for (const auto& source : _sources)
        if (source.stream)
            return true;
    return false;
}

bool GpuCommandPump::HasReady() const noexcept
{
    if (!IsOwner() || _stopped || _inCallback)
        return false;
    for (const auto& source : _sources)
    {
        if (!source.stream || source.waiting)
            continue;
        if (source.quarantined)
        {
            if (source.recoveryArmed && _queue->HasRetirementCompletionSlot(source.captured))
                return true;
        }
        else if (source.stream->NextOwner().command && _queue->HasCompletionSlot())
            return true;
    }
    return false;
}

void GpuCommandPump::Pump() noexcept
{
    if (!HasReady())
        return;
    for (std::size_t offset = 0; offset < kCapacity; ++offset)
    {
        const auto index = (_next + offset) % kCapacity;
        auto& source = _sources[index];
        if (!source.stream || source.waiting)
            continue;
        if (source.quarantined)
        {
            if (!source.recoveryArmed || !_queue->HasRetirementCompletionSlot(source.captured))
                continue;
            source.recoveryArmed = false;
            _next = (index + 1) % kCapacity;
            const HRESULT status = source.captured.DeviceStatusOwner();
            if (FAILED(status))
            {
                AcknowledgeOwner(source, status); // Confirmed failure of this actual original device.
                return;
            }
            const HRESULT barrier = FAILED(_recoveryRefusal)
                                        ? std::exchange(_recoveryRefusal, S_OK)
                                        : _queue->EnqueueRetirementCompletion(source.captured, source.ticket, source);
            if (SUCCEEDED(barrier))
                source.waiting = true;
            // A healthy failed recovery remains charged, dormant, and quarantined until an actual device
            // change enables one attempt or the parent performs joined shutdown. No unchanged self-rearm.
            return;
        }
        const auto ready = source.stream->NextOwner();
        if (!ready.command || !_queue->HasCompletionSlot())
            continue;
        _next = (index + 1) % kCapacity;
        if (_queue->DeviceGeneration() != source.captured.DeviceGeneration())
        {
            FailOwner(source, E_ABORT);
            return;
        }
        const HRESULT status = source.captured.DeviceStatusOwner();
        if (FAILED(status))
        {
            FailOwner(source, status); // This command has not executed; no acknowledgement is fabricated.
            return;
        }
        wil::com_ptr_nothrow<ID3D11Device> recordedDevice;
        ready.command->GetDevice(recordedDevice.put());
        if (recordedDevice.get() != source.captured._device.get())
        {
            // A numeric queue generation is not device identity. The list still holds its actual device;
            // refusal keeps every unsubmitted command charged until the original producer joins and retires.
            FailOwner(source, E_INVALIDARG);
            return;
        }
        if (!source.stream->BeginExecuteOwner(ready.lease))
            std::terminate();
        source.executing = ready.lease;
        source.captured._context->ExecuteCommandList(ready.command, FALSE);
        const HRESULT barrier = FAILED(_completionRefusal) ? std::exchange(_completionRefusal, S_OK)
                                                           : _queue->EnqueueCompletion(source.ticket, source);
        if (SUCCEEDED(barrier))
            source.waiting = true;
        else
        {
            source.quarantined = true;
            source.recoveryArmed = true; // One explicit bounded recovery transition, not polling.
            FailOwner(source, barrier);
        }
        return;
    }
}

void GpuCommandPump::DeviceChangedOwner() noexcept
{
    if (!IsOwner() || _stopped || _inCallback)
        return;
    for (auto& source : _sources)
        if (source.stream)
        {
            FailOwner(source, E_ABORT);
            if (source.quarantined && !source.waiting)
                source.recoveryArmed = true;
        }
    _scheduler->NotifyOwnerWork();
}

void GpuCommandPump::StopOwner() noexcept
{
    if (!IsOwner() || _inCallback)
        return;
    _stopped = true;
    for (auto& source : _sources)
        if (source.stream)
            source.stream->RequestStopOwner();
}

void GpuCommandPump::AcknowledgeJoinedShutdownOwner() noexcept
{
    if (!IsOwner() || !_stopped || _inCallback)
        std::terminate();
    for (auto& source : _sources)
        if (source.stream && !AcknowledgeJoinedSourceOwner(source))
            std::terminate(); // Every started recording stage must have actually joined.
}

bool GpuCommandPump::AcknowledgeSourceJoinedShutdownOwner(GpuCommandSourceLease original) noexcept
{
    if (!IsOwner() || !_stopped || _inCallback)
        return false;
    auto* source = FindOwner(original);
    if (!source || !_scheduler->InJoinedRetirementCallback(source->ticket))
        return false;
    return AcknowledgeJoinedSourceOwner(*source);
}

bool GpuCommandPump::AcknowledgeJoinedSourceOwner(Source& source) noexcept
{
    HRESULT result = E_PENDING;
    if (!source.stream->ProducerFinishedOwner(result) && !source.stream->AcknowledgeEmptyJoinedShutdownOwner())
        return false;
    if (source.executing.ordinal)
    {
        if (!source.stream->AcknowledgeOwner(source.executing))
            return false;
        source.executing = {};
        source.waiting = false;
        source.quarantined = false;
        source.recoveryArmed = false;
        // Destruction only. The receiver may be a detached wrapper; never call it or publish useful results.
    }
    return true;
}

bool GpuCommandPump::CloseOwner() noexcept
{
    if (!IsOwner() || _inCallback || HasPending() || !_routes->Detach(*this))
        return false;
    _routes = nullptr;
    _queue = nullptr;
    _scheduler = nullptr;
    return true;
}

void GpuCommandPump::SetCompletionRefusalsForTest(HRESULT first, HRESULT recovery) noexcept
{
    if (IsOwner() && !_inCallback)
    {
        _completionRefusal = first;
        _recoveryRefusal = recovery;
    }
}

std::size_t GpuCommandPump::AttachedSourceCountOwnerForTest() const noexcept
{
    if (!IsOwner() || _inCallback)
        return 0;
    std::size_t count = 0;
    for (const auto& source : _sources)
        if (source.stream)
            ++count;
    return count;
}

std::size_t GpuCommandPump::QuarantinedSourceCountOwnerForTest() const noexcept
{
    if (!IsOwner() || _inCallback)
        return 0;
    std::size_t count = 0;
    for (const auto& source : _sources)
        if (source.stream && source.executing.ordinal && source.quarantined && !source.waiting)
            ++count;
    return count;
}
