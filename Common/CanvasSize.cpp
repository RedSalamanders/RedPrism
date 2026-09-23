#include "CanvasSize.h"

#include "Selection.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <new>
#include <vector>

namespace
{
constexpr double kMaxResolution = 9600.0;

[[nodiscard]] double UnitFromPixels(double pixels, double original, double resolution, CanvasUnit unit) noexcept
{
    switch (unit)
    {
    case CanvasUnit::Percent:
        return pixels / original * 100.0;
    case CanvasUnit::Inches:
        return pixels / resolution;
    case CanvasUnit::Centimeters:
        return pixels / resolution * 2.54;
    case CanvasUnit::Pixels:
    default:
        return pixels;
    }
}

[[nodiscard]] double PixelsFromUnit(double value, double original, double resolution, CanvasUnit unit) noexcept
{
    switch (unit)
    {
    case CanvasUnit::Percent:
        return value / 100.0 * original;
    case CanvasUnit::Inches:
        return value * resolution;
    case CanvasUnit::Centimeters:
        return value / 2.54 * resolution;
    case CanvasUnit::Pixels:
    default:
        return value;
    }
}

[[nodiscard]] bool SideValid(double side) noexcept
{
    return std::isfinite(side) && std::round(side) >= 1.0 && std::round(side) <= kMaxCanvasSide;
}

[[nodiscard]] std::uint8_t Round8(double value) noexcept
{
    return static_cast<std::uint8_t>(std::clamp(std::lround(value), 0L, 255L));
}

// The corners of `image`'s pixel rectangle through `map`, as the target rows and columns they can touch.
struct TargetSpan
{
    int x0 = 0;
    int x1 = -1;
    int y0 = 0;
    int y1 = -1;
    bool finite = true;
};

[[nodiscard]] TargetSpan SpanOf(const Affine2& pixelToTarget, int imageWidth, int imageHeight, int width, int height)
{
    const std::array<Vec2, 4> corners{
        pixelToTarget.Apply(Vec2{0, 0}), pixelToTarget.Apply(Vec2{static_cast<double>(imageWidth), 0}),
        pixelToTarget.Apply(Vec2{static_cast<double>(imageWidth), static_cast<double>(imageHeight)}),
        pixelToTarget.Apply(Vec2{0, static_cast<double>(imageHeight)})};
    const SelectionBounds box = PointBounds(corners);
    TargetSpan span;
    if (!std::isfinite(box.minX) || !std::isfinite(box.maxX) || !std::isfinite(box.minY) || !std::isfinite(box.maxY))
    {
        span.finite = false;
        return span;
    }
    span.x0 = std::max(0, static_cast<int>(std::floor(std::max(box.minX, -1.0))) - 1);
    span.x1 = std::min(width - 1, static_cast<int>(std::ceil(std::min(box.maxX, width + 1.0))) + 1);
    span.y0 = std::max(0, static_cast<int>(std::floor(std::max(box.minY, -1.0))) - 1);
    span.y1 = std::min(height - 1, static_cast<int>(std::ceil(std::min(box.maxY, height + 1.0))) + 1);
    return span;
}

// How many source pixels one target pixel covers along its longer axis: the supersampling grid for High.
[[nodiscard]] int SampleGrid(const Affine2& targetToPixel, LayerSampling sampling) noexcept
{
    if (sampling != LayerSampling::High)
    {
        return 1;
    }
    const double alongX = std::hypot(targetToPixel.a, targetToPixel.b);
    const double alongY = std::hypot(targetToPixel.c, targetToPixel.d);
    const double shrink = std::max(alongX, alongY);
    if (!std::isfinite(shrink) || shrink <= 1.0)
    {
        return 1;
    }
    return std::clamp(static_cast<int>(std::ceil(shrink)), 1, 4);
}

// One channel plane sampled at a source position: nearest, or bilinear with the edges clamped inside the pixels.
template <int Channels>
[[nodiscard]] double SamplePlane(const std::uint8_t* source, std::size_t stride, int imageWidth, int imageHeight,
                                 double sx, double sy, int channel, bool nearest) noexcept
{
    const auto at = [&](int px, int py) -> double
    {
        px = std::clamp(px, 0, imageWidth - 1);
        py = std::clamp(py, 0, imageHeight - 1);
        return source[static_cast<std::size_t>(py) * stride + static_cast<std::size_t>(px) * Channels +
                      static_cast<std::size_t>(channel)];
    };
    if (nearest)
    {
        return at(static_cast<int>(std::floor(sx)), static_cast<int>(std::floor(sy)));
    }
    const double cx = std::clamp(sx - 0.5, 0.0, static_cast<double>(imageWidth - 1));
    const double cy = std::clamp(sy - 0.5, 0.0, static_cast<double>(imageHeight - 1));
    const double fx0 = std::floor(cx);
    const double fy0 = std::floor(cy);
    const double tx = cx - fx0;
    const double ty = cy - fy0;
    const int ix = static_cast<int>(fx0);
    const int iy = static_cast<int>(fy0);
    const double top = at(ix, iy) + (at(ix + 1, iy) - at(ix, iy)) * tx;
    const double bottom = at(ix, iy + 1) + (at(ix + 1, iy + 1) - at(ix, iy + 1)) * tx;
    return top + (bottom - top) * ty;
}

// False when the source's pixels cannot be read (a sparse raster that could not be assembled): the caller fails
// with Memory rather than installing a transparent layer.
template <int Channels>
[[nodiscard]] bool RenderPlaced(const Raster& source, const Affine2& pixelToTarget, int width, int height,
                                LayerSampling sampling, std::vector<std::uint8_t>& bytes)
{
    const int imageWidth = source.Width();
    const int imageHeight = source.Height();
    const std::size_t stride = static_cast<std::size_t>(source.Stride());
    const std::uint8_t* pixels = source.Data();
    if (pixels == nullptr)
    {
        return false;
    }
    const TargetSpan span = SpanOf(pixelToTarget, imageWidth, imageHeight, width, height);
    if (!span.finite)
    {
        return true;
    }
    const Affine2 toPixel = pixelToTarget.Inverted();
    const int grid = SampleGrid(toPixel, sampling);
    const bool nearest = sampling == LayerSampling::Nearest;
    const double weight = 1.0 / static_cast<double>(grid * grid);
    for (int y = span.y0; y <= span.y1; ++y)
    {
        std::uint8_t* row = bytes.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(width) * Channels;
        for (int x = span.x0; x <= span.x1; ++x)
        {
            std::array<double, Channels> sum{};
            int covered = 0;
            for (int gy = 0; gy < grid; ++gy)
            {
                for (int gx = 0; gx < grid; ++gx)
                {
                    const Vec2 pixel = toPixel.Apply(Vec2{x + (gx + 0.5) / grid, y + (gy + 0.5) / grid});
                    // Beyond the source's pixels the target stays as it is (transparent, or the mask background).
                    if (!(pixel.x >= 0.0) || !(pixel.y >= 0.0) || !(pixel.x < imageWidth) || !(pixel.y < imageHeight))
                    {
                        continue;
                    }
                    ++covered;
                    for (int channel = 0; channel < Channels; ++channel)
                    {
                        sum[static_cast<std::size_t>(channel)] += SamplePlane<Channels>(
                            pixels, stride, imageWidth, imageHeight, pixel.x, pixel.y, channel, nearest);
                    }
                }
            }
            if (covered == 0)
            {
                continue;
            }
            std::uint8_t* out = row + static_cast<std::size_t>(x) * Channels;
            for (int channel = 0; channel < Channels; ++channel)
            {
                // Samples off the source count as transparent (or the mask's background) in the average.
                const double background = Channels == 1 ? static_cast<double>(out[0]) * (grid * grid - covered) : 0.0;
                out[static_cast<std::size_t>(channel)] =
                    Round8((sum[static_cast<std::size_t>(channel)] + background) * weight);
            }
        }
    }
    return true;
}

[[nodiscard]] std::int64_t UsedLayerPixels(const CanvasDocument& document) noexcept
{
    std::int64_t used = 0;
    for (const ImageLayer& layer : document.layers)
    {
        if (layer.asset.has_value())
        {
            used += static_cast<std::int64_t>(layer.asset->image.Width()) * layer.asset->image.Height();
        }
    }
    return used;
}

[[nodiscard]] bool ComponentValid(double value) noexcept
{
    return std::isfinite(value) && value >= 0.0 && value <= 1.0;
}
} // namespace

CanvasSizeDraft::CanvasSizeDraft(int width, int height, double resolution) noexcept
    : originalWidth(width), originalHeight(height), resolution(resolution), width(static_cast<double>(width)),
      height(static_cast<double>(height))
{
}

bool CanvasSizeDraft::Valid() const noexcept
{
    return SideValid(width) && SideValid(height);
}

double CanvasSizeDraft::Displayed(bool widthAxis) const noexcept
{
    const double original = static_cast<double>(widthAxis ? originalWidth : originalHeight);
    const double pixels = (widthAxis ? width : height) - (relative ? original : 0.0);
    return UnitFromPixels(pixels, original, resolution, unit);
}

void CanvasSizeDraft::Set(double value, bool widthAxis) noexcept
{
    const double original = static_cast<double>(widthAxis ? originalWidth : originalHeight);
    const double pixels = PixelsFromUnit(value, original, resolution, unit);
    const double final = pixels + (relative ? original : 0.0);
    if (widthAxis)
    {
        width = final;
        if (locked)
        {
            height = final * static_cast<double>(originalHeight) / static_cast<double>(originalWidth);
        }
    }
    else
    {
        height = final;
        if (locked)
        {
            width = final * static_cast<double>(originalWidth) / static_cast<double>(originalHeight);
        }
    }
}

int CanvasSizeDraft::RoundedWidth() const noexcept
{
    return static_cast<int>(std::round(width));
}

int CanvasSizeDraft::RoundedHeight() const noexcept
{
    return static_cast<int>(std::round(height));
}

Vec2 CanvasSizeOptions::Offset(int fromWidth, int fromHeight) const noexcept
{
    if (contentOffset.has_value())
    {
        return contentOffset.value();
    }
    return Vec2{std::floor(static_cast<double>(width - fromWidth) * static_cast<double>(anchor % 3) / 2.0),
                std::floor(static_cast<double>(height - fromHeight) * static_cast<double>(anchor / 3) / 2.0)};
}

ImageSizeDraft::ImageSizeDraft(int width, int height, double resolution) noexcept
    : originalWidth(width), originalHeight(height), width(static_cast<double>(width)),
      height(static_cast<double>(height)), resolution(resolution)
{
}

bool ImageSizeDraft::Valid() const noexcept
{
    if (!std::isfinite(resolution) || resolution < 1.0 || resolution > kMaxResolution)
    {
        return false;
    }
    if (!SideValid(width) || !SideValid(height))
    {
        return false;
    }
    return !resample || std::round(width) * std::round(height) <= static_cast<double>(kMaxLayerPixels);
}

double ImageSizeDraft::Displayed(bool widthAxis) const noexcept
{
    return UnitFromPixels(widthAxis ? width : height, static_cast<double>(widthAxis ? originalWidth : originalHeight),
                          resolution, unit);
}

void ImageSizeDraft::Set(double value, bool widthAxis) noexcept
{
    if (!std::isfinite(value) || value <= 0.0)
    {
        return;
    }
    if (!resample)
    {
        resolution = (widthAxis ? width : height) / value * (unit == CanvasUnit::Centimeters ? 2.54 : 1.0);
        return;
    }
    const double pixels =
        PixelsFromUnit(value, static_cast<double>(widthAxis ? originalWidth : originalHeight), resolution, unit);
    if (widthAxis)
    {
        if (locked)
        {
            height = pixels * height / width;
        }
        width = pixels;
    }
    else
    {
        if (locked)
        {
            width = pixels * width / height;
        }
        height = pixels;
    }
}

void ImageSizeDraft::SetResolution(double value) noexcept
{
    const double old = resolution;
    resolution = value;
    if (resample && (unit == CanvasUnit::Inches || unit == CanvasUnit::Centimeters) && old > 0.0 && value > 0.0 &&
        std::isfinite(value))
    {
        width *= value / old;
        height *= value / old;
    }
}

void ImageSizeDraft::SetResample(bool enabled) noexcept
{
    resample = enabled;
    if (!enabled)
    {
        width = static_cast<double>(originalWidth);
        height = static_cast<double>(originalHeight);
        locked = true;
        if (unit == CanvasUnit::Pixels || unit == CanvasUnit::Percent)
        {
            unit = CanvasUnit::Inches;
        }
    }
}

int ImageSizeDraft::RoundedWidth() const noexcept
{
    return static_cast<int>(std::round(width));
}

int ImageSizeDraft::RoundedHeight() const noexcept
{
    return static_cast<int>(std::round(height));
}

Raster RenderPlacedImage(const Raster& image, const Affine2& pixelToTarget, int width, int height,
                         LayerSampling sampling)
{
    if (width < 1 || height < 1)
    {
        return {};
    }
    try
    {
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(width) * 4u * static_cast<std::size_t>(height), 0);
        if (image.HasStorage() && image.Format() == RasterFormat::Bgra8888 &&
            !RenderPlaced<4>(image, pixelToTarget, width, height, sampling, bytes))
        {
            return {};
        }
        return Raster::Make(width, height, RasterFormat::Bgra8888, bytes.data(), bytes.size());
    }
    catch (const std::bad_alloc&)
    {
        return {};
    }
}

Raster RenderPlacedMask(const Raster& mask, const Affine2& pixelToTarget, int width, int height, LayerSampling sampling,
                        std::uint8_t background)
{
    if (width < 1 || height < 1)
    {
        return {};
    }
    try
    {
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), background);
        if (mask.HasStorage() && mask.Format() == RasterFormat::Gray8 &&
            !RenderPlaced<1>(mask, pixelToTarget, width, height, sampling, bytes))
        {
            return {};
        }
        return Raster::Make(width, height, RasterFormat::Gray8, bytes.data(), bytes.size());
    }
    catch (const std::bad_alloc&)
    {
        return {};
    }
}

ResizeStatus ResizeCanvas(const CanvasDocument& source, const CanvasSizeOptions& options, CanvasDocument& result)
{
    if (options.width < 1 || options.width > kMaxCanvasSide || options.height < 1 || options.height > kMaxCanvasSide ||
        options.anchor < 0 || options.anchor > 8)
    {
        return ResizeStatus::TooLarge;
    }
    const Vec2 offset = options.Offset(source.width, source.height);
    if (!std::isfinite(offset.x) || !std::isfinite(offset.y) || std::abs(offset.x) > kMaxTransformOrigin ||
        std::abs(offset.y) > kMaxTransformOrigin)
    {
        return ResizeStatus::Invalid;
    }
    if (options.width == source.width && options.height == source.height && offset.x == 0.0 && offset.y == 0.0)
    {
        result = source;
        return ResizeStatus::Unchanged;
    }
    try
    {
        CanvasDocument next = source;
        next.width = options.width;
        next.height = options.height;
        next.selection.reset();
        for (ImageLayer& layer : next.layers)
        {
            layer.transform.origin.x += offset.x;
            layer.transform.origin.y += offset.y;
            if (!layer.transform.IsValid())
            {
                return ResizeStatus::TooLarge;
            }
            if (layer.mask.has_value() && layer.mask->placement.has_value())
            {
                layer.mask->placement->origin.x += offset.x;
                layer.mask->placement->origin.y += offset.y;
            }
        }
        // A colored extension is separate bottom-layer content; the old canvas intersection stays transparent,
        // including holes in the existing artwork.
        if (options.fill.has_value() && (options.width > source.width || options.height > source.height))
        {
            const std::int64_t pixels = static_cast<std::int64_t>(options.width) * options.height;
            if (pixels > static_cast<std::int64_t>(kMaxLayerPixels) - UsedLayerPixels(source) ||
                static_cast<int>(next.layers.size()) >= kMaxLayerCount)
            {
                return ResizeStatus::TooLarge;
            }
            const CanvasExtensionColor& color = options.fill.value();
            if (!ComponentValid(color.red) || !ComponentValid(color.green) || !ComponentValid(color.blue))
            {
                return ResizeStatus::Invalid;
            }
            const std::array<std::uint8_t, 4> bgra{Round8(color.blue * 255.0), Round8(color.green * 255.0),
                                                   Round8(color.red * 255.0), 255};
            std::vector<std::uint8_t> bytes(static_cast<std::size_t>(pixels) * 4u);
            for (std::size_t index = 0; index + 3 < bytes.size(); index += 4)
            {
                std::memcpy(bytes.data() + index, bgra.data(), 4);
            }
            const int holeLeft = static_cast<int>(offset.x);
            const int holeTop = static_cast<int>(offset.y);
            const int clearX0 = std::clamp(holeLeft, 0, options.width);
            const int clearX1 = std::clamp(holeLeft + source.width, 0, options.width);
            const int clearY0 = std::clamp(holeTop, 0, options.height);
            const int clearY1 = std::clamp(holeTop + source.height, 0, options.height);
            for (int y = clearY0; y < clearY1; ++y)
            {
                if (clearX1 > clearX0)
                {
                    std::memset(bytes.data() + (static_cast<std::size_t>(y) * options.width + clearX0) * 4u, 0,
                                static_cast<std::size_t>(clearX1 - clearX0) * 4u);
                }
            }
            const Raster image =
                Raster::Make(options.width, options.height, RasterFormat::Bgra8888, bytes.data(), bytes.size());
            if (!image.HasStorage())
            {
                return ResizeStatus::Memory;
            }
            ImageLayer extension = ImageLayer::Blank(
                L"Canvas Extension", Vec2{static_cast<double>(options.width), static_cast<double>(options.height)});
            extension.asset = RasterAsset{image, Raster::MakeThumbnail(image), extension.name};
            next.layers.insert(next.layers.begin(), std::move(extension));
        }
        result = std::move(next);
        return ResizeStatus::Ok;
    }
    catch (const std::bad_alloc&)
    {
        return ResizeStatus::Memory;
    }
}

ResizeStatus ResizeImage(const CanvasDocument& source, const ImageSizeOptions& options, CanvasDocument& result)
{
    if (options.width < 1 || options.width > kMaxCanvasSide || options.height < 1 || options.height > kMaxCanvasSide ||
        !std::isfinite(options.resolution) || options.resolution < 1.0 || options.resolution > kMaxResolution)
    {
        return ResizeStatus::TooLarge;
    }
    try
    {
        CanvasDocument next = source;
        next.resolution = options.resolution;
        if (options.width == source.width && options.height == source.height)
        {
            result = std::move(next);
            return ResizeStatus::Ok;
        }
        if (static_cast<std::int64_t>(options.width) * options.height > kMaxLayerPixels)
        {
            return ResizeStatus::TooLarge;
        }
        next.width = options.width;
        next.height = options.height;
        next.selection.reset();
        const double sx = static_cast<double>(options.width) / static_cast<double>(source.width);
        const double sy = static_cast<double>(options.height) / static_cast<double>(source.height);
        const Affine2 scale = Affine2::Scale(sx, sy);
        std::int64_t usedPixels = 0;
        std::int64_t usedMaskPixels = 0;
        for (ImageLayer& layer : next.layers)
        {
            // Each layer rasterizes on its own: the box its scaled corners span becomes its upright transform.
            const std::array<Vec2, 4> corners = layer.transform.Corners();
            const std::array<Vec2, 4> scaledCorners{
                Vec2{corners[0].x * sx, corners[0].y * sy}, Vec2{corners[1].x * sx, corners[1].y * sy},
                Vec2{corners[2].x * sx, corners[2].y * sy}, Vec2{corners[3].x * sx, corners[3].y * sy}};
            const SelectionBounds box = PointBounds(scaledCorners);
            if (!std::isfinite(box.minX) || !std::isfinite(box.maxX) || !std::isfinite(box.minY) ||
                !std::isfinite(box.maxY))
            {
                return ResizeStatus::TooLarge;
            }
            const double left = std::floor(box.minX);
            const double top = std::floor(box.minY);
            const int boxWidth = static_cast<int>(std::ceil(box.maxX) - left);
            const int boxHeight = static_cast<int>(std::ceil(box.maxY) - top);
            LayerTransform placed;
            placed.origin = Vec2{left, top};
            placed.size = Vec2{static_cast<double>(boxWidth), static_cast<double>(boxHeight)};
            placed.sampling = options.sampling;
            if (!placed.IsValid())
            {
                return ResizeStatus::TooLarge;
            }
            const Affine2 toTarget = Affine2::Translation(-left, -top);
            if (layer.asset.has_value())
            {
                if (boxWidth < 1 || boxWidth > kMaxCanvasSide || boxHeight < 1 || boxHeight > kMaxCanvasSide ||
                    static_cast<std::int64_t>(boxWidth) * boxHeight > kMaxLayerPixels - usedPixels)
                {
                    return ResizeStatus::TooLarge;
                }
                usedPixels += static_cast<std::int64_t>(boxWidth) * boxHeight;
                const Raster& image = layer.asset->image;
                const Affine2 map =
                    PixelToDocument(layer.transform, image.Width(), image.Height()).Concat(scale).Concat(toTarget);
                const Raster rendered = RenderPlacedImage(image, map, boxWidth, boxHeight, options.sampling);
                if (!rendered.HasStorage())
                {
                    return ResizeStatus::Memory;
                }
                layer.asset = RasterAsset{rendered, Raster::MakeThumbnail(rendered), layer.asset->name};
                layer.shapeImage = Raster{}; // resized pixels are plain pixels
            }
            if (layer.mask.has_value())
            {
                LayerMask& mask = layer.mask.value();
                const Raster& pixels = mask.asset.image;
                if (mask.placement.has_value())
                {
                    // A mask on its own placement keeps its pixels; the placement scales with the canvas.
                    mask.placement = mask.placement->Placing(mask.placement->UnitToDocument().Concat(scale));
                }
                else if (pixels.HasStorage() && !(pixels.Width() == 1 && pixels.Height() == 1))
                {
                    if (boxWidth < 1 || boxHeight < 1 ||
                        static_cast<std::int64_t>(boxWidth) * boxHeight > kMaxMaskPixels - usedMaskPixels)
                    {
                        return ResizeStatus::TooLarge;
                    }
                    usedMaskPixels += static_cast<std::int64_t>(boxWidth) * boxHeight;
                    const Affine2 map = PixelToDocument(layer.transform, pixels.Width(), pixels.Height())
                                            .Concat(scale)
                                            .Concat(toTarget);
                    const Raster rendered = RenderPlacedMask(pixels, map, boxWidth, boxHeight, options.sampling, 0);
                    if (!rendered.HasStorage())
                    {
                        return ResizeStatus::Memory;
                    }
                    mask.asset = RasterAsset{rendered, Raster::MakeThumbnail(rendered), mask.asset.name};
                }
            }
            layer.transform = placed;
        }
        result = std::move(next);
        return ResizeStatus::Ok;
    }
    catch (const std::bad_alloc&)
    {
        return ResizeStatus::Memory;
    }
}
