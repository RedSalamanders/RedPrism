#include "PixelMoveClosureWork.h"

#include "DocumentDraft.h"
#include "SharedPublicationMemory.h"

#include <cmath>
#include <type_traits>

std::optional<PixelMoveClosureInput> DocumentSession::PrepareDeferredPixelMoveClosureInput(
    const PreparedPixelMoveOpening& opening) const
{
    if (!IsPreparedDeferredPixelMoveGestureCurrent(opening) || !_document || !_pixelMove)
        return std::nullopt;
    auto token = CaptureJobToken(); // Full mask/name metadata is copied only by the worker below.
    if (!token)
        return std::nullopt;
    return PixelMoveClosureInput(opening._input._read, opening._input._editor, opening._input._history,
                                 history.CaptureState(), _document.value(), std::move(token.value()), opening._lift,
                                 opening._input._active, _pixelMove->offset, _pixelMove->opening, _pixelMove->sequence,
                                 opening._input._pixelLimit, _pixelMove->duplicate);
}

std::optional<PreparedPixelMoveClosure> DocumentSession::PrepareDeferredPixelMoveClosure(
    PixelMoveClosureInput input, CancelToken cancellation, std::shared_ptr<const SharedPublicationMemory> memory,
    DocumentHistory::PrepareObserver observer)
{
    if (cancellation.IsCancelled() || !input._valid || input._owner == std::this_thread::get_id() || !input._document ||
        !input._document->selection || !input._lift || input._token.target || !input._opening ||
        input._pixelLimit < 1 || !std::isfinite(input._offset.x) || !std::isfinite(input._offset.y) ||
        input._offset.x != std::round(input._offset.x) || input._offset.y != std::round(input._offset.y))
        return std::nullopt;
    PreparedPixelMoveClosure result(std::move(input));
    result._memory = std::move(memory);
    if (result._input._offset == Vec2{})
        return result; // Exact no-op: do not construct history, clear redo, translate geometry or run pixels.
    const auto& source = result._input._document.value();
    const auto index = IndexOf(source.layers, result._input._active);
    if (!index || !source.layers[*index].asset)
        return std::nullopt;
    RasterEditInput raster;
    raster.layer = source.layers[*index];
    raster.selection = source.selection;
    raster.canvasWidth = source.width;
    raster.canvasHeight = source.height;
    raster.pixelLimit = result._input._pixelLimit;
    raster.kind = RasterEditKind::PixelMove;
    raster.offset = result._input._offset;
    raster.duplicate = result._input._duplicate;
    raster.pixelMoveOpening = result._input._lift;
    raster.outputMemory = result._memory;
    auto changed = RunRasterEdit(raster, cancellation);
    if (cancellation.IsCancelled() || changed.status != RasterEditStatus::Ready || !changed.layer || !changed.selection)
        return std::nullopt;
    auto draft = CanvasDocumentDraft::Clone(source, source.layers.size(), cancellation);
    if (!draft)
        return std::nullopt;
    draft->EditLayers()[*index] = std::move(changed.layer.value());
    draft->DocumentState().selection = std::move(changed.selection);
    result._after = draft->Seal(cancellation, result._memory);
    if (cancellation.IsCancelled() || !result._after || ValidateDocument(result._after.value()) != DocumentStatus::Ok)
        return std::nullopt;
    result._history = DocumentHistory::PrepareEdit(std::move(result._input._history), result._input._document,
                                                   result._input._active, result._after, result._input._active,
                                                   result._input._duplicate ? L"Duplicate Pixels" : L"Move Pixels",
                                                   cancellation, observer, result._memory);
    if (cancellation.IsCancelled() || !result._history)
        return std::nullopt;
    return result;
}

DocumentHistory::ResidencyValidation PreparedPixelMoveClosure::TryAcquireHistoryResidency(
    DocumentHistory::ResidencyPermit& permit) noexcept
{
    if (_applied || !_input._valid)
        return DocumentHistory::ResidencyValidation::Invalid;
    _residencyAttempted = true;
    if (_history)
        return permit.TryAcquire(*_history);
    permit.Reset();
    return DocumentHistory::ResidencyValidation::Ready;
}

bool PreparedPixelMoveClosure::RefreshHistoryResidency(CancelToken cancellation,
                                                       DocumentHistory::PrepareObserver observer)
{
    if (_applied || !_input._valid || cancellation.IsCancelled())
        return false;
    return !_history ||
           (_after && DocumentHistory::RefreshPreparedResidency(*_history, _after, cancellation, observer));
}

bool DocumentSession::IsPreparedDeferredPixelMoveClosureCurrent(const PreparedPixelMoveClosure& prepared) const noexcept
{
    const auto& input = prepared._input;
    const auto& editor = input._editor;
    return !prepared._applied && input._valid && _document && input._document && _pixelMove && _pixelMove->deferred &&
           _pixelMove->opening == input._opening && _pixelMoveOpening == input._opening &&
           _pixelMove->target == input._active && _activeLayerID == input._active &&
           _pixelMove->sequence == input._sequence && _pixelMove->offset == input._offset &&
           _pixelMove->duplicate == input._duplicate && _pixelMove->openingGeneration == _generation &&
           IsDocumentReadCurrent(input._read) && history.IsCurrent(input._authority) &&
           _document->layers.Identity() == input._document->layers.Identity() && _document->selection &&
           input._document->selection &&
           _document->selection->loops.Identity() == input._document->selection->loops.Identity() &&
           _pixelMove->origin.loops.Identity() == input._document->selection->loops.Identity() &&
           _pixelMove->origin.antialiased == input._document->selection->antialiased &&
           editor._session == _tokenIdentity.value && editor._active == _activeLayerID && editor._crop == _cropRect &&
           editor._maskSelected == _maskSelected && !_maskSelected && !HasColorEdit() && !_gradient &&
           !_transformEdit && !_brush && !_brushFinishingOpening && !_brushUpdatingOpening && !HasPendingWarp() &&
           !_shapeDraft && !_selectionMoveOrigin && !_lassoDraft &&
           (prepared._history ? prepared._after && history.CanPublish(*prepared._history) : input._offset == Vec2{});
}

bool DocumentSession::ApplyPreparedDeferredPixelMoveClosureIfCurrent(PreparedPixelMoveClosure& prepared) noexcept
{
    static_assert(std::is_nothrow_swappable_v<std::optional<CanvasDocument>>);
    static_assert(std::is_nothrow_swappable_v<std::optional<PixelMove>>);
    if ((prepared._memory && !prepared._memory->IsBound()) || (prepared._history && !prepared._residencyAttempted) ||
        !IsPreparedDeferredPixelMoveClosureCurrent(prepared))
        return false;
    if (prepared._history)
    {
        if (!history.Publish(*prepared._history))
            return false;
        std::swap(_document, prepared._after);
        NoteDocumentMutation();
    }
    std::swap(_pixelMove, prepared._retiredPixelMove);
    std::swap(_displayCopy, prepared._retiredDisplay);
    prepared._applied = true;
    return true;
}
