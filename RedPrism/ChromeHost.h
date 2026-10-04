#pragma once

#include "AccessibilityRoot.h"
#include "AdjustControls.h"
#include "CanvasOverlay.h"
#include "ChromeEvents.h"
#include "CommandBindings.h"
#include "Document.h"
#include "EditorTool.h"
#include "Resources.h"
#include "TransformOverlay.h"

#include <array>
#include <cstdint>
#include <d3d11.h>
#include <functional>
#include <initializer_list>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <type_traits>
#include <utility>
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
    [[nodiscard]] bool operator==(const BrushInspectorState&) const noexcept = default;
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
    [[nodiscard]] bool operator==(const GradientInspectorState&) const noexcept = default;
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
    [[nodiscard]] bool operator==(const ShapeInspectorState&) const noexcept = default;
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
    [[nodiscard]] bool operator==(const CropInspectorState&) const noexcept = default;
};

// The adjustment panel (C12, Specs/UI/UI_ApplicationChrome.md): which editor it shows and every value the session
// holds for it, mirrored through SetAdjustInspector; its controls report changes as actions with one value.
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
    std::wstring title;
    // nullopt: the panel is closed. Otherwise Levels and HueSaturation draw their own editors; every other kind
    // shares the generic filter layout (ChromeHostAdjust.cpp).
    std::optional<FilterKind> filter;
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
    bool busy = false;
    [[nodiscard]] bool operator==(const AdjustInspectorState&) const noexcept = default;
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
    [[nodiscard]] bool operator==(const SizeCardState&) const noexcept = default;
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
    [[nodiscard]] bool operator==(const SelectionInspectorState&) const noexcept = default;
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
    [[nodiscard]] bool operator==(const TransformInspectorState&) const noexcept = default;
};

// Editor chrome on the one HWND: MenuBar, document tabs, tool rail with the palette wells, tool header, layers
// splitter, layers panel with the opacity stepper, status strip, and the color picker card. Owns one DxUi
// EmbeddedHost that borrows the application device. Every visible string comes from the STRINGTABLE. The command
// ids (`kCommand…`) live on CommandIds (RedPrism/CommandCatalog.h) and read as `ChromeHost::kCommand…`. What the user
// does in it goes to the ChromeEvents it is constructed with.
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
    static constexpr float kStatusZoomSectionWidthDip = 80.0f;
    static constexpr float kStatusDocumentSectionWidthDip = 140.0f;
    static constexpr float kJobProgressSectionWidthDip = 160.0f;
    static constexpr float kJobProgressInsetDip = 8.0f;
    static constexpr float kJobProgressTrackHeightDip = 8.0f;
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

    explicit ChromeHost(ChromeEvents& events) noexcept;
    ~ChromeHost();

    ChromeHost(const ChromeHost&) = delete;
    ChromeHost& operator=(const ChromeHost&) = delete;

    HRESULT Attach(ID3D11Device* device, void* requestContext, void (*requestFrame)(void*) noexcept) noexcept;
    HRESULT ReplaceDevice(ID3D11Device* device) noexcept;
    void Detach() noexcept;
    void SetVisible(bool visible) noexcept;
    void SetOwnerWindow(HWND window) noexcept;
    // The Rename Layer card (C16): opened on a name; closing it reports ChromeEvents::OnRename.
    void OpenRenameCard(const std::wstring& name) noexcept;
    void CloseRenameCard(bool commit) noexcept;
    [[nodiscard]] bool RenameCardOpen() const noexcept;
    // The Move tool's header: typed numbers, flips, sampling, Apply/Cancel and the toggles.
    void SetTransformInspector(const TransformInspectorState& state) noexcept;
    // The transform box in canvas-hole pixels (nullopt hides it) and snap guides in canvas-hole pixels.
    void SetTransformOverlay(const std::optional<TransformOverlayGeometry>& geometry, const std::vector<float>& guideXs,
                             const std::vector<float>& guideYs) noexcept;
    // The selection tools' header: kinds, mode, wand settings, Anti-alias, Expand / Contract and Deselect.
    void SetSelectionInspector(const SelectionInspectorState& state) noexcept;
    // The selection outline (marching ants) and the lasso draft, in canvas-hole pixels.
    void SetSelectionOutline(std::vector<std::vector<D2D1_POINT_2F>> loops) noexcept;
    void SetSelectionAntPhase(float phase) noexcept;
    [[nodiscard]] float SelectionAntPhase() const noexcept;
    void SetLassoDraft(std::optional<CanvasOverlay::Draft> draft) noexcept;
    // The Brush and Eraser header: mode, size, hardness, opacity, mask paint and the color swatch.
    void SetBrushInspector(const BrushInspectorState& state) noexcept;
    // The tip circle over the canvas, in canvas-hole pixels (nullopt hides it).
    void SetBrushCursor(std::optional<CanvasOverlay::BrushCursor> cursor) noexcept;
    // The Gradient and Shape headers (C11), and the tool overlays in canvas-hole pixels (nullopt hides each).
    void SetGradientInspector(const GradientInspectorState& state) noexcept;
    void SetShapeInspector(const ShapeInspectorState& state) noexcept;
    // The Crop header (C16) and the shade outside the crop frame (canvas-hole pixels; nullopt clears it).
    void SetCropInspector(const CropInspectorState& state) noexcept;
    void SetCropShade(std::optional<D2D1_RECT_F> framePixels) noexcept;
    // The adjustment panel (C12): the mirrored session edit; its controls report adjust actions, the curve editor's
    // points and the spectrum's band handles.
    void SetAdjustInspector(const AdjustInspectorState& state) noexcept;
    [[nodiscard]] bool AdjustPanelOpen() const noexcept;
    // The size cards (C16): the mirrored draft; the controls report size actions.
    void SetSizeCard(const SizeCardState& state) noexcept;
    [[nodiscard]] bool SizeCardOpen() const noexcept;
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
    struct PickerSampleAuthority final
    {
        Guid host;
        std::uint64_t revision = 0;
        PickerTarget target = PickerTarget::Foreground;
        bool operator==(const PickerSampleAuthority&) const noexcept = default;
    };
    [[nodiscard]] std::optional<PickerSampleAuthority> CapturePickerSampleAuthority() const noexcept;
    // Explicit picker input is independent of the revision renewed by a checked sample's preview callback.
    [[nodiscard]] std::optional<PickerSampleAuthority> CapturePickerIntentAuthority() const noexcept;
    [[nodiscard]] bool IsPickerIntentCurrent(const PickerSampleAuthority& authority) const noexcept;
    [[nodiscard]] bool ApplySampledPickerColor(PickerSampleAuthority& authority, uint32_t argb) noexcept;
    // A checkable View menu item (Sample Ring).
    void SetCommandChecked(int command, bool checked) noexcept;
    void SetDocumentTitle(std::wstring title, bool dirty);
    void SetStatus(std::wstring zoom, std::wstring size, std::wstring tool);
    // Application-driven busy state; visibility timing belongs to Application. nullopt means indeterminate.
    void SetJobProgress(bool visible, std::optional<unsigned> percent) noexcept;
    void SetUndoRedoNames(const std::wstring& undoName, const std::wstring& redoName, bool canUndo, bool canRedo);
    // Captions that follow the target, as the legacy menus did: Invert / Invert Mask, Delete Layer / Delete Layer
    // Mask, Free Transform / Transform Selection.
    // `clipped`: the active layer clips to a base, so the Layer menu offers Release Clipping Mask.
    // `merge`: what Ctrl+E would do (0 Merge Down, 1 Merge Group, 2 Merge Layers), for the Layer menu's caption.
    void SetEditTargets(bool masksTargeted, bool transformsSelection, bool clipped, int merge);
    // The Layer menu's mask commands follow the active layer's mask (RF-907): Target Layer Mask / Target Layer Pixels,
    // Unlink / Link Layer Mask, Disable / Enable Layer Mask; without a mask they are grayed.
    void SetMaskCommands(bool present, bool targeted, bool linked, bool enabled);
    void SetTool(EditorTool tool) noexcept;
    // `maskTargeted`: the active layer's mask is the target, so its row's Mask badge shows in the accent (RF-907).
    void SetLayers(std::vector<HierarchyEntry> rows, const std::optional<Guid>& active, bool maskTargeted);
    // The layer a press at a point of the layers panel targets (RF-907, legacy NativeLayerList's thumbnails): the
    // row's mask when the point is on its Mask badge, else the row's pixels when that row's mask is the target; nullopt
    // anywhere else, a floating card over the panel included.
    struct LayerTargetPress
    {
        Guid id;
        bool mask = false;
    };
    [[nodiscard]] std::optional<LayerTargetPress> LayerTargetAt(float xPixels, float yPixels) const;
    // `maskPaintWhite` is engaged while a layer's mask is the target (RF-1113, legacy ColorPalette.swift): the wells
    // show the mask palette instead of the colors (the foreground white when it paints white, the background the
    // other), a well opens no picker, and a picker open on a well closes unchanged.
    void SetPalette(uint32_t foregroundArgb, uint32_t backgroundArgb, std::optional<bool> maskPaintWhite) noexcept;
    // nullopt disables the stepper (no active raster layer). Ignored while the user is typing into it.
    void SetActiveLayerOpacity(std::optional<double> opacity) noexcept;
    // Persisted layers width; clamped by the splitter on the next Prepare.
    void SetLayersWidth(float widthDip) noexcept;
    // The color picker card on `target`, starting from `argb`: a palette well previews into the well and OK sets it;
    // every change goes to ChromeEvents::OnPickerColor with the target.
    void OpenColorPicker(PickerTarget target, uint32_t argb) noexcept;
    // Host-driven close (Escape fallback, self-test): commits or cancels the open picker.
    void CloseColorPicker(bool commit) noexcept;
    // Host eyedropper result while the picker is open (previews, does not commit).
    void SampleColor(uint32_t argb) noexcept;
    // Self-test hook: one committed stepper change, as the increment button would produce.
    [[nodiscard]] bool RequestLayerOpacity(double percent) noexcept;
    // Self-test hook: shows the tooltip of the hovered control at once, as the hover delay would; true when one shows.
    [[nodiscard]] bool ForceTooltipForTest() noexcept;
    // Self-test hook: DxUi's focus on the layer opacity stepper's text field, as a click on its value leaves it (the
    // stepper itself is a panel, which takes no focus).
    void FocusLayerOpacityForTest() noexcept;
    // Self-test hook: the keyboard on the layers panel's list, as a click on a row would leave it.
    void FocusLayersForTest() noexcept;
    // Self-test hooks for the live controls (review fixes wave 1b, RF-1107): the Gaussian Blur radius field focused
    // with its text selected, as a click and Ctrl+A leave it, and the text it shows; the Image Size unit combo
    // focused, as a click on it leaves it; the Hue/Saturation spectrum's bounds.
    [[nodiscard]] bool FocusAdjustRadiusForTest() noexcept;
    [[nodiscard]] std::wstring AdjustRadiusTextForTest() const;
    [[nodiscard]] bool FocusSizeUnitForTest() noexcept;
    [[nodiscard]] D2D1_RECT_F SpectrumBoundsDipForTest() const noexcept;
    enum class AdjustControlForTest : std::uint8_t
    {
        Curves,
        LevelsInput,
        Spectrum,
        LevelsAutoContrast,
        Apply,
        Cancel,
        HueRange,
        CurveRemove,
    };
    [[nodiscard]] const AdjustInspectorState& AdjustInspectorForTest() const noexcept
    {
        return _adjustInspector;
    }
    [[nodiscard]] D2D1_RECT_F AdjustControlBoundsForTest(AdjustControlForTest control) const noexcept;
    [[nodiscard]] bool AdjustControlEnabledForTest(AdjustControlForTest control) const noexcept;
    [[nodiscard]] std::uint64_t AdjustCallbackCountForTest() const noexcept
    {
        return _adjustCallbacksForTest;
    }
    [[nodiscard]] bool JobProgressVisible() const noexcept;
    [[nodiscard]] bool JobProgressIndeterminate() const noexcept;
    [[nodiscard]] std::optional<unsigned> JobProgressPercent() const noexcept;
    [[nodiscard]] D2D1_RECT_F JobProgressBoundsDip() const noexcept;
    // Lays the chrome out for the client size and DPI without drawing: HolePixels follows. Prepare lays out and draws.
    void Layout(UINT widthPixels, UINT heightPixels, float dpi) noexcept;
    HRESULT Prepare(UINT widthPixels, UINT heightPixels, float dpi) noexcept;
    HRESULT Composite(ID3D11DeviceContext* context, const D3D11_VIEWPORT& viewport) noexcept;
    bool DispatchPointer(const DxUi::PointerEvent& event) noexcept;
    bool DispatchKey(UINT virtualKey, bool down, UINT modifiers = 0) noexcept;
    bool DispatchCharacter(wchar_t character, UINT modifiers = 0) noexcept;
    bool AdvanceAnimation(uint64_t nowTickMs) noexcept;
    [[nodiscard]] bool NeedsPreparation() const noexcept;
    [[nodiscard]] bool NeedsAnimation() const noexcept;
    [[nodiscard]] DxUi::EmbeddedStatistics Statistics() const noexcept;
    // What the chrome wrote for the Application's pushes (RF-1001), for the self-test's steady-state checks: each
    // count is one push that changed something, never one that found its region as it was.
    struct SyncCounters
    {
        std::uint64_t layerRows = 0; // SetLayers rebuilt the rows and told the tree
        std::uint64_t menus = 0;     // SetUndoRedoNames, SetEditTargets or SetMaskCommands rebuilt their captions
        std::uint64_t texts = 0;     // SetStatus or SetDocumentTitle wrote a text
        std::uint64_t headers = 0;   // a tool header, the adjust panel or a size card wrote its controls
        std::uint64_t overlays = 0;  // the canvas overlay took something new to draw
        std::uint64_t publishes = 0; // the UI Automation snapshot was republished
    };
    [[nodiscard]] const SyncCounters& SyncCountersForTest() const noexcept
    {
        return _syncCounters;
    }
    // Bumped when the chrome rebuilds its controls or retitles its chords (bindings, keyboard layout): text the
    // Application built for it before then (the status bar's) must be pushed again.
    [[nodiscard]] std::uint64_t DisplayRevision() const noexcept
    {
        return _displayRevision;
    }
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
        if (focused)
        {
            LeaveMenuKeyboard(); // the menu bar never keeps the keyboard behind the canvas
        }
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
    // UI Automation (ChromeHostAccessibility.cpp): the window's root with the DxUi tree beneath it. DxUi serves the
    // tree from a snapshot it republishes when told (EmbeddedHost::UpdateAccessibility walks every control). After a
    // preparation that changed the chrome, Prepare republishes while a client listens (UiaClientsAreListening), when
    // the accessible structure changed (a tool switch, a card or panel, a header control shown or hidden, the layers
    // panel's rows, the document title) or when a client asked for the tree meanwhile; otherwise the snapshot waits
    // until a client asks for the tree (RF-1001). A pointer move changes none of those.
    void AttachAccessibility() noexcept;
    void DetachAccessibility() noexcept;
    // WM_GETOBJECT: a client connecting (UiaRootObjectId) gets the window root, the waiting snapshot republished first.
    [[nodiscard]] LRESULT HandleGetObject(WPARAM wParam, LPARAM lParam) noexcept;
    // Marks the placement (client rectangle, keyboard focus in the chrome) for the next Prepare and asks for a
    // frame: DxUi is never re-entered from a pointer, key or focus path.
    void UpdateAccessibilityPlacement() noexcept;
    // The window root as an in-process client reads it (the self-test's walks): the waiting snapshot is republished
    // first, as for WM_GETOBJECT. Not add-referenced.
    [[nodiscard]] IRawElementProviderFragmentRoot* AccessibilityRoot() noexcept;
    [[nodiscard]] HRESULT AccessibilityStatus() const noexcept // the last attach attempt (S_FALSE: none yet)
    {
        return _accessibilityStatus;
    }
    // Self-test hooks (RF-1001): whether a UI Automation client listens, as the self-test decides (nullopt: ask UI
    // Automation), and whether the published snapshot is behind the chrome (a preparation or a placement since).
    void SetAccessibilityListenerForTest(std::optional<bool> listening) noexcept
    {
        _accessibilityListenerForTest = listening;
    }
    [[nodiscard]] bool AccessibilityStaleForTest() const noexcept;
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
    // What the rail's wells show: the colors, or the mask palette while a mask is the target (RF-1113 self-test).
    [[nodiscard]] uint32_t ForegroundWellForTest() const noexcept;
    [[nodiscard]] uint32_t BackgroundWellForTest() const noexcept;
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
    // A row's Mask badge in DIP (empty without one) and the row as the tree paints it (RF-907 self-test).
    [[nodiscard]] D2D1_RECT_F LayerMaskBadgeBoundsDip(std::size_t index) const noexcept;
    // The Move tool header's Flip Horizontal button, for a self-test click; empty while hidden.
    [[nodiscard]] D2D1_RECT_F FlipHorizontalBoundsDipForTest() const noexcept;
    [[nodiscard]] DxUi::TreeItemData LayerRowItemForTest(std::size_t index) const;
    [[nodiscard]] bool MenuItemGrayedForTest(int command) const noexcept;
    // 0 none, 1 layers tree, 2 splitter, 3 shortcuts, 4 notice, 5 adjust, 6 picker, 7 overlay, 8 other.
    [[nodiscard]] int HitKindForTest(float xPixels, float yPixels) noexcept;
    // The open filter panel's option row (radius, curve, ramp, hint) is visible inside the card.
    [[nodiscard]] bool FilterOptionsVisible() const noexcept;
    void ShowNotice(std::wstring text) noexcept;
    void CloseNotice() noexcept;
    [[nodiscard]] bool NoticeOpen() const noexcept;
    // Escape closes the floating card on top wherever the focus is: More tools, the notice, the Command List.
    bool EscapeFloatingCard() noexcept;
    [[nodiscard]] D2D1_RECT_F NoticeBoundsDip() const noexcept;
    // Help › Command List: every catalog command with its effective chord, a search field that filters by name.
    // Enter in the search field runs the selected row, or the first row when none is selected; Enter, Space or a
    // double-click on the list runs its row, and with no row selected the first Enter or Space only selects the first
    // row. Closing gives the keyboard focus back to the card underneath (the Rename card's field, the color picker)
    // or, with no such card, to the canvas.
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
    // Where the keyboard is: the canvas, or DxUi's focused control (none when null). Application::OnKeyDown compares
    // it across a key down the chrome took.
    struct KeyboardFocusMark
    {
        bool canvas = false;
        const DxUi::Control* control = nullptr;
        [[nodiscard]] bool operator==(const KeyboardFocusMark&) const noexcept = default;
    };
    [[nodiscard]] KeyboardFocusMark KeyboardFocus() const noexcept;
    // After a key down: true when it moved the keyboard (Tab, a button or row that opened a card, a command that
    // closed one). A control a navigation key (`navigation`: Tab, the arrows) moved the focus to counts as reached
    // with the keyboard until the next pointer press; a card opening or closing is not navigation.
    bool NoteKeyFocusMove(const KeyboardFocusMark& before, bool navigation) noexcept;
    // Whether the chrome control holding the focus was reached with the keyboard (Tab), not with a press: Space then
    // presses it instead of panning (Specs/UI/UI_ApplicationChrome.md "Menus and accelerators").
    [[nodiscard]] bool FocusFromKeyboard() const noexcept;
    // Whether the focused chrome control takes Enter and Escape as its own: a control the keyboard reached, a text
    // input (Escape reverts its typed edit), a dropdown showing its list (Enter chooses, Escape closes it) and a
    // control in a card that took the keyboard (the notice, the Command List, the Rename card, the picker) do. A
    // control a click focused does not: its Enter applies and its Escape cancels the edit under way, as the legacy
    // Apply and Cancel are the default and cancel buttons (a clicked header button is not pressed again, and Escape
    // still cancels).
    [[nodiscard]] bool FocusTakesEnterAndEscape() const noexcept;
    // Keyboard access to the menu bar (a tapped Alt or F10, SC_KEYMENU): the bar takes the focus with its first
    // item highlighted; arrows move, Enter, Space or Down open, a letter opens that menu, Escape leaves and the
    // focus returns where it was. A pointer press anywhere (the canvas included), the canvas taking the keyboard
    // and the window losing the focus leave it too.
    void ToggleMenuKeyboard() noexcept;
    void LeaveMenuKeyboard() noexcept;
    [[nodiscard]] bool MenuKeyboardActive() const noexcept;
    // Alt+letter, or a letter while the menu bar has the keyboard: opens the menu with that mnemonic.
    bool OpenMenuByMnemonic(wchar_t mnemonic) noexcept;
    // Self-test hooks: menus are recorded instead of shown (a native flyout runs a modal loop), and the Command List
    // and focus are read and steered without the pointer. A command probe, while set, takes the commands the chrome
    // would report (a command that runs a modal loop inside a chrome callback).
    void SetMenusRecordOnlyForTest(bool recordOnly) noexcept;
    void SetCommandProbeForTest(std::function<void(int)> probe) noexcept;
    [[nodiscard]] std::optional<std::size_t> TakeRecordedMenuForTest() noexcept;
    [[nodiscard]] std::size_t ShortcutRowCountForTest() const noexcept;
    [[nodiscard]] int ShortcutCommandAtForTest(std::size_t index) const noexcept;
    [[nodiscard]] int SelectedShortcutForTest() const noexcept;
    void SelectShortcutForTest(int command) noexcept;
    void FocusShortcutListForTest() noexcept;
    [[nodiscard]] std::wstring ShortcutSearchTextForTest() const;
    [[nodiscard]] std::wstring FocusedControlNameForTest() const;
    [[nodiscard]] std::wstring FocusedTextForTest() const; // the focused text field's text; empty for another control
    // The open Command List's model fills one item from its longest row, then from its shortest, as DxUi's Tree::Paint
    // reuses one item for every row: true when the item's text buffer kept its capacity (no allocation per row).
    [[nodiscard]] bool ShortcutRowsKeepBuffersForTest() const;
    // DxUi's focus on nothing, as DxUi leaves it when it prunes a hidden or disabled control; the canvas flag stays.
    void DropChromeFocusForTest() noexcept;
    [[nodiscard]] std::wstring ToolTooltipForTest(EditorTool tool) const;
    [[nodiscard]] std::wstring PaletteSwapTooltipForTest() const;

    // The catalog rows (RedPrism/CommandCatalog.h) under their former names.
    using CommandShortcutDefault = CommandCatalogEntry;
    [[nodiscard]] static std::span<const CommandShortcutDefault> CommandShortcuts() noexcept
    {
        return CommandCatalog();
    }

  private:
    class LayerTreeModel final : public DxUi::ITreeModel, public DxUi::ITreeDelegate
    {
      public:
        explicit LayerTreeModel(ChromeEvents& events) noexcept : events(events) {}
        ChromeEvents& events;
        std::vector<HierarchyEntry> rows;
        // Each row's display text (the name, or the "(hidden)" suffixed form), built once by SetRows so
        // GetVisibleItem's per-paint call is an assignment into the caller's reused buffer, never a
        // FormatStringResource call.
        std::vector<std::wstring> displayText;
        // Each row's Mask badge (RF-907): empty without a mask, else `Mask` or `Mask off`, in the accent while that
        // mask is the target; the strings load once.
        std::vector<const std::wstring*> badgeText;
        std::vector<DxUi::AdornmentTone> badgeTone;
        std::wstring maskBadge;
        std::wstring maskOffBadge;
        std::optional<Guid> active;
        bool maskTargeted = false;
        std::wstring hiddenPattern;
        bool useFluentIcons = true;
        // Stores the hierarchy and rebuilds displayText and the badges once; SetLayers calls this instead of assigning
        // rows directly so a settled layers panel does not re-format every row's name on every repaint.
        void SetRows(std::vector<HierarchyEntry> newRows, const std::optional<Guid>& newActive, bool newMaskTargeted);
        [[nodiscard]] size_t GetVisibleItemCount() const noexcept override;
        void GetVisibleItem(size_t visibleIndex, DxUi::TreeItemData& outItem) const override;
        void OnTreeSelectionChanged(uint64_t itemId) override;
        void OnTreeToggleExpanded(uint64_t itemId, bool expanded) override;
        void OnTreeItemInvoked(uint64_t itemId) override;
        void OnTreeReorder(const DxUi::TreeDrop& drop) override;
    };

    // The Command List's rows. An item's id is its command id, so a selection survives filtering only while its
    // command is still listed and never moves to another command.
    class ShortcutListModel final : public DxUi::ITreeModel, public DxUi::ITreeDelegate
    {
      public:
        struct Row
        {
            std::wstring name;
            std::wstring chord;
            int command = 0;
        };
        explicit ShortcutListModel(ChromeHost& host) noexcept : host(host) {}
        ChromeHost& host; // a row invoked runs through LaunchShortcut
        std::vector<Row> all;
        std::vector<Row> rows;
        std::wstring query;
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
    // Enter or Space on the list with no row selected: selects the first row and runs nothing (DxUi's Tree would
    // select it and run it at once, so a stray Enter ran New).
    bool SelectFirstShortcutRow(UINT virtualKey) noexcept;
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
    // Control factories for the tool headers and the cards. Each adds its control to `parent`, names it and wires
    // `handler`: an action (reported through ChromeEvents unless a sync is writing the controls) or a callable taking
    // what the control reports (the stepper's change, the combo's index, the checkbox's state, nothing for a button).
    struct ComboSpec
    {
        UINT nameId = 0; // the accessible name; 0: the tooltip names it
        UINT tipId = 0;
        std::size_t selected = 0;
    };
    struct StepperSpec
    {
        UINT labelId = 0;     // also the accessible name (never the tooltip)
        float labelWidth = 0; // 0: the label names the stepper without being painted
        UINT unitId = 0;
        float unitWidth = 0;
        double minimum = 0;
        double maximum = 0;
        double step = 1; // the large step is ten steps
        std::uint8_t decimals = 0;
        std::optional<double> value;
        UINT tipId = 0;
    };
    struct CheckSpec
    {
        UINT labelId = 0;
        UINT tipId = 0;
        UINT nameId = 0; // 0: the caption names it
        bool checked = false;
    };
    struct ButtonSpec
    {
        UINT labelId = 0;
        UINT tipId = 0;
        bool primary = false;
    };
    template <typename Handler>
    DxUi::ComboBox* MakeCombo(DxUi::Panel& parent, std::vector<DxUi::ComboBox::Item> items, const ComboSpec& spec,
                              Handler handler);
    template <typename Handler>
    DxUi::NumericStepper* MakeStepper(DxUi::Panel& parent, const StepperSpec& spec, Handler handler);
    template <typename Handler> DxUi::Checkbox* MakeCheck(DxUi::Panel& parent, const CheckSpec& spec, Handler handler);
    template <typename Handler> DxUi::Button* MakeButton(DxUi::Panel& parent, const ButtonSpec& spec, Handler handler);
    // Combo items with stable values, or numbered from `first` (the unit list starts at Inches without resampling).
    [[nodiscard]] static std::vector<DxUi::ComboBox::Item> NamedItems(
        std::initializer_list<std::pair<const wchar_t*, UINT>> items);
    [[nodiscard]] static std::vector<DxUi::ComboBox::Item> NumberedItems(std::span<const UINT> ids,
                                                                         std::size_t first = 0);
    [[nodiscard]] static std::vector<DxUi::ComboBox::Item> NumberedItems(std::initializer_list<UINT> ids)
    {
        return NumberedItems(std::span<const UINT>(ids.begin(), ids.size()));
    }
    // A header's or card's action from one of its controls, unless a sync is writing the controls.
    template <typename Action> void Report(Action action, double value) noexcept;
    // Set*Inspector, SetSizeCard and SetAdjustInspector write their controls with the reports held back.
    [[nodiscard]] auto HoldReports() noexcept
    {
        const bool outer = std::exchange(_syncing, true);
        return wil::scope_exit([this, outer]() noexcept { _syncing = outer; });
    }
    // A header stepper shows `value` unless the user is typing in it.
    static void SyncStepper(DxUi::NumericStepper* stepper, double value, bool enabled) noexcept;
    // One control of a tool header's row: its width, whether the header's state wants it, a vertical inset (the
    // swatches) and the gap after it.
    struct HeaderSlot
    {
        DxUi::Control* control = nullptr;
        float width = 0;
        bool wanted = true;
        float inset = 0;
        float gap = 6.0f;
    };
    // A tool header's row: the pinned slots take the right end from the right (with `keepTitleRoom`, each only while
    // it leaves 80 DIP to its left); the flow slots run from the left in order and stop at the first wanted slot that
    // does not fit. A slot that is not wanted or has no room hides.
    void ArrangeHeaderRow(DxUi::Panel* header, const D2D1_RECT_F& bounds, std::span<const HeaderSlot> pinned,
                          std::span<const HeaderSlot> flow, bool keepTitleRoom) noexcept;
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
    void NotePickerInput(bool sampledPublication = false) noexcept;
    // A palette well shows `argb` (the picker's working color while it previews there).
    void ShowPaletteWell(PickerTarget target, uint32_t argb) noexcept;
    // Hides the picker card without reporting a pick (its target's card closed).
    void ClosePickerCard() noexcept;
    // The modal cards, oldest first, each with the keyboard as it was when it opened. A card with a `home` control
    // takes the keyboard when it opens; a card that holds the keyboard when it closes gives it back: to the chrome
    // control that had it (never the menu bar a click left it on), else to the home of the card now on top, else to
    // the canvas. An Enter or Escape the focused control leaves is the OK or Cancel of the card holding the focus,
    // else of the top card (DxUi's embedded host routes no default or cancel button).
    enum class ModalCard : std::uint8_t
    {
        Notice,
        Shortcuts,
        Rename,
        Size,
        Adjust,
        Picker,
        Count,
    };
    struct OpenCard
    {
        ModalCard card = ModalCard::Notice;
        DxUi::Control* panel = nullptr;
        DxUi::Control* home = nullptr;     // takes the keyboard when the card opens (none for the size card and panel)
        DxUi::Control* keyboard = nullptr; // the chrome control that had the keyboard before
        bool canvas = false;               // the canvas had the keyboard before
    };
    void PushCard(ModalCard card, DxUi::Control* panel, DxUi::Control* home) noexcept;
    void PopCard(ModalCard card) noexcept;
    [[nodiscard]] std::optional<OpenCard> TakeCard(ModalCard card) noexcept; // removes the card's entry
    [[nodiscard]] const OpenCard* KeyboardCard() const noexcept;
    bool CardKey(bool enter) noexcept;
    [[nodiscard]] float WidthDip() const noexcept;
    [[nodiscard]] float HeightDip() const noexcept;

    ChromeEvents& _events;
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
    // run every frame, including between a press and its release. A control shown or hidden changes the accessible
    // structure.
    static void PlaceControl(DxUi::Control* control, const D2D1_RECT_F& bounds) noexcept;
    void ShowControl(DxUi::Control* control, bool visible) noexcept;
    // Canvas-hole pixels to DIP at the current layout DPI, for the overlay geometry the setters below hand to
    // CanvasOverlay (96/_dpi, or 1 before the first layout knows a DPI).
    [[nodiscard]] float PixelToDip(float pixels) const noexcept
    {
        return _dpi > 0.0f ? pixels * 96.0f / _dpi : pixels;
    }
    [[nodiscard]] D2D1_POINT_2F PixelToDip(D2D1_POINT_2F pixels) const noexcept
    {
        return D2D1::Point2F(PixelToDip(pixels.x), PixelToDip(pixels.y));
    }
    [[nodiscard]] D2D1_RECT_F PixelToDip(const D2D1_RECT_F& pixels) const noexcept
    {
        return D2D1::RectF(PixelToDip(pixels.left), PixelToDip(pixels.top), PixelToDip(pixels.right),
                           PixelToDip(pixels.bottom));
    }
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
    // A client is about to read the tree (WM_GETOBJECT, AccessibilityRoot): a waiting snapshot is republished now
    // from the prepared view, or by the next Prepare when a press, a dispatch or a pending preparation is in the way.
    void PublishAccessibilityForClient() noexcept;
    [[nodiscard]] bool AccessibilityListened() const noexcept;
    // The published snapshot is behind the chrome: a preparation or a placement change since.
    [[nodiscard]] bool AccessibilityBehind() const noexcept;
    bool _accessibilityPlacementDirty = false;
    // DxUi's preparation count when the UI Automation snapshot was last published: a preparation since then changed
    // the controls, and a later Prepare or a client's request republishes them (EmbeddedHost::UpdateAccessibility).
    std::uint64_t _accessibilityPreparations = 0;
    bool _accessibleStructureChanged = false; // since the last publish (see AttachAccessibility above)
    bool _accessibilityRequested = false;     // a client asked for the tree while the snapshot could not be published
    std::optional<bool> _accessibilityListenerForTest;
    SyncCounters _syncCounters;
    std::uint64_t _displayRevision = 0;
    // A region's next push writes its controls whatever its state (RF-1001): set for every region when the controls
    // are built, and for a header or card when one of its controls changed itself (Report and the adjust panel's
    // drawn editors), so a value the session did not take goes back to the control. Otherwise a push that finds its
    // state as last written writes nothing: every DxUi write dirties the whole chrome surface.
    static constexpr std::uint32_t kResyncTransform = 1u << 0;
    static constexpr std::uint32_t kResyncSelection = 1u << 1;
    static constexpr std::uint32_t kResyncBrush = 1u << 2;
    static constexpr std::uint32_t kResyncGradient = 1u << 3;
    static constexpr std::uint32_t kResyncShape = 1u << 4;
    static constexpr std::uint32_t kResyncCrop = 1u << 5;
    static constexpr std::uint32_t kResyncAdjust = 1u << 6;
    static constexpr std::uint32_t kResyncSize = 1u << 7;
    static constexpr std::uint32_t kResyncUndoRedo = 1u << 8;
    static constexpr std::uint32_t kResyncEditTargets = 1u << 9;
    static constexpr std::uint32_t kResyncMaskCommands = 1u << 10;
    static constexpr std::uint32_t kResyncTitle = 1u << 11;
    static constexpr std::uint32_t kResyncLayers = 1u << 12;
    static constexpr std::uint32_t kResyncTool = 1u << 13;
    static constexpr std::uint32_t kResyncAll = ~0u;
    std::uint32_t _resync = kResyncAll;
    // Clears `region`'s mark; true when it was set.
    [[nodiscard]] bool TakeResync(std::uint32_t region) noexcept
    {
        const bool marked = (_resync & region) != 0;
        _resync &= ~region;
        return marked;
    }
    // The dynamic menu rows and the tab title as last written.
    struct MenuTargets
    {
        std::wstring undoName;
        std::wstring redoName;
        bool canUndo = false;
        bool canRedo = false;
        bool masksTargeted = false;
        bool transformsSelection = false;
        bool clipped = false;
        int merge = 0;
        bool maskPresent = false;
        bool maskTargeted = false;
        bool maskLinked = false;
        bool maskEnabled = false;
    };
    MenuTargets _menuTargets;
    std::wstring _documentTitle;
    bool _documentDirty = false;
    bool _inDispatch = false;     // inside _view.Dispatch*: no accessibility flush from a re-entered Prepare
    bool _pointerPressed = false; // a chrome press in progress: DxUi's accessibility calls would cancel it
    struct DeferredJobProgress final
    {
        bool visible;
        std::optional<unsigned> percent;
    };
    std::optional<DeferredJobProgress> _deferredJobProgress;
    bool _syncing = false; // a header or card sync is writing its controls (HoldReports)
    // Detach() calls this: DxUi::ControlHost::Detach frees the whole control tree (_root.reset(), reached through
    // _view.Detach() below), so every raw pointer into it below would dangle without this. Several accessors
    // (NoticeOpen, ShortcutsOpen, MoreToolsOpen, ColorPickerOpen, AdjustPanelOpen, FilterOptionsVisible,
    // RenameCardOpen, SizeCardOpen, and the header setters) check only for null, never `_attached`.
    void ResetControlPointers() noexcept;
    std::array<OpenCard, static_cast<std::size_t>(ModalCard::Count)> _cards{}; // one entry per open card
    std::size_t _cardCount = 0;
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
    DxUi::ProgressBar* _jobProgress = nullptr;
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
    bool _focusFromKeyboard = false;       // a key moved DxUi's focus since the last pointer press
    DxUi::Control* _menuReturnFocus = nullptr;
    bool _menuReturnCanvas = false;
    bool _menusRecordOnly = false;
    std::optional<std::size_t> _recordedMenu;
    std::function<void(int)> _commandProbe; // SetCommandProbeForTest
    // A menu row, a rail tool, a palette hot zone or a Command List row: ChromeEvents::OnCommand (or the probe).
    void RunCommand(int command) noexcept;
    CanvasOverlay* _overlay = nullptr;
    std::vector<float> _guideXsDip; // SetTransformOverlay's snap guides in DIP, reused between calls
    std::vector<float> _guideYsDip;
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
    TransformInspectorState _transformInspector;
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
    DxUi::Label* _shapeTitle = nullptr;
    DxUi::ComboBox* _shapeKind = nullptr;
    DxUi::NumericStepper* _shapeRadius = nullptr;
    DxUi::Label* _shapeFillLabel = nullptr;
    DxUi::ColorSwatch* _shapeFill = nullptr;
    ShapeInspectorState _shapeInspector;
    BrushInspectorState _brushInspector;
    std::array<DxUi::Button*, kEditorToolCount> _toolButtons{}; // icon buttons; the active tool's is latched
    bool _toolVisualsDirty = false;                             // a latch change waits for the pointer to come up
    LayerTreeModel _treeModel;
    // ChromeHostRename.cpp: the Rename Layer card.
    void BuildRenameCard(DxUi::Panel& root);
    void ArrangeRenameCard(float right, float top) noexcept;
    DxUi::CardPanel* _renameCard = nullptr;
    DxUi::Label* _renameTitle = nullptr;
    DxUi::TextField* _renameField = nullptr;
    DxUi::Button* _renameCancel = nullptr;
    DxUi::Button* _renameOk = nullptr;
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
    std::uint64_t _adjustCallbacksForTest = 0;
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
    std::size_t _sizeUnitFirst = 0; // the unit the size card's unit list starts at
    UINT _width = 0;
    UINT _height = 0;
    float _dpi = 96.0f;
    float _layersWidthDip = kLayersDefaultDip;
    float _preferredLayersWidthDip = kLayersDefaultDip;
    uint32_t _foregroundArgb = 0xFF000000u;
    uint32_t _backgroundArgb = 0xFFFFFFFFu;
    std::optional<bool> _maskPaintWhite; // engaged while a layer's mask is the target: the wells show black and white
    EditorTool _tool = EditorTool::Move;
    PickerTarget _pickerTarget = PickerTarget::Foreground; // what the open color picker edits
    Guid _pickerInstance = Guid::Generate();
    InputRevision _pickerInputRevision;
    InputRevision _pickerIntentRevision;
    bool _sampledPickerPublication = false;
    bool _canvasFocused = true;
    bool _attached = false;
};

template <typename Action> void ChromeHost::Report(Action action, double value) noexcept
{
    if (_syncing)
    {
        return;
    }
    // The reporting control shows what the user did, which the session may not take: its header or card writes on
    // its next push whatever its state (`_resync`).
    if constexpr (std::is_same_v<Action, TransformAction>)
    {
        _resync |= kResyncTransform;
        _events.OnTransformAction(action, value);
    }
    else if constexpr (std::is_same_v<Action, SelectionAction>)
    {
        _resync |= kResyncSelection;
        _events.OnSelectionAction(action, value);
    }
    else if constexpr (std::is_same_v<Action, BrushAction>)
    {
        _resync |= kResyncBrush;
        _events.OnBrushAction(action, value);
    }
    else if constexpr (std::is_same_v<Action, GradientAction>)
    {
        _resync |= kResyncGradient;
        _events.OnGradientAction(action, value);
    }
    else if constexpr (std::is_same_v<Action, ShapeAction>)
    {
        _resync |= kResyncShape;
        _events.OnShapeAction(action, value);
    }
    else if constexpr (std::is_same_v<Action, CropAction>)
    {
        _resync |= kResyncCrop;
        _events.OnCropAction(action, value);
    }
    else if constexpr (std::is_same_v<Action, AdjustAction>)
    {
        if (_adjustInspector.busy && action != AdjustAction::Cancel)
        {
            return; // disabling a focused stepper can deliver a late focus-loss commit
        }
        _resync |= kResyncAdjust;
        ++_adjustCallbacksForTest;
        _events.OnAdjustAction(action, value);
    }
    else
    {
        static_assert(std::is_same_v<Action, SizeAction>, "an action ChromeEvents does not report");
        _resync |= kResyncSize;
        _events.OnSizeAction(action, value);
    }
}

template <typename Handler>
DxUi::ComboBox* ChromeHost::MakeCombo(DxUi::Panel& parent, std::vector<DxUi::ComboBox::Item> items,
                                      const ComboSpec& spec, Handler handler)
{
    DxUi::ComboBox* control = parent.AddChild<DxUi::ComboBox>();
    control->SetItems(std::move(items));
    control->SetSelectedIndex(spec.selected);
    control->SetAccessibleName(LoadStringResource(nullptr, spec.nameId != 0 ? spec.nameId : spec.tipId));
    if (spec.tipId != 0)
    {
        control->SetTooltipText(LoadStringResource(nullptr, spec.tipId));
    }
    if constexpr (std::is_enum_v<Handler>)
    {
        control->SetOnSelectionChanged([this, handler](size_t index) { Report(handler, static_cast<double>(index)); });
    }
    else
    {
        control->SetOnSelectionChanged(std::move(handler));
    }
    return control;
}

template <typename Handler>
DxUi::NumericStepper* ChromeHost::MakeStepper(DxUi::Panel& parent, const StepperSpec& spec, Handler handler)
{
    DxUi::NumericStepper* control = parent.AddChild<DxUi::NumericStepper>();
    std::wstring label = LoadStringResource(nullptr, spec.labelId);
    control->SetLabel(label, spec.labelWidth);
    control->SetAccessibleName(std::move(label));
    if (spec.unitId != 0)
    {
        control->SetUnit(LoadStringResource(nullptr, spec.unitId), spec.unitWidth);
    }
    control->SetMinimum(spec.minimum);
    control->SetMaximum(spec.maximum);
    control->SetStep(spec.step);
    control->SetLargeStep(spec.step * 10);
    control->SetDecimals(spec.decimals);
    if (spec.value.has_value())
    {
        control->SetValue(spec.value.value());
    }
    if (spec.tipId != 0)
    {
        control->SetTooltipText(LoadStringResource(nullptr, spec.tipId));
    }
    if constexpr (std::is_enum_v<Handler>)
    {
        control->SetOnChange([this, handler](DxUi::NumericStepperChange change) { Report(handler, change.value); });
    }
    else
    {
        control->SetOnChange(std::move(handler));
    }
    return control;
}

template <typename Handler>
DxUi::Checkbox* ChromeHost::MakeCheck(DxUi::Panel& parent, const CheckSpec& spec, Handler handler)
{
    DxUi::Checkbox* control = parent.AddChild<DxUi::Checkbox>(LoadStringResource(nullptr, spec.labelId));
    if (spec.tipId != 0)
    {
        control->SetTooltipText(LoadStringResource(nullptr, spec.tipId));
    }
    if (spec.nameId != 0)
    {
        control->SetAccessibleName(LoadStringResource(nullptr, spec.nameId));
    }
    if (spec.checked)
    {
        control->SetChecked(true);
    }
    if constexpr (std::is_enum_v<Handler>)
    {
        control->SetOnToggled([this, handler](bool checked) { Report(handler, checked ? 1.0 : 0.0); });
    }
    else
    {
        control->SetOnToggled(std::move(handler));
    }
    return control;
}

template <typename Handler>
DxUi::Button* ChromeHost::MakeButton(DxUi::Panel& parent, const ButtonSpec& spec, Handler handler)
{
    DxUi::Button* control = parent.AddChild<DxUi::Button>(LoadStringResource(nullptr, spec.labelId));
    if (spec.primary)
    {
        control->SetPrimary(true);
    }
    if (spec.tipId != 0)
    {
        control->SetTooltipText(LoadStringResource(nullptr, spec.tipId));
    }
    if constexpr (std::is_enum_v<Handler>)
    {
        control->SetOnClick([this, handler] { Report(handler, 0.0); });
    }
    else
    {
        control->SetOnClick(std::move(handler));
    }
    return control;
}
