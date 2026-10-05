#include "SelectionCommandWork.h"

#include "BrushStroke.h"
#include "Selection.h"
#include "WarpStroke.h"

#include <limits>
#include <type_traits>
#include <utility>

SelectionCommandAuthority DocumentSession::CaptureSelectionCommandAuthority() const noexcept
{
    SelectionCommandAuthority result;
    result._session = _tokenIdentity.value;
    result._documentSerial = _documentSerial;
    result._generation = _generation;
    result._restoreSerial = _restoreSerial;
    result._documentID = _document ? std::optional(_document->id) : std::optional<Guid>{};
    result._active = _activeLayerID;
    result._crop = _cropRect;
    result._maskSelected = _maskSelected;
    result._brush = _brush;
    result._warp = _warp;
    result._shape = _shapeDraft.has_value();
    result._shapeOpening = _shapeOpening;
    result._valid =
        CanEditSelection() && !HasColorEdit() && !_gradient && !_pixelMove && !_selectionMoveOrigin && !_lassoDraft;
    return result;
}

bool DocumentSession::CanApplySelectionCommand(const SelectionCommandAuthority& authority) const noexcept
{
    // owner_before compares control blocks, so cancellation/reopening cannot pass through an address ABA.
    return authority._valid && authority._session == _tokenIdentity.value &&
           authority._documentSerial == _documentSerial && authority._generation == _generation &&
           authority._restoreSerial == _restoreSerial &&
           authority._documentID == (_document ? std::optional(_document->id) : std::optional<Guid>{}) &&
           authority._active == _activeLayerID && authority._maskSelected == _maskSelected &&
           authority._crop == _cropRect && !authority._brush.owner_before(_brush) &&
           !_brush.owner_before(authority._brush) && !authority._warp.owner_before(_warp) &&
           !_warp.owner_before(authority._warp) && authority._shape == _shapeDraft.has_value() &&
           authority._shapeOpening == _shapeOpening && CanEditSelection() && !HasColorEdit() && !_gradient &&
           !_pixelMove && !_selectionMoveOrigin && !_lassoDraft;
}

std::optional<SelectionCommandInput> DocumentSession::PreparePreservingSelectionCommandInput(
    SelectionCommandKind kind) const
{
    if (!_document || kind > SelectionCommandKind::Deselect)
        return std::nullopt;
    auto authority = CaptureSelectionCommandAuthority();
    if (!CanApplySelectionCommand(authority))
        return std::nullopt;
    auto token = CaptureJobToken();
    if (!token)
        return std::nullopt;
    SelectionCommandInput result(kind, HistoryRestoreAuthority{}, std::move(token.value()), history.CaptureState(),
                                 _document.value(), _activeLayerID);
    result._preservedGesture = std::move(authority);
    return result;
}

std::optional<SelectionCommandInput> DocumentSession::PrepareSelectionCommandInput(SelectionCommandKind kind) const
{
    if (!_document || kind > SelectionCommandKind::Deselect)
        return std::nullopt;
    auto authority = CaptureHistoryRestoreAuthority();
    if (!CanApplyHistoryRestore(authority))
        return std::nullopt; // selected host prerequisites must settle/cancel gestures and pending edits first
    auto token = CaptureJobToken();
    if (!token)
        return std::nullopt;
    return SelectionCommandInput(kind, std::move(authority), std::move(token.value()), history.CaptureState(),
                                 _document.value(), _activeLayerID);
}

std::optional<PreparedSelectionCommand> DocumentSession::PrepareSelectionCommandPublication(
    SelectionCommandInput input, CancelToken cancellation, DocumentHistory::PrepareObserver observer)
{
    if (cancellation.IsCancelled() || !input._valid || !input._document || input._token.target ||
        input._kind > SelectionCommandKind::Deselect || input._document->width != input._token.canvasWidth ||
        input._document->height != input._token.canvasHeight)
        return std::nullopt;
    const auto& source = input._document.value();
    std::optional<DocumentSelection> selection;
    if (input._kind == SelectionCommandKind::SelectAll)
        selection = DocumentSelection{true, SelectionGeometry::Rect(0, 0, source.width, source.height)};
    if (cancellation.IsCancelled())
        return std::nullopt;
    // Equality is geometric, not just a root address: repeat Select All is a true no-op with retained redo.
    const bool unchanged = selection == source.selection;
    PreparedSelectionCommand result(std::move(input));
    if (unchanged)
        return result;
    if (observer.checkpoint)
        observer.checkpoint(observer.context, DocumentHistory::PrepareStage::SelectionSnapshot);
    if (cancellation.IsCancelled())
        return std::nullopt;
    const auto& before = result._input._document.value();
    CanvasDocument after;
    after.id = before.id;
    after.width = before.width;
    after.height = before.height;
    after.resolution = before.resolution;
    after.selection = std::move(selection);
    if (cancellation.IsCancelled())
        return std::nullopt;
    after.layers = before.layers;
    if (cancellation.IsCancelled() || ValidateDocument(after) != DocumentStatus::Ok)
        return std::nullopt;
    result._document = std::move(after);
    result._history = DocumentHistory::PrepareEdit(
        result._input._history, result._input._document, result._input._active, result._document, result._input._active,
        result._input._kind == SelectionCommandKind::SelectAll ? L"Select All" : L"Deselect", cancellation, observer);
    if (!result._history || cancellation.IsCancelled())
        return std::nullopt;
    return result;
}

DocumentHistory::ResidencyValidation PreparedSelectionCommand::TryAcquireHistoryResidency(
    DocumentHistory::ResidencyPermit& permit) noexcept
{
    if (_applied || !_input._valid)
        return DocumentHistory::ResidencyValidation::Invalid;
    return _history ? permit.TryAcquire(_history.value()) : DocumentHistory::ResidencyValidation::Ready;
}

bool PreparedSelectionCommand::RefreshHistoryResidency(CancelToken cancellation,
                                                       DocumentHistory::PrepareObserver observer)
{
    if (_applied || !_input._valid || cancellation.IsCancelled())
        return false;
    return !_history || (_document && DocumentHistory::RefreshPreparedResidency(_history.value(), _document,
                                                                                cancellation, observer));
}

bool DocumentSession::IsPreparedSelectionCommandCurrent(const PreparedSelectionCommand& prepared) const noexcept
{
    return !prepared._applied && prepared._input._valid &&
           (prepared._input._preservedGesture ? CanApplySelectionCommand(prepared._input._preservedGesture.value())
                                              : CanApplyHistoryRestore(prepared._input._authority)) &&
           CanApplyJobResult(prepared._input._token, 0) &&
           (prepared._history ? history.CanPublish(prepared._history.value())
                              : history.IsCurrent(prepared._input._history));
}

bool DocumentSession::ApplyPreparedSelectionCommandIfCurrent(PreparedSelectionCommand& prepared) noexcept
{
    static_assert(std::is_nothrow_swappable_v<std::optional<CanvasDocument>>);
    if (!IsPreparedSelectionCommandCurrent(prepared))
        return false;
    if (prepared._history)
    {
        if (!prepared._document || !history.Publish(prepared._history.value()))
            return false;
        std::swap(_document, prepared._document);
        std::swap(_displayCopy, prepared._retiredDisplay);
        NoteDocumentMutation();
    }
    // No-op acknowledgement neither constructs/publishes history nor changes the document generation.
    prepared._applied = true;
    return true;
}

std::uint64_t DocumentSession::SelectionCommandMemoryBytes(SelectionCommandKind kind) const noexcept
{
    if (!_document || kind > SelectionCommandKind::Deselect)
        return 0;
    auto bytes = history.EditMemoryBytes(_document);
    // Reserve the original history candidate and one refreshed private index/stack concurrently, before capture.
    if (!bytes || bytes > (std::numeric_limits<std::uint64_t>::max)() / 2)
        return 0;
    bytes *= 2;
    // EditMemoryBytes bounds eight record copies, immutable stack growth and trim workspace. This graph uses
    // owner capture, private after-document and before/after snapshots (four copies). No raster/coverage output
    // or Clipper workspace is created; Select All adds exactly one four-point loop plus shared root/control.
    // Weak identities can retain the combined make_shared record after a gesture is cancelled. Its vectors and
    // rasters have already been destroyed, but the inline object/control storage stays until this input retires.
    constexpr std::uint64_t extra = 4096 + sizeof(SelectionCommandInput) + sizeof(PreparedSelectionCommand) +
                                    sizeof(BrushStroke) + sizeof(WarpStroke) + 128;
    return bytes && bytes <= (std::numeric_limits<std::uint64_t>::max)() - extra ? bytes + extra : 0;
}
