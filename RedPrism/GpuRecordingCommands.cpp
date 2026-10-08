#include "GpuRecordingCommands.h"

#include "CompositionVertexShader.h"
#include "RasterMipPixelShader.h"

#include <algorithm>
#include <cstddef>
#include <limits>

namespace
{
unsigned int PixelBytes(DXGI_FORMAT format) noexcept
{
    switch (format)
    {
    case DXGI_FORMAT_R8_UNORM:
        return 1;
    case DXGI_FORMAT_B8G8R8A8_UNORM:
        return 4;
    case DXGI_FORMAT_R32G32B32A32_FLOAT:
        return 16;
    default:
        return 0;
    }
}
bool OnDevice(ID3D11Resource* resource, ID3D11Device* expected) noexcept
{
    if (!resource || !expected)
        return false;
    wil::com_ptr_nothrow<ID3D11Device> device;
    resource->GetDevice(device.put());
    return device.get() == expected;
}
} // namespace

HRESULT GpuRecordingCommands::InitializeForWork(ID3D11DeviceContext* context, GpuCommandStream& stream,
                                                CancelToken cancellation) noexcept
{
    if (_stream || !stream.CanRecordForWork() || !context || context->GetType() != D3D11_DEVICE_CONTEXT_DEFERRED)
        return E_INVALIDARG;
    if (cancellation.IsCancelled())
        return E_ABORT;
    _workerThread = GetCurrentThreadId();
    _stream = &stream;
    _cancellation = cancellation;
    HRESULT result = context->QueryInterface(IID_PPV_ARGS(_context.put()));
    const auto publishResult = wil::scope_exit([&]() noexcept { _result = result; });
    if (FAILED(result))
        return result;
    _context->GetDevice(_device.put());
    D3D11_FEATURE_DATA_THREADING threading{};
    result = _device->CheckFeatureSupport(D3D11_FEATURE_THREADING, &threading, sizeof(threading));
    if (FAILED(result) || !threading.DriverConcurrentCreates)
    {
        result = FAILED(result) ? result : DXGI_ERROR_UNSUPPORTED;
        return result;
    }
    const auto mayCreate = [&]() noexcept
    {
        if (SUCCEEDED(result) && cancellation.IsCancelled())
            result = E_ABORT;
        return SUCCEEDED(result);
    };
    if (mayCreate())
        result = _device->CreateVertexShader(g_CompositionVertexShader, sizeof(g_CompositionVertexShader), nullptr,
                                             _mipVertex.put());
    if (mayCreate())
        result = _device->CreatePixelShader(g_RasterMipPixelShader, sizeof(g_RasterMipPixelShader), nullptr,
                                            _mipPixel.put());
    D3D11_SAMPLER_DESC sampling{};
    sampling.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampling.AddressU = sampling.AddressV = sampling.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampling.MaxLOD = D3D11_FLOAT32_MAX;
    if (mayCreate())
        result = _device->CreateSamplerState(&sampling, _mipSampler.put());
    D3D11_RASTERIZER_DESC raster{};
    raster.FillMode = D3D11_FILL_SOLID;
    raster.CullMode = D3D11_CULL_NONE;
    raster.DepthClipEnable = TRUE;
    raster.ScissorEnable = TRUE;
    if (mayCreate())
        result = _device->CreateRasterizerState(&raster, _mipRasterizer.put());
    D3D11_DEPTH_STENCIL_DESC depth{};
    if (mayCreate())
        result = _device->CreateDepthStencilState(&depth, _mipDepth.put());
    (void)mayCreate();
    return result;
}

bool GpuRecordingCommands::IsWorker() const noexcept
{
    return _stream && _workerThread == GetCurrentThreadId();
}

HRESULT GpuRecordingCommands::StatusForWork() const noexcept
{
    if (!IsWorker())
        return HRESULT_FROM_WIN32(ERROR_INVALID_THREAD_ID);
    return SUCCEEDED(_result) && _cancellation.IsCancelled() ? E_ABORT : _result;
}

HRESULT GpuRecordingCommands::ContextStatusForWork(ID3D11Device* device, ID3D11DeviceContext* context) const noexcept
{
    const auto status = StatusForWork();
    if (FAILED(status))
        return status;
    return device && device == _device.get() && context == static_cast<ID3D11DeviceContext*>(_context.get())
               ? S_OK
               : E_INVALIDARG;
}

void GpuRecordingCommands::FailForWork(HRESULT error) noexcept
{
    if (SUCCEEDED(_result))
        _result = error;
}

void GpuRecordingCommands::CheckpointForWork() noexcept
{
    if (FAILED(StatusForWork()))
        return;
    wil::com_ptr_nothrow<ID3D11CommandList> command;
    _result = _context->FinishCommandList(FALSE, command.put());
    if (SUCCEEDED(_result))
        _result = _stream->PublishForWork(std::move(command), _cancellation);
}

void GpuRecordingCommands::UpdateForWork(ID3D11Resource* resource, unsigned int subresource, const D3D11_BOX* box,
                                         const void* data, unsigned int rowPitch, unsigned int depthPitch) noexcept
{
    if (FAILED(StatusForWork()))
        return;
    if (!resource || !data || !rowPitch || rowPitch > kUploadBytes)
    {
        FailForWork(E_INVALIDARG);
        return;
    }
    D3D11_BOX complete{};
    unsigned int bytes = 0;
    wil::com_ptr_nothrow<ID3D11Texture3D> volume;
    wil::com_ptr_nothrow<ID3D11Texture2D> texture;
    if (!OnDevice(resource, _device.get()))
    {
        FailForWork(E_INVALIDARG);
        return;
    }
    if (SUCCEEDED(resource->QueryInterface(IID_PPV_ARGS(volume.put()))))
    {
        D3D11_TEXTURE3D_DESC description{};
        volume->GetDesc(&description);
        bytes = PixelBytes(description.Format);
        if (!bytes || !depthPitch || subresource >= description.MipLevels || description.Usage != D3D11_USAGE_DEFAULT)
        {
            FailForWork(E_INVALIDARG);
            return;
        }
        complete = {0,
                    0,
                    0,
                    (std::max)(1u, description.Width >> subresource),
                    (std::max)(1u, description.Height >> subresource),
                    (std::max)(1u, description.Depth >> subresource)};
    }
    else if (SUCCEEDED(resource->QueryInterface(IID_PPV_ARGS(texture.put()))))
    {
        D3D11_TEXTURE2D_DESC description{};
        texture->GetDesc(&description);
        bytes = PixelBytes(description.Format);
        if (!bytes || description.ArraySize != 1 || subresource >= description.MipLevels ||
            description.SampleDesc.Count != 1 || description.Usage != D3D11_USAGE_DEFAULT)
        {
            FailForWork(E_INVALIDARG);
            return;
        }
        complete = {0,
                    0,
                    0,
                    (std::max)(1u, description.Width >> subresource),
                    (std::max)(1u, description.Height >> subresource),
                    1};
    }
    else
    {
        FailForWork(E_INVALIDARG);
        return;
    }
    if (!box)
        box = &complete;
    if (box->right <= box->left || box->bottom <= box->top || box->back <= box->front || box->right > complete.right ||
        box->bottom > complete.bottom || box->back > complete.back ||
        static_cast<std::uint64_t>(box->right - box->left) * bytes > rowPitch ||
        (volume && static_cast<std::uint64_t>(box->bottom - box->top) * rowPitch > depthPitch))
    {
        FailForWork(E_INVALIDARG);
        return;
    }
    const auto band = (std::max)(1u, kUploadBytes / rowPitch);
    for (UINT z = box->front; z < box->back; ++z)
        for (UINT y = box->top; y < box->bottom; y += band)
        {
            D3D11_BOX chunk = *box;
            chunk.top = y;
            chunk.bottom = (std::min)(box->bottom, y + band);
            chunk.front = z;
            chunk.back = z + 1;
            const auto* source = static_cast<const std::uint8_t*>(data) +
                                 static_cast<std::size_t>(z - box->front) * depthPitch +
                                 static_cast<std::size_t>(y - box->top) * rowPitch;
            const auto chunkBytes = static_cast<std::uint64_t>(chunk.bottom - chunk.top) * rowPitch;
            if (_uploadBytes > (std::numeric_limits<std::uint64_t>::max)() - chunkBytes)
            {
                FailForWork(HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW));
                return;
            }
            // Context1 fixes software command-list source offsets (including WARP DriverCommandLists=false).
            _context->UpdateSubresource1(resource, subresource, &chunk, source, rowPitch, depthPitch, 0);
            _uploadBytes += chunkBytes;
            CheckpointForWork();
            if (FAILED(StatusForWork()))
                return;
        }
}

void GpuRecordingCommands::ClearForWork(ID3D11RenderTargetView* target, const float* color) noexcept
{
    if (FAILED(StatusForWork()))
        return;
    if (!target || !color)
    {
        FailForWork(E_INVALIDARG);
        return;
    }
    wil::com_ptr_nothrow<ID3D11Resource> resource;
    target->GetResource(resource.put());
    wil::com_ptr_nothrow<ID3D11Texture2D> texture;
    const HRESULT result = resource->QueryInterface(IID_PPV_ARGS(texture.put()));
    if (FAILED(result))
    {
        FailForWork(result);
        return;
    }
    D3D11_TEXTURE2D_DESC description{};
    texture->GetDesc(&description);
    D3D11_RENDER_TARGET_VIEW_DESC view{};
    target->GetDesc(&view);
    if (!OnDevice(resource.get(), _device.get()) || !PixelBytes(description.Format) || description.ArraySize != 1 ||
        description.SampleDesc.Count != 1 || view.ViewDimension != D3D11_RTV_DIMENSION_TEXTURE2D ||
        view.Texture2D.MipSlice >= description.MipLevels)
    {
        FailForWork(E_INVALIDARG);
        return;
    }
    const auto width = (std::max)(1u, description.Width >> view.Texture2D.MipSlice);
    const auto height = (std::max)(1u, description.Height >> view.Texture2D.MipSlice);
    for (UINT y = 0; y < height; y += kTileSide)
        for (UINT x = 0; x < width; x += kTileSide)
        {
            const D3D11_RECT rectangle{static_cast<LONG>(x), static_cast<LONG>(y),
                                       static_cast<LONG>((std::min)(width, x + kTileSide)),
                                       static_cast<LONG>((std::min)(height, y + kTileSide))};
            _context->ClearView(target, color, &rectangle, 1);
            CheckpointForWork();
            if (FAILED(StatusForWork()))
                return;
        }
}

void GpuRecordingCommands::CopyForWork(ID3D11Resource* destination, unsigned int destinationSubresource,
                                       unsigned int destinationX, unsigned int destinationY, unsigned int destinationZ,
                                       ID3D11Resource* source, unsigned int sourceSubresource,
                                       const D3D11_BOX* region) noexcept
{
    if (FAILED(StatusForWork()))
        return;
    if (!destination || !source || !region || destinationZ || region->front || region->back != 1 ||
        region->right <= region->left || region->bottom <= region->top)
    {
        FailForWork(E_INVALIDARG);
        return;
    }
    wil::com_ptr_nothrow<ID3D11Texture2D> sourceTexture, destinationTexture;
    if (!OnDevice(source, _device.get()) || !OnDevice(destination, _device.get()) ||
        FAILED(source->QueryInterface(IID_PPV_ARGS(sourceTexture.put()))) ||
        FAILED(destination->QueryInterface(IID_PPV_ARGS(destinationTexture.put()))))
    {
        FailForWork(E_INVALIDARG);
        return;
    }
    D3D11_TEXTURE2D_DESC sourceDescription{}, destinationDescription{};
    sourceTexture->GetDesc(&sourceDescription);
    destinationTexture->GetDesc(&destinationDescription);
    if (!PixelBytes(sourceDescription.Format) || sourceDescription.Format != destinationDescription.Format ||
        sourceDescription.ArraySize != 1 || destinationDescription.ArraySize != 1 ||
        sourceDescription.SampleDesc.Count != 1 || destinationDescription.SampleDesc.Count != 1 ||
        sourceSubresource >= sourceDescription.MipLevels ||
        destinationSubresource >= destinationDescription.MipLevels ||
        (source == destination && sourceSubresource == destinationSubresource))
    {
        FailForWork(E_INVALIDARG);
        return;
    }
    const auto sourceWidth = (std::max)(1u, sourceDescription.Width >> sourceSubresource);
    const auto sourceHeight = (std::max)(1u, sourceDescription.Height >> sourceSubresource);
    const auto destinationWidth = (std::max)(1u, destinationDescription.Width >> destinationSubresource);
    const auto destinationHeight = (std::max)(1u, destinationDescription.Height >> destinationSubresource);
    if (region->right > sourceWidth || region->bottom > sourceHeight || destinationX > destinationWidth ||
        destinationY > destinationHeight || region->right - region->left > destinationWidth - destinationX ||
        region->bottom - region->top > destinationHeight - destinationY)
    {
        FailForWork(E_INVALIDARG);
        return;
    }
    for (UINT y = region->top; y < region->bottom; y += kTileSide)
        for (UINT x = region->left; x < region->right; x += kTileSide)
        {
            const D3D11_BOX tile{
                x, y, 0, (std::min)(region->right, x + kTileSide), (std::min)(region->bottom, y + kTileSide), 1};
            _context->CopySubresourceRegion(destination, destinationSubresource, destinationX + x - region->left,
                                            destinationY + y - region->top, 0, source, sourceSubresource, &tile);
            CheckpointForWork();
            if (FAILED(StatusForWork()))
                return;
        }
}

void GpuRecordingCommands::GenerateMipsForWork(ID3D11ShaderResourceView* view) noexcept
{
    if (FAILED(StatusForWork()))
        return;
    if (!view)
    {
        FailForWork(E_INVALIDARG);
        return;
    }
    wil::com_ptr_nothrow<ID3D11Resource> resource;
    view->GetResource(resource.put());
    wil::com_ptr_nothrow<ID3D11Texture2D> texture;
    HRESULT result = resource->QueryInterface(IID_PPV_ARGS(texture.put()));
    if (FAILED(result))
    {
        FailForWork(result);
        return;
    }
    D3D11_TEXTURE2D_DESC description{};
    texture->GetDesc(&description);
    D3D11_SHADER_RESOURCE_VIEW_DESC requested{};
    view->GetDesc(&requested);
    if (!OnDevice(resource.get(), _device.get()) || !PixelBytes(description.Format) || description.ArraySize != 1 ||
        description.SampleDesc.Count != 1 || requested.ViewDimension != D3D11_SRV_DIMENSION_TEXTURE2D ||
        requested.Texture2D.MostDetailedMip != 0 ||
        (requested.Texture2D.MipLevels != description.MipLevels && requested.Texture2D.MipLevels != UINT_MAX))
    {
        FailForWork(E_INVALIDARG);
        return;
    }
    for (UINT level = 1; level < description.MipLevels; ++level)
    {
        D3D11_SHADER_RESOURCE_VIEW_DESC sourceDescription{};
        sourceDescription.Format = description.Format;
        sourceDescription.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        sourceDescription.Texture2D.MostDetailedMip = level - 1;
        sourceDescription.Texture2D.MipLevels = 1;
        wil::com_ptr_nothrow<ID3D11ShaderResourceView> source;
        result = _device->CreateShaderResourceView(texture.get(), &sourceDescription, source.put());
        D3D11_RENDER_TARGET_VIEW_DESC targetDescription{};
        targetDescription.Format = description.Format;
        targetDescription.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
        targetDescription.Texture2D.MipSlice = level;
        wil::com_ptr_nothrow<ID3D11RenderTargetView> target;
        if (SUCCEEDED(result))
            result = _device->CreateRenderTargetView(texture.get(), &targetDescription, target.put());
        if (FAILED(result))
        {
            FailForWork(result);
            return;
        }
        const UINT width = (std::max)(1u, description.Width >> level);
        const UINT height = (std::max)(1u, description.Height >> level);
        for (UINT y = 0; y < height; y += kTileSide)
            for (UINT x = 0; x < width; x += kTileSide)
            {
                ID3D11ShaderResourceView* sources[] = {source.get()};
                ID3D11RenderTargetView* targets[] = {target.get()};
                ID3D11SamplerState* samplers[] = {_mipSampler.get()};
                const D3D11_VIEWPORT viewport{0, 0, static_cast<float>(width), static_cast<float>(height), 0, 1};
                const D3D11_RECT rectangle{static_cast<LONG>(x), static_cast<LONG>(y),
                                           static_cast<LONG>((std::min)(width, x + kTileSide)),
                                           static_cast<LONG>((std::min)(height, y + kTileSide))};
                _context->IASetInputLayout(nullptr);
                _context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                _context->VSSetShader(_mipVertex.get(), nullptr, 0);
                _context->PSSetShader(_mipPixel.get(), nullptr, 0);
                _context->RSSetState(_mipRasterizer.get());
                _context->RSSetViewports(1, &viewport);
                _context->RSSetScissorRects(1, &rectangle);
                _context->OMSetDepthStencilState(_mipDepth.get(), 0);
                _context->OMSetBlendState(nullptr, nullptr, 0xffffffffU);
                _context->OMSetRenderTargets(1, targets, nullptr);
                _context->PSSetShaderResources(0, 1, sources);
                _context->PSSetSamplers(0, 1, samplers);
                _context->Draw(3, 0);
                sources[0] = nullptr;
                _context->PSSetShaderResources(0, 1, sources);
                _context->OMSetRenderTargets(0, nullptr, nullptr);
                CheckpointForWork();
                if (FAILED(StatusForWork()))
                    return;
            }
    }
}
