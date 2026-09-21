#include "TransformEdit.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace
{
constexpr double kPi = 3.14159265358979323846;
constexpr double kMaxWarpSide = 30000.0;
constexpr double kMaxWarpArea = 100'000'000.0;
constexpr std::array<Vec2, 4> kSubSamples{Vec2{-0.25, -0.25}, Vec2{0.25, -0.25}, Vec2{-0.25, 0.25}, Vec2{0.25, 0.25}};

[[nodiscard]] bool Finite(Vec2 point) noexcept
{
    return std::isfinite(point.x) && std::isfinite(point.y);
}

// Pixel `(x, y)` of a raster clamped to its edges.
[[nodiscard]] const std::uint8_t* PixelAt(const Raster& image, int x, int y) noexcept
{
    x = std::clamp(x, 0, image.Width() - 1);
    y = std::clamp(y, 0, image.Height() - 1);
    const int channels = image.Format() == RasterFormat::Gray8 ? 1 : 4;
    return image.Data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(image.Stride()) +
           static_cast<std::size_t>(x) * static_cast<std::size_t>(channels);
}

// The source sampled at pixel coordinates (`fx`, `fy`; pixel i spans [i, i + 1)), bilinear unless `nearest`.
void Sample(const Raster& image, double fx, double fy, bool nearest, std::uint8_t* out, int channels) noexcept
{
    if (nearest)
    {
        const std::uint8_t* pixel = PixelAt(image, static_cast<int>(std::floor(fx)), static_cast<int>(std::floor(fy)));
        std::memcpy(out, pixel, static_cast<std::size_t>(channels));
        return;
    }
    const double sx = fx - 0.5;
    const double sy = fy - 0.5;
    const int x0 = static_cast<int>(std::floor(sx));
    const int y0 = static_cast<int>(std::floor(sy));
    const double tx = sx - static_cast<double>(x0);
    const double ty = sy - static_cast<double>(y0);
    const std::uint8_t* p00 = PixelAt(image, x0, y0);
    const std::uint8_t* p10 = PixelAt(image, x0 + 1, y0);
    const std::uint8_t* p01 = PixelAt(image, x0, y0 + 1);
    const std::uint8_t* p11 = PixelAt(image, x0 + 1, y0 + 1);
    for (int channel = 0; channel < channels; ++channel)
    {
        const double top = p00[channel] * (1.0 - tx) + p10[channel] * tx;
        const double bottom = p01[channel] * (1.0 - tx) + p11[channel] * tx;
        const double value = top * (1.0 - ty) + bottom * ty;
        out[channel] = static_cast<std::uint8_t>(std::clamp(std::lround(value), 0L, 255L));
    }
}

struct WarpBounds
{
    double minX = 0;
    double minY = 0;
    double width = 0;
    double height = 0;
};

[[nodiscard]] std::optional<WarpBounds> BoundsOf(const std::array<Vec2, 4>& corners) noexcept
{
    double minX = corners[0].x;
    double maxX = corners[0].x;
    double minY = corners[0].y;
    double maxY = corners[0].y;
    for (const Vec2& corner : corners)
    {
        minX = std::min(minX, corner.x);
        maxX = std::max(maxX, corner.x);
        minY = std::min(minY, corner.y);
        maxY = std::max(maxY, corner.y);
    }
    WarpBounds bounds;
    bounds.minX = std::floor(minX);
    bounds.minY = std::floor(minY);
    bounds.width = std::ceil(maxX) - bounds.minX;
    bounds.height = std::ceil(maxY) - bounds.minY;
    if (bounds.width < 1 || bounds.height < 1 || bounds.width > kMaxWarpSide || bounds.height > kMaxWarpSide ||
        bounds.width * bounds.height > kMaxWarpArea)
    {
        return std::nullopt;
    }
    return bounds;
}

// The shared warp: `outside` is the byte written where the shape does not reach (0 for pixels and hide-all masks).
[[nodiscard]] std::optional<DistortWarp::Warped> WarpImpl(const Raster& image, const LayerTransform& transform,
                                                          const std::array<Vec2, 4>& corners, bool isMask, double limit,
                                                          std::uint8_t outside)
{
    if (!image.HasStorage() || !DistortWarp::IsUsable(corners))
    {
        return std::nullopt;
    }
    const std::optional<WarpBounds> bounds = BoundsOf(corners);
    if (!bounds.has_value())
    {
        return std::nullopt;
    }
    LayerTransform placed;
    placed.origin = Vec2{bounds->minX, bounds->minY};
    placed.size = Vec2{bounds->width, bounds->height};
    placed.sampling = transform.sampling;
    // A uniform 1 x 1 mask already covers any shape.
    if (isMask && image.Width() == 1 && image.Height() == 1)
    {
        return DistortWarp::Warped{image, placed};
    }
    const double factor = limit > 0 ? std::min(1.0, limit / std::max(bounds->width, bounds->height)) : 1.0;
    const int width = std::max(1, static_cast<int>(std::ceil(bounds->width * factor)));
    const int height = std::max(1, static_cast<int>(std::ceil(bounds->height * factor)));
    const std::optional<DistortWarp::Homography> inverse = DistortWarp::MakeHomography(corners).Inverted();
    if (!inverse.has_value())
    {
        return std::nullopt;
    }
    const RasterFormat format = isMask ? RasterFormat::Gray8 : RasterFormat::Bgra8888;
    const int channels = format == RasterFormat::Gray8 ? 1 : 4;
    if ((image.Format() == RasterFormat::Gray8) != isMask)
    {
        return std::nullopt;
    }
    const int stride = width * channels;
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(stride) * static_cast<std::size_t>(height),
                                    isMask ? outside : std::uint8_t{0});
    const bool nearest = transform.sampling == LayerSampling::Nearest;
    const double sourceWidth = static_cast<double>(image.Width());
    const double sourceHeight = static_cast<double>(image.Height());
    for (int y = 0; y < height; ++y)
    {
        std::uint8_t* row = bytes.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(stride);
        const double documentY = bounds->minY + (static_cast<double>(y) + 0.5) / factor;
        for (int x = 0; x < width; ++x)
        {
            const double documentX = bounds->minX + (static_cast<double>(x) + 0.5) / factor;
            // Four sub-samples decide how much of the pixel the shape covers, so its edges come out antialiased.
            int inside = 0;
            for (const Vec2& offset : kSubSamples)
            {
                const Vec2 unit = inverse->Map(Vec2{documentX + offset.x / factor, documentY + offset.y / factor});
                if (unit.x >= 0.0 && unit.x <= 1.0 && unit.y >= 0.0 && unit.y <= 1.0)
                {
                    ++inside;
                }
            }
            if (inside == 0)
            {
                continue;
            }
            const Vec2 center = inverse->Map(Vec2{documentX, documentY});
            const Vec2 unit{std::clamp(center.x, 0.0, 1.0), std::clamp(center.y, 0.0, 1.0)};
            // A flipped layer shows its pixels mirrored, so the unit square reads them from the far side.
            const double u = transform.flipX ? 1.0 - unit.x : unit.x;
            const double v = transform.flipY ? 1.0 - unit.y : unit.y;
            std::uint8_t* out = row + static_cast<std::size_t>(x) * channels;
            Sample(image, u * sourceWidth, v * sourceHeight, nearest, out, channels);
            if (inside < 4)
            {
                const double coverage = static_cast<double>(inside) / 4.0;
                for (int channel = 0; channel < channels; ++channel)
                {
                    // Premultiplied pixels scale whole; a mask blends toward its background.
                    const double rest = isMask ? static_cast<double>(outside) * (1.0 - coverage) : 0.0;
                    out[channel] =
                        static_cast<std::uint8_t>(std::clamp(std::lround(out[channel] * coverage + rest), 0L, 255L));
                }
            }
        }
    }
    Raster raster = Raster::Make(width, height, format, bytes.data(), bytes.size());
    if (!raster.HasStorage())
    {
        return std::nullopt;
    }
    return DistortWarp::Warped{std::move(raster), placed};
}
} // namespace

// ── TransformDrag ────────────────────────────────────────────────────────

std::optional<std::array<Vec2, 4>> TransformDrag::Corners(Vec2 point, bool shift) const noexcept
{
    if (!originalCorners.has_value())
    {
        return std::nullopt;
    }
    std::array<Vec2, 4> result = originalCorners.value();
    double dx = point.x - start.x;
    double dy = point.y - start.y;
    if (shift)
    {
        // Shift keeps what is being dragged on one axis.
        if (std::abs(dx) >= std::abs(dy))
        {
            dy = 0;
        }
        else
        {
            dx = 0;
        }
    }
    std::array<int, 4> moved{};
    int count = 0;
    switch (mode)
    {
    case Mode::Distort:
        if (handle < 0 || handle > 7)
        {
            return std::nullopt;
        }
        if (handle % 2 == 0)
        {
            moved[count++] = handle / 2;
        }
        else
        {
            moved[count++] = handle / 2;
            moved[count++] = (handle / 2 + 1) % 4;
        }
        break;
    case Mode::Move:
        moved = {0, 1, 2, 3};
        count = 4;
        break;
    default:
        return std::nullopt;
    }
    for (int index = 0; index < count; ++index)
    {
        result[static_cast<std::size_t>(moved[static_cast<std::size_t>(index)])].x += dx;
        result[static_cast<std::size_t>(moved[static_cast<std::size_t>(index)])].y += dy;
    }
    return result;
}

LayerTransform TransformDrag::Updated(Vec2 point, bool lockRatio, bool shift, bool option) const noexcept
{
    LayerTransform result = original;
    switch (mode)
    {
    case Mode::Distort:
        break;
    case Mode::Move:
    {
        double dx = point.x - start.x;
        double dy = point.y - start.y;
        if (shift)
        {
            if (std::abs(dx) >= std::abs(dy))
            {
                dy = 0;
            }
            else
            {
                dx = 0;
            }
        }
        result.origin.x += dx;
        result.origin.y += dy;
        break;
    }
    case Mode::Rotate:
    {
        const Vec2 center = original.Center();
        const double delta =
            std::atan2(point.y - center.y, point.x - center.x) - std::atan2(start.y - center.y, start.x - center.x);
        result.rotation += delta * 180.0 / kPi;
        if (shift)
        {
            result.rotation = std::round(result.rotation / 15.0) * 15.0;
        }
        break;
    }
    case Mode::Resize:
    {
        if (handle < 0 || handle > 7)
        {
            return original;
        }
        const Vec2 unit = LayerTransform::kHandles[static_cast<std::size_t>(handle)];
        const Vec2 anchorUnit = option ? Vec2{0.5, 0.5} : Vec2{1.0 - unit.x, 1.0 - unit.y};
        const Vec2 anchor = original.Point(anchorUnit);
        // Use the initial handle plus the pointer delta to avoid a jump on grab.
        const Vec2 initialHandle = original.Point(unit);
        const double dx = initialHandle.x + point.x - start.x - anchor.x;
        const double dy = initialHandle.y + point.y - start.y - anchor.y;
        // Center-to-handle distances cover half the size on each axis.
        const double span = option ? 2.0 : 1.0;
        const double radians = original.Radians();
        const double localX = (dx * std::cos(radians) + dy * std::sin(radians)) * span;
        const double localY = (-dx * std::sin(radians) + dy * std::cos(radians)) * span;
        const double sx = unit.x * 2.0 - 1.0;
        const double sy = unit.y * 2.0 - 1.0;
        double width = sx == 0.0 ? original.size.x : std::max(1.0, localX * sx);
        double height = sy == 0.0 ? original.size.y : std::max(1.0, localY * sy);
        if (lockRatio != shift)
        {
            double factor = 1.0;
            if (sx == 0.0)
            {
                factor = height / original.size.y;
            }
            else if (sy == 0.0)
            {
                factor = width / original.size.x;
            }
            else
            {
                // Project onto the original diagonal for proportional scaling.
                factor = std::max(1.0 / std::min(original.size.x, original.size.y),
                                  (localX * sx * original.size.x + localY * sy * original.size.y) /
                                      (original.size.x * original.size.x + original.size.y * original.size.y));
            }
            width = original.size.x * factor;
            height = original.size.y * factor;
        }
        result.size = Vec2{width, height};
        const double offsetX = (0.5 - anchorUnit.x) * width;
        const double offsetY = (0.5 - anchorUnit.y) * height;
        const Vec2 center{anchor.x + offsetX * std::cos(radians) - offsetY * std::sin(radians),
                          anchor.y + offsetX * std::sin(radians) + offsetY * std::cos(radians)};
        result.origin = Vec2{center.x - width / 2.0, center.y - height / 2.0};
        break;
    }
    }
    return result.IsValid() ? result : original;
}

// ── TransformSnap ────────────────────────────────────────────────────────

namespace TransformSnap
{
namespace
{
struct Shift
{
    double move = 0;
    std::optional<double> target;
};

// The smallest move that puts one of `guides` on one of `targets`, and the target it met.
[[nodiscard]] Shift ShiftTo(std::span<const double> guides, std::span<const double> targets, double tolerance) noexcept
{
    Shift best;
    bool found = false;
    for (const double guide : guides)
    {
        for (const double target : targets)
        {
            const double move = target - guide;
            if (!(std::abs(move) <= tolerance))
            {
                continue;
            }
            if (found && std::abs(best.move) <= std::abs(move))
            {
                continue;
            }
            best.move = move;
            best.target = target;
            found = true;
        }
    }
    return best;
}
} // namespace

Result Offset(double minX, double minY, double maxX, double maxY, std::span<const double> xs,
              std::span<const double> ys, double tolerance) noexcept
{
    const std::array<double, 3> horizontalGuides{minX, (minX + maxX) / 2.0, maxX};
    const std::array<double, 3> verticalGuides{minY, (minY + maxY) / 2.0, maxY};
    const Shift horizontal = ShiftTo(horizontalGuides, xs, tolerance);
    const Shift vertical = ShiftTo(verticalGuides, ys, tolerance);
    Result result;
    result.offset = Vec2{horizontal.move, vertical.move};
    result.x = horizontal.target;
    result.y = vertical.target;
    return result;
}
} // namespace TransformSnap

// ── DistortWarp ──────────────────────────────────────────────────────────

namespace DistortWarp
{
Vec2 Homography::Map(Vec2 point) const noexcept
{
    const double w = m[6] * point.x + m[7] * point.y + m[8];
    if (std::abs(w) < 1e-12)
    {
        return Vec2{std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity()};
    }
    return Vec2{(m[0] * point.x + m[1] * point.y + m[2]) / w, (m[3] * point.x + m[4] * point.y + m[5]) / w};
}

std::optional<Homography> Homography::Inverted() const noexcept
{
    const double a = m[0], b = m[1], c = m[2], d = m[3], e = m[4], f = m[5], g = m[6], h = m[7], i = m[8];
    const double determinant = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
    if (!std::isfinite(determinant) || std::abs(determinant) < 1e-18)
    {
        return std::nullopt;
    }
    Homography result;
    result.m = {(e * i - f * h) / determinant, (c * h - b * i) / determinant, (b * f - c * e) / determinant,
                (f * g - d * i) / determinant, (a * i - c * g) / determinant, (c * d - a * f) / determinant,
                (d * h - e * g) / determinant, (b * g - a * h) / determinant, (a * e - b * d) / determinant};
    return result;
}

bool IsUsable(const std::array<Vec2, 4>& corners) noexcept
{
    for (const Vec2& corner : corners)
    {
        if (!Finite(corner) || std::abs(corner.x) > 1'000'000.0 || std::abs(corner.y) > 1'000'000.0)
        {
            return false;
        }
    }
    double sign = 0;
    for (std::size_t index = 0; index < 4; ++index)
    {
        const Vec2 a = corners[index];
        const Vec2 b = corners[(index + 1) % 4];
        const Vec2 c = corners[(index + 2) % 4];
        const double cross = (b.x - a.x) * (c.y - b.y) - (b.y - a.y) * (c.x - b.x);
        if (!(std::abs(cross) > 0.01))
        {
            return false;
        }
        if (sign == 0)
        {
            sign = cross < 0 ? -1 : 1;
        }
        else if ((cross < 0) != (sign < 0))
        {
            return false;
        }
    }
    return true;
}

Homography MakeHomography(const std::array<Vec2, 4>& c) noexcept
{
    const double sx = c[0].x - c[1].x + c[2].x - c[3].x;
    const double sy = c[0].y - c[1].y + c[2].y - c[3].y;
    double g = 0;
    double h = 0;
    if (std::abs(sx) > 1e-9 || std::abs(sy) > 1e-9)
    {
        const double dx1 = c[1].x - c[2].x;
        const double dx2 = c[3].x - c[2].x;
        const double dy1 = c[1].y - c[2].y;
        const double dy2 = c[3].y - c[2].y;
        const double den = dx1 * dy2 - dx2 * dy1;
        if (std::abs(den) > 1e-12)
        {
            g = (sx * dy2 - dx2 * sy) / den;
            h = (dx1 * sy - sx * dy1) / den;
        }
    }
    Homography result;
    result.m = {c[1].x - c[0].x + g * c[1].x,
                c[3].x - c[0].x + h * c[3].x,
                c[0].x,
                c[1].y - c[0].y + g * c[1].y,
                c[3].y - c[0].y + h * c[3].y,
                c[0].y,
                g,
                h,
                1.0};
    return result;
}

std::optional<Warped> Warp(const Raster& image, const LayerTransform& transform, const std::array<Vec2, 4>& corners,
                           bool isMask, double limit)
{
    return WarpImpl(image, transform, corners, isMask, limit, 0);
}

std::optional<WarpedTrimmed> WarpTrimmed(const Raster& image, const LayerTransform& transform,
                                         const std::array<Vec2, 4>& corners)
{
    std::optional<Warped> warped = WarpImpl(image, transform, corners, false, 0.0, 0);
    if (!warped.has_value())
    {
        return std::nullopt;
    }
    const Raster& full = warped->image;
    WarpedTrimmed result;
    result.cropWidth = full.Width();
    result.cropHeight = full.Height();
    int left = full.Width();
    int top = full.Height();
    int right = -1;
    int bottom = -1;
    for (int y = 0; y < full.Height(); ++y)
    {
        for (int x = 0; x < full.Width(); ++x)
        {
            if (PixelAt(full, x, y)[3] != 0)
            {
                left = std::min(left, x);
                right = std::max(right, x);
                top = std::min(top, y);
                bottom = std::max(bottom, y);
            }
        }
    }
    // Nothing visible, or nothing to trim: keep the warp as it is.
    if (right < left || bottom < top ||
        (left == 0 && top == 0 && right == full.Width() - 1 && bottom == full.Height() - 1))
    {
        result.image = full;
        result.transform = warped->transform;
        return result;
    }
    const int width = right - left + 1;
    const int height = bottom - top + 1;
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(width) * 4u * static_cast<std::size_t>(height));
    for (int y = 0; y < height; ++y)
    {
        std::memcpy(bytes.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(width) * 4u,
                    PixelAt(full, left, top + y), static_cast<std::size_t>(width) * 4u);
    }
    result.image = Raster::Make(width, height, RasterFormat::Bgra8888, bytes.data(), bytes.size());
    if (!result.image.HasStorage())
    {
        return std::nullopt;
    }
    result.transform = warped->transform;
    result.transform.origin = Vec2{warped->transform.origin.x + left, warped->transform.origin.y + top};
    result.transform.size = Vec2{static_cast<double>(width), static_cast<double>(height)};
    result.cropX = left;
    result.cropY = top;
    result.cropWidth = width;
    result.cropHeight = height;
    return result;
}

std::optional<Warped> WarpMask(const Raster& mask, const LayerTransform& transform, const std::array<Vec2, 4>& corners,
                               double background)
{
    return WarpImpl(mask, transform, corners, true, 0.0, background >= 0.5 ? std::uint8_t{255} : std::uint8_t{0});
}

std::array<Vec2, 4> Carried(const LayerTransform& placement, const LayerTransform& transform,
                            const std::array<Vec2, 4>& corners) noexcept
{
    // The transform's unit square without its flips, as the legacy geometry did.
    const Affine2 toUnit = Affine2::Translation(-0.5, -0.5)
                               .Concat(Affine2::Scale(transform.size.x, transform.size.y))
                               .Concat(Affine2::Rotation(transform.Radians()))
                               .Concat(Affine2::Translation(transform.Center().x, transform.Center().y))
                               .Inverted();
    const Homography map = MakeHomography(corners);
    std::array<Vec2, 4> result{};
    const std::array<Vec2, 4> placementCorners = placement.Corners();
    for (std::size_t index = 0; index < 4; ++index)
    {
        result[index] = map.Map(toUnit.Apply(placementCorners[index]));
    }
    return result;
}
} // namespace DistortWarp
