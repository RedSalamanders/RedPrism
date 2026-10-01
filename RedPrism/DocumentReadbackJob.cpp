#include "DocumentReadbackJob.h"

#include "CanvasRenderer.h"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <limits>
#include <type_traits>
#include <utility>

namespace
{
bool CanRestart(HRESULT result) noexcept
{
    return result == E_ABORT || result == DXGI_ERROR_DEVICE_REMOVED || result == DXGI_ERROR_DEVICE_RESET ||
           result == DXGI_ERROR_DEVICE_HUNG || result == DXGI_ERROR_DRIVER_INTERNAL_ERROR;
}
} // namespace

DocumentReadbackJob::DocumentReadbackJob(JobScheduler& scheduler, GpuWorkQueue& queue, CanvasRenderer& composition,
                                         CanvasDocument document, D3D11_BOX region) noexcept
    : _scheduler(scheduler), _queue(queue), _composition(composition), _document(std::move(document)), _region(region)
{
}

DocumentReadbackJob::~DocumentReadbackJob()
{
    // Job destruction follows worker join, including shutdown when Completed is deliberately dropped.
    Acknowledge();
    if (_utilityAcquired)
        _composition.ReleaseUtility();
}

DocumentReadbackJob::DocumentReadbackJob(JobScheduler& scheduler, GpuWorkQueue& queue, CanvasRenderer& composition,
                                         D3D11_BOX region) noexcept
    : _scheduler(scheduler), _queue(queue), _composition(composition), _region(region), _phase(Phase::Capture)
{
}

void DocumentReadbackJob::TransferReadbackOwners(CanvasDocument& document, PixelBuffer& pixels, Raster& image) noexcept
{
    static_assert(std::is_nothrow_swappable_v<CanvasDocument>);
    static_assert(std::is_nothrow_swappable_v<PixelBuffer>);
    static_assert(std::is_nothrow_swappable_v<Raster>);
    std::swap(_document, document);
    std::swap(_pixels, pixels);
    std::swap(_image, image);
}

std::uint64_t DocumentReadbackJob::RequiredMemory(const D3D11_BOX& region) noexcept
{
    if (region.front != 0 || region.back != 1 || region.right <= region.left || region.bottom <= region.top ||
        region.right - region.left > kMaxCanvasSide || region.bottom - region.top > kMaxCanvasSide)
        return 0;
    const auto bytes = static_cast<std::uint64_t>(region.right - region.left) * (region.bottom - region.top) * 4;
    return bytes <= std::numeric_limits<std::size_t>::max()
               ? PixelBuffer::RequiredBytes(static_cast<std::size_t>(bytes))
               : 0;
}

bool DocumentReadbackJob::ValidRegion() const noexcept
{
    return _document.width > 0 && _document.height > 0 && _document.width <= kMaxCanvasSide &&
           _document.height <= kMaxCanvasSide && RequiredMemory(_region) != 0 &&
           _region.right <= static_cast<unsigned int>(_document.width) &&
           _region.bottom <= static_cast<unsigned int>(_document.height);
}

bool DocumentReadbackJob::LastTile() const noexcept
{
    return _x + _tileWidth == _region.right - _region.left && _y + _tileHeight == _region.bottom - _region.top;
}

JobResult DocumentReadbackJob::Run(JobContext& context)
{
    _cancellation = context.cancellation;
    if (_phase == Phase::Capture)
        return {}; // no source capture before the owner's admitted continuation
    if (_phase == Phase::Error)
        return {JobStatus::Failed, _error};
    if (_phase == Phase::ProcessDetached)
        return ProcessReadback(_image, context);
    if (_phase == Phase::Prepare)
    {
        if (!ValidRegion())
            return {JobStatus::Failed, E_INVALIDARG};
        if (context.reservedMemoryBytes < RequiredMemory(_region))
            return {JobStatus::Failed, HRESULT_FROM_WIN32(ERROR_NOT_ENOUGH_QUOTA)};
        if (_resetOutput)
        {
            ResetReadback();
            _image = {};
            _pixels = {};
            _resetOutput = false;
            _copiedPixels = 0;
        }
        if (const auto prepared = PrepareReadbackSnapshot(_document, context); prepared.status != JobStatus::Finished)
            return prepared;
        if (context.cancellation.IsCancelled())
            return {JobStatus::Cancelled};
        const auto bytes = static_cast<std::size_t>(_region.right - _region.left) * (_region.bottom - _region.top) * 4;
        _pixels = PixelBuffer::TryAllocate(bytes);
        return _pixels ? JobResult{} : JobResult{JobStatus::Failed, E_OUTOFMEMORY};
    }
    // A GPU failure resumes a successful decision stage so its owner can perform a bounded restart. A final
    // error is delivered by a distinct worker stage; it cannot bypass mapped-row acknowledgement.
    if (FAILED(_error))
        return {};
    if (!_rows.data || _rows.width != _tileWidth || _rows.height != _tileHeight)
        return {JobStatus::Failed, E_UNEXPECTED};
    const auto stride = static_cast<std::size_t>(_region.right - _region.left) * 4;
    for (unsigned int y = 0; y < _tileHeight; ++y)
    {
        if (context.cancellation.IsCancelled())
            return {JobStatus::Cancelled};
        std::memcpy(_pixels.Data() + static_cast<std::size_t>(_y + y) * stride + static_cast<std::size_t>(_x) * 4,
                    _rows.data + static_cast<std::size_t>(y) * _rows.rowPitch,
                    static_cast<std::size_t>(_tileWidth) * 4);
    }
    _copiedPixels += static_cast<std::uint64_t>(_tileWidth) * _tileHeight;
    context.ReportProgress(static_cast<unsigned int>(
        _copiedPixels * 100 /
        (static_cast<std::uint64_t>(_region.right - _region.left) * (_region.bottom - _region.top))));
    if (LastTile())
    {
        _image =
            Raster::Adopt(static_cast<int>(_region.right - _region.left),
                          static_cast<int>(_region.bottom - _region.top), RasterFormat::Bgra8888, std::move(_pixels));
        if (!_image.HasStorage())
            return {JobStatus::Failed, E_UNEXPECTED};
        return ProcessReadbackDetached() ? JobResult{} : ProcessReadback(_image, context);
    }
    return {};
}

void DocumentReadbackJob::Acknowledge() noexcept
{
    if (_rows.data)
    {
        const bool released = _queue.ReleaseMapped(_rows.lease);
        assert(released);
        (void)released;
        _rows = {};
    }
}

void DocumentReadbackJob::ResumeError(JobTicket ticket, HRESULT error) noexcept
{
    _phase = Phase::Error;
    _error = error;
    const bool resumed = _scheduler.Resume(ticket);
    assert(resumed);
    (void)resumed;
}

void DocumentReadbackJob::RequestTile(JobTicket ticket) noexcept
{
    _ticket = ticket;
    _tileWidth = (std::min)(_tileSide, _region.right - _region.left - _x);
    _tileHeight = (std::min)(_tileSide, _region.bottom - _region.top - _y);
    _phase = Phase::Copy;
    _deviceGeneration = _queue.DeviceGeneration();
    _error = _queue.EnqueueProducedReadback(_tileWidth, _tileHeight, ticket, *this, *this);
    if (FAILED(_error))
        ResumeError(ticket, _error);
}

bool DocumentReadbackJob::DeferCompletion(JobResult, JobTicket ticket) noexcept
{
    Acknowledge();
    if (_phase == Phase::Capture)
    {
        const HRESULT captured = CaptureReadbackDocument(_document);
        if (FAILED(captured))
            ResumeError(ticket, captured);
        else
        {
            _phase = captured == S_FALSE ? Phase::ProcessDetached : Phase::Prepare;
            const bool resumed = _scheduler.Resume(ticket);
            assert(resumed);
            (void)resumed;
        }
        return true;
    }
    if (_phase == Phase::ProcessDetached)
        return false; // all GPU rows were acknowledged before this CPU-only stage started
    if (_phase == Phase::Copy)
    {
        const HRESULT failure = FAILED(_error)                                   ? _error
                                : _deviceGeneration != _queue.DeviceGeneration() ? E_ABORT
                                                                                 : S_OK;
        if (FAILED(failure))
        {
            if (!CanRestart(failure) || _restarts >= 2)
                ResumeError(ticket, failure);
            else
            {
                const HRESULT rebound = PrepareReadbackDevice(failure);
                if (FAILED(rebound))
                    ResumeError(ticket, rebound);
                else
                {
                    ++_restarts;
                    _x = _y = 0;
                    _phase = Phase::Prepare;
                    _resetOutput = true;
                    const bool resumed = _scheduler.Resume(ticket);
                    assert(resumed);
                    (void)resumed;
                }
            }
            return true;
        }
        if (LastTile())
        {
            if (!ProcessReadbackDetached())
                return false;
            if (_utilityAcquired)
            {
                _composition.ReleaseUtility();
                _utilityAcquired = false;
            }
            _phase = Phase::ProcessDetached;
            const bool resumed = _scheduler.Resume(ticket);
            assert(resumed);
            (void)resumed;
            return true;
        }
        _x += _tileWidth;
        if (_x == _region.right - _region.left)
        {
            _x = 0;
            _y += _tileHeight;
        }
    }
    else
    {
        const HRESULT ready = PrepareReadbackDevice(S_OK);
        if (FAILED(ready))
        {
            ResumeError(ticket, ready);
            return true;
        }
        if (!_utilityAcquired)
        {
            _utilityAcquired = _composition.AcquireUtility();
            if (!_utilityAcquired)
            {
                ResumeError(ticket, HRESULT_FROM_WIN32(ERROR_NOT_ENOUGH_QUOTA));
                return true;
            }
        }
        // Match the committed synchronous traversal's guard room for inverse transforms/filter footprints.
        _tileSide = (std::min)(GpuWorkQueue::kMaxReadbackSide,
                               static_cast<unsigned int>((std::max)(1, _composition.TextureLimit() / 2)));
    }
    RequestTile(ticket);
    return true;
}

void DocumentReadbackJob::ReadbackReady(JobTicket ticket, GpuReadbackRows rows, HRESULT result) noexcept
{
    assert(ticket == _ticket);
    _rows = rows;
    _error = result;
    const bool resumed = _scheduler.Resume(ticket);
    assert(resumed);
    (void)resumed;
}

HRESULT DocumentReadbackJob::RecordReadbackSource(ID3D11Texture2D*& source, D3D11_BOX& box) noexcept
{
    source = nullptr;
    if (_cancellation.IsCancelled())
        return E_ABORT;
    const D3D11_BOX region{
        _region.left + _x, _region.top + _y, 0, _region.left + _x + _tileWidth, _region.top + _y + _tileHeight, 1};
    wil::com_ptr_nothrow<ID3D11Texture2D> texture;
    const HRESULT result = _composition.ComposeUtilityTile(_document, region, texture);
    if (FAILED(result))
        return result;
    source = texture.get(); // composition owns it through the immediately following CopySubresourceRegion
    box = {0, 0, 0, _tileWidth, _tileHeight, 1};
    return S_OK;
}

void DocumentReadbackJob::Completed(JobResult result) noexcept
{
    Acknowledge();
    if (_utilityAcquired)
    {
        _composition.ReleaseUtility();
        _utilityAcquired = false;
    }
    if (result.status == JobStatus::Finished && _phase != Phase::ProcessDetached &&
        _deviceGeneration != _queue.DeviceGeneration())
        result = {JobStatus::Cancelled};
    ReadbackCompleted(result, result.status == JobStatus::Finished ? std::move(_image) : Raster{}, _deviceGeneration);
}
