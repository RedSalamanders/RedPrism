#include "GpuRasterUploadJob.h"

#include <cassert>
#include <exception>
#include <limits>
#include <utility>

GpuRasterCreatedResidency::~GpuRasterCreatedResidency()
{
    ResetOwner();
}

GpuRasterCreatedResidency::GpuRasterCreatedResidency(GpuRasterCreatedResidency&& other) noexcept
    : _reservation(std::move(other._reservation)), _source(std::move(other._source)),
      _texture(std::move(other._texture)), _view(std::move(other._view)),
      _ownerThread(std::exchange(other._ownerThread, 0))
{
}

GpuRasterCreatedResidency& GpuRasterCreatedResidency::operator=(GpuRasterCreatedResidency&& other) noexcept
{
    if (this != &other)
    {
        ResetOwner();
        _reservation = std::move(other._reservation);
        _source = std::move(other._source);
        _texture = std::move(other._texture);
        _view = std::move(other._view);
        _ownerThread = std::exchange(other._ownerThread, 0);
    }
    return *this;
}

ID3D11Texture2D* GpuRasterCreatedResidency::TextureOwner() const noexcept
{
    return _ownerThread == GetCurrentThreadId() && _reservation ? _texture.get() : nullptr;
}

ID3D11ShaderResourceView* GpuRasterCreatedResidency::ShaderViewOwner() const noexcept
{
    return _ownerThread == GetCurrentThreadId() && _reservation ? _view.get() : nullptr;
}

void GpuRasterCreatedResidency::ResetOwner() noexcept
{
    if (_ownerThread && _ownerThread != GetCurrentThreadId())
        std::terminate();
    _view.reset();
    _texture.reset();
    _source = {};
    _reservation.ResetOwner();
    _ownerThread = 0;
}

std::unique_ptr<GpuRasterUploadInput> GpuRasterUploadInput::Make(RasterUploadLayout layout)
{
    if (!layout.IsValid())
        return {};
    const auto maximum = (std::numeric_limits<std::uint64_t>::max)();
    const auto source = layout.Source().RetirementMemoryBytes();
    const auto scratch = layout.ReductionScratchBytes();
    if (source == 0 || source > maximum - sizeof(GpuRasterUploadInput) ||
        scratch > maximum - sizeof(GpuRasterUploadInput) - source)
        return {};
    auto input = std::make_unique<GpuRasterUploadInput>();
    input->_layout = std::move(layout);
    input->bytes = source + scratch + sizeof(GpuRasterUploadInput);
    input->destroy = [](DeferredReleaseNode* node) noexcept
    { const std::unique_ptr<GpuRasterUploadInput> owned(static_cast<GpuRasterUploadInput*>(node)); };
    return input;
}

std::uint64_t GpuRasterUploadInput::WorkerAllocationBytes() const noexcept
{
    return _layout.ReductionScratchBytes();
}

void GpuRasterUploadJob::RetireInput::operator()(GpuRasterUploadInput* input) const noexcept
{
    if (input && !TryDeferRelease(*input, DeferredReleasePolicy::AlwaysOnOwner))
    {
        const std::unique_ptr<GpuRasterUploadInput> owned(input);
    }
}

GpuRasterUploadJob::GpuRasterUploadJob(JobScheduler& scheduler, GpuUploadQueue& queue,
                                       std::unique_ptr<GpuRasterUploadInput> input, ID3D11Texture2D* destination,
                                       unsigned int subresource, unsigned int x, unsigned int y) noexcept
    : _scheduler(scheduler), _queue(queue), _input(input.release()), _subresource(subresource), _x(x), _y(y)
{
    _destination = destination;
}

GpuRasterUploadJob::~GpuRasterUploadJob()
{
    // Shutdown has joined workers and suppressed notification producers before destroying jobs. A queued
    // cancelled stage can have skipped Run entirely, so its mapped loan still requires an acknowledgement.
    if (_rows.data && !_workerPublished)
    {
        _queue.Cancel(_ticket);
        AcknowledgeUnpublished({JobStatus::Cancelled});
    }
    if (_gpuReservation)
        (void)_queue.RetainRetiredResidency(_ticket, _gpuReservation);
}

GpuRasterUploadJob::GpuRasterUploadJob(JobScheduler& scheduler, GpuUploadQueue& queue,
                                       std::unique_ptr<GpuRasterUploadInput> input,
                                       GpuRasterCreationRequest creation) noexcept
    : _scheduler(scheduler), _queue(queue), _input(input.release()), _gpuReservation(std::move(creation.reservation)),
      _creationMode(true)
{
    _deviceGeneration = _gpuReservation.DeviceGeneration();
    _creationCaptureResult =
        !_gpuReservation || !_deviceGeneration || _deviceGeneration != _queue.DeviceGeneration() ? E_ABORT
        : !_queue.HasAdmittedPersistentResidency()                                               ? E_UNEXPECTED
                                                   : _creation.CaptureOwner(creation.device, _deviceGeneration);
}

void GpuRasterUploadJob::SetRowObserverForTest(RasterUploadRowObserver observer, void* context) noexcept
{
    assert(_phase == Phase::Prepare);
    _observer = observer;
    _observerContext = context;
}

void GpuRasterUploadJob::SetPublicationObserverForTest(PublicationObserver observer, void* context) noexcept
{
    assert(_phase == Phase::Prepare);
    _publicationObserver = observer;
    _publicationContext = context;
}

JobResult GpuRasterUploadJob::Run(JobContext& context)
{
    if (context.cancellation.IsCancelled())
        return {JobStatus::Cancelled};
    if (_phase == Phase::Prepare)
    {
        if (!context.deferredRetirement)
            return {JobStatus::Failed, E_UNEXPECTED}; // refuse before a receiver can escape its scheduler record
        if (!_input || !_input->Layout().IsValid() || (!_creationMode && !_destination))
            return {JobStatus::Failed, E_INVALIDARG};
        if (context.reservedMemoryBytes < _input->WorkerAllocationBytes())
            return {JobStatus::Failed, E_OUTOFMEMORY};
        const auto& layout = _input->Layout();
        if (_creationMode)
        {
            if (FAILED(_creationCaptureResult))
                return {JobStatus::Failed, _creationCaptureResult};
            constexpr auto headroom = GpuResidencyBudget::kMinimumHeadroom + GpuUploadQueue::kStagingCapBytes;
            if (layout.LogicalTextureBytes() > (std::numeric_limits<std::uint64_t>::max)() - headroom ||
                _gpuReservation.Bytes() < layout.LogicalTextureBytes() + headroom)
                return {JobStatus::Failed, E_OUTOFMEMORY};
            const auto created = _creation.CreateOnWorker(layout, context.cancellation);
            if (FAILED(created))
                return context.cancellation.IsCancelled() ? JobResult{JobStatus::Cancelled}
                                                          : JobResult{JobStatus::Failed, created};
        }
        if (layout.SourceRowScratchBytes() > (std::numeric_limits<std::size_t>::max)() ||
            layout.ChannelSumElements() > (std::numeric_limits<std::size_t>::max)())
            return {JobStatus::Failed, E_INVALIDARG};
        _input->_sourceRow.resize(static_cast<std::size_t>(layout.SourceRowScratchBytes()));
        _input->_channelSums.resize(static_cast<std::size_t>(layout.ChannelSumElements()));
        _phase = Phase::Ready;
        return {};
    }
    if (_phase == Phase::Filling)
    {
        const auto& layout = _input->Layout();
        const auto status = ReduceRasterBlocksForUpload(
            layout.Source(), layout.BaseChunkAt(_chunk), _rows.data, _rows.capacity, _rows.rowPitch, _input->_sourceRow,
            _input->_channelSums, context.cancellation, _observer, _observerContext);
        const auto fill = status == RasterUploadWorkStatus::Ready       ? GpuUploadFill::Ready
                          : status == RasterUploadWorkStatus::Cancelled ? GpuUploadFill::Cancelled
                                                                        : GpuUploadFill::Failed;
        const JobResult result = status == RasterUploadWorkStatus::Ready ? JobResult{}
                                 : status == RasterUploadWorkStatus::Cancelled
                                     ? JobResult{JobStatus::Cancelled}
                                     : JobResult{JobStatus::Failed, E_INVALIDARG};
        // Final access to the borrowed writable rows. The owner may finish the copy before this worker's
        // scheduler completion is drained, but the receiver remains owned by this exact record either way.
        _workerPublished = _queue.PublishFilled(_rows.lease, fill);
        if (_publicationObserver)
            _publicationObserver(_publicationContext, context);
        return _workerPublished ? result : JobResult{JobStatus::Failed, E_UNEXPECTED};
    }
    return _phase == Phase::AfterChunk ? _workerResult : JobResult{JobStatus::Failed, E_UNEXPECTED};
}

void GpuRasterUploadJob::AcknowledgeUnpublished(JobResult result) noexcept
{
    if (!_rows.data || _workerPublished)
        return;
    const auto fill = result.status == JobStatus::Cancelled ? GpuUploadFill::Cancelled : GpuUploadFill::Failed;
    _workerPublished = _queue.PublishFilled(_rows.lease, fill);
    assert(_workerPublished);
}

bool GpuRasterUploadJob::DeferRetirement(JobResult result, JobTicket ticket) noexcept
{
    if (!_ticket.generation)
        _ticket = ticket; // retains original identity even when preparation/refusal precedes the first chunk
    assert(_ticket == ticket);
    if (_phase == Phase::AwaitRows || _phase == Phase::AwaitCopy)
    {
        assert(ticket == _ticket);
        _workerResult = result;
        _queue.Cancel(ticket);
        // A cancelled suspended record can run its terminal acknowledgement before the pending map or
        // copy fence is pumped. Its receiver still belongs to the queue until UploadFinished acknowledges
        // that exact request. Repeated scheduler cancellation must retain this same reservation too.
        if (!_queueFinished)
        {
            _phase = Phase::AwaitCopy;
            return true;
        }
        _phase = Phase::AfterChunk;
        return false;
    }
    if (_phase != Phase::Filling)
        return false;
    assert(ticket == _ticket);
    _workerResult = result;
    if (result.status != JobStatus::Finished)
        _queue.Cancel(ticket);
    AcknowledgeUnpublished(result);
    _rows = {};
    _phase = _queueFinished ? Phase::AfterChunk : Phase::AwaitCopy;
    // Keep the complete reservation even when Run was skipped/cancelled or threw before publishing its loan.
    // A queue result that arrived early is already safe; otherwise only its real completion resumes this job.
    return !_queueFinished;
}

bool GpuRasterUploadJob::RequestChunk(JobTicket ticket) noexcept
{
    _ticket = ticket;
    const auto& layout = _input->Layout();
    const auto region = layout.BaseChunkAt(_chunk);
    const auto y = static_cast<std::uint64_t>(_y) + static_cast<unsigned int>(region.outputY);
    if (region.outputWidth <= 0 || region.outputHeight <= 0 || y > (std::numeric_limits<unsigned int>::max)())
    {
        _queueResult = E_INVALIDARG;
        return false;
    }
    D3D11_TEXTURE2D_DESC desc{};
    _destination->GetDesc(&desc);
    const auto format =
        layout.Source().Format() == RasterFormat::Gray8 ? DXGI_FORMAT_R8_UNORM : DXGI_FORMAT_B8G8R8A8_UNORM;
    if (desc.Format != format || (_deviceGeneration != 0 && _deviceGeneration != _queue.DeviceGeneration()))
    {
        _queueResult = desc.Format != format ? E_INVALIDARG : E_ABORT;
        return false;
    }
    if (_deviceGeneration == 0)
        _deviceGeneration = _queue.DeviceGeneration();
    _workerPublished = false;
    _queueFinished = false;
    _phase = Phase::AwaitRows;
    _queueResult = _queue.Enqueue(_destination.get(), _subresource, _x, static_cast<unsigned int>(y),
                                  static_cast<unsigned int>(region.outputWidth),
                                  static_cast<unsigned int>(region.outputHeight), ticket, *this);
    if (FAILED(_queueResult))
    {
        _queueFinished = true;
        _phase = Phase::AfterChunk;
        return false;
    }
    return true;
}

bool GpuRasterUploadJob::DeferCompletion(JobResult, JobTicket ticket) noexcept
{
    if (_phase == Phase::Ready)
    {
        if (_creationMode)
        {
            if (_deviceGeneration != _queue.DeviceGeneration() || !_creation.AcknowledgeOwner())
            {
                _queueResult = _deviceGeneration != _queue.DeviceGeneration() ? E_ABORT : E_UNEXPECTED;
                _phase = Phase::AfterChunk;
                return false;
            }
            _destination = _creation.TextureOwner();
            if (!_destination || !_creation.ShaderViewOwner())
            {
                _queueResult = E_UNEXPECTED;
                _phase = Phase::AfterChunk;
                return false;
            }
        }
        return RequestChunk(ticket);
    }
    if (_phase != Phase::AfterChunk || FAILED(_queueResult))
        return false;
    ++_chunk;
    return _chunk < _input->Layout().BaseChunkCount() ? RequestChunk(ticket) : false;
}

void GpuRasterUploadJob::WritableRowsReady(JobTicket ticket, GpuUploadRows rows) noexcept
{
    assert(ticket == _ticket && _phase == Phase::AwaitRows);
    _rows = rows;
    _phase = Phase::Filling;
    const bool resumed = _scheduler.Resume(ticket);
    assert(resumed);
    (void)resumed;
}

void GpuRasterUploadJob::UploadFinished(JobTicket ticket, HRESULT result) noexcept
{
    assert(ticket == _ticket);
    _queueResult = result;
    _queueFinished = true;
    if (_phase == Phase::AwaitRows || _phase == Phase::AwaitCopy)
    {
        _phase = Phase::AfterChunk;
        const bool resumed = _scheduler.Resume(ticket);
        assert(resumed);
        (void)resumed;
    }
    // Filling has not yet acknowledged its scheduler producer. Its terminal-retirement hook observes this
    // owner result after Run returns; the callback must not read the unfinished worker's publication fields.
}

void GpuRasterUploadJob::Completed(JobResult result) noexcept
{
    if (result.status == JobStatus::Finished && FAILED(_queueResult))
        result = {JobStatus::Failed, _queueResult}; // caller distinguishes device retirement from local Cancel
    _canAdoptCreated = _creationMode && result.status == JobStatus::Finished;
    RasterUploadCompleted(result);
    _canAdoptCreated = false;
}

GpuRasterCreatedResidency GpuRasterUploadJob::TakeCreatedResidencyOwner() noexcept
{
    if (!_canAdoptCreated || !_gpuReservation || !_input || !_destination || !_creation.ShaderViewOwner())
        return {};
    GpuRasterCreatedResidency residency;
    residency._ownerThread = GetCurrentThreadId();
    residency._source = _input->Layout().Source();
    residency._texture = _destination.get();
    residency._view = _creation.ShaderViewOwner();
    const bool retired = _creation.ReleaseOwner();
    assert(retired);
    if (!retired)
        std::terminate();
    _destination.reset(); // the adopted owner now holds every graphics reference from this job
    residency._reservation = std::move(_gpuReservation);
    return residency;
}
