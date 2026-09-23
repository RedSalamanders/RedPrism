#include "ChromeHost.h"

#include "Resources.h"
#include "resource.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <initializer_list>
#include <string>
#include <vector>

// The size cards (C16, Specs/UI/UI_ApplicationChrome.md "Size cards"): New Canvas, Canvas Size and Image Size float
// over the canvas hole like the adjustment panel; the Application owns the draft and mirrors it through SetSizeCard,
// and every control reports a change as one SizeAction with one value.

namespace
{
constexpr float kCardWidthDip = 384.0f;
constexpr float kCardPaddingDip = 12.0f;
constexpr float kRowDip = 28.0f;
constexpr float kRowGapDip = 6.0f;
constexpr float kMarginDip = 8.0f;
constexpr float kAnchorDip = 25.0f;
constexpr float kAnchorGapDip = 3.0f;
constexpr float kNoteDip = 20.0f;
constexpr float kButtonDip = 30.0f;

constexpr std::array<UINT, 9> kAnchorNameIds{
    IDS_SIZE_ANCHOR_TOP_LEFT,    IDS_SIZE_ANCHOR_TOP_CENTER,    IDS_SIZE_ANCHOR_TOP_RIGHT,
    IDS_SIZE_ANCHOR_MIDDLE_LEFT, IDS_SIZE_ANCHOR_CENTER,        IDS_SIZE_ANCHOR_MIDDLE_RIGHT,
    IDS_SIZE_ANCHOR_BOTTOM_LEFT, IDS_SIZE_ANCHOR_BOTTOM_CENTER, IDS_SIZE_ANCHOR_BOTTOM_RIGHT};

constexpr std::array<UINT, 4> kUnitIds{IDS_SIZE_UNIT_PIXELS, IDS_SIZE_UNIT_PERCENT, IDS_SIZE_UNIT_INCHES,
                                       IDS_SIZE_UNIT_CENTIMETERS};

// An uncompressed RGBA canvas as the card announces it.
[[nodiscard]] std::wstring ByteText(int width, int height)
{
    const double bytes = static_cast<double>(width) * static_cast<double>(height) * 4.0;
    if (bytes < 1024.0 * 1024.0)
    {
        return FormatStringResource(nullptr, IDS_SIZE_KILOBYTES, std::format(L"{:.0f}", bytes / 1024.0));
    }
    return FormatStringResource(nullptr, IDS_SIZE_MEGABYTES, std::format(L"{:.1f}", bytes / (1024.0 * 1024.0)));
}

[[nodiscard]] std::wstring UnitSuffix(int unit)
{
    switch (unit)
    {
    case 1:
        return LoadStringResource(nullptr, IDS_UNIT_PERCENT);
    case 2:
        return LoadStringResource(nullptr, IDS_UNIT_INCHES);
    case 3:
        return LoadStringResource(nullptr, IDS_UNIT_CENTIMETERS);
    default:
        return LoadStringResource(nullptr, IDS_UNIT_PIXELS);
    }
}
} // namespace

void ChromeHost::SetSizeHandler(std::function<void(SizeAction, double)> handler)
{
    _onSize = std::move(handler);
}

void ChromeHost::SetSizeColorHandler(std::function<void(ChromeEditPhase, uint32_t)> handler)
{
    _onSizeColor = std::move(handler);
}

bool ChromeHost::SizeCardOpen() const noexcept
{
    return _sizeCard != nullptr && _sizeCard->IsVisible();
}

void ChromeHost::BuildSizeCard(DxUi::Panel& root)
{
    _sizeCard = root.AddChild<DxUi::CardPanel>();
    _sizeCard->SetCornerRadius(8.0f);
    _sizeCard->SetVisible(false);
    const auto action = [this](SizeAction what, double value)
    {
        if (!_syncingSize && _onSize)
        {
            _onSize(what, value);
        }
    };
    const auto label = [this](UINT textId, DxUi::FontRole role)
    {
        DxUi::Label* control = _sizeCard->AddChild<DxUi::Label>(LoadStringResource(nullptr, textId));
        control->SetFontRole(role);
        return control;
    };
    const auto stepper = [this, action](UINT labelId, double minimum, double maximum, int decimals, SizeAction what)
    {
        DxUi::NumericStepper* control = _sizeCard->AddChild<DxUi::NumericStepper>();
        control->SetLabel(LoadStringResource(nullptr, labelId), 72.0f);
        control->SetAccessibleName(LoadStringResource(nullptr, labelId));
        control->SetMinimum(minimum);
        control->SetMaximum(maximum);
        control->SetStep(1.0);
        control->SetLargeStep(10.0);
        control->SetDecimals(static_cast<uint8_t>(decimals));
        control->SetOnChange(
            [action, what](DxUi::NumericStepperChange change)
            {
                // A typed number counts once it is committed (Enter, a step button, the focus leaving): a value
                // in the making would move the locked other axis through every keystroke.
                if (change.phase == DxUi::NumericStepperChangePhase::Commit)
                {
                    action(what, change.value);
                }
            });
        return control;
    };
    const auto combo = [this, action](UINT nameId, std::initializer_list<UINT> items, SizeAction what)
    {
        DxUi::ComboBox* control = _sizeCard->AddChild<DxUi::ComboBox>();
        control->SetAccessibleName(LoadStringResource(nullptr, nameId));
        std::vector<DxUi::ComboBox::Item> entries;
        std::size_t index = 0;
        for (const UINT id : items)
        {
            entries.push_back(DxUi::ComboBox::Item{std::to_wstring(index++), LoadStringResource(nullptr, id)});
        }
        control->SetItems(std::move(entries));
        control->SetSelectedIndex(0);
        control->SetOnSelectionChanged([action, what](size_t selected)
                                       { action(what, static_cast<double>(selected)); });
        return control;
    };
    const auto checkbox = [this, action](UINT labelId, SizeAction what)
    {
        DxUi::Checkbox* control = _sizeCard->AddChild<DxUi::Checkbox>(LoadStringResource(nullptr, labelId));
        control->SetOnToggled([action, what](bool value) { action(what, value ? 1.0 : 0.0); });
        return control;
    };
    const auto button = [this](UINT labelId, std::function<void()> onClick)
    {
        DxUi::Button* control = _sizeCard->AddChild<DxUi::Button>(LoadStringResource(nullptr, labelId));
        control->SetOnClick(std::move(onClick));
        return control;
    };
    _sizeTitle = label(IDS_SIZE_CANVAS_TITLE, DxUi::FontRole::BodyStrong);
    _sizeCurrent = label(IDS_SIZE_CURRENT, DxUi::FontRole::Body);
    _sizeBytes = label(IDS_SIZE_BYTES, DxUi::FontRole::Small);
    _sizeUnit = combo(IDS_SIZE_UNITS, {kUnitIds[0], kUnitIds[1], kUnitIds[2], kUnitIds[3]}, SizeAction::SetUnit);
    _sizeWidth = stepper(IDS_SIZE_WIDTH, -1.0e6, 1.0e6, 0, SizeAction::SetWidth);
    _sizeHeight = stepper(IDS_SIZE_HEIGHT, -1.0e6, 1.0e6, 0, SizeAction::SetHeight);
    _sizeRelative = checkbox(IDS_SIZE_RELATIVE, SizeAction::SetRelative);
    _sizeLocked = checkbox(IDS_SIZE_LOCK_ORIGINAL, SizeAction::SetLocked);
    _sizeResult = label(IDS_SIZE_NEW, DxUi::FontRole::Small);
    _sizeResult->SetMultiline(true);
    _sizeAnchorLabel = label(IDS_SIZE_ANCHOR, DxUi::FontRole::Body);
    for (std::size_t index = 0; index < _sizeAnchors.size(); ++index)
    {
        DxUi::Button* control = _sizeCard->AddChild<DxUi::Button>(std::wstring{});
        const std::wstring name = LoadStringResource(nullptr, kAnchorNameIds[index]);
        control->SetAccessibleName(name);
        control->SetTooltipText(name);
        control->SetOnClick([action, index] { action(SizeAction::SetAnchor, static_cast<double>(index)); });
        _sizeAnchors[index] = control;
    }
    _sizeAnchorName = label(IDS_SIZE_ANCHOR_CENTER, DxUi::FontRole::BodyStrong);
    _sizeAnchorHint = label(IDS_SIZE_ANCHOR_HINT, DxUi::FontRole::Small);
    _sizeAnchorHint->SetMultiline(true);
    _sizeExtension = combo(IDS_SIZE_EXTENSION,
                           {IDS_SIZE_EXT_TRANSPARENT, IDS_SIZE_EXT_FOREGROUND, IDS_SIZE_EXT_BACKGROUND,
                            IDS_SIZE_EXT_BLACK, IDS_SIZE_EXT_WHITE, IDS_SIZE_EXT_CUSTOM},
                           SizeAction::SetExtension);
    _sizeExtensionSwatch = _sizeCard->AddChild<DxUi::ColorSwatch>(0xFFFFFFFFu);
    _sizeExtensionSwatch->SetAccessibleName(LoadStringResource(nullptr, IDS_SIZE_EXTENSION_COLOR));
    _sizeExtensionSwatch->SetOnClick([action] { action(SizeAction::SetExtension, 5.0); });
    _sizeResolution = stepper(IDS_SIZE_RESOLUTION, 1.0, 9600.0, 2, SizeAction::SetResolution);
    _sizePpi = label(IDS_SIZE_PPI, DxUi::FontRole::Small);
    _sizeResample = checkbox(IDS_SIZE_RESAMPLE, SizeAction::SetResample);
    _sizeSampling = combo(IDS_SIZE_SAMPLING,
                          {IDS_TRANSFORM_SAMPLING_NEAREST, IDS_TRANSFORM_SAMPLING_SMOOTH, IDS_TRANSFORM_SAMPLING_HIGH},
                          SizeAction::SetSampling);
    _sizeNote = label(IDS_SIZE_RESAMPLE_NOTE, DxUi::FontRole::Small);
    _sizeNote->SetMultiline(true);
    _sizeOpen = button(IDS_SIZE_OPEN_PROJECT, [action] { action(SizeAction::OpenProject, 0.0); });
    _sizeImport = button(IDS_SIZE_IMPORT_IMAGE, [action] { action(SizeAction::ImportImage, 0.0); });
    _sizeCancel = button(IDS_TRANSFORM_CANCEL, [action] { action(SizeAction::Cancel, 0.0); });
    _sizeApply = button(IDS_ADJUST_OK, [action] { action(SizeAction::Apply, 0.0); });
    _sizeApply->SetPrimary(true);
    for (const std::unique_ptr<DxUi::Control>& child : _sizeCard->GetChildren())
    {
        child->SetVisible(false);
    }
}

void ChromeHost::ArrangeSizeCard(float right, float top, float bottom) noexcept
{
    if (!_sizeCard || !_sizeCard->IsVisible())
    {
        return;
    }
    const SizeCardState& state = _sizeState;
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
    place(_sizeTitle, 24.0f);
    switch (state.kind)
    {
    case SizeCardKind::NewCanvas:
        place(_sizeCurrent, kNoteDip);
        place(_sizeWidth, kRowDip);
        place(_sizeHeight, kRowDip);
        place(_sizeResult, kNoteDip);
        {
            const float buttonWidth = 110.0f;
            PlaceControl(_sizeOpen, D2D1::RectF(innerLeft, y, innerLeft + buttonWidth, y + kButtonDip));
            PlaceControl(_sizeImport, D2D1::RectF(innerLeft + buttonWidth + 6.0f, y, innerLeft + 2 * buttonWidth + 6.0f,
                                                  y + kButtonDip));
            y += kButtonDip + kRowGapDip;
        }
        break;
    case SizeCardKind::CanvasSize:
    {
        place(_sizeCurrent, kNoteDip);
        place(_sizeBytes, kNoteDip);
        place(_sizeUnit, kRowDip);
        place(_sizeWidth, kRowDip);
        place(_sizeHeight, kRowDip);
        place(_sizeRelative, 24.0f);
        place(_sizeLocked, 24.0f);
        place(_sizeResult, 36.0f);
        const float gridLeft = innerLeft;
        const float gridTop = y;
        PlaceControl(_sizeAnchorLabel,
                     D2D1::RectF(gridLeft, y, gridLeft + 3 * kAnchorDip + 2 * kAnchorGapDip, y + kNoteDip));
        const float cellsTop = gridTop + kNoteDip + kRowGapDip;
        for (std::size_t index = 0; index < _sizeAnchors.size(); ++index)
        {
            const float column = static_cast<float>(index % 3);
            const float row = static_cast<float>(index / 3);
            const float x = gridLeft + column * (kAnchorDip + kAnchorGapDip);
            const float cellY = cellsTop + row * (kAnchorDip + kAnchorGapDip);
            PlaceControl(_sizeAnchors[index], D2D1::RectF(x, cellY, x + kAnchorDip, cellY + kAnchorDip));
        }
        const float textLeft = gridLeft + 3 * kAnchorDip + 2 * kAnchorGapDip + 16.0f;
        PlaceControl(_sizeAnchorName, D2D1::RectF(textLeft, cellsTop, innerRight, cellsTop + kNoteDip));
        PlaceControl(_sizeAnchorHint,
                     D2D1::RectF(textLeft, cellsTop + kNoteDip + 2.0f, innerRight, cellsTop + kNoteDip + 2.0f + 54.0f));
        y = cellsTop + 3 * kAnchorDip + 2 * kAnchorGapDip + kRowGapDip;
        PlaceControl(_sizeExtension, D2D1::RectF(innerLeft, y, innerRight - 34.0f, y + kRowDip));
        PlaceControl(_sizeExtensionSwatch, D2D1::RectF(innerRight - kRowDip, y, innerRight, y + kRowDip));
        y += kRowDip + kRowGapDip;
        break;
    }
    case SizeCardKind::ImageSize:
        place(_sizeCurrent, kNoteDip);
        place(_sizeUnit, kRowDip);
        place(_sizeWidth, kRowDip);
        place(_sizeHeight, kRowDip);
        place(_sizeLocked, 24.0f);
        PlaceControl(_sizeResolution, D2D1::RectF(innerLeft, y, innerRight - 90.0f, y + kRowDip));
        PlaceControl(_sizePpi, D2D1::RectF(innerRight - 84.0f, y + 4.0f, innerRight, y + kRowDip));
        y += kRowDip + kRowGapDip;
        place(_sizeResample, 24.0f);
        if (state.resample)
        {
            place(_sizeSampling, kRowDip);
        }
        place(_sizeNote, 36.0f);
        place(_sizeResult, 36.0f);
        break;
    case SizeCardKind::None:
        break;
    }
    const float buttonWidth = 110.0f;
    PlaceControl(_sizeCancel, D2D1::RectF(innerLeft, y, innerLeft + 90.0f, y + kButtonDip));
    PlaceControl(_sizeApply, D2D1::RectF(innerRight - buttonWidth, y, innerRight, y + kButtonDip));
    y += kButtonDip + kCardPaddingDip;
    const float cardBottom = std::min(y, bottom - kMarginDip);
    PlaceControl(_sizeCard, D2D1::RectF(left, top + kMarginDip, left + width, cardBottom));
}

void ChromeHost::SetSizeCard(const SizeCardState& state) noexcept
{
    if (!_sizeCard)
    {
        return;
    }
    _syncingSize = true;
    const bool wasOpen = _sizeCard->IsVisible();
    _sizeState = state;
    const bool open = state.kind != SizeCardKind::None;
    ShowControl(_sizeCard, open);
    if (open)
    {
        const bool newCanvas = state.kind == SizeCardKind::NewCanvas;
        const bool canvas = state.kind == SizeCardKind::CanvasSize;
        const bool image = state.kind == SizeCardKind::ImageSize;
        // ShowControl (ChromeHost.cpp) already writes SetVisible only when it changes; a local re-implementation
        // without that check re-issued it for every control on every sync (every pan move re-syncs the card).
        _sizeTitle->SetText(LoadStringResource(nullptr, newCanvas ? IDS_SIZE_NEW_CANVAS_TITLE
                                                        : canvas  ? IDS_SIZE_CANVAS_TITLE
                                                                  : IDS_SIZE_IMAGE_TITLE));
        ShowControl(_sizeTitle, true);
        _sizeCurrent->SetText(
            newCanvas ? LoadStringResource(nullptr, IDS_SIZE_NEW_CANVAS_HINT)
                      : FormatStringResource(nullptr, IDS_SIZE_CURRENT, state.originalWidth, state.originalHeight));
        ShowControl(_sizeCurrent, true);
        ShowControl(_sizeBytes, canvas);
        if (canvas)
        {
            _sizeBytes->SetText(
                FormatStringResource(nullptr, IDS_SIZE_BYTES, ByteText(state.originalWidth, state.originalHeight)));
        }
        ShowControl(_sizeUnit, !newCanvas);
        if (!newCanvas)
        {
            // Without resampling only the print units make sense.
            std::vector<DxUi::ComboBox::Item> entries;
            const std::size_t first = image && !state.resample ? 2u : 0u;
            for (std::size_t index = first; index < kUnitIds.size(); ++index)
            {
                entries.push_back(
                    DxUi::ComboBox::Item{std::to_wstring(index), LoadStringResource(nullptr, kUnitIds[index])});
            }
            _sizeUnit->SetItems(std::move(entries));
            _sizeUnit->SetSelectedIndex(static_cast<size_t>(std::max(0, state.unit - static_cast<int>(first))));
            // The combo reports the list position it was clicked at, not the unit value: without resampling the
            // list starts at Inches (first == 2), so position 0 means unit 2, not unit 0. Re-map position -> unit
            // with the same offset the list was just built with (SetOnSelectionChanged only fires from a real
            // pick; a sync-driven SetSelectedIndex above is swallowed by _syncingSize like every other control here).
            _sizeUnit->SetOnSelectionChanged(
                [this, first](size_t selected)
                {
                    if (!_syncingSize && _onSize)
                    {
                        _onSize(SizeAction::SetUnit, static_cast<double>(selected + first));
                    }
                });
        }
        const int decimals = newCanvas || state.unit == 0 ? 0 : (state.unit == 1 ? 2 : 3);
        const std::wstring suffix = newCanvas ? LoadStringResource(nullptr, IDS_UNIT_PIXELS) : UnitSuffix(state.unit);
        for (DxUi::NumericStepper* control : {_sizeWidth, _sizeHeight})
        {
            control->SetDecimals(static_cast<uint8_t>(decimals));
            control->SetUnit(suffix, 26.0f);
            control->SetMinimum(newCanvas ? 1.0 : -1.0e6);
            control->SetMaximum(newCanvas ? static_cast<double>(kMaxCanvasSide) : 1.0e6);
            ShowControl(control, true);
        }
        _sizeWidth->SetValue(state.width);
        _sizeHeight->SetValue(state.height);
        ShowControl(_sizeRelative, canvas);
        _sizeRelative->SetChecked(state.relative);
        ShowControl(_sizeLocked, !newCanvas);
        _sizeLocked->SetText(LoadStringResource(nullptr, canvas ? IDS_SIZE_LOCK_ORIGINAL : IDS_SIZE_LOCK));
        _sizeLocked->SetChecked(state.locked);
        _sizeLocked->SetEnabled(!image || state.resample);
        if (newCanvas)
        {
            _sizeResult->SetText(
                LoadStringResource(nullptr, state.valid ? IDS_SIZE_TRANSPARENT_NOTE : IDS_SIZE_NEW_CANVAS_INVALID));
        }
        else if (canvas)
        {
            _sizeResult->SetText(state.valid ? FormatStringResource(nullptr, IDS_SIZE_NEW, state.resultWidth,
                                                                    state.resultHeight,
                                                                    ByteText(state.resultWidth, state.resultHeight))
                                             : LoadStringResource(nullptr, IDS_SIZE_CANVAS_INVALID));
        }
        else
        {
            _sizeResult->SetText(
                state.valid ? FormatStringResource(nullptr, IDS_SIZE_RESULT, state.resultWidth, state.resultHeight)
                            : LoadStringResource(nullptr, IDS_SIZE_IMAGE_INVALID));
        }
        ShowControl(_sizeResult, true);
        ShowControl(_sizeAnchorLabel, canvas);
        for (std::size_t index = 0; index < _sizeAnchors.size(); ++index)
        {
            ShowControl(_sizeAnchors[index], canvas);
            SetPressedVisualIfChanged(_sizeAnchors[index], _sizeAnchorPressed[index],
                                      canvas && static_cast<int>(index) == state.anchor);
        }
        ShowControl(_sizeAnchorName, canvas);
        ShowControl(_sizeAnchorHint, canvas);
        if (canvas)
        {
            _sizeAnchorName->SetText(
                LoadStringResource(nullptr, kAnchorNameIds[static_cast<std::size_t>(std::clamp(state.anchor, 0, 8))]));
            _sizeExtension->SetSelectedIndex(static_cast<size_t>(std::clamp(state.extension, 0, 5)));
            _sizeExtensionSwatch->SetSwatchValue(state.extensionArgb);
        }
        ShowControl(_sizeExtension, canvas);
        ShowControl(_sizeExtensionSwatch, canvas && state.extension == 5);
        ShowControl(_sizeResolution, image);
        ShowControl(_sizePpi, image);
        ShowControl(_sizeResample, image);
        ShowControl(_sizeSampling, image && state.resample);
        ShowControl(_sizeNote, image);
        if (image)
        {
            _sizeResolution->SetValue(state.resolution);
            _sizeResample->SetChecked(state.resample);
            _sizeSampling->SetSelectedIndex(static_cast<size_t>(std::clamp(state.sampling, 0, 2)));
            _sizeNote->SetText(
                LoadStringResource(nullptr, state.resample ? IDS_SIZE_RESAMPLE_NOTE : IDS_SIZE_NO_RESAMPLE_NOTE));
        }
        ShowControl(_sizeOpen, newCanvas);
        ShowControl(_sizeImport, newCanvas);
        ShowControl(_sizeCancel, true);
        ShowControl(_sizeApply, true);
        _sizeApply->SetText(LoadStringResource(nullptr, newCanvas ? IDS_SIZE_CREATE
                                                        : canvas  ? IDS_ADJUST_OK
                                                                  : IDS_SIZE_RESIZE));
        _sizeApply->SetEnabled(state.valid);
        _view.Controls().SetDefaultButton(_sizeApply);
        _view.Controls().SetCancelButton(_sizeCancel);
    }
    else if (wasOpen)
    {
        _view.Controls().SetDefaultButton(nullptr);
        _view.Controls().SetCancelButton(nullptr);
        if (_pickerForSize && _pickerCard)
        {
            _pickerForSize = false;
            _pickerCard->SetVisible(false);
        }
    }
    _syncingSize = false;
    _view.MarkDirty();
}

void ChromeHost::OpenSizeColorPicker(uint32_t argb) noexcept
{
    if (!_picker || !_pickerCard)
    {
        return;
    }
    _pickerForSize = true;
    _picker->SetColor(argb | 0xFF000000u);
    ArrangePicker();
    _pickerCard->SetVisible(true);
    _view.Controls().SetFocusControl(_picker);
    _view.MarkDirty();
}
