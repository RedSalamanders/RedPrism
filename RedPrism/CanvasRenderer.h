#pragma once

#include "CanvasViewport.h"
#include "Document.h"

#include <array>
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
//
// The canvas is a window of the document (an origin and a scale). The display composes the whole document at scale
// 1, or through a reduced proxy (2, 4, ...) when a side exceeds the device's texture limit; ComposeReadback composes
// the committed document at full resolution, in tiles when it must. A raster larger than the limit is uploaded as
// the part the window shows, reduced by a power of two when that part is still too large; nothing ever asks the
// device for a texture it cannot create (Decision D1, Rendering_Composition.md "Documents larger than the device").
class GpuRecordingCommands;
struct PreparedWarpPreview;
class PreparedPixelMovePreview;

class CanvasRenderer final
{
  public:
    CanvasRenderer() = default;
    ~CanvasRenderer();

    CanvasRenderer(const CanvasRenderer&) = delete;
    CanvasRenderer& operator=(const CanvasRenderer&) = delete;

    HRESULT CreateDeviceResources(ID3D11Device* device, ID3D11DeviceContext* context,
                                  GpuRecordingCommands* recording = nullptr) noexcept;
    // Private producer-only preparation. Seed only from a retained, published/sealed graph whose owner blits
    // without editing its cache. This copies immutable raster residents, never adjustment/stroke writable targets.
    [[nodiscard]] ID3D11Texture2D* PreparedDisplayTextureForTest() const noexcept
    {
        return _recordedDisplaySealed ? _canvas[_front].texture.texture.get() : nullptr;
    }
    HRESULT SeedRasterResidencyForWork(const CanvasRenderer& stable) noexcept;
    HRESULT SealRecordedDisplayForWork() noexcept;
    // Parent has separately joined recording and acknowledged every original command batch. This binds the
    // already complete graph for owner blits; it neither establishes GPU completion nor adopts a reservation.
    HRESULT BindAcknowledgedDisplayOwner(ID3D11DeviceContext* context) noexcept;
    void ReleaseDeviceResources() noexcept;
    void ReleaseDocumentSurfaces() noexcept;
    // The next Compose re-renders every layer. Cheap; the application calls it after every document edit.
    void InvalidateDocument() noexcept
    {
        if (!_recordedDisplaySealed)
            _documentDirty = true;
    }
    // The display composite (with the stroke preview in place of its layer), at the proxy scale the document needs.
    HRESULT Compose(const CanvasDocument& document) noexcept;
    // Owner-only utility lifetime. Targets are separate from the display; shared raster residents are retained
    // by both uses. The last release drops utility surfaces/cache usage. No utility operation dirties the display.
    [[nodiscard]] bool AcquireUtility() noexcept;
    void ReleaseUtility() noexcept;
    // Record a committed document region at full resolution. The caller must record its staging Copy in this
    // same owner call: the reusable target can be overwritten by the next utility region. Never waits/readbacks.
    HRESULT ComposeUtilityTile(const CanvasDocument& document, const D3D11_BOX& region,
                               wil::com_ptr_nothrow<ID3D11Texture2D>& texture) noexcept;
    [[nodiscard]] std::uint64_t UtilitySurfaceBytes() const noexcept;
    [[nodiscard]] std::uint64_t UtilityCompositeCount() const noexcept
    {
        return _utilityCompositeCount;
    }
    struct AdjustmentPreview final
    {
        Guid layerId;
        Raster source;
        Raster coverage;
        std::shared_ptr<const AdjustmentLookup> lookup;
        std::optional<LayerAdjustment> liveOriginal;
        double previewScale = 1;
        [[nodiscard]] bool operator==(const AdjustmentPreview&) const noexcept = default;
    };
    void SetAdjustmentPreview(std::optional<AdjustmentPreview> preview) noexcept;
    // `document` flattened at full resolution into premultiplied BGRA rows (width x height x 4), without the stroke
    // preview: what export, Copy Merged, the wand and the eyedroppers read. Tiled when the document is larger than
    // the device's textures. The display composes again on its next frame.
    HRESULT ComposeReadback(const CanvasDocument& document, std::vector<std::uint8_t>& bgra, UINT& width,
                            UINT& height) noexcept;
    // Same committed pixels, adopted directly into an immutable raster without a second full-image copy.
    HRESULT ComposeReadback(const CanvasDocument& document, Raster& image) noexcept;
    HRESULT BlitToTarget(ID3D11RenderTargetView* target, UINT targetWidth, UINT targetHeight,
                         const CanvasViewport& viewport, float holeLeft, float holeTop, float holeWidth,
                         float holeHeight) noexcept;
    // Private S5 route proof only: complete same-device R8 mip-0 masks on a pixel-aligned view grid.
    // Parent retains the original admitted CPU/GPU epoch and both SRVs through actual drawing/retirement.
    // This records one overlay pass; it never composes, traverses points, creates resources or copies pixels.
    // Calls borrow the actual immediate owner context; no recorder or ordinary activation is authorized here.
    HRESULT DrawSelectionOutlineToTarget(ID3D11RenderTargetView* target, unsigned int targetWidth,
                                         unsigned int targetHeight, ID3D11ShaderResourceView* solid,
                                         ID3D11ShaderResourceView* dashes, unsigned int maskWidth,
                                         unsigned int maskHeight, float pixelLeft, float pixelTop,
                                         const std::array<float, 4>* clip = nullptr) noexcept;
    // One same-device premultiplied BGRA draft view. The original admitted parent owns the graph
    // through recording and actual context acknowledgement; the optional fractional clip applies once.
    HRESULT DrawLassoDraftToTarget(ID3D11RenderTargetView* target, unsigned int targetWidth, unsigned int targetHeight,
                                   ID3D11ShaderResourceView* color, unsigned int width, unsigned int height,
                                   float pixelLeft, float pixelTop,
                                   const std::array<float, 4>* clip = nullptr) noexcept;
    // The last display composite at document resolution. Fails while the display draws a reduced proxy: a document
    // readback goes through ComposeReadback.
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
        // Recorded worker only: complete document-grid Warp bytes over the exact placed original.
        std::shared_ptr<const PreparedWarpPreview> warpSource;
        int sourceX = 0; // where the source sits in the grid
        int sourceY = 0;
        // Explicit nearest stretch of the immutable source in the private recorder. Zero uses source size.
        int sourceWidth = 0;
        int sourceHeight = 0;
        std::uint8_t fill = 0; // pixels beyond the source (255 reveals for masks)
        // Complete immutable lifted-pixel reader; accepted only by the private recording path.
        std::shared_ptr<const PreparedPixelMovePreview> pixelSource;
    };
    HRESULT BeginStroke(const StrokePreview& preview) noexcept;
    // Exclusive private recorder only. Establish document/proxy geometry before any oversize grid upload.
    // Inherit only an exact same-grid seed into a NEW texture, with bounded GPU copies; no seed mutation.
    // Parent holds the published seed through this producer and all original command acknowledgements.
    HRESULT PrepareRecordedStrokeForWork(const CanvasDocument& document, const StrokePreview& preview,
                                         std::span<const RasterTile> changed, const CanvasRenderer* publishedSeed,
                                         bool& inherited) noexcept;
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
    // Harness borrow after the original recorder joined; no device commands, upload or source creation.
    [[nodiscard]] std::shared_ptr<const PreparedPixelMovePreview> PixelPreviewForTestOwner() const noexcept
    {
        return _stroke ? _stroke->pixelSource : nullptr;
    }
    // The layer and target (its mask or its image) the preview draws for, while one is live.
    [[nodiscard]] std::optional<Guid> StrokeLayerID() const noexcept
    {
        return _stroke.has_value() ? std::optional<Guid>(_stroke->layerID) : std::nullopt;
    }
    [[nodiscard]] bool StrokeTargetsMask() const noexcept
    {
        return _stroke.has_value() && _stroke->mask;
    }
    // Ends the preview. With `committed` (the stroke's snapshot, covering the grid from (cropX, cropY)) installed on
    // `committedLayerID`, the working texture is cropped into the raster cache so the committed layer draws without
    // uploading again. The adoption happens only when that layer is the preview's, the raster's format is the
    // target's (Gray8 for a mask), the crop lies inside the working texture and nothing was reduced; otherwise the
    // committed raster uploads on its own.
    void EndStroke(const Raster* committed = nullptr, int cropX = 0, int cropY = 0,
                   const Guid* committedLayerID = nullptr) noexcept;
    [[nodiscard]] std::uint64_t CanvasSurfaceBytes() const noexcept;
    [[nodiscard]] std::uint64_t CompositeCount() const noexcept
    {
        return _compositeCount;
    }
    [[nodiscard]] std::uint64_t AdjustmentTextureCreates() const noexcept
    {
        return _adjustmentTextureCreates;
    }
    [[nodiscard]] std::uint64_t AdjustmentTextureUpdates() const noexcept
    {
        return _adjustmentTextureUpdates;
    }
    // Working textures adopted by EndStroke so far (tests prove a commit drew from the texture, not an upload).
    [[nodiscard]] std::uint64_t AdoptionCount() const noexcept
    {
        return _adoptionCount;
    }
    [[nodiscard]] std::size_t CoverageTextureCount() const noexcept
    {
        return _coverage.size();
    }
    // Document pixels per canvas pixel of the display: 1, or a power of two for a document larger than the device.
    [[nodiscard]] int CanvasScale() const noexcept
    {
        return _displayScale;
    }
    // Textures holding part of a raster larger than the device's limit, as the last composition used them.
    [[nodiscard]] std::size_t RegionTextureCount() const noexcept
    {
        return _regions.size();
    }
    // The largest texture side the composition asks for: the Direct3D 11 limit (16 384); tests lower it to prove the
    // oversize paths on small documents.
    void SetTextureLimitForTest(int limit) noexcept;
    [[nodiscard]] int TextureLimit() const noexcept
    {
        return _textureLimit;
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
    HRESULT DrawViewOverlayToTarget(ID3D11RenderTargetView*, unsigned int targetWidth, unsigned int targetHeight,
                                    ID3D11ShaderResourceView*, ID3D11ShaderResourceView*, unsigned int width,
                                    unsigned int height, float pixelLeft, float pixelTop,
                                    const std::array<float, 4>* clip, bool colorDraft) noexcept;

    struct GpuTexture
    {
        wil::com_ptr_nothrow<ID3D11Texture2D> texture;
        wil::com_ptr_nothrow<ID3D11ShaderResourceView> srv;
        // Uploaded rasters keep their storage alive so a freed address cannot alias a different image; entries the
        // last composition did not use are released, which bounds residency to the current document.
        Raster keepAlive;
        std::uint64_t lastUse = 0;
        bool displayResident = false;
        int width = 0; // texels
        int height = 0;
        // The part of the raster (or stroke grid) the texels hold, in its pixels, and the reduction: each texel is
        // the average of `scale` x `scale` pixels. A whole texture is the entire raster at scale 1.
        int originX = 0;
        int originY = 0;
        int regionWidth = 0;
        int regionHeight = 0;
        int sourceWidth = 0;
        int sourceHeight = 0;
        int scale = 1;
        bool gray = false;
        bool mipped = false; // created with a mip chain the device generates
    };

    struct Surface
    {
        GpuTexture texture;
        wil::com_ptr_nothrow<ID3D11RenderTargetView> rtv;
    };

    // A texture as a pass samples it: the view and where the raster's unit square lands on its texels.
    struct TextureView
    {
        ID3D11ShaderResourceView* srv = nullptr;
        std::array<float, 4> rect{0.0f, 0.0f, 1.0f, 1.0f};
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
        // Adjustment passes (C12): the kernel and Grain's parameters; the tables come through textures.
        std::uint32_t adjustKind = 0;
        float grainAmount = 0;
        float grainSize = 1;
        float grainRoughness = 0;
        std::uint32_t grainSeed = 0;
        // The document window the canvas covers: canvas pixel (i, j) is the document square at origin + (i, j) *
        // scale.
        float viewOrigin[2]{};
        float viewScale = 1;
        float sourceRect[4]{0, 0, 1, 1};
        float maskRect[4]{0, 0, 1, 1};
    };

    struct BlitConstants
    {
        float viewSize[2]{};
        float holeOrigin[2]{};
        float holeSize[2]{};
        float documentSize[2]{};
        float pan[2]{};
        float zoom = 1;
        std::uint32_t blitFlags = 0;
        float canvasExtent[2]{};
        float canvasLod = 0; // the canvas mip the zoom reads: log2 of canvas texels per device pixel, at least 0
        float pad = 0;
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
        bool displayResident = false;
    };

    // A rectangle of a raster's (or grid's) pixels.
    struct PixelRect
    {
        int x = 0;
        int y = 0;
        int width = 0;
        int height = 0;
    };

    // A window of the document: its top-left and size in document pixels, and document pixels per canvas pixel.
    struct DocumentWindow
    {
        double x = 0;
        double y = 0;
        double width = 0;
        double height = 0;
        double scale = 1;
    };

    static constexpr int kNoCoverage = -1;
    // Tile side of a full-resolution readback of a document larger than the device (64 MiB per BGRA surface).
    static constexpr int kReadbackTileSide = 4096;

    HRESULT PrepareView(int originX, int originY, int scale, UINT width, UINT height) noexcept;
    HRESULT ComposeView(const CanvasDocument& document) noexcept;
    void SwapUtilityView() noexcept;
    void ReleaseUtilitySurfaces() noexcept;
    HRESULT ReadCanvas(std::uint8_t* out, std::size_t outStride, UINT width, UINT height) noexcept;
    HRESULT ComposeReadbackRows(const CanvasDocument& document, std::uint8_t* out) noexcept;
    [[nodiscard]] int ScaleToFit(int width, int height) const noexcept;
    HRESULT CreateCanvas(UINT width, UINT height) noexcept;
    HRESULT CreateSurface(UINT width, UINT height, DXGI_FORMAT format, bool mips, Surface& surface) noexcept;
    HRESULT EnsureStackSurfaces() noexcept;
    HRESULT AcquireCoverage(int& index) noexcept;
    HRESULT UploadRaster(const Raster& raster, GpuTexture& gpu, bool gray) noexcept;
    // A texture assembled on the GPU: cleared to `fill`, the base copied from its own texture, the tiles uploaded.
    HRESULT CreateSparseTexture(int width, int height, bool gray, const Raster& base, int baseX, int baseY,
                                std::uint8_t fill, std::span<const RasterTile> tiles, GpuTexture& gpu) noexcept;
    HRESULT CreateMippedTexture(int width, int height, bool gray, GpuTexture& gpu) noexcept;
    // Writes `tiles` of the texture's raster or grid into the part the texture holds, reduced by its scale.
    HRESULT WriteTiles(GpuTexture& gpu, std::span<const RasterTile> tiles, bool gray) noexcept;
    // Fills a region texture from `read(x, y, width, height, out, stride)` (rows of the raster or grid), band by band
    // and reduced by the texture's scale, so no allocation holds more than a band.
    template <typename Reader> HRESULT UploadRows(GpuTexture& gpu, Reader&& read) noexcept;
    // The part of a `width` x `height` raster placed by `mapping` that `window` shows (with a margin for filtering),
    // aligned to `align` pixels, and the power-of-two reduction that fits it in the texture limit. False when
    // nothing of it is in the window.
    [[nodiscard]] bool RegionFor(const LayerTransform& mapping, int width, int height, int align,
                                 const DocumentWindow& window, PixelRect& region, int& scale) const noexcept;
    [[nodiscard]] DocumentWindow CanvasWindow() const noexcept;
    void GenerateMipsFor(const GpuTexture& gpu) noexcept;
    HRESULT TextureFor(const Raster& raster, bool gray, GpuTexture*& texture) noexcept;
    HRESULT RegionTextureFor(const Raster& raster, bool gray, const PixelRect& region, int scale,
                             GpuTexture*& texture) noexcept;
    // S_OK with the view, S_FALSE when the raster shows nothing in the canvas window, or the failure.
    HRESULT RasterView(const Raster& raster, bool gray, const LayerTransform& mapping, TextureView& view) noexcept;
    HRESULT MaskView(const ImageLayer& layer, TextureView& view) noexcept;
    HRESULT CreateStrokeTexture(std::span<const RasterTile> tiles) noexcept;
    [[nodiscard]] static std::array<float, 4> RectFor(const GpuTexture& texture) noexcept;
    [[nodiscard]] bool DrawsStrokeImage(const ImageLayer& layer) const noexcept;
    [[nodiscard]] bool DrawsStrokeMask(const ImageLayer& layer) const noexcept;
    HRESULT RunPass(const LayerConstants& constants, const PassInputs& inputs, ID3D11RenderTargetView* target) noexcept;
    // S_OK, S_FALSE when the layer shows nothing in the canvas window, or the failure.
    HRESULT FillLayerConstants(const ImageLayer& layer, LayerConstants& constants, PassInputs& inputs) noexcept;
    void FillViewConstants(LayerConstants& constants) const noexcept;
    HRESULT LayerCoverage(const Guid& id, int depth, int& index) noexcept;
    HRESULT GroupCoverage(const Guid& groupId, int depth, int& index) noexcept;
    HRESULT DrawLayerPass(const ImageLayer& layer, std::uint32_t pass, ID3D11ShaderResourceView* destSrv,
                          ID3D11RenderTargetView* destRtv, int liveCoverage, int folderCoverage, bool& drawn) noexcept;
    HRESULT DrawStack(const ImageLayer& base, std::size_t firstChild, std::size_t childCount,
                      int folderCoverage) noexcept;
    // Adjustment layers (C12, Rendering_Composition.md): the composite beneath (or the stack's straight base color)
    // reworked through the layer's tables, blend mode, opacity, mask and folder coverage.
    HRESULT FillAdjustConstants(const ImageLayer& layer, LayerConstants& constants, PassInputs& inputs) noexcept;
    HRESULT AdjustmentInputs(const ImageLayer& layer, LayerConstants& constants, PassInputs& inputs) noexcept;
    HRESULT AdjustmentInputs(Guid id, const LayerAdjustment& adjustment, LayerConstants& constants, PassInputs& inputs,
                             const AdjustmentLookup* lookup) noexcept;
    HRESULT LayerAdjustmentView(TextureView& view) noexcept;
    HRESULT DrawAdjustPass(const ImageLayer& layer, std::uint32_t pass, ID3D11ShaderResourceView* destSrv,
                           ID3D11RenderTargetView* destRtv, int folderCoverage) noexcept;
    [[nodiscard]] const ImageLayer* Find(const Guid& id) const noexcept;
    void SwapCanvas() noexcept;

    void ClearRecorded(ID3D11RenderTargetView* target, const float* color) noexcept;
    void UpdateRecorded(ID3D11Resource* resource, UINT subresource, const D3D11_BOX* region, const void* data,
                        UINT rowPitch, UINT depthPitch) noexcept;
    void CopyRecorded(ID3D11Resource* destination, UINT destinationSubresource, UINT x, UINT y, UINT z,
                      ID3D11Resource* source, UINT sourceSubresource, const D3D11_BOX* region) noexcept;
    void GenerateRecordedMips(ID3D11ShaderResourceView* source) noexcept;
    GpuRecordingCommands* _recordingCommands = nullptr;
    bool _recordedDisplaySealed = false;
    std::uint32_t _recordedOwnerThread = 0;
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
    wil::com_ptr_nothrow<ID3D11BlendState> _selectionOutlineBlend;
    Surface _canvas[2];
    Surface _stack[2];
    std::vector<Surface> _coverage;
    std::size_t _coverageUsed = 0;
    std::unordered_map<const void*, GpuTexture> _rasters;
    std::vector<GpuTexture> _regions;
    std::unordered_map<Guid, AdjustTextures> _adjustments;
    std::optional<AdjustmentPreview> _adjustmentPreview;
    Surface _adjustmentPreviewSurface;
    bool _adjustmentPreviewDirty = true;
    std::optional<StrokePreview> _stroke;
    GpuTexture _strokeTexture;
    std::unordered_map<Guid, const ImageLayer*> _byId;
    std::unordered_map<Guid, int> _liveCoverageIndex;
    std::unordered_map<Guid, int> _groupCoverageIndex;
    std::vector<Guid> _renderIds;
    std::vector<std::uint8_t> _consumed;
    std::vector<std::uint8_t> _scratch; // reduced tile texels (oversize stroke previews only)
    UINT _canvasWidth = 0;
    UINT _canvasHeight = 0;
    bool _canvasHasMips = false;
    bool _canvasMipsStale = false;
    // The document window of the canvas (Rendering_Composition.md "Documents larger than the device").
    int _viewOriginX = 0;
    int _viewOriginY = 0;
    int _viewScale = 1;
    int _displayScale = 1;
    int _documentWidth = 0;
    int _documentHeight = 0;
    int _textureLimit = D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION;
    int _front = 0;
    int _stackFront = 0;
    std::uint64_t _compositeCount = 0;
    std::uint64_t _adjustmentTextureCreates = 0;
    std::uint64_t _adjustmentTextureUpdates = 0;
    std::uint64_t _adoptionCount = 0;
    std::uint64_t _composeSerial = 0;
    std::uint64_t _composeId = 0;
    bool _documentDirty = true;
    bool _ignoreStroke = false; // a readback composes the committed document, never the preview
    struct UtilityView final
    {
        Surface canvas[2];
        Surface stack[2];
        std::vector<Surface> coverage;
        std::size_t coverageUsed = 0;
        std::unordered_map<Guid, const ImageLayer*> byId;
        std::unordered_map<Guid, int> liveCoverageIndex;
        std::unordered_map<Guid, int> groupCoverageIndex;
        std::vector<Guid> renderIds;
        std::vector<std::uint8_t> consumed;
        UINT canvasWidth = 0;
        UINT canvasHeight = 0;
        bool canvasHasMips = false;
        bool canvasMipsStale = false;
        int viewOriginX = 0;
        int viewOriginY = 0;
        int viewScale = 1;
        int displayScale = 1;
        int documentWidth = 0;
        int documentHeight = 0;
        int front = 0;
        int stackFront = 0;
        bool documentDirty = true;
    };
    UtilityView _utilityView;
    std::size_t _utilityUsers = 0;
    std::uint64_t _utilityCompositeCount = 0;
    std::uint64_t _lastUtilityComposeId = 0;
    bool _utilityRecording = false;
};
