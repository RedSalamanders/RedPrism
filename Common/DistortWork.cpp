#include "DistortWork.h"

#include "Adjust.h"
#include "RasterWork.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <utility>

namespace
{
constexpr std::array<Vec2, 4> kSubSamples{Vec2{-0.25, -0.25}, Vec2{0.25, -0.25}, Vec2{-0.25, 0.25}, Vec2{0.25, 0.25}};

struct WarpPlan final
{
    LayerTransform placement;
    DistortWarp::Homography inverse;
    double factor = 1;
    int width = 0;
    int height = 0;
    int channels = 4;
};

DistortWorkStatus PlanWarp(const DistortWorkInput& input, WarpPlan& plan) noexcept
{
    if (!input.image.HasStorage() || !input.placement.IsValid() || !DistortWarp::IsUsable(input.corners) ||
        !std::isfinite(input.previewLimit) || input.previewLimit < 0 || !std::isfinite(input.background) ||
        (input.image.Format() == RasterFormat::Gray8) != input.mask || (input.mask && input.trim))
        return DistortWorkStatus::Invalid;
    const auto bounds = PointBounds(input.corners);
    const auto minX = std::floor(bounds.minX);
    const auto minY = std::floor(bounds.minY);
    const auto width = std::ceil(bounds.maxX) - minX;
    const auto height = std::ceil(bounds.maxY) - minY;
    if (!std::isfinite(width) || !std::isfinite(height) || width < 1 || height < 1)
        return DistortWorkStatus::Invalid;
    // Full bounds are checked before the preview cap, exactly as in the direct reference.
    if (width > kMaxCanvasSide || height > kMaxCanvasSide || width * height > kMaxLayerPixels)
        return DistortWorkStatus::TooLarge;
    const auto inverse = DistortWarp::MakeHomography(input.corners).Inverted();
    if (!inverse)
        return DistortWorkStatus::Invalid;
    plan.placement.origin = {minX, minY};
    plan.placement.size = {width, height};
    plan.placement.sampling = input.placement.sampling;
    plan.inverse = inverse.value();
    plan.factor = input.previewLimit > 0 ? (std::min)(1.0, input.previewLimit / (std::max)(width, height)) : 1.0;
    plan.width = (std::max)(1, static_cast<int>(std::ceil(width * plan.factor)));
    plan.height = (std::max)(1, static_cast<int>(std::ceil(height * plan.factor)));
    plan.channels = input.mask ? 1 : 4;
    return DistortWorkStatus::Ready;
}

bool Checkpoint(DistortWorkObserver observer, DistortWorkStage stage, CancelToken cancellation)
{
    if (cancellation.IsCancelled())
        return false;
    if (observer.checkpoint)
        observer.checkpoint(observer.context, stage);
    return !cancellation.IsCancelled();
}

struct Pixels final
{
    const std::uint8_t* data = nullptr;
    int width = 0;
    int height = 0;
    int stride = 0;
    int channels = 4;
};

const std::uint8_t* PixelAt(const Pixels& image, int x, int y) noexcept
{
    x = std::clamp(x, 0, image.width - 1);
    y = std::clamp(y, 0, image.height - 1);
    return image.data + static_cast<std::size_t>(y) * image.stride + static_cast<std::size_t>(x) * image.channels;
}

// The scalar operation order matches TransformEdit.cpp and legacy DistortWarp sampling. Tiles partition only
// destination coordinates; they never reorder the four edge samples or the channel interpolation.
void Sample(const Pixels& image, double fx, double fy, bool nearest, std::uint8_t* output) noexcept
{
    if (nearest)
    {
        const auto* pixel = PixelAt(image, static_cast<int>(std::floor(fx)), static_cast<int>(std::floor(fy)));
        std::memcpy(output, pixel, static_cast<std::size_t>(image.channels));
        return;
    }
    const double sx = fx - 0.5;
    const double sy = fy - 0.5;
    const int x0 = static_cast<int>(std::floor(sx));
    const int y0 = static_cast<int>(std::floor(sy));
    const double tx = sx - static_cast<double>(x0);
    const double ty = sy - static_cast<double>(y0);
    const auto* p00 = PixelAt(image, x0, y0);
    const auto* p10 = PixelAt(image, x0 + 1, y0);
    const auto* p01 = PixelAt(image, x0, y0 + 1);
    const auto* p11 = PixelAt(image, x0 + 1, y0 + 1);
    for (int channel = 0; channel < image.channels; ++channel)
    {
        const double top = p00[channel] * (1.0 - tx) + p10[channel] * tx;
        const double bottom = p01[channel] * (1.0 - tx) + p11[channel] * tx;
        const double value = top * (1.0 - ty) + bottom * ty;
        output[channel] = static_cast<std::uint8_t>(std::clamp(std::lround(value), 0L, 255L));
    }
}
} // namespace

std::uint64_t DistortWorkMemoryBytes(const DistortWorkInput& input) noexcept
{
    WarpPlan plan;
    if (PlanWarp(input, plan) != DistortWorkStatus::Ready)
        return 0;
    constexpr std::uint64_t metadata = 4096 + sizeof(DistortWorkInput) + sizeof(DistortWorkResult) + sizeof(WarpPlan);
    if (input.mask && input.image.Width() == 1 && input.image.Height() == 1)
        return metadata;
    const auto source = PixelBuffer::RequiredBytes(static_cast<std::size_t>(input.image.PixelCount()) * plan.channels);
    const auto output = PixelBuffer::RequiredBytes(static_cast<std::size_t>(plan.width) * plan.height * plan.channels);
    const auto copies = input.trim ? 2ull : 1ull;
    const auto maximum = (std::numeric_limits<std::uint64_t>::max)();
    if (!source || !output || source > maximum - metadata || output > (maximum - source - metadata) / copies)
        return 0;
    return source + output * copies + metadata;
}

DistortWorkResult RunDistortWork(const DistortWorkInput& input, Executor& executor, CancelToken cancellation,
                                 DistortWorkObserver observer)
{
    cancellation = cancellation.WithShutdown(executor.ShutdownToken());
    if (cancellation.IsCancelled())
        return {DistortWorkStatus::Cancelled};
    WarpPlan plan;
    const auto planned = PlanWarp(input, plan);
    if (planned != DistortWorkStatus::Ready)
        return {planned};
    if (!DistortWorkMemoryBytes(input))
        return {DistortWorkStatus::TooLarge};
    if (!Checkpoint(observer, DistortWorkStage::Source, cancellation))
        return {DistortWorkStatus::Cancelled};
    if (input.mask && input.image.Width() == 1 && input.image.Height() == 1)
        return {DistortWorkStatus::Ready, input.image, plan.placement};
    const auto prepared = ContiguousRasterForWork(input.image, cancellation);
    if (!prepared.HasStorage())
        return {cancellation.IsCancelled() ? DistortWorkStatus::Cancelled : DistortWorkStatus::Memory};
    const Pixels source{prepared.Data(), prepared.Width(), prepared.Height(), prepared.Stride(), plan.channels};
    if (!source.data)
        return {DistortWorkStatus::Memory};
    if (!Checkpoint(observer, DistortWorkStage::Pixels, cancellation))
        return {DistortWorkStatus::Cancelled};
    auto bytes = PixelBuffer::TryAllocate(static_cast<std::size_t>(plan.width) * plan.height * plan.channels);
    if (!bytes)
        return {DistortWorkStatus::Memory};
    const bool nearest = input.placement.sampling == LayerSampling::Nearest;
    const auto outside = input.mask && input.background >= 0.5 ? std::uint8_t{255} : std::uint8_t{0};
    auto* outputData = bytes.Data();
    const bool completed = ParallelFor(
        executor, TileGrid{static_cast<std::uint32_t>(plan.width), static_cast<std::uint32_t>(plan.height)},
        cancellation,
        [&](Tile tile, CancelToken token) noexcept
        {
            for (auto y = tile.y; y < tile.y + tile.height; ++y)
            {
                if (token.IsCancelled())
                    return false;
                const double documentY = plan.placement.origin.y + (static_cast<double>(y) + 0.5) / plan.factor;
                for (auto x = tile.x; x < tile.x + tile.width; ++x)
                {
                    auto* out = outputData + (static_cast<std::size_t>(y) * plan.width + x) * plan.channels;
                    // Initialize even pixels outside the shape: PixelBuffer deliberately has no zero-fill pass.
                    std::memset(out, outside, static_cast<std::size_t>(plan.channels));
                    const double documentX = plan.placement.origin.x + (static_cast<double>(x) + 0.5) / plan.factor;
                    int inside = 0;
                    for (const auto& offset : kSubSamples)
                    {
                        const auto unit =
                            plan.inverse.Map({documentX + offset.x / plan.factor, documentY + offset.y / plan.factor});
                        if (unit.x >= 0.0 && unit.x <= 1.0 && unit.y >= 0.0 && unit.y <= 1.0)
                            ++inside;
                    }
                    if (inside == 0)
                        continue;
                    const auto center = plan.inverse.Map({documentX, documentY});
                    const Vec2 unit{std::clamp(center.x, 0.0, 1.0), std::clamp(center.y, 0.0, 1.0)};
                    const double u = input.placement.flipX ? 1.0 - unit.x : unit.x;
                    const double v = input.placement.flipY ? 1.0 - unit.y : unit.y;
                    Sample(source, u * source.width, v * source.height, nearest, out);
                    if (inside < 4)
                    {
                        const double coverage = static_cast<double>(inside) / 4.0;
                        for (int channel = 0; channel < plan.channels; ++channel)
                        {
                            const double rest = input.mask ? static_cast<double>(outside) * (1.0 - coverage) : 0.0;
                            out[channel] = static_cast<std::uint8_t>(
                                std::clamp(std::lround(out[channel] * coverage + rest), 0L, 255L));
                        }
                    }
                }
            }
            return true;
        });
    if (!completed || cancellation.IsCancelled())
        return {DistortWorkStatus::Cancelled};
    auto image = Raster::Adopt(plan.width, plan.height, input.mask ? RasterFormat::Gray8 : RasterFormat::Bgra8888,
                               std::move(bytes));
    if (!image.HasStorage())
        return {DistortWorkStatus::Memory};
    DistortWorkResult result{DistortWorkStatus::Ready, std::move(image), plan.placement};
    if (input.trim)
    {
        if (!Checkpoint(observer, DistortWorkStage::Trim, cancellation))
            return {DistortWorkStatus::Cancelled};
        auto trimmed = Adjust::TrimmedToAlpha(result.image, result.placement, cancellation);
        if (!trimmed.image.HasStorage())
            return {cancellation.IsCancelled() ? DistortWorkStatus::Cancelled : DistortWorkStatus::Memory};
        result.image = std::move(trimmed.image);
        result.placement = trimmed.transform;
        result.cropX = trimmed.cropX;
        result.cropY = trimmed.cropY;
    }
    if (cancellation.IsCancelled())
        return {DistortWorkStatus::Cancelled};
    return result;
}
