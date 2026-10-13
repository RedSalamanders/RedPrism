#include "LayerMetadataWork.h"

#include "DocumentDraft.h"
#include "SharedPublicationMemory.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <type_traits>
#include <utility>

namespace
{
[[nodiscard]] bool Checkpoint(LayerMetadataPrepareObserver observer, LayerMetadataPrepareStage stage,
                              CancelToken cancellation)
{
    if (observer.checkpoint)
        observer.checkpoint(observer.context, stage);
    return !cancellation.IsCancelled();
}
} // namespace

LayerMetadataAuthority DocumentSession::CaptureLayerMetadataAuthority(bool preserve) const noexcept
{
    LayerMetadataAuthority result;
    result._settled = CaptureHistoryRestoreAuthority();
    result._preserved = preserve;
    if (preserve)
    {
        result._settled._valid =
            _document && !HasColorEdit() && !_gradient && !_transformEdit && !_pixelMove && !_selectionMoveOrigin;
        result._brushOpening = _brush ? std::optional(_brushOpening) : std::nullopt;
        result._warpOpening = PendingWarpOpening();
        result._shapeOpening = _shapeDraft ? std::optional(_shapeOpening) : std::nullopt;
        result._lassoOpening = _lassoDraft ? std::optional(_lassoOpening) : std::nullopt;
    }
    return result;
}

bool DocumentSession::CanApplyLayerMetadataAuthority(const LayerMetadataAuthority& authority) const noexcept
{
    if (!authority._preserved)
        return CanApplyHistoryRestore(authority._settled);
    const auto& captured = authority._settled;
    return captured._valid && _document && captured._session == _tokenIdentity.value &&
           captured._documentSerial == _documentSerial && captured._generation == _generation &&
           captured._restoreSerial == _restoreSerial && captured._active == _activeLayerID &&
           captured._maskSelected == _maskSelected && captured._crop == _cropRect && !HasColorEdit() && !_gradient &&
           !_transformEdit && !_pixelMove && !_selectionMoveOrigin &&
           authority._brushOpening == (_brush ? std::optional(_brushOpening) : std::nullopt) && !_warpUpdatingOpening &&
           !_warpFinishingOpening && authority._warpOpening == PendingWarpOpening() &&
           authority._shapeOpening == (_shapeDraft ? std::optional(_shapeOpening) : std::nullopt) &&
           authority._lassoOpening == (_lassoDraft ? std::optional(_lassoOpening) : std::nullopt);
}

std::optional<LayerMetadataInput> DocumentSession::PrepareLayerMetadataInput(LayerMetadataKind kind, Guid target,
                                                                             std::wstring_view name, double opacity,
                                                                             LayerBlendMode blendMode) const
{
    return PrepareLayerMetadataInputImpl(kind, target, name, opacity, blendMode, false);
}

std::optional<LayerMetadataInput> DocumentSession::PreparePreservedLayerMetadataInput(
    LayerMetadataKind kind, Guid target, std::wstring_view name, double opacity, LayerBlendMode blendMode) const
{
    return PrepareLayerMetadataInputImpl(kind, target, name, opacity, blendMode, true);
}

std::optional<LayerMetadataInput> DocumentSession::PrepareLayerMetadataInputImpl(LayerMetadataKind kind, Guid target,
                                                                                 std::wstring_view name, double opacity,
                                                                                 LayerBlendMode blendMode,
                                                                                 bool preserve) const
{
    if (!_document || kind > LayerMetadataKind::SetBlendMode || (!preserve && history.CaptureState().Depth() != 0) ||
        (kind == LayerMetadataKind::SetOpacity && !std::isfinite(opacity)) ||
        (kind == LayerMetadataKind::SetBlendMode &&
         (blendMode < LayerBlendMode::Normal || blendMode > LayerBlendMode::Luminosity)) ||
        ((kind == LayerMetadataKind::SetOpacity || kind == LayerMetadataKind::SetBlendMode) &&
         _activeLayerID != target))
        return std::nullopt;
    auto authority = CaptureLayerMetadataAuthority(preserve);
    if (!CanApplyLayerMetadataAuthority(authority))
        return std::nullopt;
    auto token = CaptureJobToken();
    if (!token)
        return std::nullopt;
    return LayerMetadataInput(kind, target, kind == LayerMetadataKind::Rename ? std::wstring(name) : std::wstring{},
                              opacity, blendMode, std::move(authority), std::move(token.value()),
                              history.CaptureState(), _document.value(), _activeLayerID);
}

std::optional<LayerMetadataInput> DocumentSession::PrepareLayerOpacityPreviewInput(Guid target, double value) const
{
    auto input = PreparePreservedLayerMetadataInput(LayerMetadataKind::SetOpacity, target, {}, value);
    if (input)
        input.value()._opacityPreview = true;
    return input;
}

std::optional<LayerMetadataInput> DocumentSession::PrepareTypedLayerOpacityInput(Guid target, double opening,
                                                                                 double value) const
{
    if (!std::isfinite(opening) || opening < 0.0 || opening > 1.0)
        return std::nullopt;
    auto input = PreparePreservedLayerMetadataInput(LayerMetadataKind::SetOpacity, target, {}, value);
    if (input)
        input.value()._opacityOpening = opening;
    return input;
}

std::optional<PreparedLayerMetadata> DocumentSession::PrepareLayerMetadataPublication(
    LayerMetadataInput input, CancelToken cancellation, DocumentHistory::PrepareObserver historyObserver,
    LayerMetadataPrepareObserver observer, std::shared_ptr<const SharedPublicationMemory> memory)
{
    if (cancellation.IsCancelled() || !input._valid || !input._document || input._token.target ||
        (!input._authority._preserved && input._history.Depth() != 0) ||
        input._kind > LayerMetadataKind::SetBlendMode || input._document.value().width != input._token.canvasWidth ||
        input._document.value().height != input._token.canvasHeight)
        return std::nullopt;
    PreparedLayerMetadata result(std::move(input));
    const auto& captured = result._input;
    const auto& before = captured._document.value();
    std::optional<std::size_t> index;
    for (std::size_t i = 0; i < before.layers.size(); ++i)
    {
        if (cancellation.IsCancelled())
            return std::nullopt;
        if (before.layers[i].id == captured._target)
        {
            index = i;
            break;
        }
    }
    const auto unchanged = [&]() -> std::optional<PreparedLayerMetadata>
    { return cancellation.IsCancelled() ? std::nullopt : std::optional<PreparedLayerMetadata>(std::move(result)); };
    // A rename card can outlive its original layer. Missing targets acknowledge without retargeting or history.
    if (!index)
        return unchanged();
    const auto& layer = before.layers[index.value()];
    std::wstring name;
    std::wstring historyName;
    double opacity = layer.opacity;
    bool recordHistory = captured._history.Depth() == 0;
    switch (captured._kind)
    {
    case LayerMetadataKind::Rename:
        name = TrimWide(captured._name);
        if (name.empty() || name == layer.name)
            return unchanged();
        // Bound the UTF-16-to-UTF-8 count before its int conversion; whole-document validation follows sealing.
        if (name.size() > static_cast<std::size_t>(kMaxNameUtf8Bytes) || !NameUtf8LengthOk(name))
            return std::nullopt;
        historyName = L"Rename Layer";
        break;
    case LayerMetadataKind::ToggleVisibility:
        historyName = layer.visible ? L"Hide Layer" : L"Show Layer";
        break;
    case LayerMetadataKind::SetOpacity:
        if (layer.isGroup)
            return unchanged();
        opacity = std::clamp(captured._opacity, 0.0, 1.0);
        recordHistory =
            recordHistory && !captured._opacityPreview && opacity != captured._opacityOpening.value_or(layer.opacity);
        if (!recordHistory && opacity == layer.opacity)
            return unchanged();
        historyName = L"Layer Opacity";
        break;
    case LayerMetadataKind::SetBlendMode:
        if (layer.isGroup || layer.blendMode == captured._blendMode)
            return unchanged();
        historyName = L"Layer Blend Mode";
        break;
    }
    if (!Checkpoint(observer, LayerMetadataPrepareStage::BeforeDraft, cancellation))
        return std::nullopt;
    auto draft = CanvasDocumentDraft::Clone(before, before.layers.size(), cancellation);
    if (!draft)
        return std::nullopt;
    {
        auto& writable = draft.value().EditLayers()[index.value()];
        switch (captured._kind)
        {
        case LayerMetadataKind::Rename:
            writable.name = std::move(name);
            break;
        case LayerMetadataKind::ToggleVisibility:
            writable.visible = !layer.visible;
            break;
        case LayerMetadataKind::SetOpacity:
            writable.opacity = opacity;
            break;
        case LayerMetadataKind::SetBlendMode:
            writable.blendMode = captured._blendMode;
            break;
        }
    }
    if (!Checkpoint(observer, LayerMetadataPrepareStage::AfterEdit, cancellation))
        return std::nullopt;
    result._document = draft.value().Seal(cancellation, memory);
    if (!result._document || !Checkpoint(observer, LayerMetadataPrepareStage::AfterSeal, cancellation) ||
        ValidateDocument(result._document.value()) != DocumentStatus::Ok || cancellation.IsCancelled())
        return std::nullopt;
    if (recordHistory)
    {
        if (captured._opacityOpening && captured._opacityOpening.value() != layer.opacity)
        {
            if (!Checkpoint(observer, LayerMetadataPrepareStage::BeforeOpacityOpening, cancellation))
                return std::nullopt;
            auto opening = CanvasDocumentDraft::Clone(before, before.layers.size(), cancellation);
            if (!opening)
                return std::nullopt;
            {
                opening.value().EditLayers()[index.value()].opacity = captured._opacityOpening.value();
            }
            result._opacityHistoryOpening = opening.value().Seal(cancellation, memory);
            if (!result._opacityHistoryOpening ||
                !Checkpoint(observer, LayerMetadataPrepareStage::AfterOpacityOpening, cancellation) ||
                ValidateDocument(result._opacityHistoryOpening.value()) != DocumentStatus::Ok ||
                cancellation.IsCancelled())
                return std::nullopt;
        }
        const auto& historyOpening = result._opacityHistoryOpening ? result._opacityHistoryOpening : captured._document;
        result._history = DocumentHistory::PrepareEdit(captured._history, historyOpening, captured._active,
                                                       result._document, captured._active, std::move(historyName),
                                                       cancellation, historyObserver, memory);
        if (!result._history || cancellation.IsCancelled())
            return std::nullopt;
    }
    return result;
}

DocumentHistory::ResidencyValidation PreparedLayerMetadata::TryAcquireHistoryResidency(
    DocumentHistory::ResidencyPermit& permit) noexcept
{
    if (_applied || !_input._valid)
        return DocumentHistory::ResidencyValidation::Invalid;
    return _history ? permit.TryAcquire(_history.value()) : DocumentHistory::ResidencyValidation::Ready;
}

bool PreparedLayerMetadata::RefreshHistoryResidency(CancelToken cancellation, DocumentHistory::PrepareObserver observer)
{
    if (_applied || !_input._valid || cancellation.IsCancelled())
        return false;
    return !_history || (_document && DocumentHistory::RefreshPreparedResidency(_history.value(), _document,
                                                                                cancellation, observer));
}

bool DocumentSession::IsPreparedLayerMetadataCurrent(const PreparedLayerMetadata& prepared) const noexcept
{
    return !prepared._applied && prepared._input._valid && CanApplyLayerMetadataAuthority(prepared._input._authority) &&
           CanApplyJobResult(prepared._input._token, 0) &&
           (prepared._history ? history.CanPublish(prepared._history.value())
                              : history.IsCurrent(prepared._input._history));
}

bool DocumentSession::ApplyPreparedLayerMetadataIfCurrent(PreparedLayerMetadata& prepared) noexcept
{
    static_assert(std::is_nothrow_swappable_v<std::optional<CanvasDocument>>);
    if (!IsPreparedLayerMetadataCurrent(prepared))
        return false;
    if (prepared._document)
    {
        if (prepared._history && !history.Publish(prepared._history.value()))
            return false;
        std::swap(_document, prepared._document);
        std::swap(_displayCopy, prepared._retiredDisplay);
        NoteDocumentMutation();
    }
    prepared._applied = true;
    return true;
}

std::uint64_t DocumentSession::LayerMetadataMemoryBytes(LayerMetadataKind kind, std::size_t nameUnits) const noexcept
{
    return LayerMetadataMemoryBytesImpl(kind, nameUnits, false);
}

std::uint64_t DocumentSession::PreservedLayerMetadataMemoryBytes(LayerMetadataKind kind,
                                                                 std::size_t nameUnits) const noexcept
{
    return LayerMetadataMemoryBytesImpl(kind, nameUnits, true);
}

std::uint64_t DocumentSession::LayerMetadataMemoryBytesImpl(LayerMetadataKind kind, std::size_t nameUnits,
                                                            bool preserve) const noexcept
{
    if (!_document || kind > LayerMetadataKind::SetBlendMode || (!preserve && history.CaptureState().Depth() != 0) ||
        !CanApplyLayerMetadataAuthority(CaptureLayerMetadataAuthority(preserve)))
        return 0;
    auto bytes = history.EditMemoryBytes(_document);
    const auto maximum = (std::numeric_limits<std::uint64_t>::max)();
    // Metadata changes retain identical raster/selection owners. A resident refresh may overlap its sealed
    // original index/stack with one additional private clone, roots and dedup scratch. Reserve a second complete
    // construction allowance before capture; neither the refresh nor its old owners release this reservation.
    // The new rename/root overlap remains covered by the explicit name allowance below.
    if (!bytes || bytes > maximum / 2)
        return 0;
    bytes *= 2;
    // Existing eight-copy edit accounting covers capture, output, a typed opening and both history snapshots,
    // record/selection children, stack growth and trim. Reserve the captured
    // request, trimmed replacement and all before/after name overlap as well as root/control/block overhead.
    const std::uint64_t extra = 16384 + 2 * SharedPublicationMemory::OwnershipAllowance() + sizeof(LayerMetadataInput) +
                                sizeof(PreparedLayerMetadata);
    constexpr std::uint64_t nameCopies = 8 * sizeof(wchar_t);
    if (!bytes || bytes > maximum - extra || nameUnits > (maximum - bytes - extra) / nameCopies)
        return 0;
    bytes += extra + static_cast<std::uint64_t>(nameUnits) * nameCopies;
    return bytes;
}
