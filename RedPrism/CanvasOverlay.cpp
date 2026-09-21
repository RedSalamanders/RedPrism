#include "CanvasOverlay.h"

#include <utility>

namespace
{
constexpr float kHandleHalfDip = 3.5f;
constexpr float kRotationRadiusDip = 4.0f;
constexpr float kShadowWidthDip = 3.0f;
constexpr float kLineWidthDip = 1.0f;
} // namespace

void CanvasOverlay::SetGeometry(std::optional<Geometry> geometry) noexcept
{
    _geometry = geometry;
    RequestInvalidate();
}

void CanvasOverlay::SetGuides(std::vector<float> xs, std::vector<float> ys)
{
    _guideXs = std::move(xs);
    _guideYs = std::move(ys);
    RequestInvalidate();
}

D2D1_RECT_F CanvasOverlay::GetHitBounds() const noexcept
{
    return D2D1::RectF();
}

void CanvasOverlay::Paint(DxUi::ControlHost& host) const
{
    ID2D1DeviceContext* dc = host.GetDeviceContext();
    if (dc == nullptr)
    {
        return;
    }
    const D2D1_RECT_F bounds = GetBounds();
    const DxUi::ThemePalette& theme = host.GetTheme();
    ID2D1SolidColorBrush* accent = host.GetSolidBrush(theme.accent);
    // Snap guides run across the whole canvas hole.
    for (const float x : _guideXs)
    {
        dc->DrawLine(D2D1::Point2F(bounds.left + x, bounds.top), D2D1::Point2F(bounds.left + x, bounds.bottom), accent,
                     kLineWidthDip);
    }
    for (const float y : _guideYs)
    {
        dc->DrawLine(D2D1::Point2F(bounds.left, bounds.top + y), D2D1::Point2F(bounds.right, bounds.top + y), accent,
                     kLineWidthDip);
    }
    if (!_geometry.has_value())
    {
        return;
    }
    const Geometry& geometry = _geometry.value();
    const auto at = [&](D2D1_POINT_2F point) { return D2D1::Point2F(bounds.left + point.x, bounds.top + point.y); };
    ID2D1SolidColorBrush* shadow = host.GetSolidBrush(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.7f));
    ID2D1SolidColorBrush* white = host.GetSolidBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f));
    // A dark outline under the accent line keeps the box readable on any pixels.
    const auto strokeBox = [&](ID2D1Brush* brush, float width)
    {
        for (std::size_t side = 0; side < 4; ++side)
        {
            const std::size_t from = side * 2;
            const std::size_t to = ((side + 1) % 4) * 2;
            dc->DrawLine(at(geometry.handles[from]), at(geometry.handles[to]), brush, width);
        }
        if (geometry.showsRotation)
        {
            dc->DrawLine(at(geometry.handles[1]), at(geometry.rotation), brush, width);
        }
    };
    strokeBox(shadow, kShadowWidthDip);
    strokeBox(accent, kLineWidthDip);
    for (const D2D1_POINT_2F& handle : geometry.handles)
    {
        const D2D1_POINT_2F point = at(handle);
        const D2D1_RECT_F square = D2D1::RectF(point.x - kHandleHalfDip, point.y - kHandleHalfDip,
                                               point.x + kHandleHalfDip, point.y + kHandleHalfDip);
        dc->FillRectangle(square, white);
        dc->DrawRectangle(square, accent, kLineWidthDip);
    }
    if (geometry.showsRotation)
    {
        const D2D1_ELLIPSE knob = D2D1::Ellipse(at(geometry.rotation), kRotationRadiusDip, kRotationRadiusDip);
        dc->FillEllipse(knob, white);
        dc->DrawEllipse(knob, accent, kLineWidthDip);
    }
}
