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

// Both caches hold Direct2D objects of the chrome's device: they live only while the chrome is attached to it.
// ChromeHost releases them before it replaces its device and before it detaches, while DxUi's graphics device
// still holds the Direct2D factory. Nothing may leave them for the static destructors at exit: d2d1 releases the
// factory from inside the Release that drops a resource's last reference to it, then leaves that factory's lock,
// which is freed memory by then (RF-1110; the access violations in d2d1.dll at process exit).
void ReleaseLucideIcons() noexcept;
// SVG documents currently held by both caches: zero after ReleaseLucideIcons.
[[nodiscard]] int LucideCachedDocumentCount() noexcept;

// An icon-only button that strokes one pinned SVG, for chrome that is not itself a tool (the rail's More button).
class LucideIconButton final : public DxUi::Button
{
  public:
    LucideIconButton(std::wstring tooltip, int resourceId);

    void Paint(DxUi::ControlHost& host) const override;

  private:
    int _resourceId = 0;
};
