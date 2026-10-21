#include "TransformPreviewWork.h"
#include "DocumentDraft.h"

#include "FloatingPixelsWork.h"

#include <algorithm>
#include <limits>
#include <type_traits>
#include <utility>

namespace
{
constexpr double kPreviewLimit = 1024;

bool Add(std::uint64_t& bytes, std::uint64_t count, std::uint64_t size = 1) noexcept
{
    const auto maximum = (std::numeric_limits<std::uint64_t>::max)();
    if ((size && count > maximum / size) || count * size > maximum - bytes)
        return false;
    bytes += count * size;
    return true;
}

std::optional<LayerTransform> Pending(const TransformEdit& edit, const ImageLayer& layer) noexcept
{
    if (edit.mask)
        return std::nullopt;
    if (edit.group)
    {
        for (const auto& [id, original] : edit.group->originals)
            if (id == layer.id)
                return original.Following(edit.group->box, edit.draft);
        return std::nullopt;
    }
    return edit.layerID == layer.id ? std::optional(edit.draft) : std::nullopt;
}

bool Checkpoint(TransformPreviewObserver observer, TransformPreviewStage stage, CancelToken cancellation)
{
    if (cancellation.IsCancelled())
        return false;
    if (observer.checkpoint)
        observer.checkpoint(observer.context, stage);
    return !cancellation.IsCancelled();
}

DistortWorkInput MaskWarp(const LayerMask& mask, const LayerTransform& placement,
                          const std::array<Vec2, 4>& corners) noexcept
{
    return {mask.asset.image, placement, corners, true, MaskEdgeBackground(mask.asset.thumbnail), kPreviewLimit};
}

bool Failed(DistortWorkStatus status) noexcept
{
    return status == DistortWorkStatus::Memory || status == DistortWorkStatus::Cancelled;
}
} // namespace

std::uint64_t TransformDisplayMemoryBytes(const CanvasDocument& source, const TransformEdit& edit) noexcept
{
    if (!edit.draft.IsValid() || source.layers.size() > kMaxLayerCount ||
        (edit.group && (edit.mask || !edit.group->box.IsValid() || edit.group->originals.empty() ||
                        edit.group->originals.size() > static_cast<std::size_t>(kMaxLayerCount))) ||
        !IndexOf(source.layers, edit.layerID))
        return 0;
    std::uint64_t bytes = 8192 + sizeof(TransformDisplayResult);
    if (edit.floating && (edit.mask || edit.group || !edit.floating->original.IsValid() ||
                          edit.floating->pixelWidth < 1 || edit.floating->pixelHeight < 1 ||
                          edit.floating->pixelWidth > kMaxCanvasSide || edit.floating->pixelHeight > kMaxCanvasSide ||
                          (edit.corners && !DistortWarp::IsUsable(edit.corners.value())) ||
                          (source.selection && !Add(bytes, source.selection->loops.StorageBytes(), 3))))
        return 0;
    if (edit.group && !Add(bytes, edit.group->originals.size(), sizeof(std::pair<Guid, LayerTransform>) * 2ull))
        return 0;
    for (const auto& layer : source.layers)
    {
        if ((Pending(edit, layer) && layer.asset && layer.asset->image.Format() != RasterFormat::Bgra8888) ||
            ((Pending(edit, layer) || (edit.mask && layer.id == edit.layerID)) && layer.mask &&
             layer.mask->asset.image.Format() != RasterFormat::Gray8))
            return 0;
        // Input capture and the completed private display both own record/string/curve storage. Pixel sources
        // are immutable handles; sparse assembly and destination pixels are declared separately below.
        if (!Add(bytes, 2, sizeof(ImageLayer) + 256) || !Add(bytes, layer.name.size() + 1, 4 * sizeof(wchar_t)) ||
            (layer.asset && !Add(bytes, layer.asset->name.size() + 1, 4 * sizeof(wchar_t))) ||
            (layer.mask && !Add(bytes, layer.mask->asset.name.size() + 1, 4 * sizeof(wchar_t))))
            return 0;
        if (layer.adjustment)
            for (const auto& channel : layer.adjustment->curves.channels)
                if (!Add(bytes, channel.size(), 4 * sizeof(CurvePoint)))
                    return 0;
        if (!edit.corners)
            continue;
        if (edit.mask)
        {
            if (layer.id == edit.layerID && layer.mask &&
                !Add(bytes, DistortWorkMemoryBytes(MaskWarp(layer.mask.value(), edit.draft, edit.corners.value()))))
                return 0;
            continue;
        }
        const auto pending = Pending(edit, layer);
        if (!pending || !layer.asset)
            continue;
        const auto corners =
            edit.group ? DistortWarp::Carried(pending.value(), edit.draft, edit.corners.value()) : edit.corners.value();
        if (!Add(bytes,
                 DistortWorkMemoryBytes({layer.asset->image, pending.value(), corners, false, 0, kPreviewLimit})))
            return 0;
        if (layer.mask && layer.mask->linked)
        {
            const auto placement = layer.mask->placement
                                       ? layer.mask->placement->Following(layer.transform, pending.value())
                                       : pending.value();
            const auto carried =
                layer.mask->placement ? DistortWarp::Carried(placement, pending.value(), corners) : corners;
            if (!Add(bytes, DistortWorkMemoryBytes(MaskWarp(layer.mask.value(), placement, carried))))
                return 0;
        }
    }
    return bytes;
}

TransformDisplayResult PrepareTransformDisplay(const CanvasDocument& source, const TransformEdit& edit,
                                               Executor& executor, CancelToken cancellation,
                                               TransformPreviewObserver observer)
{
    cancellation = cancellation.WithShutdown(executor.ShutdownToken());
    if (!Checkpoint(observer, TransformPreviewStage::Snapshot, cancellation))
        return {DistortWorkStatus::Cancelled};
    if (!TransformDisplayMemoryBytes(source, edit) || ValidateDocument(source) != DocumentStatus::Ok)
        return {};
    if (edit.group)
        for (std::size_t i = 0; i < edit.group->originals.size(); ++i)
        {
            if (cancellation.IsCancelled())
                return {DistortWorkStatus::Cancelled};
            if (!IndexOf(source.layers, edit.group->originals[i].first))
                return {};
            for (std::size_t j = 0; j < i; ++j)
                if (edit.group->originals[j].first == edit.group->originals[i].first)
                    return {};
        }
    auto draft = CanvasDocumentDraft::Clone(source, source.layers.size(), cancellation);
    if (!draft)
        return {cancellation.IsCancelled() ? DistortWorkStatus::Cancelled : DistortWorkStatus::Invalid};
    for (auto& layer : draft.value().EditLayers())
    {
        if (!Checkpoint(observer, TransformPreviewStage::Layer, cancellation))
            return {DistortWorkStatus::Cancelled};
        if (edit.mask)
        {
            if (edit.layerID != layer.id || !layer.mask)
                continue;
            auto placement = edit.draft;
            if (edit.corners)
            {
                auto mask = RunDistortWork(MaskWarp(layer.mask.value(), placement, edit.corners.value()), executor,
                                           cancellation);
                if (Failed(mask.status))
                    return {mask.status};
                if (mask.status == DistortWorkStatus::Ready)
                {
                    layer.mask->asset.image = std::move(mask.image);
                    placement = mask.placement;
                }
            }
            layer.mask->placement = placement;
            continue;
        }
        const auto pending = Pending(edit, layer);
        if (!pending)
            continue;
        const auto original = layer.transform;
        bool warped = false;
        if (edit.corners && layer.asset)
        {
            const auto corners = edit.group ? DistortWarp::Carried(pending.value(), edit.draft, edit.corners.value())
                                            : edit.corners.value();
            auto color = RunDistortWork({layer.asset->image, pending.value(), corners, false, 0, kPreviewLimit},
                                        executor, cancellation);
            if (Failed(color.status))
                return {color.status};
            if (color.status == DistortWorkStatus::Ready)
            {
                layer.asset->image = std::move(color.image);
                layer.transform = color.placement;
                if (layer.mask && layer.mask->linked)
                {
                    const bool placed = layer.mask->placement.has_value();
                    const auto placement =
                        placed ? layer.mask->placement->Following(original, pending.value()) : pending.value();
                    const auto carried = placed ? DistortWarp::Carried(placement, pending.value(), corners) : corners;
                    auto mask =
                        RunDistortWork(MaskWarp(layer.mask.value(), placement, carried), executor, cancellation);
                    if (Failed(mask.status))
                        return {mask.status};
                    if (mask.status == DistortWorkStatus::Ready)
                    {
                        layer.mask->asset.image = std::move(mask.image);
                        layer.mask->placement = !placed && mask.placement.SamePlacement(layer.transform)
                                                    ? std::nullopt
                                                    : std::optional(mask.placement);
                    }
                }
                else if (layer.mask && !layer.mask->placement)
                    layer.mask->placement = original;
                warped = true;
            }
        }
        if (!warped)
        {
            layer.transform = pending.value();
            if (layer.mask)
                layer.mask->placement = MaskPlacementMoving(layer.mask.value(), original, pending.value());
        }
    }
    if (!Checkpoint(observer, TransformPreviewStage::Selection, cancellation))
        return {DistortWorkStatus::Cancelled};
    if (edit.floating && draft.value().DocumentState().selection &&
        !PrepareFloatingSelectionOutline(draft.value().DocumentState().selection.value(), edit, cancellation))
        return {cancellation.IsCancelled() ? DistortWorkStatus::Cancelled : DistortWorkStatus::Invalid};
    if (!Checkpoint(observer, TransformPreviewStage::Publication, cancellation))
        return {DistortWorkStatus::Cancelled};
    auto display = draft.value().Seal(cancellation);
    if (!display)
        return {cancellation.IsCancelled() ? DistortWorkStatus::Cancelled : DistortWorkStatus::Invalid};
    return {DistortWorkStatus::Ready, std::move(display.value())};
}

std::uint64_t DocumentSession::TransformPreviewPreparationMemoryBytes() const noexcept
{
    if (!_document || !_transformEdit || _gradient || _pixelMove || _brush || HasPendingWarp() || _colorEdit ||
        _shapeDraft || _selectionMoveOrigin || _lassoDraft)
        return 0;
    auto bytes = TransformDisplayMemoryBytes(_document.value(), _transformEdit.value());
    const auto retirement = ImageImportRetirementMemoryBytes();
    if (!bytes || !retirement || !Add(bytes, retirement) ||
        !Add(bytes, sizeof(TransformPreviewInput) + sizeof(PreparedTransformPreview) + 8192))
        return 0;
    return bytes;
}

std::optional<TransformPreviewInput> DocumentSession::PrepareTransformPreviewInput() const
{
    if (!TransformPreviewPreparationMemoryBytes())
        return std::nullopt;
    return TransformPreviewInput(CaptureImageImportAuthority(), history.CaptureState(), _document.value(),
                                 _transformEdit.value(), _editRevision);
}

TransformPreviewResult DocumentSession::PrepareTransformPreviewPublication(TransformPreviewInput input,
                                                                           Executor& executor, CancelToken cancellation,
                                                                           TransformPreviewObserver observer)
{
    cancellation = cancellation.WithShutdown(executor.ShutdownToken());
    if (!input._valid)
        return {};
    auto display = PrepareTransformDisplay(input._source, input._edit, executor, cancellation, observer);
    if (display.status != DistortWorkStatus::Ready || !display.document)
        return {display.status};
    if (cancellation.IsCancelled())
        return {DistortWorkStatus::Cancelled};
    TransformPreviewResult result;
    result.status = DistortWorkStatus::Ready;
    result.publication.emplace(PreparedTransformPreview(std::move(input._authority), std::move(input._history),
                                                        std::move(display.document), input._revision));
    return result;
}

bool DocumentSession::ApplyPreparedTransformPreviewIfCurrent(PreparedTransformPreview& prepared) noexcept
{
    static_assert(std::is_nothrow_swappable_v<std::optional<CanvasDocument>>);
    if (prepared._consumed || !prepared._display || !_transformEdit || _editRevision != prepared._revision ||
        _gradient || _pixelMove || _brush || HasPendingWarp() || _colorEdit || _shapeDraft || _selectionMoveOrigin ||
        _lassoDraft || !CanApplyImageImport(prepared._authority) || !history.IsCurrent(prepared._history))
        return false;
    std::swap(_displayCopy, prepared._display);
    _displayRevision = _editRevision;
    _displayTransformOpening = _transformEdit->opening;
    _displaySourceGeneration = Generation();
    prepared._consumed = true;
    return true;
}
