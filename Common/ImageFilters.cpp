#include "ImageFilters.h"

#include "Selection.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <new>
#include <vector>

namespace ImageFilters
{
namespace
{
[[nodiscard]] std::uint8_t Round8(double value) noexcept
{
    return static_cast<std::uint8_t>(std::lround(std::clamp(value, 0.0, 255.0)));
}

[[nodiscard]] Raster BlurUnchecked(const Raster& source, double sigma)
{
    const int width = source.Width();
    const int height = source.Height();
    const int channels = source.Channels();
    const bool clamp = source.Format() == RasterFormat::Gray8;
    const std::uint8_t* data = source.Data();
    if (data == nullptr)
    {
        return {};
    }
    const std::size_t stride = static_cast<std::size_t>(source.Stride());
    const int radius = std::max(1, static_cast<int>(std::ceil(sigma * 3.0)));
    std::vector<double> weights(static_cast<std::size_t>(radius) * 2 + 1);
    double total = 0;
    for (int i = -radius; i <= radius; ++i)
    {
        const double w = std::exp(-(static_cast<double>(i) * i) / (2.0 * sigma * sigma));
        weights[static_cast<std::size_t>(i + radius)] = w;
        total += w;
    }
    for (double& w : weights)
    {
        w /= total;
    }
    const std::size_t pixelCount = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    const std::size_t channelCount = static_cast<std::size_t>(channels);
    std::vector<float> horizontal(pixelCount * channelCount, 0.0f);
    for (int y = 0; y < height; ++y)
    {
        const std::uint8_t* row = data + static_cast<std::size_t>(y) * stride;
        for (int x = 0; x < width; ++x)
        {
            for (int c = 0; c < channels; ++c)
            {
                double sum = 0;
                for (int i = -radius; i <= radius; ++i)
                {
                    int sx = x + i;
                    if (sx < 0 || sx >= width)
                    {
                        if (!clamp)
                        {
                            continue;
                        }
                        sx = std::clamp(sx, 0, width - 1);
                    }
                    sum += weights[static_cast<std::size_t>(i + radius)] *
                           row[static_cast<std::size_t>(sx) * channelCount + static_cast<std::size_t>(c)];
                }
                horizontal[(static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                            static_cast<std::size_t>(x)) *
                               channelCount +
                           static_cast<std::size_t>(c)] = static_cast<float>(sum);
            }
        }
    }
    std::vector<std::uint8_t> bytes(pixelCount * channelCount, 0);
    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            for (int c = 0; c < channels; ++c)
            {
                double sum = 0;
                for (int i = -radius; i <= radius; ++i)
                {
                    int sy = y + i;
                    if (sy < 0 || sy >= height)
                    {
                        if (!clamp)
                        {
                            continue;
                        }
                        sy = std::clamp(sy, 0, height - 1);
                    }
                    sum += weights[static_cast<std::size_t>(i + radius)] *
                           horizontal[(static_cast<std::size_t>(sy) * static_cast<std::size_t>(width) +
                                       static_cast<std::size_t>(x)) *
                                          channelCount +
                                      static_cast<std::size_t>(c)];
                }
                bytes[(static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)) *
                          channelCount +
                      static_cast<std::size_t>(c)] = Round8(sum);
            }
        }
    }
    if (!clamp)
    {
        // Premultiplied pixels stay premultiplied: rounding can leave a color byte past its alpha.
        for (std::size_t p = 0; p < pixelCount; ++p)
        {
            std::uint8_t* pixel = bytes.data() + p * 4u;
            for (int c = 0; c < 3; ++c)
            {
                pixel[c] = std::min(pixel[c], pixel[3]);
            }
        }
    }
    return Raster::Make(width, height, source.Format(), bytes.data(), bytes.size());
}

[[nodiscard]] Raster RenderMaskUnchecked(const Raster& mask, const LayerTransform& transform, int width, int height,
                                         std::uint8_t background)
{
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), background);
    const std::uint8_t* source = mask.Data();
    if (source == nullptr)
    {
        // A sparse mask that could not be assembled: nothing to render, never the background in its place.
        return {};
    }
    if (!transform.IsValid())
    {
        return Raster::Make(width, height, RasterFormat::Gray8, bytes.data(), bytes.size());
    }
    const int maskWidth = mask.Width();
    const int maskHeight = mask.Height();
    const std::size_t maskStride = static_cast<std::size_t>(mask.Stride());
    const Affine2 toPixel = PixelToDocument(transform, maskWidth, maskHeight).Inverted();
    const std::array<Vec2, 4> corners = transform.Corners();
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
        return Raster::Make(width, height, RasterFormat::Gray8, bytes.data(), bytes.size());
    }
    const int x0 = std::max(0, static_cast<int>(std::floor(std::max(minX, -1.0))) - 1);
    const int x1 = std::min(width - 1, static_cast<int>(std::ceil(std::min(maxX, width + 1.0))) + 1);
    const int y0 = std::max(0, static_cast<int>(std::floor(std::max(minY, -1.0))) - 1);
    const int y1 = std::min(height - 1, static_cast<int>(std::ceil(std::min(maxY, height + 1.0))) + 1);
    const auto sample = [&](int px, int py) -> double
    {
        px = std::clamp(px, 0, maskWidth - 1);
        py = std::clamp(py, 0, maskHeight - 1);
        return source[static_cast<std::size_t>(py) * maskStride + static_cast<std::size_t>(px)];
    };
    for (int y = y0; y <= y1; ++y)
    {
        std::uint8_t* row = bytes.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(width);
        for (int x = x0; x <= x1; ++x)
        {
            const Vec2 pixel = toPixel.Apply(Vec2{x + 0.5, y + 0.5});
            if (!(pixel.x >= 0.0) || !(pixel.y >= 0.0) || !(pixel.x < maskWidth) || !(pixel.y < maskHeight))
            {
                continue;
            }
            const double sx = std::clamp(pixel.x - 0.5, 0.0, static_cast<double>(maskWidth - 1));
            const double sy = std::clamp(pixel.y - 0.5, 0.0, static_cast<double>(maskHeight - 1));
            const double fx0 = std::floor(sx);
            const double fy0 = std::floor(sy);
            const double tx = sx - fx0;
            const double ty = sy - fy0;
            const int ix = static_cast<int>(fx0);
            const int iy = static_cast<int>(fy0);
            const double top = sample(ix, iy) + (sample(ix + 1, iy) - sample(ix, iy)) * tx;
            const double bottom = sample(ix, iy + 1) + (sample(ix + 1, iy + 1) - sample(ix, iy + 1)) * tx;
            row[static_cast<std::size_t>(x)] = Round8(top + (bottom - top) * ty);
        }
    }
    return Raster::Make(width, height, RasterFormat::Gray8, bytes.data(), bytes.size());
}
} // namespace

Raster GaussianBlur(const Raster& source, double sigma)
{
    if (!source.HasStorage() || !(sigma > 0.0) || !std::isfinite(sigma))
    {
        return {};
    }
    try
    {
        return BlurUnchecked(source, sigma);
    }
    catch (const std::bad_alloc&)
    {
        return {};
    }
}

Raster RenderMaskPixels(const Raster& mask, const LayerTransform& transform, int width, int height,
                        std::uint8_t background)
{
    if (width < 1 || height < 1 || !mask.HasStorage() || mask.Format() != RasterFormat::Gray8)
    {
        return {};
    }
    try
    {
        return RenderMaskUnchecked(mask, transform, width, height, background);
    }
    catch (const std::bad_alloc&)
    {
        return {};
    }
}
} // namespace ImageFilters
