#include "ColorEditWork.h"
#include "ColorEditHistory.h"
#include "DocumentDraft.h"

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
bool CanReuseColorPreview(const ColorEdit& edit, std::uint64_t sequence) noexcept
{
    return edit.previewSequence == sequence && edit.preview && !edit.previewFailed && edit.previewRaster.HasStorage() &&
           edit.previewScale == 1.0 && edit.previewSource == edit.Source() &&
           edit.previewTransform == edit.Placement() && edit.previewRaster.Format() == RasterFormat::Bgra8888 &&
           edit.previewRaster.Width() == edit.Source().Width() && edit.previewRaster.Height() == edit.Source().Height();
}

bool CanReuseColorPreview(const ColorEditJobInput& input) noexcept
{
    return CanReuseColorPreview(input.edit, input.token.sequence);
}

bool ValidColorGrid(const ColorEdit& edit) noexcept
{
    if (!edit.original.HasStorage() || edit.original.Format() != RasterFormat::Bgra8888 ||
        !edit.Source().HasStorage() || edit.Source().Format() != RasterFormat::Bgra8888 ||
        !edit.settings.curves.IsValid())
    {
        return false;
    }
    if (!edit.grownGrid)
    {
        return true;
    }
    const ColorEditGrid& grid = *edit.grownGrid;
    return grid.width > 0 && grid.height > 0 && grid.width <= kMaxCanvasSide && grid.height <= kMaxCanvasSide &&
           grid.x <= 0 && grid.y <= 0 && static_cast<std::int64_t>(grid.x) + grid.width >= edit.original.Width() &&
           static_cast<std::int64_t>(grid.y) + grid.height >= edit.original.Height() &&
           (!edit.grownImage.HasStorage() ||
            (edit.grownImage.Width() == grid.width && edit.grownImage.Height() == grid.height &&
             edit.grownImage.Format() == RasterFormat::Bgra8888));
}

std::uint64_t RgbaAllocation(int width, int height) noexcept
{
    return PixelBuffer::RequiredBytes(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4);
}

bool PrepareColorGrid(ColorEdit& edit, const CancelToken& cancellation)
{
    if (!ValidColorGrid(edit) || cancellation.IsCancelled())
    {
        return false;
    }
    if (edit.grownGrid && !edit.grownImage.HasStorage())
    {
        const ColorEditGrid& grid = *edit.grownGrid;
        edit.grownImage = CropRasterForWork(edit.original, {grid.x, grid.y, grid.width, grid.height}, 0, cancellation);
        if (!edit.grownImage.HasStorage())
        {
            return false;
        }
    }
    return !cancellation.IsCancelled();
}

std::pair<int, int> PreviewSize(const ColorEdit& edit) noexcept
{
    const int width = edit.SourceWidth();
    const int height = edit.SourceHeight();
    const int limit = ColorEditPreviewLimit(edit.kind);
    const int largest = std::max(width, height);
    const double factor = limit > 0 && largest > limit ? static_cast<double>(limit) / largest : 1.0;
    return {std::max(1, static_cast<int>(std::lround(width * factor))),
            std::max(1, static_cast<int>(std::lround(height * factor)))};
}

struct CoverageMemory final
{
    std::uint64_t bytes = 0;
    std::uint64_t peak = 0;
};

std::optional<CoverageMemory> CoverageMemoryBytes(const ColorEdit& edit, std::uint64_t width,
                                                  std::uint64_t height) noexcept
{
    if (!edit.selection)
    {
        return CoverageMemory{};
    }
    const auto bytes = PixelBuffer::RequiredBytes(static_cast<std::size_t>(width * height));
    if (edit.selection->antialiased)
    {
        return CoverageMemory{bytes, bytes + (width + 2) * height * 2 * sizeof(float)};
    }
    const auto edges = edit.selection->loops.PointCount();
    if (edges > std::numeric_limits<std::uint64_t>::max() / 32)
    {
        return std::nullopt;
    }
    return CoverageMemory{bytes, bytes + edges * 16};
}

std::optional<std::uint64_t> RenderMemoryBytes(const ColorEdit& edit, int width, int height, double scale,
                                               std::uint64_t privateSource) noexcept
{
    const auto coverage =
        CoverageMemoryBytes(edit, static_cast<std::uint64_t>(width), static_cast<std::uint64_t>(height));
    if (!coverage)
    {
        return std::nullopt;
    }
    const auto pixels = static_cast<std::uint64_t>(width) * height;
    const auto rgba = RgbaAllocation(width, height);
    std::uint64_t kernelPeak = rgba;
    switch (edit.kind)
    {
    case FilterKind::GaussianBlur:
    {
        const double sigma =
            (std::isfinite(edit.settings.radius) ? std::clamp(edit.settings.radius, 0.1, 250.0) : 1.0) * scale;
        const auto taps = static_cast<std::uint64_t>(std::ceil(sigma * 3.0)) * 2 + 1;
        kernelPeak += privateSource + pixels * 4 * sizeof(float) + taps * sizeof(double);
        break;
    }
    case FilterKind::MotionBlur:
        kernelPeak += privateSource;
        break;
    case FilterKind::LensCorrection:
        kernelPeak += rgba;
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
    const auto blendPeak = edit.selection ? rgba * 2 + privateSource : 0;
    return std::max(coverage->peak, coverage->bytes + std::max(kernelPeak, blendPeak));
}
} // namespace

int ColorEditPreviewLimit(FilterKind kind) noexcept
{
    switch (kind)
    {
    case FilterKind::AddNoise:
    case FilterKind::Grain:
    case FilterKind::ContentAwareFill:
        return 0;
    case FilterKind::GaussianBlur:
    case FilterKind::MotionBlur:
    case FilterKind::LensCorrection:
        return 2048;
    default:
        return 4096;
    }
}

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

namespace
{
std::optional<std::uint64_t> CommitMemoryBytes(const ColorEdit& edit, std::uint64_t sequence,
                                               const std::optional<TargetIdentity>& target) noexcept
{
    const Raster& source = edit.Source();
    if (!ValidColorGrid(edit))
    {
        return std::nullopt;
    }
    const auto width = static_cast<std::uint64_t>(edit.SourceWidth());
    const auto height = static_cast<std::uint64_t>(edit.SourceHeight());
    const auto pixels = width * height; // Raster limits each side to 30,000.
    const auto allocation = [](std::uint64_t bytes) noexcept
    { return PixelBuffer::RequiredBytes(static_cast<std::size_t>(bytes)); };
    const std::uint64_t rgba = allocation(pixels * 4);
    const bool needsPadding = edit.grownGrid && !edit.grownImage.HasStorage();
    const std::uint64_t padding = needsPadding ? rgba : 0;
    const bool reused = !needsPadding && CanReuseColorPreview(edit, sequence);
    const std::uint64_t privateSource = needsPadding || source.IsMaterialized() ? 0 : rgba;
    const auto renderPeak = reused
                                ? std::optional<std::uint64_t>(0)
                                : RenderMemoryBytes(edit, edit.SourceWidth(), edit.SourceHeight(), 1.0, privateSource);
    if (!renderPeak)
    {
        return std::nullopt;
    }
    // Trim keeps input and cropped output until the crop is adopted. Finish retains the final image and any eager
    // semantic mask thumbnail; a reused sparse preview may need a private copy even though kernel outputs are flat.
    const bool trim = (edit.grownGrid || edit.grownImage.HasStorage()) &&
                      (edit.kind == FilterKind::GaussianBlur || edit.kind == FilterKind::MotionBlur);
    const std::uint64_t thumbnails = target && target->mask ? allocation(96 * 96) : 0;
    const std::uint64_t newImage = !reused || trim ? rgba : 0;
    const std::uint64_t privatePreview = reused && !edit.previewRaster.IsMaterialized() ? rgba : 0;
    std::uint64_t finishPeak =
        std::max(newImage + thumbnails + privatePreview, trim ? (reused ? rgba : rgba * 2) + privatePreview : 0);
    std::uint64_t metadata = 128 * 1024; // Bounded settings copies, stack, raster headers, small vectors/records.
    if (target && target->mask)
    {
        const LayerMask& mask = target->mask.value();
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
        if ((edit.grownGrid || edit.grownImage.HasStorage()) && !mask.placement && mask.asset.image.PixelCount() > 1)
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
    const std::uint64_t peak = padding + std::max(*renderPeak, finishPeak);
    if (peak > std::numeric_limits<std::uint64_t>::max() - metadata)
    {
        return std::nullopt;
    }
    return metadata + peak;
}
} // namespace

std::optional<std::uint64_t> ColorEditJobMemoryBytes(const ColorEditJobInput& input) noexcept
{
    return CommitMemoryBytes(input.edit, input.token.sequence, input.token.target);
}

std::optional<std::uint64_t> ColorEditCommitMemoryBytes(const ColorEdit& edit, std::uint64_t sequence) noexcept
{
    if (!edit.sourceToken)
        return std::nullopt;
    return CommitMemoryBytes(edit, sequence, edit.sourceToken->target);
}

std::optional<std::uint64_t> ColorEditPreviewMemoryBytes(const ColorEditJobInput& input) noexcept
{
    return ColorEditPreviewMemoryBytes(input.edit, input.lookupPreview, input.liveAdjustment.has_value());
}

std::optional<std::uint64_t> ColorEditPreviewMemoryBytes(const ColorEdit& edit, bool lookupPreview,
                                                         bool liveAdjustment) noexcept
{
    if (liveAdjustment)
        return 1024 * 1024; // immutable tables/cube, bounded settings and result metadata, without raster work
    if (!ValidColorGrid(edit))
    {
        return std::nullopt;
    }
    constexpr std::uint64_t metadata = 128 * 1024;
    if (!edit.preview || ColorEditChangesNothing(edit.kind, edit.settings))
    {
        return metadata;
    }
    const auto [width, height] = PreviewSize(edit);
    const auto rgba = RgbaAllocation(width, height);
    const auto fullRgba = RgbaAllocation(edit.SourceWidth(), edit.SourceHeight());
    const bool needsPadding = edit.grownGrid && !edit.grownImage.HasStorage();
    const auto padding = needsPadding ? fullRgba : 0;
    const bool cached = edit.previewSource.HasStorage() && edit.previewSource.Width() == width &&
                        edit.previewSource.Height() == height && edit.previewTransform == edit.Placement();
    const bool downscale = !cached && (width != edit.SourceWidth() || height != edit.SourceHeight());
    const auto newSource = downscale ? rgba : 0;
    const bool dense =
        downscale || needsPadding || (cached ? edit.previewSource.IsMaterialized() : edit.Source().IsMaterialized());
    const auto render =
        RenderMemoryBytes(edit, width, height, static_cast<double>(width) / edit.SourceWidth(), dense ? 0 : rgba);
    if (!render)
    {
        return std::nullopt;
    }
    // Downscaled currently owns a full-size private read until its smaller result is produced. It is released
    // before filtering starts; the new padding and preview source remain owned by the returned cache.
    const auto preparation = downscale ? fullRgba + rgba : 0;
    if (lookupPreview && FilterIsImageAdjustment(edit.kind))
    {
        const auto coverage = CoverageMemoryBytes(edit, width, height);
        if (!coverage)
            return std::nullopt;
        const bool cachedCoverage = cached && edit.previewCoverage.Format() == RasterFormat::Gray8 &&
                                    edit.previewCoverage.Width() == width && edit.previewCoverage.Height() == height;
        return metadata + 1024 * 1024 + padding +
               std::max(preparation, newSource + (cachedCoverage ? 0 : coverage->peak));
    }
    return metadata + padding + std::max(preparation, newSource + *render);
}

std::optional<std::uint64_t> ColorEditHistogramMemoryBytes(const ColorEditJobInput& input) noexcept
{
    return ColorEditHistogramMemoryBytes(input.edit);
}

std::optional<std::uint64_t> ColorEditHistogramMemoryBytes(const ColorEdit& edit) noexcept
{
    if (!edit.original.HasStorage() || edit.original.Format() != RasterFormat::Bgra8888 ||
        edit.kind != FilterKind::Levels)
    {
        return std::nullopt;
    }
    const auto coverage = CoverageMemoryBytes(edit, static_cast<std::uint64_t>(edit.original.Width()),
                                              static_cast<std::uint64_t>(edit.original.Height()));
    if (!coverage)
    {
        return std::nullopt;
    }
    const auto privateSource =
        edit.original.IsMaterialized() ? 0 : RgbaAllocation(edit.original.Width(), edit.original.Height());
    return 128 * 1024 + privateSource + coverage->peak;
}

ColorEditPreviewResult RunColorEditPreviewJob(const ColorEditJobInput& input, Executor& executor,
                                              CancelToken cancellation)
{
    cancellation = cancellation.WithShutdown(executor.ShutdownToken());
    ColorEditPreviewResult result{input.token, input.edit};
    ColorEdit& edit = result.edit;
    edit.previewRaster = {};
    edit.previewCoverage = {};
    edit.previewLookup.reset();
    edit.previewLookupOnly = false;
    edit.previewSequence = 0;
    const auto failed = [&]()
    {
        result.status = cancellation.IsCancelled() ? ColorEditJobStatus::Cancelled : ColorEditJobStatus::Failed;
        return std::move(result);
    };
    if (input.liveAdjustment)
    {
        // The admitted source remains the original immutable layer in worker mode. Render the accepted
        // settings, rather than depending on an owner-side mutation of that layer's parameter record.
        const auto adjustment = Adjust::AdjustmentWithSettings(input.liveAdjustment.value(), edit.settings);
        edit.previewLookup = Adjust::BuildAdjustmentLookup(adjustment, cancellation);
        if (!edit.previewLookup)
            return failed();
        edit.previewLookupOnly = true;
        edit.previewSequence = input.token.sequence;
        result.status = ColorEditJobStatus::Ready;
        return result;
    }
    if (cancellation.IsCancelled() || !ValidColorGrid(edit))
    {
        return failed();
    }
    if (!edit.preview || ColorEditChangesNothing(edit.kind, edit.settings))
    {
        result.status = ColorEditJobStatus::Unchanged;
        return result;
    }
    if (Adjust::BlurMargin(edit.kind, edit.settings) > edit.grownMargin || !PrepareColorGrid(edit, cancellation))
    {
        return failed();
    }
    const auto [width, height] = PreviewSize(edit);
    if (!edit.previewSource.HasStorage() || edit.previewSource.Width() != width ||
        edit.previewSource.Height() != height || edit.previewTransform != edit.Placement())
    {
        const int limit = ColorEditPreviewLimit(edit.kind);
        edit.previewSource = limit > 0 ? Adjust::Downscaled(edit.Source(), limit, cancellation) : edit.Source();
    }
    if (!edit.previewSource.HasStorage() || cancellation.IsCancelled())
    {
        return failed();
    }
    edit.previewScale = static_cast<double>(width) / edit.SourceWidth();
    edit.previewTransform = edit.Placement();
    if (input.lookupPreview && FilterIsImageAdjustment(edit.kind))
    {
        LayerAdjustment adjustment;
        adjustment.kind = FilterAdjustmentKind(edit.kind).value();
        adjustment = Adjust::AdjustmentWithSettings(std::move(adjustment), edit.settings);
        if (adjustment.kind == AdjustmentKind::Grain)
        {
            auto grain = adjustment.Grain();
            grain.seed = edit.seed;
            adjustment.grainSettings = grain;
        }
        edit.previewLookup = Adjust::BuildAdjustmentLookup(adjustment, cancellation);
        if (edit.selection)
        {
            if (input.edit.previewSource == edit.previewSource &&
                input.edit.previewTransform == edit.previewTransform &&
                input.edit.previewCoverage.Format() == RasterFormat::Gray8 &&
                input.edit.previewCoverage.Width() == width && input.edit.previewCoverage.Height() == height)
            {
                edit.previewCoverage = input.edit.previewCoverage;
            }
            else
            {
                edit.previewCoverage = SelectionGeometry::CoverageTransformed(
                    edit.selection->loops, PixelToDocument(edit.previewTransform, width, height).Inverted(), width,
                    height, edit.selection->antialiased, cancellation);
            }
            if (!edit.previewCoverage.HasStorage())
                return failed();
        }
        if (!edit.previewLookup || cancellation.IsCancelled())
            return failed();
        edit.previewSequence = input.token.sequence;
        result.status = ColorEditJobStatus::Ready;
        return result;
    }
    bool noSource = false;
    edit.previewRaster = RenderColorEditRaster(edit, edit.previewSource, edit.previewTransform, edit.previewScale,
                                               &noSource, cancellation);
    if (cancellation.IsCancelled())
    {
        return failed();
    }
    if (!edit.previewRaster.HasStorage())
    {
        result.status = noSource ? ColorEditJobStatus::NoSource : ColorEditJobStatus::Failed;
        return result;
    }
    edit.previewSequence = input.token.sequence;
    edit.previewFailed = false;
    result.status = ColorEditJobStatus::Ready;
    return result;
}

ColorEditHistogramResult RunColorEditHistogramJob(const ColorEditJobInput& input, Executor& executor,
                                                  CancelToken cancellation)
{
    cancellation = cancellation.WithShutdown(executor.ShutdownToken());
    const ColorEdit& edit = input.edit;
    ColorEditHistogramResult result{input.token, edit.openingSequence, edit.original};
    const auto failed = [&]()
    {
        result.status = cancellation.IsCancelled() ? ColorEditJobStatus::Cancelled : ColorEditJobStatus::Failed;
        return std::move(result);
    };
    if (edit.kind != FilterKind::Levels || cancellation.IsCancelled())
    {
        return failed();
    }
    // Prepare private reads explicitly so Histogram's zero-bin failure result cannot be mistaken for an empty
    // successful histogram. Once these handles exist, its accumulation allocates no additional pixel storage.
    const Raster source = ContiguousRasterForWork(edit.original, cancellation);
    if (!source.HasStorage() || source.Format() != RasterFormat::Bgra8888)
    {
        return failed();
    }
    Raster coverage;
    if (edit.selection)
    {
        coverage = SelectionGeometry::CoverageTransformed(
            edit.selection->loops, PixelToDocument(edit.transform, source.Width(), source.Height()).Inverted(),
            source.Width(), source.Height(), edit.selection->antialiased, cancellation);
        if (!coverage.HasStorage())
        {
            return failed();
        }
    }
    result.histogram = Adjust::Histogram(source, edit.selection ? &coverage : nullptr, cancellation);
    if (cancellation.IsCancelled())
    {
        return failed();
    }
    result.status = ColorEditJobStatus::Ready;
    return result;
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
    ColorEdit edit = input.edit;
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
    if (!PrepareColorGrid(edit, cancellation))
    {
        (void)cancelled();
        return result;
    }
    result.reusedPreview = CanReuseColorPreview(input) && (!input.edit.grownGrid || input.edit.grownImage.HasStorage());
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
    result.image = std::move(image);
    result.transform = placed;
    result.status = ColorEditJobStatus::Ready;
    return result;
}

bool DocumentSession::HasCurrentWandColorSelectionBinding() const noexcept
{
    if (!_document || !_colorEdit || !_colorEdit->_wandSelectionBinding)
        return false;
    const auto& binding = _colorEdit->_wandSelectionBinding.value();
    const auto& current = _document->selection;
    return binding.opening == _colorEdit->openingSequence && binding.selection.has_value() == current.has_value() &&
           (!current || (binding.selection->antialiased == current->antialiased &&
                         binding.selection->loops.Identity() == current->loops.Identity()));
}

void DocumentSession::NormalizeWandColorSelectionToken(JobToken& token) const noexcept
{
    if (!HasCurrentWandColorSelectionBinding() || !token.target || token.target->layerId != _colorEdit->layerID)
        return;
    // Only a newly captured job receives the acknowledged Wand validation selection. The panel still
    // samples/renders its original opening coverage; every older produced token stays strict.
    token.selection = _colorEdit->_wandSelectionBinding->selection;
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
    NormalizeWandColorSelectionToken(token);
    return ColorEditJobInput{std::move(token), _colorEdit.value()};
}

std::optional<ColorEditJobInput> DocumentSession::PrepareColorEditPreview() const
{
    if (!_document || !_colorEdit)
    {
        return std::nullopt;
    }
    auto token = _colorEdit->sourceToken ? _colorEdit->sourceToken : CaptureJobToken(_colorEdit->layerID);
    if (!token)
    {
        return std::nullopt;
    }
    token->sessionIdentity = _tokenIdentity.value;
    token->sequence = _colorEditSequence;
    NormalizeWandColorSelectionToken(*token);
    return ColorEditJobInput{std::move(*token), *_colorEdit};
}

bool DocumentSession::ColorEditOpeningMatches(const JobToken& token, std::uint64_t opening,
                                              const Raster& original) const noexcept
{
    if (!_document || !_colorEdit || token.sessionIdentity != _tokenIdentity.value ||
        token.documentSerial != _documentSerial || token.restoreSerial != _restoreSerial ||
        opening != _colorEdit->openingSequence || original != _colorEdit->original ||
        token.sequence > _colorEditSequence || token.canvasWidth != _document->width ||
        token.canvasHeight != _document->height || !token.target || token.target->layerId != _colorEdit->layerID)
    {
        return false;
    }
    if (!_adjustmentEditingID)
    {
        return CanApplyJobResult(token, token.sequence);
    }
    // An adjustment panel changes its own parameters during preview. The opening composite remains the fixed
    // histogram/sampling source throughout that panel lifetime; its own adjustment change must not stale it.
    const ImageLayer* layer = FindLayer(token.target->layerId);
    return _adjustmentOriginal && layer && layer->adjustment && layer->transform == token.target->transform &&
           layer->mask == token.target->mask;
}

bool DocumentSession::ApplyColorEditPreview(const ColorEditPreviewResult& result)
{
    const bool live = result.edit.previewLookupOnly;
    if (!ColorEditOpeningMatches(result.token, result.edit.openingSequence, result.edit.original) ||
        live != _adjustmentEditingID.has_value() || result.edit.kind != _colorEdit->kind ||
        result.edit.seed != _colorEdit->seed ||
        (!live && (!_colorEdit->preview || ColorEditChangesNothing(_colorEdit->kind, _colorEdit->settings))) ||
        result.token.sequence < _colorEdit->previewSequence ||
        (!live && result.token.sequence <= _colorEdit->previewInvalidatedSequence) ||
        (result.token.sequence == _colorEditSequence &&
         (result.edit.settings != _colorEdit->settings || result.edit.grownGrid != _colorEdit->grownGrid)))
    {
        return false;
    }
    if (result.status != ColorEditJobStatus::Ready)
    {
        if (result.token.sequence != _colorEditSequence || result.status == ColorEditJobStatus::Cancelled)
        {
            return false;
        }
        _colorEdit->previewRaster = {};
        if (!live)
            _colorEdit->previewLookup.reset();
        _colorEdit->previewCoverage = {};
        _colorEdit->previewSequence = 0;
        _colorEdit->previewFailed = result.status == ColorEditJobStatus::NoSource;
        ++_editRevision;
        return false;
    }
    const ColorEdit& rendered = result.edit;
    if (live)
    {
        if (!rendered.previewLookup || !rendered.previewLookup->adjustment.IsValid() ||
            FilterAdjustmentKind(result.edit.kind) != rendered.previewLookup->adjustment.kind ||
            rendered.previewSequence != result.token.sequence)
            return false;
        _colorEdit->previewLookup = rendered.previewLookup;
        _colorEdit->previewLookupOnly = true;
        _colorEdit->previewSequence = result.token.sequence;
        ++_editRevision;
        return true;
    }
    const Raster& displayed = rendered.previewLookup ? rendered.previewSource : rendered.previewRaster;
    if (!displayed.HasStorage() || displayed.Format() != RasterFormat::Bgra8888 ||
        displayed.Width() != rendered.previewSource.Width() || displayed.Height() != rendered.previewSource.Height() ||
        (rendered.previewLookup &&
         (!rendered.previewLookup->adjustment.IsValid() ||
          FilterAdjustmentKind(result.edit.kind) != rendered.previewLookup->adjustment.kind)) ||
        (rendered.selection && rendered.previewLookup &&
         (!rendered.previewCoverage.HasStorage() || rendered.previewCoverage.Format() != RasterFormat::Gray8 ||
          rendered.previewCoverage.Width() != displayed.Width() ||
          rendered.previewCoverage.Height() != displayed.Height())) ||
        rendered.previewSequence != result.token.sequence || !rendered.previewTransform.IsValid() ||
        !(rendered.previewScale > 0 && rendered.previewScale <= 1))
    {
        return false;
    }
    // Display the completed intermediate image without rolling settings or grid planning back to that request.
    _colorEdit->previewRaster = rendered.previewRaster;
    _colorEdit->previewLookup = rendered.previewLookup;
    _colorEdit->previewCoverage = rendered.previewCoverage;
    _colorEdit->previewLookupOnly = false;
    _colorEdit->previewTransform = rendered.previewTransform;
    _colorEdit->previewScale = rendered.previewScale;
    _colorEdit->previewSequence = result.token.sequence;
    _colorEdit->previewFailed = false;
    if (rendered.grownGrid == _colorEdit->grownGrid && rendered.Placement() == _colorEdit->Placement())
    {
        _colorEdit->grownImage = rendered.grownImage;
        _colorEdit->previewSource = rendered.previewSource;
    }
    ++_editRevision;
    return true;
}

bool DocumentSession::ApplyColorEditHistogram(const ColorEditHistogramResult& result)
{
    if (!ColorEditOpeningMatches(result.token, result.openingSequence, result.original) ||
        _colorEdit->kind != FilterKind::Levels || _colorEdit->histogramReady ||
        result.status != ColorEditJobStatus::Ready)
    {
        return false;
    }
    _colorEdit->histogram = result.histogram;
    _colorEdit->histogramReady = true;
    ++_editRevision;
    return true;
}

bool DocumentSession::ApplyColorEditResult(const ColorEditJobResult& result)
{
    return ApplyColorEditResultImpl(result, nullptr);
}

bool DocumentSession::ApplyColorEditResultImpl(const ColorEditJobResult& result, PreparedColorEdit* prepared)
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
        // An older selection result must not close this still-valid panel after its acknowledged Wand.
        // This branch only refuses; publication below still requires the complete public token guard.
        if (!HasCurrentWandColorSelectionBinding() || !IsJobTargetAndSessionCurrent(result.token, _colorEditSequence))
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
        _colorEdit->previewLookup.reset();
        _colorEdit->previewCoverage = {};
        ++_editRevision;
        return false;
    case ColorEditJobStatus::Ready:
        break;
    }
    if (prepared && !IsPreparedColorEditCurrent(*prepared))
        return false;
    if (!result.image.HasStorage() || result.image.Format() != RasterFormat::Bgra8888 ||
        (result.thumbnail.HasStorage() && result.thumbnail.Format() != RasterFormat::Bgra8888) ||
        !result.transform.IsValid())
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
    const ImageLayer* layer = FindLayer(id); // CanApplyJobResult proved that this image layer still exists.
    if (!layer)
        return false;
    if (prepared)
    {
        if (!prepared->_document || !history.Publish(prepared->_history))
            return false;
        std::swap(_document, prepared->_document);
        std::swap(filterSettings, prepared->_result.settings);
        std::swap(_colorEdit, prepared->_retiredPanel);
        ++_colorEditSequence;
        NoteDocumentMutation();
        return true;
    }
    RasterAsset asset{result.image, result.thumbnail, layer->name};
    auto mask = result.mask;
    FilterSettings appliedSettings = result.settings;
    const auto index = IndexOf(_document.value().layers, id);
    if (!index)
        return false;
    auto draft = CanvasDocumentDraft::Clone(_document.value(), _document.value().layers.size());
    if (!draft)
        return false;
    {
        auto& edited = draft.value().EditLayers()[index.value()];
        edited.asset = std::move(asset);
        edited.transform = result.transform;
        edited.mask = std::move(mask);
    }
    auto after = draft.value().Seal();
    if (!after)
        return false;
    BeginEdit(FilterKindName(result.kind));
    std::swap(_document.value(), after.value());
    EndEdit();
    filterSettings = std::move(appliedSettings);
    _colorEdit.reset();
    ++_colorEditSequence;
    ++_editRevision;
    return true;
}

namespace
{
std::int32_t TryRunColorPreviewInline(const DocumentSession& session,
                                      std::optional<ColorEditPreviewResult>& result) noexcept
{
    try
    {
        if (const auto input = session.PrepareColorEditPreview(); input)
        {
            InlineExecutor executor;
            result.emplace(RunColorEditPreviewJob(*input, executor));
        }
        return 0;
    }
    catch (const std::bad_alloc&)
    {
        return static_cast<std::int32_t>(0x8007000eU);
    }
}

std::int32_t TryRunColorHistogramInline(const DocumentSession& session,
                                        std::optional<ColorEditHistogramResult>& result) noexcept
{
    try
    {
        if (const auto input = session.PrepareColorEditPreview(); input)
        {
            InlineExecutor executor;
            result.emplace(RunColorEditHistogramJob(*input, executor));
        }
        return 0;
    }
    catch (const std::bad_alloc&)
    {
        return static_cast<std::int32_t>(0x8007000eU);
    }
}

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

void DocumentSession::RenderColorEditInline()
{
    std::optional<ColorEditPreviewResult> result;
    if (TryRunColorPreviewInline(*this, result) < 0)
    {
        _colorEdit->previewRaster = {};
        _colorEdit->previewSequence = 0;
        _colorEdit->commitFailed = true;
        ++_editRevision;
        return;
    }
    if (result)
    {
        (void)ApplyColorEditPreview(*result);
    }
}

void DocumentSession::MeasureColorEditHistogramInline()
{
    if (!_colorEdit || _colorEdit->kind != FilterKind::Levels || _colorEdit->histogramReady)
    {
        return;
    }
    std::optional<ColorEditHistogramResult> result;
    if (TryRunColorHistogramInline(*this, result) < 0)
    {
        _colorEdit->commitFailed = true;
        return;
    }
    if (result)
    {
        (void)ApplyColorEditHistogram(*result);
    }
}

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
