#include "OriginalDisplayPublication.h"

#include <exception>
#include <limits>
#include <utility>
#include <windows.h>

namespace
{
JobResult Failure(HRESULT value) noexcept
{
    return {value == E_ABORT ? JobStatus::Cancelled : JobStatus::Failed, value};
}

bool Add(std::uint64_t& total, std::uint64_t bytes) noexcept
{
    if (bytes > (std::numeric_limits<std::uint64_t>::max)() - total)
        return false;
    total += bytes;
    return true;
}
} // namespace

OriginalDisplayPublication::OriginalDisplayPublication(std::uint32_t ownerThread, Bounds bounds, OwnerReady ownerReady,
                                                       void* context) noexcept
    : _ownerThread(ownerThread), _bounds(bounds), _ownerReady(ownerReady), _ownerReadyContext(context)
{
}

OriginalDisplayPublication::~OriginalDisplayPublication()
{
    if (!IsWorker() || _slot || _seed || _gpu || (_streamInitialized && !_transportRetired) ||
        ((_prepared || _rows || _device || _context || _source) && !_graphRetired))
        std::terminate();
}

bool OriginalDisplayPublication::IsOwner() const noexcept
{
    return _ownerThread && GetCurrentThreadId() == _ownerThread;
}

bool OriginalDisplayPublication::IsWorker() const noexcept
{
    return _ownerThread && GetCurrentThreadId() != _ownerThread;
}

HRESULT OriginalDisplayPublication::CaptureOwner(JobScheduler& jobs, JobTicket original, GpuWorkQueue& queue,
                                                 GpuCommandPump& commands, CommittedDisplayController& display,
                                                 const CommittedDisplayDescriptor& current) noexcept
{
    if (!IsOwner() || _jobs || !_bounds.cpuBytes || !_bounds.gpuBytes || !original.generation ||
        !jobs.InAcknowledgedJobCallback(original) || !current.key.document.documentSerial || !current.document ||
        !current.device || !current.immediate || current.pending ||
        current.key.purpose != DisplaySourcePurpose::Committed ||
        current.key.deviceGeneration != queue.DeviceGeneration())
        return E_INVALIDARG;
    _jobs = &jobs;
    _queue = &queue;
    _commands = &commands;
    _display = &display;
    _original = original;
    _software = current.software;
    _deviceGeneration = current.key.deviceGeneration;
    _documentSerial = current.key.document.documentSerial;
    if (!display.ClaimPublicationSlotOwner(original, _slot))
        return E_OUTOFMEMORY;
    const HRESULT result = queue.CaptureCompletionContext(_captured);
    if (FAILED(result))
        return result;
    _device = current.device;
    _context = current.immediate;
    (void)display.AcquirePublishedSeedOwner(current.key, _seed);
    _capturedOwner = true;
    return S_OK;
}

JobResult OriginalDisplayPublication::PrepareForWork(CanvasDocument final, RowInput rows, CancelToken cancellation)
{
    if (!IsWorker() || !_capturedOwner || _prepareAttempted || rows.documentSerial != _documentSerial ||
        !rows.firstItemId || final.layers.size() > (std::numeric_limits<std::uint64_t>::max)() - rows.firstItemId)
        return Failure(E_INVALIDARG);
    _prepareAttempted = true;
    if (cancellation.IsCancelled())
        return {JobStatus::Cancelled};
    RasterResidencyPlanOptions options;
    options.deviceGeneration = _deviceGeneration;
    const auto admission = RasterResidencyCpuAdmission::Estimate(final, options);
    const auto rowAdmission = LayerRowsAdmission::Estimate(final, rows.collapsed.size(), rows.hiddenPattern);
    const auto rowAllowance = PreparedLayerRows::OwnershipAllowance(final.layers.size());
    auto required = admission.CpuBytes();
    if (!admission.IsValid() || !rowAdmission.IsValid() || !rowAllowance || !Add(required, rowAdmission.Bytes()) ||
        !Add(required, rowAllowance) || !Add(required, sizeof(OriginalDisplayPublication)) ||
        required > _bounds.cpuBytes)
        return Failure(E_OUTOFMEMORY);
    _source.emplace(std::move(final));
    _previousRows = std::move(rows.previous);
    _prepared = PreparedCommittedDisplay::PrepareForWork(_ownerThread, _source.value(), admission, _bounds.cpuBytes,
                                                         _device.get(), _context.get(), _captured, cancellation);
    if (!_prepared)
        return Failure(cancellation.IsCancelled() ? E_ABORT : E_OUTOFMEMORY);
    const auto* manifest = _prepared->ManifestAfterJoin();
    if (!manifest || (_software && !Add(required, manifest->LogicalGpuBytes())) || required > _bounds.cpuBytes ||
        manifest->RequiredGpuBytes() > _bounds.gpuBytes)
        return Failure(E_OUTOFMEMORY);
    auto preparedRows = LayerRowsWork::Prepare(_source.value(), std::move(rows.collapsed),
                                               std::move(rows.hiddenPattern), rowAdmission, cancellation);
    auto rowBytes = rowAdmission.Bytes();
    if (!Add(rowBytes, rowAllowance))
        return Failure(E_OUTOFMEMORY);
    _rows = PreparedLayerRows::Create(std::move(preparedRows), rowBytes, rows.documentSerial, rows.firstItemId,
                                      _previousRows.get());
    return _rows ? JobResult{} : Failure(cancellation.IsCancelled() ? E_ABORT : E_OUTOFMEMORY);
}

HRESULT OriginalDisplayPublication::AdmitRecordingOwner() noexcept
{
    if (!IsOwner() || !_jobs || !_jobs->InAcknowledgedJobCallback(_original) || !_capturedOwner || !_prepareAttempted ||
        !_prepared || !_rows || _streamInitialized || FAILED(_failure) ||
        !_display->PublicationSlotCurrentOwner(_slot) || _queue->DeviceGeneration() != _deviceGeneration)
        return E_ABORT;
    const auto* manifest = _prepared->ManifestAfterJoin();
    if (!manifest || manifest->RequiredGpuBytes() > _bounds.gpuBytes)
        return E_OUTOFMEMORY;
    auto result =
        _display->ReservePublicationGpuOwner(_slot, _device.get(), _software, manifest->RequiredGpuBytes(), _gpu);
    if (FAILED(result))
        return result;
    result = _stream.InitializeOwner(*_jobs, _original, *_queue, _captured);
    if (FAILED(result))
        return result;
    _streamInitialized = true;
    result = _commands->AttachOwner(_stream, _original, *this, _commandSource);
    if (FAILED(result))
        FailOwner(result);
    return result;
}

bool OriginalDisplayPublication::HasRecorderOwner() const noexcept
{
    return IsOwner() && _streamInitialized;
}

JobResult OriginalDisplayPublication::RecordForWork(CancelToken cancellation) noexcept
{
    if (!IsWorker() || !_streamInitialized || !_prepared || _producerFinished ||
        _commandSource.slot >= GpuCommandPump::kCapacity)
        return Failure(E_UNEXPECTED);
    const HRESULT result = _prepared->RecordForWork(_stream, cancellation, _seed.GraphForWork());
    return FAILED(result) ? Failure(result) : JobResult{};
}

void OriginalDisplayPublication::FinishRecordingForWork(JobResult result) noexcept
{
    if (!IsWorker() || !_streamInitialized || _producerFinished)
        std::terminate();
    _stream.FinishProducerForWork(result.status == JobStatus::Finished    ? S_OK
                                  : result.status == JobStatus::Cancelled ? E_ABORT
                                                                          : result.error);
    _producerFinished = true;
}

bool OriginalDisplayPublication::RecordingJoinedOwner() noexcept
{
    // Destruction-only original parent hook also runs after failed/cancelled worker stages.
    // Its actual joined callback, recorded phase and real transport acknowledgements remain required.
    if (!IsOwner() || !_jobs || !_jobs->InUiCallback() || !_producerFinished || _recordingJoined)
        return false;
    _recordingJoined = true;
    if (_seed && !_display->ReleasePublishedSeedOwner(_seed))
        std::terminate();
    SignalOwnerReady();
    return true;
}

bool OriginalDisplayPublication::TransportReadyOwner() const noexcept
{
    return IsOwner() && _streamInitialized && _recordingJoined && !_transportAuthorized && _stream.CanRetireOwner();
}

bool OriginalDisplayPublication::AuthorizeTransportRetirementOwner() noexcept
{
    if (!TransportReadyOwner())
        return false;
    if (_commandSource.slot < GpuCommandPump::kCapacity)
    {
        if (!_commands->DetachOwner(_commandSource, _transportCapture))
            return false;
        _commandSource = {};
    }
    if (!_stream.AuthorizeRetirementOwner())
        std::terminate();
    _transportAuthorized = true;
    return true;
}

bool OriginalDisplayPublication::RetireTransportForWork() noexcept
{
    if (!IsWorker() || !_transportAuthorized || _transportRetired || !_stream.RetireForWork())
        return false;
    _transportCapture = {};
    _transportRetired = true;
    return true;
}

HRESULT OriginalDisplayPublication::PreparePublicationOwner(
    const std::shared_ptr<const SharedPublicationMemory>& memory, const CanvasDocument& final) noexcept
{
    if (!IsOwner() || !_jobs || !_jobs->InAcknowledgedJobCallback(_original) || !_transportRetired ||
        _publicationReady || _detached || !_prepared || !_rows || !memory || !memory->BoundTo(*_jobs, _original) ||
        _queue->DeviceGeneration() != _deviceGeneration || FAILED(_failure))
        return FAILED(_failure) ? _failure : E_ABORT;
    if (!_prepared->BindMemoryOwner(memory) || FAILED(_rows->BindPublicationMemory(memory)) ||
        !_rows->SealForPublication())
        return E_INVALIDARG;
    const HRESULT result = _prepared->PublishOwner();
    if (FAILED(result))
        return result;
    if (!_display->CanAdoptPublicationOwner(_slot, *_prepared, _gpu, final))
        return E_ABORT;
    _publicationReady = true;
    return S_OK;
}

bool OriginalDisplayPublication::AdoptOwner(std::shared_ptr<const PreparedLayerRows>& empty, RowsAdopted rowsAdopted,
                                            void* context) noexcept
{
    if (!IsOwner() || !_jobs || !_jobs->InAcknowledgedJobCallback(_original) || !_publicationReady || _adopted ||
        empty || !_rows)
        return false;
    empty = std::move(_rows);
    if (rowsAdopted)
        rowsAdopted(context);
    // Rows precede the display client's publication callback. PreparePublicationOwner prevalidated
    // the exact graph before the caller's checked CPU/history swap, with no intervening notification.
    if (!_display->AdoptPublicationOwner(_slot, *_prepared, _gpu))
        std::terminate();
    _adopted = true;
    return true;
}

void OriginalDisplayPublication::StopOwner() noexcept
{
    if (!IsOwner())
        std::terminate();
    FailOwner(E_ABORT);
    if (_streamInitialized && !_transportRetired)
    {
        _stream.RequestStopOwner();
        if (_commandSource.slot < GpuCommandPump::kCapacity)
            (void)_commands->StopSourceOwner(_commandSource);
    }
}

bool OriginalDisplayPublication::DetachForRetirementOwner() noexcept
{
    // Destruction-only original parent hook also runs after failed/cancelled worker stages.
    // Its actual joined callback, recorded phase and real transport acknowledgements remain required.
    if (!IsOwner() || (_jobs && !_jobs->InUiCallback()) || _detached || (_streamInitialized && !_transportRetired))
        return false;
    if (_prepared && !_adopted && !_prepared->DetachOwner())
        return false;
    if (_seed && !_display->ReleasePublishedSeedOwner(_seed))
        return false;
    _detached = true;
    return true;
}

bool OriginalDisplayPublication::RetireGraphForWork() noexcept
{
    if (!IsWorker() || !_detached || _graphRetired || (_streamInitialized && !_transportRetired) ||
        (_prepared && !_adopted && !_prepared->RetireForWork()))
        return false;
    _prepared.reset();
    _rows.reset();
    _previousRows.reset();
    _source.reset();
    _captured = {};
    _context.reset();
    _device.reset();
    _graphRetired = true;
    return true;
}

bool OriginalDisplayPublication::FinishRetirementOwner() noexcept
{
    // Destruction-only original parent hook also runs after failed/cancelled worker stages.
    // Its actual joined callback, recorded phase and real transport acknowledgements remain required.
    if (!IsOwner() || !_jobs || !_jobs->InUiCallback() || !_graphRetired || _seed ||
        (_streamInitialized && !_transportRetired))
        return false;
    _gpu.ResetOwner();
    return !_slot || _display->ReleasePublicationSlotOwner(_slot);
}

bool OriginalDisplayPublication::JoinedShutdownOwner(GpuResidencyReservation& empty) noexcept
{
    if (!IsOwner() || !_jobs || !_jobs->InJoinedRetirementCallback(_original) || _shutdownJoined || empty)
        return false;
    if (_streamInitialized && !_transportRetired)
    {
        _stream.RequestStopOwner();
        HRESULT result = E_PENDING;
        if (!_stream.ProducerFinishedOwner(result) && !_stream.AcknowledgeEmptyJoinedShutdownOwner())
            return false;
        if (_commandSource.slot < GpuCommandPump::kCapacity)
        {
            if (!_commands->AcknowledgeSourceJoinedShutdownOwner(_commandSource) ||
                !_commands->DetachOwner(_commandSource, _transportCapture))
                return false;
            _commandSource = {};
        }
        if (!_transportAuthorized && !_stream.AuthorizeRetirementOwner())
            return false;
        _transportAuthorized = true;
    }
    if (_prepared && !_adopted && !_detached && !_prepared->DetachOwner())
        return false;
    if (_seed && !_display->ReleasePublishedSeedOwner(_seed))
        return false;
    if (_slot && !_display->ReleasePublicationSlotOwner(_slot))
        return false;
    empty = std::move(_gpu);
    _detached = true;
    _shutdownJoined = true;
    return true;
}

bool OriginalDisplayPublication::RetireJoinedShutdownForWork() noexcept
{
    if (!IsWorker() || !_shutdownJoined)
        return false;
    if (_streamInitialized && !_transportRetired && !RetireTransportForWork())
        return false;
    return _graphRetired || RetireGraphForWork();
}

JobTicket OriginalDisplayPublication::OriginalOwner() const noexcept
{
    return IsOwner() && _jobs ? _original : JobTicket{};
}

HRESULT OriginalDisplayPublication::FailureOwner() const noexcept
{
    return IsOwner() ? _failure : E_UNEXPECTED;
}

const CanvasDocument* OriginalDisplayPublication::SourceAfterJoin() const noexcept
{
    return IsOwner() && _jobs && _jobs->InAcknowledgedJobCallback(_original) && _source ? &_source.value() : nullptr;
}

bool OriginalDisplayPublication::AdoptedOwner() const noexcept
{
    return IsOwner() && _adopted;
}

void OriginalDisplayPublication::FailOwner(HRESULT result) noexcept
{
    if (!IsOwner() || SUCCEEDED(result))
        std::terminate();
    if (SUCCEEDED(_failure))
        _failure = result;
    SignalOwnerReady();
}

void OriginalDisplayPublication::SignalOwnerReady() noexcept
{
    if (!IsOwner())
        std::terminate();
    if (_jobs)
        _jobs->NotifyOwnerWork();
    if (_ownerReady)
        _ownerReady(_ownerReadyContext);
}

void OriginalDisplayPublication::CommandAcknowledgedOwner(JobTicket source, GpuCommandLease command,
                                                          HRESULT result) noexcept
{
    if (!IsOwner() || source != _original || command.source != _original)
        std::terminate();
    if (FAILED(result))
        FailOwner(result);
    else
        SignalOwnerReady();
}

void OriginalDisplayPublication::CommandSourceFailedOwner(JobTicket source, HRESULT result) noexcept
{
    if (source != _original)
        std::terminate();
    FailOwner(result);
}
