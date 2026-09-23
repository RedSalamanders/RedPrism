#include "ImageFilters.h"

#include "CanvasSize.h"
#include "Selection.h"

#include <algorithm>
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
    if (mask.Data() == nullptr)
    {
        // A sparse mask that could not be assembled: nothing to render, never the background in its place.
        return {};
    }
    try
    {
        if (!transform.IsValid())
        {
            const std::vector<std::uint8_t> bytes(static_cast<std::size_t>(width) * static_cast<std::size_t>(height),
                                                  background);
            return Raster::Make(width, height, RasterFormat::Gray8, bytes.data(), bytes.size());
        }
        // A transient sample, not a resample: High collapses to Smooth's single bilinear tap (no supersampling),
        // as RenderLayerPixels does for a layer's pixels.
        const LayerSampling sampling =
            transform.sampling == LayerSampling::Nearest ? LayerSampling::Nearest : LayerSampling::Smooth;
        const Affine2 pixelToTarget = PixelToDocument(transform, mask.Width(), mask.Height());
        return RenderPlacedMask(mask, pixelToTarget, width, height, sampling, background);
    }
    catch (const std::bad_alloc&)
    {
        return {};
    }
}
} // namespace ImageFilters
