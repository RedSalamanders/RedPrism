#include "ImageImportHistory.h"

#include "BrushStroke.h"
#include "ColorEditHistory.h"
#include "WarpStroke.h"

#include <algorithm>
#include <limits>
#include <type_traits>
#include <utility>

namespace
{
[[nodiscard]] bool BeginReplacementSnapshot(CancelToken cancellation, DocumentHistory::PrepareObserver observer)
{
    if (cancellation.IsCancelled())
        return false;
    if (observer.checkpoint)
        observer.checkpoint(observer.context, DocumentHistory::PrepareStage::ReplacementSnapshot);
    return !cancellation.IsCancelled();
}
} // namespace

ImageImportAuthority DocumentSession::CaptureImageImportAuthority() const noexcept
{
    ImageImportAuthority result;
    result._session = _tokenIdentity.value;
    result._documentSerial = _documentSerial;
    result._generation = _generation;
    result._restoreSerial = _restoreSerial;
    result._documentID = _document ? std::optional(_document->id) : std::optional<Guid>{};
    result._active = _activeLayerID;
    result._crop = _cropRect;
    result._maskSelected = _maskSelected;
    result._valid = true;
    return result;
}

bool DocumentSession::CanApplyImageImport(const ImageImportAuthority& authority) const noexcept
{
    return authority._valid && authority._session == _tokenIdentity.value &&
           authority._documentSerial == _documentSerial && authority._generation == _generation &&
           authority._restoreSerial == _restoreSerial && authority._active == _activeLayerID &&
           authority._maskSelected == _maskSelected && authority._crop == _cropRect &&
           authority._documentID == (_document ? std::optional(_document->id) : std::optional<Guid>{});
}

std::optional<PreparedImageImport> DocumentSession::PrepareImageImportPublication(
    ImageImportAuthority authority, std::optional<CanvasDocument> source, std::optional<Guid> sourceActive,
    DocumentHistory::StateView state, ImageImportBatchResult imported, CancelToken cancellation,
    DocumentHistory::PrepareObserver observer)
{
    if (!authority._valid || cancellation.IsCancelled() || sourceActive != authority._active ||
        (source ? std::optional(source.value().id) : std::optional<Guid>{}) != authority._documentID)
        return std::nullopt;
    PreparedImageImport result(std::move(authority), std::move(state));
    result._source = std::move(source);
    if (!imported.createdDocument && !imported.importedDocument)
        return result; // A batch with no successful image has no publication effect.
    const auto valid = [&](const std::optional<CanvasDocument>& document, const std::optional<Guid>& active)
    {
        if (!document || !active || cancellation.IsCancelled() ||
            ValidateDocument(document.value()) != DocumentStatus::Ok)
            return false;
        return std::any_of(document.value().layers.begin(), document.value().layers.end(),
                           [&](const ImageLayer& layer) { return layer.id == active.value(); });
    };
    if (imported.createdDocument)
    {
        result._retiredDrafts = std::make_unique<PreparedImageImport::RetiredDrafts>();
        if (!valid(imported.createdDocument, imported.createdActiveLayerID) ||
            (imported.importedDocument &&
             (!valid(imported.importedDocument, imported.importedActiveLayerID) ||
              imported.importedDocument.value().id != imported.createdDocument.value().id)))
            return std::nullopt;
        result._baseline = std::move(imported.createdDocument);
        if (imported.importedDocument)
        {
            result._document = std::move(imported.importedDocument);
            result._active = imported.importedActiveLayerID;
        }
        else
        {
            if (!BeginReplacementSnapshot(cancellation, observer))
                return std::nullopt;
            result._document = result._baseline; // Private worker copy; the baseline owns the first undo pixels.
            result._active = imported.createdActiveLayerID;
        }
        result._replacement = DocumentHistory::PrepareReplacement(
            std::move(result._view), result._baseline, imported.createdActiveLayerID, result._document, result._active,
            std::move(imported.historyName), cancellation, observer);
        if (!result._replacement)
            return std::nullopt;
    }
    else
    {
        if (!result._source || !valid(imported.importedDocument, imported.importedActiveLayerID) ||
            imported.importedDocument.value().id != result._source.value().id)
            return std::nullopt;
        result._document = std::move(imported.importedDocument);
        result._active = imported.importedActiveLayerID;
        result._history =
            DocumentHistory::PrepareEdit(std::move(result._view), result._source, sourceActive, result._document,
                                         result._active, std::move(imported.historyName), cancellation, observer);
        if (!result._history)
            return std::nullopt;
        if (result._authority._maskSelected && result._authority._active == result._active)
            for (const auto& layer : result._document.value().layers)
                if (layer.id == result._active.value())
                    result._maskSelected = layer.mask.has_value();
    }
    result._selected.insert(result._active.value());
    result._changesDocument = true;
    return cancellation.IsCancelled() ? std::nullopt : std::optional<PreparedImageImport>{std::move(result)};
}

std::optional<PreparedImageImport> DocumentSession::PrepareDocumentReplacementPublication(
    ImageImportAuthority authority, std::optional<CanvasDocument> source, DocumentHistory::StateView state,
    CanvasDocument replacement, std::optional<Guid> active, CancelToken cancellation,
    DocumentHistory::PrepareObserver observer)
{
    if (!authority._valid || cancellation.IsCancelled() ||
        (source ? std::optional(source.value().id) : std::optional<Guid>{}) != authority._documentID ||
        ValidateDocument(replacement) != DocumentStatus::Ok)
        return std::nullopt;
    PreparedImageImport result(std::move(authority), std::move(state));
    result._retiredDrafts = std::make_unique<PreparedImageImport::RetiredDrafts>();
    result._source = std::move(source);
    // A project can have no selected layer. Match ReplaceDocument: an absent/unknown active id selects nothing.
    if (active && !std::any_of(replacement.layers.begin(), replacement.layers.end(),
                               [&](const ImageLayer& layer) { return layer.id == active.value(); }))
        active.reset();
    result._baseline = std::move(replacement);
    if (!BeginReplacementSnapshot(cancellation, observer))
        return std::nullopt;
    result._document = result._baseline;
    result._active = active;
    if (active)
        result._selected.insert(active.value());
    result._replacement = DocumentHistory::PrepareReplacement(std::move(result._view), result._baseline, active,
                                                              result._document, active, {}, cancellation, observer);
    if (!result._replacement || cancellation.IsCancelled())
        return std::nullopt;
    result._changesDocument = true;
    return result;
}

std::optional<PreparedImageImport> DocumentSession::PrepareDocumentLifecyclePublication(
    ImageImportAuthority authority, std::optional<CanvasDocument> source, DocumentHistory::StateView state,
    std::optional<CanvasDocument> replacement, std::optional<Guid> active, std::wstring name, CancelToken cancellation,
    DocumentHistory::PrepareObserver observer)
{
    if (!authority._valid || cancellation.IsCancelled() ||
        (source ? std::optional(source->id) : std::optional<Guid>{}) != authority._documentID)
        return std::nullopt;
    auto result = PrepareDocumentLifecyclePublication(std::move(authority), std::move(state), std::move(replacement),
                                                      active, std::move(name), cancellation, observer);
    if (result)
        result->_source = std::move(source);
    return result;
}

std::optional<PreparedImageImport> DocumentSession::PrepareDocumentLifecyclePublication(
    ImageImportAuthority authority, DocumentHistory::StateView state, std::optional<CanvasDocument> replacement,
    std::optional<Guid> active, std::wstring name, CancelToken cancellation, DocumentHistory::PrepareObserver observer)
{
    if (!authority._valid || cancellation.IsCancelled() ||
        (replacement && ValidateDocument(*replacement) != DocumentStatus::Ok) || (!replacement && active))
        return std::nullopt;
    PreparedImageImport result(std::move(authority), std::move(state));
    if (!result._authority._documentID && !replacement)
        return result; // Close on an already absent document preserves its existing redo/saved lineage.
    if (active && !std::any_of(replacement->layers.begin(), replacement->layers.end(),
                               [&](const ImageLayer& layer) { return layer.id == *active; }))
        return std::nullopt;
    result._retiredDrafts = std::make_unique<PreparedImageImport::RetiredDrafts>();
    if (!BeginReplacementSnapshot(cancellation, observer))
        return std::nullopt;
    result._document = std::move(replacement);
    result._active = active;
    if (active)
        result._selected.insert(*active);
    result._replacement = DocumentHistory::PrepareSavedReplacement(std::move(result._view), result._document, active,
                                                                   std::move(name), cancellation, observer);
    if (!result._replacement || cancellation.IsCancelled())
        return std::nullopt;
    result._changesDocument = true;
    return result;
}

bool DocumentSession::ApplyPreparedImageImportIfCurrent(PreparedImageImport& prepared) noexcept
{
    static_assert(std::is_nothrow_swappable_v<std::optional<CanvasDocument>>);
    static_assert(std::is_nothrow_swappable_v<std::unordered_set<Guid>>);
    static_assert(std::is_nothrow_swappable_v<std::optional<BrushCommitInfo>>);
    static_assert(std::is_nothrow_swappable_v<SnapGuides>);
    static_assert(std::is_nothrow_swappable_v<std::optional<LayerAdjustment>>);
    static_assert(std::is_nothrow_swappable_v<std::optional<TransformEdit>>);
    static_assert(std::is_nothrow_swappable_v<std::optional<LassoDraft>>);
    static_assert(std::is_nothrow_swappable_v<std::optional<DocumentSelection>>);
    static_assert(std::is_nothrow_swappable_v<std::optional<GradientEdit>>);
    static_assert(std::is_nothrow_swappable_v<std::optional<PixelMove>>);
    static_assert(std::is_nothrow_swappable_v<std::optional<ColorEdit>>);
    if (!CanApplyImageImport(prepared._authority) || (prepared._replacement && !prepared._retiredDrafts) ||
        (prepared._replacement ? !history.CanPublishReplacement(prepared._replacement.value())
         : prepared._history   ? !history.CanPublish(prepared._history.value())
                               : !history.IsCurrent(prepared._view)))
        return false;
    if (prepared._replacement)
    {
        if (!history.PublishReplacement(prepared._replacement.value()))
            return false;
    }
    else if (prepared._history && !history.Publish(prepared._history.value()))
        return false;
    if (prepared._document || prepared._replacement)
    {
        std::swap(_document, prepared._document);
        std::swap(_activeLayerID, prepared._active);
        std::swap(_selected, prepared._selected);
        std::swap(_displayCopy, prepared._retiredDisplay);
        _maskSelected = prepared._maskSelected;
        if (prepared._replacement)
        {
            // Close borrowed coverage engines on their owner before transferring CPU destruction. A retired
            // BrushStroke's destructor must never call the application graphics engine on the release worker.
            const auto detach = [](const std::shared_ptr<BrushStroke>& stroke) noexcept
            {
                if (stroke)
                    stroke->ReleaseCoverageEngine();
            };
            detach(_brush);
            if (_gradient)
                detach(_gradient->raster);
            if (_pixelMove)
                detach(_pixelMove->raster);
            auto& drafts = *prepared._retiredDrafts;
            std::swap(_transformEdit, drafts.transform);
            std::swap(_lassoDraft, drafts.lasso);
            std::swap(_selectionMoveOrigin, drafts.selectionMove);
            std::swap(_brush, drafts.brush);
            std::swap(_warp, drafts.warp);
            std::swap(_gradient, drafts.gradient);
            std::swap(_shapeDraft, drafts.shape);
            std::swap(_pixelMove, drafts.pixelMove);
            std::swap(_colorEdit, drafts.color);
            ++_documentSerial;
            std::swap(_collapsed, prepared._retiredCollapsed);
            std::swap(_lastBrushCommit, prepared._retiredBrushCommit);
            std::swap(_adjustmentOriginal, prepared._retiredAdjustment);
            std::swap(_snapGuides, prepared._retiredSnapGuides);
            std::swap(_warpSource.image, prepared._retiredWarpImage);
            _warpSource.layerID = {};
            _warpSource.transform = {};
            _cropRect.reset();
            _transformDuplicate.reset();
            _adjustmentEditingID.reset();
            _lastBrushPoint.reset();
            _pendingOpacityDigit.reset();
            _cloneOffset.reset();
            _budgetRefused = false;
        }
        NoteDocumentMutation();
    }
    prepared._authority._valid = false;
    return true;
}

std::uint64_t DocumentSession::ImageImportRetirementMemoryBytes() const noexcept
{
    auto bytes = HistoryRestoreRetirementMemoryBytes();
    const auto maximum = (std::numeric_limits<std::uint64_t>::max)();
    const auto add = [&](std::uint64_t count, std::uint64_t size) noexcept
    {
        if ((size && count > maximum / size) || count * size > maximum - bytes)
            return false;
        bytes += count * size;
        return true;
    };
    const auto rasterBytes = [&](const Raster& image) noexcept
    {
        const auto amount = image.RetirementMemoryBytes();
        return !image.HasStorage() || (amount && add(1, amount));
    };
    const auto strokeBytes = [&](const std::shared_ptr<BrushStroke>& stroke) noexcept
    {
        if (!stroke)
            return true;
        const auto amount = stroke->RetirementMemoryBytes();
        return amount && add(1, amount);
    };
    // H2 will cache this shared document's complete record/storage bound. Until then metadata traversal is
    // explicit admission work; no optional<CanvasDocument> copy or pixel/materialization occurs on the owner.
    const auto documentBytes = [&](const std::shared_ptr<CanvasDocument>& document) noexcept
    {
        if (!document)
            return true;
        if (!add(1, sizeof(CanvasDocument) + 64) || !add(document->layers.capacity(), sizeof(ImageLayer)) ||
            (document->selection && !add(1, document->selection->loops.StorageBytes())))
            return false;
        for (const auto& layer : document->layers)
        {
            if (!add(layer.name.capacity() + 1, sizeof(wchar_t)) || !rasterBytes(layer.shapeImage))
                return false;
            if (layer.asset && (!add(layer.asset->name.capacity() + 1, sizeof(wchar_t)) ||
                                !rasterBytes(layer.asset->image) || !rasterBytes(layer.asset->thumbnail)))
                return false;
            if (layer.mask && (!add(layer.mask->asset.name.capacity() + 1, sizeof(wchar_t)) ||
                               !rasterBytes(layer.mask->asset.image) || !rasterBytes(layer.mask->asset.thumbnail)))
                return false;
            if (layer.adjustment)
                for (const auto& channel : layer.adjustment->curves.channels)
                    if (!add(channel.capacity(), sizeof(CurvePoint)))
                        return false;
        }
        return true;
    };
    if (!bytes || !add(1, sizeof(PreparedImageImport::RetiredDrafts) + 64) ||
        (_lassoDraft && !add(_lassoDraft->points.capacity(), sizeof(Vec2))) ||
        (_selectionMoveOrigin && !add(1, _selectionMoveOrigin->loops.StorageBytes())) || !strokeBytes(_brush) ||
        (_gradient && !strokeBytes(_gradient->raster)) ||
        (_pixelMove && (!strokeBytes(_pixelMove->raster) || !add(1, _pixelMove->origin.loops.StorageBytes()))))
        return 0;
    if (_transformEdit && ((_transformEdit->group && !add(_transformEdit->group->originals.capacity(),
                                                          sizeof(std::pair<Guid, LayerTransform>))) ||
                           (_transformEdit->floating && !documentBytes(_transformEdit->floating->before))))
        return 0;
    if (_warp)
    {
        const auto amount = _warp->RetirementMemoryBytes();
        if (!amount || !add(1, amount))
            return 0;
    }
    if (_colorEdit)
    {
        const auto amount = ColorEditHeldMemoryBytes(_colorEdit.value());
        if (!amount || !add(1, amount))
            return 0;
    }
    if (_adjustmentOriginal)
    {
        if (!add(1, sizeof(LayerAdjustment)))
            return 0;
        for (const auto& channel : _adjustmentOriginal.value().curves.channels)
            if (!add(channel.capacity(), sizeof(channel.front())))
                return 0;
    }
    return bytes && add(_snapGuides.xs.capacity(), sizeof(double)) && add(_snapGuides.ys.capacity(), sizeof(double)) &&
                   add(_collapsed.bucket_count(), sizeof(void*)) &&
                   add(_collapsed.size(), sizeof(Guid) + 4 * sizeof(void*) + 64) &&
                   (!_lastBrushCommit || rasterBytes(_lastBrushCommit.value().image)) && rasterBytes(_warpSource.image)
               ? bytes
               : 0;
}
