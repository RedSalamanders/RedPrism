#pragma once

#include "Document.h"

#include <algorithm>
#include <cmath>

struct CanvasViewport
{
    Vec2 viewSize{};
    double backingScale = 1;
    double zoom = 1;
    Vec2 pan{};
    bool followsFit = true;

    static constexpr double kMinZoom = 0.001;
    static constexpr double kMaxZoom = 32;
    static constexpr double kPixelGridZoom = 8;

    [[nodiscard]] double PointsPerPixel() const noexcept
    {
        return zoom / std::max(1.0, backingScale);
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
        zoom = ClampZoom(std::min(std::max(1.0, viewSize.x - 96.0) / documentSize.x,
                                  std::max(1.0, viewSize.y - 96.0) / documentSize.y) *
                         std::max(1.0, backingScale));
        pan = {};
        followsFit = true;
    }

    void Resize(Vec2 size, double newScale, Vec2 documentSize) noexcept
    {
        const double old = PointsPerPixel();
        viewSize = size;
        backingScale = std::max(1.0, newScale);
        if (followsFit)
        {
            Fit(documentSize);
        }
        else if (old > 0)
        {
            const double ratio = PointsPerPixel() / old;
            pan.x *= ratio;
            pan.y *= ratio;
        }
    }

    [[nodiscard]] Vec2 DocumentRectOrigin(Vec2 documentSize) const noexcept
    {
        const double scale = PointsPerPixel();
        const Vec2 center = Center();
        return Vec2{center.x - documentSize.x * scale / 2.0 + pan.x, center.y - documentSize.y * scale / 2.0 + pan.y};
    }

    [[nodiscard]] Vec2 DocumentFromView(Vec2 view, Vec2 documentSize) const noexcept
    {
        const Vec2 origin = DocumentRectOrigin(documentSize);
        const double scale = PointsPerPixel();
        return Vec2{(view.x - origin.x) / scale, (view.y - origin.y) / scale};
    }

    [[nodiscard]] Vec2 ViewFromDocument(Vec2 document, Vec2 documentSize) const noexcept
    {
        const Vec2 origin = DocumentRectOrigin(documentSize);
        const double scale = PointsPerPixel();
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
        followsFit = false;
    }

    void Translate(Vec2 delta) noexcept
    {
        pan.x += delta.x;
        pan.y += delta.y;
        followsFit = false;
    }

    [[nodiscard]] bool ShowsPixelGrid() const noexcept
    {
        return zoom >= kPixelGridZoom;
    }
};
