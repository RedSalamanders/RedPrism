#pragma once

#include "AccessibilityRoot.h"
#include "AdjustControls.h"
#include "CanvasOverlay.h"
#include "CommandBindings.h"
#include "Document.h"
#include "EditorTool.h"
#include "TransformOverlay.h"

#include <array>
#include <d3d11.h>
#include <functional>
#include <initializer_list>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>
#include <windows.h>

#include <DxUi/DxUi.h>
#include <DxUi/Embedded.h>
#include <DxUi/EmbeddedAccessibility.h>
#include <DxUi/NativeMenuInterop.h>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/resource.h>

#include "ThemePalette.h"

class ChromeAccessibilitySite;
class PaletteControl;
#pragma warning(pop)

using unique_hmenu = wil::unique_any<HMENU, decltype(&::DestroyMenu), ::DestroyMenu>;

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

// What the selection tools' header asks of the host. Numeric actions carry the typed value or the chosen index;
// flags carry 0 or 1; Expand, Contract and Deselect carry nothing.
enum class SelectionAction : std::uint8_t
{
    SetMarqueeKind,
    SetLassoKind,
    SetMode,
    SetAntialias,
    SetTolerance,
    SetSampleSize,
    SetSampleAllLayers,
    SetContiguous,
    SetExpandAmount,
    Expand,
    SetContractAmount,
    Contract,
    Deselect,
};

// What the Brush and Eraser header asks of the host (Specs/Rendering/Rendering_Brush.md): a chosen index, a typed
// number (size in pixels, hardness and opacity in percent) or a request to open the color picker.
enum class BrushAction : std::uint8_t
{
    SetMode,
    SetSize,
    SetHardness,
    SetOpacity,
    SetMaskPaint,
    OpenColor,
    SetHealMode,
    SetCloneAligned,
    SetCloneSample,
    SetSmearMode,
};

struct BrushInspectorState
{
    bool visible = false;
    bool enabled = false;
    EditorTool tool = EditorTool::Brush;
    int mode = 0; // 0 paint, 1 erase
    double size = 40;
    double hardness = 100;
    double opacity = 100;
    bool maskSelected = false;
    bool maskPaintWhite = false;
    uint32_t foregroundArgb = 0xFF000000u;
    // Spot Healing, Clone Stamp and Smear (C11).
    int healMode = 0; // Content-Aware, Create Texture, Proximity Match
    bool cloneAligned = true;
    bool cloneAllLayers = false;
    bool cloneHasSource = false;
    int smearMode = 0; // Liquify, Blur, Smudge
};

// The Gradient header (C11): shape, the two end colors, style, Reverse, opacity, and Cancel / Apply while a line is
// pending.
enum class GradientAction : std::uint8_t
{
    SetShape,
    SetStyle,
    SetReversed,
    SetOpacity,
    Cancel,
    Apply,
    OpenColor,
};

struct GradientInspectorState
{
    bool enabled = false;
    int shape = 0; // Linear, Radial
    int style = 1; // Foreground to Background, Foreground to Transparent
    bool reversed = false;
    double opacity = 100;
    bool maskSelected = false;
    bool pending = false;
    uint32_t startArgb = 0xFF000000u;
    uint32_t endArgb = 0x00000000u; // alpha 0: the transparent end
};

// The Shape header (C11): kind, corner radius (rectangles) and the fill swatch.
enum class ShapeAction : std::uint8_t
{
    SetKind,
    SetRadius,
    OpenColor,
};

struct ShapeInspectorState
{
    bool enabled = false;
    int kind = 0; // Rectangle, Ellipse
    double radius = 0;
    uint32_t foregroundArgb = 0xFF000000u;
};

// The Crop header (C16): the ratio choice, the frame's size, Cancel and Apply Crop.
enum class CropAction : std::uint8_t
{
    SetRatio, // 0 free, 1 original, 2 1:1, 3 4:3, 4 16:9
    Cancel,
    Apply,
};

struct CropInspectorState
{
    bool enabled = false; // a document is open
    int ratio = 0;
    bool hasFrame = false;
    int width = 0;
    int height = 0;
};

// The adjustment panel (C12, Specs/UI/UI_ApplicationChrome.md): which editor it shows and every value the session
// holds for it, mirrored through SetAdjustInspector; its controls report changes as actions with one value.
enum class AdjustPanelKind : std::uint8_t
{
    None,
    Filter,
    Levels,
    HueSaturation,
};

enum class AdjustAction : std::uint8_t
{
    SetPreview,
    Cancel,
    Apply,
    Reset,
    SetChannel,  // Curves and Levels: 0 RGB, 1 red, 2 green, 3 blue
    RemovePoint, // Curves: the point at this index
    SetInputBlack,
    SetInputGamma,
    SetInputWhite,
    SetOutputBlack,
    SetOutputWhite,
    SetLevelsSample, // 0 none, 1 black, 2 gray, 3 white
    AutoLevels,      // 0 contrast, 1 color, 2 neutral
    SetRadius,
    SetAngle,
    SetDistance,
    SetAmount,
    SetGaussian,
    SetMonochromatic,
    SetDistortion,
    SetExposure,
    SetOffset,
    SetGamma,
    SetGrainAmount,
    SetGrainSize,
    SetGrainRoughness,
    SetReversed,
    OpenShadows,
    OpenHighlights,
    SetRange, // 0 master, 1 reds … 6 magentas
    SetHue,
    SetSaturation,
    SetLightness,
    SetColorize,
    SetInvertRange,
    SetHueSample, // 0 none, 1 sample, 2 add, 3 remove
    SetTargeting,
};

struct AdjustInspectorState
{
    AdjustPanelKind panel = AdjustPanelKind::None;
    std::wstring title;
    FilterKind filter = FilterKind::GaussianBlur;
    FilterSettings settings;
    LevelsSettings levels;
    LevelsHistogram histogram{};
    bool histogramReady = false;
    int levelsSample = 0;
    HueSaturationSettings hsv;
    int hueSample = 0;
    bool targeting = false;
    bool preview = true;
    bool limitedToSelection = false;
    bool editsAdjustment = false;
    bool previewFailed = false; // Content-Aware Fill found nothing to copy from: OK waits, the note says so
};

// The size cards (C16, Specs/UI/UI_ApplicationChrome.md "Size cards"): New Canvas, Canvas Size and Image Size,
// mirrored from the Application's draft through SetSizeCard; the controls report changes as actions with one value.
enum class SizeCardKind : std::uint8_t
{
    None,
    NewCanvas,
    CanvasSize,
    ImageSize,
};

enum class SizeAction : std::uint8_t
{
    SetWidth, // the field's number in the chosen unit
    SetHeight,
    SetUnit, // 0 pixels, 1 percent, 2 inches, 3 centimeters
    SetRelative,
    SetLocked,
    SetAnchor,    // 0…8, row-major from the top left
    SetExtension, // 0 transparent, 1 foreground, 2 background, 3 black, 4 white, 5 custom (opens the picker)
    SetResolution,
    SetResample,
    SetSampling, // 0 nearest, 1 smooth, 2 high
    OpenProject, // New Canvas: Open Project…
    ImportImage, // New Canvas: Import Image…
    Cancel,
    Apply,
};

struct SizeCardState
{
    SizeCardKind kind = SizeCardKind::None;
    int originalWidth = 0;
    int originalHeight = 0;
    double width = 0; // the numbers the fields show, in the unit
    double height = 0;
    int unit = 0;
    bool relative = false;
    bool locked = false;
    int anchor = 4;
    int extension = 0;
    uint32_t extensionArgb = 0xFFFFFFFFu;
    double resolution = 72;
    bool resample = true;
    int sampling = 2;
    bool valid = true;
    int resultWidth = 0; // the rounded pixels the card announces
    int resultHeight = 0;
};

// The selection tools' header state, mirrored from the session (Specs/Document/Document_Tools.md).
struct SelectionInspectorState
{
    bool visible = false;
    bool enabled = false; // a document is open
    EditorTool tool = EditorTool::Marquee;
    int marqueeKind = 0; // 0 rectangle, 1 ellipse
    int lassoKind = 0;   // 0 freehand, 1 polygonal
    int mode = 0;        // displayed mode: 0 new, 1 add, 2 subtract
    bool antialiasVisible = true;
    bool antialiased = true;
    int tolerance = 32;
    int sampleSize = 0; // 0 point, 1 three by three, 2 five by five
    bool sampleAllLayers = false;
    bool contiguous = true;
    bool canModify = false;
    int expandAmount = 1;
    int contractAmount = 1;
    bool hasSelection = false;
    bool isEmpty = false;
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
// EmbeddedHost that borrows the application device. Every visible string comes from the STRINGTABLE. The command
// ids (`kCommand…`) live on CommandIds (RedPrism/CommandCatalog.h) and read as `ChromeHost::kCommand…`.
class ChromeHost final : public CommandIds
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
    static constexpr float kWellBlockDip = 48.0f;
    static constexpr float kSwatchDip = 22.0f;
    static constexpr float kPaletteHotDip = 14.0f;
    static constexpr float kPickerMarginDip = 8.0f;

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
    // A double-click on a layer row (C16): the host renames it.
    void SetLayerInvokeHandler(std::function<void(Guid)> handler);
    // A row drag. `parent` is the target's parent for Before/After, and is ignored for Inside (the target is the
    // folder). The document move is `PlaceLayer`.
    void SetLayerReorderHandler(
        std::function<void(Guid source, Guid target, std::optional<Guid> parent, DxUi::TreeDropPlace place)> handler);
    // The Rename Layer card (C16): opened on a name; the handler gets the committed name, nullopt on cancel.
    void SetRenameHandler(std::function<void(std::optional<std::wstring>)> handler);
    void OpenRenameCard(const std::wstring& name) noexcept;
    void CloseRenameCard(bool commit) noexcept;
    [[nodiscard]] bool RenameCardOpen() const noexcept;
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
    // The selection tools' header: kinds, mode, wand settings, Anti-alias, Expand / Contract and Deselect.
    void SetSelectionHandler(std::function<void(SelectionAction, double)> handler);
    void SetSelectionInspector(const SelectionInspectorState& state) noexcept;
    // The selection outline (marching ants) and the lasso draft, in canvas-hole pixels.
    void SetSelectionOutline(std::vector<std::vector<D2D1_POINT_2F>> loops) noexcept;
    void SetSelectionAntPhase(float phase) noexcept;
    [[nodiscard]] float SelectionAntPhase() const noexcept;
    void SetLassoDraft(std::optional<CanvasOverlay::Draft> draft) noexcept;
    // The Brush and Eraser header: mode, size, hardness, opacity, mask paint and the color swatch.
    void SetBrushHandler(std::function<void(BrushAction, double)> handler);
    void SetBrushInspector(const BrushInspectorState& state) noexcept;
    // The tip circle over the canvas, in canvas-hole pixels (nullopt hides it).
    void SetBrushCursor(std::optional<CanvasOverlay::BrushCursor> cursor) noexcept;
    // The Gradient and Shape headers (C11), and the tool overlays in canvas-hole pixels (nullopt hides each).
    void SetGradientHandler(std::function<void(GradientAction, double)> handler);
    void SetGradientInspector(const GradientInspectorState& state) noexcept;
    void SetShapeHandler(std::function<void(ShapeAction, double)> handler);
    void SetShapeInspector(const ShapeInspectorState& state) noexcept;
    // The Crop header (C16) and the shade outside the crop frame (canvas-hole pixels; nullopt clears it).
    void SetCropHandler(std::function<void(CropAction, double)> handler);
    void SetCropInspector(const CropInspectorState& state) noexcept;
    void SetCropShade(std::optional<D2D1_RECT_F> framePixels) noexcept;
    // The adjustment panel (C12): actions with a value, the curve editor's points, a spectrum band handle, and the
    // Gradient Map's color picks (the picker's phase, the color and which end).
    void SetAdjustHandler(std::function<void(AdjustAction, double)> handler);
    void SetCurveHandler(std::function<void(std::vector<CurvePoint>)> handler);
    void SetBandHandler(std::function<void(int, double)> handler);
    void SetAdjustColorHandler(std::function<void(ChromeEditPhase, uint32_t, bool)> handler);
    void SetAdjustInspector(const AdjustInspectorState& state) noexcept;
    [[nodiscard]] bool AdjustPanelOpen() const noexcept;
    [[nodiscard]] D2D1_RECT_F AdjustPanelBoundsDip() const noexcept;
    // The color picker on a Gradient Map end; its changes go to the adjust color handler, never the palette.
    void OpenAdjustColorPicker(uint32_t argb, bool highlights) noexcept;
    // The size cards (C16): actions with a value, the custom extension color's picks, and the mirrored draft.
    void SetSizeHandler(std::function<void(SizeAction, double)> handler);
    void SetSizeColorHandler(std::function<void(ChromeEditPhase, uint32_t)> handler);
    void SetSizeCard(const SizeCardState& state) noexcept;
    [[nodiscard]] bool SizeCardOpen() const noexcept;
    // The color picker on the Canvas Size card's custom extension color; changes go to the size color handler.
    void OpenSizeColorPicker(uint32_t argb) noexcept;
    void SetGradientLine(std::optional<std::pair<D2D1_POINT_2F, D2D1_POINT_2F>> line) noexcept;
    void SetShapeDraft(std::optional<CanvasOverlay::ShapeDraftOverlay> draft) noexcept;
    void SetSampleRing(std::optional<CanvasOverlay::SampleRing> ring) noexcept;
    void SetCloneSourceMark(std::optional<D2D1_POINT_2F> point) noexcept;
    [[nodiscard]] std::optional<D2D1_POINT_2F> CloneSourceMarkDip() const noexcept;
    [[nodiscard]] float LayoutDpi() const noexcept
    {
        return _dpi;
    }
    // The eyedropper loads a sampled color into the open picker (its swatch previews it; OK commits it).
    void SetPickerColor(uint32_t argb) noexcept;
    [[nodiscard]] uint32_t PickerColor() const noexcept;
    // A checkable View menu item (Sample Ring).
    void SetCommandChecked(int command, bool checked) noexcept;
    void SetDocumentTitle(std::wstring title, bool dirty);
    void SetStatus(std::wstring zoom, std::wstring size, std::wstring tool);
    void SetUndoRedoNames(const std::wstring& undoName, const std::wstring& redoName, bool canUndo, bool canRedo);
    // Captions that follow the target, as the legacy menus did: Invert / Invert Mask, Delete Layer / Delete Layer
    // Mask, Free Transform / Transform Selection.
    // `clipped`: the active layer clips to a base, so the Layer menu offers Release Clipping Mask.
    // `merge`: what Ctrl+E would do (0 Merge Down, 1 Merge Group, 2 Merge Layers), for the Layer menu's caption.
    void SetEditTargets(bool masksTargeted, bool transformsSelection, bool clipped, int merge);
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
    // Self-test hook: shows the tooltip of the hovered control at once, as the hover delay would; true when one shows.
    [[nodiscard]] bool ForceTooltipForTest() noexcept;
    // Self-test hook: DxUi's focus on the layer opacity stepper, as a click on it would leave it.
    void FocusLayerOpacityForTest() noexcept;
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
    // Whether a client-area point (pixels) belongs to the canvas: inside the hole and not on a floating card.
    [[nodiscard]] bool IsCanvasPoint(float xPixels, float yPixels) const noexcept;
    // Sets the cursor the chrome wants at a client-area point (pixels) off the canvas: the splitter's resize
    // arrows, the arrow elsewhere. False before the chrome is attached.
    bool ApplyChromeCursor(float xPixels, float yPixels) noexcept;
    // The screen point a native flyout for menu `index` hangs from: the item's bottom-left, translated from the
    // origin DxUi assumed for the embedded view to the owner window's client origin.
    [[nodiscard]] POINT MenuAnchorScreen(size_t index, POINT callbackPoint) const noexcept;
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
    // Inside a DxUi dispatch (pointer, key or character): a callback running there must not destroy the chrome.
    [[nodiscard]] bool InDispatch() const noexcept
    {
        return _inDispatch;
    }
    void SetCanvasFocused(bool focused) noexcept
    {
        _canvasFocused = focused;
        _accessibilityPlacementDirty = true;
    }
    // The chrome palette (Specs/UI/UI_ApplicationChrome.md, Theme): dark, light or the system high-contrast colors.
    void SetTheme(const DxUi::ThemePalette& palette) noexcept;
    [[nodiscard]] bool ThemeIsDark() const noexcept
    {
        return _palette.dark;
    }
    [[nodiscard]] bool ThemeIsHighContrast() const noexcept
    {
        return _palette.highContrast;
    }
    // UI Automation (ChromeHostAccessibility.cpp): the window's root with the DxUi tree beneath it.
    void AttachAccessibility() noexcept;
    void DetachAccessibility() noexcept;
    [[nodiscard]] LRESULT HandleGetObject(WPARAM wParam, LPARAM lParam) noexcept;
    // Marks the placement (client rectangle, keyboard focus in the chrome) for the next Prepare and asks for a
    // frame: DxUi is never re-entered from a pointer, key or focus path.
    void UpdateAccessibilityPlacement() noexcept;
    [[nodiscard]] IRawElementProviderFragmentRoot* AccessibilityRoot() const noexcept; // not add-referenced
    [[nodiscard]] HRESULT AccessibilityStatus() const noexcept // the last attach attempt (S_FALSE: none yet)
    {
        return _accessibilityStatus;
    }
    // Self-test gate: interactive controls without an accessible name, described by kind and parent.
    [[nodiscard]] std::vector<std::wstring> UnnamedInteractiveControls() const;
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
    [[nodiscard]] int VisibleToolButtonCount() const noexcept;
    [[nodiscard]] bool MoreToolsOpen() const noexcept;
    void CloseMoreTools() noexcept;
    [[nodiscard]] D2D1_RECT_F MoreToolsBoundsDip() const noexcept;
    [[nodiscard]] D2D1_RECT_F OverflowToolBoundsDip(EditorTool tool) const noexcept;
    // Separator bar of the layers splitter, palette wells, and the picker card, in DIP, for the same purpose.
    [[nodiscard]] D2D1_RECT_F SplitterBoundsDip() const noexcept;
    [[nodiscard]] D2D1_RECT_F PaletteWellBoundsDip(bool background) const noexcept;
    [[nodiscard]] D2D1_RECT_F PaletteHotBoundsDip(bool reset) const noexcept;
    [[nodiscard]] D2D1_RECT_F ColorPickerBoundsDip() const noexcept;
    [[nodiscard]] D2D1_RECT_F LayerRowBoundsDip(std::size_t index) const noexcept;
    // 0 none, 1 layers tree, 2 splitter, 3 shortcuts, 4 notice, 5 adjust, 6 picker, 7 overlay, 8 other.
    [[nodiscard]] int HitKindForTest(float xPixels, float yPixels) noexcept;
    // The open filter panel's option row (radius, curve, ramp, hint) is visible inside the card.
    [[nodiscard]] bool FilterOptionsVisible() const noexcept;
    void ShowNotice(std::wstring text) noexcept;
    void CloseNotice() noexcept;
    [[nodiscard]] bool NoticeOpen() const noexcept;
    [[nodiscard]] D2D1_RECT_F NoticeBoundsDip() const noexcept;
    // Help › Command List: every catalog command with its effective chord, a search field that filters by name.
    // Enter in the search field runs the selected row, or the first row when none is selected; Enter, Space or a
    // double-click on the list runs its row. Closing gives the keyboard focus back to the card underneath (the
    // Rename card's field, the color picker) or, with no such card, to the canvas.
    void OpenShortcuts() noexcept;
    void CloseShortcuts() noexcept;
    [[nodiscard]] bool ShortcutsOpen() const noexcept;
    // The effective binding table the chrome shows (borrowed; null shows the catalog defaults). Menus, the Command
    // List, tooltips and the palette hints follow it at once.
    void SetCommandBindings(const CommandBindings* bindings) noexcept;
    // Retitle menu accelerators, tooltips and the open Command List from the table and the active keyboard layout.
    void RefreshDisplayedChords() noexcept;
    // The effective chord of `command` as the chrome shows it (STRINGTABLE modifier names, the layout's key names);
    // empty when the command has no chord.
    [[nodiscard]] std::wstring CommandChordText(int command) const;
    [[nodiscard]] static std::wstring ChordDisplayText(const KeyChord& chord);
    [[nodiscard]] std::wstring ShortcutChordAt(std::size_t index) const;
    [[nodiscard]] std::wstring MenuItemCaption(int command) const;
    // Whether a chrome text input holds the keyboard focus (never while the canvas has it).
    [[nodiscard]] bool TextInputFocused() const noexcept;
    // Keyboard access to the menu bar (a tapped Alt or F10, SC_KEYMENU): the bar takes the focus with its first
    // item highlighted; arrows move, Enter, Space or Down open, a letter opens that menu, Escape leaves and the
    // focus returns where it was.
    void ToggleMenuKeyboard() noexcept;
    void LeaveMenuKeyboard() noexcept;
    [[nodiscard]] bool MenuKeyboardActive() const noexcept;
    // Alt+letter, or a letter while the menu bar has the keyboard: opens the menu with that mnemonic.
    bool OpenMenuByMnemonic(wchar_t mnemonic) noexcept;
    // Self-test hooks: menus are recorded instead of shown (a native flyout runs a modal loop), and the Command List
    // and focus are read and steered without the pointer.
    void SetMenusRecordOnlyForTest(bool recordOnly) noexcept;
    [[nodiscard]] std::optional<std::size_t> TakeRecordedMenuForTest() noexcept;
    [[nodiscard]] std::size_t ShortcutRowCountForTest() const noexcept;
    [[nodiscard]] int ShortcutCommandAtForTest(std::size_t index) const noexcept;
    [[nodiscard]] int SelectedShortcutForTest() const noexcept;
    void SelectShortcutForTest(int command) noexcept;
    void FocusShortcutListForTest() noexcept;
    [[nodiscard]] std::wstring ShortcutSearchTextForTest() const;
    [[nodiscard]] std::wstring FocusedControlNameForTest() const;
    [[nodiscard]] std::wstring ToolTooltipForTest(EditorTool tool) const;
    [[nodiscard]] std::wstring PaletteSwapTooltipForTest() const;

    // The catalog rows (RedPrism/CommandCatalog.h) under their former names.
    using CommandShortcutDefault = CommandCatalogEntry;
    [[nodiscard]] static std::span<const CommandShortcutDefault> CommandShortcuts() noexcept
    {
        return CommandCatalog();
    }

  private:
    class LayerTreeModel final : public DxUi::IDxTreeModel, public DxUi::IDxTreeDelegate
    {
      public:
        std::vector<HierarchyEntry> rows;
        // Each row's display text (the name, or the "(hidden)" suffixed form), built once by SetRows so
        // GetVisibleItem's per-paint call is an assignment into the caller's reused buffer, never a
        // FormatStringResource call.
        std::vector<std::wstring> displayText;
        std::optional<Guid> active;
        std::wstring hiddenPattern;
        std::function<void(Guid)>* onLayer = nullptr;
        std::function<void(Guid, bool)>* onExpand = nullptr;
        std::function<void(Guid)>* onInvoke = nullptr;
        std::function<void(Guid, Guid, std::optional<Guid>, DxUi::TreeDropPlace)>* onReorder = nullptr;
        bool useFluentIcons = true;
        // Stores the hierarchy and rebuilds displayText once; SetLayers calls this instead of assigning rows
        // directly so a settled layers panel does not re-format every row's name on every repaint.
        void SetRows(std::vector<HierarchyEntry> newRows);
        [[nodiscard]] size_t GetVisibleItemCount() const noexcept override;
        void GetVisibleItem(size_t visibleIndex, DxUi::TreeItemData& outItem) const override;
        void OnTreeSelectionChanged(uint64_t itemId) override;
        void OnTreeToggleExpanded(uint64_t itemId, bool expanded) override;
        void OnTreeItemInvoked(uint64_t itemId) override;
        void OnTreeReorder(const DxUi::TreeDrop& drop) override;
    };

    // The Command List's rows. An item's id is its command id, so a selection survives filtering only while its
    // command is still listed and never moves to another command.
    class ShortcutListModel final : public DxUi::IDxTreeModel, public DxUi::IDxTreeDelegate
    {
      public:
        struct Row
        {
            std::wstring name;
            std::wstring chord;
            int command = 0;
        };
        std::vector<Row> all;
        std::vector<Row> rows;
        std::wstring query;
        std::function<void(int)> onInvoke;
        [[nodiscard]] std::size_t GetVisibleItemCount() const noexcept override;
        void GetVisibleItem(std::size_t visibleIndex, DxUi::TreeItemData& outItem) const override;
        [[nodiscard]] std::optional<std::size_t> FindVisibleItemById(uint64_t itemId) const noexcept override;
        void OnTreeItemInvoked(uint64_t itemId) override;
    };

    HRESULT BindDevice(ID3D11Device* device) noexcept;
    HRESULT Build() noexcept;
    void Arrange() noexcept;
    void ArrangePicker() noexcept;
    void ArrangeNotice() noexcept;
    void ArrangeShortcuts() noexcept;
    void ApplyShortcutFilter() noexcept;
    void LaunchShortcut(int command) noexcept;
    // Every catalog command with its plain name and effective chord, filtered by the query.
    void BuildShortcutRows() noexcept;
    // Enter in the search field: the selected row, else the first row; nothing when no row is listed.
    void RunShortcutFromSearch() noexcept;
    // Up or Down in the search field moves the list's selection, the focus staying in the field.
    bool StepShortcutSelection(int delta) noexcept;
    // The table the chrome shows: the application's, or the catalog defaults before it arrives.
    [[nodiscard]] const CommandBindings& Bindings() const noexcept;
    // A menu row's caption: the STRINGTABLE caption without its accelerator column, a tab and the effective chord.
    [[nodiscard]] std::wstring CommandMenuCaption(UINT stringId, int command) const;
    [[nodiscard]] std::wstring CommandMenuCaption(std::wstring caption, int command) const;
    // Writes a menu row's caption only when it changed (the dynamic captions are set on every sync).
    static void SetMenuCaption(HMENU menu, int command, const std::wstring& caption, UINT stateFlags) noexcept;
    // Re-suffixes every menu row with its effective chord, keeping its current name, check and gray state.
    void RefreshMenuChords() noexcept;
    // The tooltips that name keys: the rail tools, the palette hot zones and the brush header.
    void RefreshShortcutHints() noexcept;
    // `text` with the chords of `commands` that have one, as "text (A, B)"; `text` alone when none has.
    [[nodiscard]] std::wstring WithChordHint(std::wstring text, std::initializer_list<int> commands) const;
    void BuildTransformHeader(DxUi::Panel& root);
    void ArrangeTransformHeader(float left, float top, float right, float bottom) noexcept;
    void BuildSelectionHeader(DxUi::Panel& root);
    void ArrangeSelectionHeader(float left, float top, float right, float bottom) noexcept;
    void BuildBrushHeader(DxUi::Panel& root);
    void ArrangeBrushHeader(float left, float top, float right, float bottom) noexcept;
    void BuildGradientHeader(DxUi::Panel& root);
    void ArrangeGradientHeader(float left, float top, float right, float bottom) noexcept;
    void BuildShapeHeader(DxUi::Panel& root);
    void ArrangeShapeHeader(float left, float top, float right, float bottom) noexcept;
    // The plain title header stands in for tools without a header of their own.
    [[nodiscard]] static bool GenericHeaderWanted(EditorTool tool) noexcept;
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
    DxUi::ThemePalette _palette = RedPrism::MakeRedPrismThemePalette(true);
    wil::com_ptr_nothrow<WindowAccessibilityRoot> _accessibilityRoot;
    std::shared_ptr<ChromeAccessibilitySite> _accessibilitySite;
    HRESULT _accessibilityStatus = S_FALSE;
    // Layout writes only when something changes: DxUi reacts to every SetBounds/SetVisible, and the arrange paths
    // run every frame, including between a press and its release.
    static void PlaceControl(DxUi::Control* control, const D2D1_RECT_F& bounds) noexcept;
    static void ShowControl(DxUi::Control* control, bool visible) noexcept;
    // A pressed look only when it changes: a button's transition restarted every sync cancels presses held across
    // frames.
    static void SetPressedVisualIfChanged(DxUi::Button* button, bool& current, bool pressed) noexcept;
    void ReleaseFocusForPress(const DxUi::PointerEvent& event) noexcept;
    void ChooseTool(EditorTool tool) noexcept;
    void OpenMoreTools() noexcept;
    void ArrangeMoreTools() noexcept;
    [[nodiscard]] int RailSlotsThatFit(float wellTop) const noexcept;
    [[nodiscard]] DxUi::EmbeddedAccessibilityPlacement AccessibilityPlacement() const noexcept;
    void FlushAccessibilityPlacement() noexcept;
    bool _accessibilityPlacementDirty = false;
    bool _inDispatch = false;     // inside _view.Dispatch*: no accessibility flush from a re-entered Prepare
    bool _pointerPressed = false; // a chrome press in progress: DxUi's accessibility calls would cancel it
    std::array<unique_hmenu, static_cast<std::size_t>(MenuIndex::Count)> _menus;
    DxUi::MenuBar* _menu = nullptr;
    DxUi::TabControl* _tabs = nullptr;
    DxUi::Toolbar* _rail = nullptr;
    DxUi::Button* _moreTools = nullptr;
    DxUi::CardPanel* _moreCard = nullptr;
    std::array<DxUi::Button*, kEditorToolCount> _overflowButtons{};
    int _moreToolStart = static_cast<int>(kEditorToolCount);
    bool _moreToolsOpen = false;
    DxUi::Label* _header = nullptr;
    DxUi::Tree* _layers = nullptr;
    DxUi::StatusStrip* _status = nullptr;
    DxUi::Label* _layersTitle = nullptr;
    DxUi::Splitter* _splitter = nullptr;
    PaletteControl* _railPalette = nullptr;
    DxUi::NumericStepper* _opacity = nullptr;
    DxUi::CardPanel* _pickerCard = nullptr;
    DxUi::ColorPicker* _picker = nullptr;
    DxUi::CardPanel* _noticeCard = nullptr;
    DxUi::Label* _noticeLabel = nullptr;
    DxUi::Button* _noticeClose = nullptr;
    DxUi::CardPanel* _shortcutsCard = nullptr;
    DxUi::Label* _shortcutsTitle = nullptr;
    DxUi::TextField* _shortcutsSearch = nullptr;
    DxUi::Tree* _shortcutsTree = nullptr;
    DxUi::Button* _shortcutsClose = nullptr;
    ShortcutListModel _shortcutModel;
    const CommandBindings* _bindings = nullptr;
    std::vector<std::wstring> _chordTexts; // the effective chord text of each catalog row, in catalog order
    bool _menuKeyboard = false;            // the menu bar took the keyboard (Alt, F10 or a mnemonic)
    DxUi::Control* _menuReturnFocus = nullptr;
    bool _menuReturnCanvas = false;
    bool _menusRecordOnly = false;
    std::optional<std::size_t> _recordedMenu;
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
    DxUi::Panel* _selectionHeader = nullptr;
    DxUi::Label* _selectionTitle = nullptr;
    DxUi::ComboBox* _marqueeShape = nullptr;
    DxUi::ComboBox* _lassoShape = nullptr;
    DxUi::ComboBox* _selectionMode = nullptr;
    DxUi::NumericStepper* _wandTolerance = nullptr;
    DxUi::ComboBox* _wandSampleSize = nullptr;
    DxUi::ComboBox* _wandSample = nullptr;
    DxUi::Checkbox* _wandContiguous = nullptr;
    DxUi::Checkbox* _selectionAntialias = nullptr;
    DxUi::Button* _expandButton = nullptr;
    DxUi::NumericStepper* _expandAmount = nullptr;
    DxUi::Button* _contractButton = nullptr;
    DxUi::NumericStepper* _contractAmount = nullptr;
    DxUi::Label* _selectionEmpty = nullptr;
    DxUi::Button* _deselectButton = nullptr;
    SelectionInspectorState _selectionInspector;
    bool _syncingSelection = false;
    DxUi::Panel* _brushHeader = nullptr;
    DxUi::Label* _brushTitle = nullptr;
    DxUi::ComboBox* _brushMode = nullptr;
    DxUi::NumericStepper* _brushSize = nullptr;
    DxUi::NumericStepper* _brushHardness = nullptr;
    DxUi::NumericStepper* _brushOpacity = nullptr;
    DxUi::ComboBox* _brushMaskPaint = nullptr;
    DxUi::Label* _brushColorLabel = nullptr;
    DxUi::ColorSwatch* _brushColor = nullptr;
    DxUi::Label* _brushMaskNote = nullptr;
    DxUi::ComboBox* _brushHealMode = nullptr;
    DxUi::Checkbox* _brushCloneAligned = nullptr;
    DxUi::ComboBox* _brushCloneSample = nullptr;
    DxUi::ComboBox* _brushSmearMode = nullptr;
    DxUi::Label* _brushSourceNote = nullptr;
    DxUi::Panel* _gradientHeader = nullptr;
    DxUi::Label* _gradientTitle = nullptr;
    DxUi::ComboBox* _gradientShape = nullptr;
    DxUi::ColorSwatch* _gradientStart = nullptr;
    DxUi::ColorSwatch* _gradientEnd = nullptr;
    DxUi::ComboBox* _gradientStyle = nullptr;
    DxUi::Checkbox* _gradientReverse = nullptr;
    DxUi::NumericStepper* _gradientOpacity = nullptr;
    DxUi::Label* _gradientMaskNote = nullptr;
    DxUi::Button* _gradientCancel = nullptr;
    DxUi::Button* _gradientApply = nullptr;
    GradientInspectorState _gradientInspector;
    bool _syncingGradient = false;
    DxUi::Panel* _shapeHeader = nullptr;
    // ChromeHostCrop.cpp: the Crop header.
    void BuildCropHeader(DxUi::Panel& root);
    void ArrangeCropHeader(float left, float top, float right, float bottom) noexcept;
    DxUi::Panel* _cropHeader = nullptr;
    DxUi::Label* _cropTitle = nullptr;
    DxUi::Label* _cropRatioLabel = nullptr;
    DxUi::ComboBox* _cropRatio = nullptr;
    DxUi::Label* _cropSize = nullptr;
    DxUi::Button* _cropCancel = nullptr;
    DxUi::Button* _cropApply = nullptr;
    CropInspectorState _cropInspector;
    bool _syncingCrop = false;
    std::function<void(CropAction, double)> _onCrop;
    DxUi::Label* _shapeTitle = nullptr;
    DxUi::ComboBox* _shapeKind = nullptr;
    DxUi::NumericStepper* _shapeRadius = nullptr;
    DxUi::Label* _shapeFillLabel = nullptr;
    DxUi::ColorSwatch* _shapeFill = nullptr;
    ShapeInspectorState _shapeInspector;
    bool _syncingShape = false;
    BrushInspectorState _brushInspector;
    bool _syncingBrush = false;
    std::array<DxUi::Button*, kEditorToolCount> _toolButtons{}; // icon buttons; the active tool's is latched
    bool _toolVisualsDirty = false;                             // a latch change waits for the pointer to come up
    LayerTreeModel _treeModel;
    std::function<void(int)> _onCommand;
    std::function<void(EditorTool)> _onTool;
    std::function<void(Guid)> _onLayer;
    std::function<void(Guid, bool)> _onExpand;
    std::function<void(Guid)> _onLayerInvoke;
    std::function<void(Guid, Guid, std::optional<Guid>, DxUi::TreeDropPlace)> _onReorder;
    // ChromeHostRename.cpp: the Rename Layer card.
    void BuildRenameCard(DxUi::Panel& root);
    void ArrangeRenameCard(float right, float top) noexcept;
    DxUi::CardPanel* _renameCard = nullptr;
    DxUi::Label* _renameTitle = nullptr;
    DxUi::TextField* _renameField = nullptr;
    DxUi::Button* _renameCancel = nullptr;
    DxUi::Button* _renameOk = nullptr;
    std::function<void(std::optional<std::wstring>)> _onRename;
    std::function<void(uint32_t, uint32_t)> _onPalette;
    std::function<void(double, ChromeEditPhase)> _onOpacity;
    std::function<void()> _onLayout;
    std::function<void(TransformAction, double)> _onTransform;
    std::function<void(SelectionAction, double)> _onSelection;
    std::function<void(BrushAction, double)> _onBrush;
    std::function<void(GradientAction, double)> _onGradient;
    std::function<void(ShapeAction, double)> _onShape;
    // The adjustment panel (ChromeHostAdjust.cpp).
    enum class AdjustRowId : std::uint8_t
    {
        Radius,
        Angle,
        Distance,
        Amount,
        Distortion,
        Exposure,
        Offset,
        Gamma,
        GrainAmount,
        GrainSize,
        GrainRoughness,
        Hue,
        Saturation,
        Lightness,
        Count,
    };
    struct AdjustRow
    {
        DxUi::Label* label = nullptr;
        DxUi::Slider* slider = nullptr;
        DxUi::NumericStepper* stepper = nullptr;
        AdjustAction action = AdjustAction::SetRadius;
        double minimum = 0;
        double maximum = 1;
        bool logarithmic = false;
        int decimals = 0;
    };
    void BuildAdjustPanel(DxUi::Panel& root);
    void ArrangeAdjustPanel(float right, float top, float bottom) noexcept;
    DxUi::CardPanel* _adjustCard = nullptr;
    DxUi::Label* _adjustTitle = nullptr;
    DxUi::ComboBox* _adjustChannel = nullptr;
    CurveEditor* _curveEditor = nullptr;
    DxUi::Label* _curveCaption = nullptr;
    DxUi::Button* _curveRemove = nullptr;
    DxUi::Button* _curveReset = nullptr;
    LevelsBar* _levelsInput = nullptr;
    LevelsBar* _levelsOutput = nullptr;
    DxUi::NumericStepper* _levelsBlack = nullptr;
    DxUi::NumericStepper* _levelsGamma = nullptr;
    DxUi::NumericStepper* _levelsWhite = nullptr;
    DxUi::NumericStepper* _levelsOutBlack = nullptr;
    DxUi::NumericStepper* _levelsOutWhite = nullptr;
    std::array<DxUi::Button*, 3> _levelsSample{};
    std::array<bool, 3> _levelsSamplePressed{};
    std::array<DxUi::Button*, 3> _levelsAuto{};
    DxUi::ComboBox* _hueRange = nullptr;
    std::array<DxUi::Button*, 3> _hueSample{};
    std::array<bool, 3> _hueSamplePressed{};
    DxUi::Button* _hueTargeting = nullptr;
    bool _hueTargetingPressed = false;
    SpectrumEditor* _spectrum = nullptr;
    DxUi::Checkbox* _hueInvert = nullptr;
    DxUi::Checkbox* _hueColorize = nullptr;
    GradientRamp* _gradientRamp = nullptr;
    DxUi::ColorSwatch* _mapShadows = nullptr;
    DxUi::Label* _mapShadowsLabel = nullptr;
    DxUi::ColorSwatch* _mapHighlights = nullptr;
    DxUi::Label* _mapHighlightsLabel = nullptr;
    DxUi::Checkbox* _mapReverse = nullptr;
    DxUi::ComboBox* _noiseDistribution = nullptr;
    DxUi::Checkbox* _noiseMono = nullptr;
    DxUi::Label* _lensHint = nullptr;
    std::array<AdjustRow, static_cast<std::size_t>(AdjustRowId::Count)> _adjustRows{};
    DxUi::Checkbox* _adjustPreview = nullptr;
    DxUi::Label* _fillHint = nullptr;
    DxUi::Label* _fillNoSource = nullptr;
    DxUi::Label* _adjustNote = nullptr;
    DxUi::Button* _adjustCancel = nullptr;
    DxUi::Button* _adjustApply = nullptr;
    AdjustInspectorState _adjustInspector;
    bool _syncingAdjust = false;
    std::function<void(AdjustAction, double)> _onAdjust;
    std::function<void(std::vector<CurvePoint>)> _onCurve;
    std::function<void(int, double)> _onBand;
    std::function<void(ChromeEditPhase, uint32_t, bool)> _onAdjustColor;
    bool _pickerForAdjust = false;
    bool _pickerHighlights = false;
    // ChromeHostSize.cpp: the size cards.
    void BuildSizeCard(DxUi::Panel& root);
    void ArrangeSizeCard(float right, float top, float bottom) noexcept;
    DxUi::CardPanel* _sizeCard = nullptr;
    DxUi::Label* _sizeTitle = nullptr;
    DxUi::Label* _sizeCurrent = nullptr;
    DxUi::Label* _sizeBytes = nullptr;
    DxUi::ComboBox* _sizeUnit = nullptr;
    DxUi::NumericStepper* _sizeWidth = nullptr;
    DxUi::NumericStepper* _sizeHeight = nullptr;
    DxUi::Checkbox* _sizeRelative = nullptr;
    DxUi::Checkbox* _sizeLocked = nullptr;
    DxUi::Label* _sizeResult = nullptr;
    DxUi::Label* _sizeAnchorLabel = nullptr;
    std::array<DxUi::Button*, 9> _sizeAnchors{};
    std::array<bool, 9> _sizeAnchorPressed{};
    DxUi::Label* _sizeAnchorName = nullptr;
    DxUi::Label* _sizeAnchorHint = nullptr;
    DxUi::ComboBox* _sizeExtension = nullptr;
    DxUi::ColorSwatch* _sizeExtensionSwatch = nullptr;
    DxUi::NumericStepper* _sizeResolution = nullptr;
    DxUi::Label* _sizePpi = nullptr;
    DxUi::Checkbox* _sizeResample = nullptr;
    DxUi::ComboBox* _sizeSampling = nullptr;
    DxUi::Label* _sizeNote = nullptr;
    DxUi::Button* _sizeOpen = nullptr;
    DxUi::Button* _sizeImport = nullptr;
    DxUi::Button* _sizeCancel = nullptr;
    DxUi::Button* _sizeApply = nullptr;
    SizeCardState _sizeState;
    bool _syncingSize = false;
    bool _pickerForSize = false;
    std::function<void(SizeAction, double)> _onSize;
    std::function<void(ChromeEditPhase, uint32_t)> _onSizeColor;
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
