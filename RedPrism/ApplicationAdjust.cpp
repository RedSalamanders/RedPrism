#include "Application.h"

#include "Adjust.h"
#include "AdjustControls.h"
#include "ChromeHost.h"
#include "Resources.h"
#include "resource.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <string>
#include <unordered_set>
#include <vector>

#include <UIAutomation.h>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/com.h>
#pragma warning(pop)

// Color adjustments and filters on the host (C12, Specs/UI/UI_ApplicationChrome.md): the Image and Filter menus
// open a session edit, the adjustment panel's actions change its settings, previews reach the canvas through the
// display document, the eyedroppers and the targeted drag read the flattened composite, and an adjustment layer
// edits over the composite beneath it.

namespace
{
[[nodiscard]] AdjustmentColor ColorFromArgb(uint32_t argb) noexcept
{
    return AdjustmentColor{((argb >> 16) & 0xFFu) / 255.0, ((argb >> 8) & 0xFFu) / 255.0, (argb & 0xFFu) / 255.0};
}

[[nodiscard]] uint32_t ArgbOf(const AdjustmentColor& color) noexcept
{
    const auto byte = [](double value)
    { return static_cast<uint32_t>(std::lround(std::clamp(value, 0.0, 1.0) * 255)); };
    return 0xFF000000u | (byte(color.red) << 16) | (byte(color.green) << 8) | byte(color.blue);
}

// Whether the UI Automation subtree under `fragment` holds a Button named `name` (the drawn controls are Buttons for
// UI Automation, AdjustControls.h), walked as a client would, through Navigate.
[[nodiscard]] bool HasNamedButton(IRawElementProviderFragment* fragment, const std::wstring& name, int& budget) noexcept
{
    if (fragment == nullptr || budget <= 0)
    {
        return false;
    }
    --budget;
    wil::com_ptr_nothrow<IRawElementProviderSimple> simple;
    if (SUCCEEDED(fragment->QueryInterface(IID_PPV_ARGS(simple.put()))) && simple)
    {
        VARIANT value;
        VariantInit(&value);
        const bool named = SUCCEEDED(simple->GetPropertyValue(UIA_NamePropertyId, &value)) && value.vt == VT_BSTR &&
                           value.bstrVal != nullptr && name == value.bstrVal;
        (void)VariantClear(&value);
        VariantInit(&value);
        const bool button = SUCCEEDED(simple->GetPropertyValue(UIA_ControlTypePropertyId, &value)) &&
                            value.vt == VT_I4 && value.lVal == UIA_ButtonControlTypeId;
        (void)VariantClear(&value);
        if (named && button)
        {
            return true;
        }
    }
    wil::com_ptr_nothrow<IRawElementProviderFragment> child;
    if (FAILED(fragment->Navigate(NavigateDirection_FirstChild, child.put())))
    {
        return false;
    }
    while (child && budget > 0)
    {
        if (HasNamedButton(child.get(), name, budget))
        {
            return true;
        }
        wil::com_ptr_nothrow<IRawElementProviderFragment> next;
        if (FAILED(child->Navigate(NavigateDirection_NextSibling, next.put())))
        {
            break;
        }
        child = std::move(next);
    }
    return false;
}

[[nodiscard]] std::optional<AdjustmentKind> AdjustmentKindForCommand(int command) noexcept
{
    switch (command)
    {
    case ChromeHost::kCommandNewLevelsAdjustment:
        return AdjustmentKind::Levels;
    case ChromeHost::kCommandNewCurvesAdjustment:
        return AdjustmentKind::Curves;
    case ChromeHost::kCommandNewHueSaturationAdjustment:
        return AdjustmentKind::Hsv;
    case ChromeHost::kCommandNewExposureAdjustment:
        return AdjustmentKind::Exposure;
    case ChromeHost::kCommandNewGradientMapAdjustment:
        return AdjustmentKind::GradientMap;
    case ChromeHost::kCommandNewGrainAdjustment:
        return AdjustmentKind::Grain;
    default:
        return std::nullopt;
    }
}

// The panel an Image or Filter menu command opens.
[[nodiscard]] std::optional<FilterKind> ColorEditKindForCommand(int command) noexcept
{
    switch (command)
    {
    case ChromeHost::kCommandLevels:
        return FilterKind::Levels;
    case ChromeHost::kCommandHueSaturation:
        return FilterKind::HueSaturation;
    case ChromeHost::kCommandCurves:
        return FilterKind::Curves;
    case ChromeHost::kCommandExposure:
        return FilterKind::Exposure;
    case ChromeHost::kCommandGradientMap:
        return FilterKind::GradientMap;
    case ChromeHost::kCommandGrain:
        return FilterKind::Grain;
    case ChromeHost::kCommandGaussianBlur:
        return FilterKind::GaussianBlur;
    case ChromeHost::kCommandMotionBlur:
        return FilterKind::MotionBlur;
    case ChromeHost::kCommandAddNoise:
        return FilterKind::AddNoise;
    case ChromeHost::kCommandLensCorrection:
        return FilterKind::LensCorrection;
    case ChromeHost::kCommandContentAwareFill:
        return FilterKind::ContentAwareFill;
    default:
        return std::nullopt;
    }
}
} // namespace

bool Application::BeginColorEdit(int command) noexcept
{
    const std::optional<FilterKind> kind = ColorEditKindForCommand(command);
    if (!kind.has_value())
    {
        return false;
    }
    _session.BeginColorEdit(kind.value());
    if (!_session.HasColorEdit())
    {
        if (_session.TakeBudgetRefusal())
        {
            // The filter's grown grid would not fit the document's pixel budget (Decision D7): that is the reason,
            // not the layer.
            ReportBudgetRefusal();
            return false;
        }
        UINT captionId = 0;
        for (const ChromeHost::CommandShortcutDefault& row : ChromeHost::CommandShortcuts())
        {
            if (row.command == command)
            {
                captionId = row.captionId;
                break;
            }
        }
        if (captionId != 0)
        {
            ShowUnavailable(captionId);
        }
        return false;
    }
    DocumentChanged();
    return true;
}

void Application::AddAdjustmentLayer(int command) noexcept
{
    const std::optional<AdjustmentKind> kind = AdjustmentKindForCommand(command);
    if (!kind.has_value())
    {
        return;
    }
    ApplyColorEdit();
    _session.AddAdjustment(kind.value());
    OpenAdjustmentEditor();
}

void Application::EditActiveAdjustment() noexcept
{
    const ImageLayer* layer = _session.ActiveLayer();
    if (layer == nullptr || !layer->adjustment.has_value() || _session.HasColorEdit())
    {
        return;
    }
    _session.SetAdjustmentEditingID(layer->id);
    OpenAdjustmentEditor();
}

void Application::OpenAdjustmentEditor() noexcept
{
    const std::optional<Guid> id = _session.AdjustmentEditingID();
    if (!id.has_value())
    {
        DocumentChanged();
        return;
    }
    const std::optional<Raster> below = CompositeBelow(id.value());
    if (!below.has_value() || !_session.BeginAdjustmentEditing(id.value(), below.value()))
    {
        _session.SetAdjustmentEditingID(std::nullopt);
    }
    DocumentChanged();
}

std::optional<Raster> Application::CompositeBelow(const Guid& id) noexcept
{
    if (!_session.Document().has_value())
    {
        return std::nullopt;
    }
    // Keep records for live-mask references and folder ancestry, but leave the adjustment itself and everything
    // above it out of the sampling image.
    CanvasDocument copy = _session.Document().value();
    std::unordered_set<Guid> hidden;
    bool above = false;
    for (const Guid& layerId : RenderLayerIds(copy))
    {
        if (layerId == id)
        {
            above = true;
        }
        if (above)
        {
            hidden.insert(layerId);
        }
    }
    for (ImageLayer& layer : copy.layers)
    {
        if (!layer.isGroup && (layer.id == id || hidden.contains(layer.id)))
        {
            layer.visible = false;
        }
    }
    const std::optional<Raster> raster = ComposeDocumentReadback(copy);
    if (raster.has_value() && raster->HasStorage())
    {
        return raster;
    }
    return std::nullopt;
}

void Application::ApplyColorEdit() noexcept
{
    if (!_session.HasColorEdit())
    {
        return;
    }
    _hueTargetStart.reset();
    _session.CommitColorEdit();
    DocumentChanged();
}

void Application::CancelColorEdit() noexcept
{
    if (!_session.HasColorEdit())
    {
        return;
    }
    _hueTargetStart.reset();
    _session.CancelColorEdit();
    DocumentChanged();
}

void Application::OnAdjustAction(AdjustAction action, double value) noexcept
{
    const std::optional<ColorEdit>& edit = _session.PendingColorEdit();
    if (!std::isfinite(value) || !edit.has_value())
    {
        return;
    }
    // Every panel's controls report here; each changes its own kind's settings.
    const bool levels = edit->kind == FilterKind::Levels;
    FilterSettings settings = edit->settings;
    bool preview = edit->preview;
    LevelRange range = Adjust::LevelsCurrent(settings.levels);
    HueSaturationSettings& hsv = settings.hsv;
    std::vector<CurvePoint>& points = settings.curves.channels[static_cast<std::size_t>(settings.curves.channel)];
    switch (action)
    {
    case AdjustAction::Cancel:
        CancelColorEdit();
        return;
    case AdjustAction::Apply:
        ApplyColorEdit();
        return;
    case AdjustAction::OpenShadows:
    case AdjustAction::OpenHighlights:
        if (edit->kind == FilterKind::GradientMap && _chrome)
        {
            _adjustColorOriginal = settings.gradientMap;
            const bool highlights = action == AdjustAction::OpenHighlights;
            _chrome->OpenColorPicker(
                highlights ? PickerTarget::GradientHighlights : PickerTarget::GradientShadows,
                ArgbOf(highlights ? settings.gradientMap.highlights : settings.gradientMap.shadows));
        }
        return;
    case AdjustAction::SetPreview:
        preview = value != 0.0;
        break;
    case AdjustAction::SetChannel:
        (levels ? settings.levels.channel : settings.curves.channel) =
            static_cast<LevelsChannel>(std::clamp(static_cast<int>(value), 0, 3));
        break;
    case AdjustAction::Reset:
        if (levels)
        {
            settings.levels = LevelsSettings{};
            range = LevelRange{};
            _session.SetLevelsSampleMode(std::nullopt);
        }
        else if (edit->kind == FilterKind::HueSaturation)
        {
            hsv = hsv.colorize ? Adjust::ColorizeStart() : HueSaturationSettings{};
        }
        else
        {
            points = {CurvePoint{0, 0}, CurvePoint{255, 255}};
        }
        break;
    // Levels.
    case AdjustAction::SetInputBlack:
        range.black = std::min(range.white - 1, std::max(0.0, value));
        break;
    case AdjustAction::SetInputGamma:
        range.gamma = std::clamp(value, 0.1, 9.99);
        break;
    case AdjustAction::SetInputWhite:
        range.white = std::max(range.black + 1, std::min(255.0, value));
        break;
    case AdjustAction::SetOutputBlack:
        range.outputBlack = std::clamp(value, 0.0, 255.0);
        break;
    case AdjustAction::SetOutputWhite:
        range.outputWhite = std::clamp(value, 0.0, 255.0);
        break;
    case AdjustAction::SetLevelsSample:
        _session.SetLevelsSampleMode(value <= 0.0 ? std::nullopt
                                                  : std::optional<LevelsSample>(static_cast<LevelsSample>(
                                                        std::clamp(static_cast<int>(value) - 1, 0, 2))));
        SyncChrome();
        return;
    case AdjustAction::AutoLevels:
        _session.AutoLevels(static_cast<LevelsAuto>(std::clamp(static_cast<int>(value), 0, 2)));
        DocumentChanged();
        return;
    // Hue/Saturation.
    case AdjustAction::SetRange:
        hsv.range = static_cast<ColorRange>(std::clamp(static_cast<int>(value), 0, 6));
        break;
    case AdjustAction::SetHue:
        Adjust::RangeOf(hsv).hue = std::clamp(value, hsv.colorize ? 0.0 : -180.0, hsv.colorize ? 360.0 : 180.0);
        break;
    case AdjustAction::SetSaturation:
        Adjust::RangeOf(hsv).saturation = std::clamp(value, hsv.colorize ? 0.0 : -100.0, 100.0);
        break;
    case AdjustAction::SetLightness:
        Adjust::RangeOf(hsv).lightness = std::clamp(value, -100.0, 100.0);
        break;
    case AdjustAction::SetColorize:
        // Photoshop starts colorizing at hue 0, saturation 25.
        hsv = value != 0.0 ? Adjust::ColorizeStart() : HueSaturationSettings{};
        break;
    case AdjustAction::SetInvertRange:
        hsv.invertRange = value != 0.0;
        break;
    case AdjustAction::SetHueSample:
        _session.SetHueSampleMode(value <= 0.0 ? std::nullopt
                                               : std::optional<HueSampleMode>(static_cast<HueSampleMode>(
                                                     std::clamp(static_cast<int>(value) - 1, 0, 2))));
        SyncChrome();
        return;
    case AdjustAction::SetTargeting:
        _session.SetHueTargeting(value != 0.0);
        _hueTargetStart.reset();
        SyncChrome();
        return;
    // Curves and the filters.
    case AdjustAction::RemovePoint:
    {
        const std::size_t index = static_cast<std::size_t>(std::max(0.0, value));
        if (index == 0 || index + 1 >= points.size())
        {
            return;
        }
        points.erase(points.begin() + static_cast<std::ptrdiff_t>(index));
        break;
    }
    case AdjustAction::SetRadius:
        settings.radius = value;
        break;
    case AdjustAction::SetAngle:
        settings.angle = value;
        break;
    case AdjustAction::SetDistance:
        settings.distance = value;
        break;
    case AdjustAction::SetAmount:
        settings.amount = value;
        break;
    case AdjustAction::SetGaussian:
        settings.gaussian = value != 0.0;
        break;
    case AdjustAction::SetMonochromatic:
        settings.monochromatic = value != 0.0;
        break;
    case AdjustAction::SetDistortion:
        settings.distortion = value;
        break;
    case AdjustAction::SetExposure:
        settings.exposure.exposure = value;
        break;
    case AdjustAction::SetOffset:
        settings.exposure.offset = value;
        break;
    case AdjustAction::SetGamma:
        settings.exposure.gamma = value;
        break;
    case AdjustAction::SetGrainAmount:
        settings.grain.amount = value;
        break;
    case AdjustAction::SetGrainSize:
        settings.grain.size = value;
        break;
    case AdjustAction::SetGrainRoughness:
        settings.grain.roughness = value;
        break;
    case AdjustAction::SetReversed:
        settings.gradientMap.reversed = value != 0.0;
        break;
    }
    // A range edit lands on the channel it was made on; choosing another channel only switches to it (legacy).
    if (levels && action != AdjustAction::SetChannel)
    {
        Adjust::SetLevelsCurrent(settings.levels, range);
    }
    _session.UpdateColorEdit(settings, preview);
    DocumentChanged();
}

void Application::OnCurveChanged(std::vector<CurvePoint> points) noexcept
{
    const std::optional<ColorEdit>& edit = _session.PendingColorEdit();
    if (!edit.has_value() || edit->kind != FilterKind::Curves)
    {
        return;
    }
    FilterSettings settings = edit->settings;
    settings.curves.channels[static_cast<std::size_t>(settings.curves.channel)] = std::move(points);
    if (!settings.curves.IsValid())
    {
        return;
    }
    _session.UpdateColorEdit(settings, edit->preview);
    DocumentChanged();
}

void Application::OnBandChanged(int index, double degrees) noexcept
{
    const std::optional<ColorEdit>& edit = _session.PendingColorEdit();
    if (!edit.has_value() || edit->kind != FilterKind::HueSaturation || !std::isfinite(degrees))
    {
        return;
    }
    FilterSettings settings = edit->settings;
    if (!Adjust::BandSetHandle(Adjust::BandOf(settings.hsv), index, degrees))
    {
        return;
    }
    _session.UpdateColorEdit(settings, edit->preview);
    DocumentChanged();
}

void Application::OnAdjustColor(ChromeEditPhase phase, uint32_t argb, bool highlights) noexcept
{
    const std::optional<ColorEdit>& edit = _session.PendingColorEdit();
    if (!edit.has_value() || edit->kind != FilterKind::GradientMap)
    {
        return;
    }
    FilterSettings settings = edit->settings;
    if (phase == ChromeEditPhase::Cancel)
    {
        // Cancel restores the end the picker was opened on; OK keeps it; the palette is left alone.
        settings.gradientMap = _adjustColorOriginal;
    }
    else if (highlights)
    {
        settings.gradientMap.highlights = ColorFromArgb(argb);
    }
    else
    {
        settings.gradientMap.shadows = ColorFromArgb(argb);
    }
    _session.UpdateColorEdit(settings, edit->preview);
    DocumentChanged();
}

void Application::SyncAdjustPanel() noexcept
{
    if (!_chrome)
    {
        return;
    }
    AdjustInspectorState state;
    state.editsAdjustment = _session.AdjustmentEditingID().has_value() && _session.AdjustmentOriginal().has_value();
    if (const std::optional<ColorEdit>& edit = _session.PendingColorEdit(); edit.has_value())
    {
        const std::wstring name = FilterKindName(edit->kind);
        state.title = state.editsAdjustment ? FormatStringResource(nullptr, IDS_ADJUST_EDIT_TITLE, name) : name;
        state.filter = edit->kind;
        state.settings = edit->settings;
        state.levels = edit->settings.levels;
        state.histogram = edit->histogram;
        state.histogramReady = edit->histogramReady;
        state.levelsSample = edit->levelsSample.has_value() ? static_cast<int>(edit->levelsSample.value()) + 1 : 0;
        state.hsv = edit->settings.hsv;
        state.hueSample = edit->hueSample.has_value() ? static_cast<int>(edit->hueSample.value()) + 1 : 0;
        state.targeting = edit->targeting;
        state.preview = edit->preview;
        state.limitedToSelection = edit->selection.has_value();
        state.previewFailed = edit->previewFailed;
    }
    _chrome->SetAdjustInspector(state);
}

bool Application::AdjustPointer(const DxUi::PointerEvent& event, Vec2 view, bool inHole) noexcept
{
    const std::optional<ColorEdit>& edit = _session.PendingColorEdit();
    if (!edit.has_value())
    {
        return false;
    }
    const Vec2 pixel = _viewport.DocumentFromView(view, DocumentSize());
    if (edit->kind == FilterKind::Levels)
    {
        if (!edit->levelsSample.has_value())
        {
            return false;
        }
        if (event.action == DxUi::PointerAction::Down && inHole)
        {
            _session.SampleLevels(pixel);
            DocumentChanged();
        }
        return true;
    }
    if (edit->kind != FilterKind::HueSaturation)
    {
        return false;
    }
    if (edit->hueSample.has_value())
    {
        if (event.action == DxUi::PointerAction::Down && inHole)
        {
            const std::optional<Raster> composite = WandComposite();
            if (composite.has_value())
            {
                _session.SampleHueRange(composite.value(), pixel);
                DocumentChanged();
            }
        }
        return true;
    }
    if (edit->targeting)
    {
        // Targeted adjustment: the press picks the range under the pointer, the drag changes its saturation
        // (its hue with Ctrl), measured from where it started.
        if (event.action == DxUi::PointerAction::Down && inHole)
        {
            const std::optional<Raster> composite = WandComposite();
            if (composite.has_value() && _session.BeginHueTargeting(composite.value(), pixel))
            {
                _hueTargetStart = view.x;
                DocumentChanged();
            }
            return true;
        }
        if (event.action == DxUi::PointerAction::Move && _hueTargetStart.has_value())
        {
            _session.DragHueTargeting(view.x - _hueTargetStart.value(), (event.modifiers & MK_CONTROL) != 0);
            DocumentChanged();
            return true;
        }
        if ((event.action == DxUi::PointerAction::Up || event.action == DxUi::PointerAction::Leave) &&
            _hueTargetStart.has_value())
        {
            _hueTargetStart.reset();
            _session.EndHueTargeting();
            return true;
        }
    }
    return false;
}

int Application::RunAdjustSelfTest() noexcept
{
    if (!_chrome || !_window)
    {
        SelfTestFailure(L"The adjust self-test needs the chrome and the window.\n");
        return 5;
    }
    const auto fail = [](const std::wstring& message) noexcept
    {
        SelfTestFailure(message.c_str());
        return 5;
    };
    // A user sees frames between inputs: the chrome lays out and renders, and DxUi takes keys only after that.
    const auto settle = [this]() noexcept
    {
        (void)_chrome->AdvanceAnimation(GetTickCount64());
        SyncChrome();
        (void)_renderer.Render(_chrome.get(), &_composition, &_viewport, _session.DisplayDocument());
    };
    const float scale = static_cast<float>(GetDpiForWindow(_window.get())) / 96.0f;
    const auto press = [&](float xDip, float yDip) noexcept
    {
        DxUi::PointerEvent pointer{};
        pointer.action = DxUi::PointerAction::Down;
        pointer.xPixels = xDip * scale;
        pointer.yPixels = yDip * scale;
        (void)_chrome->DispatchPointer(pointer);
        pointer.action = DxUi::PointerAction::Up;
        (void)_chrome->DispatchPointer(pointer);
    };
    // Whether the window's UI Automation root reaches a Button of that name through the chrome's tree.
    const auto reachable = [&](UINT nameId) noexcept
    {
        settle();
        IRawElementProviderFragmentRoot* root = _chrome->AccessibilityRoot();
        wil::com_ptr_nothrow<IRawElementProviderFragment> fragment;
        int budget = 4000;
        return root != nullptr && SUCCEEDED(root->QueryInterface(IID_PPV_ARGS(fragment.put()))) && fragment &&
               HasNamedButton(fragment.get(), LoadStringResource(nullptr, nameId), budget);
    };

    // A fresh document with one painted layer for the panels.
    _forcedDiscard = true;
    OnCommand(ChromeHost::kCommandNew);
    _forcedDiscard.reset();
    {
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(64 * 64 * 4), 0);
        for (std::size_t index = 0; index + 3 < bytes.size(); index += 4)
        {
            bytes[index] = 0x2E;
            bytes[index + 1] = 0x8A;
            bytes[index + 2] = 0xE5;
            bytes[index + 3] = 0xFF;
        }
        const Raster raster = Raster::Make(64, 64, RasterFormat::Bgra8888, bytes.data(), bytes.size());
        ImageLayer layer = ImageLayer::Blank(L"Swatch", Vec2{64, 64});
        layer.asset = RasterAsset{raster, Raster::MakeThumbnail(raster), layer.name};
        layer.transform.origin = Vec2{20, 20};
        _session.BeginEdit(L"Import Image");
        _session.Document().value().layers.push_back(layer);
        _session.EndEdit();
        _session.SelectLayer(layer.id);
        DocumentChanged();
    }
    OnTool(EditorTool::Move);

    // A typed decimal stays as typed while it previews (RF-501): "2.5" into Gaussian Blur's radius shows "2", "2."
    // and "2.5", never a reformatted "2.0", and the preview follows it.
    {
        _session.filterSettings.radius = 1.0;
        if (!BeginColorEdit(ChromeHost::kCommandGaussianBlur))
        {
            return fail(L"Gaussian Blur did not open for the typed-decimal check.\n");
        }
        settle();
        if (!_chrome->FocusAdjustRadiusForTest())
        {
            CancelColorEdit();
            return fail(L"The Gaussian Blur radius field could not take the focus.\n");
        }
        // Each key is followed by the frame its preview asks for, which syncs the panel from the session.
        std::array<std::wstring, 3> shown;
        const std::array<wchar_t, 3> typed{L'2', L'.', L'5'};
        for (std::size_t index = 0; index < typed.size(); ++index)
        {
            (void)_chrome->DispatchCharacter(typed[index], 0);
            settle();
            shown[index] = _chrome->AdjustRadiusTextForTest();
        }
        const bool previewed = _session.PendingColorEdit().has_value() &&
                               std::fabs(_session.PendingColorEdit()->settings.radius - 2.5) < 1e-9;
        CancelColorEdit();
        _chrome->SetCanvasFocused(true);
        if (shown[0] != L"2" || shown[1] != L"2." || shown[2] != L"2.5" || !previewed)
        {
            return fail(std::format(L"Typing 2.5 into the Gaussian Blur radius was reformatted ('{}', '{}', '{}', "
                                    L"previewed {}).\n",
                                    shown[0], shown[1], shown[2], previewed));
        }
    }

    // The Gradient Map's color picker closes with its panel, and the next pick from a palette well reaches the
    // palette (RF-503).
    {
        if (!BeginColorEdit(ChromeHost::kCommandGradientMap))
        {
            return fail(L"Gradient Map did not open for the picker check.\n");
        }
        settle();
        OnAdjustAction(AdjustAction::OpenShadows, 0.0);
        const bool pickerOpen = _chrome->ColorPickerOpen();
        CancelColorEdit();
        settle();
        const bool closedWithPanel = !_chrome->ColorPickerOpen() && !_chrome->AdjustPanelOpen();
        const std::uint32_t foreground = _session.ForegroundColor();
        const D2D1_RECT_F well = _chrome->PaletteWellBoundsDip(false);
        press((well.left + well.right) * 0.5f, (well.top + well.bottom) * 0.5f);
        const bool wellOpened = _chrome->ColorPickerOpen();
        _chrome->SampleColor(0xFF123456u);
        _chrome->CloseColorPicker(true);
        settle();
        const bool reached = _session.ForegroundColor() == 0xFF123456u;
        _session.SetForegroundColor(foreground);
        _chrome->SetCanvasFocused(true);
        settle();
        if (!pickerOpen || !closedWithPanel || !wellOpened || !reached)
        {
            return fail(std::format(L"The Gradient Map picker outlived its panel or took the next palette pick (open "
                                    L"{}, closed with the panel {}, well opened {}, palette reached {}).\n",
                                    pickerOpen, closedWithPanel, wellOpened, reached));
        }
    }

    // The drawn controls are elements of the window's UI Automation tree (RF-507): the palette's zones always, the
    // curve editor, the levels bars and the hue spectrum while their panel shows them.
    {
        const bool palette = reachable(IDS_PALETTE_FOREGROUND) && reachable(IDS_PALETTE_BACKGROUND) &&
                             reachable(IDS_PALETTE_SWAP) && reachable(IDS_PALETTE_RESET);
        const bool curves = BeginColorEdit(ChromeHost::kCommandCurves) && reachable(IDS_A11Y_CURVE_EDITOR);
        CancelColorEdit();
        const bool levels = BeginColorEdit(ChromeHost::kCommandLevels) && reachable(IDS_A11Y_LEVELS_INPUT) &&
                            reachable(IDS_A11Y_LEVELS_OUTPUT);
        CancelColorEdit();
        const bool opened = BeginColorEdit(ChromeHost::kCommandHueSaturation);
        OnAdjustAction(AdjustAction::SetRange, 1.0);
        const bool spectrum = opened && reachable(IDS_A11Y_HUE_SPECTRUM);
        CancelColorEdit();
        settle();
        if (!palette || !curves || !levels || !spectrum)
        {
            return fail(std::format(L"A drawn control was not in the UI Automation tree (palette zones {}, curve "
                                    L"editor {}, levels bars {}, hue spectrum {}).\n",
                                    palette, curves, levels, spectrum));
        }
    }

    // A press on the spectrum's hue bars moves no handle; a press in the handle strip between them drags the nearest
    // handle (RF-511, legacy HueSaturationSheet).
    {
        if (!BeginColorEdit(ChromeHost::kCommandHueSaturation))
        {
            return fail(L"Hue/Saturation did not open for the spectrum check.\n");
        }
        OnAdjustAction(AdjustAction::SetRange, 1.0);
        settle();
        const D2D1_RECT_F bounds = _chrome->SpectrumBoundsDipForTest();
        std::optional<float> stripTop;
        std::optional<float> stripBottom;
        for (float y = bounds.top; y < bounds.bottom; y += 0.25f)
        {
            if (SpectrumEditor::HandleStripContainsDip(bounds, D2D1::Point2F(bounds.left + 1.0f, y)))
            {
                stripTop = stripTop.value_or(y);
                stripBottom = y;
            }
        }
        const auto hueSettings = [this]() -> std::optional<HueSaturationSettings>
        {
            const std::optional<ColorEdit>& edit = _session.PendingColorEdit();
            return edit.has_value() && edit->kind == FilterKind::HueSaturation
                       ? std::optional<HueSaturationSettings>(edit->settings.hsv)
                       : std::nullopt;
        };
        const HueSaturationSettings before = hueSettings().value_or(HueSaturationSettings{});
        // Just right of the band's range start: that handle is the nearest, and the place is a valid one for it.
        const double degrees = Adjust::BandOf(before).Handles()[1] + 3.0;
        const float x = bounds.left + (bounds.right - bounds.left) * static_cast<float>(degrees / 360.0);
        const float barY = bounds.top + 1.0f;
        const bool onBar = !SpectrumEditor::HandleStripContainsDip(bounds, D2D1::Point2F(x, barY));
        press(x, barY);
        const bool barIgnored = hueSettings() == std::optional<HueSaturationSettings>(before);
        bool stripMoved = false;
        if (stripTop.has_value() && stripBottom.has_value())
        {
            press(x, (stripTop.value() + stripBottom.value()) * 0.5f);
            stripMoved = hueSettings().has_value() && !(hueSettings().value() == before);
        }
        CancelColorEdit();
        _chrome->SetCanvasFocused(true);
        settle();
        if (bounds.right <= bounds.left || !stripTop.has_value() || !onBar || !barIgnored || !stripMoved)
        {
            return fail(std::format(L"The spectrum's press rules broke (bounds {:.1f}-{:.1f}, strip {}, a bar press "
                                    L"ignored {}, a strip press moved a handle {}).\n",
                                    bounds.left, bounds.right, stripTop.has_value(), barIgnored, stripMoved));
        }
    }
    OnTool(EditorTool::Move);
    return 0;
}
