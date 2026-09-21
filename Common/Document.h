#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

inline constexpr int kMaxCanvasSide = 30'000;
inline constexpr int kMaxTransformSide = 300'000;
inline constexpr double kMaxTransformOrigin = 1'000'000.0;
inline constexpr int kMaxLayerPixels = 100'000'000;
inline constexpr int kMaxMaskPixels = 100'000'000;
inline constexpr int kMaxLayerCount = 10'000;
inline constexpr int kMaxNameUtf8Bytes = 16'384;
inline constexpr int kMaxGroupDepth = 64;
inline constexpr int kMaxLiveMaskPath = 256;
inline constexpr int kMinFormatVersion = 1;
inline constexpr int kMaxFormatVersion = 7;
inline constexpr int kCurrentFormatVersion = 7;
inline constexpr int kHistoryEntryLimit = 100;
inline constexpr int kHistoryRetainedByteLimit = 256 * 1024 * 1024;

enum class DocumentStatus
{
    Ok,
    Invalid,
    UnsupportedVersion,
    TooLarge,
};

struct Guid
{
    std::array<std::uint8_t, 16> bytes{};

    [[nodiscard]] static Guid Generate() noexcept;
    [[nodiscard]] static std::optional<Guid> Parse(std::string_view text) noexcept;
    [[nodiscard]] std::string ToUtf8() const;
    [[nodiscard]] bool operator==(const Guid&) const noexcept = default;
    [[nodiscard]] bool operator<(const Guid& other) const noexcept
    {
        return bytes < other.bytes;
    }
    [[nodiscard]] std::wstring ToString() const;
};

template <> struct std::hash<Guid>
{
    [[nodiscard]] std::size_t operator()(const Guid& id) const noexcept
    {
        std::size_t hash = 2166136261u;
        for (const std::uint8_t byte : id.bytes)
        {
            hash ^= byte;
            hash *= 16777619u;
        }
        return hash;
    }
};

struct Vec2
{
    double x = 0;
    double y = 0;
    [[nodiscard]] bool operator==(const Vec2&) const noexcept = default;
};

// A 2-D affine map in the row-vector convention of CGAffineTransform: x' = a*x + c*y + tx, y' = b*x + d*y + ty.
struct Affine2
{
    double a = 1;
    double b = 0;
    double c = 0;
    double d = 1;
    double tx = 0;
    double ty = 0;
    [[nodiscard]] static Affine2 Translation(double x, double y) noexcept
    {
        return Affine2{1, 0, 0, 1, x, y};
    }
    [[nodiscard]] static Affine2 Scale(double x, double y) noexcept
    {
        return Affine2{x, 0, 0, y, 0, 0};
    }
    [[nodiscard]] static Affine2 Rotation(double radians) noexcept;
    [[nodiscard]] Vec2 Apply(Vec2 point) const noexcept
    {
        return Vec2{a * point.x + c * point.y + tx, b * point.x + d * point.y + ty};
    }
    // This map, then `other` (CGAffineTransform.concatenating).
    [[nodiscard]] Affine2 Concat(const Affine2& other) const noexcept;
    [[nodiscard]] Affine2 Inverted() const noexcept;
    [[nodiscard]] bool operator==(const Affine2&) const noexcept = default;
};

enum class RasterFormat
{
    Bgra8888,
    Gray8,
};

class Raster;
struct RasterTile;

// Immutable pixels with shared storage. A raster is either contiguous, or sparse (C10): an unchanged base placed in
// its grid plus replacement tiles, as a painted layer leaves it. Sparse rasters share every untouched tile and the
// base with the raster they came from, and assemble contiguous bytes only when a consumer asks for Data().
class Raster
{
  public:
    [[nodiscard]] static Raster Make(int width, int height, RasterFormat format);
    [[nodiscard]] static Raster Make(int width, int height, RasterFormat format, const std::uint8_t* pixels,
                                     std::size_t byteCount);
    [[nodiscard]] static Raster MakeThumbnail(const Raster& source, int maxSide = 96);
    // A sparse raster: `base` (contiguous; may be empty) with its top-left at (baseX, baseY) of the new grid, under
    // `tiles` (clipped to the grid, later tiles over earlier ones). Pixels no base or tile reaches read `fill`.
    [[nodiscard]] static Raster MakeSparse(int width, int height, RasterFormat format, const Raster& base, int baseX,
                                           int baseY, std::vector<RasterTile> tiles, std::uint8_t fill);
    // `source` (its top-left at (sourceX, sourceY) of a working grid) with `tiles` of that grid written over it,
    // cropped to the given rectangle of the grid. Older tiles are split around newer ones so the tile list stays
    // disjoint; the base and untouched tiles are shared, never copied.
    [[nodiscard]] static Raster Replacing(const Raster& source, int sourceX, int sourceY, std::vector<RasterTile> tiles,
                                          int cropX, int cropY, int cropWidth, int cropHeight, std::uint8_t fill);
    // A contiguous copy of the region (pixels outside this raster read `fill`).
    [[nodiscard]] Raster Crop(int x, int y, int width, int height, std::uint8_t fill) const;
    [[nodiscard]] bool operator==(const Raster& other) const noexcept
    {
        return _storage == other._storage;
    }
    [[nodiscard]] bool HasStorage() const noexcept
    {
        return static_cast<bool>(_storage);
    }
    [[nodiscard]] int Width() const noexcept
    {
        return _storage ? _storage->width : 0;
    }
    [[nodiscard]] int Height() const noexcept
    {
        return _storage ? _storage->height : 0;
    }
    [[nodiscard]] int Stride() const noexcept
    {
        return _storage ? _storage->stride : 0;
    }
    [[nodiscard]] RasterFormat Format() const noexcept
    {
        return _storage ? _storage->format : RasterFormat::Bgra8888;
    }
    [[nodiscard]] int Channels() const noexcept
    {
        return Format() == RasterFormat::Gray8 ? 1 : 4;
    }
    // Bytes this storage holds itself: contiguous pixels, or a sparse raster's assembled copy once it exists. The
    // base and the tiles are separate storages (ForEachStorage).
    [[nodiscard]] std::size_t ByteSize() const noexcept
    {
        return _storage ? _storage->bytes.size() : 0;
    }
    [[nodiscard]] const void* Identity() const noexcept
    {
        return _storage.get();
    }
    [[nodiscard]] int PixelCount() const noexcept
    {
        return Width() * Height();
    }
    // Contiguous pixels. A sparse raster assembles them on the first call; nullptr when that allocation fails.
    [[nodiscard]] const std::uint8_t* Data() const noexcept;
    [[nodiscard]] bool IsSparse() const noexcept
    {
        return _storage && _storage->sparse;
    }
    // A sparse raster whose contiguous bytes were assembled (mouse-up must never do this).
    [[nodiscard]] bool IsMaterialized() const noexcept
    {
        return _storage && _storage->materialized;
    }
    [[nodiscard]] Raster Base() const noexcept
    {
        return _storage ? Raster(_storage->base) : Raster();
    }
    [[nodiscard]] int BaseX() const noexcept
    {
        return _storage ? _storage->baseX : 0;
    }
    [[nodiscard]] int BaseY() const noexcept
    {
        return _storage ? _storage->baseY : 0;
    }
    [[nodiscard]] const std::vector<RasterTile>& Tiles() const noexcept;
    [[nodiscard]] std::uint8_t Fill() const noexcept
    {
        return _storage ? _storage->fill : 0;
    }
    // The region [x, x + width) × [y, y + height) of this raster written to `out` (`outStride` bytes per row),
    // without assembling a sparse raster; pixels outside it read `fill`.
    void ReadRegion(int x, int y, int width, int height, std::uint8_t* out, int outStride,
                    std::uint8_t fill) const noexcept;
    // Every storage this raster reaches: itself, then its base and tiles (retained-memory accounting).
    void ForEachStorage(const std::function<void(const Raster&)>& visit) const;

  private:
    struct Storage
    {
        int width = 0;
        int height = 0;
        int stride = 0;
        RasterFormat format = RasterFormat::Bgra8888;
        mutable std::vector<std::uint8_t> bytes;
        mutable bool materialized = true;
        bool sparse = false;
        std::uint8_t fill = 0;
        std::shared_ptr<const Storage> base;
        int baseX = 0;
        int baseY = 0;
        std::vector<RasterTile> tiles;
    };
    explicit Raster(std::shared_ptr<const Storage> storage) noexcept : _storage(std::move(storage)) {}
    static void Materialize(const Storage& storage) noexcept;
    std::shared_ptr<const Storage> _storage;

  public:
    Raster() = default;
};

// One replacement tile of a sparse raster: a window of `pixels` (contiguous, the raster's format) placed with its
// top-left at (x, y) in the grid. A window smaller than the pixels is how a split tile keeps sharing its storage.
struct RasterTile
{
    int x = 0;
    int y = 0;
    Raster pixels;
    int sourceX = 0; // window origin inside `pixels`
    int sourceY = 0;
    int width = 0; // 0: the whole of `pixels`
    int height = 0;
    [[nodiscard]] int Width() const noexcept
    {
        return width > 0 ? width : pixels.Width();
    }
    [[nodiscard]] int Height() const noexcept
    {
        return height > 0 ? height : pixels.Height();
    }
};

struct RasterAsset
{
    Raster image;
    Raster thumbnail;
    std::wstring name;
    [[nodiscard]] bool operator==(const RasterAsset& other) const noexcept
    {
        return image == other.image && thumbnail == other.thumbnail && name == other.name;
    }
};

enum class LayerSampling
{
    Nearest,
    Smooth,
    High,
};

struct LayerTransform
{
    Vec2 origin{};
    Vec2 size{1, 1};
    double rotation = 0;
    bool flipX = false;
    bool flipY = false;
    LayerSampling sampling = LayerSampling::High;

    [[nodiscard]] bool operator==(const LayerTransform&) const noexcept = default;
    [[nodiscard]] bool IsValid() const noexcept;
    [[nodiscard]] Vec2 Center() const noexcept
    {
        return Vec2{origin.x + size.x / 2.0, origin.y + size.y / 2.0};
    }
    [[nodiscard]] double Radians() const noexcept;
    // The unit square (0…1, y down) mapped where this transform places a layer; flips are not applied here.
    [[nodiscard]] Vec2 Point(Vec2 unit) const noexcept;
    [[nodiscard]] bool Contains(Vec2 point) const noexcept;
    // Corners in handle order: top-left, top-right, bottom-right, bottom-left.
    [[nodiscard]] std::array<Vec2, 4> Corners() const noexcept;
    // Width as a percentage of the `pixelSize` it places (100 % draws them 1:1).
    [[nodiscard]] double ScalePercent(Vec2 pixelSize) const noexcept;
    // Both sides set to `percent` of `pixelSize`, keeping the center, rotation and flips.
    [[nodiscard]] LayerTransform ScaledToPercent(double percent, Vec2 pixelSize) const noexcept;
    // Whole pixels and whole degrees: what dragging, scaling and rotating leave behind.
    [[nodiscard]] LayerTransform Rounded() const noexcept;
    // The unit square mapped onto the document, flips included.
    [[nodiscard]] Affine2 UnitToDocument() const noexcept;
    // A transform placing the unit square as `map` does (shear dropped), keeping this sampling.
    [[nodiscard]] LayerTransform Placing(const Affine2& map) const noexcept;
    // This placement carried along as a layer moves from `from` to `to`.
    [[nodiscard]] LayerTransform Following(const LayerTransform& from, const LayerTransform& to) const noexcept;
    // The same place on the document, whatever the sampling.
    [[nodiscard]] bool SamePlacement(const LayerTransform& other) const noexcept;
    // Mirrored across a vertical line at `axis` (horizontally) or a horizontal one.
    [[nodiscard]] LayerTransform Mirrored(bool horizontally, double axis) const noexcept;
    static constexpr std::array<Vec2, 8> kHandles{Vec2{0, 0}, Vec2{0.5, 0}, Vec2{1, 0}, Vec2{1, 0.5},
                                                  Vec2{1, 1}, Vec2{0.5, 1}, Vec2{0, 1}, Vec2{0, 0.5}};
};

enum class LayerBlendMode
{
    Normal,
    Multiply,
    Screen,
    Overlay,
    Darken,
    Lighten,
    Difference,
    ColorDodge,
    ColorBurn,
    Hue,
    Saturation,
    Color,
    Luminosity,
};

[[nodiscard]] constexpr int LayerBlendModeCount() noexcept
{
    return 14;
}

enum class AdjustmentKind
{
    Hsv,
    Levels,
    Curves,
    Exposure,
    GradientMap,
    Grain,
};

enum class LevelsChannel
{
    Rgb,
    Red,
    Green,
    Blue,
};

enum class ColorRange : std::uint8_t
{
    Master,
    Reds,
    Yellows,
    Greens,
    Cyans,
    Blues,
    Magentas,
    Count,
};

struct LevelRange
{
    double black = 0;
    double gamma = 1;
    double white = 255;
    double outputBlack = 0;
    double outputWhite = 255;
    [[nodiscard]] bool operator==(const LevelRange&) const noexcept = default;
    [[nodiscard]] LevelRange Normalized() const noexcept;
};

struct LevelsSettings
{
    LevelsChannel channel = LevelsChannel::Rgb;
    std::array<LevelRange, 4> ranges{};
    [[nodiscard]] bool operator==(const LevelsSettings&) const noexcept = default;
};

struct CurvePoint
{
    double x = 0;
    double y = 0;
    [[nodiscard]] bool operator==(const CurvePoint&) const noexcept = default;
};

struct CurvesSettings
{
    LevelsChannel channel = LevelsChannel::Rgb;
    std::array<std::vector<CurvePoint>, 4> channels{
        std::vector<CurvePoint>{{0, 0}, {255, 255}},
        std::vector<CurvePoint>{{0, 0}, {255, 255}},
        std::vector<CurvePoint>{{0, 0}, {255, 255}},
        std::vector<CurvePoint>{{0, 0}, {255, 255}},
    };
    [[nodiscard]] bool operator==(const CurvesSettings&) const noexcept = default;
    [[nodiscard]] bool IsValid() const noexcept;
};

struct AdjustmentColor
{
    double red = 0;
    double green = 0;
    double blue = 0;
    [[nodiscard]] bool operator==(const AdjustmentColor&) const noexcept = default;
    [[nodiscard]] bool IsValid() const noexcept
    {
        return std::isfinite(red) && std::isfinite(green) && std::isfinite(blue) && red >= 0 && red <= 1 &&
               green >= 0 && green <= 1 && blue >= 0 && blue <= 1;
    }
};

struct ExposureSettings
{
    double exposure = 0;
    double offset = 0;
    double gamma = 1;
    [[nodiscard]] bool operator==(const ExposureSettings&) const noexcept = default;
    [[nodiscard]] bool IsValid() const noexcept
    {
        return exposure >= -20 && exposure <= 20 && offset >= -0.5 && offset <= 0.5 && gamma >= 0.01 && gamma <= 9.99;
    }
};

struct GradientMapSettings
{
    AdjustmentColor shadows{0, 0, 0};
    AdjustmentColor highlights{1, 1, 1};
    bool reversed = false;
    [[nodiscard]] bool operator==(const GradientMapSettings&) const noexcept = default;
    [[nodiscard]] bool IsValid() const noexcept
    {
        return shadows.IsValid() && highlights.IsValid();
    }
};

struct GrainSettings
{
    double amount = 25;
    double size = 1.5;
    double roughness = 50;
    std::uint32_t seed = 0;
    [[nodiscard]] bool operator==(const GrainSettings&) const noexcept = default;
    [[nodiscard]] bool IsValid() const noexcept
    {
        return amount >= 0 && amount <= 100 && size >= 0.5 && size <= 20 && roughness >= 0 && roughness <= 100;
    }
};

struct RangeAdjustment
{
    double hue = 0;
    double saturation = 0;
    double lightness = 0;
    [[nodiscard]] bool operator==(const RangeAdjustment&) const noexcept = default;
};

struct HueBand
{
    double falloffStart = 0;
    double rangeStart = 0;
    double rangeEnd = 360;
    double falloffEnd = 360;
    [[nodiscard]] bool operator==(const HueBand&) const noexcept = default;
    [[nodiscard]] std::array<double, 4> Handles() const noexcept
    {
        return {falloffStart, rangeStart, rangeEnd, falloffEnd};
    }
};

struct HueSaturationSettings
{
    ColorRange range = ColorRange::Master;
    bool colorize = false;
    bool invertRange = false;
    std::array<RangeAdjustment, static_cast<std::size_t>(ColorRange::Count)> adjustments{};
    std::array<HueBand, static_cast<std::size_t>(ColorRange::Count)> bands = DefaultBands();
    [[nodiscard]] bool operator==(const HueSaturationSettings&) const noexcept = default;
    [[nodiscard]] static std::array<HueBand, static_cast<std::size_t>(ColorRange::Count)> DefaultBands() noexcept;
};

struct LayerAdjustment
{
    AdjustmentKind kind = AdjustmentKind::Hsv;
    double hue = 0;
    double saturation = 0;
    double lightness = 0;
    bool colorize = false;
    std::optional<HueSaturationSettings> hsvSettings;
    LevelsSettings levels{};
    CurvesSettings curves{};
    std::optional<ExposureSettings> exposureSettings;
    std::optional<GradientMapSettings> gradientMapSettings;
    std::optional<GrainSettings> grainSettings;
    [[nodiscard]] bool operator==(const LayerAdjustment&) const noexcept = default;
    [[nodiscard]] HueSaturationSettings ResolvedHsv() const noexcept;
    [[nodiscard]] ExposureSettings Exposure() const noexcept
    {
        return exposureSettings.value_or(ExposureSettings{});
    }
    [[nodiscard]] GradientMapSettings GradientMap() const noexcept
    {
        return gradientMapSettings.value_or(GradientMapSettings{});
    }
    [[nodiscard]] GrainSettings Grain() const noexcept
    {
        return grainSettings.value_or(GrainSettings{});
    }
    [[nodiscard]] bool IsValid() const noexcept;
};

enum class ShapeKind
{
    Rectangle,
    Ellipse,
};

[[nodiscard]] const char* LayerBlendModeName(LayerBlendMode mode) noexcept;
[[nodiscard]] std::optional<LayerBlendMode> LayerBlendModeFromName(std::string_view name) noexcept;
[[nodiscard]] const char* LayerSamplingName(LayerSampling sampling) noexcept;
[[nodiscard]] std::optional<LayerSampling> LayerSamplingFromName(std::string_view name) noexcept;
[[nodiscard]] const char* AdjustmentKindName(AdjustmentKind kind) noexcept;
[[nodiscard]] std::optional<AdjustmentKind> AdjustmentKindFromName(std::string_view name) noexcept;
[[nodiscard]] const char* LevelsChannelName(LevelsChannel channel) noexcept;
[[nodiscard]] std::optional<LevelsChannel> LevelsChannelFromName(std::string_view name) noexcept;
[[nodiscard]] const char* ColorRangeName(ColorRange range) noexcept;
[[nodiscard]] std::optional<ColorRange> ColorRangeFromName(std::string_view name) noexcept;
[[nodiscard]] const char* ShapeKindName(ShapeKind kind) noexcept;
[[nodiscard]] std::optional<ShapeKind> ShapeKindFromName(std::string_view name) noexcept;

struct LayerShapeStyle
{
    ShapeKind kind = ShapeKind::Rectangle;
    double red = 0;
    double green = 0;
    double blue = 0;
    double cornerRadius = 0;
    [[nodiscard]] bool operator==(const LayerShapeStyle&) const noexcept = default;
};

struct LayerMask
{
    RasterAsset asset;
    bool enabled = true;
    std::optional<LayerTransform> placement;
    bool linked = true;
    [[nodiscard]] bool operator==(const LayerMask& other) const noexcept
    {
        return asset.image == other.asset.image && enabled == other.enabled && placement == other.placement &&
               linked == other.linked;
    }
    [[nodiscard]] static bool IsValid(const Raster& image) noexcept;
};

// Where a mask sits after its layer moves from `from` to `to`: a linked placement follows, an unlinked mask stays,
// a uniform 1 x 1 mask and a mask that lands exactly on its layer report nullopt.
[[nodiscard]] std::optional<LayerTransform> MaskPlacementMoving(const LayerMask& mask, const LayerTransform& from,
                                                                const LayerTransform& to) noexcept;

// Selection (C9, Specs/Document/Document_Tools.md): closed loops in document pixels under the nonzero winding rule,
// Clipper2-normalized (outer loops and holes) except a wand or mask trace, whose pixel-edge loops already are.
using SelectionLoop = std::vector<Vec2>;

struct SelectionBounds
{
    double minX = 0;
    double minY = 0;
    double maxX = 0;
    double maxY = 0;
    [[nodiscard]] double Width() const noexcept
    {
        return maxX - minX;
    }
    [[nodiscard]] double Height() const noexcept
    {
        return maxY - minY;
    }
    [[nodiscard]] bool operator==(const SelectionBounds&) const noexcept = default;
};

// A document-space selection outline, clipped to the canvas when drawn (not when moved). nullopt on the document
// means no selection; a selection with no area is an explicit empty selection, which later edits must treat as
// "touch nothing", never as "touch everything".
struct DocumentSelection
{
    bool antialiased = true;
    std::vector<SelectionLoop> loops;
    [[nodiscard]] bool operator==(const DocumentSelection&) const noexcept = default;
    [[nodiscard]] std::optional<SelectionBounds> Bounds() const noexcept
    {
        std::optional<SelectionBounds> bounds;
        for (const SelectionLoop& loop : loops)
        {
            for (const Vec2& point : loop)
            {
                if (!std::isfinite(point.x) || !std::isfinite(point.y))
                {
                    continue;
                }
                if (!bounds.has_value())
                {
                    bounds = SelectionBounds{point.x, point.y, point.x, point.y};
                    continue;
                }
                bounds->minX = std::min(bounds->minX, point.x);
                bounds->minY = std::min(bounds->minY, point.y);
                bounds->maxX = std::max(bounds->maxX, point.x);
                bounds->maxY = std::max(bounds->maxY, point.y);
            }
        }
        return bounds;
    }
    [[nodiscard]] bool IsEmpty() const noexcept
    {
        const std::optional<SelectionBounds> bounds = Bounds();
        return !bounds.has_value() || !(bounds->Width() > 0.0) || !(bounds->Height() > 0.0);
    }
};

enum class LassoKind : std::uint8_t
{
    Freehand,
    Polygonal,
    // The Marquee's outlines; not offered in the Lasso's Freehand/Polygonal choice.
    Rectangle,
    Ellipse,
};

enum class SelectionMode : std::uint8_t
{
    New,
    Add,
    Subtract,
};

// A lasso outline being drawn, in document pixels. `cursor` is the polygonal lasso's rubber-band end point;
// `anchor` the Marquee's starting corner, in whole pixels.
struct LassoDraft
{
    std::vector<Vec2> points;
    std::optional<Vec2> cursor;
    SelectionMode mode = SelectionMode::New;
    LassoKind kind = LassoKind::Freehand;
    std::optional<Vec2> anchor;
};

enum class WandSampleSize : std::uint8_t
{
    Point,
    ThreeByThree,
    FiveByFive,
};

// The Magic Wand's header settings.
struct WandSettings
{
    // How far (0–255) each channel may differ from the sampled color and still be selected.
    int tolerance = 32;
    WandSampleSize sampleSize = WandSampleSize::Point;
    // Only similar pixels connected to the clicked one, rather than every similar pixel.
    bool contiguous = true;
    // Read the visible composite rather than just the active layer.
    bool sampleAllLayers = false;
    // Pixels either side of the click that are averaged into the color to match.
    [[nodiscard]] int Radius() const noexcept
    {
        return static_cast<int>(sampleSize);
    }
    [[nodiscard]] bool operator==(const WandSettings&) const noexcept = default;
};

enum class WandFailure : std::uint8_t
{
    None,
    TooDetailed,
    Memory,
};

struct ImageLayer
{
    Guid id;
    std::optional<RasterAsset> asset;
    LayerTransform transform{};
    std::wstring name;
    bool visible = true;
    std::optional<Guid> parentID;
    bool isGroup = false;
    double opacity = 1;
    LayerBlendMode blendMode = LayerBlendMode::Normal;
    std::optional<Guid> maskSourceID;
    std::optional<LayerMask> mask;
    std::optional<LayerAdjustment> adjustment;
    std::optional<LayerShapeStyle> shape;
    // The raster the shape drew (C11): while `asset` still holds it the layer is a live shape that redraws at a new
    // size; once anything else changes the pixels it is plain pixels. Not part of equality.
    Raster shapeImage;

    [[nodiscard]] bool operator==(const ImageLayer& other) const noexcept;
    static ImageLayer Blank(std::wstring name, Vec2 size);
};

struct CanvasDocument
{
    Guid id;
    int width = 1;
    int height = 1;
    double resolution = 72;
    std::vector<ImageLayer> layers;
    std::optional<DocumentSelection> selection;
    [[nodiscard]] bool operator==(const CanvasDocument&) const noexcept = default;
};

// Several layers transformed together: the upright box around them when the edit began (what the draft edits) and
// each one's transform then.
struct SnapTargets;

struct TransformGroup
{
    LayerTransform box;
    std::vector<std::pair<Guid, LayerTransform>> originals;
};

// Ctrl+T with a selection (C9b): the selected pixels float on a temporary layer, edited with the normal transform
// handles, then merge back into their layer as one `Transform Selection` entry; Escape restores exactly.
struct FloatingTransform
{
    Guid sourceID;
    std::shared_ptr<CanvasDocument> before;
    std::optional<Guid> beforeActive;
    LayerTransform original;
    int pixelWidth = 0;
    int pixelHeight = 0;
};

// A pending transform: the draft the handles and inspector edit. `persistent` edits wait for Apply/Cancel; others
// commit when the drag ends. `corners` set means a free distortion; `mask` an unlinked mask placed alone.
struct TransformEdit
{
    Guid layerID;
    LayerTransform draft;
    bool persistent = true;
    std::optional<std::array<Vec2, 4>> corners;
    bool mask = false;
    std::optional<TransformGroup> group;
    // Set when transforming selected pixels (Ctrl+T with a selection) rather than a layer.
    std::optional<FloatingTransform> floating;
};

// Document positions a move has just snapped to, drawn as guides while it lasts.
struct SnapGuides
{
    std::vector<double> xs;
    std::vector<double> ys;
};

// Brush (C10, Specs/Rendering/Rendering_Brush.md).
// Spot Healing's kind of repair (C11, Specs/Document/Document_Tools.md).
enum class SpotHealingMode : std::uint8_t
{
    ContentAware,
    CreateTexture,
    ProximityMatch,
};

// The Smear tool's modes: Liquify pushes pixels, Blur softens, Smudge drags color along.
enum class SmearMode : std::uint8_t
{
    Liquify,
    Blur,
    Smudge,
};

// What a brush stroke does to the pixels under the tip.
enum class BrushKind : std::uint8_t
{
    Paint,
    Erase,
    Heal,
    Clone,
    Blur,
};

// Clone Stamp's options-bar settings.
struct CloneSettings
{
    // The source moves with the brush and keeps its offset between strokes; off, every stroke starts again at the
    // source point.
    bool aligned = true;
    // Copy from every visible layer as shown rather than the active layer alone.
    bool sampleAllLayers = false;
    [[nodiscard]] bool operator==(const CloneSettings&) const noexcept = default;
};

// Linear runs from start to end; radial is centered on the start with the end on its rim.
enum class GradientShape : std::uint8_t
{
    Linear,
    Radial,
};

enum class GradientStyle : std::uint8_t
{
    ForegroundToBackground,
    ForegroundToTransparent,
};

struct GradientSettings
{
    GradientShape shape = GradientShape::Linear;
    GradientStyle style = GradientStyle::ForegroundToTransparent;
    bool reversed = false;
    double opacity = 1;
    [[nodiscard]] bool operator==(const GradientSettings&) const noexcept = default;
};

struct BrushSettings
{
    double diameter = 40; // document pixels, 1–2000
    double hardness = 1;  // 0 (soft) … 1 (hard)
    double red = 0;       // paint color, straight 0…1
    double green = 0;
    double blue = 0;
    // Caps the whole stroke, as in Photoshop: overlapping dabs never exceed it (0.01–1).
    double opacity = 1;
    // Erase: the stroke clears the layer's pixels instead of painting color on them.
    bool erasing = false;
    // Spot Healing uses nearby source pixels instead of the foreground color, once the stroke ends.
    bool healing = false;
    SpotHealingMode healingMode = SpotHealingMode::ContentAware;
    [[nodiscard]] bool operator==(const BrushSettings&) const noexcept = default;
    [[nodiscard]] bool IsValid() const noexcept;
    // Soft-tip deposition rate shared by the continuous integral and the dab fallback.
    [[nodiscard]] static double SpacingFraction(double hardness) noexcept
    {
        return hardness >= 1.0 ? 0.015 : 0.025;
    }
};

enum class BrushToolMode : std::uint8_t
{
    Paint,
    Erase,
};

class BrushStroke;
struct GridRect;
class BrushCoverageEngine;
class WarpStroke;

// An uncommitted gradient on one layer or mask (C11). Endpoints are document pixels; the raster preview lives in the
// stroke's tiles and never touches the document until commit.
struct GradientEdit
{
    std::shared_ptr<BrushStroke> raster;
    Vec2 start;
    Vec2 end;
    [[nodiscard]] bool HasLine() const noexcept
    {
        return std::hypot(end.x - start.x, end.y - start.y) >= 0.5;
    }
};

// A shape being dragged out with the Shape tool, in whole document pixels.
struct ShapeDraft
{
    ShapeKind kind = ShapeKind::Rectangle;
    Vec2 anchor;
    SelectionBounds rect;
    // Document pixels, fixed when the drag starts; rectangles only.
    double cornerRadius = 0;
    [[nodiscard]] bool operator==(const ShapeDraft&) const noexcept = default;
};

// The shape this layer still is: nullopt once its pixels were edited some other way.
[[nodiscard]] std::optional<LayerShapeStyle> LiveShape(const ImageLayer& layer) noexcept;

// Fill with the foreground or background color (C9b).
enum class FillSource : std::uint8_t
{
    Foreground,
    Background,
};

// Selected pixels being dragged (C9b): the lifted raster plus the outline it started from.
struct PixelMove
{
    std::shared_ptr<BrushStroke> raster;
    DocumentSelection origin;
    bool duplicate = false;
    Vec2 offset;
};

// Pixels copied from the canvas, with where they came from so Paste can put them back in place.
struct PixelClipboard
{
    Raster image;
    Vec2 origin;
};

// ── Color adjustments and filters (C12) ───────────────────────────────────

// Filters from the Filter and Image menus. Each runs on the active image layer, inside the selection if there is
// one, with a live preview and one undo step on OK. Remove Background and Content-Aware Fill are C13.
enum class FilterKind : std::uint8_t
{
    GaussianBlur,
    MotionBlur,
    AddNoise,
    LensCorrection,
    ContentAwareFill, // C13: the selection filled from its surroundings (automatic, no settings)
    Curves,
    Exposure,
    GradientMap,
    Grain,
};

// Every filter's settings; each filter reads only its own.
struct FilterSettings
{
    double radius = 1;          // Gaussian Blur: the blur's standard deviation in layer pixels, 0.1–250
    double angle = 0;           // Motion Blur: degrees, counterclockwise from horizontal as in Photoshop, −90–90
    double distance = 10;       // Motion Blur: streak length in layer pixels, 1–2000
    double amount = 10;         // Add Noise: Photoshop's percentage, 0.1–400
    bool gaussian = false;      // Add Noise: Gaussian (more speckled) instead of Uniform
    bool monochromatic = false; // Add Noise: the same amount on every channel
    double distortion = 0;      // Lens Correction: Remove Distortion −100–100, positive straightens barrel distortion
    CurvesSettings curves{};
    ExposureSettings exposure{};
    GradientMapSettings gradientMap{};
    GrainSettings grain{};
    [[nodiscard]] bool operator==(const FilterSettings&) const noexcept = default;
    [[nodiscard]] FilterSettings Normalized() const noexcept;
};

enum class LevelsSample : std::uint8_t
{
    Black,
    Gray,
    White,
};

enum class LevelsAuto : std::uint8_t
{
    Contrast,
    Color,
    Neutral, // Color + neutral midtones
};

// Which eyedropper is armed while the Hue/Saturation panel is open.
enum class HueSampleMode : std::uint8_t
{
    Replace,
    Add,
    Remove,
};

// RGB (the mean of the three channel histograms) followed by red, green and blue; alpha-weighted.
using LevelsHistogram = std::array<std::array<double, 256>, 4>;

// A targeted-adjustment drag in progress: the range it picked and the values it started from.
struct HueTargetDrag
{
    ColorRange range = ColorRange::Reds;
    double hue = 0;
    double saturation = 0;
};

// One open filter: the layer's pixels it started from, the grid grown for a blur to spread into, the copy the
// preview renders from and what the canvas shows meanwhile (`previewRaster`; empty shows the layer's own pixels).
struct FilterEdit
{
    FilterKind kind = FilterKind::GaussianBlur;
    Guid layerID;
    Raster original;
    LayerTransform transform;
    std::optional<DocumentSelection> selection;
    FilterSettings settings;
    bool preview = true;
    std::uint32_t seed = 0; // Add Noise's and Grain's pattern, fixed while the panel is open
    Raster grownImage;      // the pixels padded out for a blur (empty when not grown)
    LayerTransform grownTransform;
    double grownMargin = 0;
    int grownOffsetX = 0; // where the original grid sits in the grown one
    int grownOffsetY = 0;
    Raster previewSource;    // the source no larger than the preview limit
    double previewScale = 1; // pixels in previewSource per source pixel
    Raster previewRaster;
    LayerTransform previewTransform;
    // An automatic filter that found nothing to work from (Content-Aware Fill without opaque surroundings): the
    // panel says so and OK waits.
    bool previewFailed = false;
    [[nodiscard]] const Raster& Source() const noexcept
    {
        return grownImage.HasStorage() ? grownImage : original;
    }
    [[nodiscard]] const LayerTransform& Placement() const noexcept
    {
        return grownImage.HasStorage() ? grownTransform : transform;
    }
};

// One open Levels dialog: the histogram of the original (alpha and selection weighted) and the preview.
struct LevelsEdit
{
    Guid layerID;
    Raster original;
    LayerTransform transform;
    std::optional<DocumentSelection> selection;
    LevelsSettings settings;
    bool preview = true;
    std::optional<LevelsSample> sampleMode;
    LevelsHistogram histogram{};
    bool histogramReady = false;
    Raster previewSource;
    Raster previewRaster;
};

// One open Hue/Saturation dialog, with its eyedroppers and the targeted-adjustment drag.
struct HueSaturationEdit
{
    Guid layerID;
    Raster original;
    LayerTransform transform;
    std::optional<DocumentSelection> selection;
    HueSaturationSettings settings;
    bool preview = true;
    Raster previewSource;
    Raster previewRaster;
    std::optional<HueSampleMode> sampleMode;
    bool targeting = false;
    std::optional<HueTargetDrag> targetDrag;
};

// What the last brush commit installed: the raster and the grid rectangle it covers (for the host's live preview).
struct BrushCommitInfo
{
    Raster image;
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    bool mask = false;
};

struct HierarchyEntry
{
    Guid id;
    std::wstring name;
    int depth = 0;
    bool visible = true;
    bool isGroup = false;
    bool collapsed = false;
};

// What a mask shows beyond its pixels once placed apart from its layer: 1 (reveal) or 0 (hide), whichever most
// of its edge is, read from the thumbnail (legacy LayerMask.background). A missing thumbnail reveals.
[[nodiscard]] double MaskEdgeBackground(const Raster& thumbnail) noexcept;

// Where a mask's pixels sit on the document: its own placement, else the layer's transform.
[[nodiscard]] inline LayerTransform MaskTransform(const ImageLayer& layer) noexcept
{
    if (layer.mask.has_value() && layer.mask.value().placement.has_value())
    {
        return layer.mask.value().placement.value();
    }
    return layer.transform;
}

[[nodiscard]] std::wstring TrimWide(std::wstring_view text);
[[nodiscard]] bool NameUtf8LengthOk(std::wstring_view name) noexcept;
[[nodiscard]] DocumentStatus ValidateHierarchy(const std::vector<ImageLayer>& layers);
[[nodiscard]] DocumentStatus ValidateLiveMaskGraph(const std::vector<ImageLayer>& layers);
[[nodiscard]] DocumentStatus ValidateDocument(const CanvasDocument& document,
                                              int formatVersion = kCurrentFormatVersion);
[[nodiscard]] std::vector<HierarchyEntry> HierarchyEntries(const std::vector<ImageLayer>& layers, bool topFirst = false,
                                                           const std::unordered_set<Guid>& collapsed = {});
[[nodiscard]] std::vector<Guid> RenderLayerIds(const CanvasDocument& document);
// A layer shown: it and every folder above it are visible.
[[nodiscard]] bool EffectivelyVisible(const CanvasDocument& document, const Guid& id) noexcept;
[[nodiscard]] std::unordered_set<Guid> DescendantIds(const std::vector<ImageLayer>& layers, const Guid& id);

class DocumentHistory
{
  public:
    struct Snapshot
    {
        std::optional<CanvasDocument> document;
        std::optional<Guid> activeLayerID;
        Guid revision;
    };

    explicit DocumentHistory(int entryLimit = kHistoryEntryLimit,
                             int retainedByteLimit = kHistoryRetainedByteLimit) noexcept;

    void Begin(std::wstring name, const std::optional<CanvasDocument>& document, const std::optional<Guid>& selection);
    void End(const std::optional<CanvasDocument>& document, const std::optional<Guid>& selection);
    [[nodiscard]] std::optional<Snapshot> Undo();
    [[nodiscard]] std::optional<Snapshot> Redo();
    void MarkSaved() noexcept;
    void Reset();

    [[nodiscard]] bool CanUndo() const noexcept
    {
        return _depth == 0 && !_past.empty();
    }
    [[nodiscard]] bool CanRedo() const noexcept
    {
        return _depth == 0 && !_future.empty();
    }
    [[nodiscard]] const std::wstring& UndoName() const noexcept;
    [[nodiscard]] const std::wstring& RedoName() const noexcept;
    [[nodiscard]] bool IsModified() const noexcept
    {
        return _revision != _savedRevision;
    }
    [[nodiscard]] int UndoCount() const noexcept
    {
        return static_cast<int>(_past.size());
    }
    [[nodiscard]] std::size_t RetainedBytes(const std::optional<CanvasDocument>& current) const;
    [[nodiscard]] int EntryLimit() const noexcept
    {
        return _entryLimit;
    }
    [[nodiscard]] int RetainedByteLimit() const noexcept
    {
        return _retainedByteLimit;
    }
    [[nodiscard]] Guid CurrentRevision() const noexcept
    {
        return _revision;
    }

  private:
    struct Entry
    {
        std::wstring name;
        Snapshot before;
        Snapshot after;
    };

    void Trim(const std::optional<CanvasDocument>& current);

    std::vector<Entry> _past;
    std::vector<Entry> _future;
    Guid _revision = Guid::Generate();
    Guid _savedRevision;
    std::optional<Snapshot> _pending;
    std::wstring _pendingName{L"Edit"};
    int _depth = 0;
    int _entryLimit = kHistoryEntryLimit;
    int _retainedByteLimit = kHistoryRetainedByteLimit;
};

class DocumentSession
{
  public:
    DocumentHistory history;

    void CreateDocument(int width, int height, bool emptyLayer = false);
    void AddBlankLayer();
    void AddGroup();
    void RenameLayer(const Guid& id, std::wstring_view name);
    void ToggleVisibility(const Guid& id);
    [[nodiscard]] bool PlaceLayer(const Guid& id, const std::optional<Guid>& parent,
                                  const std::optional<Guid>& above = std::nullopt, bool atBottom = false);
    void MoveActiveLayerOutOfGroup();
    void DeleteLayer(const Guid& id);
    void SetOpacity(double opacity);
    void SetBlendMode(LayerBlendMode mode);
    [[nodiscard]] bool LinkMask(const Guid& source, const Guid& target);
    [[nodiscard]] bool CanLinkMask(const Guid& source, const Guid& target) const;
    void BeginEdit(std::wstring name);
    void EndEdit();
    void Undo();
    void Redo();

    [[nodiscard]] const std::optional<CanvasDocument>& Document() const noexcept
    {
        return _document;
    }
    [[nodiscard]] std::optional<CanvasDocument>& Document() noexcept
    {
        return _document;
    }
    [[nodiscard]] const std::optional<Guid>& ActiveLayerID() const noexcept
    {
        return _activeLayerID;
    }
    void SelectLayer(const std::optional<Guid>& id);
    [[nodiscard]] const ImageLayer* ActiveLayer() const noexcept;
    ImageLayer* ActiveLayer() noexcept;
    [[nodiscard]] std::unordered_set<Guid>& CollapsedGroupIDs() noexcept
    {
        return _collapsed;
    }
    [[nodiscard]] std::vector<HierarchyEntry> LayerRows() const;
    [[nodiscard]] bool CanPlaceLayer(const Guid& id, const std::optional<Guid>& parent) const;

    // Transform editing (C8, Specs/Document/Document_Tools.md). A pending edit holds a draft; the document changes
    // only on commit, as one history entry. Blank layers (no pixels), hidden layers and folders alone cannot
    // transform; a selected folder transforms its visible pixel layers as a group.
    [[nodiscard]] bool CanTransform() const noexcept;
    [[nodiscard]] bool TransformsAsGroup() const noexcept;
    [[nodiscard]] std::vector<Guid> GroupTransformMemberIDs() const;
    [[nodiscard]] std::optional<LayerTransform> GroupTransformBox() const;
    [[nodiscard]] const std::optional<TransformEdit>& PendingTransformEdit() const noexcept
    {
        return _transformEdit;
    }
    void BeginTransform(bool persistent = true);
    void PreviewTransform(const LayerTransform& value);
    void BeginDistort();
    void PreviewCorners(const std::array<Vec2, 4>& corners);
    void BeginDuplicateTransform();
    void CommitTransform();
    void CancelTransform();
    // Arrow keys: a pending edit moves its draft; without one the move commits at once.
    void NudgeLayer(double dx, double dy);
    // Where a layer's handles sit: the pending draft (the layer's or its mask's), else the layer.
    [[nodiscard]] LayerTransform EditedTransform(const ImageLayer& layer) const;
    // A layer's transform under the pending edit; nullopt when the edit does not move it.
    [[nodiscard]] std::optional<LayerTransform> PendingTransform(const ImageLayer& layer) const;
    [[nodiscard]] LayerTransform DisplayedTransform(const ImageLayer& layer) const;
    [[nodiscard]] std::optional<LayerTransform> DisplayedMaskPlacement(const ImageLayer& layer) const;
    // Pixels the transform places, what 100 % scale draws 1:1; nullopt for a layer without pixels.
    [[nodiscard]] std::optional<Vec2> TransformPixelSize() const;
    [[nodiscard]] bool TransformTargetsMask() const noexcept;
    // Snap targets: canvas edges and center plus every other visible pixel layer's bounds and center.
    [[nodiscard]] SnapTargets TransformSnapTargets(const std::unordered_set<Guid>& excluding) const;
    // `draft` nudged so the layer lines up with a nearby target within `tolerance` document pixels; the guides it
    // met stay in LastSnapGuides until cleared.
    [[nodiscard]] LayerTransform SnappedMove(const LayerTransform& draft, const std::unordered_set<Guid>& moving,
                                             double tolerance);
    [[nodiscard]] const SnapGuides& LastSnapGuides() const noexcept
    {
        return _snapGuides;
    }
    void ClearSnapGuides() noexcept
    {
        _snapGuides = {};
    }
    void FlipLayers(bool horizontally);
    void FlipCanvas(bool horizontally);
    void DuplicateActiveLayer();
    void SelectLayerTarget(const Guid& id, bool mask);
    [[nodiscard]] bool IsMaskSelected() const noexcept
    {
        return _maskSelected;
    }
    void ToggleMaskLink(const Guid& id);
    // A uniform mask on the active layer (`Add Reveal-All Mask` / `Add Hide-All Mask`), then targeted.
    void AddLayerMask(bool revealing = true);
    // Enables or disables a layer's mask (`Enable Layer Mask` / `Disable Layer Mask`).
    void ToggleLayerMask(const Guid& id);
    // The document as the canvas draws it: the pending transform, mask placement and distortion (at preview size)
    // applied to a cached copy that shares every raster with the real document.
    [[nodiscard]] const CanvasDocument* DisplayDocument() const;
    bool locksTransformRatio = true;
    bool transformAutoSelect = false;
    bool showsTransformControls = true;

    // Selection (C9, Specs/Document/Document_Tools.md). Outlines live in the document, so history covers them;
    // drafts, the outline-move origin and the header settings are session state.
    [[nodiscard]] bool CanEditSelection() const noexcept;
    [[nodiscard]] const std::optional<DocumentSelection>& Selection() const noexcept;
    // Shift adds, Alt (with or without Shift) subtracts; otherwise the header choice.
    [[nodiscard]] SelectionMode SelectionModeFor(bool shift, bool option) const noexcept;
    // The mode the cursor advertises: an outline in progress keeps its starting mode.
    [[nodiscard]] SelectionMode LassoCursorMode(bool shift, bool option) const noexcept;
    // What the header highlights: the same rule, using the tracked held keys.
    [[nodiscard]] SelectionMode DisplayedSelectionMode() const noexcept;
    // True when the displayed mode changed.
    bool UpdateHeldSelectionKeys(bool shift, bool option) noexcept;
    [[nodiscard]] const std::optional<LassoDraft>& PendingLasso() const noexcept
    {
        return _lassoDraft;
    }
    // The Marquee starts at a whole-pixel anchor in the current marquee kind; the Lasso in the current lasso kind.
    void BeginMarquee(Vec2 point, SelectionMode mode);
    void BeginLasso(Vec2 point, SelectionMode mode);
    // Marquee drag snapped to whole pixels: `square` evens the sides, `fromCenter` grows around the anchor.
    void DragMarquee(Vec2 point, bool square, bool fromCenter);
    // Adds an outline point; points closer than a quarter pixel are skipped.
    void ExtendLasso(Vec2 point);
    void MoveLassoCursor(std::optional<Vec2> point) noexcept;
    void RemoveLastLassoPoint();
    void CancelLasso() noexcept;
    // Closes the outline and combines it with the selection; a click enclosing nothing deselects in New mode.
    void FinishLasso();
    void ToggleMarqueeKind() noexcept;
    void ToggleLassoKind() noexcept;
    // `shape` clipped to the canvas, combined by `mode` (subtracting from no selection changes nothing), one entry.
    void ApplySelection(const std::vector<SelectionLoop>& shape, SelectionMode mode, std::wstring name);
    // One history entry when the value differs.
    void SetSelection(std::optional<DocumentSelection> value, std::wstring name);
    // True where dragging in New mode would move the selection outline.
    [[nodiscard]] bool CanMoveSelection(Vec2 point) const;
    // Moves the outline only (never pixels); the whole drag is one `Move Selection` entry.
    [[nodiscard]] bool BeginSelectionMove();
    // Whole-pixel offsets from the drag's start; the outline is not re-clipped, so it can leave and come back.
    void MoveSelection(double dx, double dy);
    void EndSelectionMove();
    [[nodiscard]] bool IsMovingSelection() const noexcept
    {
        return _selectionMoveOrigin.has_value();
    }
    // Arrow keys: 1 px, or 10 with Shift; each press is one entry.
    void NudgeSelection(double dx, double dy);
    // Expand / Contract need a non-empty selection and no outline in progress.
    [[nodiscard]] bool CanModifySelection() const noexcept;
    void ExpandSelection(int amount);
    void ContractSelection(int amount);
    void SelectAll();
    void Deselect();
    void InvertSelection();
    // A mask's black (hidden) areas, or a layer's pixels at least 50 % opaque, become the selection; false when
    // there is nothing to trace.
    bool LoadMaskSelection(const Guid& id, SelectionMode mode = SelectionMode::New);
    bool LoadLayerSelection(const Guid& id, SelectionMode mode = SelectionMode::New);
    // Selects pixels similar to the one at `point`, read from `composite` (the visible canvas at document size,
    // required when `wandSettings.sampleAllLayers`) or the active layer's own pixels as placed. One entry.
    WandFailure MagicWand(Vec2 point, SelectionMode mode, const Raster* composite = nullptr);
    // What the wand reads at document size: the composite, or the active layer's pixels without its mask; a
    // folder or blank layer reads as transparent.
    [[nodiscard]] Raster WandSample(const Raster* composite) const;
    LassoKind lassoKind = LassoKind::Freehand;
    LassoKind marqueeKind = LassoKind::Rectangle;
    SelectionMode selectionModeChoice = SelectionMode::New;
    std::optional<SelectionMode> heldSelectionMode;
    bool selectionAntialiased = true;
    WandSettings wandSettings;
    int selectionExpandAmount = 1;
    int selectionContractAmount = 1;
    static constexpr int kSelectionModifyLimit = 500;

    // Brush and eraser (C10, Specs/Rendering/Rendering_Brush.md). A stroke edits the active layer's pixels or
    // its mask in 256-pixel tiles; the coverage engine (GPU compute in the app, CPU here) computes the tip.
    [[nodiscard]] bool CanPaint() const noexcept;
    // Coverage engine for later strokes; nullptr means the CPU engine.
    void SetBrushEngine(BrushCoverageEngine* engine) noexcept
    {
        _brushEngine = engine;
    }
    // Starts a stroke at `point` (document pixels): the foreground color, or `erase`; on a mask, black or white
    // per `maskPaintWhite`. False when painting cannot start (or the stroke exceeded the pixel budget).
    bool BeginBrush(Vec2 point, bool erase = false);
    // The retouch brushes (C11): Spot Healing and Clone Stamp rework image pixels and never start on a mask; Blur
    // paints a softened copy of the layer or its mask through the tip. Clone Stamp needs a source
    // (`SetCloneSource`) and, with `cloneSettings.sampleAllLayers`, the host's composite of the visible canvas at
    // document size.
    bool BeginBrush(Vec2 point, BrushKind kind, const Raster* composite = nullptr);
    // Alt-click: where Clone Stamp copies from. A new source starts a new alignment.
    void SetCloneSource(Vec2 point) noexcept;
    // The whole-pixel offset a stroke starting at `point` would copy with: aligned strokes keep the first
    // stroke's; otherwise it runs from the brush to the source. Nullopt without a source.
    [[nodiscard]] std::optional<Vec2> CloneStrokeOffset(Vec2 point) const noexcept;
    // Where the source sits for a brush at `point`, for the canvas's crosshair: the source itself until a stroke
    // fixes the offset.
    [[nodiscard]] std::optional<Vec2> CloneSamplePoint(Vec2 point) const noexcept;
    // What a Clone Stamp stroke copies from, at document size: the active layer's own pixels as placed, or the
    // composite. Empty when there is nothing to copy.
    [[nodiscard]] Raster CloneSample(const Raster* composite) const;
    // What a Blur stroke paints: the active layer (or its mask) as shown, softened by an amount that follows the
    // brush size. Empty when there is nothing to blur.
    [[nodiscard]] Raster BlurSample(bool mask) const;
    // Smudge and Liquify (the Smear tool's warp modes) work on a document-size copy of the active layer's pixels,
    // never on a mask; `FinishWarp` paints the result into the layer along the stroke as one entry (`Liquify`,
    // `Smudge`).
    bool BeginWarp(Vec2 point);
    void ContinueWarp(Vec2 point);
    void FinishWarp();
    void CancelWarp() noexcept;
    [[nodiscard]] const WarpStroke* PendingWarp() const noexcept
    {
        return _warp.get();
    }
    [[nodiscard]] WarpStroke* MutableWarp() noexcept
    {
        return _warp.get();
    }
    // Clone Stamp (family 1) and Smear (family 2) keep their own size, hardness and opacity; the other brushes
    // share family 0. Called when the tool changes families; the parked tips start soft at 40 px.
    void SwitchBrushTipFamily(int from, int to) noexcept;

    // Gradient (C11): a pending raster edit on the active layer or its mask, shown through the stroke preview until
    // Apply, Enter, or a tool, layer or target switch commits it (`Gradient`, `Gradient Mask`); Undo, Escape or
    // Cancel drop it. Dragging a new line on the same target replaces the pending one.
    bool BeginGradient(Vec2 point);
    void MoveGradient(std::optional<Vec2> start, std::optional<Vec2> end);
    // Re-renders the pending gradient from the current endpoints, settings and palette.
    void RefreshGradient();
    // Straight red, green, blue, alpha of both ends after the style and Reverse; a mask takes the first component.
    [[nodiscard]] std::array<std::array<double, 4>, 2> GradientColors() const noexcept;
    // Ends a drag; a click without a line leaves nothing pending.
    void EndGradientDrag();
    void CancelGradient() noexcept;
    void CommitGradient();
    // Switching tools, layers or targets applies the pending gradient, as in Photoshop.
    void ResolveGradient();
    [[nodiscard]] const GradientEdit* PendingGradient() const noexcept
    {
        return _gradient.get();
    }
    [[nodiscard]] GradientEdit* MutableGradient() noexcept
    {
        return _gradient.get();
    }

    // Shape (C11): a drag in whole pixels (Shift squares, Alt grows from the center) becomes a new layer above the
    // active one filled with the foreground color, one entry (`Rectangle`, `Ellipse`); a click makes nothing and
    // the selection is left alone. Shift-U switches the kind.
    static constexpr std::int64_t kMaxShapePixels = 100'000'000;
    void BeginShape(Vec2 point);
    void DragShape(Vec2 point, bool square, bool fromCenter);
    void CancelShape() noexcept;
    void ToggleShapeKind();
    void FinishShape();
    [[nodiscard]] const std::optional<ShapeDraft>& PendingShape() const noexcept
    {
        return _shapeDraft;
    }
    // "Rectangle 1", "Ellipse 2", … skipping names already in the document.
    [[nodiscard]] std::wstring NextShapeName(ShapeKind kind) const;
    // "Layer 1", "Layer 2", … skipping names already in the document.
    [[nodiscard]] std::wstring NextLayerName() const;

    // Pixel edits (C9b, Specs/Document/Document_Tools.md): the selection edits that need raster primitives.
    [[nodiscard]] bool CanEditPixels() const noexcept
    {
        return CanPaint();
    }
    // Fills the selection (the whole canvas without one; an empty selection nothing) with the palette color as one
    // entry (`Fill`, `Fill Mask`); a mask takes the color's first component, so black hides and white reveals.
    void FillSelection(FillSource source);
    // Delete with a selection: image pixels become transparent (`Clear`); on a mask the selection fills with the
    // background color, as in Photoshop.
    void ClearSelectedPixels();
    // The Delete key: clears the selection when there is one; otherwise deletes the targeted mask, or the layer.
    void DeleteKeyPressed();
    void DeleteLayerOrMask();
    void DeleteLayerMask();
    // Ctrl+I: inverts the layer's colors (transparency kept) or its mask, inside the selection or across the whole
    // layer without one; a pending transform or gradient applies first. One entry (`Invert`, `Invert Mask`).
    [[nodiscard]] bool CanInvert() const noexcept;
    void InvertPixels();
    // Ctrl-drag / Ctrl-arrow: the selected pixels (or a copy) move with the outline as one entry (`Move Pixels`,
    // `Duplicate Pixels`); false when there is nothing to move (no selection, a mask target, no pixels).
    bool BeginPixelMove(bool duplicate = false);
    // Previews the pixels `offset` whole document pixels away; the stored selection stays put until commit.
    void MovePixels(double dx, double dy);
    void FinishPixelMove();
    void CancelPixelMove() noexcept;
    void NudgePixels(double dx, double dy);
    [[nodiscard]] const PixelMove* PendingPixelMove() const noexcept
    {
        return _pixelMove.get();
    }
    [[nodiscard]] PixelMove* MutablePixelMove() noexcept
    {
        return _pixelMove.get();
    }
    // The outline to draw: during a pixel move the original shifted by the drag; while transforming selected
    // pixels it follows the handles (and a distortion).
    [[nodiscard]] std::optional<DocumentSelection> DisplayedSelection() const;
    // Add Mask with a selection: a mask on the layer's pixel grid hiding the selection (or, not revealing, showing
    // only it), dropping the selection and targeting the mask (`Add Mask from Selection`); without one,
    // `AddLayerMask`.
    void AddMask(bool revealing = true);
    // Ctrl+T with a selection transforms the selected pixels on a floating layer; without one, the layer.
    [[nodiscard]] bool CanTransformSelection() const noexcept;
    void TransformCommand();
    void BeginSelectionTransform();
    // Maps the original selection to where the floating pixels are now (nullopt for a plain layer edit).
    [[nodiscard]] std::optional<Affine2> FloatingSelectionMap(const TransformEdit& edit) const noexcept;
    // The clipboard: Copy takes the selection (or the whole canvas) from the active layer or its mask as shown;
    // Copy Merged takes the host's composite; Paste puts the pixels back in place as a new layer; Layer via Copy
    // skips the clipboard. The host mirrors copies to the system clipboard and brings other apps' images in.
    [[nodiscard]] std::optional<GridRect> SelectionCopyRegion() const noexcept;
    [[nodiscard]] bool CanCopyPixels() const noexcept;
    [[nodiscard]] std::optional<PixelClipboard> RenderSelectedPixels(const ImageLayer& layer, bool mask) const;
    [[nodiscard]] bool CanCopyMerged() const noexcept;
    [[nodiscard]] std::optional<PixelClipboard> RenderMergedPixels(const Raster& composite) const;
    bool CopySelection();
    bool CopyMergedSelection(const Raster& composite);
    void CutSelection();
    [[nodiscard]] bool CanPaste() const noexcept
    {
        return _document.has_value() && _clipboard.has_value();
    }
    void Paste();
    // Pixels from another app, centered on the canvas (`Paste`).
    void PasteImage(const Raster& image);
    void LayerViaCopy();
    [[nodiscard]] const std::optional<PixelClipboard>& Clipboard() const noexcept
    {
        return _clipboard;
    }

    // Color adjustments and filters (C12, Specs/Document/Document_Tools.md): the Image menu's Curves, Levels,
    // Hue/Saturation, Exposure, Gradient Map and Grain and the Filter menu's blurs, noise and lens correction run on
    // the active image layer inside the selection, previewed live through `DisplayDocument` and recorded as one
    // entry on OK; adjustment layers keep their settings live and edit through the same panels over the composite
    // beneath them. Painting and history wait while a panel is open.
    [[nodiscard]] bool CanAdjustColors() const noexcept;
    // Content-Aware Fill (C13) needs a non-empty selection on an image layer's pixels.
    [[nodiscard]] bool CanContentAwareFill() const noexcept;
    [[nodiscard]] bool HasColorEdit() const noexcept
    {
        return _filter.has_value() || _levels.has_value() || _hueSaturation.has_value();
    }
    FilterSettings filterSettings; // the last applied settings, the next panel's starting point
    void BeginFilter(FilterKind kind);
    void UpdateFilter(const FilterSettings& settings, bool preview);
    void CommitFilter();
    void CancelFilter();
    [[nodiscard]] const std::optional<FilterEdit>& PendingFilter() const noexcept
    {
        return _filter;
    }
    void BeginLevels();
    void UpdateLevels(const LevelsSettings& settings, bool preview);
    void AutoLevels(LevelsAuto mode);
    void SetLevelsSampleMode(std::optional<LevelsSample> mode) noexcept;
    // The eyedroppers read the original layer under a document point; transparent pixels are ignored.
    void SampleLevels(Vec2 point);
    void CommitLevels();
    void CancelLevels();
    [[nodiscard]] const std::optional<LevelsEdit>& PendingLevels() const noexcept
    {
        return _levels;
    }
    void BeginHueSaturation();
    void UpdateHueSaturation(const HueSaturationSettings& settings, bool preview);
    void SetHueSampleMode(std::optional<HueSampleMode> mode) noexcept;
    void SetHueTargeting(bool targeting) noexcept;
    // The hue under a document point of the visible composite; near-neutral pixels have no meaningful hue.
    [[nodiscard]] static std::optional<double> SampledHue(const Raster& composite, Vec2 point) noexcept;
    // The eyedroppers: re-center, widen or narrow the selected range's band on the sampled hue.
    void SampleHueRange(const Raster& composite, Vec2 point);
    // Targeted adjustment: picks the range owning the sampled color; the drag changes its saturation (or hue).
    bool BeginHueTargeting(const Raster& composite, Vec2 point);
    void DragHueTargeting(double viewDelta, bool adjustsHue);
    void EndHueTargeting() noexcept;
    void CommitHueSaturation();
    void CancelHueSaturation();
    [[nodiscard]] const std::optional<HueSaturationEdit>& PendingHueSaturation() const noexcept
    {
        return _hueSaturation;
    }
    // Adjustment layers: a new one above the active layer (`New <Kind> Adjustment`; a Gradient Map runs from the
    // foreground to the background color, each Grain gets its own pattern) opens for editing; `UpdateAdjustment`
    // changes a record live; editing wraps the kind's panel in one `Edit <Kind> Adjustment` entry over the
    // composite beneath the layer (`below`, rendered by the host), OK keeping the settings and Cancel restoring them.
    void AddAdjustment(AdjustmentKind kind);
    bool UpdateAdjustment(const Guid& id, const LayerAdjustment& value);
    [[nodiscard]] std::optional<Guid> AdjustmentEditingID() const noexcept
    {
        return _adjustmentEditingID;
    }
    void SetAdjustmentEditingID(std::optional<Guid> id) noexcept
    {
        _adjustmentEditingID = id;
    }
    bool BeginAdjustmentEditing(const Guid& id, const Raster& below);
    bool PreviewAdjustmentEditing(bool preview);
    bool FinishAdjustmentEditing(bool commit);
    [[nodiscard]] const std::optional<LayerAdjustment>& AdjustmentOriginal() const noexcept
    {
        return _adjustmentOriginal;
    }
    // Shift+= / Shift+-: the active layer's blend mode, forward or back, wrapping (one entry each).
    void StepBlendMode(bool forward);
    // The shape filling its box, anti-aliased where it curves, premultiplied BGRA; empty over the pixel budget.
    [[nodiscard]] static Raster ShapeImage(ShapeKind kind, int width, int height, std::uint32_t argb,
                                           double cornerRadius);
    // Inserts pixels as a new layer above the active one (inside its folder), all in one entry. Pasting drops the
    // selection, as in Photoshop; a drawn shape keeps it.
    void AddPixelLayer(const Raster& image, Vec2 origin, std::wstring name, const std::wstring& editName,
                       bool dropsSelection, std::optional<LayerShapeStyle> shape);

    // Eyedropper (C11): the straight sRGB color of the visible canvas at one document pixel, read from the host's
    // composite (document-size premultiplied BGRA). Nullopt outside the canvas or over a fully transparent pixel.
    [[nodiscard]] static std::optional<std::uint32_t> SampleCompositeColor(const Raster& composite,
                                                                           Vec2 point) noexcept;
    void ContinueBrush(Vec2 point);
    // Replaces the provisional tail by the final curve and commits the tiles as one entry (`Brush Stroke`,
    // `Erase`, `Paint Mask`); nothing is recorded when no pixel changed.
    void FinishBrush();
    void CancelBrush() noexcept;
    [[nodiscard]] const BrushStroke* PendingBrush() const noexcept
    {
        return _brush.get();
    }
    [[nodiscard]] BrushStroke* MutableBrush() noexcept
    {
        return _brush.get();
    }
    // The commit FinishBrush just made, once.
    [[nodiscard]] std::optional<BrushCommitInfo> TakeLastBrushCommit() noexcept
    {
        std::optional<BrushCommitInfo> result = std::move(_lastBrushCommit);
        _lastBrushCommit.reset();
        return result;
    }
    // Where a Shift-click paints a line from: the end of the last stroke while the same layer (or mask) is the
    // target.
    [[nodiscard]] std::optional<Vec2> ShiftLineStart() const noexcept;
    // ] / [ : a fifth up or down, always at least one pixel, 1–2000.
    void ChangeBrushSize(bool increase) noexcept;
    // Shift-] / Shift-[ : hardness in Photoshop's 25 % steps.
    void ChangeBrushHardness(bool increase) noexcept;
    // Photoshop-style opacity keys: 1 = 10 % … 9 = 90 %, 0 = 100 %; two digits within 0.6 s set an exact value.
    void TypeOpacityDigit(int digit, double timeSeconds) noexcept;
    // The percent a digit types (with the two-digit window), for a caller that applies it elsewhere; -1 when none.
    [[nodiscard]] int TypedOpacityPercent(int digit, double timeSeconds) noexcept;
    // A tool switch ends a two-digit entry in progress.
    void ClearPendingOpacityDigit() noexcept
    {
        _pendingOpacityDigit.reset();
    }
    // The pixel budget left for a raster edit of `layer` (100 M image or mask pixels less the other layers).
    [[nodiscard]] std::int64_t RasterEditPixelLimit(const ImageLayer& layer, bool mask) const noexcept;
    BrushSettings brushSettings;
    BrushToolMode brushMode = BrushToolMode::Paint;
    bool maskPaintWhite = false;
    SpotHealingMode spotHealingMode = SpotHealingMode::ContentAware;
    CloneSettings cloneSettings;
    std::optional<Vec2> cloneSource;
    SmearMode smearMode = SmearMode::Liquify;
    GradientSettings gradientSettings;
    ShapeKind shapeKind = ShapeKind::Rectangle;
    // Corner radius in pixels for rectangles the Shape tool draws; 0 keeps the corners square.
    double shapeCornerRadius = 0;
    // The eyedropper's comparison ring (new color above, previous below) while sampling.
    bool showsSampleRing = true;

    // Palette (foreground/background paint colors, opaque ARGB). Session state outside history, as in the legacy
    // EditorSession brush settings: X swaps, D resets. Mask-selected black/white policy arrives with mask painting.
    static constexpr uint32_t kDefaultForegroundArgb = 0xFF000000u;
    static constexpr uint32_t kDefaultBackgroundArgb = 0xFFFFFFFFu;
    [[nodiscard]] uint32_t ForegroundColor() const noexcept
    {
        return _foregroundArgb;
    }
    [[nodiscard]] uint32_t BackgroundColor() const noexcept
    {
        return _backgroundArgb;
    }
    void SetForegroundColor(uint32_t argb) noexcept
    {
        _foregroundArgb = argb | 0xFF000000u;
        RefreshGradient();
    }
    void SetBackgroundColor(uint32_t argb) noexcept
    {
        _backgroundArgb = argb | 0xFF000000u;
        RefreshGradient();
    }
    void SwapPaletteColors() noexcept
    {
        std::swap(_foregroundArgb, _backgroundArgb);
        RefreshGradient();
    }
    void ResetPaletteColors() noexcept
    {
        _foregroundArgb = kDefaultForegroundArgb;
        _backgroundArgb = kDefaultBackgroundArgb;
        RefreshGradient();
    }

  private:
    void Restore(const DocumentHistory::Snapshot& snapshot);
    static void AdoptClipping(const Guid& id, std::vector<ImageLayer>& layers);
    static void ReleaseDetachedClipping(std::vector<ImageLayer>& layers);
    [[nodiscard]] ImageLayer* MutableLayer(const Guid& id) noexcept;
    [[nodiscard]] const ImageLayer* FindLayer(const Guid& id) const noexcept;

    std::optional<CanvasDocument> _document;
    std::optional<Guid> _activeLayerID;
    std::unordered_set<Guid> _selected;
    std::unordered_set<Guid> _collapsed;
    uint32_t _foregroundArgb = kDefaultForegroundArgb;
    uint32_t _backgroundArgb = kDefaultBackgroundArgb;
    std::optional<TransformEdit> _transformEdit;
    std::optional<std::pair<Guid, Guid>> _transformDuplicate; // copy, source
    SnapGuides _snapGuides;
    bool _maskSelected = false;
    std::uint64_t _editRevision = 0;
    mutable std::uint64_t _displayRevision = 0;
    mutable std::optional<CanvasDocument> _displayCopy;
    std::optional<LassoDraft> _lassoDraft;
    std::optional<DocumentSelection> _selectionMoveOrigin;
    std::shared_ptr<BrushStroke> _brush;
    BrushCoverageEngine* _brushEngine = nullptr;
    struct LastBrushPoint
    {
        Vec2 point;
        Guid layerID;
        bool mask = false;
    };
    std::optional<LastBrushPoint> _lastBrushPoint;
    std::optional<std::pair<int, double>> _pendingOpacityDigit;
    std::optional<BrushCommitInfo> _lastBrushCommit;
    std::optional<Vec2> _cloneOffset;
    std::shared_ptr<WarpStroke> _warp;
    struct WarpSource
    {
        Guid layerID;
        Raster image;
        LayerTransform transform;
    };
    WarpSource _warpSource;
    std::array<std::array<double, 3>, 3> _parkedTips{{{40, 1, 1}, {40, 0, 1}, {40, 0, 1}}};
    std::uint32_t _healSeed = 0x9e3779b9u;
    std::shared_ptr<GradientEdit> _gradient;
    std::optional<ShapeDraft> _shapeDraft;
    std::shared_ptr<PixelMove> _pixelMove;
    std::optional<PixelClipboard> _clipboard;
    std::optional<FilterEdit> _filter;
    std::optional<LevelsEdit> _levels;
    std::optional<HueSaturationEdit> _hueSaturation;
    std::optional<Guid> _adjustmentEditingID;
    std::optional<LayerAdjustment> _adjustmentOriginal;
    struct PreviewLayer
    {
        Guid layerID;
        Raster raster;
        LayerTransform transform;
    };
    [[nodiscard]] PreviewLayer PreviewedLayer() const noexcept;
    [[nodiscard]] std::optional<LayerAdjustment> EditedAdjustment() const noexcept;
    bool GrowFilterForBlur();
    // Pads the filter's grid out to cover `extent` (layer pixels), keeping what is already grown.
    bool GrowFilterGrid(int left, int top, int right, int bottom);
    void PrepareFilterPreview();
    void RenderFilterPreview();
    void RenderLevelsPreview();
    void RenderHuePreview();
    // Replaces a layer's pixels as one entry, unless the layer changed under the edit.
    bool ReplaceLayerPixels(const Guid& id, const Raster& originalImage, const Raster& image,
                            const LayerTransform& transform, std::optional<LayerMask> mask, const std::wstring& name);
    [[nodiscard]] static Raster SelectionCoverageOn(const DocumentSelection& selection, const LayerTransform& placement,
                                                    int width, int height);
    [[nodiscard]] std::shared_ptr<BrushStroke> MakeRasterEdit(const ImageLayer& layer, bool mask) const;
    void MergeFloating(const TransformEdit& edit, const FloatingTransform& floating);
    void CancelFloating(const FloatingTransform& floating);
    void InsertLayerAboveActive(ImageLayer layer, const std::wstring& editName, bool dropsSelection);
    // A shape layer scaled to a new size draws its shape again at that size, so a rounded corner keeps its radius
    // instead of stretching. Part of the edit that changed the size.
    void RedrawShape(std::size_t index);
    // Installs a raster edit as one entry; `alsoApply` runs inside the same entry (a moved selection).
    void CommitStroke(const std::shared_ptr<BrushStroke>& stroke, const std::function<void()>& alsoApply = {});
    [[nodiscard]] std::uint32_t NextHealSeed() noexcept
    {
        _healSeed = _healSeed * 1664525u + 1013904223u;
        return _healSeed;
    }
};
