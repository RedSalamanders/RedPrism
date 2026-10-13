#include "BrushStroke.h"
#include "DocumentDraft.h"
#include "WarpPreviewWork.h"

#include "Heal.h"
#include "ImageFilters.h"
#include "PreparedRasterPreview.h"
#include "RasterEditWork.h"
#include "RasterWork.h"
#include "SharedPublicationMemory.h"
#include "TransformEdit.h"
#include "WarpStroke.h"

#include "Selection.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstring>
#include <exception>
#include <limits>
#include <new>
#include <utility>

namespace
{
constexpr double kDensityCap = 20.0;
constexpr double kFalloffK = 2.5;
constexpr double kCurveTolerance = 0.2;
constexpr int kCurveDepth = 10;

[[nodiscard]] bool AddRetirementBytes(std::uint64_t& total, std::uint64_t bytes) noexcept
{
    if (bytes > std::numeric_limits<std::uint64_t>::max() - total)
    {
        return false;
    }
    total += bytes;
    return true;
}

template <typename T> [[nodiscard]] bool AddRetirementCapacity(std::uint64_t& total, std::size_t capacity) noexcept
{
    if (capacity > std::numeric_limits<std::uint64_t>::max() / sizeof(T))
    {
        return false;
    }
    return AddRetirementBytes(total, static_cast<std::uint64_t>(capacity) * sizeof(T));
}

template <typename Container>
[[nodiscard]] bool AddRetirementHashStorage(std::uint64_t& total, const Container& container) noexcept
{
    using Value = typename Container::value_type;
    constexpr std::uint64_t kNodeBytes = sizeof(Value) + 4 * sizeof(void*) + 64;
    // The pinned MSVC hash directory stores two iterator slots per bucket.
    constexpr std::uint64_t kBucketBytes = 2 * sizeof(void*);
    const std::size_t buckets = container.bucket_count();
    const std::size_t nodes = container.size();
    if (buckets > std::numeric_limits<std::uint64_t>::max() / kBucketBytes ||
        nodes > std::numeric_limits<std::uint64_t>::max() / kNodeBytes)
    {
        return false;
    }
    return AddRetirementBytes(total, static_cast<std::uint64_t>(buckets) * kBucketBytes) &&
           AddRetirementBytes(total, static_cast<std::uint64_t>(nodes) * kNodeBytes);
}

[[nodiscard]] bool AddRetirementRaster(std::uint64_t& total, const Raster& raster) noexcept
{
    if (!raster.HasStorage())
    {
        return true;
    }
    const std::uint64_t bytes = raster.RetirementMemoryBytes();
    return bytes != 0 && AddRetirementBytes(total, bytes);
}

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
    // Clamped endpoints fit int, but their difference can still exceed INT_MAX. Keep the
    // subtraction wide and reject unsupported mapped geometry before any cast or grid iteration.
    const auto width = static_cast<std::int64_t>(x1) - x0;
    const auto height = static_cast<std::int64_t>(y1) - y0;
    if (width < 0 || height < 0 || width > (std::numeric_limits<int>::max)() ||
        height > (std::numeric_limits<int>::max)())
        return {};
    return GridRect{x0, y0, static_cast<int>(width), static_cast<int>(height)};
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

template <bool Cancellable>
bool RenderTileImpl(const BrushKernelTile& tile, std::span<const BrushSegment> settled,
                    std::span<const BrushSegment> tail, const BrushKernelParams& params,
                    const CancelToken& cancellation) noexcept
{
    if (tile.preview == nullptr || tile.permanent == nullptr || tile.width <= 0 || tile.height <= 0)
    {
        return false;
    }
    const bool hard = params.hardness >= 1.0;
    for (int py = 0; py < tile.height; ++py)
    {
        if constexpr (Cancellable)
            if (cancellation.IsCancelled())
                return false;
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
    if constexpr (Cancellable)
        return !cancellation.IsCancelled();
    else
        return true;
}

void RenderTile(const BrushKernelTile& tile, std::span<const BrushSegment> settled, std::span<const BrushSegment> tail,
                const BrushKernelParams& params) noexcept
{
    (void)RenderTileImpl<false>(tile, settled, tail, params, {});
}

bool RenderTileForWork(const BrushKernelTile& tile, std::span<const BrushSegment> settled,
                       std::span<const BrushSegment> tail, const BrushKernelParams& params,
                       const CancelToken& cancellation) noexcept
{
    return RenderTileImpl<true>(tile, settled, tail, params, cancellation);
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

std::optional<BrushStroke::GridPlan> BrushStroke::PlanGrid(const ImageLayer& layer, bool mask,
                                                           const BrushSettings& settings, int canvasWidth,
                                                           int canvasHeight) noexcept
{
    GridPlan plan;
    // A mask on its own placement is painted in its own pixel grid; otherwise the grid is the layer's.
    const bool placedMask = mask && layer.mask.has_value() && layer.mask->placement.has_value();
    const LayerTransform base = placedMask ? layer.mask->placement.value() : layer.transform;
    if (!settings.IsValid() || !base.IsValid())
        return std::nullopt;
    plan.sourceTransform = base;
    const Raster* source =
        mask ? (layer.mask ? &layer.mask->asset.image : nullptr) : (layer.asset ? &layer.asset->image : nullptr);
    // The grid: a placed mask's own pixels; otherwise the layer's pixels (a mask on the layer's grid is stretched
    // over them, as a uniform 1 × 1 mask is), or the layer's size when it has no pixels yet.
    int originalWidth = 0;
    int originalHeight = 0;
    if (placedMask)
    {
        originalWidth = source->Width();
        originalHeight = source->Height();
    }
    else if (layer.asset.has_value() && layer.asset->image.HasStorage())
    {
        originalWidth = layer.asset->image.Width();
        originalHeight = layer.asset->image.Height();
    }
    else
    {
        // IsValid requires finite positive dimensions, but does not constrain their rounded integer range.
        if (layer.transform.size.x < 0.5 || layer.transform.size.y < 0.5 ||
            layer.transform.size.x >= static_cast<double>(kMaxCanvasSide) + 0.5 ||
            layer.transform.size.y >= static_cast<double>(kMaxCanvasSide) + 0.5)
            return std::nullopt;
        originalWidth = static_cast<int>(std::lround(layer.transform.size.x));
        originalHeight = static_cast<int>(std::lround(layer.transform.size.y));
    }
    if (originalWidth < 1 || originalHeight < 1 || originalWidth > kMaxCanvasSide || originalHeight > kMaxCanvasSide ||
        !settings.IsValid() || !base.IsValid())
    {
        return std::nullopt;
    }
    const Affine2 originalMapping = ::PixelToDocument(base, originalWidth, originalHeight);
    const GridRect originalBounds{0, 0, originalWidth, originalHeight};
    GridRect extent = originalBounds;
    if (!mask)
    {
        const GridRect canvas =
            MappedBox(originalMapping.Inverted(), 0.0, 0.0, static_cast<double>((std::max)(1, canvasWidth)),
                      static_cast<double>((std::max)(1, canvasHeight)));
        if (canvas.Empty())
            return std::nullopt; // Unsupported inverse geometry cannot silently produce a valid undersized paint grid.
        const auto left = (std::min)(std::int64_t{0}, static_cast<std::int64_t>(canvas.x));
        const auto top = (std::min)(std::int64_t{0}, static_cast<std::int64_t>(canvas.y));
        const auto right =
            (std::max)(static_cast<std::int64_t>(originalWidth), static_cast<std::int64_t>(canvas.x) + canvas.width);
        const auto bottom =
            (std::max)(static_cast<std::int64_t>(originalHeight), static_cast<std::int64_t>(canvas.y) + canvas.height);
        if (right <= left || bottom <= top || right - left > kMaxGridSide || bottom - top > kMaxGridSide)
            return std::nullopt;
        // The resulting interval includes zero and has side <=1e9, so endpoints and widths now fit int.
        extent = {static_cast<int>(left), static_cast<int>(top), static_cast<int>(right - left),
                  static_cast<int>(bottom - top)};
    }
    if (extent.Empty() || extent.width > kMaxGridSide || extent.height > kMaxGridSide)
    {
        return std::nullopt;
    }
    plan.width = extent.width;
    plan.height = extent.height;
    plan.sourceRect = GridRect{-extent.x, -extent.y, originalWidth, originalHeight};
    plan.pixelToDocument = Affine2::Translation(extent.x, extent.y).Concat(originalMapping);
    plan.paintTransform = PlacementOfRect(base, originalWidth, originalHeight, originalMapping, extent);
    return plan;
}

BrushStroke::BrushStroke(const ImageLayer& layer, bool mask, const BrushSettings& settings, int canvasWidth,
                         int canvasHeight, BrushCoverageEngine* engine, BrushKind kind, SpotHealingMode healingMode)
    : _layerID(layer.id), _mask(mask), _settings(settings), _canvasWidth(std::max(1, canvasWidth)),
      _canvasHeight(std::max(1, canvasHeight)), _engine(engine != nullptr ? engine : &CpuBrushCoverage::Shared()),
      _kind(kind), _healingMode(healingMode)
{
    _coverageOwner = std::this_thread::get_id();
    _usesCpuPermanent = _engine->UsesCpuPermanent();
    if (!mask && layer.mask && !layer.mask->placement)
        _carriedMask = layer.mask->asset.image;
    const auto plan = PlanGrid(layer, mask, settings, canvasWidth, canvasHeight);
    if (!plan)
        return;
    _source = mask ? (layer.mask ? layer.mask->asset.image : Raster{}) : (layer.asset ? layer.asset->image : Raster{});
    _sourceTransform = plan->sourceTransform;
    _width = plan->width;
    _height = plan->height;
    _sourceRect = plan->sourceRect;
    _pixelToDocument = plan->pixelToDocument;
    _paintTransform = plan->paintTransform;
    _valid = true;
    _engine->BeginStroke();
}

BrushStroke::~BrushStroke()
{
    ReleaseCoverageEngine();
}

void BrushStroke::ReleaseCoverageEngine() noexcept
{
    assert(!_engine || _engine == &CpuBrushCoverage::Shared() || _coverageOwner == std::this_thread::get_id());
    auto* engine = std::exchange(_engine, nullptr);
    if (_valid && engine != nullptr)
        engine->EndStroke();
}

BrushCoverageEngine* BrushStroke::DetachCoverageForRetirement() noexcept
{
    assert(!_engine || _engine == &CpuBrushCoverage::Shared() || _coverageOwner == std::this_thread::get_id());
    _detachedFinish = true;
    _workerRasterEdit = true;
    return std::exchange(_engine, nullptr);
}

BrushCoverageEngine* BrushStroke::DetachCoverageForFinish() noexcept
{
    if (!_valid || _failed || _workerRasterEdit || _detachedFinish || _detachedSample || !_engine)
        return nullptr;
    assert(_engine == &CpuBrushCoverage::Shared() || _coverageOwner == std::this_thread::get_id());
    auto* engine = std::exchange(_engine, nullptr);
    _detachedFinish = true;
    _workerRasterEdit = true;
    return engine;
}

std::optional<BrushStroke::SampleMemoryPlan> BrushStroke::PlanSampleMemory(Vec2 point) const noexcept
{
    if (!_valid || _failed || _workerRasterEdit || _detachedFinish || _detachedSample || !_engine ||
        !_tileOwnershipValid || _sampleSequence == UINT64_MAX || !std::isfinite(point.x) || !std::isfinite(point.y) ||
        std::abs(point.x) > 1e7 || std::abs(point.y) > 1e7)
        return std::nullopt;
    SampleMemoryPlan plan;
    plan.point = point;
    plan.sequence = _sampleSequence + 1;
    // Cubic convex hull of the unchanged centripetal segment, plus the straight provisional tail.
    // Only the last three old samples are read; no curve, keys or pixels are built on the owner.
    std::array<Vec2, 6> controls{point, point, point, point, point, point};
    if (!_samples.empty())
        controls[4] = _samples.back();
    if (_samples.size() >= 2)
    {
        const auto n = _samples.size();
        const Vec2 before = _samples[n >= 3 ? n - 3 : 0];
        const Vec2 start = _samples[n - 2];
        const Vec2 end = _samples[n - 1];
        const double a = Knot(0, before, start);
        const double b = Knot(0, start, end);
        const double c = Knot(0, end, point);
        controls[0] = start;
        controls[1] = {
            start.x + b * ((start.x - before.x) / a - (end.x - before.x) / (a + b) + (end.x - start.x) / b) / 3,
            start.y + b * ((start.y - before.y) / a - (end.y - before.y) / (a + b) + (end.y - start.y) / b) / 3};
        controls[2] = {end.x - b * ((end.x - start.x) / b - (point.x - start.x) / (b + c) + (point.x - end.x) / c) / 3,
                       end.y - b * ((end.y - start.y) / b - (point.y - start.y) / (b + c) + (point.y - end.y) / c) / 3};
        controls[3] = end;
    }
    const auto hull = PointBounds(controls);
    if (!std::isfinite(hull.minX) || !std::isfinite(hull.maxX) || !std::isfinite(hull.minY) ||
        !std::isfinite(hull.maxY))
        return std::nullopt;
    const double reach = _settings.diameter / 2 + 3;
    const double left = (std::max)(0.0, hull.minX - reach);
    const double top = (std::max)(0.0, hull.minY - reach);
    const double right = (std::min)(static_cast<double>(_canvasWidth), hull.maxX + reach);
    const double bottom = (std::min)(static_cast<double>(_canvasHeight), hull.maxY + reach);
    GridRect nextGrid;
    if (right > left && bottom > top)
    {
        const auto mapped = MappedBox(_pixelToDocument.Inverted(), left, top, right, bottom);
        if (mapped.Empty())
            return std::nullopt;
        const auto affected = mapped.Intersect({0, 0, _width, _height});
        if (!affected.Empty())
        {
            const int x = affected.x / kTileSize * kTileSize;
            const int y = affected.y / kTileSize * kTileSize;
            const int endX = (std::min)(_width, ((affected.x + affected.width - 1) / kTileSize + 1) * kTileSize);
            const int endY = (std::min)(_height, ((affected.y + affected.height - 1) / kTileSize + 1) * kTileSize);
            nextGrid = {x, y, endX - x, endY - y};
        }
    }
    const auto projected = _allocated.value_or(_source.HasStorage() ? _sourceRect : GridRect{}).Union(nextGrid);
    if (projected.width > kMaxCanvasSide || projected.height > kMaxCanvasSide ||
        static_cast<std::int64_t>(projected.width) * projected.height > _pixelLimit)
        return std::nullopt;
    const auto countTiles = [](const GridRect& grid) noexcept -> std::uint64_t
    {
        return grid.Empty() ? 0
                            : static_cast<std::uint64_t>((grid.width + kTileSize - 1) / kTileSize) *
                                  static_cast<std::uint64_t>((grid.height + kTileSize - 1) / kTileSize);
    };
    // A conservative rectangle excludes untouched source pixels and does not grow for revisited tiles.
    plan.maximumTiles = countTiles(_tileBounds.value_or(GridRect{}).Union(nextGrid));
    plan.maximumChangedTiles = countTiles(nextGrid);
    if (!AddRetirementBytes(plan.maximumChangedTiles, _tailKeys.size()))
        return std::nullopt;
    const auto original = RetirementMemoryBytes();
    if (!original)
        return std::nullopt;
    const auto base = _sampleMemoryBaseCpu ? _sampleMemoryBaseCpu : (std::max)(original, _openingMemory.Bytes());
    const auto add = [](std::uint64_t& total, std::uint64_t count, std::uint64_t bytes) noexcept
    { return (!bytes || count <= UINT64_MAX / bytes) && AddRetirementBytes(total, count * bytes); };
    constexpr std::uint64_t pixels = kTileSize * kTileSize;
    const std::uint64_t channels = _mask ? 1 : 4;
    const auto pixelBytes = PixelBuffer::RequiredBytes(static_cast<std::size_t>(pixels * channels));
    if (!pixelBytes)
        return std::nullopt;
    plan.persistentCpuBytes = base;
    if (!AddRetirementBytes(plan.persistentCpuBytes, 4096) ||
        !add(plan.persistentCpuBytes, plan.maximumTiles,
             2 * pixelBytes + pixels * (1 + (_usesCpuPermanent ? 4 : 0)) + 2 * sizeof(Tile) + 4608))
        return std::nullopt;
    plan.transientCpuBytes = 64 * 1024;
    if (!add(plan.transientCpuBytes, 2 * (std::uint64_t{1} << kCurveDepth), sizeof(BrushSegment)) ||
        !add(plan.transientCpuBytes, plan.maximumChangedTiles,
             1024 + 2 * sizeof(BrushKernelTile) + 8 * sizeof(std::uint64_t) + 2 * pixelBytes))
        return std::nullopt;
    if (!_usesCpuPermanent &&
        (!add(plan.additionalGpuBytes, countTiles(nextGrid), pixels * 8 + 4 * 4096) ||
         !AddRetirementBytes(plan.additionalGpuBytes,
                             2 * (std::uint64_t{1} << kCurveDepth) * 4 * sizeof(float) + 4 * 4096)))
        return std::nullopt;
    return plan;
}

BrushCoverageEngine* BrushStroke::DetachCoverageForSample(const SampleMemoryPlan& plan) noexcept
{
    const auto current = PlanSampleMemory(plan.point);
    if (!current || current->sequence != plan.sequence || current->persistentCpuBytes != plan.persistentCpuBytes ||
        _coverageOwner != std::this_thread::get_id() ||
        (_openingMemory && _openingMemory.Bytes() < plan.persistentCpuBytes))
        return nullptr;
    if (!_sampleMemoryBaseCpu)
        _sampleMemoryBaseCpu = RetirementMemoryBytes();
    _samplePoint = plan.point;
    _sampleSequence = plan.sequence;
    _sampleCoveragePrepared = false;
    _sampleCoverageComplete = false;
    _detachedSample = true;
    _workerRasterEdit = true;
    _sampleOwnerEngine = _engine;
    return std::exchange(_engine, nullptr);
}

std::optional<BrushStroke::SampleCoverage> BrushStroke::PrepareSampleCoverage(CancelToken cancellation)
{
    if (!_detachedSample || _engine || _failed || _sampleCoveragePrepared)
        return std::nullopt;
    _workCancellation = std::move(cancellation);
    _sampleCoveragePrepared = true;
    struct PreparationGuard final
    {
        bool& failed;
        bool ready = false;
        ~PreparationGuard()
        {
            if (!ready)
                failed = true;
        }
    } guard{_failed};
    if (_workCancellation.IsCancelled())
        return std::nullopt;
    SampleCoverage sample;
    sample.sequence = _sampleSequence;
    sample.params = KernelParams();
    const bool duplicate = !_samples.empty() && _samples.back() == _samplePoint;
    if (!duplicate)
    {
        _samples.push_back(_samplePoint);
        if (_samples.size() > 4)
            _samples.erase(_samples.begin());
        const auto n = _samples.size();
        if (n == 1)
            sample.settled.push_back({_samplePoint, _samplePoint});
        else if (n >= 3)
            sample.settled =
                BrushCurveSegments(_samples[n >= 4 ? n - 4 : 0], _samples[n - 3], _samples[n - 2], _samplePoint);
        if (n >= 2)
            sample.tail.push_back({_samples[n - 2], _samplePoint});
    }
    std::unordered_set<std::uint64_t> changed;
    if (!duplicate)
    {
        if (!TileKeys(sample.tail, _sampleTailKeys))
            return std::nullopt;
        changed = _sampleTailKeys;
        if (!TileKeys(sample.settled, changed))
            return std::nullopt;
        changed.insert(_tailKeys.begin(), _tailKeys.end());
    }
    std::vector<std::uint64_t> ordered(changed.begin(), changed.end());
    std::sort(ordered.begin(), ordered.end());
    for (const auto key : ordered)
        if (_workCancellation.IsCancelled() || !AllocateTile(key))
            return std::nullopt;
    sample.tiles.reserve(ordered.size());
    for (const auto key : ordered)
    {
        if (_workCancellation.IsCancelled())
            return std::nullopt;
        auto& tile = _tiles[_index.at(key)];
        sample.tiles.push_back({key, tile.rect.x, tile.rect.y, tile.rect.width, tile.rect.height,
                                tile.permanent.empty() ? nullptr : tile.permanent.data(), tile.coverage.data()});
    }
    sample.needsGpu = !_usesCpuPermanent && !sample.tiles.empty();
    if (_usesCpuPermanent)
        for (const auto& tile : sample.tiles)
            if (!BrushKernel::RenderTileForWork(tile, sample.settled, sample.tail, sample.params, _workCancellation))
                return std::nullopt;
    _finalCoverageRemaining = sample.needsGpu ? ordered.size() : 0;
    _finalCoverageReceived.assign(ordered.size(), static_cast<std::uint8_t>(sample.needsGpu ? 0 : 1));
    _finalCoverageOrder = std::move(ordered);
    if (!duplicate)
        _tailKeys.swap(_sampleTailKeys);
    std::unordered_set<std::uint64_t>{}.swap(_sampleTailKeys);
    if (_workCancellation.IsCancelled())
        return std::nullopt;
    guard.ready = true;
    return sample;
}

bool BrushStroke::InstallSampleCoverage(std::uint64_t key, std::span<const std::uint32_t> values)
{
    if (!_detachedSample || !_sampleCoveragePrepared || _sampleCoverageComplete || _engine || _failed ||
        _workCancellation.IsCancelled())
        return false;
    const auto position = std::lower_bound(_finalCoverageOrder.begin(), _finalCoverageOrder.end(), key);
    if (position == _finalCoverageOrder.end() || *position != key)
        return false;
    const auto i = static_cast<std::size_t>(position - _finalCoverageOrder.begin());
    if (_finalCoverageReceived[i] || !_finalCoverageRemaining)
        return false;
    auto& tile = _tiles[_index.at(key)];
    if (values.size() != tile.coverage.size())
        return false;
    for (std::size_t y = 0; y < static_cast<std::size_t>(tile.rect.height); ++y)
    {
        if (_workCancellation.IsCancelled())
            return false;
        for (std::size_t x = 0; x < static_cast<std::size_t>(tile.rect.width); ++x)
        {
            const auto pixel = y * static_cast<std::size_t>(tile.rect.width) + x;
            tile.coverage[pixel] = static_cast<std::uint8_t>((std::min)(255u, values[pixel]));
        }
    }
    if (_workCancellation.IsCancelled())
        return false;
    _finalCoverageReceived[i] = 1;
    --_finalCoverageRemaining;
    return true;
}

bool BrushStroke::ComposeSampleCoverage(const SampleCoverage& sample)
{
    if (!_detachedSample || !_sampleCoveragePrepared || _sampleCoverageComplete || _engine || _failed ||
        sample.sequence != _sampleSequence || _finalCoverageRemaining ||
        sample.tiles.size() != _finalCoverageOrder.size() || _workCancellation.IsCancelled())
        return false;
    // Validate the complete description before composition. Private partial results stay detached.
    for (std::size_t i = 0; i < sample.tiles.size(); ++i)
    {
        const auto& work = sample.tiles[i];
        const auto found = _index.find(work.key);
        if (work.key != _finalCoverageOrder[i] || found == _index.end())
            return false;
        const auto& tile = _tiles[found->second];
        if (work.preview != tile.coverage.data() || work.x != tile.rect.x || work.y != tile.rect.y ||
            work.width != tile.rect.width || work.height != tile.rect.height)
            return false;
    }
    for (const auto& work : sample.tiles)
    {
        auto& tile = _tiles[_index.at(work.key)];
        if (_workCancellation.IsCancelled() || !ComposeTile(tile))
            return false;
        tile.touched = true;
        tile.changed = true;
    }
    if (_workCancellation.IsCancelled())
        return false;
    std::vector<std::uint64_t>{}.swap(_finalCoverageOrder);
    std::vector<std::uint8_t>{}.swap(_finalCoverageReceived);
    _sampleCoverageComplete = true;
    return true;
}

bool BrushStroke::AttachCoverageAfterSample(BrushCoverageEngine* engine) noexcept
{
    if (!_detachedSample || !_sampleCoveragePrepared || !_sampleCoverageComplete || _engine || _failed ||
        _workCancellation.IsCancelled() || !engine || engine != _sampleOwnerEngine ||
        _coverageOwner != std::this_thread::get_id() || engine->UsesCpuPermanent() != _usesCpuPermanent)
        return false;
    _engine = engine;
    _sampleOwnerEngine = nullptr;
    _detachedSample = false;
    _workerRasterEdit = false;
    EndWorkerPreparation();
    return true;
}

std::optional<BrushStroke::FinishMemoryPlan> BrushStroke::PlanFinishMemory() const noexcept
{
    if (!_valid || _failed || _workerRasterEdit || _detachedFinish || !_engine || !_tileOwnershipValid)
        return std::nullopt;
    FinishMemoryPlan plan;
    plan.retirementCpuBytes = RetirementMemoryBytes();
    if (!plan.retirementCpuBytes)
        return std::nullopt;
    const auto add = [](std::uint64_t& total, std::uint64_t count, std::uint64_t size) noexcept
    {
        return (!size || count <= (std::numeric_limits<std::uint64_t>::max)() / size) &&
               AddRetirementBytes(total, count * size);
    };
    GridRect finalGrid;
    std::uint64_t newTiles = 0;
    if (_samples.size() >= 2)
    {
        const auto count = _samples.size();
        const Vec2 before = _samples[count >= 3 ? count - 3 : 0];
        const Vec2 start = _samples[count - 2];
        const Vec2 end = _samples[count - 1];
        const Vec2 after = end;
        // This is the cubic Bezier form of the very same nonuniform centripetal Catmull-Rom segment.
        // Its convex hull bounds all adaptive chord endpoints; only the last three samples are read.
        const double a = Knot(0, before, start);
        const double b = Knot(0, start, end);
        const double c = Knot(0, end, after);
        const Vec2 first{
            start.x + b * ((start.x - before.x) / a - (end.x - before.x) / (a + b) + (end.x - start.x) / b) / 3,
            start.y + b * ((start.y - before.y) / a - (end.y - before.y) / (a + b) + (end.y - start.y) / b) / 3};
        const Vec2 second{
            end.x - b * ((end.x - start.x) / b - (after.x - start.x) / (b + c) + (after.x - end.x) / c) / 3,
            end.y - b * ((end.y - start.y) / b - (after.y - start.y) / (b + c) + (after.y - end.y) / c) / 3};
        const std::array<Vec2, 4> controls{start, first, second, end};
        const SelectionBounds hull = PointBounds(controls);
        if (!std::isfinite(hull.minX) || !std::isfinite(hull.maxX) || !std::isfinite(hull.minY) ||
            !std::isfinite(hull.maxY))
            return std::nullopt;
        // One document pixel is conservative numerical padding in addition to TileKeys' exact tip reach.
        const double reach = _settings.diameter / 2 + 3;
        const double left = (std::max)(0.0, hull.minX - reach);
        const double top = (std::max)(0.0, hull.minY - reach);
        const double right = (std::min)(static_cast<double>(_canvasWidth), hull.maxX + reach);
        const double bottom = (std::min)(static_cast<double>(_canvasHeight), hull.maxY + reach);
        if (right > left && bottom > top)
        {
            const GridRect affected =
                MappedBox(_pixelToDocument.Inverted(), left, top, right, bottom).Intersect({0, 0, _width, _height});
            if (!affected.Empty())
            {
                const int firstColumn = affected.x / kTileSize;
                const int lastColumn = (affected.x + affected.width - 1) / kTileSize;
                const int firstRow = affected.y / kTileSize;
                const int lastRow = (affected.y + affected.height - 1) / kTileSize;
                newTiles = static_cast<std::uint64_t>(lastColumn - firstColumn + 1) *
                           static_cast<std::uint64_t>(lastRow - firstRow + 1);
                const int x = firstColumn * kTileSize;
                const int y = firstRow * kTileSize;
                const int rightEdge = (std::min)(_width, (lastColumn + 1) * kTileSize);
                const int bottomEdge = (std::min)(_height, (lastRow + 1) * kTileSize);
                finalGrid = {x, y, rightEdge - x, bottomEdge - y};
            }
        }
    }
    plan.maximumTiles = _tiles.size();
    plan.maximumChangedTiles = _tailKeys.size();
    if (!AddRetirementBytes(plan.maximumTiles, newTiles) || !AddRetirementBytes(plan.maximumChangedTiles, newTiles))
        return std::nullopt;
    plan.commitBounds = _allocated.value_or(_sourceRect).Union(finalGrid);
    if (plan.commitBounds.width > kMaxCanvasSide || plan.commitBounds.height > kMaxCanvasSide ||
        static_cast<std::int64_t>(plan.commitBounds.width) * plan.commitBounds.height > _pixelLimit)
        return std::nullopt;

    auto& bytes = plan.additionalCpuBytes;
    bytes = 64 * 1024; // fixed reader stack, control blocks and allocator headroom
    // At most 2^10 final chords; geometric vector growth is at most twice its final size on the supported STL.
    // Changed hash nodes, old/new vector capacities, index buckets and final acknowledgement buffers coexist.
    if (!add(bytes, 2 * (std::uint64_t{1} << kCurveDepth), sizeof(BrushSegment)) ||
        !add(bytes, plan.maximumChangedTiles, 256 + 2 * sizeof(BrushKernelTile) + 4 * sizeof(std::uint64_t)) ||
        !add(bytes, plan.maximumTiles, 2 * sizeof(Tile) + 256 + 8 * sizeof(RasterTile) + 4096))
        return std::nullopt;
    constexpr auto tilePixels = static_cast<std::uint64_t>(kTileSize) * kTileSize;
    const std::uint64_t channels = _mask ? 1 : 4;
    // New base allocation/copy + coverage + optional permanent; replaced tile outputs remain owned until
    // publication retires the whole draft. One composing vector and its Make copy may coexist with old image.
    if (!add(bytes, newTiles, tilePixels * (2 * channels + 1 + (_usesCpuPermanent ? 4 : 0))) ||
        !add(bytes, plan.maximumTiles, tilePixels * 2 * channels + 4096))
        return std::nullopt;
    const auto windows = _source.GridReplacementWorkspaceBytes();
    if (!windows || !AddRetirementBytes(bytes, windows.value()))
        return std::nullopt;
    const auto outputPixels = static_cast<std::uint64_t>(plan.commitBounds.width) * plan.commitBounds.height;
    const auto buffer = [&](std::uint64_t pixels, std::uint64_t components, std::uint64_t allocations = 1) noexcept
    {
        if (pixels > (std::numeric_limits<std::size_t>::max)() / components)
            return false;
        const auto required = PixelBuffer::RequiredBytes(static_cast<std::size_t>(pixels * components));
        return required && add(bytes, allocations, required);
    };
    // Reserve the sparse output's possible assembled cache, including its size class and allocation record.
    if (outputPixels && !buffer(outputPixels, channels))
        return std::nullopt;
    const auto outputGridTiles = static_cast<std::uint64_t>((plan.commitBounds.width + kTileSize - 1) / kTileSize) *
                                 ((plan.commitBounds.height + kTileSize - 1) / kTileSize);
    // Replacing may add outside-fill windows. Four disjoint strips, vector growth/splitting and the published
    // sparse metadata can overlap. Their single shared uniform pixel block has no output-sized pixel plane.
    if (!add(bytes, outputGridTiles, 4 * 8 * sizeof(RasterTile)) || !add(bytes, 2, tilePixels * channels + 512))
        return std::nullopt;
    if (_mask && _source.HasStorage() && _source.PixelCount() > 1 &&
        (_source.Width() != _sourceRect.width || _source.Height() != _sourceRect.height))
    {
        // PaintSnapshot retains the stretched mask under the replacement tiles. It coexists with the future
        // sparse cache; StretchedRasterForWork also owns one original source row and an integer column map.
        if (!buffer(static_cast<std::uint64_t>(_sourceRect.width) * _sourceRect.height, channels) ||
            !add(bytes, static_cast<std::uint64_t>(_source.Stride()), 1) ||
            !add(bytes, static_cast<std::uint64_t>(_sourceRect.width), sizeof(int)))
            return std::nullopt;
    }
    if (!_mask && _carriedMask.HasStorage() && _carriedMask.PixelCount() > 1)
    {
        // The actual CarriedMaskForWork fuses stretch/crop: one output plus a sparse source row and column map.
        // It never allocates a full old stretched plane. The immutable original mask is already in retirement.
        if (!buffer(outputPixels, 1) || !add(bytes, static_cast<std::uint64_t>(_carriedMask.Stride()), 1) ||
            !add(bytes, static_cast<std::uint64_t>(plan.commitBounds.width), sizeof(int)))
            return std::nullopt;
    }
    if ((_mask || _carriedMask.HasStorage()) &&
        (!buffer(96 * 96, 1, 2) || !add(bytes, static_cast<std::uint64_t>(plan.commitBounds.width), channels)))
        return std::nullopt;

    if (Healing())
    {
        const GridRect painted = _tileBounds.value_or(GridRect{}).Union(finalGrid);
        if (!painted.Empty())
        {
            const auto longest = (std::max)(painted.width, painted.height);
            const auto reach = static_cast<std::int64_t>(std::ceil((static_cast<double>(longest) + 32) * 3.2));
            const auto left = (std::max)(std::int64_t{0}, static_cast<std::int64_t>(painted.x) - reach);
            const auto top = (std::max)(std::int64_t{0}, static_cast<std::int64_t>(painted.y) - reach);
            const auto right = (std::min)(static_cast<std::int64_t>(_width),
                                          static_cast<std::int64_t>(painted.x) + painted.width + reach);
            const auto bottom = (std::min)(static_cast<std::int64_t>(_height),
                                           static_cast<std::int64_t>(painted.y) + painted.height + reach);
            if (right <= left || bottom <= top || right - left > kMaxCanvasSide || bottom - top > kMaxCanvasSide ||
                (right - left) * (bottom - top) > kMaxLayerPixels)
                return std::nullopt;
            plan.healRegion = {static_cast<int>(left), static_cast<int>(top), static_cast<int>(right - left),
                               static_cast<int>(bottom - top)};
            auto width = static_cast<std::uint64_t>(plan.healRegion.width);
            auto height = static_cast<std::uint64_t>(plan.healRegion.height);
            // Input BGRA, coverage, role, near and four float channels; all recursive coarse grids coexist.
            if (!add(bytes, width * height, 4 + 1 + 1 + 1 + 4 * sizeof(float)) ||
                !add(bytes, (std::max)(width, height) + 1, sizeof(std::int64_t)))
                return std::nullopt;
            for (unsigned depth = 0; depth < 16 && (width > 32 || height > 32); ++depth)
            {
                width = (width + 1) / 2;
                height = (height + 1) / 2;
                if (!add(bytes, width * height, 4 * sizeof(float) + 1))
                    return std::nullopt;
            }
        }
    }
    if (!_usesCpuPermanent)
    {
        // GPU density/uint coverage for potentially new tiles, their two buffers plus two views/driver allocation
        // allowance, segment-buffer growth and one extra generation of that dynamic buffer. Queue staging is separate.
        if (!add(plan.additionalGpuBytes, newTiles, tilePixels * 8 + 4 * 4096) ||
            !add(plan.additionalGpuBytes, 2 * (std::uint64_t{1} << kCurveDepth), 4 * sizeof(float)) ||
            !AddRetirementBytes(plan.additionalGpuBytes, 4 * 4096))
            return std::nullopt;
    }
    if (!AddRetirementBytes(plan.retirementCpuBytes, bytes))
        return std::nullopt;
    return plan;
}

std::optional<BrushStroke::FinalCoverage> BrushStroke::PrepareFinalCoverage(CancelToken cancellation)
{
    if (!_detachedFinish || _engine || !_valid || _failed || _finalCoveragePrepared || cancellation.IsCancelled())
        return std::nullopt;
    _workCancellation = std::move(cancellation);
    FinalCoverage final;
    final.params = KernelParams();
    const std::size_t n = _samples.size();
    if (n >= 2)
        final.settled =
            BrushCurveSegments(_samples[n >= 3 ? n - 3 : 0], _samples[n - 2], _samples[n - 1], _samples[n - 1]);
    std::unordered_set<std::uint64_t> changed;
    if (!TileKeys(final.settled, changed))
        return std::nullopt;
    changed.insert(_tailKeys.begin(), _tailKeys.end());
    std::vector<std::uint64_t> ordered(changed.begin(), changed.end());
    std::sort(ordered.begin(), ordered.end());
    for (const auto key : ordered)
    {
        if (_workCancellation.IsCancelled() || !AllocateTile(key))
            return std::nullopt;
    }
    final.tiles.reserve(ordered.size());
    for (const auto key : ordered)
    {
        if (_workCancellation.IsCancelled())
            return std::nullopt;
        Tile& tile = _tiles[_index.at(key)];
        final.tiles.push_back({key, tile.rect.x, tile.rect.y, tile.rect.width, tile.rect.height,
                               tile.permanent.empty() ? nullptr : tile.permanent.data(), tile.coverage.data()});
    }
    final.needsGpu = !_usesCpuPermanent && !final.tiles.empty();
    if (_usesCpuPermanent)
        for (const auto& tile : final.tiles)
            if (!BrushKernel::RenderTileForWork(tile, final.settled, {}, KernelParams(), _workCancellation))
                return std::nullopt;
    _finalCoverageRemaining = final.needsGpu ? ordered.size() : 0;
    _finalCoverageReceived.assign(ordered.size(), static_cast<std::uint8_t>(final.needsGpu ? 0 : 1));
    _finalCoverageOrder = std::move(ordered);
    if (_workCancellation.IsCancelled())
        return std::nullopt;
    _finalCoveragePrepared = true;
    return final;
}

bool BrushStroke::InstallFinalCoverage(std::uint64_t key, std::span<const std::uint32_t> values)
{
    if (!_detachedFinish || !_finalCoveragePrepared || _finalCoverageComplete || _engine ||
        _workCancellation.IsCancelled())
        return false;
    const auto position = std::lower_bound(_finalCoverageOrder.begin(), _finalCoverageOrder.end(), key);
    if (position == _finalCoverageOrder.end() || *position != key)
        return false;
    const auto coverageIndex = static_cast<std::size_t>(position - _finalCoverageOrder.begin());
    if (_finalCoverageReceived[coverageIndex] != 0)
        return false;
    const auto found = _index.find(key);
    if (found == _index.end())
        return false;
    Tile& tile = _tiles[found->second];
    const auto width = static_cast<std::size_t>(tile.rect.width);
    const auto height = static_cast<std::size_t>(tile.rect.height);
    if (values.size() != width * height || tile.coverage.size() != values.size())
        return false;
    for (std::size_t y = 0; y < height; ++y)
    {
        if (_workCancellation.IsCancelled())
            return false;
        for (std::size_t x = 0; x < width; ++x)
        {
            const auto i = y * width + x;
            tile.coverage[i] = static_cast<std::uint8_t>((std::min)(255u, values[i]));
        }
    }
    if (_workCancellation.IsCancelled())
        return false;
    _finalCoverageReceived[coverageIndex] = 1;
    --_finalCoverageRemaining;
    return true;
}

bool BrushStroke::ComposeFinalCoverage(const FinalCoverage& final)
{
    if (!_detachedFinish || !_finalCoveragePrepared || _finalCoverageComplete || _engine ||
        _finalCoverageRemaining != 0 || final.tiles.size() != _finalCoverageOrder.size() ||
        _workCancellation.IsCancelled())
        return false;
    for (std::size_t i = 0; i < final.tiles.size(); ++i)
    {
        const auto& work = final.tiles[i];
        if (work.key != _finalCoverageOrder[i])
            return false;
        if (_workCancellation.IsCancelled())
            return false;
        const auto found = _index.find(work.key);
        if (found == _index.end())
            return false;
        Tile& tile = _tiles[found->second];
        if (work.preview != tile.coverage.data() || work.x != tile.rect.x || work.y != tile.rect.y ||
            work.width != tile.rect.width || work.height != tile.rect.height || !ComposeTile(tile))
            return false;
        tile.touched = true;
        tile.changed = true;
    }
    if (!_samples.empty())
    {
        const Vec2 last = _samples.back();
        _samples.assign(1, last);
    }
    _tailKeys.clear();
    if (_workCancellation.IsCancelled())
        return false;
    _finalCoverageComplete = true;
    return true;
}

void DocumentSession::DetachCoverageForShutdown() noexcept
{
    const auto detach = [](const std::shared_ptr<BrushStroke>& stroke) noexcept
    {
        if (stroke)
            stroke->ReleaseCoverageEngine();
    };
    detach(_brush);
    if (_gradient)
        detach(_gradient->raster);
    if (_pixelMove)
        detach(_pixelMove->raster);
    _brushEngine = nullptr;
}

void DocumentSession::RetireCpuForShutdown() noexcept
{
    assert(!_brushEngine);
    _document.reset();
    _displayCopy.reset();
    history.Reset();
    _transformEdit.reset();
    CancelLasso();
    _selectionMoveOrigin.reset();
    _brush.reset();
    _brushFinishingOpening.reset();
    _brushUpdatingOpening.reset();
    _warp.reset();
    _warpUpdatingOpening.reset();
    _warpUpdatingSequence.reset();
    _warpFinishingOpening.reset();
    _gradient.reset();
    _shapeDraft.reset();
    _pixelMove.reset();
    _colorEdit.reset();
    _clipboard.reset();
    _lastBrushCommit.reset();
    _adjustmentOriginal.reset();
    _warpSource.image = {};
    _selected.clear();
    ClearCollapsedGroups();
    std::vector<double>{}.swap(_snapGuides.xs);
    std::vector<double>{}.swap(_snapGuides.ys);
    for (auto& channel : filterSettings.curves.channels)
        std::vector<CurvePoint>{}.swap(channel);
    _activeLayerID.reset();
    _transformDuplicate.reset();
    _cropRect.reset();
    _adjustmentEditingID.reset();
    _lastBrushPoint.reset();
    _pendingOpacityDigit.reset();
    _cloneOffset.reset();
    _maskSelected = false;
    _tokenIdentity.value = Guid::Generate();
    ++_documentSerial;
    ++_restoreSerial;
    NoteDocumentMutation();
}

std::uint64_t BrushStroke::RetirementMemoryBytes() const noexcept
{
    std::uint64_t total = sizeof(*this) + 1024; // control/allocator/stop-token headroom
    if (!AddRetirementCapacity<std::uint64_t>(total, _finalCoverageOrder.capacity()) ||
        !AddRetirementCapacity<std::uint8_t>(total, _finalCoverageReceived.capacity()) ||
        !AddRetirementCapacity<Vec2>(total, _samples.capacity()) ||
        !AddRetirementCapacity<std::uint64_t>(total, _landingTiles.capacity()) ||
        _editName.capacity() == std::numeric_limits<std::size_t>::max() ||
        !AddRetirementCapacity<wchar_t>(total, _editName.capacity() + 1) ||
        !AddRetirementCapacity<Tile>(total, _tiles.capacity()) || !AddRetirementHashStorage(total, _tailKeys) ||
        !AddRetirementHashStorage(total, _index) || !AddRetirementHashStorage(total, _sampleTailKeys))
    {
        return 0;
    }

    if (!AddRetirementRaster(total, _source) ||
        (_selection.has_value() && !AddRetirementRaster(total, _selection.value())) ||
        (_sample.has_value() && !AddRetirementRaster(total, _sample.value())) ||
        !AddRetirementRaster(total, _carriedMask) ||
        (_lifted.has_value() && !AddRetirementRaster(total, _lifted->image)))
    {
        return 0;
    }

    if (_warpSample)
    {
        const auto bytes = _warpSample->RetirementMemoryBytes();
        if (!bytes || !AddRetirementBytes(total, bytes))
            return 0;
    }
    // Producers cache each tile's complete buffer/raster ownership after every allocation or replacement.
    // Pointer release reads this aggregate, never the tiles or their pixels. Duplicate immutable ownership
    // stays conservatively counted just as in the original per-tile reference bound.
    return _tileOwnershipValid && AddRetirementBytes(total, _tileOwnershipBytes) ? total : 0;
}

void BrushStroke::RefreshTileOwnership(Tile& tile) noexcept
{
    if (!_tileOwnershipValid)
        return;
    std::uint64_t next = 0;
    if (!AddRetirementCapacity<float>(next, tile.permanent.capacity()) ||
        !AddRetirementCapacity<std::uint8_t>(next, tile.coverage.capacity()) || !AddRetirementRaster(next, tile.base) ||
        !AddRetirementRaster(next, tile.image) || tile.ownershipBytes > _tileOwnershipBytes)
    {
        _tileOwnershipValid = false;
        return;
    }
    const auto remaining = _tileOwnershipBytes - tile.ownershipBytes;
    if (next > (std::numeric_limits<std::uint64_t>::max)() - remaining)
    {
        _tileOwnershipValid = false;
        return;
    }
    _tileOwnershipBytes = remaining + next;
    tile.ownershipBytes = next;
}

bool BrushStroke::ConfigureWorkerRasterEdit(CancelToken cancellation,
                                            void (*progress)(void*, std::size_t, std::size_t) noexcept,
                                            void* progressContext) noexcept
{
    if (!_valid || _engine != &CpuBrushCoverage::Shared() || !_tiles.empty() || !_samples.empty())
        return false;
    _workerRasterEdit = true;
    _workCancellation = std::move(cancellation);
    _workProgress = progress;
    _workProgressContext = progressContext;
    return true;
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
    if (_detachedFinish || _detachedSample)
        return false;
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
    if (_detachedFinish || _detachedSample)
        return false;
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

bool BrushStroke::TileKeys(std::span<const BrushSegment> segments, std::unordered_set<std::uint64_t>& keys)
{
    const double reach = _settings.diameter / 2.0 + 2.0;
    const Affine2 inverse = _pixelToDocument.Inverted();
    const int columns = (_width + kTileSize - 1) / kTileSize;
    const auto tileGrid = [&](const BrushSegment& segment) noexcept -> std::optional<GridRect>
    {
        const double left = (std::max)(0.0, (std::min)(segment.from.x, segment.to.x) - reach);
        const double top = (std::max)(0.0, (std::min)(segment.from.y, segment.to.y) - reach);
        const double right =
            (std::min)(static_cast<double>(_canvasWidth), (std::max)(segment.from.x, segment.to.x) + reach);
        const double bottom =
            (std::min)(static_cast<double>(_canvasHeight), (std::max)(segment.from.y, segment.to.y) + reach);
        if (right <= left || bottom <= top)
            return GridRect{};
        const GridRect mapped = MappedBox(inverse, left, top, right, bottom);
        if (mapped.Empty())
            return std::nullopt;
        const GridRect affected = mapped.Intersect({0, 0, _width, _height});
        if (affected.Empty())
            return GridRect{};
        const int x = affected.x / kTileSize * kTileSize;
        const int y = affected.y / kTileSize * kTileSize;
        const int rightEdge = (std::min)(_width, ((affected.x + affected.width - 1) / kTileSize + 1) * kTileSize);
        const int bottomEdge = (std::min)(_height, ((affected.y + affected.height - 1) / kTileSize + 1) * kTileSize);
        return GridRect{x, y, rightEdge - x, bottomEdge - y};
    };
    GridRect projected = _allocated.value_or(_source.HasStorage() ? _sourceRect : GridRect{});
    for (const auto& segment : segments)
    {
        if (_workerRasterEdit && _workCancellation.IsCancelled())
            return false;
        const auto grid = tileGrid(segment);
        if (!grid)
            return false;
        projected = projected.Union(grid.value()); // All coordinates lie in the validated <=1e9 grid.
        if (projected.width > kMaxCanvasSide || projected.height > kMaxCanvasSide ||
            static_cast<std::int64_t>(projected.width) * projected.height > _pixelLimit)
        {
            _pixelLimitRefused = true;
            return false;
        }
    }
    for (const auto& segment : segments)
    {
        const auto grid = tileGrid(segment);
        if (!grid || grid->Empty())
            continue;
        const int firstColumn = grid->x / kTileSize;
        const int lastColumn = (grid->x + grid->width - 1) / kTileSize;
        const int firstRow = grid->y / kTileSize;
        const int lastRow = (grid->y + grid->height - 1) / kTileSize;
        for (int row = firstRow; row <= lastRow; ++row)
        {
            if (_workerRasterEdit && _workCancellation.IsCancelled())
                return false;
            for (int column = firstColumn; column <= lastColumn; ++column)
                keys.insert(static_cast<std::uint64_t>(row) * static_cast<std::uint64_t>(columns) +
                            static_cast<std::uint64_t>(column));
        }
    }
    return true;
}

bool BrushStroke::AllocateTile(std::uint64_t key, bool sourceOnly)
{
    if (_workerRasterEdit && _workCancellation.IsCancelled())
        return false;
    if (_index.contains(key))
    {
        return true;
    }
    const int columns = (_width + kTileSize - 1) / kTileSize;
    const int column = static_cast<int>(key % static_cast<std::uint64_t>(columns));
    const int row = static_cast<int>(key / static_cast<std::uint64_t>(columns));
    GridRect rect{column * kTileSize, row * kTileSize, std::min(kTileSize, _width - column * kTileSize),
                  std::min(kTileSize, _height - row * kTileSize)};
    // Detached Clear cannot create pixels outside the existing source. Do not charge its temporary canvas
    // tile as a grown committed image, or a one-pixel clear near the document cap would refuse unnecessarily.
    // Pointer/GPU brush tiles and operations that can grow the source retain their complete tile rectangles.
    if (sourceOnly)
        rect = rect.Intersect(_sourceRect);
    if (rect.Empty())
        return false;
    // Every allocated tile grows the committed bounds; they stay within the document limits and the pixel budget.
    const GridRect next =
        _allocated.has_value() ? _allocated->Union(rect) : (_source.HasStorage() ? _sourceRect.Union(rect) : rect);
    if (next.width > kMaxCanvasSide || next.height > kMaxCanvasSide ||
        static_cast<std::int64_t>(next.width) * static_cast<std::int64_t>(next.height) > _pixelLimit)
    {
        _pixelLimitRefused = true;
        return false;
    }
    _allocated = next;
    Tile tile;
    tile.key = key;
    tile.rect = rect;
    try
    {
        const std::size_t count = static_cast<std::size_t>(rect.width) * static_cast<std::size_t>(rect.height);
        if (!_workerRasterEdit || _detachedFinish || _detachedSample)
            tile.coverage.assign(count, 0);
        if ((!_workerRasterEdit || _detachedFinish || _detachedSample) && _usesCpuPermanent)
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
            if (_workerRasterEdit)
            {
                if (!ReadRasterRegionForWork(
                        _source, {rect.x - _sourceRect.x, rect.y - _sourceRect.y, rect.width, rect.height}, bytes,
                        static_cast<std::size_t>(rect.width) * channels, fill, _workCancellation))
                    return false;
            }
            else
                _source.ReadRegion(rect.x - _sourceRect.x, rect.y - _sourceRect.y, rect.width, rect.height,
                                   bytes.data(), rect.width * channels, fill);
        }
        else if (_source.HasStorage())
        {
            // A uniform mask (or any image stretched over the layer) reads the same pixel everywhere it covers.
            const GridRect covered = rect.Intersect(_sourceRect);
            std::array<std::uint8_t, 4> uniform{};
            const bool readUniform = _workerRasterEdit && _source.Width() == 1 && _source.Height() == 1;
            if (readUniform &&
                !ReadRasterRegionForWork(_source, {0, 0, 1, 1}, {uniform.data(), static_cast<std::size_t>(channels)},
                                         static_cast<std::size_t>(channels), fill, _workCancellation))
                return false;
            for (int y = covered.y; y < covered.y + covered.height; ++y)
            {
                if (_workerRasterEdit && _workCancellation.IsCancelled())
                    return false;
                for (int x = covered.x; x < covered.x + covered.width; ++x)
                {
                    const int sx = static_cast<int>((static_cast<std::int64_t>(x - _sourceRect.x) * _source.Width()) /
                                                    _sourceRect.width);
                    const int sy = static_cast<int>((static_cast<std::int64_t>(y - _sourceRect.y) * _source.Height()) /
                                                    _sourceRect.height);
                    std::uint8_t* const to =
                        bytes.data() + (static_cast<std::size_t>(y - rect.y) * static_cast<std::size_t>(rect.width) +
                                        static_cast<std::size_t>(x - rect.x)) *
                                           static_cast<std::size_t>(channels);
                    if (readUniform)
                        std::copy_n(uniform.data(), channels, to);
                    else if (_workerRasterEdit)
                    {
                        if (!ReadRasterRegionForWork(_source, {sx, sy, 1, 1}, {to, static_cast<std::size_t>(channels)},
                                                     static_cast<std::size_t>(channels), fill, _workCancellation))
                            return false;
                    }
                    else
                        _source.ReadRegion(sx, sy, 1, 1, to, channels, fill);
                }
            }
        }
        tile.base = Raster::Make(rect.width, rect.height, format, bytes.data(), bytes.size());
        tile.image = tile.base;
        _tiles.push_back(std::move(tile));
        _tileBounds = _tileBounds ? _tileBounds->Union(rect) : rect;
        RefreshTileOwnership(_tiles.back());
        _index.emplace(key, _tiles.size() - 1);
    }
    catch (const std::bad_alloc&)
    {
        if (_workerRasterEdit)
            throw;
        return false;
    }
    return true;
}

bool BrushStroke::Render(const std::vector<BrushSegment>& settled, const std::vector<BrushSegment>& tail)
{
    std::unordered_set<std::uint64_t> tailKeys;
    if (!TileKeys(tail, tailKeys))
    {
        _failed = true;
        return false;
    }
    std::unordered_set<std::uint64_t> changed = tailKeys;
    if (!TileKeys(settled, changed))
    {
        _failed = true;
        return false;
    }
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
    const bool sampling = _warpSample || (_sample.has_value() && _sample->HasStorage() &&
                                          _sample->Format() == (_mask ? RasterFormat::Gray8 : RasterFormat::Bgra8888));
    const bool washing = !sampling && Healing() && !_mask;
    const auto round8 = [](double value)
    { return static_cast<std::uint8_t>(std::lround(std::clamp(value, 0.0, 255.0))); };
    for (std::size_t index = 0; index < count; ++index)
    {
        if (_workerRasterEdit && index % static_cast<std::size_t>(tile.rect.width) == 0 &&
            _workCancellation.IsCancelled())
            return false;
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
            if (_warpSample ? !_warpSample->Sample(document, sample, _workCancellation)
                            : !SampleAt(*_sample, document.x + _sampleX, document.y + _sampleY, sample))
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
    RefreshTileOwnership(tile);
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
        if (_workerRasterEdit && _workCancellation.IsCancelled())
            return {};
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
    if (!_valid || _failed || _detachedSample || (_detachedFinish && !_finalCoverageComplete) ||
        (_workerRasterEdit && _workCancellation.IsCancelled()))
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
            if (_workerRasterEdit && _workCancellation.IsCancelled())
                return std::nullopt;
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
            if (_workerRasterEdit)
            {
                if (!ReadRasterRegionForWork(_source, {0, 0, 1, 1}, {&uniform, 1}, 1, fill, _workCancellation))
                    return std::nullopt;
            }
            else
                _source.ReadRegion(0, 0, 1, 1, &uniform, _source.Channels(), fill);
            fill = uniform;
        }
        else
        {
            base = _workerRasterEdit
                       ? StretchedRasterForWork(_source, _sourceRect.width, _sourceRect.height, _workCancellation)
                       : _source.Stretched(_sourceRect.width, _sourceRect.height);
            if (!base.HasStorage())
            {
                return std::nullopt;
            }
        }
    }
    commit.image = Raster::Replacing(base, _sourceRect.x, _sourceRect.y, Patches(), crop.x, crop.y, crop.width,
                                     crop.height, fill, _workCancellation);
    if (!commit.image.HasStorage())
    {
        return std::nullopt;
    }
    if (_mask)
    {
        commit.thumbnail = _workerRasterEdit ? ThumbnailRasterForWork(commit.image, 96, _workCancellation)
                                             : Raster::MakeThumbnail(commit.image);
        if (!commit.thumbnail.HasStorage())
            return std::nullopt;
    }
    commit.transform = TransformFor(crop);
    if (!commit.transform.IsValid() || (_workerRasterEdit && _workCancellation.IsCancelled()))
    {
        return std::nullopt;
    }
    return commit;
}

// ── DocumentSession ──────────────────────────────────────────────────────

bool EffectivelyVisible(std::span<const ImageLayer> layers, const Guid& id) noexcept
{
    std::optional<Guid> current = id;
    int depth = 0;
    while (current.has_value() && depth < kMaxLayerCount)
    {
        const ImageLayer* found = nullptr;
        for (const ImageLayer& layer : layers)
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
    if (!_document.has_value() || _transformEdit.has_value() || _brush || HasColorEdit() || _warpUpdatingOpening ||
        _warpFinishingOpening || (_pixelMove && _pixelMove->deferred))
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
    if (!EffectivelyVisible(_document->layers, layer->id))
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
    return BeginBrush(point, kind, composite, brushSettings, maskPaintWhite);
}

bool DocumentSession::BeginBrush(Vec2 point, BrushKind kind, const Raster* composite, BrushSettings settings,
                                 bool maskWhite, std::optional<Vec2> capturedCloneOffset)
{
    if (capturedCloneOffset && (kind != BrushKind::Clone || !_document || !composite || !composite->HasStorage() ||
                                composite->Format() != RasterFormat::Bgra8888 ||
                                composite->Width() != _document->width || composite->Height() != _document->height ||
                                !std::isfinite(capturedCloneOffset->x) || !std::isfinite(capturedCloneOffset->y) ||
                                std::round(capturedCloneOffset->x) < (std::numeric_limits<int>::min)() ||
                                std::round(capturedCloneOffset->x) > (std::numeric_limits<int>::max)() ||
                                std::round(capturedCloneOffset->y) < (std::numeric_limits<int>::min)() ||
                                std::round(capturedCloneOffset->y) > (std::numeric_limits<int>::max)()))
        return false; // Refuse captured input before resolving a gradient or touching alignment/history.
    if (_brushFinishingOpening || _brushUpdatingOpening || _brushOpening == UINT64_MAX)
        return false;
    if (HasDeferredTransform() || (_gradient && _gradient->deferred) || (_pixelMove && _pixelMove->deferred))
        return false;
    // A pending gradient applies first, so the stroke paints over it and never drops it (Decision D3).
    if (!ResolveGradient())
        return false;
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
    if (mask)
    {
        settings.red = maskWhite ? 1.0 : 0.0;
        settings.green = settings.red;
        settings.blue = settings.red;
    }
    std::optional<Raster> sample;
    int offsetX = 0;
    int offsetY = 0;
    std::wstring editName;
    if (kind == BrushKind::Clone)
    {
        const std::optional<Vec2> offset = capturedCloneOffset ? capturedCloneOffset : CloneStrokeOffset(point);
        if (!offset.has_value())
        {
            return false;
        }
        // The admitted host retains the event-time offset and a complete committed composite. Later live
        // source/settings changes cannot reinterpret that accepted stroke. Direct callers retain their reference.
        if (capturedCloneOffset)
            sample = *composite;
        else
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
    ++_brushOpening;
    _brush = std::move(stroke);
    return true;
}

void DocumentSession::ContinueBrush(Vec2 point)
{
    if (!_brush)
    {
        return;
    }
    // A worker-opened draft has no pointer sample yet. Preserve Shift-line metadata using the accepted
    // point and its exact target, without producing coverage or pixels during opening publication.
    _lastBrushPoint = LastBrushPoint{point, _brush->LayerID(), _brush->IsMask()};
    if (!_brush->Append(point))
    {
        CancelBrush();
    }
}

void DocumentSession::CancelBrush() noexcept
{
    _brush.reset();
    _brushFinishingOpening.reset();
    _brushUpdatingOpening.reset();
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
    if (HasDeferredTransform())
        return false;
    const ImageLayer* current = FindLayer(stroke->LayerID());
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
    const auto index = IndexOf(_document.value().layers, stroke->LayerID());
    if (!index)
        return false;
    auto draft = CanvasDocumentDraft::Clone(_document.value(), _document.value().layers.size());
    if (!draft)
        return false;
    {
        ImageLayer& edited = draft.value().EditLayers()[index.value()];
        if (stroke->IsMask())
        {
            edited.mask->asset.image = result->image;
            edited.mask->asset.thumbnail = result->thumbnail;
        }
        else
        {
            // A mask on the layer's own grid grows with the layer; only the new area is revealed (the old one keeps the
            // mask's pixels and fill). A uniform mask stays as is.
            if (edited.mask.has_value() && !edited.mask->placement.has_value() &&
                result->bounds != stroke->SourceRect() && edited.mask->asset.image.PixelCount() > 1)
            {
                const GridRect& source = stroke->SourceRect();
                const Raster grown =
                    CarriedMask(edited.mask->asset.image, source.width, source.height, source.x - result->bounds.x,
                                source.y - result->bounds.y, result->bounds.width, result->bounds.height, 255);
                if (grown.HasStorage())
                {
                    edited.mask->asset.image = grown;
                    edited.mask->asset.thumbnail = Raster::MakeThumbnail(grown);
                }
            }
            const std::wstring layerName = edited.name;
            edited.asset = RasterAsset{result->image, result->thumbnail, layerName};
            edited.transform = result->transform;
        }
    }
    if (selectionAfterCommit.has_value())
    {
        draft.value().DocumentState().selection = selectionAfterCommit;
    }
    auto after = draft.value().Seal();
    if (!after)
        return false;
    BeginEdit(name);
    std::swap(_document.value(), after.value());
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
    if (HasDeferredTransform() || (_gradient && _gradient->deferred) || (_pixelMove && _pixelMove->deferred))
        return;
    // Targeting the new mask is a target switch: a pending gradient applies first.
    if (!ResolveGradient())
        return;
    const ImageLayer* layer = ActiveLayer();
    if (!_document.has_value() || layer == nullptr || layer->mask.has_value())
    {
        return;
    }
    CommitTransform();
    // Settlement can replace the root; the mask targets the same active layer in the settled document.
    layer = ActiveLayer();
    if (!layer || layer->mask)
        return;
    if (PixelBudgetLeft(true) < 1)
    {
        _budgetRefused = true;
        return;
    }
    const std::uint8_t value = revealing ? 255 : 0;
    const Raster mask = Raster::Make(1, 1, RasterFormat::Gray8, &value, 1);
    const auto index = IndexOf(_document.value().layers, layer->id);
    if (!index)
        return;
    auto draft = CanvasDocumentDraft::Clone(_document.value(), _document.value().layers.size());
    if (!draft)
        return;
    draft.value().EditLayers()[index.value()].mask =
        LayerMask{RasterAsset{mask, mask, layer->name}, true, std::nullopt, true};
    auto after = draft.value().Seal();
    if (!after)
        return;
    BeginEdit(revealing ? L"Add Reveal-All Mask" : L"Add Hide-All Mask");
    std::swap(_document.value(), after.value());
    ++_editRevision;
    EndEdit();
    _maskSelected = true;
}

void DocumentSession::ToggleLayerMask(const Guid& id)
{
    if (HasDeferredTransform())
        return;
    const ImageLayer* layer = FindLayer(id);
    if (layer == nullptr || !layer->mask.has_value())
    {
        return;
    }
    const bool enabled = layer->mask->enabled;
    const auto index = IndexOf(_document.value().layers, id);
    if (!index)
        return;
    auto draft = CanvasDocumentDraft::Clone(_document.value(), _document.value().layers.size());
    if (!draft)
        return;
    draft.value().EditLayers()[index.value()].mask.value().enabled = !enabled;
    auto after = draft.value().Seal();
    if (!after)
        return;
    BeginEdit(enabled ? L"Disable Layer Mask" : L"Enable Layer Mask");
    std::swap(_document.value(), after.value());
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

bool BrushStroke::HealForFinish(std::uint32_t seed)
{
    if (!_detachedFinish || !_finalCoverageComplete || _engine || !_valid || _failed || !Healing() || _mask ||
        _workCancellation.IsCancelled())
        return false;
    // Failure can leave a private prefix of healed tiles. Keep every snapshot path closed until all tiles
    // and selection blending finish; the caller drops the complete draft on any failure.
    _failed = true;
    _finalCoverageComplete = false;
    std::optional<GridRect> painted;
    for (const Tile& tile : _tiles)
    {
        if (_workCancellation.IsCancelled())
            return false;
        if (!tile.touched)
            continue;
        const auto edges = SpotHealing::CoverageBounds(tile.coverage.data(), static_cast<std::size_t>(tile.rect.width),
                                                       static_cast<std::size_t>(tile.rect.height),
                                                       static_cast<std::size_t>(tile.rect.width), _workCancellation);
        if (edges.Empty())
            continue;
        const GridRect rect{static_cast<int>(edges.x0) + tile.rect.x, static_cast<int>(edges.y0) + tile.rect.y,
                            static_cast<int>(edges.x1 - edges.x0), static_cast<int>(edges.y1 - edges.y0)};
        painted = painted ? painted.value().Union(rect) : rect;
    }
    const auto complete = [&]() noexcept
    {
        if (_workCancellation.IsCancelled())
            return false;
        _failed = false;
        _finalCoverageComplete = true;
        return true;
    };
    if (!painted)
        return complete();
    // Same patch-search extent and arithmetic as the compatibility Heal path.
    const auto bounds = painted.value();
    const int reach = static_cast<int>(std::ceil(((std::max)(bounds.width, bounds.height) + 32) * 3.2));
    const GridRect region =
        GridRect{bounds.x - reach, bounds.y - reach, bounds.width + 2 * reach, bounds.height + 2 * reach}.Intersect(
            GridRect{0, 0, _width, _height});
    if (region.Empty())
        return complete();
    // The patch region can exceed the source dimensions on a heavily scaled grid. Validate the
    // kernel's complete supported shape before either large private vector is allocated.
    if (region.width > kMaxCanvasSide || region.height > kMaxCanvasSide ||
        static_cast<std::int64_t>(region.width) * region.height > kMaxLayerPixels)
        return false;
    const auto width = static_cast<std::size_t>(region.width);
    const auto height = static_cast<std::size_t>(region.height);
    std::vector<std::uint8_t> pixels(width * height * 4, 0);
    std::vector<std::uint8_t> coverage(width * height, 0);
    if (_source.HasStorage() &&
        !ReadRasterRegionForWork(_source,
                                 {region.x - _sourceRect.x, region.y - _sourceRect.y, region.width, region.height},
                                 pixels, width * 4, 0, _workCancellation))
        return false;
    for (const Tile& tile : _tiles)
    {
        if (_workCancellation.IsCancelled())
            return false;
        if (!tile.touched)
            continue;
        const auto overlap = tile.rect.Intersect(region);
        for (int y = overlap.y; y < overlap.y + overlap.height; ++y)
        {
            if (_workCancellation.IsCancelled())
                return false;
            std::memcpy(coverage.data() + static_cast<std::size_t>(y - region.y) * width +
                            static_cast<std::size_t>(overlap.x - region.x),
                        tile.coverage.data() +
                            static_cast<std::size_t>(y - tile.rect.y) * static_cast<std::size_t>(tile.rect.width) +
                            static_cast<std::size_t>(overlap.x - tile.rect.x),
                        static_cast<std::size_t>(overlap.width));
        }
    }
    if (!SpotHealing::HealForWork(pixels, coverage, width, height, width * 4, static_cast<float>(_settings.opacity),
                                  _healingMode, seed, _workCancellation))
        return false;
    for (Tile& tile : _tiles)
    {
        if (_workCancellation.IsCancelled())
            return false;
        if (!tile.touched)
            continue;
        const auto count = static_cast<std::size_t>(tile.rect.width) * static_cast<std::size_t>(tile.rect.height) * 4;
        auto output = PixelBuffer::TryAllocate(count);
        if (!output)
            throw std::bad_alloc{};
        if (!ReadRasterRegionForWork(tile.base, {0, 0, tile.rect.width, tile.rect.height},
                                     {output.Data(), output.Size()}, static_cast<std::size_t>(tile.rect.width) * 4, 0,
                                     _workCancellation))
            return false;
        const auto overlap = tile.rect.Intersect(region);
        for (int y = overlap.y; y < overlap.y + overlap.height; ++y)
        {
            if (_workCancellation.IsCancelled())
                return false;
            for (int x = overlap.x; x < overlap.x + overlap.width; ++x)
            {
                const auto* healed =
                    pixels.data() +
                    (static_cast<std::size_t>(y - region.y) * width + static_cast<std::size_t>(x - region.x)) * 4;
                auto* pixel = output.Data() +
                              (static_cast<std::size_t>(y - tile.rect.y) * static_cast<std::size_t>(tile.rect.width) +
                               static_cast<std::size_t>(x - tile.rect.x)) *
                                  4;
                const double clip = _selection ? SelectionAt(_pixelToDocument.Apply(Vec2{x + 0.5, y + 0.5})) : 1.0;
                for (int channel = 0; channel < 4; ++channel)
                    pixel[channel] =
                        static_cast<std::uint8_t>(std::lround(healed[channel] * clip + pixel[channel] * (1.0 - clip)));
            }
        }
        tile.image = Raster::Adopt(tile.rect.width, tile.rect.height, RasterFormat::Bgra8888, std::move(output));
        RefreshTileOwnership(tile);
        if (!tile.image.HasStorage())
            return false;
        tile.changed = true;
    }
    return complete();
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
        RefreshTileOwnership(tile);
        tile.changed = true;
    }
    return true;
}

bool BrushStroke::PaintCanvas(bool withinSource, double opacity, PaintMode mode,
                              const std::function<bool(Vec2, std::array<double, 4>&)>& shade)
{
    if (!_valid || _failed || (_detachedFinish && !_finalCoverageComplete) ||
        (_workerRasterEdit && _workCancellation.IsCancelled()))
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
        const SpotHealing::Bounds bounds =
            SpotHealing::CoverageBounds(_selection->Data(), static_cast<std::size_t>(_selection->Width()),
                                        static_cast<std::size_t>(_selection->Height()),
                                        static_cast<std::size_t>(_selection->Stride()), _workCancellation);
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
    const auto workColumns =
        static_cast<std::size_t>((affected.x + affected.width - 1) / kTileSize - affected.x / kTileSize + 1);
    const auto workRows =
        static_cast<std::size_t>((affected.y + affected.height - 1) / kTileSize - affected.y / kTileSize + 1);
    const std::size_t workTotal = workColumns * workRows;
    std::size_t completedTiles = 0;
    if (_workerRasterEdit && _workProgress)
        _workProgress(_workProgressContext, 0, workTotal);
    for (int ty = affected.y / kTileSize; ty <= (affected.y + affected.height - 1) / kTileSize; ++ty)
    {
        for (int tx = affected.x / kTileSize; tx <= (affected.x + affected.width - 1) / kTileSize; ++tx)
        {
            if (_workerRasterEdit && _workCancellation.IsCancelled())
                return false;
            const std::uint64_t key =
                static_cast<std::uint64_t>(ty) * static_cast<std::uint64_t>(columns) + static_cast<std::uint64_t>(tx);
            if (!AllocateTile(key, _workerRasterEdit && withinSource && mode == PaintMode::DestinationOut))
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
                if (_workerRasterEdit)
                    throw;
                _failed = true;
                return false;
            }
            for (std::size_t index = 0; index < count; ++index)
            {
                if (_workerRasterEdit && index % static_cast<std::size_t>(tile.rect.width) == 0 &&
                    _workCancellation.IsCancelled())
                    return false;
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
            RefreshTileOwnership(tile);
            tile.touched = true;
            tile.changed = true;
            ++completedTiles;
            if (_workerRasterEdit && _workProgress)
                _workProgress(_workProgressContext, completedTiles, workTotal);
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
    if (!std::isfinite(point.x) || !std::isfinite(point.y) || _warpOpening == UINT64_MAX)
        return false;
    if (HasDeferredTransform() || (_gradient && _gradient->deferred) || (_pixelMove && _pixelMove->deferred))
        return false;
    if (!ResolveGradient())
        return false;
    if (!CanPaint() || _maskSelected || HasPendingWarp())
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
    warp->Append(point);
    if (!warp->IsValid())
        return false;
    _warpSource = WarpSource{layer->id, layer->asset.has_value() ? layer->asset->image : Raster{}, layer->transform};
    ++_warpOpening;
    _warp = std::move(warp);
    _lastBrushPoint = LastBrushPoint{point, layer->id, false};
    return true;
}

void DocumentSession::ContinueWarp(Vec2 point)
{
    if (!_warp || !std::isfinite(point.x) || !std::isfinite(point.y))
    {
        return;
    }
    _warp->Append(point);
    if (!_warp->IsValid())
    {
        _lastBrushPoint.reset();
        return;
    }
    if (_lastBrushPoint.has_value())
    {
        _lastBrushPoint->point = point;
    }
}

void DocumentSession::CancelWarp() noexcept
{
    _warp.reset();
    _warpUpdatingOpening.reset();
    _warpUpdatingSequence.reset();
    _warpFinishingOpening.reset();
}

void DocumentSession::FinishWarp()
{
    if (!_warp)
    {
        return;
    }
    const std::shared_ptr<WarpStroke> warp = std::move(_warp);
    _warp.reset();
    if (!warp->IsValid() || warp->Points().empty() || !_document.has_value() || HasDeferredTransform())
    {
        return;
    }
    const ImageLayer* current = FindLayer(_warpSource.layerID);
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
    const bool mask = _gradient ? _gradient->mask : _maskSelected;
    constexpr std::array<double, 4> kBlack{0.0, 0.0, 0.0, 1.0};
    constexpr std::array<double, 4> kWhite{1.0, 1.0, 1.0, 1.0};
    const auto* captured = _gradient && _gradient->capturedInputs ? &_gradient->capturedInputs.value() : nullptr;
    const auto& settings = captured ? captured->settings : gradientSettings;
    const bool white = captured ? captured->maskWhite : maskPaintWhite;
    const std::array<double, 4> foreground =
        mask ? (white ? kWhite : kBlack) : StraightColor(captured ? captured->foreground : _foregroundArgb, 1.0);
    const std::array<double, 4> background =
        mask ? (white ? kBlack : kWhite) : StraightColor(captured ? captured->background : _backgroundArgb, 1.0);
    std::array<std::array<double, 4>, 2> colors{foreground, foreground};
    if (settings.style == GradientStyle::ForegroundToBackground)
    {
        colors[1] = background;
    }
    else
    {
        colors[1][3] = 0.0;
    }
    if (settings.reversed)
    {
        std::swap(colors[0], colors[1]);
    }
    return colors;
}

bool DocumentSession::BeginGradient(Vec2 point)
{
    return BeginGradientImpl(point, false);
}

bool DocumentSession::BeginDeferredGradient(Vec2 point)
{
    return BeginGradientImpl(point, true);
}

bool DocumentSession::BeginDeferredGradient(Vec2 point, GradientEdit::CapturedInputs inputs)
{
    if (!BeginGradientImpl(point, true))
        return false;
    _gradient->capturedInputs = inputs;
    return true;
}

bool DocumentSession::BeginGradientImpl(Vec2 point, bool deferred)
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
    if (_gradient && _gradient->target == layer->id && _gradient->mask == _maskSelected)
    {
        if (_gradient->deferred != deferred)
            return false;
        _gradient->start = point;
        _gradient->end = point;
        RefreshGradient();
        return _gradient.has_value();
    }
    if (HasDeferredTransform() || (_gradient && _gradient->deferred) || (_pixelMove && _pixelMove->deferred))
        return false; // a host must acknowledge settlement before opening on another target
    if (!ResolveGradient())
        return false;
    layer = ActiveLayer();
    if (layer == nullptr || !CanPaint())
    {
        return false;
    }
    const bool mask = _maskSelected;
    std::shared_ptr<BrushStroke> raster;
    if (!deferred)
    {
        raster = MakeRasterEdit(*layer, mask);
        if (!raster)
            return false;
        raster->SetEditName(mask ? L"Gradient Mask" : L"Gradient");
    }
    if (_gradientOpening == (std::numeric_limits<std::uint64_t>::max)())
        return false;
    // Vec2 and a moved shared_ptr cannot throw: no allocation is left to catch (unlike a pixel move's selection
    // copy).
    _gradient = GradientEdit{{}, std::move(raster), point, point, layer->id, mask, deferred, ++_gradientOpening};
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
    RefreshGradient(false); // Pointer motion preserves the admitted opening's captured settings and palette.
}

void DocumentSession::RefreshGradient(bool useLiveInputs)
{
    if (!_gradient)
    {
        return;
    }
    if (useLiveInputs)
        _gradient->capturedInputs.reset(); // An explicit palette/header input after opening follows the live UI.
    if (_gradient->sequence == (std::numeric_limits<std::uint64_t>::max)())
    {
        CancelGradient();
        return;
    }
    ++_gradient->sequence;
    if (_gradient->deferred)
    {
        if (!_gradient->HasLine())
        {
            _gradient->raster.reset(); // a redrag without a line must not keep the previous pixels visible
            _gradient->display.reset();
        }
        return;
    }
    if (!_gradient->HasLine())
        return;
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
    if (!_gradient || _gradient->deferred)
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

bool DocumentSession::PublishGradientPreview(std::shared_ptr<BrushStroke>&& raster, const JobToken& token,
                                             std::uint64_t opening, std::uint64_t sequence,
                                             std::shared_ptr<const PreparedRasterPreview> display) noexcept
{
    if (!_gradient || !_gradient->deferred || !_gradient->HasLine() || _gradient->opening != opening || !token.target ||
        token.generation != _generation || !CanApplyJobResult(token, token.sequence) ||
        _gradient->target != token.target->layerId || _activeLayerID != _gradient->target ||
        _maskSelected != _gradient->mask || sequence == 0 || sequence > _gradient->sequence ||
        sequence <= _gradient->publishedSequence || !raster || !raster->IsValid() ||
        raster->LayerID() != _gradient->target || raster->IsMask() != _gradient->mask)
        return false;
    // Completed CPU producer only: validate cached scalar identities without walking tiles on the owner.
    if (display && (!display->memory || !display->memory->IsBound() || !display->ownershipBytes ||
                    display->target != raster->LayerID() || display->mask != raster->IsMask() ||
                    display->width != raster->Width() || display->height != raster->Height() ||
                    display->transform != raster->PaintTransform() || display->fill != raster->FillValue() ||
                    display->source != raster->SourceImage() || display->sourceRect != raster->SourceRect()))
        return false;
    _gradient->display = std::move(display);
    _gradient->raster = std::move(raster);
    _gradient->publishedSequence = sequence;
    _gradient->publishedGeneration = _generation;
    return true;
}

bool DocumentSession::ApplyGradientIfCurrent(ImageLayer&& prepared, const JobToken& token, std::uint64_t opening,
                                             std::uint64_t sequence)
{
    if (!_gradient || !_gradient->deferred || !_gradient->HasLine() || _gradient->opening != opening ||
        _gradient->sequence != sequence || !token.target || _gradient->target != token.target->layerId)
        return false;
    const bool mask = _gradient->mask;
    return ApplyRasterEditImpl(std::move(prepared), mask, mask ? L"Gradient Mask" : L"Gradient", token, true);
}

bool DocumentSession::ResolveGradient()
{
    if (HasDeferredTransform() || (_gradient && _gradient->deferred))
        return false;
    if (_gradient)
    {
        CommitGradient();
    }
    return true;
}

void DocumentSession::BeginShape(Vec2 point)
{
    BeginShape(point, shapeKind, shapeCornerRadius);
}

void DocumentSession::BeginShape(Vec2 point, ShapeKind kind, double cornerRadius)
{
    if (!_document.has_value() || _transformEdit.has_value() || !std::isfinite(point.x) || !std::isfinite(point.y))
    {
        return;
    }
    const Vec2 anchor{std::round(point.x), std::round(point.y)};
    ++_shapeOpening;
    _shapeDraft = ShapeDraft{kind, anchor, SelectionBounds{anchor.x, anchor.y, anchor.x, anchor.y},
                             kind == ShapeKind::Rectangle ? std::max(0.0, cornerRadius) : 0.0};
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
    if (_shapeDraft)
        ++_shapeOpening;
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
    ++_shapeOpening;
    _shapeDraft.reset();
    CommitShape(draft, _foregroundArgb);
}

void DocumentSession::CommitShape(const ShapeDraft& draft, std::uint32_t foregroundArgb)
{
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
    const Raster image = ShapeImage(draft.kind, pixelWidth, pixelHeight, foregroundArgb, draft.cornerRadius);
    if (!image.HasStorage())
    {
        return;
    }
    const std::array<double, 4> color = StraightColor(foregroundArgb, 1.0);
    const LayerShapeStyle style{draft.kind, color[0], color[1], color[2], draft.cornerRadius};
    AddPixelLayer(image, Vec2{draft.rect.minX, draft.rect.minY}, NextShapeName(draft.kind),
                  draft.kind == ShapeKind::Rectangle ? L"Rectangle" : L"Ellipse", false, style);
}

void DocumentSession::AddPixelLayer(const Raster& image, Vec2 origin, std::wstring name, const std::wstring& editName,
                                    bool dropsSelection, std::optional<LayerShapeStyle> shape)
{
    if (HasDeferredTransform() || (_gradient && _gradient->deferred) || (_pixelMove && _pixelMove->deferred))
        return;
    if (!ResolveGradient())
        return;
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
    layer.asset = RasterAsset{image, {}, std::move(name)};
    layer.transform.origin = origin;
    layer.shape = shape;
    if (shape.has_value())
    {
        layer.shapeImage = image;
    }
    InsertLayerAboveActive(std::move(layer), editName, dropsSelection);
}

void DocumentSession::RedrawShape(ImageLayer& layer, std::span<const ImageLayer> records)
{
    if (HasDeferredTransform())
        return;

    if (!_document.has_value())
    {
        return;
    }
    const std::optional<LayerShapeStyle> shape = LiveShape(layer);
    if (!shape.has_value() || !layer.asset.has_value())
    {
        return;
    }
    const int width = std::max(1, static_cast<int>(std::lround(layer.transform.size.x)));
    const int height = std::max(1, static_cast<int>(std::lround(layer.transform.size.y)));
    std::int64_t imageLeft = kMaxLayerPixels;
    for (const auto& record : records)
        if (record.id != layer.id && record.asset)
            imageLeft -= record.asset->image.PixelCount();
    // Past the document's budget (Decision D7) the shape keeps its pixels, stretched, instead of redrawing.
    if ((width == layer.asset->image.Width() && height == layer.asset->image.Height()) ||
        static_cast<std::int64_t>(width) * static_cast<std::int64_t>(height) > kMaxShapePixels ||
        static_cast<std::int64_t>(width) * static_cast<std::int64_t>(height) > std::max<std::int64_t>(0, imageLeft))
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
    layer.asset = RasterAsset{image, {}, name};
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

bool BrushStroke::LiftSelection(bool* noPixels)
{
    if (noPixels)
        *noPixels = false;
    if (!_valid || _failed || _mask || !_source.HasStorage() || !_selection.has_value() ||
        _selection->Data() == nullptr || (_workerRasterEdit && _workCancellation.IsCancelled()))
    {
        return false;
    }
    const SpotHealing::Bounds bounds =
        SpotHealing::CoverageBounds(_selection->Data(), static_cast<std::size_t>(_selection->Width()),
                                    static_cast<std::size_t>(_selection->Height()),
                                    static_cast<std::size_t>(_selection->Stride()), _workCancellation);
    if (bounds.Empty())
    {
        if (noPixels && !_workCancellation.IsCancelled())
            *noPixels = true;
        return false;
    }
    const GridRect region =
        MappedBox(_pixelToDocument.Inverted(), static_cast<double>(bounds.x0), static_cast<double>(bounds.y0),
                  static_cast<double>(bounds.x1), static_cast<double>(bounds.y1))
            .Intersect(_sourceRect);
    if (region.Empty())
    {
        if (noPixels)
            *noPixels = true;
        return false;
    }
    std::vector<std::uint8_t> bytes;
    try
    {
        bytes.assign(static_cast<std::size_t>(region.width) * static_cast<std::size_t>(region.height) * 4u, 0);
    }
    catch (const std::bad_alloc&)
    {
        if (_workerRasterEdit)
            throw;
        return false;
    }
    // The layer's pixels under the selection, faded by its coverage.
    if (_workerRasterEdit)
    {
        if (!ReadRasterRegionForWork(_source,
                                     {region.x - _sourceRect.x, region.y - _sourceRect.y, region.width, region.height},
                                     bytes, static_cast<std::size_t>(region.width) * 4, 0, _workCancellation))
            return false;
    }
    else
        _source.ReadRegion(region.x - _sourceRect.x, region.y - _sourceRect.y, region.width, region.height,
                           bytes.data(), region.width * 4, 0);
    if (_workerRasterEdit && _workProgress)
        _workProgress(_workProgressContext, 0, static_cast<std::size_t>(region.height));
    for (int y = 0; y < region.height; ++y)
    {
        if (_workerRasterEdit && _workCancellation.IsCancelled())
            return false;
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
        if (_workerRasterEdit && _workProgress)
            _workProgress(_workProgressContext, static_cast<std::size_t>(y + 1),
                          static_cast<std::size_t>(region.height));
    }
    if (_workerRasterEdit && _workCancellation.IsCancelled())
        return false;
    const Raster image = Raster::Make(region.width, region.height, RasterFormat::Bgra8888, bytes.data(), bytes.size());
    if (!image.HasStorage())
    {
        return false;
    }
    _lifted = Lifted{image, region};
    return true;
}

std::optional<BrushStrokeOpening> BrushStroke::CaptureLiftedOpening() const noexcept
{
    if (!_valid || _failed || _mask || !_workerRasterEdit || _engine != &CpuBrushCoverage::Shared() ||
        _workCancellation.IsCancelled() || !_source.HasStorage() || !_selection.has_value() || !_lifted.has_value())
        return std::nullopt;

    const Raster& coverage = _selection.value();
    const Raster& lifted = _lifted->image;
    if (!coverage.HasStorage() || coverage.Format() != RasterFormat::Gray8 || coverage.Width() != _canvasWidth ||
        coverage.Height() != _canvasHeight || !lifted.HasStorage() || lifted.Format() != RasterFormat::Bgra8888 ||
        lifted.Width() != _lifted->rect.width || lifted.Height() != _lifted->rect.height || _lifted->rect.Empty() ||
        _sourceRect.Empty() || _width < 1 || _height < 1)
        return std::nullopt;

    // MoveLifted changes only mutable tile/landing state. The opening handles above remain the exact immutable
    // inputs to the existing computation, so a post-move capture is safe and does not retain those mutable buffers.
    std::uint64_t retainedBytes = static_cast<std::uint64_t>(sizeof(BrushStrokeOpening));
    const auto addRasterBound = [&retainedBytes](const Raster& raster) noexcept
    {
        const std::uint64_t bytes = raster.RetirementMemoryBytes();
        if (bytes == 0 || bytes > std::numeric_limits<std::uint64_t>::max() - retainedBytes)
            return false;
        retainedBytes += bytes;
        return true;
    };
    if (!addRasterBound(_source) || !addRasterBound(coverage) || !addRasterBound(lifted))
        return std::nullopt;

    BrushStrokeOpening opening;
    opening._source = _source;
    opening._coverage = coverage;
    opening._lifted = lifted;
    opening._sourceRect = _sourceRect;
    opening._liftedRect = _lifted->rect;
    opening._pixelToDocument = _pixelToDocument;
    opening._sourceTransform = _sourceTransform;
    opening._paintTransform = _paintTransform;
    opening._width = _width;
    opening._height = _height;
    opening._canvasWidth = _canvasWidth;
    opening._canvasHeight = _canvasHeight;
    opening._retirementMemoryBytes = retainedBytes;
    return std::optional<BrushStrokeOpening>{std::move(opening)};
}

bool BrushStroke::UseLiftedOpening(const BrushStrokeOpening& opening) noexcept
{
    // This installs only the immutable lift inputs. In particular it does not copy worker progress/cancellation,
    // mutable tiles, coverage-engine state or a previous move's landing state.
    if (!_valid || _failed || _mask || !_workerRasterEdit || _engine != &CpuBrushCoverage::Shared() ||
        _workCancellation.IsCancelled() || !_tiles.empty() || !_samples.empty() || !_tailKeys.empty() ||
        !_index.empty() || _allocated.has_value() || _selection.has_value() || _sample.has_value() ||
        _lifted.has_value() || _moved.has_value() || !_landingTiles.empty() || !_source.HasStorage() ||
        _sourceRect != opening._sourceRect || _source != opening._source || _width != opening._width ||
        _height != opening._height || _canvasWidth != opening._canvasWidth || _canvasHeight != opening._canvasHeight ||
        _pixelToDocument != opening._pixelToDocument || _sourceTransform != opening._sourceTransform ||
        _paintTransform != opening._paintTransform || opening._retirementMemoryBytes == 0 ||
        !opening._coverage.HasStorage() || !opening._lifted.HasStorage())
        return false;

    _selection = opening._coverage;
    _lifted = Lifted{opening._lifted, opening._liftedRect};
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
            if (_workerRasterEdit && _workCancellation.IsCancelled())
                return false;
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
    if (!_lifted.has_value() || !_selection.has_value() || (_workerRasterEdit && _workCancellation.IsCancelled()))
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
    std::size_t completedTiles = 0;
    if (_workerRasterEdit && _workProgress)
        _workProgress(_workProgressContext, 0, keys.size());
    for (const std::uint64_t key : keys)
    {
        if (_workerRasterEdit && _workCancellation.IsCancelled())
            return false;
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
            if (_workerRasterEdit && index % static_cast<std::size_t>(tile.rect.width) == 0 &&
                _workCancellation.IsCancelled())
                return false;
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
        RefreshTileOwnership(tile);
        tile.touched = true;
        tile.changed = true;
        if (_workerRasterEdit && _workProgress)
            _workProgress(_workProgressContext, ++completedTiles, keys.size());
    }
    if (_workerRasterEdit && _workCancellation.IsCancelled())
        return false;
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
    if (HasDeferredTransform() || (_gradient && _gradient->deferred) || (_pixelMove && _pixelMove->deferred))
        return false;
    // Pixel edits apply a pending gradient first (Decision D3; legacy refuses them while one is pending).
    if (!ResolveGradient())
        return false;
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
    if (HasDeferredTransform() || (_gradient && _gradient->deferred) || (_pixelMove && _pixelMove->deferred))
        return false;
    if (!ResolveGradient())
        return false;
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

bool DocumentSession::ApplyRasterEditIfCurrent(ImageLayer&& prepared, bool mask, std::wstring name,
                                               const JobToken& token)
{
    return ApplyRasterEditImpl(std::move(prepared), mask, std::move(name), token, false);
}

bool DocumentSession::ApplyRasterEditImpl(ImageLayer&& prepared, bool mask, std::wstring name, const JobToken& token,
                                          bool gradientCommit, bool pixelMoveCommit, DocumentSelection* movedSelection,
                                          PreparedRasterEdit* publication)
{
    if (!token.target || token.generation != _generation || !CanApplyJobResult(token, token.sequence) || _brush ||
        HasPendingWarp() || (_pixelMove && !pixelMoveCommit) || _transformEdit || (_gradient && !gradientCommit) ||
        HasColorEdit() || _activeLayerID != token.target->layerId || _maskSelected != mask)
        return false;
    if (pixelMoveCommit && (!_pixelMove || !_pixelMove->deferred || mask || !movedSelection || !_document->selection))
        return false;
    const ImageLayer* const current = FindLayer(token.target->layerId);
    if (!current || prepared.id != current->id || prepared.name != current->name ||
        prepared.visible != current->visible || prepared.parentID != current->parentID ||
        prepared.isGroup != current->isGroup || prepared.opacity != current->opacity ||
        prepared.blendMode != current->blendMode || prepared.maskSourceID != current->maskSourceID ||
        prepared.adjustment != current->adjustment || prepared.shape != current->shape ||
        prepared.shapeImage != current->shapeImage || prepared.mask.has_value() != current->mask.has_value() ||
        !prepared.transform.IsValid())
        return false;
    const auto validRaster = [](const Raster& raster, RasterFormat format) noexcept
    {
        return raster.HasStorage() && raster.Format() == format && raster.Width() > 0 && raster.Height() > 0 &&
               raster.Width() <= kMaxCanvasSide && raster.Height() <= kMaxCanvasSide &&
               raster.PixelCount() <= kMaxLayerPixels;
    };
    if (prepared.mask)
    {
        const auto& before = current->mask.value();
        const auto& after = prepared.mask.value();
        if (after.enabled != before.enabled || after.linked != before.linked || after.placement != before.placement ||
            after.asset.name != before.asset.name || !validRaster(after.asset.image, RasterFormat::Gray8) ||
            !validRaster(after.asset.thumbnail, RasterFormat::Gray8) ||
            after.asset.image.PixelCount() > PixelBudgetLeft(true, {current->id}))
            return false;
    }
    if (mask)
    {
        if (!prepared.mask || prepared.asset != current->asset || prepared.transform != current->transform)
            return false;
    }
    else if (prepared.isGroup || prepared.adjustment || !prepared.asset ||
             !validRaster(prepared.asset->image, RasterFormat::Bgra8888) ||
             (prepared.asset->thumbnail.HasStorage() &&
              !validRaster(prepared.asset->thumbnail, RasterFormat::Bgra8888)) ||
             prepared.asset->name != prepared.name ||
             prepared.asset->image.PixelCount() > PixelBudgetLeft(false, {current->id}))
        return false;
    if (publication)
    {
        if (!publication->_document || !history.CanPublish(publication->_history) ||
            !history.Publish(publication->_history))
            return false;
        // Every operation after validation is nonthrowing. The caller's aggregate now retains the old layer,
        // selection and history containers until its worker release; no destructor walks a large graph here.
        std::swap(_document, publication->_document);
        publication->_applied = true;
        std::swap(_lastBrushCommit, publication->_retiredBrushCommit);
        if (gradientCommit)
            std::swap(_gradient, publication->_retiredGradient);
        if (pixelMoveCommit)
            std::swap(_pixelMove, publication->_retiredPixelMove);
        NoteDocumentMutation();
    }
    else
    {
        const auto index = IndexOf(_document.value().layers, prepared.id);
        if (!index)
            return false;
        auto draft = CanvasDocumentDraft::Clone(_document.value(), _document.value().layers.size());
        if (!draft)
            return false;
        draft.value().EditLayers()[index.value()] = std::move(prepared);
        if (pixelMoveCommit)
            draft.value().DocumentState().selection = std::move(*movedSelection);
        auto after = draft.value().Seal();
        if (!after)
            return false;
        BeginEdit(std::move(name));
        std::swap(_document.value(), after.value());
        _lastBrushCommit.reset(); // an older gradient/stroke preview cannot be adopted for this prepared edit
        EndEdit();
    }
    if (gradientCommit)
        _gradient.reset();
    if (pixelMoveCommit)
        _pixelMove.reset();
    return true;
}

void DocumentSession::DeleteLayerMask()
{
    if (HasDeferredTransform() || (_gradient && _gradient->deferred) || (_pixelMove && _pixelMove->deferred))
        return;
    if (!ResolveGradient())
        return;
    if (!_activeLayerID.has_value())
    {
        return;
    }
    const ImageLayer* layer = FindLayer(_activeLayerID.value());
    if (layer == nullptr || !layer->mask.has_value())
    {
        return;
    }
    const auto index = IndexOf(_document.value().layers, layer->id);
    if (!index)
        return;
    auto draft = CanvasDocumentDraft::Clone(_document.value(), _document.value().layers.size());
    if (!draft)
        return;
    draft.value().EditLayers()[index.value()].mask.reset();
    auto after = draft.value().Seal();
    if (!after)
        return;
    BeginEdit(L"Delete Layer Mask");
    std::swap(_document.value(), after.value());
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
    if (!_document.has_value() || _brush || HasPendingWarp() || _pixelMove)
    {
        return false;
    }
    const ImageLayer* layer = ActiveLayer();
    if (layer == nullptr || (layer->isGroup && !_maskSelected) || !EffectivelyVisible(_document->layers, layer->id))
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
    if (HasDeferredTransform() || (_gradient && _gradient->deferred) || (_pixelMove && _pixelMove->deferred))
        return;
    if (!CanInvert())
    {
        return;
    }
    // Invert is available in every tool: a pending transform or gradient is applied first.
    CommitTransform();
    if (!ResolveGradient())
        return;
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
    return BeginPixelMoveImpl(duplicate, false);
}

bool DocumentSession::BeginDeferredPixelMove(bool duplicate)
{
    return BeginPixelMoveImpl(duplicate, true);
}

bool DocumentSession::BeginPixelMoveImpl(bool duplicate, bool deferred)
{
    if (HasDeferredTransform() || (_gradient && _gradient->deferred) || (_pixelMove && _pixelMove->deferred))
        return false;
    if (!ResolveGradient())
        return false;
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
    if (_pixelMoveOpening == (std::numeric_limits<std::uint64_t>::max)())
        return false;
    std::shared_ptr<BrushStroke> raster;
    if (!deferred)
    {
        raster = MakeRasterEdit(*layer, false);
        if (!raster || !raster->LiftSelection())
            return false;
        raster->SetEditName(duplicate ? L"Duplicate Pixels" : L"Move Pixels");
    }
    // Pixel and immutable selection handles are shared with the document until the move is committed.
    _pixelMove = PixelMove{{},        std::move(raster), _document->selection.value(), duplicate, Vec2{0, 0},
                           layer->id, deferred,          ++_pixelMoveOpening};
    _pixelMove->openingGeneration = _generation;
    return true;
}

void DocumentSession::MovePixels(double dx, double dy)
{
    if (!_pixelMove || !std::isfinite(dx) || !std::isfinite(dy))
    {
        return;
    }
    const Vec2 rounded{std::round(dx), std::round(dy)};
    if (_pixelMove->deferred)
    {
        if (_pixelMove->openingGeneration != _generation)
        {
            CancelPixelMove();
            return;
        }
        if (rounded == _pixelMove->offset || _pixelMove->sequence == (std::numeric_limits<std::uint64_t>::max)())
            return;
        _pixelMove->offset = rounded;
        ++_pixelMove->sequence;
        if (!_pixelMove->HasOffset())
        {
            _pixelMove->raster.reset(); // an older worker cannot resurrect a zero-offset preview
            _pixelMove->display.reset();
        }
        return;
    }
    if (!_pixelMove->raster->MoveLifted(rounded, _pixelMove->duplicate))
    {
        CancelPixelMove();
        return;
    }
    _pixelMove->offset = rounded;
}

SelectionDisplayValue DocumentSession::DisplayedSelectionValue() const
{
    if (_pixelMove && !HasDeferredTransform())
        return {_pixelMove->origin, _pixelMove->offset};
    return {DisplayedSelection(), {}};
}

std::optional<DocumentSelection> DocumentSession::DisplayedSelection() const
{
    if (HasDeferredTransform())
    {
        const auto* display = DisplayDocument();
        return display ? display->selection : std::nullopt;
    }
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
    if (!_pixelMove || _pixelMove->deferred)
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

bool DocumentSession::PublishPixelMovePreview(std::shared_ptr<BrushStroke>&& stroke, const JobToken& token,
                                              std::uint64_t opening, std::uint64_t sequence,
                                              std::shared_ptr<const PreparedRasterPreview> display) noexcept
{
    if (!_pixelMove || !_pixelMove->deferred || !_pixelMove->HasOffset() || _pixelMove->opening != opening ||
        _pixelMove->openingGeneration != _generation || sequence <= _pixelMove->publishedSequence ||
        sequence > _pixelMove->sequence || !token.target || token.target->layerId != _pixelMove->target ||
        token.generation != _generation || !CanApplyJobResult(token, token.sequence) ||
        _activeLayerID != _pixelMove->target || _maskSelected || !stroke || !stroke->IsValid() ||
        stroke->LayerID() != _pixelMove->target || stroke->IsMask())
        return false;
    // Completed CPU producer only: validate cached scalar identities without walking tiles on the owner.
    if (display && (!display->memory || !display->memory->IsBound() || !display->ownershipBytes ||
                    display->target != stroke->LayerID() || display->mask != stroke->IsMask() ||
                    display->width != stroke->Width() || display->height != stroke->Height() ||
                    display->transform != stroke->PaintTransform() || display->fill != stroke->FillValue() ||
                    display->source != stroke->SourceImage() || display->sourceRect != stroke->SourceRect()))
        return false;
    _pixelMove->display = std::move(display);
    _pixelMove->raster = std::move(stroke);
    _pixelMove->publishedSequence = sequence;
    _pixelMove->publishedGeneration = _generation;
    return true;
}

bool DocumentSession::ApplyPixelMoveIfCurrent(ImageLayer&& prepared, DocumentSelection&& movedSelection,
                                              const JobToken& token, std::uint64_t opening, std::uint64_t sequence)
{
    if (!_pixelMove || !_pixelMove->deferred || !_pixelMove->HasOffset() || _pixelMove->opening != opening ||
        _pixelMove->openingGeneration != _generation || _pixelMove->sequence != sequence || !token.target ||
        token.target->layerId != _pixelMove->target || movedSelection.antialiased != _pixelMove->origin.antialiased ||
        movedSelection.loops.PointCount() != _pixelMove->origin.loops.PointCount())
        return false;
    // Prepared output comes from the trusted move graph. Its cached bounds independently guard accidental
    // coordinate-grid confusion without walking a potentially large outline on the owner.
    const auto before = _pixelMove->origin.Bounds();
    const auto after = movedSelection.Bounds();
    if (!before || !after || after->minX != before->minX + _pixelMove->offset.x ||
        after->maxX != before->maxX + _pixelMove->offset.x || after->minY != before->minY + _pixelMove->offset.y ||
        after->maxY != before->maxY + _pixelMove->offset.y)
        return false;
    return ApplyRasterEditImpl(std::move(prepared), false, _pixelMove->duplicate ? L"Duplicate Pixels" : L"Move Pixels",
                               token, false, true, &movedSelection);
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
    if (HasDeferredTransform() || (_gradient && _gradient->deferred) || (_pixelMove && _pixelMove->deferred))
        return;
    if (!ResolveGradient())
        return;
    if (!_document.has_value() || !_document->selection.has_value())
    {
        AddLayerMask(revealing);
        return;
    }
    if (_transformEdit.has_value() || !_activeLayerID.has_value())
    {
        return;
    }
    const ImageLayer* layer = FindLayer(_activeLayerID.value());
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
    const auto index = IndexOf(_document.value().layers, layer->id);
    if (!index)
        return;
    auto draft = CanvasDocumentDraft::Clone(_document.value(), _document.value().layers.size());
    if (!draft)
        return;
    draft.value().EditLayers()[index.value()].mask =
        LayerMask{RasterAsset{mask, Raster::MakeThumbnail(mask), layer->name}, true, std::nullopt, true};
    draft.value().DocumentState().selection.reset();
    auto after = draft.value().Seal();
    if (!after)
        return;
    BeginEdit(L"Add Mask from Selection");
    std::swap(_document.value(), after.value());
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
    if (HasDeferredTransform() || (_gradient && _gradient->deferred) || (_pixelMove && _pixelMove->deferred))
        return;
    // Before the snapshot Escape restores, so a pending gradient is never part of what a cancel undoes.
    if (!ResolveGradient())
        return;
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
    floating.asset = RasterAsset{lifted->image, {}, L"Floating Selection"};
    floating.transform.origin = lifted->origin;
    floating.parentID = source.parentID;
    floating.opacity = source.opacity;
    floating.blendMode = source.blendMode;
    TransformEdit edit;
    edit.layerID = floating.id;
    edit.draft = floating.transform;
    edit.persistent = true;
    edit.floating = FloatingTransform{
        source.id, before, beforeActive, floating.transform, lifted->image.Width(), lifted->image.Height()};
    const Guid floatingId = floating.id;
    auto draft = CanvasDocumentDraft::Clone(_document.value(), _document.value().layers.size() + 1);
    if (!draft || !draft.value().Insert(index.value() + 1, std::move(floating)))
    {
        _document = *before;
        SetActiveLayerID(beforeActive);
        EndEdit();
        return;
    }
    auto after = draft.value().Seal();
    if (!after)
    {
        _document = *before;
        SetActiveLayerID(beforeActive);
        EndEdit();
        return;
    }
    std::swap(_document.value(), after.value());
    SetActiveLayerID(floatingId);
    _maskSelected = false;
    _transformEdit = std::move(edit);
    NoteDocumentMutation();
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
        const auto& layers = _document->layers;
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
        auto draft = CanvasDocumentDraft::Clone(_document.value(), layers.size());
        if (!draft || !draft.value().Erase(floatingIndex.value()))
            break;
        const std::optional<std::size_t> target = IndexOf(draft.value().ReadLayers(), floating.sourceID);
        if (!target.has_value())
        {
            break;
        }
        {
            ImageLayer& installed = draft.value().EditLayers()[target.value()];
            installed.asset = RasterAsset{image, {}, name};
            installed.transform = mergedTransform;
            installed.mask = mask;
        }
        draft.value().DocumentState().selection = moved;
        auto after = draft.value().Seal();
        if (!after)
            break;
        std::swap(_document.value(), after.value());
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
    if (_gradient && _gradient->deferred)
        return false;
    // The copy takes the pixels the canvas shows, a pending gradient applied.
    if (!ResolveGradient())
        return false;
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
    ++_clipboardSerial;
    return true;
}

bool DocumentSession::CopyMergedSelection(const Raster& composite)
{
    if (HasDeferredTransform())
        return false;

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
    ++_clipboardSerial;
    return true;
}

void DocumentSession::CutSelection()
{
    if (HasDeferredTransform() || (_gradient && _gradient->deferred) || (_pixelMove && _pixelMove->deferred))
        return;
    if (!ResolveGradient())
        return;
    if (!_document.has_value() || !_document->selection.has_value() || !CanCopyPixels() || !CanPaint())
    {
        return;
    }
    const std::optional<PixelClipboard> previous = _clipboard;
    const auto previousSerial = _clipboardSerial;
    if (!CopySelection())
    {
        return;
    }
    if (!ClearSelectedPixels())
    {
        _clipboard = previous;
        _clipboardSerial = previousSerial;
    }
}

void DocumentSession::Paste()
{
    if (HasDeferredTransform())
        return;

    if (!CanPaste())
    {
        return;
    }
    const PixelClipboard clip = _clipboard.value();
    AddPixelLayer(clip.image, clip.origin, NextLayerName(), L"Paste", true, std::nullopt);
}

void DocumentSession::PasteImage(const Raster& image)
{
    if (HasDeferredTransform())
        return;

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
    if (HasDeferredTransform() || (_gradient && _gradient->deferred) || (_pixelMove && _pixelMove->deferred))
        return;
    if (!ResolveGradient())
        return;
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

namespace
{
#pragma warning(push)
#pragma warning(disable : 4324) // SLIST node requires 16-byte alignment.
struct RetiredBrushOwner final : DeferredReleaseNode
{
    std::unique_ptr<BrushStroke> stroke;
};
#pragma warning(pop)

void DestroyRetiredBrushOwner(DeferredReleaseNode* node) noexcept
{
    const std::unique_ptr<RetiredBrushOwner> owner(static_cast<RetiredBrushOwner*>(node));
    // The graphics borrow ended before publication to the retirement consumer. The whole tile/vector/hash
    // graph and all retained rasters are destroyed here, before this owner's admission acknowledgement.
    assert(!owner->stroke->HasCoverageEngineForTest());
}

void ReleasePrivateBrushOwner(RetiredBrushOwner* raw) noexcept
{
    std::unique_ptr<RetiredBrushOwner> owner(raw);
    owner->stroke->ReleaseCoverageEngine();
    const std::uint64_t bytes = owner->stroke->RetirementMemoryBytes();
    // MakeForWork accepts only complete cached source graphs. Its worker opening produces bounded contiguous
    // selection/sample rasters; every tile producer maintains this complete cache. Losing that invariant is
    // a producer bug, never permission to publish an unknown or invented retirement charge.
    if (bytes == 0 || bytes > (std::numeric_limits<std::uint64_t>::max)() - sizeof(RetiredBrushOwner) - 128)
        std::terminate();
    owner->bytes = bytes + sizeof(RetiredBrushOwner) + 128;
    owner->destroy = &DestroyRetiredBrushOwner;
    if (TryDeferRelease(*owner, DeferredReleasePolicy::AlwaysOnOwner))
        (void)owner.release();
}
} // namespace

std::shared_ptr<BrushStroke> BrushStroke::MakeForWork(const ImageLayer& layer, bool mask, const BrushSettings& settings,
                                                      int canvasWidth, int canvasHeight, BrushKind kind,
                                                      SpotHealingMode healing)
{
    // Worker only. Allocation failures propagate to the original admitted job's bad_alloc boundary.
    // No GPU/context is constructed or borrowed here.
    const Raster source =
        mask ? (layer.mask ? layer.mask->asset.image : Raster{}) : (layer.asset ? layer.asset->image : Raster{});
    if (source.HasStorage() && source.RetirementMemoryBytes() == 0)
        return nullptr;
    auto owner = std::make_unique<RetiredBrushOwner>();
    owner->stroke =
        std::make_unique<BrushStroke>(layer, mask, settings, canvasWidth, canvasHeight, nullptr, kind, healing);
    if (!owner->stroke->IsValid() || !owner->stroke->RetirementMemoryBytes())
        return nullptr;
    owner->stroke->_privateWorkerOwner = true;
    BrushStroke* const draft = owner->stroke.get();
    // shared_ptr construction invokes its supplied deleter if allocating the control block fails. Transfer
    // exactly once before that construction; retaining the unique_ptr would double-destroy on that path.
    RetiredBrushOwner* const transferred = owner.release();
    return std::shared_ptr<BrushStroke>(draft, [transferred](BrushStroke*) noexcept
                                        { ReleasePrivateBrushOwner(transferred); });
}

bool BrushStroke::AttachCoverageForOpening(BrushCoverageEngine* engine) noexcept
{
    if (!_valid || _failed || _detachedFinish || _workerRasterEdit || _engine != &CpuBrushCoverage::Shared() ||
        !_samples.empty() || !_tiles.empty() || !_finalCoverageOrder.empty())
        return false;
    ReleaseCoverageEngine();
    _engine = engine != nullptr ? engine : &CpuBrushCoverage::Shared();
    _usesCpuPermanent = _engine->UsesCpuPermanent();
    _coverageOwner = std::this_thread::get_id();
    _engine->BeginStroke();
    return true;
}

bool BrushStroke::RetainOpeningMemory(SharedRetainedMemoryLease&& lease) noexcept
{
    if (!_privateWorkerOwner || !_valid || _failed || _openingMemory || !lease || !_engine ||
        (_engine != &CpuBrushCoverage::Shared() && _coverageOwner != std::this_thread::get_id()) || !_samples.empty() ||
        !_tiles.empty())
        return false;
    _openingMemory = std::move(lease);
    return true;
}

bool BrushStroke::BeginWarpReplayForWork(std::shared_ptr<const PreparedWarpPreview> preview, bool computeCoverage,
                                         CancelToken cancellation)
{
    if (!_privateWorkerOwner || !_valid || _failed || _mask || _kind != BrushKind::Paint ||
        _engine != &CpuBrushCoverage::Shared() || _workerRasterEdit || _detachedFinish || _detachedSample ||
        !_tiles.empty() || !_samples.empty() || _sample || _warpSample || !preview ||
        preview->ownerThread == std::this_thread::get_id() || preview->source.width != _canvasWidth ||
        preview->source.height != _canvasHeight || cancellation.IsCancelled())
        return false;
    auto reader = std::make_unique<WarpPreviewReader>(preview);
    if (!reader->IsValidForWork() || !reader->RetirementMemoryBytes())
        return false;
    ReleaseCoverageEngine(); // The worker factory's CPU engine has no graphics state.
    _warpSample = std::move(reader);
    _warpReplayOwner = preview->ownerThread;
    _usesCpuPermanent = !computeCoverage;
    _workerRasterEdit = true;
    _warpReplay = true;
    _replaces = true;
    _workCancellation = std::move(cancellation);
    return true;
}

std::optional<BrushStroke::SampleCoverage> BrushStroke::PrepareWarpReplaySampleForWork(Vec2 point,
                                                                                       CancelToken cancellation)
{
    if (!_warpReplay || !_warpSample || _engine || !_valid || _failed || _detachedFinish ||
        _warpReplayOwner == std::this_thread::get_id() || _sampleSequence == UINT64_MAX ||
        (_sampleCoveragePrepared && !_sampleCoverageComplete) || !std::isfinite(point.x) || !std::isfinite(point.y))
        return std::nullopt;
    if (cancellation.IsCancelled())
    {
        _failed = true;
        return std::nullopt;
    }
    _detachedSample = true;
    _sampleCoveragePrepared = false;
    _sampleCoverageComplete = false;
    ++_sampleSequence;
    // The unchanged scalar Append ignores finite centers beyond 1e7. Preserve a previously painted prefix,
    // without replacing its last-four curve controls, and still acknowledge this accepted replay phase.
    if (std::abs(point.x) > 1e7 || std::abs(point.y) > 1e7)
    {
        _workCancellation = std::move(cancellation);
        _sampleCoveragePrepared = true;
        SampleCoverage noop;
        noop.sequence = _sampleSequence;
        noop.params = KernelParams();
        return noop;
    }
    _samplePoint = point;
    return PrepareSampleCoverage(std::move(cancellation));
}

std::optional<BrushStroke::FinalCoverage> BrushStroke::PrepareWarpReplayFinishForWork(CancelToken cancellation)
{
    if (!_warpReplay || !_warpSample || _engine || !_valid || _failed || _detachedFinish ||
        _warpReplayOwner == std::this_thread::get_id() || (_sampleCoveragePrepared && !_sampleCoverageComplete))
        return std::nullopt;
    _detachedSample = false;
    _detachedFinish = true;
    return PrepareFinalCoverage(std::move(cancellation));
}
