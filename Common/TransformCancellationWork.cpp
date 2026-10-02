#include "TransformCancellationWork.h"

#include <algorithm>
#include <limits>
#include <type_traits>
#include <utility>

std::uint64_t DocumentSession::TransformCancellationPreparationMemoryBytes() const noexcept
{
    if (!_document || !_transformEdit || _transformEdit->floating || _gradient || _pixelMove || _brush || _warp ||
        _colorEdit || _shapeDraft || _selectionMoveOrigin || _lassoDraft ||
        (_transformDuplicate &&
         (_transformDuplicate->first != _transformEdit->layerID || history.CaptureState().Depth() < 1)))
        return 0;
    auto bytes = ImageImportRetirementMemoryBytes();
    if (!bytes)
        return 0;
    const auto add = [&](std::uint64_t amount) noexcept
    {
        if (!amount || amount > (std::numeric_limits<std::uint64_t>::max)() - bytes)
            return false;
        bytes += amount;
        return true;
    };
    if (!add(sizeof(TransformCancellationInput) + sizeof(PreparedTransformCancellation) + 8192))
        return 0;
    if (_transformDuplicate &&
        (!add(history.EditMemoryBytes(_document)) ||
         !add((_document->layers.size() + 1) * (sizeof(ImageLayer) + sizeof(Guid) * 8ull + 256))))
        return 0;
    return bytes;
}

std::optional<TransformCancellationInput> DocumentSession::PrepareTransformCancellationInput() const
{
    if (!TransformCancellationPreparationMemoryBytes())
        return std::nullopt;
    // An ordinary cancellation needs no document snapshot. The old display/group owners move into the
    // result only after authority validation. A duplicate closure constructs its restored records privately.
    return TransformCancellationInput(CaptureImageImportAuthority(), _transformDuplicate ? _document : std::nullopt,
                                      history.CaptureState(), _transformDuplicate, _transformEdit->layerID,
                                      _editRevision);
}

std::optional<PreparedTransformCancellation> DocumentSession::PrepareTransformCancellationPublication(
    TransformCancellationInput input, CancelToken cancellation, DocumentHistory::PrepareObserver observer)
{
    if (!input._valid || cancellation.IsCancelled())
        return std::nullopt;
    PreparedTransformCancellation prepared(std::move(input._authority), input._history, input._duplicate, input._layer,
                                           input._revision);
    if (!input._duplicate)
        return prepared;
    if (!input._source || input._history.Depth() < 1 || input._duplicate->first != input._layer ||
        input._duplicate->first == input._duplicate->second)
        return std::nullopt;
    const auto copyIndex = IndexOf(input._source->layers, input._duplicate->first);
    const auto originalIndex = IndexOf(input._source->layers, input._duplicate->second);
    if (!copyIndex || !originalIndex)
        return std::nullopt;
    CanvasDocument after;
    after.id = input._source->id;
    after.width = input._source->width;
    after.height = input._source->height;
    after.resolution = input._source->resolution;
    after.selection = input._source->selection;
    after.layers.reserve(input._source->layers.size() - 1);
    for (const auto& layer : input._source->layers)
    {
        if (cancellation.IsCancelled())
            return std::nullopt;
        if (layer.id != input._duplicate->first)
            after.layers.push_back(layer);
    }
    if (ValidateDocument(after) != DocumentStatus::Ok)
        return std::nullopt;
    PreparedImageImport publication(prepared._authority, input._history);
    publication._history =
        DocumentHistory::PrepareEnd(input._history, after, input._duplicate->second, cancellation, observer);
    if (!publication._history || cancellation.IsCancelled())
        return std::nullopt;
    publication._source = std::move(input._source);
    publication._document = std::move(after);
    publication._active = input._duplicate->second;
    publication._selected.insert(input._duplicate->second);
    publication._maskSelected = false;
    publication._changesDocument = true;
    if (cancellation.IsCancelled())
        return std::nullopt;
    prepared._publication.emplace(std::move(publication));
    return prepared;
}

bool DocumentSession::ApplyPreparedTransformCancellationIfCurrent(PreparedTransformCancellation& prepared) noexcept
{
    static_assert(std::is_nothrow_swappable_v<std::optional<TransformEdit>>);
    static_assert(std::is_nothrow_swappable_v<SnapGuides>);
    static_assert(std::is_nothrow_swappable_v<std::optional<CanvasDocument>>);
    if (prepared._consumed || !_transformEdit || _transformEdit->floating ||
        _transformEdit->layerID != prepared._layer || _editRevision != prepared._revision ||
        _transformDuplicate != prepared._duplicate || _gradient || _pixelMove || _brush || _warp || _colorEdit ||
        _shapeDraft || _selectionMoveOrigin || _lassoDraft || !CanApplyImageImport(prepared._authority) ||
        !history.IsCurrent(prepared._history))
        return false;
    if (prepared._publication && !ApplyPreparedImageImportIfCurrent(prepared._publication.value()))
        return false;
    std::swap(_transformEdit, prepared._retiredEdit);
    std::swap(_transformDuplicate, prepared._retiredDuplicate);
    std::swap(_snapGuides, prepared._retiredGuides);
    std::swap(_displayCopy, prepared._retiredDisplay);
    // Ordinary cancellation is a draft-only transition: it must not create a document generation or history edit.
    if (!prepared._publication)
        ++_editRevision;
    prepared._consumed = true;
    return true;
}
