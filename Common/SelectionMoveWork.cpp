#include "SelectionMoveWork.h"

#include <limits>
#include <type_traits>
#include <utility>

std::optional<SelectionMoveClosureInput> DocumentSession::PrepareSelectionMoveClosureInput(bool cancel) const
{
    if (!_document || !_selectionMoveOrigin || history.CaptureState().Depth() <= 0)
        return std::nullopt;
    auto token = CaptureJobToken();
    if (!token)
        return std::nullopt;
    return SelectionMoveClosureInput(std::move(token.value()), history.CaptureState(), _document.value(),
                                     _selectionMoveOrigin.value(), _activeLayerID, _cropRect, _maskSelected, cancel);
}

std::optional<PreparedSelectionMoveClosure> DocumentSession::PrepareSelectionMoveClosurePublication(
    SelectionMoveClosureInput input, CancelToken cancellation, DocumentHistory::PrepareObserver observer)
{
    if (cancellation.IsCancelled() || !input._valid || !input._document || input._token.target ||
        input._history.Depth() <= 0 || input._document->width != input._token.canvasWidth ||
        input._document->height != input._token.canvasHeight)
        return std::nullopt;
    PreparedSelectionMoveClosure result(std::move(input));
    if (result._input._cancel)
        result._input._document->selection = result._input._origin;
    if (cancellation.IsCancelled() || ValidateDocument(result._input._document.value()) != DocumentStatus::Ok)
        return std::nullopt;
    result._history = DocumentHistory::PrepareEnd(result._input._history, result._input._document,
                                                  result._input._active, cancellation, observer);
    return result._history && !cancellation.IsCancelled()
               ? std::optional<PreparedSelectionMoveClosure>(std::move(result))
               : std::nullopt;
}

bool DocumentSession::IsPreparedSelectionMoveClosureCurrent(const PreparedSelectionMoveClosure& prepared) const noexcept
{
    const auto& input = prepared._input;
    return !prepared._applied && input._valid && _document && _selectionMoveOrigin && prepared._history &&
           _activeLayerID == input._active && _maskSelected == input._maskSelected && _cropRect == input._crop &&
           _selectionMoveOrigin->antialiased == input._origin.antialiased &&
           _selectionMoveOrigin->loops.Identity() == input._origin.loops.Identity() &&
           CanApplyJobResult(input._token, 0) && history.CanPublish(prepared._history.value());
}

bool DocumentSession::ApplyPreparedSelectionMoveClosureIfCurrent(PreparedSelectionMoveClosure& prepared) noexcept
{
    static_assert(std::is_nothrow_swappable_v<std::optional<CanvasDocument>>);
    static_assert(std::is_nothrow_swappable_v<std::optional<DocumentSelection>>);
    if (!IsPreparedSelectionMoveClosureCurrent(prepared) || !history.Publish(prepared._history.value()))
        return false;
    if (prepared._input._cancel)
    {
        std::swap(_document, prepared._input._document);
        NoteDocumentMutation(); // CancelSelectionMove restores the original before its End acknowledgement.
    }
    std::swap(_selectionMoveOrigin, prepared._retiredOrigin);
    std::swap(_displayCopy, prepared._retiredDisplay);
    NoteDocumentMutation(); // Every acknowledged End expires the previous whole-document token, even a no-op.
    prepared._applied = true;
    return true;
}

std::uint64_t DocumentSession::SelectionMoveClosureMemoryBytes() const noexcept
{
    if (!_document || !_selectionMoveOrigin)
        return 0;
    const auto bytes = history.EditMemoryBytes(_document);
    constexpr std::uint64_t extra = 4096 + sizeof(SelectionMoveClosureInput) + sizeof(PreparedSelectionMoveClosure);
    return bytes && bytes <= (std::numeric_limits<std::uint64_t>::max)() - extra ? bytes + extra : 0;
}
