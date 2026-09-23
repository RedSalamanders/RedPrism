#pragma once

#include "Document.h"

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

// Image adjustments and filters (C12, Specs/Rendering/Rendering_Composition.md and Document/Document_Tools.md): the
// legacy kernels number for number on premultiplied BGRA (the legacy RGBA byte order swapped), the settings math
// the panels and the adjustment layers share, and the CPU reference the composition's GPU passes are checked against.

// The adjustment layer kind a color adjustment of the Image menu edits; nullopt for the Filter menu's filters.
[[nodiscard]] std::optional<AdjustmentKind> FilterAdjustmentKind(FilterKind kind) noexcept;
// Color adjustments: in the Image menu (and editable as adjustment layers), not under Filter.
[[nodiscard]] inline bool FilterIsImageAdjustment(FilterKind kind) noexcept
{
    return FilterAdjustmentKind(kind).has_value();
}
// The panel that edits an adjustment layer of this kind.
[[nodiscard]] FilterKind AdjustmentFilterKind(AdjustmentKind kind) noexcept;
// The legacy raw value ("Gaussian Blur", "Levels"): the panel's title and its entry's name.
[[nodiscard]] const wchar_t* FilterKindName(FilterKind kind) noexcept;

namespace Adjust
{
// ── Levels ────────────────────────────────────────────────────────────────

// A range's output (0–1) for an input (0–1), through its normalized values.
[[nodiscard]] double LevelRangeApply(const LevelRange& range, double value) noexcept;
// Individual channels, followed by the composite RGB adjustment.
[[nodiscard]] double LevelsApply(const LevelsSettings& settings, double value, LevelsChannel channel) noexcept;
[[nodiscard]] bool LevelsIsIdentity(const LevelsSettings& settings) noexcept;
[[nodiscard]] LevelRange& LevelsCurrent(LevelsSettings& settings) noexcept;
[[nodiscard]] const LevelRange& LevelsCurrent(const LevelsSettings& settings) noexcept;
// Sets the selected channel's range, normalized.
void SetLevelsCurrent(LevelsSettings& settings, const LevelRange& range) noexcept;
// Display-only vertical scaling: linear bin ratios, isolated spikes capped so a solid background cannot flatten
// the useful distribution.
[[nodiscard]] double HistogramDisplayScale(const std::array<double, 256>& bins) noexcept;
// Auto Contrast / Color / Neutral from the histogram (0.1 % clipped at each end).
[[nodiscard]] LevelsSettings AutoLevels(LevelsAuto mode, const LevelsHistogram& histogram) noexcept;
// The eyedroppers: `rgb` is the unpremultiplied original color (0–1); all three channels calibrate together.
[[nodiscard]] LevelsSettings SampledLevels(const LevelsSettings& settings, const std::array<double, 3>& rgb,
                                           LevelsSample mode) noexcept;

// ── Lookup tables (three 256-entry tables, red, green, blue, outputs 0–1) ─

struct ChannelTables
{
    std::array<float, 768> values{};
};
[[nodiscard]] ChannelTables LevelsTables(const LevelsSettings& settings) noexcept;
[[nodiscard]] ChannelTables CurvesTables(const CurvesSettings& settings) noexcept;
[[nodiscard]] ChannelTables ExposureTables(const ExposureSettings& settings) noexcept;
// Shape-preserving cubic Hermite interpolation of one channel's points (no overshoot between handles), 0–255.
[[nodiscard]] double CurveValue(const CurvesSettings& settings, double x, int channel) noexcept;
// Each channel's output (0–1) per input byte, decoded to linear light, scaled by the stops, offset, gamma
// corrected and encoded back.
[[nodiscard]] std::array<float, 256> ExposureTable(const ExposureSettings& settings) noexcept;
// 256 × 3 straight sRGB bytes, darkest first, in the order the ends apply (reversed swaps them).
[[nodiscard]] std::array<std::uint8_t, 768> GradientMapTable(const GradientMapSettings& settings) noexcept;

// ── Kernels on premultiplied BGRA (stride bytes per row) ──────────────────

// Each color unpremultiplied, looked up between its two nearest table entries, premultiplied and clamped to the
// alpha again (legacy levels_apply): alpha is kept and transparent pixels are left alone.
void ApplyTables(std::uint8_t* bgra, int width, int height, int stride, const ChannelTables& tables) noexcept;
void ApplyGradientMap(std::uint8_t* bgra, int width, int height, int stride,
                      const std::array<std::uint8_t, 768>& table) noexcept;
// Film grain: the same brightness change on all three channels, strongest in the midtones. Pixel (x, y) sits at
// (originX + (x + 0.5) × unitsPerPixel, originY + (y + 0.5) × unitsPerPixel) and its grain depends only on that
// position and the seed, so a piece of an image gets the same grain as that part of the whole.
void ApplyGrain(std::uint8_t* bgra, int width, int height, int stride, const GrainSettings& grain, double originX,
                double originY, double unitsPerPixel) noexcept;
// Noise on the colors, alpha untouched; uniform spans ±amount % of half the range, Gaussian's deviation is two
// thirds of that; monochromatic adds one value to every channel. The same seed gives the same grain.
void AddNoise(std::uint8_t* bgra, int width, int height, int stride, float amount, bool gaussian, bool monochromatic,
              std::uint32_t seed) noexcept;
// Radial lens distortion: each destination pixel samples the source bilinearly at its offset from the center scaled
// by (1 − k r²), r relative to the half diagonal; k > 0 pulls inward (barrel), k < 0 outward (pincushion).
void LensDistort(const std::uint8_t* source, std::uint8_t* destination, int width, int height, int stride,
                 double k) noexcept;
// Remove Distortion at ±100 moves the corners by this share of their distance from the center.
inline constexpr double kLensStrength = 0.35;

// ── Hue / Saturation ──────────────────────────────────────────────────────

// Degrees from `from` forward to `to`, always 0…360.
[[nodiscard]] double HueForward(double from, double to) noexcept;
// How strongly a band claims a hue: 1 inside the range, ramping linearly through each shoulder, 0 outside.
[[nodiscard]] double BandWeight(const HueBand& band, double hue) noexcept;
// A band centered on one hue, keeping the core and shoulder widths.
[[nodiscard]] HueBand BandCentered(const HueBand& band, double hue) noexcept;
// Widens the band so the hue is fully inside it, moving the nearer edge; narrows it so the hue falls outside.
void BandInclude(HueBand& band, double hue) noexcept;
void BandExclude(HueBand& band, double hue) noexcept;
// Moves one handle (0 falloff start, 1 range start, 2 range end, 3 falloff end), keeping the four in order and the
// band under a full circle; a crossing move is refused (false).
bool BandSetHandle(HueBand& band, int index, double degrees) noexcept;
[[nodiscard]] HueBand DefaultBand(ColorRange range) noexcept;

[[nodiscard]] RangeAdjustment& RangeOf(HueSaturationSettings& settings) noexcept;
[[nodiscard]] const RangeAdjustment& RangeOf(const HueSaturationSettings& settings) noexcept;
[[nodiscard]] HueBand& BandOf(HueSaturationSettings& settings) noexcept;
[[nodiscard]] const HueBand& BandOf(const HueSaturationSettings& settings) noexcept;
// Master everywhere, other ranges through their band (the selected one inverted when `invertRange`).
[[nodiscard]] double RangeWeight(const HueSaturationSettings& settings, ColorRange range, double hue) noexcept;
[[nodiscard]] bool HsvIsIdentity(const HueSaturationSettings& settings) noexcept;
// Settings with one range's values (the legacy initializer).
[[nodiscard]] HueSaturationSettings HsvSettings(double hue, double saturation = 0, double lightness = 0,
                                                bool colorize = false, ColorRange range = ColorRange::Master) noexcept;
// Photoshop's starting point when Colorize is switched on: hue 0, saturation 25.
[[nodiscard]] HueSaturationSettings ColorizeStart() noexcept;

struct HueResponse
{
    double shift = 0;
    double saturation = 0;
    double lightness = 0;
};
// How much every range shifts a given hue, sampled once per degree (0…360).
[[nodiscard]] std::array<HueResponse, 361> HueResponses(const HueSaturationSettings& settings) noexcept;
// One straight color (0–1) through the settings.
[[nodiscard]] std::array<double, 3> AdjustColor(double red, double green, double blue,
                                                const HueSaturationSettings& settings,
                                                const std::array<HueResponse, 361>* responses = nullptr) noexcept;
// The hue a spectrum swatch becomes, for the "after" bar.
[[nodiscard]] double ShiftedHue(double hue, const HueSaturationSettings& settings) noexcept;
// The color cube (33 points per axis, red fastest, RGBA floats) built from the settings.
inline constexpr int kCubeDimension = 33;
[[nodiscard]] std::vector<float> ColorCube(const HueSaturationSettings& settings);
// Straight colors through the cube, trilinearly (what CIColorCube does), premultiplied around the lookup.
void ApplyCube(std::uint8_t* bgra, int width, int height, int stride, const std::vector<float>& cube) noexcept;

// ── Straight-color blends (the PDF formulas, modes as LayerBlendMode) ─────

[[nodiscard]] std::array<double, 3> BlendStraight(const std::array<double, 3>& backdrop,
                                                  const std::array<double, 3>& source, LayerBlendMode mode) noexcept;

// ── Whole rasters ─────────────────────────────────────────────────────────

// A premultiplied BGRA copy of `image` with the adjustment applied (Grain fixed in document space by `originX`,
// `originY` and `unitsPerPixel`); empty when the settings are invalid or memory runs out. Identity settings
// return `image` itself.
[[nodiscard]] Raster ApplyAdjustment(const LayerAdjustment& adjustment, const Raster& image, double originX = 0,
                                     double originY = 0, double unitsPerPixel = 1);
// A panel's settings for an adjustment record (every kind's own from the record), and the record with its kind's
// settings taken from a panel.
[[nodiscard]] FilterSettings AdjustmentSettings(const LayerAdjustment& adjustment);
[[nodiscard]] LayerAdjustment AdjustmentWithSettings(LayerAdjustment adjustment, const FilterSettings& settings);
// The alpha-weighted histogram of an image, within `coverage` (Gray8 on the same grid) when given; an explicitly
// empty coverage counts nothing.
[[nodiscard]] LevelsHistogram Histogram(const Raster& image, const Raster* coverage) noexcept;
// Any kind on a raster: `scale` is pixels in `image` per original layer pixel (a downscaled preview blurs
// proportionally less); `seed` is Add Noise's and Grain's pattern; `coverage` (Gray8 on the same grid) is the
// selection Content-Aware Fill fills, which needs it. The Image menu's kinds run their adjustment layer's kernel
// (`ApplyAdjustment`), Grain in layer pixels. Empty on failure, or when a fill found nothing to copy from.
[[nodiscard]] Raster RunFilter(FilterKind kind, const Raster& image, const FilterSettings& settings, double scale,
                               std::uint32_t seed, const Raster* coverage = nullptr);
// Content-Aware Fill (the legacy content_fill): selected pixels (`mask` nonzero) are synthesized from the
// unselected opaque ones by coherent patch matching with a randomized search; unselected transparent pixels are
// neither matched nor changed. 1 on success, 0 when no source patch exists, -1 when memory runs out.
[[nodiscard]] int ContentFill(std::uint8_t* bgra, int width, int height, int stride, const std::uint8_t* mask,
                              int maskStride) noexcept;
// coverage × adjusted + (1 − coverage) × original, so fully selected pixels stay exact and soft edges blend.
[[nodiscard]] Raster BlendThrough(const Raster& adjusted, const Raster& original, const Raster& coverage);
// A box-filtered copy no larger than `maxSide` on its longest side (`image` itself when it already fits).
[[nodiscard]] Raster Downscaled(const Raster& image, int maxSide);
// `image` cropped to the pixels that are actually there, with the transform that keeps them in place and where the
// crop started in the source grid.
struct Trimmed
{
    Raster image;
    LayerTransform transform;
    int cropX = 0;
    int cropY = 0;
};
[[nodiscard]] Trimmed TrimmedToAlpha(const Raster& image, const LayerTransform& placed);
// The room a blur needs around the layer: about three standard deviations, or half a streak.
[[nodiscard]] double BlurMargin(FilterKind kind, const FilterSettings& settings) noexcept;
// A Gaussian-blurred copy as the Gaussian Blur filter makes it (sigma = radius × scale).
[[nodiscard]] Raster GaussianBlurred(const Raster& image, double sigma);
// An even streak of `distance` pixels along `angle` degrees (counterclockwise from horizontal on screen).
[[nodiscard]] Raster MotionBlurred(const Raster& image, double angleDegrees, double distance);
} // namespace Adjust
