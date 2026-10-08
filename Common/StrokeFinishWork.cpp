#include "StrokeFinishWork.h"

#include "DocumentDraft.h"
#include "RasterWork.h"

#include <type_traits>
#include <utility>

namespace
{
[[nodiscard]] bool Checkpoint(StrokeFinishObserver observer, StrokeFinishStage stage, CancelToken cancellation)
{
    if (cancellation.IsCancelled())
        return false;
    if (observer.checkpoint)
        observer.checkpoint(observer.context, stage);
    return !cancellation.IsCancelled();
}

[[nodiscard]] bool SameSelection(const std::optional<DocumentSelection>& first,
                                 const std::optional<DocumentSelection>& second) noexcept
{
    return first.has_value() == second.has_value() && (!first || (first->antialiased == second->antialiased &&
                                                                  first->loops.Identity() == second->loops.Identity()));
}
} // namespace

std::optional<StrokeFinishInput> DocumentSession::TakeStrokeFinishInput(std::uint64_t opening,
                                                                        BrushCoverageEngine*& ownerEngine) noexcept
{
    static_assert(std::is_nothrow_copy_constructible_v<CanvasDocument>);
    if (ownerEngine || !_document || !_brush || _brushFinishingOpening || _brushUpdatingOpening ||
        opening != _brushOpening || !_brush->IsValid() || !_activeLayerID ||
        _brush->LayerID() != _activeLayerID.value() || _brush->IsMask() != _maskSelected || _transformEdit ||
        _gradient || _pixelMove || _warp || _colorEdit)
        return std::nullopt;
    // All capture here is immutable handle/scalar copying. A failed exclusive transfer leaves the live draft.
    StrokeFinishInput input(CaptureImageImportAuthority(), _document.value(), _activeLayerID, history.CaptureState(),
                            _brush, opening, 0);
    ownerEngine = _brush->DetachCoverageForFinish();
    if (!ownerEngine)
        return std::nullopt;
    if (_brush->Healing())
        input._healSeed = NextHealSeed();
    _brush.reset(); // The captured private input retains the CPU draft; no destructor runs here.
    _brushFinishingOpening = opening;
    return input;
}

bool DocumentSession::DiscardStrokeFinishIfCurrent(std::uint64_t opening) noexcept
{
    if (_brushFinishingOpening != opening)
        return false;
    _brushFinishingOpening.reset();
    return true;
}

StrokeFinishResult DocumentSession::PrepareStrokeFinishPublication(StrokeFinishInput input, CancelToken cancellation,
                                                                   StrokeFinishObserver observer,
                                                                   DocumentHistory::PrepareObserver historyObserver)
{
    if (!input._valid || !input._stroke || !input._active || input._stroke->LayerID() != input._active.value())
        return {};
    const auto failed = [&]()
    {
        return StrokeFinishResult{cancellation.IsCancelled() ? StrokeFinishStatus::Cancelled
                                                             : StrokeFinishStatus::Invalid};
    };
    if (!Checkpoint(observer, StrokeFinishStage::Heal, cancellation))
        return failed();
    auto& stroke = *input._stroke;
    if (stroke.Healing() && !stroke.HealForFinish(input._healSeed))
        return failed();
    if (!Checkpoint(observer, StrokeFinishStage::Snapshot, cancellation))
        return failed();
    if (!stroke.HasPaint())
        return {StrokeFinishStatus::Unchanged};
    const auto snapshot = stroke.PaintSnapshot();
    if (!snapshot)
        return failed();

    std::optional<std::size_t> index;
    for (std::size_t i = 0; i < input._source.layers.size(); ++i)
    {
        if (cancellation.IsCancelled())
            return failed();
        if (input._source.layers[i].id == stroke.LayerID())
        {
            index = i;
            break;
        }
    }
    if (!index)
        return {};
    const auto& sourceLayer = input._source.layers[index.value()];
    if (stroke.IsMask() ? !sourceLayer.mask || sourceLayer.mask->asset.image != stroke.SourceImage()
                        : (sourceLayer.asset ? sourceLayer.asset->image : Raster{}) != stroke.SourceImage() ||
                              sourceLayer.transform != stroke.SourceTransform())
        return {};

    auto draft = CanvasDocumentDraft::Clone(input._source, input._source.layers.size(), cancellation);
    if (!draft)
        return failed();
    if (!Checkpoint(observer, StrokeFinishStage::Mask, cancellation))
        return failed();
    {
        auto& edited = draft.value().EditLayers()[index.value()];
        if (stroke.IsMask())
        {
            if (!snapshot->thumbnail.HasStorage())
                return {};
            edited.mask->asset.image = snapshot->image;
            edited.mask->asset.thumbnail = snapshot->thumbnail;
        }
        else
        {
            if (edited.mask && !edited.mask->placement && snapshot->bounds != stroke.SourceRect() &&
                edited.mask->asset.image.PixelCount() > 1)
            {
                const auto& source = stroke.SourceRect();
                auto grown = CarriedMaskForWork(edited.mask->asset.image, source.width, source.height,
                                                source.x - snapshot->bounds.x, source.y - snapshot->bounds.y,
                                                snapshot->bounds.width, snapshot->bounds.height, 255, cancellation);
                if (!grown.HasStorage())
                    return failed();
                auto thumbnail = ThumbnailRasterForWork(grown, 96, cancellation);
                if (!thumbnail.HasStorage())
                    return failed();
                edited.mask->asset.image = std::move(grown);
                edited.mask->asset.thumbnail = std::move(thumbnail);
            }
            edited.asset = RasterAsset{snapshot->image, snapshot->thumbnail, edited.name};
            edited.transform = snapshot->transform;
        }
    }
    if (!Checkpoint(observer, StrokeFinishStage::Document, cancellation))
        return failed();
    auto after = draft.value().Seal(cancellation);
    if (!after)
        return failed();
    const auto valid = ValidateDocument(after.value());
    if (valid != DocumentStatus::Ok)
        return {valid == DocumentStatus::TooLarge ? StrokeFinishStatus::TooLarge : StrokeFinishStatus::Invalid};
    if (!Checkpoint(observer, StrokeFinishStage::History, cancellation))
        return failed();
    const auto name = !stroke.EditName().empty() ? stroke.EditName()
                      : stroke.IsMask()          ? L"Paint Mask"
                      : stroke.Erasing()         ? L"Erase"
                                                 : L"Brush Stroke";
    std::optional<CanvasDocument> afterDocument{std::move(after.value())};
    auto history = DocumentHistory::PrepareEdit(std::move(input._history), input._source, input._active, afterDocument,
                                                input._active, name, cancellation, historyObserver);
    if (!history || cancellation.IsCancelled())
        return failed();
    const BrushCommitInfo commit{snapshot->image,        snapshot->bounds.x,      snapshot->bounds.y,
                                 snapshot->bounds.width, snapshot->bounds.height, stroke.IsMask()};
    StrokeFinishResult result;
    result.status = StrokeFinishStatus::Ready;
    result.publication.emplace(
        PreparedStrokeFinish(std::move(input), std::move(afterDocument.value()), std::move(history.value()), commit));
    return result;
}

DocumentHistory::ResidencyValidation PreparedStrokeFinish::TryAcquireHistoryResidency(
    DocumentHistory::ResidencyPermit& permit) noexcept
{
    if (_applied || !_input._valid)
        return DocumentHistory::ResidencyValidation::Invalid;
    return permit.TryAcquire(_history);
}

bool PreparedStrokeFinish::RefreshHistoryResidency(CancelToken cancellation, DocumentHistory::PrepareObserver observer)
{
    return !_applied && _input._valid && _document && !cancellation.IsCancelled() &&
           DocumentHistory::RefreshPreparedResidency(_history, _document, cancellation, observer);
}

bool DocumentSession::IsPreparedStrokeFinishCurrent(const PreparedStrokeFinish& prepared) const noexcept
{
    return !prepared._applied && prepared._input._valid && prepared._document && _document && !_brush &&
           _brushFinishingOpening == prepared._input._opening && !_transformEdit && !_gradient && !_pixelMove &&
           !_warp && !_colorEdit && CanApplyImageImport(prepared._input._authority) &&
           _document->layers.Identity() == prepared._input._source.layers.Identity() &&
           _document->width == prepared._input._source.width && _document->height == prepared._input._source.height &&
           _document->resolution == prepared._input._source.resolution &&
           SameSelection(_document->selection, prepared._input._source.selection) &&
           history.CanPublish(prepared._history);
}

bool DocumentSession::ApplyPreparedStrokeFinishIfCurrent(PreparedStrokeFinish& prepared) noexcept
{
    static_assert(std::is_nothrow_swappable_v<std::optional<CanvasDocument>>);
    static_assert(std::is_nothrow_swappable_v<std::optional<BrushCommitInfo>>);
    if (!IsPreparedStrokeFinishCurrent(prepared) || !history.Publish(prepared._history))
        return false;
    std::swap(_document, prepared._document);
    std::swap(_displayCopy, prepared._retiredDisplay);
    std::swap(_lastBrushCommit, prepared._commit);
    _brushFinishingOpening.reset();
    NoteDocumentMutation();
    prepared._applied = true;
    return true;
}
