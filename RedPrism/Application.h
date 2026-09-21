#pragma once

#include "BrushCompute.h"
#include "CanvasRenderer.h"
#include "CanvasViewport.h"
#include "Document.h"
#include "EditorTool.h"
#include "Renderer.h"
#include "TransformEdit.h"
#include "TransformOverlay.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <windows.h>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/resource.h>
#pragma warning(pop)

class ChromeHost;
enum class TransformAction : std::uint8_t;
enum class SelectionAction : std::uint8_t;
enum class BrushAction : std::uint8_t;
enum class GradientAction : std::uint8_t;
enum class ShapeAction : std::uint8_t;

class Application final
{
  public:
    Application(HINSTANCE instance, bool forceWarp, const wchar_t* screenshotPath, uint32_t screenshotDpi,
                const wchar_t* demo = nullptr, const wchar_t* bench = nullptr) noexcept;
    ~Application();

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;
    Application(Application&&) = delete;
    Application& operator=(Application&&) = delete;

    int Run(int showCommand) noexcept;
    int RunSelfTest() noexcept;
    // `--bench brush`: the legacy 4K stroke benchmark on this device; prints BRUSH BENCH lines to stdout.
    int RunBenchmark() noexcept;

  private:
    static constexpr wchar_t kWindowClassName[] = L"RedPrism.Window";
    static constexpr UINT_PTR kScreenshotTimerId = 1;
    static LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) noexcept;
    static void RequestFrameThunk(void* context) noexcept;

    HRESULT RegisterWindowClass() noexcept;
    HRESULT CreateMainWindow(bool visible) noexcept;
    HRESULT RegisterDisplayPowerNotification(HWND window) noexcept;
    HRESULT InitializeRenderer() noexcept;
    HRESULT InitializeChrome() noexcept;
    HRESULT InitializeDocument() noexcept;
    void SyncChrome() noexcept;
    void DocumentChanged() noexcept;
    void BindCompositionDevice() noexcept;
    void CloseMainWindow() noexcept;
    bool WaitUntilMessage() noexcept;
    bool WaitForFrameLatency() noexcept;
    void CheckDeviceAdapter() noexcept;
    void OnPointer(UINT message, WPARAM wParam, LPARAM lParam) noexcept;
    void OnCommand(int command) noexcept;
    void OnTool(EditorTool tool) noexcept;
    // Move tool (C8, Specs/Document/Document_Tools.md): header actions, pointer drags, cursor and the demo document.
    void OnTransformAction(TransformAction action, double value) noexcept;
    void ChangeTransform(const std::function<void(LayerTransform&)>& update) noexcept;
    void BeginTransformDrag(Vec2 view, UINT modifiers) noexcept;
    void ContinueTransformDrag(Vec2 view, UINT modifiers) noexcept;
    void EndTransformDrag() noexcept;
    [[nodiscard]] std::optional<std::pair<Guid, bool>> TransformPressLayer(Vec2 pixel, bool control) const;
    [[nodiscard]] bool ApplyCanvasCursor() noexcept;
    void ApplyDemo() noexcept;
    // Selection tools (C9, Specs/Document/Document_Tools.md): header actions, pointer rules, keys and the wand's
    // composite sample. Outline and draft changes touch chrome only; the canvas never re-composites for them.
    void OnSelectionAction(SelectionAction action, double value) noexcept;
    void SelectionPress(Vec2 view, UINT modifiers, POINT client) noexcept;
    [[nodiscard]] bool SelectionPointerMove(Vec2 view, UINT modifiers) noexcept;
    void SelectionRelease(Vec2 view) noexcept;
    void EndSelectionInteraction(bool keepDraft) noexcept;
    void UpdateSelectionModifiers() noexcept;
    void SelectionChanged() noexcept;
    void RunMagicWand(Vec2 pixel, SelectionMode mode) noexcept;
    [[nodiscard]] std::optional<Raster> WandComposite() noexcept;
    // Pixel edits (C9b, Specs/Document/Document_Tools.md): the Edit menu's fills, clear, invert and clipboard,
    // Ctrl-drag and Ctrl-arrow pixel moves (shown through the stroke preview), Ctrl+T on a selection, the Delete
    // key and masks from the selection. A copy mirrors to the system clipboard as PNG and DIB; Paste brings another
    // app's image in when the system clipboard changed since that copy.
    void BeginPixelDrag(Vec2 pixel, bool duplicate) noexcept;
    void DragPixels(Vec2 pixel, bool constrain) noexcept;
    void FinishPixelDrag() noexcept;
    void CancelPixelDrag() noexcept;
    void SyncPixelMovePreview() noexcept;
    void PushPixelMoveTiles() noexcept;
    void MirrorClipboard() noexcept;
    void PasteFromClipboard() noexcept;
    // Brush and Eraser (C10, Specs/Rendering/Rendering_Brush.md): header actions, strokes through the compute
    // engine, the live preview in the composition, keys and the tip circle.
    void OnBrushAction(BrushAction action, double value) noexcept;
    void BeginBrushStroke(Vec2 view, UINT modifiers) noexcept;
    void ContinueBrushStroke(Vec2 view) noexcept;
    void FinishBrushStroke() noexcept;
    void CancelBrushStroke() noexcept;
    void PushStrokeTiles() noexcept;
    void SyncBrushCursor() noexcept;
    void DropBrushEngine() noexcept;
    [[nodiscard]] bool BrushErases() const noexcept;
    // Retouch tools (C11): Spot Healing, Clone Stamp and Blur go through the brush pipeline with a kind; Liquify
    // and Smudge through the warp pipeline (a document-size working copy shown through the stroke preview); the
    // Gradient's pending raster shows through the preview until Apply; the Shape's draft and the Eyedropper's ring
    // draw on the overlay.
    [[nodiscard]] BrushKind BrushKindFor() const noexcept;
    [[nodiscard]] bool WarpsWithSmear() const noexcept;
    void BeginWarpStroke(Vec2 view) noexcept;
    void ContinueWarpStroke(Vec2 view) noexcept;
    void FinishWarpStroke() noexcept;
    void CancelWarpStroke() noexcept;
    void PushWarpTiles() noexcept;
    void OnGradientAction(GradientAction action, double value) noexcept;
    void BeginGradientDrag(Vec2 view) noexcept;
    void DragGradient(Vec2 view, UINT modifiers) noexcept;
    void EndGradientDrag() noexcept;
    void CommitGradient() noexcept;
    void CancelGradient() noexcept;
    void SyncGradientPreview() noexcept;
    void PushGradientTiles() noexcept;
    void OnShapeAction(ShapeAction action, double value) noexcept;
    [[nodiscard]] bool SamplesColor() const noexcept;
    void SampleColorAt(Vec2 view) noexcept;
    void EndColorSampling() noexcept;
    void SyncToolOverlays() noexcept;
    bool HandleAccelerator(UINT virtualKey, UINT modifiers, bool repeat = false) noexcept;
    void NewDocument() noexcept;
    void OpenDocument() noexcept;
    void SaveDocument(bool saveAs) noexcept;
    void ExportFlattened(bool jpeg) noexcept;
    void OpenPath(const wchar_t* path) noexcept;
    void ZoomBy(double factor) noexcept;
    void ShowError(UINT stringId) noexcept;
    [[nodiscard]] Vec2 DocumentSize() const noexcept;
    LRESULT HandleMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam) noexcept;
    LRESULT OnSize(HWND window, UINT width, UINT height) noexcept;
    LRESULT OnDpiChanged(HWND window, const RECT* suggestedBounds) noexcept;

    HINSTANCE _instance = nullptr;
    wil::unique_hwnd _window;
    wil::unique_hpowernotify _displayPowerNotification;
    Renderer _renderer;
    std::unique_ptr<ChromeHost> _chrome;
    // Declared before the session: a pending stroke tells its engine when the session drops it.
    GpuBrushCoverage _brushEngine;
    DocumentSession _session;
    CanvasViewport _viewport;
    CanvasRenderer _composition;
    std::wstring _screenshotPath;
    std::wstring _documentPath;
    uint32_t _screenshotDpi = 0;
    EditorTool _tool = EditorTool::Move;
    POINT _lastPointer{};
    bool _spacePan = false;
    bool _canvasDrag = false;
    bool _opacityEditing = false;
    bool _layoutDirty = false;
    bool _duplicateOnDrag = false;
    std::optional<TransformDrag> _transformDrag;
    std::optional<TransformOverlayGeometry> _overlayGeometry;
    std::optional<Vec2> _selectionDragStart;
    Vec2 _selectionMoveDelta{};
    std::optional<Vec2> _marqueeDragPixel;
    bool _marqueeConstrainArmed = true;
    bool _lassoDragging = false;
    std::optional<Vec2> _pixelDragStart; // document point where a Ctrl-drag lifted the selected pixels
    DWORD _clipboardSequence = 0;        // the system clipboard's sequence number at the last mirrored copy
    ULONGLONG _lastPressTick = 0;
    POINT _lastPressPoint{};
    std::optional<DocumentSelection> _overlaySelection;
    bool _brushDragging = false;
    std::optional<Vec2> _brushHover; // document point under the pointer while a brush tool is active
    std::wstring _bench;
    bool _warpDragging = false;
    std::optional<int> _gradientHandle; // 0 start, 1 end, while a pending line's end is dragged
    bool _shapeDragging = false;
    bool _samplingColor = false;
    uint32_t _samplingOriginal = 0xFF000000u;
    std::optional<Raster> _sampleComposite;
    std::optional<Vec2> _samplePoint; // document point under the eyedropper while sampling
    Vec2 _overlayOrigin{};
    double _overlayScale = 0.0;
    std::wstring _demo;
    bool _quietUi = false;
    bool _forceWarp = false;
    bool _classRegistered = false;
    bool _rendererReady = false;
    bool _windowVisible = false;
    bool _displayPoweredOn = true;
    bool _occlusionStatusChanged = false;
    bool _frameInvalidated = true;
    bool _mouseTracked = false;
    bool _screenshotArmed = false;
    HRESULT _runtimeFailure = S_OK;
    HRESULT _screenshotResult = S_OK;
};
