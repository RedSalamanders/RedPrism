#pragma once

#include "CanvasViewport.h"
#include "Document.h"

#include <cstdint>
#include <d3d11_1.h>
#include <optional>
#include <span>
#include <unordered_map>
#include <vector>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/com.h>
#pragma warning(pop)

// Document-space GPU composition (Rendering_Composition.md). Borrows the application device; owns no swap chain.
//
// Composition happens only when the document changed (InvalidateDocument) or the canvas surfaces were rebuilt.
// Each visible pixel layer is one fullscreen pass into ping-pong canvas textures. Coverage that another layer
// depends on (live clipping sources, folder masks) is rendered once per composition into pooled R8 textures in
// document space, exactly as the legacy LiveMaskRenderer / FolderMaskClip define it. A clipping stack (a base plus
// the contiguous clipped layers above it) composites through a scratch stack surface that keeps the base alpha.
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
    // The next Compose re-renders every layer. Cheap; the application calls it after every document edit.
    void InvalidateDocument() noexcept
    {
        _documentDirty = true;
    }
    HRESULT Compose(const CanvasDocument& document) noexcept;
    HRESULT BlitToTarget(ID3D11RenderTargetView* target, UINT targetWidth, UINT targetHeight,
                         const CanvasViewport& viewport, float holeLeft, float holeTop, float holeWidth,
                         float holeHeight) noexcept;
    HRESULT Readback(std::vector<std::uint8_t>& bgra, UINT& width, UINT& height) noexcept;

    // A brush stroke in progress on one layer (Rendering_Brush.md): the grid it paints, the pixels it started from
    // and where they sit. The composition keeps one working texture for the grid, writes only the tiles that change,
    // and draws it in place of the layer's image (or mask) until EndStroke.
    struct StrokePreview
    {
        Guid layerID;
        bool mask = false;
        int width = 0;
        int height = 0;
        LayerTransform transform; // places the grid on the document
        Raster source;            // the layer's image (or mask) the stroke started from; may be empty
        int sourceX = 0;          // where the source sits in the grid
        int sourceY = 0;
        std::uint8_t fill = 0; // pixels beyond the source (255 reveals for masks)
    };
    HRESULT BeginStroke(const StrokePreview& preview) noexcept;
    // Tiles of the grid whose pixels changed; every touched tile when StrokeTextureMissing() (the texture is then
    // created from the source and the fill first).
    HRESULT UpdateStrokeTiles(std::span<const RasterTile> tiles) noexcept;
    [[nodiscard]] bool StrokeTextureMissing() const noexcept
    {
        return _stroke.has_value() && !_strokeTexture.srv;
    }
    [[nodiscard]] bool HasStroke() const noexcept
    {
        return _stroke.has_value();
    }
    // Ends the preview. With `committed` (the stroke's snapshot, covering the grid from (cropX, cropY)), the working
    // texture is cropped into the raster cache so the committed layer draws without uploading again.
    void EndStroke(const Raster* committed, int cropX, int cropY) noexcept;
    [[nodiscard]] std::uint64_t CanvasSurfaceBytes() const noexcept;
    [[nodiscard]] std::uint64_t CompositeCount() const noexcept
    {
        return _compositeCount;
    }
    [[nodiscard]] std::size_t CoverageTextureCount() const noexcept
    {
        return _coverage.size();
    }
    [[nodiscard]] bool HasDevice() const noexcept
    {
        return static_cast<bool>(_device);
    }
    [[nodiscard]] ID3D11Device* Device() const noexcept
    {
        return _device;
    }

  private:
    enum class Pass : std::uint32_t
    {
        Layer = 0,
        StackBegin = 1,
        StackChild = 2,
        StackResolve = 3,
        LayerCoverage = 4,
        MaskCoverage = 5,
        Adjust = 6,
        StackAdjust = 7,
    };

    enum Flags : std::uint32_t
    {
        kFlagHasMask = 1u,
        kFlagHasLive = 2u,
        kFlagHasFolder = 4u,
        kFlagMaskPlaced = 8u,
    };

    struct GpuTexture
    {
        wil::com_ptr_nothrow<ID3D11Texture2D> texture;
        wil::com_ptr_nothrow<ID3D11ShaderResourceView> srv;
        // Uploaded rasters keep their storage alive so a freed address cannot alias a different image; entries the
        // last composition did not use are released, which bounds residency to the current document.
        Raster keepAlive;
        std::uint64_t lastUse = 0;
        int width = 0;
        int height = 0;
    };

    struct Surface
    {
        GpuTexture texture;
        wil::com_ptr_nothrow<ID3D11RenderTargetView> rtv;
    };

    struct LayerConstants
    {
        float canvasSize[2]{};
        float layerCenter[2]{};
        float layerSize[2]{};
        float layerSinCos[2]{0, 1};
        float layerFlip[2]{1, 1};
        float maskCenter[2]{};
        float maskSize[2]{1, 1};
        float maskSinCos[2]{0, 1};
        float maskFlip[2]{1, 1};
        float maskBackground = 1;
        float opacity = 1;
        std::uint32_t blendMode = 0;
        std::uint32_t filterMode = 1;
        std::uint32_t passMode = 0;
        std::uint32_t flags = 0;
        // Adjustment passes (C12): the kind and Grain's parameters; the tables come through textures.
        std::uint32_t adjustKind = 0;
        float grainAmount = 0;
        float grainSize = 1;
        float grainRoughness = 0;
        std::uint32_t grainSeed = 0;
        float adjustPad[3]{};
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

    struct PassInputs
    {
        ID3D11ShaderResourceView* source = nullptr;
        ID3D11ShaderResourceView* dest = nullptr;
        ID3D11ShaderResourceView* mask = nullptr;
        ID3D11ShaderResourceView* live = nullptr;
        ID3D11ShaderResourceView* folder = nullptr;
        ID3D11ShaderResourceView* lut = nullptr;  // 256 x 1 RGBA32F: per-channel tables, or the gradient map
        ID3D11ShaderResourceView* cube = nullptr; // 33^3 RGBA32F: the Hue/Saturation color cube
    };

    // An adjustment layer's lookup textures, rebuilt when its settings change and released when a composition no
    // longer uses them.
    struct AdjustTextures
    {
        LayerAdjustment value;
        GpuTexture lut;
        wil::com_ptr_nothrow<ID3D11Texture3D> cubeTexture;
        wil::com_ptr_nothrow<ID3D11ShaderResourceView> cubeSrv;
        std::uint64_t lastUse = 0;
    };

    static constexpr int kNoCoverage = -1;

    HRESULT CreateCanvas(UINT width, UINT height) noexcept;
    HRESULT CreateSurface(UINT width, UINT height, DXGI_FORMAT format, Surface& surface) noexcept;
    HRESULT EnsureStackSurfaces() noexcept;
    HRESULT AcquireCoverage(int& index) noexcept;
    HRESULT UploadRaster(const Raster& raster, GpuTexture& gpu, bool gray) noexcept;
    // A texture assembled on the GPU: cleared to `fill`, the base copied from its own texture, the tiles uploaded.
    HRESULT CreateSparseTexture(int width, int height, bool gray, const Raster& base, int baseX, int baseY,
                                std::uint8_t fill, std::span<const RasterTile> tiles, GpuTexture& gpu) noexcept;
    HRESULT CreateMippedTexture(int width, int height, bool gray, GpuTexture& gpu) noexcept;
    HRESULT WriteTiles(GpuTexture& gpu, std::span<const RasterTile> tiles, bool gray) noexcept;
    [[nodiscard]] ID3D11ShaderResourceView* MaskViewFor(const ImageLayer& layer) noexcept;
    GpuTexture* TextureFor(const Raster& raster, bool gray) noexcept;
    HRESULT RunPass(const LayerConstants& constants, const PassInputs& inputs, ID3D11RenderTargetView* target) noexcept;
    void FillLayerConstants(const ImageLayer& layer, LayerConstants& constants, PassInputs& inputs, bool& ok) noexcept;
    HRESULT LayerCoverage(const Guid& id, int depth, int& index) noexcept;
    HRESULT GroupCoverage(const Guid& groupId, int depth, int& index) noexcept;
    HRESULT DrawLayerPass(const ImageLayer& layer, Pass pass, ID3D11ShaderResourceView* destSrv,
                          ID3D11RenderTargetView* destRtv, int liveCoverage, int folderCoverage) noexcept;
    HRESULT DrawStack(const ImageLayer& base, std::size_t firstChild, std::size_t childCount,
                      int folderCoverage) noexcept;
    // Adjustment layers (C12, Rendering_Composition.md): the composite beneath (or the stack's straight base color)
    // reworked through the layer's tables, blend mode, opacity, mask and folder coverage.
    void FillAdjustConstants(const ImageLayer& layer, LayerConstants& constants, PassInputs& inputs) noexcept;
    HRESULT AdjustmentInputs(const ImageLayer& layer, LayerConstants& constants, PassInputs& inputs) noexcept;
    HRESULT DrawAdjustPass(const ImageLayer& layer, Pass pass, ID3D11ShaderResourceView* destSrv,
                           ID3D11RenderTargetView* destRtv, int folderCoverage) noexcept;
    [[nodiscard]] const ImageLayer* Find(const Guid& id) const noexcept;
    void SwapCanvas() noexcept;

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
    Surface _canvas[2];
    Surface _stack[2];
    std::vector<Surface> _coverage;
    std::size_t _coverageUsed = 0;
    std::unordered_map<const void*, GpuTexture> _rasters;
    std::unordered_map<Guid, AdjustTextures> _adjustments;
    std::optional<StrokePreview> _stroke;
    GpuTexture _strokeTexture;
    std::unordered_map<Guid, const ImageLayer*> _byId;
    std::unordered_map<Guid, int> _liveCoverageIndex;
    std::unordered_map<Guid, int> _groupCoverageIndex;
    std::vector<Guid> _renderIds;
    std::vector<std::uint8_t> _consumed;
    UINT _canvasWidth = 0;
    UINT _canvasHeight = 0;
    int _front = 0;
    int _stackFront = 0;
    std::uint64_t _compositeCount = 0;
    std::uint64_t _composeId = 0;
    bool _documentDirty = true;
};
