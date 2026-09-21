#include "Document.h"

#include "TransformEdit.h"
#include "Utf8.h"

#include <algorithm>
#include <cstring>
#include <exception>
#include <format>
#include <iterator>
#include <rpc.h>
#include <string>
#include <string_view>
#include <windows.h>

#pragma comment(lib, "rpcrt4.lib")
#pragma comment(lib, "ole32.lib")

Guid Guid::Generate() noexcept
{
    GUID value{};
    const RPC_STATUS status = UuidCreate(&value);
    if (status != RPC_S_OK && status != RPC_S_UUID_LOCAL_ONLY)
    {
        std::terminate();
    }
    Guid id;
    std::memcpy(id.bytes.data(), &value, sizeof(value));
    return id;
}

std::wstring Guid::ToString() const
{
    GUID value{};
    std::memcpy(&value, bytes.data(), sizeof(value));
    return std::format(L"{:08X}-{:04X}-{:04X}-{:02X}{:02X}-{:02X}{:02X}{:02X}{:02X}{:02X}{:02X}", value.Data1,
                       value.Data2, value.Data3, value.Data4[0], value.Data4[1], value.Data4[2], value.Data4[3],
                       value.Data4[4], value.Data4[5], value.Data4[6], value.Data4[7]);
}

std::string Guid::ToUtf8() const
{
    return WideToUtf8(ToString());
}

std::optional<Guid> Guid::Parse(std::string_view text) noexcept
{
    if (text.empty() || text.size() > 48)
    {
        return std::nullopt;
    }
    std::string copy(text);
    UUID value{};
    if (UuidFromStringA(reinterpret_cast<RPC_CSTR>(copy.data()), &value) != RPC_S_OK)
    {
        return std::nullopt;
    }
    Guid id;
    std::memcpy(id.bytes.data(), &value, sizeof(value));
    return id;
}

namespace
{
[[nodiscard]] const char* NameAt(const char* const* names, int count, int index) noexcept
{
    if (index < 0 || index >= count || names[index] == nullptr)
    {
        return nullptr;
    }
    return names[index];
}

template <typename T>
[[nodiscard]] std::optional<T> ValueFromTable(const char* const* names, int count, std::string_view name) noexcept
{
    for (int index = 0; index < count; ++index)
    {
        if (names[index] != nullptr && name == names[index])
        {
            return static_cast<T>(index);
        }
    }
    return std::nullopt;
}

constexpr const char* kBlendNames[] = {"Normal",     "Multiply",   "Screen",      "Overlay",    "Darken",
                                       "Lighten",    "Difference", "Color Dodge", "Color Burn", "Hue",
                                       "Saturation", "Color",      "Luminosity"};
constexpr const char* kSamplingNames[] = {"Nearest", "Smooth", "High quality"};
constexpr const char* kAdjustmentNames[] = {"Hue/Saturation", "Levels", "Curves", "Exposure", "Gradient Map", "Grain"};
constexpr const char* kLevelsChannelNames[] = {"RGB", "Red", "Green", "Blue"};
constexpr const char* kColorRangeNames[] = {"Master", "Reds", "Yellows", "Greens", "Cyans", "Blues", "Magentas"};
constexpr const char* kShapeKindNames[] = {"Rectangle", "Ellipse"};
} // namespace

const char* LayerBlendModeName(LayerBlendMode mode) noexcept
{
    return NameAt(kBlendNames, static_cast<int>(std::size(kBlendNames)), static_cast<int>(mode));
}

std::optional<LayerBlendMode> LayerBlendModeFromName(std::string_view name) noexcept
{
    return ValueFromTable<LayerBlendMode>(kBlendNames, static_cast<int>(std::size(kBlendNames)), name);
}

const char* LayerSamplingName(LayerSampling sampling) noexcept
{
    return NameAt(kSamplingNames, static_cast<int>(std::size(kSamplingNames)), static_cast<int>(sampling));
}

std::optional<LayerSampling> LayerSamplingFromName(std::string_view name) noexcept
{
    return ValueFromTable<LayerSampling>(kSamplingNames, static_cast<int>(std::size(kSamplingNames)), name);
}

const char* AdjustmentKindName(AdjustmentKind kind) noexcept
{
    return NameAt(kAdjustmentNames, static_cast<int>(std::size(kAdjustmentNames)), static_cast<int>(kind));
}

std::optional<AdjustmentKind> AdjustmentKindFromName(std::string_view name) noexcept
{
    return ValueFromTable<AdjustmentKind>(kAdjustmentNames, static_cast<int>(std::size(kAdjustmentNames)), name);
}

const char* LevelsChannelName(LevelsChannel channel) noexcept
{
    return NameAt(kLevelsChannelNames, static_cast<int>(std::size(kLevelsChannelNames)), static_cast<int>(channel));
}

std::optional<LevelsChannel> LevelsChannelFromName(std::string_view name) noexcept
{
    return ValueFromTable<LevelsChannel>(kLevelsChannelNames, static_cast<int>(std::size(kLevelsChannelNames)), name);
}

const char* ColorRangeName(ColorRange range) noexcept
{
    return NameAt(kColorRangeNames, static_cast<int>(std::size(kColorRangeNames)), static_cast<int>(range));
}

std::optional<ColorRange> ColorRangeFromName(std::string_view name) noexcept
{
    return ValueFromTable<ColorRange>(kColorRangeNames, static_cast<int>(std::size(kColorRangeNames)), name);
}

const char* ShapeKindName(ShapeKind kind) noexcept
{
    return NameAt(kShapeKindNames, static_cast<int>(std::size(kShapeKindNames)), static_cast<int>(kind));
}

std::optional<ShapeKind> ShapeKindFromName(std::string_view name) noexcept
{
    return ValueFromTable<ShapeKind>(kShapeKindNames, static_cast<int>(std::size(kShapeKindNames)), name);
}

Raster Raster::Make(int width, int height, RasterFormat format)
{
    return Make(width, height, format, nullptr, 0);
}

Raster Raster::Make(int width, int height, RasterFormat format, const std::uint8_t* pixels, std::size_t byteCount)
{
    Raster raster;
    if (width < 1 || height < 1 || width > kMaxCanvasSide || height > kMaxCanvasSide)
    {
        return raster;
    }
    const int stride = format == RasterFormat::Gray8 ? width : width * 4;
    const std::uint64_t bytes = static_cast<std::uint64_t>(stride) * static_cast<std::uint64_t>(height);
    if (bytes > SIZE_MAX)
    {
        return raster;
    }
    const std::size_t size = static_cast<std::size_t>(bytes);
    if (pixels != nullptr && byteCount != size)
    {
        return raster;
    }
    auto storage = std::make_shared<Storage>();
    storage->width = width;
    storage->height = height;
    storage->stride = stride;
    storage->format = format;
    if (pixels == nullptr)
    {
        storage->bytes.assign(size, 0);
    }
    else
    {
        storage->bytes.assign(pixels, pixels + size);
    }
    raster._storage = std::move(storage);
    return raster;
}

Raster Raster::MakeThumbnail(const Raster& source, int maxSide)
{
    if (!source.HasStorage() || maxSide < 1)
    {
        return {};
    }
    const int width = source.Width();
    const int height = source.Height();
    const int largest = std::max(width, height);
    if (largest <= maxSide)
    {
        return source;
    }
    const int thumbWidth = std::max(1, (width * maxSide + largest / 2) / largest);
    const int thumbHeight = std::max(1, (height * maxSide + largest / 2) / largest);
    const RasterFormat format = source.Format();
    const int srcStride = source.Stride();
    const int dstStride = format == RasterFormat::Gray8 ? thumbWidth : thumbWidth * 4;
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(dstStride) * static_cast<std::size_t>(thumbHeight), 0);
    const std::uint8_t* src = source.Data();
    if (src == nullptr)
    {
        return {};
    }
    const int channels = format == RasterFormat::Gray8 ? 1 : 4;
    for (int y = 0; y < thumbHeight; ++y)
    {
        const int srcY = std::min(height - 1, (y * height) / thumbHeight);
        for (int x = 0; x < thumbWidth; ++x)
        {
            const int srcX = std::min(width - 1, (x * width) / thumbWidth);
            const std::uint8_t* from = src + static_cast<std::size_t>(srcY) * static_cast<std::size_t>(srcStride) +
                                       static_cast<std::size_t>(srcX) * static_cast<std::size_t>(channels);
            std::uint8_t* to = bytes.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(dstStride) +
                               static_cast<std::size_t>(x) * static_cast<std::size_t>(channels);
            std::memcpy(to, from, static_cast<std::size_t>(channels));
        }
    }
    return Make(thumbWidth, thumbHeight, format, bytes.data(), bytes.size());
}

bool LayerTransform::IsValid() const noexcept
{
    return std::isfinite(origin.x) && std::isfinite(origin.y) && std::isfinite(size.x) && std::isfinite(size.y) &&
           std::isfinite(rotation) && size.x >= 1 && size.x <= kMaxTransformSide && size.y >= 1 &&
           size.y <= kMaxTransformSide && std::abs(origin.x) <= kMaxTransformOrigin &&
           std::abs(origin.y) <= kMaxTransformOrigin;
}

LevelRange LevelRange::Normalized() const noexcept
{
    const auto clamp = [](double value, double lo, double hi, double fallback)
    {
        if (!std::isfinite(value))
        {
            return fallback;
        }
        return std::min(hi, std::max(lo, value));
    };
    LevelRange result = *this;
    result.black = clamp(black, 0, 254, 0);
    result.white = clamp(white, result.black + 1, 255, 255);
    result.gamma = clamp(gamma, 0.1, 9.99, 1);
    result.outputBlack = clamp(outputBlack, 0, 255, 0);
    result.outputWhite = clamp(outputWhite, 0, 255, 255);
    return result;
}

bool CurvesSettings::IsValid() const noexcept
{
    for (const std::vector<CurvePoint>& points : channels)
    {
        if (points.size() < 2 || points.size() > 32)
        {
            return false;
        }
        if (points.front().x != 0 || points.back().x != 255)
        {
            return false;
        }
        for (std::size_t i = 0; i < points.size(); ++i)
        {
            const CurvePoint& point = points[i];
            if (!std::isfinite(point.x) || !std::isfinite(point.y) || point.x < 0 || point.x > 255 || point.y < 0 ||
                point.y > 255)
            {
                return false;
            }
            if (i > 0 && !(points[i - 1].x < point.x))
            {
                return false;
            }
        }
    }
    return true;
}

std::array<HueBand, static_cast<std::size_t>(ColorRange::Count)> HueSaturationSettings::DefaultBands() noexcept
{
    return {
        HueBand{0, 0, 360, 360},     HueBand{315, 345, 15, 45},   HueBand{15, 45, 75, 105},
        HueBand{75, 105, 135, 165},  HueBand{135, 165, 195, 225}, HueBand{195, 225, 255, 285},
        HueBand{255, 285, 315, 345},
    };
}

HueSaturationSettings LayerAdjustment::ResolvedHsv() const noexcept
{
    if (hsvSettings.has_value())
    {
        return hsvSettings.value();
    }
    HueSaturationSettings settings;
    settings.colorize = colorize;
    settings.adjustments[static_cast<std::size_t>(ColorRange::Master)] = RangeAdjustment{hue, saturation, lightness};
    return settings;
}

bool LayerAdjustment::IsValid() const noexcept
{
    if (!std::isfinite(hue) || !std::isfinite(saturation) || !std::isfinite(lightness) || std::abs(hue) > 360 ||
        std::abs(saturation) > 100 || std::abs(lightness) > 100)
    {
        return false;
    }
    const HueSaturationSettings hsv = ResolvedHsv();
    for (const RangeAdjustment& adjustment : hsv.adjustments)
    {
        if (!std::isfinite(adjustment.hue) || std::abs(adjustment.hue) > 360 || !std::isfinite(adjustment.saturation) ||
            std::abs(adjustment.saturation) > 100 || !std::isfinite(adjustment.lightness) ||
            std::abs(adjustment.lightness) > 100)
        {
            return false;
        }
    }
    for (const HueBand& band : hsv.bands)
    {
        for (const double handle : band.Handles())
        {
            if (!std::isfinite(handle))
            {
                return false;
            }
        }
    }
    for (const LevelRange& range : levels.ranges)
    {
        if (!(range == range.Normalized()))
        {
            return false;
        }
    }
    return curves.IsValid() && Exposure().IsValid() && GradientMap().IsValid() && Grain().IsValid();
}

bool LayerMask::IsValid(const Raster& image) noexcept
{
    return image.HasStorage() && image.Format() == RasterFormat::Gray8 && image.Width() > 0 && image.Height() > 0;
}

bool ImageLayer::operator==(const ImageLayer& other) const noexcept
{
    const bool assetEqual =
        (!asset.has_value() && !other.asset.has_value()) ||
        (asset.has_value() && other.asset.has_value() && asset.value().image == other.asset.value().image);
    return id == other.id && name == other.name && visible == other.visible && transform == other.transform &&
           assetEqual && parentID == other.parentID && isGroup == other.isGroup && opacity == other.opacity &&
           blendMode == other.blendMode && mask == other.mask && maskSourceID == other.maskSourceID &&
           adjustment == other.adjustment && shape == other.shape;
}

ImageLayer ImageLayer::Blank(std::wstring layerName, Vec2 size)
{
    ImageLayer layer;
    layer.id = Guid::Generate();
    layer.transform.size = size;
    layer.name = std::move(layerName);
    return layer;
}

std::wstring TrimWide(std::wstring_view text)
{
    std::size_t begin = 0;
    while (begin < text.size() && iswspace(text[begin]))
    {
        ++begin;
    }
    std::size_t end = text.size();
    while (end > begin && iswspace(text[end - 1]))
    {
        --end;
    }
    return std::wstring(text.substr(begin, end - begin));
}

bool NameUtf8LengthOk(std::wstring_view name) noexcept
{
    if (name.empty())
    {
        return true;
    }
    const int bytes =
        WideCharToMultiByte(CP_UTF8, 0, name.data(), static_cast<int>(name.size()), nullptr, 0, nullptr, nullptr);
    return bytes >= 0 && bytes <= kMaxNameUtf8Bytes;
}

namespace
{
[[nodiscard]] const ImageLayer* LayerById(const std::vector<ImageLayer>& layers, const Guid& id) noexcept
{
    for (const ImageLayer& layer : layers)
    {
        if (layer.id == id)
        {
            return &layer;
        }
    }
    return nullptr;
}

[[nodiscard]] ImageLayer* LayerById(std::vector<ImageLayer>& layers, const Guid& id) noexcept
{
    for (ImageLayer& layer : layers)
    {
        if (layer.id == id)
        {
            return &layer;
        }
    }
    return nullptr;
}

[[nodiscard]] std::optional<std::size_t> IndexOf(const std::vector<ImageLayer>& layers, const Guid& id) noexcept
{
    for (std::size_t i = 0; i < layers.size(); ++i)
    {
        if (layers[i].id == id)
        {
            return i;
        }
    }
    return std::nullopt;
}
} // namespace

DocumentStatus ValidateHierarchy(const std::vector<ImageLayer>& layers)
{
    std::unordered_map<Guid, const ImageLayer*> byId;
    byId.reserve(layers.size());
    for (const ImageLayer& layer : layers)
    {
        if (!byId.emplace(layer.id, &layer).second)
        {
            return DocumentStatus::Invalid;
        }
        if (layer.isGroup && layer.asset.has_value())
        {
            return DocumentStatus::Invalid;
        }
    }
    for (const ImageLayer& layer : layers)
    {
        std::unordered_set<Guid> seen{layer.id};
        std::optional<Guid> parent = layer.parentID;
        while (parent.has_value())
        {
            if (static_cast<int>(seen.size()) > kMaxGroupDepth)
            {
                return DocumentStatus::Invalid;
            }
            if (!seen.insert(parent.value()).second)
            {
                return DocumentStatus::Invalid;
            }
            const auto found = byId.find(parent.value());
            if (found == byId.end() || !found->second->isGroup)
            {
                return DocumentStatus::Invalid;
            }
            parent = found->second->parentID;
        }
        if (layer.isGroup && static_cast<int>(seen.size()) > kMaxGroupDepth)
        {
            return DocumentStatus::Invalid;
        }
    }
    return DocumentStatus::Ok;
}

DocumentStatus ValidateLiveMaskGraph(const std::vector<ImageLayer>& layers)
{
    std::unordered_map<Guid, const ImageLayer*> records;
    records.reserve(layers.size());
    for (const ImageLayer& layer : layers)
    {
        if (!records.emplace(layer.id, &layer).second)
        {
            return DocumentStatus::Invalid;
        }
    }
    for (const ImageLayer& layer : layers)
    {
        std::unordered_set<Guid> path;
        std::optional<Guid> current = layer.id;
        while (current.has_value())
        {
            if (static_cast<int>(path.size()) >= kMaxLiveMaskPath || !path.insert(current.value()).second)
            {
                return DocumentStatus::Invalid;
            }
            const auto found = records.find(current.value());
            if (found == records.end())
            {
                return DocumentStatus::Invalid;
            }
            const ImageLayer* record = found->second;
            if (record->maskSourceID.has_value())
            {
                if (record->isGroup)
                {
                    return DocumentStatus::Invalid;
                }
                const auto source = records.find(record->maskSourceID.value());
                if (source == records.end() || source->second->isGroup || source->second->adjustment.has_value())
                {
                    return DocumentStatus::Invalid;
                }
            }
            current = record->maskSourceID;
        }
    }
    return DocumentStatus::Ok;
}

DocumentStatus ValidateDocument(const CanvasDocument& document, int formatVersion)
{
    if (formatVersion < kMinFormatVersion || formatVersion > kMaxFormatVersion)
    {
        return DocumentStatus::UnsupportedVersion;
    }
    if (!std::isfinite(document.resolution) || document.resolution < 1 || document.resolution > 9600)
    {
        return DocumentStatus::Invalid;
    }
    if (document.width < 1 || document.width > kMaxCanvasSide || document.height < 1 ||
        document.height > kMaxCanvasSide || static_cast<int>(document.layers.size()) > kMaxLayerCount)
    {
        return DocumentStatus::TooLarge;
    }
    int imagePixels = 0;
    int maskPixels = 0;
    std::unordered_set<Guid> ids;
    ids.reserve(document.layers.size());
    for (const ImageLayer& layer : document.layers)
    {
        if (layer.adjustment.has_value())
        {
            if (formatVersion < 7 || layer.isGroup || layer.asset.has_value() || !layer.adjustment.value().IsValid())
            {
                return DocumentStatus::Invalid;
            }
        }
        const int maskVersion = layer.isGroup ? 6 : 4;
        if (layer.mask.has_value())
        {
            // Pixels arrive after manifest validation (ProjectStore attaches assets later), so the format check
            // applies once storage exists; the decoder rejects non-Gray8 masks on the way in.
            const Raster& maskImage = layer.mask.value().asset.image;
            if (formatVersion < maskVersion || (maskImage.HasStorage() && !LayerMask::IsValid(maskImage)))
            {
                return DocumentStatus::Invalid;
            }
            if (layer.mask.value().placement.has_value() &&
                (!layer.mask.value().placement.value().IsValid() || !layer.mask.has_value()))
            {
                return DocumentStatus::Invalid;
            }
            maskPixels += layer.mask.value().asset.image.PixelCount();
            if (maskPixels > kMaxMaskPixels)
            {
                return DocumentStatus::TooLarge;
            }
        }
        if (!std::isfinite(layer.opacity) || layer.opacity < 0 || layer.opacity > 1)
        {
            return DocumentStatus::Invalid;
        }
        if ((formatVersion < 3 || layer.isGroup) && (layer.opacity != 1 || layer.blendMode != LayerBlendMode::Normal))
        {
            return DocumentStatus::Invalid;
        }
        if (layer.asset.has_value())
        {
            imagePixels += layer.asset.value().image.PixelCount();
            if (imagePixels > kMaxLayerPixels)
            {
                return DocumentStatus::TooLarge;
            }
        }
        if (!ids.insert(layer.id).second || !layer.transform.IsValid())
        {
            return DocumentStatus::Invalid;
        }
        if (TrimWide(layer.name).empty() || !NameUtf8LengthOk(layer.name))
        {
            return DocumentStatus::Invalid;
        }
        if (formatVersion < 5 && layer.maskSourceID.has_value())
        {
            return DocumentStatus::Invalid;
        }
        if (formatVersion == 1 && (layer.parentID.has_value() || layer.isGroup))
        {
            return DocumentStatus::Invalid;
        }
    }
    const DocumentStatus hierarchy = ValidateHierarchy(document.layers);
    if (hierarchy != DocumentStatus::Ok)
    {
        return hierarchy;
    }
    return ValidateLiveMaskGraph(document.layers);
}

std::vector<HierarchyEntry> HierarchyEntries(const std::vector<ImageLayer>& layers, bool topFirst,
                                             const std::unordered_set<Guid>& collapsed)
{
    std::unordered_map<Guid, std::vector<std::size_t>> children;
    std::vector<std::size_t> roots;
    for (std::size_t i = 0; i < layers.size(); ++i)
    {
        if (layers[i].parentID.has_value())
        {
            children[layers[i].parentID.value()].push_back(i);
        }
        else
        {
            roots.push_back(i);
        }
    }
    std::vector<HierarchyEntry> result;
    const auto visit = [&](auto&& self, const std::optional<Guid>& parent, int depth, bool visible) -> void
    {
        if (depth > kMaxGroupDepth)
        {
            return;
        }
        std::vector<std::size_t> siblings = parent.has_value() ? children[parent.value()] : roots;
        if (topFirst)
        {
            std::reverse(siblings.begin(), siblings.end());
        }
        for (const std::size_t index : siblings)
        {
            const ImageLayer& layer = layers[index];
            const bool effective = visible && layer.visible;
            const bool isCollapsed = layer.isGroup && collapsed.find(layer.id) != collapsed.end();
            result.push_back(HierarchyEntry{layer.id, layer.name, depth, effective, layer.isGroup, isCollapsed});
            if (layer.isGroup && !isCollapsed)
            {
                self(self, layer.id, depth + 1, effective);
            }
        }
    };
    visit(visit, std::nullopt, 0, true);
    return result;
}

std::vector<Guid> RenderLayerIds(const CanvasDocument& document)
{
    std::vector<Guid> ids;
    for (const HierarchyEntry& entry : HierarchyEntries(document.layers))
    {
        if (entry.visible && !entry.isGroup)
        {
            ids.push_back(entry.id);
        }
    }
    return ids;
}

std::unordered_set<Guid> DescendantIds(const std::vector<ImageLayer>& layers, const Guid& id)
{
    std::unordered_map<Guid, std::vector<Guid>> children;
    for (const ImageLayer& layer : layers)
    {
        if (layer.parentID.has_value())
        {
            children[layer.parentID.value()].push_back(layer.id);
        }
    }
    std::unordered_set<Guid> result;
    std::vector<Guid> pending{id};
    while (!pending.empty())
    {
        const Guid parent = pending.back();
        pending.pop_back();
        const auto found = children.find(parent);
        if (found == children.end())
        {
            continue;
        }
        for (const Guid& child : found->second)
        {
            if (result.insert(child).second)
            {
                pending.push_back(child);
            }
        }
    }
    return result;
}

const ImageLayer* DocumentSession::FindLayer(const Guid& id) const noexcept
{
    if (!_document.has_value())
    {
        return nullptr;
    }
    return LayerById(_document.value().layers, id);
}

ImageLayer* DocumentSession::MutableLayer(const Guid& id) noexcept
{
    if (!_document.has_value())
    {
        return nullptr;
    }
    return LayerById(_document.value().layers, id);
}

const ImageLayer* DocumentSession::ActiveLayer() const noexcept
{
    if (!_activeLayerID.has_value())
    {
        return nullptr;
    }
    return FindLayer(_activeLayerID.value());
}

ImageLayer* DocumentSession::ActiveLayer() noexcept
{
    if (!_activeLayerID.has_value())
    {
        return nullptr;
    }
    return MutableLayer(_activeLayerID.value());
}

void DocumentSession::BeginEdit(std::wstring name)
{
    history.Begin(std::move(name), _document, _activeLayerID);
}

void DocumentSession::EndEdit()
{
    history.End(_document, _activeLayerID);
}

void DocumentSession::Restore(const DocumentHistory::Snapshot& snapshot)
{
    const bool keepMaskTarget = _maskSelected && _activeLayerID == snapshot.activeLayerID;
    _document = snapshot.document;
    _activeLayerID = snapshot.activeLayerID;
    _selected.clear();
    if (_activeLayerID.has_value())
    {
        _selected.insert(_activeLayerID.value());
    }
    const ImageLayer* active = ActiveLayer();
    _maskSelected = keepMaskTarget && active != nullptr && active->mask.has_value();
    ++_editRevision;
    _displayCopy.reset();
}

void DocumentSession::Undo()
{
    _lassoDraft.reset();
    // A pending transform is not in history yet: the first Undo discards it, as Escape would.
    if (_transformEdit.has_value())
    {
        CancelTransform();
        return;
    }
    const std::optional<DocumentHistory::Snapshot> snapshot = history.Undo();
    if (snapshot.has_value())
    {
        Restore(snapshot.value());
    }
}

void DocumentSession::Redo()
{
    _lassoDraft.reset();
    if (_transformEdit.has_value())
    {
        CancelTransform();
        return;
    }
    const std::optional<DocumentHistory::Snapshot> snapshot = history.Redo();
    if (snapshot.has_value())
    {
        Restore(snapshot.value());
    }
}

void DocumentSession::SelectLayer(const std::optional<Guid>& id)
{
    if (id != _activeLayerID)
    {
        CommitTransform();
        _maskSelected = false;
    }
    if (!id.has_value() || FindLayer(id.value()) == nullptr)
    {
        _activeLayerID.reset();
        _selected.clear();
        return;
    }
    _activeLayerID = id;
    _selected = {id.value()};
}

void DocumentSession::CreateDocument(int width, int height, bool emptyLayer)
{
    if (width < 1 || width > kMaxCanvasSide || height < 1 || height > kMaxCanvasSide)
    {
        return;
    }
    _lassoDraft.reset();
    BeginEdit(L"New Canvas");
    CanvasDocument document;
    document.id = Guid::Generate();
    document.width = width;
    document.height = height;
    if (emptyLayer)
    {
        ImageLayer layer = ImageLayer::Blank(L"Layer 1", Vec2{static_cast<double>(width), static_cast<double>(height)});
        _activeLayerID = layer.id;
        document.layers.push_back(std::move(layer));
    }
    else
    {
        _activeLayerID.reset();
    }
    _document = std::move(document);
    _collapsed.clear();
    EndEdit();
}

void DocumentSession::AddBlankLayer()
{
    if (!_document.has_value())
    {
        return;
    }
    std::unordered_set<std::wstring> names;
    for (const ImageLayer& layer : _document.value().layers)
    {
        names.insert(layer.name);
    }
    int number = 1;
    std::wstring name;
    do
    {
        name = L"Layer " + std::to_wstring(number);
        ++number;
    } while (names.contains(name));
    ImageLayer layer = ImageLayer::Blank(
        name, Vec2{static_cast<double>(_document.value().width), static_cast<double>(_document.value().height)});
    const ImageLayer* active = ActiveLayer();
    if (active != nullptr)
    {
        layer.parentID = active->isGroup ? _activeLayerID : active->parentID;
    }
    if (layer.parentID.has_value())
    {
        _collapsed.erase(layer.parentID.value());
    }
    std::size_t insertion = _document.value().layers.size();
    if (_activeLayerID.has_value())
    {
        const std::optional<std::size_t> index = IndexOf(_document.value().layers, _activeLayerID.value());
        if (index.has_value())
        {
            insertion = index.value() + 1;
        }
    }
    if (active != nullptr && active->isGroup && _activeLayerID.has_value())
    {
        const Guid folder = _activeLayerID.value();
        std::unordered_map<Guid, std::optional<Guid>> parents;
        for (const ImageLayer& existing : _document.value().layers)
        {
            parents[existing.id] = existing.parentID;
        }
        const auto isInside = [&](const Guid& id)
        {
            std::optional<Guid> parent = parents[id];
            int steps = 0;
            while (parent.has_value() && steps < kMaxGroupDepth)
            {
                if (parent.value() == folder)
                {
                    return true;
                }
                parent = parents[parent.value()];
                ++steps;
            }
            return false;
        };
        for (std::size_t i = 0; i < _document.value().layers.size(); ++i)
        {
            if (isInside(_document.value().layers[i].id))
            {
                insertion = std::max(insertion, i + 1);
            }
        }
    }
    BeginEdit(L"New Blank Layer");
    _document.value().layers.insert(_document.value().layers.begin() + static_cast<std::ptrdiff_t>(insertion), layer);
    _activeLayerID = layer.id;
    EndEdit();
}

void DocumentSession::AddGroup()
{
    if (!_document.has_value() || static_cast<int>(_document.value().layers.size()) >= kMaxLayerCount)
    {
        return;
    }
    std::unordered_set<std::wstring> names;
    for (const ImageLayer& layer : _document.value().layers)
    {
        names.insert(layer.name);
    }
    int number = 1;
    std::wstring name;
    do
    {
        name = L"Folder " + std::to_wstring(number);
        ++number;
    } while (names.contains(name));
    ImageLayer group = ImageLayer::Blank(
        name, Vec2{static_cast<double>(_document.value().width), static_cast<double>(_document.value().height)});
    group.isGroup = true;
    const ImageLayer* active = ActiveLayer();
    group.parentID =
        (active != nullptr && active->isGroup) ? _activeLayerID : (active != nullptr ? active->parentID : std::nullopt);
    std::size_t insertion = _document.value().layers.size();
    if (_activeLayerID.has_value())
    {
        const std::optional<std::size_t> index = IndexOf(_document.value().layers, _activeLayerID.value());
        if (index.has_value())
        {
            insertion = index.value() + 1;
        }
    }
    std::vector<ImageLayer> layers = _document.value().layers;
    layers.insert(layers.begin() + static_cast<std::ptrdiff_t>(insertion), group);
    if (ValidateHierarchy(layers) != DocumentStatus::Ok)
    {
        return;
    }
    BeginEdit(L"New Folder");
    _document.value().layers = std::move(layers);
    _activeLayerID = group.id;
    if (group.parentID.has_value())
    {
        _collapsed.erase(group.parentID.value());
    }
    EndEdit();
}

void DocumentSession::RenameLayer(const Guid& id, std::wstring_view name)
{
    const std::wstring trimmed = TrimWide(name);
    if (trimmed.empty())
    {
        return;
    }
    ImageLayer* layer = MutableLayer(id);
    if (layer == nullptr)
    {
        return;
    }
    BeginEdit(L"Rename Layer");
    layer->name = trimmed;
    EndEdit();
}

void DocumentSession::ToggleVisibility(const Guid& id)
{
    ImageLayer* layer = MutableLayer(id);
    if (layer == nullptr)
    {
        return;
    }
    BeginEdit(layer->visible ? L"Hide Layer" : L"Show Layer");
    layer->visible = !layer->visible;
    EndEdit();
}

bool DocumentSession::CanPlaceLayer(const Guid& id, const std::optional<Guid>& parent) const
{
    if (!_document.has_value() || FindLayer(id) == nullptr)
    {
        return false;
    }
    if (!parent.has_value())
    {
        return true;
    }
    if (parent.value() == id)
    {
        return false;
    }
    if (DescendantIds(_document.value().layers, id).contains(parent.value()))
    {
        return false;
    }
    const ImageLayer* node = FindLayer(parent.value());
    return node != nullptr && node->isGroup;
}

bool DocumentSession::PlaceLayer(const Guid& id, const std::optional<Guid>& parent, const std::optional<Guid>& above,
                                 bool atBottom)
{
    const std::optional<Guid> destination = parent;
    const std::optional<Guid> aboveId = above;
    if (!CanPlaceLayer(id, destination) || !_document.has_value() || (aboveId.has_value() && aboveId.value() == id))
    {
        return false;
    }
    std::vector<ImageLayer> layers = _document.value().layers;
    const std::optional<std::size_t> index = IndexOf(layers, id);
    if (!index.has_value())
    {
        return false;
    }
    ImageLayer layer = layers[index.value()];
    layers.erase(layers.begin() + static_cast<std::ptrdiff_t>(index.value()));
    layer.parentID = destination;
    std::size_t insertion = atBottom ? 0 : layers.size();
    if (aboveId.has_value())
    {
        const std::optional<std::size_t> target = IndexOf(layers, aboveId.value());
        if (!target.has_value() || layers[target.value()].parentID != destination)
        {
            return false;
        }
        insertion = target.value() + 1;
    }
    layers.insert(layers.begin() + static_cast<std::ptrdiff_t>(insertion), std::move(layer));
    AdoptClipping(id, layers);
    ReleaseDetachedClipping(layers);
    if (ValidateHierarchy(layers) != DocumentStatus::Ok)
    {
        return false;
    }
    BeginEdit(L"Move Layer");
    _document.value().layers = std::move(layers);
    _activeLayerID = id;
    if (destination.has_value())
    {
        _collapsed.erase(destination.value());
    }
    EndEdit();
    return true;
}

void DocumentSession::MoveActiveLayerOutOfGroup()
{
    const ImageLayer* layer = ActiveLayer();
    if (layer == nullptr || !layer->parentID.has_value())
    {
        return;
    }
    const Guid layerId = layer->id;
    const Guid parentId = layer->parentID.value();
    const ImageLayer* group = FindLayer(parentId);
    if (group == nullptr)
    {
        return;
    }
    const std::optional<Guid> destination = group->parentID;
    const Guid above = group->id;
    (void)PlaceLayer(layerId, destination, above);
}

void DocumentSession::DeleteLayer(const Guid& id)
{
    if (!_document.has_value())
    {
        return;
    }
    const std::optional<std::size_t> index = IndexOf(_document.value().layers, id);
    if (!index.has_value())
    {
        return;
    }
    std::unordered_set<Guid> removed = DescendantIds(_document.value().layers, id);
    removed.insert(id);
    BeginEdit(L"Delete Layer");
    auto& layers = _document.value().layers;
    layers.erase(std::remove_if(layers.begin(), layers.end(),
                                [&](const ImageLayer& layer) { return removed.contains(layer.id); }),
                 layers.end());
    for (ImageLayer& layer : layers)
    {
        if (layer.maskSourceID.has_value() && removed.contains(layer.maskSourceID.value()))
        {
            layer.maskSourceID.reset();
        }
    }
    if (_activeLayerID.has_value() && removed.contains(_activeLayerID.value()))
    {
        if (layers.empty())
        {
            _activeLayerID.reset();
        }
        else
        {
            _activeLayerID = layers[std::min(index.value(), layers.size() - 1)].id;
        }
    }
    EndEdit();
}

void DocumentSession::SetOpacity(double opacity)
{
    ImageLayer* layer = ActiveLayer();
    if (layer == nullptr || layer->isGroup || !std::isfinite(opacity))
    {
        return;
    }
    BeginEdit(L"Layer Opacity");
    layer->opacity = std::min(1.0, std::max(0.0, opacity));
    EndEdit();
}

void DocumentSession::SetBlendMode(LayerBlendMode mode)
{
    ImageLayer* layer = ActiveLayer();
    if (layer == nullptr || layer->isGroup)
    {
        return;
    }
    BeginEdit(L"Layer Blend Mode");
    layer->blendMode = mode;
    EndEdit();
}

bool DocumentSession::CanLinkMask(const Guid& source, const Guid& target) const
{
    if (!_document.has_value() || source == target)
    {
        return false;
    }
    const ImageLayer* sourceLayer = FindLayer(source);
    const ImageLayer* targetLayer = FindLayer(target);
    if (sourceLayer == nullptr || targetLayer == nullptr || sourceLayer->isGroup ||
        sourceLayer->adjustment.has_value() || targetLayer->isGroup)
    {
        return false;
    }
    std::vector<ImageLayer> layers = _document.value().layers;
    ImageLayer* writable = LayerById(layers, target);
    if (writable == nullptr)
    {
        return false;
    }
    writable->maskSourceID = source;
    return ValidateLiveMaskGraph(layers) == DocumentStatus::Ok;
}

bool DocumentSession::LinkMask(const Guid& source, const Guid& target)
{
    if (!CanLinkMask(source, target))
    {
        return false;
    }
    ImageLayer* layer = MutableLayer(target);
    if (layer == nullptr)
    {
        return false;
    }
    if (layer->maskSourceID.has_value() && layer->maskSourceID.value() == source)
    {
        return true;
    }
    BeginEdit(L"Create Clipping Mask");
    layer->maskSourceID = source;
    EndEdit();
    return true;
}

std::vector<HierarchyEntry> DocumentSession::LayerRows() const
{
    if (!_document.has_value())
    {
        return {};
    }
    return HierarchyEntries(_document.value().layers, true, _collapsed);
}

void DocumentSession::AdoptClipping(const Guid& id, std::vector<ImageLayer>& layers)
{
    const ImageLayer* layer = LayerById(layers, id);
    if (layer == nullptr || layer->isGroup)
    {
        return;
    }
    std::vector<const ImageLayer*> siblings;
    for (const ImageLayer& candidate : layers)
    {
        if (candidate.parentID == layer->parentID)
        {
            siblings.push_back(&candidate);
        }
    }
    std::size_t index = 0;
    bool found = false;
    for (std::size_t i = 0; i < siblings.size(); ++i)
    {
        if (siblings[i]->id == id)
        {
            index = i;
            found = true;
            break;
        }
    }
    if (!found || index == 0 || index + 1 >= siblings.size() || !siblings[index + 1]->maskSourceID.has_value() ||
        siblings[index + 1]->maskSourceID.value() == id)
    {
        return;
    }
    const Guid source = siblings[index + 1]->maskSourceID.value();
    const ImageLayer* below = siblings[index - 1];
    if (below->id != source && below->maskSourceID != source)
    {
        return;
    }
    ImageLayer* writable = LayerById(layers, id);
    if (writable != nullptr)
    {
        writable->maskSourceID = source;
    }
}

void DocumentSession::ReleaseDetachedClipping(std::vector<ImageLayer>& layers)
{
    std::vector<std::size_t> roots;
    std::unordered_map<Guid, std::vector<std::size_t>> grouped;
    for (std::size_t i = 0; i < layers.size(); ++i)
    {
        if (layers[i].parentID.has_value())
        {
            grouped[layers[i].parentID.value()].push_back(i);
        }
        else
        {
            roots.push_back(i);
        }
    }
    std::unordered_set<Guid> release;
    const auto mark = [&](const std::vector<std::size_t>& stack)
    {
        std::optional<Guid> base;
        for (const std::size_t index : stack)
        {
            ImageLayer& layer = layers[index];
            if (layer.maskSourceID.has_value())
            {
                if (!base.has_value() || layer.maskSourceID.value() != base.value())
                {
                    release.insert(layer.id);
                    base = layer.id;
                }
            }
            else
            {
                base = layer.isGroup ? std::nullopt : std::optional<Guid>(layer.id);
            }
        }
    };
    mark(roots);
    for (const auto& stack : grouped)
    {
        mark(stack.second);
    }
    for (ImageLayer& layer : layers)
    {
        if (release.contains(layer.id))
        {
            layer.maskSourceID.reset();
        }
    }
}

double MaskEdgeBackground(const Raster& thumbnail) noexcept
{
    if (!thumbnail.HasStorage() || thumbnail.Format() != RasterFormat::Gray8 || thumbnail.Data() == nullptr)
    {
        return 1.0;
    }
    const int width = thumbnail.Width();
    const int height = thumbnail.Height();
    const int stride = thumbnail.Stride();
    const std::uint8_t* data = thumbnail.Data();
    std::uint64_t total = 0;
    std::uint64_t count = 0;
    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            if (y == 0 || y == height - 1 || x == 0 || x == width - 1)
            {
                total +=
                    data[static_cast<std::size_t>(y) * static_cast<std::size_t>(stride) + static_cast<std::size_t>(x)];
                ++count;
            }
        }
    }
    return total * 2 >= count * 255 ? 1.0 : 0.0;
}

// ── Transform editing (C8) ───────────────────────────────────────────────

namespace
{
constexpr double kTransformPi = 3.14159265358979323846;

[[nodiscard]] std::unordered_set<Guid> EffectiveVisibleIDs(const CanvasDocument& document)
{
    std::unordered_set<Guid> ids;
    for (const HierarchyEntry& entry : HierarchyEntries(document.layers))
    {
        if (entry.visible)
        {
            ids.insert(entry.id);
        }
    }
    return ids;
}

struct CornerBounds
{
    double minX = 0;
    double minY = 0;
    double maxX = 0;
    double maxY = 0;
};

[[nodiscard]] CornerBounds BoundsOfCorners(const std::array<Vec2, 4>& corners) noexcept
{
    CornerBounds bounds{corners[0].x, corners[0].y, corners[0].x, corners[0].y};
    for (const Vec2& corner : corners)
    {
        bounds.minX = std::min(bounds.minX, corner.x);
        bounds.minY = std::min(bounds.minY, corner.y);
        bounds.maxX = std::max(bounds.maxX, corner.x);
        bounds.maxY = std::max(bounds.maxY, corner.y);
    }
    return bounds;
}
} // namespace

Affine2 Affine2::Rotation(double radians) noexcept
{
    const double s = std::sin(radians);
    const double co = std::cos(radians);
    return Affine2{co, s, -s, co, 0, 0};
}

Affine2 Affine2::Concat(const Affine2& o) const noexcept
{
    return Affine2{a * o.a + b * o.c, a * o.b + b * o.d,          c * o.a + d * o.c,
                   c * o.b + d * o.d, tx * o.a + ty * o.c + o.tx, tx * o.b + ty * o.d + o.ty};
}

Affine2 Affine2::Inverted() const noexcept
{
    const double determinant = a * d - b * c;
    if (!std::isfinite(determinant) || std::abs(determinant) < 1e-300)
    {
        return Affine2{};
    }
    Affine2 result;
    result.a = d / determinant;
    result.b = -b / determinant;
    result.c = -c / determinant;
    result.d = a / determinant;
    result.tx = (c * ty - d * tx) / determinant;
    result.ty = (b * tx - a * ty) / determinant;
    return result;
}

double LayerTransform::Radians() const noexcept
{
    return std::fmod(rotation, 360.0) * kTransformPi / 180.0;
}

Vec2 LayerTransform::Point(Vec2 unit) const noexcept
{
    const double x = (unit.x - 0.5) * size.x;
    const double y = (unit.y - 0.5) * size.y;
    const double radians = Radians();
    const Vec2 center = Center();
    return Vec2{center.x + x * std::cos(radians) - y * std::sin(radians),
                center.y + x * std::sin(radians) + y * std::cos(radians)};
}

bool LayerTransform::Contains(Vec2 point) const noexcept
{
    const Vec2 center = Center();
    const double x = point.x - center.x;
    const double y = point.y - center.y;
    const double radians = Radians();
    return std::abs(x * std::cos(radians) + y * std::sin(radians)) <= size.x / 2.0 &&
           std::abs(-x * std::sin(radians) + y * std::cos(radians)) <= size.y / 2.0;
}

std::array<Vec2, 4> LayerTransform::Corners() const noexcept
{
    return {Point(Vec2{0, 0}), Point(Vec2{1, 0}), Point(Vec2{1, 1}), Point(Vec2{0, 1})};
}

double LayerTransform::ScalePercent(Vec2 pixelSize) const noexcept
{
    return size.x / std::max(1.0, pixelSize.x) * 100.0;
}

LayerTransform LayerTransform::ScaledToPercent(double percent, Vec2 pixelSize) const noexcept
{
    LayerTransform result = *this;
    const Vec2 center = Center();
    result.size = Vec2{pixelSize.x * percent / 100.0, pixelSize.y * percent / 100.0};
    result.origin = Vec2{center.x - result.size.x / 2.0, center.y - result.size.y / 2.0};
    return result;
}

LayerTransform LayerTransform::Rounded() const noexcept
{
    LayerTransform result = *this;
    result.origin = Vec2{std::round(origin.x), std::round(origin.y)};
    result.size = Vec2{std::max(1.0, std::round(size.x)), std::max(1.0, std::round(size.y))};
    result.rotation = std::round(rotation);
    return result;
}

Affine2 LayerTransform::UnitToDocument() const noexcept
{
    const Vec2 center = Center();
    return Affine2::Translation(-0.5, -0.5)
        .Concat(Affine2::Scale(size.x * (flipX ? -1.0 : 1.0), size.y * (flipY ? -1.0 : 1.0)))
        .Concat(Affine2::Rotation(Radians()))
        .Concat(Affine2::Translation(center.x, center.y));
}

LayerTransform LayerTransform::Placing(const Affine2& map) const noexcept
{
    // Keep this transform's horizontal flip and the rotation nearest its own, so the numbers stay familiar.
    const double sign = flipX ? -1.0 : 1.0;
    const double angle = std::atan2(map.b * sign, map.a * sign);
    const double along = -map.c * std::sin(angle) + map.d * std::cos(angle);
    const Vec2 middle = map.Apply(Vec2{0.5, 0.5});
    LayerTransform result = *this;
    result.size = Vec2{std::hypot(map.a, map.b), std::abs(along)};
    const double degrees = angle * 180.0 / kTransformPi;
    result.rotation = degrees + std::round((rotation - degrees) / 360.0) * 360.0;
    result.flipY = along < 0;
    result.origin = Vec2{middle.x - result.size.x / 2.0, middle.y - result.size.y / 2.0};
    return result;
}

LayerTransform LayerTransform::Following(const LayerTransform& from, const LayerTransform& to) const noexcept
{
    if (from == to)
    {
        return *this;
    }
    // A plain move carries exactly.
    if (from.size == to.size && from.rotation == to.rotation && from.flipX == to.flipX && from.flipY == to.flipY)
    {
        LayerTransform moved = *this;
        moved.origin.x += to.origin.x - from.origin.x;
        moved.origin.y += to.origin.y - from.origin.y;
        return moved;
    }
    return Placing(UnitToDocument().Concat(from.UnitToDocument().Inverted()).Concat(to.UnitToDocument()));
}

bool LayerTransform::SamePlacement(const LayerTransform& other) const noexcept
{
    LayerTransform copy = *this;
    copy.sampling = other.sampling;
    return copy == other;
}

LayerTransform LayerTransform::Mirrored(bool horizontally, double axis) const noexcept
{
    LayerTransform result = *this;
    const Vec2 center = Center();
    if (horizontally)
    {
        result.flipX = !flipX;
        result.origin.x = 2.0 * axis - center.x - size.x / 2.0;
    }
    else
    {
        result.flipY = !flipY;
        result.origin.y = 2.0 * axis - center.y - size.y / 2.0;
    }
    result.rotation = -rotation;
    return result;
}

std::optional<LayerTransform> MaskPlacementMoving(const LayerMask& mask, const LayerTransform& from,
                                                  const LayerTransform& to) noexcept
{
    // A uniform mask looks the same wherever it sits.
    if (mask.asset.image.Width() <= 1 && mask.asset.image.Height() <= 1)
    {
        return std::nullopt;
    }
    std::optional<LayerTransform> moved;
    if (mask.linked)
    {
        if (mask.placement.has_value())
        {
            moved = mask.placement.value().Following(from, to);
        }
    }
    else
    {
        moved = mask.placement.has_value() ? mask.placement.value() : from;
    }
    if (!moved.has_value() || moved.value().SamePlacement(to))
    {
        return std::nullopt;
    }
    return moved;
}

bool DocumentSession::CanTransform() const noexcept
{
    if (!_document.has_value())
    {
        return false;
    }
    if (TransformsAsGroup())
    {
        return !GroupTransformMemberIDs().empty();
    }
    const ImageLayer* layer = ActiveLayer();
    if (layer == nullptr || !layer->asset.has_value() || layer->isGroup)
    {
        return false;
    }
    return EffectiveVisibleIDs(_document.value()).contains(layer->id);
}

bool DocumentSession::TransformsAsGroup() const noexcept
{
    if (_selected.size() > 1)
    {
        return true;
    }
    const ImageLayer* layer = ActiveLayer();
    return _selected.size() == 1 && layer != nullptr && layer->isGroup;
}

std::vector<Guid> DocumentSession::GroupTransformMemberIDs() const
{
    std::vector<Guid> ids;
    if (!TransformsAsGroup() || !_document.has_value())
    {
        return ids;
    }
    const CanvasDocument& document = _document.value();
    std::unordered_map<Guid, std::optional<Guid>> parents;
    for (const ImageLayer& layer : document.layers)
    {
        parents[layer.id] = layer.parentID;
    }
    const std::unordered_set<Guid> visible = EffectiveVisibleIDs(document);
    for (const ImageLayer& layer : document.layers)
    {
        if (!layer.asset.has_value() || layer.isGroup || !visible.contains(layer.id))
        {
            continue;
        }
        std::optional<Guid> current = layer.id;
        for (int depth = 0; depth < 64 && current.has_value(); ++depth)
        {
            if (_selected.contains(current.value()))
            {
                ids.push_back(layer.id);
                break;
            }
            const auto parent = parents.find(current.value());
            current = parent == parents.end() ? std::nullopt : parent->second;
        }
    }
    return ids;
}

std::optional<LayerTransform> DocumentSession::GroupTransformBox() const
{
    std::optional<CornerBounds> bounds;
    for (const Guid& id : GroupTransformMemberIDs())
    {
        const ImageLayer* layer = FindLayer(id);
        if (layer == nullptr)
        {
            continue;
        }
        const CornerBounds layerBounds = BoundsOfCorners(layer->transform.Corners());
        if (!bounds.has_value())
        {
            bounds = layerBounds;
            continue;
        }
        bounds->minX = std::min(bounds->minX, layerBounds.minX);
        bounds->minY = std::min(bounds->minY, layerBounds.minY);
        bounds->maxX = std::max(bounds->maxX, layerBounds.maxX);
        bounds->maxY = std::max(bounds->maxY, layerBounds.maxY);
    }
    if (!bounds.has_value())
    {
        return std::nullopt;
    }
    LayerTransform box;
    box.origin = Vec2{bounds->minX, bounds->minY};
    box.size = Vec2{std::max(1.0, bounds->maxX - bounds->minX), std::max(1.0, bounds->maxY - bounds->minY)};
    return box;
}

void DocumentSession::BeginTransform(bool persistent)
{
    if (_transformEdit.has_value() || !CanTransform())
    {
        return;
    }
    const ImageLayer* layer = ActiveLayer();
    if (layer == nullptr)
    {
        return;
    }
    if (TransformsAsGroup())
    {
        const std::optional<LayerTransform> box = GroupTransformBox();
        if (!box.has_value())
        {
            return;
        }
        TransformGroup group;
        group.box = box.value();
        for (const Guid& id : GroupTransformMemberIDs())
        {
            if (const ImageLayer* member = FindLayer(id))
            {
                group.originals.emplace_back(id, member->transform);
            }
        }
        _transformEdit = TransformEdit{layer->id, box.value(), persistent, std::nullopt, false, std::move(group)};
    }
    else
    {
        // An unlinked mask, when selected, transforms on its own; linked, layer and mask move together.
        const bool maskAlone = _maskSelected && layer->mask.has_value() && !layer->mask.value().linked;
        _transformEdit = TransformEdit{layer->id,  maskAlone ? MaskTransform(*layer) : layer->transform,
                                       persistent, std::nullopt,
                                       maskAlone,  std::nullopt};
    }
    ++_editRevision;
}

void DocumentSession::PreviewTransform(const LayerTransform& value)
{
    if (!value.IsValid() || !_transformEdit.has_value())
    {
        return;
    }
    _transformEdit->draft = value;
    ++_editRevision;
}

void DocumentSession::BeginDistort()
{
    if (!_transformEdit.has_value() || _transformEdit->corners.has_value() || !_transformEdit->draft.IsValid())
    {
        return;
    }
    _transformEdit->corners = _transformEdit->draft.Corners();
    _transformEdit->persistent = true;
    ++_editRevision;
}

void DocumentSession::PreviewCorners(const std::array<Vec2, 4>& corners)
{
    if (!_transformEdit.has_value() || !_transformEdit->corners.has_value() || !DistortWarp::IsUsable(corners))
    {
        return;
    }
    _transformEdit->corners = corners;
    ++_editRevision;
}

void DocumentSession::BeginDuplicateTransform()
{
    // Alt-drag duplicates a single layer; several selected, or a folder, just move.
    if (_transformDuplicate.has_value() || TransformsAsGroup() || !_activeLayerID.has_value())
    {
        return;
    }
    const Guid source = _activeLayerID.value();
    CommitTransform();
    if (!CanTransform())
    {
        return;
    }
    BeginEdit(L"Duplicate Layer");
    DuplicateActiveLayer();
    if (!_activeLayerID.has_value() || _activeLayerID.value() == source)
    {
        EndEdit();
        return;
    }
    _transformDuplicate = std::make_pair(_activeLayerID.value(), source);
    BeginTransform(false);
}

void DocumentSession::CommitTransform()
{
    _snapGuides = {};
    if (!_transformEdit.has_value())
    {
        return;
    }
    const TransformEdit edit = _transformEdit.value();
    _transformEdit.reset();
    _displayCopy.reset();
    ++_editRevision;
    const auto finishDuplicate = [this]
    {
        if (_transformDuplicate.has_value())
        {
            _transformDuplicate.reset();
            EndEdit();
        }
    };
    if (!_document.has_value())
    {
        finishDuplicate();
        return;
    }
    std::vector<ImageLayer>& layers = _document.value().layers;
    if (edit.mask)
    {
        const std::optional<std::size_t> index = IndexOf(layers, edit.layerID);
        if (edit.draft.IsValid() && index.has_value() && layers[index.value()].mask.has_value())
        {
            ImageLayer& layer = layers[index.value()];
            if (edit.corners.has_value())
            {
                // A distorted mask is resampled into the shape, its background outside it.
                const std::optional<DistortWarp::Warped> moved =
                    DistortWarp::WarpMask(layer.mask->asset.image, edit.draft, edit.corners.value(),
                                          MaskEdgeBackground(layer.mask->asset.thumbnail));
                if (moved.has_value())
                {
                    BeginEdit(L"Distort Layer Mask");
                    if (!(moved->image == layer.mask->asset.image))
                    {
                        layer.mask->asset.image = moved->image;
                        layer.mask->asset.thumbnail = Raster::MakeThumbnail(moved->image);
                    }
                    layer.mask->placement = moved->transform.SamePlacement(layer.transform)
                                                ? std::nullopt
                                                : std::optional<LayerTransform>(moved->transform);
                    EndEdit();
                }
            }
            else
            {
                const std::optional<LayerTransform> placement = edit.draft.SamePlacement(layer.transform)
                                                                    ? std::nullopt
                                                                    : std::optional<LayerTransform>(edit.draft);
                if (placement != layer.mask->placement)
                {
                    BeginEdit(L"Transform Layer Mask");
                    layer.mask->placement = placement;
                    EndEdit();
                }
            }
        }
        finishDuplicate();
        return;
    }
    if (edit.corners.has_value())
    {
        BeginEdit(edit.group.has_value() ? L"Distort Layers" : L"Distort");
        std::vector<Guid> ids;
        if (edit.group.has_value())
        {
            for (const auto& [id, original] : edit.group->originals)
            {
                ids.push_back(id);
            }
        }
        else
        {
            ids.push_back(edit.layerID);
        }
        for (const Guid& id : ids)
        {
            const std::optional<std::size_t> index = IndexOf(layers, id);
            if (!index.has_value() || !layers[index.value()].asset.has_value())
            {
                continue;
            }
            ImageLayer& layer = layers[index.value()];
            LayerTransform transform = edit.draft;
            std::array<Vec2, 4> corners = edit.corners.value();
            if (edit.group.has_value())
            {
                const auto original = std::find_if(edit.group->originals.begin(), edit.group->originals.end(),
                                                   [&](const auto& entry) { return entry.first == id; });
                if (original == edit.group->originals.end())
                {
                    continue;
                }
                transform = original->second.Following(edit.group->box, edit.draft);
                corners = DistortWarp::Carried(transform, edit.draft, edit.corners.value());
                if (!DistortWarp::IsUsable(corners))
                {
                    continue;
                }
            }
            const std::optional<DistortWarp::WarpedTrimmed> warped =
                DistortWarp::WarpTrimmed(layer.asset->image, transform, corners);
            if (!warped.has_value())
            {
                continue;
            }
            if (layer.mask.has_value() && !layer.mask->placement.has_value() && layer.mask->linked)
            {
                // A uniform mask passes through; any other is warped and cropped with the pixels.
                const std::optional<DistortWarp::Warped> warpedMask =
                    DistortWarp::Warp(layer.mask->asset.image, transform, corners, true);
                if (warpedMask.has_value() && !(warpedMask->image == layer.mask->asset.image))
                {
                    const Raster& full = warpedMask->image;
                    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(warped->cropWidth) *
                                                    static_cast<std::size_t>(warped->cropHeight));
                    for (int y = 0; y < warped->cropHeight; ++y)
                    {
                        const int sourceY = std::clamp(warped->cropY + y, 0, full.Height() - 1);
                        for (int x = 0; x < warped->cropWidth; ++x)
                        {
                            const int sourceX = std::clamp(warped->cropX + x, 0, full.Width() - 1);
                            bytes[static_cast<std::size_t>(y) * static_cast<std::size_t>(warped->cropWidth) +
                                  static_cast<std::size_t>(x)] =
                                full.Data()[static_cast<std::size_t>(sourceY) *
                                                static_cast<std::size_t>(full.Stride()) +
                                            static_cast<std::size_t>(sourceX)];
                        }
                    }
                    Raster cropped = Raster::Make(warped->cropWidth, warped->cropHeight, RasterFormat::Gray8,
                                                  bytes.data(), bytes.size());
                    if (cropped.HasStorage())
                    {
                        layer.mask->asset.image = cropped;
                        layer.mask->asset.thumbnail = Raster::MakeThumbnail(cropped);
                    }
                }
            }
            layer.asset->image = warped->image;
            layer.asset->thumbnail = Raster::MakeThumbnail(warped->image);
            layer.transform = warped->transform;
        }
        EndEdit();
        finishDuplicate();
        return;
    }
    if (edit.group.has_value())
    {
        if (edit.draft.IsValid())
        {
            BeginEdit(L"Transform Layers");
            for (const auto& [id, original] : edit.group->originals)
            {
                const std::optional<std::size_t> index = IndexOf(layers, id);
                if (!index.has_value())
                {
                    continue;
                }
                const LayerTransform moved = original.Following(edit.group->box, edit.draft);
                if (!moved.IsValid())
                {
                    continue;
                }
                ImageLayer& layer = layers[index.value()];
                if (layer.mask.has_value())
                {
                    layer.mask->placement = MaskPlacementMoving(layer.mask.value(), original, moved);
                }
                layer.transform = moved;
            }
            EndEdit();
        }
        finishDuplicate();
        return;
    }
    const std::optional<std::size_t> index = IndexOf(layers, edit.layerID);
    if (edit.draft.IsValid() && index.has_value())
    {
        BeginEdit(L"Transform Layer");
        ImageLayer& layer = layers[index.value()];
        if (layer.mask.has_value())
        {
            layer.mask->placement = MaskPlacementMoving(layer.mask.value(), layer.transform, edit.draft);
        }
        layer.transform = edit.draft;
        EndEdit();
    }
    finishDuplicate();
}

void DocumentSession::CancelTransform()
{
    _snapGuides = {};
    if (!_transformEdit.has_value())
    {
        return;
    }
    _transformEdit.reset();
    _displayCopy.reset();
    ++_editRevision;
    if (_transformDuplicate.has_value() && _document.has_value())
    {
        const auto [copy, source] = _transformDuplicate.value();
        std::vector<ImageLayer>& layers = _document.value().layers;
        layers.erase(
            std::remove_if(layers.begin(), layers.end(), [&](const ImageLayer& layer) { return layer.id == copy; }),
            layers.end());
        _activeLayerID = source;
        _selected = {source};
        _transformDuplicate.reset();
        EndEdit();
    }
}

void DocumentSession::NudgeLayer(double dx, double dy)
{
    const bool alreadyEditing = _transformEdit.has_value();
    if (!alreadyEditing)
    {
        BeginTransform(false);
    }
    if (!_transformEdit.has_value())
    {
        return;
    }
    LayerTransform value = _transformEdit->draft;
    value.origin.x += dx;
    value.origin.y += dy;
    PreviewTransform(value);
    if (_transformEdit->corners.has_value())
    {
        std::array<Vec2, 4> corners = _transformEdit->corners.value();
        for (Vec2& corner : corners)
        {
            corner.x += dx;
            corner.y += dy;
        }
        PreviewCorners(corners);
    }
    if (!alreadyEditing)
    {
        CommitTransform();
    }
}

LayerTransform DocumentSession::EditedTransform(const ImageLayer& layer) const
{
    if (_transformEdit.has_value() && _transformEdit->layerID == layer.id)
    {
        return _transformEdit->draft;
    }
    if (!_transformEdit.has_value() && _activeLayerID == layer.id && TransformsAsGroup())
    {
        if (const std::optional<LayerTransform> box = GroupTransformBox())
        {
            return box.value();
        }
    }
    return _activeLayerID == layer.id && TransformTargetsMask() ? MaskTransform(layer) : layer.transform;
}

std::optional<LayerTransform> DocumentSession::PendingTransform(const ImageLayer& layer) const
{
    if (!_transformEdit.has_value() || _transformEdit->mask)
    {
        return std::nullopt;
    }
    if (_transformEdit->group.has_value())
    {
        for (const auto& [id, original] : _transformEdit->group->originals)
        {
            if (id == layer.id)
            {
                return original.Following(_transformEdit->group->box, _transformEdit->draft);
            }
        }
        return std::nullopt;
    }
    return _transformEdit->layerID == layer.id ? std::optional<LayerTransform>(_transformEdit->draft) : std::nullopt;
}

LayerTransform DocumentSession::DisplayedTransform(const ImageLayer& layer) const
{
    const std::optional<LayerTransform> pending = PendingTransform(layer);
    return pending.has_value() ? pending.value() : layer.transform;
}

std::optional<LayerTransform> DocumentSession::DisplayedMaskPlacement(const ImageLayer& layer) const
{
    if (!_transformEdit.has_value())
    {
        return std::nullopt;
    }
    if (_transformEdit->mask)
    {
        return _transformEdit->layerID == layer.id ? std::optional<LayerTransform>(_transformEdit->draft)
                                                   : std::nullopt;
    }
    const std::optional<LayerTransform> pending = PendingTransform(layer);
    if (!pending.has_value() || !layer.mask.has_value())
    {
        return std::nullopt;
    }
    return MaskPlacementMoving(layer.mask.value(), layer.transform, pending.value());
}

std::optional<Vec2> DocumentSession::TransformPixelSize() const
{
    if (_transformEdit.has_value() && _transformEdit->group.has_value())
    {
        return _transformEdit->group->box.size;
    }
    if (!_transformEdit.has_value() && TransformsAsGroup())
    {
        const std::optional<LayerTransform> box = GroupTransformBox();
        return box.has_value() ? std::optional<Vec2>(box->size) : std::nullopt;
    }
    if (TransformTargetsMask())
    {
        return std::nullopt;
    }
    const ImageLayer* layer = ActiveLayer();
    if (layer == nullptr || !layer->asset.has_value() || !layer->asset->image.HasStorage())
    {
        return std::nullopt;
    }
    return Vec2{static_cast<double>(layer->asset->image.Width()), static_cast<double>(layer->asset->image.Height())};
}

bool DocumentSession::TransformTargetsMask() const noexcept
{
    if (_transformEdit.has_value())
    {
        return _transformEdit->mask;
    }
    const ImageLayer* layer = ActiveLayer();
    return _maskSelected && layer != nullptr && layer->mask.has_value() && !layer->mask->linked;
}

SnapTargets DocumentSession::TransformSnapTargets(const std::unordered_set<Guid>& excluding) const
{
    SnapTargets targets;
    if (!_document.has_value())
    {
        return targets;
    }
    const CanvasDocument& document = _document.value();
    const double width = static_cast<double>(document.width);
    const double height = static_cast<double>(document.height);
    targets.xs = {0.0, width / 2.0, width};
    targets.ys = {0.0, height / 2.0, height};
    for (const Guid& id : RenderLayerIds(document))
    {
        const ImageLayer* layer = FindLayer(id);
        if (layer == nullptr || !layer->asset.has_value() || excluding.contains(id))
        {
            continue;
        }
        const CornerBounds bounds = BoundsOfCorners(DisplayedTransform(*layer).Corners());
        targets.xs.insert(targets.xs.end(), {std::round(bounds.minX), std::round((bounds.minX + bounds.maxX) / 2.0),
                                             std::round(bounds.maxX)});
        targets.ys.insert(targets.ys.end(), {std::round(bounds.minY), std::round((bounds.minY + bounds.maxY) / 2.0),
                                             std::round(bounds.maxY)});
    }
    return targets;
}

LayerTransform DocumentSession::SnappedMove(const LayerTransform& draft, const std::unordered_set<Guid>& moving,
                                            double tolerance)
{
    const CornerBounds bounds = BoundsOfCorners(draft.Corners());
    const SnapTargets targets = TransformSnapTargets(moving);
    const TransformSnap::Result snap =
        TransformSnap::Offset(bounds.minX, bounds.minY, bounds.maxX, bounds.maxY, targets.xs, targets.ys, tolerance);
    _snapGuides = {};
    if (snap.x.has_value())
    {
        _snapGuides.xs.push_back(snap.x.value());
    }
    if (snap.y.has_value())
    {
        _snapGuides.ys.push_back(snap.y.value());
    }
    if (snap.offset == Vec2{})
    {
        return draft;
    }
    LayerTransform snapped = draft;
    snapped.origin.x += snap.offset.x;
    snapped.origin.y += snap.offset.y;
    return snapped;
}

void DocumentSession::FlipLayers(bool horizontally)
{
    CommitTransform();
    if (!CanTransform() || !_document.has_value())
    {
        return;
    }
    std::vector<Guid> members;
    double axis = 0;
    if (TransformsAsGroup())
    {
        const std::optional<LayerTransform> box = GroupTransformBox();
        if (!box.has_value())
        {
            return;
        }
        members = GroupTransformMemberIDs();
        axis = horizontally ? box->Center().x : box->Center().y;
    }
    else
    {
        const ImageLayer* layer = ActiveLayer();
        if (layer == nullptr)
        {
            return;
        }
        members = {layer->id};
        axis = horizontally ? layer->transform.Center().x : layer->transform.Center().y;
    }
    if (members.empty())
    {
        return;
    }
    const std::unordered_set<Guid> ids(members.begin(), members.end());
    BeginEdit(horizontally ? L"Flip Horizontal" : L"Flip Vertical");
    for (ImageLayer& layer : _document.value().layers)
    {
        if (!ids.contains(layer.id))
        {
            continue;
        }
        const LayerTransform flipped = layer.transform.Mirrored(horizontally, axis);
        if (layer.mask.has_value())
        {
            layer.mask->placement = MaskPlacementMoving(layer.mask.value(), layer.transform, flipped);
        }
        layer.transform = flipped;
    }
    EndEdit();
}

void DocumentSession::FlipCanvas(bool horizontally)
{
    CommitTransform();
    if (!_document.has_value())
    {
        return;
    }
    CanvasDocument& document = _document.value();
    const double axis = horizontally ? document.width / 2.0 : document.height / 2.0;
    BeginEdit(horizontally ? L"Flip Canvas Horizontal" : L"Flip Canvas Vertical");
    for (ImageLayer& layer : document.layers)
    {
        layer.transform = layer.transform.Mirrored(horizontally, axis);
        if (layer.mask.has_value() && layer.mask->placement.has_value())
        {
            layer.mask->placement = layer.mask->placement->Mirrored(horizontally, axis);
        }
    }
    if (document.selection.has_value())
    {
        // Mirrored points run the other way round; the nonzero rule does not mind.
        for (SelectionLoop& loop : document.selection->loops)
        {
            for (Vec2& point : loop)
            {
                if (horizontally)
                {
                    point.x = document.width - point.x;
                }
                else
                {
                    point.y = document.height - point.y;
                }
            }
        }
    }
    EndEdit();
}

void DocumentSession::DuplicateActiveLayer()
{
    if (!_document.has_value())
    {
        return;
    }
    const ImageLayer* active = ActiveLayer();
    if (active == nullptr || active->isGroup)
    {
        return;
    }
    std::vector<ImageLayer>& layers = _document.value().layers;
    const std::optional<std::size_t> index = IndexOf(layers, active->id);
    if (!index.has_value())
    {
        return;
    }
    ImageLayer copy = *active;
    copy.id = Guid::Generate();
    copy.name = active->name + L" copy";
    BeginEdit(L"Duplicate Layer");
    layers.insert(layers.begin() + static_cast<std::ptrdiff_t>(index.value()) + 1, copy);
    _activeLayerID = copy.id;
    _selected = {copy.id};
    _maskSelected = false;
    EndEdit();
}

void DocumentSession::SelectLayerTarget(const Guid& id, bool mask)
{
    SelectLayer(id);
    const ImageLayer* layer = ActiveLayer();
    _maskSelected = mask && layer != nullptr && layer->mask.has_value();
}

void DocumentSession::ToggleMaskLink(const Guid& id)
{
    ImageLayer* layer = MutableLayer(id);
    if (layer == nullptr || !layer->mask.has_value())
    {
        return;
    }
    CommitTransform();
    BeginEdit(layer->mask->linked ? L"Unlink Layer Mask" : L"Link Layer Mask");
    layer->mask->linked = !layer->mask->linked;
    EndEdit();
}

const CanvasDocument* DocumentSession::DisplayDocument() const
{
    if (!_document.has_value())
    {
        return nullptr;
    }
    if (!_transformEdit.has_value())
    {
        _displayCopy.reset();
        return &_document.value();
    }
    if (_displayCopy.has_value() && _displayRevision == _editRevision)
    {
        return &_displayCopy.value();
    }
    _displayCopy = _document;
    const TransformEdit& edit = _transformEdit.value();
    for (ImageLayer& layer : _displayCopy->layers)
    {
        const std::optional<LayerTransform> placement = DisplayedMaskPlacement(layer);
        const std::optional<LayerTransform> pending = PendingTransform(layer);
        if (pending.has_value())
        {
            bool warped = false;
            if (edit.corners.has_value() && layer.asset.has_value())
            {
                std::array<Vec2, 4> corners = edit.corners.value();
                if (edit.group.has_value())
                {
                    corners = DistortWarp::Carried(pending.value(), edit.draft, edit.corners.value());
                }
                if (DistortWarp::IsUsable(corners))
                {
                    // Preview at a bounded size; Apply resamples at full resolution.
                    const std::optional<DistortWarp::Warped> preview =
                        DistortWarp::Warp(layer.asset->image, pending.value(), corners, false, 1024.0);
                    if (preview.has_value())
                    {
                        layer.asset->image = preview->image;
                        layer.transform = preview->transform;
                        if (layer.mask.has_value() && !layer.mask->placement.has_value() && layer.mask->linked)
                        {
                            const std::optional<DistortWarp::Warped> mask =
                                DistortWarp::WarpMask(layer.mask->asset.image, pending.value(), corners,
                                                      MaskEdgeBackground(layer.mask->asset.thumbnail));
                            if (mask.has_value())
                            {
                                layer.mask->asset.image = mask->image;
                                layer.mask->placement = mask->transform.SamePlacement(layer.transform)
                                                            ? std::nullopt
                                                            : std::optional<LayerTransform>(mask->transform);
                            }
                        }
                        warped = true;
                    }
                }
            }
            if (!warped)
            {
                layer.transform = pending.value();
                if (layer.mask.has_value())
                {
                    layer.mask->placement = placement;
                }
            }
        }
        else if (placement.has_value() && layer.mask.has_value())
        {
            layer.mask->placement = placement;
        }
    }
    _displayRevision = _editRevision;
    return &_displayCopy.value();
}
