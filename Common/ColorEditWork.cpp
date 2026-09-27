#include "ColorEditWork.h"

#include "Adjust.h"
#include "Selection.h"

#include <cmath>
#include <new>
#include <utility>

bool ColorEditChangesNothing(FilterKind kind, const FilterSettings& settings) noexcept
{
    switch (kind)
    {
    case FilterKind::Levels:
        return Adjust::LevelsIsIdentity(settings.levels);
    case FilterKind::HueSaturation:
        return Adjust::HsvIsIdentity(settings.hsv);
    case FilterKind::LensCorrection:
        return settings.distortion == 0;
    case FilterKind::Exposure:
        return settings.exposure == ExposureSettings{};
    case FilterKind::Grain:
        return settings.grain.amount == 0;
    case FilterKind::GaussianBlur:
    case FilterKind::MotionBlur:
    case FilterKind::AddNoise:
    case FilterKind::ContentAwareFill:
    case FilterKind::Curves:
    case FilterKind::GradientMap:
        return false;
    }
    return false;
}

Raster RenderColorEditRaster(const ColorEdit& edit, const Raster& source, const LayerTransform& placement, double scale,
                             bool* noSource)
{
    if (noSource != nullptr)
    {
        *noSource = false;
    }
    if (!edit.selection.has_value())
    {
        return Adjust::RunFilter(edit.kind, source, edit.settings, scale, edit.seed, nullptr, noSource);
    }
    const Raster coverage = SelectionGeometry::Coverage(
        SelectionGeometry::Transformed(edit.selection->loops,
                                       PixelToDocument(placement, source.Width(), source.Height()).Inverted()),
        source.Width(), source.Height(), edit.selection->antialiased);
    // An allocation failure must never change a selected edit into an unselected edit.
    if (!coverage.HasStorage())
    {
        return {};
    }
    const Raster result = Adjust::RunFilter(edit.kind, source, edit.settings, scale, edit.seed, &coverage, noSource);
    return result.HasStorage() ? Adjust::BlendThrough(result, source, coverage) : result;
}

ColorEditJobResult RunColorEditJob(const ColorEditJobInput& input, Executor& executor, CancelToken cancellation)
{
    cancellation = cancellation.WithShutdown(executor.ShutdownToken());
    const ColorEdit& edit = input.edit;
    ColorEditJobResult result;
    result.token = input.token;
    result.kind = edit.kind;
    result.settings = edit.settings;
    const auto cancelled = [&]() noexcept
    {
        if (!cancellation.IsCancelled())
        {
            return false;
        }
        result.status = ColorEditJobStatus::Cancelled;
        result.image = {};
        result.thumbnail = {};
        result.mask.reset();
        return true;
    };
    if (cancelled())
    {
        return result;
    }
    if (ColorEditChangesNothing(edit.kind, edit.settings))
    {
        result.status = ColorEditJobStatus::Unchanged;
        return result;
    }
    // A settings update may have refused a larger grid while retaining the panel for retry. Never commit that
    // larger blur on the previous, smaller grid: it would clip the spread even when the final pixels fit.
    if (Adjust::BlurMargin(edit.kind, edit.settings) > edit.grownMargin)
    {
        return result;
    }
    // The direct path always replaces its preview on Update. The host's mailbox must only populate this
    // candidate from the exact current settings/source/seed once previews become asynchronous.
    result.reusedPreview = edit.preview && !edit.previewFailed && edit.previewRaster.HasStorage() &&
                           edit.previewScale == 1.0 && edit.previewSource == edit.Source() &&
                           edit.previewTransform == edit.Placement();
    bool noSource = false;
    Raster image = result.reusedPreview ? edit.previewRaster
                                        : RenderColorEditRaster(edit, edit.Source(), edit.Placement(), 1.0, &noSource);
    if (cancelled())
    {
        return result;
    }
    if (!image.HasStorage())
    {
        result.status = noSource ? ColorEditJobStatus::NoSource : ColorEditJobStatus::Failed;
        return result;
    }
    LayerTransform placed = edit.Placement();
    int cropX = 0;
    int cropY = 0;
    if ((edit.kind == FilterKind::GaussianBlur || edit.kind == FilterKind::MotionBlur) && edit.grownImage.HasStorage())
    {
        const Adjust::Trimmed trimmed = Adjust::TrimmedToAlpha(image, placed);
        image = trimmed.image;
        placed = trimmed.transform;
        cropX = trimmed.cropX;
        cropY = trimmed.cropY;
    }
    if (cancelled() || !image.HasStorage() || !placed.IsValid() || !input.token.target.has_value())
    {
        return result;
    }
    result.mask = input.token.target->mask;
    if (edit.grownImage.HasStorage() && result.mask.has_value() && !result.mask->placement.has_value() &&
        result.mask->asset.image.PixelCount() > 1)
    {
        const std::uint8_t tone =
            static_cast<std::uint8_t>(std::lround(MaskEdgeBackground(result.mask->asset.thumbnail) * 255.0));
        const Raster carried =
            CarriedMask(result.mask->asset.image, edit.original.Width(), edit.original.Height(),
                        edit.grownOffsetX - cropX, edit.grownOffsetY - cropY, image.Width(), image.Height(), tone);
        if (!carried.HasStorage() || cancelled())
        {
            return result;
        }
        result.mask->asset.image = carried;
        result.mask->asset.thumbnail = Raster::MakeThumbnail(carried);
        result.mask->enabled = true;
        if (!result.mask->asset.thumbnail.HasStorage())
        {
            return result;
        }
    }
    if (cancelled())
    {
        return result;
    }
    result.thumbnail = Raster::MakeThumbnail(image);
    if (!result.thumbnail.HasStorage() || cancelled())
    {
        return result;
    }
    result.image = std::move(image);
    result.transform = placed;
    result.status = ColorEditJobStatus::Ready;
    return result;
}

std::optional<ColorEditJobInput> DocumentSession::PrepareColorEditCommit() const
{
    if (!_document.has_value() || !_colorEdit.has_value() || _adjustmentEditingID.has_value() ||
        !_colorEdit->sourceToken.has_value())
    {
        return std::nullopt;
    }
    // Preserve the opening target, mask, selection, canvas dimensions and restore epoch. Capturing those now
    // would bless a change made while the panel was open. A fresh job belongs to this owner, including after
    // copying a session; an old owner's already-produced result still fails the identity check below.
    JobToken token = _colorEdit->sourceToken.value();
    token.sessionIdentity = _tokenIdentity.value;
    token.sequence = _colorEditSequence;
    return ColorEditJobInput{std::move(token), _colorEdit.value()};
}

bool DocumentSession::ApplyColorEditResult(const ColorEditJobResult& result)
{
    if (!_colorEdit.has_value() || _adjustmentEditingID.has_value() ||
        result.token.sessionIdentity != _tokenIdentity.value || result.token.sequence != _colorEditSequence ||
        result.kind != _colorEdit->kind || result.settings != _colorEdit->settings ||
        !result.token.target.has_value() || result.token.target->layerId != _colorEdit->layerID)
    {
        return false; // A late result must not close or mark a newer panel as failed.
    }
    if (!CanApplyJobResult(result.token, _colorEditSequence))
    {
        CancelColorEdit();
        return false;
    }
    switch (result.status)
    {
    case ColorEditJobStatus::Unchanged:
        CancelColorEdit();
        return false;
    case ColorEditJobStatus::Cancelled:
        return false;
    case ColorEditJobStatus::NoSource:
        _colorEdit->commitFailed = false;
        _colorEdit->previewFailed = true;
        ++_editRevision;
        return false;
    case ColorEditJobStatus::Failed:
        _colorEdit->commitFailed = true;
        ++_editRevision;
        return false;
    case ColorEditJobStatus::Ready:
        break;
    }
    if (!result.image.HasStorage() || result.image.Format() != RasterFormat::Bgra8888 ||
        !result.thumbnail.HasStorage() || !result.transform.IsValid())
    {
        _colorEdit->commitFailed = true;
        ++_editRevision;
        return false;
    }
    // Unrelated edits may consume the document budget without invalidating a targeted token.
    const Guid id = _colorEdit->layerID;
    if (result.image.PixelCount() > PixelBudgetLeft(false, {id}) ||
        (result.mask.has_value() && result.mask->asset.image.PixelCount() > PixelBudgetLeft(true, {id})))
    {
        _budgetRefused = true;
        _colorEdit->commitFailed = true;
        ++_editRevision;
        return false;
    }
    ImageLayer* layer = MutableLayer(id); // CanApplyJobResult proved that this image layer still exists.
    RasterAsset asset{result.image, result.thumbnail, layer->name};
    auto mask = result.mask;
    FilterSettings appliedSettings = result.settings;
    BeginEdit(FilterKindName(result.kind));
    layer->asset = std::move(asset);
    layer->transform = result.transform;
    layer->mask = std::move(mask);
    EndEdit();
    filterSettings = std::move(appliedSettings);
    _colorEdit.reset();
    ++_colorEditSequence;
    ++_editRevision;
    return true;
}

namespace
{
// Direct API construction/computation boundary. No document or history is changed here. On allocation failure
// the caller fails this operation and retains the panel. History allocation remains in the owning apply path.
std::int32_t TryRunColorEditInline(const DocumentSession& session, std::optional<ColorEditJobResult>& result) noexcept
{
    try
    {
        if (const auto input = session.PrepareColorEditCommit(); input.has_value())
        {
            InlineExecutor executor;
            result.emplace(RunColorEditJob(input.value(), executor));
        }
        return 0;
    }
    catch (const std::bad_alloc&)
    {
        return static_cast<std::int32_t>(0x8007000eU); // E_OUTOFMEMORY without Windows types in the document graph.
    }
}
} // namespace

void DocumentSession::CommitColorEdit()
{
    if (FinishAdjustmentEditing(true) || !_colorEdit.has_value() || !_document.has_value())
    {
        return;
    }
    std::optional<ColorEditJobResult> result;
    if (TryRunColorEditInline(*this, result) < 0)
    {
        _colorEdit->commitFailed = true;
        ++_editRevision;
        return;
    }
    if (result.has_value())
    {
        (void)ApplyColorEditResult(result.value());
    }
}
