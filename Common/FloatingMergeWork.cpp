#include "FloatingMergeWork.h"

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
bool Checkpoint(FloatingMergeObserver observer, FloatingMergeStage stage, CancelToken cancellation)
{
    if (cancellation.IsCancelled())
        return false;
    if (observer.checkpoint)
        observer.checkpoint(observer.context, stage);
    return !cancellation.IsCancelled();
}
} // namespace

bool DocumentSession::IsFloatingRestoreCurrent(const PreparedFloatingRestore& prepared) const noexcept
{
    return _document && _transformEdit && _transformEdit->floating && _transformEdit->layerID == prepared._floating &&
           _transformEdit->floating->before == prepared._before && !_transformDuplicate && !_gradient && !_pixelMove &&
           !_brush && !_warp && !_colorEdit && !_shapeDraft && !_lassoDraft && !_selectionMoveOrigin &&
           CanApplyImageImport(prepared._publication._authority) && prepared._publication._history &&
           history.CanPublish(prepared._publication._history.value());
}

std::uint64_t DocumentSession::FloatingMergePreparationMemoryBytes(
    const PreparedFloatingRestore& restore) const noexcept
{
    if (!IsFloatingRestoreCurrent(restore) || history.CaptureState().Depth() != restore._publication._view.Depth())
        return 0;
    // Invalid/oversized geometry still needs an admitted worker resolution and the prebuilt rollback.
    // A zero pixel plan does not authorize pixel allocation; PrepareFloatingMergePixels refuses it.
    auto bytes = FloatingMergePixelsMemoryBytes(_document.value(), _transformEdit.value());
    if (!bytes)
        bytes = 8192;
    const auto historyBytes = history.EditMemoryBytes(_document);
    const auto retirementBytes = ImageImportRetirementMemoryBytes();
    return Add(bytes, historyBytes) && Add(bytes, retirementBytes) &&
                   Add(bytes, sizeof(FloatingMergeInput) + sizeof(PreparedFloatingMerge) + 8192) &&
                   Add(bytes, (_document->layers.size() + _selected.size() + 1) * (sizeof(Guid) * 8ull + 256))
               ? bytes
               : 0;
}

std::optional<FloatingMergeInput> DocumentSession::PrepareFloatingMergeInput(
    std::shared_ptr<PreparedFloatingRestore> restore) const
{
    if (!restore || !FloatingMergePreparationMemoryBytes(*restore))
        return std::nullopt;
    return FloatingMergeInput(CaptureImageImportAuthority(), _document.value(), history.CaptureState(),
                              _transformEdit.value(), std::move(restore), _editRevision);
}

FloatingMergeResult DocumentSession::PrepareFloatingMergePublication(FloatingMergeInput input, Executor& executor,
                                                                     CancelToken cancellation,
                                                                     DocumentHistory::PrepareObserver observer,
                                                                     FloatingMergeObserver mergeObserver)
{
    cancellation = cancellation.WithShutdown(executor.ShutdownToken());
    if (!input._valid || !input._restore || !input._edit.floating || input._history.Depth() < 1 ||
        input._history.Depth() != input._restore->_publication._view.Depth())
        return {};
    FloatingMergeResult result;
    result.publication.emplace(PreparedFloatingMerge(std::move(input._authority), std::move(input._source),
                                                     std::move(input._history), std::move(input._edit),
                                                     std::move(input._restore), input._revision));
    auto& prepared = result.publication.value();
    if (!Checkpoint(mergeObserver, FloatingMergeStage::Pixels, cancellation))
    {
        result.status = FloatingPixelsStatus::Cancelled;
        return result;
    }
    auto pixels = PrepareFloatingMergePixels(prepared._source.value(), prepared._edit, executor, cancellation);
    result.status = pixels.status;
    prepared._budgetRefused = pixels.budgetRefused;
    if (pixels.status != FloatingPixelsStatus::Ready || !pixels.document || !pixels.active)
        return result;
    if (!Checkpoint(mergeObserver, FloatingMergeStage::History, cancellation))
    {
        result.status = FloatingPixelsStatus::Cancelled;
        return result;
    }
    PreparedImageImport publication(prepared._authority, prepared._history);
    publication._history =
        DocumentHistory::PrepareEnd(prepared._history, pixels.document, pixels.active, cancellation, observer);
    if (!publication._history)
    {
        result.status = cancellation.IsCancelled() ? FloatingPixelsStatus::Cancelled : FloatingPixelsStatus::Invalid;
        return result;
    }
    if (!Checkpoint(mergeObserver, FloatingMergeStage::Publication, cancellation))
    {
        result.status = FloatingPixelsStatus::Cancelled;
        return result;
    }
    publication._source = std::move(prepared._source);
    publication._document = std::move(pixels.document);
    publication._active = pixels.active;
    publication._selected.insert(pixels.active.value());
    publication._maskSelected = false;
    publication._changesDocument = true;
    if (cancellation.IsCancelled())
    {
        result.status = FloatingPixelsStatus::Cancelled;
        return result;
    }
    prepared._publication.emplace(std::move(publication));
    return result;
}

bool DocumentSession::ApplyPreparedFloatingMergeIfCurrent(PreparedFloatingMerge& prepared) noexcept
{
    static_assert(std::is_nothrow_swappable_v<std::optional<TransformEdit>>);
    static_assert(std::is_nothrow_swappable_v<SnapGuides>);
    if (prepared._consumed || !prepared._restore || _editRevision != prepared._revision ||
        !IsFloatingRestoreCurrent(*prepared._restore) || !CanApplyImageImport(prepared._authority) ||
        !history.IsCurrent(prepared._history))
        return false;
    if (prepared._publication)
    {
        if (!ApplyPreparedImageImportIfCurrent(prepared._publication.value()))
            return false;
        std::swap(_transformEdit, prepared._retiredEdit);
        std::swap(_snapGuides, prepared._retiredGuides);
    }
    else if (!ApplyPreparedFloatingRestoreIfCurrent(*prepared._restore))
        return false;
    _budgetRefused |= prepared._budgetRefused;
    prepared._consumed = true;
    return true;
}
