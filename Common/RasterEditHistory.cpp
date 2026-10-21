#include "RasterEditWork.h"

#include "DocumentDraft.h"

#include <algorithm>
#include <type_traits>
#include <utility>

static_assert(std::is_nothrow_swappable_v<ImageLayer>);
static_assert(std::is_nothrow_swappable_v<DocumentSelection>);
static_assert(std::is_nothrow_move_constructible_v<DocumentHistory::Snapshot>);
static_assert(std::is_nothrow_move_constructible_v<JobToken>);
static_assert(std::is_nothrow_move_constructible_v<RasterEditResult>);
static_assert(std::is_nothrow_swappable_v<std::optional<BrushCommitInfo>>);
static_assert(std::is_nothrow_swappable_v<std::optional<GradientEdit>>);
static_assert(std::is_nothrow_swappable_v<std::optional<PixelMove>>);

std::optional<PreparedRasterEdit> PrepareRasterEditPublication(
    RasterEditKind kind, bool mask, const std::optional<CanvasDocument>& source, const std::optional<Guid>& active,
    JobToken token, DocumentHistory::StateView history, RasterEditResult&& raster, std::wstring name,
    CancelToken cancellation, DocumentHistory::PrepareObserver observer)
{
    if (cancellation.IsCancelled() || !source || !active || !token.target || active != token.target->layerId ||
        token.canvasWidth != source->width || token.canvasHeight != source->height ||
        raster.status != RasterEditStatus::Ready || !raster.layer || raster.layer->id != token.target->layerId ||
        (kind == RasterEditKind::PixelMove && (mask || !raster.selection)) ||
        (kind != RasterEditKind::PixelMove && raster.selection))
        return std::nullopt;
    const auto original = std::find_if(source->layers.begin(), source->layers.end(),
                                       [&](const ImageLayer& layer) { return layer.id == token.target->layerId; });
    if (original == source->layers.end() || original->transform != token.target->transform ||
        original->mask != token.target->mask ||
        (original->asset ? original->asset->image : Raster{}) != token.target->image ||
        original->isGroup != token.target->isGroup || original->adjustment != token.target->adjustment ||
        source->selection.has_value() != token.selection.has_value() ||
        (source->selection && (source->selection->antialiased != token.selection->antialiased ||
                               source->selection->loops.Identity() != token.selection->loops.Identity())))
        return std::nullopt;
    const auto index = IndexOf(source->layers, raster.layer.value().id);
    if (!index)
        return std::nullopt;
    auto draft = CanvasDocumentDraft::Clone(source.value(), source->layers.size(), cancellation);
    if (!draft)
        return std::nullopt;
    {
        auto layers = draft.value().EditLayers();
        layers[index.value()] = raster.layer.value();
    }
    if (raster.selection)
        draft.value().DocumentState().selection = raster.selection;
    auto sealed = draft.value().Seal(cancellation);
    if (!sealed || cancellation.IsCancelled() || ValidateDocument(sealed.value()) != DocumentStatus::Ok)
        return std::nullopt;
    // Moving the after-document into its optional avoids an extra layer-record copy before history preparation.
    std::optional<CanvasDocument> afterDocument{std::move(sealed.value())};
    auto publication = DocumentHistory::PrepareEdit(std::move(history), source, active, afterDocument, active,
                                                    std::move(name), cancellation, observer);
    if (!publication || cancellation.IsCancelled())
        return std::nullopt;
    return PreparedRasterEdit(kind, mask, std::move(token), std::move(raster), std::move(afterDocument), index.value(),
                              std::move(publication.value()));
}

bool DocumentSession::ApplyPreparedRasterEditIfCurrent(PreparedRasterEdit& prepared) noexcept
{
    if (!prepared._raster.layer || !history.CanPublish(prepared._history) ||
        prepared._kind == RasterEditKind::Gradient || prepared._kind == RasterEditKind::PixelMove)
        return false;
    return ApplyRasterEditImpl(std::move(*prepared._raster.layer), prepared._mask, {}, prepared._token, false, false,
                               nullptr, &prepared);
}

bool DocumentSession::ApplyPreparedGradientIfCurrent(PreparedRasterEdit& prepared, std::uint64_t opening,
                                                     std::uint64_t sequence) noexcept
{
    if (!prepared._raster.layer || !history.CanPublish(prepared._history) ||
        prepared._kind != RasterEditKind::Gradient || !_gradient || !_gradient->deferred || !_gradient->HasLine() ||
        _gradient->opening != opening || _gradient->sequence != sequence || !prepared._token.target ||
        _gradient->target != prepared._token.target->layerId || _gradient->mask != prepared._mask)
        return false;
    return ApplyRasterEditImpl(std::move(*prepared._raster.layer), prepared._mask, {}, prepared._token, true, false,
                               nullptr, &prepared);
}

bool DocumentSession::ApplyPreparedPixelMoveIfCurrent(PreparedRasterEdit& prepared, std::uint64_t opening,
                                                      std::uint64_t sequence) noexcept
{
    if (!prepared._raster.layer || !history.CanPublish(prepared._history) ||
        prepared._kind != RasterEditKind::PixelMove || !_pixelMove || !_pixelMove->deferred ||
        !_pixelMove->HasOffset() || _pixelMove->opening != opening || _pixelMove->sequence != sequence ||
        _pixelMove->openingGeneration != _generation || !prepared._token.target ||
        _pixelMove->target != prepared._token.target->layerId || !prepared._raster.selection ||
        prepared._raster.selection->antialiased != _pixelMove->origin.antialiased ||
        prepared._raster.selection->loops.PointCount() != _pixelMove->origin.loops.PointCount())
        return false;
    const auto before = _pixelMove->origin.Bounds();
    const auto after = prepared._raster.selection->Bounds();
    if (!before || !after || after->minX != before->minX + _pixelMove->offset.x ||
        after->maxX != before->maxX + _pixelMove->offset.x || after->minY != before->minY + _pixelMove->offset.y ||
        after->maxY != before->maxY + _pixelMove->offset.y)
        return false;
    return ApplyRasterEditImpl(std::move(*prepared._raster.layer), false, {}, prepared._token, false, true,
                               &prepared._raster.selection.value(), &prepared);
}
