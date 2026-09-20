#pragma once

#include "CanvasViewport.h"
#include "Document.h"

#include <cstdint>
#include <d3d11_1.h>
#include <unordered_map>
#include <vector>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/com.h>
#pragma warning(pop)

class CanvasRenderer final
{
  public:
    CanvasRenderer() = default;
    ~CanvasRenderer();

    CanvasRenderer(const CanvasRenderer&) = delete;
    CanvasRenderer& operator=(const CanvasRenderer&) = delete;

    HRESULT CreateDeviceResources(ID3D11Device* device, ID3D11DeviceContext* context) noexcept;
    void ReleaseDeviceResources() noexcept;
    void ReleaseDocumentSurfaces() noexcept;
    HRESULT Compose(const CanvasDocument& document) noexcept;
    HRESULT BlitToTarget(ID3D11RenderTargetView* target, UINT targetWidth, UINT targetHeight,
                         const CanvasViewport& viewport, float holeLeft, float holeTop, float holeWidth,
                         float holeHeight) noexcept;
    HRESULT Readback(std::vector<std::uint8_t>& bgra, UINT& width, UINT& height) noexcept;
    [[nodiscard]] std::uint64_t CanvasSurfaceBytes() const noexcept;
    [[nodiscard]] bool HasDevice() const noexcept
    {
        return static_cast<bool>(_device);
    }
    [[nodiscard]] ID3D11Device* Device() const noexcept
    {
        return _device;
    }

  private:
    struct GpuTexture
    {
        wil::com_ptr_nothrow<ID3D11Texture2D> texture;
        wil::com_ptr_nothrow<ID3D11ShaderResourceView> srv;
        int width = 0;
        int height = 0;
    };

    struct LayerConstants
    {
        float canvasSize[2]{};
        float layerCenter[2]{};
        float layerSize[2]{};
        float rotationSin = 0;
        float rotationCos = 1;
        float flipSign[2]{1, 1};
        std::uint32_t blendMode = 0;
        std::uint32_t filterMode = 1;
        float opacity = 1;
        std::uint32_t hasMask = 0;
        std::uint32_t hasLiveMask = 0;
        std::uint32_t hasFolderMask = 0;
        float pad[2]{};
        float padEnd[2]{};
    };

    struct BlitConstants
    {
        float viewSize[2]{};
        float holeOrigin[2]{};
        float holeSize[2]{};
        float documentSize[2]{};
        float pan[2]{};
        float zoom = 1;
        float showGrid = 0;
        float pad[4]{};
    };

    HRESULT CreateCanvas(UINT width, UINT height) noexcept;
    HRESULT UploadRaster(const Raster& raster, GpuTexture& gpu, bool gray) noexcept;
    GpuTexture* TextureFor(const Raster& raster, bool gray) noexcept;
    HRESULT DrawLayer(const ImageLayer& layer, const ImageLayer* liveSource, const ImageLayer* folderSource,
                      ID3D11RenderTargetView* destRtv, ID3D11ShaderResourceView* destSrv) noexcept;

    ID3D11Device* _device = nullptr;
    ID3D11DeviceContext* _context = nullptr;
    wil::com_ptr_nothrow<ID3D11VertexShader> _vertexShader;
    wil::com_ptr_nothrow<ID3D11PixelShader> _pixelShader;
    wil::com_ptr_nothrow<ID3D11PixelShader> _blitPixelShader;
    wil::com_ptr_nothrow<ID3D11SamplerState> _linearSampler;
    wil::com_ptr_nothrow<ID3D11SamplerState> _pointSampler;
    wil::com_ptr_nothrow<ID3D11RasterizerState> _rasterizer;
    wil::com_ptr_nothrow<ID3D11Buffer> _layerConstants;
    wil::com_ptr_nothrow<ID3D11Buffer> _blitConstants;
    GpuTexture _ping;
    GpuTexture _pong;
    wil::com_ptr_nothrow<ID3D11RenderTargetView> _pingRtv;
    wil::com_ptr_nothrow<ID3D11RenderTargetView> _pongRtv;
    std::unordered_map<const void*, GpuTexture> _rasters;
    UINT _canvasWidth = 0;
    UINT _canvasHeight = 0;
    bool _pingIsFront = true;
};
