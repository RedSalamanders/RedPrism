#include "HistoryRestoreWork.h"

#include <limits>
#include <type_traits>
#include <utility>

PreparedHistoryRestore::PreparedHistoryRestore(PreparedHistoryRestore&& source) noexcept
    : _authority(std::move(source._authority)), _view(std::move(source._view)), _history(std::move(source._history)),
      _selected(std::move(source._selected)), _retired(std::move(source._retired)),
      _retiredDisplay(std::move(source._retiredDisplay)), _changesDocumentSerial(source._changesDocumentSerial),
      _maskSelected(source._maskSelected), _applied(std::exchange(source._applied, true))
{
}

PreparedHistoryRestore& PreparedHistoryRestore::operator=(PreparedHistoryRestore&& source) noexcept
{
    if (this != &source)
    {
        _authority = std::move(source._authority);
        _view = std::move(source._view);
        _history = std::move(source._history);
        _selected = std::move(source._selected);
        _retired = std::move(source._retired);
        _retiredDisplay = std::move(source._retiredDisplay);
        _changesDocumentSerial = source._changesDocumentSerial;
        _maskSelected = source._maskSelected;
        _applied = std::exchange(source._applied, true);
    }
    return *this;
}

DocumentHistory::ResidencyValidation PreparedHistoryRestore::TryAcquireHistoryResidency(
    DocumentHistory::ResidencyPermit& permit) noexcept
{
    if (_applied)
        return DocumentHistory::ResidencyValidation::Invalid;
    return _history ? permit.TryAcquire(_history.value()) : DocumentHistory::ResidencyValidation::Ready;
}

bool PreparedHistoryRestore::RefreshHistoryResidency(CancelToken cancellation,
                                                     DocumentHistory::PrepareObserver observer)
{
    if (_applied || cancellation.IsCancelled())
        return false;
    if (!_history)
        return true;
    const auto& restore = _history->RestoreSnapshot();
    return restore &&
           DocumentHistory::RefreshPreparedResidency(_history.value(), restore->document, cancellation, observer);
}

std::optional<PreparedHistoryRestore> PrepareHistoryRestorePublication(HistoryRestoreAuthority authority,
                                                                       DocumentHistory::StateView view, bool redo,
                                                                       CancelToken cancellation,
                                                                       DocumentHistory::PrepareObserver observer)
{
    if (!authority._valid || view.Depth() != 0 || cancellation.IsCancelled())
        return std::nullopt;
    PreparedHistoryRestore result(std::move(authority), std::move(view));
    if ((redo ? result._view.FutureCount() : result._view.PastCount()) == 0)
        return result; // A settled Undo/Redo with no entry leaves crop and history intact.
    result._history = DocumentHistory::PrepareRestore(std::move(result._view), redo, cancellation, observer);
    if (!result._history || cancellation.IsCancelled() || !result._history->RestoreSnapshot())
        return std::nullopt;
    const auto& snapshot = result._history->RestoreSnapshot().value();
    const auto restoredID = snapshot.document ? std::optional(snapshot.document->id) : std::optional<Guid>{};
    result._changesDocumentSerial = result._authority._documentID != restoredID;
    if (snapshot.activeLayerID)
        result._selected.insert(snapshot.activeLayerID.value());
    if (result._authority._maskSelected && result._authority._active == snapshot.activeLayerID && snapshot.document &&
        snapshot.activeLayerID)
    {
        for (const auto& layer : snapshot.document->layers)
        {
            if (cancellation.IsCancelled())
                return std::nullopt;
            if (layer.id == snapshot.activeLayerID.value())
            {
                result._maskSelected = layer.mask.has_value();
                break;
            }
        }
    }
    return cancellation.IsCancelled() ? std::nullopt : std::optional<PreparedHistoryRestore>{std::move(result)};
}

HistoryRestoreAuthority DocumentSession::CaptureHistoryRestoreAuthority() const noexcept
{
    HistoryRestoreAuthority result;
    result._session = _tokenIdentity.value;
    result._documentSerial = _documentSerial;
    result._generation = _generation;
    result._restoreSerial = _restoreSerial;
    result._documentID = _document ? std::optional(_document->id) : std::optional<Guid>{};
    result._active = _activeLayerID;
    result._crop = _cropRect;
    result._maskSelected = _maskSelected;
    result._valid = !HasColorEdit() && !_gradient && !_transformEdit && !_brush && !_brushFinishingOpening &&
                    !_brushUpdatingOpening && !HasPendingWarp() && !_pixelMove && !_shapeDraft &&
                    !_selectionMoveOrigin && !_lassoDraft;
    return result;
}

bool DocumentSession::CanApplyHistoryRestore(const HistoryRestoreAuthority& authority) const noexcept
{
    return !_lassoDraft && CanApplyHistoryRestoreWithoutLasso(authority);
}

bool DocumentSession::CanApplyHistoryRestoreWithoutLasso(const HistoryRestoreAuthority& authority) const noexcept
{
    return authority._valid && authority._session == _tokenIdentity.value &&
           authority._documentSerial == _documentSerial && authority._generation == _generation &&
           authority._restoreSerial == _restoreSerial && authority._active == _activeLayerID &&
           authority._maskSelected == _maskSelected && authority._crop == _cropRect && !HasColorEdit() && !_gradient &&
           !_transformEdit && !_brush && !_brushFinishingOpening && !_brushUpdatingOpening && !HasPendingWarp() &&
           !_pixelMove && !_shapeDraft && !_selectionMoveOrigin;
}

bool DocumentSession::ApplyPreparedHistoryRestoreIfCurrent(PreparedHistoryRestore& prepared) noexcept
{
    static_assert(std::is_nothrow_swappable_v<std::optional<CanvasDocument>>);
    static_assert(std::is_nothrow_swappable_v<std::unordered_set<Guid>>);
    if (prepared._applied || !CanApplyHistoryRestore(prepared._authority) ||
        (prepared._history ? !history.CanPublish(prepared._history.value()) : !history.IsCurrent(prepared._view)))
        return false;
    if (prepared._history)
    {
        if (!history.Publish(prepared._history.value()))
            return false;
        prepared._retired = prepared._history->TakeRestoreSnapshot();
        std::swap(_document, prepared._retired->document);
        std::swap(_activeLayerID, prepared._retired->activeLayerID);
        std::swap(_selected, prepared._selected);
        std::swap(_displayCopy, prepared._retiredDisplay);
        _maskSelected = prepared._maskSelected;
        _cropRect.reset();
        ++_restoreSerial;
        if (prepared._changesDocumentSerial)
            ++_documentSerial;
        NoteDocumentMutation();
    }
    prepared._authority._valid = false;
    prepared._applied = true;
    return true;
}

std::uint64_t DocumentSession::HistoryRestoreRetirementMemoryBytes() const noexcept
{
    auto bytes = history.RetirementMemoryBytes(_document);
    const auto maximum = (std::numeric_limits<std::uint64_t>::max)();
    const auto add = [&](std::uint64_t count, std::uint64_t size) noexcept
    {
        if ((size && count > maximum / size) || count * size > maximum - bytes)
            return false;
        bytes += count * size;
        return true;
    };
    if (!bytes || !add(_selected.bucket_count(), sizeof(void*)) ||
        !add(_selected.size(), sizeof(Guid) + 4 * sizeof(void*) + 64))
        return 0;
    if (_displayCopy)
    {
        const auto display = history.RetirementMemoryBytes(_displayCopy);
        if (!display || !add(1, display))
            return 0;
    }
    return bytes;
}
