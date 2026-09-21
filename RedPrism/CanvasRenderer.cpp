#include "CanvasRenderer.h"

#include "CompositionBlitPixelShader.h"
#include "CompositionPixelShader.h"
#include "CompositionVertexShader.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace
{
constexpr std::array<float, 4> kClearZero{0, 0, 0, 0};
constexpr double kPi = 3.14159265358979323846;

[[nodiscard]] D3D11_VIEWPORT MakeViewport(UINT width, UINT height) noexcept
{
    D3D11_VIEWPORT viewport{};
    viewport.Width = static_cast<float>(width);
    viewport.Height = static_cast<float>(height);
    viewport.MaxDepth = 1.0f;
    return viewport;
}

void FillTransform(const LayerTransform& transform, float* center, float* size, float* sinCos, float* flip) noexcept
{
    const Vec2 middle = transform.Center();
    center[0] = static_cast<float>(middle.x);
    center[1] = static_cast<float>(middle.y);
    size[0] = static_cast<float>(transform.size.x);
    size[1] = static_cast<float>(transform.size.y);
    const double radians = std::fmod(transform.rotation, 360.0) * kPi / 180.0;
    sinCos[0] = static_cast<float>(std::sin(radians));
    sinCos[1] = static_cast<float>(std::cos(radians));
    flip[0] = transform.flipX ? -1.0f : 1.0f;
    flip[1] = transform.flipY ? -1.0f : 1.0f;
}

[[nodiscard]] bool HasEnabledMask(const ImageLayer& layer) noexcept
{
    return layer.mask.has_value() && layer.mask.value().enabled && layer.mask.value().asset.image.HasStorage();
}
} // namespace

CanvasRenderer::~CanvasRenderer()
{
    ReleaseDeviceResources();
}

void CanvasRenderer::ReleaseDocumentSurfaces() noexcept
{
    _rasters.clear();
    _canvas[0] = {};
    _canvas[1] = {};
    _stack[0] = {};
    _stack[1] = {};
    _coverage.clear();
    _coverageUsed = 0;
    _byId.clear();
    _liveCoverageIndex.clear();
    _groupCoverageIndex.clear();
    _renderIds.clear();
    _consumed.clear();
    _canvasWidth = 0;
    _canvasHeight = 0;
    _front = 0;
    _stackFront = 0;
    _documentDirty = true;
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
    result = device->CreateBuffer(&constants, nullptr, _blitConstants.put());
    if (FAILED(result))
    {
        return result;
    }
    _documentDirty = true;
    return S_OK;
}

HRESULT CanvasRenderer::CreateSurface(UINT width, UINT height, DXGI_FORMAT format, Surface& surface) noexcept
{
    surface = {};
    D3D11_TEXTURE2D_DESC description{};
    description.Width = width;
    description.Height = height;
    description.MipLevels = 1;
    description.ArraySize = 1;
    description.Format = format;
    description.SampleDesc.Count = 1;
    description.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    HRESULT result = _device->CreateTexture2D(&description, nullptr, surface.texture.texture.put());
    if (FAILED(result))
    {
        return result;
    }
    result = _device->CreateShaderResourceView(surface.texture.texture.get(), nullptr, surface.texture.srv.put());
    if (FAILED(result))
    {
        return result;
    }
    result = _device->CreateRenderTargetView(surface.texture.texture.get(), nullptr, surface.rtv.put());
    if (FAILED(result))
    {
        return result;
    }
    surface.texture.width = static_cast<int>(width);
    surface.texture.height = static_cast<int>(height);
    return S_OK;
}

HRESULT CanvasRenderer::CreateCanvas(UINT width, UINT height) noexcept
{
    if (width == _canvasWidth && height == _canvasHeight && _canvas[0].texture.texture && _canvas[1].texture.texture)
    {
        return S_OK;
    }
    _canvas[0] = {};
    _canvas[1] = {};
    _stack[0] = {};
    _stack[1] = {};
    _coverage.clear();
    _coverageUsed = 0;
    _canvasWidth = 0;
    _canvasHeight = 0;
    _documentDirty = true;
    if (width == 0 || height == 0)
    {
        return S_OK;
    }
    HRESULT result = CreateSurface(width, height, DXGI_FORMAT_B8G8R8A8_UNORM, _canvas[0]);
    if (FAILED(result))
    {
        return result;
    }
    result = CreateSurface(width, height, DXGI_FORMAT_B8G8R8A8_UNORM, _canvas[1]);
    if (FAILED(result))
    {
        _canvas[0] = {};
        return result;
    }
    _canvasWidth = width;
    _canvasHeight = height;
    _front = 0;
    return S_OK;
}

HRESULT CanvasRenderer::EnsureStackSurfaces() noexcept
{
    if (_stack[0].texture.texture && _stack[1].texture.texture)
    {
        return S_OK;
    }
    HRESULT result = CreateSurface(_canvasWidth, _canvasHeight, DXGI_FORMAT_B8G8R8A8_UNORM, _stack[0]);
    if (FAILED(result))
    {
        return result;
    }
    result = CreateSurface(_canvasWidth, _canvasHeight, DXGI_FORMAT_B8G8R8A8_UNORM, _stack[1]);
    if (FAILED(result))
    {
        _stack[0] = {};
    }
    return result;
}

HRESULT CanvasRenderer::AcquireCoverage(int& index) noexcept
{
    index = kNoCoverage;
    if (_coverageUsed >= _coverage.size())
    {
        Surface surface;
        const HRESULT result = CreateSurface(_canvasWidth, _canvasHeight, DXGI_FORMAT_R8_UNORM, surface);
        if (FAILED(result))
        {
            return result;
        }
        _coverage.push_back(std::move(surface));
    }
    index = static_cast<int>(_coverageUsed);
    ++_coverageUsed;
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
    }
    const D3D11_BOX box{0, 0, 0, description.Width, description.Height, 1};
    _context->UpdateSubresource(gpu.texture.get(), 0, &box, raster.Data(), static_cast<UINT>(raster.Stride()), 0);
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
        gpu.keepAlive = raster;
    }
    gpu.lastUse = _composeId;
    return &gpu;
}

HRESULT CanvasRenderer::RunPass(const LayerConstants& constants, const PassInputs& inputs,
                                  ID3D11RenderTargetView* target) noexcept
{
    D3D11_MAPPED_SUBRESOURCE mapped{};
    const HRESULT result = _context->Map(_layerConstants.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (FAILED(result))
    {
        return result;
    }
    std::memcpy(mapped.pData, &constants, sizeof(constants));
    _context->Unmap(_layerConstants.get(), 0);

    ID3D11RenderTargetView* views[] = {target};
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
    ID3D11ShaderResourceView* srvs[] = {inputs.source, inputs.dest, inputs.mask, inputs.live, inputs.folder};
    _context->PSSetShaderResources(0, 5, srvs);
    ID3D11SamplerState* samplers[] = {_linearSampler.get(), _pointSampler.get()};
    _context->PSSetSamplers(0, 2, samplers);
    _context->Draw(3, 0);
    ID3D11ShaderResourceView* none[] = {nullptr, nullptr, nullptr, nullptr, nullptr};
    _context->PSSetShaderResources(0, 5, none);
    _context->OMSetRenderTargets(0, nullptr, nullptr);
    return S_OK;
}

void CanvasRenderer::FillLayerConstants(const ImageLayer& layer, LayerConstants& constants, PassInputs& inputs,
                                          bool& ok) noexcept
{
    ok = false;
    constants = {};
    constants.canvasSize[0] = static_cast<float>(_canvasWidth);
    constants.canvasSize[1] = static_cast<float>(_canvasHeight);
    if (!layer.asset.has_value())
    {
        return;
    }
    GpuTexture* image = TextureFor(layer.asset.value().image, false);
    if (image == nullptr || !image->srv)
    {
        return;
    }
    inputs.source = image->srv.get();
    FillTransform(layer.transform, constants.layerCenter, constants.layerSize, constants.layerSinCos,
                  constants.layerFlip);
    constants.blendMode = static_cast<std::uint32_t>(layer.blendMode);
    constants.filterMode = layer.transform.sampling == LayerSampling::Nearest ? 0u : 1u;
    constants.opacity = static_cast<float>(layer.opacity);
    if (HasEnabledMask(layer))
    {
        const LayerMask& mask = layer.mask.value();
        GpuTexture* maskTexture = TextureFor(mask.asset.image, true);
        if (maskTexture != nullptr && maskTexture->srv)
        {
            inputs.mask = maskTexture->srv.get();
            constants.flags |= kFlagHasMask;
            if (mask.placement.has_value())
            {
                constants.flags |= kFlagMaskPlaced;
                FillTransform(mask.placement.value(), constants.maskCenter, constants.maskSize, constants.maskSinCos,
                              constants.maskFlip);
                constants.maskBackground = static_cast<float>(MaskEdgeBackground(mask.asset.thumbnail));
            }
        }
    }
    ok = true;
}

const ImageLayer* CanvasRenderer::Find(const Guid& id) const noexcept
{
    const auto found = _byId.find(id);
    return found == _byId.end() ? nullptr : found->second;
}

HRESULT CanvasRenderer::LayerCoverage(const Guid& id, int depth, int& index) noexcept
{
    index = kNoCoverage;
    const auto cached = _liveCoverageIndex.find(id);
    if (cached != _liveCoverageIndex.end())
    {
        index = cached->second;
        return S_OK;
    }
    if (depth > kMaxLiveMaskPath)
    {
        return S_OK;
    }
    const ImageLayer* layer = Find(id);
    HRESULT result = AcquireCoverage(index);
    if (FAILED(result))
    {
        return result;
    }
    _liveCoverageIndex.emplace(id, index);
    ID3D11RenderTargetView* target = _coverage[static_cast<std::size_t>(index)].rtv.get();
    _context->ClearRenderTargetView(target, kClearZero.data());
    if (layer == nullptr || !layer->asset.has_value())
    {
        // A blank source covers nothing: the clipped layer is hidden entirely (legacy draws nothing).
        return S_OK;
    }
    int upstream = kNoCoverage;
    if (layer->maskSourceID.has_value())
    {
        result = LayerCoverage(layer->maskSourceID.value(), depth + 1, upstream);
        if (FAILED(result))
        {
            return result;
        }
    }
    LayerConstants constants{};
    PassInputs inputs{};
    bool ok = false;
    FillLayerConstants(*layer, constants, inputs, ok);
    if (!ok)
    {
        return S_OK;
    }
    constants.passMode = static_cast<std::uint32_t>(Pass::LayerCoverage);
    if (upstream != kNoCoverage)
    {
        inputs.live = _coverage[static_cast<std::size_t>(upstream)].texture.srv.get();
        constants.flags |= kFlagHasLive;
    }
    return RunPass(constants, inputs, target);
}

HRESULT CanvasRenderer::GroupCoverage(const Guid& groupId, int depth, int& index) noexcept
{
    index = kNoCoverage;
    const auto cached = _groupCoverageIndex.find(groupId);
    if (cached != _groupCoverageIndex.end())
    {
        index = cached->second;
        return S_OK;
    }
    if (depth > kMaxGroupDepth)
    {
        return S_OK;
    }
    const ImageLayer* group = Find(groupId);
    if (group == nullptr)
    {
        return S_OK;
    }
    int parent = kNoCoverage;
    if (group->parentID.has_value())
    {
        const HRESULT result = GroupCoverage(group->parentID.value(), depth + 1, parent);
        if (FAILED(result))
        {
            return result;
        }
    }
    if (!HasEnabledMask(*group))
    {
        index = parent;
        _groupCoverageIndex.emplace(groupId, index);
        return S_OK;
    }
    GpuTexture* maskTexture = TextureFor(group->mask.value().asset.image, true);
    if (maskTexture == nullptr || !maskTexture->srv)
    {
        index = parent;
        _groupCoverageIndex.emplace(groupId, index);
        return S_OK;
    }
    HRESULT result = AcquireCoverage(index);
    if (FAILED(result))
    {
        return result;
    }
    _groupCoverageIndex.emplace(groupId, index);
    LayerConstants constants{};
    constants.canvasSize[0] = static_cast<float>(_canvasWidth);
    constants.canvasSize[1] = static_cast<float>(_canvasHeight);
    constants.passMode = static_cast<std::uint32_t>(Pass::MaskCoverage);
    constants.filterMode = group->transform.sampling == LayerSampling::Nearest ? 0u : 1u;
    FillTransform(MaskTransform(*group), constants.maskCenter, constants.maskSize, constants.maskSinCos,
                  constants.maskFlip);
    PassInputs inputs{};
    inputs.mask = maskTexture->srv.get();
    if (parent != kNoCoverage)
    {
        inputs.folder = _coverage[static_cast<std::size_t>(parent)].texture.srv.get();
        constants.flags |= kFlagHasFolder;
    }
    return RunPass(constants, inputs, _coverage[static_cast<std::size_t>(index)].rtv.get());
}

HRESULT CanvasRenderer::DrawLayerPass(const ImageLayer& layer, Pass pass, ID3D11ShaderResourceView* destSrv,
                                        ID3D11RenderTargetView* destRtv, int liveCoverage, int folderCoverage) noexcept
{
    LayerConstants constants{};
    PassInputs inputs{};
    bool ok = false;
    FillLayerConstants(layer, constants, inputs, ok);
    if (!ok)
    {
        return E_FAIL;
    }
    constants.passMode = static_cast<std::uint32_t>(pass);
    inputs.dest = destSrv;
    if (liveCoverage != kNoCoverage)
    {
        inputs.live = _coverage[static_cast<std::size_t>(liveCoverage)].texture.srv.get();
        constants.flags |= kFlagHasLive;
    }
    if (folderCoverage != kNoCoverage)
    {
        inputs.folder = _coverage[static_cast<std::size_t>(folderCoverage)].texture.srv.get();
        constants.flags |= kFlagHasFolder;
    }
    return RunPass(constants, inputs, destRtv);
}

void CanvasRenderer::SwapCanvas() noexcept
{
    _front = 1 - _front;
}

HRESULT CanvasRenderer::DrawStack(const ImageLayer& base, std::size_t firstChild, std::size_t childCount,
                                    int folderCoverage) noexcept
{
    HRESULT result = EnsureStackSurfaces();
    if (FAILED(result))
    {
        return result;
    }
    _stackFront = 0;
    _context->ClearRenderTargetView(_stack[0].rtv.get(), kClearZero.data());
    result = DrawLayerPass(base, Pass::StackBegin, nullptr, _stack[0].rtv.get(), kNoCoverage, kNoCoverage);
    if (FAILED(result))
    {
        return result;
    }
    for (std::size_t offset = 0; offset < childCount; ++offset)
    {
        const ImageLayer* child = Find(_renderIds[firstChild + offset]);
        if (child == nullptr || child->adjustment.has_value() || !child->asset.has_value())
        {
            continue;
        }
        const int back = 1 - _stackFront;
        result = DrawLayerPass(*child, Pass::StackChild, _stack[_stackFront].texture.srv.get(), _stack[back].rtv.get(),
                               kNoCoverage, kNoCoverage);
        if (FAILED(result))
        {
            return result;
        }
        _stackFront = back;
    }
    LayerConstants constants{};
    constants.canvasSize[0] = static_cast<float>(_canvasWidth);
    constants.canvasSize[1] = static_cast<float>(_canvasHeight);
    constants.passMode = static_cast<std::uint32_t>(Pass::StackResolve);
    constants.blendMode = static_cast<std::uint32_t>(base.blendMode);
    PassInputs inputs{};
    inputs.source = _stack[_stackFront].texture.srv.get();
    inputs.dest = _canvas[_front].texture.srv.get();
    if (folderCoverage != kNoCoverage)
    {
        inputs.folder = _coverage[static_cast<std::size_t>(folderCoverage)].texture.srv.get();
        constants.flags |= kFlagHasFolder;
    }
    result = RunPass(constants, inputs, _canvas[1 - _front].rtv.get());
    if (FAILED(result))
    {
        return result;
    }
    SwapCanvas();
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
    if (!_documentDirty)
    {
        return S_OK;
    }

    _composeId = _compositeCount + 1;
    _coverageUsed = 0;
    _byId.clear();
    _liveCoverageIndex.clear();
    _groupCoverageIndex.clear();
    for (const ImageLayer& layer : document.layers)
    {
        _byId.emplace(layer.id, &layer);
    }
    _renderIds = RenderLayerIds(document);
    _consumed.assign(_renderIds.size(), 0);

    _context->ClearRenderTargetView(_canvas[0].rtv.get(), kClearZero.data());
    _context->ClearRenderTargetView(_canvas[1].rtv.get(), kClearZero.data());
    _front = 0;

    for (std::size_t index = 0; index < _renderIds.size(); ++index)
    {
        if (_consumed[index] != 0)
        {
            continue;
        }
        const ImageLayer* layer = Find(_renderIds[index]);
        if (layer == nullptr || layer->isGroup || layer->adjustment.has_value() || !layer->asset.has_value())
        {
            continue;
        }
        int folder = kNoCoverage;
        if (layer->parentID.has_value())
        {
            result = GroupCoverage(layer->parentID.value(), 0, folder);
            if (FAILED(result))
            {
                return result;
            }
        }
        if (!layer->maskSourceID.has_value())
        {
            // Clipping stack: the contiguous clipped siblings directly above this base share its alpha.
            std::size_t childCount = 0;
            while (index + 1 + childCount < _renderIds.size())
            {
                const ImageLayer* candidate = Find(_renderIds[index + 1 + childCount]);
                if (candidate == nullptr || !candidate->maskSourceID.has_value() ||
                    !(candidate->maskSourceID.value() == layer->id) || candidate->parentID != layer->parentID)
                {
                    break;
                }
                ++childCount;
            }
            if (childCount > 0)
            {
                for (std::size_t offset = 0; offset < childCount; ++offset)
                {
                    _consumed[index + 1 + offset] = 1;
                }
                result = DrawStack(*layer, index + 1, childCount, folder);
                if (FAILED(result))
                {
                    return result;
                }
                continue;
            }
        }
        int live = kNoCoverage;
        if (layer->maskSourceID.has_value())
        {
            result = LayerCoverage(layer->maskSourceID.value(), 0, live);
            if (FAILED(result))
            {
                return result;
            }
        }
        result = DrawLayerPass(*layer, Pass::Layer, _canvas[_front].texture.srv.get(), _canvas[1 - _front].rtv.get(),
                               live, folder);
        if (FAILED(result))
        {
            return result;
        }
        SwapCanvas();
    }
    // Rasters this composition did not touch belong to a previous document state: release their textures.
    for (auto entry = _rasters.begin(); entry != _rasters.end();)
    {
        if (entry->second.lastUse != _composeId)
        {
            entry = _rasters.erase(entry);
        }
        else
        {
            ++entry;
        }
    }
    _documentDirty = false;
    ++_compositeCount;
    return S_OK;
}

HRESULT CanvasRenderer::BlitToTarget(ID3D11RenderTargetView* target, UINT targetWidth, UINT targetHeight,
                                       const CanvasViewport& viewport, float holeLeft, float holeTop, float holeWidth,
                                       float holeHeight) noexcept
{
    if (!_context || !target || !_canvas[_front].texture.srv)
    {
        return S_OK;
    }
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
    ID3D11ShaderResourceView* srvs[] = {_canvas[_front].texture.srv.get()};
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
    if (!_context || !_canvasWidth || !_canvas[_front].texture.texture)
    {
        return E_UNEXPECTED;
    }
    ID3D11Texture2D* front = _canvas[_front].texture.texture.get();
    D3D11_TEXTURE2D_DESC description{};
    front->GetDesc(&description);
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
    _context->CopyResource(staging.get(), front);
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
    const std::uint64_t pixels = static_cast<std::uint64_t>(_canvasWidth) * static_cast<std::uint64_t>(_canvasHeight);
    std::uint64_t bytes = 2ull * pixels * 4ull;
    if (_stack[0].texture.texture)
    {
        bytes += 2ull * pixels * 4ull;
    }
    bytes += static_cast<std::uint64_t>(_coverage.size()) * pixels;
    return bytes;
}
