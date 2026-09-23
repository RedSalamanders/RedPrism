#pragma once

#include "Adjust.h"
#include "Document.h"

#include <DxUi/DxUi.h>

#include <array>
#include <functional>
#include <optional>
#include <vector>

// The adjustment panel's drawn controls (C12, Specs/UI/UI_ApplicationChrome.md): a curve editor, a levels histogram
// with its input handles, the levels output bar, the Hue/Saturation spectrum bars and a gradient ramp. Each paints
// through the DxUi host, owns its pointer drags and reports changes through callbacks; the panel sets values back
// from the session, never inside a drag callback.

// CurveEditor, LevelsBar and SpectrumEditor below derive from DxUi::Button, not Control, purely for UI Automation:
// pinned DxUi only walks a control into its accessibility tree when it is a non-Default AccessibilityRole or one
// of a short list of concrete types (IsSemanticAccessibilityControl, DxUi.Accessibility.cpp), and
// AccessibilityRole has no drawn-control value to set instead. Each keeps its own fully custom Paint and
// pointer/keyboard handling; Button's own chrome, click and text machinery are never invoked. Three pieces of
// Button behavior they never had are explicitly suppressed rather than inherited: Button's constructor calls
// SetFocusable(true), which would make them Tab stops with no keyboard function, so each constructor calls
// SetFocusable(false); Button::OnHoverChanged starts a hover-chrome fade transition that these controls never
// paint, so overriding it to call Control::OnHoverChanged directly stops the chrome from animating (and
// repainting every frame) for the transition's duration just because the pointer is over one of them;
// Button::OnMnemonic calls Invoke, so overriding it to call Control::OnMnemonic directly keeps Alt+letter
// mnemonics a no-op the way they always were (Control::OnMnemonic itself already no-ops once non-focusable).
// GradientRamp is read-only (no pointer or keyboard handling of its own) and derives from DxUi::Label instead:
// Label is also on IsSemanticAccessibilityControl's list, is not focusable and its own HitTest always returns
// null (a pointer-interactive label-like control is expected to be a button), which fits a decorative strip
// better than a Button with a click that would do nothing. GetControlAccessibleName (DxUi.Accessibility.cpp)
// checks Control::GetAccessibleName first regardless of concrete type, so its explicitly-set accessible name is
// unaffected by the base class.

// Curves: a 256 x 256 map with the channel's points; click adds a point, dragging moves one (its x kept between its
// neighbors, the ends fixed), the selected point can be removed.
class CurveEditor final : public DxUi::Button
{
  public:
    CurveEditor()
    {
        SetFocusable(false);
    }
    void SetCurves(const CurvesSettings& settings, int channel) noexcept;
    [[nodiscard]] std::optional<std::size_t> SelectedIndex() const noexcept
    {
        return _selected;
    }
    void ClearSelection() noexcept
    {
        _selected.reset();
        _dragging.reset();
    }
    void SetOnChange(std::function<void(std::vector<CurvePoint>)> onChange)
    {
        _onChange = std::move(onChange);
    }
    void Paint(DxUi::ControlHost& host) const override;
    bool OnMouseDown(DxUi::ControlHost& host, D2D1_POINT_2F point, bool rightButton, UINT modifiers) override;
    bool OnMouseMove(DxUi::ControlHost& host, D2D1_POINT_2F point, UINT modifiers) override;
    bool OnMouseUp(DxUi::ControlHost& host, D2D1_POINT_2F point, bool rightButton, UINT modifiers) override;
    void OnCaptureLost(DxUi::ControlHost& host) override;
    void OnHoverChanged(DxUi::ControlHost& host, bool hovered) override
    {
        DxUi::Control::OnHoverChanged(host, hovered);
    }
    bool OnMnemonic(DxUi::ControlHost& host) override
    {
        return DxUi::Control::OnMnemonic(host);
    }

  private:
    [[nodiscard]] const std::vector<CurvePoint>& Points() const noexcept;
    [[nodiscard]] D2D1_POINT_2F Position(const CurvePoint& point) const noexcept;
    [[nodiscard]] CurvePoint Unposition(D2D1_POINT_2F point) const noexcept;
    void Drag(D2D1_POINT_2F point);
    CurvesSettings _settings;
    int _channel = 0;
    std::optional<std::size_t> _selected;
    std::optional<std::size_t> _dragging;
    std::function<void(std::vector<CurvePoint>)> _onChange;
};

// Levels: the histogram of the channel with the three input handles (black, gamma, white) under it, or the output
// gradient with its two handles. A drag reports the handle and its 0-255 position.
class LevelsBar final : public DxUi::Button
{
  public:
    LevelsBar()
    {
        SetFocusable(false);
    }
    enum class Kind : std::uint8_t
    {
        Input,  // histogram plus black / gamma / white handles
        Output, // black-to-white ramp plus output black / white handles
    };
    static constexpr float kHandleRowDip = 18.0f;
    void SetKind(Kind kind) noexcept
    {
        _kind = kind;
    }
    void SetHistogram(const std::array<double, 256>& bins, int channel) noexcept;
    void SetRange(const LevelRange& range) noexcept;
    // handle: 0 black, 1 gamma (input only), 2 white; value: the handle's 0-255 position.
    void SetOnHandle(std::function<void(int, double)> onHandle)
    {
        _onHandle = std::move(onHandle);
    }
    void Paint(DxUi::ControlHost& host) const override;
    bool OnMouseDown(DxUi::ControlHost& host, D2D1_POINT_2F point, bool rightButton, UINT modifiers) override;
    bool OnMouseMove(DxUi::ControlHost& host, D2D1_POINT_2F point, UINT modifiers) override;
    bool OnMouseUp(DxUi::ControlHost& host, D2D1_POINT_2F point, bool rightButton, UINT modifiers) override;
    void OnCaptureLost(DxUi::ControlHost& host) override;
    void OnHoverChanged(DxUi::ControlHost& host, bool hovered) override
    {
        DxUi::Control::OnHoverChanged(host, hovered);
    }
    bool OnMnemonic(DxUi::ControlHost& host) override
    {
        return DxUi::Control::OnMnemonic(host);
    }

  private:
    [[nodiscard]] std::vector<double> HandlePositions() const noexcept;
    [[nodiscard]] double ValueAt(float x) const noexcept;
    void Drag(D2D1_POINT_2F point);
    Kind _kind = Kind::Input;
    std::array<double, 256> _bins{};
    int _channel = 0;
    LevelRange _range;
    std::optional<int> _dragging;
    std::function<void(int, double)> _onHandle;
};

// Hue/Saturation: the hues as they are, the selected range's four handles, and the hues as the adjustment leaves
// them. Dragging a handle reports its index (0 falloff start, 1 range start, 2 range end, 3 falloff end) and degrees.
class SpectrumEditor final : public DxUi::Button
{
  public:
    SpectrumEditor()
    {
        SetFocusable(false);
    }
    void SetSettings(const HueSaturationSettings& settings) noexcept;
    void SetOnHandle(std::function<void(int, double)> onHandle)
    {
        _onHandle = std::move(onHandle);
    }
    // Whether `point` (control-local DIP) falls in the handle row between the two hue bars: legacy limits the
    // drag gesture to that strip (HueSaturationSheet.swift), and a click on either bar does nothing. Exposed so
    // it can be checked directly, since exercising OnMouseDown itself needs a live DxUi ControlHost.
    [[nodiscard]] static bool HandleStripContainsDip(const D2D1_RECT_F& bounds, D2D1_POINT_2F point) noexcept;
    void Paint(DxUi::ControlHost& host) const override;
    bool OnMouseDown(DxUi::ControlHost& host, D2D1_POINT_2F point, bool rightButton, UINT modifiers) override;
    bool OnMouseMove(DxUi::ControlHost& host, D2D1_POINT_2F point, UINT modifiers) override;
    bool OnMouseUp(DxUi::ControlHost& host, D2D1_POINT_2F point, bool rightButton, UINT modifiers) override;
    void OnCaptureLost(DxUi::ControlHost& host) override;
    void OnHoverChanged(DxUi::ControlHost& host, bool hovered) override
    {
        DxUi::Control::OnHoverChanged(host, hovered);
    }
    bool OnMnemonic(DxUi::ControlHost& host) override
    {
        return DxUi::Control::OnMnemonic(host);
    }

  private:
    [[nodiscard]] int NearestHandle(double degrees) const noexcept;
    void Drag(D2D1_POINT_2F point);
    HueSaturationSettings _settings;
    std::optional<int> _dragging;
    std::function<void(int, double)> _onHandle;
};

// Gradient Map: the ramp from the dark end to the light end, in the order they apply. Read-only (no pointer or
// keyboard handling of its own): the two swatches beside it, not the ramp, open the color picker. Derives from
// DxUi::Label rather than Button (see the block comment above): Label is not focusable and never hit-tests, both
// already true of a purely decorative strip, and its Paint is fully overridden below and never invoked.
class GradientRamp final : public DxUi::Label
{
  public:
    void SetEnds(const AdjustmentColor& dark, const AdjustmentColor& light) noexcept;
    void Paint(DxUi::ControlHost& host) const override;

  private:
    AdjustmentColor _dark{0, 0, 0};
    AdjustmentColor _light{1, 1, 1};
};
