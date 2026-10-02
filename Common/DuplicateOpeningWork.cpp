#include "DuplicateOpeningWork.h"

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
bool Checkpoint(DuplicateOpeningObserver observer, DuplicateOpeningStage stage, CancelToken cancellation)
{
    if (cancellation.IsCancelled())
        return false;
    if (observer.checkpoint)
        observer.checkpoint(observer.context, stage);
    return !cancellation.IsCancelled();
}
} // namespace

std::uint64_t DocumentSession::DuplicateOpeningPreparationMemoryBytes() const
{
    if (!_document || !_activeLayerID || _transformEdit || _transformDuplicate || _gradient || _pixelMove || _brush ||
        _warp || _colorEdit || _shapeDraft || _lassoDraft || _selectionMoveOrigin || TransformsAsGroup() ||
        !CanTransform() || _document->layers.size() >= static_cast<std::size_t>(kMaxLayerCount) ||
        history.CaptureState().Depth() == (std::numeric_limits<int>::max)() ||
        _generation == (std::numeric_limits<std::uint64_t>::max)() ||
        _editRevision == (std::numeric_limits<std::uint64_t>::max)())
        return 0;
    const auto* layer = ActiveLayer();
    if (!layer || !layer->asset || layer->asset->image.PixelCount() > PixelBudgetLeft(false) ||
        (layer->mask && layer->mask->asset.image.PixelCount() > PixelBudgetLeft(true)))
        return 0;
    auto bytes = ImageImportRetirementMemoryBytes();
    const auto historyBytes = history.EditMemoryBytes(_document);
    return bytes && Add(bytes, historyBytes) && Add(bytes, historyBytes) &&
                   Add(bytes, sizeof(DuplicateOpeningInput) + sizeof(PreparedDuplicateOpening) +
                                  sizeof(PreparedDuplicateRestore) + 8192) &&
                   Add(bytes, (_document->layers.size() + _selected.size() + 1) *
                                  (sizeof(ImageLayer) + sizeof(Guid) * 8ull + 256))
               ? bytes
               : 0;
}

std::optional<DuplicateOpeningInput> DocumentSession::PrepareDuplicateOpeningInput() const
{
    if (!DuplicateOpeningPreparationMemoryBytes())
        return std::nullopt;
    return DuplicateOpeningInput(CaptureImageImportAuthority(), _document.value(), _activeLayerID.value(),
                                 history.CaptureState(), _selected, _editRevision);
}

DuplicateOpeningResult DocumentSession::PrepareDuplicateOpeningPublication(
    DuplicateOpeningInput input, Executor& executor, CancelToken cancellation,
    DocumentHistory::PrepareObserver historyObserver, DuplicateOpeningObserver observer)
{
    cancellation = cancellation.WithShutdown(executor.ShutdownToken());
    if (!input._valid || input._history.Depth() < 0 || input._history.Depth() == (std::numeric_limits<int>::max)() ||
        input._authority._generation == (std::numeric_limits<std::uint64_t>::max)() ||
        input._revision == (std::numeric_limits<std::uint64_t>::max)())
        return {};
    if (!Checkpoint(observer, DuplicateOpeningStage::Layer, cancellation))
        return {DuplicateOpeningStatus::Cancelled};
    const auto index = IndexOf(input._source.layers, input._active);
    if (!index || ValidateDocument(input._source) != DocumentStatus::Ok)
        return {};
    const auto& original = input._source.layers[index.value()];
    if (original.isGroup || !original.asset)
        return {};
    if (input._source.layers.size() >= static_cast<std::size_t>(kMaxLayerCount))
        return {DuplicateOpeningStatus::TooLarge};
    ImageLayer duplicate = original;
    duplicate.id = Guid::Generate();
    duplicate.name += L" copy";
    const auto duplicateID = duplicate.id;
    const std::pair<Guid, Guid> pair{duplicateID, original.id};
    TransformEdit edit{duplicateID, duplicate.transform, false, std::nullopt, false, std::nullopt};
    if (!Checkpoint(observer, DuplicateOpeningStage::Document, cancellation))
        return {DuplicateOpeningStatus::Cancelled};
    auto after = input._source;
    after.layers.insert(after.layers.begin() + static_cast<std::ptrdiff_t>(index.value() + 1), std::move(duplicate));
    const auto validation = ValidateDocument(after);
    if (validation != DocumentStatus::Ok)
        return {validation == DocumentStatus::TooLarge ? DuplicateOpeningStatus::TooLarge
                                                       : DuplicateOpeningStatus::Invalid};
    PreparedImageImport opening(input._authority, input._history);
    if (!Checkpoint(observer, DuplicateOpeningStage::History, cancellation))
        return {DuplicateOpeningStatus::Cancelled};
    opening._history = DocumentHistory::PrepareBegin(input._history, input._source, input._active, L"Duplicate Layer",
                                                     cancellation, historyObserver);
    if (!opening._history)
        return {cancellation.IsCancelled() ? DuplicateOpeningStatus::Cancelled : DuplicateOpeningStatus::Invalid};
    auto following = DocumentHistory::FollowingPreparedState(opening._history.value());
    if (!following)
        return {};
    if (!Checkpoint(observer, DuplicateOpeningStage::Restoration, cancellation))
        return {DuplicateOpeningStatus::Cancelled};
    auto restoreAuthority = input._authority;
    ++restoreAuthority._generation;
    restoreAuthority._active = duplicateID;
    restoreAuthority._maskSelected = false;
    PreparedImageImport restoration(std::move(restoreAuthority), following.value());
    restoration._history = DocumentHistory::PrepareEnd(std::move(following.value()), input._source, input._active,
                                                       cancellation, historyObserver);
    if (!restoration._history || cancellation.IsCancelled())
        return {cancellation.IsCancelled() ? DuplicateOpeningStatus::Cancelled : DuplicateOpeningStatus::Invalid};
    restoration._document = input._source;
    restoration._active = input._active;
    restoration._selected = std::move(input._selected);
    restoration._maskSelected = input._authority._maskSelected;
    restoration._changesDocument = true;
    auto restore = std::make_shared<PreparedDuplicateRestore>(PreparedDuplicateRestore(std::move(restoration), pair));
    if (!Checkpoint(observer, DuplicateOpeningStage::Publication, cancellation))
        return {DuplicateOpeningStatus::Cancelled};
    opening._source = std::move(input._source);
    opening._document = std::move(after);
    opening._active = duplicateID;
    opening._selected.insert(duplicateID);
    opening._maskSelected = false;
    opening._changesDocument = true;
    if (cancellation.IsCancelled())
        return {DuplicateOpeningStatus::Cancelled};
    DuplicateOpeningResult result;
    result.status = DuplicateOpeningStatus::Ready;
    result.publication.emplace(
        PreparedDuplicateOpening(std::move(opening), std::move(edit), pair, std::move(restore), input._revision));
    return result;
}

bool DocumentSession::ApplyPreparedDuplicateOpeningIfCurrent(PreparedDuplicateOpening& prepared) noexcept
{
    static_assert(std::is_nothrow_swappable_v<std::optional<TransformEdit>>);
    static_assert(std::is_nothrow_swappable_v<std::optional<std::pair<Guid, Guid>>>);
    static_assert(std::is_nothrow_swappable_v<SnapGuides>);
    if (!_document || !_activeLayerID || !prepared._restore || !prepared._edit || !prepared._duplicate ||
        _transformEdit || _transformDuplicate || _gradient || _pixelMove || _brush || _warp || _colorEdit ||
        _shapeDraft || _lassoDraft || _selectionMoveOrigin || _selected.size() > 1 ||
        _editRevision != prepared._revision || !ApplyPreparedImageImportIfCurrent(prepared._publication))
        return false;
    std::swap(_transformEdit, prepared._edit);
    std::swap(_transformDuplicate, prepared._duplicate);
    std::swap(_snapGuides, prepared._retiredGuides);
    return true;
}

bool DocumentSession::IsDuplicateRestoreCurrent(const PreparedDuplicateRestore& prepared) const noexcept
{
    return _document && _transformEdit && !_transformEdit->floating &&
           _transformEdit->layerID == prepared._duplicate.first && _transformDuplicate == prepared._duplicate &&
           !_gradient && !_pixelMove && !_brush && !_warp && !_colorEdit && !_shapeDraft && !_lassoDraft &&
           !_selectionMoveOrigin && CanApplyImageImport(prepared._publication._authority) &&
           prepared._publication._history && history.CanPublish(prepared._publication._history.value());
}

bool DocumentSession::ApplyPreparedDuplicateRestoreIfCurrent(PreparedDuplicateRestore& prepared) noexcept
{
    if (!IsDuplicateRestoreCurrent(prepared) || !ApplyPreparedImageImportIfCurrent(prepared._publication))
        return false;
    std::swap(_transformEdit, prepared._retiredEdit);
    std::swap(_transformDuplicate, prepared._retiredDuplicate);
    std::swap(_snapGuides, prepared._retiredGuides);
    return true;
}
