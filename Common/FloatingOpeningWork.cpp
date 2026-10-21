#include "FloatingOpeningWork.h"

#include <limits>
#include <type_traits>
#include <utility>

namespace
{
bool Add(std::uint64_t& bytes, std::uint64_t amount) noexcept
{
    if (!amount || amount > (std::numeric_limits<std::uint64_t>::max)() - bytes)
        return false;
    bytes += amount;
    return true;
}
bool Checkpoint(FloatingOpeningObserver observer, FloatingOpeningStage stage, CancelToken cancellation)
{
    if (cancellation.IsCancelled())
        return false;
    if (observer.checkpoint)
        observer.checkpoint(observer.context, stage);
    return !cancellation.IsCancelled();
}
} // namespace

std::uint64_t DocumentSession::FloatingOpeningPreparationMemoryBytes() const noexcept
{
    if (!CanTransformSelection() || !_document || !_activeLayerID || _transformDuplicate || _gradient || _pixelMove ||
        _brush || HasPendingWarp() || _colorEdit || _shapeDraft || _lassoDraft || _selectionMoveOrigin ||
        history.CaptureState().Depth() == (std::numeric_limits<int>::max)() ||
        _generation == (std::numeric_limits<std::uint64_t>::max)())
        return 0;
    auto bytes = FloatingOpeningPixelsMemoryBytes(_document.value(), _activeLayerID.value());
    const auto historyBytes = history.EditMemoryBytes(_document);
    const auto retirementBytes = ImageImportRetirementMemoryBytes();
    return bytes && Add(bytes, historyBytes) && Add(bytes, historyBytes) && Add(bytes, retirementBytes) &&
                   Add(bytes, sizeof(FloatingOpeningInput) + sizeof(PreparedFloatingOpening) +
                                  sizeof(PreparedFloatingRestore) + 8192) &&
                   Add(bytes, (_document->layers.size() + _selected.size() + 1) * (sizeof(Guid) * 8ull + 256))
               ? bytes
               : 0;
}

std::optional<FloatingOpeningInput> DocumentSession::PrepareFloatingOpeningInput() const
{
    if (!FloatingOpeningPreparationMemoryBytes())
        return std::nullopt;
    return FloatingOpeningInput(CaptureImageImportAuthority(), _document.value(), _activeLayerID.value(),
                                history.CaptureState(), _selected, _editRevision);
}

FloatingOpeningResult DocumentSession::PrepareFloatingOpeningPublication(FloatingOpeningInput input, Executor& executor,
                                                                         CancelToken cancellation,
                                                                         DocumentHistory::PrepareObserver observer,
                                                                         FloatingOpeningObserver openingObserver)
{
    cancellation = cancellation.WithShutdown(executor.ShutdownToken());
    if (!input._valid || input._history.Depth() < 0 || input._history.Depth() == (std::numeric_limits<int>::max)() ||
        input._authority._generation == (std::numeric_limits<std::uint64_t>::max)())
        return {};
    if (cancellation.IsCancelled())
        return {FloatingPixelsStatus::Cancelled};
    if (!Checkpoint(openingObserver, FloatingOpeningStage::Pixels, cancellation))
        return {FloatingPixelsStatus::Cancelled};
    auto pixels = PrepareFloatingOpeningPixels(input._source, input._active, executor, cancellation);
    if (pixels.status != FloatingPixelsStatus::Ready || !pixels.document || !pixels.edit)
        return {pixels.status};
    PreparedImageImport opening(input._authority, input._history);
    if (!Checkpoint(openingObserver, FloatingOpeningStage::History, cancellation))
        return {FloatingPixelsStatus::Cancelled};
    opening._history = DocumentHistory::PrepareBegin(input._history, input._source, input._active,
                                                     L"Transform Selection", cancellation, observer);
    if (!opening._history)
        return {cancellation.IsCancelled() ? FloatingPixelsStatus::Cancelled : FloatingPixelsStatus::Invalid};
    auto following = DocumentHistory::FollowingPreparedState(opening._history.value());
    if (!following)
        return {};
    auto restoreAuthority = input._authority;
    ++restoreAuthority._generation;
    restoreAuthority._active = pixels.edit->layerID;
    restoreAuthority._maskSelected = false;
    PreparedImageImport restoration(std::move(restoreAuthority), following.value());
    restoration._history =
        DocumentHistory::PrepareEnd(std::move(following.value()), input._source, input._active, cancellation, observer);
    if (!restoration._history || cancellation.IsCancelled())
        return {cancellation.IsCancelled() ? FloatingPixelsStatus::Cancelled : FloatingPixelsStatus::Invalid};
    if (!Checkpoint(openingObserver, FloatingOpeningStage::RestorationDocument, cancellation))
        return {FloatingPixelsStatus::Cancelled};
    restoration._document = input._source;
    restoration._active = input._active;
    restoration._selected = std::move(input._selected);
    restoration._maskSelected = input._authority._maskSelected;
    restoration._changesDocument = true;
    if (!Checkpoint(openingObserver, FloatingOpeningStage::RestorationOwner, cancellation))
        return {FloatingPixelsStatus::Cancelled};
    auto restore = std::make_shared<PreparedFloatingRestore>(
        PreparedFloatingRestore(std::move(restoration), pixels.edit->layerID, pixels.edit->floating->before));
    if (!Checkpoint(openingObserver, FloatingOpeningStage::Publication, cancellation))
        return {FloatingPixelsStatus::Cancelled};
    opening._source = std::move(input._source);
    opening._document = std::move(pixels.document);
    opening._active = pixels.edit->layerID;
    opening._selected.insert(pixels.edit->layerID);
    opening._maskSelected = false;
    opening._changesDocument = true;
    if (cancellation.IsCancelled())
        return {FloatingPixelsStatus::Cancelled};
    FloatingOpeningResult result;
    result.status = FloatingPixelsStatus::Ready;
    result.publication.emplace(PreparedFloatingOpening(std::move(opening), std::move(pixels.edit.value()),
                                                       std::move(restore), input._revision));
    return result;
}

bool DocumentSession::ApplyPreparedFloatingOpeningIfCurrent(PreparedFloatingOpening& prepared) noexcept
{
    static_assert(std::is_nothrow_swappable_v<std::optional<TransformEdit>>);
    static_assert(std::is_nothrow_swappable_v<SnapGuides>);
    if (!prepared._restore || !prepared._edit || !prepared._edit->floating || !CanTransformSelection() ||
        _editRevision != prepared._revision || _transformDuplicate || _gradient || _pixelMove || _brush ||
        HasPendingWarp() || _colorEdit || _shapeDraft || _lassoDraft || _selectionMoveOrigin ||
        !ApplyPreparedImageImportIfCurrent(prepared._publication))
        return false;
    std::swap(_transformEdit, prepared._edit);
    std::swap(_snapGuides, prepared._retiredGuides);
    return true;
}

bool DocumentSession::ApplyPreparedFloatingRestoreIfCurrent(PreparedFloatingRestore& prepared) noexcept
{
    if (!IsFloatingRestoreCurrent(prepared) || !ApplyPreparedImageImportIfCurrent(prepared._publication))
        return false;
    std::swap(_transformEdit, prepared._retiredEdit);
    std::swap(_snapGuides, prepared._retiredGuides);
    return true;
}
