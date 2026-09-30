#include "AdjustControls.h"

#include <algorithm>
#include <cmath>

namespace
{
constexpr float kPointRadiusDip = 4.0f;
constexpr float kHandleHalfDip = 7.0f;
constexpr double kPointPickDistance = 14.0; // in curve units (0-255)

// SpectrumEditor's three rows: the hues as they are, the band handles, the hues as they leave (kSpectrumGapDip
// between each). Legacy (HueSaturationSheet.swift's `handles` view) attaches its DragGesture to only the middle
// row, so a click on either hue bar does nothing; RedPrism matches that with SpectrumEditor::HandleStripContainsDip.
constexpr float kSpectrumBarDip = 16.0f;
constexpr float kSpectrumGapDip = 5.0f;
constexpr float kSpectrumHandleStripDip = 12.0f;

[[nodiscard]] float SpectrumHandleTopDip(const D2D1_RECT_F& bounds) noexcept
{
    return bounds.top + kSpectrumBarDip + kSpectrumGapDip;
}

[[nodiscard]] D2D1_COLOR_F HueColor(double hue) noexcept
{
    // Saturation and brightness 1: the spectrum bars.
    const double h = std::fmod(std::fmod(hue, 360.0) + 360.0, 360.0) / 60.0;
    const double x = 1.0 - std::fabs(std::fmod(h, 2.0) - 1.0);
    double r = 0;
    double g = 0;
    double b = 0;
    switch (static_cast<int>(h))
    {
    case 0:
        r = 1;
        g = x;
        break;
    case 1:
        r = x;
        g = 1;
        break;
    case 2:
        g = 1;
        b = x;
        break;
    case 3:
        g = x;
        b = 1;
        break;
    case 4:
        r = x;
        b = 1;
        break;
    default:
        r = 1;
        b = x;
        break;
    }
    return D2D1::ColorF(static_cast<float>(r), static_cast<float>(g), static_cast<float>(b), 1.0f);
}

[[nodiscard]] D2D1_COLOR_F ChannelColor(int channel) noexcept
{
    switch (channel)
    {
    case 1:
        return D2D1::ColorF(0.85f, 0.25f, 0.25f, 0.9f);
    case 2:
        return D2D1::ColorF(0.25f, 0.7f, 0.3f, 0.9f);
    case 3:
        return D2D1::ColorF(0.25f, 0.45f, 0.9f, 0.9f);
    default:
        return D2D1::ColorF(0.55f, 0.55f, 0.55f, 0.9f);
    }
}

void FillTriangle(ID2D1DeviceContext* dc, float x, float top, float half, float height, ID2D1SolidColorBrush* fill,
                  ID2D1SolidColorBrush* stroke) noexcept
{
    // A small upward-pointing triangle, drawn as three lines around its filled center: no geometry objects needed.
    const D2D1_POINT_2F apex = D2D1::Point2F(x, top);
    const D2D1_POINT_2F left = D2D1::Point2F(x - half, top + height);
    const D2D1_POINT_2F right = D2D1::Point2F(x + half, top + height);
    for (float scan = 0; scan <= height; scan += 1.0f)
    {
        const float spread = half * scan / height;
        dc->DrawLine(D2D1::Point2F(x - spread, top + scan), D2D1::Point2F(x + spread, top + scan), fill, 1.0f);
    }
    dc->DrawLine(apex, left, stroke, 1.0f);
    dc->DrawLine(left, right, stroke, 1.0f);
    dc->DrawLine(right, apex, stroke, 1.0f);
}
} // namespace

// ── CurveEditor ───────────────────────────────────────────────────────────

void CurveEditor::SetCurves(const CurvesSettings& settings, int channel) noexcept
{
    const int clamped = std::clamp(channel, 0, 3);
    if (clamped != _channel)
    {
        _selected.reset();
        _dragging.reset();
    }
    _settings = settings;
    _channel = clamped;
    if (_selected.has_value() && _selected.value() >= Points().size())
    {
        _selected.reset();
    }
}

const std::vector<CurvePoint>& CurveEditor::Points() const noexcept
{
    return _settings.channels[static_cast<std::size_t>(_channel)];
}

D2D1_POINT_2F CurveEditor::Position(const CurvePoint& point) const noexcept
{
    const D2D1_RECT_F bounds = GetBounds();
    const float width = std::max(1.0f, bounds.right - bounds.left);
    const float height = std::max(1.0f, bounds.bottom - bounds.top);
    return D2D1::Point2F(bounds.left + static_cast<float>(point.x / 255.0) * width,
                         bounds.top + static_cast<float>(1.0 - point.y / 255.0) * height);
}

CurvePoint CurveEditor::Unposition(D2D1_POINT_2F point) const noexcept
{
    const D2D1_RECT_F bounds = GetBounds();
    const float width = std::max(1.0f, bounds.right - bounds.left);
    const float height = std::max(1.0f, bounds.bottom - bounds.top);
    return CurvePoint{
        std::min(255.0, std::max(0.0, static_cast<double>((point.x - bounds.left) / width) * 255.0)),
        std::min(255.0, std::max(0.0, 255.0 - static_cast<double>((point.y - bounds.top) / height) * 255.0))};
}

void CurveEditor::Paint(DxUi::ControlHost& host) const
{
    ID2D1DeviceContext* dc = host.GetDeviceContext();
    if (dc == nullptr)
    {
        return;
    }
    const D2D1_RECT_F bounds = GetBounds();
    const DxUi::ThemePalette& theme = host.GetTheme();
    dc->FillRectangle(bounds, host.GetSolidBrush(theme.inputFill));
    ID2D1SolidColorBrush* grid = host.GetSolidBrush(theme.gridLine);
    const float width = bounds.right - bounds.left;
    const float height = bounds.bottom - bounds.top;
    for (int i = 0; i <= 4; ++i)
    {
        const float f = static_cast<float>(i) / 4.0f;
        dc->DrawLine(D2D1::Point2F(bounds.left + f * width, bounds.top),
                     D2D1::Point2F(bounds.left + f * width, bounds.bottom), grid, 1.0f);
        dc->DrawLine(D2D1::Point2F(bounds.left, bounds.top + f * height),
                     D2D1::Point2F(bounds.right, bounds.top + f * height), grid, 1.0f);
    }
    dc->DrawRectangle(bounds, host.GetSolidBrush(theme.inputBorder), 1.0f);
    ID2D1SolidColorBrush* line = host.GetSolidBrush(theme.text);
    D2D1_POINT_2F previous = Position(CurvePoint{0, Adjust::CurveValue(_settings, 0, _channel)});
    for (int x = 1; x <= 255; ++x)
    {
        const D2D1_POINT_2F next =
            Position(CurvePoint{static_cast<double>(x), Adjust::CurveValue(_settings, x, _channel)});
        dc->DrawLine(previous, next, line, 2.0f);
        previous = next;
    }
    ID2D1SolidColorBrush* accent = host.GetSolidBrush(theme.accent);
    ID2D1SolidColorBrush* plain = host.GetSolidBrush(theme.cardBackground);
    const std::vector<CurvePoint>& points = Points();
    for (std::size_t index = 0; index < points.size(); ++index)
    {
        const D2D1_POINT_2F center = Position(points[index]);
        const D2D1_ELLIPSE ellipse = D2D1::Ellipse(center, kPointRadiusDip, kPointRadiusDip);
        dc->FillEllipse(ellipse, _selected == index ? accent : plain);
        dc->DrawEllipse(ellipse, line, 1.0f);
    }
    if (!IsEnabled())
    {
        D2D1_COLOR_F veil = theme.cardBackground;
        veil.a = 0.55f;
        dc->FillRectangle(bounds, host.GetSolidBrush(veil));
        dc->DrawRectangle(bounds, host.GetSolidBrush(theme.disabledText), 1.0f);
    }
}

bool CurveEditor::OnMouseDown(DxUi::ControlHost& host, D2D1_POINT_2F point, bool rightButton, UINT)
{
    if (rightButton || !IsEnabled())
    {
        return false;
    }
    const CurvePoint at = Unposition(point);
    const std::vector<CurvePoint>& points = Points();
    std::optional<std::size_t> nearest;
    double best = kPointPickDistance;
    for (std::size_t index = 0; index < points.size(); ++index)
    {
        const double distance = std::hypot(points[index].x - at.x, points[index].y - at.y);
        if (distance < best)
        {
            best = distance;
            nearest = index;
        }
    }
    if (nearest.has_value())
    {
        _dragging = nearest;
    }
    else if (points.size() < 32 && at.x > 1 && at.x < 254)
    {
        for (const CurvePoint& existing : points)
        {
            if (std::fabs(existing.x - at.x) <= 1)
            {
                return true;
            }
        }
        std::vector<CurvePoint> added = points;
        added.push_back(CurvePoint{at.x, at.y});
        std::sort(added.begin(), added.end(), [](const CurvePoint& a, const CurvePoint& b) { return a.x < b.x; });
        _settings.channels[static_cast<std::size_t>(_channel)] = added;
        for (std::size_t index = 0; index < added.size(); ++index)
        {
            if (added[index].x == at.x)
            {
                _dragging = index;
            }
        }
        if (_onChange)
        {
            _onChange(added);
        }
    }
    else
    {
        return true;
    }
    _selected = _dragging;
    host.CaptureMouse(this);
    Drag(point);
    host.Invalidate();
    return true;
}

bool CurveEditor::OnMouseMove(DxUi::ControlHost& host, D2D1_POINT_2F point, UINT)
{
    if (!_dragging.has_value())
    {
        return false;
    }
    Drag(point);
    host.Invalidate();
    return true;
}

bool CurveEditor::OnMouseUp(DxUi::ControlHost& host, D2D1_POINT_2F, bool, UINT)
{
    if (!_dragging.has_value())
    {
        return false;
    }
    _dragging.reset();
    host.ReleaseMouseCapture();
    host.Invalidate();
    return true;
}

void CurveEditor::OnCaptureLost(DxUi::ControlHost&)
{
    _dragging.reset();
}

void CurveEditor::Drag(D2D1_POINT_2F point)
{
    if (!_dragging.has_value())
    {
        return;
    }
    std::vector<CurvePoint> points = Points();
    const std::size_t i = _dragging.value();
    if (i >= points.size())
    {
        _dragging.reset();
        return;
    }
    const CurvePoint at = Unposition(point);
    points[i].y = at.y;
    if (i > 0 && i + 1 < points.size())
    {
        points[i].x = std::min(points[i + 1].x - 1, std::max(points[i - 1].x + 1, at.x));
    }
    _settings.channels[static_cast<std::size_t>(_channel)] = points;
    if (_onChange)
    {
        _onChange(points);
    }
}

// ── LevelsBar ─────────────────────────────────────────────────────────────

void LevelsBar::SetHistogram(const std::array<double, 256>& bins, int channel) noexcept
{
    _bins = bins;
    _channel = std::clamp(channel, 0, 3);
}

void LevelsBar::SetRange(const LevelRange& range) noexcept
{
    _range = range.Normalized();
}

std::vector<double> LevelsBar::HandlePositions() const noexcept
{
    if (_kind == Kind::Output)
    {
        return {_range.outputBlack, _range.outputWhite};
    }
    const double gammaPosition = _range.black + (_range.white - _range.black) * std::pow(0.5, _range.gamma);
    return {_range.black, gammaPosition, _range.white};
}

double LevelsBar::ValueAt(float x) const noexcept
{
    const D2D1_RECT_F bounds = GetBounds();
    const float width = std::max(1.0f, bounds.right - bounds.left);
    return std::min(255.0, std::max(0.0, static_cast<double>((x - bounds.left) / width) * 255.0));
}

void LevelsBar::Paint(DxUi::ControlHost& host) const
{
    ID2D1DeviceContext* dc = host.GetDeviceContext();
    if (dc == nullptr)
    {
        return;
    }
    const D2D1_RECT_F bounds = GetBounds();
    const DxUi::ThemePalette& theme = host.GetTheme();
    const float width = bounds.right - bounds.left;
    const float graphBottom = bounds.bottom - kHandleRowDip;
    const D2D1_RECT_F graph = D2D1::RectF(bounds.left, bounds.top, bounds.right, graphBottom);
    if (_kind == Kind::Input)
    {
        dc->FillRectangle(graph, host.GetSolidBrush(theme.inputFill));
        const double peak = Adjust::HistogramDisplayScale(_bins);
        if (peak > 0)
        {
            ID2D1SolidColorBrush* bar = host.GetSolidBrush(ChannelColor(_channel));
            const float graphHeight = graphBottom - bounds.top;
            for (int index = 0; index < 256; ++index)
            {
                const float height =
                    graphHeight *
                    static_cast<float>(std::min(1.0, std::max(0.0, _bins[static_cast<std::size_t>(index)] / peak)));
                if (height <= 0)
                {
                    continue;
                }
                const float x = bounds.left + static_cast<float>(index) * width / 256.0f;
                dc->FillRectangle(D2D1::RectF(x, graphBottom - height, x + width / 256.0f + 0.1f, graphBottom), bar);
            }
        }
        dc->DrawRectangle(graph, host.GetSolidBrush(theme.inputBorder), 1.0f);
    }
    else
    {
        // The output ramp: black to white in 32 steps (a gradient brush would need per-paint resources).
        for (int step = 0; step < 32; ++step)
        {
            const float value = static_cast<float>(step) / 31.0f;
            const float x0 = bounds.left + width * static_cast<float>(step) / 32.0f;
            const float x1 = bounds.left + width * static_cast<float>(step + 1) / 32.0f;
            dc->FillRectangle(D2D1::RectF(x0, bounds.top, x1 + 0.5f, graphBottom),
                              host.GetSolidBrush(D2D1::ColorF(value, value, value, 1.0f)));
        }
        dc->DrawRectangle(graph, host.GetSolidBrush(theme.inputBorder), 1.0f);
    }
    // The handles under the graph: black, gray (gamma) and white triangles.
    const std::vector<double> positions = HandlePositions();
    ID2D1SolidColorBrush* stroke = host.GetSolidBrush(theme.borderStrong);
    for (std::size_t index = 0; index < positions.size(); ++index)
    {
        const float x = bounds.left + static_cast<float>(positions[index] / 255.0) * width;
        const bool last = index + 1 == positions.size();
        const D2D1_COLOR_F color = index == 0 ? D2D1::ColorF(0.0f, 0.0f, 0.0f, 1.0f)
                                   : last     ? D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f)
                                              : D2D1::ColorF(0.5f, 0.5f, 0.5f, 1.0f);
        FillTriangle(dc, x, graphBottom + 3.0f, kHandleHalfDip - 1.0f, kHandleRowDip - 6.0f, host.GetSolidBrush(color),
                     stroke);
    }
    if (!IsEnabled())
    {
        D2D1_COLOR_F veil = theme.cardBackground;
        veil.a = 0.55f;
        dc->FillRectangle(bounds, host.GetSolidBrush(veil));
        dc->DrawRectangle(bounds, host.GetSolidBrush(theme.disabledText), 1.0f);
    }
}

bool LevelsBar::OnMouseDown(DxUi::ControlHost& host, D2D1_POINT_2F point, bool rightButton, UINT)
{
    if (rightButton || !IsEnabled())
    {
        return false;
    }
    const D2D1_RECT_F bounds = GetBounds();
    const float width = std::max(1.0f, bounds.right - bounds.left);
    const std::vector<double> positions = HandlePositions();
    std::optional<int> nearest;
    float best = kHandleHalfDip + 4.0f;
    for (std::size_t index = 0; index < positions.size(); ++index)
    {
        const float x = bounds.left + static_cast<float>(positions[index] / 255.0) * width;
        const float distance = std::fabs(point.x - x);
        if (distance < best)
        {
            best = distance;
            nearest = static_cast<int>(index);
        }
    }
    if (!nearest.has_value())
    {
        return true;
    }
    _dragging = nearest;
    host.CaptureMouse(this);
    Drag(point);
    host.Invalidate();
    return true;
}

bool LevelsBar::OnMouseMove(DxUi::ControlHost& host, D2D1_POINT_2F point, UINT)
{
    if (!_dragging.has_value())
    {
        return false;
    }
    Drag(point);
    host.Invalidate();
    return true;
}

bool LevelsBar::OnMouseUp(DxUi::ControlHost& host, D2D1_POINT_2F, bool, UINT)
{
    if (!_dragging.has_value())
    {
        return false;
    }
    _dragging.reset();
    host.ReleaseMouseCapture();
    return true;
}

void LevelsBar::OnCaptureLost(DxUi::ControlHost&)
{
    _dragging.reset();
}

void LevelsBar::Drag(D2D1_POINT_2F point)
{
    if (!_dragging.has_value() || !_onHandle)
    {
        return;
    }
    // Input handles are 0 black, 1 gamma, 2 white; output handles 0 black, 1 white.
    _onHandle(_dragging.value(), ValueAt(point.x));
}

// ── SpectrumEditor ────────────────────────────────────────────────────────

void SpectrumEditor::SetSettings(const HueSaturationSettings& settings) noexcept
{
    _settings = settings;
}

void SpectrumEditor::Paint(DxUi::ControlHost& host) const
{
    ID2D1DeviceContext* dc = host.GetDeviceContext();
    if (dc == nullptr)
    {
        return;
    }
    const D2D1_RECT_F bounds = GetBounds();
    const float width = bounds.right - bounds.left;
    constexpr int kSlices = 72;
    const float handleTop = SpectrumHandleTopDip(bounds);
    const float handleBottom = handleTop + kSpectrumHandleStripDip;
    const float afterTop = handleBottom + kSpectrumGapDip;
    for (int slice = 0; slice < kSlices; ++slice)
    {
        const double hue = static_cast<double>(slice) / kSlices * 360.0;
        const float x0 = bounds.left + width * static_cast<float>(slice) / kSlices;
        const float x1 = x0 + width / kSlices + 0.5f;
        dc->FillRectangle(D2D1::RectF(x0, bounds.top, x1, bounds.top + kSpectrumBarDip),
                          host.GetSolidBrush(HueColor(hue)));
        dc->FillRectangle(D2D1::RectF(x0, afterTop, x1, afterTop + kSpectrumBarDip),
                          host.GetSolidBrush(HueColor(Adjust::ShiftedHue(hue, _settings))));
    }
    // Outer marks are the falloff shoulders; inner bars the full-strength range.
    ID2D1SolidColorBrush* mark = host.GetSolidBrush(host.GetTheme().text);
    const std::array<double, 4> handles = Adjust::BandOf(_settings).Handles();
    for (std::size_t index = 0; index < handles.size(); ++index)
    {
        const float x = bounds.left + static_cast<float>(handles[index] / 360.0) * width;
        const bool inner = index == 1 || index == 2;
        const D2D1_RECT_F rect = inner ? D2D1::RectF(x - 1.0f, handleTop, x + 1.0f, handleBottom)
                                       : D2D1::RectF(x - 3.5f, (handleTop + handleBottom) / 2 - 2.5f, x + 3.5f,
                                                     (handleTop + handleBottom) / 2 + 2.5f);
        dc->FillRectangle(rect, mark);
    }
    if (!IsEnabled())
    {
        D2D1_COLOR_F veil = host.GetTheme().cardBackground;
        veil.a = 0.55f;
        dc->FillRectangle(bounds, host.GetSolidBrush(veil));
        dc->DrawRectangle(bounds, host.GetSolidBrush(host.GetTheme().disabledText), 1.0f);
    }
}

int SpectrumEditor::NearestHandle(double degrees) const noexcept
{
    const std::array<double, 4> handles = Adjust::BandOf(_settings).Handles();
    int nearest = 0;
    double best = 361;
    for (std::size_t index = 0; index < handles.size(); ++index)
    {
        const double gap = std::fmod(std::fabs(handles[index] - degrees), 360.0);
        const double distance = std::min(gap, 360.0 - gap);
        if (distance < best)
        {
            best = distance;
            nearest = static_cast<int>(index);
        }
    }
    return nearest;
}

bool SpectrumEditor::HandleStripContainsDip(const D2D1_RECT_F& bounds, D2D1_POINT_2F point) noexcept
{
    const float top = SpectrumHandleTopDip(bounds);
    return point.y >= top && point.y <= top + kSpectrumHandleStripDip;
}

bool SpectrumEditor::OnMouseDown(DxUi::ControlHost& host, D2D1_POINT_2F point, bool rightButton, UINT)
{
    if (rightButton || !IsEnabled())
    {
        return false;
    }
    const D2D1_RECT_F bounds = GetBounds();
    if (!HandleStripContainsDip(bounds, point))
    {
        // Legacy scopes the drag gesture to the handle row between the two hue bars (HueSaturationSheet.swift's
        // `handles` view carries the only DragGesture); a press on either bar is consumed but changes nothing.
        return true;
    }
    const float width = std::max(1.0f, bounds.right - bounds.left);
    const double degrees = std::min(360.0, std::max(0.0, static_cast<double>((point.x - bounds.left) / width) * 360.0));
    _dragging = NearestHandle(degrees);
    host.CaptureMouse(this);
    Drag(point);
    host.Invalidate();
    return true;
}

bool SpectrumEditor::OnMouseMove(DxUi::ControlHost& host, D2D1_POINT_2F point, UINT)
{
    if (!_dragging.has_value())
    {
        return false;
    }
    Drag(point);
    host.Invalidate();
    return true;
}

bool SpectrumEditor::OnMouseUp(DxUi::ControlHost& host, D2D1_POINT_2F, bool, UINT)
{
    if (!_dragging.has_value())
    {
        return false;
    }
    _dragging.reset();
    host.ReleaseMouseCapture();
    return true;
}

void SpectrumEditor::OnCaptureLost(DxUi::ControlHost&)
{
    _dragging.reset();
}

void SpectrumEditor::Drag(D2D1_POINT_2F point)
{
    if (!_dragging.has_value() || !_onHandle)
    {
        return;
    }
    const D2D1_RECT_F bounds = GetBounds();
    const float width = std::max(1.0f, bounds.right - bounds.left);
    const double degrees = std::min(360.0, std::max(0.0, static_cast<double>((point.x - bounds.left) / width) * 360.0));
    _onHandle(_dragging.value(), degrees);
}

// ── GradientRamp ──────────────────────────────────────────────────────────

void GradientRamp::SetEnds(const AdjustmentColor& dark, const AdjustmentColor& light) noexcept
{
    _dark = dark;
    _light = light;
}

void GradientRamp::Paint(DxUi::ControlHost& host) const
{
    ID2D1DeviceContext* dc = host.GetDeviceContext();
    if (dc == nullptr)
    {
        return;
    }
    const D2D1_RECT_F bounds = GetBounds();
    const float width = bounds.right - bounds.left;
    constexpr int kSteps = 48;
    for (int step = 0; step < kSteps; ++step)
    {
        const double t = static_cast<double>(step) / (kSteps - 1);
        const D2D1_COLOR_F color = D2D1::ColorF(static_cast<float>(_dark.red + (_light.red - _dark.red) * t),
                                                static_cast<float>(_dark.green + (_light.green - _dark.green) * t),
                                                static_cast<float>(_dark.blue + (_light.blue - _dark.blue) * t), 1.0f);
        const float x0 = bounds.left + width * static_cast<float>(step) / kSteps;
        const float x1 = bounds.left + width * static_cast<float>(step + 1) / kSteps + 0.5f;
        dc->FillRectangle(D2D1::RectF(x0, bounds.top, std::min(x1, bounds.right), bounds.bottom),
                          host.GetSolidBrush(color));
    }
    dc->DrawRectangle(bounds, host.GetSolidBrush(host.GetTheme().inputBorder), 1.0f);
}
