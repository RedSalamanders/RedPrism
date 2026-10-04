#include "LayerMetadataWork.h"

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

std::optional<LayerMetadataInput> DocumentSession::PrepareLayerMetadataInput(LayerMetadataKind kind, Guid target,
                                                                             std::wstring_view name, double opacity,
                                                                             LayerBlendMode blendMode) const
{
    if (!_document || kind > LayerMetadataKind::SetBlendMode || history.CaptureState().Depth() != 0 ||
        (kind == LayerMetadataKind::SetOpacity && !std::isfinite(opacity)) ||
        (kind == LayerMetadataKind::SetBlendMode &&
         (blendMode < LayerBlendMode::Normal || blendMode > LayerBlendMode::Luminosity)) ||
        ((kind == LayerMetadataKind::SetOpacity || kind == LayerMetadataKind::SetBlendMode) &&
         _activeLayerID != target))
        return std::nullopt;
    auto authority = CaptureHistoryRestoreAuthority();
    if (!CanApplyHistoryRestore(authority))
        return std::nullopt;
    auto token = CaptureJobToken();
    if (!token)
        return std::nullopt;
    return LayerMetadataInput(kind, target, kind == LayerMetadataKind::Rename ? std::wstring(name) : std::wstring{},
                              opacity, blendMode, std::move(authority), std::move(token.value()),
                              history.CaptureState(), _document.value(), _activeLayerID);
}

std::optional<PreparedLayerMetadata> DocumentSession::PrepareLayerMetadataPublication(
    LayerMetadataInput input, CancelToken cancellation, DocumentHistory::PrepareObserver historyObserver,
    LayerMetadataPrepareObserver observer)
{
    if (cancellation.IsCancelled() || !input._valid || !input._document || input._token.target ||
        input._history.Depth() != 0 || input._kind > LayerMetadataKind::SetBlendMode ||
        input._document.value().width != input._token.canvasWidth ||
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
        if (opacity == layer.opacity)
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
    CanvasDocument after;
    after.id = before.id;
    after.width = before.width;
    after.height = before.height;
    after.resolution = before.resolution;
    after.selection = before.selection;
    after.layers.reserve(before.layers.size());
    for (const auto& sourceLayer : before.layers)
    {
        if (cancellation.IsCancelled())
            return std::nullopt;
        after.layers.push_back(sourceLayer);
    }
    if (cancellation.IsCancelled())
        return std::nullopt;
    {
        auto& writable = after.layers[index.value()];
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
    result._document = std::move(after);
    if (!result._document || !Checkpoint(observer, LayerMetadataPrepareStage::AfterSeal, cancellation) ||
        ValidateDocument(result._document.value()) != DocumentStatus::Ok || cancellation.IsCancelled())
        return std::nullopt;
    result._history =
        DocumentHistory::PrepareEdit(captured._history, captured._document, captured._active, result._document,
                                     captured._active, std::move(historyName), cancellation, historyObserver);
    if (!result._history || cancellation.IsCancelled())
        return std::nullopt;
    return result;
}

bool DocumentSession::IsPreparedLayerMetadataCurrent(const PreparedLayerMetadata& prepared) const noexcept
{
    return !prepared._applied && prepared._input._valid && CanApplyHistoryRestore(prepared._input._authority) &&
           CanApplyJobResult(prepared._input._token, 0) &&
           (prepared._history ? history.CanPublish(prepared._history.value())
                              : history.IsCurrent(prepared._input._history));
}

bool DocumentSession::ApplyPreparedLayerMetadataIfCurrent(PreparedLayerMetadata& prepared) noexcept
{
    static_assert(std::is_nothrow_swappable_v<std::optional<CanvasDocument>>);
    if (!IsPreparedLayerMetadataCurrent(prepared))
        return false;
    if (prepared._history)
    {
        if (!prepared._document || !history.Publish(prepared._history.value()))
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
    if (!_document || kind > LayerMetadataKind::SetBlendMode || history.CaptureState().Depth() != 0 ||
        !CanApplyHistoryRestore(CaptureHistoryRestoreAuthority()))
        return 0;
    auto bytes = history.EditMemoryBytes(_document);
    const auto maximum = (std::numeric_limits<std::uint64_t>::max)();
    // Existing edit accounting covers record/selection children, stack growth and trim. Reserve the captured
    // request, trimmed replacement and all before/after name overlap as well as root/control/block overhead.
    constexpr std::uint64_t extra = 8192 + sizeof(LayerMetadataInput) + sizeof(PreparedLayerMetadata);
    constexpr std::uint64_t nameCopies = 8 * sizeof(wchar_t);
    if (!bytes || bytes > maximum - extra || nameUnits > (maximum - bytes - extra) / nameCopies)
        return 0;
    bytes += extra + static_cast<std::uint64_t>(nameUnits) * nameCopies;
    return bytes;
}
