#include "WarpStroke.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <new>

WarpStroke::WarpStroke(const Raster& image, SmearMode mode, const BrushSettings& settings)
    : _mode(mode), _diameter(std::max(2.0, settings.diameter)), _hardness(std::clamp(settings.hardness, 0.0, 0.98)),
      _strength(std::clamp(settings.opacity, 0.01, 1.0))
{
    const std::uint8_t* data = image.HasStorage() && image.Format() == RasterFormat::Bgra8888 ? image.Data() : nullptr;
    if (data == nullptr || !std::isfinite(_diameter))
    {
        return;
    }
    _width = image.Width();
    _height = image.Height();
    _columns = (_width + kTileSize - 1) / kTileSize;
    const int rows = (_height + kTileSize - 1) / kTileSize;
    try
    {
        _pixels.resize(static_cast<std::size_t>(_width) * static_cast<std::size_t>(_height) * 4u);
        for (int y = 0; y < _height; ++y)
        {
            std::memcpy(_pixels.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(_width) * 4u,
                        data + static_cast<std::size_t>(y) * static_cast<std::size_t>(image.Stride()),
                        static_cast<std::size_t>(_width) * 4u);
        }
        _dirty.assign(static_cast<std::size_t>(_columns) * static_cast<std::size_t>(rows), 0);
    }
    catch (const std::bad_alloc&)
    {
        _pixels.clear();
        return;
    }
    _valid = true;
}

int WarpStroke::Radius() const noexcept
{
    return static_cast<int>(std::ceil(_diameter / 2.0));
}

float WarpStroke::Weight(float u) const noexcept
{
    if (!(u < 1.0f))
    {
        return 0.0f;
    }
    const float h = static_cast<float>(_hardness);
    if (!(u > h))
    {
        return 1.0f;
    }
    const float t = (1.0f - u) / (1.0f - h);
    return t * t * (3.0f - 2.0f * t);
}

void WarpStroke::MarkDirty(int x0, int y0, int x1, int y1) noexcept
{
    x0 = std::clamp(x0, 0, _width - 1);
    x1 = std::clamp(x1, 0, _width - 1);
    y0 = std::clamp(y0, 0, _height - 1);
    y1 = std::clamp(y1, 0, _height - 1);
    for (int ty = y0 / kTileSize; ty <= y1 / kTileSize; ++ty)
    {
        for (int tx = x0 / kTileSize; tx <= x1 / kTileSize; ++tx)
        {
            _dirty[static_cast<std::size_t>(ty) * static_cast<std::size_t>(_columns) + static_cast<std::size_t>(tx)] =
                1;
        }
    }
}

void WarpStroke::Append(Vec2 point)
{
    if (!_valid || !std::isfinite(point.x) || !std::isfinite(point.y))
    {
        return;
    }
    if (!_last.has_value())
    {
        _last = point;
        if (_mode == SmearMode::Smudge)
        {
            PickUp(point);
        }
        return;
    }
    const Vec2 from = _last.value();
    const double distance = std::hypot(point.x - from.x, point.y - from.y);
    const double spacing = std::max(1.0, _diameter * (_mode == SmearMode::Smudge ? 0.08 : 0.025));
    if (distance < spacing)
    {
        return;
    }
    const int steps = static_cast<int>(std::ceil(distance / spacing));
    Vec2 previous = from;
    for (int step = 1; step <= steps; ++step)
    {
        const double t = static_cast<double>(step) / steps;
        const Vec2 next{from.x + (point.x - from.x) * t, from.y + (point.y - from.y) * t};
        if (_mode == SmearMode::Smudge)
        {
            Smudge(next);
        }
        else
        {
            Push(previous, next);
        }
        _points.push_back(next);
        previous = next;
    }
    _last = point;
}

void WarpStroke::PickUp(Vec2 center)
{
    const int r = Radius();
    const int side = 2 * r + 1;
    _carried.assign(static_cast<std::size_t>(side) * static_cast<std::size_t>(side) * 4u, 0.0f);
    const int cx = static_cast<int>(std::lround(center.x));
    const int cy = static_cast<int>(std::lround(center.y));
    for (int dy = -r; dy <= r; ++dy)
    {
        const int y = cy + dy;
        if (y < 0 || y >= _height)
        {
            continue;
        }
        for (int dx = -r; dx <= r; ++dx)
        {
            const int x = cx + dx;
            if (x < 0 || x >= _width)
            {
                continue;
            }
            const std::size_t p =
                (static_cast<std::size_t>(y) * static_cast<std::size_t>(_width) + static_cast<std::size_t>(x)) * 4u;
            const std::size_t c =
                (static_cast<std::size_t>(dy + r) * static_cast<std::size_t>(side) + static_cast<std::size_t>(dx + r)) *
                4u;
            for (std::size_t k = 0; k < 4; ++k)
            {
                _carried[c + k] = static_cast<float>(_pixels[p + k]);
            }
        }
    }
}

void WarpStroke::Smudge(Vec2 center)
{
    const int r = Radius();
    const int side = 2 * r + 1;
    if (_carried.size() < static_cast<std::size_t>(side) * static_cast<std::size_t>(side) * 4u)
    {
        PickUp(center);
    }
    const int cx = static_cast<int>(std::lround(center.x));
    const int cy = static_cast<int>(std::lround(center.y));
    const float keep = static_cast<float>(_strength);
    const float invR = 1.0f / static_cast<float>(_diameter / 2.0);
    MarkDirty(cx - r, cy - r, cx + r, cy + r);
    for (int dy = -r; dy <= r; ++dy)
    {
        const int y = cy + dy;
        if (y < 0 || y >= _height)
        {
            continue;
        }
        for (int dx = -r; dx <= r; ++dx)
        {
            const int x = cx + dx;
            if (x < 0 || x >= _width)
            {
                continue;
            }
            const float w = Weight(std::sqrt(static_cast<float>(dx * dx + dy * dy)) * invR);
            if (!(w > 0.0f))
            {
                continue;
            }
            const std::size_t p =
                (static_cast<std::size_t>(y) * static_cast<std::size_t>(_width) + static_cast<std::size_t>(x)) * 4u;
            const std::size_t c =
                (static_cast<std::size_t>(dy + r) * static_cast<std::size_t>(side) + static_cast<std::size_t>(dx + r)) *
                4u;
            for (std::size_t k = 0; k < 4; ++k)
            {
                const float under = static_cast<float>(_pixels[p + k]);
                const float painted = under + (_carried[c + k] - under) * w;
                _pixels[p + k] = static_cast<std::uint8_t>(std::max(0.0f, std::min(255.0f, std::round(painted))));
                // The brush picks up some of what it just left, more the weaker the smudge.
                _carried[c + k] = painted + (_carried[c + k] - painted) * keep;
            }
        }
    }
}

// Forward warp: pixels under the brush move with it, most at its center, fading to none at its rim.
void WarpStroke::Push(Vec2 from, Vec2 to)
{
    const int r = Radius();
    const float moveX = static_cast<float>((to.x - from.x) * _strength);
    const float moveY = static_cast<float>((to.y - from.y) * _strength);
    const int margin = static_cast<int>(std::ceil(std::max(std::fabs(moveX), std::fabs(moveY)))) + 2;
    const int cx = static_cast<int>(std::lround(to.x));
    const int cy = static_cast<int>(std::lround(to.y));
    // A copy of the area as it was before this dab, which the dab samples from.
    const int x0 = std::max(0, cx - r - margin);
    const int x1 = std::min(_width - 1, cx + r + margin);
    const int y0 = std::max(0, cy - r - margin);
    const int y1 = std::min(_height - 1, cy + r + margin);
    if (x0 > x1 || y0 > y1)
    {
        return;
    }
    const int cw = x1 - x0 + 1;
    const int ch = y1 - y0 + 1;
    const std::size_t needed = static_cast<std::size_t>(cw) * static_cast<std::size_t>(ch) * 4u;
    if (_scratch.size() < needed)
    {
        _scratch.assign(needed, 0.0f);
    }
    for (int y = 0; y < ch; ++y)
    {
        for (int x = 0; x < cw; ++x)
        {
            const std::size_t p = (static_cast<std::size_t>(y + y0) * static_cast<std::size_t>(_width) +
                                   static_cast<std::size_t>(x + x0)) *
                                  4u;
            const std::size_t s =
                (static_cast<std::size_t>(y) * static_cast<std::size_t>(cw) + static_cast<std::size_t>(x)) * 4u;
            for (std::size_t k = 0; k < 4; ++k)
            {
                _scratch[s + k] = static_cast<float>(_pixels[p + k]);
            }
        }
    }
    MarkDirty(cx - r, cy - r, cx + r, cy + r);
    const float invR = 1.0f / static_cast<float>(_diameter / 2.0);
    for (int dy = -r; dy <= r; ++dy)
    {
        const int y = cy + dy;
        if (y < y0 || y > y1)
        {
            continue;
        }
        for (int dx = -r; dx <= r; ++dx)
        {
            const int x = cx + dx;
            if (x < x0 || x > x1)
            {
                continue;
            }
            const float w = Weight(std::sqrt(static_cast<float>(dx * dx + dy * dy)) * invR);
            if (!(w > 0.0f))
            {
                continue;
            }
            // Bilinear sample of the old pixels, from behind the brush's travel.
            const float sx =
                std::min(static_cast<float>(cw - 1), std::max(0.0f, static_cast<float>(x - x0) - moveX * w));
            const float sy =
                std::min(static_cast<float>(ch - 1), std::max(0.0f, static_cast<float>(y - y0) - moveY * w));
            const int ix = std::min(cw - 2, static_cast<int>(sx));
            const int iy = std::min(ch - 2, static_cast<int>(sy));
            if (ix < 0 || iy < 0)
            {
                continue;
            }
            const float fx = sx - static_cast<float>(ix);
            const float fy = sy - static_cast<float>(iy);
            const std::size_t p =
                (static_cast<std::size_t>(y) * static_cast<std::size_t>(_width) + static_cast<std::size_t>(x)) * 4u;
            const std::size_t s00 =
                (static_cast<std::size_t>(iy) * static_cast<std::size_t>(cw) + static_cast<std::size_t>(ix)) * 4u;
            const std::size_t s10 = s00 + 4u;
            const std::size_t s01 = s00 + static_cast<std::size_t>(cw) * 4u;
            const std::size_t s11 = s01 + 4u;
            for (std::size_t k = 0; k < 4; ++k)
            {
                const float top = _scratch[s00 + k] + (_scratch[s10 + k] - _scratch[s00 + k]) * fx;
                const float bottom = _scratch[s01 + k] + (_scratch[s11 + k] - _scratch[s01 + k]) * fx;
                _pixels[p + k] =
                    static_cast<std::uint8_t>(std::max(0.0f, std::min(255.0f, std::round(top + (bottom - top) * fy))));
            }
        }
    }
}

Raster WarpStroke::Image() const
{
    if (!_valid)
    {
        return {};
    }
    return Raster::Make(_width, _height, RasterFormat::Bgra8888, _pixels.data(), _pixels.size());
}

std::vector<RasterTile> WarpStroke::TakeChangedTiles()
{
    std::vector<RasterTile> tiles;
    if (!_valid)
    {
        return tiles;
    }
    const int rows = (_height + kTileSize - 1) / kTileSize;
    for (int ty = 0; ty < rows; ++ty)
    {
        for (int tx = 0; tx < _columns; ++tx)
        {
            std::uint8_t& flag = _dirty[static_cast<std::size_t>(ty) * static_cast<std::size_t>(_columns) +
                                        static_cast<std::size_t>(tx)];
            if (!flag)
            {
                continue;
            }
            flag = 0;
            const int x = tx * kTileSize;
            const int y = ty * kTileSize;
            const int width = std::min(kTileSize, _width - x);
            const int height = std::min(kTileSize, _height - y);
            std::vector<std::uint8_t> bytes(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u);
            for (int row = 0; row < height; ++row)
            {
                std::memcpy(bytes.data() + static_cast<std::size_t>(row) * static_cast<std::size_t>(width) * 4u,
                            _pixels.data() + (static_cast<std::size_t>(y + row) * static_cast<std::size_t>(_width) +
                                              static_cast<std::size_t>(x)) *
                                                 4u,
                            static_cast<std::size_t>(width) * 4u);
            }
            const Raster pixels = Raster::Make(width, height, RasterFormat::Bgra8888, bytes.data(), bytes.size());
            if (pixels.HasStorage())
            {
                tiles.push_back(RasterTile{x, y, pixels});
            }
        }
    }
    return tiles;
}
