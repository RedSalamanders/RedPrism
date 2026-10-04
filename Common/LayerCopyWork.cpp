#include "LayerCopyWork.h"

#include "BrushStroke.h"
#include "CopyPixelsWork.h"
#include "DocumentDraft.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <unordered_set>

namespace
{
bool LayerCopyCheckpoint(LayerCopyObserver observer, LayerCopyStage stage, CancelToken cancellation)
{
    if (cancellation.IsCancelled())
        return false;
    if (observer.checkpoint)
        observer.checkpoint(observer.context, stage);
    return !cancellation.IsCancelled();
}

CopyPixelsInput SelectedCopy(const CanvasDocument& document, const ImageLayer& layer, LayerTransform placement,
                             bool mask)
{
    CopyPixelsInput result;
    result.canvasWidth = document.width;
    result.canvasHeight = document.height;
    result.selection = document.selection;
    result.mask = mask;
    result.placement = placement;
    if (mask && layer.mask)
    {
        result.source = layer.mask->asset.image;
        if (layer.mask->placement)
            result.background =
                static_cast<std::uint8_t>(std::lround(MaskEdgeBackground(layer.mask->asset.thumbnail) * 255));
    }
    else if (!mask && layer.asset)
        result.source = layer.asset->image;
    return result;
}

std::wstring LayerCopyName(const CanvasDocument& document, CancelToken cancellation)
{
    std::unordered_set<std::wstring> names;
    names.reserve(document.layers.size());
    for (const auto& layer : document.layers)
    {
        if (cancellation.IsCancelled())
            return {};
        names.insert(layer.name);
    }
    for (std::size_t number = 1; number <= document.layers.size() + 1; ++number)
    {
        if (cancellation.IsCancelled())
            return {};
        auto name = L"Layer " + std::to_wstring(number);
        if (!names.contains(name))
            return name;
    }
    return {};
}
} // namespace

std::uint64_t DocumentSession::LayerCopyPreparationMemoryBytes() const
{
    if (!_document || _document->layers.size() >= static_cast<std::size_t>(kMaxLayerCount) || !_activeLayerID ||
        _transformEdit || _gradient || _pixelMove || _brush || _warp || _colorEdit || _shapeDraft ||
        _selectionMoveOrigin || _lassoDraft)
        return 0;
    const auto* layer = ActiveLayer();
    if (!layer || layer->isGroup || (_document->selection && _document->selection->IsEmpty()))
        return 0;
    std::uint64_t pixels = 0;
    if (_document->selection)
    {
        const auto region = SelectionCopyRegion();
        if (!region || static_cast<std::int64_t>(region->width) * region->height > PixelBudgetLeft(false))
            return 0;
        pixels = CopyPixelsMemoryBytes(SelectedCopy(_document.value(), *layer,
                                                    _maskSelected ? MaskTransform(*layer) : DisplayedTransform(*layer),
                                                    _maskSelected));
        if (!pixels)
            return 0;
    }
    else if ((layer->asset && layer->asset->image.PixelCount() > PixelBudgetLeft(false)) ||
             (layer->mask && layer->mask->asset.image.PixelCount() > PixelBudgetLeft(true)))
        return 0;
    const auto historyBytes = history.EditMemoryBytes(_document);
    constexpr auto metadata = sizeof(LayerCopyInput) + sizeof(PreparedLayerCopy) + 4096;
    const auto maximum = (std::numeric_limits<std::uint64_t>::max)();
    const auto records = (static_cast<std::uint64_t>(_document->layers.size()) + 1) * 256ull;
    if (!historyBytes || historyBytes > maximum - metadata || records > maximum - historyBytes - metadata ||
        pixels > maximum - records - historyBytes - metadata)
        return 0;
    return pixels + historyBytes + records + metadata;
}

std::optional<LayerCopyInput> DocumentSession::PrepareLayerCopyInput() const
{
    if (!LayerCopyPreparationMemoryBytes())
        return std::nullopt;
    const auto* layer = ActiveLayer();
    return LayerCopyInput(CaptureImageImportAuthority(), _document.value(), _activeLayerID.value(),
                          _maskSelected ? MaskTransform(*layer) : DisplayedTransform(*layer), history.CaptureState(),
                          _maskSelected);
}

LayerCopyResult DocumentSession::PrepareLayerCopyPublication(LayerCopyInput input, Executor& executor,
                                                             CancelToken cancellation, LayerCopyObserver observer,
                                                             DocumentHistory::PrepareObserver historyObserver)
{
    if (!input._valid)
        return {};
    cancellation = cancellation.WithShutdown(executor.ShutdownToken());
    if (cancellation.IsCancelled())
        return {LayerCopyStatus::Cancelled};
    const auto index = IndexOf(input._source.layers, input._active);
    if (!index)
        return {};
    const auto& sourceLayer = input._source.layers[index.value()];
    if (sourceLayer.isGroup || (input._source.selection && input._source.selection->IsEmpty()))
        return {LayerCopyStatus::Unchanged};
    const bool selected = input._source.selection.has_value();
    if (!LayerCopyCheckpoint(observer, LayerCopyStage::Pixels, cancellation))
        return {LayerCopyStatus::Cancelled};
    ImageLayer layer;
    if (selected)
    {
        auto copied =
            RunCopyPixels(SelectedCopy(input._source, sourceLayer, input._placement, input._mask), cancellation);
        if (copied.status != CopyPixelsStatus::Ready || !copied.clipboard)
            return {copied.status == CopyPixelsStatus::Cancelled ? LayerCopyStatus::Cancelled
                    : copied.status == CopyPixelsStatus::Memory  ? LayerCopyStatus::Memory
                                                                 : LayerCopyStatus::Unchanged};
        const auto name = LayerCopyName(input._source, cancellation);
        if (cancellation.IsCancelled())
            return {LayerCopyStatus::Cancelled};
        if (name.empty())
            return {};
        const auto& image = copied.clipboard->image;
        layer = ImageLayer::Blank(name, {static_cast<double>(image.Width()), static_cast<double>(image.Height())});
        layer.transform.origin = copied.clipboard->origin;
        layer.parentID = sourceLayer.parentID;
        layer.asset = RasterAsset{image, {}, name}; // Decorative image thumbnails are prepared on demand.
    }
    else
    {
        layer = sourceLayer;
        layer.id = Guid::Generate();
        layer.name += L" copy";
    }
    if (!LayerCopyCheckpoint(observer, LayerCopyStage::Layer, cancellation))
        return {LayerCopyStatus::Cancelled};
    std::int64_t images = layer.asset ? layer.asset->image.PixelCount() : 0;
    std::int64_t masks = layer.mask ? layer.mask->asset.image.PixelCount() : 0;
    for (const auto& existing : input._source.layers)
    {
        if (cancellation.IsCancelled())
            return {LayerCopyStatus::Cancelled};
        images += existing.asset ? existing.asset->image.PixelCount() : 0;
        masks += existing.mask ? existing.mask->asset.image.PixelCount() : 0;
        if (images > kMaxLayerPixels || masks > kMaxMaskPixels)
            return {LayerCopyStatus::TooLarge};
    }
    const auto newActive = layer.id;
    const auto expandParent = selected ? layer.parentID : std::optional<Guid>{};
    auto draft = CanvasDocumentDraft::Clone(input._source, input._source.layers.size() + 1, cancellation);
    if (!draft)
        return {cancellation.IsCancelled() ? LayerCopyStatus::Cancelled : LayerCopyStatus::Invalid};
    if (!draft.value().Insert(index.value() + 1, std::move(layer)))
        return {cancellation.IsCancelled() ? LayerCopyStatus::Cancelled : LayerCopyStatus::Invalid};
    if (selected)
        draft.value().DocumentState().selection.reset();
    if (!LayerCopyCheckpoint(observer, LayerCopyStage::Document, cancellation))
        return {LayerCopyStatus::Cancelled};
    auto after = draft.value().Seal(cancellation);
    if (!after)
        return {cancellation.IsCancelled() ? LayerCopyStatus::Cancelled : LayerCopyStatus::Invalid};
    ImageImportBatchResult imported;
    imported.status = ProjectIoStatus::Ok;
    imported.importedDocument = std::move(after.value());
    imported.importedActiveLayerID = newActive;
    imported.historyName = selected ? L"Layer via Copy" : L"Duplicate Layer";
    auto publication =
        PrepareImageImportPublication(std::move(input._authority), std::move(input._source), input._active,
                                      std::move(input._history), std::move(imported), cancellation, historyObserver);
    if (!publication || cancellation.IsCancelled())
        return {cancellation.IsCancelled() ? LayerCopyStatus::Cancelled : LayerCopyStatus::Invalid};
    LayerCopyResult result;
    result.status = LayerCopyStatus::Ready;
    result.publication.emplace(PreparedLayerCopy(std::move(publication.value()), expandParent));
    return result;
}

bool DocumentSession::ApplyPreparedLayerCopyIfCurrent(PreparedLayerCopy& prepared) noexcept
{
    if (_transformEdit || _gradient || _pixelMove || _brush || _warp || _colorEdit || _shapeDraft ||
        _selectionMoveOrigin || _lassoDraft || !ApplyPreparedImageImportIfCurrent(prepared._publication))
        return false;
    if (prepared._expandParent)
        _collapsed.erase(prepared._expandParent.value());
    return true;
}
