#pragma once

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

struct DocumentSelection
{
    bool antialiased = true;
    bool empty = false;
    std::vector<Vec2> outline;
    [[nodiscard]] bool operator==(const DocumentSelection&) const noexcept = default;
    [[nodiscard]] bool IsEmpty() const noexcept
    {
        return empty || outline.size() < 3;
    }
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
};
