#include "DocumentSizeHistory.h"

#include <limits>
#include <type_traits>
#include <utility>

static_assert(std::is_nothrow_swappable_v<std::optional<CanvasDocument>>);
static_assert(std::is_nothrow_swappable_v<std::optional<LassoDraft>>);
static_assert(std::is_nothrow_swappable_v<std::optional<DocumentSelection>>);

std::optional<PreparedDocumentSize> PrepareDocumentSizePublication(
    const std::optional<CanvasDocument>& source, const std::optional<Guid>& active, JobToken token,
    DocumentHistory::StateView history, CanvasDocument result, std::wstring name, CancelToken cancellation,
    DocumentHistory::PrepareObserver observer, std::shared_ptr<const SharedPublicationMemory> memory)
{
    if (cancellation.IsCancelled() || !source || token.target || token.canvasWidth != source->width ||
        token.canvasHeight != source->height || result.id != source->id ||
        ValidateDocument(source.value()) != DocumentStatus::Ok || ValidateDocument(result) != DocumentStatus::Ok)
        return std::nullopt;
    std::optional<CanvasDocument> after{std::move(result)};
    auto publication = DocumentHistory::PrepareEdit(std::move(history), source, active, after, active, std::move(name),
                                                    cancellation, observer, memory);
    if (!publication || cancellation.IsCancelled())
        return std::nullopt;
    return PreparedDocumentSize(std::move(token), active, std::move(after.value()), std::move(publication.value()),
                                std::move(memory));
}

DocumentHistory::ResidencyValidation PreparedDocumentSize::TryAcquireHistoryResidency(
    DocumentHistory::ResidencyPermit& permit) noexcept
{
    if (!_valid || _applied || !_document)
        return DocumentHistory::ResidencyValidation::Invalid;
    return permit.TryAcquire(_history);
}

bool PreparedDocumentSize::RefreshHistoryResidency(CancelToken cancellation, DocumentHistory::PrepareObserver observer)
{
    return _valid && !_applied && _document && !cancellation.IsCancelled() &&
           DocumentHistory::RefreshPreparedResidency(_history, _document, cancellation, observer);
}

bool DocumentSession::IsPreparedDocumentSizeCurrent(const PreparedDocumentSize& prepared) const noexcept
{
    return prepared._valid && !prepared._applied && !HasDeferredTransform() && prepared._document &&
           _activeLayerID == prepared._active && CanApplyJobResult(prepared._token, prepared._token.sequence) &&
           history.CanPublish(prepared._history);
}

bool DocumentSession::ApplyPreparedDocumentSizeIfCurrent(PreparedDocumentSize& prepared) noexcept
{
    if (!IsPreparedDocumentSizeCurrent(prepared))
        return false;
    if (!history.Publish(prepared._history))
        return false;
    std::swap(_document, prepared._document);
    prepared._applied = true;
    std::swap(_lassoDraft, prepared._retiredLasso);
    InvalidateLassoPendingIntents();
    std::swap(_selectionMoveOrigin, prepared._retiredSelectionMove);
    _selectionMoveOffset.reset();
    NoteDocumentMutation();
    return true;
}

std::uint64_t DocumentSession::DocumentSizeRetirementMemoryBytes() const noexcept
{
    auto bytes = history.RetirementMemoryBytes(_document);
    if (!bytes)
        return 0;
    const auto add = [&](std::uint64_t count, std::uint64_t size) noexcept
    {
        if ((size && count > (std::numeric_limits<std::uint64_t>::max)() / size) ||
            count * size > (std::numeric_limits<std::uint64_t>::max)() - bytes)
            return false;
        bytes += count * size;
        return true;
    };
    if ((_lassoDraft && !add(1, _lassoDraft->RetirementMemoryBytes())) ||
        (_selectionMoveOrigin && !add(1, _selectionMoveOrigin->loops.StorageBytes())))
        return 0;
    return bytes;
}
