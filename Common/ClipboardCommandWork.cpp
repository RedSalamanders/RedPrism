#include "ClipboardCommandWork.h"

#include "DocumentDraft.h"

#include <cmath>
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
std::uint64_t TargetMetadataBytes(const ImageLayer& layer) noexcept
{
    std::uint64_t bytes = 384;
    const auto maximum = (std::numeric_limits<std::uint64_t>::max)();
    if (layer.mask)
    {
        const auto count = static_cast<std::uint64_t>(layer.mask->asset.name.size()) + 1;
        if (count > (maximum - 64) / (2 * sizeof(wchar_t)) || !Add(bytes, count * 2 * sizeof(wchar_t) + 64))
            return 0;
    }
    if (layer.adjustment)
        for (const auto& channel : layer.adjustment->curves.channels)
        {
            const auto count = static_cast<std::uint64_t>(channel.size());
            if (count > (maximum - 64) / (2 * sizeof(CurvePoint)) || !Add(bytes, count * 2 * sizeof(CurvePoint) + 64))
                return 0;
        }
    return bytes;
}
bool HeldRaster(std::uint64_t& bytes, const Raster& raster) noexcept
{
    return !raster.HasStorage() || Add(bytes, raster.RetirementMemoryBytes());
}
} // namespace

bool DocumentSession::CanPrepareClipboardCommand(ClipboardCommandKind kind) const noexcept
{
    if (kind > ClipboardCommandKind::Cut || !_document || HasColorEdit() || _gradient || _transformEdit)
        return false;
    if (kind == ClipboardCommandKind::CopyMerged)
        return CanCopyMerged();
    if (!CanCopyPixels())
        return false;
    // Production Cut closes lasso, outline move and lifted-pixel interaction before capture. Copy does not.
    return kind != ClipboardCommandKind::Cut ||
           (_document->selection && CanPaint() && !_pixelMove && !_lassoDraft && !_selectionMoveOrigin);
}

std::optional<CopyPixelsInput> DocumentSession::ClipboardPixelsInput(ClipboardCommandKind kind,
                                                                     const Raster& composite) const
{
    if (!CanPrepareClipboardCommand(kind))
        return std::nullopt;
    CopyPixelsInput copy;
    copy.canvasWidth = _document->width;
    copy.canvasHeight = _document->height;
    copy.selection = _document->selection;
    if (kind == ClipboardCommandKind::CopyMerged)
    {
        if (!composite.HasStorage() || composite.Format() != RasterFormat::Bgra8888 ||
            composite.Width() != copy.canvasWidth || composite.Height() != copy.canvasHeight)
            return std::nullopt;
        copy.source = composite;
        copy.composite = true;
    }
    else
    {
        const auto& layer = *ActiveLayer();
        copy.mask = _maskSelected;
        copy.source = copy.mask ? layer.mask->asset.image : layer.asset->image;
        copy.placement = copy.mask ? MaskTransform(layer) : DisplayedTransform(layer);
        if (copy.mask && layer.mask->placement)
            copy.background =
                static_cast<std::uint8_t>(std::lround(MaskEdgeBackground(layer.mask->asset.thumbnail) * 255.0));
    }
    return copy;
}

std::optional<ClipboardCommandInput> DocumentSession::PrepareClipboardCommandInput(
    ClipboardCommandKind kind, const Raster& composite, const std::optional<JobToken>& compositeAuthority) const
{
    if (kind == ClipboardCommandKind::CopyMerged &&
        (!compositeAuthority || compositeAuthority->target ||
         !CanApplyJobResult(compositeAuthority.value(), compositeAuthority->sequence)))
        return std::nullopt;
    auto copy = ClipboardPixelsInput(kind, composite);
    auto token = kind == ClipboardCommandKind::CopyMerged ? compositeAuthority : CaptureJobToken(_activeLayerID);
    if (!copy || !token)
        return std::nullopt;
    ClipboardCommandInput input(kind, std::move(token.value()), std::move(copy.value()), _activeLayerID, _cropRect,
                                _clipboardSerial, _maskSelected);
    if (kind == ClipboardCommandKind::Cut)
    {
        const auto& layer = *ActiveLayer();
        RasterEditInput clear;
        clear.kind = RasterEditKind::Clear;
        clear.layer = layer;
        clear.selection = _document->selection;
        clear.canvasWidth = _document->width;
        clear.canvasHeight = _document->height;
        clear.pixelLimit = RasterEditPixelLimit(layer, _maskSelected);
        clear.mask = _maskSelected;
        const double gray = maskPaintWhite ? 0.0 : 1.0; // targeted mask's background palette
        clear.color = {gray, gray, gray, 1.0};
        input._clear = std::move(clear);
        input._document = _document;
        input._history = history.CaptureState();
    }
    return input;
}

std::optional<PreparedClipboardCommand> DocumentSession::PrepareClipboardCommandPublication(
    ClipboardCommandInput input, CancelToken cancellation, DocumentHistory::PrepareObserver historyObserver)
{
    if (!input._valid || cancellation.IsCancelled())
        return std::nullopt;
    auto copy = RunCopyPixels(input._copy, cancellation);
    if (copy.status != CopyPixelsStatus::Ready || !copy.clipboard || cancellation.IsCancelled())
        return std::nullopt;
    PreparedClipboardCommand result(std::move(input));
    result._clipboard = std::move(copy.clipboard);
    result._copied = result._clipboard;
    if (result._input._kind != ClipboardCommandKind::Cut)
        return result;
    if (!result._input._clear || !result._input._document || !result._input._active)
        return std::nullopt;
    auto cleared = RunRasterEdit(result._input._clear.value(), cancellation);
    // Clear that did not paint must not replace the previous private clipboard.
    if (cleared.status != RasterEditStatus::Ready || !cleared.layer || cancellation.IsCancelled())
        return std::nullopt;
    const auto& before = result._input._document.value();
    auto draft = CanvasDocumentDraft::Clone(before, before.layers.size(), cancellation);
    if (!draft)
        return std::nullopt;
    bool replaced = false;
    {
        auto layers = draft.value().EditLayers();
        for (auto& layer : layers)
        {
            if (cancellation.IsCancelled())
                return std::nullopt;
            if (layer.id == result._input._active.value())
            {
                layer = std::move(cleared.layer.value());
                replaced = true;
            }
        }
    }
    if (!replaced || cancellation.IsCancelled())
        return std::nullopt;
    auto sealed = draft.value().Seal(cancellation);
    if (!sealed || cancellation.IsCancelled() || ValidateDocument(sealed.value()) != DocumentStatus::Ok)
        return std::nullopt;
    result._document = std::move(sealed.value());
    result._history = DocumentHistory::PrepareEdit(
        result._input._history, result._input._document, result._input._active, result._document, result._input._active,
        result._input._mask ? L"Fill Mask" : L"Clear", cancellation, historyObserver);
    return result._history && !cancellation.IsCancelled() ? std::optional<PreparedClipboardCommand>(std::move(result))
                                                          : std::nullopt;
}

bool DocumentSession::IsPreparedClipboardCommandCurrent(const PreparedClipboardCommand& prepared) const noexcept
{
    const auto& input = prepared._input;
    return !prepared._applied && input._valid && prepared._clipboard && CanPrepareClipboardCommand(input._kind) &&
           _clipboardSerial == input._clipboardSerial && _generation == input._token.generation &&
           CanApplyJobResult(input._token, input._token.sequence) && _activeLayerID == input._active &&
           _cropRect == input._crop && _maskSelected == input._mask &&
           (input._kind != ClipboardCommandKind::Cut ||
            (prepared._document && prepared._history && history.CanPublish(prepared._history.value())));
}

bool DocumentSession::ApplyPreparedClipboardCommandIfCurrent(PreparedClipboardCommand& prepared) noexcept
{
    static_assert(std::is_nothrow_swappable_v<std::optional<CanvasDocument>>);
    static_assert(std::is_nothrow_swappable_v<std::optional<PixelClipboard>>);
    if (!IsPreparedClipboardCommandCurrent(prepared))
        return false;
    if (prepared._input._kind == ClipboardCommandKind::Cut)
    {
        if (!history.Publish(prepared._history.value()))
            return false;
        std::swap(_document, prepared._document);
        std::swap(_displayCopy, prepared._retiredDisplay);
        std::swap(_lastBrushCommit, prepared._retiredBrushCommit);
        NoteDocumentMutation();
    }
    // No allocating, throwing or failing operation remains between the history/doc and private copy swaps.
    std::swap(_clipboard, prepared._clipboard);
    ++_clipboardSerial;
    prepared._applied = true;
    return true;
}

std::uint64_t DocumentSession::ClipboardCommandMemoryBytes(ClipboardCommandKind kind, const Raster& composite) const
{
    const auto copy = ClipboardPixelsInput(kind, composite);
    if (!copy)
        return 0;
    std::uint64_t bytes = 4096 + sizeof(ClipboardCommandInput) + sizeof(PreparedClipboardCommand);
    if (!Add(bytes, CopyPixelsMemoryBytes(copy.value())))
        return 0;
    if (kind != ClipboardCommandKind::CopyMerged && !Add(bytes, TargetMetadataBytes(*ActiveLayer())))
        return 0;
    if (kind == ClipboardCommandKind::Cut)
    {
        const auto& layer = *ActiveLayer();
        if (!Add(bytes, history.EditMemoryBytes(_document)) ||
            !Add(bytes, RasterEditMemoryBytes(layer, _document->selection, _document->width, _document->height,
                                              RasterEditPixelLimit(layer, _maskSelected), _maskSelected)))
            return 0;
    }
    return bytes;
}

std::uint64_t DocumentSession::ClipboardCommandRetirementMemoryBytes(ClipboardCommandKind kind,
                                                                     const Raster& composite) const
{
    const auto copy = ClipboardPixelsInput(kind, composite);
    if (!copy)
        return 0;
    std::uint64_t bytes = 4096;
    // Capture may become the final owner after a stale replacement; include its complete immutable graph.
    if (!Add(bytes, copy->source.RetirementMemoryBytes()))
        return 0;
    if (copy->selection && !Add(bytes, copy->selection->loops.StorageBytes()))
        return 0;
    if (kind != ClipboardCommandKind::CopyMerged)
    {
        const auto& target = *ActiveLayer();
        if (!Add(bytes, TargetMetadataBytes(target)))
            return 0;
        if (target.asset && !HeldRaster(bytes, target.asset->image))
            return 0;
        if (target.mask &&
            (!HeldRaster(bytes, target.mask->asset.image) || !HeldRaster(bytes, target.mask->asset.thumbnail)))
            return 0;
    }
    if (_clipboard && !Add(bytes, _clipboard->image.RetirementMemoryBytes()))
        return 0;
    if (kind == ClipboardCommandKind::Cut)
    {
        if (!Add(bytes, HistoryRestoreRetirementMemoryBytes()))
            return 0;
        if (_lastBrushCommit && !Add(bytes, _lastBrushCommit->image.RetirementMemoryBytes()))
            return 0;
    }
    return bytes;
}
