#include "ProjectStore.h"

#include "Utf8.h"
#include "ZipOwner.h"

#include <algorithm>
#include <array>
#include <climits>
#include <cmath>
#include <cstring>
#include <cwctype>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>
#include <windows.h>
#include <yyjson.h>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/resource.h>
#pragma warning(pop)

using unique_yyjson_doc = wil::unique_any<yyjson_doc*, decltype(&yyjson_doc_free), yyjson_doc_free>;
using unique_yyjson_mut_doc = wil::unique_any<yyjson_mut_doc*, decltype(&yyjson_mut_doc_free), yyjson_mut_doc_free>;

namespace
{
constexpr char kFormatId[] = "com.redprism.project";
constexpr std::size_t kMaxManifestBytes = 4u * 1024u * 1024u;
constexpr DWORD kInvalidAttributes = INVALID_FILE_ATTRIBUTES;

struct LayerRecord
{
    ImageLayer layer;
    std::string imageFile;
    std::string maskFile;
};

struct Manifest
{
    int version = 0;
    CanvasDocument document;
    std::optional<Guid> activeLayerID;
    std::vector<LayerRecord> records;
};

[[nodiscard]] std::wstring FullPath(const wchar_t* path)
{
    if (path == nullptr || path[0] == L'\0')
    {
        return {};
    }
    const DWORD needed = GetFullPathNameW(path, 0, nullptr, nullptr);
    if (needed == 0)
    {
        return {};
    }
    std::wstring full(static_cast<std::size_t>(needed), L'\0');
    const DWORD written = GetFullPathNameW(path, needed, full.data(), nullptr);
    if (written == 0)
    {
        return {};
    }
    full.resize(written);
    return full;
}

[[nodiscard]] bool HasReparse(const wchar_t* path) noexcept
{
    const DWORD attributes = GetFileAttributesW(path);
    return attributes != kInvalidAttributes && (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
}

[[nodiscard]] bool IsDirectory(const wchar_t* path) noexcept
{
    const DWORD attributes = GetFileAttributesW(path);
    return attributes != kInvalidAttributes && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

[[nodiscard]] bool IsFile(const wchar_t* path) noexcept
{
    const DWORD attributes = GetFileAttributesW(path);
    return attributes != kInvalidAttributes && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

[[nodiscard]] ProjectIoStatus ReadFileBytes(const wchar_t* path, std::size_t maximum, std::vector<std::uint8_t>& bytes)
{
    bytes.clear();
    if (path == nullptr || HasReparse(path))
    {
        return ProjectIoStatus::Invalid;
    }
    WIN32_FILE_ATTRIBUTE_DATA info{};
    if (!GetFileAttributesExW(path, GetFileExInfoStandard, &info) ||
        (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
    {
        return ProjectIoStatus::Invalid;
    }
    ULARGE_INTEGER size{};
    size.HighPart = info.nFileSizeHigh;
    size.LowPart = info.nFileSizeLow;
    if (size.QuadPart > maximum)
    {
        return ProjectIoStatus::TooLarge;
    }
    const HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                    FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (file == INVALID_HANDLE_VALUE)
    {
        return ProjectIoStatus::Invalid;
    }
    wil::unique_hfile owned{file};
    BY_HANDLE_FILE_INFORMATION handleInfo{};
    if (GetFileInformationByHandle(owned.get(), &handleInfo) &&
        (handleInfo.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
    {
        return ProjectIoStatus::Invalid;
    }
    bytes.resize(static_cast<std::size_t>(size.QuadPart));
    if (!bytes.empty())
    {
        DWORD read = 0;
        if (!ReadFile(owned.get(), bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr) ||
            read != bytes.size())
        {
            bytes.clear();
            return ProjectIoStatus::Invalid;
        }
    }
    return ProjectIoStatus::Ok;
}

// Shares ImageCodecs' file-write primitive (CreateFile/WriteFile/FlushFileBuffers) so there is one implementation
// of "write these bytes durably" in the codebase; this wrapper just keeps ProjectStore's own status enum.
[[nodiscard]] ProjectIoStatus WriteFileBytes(const wchar_t* path, const std::uint8_t* bytes, std::size_t size)
{
    return WriteFileBytesToDisk(path, bytes, size) ? ProjectIoStatus::Ok : ProjectIoStatus::Encode;
}

// Flushes an already-closed file to disk (libzip closes its own handle internally, so this reopens the path
// briefly) before the caller renames or replaces another file with it.
void FlushFileToDisk(const wchar_t* path) noexcept
{
    wil::unique_hfile file{CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                                       FILE_ATTRIBUTE_NORMAL, nullptr)};
    if (file)
    {
        FlushFileBuffers(file.get());
    }
}

[[nodiscard]] bool GetNumberValue(yyjson_val* field, double& value)
{
    if (field == nullptr)
    {
        return false;
    }
    if (yyjson_is_real(field))
    {
        value = yyjson_get_real(field);
        return std::isfinite(value);
    }
    if (yyjson_is_int(field))
    {
        value = static_cast<double>(yyjson_get_sint(field));
        return true;
    }
    return false;
}

[[nodiscard]] bool GetNumber(yyjson_val* object, const char* key, double& value, bool required)
{
    yyjson_val* field = yyjson_obj_get(object, key);
    if (field == nullptr)
    {
        return !required;
    }
    return GetNumberValue(field, value);
}

[[nodiscard]] bool GetInt(yyjson_val* object, const char* key, int& value, bool required)
{
    double number = 0;
    if (!GetNumber(object, key, number, required))
    {
        return false;
    }
    if (yyjson_obj_get(object, key) == nullptr)
    {
        return !required;
    }
    if (number < INT_MIN || number > INT_MAX || number != std::floor(number))
    {
        return false;
    }
    value = static_cast<int>(number);
    return true;
}

[[nodiscard]] bool GetBool(yyjson_val* object, const char* key, bool& value, bool required)
{
    yyjson_val* field = yyjson_obj_get(object, key);
    if (field == nullptr)
    {
        return !required;
    }
    if (!yyjson_is_bool(field))
    {
        return false;
    }
    value = yyjson_get_bool(field);
    return true;
}

[[nodiscard]] bool GetString(yyjson_val* object, const char* key, std::string& value, bool required)
{
    yyjson_val* field = yyjson_obj_get(object, key);
    if (field == nullptr)
    {
        value.clear();
        return !required;
    }
    if (!yyjson_is_str(field))
    {
        return false;
    }
    value = yyjson_get_str(field);
    return true;
}

[[nodiscard]] bool ParseGuidField(yyjson_val* object, const char* key, Guid& id, bool required)
{
    std::string text;
    if (!GetString(object, key, text, required))
    {
        return false;
    }
    if (text.empty())
    {
        return !required;
    }
    const std::optional<Guid> parsed = Guid::Parse(text);
    if (!parsed.has_value())
    {
        return false;
    }
    id = parsed.value();
    return true;
}

// A Swift CGPoint/CGSize (legacy `LayerTransform.origin`/`.size`): Apple's Codable overlay encodes these as a
// 2-element unkeyed array, `[x, y]` / `[width, height]`, not a keyed object. RedPrism's writer now matches that
// shape; the reader also accepts the `{x,y}` / `{width,height}` object shape RedPrism itself used to write, so
// older saved projects keep loading.
[[nodiscard]] bool ParseCoordinatePair(yyjson_val* value, const char* firstKey, const char* secondKey, double& first,
                                       double& second)
{
    if (value == nullptr)
    {
        return false;
    }
    if (yyjson_is_arr(value))
    {
        return yyjson_arr_size(value) == 2 && GetNumberValue(yyjson_arr_get(value, 0), first) &&
               GetNumberValue(yyjson_arr_get(value, 1), second);
    }
    if (yyjson_is_obj(value))
    {
        return GetNumber(value, firstKey, first, true) && GetNumber(value, secondKey, second, true);
    }
    return false;
}

[[nodiscard]] bool ParseTransform(yyjson_val* object, LayerTransform& transform)
{
    if (object == nullptr || !yyjson_is_obj(object))
    {
        return false;
    }
    if (!ParseCoordinatePair(yyjson_obj_get(object, "origin"), "x", "y", transform.origin.x, transform.origin.y) ||
        !ParseCoordinatePair(yyjson_obj_get(object, "size"), "width", "height", transform.size.x, transform.size.y))
    {
        return false;
    }
    (void)GetNumber(object, "rotation", transform.rotation, false);
    (void)GetBool(object, "flipX", transform.flipX, false);
    (void)GetBool(object, "flipY", transform.flipY, false);
    std::string sampling;
    if (GetString(object, "sampling", sampling, false) && !sampling.empty())
    {
        const std::optional<LayerSampling> parsed = LayerSamplingFromName(sampling);
        if (!parsed.has_value())
        {
            return false;
        }
        transform.sampling = parsed.value();
    }
    return transform.IsValid();
}

// The adjustment payload mirrors the Swift Codable shapes exactly (LayerAdjustment.swift, Levels.swift, Curves.swift,
// HueSaturation.swift, ImageAdjustments.swift). `levels` and `curves` are always written; `hsvSettings`,
// `exposureSettings`, `gradientMapSettings` and `grainSettings` only when the record carries them, so older readers
// and older files stay byte-compatible. Swift encodes a dictionary keyed by a String-backed enum that is not
// CodingKeyRepresentable as an unkeyed array of alternating keys and values; `adjustments` and `bands` use that
// form on disk. Reading also accepts a plain object keyed by range name.

[[nodiscard]] bool ParseLevelRange(yyjson_val* object, LevelRange& range)
{
    if (object == nullptr || !yyjson_is_obj(object))
    {
        return false;
    }
    return GetNumber(object, "black", range.black, false) && GetNumber(object, "gamma", range.gamma, false) &&
           GetNumber(object, "white", range.white, false) &&
           GetNumber(object, "outputBlack", range.outputBlack, false) &&
           GetNumber(object, "outputWhite", range.outputWhite, false);
}

[[nodiscard]] bool ParseLevelsChannel(yyjson_val* object, LevelsChannel& channel)
{
    std::string name;
    if (!GetString(object, "channel", name, false))
    {
        return false;
    }
    if (name.empty())
    {
        return true;
    }
    const std::optional<LevelsChannel> parsed = LevelsChannelFromName(name);
    if (!parsed.has_value())
    {
        return false;
    }
    channel = parsed.value();
    return true;
}

[[nodiscard]] bool ParseLevels(yyjson_val* object, LevelsSettings& levels)
{
    if (object == nullptr || !yyjson_is_obj(object) || !ParseLevelsChannel(object, levels.channel))
    {
        return false;
    }
    yyjson_val* ranges = yyjson_obj_get(object, "ranges");
    if (ranges == nullptr)
    {
        return true;
    }
    if (!yyjson_is_arr(ranges) || yyjson_arr_size(ranges) != levels.ranges.size())
    {
        return false;
    }
    std::size_t index = 0;
    std::size_t count = 0;
    yyjson_val* item = nullptr;
    yyjson_arr_foreach(ranges, index, count, item)
    {
        LevelRange range{};
        if (!ParseLevelRange(item, range))
        {
            return false;
        }
        levels.ranges[index] = range;
    }
    return true;
}

[[nodiscard]] bool ParseCurves(yyjson_val* object, CurvesSettings& curves)
{
    if (object == nullptr || !yyjson_is_obj(object) || !ParseLevelsChannel(object, curves.channel))
    {
        return false;
    }
    yyjson_val* channels = yyjson_obj_get(object, "channels");
    if (channels == nullptr)
    {
        return true;
    }
    if (!yyjson_is_arr(channels) || yyjson_arr_size(channels) != curves.channels.size())
    {
        return false;
    }
    std::size_t channelIndex = 0;
    std::size_t channelCount = 0;
    yyjson_val* channel = nullptr;
    yyjson_arr_foreach(channels, channelIndex, channelCount, channel)
    {
        if (!yyjson_is_arr(channel) || yyjson_arr_size(channel) > 32)
        {
            return false;
        }
        std::vector<CurvePoint> points;
        points.reserve(yyjson_arr_size(channel));
        std::size_t pointIndex = 0;
        std::size_t pointCount = 0;
        yyjson_val* point = nullptr;
        yyjson_arr_foreach(channel, pointIndex, pointCount, point)
        {
            CurvePoint value{};
            if (!yyjson_is_obj(point) || !GetNumber(point, "x", value.x, true) || !GetNumber(point, "y", value.y, true))
            {
                return false;
            }
            points.push_back(value);
        }
        curves.channels[channelIndex] = std::move(points);
    }
    return true;
}

[[nodiscard]] bool ParseRangeAdjustment(yyjson_val* object, RangeAdjustment& value)
{
    return object != nullptr && yyjson_is_obj(object) && GetNumber(object, "hue", value.hue, false) &&
           GetNumber(object, "saturation", value.saturation, false) &&
           GetNumber(object, "lightness", value.lightness, false);
}

[[nodiscard]] bool ParseHueBand(yyjson_val* object, HueBand& band)
{
    return object != nullptr && yyjson_is_obj(object) && GetNumber(object, "falloffStart", band.falloffStart, true) &&
           GetNumber(object, "rangeStart", band.rangeStart, true) &&
           GetNumber(object, "rangeEnd", band.rangeEnd, true) && GetNumber(object, "falloffEnd", band.falloffEnd, true);
}

// A Swift `[ColorRange: T]`: an alternating [key, value, key, value] array, or an object keyed by range name.
template <typename T, typename Parser>
[[nodiscard]] bool ParseRangeDictionary(yyjson_val* container,
                                        std::array<T, static_cast<std::size_t>(ColorRange::Count)>& out,
                                        Parser&& parser)
{
    if (container == nullptr)
    {
        return true;
    }
    if (yyjson_is_obj(container))
    {
        std::size_t index = 0;
        std::size_t count = 0;
        yyjson_val* key = nullptr;
        yyjson_val* value = nullptr;
        yyjson_obj_foreach(container, index, count, key, value)
        {
            const std::optional<ColorRange> range = ColorRangeFromName(yyjson_get_str(key));
            if (!range.has_value())
            {
                return false;
            }
            T parsed{};
            if (!parser(value, parsed))
            {
                return false;
            }
            out[static_cast<std::size_t>(range.value())] = parsed;
        }
        return true;
    }
    if (!yyjson_is_arr(container))
    {
        return false;
    }
    const std::size_t size = yyjson_arr_size(container);
    if (size % 2 != 0)
    {
        return false;
    }
    for (std::size_t index = 0; index < size; index += 2)
    {
        yyjson_val* key = yyjson_arr_get(container, index);
        yyjson_val* value = yyjson_arr_get(container, index + 1);
        if (!yyjson_is_str(key))
        {
            return false;
        }
        const std::optional<ColorRange> range = ColorRangeFromName(yyjson_get_str(key));
        if (!range.has_value())
        {
            return false;
        }
        T parsed{};
        if (!parser(value, parsed))
        {
            return false;
        }
        out[static_cast<std::size_t>(range.value())] = parsed;
    }
    return true;
}

[[nodiscard]] bool ParseHsvSettings(yyjson_val* object, HueSaturationSettings& settings)
{
    if (object == nullptr || !yyjson_is_obj(object))
    {
        return false;
    }
    std::string range;
    if (!GetString(object, "range", range, false))
    {
        return false;
    }
    if (!range.empty())
    {
        const std::optional<ColorRange> parsed = ColorRangeFromName(range);
        if (!parsed.has_value())
        {
            return false;
        }
        settings.range = parsed.value();
    }
    if (!GetBool(object, "colorize", settings.colorize, false) ||
        !GetBool(object, "invertRange", settings.invertRange, false))
    {
        return false;
    }
    settings.adjustments = {};
    return ParseRangeDictionary(yyjson_obj_get(object, "adjustments"), settings.adjustments, ParseRangeAdjustment) &&
           ParseRangeDictionary(yyjson_obj_get(object, "bands"), settings.bands, ParseHueBand);
}

[[nodiscard]] bool ParseExposure(yyjson_val* object, ExposureSettings& exposure)
{
    return object != nullptr && yyjson_is_obj(object) && GetNumber(object, "exposure", exposure.exposure, false) &&
           GetNumber(object, "offset", exposure.offset, false) && GetNumber(object, "gamma", exposure.gamma, false);
}

[[nodiscard]] bool ParseAdjustmentColor(yyjson_val* object, AdjustmentColor& color)
{
    return object != nullptr && yyjson_is_obj(object) && GetNumber(object, "red", color.red, true) &&
           GetNumber(object, "green", color.green, true) && GetNumber(object, "blue", color.blue, true);
}

[[nodiscard]] bool ParseGradientMap(yyjson_val* object, GradientMapSettings& gradient)
{
    if (object == nullptr || !yyjson_is_obj(object) || !GetBool(object, "reversed", gradient.reversed, false))
    {
        return false;
    }
    if (yyjson_val* shadows = yyjson_obj_get(object, "shadows");
        shadows != nullptr && !ParseAdjustmentColor(shadows, gradient.shadows))
    {
        return false;
    }
    if (yyjson_val* highlights = yyjson_obj_get(object, "highlights");
        highlights != nullptr && !ParseAdjustmentColor(highlights, gradient.highlights))
    {
        return false;
    }
    return true;
}

[[nodiscard]] bool ParseGrain(yyjson_val* object, GrainSettings& grain)
{
    if (object == nullptr || !yyjson_is_obj(object) || !GetNumber(object, "amount", grain.amount, false) ||
        !GetNumber(object, "size", grain.size, false) || !GetNumber(object, "roughness", grain.roughness, false))
    {
        return false;
    }
    double seed = static_cast<double>(grain.seed);
    if (!GetNumber(object, "seed", seed, false) || seed < 0 || seed > 4294967295.0 || seed != std::floor(seed))
    {
        return false;
    }
    grain.seed = static_cast<std::uint32_t>(seed);
    return true;
}

[[nodiscard]] bool ParseAdjustment(yyjson_val* object, LayerAdjustment& adjustment)
{
    if (object == nullptr || !yyjson_is_obj(object))
    {
        return false;
    }
    std::string kind;
    if (!GetString(object, "kind", kind, true))
    {
        return false;
    }
    const std::optional<AdjustmentKind> parsedKind = AdjustmentKindFromName(kind);
    if (!parsedKind.has_value())
    {
        return false;
    }
    adjustment = {};
    adjustment.kind = parsedKind.value();
    if (!GetNumber(object, "hue", adjustment.hue, false) ||
        !GetNumber(object, "saturation", adjustment.saturation, false) ||
        !GetNumber(object, "lightness", adjustment.lightness, false) ||
        !GetBool(object, "colorize", adjustment.colorize, false))
    {
        return false;
    }
    if (yyjson_val* hsv = yyjson_obj_get(object, "hsvSettings"); hsv != nullptr && !yyjson_is_null(hsv))
    {
        HueSaturationSettings settings{};
        if (!ParseHsvSettings(hsv, settings))
        {
            return false;
        }
        adjustment.hsvSettings = settings;
    }
    if (yyjson_val* levels = yyjson_obj_get(object, "levels");
        levels != nullptr && !ParseLevels(levels, adjustment.levels))
    {
        return false;
    }
    if (yyjson_val* curves = yyjson_obj_get(object, "curves");
        curves != nullptr && !ParseCurves(curves, adjustment.curves))
    {
        return false;
    }
    if (yyjson_val* exposure = yyjson_obj_get(object, "exposureSettings");
        exposure != nullptr && !yyjson_is_null(exposure))
    {
        ExposureSettings settings{};
        if (!ParseExposure(exposure, settings))
        {
            return false;
        }
        adjustment.exposureSettings = settings;
    }
    if (yyjson_val* gradient = yyjson_obj_get(object, "gradientMapSettings");
        gradient != nullptr && !yyjson_is_null(gradient))
    {
        GradientMapSettings settings{};
        if (!ParseGradientMap(gradient, settings))
        {
            return false;
        }
        adjustment.gradientMapSettings = settings;
    }
    if (yyjson_val* grain = yyjson_obj_get(object, "grainSettings"); grain != nullptr && !yyjson_is_null(grain))
    {
        GrainSettings settings{};
        if (!ParseGrain(grain, settings))
        {
            return false;
        }
        adjustment.grainSettings = settings;
    }
    return adjustment.IsValid();
}

[[nodiscard]] bool ParseShape(yyjson_val* object, LayerShapeStyle& shape)
{
    if (object == nullptr || !yyjson_is_obj(object))
    {
        return false;
    }
    std::string kind;
    if (!GetString(object, "kind", kind, true))
    {
        return false;
    }
    const std::optional<ShapeKind> parsed = ShapeKindFromName(kind);
    if (!parsed.has_value())
    {
        return false;
    }
    shape.kind = parsed.value();
    return GetNumber(object, "red", shape.red, true) && GetNumber(object, "green", shape.green, true) &&
           GetNumber(object, "blue", shape.blue, true) && GetNumber(object, "cornerRadius", shape.cornerRadius, false);
}

[[nodiscard]] bool ParseLayer(yyjson_val* object, LayerRecord& record)
{
    record = {};
    if (object == nullptr || !yyjson_is_obj(object))
    {
        return false;
    }
    if (!ParseGuidField(object, "id", record.layer.id, true))
    {
        return false;
    }
    std::string name;
    if (!GetString(object, "name", name, true))
    {
        return false;
    }
    record.layer.name = Utf8ToWide(name);
    if (!GetBool(object, "isVisible", record.layer.visible, true) ||
        !ParseTransform(yyjson_obj_get(object, "transform"), record.layer.transform))
    {
        return false;
    }
    (void)GetString(object, "imageFile", record.imageFile, false);
    Guid parent{};
    if (yyjson_obj_get(object, "parentID") != nullptr)
    {
        if (!ParseGuidField(object, "parentID", parent, true))
        {
            return false;
        }
        record.layer.parentID = parent;
    }
    (void)GetBool(object, "isGroup", record.layer.isGroup, false);
    (void)GetNumber(object, "opacity", record.layer.opacity, false);
    std::string blend;
    if (GetString(object, "blendMode", blend, false) && !blend.empty())
    {
        const std::optional<LayerBlendMode> parsed = LayerBlendModeFromName(blend);
        if (!parsed.has_value())
        {
            return false;
        }
        record.layer.blendMode = parsed.value();
    }
    (void)GetString(object, "maskFile", record.maskFile, false);
    bool maskEnabled = true;
    const bool hasMaskEnabled = yyjson_obj_get(object, "maskEnabled") != nullptr;
    if (hasMaskEnabled && !GetBool(object, "maskEnabled", maskEnabled, true))
    {
        return false;
    }
    Guid maskSource{};
    if (yyjson_obj_get(object, "maskSourceID") != nullptr)
    {
        if (!ParseGuidField(object, "maskSourceID", maskSource, true))
        {
            return false;
        }
        record.layer.maskSourceID = maskSource;
    }
    if (yyjson_obj_get(object, "adjustment") != nullptr)
    {
        LayerAdjustment adjustment{};
        if (!ParseAdjustment(yyjson_obj_get(object, "adjustment"), adjustment))
        {
            return false;
        }
        record.layer.adjustment = adjustment;
    }
    LayerMask mask{};
    mask.enabled = maskEnabled;
    const bool hasMaskPlacement = yyjson_obj_get(object, "maskPlacement") != nullptr;
    if (hasMaskPlacement)
    {
        LayerTransform placement{};
        if (!ParseTransform(yyjson_obj_get(object, "maskPlacement"), placement))
        {
            return false;
        }
        mask.placement = placement;
    }
    bool linked = true;
    const bool hasMaskLinked = yyjson_obj_get(object, "maskLinked") != nullptr;
    if (hasMaskLinked && !GetBool(object, "maskLinked", linked, true))
    {
        return false;
    }
    mask.linked = linked;
    if (!record.maskFile.empty())
    {
        record.layer.mask = mask;
    }
    else if (hasMaskEnabled || hasMaskPlacement || hasMaskLinked)
    {
        // Mask metadata without a mask file is damaged (ProjectStore.validate).
        return false;
    }
    if (yyjson_obj_get(object, "shape") != nullptr)
    {
        LayerShapeStyle shape{};
        if (!ParseShape(yyjson_obj_get(object, "shape"), shape))
        {
            return false;
        }
        record.layer.shape = shape;
    }
    return true;
}

// `fileName` is `<id>suffix` where `<id>` names `id` (IO_ProjectFormat.md: "UUIDs are written uppercase and read
// in either case") and `suffix` is the literal, case-sensitive ".png" / ".mask.png". Comparing by parsed value
// instead of by text makes the case-insensitivity exact instead of an ad hoc re-casing of one side.
[[nodiscard]] bool AssetNameMatches(const std::string& fileName, const Guid& id, const char* suffix)
{
    const std::size_t suffixLength = std::strlen(suffix);
    if (fileName.size() <= suffixLength || fileName.compare(fileName.size() - suffixLength, suffixLength, suffix) != 0)
    {
        return false;
    }
    const std::optional<Guid> parsed = Guid::Parse(fileName.substr(0, fileName.size() - suffixLength));
    return parsed.has_value() && parsed.value() == id;
}

[[nodiscard]] ProjectIoStatus ParseManifest(const std::vector<std::uint8_t>& bytes, Manifest& manifest)
{
    manifest = {};
    if (bytes.size() > kMaxManifestBytes)
    {
        return ProjectIoStatus::TooLarge;
    }
    yyjson_read_err error{};
    unique_yyjson_doc doc{yyjson_read_opts(reinterpret_cast<char*>(const_cast<std::uint8_t*>(bytes.data())),
                                           bytes.size(), 0, nullptr, &error)};
    if (!doc || error.code != YYJSON_READ_SUCCESS)
    {
        return ProjectIoStatus::Invalid;
    }
    yyjson_val* root = yyjson_doc_get_root(doc.get());
    if (!yyjson_is_obj(root))
    {
        return ProjectIoStatus::Invalid;
    }
    std::string format;
    if (!GetString(root, "format", format, true) || format != kFormatId)
    {
        return ProjectIoStatus::Invalid;
    }
    if (!GetInt(root, "version", manifest.version, true))
    {
        return ProjectIoStatus::Invalid;
    }
    if (manifest.version < kMinFormatVersion || manifest.version > kMaxFormatVersion)
    {
        return ProjectIoStatus::UnsupportedVersion;
    }
    std::string colorSpace;
    if (!GetString(root, "colorSpace", colorSpace, true) || colorSpace != "sRGB")
    {
        return ProjectIoStatus::Invalid;
    }
    if (!ParseGuidField(root, "documentID", manifest.document.id, true) ||
        !GetInt(root, "width", manifest.document.width, true) ||
        !GetInt(root, "height", manifest.document.height, true))
    {
        return ProjectIoStatus::Invalid;
    }
    double resolution = 72;
    if (!GetNumber(root, "resolution", resolution, false))
    {
        return ProjectIoStatus::Invalid;
    }
    manifest.document.resolution = resolution;
    Guid active{};
    if (yyjson_obj_get(root, "activeLayerID") != nullptr && !yyjson_is_null(yyjson_obj_get(root, "activeLayerID")))
    {
        if (!ParseGuidField(root, "activeLayerID", active, true))
        {
            return ProjectIoStatus::Invalid;
        }
        manifest.activeLayerID = active;
    }
    yyjson_val* layers = yyjson_obj_get(root, "layers");
    if (!yyjson_is_arr(layers))
    {
        return ProjectIoStatus::Invalid;
    }
    const std::size_t count = yyjson_arr_size(layers);
    if (count > static_cast<std::size_t>(kMaxLayerCount))
    {
        return ProjectIoStatus::TooLarge;
    }
    manifest.records.reserve(count);
    yyjson_val* item = nullptr;
    std::size_t index = 0;
    std::size_t layerCount = 0;
    yyjson_arr_foreach(layers, index, layerCount, item)
    {
        LayerRecord record{};
        if (!ParseLayer(item, record))
        {
            return ProjectIoStatus::Invalid;
        }
        if (!record.imageFile.empty() && !AssetNameMatches(record.imageFile, record.layer.id, ".png"))
        {
            return ProjectIoStatus::Invalid;
        }
        if (!record.maskFile.empty() && !AssetNameMatches(record.maskFile, record.layer.id, ".mask.png"))
        {
            return ProjectIoStatus::Invalid;
        }
        if (record.layer.adjustment.has_value() &&
            (manifest.version < 7 || record.layer.isGroup || !record.imageFile.empty()))
        {
            return ProjectIoStatus::Invalid;
        }
        manifest.records.push_back(std::move(record));
    }
    for (LayerRecord& record : manifest.records)
    {
        manifest.document.layers.push_back(record.layer);
    }
    const DocumentStatus graph = ValidateDocument(manifest.document, manifest.version);
    if (graph == DocumentStatus::UnsupportedVersion)
    {
        return ProjectIoStatus::UnsupportedVersion;
    }
    if (graph == DocumentStatus::TooLarge)
    {
        return ProjectIoStatus::TooLarge;
    }
    if (graph != DocumentStatus::Ok)
    {
        return ProjectIoStatus::Invalid;
    }
    if (manifest.activeLayerID.has_value())
    {
        bool found = false;
        for (const ImageLayer& layer : manifest.document.layers)
        {
            if (layer.id == manifest.activeLayerID.value())
            {
                found = true;
                break;
            }
        }
        if (!found)
        {
            return ProjectIoStatus::Invalid;
        }
    }
    return ProjectIoStatus::Ok;
}

void AddStr(yyjson_mut_doc* doc, yyjson_mut_val* object, const char* key, const char* value)
{
    yyjson_mut_obj_add_strcpy(doc, object, key, value);
}

[[nodiscard]] yyjson_mut_val* WriteTransform(yyjson_mut_doc* doc, const LayerTransform& transform)
{
    // origin/size are Swift CGPoint/CGSize on the wire: a 2-element array, [x, y] / [width, height], matching
    // Apple's Codable overlay for those types (legacy `LayerTransform`), not a keyed {x,y}/{width,height} object.
    yyjson_mut_val* object = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_bool(doc, object, "flipX", transform.flipX);
    yyjson_mut_obj_add_bool(doc, object, "flipY", transform.flipY);
    yyjson_mut_val* origin = yyjson_mut_arr(doc);
    yyjson_mut_arr_add_real(doc, origin, transform.origin.x);
    yyjson_mut_arr_add_real(doc, origin, transform.origin.y);
    yyjson_mut_obj_add_val(doc, object, "origin", origin);
    yyjson_mut_obj_add_real(doc, object, "rotation", transform.rotation);
    AddStr(doc, object, "sampling", LayerSamplingName(transform.sampling));
    yyjson_mut_val* size = yyjson_mut_arr(doc);
    yyjson_mut_arr_add_real(doc, size, transform.size.x);
    yyjson_mut_arr_add_real(doc, size, transform.size.y);
    yyjson_mut_obj_add_val(doc, object, "size", size);
    return object;
}

[[nodiscard]] yyjson_mut_val* WriteLevelRange(yyjson_mut_doc* doc, const LevelRange& range)
{
    yyjson_mut_val* object = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_real(doc, object, "black", range.black);
    yyjson_mut_obj_add_real(doc, object, "gamma", range.gamma);
    yyjson_mut_obj_add_real(doc, object, "outputBlack", range.outputBlack);
    yyjson_mut_obj_add_real(doc, object, "outputWhite", range.outputWhite);
    yyjson_mut_obj_add_real(doc, object, "white", range.white);
    return object;
}

[[nodiscard]] yyjson_mut_val* WriteLevels(yyjson_mut_doc* doc, const LevelsSettings& levels)
{
    yyjson_mut_val* object = yyjson_mut_obj(doc);
    AddStr(doc, object, "channel", LevelsChannelName(levels.channel));
    yyjson_mut_val* ranges = yyjson_mut_arr(doc);
    for (const LevelRange& range : levels.ranges)
    {
        yyjson_mut_arr_append(ranges, WriteLevelRange(doc, range));
    }
    yyjson_mut_obj_add_val(doc, object, "ranges", ranges);
    return object;
}

[[nodiscard]] yyjson_mut_val* WriteCurves(yyjson_mut_doc* doc, const CurvesSettings& curves)
{
    yyjson_mut_val* object = yyjson_mut_obj(doc);
    AddStr(doc, object, "channel", LevelsChannelName(curves.channel));
    yyjson_mut_val* channels = yyjson_mut_arr(doc);
    for (const std::vector<CurvePoint>& points : curves.channels)
    {
        yyjson_mut_val* channel = yyjson_mut_arr(doc);
        for (const CurvePoint& point : points)
        {
            yyjson_mut_val* value = yyjson_mut_obj(doc);
            yyjson_mut_obj_add_real(doc, value, "x", point.x);
            yyjson_mut_obj_add_real(doc, value, "y", point.y);
            yyjson_mut_arr_append(channel, value);
        }
        yyjson_mut_arr_append(channels, channel);
    }
    yyjson_mut_obj_add_val(doc, object, "channels", channels);
    return object;
}

[[nodiscard]] yyjson_mut_val* WriteHsvSettings(yyjson_mut_doc* doc, const HueSaturationSettings& settings)
{
    yyjson_mut_val* object = yyjson_mut_obj(doc);
    // Swift writes `[ColorRange: T]` as an alternating key/value array; order is fixed here for stable diffs.
    yyjson_mut_val* adjustments = yyjson_mut_arr(doc);
    yyjson_mut_val* bands = yyjson_mut_arr(doc);
    for (std::size_t index = 0; index < static_cast<std::size_t>(ColorRange::Count); ++index)
    {
        const char* name = ColorRangeName(static_cast<ColorRange>(index));
        const RangeAdjustment& adjustment = settings.adjustments[index];
        yyjson_mut_arr_add_strcpy(doc, adjustments, name);
        yyjson_mut_val* value = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_real(doc, value, "hue", adjustment.hue);
        yyjson_mut_obj_add_real(doc, value, "lightness", adjustment.lightness);
        yyjson_mut_obj_add_real(doc, value, "saturation", adjustment.saturation);
        yyjson_mut_arr_append(adjustments, value);
        const HueBand& band = settings.bands[index];
        yyjson_mut_arr_add_strcpy(doc, bands, name);
        yyjson_mut_val* bandValue = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_real(doc, bandValue, "falloffEnd", band.falloffEnd);
        yyjson_mut_obj_add_real(doc, bandValue, "falloffStart", band.falloffStart);
        yyjson_mut_obj_add_real(doc, bandValue, "rangeEnd", band.rangeEnd);
        yyjson_mut_obj_add_real(doc, bandValue, "rangeStart", band.rangeStart);
        yyjson_mut_arr_append(bands, bandValue);
    }
    yyjson_mut_obj_add_val(doc, object, "adjustments", adjustments);
    yyjson_mut_obj_add_val(doc, object, "bands", bands);
    yyjson_mut_obj_add_bool(doc, object, "colorize", settings.colorize);
    yyjson_mut_obj_add_bool(doc, object, "invertRange", settings.invertRange);
    AddStr(doc, object, "range", ColorRangeName(settings.range));
    return object;
}

[[nodiscard]] yyjson_mut_val* WriteAdjustmentColor(yyjson_mut_doc* doc, const AdjustmentColor& color)
{
    yyjson_mut_val* object = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_real(doc, object, "blue", color.blue);
    yyjson_mut_obj_add_real(doc, object, "green", color.green);
    yyjson_mut_obj_add_real(doc, object, "red", color.red);
    return object;
}

[[nodiscard]] yyjson_mut_val* WriteAdjustment(yyjson_mut_doc* doc, const LayerAdjustment& adjustment)
{
    yyjson_mut_val* object = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_bool(doc, object, "colorize", adjustment.colorize);
    yyjson_mut_obj_add_val(doc, object, "curves", WriteCurves(doc, adjustment.curves));
    if (adjustment.exposureSettings.has_value())
    {
        const ExposureSettings& exposure = adjustment.exposureSettings.value();
        yyjson_mut_val* value = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_real(doc, value, "exposure", exposure.exposure);
        yyjson_mut_obj_add_real(doc, value, "gamma", exposure.gamma);
        yyjson_mut_obj_add_real(doc, value, "offset", exposure.offset);
        yyjson_mut_obj_add_val(doc, object, "exposureSettings", value);
    }
    if (adjustment.gradientMapSettings.has_value())
    {
        const GradientMapSettings& gradient = adjustment.gradientMapSettings.value();
        yyjson_mut_val* value = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_val(doc, value, "highlights", WriteAdjustmentColor(doc, gradient.highlights));
        yyjson_mut_obj_add_bool(doc, value, "reversed", gradient.reversed);
        yyjson_mut_obj_add_val(doc, value, "shadows", WriteAdjustmentColor(doc, gradient.shadows));
        yyjson_mut_obj_add_val(doc, object, "gradientMapSettings", value);
    }
    if (adjustment.grainSettings.has_value())
    {
        const GrainSettings& grain = adjustment.grainSettings.value();
        yyjson_mut_val* value = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_real(doc, value, "amount", grain.amount);
        yyjson_mut_obj_add_real(doc, value, "roughness", grain.roughness);
        yyjson_mut_obj_add_uint(doc, value, "seed", grain.seed);
        yyjson_mut_obj_add_real(doc, value, "size", grain.size);
        yyjson_mut_obj_add_val(doc, object, "grainSettings", value);
    }
    if (adjustment.hsvSettings.has_value())
    {
        yyjson_mut_obj_add_val(doc, object, "hsvSettings", WriteHsvSettings(doc, adjustment.hsvSettings.value()));
    }
    yyjson_mut_obj_add_real(doc, object, "hue", adjustment.hue);
    AddStr(doc, object, "kind", AdjustmentKindName(adjustment.kind));
    yyjson_mut_obj_add_val(doc, object, "levels", WriteLevels(doc, adjustment.levels));
    yyjson_mut_obj_add_real(doc, object, "lightness", adjustment.lightness);
    yyjson_mut_obj_add_real(doc, object, "saturation", adjustment.saturation);
    return object;
}

[[nodiscard]] yyjson_mut_val* WriteShape(yyjson_mut_doc* doc, const LayerShapeStyle& shape)
{
    yyjson_mut_val* object = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_real(doc, object, "blue", shape.blue);
    yyjson_mut_obj_add_real(doc, object, "cornerRadius", shape.cornerRadius);
    yyjson_mut_obj_add_real(doc, object, "green", shape.green);
    AddStr(doc, object, "kind", ShapeKindName(shape.kind));
    yyjson_mut_obj_add_real(doc, object, "red", shape.red);
    return object;
}

[[nodiscard]] bool WriteManifest(const CanvasDocument& document, const std::optional<Guid>& activeLayerID,
                                 std::string& json)
{
    unique_yyjson_mut_doc doc{yyjson_mut_doc_new(nullptr)};
    if (!doc)
    {
        return false;
    }
    yyjson_mut_val* root = yyjson_mut_obj(doc.get());
    yyjson_mut_doc_set_root(doc.get(), root);
    if (activeLayerID.has_value())
    {
        AddStr(doc.get(), root, "activeLayerID", activeLayerID.value().ToUtf8().c_str());
    }
    AddStr(doc.get(), root, "colorSpace", "sRGB");
    AddStr(doc.get(), root, "documentID", document.id.ToUtf8().c_str());
    AddStr(doc.get(), root, "format", kFormatId);
    yyjson_mut_obj_add_int(doc.get(), root, "height", document.height);
    yyjson_mut_val* layers = yyjson_mut_arr(doc.get());
    for (const ImageLayer& layer : document.layers)
    {
        yyjson_mut_val* object = yyjson_mut_obj(doc.get());
        if (layer.adjustment.has_value())
        {
            yyjson_mut_obj_add_val(doc.get(), object, "adjustment",
                                   WriteAdjustment(doc.get(), layer.adjustment.value()));
        }
        if (!layer.isGroup && (layer.opacity != 1 || layer.blendMode != LayerBlendMode::Normal))
        {
            AddStr(doc.get(), object, "blendMode", LayerBlendModeName(layer.blendMode));
        }
        AddStr(doc.get(), object, "id", layer.id.ToUtf8().c_str());
        if (layer.asset.has_value())
        {
            AddStr(doc.get(), object, "imageFile", (layer.id.ToUtf8() + ".png").c_str());
        }
        if (layer.isGroup)
        {
            yyjson_mut_obj_add_bool(doc.get(), object, "isGroup", true);
        }
        yyjson_mut_obj_add_bool(doc.get(), object, "isVisible", layer.visible);
        if (layer.mask.has_value())
        {
            yyjson_mut_obj_add_bool(doc.get(), object, "maskEnabled", layer.mask.value().enabled);
            AddStr(doc.get(), object, "maskFile", (layer.id.ToUtf8() + ".mask.png").c_str());
            yyjson_mut_obj_add_bool(doc.get(), object, "maskLinked", layer.mask.value().linked);
            if (layer.mask.value().placement.has_value())
            {
                yyjson_mut_obj_add_val(doc.get(), object, "maskPlacement",
                                       WriteTransform(doc.get(), layer.mask.value().placement.value()));
            }
        }
        if (layer.maskSourceID.has_value())
        {
            AddStr(doc.get(), object, "maskSourceID", layer.maskSourceID.value().ToUtf8().c_str());
        }
        AddStr(doc.get(), object, "name", WideToUtf8(layer.name).c_str());
        if (!layer.isGroup && layer.opacity != 1)
        {
            yyjson_mut_obj_add_real(doc.get(), object, "opacity", layer.opacity);
        }
        if (layer.parentID.has_value())
        {
            AddStr(doc.get(), object, "parentID", layer.parentID.value().ToUtf8().c_str());
        }
        // Liveness is image identity (BrushStroke.cpp's IsLive check): once the layer's pixels are painted over,
        // `asset` no longer holds the raster the shape drew, and `shape` must not be written — otherwise reopening
        // the project makes it live again and the next size change redraws the shape, discarding the paint
        // (legacy EditorSession+Projects.swift writes `shape: layer.liveShape?.style`).
        if (layer.shape.has_value() && layer.asset.has_value() && layer.shapeImage.HasStorage() &&
            layer.asset.value().image == layer.shapeImage)
        {
            yyjson_mut_obj_add_val(doc.get(), object, "shape", WriteShape(doc.get(), layer.shape.value()));
        }
        yyjson_mut_obj_add_val(doc.get(), object, "transform", WriteTransform(doc.get(), layer.transform));
        yyjson_mut_arr_append(layers, object);
    }
    yyjson_mut_obj_add_val(doc.get(), root, "layers", layers);
    yyjson_mut_obj_add_real(doc.get(), root, "resolution", document.resolution);
    yyjson_mut_obj_add_int(doc.get(), root, "version", kCurrentFormatVersion);
    yyjson_mut_obj_add_int(doc.get(), root, "width", document.width);

    yyjson_write_err error{};
    char* text =
        yyjson_mut_write_opts(doc.get(), YYJSON_WRITE_PRETTY | YYJSON_WRITE_NEWLINE_AT_END, nullptr, nullptr, &error);
    if (text == nullptr)
    {
        return false;
    }
    json.assign(text);
    free(text);
    return json.size() <= kMaxManifestBytes;
}

[[nodiscard]] unique_zip OpenZipArchive(const wchar_t* path, int flags)
{
    zip_error_t error{};
    zip_error_init(&error);
    zip_source_t* source = zip_source_win32w_create(path, 0, -1, &error);
    if (source == nullptr)
    {
        zip_error_fini(&error);
        return {};
    }
    zip_t* zip = zip_open_from_source(source, flags, &error);
    if (zip == nullptr)
    {
        zip_source_free(source);
        zip_error_fini(&error);
        return {};
    }
    zip_error_fini(&error);
    return unique_zip{zip};
}

[[nodiscard]] ProjectIoStatus ReadZipEntry(zip_t* zip, const char* name, std::size_t maximum,
                                           std::vector<std::uint8_t>& bytes)
{
    bytes.clear();
    if (name == nullptr || std::strstr(name, "..") != nullptr || std::strchr(name, '\\') != nullptr)
    {
        return ProjectIoStatus::Invalid;
    }
    zip_stat_t stat{};
    zip_stat_init(&stat);
    if (zip_stat(zip, name, 0, &stat) != 0 || (stat.valid & ZIP_STAT_SIZE) == 0)
    {
        return ProjectIoStatus::MissingImage;
    }
    if (stat.size > maximum)
    {
        return ProjectIoStatus::TooLarge;
    }
    unique_zip_file file{zip_fopen(zip, name, 0)};
    if (!file)
    {
        return ProjectIoStatus::MissingImage;
    }
    bytes.resize(static_cast<std::size_t>(stat.size));
    if (!bytes.empty())
    {
        const zip_int64_t read = zip_fread(file.get(), bytes.data(), bytes.size());
        if (read < 0 || static_cast<std::size_t>(read) != bytes.size())
        {
            bytes.clear();
            return ProjectIoStatus::MissingImage;
        }
    }
    return ProjectIoStatus::Ok;
}

[[nodiscard]] ProjectIoStatus AttachAssets(Manifest& manifest, auto&& loader)
{
    int imagePixels = kMaxLayerPixels;
    int maskPixels = kMaxMaskPixels;
    for (std::size_t index = 0; index < manifest.records.size(); ++index)
    {
        LayerRecord& record = manifest.records[index];
        ImageLayer& layer = manifest.document.layers[index];
        if (!record.imageFile.empty())
        {
            DecodedImage decoded{};
            const ProjectIoStatus status = loader(record.imageFile, false, decoded);
            if (status != ProjectIoStatus::Ok)
            {
                return status;
            }
            if (decoded.image.PixelCount() > imagePixels)
            {
                return ProjectIoStatus::TooLarge;
            }
            imagePixels -= decoded.image.PixelCount();
            layer.asset = RasterAsset{decoded.image, decoded.thumbnail, layer.name};
            if (layer.shape.has_value())
            {
                // A loaded shape layer is live: it redraws at a new size until its pixels are edited.
                layer.shapeImage = decoded.image;
            }
        }
        if (!record.maskFile.empty())
        {
            DecodedImage decoded{};
            const ProjectIoStatus status = loader(record.maskFile, true, decoded);
            if (status != ProjectIoStatus::Ok)
            {
                return status;
            }
            if (decoded.image.PixelCount() > maskPixels)
            {
                return ProjectIoStatus::TooLarge;
            }
            maskPixels -= decoded.image.PixelCount();
            LayerMask mask = layer.mask.value_or(LayerMask{});
            mask.asset = RasterAsset{decoded.image, decoded.thumbnail, layer.name};
            layer.mask = mask;
        }
    }
    return ProjectIoStatus::Ok;
}

[[nodiscard]] ProjectIoStatus LoadExploded(const std::wstring& root, Manifest& manifest)
{
    const auto attributes = [](const std::wstring& path) -> std::uint32_t { return GetFileAttributesW(path.c_str()); };
    const std::wstring metadata = root + L"\\manifest.json";
    if (!ExplodedMemberPathAllowed(root, FullPath(metadata.c_str()), attributes))
    {
        return ProjectIoStatus::Invalid;
    }
    std::vector<std::uint8_t> bytes;
    ProjectIoStatus status = ReadFileBytes(metadata.c_str(), kMaxManifestBytes, bytes);
    if (status != ProjectIoStatus::Ok)
    {
        return status;
    }
    status = ParseManifest(bytes, manifest);
    if (status != ProjectIoStatus::Ok)
    {
        return status;
    }
    return AttachAssets(manifest,
                        [&](const std::string& file, bool mask, DecodedImage& decoded) -> ProjectIoStatus
                        {
                            const std::wstring path = root + L"\\images\\" + Utf8ToWide(file);
                            const std::wstring full = FullPath(path.c_str());
                            if (!ExplodedMemberPathAllowed(root, full, attributes))
                            {
                                return ProjectIoStatus::Invalid;
                            }
                            const ImageCodecStatus codec = DecodeProjectPngFile(
                                full.c_str(), mask, mask ? kMaxMaskPixels : kMaxLayerPixels, decoded);
                            if (codec == ImageCodecStatus::TooLarge)
                            {
                                return ProjectIoStatus::TooLarge;
                            }
                            if (codec != ImageCodecStatus::Ok)
                            {
                                return ProjectIoStatus::MissingImage;
                            }
                            return ProjectIoStatus::Ok;
                        });
}

[[nodiscard]] ProjectIoStatus LoadZip(const wchar_t* path, Manifest& manifest)
{
    unique_zip zip = OpenZipArchive(path, ZIP_RDONLY);
    if (!zip)
    {
        return ProjectIoStatus::Invalid;
    }
    std::vector<std::uint8_t> bytes;
    ProjectIoStatus status = ReadZipEntry(zip.get(), "manifest.json", kMaxManifestBytes, bytes);
    if (status != ProjectIoStatus::Ok)
    {
        return status == ProjectIoStatus::MissingImage ? ProjectIoStatus::Invalid : status;
    }
    status = ParseManifest(bytes, manifest);
    if (status != ProjectIoStatus::Ok)
    {
        return status;
    }
    return AttachAssets(manifest,
                        [&](const std::string& file, bool mask, DecodedImage& decoded) -> ProjectIoStatus
                        {
                            const std::string entry = std::string("images/") + file;
                            std::vector<std::uint8_t> png;
                            const ProjectIoStatus read =
                                ReadZipEntry(zip.get(), entry.c_str(), 512u * 1024u * 1024u, png);
                            if (read != ProjectIoStatus::Ok)
                            {
                                return read;
                            }
                            const ImageCodecStatus codec = DecodeProjectPng(
                                png.data(), png.size(), mask, mask ? kMaxMaskPixels : kMaxLayerPixels, decoded);
                            if (codec == ImageCodecStatus::TooLarge)
                            {
                                return ProjectIoStatus::TooLarge;
                            }
                            if (codec != ImageCodecStatus::Ok)
                            {
                                return ProjectIoStatus::MissingImage;
                            }
                            return ProjectIoStatus::Ok;
                        });
}

[[nodiscard]] ProjectIoStatus EncodeLayerAssets(const CanvasDocument& document,
                                                std::vector<std::pair<std::string, std::vector<std::uint8_t>>>& files)
{
    files.clear();
    for (const ImageLayer& layer : document.layers)
    {
        if (layer.asset.has_value())
        {
            std::vector<std::uint8_t> png;
            if (EncodePng(layer.asset.value().image, document.resolution, png) != ImageCodecStatus::Ok)
            {
                return ProjectIoStatus::Encode;
            }
            files.emplace_back(layer.id.ToUtf8() + ".png", std::move(png));
        }
        if (layer.mask.has_value())
        {
            std::vector<std::uint8_t> png;
            if (EncodePng(layer.mask.value().asset.image, document.resolution, png) != ImageCodecStatus::Ok)
            {
                return ProjectIoStatus::Encode;
            }
            files.emplace_back(layer.id.ToUtf8() + ".mask.png", std::move(png));
        }
    }
    return ProjectIoStatus::Ok;
}

void DeleteTree(const std::wstring& path)
{
    std::error_code error;
    std::filesystem::remove_all(std::filesystem::path(path), error);
}

[[nodiscard]] ProjectIoStatus SaveExploded(const wchar_t* path, const std::string& json,
                                           const std::vector<std::pair<std::string, std::vector<std::uint8_t>>>& files)
{
    const std::wstring dest = FullPath(path);
    if (dest.empty())
    {
        return ProjectIoStatus::Encode;
    }
    const std::wstring staging = dest + L".saving";
    const std::wstring backup = dest + L".old";
    DeleteTree(staging);
    DeleteTree(backup);
    if (!CreateDirectoryW(staging.c_str(), nullptr))
    {
        return ProjectIoStatus::Encode;
    }
    const std::wstring images = staging + L"\\images";
    if (!CreateDirectoryW(images.c_str(), nullptr))
    {
        DeleteTree(staging);
        return ProjectIoStatus::Encode;
    }
    if (WriteFileBytes((staging + L"\\manifest.json").c_str(), reinterpret_cast<const std::uint8_t*>(json.data()),
                       json.size()) != ProjectIoStatus::Ok)
    {
        DeleteTree(staging);
        return ProjectIoStatus::Encode;
    }
    for (const auto& file : files)
    {
        const std::wstring name = images + L"\\" + Utf8ToWide(file.first);
        if (WriteFileBytes(name.c_str(), file.second.data(), file.second.size()) != ProjectIoStatus::Ok)
        {
            DeleteTree(staging);
            return ProjectIoStatus::Encode;
        }
    }
    if (IsDirectory(dest.c_str()) && !MoveFileW(dest.c_str(), backup.c_str()))
    {
        DeleteTree(staging);
        return ProjectIoStatus::Encode;
    }
    if (!MoveFileW(staging.c_str(), dest.c_str()))
    {
        if (IsDirectory(backup.c_str()))
        {
            (void)MoveFileW(backup.c_str(), dest.c_str());
        }
        DeleteTree(staging);
        return ProjectIoStatus::Encode;
    }
    DeleteTree(backup);
    return ProjectIoStatus::Ok;
}

[[nodiscard]] ProjectIoStatus SaveZip(const wchar_t* path, const std::string& json,
                                      const std::vector<std::pair<std::string, std::vector<std::uint8_t>>>& files)
{
    const std::wstring dest = FullPath(path);
    const std::wstring temp = dest + L".tmp";
    if (dest.empty())
    {
        return ProjectIoStatus::Encode;
    }
    DeleteFileW(temp.c_str());
    unique_zip zip = OpenZipArchive(temp.c_str(), ZIP_CREATE | ZIP_TRUNCATE);
    if (!zip)
    {
        return ProjectIoStatus::Encode;
    }
    zip_source_t* manifestSource = zip_source_buffer(zip.get(), const_cast<char*>(json.data()), json.size(), 0);
    if (manifestSource == nullptr)
    {
        return ProjectIoStatus::Encode;
    }
    if (zip_file_add(zip.get(), "manifest.json", manifestSource, ZIP_FL_ENC_UTF_8 | ZIP_FL_OVERWRITE) < 0)
    {
        // zip_file_add only takes ownership of the source on success; on failure it is still ours to free
        // (OpenZipArchive frees zip_source_win32w_create's source the same way on its own failure path).
        zip_source_free(manifestSource);
        return ProjectIoStatus::Encode;
    }
    for (const auto& file : files)
    {
        const std::string entry = std::string("images/") + file.first;
        zip_source_t* source =
            zip_source_buffer(zip.get(), const_cast<std::uint8_t*>(file.second.data()), file.second.size(), 0);
        if (source == nullptr)
        {
            return ProjectIoStatus::Encode;
        }
        if (zip_file_add(zip.get(), entry.c_str(), source, ZIP_FL_ENC_UTF_8 | ZIP_FL_OVERWRITE) < 0)
        {
            zip_source_free(source);
            return ProjectIoStatus::Encode;
        }
    }
    if (!ZipCommit(zip))
    {
        DeleteFileW(temp.c_str());
        return ProjectIoStatus::Encode;
    }
    // libzip has already closed its own handle to `temp`; flush it to disk before it is renamed or replaces
    // another file, so a crash right after a save can never leave `dest` pointing at an unflushed file.
    FlushFileToDisk(temp.c_str());
    if (IsFile(dest.c_str()) &&
        !ReplaceFileW(dest.c_str(), temp.c_str(), nullptr, REPLACEFILE_IGNORE_MERGE_ERRORS, nullptr, nullptr))
    {
        if (!MoveFileExW(temp.c_str(), dest.c_str(), MOVEFILE_REPLACE_EXISTING))
        {
            DeleteFileW(temp.c_str());
            return ProjectIoStatus::Encode;
        }
        return ProjectIoStatus::Ok;
    }
    if (!IsFile(dest.c_str()) && !MoveFileW(temp.c_str(), dest.c_str()))
    {
        DeleteFileW(temp.c_str());
        return ProjectIoStatus::Encode;
    }
    return ProjectIoStatus::Ok;
}
} // namespace

bool PathLooksLikeZipProject(const wchar_t* path) noexcept
{
    if (path == nullptr)
    {
        return false;
    }
    if (IsDirectory(path))
    {
        return false;
    }
    const std::wstring wide(path);
    constexpr std::wstring_view extension = L".prism";
    if (wide.size() >= extension.size())
    {
        std::wstring ext = wide.substr(wide.size() - extension.size());
        for (wchar_t& ch : ext)
        {
            ch = static_cast<wchar_t>(towlower(ch));
        }
        if (ext == extension)
        {
            return true;
        }
    }
    // Any other file counts only with a ZIP signature (a local, empty or spanned archive): an image dropped on the
    // window is not a project.
    if (!IsFile(path))
    {
        return false;
    }
    wil::unique_hfile file{
        CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)};
    if (!file)
    {
        return false;
    }
    std::array<std::uint8_t, 4> header{};
    DWORD read = 0;
    if (!ReadFile(file.get(), header.data(), static_cast<DWORD>(header.size()), &read, nullptr) || read < header.size())
    {
        return false;
    }
    return header[0] == 'P' && header[1] == 'K' &&
           ((header[2] == 3 && header[3] == 4) || (header[2] == 5 && header[3] == 6) ||
            (header[2] == 7 && header[3] == 8));
}

ProjectLoadResult LoadProject(const wchar_t* path)
{
    ProjectLoadResult result{};
    if (path == nullptr)
    {
        result.status = ProjectIoStatus::Invalid;
        return result;
    }
    Manifest manifest{};
    const ProjectIoStatus status = IsDirectory(path) ? LoadExploded(FullPath(path), manifest) : LoadZip(path, manifest);
    result.status = status;
    if (status == ProjectIoStatus::Ok)
    {
        result.document = std::move(manifest.document);
        result.activeLayerID = manifest.activeLayerID;
        result.formatVersion = manifest.version;
    }
    return result;
}

ProjectIoStatus SaveProject(const wchar_t* path, const CanvasDocument& document,
                            const std::optional<Guid>& activeLayerID, bool zipArchive)
{
    if (path == nullptr || ValidateDocument(document) != DocumentStatus::Ok)
    {
        return ProjectIoStatus::Invalid;
    }
    std::string json;
    if (!WriteManifest(document, activeLayerID, json))
    {
        return json.size() > kMaxManifestBytes ? ProjectIoStatus::TooLarge : ProjectIoStatus::Encode;
    }
    std::vector<std::pair<std::string, std::vector<std::uint8_t>>> files;
    const ProjectIoStatus encoded = EncodeLayerAssets(document, files);
    if (encoded != ProjectIoStatus::Ok)
    {
        return encoded;
    }
    return zipArchive ? SaveZip(path, json, files) : SaveExploded(path, json, files);
}

ProjectIoStatus ImportImageOnto(CanvasDocument& document, const wchar_t* path,
                                std::optional<Guid>& activeLayerID) noexcept
{
    DecodedImage decoded{};
    const ImageCodecStatus status = ImportImageFile(path, kMaxLayerPixels, decoded);
    if (status == ImageCodecStatus::HeifUnavailable || status == ImageCodecStatus::Unsupported)
    {
        return ProjectIoStatus::Invalid;
    }
    if (status == ImageCodecStatus::TooLarge)
    {
        return ProjectIoStatus::TooLarge;
    }
    if (status != ImageCodecStatus::Ok || !decoded.image.HasStorage())
    {
        return ProjectIoStatus::MissingImage;
    }
    if (static_cast<int>(document.layers.size()) >= kMaxLayerCount)
    {
        return ProjectIoStatus::TooLarge;
    }
    ImageLayer layer = ImageLayer::Blank(
        decoded.name.empty() ? L"Layer" : decoded.name,
        Vec2{static_cast<double>(decoded.image.Width()), static_cast<double>(decoded.image.Height())});
    layer.asset = RasterAsset{decoded.image, decoded.thumbnail, layer.name};
    if (document.layers.empty())
    {
        document.width = decoded.image.Width();
        document.height = decoded.image.Height();
    }
    else
    {
        // legacy EditorSession.insert (no drop point supplied here: the host wires that separately): centered on
        // the canvas, above the active layer inside its folder (`document.layers.append` puts it topmost of
        // whichever layers share its parentID; parentID is the active layer's group, or the group it is inside).
        const ImageLayer* active = nullptr;
        if (activeLayerID.has_value())
        {
            for (const ImageLayer& candidate : document.layers)
            {
                if (candidate.id == activeLayerID.value())
                {
                    active = &candidate;
                    break;
                }
            }
        }
        const Vec2 center{static_cast<double>(document.width) / 2.0, static_cast<double>(document.height) / 2.0};
        layer.transform.origin = Vec2{std::floor(center.x - layer.transform.size.x / 2.0),
                                      std::floor(center.y - layer.transform.size.y / 2.0)};
        if (active != nullptr)
        {
            layer.parentID = active->isGroup ? std::optional<Guid>(active->id) : active->parentID;
        }
    }
    const std::optional<Guid> previousActive = activeLayerID;
    document.layers.push_back(std::move(layer));
    const Guid importedID = document.layers.back().id;
    const DocumentStatus validated = ValidateDocument(document);
    if (validated != DocumentStatus::Ok)
    {
        document.layers.pop_back();
        activeLayerID = previousActive;
        return validated == DocumentStatus::TooLarge ? ProjectIoStatus::TooLarge : ProjectIoStatus::Invalid;
    }
    activeLayerID = importedID;
    return ProjectIoStatus::Ok;
}

ProjectIoStatus ExportDocumentPng(const Raster& flattened, double resolution, const wchar_t* path) noexcept
{
    return EncodePngFile(flattened, resolution, path) == ImageCodecStatus::Ok ? ProjectIoStatus::Ok
                                                                              : ProjectIoStatus::Encode;
}

ProjectIoStatus ExportDocumentJpeg(const Raster& flattened, const JpegExportOptions& options,
                                   const wchar_t* path) noexcept
{
    return EncodeJpegFile(flattened, options, path) == ImageCodecStatus::Ok ? ProjectIoStatus::Ok
                                                                            : ProjectIoStatus::Encode;
}
