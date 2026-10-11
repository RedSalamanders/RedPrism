#include "RecordedDocumentReadbackJob.h"

#include "DeferredRelease.h"
#include "RecordedUtilityGraph.h"
#include "SharedPublicationMemory.h"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <exception>
#include <limits>
#include <type_traits>
#include <utility>

namespace
{
bool RetryableUtilityFailure(HRESULT result) noexcept
{
    return result == E_ABORT || result == DXGI_ERROR_DEVICE_REMOVED || result == DXGI_ERROR_DEVICE_RESET ||
           result == DXGI_ERROR_DEVICE_HUNG || result == DXGI_ERROR_DRIVER_INTERNAL_ERROR;
}
HRESULT UtilityResultError(JobResult result) noexcept
{
    return result.status == JobStatus::Cancelled ? E_ABORT : result.status == JobStatus::Failed ? result.error : S_OK;
}
HRESULT QueryUtilitySoftwareDeviceOwner(ID3D11Device* device, bool& software) noexcept
{
    software = false;
    wil::com_ptr_nothrow<IDXGIDevice> dxgi;
    RETURN_IF_FAILED(device->QueryInterface(IID_PPV_ARGS(dxgi.put())));
    wil::com_ptr_nothrow<IDXGIAdapter> adapter;
    RETURN_IF_FAILED(dxgi->GetAdapter(adapter.put()));
    wil::com_ptr_nothrow<IDXGIAdapter1> adapter1;
    RETURN_IF_FAILED(adapter->QueryInterface(IID_PPV_ARGS(adapter1.put())));
    DXGI_ADAPTER_DESC1 description{};
    RETURN_IF_FAILED(adapter1->GetDesc1(&description));
    software = (description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0;
    return S_OK;
}
} // namespace

#pragma warning(push)
#pragma warning(disable : 4324) // The intrusive release node requires 16-byte alignment.
struct RecordedDocumentReadbackJob::Root final : DeferredReleaseNode, GpuReadbackReceiver, GpuReadbackProducer
{
    // Declare accounting first: its last reference dies after every contained source/output/device owner.
    std::shared_ptr<SharedPublicationMemory> memory;
    CanvasDocument originalDocument;
    CanvasDocument document;
    PixelBuffer pixels;
    Raster image;
    std::unique_ptr<RecordedUtilityGraph> graph;
    GpuCommandStream stream;
    GpuCompletionContext captured;
    GpuCompletionContext retiredCapture;
    wil::com_ptr_nothrow<ID3D11Device> device;
    const std::uint32_t ownerThread;
    RecordedDocumentReadbackJob* parent;
    bool streamInitialized = false;
    bool streamRetired = false;
    bool graphExcluded = false;

    Root(std::shared_ptr<SharedPublicationMemory> accounting, std::uint64_t allowance, std::uint32_t owner,
         RecordedDocumentReadbackJob& original) noexcept
        : memory(std::move(accounting)), ownerThread(owner), parent(&original)
    {
        bytes = allowance;
        destroy = [](DeferredReleaseNode* node) noexcept
        {
            auto* root = static_cast<Root*>(node);
            if (GetCurrentThreadId() == root->ownerThread)
                std::terminate();
            if (root->streamInitialized && !root->streamRetired)
            {
                if (!root->stream.RetireForWork())
                    std::terminate();
                root->streamRetired = true;
            }
            if (root->graph)
            {
                const bool retired = root->graph->RetireForWork() || root->graph->RetireUnpublishedForWork();
                if (!retired)
                    std::terminate();
                root->graph.reset();
            }
            const std::unique_ptr<Root> owned(root);
        };
    }
    void ReadbackReady(JobTicket ticket, GpuReadbackRows rows, HRESULT result) noexcept override
    {
        if (!parent)
            std::terminate(); // Queue notifications joined before wrapper detachment.
        parent->ReadbackReady(ticket, rows, result);
    }
    HRESULT RecordReadbackSource(ID3D11Texture2D*& source, D3D11_BOX& box) noexcept override
    {
        if (!parent)
            std::terminate();
        return parent->RecordReadbackSource(source, box);
    }
};

#pragma warning(pop)

RecordedDocumentReadbackJob::RecordedDocumentReadbackJob(JobScheduler& scheduler, GpuWorkQueue& queue,
                                                         GpuCommandPump& commands, UtilityReadbackOwnerService& owners,
                                                         GpuResidencyBudget& gpuBudget,
                                                         UtilityReadbackAdmission::Envelope envelope,
                                                         GpuResidencyReservation originalGpuCharge, D3D11_BOX region,
                                                         bool software,
                                                         RecordedReadbackCaptureKind captureKind) noexcept
    : _scheduler(scheduler), _queue(queue), _commands(commands), _owners(owners), _gpuBudget(gpuBudget),
      _envelope(envelope), _gpuCharge(std::move(originalGpuCharge)), _captureKind(captureKind),
      _ownerThread(GetCurrentThreadId()), _cpuAdmissionBytes(envelope.cpuBytes), _region(region),
      _softwareMemoryAdmitted(software)
{
}

RecordedDocumentReadbackJob::~RecordedDocumentReadbackJob()
{
    assert(IsOwner());
    if (_serviceAttached || _sourceAttached || _rows.data || _graphPublished ||
        (!_graphicsRetired && !_joinedDestruction))
        std::terminate();
    if (_root)
    {
        // The actual application Close hook closes queued copy/map owners before the final MTA drain.
        // Root survives wrapper destruction, including the original stream's receiver lifetime exclusion.
        auto* root = std::exchange(_root, nullptr);
        root->parent = nullptr;
        if (!TryDeferRelease(*root, DeferredReleasePolicy::AlwaysOnOwner))
            std::terminate();
    }
}

bool RecordedDocumentReadbackJob::IsOwner() const noexcept
{
    return GetCurrentThreadId() == _ownerThread;
}

bool RecordedDocumentReadbackJob::ReadbackGraphicsRetiredOwner() const noexcept
{
    return IsOwner() && _root && _graphicsRetired && !_graphPrepared && !_graphPublished && !_sourceAttached &&
           !_rows.data && !_gpuCharge && !_root->graph && !_root->captured && !_root->retiredCapture &&
           !_root->device && (!_streamInitialized || _streamRetired);
}

std::uint64_t RecordedDocumentReadbackJob::RequiredMemory(D3D11_BOX region) noexcept
{
    if (region.front != 0 || region.back != 1 || region.right <= region.left || region.bottom <= region.top ||
        region.right - region.left > kMaxCanvasSide || region.bottom - region.top > kMaxCanvasSide)
        return 0;
    const auto bytes = static_cast<std::uint64_t>(region.right - region.left) * (region.bottom - region.top) * 4;
    return bytes <= (std::numeric_limits<std::size_t>::max)()
               ? PixelBuffer::RequiredBytes(static_cast<std::size_t>(bytes))
               : 0;
}

bool RecordedDocumentReadbackJob::ValidRegion() const noexcept
{
    return _root && _root->document.width > 0 && _root->document.height > 0 &&
           _root->document.width <= kMaxCanvasSide && _root->document.height <= kMaxCanvasSide &&
           RequiredMemory(_region) && _region.right <= static_cast<unsigned int>(_root->document.width) &&
           _region.bottom <= static_cast<unsigned int>(_root->document.height) && _envelope.tileSide &&
           _envelope.tileSide <= GpuWorkQueue::kMaxReadbackSide;
}

bool RecordedDocumentReadbackJob::LastRegion() const noexcept
{
    return _x + _width == _region.right - _region.left && _y + _height == _region.bottom - _region.top;
}

D3D11_BOX RecordedDocumentReadbackJob::CurrentRegion() const noexcept
{
    return {_region.left + _x, _region.top + _y, 0, _region.left + _x + _width, _region.top + _y + _height, 1};
}

JobResult RecordedDocumentReadbackJob::Run(JobContext& context)
{
    if (_phase == Phase::Opening)
    {
        if (!context.deferredRetirement || context.reservedMemoryBytes < _envelope.cpuBytes || !_envelope.cpuBytes ||
            !RequiredMemory(_region) ||
            (_captureKind != RecordedReadbackCaptureKind::Gpu &&
             _captureKind != RecordedReadbackCaptureKind::CpuOnly) ||
            (_captureKind == RecordedReadbackCaptureKind::Gpu && !_envelope.gpuBytes))
            return {JobStatus::Failed, E_INVALIDARG};
        // This accounting node must exist before Root or any source/device/output allocation.
        _memory = SharedPublicationMemory::CreateForWork();
        _root = std::make_unique<Root>(_memory, _envelope.cpuBytes, _ownerThread, *this).release();
        return PrepareReadbackOpening(context);
    }
    if (!_root)
        return {JobStatus::Failed, E_UNEXPECTED};
    if (_phase == Phase::Preparing)
    {
        if (_resetOutput)
        {
            ResetReadback();
            _root->image = {};
            _root->pixels = {};
            _root->document = _root->originalDocument;
            _resetOutput = false;
        }
        if (!ValidRegion() || context.reservedMemoryBytes < _cpuAdmissionBytes ||
            context.reservedMemoryBytes < RequiredMemory(_region))
            return {JobStatus::Failed, E_INVALIDARG};
        const auto prepared = PrepareReadbackSnapshot(_root->document, context);
        if (prepared.status != JobStatus::Finished)
            return prepared;
        if (context.cancellation.IsCancelled())
            return {JobStatus::Cancelled};
        const auto size = static_cast<std::size_t>(_region.right - _region.left) * (_region.bottom - _region.top) * 4;
        if (!_root->pixels)
            _root->pixels = PixelBuffer::TryAllocate(size);
        return _root->pixels ? JobResult{} : JobResult{JobStatus::Failed, E_OUTOFMEMORY};
    }
    if (_phase == Phase::Recording)
    {
        if (FAILED(_recordingFailure))
            return {JobStatus::Failed, _recordingFailure};
        if (!_root->graph)
            _root->graph = std::make_unique<RecordedUtilityGraph>(_ownerThread);
        const HRESULT recorded =
            !_graphPrepared ? _root->graph->PrepareForWork(_root->device.get(), _root->document, CurrentRegion(),
                                                           _root->stream, context.cancellation)
                            : _root->graph->RecordNextForWork(CurrentRegion(), _root->stream, context.cancellation);
        return FAILED(recorded) ? JobResult{JobStatus::Failed, recorded} : JobResult{};
    }
    if (_phase == Phase::Copying)
    {
        if (FAILED(_attemptFailure))
            return {}; // Owner decides recovery only after the actual failed-copy acknowledgement.
        if (!_rows.data || _rows.width != _width || _rows.height != _height || !_root->pixels)
            return {JobStatus::Failed, E_UNEXPECTED};
        const auto stride = static_cast<std::size_t>(_region.right - _region.left) * 4;
        for (unsigned int y = 0; y < _height; ++y)
        {
            if (context.cancellation.IsCancelled())
                return {JobStatus::Cancelled};
            std::memcpy(_root->pixels.Data() + static_cast<std::size_t>(_y + y) * stride + _x * 4,
                        _rows.data + static_cast<std::size_t>(y) * _rows.rowPitch,
                        static_cast<std::size_t>(_width) * 4);
        }
        _copiedPixels += static_cast<std::uint64_t>(_width) * _height;
        context.ReportProgress(static_cast<unsigned int>(
            _copiedPixels * 100 /
            (static_cast<std::uint64_t>(_region.right - _region.left) * (_region.bottom - _region.top))));
        if (LastRegion())
        {
            _root->image = Raster::Adopt(static_cast<int>(_region.right - _region.left),
                                         static_cast<int>(_region.bottom - _region.top), RasterFormat::Bgra8888,
                                         std::move(_root->pixels), _memory);
            if (!_root->image.HasStorage())
                return {JobStatus::Failed, E_UNEXPECTED};
            if (!ProcessReadbackDetached())
                return ProcessReadback(_root->image, context);
        }
        return {};
    }
    if (_phase == Phase::Detached)
        return ProcessReadback(_root->image, context);
    return {}; // Actual admitted Compute acknowledgement; no source/graphics work in this body.
}

void RecordedDocumentReadbackJob::RetireGraphicsForWork() noexcept
{
    if (!_root || IsOwner())
        std::terminate();
    if (_root->graph)
    {
        const bool retired = _root->graph->RetireForWork() || _root->graph->RetireUnpublishedForWork();
        if (!retired)
            std::terminate();
        _root->graph.reset();
        _root->graphExcluded = false;
    }
    _root->captured = {};
    _root->retiredCapture = {};
    _root->device.reset();
}

void RecordedDocumentReadbackJob::RetireWorkerStage(JobContext& context, JobResult result) noexcept
{
    // The service is not attached until Opening acknowledges. Keep this token immutable afterwards:
    // reading cancellation from the owner while the recorder runs must not race a wrapper assignment.
    if (_phase == Phase::Opening)
        _cancellation = context.cancellation;
    if (!_root)
        return;
    if (_phase == Phase::Recording)
        _root->stream.FinishProducerForWork(UtilityResultError(result));
    else if (_phase == Phase::RetireTransport)
    {
        if (!_root->stream.RetireForWork())
            std::terminate();
        _root->streamRetired = true;
        _root->retiredCapture = {}; // Captured command source references die on Compute.
    }
    else if (_phase == Phase::RetireGraphics)
        RetireGraphicsForWork();
}

void RecordedDocumentReadbackJob::ResumeOwner(Phase next, JobExecutionLane lane) noexcept
{
    assert(IsOwner());
    if (next == Phase::Recording)
        _recordingFailure = FAILED(_attemptFailure) ? _attemptFailure : UtilityResultError(_terminal);
    if (next == Phase::Detached)
        _detachedReady = false;
    _phase = next;
    if (!_scheduler.Resume(_ticket, lane))
        std::terminate();
}

void RecordedDocumentReadbackJob::RememberFailureOwner(JobResult result) noexcept
{
    if (result.status == JobStatus::Finished)
        return;
    const HRESULT error = UtilityResultError(result);
    if (SUCCEEDED(_attemptFailure))
        _attemptFailure = error;
    if (_terminal.status == JobStatus::Finished &&
        (result.status == JobStatus::Cancelled || !RetryableUtilityFailure(error)))
        _terminal = result;
}

void RecordedDocumentReadbackJob::ReleaseRowsOwner() noexcept
{
    if (_rows.data && !_queue.ReleaseMapped(_rows.lease))
        std::terminate();
    _rows = {};
    _rowsReady = false;
}

void RecordedDocumentReadbackJob::ExcludeGraphOwner() noexcept
{
    if (!_graphPublished)
        return;
    if (!_root || !_root->graph || !_root->graph->ExcludeRegionOwner())
        std::terminate();
    _root->graphExcluded = true;
    _graphPublished = false;
}

void RecordedDocumentReadbackJob::BeginGraphicsRetirementOwner(AfterGraphics next) noexcept
{
    assert(!_sourceAttached && !_rows.data);
    ExcludeGraphOwner();
    _afterGraphics = next;
    if (_root)
        ResumeOwner(Phase::RetireGraphics);
    else
        _phase = Phase::Complete;
}

HRESULT RecordedDocumentReadbackJob::ReserveCurrentGraphicsOwner(ID3D11Device* current) noexcept
{
    GpuCapabilities::VideoMemorySnapshot snapshot;
    RETURN_IF_FAILED(GpuCapabilities::QueryVideoMemorySnapshot(current, snapshot));
    const auto& segment =
        _deviceSoftware && snapshot.nonLocal.available && snapshot.nonLocal.budget ? snapshot.nonLocal : snapshot.local;
    if (!segment.available || !segment.budget)
        return E_OUTOFMEMORY;
    const GpuResidencySnapshot fresh{_queue.DeviceGeneration(), segment.budget, segment.currentUsage};
    GpuResidencyAdmission decision;
    if (_gpuCharge)
    {
        if (!_gpuCharge.BelongsTo(_gpuBudget) || _gpuCharge.Bytes() != _envelope.gpuBytes)
            return E_INVALIDARG;
        if (_gpuCharge.DeviceGeneration() == fresh.deviceGeneration)
        {
            // Equal-size Increase is intentionally a no-op. Independently validate this fresh real budget
            // before the first graphics allocation, including every overlapping declared reservation.
            const auto headroom = (std::max)(GpuResidencyBudget::kMinimumHeadroom, fresh.budgetBytes / 8);
            if (fresh.currentUsageBytes > fresh.budgetBytes || headroom > fresh.budgetBytes - fresh.currentUsageBytes ||
                _gpuBudget.ReservedOwner() > fresh.budgetBytes - fresh.currentUsageBytes - headroom)
                return E_OUTOFMEMORY;
            return _gpuBudget.IncreaseOwner(_gpuCharge, _envelope.gpuBytes, fresh, decision) ? S_OK : E_OUTOFMEMORY;
        }
        if (_root->graph || (_streamInitialized && !_streamRetired))
            return E_ABORT;
        _gpuCharge.ResetOwner(); // Original has no resource users, or actual graph retirement acknowledged.
    }
    _gpuCharge = _gpuBudget.ReserveOwner(_envelope.gpuBytes, fresh, decision);
    return _gpuCharge ? S_OK : E_OUTOFMEMORY;
}

HRESULT RecordedDocumentReadbackJob::CaptureCurrentGraphicsOwner(bool recovery) noexcept
{
    if (!_root || _root->captured || _root->device || !_graphicsRetired)
        return E_UNEXPECTED;
    RETURN_IF_FAILED(PrepareReadbackDevice(recovery ? _attemptFailure : S_OK));
    RETURN_IF_FAILED(_queue.CaptureCompletionContext(_root->captured));
    _graphicsRetired = false; // Retain even a partially captured opening through actual worker destruction.
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* immediate = nullptr;
    RETURN_IF_FAILED(_queue.BorrowCurrentContextOwner(_root->captured, device, immediate));
    RETURN_IF_FAILED(QueryUtilitySoftwareDeviceOwner(device, _deviceSoftware));
    if (_deviceSoftware && !_softwareMemoryAdmitted)
    {
        if (_envelope.gpuBytes > (std::numeric_limits<std::uint64_t>::max)() - _envelope.cpuBytes)
            return HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW);
        const auto total = _envelope.cpuBytes + _envelope.gpuBytes;
        // Fresh actual physical/commit admission before a replacement WARP graph creates CPU-backed resources.
        // This is the same acknowledged ticket/retained slot; refusal preserves the original reservation.
        RETURN_IF_FAILED(static_cast<HRESULT>(_scheduler.IncreaseReservation(_ticket, total)));
        _cpuAdmissionBytes = total;
        _root->bytes = total;
        _softwareMemoryAdmitted = true;
    }
    RETURN_IF_FAILED(ReserveCurrentGraphicsOwner(device));
    _root->device = device;
    if (recovery && _streamInitialized)
    {
        RETURN_IF_FAILED(_root->stream.RestartDeviceOwner(_queue, _root->captured));
        _streamRetired = _root->streamRetired = false;
        _transportAuthorized = false;
    }
    _deviceGeneration = _root->captured.DeviceGeneration();
    _graphicsRetired = false;
    return S_OK;
}

void RecordedDocumentReadbackJob::BeginFailureRetirementOwner() noexcept
{
    if (_cancellation.IsCancelled())
        _terminal = {JobStatus::Cancelled};
    if (_sourceAttached)
    {
        if (!_stopOffered && !_commands.StopSourceOwner(_source))
            std::terminate();
        _stopOffered = true;
        // An original healthy quarantine stays dormant until its actual event/device/Close acknowledgement.
        // Recording's worker acknowledgement changes Recording to AwaitCommands before transport retirement.
        _scheduler.NotifyOwnerWork();
        return;
    }
    if (_streamInitialized && !_streamRetired)
    {
        _root->stream.RequestStopOwner();
        // A failed attach has no admitted command receiver. Dispatch the empty original producer stage so
        // RetireWorkerStage actually publishes its terminal state, even when cancellation skips Run.
        ResumeOwner(Phase::Recording, JobExecutionLane::GpuRecording);
        return;
    }
    const bool beforeDetached = _phase != Phase::Detached && _phase != Phase::Complete;
    BeginGraphicsRetirementOwner((_terminal.status == JobStatus::Finished && beforeDetached &&
                                  RetryableUtilityFailure(_attemptFailure) && _restarts < 2)
                                     ? AfterGraphics::Recover
                                     : AfterGraphics::Complete);
}

void RecordedDocumentReadbackJob::DetachServiceOwner() noexcept
{
    if (!_serviceAttached)
        return;
    if (!_owners.DetachOwner(_ticket, *this))
        std::terminate();
    _serviceAttached = false;
}

void RecordedDocumentReadbackJob::FinishOwner(JobResult result) noexcept
{
    if (_terminal.status == JobStatus::Finished && result.status != JobStatus::Finished)
        _terminal = result;
    if (_terminal.status == JobStatus::Finished && FAILED(_attemptFailure))
        _terminal = {JobStatus::Failed, _attemptFailure};
    _phase = Phase::Complete;
    _detachedReady = false;
    if (!_graphicsRetired || _sourceAttached || _rows.data)
        std::terminate();
    _gpuCharge.ResetOwner();
    DetachServiceOwner();
}

bool RecordedDocumentReadbackJob::DeferRetirement(JobResult result, JobTicket original) noexcept
{
    if (!IsOwner() || (_ticket.generation && _ticket != original))
        std::terminate();
    _ticket = original;
    RememberFailureOwner(result);
    if (_phase == Phase::Opening)
    {
        const bool gpuAdmission =
            _captureKind == RecordedReadbackCaptureKind::Gpu
                ? _gpuCharge && _gpuCharge.BelongsTo(_gpuBudget) && _gpuCharge.Bytes() == _envelope.gpuBytes
                : !_gpuCharge;
        if (!_root || result.status != JobStatus::Finished || !gpuAdmission)
        {
            if (result.status == JobStatus::Finished)
                _terminal = {JobStatus::Failed, E_INVALIDARG};
            if (_root)
            {
                BeginGraphicsRetirementOwner(AfterGraphics::Complete);
                return true;
            }
            FinishOwner(result);
            return false;
        }
        if (!_owners.AttachOwner(original, *this))
        {
            _terminal = {JobStatus::Failed, E_OUTOFMEMORY};
            BeginGraphicsRetirementOwner(AfterGraphics::Complete);
            return true;
        }
        _serviceAttached = true;
        const HRESULT captured = CaptureReadbackDocument(_root->originalDocument);
        if (FAILED(captured))
        {
            _terminal = {JobStatus::Failed, captured};
            _attemptFailure = captured;
            BeginGraphicsRetirementOwner(AfterGraphics::Complete);
        }
        else if (captured == S_FALSE)
        {
            _gpuCharge.ResetOwner(); // No graphics source was captured or created.
            ResumeOwner(Phase::Detached);
        }
        else if (_captureKind == RecordedReadbackCaptureKind::CpuOnly)
        {
            _terminal = {JobStatus::Failed, E_INVALIDARG}; // Explicit CPU-only descriptor cannot capture a GPU route.
            BeginGraphicsRetirementOwner(AfterGraphics::Complete);
        }
        else
        {
            const auto bounds = _root->originalDocument.layers.OwnershipBounds();
            if (_root->originalDocument.RetirementMemoryBytes() > _envelope.sourceBytes ||
                (!bounds && _root->originalDocument.layers.Identity()) ||
                (bounds && bounds->rasterNodes > _envelope.rasterNodes))
            {
                _terminal = {JobStatus::Failed, HRESULT_FROM_WIN32(ERROR_NOT_ENOUGH_QUOTA)};
                BeginGraphicsRetirementOwner(AfterGraphics::Complete);
                return true;
            }
            _resetOutput = true;
            ResumeOwner(Phase::Preparing);
        }
        return true;
    }
    if (_phase == Phase::RetireGraphics)
    {
        _graphicsRetired = true;
        _graphPrepared = false;
        _gpuCharge.ResetOwner(); // Worker destruction completed; no context/copy/row owner remains.
        if (_terminal.status != JobStatus::Finished || result.status != JobStatus::Finished)
        {
            FinishOwner(result);
            return false;
        }
        if (_afterGraphics == AfterGraphics::Recover)
        {
            const HRESULT captured = CaptureCurrentGraphicsOwner(true);
            if (FAILED(captured))
            {
                _terminal = {JobStatus::Failed, captured};
                _attemptFailure = captured;
                BeginGraphicsRetirementOwner(AfterGraphics::Complete);
                return true;
            }
            ++_restarts;
            _x = _y = 0;
            _copiedPixels = 0;
            _attemptFailure = S_OK;
            _stopOffered = false;
            _resetOutput = true;
            ResumeOwner(Phase::Preparing);
            return true;
        }
        if (_afterGraphics == AfterGraphics::Detached)
        {
            const HRESULT captured = CaptureDetachedReadback(_root->image);
            if (FAILED(captured))
            {
                _terminal = {JobStatus::Failed, captured};
                FinishOwner(_terminal);
                return false;
            }
            ResumeOwner(Phase::Detached);
            return true;
        }
        FinishOwner(result);
        return false;
    }
    if (_phase == Phase::Recording)
    {
        _phase = Phase::AwaitCommands; // Actual recorder callback joined, not merely producer-finished.
        if (result.status == JobStatus::Finished && SUCCEEDED(_attemptFailure))
            _graphPrepared = true;
        _scheduler.NotifyOwnerWork();
        return true;
    }
    if (_phase == Phase::RetireTransport)
    {
        _streamRetired = true;
        if (_terminal.status != JobStatus::Finished || FAILED(_attemptFailure) ||
            _deviceGeneration != _queue.DeviceGeneration())
        {
            if (SUCCEEDED(_attemptFailure))
                _attemptFailure = E_ABORT;
            BeginFailureRetirementOwner();
            return true;
        }
        ID3D11Device* device = nullptr;
        ID3D11DeviceContext* immediate = nullptr;
        HRESULT status = _queue.BorrowCurrentContextOwner(_root->captured, device, immediate);
        if (SUCCEEDED(status))
            status = _root->graph->PublishRegionOwner(immediate);
        if (FAILED(status))
        {
            RememberFailureOwner({JobStatus::Failed, status});
            BeginFailureRetirementOwner();
        }
        else
        {
            _graphPublished = true;
            _phase = Phase::AwaitCopySlot;
            _scheduler.NotifyOwnerWork();
        }
        return true;
    }
    if (_phase == Phase::Copying)
    {
        ReleaseRowsOwner(); // Even a skipped/cancelled body acknowledges the actual lent map after join.
        ExcludeGraphOwner();
        if (_terminal.status != JobStatus::Finished || FAILED(_attemptFailure) ||
            _deviceGeneration != _queue.DeviceGeneration())
        {
            if (SUCCEEDED(_attemptFailure))
                _attemptFailure = E_ABORT;
            BeginFailureRetirementOwner();
            return true;
        }
        if (LastRegion())
        {
            if (ProcessReadbackDetached() && KeepReadbackGraphForPixelContinuation())
            {
                if (_region.right - _region.left != 1 || _region.bottom - _region.top != 1)
                    std::terminate();
                const HRESULT captured = CaptureDetachedReadback(_root->image);
                if (FAILED(captured))
                {
                    _terminal = {JobStatus::Failed, captured};
                    BeginGraphicsRetirementOwner(AfterGraphics::Complete);
                }
                else
                    ResumeOwner(Phase::Detached);
            }
            else
                BeginGraphicsRetirementOwner(ProcessReadbackDetached() ? AfterGraphics::Detached
                                                                       : AfterGraphics::Complete);
        }
        else
        {
            _x += _width;
            if (_x == _region.right - _region.left)
            {
                _x = 0;
                _y += _height;
            }
            _phase = Phase::AwaitSource; // Restart is delayed until a successful original AttachSource callback.
            _scheduler.NotifyOwnerWork();
        }
        return true;
    }
    if (_phase == Phase::Detached)
    {
        if (result.status == JobStatus::Finished && _terminal.status == JobStatus::Finished)
        {
            _detachedReady = true;
            if (DeferDetachedCompletion(result, original))
                return true;
            _detachedReady = false;
        }
        if (!_graphicsRetired)
        {
            BeginGraphicsRetirementOwner(AfterGraphics::Complete);
            return true;
        }
        FinishOwner(result);
        return false;
    }
    if (result.status != JobStatus::Finished || _terminal.status != JobStatus::Finished)
    {
        BeginFailureRetirementOwner();
        return true;
    }
    if (_phase == Phase::Preparing)
    {
        _width = (std::min)(_envelope.tileSide, _region.right - _region.left - _x);
        _height = (std::min)(_envelope.tileSide, _region.bottom - _region.top - _y);
        _phase = Phase::AwaitSource;
        _scheduler.NotifyOwnerWork();
        return true;
    }
    if (_phase == Phase::AttachSource)
    {
        // Another admitted source may consume capacity between the service offer and this worker ack.
        // Remain suspended on the same ticket; the real detach notification makes another offer possible.
        if (!_commands.HasSourceSlotOwner())
        {
            _phase = Phase::AwaitSource;
            return true;
        }
        HRESULT status = S_OK;
        if (!_root->captured)
            status = CaptureCurrentGraphicsOwner(false);
        else
        {
            ID3D11Device* device = nullptr;
            ID3D11DeviceContext* immediate = nullptr;
            status = _queue.BorrowCurrentContextOwner(_root->captured, device, immediate);
        }
        if (SUCCEEDED(status))
        {
            status = !_streamInitialized ? _root->stream.InitializeOwner(_scheduler, original, _queue, _root->captured)
                     : _streamRetired    ? _root->stream.RestartOwner()
                                         : S_OK;
            if (SUCCEEDED(status))
            {
                _streamInitialized = _root->streamInitialized = true;
                _streamRetired = _root->streamRetired = false;
                _transportAuthorized = false;
                status = _commands.AttachOwner(_root->stream, original, *this, _source,
                                               GpuCommandFailureDisposition::RetainOriginalForRecovery);
                _sourceAttached = SUCCEEDED(status);
            }
        }
        if (FAILED(status))
        {
            RememberFailureOwner({JobStatus::Failed, status});
            BeginFailureRetirementOwner();
        }
        else
        {
            _width = (std::min)(_envelope.tileSide, _region.right - _region.left - _x);
            _height = (std::min)(_envelope.tileSide, _region.bottom - _region.top - _y);
            ResumeOwner(Phase::Recording, JobExecutionLane::GpuRecording);
        }
        return true;
    }
    std::terminate();
}

void RecordedDocumentReadbackJob::CommandAcknowledgedOwner(JobTicket original, GpuCommandLease command,
                                                           HRESULT result) noexcept
{
    if (!IsOwner() || original != _ticket || command.source != original || command.ordinal <= _lastCommandOrdinal ||
        command.deviceGeneration != _deviceGeneration)
        std::terminate();
    _lastCommandOrdinal = command.ordinal;
    if (FAILED(result))
        RememberFailureOwner({JobStatus::Failed, result});
    _scheduler.NotifyOwnerWork();
}

void RecordedDocumentReadbackJob::CommandSourceFailedOwner(JobTicket original, HRESULT result) noexcept
{
    if (!IsOwner() || original != _ticket || SUCCEEDED(result))
        std::terminate();
    RememberFailureOwner({JobStatus::Failed, result});
    _scheduler.NotifyOwnerWork();
}

void RecordedDocumentReadbackJob::ReadbackReady(JobTicket original, GpuReadbackRows rows, HRESULT result) noexcept
{
    if (!IsOwner() || original != _ticket || _phase != Phase::AwaitRows || _rowsReady)
        std::terminate();
    _rows = rows;
    _rowsReady = true;
    if (FAILED(result))
        RememberFailureOwner({JobStatus::Failed, result});
    _scheduler.NotifyOwnerWork();
}

HRESULT RecordedDocumentReadbackJob::RecordReadbackSource(ID3D11Texture2D*& source, D3D11_BOX& box) noexcept
{
    source = nullptr;
    box = {};
    if (!IsOwner() || !_root || !_root->graph || !_graphPublished || _cancellation.IsCancelled())
        return E_ABORT;
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* immediate = nullptr;
    RETURN_IF_FAILED(_queue.BorrowCurrentContextOwner(_root->captured, device, immediate));
    return _root->graph->BorrowRegionOwner(device, immediate, source, box);
}

bool RecordedDocumentReadbackJob::HasUtilityOwnerWork() const noexcept
{
    return IsOwner() && _serviceAttached && _phase != Phase::Complete;
}

bool RecordedDocumentReadbackJob::IsUtilityOwnerReady() const noexcept
{
    if (!HasUtilityOwnerWork())
        return false;
    const bool cancelled = _cancellation.IsCancelled();
    if (_sourceAttached && !_stopOffered &&
        (cancelled || FAILED(_attemptFailure) || _deviceGeneration != _queue.DeviceGeneration()))
        return true;
    if (_phase == Phase::AwaitCommands)
        return _root->stream.CanRetireOwner();
    if (_phase == Phase::AwaitSource)
        return cancelled || _commands.HasSourceSlotOwner();
    if (_phase == Phase::AwaitCopySlot)
        return cancelled || _deviceGeneration != _queue.DeviceGeneration() || _queue.HasReadbackSlot();
    if (_phase == Phase::AwaitRows)
        return _rowsReady; // Cancellation does not acknowledge a pending original queue request.
    if (_phase == Phase::Detached)
        return cancelled && _detachedReady; // Never resume a detached CPU body that is still executing.
    return false;
}

void RecordedDocumentReadbackJob::PumpUtilityOwner() noexcept
{
    if (!IsUtilityOwnerReady())
        return;
    if (_sourceAttached && !_stopOffered &&
        (_cancellation.IsCancelled() || FAILED(_attemptFailure) || _deviceGeneration != _queue.DeviceGeneration()))
    {
        if (_cancellation.IsCancelled())
            _terminal = {JobStatus::Cancelled};
        else if (SUCCEEDED(_attemptFailure))
            _attemptFailure = E_ABORT;
        if (!_commands.StopSourceOwner(_source))
            std::terminate();
        _stopOffered = true;
    }
    if (_phase == Phase::AwaitCommands && _root->stream.CanRetireOwner())
    {
        if (_sourceAttached)
        {
            if (!_commands.DetachOwner(_source, _root->retiredCapture))
                std::terminate();
            _sourceAttached = false;
            _source = {};
        }
        if (!_root->stream.AuthorizeRetirementOwner())
            std::terminate();
        _transportAuthorized = true;
        ResumeOwner(Phase::RetireTransport); // Worker drops original command/capture owners before copying.
    }
    else if (_phase == Phase::AwaitSource)
        ResumeOwner(Phase::AttachSource); // Successful actual Compute callback supplies stream authority.
    else if (_phase == Phase::AwaitCopySlot)
    {
        if (_cancellation.IsCancelled() || _deviceGeneration != _queue.DeviceGeneration())
        {
            if (SUCCEEDED(_attemptFailure))
                _attemptFailure = E_ABORT;
            BeginFailureRetirementOwner();
            return;
        }
        _phase = Phase::AwaitRows;
        // The queue borrows Root, which survives actual joined Close even after this wrapper is destroyed.
        const HRESULT queued = _queue.EnqueueProducedReadback(_width, _height, _ticket, *_root, *_root);
        if (FAILED(queued))
        {
            RememberFailureOwner({JobStatus::Failed, queued});
            BeginFailureRetirementOwner();
        }
    }
    else if (_phase == Phase::AwaitRows && _rowsReady)
        ResumeOwner(Phase::Copying);
    else if (_phase == Phase::Detached && _cancellation.IsCancelled())
        ResumeOwner(Phase::Detached); // Skipped useful body still reaches actual retirement acknowledgement.
}

void RecordedDocumentReadbackJob::StopUtilityOwner() noexcept
{
    if (!IsOwner())
        std::terminate();
    if (_sourceAttached && !_stopOffered)
    {
        if (!_commands.StopSourceOwner(_source))
            std::terminate();
        _stopOffered = true;
    }
    else if (_root && _streamInitialized)
        _root->stream.RequestStopOwner();
}

bool RecordedDocumentReadbackJob::ResumeDetachedReadback(JobTicket original) noexcept
{
    if (!IsOwner() || !_root || !_detachedReady || _phase != Phase::Detached || original != _ticket || !_memory ||
        _memory->IsBound())
        return false;
    _detachedReady = false;
    if (_scheduler.Resume(original, JobExecutionLane::Compute))
        return true;
    _detachedReady = true;
    return false;
}

bool RecordedDocumentReadbackJob::ContinuePixelReadback(JobTicket original, unsigned int x, unsigned int y) noexcept
{
    if (!IsOwner() || !_root || !_detachedReady || _phase != Phase::Detached || original != _ticket ||
        !KeepReadbackGraphForPixelContinuation() || !_graphPrepared || !_streamRetired || _sourceAttached ||
        _rows.data || _graphPublished || !_memory || _memory->IsBound() || _region.right - _region.left != 1 ||
        _region.bottom - _region.top != 1 || _root->originalDocument.width <= 0 ||
        _root->originalDocument.height <= 0 || x >= static_cast<unsigned int>(_root->originalDocument.width) ||
        y >= static_cast<unsigned int>(_root->originalDocument.height))
        return false;
    const auto previous = _region;
    const auto reset = _resetOutput;
    const auto attempt = _attemptFailure;
    const auto copied = _copiedPixels;
    const auto previousX = _x, previousY = _y;
    _region = {x, y, 0, x + 1, y + 1, 1};
    _x = _y = 0;
    _copiedPixels = 0;
    _resetOutput = true;
    _attemptFailure = S_OK;
    _detachedReady = false;
    _phase = Phase::Preparing;
    if (_scheduler.Resume(original, JobExecutionLane::Compute))
        return true;
    _region = previous;
    _resetOutput = reset;
    _attemptFailure = attempt;
    _copiedPixels = copied;
    _x = previousX;
    _y = previousY;
    _detachedReady = true;
    _phase = Phase::Detached;
    return false;
}

void RecordedDocumentReadbackJob::RetainJoinedRetirement(JobScheduler& scheduler, JobTicket original) noexcept
{
    if (!IsOwner() || &scheduler != &_scheduler || !_scheduler.InJoinedRetirementCallback(original) ||
        (_ticket.generation && _ticket != original))
        std::terminate();
    _ticket = original;
    if (_memory && !_memory->IsBound() && FAILED(BindReadbackMemoryForRetirement(original, *_memory)))
        std::terminate();
    ReleaseRowsOwner(); // Actual compute join precedes this callback; no worker can still read the rows.
    if (_sourceAttached)
    {
        if (!_commands.AcknowledgeSourceJoinedShutdownOwner(_source) ||
            !_commands.DetachOwner(_source, _root->retiredCapture))
            std::terminate();
        _sourceAttached = false;
        _source = {};
    }
    // Close can join a completed retirement body without delivering its owner acknowledgement.
    // Conversely a queued retirement body may never dispatch, while its owner authorization is already real.
    if (_root)
        _streamRetired = _root->streamRetired;
    if (_root && _streamInitialized && !_streamRetired)
    {
        _root->stream.RequestStopOwner();
        HRESULT result = E_PENDING;
        if (!_root->stream.ProducerFinishedOwner(result) && !_root->stream.AcknowledgeEmptyJoinedShutdownOwner())
            std::terminate();
        if (!_transportAuthorized && !_root->stream.AuthorizeRetirementOwner())
            std::terminate();
        _transportAuthorized = true;
    }
    ExcludeGraphOwner();
    if (_gpuCharge)
    {
        if (_graphicsRetired)
            _gpuCharge.ResetOwner(); // No captured/created graphics users exist.
        else if (!_serviceAttached || !_memory ||
                 !_owners.RetainJoinedChargeOwner(original, *this, _gpuCharge, *_memory))
            std::terminate();
    }
    _joinedDestruction = true;
    _phase = Phase::Complete;
    _detachedReady = false;
    if (_root)
        _root->parent = nullptr; // Queue retains Root until its actual later owner Close hook.
    DetachServiceOwner();
}

void RecordedDocumentReadbackJob::TransferReadbackOwners(CanvasDocument& document, PixelBuffer& pixels, Raster& image,
                                                         std::shared_ptr<SharedPublicationMemory>& memory) noexcept
{
    static_assert(std::is_nothrow_swappable_v<CanvasDocument>);
    static_assert(std::is_nothrow_swappable_v<PixelBuffer>);
    static_assert(std::is_nothrow_swappable_v<Raster>);
    // Empty destination is mandatory; it must not drop another charged owner on this callback stack.
    if (!IsOwner() || memory || document.layers.Identity() || document.selection || pixels || image.HasStorage())
        std::terminate();
    if (!_memory)
    {
        if (_root)
            std::terminate();
        return; // Accounting creation failed before any heavy root/source/device owner existed.
    }
    if (!_memory->IsBound())
        std::terminate();
    if (_root)
    {
        std::swap(_root->document, document);
        std::swap(_root->pixels, pixels);
        std::swap(_root->image, image);
    }
    memory = _memory;
}

void RecordedDocumentReadbackJob::Completed(JobResult result) noexcept
{
    if (!IsOwner() || !_graphicsRetired || _phase != Phase::Complete || _serviceAttached || _sourceAttached ||
        _rows.data || _graphPublished)
        std::terminate();
    const JobResult terminal = _terminal.status != JobStatus::Finished ? _terminal : result;
    if (_memory && !_memory->IsBound() && FAILED(BindReadbackMemoryForRetirement(_ticket, *_memory)))
        std::terminate();
    ReadbackCompleted(terminal, _root ? std::move(_root->image) : Raster{}, _deviceGeneration);
}

HRESULT RecordedDocumentReadbackJob::BindReadbackMemoryForRetirement(JobTicket original,
                                                                     SharedPublicationMemory& temporary) noexcept
{
    return temporary.BindForRetirement(_scheduler, original);
}
