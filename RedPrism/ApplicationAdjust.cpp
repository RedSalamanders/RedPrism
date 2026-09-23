#include "Application.h"

#include "Adjust.h"
#include "ChromeHost.h"
#include "Resources.h"
#include "resource.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>

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
} // namespace

bool Application::BeginColorEdit(int command) noexcept
{
    switch (command)
    {
    case ChromeHost::kCommandLevels:
        _session.BeginLevels();
        break;
    case ChromeHost::kCommandHueSaturation:
        _session.BeginHueSaturation();
        break;
    case ChromeHost::kCommandCurves:
        _session.BeginFilter(FilterKind::Curves);
        break;
    case ChromeHost::kCommandExposure:
        _session.BeginFilter(FilterKind::Exposure);
        break;
    case ChromeHost::kCommandGradientMap:
        _session.BeginFilter(FilterKind::GradientMap);
        break;
    case ChromeHost::kCommandGrain:
        _session.BeginFilter(FilterKind::Grain);
        break;
    case ChromeHost::kCommandGaussianBlur:
        _session.BeginFilter(FilterKind::GaussianBlur);
        break;
    case ChromeHost::kCommandMotionBlur:
        _session.BeginFilter(FilterKind::MotionBlur);
        break;
    case ChromeHost::kCommandAddNoise:
        _session.BeginFilter(FilterKind::AddNoise);
        break;
    case ChromeHost::kCommandLensCorrection:
        _session.BeginFilter(FilterKind::LensCorrection);
        break;
    case ChromeHost::kCommandContentAwareFill:
        _session.BeginFilter(FilterKind::ContentAwareFill);
        break;
    default:
        return false;
    }
    if (!_session.HasColorEdit())
    {
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
    BindCompositionDevice();
    // A full-resolution readback of `copy` (tiled past the texture limit, never the display proxy or a preview).
    std::optional<Raster> result;
    std::vector<std::uint8_t> bgra;
    UINT width = 0;
    UINT height = 0;
    if (SUCCEEDED(_composition.ComposeReadback(copy, bgra, width, height)) && width > 0 && height > 0)
    {
        const Raster raster = Raster::Make(static_cast<int>(width), static_cast<int>(height), RasterFormat::Bgra8888,
                                           bgra.data(), bgra.size());
        if (raster.HasStorage())
        {
            result = raster;
        }
    }
    return result;
}

void Application::ApplyColorEdit() noexcept
{
    if (!_session.HasColorEdit())
    {
        return;
    }
    _hueTargetStart.reset();
    if (_session.PendingLevels().has_value())
    {
        _session.CommitLevels();
    }
    else if (_session.PendingHueSaturation().has_value())
    {
        _session.CommitHueSaturation();
    }
    else
    {
        _session.CommitFilter();
    }
    DocumentChanged();
}

void Application::CancelColorEdit() noexcept
{
    if (!_session.HasColorEdit())
    {
        return;
    }
    _hueTargetStart.reset();
    if (_session.PendingLevels().has_value())
    {
        _session.CancelLevels();
    }
    else if (_session.PendingHueSaturation().has_value())
    {
        _session.CancelHueSaturation();
    }
    else
    {
        _session.CancelFilter();
    }
    DocumentChanged();
}

void Application::OnAdjustAction(AdjustAction action, double value) noexcept
{
    if (!std::isfinite(value))
    {
        return;
    }
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
        if (_session.PendingFilter().has_value() && _session.PendingFilter()->kind == FilterKind::GradientMap &&
            _chrome)
        {
            const GradientMapSettings& map = _session.PendingFilter()->settings.gradientMap;
            _adjustColorOriginal = map;
            const bool highlights = action == AdjustAction::OpenHighlights;
            _chrome->OpenAdjustColorPicker(ArgbOf(highlights ? map.highlights : map.shadows), highlights);
        }
        return;
    default:
        break;
    }
    if (const std::optional<LevelsEdit>& levels = _session.PendingLevels(); levels.has_value())
    {
        LevelsSettings settings = levels->settings;
        bool preview = levels->preview;
        LevelRange range = Adjust::LevelsCurrent(settings);
        switch (action)
        {
        case AdjustAction::SetPreview:
            preview = value != 0.0;
            break;
        case AdjustAction::SetChannel:
            settings.channel = static_cast<LevelsChannel>(std::clamp(static_cast<int>(value), 0, 3));
            break;
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
        case AdjustAction::Reset:
            settings = LevelsSettings{};
            range = LevelRange{};
            _session.SetLevelsSampleMode(std::nullopt);
            break;
        default:
            return;
        }
        Adjust::SetLevelsCurrent(settings, range);
        _session.UpdateLevels(settings, preview);
        DocumentChanged();
        return;
    }
    if (const std::optional<HueSaturationEdit>& hsv = _session.PendingHueSaturation(); hsv.has_value())
    {
        HueSaturationSettings settings = hsv->settings;
        bool preview = hsv->preview;
        switch (action)
        {
        case AdjustAction::SetPreview:
            preview = value != 0.0;
            break;
        case AdjustAction::SetRange:
            settings.range = static_cast<ColorRange>(std::clamp(static_cast<int>(value), 0, 6));
            break;
        case AdjustAction::SetHue:
            Adjust::RangeOf(settings).hue =
                std::clamp(value, settings.colorize ? 0.0 : -180.0, settings.colorize ? 360.0 : 180.0);
            break;
        case AdjustAction::SetSaturation:
            Adjust::RangeOf(settings).saturation = std::clamp(value, settings.colorize ? 0.0 : -100.0, 100.0);
            break;
        case AdjustAction::SetLightness:
            Adjust::RangeOf(settings).lightness = std::clamp(value, -100.0, 100.0);
            break;
        case AdjustAction::SetColorize:
            // Photoshop starts colorizing at hue 0, saturation 25.
            settings = value != 0.0 ? Adjust::ColorizeStart() : HueSaturationSettings{};
            break;
        case AdjustAction::SetInvertRange:
            settings.invertRange = value != 0.0;
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
        case AdjustAction::Reset:
            settings = settings.colorize ? Adjust::ColorizeStart() : HueSaturationSettings{};
            break;
        default:
            return;
        }
        _session.UpdateHueSaturation(settings, preview);
        DocumentChanged();
        return;
    }
    if (const std::optional<FilterEdit>& filter = _session.PendingFilter(); filter.has_value())
    {
        FilterSettings settings = filter->settings;
        bool preview = filter->preview;
        std::vector<CurvePoint>& points = settings.curves.channels[static_cast<std::size_t>(settings.curves.channel)];
        switch (action)
        {
        case AdjustAction::SetPreview:
            preview = value != 0.0;
            break;
        case AdjustAction::SetChannel:
            settings.curves.channel = static_cast<LevelsChannel>(std::clamp(static_cast<int>(value), 0, 3));
            break;
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
        case AdjustAction::Reset:
            points = {CurvePoint{0, 0}, CurvePoint{255, 255}};
            break;
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
        default:
            return;
        }
        _session.UpdateFilter(settings, preview);
        DocumentChanged();
    }
}

void Application::OnCurveChanged(std::vector<CurvePoint> points) noexcept
{
    const std::optional<FilterEdit>& filter = _session.PendingFilter();
    if (!filter.has_value() || filter->kind != FilterKind::Curves)
    {
        return;
    }
    FilterSettings settings = filter->settings;
    settings.curves.channels[static_cast<std::size_t>(settings.curves.channel)] = std::move(points);
    if (!settings.curves.IsValid())
    {
        return;
    }
    _session.UpdateFilter(settings, filter->preview);
    DocumentChanged();
}

void Application::OnBandChanged(int index, double degrees) noexcept
{
    const std::optional<HueSaturationEdit>& hsv = _session.PendingHueSaturation();
    if (!hsv.has_value() || !std::isfinite(degrees))
    {
        return;
    }
    HueSaturationSettings settings = hsv->settings;
    if (!Adjust::BandSetHandle(Adjust::BandOf(settings), index, degrees))
    {
        return;
    }
    _session.UpdateHueSaturation(settings, hsv->preview);
    DocumentChanged();
}

void Application::OnAdjustColor(ChromeEditPhase phase, uint32_t argb, bool highlights) noexcept
{
    const std::optional<FilterEdit>& filter = _session.PendingFilter();
    if (!filter.has_value() || filter->kind != FilterKind::GradientMap)
    {
        return;
    }
    FilterSettings settings = filter->settings;
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
    _session.UpdateFilter(settings, filter->preview);
    DocumentChanged();
}

void Application::SyncAdjustPanel() noexcept
{
    if (!_chrome)
    {
        return;
    }
    AdjustInspectorState state;
    const bool editsAdjustment =
        _session.AdjustmentEditingID().has_value() && _session.AdjustmentOriginal().has_value();
    const auto title = [&](const wchar_t* name)
    {
        return editsAdjustment ? FormatStringResource(nullptr, IDS_ADJUST_EDIT_TITLE, std::wstring(name))
                               : std::wstring(name);
    };
    if (const std::optional<LevelsEdit>& levels = _session.PendingLevels(); levels.has_value())
    {
        state.panel = AdjustPanelKind::Levels;
        state.title = title(L"Levels");
        state.levels = levels->settings;
        state.histogram = levels->histogram;
        state.histogramReady = levels->histogramReady;
        state.levelsSample = levels->sampleMode.has_value() ? static_cast<int>(levels->sampleMode.value()) + 1 : 0;
        state.preview = levels->preview;
        state.limitedToSelection = levels->selection.has_value();
    }
    else if (const std::optional<HueSaturationEdit>& hsv = _session.PendingHueSaturation(); hsv.has_value())
    {
        state.panel = AdjustPanelKind::HueSaturation;
        state.title = title(L"Hue/Saturation");
        state.hsv = hsv->settings;
        state.hueSample = hsv->sampleMode.has_value() ? static_cast<int>(hsv->sampleMode.value()) + 1 : 0;
        state.targeting = hsv->targeting;
        state.preview = hsv->preview;
        state.limitedToSelection = hsv->selection.has_value();
    }
    else if (const std::optional<FilterEdit>& filter = _session.PendingFilter(); filter.has_value())
    {
        state.panel = AdjustPanelKind::Filter;
        state.filter = filter->kind;
        state.title = title(FilterKindName(filter->kind));
        state.settings = filter->settings;
        state.preview = filter->preview;
        state.limitedToSelection = filter->selection.has_value();
        state.previewFailed = filter->previewFailed;
    }
    state.editsAdjustment = editsAdjustment;
    _chrome->SetAdjustInspector(state);
}

bool Application::AdjustPointer(const DxUi::PointerEvent& event, Vec2 view, bool inHole) noexcept
{
    if (!_session.HasColorEdit())
    {
        return false;
    }
    const Vec2 pixel = _viewport.DocumentFromView(view, DocumentSize());
    if (const std::optional<LevelsEdit>& levels = _session.PendingLevels(); levels.has_value())
    {
        if (!levels->sampleMode.has_value())
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
    const std::optional<HueSaturationEdit>& hsv = _session.PendingHueSaturation();
    if (!hsv.has_value())
    {
        return false;
    }
    if (hsv->sampleMode.has_value())
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
    if (hsv->targeting)
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
