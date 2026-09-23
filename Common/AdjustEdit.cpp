#include "Adjust.h"
#include "BrushStroke.h"
#include "Document.h"
#include "Selection.h"

#include <algorithm>
#include <cmath>
#include <new>

// The color adjustment and filter panels on the session (C12): one edit for every kind (`ColorEdit`); previews render
// synchronously from a copy of the layer no larger than the kind's preview limit and show through DisplayDocument;
// OK renders at full size and records one entry; adjustment layers route the same panels to their live record.

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
    case FilterKind::Levels:
    case FilterKind::HueSaturation:
        return kLutPreviewSide;
    }
    return kLutPreviewSide;
}

// An edit that changes nothing previews the layer's own pixels and closes without an entry on OK: Levels and
// Hue/Saturation at identity, no distortion to remove, the default exposure, no grain. The other kinds always
// record their entry (a Curves panel left on its identity curve too, as in the legacy).
[[nodiscard]] bool ChangesNothing(FilterKind kind, const FilterSettings& settings) noexcept
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

// The selection's coverage on the grid of a `width` × `height` raster that `placement` puts on the document.
[[nodiscard]] Raster CoverageOn(const DocumentSelection& selection, const LayerTransform& placement, int width,
                                int height)
{
    return SelectionGeometry::Coverage(
        SelectionGeometry::Transformed(selection.loops, PixelToDocument(placement, width, height).Inverted()), width,
        height, selection.antialiased);
}

// The edit's kernel on `source` (placed by `placement`, `scale` pixels per layer pixel), blended through the
// selection's coverage on that grid.
[[nodiscard]] Raster Rendered(const ColorEdit& edit, const Raster& source, const LayerTransform& placement,
                              double scale)
{
    if (!edit.selection.has_value())
    {
        return Adjust::RunFilter(edit.kind, source, edit.settings, scale, edit.seed);
    }
    const Raster coverage = CoverageOn(edit.selection.value(), placement, source.Width(), source.Height());
    const Raster result = Adjust::RunFilter(edit.kind, source, edit.settings, scale, edit.seed, &coverage);
    return result.HasStorage() ? Adjust::BlendThrough(result, source, coverage) : result;
}

// Levels reads the alpha- and selection-weighted histogram of what it opens on, once.
void MeasureHistogram(ColorEdit& edit)
{
    if (edit.kind != FilterKind::Levels)
    {
        return;
    }
    std::optional<Raster> coverage;
    if (edit.selection.has_value())
    {
        coverage = CoverageOn(edit.selection.value(), edit.transform, edit.original.Width(), edit.original.Height());
    }
    edit.histogram = Adjust::Histogram(edit.original, coverage.has_value() ? &coverage.value() : nullptr);
    edit.histogramReady = true;
}
} // namespace

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
    if (_adjustmentEditingID.has_value() || !_colorEdit.has_value() || !_colorEdit->previewRaster.HasStorage())
    {
        return {};
    }
    return PreviewLayer{_colorEdit->layerID, _colorEdit->previewRaster, _colorEdit->previewTransform};
}

ColorEdit* DocumentSession::OpenColorEdit(FilterKind kind) noexcept
{
    return HasColorEdit(kind) ? &_colorEdit.value() : nullptr;
}

// ── Grown grids (blurs and Content-Aware Fill) ────────────────────────────

void DocumentSession::PrepareFilterPreview()
{
    ColorEdit& edit = _colorEdit.value();
    const Raster& source = edit.Source();
    const int limit = PreviewLimit(edit.kind);
    edit.previewSource = limit > 0 ? Adjust::Downscaled(source, limit) : source;
    edit.previewScale = source.Width() > 0 ? static_cast<double>(edit.previewSource.Width()) / source.Width() : 1.0;
    edit.previewTransform = edit.Placement();
}

bool DocumentSession::GrowFilterForBlur()
{
    ColorEdit& edit = _colorEdit.value();
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
    _colorEdit->grownMargin = pad;
    return true;
}

bool DocumentSession::GrowFilterGrid(int left, int top, int right, int bottom)
{
    ColorEdit& edit = _colorEdit.value();
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
    const GridRect grownRect{targetLeft, targetTop, static_cast<int>(grownWidth), static_cast<int>(grownHeight)};
    edit.grownImage = grown;
    edit.grownTransform = PlacementOfRect(edit.transform, width, height, grownRect);
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

// ── The panel session ─────────────────────────────────────────────────────

void DocumentSession::BeginColorEdit(FilterKind kind)
{
    if (!CanAdjustColors() || (kind == FilterKind::ContentAwareFill && !CanContentAwareFill()))
    {
        return;
    }
    // Every kind settles the same pending work first: a pending transform commits, a pending gradient applies and a
    // lasso draft goes, so the edit reads the layer and the selection as they will stay.
    CommitTransform();
    ResolveGradient();
    CancelLasso();
    const ImageLayer* layer = ActiveLayer();
    if (layer == nullptr || !layer->asset.has_value() || !_document.has_value())
    {
        return;
    }
    ColorEdit edit;
    edit.kind = kind;
    edit.layerID = layer->id;
    edit.original = layer->asset->image;
    edit.transform = layer->transform;
    edit.selection = _document->selection;
    // The last applied settings; Levels and Hue/Saturation start at identity and a Gradient Map from the foreground
    // and background colors, as in Photoshop.
    edit.settings = filterSettings.Normalized();
    edit.settings.levels = {};
    edit.settings.hsv = {};
    if (kind == FilterKind::GradientMap)
    {
        edit.settings.gradientMap =
            GradientMapSettings{ColorFromArgb(_foregroundArgb), ColorFromArgb(_backgroundArgb), false};
    }
    // Add Noise and Grain keep a pattern of their own while the panel is open; no other kind reads a seed.
    if (kind == FilterKind::AddNoise || kind == FilterKind::Grain)
    {
        edit.seed = NextHealSeed();
    }
    MeasureHistogram(edit);
    _colorEdit = std::move(edit);
    PrepareFilterPreview();
    if (kind == FilterKind::ContentAwareFill)
    {
        // The fill extends the layer over any of the selection on the canvas past its edge.
        const std::optional<SelectionBounds> bounds = _colorEdit->selection->Bounds();
        if (bounds.has_value())
        {
            const Affine2 toPixels =
                PixelToDocument(_colorEdit->transform, _colorEdit->original.Width(), _colorEdit->original.Height())
                    .Inverted();
            const double x0 = std::max(0.0, bounds->minX);
            const double y0 = std::max(0.0, bounds->minY);
            const double x1 = std::min(static_cast<double>(_document->width), bounds->maxX);
            const double y1 = std::min(static_cast<double>(_document->height), bounds->maxY);
            if (x1 > x0 && y1 > y0)
            {
                const std::array<Vec2, 4> corners{toPixels.Apply(Vec2{x0, y0}), toPixels.Apply(Vec2{x1, y0}),
                                                  toPixels.Apply(Vec2{x0, y1}), toPixels.Apply(Vec2{x1, y1})};
                const SelectionBounds box = PointBounds(corners);
                if (!GrowFilterGrid(static_cast<int>(std::floor(box.minX)), static_cast<int>(std::floor(box.minY)),
                                    static_cast<int>(std::ceil(box.maxX)), static_cast<int>(std::ceil(box.maxY))))
                {
                    _colorEdit.reset();
                    return;
                }
            }
        }
    }
    if (!GrowFilterForBlur())
    {
        _colorEdit.reset();
        return;
    }
    UpdateColorEdit(_colorEdit->settings, true);
}

void DocumentSession::UpdateColorEdit(const FilterSettings& settings, bool preview)
{
    if (!_colorEdit.has_value())
    {
        return;
    }
    ColorEdit& edit = _colorEdit.value();
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
    if (!preview || ChangesNothing(edit.kind, edit.settings))
    {
        edit.previewRaster = {};
    }
    else
    {
        edit.previewRaster = Rendered(edit, edit.previewSource, edit.previewTransform, edit.previewScale);
        edit.previewFailed = edit.kind == FilterKind::ContentAwareFill && !edit.previewRaster.HasStorage();
    }
    ++_editRevision;
}

void DocumentSession::UpdateLevels(const LevelsSettings& settings, bool preview)
{
    if (const ColorEdit* edit = OpenColorEdit(FilterKind::Levels); edit != nullptr)
    {
        FilterSettings all = edit->settings;
        all.levels = settings;
        UpdateColorEdit(all, preview);
    }
}

void DocumentSession::UpdateHueSaturation(const HueSaturationSettings& settings, bool preview)
{
    if (const ColorEdit* edit = OpenColorEdit(FilterKind::HueSaturation); edit != nullptr)
    {
        FilterSettings all = edit->settings;
        all.hsv = settings;
        UpdateColorEdit(all, preview);
    }
}

void DocumentSession::CommitColorEdit()
{
    if (FinishAdjustmentEditing(true) || !_colorEdit.has_value() || !_document.has_value())
    {
        return;
    }
    const ColorEdit edit = _colorEdit.value();
    if (ChangesNothing(edit.kind, edit.settings))
    {
        CancelColorEdit();
        return;
    }
    filterSettings = edit.settings;
    Raster image = Rendered(edit, edit.Source(), edit.Placement(), 1.0);
    if (edit.kind == FilterKind::ContentAwareFill && !image.HasStorage())
    {
        // Nothing to copy from: the panel stays open and says so; Cancel closes it.
        _colorEdit->previewFailed = true;
        ++_editRevision;
        return;
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
    // OK installs only while the layer still holds the pixels and the placement the panel opened on; otherwise the
    // panel closes without an entry.
    ImageLayer* layer = image.HasStorage() && placed.IsValid() ? MutableLayer(edit.layerID) : nullptr;
    if (layer != nullptr && layer->asset.has_value() && layer->asset->image == edit.original &&
        layer->transform == edit.transform)
    {
        // A grown layer's mask (covering the old grid) is carried onto the new one, its edge tone past the old edge.
        std::optional<LayerMask> mask = layer->mask;
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
        BeginEdit(FilterKindName(edit.kind));
        layer->asset = RasterAsset{image, Raster::MakeThumbnail(image), layer->name};
        layer->transform = placed;
        layer->mask = std::move(mask);
        EndEdit();
    }
    _colorEdit.reset();
    ++_editRevision;
}

void DocumentSession::CancelColorEdit()
{
    if (FinishAdjustmentEditing(false) || !_colorEdit.has_value())
    {
        return;
    }
    _colorEdit.reset();
    ++_editRevision;
}

// ── Levels ────────────────────────────────────────────────────────────────

void DocumentSession::AutoLevels(LevelsAuto mode)
{
    ColorEdit* edit = OpenColorEdit(FilterKind::Levels);
    if (edit == nullptr || !edit->histogramReady)
    {
        return;
    }
    edit->levelsSample.reset();
    UpdateLevels(Adjust::AutoLevels(mode, edit->histogram), edit->preview);
}

void DocumentSession::SetLevelsSampleMode(std::optional<LevelsSample> mode) noexcept
{
    if (ColorEdit* edit = OpenColorEdit(FilterKind::Levels); edit != nullptr)
    {
        edit->levelsSample = mode;
    }
}

void DocumentSession::SampleLevels(Vec2 point)
{
    const ColorEdit* edit = OpenColorEdit(FilterKind::Levels);
    if (edit == nullptr || !edit->levelsSample.has_value() || !_document.has_value() || !(point.x >= 0) ||
        !(point.y >= 0) || !(point.x < _document->width) || !(point.y < _document->height))
    {
        return;
    }
    const std::optional<std::array<double, 3>> rgb = StraightPixel(edit->original, edit->transform, point);
    if (!rgb.has_value())
    {
        return;
    }
    UpdateLevels(Adjust::SampledLevels(edit->settings.levels, rgb.value(), edit->levelsSample.value()), edit->preview);
}

// ── Hue / Saturation ──────────────────────────────────────────────────────

void DocumentSession::SetHueSampleMode(std::optional<HueSampleMode> mode) noexcept
{
    if (ColorEdit* edit = OpenColorEdit(FilterKind::HueSaturation); edit != nullptr)
    {
        edit->hueSample = mode;
        if (mode.has_value())
        {
            edit->targeting = false;
        }
    }
}

void DocumentSession::SetHueTargeting(bool targeting) noexcept
{
    if (ColorEdit* edit = OpenColorEdit(FilterKind::HueSaturation); edit != nullptr)
    {
        edit->targeting = targeting;
        if (targeting)
        {
            edit->hueSample.reset();
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
    const ColorEdit* edit = OpenColorEdit(FilterKind::HueSaturation);
    if (edit == nullptr || !edit->hueSample.has_value())
    {
        return;
    }
    HueSaturationSettings settings = edit->settings.hsv;
    const std::optional<double> hue = SampledHue(composite, point);
    if (settings.range == ColorRange::Master || settings.colorize || !hue.has_value())
    {
        return;
    }
    HueBand& band = Adjust::BandOf(settings);
    switch (edit->hueSample.value())
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
    UpdateHueSaturation(settings, edit->preview);
}

bool DocumentSession::BeginHueTargeting(const Raster& composite, Vec2 point)
{
    ColorEdit* edit = OpenColorEdit(FilterKind::HueSaturation);
    if (edit == nullptr || !edit->targeting || edit->settings.hsv.colorize)
    {
        return false;
    }
    const std::optional<double> hue = SampledHue(composite, point);
    if (!hue.has_value())
    {
        return false;
    }
    HueSaturationSettings settings = edit->settings.hsv;
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
    edit->targetDrag = HueTargetDrag{range, adjustment.hue, adjustment.saturation};
    UpdateHueSaturation(settings, edit->preview);
    return true;
}

void DocumentSession::DragHueTargeting(double viewDelta, bool adjustsHue)
{
    const ColorEdit* edit = OpenColorEdit(FilterKind::HueSaturation);
    if (edit == nullptr || !edit->targetDrag.has_value() || !std::isfinite(viewDelta))
    {
        return;
    }
    const HueTargetDrag drag = edit->targetDrag.value();
    HueSaturationSettings settings = edit->settings.hsv;
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
    UpdateHueSaturation(settings, edit->preview);
}

void DocumentSession::EndHueTargeting() noexcept
{
    if (ColorEdit* edit = OpenColorEdit(FilterKind::HueSaturation); edit != nullptr)
    {
        edit->targetDrag.reset();
    }
}

// ── Adjustment layers ─────────────────────────────────────────────────────

void DocumentSession::AddAdjustment(AdjustmentKind kind)
{
    if (!_document.has_value() || static_cast<int>(_document->layers.size()) >= kMaxLayerCount || HasColorEdit())
    {
        return;
    }
    const std::wstring name = FilterKindName(AdjustmentFilterKind(kind));
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
    ColorEdit edit;
    edit.kind = AdjustmentFilterKind(original.kind);
    edit.layerID = id;
    edit.original = below;
    edit.transform = CanvasTransform(_document.value());
    edit.settings = Adjust::AdjustmentSettings(original);
    edit.seed = original.Grain().seed;
    MeasureHistogram(edit);
    _colorEdit = std::move(edit);
    _adjustmentOriginal = original;
    BeginEdit(L"Edit " + std::wstring(FilterKindName(AdjustmentFilterKind(original.kind))) + L" Adjustment");
    return true;
}

std::optional<LayerAdjustment> DocumentSession::EditedAdjustment() const
{
    if (!_adjustmentOriginal.has_value() || !_colorEdit.has_value())
    {
        return std::nullopt;
    }
    return Adjust::AdjustmentWithSettings(_adjustmentOriginal.value(), _colorEdit->settings);
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
    _colorEdit.reset();
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
    constexpr int count = LayerBlendModeCount();
    const int current = static_cast<int>(layer->blendMode);
    const int next = (current + (forward ? 1 : count - 1)) % count;
    SetBlendMode(static_cast<LayerBlendMode>(next));
}
