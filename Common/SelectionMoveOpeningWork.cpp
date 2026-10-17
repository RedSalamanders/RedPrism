#include "SelectionMoveOpeningWork.h"
#include "SharedPublicationMemory.h"

#include <cmath>
#include <limits>
#include <type_traits>

std::optional<SelectionMoveOpeningInput> DocumentSession::PrepareDeferredSelectionMoveOpeningInput() const
{
    const auto authority = CaptureHistoryRestoreAuthority();
    if (!CanApplyHistoryRestore(authority) || !_document || !_document->selection || _document->selection->IsEmpty() ||
        _selectionMoveOpening == (std::numeric_limits<std::uint64_t>::max)())
        return std::nullopt;
    auto token = CaptureJobToken();
    if (!token)
        return std::nullopt;
    return SelectionMoveOpeningInput(authority, std::move(token.value()), history.CaptureState(), _document.value(),
                                     _activeLayerID, _selectionMoveOpening + 1);
}

std::optional<PreparedSelectionMoveOpening> DocumentSession::PrepareDeferredSelectionMoveOpening(
    SelectionMoveOpeningInput input, CancelToken cancellation, DocumentHistory::PrepareObserver observer,
    std::shared_ptr<const SharedPublicationMemory> memory, bool prepareAbort)
{
    if (!input._valid || !input._opening || cancellation.IsCancelled() || !input._document ||
        !input._document->selection || input._document->selection->IsEmpty() || input._token.target ||
        input._document->width != input._token.canvasWidth || input._document->height != input._token.canvasHeight)
        return std::nullopt;
    PreparedSelectionMoveOpening result(std::move(input));
    result._origin = result._input._document->selection;
    result._history =
        DocumentHistory::PrepareBegin(result._input._history, result._input._document, result._input._active,
                                      L"Move Selection", cancellation, observer, memory);
    result._memory = std::move(memory);
    if (!result._history || cancellation.IsCancelled())
        return std::nullopt;
    if (prepareAbort)
    {
        // The scalar route never changes document geometry before End. Its cancellation is therefore a
        // no-op End of this exact Begin. Prepare it before any owner publication, retaining predecessor
        // roots so it cannot close a newer or unrelated history transaction.
        auto following = DocumentHistory::FollowingPreparedState(result._history.value());
        if (!following)
            return std::nullopt;
        result._abortHistory = DocumentHistory::PrepareEnd(std::move(following.value()), result._input._document,
                                                           result._input._active, cancellation, observer);
        if (!result._abortHistory || cancellation.IsCancelled())
            return std::nullopt;
    }
    return result;
}

bool DocumentSession::IsPreparedDeferredSelectionMoveOpeningCurrent(
    const PreparedSelectionMoveOpening& prepared) const noexcept
{
    return !prepared._applied && prepared._input._valid && prepared._history && prepared._origin &&
           _selectionMoveOpening != (std::numeric_limits<std::uint64_t>::max)() &&
           prepared._input._opening == _selectionMoveOpening + 1 &&
           CanApplyHistoryRestore(prepared._input._authority) && CanApplyJobResult(prepared._input._token, 0) &&
           history.CanPublish(prepared._history.value());
}

bool DocumentSession::ApplyPreparedDeferredSelectionMoveOpeningIfCurrent(
    PreparedSelectionMoveOpening& prepared) noexcept
{
    static_assert(std::is_nothrow_swappable_v<std::optional<DocumentSelection>>);
    if ((prepared._memory && !prepared._memory->IsBound()) ||
        !IsPreparedDeferredSelectionMoveOpeningCurrent(prepared) || !history.Publish(prepared._history.value()))
        return false;
    std::swap(_selectionMoveOrigin, prepared._origin);
    _selectionMoveOffset = Vec2{};
    _selectionMoveOpening = prepared._input._opening;
    prepared._applied = true;
    if (prepared._abortHistory)
        prepared._gestureAuthority = CaptureDocumentReadAuthority();
    return true;
}

bool DocumentSession::IsPreparedDeferredSelectionMoveGestureCurrent(
    const PreparedSelectionMoveOpening& prepared) const noexcept
{
    const auto& authority = prepared._input._authority;
    return prepared._applied && prepared._input._valid && prepared._abortHistory && prepared._gestureAuthority &&
           _selectionMoveOpening == prepared._input._opening &&
           IsDocumentReadCurrent(prepared._gestureAuthority.value()) && HasCurrentDeferredSelectionMoveSource() &&
           authority._documentSerial == _documentSerial && authority._restoreSerial == _restoreSerial &&
           authority._documentID == std::optional(_document->id) && authority._active == _activeLayerID &&
           authority._crop == _cropRect && authority._maskSelected == _maskSelected && !HasColorEdit() &&
           !_transformEdit && !_brush && !_brushFinishingOpening && !_brushUpdatingOpening && !HasPendingWarp() &&
           !_shapeDraft && !_gradient && !_pixelMove && !_lassoDraft &&
           history.CanPublish(prepared._abortHistory.value());
}

bool DocumentSession::MovePreparedDeferredSelectionMoveIfCurrent(PreparedSelectionMoveOpening& prepared, double dx,
                                                                 double dy) noexcept
{
    if (!IsPreparedDeferredSelectionMoveGestureCurrent(prepared) || !std::isfinite(dx) || !std::isfinite(dy))
        return false;
    const Vec2 offset{std::round(dx), std::round(dy)};
    const auto bounds = _selectionMoveOrigin->Bounds();
    if (!bounds || !std::isfinite(bounds->minX + offset.x) || !std::isfinite(bounds->maxX + offset.x) ||
        !std::isfinite(bounds->minY + offset.y) || !std::isfinite(bounds->maxY + offset.y))
        return false;
    MoveSelection(dx, dy);
    prepared._gestureAuthority = CaptureDocumentReadAuthority();
    return _selectionMoveOffset == offset;
}

bool DocumentSession::AbortPreparedDeferredSelectionMoveIfCurrent(PreparedSelectionMoveOpening& prepared) noexcept
{
    static_assert(std::is_nothrow_swappable_v<std::optional<DocumentSelection>>);
    static_assert(std::is_nothrow_swappable_v<std::optional<CanvasDocument>>);
    if (!IsPreparedDeferredSelectionMoveGestureCurrent(prepared) || !history.Publish(prepared._abortHistory.value()))
        return false;
    // The immutable document still contains the exact original selection. Only discard its scalar preview;
    // keep replaced owners in the original prepared payload for deferred, off-owner destruction.
    std::swap(_selectionMoveOrigin, prepared._origin);
    _selectionMoveOffset.reset();
    std::swap(_displayCopy, prepared._retiredDisplay);
    NoteDocumentMutation(); // Reference Cancel restores, then End invalidates once more.
    NoteDocumentMutation();
    prepared._gestureAuthority.reset();
    return true;
}
