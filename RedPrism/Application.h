#pragma once

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

class Application final
{
  public:
    Application(HINSTANCE instance, bool forceWarp, const wchar_t* screenshotPath, uint32_t screenshotDpi,
                const wchar_t* demo = nullptr) noexcept;
    ~Application();

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;
    Application(Application&&) = delete;
    Application& operator=(Application&&) = delete;

    int Run(int showCommand) noexcept;
    int RunSelfTest() noexcept;

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
    ULONGLONG _lastPressTick = 0;
    POINT _lastPressPoint{};
    std::optional<DocumentSelection> _overlaySelection;
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
