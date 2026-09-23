#pragma once

#include "BrushCompute.h"
#include "CanvasRenderer.h"
#include "CanvasSize.h"
#include "CanvasViewport.h"
#include "ChromeEvents.h"
#include "CommandBindings.h"
#include "Crop.h"
#include "Document.h"
#include "EditorTool.h"
#include "Renderer.h"
#include "SettingsStore.h"
#include "TransformEdit.h"
#include "TransformOverlay.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>
#include <windows.h>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/resource.h>
#pragma warning(pop)

class ChromeHost;
enum class SizeCardKind : std::uint8_t;
namespace DxUi
{
struct PointerEvent;
}

// The chrome's events (ChromeEvents) land on the handlers below, marked `override`.
class Application final : public ChromeEvents
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
    // Settings (Specs/Core/Core_Settings.md): the file to read, watch and write; empty for a run without one.
    void UseSettings(std::wstring path, const AppSettings& settings) noexcept;

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
    // ApplicationSettings.cpp: the settings file, its event-driven reload on the UI thread, the theme and the C14
    // gates.
    void ApplySettings(const AppSettings& settings) noexcept;
    void RefreshTheme() noexcept;
    bool StartSettingsWatch() noexcept;
    void StopSettingsWatch() noexcept;
    void NoteSettingsFileState() noexcept;
    void OnSettingsDirectoryChanged(bool immediate) noexcept;
    bool ReloadSettingsIfChanged() noexcept;
    void SaveSettings() noexcept;
    [[nodiscard]] HANDLE SettingsWatchHandle() const noexcept;
    int RunProductHardeningSelfTest() noexcept;
    static constexpr UINT_PTR kSettingsTimerId = 7;
    void SyncChrome() noexcept;
    void DocumentChanged() noexcept;
    void BindCompositionDevice() noexcept;
    void CloseMainWindow() noexcept;
    bool WaitUntilMessage(DWORD timeout = INFINITE) noexcept;
    bool WaitForFrameLatency() noexcept;
    void CheckDeviceAdapter() noexcept;
    // The frame loop (Rendering_Composition.md "Failures"): one frame with device-loss recovery, the canvas failure
    // notice, the stop that asks to close through the save prompt, surfaces dropped while unseen, the exit code.
    void RenderFrame() noexcept;
    void ReportCanvasFailure(HRESULT failure) noexcept;
    void OnFrameLoopFailure(HRESULT failure) noexcept;
    void ReleaseSurfacesWhileUnseen() noexcept;
    [[nodiscard]] int ExitCode(int quitCode) const noexcept;
    int RunRenderingSelfTest() noexcept;
    void OnPointer(UINT message, WPARAM wParam, LPARAM lParam) noexcept;
    void OnCommand(int command) noexcept override;
    void OnTool(EditorTool tool) noexcept;
    // The chrome's layers panel, color picker and layout events (next to InitializeChrome).
    void OnLayerSelected(Guid id) noexcept override;
    void OnLayerExpanded(Guid id, bool expanded) noexcept override;
    void OnLayerReordered(Guid source, Guid target, std::optional<Guid> parent,
                          DxUi::TreeDropPlace place) noexcept override;
    void OnPickerColor(PickerTarget target, ChromeEditPhase phase, uint32_t argb) noexcept override;
    void OnChromeLayout() noexcept override;
    // Move tool (C8, Specs/Document/Document_Tools.md): header actions, pointer drags, cursor and the demo document.
    void OnTransformAction(TransformAction action, double value) noexcept override;
    void ChangeTransform(const std::function<void(LayerTransform&)>& update) noexcept;
    void BeginTransformDrag(Vec2 view, UINT modifiers) noexcept;
    void ContinueTransformDrag(Vec2 view, UINT modifiers) noexcept;
    void EndTransformDrag() noexcept;
    [[nodiscard]] std::optional<std::pair<Guid, bool>> TransformPressLayer(Vec2 pixel, bool control) const;
    [[nodiscard]] bool ApplyCanvasCursor() noexcept;
    void ApplyDemo() noexcept;
    // Selection tools (C9, Specs/Document/Document_Tools.md): header actions, pointer rules, keys and the wand's
    // composite sample. Outline and draft changes touch chrome only; the canvas never re-composites for them.
    void OnSelectionAction(SelectionAction action, double value) noexcept override;
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
    // Color adjustments and filters (C12, ApplicationAdjust.cpp): the Image and Filter menus, the adjustment panel's
    // actions, adjustment layers edited over the composite beneath them, and the eyedroppers on the canvas.
    bool BeginColorEdit(int command) noexcept;
    void AddAdjustmentLayer(int command) noexcept;
    void EditActiveAdjustment() noexcept;
    void OpenAdjustmentEditor() noexcept;
    [[nodiscard]] std::optional<Raster> CompositeBelow(const Guid& id) noexcept;
    void ApplyColorEdit() noexcept;
    void CancelColorEdit() noexcept;
    void OnAdjustAction(AdjustAction action, double value) noexcept override;
    void OnCurveChanged(std::vector<CurvePoint> points) noexcept override;
    void OnBandChanged(int index, double degrees) noexcept override;
    void OnAdjustColor(ChromeEditPhase phase, uint32_t argb, bool highlights) noexcept;
    void SyncAdjustPanel() noexcept;
    // ApplicationSize.cpp (C16, Specs/UI/UI_ApplicationChrome.md "Size cards"): the New Canvas, Canvas Size and
    // Image Size cards edit a draft the Application owns; Apply resizes as one entry.
    [[nodiscard]] bool SizeCardOpen() const noexcept;
    void OpenSizeCard(SizeCardKind kind) noexcept;
    void CancelSizeCard() noexcept;
    void ApplySizeCard() noexcept;
    void OnSizeAction(SizeAction action, double value) noexcept override;
    void OnSizeColor(ChromeEditPhase phase, uint32_t argb) noexcept;
    void SyncSizeCard() noexcept;
    int RunSizeSelfTest() noexcept;
    [[nodiscard]] bool AdjustPointer(const DxUi::PointerEvent& event, Vec2 view, bool inHole) noexcept;
    // Brush and Eraser (C10, Specs/Rendering/Rendering_Brush.md): header actions, strokes through the compute
    // engine, the live preview in the composition, keys and the tip circle.
    void OnBrushAction(BrushAction action, double value) noexcept override;
    void BeginBrushStroke(Vec2 view, UINT modifiers) noexcept;
    void ContinueBrushStroke(Vec2 view) noexcept;
    void FinishBrushStroke() noexcept;
    void CancelBrushStroke() noexcept;
    void PushStrokeTiles() noexcept;
    // Ends the composition's stroke preview; its working texture becomes `commit`'s only when the commit landed on
    // the preview's own layer and target (RF-407).
    void EndStrokePreview(const std::optional<BrushCommitInfo>& commit) noexcept;
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
    void OnGradientAction(GradientAction action, double value) noexcept override;
    void BeginGradientDrag(Vec2 view) noexcept;
    void DragGradient(Vec2 view, UINT modifiers) noexcept;
    void EndGradientDrag() noexcept;
    void CommitGradient() noexcept;
    void CancelGradient() noexcept;
    void SyncGradientPreview() noexcept;
    void PushGradientTiles() noexcept;
    void OnShapeAction(ShapeAction action, double value) noexcept override;
    // ApplicationCrop.cpp (C16, Specs/UI/UI_ApplicationChrome.md "Crop tool"): the frame's drags, the header's
    // actions, Enter / Escape, the cursor and the self-test.
    void BeginCropDrag(Vec2 view) noexcept;
    void ContinueCropDrag(Vec2 view) noexcept;
    void EndCropDrag() noexcept;
    void ApplyCrop() noexcept;
    void CancelCropFrame() noexcept;
    void OnCropAction(CropAction action, double value) noexcept override;
    void SyncCropChrome() noexcept;
    [[nodiscard]] LPCWSTR CropCursor(Vec2 view) const noexcept;
    int RunCropSelfTest() noexcept;
    // ApplicationLayerEdits.cpp (C16): the Rename Layer card and Merge Down through the composition.
    void RenameActiveLayer() noexcept;
    void OnLayerInvoked(Guid id) noexcept override;
    void OnRename(std::optional<std::wstring> name) noexcept override;
    // The card closes without renaming: its layer or the document went away, or an interaction settled.
    void CloseRenameCard() noexcept;
    void MergeLayersCommand() noexcept;
    // The layers panel's opacity stepper: a typed edit previews on the layer it started on and records one entry
    // when it commits, or when anything else (a command, a tool, a canvas press, another layer, a save) closes it.
    void OnLayerOpacity(double opacity, ChromeEditPhase phase) noexcept override;
    void CloseOpacityEdit() noexcept;
    [[nodiscard]] ImageLayer* OpacityEditLayer() noexcept;
    int RunLayerEditsSelfTest() noexcept;
    [[nodiscard]] bool SamplesColor() const noexcept;
    void SampleColorAt(Vec2 view) noexcept;
    void EndColorSampling() noexcept;
    void SyncToolOverlays() noexcept;
    // ApplicationKeyboard.cpp (Specs/UI/UI_ApplicationChrome.md "Menus and accelerators"): a key down goes to the
    // gestures of an edit in progress, then the effective binding table (`_bindings`), then the chrome; the menu
    // bar takes a tapped Alt, F10 and Alt+letter.
    LRESULT OnKeyDown(HWND window, UINT message, WPARAM wParam, LPARAM lParam) noexcept;
    LRESULT OnKeyUp(HWND window, UINT message, WPARAM wParam, LPARAM lParam) noexcept;
    LRESULT OnCharacter(HWND window, UINT message, WPARAM wParam, LPARAM lParam) noexcept;
    void OnKeyMenu(wchar_t character) noexcept;
    bool HandleKey(const KeyPress& press) noexcept;
    bool HandleEditGesture(const KeyPress& press) noexcept;
    bool HandleCanvasKey(const KeyPress& press) noexcept;
    [[nodiscard]] KeyState CurrentKeyState() const noexcept;
    // Self-test driver: one key press through the window procedure as the message loop delivers it — the key down
    // (a system key with Alt), the character TranslateMessage posts, the key up — with the thread's key state
    // holding the modifiers (MK_CONTROL, MK_SHIFT, kKeyAlt) and a real scan code (the US OEM bracket and number-row
    // keys name their physical position). True when the key path took the key down.
    static constexpr UINT kKeyAlt = 0x0100;
    bool HandleAccelerator(UINT virtualKey, UINT modifiers, bool repeat = false) noexcept;
    int RunKeyboardSelfTest() noexcept;
    void NewDocument() noexcept;
    void OpenDocument() noexcept;
    [[nodiscard]] bool SaveDocument(bool saveAs) noexcept;
    void ExportFlattened(bool jpeg) noexcept;
    void OpenPath(const wchar_t* path) noexcept;
    // ApplicationFiles.cpp (C16, Specs/UI/UI_ApplicationChrome.md "Files" and "Unimplemented functions"): images
    // opened as a layer or a new project, the save prompt, Import Images, Close Project and the notice for a
    // function that has no implementation yet.
    enum class ImageOpenChoice : std::uint8_t
    {
        AddLayer,
        NewProject,
        Cancel,
    };
    static constexpr UINT kNotImplementedMessage = WM_APP + 7; // Renderer owns WM_APP + 1
    static constexpr UINT kEndSessionMessage = WM_APP + 8;     // the save prompt a refused session end posts
    [[nodiscard]] ImageOpenChoice PromptImageOpen(const std::wstring& firstName, std::size_t count) noexcept;
    [[nodiscard]] bool ConfirmDiscardChanges() noexcept;
    // Modified history, or a pending edit that Save would record (a live stroke or warp, a pending gradient, a
    // changed transform, a typed opacity).
    [[nodiscard]] bool HasUnsavedWork() const noexcept;
    // Before Save and Export (Decision D2): the typed opacity records its entry, a live stroke, warp or pending
    // gradient commits, a pending transform applies, and an open panel applies as its OK would.
    void SettleForWrite() noexcept;
    // Session end (Specs/UI/UI_Windowing.md): a modified document refuses with a block reason and asks through the
    // posted prompt; the settings are saved when the session ends.
    [[nodiscard]] bool OnQueryEndSession(LPARAM reasons) noexcept;
    void OnEndSession(bool ending) noexcept;
    void RunEndSessionPrompt() noexcept;
    // A close or the session-end prompt waits while a message it arrived inside (a modal loop, a chrome dispatch) is
    // still on the stack; the outermost window-procedure return posts it again.
    [[nodiscard]] bool CloseMustWait() const noexcept;
    void ReleaseDeferredClose() noexcept;
    // Capture or focus went elsewhere (a dialog, a menu, another app): every pointer interaction ends.
    void EndPointerInteractions() noexcept;
    // Quiet runs drive the pointer synthetically from a hidden window: they honor only the losses their own thread
    // causes, never a capture or focus change another thread or process sends.
    [[nodiscard]] bool HonorsPointerLoss() const noexcept;
    void OpenPaths(const std::vector<std::wstring>& paths) noexcept;
    [[nodiscard]] bool OpenProjectPath(const wchar_t* path) noexcept;
    void ReplaceDocument(CanvasDocument document, const std::optional<Guid>& active, std::wstring path) noexcept;
    [[nodiscard]] bool ImportImagePaths(const std::vector<std::wstring>& paths, bool newProject) noexcept;
    void ImportImages() noexcept;
    void CloseProject() noexcept;
    // Ends every interaction in flight before a tool switch (panels apply) or before the document goes (they cancel).
    void SettleInteractions(bool applyPanels) noexcept;
    void ShowNotImplemented(UINT captionId) noexcept;
    void ShowNotImplementedNotice(UINT captionId) noexcept;
    void ShowUnavailable(UINT captionId) noexcept;
    int RunFilesSelfTest() noexcept;
    int RunSessionSelfTest() noexcept;
    int RunClipboardSelfTest() noexcept;
    void ZoomBy(double factor) noexcept;
    void ShowError(UINT stringId) noexcept;
    [[nodiscard]] Vec2 DocumentSize() const noexcept;
    LRESULT HandleMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam) noexcept;
    LRESULT OnSize(UINT width, UINT height) noexcept;
    LRESULT OnDpiChanged(HWND window, const RECT* suggestedBounds) noexcept;
    void OnGetMinMaxInfo(HWND window, MINMAXINFO* info) const noexcept;

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
    bool _acceleratorShift = false;
    CommandBindings _bindings;       // the effective binding table: catalog defaults and the settings' replacements
    bool _suppressCharacter = false; // the key down ran a command or a gesture: its WM_CHAR / WM_SYSCHAR types nothing
    bool _lastKeyHandled = false;    // the last key down went to a command or a gesture (read by the self-test driver)
    bool _canvasDrag = false;
    bool _releasingCapture = false; // a button-up releases the capture: not a capture loss
    // The typed opacity edit: the layer it started on, that layer's opacity before the first preview (what history
    // holds) and the value previewed. Previews change the document outside history; one entry records the edit.
    struct OpacityEdit
    {
        Guid layer;
        double start = 1.0;
        double shown = 1.0;
    };
    std::optional<OpacityEdit> _opacityEdit;
    // The value an edit closed from outside recorded while the field kept its text: the field's late commit of that
    // same value is already in history and changes nothing (not even another document's layer).
    std::optional<double> _opacityRecorded;
    std::optional<Guid> _renameLayer; // the layer the open Rename card renames
    int _messageDepth = 0;            // window-procedure calls on the stack
    bool _closeDeferred = false;      // a WM_CLOSE waits for the stack to unwind
    bool _closeConfirmed = false;     // the session-end prompt already asked: the next WM_CLOSE does not
    bool _endSessionPending = false;  // a session end was refused and its prompt has not answered yet
    bool _endSessionPromptDeferred = false;
    bool _layoutDirty = false;
    bool _duplicateOnDrag = false;
    std::optional<TransformDrag> _transformDrag;
    std::optional<CropDrag> _cropDrag;
    std::optional<TransformOverlayGeometry> _overlayGeometry;
    std::optional<Vec2> _selectionDragStart;
    Vec2 _selectionMoveDelta{};
    std::optional<Vec2> _marqueeDragPixel;
    bool _marqueeConstrainArmed = true;
    bool _lassoDragging = false;
    std::optional<Vec2> _pixelDragStart;      // document point where a Ctrl-drag lifted the selected pixels
    std::optional<double> _hueTargetStart;    // view x where a targeted Hue/Saturation drag began
    GradientMapSettings _adjustColorOriginal; // the Gradient Map ends when its color picker opened
    DWORD _clipboardSequence = 0; // the system clipboard's sequence number right after the last copy was offered to it
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
    double _overlayBacking = 0.0;
    bool _marchingAnts = false; // a non-empty selection outline shows (SyncChrome); the frame loop steps its dash
    // The Zoom tool's press: where, the zoom then, and whether the drag has gone past the click slop.
    struct ZoomDrag
    {
        POINT start{};
        double zoom = 1;
        bool moved = false;
    };
    std::optional<ZoomDrag> _zoomDrag;
    // The gradient or pixel-move raster the composition's stroke preview was begun for (never dereferenced: a new
    // raster, or an expired one, restarts the preview).
    std::weak_ptr<const BrushStroke> _previewSource;
    std::wstring _demo;
    bool _quietUi = false;
    int _notImplementedCount = 0;                    // notices raised; quiet runs count instead of showing them
    std::optional<ImageOpenChoice> _forcedImageOpen; // the self-test's answer to the image-open prompt
    std::optional<bool> _forcedDiscard;              // the self-test's answer to the save prompt (true: Don't Save)
    struct SizeDialog
    {
        SizeCardKind kind{}; // None
        CanvasSizeDraft canvas;
        int anchor = 4;
        int extension = 0; // 0 transparent, 1 foreground, 2 background, 3 black, 4 white, 5 custom
        uint32_t customArgb = 0xFFFFFFFFu;
        uint32_t customBefore = 0xFFFFFFFFu; // the custom color before the picker opened (Cancel restores)
        ImageSizeDraft image;
        double newWidth = 1920; // New Canvas: the last typed size, or the clipboard image's
        double newHeight = 1080;
    };
    SizeDialog _sizeDialog;
    bool _forceWarp = false;
    std::wstring _settingsPath;
    AppSettings _settings;
    wil::unique_hfind_change _settingsWatch;
    FILETIME _settingsSeenWrite{};
    std::uint64_t _settingsSeenSize = 0;
    bool _restoreMaximized = false;
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
    HRESULT _frameLoopFailure = S_OK; // drawing stopped; the close it asked for prompts to save (RF-401)
    HRESULT _lastFrameFailure = S_OK;
    int _failedFrames = 0;
    bool _canvasFailureShown = false;
    int _canvasFailureNotices = 0; // quiet runs count them
    std::uint64_t _boundDeviceGeneration = 0;
};
