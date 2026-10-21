#include "GpuRasterTextureCreation.h"
#include "CompositionVertexShader.h"
#include "RasterMipPixelShader.h"

#include <algorithm>
#include <cassert>
#include <exception>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/resource.h>
#pragma warning(pop)

GpuRasterTextureCreation::~GpuRasterTextureCreation()
{
    const bool retired = ReleaseOwner(); // scheduler shutdown joins creators before owner destruction
    assert(retired);
    if (!retired)
        std::terminate(); // never let member destructors release a running creator on another thread
}

bool GpuRasterTextureCreation::IsOwner() const noexcept
{
    return _ownerThread != 0 && _ownerThread == GetCurrentThreadId();
}

HRESULT GpuRasterTextureCreation::CaptureOwner(ID3D11Device* device, std::uint64_t generation) noexcept
{
    if (_state.load(std::memory_order_relaxed) != State::Empty)
        return E_UNEXPECTED;
    if (!device || !generation || (device->GetCreationFlags() & D3D11_CREATE_DEVICE_SINGLETHREADED) != 0)
        return E_INVALIDARG;
    D3D11_FEATURE_DATA_THREADING threading{};
    _threadingResult = device->CheckFeatureSupport(D3D11_FEATURE_THREADING, &threading, sizeof(threading));
    if (FAILED(_threadingResult))
        return _threadingResult;
    _device = device; // WIL retains the exact borrowed device; no raw AddRef/attach pair
    _generation = generation;
    _ownerThread = GetCurrentThreadId();
    _concurrentCreates = threading.DriverConcurrentCreates != FALSE;
    _state.store(State::Captured, std::memory_order_release);
    return S_OK;
}

HRESULT GpuRasterTextureCreation::CreateOnWorker(const RasterUploadLayout& layout, CancelToken cancellation) noexcept
{
    if (_state.load(std::memory_order_acquire) != State::Captured)
        return E_UNEXPECTED;
    if (IsOwner())
        return E_ACCESSDENIED;
    State expected = State::Captured;
    if (!_state.compare_exchange_strong(expected, State::Running, std::memory_order_acquire))
        return E_UNEXPECTED;
    const auto publish =
        wil::scope_exit([this]() noexcept { _state.store(State::Finished, std::memory_order_release); });
    const auto cancelled = [&]() noexcept { return cancellation.IsCancelled(); };
    if (cancelled())
        return _result = HRESULT_FROM_WIN32(ERROR_CANCELLED);
    if (!layout.IsValid() || layout.Width() < 1 || layout.Height() < 1 || !layout.MipLevels())
        return _result = E_INVALIDARG;
    if (!_concurrentCreates)
        return _result = HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);
    if (_prepareMipSlices && layout.MipLevels() > kMipSliceCapacity + 1)
        return _result = E_INVALIDARG;

    D3D11_TEXTURE2D_DESC description{};
    description.Width = static_cast<unsigned int>(layout.Width());
    description.Height = static_cast<unsigned int>(layout.Height());
    description.MipLevels = layout.MipLevels();
    description.ArraySize = 1;
    description.Format =
        layout.Source().Format() == RasterFormat::Gray8 ? DXGI_FORMAT_R8_UNORM : DXGI_FORMAT_B8G8R8A8_UNORM;
    description.SampleDesc.Count = 1;
    description.Usage = D3D11_USAGE_DEFAULT;
    description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    if (description.MipLevels > 1)
    {
        description.BindFlags |= D3D11_BIND_RENDER_TARGET;
        description.MiscFlags = _prepareMipSlices ? 0 : D3D11_RESOURCE_MISC_GENERATE_MIPS;
    }
    if (_prepareMipSlices && description.MipLevels > 1)
    {
        unsigned int support = 0;
        _result = _device->CheckFormatSupport(description.Format, &support);
        if (FAILED(_result))
            return _result;
        constexpr auto required =
            D3D11_FORMAT_SUPPORT_TEXTURE2D | D3D11_FORMAT_SUPPORT_SHADER_SAMPLE | D3D11_FORMAT_SUPPORT_RENDER_TARGET;
        if ((support & required) != required)
            return _result = HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);
    }
    if (_failure == GpuRasterCreationFailure::Texture)
        return _result = _failureResult;
    ++_resourceCalls;
    _result = _device->CreateTexture2D(&description, nullptr, _texture.put());
    if (FAILED(_result) || cancelled())
        return _result = cancelled() ? HRESULT_FROM_WIN32(ERROR_CANCELLED) : _result;
    if (_failure == GpuRasterCreationFailure::ShaderView)
        return _result = _failureResult;
    ++_resourceCalls;
    _result = _device->CreateShaderResourceView(_texture.get(), nullptr, _view.put());
    if (FAILED(_result))
        return _result;
    if (_prepareMipSlices)
    {
        if (description.MipLevels > 1)
        {
            if (cancelled())
                return _result = HRESULT_FROM_WIN32(ERROR_CANCELLED);
            if (_failure == GpuRasterCreationFailure::MipVertexShader)
                return _result = _failureResult;
            ++_resourceCalls;
            _result = _device->CreateVertexShader(g_CompositionVertexShader, sizeof(g_CompositionVertexShader), nullptr,
                                                  _mipVertex.put());
            if (FAILED(_result))
                return _result;
            if (cancelled())
                return _result = HRESULT_FROM_WIN32(ERROR_CANCELLED);
            if (_failure == GpuRasterCreationFailure::MipPixelShader)
                return _result = _failureResult;
            ++_resourceCalls;
            _result = _device->CreatePixelShader(g_RasterMipPixelShader, sizeof(g_RasterMipPixelShader), nullptr,
                                                 _mipPixel.put());
            if (FAILED(_result))
                return _result;
            if (cancelled())
                return _result = HRESULT_FROM_WIN32(ERROR_CANCELLED);
            if (_failure == GpuRasterCreationFailure::MipSampler)
                return _result = _failureResult;
            D3D11_SAMPLER_DESC sampler{};
            sampler.Filter = D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
            sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
            sampler.MaxAnisotropy = 1;
            sampler.ComparisonFunc = D3D11_COMPARISON_NEVER;
            ++_resourceCalls;
            _result = _device->CreateSamplerState(&sampler, _mipSampler.put());
            if (FAILED(_result))
                return _result;
            if (cancelled())
                return _result = HRESULT_FROM_WIN32(ERROR_CANCELLED);
            if (_failure == GpuRasterCreationFailure::MipRasterizer)
                return _result = _failureResult;
            D3D11_RASTERIZER_DESC rasterizer{};
            rasterizer.FillMode = D3D11_FILL_SOLID;
            rasterizer.CullMode = D3D11_CULL_NONE;
            rasterizer.DepthClipEnable = TRUE;
            ++_resourceCalls;
            _result = _device->CreateRasterizerState(&rasterizer, _mipRasterizer.put());
            if (FAILED(_result))
                return _result;
            if (cancelled())
                return _result = HRESULT_FROM_WIN32(ERROR_CANCELLED);
            if (_failure == GpuRasterCreationFailure::MipDepthStencil)
                return _result = _failureResult;
            D3D11_DEPTH_STENCIL_DESC depth{};
            depth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
            depth.DepthFunc = D3D11_COMPARISON_ALWAYS;
            depth.StencilReadMask = D3D11_DEFAULT_STENCIL_READ_MASK;
            depth.StencilWriteMask = D3D11_DEFAULT_STENCIL_WRITE_MASK;
            depth.FrontFace = depth.BackFace = {D3D11_STENCIL_OP_KEEP, D3D11_STENCIL_OP_KEEP, D3D11_STENCIL_OP_KEEP,
                                                D3D11_COMPARISON_ALWAYS};
            ++_resourceCalls;
            _result = _device->CreateDepthStencilState(&depth, _mipDepth.put());
            if (FAILED(_result))
                return _result;
        }
        for (unsigned int slice = 0; slice + 1 < description.MipLevels; ++slice)
        {
            if (cancelled())
                return _result = HRESULT_FROM_WIN32(ERROR_CANCELLED);
            if (slice == _mipFailureSlice)
                return _result = _mipFailureResult;
            D3D11_SHADER_RESOURCE_VIEW_DESC view{};
            view.Format = description.Format;
            view.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
            view.Texture2D.MostDetailedMip = slice;
            view.Texture2D.MipLevels = 1;
            ++_resourceCalls;
            _result = _device->CreateShaderResourceView(_texture.get(), &view, _mipViews[slice].put());
            if (FAILED(_result))
                return _result;
            D3D11_RENDER_TARGET_VIEW_DESC target{};
            target.Format = description.Format;
            target.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
            target.Texture2D.MipSlice = slice + 1;
            ++_resourceCalls;
            _result = _device->CreateRenderTargetView(_texture.get(), &target, _mipTargets[slice].put());
            if (FAILED(_result))
                return _result;
            _mipWidths[slice] = (std::max)(1U, description.Width >> (slice + 1));
            _mipHeights[slice] = (std::max)(1U, description.Height >> (slice + 1));
            ++_mipCount;
        }
    }
    if (cancelled())
        return _result = HRESULT_FROM_WIN32(ERROR_CANCELLED);
    return _result;
}

bool GpuRasterTextureCreation::AcknowledgeOwner() noexcept
{
    if (!IsOwner())
        return false;
    State expected = State::Finished;
    return _state.compare_exchange_strong(expected, State::Acknowledged, std::memory_order_acquire);
}

ID3D11Texture2D* GpuRasterTextureCreation::TextureOwner() const noexcept
{
    return IsOwner() && _state.load(std::memory_order_acquire) == State::Acknowledged && SUCCEEDED(_result)
               ? _texture.get()
               : nullptr;
}

ID3D11ShaderResourceView* GpuRasterTextureCreation::ShaderViewOwner() const noexcept
{
    return IsOwner() && _state.load(std::memory_order_acquire) == State::Acknowledged && SUCCEEDED(_result)
               ? _view.get()
               : nullptr;
}

unsigned int GpuRasterTextureCreation::ResourceCallsOwner() const noexcept
{
    const auto state = _state.load(std::memory_order_acquire);
    return IsOwner() && (state == State::Acknowledged || state == State::Retired) ? _resourceCalls : 0;
}

HRESULT GpuRasterTextureCreation::ResultOwner() const noexcept
{
    const auto state = _state.load(std::memory_order_acquire);
    return IsOwner() && (state == State::Acknowledged || state == State::Retired) ? _result : E_PENDING;
}

bool GpuRasterTextureCreation::ReleaseOwner() noexcept
{
    const auto state = _state.load(std::memory_order_acquire);
    if (state == State::Empty)
        return true;
    if (!IsOwner() || state == State::Running)
        return false;
    if (state == State::Retired)
        return true;
    auto expected = state;
    if (!_state.compare_exchange_strong(expected, State::Retired, std::memory_order_acq_rel))
        return false; // a creator that won Captured -> Running keeps its exact device until acknowledgement
    // Finished means final member writes are published. The caller separately joins the producer; retirement
    // does not expose a failed/partial resource and permits owner cleanup on joined scheduler shutdown.
    _view.reset();
    for (auto& view : _mipViews)
        view.reset();
    for (auto& target : _mipTargets)
        target.reset();
    _mipVertex.reset();
    _mipPixel.reset();
    _mipSampler.reset();
    _mipRasterizer.reset();
    _mipDepth.reset();
    _texture.reset();
    _device.reset();
    return true;
}

bool GpuRasterTextureCreation::PrepareMipSlicesOwner() noexcept
{
    if (!IsOwner() || _state.load(std::memory_order_relaxed) != State::Captured)
        return false;
    _prepareMipSlices = true;
    return true;
}

unsigned int GpuRasterTextureCreation::MipSliceCountOwner() const noexcept
{
    return ShaderViewOwner() ? _mipCount : 0;
}

GpuMipSlice GpuRasterTextureCreation::MipSliceOwner(unsigned int slice) const noexcept
{
    return slice < MipSliceCountOwner() ? GpuMipSlice{_mipViews[slice].get(), _mipTargets[slice].get(),
                                                      _mipVertex.get(),       _mipPixel.get(),
                                                      _mipSampler.get(),      _mipRasterizer.get(),
                                                      _mipDepth.get(),        _mipWidths[slice],
                                                      _mipHeights[slice],     _generation}
                                        : GpuMipSlice{};
}

bool GpuRasterTextureCreation::SetMipViewFailureForTest(unsigned int slice, HRESULT error) noexcept
{
    if (!IsOwner() || _state.load(std::memory_order_relaxed) != State::Captured || !_prepareMipSlices ||
        slice >= kMipSliceCapacity || SUCCEEDED(error))
        return false;
    _mipFailureSlice = slice;
    _mipFailureResult = error;
    return true;
}

bool GpuRasterTextureCreation::RestrictConcurrencyForTest() noexcept
{
    if (!IsOwner() || _state.load(std::memory_order_relaxed) != State::Captured)
        return false;
    _concurrentCreates = false;
    return true;
}

bool GpuRasterTextureCreation::SetFailureForTest(GpuRasterCreationFailure point, HRESULT error) noexcept
{
    if (!IsOwner() || _state.load(std::memory_order_relaxed) != State::Captured || SUCCEEDED(error) ||
        point > GpuRasterCreationFailure::MipDepthStencil ||
        (point > GpuRasterCreationFailure::ShaderView && !_prepareMipSlices))
        return false;
    _failure = point;
    _failureResult = error;
    return true;
}
