#include "Adjust.h"
#include "BrushStroke.h"
#include "ColorEditWork.h"
#include "Document.h"
#include "DocumentDraft.h"
#include "RasterWork.h"
#include "Selection.h"

#include <algorithm>
#include <cmath>
#include <utility>

// The panel owns metadata and immutable handles. Deferred callers prepare worker inputs explicitly; direct
// callers run the same ColorEditWork graph inline. Neither result path may overwrite newer settings.

namespace
{
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
    if (!image.HasStorage())
    {
        return std::nullopt;
    }
    const Vec2 pixel = PixelToDocument(transform, image.Width(), image.Height()).Inverted().Apply(point);
    if (!(pixel.x >= 0) || !(pixel.y >= 0) || !(pixel.x < image.Width()) || !(pixel.y < image.Height()))
    {
        return std::nullopt;
    }
    std::array<std::uint8_t, 4> sampled{};
    if (!ReadRasterRegionForWork(image, {static_cast<int>(pixel.x), static_cast<int>(pixel.y), 1, 1}, sampled, 4, 0))
    {
        return std::nullopt;
    }
    const std::uint8_t* p = sampled.data();
    if (p[3] == 0)
    {
        return std::nullopt;
    }
    return std::array<double, 3>{std::min(1.0, p[2] / static_cast<double>(p[3])),
                                 std::min(1.0, p[1] / static_cast<double>(p[3])),
                                 std::min(1.0, p[0] / static_cast<double>(p[3]))};
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
    if (_adjustmentEditingID.has_value() || !_colorEdit.has_value() ||
        (!_colorEdit->previewRaster.HasStorage() && !_colorEdit->previewLookup))
    {
        return {};
    }
    return PreviewLayer{_colorEdit->layerID,
                        _colorEdit->previewLookup ? _colorEdit->previewSource : _colorEdit->previewRaster,
                        _colorEdit->previewTransform};
}

ColorEdit* DocumentSession::OpenColorEdit(FilterKind kind) noexcept
{
    return HasColorEdit(kind) ? &_colorEdit.value() : nullptr;
}

// ── Grown grids (blurs and Content-Aware Fill) ────────────────────────────

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

bool DocumentSession::HoldColorDisplayOwners() const noexcept
{
    return _colorDisplayJobsEnabled && _colorEdit && _colorEdit->deferred && !_adjustmentEditingID;
}

void DocumentSession::InvalidateColorPreviewOwners() noexcept
{
    if (HoldColorDisplayOwners())
        return; // Visibility follows its sequence; complete owners transfer at admitted publication/dismissal.
    _colorEdit->previewRaster = {};
    _colorEdit->previewLookup.reset();
    _colorEdit->previewCoverage = {};
}

bool DocumentSession::GrowFilterGrid(int left, int top, int right, int bottom)
{
    ColorEdit& edit = _colorEdit.value();
    const int width = edit.original.Width();
    const int height = edit.original.Height();
    // The grid already grown (or the original) unioned with the extent, all in the original's pixels.
    int grownLeft = -edit.grownOffsetX;
    int grownTop = -edit.grownOffsetY;
    int grownRight = grownLeft + edit.SourceWidth();
    int grownBottom = grownTop + edit.SourceHeight();
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
    const GridRect grownRect{targetLeft, targetTop, static_cast<int>(grownWidth), static_cast<int>(grownHeight)};
    if (HoldColorDisplayOwners() && edit.grownImage.HasStorage())
    {
        // An acknowledged matching publication drains this slot before installing another grown cache.
        // Refuse an unexpected second owner instead of synchronously freeing the first one on a settings tick.
        if (edit.retiredGrownImage.HasStorage())
            return false;
        std::swap(edit.grownImage, edit.retiredGrownImage);
    }
    edit.grownGrid = ColorEditGrid{targetLeft, targetTop, grownRect.width, grownRect.height};
    edit.grownImage = {};
    if (!HoldColorDisplayOwners())
        edit.previewSource = {};
    edit.previewSequence = 0;
    edit.grownTransform = PlacementOfRect(edit.transform, width, height, grownRect);
    edit.grownOffsetX = -targetLeft;
    edit.grownOffsetY = -targetTop;
    return true;
}

bool DocumentSession::CanContentAwareFill() const noexcept
{
    return CanAdjustColors() && _document.has_value() && _document->selection.has_value() &&
           !_document->selection->IsEmpty();
}

// ── The panel session ─────────────────────────────────────────────────────

void DocumentSession::BeginColorEdit(FilterKind kind, bool deferred)
{
    if (HasDeferredTransform() || (_gradient && _gradient->deferred) || (_pixelMove && _pixelMove->deferred))
        return;
    if (!CanAdjustColors() || (kind == FilterKind::ContentAwareFill && !CanContentAwareFill()))
    {
        return;
    }
    // Every kind settles the same pending work first: a pending transform commits, a pending gradient applies and a
    // lasso draft goes, so the edit reads the layer and the selection as they will stay.
    CommitTransform();
    if (!ResolveGradient())
        return;
    CancelLasso();
    const ImageLayer* layer = ActiveLayer();
    if (layer == nullptr || !layer->asset.has_value() || !_document.has_value())
    {
        return;
    }
    ColorEdit edit;
    edit.deferred = deferred;
    edit.kind = kind;
    edit.layerID = layer->id;
    edit.original = layer->asset->image;
    edit.transform = layer->transform;
    edit.selection = _document->selection;
    edit.sourceToken = CaptureJobToken(layer->id);
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
    edit.openingSequence = ++_colorEditSequence;
    _colorEdit = std::move(edit);
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
                if (!std::isfinite(box.minX) || !std::isfinite(box.minY) || !std::isfinite(box.maxX) ||
                    !std::isfinite(box.maxY) || box.minX < -kMaxCanvasSide || box.minY < -kMaxCanvasSide ||
                    box.maxX > kMaxCanvasSide || box.maxY > kMaxCanvasSide)
                {
                    _budgetRefused = true;
                    _colorEdit.reset();
                    return;
                }
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
    if (!deferred)
    {
        MeasureColorEditHistogramInline();
    }
}

void DocumentSession::UpdateColorEdit(const FilterSettings& settings, bool preview)
{
    if (!_colorEdit.has_value())
    {
        return;
    }
    ColorEdit& edit = _colorEdit.value();
    edit.settings = settings.Normalized();
    ++_colorEditSequence;
    edit.commitFailed = false;
    edit.preview = preview;
    // A bigger blur needs more room around the layer than it was given.
    if (Adjust::BlurMargin(edit.kind, edit.settings) > edit.grownMargin && !GrowFilterForBlur())
    {
        InvalidateColorPreviewOwners();
        edit.previewSequence = 0;
        edit.previewInvalidatedSequence = _colorEditSequence;
        ++_editRevision;
        return;
    }
    if (PreviewAdjustmentEditing(preview))
    {
        return;
    }
    if (!preview || ColorEditChangesNothing(edit.kind, edit.settings))
    {
        InvalidateColorPreviewOwners();
        edit.previewSequence = 0;
        edit.previewFailed = false;
        edit.previewInvalidatedSequence = _colorEditSequence;
    }
    else if (!edit.deferred)
    {
        RenderColorEditInline();
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

void DocumentSession::CancelColorEdit()
{
    if (FinishAdjustmentEditing(false) || !_colorEdit.has_value())
    {
        return;
    }
    _colorEdit.reset();
    ++_colorEditSequence;
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
    const auto rangeIndex = static_cast<std::size_t>(range);
    if (rangeIndex >= settings.adjustments.size())
    {
        return false;
    }
    settings.range = range;
    const RangeAdjustment& adjustment = settings.adjustments[rangeIndex];
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
    if (HasDeferredTransform())
        return;

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
    if (HasDeferredTransform())
        return false;
    const ImageLayer* layer = FindLayer(id);
    if (layer == nullptr || !layer->adjustment.has_value() || !value.IsValid())
    {
        return false;
    }
    const auto index = IndexOf(_document.value().layers, id);
    if (!index)
        return false;
    auto draft = CanvasDocumentDraft::Clone(_document.value(), _document.value().layers.size());
    if (!draft)
        return false;
    draft.value().EditLayers()[index.value()].adjustment = value;
    auto after = draft.value().Seal();
    if (!after)
        return false;
    std::swap(_document.value(), after.value());
    NoteDocumentMutation();
    return true;
}

bool DocumentSession::BeginAdjustmentEditing(const Guid& id, const Raster& below, bool deferred)
{
    if (HasDeferredTransform())
        return false;
    if (!_adjustmentEditingID.has_value() || !(_adjustmentEditingID.value() == id) || _adjustmentOriginal.has_value() ||
        HasColorEdit() || !_document.has_value())
    {
        return false;
    }
    const ImageLayer* layer = FindLayer(id);
    if (layer == nullptr || !layer->adjustment.has_value() || !below.HasStorage() ||
        below.Format() != RasterFormat::Bgra8888)
    {
        _adjustmentEditingID.reset();
        return false;
    }
    const LayerAdjustment original = layer->adjustment.value();
    // The source is only for the histogram and sampling; it never replaces layer pixels.
    ColorEdit edit;
    edit.deferred = deferred;
    edit.kind = AdjustmentFilterKind(original.kind);
    edit.layerID = id;
    edit.original = below;
    edit.transform = CanvasTransform(_document.value());
    edit.settings = Adjust::AdjustmentSettings(original);
    edit.seed = original.Grain().seed;
    edit.openingSequence = ++_colorEditSequence;
    _colorEdit = std::move(edit);
    _adjustmentOriginal = original;
    BeginEdit(L"Edit " + std::wstring(FilterKindName(AdjustmentFilterKind(original.kind))) + L" Adjustment");
    if (!deferred)
    {
        MeasureColorEditHistogramInline();
    }
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
    ++_colorEditSequence;
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
