#include "ColorDisplayWork.h"
#include "DocumentDraft.h"

#include <limits>
#include <utility>

namespace
{
bool Add(std::uint64_t& total, std::uint64_t count, std::uint64_t size = 1) noexcept
{
    constexpr auto maximum = (std::numeric_limits<std::uint64_t>::max)();
    if ((size && count > maximum / size) || count * size > maximum - total)
        return false;
    total += count * size;
    return true;
}

bool Checkpoint(ColorDisplayObserver observer, ColorDisplayStage stage, const CancelToken& cancellation)
{
    if (cancellation.IsCancelled())
        return false;
    if (observer.checkpoint)
        observer.checkpoint(observer.context, stage);
    return !cancellation.IsCancelled();
}
} // namespace

std::optional<ColorDisplayMemory> ColorDisplayMemoryFromBounds(const RecordOwnershipBounds& records,
                                                               std::uint64_t recordCount, std::uint64_t previewBytes,
                                                               std::uint64_t previewNodes, std::uint64_t selectionBytes,
                                                               std::uint64_t selectionLoops) noexcept
{
    if (!recordCount || recordCount > kMaxLayerCount || !records.metadataBytes ||
        records.ownershipBytes < records.metadataBytes || !previewBytes || !previewNodes)
        return std::nullopt;
    // Three metadata envelopes cover private records/children, sealing/control overlap and the largest name
    // validation temporary. 512 bytes/record cover simultaneous id/hierarchy map nodes and bucket arrays;
    // 64 KiB cover the bounded 256-entry live-mask path and validation/draft headers. Existing pixels are held,
    // not newly allocated. Retirement charges both source/display occurrences plus the preview DAG.
    ColorDisplayMemory result;
    result.additionalBytes = 65536;
    if (!Add(result.additionalBytes, records.metadataBytes, 3) || !Add(result.additionalBytes, recordCount, 512) ||
        !Add(result.retirementBytes, records.ownershipBytes, 2) || !Add(result.retirementBytes, previewBytes) ||
        !Add(result.retirementBytes, selectionBytes, 2) || !Add(result.retirementBytes, selectionLoops, 128) ||
        !Add(result.retirementBytes, 256) || !Add(result.retirementRasterNodes, records.rasterNodes, 2) ||
        !Add(result.retirementRasterNodes, previewNodes))
        return std::nullopt;
    return result;
}

std::optional<ColorDisplayMemory> ColorDisplayMemoryBytes(const CanvasDocument& source,
                                                          const ColorDisplayLayer& preview) noexcept
{
    const auto bounds = source.layers.OwnershipBounds();
    if (!bounds || !preview.transform.IsValid() || !preview.image.HasStorage() ||
        preview.image.Format() != RasterFormat::Bgra8888 || preview.image.PixelCount() < 1 ||
        preview.image.PixelCount() > kMaxLayerPixels)
        return std::nullopt;
    return ColorDisplayMemoryFromBounds(bounds.value(), source.layers.size(), preview.image.RetirementMemoryBytes(),
                                        preview.image.RetirementNodeCount(),
                                        source.selection ? source.selection->loops.StorageBytes() : 0,
                                        source.selection ? source.selection->loops.size() : 0);
}

ColorDisplayResult PrepareColorDisplay(const CanvasDocument& source, const ColorDisplayLayer& preview,
                                       CancelToken cancellation, ColorDisplayObserver observer)
{
    if (!Checkpoint(observer, ColorDisplayStage::Validation, cancellation))
        return {ColorDisplayStatus::Cancelled};
    if (!ColorDisplayMemoryBytes(source, preview) || ValidateDocument(source) != DocumentStatus::Ok)
        return {};
    if (!Checkpoint(observer, ColorDisplayStage::Draft, cancellation))
        return {ColorDisplayStatus::Cancelled};
    auto draft = CanvasDocumentDraft::Clone(source, source.layers.size(), cancellation);
    if (!draft)
        return {cancellation.IsCancelled() ? ColorDisplayStatus::Cancelled : ColorDisplayStatus::Invalid};
    bool replaced = false;
    for (auto& layer : draft->EditLayers())
    {
        if (!Checkpoint(observer, ColorDisplayStage::Layer, cancellation))
            return {ColorDisplayStatus::Cancelled};
        if (layer.id != preview.layerId)
            continue;
        if (!layer.asset || !layer.asset->image.HasStorage() || layer.asset->image.Format() != RasterFormat::Bgra8888 ||
            layer.isGroup || layer.adjustment)
            return {};
        if (layer.mask && !layer.mask->placement && preview.transform != layer.transform)
            layer.mask->placement = layer.transform;
        layer.asset->image = preview.image;
        layer.transform = preview.transform;
        replaced = true;
    }
    if (!replaced)
        return {};
    if (!Checkpoint(observer, ColorDisplayStage::Seal, cancellation))
        return {ColorDisplayStatus::Cancelled};
    auto display = draft->Seal(cancellation);
    if (!display)
        return {cancellation.IsCancelled() ? ColorDisplayStatus::Cancelled : ColorDisplayStatus::Invalid};
    // Sealing publishes an immutable root; it does not validate the document or its combined pixel budget.
    if (ValidateDocument(display.value()) != DocumentStatus::Ok)
        return {};
    if (!Checkpoint(observer, ColorDisplayStage::Publication, cancellation))
        return {ColorDisplayStatus::Cancelled};
    return {ColorDisplayStatus::Ready, std::move(display)};
}
