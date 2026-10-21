#include "PixelMoveOpeningWork.h"

#include "BrushStroke.h"
#include "SharedPublicationMemory.h"

#include <cmath>
#include <limits>
#include <type_traits>

std::optional<PixelMoveOpeningInput> DocumentSession::PrepareDeferredPixelMoveOpeningInput(bool duplicate) const
{
    const auto editor = CaptureHistoryRestoreAuthority();
    const auto read = CaptureDocumentReadAuthority();
    if (!read || !CanApplyHistoryRestore(editor) || history.CaptureState().Depth() != 0 || !_document ||
        !_document->selection || _document->selection->IsEmpty() || _maskSelected || !_activeLayerID ||
        _pixelMoveOpening == (std::numeric_limits<std::uint64_t>::max)())
        return std::nullopt;
    const auto* layer = ActiveLayer();
    if (!layer || !layer->asset || layer->isGroup || layer->adjustment)
        return std::nullopt;
    auto token = CaptureJobToken(); // No target: copying mask names belongs to the admitted worker.
    if (!token)
        return std::nullopt;
    const auto pixelLimit = RasterEditPixelLimit(*layer, false);
    if (pixelLimit < 1)
        return std::nullopt;
    return PixelMoveOpeningInput(editor, read.value(), history.CaptureAuthority(), _document.value(),
                                 std::move(token.value()), layer->id, _pixelMoveOpening + 1, pixelLimit, duplicate);
}

PreparedPixelMoveOpeningResult DocumentSession::PrepareDeferredPixelMoveOpening(
    PixelMoveOpeningInput input, CancelToken cancellation, std::shared_ptr<const SharedPublicationMemory> memory)
{
    if (cancellation.IsCancelled())
        return {PixelMoveLiftStatus::Cancelled};
    if (!input._valid || input._owner == std::this_thread::get_id() || !input._document || !input._opening ||
        input._token.target || !input._document->selection || input._document->selection->IsEmpty() ||
        input._token.canvasWidth != input._document->width || input._token.canvasHeight != input._document->height)
        return {};
    const auto* layer = input._document->layers.FindKey(input._active);
    if (!layer || !layer->asset)
        return {};
    // CanPaint's ancestor walk belongs here, after admission, rather than in owner capture.
    // Use the immutable keyed index and check cancellation at every ancestor; preserve its depth bound.
    std::optional<Guid> current = input._active;
    for (int depth = 0; current && depth < kMaxLayerCount; ++depth)
    {
        if (cancellation.IsCancelled())
            return {PixelMoveLiftStatus::Cancelled};
        const auto* ancestor = input._document->layers.FindKey(current.value());
        if (!ancestor || !ancestor->visible)
            return {};
        current = ancestor->parentID;
    }
    if (current)
        return {}; // A cyclic/over-depth immutable source cannot authorize a lift.
    RasterEditInput raster;
    raster.layer = *layer;
    raster.selection = input._document->selection;
    raster.canvasWidth = input._document->width;
    raster.canvasHeight = input._document->height;
    raster.pixelLimit = input._pixelLimit;
    raster.kind = RasterEditKind::PixelMove;
    auto lift = PreparePixelMoveLiftForWork(raster, cancellation, memory);
    if (lift.status != PixelMoveLiftStatus::Ready)
        return {lift.status};
    if (!lift.opening)
        return {PixelMoveLiftStatus::Memory};
    input._token.target = TargetIdentity{layer->id,   layer->asset->image, layer->transform,
                                         layer->mask, layer->isGroup,      layer->adjustment};
    PreparedPixelMoveOpening result(std::move(input));
    result._memory = std::move(memory);
    result._lift = std::move(lift.opening);
    result._pending.emplace(PixelMove{{},
                                      {},
                                      result._input._document->selection.value(),
                                      result._input._duplicate,
                                      {},
                                      result._input._active,
                                      true,
                                      result._input._opening});
    result._pending->openingGeneration = result._input._read.generation;
    const auto grid = BrushStroke::PlanGrid(*layer, false, BrushSettings{}, raster.canvasWidth, raster.canvasHeight);
    if (!grid)
        return {};
    result._pending->proceduralOpening = result._lift;
    result._pending->proceduralMemory = result._memory;
    result._pending->proceduralWidth = grid->width;
    result._pending->proceduralHeight = grid->height;
    result._pending->proceduralOwnershipBytes = PixelMoveOpeningMemoryBytes(result._lift);
    if (!result._pending->proceduralOwnershipBytes)
        return {};
    if (cancellation.IsCancelled())
        return {PixelMoveLiftStatus::Cancelled};
    return {PixelMoveLiftStatus::Ready, std::move(result)};
}

bool DocumentSession::IsPreparedDeferredPixelMoveOpeningCurrent(const PreparedPixelMoveOpening& prepared) const noexcept
{
    // Generation and immutable root identity cover the complete target record without walking mask names.
    return !prepared._applied && prepared._input._valid && prepared._pending && prepared._lift &&
           _pixelMoveOpening != (std::numeric_limits<std::uint64_t>::max)() &&
           prepared._input._opening == _pixelMoveOpening + 1 && IsDocumentReadCurrent(prepared._input._read) &&
           CanApplyHistoryRestore(prepared._input._editor) && history.IsCurrent(prepared._input._history) &&
           _document && prepared._input._document &&
           _document->layers.Identity() == prepared._input._document->layers.Identity();
}

bool DocumentSession::ApplyPreparedDeferredPixelMoveOpeningIfCurrent(PreparedPixelMoveOpening& prepared) noexcept
{
    static_assert(std::is_nothrow_swappable_v<std::optional<PixelMove>>);
    if ((prepared._memory && !prepared._memory->IsBound()) || !IsPreparedDeferredPixelMoveOpeningCurrent(prepared))
        return false;
    std::swap(_pixelMove, prepared._pending);
    _pixelMoveOpening = prepared._input._opening;
    prepared._applied = true;
    return true;
}

bool DocumentSession::IsPreparedDeferredPixelMoveGestureCurrent(const PreparedPixelMoveOpening& prepared) const noexcept
{
    const auto& editor = prepared._input._editor;
    return prepared._applied && prepared._input._valid && !prepared._pending && prepared._lift && _pixelMove &&
           _pixelMove->deferred && _pixelMove->opening == prepared._input._opening &&
           _pixelMoveOpening == prepared._input._opening && _pixelMove->target == prepared._input._active &&
           _pixelMove->openingGeneration == _generation && IsDocumentReadCurrent(prepared._input._read) &&
           history.IsCurrent(prepared._input._history) && editor._session == _tokenIdentity.value &&
           editor._active == _activeLayerID && editor._crop == _cropRect && editor._maskSelected == _maskSelected &&
           !_maskSelected && !HasColorEdit() && !_gradient && !_transformEdit && !_brush && !_brushFinishingOpening &&
           !_brushUpdatingOpening && !HasPendingWarp() && !_shapeDraft && !_selectionMoveOrigin && !_lassoDraft;
}

bool DocumentSession::MovePreparedDeferredPixelMoveIfCurrent(PreparedPixelMoveOpening& prepared, double dx,
                                                             double dy) noexcept
{
    if (!IsPreparedDeferredPixelMoveGestureCurrent(prepared) || !std::isfinite(dx) || !std::isfinite(dy))
        return false;
    const Vec2 rounded{std::round(dx), std::round(dy)};
    const auto bounds = _pixelMove->origin.Bounds();
    if (!bounds || !std::isfinite(bounds->minX + rounded.x) || !std::isfinite(bounds->maxX + rounded.x) ||
        !std::isfinite(bounds->minY + rounded.y) || !std::isfinite(bounds->maxY + rounded.y) ||
        (_pixelMove->sequence == (std::numeric_limits<std::uint64_t>::max)() && rounded != _pixelMove->offset))
        return false;
    MovePixels(dx, dy); // Scalar deferred path only; no pixel kernel or history publication.
    return _pixelMove && _pixelMove->offset == rounded;
}

bool DocumentSession::AbortPreparedDeferredPixelMoveIfCurrent(PreparedPixelMoveOpening& prepared) noexcept
{
    static_assert(std::is_nothrow_swappable_v<std::optional<PixelMove>>);
    static_assert(std::is_nothrow_swappable_v<std::optional<CanvasDocument>>);
    // A foreign document edit may stale the gesture; retire only this exact pending opening, never a newer one.
    if (!prepared._applied || !prepared._input._valid || prepared._pending || !_pixelMove ||
        _pixelMove->opening != prepared._input._opening || _pixelMoveOpening != prepared._input._opening ||
        _pixelMove->target != prepared._input._active ||
        prepared._input._read.sessionIdentity != _tokenIdentity.value ||
        prepared._input._read.documentSerial != _documentSerial)
        return false;
    std::swap(_pixelMove, prepared._pending);
    std::swap(_displayCopy, prepared._retiredDisplay);
    prepared._applied = false;
    prepared._input._valid = false;
    return true;
}
