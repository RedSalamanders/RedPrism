#include "AdjustmentOpeningWork.h"
#include "Adjust.h"
#include "ColorEditWork.h"
#include <algorithm>
#include <limits>
#include <type_traits>
#include <utility>

std::optional<AdjustmentOpeningInput> DocumentSession::PrepareAdjustmentOpeningInput() const
{
    if (!_document || !_adjustmentEditingID || _adjustmentOriginal || _colorEdit ||
        _colorEditSequence == (std::numeric_limits<std::uint64_t>::max)())
        return std::nullopt;
    auto token = CaptureJobToken(std::nullopt, _colorEditSequence);
    if (!token)
        return std::nullopt;
    return AdjustmentOpeningInput{std::move(token.value()), _document->id, _adjustmentEditingID.value(),
                                  _activeLayerID,           _maskSelected, _cropRect};
}

std::optional<PreparedAdjustmentOpening> DocumentSession::PrepareAdjustmentOpeningPublication(
    AdjustmentOpeningInput input, const std::optional<CanvasDocument>& source, Raster below,
    DocumentHistory::StateView history, CancelToken cancellation, DocumentHistory::PrepareObserver observer)
{
    if (cancellation.IsCancelled() || !source || input.token.target || source->id != input.documentID ||
        source->width != input.token.canvasWidth || source->height != input.token.canvasHeight || !below.HasStorage() ||
        below.Format() != RasterFormat::Bgra8888 || below.Width() != source->width ||
        below.Height() != source->height || input.token.sequence == (std::numeric_limits<std::uint64_t>::max)())
        return std::nullopt;
    const auto target = std::find_if(source->layers.begin(), source->layers.end(),
                                     [&](const auto& layer) { return layer.id == input.layerID; });
    if (target == source->layers.end() || !target->adjustment || !target->adjustment->IsValid())
        return std::nullopt;
    if (observer.checkpoint)
        observer.checkpoint(observer.context, DocumentHistory::PrepareStage::AdjustmentSnapshot);
    if (cancellation.IsCancelled())
        return std::nullopt;
    const auto original = target->adjustment.value();
    ColorEdit panel;
    panel.deferred = true;
    panel.kind = AdjustmentFilterKind(original.kind);
    panel.layerID = input.layerID;
    panel.original = std::move(below);
    panel.transform.origin = Vec2{0, 0};
    panel.transform.size = Vec2{static_cast<double>(source->width), static_cast<double>(source->height)};
    panel.settings = Adjust::AdjustmentSettings(original);
    panel.seed = original.Grain().seed;
    panel.openingSequence = input.token.sequence + 1;
    // Keep this opening's exact target authority while owner preview captures retain only shared roots
    // and bounded descriptors. The original input stays a whole-document opening token.
    panel.sourceLayers = source->layers;
    panel.sourceToken = input.token;
    panel.sourceToken->sequence = panel.openingSequence;
    panel.sourceToken->target = DescribeColorEditTarget(*target);
    auto publication = DocumentHistory::PrepareBegin(
        std::move(history), source, input.active, L"Edit " + std::wstring(FilterKindName(panel.kind)) + L" Adjustment",
        cancellation, observer);
    if (!publication || cancellation.IsCancelled())
        return std::nullopt;
    return PreparedAdjustmentOpening{std::move(input), std::move(panel), original, std::move(publication.value())};
}

bool DocumentSession::IsPreparedAdjustmentOpeningCurrent(const PreparedAdjustmentOpening& prepared) const noexcept
{
    const auto& input = prepared._input;
    return _document && _document->id == input.documentID && !_colorEdit && !_adjustmentOriginal &&
           _adjustmentEditingID == std::optional(input.layerID) && _activeLayerID == input.active &&
           _maskSelected == input.maskSelected && _cropRect == input.crop &&
           _colorEditSequence == input.token.sequence && CanApplyJobResult(input.token, _colorEditSequence) &&
           history.CanPublish(prepared._history);
}

bool DocumentSession::ApplyPreparedAdjustmentOpeningIfCurrent(PreparedAdjustmentOpening& prepared) noexcept
{
    static_assert(std::is_nothrow_swappable_v<std::optional<ColorEdit>>);
    static_assert(std::is_nothrow_swappable_v<std::optional<LayerAdjustment>>);
    static_assert(std::is_nothrow_swappable_v<std::optional<CanvasDocument>>);
    if (!prepared._panel || !prepared._original || !IsPreparedAdjustmentOpeningCurrent(prepared) ||
        !history.Publish(prepared._history))
        return false;
    std::swap(_colorEdit, prepared._panel);
    std::swap(_adjustmentOriginal, prepared._original);
    std::swap(_displayCopy, prepared._retiredDisplay);
    ++_colorEditSequence;
    ++_editRevision;
    return true;
}