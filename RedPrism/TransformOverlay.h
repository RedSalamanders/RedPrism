#pragma once

#include "CanvasViewport.h"
#include "Document.h"
#include "TransformEdit.h"

#include <array>
#include <cmath>
#include <optional>

// The Move tool's box, handles and rotation knob in view coordinates (canvas-hole device pixels), built from a
// document transform or a distortion's corners through the viewport. Hit-testing mirrors the legacy
// TransformOverlayGeometry: the rotation knob first, then the eight handles, then the four edges, all within the hit
// radius. The radius and the knob's offset are DIPs (the overlay draws in DIPs), scaled to device pixels by the
// viewport's backing scale so a handle hits where it is drawn on any display.
struct TransformHit
{
    TransformDrag::Mode mode = TransformDrag::Mode::Move;
    int handle = -1;
};

struct TransformOverlayGeometry
{
    static constexpr double kHitRadiusDip = 10.0;
    static constexpr double kRotationOffsetDip = 28.0;

    std::array<Vec2, 8> handles{};
    Vec2 rotationHandle{};
    double hitRadius = kHitRadiusDip; // device pixels
    // A distortion has no single rotation, so its rotation knob is hidden.
    bool showsRotation = true;

    [[nodiscard]] static TransformOverlayGeometry FromTransform(const LayerTransform& transform,
                                                                const CanvasViewport& viewport,
                                                                Vec2 documentSize) noexcept
    {
        TransformOverlayGeometry geometry;
        for (std::size_t index = 0; index < 8; ++index)
        {
            geometry.handles[index] =
                viewport.ViewFromDocument(transform.Point(LayerTransform::kHandles[index]), documentSize);
        }
        const double radians = transform.Radians();
        const double offset = viewport.DipToView(kRotationOffsetDip);
        geometry.rotationHandle = Vec2{geometry.handles[1].x + std::sin(radians) * offset,
                                       geometry.handles[1].y - std::cos(radians) * offset};
        geometry.hitRadius = viewport.DipToView(kHitRadiusDip);
        geometry.showsRotation = true;
        return geometry;
    }

    [[nodiscard]] static TransformOverlayGeometry FromCorners(const std::array<Vec2, 4>& corners,
                                                              const CanvasViewport& viewport,
                                                              Vec2 documentSize) noexcept
    {
        std::array<Vec2, 4> view{};
        for (std::size_t index = 0; index < 4; ++index)
        {
            view[index] = viewport.ViewFromDocument(corners[index], documentSize);
        }
        const auto middle = [](Vec2 a, Vec2 b) { return Vec2{(a.x + b.x) / 2.0, (a.y + b.y) / 2.0}; };
        TransformOverlayGeometry geometry;
        geometry.handles = {view[0], middle(view[0], view[1]), view[1], middle(view[1], view[2]),
                            view[2], middle(view[2], view[3]), view[3], middle(view[3], view[0])};
        geometry.rotationHandle = geometry.handles[1];
        geometry.hitRadius = viewport.DipToView(kHitRadiusDip);
        geometry.showsRotation = false;
        return geometry;
    }

    [[nodiscard]] std::optional<TransformHit> Hit(Vec2 point) const noexcept
    {
        const auto nearHandle = [&](Vec2 other)
        { return std::hypot(point.x - other.x, point.y - other.y) <= hitRadius; };
        if (showsRotation && nearHandle(rotationHandle))
        {
            return TransformHit{TransformDrag::Mode::Rotate, -1};
        }
        for (int index = 0; index < 8; ++index)
        {
            if (nearHandle(handles[static_cast<std::size_t>(index)]))
            {
                return TransformHit{TransformDrag::Mode::Resize, index};
            }
        }
        constexpr std::array<std::array<int, 3>, 4> kEdges{{{0, 2, 1}, {2, 4, 3}, {4, 6, 5}, {6, 0, 7}}};
        for (const std::array<int, 3>& edge : kEdges)
        {
            const Vec2 a = handles[static_cast<std::size_t>(edge[0])];
            const Vec2 b = handles[static_cast<std::size_t>(edge[1])];
            const double dx = b.x - a.x;
            const double dy = b.y - a.y;
            const double lengthSquared = dx * dx + dy * dy;
            if (lengthSquared <= 0.0)
            {
                continue;
            }
            const double t = ((point.x - a.x) * dx + (point.y - a.y) * dy) / lengthSquared;
            if (t >= 0.0 && t <= 1.0 && std::hypot(point.x - a.x - t * dx, point.y - a.y - t * dy) <= hitRadius)
            {
                return TransformHit{TransformDrag::Mode::Resize, edge[2]};
            }
        }
        return std::nullopt;
    }

    // The resize cursor for a handle as a direction in eighths of a turn: 0 horizontal (west-east), 1 north-west to
    // south-east, 2 vertical, 3 north-east to south-west, following the box's own rotation.
    [[nodiscard]] int ResizeCursorDirection(int index) const noexcept
    {
        constexpr double kPi = 3.14159265358979323846;
        constexpr std::array<double, 8> kOffsets{kPi / 4, kPi / 2, 3 * kPi / 4, 0, kPi / 4, kPi / 2, 3 * kPi / 4, 0};
        if (index < 0 || index > 7)
        {
            return 0;
        }
        const double angle = std::atan2(handles[2].y - handles[0].y, handles[2].x - handles[0].x);
        const long steps = std::lround((angle + kOffsets[static_cast<std::size_t>(index)]) / (kPi / 4));
        return static_cast<int>(((steps % 4) + 4) % 4);
    }
};
