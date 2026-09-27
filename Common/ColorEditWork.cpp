#include "ColorEditWork.h"

#include "Adjust.h"
#include "RasterWork.h"
#include "Selection.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <new>
#include <utility>

namespace
{
bool CanReuseColorPreview(const ColorEdit& edit) noexcept
{
    return edit.preview && !edit.previewFailed && edit.previewRaster.HasStorage() && edit.previewScale == 1.0 &&
           edit.previewSource == edit.Source() && edit.previewTransform == edit.Placement() &&
           edit.previewRaster.Format() == RasterFormat::Bgra8888 &&
           edit.previewRaster.Width() == edit.Source().Width() && edit.previewRaster.Height() == edit.Source().Height();
}
} // namespace

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

std::optional<std::uint64_t> ColorEditJobMemoryBytes(const ColorEditJobInput& input) noexcept
{
    const ColorEdit& edit = input.edit;
    const Raster& source = edit.Source();
    if (!source.HasStorage() || source.Format() != RasterFormat::Bgra8888 || !edit.settings.curves.IsValid())
    {
        return std::nullopt;
    }
    const auto width = static_cast<std::uint64_t>(source.Width());
    const auto height = static_cast<std::uint64_t>(source.Height());
    const auto pixels = width * height; // Raster limits each side to 30,000.
    const auto allocation = [](std::uint64_t bytes) noexcept
    { return PixelBuffer::RequiredBytes(static_cast<std::size_t>(bytes)); };
    const std::uint64_t rgba = allocation(pixels * 4);
    const bool reused = CanReuseColorPreview(edit);
    const std::uint64_t privateSource = source.IsMaterialized() ? 0 : rgba;
    std::uint64_t coverage = 0;
    std::uint64_t coveragePeak = 0;
    if (!reused && edit.selection.has_value())
    {
        coverage = allocation(pixels);
        if (edit.selection->antialiased)
        {
            coveragePeak = coverage + (width + 2) * height * 2 * sizeof(float);
        }
        else
        {
            // Hard coverage reserves one {double x, int winding, padding} per edge, in place.
            const std::uint64_t edges = edit.selection->loops.PointCount();
            if (edges > std::numeric_limits<std::uint64_t>::max() / 32)
            {
                return std::nullopt;
            }
            coveragePeak = coverage + edges * 16;
        }
    }
    std::uint64_t kernelPeak = rgba;
    switch (edit.kind)
    {
    case FilterKind::GaussianBlur:
    {
        // RunFilter normalizes radius to [0.1,250], with 1 for a non-finite input; commit scale is one.
        const double sigma = std::isfinite(edit.settings.radius) ? std::clamp(edit.settings.radius, 0.1, 250.0) : 1.0;
        const auto taps = static_cast<std::uint64_t>(std::ceil(sigma * 3.0)) * 2 + 1;
        kernelPeak += privateSource + pixels * 4 * sizeof(float) + taps * sizeof(double);
        break;
    }
    case FilterKind::MotionBlur:
        kernelPeak += privateSource;
        break;
    case FilterKind::LensCorrection:
        kernelPeak += rgba; // Private source and output coexist, even for a contiguous input.
        break;
    case FilterKind::ContentAwareFill:
        kernelPeak += pixels * (4 * sizeof(std::uint8_t) + 3 * sizeof(int));
        break;
    case FilterKind::HueSaturation:
        kernelPeak += static_cast<std::uint64_t>(Adjust::kCubeDimension) * Adjust::kCubeDimension *
                      Adjust::kCubeDimension * 4 * sizeof(float);
        break;
    default:
        break;
    }
    // Selection coverage survives filtering and blending. Blending owns the filter output, a private writable
    // output and (only for an unassembled source) a private original. It does not copy an existing mask.
    const std::uint64_t renderPeak =
        reused ? 0 : coverage + std::max(kernelPeak, edit.selection ? rgba * 2 + privateSource : 0);
    // Trim keeps input and cropped output until the crop is adopted. Finish retains the final image and eager
    // thumbnails; a reused sparse preview may need a private copy even though ordinary kernel outputs are flat.
    const bool trim =
        edit.grownImage.HasStorage() && (edit.kind == FilterKind::GaussianBlur || edit.kind == FilterKind::MotionBlur);
    const std::uint64_t thumbnails = allocation(96 * 96 * 4) + allocation(96 * 96);
    const std::uint64_t newImage = !reused || trim ? rgba : 0;
    const std::uint64_t privatePreview = reused && !edit.previewRaster.IsMaterialized() ? rgba : 0;
    std::uint64_t finishPeak =
        std::max(newImage + thumbnails + privatePreview, trim ? (reused ? rgba : rgba * 2) + privatePreview : 0);
    std::uint64_t metadata = 128 * 1024; // Bounded settings copies, stack, raster headers, small vectors/records.
    if (input.token.target && input.token.target->mask)
    {
        const LayerMask& mask = input.token.target->mask.value();
        if (mask.asset.image.Format() != RasterFormat::Gray8)
        {
            return std::nullopt;
        }
        const auto nameLength = static_cast<std::uint64_t>(mask.asset.name.size());
        if (nameLength > (std::numeric_limits<std::uint64_t>::max() - metadata) / (4 * sizeof(wchar_t)) - 1)
        {
            return std::nullopt;
        }
        metadata += (nameLength + 1) * 4 * sizeof(wchar_t);
        if (edit.grownImage.HasStorage() && !mask.placement && mask.asset.image.PixelCount() > 1)
        {
            std::uint64_t maskPeak = newImage + thumbnails + privatePreview + allocation(pixels) + width * sizeof(int);
            if (!mask.asset.image.IsMaterialized())
            {
                maskPeak +=
                    allocation(static_cast<std::uint64_t>(mask.asset.image.Stride()) * mask.asset.image.Height());
            }
            if (!mask.asset.thumbnail.IsMaterialized())
            {
                maskPeak += allocation(static_cast<std::uint64_t>(mask.asset.thumbnail.Stride()) *
                                       mask.asset.thumbnail.Height());
            }
            finishPeak = std::max(finishPeak, maskPeak);
        }
    }
    const std::uint64_t peak = std::max({coveragePeak, renderPeak, finishPeak});
    if (peak > std::numeric_limits<std::uint64_t>::max() - metadata)
    {
        return std::nullopt;
    }
    return metadata + peak;
}

Raster RenderColorEditRaster(const ColorEdit& edit, const Raster& source, const LayerTransform& placement, double scale,
                             bool* noSource, const CancelToken& cancellation)
{
    if (noSource != nullptr)
    {
        *noSource = false;
    }
    if (cancellation.IsCancelled())
    {
        return {};
    }
    if (!edit.selection.has_value())
    {
        return Adjust::RunFilter(edit.kind, source, edit.settings, scale, edit.seed, nullptr, noSource, cancellation);
    }
    const Raster coverage = SelectionGeometry::CoverageTransformed(
        edit.selection->loops, PixelToDocument(placement, source.Width(), source.Height()).Inverted(), source.Width(),
        source.Height(), edit.selection->antialiased, cancellation);
    // An allocation failure must never change a selected edit into an unselected edit.
    if (!coverage.HasStorage())
    {
        return {};
    }
    const Raster result =
        Adjust::RunFilter(edit.kind, source, edit.settings, scale, edit.seed, &coverage, noSource, cancellation);
    return result.HasStorage() ? Adjust::BlendThrough(result, source, coverage, cancellation) : result;
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
    result.reusedPreview = CanReuseColorPreview(edit);
    bool noSource = false;
    Raster image = result.reusedPreview
                       ? edit.previewRaster
                       : RenderColorEditRaster(edit, edit.Source(), edit.Placement(), 1.0, &noSource, cancellation);
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
        const Adjust::Trimmed trimmed = Adjust::TrimmedToAlpha(image, placed, cancellation);
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
        const Raster edgeThumbnail = ContiguousRasterForWork(result.mask->asset.thumbnail, cancellation);
        if (cancelled() || (result.mask->asset.thumbnail.HasStorage() && !edgeThumbnail.HasStorage()))
        {
            return result;
        }
        const std::uint8_t tone = static_cast<std::uint8_t>(std::lround(MaskEdgeBackground(edgeThumbnail) * 255.0));
        const Raster carried = CarriedMaskForWork(
            result.mask->asset.image, edit.original.Width(), edit.original.Height(), edit.grownOffsetX - cropX,
            edit.grownOffsetY - cropY, image.Width(), image.Height(), tone, cancellation);
        if (cancelled() || !carried.HasStorage())
        {
            return result;
        }
        result.mask->asset.image = carried;
        result.mask->asset.thumbnail = ThumbnailRasterForWork(carried, 96, cancellation);
        result.mask->enabled = true;
        if (cancelled() || !result.mask->asset.thumbnail.HasStorage())
        {
            return result;
        }
    }
    if (cancelled())
    {
        return result;
    }
    result.thumbnail = ThumbnailRasterForWork(image, 96, cancellation);
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
