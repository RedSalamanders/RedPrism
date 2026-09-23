#include "Crop.h"

#include <algorithm>
#include <cmath>

namespace
{
[[nodiscard]] double Sign(double value) noexcept
{
    return value < 0 ? -1.0 : 1.0;
}

[[nodiscard]] std::optional<double> Nearest(double value, const std::vector<double>& targets, double tolerance) noexcept
{
    std::optional<double> best;
    for (const double target : targets)
    {
        if (std::abs(target - value) > tolerance)
        {
            continue;
        }
        if (best.has_value() && std::abs(best.value() - value) <= std::abs(target - value))
        {
            continue;
        }
        best = target;
    }
    return best;
}
} // namespace

namespace CropGeometry
{
CropRect Snapped(const CropRect& rect) noexcept
{
    const double minX = std::min(rect.x, rect.x + rect.width);
    const double maxX = std::max(rect.x, rect.x + rect.width);
    const double minY = std::min(rect.y, rect.y + rect.height);
    const double maxY = std::max(rect.y, rect.y + rect.height);
    const double x = std::round(minX);
    const double y = std::round(minY);
    return CropRect{x, y, std::max(1.0, std::round(maxX) - x), std::max(1.0, std::round(maxY) - y)};
}

bool Valid(const CropRect& rect) noexcept
{
    return std::isfinite(rect.x) && std::isfinite(rect.y) && std::isfinite(rect.width) && std::isfinite(rect.height) &&
           rect.width >= 1.0 && rect.width <= kMaxCanvasSide && rect.height >= 1.0 && rect.height <= kMaxCanvasSide &&
           std::abs(rect.x) <= kMaxTransformOrigin && std::abs(rect.y) <= kMaxTransformOrigin;
}

CropRect Create(Vec2 start, Vec2 end, std::optional<double> ratio, bool symmetric) noexcept
{
    double dx = end.x - start.x;
    double dy = end.y - start.y;
    if (ratio.has_value() && ratio.value() > 0.0)
    {
        if (std::abs(dx) > std::abs(dy) * ratio.value())
        {
            dy = Sign(dy) * std::abs(dx) / ratio.value();
        }
        else
        {
            dx = Sign(dx) * std::abs(dy) * ratio.value();
        }
    }
    if (symmetric)
    {
        return Snapped(
            CropRect{start.x - std::abs(dx), start.y - std::abs(dy), std::abs(dx) * 2.0, std::abs(dy) * 2.0});
    }
    return Snapped(
        CropRect{std::min(start.x, start.x + dx), std::min(start.y, start.y + dy), std::abs(dx), std::abs(dy)});
}
} // namespace CropGeometry

CropRect CropDrag::Updated(Vec2 point, std::optional<double> ratio, bool symmetric) const noexcept
{
    switch (mode)
    {
    case Mode::Create:
        return CropGeometry::Create(start, point, ratio, symmetric);
    case Mode::Move:
        return CropGeometry::Snapped(original.OffsetBy(point.x - start.x, point.y - start.y));
    case Mode::Resize:
    default:
    {
        LayerTransform transform;
        transform.origin = Vec2{original.x, original.y};
        transform.size = Vec2{original.width, original.height};
        TransformDrag drag;
        drag.mode = TransformDrag::Mode::Resize;
        drag.handle = handle;
        drag.original = transform;
        drag.start = start;
        const LayerTransform next = drag.Updated(point, ratio.has_value(), false, symmetric);
        return CropGeometry::Snapped(CropRect{next.origin.x, next.origin.y, next.size.x, next.size.y});
    }
    }
}

CropRect CropSnap::Apply(const CropRect& rect, const CropDrag& drag, Vec2 point, std::optional<double> ratio,
                         bool symmetric) const noexcept
{
    if (!(tolerance > 0.0))
    {
        return rect;
    }
    bool horizontal = false;
    bool vertical = false;
    switch (drag.mode)
    {
    case CropDrag::Mode::Move:
    {
        // The nearest edge on each axis lands on a target; the size is kept.
        const auto shift = [&](std::initializer_list<double> edges, const std::vector<double>& targets) noexcept
        {
            std::optional<double> best;
            for (const double edge : edges)
            {
                const std::optional<double> target = Nearest(edge, targets, tolerance);
                if (target.has_value() &&
                    (!best.has_value() || std::abs(target.value() - edge) < std::abs(best.value())))
                {
                    best = target.value() - edge;
                }
            }
            return best.value_or(0.0);
        };
        return rect.OffsetBy(shift({rect.MinX(), rect.MaxX()}, xs), shift({rect.MinY(), rect.MaxY()}, ys));
    }
    case CropDrag::Mode::Create:
        if (ratio.has_value())
        {
            return rect;
        }
        horizontal = true;
        vertical = true;
        break;
    case CropDrag::Mode::Resize:
    default:
    {
        if (ratio.has_value() || drag.handle < 0 || drag.handle >= 8)
        {
            return rect;
        }
        const Vec2 unit = LayerTransform::kHandles[static_cast<std::size_t>(drag.handle)];
        horizontal = unit.x != 0.5;
        vertical = unit.y != 0.5;
        break;
    }
    }
    CropRect result = rect;
    // The dragged edge is the one on the pointer's side.
    if (horizontal)
    {
        if (std::abs(point.x - result.MinX()) <= std::abs(point.x - result.MaxX()))
        {
            const std::optional<double> x = Nearest(result.MinX(), xs, tolerance);
            if (x.has_value() && x.value() < result.MaxX())
            {
                result = CropRect{x.value(), result.y, result.MaxX() - x.value(), result.height};
            }
        }
        else
        {
            const std::optional<double> x = Nearest(result.MaxX(), xs, tolerance);
            if (x.has_value() && x.value() > result.MinX())
            {
                result.width = x.value() - result.MinX();
            }
        }
    }
    if (vertical)
    {
        if (std::abs(point.y - result.MinY()) <= std::abs(point.y - result.MaxY()))
        {
            const std::optional<double> y = Nearest(result.MinY(), ys, tolerance);
            if (y.has_value() && y.value() < result.MaxY())
            {
                result = CropRect{result.x, y.value(), result.width, result.MaxY() - y.value()};
            }
        }
        else
        {
            const std::optional<double> y = Nearest(result.MaxY(), ys, tolerance);
            if (y.has_value() && y.value() > result.MinY())
            {
                result.height = y.value() - result.MinY();
            }
        }
    }
    if (symmetric)
    {
        // The snapped (dragged) edge sets the half size; the opposite edge mirrors it about the center. The center
        // sits on the half-pixel grid, so both edges land on whole pixels (Decision D6: legacy mirrors about the
        // unrounded press point, which leaves the frame between pixels and makes Apply resample every layer).
        Vec2 center{drag.original.MidX(), drag.original.MidY()};
        if (drag.mode == CropDrag::Mode::Create)
        {
            center = drag.start;
        }
        center = Vec2{std::round(center.x * 2.0) / 2.0, std::round(center.y * 2.0) / 2.0};
        if (horizontal)
        {
            const double half = point.x >= center.x ? result.MaxX() - center.x : center.x - result.MinX();
            if (half >= 0.5)
            {
                result.x = center.x - half;
                result.width = half * 2.0;
            }
        }
        if (vertical)
        {
            const double half = point.y >= center.y ? result.MaxY() - center.y : center.y - result.MinY();
            if (half >= 0.5)
            {
                result.y = center.y - half;
                result.height = half * 2.0;
            }
        }
        return CropGeometry::Snapped(result);
    }
    return result;
}
