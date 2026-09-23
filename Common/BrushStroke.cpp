#include "BrushStroke.h"

#include "Heal.h"
#include "ImageFilters.h"
#include "TransformEdit.h"
#include "WarpStroke.h"

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
    const SelectionBounds box = PointBounds(corners);
    if (!std::isfinite(box.minX) || !std::isfinite(box.maxX) || !std::isfinite(box.minY) || !std::isfinite(box.maxY))
    {
        return {};
    }
    const double clampLimit = 2.0 * BrushStroke::kMaxGridSide;
    const double minX = std::clamp(box.minX, -clampLimit, clampLimit);
    const double maxX = std::clamp(box.maxX, -clampLimit, clampLimit);
    const double minY = std::clamp(box.minY, -clampLimit, clampLimit);
    const double maxY = std::clamp(box.maxY, -clampLimit, clampLimit);
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

LayerTransform PlacementOfRect(const LayerTransform& placed, int width, int height, const Affine2& pixelToDocument,
                               const GridRect& rect) noexcept
{
    LayerTransform result = placed;
    result.size = Vec2{rect.width * placed.size.x / width, rect.height * placed.size.y / height};
    const Vec2 center = pixelToDocument.Apply(Vec2{rect.x + rect.width / 2.0, rect.y + rect.height / 2.0});
    result.origin = Vec2{center.x - result.size.x / 2.0, center.y - result.size.y / 2.0};
    return result;
}

LayerTransform PlacementOfRect(const LayerTransform& placed, int width, int height, const GridRect& rect) noexcept
{
    return PlacementOfRect(placed, width, height, PixelToDocument(placed, width, height), rect);
}

// ── BrushStroke ──────────────────────────────────────────────────────────

BrushStroke::BrushStroke(const ImageLayer& layer, bool mask, const BrushSettings& settings, int canvasWidth,
                         int canvasHeight, BrushCoverageEngine* engine, BrushKind kind, SpotHealingMode healingMode)
    : _layerID(layer.id), _mask(mask), _settings(settings), _canvasWidth(std::max(1, canvasWidth)),
      _canvasHeight(std::max(1, canvasHeight)), _engine(engine != nullptr ? engine : &CpuBrushCoverage::Shared()),
      _kind(kind), _healingMode(healingMode)
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
    _paintTransform = PlacementOfRect(base, originalWidth, originalHeight, originalMapping, extent);
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
        if (!ComposeTile(tile))
        {
            _failed = true;
            return false;
        }
        tile.touched = true;
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

namespace
{
[[nodiscard]] bool SampleAt(const Raster& raster, double x, double y, std::array<double, 4>& out) noexcept;
}

bool BrushStroke::ComposeTile(Tile& tile)
{
    // The pointer hot path never catches: an allocation failure here is fatal (AGENTS.md, Core_Architecture.md).
    const std::uint8_t* base = tile.base.Data();
    if (base == nullptr)
    {
        return false;
    }
    const int channels = _mask ? 1 : 4;
    const std::size_t count = static_cast<std::size_t>(tile.rect.width) * static_cast<std::size_t>(tile.rect.height);
    std::vector<std::uint8_t> bytes(base, base + count * static_cast<std::size_t>(channels));
    const double gray = _settings.red * 255.0;
    const double paintB = _settings.blue * 255.0;
    const double paintG = _settings.green * 255.0;
    const double paintR = _settings.red * 255.0;
    const bool sampling = _sample.has_value() && _sample->HasStorage() &&
                          _sample->Format() == (_mask ? RasterFormat::Gray8 : RasterFormat::Bgra8888);
    const bool washing = !sampling && Healing() && !_mask;
    const auto round8 = [](double value)
    { return static_cast<std::uint8_t>(std::lround(std::clamp(value, 0.0, 255.0))); };
    for (std::size_t index = 0; index < count; ++index)
    {
        const std::uint8_t coverage = tile.coverage[index];
        if (coverage == 0)
        {
            continue;
        }
        const int px = static_cast<int>(index % static_cast<std::size_t>(tile.rect.width));
        const int py = static_cast<int>(index / static_cast<std::size_t>(tile.rect.width));
        const Vec2 document = _pixelToDocument.Apply(Vec2{tile.rect.x + px + 0.5, tile.rect.y + py + 0.5});
        // While painting, the area to heal shows as a dark wash at a fixed 45 %, as in Photoshop; Heal() rebuilds
        // it from its surroundings when the stroke ends. Everything else takes the opacity.
        double c = coverage / 255.0 * (washing ? 0.45 : _settings.opacity);
        if (_selection.has_value())
        {
            c *= SelectionAt(document);
            if (c <= 0.0)
            {
                continue;
            }
        }
        std::uint8_t* out = bytes.data() + index * static_cast<std::size_t>(channels);
        const std::uint8_t* under = base + index * static_cast<std::size_t>(channels);
        const double keep = 1.0 - c;
        if (sampling)
        {
            std::array<double, 4> sample{};
            if (!SampleAt(*_sample, document.x + _sampleX, document.y + _sampleY, sample))
            {
                continue;
            }
            if (_mask)
            {
                out[0] = round8(sample[0] * c + under[0] * keep);
            }
            else if (_replaces)
            {
                // The sample replaces what is under the tip, so it can also clear pixels.
                for (int channel = 0; channel < 4; ++channel)
                {
                    out[channel] = round8(sample[channel] * c + under[channel] * keep);
                }
            }
            else
            {
                // Premultiplied source-over with the sample's own alpha.
                const double hold = 1.0 - sample[3] / 255.0 * c;
                for (int channel = 0; channel < 4; ++channel)
                {
                    out[channel] = round8(sample[channel] * c + under[channel] * hold);
                }
            }
        }
        else if (washing)
        {
            constexpr double kWash = 0.12 * 255.0;
            out[0] = round8(kWash * c + under[0] * keep);
            out[1] = round8(kWash * c + under[1] * keep);
            out[2] = round8(kWash * c + under[2] * keep);
            out[3] = round8(255.0 * c + under[3] * keep);
        }
        else if (_mask)
        {
            out[0] = round8(gray * c + under[0] * keep);
        }
        else if (Erasing())
        {
            // Erasing takes the coverage out of the layer's alpha, leaving the pixels under it transparent.
            for (int channel = 0; channel < 4; ++channel)
            {
                out[channel] = round8(under[channel] * keep);
            }
        }
        else
        {
            // Premultiplied paint (color × coverage) over the pixels under it.
            out[0] = round8(paintB * c + under[0] * keep);
            out[1] = round8(paintG * c + under[1] * keep);
            out[2] = round8(paintR * c + under[2] * keep);
            out[3] = round8(255.0 * c + under[3] * keep);
        }
    }
    tile.image = Raster::Make(tile.rect.width, tile.rect.height, _mask ? RasterFormat::Gray8 : RasterFormat::Bgra8888,
                              bytes.data(), bytes.size());
    return tile.image.HasStorage();
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
    return PlacementOfRect(_paintTransform, _width, _height, _pixelToDocument, bounds);
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
    // A source stretched over the grid (a mask whose pixels differ in size from its layer's) cannot sit under the
    // tiles as it is: a uniform 1 × 1 mask becomes its one value as the fill (exact, since a mask grid never grows
    // past the layer), any other is resampled onto the grid the way the tiles read it, so untouched pixels stay.
    // Beyond the source the fill is FillValue(); a sparse source keeps its own fill (Raster::Replacing).
    Raster base = _source;
    std::uint8_t fill = FillValue();
    if (_source.HasStorage() && (_source.Width() != _sourceRect.width || _source.Height() != _sourceRect.height))
    {
        if (_source.Width() == 1 && _source.Height() == 1)
        {
            base = Raster{};
            std::uint8_t uniform = fill;
            _source.ReadRegion(0, 0, 1, 1, &uniform, _source.Channels(), fill);
            fill = uniform;
        }
        else
        {
            base = _source.Stretched(_sourceRect.width, _sourceRect.height);
            if (!base.HasStorage())
            {
                return std::nullopt;
            }
        }
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

bool EffectivelyVisible(const CanvasDocument& document, const Guid& id) noexcept
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

bool DocumentSession::CanPaint() const noexcept
{
    if (!_document.has_value() || _transformEdit.has_value() || _brush || HasColorEdit())
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
    std::int64_t limit = PixelBudgetLeft(mask, {layer.id});
    // An image edit that grows the layer grows a mask on its grid too.
    if (!mask && layer.mask.has_value())
    {
        limit = std::min(limit, PixelBudgetLeft(true, {layer.id}));
    }
    return limit;
}

bool DocumentSession::BeginBrush(Vec2 point, bool erase)
{
    return BeginBrush(point, erase ? BrushKind::Erase : BrushKind::Paint);
}

bool DocumentSession::BeginBrush(Vec2 point, BrushKind kind, const Raster* composite)
{
    // A pending gradient applies first, so the stroke paints over it and never drops it (Decision D3).
    ResolveGradient();
    if (!CanPaint())
    {
        return false;
    }
    const ImageLayer* layer = ActiveLayer();
    const bool mask = _maskSelected;
    // Spot Healing and Clone Stamp rework image pixels; they have nothing to do on a mask.
    if (mask && (kind == BrushKind::Heal || kind == BrushKind::Clone))
    {
        return false;
    }
    BrushSettings settings = brushSettings;
    if (mask)
    {
        settings.red = maskPaintWhite ? 1.0 : 0.0;
        settings.green = settings.red;
        settings.blue = settings.red;
    }
    std::optional<Raster> sample;
    int offsetX = 0;
    int offsetY = 0;
    std::wstring editName;
    if (kind == BrushKind::Clone)
    {
        const std::optional<Vec2> offset = CloneStrokeOffset(point);
        if (!offset.has_value())
        {
            return false;
        }
        sample = CloneSample(composite);
        if (!sample->HasStorage())
        {
            return false;
        }
        offsetX = static_cast<int>(std::lround(offset->x));
        offsetY = static_cast<int>(std::lround(offset->y));
        _cloneOffset = offset;
        editName = L"Clone Stamp";
    }
    else if (kind == BrushKind::Blur)
    {
        sample = BlurSample(mask);
        if (!sample->HasStorage())
        {
            return false;
        }
        editName = L"Blur";
    }
    else if (kind == BrushKind::Heal)
    {
        editName = L"Spot Healing";
    }
    std::shared_ptr<BrushStroke> stroke = MakeRasterEdit(*layer, mask, settings, kind);
    if (!stroke)
    {
        return false;
    }
    if (sample.has_value())
    {
        stroke->SetSample(std::move(sample.value()), offsetX, offsetY, false);
    }
    stroke->SetEditName(std::move(editName));
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
    _lastBrushCommit.reset();
    if (!_brush)
    {
        return;
    }
    const std::shared_ptr<BrushStroke> stroke = std::move(_brush);
    _brush.reset();
    if (!stroke->Flush())
    {
        return;
    }
    if (stroke->Healing() && !stroke->Heal(NextHealSeed()))
    {
        return;
    }
    if (!stroke->HasPaint())
    {
        return;
    }
    CommitStroke(stroke);
}

bool DocumentSession::CommitStroke(const std::shared_ptr<BrushStroke>& stroke,
                                   const std::optional<DocumentSelection>& selectionAfterCommit)
{
    if (!stroke || !_document.has_value())
    {
        return false;
    }
    const std::optional<BrushStroke::Commit> result = stroke->PaintSnapshot();
    if (!result.has_value())
    {
        return false;
    }
    ImageLayer* current = MutableLayer(stroke->LayerID());
    if (current == nullptr)
    {
        return false;
    }
    // The raster was built from this layer's pixels and transform; never write it over content that changed.
    if (stroke->IsMask())
    {
        if (!current->mask.has_value() || !(current->mask->asset.image == stroke->SourceImage()))
        {
            return false;
        }
    }
    else
    {
        const Raster currentImage = current->asset.has_value() ? current->asset->image : Raster{};
        if (!(currentImage == stroke->SourceImage()) || current->transform != stroke->SourceTransform())
        {
            return false;
        }
    }
    const std::wstring name = !stroke->EditName().empty() ? stroke->EditName()
                              : stroke->IsMask()          ? L"Paint Mask"
                              : stroke->Erasing()         ? L"Erase"
                                                          : L"Brush Stroke";
    BeginEdit(name);
    if (stroke->IsMask())
    {
        current->mask->asset.image = result->image;
        current->mask->asset.thumbnail = result->thumbnail;
    }
    else
    {
        // A mask on the layer's own grid grows with the layer; only the new area is revealed (the old one keeps the
        // mask's pixels and fill). A uniform mask stays as is.
        if (current->mask.has_value() && !current->mask->placement.has_value() &&
            result->bounds != stroke->SourceRect() && current->mask->asset.image.PixelCount() > 1)
        {
            const GridRect& source = stroke->SourceRect();
            const Raster grown =
                CarriedMask(current->mask->asset.image, source.width, source.height, source.x - result->bounds.x,
                            source.y - result->bounds.y, result->bounds.width, result->bounds.height, 255);
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
    if (selectionAfterCommit.has_value())
    {
        _document->selection = selectionAfterCommit;
    }
    ++_editRevision;
    EndEdit();
    _lastBrushCommit = BrushCommitInfo{result->image,        result->bounds.x,      result->bounds.y,
                                       result->bounds.width, result->bounds.height, stroke->IsMask()};
    return true;
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
    if (_brush)
    {
        return;
    }
    const int percent = TypedOpacityPercent(digit, timeSeconds);
    if (percent > 0)
    {
        brushSettings.opacity = percent / 100.0;
    }
}

int DocumentSession::TypedOpacityPercent(int digit, double timeSeconds) noexcept
{
    if (digit < 0 || digit > 9)
    {
        return -1;
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
    return percent;
}

void DocumentSession::AddLayerMask(bool revealing)
{
    // Targeting the new mask is a target switch: a pending gradient applies first.
    ResolveGradient();
    ImageLayer* layer = ActiveLayer();
    if (!_document.has_value() || layer == nullptr || layer->mask.has_value())
    {
        return;
    }
    CommitTransform();
    if (PixelBudgetLeft(true) < 1)
    {
        _budgetRefused = true;
        return;
    }
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

// ── Samples, healing and canvas fills (C11) ───────────────────────────────

namespace
{
// `raster` read bilinearly at document coordinates (pixel centers at half coordinates), into premultiplied BGRA (or
// the gray in [0]); false outside the raster, where nothing is copied.
[[nodiscard]] bool SampleAt(const Raster& raster, double x, double y, std::array<double, 4>& out) noexcept
{
    const int width = raster.Width();
    const int height = raster.Height();
    const std::uint8_t* data = raster.Data();
    if (data == nullptr || !(x >= 0.0) || !(y >= 0.0) || !(x < width) || !(y < height))
    {
        return false;
    }
    const int channels = raster.Channels();
    const std::size_t stride = static_cast<std::size_t>(raster.Stride());
    const double sx = std::clamp(x - 0.5, 0.0, static_cast<double>(width - 1));
    const double sy = std::clamp(y - 0.5, 0.0, static_cast<double>(height - 1));
    const double fx0 = std::floor(sx);
    const double fy0 = std::floor(sy);
    const double tx = sx - fx0;
    const double ty = sy - fy0;
    const int ix = static_cast<int>(fx0);
    const int iy = static_cast<int>(fy0);
    const auto at = [&](int px, int py, int channel) -> double
    {
        px = std::clamp(px, 0, width - 1);
        py = std::clamp(py, 0, height - 1);
        return data[static_cast<std::size_t>(py) * stride +
                    static_cast<std::size_t>(px) * static_cast<std::size_t>(channels) +
                    static_cast<std::size_t>(channel)];
    };
    for (int channel = 0; channel < channels; ++channel)
    {
        const double top = at(ix, iy, channel) + (at(ix + 1, iy, channel) - at(ix, iy, channel)) * tx;
        const double bottom = at(ix, iy + 1, channel) + (at(ix + 1, iy + 1, channel) - at(ix, iy + 1, channel)) * tx;
        out[static_cast<std::size_t>(channel)] = top + (bottom - top) * ty;
    }
    return true;
}
} // namespace

void BrushStroke::SetSample(Raster sample, int offsetX, int offsetY, bool replaces)
{
    _sample = std::move(sample);
    _sampleX = offsetX;
    _sampleY = offsetY;
    _replaces = replaces;
}

bool BrushStroke::Heal(std::uint32_t seed)
{
    if (!_valid || _failed || !Healing() || _mask)
    {
        return false;
    }
    std::optional<GridRect> painted;
    for (const Tile& tile : _tiles)
    {
        if (!tile.touched)
        {
            continue;
        }
        const SpotHealing::Bounds edges = SpotHealing::CoverageBounds(
            tile.coverage.data(), static_cast<std::size_t>(tile.rect.width), static_cast<std::size_t>(tile.rect.height),
            static_cast<std::size_t>(tile.rect.width));
        if (edges.Empty())
        {
            continue;
        }
        const GridRect rect{static_cast<int>(edges.x0) + tile.rect.x, static_cast<int>(edges.y0) + tile.rect.y,
                            static_cast<int>(edges.x1 - edges.x0), static_cast<int>(edges.y1 - edges.y0)};
        painted = painted.has_value() ? painted->Union(rect) : rect;
    }
    if (!painted.has_value())
    {
        return true;
    }
    // Room for the kernel's patch search, which looks up to about three spot-widths away.
    const int reach = static_cast<int>(std::ceil((std::max(painted->width, painted->height) + 32) * 3.2));
    const GridRect region =
        GridRect{painted->x - reach, painted->y - reach, painted->width + 2 * reach, painted->height + 2 * reach}
            .Intersect(GridRect{0, 0, _width, _height});
    if (region.Empty())
    {
        return true;
    }
    const std::size_t w = static_cast<std::size_t>(region.width);
    const std::size_t h = static_cast<std::size_t>(region.height);
    std::vector<std::uint8_t> pixels;
    std::vector<std::uint8_t> gray;
    try
    {
        pixels.assign(w * h * 4u, 0);
        gray.assign(w * h, 0);
    }
    catch (const std::bad_alloc&)
    {
        return false;
    }
    // The layer's original pixels within the region (transparent beyond them), never the wash.
    if (_source.HasStorage())
    {
        _source.ReadRegion(region.x - _sourceRect.x, region.y - _sourceRect.y, region.width, region.height,
                           pixels.data(), region.width * 4, 0);
    }
    for (const Tile& tile : _tiles)
    {
        if (!tile.touched)
        {
            continue;
        }
        const GridRect overlap = tile.rect.Intersect(region);
        for (int y = overlap.y; y < overlap.y + overlap.height; ++y)
        {
            std::memcpy(gray.data() + static_cast<std::size_t>(y - region.y) * w +
                            static_cast<std::size_t>(overlap.x - region.x),
                        tile.coverage.data() +
                            static_cast<std::size_t>(y - tile.rect.y) * static_cast<std::size_t>(tile.rect.width) +
                            static_cast<std::size_t>(overlap.x - tile.rect.x),
                        static_cast<std::size_t>(overlap.width));
        }
    }
    if (!SpotHealing::Heal(pixels.data(), gray.data(), w, h, w * 4u, static_cast<float>(_settings.opacity),
                           _healingMode, seed))
    {
        return false;
    }
    // The healed region replaces the wash in every touched tile, within the selection.
    for (Tile& tile : _tiles)
    {
        if (!tile.touched || tile.base.Data() == nullptr)
        {
            continue;
        }
        const std::size_t count =
            static_cast<std::size_t>(tile.rect.width) * static_cast<std::size_t>(tile.rect.height) * 4u;
        std::vector<std::uint8_t> bytes;
        try
        {
            bytes.assign(tile.base.Data(), tile.base.Data() + count);
        }
        catch (const std::bad_alloc&)
        {
            return false;
        }
        const GridRect overlap = tile.rect.Intersect(region);
        for (int y = overlap.y; y < overlap.y + overlap.height; ++y)
        {
            for (int x = overlap.x; x < overlap.x + overlap.width; ++x)
            {
                const std::uint8_t* healed =
                    pixels.data() +
                    (static_cast<std::size_t>(y - region.y) * w + static_cast<std::size_t>(x - region.x)) * 4u;
                std::uint8_t* out = bytes.data() + (static_cast<std::size_t>(y - tile.rect.y) *
                                                        static_cast<std::size_t>(tile.rect.width) +
                                                    static_cast<std::size_t>(x - tile.rect.x)) *
                                                       4u;
                double clip = 1.0;
                if (_selection.has_value())
                {
                    clip = SelectionAt(_pixelToDocument.Apply(Vec2{x + 0.5, y + 0.5}));
                }
                for (int channel = 0; channel < 4; ++channel)
                {
                    out[channel] =
                        static_cast<std::uint8_t>(std::lround(healed[channel] * clip + out[channel] * (1.0 - clip)));
                }
            }
        }
        tile.image =
            Raster::Make(tile.rect.width, tile.rect.height, RasterFormat::Bgra8888, bytes.data(), bytes.size());
        tile.changed = true;
    }
    return true;
}

bool BrushStroke::PaintCanvas(bool withinSource, double opacity, PaintMode mode,
                              const std::function<bool(Vec2, std::array<double, 4>&)>& shade)
{
    if (!_valid || _failed)
    {
        return false;
    }
    // The area: the canvas, within the selection's bounds when there is one. Without a selection Invert covers
    // every pixel the layer has, on or off the canvas.
    const bool everywhere = mode == PaintMode::Invert && !_selection.has_value();
    double areaX0 = 0;
    double areaY0 = 0;
    double areaX1 = _canvasWidth;
    double areaY1 = _canvasHeight;
    if (_selection.has_value() && _selection->Data() != nullptr)
    {
        const SpotHealing::Bounds bounds = SpotHealing::CoverageBounds(
            _selection->Data(), static_cast<std::size_t>(_selection->Width()),
            static_cast<std::size_t>(_selection->Height()), static_cast<std::size_t>(_selection->Stride()));
        if (bounds.Empty())
        {
            return true;
        }
        areaX0 = std::max(areaX0, static_cast<double>(bounds.x0));
        areaY0 = std::max(areaY0, static_cast<double>(bounds.y0));
        areaX1 = std::min(areaX1, static_cast<double>(bounds.x1));
        areaY1 = std::min(areaY1, static_cast<double>(bounds.y1));
        if (areaX1 <= areaX0 || areaY1 <= areaY0)
        {
            return true;
        }
    }
    GridRect affected = everywhere ? _sourceRect
                                   : MappedBox(_pixelToDocument.Inverted(), areaX0, areaY0, areaX1, areaY1)
                                         .Intersect(GridRect{0, 0, _width, _height});
    if (withinSource)
    {
        affected = affected.Intersect(_sourceRect);
    }
    if (affected.Empty())
    {
        return true;
    }
    const int columns = (_width + kTileSize - 1) / kTileSize;
    const int channels = _mask ? 1 : 4;
    const auto round8 = [](double value)
    { return static_cast<std::uint8_t>(std::lround(std::clamp(value, 0.0, 255.0))); };
    for (int ty = affected.y / kTileSize; ty <= (affected.y + affected.height - 1) / kTileSize; ++ty)
    {
        for (int tx = affected.x / kTileSize; tx <= (affected.x + affected.width - 1) / kTileSize; ++tx)
        {
            const std::uint64_t key =
                static_cast<std::uint64_t>(ty) * static_cast<std::uint64_t>(columns) + static_cast<std::uint64_t>(tx);
            if (!AllocateTile(key))
            {
                _failed = true;
                return false;
            }
            Tile& tile = _tiles[_index[key]];
            const std::uint8_t* base = tile.base.Data();
            if (base == nullptr)
            {
                _failed = true;
                return false;
            }
            const std::size_t count =
                static_cast<std::size_t>(tile.rect.width) * static_cast<std::size_t>(tile.rect.height);
            std::vector<std::uint8_t> bytes;
            try
            {
                bytes.assign(base, base + count * static_cast<std::size_t>(channels));
            }
            catch (const std::bad_alloc&)
            {
                _failed = true;
                return false;
            }
            for (std::size_t index = 0; index < count; ++index)
            {
                const int px = static_cast<int>(index % static_cast<std::size_t>(tile.rect.width));
                const int py = static_cast<int>(index / static_cast<std::size_t>(tile.rect.width));
                if (withinSource && (tile.rect.x + px < _sourceRect.x || tile.rect.y + py < _sourceRect.y ||
                                     tile.rect.x + px >= _sourceRect.x + _sourceRect.width ||
                                     tile.rect.y + py >= _sourceRect.y + _sourceRect.height))
                {
                    continue;
                }
                const Vec2 document = _pixelToDocument.Apply(Vec2{tile.rect.x + px + 0.5, tile.rect.y + py + 0.5});
                if (!everywhere && (!(document.x >= 0.0) || !(document.y >= 0.0) || !(document.x < _canvasWidth) ||
                                    !(document.y < _canvasHeight)))
                {
                    continue;
                }
                double a = opacity;
                if (_selection.has_value())
                {
                    a *= SelectionAt(document);
                }
                std::array<double, 4> color{};
                if (a <= 0.0 || !shade(document, color))
                {
                    continue;
                }
                a *= std::clamp(color[3], 0.0, 1.0);
                if (a <= 0.0)
                {
                    continue;
                }
                std::uint8_t* out = bytes.data() + index * static_cast<std::size_t>(channels);
                const double keep = 1.0 - a;
                if (mode == PaintMode::DestinationOut)
                {
                    for (int channel = 0; channel < channels; ++channel)
                    {
                        out[channel] = round8(out[channel] * keep);
                    }
                }
                else if (mode == PaintMode::Invert)
                {
                    // Premultiplied: each color becomes alpha − color, so transparency is kept; a mask flips.
                    if (_mask)
                    {
                        out[0] = round8((255.0 - out[0]) * a + out[0] * keep);
                    }
                    else
                    {
                        for (int channel = 0; channel < 3; ++channel)
                        {
                            out[channel] = round8((out[3] - out[channel]) * a + out[channel] * keep);
                        }
                    }
                }
                else if (_mask)
                {
                    out[0] = round8(std::clamp(color[0], 0.0, 1.0) * 255.0 * a + out[0] * keep);
                }
                else
                {
                    out[0] = round8(std::clamp(color[2], 0.0, 1.0) * 255.0 * a + out[0] * keep);
                    out[1] = round8(std::clamp(color[1], 0.0, 1.0) * 255.0 * a + out[1] * keep);
                    out[2] = round8(std::clamp(color[0], 0.0, 1.0) * 255.0 * a + out[2] * keep);
                    out[3] = round8(255.0 * a + out[3] * keep);
                }
            }
            tile.image = Raster::Make(tile.rect.width, tile.rect.height,
                                      _mask ? RasterFormat::Gray8 : RasterFormat::Bgra8888, bytes.data(), bytes.size());
            tile.touched = true;
            tile.changed = true;
        }
    }
    return true;
}

bool BrushStroke::FillGradient(GradientShape shape, Vec2 start, Vec2 end, const std::array<double, 4>& from,
                               const std::array<double, 4>& to, double opacity)
{
    const double dx = end.x - start.x;
    const double dy = end.y - start.y;
    const double length2 = dx * dx + dy * dy;
    const double length = std::sqrt(length2);
    return PaintCanvas(false, std::clamp(opacity, 0.0, 1.0), PaintMode::Over,
                       [&](Vec2 point, std::array<double, 4>& color)
                       {
                           double t = 0.0;
                           if (shape == GradientShape::Linear)
                           {
                               t = length2 > 0.0 ? ((point.x - start.x) * dx + (point.y - start.y) * dy) / length2
                                                 : 0.0;
                           }
                           else
                           {
                               t = length > 0.0 ? std::hypot(point.x - start.x, point.y - start.y) / length : 0.0;
                           }
                           t = std::clamp(t, 0.0, 1.0);
                           for (std::size_t channel = 0; channel < 4; ++channel)
                           {
                               color[channel] = from[channel] + (to[channel] - from[channel]) * t;
                           }
                           return true;
                       });
}

bool BrushStroke::Fill(const std::array<double, 4>& color)
{
    return PaintCanvas(false, 1.0, PaintMode::Over,
                       [&](Vec2, std::array<double, 4>& out)
                       {
                           out = color;
                           return true;
                       });
}

bool BrushStroke::ClearPixels()
{
    return PaintCanvas(true, 1.0, PaintMode::DestinationOut,
                       [](Vec2, std::array<double, 4>& out)
                       {
                           out = {0.0, 0.0, 0.0, 1.0};
                           return true;
                       });
}

// ── Clone Stamp, Blur and the warp modes on the session ───────────────────

void DocumentSession::SetCloneSource(Vec2 point) noexcept
{
    if (!std::isfinite(point.x) || !std::isfinite(point.y))
    {
        return;
    }
    cloneSource = point;
    _cloneOffset.reset();
}

std::optional<Vec2> DocumentSession::CloneStrokeOffset(Vec2 point) const noexcept
{
    if (!cloneSource.has_value())
    {
        return std::nullopt;
    }
    if (cloneSettings.aligned && _cloneOffset.has_value())
    {
        return _cloneOffset;
    }
    return Vec2{std::round(cloneSource->x - point.x), std::round(cloneSource->y - point.y)};
}

std::optional<Vec2> DocumentSession::CloneSamplePoint(Vec2 point) const noexcept
{
    if (!cloneSource.has_value())
    {
        return std::nullopt;
    }
    if (!_cloneOffset.has_value() || (!cloneSettings.aligned && !_brush))
    {
        return cloneSource;
    }
    return Vec2{point.x + _cloneOffset->x, point.y + _cloneOffset->y};
}

Raster DocumentSession::CloneSample(const Raster* composite) const
{
    return LayerSample(cloneSettings.sampleAllLayers, composite);
}

Raster DocumentSession::BlurSample(bool mask) const
{
    const ImageLayer* layer = ActiveLayer();
    if (!_document.has_value() || layer == nullptr)
    {
        return {};
    }
    const CanvasDocument& document = _document.value();
    const double sigma = std::min(30.0, std::max(1.5, brushSettings.diameter / 10.0));
    if (mask)
    {
        if (!layer->mask.has_value())
        {
            return {};
        }
        // Past its pixels a mask keeps its edge tone, so blurring near its edge doesn't pull in the wrong one.
        const std::uint8_t background =
            static_cast<std::uint8_t>(std::lround(MaskEdgeBackground(layer->mask->asset.thumbnail) * 255.0));
        const Raster sharp = ImageFilters::RenderMaskPixels(layer->mask->asset.image, MaskTransform(*layer),
                                                            document.width, document.height, background);
        return ImageFilters::GaussianBlur(sharp, sigma);
    }
    if (layer->isGroup || !layer->asset.has_value())
    {
        return {};
    }
    const Raster sharp =
        RenderLayerPixels(layer->asset->image, DisplayedTransform(*layer), document.width, document.height);
    return ImageFilters::GaussianBlur(sharp, sigma);
}

bool DocumentSession::BeginWarp(Vec2 point)
{
    ResolveGradient();
    if (!CanPaint() || _maskSelected || _warp)
    {
        return false;
    }
    const ImageLayer* layer = ActiveLayer();
    if (layer == nullptr || layer->isGroup)
    {
        return false;
    }
    const CanvasDocument& document = _document.value();
    const Raster image =
        layer->asset.has_value()
            ? RenderLayerPixels(layer->asset->image, DisplayedTransform(*layer), document.width, document.height)
            : Raster::Make(document.width, document.height, RasterFormat::Bgra8888);
    std::shared_ptr<WarpStroke> warp;
    try
    {
        warp = std::make_shared<WarpStroke>(image, smearMode, brushSettings);
    }
    catch (const std::bad_alloc&)
    {
        return false;
    }
    if (!warp->IsValid())
    {
        return false;
    }
    _warpSource = WarpSource{layer->id, layer->asset.has_value() ? layer->asset->image : Raster{}, layer->transform};
    warp->Append(point);
    _warp = std::move(warp);
    _lastBrushPoint = LastBrushPoint{point, layer->id, false};
    return true;
}

void DocumentSession::ContinueWarp(Vec2 point)
{
    if (!_warp)
    {
        return;
    }
    _warp->Append(point);
    if (_lastBrushPoint.has_value())
    {
        _lastBrushPoint->point = point;
    }
}

void DocumentSession::CancelWarp() noexcept
{
    _warp.reset();
}

void DocumentSession::FinishWarp()
{
    if (!_warp)
    {
        return;
    }
    const std::shared_ptr<WarpStroke> warp = std::move(_warp);
    _warp.reset();
    if (warp->Points().empty() || !_document.has_value())
    {
        return;
    }
    const ImageLayer* current = MutableLayer(_warpSource.layerID);
    const Raster currentImage = current != nullptr && current->asset.has_value() ? current->asset->image : Raster{};
    if (current == nullptr || !(currentImage == _warpSource.image) || current->transform != _warpSource.transform)
    {
        return;
    }
    // A hard tip a little wider than the brush covers everything the stroke moved.
    BrushSettings settings = brushSettings;
    settings.diameter = std::min(2000.0, warp->Diameter() + 4.0);
    settings.hardness = 1.0;
    settings.opacity = 1.0;
    // Neither erase nor heal: MakeRasterEdit's default BrushKind::Paint applies.
    const std::shared_ptr<BrushStroke> stroke = MakeRasterEdit(*current, false, settings);
    if (!stroke)
    {
        return;
    }
    stroke->SetSample(warp->Image(), 0, 0, true);
    stroke->SetEditName(warp->Mode() == SmearMode::Liquify ? L"Liquify" : L"Smudge");
    for (const Vec2& point : warp->Points())
    {
        if (!stroke->Append(point))
        {
            return;
        }
    }
    if (!stroke->Flush() || !stroke->HasPaint())
    {
        return;
    }
    CommitStroke(stroke);
}

void DocumentSession::SwitchBrushTipFamily(int from, int to) noexcept
{
    if (from == to || from < 0 || to < 0 || from >= static_cast<int>(_parkedTips.size()) ||
        to >= static_cast<int>(_parkedTips.size()))
    {
        return;
    }
    _parkedTips[static_cast<std::size_t>(from)] = {brushSettings.diameter, brushSettings.hardness,
                                                   brushSettings.opacity};
    const std::array<double, 3>& parked = _parkedTips[static_cast<std::size_t>(to)];
    brushSettings.diameter = parked[0];
    brushSettings.hardness = parked[1];
    brushSettings.opacity = parked[2];
}

// ── Gradient, Shape and the eyedropper (C11) ──────────────────────────────

namespace
{
[[nodiscard]] std::array<double, 4> StraightColor(std::uint32_t argb, double alpha) noexcept
{
    return {((argb >> 16) & 0xFFu) / 255.0, ((argb >> 8) & 0xFFu) / 255.0, (argb & 0xFFu) / 255.0, alpha};
}

// A rectangle with rounded corners as one clockwise loop; the arcs are fine enough to read as curves.
[[nodiscard]] std::vector<SelectionLoop> RoundedRectLoops(double width, double height, double radius)
{
    SelectionLoop loop;
    const int segments = std::max(4, static_cast<int>(std::ceil(radius * 3.14159265358979323846 / 4.0)));
    const auto arc = [&](double cx, double cy, double from)
    {
        for (int i = 0; i <= segments; ++i)
        {
            const double angle = from + (3.14159265358979323846 / 2.0) * i / segments;
            loop.push_back(Vec2{cx + std::cos(angle) * radius, cy + std::sin(angle) * radius});
        }
    };
    // Clockwise in top-left coordinates: top edge, right edge, bottom edge, left edge.
    arc(width - radius, radius, -3.14159265358979323846 / 2.0);
    arc(width - radius, height - radius, 0.0);
    arc(radius, height - radius, 3.14159265358979323846 / 2.0);
    arc(radius, radius, 3.14159265358979323846);
    return {loop};
}
} // namespace

std::optional<LayerShapeStyle> LiveShape(const ImageLayer& layer) noexcept
{
    if (!layer.shape.has_value() || !layer.asset.has_value() || !layer.shapeImage.HasStorage() ||
        !(layer.asset->image == layer.shapeImage))
    {
        return std::nullopt;
    }
    return layer.shape;
}

std::array<std::array<double, 4>, 2> DocumentSession::GradientColors() const noexcept
{
    // A mask has its own palette (legacy paletteColor): black hides and white reveals, the foreground per
    // `maskPaintWhite` and the background the other one.
    const bool mask = _gradient ? _gradient->raster->IsMask() : _maskSelected;
    constexpr std::array<double, 4> kBlack{0.0, 0.0, 0.0, 1.0};
    constexpr std::array<double, 4> kWhite{1.0, 1.0, 1.0, 1.0};
    const std::array<double, 4> foreground =
        mask ? (maskPaintWhite ? kWhite : kBlack) : StraightColor(_foregroundArgb, 1.0);
    const std::array<double, 4> background =
        mask ? (maskPaintWhite ? kBlack : kWhite) : StraightColor(_backgroundArgb, 1.0);
    std::array<std::array<double, 4>, 2> colors{foreground, foreground};
    if (gradientSettings.style == GradientStyle::ForegroundToBackground)
    {
        colors[1] = background;
    }
    else
    {
        colors[1][3] = 0.0;
    }
    if (gradientSettings.reversed)
    {
        std::swap(colors[0], colors[1]);
    }
    return colors;
}

bool DocumentSession::BeginGradient(Vec2 point)
{
    if (!_document.has_value() || !std::isfinite(point.x) || !std::isfinite(point.y))
    {
        return false;
    }
    const ImageLayer* layer = ActiveLayer();
    if (layer == nullptr)
    {
        return false;
    }
    // Dragging a new line replaces the pending one on the same target; a line on another target applies the
    // pending one first (Decision D3), never dropping it.
    if (_gradient && _gradient->raster->LayerID() == layer->id && _gradient->raster->IsMask() == _maskSelected)
    {
        _gradient->start = point;
        _gradient->end = point;
        RefreshGradient();
        return _gradient.has_value();
    }
    ResolveGradient();
    layer = ActiveLayer();
    if (layer == nullptr || !CanPaint())
    {
        return false;
    }
    const bool mask = _maskSelected;
    std::shared_ptr<BrushStroke> raster = MakeRasterEdit(*layer, mask);
    if (!raster)
    {
        return false;
    }
    raster->SetEditName(mask ? L"Gradient Mask" : L"Gradient");
    // Vec2 and a moved shared_ptr cannot throw: no allocation is left to catch (unlike a pixel move's selection
    // copy).
    _gradient = GradientEdit{std::move(raster), point, point};
    return true;
}

void DocumentSession::MoveGradient(std::optional<Vec2> start, std::optional<Vec2> end)
{
    if (!_gradient)
    {
        return;
    }
    if (start.has_value() && std::isfinite(start->x) && std::isfinite(start->y))
    {
        _gradient->start = start.value();
    }
    if (end.has_value() && std::isfinite(end->x) && std::isfinite(end->y))
    {
        _gradient->end = end.value();
    }
    RefreshGradient();
}

void DocumentSession::RefreshGradient()
{
    if (!_gradient || !_gradient->HasLine())
    {
        return;
    }
    const std::array<std::array<double, 4>, 2> colors = GradientColors();
    if (!_gradient->raster->FillGradient(gradientSettings.shape, _gradient->start, _gradient->end, colors[0], colors[1],
                                         gradientSettings.opacity))
    {
        CancelGradient();
    }
}

void DocumentSession::EndGradientDrag()
{
    if (_gradient && !_gradient->HasLine())
    {
        CancelGradient();
    }
}

void DocumentSession::CancelGradient() noexcept
{
    _gradient.reset();
}

void DocumentSession::CommitGradient()
{
    if (!_gradient)
    {
        return;
    }
    const GradientEdit edit = std::move(_gradient).value();
    _gradient.reset();
    if (!edit.HasLine())
    {
        return;
    }
    CommitStroke(edit.raster);
}

void DocumentSession::ResolveGradient()
{
    if (_gradient)
    {
        CommitGradient();
    }
}

void DocumentSession::BeginShape(Vec2 point)
{
    if (!_document.has_value() || _transformEdit.has_value() || !std::isfinite(point.x) || !std::isfinite(point.y))
    {
        return;
    }
    const Vec2 anchor{std::round(point.x), std::round(point.y)};
    _shapeDraft = ShapeDraft{shapeKind, anchor, SelectionBounds{anchor.x, anchor.y, anchor.x, anchor.y},
                             shapeKind == ShapeKind::Rectangle ? std::max(0.0, shapeCornerRadius) : 0.0};
}

void DocumentSession::DragShape(Vec2 point, bool square, bool fromCenter)
{
    if (!_shapeDraft.has_value() || !std::isfinite(point.x) || !std::isfinite(point.y))
    {
        return;
    }
    _shapeDraft->rect = DragBox(_shapeDraft->anchor, point, square, fromCenter);
}

void DocumentSession::CancelShape() noexcept
{
    _shapeDraft.reset();
}

void DocumentSession::ToggleShapeKind()
{
    CancelShape();
    shapeKind = shapeKind == ShapeKind::Rectangle ? ShapeKind::Ellipse : ShapeKind::Rectangle;
}

std::wstring DocumentSession::NextShapeName(ShapeKind kind) const
{
    return UniqueLayerName(kind == ShapeKind::Rectangle ? L"Rectangle " : L"Ellipse ");
}

Raster DocumentSession::ShapeImage(ShapeKind kind, int width, int height, std::uint32_t argb, double cornerRadius)
{
    if (width < 1 || height < 1 ||
        static_cast<std::int64_t>(width) * static_cast<std::int64_t>(height) > kMaxShapePixels)
    {
        return {};
    }
    std::vector<SelectionLoop> loops;
    if (kind == ShapeKind::Ellipse)
    {
        loops = SelectionGeometry::Ellipse(0, 0, width, height);
    }
    else
    {
        // A rectangle's corners round by the radius, at most half its shorter side (so a large radius makes a pill).
        const double radius = std::min({std::max(0.0, cornerRadius), width / 2.0, height / 2.0});
        loops = radius > 0.0 ? RoundedRectLoops(width, height, radius) : SelectionGeometry::Rect(0, 0, width, height);
    }
    const Raster coverage = SelectionGeometry::Coverage(loops, width, height, true);
    const std::uint8_t* mask = coverage.Data();
    if (mask == nullptr)
    {
        return {};
    }
    const std::array<double, 4> color = StraightColor(argb, 1.0);
    std::vector<std::uint8_t> bytes;
    try
    {
        bytes.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u);
    }
    catch (const std::bad_alloc&)
    {
        return {};
    }
    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            const std::size_t index =
                static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x);
            const double c = mask[static_cast<std::size_t>(y) * static_cast<std::size_t>(coverage.Stride()) +
                                  static_cast<std::size_t>(x)] /
                             255.0;
            std::uint8_t* out = bytes.data() + index * 4u;
            out[0] = static_cast<std::uint8_t>(std::lround(color[2] * 255.0 * c));
            out[1] = static_cast<std::uint8_t>(std::lround(color[1] * 255.0 * c));
            out[2] = static_cast<std::uint8_t>(std::lround(color[0] * 255.0 * c));
            out[3] = static_cast<std::uint8_t>(std::lround(255.0 * c));
        }
    }
    return Raster::Make(width, height, RasterFormat::Bgra8888, bytes.data(), bytes.size());
}

void DocumentSession::FinishShape()
{
    if (!_shapeDraft.has_value())
    {
        return;
    }
    const ShapeDraft draft = _shapeDraft.value();
    _shapeDraft.reset();
    const double width = draft.rect.maxX - draft.rect.minX;
    const double height = draft.rect.maxY - draft.rect.minY;
    if (!_document.has_value() || _transformEdit.has_value() || width < 1.0 || height < 1.0)
    {
        return;
    }
    const int pixelWidth = static_cast<int>(std::lround(width));
    const int pixelHeight = static_cast<int>(std::lround(height));
    if (static_cast<std::int64_t>(pixelWidth) * static_cast<std::int64_t>(pixelHeight) > kMaxShapePixels)
    {
        return;
    }
    const Raster image = ShapeImage(draft.kind, pixelWidth, pixelHeight, _foregroundArgb, draft.cornerRadius);
    if (!image.HasStorage())
    {
        return;
    }
    const std::array<double, 4> color = StraightColor(_foregroundArgb, 1.0);
    const LayerShapeStyle style{draft.kind, color[0], color[1], color[2], draft.cornerRadius};
    AddPixelLayer(image, Vec2{draft.rect.minX, draft.rect.minY}, NextShapeName(draft.kind),
                  draft.kind == ShapeKind::Rectangle ? L"Rectangle" : L"Ellipse", false, style);
}

void DocumentSession::AddPixelLayer(const Raster& image, Vec2 origin, std::wstring name, const std::wstring& editName,
                                    bool dropsSelection, std::optional<LayerShapeStyle> shape)
{
    ResolveGradient();
    if (!_document.has_value() || !image.HasStorage())
    {
        return;
    }
    // Decision D7: the new layer's pixels fit the document's budget, or nothing is added.
    if (static_cast<std::int64_t>(image.PixelCount()) > PixelBudgetLeft(false))
    {
        _budgetRefused = true;
        return;
    }
    ImageLayer layer =
        ImageLayer::Blank(name, Vec2{static_cast<double>(image.Width()), static_cast<double>(image.Height())});
    layer.asset = RasterAsset{image, Raster::MakeThumbnail(image), std::move(name)};
    layer.transform.origin = origin;
    layer.shape = shape;
    if (shape.has_value())
    {
        layer.shapeImage = image;
    }
    InsertLayerAboveActive(std::move(layer), editName, dropsSelection);
}

void DocumentSession::RedrawShape(std::size_t index)
{
    if (!_document.has_value() || index >= _document->layers.size())
    {
        return;
    }
    ImageLayer& layer = _document->layers[index];
    const std::optional<LayerShapeStyle> shape = LiveShape(layer);
    if (!shape.has_value() || !layer.asset.has_value())
    {
        return;
    }
    const int width = std::max(1, static_cast<int>(std::lround(layer.transform.size.x)));
    const int height = std::max(1, static_cast<int>(std::lround(layer.transform.size.y)));
    // Past the document's budget (Decision D7) the shape keeps its pixels, stretched, instead of redrawing.
    if ((width == layer.asset->image.Width() && height == layer.asset->image.Height()) ||
        static_cast<std::int64_t>(width) * static_cast<std::int64_t>(height) > kMaxShapePixels ||
        static_cast<std::int64_t>(width) * static_cast<std::int64_t>(height) > PixelBudgetLeft(false, {layer.id}))
    {
        return;
    }
    const std::uint32_t argb =
        0xFF000000u | (static_cast<std::uint32_t>(std::lround(std::clamp(shape->red, 0.0, 1.0) * 255.0)) << 16) |
        (static_cast<std::uint32_t>(std::lround(std::clamp(shape->green, 0.0, 1.0) * 255.0)) << 8) |
        static_cast<std::uint32_t>(std::lround(std::clamp(shape->blue, 0.0, 1.0) * 255.0));
    const Raster image = ShapeImage(shape->kind, width, height, argb, shape->cornerRadius);
    if (!image.HasStorage())
    {
        return;
    }
    // A mask that follows the layer's pixel grid stays exactly where it is while that grid changes size.
    if (layer.mask.has_value() && !layer.mask->placement.has_value())
    {
        layer.mask->placement = MaskTransform(layer);
    }
    const std::wstring name = layer.asset->name;
    layer.asset = RasterAsset{image, Raster::MakeThumbnail(image), name};
    layer.shapeImage = image;
}

std::optional<std::uint32_t> DocumentSession::SampleCompositeColor(const Raster& composite, Vec2 point) noexcept
{
    const std::uint8_t* data =
        composite.HasStorage() && composite.Format() == RasterFormat::Bgra8888 ? composite.Data() : nullptr;
    if (data == nullptr || !std::isfinite(point.x) || !std::isfinite(point.y) || point.x < 0.0 || point.y < 0.0 ||
        point.x >= composite.Width() || point.y >= composite.Height())
    {
        return std::nullopt;
    }
    const int x = static_cast<int>(std::floor(point.x));
    const int y = static_cast<int>(std::floor(point.y));
    const std::uint8_t* pixel = data + static_cast<std::size_t>(y) * static_cast<std::size_t>(composite.Stride()) +
                                static_cast<std::size_t>(x) * 4u;
    const double alpha = pixel[3];
    if (alpha <= 0.0)
    {
        return std::nullopt;
    }
    const auto channel = [&](std::uint8_t value) -> std::uint32_t
    { return static_cast<std::uint32_t>(std::lround(std::min(alpha, static_cast<double>(value)) / alpha * 255.0)); };
    return 0xFF000000u | (channel(pixel[2]) << 16) | (channel(pixel[1]) << 8) | channel(pixel[0]);
}

// ── Lifted pixels and the invert pass ─────────────────────────────────────

bool BrushStroke::Invert()
{
    return PaintCanvas(true, 1.0, PaintMode::Invert,
                       [](Vec2, std::array<double, 4>& out)
                       {
                           out = {0.0, 0.0, 0.0, 1.0};
                           return true;
                       });
}

bool BrushStroke::LiftSelection()
{
    if (!_valid || _failed || _mask || !_source.HasStorage() || !_selection.has_value() ||
        _selection->Data() == nullptr)
    {
        return false;
    }
    const SpotHealing::Bounds bounds = SpotHealing::CoverageBounds(
        _selection->Data(), static_cast<std::size_t>(_selection->Width()),
        static_cast<std::size_t>(_selection->Height()), static_cast<std::size_t>(_selection->Stride()));
    if (bounds.Empty())
    {
        return false;
    }
    const GridRect region =
        MappedBox(_pixelToDocument.Inverted(), static_cast<double>(bounds.x0), static_cast<double>(bounds.y0),
                  static_cast<double>(bounds.x1), static_cast<double>(bounds.y1))
            .Intersect(_sourceRect);
    if (region.Empty())
    {
        return false;
    }
    std::vector<std::uint8_t> bytes;
    try
    {
        bytes.assign(static_cast<std::size_t>(region.width) * static_cast<std::size_t>(region.height) * 4u, 0);
    }
    catch (const std::bad_alloc&)
    {
        return false;
    }
    // The layer's pixels under the selection, faded by its coverage.
    _source.ReadRegion(region.x - _sourceRect.x, region.y - _sourceRect.y, region.width, region.height, bytes.data(),
                       region.width * 4, 0);
    for (int y = 0; y < region.height; ++y)
    {
        for (int x = 0; x < region.width; ++x)
        {
            const double coverage = SelectionAt(_pixelToDocument.Apply(Vec2{region.x + x + 0.5, region.y + y + 0.5}));
            std::uint8_t* pixel = bytes.data() + (static_cast<std::size_t>(y) * static_cast<std::size_t>(region.width) +
                                                  static_cast<std::size_t>(x)) *
                                                     4u;
            for (int channel = 0; channel < 4; ++channel)
            {
                pixel[channel] = static_cast<std::uint8_t>(std::lround(pixel[channel] * coverage));
            }
        }
    }
    const Raster image = Raster::Make(region.width, region.height, RasterFormat::Bgra8888, bytes.data(), bytes.size());
    if (!image.HasStorage())
    {
        return false;
    }
    _lifted = Lifted{image, region};
    return true;
}

bool BrushStroke::AllocateTilesIn(const GridRect& rect, std::vector<std::uint64_t>& keys)
{
    const GridRect clipped = rect.Intersect(GridRect{0, 0, _width, _height});
    if (clipped.Empty())
    {
        return true;
    }
    const int columns = (_width + kTileSize - 1) / kTileSize;
    for (int ty = clipped.y / kTileSize; ty <= (clipped.y + clipped.height - 1) / kTileSize; ++ty)
    {
        for (int tx = clipped.x / kTileSize; tx <= (clipped.x + clipped.width - 1) / kTileSize; ++tx)
        {
            const std::uint64_t key =
                static_cast<std::uint64_t>(ty) * static_cast<std::uint64_t>(columns) + static_cast<std::uint64_t>(tx);
            if (!AllocateTile(key))
            {
                return false;
            }
            keys.push_back(key);
        }
    }
    return true;
}

bool BrushStroke::MoveLifted(Vec2 offset, bool duplicate)
{
    if (!_lifted.has_value() || !_selection.has_value())
    {
        return false;
    }
    // A pointer update that rounds to the offset already shown changes nothing.
    if (_moved.has_value() && _moved->offset == offset && _moved->duplicate == duplicate)
    {
        return true;
    }
    const Affine2 inverse = _pixelToDocument.Inverted();
    const Vec2 zero = inverse.Apply(Vec2{0, 0});
    const Vec2 moved = inverse.Apply(offset);
    const double targetX = _lifted->rect.x + (moved.x - zero.x);
    const double targetY = _lifted->rect.y + (moved.y - zero.y);
    const bool whole = targetX == std::round(targetX) && targetY == std::round(targetY);
    // Only the tiles the pixels land on now, the ones they landed on last time and, on the first update (or when
    // duplicating switches), the hole's tiles are rebuilt; each rebuild starts from the original tile, so a preview
    // never accumulates and every other tile already shows what it should.
    std::vector<std::uint64_t> landing;
    if (!AllocateTilesIn(GridRect{static_cast<int>(std::floor(targetX)), static_cast<int>(std::floor(targetY)),
                                  _lifted->rect.width + 1, _lifted->rect.height + 1},
                         landing))
    {
        _failed = true;
        return false;
    }
    std::vector<std::uint64_t> keys = landing;
    const auto add = [&](const std::vector<std::uint64_t>& more)
    {
        for (const std::uint64_t key : more)
        {
            if (std::find(keys.begin(), keys.end(), key) == keys.end())
            {
                keys.push_back(key);
            }
        }
    };
    add(_landingTiles);
    if (!_moved.has_value() || _moved->duplicate != duplicate)
    {
        std::vector<std::uint64_t> hole;
        if (!AllocateTilesIn(_lifted->rect, hole))
        {
            _failed = true;
            return false;
        }
        add(hole);
    }
    const Raster& lifted = _lifted->image;
    for (const std::uint64_t key : keys)
    {
        const auto found = _index.find(key);
        if (found == _index.end())
        {
            continue;
        }
        Tile& tile = _tiles[found->second];
        const std::uint8_t* base = tile.base.Data();
        if (base == nullptr)
        {
            _failed = true;
            return false;
        }
        const std::size_t count =
            static_cast<std::size_t>(tile.rect.width) * static_cast<std::size_t>(tile.rect.height);
        std::vector<std::uint8_t> bytes(base, base + count * 4u);
        for (std::size_t index = 0; index < count; ++index)
        {
            const int px = static_cast<int>(index % static_cast<std::size_t>(tile.rect.width));
            const int py = static_cast<int>(index / static_cast<std::size_t>(tile.rect.width));
            const int gx = tile.rect.x + px;
            const int gy = tile.rect.y + py;
            std::uint8_t* out = bytes.data() + index * 4u;
            if (!duplicate)
            {
                // The selection becomes a hole.
                const double coverage = SelectionAt(_pixelToDocument.Apply(Vec2{gx + 0.5, gy + 0.5}));
                if (coverage > 0.0)
                {
                    for (int channel = 0; channel < 4; ++channel)
                    {
                        out[channel] = static_cast<std::uint8_t>(std::lround(out[channel] * (1.0 - coverage)));
                    }
                }
            }
            // The lifted pixels land `offset` away: exact when whole pixels, bilinear otherwise.
            std::array<double, 4> sample{};
            const double sx = gx + 0.5 - targetX;
            const double sy = gy + 0.5 - targetY;
            bool inside = false;
            if (whole)
            {
                const int ix = static_cast<int>(std::floor(sx));
                const int iy = static_cast<int>(std::floor(sy));
                if (ix >= 0 && iy >= 0 && ix < lifted.Width() && iy < lifted.Height())
                {
                    const std::uint8_t* pixel =
                        lifted.Data() + static_cast<std::size_t>(iy) * static_cast<std::size_t>(lifted.Stride()) +
                        static_cast<std::size_t>(ix) * 4u;
                    for (int channel = 0; channel < 4; ++channel)
                    {
                        sample[static_cast<std::size_t>(channel)] = pixel[channel];
                    }
                    inside = true;
                }
            }
            else
            {
                inside = SampleAt(lifted, sx, sy, sample);
            }
            if (!inside || sample[3] <= 0.0)
            {
                continue;
            }
            const double hold = 1.0 - sample[3] / 255.0;
            for (int channel = 0; channel < 4; ++channel)
            {
                out[channel] = static_cast<std::uint8_t>(std::lround(
                    std::clamp(sample[static_cast<std::size_t>(channel)] + out[channel] * hold, 0.0, 255.0)));
            }
        }
        tile.image =
            Raster::Make(tile.rect.width, tile.rect.height, RasterFormat::Bgra8888, bytes.data(), bytes.size());
        tile.touched = true;
        tile.changed = true;
    }
    _landingTiles = std::move(landing);
    _moved = MoveState{offset, duplicate};
    return true;
}

// ── Pixel edits on the session (C9b) ──────────────────────────────────────

namespace
{
// A selection's loops carried through a distortion of the floating pixels (legacy DistortWarp.mapPath).
[[nodiscard]] std::vector<SelectionLoop> LoopsThroughDistortion(const std::vector<SelectionLoop>& loops,
                                                                const LayerTransform& original, int width, int height,
                                                                const LayerTransform& draft,
                                                                const std::array<Vec2, 4>& corners)
{
    if (!DistortWarp::IsUsable(corners) || width < 1 || height < 1)
    {
        return loops;
    }
    const Affine2 toPixels = PixelToDocument(original, width, height).Inverted();
    const DistortWarp::Homography map = DistortWarp::MakeHomography(corners);
    std::vector<SelectionLoop> result;
    result.reserve(loops.size());
    for (const SelectionLoop& loop : loops)
    {
        SelectionLoop carried;
        carried.reserve(loop.size());
        for (const Vec2& point : loop)
        {
            const Vec2 pixel = toPixels.Apply(point);
            double u = pixel.x / width;
            double v = pixel.y / height;
            if (draft.flipX)
            {
                u = 1.0 - u;
            }
            if (draft.flipY)
            {
                v = 1.0 - v;
            }
            carried.push_back(map.Map(Vec2{u, v}));
        }
        result.push_back(std::move(carried));
    }
    return result;
}

// Gray8 as opaque gray BGRA (a mask copied to the clipboard shows as pixels).
[[nodiscard]] Raster GrayAsPixels(const Raster& gray)
{
    const std::uint8_t* data = gray.Data();
    if (data == nullptr)
    {
        return {};
    }
    std::vector<std::uint8_t> bytes;
    try
    {
        bytes.resize(static_cast<std::size_t>(gray.Width()) * static_cast<std::size_t>(gray.Height()) * 4u);
    }
    catch (const std::bad_alloc&)
    {
        return {};
    }
    for (int y = 0; y < gray.Height(); ++y)
    {
        for (int x = 0; x < gray.Width(); ++x)
        {
            const std::uint8_t value = data[static_cast<std::size_t>(y) * static_cast<std::size_t>(gray.Stride()) +
                                            static_cast<std::size_t>(x)];
            std::uint8_t* out = bytes.data() + (static_cast<std::size_t>(y) * static_cast<std::size_t>(gray.Width()) +
                                                static_cast<std::size_t>(x)) *
                                                   4u;
            out[0] = value;
            out[1] = value;
            out[2] = value;
            out[3] = 255;
        }
    }
    return Raster::Make(gray.Width(), gray.Height(), RasterFormat::Bgra8888, bytes.data(), bytes.size());
}
} // namespace

std::wstring DocumentSession::NextLayerName() const
{
    return UniqueLayerName(L"Layer ");
}

std::shared_ptr<BrushStroke> DocumentSession::MakeRasterEdit(const ImageLayer& layer, bool mask,
                                                             const BrushSettings& settings, BrushKind kind) const
{
    if (!_document.has_value())
    {
        return nullptr;
    }
    std::shared_ptr<BrushStroke> edit;
    try
    {
        edit = std::make_shared<BrushStroke>(layer, mask, settings, _document->width, _document->height, _brushEngine,
                                             kind, spotHealingMode);
    }
    catch (const std::bad_alloc&)
    {
        return nullptr;
    }
    if (!edit->IsValid())
    {
        return nullptr;
    }
    edit->SetPixelLimit(RasterEditPixelLimit(layer, mask));
    if (_document->selection.has_value())
    {
        const DocumentSelection& selection = _document->selection.value();
        edit->SetSelectionClip(
            SelectionGeometry::Coverage(selection.loops, _document->width, _document->height, selection.antialiased));
    }
    return edit;
}

bool DocumentSession::FillSelection(FillSource source)
{
    // Pixel edits apply a pending gradient first (Decision D3; legacy refuses them while one is pending).
    ResolveGradient();
    if (!CanPaint())
    {
        return false;
    }
    const ImageLayer* layer = ActiveLayer();
    if (layer == nullptr)
    {
        return false;
    }
    const bool mask = _maskSelected;
    // A mask has its own palette: black hides and white reveals, the foreground per `maskPaintWhite`.
    std::array<double, 4> color =
        StraightColor(source == FillSource::Background ? _backgroundArgb : _foregroundArgb, 1.0);
    if (mask)
    {
        const bool white = source == FillSource::Background ? !maskPaintWhite : maskPaintWhite;
        color = {white ? 1.0 : 0.0, white ? 1.0 : 0.0, white ? 1.0 : 0.0, 1.0};
    }
    const std::shared_ptr<BrushStroke> edit = MakeRasterEdit(*layer, mask);
    if (!edit)
    {
        return false;
    }
    edit->SetEditName(mask ? L"Fill Mask" : L"Fill");
    if (!edit->Fill(color) || !edit->HasPaint())
    {
        return false;
    }
    return CommitStroke(edit);
}

bool DocumentSession::ClearSelectedPixels()
{
    ResolveGradient();
    if (!_document.has_value() || !_document->selection.has_value() || !CanPaint())
    {
        return false;
    }
    if (_maskSelected)
    {
        // Whether the fill was recorded, never a count of entries (a full history trims one as it adds one).
        return FillSelection(FillSource::Background);
    }
    const ImageLayer* layer = ActiveLayer();
    if (layer == nullptr || !layer->asset.has_value())
    {
        return false;
    }
    const std::shared_ptr<BrushStroke> edit = MakeRasterEdit(*layer, false);
    if (!edit)
    {
        return false;
    }
    edit->SetEditName(L"Clear");
    if (!edit->ClearPixels() || !edit->HasPaint())
    {
        return false;
    }
    return CommitStroke(edit);
}

void DocumentSession::DeleteLayerMask()
{
    ResolveGradient();
    if (!_activeLayerID.has_value())
    {
        return;
    }
    ImageLayer* layer = MutableLayer(_activeLayerID.value());
    if (layer == nullptr || !layer->mask.has_value())
    {
        return;
    }
    BeginEdit(L"Delete Layer Mask");
    layer->mask.reset();
    _maskSelected = false;
    ++_editRevision;
    EndEdit();
}

void DocumentSession::DeleteLayerOrMask()
{
    const ImageLayer* layer = ActiveLayer();
    if (layer == nullptr)
    {
        return;
    }
    if (_maskSelected && layer->mask.has_value())
    {
        DeleteLayerMask();
    }
    else
    {
        DeleteLayer(layer->id);
    }
}

void DocumentSession::DeleteKeyPressed()
{
    if (_document.has_value() && _document->selection.has_value())
    {
        ClearSelectedPixels();
    }
    else
    {
        DeleteLayerOrMask();
    }
}

bool DocumentSession::CanInvert() const noexcept
{
    if (!_document.has_value() || _brush || _warp || _pixelMove)
    {
        return false;
    }
    const ImageLayer* layer = ActiveLayer();
    if (layer == nullptr || (layer->isGroup && !_maskSelected) || !EffectivelyVisible(_document.value(), layer->id))
    {
        return false;
    }
    if (_document->selection.has_value() && _document->selection->IsEmpty())
    {
        return false;
    }
    return _maskSelected ? (layer->mask.has_value() && layer->mask->enabled)
                         : (layer->asset.has_value() && !layer->adjustment.has_value());
}

void DocumentSession::InvertPixels()
{
    if (!CanInvert())
    {
        return;
    }
    // Invert is available in every tool: a pending transform or gradient is applied first.
    CommitTransform();
    ResolveGradient();
    if (!CanInvert())
    {
        return;
    }
    const ImageLayer* layer = ActiveLayer();
    const bool mask = _maskSelected;
    const std::shared_ptr<BrushStroke> edit = MakeRasterEdit(*layer, mask);
    if (!edit)
    {
        return;
    }
    edit->SetEditName(mask ? L"Invert Mask" : L"Invert");
    if (!edit->Invert() || !edit->HasPaint())
    {
        return;
    }
    CommitStroke(edit);
}

bool DocumentSession::BeginPixelMove(bool duplicate)
{
    ResolveGradient();
    if (_pixelMove || !_document.has_value() || !_document->selection.has_value() || _document->selection->IsEmpty() ||
        !CanPaint() || _maskSelected)
    {
        return false;
    }
    const ImageLayer* layer = ActiveLayer();
    if (layer == nullptr || !layer->asset.has_value())
    {
        return false;
    }
    std::shared_ptr<BrushStroke> raster = MakeRasterEdit(*layer, false);
    if (!raster || !raster->LiftSelection())
    {
        return false;
    }
    raster->SetEditName(duplicate ? L"Duplicate Pixels" : L"Move Pixels");
    try
    {
        // The selection copy (the loops being lifted) can still allocate; the raster itself no longer does.
        _pixelMove = PixelMove{std::move(raster), _document->selection.value(), duplicate, Vec2{0, 0}};
    }
    catch (const std::bad_alloc&)
    {
        return false;
    }
    return true;
}

void DocumentSession::MovePixels(double dx, double dy)
{
    if (!_pixelMove || !std::isfinite(dx) || !std::isfinite(dy))
    {
        return;
    }
    const Vec2 rounded{std::round(dx), std::round(dy)};
    if (!_pixelMove->raster->MoveLifted(rounded, _pixelMove->duplicate))
    {
        CancelPixelMove();
        return;
    }
    _pixelMove->offset = rounded;
}

std::optional<DocumentSelection> DocumentSession::DisplayedSelection() const
{
    if (!_document.has_value())
    {
        return std::nullopt;
    }
    if (_pixelMove)
    {
        DocumentSelection moved = _pixelMove->origin;
        moved.loops = SelectionGeometry::Translated(moved.loops, _pixelMove->offset.x, _pixelMove->offset.y);
        return moved;
    }
    if (_transformEdit.has_value() && _transformEdit->floating.has_value() && _document->selection.has_value())
    {
        DocumentSelection moved = _document->selection.value();
        const FloatingTransform& floating = _transformEdit->floating.value();
        if (_transformEdit->corners.has_value())
        {
            moved.loops =
                LoopsThroughDistortion(moved.loops, floating.original, floating.pixelWidth, floating.pixelHeight,
                                       _transformEdit->draft, _transformEdit->corners.value());
            return moved;
        }
        if (const std::optional<Affine2> map = FloatingSelectionMap(_transformEdit.value()); map.has_value())
        {
            moved.loops = SelectionGeometry::Transformed(moved.loops, map.value());
            return moved;
        }
    }
    return _document->selection;
}

void DocumentSession::FinishPixelMove()
{
    if (!_pixelMove)
    {
        return;
    }
    const PixelMove move = std::move(_pixelMove).value();
    _pixelMove.reset();
    if (move.offset.x == 0.0 && move.offset.y == 0.0)
    {
        return;
    }
    DocumentSelection moved = move.origin;
    moved.loops = SelectionGeometry::Translated(moved.loops, move.offset.x, move.offset.y);
    CommitStroke(move.raster, moved);
}

void DocumentSession::CancelPixelMove() noexcept
{
    _pixelMove.reset();
}

void DocumentSession::NudgePixels(double dx, double dy)
{
    if (!BeginPixelMove())
    {
        return;
    }
    MovePixels(dx, dy);
    FinishPixelMove();
}

void DocumentSession::AddMask(bool revealing)
{
    ResolveGradient();
    if (!_document.has_value() || !_document->selection.has_value())
    {
        AddLayerMask(revealing);
        return;
    }
    if (_transformEdit.has_value() || !_activeLayerID.has_value())
    {
        return;
    }
    ImageLayer* layer = MutableLayer(_activeLayerID.value());
    if (layer == nullptr || layer->mask.has_value())
    {
        return;
    }
    // Mask pixels cover the layer's own pixel grid, like every other mask.
    const int width =
        layer->asset.has_value() ? layer->asset->image.Width() : static_cast<int>(std::lround(layer->transform.size.x));
    const int height = layer->asset.has_value() ? layer->asset->image.Height()
                                                : static_cast<int>(std::lround(layer->transform.size.y));
    if (width < 1 || height < 1 || static_cast<std::int64_t>(width) * static_cast<std::int64_t>(height) > 100'000'000)
    {
        return;
    }
    // Decision D7: the new mask fits the document's mask budget, or nothing is added.
    if (static_cast<std::int64_t>(width) * static_cast<std::int64_t>(height) > PixelBudgetLeft(true))
    {
        _budgetRefused = true;
        return;
    }
    const DocumentSelection& selection = _document->selection.value();
    const Affine2 toPixels = PixelToDocument(layer->transform, width, height).Inverted();
    const Raster coverage = SelectionGeometry::Coverage(SelectionGeometry::Transformed(selection.loops, toPixels),
                                                        width, height, selection.antialiased);
    const std::uint8_t* data = coverage.Data();
    if (data == nullptr)
    {
        return;
    }
    std::vector<std::uint8_t> bytes;
    try
    {
        bytes.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
    }
    catch (const std::bad_alloc&)
    {
        return;
    }
    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            const std::uint8_t inside = data[static_cast<std::size_t>(y) * static_cast<std::size_t>(coverage.Stride()) +
                                             static_cast<std::size_t>(x)];
            bytes[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)] =
                revealing ? static_cast<std::uint8_t>(255 - inside) : inside;
        }
    }
    const Raster mask = Raster::Make(width, height, RasterFormat::Gray8, bytes.data(), bytes.size());
    if (!mask.HasStorage())
    {
        return;
    }
    BeginEdit(L"Add Mask from Selection");
    layer->mask = LayerMask{RasterAsset{mask, Raster::MakeThumbnail(mask), layer->name}, true, std::nullopt, true};
    _document->selection.reset();
    _maskSelected = true;
    ++_editRevision;
    EndEdit();
}

bool DocumentSession::CanTransformSelection() const noexcept
{
    if (_transformEdit.has_value() || !CanPaint() || _maskSelected || !_document.has_value() ||
        !_document->selection.has_value() || _document->selection->IsEmpty())
    {
        return false;
    }
    const ImageLayer* layer = ActiveLayer();
    return layer != nullptr && layer->asset.has_value();
}

void DocumentSession::TransformCommand()
{
    if (CanTransformSelection())
    {
        BeginSelectionTransform();
    }
    else
    {
        BeginTransform(true);
    }
}

void DocumentSession::BeginSelectionTransform()
{
    // Before the snapshot Escape restores, so a pending gradient is never part of what a cancel undoes.
    ResolveGradient();
    if (!CanTransformSelection())
    {
        return;
    }
    const ImageLayer source = *ActiveLayer();
    const std::optional<PixelClipboard> lifted = RenderSelectedPixels(source, false);
    if (!lifted.has_value())
    {
        return;
    }
    std::shared_ptr<CanvasDocument> before;
    try
    {
        before = std::make_shared<CanvasDocument>(_document.value());
    }
    catch (const std::bad_alloc&)
    {
        return;
    }
    const std::optional<Guid> beforeActive = _activeLayerID;
    // Outer entry: closed by CommitTransform (merge) or CancelTransform (restore).
    BeginEdit(L"Transform Selection");
    if (!ClearSelectedPixels())
    {
        _document = *before;
        SetActiveLayerID(beforeActive);
        EndEdit();
        return;
    }
    const std::optional<std::size_t> index = IndexOf(_document->layers, source.id);
    if (!index.has_value())
    {
        _document = *before;
        SetActiveLayerID(beforeActive);
        EndEdit();
        return;
    }
    ImageLayer floating = ImageLayer::Blank(L"Floating Selection", Vec2{static_cast<double>(lifted->image.Width()),
                                                                        static_cast<double>(lifted->image.Height())});
    floating.asset = RasterAsset{lifted->image, Raster::MakeThumbnail(lifted->image), L"Floating Selection"};
    floating.transform.origin = lifted->origin;
    floating.parentID = source.parentID;
    floating.opacity = source.opacity;
    floating.blendMode = source.blendMode;
    _document->layers.insert(_document->layers.begin() + static_cast<std::ptrdiff_t>(index.value()) + 1, floating);
    SetActiveLayerID(floating.id);
    _maskSelected = false;
    TransformEdit edit;
    edit.layerID = floating.id;
    edit.draft = floating.transform;
    edit.persistent = true;
    edit.floating = FloatingTransform{
        source.id, before, beforeActive, floating.transform, lifted->image.Width(), lifted->image.Height()};
    _transformEdit = std::move(edit);
    ++_editRevision;
}

std::optional<Affine2> DocumentSession::FloatingSelectionMap(const TransformEdit& edit) const noexcept
{
    if (!edit.floating.has_value() || edit.floating->pixelWidth < 1 || edit.floating->pixelHeight < 1)
    {
        return std::nullopt;
    }
    const FloatingTransform& floating = edit.floating.value();
    return PixelToDocument(floating.original, floating.pixelWidth, floating.pixelHeight)
        .Inverted()
        .Concat(PixelToDocument(edit.draft, floating.pixelWidth, floating.pixelHeight));
}

void DocumentSession::MergeFloating(const TransformEdit& edit, const FloatingTransform& floating)
{
    bool merged = false;
    do
    {
        if (!edit.draft.IsValid() || !_document.has_value() || !floating.before)
        {
            break;
        }
        std::vector<ImageLayer>& layers = _document->layers;
        const std::optional<std::size_t> floatingIndex = IndexOf(layers, edit.layerID);
        const std::optional<std::size_t> sourceIndex = IndexOf(layers, floating.sourceID);
        if (!floatingIndex.has_value() || !sourceIndex.has_value() ||
            !layers[floatingIndex.value()].asset.has_value() || !layers[sourceIndex.value()].asset.has_value())
        {
            break;
        }
        Raster pixels = layers[floatingIndex.value()].asset->image;
        LayerTransform placed = edit.draft;
        if (edit.corners.has_value())
        {
            // A distorted selection is warped into its new shape first, then merged like any other.
            const std::optional<DistortWarp::WarpedTrimmed> warped =
                DistortWarp::WarpTrimmed(pixels, edit.draft, edit.corners.value());
            if (!warped.has_value())
            {
                break;
            }
            pixels = warped->image;
            placed = warped->transform;
        }
        const ImageLayer& source = layers[sourceIndex.value()];
        const int sourceWidth = source.asset->image.Width();
        const int sourceHeight = source.asset->image.Height();
        const Affine2 toDocument = PixelToDocument(source.transform, sourceWidth, sourceHeight);
        const Affine2 toPixels = toDocument.Inverted();
        const Affine2 floatingToDocument = PixelToDocument(placed, pixels.Width(), pixels.Height());
        // The floating pixels on the source's own grid, growing the layer where they now extend past it.
        const GridRect original{0, 0, sourceWidth, sourceHeight};
        const GridRect extent =
            original.Union(MappedBox(floatingToDocument.Concat(toPixels), 0.0, 0.0, static_cast<double>(pixels.Width()),
                                     static_cast<double>(pixels.Height())));
        if (extent.width > 30'000 || extent.height > 30'000 ||
            static_cast<std::int64_t>(extent.width) * static_cast<std::int64_t>(extent.height) > 100'000'000)
        {
            break;
        }
        // Decision D7: the merged layer (and a mask on its grid growing with it) fits the document's budget, the
        // floating layer and the source's old pixels no longer counted; otherwise the document is restored.
        const std::int64_t mergedPixels =
            static_cast<std::int64_t>(extent.width) * static_cast<std::int64_t>(extent.height);
        const bool maskGrows = source.mask.has_value() && !source.mask->placement.has_value() && extent != original &&
                               source.mask->asset.image.PixelCount() > 1;
        if (mergedPixels > PixelBudgetLeft(false, {floating.sourceID, edit.layerID}) ||
            (maskGrows && mergedPixels > PixelBudgetLeft(true, {floating.sourceID, edit.layerID})))
        {
            _budgetRefused = true;
            break;
        }
        std::vector<std::uint8_t> bytes;
        try
        {
            bytes.assign(static_cast<std::size_t>(extent.width) * static_cast<std::size_t>(extent.height) * 4u, 0);
        }
        catch (const std::bad_alloc&)
        {
            break;
        }
        source.asset->image.ReadRegion(extent.x, extent.y, extent.width, extent.height, bytes.data(), extent.width * 4,
                                       0);
        const Affine2 documentToFloating = floatingToDocument.Inverted();
        for (int y = 0; y < extent.height; ++y)
        {
            for (int x = 0; x < extent.width; ++x)
            {
                const Vec2 document = toDocument.Apply(Vec2{extent.x + x + 0.5, extent.y + y + 0.5});
                const Vec2 floatingPixel = documentToFloating.Apply(document);
                std::array<double, 4> sample{};
                if (!SampleAt(pixels, floatingPixel.x, floatingPixel.y, sample) || sample[3] <= 0.0)
                {
                    continue;
                }
                std::uint8_t* out =
                    bytes.data() + (static_cast<std::size_t>(y) * static_cast<std::size_t>(extent.width) +
                                    static_cast<std::size_t>(x)) *
                                       4u;
                const double hold = 1.0 - sample[3] / 255.0;
                for (int channel = 0; channel < 4; ++channel)
                {
                    out[channel] = static_cast<std::uint8_t>(std::lround(
                        std::clamp(sample[static_cast<std::size_t>(channel)] + out[channel] * hold, 0.0, 255.0)));
                }
            }
        }
        const Raster image =
            Raster::Make(extent.width, extent.height, RasterFormat::Bgra8888, bytes.data(), bytes.size());
        if (!image.HasStorage())
        {
            break;
        }
        const LayerTransform mergedTransform =
            PlacementOfRect(source.transform, sourceWidth, sourceHeight, toDocument, extent);
        if (!mergedTransform.IsValid())
        {
            break;
        }
        std::optional<LayerMask> mask = source.mask;
        if (mask.has_value() && !mask->placement.has_value() && extent != original &&
            mask->asset.image.PixelCount() > 1)
        {
            // A mask on the layer's grid grows with it, revealing only the new area; a uniform mask stays as is.
            const Raster grown = CarriedMask(mask->asset.image, sourceWidth, sourceHeight, -extent.x, -extent.y,
                                             extent.width, extent.height, 255);
            if (grown.HasStorage())
            {
                mask->asset.image = grown;
                mask->asset.thumbnail = Raster::MakeThumbnail(grown);
            }
        }
        std::optional<DocumentSelection> moved;
        if (_document->selection.has_value())
        {
            DocumentSelection selection = _document->selection.value();
            if (edit.corners.has_value())
            {
                selection.loops = LoopsThroughDistortion(selection.loops, floating.original, floating.pixelWidth,
                                                         floating.pixelHeight, edit.draft, edit.corners.value());
            }
            else if (const std::optional<Affine2> map = FloatingSelectionMap(edit); map.has_value())
            {
                selection.loops = SelectionGeometry::Transformed(selection.loops, map.value());
            }
            moved = std::move(selection);
        }
        const std::wstring name = source.name;
        layers.erase(layers.begin() + static_cast<std::ptrdiff_t>(floatingIndex.value()));
        const std::optional<std::size_t> target = IndexOf(layers, floating.sourceID);
        if (!target.has_value())
        {
            break;
        }
        ImageLayer& installed = layers[target.value()];
        installed.asset = RasterAsset{image, Raster::MakeThumbnail(image), name};
        installed.transform = mergedTransform;
        installed.mask = mask;
        _document->selection = moved;
        SetActiveLayerID(floating.sourceID);
        _maskSelected = false;
        merged = true;
    } while (false);
    if (!merged && floating.before)
    {
        _document = *floating.before;
        SetActiveLayerID(floating.beforeActive);
    }
    ++_editRevision;
    EndEdit();
}

void DocumentSession::CancelFloating(const FloatingTransform& floating)
{
    if (floating.before)
    {
        _document = *floating.before;
    }
    SetActiveLayerID(floating.beforeActive);
    ++_editRevision;
    EndEdit();
}

std::optional<GridRect> DocumentSession::SelectionCopyRegion() const noexcept
{
    if (!_document.has_value())
    {
        return std::nullopt;
    }
    double minX = 0;
    double minY = 0;
    double maxX = _document->width;
    double maxY = _document->height;
    if (_document->selection.has_value())
    {
        const std::optional<SelectionBounds> bounds = _document->selection->Bounds();
        if (!bounds.has_value())
        {
            return std::nullopt;
        }
        minX = bounds->minX;
        minY = bounds->minY;
        maxX = bounds->maxX;
        maxY = bounds->maxY;
    }
    // Path booleans leave tiny float noise (59.9999999), so round with a tolerance rather than add a pixel.
    const double tolerance = 0.001;
    const double left = std::max(0.0, std::floor(minX + tolerance));
    const double top = std::max(0.0, std::floor(minY + tolerance));
    const double right = std::min(static_cast<double>(_document->width), std::ceil(maxX - tolerance));
    const double bottom = std::min(static_cast<double>(_document->height), std::ceil(maxY - tolerance));
    if (!(right - left >= 1.0) || !(bottom - top >= 1.0))
    {
        return std::nullopt;
    }
    return GridRect{static_cast<int>(left), static_cast<int>(top), static_cast<int>(right - left),
                    static_cast<int>(bottom - top)};
}

bool DocumentSession::CanCopyPixels() const noexcept
{
    const ImageLayer* layer = ActiveLayer();
    if (!_document.has_value() || layer == nullptr || (layer->isGroup && !_maskSelected) ||
        (_document->selection.has_value() && _document->selection->IsEmpty()))
    {
        return false;
    }
    return _maskSelected ? layer->mask.has_value() : layer->asset.has_value();
}

std::optional<PixelClipboard> DocumentSession::RenderSelectedPixels(const ImageLayer& layer, bool mask) const
{
    if (!_document.has_value())
    {
        return std::nullopt;
    }
    const std::optional<GridRect> region = SelectionCopyRegion();
    if (!region.has_value())
    {
        return std::nullopt;
    }
    const int width = _document->width;
    const int height = _document->height;
    Raster shown;
    if (mask)
    {
        if (!layer.mask.has_value())
        {
            return std::nullopt;
        }
        // A mask as opaque gray: beyond its pixels its edge tone when placed apart, else black.
        const std::uint8_t background =
            layer.mask->placement.has_value()
                ? static_cast<std::uint8_t>(std::lround(MaskEdgeBackground(layer.mask->asset.thumbnail) * 255.0))
                : 0;
        shown = GrayAsPixels(
            ImageFilters::RenderMaskPixels(layer.mask->asset.image, MaskTransform(layer), width, height, background));
    }
    else
    {
        if (!layer.asset.has_value())
        {
            return std::nullopt;
        }
        shown = RenderLayerPixels(layer.asset->image, DisplayedTransform(layer), width, height);
    }
    if (!shown.HasStorage())
    {
        return std::nullopt;
    }
    return RenderMergedPixels(shown);
}

bool DocumentSession::CanCopyMerged() const noexcept
{
    if (!_document.has_value() || (_document->selection.has_value() && _document->selection->IsEmpty()))
    {
        return false;
    }
    for (const ImageLayer& layer : _document->layers)
    {
        if (layer.asset.has_value())
        {
            return true;
        }
    }
    return false;
}

std::optional<PixelClipboard> DocumentSession::RenderMergedPixels(const Raster& composite) const
{
    if (!_document.has_value() || !composite.HasStorage() || composite.Format() != RasterFormat::Bgra8888 ||
        composite.Width() != _document->width || composite.Height() != _document->height)
    {
        return std::nullopt;
    }
    const std::optional<GridRect> region = SelectionCopyRegion();
    if (!region.has_value())
    {
        return std::nullopt;
    }
    std::optional<Raster> coverage;
    if (_document->selection.has_value())
    {
        const DocumentSelection& selection = _document->selection.value();
        coverage =
            SelectionGeometry::Coverage(selection.loops, _document->width, _document->height, selection.antialiased);
        if (!coverage->HasStorage() || coverage->Data() == nullptr)
        {
            return std::nullopt;
        }
    }
    std::vector<std::uint8_t> bytes;
    try
    {
        bytes.resize(static_cast<std::size_t>(region->width) * static_cast<std::size_t>(region->height) * 4u);
    }
    catch (const std::bad_alloc&)
    {
        return std::nullopt;
    }
    composite.ReadRegion(region->x, region->y, region->width, region->height, bytes.data(), region->width * 4, 0);
    if (coverage.has_value())
    {
        const std::uint8_t* mask = coverage->Data();
        for (int y = 0; y < region->height; ++y)
        {
            for (int x = 0; x < region->width; ++x)
            {
                const double c =
                    mask[static_cast<std::size_t>(region->y + y) * static_cast<std::size_t>(coverage->Stride()) +
                         static_cast<std::size_t>(region->x + x)] /
                    255.0;
                std::uint8_t* pixel =
                    bytes.data() + (static_cast<std::size_t>(y) * static_cast<std::size_t>(region->width) +
                                    static_cast<std::size_t>(x)) *
                                       4u;
                for (int channel = 0; channel < 4; ++channel)
                {
                    pixel[channel] = static_cast<std::uint8_t>(std::lround(pixel[channel] * c));
                }
            }
        }
    }
    const Raster image =
        Raster::Make(region->width, region->height, RasterFormat::Bgra8888, bytes.data(), bytes.size());
    if (!image.HasStorage())
    {
        return std::nullopt;
    }
    return PixelClipboard{image, Vec2{static_cast<double>(region->x), static_cast<double>(region->y)}};
}

bool DocumentSession::CopySelection()
{
    // The copy takes the pixels the canvas shows, a pending gradient applied.
    ResolveGradient();
    if (!CanCopyPixels())
    {
        return false;
    }
    std::optional<PixelClipboard> copied = RenderSelectedPixels(*ActiveLayer(), _maskSelected);
    if (!copied.has_value())
    {
        return false;
    }
    _clipboard = std::move(copied);
    return true;
}

bool DocumentSession::CopyMergedSelection(const Raster& composite)
{
    if (!CanCopyMerged())
    {
        return false;
    }
    std::optional<PixelClipboard> copied = RenderMergedPixels(composite);
    if (!copied.has_value())
    {
        return false;
    }
    _clipboard = std::move(copied);
    return true;
}

void DocumentSession::CutSelection()
{
    ResolveGradient();
    if (!_document.has_value() || !_document->selection.has_value() || !CanCopyPixels() || !CanPaint())
    {
        return;
    }
    const std::optional<PixelClipboard> previous = _clipboard;
    if (!CopySelection())
    {
        return;
    }
    if (!ClearSelectedPixels())
    {
        _clipboard = previous;
    }
}

void DocumentSession::Paste()
{
    if (!CanPaste())
    {
        return;
    }
    const PixelClipboard clip = _clipboard.value();
    AddPixelLayer(clip.image, clip.origin, NextLayerName(), L"Paste", true, std::nullopt);
}

void DocumentSession::PasteImage(const Raster& image)
{
    if (!_document.has_value() || !image.HasStorage())
    {
        return;
    }
    const Vec2 origin{std::floor((_document->width - image.Width()) / 2.0),
                      std::floor((_document->height - image.Height()) / 2.0)};
    AddPixelLayer(image, origin, NextLayerName(), L"Paste", true, std::nullopt);
}

void DocumentSession::LayerViaCopy()
{
    ResolveGradient();
    const ImageLayer* layer = ActiveLayer();
    if (!_document.has_value() || layer == nullptr || layer->isGroup ||
        (_document->selection.has_value() && _document->selection->IsEmpty()))
    {
        return;
    }
    if (!_document->selection.has_value())
    {
        DuplicateActiveLayer();
        return;
    }
    const std::optional<PixelClipboard> copied = RenderSelectedPixels(*layer, _maskSelected);
    if (!copied.has_value())
    {
        return;
    }
    AddPixelLayer(copied->image, copied->origin, NextLayerName(), L"Layer via Copy", true, std::nullopt);
}
