#include "CanvasRenderer.h"

#include "CompositionBlitPixelShader.h"
#include "CompositionPixelShader.h"
#include "CompositionVertexShader.h"

#include <array>
#include <cmath>
#include <cstring>

namespace
{
constexpr std::array<float, 4> kClearZero{0, 0, 0, 0};

[[nodiscard]] D3D11_VIEWPORT MakeViewport(UINT width, UINT height) noexcept
{
    D3D11_VIEWPORT viewport{};
    viewport.Width = static_cast<float>(width);
    viewport.Height = static_cast<float>(height);
    viewport.MaxDepth = 1.0f;
    return viewport;
}
} // namespace

CanvasRenderer::~CanvasRenderer()
{
    ReleaseDeviceResources();
}

void CanvasRenderer::ReleaseDocumentSurfaces() noexcept
{
    _rasters.clear();
    _ping = {};
    _pong = {};
    _pingRtv.reset();
    _pongRtv.reset();
    _canvasWidth = 0;
    _canvasHeight = 0;
    _pingIsFront = true;
}

void CanvasRenderer::ReleaseDeviceResources() noexcept
{
    ReleaseDocumentSurfaces();
    _layerConstants.reset();
    _blitConstants.reset();
    _rasterizer.reset();
    _pointSampler.reset();
    _linearSampler.reset();
    _blitPixelShader.reset();
    _pixelShader.reset();
    _vertexShader.reset();
    _context = nullptr;
    _device = nullptr;
}

HRESULT CanvasRenderer::CreateDeviceResources(ID3D11Device* device, ID3D11DeviceContext* context) noexcept
{
    static_assert(sizeof(LayerConstants) % 16 == 0);
    static_assert(sizeof(BlitConstants) % 16 == 0);
    ReleaseDeviceResources();
    if (!device || !context)
    {
        return E_INVALIDARG;
    }
    _device = device;
    _context = context;

    HRESULT result = device->CreateVertexShader(g_CompositionVertexShader, sizeof(g_CompositionVertexShader), nullptr,
                                                _vertexShader.put());
    if (FAILED(result))
    {
        return result;
    }
    result = device->CreatePixelShader(g_CompositionPixelShader, sizeof(g_CompositionPixelShader), nullptr,
                                       _pixelShader.put());
    if (FAILED(result))
    {
        return result;
    }
    result = device->CreatePixelShader(g_CompositionBlitPixelShader, sizeof(g_CompositionBlitPixelShader), nullptr,
                                       _blitPixelShader.put());
    if (FAILED(result))
    {
        return result;
    }

    D3D11_SAMPLER_DESC sampler{};
    sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler.MaxLOD = D3D11_FLOAT32_MAX;
    result = device->CreateSamplerState(&sampler, _linearSampler.put());
    if (FAILED(result))
    {
        return result;
    }
    sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    result = device->CreateSamplerState(&sampler, _pointSampler.put());
    if (FAILED(result))
    {
        return result;
    }

    D3D11_RASTERIZER_DESC raster{};
    raster.FillMode = D3D11_FILL_SOLID;
    raster.CullMode = D3D11_CULL_NONE;
    raster.DepthClipEnable = TRUE;
    result = device->CreateRasterizerState(&raster, _rasterizer.put());
    if (FAILED(result))
    {
        return result;
    }

    D3D11_BUFFER_DESC constants{};
    constants.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    constants.ByteWidth = sizeof(LayerConstants);
    constants.Usage = D3D11_USAGE_DYNAMIC;
    constants.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    result = device->CreateBuffer(&constants, nullptr, _layerConstants.put());
    if (FAILED(result))
    {
        return result;
    }
    constants.ByteWidth = sizeof(BlitConstants);
    return device->CreateBuffer(&constants, nullptr, _blitConstants.put());
}

HRESULT CanvasRenderer::CreateCanvas(UINT width, UINT height) noexcept
{
    if (width == _canvasWidth && height == _canvasHeight && _ping.texture && _pong.texture)
    {
        return S_OK;
    }
    _ping = {};
    _pong = {};
    _pingRtv.reset();
    _pongRtv.reset();
    _canvasWidth = 0;
    _canvasHeight = 0;
    if (width == 0 || height == 0)
    {
        return S_OK;
    }

    D3D11_TEXTURE2D_DESC description{};
    description.Width = width;
    description.Height = height;
    description.MipLevels = 1;
    description.ArraySize = 1;
    description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    description.SampleDesc.Count = 1;
    description.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

    HRESULT result = _device->CreateTexture2D(&description, nullptr, _ping.texture.put());
    if (FAILED(result))
    {
        return result;
    }
    result = _device->CreateTexture2D(&description, nullptr, _pong.texture.put());
    if (FAILED(result))
    {
        return result;
    }
    result = _device->CreateShaderResourceView(_ping.texture.get(), nullptr, _ping.srv.put());
    if (FAILED(result))
    {
        return result;
    }
    result = _device->CreateShaderResourceView(_pong.texture.get(), nullptr, _pong.srv.put());
    if (FAILED(result))
    {
        return result;
    }
    result = _device->CreateRenderTargetView(_ping.texture.get(), nullptr, _pingRtv.put());
    if (FAILED(result))
    {
        return result;
    }
    result = _device->CreateRenderTargetView(_pong.texture.get(), nullptr, _pongRtv.put());
    if (FAILED(result))
    {
        return result;
    }
    _ping.width = static_cast<int>(width);
    _ping.height = static_cast<int>(height);
    _pong.width = static_cast<int>(width);
    _pong.height = static_cast<int>(height);
    _canvasWidth = width;
    _canvasHeight = height;
    _pingIsFront = true;
    return S_OK;
}

HRESULT CanvasRenderer::UploadRaster(const Raster& raster, GpuTexture& gpu, bool gray) noexcept
{
    gpu = {};
    if (!raster.HasStorage() || raster.Data() == nullptr)
    {
        return E_INVALIDARG;
    }
    D3D11_TEXTURE2D_DESC description{};
    description.Width = static_cast<UINT>(raster.Width());
    description.Height = static_cast<UINT>(raster.Height());
    description.MipLevels = 0;
    description.ArraySize = 1;
    description.Format = gray ? DXGI_FORMAT_R8_UNORM : DXGI_FORMAT_B8G8R8A8_UNORM;
    description.SampleDesc.Count = 1;
    description.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    description.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;
    HRESULT result = _device->CreateTexture2D(&description, nullptr, gpu.texture.put());
    if (FAILED(result))
    {
        description.MipLevels = 1;
        description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        description.MiscFlags = 0;
        result = _device->CreateTexture2D(&description, nullptr, gpu.texture.put());
        if (FAILED(result))
        {
            return result;
        }
        D3D11_BOX box{0, 0, 0, description.Width, description.Height, 1};
        _context->UpdateSubresource(gpu.texture.get(), 0, &box, raster.Data(), static_cast<UINT>(raster.Stride()), 0);
    }
    else
    {
        D3D11_BOX box{0, 0, 0, description.Width, description.Height, 1};
        _context->UpdateSubresource(gpu.texture.get(), 0, &box, raster.Data(), static_cast<UINT>(raster.Stride()), 0);
    }
    result = _device->CreateShaderResourceView(gpu.texture.get(), nullptr, gpu.srv.put());
    if (FAILED(result))
    {
        return result;
    }
    if ((description.MiscFlags & D3D11_RESOURCE_MISC_GENERATE_MIPS) != 0)
    {
        _context->GenerateMips(gpu.srv.get());
    }
    gpu.width = raster.Width();
    gpu.height = raster.Height();
    return S_OK;
}

CanvasRenderer::GpuTexture* CanvasRenderer::TextureFor(const Raster& raster, bool gray) noexcept
{
    const void* identity = raster.Identity();
    if (identity == nullptr)
    {
        return nullptr;
    }
    GpuTexture& gpu = _rasters[identity];
    if (!gpu.srv)
    {
        if (FAILED(UploadRaster(raster, gpu, gray)))
        {
            _rasters.erase(identity);
            return nullptr;
        }
    }
    return &gpu;
}

HRESULT CanvasRenderer::DrawLayer(const ImageLayer& layer, const ImageLayer* liveSource,
                                    const ImageLayer* folderSource, ID3D11RenderTargetView* destRtv,
                                    ID3D11ShaderResourceView* destSrv) noexcept
{
    if (!layer.asset.has_value() || !layer.visible)
    {
        return S_OK;
    }
    GpuTexture* image = TextureFor(layer.asset.value().image, false);
    if (image == nullptr || image->srv == nullptr)
    {
        return E_FAIL;
    }
    GpuTexture* mask = nullptr;
    if (layer.mask.has_value() && layer.mask.value().enabled && layer.mask.value().asset.image.HasStorage())
    {
        mask = TextureFor(layer.mask.value().asset.image, true);
    }
    GpuTexture* live = nullptr;
    if (liveSource != nullptr && liveSource->asset.has_value())
    {
        live = TextureFor(liveSource->asset.value().image, false);
    }
    GpuTexture* folder = nullptr;
    if (folderSource != nullptr && folderSource->mask.has_value() && folderSource->mask.value().enabled &&
        folderSource->mask.value().asset.image.HasStorage())
    {
        folder = TextureFor(folderSource->mask.value().asset.image, true);
    }

    LayerConstants constants{};
    constants.canvasSize[0] = static_cast<float>(_canvasWidth);
    constants.canvasSize[1] = static_cast<float>(_canvasHeight);
    const Vec2 center = layer.transform.Center();
    constants.layerCenter[0] = static_cast<float>(center.x);
    constants.layerCenter[1] = static_cast<float>(center.y);
    constants.layerSize[0] = static_cast<float>(layer.transform.size.x);
    constants.layerSize[1] = static_cast<float>(layer.transform.size.y);
    const double radians = layer.transform.rotation * 3.14159265358979323846 / 180.0;
    constants.rotationSin = static_cast<float>(std::sin(radians));
    constants.rotationCos = static_cast<float>(std::cos(radians));
    constants.flipSign[0] = layer.transform.flipX ? -1.0f : 1.0f;
    constants.flipSign[1] = layer.transform.flipY ? -1.0f : 1.0f;
    constants.blendMode = static_cast<std::uint32_t>(layer.blendMode);
    constants.filterMode = layer.transform.sampling == LayerSampling::Nearest ? 0u : 1u;
    constants.opacity = static_cast<float>(layer.opacity);
    constants.hasMask = mask && mask->srv ? 1u : 0u;
    constants.hasLiveMask = live && live->srv ? 1u : 0u;
    constants.hasFolderMask = folder && folder->srv ? 1u : 0u;

    D3D11_MAPPED_SUBRESOURCE mapped{};
    HRESULT result = _context->Map(_layerConstants.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (FAILED(result))
    {
        return result;
    }
    std::memcpy(mapped.pData, &constants, sizeof(constants));
    _context->Unmap(_layerConstants.get(), 0);

    ID3D11RenderTargetView* views[] = {destRtv};
    _context->OMSetRenderTargets(1, views, nullptr);
    const D3D11_VIEWPORT viewport = MakeViewport(_canvasWidth, _canvasHeight);
    _context->RSSetViewports(1, &viewport);
    _context->RSSetState(_rasterizer.get());
    _context->IASetInputLayout(nullptr);
    _context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    _context->VSSetShader(_vertexShader.get(), nullptr, 0);
    _context->PSSetShader(_pixelShader.get(), nullptr, 0);
    ID3D11Buffer* buffers[] = {_layerConstants.get()};
    _context->VSSetConstantBuffers(0, 1, buffers);
    _context->PSSetConstantBuffers(0, 1, buffers);
    ID3D11ShaderResourceView* srvs[] = {image->srv.get(), destSrv, mask && mask->srv ? mask->srv.get() : nullptr,
                                        live && live->srv ? live->srv.get() : nullptr,
                                        folder && folder->srv ? folder->srv.get() : nullptr};
    _context->PSSetShaderResources(0, 5, srvs);
    ID3D11SamplerState* samplers[] = {_linearSampler.get(), _pointSampler.get()};
    _context->PSSetSamplers(0, 2, samplers);
    _context->Draw(3, 0);
    ID3D11ShaderResourceView* none[] = {nullptr, nullptr, nullptr, nullptr, nullptr};
    _context->PSSetShaderResources(0, 5, none);
    _context->OMSetRenderTargets(0, nullptr, nullptr);
    return S_OK;
}

HRESULT CanvasRenderer::Compose(const CanvasDocument& document) noexcept
{
    if (!_device || !_context)
    {
        return E_UNEXPECTED;
    }
    HRESULT result = CreateCanvas(static_cast<UINT>(document.width), static_cast<UINT>(document.height));
    if (FAILED(result) || _canvasWidth == 0)
    {
        return result;
    }
    _context->ClearRenderTargetView(_pingRtv.get(), kClearZero.data());
    _context->ClearRenderTargetView(_pongRtv.get(), kClearZero.data());
    _pingIsFront = true;

    std::unordered_map<Guid, const ImageLayer*> byId;
    for (const ImageLayer& layer : document.layers)
    {
        byId.emplace(layer.id, &layer);
    }

    for (const Guid& id : RenderLayerIds(document))
    {
        const auto found = byId.find(id);
        if (found == byId.end())
        {
            continue;
        }
        const ImageLayer& layer = *found->second;
        if (layer.isGroup || layer.adjustment.has_value() || !layer.asset.has_value() || !layer.visible)
        {
            continue;
        }
        const ImageLayer* live = nullptr;
        if (layer.maskSourceID.has_value())
        {
            const auto source = byId.find(layer.maskSourceID.value());
            if (source != byId.end())
            {
                live = source->second;
            }
        }
        const ImageLayer* folder = nullptr;
        std::optional<Guid> parent = layer.parentID;
        while (parent.has_value())
        {
            const auto foundParent = byId.find(parent.value());
            if (foundParent == byId.end())
            {
                break;
            }
            if (foundParent->second->mask.has_value() && foundParent->second->mask.value().enabled)
            {
                folder = foundParent->second;
                break;
            }
            parent = foundParent->second->parentID;
        }
        GpuTexture& src = _pingIsFront ? _ping : _pong;
        GpuTexture& dst = _pingIsFront ? _pong : _ping;
        ID3D11RenderTargetView* destRtv = _pingIsFront ? _pongRtv.get() : _pingRtv.get();
        result = DrawLayer(layer, live, folder, destRtv, src.srv.get());
        if (FAILED(result))
        {
            return result;
        }
        _pingIsFront = !_pingIsFront;
        (void)dst;
    }
    return S_OK;
}

HRESULT CanvasRenderer::BlitToTarget(ID3D11RenderTargetView* target, UINT targetWidth, UINT targetHeight,
                                       const CanvasViewport& viewport, float holeLeft, float holeTop, float holeWidth,
                                       float holeHeight) noexcept
{
    if (!_context || !target || !_ping.srv)
    {
        return S_OK;
    }
    GpuTexture& front = _pingIsFront ? _ping : _pong;
    BlitConstants constants{};
    constants.viewSize[0] = static_cast<float>(targetWidth);
    constants.viewSize[1] = static_cast<float>(targetHeight);
    constants.holeOrigin[0] = holeLeft;
    constants.holeOrigin[1] = holeTop;
    constants.holeSize[0] = holeWidth;
    constants.holeSize[1] = holeHeight;
    constants.documentSize[0] = static_cast<float>(_canvasWidth);
    constants.documentSize[1] = static_cast<float>(_canvasHeight);
    constants.pan[0] = static_cast<float>(viewport.pan.x);
    constants.pan[1] = static_cast<float>(viewport.pan.y);
    constants.zoom = static_cast<float>(viewport.PointsPerPixel());
    constants.showGrid = viewport.ShowsPixelGrid() ? 1.0f : 0.0f;

    D3D11_MAPPED_SUBRESOURCE mapped{};
    HRESULT result = _context->Map(_blitConstants.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (FAILED(result))
    {
        return result;
    }
    std::memcpy(mapped.pData, &constants, sizeof(constants));
    _context->Unmap(_blitConstants.get(), 0);

    ID3D11RenderTargetView* views[] = {target};
    _context->OMSetRenderTargets(1, views, nullptr);
    const D3D11_VIEWPORT view = MakeViewport(targetWidth, targetHeight);
    _context->RSSetViewports(1, &view);
    _context->RSSetState(_rasterizer.get());
    _context->IASetInputLayout(nullptr);
    _context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    _context->VSSetShader(_vertexShader.get(), nullptr, 0);
    _context->PSSetShader(_blitPixelShader.get(), nullptr, 0);
    ID3D11Buffer* buffers[] = {_blitConstants.get()};
    _context->VSSetConstantBuffers(0, 1, buffers);
    _context->PSSetConstantBuffers(0, 1, buffers);
    ID3D11ShaderResourceView* srvs[] = {front.srv.get()};
    _context->PSSetShaderResources(0, 1, srvs);
    ID3D11SamplerState* samplers[] = {_linearSampler.get(), _pointSampler.get()};
    _context->PSSetSamplers(0, 2, samplers);
    _context->Draw(3, 0);
    ID3D11ShaderResourceView* none[] = {nullptr};
    _context->PSSetShaderResources(0, 1, none);
    return S_OK;
}

HRESULT CanvasRenderer::Readback(std::vector<std::uint8_t>& bgra, UINT& width, UINT& height) noexcept
{
    bgra.clear();
    width = _canvasWidth;
    height = _canvasHeight;
    if (!_context || !_canvasWidth)
    {
        return E_UNEXPECTED;
    }
    GpuTexture& front = _pingIsFront ? _ping : _pong;
    D3D11_TEXTURE2D_DESC description{};
    front.texture->GetDesc(&description);
    description.BindFlags = 0;
    description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    description.Usage = D3D11_USAGE_STAGING;
    description.MiscFlags = 0;
    wil::com_ptr_nothrow<ID3D11Texture2D> staging;
    HRESULT result = _device->CreateTexture2D(&description, nullptr, staging.put());
    if (FAILED(result))
    {
        return result;
    }
    _context->CopyResource(staging.get(), front.texture.get());
    D3D11_MAPPED_SUBRESOURCE mapped{};
    result = _context->Map(staging.get(), 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(result))
    {
        return result;
    }
    bgra.resize(static_cast<std::size_t>(_canvasWidth) * static_cast<std::size_t>(_canvasHeight) * 4);
    for (UINT y = 0; y < _canvasHeight; ++y)
    {
        std::memcpy(bgra.data() + static_cast<std::size_t>(y) * _canvasWidth * 4,
                    static_cast<const std::uint8_t*>(mapped.pData) + static_cast<std::size_t>(y) * mapped.RowPitch,
                    static_cast<std::size_t>(_canvasWidth) * 4);
    }
    _context->Unmap(staging.get(), 0);
    return S_OK;
}

std::uint64_t CanvasRenderer::CanvasSurfaceBytes() const noexcept
{
    if (_canvasWidth == 0 || _canvasHeight == 0)
    {
        return 0;
    }
    return 2ull * static_cast<std::uint64_t>(_canvasWidth) * static_cast<std::uint64_t>(_canvasHeight) * 4ull;
}
