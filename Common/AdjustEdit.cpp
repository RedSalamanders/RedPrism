#include "Adjust.h"
#include "Document.h"
#include "Selection.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <new>

// The color adjustment and filter panels on the session (C12): previews render synchronously from a copy of the
// layer no larger than a preview limit and show through DisplayDocument; OK renders at full size and records one
// entry; adjustment layers route the same panels to their live record.

namespace
{
// Previews of lookup-table kinds render from a copy no larger than this on the longest side, blurs and the lens
// from a smaller one; noise and grain preview at full size (grain made on a smaller copy would look coarser once
// enlarged).
constexpr int kLutPreviewSide = 4096;
constexpr int kFilterPreviewSide = 2048;

[[nodiscard]] int PreviewLimit(FilterKind kind) noexcept
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
        return kFilterPreviewSide;
    case FilterKind::Curves:
    case FilterKind::Exposure:
    case FilterKind::GradientMap:
        return kLutPreviewSide;
    }
    return kLutPreviewSide;
}

[[nodiscard]] AdjustmentColor ColorFromArgb(std::uint32_t argb) noexcept
{
    return AdjustmentColor{((argb >> 16) & 0xFFu) / 255.0, ((argb >> 8) & 0xFFu) / 255.0, (argb & 0xFFu) / 255.0};
}

[[nodiscard]] LayerTransform CanvasTransform(const CanvasDocument& document) noexcept
{
    LayerTransform transform;
    transform.origin = Vec2{0, 0};
    transform.size = Vec2{static_cast<double>(document.width), static_cast<double>(document.height)};
    return transform;
}

// The straight color of a preview pixel through the layer's mapping; nullopt outside or transparent.
[[nodiscard]] std::optional<std::array<double, 3>> StraightPixel(const Raster& image, const LayerTransform& transform,
                                                                 Vec2 point) noexcept
{
    const std::uint8_t* data = image.Data();
    if (data == nullptr)
    {
        return std::nullopt;
    }
    const Vec2 pixel = PixelToDocument(transform, image.Width(), image.Height()).Inverted().Apply(point);
    if (!(pixel.x >= 0) || !(pixel.y >= 0) || !(pixel.x < image.Width()) || !(pixel.y < image.Height()))
    {
        return std::nullopt;
    }
    const std::uint8_t* p = data +
                            static_cast<std::size_t>(std::floor(pixel.y)) * static_cast<std::size_t>(image.Stride()) +
                            static_cast<std::size_t>(std::floor(pixel.x)) * 4u;
    if (p[3] == 0)
    {
        return std::nullopt;
    }
    return std::array<double, 3>{std::min(1.0, p[2] / static_cast<double>(p[3])),
                                 std::min(1.0, p[1] / static_cast<double>(p[3])),
                                 std::min(1.0, p[0] / static_cast<double>(p[3]))};
}
} // namespace

Raster DocumentSession::SelectionCoverageOn(const DocumentSelection& selection, const LayerTransform& placement,
                                            int width, int height)
{
    return SelectionGeometry::Coverage(
        SelectionGeometry::Transformed(selection.loops, PixelToDocument(placement, width, height).Inverted()), width,
        height, selection.antialiased);
}

bool DocumentSession::CanAdjustColors() const noexcept
{
    if (!_document.has_value() || HasColorEdit() || _brush || _warp || _pixelMove || _maskSelected)
    {
        return false;
    }
    const ImageLayer* layer = ActiveLayer();
    if (layer == nullptr || layer->isGroup || !layer->asset.has_value() ||
        !EffectivelyVisible(_document.value(), layer->id))
    {
        return false;
    }
    return !(_document->selection.has_value() && _document->selection->IsEmpty());
}

DocumentSession::PreviewLayer DocumentSession::PreviewedLayer() const noexcept
{
    if (_adjustmentEditingID.has_value())
    {
        return {};
    }
    if (_filter.has_value() && _filter->previewRaster.HasStorage())
    {
        return PreviewLayer{_filter->layerID, _filter->previewRaster, _filter->previewTransform};
    }
    if (_levels.has_value() && _levels->previewRaster.HasStorage())
    {
        return PreviewLayer{_levels->layerID, _levels->previewRaster, _levels->transform};
    }
    if (_hueSaturation.has_value() && _hueSaturation->previewRaster.HasStorage())
    {
        return PreviewLayer{_hueSaturation->layerID, _hueSaturation->previewRaster, _hueSaturation->transform};
    }
    return {};
}

bool DocumentSession::ReplaceLayerPixels(const Guid& id, const Raster& originalImage, const Raster& image,
                                         const LayerTransform& transform, std::optional<LayerMask> mask,
                                         const std::wstring& name)
{
    ImageLayer* layer = MutableLayer(id);
    if (layer == nullptr || !layer->asset.has_value() || !(layer->asset->image == originalImage) ||
        !image.HasStorage() || !transform.IsValid())
    {
        return false;
    }
    BeginEdit(name);
    layer->asset = RasterAsset{image, Raster::MakeThumbnail(image), layer->name};
    layer->transform = transform;
    layer->mask = std::move(mask);
    ++_editRevision;
    EndEdit();
    return true;
}

// ── Filters ───────────────────────────────────────────────────────────────

void DocumentSession::PrepareFilterPreview()
{
    FilterEdit& edit = _filter.value();
    const Raster& source = edit.Source();
    const int limit = PreviewLimit(edit.kind);
    edit.previewSource = limit > 0 ? Adjust::Downscaled(source, limit) : source;
    edit.previewScale = source.Width() > 0 ? static_cast<double>(edit.previewSource.Width()) / source.Width() : 1.0;
    edit.previewTransform = edit.Placement();
}

bool DocumentSession::GrowFilterForBlur()
{
    FilterEdit& edit = _filter.value();
    const double margin = Adjust::BlurMargin(edit.kind, edit.settings);
    if (!(margin > edit.grownMargin))
    {
        return true;
    }
    const int pad = static_cast<int>(std::ceil(margin));
    if (!GrowFilterGrid(-pad, -pad, edit.original.Width() + pad, edit.original.Height() + pad))
    {
        return false;
    }
    _filter->grownMargin = pad;
    return true;
}

bool DocumentSession::GrowFilterGrid(int left, int top, int right, int bottom)
{
    FilterEdit& edit = _filter.value();
    const int width = edit.original.Width();
    const int height = edit.original.Height();
    // The grid already grown (or the original) unioned with the extent, all in the original's pixels.
    int grownLeft = -edit.grownOffsetX;
    int grownTop = -edit.grownOffsetY;
    int grownRight = grownLeft + (edit.grownImage.HasStorage() ? edit.grownImage.Width() : width);
    int grownBottom = grownTop + (edit.grownImage.HasStorage() ? edit.grownImage.Height() : height);
    const int targetLeft = std::min(grownLeft, left);
    const int targetTop = std::min(grownTop, top);
    const int targetRight = std::max(grownRight, right);
    const int targetBottom = std::max(grownBottom, bottom);
    if (targetLeft == grownLeft && targetTop == grownTop && targetRight == grownRight && targetBottom == grownBottom)
    {
        return true;
    }
    const std::int64_t grownWidth = static_cast<std::int64_t>(targetRight) - targetLeft;
    const std::int64_t grownHeight = static_cast<std::int64_t>(targetBottom) - targetTop;
    if (grownWidth < 1 || grownHeight < 1 || grownWidth > kMaxCanvasSide || grownHeight > kMaxCanvasSide ||
        grownWidth * grownHeight > 100'000'000)
    {
        return false;
    }
    // Decision D7: the grown layer (and a mask on its grid carried onto it) fits the document's budget.
    const ImageLayer* layer = FindLayer(edit.layerID);
    const bool maskGrows = layer != nullptr && layer->mask.has_value() && !layer->mask->placement.has_value() &&
                           layer->mask->asset.image.PixelCount() > 1;
    if (grownWidth * grownHeight > PixelBudgetLeft(false, {edit.layerID}) ||
        (maskGrows && grownWidth * grownHeight > PixelBudgetLeft(true, {edit.layerID})))
    {
        _budgetRefused = true;
        return false;
    }
    const Raster grown = Raster::Replacing(edit.original, -targetLeft, -targetTop, {}, 0, 0,
                                           static_cast<int>(grownWidth), static_cast<int>(grownHeight), 0);
    if (!grown.HasStorage())
    {
        return false;
    }
    LayerTransform expanded = edit.transform;
    expanded.size = Vec2{grownWidth * edit.transform.size.x / width, grownHeight * edit.transform.size.y / height};
    const Vec2 middle = PixelToDocument(edit.transform, width, height)
                            .Apply(Vec2{targetLeft + grownWidth / 2.0, targetTop + grownHeight / 2.0});
    expanded.origin = Vec2{middle.x - expanded.size.x / 2, middle.y - expanded.size.y / 2};
    edit.grownImage = grown;
    edit.grownTransform = expanded;
    edit.grownOffsetX = -targetLeft;
    edit.grownOffsetY = -targetTop;
    PrepareFilterPreview();
    return true;
}

bool DocumentSession::CanContentAwareFill() const noexcept
{
    return CanAdjustColors() && _document.has_value() && _document->selection.has_value() &&
           !_document->selection->IsEmpty();
}

void DocumentSession::BeginFilter(FilterKind kind)
{
    if (!CanAdjustColors() || (kind == FilterKind::ContentAwareFill && !CanContentAwareFill()))
    {
        return;
    }
    CommitTransform();
    ResolveGradient();
    CancelLasso();
    const ImageLayer* layer = ActiveLayer();
    if (layer == nullptr || !layer->asset.has_value() || !_document.has_value())
    {
        return;
    }
    FilterEdit edit;
    edit.kind = kind;
    edit.layerID = layer->id;
    edit.original = layer->asset->image;
    edit.transform = layer->transform;
    edit.selection = _document->selection;
    edit.settings = filterSettings.Normalized();
    // Gradient Map starts from the foreground and background colors, as in Photoshop.
    if (kind == FilterKind::GradientMap)
    {
        edit.settings.gradientMap =
            GradientMapSettings{ColorFromArgb(_foregroundArgb), ColorFromArgb(_backgroundArgb), false};
    }
    edit.seed = NextHealSeed();
    _filter = std::move(edit);
    PrepareFilterPreview();
    if (kind == FilterKind::ContentAwareFill)
    {
        // The fill extends the layer over any of the selection on the canvas past its edge.
        const std::optional<SelectionBounds> bounds = _filter->selection->Bounds();
        if (bounds.has_value())
        {
            const Affine2 toPixels =
                PixelToDocument(_filter->transform, _filter->original.Width(), _filter->original.Height()).Inverted();
            const double x0 = std::max(0.0, bounds->minX);
            const double y0 = std::max(0.0, bounds->minY);
            const double x1 = std::min(static_cast<double>(_document->width), bounds->maxX);
            const double y1 = std::min(static_cast<double>(_document->height), bounds->maxY);
            if (x1 > x0 && y1 > y0)
            {
                double left = std::numeric_limits<double>::max();
                double top = left;
                double right = std::numeric_limits<double>::lowest();
                double bottom = right;
                for (const Vec2 corner : {Vec2{x0, y0}, Vec2{x1, y0}, Vec2{x0, y1}, Vec2{x1, y1}})
                {
                    const Vec2 pixel = toPixels.Apply(corner);
                    left = std::min(left, pixel.x);
                    top = std::min(top, pixel.y);
                    right = std::max(right, pixel.x);
                    bottom = std::max(bottom, pixel.y);
                }
                if (!GrowFilterGrid(static_cast<int>(std::floor(left)), static_cast<int>(std::floor(top)),
                                    static_cast<int>(std::ceil(right)), static_cast<int>(std::ceil(bottom))))
                {
                    _filter.reset();
                    return;
                }
            }
        }
    }
    if (!GrowFilterForBlur())
    {
        _filter.reset();
        return;
    }
    UpdateFilter(_filter->settings, true);
}

void DocumentSession::RenderFilterPreview()
{
    FilterEdit& edit = _filter.value();
    std::optional<Raster> coverage;
    if (edit.selection.has_value())
    {
        coverage = SelectionCoverageOn(edit.selection.value(), edit.previewTransform, edit.previewSource.Width(),
                                       edit.previewSource.Height());
    }
    Raster result = Adjust::RunFilter(edit.kind, edit.previewSource, edit.settings, edit.previewScale, edit.seed,
                                      coverage.has_value() ? &coverage.value() : nullptr);
    edit.previewFailed = edit.kind == FilterKind::ContentAwareFill && !result.HasStorage();
    if (result.HasStorage() && coverage.has_value())
    {
        result = Adjust::BlendThrough(result, edit.previewSource, coverage.value());
    }
    edit.previewRaster = result;
}

void DocumentSession::UpdateFilter(const FilterSettings& settings, bool preview)
{
    if (!_filter.has_value())
    {
        return;
    }
    FilterEdit& edit = _filter.value();
    edit.settings = settings.Normalized();
    edit.preview = preview;
    // A bigger blur needs more room around the layer than it was given.
    if (Adjust::BlurMargin(edit.kind, edit.settings) > edit.grownMargin && !GrowFilterForBlur())
    {
        edit.previewRaster = {};
        ++_editRevision;
        return;
    }
    if (PreviewAdjustmentEditing(preview))
    {
        return;
    }
    if (!preview)
    {
        edit.previewRaster = {};
    }
    else
    {
        RenderFilterPreview();
    }
    ++_editRevision;
}

void DocumentSession::CommitFilter()
{
    if (FinishAdjustmentEditing(true))
    {
        return;
    }
    if (!_filter.has_value() || !_document.has_value())
    {
        return;
    }
    const FilterEdit edit = _filter.value();
    // No distortion to remove: close as Cancel does, without an undo step.
    if ((edit.kind == FilterKind::LensCorrection && edit.settings.distortion == 0) ||
        (edit.kind == FilterKind::Exposure && edit.settings.exposure == ExposureSettings{}) ||
        (edit.kind == FilterKind::Grain && edit.settings.grain.amount == 0))
    {
        CancelFilter();
        return;
    }
    filterSettings = edit.settings;
    std::optional<Raster> coverage;
    if (edit.selection.has_value())
    {
        coverage = SelectionCoverageOn(edit.selection.value(), edit.Placement(), edit.Source().Width(),
                                       edit.Source().Height());
    }
    Raster image = Adjust::RunFilter(edit.kind, edit.Source(), edit.settings, 1.0, edit.seed,
                                     coverage.has_value() ? &coverage.value() : nullptr);
    if (edit.kind == FilterKind::ContentAwareFill && !image.HasStorage())
    {
        // Nothing to copy from: the panel stays open and says so; Cancel closes it.
        _filter->previewFailed = true;
        ++_editRevision;
        return;
    }
    if (image.HasStorage() && coverage.has_value())
    {
        image = Adjust::BlendThrough(image, edit.Source(), coverage.value());
    }
    LayerTransform placed = edit.Placement();
    int cropX = 0;
    int cropY = 0;
    const bool spreads = edit.kind == FilterKind::GaussianBlur || edit.kind == FilterKind::MotionBlur;
    if (image.HasStorage() && spreads && edit.grownImage.HasStorage())
    {
        const Adjust::Trimmed trimmed = Adjust::TrimmedToAlpha(image, placed);
        image = trimmed.image;
        placed = trimmed.transform;
        cropX = trimmed.cropX;
        cropY = trimmed.cropY;
    }
    const ImageLayer* current = image.HasStorage() ? MutableLayer(edit.layerID) : nullptr;
    if (current == nullptr || !current->asset.has_value() || !(current->asset->image == edit.original) ||
        current->transform != edit.transform)
    {
        _filter.reset();
        ++_editRevision;
        return;
    }
    // A grown layer's mask (covering the old grid) is carried onto the new one, its edge tone past the old edge.
    std::optional<LayerMask> mask = current->mask;
    if (edit.grownImage.HasStorage() && mask.has_value() && !mask->placement.has_value() &&
        mask->asset.image.PixelCount() > 1)
    {
        const std::uint8_t tone =
            static_cast<std::uint8_t>(std::lround(MaskEdgeBackground(mask->asset.thumbnail) * 255.0));
        const Raster carried =
            CarriedMask(mask->asset.image, edit.original.Width(), edit.original.Height(), edit.grownOffsetX - cropX,
                        edit.grownOffsetY - cropY, image.Width(), image.Height(), tone);
        if (carried.HasStorage())
        {
            mask->asset.image = carried;
            mask->asset.thumbnail = Raster::MakeThumbnail(carried);
            mask->enabled = true;
        }
    }
    ReplaceLayerPixels(edit.layerID, edit.original, image, placed, std::move(mask), FilterKindName(edit.kind));
    _filter.reset();
    ++_editRevision;
}

void DocumentSession::CancelFilter()
{
    if (FinishAdjustmentEditing(false))
    {
        return;
    }
    if (!_filter.has_value())
    {
        return;
    }
    _filter.reset();
    ++_editRevision;
}

// ── Levels ────────────────────────────────────────────────────────────────

void DocumentSession::BeginLevels()
{
    if (_levels.has_value() || _hueSaturation.has_value() || !CanAdjustColors())
    {
        return;
    }
    CommitTransform();
    ResolveGradient();
    CancelLasso();
    const ImageLayer* layer = ActiveLayer();
    if (layer == nullptr || !layer->asset.has_value() || !_document.has_value())
    {
        return;
    }
    LevelsEdit edit;
    edit.layerID = layer->id;
    edit.original = layer->asset->image;
    edit.transform = layer->transform;
    edit.selection = _document->selection;
    edit.previewSource = Adjust::Downscaled(edit.original, kLutPreviewSide);
    std::optional<Raster> coverage;
    if (edit.selection.has_value())
    {
        coverage =
            SelectionCoverageOn(edit.selection.value(), edit.transform, edit.original.Width(), edit.original.Height());
    }
    edit.histogram = Adjust::Histogram(edit.original, coverage.has_value() ? &coverage.value() : nullptr);
    edit.histogramReady = true;
    _levels = std::move(edit);
}

void DocumentSession::RenderLevelsPreview()
{
    LevelsEdit& edit = _levels.value();
    LayerAdjustment adjustment;
    adjustment.kind = AdjustmentKind::Levels;
    adjustment.levels = edit.settings;
    Raster result = Adjust::ApplyAdjustment(adjustment, edit.previewSource);
    if (result.HasStorage() && edit.selection.has_value())
    {
        const Raster coverage = SelectionCoverageOn(edit.selection.value(), edit.transform, edit.previewSource.Width(),
                                                    edit.previewSource.Height());
        result = Adjust::BlendThrough(result, edit.previewSource, coverage);
    }
    edit.previewRaster = result;
}

void DocumentSession::UpdateLevels(const LevelsSettings& settings, bool preview)
{
    if (!_levels.has_value())
    {
        return;
    }
    LevelsEdit& edit = _levels.value();
    edit.settings = settings;
    edit.preview = preview;
    if (PreviewAdjustmentEditing(preview))
    {
        return;
    }
    if (!preview || Adjust::LevelsIsIdentity(settings))
    {
        edit.previewRaster = {};
    }
    else
    {
        RenderLevelsPreview();
    }
    ++_editRevision;
}

void DocumentSession::AutoLevels(LevelsAuto mode)
{
    if (!_levels.has_value() || !_levels->histogramReady)
    {
        return;
    }
    _levels->sampleMode.reset();
    UpdateLevels(Adjust::AutoLevels(mode, _levels->histogram), _levels->preview);
}

void DocumentSession::SetLevelsSampleMode(std::optional<LevelsSample> mode) noexcept
{
    if (_levels.has_value())
    {
        _levels->sampleMode = mode;
    }
}

void DocumentSession::SampleLevels(Vec2 point)
{
    if (!_levels.has_value() || !_levels->sampleMode.has_value() || !_document.has_value() || !(point.x >= 0) ||
        !(point.y >= 0) || !(point.x < _document->width) || !(point.y < _document->height))
    {
        return;
    }
    const std::optional<std::array<double, 3>> rgb = StraightPixel(_levels->original, _levels->transform, point);
    if (!rgb.has_value())
    {
        return;
    }
    UpdateLevels(Adjust::SampledLevels(_levels->settings, rgb.value(), _levels->sampleMode.value()), _levels->preview);
}

void DocumentSession::CommitLevels()
{
    if (FinishAdjustmentEditing(true))
    {
        return;
    }
    if (!_levels.has_value())
    {
        return;
    }
    const LevelsEdit edit = _levels.value();
    if (Adjust::LevelsIsIdentity(edit.settings))
    {
        CancelLevels();
        return;
    }
    LayerAdjustment adjustment;
    adjustment.kind = AdjustmentKind::Levels;
    adjustment.levels = edit.settings;
    Raster image = Adjust::ApplyAdjustment(adjustment, edit.original);
    if (image.HasStorage() && edit.selection.has_value())
    {
        const Raster coverage =
            SelectionCoverageOn(edit.selection.value(), edit.transform, edit.original.Width(), edit.original.Height());
        image = Adjust::BlendThrough(image, edit.original, coverage);
    }
    const ImageLayer* current = image.HasStorage() ? MutableLayer(edit.layerID) : nullptr;
    if (current != nullptr && current->transform == edit.transform)
    {
        ReplaceLayerPixels(edit.layerID, edit.original, image, edit.transform, current->mask, L"Levels");
    }
    _levels.reset();
    ++_editRevision;
}

void DocumentSession::CancelLevels()
{
    if (FinishAdjustmentEditing(false))
    {
        return;
    }
    if (!_levels.has_value())
    {
        return;
    }
    _levels.reset();
    ++_editRevision;
}

// ── Hue / Saturation ──────────────────────────────────────────────────────

void DocumentSession::BeginHueSaturation()
{
    if (_hueSaturation.has_value() || !CanAdjustColors())
    {
        return;
    }
    CommitTransform();
    ResolveGradient();
    const ImageLayer* layer = ActiveLayer();
    if (layer == nullptr || !layer->asset.has_value() || !_document.has_value())
    {
        return;
    }
    HueSaturationEdit edit;
    edit.layerID = layer->id;
    edit.original = layer->asset->image;
    edit.transform = layer->transform;
    edit.selection = _document->selection;
    edit.previewSource = Adjust::Downscaled(edit.original, kLutPreviewSide);
    _hueSaturation = std::move(edit);
}

void DocumentSession::RenderHuePreview()
{
    HueSaturationEdit& edit = _hueSaturation.value();
    LayerAdjustment adjustment;
    adjustment.kind = AdjustmentKind::Hsv;
    adjustment.hsvSettings = edit.settings;
    Raster result = Adjust::ApplyAdjustment(adjustment, edit.previewSource);
    if (result.HasStorage() && edit.selection.has_value())
    {
        const Raster coverage = SelectionCoverageOn(edit.selection.value(), edit.transform, edit.previewSource.Width(),
                                                    edit.previewSource.Height());
        result = Adjust::BlendThrough(result, edit.previewSource, coverage);
    }
    edit.previewRaster = result;
}

void DocumentSession::UpdateHueSaturation(const HueSaturationSettings& settings, bool preview)
{
    if (!_hueSaturation.has_value())
    {
        return;
    }
    HueSaturationEdit& edit = _hueSaturation.value();
    edit.settings = settings;
    edit.preview = preview;
    if (PreviewAdjustmentEditing(preview))
    {
        return;
    }
    if (!preview || Adjust::HsvIsIdentity(settings))
    {
        edit.previewRaster = {};
    }
    else
    {
        RenderHuePreview();
    }
    ++_editRevision;
}

void DocumentSession::SetHueSampleMode(std::optional<HueSampleMode> mode) noexcept
{
    if (_hueSaturation.has_value())
    {
        _hueSaturation->sampleMode = mode;
        if (mode.has_value())
        {
            _hueSaturation->targeting = false;
        }
    }
}

void DocumentSession::SetHueTargeting(bool targeting) noexcept
{
    if (_hueSaturation.has_value())
    {
        _hueSaturation->targeting = targeting;
        if (targeting)
        {
            _hueSaturation->sampleMode.reset();
        }
    }
}

std::optional<double> DocumentSession::SampledHue(const Raster& composite, Vec2 point) noexcept
{
    const std::optional<std::uint32_t> color = SampleCompositeColor(composite, point);
    if (!color.has_value())
    {
        return std::nullopt;
    }
    const double red = ((color.value() >> 16) & 0xFFu) / 255.0;
    const double green = ((color.value() >> 8) & 0xFFu) / 255.0;
    const double blue = (color.value() & 0xFFu) / 255.0;
    const double high = std::max({red, green, blue});
    const double low = std::min({red, green, blue});
    const double delta = high - low;
    const double saturation = high > 0 ? delta / high : 0;
    if (!(saturation > 0.02) || !(delta > 0))
    {
        return std::nullopt;
    }
    double hue = 0;
    if (high == red)
    {
        hue = (green - blue) / delta;
    }
    else if (high == green)
    {
        hue = (blue - red) / delta + 2;
    }
    else
    {
        hue = (red - green) / delta + 4;
    }
    hue *= 60;
    if (hue < 0)
    {
        hue += 360;
    }
    return hue;
}

void DocumentSession::SampleHueRange(const Raster& composite, Vec2 point)
{
    if (!_hueSaturation.has_value() || !_hueSaturation->sampleMode.has_value())
    {
        return;
    }
    HueSaturationSettings settings = _hueSaturation->settings;
    const std::optional<double> hue = SampledHue(composite, point);
    if (settings.range == ColorRange::Master || settings.colorize || !hue.has_value())
    {
        return;
    }
    HueBand& band = Adjust::BandOf(settings);
    switch (_hueSaturation->sampleMode.value())
    {
    case HueSampleMode::Replace:
        band = Adjust::BandCentered(band, hue.value());
        break;
    case HueSampleMode::Add:
        Adjust::BandInclude(band, hue.value());
        break;
    case HueSampleMode::Remove:
        Adjust::BandExclude(band, hue.value());
        break;
    }
    UpdateHueSaturation(settings, _hueSaturation->preview);
}

bool DocumentSession::BeginHueTargeting(const Raster& composite, Vec2 point)
{
    if (!_hueSaturation.has_value() || !_hueSaturation->targeting || _hueSaturation->settings.colorize)
    {
        return false;
    }
    const std::optional<double> hue = SampledHue(composite, point);
    if (!hue.has_value())
    {
        return false;
    }
    HueSaturationSettings settings = _hueSaturation->settings;
    ColorRange range = ColorRange::Reds;
    double best = -1;
    for (int index = static_cast<int>(ColorRange::Reds); index < static_cast<int>(ColorRange::Count); ++index)
    {
        const double weight = Adjust::RangeWeight(settings, static_cast<ColorRange>(index), hue.value());
        if (weight > best)
        {
            best = weight;
            range = static_cast<ColorRange>(index);
        }
    }
    settings.range = range;
    const RangeAdjustment& adjustment = settings.adjustments[static_cast<std::size_t>(range)];
    _hueSaturation->targetDrag = HueTargetDrag{range, adjustment.hue, adjustment.saturation};
    UpdateHueSaturation(settings, _hueSaturation->preview);
    return true;
}

void DocumentSession::DragHueTargeting(double viewDelta, bool adjustsHue)
{
    if (!_hueSaturation.has_value() || !_hueSaturation->targetDrag.has_value() || !std::isfinite(viewDelta))
    {
        return;
    }
    const HueTargetDrag drag = _hueSaturation->targetDrag.value();
    HueSaturationSettings settings = _hueSaturation->settings;
    RangeAdjustment& adjustment = settings.adjustments[static_cast<std::size_t>(drag.range)];
    // Dragging right raises the value, left lowers it; one unit per two view points, measured from the start.
    if (adjustsHue)
    {
        adjustment.hue = std::min(180.0, std::max(-180.0, drag.hue + viewDelta / 2));
    }
    else
    {
        adjustment.saturation = std::min(100.0, std::max(-100.0, drag.saturation + viewDelta / 2));
    }
    UpdateHueSaturation(settings, _hueSaturation->preview);
}

void DocumentSession::EndHueTargeting() noexcept
{
    if (_hueSaturation.has_value())
    {
        _hueSaturation->targetDrag.reset();
    }
}

void DocumentSession::CommitHueSaturation()
{
    if (FinishAdjustmentEditing(true))
    {
        return;
    }
    if (!_hueSaturation.has_value())
    {
        return;
    }
    const HueSaturationEdit edit = _hueSaturation.value();
    // Identity settings change nothing at all; the preview stays up until the pixels are in the document.
    if (!Adjust::HsvIsIdentity(edit.settings))
    {
        LayerAdjustment adjustment;
        adjustment.kind = AdjustmentKind::Hsv;
        adjustment.hsvSettings = edit.settings;
        Raster image = Adjust::ApplyAdjustment(adjustment, edit.original);
        if (image.HasStorage() && edit.selection.has_value())
        {
            const Raster coverage = SelectionCoverageOn(edit.selection.value(), edit.transform, edit.original.Width(),
                                                        edit.original.Height());
            image = Adjust::BlendThrough(image, edit.original, coverage);
        }
        const ImageLayer* current = image.HasStorage() ? MutableLayer(edit.layerID) : nullptr;
        if (current != nullptr)
        {
            ReplaceLayerPixels(edit.layerID, edit.original, image, current->transform, current->mask,
                               L"Hue/Saturation");
        }
    }
    _hueSaturation.reset();
    ++_editRevision;
}

void DocumentSession::CancelHueSaturation()
{
    if (FinishAdjustmentEditing(false))
    {
        return;
    }
    if (!_hueSaturation.has_value())
    {
        return;
    }
    _hueSaturation.reset();
    ++_editRevision;
}

// ── Adjustment layers ─────────────────────────────────────────────────────

void DocumentSession::AddAdjustment(AdjustmentKind kind)
{
    if (!_document.has_value() || static_cast<int>(_document->layers.size()) >= kMaxLayerCount || HasColorEdit())
    {
        return;
    }
    const std::wstring name = AdjustmentTitle(kind);
    ImageLayer layer =
        ImageLayer::Blank(name, Vec2{static_cast<double>(_document->width), static_cast<double>(_document->height)});
    LayerAdjustment adjustment;
    adjustment.kind = kind;
    // A new Gradient Map runs from the foreground to the background color, as in Photoshop; each Grain layer gets
    // a pattern of its own.
    if (kind == AdjustmentKind::GradientMap)
    {
        adjustment.gradientMapSettings =
            GradientMapSettings{ColorFromArgb(_foregroundArgb), ColorFromArgb(_backgroundArgb), false};
    }
    if (kind == AdjustmentKind::Grain)
    {
        GrainSettings grain;
        grain.seed = NextHealSeed();
        adjustment.grainSettings = grain;
    }
    layer.adjustment = adjustment;
    const Guid id = layer.id;
    InsertLayerAboveActive(std::move(layer), L"New " + name + L" Adjustment", false);
    _adjustmentEditingID = id;
    ++_editRevision;
}

bool DocumentSession::UpdateAdjustment(const Guid& id, const LayerAdjustment& value)
{
    ImageLayer* layer = MutableLayer(id);
    if (layer == nullptr || !layer->adjustment.has_value() || !value.IsValid())
    {
        return false;
    }
    layer->adjustment = value;
    ++_editRevision;
    return true;
}

bool DocumentSession::BeginAdjustmentEditing(const Guid& id, const Raster& below)
{
    if (!_adjustmentEditingID.has_value() || !(_adjustmentEditingID.value() == id) || _adjustmentOriginal.has_value() ||
        HasColorEdit() || !_document.has_value())
    {
        return false;
    }
    const ImageLayer* layer = MutableLayer(id);
    if (layer == nullptr || !layer->adjustment.has_value() || !below.HasStorage() ||
        below.Format() != RasterFormat::Bgra8888)
    {
        _adjustmentEditingID.reset();
        return false;
    }
    const LayerAdjustment original = layer->adjustment.value();
    // The source is only for the histogram and sampling; it never replaces layer pixels.
    const LayerTransform placement = CanvasTransform(_document.value());
    switch (original.kind)
    {
    case AdjustmentKind::Levels:
    {
        LevelsEdit edit;
        edit.layerID = id;
        edit.original = below;
        edit.transform = placement;
        edit.settings = original.levels;
        edit.previewSource = below;
        edit.histogram = Adjust::Histogram(below, nullptr);
        edit.histogramReady = true;
        _levels = std::move(edit);
        break;
    }
    case AdjustmentKind::Hsv:
    {
        HueSaturationEdit edit;
        edit.layerID = id;
        edit.original = below;
        edit.transform = placement;
        edit.settings = original.ResolvedHsv();
        edit.previewSource = below;
        _hueSaturation = std::move(edit);
        break;
    }
    case AdjustmentKind::Curves:
    case AdjustmentKind::Exposure:
    case AdjustmentKind::GradientMap:
    case AdjustmentKind::Grain:
    {
        FilterEdit edit;
        edit.kind = AdjustmentFilterKind(original.kind).value_or(FilterKind::Curves);
        edit.layerID = id;
        edit.original = below;
        edit.transform = placement;
        edit.settings = FilterSettings{};
        edit.settings.curves = original.curves;
        edit.settings.exposure = original.Exposure();
        edit.settings.gradientMap = original.GradientMap();
        edit.settings.grain = original.Grain();
        edit.seed = original.Grain().seed;
        edit.previewSource = below;
        edit.previewTransform = placement;
        _filter = std::move(edit);
        break;
    }
    }
    _adjustmentOriginal = original;
    BeginEdit(L"Edit " + std::wstring(AdjustmentTitle(original.kind)) + L" Adjustment");
    return true;
}

std::optional<LayerAdjustment> DocumentSession::EditedAdjustment() const noexcept
{
    if (!_adjustmentOriginal.has_value())
    {
        return std::nullopt;
    }
    LayerAdjustment value = _adjustmentOriginal.value();
    switch (value.kind)
    {
    case AdjustmentKind::Levels:
        if (!_levels.has_value())
        {
            return std::nullopt;
        }
        value.levels = _levels->settings;
        break;
    case AdjustmentKind::Hsv:
        if (!_hueSaturation.has_value())
        {
            return std::nullopt;
        }
        value.hsvSettings = _hueSaturation->settings;
        break;
    case AdjustmentKind::Curves:
    case AdjustmentKind::Exposure:
    case AdjustmentKind::GradientMap:
    case AdjustmentKind::Grain:
        if (!_filter.has_value())
        {
            return std::nullopt;
        }
        switch (value.kind)
        {
        case AdjustmentKind::Exposure:
            value.exposureSettings = _filter->settings.exposure;
            break;
        case AdjustmentKind::GradientMap:
            value.gradientMapSettings = _filter->settings.gradientMap;
            break;
        case AdjustmentKind::Grain:
            value.grainSettings = _filter->settings.grain;
            break;
        default:
            value.curves = _filter->settings.curves;
            break;
        }
        break;
    }
    return value;
}

bool DocumentSession::PreviewAdjustmentEditing(bool preview)
{
    if (!_adjustmentEditingID.has_value() || !_adjustmentOriginal.has_value())
    {
        return false;
    }
    const std::optional<LayerAdjustment> value = EditedAdjustment();
    if (!value.has_value())
    {
        return false;
    }
    UpdateAdjustment(_adjustmentEditingID.value(), preview ? value.value() : _adjustmentOriginal.value());
    return true;
}

bool DocumentSession::FinishAdjustmentEditing(bool commit)
{
    if (!_adjustmentEditingID.has_value() || !_adjustmentOriginal.has_value())
    {
        return false;
    }
    const Guid id = _adjustmentEditingID.value();
    const LayerAdjustment original = _adjustmentOriginal.value();
    // OK keeps editable settings; Cancel (including the panel's close) restores them.
    UpdateAdjustment(id, commit ? EditedAdjustment().value_or(original) : original);
    _levels.reset();
    _hueSaturation.reset();
    _filter.reset();
    EndEdit();
    _adjustmentOriginal.reset();
    _adjustmentEditingID.reset();
    ++_editRevision;
    return true;
}

void DocumentSession::StepBlendMode(bool forward)
{
    const ImageLayer* layer = ActiveLayer();
    if (layer == nullptr || layer->isGroup || HasColorEdit())
    {
        return;
    }
    constexpr int count = static_cast<int>(LayerBlendMode::Luminosity) + 1;
    const int current = static_cast<int>(layer->blendMode);
    const int next = (current + (forward ? 1 : count - 1)) % count;
    SetBlendMode(static_cast<LayerBlendMode>(next));
}
