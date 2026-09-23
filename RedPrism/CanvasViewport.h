#pragma once

#include "Document.h"

#include <algorithm>
#include <cmath>

// The canvas hole's view of the document (Rendering_Composition.md, Viewport), ported from the legacy
// CanvasViewport.swift. View coordinates are device pixels relative to the hole's top-left corner (the Win32
// client area is physical pixels), `backingScale` is the window's DPI / 96, and `zoom` is device pixels per
// document pixel: zoom 1 shows actual pixels on any display (legacy "zoom 1 means actual display pixels"). Chrome
// constants are DIPs and go through `DipToView` / `DipToDocument`.
struct CanvasViewport
{
    Vec2 viewSize{};
    double backingScale = 1;
    double zoom = 1;
    Vec2 pan{};
    bool followsFit = true;

    static constexpr double kMinZoom = 0.001;
    static constexpr double kMaxZoom = 32;
    // From 200 % document pixels draw hard-edged (legacy EditorCanvas.crispZoom); the pixel grid from 800 %.
    static constexpr double kCrispZoom = 2;
    static constexpr double kPixelGridZoom = 8;
    // Fit leaves this margin in DIPs across each axis (legacy CanvasViewport.fit: 96 points).
    static constexpr double kFitMarginDip = 96;
    // One wheel notch (WHEEL_DELTA) zooms by this factor; a partial delta (a precision touchpad) by its fraction.
    static constexpr double kWheelNotch = 120;
    static constexpr double kWheelNotchZoom = 1.1;
    // The Zoom tool doubles the zoom for every 100 DIPs dragged right and halves it for 100 DIPs left.
    static constexpr double kZoomToolDoublingDip = 100;

    // Device pixels per document pixel: the one scale the blit, the overlays and pointer mapping share.
    [[nodiscard]] double ViewScale() const noexcept
    {
        return zoom;
    }

    // DIPs (chrome constants: hit radii, snap distances, margins) to device pixels and to document pixels.
    [[nodiscard]] double DipToView(double dip) const noexcept
    {
        return dip * std::max(1.0, backingScale);
    }
    [[nodiscard]] double DipToDocument(double dip) const noexcept
    {
        return DipToView(dip) / std::max(zoom, kMinZoom);
    }

    [[nodiscard]] Vec2 Center() const noexcept
    {
        return Vec2{viewSize.x / 2.0, viewSize.y / 2.0};
    }

    [[nodiscard]] double ClampZoom(double value) const noexcept
    {
        return std::min(kMaxZoom, std::max(kMinZoom, value));
    }

    void Fit(Vec2 documentSize) noexcept
    {
        if (viewSize.x <= 0 || viewSize.y <= 0 || documentSize.x <= 0 || documentSize.y <= 0)
        {
            followsFit = true;
            return;
        }
        // The legacy fit in points (view - 96, at least 1 point) times the backing scale, in device pixels.
        const double scale = std::max(1.0, backingScale);
        const double margin = DipToView(kFitMarginDip);
        zoom = ClampZoom(std::min(std::max(scale, viewSize.x - margin) / documentSize.x,
                                  std::max(scale, viewSize.y - margin) / documentSize.y));
        pan = {};
        followsFit = true;
    }

    // A new hole size or display scale. Zoom is device pixels per document pixel, so moving to another display
    // keeps it, and with it the pan in device pixels: the document point at the center stays put.
    void Resize(Vec2 size, double newScale, Vec2 documentSize) noexcept
    {
        viewSize = size;
        backingScale = std::max(1.0, newScale);
        if (followsFit)
        {
            Fit(documentSize);
        }
    }

    [[nodiscard]] Vec2 DocumentRectOrigin(Vec2 documentSize) const noexcept
    {
        const double scale = ViewScale();
        const Vec2 center = Center();
        return Vec2{center.x - documentSize.x * scale / 2.0 + pan.x, center.y - documentSize.y * scale / 2.0 + pan.y};
    }

    [[nodiscard]] Vec2 DocumentFromView(Vec2 view, Vec2 documentSize) const noexcept
    {
        const Vec2 origin = DocumentRectOrigin(documentSize);
        const double scale = ViewScale();
        return Vec2{(view.x - origin.x) / scale, (view.y - origin.y) / scale};
    }

    [[nodiscard]] Vec2 ViewFromDocument(Vec2 document, Vec2 documentSize) const noexcept
    {
        const Vec2 origin = DocumentRectOrigin(documentSize);
        const double scale = ViewScale();
        return Vec2{origin.x + document.x * scale, origin.y + document.y * scale};
    }

    void SetZoom(double value, Vec2 anchor, Vec2 documentSize) noexcept
    {
        if (!std::isfinite(value))
        {
            return;
        }
        const Vec2 pixel = DocumentFromView(anchor, documentSize);
        zoom = ClampZoom(value);
        const Vec2 moved = ViewFromDocument(pixel, documentSize);
        pan.x += anchor.x - moved.x;
        pan.y += anchor.y - moved.y;
        if (zoom == 1.0)
        {
            // Actual pixels: document pixels land on whole device pixels (never half a pixel off, which would blur
            // the 1:1 view and put a click between two document pixels); the anchor moves less than half a pixel.
            const Vec2 origin = DocumentRectOrigin(documentSize);
            pan.x += std::round(origin.x) - origin.x;
            pan.y += std::round(origin.y) - origin.y;
        }
        followsFit = false;
    }

    void Translate(Vec2 delta) noexcept
    {
        pan.x += delta.x;
        pan.y += delta.y;
        followsFit = false;
    }

    [[nodiscard]] bool SamplesCrisp() const noexcept
    {
        return zoom >= kCrispZoom;
    }

    [[nodiscard]] bool ShowsPixelGrid() const noexcept
    {
        return zoom >= kPixelGridZoom;
    }

    // The zoom factor of one WM_MOUSEWHEEL message: 1.1 per notch, a fraction of it for a partial delta, so the
    // many small deltas of a precision touchpad add up to the same zoom as the notches they sum to.
    [[nodiscard]] static double WheelZoomFactor(double wheelDelta) noexcept
    {
        if (!std::isfinite(wheelDelta))
        {
            return 1.0;
        }
        return std::pow(kWheelNotchZoom, wheelDelta / kWheelNotch);
    }

    // The Zoom tool's zoom after a horizontal drag of `dragDip` DIPs from the press, relative to the zoom at the
    // press (legacy EditorCanvas: `2^(dx / 100)`): proportional to the distance, whatever the pointer's rate.
    [[nodiscard]] static double DragZoomFactor(double dragDip) noexcept
    {
        if (!std::isfinite(dragDip))
        {
            return 1.0;
        }
        return std::pow(2.0, dragDip / kZoomToolDoublingDip);
    }
};
