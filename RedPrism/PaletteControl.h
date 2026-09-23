#pragma once

#include <DxUi/DxUi.h>

#include <cstdint>
#include <functional>
#include <string>

// One rail palette: foreground and background swatches, a swap hot zone, and a black-and-white reset hot zone.
// Derives from DxUi::Button, not Control, purely so DxUi's internal accessibility walk recognizes it: pinned
// DxUi only exposes a control to UI Automation when it is a non-Default AccessibilityRole or one of a short list
// of concrete types (IsSemanticAccessibilityControl, DxUi.Accessibility.cpp), and AccessibilityRole has no
// button-like value to set instead. Paint, pointer and keyboard handling stay fully overridden below; Button's
// own chrome is never drawn and its own click/text machinery is never used (the constructor wires
// SetAccessibleInvoke to the zone's own _onClick instead, since Button's own click callback is never set).
// OnHoverChanged is overridden to skip Button's hover-chrome fade transition: PaletteZone::Paint reads only the
// boolean IsHovered(), never the animation progress, so the transition would only keep the chrome animating
// (and repainting) for its duration with nothing to show for it. IsFocusable stays true (unlike the drawn adjust
// controls next to it, PaletteZone was already a real click target before it derived from Button); OnMnemonic
// and OnMouseLeave are unaffected by the base class (Button's own _pressed, cleared on mouse-leave, is a
// separate field from PaletteZone's own _pressed below, and nothing ever wires a mnemonic to a PaletteZone).
class PaletteZone final : public DxUi::Button
{
  public:
    enum class Kind
    {
        Foreground,
        Background,
        Swap,
        Reset
    };

    PaletteZone(Kind kind, std::function<void()> onClick);

    void SetSwatch(std::uint32_t argb) noexcept;
    void SetOnClick(std::function<void()> onClick);

    void Paint(DxUi::ControlHost& host) const override;
    bool OnMouseDown(DxUi::ControlHost& host, D2D1_POINT_2F point, bool rightButton, UINT modifiers) override;
    bool OnMouseUp(DxUi::ControlHost& host, D2D1_POINT_2F point, bool rightButton, UINT modifiers) override;
    bool OnKeyDown(DxUi::ControlHost& host, UINT virtualKey, UINT modifiers) override;
    void OnCaptureLost(DxUi::ControlHost& host) override;
    void OnHoverChanged(DxUi::ControlHost& host, bool hovered) override
    {
        DxUi::Control::OnHoverChanged(host, hovered);
    }

  private:
    Kind _kind = Kind::Foreground;
    std::uint32_t _argb = 0xFF000000u;
    bool _pressed = false;
    std::function<void()> _onClick;
};

class PaletteControl final : public DxUi::Panel
{
  public:
    PaletteControl(std::wstring foregroundName, std::wstring backgroundName, std::wstring swapName,
                   std::wstring resetName);

    void SetHandlers(std::function<void()> onForeground, std::function<void()> onBackground,
                     std::function<void()> onSwap, std::function<void()> onReset);
    void SetForeground(std::uint32_t argb) noexcept;
    void SetBackground(std::uint32_t argb) noexcept;

    [[nodiscard]] DxUi::Control* ForegroundZone() const noexcept;
    [[nodiscard]] DxUi::Control* BackgroundZone() const noexcept;
    [[nodiscard]] DxUi::Control* SwapZone() const noexcept;
    [[nodiscard]] DxUi::Control* ResetZone() const noexcept;

    bool OnMouseDown(DxUi::ControlHost& host, D2D1_POINT_2F point, bool rightButton, UINT modifiers) override;
    bool OnMouseUp(DxUi::ControlHost& host, D2D1_POINT_2F point, bool rightButton, UINT modifiers) override;

  private:
    PaletteZone* _foreground = nullptr;
    PaletteZone* _background = nullptr;
    PaletteZone* _swap = nullptr;
    PaletteZone* _reset = nullptr;
};
