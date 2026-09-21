#include "ChromeHost.h"

#include "Resources.h"
#include "resource.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <span>

// The adjustment panel (C12, Specs/UI/UI_ApplicationChrome.md): a card over the canvas hole while a Levels,
// Hue/Saturation or filter edit is open. Every control mirrors the session through SetAdjustInspector and reports
// changes through the adjust handler; nothing here re-lays out inside a drag callback.

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

void ChromeHost::SetAdjustHandler(std::function<void(AdjustAction, double)> handler)
{
    _onAdjust = std::move(handler);
}

void ChromeHost::SetCurveHandler(std::function<void(std::vector<CurvePoint>)> handler)
{
    _onCurve = std::move(handler);
}

void ChromeHost::SetBandHandler(std::function<void(int, double)> handler)
{
    _onBand = std::move(handler);
}

void ChromeHost::SetAdjustColorHandler(std::function<void(ChromeEditPhase, uint32_t, bool)> handler)
{
    _onAdjustColor = std::move(handler);
}

bool ChromeHost::AdjustPanelOpen() const noexcept
{
    return _adjustCard != nullptr && _adjustCard->IsVisible();
}

D2D1_RECT_F ChromeHost::AdjustPanelBoundsDip() const noexcept
{
    return AdjustPanelOpen() ? _adjustCard->GetBounds() : D2D1::RectF();
}

void ChromeHost::BuildAdjustPanel(DxUi::Panel& root)
{
    _adjustCard = root.AddChild<DxUi::CardPanel>();
    _adjustCard->SetCornerRadius(8.0f);
    _adjustCard->SetVisible(false);
    _adjustTitle = _adjustCard->AddChild<DxUi::Label>(std::wstring{});
    _adjustTitle->SetFontRole(DxUi::FontRole::BodyStrong);
    const auto action = [this](AdjustAction what, double value)
    {
        if (!_syncingAdjust && _onAdjust)
        {
            _onAdjust(what, value);
        }
    };
    const auto combo = [this](UINT nameId, std::initializer_list<UINT> items, AdjustAction what)
    {
        DxUi::ComboBox* control = _adjustCard->AddChild<DxUi::ComboBox>();
        control->SetAccessibleName(LoadStringResource(nullptr, nameId));
        std::vector<DxUi::ComboBox::Item> entries;
        std::size_t index = 0;
        for (const UINT id : items)
        {
            entries.push_back(DxUi::ComboBox::Item{std::to_wstring(index++), LoadStringResource(nullptr, id)});
        }
        control->SetItems(std::move(entries));
        control->SetSelectedIndex(0);
        control->SetOnSelectionChanged(
            [this, what](size_t selected)
            {
                if (!_syncingAdjust && _onAdjust)
                {
                    _onAdjust(what, static_cast<double>(selected));
                }
            });
        return control;
    };
    const auto button = [this](UINT labelId, std::function<void()> onClick)
    {
        DxUi::Button* control = _adjustCard->AddChild<DxUi::Button>(LoadStringResource(nullptr, labelId));
        control->SetOnClick(std::move(onClick));
        return control;
    };
    const auto checkbox = [this](UINT labelId, AdjustAction what)
    {
        DxUi::Checkbox* control = _adjustCard->AddChild<DxUi::Checkbox>(LoadStringResource(nullptr, labelId));
        control->SetOnToggled(
            [this, what](bool value)
            {
                if (!_syncingAdjust && _onAdjust)
                {
                    _onAdjust(what, value ? 1.0 : 0.0);
                }
            });
        return control;
    };
    const auto stepper =
        [this](UINT labelId, UINT unitId, double minimum, double maximum, double step, int decimals, AdjustAction what)
    {
        DxUi::NumericStepper* control = _adjustCard->AddChild<DxUi::NumericStepper>();
        control->SetLabel(LoadStringResource(nullptr, labelId), 0.0f);
        control->SetAccessibleName(LoadStringResource(nullptr, labelId));
        control->SetUnit(unitId != 0 ? LoadStringResource(nullptr, unitId) : std::wstring{},
                         unitId != 0 ? 14.0f : 0.0f);
        control->SetMinimum(minimum);
        control->SetMaximum(maximum);
        control->SetStep(step);
        control->SetLargeStep(step * 10);
        control->SetDecimals(static_cast<uint8_t>(decimals));
        control->SetOnChange(
            [this, what](DxUi::NumericStepperChange change)
            {
                if (!_syncingAdjust && _onAdjust)
                {
                    _onAdjust(what, change.value);
                }
            });
        return control;
    };
    // A value row: a label, a slider (logarithmic ones give the small values most of the travel) and an exact stepper.
    const auto row = [&](AdjustRowId id, UINT labelId, UINT unitId, double minimum, double maximum, int decimals,
                         bool logarithmic, AdjustAction what)
    {
        AdjustRow& entry = _adjustRows[static_cast<std::size_t>(id)];
        entry.label = _adjustCard->AddChild<DxUi::Label>(LoadStringResource(nullptr, labelId));
        entry.slider = _adjustCard->AddChild<DxUi::Slider>();
        entry.slider->SetAccessibleName(LoadStringResource(nullptr, labelId));
        entry.slider->SetMinimum(logarithmic ? std::log(minimum) : minimum);
        entry.slider->SetMaximum(logarithmic ? std::log(maximum) : maximum);
        entry.slider->SetStep(logarithmic ? 0.01 : std::pow(10.0, -decimals));
        entry.slider->SetLargeStep(logarithmic ? 0.1 : (maximum - minimum) / 10);
        entry.slider->SetOnValueChanged(
            [this, id](double value)
            {
                const AdjustRow& changed = _adjustRows[static_cast<std::size_t>(id)];
                if (!_syncingAdjust && _onAdjust)
                {
                    const double real = changed.logarithmic ? std::exp(value) : value;
                    _onAdjust(changed.action,
                              RoundTo(std::clamp(real, changed.minimum, changed.maximum), changed.decimals));
                }
            });
        entry.stepper = stepper(labelId, unitId, minimum, maximum, std::pow(10.0, -decimals), decimals, what);
        entry.action = what;
        entry.minimum = minimum;
        entry.maximum = maximum;
        entry.logarithmic = logarithmic;
        entry.decimals = decimals;
    };
    // Curves and Levels share the channel choice.
    _adjustChannel = combo(IDS_ADJUST_CHANNEL, {IDS_ADJUST_RGB, IDS_ADJUST_RED, IDS_ADJUST_GREEN, IDS_ADJUST_BLUE},
                           AdjustAction::SetChannel);
    _curveEditor = _adjustCard->AddChild<CurveEditor>();
    _curveEditor->SetAccessibleName(LoadStringResource(nullptr, IDS_A11Y_CURVE_EDITOR));
    _curveEditor->SetOnChange(
        [this](std::vector<CurvePoint> points)
        {
            if (!_syncingAdjust && _onCurve)
            {
                _onCurve(std::move(points));
            }
        });
    _curveCaption = _adjustCard->AddChild<DxUi::Label>(LoadStringResource(nullptr, IDS_ADJUST_CURVE_HINT));
    _curveRemove = button(IDS_ADJUST_REMOVE_POINT,
                          [this]
                          {
                              const std::optional<std::size_t> selected =
                                  _curveEditor ? _curveEditor->SelectedIndex() : std::nullopt;
                              if (selected.has_value() && _onAdjust)
                              {
                                  _onAdjust(AdjustAction::RemovePoint, static_cast<double>(selected.value()));
                              }
                          });
    _curveReset = button(IDS_ADJUST_RESET, [action] { action(AdjustAction::Reset, 0.0); });
    // Levels.
    _levelsInput = _adjustCard->AddChild<LevelsBar>();
    _levelsInput->SetAccessibleName(LoadStringResource(nullptr, IDS_A11Y_LEVELS_INPUT));
    _levelsInput->SetKind(LevelsBar::Kind::Input);
    _levelsInput->SetOnHandle(
        [this](int handle, double value)
        {
            if (_syncingAdjust || !_onAdjust)
            {
                return;
            }
            const LevelRange& range = Adjust::LevelsCurrent(_adjustInspector.levels);
            if (handle == 0)
            {
                _onAdjust(AdjustAction::SetInputBlack, std::min(range.white - 1, std::round(value)));
            }
            else if (handle == 2)
            {
                _onAdjust(AdjustAction::SetInputWhite, std::max(range.black + 1, std::round(value)));
            }
            else
            {
                const double fraction =
                    std::min(0.999, std::max(0.001, (value - range.black) / (range.white - range.black)));
                _onAdjust(AdjustAction::SetInputGamma, std::log(fraction) / std::log(0.5));
            }
        });
    _levelsOutput = _adjustCard->AddChild<LevelsBar>();
    _levelsOutput->SetAccessibleName(LoadStringResource(nullptr, IDS_A11Y_LEVELS_OUTPUT));
    _levelsOutput->SetKind(LevelsBar::Kind::Output);
    _levelsOutput->SetOnHandle(
        [this](int handle, double value)
        {
            if (!_syncingAdjust && _onAdjust)
            {
                _onAdjust(handle == 0 ? AdjustAction::SetOutputBlack : AdjustAction::SetOutputWhite, std::round(value));
            }
        });
    _levelsBlack = stepper(IDS_ADJUST_INPUT_BLACK, 0, 0, 254, 1, 0, AdjustAction::SetInputBlack);
    _levelsGamma = stepper(IDS_ADJUST_GAMMA, 0, 0.1, 9.99, 0.01, 2, AdjustAction::SetInputGamma);
    _levelsWhite = stepper(IDS_ADJUST_INPUT_WHITE, 0, 1, 255, 1, 0, AdjustAction::SetInputWhite);
    _levelsOutBlack = stepper(IDS_ADJUST_OUTPUT_BLACK, 0, 0, 255, 1, 0, AdjustAction::SetOutputBlack);
    _levelsOutWhite = stepper(IDS_ADJUST_OUTPUT_WHITE, 0, 0, 255, 1, 0, AdjustAction::SetOutputWhite);
    const UINT sampleIds[] = {IDS_ADJUST_SAMPLE_BLACK, IDS_ADJUST_SAMPLE_GRAY, IDS_ADJUST_SAMPLE_WHITE};
    for (int index = 0; index < 3; ++index)
    {
        _levelsSample[static_cast<std::size_t>(index)] =
            button(sampleIds[index],
                   [this, index]
                   {
                       if (_onAdjust)
                       {
                           // Pressing the armed eyedropper again stops sampling.
                           _onAdjust(AdjustAction::SetLevelsSample,
                                     _adjustInspector.levelsSample == index + 1 ? 0.0 : index + 1.0);
                       }
                   });
        _levelsSample[static_cast<std::size_t>(index)]->SetTooltipText(
            LoadStringResource(nullptr, IDS_ADJUST_SAMPLE_TIP));
    }
    const UINT autoIds[] = {IDS_ADJUST_AUTO_CONTRAST, IDS_ADJUST_AUTO_COLOR, IDS_ADJUST_AUTO_NEUTRAL};
    for (int index = 0; index < 3; ++index)
    {
        _levelsAuto[static_cast<std::size_t>(index)] =
            button(autoIds[index], [action, index] { action(AdjustAction::AutoLevels, static_cast<double>(index)); });
    }
    // Hue / Saturation.
    _hueRange = combo(IDS_ADJUST_RANGE,
                      {IDS_ADJUST_MASTER, IDS_ADJUST_REDS, IDS_ADJUST_YELLOWS, IDS_ADJUST_GREENS, IDS_ADJUST_CYANS,
                       IDS_ADJUST_BLUES, IDS_ADJUST_MAGENTAS},
                      AdjustAction::SetRange);
    const UINT hueSampleIds[] = {IDS_ADJUST_HUE_SAMPLE, IDS_ADJUST_HUE_ADD, IDS_ADJUST_HUE_REMOVE};
    for (int index = 0; index < 3; ++index)
    {
        _hueSample[static_cast<std::size_t>(index)] = button(
            hueSampleIds[index],
            [this, index]
            {
                if (_onAdjust)
                {
                    _onAdjust(AdjustAction::SetHueSample, _adjustInspector.hueSample == index + 1 ? 0.0 : index + 1.0);
                }
            });
        _hueSample[static_cast<std::size_t>(index)]->SetTooltipText(
            LoadStringResource(nullptr, IDS_ADJUST_HUE_SAMPLE_TIP));
    }
    _hueTargeting = button(IDS_ADJUST_TARGETED,
                           [this]
                           {
                               if (_onAdjust)
                               {
                                   _onAdjust(AdjustAction::SetTargeting, _adjustInspector.targeting ? 0.0 : 1.0);
                               }
                           });
    _hueTargeting->SetTooltipText(LoadStringResource(nullptr, IDS_ADJUST_TARGETED_TIP));
    row(AdjustRowId::Hue, IDS_ADJUST_HUE, IDS_UNIT_DEGREES, -180, 180, 0, false, AdjustAction::SetHue);
    row(AdjustRowId::Saturation, IDS_ADJUST_SATURATION, 0, -100, 100, 0, false, AdjustAction::SetSaturation);
    row(AdjustRowId::Lightness, IDS_ADJUST_LIGHTNESS, 0, -100, 100, 0, false, AdjustAction::SetLightness);
    _spectrum = _adjustCard->AddChild<SpectrumEditor>();
    _spectrum->SetAccessibleName(LoadStringResource(nullptr, IDS_A11Y_HUE_SPECTRUM));
    _spectrum->SetOnHandle(
        [this](int index, double degrees)
        {
            if (!_syncingAdjust && _onBand)
            {
                _onBand(index, degrees);
            }
        });
    _hueInvert = checkbox(IDS_ADJUST_INVERT_RANGE, AdjustAction::SetInvertRange);
    _hueColorize = checkbox(IDS_ADJUST_COLORIZE, AdjustAction::SetColorize);
    // Exposure, Gradient Map, Grain.
    row(AdjustRowId::Exposure, IDS_ADJUST_EXPOSURE, 0, -20, 20, 2, false, AdjustAction::SetExposure);
    row(AdjustRowId::Offset, IDS_ADJUST_OFFSET, 0, -0.5, 0.5, 4, false, AdjustAction::SetOffset);
    row(AdjustRowId::Gamma, IDS_ADJUST_GAMMA, 0, 0.01, 9.99, 2, true, AdjustAction::SetGamma);
    _gradientRamp = _adjustCard->AddChild<GradientRamp>();
    _gradientRamp->SetAccessibleName(LoadStringResource(nullptr, IDS_A11Y_GRADIENT_RAMP));
    _mapShadows = _adjustCard->AddChild<DxUi::ColorSwatch>(0xFF000000u);
    _mapShadows->SetAccessibleName(LoadStringResource(nullptr, IDS_ADJUST_SHADOWS));
    _mapShadows->SetOnClick([action] { action(AdjustAction::OpenShadows, 0.0); });
    _mapShadowsLabel = _adjustCard->AddChild<DxUi::Label>(LoadStringResource(nullptr, IDS_ADJUST_SHADOWS));
    _mapHighlights = _adjustCard->AddChild<DxUi::ColorSwatch>(0xFFFFFFFFu);
    _mapHighlights->SetAccessibleName(LoadStringResource(nullptr, IDS_ADJUST_HIGHLIGHTS));
    _mapHighlights->SetOnClick([action] { action(AdjustAction::OpenHighlights, 0.0); });
    _mapHighlightsLabel = _adjustCard->AddChild<DxUi::Label>(LoadStringResource(nullptr, IDS_ADJUST_HIGHLIGHTS));
    _mapReverse = checkbox(IDS_ADJUST_REVERSE, AdjustAction::SetReversed);
    row(AdjustRowId::GrainAmount, IDS_ADJUST_AMOUNT, 0, 0, 100, 0, false, AdjustAction::SetGrainAmount);
    row(AdjustRowId::GrainSize, IDS_ADJUST_SIZE, IDS_UNIT_PIXELS, 0.5, 20, 1, true, AdjustAction::SetGrainSize);
    row(AdjustRowId::GrainRoughness, IDS_ADJUST_ROUGHNESS, 0, 0, 100, 0, false, AdjustAction::SetGrainRoughness);
    // Blurs, noise and the lens.
    row(AdjustRowId::Radius, IDS_ADJUST_RADIUS, IDS_UNIT_PIXELS, 0.1, 250, 1, true, AdjustAction::SetRadius);
    row(AdjustRowId::Angle, IDS_ADJUST_ANGLE, IDS_UNIT_DEGREES, -90, 90, 0, false, AdjustAction::SetAngle);
    row(AdjustRowId::Distance, IDS_ADJUST_DISTANCE, IDS_UNIT_PIXELS, 1, 2000, 0, true, AdjustAction::SetDistance);
    row(AdjustRowId::Amount, IDS_ADJUST_AMOUNT, IDS_UNIT_PERCENT, 0.1, 400, 1, true, AdjustAction::SetAmount);
    _noiseDistribution =
        combo(IDS_A11Y_NOISE_DISTRIBUTION, {IDS_ADJUST_UNIFORM, IDS_ADJUST_GAUSSIAN}, AdjustAction::SetGaussian);
    _noiseMono = checkbox(IDS_ADJUST_MONOCHROMATIC, AdjustAction::SetMonochromatic);
    row(AdjustRowId::Distortion, IDS_ADJUST_DISTORTION, 0, -100, 100, 0, false, AdjustAction::SetDistortion);
    _lensHint = _adjustCard->AddChild<DxUi::Label>(LoadStringResource(nullptr, IDS_ADJUST_LENS_HINT));
    _lensHint->SetMultiline(true);
    _lensHint->SetFontRole(DxUi::FontRole::Small);
    _fillHint = _adjustCard->AddChild<DxUi::Label>(LoadStringResource(nullptr, IDS_ADJUST_CONTENT_FILL_HINT));
    _fillHint->SetMultiline(true);
    _fillHint->SetFontRole(DxUi::FontRole::Small);
    _fillNoSource = _adjustCard->AddChild<DxUi::Label>(LoadStringResource(nullptr, IDS_ADJUST_NO_SOURCE));
    _fillNoSource->SetMultiline(true);
    _fillNoSource->SetFontRole(DxUi::FontRole::Small);
    // Shared footer.
    _adjustPreview = checkbox(IDS_ADJUST_PREVIEW, AdjustAction::SetPreview);
    _adjustNote = _adjustCard->AddChild<DxUi::Label>(LoadStringResource(nullptr, IDS_ADJUST_LIMITED));
    _adjustNote->SetFontRole(DxUi::FontRole::Small);
    _adjustCancel = button(IDS_TRANSFORM_CANCEL, [action] { action(AdjustAction::Cancel, 0.0); });
    _adjustApply = button(IDS_ADJUST_OK, [action] { action(AdjustAction::Apply, 0.0); });
    _adjustApply->SetPrimary(true);
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
            control->SetBounds(D2D1::RectF(innerLeft, y, innerRight, y + height));
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
        entry.label->SetBounds(D2D1::RectF(innerLeft, y, innerLeft + kLabelWidthDip, y + kRowDip));
        entry.slider->SetBounds(
            D2D1::RectF(innerLeft + kLabelWidthDip + 4.0f, y, innerRight - kStepperWidthDip - 8.0f, y + kRowDip));
        entry.stepper->SetBounds(D2D1::RectF(innerRight - kStepperWidthDip, y, innerRight, y + kRowDip));
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
                control->SetBounds(D2D1::RectF(x, y, x + each, y + height));
            }
            x += each + gap;
        }
        y += height + kRowGapDip;
    };
    place(_adjustTitle, 24.0f);
    switch (state.panel)
    {
    case AdjustPanelKind::Levels:
    {
        place(_adjustChannel, kRowDip);
        place(_levelsInput, kHistogramDip + LevelsBar::kHandleRowDip);
        const float third = (innerRight - innerLeft - 12.0f) / 3.0f;
        _levelsBlack->SetBounds(D2D1::RectF(innerLeft, y, innerLeft + third, y + kRowDip));
        _levelsGamma->SetBounds(D2D1::RectF(innerLeft + third + 6.0f, y, innerLeft + 2 * third + 6.0f, y + kRowDip));
        _levelsWhite->SetBounds(D2D1::RectF(innerRight - third, y, innerRight, y + kRowDip));
        y += kRowDip + kRowGapDip;
        place(_levelsOutput, kOutputRampDip + LevelsBar::kHandleRowDip);
        const float half = (innerRight - innerLeft - 6.0f) / 2.0f;
        _levelsOutBlack->SetBounds(D2D1::RectF(innerLeft, y, innerLeft + half, y + kRowDip));
        _levelsOutWhite->SetBounds(D2D1::RectF(innerRight - half, y, innerRight, y + kRowDip));
        y += kRowDip + kRowGapDip;
        placeButtons(_levelsSample, kRowDip);
        placeButtons(_levelsAuto, kRowDip);
        break;
    }
    case AdjustPanelKind::HueSaturation:
    {
        const bool spectrum = state.hsv.range != ColorRange::Master && !state.hsv.colorize;
        _hueRange->SetBounds(D2D1::RectF(innerLeft, y, innerLeft + 150.0f, y + kRowDip));
        float x = innerRight;
        for (DxUi::Button* control : {_hueTargeting, _hueSample[2], _hueSample[1], _hueSample[0]})
        {
            const float buttonWidth = 50.0f;
            control->SetBounds(D2D1::RectF(x - buttonWidth, y, x, y + kRowDip));
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
    case AdjustPanelKind::Filter:
        switch (state.filter)
        {
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
            _mapShadows->SetBounds(D2D1::RectF(innerLeft, y, innerLeft + 24.0f, y + 24.0f));
            _mapShadowsLabel->SetBounds(D2D1::RectF(innerLeft + 30.0f, y, innerLeft + 130.0f, y + 24.0f));
            _mapHighlights->SetBounds(D2D1::RectF(innerLeft + 150.0f, y, innerLeft + 174.0f, y + 24.0f));
            _mapHighlightsLabel->SetBounds(D2D1::RectF(innerLeft + 180.0f, y, innerRight, y + 24.0f));
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
        break;
    case AdjustPanelKind::None:
        break;
    }
    place(_adjustPreview, 24.0f);
    if (state.limitedToSelection && !state.editsAdjustment)
    {
        place(_adjustNote, kNoteDip);
    }
    const float buttonWidth = 90.0f;
    _adjustCancel->SetBounds(D2D1::RectF(innerLeft, y, innerLeft + buttonWidth, y + 30.0f));
    _adjustApply->SetBounds(D2D1::RectF(innerRight - buttonWidth, y, innerRight, y + 30.0f));
    y += 30.0f + kCardPaddingDip;
    const float cardBottom = std::min(y, bottom - kMarginDip);
    _adjustCard->SetBounds(D2D1::RectF(left, top + kMarginDip, left + width, cardBottom));
}

void ChromeHost::SetAdjustInspector(const AdjustInspectorState& state) noexcept
{
    if (!_adjustCard)
    {
        return;
    }
    _syncingAdjust = true;
    const bool wasOpen = _adjustCard->IsVisible();
    _adjustInspector = state;
    const bool open = state.panel != AdjustPanelKind::None;
    _adjustCard->SetVisible(open);
    if (open)
    {
        _adjustTitle->SetText(state.title);
        _adjustTitle->SetVisible(true);
        const auto show = [](DxUi::Control* control, bool visible)
        {
            if (control != nullptr)
            {
                control->SetVisible(visible);
            }
        };
        const auto showRow = [&](AdjustRowId id, bool visible, double value)
        {
            const AdjustRow& entry = _adjustRows[static_cast<std::size_t>(id)];
            show(entry.label, visible);
            show(entry.slider, visible);
            show(entry.stepper, visible);
            if (visible)
            {
                entry.slider->SetValue(entry.logarithmic ? std::log(std::max(value, entry.minimum)) : value);
                entry.stepper->SetValue(value);
            }
        };
        const bool levels = state.panel == AdjustPanelKind::Levels;
        const bool hsv = state.panel == AdjustPanelKind::HueSaturation;
        const bool filter = state.panel == AdjustPanelKind::Filter;
        const auto kind = [&](FilterKind which) { return filter && state.filter == which; };
        show(_adjustChannel, levels || kind(FilterKind::Curves));
        if (levels)
        {
            _adjustChannel->SetSelectedIndex(static_cast<size_t>(state.levels.channel));
            const int channel = static_cast<int>(state.levels.channel);
            _levelsInput->SetHistogram(state.histogram[static_cast<std::size_t>(channel)], channel);
            const LevelRange range = Adjust::LevelsCurrent(state.levels).Normalized();
            _levelsInput->SetRange(range);
            _levelsOutput->SetRange(range);
            _levelsBlack->SetValue(range.black);
            _levelsGamma->SetValue(range.gamma);
            _levelsWhite->SetValue(range.white);
            _levelsOutBlack->SetValue(range.outputBlack);
            _levelsOutWhite->SetValue(range.outputWhite);
            for (int index = 0; index < 3; ++index)
            {
                _levelsSample[static_cast<std::size_t>(index)]->SetPressedVisual(state.levelsSample == index + 1);
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
            show(control, levels);
        }
        for (DxUi::Button* control : _levelsSample)
        {
            show(control, levels);
        }
        for (DxUi::Button* control : _levelsAuto)
        {
            show(control, levels);
        }
        show(_curveEditor, kind(FilterKind::Curves));
        show(_curveCaption, kind(FilterKind::Curves));
        show(_curveRemove, kind(FilterKind::Curves));
        show(_curveReset, kind(FilterKind::Curves));
        // Hue / Saturation.
        const bool spectrum = hsv && state.hsv.range != ColorRange::Master && !state.hsv.colorize;
        show(_hueRange, hsv);
        for (DxUi::Button* control : _hueSample)
        {
            show(control, spectrum);
        }
        show(_hueTargeting, hsv && !state.hsv.colorize);
        show(_spectrum, spectrum);
        show(_hueInvert, spectrum);
        show(_hueColorize, hsv);
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
                _hueSample[static_cast<std::size_t>(index)]->SetPressedVisual(state.hueSample == index + 1);
            }
            _hueTargeting->SetPressedVisual(state.targeting);
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
        show(_gradientRamp, map);
        show(_mapShadows, map);
        show(_mapShadowsLabel, map);
        show(_mapHighlights, map);
        show(_mapHighlightsLabel, map);
        show(_mapReverse, map);
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
        show(_noiseDistribution, kind(FilterKind::AddNoise));
        show(_noiseMono, kind(FilterKind::AddNoise));
        if (kind(FilterKind::AddNoise))
        {
            _noiseDistribution->SetSelectedIndex(settings.gaussian ? 1u : 0u);
            _noiseMono->SetChecked(settings.monochromatic);
        }
        showRow(AdjustRowId::Distortion, kind(FilterKind::LensCorrection), settings.distortion);
        show(_lensHint, kind(FilterKind::LensCorrection));
        show(_fillHint, kind(FilterKind::ContentAwareFill));
        show(_fillNoSource, kind(FilterKind::ContentAwareFill) && state.previewFailed);
        // Footer.
        show(_adjustPreview, true);
        _adjustPreview->SetChecked(state.preview);
        show(_adjustNote, state.limitedToSelection && !state.editsAdjustment);
        show(_adjustCancel, true);
        show(_adjustApply, true);
        _adjustApply->SetEnabled(!(kind(FilterKind::ContentAwareFill) && state.previewFailed));
        _view.Controls().SetDefaultButton(_adjustApply);
        _view.Controls().SetCancelButton(_adjustCancel);
    }
    else if (wasOpen)
    {
        _view.Controls().SetDefaultButton(nullptr);
        _view.Controls().SetCancelButton(nullptr);
        if (_curveEditor)
        {
            _curveEditor->ClearSelection();
        }
    }
    _syncingAdjust = false;
    _view.MarkDirty();
}

void ChromeHost::OpenAdjustColorPicker(uint32_t argb, bool highlights) noexcept
{
    if (!_picker || !_pickerCard)
    {
        return;
    }
    _pickerForAdjust = true;
    _pickerHighlights = highlights;
    _picker->SetColor(argb | 0xFF000000u);
    ArrangePicker();
    _pickerCard->SetVisible(true);
    _view.Controls().SetFocusControl(_picker);
    _view.MarkDirty();
}
