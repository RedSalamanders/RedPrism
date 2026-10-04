#include "ColorDisplayPublication.h"

#include "Adjust.h"
#include "ColorEditHistory.h"

#include <limits>
#include <memory>
#include <type_traits>
#include <utility>

namespace
{
bool Add(std::uint64_t& total, std::uint64_t count, std::uint64_t size = 1) noexcept
{
    constexpr auto maximum = (std::numeric_limits<std::uint64_t>::max)();
    if ((size && count > maximum / size) || count * size > maximum - total)
        return false;
    total += count * size;
    return true;
}

std::optional<std::uint64_t> SnapshotMetadata(const ColorEdit& edit) noexcept
{
    std::uint64_t bytes = sizeof(ColorDisplayPreviewInput) + sizeof(PreparedColorDisplayPreview) + 65536;
    // Input/result edits and their separate tokens, adjustment/table construction and temporary overlap.
    // Copy sizes, rather than resident vector capacities, determine new allocation; retirement uses capacities.
    for (const auto& channel : edit.settings.curves.channels)
        if (!Add(bytes, channel.size(), 6 * sizeof(CurvePoint)))
            return std::nullopt;
    if (edit.sourceToken && edit.sourceToken->target)
    {
        const auto& target = edit.sourceToken->target.value();
        if (target.mask && !Add(bytes, target.mask->asset.name.size() + 1, 6 * sizeof(wchar_t)))
            return std::nullopt;
        if (target.adjustment)
            for (const auto& channel : target.adjustment->curves.channels)
                if (!Add(bytes, channel.size(), 6 * sizeof(CurvePoint)))
                    return std::nullopt;
    }
    return bytes;
}

std::optional<std::uint64_t> HeldDocument(const CanvasDocument& document) noexcept
{
    const auto records = document.layers.OwnershipBounds();
    if (!records)
        return std::nullopt;
    auto bytes = records->ownershipBytes;
    if (!Add(bytes, sizeof(CanvasDocument) + 256) ||
        (document.selection &&
         (!Add(bytes, document.selection->loops.StorageBytes()) || !Add(bytes, document.selection->loops.size(), 128))))
        return std::nullopt;
    return bytes;
}
} // namespace

bool DocumentSession::ColorLookupPreviewReady() const noexcept
{
    if (!_colorEdit || !_colorEdit->preview || !_colorEdit->previewLookup)
        return false;
    if (!HoldColorDisplayOwners())
        return true; // Existing inline and live adjustment-layer routes retain their visibility rules.
    return _colorEdit->previewSequence && !ColorEditChangesNothing(_colorEdit->kind, _colorEdit->settings) &&
           Adjust::BlurMargin(_colorEdit->kind, _colorEdit->settings) <= _colorEdit->grownMargin && _displayCopy &&
           _displayColorOpening == _colorEdit->openingSequence && _displaySourceGeneration == Generation() &&
           _displayColorSequence == _colorEdit->previewSequence;
}

std::optional<ColorDisplayPreviewMemory> DocumentSession::ColorDisplayPreviewMemoryBytes(
    bool lookupPreview) const noexcept
{
    if (!_colorDisplayJobsEnabled || !_document || !_colorEdit || !_colorEdit->deferred || _adjustmentEditingID ||
        _transformEdit || _gradient || _pixelMove || _brush || _warp || _shapeDraft || _selectionMoveOrigin ||
        _lassoDraft)
        return std::nullopt;
    const auto& edit = _colorEdit.value();
    const auto display = ColorDisplayMemoryBytes(_document.value(), {edit.layerID, edit.original, edit.transform});
    const auto render = ::ColorEditPreviewMemoryBytes(edit, lookupPreview);
    const auto metadata = SnapshotMetadata(edit);
    const auto heldPanel = ColorEditHeldMemoryBytes(edit);
    if (!display || !render || !metadata || !heldPanel)
        return std::nullopt;
    ColorDisplayPreviewMemory result{display->additionalBytes, display->retirementBytes};
    if (!Add(result.additionalBytes, render.value()) || !Add(result.additionalBytes, metadata.value()) ||
        !Add(result.retirementBytes, heldPanel, 4) || !Add(result.retirementBytes, result.additionalBytes, 2))
        return std::nullopt;
    if (_displayCopy)
    {
        const auto heldDisplay = HeldDocument(_displayCopy.value());
        if (!heldDisplay || !Add(result.retirementBytes, heldDisplay.value()))
            return std::nullopt;
    }
    return result;
}

std::optional<ColorDisplayPreviewInput> DocumentSession::PrepareColorDisplayPreviewInput(bool lookupPreview) const
{
    std::optional<ColorDisplayPreviewInput> result;
    PrepareColorDisplayPreviewInputInto(result, lookupPreview);
    return result;
}

void DocumentSession::PrepareColorDisplayPreviewInputInto(std::optional<ColorDisplayPreviewInput>& destination,
                                                          bool lookupPreview) const
{
    if (destination)
        return; // Host capture requires an empty admitted slot; preserve existing owners on refusal.
    if (!_colorDisplayJobsEnabled || !_document || !_colorEdit || !_colorEdit->deferred || _adjustmentEditingID)
        return;
    auto job = PrepareColorEditPreview();
    if (!job)
        return;
    job->lookupPreview = lookupPreview;
    // Construct directly in the caller's admitted destination, without a combined input stack temporary.
    destination.emplace(ColorDisplayPreviewInput::ConstructionKey{}, CaptureImageImportAuthority(),
                        history.CaptureAuthority(), _document.value(), std::move(job.value()));
}

ColorDisplayPreviewResult DocumentSession::PrepareColorDisplayPreviewPublication(ColorDisplayPreviewInput input,
                                                                                 Executor& executor,
                                                                                 CancelToken cancellation,
                                                                                 ColorDisplayObserver observer)
{
    // Reference API: host workers use Into on their already allocated payload. Keep this large aggregate off
    // the reference wrapper's stack too; its allocation failure propagates to the existing worker boundary.
    auto output = std::make_unique<ColorDisplayPreviewResult>();
    PrepareColorDisplayPreviewPublicationInto(std::move(input), *output, executor, cancellation, observer);
    return std::move(*output);
}

void DocumentSession::PrepareColorDisplayPreviewPublicationInto(ColorDisplayPreviewInput&& input,
                                                                ColorDisplayPreviewResult& output, Executor& executor,
                                                                CancelToken cancellation, ColorDisplayObserver observer)
{
    output.publication.reset(); // Worker-only destination reuse; it never releases owners on the UI thread.
    output.status = ColorEditJobStatus::Failed;
    cancellation = cancellation.WithShutdown(executor.ShutdownToken());
    const bool valid = std::exchange(input._valid, false);
    if (!valid || cancellation.IsCancelled())
    {
        if (cancellation.IsCancelled())
            output.status = ColorEditJobStatus::Cancelled;
        return;
    }
    // Equality of independently constructed outlines can traverse millions of points. Establish opening
    // selection equivalence on the worker, then bind the result to this complete source's retained root.
    // The owner first checks full source authority, so its existing token validation takes the identity path.
    if (input._job.token.selection != input._source.selection)
        return;
    input._job.token.selection = input._source.selection;
    auto result = RunColorEditPreviewJob(input._job, executor, cancellation);
    std::optional<CanvasDocument> display;
    if (result.status == ColorEditJobStatus::Ready)
    {
        const ColorEdit& rendered = result.edit;
        const Raster& pixels = rendered.previewLookup ? rendered.previewSource : rendered.previewRaster;
        auto prepared =
            PrepareColorDisplay(input._source, ColorDisplayLayer{rendered.layerID, pixels, rendered.previewTransform},
                                cancellation, observer);
        if (prepared.status == ColorDisplayStatus::Ready)
            display = std::move(prepared.document);
        else
            result.status = prepared.status == ColorDisplayStatus::Cancelled ? ColorEditJobStatus::Cancelled
                                                                             : ColorEditJobStatus::Failed;
    }
    if (cancellation.IsCancelled())
    {
        output.status = ColorEditJobStatus::Cancelled;
        return;
    }
    output.status = result.status;
    // All members move without throwing. Restore validity only for this immediately transferred publication.
    input._valid = true;
    output.publication.emplace(PreparedColorDisplayPreview::ConstructionKey{}, std::move(input), std::move(result),
                               std::move(display));
}

bool DocumentSession::ApplyPreparedColorDisplayPreviewIfCurrent(PreparedColorDisplayPreview& prepared) noexcept
{
    static_assert(std::is_nothrow_swappable_v<std::optional<CanvasDocument>>);
    static_assert(std::is_nothrow_swappable_v<Raster>);
    if (prepared._consumed || !prepared._input._valid || !_colorDisplayJobsEnabled || !_colorEdit ||
        !_colorEdit->deferred || _adjustmentEditingID || _transformEdit || _gradient || _pixelMove || _brush || _warp ||
        _shapeDraft || _selectionMoveOrigin || _lassoDraft || !CanApplyImageImport(prepared._input._authority) ||
        !history.IsCurrent(prepared._input._history) || prepared._result.edit.previewLookupOnly)
        return false;
    const auto status = prepared._result.status;
    const auto& selection = prepared._input._source.selection;
    const auto& capturedSelection = prepared._result.token.selection;
    // Successful worker preparation binds equal outlines to this root. A preallocated failure cannot prove
    // equality of separately reconstructed outlines on the owner; conservatively refuse without traversing.
    if (selection.has_value() != capturedSelection.has_value() ||
        (selection && (selection->antialiased != capturedSelection->antialiased ||
                       selection->loops.Identity() != capturedSelection->loops.Identity())))
        return false;
    const bool ready = status == ColorEditJobStatus::Ready;
    if ((ready && !prepared._display) || status == ColorEditJobStatus::Cancelled ||
        (!ready && prepared._result.token.sequence != _colorEditSequence))
        return false;
    ColorEdit& panel = _colorEdit.value();
    const auto& rendered = prepared._result.edit;
    const bool cache = ready && rendered.grownGrid == panel.grownGrid && rendered.Placement() == panel.Placement();
    // Retain the old handle owners before the existing validated apply assigns any new one. Refusal restores
    // every handle by the same constant-time swaps. Scalar/settings checks and direct inline goldens stay in
    // ApplyColorEditPreview; it changes the revision exactly when it installs or invalidates displayed pixels.
    const auto exchangeOwners = [&]() noexcept
    {
        std::swap(panel.previewRaster, prepared._retired.raster);
        std::swap(panel.previewCoverage, prepared._retired.coverage);
        panel.previewLookup.swap(prepared._retired.lookup);
        if (cache)
        {
            std::swap(panel.grownImage, prepared._retired.grown);
            std::swap(panel.retiredGrownImage, prepared._retired.pendingGrown);
            std::swap(panel.previewSource, prepared._retired.source);
        }
    };
    const auto precedingRevision = _editRevision;
    exchangeOwners();
    const bool shown = ApplyColorEditPreview(prepared._result);
    if (_editRevision == precedingRevision)
    {
        exchangeOwners();
        return false;
    }
    std::swap(_displayCopy, prepared._display);
    _displayColorOpening = panel.openingSequence;
    _displayColorSequence = panel.previewSequence;
    _displaySourceGeneration = Generation();
    _displayTransformOpening = 0;
    _displayRevision = _editRevision;
    prepared._consumed = true;
    return shown;
}

PreparedColorDisplayPreview DocumentSession::PrepareFailedColorDisplayPreviewPublication(
    ColorDisplayPreviewInput input, ColorEditJobStatus status) noexcept
{
    static_assert(std::is_nothrow_move_constructible_v<ColorEditPreviewResult>);
    return PreparedColorDisplayPreview(PreparedColorDisplayPreview::ConstructionKey{}, std::move(input), status);
}

void DocumentSession::PrepareFailedColorDisplayPreviewPublicationInto(
    ColorDisplayPreviewInput&& input, std::optional<PreparedColorDisplayPreview>& destination,
    ColorEditJobStatus status) noexcept
{
    if (destination)
        return; // Never replace and retire a populated publication on the owner during capture.
    destination.emplace(PreparedColorDisplayPreview::ConstructionKey{}, std::move(input), status);
}
