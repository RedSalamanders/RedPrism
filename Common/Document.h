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

class Raster
{
  public:
    [[nodiscard]] static Raster Make(int width, int height, RasterFormat format);
    [[nodiscard]] static Raster Make(int width, int height, RasterFormat format, const std::uint8_t* pixels,
                                     std::size_t byteCount);
    [[nodiscard]] static Raster MakeThumbnail(const Raster& source, int maxSide = 96);
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
    [[nodiscard]] const std::uint8_t* Data() const noexcept
    {
        return _storage ? _storage->bytes.data() : nullptr;
    }

  private:
    struct Storage
    {
        int width = 0;
        int height = 0;
        int stride = 0;
        RasterFormat format = RasterFormat::Bgra8888;
        std::vector<std::uint8_t> bytes;
    };
    std::shared_ptr<const Storage> _storage;
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
};

// Document positions a move has just snapped to, drawn as guides while it lasts.
struct SnapGuides
{
    std::vector<double> xs;
    std::vector<double> ys;
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
    }
    void SetBackgroundColor(uint32_t argb) noexcept
    {
        _backgroundArgb = argb | 0xFF000000u;
    }
    void SwapPaletteColors() noexcept
    {
        std::swap(_foregroundArgb, _backgroundArgb);
    }
    void ResetPaletteColors() noexcept
    {
        _foregroundArgb = kDefaultForegroundArgb;
        _backgroundArgb = kDefaultBackgroundArgb;
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
};
