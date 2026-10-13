#include "Selection.h"
#include "BoundedLassoGesture.h"

#include "CanvasSize.h"
#include "RasterWork.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <memory>
#include <new>
#include <span>
#include <utility>

#pragma warning(push)
#pragma warning(disable : 4127 4244 4245 4267 4389 4456 4458 4701 4702 4996 5219 5262)
#include <clipper2/clipper.h>
#pragma warning(pop)

namespace
{
constexpr double kPi = 3.14159265358979323846;
constexpr int kClipperPrecision = 2; // hundredths of a document pixel
// Outlines with more pixel edges than this are refused: the path would be too slow to draw (legacy limit).

class WandProgress final
{
  public:
    WandProgress(CancelToken cancellation, MagicWand::WorkObserver observer) noexcept
        : _cancellation(cancellation), _observer(observer)
    {
    }
    // Keep token/observer call state outside the tight scalar pixel loops, including its register live ranges.
    [[nodiscard]] __declspec(noinline) bool Checkpoint(MagicWand::WorkStage stage,
                                                       std::size_t offset = 0) const noexcept
    {
        if (_cancellation.IsCancelled())
            return false;
        if (_observer.checkpoint)
            _observer.checkpoint(_observer.context, stage, offset);
        return !_cancellation.IsCancelled();
    }
    [[nodiscard]] bool Step(MagicWand::WorkStage stage, std::size_t offset) const noexcept
    {
        return offset % Quantum != 0 || Checkpoint(stage, offset);
    }
    [[nodiscard]] static std::size_t Remaining(std::size_t offset) noexcept
    {
        return Quantum - offset % Quantum;
    }
    static constexpr std::size_t Quantum = 4096;

  private:
    CancelToken _cancellation;
    MagicWand::WorkObserver _observer;
};

class WandPixels final
{
  public:
    [[nodiscard]] const std::uint8_t* Read(const Raster& image, CancelToken cancellation) noexcept
    {
        if (cancellation.IsCancelled())
            return nullptr;
        // The immutable cache is already published; Data cannot enter its assembly path here.
        if (image.IsMaterialized())
            return image.Data();
        _privateCopy = CopyRasterPixelsForWork(image, cancellation);
        return _privateCopy.Data();
    }

  private:
    PixelBuffer _privateCopy;
};

// Reserve once, then initialize bounded spans. Allocation itself has no cancellation latency guarantee.
[[nodiscard]] bool InitializeWandBytes(std::vector<std::uint8_t>& bytes, std::size_t count,
                                       const WandProgress& progress, MagicWand::WorkStage stage)
{
    if (!progress.Checkpoint(stage))
        return false;
    bytes.reserve(count);
    for (std::size_t offset = 0; offset < count;)
    {
        if (!progress.Checkpoint(stage, offset))
            return false;
        const std::size_t size = offset + std::min(WandProgress::Quantum, count - offset);
        bytes.resize(size, 0);
        offset = size;
    }
    return true;
}

void CancelledWand(WandFailure* failure) noexcept
{
    if (failure)
        *failure = WandFailure::Cancelled;
}

[[nodiscard]] Clipper2Lib::PathsD ToClipper(const std::vector<SelectionLoop>& loops)
{
    Clipper2Lib::PathsD paths;
    paths.reserve(loops.size());
    for (const SelectionLoop& loop : loops)
    {
        Clipper2Lib::PathD path;
        path.reserve(loop.size());
        for (const Vec2& point : loop)
        {
            if (std::isfinite(point.x) && std::isfinite(point.y))
            {
                path.emplace_back(point.x, point.y);
            }
        }
        if (path.size() >= 3)
        {
            paths.push_back(std::move(path));
        }
    }
    return paths;
}

[[nodiscard]] std::vector<SelectionLoop> FromClipper(const Clipper2Lib::PathsD& paths)
{
    std::vector<SelectionLoop> loops;
    loops.reserve(paths.size());
    for (const Clipper2Lib::PathD& path : paths)
    {
        if (path.size() < 3)
        {
            continue;
        }
        SelectionLoop loop;
        loop.reserve(path.size());
        for (const Clipper2Lib::PointD& point : path)
        {
            loop.push_back(Vec2{point.x, point.y});
        }
        loops.push_back(std::move(loop));
    }
    return loops;
}

// Exact-area coverage accumulation (the signed-area scanline approach): every edge deposits, per row and per
// column cell it crosses, its signed height (`cover`) and that height weighted by the share of the cell right of
// the edge (`area`). The running sum of cover across a row is the winding to the left of a cell; adding the
// cell's own area gives its coverage. Cell 0 collects everything left of the canvas, cell width + 1 the rest.
struct CoverageCells
{
    int width = 0;
    int height = 0;
    std::unique_ptr<float[]> cover;
    std::unique_ptr<float[]> area;
};

[[nodiscard]] bool DepositEdge(CoverageCells& cells, Vec2 from, Vec2 to, const CancelToken& cancellation)
{
    if (!std::isfinite(from.x) || !std::isfinite(from.y) || !std::isfinite(to.x) || !std::isfinite(to.y))
    {
        return false;
    }
    if (from.y == to.y)
    {
        return true;
    }
    const float sign = to.y > from.y ? 1.0f : -1.0f;
    Vec2 p0 = from;
    Vec2 p1 = to;
    if (sign < 0)
    {
        std::swap(p0, p1);
    }
    const double top = std::clamp(p0.y, 0.0, static_cast<double>(cells.height));
    const double bottom = std::clamp(p1.y, 0.0, static_cast<double>(cells.height));
    if (top >= bottom)
    {
        return true;
    }
    const double deltaX = p1.x - p0.x;
    const double deltaY = p1.y - p0.y;
    const double dxdy = deltaX / deltaY;
    if (!std::isfinite(deltaX) || !std::isfinite(deltaY) || !std::isfinite(dxdy))
    {
        return false;
    }
    const std::size_t stride = static_cast<std::size_t>(cells.width) + 2u;
    const int firstRow = static_cast<int>(std::floor(top));
    const int lastRow = static_cast<int>(std::ceil(bottom));
    for (int y = firstRow; y < lastRow; ++y)
    {
        if (cancellation.IsCancelled())
        {
            return false;
        }
        const double rowTop = std::max(static_cast<double>(y), top);
        const double rowBottom = std::min(static_cast<double>(y + 1), bottom);
        if (rowBottom <= rowTop)
        {
            continue;
        }
        double xa = p0.x + (rowTop - p0.y) * dxdy;
        double xb = p0.x + (rowBottom - p0.y) * dxdy;
        if (!std::isfinite(xa) || !std::isfinite(xb))
        {
            return false;
        }
        if (xa > xb)
        {
            std::swap(xa, xb);
        }
        float* coverRow = cells.cover.get() + static_cast<std::size_t>(y) * stride;
        float* areaRow = cells.area.get() + static_cast<std::size_t>(y) * stride;
        const double rowHeight = rowBottom - rowTop;
        const double span = xb - xa;
        if (!std::isfinite(span))
        {
            return false;
        }
        // Split the segment at every column boundary; a piece's height is its share of the horizontal span.
        // Keep the existing float accumulation order within one maximum canvas width to the left.
        // More distant pieces share that exterior cell; aggregate them rather than overflow an int or
        // walk billions of off-canvas columns for a transformed selection.
        int column = static_cast<int>(
            std::clamp(std::floor(xa), -static_cast<double>(kMaxCanvasSide), static_cast<double>(cells.width)));
        double pieceStart = xa;
        while (true)
        {
            if ((column & 255) == 0 && cancellation.IsCancelled())
            {
                return false;
            }
            const double columnRight = static_cast<double>(column + 1);
            const double pieceEnd = column >= cells.width ? xb : std::min(xb, columnRight);
            const double dy = span > 0 ? rowHeight * (pieceEnd - pieceStart) / span : rowHeight;
            if (dy > 0)
            {
                const int cell = std::clamp(column, -1, cells.width);
                const std::size_t index = static_cast<std::size_t>(cell + 1);
                const double midX = (pieceStart + pieceEnd) * 0.5;
                const double rightShare =
                    cell < 0 ? 1.0 : (cell >= cells.width ? 0.0 : std::clamp(columnRight - midX, 0.0, 1.0));
                coverRow[index] += sign * static_cast<float>(dy);
                areaRow[index] += sign * static_cast<float>(dy * rightShare);
            }
            if (pieceEnd >= xb || column >= cells.width)
            {
                break;
            }
            pieceStart = pieceEnd;
            ++column;
        }
    }
    return true;
}

[[nodiscard]] Raster ResolveCoverage(const CoverageCells& cells, const CancelToken& cancellation)
{
    const int width = cells.width;
    const int height = cells.height;
    PixelBuffer bytes = PixelBuffer::TryAllocate(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
    if (!bytes)
    {
        return {};
    }
    const std::size_t stride = static_cast<std::size_t>(width) + 2u;
    for (int y = 0; y < height; ++y)
    {
        if (cancellation.IsCancelled())
        {
            return {};
        }
        const float* coverRow = cells.cover.get() + static_cast<std::size_t>(y) * stride;
        const float* areaRow = cells.area.get() + static_cast<std::size_t>(y) * stride;
        std::uint8_t* out = bytes.Data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(width);
        float winding = coverRow[0];
        for (int x = 0; x < width; ++x)
        {
            if ((x & 255) == 0 && cancellation.IsCancelled())
            {
                return {};
            }
            const std::size_t index = static_cast<std::size_t>(x) + 1u;
            const float value = winding + areaRow[index];
            winding += coverRow[index];
            const float coverage = std::min(1.0f, std::abs(value));
            out[static_cast<std::size_t>(x)] = static_cast<std::uint8_t>(std::lround(coverage * 255.0f));
        }
    }
    return cancellation.IsCancelled() ? Raster{} : Raster::Adopt(width, height, RasterFormat::Gray8, std::move(bytes));
}

// Hard-edged coverage: a pixel is selected when its center has nonzero winding.
[[nodiscard]] Raster CenterCoverage(const std::vector<SelectionLoop>& loops, int width, int height, const Affine2* map,
                                    const CancelToken& cancellation)
{
    PixelBuffer bytes = PixelBuffer::TryAllocate(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
    if (!bytes)
    {
        return {};
    }
    struct Crossing
    {
        double x;
        int winding;
    };
    // Reserve the entire graph before touching pixels. Each edge contributes at most one crossing per row.
    std::size_t capacity = 0;
    for (const SelectionLoop& loop : loops)
    {
        if (cancellation.IsCancelled() || loop.size() > std::numeric_limits<std::size_t>::max() - capacity)
        {
            return {};
        }
        capacity += loop.size();
    }
    const auto crossings = std::make_unique_for_overwrite<Crossing[]>(capacity);
    // In-place heapsort bounds each cancellation interval without comparator exceptions or extra image memory.
    const auto sortCrossings = [&](std::size_t count) noexcept
    {
        const auto sift = [&](std::size_t start, std::size_t end) noexcept
        {
            std::size_t root = start;
            while (root < end / 2)
            {
                if (cancellation.IsCancelled())
                {
                    return false;
                }
                std::size_t child = root * 2 + 1;
                if (child + 1 < end && crossings[child].x < crossings[child + 1].x)
                {
                    ++child;
                }
                if (!(crossings[root].x < crossings[child].x))
                {
                    break;
                }
                std::swap(crossings[root], crossings[child]);
                root = child;
            }
            return !cancellation.IsCancelled();
        };
        for (std::size_t start = count / 2; start > 0; --start)
        {
            if (!sift(start - 1, count))
            {
                return false;
            }
        }
        for (std::size_t end = count; end > 1; --end)
        {
            std::swap(crossings[0], crossings[end - 1]);
            if (!sift(0, end - 1))
            {
                return false;
            }
        }
        return !cancellation.IsCancelled();
    };
    for (int y = 0; y < height; ++y)
    {
        if (cancellation.IsCancelled())
        {
            return {};
        }
        const double centerY = y + 0.5;
        std::uint8_t* out = bytes.Data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(width);
        for (int x = 0; x < width; x += 4096)
        {
            if (cancellation.IsCancelled())
            {
                return {};
            }
            std::fill_n(out + x, std::min(4096, width - x), std::uint8_t{0});
        }
        std::size_t crossingCount = 0;
        for (const SelectionLoop& loop : loops)
        {
            const std::size_t count = loop.size();
            if (count < 3)
            {
                continue;
            }
            for (std::size_t index = 0; index < count; ++index)
            {
                if ((index & 255u) == 0 && cancellation.IsCancelled())
                {
                    return {};
                }
                const Vec2 a = map != nullptr ? map->Apply(loop[index]) : loop[index];
                const Vec2 b = map != nullptr ? map->Apply(loop[(index + 1) % count]) : loop[(index + 1) % count];
                if (!std::isfinite(a.x) || !std::isfinite(a.y) || !std::isfinite(b.x) || !std::isfinite(b.y))
                {
                    return {};
                }
                if (a.y == b.y)
                {
                    continue;
                }
                const bool downward = b.y > a.y;
                const double top = downward ? a.y : b.y;
                const double bottom = downward ? b.y : a.y;
                if (centerY < top || centerY >= bottom)
                {
                    continue;
                }
                const double deltaX = b.x - a.x;
                const double deltaY = b.y - a.y;
                const double t = (centerY - a.y) / deltaY;
                const double crossing = a.x + deltaX * t;
                if (!std::isfinite(deltaX) || !std::isfinite(deltaY) || !std::isfinite(t) || !std::isfinite(crossing))
                {
                    return {};
                }
                crossings[crossingCount++] = Crossing{crossing, downward ? 1 : -1};
            }
        }
        if (crossingCount == 0)
        {
            continue;
        }
        if (!sortCrossings(crossingCount))
        {
            return {};
        }
        int winding = 0;
        for (std::size_t index = 0; index + 1 < crossingCount; ++index)
        {
            if ((index & 255u) == 0 && cancellation.IsCancelled())
            {
                return {};
            }
            winding += crossings[index].winding;
            if (winding == 0)
            {
                continue;
            }
            // Pixel centers x + 0.5 inside [spanStart, spanEnd).
            const double spanStart = crossings[index].x;
            const double spanEnd = crossings[index + 1].x;
            const int first = static_cast<int>(std::clamp(std::ceil(spanStart - 0.5), 0.0, static_cast<double>(width)));
            const int last =
                static_cast<int>(std::clamp(std::ceil(spanEnd - 0.5), 0.0, static_cast<double>(width))) - 1;
            for (int x = first; x <= last; ++x)
            {
                if ((x & 255) == 0 && cancellation.IsCancelled())
                {
                    return {};
                }
                out[static_cast<std::size_t>(x)] = 255;
            }
        }
    }
    return cancellation.IsCancelled() ? Raster{} : Raster::Adopt(width, height, RasterFormat::Gray8, std::move(bytes));
}

// ── Wand and tracing kernels (ported from WandPixels.c) ──────────────────

// Headings, clockwise on screen (y grows downward): east, south, west, north.
enum Heading : std::uint8_t
{
    East = 1,
    South = 2,
    West = 4,
    North = 8,
};

[[nodiscard]] constexpr std::uint8_t TurnRight(std::uint8_t d) noexcept
{
    return d == North ? East : static_cast<std::uint8_t>(d << 1);
}

[[nodiscard]] constexpr std::uint8_t TurnLeft(std::uint8_t d) noexcept
{
    return d == East ? North : static_cast<std::uint8_t>(d >> 1);
}

[[nodiscard]] constexpr std::uint8_t LowestBit(std::uint8_t bits) noexcept
{
    return static_cast<std::uint8_t>(bits & static_cast<std::uint8_t>(-static_cast<int>(bits)));
}

// Zero vertices cannot start a loop. Skip them in scalar words within the already polled span; memcpy keeps
// unaligned reads and aliasing valid, including odd-width rows. Never read beyond the span's final byte.
[[nodiscard]] const std::uint8_t* NextEdgeVertex(const std::uint8_t* begin, const std::uint8_t* end) noexcept
{
    while (end - begin >= static_cast<std::ptrdiff_t>(sizeof(std::uint64_t)))
    {
        std::uint64_t bits = 0;
        std::memcpy(&bits, begin, sizeof(bits));
        if (bits != 0)
            break;
        begin += sizeof(bits);
    }
    while (begin != end && *begin == 0)
        ++begin;
    return begin;
}

[[nodiscard]] bool WandMatches(const std::uint8_t* pixel, const int reference[4], int tolerance) noexcept
{
    for (int channel = 0; channel < 4; ++channel)
    {
        const int difference = static_cast<int>(pixel[channel]) - reference[channel];
        if (difference < -tolerance || difference > tolerance)
        {
            return false;
        }
    }
    return true;
}

// Outline of `selected(x, y)` pixels along pixel edges. Each vertex of the (width + 1) × (height + 1) grid records
// the directed boundary edges leaving it: a selected pixel's unselected sides, walked clockwise around the pixel.
// Where two loops meet at a corner, turning right keeps them apart.
template <typename Selected>
[[nodiscard]] std::optional<std::vector<SelectionLoop>> TraceEdges(int width, int height, Selected selected,
                                                                   WandFailure* failure, CancelToken cancellation = {},
                                                                   MagicWand::WorkObserver observer = {})
{
    if (failure)
    {
        *failure = WandFailure::None;
    }
    const WandProgress progress(cancellation, observer);
    if (cancellation.IsCancelled())
    {
        CancelledWand(failure);
        return std::nullopt;
    }
    if (width <= 0 || height <= 0)
    {
        return std::nullopt;
    }
    const std::size_t stride = static_cast<std::size_t>(width) + 1u;
    const std::size_t vertices = stride * (static_cast<std::size_t>(height) + 1u);
    std::vector<std::uint8_t> out;
    try
    {
        if (!InitializeWandBytes(out, vertices, progress, MagicWand::WorkStage::TraceInitialize))
        {
            CancelledWand(failure);
            return std::nullopt;
        }
    }
    catch (const std::bad_alloc&)
    {
        if (failure)
        {
            *failure = WandFailure::Memory;
        }
        return std::nullopt;
    }
    std::size_t edges = 0;
    std::size_t scanned = 0;
    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width;)
        {
            if (!progress.Step(MagicWand::WorkStage::TraceEdges, scanned))
            {
                CancelledWand(failure);
                return std::nullopt;
            }
            const int size =
                static_cast<int>(std::min(WandProgress::Remaining(scanned), static_cast<std::size_t>(width - x)));
            const int end = x + size;
            scanned += static_cast<std::size_t>(size);
            for (; x < end; ++x)
            {
                if (!selected(x, y))
                {
                    continue;
                }
                const std::size_t sy = static_cast<std::size_t>(y);
                const std::size_t sx = static_cast<std::size_t>(x);
                if (y == 0 || !selected(x, y - 1))
                {
                    out[sy * stride + sx] |= East;
                    ++edges;
                }
                if (x + 1 == width || !selected(x + 1, y))
                {
                    out[sy * stride + sx + 1] |= South;
                    ++edges;
                }
                if (y + 1 == height || !selected(x, y + 1))
                {
                    out[(sy + 1) * stride + sx + 1] |= West;
                    ++edges;
                }
                if (x == 0 || !selected(x - 1, y))
                {
                    out[(sy + 1) * stride + sx] |= North;
                    ++edges;
                }
            }
        }
        if (edges > MagicWand::kTraceEdgeLimit)
        {
            if (failure)
            {
                *failure = WandFailure::TooDetailed;
            }
            return std::nullopt;
        }
    }
    if (edges == 0)
    {
        if (!progress.Checkpoint(MagicWand::WorkStage::Complete))
            CancelledWand(failure);
        return std::nullopt;
    }
    std::vector<SelectionLoop> loops;
    std::size_t walked = 0;
    try
    {
        for (std::size_t start = 0; start < vertices;)
        {
            if (!progress.Checkpoint(MagicWand::WorkStage::TraceStarts, start))
            {
                CancelledWand(failure);
                return std::nullopt;
            }
            const std::size_t end = start + std::min(WandProgress::Quantum, vertices - start);
            while (start < end)
            {
                start = static_cast<std::size_t>(NextEdgeVertex(out.data() + start, out.data() + end) - out.data());
                if (start == end)
                    break;
                while (out[start] != 0)
                {
                    SelectionLoop loop;
                    std::size_t v = start;
                    std::uint8_t heading = 0;
                    std::uint8_t initial = 0;
                    do
                    {
                        if (!progress.Step(MagicWand::WorkStage::TraceWalk, walked++))
                        {
                            CancelledWand(failure);
                            return std::nullopt;
                        }
                        const std::uint8_t bits = out[v];
                        std::uint8_t d = 0;
                        if (heading == 0)
                        {
                            d = LowestBit(bits);
                        }
                        else if (bits & TurnRight(heading))
                        {
                            d = TurnRight(heading);
                        }
                        else if (bits & heading)
                        {
                            d = heading;
                        }
                        else if (bits & TurnLeft(heading))
                        {
                            d = TurnLeft(heading);
                        }
                        else
                        {
                            d = LowestBit(bits);
                        }
                        if (d == 0)
                        {
                            break;
                        }
                        out[v] = static_cast<std::uint8_t>(out[v] & static_cast<std::uint8_t>(~d));
                        if (d != heading)
                        {
                            loop.push_back(Vec2{static_cast<double>(v % stride), static_cast<double>(v / stride)});
                        }
                        if (heading == 0)
                        {
                            initial = d;
                        }
                        heading = d;
                        v = d == East ? v + 1 : d == West ? v - 1 : d == South ? v + stride : v - stride;
                    } while (v != start);
                    // The start is a corner unless the loop arrives on the heading it left with.
                    if (heading == initial && !loop.empty())
                    {
                        loop.erase(loop.begin());
                    }
                    if (loop.size() >= 3)
                    {
                        loops.push_back(std::move(loop));
                    }
                }
                ++start;
            }
        }
    }
    catch (const std::bad_alloc&)
    {
        if (failure)
        {
            *failure = WandFailure::Memory;
        }
        return std::nullopt;
    }
    if (loops.empty())
    {
        return std::nullopt;
    }
    if (!progress.Checkpoint(MagicWand::WorkStage::Complete))
    {
        CancelledWand(failure);
        return std::nullopt;
    }
    return loops;
}

[[nodiscard]] std::vector<SelectionLoop> CanvasLoops(const CanvasDocument& document)
{
    return SelectionGeometry::Rect(0.0, 0.0, static_cast<double>(document.width), static_cast<double>(document.height));
}
} // namespace

// ── SelectionGeometry ────────────────────────────────────────────────────

namespace SelectionGeometry
{
std::vector<SelectionLoop> Rect(double x, double y, double width, double height)
{
    if (!(width > 0) || !(height > 0) || !std::isfinite(x) || !std::isfinite(y))
    {
        return {};
    }
    return {SelectionLoop{Vec2{x, y}, Vec2{x + width, y}, Vec2{x + width, y + height}, Vec2{x, y + height}}};
}

std::vector<SelectionLoop> Ellipse(double x, double y, double width, double height)
{
    if (!(width > 0) || !(height > 0) || !std::isfinite(x) || !std::isfinite(y))
    {
        return {};
    }
    const double a = width / 2.0;
    const double b = height / 2.0;
    // Segments about two pixels long (Ramanujan's perimeter), a multiple of four so the extremes are exact.
    const double perimeter = kPi * (3.0 * (a + b) - std::sqrt((3.0 * a + b) * (a + 3.0 * b)));
    int segments = std::clamp(static_cast<int>(std::ceil(perimeter / 2.0)), 32, 2048);
    segments = (segments + 3) / 4 * 4;
    SelectionLoop loop;
    loop.reserve(static_cast<std::size_t>(segments));
    const double centerX = x + a;
    const double centerY = y + b;
    for (int index = 0; index < segments; ++index)
    {
        const double angle = 2.0 * kPi * static_cast<double>(index) / static_cast<double>(segments);
        loop.push_back(Vec2{centerX + a * std::cos(angle), centerY + b * std::sin(angle)});
    }
    return {loop};
}

std::optional<SelectionBounds> Bounds(const std::vector<SelectionLoop>& loops) noexcept
{
    return LoopsBounds(loops);
}

std::vector<SelectionLoop> Union(const std::vector<SelectionLoop>& a, const std::vector<SelectionLoop>& b)
{
    return FromClipper(
        Clipper2Lib::Union(ToClipper(a), ToClipper(b), Clipper2Lib::FillRule::NonZero, kClipperPrecision));
}

std::vector<SelectionLoop> Subtract(const std::vector<SelectionLoop>& a, const std::vector<SelectionLoop>& b)
{
    return FromClipper(
        Clipper2Lib::Difference(ToClipper(a), ToClipper(b), Clipper2Lib::FillRule::NonZero, kClipperPrecision));
}

std::vector<SelectionLoop> Intersect(const std::vector<SelectionLoop>& a, const std::vector<SelectionLoop>& b)
{
    return FromClipper(
        Clipper2Lib::Intersect(ToClipper(a), ToClipper(b), Clipper2Lib::FillRule::NonZero, kClipperPrecision));
}

std::vector<SelectionLoop> Offset(const std::vector<SelectionLoop>& loops, double delta)
{
    if (!std::isfinite(delta) || delta == 0.0)
    {
        return loops;
    }
    // Normalize first so every loop is oriented consistently, then inflate with round joins.
    const Clipper2Lib::PathsD normalized =
        Clipper2Lib::Union(ToClipper(loops), Clipper2Lib::FillRule::NonZero, kClipperPrecision);
    return FromClipper(Clipper2Lib::InflatePaths(normalized, delta, Clipper2Lib::JoinType::Round,
                                                 Clipper2Lib::EndType::Polygon, 2.0, kClipperPrecision, 0.0));
}

std::vector<SelectionLoop> Translated(const std::vector<SelectionLoop>& loops, double dx, double dy)
{
    std::vector<SelectionLoop> result = loops;
    for (SelectionLoop& loop : result)
    {
        for (Vec2& point : loop)
        {
            point.x += dx;
            point.y += dy;
        }
    }
    return result;
}

std::vector<SelectionLoop> Transformed(const std::vector<SelectionLoop>& loops, const Affine2& map)
{
    std::vector<SelectionLoop> result = loops;
    for (SelectionLoop& loop : result)
    {
        for (Vec2& point : loop)
        {
            point = map.Apply(point);
        }
    }
    return result;
}

bool Contains(const std::vector<SelectionLoop>& loops, Vec2 point) noexcept
{
    if (!std::isfinite(point.x) || !std::isfinite(point.y))
    {
        return false;
    }
    int winding = 0;
    for (const SelectionLoop& loop : loops)
    {
        const std::size_t count = loop.size();
        if (count < 3)
        {
            continue;
        }
        for (std::size_t index = 0; index < count; ++index)
        {
            const Vec2 a = loop[index];
            const Vec2 b = loop[(index + 1) % count];
            const double cross = (b.x - a.x) * (point.y - a.y) - (b.y - a.y) * (point.x - a.x);
            // On the segment itself counts as inside.
            if (std::abs(cross) < 1e-9 && point.x >= std::min(a.x, b.x) - 1e-9 &&
                point.x <= std::max(a.x, b.x) + 1e-9 && point.y >= std::min(a.y, b.y) - 1e-9 &&
                point.y <= std::max(a.y, b.y) + 1e-9)
            {
                return true;
            }
            if (a.y <= point.y)
            {
                if (b.y > point.y && cross > 0)
                {
                    ++winding;
                }
            }
            else if (b.y <= point.y && cross < 0)
            {
                --winding;
            }
        }
    }
    return winding != 0;
}

namespace
{
Raster CoverageImpl(const std::vector<SelectionLoop>& loops, int width, int height, bool antialiased,
                    const Affine2* map, const CancelToken& cancellation)
{
    if (width < 1 || height < 1 || width > kMaxCanvasSide || height > kMaxCanvasSide || cancellation.IsCancelled())
    {
        return {};
    }
    if (!antialiased)
    {
        return CenterCoverage(loops, width, height, map, cancellation);
    }
    CoverageCells cells;
    cells.width = width;
    cells.height = height;
    const std::size_t count = (static_cast<std::size_t>(width) + 2u) * static_cast<std::size_t>(height);
    cells.cover = std::make_unique_for_overwrite<float[]>(count);
    cells.area = std::make_unique_for_overwrite<float[]>(count);
    for (std::size_t start = 0; start < count; start += 4096)
    {
        if (cancellation.IsCancelled())
        {
            return {};
        }
        const std::size_t length = std::min(std::size_t{4096}, count - start);
        std::fill_n(cells.cover.get() + start, length, 0.0f);
        std::fill_n(cells.area.get() + start, length, 0.0f);
    }
    for (const SelectionLoop& loop : loops)
    {
        const std::size_t points = loop.size();
        if (points < 3)
        {
            continue;
        }
        for (std::size_t index = 0; index < points; ++index)
        {
            if (cancellation.IsCancelled())
            {
                return {};
            }
            const Vec2 from = map != nullptr ? map->Apply(loop[index]) : loop[index];
            const Vec2 to = map != nullptr ? map->Apply(loop[(index + 1) % points]) : loop[(index + 1) % points];
            if (!DepositEdge(cells, from, to, cancellation))
            {
                return {};
            }
        }
    }
    return cancellation.IsCancelled() ? Raster{} : ResolveCoverage(cells, cancellation);
}
} // namespace

Raster Coverage(const std::vector<SelectionLoop>& loops, int width, int height, bool antialiased,
                const CancelToken& cancellation)
{
    return CoverageImpl(loops, width, height, antialiased, nullptr, cancellation);
}

Raster CoverageTransformed(const std::vector<SelectionLoop>& loops, const Affine2& map, int width, int height,
                           bool antialiased, const CancelToken& cancellation)
{
    return CoverageImpl(loops, width, height, antialiased, &map, cancellation);
}
} // namespace SelectionGeometry

SelectionBounds DragBox(Vec2 anchor, Vec2 point, bool square, bool fromCenter) noexcept
{
    double dx = std::round(point.x) - anchor.x;
    double dy = std::round(point.y) - anchor.y;
    if (square)
    {
        const double side = std::max(std::abs(dx), std::abs(dy));
        dx = dx < 0 ? -side : side;
        dy = dy < 0 ? -side : side;
    }
    SelectionBounds box;
    if (fromCenter)
    {
        box.minX = anchor.x - std::abs(dx);
        box.minY = anchor.y - std::abs(dy);
        box.maxX = anchor.x + std::abs(dx);
        box.maxY = anchor.y + std::abs(dy);
    }
    else
    {
        box.minX = std::min(anchor.x, anchor.x + dx);
        box.minY = std::min(anchor.y, anchor.y + dy);
        box.maxX = box.minX + std::abs(dx);
        box.maxY = box.minY + std::abs(dy);
    }
    return box;
}

// ── MagicWand ────────────────────────────────────────────────────────────

namespace MagicWand
{
std::vector<std::uint8_t> Mask(const Raster& image, int seedX, int seedY, const WandSettings& settings, long& count,
                               CancelToken cancellation, WorkObserver observer)
{
    count = 0;
    std::vector<std::uint8_t> mask;
    const WandProgress progress(cancellation, observer);
    const auto cancelled = [&]()
    {
        count = kCancelledMaskCount;
        return std::vector<std::uint8_t>{};
    };
    if (!progress.Checkpoint(WorkStage::Source))
        return cancelled();
    if (!image.HasStorage() || image.Format() != RasterFormat::Bgra8888)
    {
        return mask;
    }
    const int width = image.Width();
    const int height = image.Height();
    const std::size_t stride = static_cast<std::size_t>(image.Stride());
    WandPixels source;
    const std::uint8_t* rgba = source.Read(image, cancellation);
    if (!progress.Checkpoint(WorkStage::Source, 1))
        return cancelled();
    if (rgba == nullptr)
    {
        count = -1;
        return {};
    }
    try
    {
        if (!InitializeWandBytes(mask, static_cast<std::size_t>(width) * static_cast<std::size_t>(height), progress,
                                 WorkStage::MaskInitialize))
            return cancelled();
    }
    catch (const std::bad_alloc&)
    {
        count = -1;
        return {};
    }
    if (seedX < 0 || seedY < 0 || seedX >= width || seedY >= height)
    {
        return mask;
    }
    const int radius = std::max(0, settings.Radius());
    const int tolerance = std::clamp(settings.tolerance, 0, 255);
    const int x0 = std::max(0, seedX - radius);
    const int x1 = std::min(width - 1, seedX + radius);
    const int y0 = std::max(0, seedY - radius);
    const int y1 = std::min(height - 1, seedY + radius);
    unsigned long sums[4] = {0, 0, 0, 0};
    unsigned long samples = 0;
    for (int y = y0; y <= y1; ++y)
    {
        for (int x = x0; x <= x1; ++x, ++samples)
        {
            if (!progress.Step(WorkStage::Sample, samples))
                return cancelled();
            const std::uint8_t* pixel = rgba + static_cast<std::size_t>(y) * stride + static_cast<std::size_t>(x) * 4u;
            for (int channel = 0; channel < 4; ++channel)
            {
                sums[channel] += pixel[channel];
            }
        }
    }
    int reference[4];
    for (int channel = 0; channel < 4; ++channel)
    {
        reference[channel] = static_cast<int>((sums[channel] + samples / 2) / samples);
    }
    const auto pixelAt = [&](int x, int y)
    { return rgba + static_cast<std::size_t>(y) * stride + static_cast<std::size_t>(x) * 4u; };
    const auto maskAt = [&](int x, int y) -> std::uint8_t&
    { return mask[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)]; };
    if (!settings.contiguous)
    {
        std::size_t scanned = 0;
        for (int y = 0; y < height; ++y)
        {
            for (int x = 0; x < width;)
            {
                if (!progress.Step(WorkStage::MatchScan, scanned))
                    return cancelled();
                const int size =
                    static_cast<int>(std::min(WandProgress::Remaining(scanned), static_cast<std::size_t>(width - x)));
                const int end = x + size;
                scanned += static_cast<std::size_t>(size);
                for (; x < end; ++x)
                {
                    if (WandMatches(pixelAt(x, y), reference, tolerance))
                    {
                        maskAt(x, y) = 255;
                        ++count;
                    }
                }
            }
        }
        return progress.Checkpoint(WorkStage::Complete) ? std::move(mask) : cancelled();
    }
    // Scanline flood fill: each popped seed fills its whole horizontal run, then pushes one seed per matching run
    // in the rows directly above and below it.
    std::vector<std::pair<int, int>> stack;
    std::size_t seeds = 0, runs = 0, neighbors = 0;
    try
    {
        stack.reserve(4096);
        stack.emplace_back(seedX, seedY);
        while (!stack.empty())
        {
            if (!progress.Step(WorkStage::FloodSeeds, seeds++))
                return cancelled();
            const auto [x, y] = stack.back();
            stack.pop_back();
            if (maskAt(x, y) != 0 || !WandMatches(pixelAt(x, y), reference, tolerance))
            {
                continue;
            }
            int left = x;
            int right = x;
            while (left > 0 && maskAt(left - 1, y) == 0 && WandMatches(pixelAt(left - 1, y), reference, tolerance))
            {
                if (!progress.Step(WorkStage::FloodRun, runs))
                    return cancelled();
                const int begin = left;
                const int end =
                    left - static_cast<int>(std::min(WandProgress::Remaining(runs), static_cast<std::size_t>(left)));
                do
                {
                    --left;
                } while (left > end && maskAt(left - 1, y) == 0 &&
                         WandMatches(pixelAt(left - 1, y), reference, tolerance));
                runs += static_cast<std::size_t>(begin - left);
            }
            while (right + 1 < width && maskAt(right + 1, y) == 0 &&
                   WandMatches(pixelAt(right + 1, y), reference, tolerance))
            {
                if (!progress.Step(WorkStage::FloodRun, runs))
                    return cancelled();
                const int begin = right;
                const int end = right + static_cast<int>(std::min(WandProgress::Remaining(runs),
                                                                  static_cast<std::size_t>(width - right - 1)));
                do
                {
                    ++right;
                } while (right < end && maskAt(right + 1, y) == 0 &&
                         WandMatches(pixelAt(right + 1, y), reference, tolerance));
                runs += static_cast<std::size_t>(right - begin);
            }
            std::memset(&maskAt(left, y), 255, static_cast<std::size_t>(right - left + 1));
            count += right - left + 1;
            for (int side = 0; side < 2; ++side)
            {
                if (side == 0 ? y == 0 : y + 1 >= height)
                {
                    continue;
                }
                const int ny = side == 0 ? y - 1 : y + 1;
                bool inRun = false;
                for (int nx = left; nx <= right;)
                {
                    if (!progress.Step(WorkStage::FloodNeighbors, neighbors))
                        return cancelled();
                    const int size = static_cast<int>(
                        std::min(WandProgress::Remaining(neighbors), static_cast<std::size_t>(right - nx + 1)));
                    const int end = nx + size;
                    neighbors += static_cast<std::size_t>(size);
                    for (; nx < end; ++nx)
                    {
                        const bool candidate =
                            maskAt(nx, ny) == 0 && WandMatches(pixelAt(nx, ny), reference, tolerance);
                        if (candidate && !inRun)
                        {
                            stack.emplace_back(nx, ny);
                        }
                        inRun = candidate;
                    }
                }
            }
        }
    }
    catch (const std::bad_alloc&)
    {
        count = -1;
        return {};
    }
    return progress.Checkpoint(WorkStage::Complete) ? std::move(mask) : cancelled();
}

std::optional<std::vector<SelectionLoop>> Trace(const std::vector<std::uint8_t>& mask, int width, int height,
                                                WandFailure* failure, CancelToken cancellation, WorkObserver observer)
{
    if (cancellation.IsCancelled())
    {
        CancelledWand(failure);
        return std::nullopt;
    }
    if (width <= 0 || height <= 0 || mask.size() != static_cast<std::size_t>(width) * static_cast<std::size_t>(height))
    {
        if (failure)
        {
            *failure = WandFailure::None;
        }
        return std::nullopt;
    }
    return TraceEdges(
        width, height,
        [&](int x, int y)
        {
            return mask[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)] !=
                   0;
        },
        failure, cancellation, observer);
}

std::optional<std::vector<SelectionLoop>> Select(const Raster& image, Vec2 point, const WandSettings& settings,
                                                 WandFailure* failure, CancelToken cancellation, WorkObserver observer)
{
    if (failure)
    {
        *failure = WandFailure::None;
    }
    if (cancellation.IsCancelled())
    {
        CancelledWand(failure);
        return std::nullopt;
    }
    if (!image.HasStorage() || !std::isfinite(point.x) || !std::isfinite(point.y))
    {
        return std::nullopt;
    }
    const double floorX = std::floor(point.x);
    const double floorY = std::floor(point.y);
    if (floorX < 0 || floorY < 0 || floorX >= image.Width() || floorY >= image.Height())
    {
        return std::nullopt;
    }
    long count = 0;
    const std::vector<std::uint8_t> mask =
        Mask(image, static_cast<int>(floorX), static_cast<int>(floorY), settings, count, cancellation, observer);
    if (count < 0)
    {
        if (failure)
        {
            *failure = count == kCancelledMaskCount ? WandFailure::Cancelled : WandFailure::Memory;
        }
        return std::nullopt;
    }
    if (count == 0)
    {
        return std::nullopt;
    }
    return Trace(mask, image.Width(), image.Height(), failure, cancellation, observer);
}
} // namespace MagicWand

// ── MaskTracing ──────────────────────────────────────────────────────────

namespace MaskTracing
{
std::optional<std::vector<SelectionLoop>> DarkPixels(const Raster& mask, WandFailure* failure, CancelToken cancellation,
                                                     MagicWand::WorkObserver observer)
{
    if (failure)
    {
        *failure = WandFailure::None;
    }
    const WandProgress progress(cancellation, observer);
    if (!progress.Checkpoint(MagicWand::WorkStage::Source))
    {
        CancelledWand(failure);
        return std::nullopt;
    }
    if (!mask.HasStorage() || mask.Format() != RasterFormat::Gray8)
    {
        return std::nullopt;
    }
    WandPixels source;
    const std::uint8_t* data = source.Read(mask, cancellation);
    if (!progress.Checkpoint(MagicWand::WorkStage::Source, 1))
    {
        CancelledWand(failure);
        return std::nullopt;
    }
    if (data == nullptr)
    {
        if (failure)
        {
            *failure = WandFailure::Memory;
        }
        return std::nullopt;
    }
    const std::size_t stride = static_cast<std::size_t>(mask.Stride());
    return TraceEdges(
        mask.Width(), mask.Height(),
        [&](int x, int y) { return data[static_cast<std::size_t>(y) * stride + static_cast<std::size_t>(x)] < 128; },
        failure, cancellation, observer);
}

std::optional<std::vector<SelectionLoop>> OpaquePixels(const Raster& image, WandFailure* failure,
                                                       CancelToken cancellation, MagicWand::WorkObserver observer)
{
    if (failure)
    {
        *failure = WandFailure::None;
    }
    const WandProgress progress(cancellation, observer);
    if (!progress.Checkpoint(MagicWand::WorkStage::Source))
    {
        CancelledWand(failure);
        return std::nullopt;
    }
    if (!image.HasStorage() || image.Format() != RasterFormat::Bgra8888)
    {
        return std::nullopt;
    }
    WandPixels source;
    const std::uint8_t* data = source.Read(image, cancellation);
    if (!progress.Checkpoint(MagicWand::WorkStage::Source, 1))
    {
        CancelledWand(failure);
        return std::nullopt;
    }
    if (data == nullptr)
    {
        if (failure)
        {
            *failure = WandFailure::Memory;
        }
        return std::nullopt;
    }
    const std::size_t stride = static_cast<std::size_t>(image.Stride());
    return TraceEdges(
        image.Width(), image.Height(), [&](int x, int y)
        { return data[static_cast<std::size_t>(y) * stride + static_cast<std::size_t>(x) * 4u + 3u] >= 128; }, failure,
        cancellation, observer);
}
} // namespace MaskTracing

// ── Layer sampling ───────────────────────────────────────────────────────

Affine2 PixelToDocument(const LayerTransform& transform, int width, int height) noexcept
{
    const double w = width > 0 ? static_cast<double>(width) : 1.0;
    const double h = height > 0 ? static_cast<double>(height) : 1.0;
    return Affine2::Scale(1.0 / w, 1.0 / h).Concat(transform.UnitToDocument());
}

Raster RenderLayerPixels(const Raster& image, const LayerTransform& transform, int width, int height,
                         const CancelToken& cancellation)
{
    if (cancellation.IsCancelled() || width < 1 || height < 1)
    {
        return {};
    }
    try
    {
        if (!image.HasStorage() || image.Format() != RasterFormat::Bgra8888 || !transform.IsValid())
        {
            return RenderPlacedImage({}, {}, width, height, LayerSampling::Smooth, cancellation);
        }
        // A transient sample, not a resample: High collapses to Smooth's single bilinear tap (no supersampling),
        // and only Nearest changes what a stroke reads, matching legacy LayerRenderer.draw's interpolation choice.
        const LayerSampling sampling =
            transform.sampling == LayerSampling::Nearest ? LayerSampling::Nearest : LayerSampling::Smooth;
        const Affine2 pixelToTarget = PixelToDocument(transform, image.Width(), image.Height());
        return RenderPlacedImage(image, pixelToTarget, width, height, sampling, cancellation);
    }
    catch (const std::bad_alloc&)
    {
        return {};
    }
}

// ── DocumentSession selection API ────────────────────────────────────────

bool DocumentSession::CanEditSelection() const noexcept
{
    return _document.has_value() && !_transformEdit.has_value() && !_warpUpdatingOpening && !_warpFinishingOpening;
}

const std::optional<DocumentSelection>& DocumentSession::Selection() const noexcept
{
    static const std::optional<DocumentSelection> kNone;
    return _document.has_value() ? _document->selection : kNone;
}

SelectionMode DocumentSession::SelectionModeFor(bool shift, bool option) const noexcept
{
    return option ? SelectionMode::Subtract : shift ? SelectionMode::Add : selectionModeChoice;
}

SelectionMode DocumentSession::LassoCursorMode(bool shift, bool option) const noexcept
{
    return _lassoDraft.has_value() ? _lassoDraft->mode : SelectionModeFor(shift, option);
}

SelectionMode DocumentSession::DisplayedSelectionMode() const noexcept
{
    if (_lassoDraft.has_value())
    {
        return _lassoDraft->mode;
    }
    return heldSelectionMode.has_value() ? heldSelectionMode.value() : selectionModeChoice;
}

bool DocumentSession::UpdateHeldSelectionKeys(bool shift, bool option) noexcept
{
    const SelectionMode before = DisplayedSelectionMode();
    std::optional<SelectionMode> held;
    if (option)
    {
        held = SelectionMode::Subtract;
    }
    else if (shift)
    {
        held = SelectionMode::Add;
    }
    heldSelectionMode = held;
    return DisplayedSelectionMode() != before;
}

void DocumentSession::BeginMarquee(Vec2 point, SelectionMode mode)
{
    if (_lassoOpening == (std::numeric_limits<std::uint64_t>::max)() || !CanEditSelection() ||
        _selectionMoveOrigin.has_value() || !std::isfinite(point.x) || !std::isfinite(point.y))
    {
        return;
    }
    const Vec2 anchor{std::round(point.x), std::round(point.y)};
    InvalidateLassoPendingIntents();
    ++_lassoOpening;
    _lassoDraft = LassoDraft{{anchor}, std::nullopt, mode, marqueeKind, anchor};
}

void DocumentSession::BeginLasso(Vec2 point, SelectionMode mode)
{
    if (_lassoOpening == (std::numeric_limits<std::uint64_t>::max)() || !CanEditSelection() ||
        _selectionMoveOrigin.has_value() || !std::isfinite(point.x) || !std::isfinite(point.y))
    {
        return;
    }
    InvalidateLassoPendingIntents();
    ++_lassoOpening;
    _lassoDraft = LassoDraft{{point}, std::nullopt, mode, lassoKind, std::nullopt};
}

void DocumentSession::DragMarquee(Vec2 point, bool square, bool fromCenter)
{
    if (_lassoDraft && _lassoDraft->IsBounded())
    {
        if (!DragBoundedMarquee(point, square, fromCenter))
            CancelLasso();
        return;
    }
    if (!_lassoDraft.has_value() || !_lassoDraft->anchor.has_value() ||
        (_lassoDraft->kind != LassoKind::Rectangle && _lassoDraft->kind != LassoKind::Ellipse) ||
        !std::isfinite(point.x) || !std::isfinite(point.y))
    {
        return;
    }
    const SelectionBounds box = DragBox(_lassoDraft->anchor.value(), point, square, fromCenter);
    _lassoDraft->points = {Vec2{box.minX, box.minY}, Vec2{box.maxX, box.minY}, Vec2{box.maxX, box.maxY},
                           Vec2{box.minX, box.maxY}};
}

void DocumentSession::ExtendLasso(Vec2 point)
{
    if (_lassoDraft && _lassoDraft->IsBounded())
    {
        if (!ExtendBoundedLasso(point))
            CancelLasso();
        return;
    }
    if (!_lassoDraft.has_value() || !std::isfinite(point.x) || !std::isfinite(point.y))
    {
        return;
    }
    if (!_lassoDraft->points.empty())
    {
        const Vec2 last = _lassoDraft->points.back();
        if (std::hypot(point.x - last.x, point.y - last.y) < 0.25)
        {
            return;
        }
    }
    _lassoDraft->points.push_back(point);
}

void DocumentSession::MoveLassoCursor(std::optional<Vec2> point) noexcept
{
    if (_lassoDraft && _lassoDraft->IsBounded())
    {
        if (!MoveBoundedLassoCursor(point))
            CancelLasso();
        return;
    }
    if (_lassoDraft.has_value())
    {
        _lassoDraft->cursor = point;
    }
}

void DocumentSession::RemoveLastLassoPoint()
{
    if (_lassoDraft && _lassoDraft->IsBounded())
    {
        if (!RemoveLastBoundedLassoPoint())
            CancelLasso();
        return;
    }
    if (!_lassoDraft.has_value())
    {
        return;
    }
    if (!_lassoDraft->points.empty())
    {
        _lassoDraft->points.pop_back();
    }
    if (_lassoDraft->points.empty())
    {
        _lassoDraft.reset();
    }
}

void DocumentSession::CancelLasso() noexcept
{
    InvalidateLassoPendingIntents();
    if (_lassoDraft && _lassoDraft->_bounded)
        (void)_lassoDraft->_bounded->CancelOwner(_lassoDraft->_bounded->CurrentOwner());
    _lassoFinishPendingSerial = 0;
    _lassoDraft.reset();
}

void DocumentSession::FinishLasso()
{
    if (_lassoDraft && _lassoDraft->IsBounded())
        return; // An admitted draft finishes only through the selected worker intent.
    if (!_lassoDraft.has_value())
    {
        return;
    }
    const LassoDraft draft = std::move(_lassoDraft.value());
    _lassoDraft.reset();
    std::vector<SelectionLoop> outline;
    if (draft.kind == LassoKind::Ellipse && draft.points.size() == 4)
    {
        // The drag's box, whole pixels like a rectangle; the oval fills it.
        const std::optional<SelectionBounds> box = SelectionGeometry::Bounds({draft.points});
        if (box.has_value())
        {
            outline = SelectionGeometry::Ellipse(box->minX, box->minY, box->Width(), box->Height());
        }
    }
    else
    {
        outline = {draft.points};
    }
    const std::optional<SelectionBounds> bounds = SelectionGeometry::Bounds(outline);
    if ((draft.points.size() < 3 && draft.kind != LassoKind::Ellipse) || !bounds.has_value() ||
        !(bounds->Width() > 0) || !(bounds->Height() > 0))
    {
        if (draft.mode == SelectionMode::New)
        {
            Deselect();
        }
        return;
    }
    ApplySelection(outline, draft.mode,
                   draft.kind == LassoKind::Freehand    ? L"Lasso"
                   : draft.kind == LassoKind::Polygonal ? L"Polygonal Lasso"
                   : draft.kind == LassoKind::Ellipse   ? L"Elliptical Marquee"
                                                        : L"Rectangular Marquee");
}

void DocumentSession::ToggleMarqueeKind() noexcept
{
    CancelLasso();
    marqueeKind = marqueeKind == LassoKind::Rectangle ? LassoKind::Ellipse : LassoKind::Rectangle;
}

void DocumentSession::ToggleLassoKind() noexcept
{
    CancelLasso();
    lassoKind = lassoKind == LassoKind::Freehand ? LassoKind::Polygonal : LassoKind::Freehand;
}

void DocumentSession::ApplySelection(const std::vector<SelectionLoop>& shape, SelectionMode mode, std::wstring name)
{
    if (!CanEditSelection())
    {
        return;
    }
    const CanvasDocument& document = _document.value();
    const std::vector<SelectionLoop> clipped = SelectionGeometry::Intersect(shape, CanvasLoops(document));
    std::vector<SelectionLoop> result;
    switch (mode)
    {
    case SelectionMode::New:
        result = clipped;
        break;
    case SelectionMode::Add:
        result =
            document.selection.has_value() ? SelectionGeometry::Union(document.selection->loops, clipped) : clipped;
        break;
    case SelectionMode::Subtract:
        // Subtracting from no selection selects nothing new, so nothing changes.
        if (!document.selection.has_value())
        {
            return;
        }
        result = SelectionGeometry::Subtract(document.selection->loops, clipped);
        break;
    }
    SetSelection(DocumentSelection{selectionAntialiased, std::move(result)}, std::move(name));
}

void DocumentSession::SetSelection(std::optional<DocumentSelection> value, std::wstring name)
{
    if (!CanEditSelection() || value == _document->selection)
    {
        return;
    }
    BeginEdit(std::move(name));
    _document->selection = std::move(value);
    ++_editRevision;
    EndEdit();
}

bool DocumentSession::CanMoveSelection(Vec2 point) const
{
    const std::optional<DocumentSelection>& selection = Selection();
    if (!selection.has_value() || selection->IsEmpty() || !CanEditSelection() || _lassoDraft.has_value())
    {
        return false;
    }
    return SelectionGeometry::Contains(selection->loops, point);
}

bool DocumentSession::BeginSelectionMove()
{
    const std::optional<DocumentSelection>& selection = Selection();
    if (_selectionMoveOrigin.has_value() || !selection.has_value() || selection->IsEmpty() || !CanEditSelection() ||
        _selectionMoveOpening == (std::numeric_limits<std::uint64_t>::max)())
    {
        return false;
    }
    BeginEdit(L"Move Selection");
    _selectionMoveOrigin = selection;
    ++_selectionMoveOpening;
    return true;
}

void DocumentSession::MoveSelection(double dx, double dy)
{
    if (!_selectionMoveOrigin.has_value() || !_document.has_value() || !std::isfinite(dx) || !std::isfinite(dy))
    {
        return;
    }
    const DocumentSelection& origin = _selectionMoveOrigin.value();
    _document->selection = DocumentSelection{
        origin.antialiased, SelectionGeometry::Translated(origin.loops, std::round(dx), std::round(dy))};
    NoteDocumentMutation();
}

void DocumentSession::EndSelectionMove()
{
    if (!_selectionMoveOrigin.has_value())
    {
        return;
    }
    _selectionMoveOrigin.reset();
    EndEdit();
}

void DocumentSession::CancelSelectionMove()
{
    if (!_selectionMoveOrigin.has_value())
    {
        return;
    }
    // The document equals the one the entry began with, so closing it records nothing and keeps the redo.
    if (_document.has_value())
    {
        _document->selection = _selectionMoveOrigin;
        NoteDocumentMutation();
    }
    EndSelectionMove();
}

void DocumentSession::NudgeSelection(double dx, double dy)
{
    if (!BeginSelectionMove())
    {
        return;
    }
    MoveSelection(dx, dy);
    EndSelectionMove();
}

bool DocumentSession::CanModifySelection() const noexcept
{
    const std::optional<DocumentSelection>& selection = Selection();
    return selection.has_value() && !selection->IsEmpty() && CanEditSelection() && !_lassoDraft.has_value();
}

void DocumentSession::ExpandSelection(int amount)
{
    if (!CanModifySelection() || amount <= 0 || amount > kSelectionModifyLimit)
    {
        return;
    }
    const DocumentSelection& current = Selection().value();
    // A band `amount` wide added outside the outline, kept on the canvas.
    std::vector<SelectionLoop> result = SelectionGeometry::Offset(current.loops, static_cast<double>(amount));
    result = SelectionGeometry::Intersect(result, CanvasLoops(_document.value()));
    SetSelection(DocumentSelection{current.antialiased, std::move(result)}, L"Expand Selection");
}

void DocumentSession::ContractSelection(int amount)
{
    if (!CanModifySelection() || amount <= 0 || amount > kSelectionModifyLimit)
    {
        return;
    }
    const DocumentSelection& current = Selection().value();
    // Shrinks away from the canvas edges too; contracting past the middle leaves an explicit empty selection.
    std::vector<SelectionLoop> result = SelectionGeometry::Offset(current.loops, -static_cast<double>(amount));
    SetSelection(DocumentSelection{current.antialiased, std::move(result)}, L"Contract Selection");
}

void DocumentSession::SelectAll()
{
    if (!CanEditSelection())
    {
        return;
    }
    SetSelection(DocumentSelection{true, CanvasLoops(_document.value())}, L"Select All");
}

void DocumentSession::Deselect()
{
    if (!Selection().has_value())
    {
        return;
    }
    SetSelection(std::nullopt, L"Deselect");
}

void DocumentSession::InvertSelection()
{
    if (!CanEditSelection() || !Selection().has_value())
    {
        return;
    }
    const DocumentSelection& current = Selection().value();
    SetSelection(DocumentSelection{current.antialiased,
                                   SelectionGeometry::Subtract(CanvasLoops(_document.value()), current.loops)},
                 L"Inverse");
}

bool DocumentSession::LoadMaskSelection(const Guid& id, SelectionMode mode, WandFailure* failure)
{
    if (failure)
    {
        *failure = WandFailure::None;
    }
    if (!CanEditSelection())
    {
        return false;
    }
    const ImageLayer* layer = FindLayer(id);
    if (layer == nullptr || !layer->mask.has_value())
    {
        return false;
    }
    const Raster& mask = layer->mask->asset.image;
    const std::optional<std::vector<SelectionLoop>> traced = MaskTracing::DarkPixels(mask, failure);
    if (!traced.has_value())
    {
        return false;
    }
    const Affine2 toDocument = PixelToDocument(MaskTransform(*layer), mask.Width(), mask.Height());
    ApplySelection(SelectionGeometry::Transformed(traced.value(), toDocument), mode, L"Load Mask Selection");
    return true;
}

bool DocumentSession::LoadLayerSelection(const Guid& id, SelectionMode mode, WandFailure* failure)
{
    if (failure)
    {
        *failure = WandFailure::None;
    }
    if (!CanEditSelection())
    {
        return false;
    }
    const ImageLayer* layer = FindLayer(id);
    if (layer == nullptr || layer->isGroup || !layer->asset.has_value())
    {
        return false;
    }
    const Raster& image = layer->asset->image;
    const std::optional<std::vector<SelectionLoop>> traced = MaskTracing::OpaquePixels(image, failure);
    if (!traced.has_value())
    {
        return false;
    }
    const Affine2 toDocument = PixelToDocument(layer->transform, image.Width(), image.Height());
    ApplySelection(SelectionGeometry::Transformed(traced.value(), toDocument), mode, L"Load Layer Selection");
    return true;
}

Raster DocumentSession::LayerSample(bool allLayers, const Raster* composite) const
{
    if (!_document.has_value())
    {
        return {};
    }
    const CanvasDocument& document = _document.value();
    if (allLayers)
    {
        if (composite == nullptr || !composite->HasStorage() || composite->Width() != document.width ||
            composite->Height() != document.height || composite->Format() != RasterFormat::Bgra8888)
        {
            return {};
        }
        return *composite;
    }
    const ImageLayer* layer = ActiveLayer();
    if (layer != nullptr && !layer->isGroup && layer->asset.has_value())
    {
        return RenderLayerPixels(layer->asset->image, DisplayedTransform(*layer), document.width, document.height);
    }
    return Raster::Make(document.width, document.height, RasterFormat::Bgra8888);
}

Raster DocumentSession::WandSample(const Raster* composite) const
{
    return LayerSample(wandSettings.sampleAllLayers, composite);
}

WandFailure DocumentSession::MagicWand(Vec2 point, SelectionMode mode, const Raster* composite)
{
    if (!CanEditSelection() || _selectionMoveOrigin.has_value() || !std::isfinite(point.x) || !std::isfinite(point.y))
    {
        return WandFailure::None;
    }
    const CanvasDocument& document = _document.value();
    if (point.x < 0 || point.y < 0 || point.x >= document.width || point.y >= document.height)
    {
        return WandFailure::None;
    }
    const Raster sample = WandSample(composite);
    if (!sample.HasStorage())
    {
        // All Layers without a usable composite does nothing; the layer's own pixels always render unless they
        // cannot be read.
        return wandSettings.sampleAllLayers ? WandFailure::None : WandFailure::Memory;
    }
    WandFailure failure = WandFailure::None;
    const std::optional<std::vector<SelectionLoop>> outline = MagicWand::Select(sample, point, wandSettings, &failure);
    if (failure != WandFailure::None)
    {
        return failure;
    }
    if (!outline.has_value())
    {
        // Nothing matched: New clears the selection, as a lasso click enclosing nothing does.
        if (mode == SelectionMode::New)
        {
            Deselect();
        }
        return WandFailure::None;
    }
    // A traced outline already lies on the canvas, so a new selection skips the clip to the canvas, which is
    // costly for a detailed outline.
    if (mode == SelectionMode::New)
    {
        SetSelection(DocumentSelection{selectionAntialiased, outline.value()}, L"Magic Wand");
    }
    else
    {
        ApplySelection(outline.value(), mode, L"Magic Wand");
    }
    return WandFailure::None;
}
