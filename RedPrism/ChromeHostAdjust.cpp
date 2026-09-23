#include "ChromeHost.h"

#include "Resources.h"
#include "resource.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <span>

// The adjustment panel (C12, Specs/UI/UI_ApplicationChrome.md): a card over the canvas hole while a Levels,
// Hue/Saturation or filter edit is open. Every control mirrors the session through SetAdjustInspector and reports
// changes as ChromeEvents; nothing here re-lays out inside a drag callback.

namespace
{
constexpr float kCardWidthDip = 384.0f;
constexpr float kCardPaddingDip = 12.0f;
constexpr float kRowDip = 28.0f;
constexpr float kRowGapDip = 6.0f;
constexpr float kLabelWidthDip = 96.0f;
constexpr float kStepperWidthDip = 118.0f;
constexpr float kCurveDip = 232.0f;
constexpr float kHistogramDip = 132.0f;
constexpr float kOutputRampDip = 14.0f;
constexpr float kSpectrumDip = 54.0f;
constexpr float kRampDip = 20.0f;
constexpr float kNoteDip = 20.0f;
constexpr float kMarginDip = 8.0f;

[[nodiscard]] double RoundTo(double value, int decimals) noexcept
{
    const double step = std::pow(10.0, decimals);
    return std::round(value * step) / step;
}

[[nodiscard]] uint32_t ArgbOf(const AdjustmentColor& color) noexcept
{
    const auto byte = [](double value)
    { return static_cast<uint32_t>(std::lround(std::clamp(value, 0.0, 1.0) * 255)); };
    return 0xFF000000u | (byte(color.red) << 16) | (byte(color.green) << 8) | byte(color.blue);
}
} // namespace

bool ChromeHost::AdjustPanelOpen() const noexcept
{
    return _adjustCard != nullptr && _adjustCard->IsVisible();
}

bool ChromeHost::FocusAdjustRadiusForTest() noexcept
{
    DxUi::NumericStepper* stepper = _adjustRows[static_cast<std::size_t>(AdjustRowId::Radius)].stepper;
    if (!_attached || !AdjustPanelOpen() || stepper == nullptr || !stepper->IsVisible())
    {
        return false;
    }
    DxUi::TextField& field = stepper->Field();
    _canvasFocused = false;
    _accessibilityPlacementDirty = true;
    _view.Controls().SetFocusControl(&field);
    field.SetSelectionRange(0, field.GetText().size());
    return true;
}

std::wstring ChromeHost::AdjustRadiusTextForTest() const
{
    const DxUi::NumericStepper* stepper = _adjustRows[static_cast<std::size_t>(AdjustRowId::Radius)].stepper;
    return stepper != nullptr ? std::wstring(stepper->Field().GetText()) : std::wstring{};
}

D2D1_RECT_F ChromeHost::SpectrumBoundsDipForTest() const noexcept
{
    return AdjustPanelOpen() && _spectrum != nullptr && _spectrum->IsVisible() ? _spectrum->GetBounds() : D2D1::RectF();
}

bool ChromeHost::FilterOptionsVisible() const noexcept
{
    if (!AdjustPanelOpen() || !_adjustInspector.filter.has_value() || _adjustInspector.filter == FilterKind::Levels ||
        _adjustInspector.filter == FilterKind::HueSaturation || _adjustCard == nullptr)
    {
        return false;
    }
    DxUi::Control* option = nullptr;
    switch (*_adjustInspector.filter)
    {
    case FilterKind::GaussianBlur:
        option = _adjustRows[static_cast<std::size_t>(AdjustRowId::Radius)].slider;
        break;
    case FilterKind::MotionBlur:
        option = _adjustRows[static_cast<std::size_t>(AdjustRowId::Angle)].slider;
        break;
    case FilterKind::AddNoise:
        option = _adjustRows[static_cast<std::size_t>(AdjustRowId::Amount)].slider;
        break;
    case FilterKind::LensCorrection:
        option = _adjustRows[static_cast<std::size_t>(AdjustRowId::Distortion)].slider;
        break;
    case FilterKind::Exposure:
        option = _adjustRows[static_cast<std::size_t>(AdjustRowId::Exposure)].slider;
        break;
    case FilterKind::Grain:
        option = _adjustRows[static_cast<std::size_t>(AdjustRowId::GrainAmount)].slider;
        break;
    case FilterKind::Curves:
        option = _curveEditor;
        break;
    case FilterKind::GradientMap:
        option = _gradientRamp;
        break;
    case FilterKind::ContentAwareFill:
        option = _fillHint;
        break;
    }
    if (option == nullptr || !option->IsVisible())
    {
        return false;
    }
    const D2D1_RECT_F bounds = option->GetBounds();
    const D2D1_RECT_F card = _adjustCard->GetBounds();
    return bounds.bottom > bounds.top + 1.0f && bounds.right > bounds.left + 1.0f && bounds.top >= card.top - 0.5f &&
           bounds.bottom <= card.bottom + 0.5f;
}

void ChromeHost::BuildAdjustPanel(DxUi::Panel& root)
{
    _adjustCard = root.AddChild<DxUi::CardPanel>();
    _adjustCard->SetCornerRadius(8.0f);
    _adjustCard->SetVisible(false);
    DxUi::Panel& card = *_adjustCard;
    _adjustTitle = card.AddChild<DxUi::Label>(std::wstring{});
    _adjustTitle->SetFontRole(DxUi::FontRole::BodyStrong);
    // A value row: a label, a slider (logarithmic ones give the small values most of the travel) and an exact stepper.
    const auto row = [&](AdjustRowId id, UINT labelId, UINT unitId, double minimum, double maximum, int decimals,
                         bool logarithmic, AdjustAction what)
    {
        AdjustRow& entry = _adjustRows[static_cast<std::size_t>(id)];
        entry.label = card.AddChild<DxUi::Label>(LoadStringResource(nullptr, labelId));
        entry.slider = card.AddChild<DxUi::Slider>();
        entry.slider->SetAccessibleName(LoadStringResource(nullptr, labelId));
        entry.slider->SetMinimum(logarithmic ? std::log(minimum) : minimum);
        entry.slider->SetMaximum(logarithmic ? std::log(maximum) : maximum);
        entry.slider->SetStep(logarithmic ? 0.01 : std::pow(10.0, -decimals));
        entry.slider->SetLargeStep(logarithmic ? 0.1 : (maximum - minimum) / 10);
        entry.slider->SetOnValueChanged(
            [this, id](double value)
            {
                const AdjustRow& changed = _adjustRows[static_cast<std::size_t>(id)];
                const double real = changed.logarithmic ? std::exp(value) : value;
                Report(changed.action, RoundTo(std::clamp(real, changed.minimum, changed.maximum), changed.decimals));
            });
        entry.stepper = MakeStepper(card,
                                    {.labelId = labelId,
                                     .unitId = unitId,
                                     .unitWidth = 14.0f,
                                     .minimum = minimum,
                                     .maximum = maximum,
                                     .step = std::pow(10.0, -decimals),
                                     .decimals = static_cast<std::uint8_t>(decimals)},
                                    what);
        entry.action = what;
        entry.minimum = minimum;
        entry.maximum = maximum;
        entry.logarithmic = logarithmic;
        entry.decimals = decimals;
    };
    // Curves and Levels share the channel choice.
    _adjustChannel = MakeCombo(card, NumberedItems({IDS_ADJUST_RGB, IDS_ADJUST_RED, IDS_ADJUST_GREEN, IDS_ADJUST_BLUE}),
                               {.nameId = IDS_ADJUST_CHANNEL}, AdjustAction::SetChannel);
    _curveEditor = card.AddChild<CurveEditor>();
    _curveEditor->SetAccessibleName(LoadStringResource(nullptr, IDS_A11Y_CURVE_EDITOR));
    _curveEditor->SetOnChange(
        [this](std::vector<CurvePoint> points)
        {
            if (!_syncing)
            {
                _resync |= kResyncAdjust; // the editor shows the user's curve: the next push writes it back
                _events.OnCurveChanged(std::move(points));
            }
        });
    _curveCaption = card.AddChild<DxUi::Label>(LoadStringResource(nullptr, IDS_ADJUST_CURVE_HINT));
    _curveRemove = MakeButton(card, {.labelId = IDS_ADJUST_REMOVE_POINT},
                              [this]
                              {
                                  const std::optional<std::size_t> selected =
                                      _curveEditor ? _curveEditor->SelectedIndex() : std::nullopt;
                                  if (selected.has_value())
                                  {
                                      Report(AdjustAction::RemovePoint, static_cast<double>(selected.value()));
                                  }
                              });
    _curveReset = MakeButton(card, {.labelId = IDS_ADJUST_RESET}, AdjustAction::Reset);
    // Levels.
    _levelsInput = card.AddChild<LevelsBar>();
    _levelsInput->SetAccessibleName(LoadStringResource(nullptr, IDS_A11Y_LEVELS_INPUT));
    _levelsInput->SetKind(LevelsBar::Kind::Input);
    _levelsInput->SetOnHandle(
        [this](int handle, double value)
        {
            if (_syncing)
            {
                return;
            }
            _resync |= kResyncAdjust;
            const LevelRange& range = Adjust::LevelsCurrent(_adjustInspector.levels);
            if (handle == 0)
            {
                _events.OnAdjustAction(AdjustAction::SetInputBlack, std::min(range.white - 1, std::round(value)));
            }
            else if (handle == 2)
            {
                _events.OnAdjustAction(AdjustAction::SetInputWhite, std::max(range.black + 1, std::round(value)));
            }
            else
            {
                const double fraction =
                    std::min(0.999, std::max(0.001, (value - range.black) / (range.white - range.black)));
                _events.OnAdjustAction(AdjustAction::SetInputGamma, std::log(fraction) / std::log(0.5));
            }
        });
    _levelsOutput = card.AddChild<LevelsBar>();
    _levelsOutput->SetAccessibleName(LoadStringResource(nullptr, IDS_A11Y_LEVELS_OUTPUT));
    _levelsOutput->SetKind(LevelsBar::Kind::Output);
    _levelsOutput->SetOnHandle(
        [this](int handle, double value)
        { Report(handle == 0 ? AdjustAction::SetOutputBlack : AdjustAction::SetOutputWhite, std::round(value)); });
    _levelsBlack = MakeStepper(card, {.labelId = IDS_ADJUST_INPUT_BLACK, .minimum = 0, .maximum = 254},
                               AdjustAction::SetInputBlack);
    _levelsGamma =
        MakeStepper(card, {.labelId = IDS_ADJUST_GAMMA, .minimum = 0.1, .maximum = 9.99, .step = 0.01, .decimals = 2},
                    AdjustAction::SetInputGamma);
    _levelsWhite = MakeStepper(card, {.labelId = IDS_ADJUST_INPUT_WHITE, .minimum = 1, .maximum = 255},
                               AdjustAction::SetInputWhite);
    _levelsOutBlack = MakeStepper(card, {.labelId = IDS_ADJUST_OUTPUT_BLACK, .minimum = 0, .maximum = 255},
                                  AdjustAction::SetOutputBlack);
    _levelsOutWhite = MakeStepper(card, {.labelId = IDS_ADJUST_OUTPUT_WHITE, .minimum = 0, .maximum = 255},
                                  AdjustAction::SetOutputWhite);
    // The eyedroppers: pressing the armed one again stops sampling.
    const UINT sampleIds[] = {IDS_ADJUST_SAMPLE_BLACK, IDS_ADJUST_SAMPLE_GRAY, IDS_ADJUST_SAMPLE_WHITE};
    for (int index = 0; index < 3; ++index)
    {
        _levelsSample[static_cast<std::size_t>(index)] = MakeButton(
            card, {.labelId = sampleIds[index], .tipId = IDS_ADJUST_SAMPLE_TIP}, [this, index]
            { Report(AdjustAction::SetLevelsSample, _adjustInspector.levelsSample == index + 1 ? 0.0 : index + 1.0); });
    }
    const UINT autoIds[] = {IDS_ADJUST_AUTO_CONTRAST, IDS_ADJUST_AUTO_COLOR, IDS_ADJUST_AUTO_NEUTRAL};
    for (int index = 0; index < 3; ++index)
    {
        _levelsAuto[static_cast<std::size_t>(index)] =
            MakeButton(card, {.labelId = autoIds[index]},
                       [this, index] { Report(AdjustAction::AutoLevels, static_cast<double>(index)); });
    }
    // Hue / Saturation.
    _hueRange = MakeCombo(card,
                          NumberedItems({IDS_ADJUST_MASTER, IDS_ADJUST_REDS, IDS_ADJUST_YELLOWS, IDS_ADJUST_GREENS,
                                         IDS_ADJUST_CYANS, IDS_ADJUST_BLUES, IDS_ADJUST_MAGENTAS}),
                          {.nameId = IDS_ADJUST_RANGE}, AdjustAction::SetRange);
    const UINT hueSampleIds[] = {IDS_ADJUST_HUE_SAMPLE, IDS_ADJUST_HUE_ADD, IDS_ADJUST_HUE_REMOVE};
    for (int index = 0; index < 3; ++index)
    {
        _hueSample[static_cast<std::size_t>(index)] = MakeButton(
            card, {.labelId = hueSampleIds[index], .tipId = IDS_ADJUST_HUE_SAMPLE_TIP}, [this, index]
            { Report(AdjustAction::SetHueSample, _adjustInspector.hueSample == index + 1 ? 0.0 : index + 1.0); });
    }
    _hueTargeting = MakeButton(card, {.labelId = IDS_ADJUST_TARGETED, .tipId = IDS_ADJUST_TARGETED_TIP},
                               [this] { Report(AdjustAction::SetTargeting, _adjustInspector.targeting ? 0.0 : 1.0); });
    row(AdjustRowId::Hue, IDS_ADJUST_HUE, IDS_UNIT_DEGREES, -180, 180, 0, false, AdjustAction::SetHue);
    row(AdjustRowId::Saturation, IDS_ADJUST_SATURATION, 0, -100, 100, 0, false, AdjustAction::SetSaturation);
    row(AdjustRowId::Lightness, IDS_ADJUST_LIGHTNESS, 0, -100, 100, 0, false, AdjustAction::SetLightness);
    _spectrum = card.AddChild<SpectrumEditor>();
    _spectrum->SetAccessibleName(LoadStringResource(nullptr, IDS_A11Y_HUE_SPECTRUM));
    _spectrum->SetOnHandle(
        [this](int index, double degrees)
        {
            if (!_syncing)
            {
                _resync |= kResyncAdjust;
                _events.OnBandChanged(index, degrees);
            }
        });
    _hueInvert = MakeCheck(card, {.labelId = IDS_ADJUST_INVERT_RANGE}, AdjustAction::SetInvertRange);
    _hueColorize = MakeCheck(card, {.labelId = IDS_ADJUST_COLORIZE}, AdjustAction::SetColorize);
    // Exposure, Gradient Map, Grain.
    row(AdjustRowId::Exposure, IDS_ADJUST_EXPOSURE, 0, -20, 20, 2, false, AdjustAction::SetExposure);
    row(AdjustRowId::Offset, IDS_ADJUST_OFFSET, 0, -0.5, 0.5, 4, false, AdjustAction::SetOffset);
    row(AdjustRowId::Gamma, IDS_ADJUST_GAMMA, 0, 0.01, 9.99, 2, true, AdjustAction::SetGamma);
    _gradientRamp = card.AddChild<GradientRamp>();
    _gradientRamp->SetAccessibleName(LoadStringResource(nullptr, IDS_A11Y_GRADIENT_RAMP));
    _mapShadows = card.AddChild<DxUi::ColorSwatch>(0xFF000000u);
    _mapShadows->SetAccessibleName(LoadStringResource(nullptr, IDS_ADJUST_SHADOWS));
    _mapShadows->SetOnClick([this] { Report(AdjustAction::OpenShadows, 0.0); });
    _mapShadowsLabel = card.AddChild<DxUi::Label>(LoadStringResource(nullptr, IDS_ADJUST_SHADOWS));
    _mapHighlights = card.AddChild<DxUi::ColorSwatch>(0xFFFFFFFFu);
    _mapHighlights->SetAccessibleName(LoadStringResource(nullptr, IDS_ADJUST_HIGHLIGHTS));
    _mapHighlights->SetOnClick([this] { Report(AdjustAction::OpenHighlights, 0.0); });
    _mapHighlightsLabel = card.AddChild<DxUi::Label>(LoadStringResource(nullptr, IDS_ADJUST_HIGHLIGHTS));
    _mapReverse = MakeCheck(card, {.labelId = IDS_ADJUST_REVERSE}, AdjustAction::SetReversed);
    row(AdjustRowId::GrainAmount, IDS_ADJUST_AMOUNT, 0, 0, 100, 0, false, AdjustAction::SetGrainAmount);
    row(AdjustRowId::GrainSize, IDS_ADJUST_SIZE, IDS_UNIT_PIXELS, 0.5, 20, 1, true, AdjustAction::SetGrainSize);
    row(AdjustRowId::GrainRoughness, IDS_ADJUST_ROUGHNESS, 0, 0, 100, 0, false, AdjustAction::SetGrainRoughness);
    // Blurs, noise and the lens.
    row(AdjustRowId::Radius, IDS_ADJUST_RADIUS, IDS_UNIT_PIXELS, 0.1, 250, 1, true, AdjustAction::SetRadius);
    row(AdjustRowId::Angle, IDS_ADJUST_ANGLE, IDS_UNIT_DEGREES, -90, 90, 0, false, AdjustAction::SetAngle);
    row(AdjustRowId::Distance, IDS_ADJUST_DISTANCE, IDS_UNIT_PIXELS, 1, 2000, 0, true, AdjustAction::SetDistance);
    row(AdjustRowId::Amount, IDS_ADJUST_AMOUNT, IDS_UNIT_PERCENT, 0.1, 400, 1, true, AdjustAction::SetAmount);
    _noiseDistribution = MakeCombo(card, NumberedItems({IDS_ADJUST_UNIFORM, IDS_ADJUST_GAUSSIAN}),
                                   {.nameId = IDS_A11Y_NOISE_DISTRIBUTION}, AdjustAction::SetGaussian);
    _noiseMono = MakeCheck(card, {.labelId = IDS_ADJUST_MONOCHROMATIC}, AdjustAction::SetMonochromatic);
    row(AdjustRowId::Distortion, IDS_ADJUST_DISTORTION, 0, -100, 100, 0, false, AdjustAction::SetDistortion);
    _lensHint = card.AddChild<DxUi::Label>(LoadStringResource(nullptr, IDS_ADJUST_LENS_HINT));
    _lensHint->SetMultiline(true);
    _lensHint->SetFontRole(DxUi::FontRole::Small);
    _fillHint = card.AddChild<DxUi::Label>(LoadStringResource(nullptr, IDS_ADJUST_CONTENT_FILL_HINT));
    _fillHint->SetMultiline(true);
    _fillHint->SetFontRole(DxUi::FontRole::Small);
    _fillNoSource = card.AddChild<DxUi::Label>(LoadStringResource(nullptr, IDS_ADJUST_NO_SOURCE));
    _fillNoSource->SetMultiline(true);
    _fillNoSource->SetFontRole(DxUi::FontRole::Small);
    // Shared footer.
    _adjustPreview = MakeCheck(card, {.labelId = IDS_ADJUST_PREVIEW}, AdjustAction::SetPreview);
    _adjustNote = card.AddChild<DxUi::Label>(LoadStringResource(nullptr, IDS_ADJUST_LIMITED));
    _adjustNote->SetFontRole(DxUi::FontRole::Small);
    _adjustCancel = MakeButton(card, {.labelId = IDS_TRANSFORM_CANCEL}, AdjustAction::Cancel);
    _adjustApply = MakeButton(card, {.labelId = IDS_ADJUST_OK, .primary = true}, AdjustAction::Apply);
    for (const std::unique_ptr<DxUi::Control>& child : _adjustCard->GetChildren())
    {
        child->SetVisible(false);
    }
}

void ChromeHost::ArrangeAdjustPanel(float right, float top, float bottom) noexcept
{
    if (!_adjustCard || !_adjustCard->IsVisible())
    {
        return;
    }
    const AdjustInspectorState& state = _adjustInspector;
    const float width = std::min(kCardWidthDip, std::max(200.0f, right - kRailWidthDip - 2.0f * kMarginDip));
    const float left = right - kMarginDip - width;
    const float innerLeft = left + kCardPaddingDip;
    const float innerRight = left + width - kCardPaddingDip;
    float y = top + kMarginDip + kCardPaddingDip;
    const auto place = [&](DxUi::Control* control, float height)
    {
        if (control != nullptr)
        {
            PlaceControl(control, D2D1::RectF(innerLeft, y, innerRight, y + height));
        }
        y += height + kRowGapDip;
    };
    const auto placeRow = [&](AdjustRowId id)
    {
        const AdjustRow& entry = _adjustRows[static_cast<std::size_t>(id)];
        if (entry.label == nullptr)
        {
            return;
        }
        PlaceControl(entry.label, D2D1::RectF(innerLeft, y, innerLeft + kLabelWidthDip, y + kRowDip));
        PlaceControl(entry.slider, D2D1::RectF(innerLeft + kLabelWidthDip + 4.0f, y,
                                               innerRight - kStepperWidthDip - 8.0f, y + kRowDip));
        PlaceControl(entry.stepper, D2D1::RectF(innerRight - kStepperWidthDip, y, innerRight, y + kRowDip));
        y += kRowDip + kRowGapDip;
    };
    const auto placeButtons = [&](std::span<DxUi::Button* const> buttons, float height)
    {
        const float gap = 6.0f;
        const float each = (innerRight - innerLeft - gap * (buttons.size() - 1)) / static_cast<float>(buttons.size());
        float x = innerLeft;
        for (DxUi::Button* control : buttons)
        {
            if (control != nullptr)
            {
                PlaceControl(control, D2D1::RectF(x, y, x + each, y + height));
            }
            x += each + gap;
        }
        y += height + kRowGapDip;
    };
    place(_adjustTitle, 24.0f);
    // Levels and Hue/Saturation draw their own editors; every other kind shares the generic filter layout below.
    if (state.filter.has_value())
    {
        switch (*state.filter)
        {
        case FilterKind::Levels:
        {
            place(_adjustChannel, kRowDip);
            place(_levelsInput, kHistogramDip + LevelsBar::kHandleRowDip);
            const float third = (innerRight - innerLeft - 12.0f) / 3.0f;
            PlaceControl(_levelsBlack, D2D1::RectF(innerLeft, y, innerLeft + third, y + kRowDip));
            PlaceControl(_levelsGamma,
                         D2D1::RectF(innerLeft + third + 6.0f, y, innerLeft + 2 * third + 6.0f, y + kRowDip));
            PlaceControl(_levelsWhite, D2D1::RectF(innerRight - third, y, innerRight, y + kRowDip));
            y += kRowDip + kRowGapDip;
            place(_levelsOutput, kOutputRampDip + LevelsBar::kHandleRowDip);
            const float half = (innerRight - innerLeft - 6.0f) / 2.0f;
            PlaceControl(_levelsOutBlack, D2D1::RectF(innerLeft, y, innerLeft + half, y + kRowDip));
            PlaceControl(_levelsOutWhite, D2D1::RectF(innerRight - half, y, innerRight, y + kRowDip));
            y += kRowDip + kRowGapDip;
            placeButtons(_levelsSample, kRowDip);
            placeButtons(_levelsAuto, kRowDip);
            break;
        }
        case FilterKind::HueSaturation:
        {
            const bool spectrum = state.hsv.range != ColorRange::Master && !state.hsv.colorize;
            PlaceControl(_hueRange, D2D1::RectF(innerLeft, y, innerLeft + 150.0f, y + kRowDip));
            float x = innerRight;
            for (DxUi::Button* control : {_hueTargeting, _hueSample[2], _hueSample[1], _hueSample[0]})
            {
                const float buttonWidth = 50.0f;
                PlaceControl(control, D2D1::RectF(x - buttonWidth, y, x, y + kRowDip));
                x -= buttonWidth + 4.0f;
            }
            y += kRowDip + kRowGapDip;
            placeRow(AdjustRowId::Hue);
            placeRow(AdjustRowId::Saturation);
            placeRow(AdjustRowId::Lightness);
            if (spectrum)
            {
                place(_spectrum, kSpectrumDip);
                place(_hueInvert, 24.0f);
            }
            place(_hueColorize, 24.0f);
            break;
        }
        case FilterKind::Curves:
            place(_adjustChannel, kRowDip);
            place(_curveEditor, kCurveDip);
            place(_curveCaption, kNoteDip);
            placeButtons(std::array<DxUi::Button*, 2>{_curveRemove, _curveReset}, kRowDip);
            break;
        case FilterKind::Exposure:
            placeRow(AdjustRowId::Exposure);
            placeRow(AdjustRowId::Offset);
            placeRow(AdjustRowId::Gamma);
            break;
        case FilterKind::GradientMap:
        {
            place(_gradientRamp, kRampDip);
            PlaceControl(_mapShadows, D2D1::RectF(innerLeft, y, innerLeft + 24.0f, y + 24.0f));
            PlaceControl(_mapShadowsLabel, D2D1::RectF(innerLeft + 30.0f, y, innerLeft + 130.0f, y + 24.0f));
            PlaceControl(_mapHighlights, D2D1::RectF(innerLeft + 150.0f, y, innerLeft + 174.0f, y + 24.0f));
            PlaceControl(_mapHighlightsLabel, D2D1::RectF(innerLeft + 180.0f, y, innerRight, y + 24.0f));
            y += 24.0f + kRowGapDip;
            place(_mapReverse, 24.0f);
            break;
        }
        case FilterKind::Grain:
            placeRow(AdjustRowId::GrainAmount);
            placeRow(AdjustRowId::GrainSize);
            placeRow(AdjustRowId::GrainRoughness);
            break;
        case FilterKind::GaussianBlur:
            placeRow(AdjustRowId::Radius);
            break;
        case FilterKind::MotionBlur:
            placeRow(AdjustRowId::Angle);
            placeRow(AdjustRowId::Distance);
            break;
        case FilterKind::AddNoise:
            placeRow(AdjustRowId::Amount);
            place(_noiseDistribution, kRowDip);
            place(_noiseMono, 24.0f);
            break;
        case FilterKind::LensCorrection:
            placeRow(AdjustRowId::Distortion);
            place(_lensHint, 36.0f);
            break;
        case FilterKind::ContentAwareFill:
            place(_fillHint, 36.0f);
            if (state.previewFailed)
            {
                place(_fillNoSource, 54.0f);
            }
            break;
        }
    }
    place(_adjustPreview, 24.0f);
    if (state.limitedToSelection && !state.editsAdjustment)
    {
        place(_adjustNote, kNoteDip);
    }
    const float buttonWidth = 90.0f;
    PlaceControl(_adjustCancel, D2D1::RectF(innerLeft, y, innerLeft + buttonWidth, y + 30.0f));
    PlaceControl(_adjustApply, D2D1::RectF(innerRight - buttonWidth, y, innerRight, y + 30.0f));
    y += 30.0f + kCardPaddingDip;
    const float cardBottom = std::min(y, bottom - kMarginDip);
    PlaceControl(_adjustCard, D2D1::RectF(left, top + kMarginDip, left + width, cardBottom));
}

void ChromeHost::SetAdjustInspector(const AdjustInspectorState& state) noexcept
{
    // The panel writes only a state it has not shown (RF-1001, `_resync`): an open panel is synced on every pointer
    // move over the canvas.
    if (!_adjustCard || (!TakeResync(kResyncAdjust) && state == _adjustInspector))
    {
        return;
    }
    ++_syncCounters.headers;
    const auto hold = HoldReports();
    const bool wasOpen = _adjustCard->IsVisible();
    _adjustInspector = state;
    const bool open = state.filter.has_value();
    _adjustCard->SetVisible(open);
    if (open)
    {
        _adjustTitle->SetText(state.title);
        _adjustTitle->SetVisible(true);
        // ShowControl (ChromeHost.cpp) already writes SetVisible only when it changes; re-implementing it here
        // without that check re-issued it for every control on every sync (every pan move re-syncs the panel).
        const auto showRow = [&](AdjustRowId id, bool visible, double value)
        {
            const AdjustRow& entry = _adjustRows[static_cast<std::size_t>(id)];
            ShowControl(entry.label, visible);
            ShowControl(entry.slider, visible);
            ShowControl(entry.stepper, visible);
            if (visible)
            {
                entry.slider->SetValue(entry.logarithmic ? std::log(std::max(value, entry.minimum)) : value);
                // A typed value in progress reformats on every keystroke otherwise (SetValue clears IsEditing()
                // and re-renders the text), mangling a second '.' or a partial decimal before Enter commits it.
                if (!entry.stepper->IsEditing())
                {
                    entry.stepper->SetValue(value);
                }
            }
        };
        const bool levels = state.filter == FilterKind::Levels;
        const bool hsv = state.filter == FilterKind::HueSaturation;
        const auto kind = [&](FilterKind which) { return state.filter == which; };
        ShowControl(_adjustChannel, levels || kind(FilterKind::Curves));
        if (levels)
        {
            _adjustChannel->SetSelectedIndex(static_cast<size_t>(state.levels.channel));
            const int channel = static_cast<int>(state.levels.channel);
            _levelsInput->SetHistogram(state.histogram[static_cast<std::size_t>(channel)], channel);
            const LevelRange range = Adjust::LevelsCurrent(state.levels).Normalized();
            _levelsInput->SetRange(range);
            _levelsOutput->SetRange(range);
            // Same guard as the value rows below: never reformat a stepper the user is still typing into.
            const auto syncLevel = [](DxUi::NumericStepper* stepper, double value)
            {
                if (!stepper->IsEditing())
                {
                    stepper->SetValue(value);
                }
            };
            syncLevel(_levelsBlack, range.black);
            syncLevel(_levelsGamma, range.gamma);
            syncLevel(_levelsWhite, range.white);
            syncLevel(_levelsOutBlack, range.outputBlack);
            syncLevel(_levelsOutWhite, range.outputWhite);
            for (int index = 0; index < 3; ++index)
            {
                SetPressedVisualIfChanged(_levelsSample[static_cast<std::size_t>(index)],
                                          _levelsSamplePressed[static_cast<std::size_t>(index)],
                                          state.levelsSample == index + 1);
                _levelsAuto[static_cast<std::size_t>(index)]->SetEnabled(state.histogramReady);
            }
        }
        else if (kind(FilterKind::Curves))
        {
            _adjustChannel->SetSelectedIndex(static_cast<size_t>(state.settings.curves.channel));
            _curveEditor->SetCurves(state.settings.curves, static_cast<int>(state.settings.curves.channel));
            const std::optional<std::size_t> selected = _curveEditor->SelectedIndex();
            const std::vector<CurvePoint>& points =
                state.settings.curves.channels[static_cast<std::size_t>(state.settings.curves.channel)];
            if (selected.has_value() && selected.value() < points.size())
            {
                _curveCaption->SetText(FormatStringResource(nullptr, IDS_ADJUST_CURVE_POINT,
                                                            static_cast<int>(points[selected.value()].x),
                                                            static_cast<int>(points[selected.value()].y)));
                _curveRemove->SetEnabled(selected.value() > 0 && selected.value() + 1 < points.size());
            }
            else
            {
                _curveCaption->SetText(LoadStringResource(nullptr, IDS_ADJUST_CURVE_HINT));
                _curveRemove->SetEnabled(false);
            }
        }
        for (DxUi::Control* control :
             {static_cast<DxUi::Control*>(_levelsInput), static_cast<DxUi::Control*>(_levelsOutput),
              static_cast<DxUi::Control*>(_levelsBlack), static_cast<DxUi::Control*>(_levelsGamma),
              static_cast<DxUi::Control*>(_levelsWhite), static_cast<DxUi::Control*>(_levelsOutBlack),
              static_cast<DxUi::Control*>(_levelsOutWhite)})
        {
            ShowControl(control, levels);
        }
        for (DxUi::Button* control : _levelsSample)
        {
            ShowControl(control, levels);
        }
        for (DxUi::Button* control : _levelsAuto)
        {
            ShowControl(control, levels);
        }
        ShowControl(_curveEditor, kind(FilterKind::Curves));
        ShowControl(_curveCaption, kind(FilterKind::Curves));
        ShowControl(_curveRemove, kind(FilterKind::Curves));
        ShowControl(_curveReset, kind(FilterKind::Curves));
        // Hue / Saturation.
        const bool spectrum = hsv && state.hsv.range != ColorRange::Master && !state.hsv.colorize;
        ShowControl(_hueRange, hsv);
        for (DxUi::Button* control : _hueSample)
        {
            ShowControl(control, spectrum);
        }
        ShowControl(_hueTargeting, hsv && !state.hsv.colorize);
        ShowControl(_spectrum, spectrum);
        ShowControl(_hueInvert, spectrum);
        ShowControl(_hueColorize, hsv);
        if (hsv)
        {
            _hueRange->SetSelectedIndex(static_cast<size_t>(state.hsv.range));
            _hueRange->SetEnabled(!state.hsv.colorize);
            const RangeAdjustment& range = Adjust::RangeOf(state.hsv);
            const AdjustRow& hueRow = _adjustRows[static_cast<std::size_t>(AdjustRowId::Hue)];
            const AdjustRow& saturationRow = _adjustRows[static_cast<std::size_t>(AdjustRowId::Saturation)];
            hueRow.slider->SetMinimum(state.hsv.colorize ? 0 : -180);
            hueRow.slider->SetMaximum(state.hsv.colorize ? 360 : 180);
            hueRow.stepper->SetMinimum(state.hsv.colorize ? 0 : -180);
            hueRow.stepper->SetMaximum(state.hsv.colorize ? 360 : 180);
            saturationRow.slider->SetMinimum(state.hsv.colorize ? 0 : -100);
            saturationRow.stepper->SetMinimum(state.hsv.colorize ? 0 : -100);
            for (int index = 0; index < 3; ++index)
            {
                SetPressedVisualIfChanged(_hueSample[static_cast<std::size_t>(index)],
                                          _hueSamplePressed[static_cast<std::size_t>(index)],
                                          state.hueSample == index + 1);
            }
            SetPressedVisualIfChanged(_hueTargeting, _hueTargetingPressed, state.targeting);
            _spectrum->SetSettings(state.hsv);
            _hueInvert->SetChecked(state.hsv.invertRange);
            _hueColorize->SetChecked(state.hsv.colorize);
            showRow(AdjustRowId::Hue, true, range.hue);
            showRow(AdjustRowId::Saturation, true, range.saturation);
            showRow(AdjustRowId::Lightness, true, range.lightness);
        }
        else
        {
            showRow(AdjustRowId::Hue, false, 0);
            showRow(AdjustRowId::Saturation, false, 0);
            showRow(AdjustRowId::Lightness, false, 0);
        }
        // Filters.
        const FilterSettings& settings = state.settings;
        showRow(AdjustRowId::Exposure, kind(FilterKind::Exposure), settings.exposure.exposure);
        showRow(AdjustRowId::Offset, kind(FilterKind::Exposure), settings.exposure.offset);
        showRow(AdjustRowId::Gamma, kind(FilterKind::Exposure), settings.exposure.gamma);
        const bool map = kind(FilterKind::GradientMap);
        ShowControl(_gradientRamp, map);
        ShowControl(_mapShadows, map);
        ShowControl(_mapShadowsLabel, map);
        ShowControl(_mapHighlights, map);
        ShowControl(_mapHighlightsLabel, map);
        ShowControl(_mapReverse, map);
        if (map)
        {
            const AdjustmentColor& dark =
                settings.gradientMap.reversed ? settings.gradientMap.highlights : settings.gradientMap.shadows;
            const AdjustmentColor& light =
                settings.gradientMap.reversed ? settings.gradientMap.shadows : settings.gradientMap.highlights;
            _gradientRamp->SetEnds(dark, light);
            _mapShadows->SetSwatchValue(ArgbOf(settings.gradientMap.shadows));
            _mapHighlights->SetSwatchValue(ArgbOf(settings.gradientMap.highlights));
            _mapReverse->SetChecked(settings.gradientMap.reversed);
        }
        showRow(AdjustRowId::GrainAmount, kind(FilterKind::Grain), settings.grain.amount);
        showRow(AdjustRowId::GrainSize, kind(FilterKind::Grain), settings.grain.size);
        showRow(AdjustRowId::GrainRoughness, kind(FilterKind::Grain), settings.grain.roughness);
        showRow(AdjustRowId::Radius, kind(FilterKind::GaussianBlur), settings.radius);
        showRow(AdjustRowId::Angle, kind(FilterKind::MotionBlur), settings.angle);
        showRow(AdjustRowId::Distance, kind(FilterKind::MotionBlur), settings.distance);
        showRow(AdjustRowId::Amount, kind(FilterKind::AddNoise), settings.amount);
        ShowControl(_noiseDistribution, kind(FilterKind::AddNoise));
        ShowControl(_noiseMono, kind(FilterKind::AddNoise));
        if (kind(FilterKind::AddNoise))
        {
            _noiseDistribution->SetSelectedIndex(settings.gaussian ? 1u : 0u);
            _noiseMono->SetChecked(settings.monochromatic);
        }
        showRow(AdjustRowId::Distortion, kind(FilterKind::LensCorrection), settings.distortion);
        ShowControl(_lensHint, kind(FilterKind::LensCorrection));
        ShowControl(_fillHint, kind(FilterKind::ContentAwareFill));
        ShowControl(_fillNoSource, kind(FilterKind::ContentAwareFill) && state.previewFailed);
        // Footer.
        ShowControl(_adjustPreview, true);
        _adjustPreview->SetChecked(state.preview);
        ShowControl(_adjustNote, state.limitedToSelection && !state.editsAdjustment);
        ShowControl(_adjustCancel, true);
        ShowControl(_adjustApply, true);
        _adjustApply->SetEnabled(!(kind(FilterKind::ContentAwareFill) && state.previewFailed));
        if (!wasOpen)
        {
            // Enter and Escape its controls leave are its OK and Cancel; it takes no keyboard when it opens.
            PushCard(ModalCard::Adjust, _adjustCard, nullptr);
        }
    }
    else if (wasOpen)
    {
        if (_curveEditor)
        {
            _curveEditor->ClearSelection();
        }
        // The panel closing (Cancel/OK, a tool switch, the document going away) takes its Gradient Map color picker
        // with it, as the size card does its custom-extension picker, without reporting a pick.
        if (ColorPickerOpen() &&
            (_pickerTarget == PickerTarget::GradientShadows || _pickerTarget == PickerTarget::GradientHighlights))
        {
            ClosePickerCard();
        }
        PopCard(ModalCard::Adjust);
    }
    _view.MarkDirty();
}
