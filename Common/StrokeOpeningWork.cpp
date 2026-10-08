#include "StrokeOpeningWork.h"

#include "ImageFilters.h"
#include "PixelBuffer.h"
#include "RasterWork.h"
#include "Selection.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <type_traits>
#include <utility>

namespace
{
[[nodiscard]] bool Checkpoint(StrokeOpeningObserver observer, StrokeOpeningStage stage, CancelToken cancellation)
{
    if (cancellation.IsCancelled())
        return false;
    if (observer.checkpoint)
        observer.checkpoint(observer.context, stage);
    return !cancellation.IsCancelled();
}

[[nodiscard]] bool OptionsValid(const StrokeOpeningOptions& options, const CanvasDocument& source, bool mask) noexcept
{
    if (!options.settings.IsValid() || options.kind > BrushKind::Blur ||
        options.healing > SpotHealingMode::ProximityMatch ||
        (mask && (options.kind == BrushKind::Clone || options.kind == BrushKind::Heal)))
        return false;
    if (options.kind != BrushKind::Clone)
        return !options.cloneOffset && !options.cloneSample.HasStorage();
    if (!options.cloneOffset || !options.cloneSample.HasStorage() ||
        options.cloneSample.Format() != RasterFormat::Bgra8888 || options.cloneSample.Width() != source.width ||
        options.cloneSample.Height() != source.height || !options.cloneSample.RetirementMemoryBytes())
        return false;
    for (const double value : {options.cloneOffset->x, options.cloneOffset->y})
        if (!std::isfinite(value) || std::round(value) < (std::numeric_limits<int>::min)() ||
            std::round(value) > (std::numeric_limits<int>::max)())
            return false;
    return true;
}
} // namespace

std::optional<StrokeOpeningInput> DocumentSession::PrepareStrokeOpeningInput(
    StrokeOpeningOptions options) const noexcept
{
    static_assert(std::is_nothrow_copy_constructible_v<CanvasDocument>);
    if (!_document || !_activeLayerID || !CanPaint() || _brush || _brushFinishingOpening || _brushUpdatingOpening ||
        _brushOpening == UINT64_MAX || _transformEdit || _gradient || _pixelMove || _warp || _colorEdit ||
        _shapeDraft || !OptionsValid(options, _document.value(), _maskSelected) || !_document->RetirementMemoryBytes())
        return std::nullopt;
    const auto* layer = ActiveLayer();
    if (!layer || layer->id != _activeLayerID.value())
        return std::nullopt;
    const auto pixels = RasterEditPixelLimit(*layer, _maskSelected);
    if (pixels < 1)
        return std::nullopt;
    if (_maskSelected)
    {
        options.settings.red = options.maskWhite ? 1 : 0;
        options.settings.green = options.settings.red;
        options.settings.blue = options.settings.red;
    }
    return StrokeOpeningInput(CaptureImageImportAuthority(), history.CaptureAuthority(), _document.value(),
                              _activeLayerID.value(), _maskSelected, pixels, std::move(options));
}

StrokeOpeningResult DocumentSession::PrepareStrokeOpeningPublication(StrokeOpeningInput input, CancelToken cancellation,
                                                                     StrokeOpeningObserver observer)
{
    const auto failed = [&]() noexcept
    {
        return StrokeOpeningResult{cancellation.IsCancelled() ? StrokeOpeningStatus::Cancelled
                                                              : StrokeOpeningStatus::Invalid};
    };
    if (!OptionsValid(input._options, input._source, input._mask) ||
        !Checkpoint(observer, StrokeOpeningStage::Draft, cancellation))
        return failed();
    const auto* target = input._source.layers.FindKey(input._target);
    if (cancellation.IsCancelled())
        return failed();
    if (!target || (!input._mask && (target->isGroup || target->adjustment)) ||
        (input._mask && (!target->mask || !target->mask->enabled)))
        return failed();
    // Worker factory owns the complete CPU graph and its preallocated retirement node.
    auto stroke = BrushStroke::MakeForWork(*target, input._mask, input._options.settings, input._source.width,
                                           input._source.height, input._options.kind, input._options.healing);
    if (!stroke || !stroke->IsValid())
        return failed();
    stroke->SetPixelLimit(input._pixelLimit);
    if (!Checkpoint(observer, StrokeOpeningStage::Selection, cancellation))
        return failed();
    if (input._source.selection)
    {
        const auto& selection = input._source.selection.value();
        auto coverage = SelectionGeometry::Coverage(selection.loops, input._source.width, input._source.height,
                                                    selection.antialiased, cancellation);
        if (!coverage.HasStorage())
            return failed();
        stroke->SetSelectionClip(std::move(coverage));
    }
    if (!Checkpoint(observer, StrokeOpeningStage::BlurSample, cancellation))
        return failed();
    if (input._options.kind == BrushKind::Blur)
    {
        Raster sharp;
        if (input._mask)
        {
            const auto background =
                static_cast<std::uint8_t>(std::lround(MaskEdgeBackground(target->mask->asset.thumbnail) * 255.0));
            // Preserve reference mask taps without materializing the shared source.
            sharp = ImageFilters::RenderMaskPixelsForWork(target->mask->asset.image, MaskTransform(*target),
                                                          input._source.width, input._source.height, background,
                                                          cancellation);
        }
        else if (target->asset)
            sharp = RenderLayerPixels(target->asset->image, target->transform, input._source.width,
                                      input._source.height, cancellation);
        if (!sharp.HasStorage())
            return failed();
        const double sigma = std::min(30.0, std::max(1.5, input._options.settings.diameter / 10.0));
        auto sample = ImageFilters::GaussianBlur(sharp, sigma, cancellation);
        if (!sample.HasStorage())
            return failed();
        stroke->SetSample(std::move(sample), 0, 0, false);
        stroke->SetEditName(L"Blur");
    }
    else if (input._options.kind == BrushKind::Clone)
    {
        stroke->SetSample(input._options.cloneSample, static_cast<int>(std::lround(input._options.cloneOffset->x)),
                          static_cast<int>(std::lround(input._options.cloneOffset->y)), false);
        stroke->SetEditName(L"Clone Stamp");
    }
    else if (input._options.kind == BrushKind::Heal)
        stroke->SetEditName(L"Spot Healing");
    if (!Checkpoint(observer, StrokeOpeningStage::Complete, cancellation) || !stroke->RetirementMemoryBytes())
        return failed();
    StrokeOpeningResult result;
    result.status = StrokeOpeningStatus::Ready;
    result.prepared.emplace(PreparedStrokeOpening(std::move(input), std::move(stroke)));
    return result;
}

bool DocumentSession::IsPreparedStrokeOpeningCurrent(const PreparedStrokeOpening& prepared) const noexcept
{
    return !prepared._applied && prepared._stroke && prepared._stroke->IsValid() && !_brush &&
           !_brushFinishingOpening && !_brushUpdatingOpening && _brushOpening != UINT64_MAX && !_transformEdit &&
           !_gradient && !_pixelMove && !_warp && !_colorEdit && !_shapeDraft &&
           CanApplyImageImport(prepared._input._authority) && history.IsCurrent(prepared._input._history) &&
           _document && _document->layers.Identity() == prepared._input._source.layers.Identity() &&
           _document->width == prepared._input._source.width && _document->height == prepared._input._source.height &&
           _document->resolution == prepared._input._source.resolution &&
           _document->selection.has_value() == prepared._input._source.selection.has_value() &&
           (!_document->selection ||
            (_document->selection->antialiased == prepared._input._source.selection->antialiased &&
             _document->selection->loops.Identity() == prepared._input._source.selection->loops.Identity()));
}

bool DocumentSession::ApplyPreparedStrokeOpeningIfCurrent(PreparedStrokeOpening& prepared) noexcept
{
    if (!IsPreparedStrokeOpeningCurrent(prepared) || !prepared._stroke->AttachCoverageForOpening(_brushEngine))
        return false;
    _brush = prepared._stroke; // Already allocated private owner: no sample, coverage or pixel/history construction.
    ++_brushOpening;
    if (prepared._input._options.kind == BrushKind::Clone)
        _cloneOffset = prepared._input._options.cloneOffset;
    prepared._applied = true;
    return true;
}

bool PreparedStrokeOpening::RetainMemory(SharedRetainedMemoryLease&& lease) noexcept
{
    return !_applied && _stroke && _stroke->RetainOpeningMemory(std::move(lease));
}

std::uint64_t StrokeOpeningMemoryBytes(const CanvasDocument& source, const ImageLayer& layer, bool mask,
                                       std::int64_t pixelLimit, const BrushSettings& settings, BrushKind kind,
                                       const Raster* cloneSample) noexcept
{
    if (source.width < 1 || source.height < 1 || source.width > kMaxCanvasSide || source.height > kMaxCanvasSide ||
        pixelLimit < 1 || pixelLimit > kMaxLayerPixels || kind > BrushKind::Blur ||
        (mask && (!layer.mask || !layer.mask->enabled || kind == BrushKind::Heal || kind == BrushKind::Clone)))
        return 0;
    if (kind == BrushKind::Clone)
    {
        if (!cloneSample || !cloneSample->HasStorage() || cloneSample->Format() != RasterFormat::Bgra8888 ||
            cloneSample->Width() != source.width || cloneSample->Height() != source.height ||
            !cloneSample->RetirementMemoryBytes())
            return 0;
    }
    else if (cloneSample)
        return 0;
    const auto grid = BrushStroke::PlanGrid(layer, mask, settings, source.width, source.height);
    const auto retained = source.RetirementMemoryBytes();
    if (!grid || !retained)
        return 0;
    std::uint64_t bytes = retained;
    const auto add = [&](std::uint64_t count, std::uint64_t unit) noexcept
    {
        const auto maximum = (std::numeric_limits<std::uint64_t>::max)();
        if (unit && count > maximum / unit)
            return false;
        const auto value = count * unit;
        if (value > maximum - bytes)
            return false;
        bytes += value;
        return true;
    };
    const auto buffer = [&](std::uint64_t count, std::uint64_t channels, std::uint64_t allocations = 1) noexcept
    {
        if (channels && count > (std::numeric_limits<std::size_t>::max)() / channels)
            return false;
        const auto allocation = PixelBuffer::RequiredBytes(static_cast<std::size_t>(count * channels));
        return allocation && add(allocations, allocation);
    };
    // The sampled source already has its source-job lease. The live draft also owns this immutable
    // graph until its final reader retires, so declare that complete cached bound before capture.
    if (cloneSample && !add(cloneSample->RetirementMemoryBytes(), 1))
        return 0;
    const auto columns = (static_cast<std::uint64_t>(grid->width) + 255) / 256;
    const auto rows = (static_cast<std::uint64_t>(grid->height) + 255) / 256;
    // A supported commit rectangle has area <= pixelLimit and both sides <=30000. Its alignment may
    // add at most two tile columns/rows: WH/256^2 + 2(W+H)/256 + 4. This bounds touched tiles even
    // when the inverse paint grid itself extends to 1e9 pixels. No supported rectangle is clipped here.
    const auto envelope = (static_cast<std::uint64_t>(pixelLimit) + 65535) / 65536 +
                          4ULL * ((static_cast<std::uint64_t>(kMaxCanvasSide) + 255) / 256) + 4;
    const auto tiles = (std::min)(columns * rows, envelope);
    const std::uint64_t channels = mask ? 1 : 4;
    // Bases, old/new output vectors plus Make copies, byte coverage, CPU permanent fallback, geometric
    // capacities, hash nodes/directories, four samples and 1024 adaptive chords. Private results coexist
    // with the complete retained source. Conservative live bounds also cover future failure retirement.
    if (!add(1, 128 * 1024 + 2 * sizeof(BrushStroke)) || !add(tiles, 65536 * (4 * channels + 1 + 4) + 16384) ||
        !add(2048, sizeof(BrushSegment)))
        return 0;
    const Raster* image = mask ? &layer.mask->asset.image : layer.asset ? &layer.asset->image : nullptr;
    if (image && image->HasStorage())
    {
        const auto windows = image->GridReplacementWorkspaceBytes();
        if (!windows || !image->RetirementMemoryBytes() || !add(windows.value(), 1) ||
            !add(image->RetirementMemoryBytes(), 1) || !add(static_cast<std::uint64_t>(image->Stride()), 1))
            return 0;
    }
    if (source.selection)
    {
        const auto pixels = static_cast<std::uint64_t>(source.width) * source.height;
        if (!buffer(pixels, 1))
            return 0;
        if (source.selection->antialiased)
        {
            if (!add((static_cast<std::uint64_t>(source.width) + 2) * source.height, 2 * sizeof(float)))
                return 0;
        }
        else if (!add(source.selection->loops.PointCount(), 32))
            return 0;
    }
    if (kind == BrushKind::Blur)
    {
        const auto pixels = static_cast<std::uint64_t>(source.width) * source.height;
        // Sharp canvas, sampled result, temporary byte vector/Make copy and float separable intermediate.
        if (!buffer(pixels, channels, 3) || !add(pixels, channels * sizeof(float)) || !add(181, sizeof(double)))
            return 0;
        if (image && image->HasStorage() &&
            !buffer(static_cast<std::uint64_t>(image->Width()) * image->Height(), channels))
            return 0;
    }
    return bytes;
}

std::shared_ptr<BrushStroke> PreparedStrokeOpening::TakeAppliedDraftForOwner() noexcept
{
    return _applied ? std::move(_stroke) : std::shared_ptr<BrushStroke>{};
}
