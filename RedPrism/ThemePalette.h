#pragma once

#include <DxUi/DxUi.h>

namespace RedPrism
{
// Product chrome palette. Dark to match the C1 clear color; high-contrast and light variants stay local to RedPrism.
[[nodiscard]] inline DxUi::ThemePalette MakeRedPrismThemePalette() noexcept
{
    DxUi::ThemePalette palette = DxUi::MakeDefaultThemePalette(true);
    palette.windowBackground = D2D1::ColorF(0.118f, 0.118f, 0.133f, 1.0f);
    palette.surfaceBackground = D2D1::ColorF(0.16f, 0.16f, 0.18f, 1.0f);
    palette.reducedMotion = false;
    DxUi::RefreshAccentVariants(palette, true);
    return palette;
}
} // namespace RedPrism
