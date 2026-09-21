#include "CanvasOverlay.h"

#include <algorithm>
#include <utility>

namespace
{
constexpr float kHandleHalfDip = 3.5f;
constexpr float kRotationRadiusDip = 4.0f;
constexpr float kShadowWidthDip = 3.0f;
constexpr float kLineWidthDip = 1.0f;
constexpr float kDraftShadowWidthDip = 2.0f;
constexpr float kDraftCornerHalfDip = 4.0f;
constexpr float kAntsDashDip = 4.0f;
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

void CanvasOverlay::SetSelectionOutline(std::vector<OutlineLoop> loops)
{
    _outline = std::move(loops);
    _outlineDirty = true;
    RequestInvalidate();
}

void CanvasOverlay::SetDraft(std::optional<Draft> draft)
{
    _draft = std::move(draft);
    RequestInvalidate();
}

void CanvasOverlay::SetBrushCursor(std::optional<BrushCursor> cursor) noexcept
{
    _brushCursor = cursor;
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
    // Outlines follow the document, which can run past the hole; nothing here may paint over other chrome.
    dc->PushAxisAlignedClip(bounds, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
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
    PaintSelection(dc, host, bounds);
    PaintDraft(dc, host, bounds);
    PaintBrushCursor(dc, host, bounds);
    PaintTransform(dc, host, bounds);
    dc->PopAxisAlignedClip();
}

bool CanvasOverlay::EnsureSelectionResources(ID2D1DeviceContext* dc) const noexcept
{
    wil::com_ptr_nothrow<ID2D1Factory> factory;
    dc->GetFactory(factory.put());
    if (!factory)
    {
        return false;
    }
    if (factory.get() != _factory.get())
    {
        // Geometry and stroke styles belong to the factory that made them.
        _factory = factory;
        _outlineGeometry.reset();
        _ants.reset();
        _outlineDirty = true;
    }
    if (!_ants)
    {
        const D2D1_STROKE_STYLE_PROPERTIES properties =
            D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_FLAT, D2D1_CAP_STYLE_FLAT, D2D1_CAP_STYLE_FLAT,
                                        D2D1_LINE_JOIN_MITER, 10.0f, D2D1_DASH_STYLE_CUSTOM, 0.0f);
        // Dash lengths are multiples of the stroke width: four DIP on, four off.
        const float dashes[] = {kAntsDashDip, kAntsDashDip};
        if (FAILED(_factory->CreateStrokeStyle(properties, dashes, 2, _ants.put())))
        {
            return false;
        }
    }
    if (_outlineDirty || !_outlineGeometry)
    {
        _outlineGeometry.reset();
        wil::com_ptr_nothrow<ID2D1PathGeometry> geometry;
        if (FAILED(_factory->CreatePathGeometry(geometry.put())))
        {
            return false;
        }
        wil::com_ptr_nothrow<ID2D1GeometrySink> sink;
        if (FAILED(geometry->Open(sink.put())))
        {
            return false;
        }
        for (const OutlineLoop& loop : _outline)
        {
            if (loop.size() < 2)
            {
                continue;
            }
            sink->BeginFigure(loop[0], D2D1_FIGURE_BEGIN_HOLLOW);
            sink->AddLines(loop.data() + 1, static_cast<UINT32>(loop.size() - 1));
            sink->EndFigure(D2D1_FIGURE_END_CLOSED);
        }
        if (FAILED(sink->Close()))
        {
            return false;
        }
        _outlineGeometry = std::move(geometry);
        _outlineDirty = false;
    }
    return true;
}

void CanvasOverlay::PaintSelection(ID2D1DeviceContext* dc, DxUi::ControlHost& host, const D2D1_RECT_F& bounds) const
{
    if (_outline.empty() || !EnsureSelectionResources(dc))
    {
        return;
    }
    // Marching ants: a white line under a black dash (static for now; the animation is a later phase).
    ID2D1SolidColorBrush* white = host.GetSolidBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f));
    ID2D1SolidColorBrush* black = host.GetSolidBrush(D2D1::ColorF(0.0f, 0.0f, 0.0f, 1.0f));
    D2D1_MATRIX_3X2_F previous{};
    dc->GetTransform(&previous);
    dc->SetTransform(D2D1::Matrix3x2F::Translation(bounds.left, bounds.top) * previous);
    dc->DrawGeometry(_outlineGeometry.get(), white, kLineWidthDip);
    dc->DrawGeometry(_outlineGeometry.get(), black, kLineWidthDip, _ants.get());
    dc->SetTransform(previous);
}

void CanvasOverlay::PaintDraft(ID2D1DeviceContext* dc, DxUi::ControlHost& host, const D2D1_RECT_F& bounds) const
{
    if (!_draft.has_value() || _draft->points.empty())
    {
        return;
    }
    const Draft& draft = _draft.value();
    const auto at = [&](D2D1_POINT_2F point) { return D2D1::Point2F(bounds.left + point.x, bounds.top + point.y); };
    ID2D1SolidColorBrush* shadow = host.GetSolidBrush(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.8f));
    ID2D1SolidColorBrush* white = host.GetSolidBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f));
    ID2D1SolidColorBrush* black = host.GetSolidBrush(D2D1::ColorF(0.0f, 0.0f, 0.0f, 1.0f));
    const bool marquee = draft.kind == LassoKind::Rectangle || draft.kind == LassoKind::Ellipse;
    // A dark line under a white one reads on any pixels.
    const auto stroke = [&](ID2D1Brush* brush, float width)
    {
        if (marquee && draft.points.size() == 4)
        {
            float minX = draft.points[0].x;
            float minY = draft.points[0].y;
            float maxX = minX;
            float maxY = minY;
            for (const D2D1_POINT_2F& point : draft.points)
            {
                minX = std::min(minX, point.x);
                minY = std::min(minY, point.y);
                maxX = std::max(maxX, point.x);
                maxY = std::max(maxY, point.y);
            }
            const D2D1_RECT_F box =
                D2D1::RectF(bounds.left + minX, bounds.top + minY, bounds.left + maxX, bounds.top + maxY);
            if (draft.kind == LassoKind::Ellipse)
            {
                dc->DrawEllipse(
                    D2D1::Ellipse(D2D1::Point2F((box.left + box.right) / 2.0f, (box.top + box.bottom) / 2.0f),
                                  (box.right - box.left) / 2.0f, (box.bottom - box.top) / 2.0f),
                    brush, width);
            }
            else
            {
                dc->DrawRectangle(box, brush, width);
            }
            return;
        }
        for (std::size_t index = 1; index < draft.points.size(); ++index)
        {
            dc->DrawLine(at(draft.points[index - 1]), at(draft.points[index]), brush, width);
        }
        if (draft.kind == LassoKind::Polygonal && draft.cursor.has_value())
        {
            dc->DrawLine(at(draft.points.back()), at(draft.cursor.value()), brush, width);
        }
    };
    stroke(shadow, kDraftShadowWidthDip);
    stroke(white, kLineWidthDip);
    if (draft.kind == LassoKind::Polygonal)
    {
        // The first corner: click it to close the outline.
        const D2D1_POINT_2F first = at(draft.points.front());
        const D2D1_RECT_F handle = D2D1::RectF(first.x - kDraftCornerHalfDip, first.y - kDraftCornerHalfDip,
                                               first.x + kDraftCornerHalfDip, first.y + kDraftCornerHalfDip);
        dc->FillRectangle(handle, white);
        dc->DrawRectangle(handle, black, kLineWidthDip);
    }
}

void CanvasOverlay::PaintBrushCursor(ID2D1DeviceContext* dc, DxUi::ControlHost& host, const D2D1_RECT_F& bounds) const
{
    if (!_brushCursor.has_value())
    {
        return;
    }
    const BrushCursor& cursor = _brushCursor.value();
    const D2D1_POINT_2F center = D2D1::Point2F(bounds.left + cursor.center.x, bounds.top + cursor.center.y);
    ID2D1SolidColorBrush* white = host.GetSolidBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f));
    ID2D1SolidColorBrush* black = host.GetSolidBrush(D2D1::ColorF(0.0f, 0.0f, 0.0f, 1.0f));
    if (cursor.radius < 2.0f)
    {
        // A tip too small to draw as a ring shows as a crosshair.
        constexpr float kArm = 5.0f;
        for (const auto& [brush, width] : {std::pair<ID2D1SolidColorBrush*, float>{white, 2.5f}, {black, 1.0f}})
        {
            dc->DrawLine(D2D1::Point2F(center.x - kArm, center.y), D2D1::Point2F(center.x + kArm, center.y), brush,
                         width);
            dc->DrawLine(D2D1::Point2F(center.x, center.y - kArm), D2D1::Point2F(center.x, center.y + kArm), brush,
                         width);
        }
        return;
    }
    // A white ring under a black one reads on any pixels (legacy BrushCursorOverlay).
    const D2D1_ELLIPSE ring = D2D1::Ellipse(center, cursor.radius, cursor.radius);
    dc->DrawEllipse(ring, white, 2.5f);
    dc->DrawEllipse(ring, black, 1.0f);
}

void CanvasOverlay::PaintTransform(ID2D1DeviceContext* dc, DxUi::ControlHost& host, const D2D1_RECT_F& bounds) const
{
    if (!_geometry.has_value())
    {
        return;
    }
    const Geometry& geometry = _geometry.value();
    const DxUi::ThemePalette& theme = host.GetTheme();
    ID2D1SolidColorBrush* accent = host.GetSolidBrush(theme.accent);
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
