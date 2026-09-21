#include "BrushStroke.h"

#include "Selection.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <new>

namespace
{
constexpr double kDensityCap = 20.0;
constexpr double kFalloffK = 2.5;
constexpr double kCurveTolerance = 0.2;
constexpr int kCurveDepth = 10;

[[nodiscard]] Vec2 Mix(Vec2 a, Vec2 b, double ta, double tb, double t) noexcept
{
    const double wa = (tb - t) / (tb - ta);
    const double wb = (t - ta) / (tb - ta);
    return Vec2{a.x * wa + b.x * wb, a.y * wa + b.y * wb};
}

[[nodiscard]] double Knot(double t, Vec2 a, Vec2 b) noexcept
{
    return t + std::max(0.0001, std::sqrt(std::hypot(b.x - a.x, b.y - a.y)));
}

// The half-open grid rectangle the corners of a document rectangle map to.
[[nodiscard]] GridRect MappedBox(const Affine2& map, double left, double top, double right, double bottom) noexcept
{
    const std::array<Vec2, 4> corners{map.Apply(Vec2{left, top}), map.Apply(Vec2{right, top}),
                                      map.Apply(Vec2{right, bottom}), map.Apply(Vec2{left, bottom})};
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
    if (!std::isfinite(minX) || !std::isfinite(maxX) || !std::isfinite(minY) || !std::isfinite(maxY))
    {
        return {};
    }
    const double clampLimit = 2.0 * BrushStroke::kMaxGridSide;
    minX = std::clamp(minX, -clampLimit, clampLimit);
    maxX = std::clamp(maxX, -clampLimit, clampLimit);
    minY = std::clamp(minY, -clampLimit, clampLimit);
    maxY = std::clamp(maxY, -clampLimit, clampLimit);
    const int x0 = static_cast<int>(std::floor(minX));
    const int y0 = static_cast<int>(std::floor(minY));
    const int x1 = static_cast<int>(std::ceil(maxX));
    const int y1 = static_cast<int>(std::ceil(maxY));
    return GridRect{x0, y0, std::max(0, x1 - x0), std::max(0, y1 - y0)};
}
} // namespace

GridRect GridRect::Intersect(const GridRect& other) const noexcept
{
    const int left = std::max(x, other.x);
    const int top = std::max(y, other.y);
    const int right = std::min(x + width, other.x + other.width);
    const int bottom = std::min(y + height, other.y + other.height);
    return GridRect{left, top, std::max(0, right - left), std::max(0, bottom - top)};
}

GridRect GridRect::Union(const GridRect& other) const noexcept
{
    if (Empty())
    {
        return other;
    }
    if (other.Empty())
    {
        return *this;
    }
    const int left = std::min(x, other.x);
    const int top = std::min(y, other.y);
    const int right = std::max(x + width, other.x + other.width);
    const int bottom = std::max(y + height, other.y + other.height);
    return GridRect{left, top, right - left, bottom - top};
}

bool BrushSettings::IsValid() const noexcept
{
    return std::isfinite(diameter) && diameter >= 1 && diameter <= 2000 && std::isfinite(hardness) && hardness >= 0 &&
           hardness <= 1 && std::isfinite(opacity) && opacity >= 0.01 && opacity <= 1 && std::isfinite(red) &&
           std::isfinite(green) && std::isfinite(blue) && red >= 0 && red <= 1 && green >= 0 && green <= 1 &&
           blue >= 0 && blue <= 1;
}

// ── Kernel ───────────────────────────────────────────────────────────────

namespace BrushKernel
{
double TipCoverage(double distance, const BrushKernelParams& params) noexcept
{
    if (params.hardness >= 1.0)
    {
        return std::clamp((params.radius - distance) / params.antialiasWidth + 0.5, 0.0, 1.0);
    }
    const double t = std::clamp((distance / params.radius - params.hardness) / (1.0 - params.hardness), 0.0, 1.0);
    return std::max(0.0, (std::exp(-kFalloffK * t * t) - std::exp(-kFalloffK)) / (1.0 - std::exp(-kFalloffK)));
}

double TipDensity(double distanceSquared, const BrushKernelParams& params) noexcept
{
    return -std::log(std::max(1.0 - TipCoverage(std::sqrt(distanceSquared), params), 0.001));
}

double SegmentDistanceSquared(Vec2 point, const BrushSegment& segment) noexcept
{
    const double vx = segment.to.x - segment.from.x;
    const double vy = segment.to.y - segment.from.y;
    const double lengthSquared = std::max(vx * vx + vy * vy, 1e-12);
    const double t =
        std::clamp(((point.x - segment.from.x) * vx + (point.y - segment.from.y) * vy) / lengthSquared, 0.0, 1.0);
    const double dx = point.x - (segment.from.x + t * vx);
    const double dy = point.y - (segment.from.y + t * vy);
    return dx * dx + dy * dy;
}

double SegmentDensity(Vec2 point, const BrushSegment& segment, const BrushKernelParams& params) noexcept
{
    const double vx = segment.to.x - segment.from.x;
    const double vy = segment.to.y - segment.from.y;
    const double length = std::hypot(vx, vy);
    const double px = point.x - segment.from.x;
    const double py = point.y - segment.from.y;
    if (length < 1e-6)
    {
        return TipDensity(px * px + py * py, params); // the initial click
    }
    const double dirX = vx / length;
    const double dirY = vy / length;
    const double projection = px * dirX + py * dirY;
    const double perpX = px - projection * dirX;
    const double perpY = py - projection * dirY;
    const double perpendicularSquared = perpX * perpX + perpY * perpY;
    const double radiusSquared = params.radius * params.radius;
    if (perpendicularSquared >= radiusSquared)
    {
        return 0.0;
    }
    const double reach = std::sqrt(radiusSquared - perpendicularSquared);
    const double lo = std::max(0.0, projection - reach);
    const double hi = std::min(length, projection + reach);
    if (hi <= lo)
    {
        return 0.0;
    }
    const double midpoint = (lo + hi) * 0.5;
    const double halfLength = (hi - lo) * 0.5;
    // Eight-point Gauss-Legendre quadrature, clipped to the tip's support.
    constexpr std::array<double, 4> nodes{0.1834346425, 0.5255324099, 0.7966664774, 0.9602898565};
    constexpr std::array<double, 4> weights{0.3626837834, 0.3137066459, 0.2223810345, 0.1012285363};
    double integral = 0.0;
    for (std::size_t i = 0; i < 4; ++i)
    {
        const double a = midpoint - halfLength * nodes[i] - projection;
        const double b = midpoint + halfLength * nodes[i] - projection;
        integral += weights[i] * (TipDensity(perpendicularSquared + a * a, params) +
                                  TipDensity(perpendicularSquared + b * b, params));
    }
    return integral * halfLength / params.spacing;
}

void RenderTile(const BrushKernelTile& tile, std::span<const BrushSegment> settled, std::span<const BrushSegment> tail,
                const BrushKernelParams& params) noexcept
{
    if (tile.preview == nullptr || tile.permanent == nullptr || tile.width <= 0 || tile.height <= 0)
    {
        return;
    }
    const bool hard = params.hardness >= 1.0;
    for (int py = 0; py < tile.height; ++py)
    {
        for (int px = 0; px < tile.width; ++px)
        {
            const std::size_t index =
                static_cast<std::size_t>(py) * static_cast<std::size_t>(tile.width) + static_cast<std::size_t>(px);
            const Vec2 p = params.pixelToDocument.Apply(Vec2{tile.x + px + 0.5, tile.y + py + 0.5});
            if (p.x < 0.0 || p.y < 0.0 || p.x >= params.canvasWidth || p.y >= params.canvasHeight)
            {
                tile.preview[index] = 0;
                continue;
            }
            if (hard)
            {
                // Hard tips already have a solid interior: keep the pixel-edge antialiasing.
                double settledMin = std::numeric_limits<double>::infinity();
                double tailMin = std::numeric_limits<double>::infinity();
                for (const BrushSegment& segment : settled)
                {
                    settledMin = std::min(settledMin, SegmentDistanceSquared(p, segment));
                }
                for (const BrushSegment& segment : tail)
                {
                    tailMin = std::min(tailMin, SegmentDistanceSquared(p, segment));
                }
                const double value =
                    std::max(static_cast<double>(tile.permanent[index]), TipCoverage(std::sqrt(settledMin), params));
                tile.permanent[index] = static_cast<float>(value);
                tile.preview[index] = static_cast<std::uint8_t>(
                    std::lround(255.0 * std::max(value, TipCoverage(std::sqrt(tailMin), params))));
            }
            else
            {
                double value = tile.permanent[index];
                double tailDensity = 0.0;
                for (const BrushSegment& segment : settled)
                {
                    value += SegmentDensity(p, segment, params);
                }
                for (const BrushSegment& segment : tail)
                {
                    tailDensity += SegmentDensity(p, segment, params);
                }
                value = std::min(value, kDensityCap);
                tile.permanent[index] = static_cast<float>(value);
                tile.preview[index] = static_cast<std::uint8_t>(
                    std::lround(255.0 * (1.0 - std::exp(-std::min(value + tailDensity, kDensityCap)))));
            }
        }
    }
}
} // namespace BrushKernel

bool CpuBrushCoverage::Render(std::span<const BrushKernelTile> tiles, std::span<const BrushSegment> settled,
                              std::span<const BrushSegment> tail, const BrushKernelParams& params) noexcept
{
    for (const BrushKernelTile& tile : tiles)
    {
        BrushKernel::RenderTile(tile, settled, tail, params);
    }
    return true;
}

CpuBrushCoverage& CpuBrushCoverage::Shared() noexcept
{
    static CpuBrushCoverage engine;
    return engine;
}

// ── Curve ────────────────────────────────────────────────────────────────

std::vector<BrushSegment> BrushCurveSegments(Vec2 before, Vec2 start, Vec2 end, Vec2 after)
{
    const double t0 = 0.0;
    const double t1 = Knot(t0, before, start);
    const double t2 = Knot(t1, start, end);
    const double t3 = Knot(t2, end, after);
    const auto point = [&](double u) -> Vec2
    {
        if (u <= 0.0)
        {
            return start;
        }
        if (u >= 1.0)
        {
            return end;
        }
        const double t = t1 + (t2 - t1) * u;
        const Vec2 a = Mix(before, start, t0, t1, t);
        const Vec2 b = Mix(start, end, t1, t2, t);
        const Vec2 c = Mix(end, after, t2, t3, t);
        return Mix(Mix(a, b, t0, t2, t), Mix(b, c, t1, t3, t), t1, t2, t);
    };
    std::vector<BrushSegment> result;
    // Adaptive chord subdivision keeps the centerline within the tolerance of the spline.
    const auto subdivide = [&](auto& self, Vec2 a, Vec2 b, double lo, double hi, int depth) -> void
    {
        const double dx = b.x - a.x;
        const double dy = b.y - a.y;
        const double lengthSquared = dx * dx + dy * dy;
        const auto error = [&](Vec2 p)
        {
            const double t =
                lengthSquared > 0 ? std::clamp(((p.x - a.x) * dx + (p.y - a.y) * dy) / lengthSquared, 0.0, 1.0) : 0.0;
            return std::hypot(p.x - a.x - t * dx, p.y - a.y - t * dy);
        };
        const double mid = (lo + hi) / 2.0;
        const Vec2 m = point(mid);
        const double deviation = std::max({error(m), error(point((lo + mid) / 2.0)), error(point((mid + hi) / 2.0))});
        if (deviation <= kCurveTolerance || depth >= kCurveDepth)
        {
            result.push_back(BrushSegment{a, b});
            return;
        }
        self(self, a, m, lo, mid, depth + 1);
        self(self, m, b, mid, hi, depth + 1);
    };
    subdivide(subdivide, start, end, 0.0, 1.0, 0);
    return result;
}

GridRect AlphaBounds(const std::uint8_t* bgra, int width, int height, int stride) noexcept
{
    if (bgra == nullptr || width <= 0 || height <= 0)
    {
        return {};
    }
    int left = width;
    int right = 0;
    int top = height;
    int bottom = 0;
    for (int y = 0; y < height; ++y)
    {
        const std::uint8_t* row = bgra + static_cast<std::size_t>(y) * static_cast<std::size_t>(stride);
        int first = 0;
        while (first < width && row[static_cast<std::size_t>(first) * 4u + 3u] == 0)
        {
            ++first;
        }
        if (first == width)
        {
            continue;
        }
        int last = width;
        while (last > first && row[static_cast<std::size_t>(last - 1) * 4u + 3u] == 0)
        {
            --last;
        }
        left = std::min(left, first);
        right = std::max(right, last);
        top = std::min(top, y);
        bottom = y + 1;
    }
    if (right == 0)
    {
        return {};
    }
    return GridRect{left, top, right - left, bottom - top};
}

// ── BrushStroke ──────────────────────────────────────────────────────────

BrushStroke::BrushStroke(const ImageLayer& layer, bool mask, const BrushSettings& settings, int canvasWidth,
                         int canvasHeight, BrushCoverageEngine* engine)
    : _layerID(layer.id), _mask(mask), _settings(settings), _canvasWidth(std::max(1, canvasWidth)),
      _canvasHeight(std::max(1, canvasHeight)), _engine(engine != nullptr ? engine : &CpuBrushCoverage::Shared())
{
    // A mask on its own placement is painted in its own pixel grid; otherwise the grid is the layer's.
    const bool placedMask = mask && layer.mask.has_value() && layer.mask->placement.has_value();
    const LayerTransform base = placedMask ? layer.mask->placement.value() : layer.transform;
    _sourceTransform = base;
    _source = mask ? (layer.mask.has_value() ? layer.mask->asset.image : Raster{})
                   : (layer.asset.has_value() ? layer.asset->image : Raster{});
    // The grid: a placed mask's own pixels; otherwise the layer's pixels (a mask on the layer's grid is stretched
    // over them, as a uniform 1 × 1 mask is), or the layer's size when it has no pixels yet.
    int originalWidth = 0;
    int originalHeight = 0;
    if (placedMask)
    {
        originalWidth = _source.Width();
        originalHeight = _source.Height();
    }
    else if (layer.asset.has_value() && layer.asset->image.HasStorage())
    {
        originalWidth = layer.asset->image.Width();
        originalHeight = layer.asset->image.Height();
    }
    else
    {
        originalWidth = static_cast<int>(std::lround(layer.transform.size.x));
        originalHeight = static_cast<int>(std::lround(layer.transform.size.y));
    }
    if (originalWidth < 1 || originalHeight < 1 || originalWidth > kMaxCanvasSide || originalHeight > kMaxCanvasSide ||
        !settings.IsValid() || !base.IsValid())
    {
        return;
    }
    const Affine2 originalMapping = ::PixelToDocument(base, originalWidth, originalHeight);
    const GridRect originalBounds{0, 0, originalWidth, originalHeight};
    GridRect extent = originalBounds;
    if (!mask)
    {
        extent = originalBounds.Union(MappedBox(originalMapping.Inverted(), 0.0, 0.0, static_cast<double>(_canvasWidth),
                                                static_cast<double>(_canvasHeight)));
    }
    if (extent.Empty() || extent.width > kMaxGridSide || extent.height > kMaxGridSide)
    {
        return;
    }
    _width = extent.width;
    _height = extent.height;
    _sourceRect = GridRect{-extent.x, -extent.y, originalWidth, originalHeight};
    _pixelToDocument = Affine2::Translation(extent.x, extent.y).Concat(originalMapping);
    LayerTransform expanded = base;
    expanded.size = Vec2{static_cast<double>(_width) * base.size.x / originalWidth,
                         static_cast<double>(_height) * base.size.y / originalHeight};
    const Vec2 center = originalMapping.Apply(Vec2{extent.x + extent.width / 2.0, extent.y + extent.height / 2.0});
    expanded.origin = Vec2{center.x - expanded.size.x / 2.0, center.y - expanded.size.y / 2.0};
    _paintTransform = expanded;
    _valid = true;
    _engine->BeginStroke();
}

BrushStroke::~BrushStroke()
{
    if (_valid && _engine != nullptr)
    {
        _engine->EndStroke();
    }
}

BrushKernelParams BrushStroke::KernelParams() const noexcept
{
    BrushKernelParams params;
    params.pixelToDocument = _pixelToDocument;
    params.radius = _settings.diameter / 2.0;
    params.hardness = _settings.hardness;
    params.canvasWidth = _canvasWidth;
    params.canvasHeight = _canvasHeight;
    params.antialiasWidth = std::max(0.001, std::min(std::hypot(_pixelToDocument.a, _pixelToDocument.b),
                                                     std::hypot(_pixelToDocument.c, _pixelToDocument.d)));
    params.spacing = std::max(0.25, _settings.diameter * BrushSettings::SpacingFraction(_settings.hardness));
    return params;
}

bool BrushStroke::Append(Vec2 point)
{
    if (!_valid || _failed || !std::isfinite(point.x) || !std::isfinite(point.y) || std::abs(point.x) > 1e7 ||
        std::abs(point.y) > 1e7)
    {
        return !_failed;
    }
    if (!_samples.empty() && _samples.back() == point)
    {
        return true;
    }
    _samples.push_back(point);
    if (_samples.size() > 4)
    {
        _samples.erase(_samples.begin());
    }
    const std::size_t n = _samples.size();
    std::vector<BrushSegment> settled;
    if (n == 1)
    {
        settled.push_back(BrushSegment{point, point});
    }
    else if (n >= 3)
    {
        settled = BrushCurveSegments(_samples[n >= 4 ? n - 4 : 0], _samples[n - 3], _samples[n - 2], point);
    }
    std::vector<BrushSegment> tail;
    if (n >= 2)
    {
        tail.push_back(BrushSegment{_samples[n - 2], point});
    }
    return Render(settled, tail);
}

bool BrushStroke::Flush()
{
    if (!_valid || _failed)
    {
        return !_failed;
    }
    const std::size_t n = _samples.size();
    if (n < 2)
    {
        return true;
    }
    const std::vector<BrushSegment> settled =
        BrushCurveSegments(_samples[n >= 3 ? n - 3 : 0], _samples[n - 2], _samples[n - 1], _samples[n - 1]);
    const bool ok = Render(settled, {});
    const Vec2 last = _samples[n - 1];
    _samples = {last};
    return ok;
}

void BrushStroke::TileKeys(std::span<const BrushSegment> segments, std::unordered_set<std::uint64_t>& keys) const
{
    const double reach = _settings.diameter / 2.0 + 2.0;
    const Affine2 inverse = _pixelToDocument.Inverted();
    const int columns = (_width + kTileSize - 1) / kTileSize;
    for (const BrushSegment& segment : segments)
    {
        const double left = std::max(0.0, std::min(segment.from.x, segment.to.x) - reach);
        const double top = std::max(0.0, std::min(segment.from.y, segment.to.y) - reach);
        const double right =
            std::min(static_cast<double>(_canvasWidth), std::max(segment.from.x, segment.to.x) + reach);
        const double bottom =
            std::min(static_cast<double>(_canvasHeight), std::max(segment.from.y, segment.to.y) + reach);
        if (right <= left || bottom <= top)
        {
            continue;
        }
        const GridRect affected =
            MappedBox(inverse, left, top, right, bottom).Intersect(GridRect{0, 0, _width, _height});
        if (affected.Empty())
        {
            continue;
        }
        const int firstColumn = affected.x / kTileSize;
        const int lastColumn = (affected.x + affected.width - 1) / kTileSize;
        const int firstRow = affected.y / kTileSize;
        const int lastRow = (affected.y + affected.height - 1) / kTileSize;
        for (int row = firstRow; row <= lastRow; ++row)
        {
            for (int column = firstColumn; column <= lastColumn; ++column)
            {
                keys.insert(static_cast<std::uint64_t>(row) * static_cast<std::uint64_t>(columns) +
                            static_cast<std::uint64_t>(column));
            }
        }
    }
}

bool BrushStroke::AllocateTile(std::uint64_t key)
{
    if (_index.contains(key))
    {
        return true;
    }
    const int columns = (_width + kTileSize - 1) / kTileSize;
    const int column = static_cast<int>(key % static_cast<std::uint64_t>(columns));
    const int row = static_cast<int>(key / static_cast<std::uint64_t>(columns));
    const GridRect rect{column * kTileSize, row * kTileSize, std::min(kTileSize, _width - column * kTileSize),
                        std::min(kTileSize, _height - row * kTileSize)};
    // Every allocated tile grows the committed bounds; they stay within the document limits and the pixel budget.
    const GridRect next =
        _allocated.has_value() ? _allocated->Union(rect) : (_source.HasStorage() ? _sourceRect.Union(rect) : rect);
    if (next.width > kMaxCanvasSide || next.height > kMaxCanvasSide ||
        static_cast<std::int64_t>(next.width) * static_cast<std::int64_t>(next.height) > _pixelLimit)
    {
        return false;
    }
    _allocated = next;
    Tile tile;
    tile.key = key;
    tile.rect = rect;
    try
    {
        const std::size_t count = static_cast<std::size_t>(rect.width) * static_cast<std::size_t>(rect.height);
        tile.coverage.assign(count, 0);
        if (_engine->UsesCpuPermanent())
        {
            tile.permanent.assign(count, 0.0f);
        }
        // The layer's pixels under the tile (a mask reveals beyond its pixels; an image is transparent there).
        const std::uint8_t fill = FillValue();
        const RasterFormat format = _mask ? RasterFormat::Gray8 : RasterFormat::Bgra8888;
        const int channels = _mask ? 1 : 4;
        std::vector<std::uint8_t> bytes(count * static_cast<std::size_t>(channels), fill);
        if (_source.HasStorage() && _source.Width() == _sourceRect.width && _source.Height() == _sourceRect.height)
        {
            _source.ReadRegion(rect.x - _sourceRect.x, rect.y - _sourceRect.y, rect.width, rect.height, bytes.data(),
                               rect.width * channels, fill);
        }
        else if (_source.HasStorage())
        {
            // A uniform mask (or any image stretched over the layer) reads the same pixel everywhere it covers.
            const GridRect covered = rect.Intersect(_sourceRect);
            for (int y = covered.y; y < covered.y + covered.height; ++y)
            {
                for (int x = covered.x; x < covered.x + covered.width; ++x)
                {
                    const int sx = static_cast<int>((static_cast<std::int64_t>(x - _sourceRect.x) * _source.Width()) /
                                                    _sourceRect.width);
                    const int sy = static_cast<int>((static_cast<std::int64_t>(y - _sourceRect.y) * _source.Height()) /
                                                    _sourceRect.height);
                    _source.ReadRegion(
                        sx, sy, 1, 1,
                        bytes.data() + (static_cast<std::size_t>(y - rect.y) * static_cast<std::size_t>(rect.width) +
                                        static_cast<std::size_t>(x - rect.x)) *
                                           static_cast<std::size_t>(channels),
                        channels, fill);
                }
            }
        }
        tile.base = Raster::Make(rect.width, rect.height, format, bytes.data(), bytes.size());
        tile.image = tile.base;
        _tiles.push_back(std::move(tile));
        _index.emplace(key, _tiles.size() - 1);
    }
    catch (const std::bad_alloc&)
    {
        return false;
    }
    return true;
}

bool BrushStroke::Render(const std::vector<BrushSegment>& settled, const std::vector<BrushSegment>& tail)
{
    std::unordered_set<std::uint64_t> tailKeys;
    TileKeys(tail, tailKeys);
    std::unordered_set<std::uint64_t> changed = tailKeys;
    TileKeys(settled, changed);
    changed.insert(_tailKeys.begin(), _tailKeys.end());
    std::vector<std::uint64_t> ordered(changed.begin(), changed.end());
    std::sort(ordered.begin(), ordered.end());
    for (const std::uint64_t key : ordered)
    {
        if (!AllocateTile(key))
        {
            _failed = true;
            return false;
        }
    }
    std::vector<BrushKernelTile> work;
    work.reserve(ordered.size());
    for (const std::uint64_t key : ordered)
    {
        Tile& tile = _tiles[_index[key]];
        work.push_back(BrushKernelTile{key, tile.rect.x, tile.rect.y, tile.rect.width, tile.rect.height,
                                       tile.permanent.empty() ? nullptr : tile.permanent.data(), tile.coverage.data()});
    }
    if (!work.empty() && !_engine->Render(work, settled, tail, KernelParams()))
    {
        _failed = true;
        return false;
    }
    _tailKeys = std::move(tailKeys);
    for (const std::uint64_t key : ordered)
    {
        Tile& tile = _tiles[_index[key]];
        tile.touched = true;
        ComposeTile(tile);
        tile.changed = true;
    }
    return true;
}

double BrushStroke::SelectionAt(Vec2 document) const noexcept
{
    if (!_selection.has_value())
    {
        return 1.0;
    }
    const Raster& coverage = _selection.value();
    const int x = static_cast<int>(std::floor(document.x));
    const int y = static_cast<int>(std::floor(document.y));
    if (x < 0 || y < 0 || x >= coverage.Width() || y >= coverage.Height() || coverage.Data() == nullptr)
    {
        return 0.0;
    }
    return coverage.Data()[static_cast<std::size_t>(y) * static_cast<std::size_t>(coverage.Stride()) +
                           static_cast<std::size_t>(x)] /
           255.0;
}

void BrushStroke::ComposeTile(Tile& tile)
{
    const int channels = _mask ? 1 : 4;
    const std::size_t count = static_cast<std::size_t>(tile.rect.width) * static_cast<std::size_t>(tile.rect.height);
    std::vector<std::uint8_t> bytes;
    try
    {
        bytes.resize(count * static_cast<std::size_t>(channels));
    }
    catch (const std::bad_alloc&)
    {
        return;
    }
    const std::uint8_t* base = tile.base.Data();
    if (base == nullptr)
    {
        return;
    }
    std::memcpy(bytes.data(), base, bytes.size());
    const double gray = _settings.red * 255.0;
    const double paintB = _settings.blue * 255.0;
    const double paintG = _settings.green * 255.0;
    const double paintR = _settings.red * 255.0;
    for (std::size_t index = 0; index < count; ++index)
    {
        const std::uint8_t coverage = tile.coverage[index];
        if (coverage == 0)
        {
            continue;
        }
        double c = coverage / 255.0 * _settings.opacity;
        if (_selection.has_value())
        {
            const int px = static_cast<int>(index % static_cast<std::size_t>(tile.rect.width));
            const int py = static_cast<int>(index / static_cast<std::size_t>(tile.rect.width));
            c *= SelectionAt(_pixelToDocument.Apply(Vec2{tile.rect.x + px + 0.5, tile.rect.y + py + 0.5}));
            if (c <= 0.0)
            {
                continue;
            }
        }
        std::uint8_t* out = bytes.data() + index * static_cast<std::size_t>(channels);
        const std::uint8_t* under = base + index * static_cast<std::size_t>(channels);
        const double keep = 1.0 - c;
        if (_mask)
        {
            out[0] = static_cast<std::uint8_t>(std::lround(gray * c + under[0] * keep));
        }
        else if (_settings.erasing)
        {
            // Erasing takes the coverage out of the layer's alpha, leaving the pixels under it transparent.
            for (int channel = 0; channel < 4; ++channel)
            {
                out[channel] = static_cast<std::uint8_t>(std::lround(under[channel] * keep));
            }
        }
        else
        {
            // Premultiplied paint (color × coverage) over the pixels under it.
            out[0] = static_cast<std::uint8_t>(std::lround(paintB * c + under[0] * keep));
            out[1] = static_cast<std::uint8_t>(std::lround(paintG * c + under[1] * keep));
            out[2] = static_cast<std::uint8_t>(std::lround(paintR * c + under[2] * keep));
            out[3] = static_cast<std::uint8_t>(std::lround(255.0 * c + under[3] * keep));
        }
    }
    tile.image = Raster::Make(tile.rect.width, tile.rect.height, _mask ? RasterFormat::Gray8 : RasterFormat::Bgra8888,
                              bytes.data(), bytes.size());
}

std::vector<std::size_t> BrushStroke::TakeChanged()
{
    std::vector<std::size_t> result;
    for (std::size_t index = 0; index < _tiles.size(); ++index)
    {
        if (_tiles[index].changed)
        {
            _tiles[index].changed = false;
            result.push_back(index);
        }
    }
    return result;
}

bool BrushStroke::HasPaint() const noexcept
{
    for (const Tile& tile : _tiles)
    {
        if (tile.touched)
        {
            return true;
        }
    }
    return false;
}

std::vector<RasterTile> BrushStroke::Patches() const
{
    std::vector<RasterTile> patches;
    for (const Tile& tile : _tiles)
    {
        if (tile.touched && tile.image.HasStorage())
        {
            patches.push_back(RasterTile{tile.rect.x, tile.rect.y, tile.image});
        }
    }
    return patches;
}

GridRect BrushStroke::CommittedBounds() const noexcept
{
    return _allocated.has_value() ? _allocated.value() : _sourceRect;
}

LayerTransform BrushStroke::TransformFor(const GridRect& bounds) const noexcept
{
    const Vec2 center = _pixelToDocument.Apply(Vec2{bounds.x + bounds.width / 2.0, bounds.y + bounds.height / 2.0});
    LayerTransform result = _paintTransform;
    result.size =
        Vec2{bounds.width * _paintTransform.size.x / _width, bounds.height * _paintTransform.size.y / _height};
    result.origin = Vec2{center.x - result.size.x / 2.0, center.y - result.size.y / 2.0};
    return result;
}

std::optional<BrushStroke::Commit> BrushStroke::PaintSnapshot() const
{
    if (!_valid || _failed)
    {
        return std::nullopt;
    }
    // Painting only adds alpha: the existing bounds stay valid and only touched tiles need inspecting.
    std::optional<GridRect> bounds;
    if (_source.HasStorage())
    {
        bounds = _sourceRect;
    }
    if (!_mask)
    {
        for (const Tile& tile : _tiles)
        {
            if (!tile.touched || tile.image.Data() == nullptr)
            {
                continue;
            }
            const GridRect painted =
                AlphaBounds(tile.image.Data(), tile.rect.width, tile.rect.height, tile.image.Stride());
            if (painted.Empty())
            {
                continue;
            }
            const GridRect placed{painted.x + tile.rect.x, painted.y + tile.rect.y, painted.width, painted.height};
            bounds = bounds.has_value() ? bounds->Union(placed) : placed;
        }
    }
    const GridRect crop = bounds.has_value() ? bounds.value() : CommittedBounds();
    if (crop.Empty())
    {
        return std::nullopt;
    }
    Commit commit;
    commit.bounds = crop;
    // A source stretched over the grid (a uniform mask) cannot sit under the tiles as a base: its one value is
    // the fill instead, which is exact because a mask grid never grows past the layer.
    Raster base = _source;
    std::uint8_t fill = FillValue();
    if (_source.HasStorage() && (_source.Width() != _sourceRect.width || _source.Height() != _sourceRect.height))
    {
        base = Raster{};
        std::uint8_t uniform = fill;
        _source.ReadRegion(0, 0, 1, 1, &uniform, _source.Channels(), fill);
        fill = uniform;
    }
    commit.image =
        Raster::Replacing(base, _sourceRect.x, _sourceRect.y, Patches(), crop.x, crop.y, crop.width, crop.height, fill);
    if (!commit.image.HasStorage())
    {
        return std::nullopt;
    }
    commit.thumbnail = Raster::MakeThumbnail(commit.image);
    commit.transform = TransformFor(crop);
    if (!commit.transform.IsValid())
    {
        return std::nullopt;
    }
    return commit;
}

// ── DocumentSession ──────────────────────────────────────────────────────

namespace
{
[[nodiscard]] bool EffectivelyVisible(const CanvasDocument& document, const Guid& id) noexcept
{
    std::optional<Guid> current = id;
    int depth = 0;
    while (current.has_value() && depth < kMaxLayerCount)
    {
        const ImageLayer* found = nullptr;
        for (const ImageLayer& layer : document.layers)
        {
            if (layer.id == current.value())
            {
                found = &layer;
                break;
            }
        }
        if (found == nullptr || !found->visible)
        {
            return false;
        }
        current = found->parentID;
        ++depth;
    }
    return true;
}
} // namespace

bool DocumentSession::CanPaint() const noexcept
{
    if (!_document.has_value() || _transformEdit.has_value() || _brush)
    {
        return false;
    }
    const ImageLayer* layer = ActiveLayer();
    if (layer == nullptr)
    {
        return false;
    }
    // A folder has no pixels of its own, so only its mask can be painted.
    if (layer->isGroup && !_maskSelected)
    {
        return false;
    }
    // An explicitly empty selection leaves nothing paintable.
    if (_document->selection.has_value() && _document->selection->IsEmpty())
    {
        return false;
    }
    if (!EffectivelyVisible(_document.value(), layer->id))
    {
        return false;
    }
    if (_maskSelected)
    {
        return layer->mask.has_value() && layer->mask->enabled;
    }
    return !layer->adjustment.has_value();
}

std::int64_t DocumentSession::RasterEditPixelLimit(const ImageLayer& layer, bool mask) const noexcept
{
    if (!_document.has_value())
    {
        return 0;
    }
    std::int64_t used = 0;
    std::int64_t maskUsed = 0;
    for (const ImageLayer& other : _document->layers)
    {
        if (other.id == layer.id)
        {
            continue;
        }
        if (other.asset.has_value())
        {
            used += static_cast<std::int64_t>(other.asset->image.PixelCount());
        }
        if (other.mask.has_value())
        {
            maskUsed += static_cast<std::int64_t>(other.mask->asset.image.PixelCount());
        }
    }
    std::int64_t limit = static_cast<std::int64_t>(mask ? kMaxMaskPixels : kMaxLayerPixels) - (mask ? maskUsed : used);
    if (!mask && layer.mask.has_value())
    {
        limit = std::min(limit, static_cast<std::int64_t>(kMaxMaskPixels) - maskUsed);
    }
    return std::max<std::int64_t>(0, limit);
}

bool DocumentSession::BeginBrush(Vec2 point, bool erase)
{
    if (!CanPaint())
    {
        return false;
    }
    const ImageLayer* layer = ActiveLayer();
    const bool mask = _maskSelected;
    BrushSettings settings = brushSettings;
    settings.erasing = erase && !mask;
    if (mask)
    {
        settings.red = maskPaintWhite ? 1.0 : 0.0;
        settings.green = settings.red;
        settings.blue = settings.red;
    }
    std::shared_ptr<BrushStroke> stroke;
    try
    {
        stroke =
            std::make_shared<BrushStroke>(*layer, mask, settings, _document->width, _document->height, _brushEngine);
    }
    catch (const std::bad_alloc&)
    {
        return false;
    }
    if (!stroke->IsValid())
    {
        return false;
    }
    stroke->SetPixelLimit(RasterEditPixelLimit(*layer, mask));
    if (_document->selection.has_value())
    {
        const DocumentSelection& selection = _document->selection.value();
        stroke->SetSelectionClip(
            SelectionGeometry::Coverage(selection.loops, _document->width, _document->height, selection.antialiased));
    }
    _lastBrushPoint = LastBrushPoint{point, layer->id, mask};
    if (!stroke->Append(point))
    {
        return false;
    }
    _brush = std::move(stroke);
    return true;
}

void DocumentSession::ContinueBrush(Vec2 point)
{
    if (!_brush)
    {
        return;
    }
    if (_lastBrushPoint.has_value())
    {
        _lastBrushPoint->point = point;
    }
    if (!_brush->Append(point))
    {
        CancelBrush();
    }
}

void DocumentSession::CancelBrush() noexcept
{
    _brush.reset();
}

void DocumentSession::FinishBrush()
{
    if (!_brush)
    {
        return;
    }
    _lastBrushCommit.reset();
    const std::shared_ptr<BrushStroke> stroke = std::move(_brush);
    _brush.reset();
    if (!stroke->Flush() || !stroke->HasPaint() || !_document.has_value())
    {
        return;
    }
    const std::optional<BrushStroke::Commit> result = stroke->PaintSnapshot();
    if (!result.has_value())
    {
        return;
    }
    ImageLayer* current = MutableLayer(stroke->LayerID());
    if (current == nullptr)
    {
        return;
    }
    // The raster was built from this layer's pixels and transform; never write it over content that changed.
    if (stroke->IsMask())
    {
        if (!current->mask.has_value() || !(current->mask->asset.image == stroke->SourceImage()))
        {
            return;
        }
    }
    else
    {
        const Raster currentImage = current->asset.has_value() ? current->asset->image : Raster{};
        if (!(currentImage == stroke->SourceImage()) || current->transform != stroke->SourceTransform())
        {
            return;
        }
    }
    const std::wstring name = stroke->IsMask()             ? L"Paint Mask"
                              : stroke->Settings().erasing ? L"Erase"
                                                           : L"Brush Stroke";
    BeginEdit(name);
    if (stroke->IsMask())
    {
        current->mask->asset.image = result->image;
        current->mask->asset.thumbnail = result->thumbnail;
    }
    else
    {
        // A mask on the layer's own grid grows with the layer; new area is revealed. A uniform mask stays as is.
        if (current->mask.has_value() && !current->mask->placement.has_value() &&
            result->bounds != stroke->SourceRect() && current->mask->asset.image.PixelCount() > 1)
        {
            const GridRect& source = stroke->SourceRect();
            const Raster grown = Raster::Replacing(current->mask->asset.image, source.x, source.y, {}, result->bounds.x,
                                                   result->bounds.y, result->bounds.width, result->bounds.height, 255);
            if (grown.HasStorage())
            {
                current->mask->asset.image = grown;
                current->mask->asset.thumbnail = Raster::MakeThumbnail(grown);
            }
        }
        const std::wstring layerName = current->name;
        current->asset = RasterAsset{result->image, result->thumbnail, layerName};
        current->transform = result->transform;
    }
    ++_editRevision;
    EndEdit();
    _lastBrushCommit = BrushCommitInfo{result->image,        result->bounds.x,      result->bounds.y,
                                       result->bounds.width, result->bounds.height, stroke->IsMask()};
}

std::optional<Vec2> DocumentSession::ShiftLineStart() const noexcept
{
    if (!_lastBrushPoint.has_value() || !_activeLayerID.has_value() ||
        !(_lastBrushPoint->layerID == _activeLayerID.value()) || _lastBrushPoint->mask != _maskSelected)
    {
        return std::nullopt;
    }
    return _lastBrushPoint->point;
}

void DocumentSession::ChangeBrushSize(bool increase) noexcept
{
    if (_brush)
    {
        return;
    }
    // A step of a fifth, but always at least one pixel, so the smallest brushes stay reachable.
    const double current = brushSettings.diameter;
    const double stepped = increase ? std::max(current + 1.0, std::round(current * 1.2))
                                    : std::min(current - 1.0, std::round(current / 1.2));
    brushSettings.diameter = std::clamp(stepped, 1.0, 2000.0);
}

void DocumentSession::ChangeBrushHardness(bool increase) noexcept
{
    if (_brush)
    {
        return;
    }
    // Snap to the next quarter up or down, so 80 % goes to 100 % or 75 %.
    const double quarter = brushSettings.hardness * 4.0;
    const double step = increase ? std::floor(quarter + 0.001) + 1.0 : std::ceil(quarter - 0.001) - 1.0;
    brushSettings.hardness = std::clamp(step, 0.0, 4.0) / 4.0;
}

void DocumentSession::TypeOpacityDigit(int digit, double timeSeconds) noexcept
{
    if (_brush || digit < 0 || digit > 9)
    {
        return;
    }
    int percent = digit == 0 ? 100 : digit * 10;
    if (_pendingOpacityDigit.has_value() && timeSeconds - _pendingOpacityDigit->second < 0.6)
    {
        percent = std::max(1, _pendingOpacityDigit->first * 10 + digit);
        _pendingOpacityDigit.reset();
    }
    else
    {
        _pendingOpacityDigit = std::make_pair(digit, timeSeconds);
    }
    brushSettings.opacity = percent / 100.0;
}

void DocumentSession::AddLayerMask(bool revealing)
{
    ImageLayer* layer = ActiveLayer();
    if (!_document.has_value() || layer == nullptr || layer->mask.has_value())
    {
        return;
    }
    CommitTransform();
    const std::uint8_t value = revealing ? 255 : 0;
    const Raster mask = Raster::Make(1, 1, RasterFormat::Gray8, &value, 1);
    BeginEdit(revealing ? L"Add Reveal-All Mask" : L"Add Hide-All Mask");
    layer->mask = LayerMask{RasterAsset{mask, mask, layer->name}, true, std::nullopt, true};
    ++_editRevision;
    EndEdit();
    _maskSelected = true;
}

void DocumentSession::ToggleLayerMask(const Guid& id)
{
    ImageLayer* layer = MutableLayer(id);
    if (layer == nullptr || !layer->mask.has_value())
    {
        return;
    }
    BeginEdit(layer->mask->enabled ? L"Disable Layer Mask" : L"Enable Layer Mask");
    layer->mask->enabled = !layer->mask->enabled;
    ++_editRevision;
    EndEdit();
}
