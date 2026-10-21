#pragma once

#include "CanvasViewport.h"

namespace RedPrismTestViewport
{
// Synthetic document-pointer fixtures use actual pixels, with their document origin visible even when
// the document is larger than the canvas hole. This never changes an HWND, display or product viewport.
inline void FrameActualPixelsAtOrigin(CanvasViewport& viewport, Vec2 documentSize) noexcept
{
    viewport.SetZoom(1.0, viewport.Center(), documentSize);
    const Vec2 origin = viewport.DocumentRectOrigin(documentSize);
    viewport.Translate(Vec2{-origin.x, -origin.y});
}
} // namespace RedPrismTestViewport
