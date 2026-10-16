#include "SelectionMoveOpeningWork.h"

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
    SelectionMoveOpeningInput input, CancelToken cancellation, DocumentHistory::PrepareObserver observer)
{
    if (!input._valid || !input._opening || cancellation.IsCancelled() || !input._document ||
        !input._document->selection || input._document->selection->IsEmpty() || input._token.target ||
        input._document->width != input._token.canvasWidth || input._document->height != input._token.canvasHeight)
        return std::nullopt;
    PreparedSelectionMoveOpening result(std::move(input));
    result._origin = result._input._document->selection;
    result._history = DocumentHistory::PrepareBegin(result._input._history, result._input._document,
                                                    result._input._active, L"Move Selection", cancellation, observer);
    return result._history && !cancellation.IsCancelled() ? std::optional(std::move(result)) : std::nullopt;
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
    if (!IsPreparedDeferredSelectionMoveOpeningCurrent(prepared) || !history.Publish(prepared._history.value()))
        return false;
    std::swap(_selectionMoveOrigin, prepared._origin);
    _selectionMoveOffset = Vec2{};
    _selectionMoveOpening = prepared._input._opening;
    prepared._applied = true;
    return true;
}
