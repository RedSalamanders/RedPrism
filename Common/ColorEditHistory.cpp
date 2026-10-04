#include "ColorEditHistory.h"
#include "Adjust.h"
#include "DocumentDraft.h"

#include <algorithm>
#include <limits>
#include <type_traits>
#include <utility>

static_assert(std::is_nothrow_swappable_v<ImageLayer>);
static_assert(std::is_nothrow_swappable_v<FilterSettings>);
static_assert(std::is_nothrow_swappable_v<std::optional<ColorEdit>>);

std::optional<PreparedColorEdit> PrepareColorEditPublication(const std::optional<CanvasDocument>& source,
                                                             const std::optional<Guid>& active,
                                                             std::uint64_t generation,
                                                             DocumentHistory::StateView history,
                                                             const ColorEditJobResult& result, CancelToken cancellation,
                                                             DocumentHistory::PrepareObserver observer)
{
    if (cancellation.IsCancelled() || !source || !active || result.status != ColorEditJobStatus::Ready ||
        !result.token.target || active != result.token.target->layerId || result.token.canvasWidth != source->width ||
        result.token.canvasHeight != source->height || !result.image.HasStorage() ||
        result.image.Format() != RasterFormat::Bgra8888 ||
        (result.thumbnail.HasStorage() && result.thumbnail.Format() != RasterFormat::Bgra8888) ||
        !result.transform.IsValid())
        return std::nullopt;
    const auto original = std::find_if(source->layers.begin(), source->layers.end(),
                                       [&](const ImageLayer& layer) { return layer.id == active; });
    const auto& target = result.token.target.value();
    if (original == source->layers.end() || !original->asset || original->isGroup || original->adjustment ||
        original->asset->image != target.image || original->transform != target.transform ||
        original->mask != target.mask || original->isGroup != target.isGroup ||
        original->adjustment != target.adjustment || original->mask.has_value() != result.mask.has_value() ||
        source->selection.has_value() != result.token.selection.has_value() ||
        (source->selection && (source->selection->antialiased != result.token.selection->antialiased ||
                               source->selection->loops.Identity() != result.token.selection->loops.Identity())))
        return std::nullopt;
    ImageLayer layer = *original;
    layer.asset = RasterAsset{result.image, result.thumbnail, layer.name};
    layer.transform = result.transform;
    layer.mask = result.mask;
    const auto index = IndexOf(source->layers, active.value());
    if (!index)
        return std::nullopt;
    auto draft = CanvasDocumentDraft::Clone(source.value(), source->layers.size(), cancellation);
    if (!draft)
        return std::nullopt;
    {
        auto layers = draft.value().EditLayers();
        layers[index.value()] = layer;
    }
    auto sealed = draft.value().Seal(cancellation);
    if (!sealed || cancellation.IsCancelled() || ValidateDocument(sealed.value()) != DocumentStatus::Ok)
        return std::nullopt;
    std::optional<CanvasDocument> afterDocument{std::move(sealed.value())};
    auto publication = DocumentHistory::PrepareEdit(std::move(history), source, active, afterDocument, active,
                                                    FilterKindName(result.kind), cancellation, observer);
    if (!publication || cancellation.IsCancelled())
        return std::nullopt;
    // The caller's complete raw result remains available for unchanged/failure and late-completion handling.
    // All result/settings/mask copies happen here, never during owner publication.
    return PreparedColorEdit(generation, result, std::move(afterDocument), std::move(publication.value()));
}

std::uint64_t ColorEditHeldMemoryBytes(const ColorEdit& edit) noexcept
{
    std::uint64_t bytes = sizeof(ColorEdit) + 1024 * 1024; // bounded adjustment lookup/control/header allowance
    const auto add = [&](std::uint64_t count, std::uint64_t size) noexcept
    {
        if ((size && count > (std::numeric_limits<std::uint64_t>::max)() / size) ||
            count * size > (std::numeric_limits<std::uint64_t>::max)() - bytes)
            return false;
        bytes += count * size;
        return true;
    };
    const auto raster = [&](const Raster& image) noexcept
    { return !image.HasStorage() || (image.RetirementMemoryBytes() && add(1, image.RetirementMemoryBytes())); };
    if (!raster(edit.original) || !raster(edit.grownImage) || !raster(edit.previewSource) ||
        !raster(edit.previewRaster) || !raster(edit.previewCoverage) ||
        (edit.selection && !add(1, edit.selection->loops.StorageBytes())))
        return 0;
    for (const auto& channel : edit.settings.curves.channels)
        if (!add(channel.capacity(), sizeof(CurvePoint)))
            return 0;
    if (edit.previewLookup)
    {
        if (!add(edit.previewLookup->cube.capacity(), sizeof(float)))
            return 0;
        for (const auto& channel : edit.previewLookup->adjustment.curves.channels)
            if (!add(channel.capacity(), sizeof(CurvePoint)))
                return 0;
    }
    if (edit.sourceToken)
    {
        const auto& token = edit.sourceToken.value();
        if (token.selection && !add(1, token.selection->loops.StorageBytes()))
            return 0;
        if (token.target)
        {
            const auto& target = token.target.value();
            if (!raster(target.image))
                return 0;
            if (target.mask && (!raster(target.mask->asset.image) || !raster(target.mask->asset.thumbnail) ||
                                !add(target.mask->asset.name.capacity() + 1, sizeof(wchar_t))))
                return 0;
            if (target.adjustment)
                for (const auto& channel : target.adjustment->curves.channels)
                    if (!add(channel.capacity(), sizeof(CurvePoint)))
                        return 0;
        }
    }
    return bytes;
}

bool DocumentSession::IsPreparedColorEditCurrent(const PreparedColorEdit& prepared) const noexcept
{
    const auto& result = prepared._result;
    return prepared._generation == _generation && history.CanPublish(prepared._history) && _colorEdit &&
           !_adjustmentEditingID && result.status == ColorEditJobStatus::Ready && result.token.target &&
           result.token.target->layerId == _colorEdit->layerID && result.token.sequence == _colorEditSequence &&
           result.kind == _colorEdit->kind && result.settings == _colorEdit->settings &&
           CanApplyJobResult(result.token, _colorEditSequence);
}

bool DocumentSession::ApplyPreparedColorEditResult(PreparedColorEdit& prepared) noexcept
{
    return ApplyColorEditResultImpl(prepared._result, &prepared);
}
