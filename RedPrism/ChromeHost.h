#pragma once

#include "Document.h"
#include "EditorTool.h"
#include "TransformOverlay.h"

#include <array>
#include <d3d11.h>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <windows.h>

#include <DxUi/DxUi.h>
#include <DxUi/Embedded.h>
#include <DxUi/NativeMenuInterop.h>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/resource.h>
#pragma warning(pop)

using unique_hmenu = wil::unique_any<HMENU, decltype(&::DestroyMenu), ::DestroyMenu>;

class CanvasOverlay;

// The canvas hole in physical pixels: the rectangle the document composition blits into.
struct CanvasHole
{
    float left = 0;
    float top = 0;
    float width = 0;
    float height = 0;
};

// Phase of a chrome-driven edit that previews live and settles on commit or cancel (the DxUi phases, app-typed so
// Application never names a DxUi enum).
enum class ChromeEditPhase : std::uint8_t
{
    Preview,
    Commit,
    Cancel,
};

// What the Move tool's header asks of the host. Numeric actions carry the typed value; flags carry 0 or 1.
enum class TransformAction : std::uint8_t
{
    SetX,
    SetY,
    SetWidth,
    SetHeight,
    SetScale,
    SetRotation,
    SetSampling,
    FlipHorizontal,
    FlipVertical,
    Apply,
    Cancel,
    AutoSelect,
    ShowControls,
    LockRatio,
};

// The Move tool's header state: the numbers describe the edited transform (draft or active layer).
struct TransformInspectorState
{
    bool visible = false;
    bool enabled = false;   // a transformable layer or a pending edit
    bool editing = false;   // a pending edit: Apply and Cancel are live
    bool distorted = false; // while distorted the handles are the controls; numbers are read-only
    bool targetsMask = false;
    double x = 0;
    double y = 0;
    double width = 1;
    double height = 1;
    double scale = 100;
    double rotation = 0;
    int sampling = 2;
    bool autoSelect = false;
    bool showControls = true;
    bool lockRatio = true;
};

// Editor chrome on the one HWND: MenuBar, document tabs, tool rail with the palette wells, tool header, layers
// splitter, layers panel with the opacity stepper, status strip, and the color picker card. Owns one DxUi
// EmbeddedHost that borrows the application device. Every visible string comes from the STRINGTABLE.
class ChromeHost final
{
  public:
    static constexpr float kMenuHeightDip = 30.0f;
    static constexpr float kTabHeightDip = 32.0f;
    static constexpr float kHeaderHeightDip = 36.0f;
    static constexpr float kStatusHeightDip = 24.0f;
    static constexpr float kRailWidthDip = 48.0f;
    static constexpr float kToolButtonDip = 40.0f;
    static constexpr float kToolGapDip = 2.0f;
    static constexpr float kSplitterWidthDip = DxUi::Splitter::kDefaultThicknessDip;
    static constexpr float kCanvasMinDip = 200.0f;
    static constexpr float kLayersMinDip = 160.0f;
    static constexpr float kLayersMaxDip = 480.0f;
    static constexpr float kLayersDefaultDip = 240.0f;
    static constexpr float kLayersTitleDip = 32.0f;
    static constexpr float kOpacityWidthDip = 96.0f;
    static constexpr float kWellDip = 26.0f;
    static constexpr float kWellOffsetDip = 14.0f;
    static constexpr float kWellBlockDip = 48.0f;
    static constexpr float kPickerMarginDip = 8.0f;

    static constexpr int kCommandNew = 100;
    static constexpr int kCommandOpen = 101;
    static constexpr int kCommandSave = 102;
    static constexpr int kCommandSaveAs = 103;
    static constexpr int kCommandExportPng = 104;
    static constexpr int kCommandExportJpeg = 105;
    static constexpr int kCommandExit = 106;
    static constexpr int kCommandUndo = 200;
    static constexpr int kCommandRedo = 201;
    static constexpr int kCommandFreeTransform = 202;
    static constexpr int kCommandNewLayer = 300;
    static constexpr int kCommandNewFolder = 301;
    static constexpr int kCommandDeleteLayer = 302;
    static constexpr int kCommandToggleLayerVisibility = 303;
    static constexpr int kCommandMoveOutOfGroup = 304;
    static constexpr int kCommandFlipLayerHorizontal = 305;
    static constexpr int kCommandFlipLayerVertical = 306;
    static constexpr int kCommandZoomFit = 400;
    static constexpr int kCommandZoomActual = 401;
    static constexpr int kCommandZoomIn = 402;
    static constexpr int kCommandZoomOut = 403;
    static constexpr int kCommandAbout = 500;
    static constexpr int kCommandFlipCanvasHorizontal = 600;
    static constexpr int kCommandFlipCanvasVertical = 601;

    enum class MenuIndex : std::size_t
    {
        File = 0,
        Edit,
        Image,
        Layer,
        Select,
        Filter,
        View,
        Help,
        Count,
    };

    ChromeHost() = default;
    ~ChromeHost();

    ChromeHost(const ChromeHost&) = delete;
    ChromeHost& operator=(const ChromeHost&) = delete;

    HRESULT Attach(ID3D11Device* device, void* requestContext, void (*requestFrame)(void*) noexcept) noexcept;
    HRESULT ReplaceDevice(ID3D11Device* device) noexcept;
    void Detach() noexcept;
    void SetVisible(bool visible) noexcept;
    void SetOwnerWindow(HWND window) noexcept;
    void SetCommandHandler(std::function<void(int)> handler);
    void SetToolHandler(std::function<void(EditorTool)> handler);
    void SetLayerHandler(std::function<void(Guid)> handler);
    void SetLayerExpandHandler(std::function<void(Guid, bool)> handler);
    // Committed palette (foreground, background) after the picker's OK or Enter.
    void SetPaletteHandler(std::function<void(uint32_t, uint32_t)> handler);
    // Active layer opacity in 0…1 from the stepper: typing previews, Enter/blur/buttons commit, Escape cancels.
    void SetOpacityHandler(std::function<void(double, ChromeEditPhase)> handler);
    // The canvas hole changed from inside chrome (splitter drag or keyboard); the host re-syncs the viewport.
    void SetLayoutHandler(std::function<void()> handler);
    // The Move tool's header: typed numbers, flips, sampling, Apply/Cancel and the toggles.
    void SetTransformHandler(std::function<void(TransformAction, double)> handler);
    void SetTransformInspector(const TransformInspectorState& state) noexcept;
    // The transform box in canvas-hole pixels (nullopt hides it) and snap guides in canvas-hole pixels.
    void SetTransformOverlay(const std::optional<TransformOverlayGeometry>& geometry, const std::vector<float>& guideXs,
                             const std::vector<float>& guideYs) noexcept;
    void SetDocumentTitle(std::wstring title, bool dirty);
    void SetStatus(std::wstring zoom, std::wstring size, std::wstring tool);
    void SetUndoRedoNames(const std::wstring& undoName, const std::wstring& redoName, bool canUndo, bool canRedo);
    void SetTool(EditorTool tool) noexcept;
    void SetLayers(std::vector<HierarchyEntry> rows, const std::optional<Guid>& active);
    void SetPalette(uint32_t foregroundArgb, uint32_t backgroundArgb) noexcept;
    // nullopt disables the stepper (no active raster layer). Ignored while the user is typing into it.
    void SetActiveLayerOpacity(std::optional<double> opacity) noexcept;
    // Persisted layers width; clamped by the splitter on the next Prepare.
    void SetLayersWidth(float widthDip) noexcept;
    void OpenColorPicker(bool background) noexcept;
    // Host-driven close (Escape fallback, self-test): commits or cancels the open picker.
    void CloseColorPicker(bool commit) noexcept;
    // Host eyedropper result while the picker is open (previews, does not commit).
    void SampleColor(uint32_t argb) noexcept;
    // Self-test hook: one committed stepper change, as the increment button would produce.
    [[nodiscard]] bool RequestLayerOpacity(double percent) noexcept;
    HRESULT Prepare(UINT widthPixels, UINT heightPixels, float dpi) noexcept;
    HRESULT Composite(ID3D11DeviceContext* context, const D3D11_VIEWPORT& viewport) noexcept;
    bool DispatchPointer(const DxUi::PointerEvent& event) noexcept;
    bool DispatchKey(UINT virtualKey, bool down, UINT modifiers = 0) noexcept;
    bool DispatchCharacter(wchar_t character, UINT modifiers = 0) noexcept;
    bool AdvanceAnimation(uint64_t nowTickMs) noexcept;
    [[nodiscard]] bool NeedsPreparation() const noexcept;
    [[nodiscard]] bool NeedsAnimation() const noexcept;
    [[nodiscard]] DxUi::EmbeddedStatistics Statistics() const noexcept;
    [[nodiscard]] CanvasHole HolePixels() const noexcept;
    [[nodiscard]] float LayersWidthDip() const noexcept
    {
        return _layersWidthDip;
    }
    [[nodiscard]] EditorTool Tool() const noexcept
    {
        return _tool;
    }
    [[nodiscard]] bool CanvasFocused() const noexcept
    {
        return _canvasFocused;
    }
    void SetCanvasFocused(bool focused) noexcept
    {
        _canvasFocused = focused;
    }
    [[nodiscard]] bool ColorPickerOpen() const noexcept;
    [[nodiscard]] uint32_t ForegroundColor() const noexcept
    {
        return _foregroundArgb;
    }
    [[nodiscard]] uint32_t BackgroundColor() const noexcept
    {
        return _backgroundArgb;
    }
    // Rail button bounds in DIP (index order of EditorTool); tests and the harness use it to prove layout.
    [[nodiscard]] D2D1_RECT_F ToolButtonBoundsDip(EditorTool tool) const noexcept;
    // Separator bar of the layers splitter, palette wells, and the picker card, in DIP, for the same purpose.
    [[nodiscard]] D2D1_RECT_F SplitterBoundsDip() const noexcept;
    [[nodiscard]] D2D1_RECT_F PaletteWellBoundsDip(bool background) const noexcept;
    [[nodiscard]] D2D1_RECT_F ColorPickerBoundsDip() const noexcept;

  private:
    class LayerTreeModel final : public DxUi::IDxTreeModel, public DxUi::IDxTreeDelegate
    {
      public:
        std::vector<HierarchyEntry> rows;
        std::optional<Guid> active;
        std::wstring hiddenPattern;
        std::function<void(Guid)>* onLayer = nullptr;
        std::function<void(Guid, bool)>* onExpand = nullptr;
        [[nodiscard]] size_t GetVisibleItemCount() const noexcept override;
        void GetVisibleItem(size_t visibleIndex, DxUi::TreeItemData& outItem) const override;
        void OnTreeSelectionChanged(uint64_t itemId) override;
        void OnTreeToggleExpanded(uint64_t itemId, bool expanded) override;
    };

    HRESULT BindDevice(ID3D11Device* device) noexcept;
    HRESULT Build() noexcept;
    void Arrange() noexcept;
    void ArrangePicker() noexcept;
    void BuildTransformHeader(DxUi::Panel& root);
    void ArrangeTransformHeader(float left, float top, float right, float bottom) noexcept;
    void BuildMenus() noexcept;
    void OpenMenu(size_t index, POINT screen, bool keyboard) noexcept;
    void SyncToolVisuals() noexcept;
    void OnSplitterChange(DxUi::SplitterChange change) noexcept;
    void OnPickerChange(DxUi::ColorPickerChange change) noexcept;
    void FinishPicker(bool commit, uint32_t argb) noexcept;
    [[nodiscard]] float WidthDip() const noexcept;
    [[nodiscard]] float HeightDip() const noexcept;

    DxUi::EmbeddedHost _view;
    std::shared_ptr<DxUi::GraphicsDevice> _graphics;
    void* _requestContext = nullptr;
    void (*_requestFrame)(void*) noexcept = nullptr;
    HWND _owner = nullptr;
    std::array<unique_hmenu, static_cast<std::size_t>(MenuIndex::Count)> _menus;
    DxUi::MenuBar* _menu = nullptr;
    DxUi::TabControl* _tabs = nullptr;
    DxUi::Toolbar* _rail = nullptr;
    DxUi::Label* _header = nullptr;
    DxUi::Tree* _layers = nullptr;
    DxUi::StatusStrip* _status = nullptr;
    DxUi::Label* _layersTitle = nullptr;
    DxUi::Splitter* _splitter = nullptr;
    DxUi::ColorSwatch* _foregroundWell = nullptr;
    DxUi::ColorSwatch* _backgroundWell = nullptr;
    DxUi::NumericStepper* _opacity = nullptr;
    DxUi::CardPanel* _pickerCard = nullptr;
    DxUi::ColorPicker* _picker = nullptr;
    CanvasOverlay* _overlay = nullptr;
    DxUi::Panel* _transformHeader = nullptr;
    DxUi::Label* _transformTitle = nullptr;
    DxUi::Checkbox* _autoSelect = nullptr;
    DxUi::Checkbox* _showControls = nullptr;
    DxUi::Checkbox* _lockRatio = nullptr;
    DxUi::NumericStepper* _transformX = nullptr;
    DxUi::NumericStepper* _transformY = nullptr;
    DxUi::NumericStepper* _transformWidth = nullptr;
    DxUi::NumericStepper* _transformHeight = nullptr;
    DxUi::NumericStepper* _transformScale = nullptr;
    DxUi::NumericStepper* _transformAngle = nullptr;
    DxUi::ComboBox* _sampling = nullptr;
    DxUi::Button* _flipHorizontal = nullptr;
    DxUi::Button* _flipVertical = nullptr;
    DxUi::Button* _transformCancel = nullptr;
    DxUi::Button* _transformApply = nullptr;
    TransformInspectorState _inspector;
    bool _syncingInspector = false;
    std::array<DxUi::Button*, kEditorToolCount> _toolButtons{};
    LayerTreeModel _treeModel;
    std::function<void(int)> _onCommand;
    std::function<void(EditorTool)> _onTool;
    std::function<void(Guid)> _onLayer;
    std::function<void(Guid, bool)> _onExpand;
    std::function<void(uint32_t, uint32_t)> _onPalette;
    std::function<void(double, ChromeEditPhase)> _onOpacity;
    std::function<void()> _onLayout;
    std::function<void(TransformAction, double)> _onTransform;
    UINT _width = 0;
    UINT _height = 0;
    float _dpi = 96.0f;
    float _layersWidthDip = kLayersDefaultDip;
    float _preferredLayersWidthDip = kLayersDefaultDip;
    uint32_t _foregroundArgb = 0xFF000000u;
    uint32_t _backgroundArgb = 0xFFFFFFFFu;
    EditorTool _tool = EditorTool::Move;
    bool _pickerBackground = false;
    bool _canvasFocused = true;
    bool _attached = false;
};
