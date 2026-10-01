#include "RasterEditWork.h"

#include "BrushStroke.h"
#include "RasterWork.h"
#include "Selection.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace
{
[[nodiscard]] bool AddBytes(std::uint64_t& bytes, std::uint64_t value) noexcept
{
    if (value > (std::numeric_limits<std::uint64_t>::max)() - bytes)
        return false;
    bytes += value;
    return true;
}

[[nodiscard]] bool AddProduct(std::uint64_t& bytes, std::uint64_t count, std::uint64_t size) noexcept
{
    return (size == 0 || count <= (std::numeric_limits<std::uint64_t>::max)() / size) && AddBytes(bytes, count * size);
}

[[nodiscard]] bool FrameValid(const Raster& image, RasterFormat format) noexcept
{
    return image.HasStorage() && image.Format() == format && image.Width() > 0 && image.Height() > 0 &&
           image.Width() <= kMaxCanvasSide && image.Height() <= kMaxCanvasSide && image.PixelCount() <= kMaxLayerPixels;
}

[[nodiscard]] bool InputGeometryValid(const ImageLayer& layer, int width, int height, std::int64_t pixelLimit,
                                      bool mask) noexcept
{
    if (width < 1 || height < 1 || width > kMaxCanvasSide || height > kMaxCanvasSide || pixelLimit < 1 ||
        pixelLimit > kMaxLayerPixels || !layer.transform.IsValid())
        return false;
    if (mask)
        return layer.mask && FrameValid(layer.mask->asset.image, RasterFormat::Gray8) &&
               (!layer.mask->placement || layer.mask->placement->IsValid());
    return !layer.isGroup && !layer.adjustment &&
           (!layer.asset || FrameValid(layer.asset->image, RasterFormat::Bgra8888));
}

// Fresh raster-edit patches lie on one 256-pixel grid. Splitting any old window only introduces those grid
// boundary coordinates plus its two original edges. This bounds pieces even for an old window larger than a
// tile; a 256-pixel window can span at most three intervals per axis, regardless of its offset from the grid.
[[nodiscard]] bool AddSparseWindows(std::uint64_t& bytes, const Raster& source) noexcept
{
    if (!source.IsSparse())
        return true;
    for (const RasterTile& tile : source.Tiles())
    {
        if (tile.Width() < 1 || tile.Height() < 1 || tile.Width() > kMaxCanvasSide || tile.Height() > kMaxCanvasSide)
            return false;
        const auto columns = (static_cast<std::uint64_t>(tile.Width()) + 255) / 256 + 2;
        const auto rows = (static_cast<std::uint64_t>(tile.Height()) + 255) / 256 + 2;
        // Result/vector growth, older headers, and one old-window's piece/next scratch coexist.
        if (!AddProduct(bytes, columns * rows, 8 * sizeof(RasterTile)))
            return false;
    }
    return true;
}
} // namespace

std::uint64_t RasterEditMemoryBytes(const ImageLayer& layer, const std::optional<DocumentSelection>& selection,
                                    int canvasWidth, int canvasHeight, std::int64_t pixelLimit, bool mask) noexcept
{
    if (!InputGeometryValid(layer, canvasWidth, canvasHeight, pixelLimit, mask))
        return 0;
    // A geometry-only CPU probe owns no pixel/coverage allocations and never reaches a graphics device.
    BrushStroke probe(layer, mask, BrushSettings{}, canvasWidth, canvasHeight, nullptr);
    if (!probe.IsValid())
        return 0;
    std::uint64_t bytes = 1024 * 1024;
    if (!AddProduct(bytes, layer.name.size(), 8 * sizeof(wchar_t)) ||
        (layer.asset && !AddProduct(bytes, layer.asset->name.size(), 4 * sizeof(wchar_t))) ||
        (layer.mask && !AddProduct(bytes, layer.mask->asset.name.size(), 8 * sizeof(wchar_t))) ||
        !AddBytes(bytes, 8 * sizeof(ImageLayer) + 4 * sizeof(JobToken)))
        return 0;
    if (layer.adjustment)
        for (const auto& channel : layer.adjustment->curves.channels)
            if (channel.size() > 32 || !AddProduct(bytes, channel.size(), 8 * sizeof(CurvePoint)))
                return 0;

    const auto gridPixels = static_cast<std::uint64_t>(probe.Width()) * probe.Height();
    const auto outputPixels = (std::min)(gridPixels, static_cast<std::uint64_t>(pixelLimit));
    const auto channels = mask ? 1ull : 4ull;
    // Private bases and result tile images coexist; current tile/vector/copy scratch fits the fixed allowance.
    if (!AddProduct(bytes, outputPixels, 2 * channels))
        return 0;
    const auto columns = (static_cast<std::uint64_t>((std::min)(probe.Width(), kMaxCanvasSide)) + 255) / 256 + 1;
    const auto rows = (static_cast<std::uint64_t>((std::min)(probe.Height(), kMaxCanvasSide)) + 255) / 256 + 1;
    if (!AddProduct(bytes, columns * rows, 4 * sizeof(BrushStroke::Tile) + 8 * sizeof(RasterTile) + 256))
        return 0;
    const Raster& source = mask ? layer.mask->asset.image : layer.asset ? layer.asset->image : Raster{};
    if (!AddSparseWindows(bytes, source))
        return 0;

    if (selection)
    {
        const auto coverage = PixelBuffer::RequiredBytes(static_cast<std::size_t>(canvasWidth) * canvasHeight);
        if (!AddBytes(bytes, coverage))
            return 0;
        if (selection->antialiased)
        {
            if (!AddProduct(bytes, (static_cast<std::uint64_t>(canvasWidth) + 2) * canvasHeight, 2 * sizeof(float)))
                return 0;
        }
        else if (!AddProduct(bytes, selection->loops.PointCount(), 32))
            return 0;
    }
    // A stretched target mask and a grown image's carried nonuniform mask each need a private full grid, final
    // cropped bytes and a source row. Inputs stay shared. These conservative planes include transient copies.
    if (layer.mask && (mask || (!layer.mask->placement && layer.mask->asset.image.PixelCount() > 1)))
    {
        if (!AddProduct(bytes, outputPixels, 4) || !AddSparseWindows(bytes, layer.mask->asset.image))
            return 0;
    }
    return bytes;
}

RasterEditResult RunRasterEdit(const RasterEditInput& input, CancelToken cancellation)
{
    const auto cancelled = [&]() noexcept { return RasterEditResult{RasterEditStatus::Cancelled}; };
    if (cancellation.IsCancelled())
        return cancelled();
    if (!InputGeometryValid(input.layer, input.canvasWidth, input.canvasHeight, input.pixelLimit, input.mask) ||
        input.kind > RasterEditKind::Invert ||
        std::any_of(input.color.begin(), input.color.end(),
                    [](double value) { return !std::isfinite(value) || value < 0 || value > 1; }))
        return {RasterEditStatus::Invalid};
    if ((input.kind == RasterEditKind::Clear && !input.selection) || (input.selection && input.selection->IsEmpty()))
        return {RasterEditStatus::Unchanged};

    BrushStroke edit(input.layer, input.mask, BrushSettings{}, input.canvasWidth, input.canvasHeight, nullptr);
    if (!edit.IsValid() || !edit.ConfigureWorkerRasterEdit(cancellation, input.progress, input.progressContext))
        return {RasterEditStatus::Invalid};
    edit.SetPixelLimit(input.pixelLimit);
    if (input.selection)
    {
        Raster coverage = SelectionGeometry::Coverage(input.selection->loops, input.canvasWidth, input.canvasHeight,
                                                      input.selection->antialiased, cancellation);
        if (cancellation.IsCancelled())
            return cancelled();
        if (!coverage.HasStorage())
            return {RasterEditStatus::Memory};
        edit.SetSelectionClip(std::move(coverage));
    }
    const bool rendered = input.kind == RasterEditKind::Invert                 ? edit.Invert()
                          : input.kind == RasterEditKind::Clear && !input.mask ? edit.ClearPixels()
                                                                               : edit.Fill(input.color);
    if (cancellation.IsCancelled())
        return cancelled();
    if (!rendered)
        return {edit.PixelLimitRefused() ? RasterEditStatus::TooLarge : RasterEditStatus::Memory};
    if (!edit.HasPaint())
        return {RasterEditStatus::Unchanged};
    const auto snapshot = edit.PaintSnapshot();
    if (cancellation.IsCancelled())
        return cancelled();
    if (!snapshot || !snapshot->image.HasStorage() || !snapshot->thumbnail.HasStorage())
        return {RasterEditStatus::Memory};

    ImageLayer prepared = input.layer;
    if (input.mask)
    {
        prepared.mask->asset.image = snapshot->image;
        prepared.mask->asset.thumbnail = snapshot->thumbnail;
    }
    else
    {
        if (prepared.mask && !prepared.mask->placement && snapshot->bounds != edit.SourceRect() &&
            prepared.mask->asset.image.PixelCount() > 1)
        {
            const auto& source = edit.SourceRect();
            Raster grown = CarriedMaskForWork(prepared.mask->asset.image, source.width, source.height,
                                              source.x - snapshot->bounds.x, source.y - snapshot->bounds.y,
                                              snapshot->bounds.width, snapshot->bounds.height, 255, cancellation);
            if (cancellation.IsCancelled())
                return cancelled();
            if (!grown.HasStorage())
                return {RasterEditStatus::Memory};
            Raster thumbnail = ThumbnailRasterForWork(grown, 96, cancellation);
            if (cancellation.IsCancelled())
                return cancelled();
            if (!thumbnail.HasStorage())
                return {RasterEditStatus::Memory};
            prepared.mask->asset.image = std::move(grown);
            prepared.mask->asset.thumbnail = std::move(thumbnail);
        }
        prepared.asset = RasterAsset{snapshot->image, snapshot->thumbnail, prepared.name};
        prepared.transform = snapshot->transform;
    }
    if (cancellation.IsCancelled())
        return cancelled();
    return {RasterEditStatus::Ready, std::move(prepared)};
}
