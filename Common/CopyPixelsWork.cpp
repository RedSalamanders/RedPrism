#include "CopyPixelsWork.h"

#include "BrushStroke.h"
#include "CanvasSize.h"
#include "RasterWork.h"
#include "Selection.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <span>

namespace
{
bool Valid(const CopyPixelsInput& input) noexcept
{
    return input.canvasWidth > 0 && input.canvasHeight > 0 && input.canvasWidth <= kMaxCanvasSide &&
           input.canvasHeight <= kMaxCanvasSide && input.source.HasStorage() &&
           input.source.Format() == (input.mask ? RasterFormat::Gray8 : RasterFormat::Bgra8888) &&
           !(input.mask && input.composite) &&
           (!input.composite ||
            (input.source.Width() == input.canvasWidth && input.source.Height() == input.canvasHeight));
}

std::optional<GridRect> Region(int width, int height, const std::optional<DocumentSelection>& selection) noexcept
{
    double minX = 0;
    double minY = 0;
    double maxX = width;
    double maxY = height;
    if (selection)
    {
        const auto bounds = selection->Bounds();
        if (!bounds)
            return std::nullopt;
        minX = bounds->minX;
        minY = bounds->minY;
        maxX = bounds->maxX;
        maxY = bounds->maxY;
    }
    if (!std::isfinite(minX) || !std::isfinite(minY) || !std::isfinite(maxX) || !std::isfinite(maxY))
        return std::nullopt;
    // Preserve the direct SelectionCopyRegion tolerance before clipping or converting to integer coordinates.
    const double left = (std::max)(0.0, std::floor(minX + 0.001));
    const double top = (std::max)(0.0, std::floor(minY + 0.001));
    const double right = (std::min)(static_cast<double>(width), std::ceil(maxX - 0.001));
    const double bottom = (std::min)(static_cast<double>(height), std::ceil(maxY - 0.001));
    if (!(right - left >= 1) || !(bottom - top >= 1))
        return std::nullopt;
    return GridRect{static_cast<int>(left), static_cast<int>(top), static_cast<int>(right - left),
                    static_cast<int>(bottom - top)};
}

bool Add(std::uint64_t& bytes, std::uint64_t count) noexcept
{
    if (count > (std::numeric_limits<std::uint64_t>::max)() - bytes)
        return false;
    bytes += count;
    return true;
}

void Checkpoint(CopyPixelsObserver observer, CopyPixelsStage stage)
{
    if (observer.checkpoint)
        observer.checkpoint(observer.context, stage);
}

CopyPixelsResult Failed(CancelToken token) noexcept
{
    return {token.IsCancelled() ? CopyPixelsStatus::Cancelled : CopyPixelsStatus::Memory};
}
} // namespace

std::uint64_t CopyPixelsMemoryBytes(const CopyPixelsInput& input) noexcept
{
    if (!Valid(input))
        return 0;
    if (input.composite)
        return CopyMergedPixelsMemoryBytes(input.canvasWidth, input.canvasHeight, input.selection);
    const auto region = Region(input.canvasWidth, input.canvasHeight, input.selection);
    if (!region)
        return 0;
    const auto canvasPixels = static_cast<std::size_t>(input.canvasWidth) * input.canvasHeight;
    const auto regionPixels = static_cast<std::size_t>(region->width) * region->height;
    std::uint64_t bytes = 4096 + sizeof(CopyPixelsInput) + sizeof(CopyPixelsResult);
    if (!input.composite && (!Add(bytes, PixelBuffer::RequiredBytes(canvasPixels * (input.mask ? 1u : 4u))) ||
                             !Add(bytes, PixelBuffer::RequiredBytes(static_cast<std::size_t>(input.source.Stride()) *
                                                                    input.source.Height()))))
        return 0;
    if (!Add(bytes, PixelBuffer::RequiredBytes(regionPixels * 4u)) ||
        !Add(bytes, static_cast<std::uint64_t>(region->width) * 5u))
        return 0;
    if (input.selection)
    {
        if (!Add(bytes, PixelBuffer::RequiredBytes(canvasPixels)))
            return 0;
        if (input.selection->antialiased)
        {
            if (!Add(bytes,
                     (static_cast<std::uint64_t>(input.canvasWidth) + 2u) * input.canvasHeight * 2u * sizeof(float)))
                return 0;
        }
        else
        {
            const auto points = input.selection->loops.PointCount();
            if (points > (std::numeric_limits<std::uint64_t>::max)() / 32u || !Add(bytes, points * 32u))
                return 0;
        }
    }
    return bytes;
}

std::uint64_t CopyMergedPixelsMemoryBytes(int width, int height,
                                          const std::optional<DocumentSelection>& selection) noexcept
{
    if (width <= 0 || height <= 0 || width > kMaxCanvasSide || height > kMaxCanvasSide ||
        static_cast<std::int64_t>(width) * height > kMaxLayerPixels)
        return 0;
    const auto region = Region(width, height, selection);
    if (!region)
        return 0;
    const auto canvasPixels = static_cast<std::size_t>(width) * height;
    const auto regionPixels = static_cast<std::size_t>(region->width) * region->height;
    std::uint64_t bytes = 4096 + sizeof(CopyPixelsInput) + sizeof(CopyPixelsResult);
    if (!Add(bytes, PixelBuffer::RequiredBytes(regionPixels * 4u)) ||
        !Add(bytes, static_cast<std::uint64_t>(region->width) * 5u))
        return 0;
    if (selection)
    {
        if (!Add(bytes, PixelBuffer::RequiredBytes(canvasPixels)))
            return 0;
        if (selection->antialiased)
        {
            if (!Add(bytes, (static_cast<std::uint64_t>(width) + 2u) * height * 2u * sizeof(float)))
                return 0;
        }
        else
        {
            const auto points = selection->loops.PointCount();
            if (points > (std::numeric_limits<std::uint64_t>::max)() / 32u || !Add(bytes, points * 32u))
                return 0;
        }
    }
    return bytes;
}

CopyPixelsResult RunCopyPixels(const CopyPixelsInput& input, CancelToken cancellation, CopyPixelsObserver observer)
{
    if (cancellation.IsCancelled())
        return {CopyPixelsStatus::Cancelled};
    if (!Valid(input))
        return {};
    const auto region = Region(input.canvasWidth, input.canvasHeight, input.selection);
    if (!region || !CopyPixelsMemoryBytes(input))
        return {};
    Checkpoint(observer, CopyPixelsStage::Source);
    if (cancellation.IsCancelled())
        return Failed(cancellation);
    Raster shown;
    if (input.composite)
        shown = input.source;
    else
    {
        const auto sampling =
            input.placement.sampling == LayerSampling::Nearest ? LayerSampling::Nearest : LayerSampling::Smooth;
        // The direct sampling reference fills invalid placements with transparent color or mask background.
        const Raster source = input.placement.IsValid() ? input.source : Raster{};
        const auto map = PixelToDocument(input.placement, input.source.Width(), input.source.Height());
        shown = input.mask
                    ? RenderPlacedMask(source, map, input.canvasWidth, input.canvasHeight, sampling, input.background,
                                       cancellation)
                    : RenderPlacedImage(source, map, input.canvasWidth, input.canvasHeight, sampling, cancellation);
    }
    if (!shown.HasStorage())
        return Failed(cancellation);
    Checkpoint(observer, CopyPixelsStage::Coverage);
    if (cancellation.IsCancelled())
        return Failed(cancellation);
    Raster coverage;
    if (input.selection)
    {
        coverage = SelectionGeometry::Coverage(input.selection->loops, input.canvasWidth, input.canvasHeight,
                                               input.selection->antialiased, cancellation);
        if (!coverage.HasStorage())
            return Failed(cancellation);
    }
    PixelBuffer bytes = PixelBuffer::TryAllocate(static_cast<std::size_t>(region->width) * region->height * 4u);
    if (!bytes)
        return {CopyPixelsStatus::Memory};
    // Mask copies are opaque gray. Build only cropped BGRA output, avoiding the reference's full BGRA plane.
    auto sourceRow = input.mask ? std::make_unique_for_overwrite<std::uint8_t[]>(region->width) : nullptr;
    auto coverageRow = coverage.HasStorage() ? std::make_unique_for_overwrite<std::uint8_t[]>(region->width) : nullptr;
    const auto stride = static_cast<std::size_t>(region->width) * 4u;
    for (int y = 0; y < region->height; ++y)
    {
        Checkpoint(observer, CopyPixelsStage::Rows);
        if (cancellation.IsCancelled())
            return Failed(cancellation);
        auto* row = bytes.Data() + static_cast<std::size_t>(y) * stride;
        const GridRect window{region->x, region->y + y, region->width, 1};
        if (input.mask)
        {
            if (!ReadRasterRegionForWork(shown, window, {sourceRow.get(), static_cast<std::size_t>(region->width)},
                                         region->width, input.background, cancellation))
                return Failed(cancellation);
            for (int x = 0; x < region->width; ++x)
            {
                if ((x & 255) == 0 && cancellation.IsCancelled())
                    return Failed(cancellation);
                const auto pixel = static_cast<std::size_t>(x) * 4u;
                row[pixel] = row[pixel + 1] = row[pixel + 2] = sourceRow[x];
                row[pixel + 3] = 255;
            }
        }
        else if (!ReadRasterRegionForWork(shown, window, {row, stride}, stride, 0, cancellation))
            return Failed(cancellation);
        if (coverage.HasStorage())
        {
            if (!ReadRasterRegionForWork(coverage, window, {coverageRow.get(), static_cast<std::size_t>(region->width)},
                                         region->width, 0, cancellation))
                return Failed(cancellation);
            for (int x = 0; x < region->width; ++x)
            {
                if ((x & 255) == 0 && cancellation.IsCancelled())
                    return Failed(cancellation);
                const double c = coverageRow[x] / 255.0;
                const auto pixel = static_cast<std::size_t>(x) * 4u;
                for (std::size_t channel = 0; channel < 4; ++channel)
                    row[pixel + channel] = static_cast<std::uint8_t>(std::lround(row[pixel + channel] * c));
            }
        }
    }
    Checkpoint(observer, CopyPixelsStage::Publication);
    if (cancellation.IsCancelled())
        return Failed(cancellation);
    Raster image = Raster::Adopt(region->width, region->height, RasterFormat::Bgra8888, std::move(bytes));
    if (!image.HasStorage() || cancellation.IsCancelled())
        return Failed(cancellation);
    return {CopyPixelsStatus::Ready,
            PixelClipboard{std::move(image), Vec2{static_cast<double>(region->x), static_cast<double>(region->y)}}};
}
