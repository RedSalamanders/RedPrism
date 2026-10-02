#include "MaskEditWork.h"

#include "RasterWork.h"
#include "Selection.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace
{
bool AddMaskBytes(std::uint64_t& total, std::uint64_t bytes) noexcept
{
    if (!bytes || bytes > (std::numeric_limits<std::uint64_t>::max)() - total)
        return false;
    total += bytes;
    return true;
}

bool MaskCheckpoint(MaskWorkObserver observer, MaskWorkStage stage, CancelToken cancellation)
{
    if (cancellation.IsCancelled())
        return false;
    if (observer.checkpoint)
        observer.checkpoint(observer.context, stage);
    return !cancellation.IsCancelled();
}

MaskEditStatus MaskDimensions(const ImageLayer& layer, bool selected, int& width, int& height) noexcept
{
    width = height = 0;
    if (layer.mask)
        return MaskEditStatus::Unchanged;
    if (!selected)
    {
        width = height = 1;
        return MaskEditStatus::Ready;
    }
    const double w = layer.asset ? layer.asset->image.Width() : std::round(layer.transform.size.x);
    const double h = layer.asset ? layer.asset->image.Height() : std::round(layer.transform.size.y);
    if (!std::isfinite(w) || !std::isfinite(h) || w < 1 || h < 1)
        return MaskEditStatus::Invalid;
    if (w > kMaxCanvasSide || h > kMaxCanvasSide || w * h > kMaxMaskPixels)
        return MaskEditStatus::TooLarge;
    width = static_cast<int>(w);
    height = static_cast<int>(h);
    return MaskEditStatus::Ready;
}
} // namespace

std::uint64_t DocumentSession::MaskEditPreparationMemoryBytes() const noexcept
{
    if (!_document || !_activeLayerID || _transformEdit || _gradient || _pixelMove || _brush || _warp || _colorEdit ||
        _shapeDraft || _selectionMoveOrigin || _lassoDraft)
        return 0;
    const auto* layer = ActiveLayer();
    int width = 0;
    int height = 0;
    if (!layer || MaskDimensions(*layer, _document->selection.has_value(), width, height) != MaskEditStatus::Ready ||
        static_cast<std::int64_t>(width) * height > PixelBudgetLeft(true))
        return 0;
    const auto pixels = static_cast<std::uint64_t>(width) * height;
    const auto output = PixelBuffer::RequiredBytes(static_cast<std::size_t>(pixels));
    std::uint64_t bytes = output + 4096;
    if (_document->selection)
    {
        const auto& selection = _document->selection.value();
        std::uint64_t coverageWork = 0;
        if (selection.antialiased)
            coverageWork = 2ull * sizeof(float) * (static_cast<std::uint64_t>(width) + 2) * height;
        else
            for (const auto& loop : selection.loops)
            {
                if (loop.size() > ((std::numeric_limits<std::uint64_t>::max)() - coverageWork) / 32)
                    return 0;
                coverageWork += loop.size() * 32ull; // independent conservative bound for Crossing records
            }
        // Coverage owns one Gray8 output while cells/crossings live; conversion owns coverage plus final pixels.
        bytes = output;
        if (!AddMaskBytes(bytes, (std::max)(coverageWork, output)) ||
            !AddMaskBytes(bytes, PixelBuffer::RequiredBytes(96 * 96) + 4096))
            return 0;
    }
    const auto historyBytes = history.EditMemoryBytes(_document);
    return historyBytes && AddMaskBytes(bytes, historyBytes) &&
                   AddMaskBytes(bytes, (_document->layers.size() + 1) * (sizeof(Guid) * 8ull + 256)) &&
                   AddMaskBytes(bytes, sizeof(MaskEditInput) + sizeof(PreparedMaskEdit) + 4096)
               ? bytes
               : 0;
}

std::optional<MaskEditInput> DocumentSession::PrepareMaskEditInput(bool revealing) const
{
    if (!MaskEditPreparationMemoryBytes())
        return std::nullopt;
    return MaskEditInput(CaptureImageImportAuthority(), _document.value(), _activeLayerID.value(), _selected,
                         history.CaptureState(), revealing);
}

MaskEditResult DocumentSession::PrepareMaskEditPublication(MaskEditInput input, Executor& executor,
                                                           CancelToken cancellation, MaskWorkObserver observer,
                                                           DocumentHistory::PrepareObserver historyObserver)
{
    if (!input._valid)
        return {};
    cancellation = cancellation.WithShutdown(executor.ShutdownToken());
    if (cancellation.IsCancelled())
        return {MaskEditStatus::Cancelled};
    std::optional<std::size_t> target;
    std::int64_t used = 0;
    for (std::size_t i = 0; i < input._source.layers.size(); ++i)
    {
        if (cancellation.IsCancelled())
            return {MaskEditStatus::Cancelled};
        const auto& layer = input._source.layers[i];
        if (layer.id == input._active)
            target = i;
        if (layer.mask)
            used += layer.mask->asset.image.PixelCount();
    }
    if (!target)
        return {};
    const auto& layer = input._source.layers[target.value()];
    int width = 0;
    int height = 0;
    const bool selected = input._source.selection.has_value();
    const auto dimensions = MaskDimensions(layer, selected, width, height);
    if (dimensions != MaskEditStatus::Ready)
        return {dimensions};
    if (static_cast<std::int64_t>(width) * height > kMaxMaskPixels - used)
        return {MaskEditStatus::TooLarge};
    if (!MaskCheckpoint(observer, MaskWorkStage::Coverage, cancellation))
        return {MaskEditStatus::Cancelled};
    Raster coverage;
    if (selected)
    {
        const auto& selection = input._source.selection.value();
        const auto map = PixelToDocument(layer.transform, width, height).Inverted();
        coverage = SelectionGeometry::CoverageTransformed(selection.loops, map, width, height, selection.antialiased,
                                                          cancellation);
        if (!coverage.HasStorage())
            return {cancellation.IsCancelled() ? MaskEditStatus::Cancelled : MaskEditStatus::Memory};
    }
    if (!MaskCheckpoint(observer, MaskWorkStage::Pixels, cancellation))
        return {MaskEditStatus::Cancelled};
    const auto* inside = selected ? coverage.Data() : nullptr; // this is a private contiguous coverage
    auto bytes = PixelBuffer::TryAllocate(static_cast<std::size_t>(width) * height);
    if (!bytes)
        return {MaskEditStatus::Memory};
    const auto filled = ParallelFor(
        executor, TileGrid{static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height)}, cancellation,
        [&](Tile tile, CancelToken token) noexcept
        {
            for (auto y = tile.y; y < tile.y + tile.height; ++y)
            {
                if (token.IsCancelled())
                    return false;
                for (auto x = tile.x; x < tile.x + tile.width; ++x)
                {
                    const auto index = static_cast<std::size_t>(y) * width + x;
                    bytes.Data()[index] =
                        selected ? input._revealing ? static_cast<std::uint8_t>(255 - inside[index]) : inside[index]
                        : input._revealing ? 255
                                           : 0;
                }
            }
            return true;
        });
    if (!filled || cancellation.IsCancelled())
        return {MaskEditStatus::Cancelled};
    Raster mask = Raster::Adopt(width, height, RasterFormat::Gray8, std::move(bytes));
    if (!mask.HasStorage())
        return {MaskEditStatus::Memory};
    if (!MaskCheckpoint(observer, MaskWorkStage::Thumbnail, cancellation))
        return {MaskEditStatus::Cancelled};
    const auto thumbnail = selected ? ThumbnailRasterForWork(mask, 96, cancellation) : mask;
    if (!thumbnail.HasStorage())
        return {cancellation.IsCancelled() ? MaskEditStatus::Cancelled : MaskEditStatus::Memory};
    auto after = input._source;
    auto& edited = after.layers[target.value()];
    edited.mask = LayerMask{RasterAsset{mask, thumbnail, layer.name}, true, std::nullopt, true};
    if (selected)
        after.selection.reset();
    if (!MaskCheckpoint(observer, MaskWorkStage::Document, cancellation))
        return {MaskEditStatus::Cancelled};
    ImageImportBatchResult imported;
    imported.status = ProjectIoStatus::Ok;
    imported.importedDocument = std::move(after);
    imported.importedActiveLayerID = input._active;
    imported.historyName = selected           ? L"Add Mask from Selection"
                           : input._revealing ? L"Add Reveal-All Mask"
                                              : L"Add Hide-All Mask";
    auto publication =
        PrepareImageImportPublication(std::move(input._authority), std::move(input._source), input._active,
                                      std::move(input._history), std::move(imported), cancellation, historyObserver);
    if (!publication || cancellation.IsCancelled())
        return {cancellation.IsCancelled() ? MaskEditStatus::Cancelled : MaskEditStatus::Invalid};
    publication->_maskSelected = true;
    publication->_selected = std::move(input._selected); // adding a mask preserves the complete layer selection
    MaskEditResult result;
    result.status = MaskEditStatus::Ready;
    result.publication.emplace(PreparedMaskEdit(std::move(publication.value())));
    return result;
}

bool DocumentSession::ApplyPreparedMaskEditIfCurrent(PreparedMaskEdit& prepared) noexcept
{
    if (_transformEdit || _gradient || _pixelMove || _brush || _warp || _colorEdit || _shapeDraft ||
        _selectionMoveOrigin || _lassoDraft)
        return false;
    return ApplyPreparedImageImportIfCurrent(prepared._publication);
}
