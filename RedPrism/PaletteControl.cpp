#include "PaletteControl.h"

#include "LucideToolButton.h"
#include "resource.h"

#include <d2d1.h>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/com.h>
#pragma warning(pop)

namespace
{
[[nodiscard]] bool Contains(const D2D1_RECT_F& bounds, D2D1_POINT_2F point) noexcept
{
    return point.x >= bounds.left && point.x < bounds.right && point.y >= bounds.top && point.y < bounds.bottom;
}

[[nodiscard]] D2D1_COLOR_F FromArgb(std::uint32_t argb) noexcept
{
    return D2D1::ColorF(static_cast<float>((argb >> 16) & 0xFFu) / 255.0f,
                        static_cast<float>((argb >> 8) & 0xFFu) / 255.0f, static_cast<float>(argb & 0xFFu) / 255.0f,
                        1.0f);
}

[[nodiscard]] wil::com_ptr_nothrow<ID2D1SolidColorBrush> MakeBrush(ID2D1DeviceContext* context, D2D1_COLOR_F color)
{
    wil::com_ptr_nothrow<ID2D1SolidColorBrush> brush;
    if (context == nullptr || FAILED(context->CreateSolidColorBrush(color, brush.put())))
    {
        brush.reset();
    }
    return brush;
}

void Invoke(const std::function<void()>& onClick)
{
    const std::function<void()> callback = onClick;
    if (callback)
    {
        callback();
    }
}
} // namespace

PaletteZone::PaletteZone(Kind kind, std::function<void()> onClick) : _kind(kind), _onClick(std::move(onClick))
{
    SetFocusable(true);
}

void PaletteZone::SetSwatch(std::uint32_t argb) noexcept
{
    argb |= 0xFF000000u;
    if (_argb == argb)
    {
        return;
    }
    _argb = argb;
    RequestInvalidate();
}

void PaletteZone::SetOnClick(std::function<void()> onClick)
{
    _onClick = std::move(onClick);
}

void PaletteZone::Paint(DxUi::ControlHost& host) const
{
    ID2D1DeviceContext* context = host.GetDeviceContext();
    if (context == nullptr)
    {
        return;
    }
    const D2D1_RECT_F bounds = GetBounds();
    const DxUi::ThemePalette& theme = host.GetTheme();
    const bool hot = _kind == Kind::Swap || _kind == Kind::Reset;
    if (hot)
    {
        if (IsHovered() || _pressed)
        {
            D2D1_COLOR_F wash = theme.text;
            wash.a = _pressed ? 0.28f : 0.16f;
            if (wil::com_ptr_nothrow<ID2D1SolidColorBrush> brush = MakeBrush(context, wash))
            {
                const D2D1_ROUNDED_RECT plate{bounds, 3.0f, 3.0f};
                context->FillRoundedRectangle(plate, brush.get());
            }
        }
        const D2D1_COLOR_F ink = IsEnabled() ? theme.text : theme.disabledText;
        const int resourceId = _kind == Kind::Swap ? IDR_LUCIDE_MOVE_DIAGONAL_2 : IDR_LUCIDE_ROTATE_CCW;
        DrawLucideSvg(context, resourceId, bounds, ink);
    }
    else
    {
        wil::com_ptr_nothrow<ID2D1SolidColorBrush> fill = MakeBrush(context, FromArgb(_argb));
        D2D1_COLOR_F border = theme.text;
        border.a = (IsHovered() || _pressed) ? 1.0f : 0.8f;
        wil::com_ptr_nothrow<ID2D1SolidColorBrush> stroke = MakeBrush(context, border);
        const D2D1_RECT_F inset{bounds.left + 0.5f, bounds.top + 0.5f, bounds.right - 0.5f, bounds.bottom - 0.5f};
        const D2D1_ROUNDED_RECT plate{inset, 2.0f, 2.0f};
        if (fill)
        {
            context->FillRoundedRectangle(plate, fill.get());
        }
        if (stroke)
        {
            context->DrawRoundedRectangle(plate, stroke.get(), 1.0f);
        }
    }

    if (HasFocus() && host.IsKeyboardFocusVisible())
    {
        if (wil::com_ptr_nothrow<ID2D1SolidColorBrush> accent = MakeBrush(context, theme.accent))
        {
            const D2D1_ROUNDED_RECT ring{bounds, 3.0f, 3.0f};
            context->DrawRoundedRectangle(ring, accent.get(), 1.0f);
        }
    }
}

bool PaletteZone::OnMouseDown(DxUi::ControlHost& host, D2D1_POINT_2F point, bool rightButton, UINT /*modifiers*/)
{
    if (!IsEnabled() || rightButton || !Contains(GetHitBounds(), point))
    {
        return false;
    }
    host.SetFocusControl(this);
    _pressed = true;
    Invalidate(host);
    return true;
}

bool PaletteZone::OnMouseUp(DxUi::ControlHost& host, D2D1_POINT_2F point, bool rightButton, UINT /*modifiers*/)
{
    if (!IsEnabled() || rightButton)
    {
        return false;
    }
    const bool wasPressed = _pressed;
    _pressed = false;
    Invalidate(host);
    if (wasPressed && Contains(GetHitBounds(), point))
    {
        Invoke(_onClick);
    }
    return wasPressed;
}

bool PaletteZone::OnKeyDown(DxUi::ControlHost& host, UINT virtualKey, UINT /*modifiers*/)
{
    if (!IsEnabled() || (virtualKey != VK_SPACE && virtualKey != VK_RETURN))
    {
        return false;
    }
    Invoke(_onClick);
    Invalidate(host);
    return true;
}

void PaletteZone::OnCaptureLost(DxUi::ControlHost& host)
{
    if (_pressed)
    {
        _pressed = false;
        Invalidate(host);
    }
}

PaletteControl::PaletteControl(std::wstring foregroundName, std::wstring backgroundName, std::wstring swapName,
                               std::wstring resetName)
{
    SetAccessibleName(foregroundName + L", " + backgroundName);
    _background = AddChild<PaletteZone>(PaletteZone::Kind::Background, nullptr);
    _foreground = AddChild<PaletteZone>(PaletteZone::Kind::Foreground, nullptr);
    _reset = AddChild<PaletteZone>(PaletteZone::Kind::Reset, nullptr);
    _swap = AddChild<PaletteZone>(PaletteZone::Kind::Swap, nullptr);
    _background->SetAccessibleName(backgroundName);
    _foreground->SetAccessibleName(foregroundName);
    _reset->SetAccessibleName(resetName);
    _swap->SetAccessibleName(swapName);
    _background->SetTooltipText(std::move(backgroundName));
    _foreground->SetTooltipText(std::move(foregroundName));
    _reset->SetTooltipText(std::move(resetName));
    _swap->SetTooltipText(std::move(swapName));
    _background->SetSwatch(0xFFFFFFFFu);
    _foreground->SetSwatch(0xFF000000u);
}

void PaletteControl::SetHandlers(std::function<void()> onForeground, std::function<void()> onBackground,
                                 std::function<void()> onSwap, std::function<void()> onReset)
{
    if (_foreground != nullptr)
    {
        _foreground->SetOnClick(std::move(onForeground));
    }
    if (_background != nullptr)
    {
        _background->SetOnClick(std::move(onBackground));
    }
    if (_swap != nullptr)
    {
        _swap->SetOnClick(std::move(onSwap));
    }
    if (_reset != nullptr)
    {
        _reset->SetOnClick(std::move(onReset));
    }
}

void PaletteControl::SetForeground(std::uint32_t argb) noexcept
{
    if (_foreground != nullptr)
    {
        _foreground->SetSwatch(argb);
    }
}

void PaletteControl::SetBackground(std::uint32_t argb) noexcept
{
    if (_background != nullptr)
    {
        _background->SetSwatch(argb);
    }
}

DxUi::Control* PaletteControl::ForegroundZone() const noexcept
{
    return _foreground;
}

DxUi::Control* PaletteControl::BackgroundZone() const noexcept
{
    return _background;
}

DxUi::Control* PaletteControl::SwapZone() const noexcept
{
    return _swap;
}

DxUi::Control* PaletteControl::ResetZone() const noexcept
{
    return _reset;
}

bool PaletteControl::OnMouseDown(DxUi::ControlHost& /*host*/, D2D1_POINT_2F /*point*/, bool rightButton,
                                 UINT /*modifiers*/)
{
    return !rightButton;
}

bool PaletteControl::OnMouseUp(DxUi::ControlHost& /*host*/, D2D1_POINT_2F /*point*/, bool rightButton,
                               UINT /*modifiers*/)
{
    return !rightButton;
}
