#include "CanvasRenderer.h"

#include "Adjust.h"
#include "Shaders/CompositionShared.h"

#include "CompositionBlitPixelShader.h"
#include "CompositionPixelShader.h"
#include "CompositionVertexShader.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <new>

namespace
{
constexpr std::array<float, 4> kClearZero{0, 0, 0, 0};
constexpr double kPi = 3.14159265358979323846;
// Stroke tiles are this many grid pixels on a side (BrushStroke's tile grid): a reduced stroke texture starts its part
// of the grid on one, so a tile never splits a reduced texel.
constexpr int kStrokeTileAlign = 256;
// A region upload reads and reduces this many bytes of rows at a time.
constexpr std::size_t kUploadBandBytes = std::size_t{8} << 20;

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

// The math an adjustment pass runs for each kind (CompositionShared.h); never the enum's order.
[[nodiscard]] std::uint32_t AdjustKernel(AdjustmentKind kind) noexcept
{
    switch (kind)
    {
    case AdjustmentKind::Hsv:
        return kCompositionAdjustCube;
    case AdjustmentKind::GradientMap:
        return kCompositionAdjustGradientMap;
    case AdjustmentKind::Grain:
        return kCompositionAdjustGrain;
    case AdjustmentKind::Levels:
    case AdjustmentKind::Curves:
    case AdjustmentKind::Exposure:
    default:
        return kCompositionAdjustTables;
    }
}

[[nodiscard]] int CeilDiv(int value, int divisor) noexcept
{
    return (value + divisor - 1) / divisor;
}

// Texels of a full mip chain for a width x height texture.
[[nodiscard]] std::uint64_t MipChainTexels(std::uint64_t width, std::uint64_t height) noexcept
{
    std::uint64_t texels = 0;
    while (true)
    {
        texels += width * height;
        if (width == 1 && height == 1)
        {
            break;
        }
        width = std::max<std::uint64_t>(1, width / 2);
        height = std::max<std::uint64_t>(1, height / 2);
    }
    return texels;
}

// Averages `scale` x `scale` blocks of `width` x `height` pixels (`channels` bytes each, rows `stride` bytes apart)
// into texels (rows `outStride` bytes apart). A block cut by the right or bottom edge averages the pixels it has.
// Premultiplied colors average correctly as they are.
void ReduceBlocks(const std::uint8_t* rows, std::size_t stride, int width, int height, int channels, int scale,
                  std::uint8_t* out, std::size_t outStride) noexcept
{
    const int texelsX = CeilDiv(width, scale);
    const int texelsY = CeilDiv(height, scale);
    for (int ty = 0; ty < texelsY; ++ty)
    {
        const int y0 = ty * scale;
        const int y1 = std::min(height, y0 + scale);
        for (int tx = 0; tx < texelsX; ++tx)
        {
            const int x0 = tx * scale;
            const int x1 = std::min(width, x0 + scale);
            std::array<std::uint32_t, 4> sums{};
            for (int y = y0; y < y1; ++y)
            {
                const std::uint8_t* pixel =
                    rows + static_cast<std::size_t>(y) * stride + static_cast<std::size_t>(x0) * channels;
                for (int x = x0; x < x1; ++x)
                {
                    for (int c = 0; c < channels; ++c)
                    {
                        sums[static_cast<std::size_t>(c)] += pixel[c];
                    }
                    pixel += channels;
                }
            }
            const std::uint32_t count = static_cast<std::uint32_t>((y1 - y0) * (x1 - x0));
            std::uint8_t* texel =
                out + static_cast<std::size_t>(ty) * outStride + static_cast<std::size_t>(tx) * channels;
            for (int c = 0; c < channels; ++c)
            {
                texel[c] = static_cast<std::uint8_t>((sums[static_cast<std::size_t>(c)] + count / 2) / count);
            }
        }
    }
}
} // namespace

CanvasRenderer::~CanvasRenderer()
{
    ReleaseDeviceResources();
}

void CanvasRenderer::ReleaseDocumentSurfaces() noexcept
{
    _rasters.clear();
    _regions.clear();
    _adjustments.clear();
    _strokeTexture = {};
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
    _scratch = {};
    _canvasWidth = 0;
    _canvasHeight = 0;
    _canvasHasMips = false;
    _canvasMipsStale = false;
    _viewOriginX = 0;
    _viewOriginY = 0;
    _viewScale = 1;
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

void CanvasRenderer::SetTextureLimitForTest(int limit) noexcept
{
    _textureLimit = std::clamp(limit, 1, static_cast<int>(D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION));
    // Every texture was sized for the old limit.
    ReleaseDocumentSurfaces();
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

HRESULT CanvasRenderer::CreateSurface(UINT width, UINT height, DXGI_FORMAT format, bool mips,
                                        Surface& surface) noexcept
{
    surface = {};
    D3D11_TEXTURE2D_DESC description{};
    description.Width = width;
    description.Height = height;
    description.MipLevels = mips ? 0u : 1u;
    description.ArraySize = 1;
    description.Format = format;
    description.SampleDesc.Count = 1;
    description.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    description.MiscFlags = mips ? D3D11_RESOURCE_MISC_GENERATE_MIPS : 0u;
    HRESULT result = _device->CreateTexture2D(&description, nullptr, surface.texture.texture.put());
    if (FAILED(result) && mips)
    {
        // Without generated mips the zoomed-out blit samples the top level only.
        description.MipLevels = 1;
        description.MiscFlags = 0;
        result = _device->CreateTexture2D(&description, nullptr, surface.texture.texture.put());
        mips = false;
    }
    if (FAILED(result))
    {
        return result;
    }
    result = _device->CreateShaderResourceView(surface.texture.texture.get(), nullptr, surface.texture.srv.put());
    if (FAILED(result))
    {
        return result;
    }
    // A null description targets mip 0; the shader resource view sees the whole chain.
    result = _device->CreateRenderTargetView(surface.texture.texture.get(), nullptr, surface.rtv.put());
    if (FAILED(result))
    {
        return result;
    }
    surface.texture.width = static_cast<int>(width);
    surface.texture.height = static_cast<int>(height);
    surface.texture.regionWidth = surface.texture.width;
    surface.texture.regionHeight = surface.texture.height;
    surface.texture.sourceWidth = surface.texture.width;
    surface.texture.sourceHeight = surface.texture.height;
    surface.texture.gray = format == DXGI_FORMAT_R8_UNORM;
    surface.texture.mipped = mips;
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
    _canvasHasMips = false;
    _canvasMipsStale = false;
    _documentDirty = true;
    if (width == 0 || height == 0)
    {
        return S_OK;
    }
    // The canvases carry mips so a zoomed-out blit reduces sharply instead of aliasing (RF-412).
    HRESULT result = CreateSurface(width, height, DXGI_FORMAT_B8G8R8A8_UNORM, true, _canvas[0]);
    if (FAILED(result))
    {
        return result;
    }
    result = CreateSurface(width, height, DXGI_FORMAT_B8G8R8A8_UNORM, true, _canvas[1]);
    if (FAILED(result))
    {
        _canvas[0] = {};
        return result;
    }
    _canvasWidth = width;
    _canvasHeight = height;
    _canvasHasMips = _canvas[0].texture.mipped && _canvas[1].texture.mipped;
    _front = 0;
    return S_OK;
}

HRESULT CanvasRenderer::EnsureStackSurfaces() noexcept
{
    if (_stack[0].texture.texture && _stack[1].texture.texture)
    {
        return S_OK;
    }
    HRESULT result = CreateSurface(_canvasWidth, _canvasHeight, DXGI_FORMAT_B8G8R8A8_UNORM, false, _stack[0]);
    if (FAILED(result))
    {
        return result;
    }
    result = CreateSurface(_canvasWidth, _canvasHeight, DXGI_FORMAT_B8G8R8A8_UNORM, false, _stack[1]);
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
        const HRESULT result = CreateSurface(_canvasWidth, _canvasHeight, DXGI_FORMAT_R8_UNORM, false, surface);
        if (FAILED(result))
        {
            return result;
        }
        try
        {
            _coverage.push_back(std::move(surface));
        }
        catch (const std::bad_alloc&)
        {
            return E_OUTOFMEMORY;
        }
    }
    index = static_cast<int>(_coverageUsed);
    ++_coverageUsed;
    return S_OK;
}

HRESULT CanvasRenderer::CreateMippedTexture(int width, int height, bool gray, GpuTexture& gpu) noexcept
{
    gpu = {};
    if (width < 1 || height < 1 || width > _textureLimit || height > _textureLimit)
    {
        return E_INVALIDARG;
    }
    D3D11_TEXTURE2D_DESC description{};
    description.Width = static_cast<UINT>(width);
    description.Height = static_cast<UINT>(height);
    description.MipLevels = 0;
    description.ArraySize = 1;
    description.Format = gray ? DXGI_FORMAT_R8_UNORM : DXGI_FORMAT_B8G8R8A8_UNORM;
    description.SampleDesc.Count = 1;
    description.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    description.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;
    HRESULT result = _device->CreateTexture2D(&description, nullptr, gpu.texture.put());
    bool mipped = true;
    if (FAILED(result))
    {
        description.MipLevels = 1;
        description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        description.MiscFlags = 0;
        result = _device->CreateTexture2D(&description, nullptr, gpu.texture.put());
        mipped = false;
        if (FAILED(result))
        {
            return result;
        }
    }
    gpu.width = width;
    gpu.height = height;
    gpu.regionWidth = width;
    gpu.regionHeight = height;
    gpu.sourceWidth = width;
    gpu.sourceHeight = height;
    gpu.gray = gray;
    gpu.mipped = mipped;
    return S_OK;
}

void CanvasRenderer::GenerateMipsFor(const GpuTexture& gpu) noexcept
{
    if (gpu.mipped && gpu.srv)
    {
        _context->GenerateMips(gpu.srv.get());
    }
}

std::array<float, 4> CanvasRenderer::RectFor(const GpuTexture& texture) noexcept
{
    const double sourceWidth = std::max(1, texture.sourceWidth);
    const double sourceHeight = std::max(1, texture.sourceHeight);
    const double coveredWidth = static_cast<double>(std::max(1, texture.width)) * std::max(1, texture.scale);
    const double coveredHeight = static_cast<double>(std::max(1, texture.height)) * std::max(1, texture.scale);
    return {static_cast<float>(texture.originX / sourceWidth), static_cast<float>(texture.originY / sourceHeight),
            static_cast<float>(sourceWidth / coveredWidth), static_cast<float>(sourceHeight / coveredHeight)};
}

template <typename Reader> HRESULT CanvasRenderer::UploadRows(GpuTexture& gpu, Reader&& read) noexcept
{
    const int channels = gpu.gray ? 1 : 4;
    const int scale = std::max(1, gpu.scale);
    const std::size_t rowBytes = static_cast<std::size_t>(gpu.regionWidth) * static_cast<std::size_t>(channels);
    if (rowBytes == 0 || gpu.width < 1 || gpu.height < 1 || gpu.regionHeight < 1)
    {
        return E_INVALIDARG;
    }
    const std::size_t band = std::max<std::size_t>(1, kUploadBandBytes / (rowBytes * static_cast<std::size_t>(scale)));
    const int texelRowsPerBand = static_cast<int>(std::min<std::size_t>(band, static_cast<std::size_t>(gpu.height)));
    const std::size_t texelRowBytes = static_cast<std::size_t>(gpu.width) * static_cast<std::size_t>(channels);
    std::vector<std::uint8_t> rows;
    std::vector<std::uint8_t> texels;
    try
    {
        rows.resize(rowBytes * static_cast<std::size_t>(scale) * static_cast<std::size_t>(texelRowsPerBand));
        if (scale > 1)
        {
            texels.resize(texelRowBytes * static_cast<std::size_t>(texelRowsPerBand));
        }
    }
    catch (const std::bad_alloc&)
    {
        return E_OUTOFMEMORY;
    }
    for (int texelRow = 0; texelRow < gpu.height; texelRow += texelRowsPerBand)
    {
        const int bandTexels = std::min(texelRowsPerBand, gpu.height - texelRow);
        const int firstRow = texelRow * scale;
        const int rowCount = std::min(bandTexels * scale, gpu.regionHeight - firstRow);
        read(gpu.originX, gpu.originY + firstRow, gpu.regionWidth, rowCount, rows.data(), rowBytes);
        const std::uint8_t* data = rows.data();
        std::size_t pitch = rowBytes;
        if (scale > 1)
        {
            ReduceBlocks(rows.data(), rowBytes, gpu.regionWidth, rowCount, channels, scale, texels.data(),
                         texelRowBytes);
            data = texels.data();
            pitch = texelRowBytes;
        }
        const D3D11_BOX box{
            0, static_cast<UINT>(texelRow), 0, static_cast<UINT>(gpu.width), static_cast<UINT>(texelRow + bandTexels),
            1};
        _context->UpdateSubresource(gpu.texture.get(), 0, &box, data, static_cast<UINT>(pitch), 0);
    }
    return S_OK;
}

HRESULT CanvasRenderer::WriteTiles(GpuTexture& gpu, std::span<const RasterTile> tiles, bool gray) noexcept
{
    const int channels = gray ? 1 : 4;
    const int scale = std::max(1, gpu.scale);
    const int regionRight = gpu.originX + gpu.regionWidth;
    const int regionBottom = gpu.originY + gpu.regionHeight;
    for (const RasterTile& tile : tiles)
    {
        const std::uint8_t* pixels = tile.pixels.Data();
        if (pixels == nullptr || (tile.pixels.Format() == RasterFormat::Gray8) != gray)
        {
            return E_INVALIDARG;
        }
        // Only the part inside the region the texture holds.
        const int left = std::max(gpu.originX, tile.x);
        const int top = std::max(gpu.originY, tile.y);
        const int right = std::min(regionRight, tile.x + tile.Width());
        const int bottom = std::min(regionBottom, tile.y + tile.Height());
        if (right <= left || bottom <= top)
        {
            continue;
        }
        const std::size_t stride = static_cast<std::size_t>(tile.pixels.Stride());
        const auto at = [&](int x, int y) noexcept
        {
            return pixels + static_cast<std::size_t>(tile.sourceY + (y - tile.y)) * stride +
                   static_cast<std::size_t>(tile.sourceX + (x - tile.x)) * static_cast<std::size_t>(channels);
        };
        if (scale == 1)
        {
            const D3D11_BOX box{static_cast<UINT>(left - gpu.originX),  static_cast<UINT>(top - gpu.originY),    0,
                                static_cast<UINT>(right - gpu.originX), static_cast<UINT>(bottom - gpu.originY), 1};
            _context->UpdateSubresource(gpu.texture.get(), 0, &box, at(left, top), static_cast<UINT>(stride), 0);
            continue;
        }
        // Reduced: the texels whose whole block of pixels lies in the tile (a block the region's own edge cuts
        // short counts as whole). Stroke tiles start on the region's tile grid, so they cover their texels exactly.
        const int texelLeft = CeilDiv(left - gpu.originX, scale);
        const int texelTop = CeilDiv(top - gpu.originY, scale);
        const int texelRight = right == regionRight ? gpu.width : (right - gpu.originX) / scale;
        const int texelBottom = bottom == regionBottom ? gpu.height : (bottom - gpu.originY) / scale;
        if (texelRight <= texelLeft || texelBottom <= texelTop)
        {
            continue;
        }
        const int pixelLeft = gpu.originX + texelLeft * scale;
        const int pixelTop = gpu.originY + texelTop * scale;
        const int pixelRight = std::min(regionRight, gpu.originX + texelRight * scale);
        const int pixelBottom = std::min(regionBottom, gpu.originY + texelBottom * scale);
        const std::size_t texelRowBytes =
            static_cast<std::size_t>(texelRight - texelLeft) * static_cast<std::size_t>(channels);
        const std::size_t bytes = texelRowBytes * static_cast<std::size_t>(texelBottom - texelTop);
        if (_scratch.size() < bytes)
        {
            try
            {
                _scratch.resize(bytes);
            }
            catch (const std::bad_alloc&)
            {
                return E_OUTOFMEMORY;
            }
        }
        ReduceBlocks(at(pixelLeft, pixelTop), stride, pixelRight - pixelLeft, pixelBottom - pixelTop, channels, scale,
                     _scratch.data(), texelRowBytes);
        const D3D11_BOX box{static_cast<UINT>(texelLeft),  static_cast<UINT>(texelTop),    0,
                            static_cast<UINT>(texelRight), static_cast<UINT>(texelBottom), 1};
        _context->UpdateSubresource(gpu.texture.get(), 0, &box, _scratch.data(), static_cast<UINT>(texelRowBytes), 0);
    }
    return S_OK;
}

HRESULT CanvasRenderer::CreateSparseTexture(int width, int height, bool gray, const Raster& base, int baseX,
                                              int baseY, std::uint8_t fill, std::span<const RasterTile> tiles,
                                              GpuTexture& gpu) noexcept
{
    HRESULT result = CreateMippedTexture(width, height, gray, gpu);
    if (FAILED(result))
    {
        return result;
    }
    if (!gpu.mipped)
    {
        // Without a render target there is nothing to clear into; the caller assembles the pixels instead.
        gpu = {};
        return E_NOTIMPL;
    }
    GpuTexture* baseTexture = nullptr;
    if (base.HasStorage())
    {
        if (base.Width() > _textureLimit || base.Height() > _textureLimit)
        {
            // A base larger than the device has no whole texture to copy from.
            gpu = {};
            return E_NOTIMPL;
        }
        // The base keeps its own texture (shared with the snapshots it came from); its visible part is copied.
        result = TextureFor(base, gray, baseTexture);
        if (FAILED(result))
        {
            gpu = {};
            return result;
        }
    }
    wil::com_ptr_nothrow<ID3D11RenderTargetView> target;
    result = _device->CreateRenderTargetView(gpu.texture.get(), nullptr, target.put());
    if (FAILED(result))
    {
        gpu = {};
        return result;
    }
    const float level = fill / 255.0f;
    const std::array<float, 4> clear{level, level, level, level};
    _context->ClearRenderTargetView(target.get(), clear.data());
    if (baseTexture != nullptr)
    {
        const int left = std::max(0, baseX);
        const int top = std::max(0, baseY);
        const int right = std::min(width, baseX + base.Width());
        const int bottom = std::min(height, baseY + base.Height());
        if (right > left && bottom > top)
        {
            const D3D11_BOX box{static_cast<UINT>(left - baseX),  static_cast<UINT>(top - baseY),    0,
                                static_cast<UINT>(right - baseX), static_cast<UINT>(bottom - baseY), 1};
            _context->CopySubresourceRegion(gpu.texture.get(), 0, static_cast<UINT>(left), static_cast<UINT>(top), 0,
                                            baseTexture->texture.get(), 0, &box);
        }
    }
    result = WriteTiles(gpu, tiles, gray);
    if (FAILED(result))
    {
        gpu = {};
        return result;
    }
    result = _device->CreateShaderResourceView(gpu.texture.get(), nullptr, gpu.srv.put());
    if (FAILED(result))
    {
        gpu = {};
        return result;
    }
    GenerateMipsFor(gpu);
    return S_OK;
}

HRESULT CanvasRenderer::UploadRaster(const Raster& raster, GpuTexture& gpu, bool gray) noexcept
{
    gpu = {};
    if (!raster.HasStorage())
    {
        return E_INVALIDARG;
    }
    if (raster.IsSparse() && !raster.IsMaterialized())
    {
        // A painted layer: assembled on the GPU from its base texture and tiles, never as contiguous CPU bytes.
        const HRESULT sparse = CreateSparseTexture(raster.Width(), raster.Height(), gray, raster.Base(), raster.BaseX(),
                                                   raster.BaseY(), raster.Fill(), raster.Tiles(), gpu);
        if (SUCCEEDED(sparse))
        {
            return S_OK;
        }
        if (sparse != E_NOTIMPL)
        {
            return sparse;
        }
    }
    if (raster.Data() == nullptr)
    {
        return E_OUTOFMEMORY;
    }
    HRESULT result = CreateMippedTexture(raster.Width(), raster.Height(), gray, gpu);
    if (FAILED(result))
    {
        return result;
    }
    const D3D11_BOX box{0, 0, 0, static_cast<UINT>(gpu.width), static_cast<UINT>(gpu.height), 1};
    _context->UpdateSubresource(gpu.texture.get(), 0, &box, raster.Data(), static_cast<UINT>(raster.Stride()), 0);
    result = _device->CreateShaderResourceView(gpu.texture.get(), nullptr, gpu.srv.put());
    if (FAILED(result))
    {
        return result;
    }
    GenerateMipsFor(gpu);
    return S_OK;
}

HRESULT CanvasRenderer::TextureFor(const Raster& raster, bool gray, GpuTexture*& texture) noexcept
{
    texture = nullptr;
    const void* identity = raster.Identity();
    if (identity == nullptr)
    {
        return E_INVALIDARG;
    }
    auto found = _rasters.find(identity);
    if (found == _rasters.end())
    {
        GpuTexture gpu;
        const HRESULT result = UploadRaster(raster, gpu, gray);
        if (FAILED(result))
        {
            return result;
        }
        gpu.keepAlive = raster;
        try
        {
            found = _rasters.emplace(identity, std::move(gpu)).first;
        }
        catch (const std::bad_alloc&)
        {
            return E_OUTOFMEMORY;
        }
    }
    found->second.lastUse = _composeId;
    texture = &found->second;
    return S_OK;
}

CanvasRenderer::DocumentWindow CanvasRenderer::CanvasWindow() const noexcept
{
    DocumentWindow window;
    window.x = _viewOriginX;
    window.y = _viewOriginY;
    window.width = static_cast<double>(_canvasWidth) * _viewScale;
    window.height = static_cast<double>(_canvasHeight) * _viewScale;
    window.scale = _viewScale;
    return window;
}

bool CanvasRenderer::RegionFor(const LayerTransform& mapping, int width, int height, int align,
                                 const DocumentWindow& window, PixelRect& region, int& scale) const noexcept
{
    region = {};
    scale = 1;
    if (width < 1 || height < 1 || window.width <= 0 || window.height <= 0)
    {
        return false;
    }
    // The window's corners in raster pixels through the inverse of the placement (the shader's DocumentToUnit).
    const Vec2 center = mapping.Center();
    const double radians = std::fmod(mapping.rotation, 360.0) * kPi / 180.0;
    const double sine = std::sin(radians);
    const double cosine = std::cos(radians);
    const double flipX = mapping.flipX ? -1.0 : 1.0;
    const double flipY = mapping.flipY ? -1.0 : 1.0;
    const double sizeX = std::max(std::fabs(mapping.size.x), 1e-5);
    const double sizeY = std::max(std::fabs(mapping.size.y), 1e-5);
    double minX = std::numeric_limits<double>::max();
    double minY = std::numeric_limits<double>::max();
    double maxX = std::numeric_limits<double>::lowest();
    double maxY = std::numeric_limits<double>::lowest();
    const std::array<Vec2, 4> corners{Vec2{window.x, window.y}, Vec2{window.x + window.width, window.y},
                                      Vec2{window.x, window.y + window.height},
                                      Vec2{window.x + window.width, window.y + window.height}};
    for (const Vec2& corner : corners)
    {
        const double dx = corner.x - center.x;
        const double dy = corner.y - center.y;
        const double localX = (dx * cosine + dy * sine) * flipX;
        const double localY = (-dx * sine + dy * cosine) * flipY;
        const double x = (localX / sizeX + 0.5) * width;
        const double y = (localY / sizeY + 0.5) * height;
        minX = std::min(minX, x);
        minY = std::min(minY, y);
        maxX = std::max(maxX, x);
        maxY = std::max(maxY, y);
    }
    // Filtering reaches a little past a pixel's footprint: twice the raster pixels one canvas pixel spans, plus two.
    const double footprint = window.scale * std::max(width / sizeX, height / sizeY);
    const double margin = std::ceil(2.0 * std::min(footprint, 1.0e6)) + 2.0;
    const double left = std::clamp(std::floor(minX - margin), 0.0, static_cast<double>(width));
    const double top = std::clamp(std::floor(minY - margin), 0.0, static_cast<double>(height));
    const double right = std::clamp(std::ceil(maxX + margin), 0.0, static_cast<double>(width));
    const double bottom = std::clamp(std::ceil(maxY + margin), 0.0, static_cast<double>(height));
    if (!(right > left) || !(bottom > top))
    {
        return false;
    }
    const int step = std::max(1, align);
    const int x0 = static_cast<int>(left) / step * step;
    const int y0 = static_cast<int>(top) / step * step;
    const int x1 = std::min(width, CeilDiv(static_cast<int>(right), step) * step);
    const int y1 = std::min(height, CeilDiv(static_cast<int>(bottom), step) * step);
    region = PixelRect{x0, y0, x1 - x0, y1 - y0};
    while (CeilDiv(region.width, scale) > _textureLimit || CeilDiv(region.height, scale) > _textureLimit)
    {
        scale *= 2;
    }
    return true;
}

HRESULT CanvasRenderer::RegionTextureFor(const Raster& raster, bool gray, const PixelRect& region, int scale,
                                           GpuTexture*& texture) noexcept
{
    texture = nullptr;
    const void* identity = raster.Identity();
    for (GpuTexture& entry : _regions)
    {
        if (entry.keepAlive.Identity() == identity && entry.gray == gray && entry.originX == region.x &&
            entry.originY == region.y && entry.regionWidth == region.width && entry.regionHeight == region.height &&
            entry.scale == scale)
        {
            entry.lastUse = _composeId;
            texture = &entry;
            return S_OK;
        }
    }
    GpuTexture gpu;
    HRESULT result = CreateMippedTexture(CeilDiv(region.width, scale), CeilDiv(region.height, scale), gray, gpu);
    if (FAILED(result))
    {
        return result;
    }
    gpu.originX = region.x;
    gpu.originY = region.y;
    gpu.regionWidth = region.width;
    gpu.regionHeight = region.height;
    gpu.sourceWidth = raster.Width();
    gpu.sourceHeight = raster.Height();
    gpu.scale = scale;
    // Read through the raster (never Data(): a sparse raster stays unassembled), band by band.
    result = UploadRows(gpu, [&](int x, int y, int width, int height, std::uint8_t* out, std::size_t stride) noexcept
                        { raster.ReadRegion(x, y, width, height, out, static_cast<int>(stride), 0); });
    if (FAILED(result))
    {
        return result;
    }
    result = _device->CreateShaderResourceView(gpu.texture.get(), nullptr, gpu.srv.put());
    if (FAILED(result))
    {
        return result;
    }
    GenerateMipsFor(gpu);
    gpu.keepAlive = raster;
    gpu.lastUse = _composeId;
    try
    {
        _regions.push_back(std::move(gpu));
    }
    catch (const std::bad_alloc&)
    {
        return E_OUTOFMEMORY;
    }
    texture = &_regions.back();
    return S_OK;
}

HRESULT CanvasRenderer::RasterView(const Raster& raster, bool gray, const LayerTransform& mapping,
                                     TextureView& view) noexcept
{
    view = {};
    if (!raster.HasStorage())
    {
        return E_INVALIDARG;
    }
    GpuTexture* texture = nullptr;
    if (raster.Width() <= _textureLimit && raster.Height() <= _textureLimit)
    {
        const HRESULT result = TextureFor(raster, gray, texture);
        if (FAILED(result))
        {
            return result;
        }
        view.srv = texture->srv.get();
        return S_OK;
    }
    // Larger than the device: only the part the canvas window shows, reduced when that part is still too large.
    PixelRect region;
    int scale = 1;
    if (!RegionFor(mapping, raster.Width(), raster.Height(), 1, CanvasWindow(), region, scale))
    {
        return S_FALSE;
    }
    const HRESULT result = RegionTextureFor(raster, gray, region, scale, texture);
    if (FAILED(result))
    {
        return result;
    }
    view.srv = texture->srv.get();
    view.rect = RectFor(*texture);
    return S_OK;
}

bool CanvasRenderer::DrawsStrokeImage(const ImageLayer& layer) const noexcept
{
    return !_ignoreStroke && _stroke.has_value() && !_stroke->mask && _stroke->layerID == layer.id &&
           _strokeTexture.srv;
}

bool CanvasRenderer::DrawsStrokeMask(const ImageLayer& layer) const noexcept
{
    return !_ignoreStroke && _stroke.has_value() && _stroke->mask && _stroke->layerID == layer.id && _strokeTexture.srv;
}

HRESULT CanvasRenderer::MaskView(const ImageLayer& layer, TextureView& view) noexcept
{
    view = {};
    if (DrawsStrokeMask(layer))
    {
        view.srv = _strokeTexture.srv.get();
        view.rect = RectFor(_strokeTexture);
        return S_OK;
    }
    if (!layer.mask.has_value())
    {
        return S_FALSE;
    }
    return RasterView(layer.mask.value().asset.image, true, MaskTransform(layer), view);
}

HRESULT CanvasRenderer::BeginStroke(const StrokePreview& preview) noexcept
{
    if (preview.width < 1 || preview.height < 1)
    {
        return E_INVALIDARG;
    }
    _stroke = preview;
    _strokeTexture = {};
    _documentDirty = true;
    return S_OK;
}

HRESULT CanvasRenderer::CreateStrokeTexture(std::span<const RasterTile> tiles) noexcept
{
    const StrokePreview& stroke = _stroke.value();
    const bool gray = stroke.mask;
    PixelRect region{0, 0, stroke.width, stroke.height};
    int scale = 1;
    if (stroke.width <= _textureLimit && stroke.height <= _textureLimit)
    {
        // The whole grid, assembled on the GPU from the source's own texture and the tiles.
        const HRESULT result = CreateSparseTexture(stroke.width, stroke.height, gray, stroke.source, stroke.sourceX,
                                                   stroke.sourceY, stroke.fill, tiles, _strokeTexture);
        if (result != E_NOTIMPL)
        {
            return result;
        }
    }
    else
    {
        // A grid larger than the device: the part the display shows (the whole document at its proxy scale), on the
        // tile grid, reduced to fit.
        DocumentWindow window;
        window.width = _documentWidth > 0 ? _documentWidth : stroke.width;
        window.height = _documentHeight > 0 ? _documentHeight : stroke.height;
        window.scale = _displayScale;
        const LayerTransform mapping = _documentWidth > 0
                                           ? stroke.transform
                                           : LayerTransform{Vec2{0, 0}, Vec2{static_cast<double>(stroke.width),
                                                                             static_cast<double>(stroke.height)}};
        if (!RegionFor(mapping, stroke.width, stroke.height, kStrokeTileAlign, window, region, scale))
        {
            // Nothing of the grid is on the canvas; a small part keeps the preview settled.
            region =
                PixelRect{0, 0, std::min(stroke.width, kStrokeTileAlign), std::min(stroke.height, kStrokeTileAlign)};
            scale = 1;
        }
    }
    // On the CPU: the fill and the source row by row, reduced to the texture, then the tiles.
    GpuTexture gpu;
    HRESULT result = CreateMippedTexture(CeilDiv(region.width, scale), CeilDiv(region.height, scale), gray, gpu);
    if (FAILED(result))
    {
        return result;
    }
    gpu.originX = region.x;
    gpu.originY = region.y;
    gpu.regionWidth = region.width;
    gpu.regionHeight = region.height;
    gpu.sourceWidth = stroke.width;
    gpu.sourceHeight = stroke.height;
    gpu.scale = scale;
    const int channels = gray ? 1 : 4;
    result = UploadRows(gpu,
                        [&](int x, int y, int width, int height, std::uint8_t* out, std::size_t stride) noexcept
                        {
                            for (int row = 0; row < height; ++row)
                            {
                                std::memset(out + static_cast<std::size_t>(row) * stride, stroke.fill,
                                            static_cast<std::size_t>(width) * static_cast<std::size_t>(channels));
                            }
                            if (!stroke.source.HasStorage())
                            {
                                return;
                            }
                            const int left = std::max(x, stroke.sourceX);
                            const int top = std::max(y, stroke.sourceY);
                            const int right = std::min(x + width, stroke.sourceX + stroke.source.Width());
                            const int bottom = std::min(y + height, stroke.sourceY + stroke.source.Height());
                            if (right > left && bottom > top)
                            {
                                stroke.source.ReadRegion(left - stroke.sourceX, top - stroke.sourceY, right - left,
                                                         bottom - top,
                                                         out + static_cast<std::size_t>(top - y) * stride +
                                                             static_cast<std::size_t>(left - x) * channels,
                                                         static_cast<int>(stride), stroke.fill);
                            }
                        });
    if (FAILED(result))
    {
        return result;
    }
    result = WriteTiles(gpu, tiles, gray);
    if (FAILED(result))
    {
        return result;
    }
    result = _device->CreateShaderResourceView(gpu.texture.get(), nullptr, gpu.srv.put());
    if (FAILED(result))
    {
        return result;
    }
    GenerateMipsFor(gpu);
    _strokeTexture = std::move(gpu);
    return S_OK;
}

HRESULT CanvasRenderer::UpdateStrokeTiles(std::span<const RasterTile> tiles) noexcept
{
    if (!_stroke.has_value() || !_device || !_context)
    {
        return E_UNEXPECTED;
    }
    HRESULT result = S_OK;
    if (!_strokeTexture.srv)
    {
        result = CreateStrokeTexture(tiles);
    }
    else
    {
        result = WriteTiles(_strokeTexture, tiles, _stroke->mask);
        if (SUCCEEDED(result))
        {
            GenerateMipsFor(_strokeTexture);
        }
    }
    _documentDirty = true;
    return result;
}

void CanvasRenderer::EndStroke(const Raster* committed, int cropX, int cropY, const Guid* committedLayerID) noexcept
{
    // The working texture holds the committed pixels only when the commit is this preview's own: the same layer
    // and target, a crop inside the part the texture holds, nothing reduced.
    const bool adopt = committed != nullptr && committed->HasStorage() && committedLayerID != nullptr &&
                       _stroke.has_value() && _strokeTexture.srv && _device && *committedLayerID == _stroke->layerID &&
                       (committed->Format() == RasterFormat::Gray8) == _stroke->mask && _strokeTexture.scale == 1 &&
                       committed->Width() <= _textureLimit && committed->Height() <= _textureLimit;
    if (adopt)
    {
        const int left = cropX - _strokeTexture.originX;
        const int top = cropY - _strokeTexture.originY;
        GpuTexture adopted;
        if (left >= 0 && top >= 0 && left + committed->Width() <= _strokeTexture.width &&
            top + committed->Height() <= _strokeTexture.height &&
            SUCCEEDED(CreateMippedTexture(committed->Width(), committed->Height(), _stroke->mask, adopted)))
        {
            // Crop the working texture into the cache: no upload.
            const D3D11_BOX box{static_cast<UINT>(left),
                                static_cast<UINT>(top),
                                0,
                                static_cast<UINT>(left + committed->Width()),
                                static_cast<UINT>(top + committed->Height()),
                                1};
            _context->CopySubresourceRegion(adopted.texture.get(), 0, 0, 0, 0, _strokeTexture.texture.get(), 0, &box);
            if (SUCCEEDED(_device->CreateShaderResourceView(adopted.texture.get(), nullptr, adopted.srv.put())))
            {
                GenerateMipsFor(adopted);
                adopted.keepAlive = *committed;
                adopted.lastUse = _composeId;
                try
                {
                    _rasters[committed->Identity()] = std::move(adopted);
                    ++_adoptionCount;
                }
                catch (const std::bad_alloc&)
                {
                    // The committed raster uploads on its own at the next composition.
                }
            }
        }
    }
    _stroke.reset();
    _strokeTexture = {};
    _documentDirty = true;
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
    ID3D11ShaderResourceView* srvs[] = {inputs.source, inputs.dest, inputs.mask, inputs.live,
                                        inputs.folder, inputs.lut,  inputs.cube};
    _context->PSSetShaderResources(0, 7, srvs);
    ID3D11SamplerState* samplers[] = {_linearSampler.get(), _pointSampler.get()};
    _context->PSSetSamplers(0, 2, samplers);
    _context->Draw(3, 0);
    ID3D11ShaderResourceView* none[] = {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};
    _context->PSSetShaderResources(0, 7, none);
    _context->OMSetRenderTargets(0, nullptr, nullptr);
    return S_OK;
}

void CanvasRenderer::FillViewConstants(LayerConstants& constants) const noexcept
{
    constants.canvasSize[0] = static_cast<float>(_canvasWidth);
    constants.canvasSize[1] = static_cast<float>(_canvasHeight);
    constants.viewOrigin[0] = static_cast<float>(_viewOriginX);
    constants.viewOrigin[1] = static_cast<float>(_viewOriginY);
    constants.viewScale = static_cast<float>(_viewScale);
}

HRESULT CanvasRenderer::FillLayerConstants(const ImageLayer& layer, LayerConstants& constants,
                                             PassInputs& inputs) noexcept
{
    constants = {};
    FillViewConstants(constants);
    const bool strokeImage = DrawsStrokeImage(layer);
    TextureView source;
    if (strokeImage)
    {
        // The stroke's grid, drawn where the stroke places it, stands in for the layer's pixels.
        source.srv = _strokeTexture.srv.get();
        source.rect = RectFor(_strokeTexture);
        FillTransform(_stroke->transform, constants.layerCenter, constants.layerSize, constants.layerSinCos,
                      constants.layerFlip);
    }
    else
    {
        if (!layer.asset.has_value())
        {
            return S_FALSE;
        }
        const HRESULT result = RasterView(layer.asset.value().image, false, layer.transform, source);
        if (result != S_OK)
        {
            return result;
        }
        FillTransform(layer.transform, constants.layerCenter, constants.layerSize, constants.layerSinCos,
                      constants.layerFlip);
    }
    inputs.source = source.srv;
    std::copy(source.rect.begin(), source.rect.end(), constants.sourceRect);
    constants.blendMode = static_cast<std::uint32_t>(layer.blendMode);
    constants.filterMode = layer.transform.sampling == LayerSampling::Nearest ? 0u : 1u;
    constants.opacity = static_cast<float>(layer.opacity);
    if (HasEnabledMask(layer))
    {
        const LayerMask& mask = layer.mask.value();
        TextureView maskView;
        const HRESULT result = MaskView(layer, maskView);
        if (FAILED(result))
        {
            return result;
        }
        inputs.mask = maskView.srv;
        std::copy(maskView.rect.begin(), maskView.rect.end(), constants.maskRect);
        constants.flags |= kCompositionFlagHasMask;
        if (mask.placement.has_value())
        {
            constants.flags |= kCompositionFlagMaskPlaced;
            FillTransform(mask.placement.value(), constants.maskCenter, constants.maskSize, constants.maskSinCos,
                          constants.maskFlip);
            constants.maskBackground = static_cast<float>(MaskEdgeBackground(mask.asset.thumbnail));
        }
        else if (strokeImage && mask.asset.image.PixelCount() > 1 && !(_stroke->transform == layer.transform))
        {
            // The preview's grid grew past the layer: the mask stays on the layer's own pixels, and paint beyond
            // them is revealed, as the commit grows the mask with 255 (a uniform 1 x 1 mask stays uniform).
            constants.flags |= kCompositionFlagMaskPlaced;
            FillTransform(layer.transform, constants.maskCenter, constants.maskSize, constants.maskSinCos,
                          constants.maskFlip);
            constants.maskBackground = 1.0f;
        }
        else if (result == S_FALSE)
        {
            // An unplaced mask whose pixels the window does not reach reads as its edge tone there.
            constants.flags |= kCompositionFlagMaskPlaced;
            FillTransform(MaskTransform(layer), constants.maskCenter, constants.maskSize, constants.maskSinCos,
                          constants.maskFlip);
            constants.maskBackground = static_cast<float>(MaskEdgeBackground(mask.asset.thumbnail));
        }
    }
    return S_OK;
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
    try
    {
        _liveCoverageIndex.emplace(id, index);
    }
    catch (const std::bad_alloc&)
    {
        return E_OUTOFMEMORY;
    }
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
    result = FillLayerConstants(*layer, constants, inputs);
    if (result != S_OK)
    {
        // S_FALSE: the source shows nothing in this window, so it covers nothing here.
        return SUCCEEDED(result) ? S_OK : result;
    }
    constants.passMode = kCompositionPassLayerCoverage;
    if (upstream != kNoCoverage)
    {
        inputs.live = _coverage[static_cast<std::size_t>(upstream)].texture.srv.get();
        constants.flags |= kCompositionFlagHasLive;
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
    const auto remember = [&](int value) noexcept -> HRESULT
    {
        index = value;
        try
        {
            _groupCoverageIndex.emplace(groupId, index);
        }
        catch (const std::bad_alloc&)
        {
            return E_OUTOFMEMORY;
        }
        return S_OK;
    };
    if (!HasEnabledMask(*group))
    {
        return remember(parent);
    }
    TextureView maskView;
    HRESULT result = MaskView(*group, maskView);
    if (FAILED(result))
    {
        return result;
    }
    int coverage = kNoCoverage;
    const HRESULT acquired = AcquireCoverage(coverage);
    if (FAILED(acquired))
    {
        return acquired;
    }
    result = remember(coverage);
    if (FAILED(result))
    {
        return result;
    }
    ID3D11RenderTargetView* target = _coverage[static_cast<std::size_t>(index)].rtv.get();
    if (maskView.srv == nullptr)
    {
        // The folder's mask lies outside this window: folder coverage is 0 outside the mask's rectangle.
        _context->ClearRenderTargetView(target, kClearZero.data());
        return S_OK;
    }
    LayerConstants constants{};
    FillViewConstants(constants);
    constants.passMode = kCompositionPassMaskCoverage;
    constants.filterMode = group->transform.sampling == LayerSampling::Nearest ? 0u : 1u;
    FillTransform(MaskTransform(*group), constants.maskCenter, constants.maskSize, constants.maskSinCos,
                  constants.maskFlip);
    std::copy(maskView.rect.begin(), maskView.rect.end(), constants.maskRect);
    PassInputs inputs{};
    inputs.mask = maskView.srv;
    if (parent != kNoCoverage)
    {
        inputs.folder = _coverage[static_cast<std::size_t>(parent)].texture.srv.get();
        constants.flags |= kCompositionFlagHasFolder;
    }
    return RunPass(constants, inputs, target);
}

HRESULT CanvasRenderer::DrawLayerPass(const ImageLayer& layer, std::uint32_t pass, ID3D11ShaderResourceView* destSrv,
                                        ID3D11RenderTargetView* destRtv, int liveCoverage, int folderCoverage,
                                        bool& drawn) noexcept
{
    drawn = false;
    LayerConstants constants{};
    PassInputs inputs{};
    const HRESULT result = FillLayerConstants(layer, constants, inputs);
    if (result != S_OK)
    {
        // S_FALSE: nothing of the layer is in this window; the destination is left as it is.
        return SUCCEEDED(result) ? S_OK : result;
    }
    constants.passMode = pass;
    inputs.dest = destSrv;
    if (liveCoverage != kNoCoverage)
    {
        inputs.live = _coverage[static_cast<std::size_t>(liveCoverage)].texture.srv.get();
        constants.flags |= kCompositionFlagHasLive;
    }
    if (folderCoverage != kNoCoverage)
    {
        inputs.folder = _coverage[static_cast<std::size_t>(folderCoverage)].texture.srv.get();
        constants.flags |= kCompositionFlagHasFolder;
    }
    const HRESULT ran = RunPass(constants, inputs, destRtv);
    drawn = SUCCEEDED(ran);
    return ran;
}

void CanvasRenderer::SwapCanvas() noexcept
{
    _front = 1 - _front;
}

HRESULT CanvasRenderer::FillAdjustConstants(const ImageLayer& layer, LayerConstants& constants,
                                              PassInputs& inputs) noexcept
{
    constants = {};
    FillViewConstants(constants);
    FillTransform(layer.transform, constants.layerCenter, constants.layerSize, constants.layerSinCos,
                  constants.layerFlip);
    constants.blendMode = static_cast<std::uint32_t>(layer.blendMode);
    constants.filterMode = layer.transform.sampling == LayerSampling::Nearest ? 0u : 1u;
    constants.opacity = static_cast<float>(layer.opacity);
    if (HasEnabledMask(layer))
    {
        const LayerMask& mask = layer.mask.value();
        TextureView maskView;
        const HRESULT result = MaskView(layer, maskView);
        if (FAILED(result))
        {
            return result;
        }
        inputs.mask = maskView.srv;
        std::copy(maskView.rect.begin(), maskView.rect.end(), constants.maskRect);
        constants.flags |= kCompositionFlagHasMask;
        if (mask.placement.has_value() || result == S_FALSE)
        {
            constants.flags |= kCompositionFlagMaskPlaced;
            FillTransform(MaskTransform(layer), constants.maskCenter, constants.maskSize, constants.maskSinCos,
                          constants.maskFlip);
            constants.maskBackground = static_cast<float>(MaskEdgeBackground(mask.asset.thumbnail));
        }
    }
    return S_OK;
}

HRESULT CanvasRenderer::AdjustmentInputs(const ImageLayer& layer, LayerConstants& constants,
                                           PassInputs& inputs) noexcept
{
    if (!layer.adjustment.has_value() || !layer.adjustment->IsValid())
    {
        return E_INVALIDARG;
    }
    const LayerAdjustment& adjustment = layer.adjustment.value();
    AdjustTextures* entry = nullptr;
    try
    {
        entry = &_adjustments[layer.id];
    }
    catch (const std::bad_alloc&)
    {
        return E_OUTOFMEMORY;
    }
    const std::uint32_t kernel = AdjustKernel(adjustment.kind);
    const bool usesLut = kernel == kCompositionAdjustTables || kernel == kCompositionAdjustGradientMap;
    const bool usesCube = kernel == kCompositionAdjustCube;
    if (!(entry->value == adjustment) || (usesLut && !entry->lut.srv) || (usesCube && !entry->cubeSrv))
    {
        *entry = {};
        entry->value = adjustment;
        if (usesLut)
        {
            // Three 256-entry tables as one RGBA32F row (Levels, Curves, Exposure), or the gradient map's colors.
            std::array<float, 256 * 4> texels{};
            if (kernel == kCompositionAdjustGradientMap)
            {
                const std::array<std::uint8_t, 768> table = Adjust::GradientMapTable(adjustment.GradientMap());
                for (std::size_t i = 0; i < 256; ++i)
                {
                    texels[i * 4] = table[i * 3] / 255.0f;
                    texels[i * 4 + 1] = table[i * 3 + 1] / 255.0f;
                    texels[i * 4 + 2] = table[i * 3 + 2] / 255.0f;
                    texels[i * 4 + 3] = 1.0f;
                }
            }
            else
            {
                const Adjust::ChannelTables tables =
                    adjustment.kind == AdjustmentKind::Levels   ? Adjust::LevelsTables(adjustment.levels)
                    : adjustment.kind == AdjustmentKind::Curves ? Adjust::CurvesTables(adjustment.curves)
                                                                : Adjust::ExposureTables(adjustment.Exposure());
                for (std::size_t i = 0; i < 256; ++i)
                {
                    texels[i * 4] = tables.values[i];
                    texels[i * 4 + 1] = tables.values[256 + i];
                    texels[i * 4 + 2] = tables.values[512 + i];
                    texels[i * 4 + 3] = 1.0f;
                }
            }
            D3D11_TEXTURE2D_DESC desc{};
            desc.Width = 256;
            desc.Height = 1;
            desc.MipLevels = 1;
            desc.ArraySize = 1;
            desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
            desc.SampleDesc.Count = 1;
            desc.Usage = D3D11_USAGE_IMMUTABLE;
            desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            D3D11_SUBRESOURCE_DATA initial{};
            initial.pSysMem = texels.data();
            initial.SysMemPitch = 256 * 16;
            HRESULT result = _device->CreateTexture2D(&desc, &initial, entry->lut.texture.put());
            if (FAILED(result))
            {
                return result;
            }
            result = _device->CreateShaderResourceView(entry->lut.texture.get(), nullptr, entry->lut.srv.put());
            if (FAILED(result))
            {
                return result;
            }
            entry->lut.width = 256;
            entry->lut.height = 1;
        }
        if (usesCube)
        {
            std::vector<float> cube;
            try
            {
                cube = Adjust::ColorCube(adjustment.ResolvedHsv());
            }
            catch (const std::bad_alloc&)
            {
                return E_OUTOFMEMORY;
            }
            D3D11_TEXTURE3D_DESC desc{};
            desc.Width = Adjust::kCubeDimension;
            desc.Height = Adjust::kCubeDimension;
            desc.Depth = Adjust::kCubeDimension;
            desc.MipLevels = 1;
            desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
            desc.Usage = D3D11_USAGE_IMMUTABLE;
            desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            D3D11_SUBRESOURCE_DATA initial{};
            initial.pSysMem = cube.data();
            initial.SysMemPitch = Adjust::kCubeDimension * 16;
            initial.SysMemSlicePitch = Adjust::kCubeDimension * Adjust::kCubeDimension * 16;
            HRESULT result = _device->CreateTexture3D(&desc, &initial, entry->cubeTexture.put());
            if (FAILED(result))
            {
                return result;
            }
            D3D11_SHADER_RESOURCE_VIEW_DESC view{};
            view.Format = desc.Format;
            view.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE3D;
            view.Texture3D.MipLevels = 1;
            result = _device->CreateShaderResourceView(entry->cubeTexture.get(), &view, entry->cubeSrv.put());
            if (FAILED(result))
            {
                return result;
            }
        }
    }
    entry->lastUse = _composeId;
    constants.adjustKind = kernel;
    if (kernel == kCompositionAdjustGrain)
    {
        const GrainSettings grain = adjustment.Grain();
        constants.grainAmount = static_cast<float>(grain.amount);
        constants.grainSize = static_cast<float>(grain.size);
        constants.grainRoughness = static_cast<float>(grain.roughness);
        constants.grainSeed = grain.seed;
    }
    inputs.lut = entry->lut.srv.get();
    inputs.cube = entry->cubeSrv.get();
    return S_OK;
}

HRESULT CanvasRenderer::DrawAdjustPass(const ImageLayer& layer, std::uint32_t pass, ID3D11ShaderResourceView* destSrv,
                                         ID3D11RenderTargetView* destRtv, int folderCoverage) noexcept
{
    LayerConstants constants{};
    PassInputs inputs{};
    HRESULT result = FillAdjustConstants(layer, constants, inputs);
    if (FAILED(result))
    {
        return result;
    }
    result = AdjustmentInputs(layer, constants, inputs);
    if (FAILED(result))
    {
        return result;
    }
    constants.passMode = pass;
    inputs.dest = destSrv;
    if (folderCoverage != kNoCoverage)
    {
        inputs.folder = _coverage[static_cast<std::size_t>(folderCoverage)].texture.srv.get();
        constants.flags |= kCompositionFlagHasFolder;
    }
    return RunPass(constants, inputs, destRtv);
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
    bool drawn = false;
    result =
        DrawLayerPass(base, kCompositionPassStackBegin, nullptr, _stack[0].rtv.get(), kNoCoverage, kNoCoverage, drawn);
    if (FAILED(result))
    {
        return result;
    }
    if (!drawn)
    {
        // The base shows nothing in this window, so neither does anything clipped to it.
        return S_OK;
    }
    for (std::size_t offset = 0; offset < childCount; ++offset)
    {
        const ImageLayer* child = Find(_renderIds[firstChild + offset]);
        if (child == nullptr)
        {
            continue;
        }
        const int back = 1 - _stackFront;
        if (child->adjustment.has_value())
        {
            // A clipped adjustment reworks the base's straight color, keeping the stack alpha.
            result = DrawAdjustPass(*child, kCompositionPassStackAdjust, _stack[_stackFront].texture.srv.get(),
                                    _stack[back].rtv.get(), kNoCoverage);
            if (FAILED(result))
            {
                return result;
            }
            _stackFront = back;
            continue;
        }
        if (!child->asset.has_value() && !DrawsStrokeImage(*child))
        {
            continue;
        }
        result = DrawLayerPass(*child, kCompositionPassStackChild, _stack[_stackFront].texture.srv.get(),
                               _stack[back].rtv.get(), kNoCoverage, kNoCoverage, drawn);
        if (FAILED(result))
        {
            return result;
        }
        if (drawn)
        {
            _stackFront = back;
        }
    }
    LayerConstants constants{};
    FillViewConstants(constants);
    constants.passMode = kCompositionPassStackResolve;
    constants.blendMode = static_cast<std::uint32_t>(base.blendMode);
    PassInputs inputs{};
    inputs.source = _stack[_stackFront].texture.srv.get();
    inputs.dest = _canvas[_front].texture.srv.get();
    if (folderCoverage != kNoCoverage)
    {
        inputs.folder = _coverage[static_cast<std::size_t>(folderCoverage)].texture.srv.get();
        constants.flags |= kCompositionFlagHasFolder;
    }
    result = RunPass(constants, inputs, _canvas[1 - _front].rtv.get());
    if (FAILED(result))
    {
        return result;
    }
    SwapCanvas();
    return S_OK;
}

int CanvasRenderer::ScaleToFit(int width, int height) const noexcept
{
    int scale = 1;
    while ((CeilDiv(width, scale) > _textureLimit || CeilDiv(height, scale) > _textureLimit) && scale < (1 << 20))
    {
        scale *= 2;
    }
    return scale;
}

HRESULT CanvasRenderer::PrepareView(int originX, int originY, int scale, UINT width, UINT height) noexcept
{
    const HRESULT result = CreateCanvas(width, height);
    if (FAILED(result))
    {
        return result;
    }
    if (originX != _viewOriginX || originY != _viewOriginY || scale != _viewScale)
    {
        _viewOriginX = originX;
        _viewOriginY = originY;
        _viewScale = scale;
        _documentDirty = true;
    }
    return S_OK;
}

HRESULT CanvasRenderer::Compose(const CanvasDocument& document) noexcept
{
    if (!_device || !_context)
    {
        return E_UNEXPECTED;
    }
    if (document.width < 1 || document.height < 1)
    {
        return CreateCanvas(0, 0);
    }
    _documentWidth = document.width;
    _documentHeight = document.height;
    // The whole document, through a reduced proxy when a side exceeds the device's texture limit.
    _displayScale = ScaleToFit(document.width, document.height);
    HRESULT result = PrepareView(0, 0, _displayScale, static_cast<UINT>(CeilDiv(document.width, _displayScale)),
                                 static_cast<UINT>(CeilDiv(document.height, _displayScale)));
    if (FAILED(result) || _canvasWidth == 0)
    {
        return result;
    }
    if (!_documentDirty)
    {
        return S_OK;
    }
    result = ComposeView(document);
    if (FAILED(result))
    {
        return result;
    }
    _documentDirty = false;
    _canvasMipsStale = true;
    ++_compositeCount;
    return S_OK;
}

HRESULT CanvasRenderer::ComposeView(const CanvasDocument& document) noexcept
{
    _composeId = ++_composeSerial;
    _coverageUsed = 0;
    _byId.clear();
    _liveCoverageIndex.clear();
    _groupCoverageIndex.clear();
    try
    {
        for (const ImageLayer& layer : document.layers)
        {
            _byId.emplace(layer.id, &layer);
        }
        _renderIds = RenderLayerIds(document);
        _consumed.assign(_renderIds.size(), 0);
    }
    catch (const std::bad_alloc&)
    {
        return E_OUTOFMEMORY;
    }

    _context->ClearRenderTargetView(_canvas[0].rtv.get(), kClearZero.data());
    _context->ClearRenderTargetView(_canvas[1].rtv.get(), kClearZero.data());
    _front = 0;

    HRESULT result = S_OK;
    for (std::size_t index = 0; index < _renderIds.size(); ++index)
    {
        if (_consumed[index] != 0)
        {
            continue;
        }
        const ImageLayer* layer = Find(_renderIds[index]);
        if (layer == nullptr || layer->isGroup)
        {
            continue;
        }
        if (layer->adjustment.has_value())
        {
            // An adjustment layer reworks the composite beneath it; a clipped one only its base, inside the stack.
            if (!layer->maskSourceID.has_value())
            {
                int adjustFolder = kNoCoverage;
                if (layer->parentID.has_value())
                {
                    result = GroupCoverage(layer->parentID.value(), 0, adjustFolder);
                    if (FAILED(result))
                    {
                        return result;
                    }
                }
                result = DrawAdjustPass(*layer, kCompositionPassAdjust, _canvas[_front].texture.srv.get(),
                                        _canvas[1 - _front].rtv.get(), adjustFolder);
                if (FAILED(result))
                {
                    return result;
                }
                SwapCanvas();
            }
            continue;
        }
        if (!layer->asset.has_value() && !DrawsStrokeImage(*layer))
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
        bool drawn = false;
        result = DrawLayerPass(*layer, kCompositionPassLayer, _canvas[_front].texture.srv.get(),
                               _canvas[1 - _front].rtv.get(), live, folder, drawn);
        if (FAILED(result))
        {
            return result;
        }
        if (drawn)
        {
            SwapCanvas();
        }
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
    std::erase_if(_regions, [this](const GpuTexture& entry) { return entry.lastUse != _composeId; });
    for (auto entry = _adjustments.begin(); entry != _adjustments.end();)
    {
        if (entry->second.lastUse != _composeId)
        {
            entry = _adjustments.erase(entry);
        }
        else
        {
            ++entry;
        }
    }
    return S_OK;
}

HRESULT CanvasRenderer::ComposeReadback(const CanvasDocument& document, std::vector<std::uint8_t>& bgra, UINT& width,
                                          UINT& height) noexcept
{
    width = 0;
    height = 0;
    bgra.clear();
    if (!_device || !_context)
    {
        return E_UNEXPECTED;
    }
    if (document.width < 1 || document.height < 1 || document.width > kMaxCanvasSide ||
        document.height > kMaxCanvasSide)
    {
        return E_INVALIDARG;
    }
    const std::size_t documentWidth = static_cast<std::size_t>(document.width);
    const std::size_t documentHeight = static_cast<std::size_t>(document.height);
    try
    {
        bgra.resize(documentWidth * documentHeight * 4u);
    }
    catch (const std::bad_alloc&)
    {
        return E_OUTOFMEMORY;
    }
    const HRESULT result = ComposeReadbackRows(document, bgra.data());
    if (FAILED(result))
    {
        bgra.clear();
        return result;
    }
    width = static_cast<UINT>(document.width);
    height = static_cast<UINT>(document.height);
    return S_OK;
}

HRESULT CanvasRenderer::ComposeReadback(const CanvasDocument& document, Raster& image) noexcept
{
    image = {};
    if (!_device || !_context)
    {
        return E_UNEXPECTED;
    }
    if (document.width < 1 || document.height < 1 || document.width > kMaxCanvasSide ||
        document.height > kMaxCanvasSide)
    {
        return E_INVALIDARG;
    }
    PixelBuffer pixels = PixelBuffer::TryAllocate(static_cast<std::size_t>(document.width) *
                                                  static_cast<std::size_t>(document.height) * 4u);
    if (!pixels)
    {
        return E_OUTOFMEMORY;
    }
    const HRESULT result = ComposeReadbackRows(document, pixels.Data());
    if (FAILED(result))
    {
        return result;
    }
    try
    {
        image = Raster::Adopt(document.width, document.height, RasterFormat::Bgra8888, std::move(pixels));
    }
    catch (const std::bad_alloc&)
    {
        // A failed storage-header allocation fails this complete readback operation.
        return E_OUTOFMEMORY;
    }
    return image.HasStorage() ? S_OK : E_UNEXPECTED;
}

HRESULT CanvasRenderer::ComposeReadbackRows(const CanvasDocument& document, std::uint8_t* out) noexcept
{
    const std::size_t documentWidth = static_cast<std::size_t>(document.width);
    // The committed document, never the stroke preview; the display composes its own document again next frame.
    _ignoreStroke = true;
    // One window when the document fits; otherwise tiles small enough that a layer's part of one, with its filtering
    // margin, still fits a full-resolution texture.
    const bool fits = document.width <= _textureLimit && document.height <= _textureLimit;
    const int tile =
        fits ? std::max(document.width, document.height) : std::max(1, std::min(_textureLimit / 2, kReadbackTileSide));
    HRESULT result = S_OK;
    for (int top = 0; top < document.height && SUCCEEDED(result); top += tile)
    {
        for (int left = 0; left < document.width && SUCCEEDED(result); left += tile)
        {
            const UINT tileWidth = static_cast<UINT>(std::min(tile, document.width - left));
            const UINT tileHeight = static_cast<UINT>(std::min(tile, document.height - top));
            result = PrepareView(left, top, 1, tileWidth, tileHeight);
            if (SUCCEEDED(result))
            {
                result = ComposeView(document);
            }
            if (SUCCEEDED(result))
            {
                result = ReadCanvas(
                    out + (static_cast<std::size_t>(top) * documentWidth + static_cast<std::size_t>(left)) * 4u,
                    documentWidth * 4u, tileWidth, tileHeight);
            }
        }
    }
    _ignoreStroke = false;
    _documentDirty = true;
    return result;
}

HRESULT CanvasRenderer::BlitToTarget(ID3D11RenderTargetView* target, UINT targetWidth, UINT targetHeight,
                                       const CanvasViewport& viewport, float holeLeft, float holeTop, float holeWidth,
                                       float holeHeight) noexcept
{
    if (!_context || !target || !_canvas[_front].texture.srv)
    {
        return S_OK;
    }
    const double scale = viewport.ViewScale();
    const double devicePixelsPerTexel = scale * _viewScale;
    const bool reduces = _canvasHasMips && devicePixelsPerTexel > 0.0 && devicePixelsPerTexel < 1.0;
    if (reduces && _canvasMipsStale)
    {
        // Zoomed out: reduce the new composite through its mips, once per change (RF-412).
        _context->GenerateMips(_canvas[_front].texture.srv.get());
        _canvasMipsStale = false;
    }
    BlitConstants constants{};
    constants.viewSize[0] = static_cast<float>(targetWidth);
    constants.viewSize[1] = static_cast<float>(targetHeight);
    constants.holeOrigin[0] = holeLeft;
    constants.holeOrigin[1] = holeTop;
    constants.holeSize[0] = holeWidth;
    constants.holeSize[1] = holeHeight;
    const int documentWidth = _documentWidth > 0 ? _documentWidth : static_cast<int>(_canvasWidth);
    const int documentHeight = _documentHeight > 0 ? _documentHeight : static_cast<int>(_canvasHeight);
    constants.documentSize[0] = static_cast<float>(documentWidth);
    constants.documentSize[1] = static_cast<float>(documentHeight);
    constants.canvasExtent[0] = static_cast<float>(static_cast<double>(_canvasWidth) * _viewScale);
    constants.canvasExtent[1] = static_cast<float>(static_cast<double>(_canvasHeight) * _viewScale);
    constants.pan[0] = static_cast<float>(viewport.pan.x);
    constants.pan[1] = static_cast<float>(viewport.pan.y);
    constants.zoom = static_cast<float>(scale);
    constants.blitFlags =
        (viewport.SamplesCrisp() ? kCompositionBlitCrisp : 0u) | (viewport.ShowsPixelGrid() ? kCompositionBlitGrid : 0u);
    constants.canvasLod = reduces ? static_cast<float>(-std::log2(devicePixelsPerTexel)) : 0.0f;

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

HRESULT CanvasRenderer::ReadCanvas(std::uint8_t* out, std::size_t outStride, UINT width, UINT height) noexcept
{
    ID3D11Texture2D* front = _canvas[_front].texture.texture.get();
    if (front == nullptr || width > _canvasWidth || height > _canvasHeight)
    {
        return E_UNEXPECTED;
    }
    // Mip 0 of the canvas into a one-level staging copy.
    D3D11_TEXTURE2D_DESC description{};
    description.Width = width;
    description.Height = height;
    description.MipLevels = 1;
    description.ArraySize = 1;
    description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    description.SampleDesc.Count = 1;
    description.Usage = D3D11_USAGE_STAGING;
    description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    wil::com_ptr_nothrow<ID3D11Texture2D> staging;
    HRESULT result = _device->CreateTexture2D(&description, nullptr, staging.put());
    if (FAILED(result))
    {
        return result;
    }
    const D3D11_BOX box{0, 0, 0, width, height, 1};
    _context->CopySubresourceRegion(staging.get(), 0, 0, 0, 0, front, 0, &box);
    D3D11_MAPPED_SUBRESOURCE mapped{};
    result = _context->Map(staging.get(), 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(result))
    {
        return result;
    }
    for (UINT y = 0; y < height; ++y)
    {
        std::memcpy(out + static_cast<std::size_t>(y) * outStride,
                    static_cast<const std::uint8_t*>(mapped.pData) + static_cast<std::size_t>(y) * mapped.RowPitch,
                    static_cast<std::size_t>(width) * 4u);
    }
    _context->Unmap(staging.get(), 0);
    return S_OK;
}

HRESULT CanvasRenderer::Readback(std::vector<std::uint8_t>& bgra, UINT& width, UINT& height) noexcept
{
    bgra.clear();
    width = 0;
    height = 0;
    if (!_context || !_canvasWidth || !_canvas[_front].texture.texture)
    {
        return E_UNEXPECTED;
    }
    if (_viewScale != 1 || _viewOriginX != 0 || _viewOriginY != 0 ||
        (_documentWidth > 0 &&
         (static_cast<int>(_canvasWidth) != _documentWidth || static_cast<int>(_canvasHeight) != _documentHeight)))
    {
        // A reduced proxy or a readback tile is not the document: read it through ComposeReadback.
        return HRESULT_FROM_WIN32(ERROR_INVALID_STATE);
    }
    try
    {
        bgra.resize(static_cast<std::size_t>(_canvasWidth) * static_cast<std::size_t>(_canvasHeight) * 4u);
    }
    catch (const std::bad_alloc&)
    {
        return E_OUTOFMEMORY;
    }
    const HRESULT result =
        ReadCanvas(bgra.data(), static_cast<std::size_t>(_canvasWidth) * 4u, _canvasWidth, _canvasHeight);
    if (FAILED(result))
    {
        bgra.clear();
        return result;
    }
    width = _canvasWidth;
    height = _canvasHeight;
    return S_OK;
}

std::uint64_t CanvasRenderer::CanvasSurfaceBytes() const noexcept
{
    if (_canvasWidth == 0 || _canvasHeight == 0)
    {
        return 0;
    }
    const std::uint64_t pixels = static_cast<std::uint64_t>(_canvasWidth) * static_cast<std::uint64_t>(_canvasHeight);
    const std::uint64_t canvasTexels = _canvasHasMips ? MipChainTexels(_canvasWidth, _canvasHeight) : pixels;
    std::uint64_t bytes = 2ull * canvasTexels * 4ull;
    if (_stack[0].texture.texture)
    {
        bytes += 2ull * pixels * 4ull;
    }
    bytes += static_cast<std::uint64_t>(_coverage.size()) * pixels;
    for (const auto& [id, textures] : _adjustments)
    {
        bytes += textures.lut.texture ? 256ull * 16ull : 0ull;
        bytes += textures.cubeTexture ? static_cast<std::uint64_t>(Adjust::kCubeDimension) * Adjust::kCubeDimension *
                                            Adjust::kCubeDimension * 16ull
                                      : 0ull;
    }
    if (_strokeTexture.texture)
    {
        const std::uint64_t texels =
            _strokeTexture.mipped
                ? MipChainTexels(static_cast<std::uint64_t>(_strokeTexture.width),
                                 static_cast<std::uint64_t>(_strokeTexture.height))
                : static_cast<std::uint64_t>(_strokeTexture.width) * static_cast<std::uint64_t>(_strokeTexture.height);
        bytes += texels * (_strokeTexture.gray ? 1ull : 4ull);
    }
    return bytes;
}
