#include "ImageFilters.h"

#include "CanvasSize.h"
#include "RasterWork.h"
#include "Selection.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <memory>
#include <new>
#include <utility>

namespace ImageFilters
{
namespace
{
[[nodiscard]] std::uint8_t Round8(double value) noexcept
{
    return static_cast<std::uint8_t>(std::lround(std::clamp(value, 0.0, 255.0)));
}

[[nodiscard]] Raster BlurUnchecked(const Raster& source, double sigma, const CancelToken& cancellation)
{
    const int width = source.Width();
    const int height = source.Height();
    const int channels = source.Channels();
    const bool clamp = source.Format() == RasterFormat::Gray8;
    const Raster prepared = ContiguousRasterForWork(source, cancellation);
    const std::uint8_t* data = prepared.Data();
    if (data == nullptr)
    {
        return {};
    }
    const std::size_t stride = static_cast<std::size_t>(source.Stride());
    const int radius = std::max(1, static_cast<int>(std::ceil(sigma * 3.0)));
    const std::size_t weightCount = static_cast<std::size_t>(radius) * 2 + 1;
    const auto weights = std::make_unique_for_overwrite<double[]>(weightCount);
    double total = 0;
    for (int i = -radius; i <= radius; ++i)
    {
        if ((i & 63) == 0 && cancellation.IsCancelled())
        {
            return {};
        }
        const double w = std::exp(-(static_cast<double>(i) * i) / (2.0 * sigma * sigma));
        weights[static_cast<std::size_t>(i + radius)] = w;
        total += w;
    }
    for (std::size_t i = 0; i < weightCount; ++i)
    {
        if ((i & 255u) == 0 && cancellation.IsCancelled())
        {
            return {};
        }
        weights[i] /= total;
    }
    const std::size_t pixelCount = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    const std::size_t channelCount = static_cast<std::size_t>(channels);
    // Every element is written before the vertical pass. Avoid an uncancellable image-sized zero fill.
    const auto horizontal = std::make_unique_for_overwrite<float[]>(pixelCount * channelCount);
    for (int y = 0; y < height; ++y)
    {
        if (cancellation.IsCancelled())
        {
            return {};
        }
        const std::uint8_t* row = data + static_cast<std::size_t>(y) * stride;
        for (int x = 0; x < width; ++x)
        {
            if ((x & 7) == 0 && cancellation.IsCancelled())
            {
                return {};
            }
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
    PixelBuffer bytes = PixelBuffer::TryAllocate(pixelCount * channelCount);
    if (!bytes)
    {
        return {};
    }
    for (int y = 0; y < height; ++y)
    {
        if (cancellation.IsCancelled())
        {
            return {};
        }
        for (int x = 0; x < width; ++x)
        {
            if ((x & 7) == 0 && cancellation.IsCancelled())
            {
                return {};
            }
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
            if ((p & 255u) == 0 && cancellation.IsCancelled())
            {
                return {};
            }
            std::uint8_t* pixel = bytes.Data() + p * 4u;
            for (int c = 0; c < 3; ++c)
            {
                pixel[c] = std::min(pixel[c], pixel[3]);
            }
        }
    }
    return cancellation.IsCancelled() ? Raster{} : Raster::Adopt(width, height, source.Format(), std::move(bytes));
}

} // namespace

// Construction/computation boundary for the direct API's existing failure-as-empty contract. No result is
// published on E_OUTOFMEMORY, and worker callers treat the empty raster as a failed whole operation.
namespace
{
std::int32_t TryGaussianBlur(const Raster& source, double sigma, const CancelToken& cancellation,
                             Raster& result) noexcept
{
    try
    {
        result = BlurUnchecked(source, sigma, cancellation);
        return 0;
    }
    catch (const std::bad_alloc&)
    {
        return static_cast<std::int32_t>(0x8007000eU); // E_OUTOFMEMORY
    }
}
} // namespace

Raster GaussianBlur(const Raster& source, double sigma, const CancelToken& cancellation)
{
    // The tap count and x/y + radius must fit int before converting a floating-point radius.
    constexpr double kMaxSigma = (std::numeric_limits<int>::max() / 2 - kMaxCanvasSide) / 3.0;
    if (!source.HasStorage() || !(sigma > 0.0) || !std::isfinite(sigma) || sigma > kMaxSigma ||
        cancellation.IsCancelled())
    {
        return {};
    }
    Raster result;
    return TryGaussianBlur(source, sigma, cancellation, result) >= 0 ? std::move(result) : Raster{};
}

Raster RenderMaskPixels(const Raster& mask, const LayerTransform& transform, int width, int height,
                        std::uint8_t background)
{
    if (width < 1 || height < 1 || width > kMaxCanvasSide || height > kMaxCanvasSide || !mask.HasStorage() ||
        mask.Format() != RasterFormat::Gray8)
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
            PixelBuffer bytes =
                PixelBuffer::TryAllocate(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
            if (!bytes)
            {
                return {};
            }
            std::memset(bytes.Data(), background, bytes.Size());
            return Raster::Adopt(width, height, RasterFormat::Gray8, std::move(bytes));
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
