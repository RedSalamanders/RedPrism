#include "Kernels_Scalar_Adjust.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace
{
[[nodiscard]] std::uint32_t Mix32(std::uint32_t x) noexcept
{
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

// A value in −1…1 for an integer lattice point, fixed by the point and the seed. Two uniform halves summed give a
// triangular spread, closer to film grain than flat noise.
[[nodiscard]] float Lattice(std::int64_t ix, std::int64_t iy, std::uint32_t seed) noexcept
{
    const std::uint32_t h = Mix32(static_cast<std::uint32_t>(ix) * 0x9E3779B1U ^
                                  Mix32(static_cast<std::uint32_t>(iy) * 0x85EBCA77U ^ seed));
    return static_cast<float>(h & 0xFFFFU) / 65535.0f + static_cast<float>(h >> 16) / 65535.0f - 1.0f;
}

[[nodiscard]] float Clamp255(float value) noexcept
{
    return value < 0 ? 0 : value > 255 ? 255 : value;
}

// Uniform in [0, 1).
[[nodiscard]] float NoiseUnit(std::uint32_t key) noexcept
{
    return static_cast<float>(Mix32(key) >> 8) * (1.0f / 16777216.0f);
}

// Bilinear premultiplied sample with transparent outside; `x`, `y` in pixel-center coordinates.
void SampleBilinear(const std::uint8_t* bgra, int width, int height, int stride, double x, double y,
                    std::array<double, 4>& out) noexcept
{
    out = {0, 0, 0, 0};
    const double fx0 = std::floor(x);
    const double fy0 = std::floor(y);
    const double fx = x - fx0;
    const double fy = y - fy0;
    const long x0 = static_cast<long>(fx0);
    const long y0 = static_cast<long>(fy0);
    for (int j = 0; j < 2; ++j)
    {
        const long row = y0 + j;
        if (row < 0 || row >= height)
        {
            continue;
        }
        const double wy = j ? fy : 1 - fy;
        if (wy == 0)
        {
            continue;
        }
        const std::uint8_t* line = bgra + static_cast<std::size_t>(row) * static_cast<std::size_t>(stride);
        for (int i = 0; i < 2; ++i)
        {
            const long column = x0 + i;
            if (column < 0 || column >= width)
            {
                continue;
            }
            const double weight = wy * (i ? fx : 1 - fx);
            if (weight == 0)
            {
                continue;
            }
            const std::uint8_t* p = line + static_cast<std::size_t>(column) * 4u;
            for (int c = 0; c < 4; ++c)
            {
                out[static_cast<std::size_t>(c)] += weight * p[c];
            }
        }
    }
}
} // namespace

extern "C" __declspec(noinline) void Kernels_Scalar_Adjust_ApplyTables(std::uint8_t* bgra, int width, int height,
                                                                       int stride, const Adjust::ChannelTables& tables,
                                                                       const CancelToken& cancellation) noexcept
{
    for (int y = 0; y < height; ++y)
    {
        if (cancellation.IsCancelled())
        {
            return;
        }
        std::uint8_t* p = bgra + static_cast<std::size_t>(y) * static_cast<std::size_t>(stride);
        for (int x = 0; x < width; ++x, p += 4)
        {
            if ((x & 63) == 0 && cancellation.IsCancelled())
            {
                return;
            }
            const float alpha = p[3];
            if (alpha == 0.0f)
            {
                continue;
            }
            for (int channel = 0; channel < 3; ++channel)
            {
                // Legacy channel order red, green, blue; the bytes are blue, green, red.
                std::uint8_t& value = p[2 - channel];
                const float x255 = std::min(255.0f, static_cast<float>(value) * 255.0f / alpha);
                const int lo = static_cast<int>(x255);
                const int hi = lo < 255 ? lo + 1 : 255;
                const float* table = tables.values.data() + static_cast<std::ptrdiff_t>(channel) * 256;
                const float result = table[lo] + (table[hi] - table[lo]) * (x255 - static_cast<float>(lo));
                value = static_cast<std::uint8_t>(std::min(alpha, std::max(0.0f, std::round(result * alpha))));
            }
        }
    }
}

extern "C" __declspec(noinline) void Kernels_Scalar_Adjust_ApplyGradientMap(std::uint8_t* bgra, int width, int height,
                                                                            int stride,
                                                                            const std::array<std::uint8_t, 768>& table,
                                                                            const CancelToken& cancellation) noexcept
{
    for (int y = 0; y < height; ++y)
    {
        if (cancellation.IsCancelled())
        {
            return;
        }
        std::uint8_t* p = bgra + static_cast<std::size_t>(y) * static_cast<std::size_t>(stride);
        for (int x = 0; x < width; ++x, p += 4)
        {
            if ((x & 63) == 0 && cancellation.IsCancelled())
            {
                return;
            }
            const unsigned a = p[3];
            if (a == 0)
            {
                continue;
            }
            unsigned r = p[2];
            unsigned g = p[1];
            unsigned b = p[0];
            if (a < 255)
            {
                r = std::min(255u, (r * 255u + a / 2) / a);
                g = std::min(255u, (g * 255u + a / 2) / a);
                b = std::min(255u, (b * 255u + a / 2) / a);
            }
            const unsigned level = std::min(255u, (2126u * r + 7152u * g + 722u * b + 5000u) / 10000u);
            const std::uint8_t* color = table.data() + level * 3;
            p[2] = static_cast<std::uint8_t>((color[0] * a + 127u) / 255u);
            p[1] = static_cast<std::uint8_t>((color[1] * a + 127u) / 255u);
            p[0] = static_cast<std::uint8_t>((color[2] * a + 127u) / 255u);
        }
    }
}

extern "C" __declspec(noinline) void Kernels_Scalar_Adjust_ApplyGrain(std::uint8_t* bgra, int width, int height,
                                                                      int stride, const GrainSettings& grain,
                                                                      double originX, double originY,
                                                                      double unitsPerPixel,
                                                                      const CancelToken& cancellation) noexcept
{
    const double amount = grain.amount;
    double size = grain.size;
    const double roughness = grain.roughness;
    if (!(amount > 0) || !(unitsPerPixel > 0))
    {
        return;
    }
    if (!(size > 0))
    {
        size = 1;
    }
    const float strength = static_cast<float>(amount > 100 ? 1.0 : amount / 100.0) * 0.35f * 255.0f;
    const float rough = static_cast<float>(roughness < 0 ? 0.0 : roughness > 100 ? 1.0 : roughness / 100.0);
    const std::uint32_t fineSeed = Mix32(grain.seed ^ 0xA511E9B3U);
    for (int y = 0; y < height; ++y)
    {
        if (cancellation.IsCancelled())
        {
            return;
        }
        const double v = originY + (static_cast<double>(y) + 0.5) * unitsPerPixel;
        const double cellY = std::floor(v / size);
        float ty = static_cast<float>(v / size - cellY);
        ty = ty * ty * (3.0f - 2.0f * ty);
        const std::int64_t iy = static_cast<std::int64_t>(cellY);
        const std::int64_t fineY = static_cast<std::int64_t>(std::floor(v));
        std::uint8_t* p = bgra + static_cast<std::size_t>(y) * static_cast<std::size_t>(stride);
        for (int x = 0; x < width; ++x, p += 4)
        {
            if ((x & 63) == 0 && cancellation.IsCancelled())
            {
                return;
            }
            const unsigned a = p[3];
            if (a == 0)
            {
                continue;
            }
            const double u = originX + (static_cast<double>(x) + 0.5) * unitsPerPixel;
            const double cellX = std::floor(u / size);
            float tx = static_cast<float>(u / size - cellX);
            tx = tx * tx * (3.0f - 2.0f * tx);
            const std::int64_t ix = static_cast<std::int64_t>(cellX);
            const float n00 = Lattice(ix, iy, grain.seed);
            const float n10 = Lattice(ix + 1, iy, grain.seed);
            const float n01 = Lattice(ix, iy + 1, grain.seed);
            const float n11 = Lattice(ix + 1, iy + 1, grain.seed);
            const float top = n00 + (n10 - n00) * tx;
            const float bottom = n01 + (n11 - n01) * tx;
            // Blending neighbors narrows the spread; scaling restores about the lattice's own.
            const float smooth = (top + (bottom - top) * ty) * 1.6f;
            const float fine = Lattice(static_cast<std::int64_t>(std::floor(u)), fineY, fineSeed);
            const float noise = smooth + (fine - smooth) * rough;
            const float unpremultiply = a == 255 ? 1.0f : 255.0f / static_cast<float>(a);
            const float r = p[2] * unpremultiply;
            const float g = p[1] * unpremultiply;
            const float b = p[0] * unpremultiply;
            float level = (0.2126f * r + 0.7152f * g + 0.0722f * b) / 255.0f;
            if (level > 1)
            {
                level = 1;
            }
            // Film grain shows most in the midtones.
            const float delta = noise * strength * (0.4f + 2.4f * level * (1.0f - level));
            const float coverage = static_cast<float>(a) / 255.0f;
            p[2] = static_cast<std::uint8_t>(Clamp255(r + delta) * coverage + 0.5f);
            p[1] = static_cast<std::uint8_t>(Clamp255(g + delta) * coverage + 0.5f);
            p[0] = static_cast<std::uint8_t>(Clamp255(b + delta) * coverage + 0.5f);
        }
    }
}

extern "C" __declspec(noinline) void Kernels_Scalar_Adjust_AddNoise(std::uint8_t* bgra, int width, int height,
                                                                    int stride, float amount, bool gaussian,
                                                                    bool monochromatic, std::uint32_t seed,
                                                                    const CancelToken& cancellation) noexcept
{
    const float spread = amount / 100.0f * 127.5f;
    for (int y = 0; y < height; ++y)
    {
        if (cancellation.IsCancelled())
        {
            return;
        }
        std::uint8_t* row = bgra + static_cast<std::size_t>(y) * static_cast<std::size_t>(stride);
        for (int x = 0; x < width; ++x)
        {
            if ((x & 63) == 0 && cancellation.IsCancelled())
            {
                return;
            }
            std::uint8_t* p = row + static_cast<std::size_t>(x) * 4u;
            const unsigned alpha = p[3];
            if (alpha == 0)
            {
                continue;
            }
            const std::uint32_t base = Mix32(
                seed ^ Mix32(static_cast<std::uint32_t>(static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                                                        static_cast<std::size_t>(x))));
            for (int c = 0; c < 3; ++c)
            {
                const std::uint32_t key = monochromatic ? base : base + static_cast<std::uint32_t>(c) * 0x9e3779b9U;
                float n = 0;
                if (gaussian)
                {
                    // Box–Muller: two uniform values make one normally distributed one.
                    const float u1 = NoiseUnit(key);
                    const float u2 = NoiseUnit(key ^ 0x68e31da4U);
                    n = std::sqrt(-2.0f * std::log(1.0f - u1)) * std::cos(6.2831853f * u2) * spread * (2.0f / 3.0f);
                }
                else
                {
                    n = (NoiseUnit(key) * 2.0f - 1.0f) * spread;
                }
                std::uint8_t& byte = p[2 - c];
                float value = static_cast<float>(byte) * 255.0f / static_cast<float>(alpha) + n;
                value = value < 0 ? 0 : value > 255 ? 255 : value;
                byte = static_cast<std::uint8_t>(std::lround(value * static_cast<float>(alpha) / 255.0f));
            }
        }
    }
}

extern "C" __declspec(noinline) void Kernels_Scalar_Adjust_LensDistort(const std::uint8_t* source,
                                                                       std::uint8_t* destination, int width, int height,
                                                                       int stride, double k,
                                                                       const CancelToken& cancellation) noexcept
{
    const double cx = width * 0.5;
    const double cy = height * 0.5;
    const double halfDiagonal2 = cx * cx + cy * cy;
    for (int y = 0; y < height; ++y)
    {
        if (cancellation.IsCancelled())
        {
            return;
        }
        const double dy = y + 0.5 - cy;
        std::uint8_t* out = destination + static_cast<std::size_t>(y) * static_cast<std::size_t>(stride);
        for (int x = 0; x < width; ++x)
        {
            if ((x & 63) == 0 && cancellation.IsCancelled())
            {
                return;
            }
            const double dx = x + 0.5 - cx;
            const double scale = 1.0 - k * (dx * dx + dy * dy) / halfDiagonal2;
            std::array<double, 4> sums{};
            SampleBilinear(source, width, height, stride, cx + dx * scale - 0.5, cy + dy * scale - 0.5, sums);
            for (int c = 0; c < 4; ++c)
            {
                out[static_cast<std::size_t>(x) * 4u + static_cast<std::size_t>(c)] =
                    static_cast<std::uint8_t>(std::lround(sums[static_cast<std::size_t>(c)]));
            }
        }
    }
}

extern "C" __declspec(noinline) void Kernels_Scalar_Adjust_ApplyCube(std::uint8_t* bgra, int width, int height,
                                                                     int stride, const std::vector<float>& cube,
                                                                     const CancelToken& cancellation) noexcept
{
    if (cube.size() <
        static_cast<std::size_t>(Adjust::kCubeDimension) * Adjust::kCubeDimension * Adjust::kCubeDimension * 4)
    {
        return;
    }
    const auto at = [&](int r, int g, int b, int channel) -> float
    {
        return cube[((static_cast<std::size_t>(b) * Adjust::kCubeDimension + static_cast<std::size_t>(g)) *
                         Adjust::kCubeDimension +
                     static_cast<std::size_t>(r)) *
                        4 +
                    static_cast<std::size_t>(channel)];
    };
    const float last = static_cast<float>(Adjust::kCubeDimension - 1);
    for (int y = 0; y < height; ++y)
    {
        if (cancellation.IsCancelled())
        {
            return;
        }
        std::uint8_t* p = bgra + static_cast<std::size_t>(y) * static_cast<std::size_t>(stride);
        for (int x = 0; x < width; ++x, p += 4)
        {
            if ((x & 63) == 0 && cancellation.IsCancelled())
            {
                return;
            }
            const float alpha = p[3];
            if (alpha == 0.0f)
            {
                continue;
            }
            float coordinate[3];
            for (int c = 0; c < 3; ++c)
            {
                const float straight = std::min(1.0f, static_cast<float>(p[2 - c]) / alpha);
                coordinate[c] = straight * last;
            }
            int lo[3];
            float t[3];
            for (int c = 0; c < 3; ++c)
            {
                lo[c] = std::min(Adjust::kCubeDimension - 2, static_cast<int>(coordinate[c]));
                t[c] = coordinate[c] - static_cast<float>(lo[c]);
            }
            for (int channel = 0; channel < 3; ++channel)
            {
                float result = 0;
                for (int corner = 0; corner < 8; ++corner)
                {
                    const int dr = corner & 1;
                    const int dg = (corner >> 1) & 1;
                    const int db = (corner >> 2) & 1;
                    const float weight = (dr ? t[0] : 1 - t[0]) * (dg ? t[1] : 1 - t[1]) * (db ? t[2] : 1 - t[2]);
                    result += weight * at(lo[0] + dr, lo[1] + dg, lo[2] + db, channel);
                }
                p[2 - channel] = static_cast<std::uint8_t>(
                    std::min(alpha, std::max(0.0f, std::round(std::min(1.0f, std::max(0.0f, result)) * alpha))));
            }
        }
    }
}
