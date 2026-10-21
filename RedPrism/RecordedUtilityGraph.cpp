#include "RecordedUtilityGraph.h"

#include <exception>
#include <utility>

RecordedUtilityGraph::RecordedUtilityGraph(std::uint32_t originalOwnerThread) noexcept
    : _ownerThread(originalOwnerThread)
{
}

RecordedUtilityGraph::~RecordedUtilityGraph()
{
    if (_phase != Phase::Retired && _phase != Phase::Empty)
        std::terminate(); // The original parent must join/detach and retire every acquired prefix on a worker.
}

bool RecordedUtilityGraph::IsOwner() const noexcept
{
    return _ownerThread == GetCurrentThreadId();
}

bool RecordedUtilityGraph::ValidRegion(D3D11_BOX region) const noexcept
{
    return _source.width > 0 && _source.height > 0 && _source.width <= kMaxCanvasSide &&
           _source.height <= kMaxCanvasSide && region.front == 0 && region.back == 1 && region.right > region.left &&
           region.bottom > region.top && region.right <= static_cast<unsigned int>(_source.width) &&
           region.bottom <= static_cast<unsigned int>(_source.height) && region.right - region.left <= 2048 &&
           region.bottom - region.top <= 2048;
}

HRESULT RecordedUtilityGraph::PrepareForWork(ID3D11Device* device, CanvasDocument source, D3D11_BOX region,
                                             GpuCommandStream& original, CancelToken cancellation) noexcept
{
    if (!_ownerThread || IsOwner() || _phase != Phase::Empty || !device || !original.CanRecordForWork())
        return E_INVALIDARG;
    if (cancellation.IsCancelled())
        return E_ABORT;
    // Concurrent creation is checked before acquiring a device/resource prefix. Unsupported creation refuses.
    D3D11_FEATURE_DATA_THREADING threading{};
    const HRESULT support = device->CheckFeatureSupport(D3D11_FEATURE_THREADING, &threading, sizeof(threading));
    if (FAILED(support) || !threading.DriverConcurrentCreates)
        return FAILED(support) ? support : DXGI_ERROR_UNSUPPORTED;
    _phase = Phase::Recording;
    _source = std::move(source);
    if (!ValidRegion(region))
        return E_INVALIDARG;
    _device = device;
    _originalStream = &original;
    _recording.emplace();
    HRESULT result = device->CreateDeferredContext(0, _deferred.put());
    if (SUCCEEDED(result))
        result = _recording->InitializeForWork(_deferred.get(), original, cancellation);
    if (SUCCEEDED(result))
        result = _composition.CreateDeviceResources(device, _deferred.get(), &*_recording);
    if (SUCCEEDED(result))
    {
        _utilityAcquired = _composition.AcquireUtility();
        if (!_utilityAcquired)
            result = HRESULT_FROM_WIN32(ERROR_NOT_ENOUGH_QUOTA);
    }
    return SUCCEEDED(result) ? RecordRegionForWork(region) : result;
}

HRESULT RecordedUtilityGraph::RecordRegionForWork(D3D11_BOX region) noexcept
{
    if (IsOwner() || _phase != Phase::Recording || !_recording || !ValidRegion(region))
        return E_INVALIDARG;
    _region = region;
    // Reuses this private composition's source residency. No shared immediate-context cache is mutated.
    HRESULT result = _composition.ComposeUtilityTile(_source, region, _regionTexture);
    if (SUCCEEDED(result))
    {
        _recording->CheckpointForWork();
        result = _recording->StatusForWork();
    }
    if (SUCCEEDED(result))
        _phase = Phase::Sealed;
    return result;
}

HRESULT RecordedUtilityGraph::RecordNextForWork(D3D11_BOX region, GpuCommandStream& original,
                                                CancelToken cancellation) noexcept
{
    if (IsOwner() || _phase != Phase::Excluded || &original != _originalStream || !ValidRegion(region) || !_recording ||
        !original.CanRecordForWork())
        return E_INVALIDARG;
    // Retain shaders and require the next epoch of the original acknowledged stream.
    const HRESULT result = _recording->RestartForWork(_deferred.get(), original, cancellation);
    if (FAILED(result))
        return result;
    _phase = Phase::Recording;
    _immediate.reset(); // Previous immediate/source copy uses have actually acknowledged on the original parent.
    _regionTexture.reset();
    return RecordRegionForWork(region);
}

HRESULT RecordedUtilityGraph::PublishRegionOwner(ID3D11DeviceContext* originalImmediate) noexcept
{
    if (!IsOwner() || _phase != Phase::Sealed || !originalImmediate || !_regionTexture ||
        originalImmediate->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE)
        return E_INVALIDARG;
    wil::com_ptr_nothrow<ID3D11Device> actualDevice;
    originalImmediate->GetDevice(actualDevice.put());
    if (actualDevice.get() != _device.get())
        return E_ABORT;
    _immediate = originalImmediate;
    _phase = Phase::Published;
    return S_OK;
}

HRESULT RecordedUtilityGraph::BorrowRegionOwner(ID3D11Device* device, ID3D11DeviceContext* immediate,
                                                ID3D11Texture2D*& texture, D3D11_BOX& localBox) noexcept
{
    texture = nullptr;
    localBox = {};
    if (!IsOwner() || _phase != Phase::Published || device != _device.get() || immediate != _immediate.get() ||
        !_regionTexture)
        return E_ABORT;
    texture = _regionTexture.get();
    localBox = {0, 0, 0, _region.right - _region.left, _region.bottom - _region.top, 1};
    return S_OK;
}

bool RecordedUtilityGraph::ExcludeRegionOwner() noexcept
{
    if (!IsOwner() || _phase != Phase::Published)
        return false;
    _phase = Phase::Excluded;
    return true;
}

bool RecordedUtilityGraph::RetireForWork() noexcept
{
    if (IsOwner() || _phase != Phase::Excluded)
        return false;
    ReleaseForWork();
    return true;
}

bool RecordedUtilityGraph::RetireUnpublishedForWork() noexcept
{
    if (IsOwner() || _phase == Phase::Published || _phase == Phase::Excluded || _phase == Phase::Retired)
        return false;
    ReleaseForWork();
    return true;
}

std::uint64_t RecordedUtilityGraph::RecordedUploadBytesForWork() const noexcept
{
    return !IsOwner() && _recording ? _recording->RecordedUploadBytes() : 0;
}

void RecordedUtilityGraph::ReleaseForWork() noexcept
{
    if (_utilityAcquired)
        _composition.ReleaseUtility();
    _utilityAcquired = false;
    _composition.ReleaseDeviceResources();
    _regionTexture.reset();
    _immediate.reset();
    _recording.reset();
    _deferred.reset();
    _device.reset();
    _source = {};
    _phase = Phase::Retired;
}
