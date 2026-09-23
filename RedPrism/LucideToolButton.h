#pragma once

#include "EditorTool.h"

#include <DxUi/DxUi.h>

#include <cstddef>

// A tool-rail button that strokes the pinned Lucide SVG. Segoe Fluent Icons remain the fallback glyph.
class LucideToolButton final : public DxUi::Button
{
  public:
    LucideToolButton(std::wstring tooltip, EditorTool tool, bool fluentFallback);

    void Paint(DxUi::ControlHost& host) const override;

  private:
    EditorTool _tool = EditorTool::Move;
    bool _fluentFallback = true;
    mutable bool _svgFailed = false;
};

// How many distinct tool icons have drawn, and how many have a cached SVG document ready. A tool hidden above
// the palette wells is prepared but not drawn.
[[nodiscard]] int LucideIconsDrawn() noexcept;
[[nodiscard]] int LucideIconsPrepared() noexcept;
// Total ID2D1SvgDocument creations across both caches in this process (LucideToolButton's per-tool cache and
// DrawLucideSvg's small shared one), for a steady-state cost measurement: a settled rail should stop growing
// this after its first frame, and a tool switch should grow it by at most two (the tool that lost the latch,
// the one that gained it).
[[nodiscard]] int LucideSvgCreateCount() noexcept;

// Strokes one pinned SVG into bounds, in color. Used by the palette hot zones as well as the rail.
bool DrawLucideSvg(ID2D1DeviceContext* context, int resourceId, const D2D1_RECT_F& bounds, const D2D1_COLOR_F& color);

// An icon-only button that strokes one pinned SVG, for chrome that is not itself a tool (the rail's More button).
class LucideIconButton final : public DxUi::Button
{
  public:
    LucideIconButton(std::wstring tooltip, int resourceId);

    void Paint(DxUi::ControlHost& host) const override;

  private:
    int _resourceId = 0;
};
