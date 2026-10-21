#include "PreparedSelectionOutlineGpu.h"

#include <cassert>
#include <exception>
#include <utility>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/resource.h>
#pragma warning(pop)

PreparedSelectionOutlineGpu::~PreparedSelectionOutlineGpu()
{
    // No accidental last CPU/control/resource release on the UI thread, and no reservation refund while
    // a producer or original graphics stream can still use the graph.
    const bool empty = _phase.load(std::memory_order_acquire) == Phase::Empty;
    assert(empty);
    if (!empty)
        std::terminate();
}

bool PreparedSelectionOutlineGpu::IsOwner() const noexcept
{
    return _ownerThread != 0 && _ownerThread == GetCurrentThreadId();
}

std::int32_t PreparedSelectionOutlineGpu::GpuMemoryBytes(unsigned int width, unsigned int height,
                                                         std::uint64_t& output) noexcept
{
    constexpr std::uint64_t limit = PreparedSelectionOutlineRaster::MaximumPixels;
    if (!width || !height || width > PreparedSelectionOutlineRaster::MaximumSide ||
        height > PreparedSelectionOutlineRaster::MaximumSide || width > limit / height)
        return E_INVALIDARG;
    // Initialized immutable texture bytes plus a conservative duplicate initialization copy and explicit
    // opaque driver headroom. This is declared admission, not a hard bound on a driver's private allocator.
    output = static_cast<std::uint64_t>(width) * height * PreparedSelectionOutlineRaster::PlaneCount * 2u +
             GpuResidencyBudget::kMinimumHeadroom;
    return S_OK;
}

HRESULT PreparedSelectionOutlineGpu::CaptureOwner(ID3D11Device* device, ID3D11DeviceContext* immediate,
                                                  std::uint64_t generation,
                                                  GpuResidencyReservation& reservation) noexcept
{
    if (_phase.load(std::memory_order_relaxed) != Phase::Empty)
        return E_UNEXPECTED;
    if (!device || !immediate || !generation || !reservation || reservation.DeviceGeneration() != generation ||
        (device->GetCreationFlags() & D3D11_CREATE_DEVICE_SINGLETHREADED) != 0 ||
        immediate->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE)
        return E_INVALIDARG;
    wil::com_ptr_nothrow<ID3D11Device> contextDevice;
    immediate->GetDevice(contextDevice.put());
    if (contextDevice.get() != device)
        return E_INVALIDARG;
    D3D11_FEATURE_DATA_THREADING threading{};
    const HRESULT result = device->CheckFeatureSupport(D3D11_FEATURE_THREADING, &threading, sizeof(threading));
    if (FAILED(result))
        return result;
    _device = device;
    _immediate = immediate;
    _generation = generation;
    _ownerThread = GetCurrentThreadId();
    _concurrentCreates = threading.DriverConcurrentCreates != FALSE;
    _reservation = std::move(reservation);
    _phase.store(Phase::Captured, std::memory_order_release);
    return S_OK;
}

HRESULT PreparedSelectionOutlineGpu::CreateOnWorker(PreparedSelectionOutlineRaster raster, CancelToken cancellation,
                                                    Observer observer) noexcept
{
    if (IsOwner())
        return E_ACCESSDENIED;
    Phase expected = Phase::Captured;
    if (!_phase.compare_exchange_strong(expected, Phase::Creating, std::memory_order_acquire))
        return E_UNEXPECTED;
    const auto publish =
        wil::scope_exit([this]() noexcept { _phase.store(Phase::Produced, std::memory_order_release); });
    if (cancellation.IsCancelled())
        return _result = HRESULT_FROM_WIN32(ERROR_CANCELLED);
    if (!_concurrentCreates)
        return _result = HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);
    if (!raster.HasStorage() || !raster.SourceIdentity())
        return _result = E_INVALIDARG;
    const auto& grid = raster.Key();
    std::uint64_t required = 0;
    _result = GpuMemoryBytes(grid.width, grid.height, required);
    if (FAILED(_result))
        return _result;
    if (_reservation.Bytes() < required)
        return _result = E_OUTOFMEMORY;
    _source = raster.Source();
    _grid = grid;
    // The incoming CPU mask owner is released on this creator thread after initialization. Only the
    // strong original selection source and scalar grid survive into the GPU cache.
    D3D11_TEXTURE2D_DESC description{};
    description.Width = grid.width;
    description.Height = grid.height;
    description.MipLevels = 1;
    description.ArraySize = 1;
    description.Format = DXGI_FORMAT_R8_UNORM;
    description.SampleDesc.Count = 1;
    description.Usage = D3D11_USAGE_IMMUTABLE;
    description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    const auto finishCall = [&]() noexcept
    {
        ++_resourceCalls;
        if (observer.afterResourceCall)
            observer.afterResourceCall(observer.context, _resourceCalls);
    };
    for (std::size_t plane = 0; plane < _textures.size(); ++plane)
    {
        if (cancellation.IsCancelled())
            return _result = HRESULT_FROM_WIN32(ERROR_CANCELLED);
        const auto pixels = raster.Plane(plane);
        if (pixels.size() != static_cast<std::size_t>(grid.width) * grid.height)
            return _result = E_INVALIDARG;
        const D3D11_SUBRESOURCE_DATA initial{pixels.data(), grid.width, 0};
        if (_resourceCalls == _failureCall)
            return _result = _failureResult;
        _result = _device->CreateTexture2D(&description, &initial, _textures[plane].put());
        finishCall();
        if (FAILED(_result))
            return _result;
        if (cancellation.IsCancelled())
            return _result = HRESULT_FROM_WIN32(ERROR_CANCELLED);
        if (_resourceCalls == _failureCall)
            return _result = _failureResult;
        _result = _device->CreateShaderResourceView(_textures[plane].get(), nullptr, _views[plane].put());
        finishCall();
        if (FAILED(_result))
            return _result;
    }
    return _result = cancellation.IsCancelled() ? HRESULT_FROM_WIN32(ERROR_CANCELLED) : S_OK;
}

bool PreparedSelectionOutlineGpu::Matches(ID3D11Device* device, ID3D11DeviceContext* immediate,
                                          std::uint64_t generation, const void* sourceIdentity,
                                          const GridKey& grid) const noexcept
{
    return device == _device.get() && immediate == _immediate.get() && generation == _generation && sourceIdentity &&
           sourceIdentity == _source.Identity() && grid == _grid;
}

HRESULT PreparedSelectionOutlineGpu::PublishOwner(ID3D11Device* device, ID3D11DeviceContext* immediate,
                                                  std::uint64_t generation, const void* sourceIdentity,
                                                  const GridKey& grid) noexcept
{
    if (!IsOwner())
        return E_ACCESSDENIED;
    if (_phase.load(std::memory_order_acquire) != Phase::Produced)
        return E_UNEXPECTED;
    if (FAILED(_result))
        return _result;
    if (!Matches(device, immediate, generation, sourceIdentity, grid))
        return E_ABORT;
    _phase.store(Phase::Published, std::memory_order_release);
    return S_OK;
}

PreparedSelectionOutlineGpu::Views PreparedSelectionOutlineGpu::BorrowOwner(
    ID3D11Device* device, ID3D11DeviceContext* immediate, std::uint64_t generation, const void* sourceIdentity,
    const GridKey& grid, unsigned int antPhase) const noexcept
{
    if (!IsOwner() || _phase.load(std::memory_order_acquire) != Phase::Published || antPhase >= 8 ||
        !Matches(device, immediate, generation, sourceIdentity, grid))
        return {};
    return {_views[0].get(), _views[antPhase + 1].get(), grid.width, grid.height};
}

bool PreparedSelectionOutlineGpu::DetachOwner() noexcept
{
    if (!IsOwner())
        return false;
    Phase expected = Phase::Published;
    return _phase.compare_exchange_strong(expected, Phase::Detached, std::memory_order_release);
}

bool PreparedSelectionOutlineGpu::RetireForWork(Phase expected) noexcept
{
    if (IsOwner() || !_phase.compare_exchange_strong(expected, Phase::Retiring, std::memory_order_acquire))
        return false;
    for (auto& view : _views)
        view.reset();
    for (auto& texture : _textures)
        texture.reset();
    _source = {};
    _grid = {};
    _immediate.reset();
    _device.reset();
    _phase.store(Phase::Retired, std::memory_order_release);
    return true;
}

bool PreparedSelectionOutlineGpu::RetireDetachedOnWorker() noexcept
{
    return RetireForWork(Phase::Detached);
}

bool PreparedSelectionOutlineGpu::RetireUnpublishedOnWorker() noexcept
{
    const auto current = _phase.load(std::memory_order_acquire);
    return (current == Phase::Captured || current == Phase::Produced) && RetireForWork(current);
}

bool PreparedSelectionOutlineGpu::CompleteRetirementOwner() noexcept
{
    if (!IsOwner() || _phase.load(std::memory_order_acquire) != Phase::Retired)
        return false;
    _reservation.ResetOwner();
    _generation = 0;
    _resourceCalls = 0;
    _result = E_PENDING;
    _failureCall = 18;
    _failureResult = E_FAIL;
    _concurrentCreates = false;
    _phase.store(Phase::Empty, std::memory_order_release);
    _ownerThread = 0;
    return true;
}

std::uint64_t PreparedSelectionOutlineGpu::ReservedBytesOwner() const noexcept
{
    return IsOwner() ? _reservation.Bytes() : 0;
}

unsigned int PreparedSelectionOutlineGpu::ResourceCallsOwner() const noexcept
{
    const auto phase = _phase.load(std::memory_order_acquire);
    return IsOwner() && (phase == Phase::Produced || phase == Phase::Published || phase == Phase::Detached)
               ? _resourceCalls
               : 0;
}

HRESULT PreparedSelectionOutlineGpu::ResultOwner() const noexcept
{
    const auto phase = _phase.load(std::memory_order_acquire);
    return IsOwner() && (phase == Phase::Produced || phase == Phase::Published || phase == Phase::Detached) ? _result
                                                                                                            : E_PENDING;
}

bool PreparedSelectionOutlineGpu::RestrictConcurrencyForTest() noexcept
{
    if (!IsOwner() || _phase.load(std::memory_order_relaxed) != Phase::Captured)
        return false;
    _concurrentCreates = false;
    return true;
}

bool PreparedSelectionOutlineGpu::SetResourceFailureForTest(unsigned int call, HRESULT error) noexcept
{
    if (!IsOwner() || _phase.load(std::memory_order_relaxed) != Phase::Captured || call >= 18 || SUCCEEDED(error))
        return false;
    _failureCall = call;
    _failureResult = error;
    return true;
}
