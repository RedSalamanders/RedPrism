#include "TransformCommitWork.h"

#include <limits>
#include <type_traits>
#include <utility>

namespace
{
bool AddCommitBytes(std::uint64_t& bytes, std::uint64_t amount) noexcept
{
    if (amount > (std::numeric_limits<std::uint64_t>::max)() - bytes)
        return false;
    bytes += amount;
    return true;
}
} // namespace

std::uint64_t DocumentSession::TransformCommitPreparationMemoryBytes() const noexcept
{
    if (!_document || !_transformEdit || _transformEdit->floating || _gradient || _pixelMove || _brush || _warp ||
        _colorEdit || _shapeDraft || _selectionMoveOrigin || _lassoDraft)
        return 0;
    auto bytes = TransformPixelsMemoryBytes(_document.value(), _transformEdit.value());
    const auto historyBytes = history.EditMemoryBytes(_document);
    const auto retirementBytes = ImageImportRetirementMemoryBytes();
    if (!bytes || !historyBytes || !retirementBytes || !AddCommitBytes(bytes, historyBytes) ||
        !AddCommitBytes(bytes, retirementBytes) ||
        !AddCommitBytes(bytes, sizeof(TransformCommitInput) + sizeof(PreparedTransformCommit) + 4096) ||
        !AddCommitBytes(bytes, (_selected.size() + _document->layers.size()) * (sizeof(Guid) * 8ull + 256)))
        return 0;
    if (_transformEdit->group && !AddCommitBytes(bytes, _transformEdit->group->originals.size() *
                                                            (sizeof(std::pair<Guid, LayerTransform>) + 64)))
        return 0;
    return bytes;
}

std::optional<TransformCommitInput> DocumentSession::PrepareTransformCommitInput() const
{
    if (!TransformCommitPreparationMemoryBytes())
        return std::nullopt;
    return TransformCommitInput(CaptureImageImportAuthority(), _document.value(), _activeLayerID,
                                history.CaptureState(), _transformEdit.value(), _selected, _transformDuplicate,
                                _editRevision);
}

TransformCommitResult DocumentSession::PrepareTransformCommitPublication(TransformCommitInput input, Executor& executor,
                                                                         CancelToken cancellation,
                                                                         DocumentHistory::PrepareObserver observer)
{
    cancellation = cancellation.WithShutdown(executor.ShutdownToken());
    if (!input._valid)
        return {};
    if (cancellation.IsCancelled())
        return {TransformPixelsStatus::Cancelled};
    auto pixels = PrepareTransformPixels(input._source, input._edit, executor, cancellation);
    if (pixels.status != TransformPixelsStatus::Ready || !pixels.document)
        return {pixels.status};
    if (ValidateDocument(pixels.document.value()) != DocumentStatus::Ok)
        return {};
    // Ordinary transforms begin/end an inner edit. A pending duplicate owns the outer transaction, which
    // CommitTransform closes exactly once. Preserve that opening name/document instead of creating a second entry.
    PreparedImageImport publication(std::move(input._authority), input._history);
    publication._history =
        input._duplicate
            ? DocumentHistory::PrepareEnd(std::move(input._history), pixels.document, input._active, cancellation,
                                          observer)
            : DocumentHistory::PrepareEdit(std::move(input._history), input._source, input._active, pixels.document,
                                           input._active, std::move(pixels.historyName), cancellation, observer);
    if (!publication._history || cancellation.IsCancelled())
        return {cancellation.IsCancelled() ? TransformPixelsStatus::Cancelled : TransformPixelsStatus::Invalid};
    publication._source = std::move(input._source);
    publication._document = std::move(pixels.document);
    publication._active = input._active;
    publication._selected = std::move(input._selected);
    publication._maskSelected = publication._authority._maskSelected;
    publication._changesDocument = true;
    TransformCommitResult result;
    result.status = TransformPixelsStatus::Ready;
    result.publication.emplace(
        PreparedTransformCommit(std::move(publication), input._duplicate, input._revision, pixels.budgetRefused));
    return result;
}

bool DocumentSession::ApplyPreparedTransformCommitIfCurrent(PreparedTransformCommit& prepared) noexcept
{
    static_assert(std::is_nothrow_swappable_v<std::optional<TransformEdit>>);
    static_assert(std::is_nothrow_swappable_v<SnapGuides>);
    if (!_transformEdit || _transformEdit->floating || _editRevision != prepared._revision ||
        _transformDuplicate != prepared._duplicate || _gradient || _pixelMove || _brush || _warp || _colorEdit ||
        _shapeDraft || _selectionMoveOrigin || _lassoDraft || !ApplyPreparedImageImportIfCurrent(prepared._publication))
        return false;
    std::swap(_transformEdit, prepared._retiredEdit);
    std::swap(_snapGuides, prepared._retiredGuides);
    std::swap(_transformDuplicate, prepared._retiredDuplicate);
    _budgetRefused |= prepared._refused;
    return true;
}
