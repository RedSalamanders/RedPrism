#pragma once

#include <DxUi/DxUi.h>

#include <windows.h>

namespace RedPrism
{
// The product chrome palette (Specs/UI/UI_ApplicationChrome.md, Theme). Dark matches the C1 clear color; light is
// DxUi's default light palette.
[[nodiscard]] inline DxUi::ThemePalette MakeRedPrismThemePalette(bool dark = true) noexcept
{
    DxUi::ThemePalette palette = DxUi::MakeDefaultThemePalette(dark);
    if (dark)
    {
        palette.windowBackground = D2D1::ColorF(0.118f, 0.118f, 0.133f, 1.0f);
        palette.surfaceBackground = D2D1::ColorF(0.16f, 0.16f, 0.18f, 1.0f);
    }
    palette.reducedMotion = false;
    DxUi::RefreshAccentVariants(palette, dark);
    return palette;
}

[[nodiscard]] inline D2D1_COLOR_F SystemColorF(int index) noexcept
{
    const COLORREF color = GetSysColor(index);
    return D2D1::ColorF(GetRValue(color) / 255.0f, GetGValue(color) / 255.0f, GetBValue(color) / 255.0f, 1.0f);
}

// Windows high contrast: every role takes the active high-contrast theme's system color, so the chrome reads with
// the contrast the user chose (accents become the highlight, borders and text the window text).
[[nodiscard]] inline DxUi::ThemePalette MakeHighContrastThemePalette() noexcept
{
    const D2D1_COLOR_F window = SystemColorF(COLOR_WINDOW);
    const D2D1_COLOR_F text = SystemColorF(COLOR_WINDOWTEXT);
    const D2D1_COLOR_F highlight = SystemColorF(COLOR_HIGHLIGHT);
    const D2D1_COLOR_F highlightText = SystemColorF(COLOR_HIGHLIGHTTEXT);
    const D2D1_COLOR_F button = SystemColorF(COLOR_BTNFACE);
    const D2D1_COLOR_F buttonText = SystemColorF(COLOR_BTNTEXT);
    const D2D1_COLOR_F gray = SystemColorF(COLOR_GRAYTEXT);
    const D2D1_COLOR_F hot = SystemColorF(COLOR_HOTLIGHT);
    const bool dark = 0.2126f * window.r + 0.7152f * window.g + 0.0722f * window.b < 0.5f;
    DxUi::ThemePalette palette = DxUi::MakeDefaultThemePalette(dark);
    palette.highContrast = true;
    palette.reducedMotion = false;
    palette.accent = highlight;
    DxUi::RefreshAccentVariants(palette, dark);
    palette.windowBackground = window;
    palette.surfaceBackground = window;
    palette.cardBackground = window;
    palette.overlayBackground = window;
    palette.overlayBorder = text;
    palette.headerBackground = button;
    palette.headerHovered = highlight;
    palette.headerPressed = highlight;
    palette.border = text;
    palette.gridLine = gray;
    palette.borderDefault = text;
    palette.borderStrong = text;
    palette.text = text;
    palette.subduedText = text;
    palette.disabledText = gray;
    palette.selectionFill = highlight;
    palette.selectionText = highlightText;
    palette.selectionInactiveFill = highlight;
    palette.toggleKnobFill = window;
    palette.toggleKnobCheckedFill = highlightText;
    palette.focusStroke = text;
    palette.hoverFill = highlight;
    palette.pressedFill = highlight;
    palette.accentHover = hot;
    palette.accentPressed = hot;
    palette.focusStrokeOuter = text;
    palette.focusStrokeInner = window;
    palette.buttonFill = button;
    palette.buttonBorder = buttonText;
    palette.buttonHotFill = highlight;
    palette.buttonPressedFill = highlight;
    palette.inputFill = window;
    palette.inputBorder = text;
    palette.scrollbarTrack = window;
    palette.scrollbarThumb = text;
    palette.scrollbarThumbHot = hot;
    return palette;
}

// Whether Windows' high contrast is on (Settings > Accessibility > Contrast themes).
[[nodiscard]] inline bool SystemHighContrast() noexcept
{
    HIGHCONTRASTW contrast{};
    contrast.cbSize = sizeof(contrast);
    return SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0) &&
           (contrast.dwFlags & HCF_HIGHCONTRASTON) != 0;
}

// Windows' "Choose your app mode": light when the Personalize key says so, dark otherwise.
[[nodiscard]] inline bool SystemAppsUseLightTheme() noexcept
{
    DWORD value = 0;
    DWORD size = sizeof(value);
    return RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                        L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &value, &size) == ERROR_SUCCESS &&
           value != 0;
}
} // namespace RedPrism
