#include "AdjustmentClosureWork.h"

#include "Adjust.h"

#include <algorithm>
#include <type_traits>
#include <utility>

std::optional<AdjustmentClosureInput> DocumentSession::PrepareAdjustmentClosureInput(bool commit) const
{
    if (!_document || !_colorEdit || !_adjustmentEditingID || !_adjustmentOriginal ||
        _colorEdit->layerID != _adjustmentEditingID.value() || !_adjustmentOriginal->IsValid() ||
        history.CaptureState().Depth() <= 0)
        return std::nullopt;
    const auto* target = FindLayer(_adjustmentEditingID.value());
    if (commit && (!target || !target->adjustment || target->adjustment->kind != _adjustmentOriginal->kind))
        return std::nullopt;
    auto token = CaptureJobToken(std::nullopt, _colorEditSequence);
    if (!token)
        return std::nullopt;
    return AdjustmentClosureInput{std::move(token.value()),
                                  _document->id,
                                  _adjustmentEditingID.value(),
                                  _colorEdit->openingSequence,
                                  _activeLayerID,
                                  _maskSelected,
                                  _cropRect,
                                  _adjustmentOriginal.value(),
                                  _colorEdit->settings,
                                  commit};
}

std::optional<PreparedAdjustmentClosure> DocumentSession::PrepareAdjustmentClosurePublication(
    AdjustmentClosureInput input, const std::optional<CanvasDocument>& source, DocumentHistory::StateView history,
    CancelToken cancellation, DocumentHistory::PrepareObserver observer)
{
    if (cancellation.IsCancelled() || !source || input.opening == 0 || input.token.target ||
        source->id != input.documentID || source->width != input.token.canvasWidth ||
        source->height != input.token.canvasHeight || !input.original.IsValid() || history.Depth() <= 0)
        return std::nullopt;
    if (observer.checkpoint)
        observer.checkpoint(observer.context, DocumentHistory::PrepareStage::AdjustmentSnapshot);
    if (cancellation.IsCancelled())
        return std::nullopt;

    CanvasDocument after;
    after.id = source->id;
    after.width = source->width;
    after.height = source->height;
    after.resolution = source->resolution;
    after.selection = source->selection;
    after.layers.reserve(source->layers.size());
    for (const auto& layer : source->layers)
    {
        if (cancellation.IsCancelled())
            return std::nullopt;
        after.layers.push_back(layer);
    }
    const auto target = std::find_if(after.layers.begin(), after.layers.end(),
                                     [&](const auto& layer) { return layer.id == input.layerID; });
    if (input.commit &&
        (target == after.layers.end() || !target->adjustment || target->adjustment->kind != input.original.kind))
        return std::nullopt;
    // Cancel mirrors UpdateAdjustment: restore when the adjustment still exists, and close even if it is gone.
    if (target != after.layers.end() && target->adjustment)
    {
        auto adjusted = input.commit ? Adjust::AdjustmentWithSettings(input.original, input.settings) : input.original;
        if (!adjusted.IsValid())
            return std::nullopt;
        target->adjustment = std::move(adjusted);
    }
    if (cancellation.IsCancelled() || ValidateDocument(after) != DocumentStatus::Ok)
        return std::nullopt;
    std::optional<CanvasDocument> finished{std::move(after)};
    auto publication = DocumentHistory::PrepareEnd(std::move(history), finished, input.active, cancellation, observer);
    if (!publication || cancellation.IsCancelled())
        return std::nullopt;
    return PreparedAdjustmentClosure{std::move(input), std::move(finished), std::move(publication.value())};
}

bool DocumentSession::IsPreparedAdjustmentClosureCurrent(const PreparedAdjustmentClosure& prepared) const noexcept
{
    const auto& input = prepared._input;
    return _document && _document->id == input.documentID && _colorEdit && _adjustmentOriginal &&
           _adjustmentEditingID == std::optional(input.layerID) && _colorEdit->layerID == input.layerID &&
           _colorEdit->openingSequence == input.opening && _colorEditSequence == input.token.sequence &&
           _colorEdit->settings == input.settings && _adjustmentOriginal.value() == input.original &&
           _activeLayerID == input.active && _maskSelected == input.maskSelected && _cropRect == input.crop &&
           CanApplyJobResult(input.token, _colorEditSequence) && history.CanPublish(prepared._history);
}

bool DocumentSession::ApplyPreparedAdjustmentClosureIfCurrent(PreparedAdjustmentClosure& prepared) noexcept
{
    static_assert(std::is_nothrow_swappable_v<std::optional<CanvasDocument>>);
    static_assert(std::is_nothrow_swappable_v<std::optional<ColorEdit>>);
    static_assert(std::is_nothrow_swappable_v<std::optional<LayerAdjustment>>);
    if (!prepared._document || !IsPreparedAdjustmentClosureCurrent(prepared) || !history.Publish(prepared._history))
        return false;
    std::swap(_document, prepared._document);
    std::swap(_displayCopy, prepared._retiredDisplay);
    std::swap(_colorEdit, prepared._retiredPanel);
    std::swap(_adjustmentOriginal, prepared._retiredOriginal);
    _adjustmentEditingID.reset();
    ++_colorEditSequence;
    NoteDocumentMutation(); // acknowledgement closes End even for unchanged metadata; old whole tokens expire
    return true;
}
