#include "Heal.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <new>
#include <vector>

namespace SpotHealing
{
namespace
{
constexpr std::uint8_t kOutside = 0;
constexpr std::uint8_t kRing = 1;
constexpr std::uint8_t kHole = 2;
constexpr double kPi = 3.14159265358979323846;
constexpr double kInfinity = std::numeric_limits<double>::infinity();

using Index = std::int64_t;

struct Work final
{
    const CancelToken& cancellation;
    WorkObserver observer;
    [[nodiscard]] bool Continue(WorkStage stage) const noexcept
    {
        if (cancellation.IsCancelled())
            return false;
        if (observer.checkpoint)
            observer.checkpoint(observer.context, stage);
        return !cancellation.IsCancelled();
    }
};

[[nodiscard]] std::uint32_t HashKey(std::uint32_t x) noexcept
{
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

[[nodiscard]] double Unit(std::uint32_t key) noexcept
{
    return static_cast<double>(HashKey(key) >> 8) / 16777216.0;
}

[[nodiscard]] const std::uint8_t* PixelAt(const std::uint8_t* rgba, std::size_t stride, Index x, Index y) noexcept
{
    return rgba + static_cast<std::size_t>(y) * stride + static_cast<std::size_t>(x) * 4u;
}

[[nodiscard]] std::uint8_t* PixelAt(std::uint8_t* rgba, std::size_t stride, Index x, Index y) noexcept
{
    return rgba + static_cast<std::size_t>(y) * stride + static_cast<std::size_t>(x) * 4u;
}

// Mean squared difference between the ring around the spot and the ring around the patch offset by (dx, dy).
// Infinite when the patch would overlap the spot or leave the image.
[[nodiscard]] double Score(const std::uint8_t* rgba, std::size_t stride, const std::uint8_t* role, Index wx0, Index wy0,
                           Index ww, Index wh, Index dx, Index dy, Index W, Index H, const Work* work) noexcept
{
    if (std::llabs(dx) < ww && std::llabs(dy) < wh)
    {
        return kInfinity;
    }
    if (wx0 + dx < 0 || wy0 + dy < 0 || wx0 + ww + dx > W || wy0 + wh + dy > H)
    {
        return kInfinity;
    }
    double sum = 0;
    Index n = 0;
    for (Index y = 0; y < wh; ++y)
    {
        if (work && !work->Continue(WorkStage::PatchSearch))
            return kInfinity;
        for (Index x = 0; x < ww; ++x)
        {
            if (role[static_cast<std::size_t>(y * ww + x)] != kRing)
            {
                continue;
            }
            const std::uint8_t* t = PixelAt(rgba, stride, wx0 + x, wy0 + y);
            const std::uint8_t* s = PixelAt(rgba, stride, wx0 + x + dx, wy0 + y + dy);
            for (int c = 0; c < 4; ++c)
            {
                const double d = static_cast<double>(t[c]) - s[c];
                sum += d * d;
            }
            ++n;
        }
    }
    return n ? sum / static_cast<double>(n) : kInfinity;
}

// Solves for smooth values over HOLE pixels, fixed to the RING values around them. A coarser copy is solved first
// and used as the starting point, so large spots settle in few passes.
[[nodiscard]] bool Solve(float* value, const std::uint8_t* role, Index w, Index h, int depth, const Work* work)
{
    int iterations = 300;
    if (w > 32 && h > 32 && depth < 16)
    {
        const Index cw = (w + 1) / 2;
        const Index ch = (h + 1) / 2;
        std::vector<float> coarse(static_cast<std::size_t>(cw * ch * 4), 0.0f);
        std::vector<std::uint8_t> coarseRole(static_cast<std::size_t>(cw * ch), 0);
        for (Index y = 0; y < ch; ++y)
        {
            if (work && !work->Continue(WorkStage::SolveCoarse))
                return false;
            for (Index x = 0; x < cw; ++x)
            {
                int known = 0;
                int hole = 0;
                float knownSum[4] = {0, 0, 0, 0};
                float holeSum[4] = {0, 0, 0, 0};
                for (Index j = 0; j < 2; ++j)
                {
                    for (Index i = 0; i < 2; ++i)
                    {
                        const Index fx = x * 2 + i;
                        const Index fy = y * 2 + j;
                        if (fx >= w || fy >= h)
                        {
                            continue;
                        }
                        const std::size_t p = static_cast<std::size_t>(fy * w + fx);
                        if (role[p] == kRing)
                        {
                            ++known;
                            for (int c = 0; c < 4; ++c)
                            {
                                knownSum[c] += value[p * 4 + static_cast<std::size_t>(c)];
                            }
                        }
                        else if (role[p] == kHole)
                        {
                            ++hole;
                            for (int c = 0; c < 4; ++c)
                            {
                                holeSum[c] += value[p * 4 + static_cast<std::size_t>(c)];
                            }
                        }
                    }
                }
                const std::size_t q = static_cast<std::size_t>(y * cw + x);
                if (known)
                {
                    coarseRole[q] = kRing;
                    for (int c = 0; c < 4; ++c)
                    {
                        coarse[q * 4 + static_cast<std::size_t>(c)] = knownSum[c] / static_cast<float>(known);
                    }
                }
                else if (hole)
                {
                    coarseRole[q] = kHole;
                    for (int c = 0; c < 4; ++c)
                    {
                        coarse[q * 4 + static_cast<std::size_t>(c)] = holeSum[c] / static_cast<float>(hole);
                    }
                }
            }
        }
        if (!Solve(coarse.data(), coarseRole.data(), cw, ch, depth + 1, work))
            return false;
        for (Index y = 0; y < h; ++y)
        {
            if (work && !work->Continue(WorkStage::Solve))
                return false;
            for (Index x = 0; x < w; ++x)
            {
                const std::size_t p = static_cast<std::size_t>(y * w + x);
                const std::size_t q = static_cast<std::size_t>((y / 2) * cw + x / 2);
                if (role[p] == kHole && coarseRole[q] == kHole)
                {
                    std::memcpy(value + p * 4, coarse.data() + q * 4, 4 * sizeof(float));
                }
            }
        }
        iterations = 40;
    }
    const float omega = 1.8f;
    for (int it = 0; it < iterations; ++it)
    {
        for (Index y = 0; y < h; ++y)
        {
            if (work && !work->Continue(WorkStage::Solve))
                return false;
            for (Index x = 0; x < w; ++x)
            {
                const std::size_t p = static_cast<std::size_t>(y * w + x);
                if (role[p] != kHole)
                {
                    continue;
                }
                float sum[4] = {0, 0, 0, 0};
                int n = 0;
                const Index neighbors[4][2] = {{x - 1, y}, {x + 1, y}, {x, y - 1}, {x, y + 1}};
                for (int k = 0; k < 4; ++k)
                {
                    const Index nx = neighbors[k][0];
                    const Index ny = neighbors[k][1];
                    if (nx < 0 || ny < 0 || nx >= w || ny >= h)
                    {
                        continue;
                    }
                    const std::size_t q = static_cast<std::size_t>(ny * w + nx);
                    if (role[q] == kOutside)
                    {
                        continue;
                    }
                    for (int c = 0; c < 4; ++c)
                    {
                        sum[c] += value[q * 4 + static_cast<std::size_t>(c)];
                    }
                    ++n;
                }
                if (!n)
                {
                    continue;
                }
                for (int c = 0; c < 4; ++c)
                {
                    float& current = value[p * 4 + static_cast<std::size_t>(c)];
                    current += omega * (sum[c] / static_cast<float>(n) - current);
                }
            }
        }
    }
    return !work || !work->cancellation.IsCancelled();
}

[[nodiscard]] bool HealUnchecked(std::uint8_t* rgba, const std::uint8_t* coverage, std::size_t width,
                                 std::size_t height, std::size_t stride, float opacity, SpotHealingMode mode,
                                 std::uint32_t seed, const Work* work)
{
    const Index W = static_cast<Index>(width);
    const Index H = static_cast<Index>(height);
    const Bounds bounds = CoverageBounds(coverage, width, height, width, work ? work->cancellation : CancelToken{});
    if (work && work->cancellation.IsCancelled())
        return false;
    if (bounds.Empty())
    {
        return true;
    }
    const Index bw = bounds.x1 - bounds.x0;
    const Index bh = bounds.y1 - bounds.y0;
    const Index size = bw > bh ? bw : bh;
    Index ring = size / 8;
    if (ring < 2)
    {
        ring = 2;
    }
    if (ring > 16)
    {
        ring = 16;
    }
    // Work box: the spot plus its ring, clipped to the image.
    const Index wx0 = bounds.x0 - ring < 0 ? 0 : bounds.x0 - ring;
    const Index wy0 = bounds.y0 - ring < 0 ? 0 : bounds.y0 - ring;
    const Index wx1 = bounds.x1 + ring > W ? W : bounds.x1 + ring;
    const Index wy1 = bounds.y1 + ring > H ? H : bounds.y1 + ring;
    const Index ww = wx1 - wx0;
    const Index wh = wy1 - wy0;
    const std::size_t wn = static_cast<std::size_t>(ww * wh);

    std::vector<std::uint8_t> role(wn, 0);
    std::vector<std::uint8_t> near(wn, 0);
    std::vector<Index> prefix(static_cast<std::size_t>((ww > wh ? ww : wh) + 1), 0);
    std::vector<float> value(wn * 4, 0.0f);
    for (Index y = 0; y < wh; ++y)
    {
        if (work && !work->Continue(WorkStage::Role))
            return false;
        for (Index x = 0; x < ww; ++x)
        {
            role[static_cast<std::size_t>(y * ww + x)] =
                coverage[static_cast<std::size_t>(wy0 + y) * width + static_cast<std::size_t>(wx0 + x)] ? kHole
                                                                                                        : kOutside;
        }
    }
    // The ring: pixels within `ring` of the spot (a square dilation, row pass then column pass).
    for (Index y = 0; y < wh; ++y)
    {
        if (work && !work->Continue(WorkStage::RingRows))
            return false;
        prefix[0] = 0;
        for (Index x = 0; x < ww; ++x)
        {
            prefix[static_cast<std::size_t>(x + 1)] =
                prefix[static_cast<std::size_t>(x)] + (role[static_cast<std::size_t>(y * ww + x)] == kHole ? 1 : 0);
        }
        for (Index x = 0; x < ww; ++x)
        {
            const Index lo = x - ring < 0 ? 0 : x - ring;
            const Index hi = x + ring + 1 > ww ? ww : x + ring + 1;
            near[static_cast<std::size_t>(y * ww + x)] =
                prefix[static_cast<std::size_t>(hi)] - prefix[static_cast<std::size_t>(lo)] > 0 ? 1 : 0;
        }
    }
    for (Index x = 0; x < ww; ++x)
    {
        if (work && !work->Continue(WorkStage::RingColumns))
            return false;
        prefix[0] = 0;
        for (Index y = 0; y < wh; ++y)
        {
            prefix[static_cast<std::size_t>(y + 1)] =
                prefix[static_cast<std::size_t>(y)] + near[static_cast<std::size_t>(y * ww + x)];
        }
        for (Index y = 0; y < wh; ++y)
        {
            const Index lo = y - ring < 0 ? 0 : y - ring;
            const Index hi = y + ring + 1 > wh ? wh : y + ring + 1;
            std::uint8_t& current = role[static_cast<std::size_t>(y * ww + x)];
            if (current == kOutside && prefix[static_cast<std::size_t>(hi)] - prefix[static_cast<std::size_t>(lo)] > 0)
            {
                current = kRing;
            }
        }
    }
    Index ringCount = 0;
    for (std::size_t p = 0; p < wn; ++p)
    {
        if (work && p % static_cast<std::size_t>(ww) == 0 && !work->Continue(WorkStage::Statistics))
            return false;
        ringCount += role[p] == kRing ? 1 : 0;
    }
    if (!ringCount)
    {
        return true;
    }

    // Source patch for Content-Aware and Proximity Match.
    Index ox = 0;
    Index oy = 0;
    bool haveSource = false;
    if (mode != SpotHealingMode::CreateTexture)
    {
        static constexpr double factors[5] = {1.05, 1.35, 1.75, 2.25, 2.8};
        const int count = mode == SpotHealingMode::ProximityMatch ? 2 : 5;
        double best = kInfinity;
        for (int f = 0; f < count; ++f)
        {
            for (int a = 0; a < 24; ++a)
            {
                if (work && !work->Continue(WorkStage::PatchSearch))
                    return false;
                const double angle = a * kPi / 12.0;
                const Index dx =
                    static_cast<Index>(std::lround(std::cos(angle) * factors[f] * static_cast<double>(ww)));
                const Index dy =
                    static_cast<Index>(std::lround(std::sin(angle) * factors[f] * static_cast<double>(wh)));
                double score = Score(rgba, stride, role.data(), wx0, wy0, ww, wh, dx, dy, W, H, work);
                if (!std::isfinite(score))
                {
                    continue;
                }
                score *=
                    mode == SpotHealingMode::ProximityMatch ? 1.0 + 0.6 * f : 1.0 + 0.1 * f; // nearer patches win ties
                if (score < best)
                {
                    best = score;
                    ox = dx;
                    oy = dy;
                }
            }
        }
        if (std::isfinite(best))
        {
            // Fine-tune the alignment so repeating texture lines up.
            const Index cx = ox;
            const Index cy = oy;
            double refined = Score(rgba, stride, role.data(), wx0, wy0, ww, wh, cx, cy, W, H, work);
            for (Index j = -3; j <= 3; ++j)
            {
                for (Index i = -3; i <= 3; ++i)
                {
                    if (work && !work->Continue(WorkStage::PatchSearch))
                        return false;
                    const double score = Score(rgba, stride, role.data(), wx0, wy0, ww, wh, cx + i, cy + j, W, H, work);
                    if (score < refined)
                    {
                        refined = score;
                        ox = cx + i;
                        oy = cy + j;
                    }
                }
            }
            haveSource = true;
        }
    }

    // Membrane: the edge difference between the original and the patch (or the original itself for a smooth
    // fill), spread across the spot.
    double mean[4] = {0, 0, 0, 0};
    double detail[3] = {0, 0, 0};
    for (Index y = 0; y < wh; ++y)
    {
        if (work && !work->Continue(WorkStage::Statistics))
            return false;
        for (Index x = 0; x < ww; ++x)
        {
            const std::size_t p = static_cast<std::size_t>(y * ww + x);
            if (role[p] != kRing)
            {
                std::memset(value.data() + p * 4, 0, 4 * sizeof(float));
                continue;
            }
            const Index ix = wx0 + x;
            const Index iy = wy0 + y;
            const std::uint8_t* t = PixelAt(rgba, stride, ix, iy);
            const std::uint8_t* s = haveSource ? PixelAt(rgba, stride, ix + ox, iy + oy) : nullptr;
            for (int c = 0; c < 4; ++c)
            {
                value[p * 4 + static_cast<std::size_t>(c)] = static_cast<float>(t[c]) - (s ? s[c] : 0);
                mean[c] += value[p * 4 + static_cast<std::size_t>(c)];
            }
            if (!haveSource)
            {
                // Fine detail around the spot: each pixel against the average of its neighbours.
                for (int c = 0; c < 3; ++c)
                {
                    double around = 0;
                    int n = 0;
                    const Index offsets[4][2] = {{ix - 1, iy}, {ix + 1, iy}, {ix, iy - 1}, {ix, iy + 1}};
                    for (int k = 0; k < 4; ++k)
                    {
                        if (offsets[k][0] < 0 || offsets[k][1] < 0 || offsets[k][0] >= W || offsets[k][1] >= H)
                        {
                            continue;
                        }
                        around += PixelAt(rgba, stride, offsets[k][0], offsets[k][1])[c];
                        ++n;
                    }
                    if (n)
                    {
                        const double d = t[c] - around / n;
                        detail[c] += d * d;
                    }
                }
            }
        }
    }
    for (int c = 0; c < 4; ++c)
    {
        mean[c] /= static_cast<double>(ringCount);
    }
    for (std::size_t p = 0; p < wn; ++p)
    {
        if (work && p % static_cast<std::size_t>(ww) == 0 && !work->Continue(WorkStage::Statistics))
            return false;
        if (role[p] == kHole)
        {
            for (int c = 0; c < 4; ++c)
            {
                value[p * 4 + static_cast<std::size_t>(c)] = static_cast<float>(mean[c]);
            }
        }
    }
    if (!Solve(value.data(), role.data(), ww, wh, 0, work))
        return false;
    for (int c = 0; c < 3; ++c)
    {
        detail[c] = std::sqrt(detail[c] / static_cast<double>(ringCount)) * 0.9;
    }

    for (Index y = 0; y < wh; ++y)
    {
        if (work && !work->Continue(WorkStage::Output))
            return false;
        for (Index x = 0; x < ww; ++x)
        {
            const std::size_t p = static_cast<std::size_t>(y * ww + x);
            if (role[p] != kHole)
            {
                continue;
            }
            const Index ix = wx0 + x;
            const Index iy = wy0 + y;
            std::uint8_t* t = PixelAt(rgba, stride, ix, iy);
            const std::uint8_t* s = haveSource ? PixelAt(rgba, stride, ix + ox, iy + oy) : nullptr;
            const double amount =
                coverage[static_cast<std::size_t>(iy) * width + static_cast<std::size_t>(ix)] / 255.0 * opacity;
            double grain = 0;
            if (!haveSource)
            {
                const std::uint32_t key = HashKey(seed ^ HashKey(static_cast<std::uint32_t>(iy * W + ix)));
                const double u1 = Unit(key);
                const double u2 = Unit(key ^ 0x68e31da4U);
                grain = std::sqrt(-2.0 * std::log(1.0 - u1)) * std::cos(2.0 * kPi * u2);
            }
            double out[4];
            for (int c = 0; c < 4; ++c)
            {
                const double healed =
                    (s ? s[c] : 0) + value[p * 4 + static_cast<std::size_t>(c)] + (c < 3 ? grain * detail[c] : 0);
                out[c] = t[c] + (healed - t[c]) * amount;
            }
            const double alpha = out[3] < 0 ? 0 : out[3] > 255 ? 255 : out[3];
            t[3] = static_cast<std::uint8_t>(std::lround(alpha));
            for (int c = 0; c < 3; ++c)
            {
                const double v = out[c] < 0 ? 0 : out[c] > t[3] ? t[3] : out[c];
                t[c] = static_cast<std::uint8_t>(std::lround(v));
            }
        }
    }
    return !work || !work->cancellation.IsCancelled();
}
} // namespace

Bounds CoverageBounds(const std::uint8_t* gray, std::size_t width, std::size_t height, std::size_t stride,
                      const CancelToken& cancellation) noexcept
{
    Index x0 = static_cast<Index>(width);
    Index y0 = static_cast<Index>(height);
    Index x1 = 0;
    Index y1 = 0;
    for (std::size_t y = 0; y < height; ++y)
    {
        if (cancellation.IsCancelled())
            return {};
        const std::uint8_t* row = gray + y * stride;
        for (std::size_t x = 0; x < width; ++x)
        {
            if (!row[x])
            {
                continue;
            }
            x0 = std::min(x0, static_cast<Index>(x));
            x1 = std::max(x1, static_cast<Index>(x) + 1);
            y0 = std::min(y0, static_cast<Index>(y));
            y1 = std::max(y1, static_cast<Index>(y) + 1);
        }
    }
    if (x1 <= x0 || y1 <= y0 || cancellation.IsCancelled())
    {
        return {};
    }
    return Bounds{x0, y0, x1, y1};
}

bool HealForWork(std::span<std::uint8_t> bgra, std::span<const std::uint8_t> coverage, std::size_t width,
                 std::size_t height, std::size_t stride, float opacity, SpotHealingMode mode, std::uint32_t seed,
                 const CancelToken& cancellation, WorkObserver observer)
{
    if (cancellation.IsCancelled() || bgra.empty() || coverage.empty() || width == 0 || height == 0 ||
        width > static_cast<std::size_t>(kMaxCanvasSide) || height > static_cast<std::size_t>(kMaxCanvasSide) ||
        width * height > static_cast<std::size_t>(kMaxLayerPixels) || stride < width * 4 ||
        (height > 1 && stride > ((std::numeric_limits<std::size_t>::max)() - width * 4) / (height - 1)) ||
        !std::isfinite(opacity) || opacity < 0 || opacity > 1 ||
        (mode != SpotHealingMode::ContentAware && mode != SpotHealingMode::CreateTexture &&
         mode != SpotHealingMode::ProximityMatch))
        return false;
    const auto pixelExtent = (height - 1) * stride + width * 4;
    if (bgra.size() < pixelExtent || coverage.size() < width * height)
        return false;
    const Work work{cancellation, observer};
    return HealUnchecked(bgra.data(), coverage.data(), width, height, stride, opacity, mode, seed, &work);
}

bool Heal(std::uint8_t* bgra, const std::uint8_t* coverage, std::size_t width, std::size_t height, std::size_t stride,
          float opacity, SpotHealingMode mode, std::uint32_t seed)
{
    if (bgra == nullptr || coverage == nullptr || width == 0 || height == 0 || stride < width * 4)
    {
        return false;
    }
    try
    {
        return HealUnchecked(bgra, coverage, width, height, stride, opacity, mode, seed, nullptr);
    }
    catch (const std::bad_alloc&)
    {
        return false;
    }
}
} // namespace SpotHealing
