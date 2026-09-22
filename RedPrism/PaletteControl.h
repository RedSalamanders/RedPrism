#pragma once

#include <DxUi/DxUi.h>

#include <cstdint>
#include <functional>
#include <string>

// One rail palette: foreground and background swatches, a swap hot zone, and a black-and-white reset hot zone.
class PaletteZone final : public DxUi::Control
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
