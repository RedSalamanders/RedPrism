#include "ProjectStore.h"
#include "DocumentDraft.h"

#include "JsonRead.h"
#include "Utf8.h"
#include "ZipOwner.h"

#include <algorithm>
#include <array>
#include <climits>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cwctype>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <windows.h>
#include <yyjson.h>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/resource.h>
#pragma warning(pop)

using unique_yyjson_doc = JsonRead::unique_doc;
using unique_yyjson_mut_doc = JsonRead::unique_mut_doc;
using unique_yyjson_text = wil::unique_any<char*, decltype(&free), free>;

namespace
{
constexpr char kFormatId[] = "com.redprism.project";
constexpr std::size_t kMaxManifestBytes = kMaxProjectManifestBytes;
constexpr DWORD kInvalidAttributes = INVALID_FILE_ATTRIBUTES;

void Report(ProjectIoObserver observer, ProjectIoStage stage, unsigned int value = 0) noexcept
{
    if (observer.notify)
        observer.notify(observer.context, stage, value);
}

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

[[nodiscard]] ProjectIoStatus ReadFileBytes(const wchar_t* path, std::size_t maximum, std::vector<std::uint8_t>& bytes,
                                            CancelToken cancellation)
{
    bytes.clear();
    if (cancellation.IsCancelled())
        return ProjectIoStatus::Cancelled;
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
    const std::size_t byteCount = static_cast<std::size_t>(size.QuadPart);
    bytes.reserve(byteCount);
    for (std::size_t offset = 0; offset < byteCount;)
    {
        if (cancellation.IsCancelled())
        {
            bytes.clear();
            return ProjectIoStatus::Cancelled;
        }
        const DWORD chunk = static_cast<DWORD>(std::min<std::size_t>(64 * 1024, byteCount - offset));
        bytes.resize(offset + chunk); // At most one band's zero-initialization between checkpoints.
        DWORD read = 0;
        if (!ReadFile(owned.get(), bytes.data() + offset, chunk, &read, nullptr) || read != chunk)
        {
            bytes.clear();
            return ProjectIoStatus::Invalid;
        }
        offset += read;
    }
    if (cancellation.IsCancelled())
    {
        bytes.clear();
        return ProjectIoStatus::Cancelled;
    }
    return ProjectIoStatus::Ok;
}

[[nodiscard]] std::uint32_t RequestedFailure(ProjectSaveFaultPolicy policy, ProjectSaveFaultStage stage) noexcept;

// Project assets are staged beside their destination and must be fully written, flushed, and closed before the
// directory rotation can publish them. Keep the manifest close result observable here; asset encodes use the
// codec's separate checked file-stream publication path.
[[nodiscard]] ProjectIoStatus WriteFileBytes(const wchar_t* path, const std::uint8_t* bytes, std::size_t size,
                                             CancelToken cancellation, ProjectIoObserver observer,
                                             ProjectSaveFaultPolicy faultPolicy)
{
    if (cancellation.IsCancelled())
        return ProjectIoStatus::Cancelled;
    if (path == nullptr || size > MAXDWORD)
    {
        return ProjectIoStatus::Encode;
    }
    wil::unique_hfile file{CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr)};
    if (!file)
    {
        Report(observer, ProjectIoStage::CreateFailed, GetLastError());
        return ProjectIoStatus::Encode;
    }
    for (std::size_t offset = 0; offset < size;)
    {
        if (cancellation.IsCancelled())
            return ProjectIoStatus::Cancelled;
        const DWORD chunk = static_cast<DWORD>(std::min<std::size_t>(64 * 1024, size - offset));
        DWORD written = 0;
        if (!WriteFile(file.get(), bytes + offset, chunk, &written, nullptr))
        {
            Report(observer, ProjectIoStage::WriteFailed, GetLastError());
            return ProjectIoStatus::Encode;
        }
        if (written != chunk)
        {
            Report(observer, ProjectIoStage::WriteFailed, ERROR_WRITE_FAULT);
            return ProjectIoStatus::Encode;
        }
        offset += written;
    }
    if (cancellation.IsCancelled())
        return ProjectIoStatus::Cancelled;
    const auto flushFailure = RequestedFailure(faultPolicy, ProjectSaveFaultStage::ManifestFlush);
    if (flushFailure != 0 || !FlushFileBuffers(file.get()))
    {
        const auto error = flushFailure != 0 ? flushFailure : GetLastError();
        Report(observer, ProjectIoStage::FlushFailed, error);
        return ProjectIoStatus::Encode;
    }
    const auto closeFailure = RequestedFailure(faultPolicy, ProjectSaveFaultStage::ManifestClose);
    if (closeFailure != 0)
    {
        Report(observer, ProjectIoStage::CloseFailed, closeFailure);
        return ProjectIoStatus::Encode; // Original RAII owner still closes before staging cleanup.
    }
    HANDLE raw = file.release();
    if (!CloseHandle(raw))
    {
        Report(observer, ProjectIoStage::CloseFailed, GetLastError());
        return ProjectIoStatus::Encode;
    }
    return ProjectIoStatus::Ok;
}

// libzip closes its own handle internally; reopen the completed archive and require both flush and close to succeed
// before the caller can publish it over the current destination.
[[nodiscard]] bool FlushFileToDisk(const wchar_t* path, ProjectIoObserver observer,
                                   ProjectSaveFaultPolicy faultPolicy) noexcept
{
    wil::unique_hfile file{CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                                       FILE_ATTRIBUTE_NORMAL, nullptr)};
    if (!file)
    {
        Report(observer, ProjectIoStage::CreateFailed, GetLastError());
        return false;
    }
    const auto flushFailure = RequestedFailure(faultPolicy, ProjectSaveFaultStage::ArchiveFlush);
    if (flushFailure != 0 || !FlushFileBuffers(file.get()))
    {
        const auto error = flushFailure != 0 ? flushFailure : GetLastError();
        Report(observer, ProjectIoStage::FlushFailed, error);
        return false;
    }
    const auto closeFailure = RequestedFailure(faultPolicy, ProjectSaveFaultStage::ArchiveClose);
    if (closeFailure != 0)
    {
        Report(observer, ProjectIoStage::CloseFailed, closeFailure);
        return false;
    }
    HANDLE raw = file.release();
    if (!CloseHandle(raw))
    {
        Report(observer, ProjectIoStage::CloseFailed, GetLastError());
        return false;
    }
    return true;
}

[[nodiscard]] std::wstring UniqueSiblingPath(const std::wstring& destination)
{
    return destination + L".~" + Utf8ToWide(Guid::Generate().ToUtf8());
}

// Thin names over Common/JsonRead.h kept so the call sites below (and their `required` argument, matching the
// manifest's Codable shape field by field) read the same as before the shared header existed.
[[nodiscard]] bool GetNumber(yyjson_val* object, const char* key, double& value, bool required)
{
    return JsonRead::ReadNumber(object, key, value, required);
}

[[nodiscard]] bool GetInt(yyjson_val* object, const char* key, int& value, bool required)
{
    return JsonRead::ReadInt(object, key, value, required);
}

[[nodiscard]] bool GetBool(yyjson_val* object, const char* key, bool& value, bool required)
{
    return JsonRead::ReadBool(object, key, value, required);
}

[[nodiscard]] bool GetString(yyjson_val* object, const char* key, std::string& value, bool required)
{
    return JsonRead::ReadString(object, key, value, required);
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
        return yyjson_arr_size(value) == 2 && JsonRead::NumberValue(yyjson_arr_get(value, 0), first) &&
               JsonRead::NumberValue(yyjson_arr_get(value, 1), second);
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

[[nodiscard]] ProjectIoStatus ParseManifest(const std::vector<std::uint8_t>& bytes, Manifest& manifest,
                                            CancelToken cancellation, std::size_t maximumLayers = kMaxLayerCount)
{
    manifest = {};
    if (cancellation.IsCancelled())
        return ProjectIoStatus::Cancelled;
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
    if (count > maximumLayers)
    {
        return ProjectIoStatus::TooLarge;
    }
    manifest.records.reserve(count);
    yyjson_val* item = nullptr;
    std::size_t index = 0;
    std::size_t layerCount = 0;
    yyjson_arr_foreach(layers, index, layerCount, item)
    {
        if (cancellation.IsCancelled())
            return ProjectIoStatus::Cancelled;
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
    auto draft = CanvasDocumentDraft::Rebuild(manifest.document, count, cancellation);
    if (!draft)
        return cancellation.IsCancelled() ? ProjectIoStatus::Cancelled : ProjectIoStatus::TooLarge;
    for (LayerRecord& record : manifest.records)
    {
        if (cancellation.IsCancelled())
            return ProjectIoStatus::Cancelled;
        if (!draft.value().Append(record.layer))
            return ProjectIoStatus::TooLarge;
    }
    auto parsed = draft.value().Seal(cancellation);
    if (!parsed)
        return cancellation.IsCancelled() ? ProjectIoStatus::Cancelled : ProjectIoStatus::Invalid;
    manifest.document = std::move(parsed.value());
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
                                 std::string& json, CancelToken cancellation)
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
        if (cancellation.IsCancelled())
            return false;
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
    unique_yyjson_text text{
        yyjson_mut_write_opts(doc.get(), YYJSON_WRITE_PRETTY | YYJSON_WRITE_NEWLINE_AT_END, nullptr, nullptr, &error)};
    if (!text)
    {
        return false;
    }
    json.assign(text.get());
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
                                           std::vector<std::uint8_t>& bytes, CancelToken cancellation)
{
    bytes.clear();
    if (cancellation.IsCancelled())
        return ProjectIoStatus::Cancelled;
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
    const std::size_t byteCount = static_cast<std::size_t>(stat.size);
    bytes.reserve(byteCount);
    for (std::size_t offset = 0; offset < byteCount;)
    {
        if (cancellation.IsCancelled())
        {
            bytes.clear();
            return ProjectIoStatus::Cancelled;
        }
        const std::size_t chunk = std::min<std::size_t>(64 * 1024, byteCount - offset);
        bytes.resize(offset + chunk);
        const zip_int64_t read = zip_fread(file.get(), bytes.data() + offset, chunk);
        if (read <= 0 || static_cast<std::size_t>(read) > chunk)
        {
            bytes.clear();
            return ProjectIoStatus::MissingImage;
        }
        offset += static_cast<std::size_t>(read);
    }
    return ProjectIoStatus::Ok;
}

[[nodiscard]] ProjectIoStatus AttachAssets(Manifest& manifest, CancelToken cancellation, ProjectIoObserver observer,
                                           ProjectLoadPixelLimits pixelLimits, auto&& loader)
{
    Report(observer, ProjectIoStage::ManifestReady);
    auto draft = CanvasDocumentDraft::Clone(manifest.document, manifest.document.layers.size(), cancellation);
    if (!draft)
        return cancellation.IsCancelled() ? ProjectIoStatus::Cancelled : ProjectIoStatus::TooLarge;
    int imagePixels = pixelLimits.imagePixels;
    int maskPixels = pixelLimits.maskPixels;
    for (std::size_t index = 0; index < manifest.records.size(); ++index)
    {
        if (cancellation.IsCancelled())
            return ProjectIoStatus::Cancelled;
        LayerRecord& record = manifest.records[index];
        ImageLayer& layer = draft.value().EditLayers()[index];
        if (!record.imageFile.empty())
        {
            DecodedImage decoded{};
            // Decoder enforces this remaining aggregate budget after reading dimensions but before allocating pixels.
            const ProjectIoStatus status = loader(record.imageFile, false, imagePixels, decoded);
            if (status != ProjectIoStatus::Ok)
            {
                return status;
            }
            Report(observer, ProjectIoStage::AssetDecoded, static_cast<unsigned int>(index + 1));
            if (cancellation.IsCancelled())
                return ProjectIoStatus::Cancelled;
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
            // Mask pixels have an independent aggregate budget, passed before the decoder allocates its output.
            const ProjectIoStatus status = loader(record.maskFile, true, maskPixels, decoded);
            if (status != ProjectIoStatus::Ok)
            {
                return status;
            }
            Report(observer, ProjectIoStage::AssetDecoded, static_cast<unsigned int>(index + 1));
            if (cancellation.IsCancelled())
                return ProjectIoStatus::Cancelled;
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
    auto decoded = draft.value().Seal(cancellation);
    if (!decoded)
        return cancellation.IsCancelled() ? ProjectIoStatus::Cancelled : ProjectIoStatus::Invalid;
    manifest.document = std::move(decoded.value());
    return ProjectIoStatus::Ok;
}

[[nodiscard]] ProjectIoStatus LoadExploded(const std::wstring& root, Manifest& manifest, CancelToken cancellation,
                                           ProjectIoObserver observer, ProjectLoadPixelLimits pixelLimits)
{
    const auto attributes = [](const std::wstring& path) -> std::uint32_t { return GetFileAttributesW(path.c_str()); };
    const std::wstring metadata = root + L"\\manifest.json";
    if (!ExplodedMemberPathAllowed(root, FullPath(metadata.c_str()), attributes))
    {
        return ProjectIoStatus::Invalid;
    }
    std::vector<std::uint8_t> bytes;
    ProjectIoStatus status = ReadFileBytes(metadata.c_str(), pixelLimits.manifestBytes, bytes, cancellation);
    if (status != ProjectIoStatus::Ok)
    {
        return status;
    }
    status = ParseManifest(bytes, manifest, cancellation, pixelLimits.layerCount);
    if (status != ProjectIoStatus::Ok)
    {
        return status;
    }
    return AttachAssets(
        manifest, cancellation, observer, pixelLimits,
        [&](const std::string& file, bool mask, int remainingPixels, DecodedImage& decoded) -> ProjectIoStatus
        {
            const std::wstring path = root + L"\\images\\" + Utf8ToWide(file);
            const std::wstring full = FullPath(path.c_str());
            if (!ExplodedMemberPathAllowed(root, full, attributes))
            {
                return ProjectIoStatus::Invalid;
            }
            const ImageCodecStatus codec = DecodeProjectPngFile(full.c_str(), mask, remainingPixels, decoded,
                                                                cancellation, pixelLimits.encodedAssetBytes);
            if (codec == ImageCodecStatus::Cancelled)
                return ProjectIoStatus::Cancelled;
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

[[nodiscard]] ProjectIoStatus LoadZip(const wchar_t* path, Manifest& manifest, CancelToken cancellation,
                                      ProjectIoObserver observer, ProjectLoadPixelLimits pixelLimits)
{
    unique_zip zip = OpenZipArchive(path, ZIP_RDONLY);
    if (!zip)
    {
        return ProjectIoStatus::Invalid;
    }
    std::vector<std::uint8_t> bytes;
    ProjectIoStatus status = ReadZipEntry(zip.get(), "manifest.json", pixelLimits.manifestBytes, bytes, cancellation);
    if (status != ProjectIoStatus::Ok)
    {
        return status == ProjectIoStatus::MissingImage ? ProjectIoStatus::Invalid : status;
    }
    status = ParseManifest(bytes, manifest, cancellation, pixelLimits.layerCount);
    if (status != ProjectIoStatus::Ok)
    {
        return status;
    }
    return AttachAssets(
        manifest, cancellation, observer, pixelLimits,
        [&](const std::string& file, bool mask, int remainingPixels, DecodedImage& decoded) -> ProjectIoStatus
        {
            const std::string entry = std::string("images/") + file;
            std::vector<std::uint8_t> png;
            const ProjectIoStatus read =
                ReadZipEntry(zip.get(), entry.c_str(), pixelLimits.encodedAssetBytes, png, cancellation);
            if (read != ProjectIoStatus::Ok)
            {
                return read;
            }
            const ImageCodecStatus codec =
                DecodeProjectPng(png.data(), png.size(), mask, remainingPixels, decoded, cancellation);
            if (codec == ImageCodecStatus::Cancelled)
                return ProjectIoStatus::Cancelled;
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

[[nodiscard]] ProjectIoStatus EncodeLayerAssets(const CanvasDocument& document, const std::wstring& images,
                                                std::vector<std::string>& files, CancelToken cancellation,
                                                ProjectIoObserver observer)
{
    files.clear();
    for (const ImageLayer& layer : document.layers)
    {
        if (cancellation.IsCancelled())
            return ProjectIoStatus::Cancelled;
        if (layer.asset.has_value())
        {
            const std::string name = layer.id.ToUtf8() + ".png";
            const std::wstring path = images + L"\\" + Utf8ToWide(name);
            const auto status =
                EncodePngFile(layer.asset.value().image, document.resolution, path.c_str(), cancellation);
            if (status == ImageCodecStatus::Cancelled)
                return ProjectIoStatus::Cancelled;
            if (status != ImageCodecStatus::Ok)
            {
                return ProjectIoStatus::Encode;
            }
            files.push_back(name);
            Report(observer, ProjectIoStage::AssetEncoded, static_cast<unsigned int>(files.size()));
        }
        if (layer.mask.has_value())
        {
            const std::string name = layer.id.ToUtf8() + ".mask.png";
            const std::wstring path = images + L"\\" + Utf8ToWide(name);
            const auto status =
                EncodePngFile(layer.mask.value().asset.image, document.resolution, path.c_str(), cancellation);
            if (status == ImageCodecStatus::Cancelled)
                return ProjectIoStatus::Cancelled;
            if (status != ImageCodecStatus::Ok)
            {
                return ProjectIoStatus::Encode;
            }
            files.push_back(name);
            Report(observer, ProjectIoStage::AssetEncoded, static_cast<unsigned int>(files.size()));
        }
    }
    return ProjectIoStatus::Ok;
}

[[nodiscard]] std::uint32_t RequestedFailure(ProjectSaveFaultPolicy policy, ProjectSaveFaultStage stage) noexcept
{
    return policy.failure == nullptr ? 0 : policy.failure(policy.context, stage);
}

using TreeCleanupBuffer = std::array<wchar_t, 32768>;
constexpr unsigned int kMaxCleanupDepth = 128;

// The path buffer is allocated before staging ownership begins. Cleanup also runs during worker OOM unwinding:
// no path construction, container growth or filesystem allocation is permitted here. Do not follow reparse points.
[[nodiscard]] std::uint32_t DeleteTreePath(TreeCleanupBuffer& path, std::size_t length, unsigned int depth) noexcept
{
    const DWORD attributes = GetFileAttributesW(path.data());
    if (attributes == INVALID_FILE_ATTRIBUTES)
    {
        const DWORD failure = GetLastError();
        return failure == ERROR_FILE_NOT_FOUND || failure == ERROR_PATH_NOT_FOUND ? 0 : failure;
    }
    if ((attributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
        return DeleteFileW(path.data()) ? 0 : GetLastError();
    if ((attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
        return RemoveDirectoryW(path.data()) ? 0 : GetLastError();
    if (depth >= kMaxCleanupDepth || length + 2 >= path.size())
        return ERROR_FILENAME_EXCED_RANGE;

    path[length] = L'\\';
    path[length + 1] = L'*';
    path[length + 2] = L'\0';
    WIN32_FIND_DATAW entry{};
    wil::unique_hfind search{FindFirstFileW(path.data(), &entry)};
    path[length] = L'\0';
    if (!search)
    {
        const DWORD failure = GetLastError();
        if (failure != ERROR_FILE_NOT_FOUND)
            return failure;
    }
    else
    {
        for (;;)
        {
            const std::wstring_view name{entry.cFileName};
            if (name != L"." && name != L"..")
            {
                if (length + 1 + name.size() >= path.size())
                    return ERROR_FILENAME_EXCED_RANGE;
                path[length] = L'\\';
                std::copy(name.begin(), name.end(), path.begin() + length + 1);
                const std::size_t childLength = length + 1 + name.size();
                path[childLength] = L'\0';
                const std::uint32_t failure = DeleteTreePath(path, childLength, depth + 1);
                path[length] = L'\0';
                if (failure != 0)
                    return failure;
            }
            if (!FindNextFileW(search.get(), &entry))
            {
                const DWORD failure = GetLastError();
                if (failure != ERROR_NO_MORE_FILES)
                    return failure;
                break;
            }
        }
        search.reset();
    }
    return RemoveDirectoryW(path.data()) ? 0 : GetLastError();
}

[[nodiscard]] bool DeleteTree(const std::wstring& path, ProjectSaveFaultStage faultStage, ProjectIoStage failureStage,
                              ProjectSaveFaultPolicy faultPolicy, ProjectIoObserver observer,
                              TreeCleanupBuffer& scratch) noexcept
{
    std::uint32_t failure = RequestedFailure(faultPolicy, faultStage);
    if (failure == 0)
    {
        const bool extended = path.starts_with(L"\\\\?\\");
        const bool unc = !extended && path.starts_with(L"\\\\");
        const std::wstring_view prefix = extended ? L"" : unc ? L"\\\\?\\UNC\\" : L"\\\\?\\";
        const std::size_t offset = unc ? 2 : 0;
        const std::size_t length = prefix.size() + path.size() - offset;
        if (path.empty() || length >= scratch.size())
            failure = ERROR_FILENAME_EXCED_RANGE;
        else
        {
            std::copy(prefix.begin(), prefix.end(), scratch.begin());
            std::copy(path.begin() + offset, path.end(), scratch.begin() + prefix.size());
            scratch[length] = L'\0';
            failure = DeleteTreePath(scratch, length, 0);
        }
    }
    if (failure != 0)
    {
        Report(observer, failureStage, failure);
        return false;
    }
    return true;
}

[[nodiscard]] ProjectIoStatus PublishDirectory(const std::wstring& staging, const std::wstring& dest,
                                               std::uint32_t& failure, bool rotated, CancelToken cancellation,
                                               ProjectSaveFaultPolicy faultPolicy, ProjectIoObserver observer) noexcept
{
    failure = RequestedFailure(faultPolicy, ProjectSaveFaultStage::StagingToDestinationRename);
    if (failure != 0)
        return ProjectIoStatus::Encode; // Injected operation failures remain final.
    constexpr std::array<LONGLONG, 2> retryDelays100ns{-25 * 10000ll, -100 * 10000ll};
    wil::unique_handle retryTimer;
    for (std::size_t attempt = 0;; ++attempt)
    {
        // Before rotation no external destination was changed. After rotation, a late stop must not abandon
        // the new publication or the complete recovery backup. Never replace an unexpected destination.
        if (!rotated && cancellation.IsCancelled())
            return ProjectIoStatus::Cancelled;
        if (MoveFileW(staging.c_str(), dest.c_str()))
            return ProjectIoStatus::Ok;
        failure = GetLastError(); // Capture before reporting, rollback or cleanup.
        if ((failure != ERROR_ACCESS_DENIED && failure != ERROR_SHARING_VIOLATION) ||
            attempt == retryDelays100ns.size())
            return ProjectIoStatus::Encode;
        if (!retryTimer)
        {
            if (RequestedFailure(faultPolicy, ProjectSaveFaultStage::PublicationTimerCreate) != 0)
                return ProjectIoStatus::Encode;
            retryTimer.reset(CreateWaitableTimerW(nullptr, FALSE, nullptr));
        }
        LARGE_INTEGER due{};
        due.QuadPart = retryDelays100ns[attempt];
        if (!retryTimer || RequestedFailure(faultPolicy, ProjectSaveFaultStage::PublicationTimerArm) != 0 ||
            !SetWaitableTimer(retryTimer.get(), &due, 0, nullptr, nullptr, FALSE))
            return ProjectIoStatus::Encode;
        Report(observer, ProjectIoStage::PublicationRetryScheduled, failure);
        if (!rotated && cancellation.IsCancelled())
            return ProjectIoStatus::Cancelled;
        if (RequestedFailure(faultPolicy, ProjectSaveFaultStage::PublicationTimerWait) != 0 ||
            WaitForSingleObject(retryTimer.get(), 1000) != WAIT_OBJECT_0)
            return ProjectIoStatus::Encode;
    }
}

[[nodiscard]] bool RestoreDirectory(const std::wstring& backup, const std::wstring& dest, std::uint32_t& failure,
                                    ProjectSaveFaultPolicy faultPolicy, ProjectIoObserver observer) noexcept
{
    failure = RequestedFailure(faultPolicy, ProjectSaveFaultStage::RollbackRename);
    if (failure != 0)
        return false; // An injected operation failure is final, preserving the deterministic fault seam.
    // NTFS can refuse a directory rename while a reader has any child open, including a reader reacting to the
    // first rename. Recovery alone gets two delayed attempts. No healthy-save overhead, spinning, UI wait or
    // cancellation-dependent abandonment of the old tree after rotation has started.
    constexpr std::array<LONGLONG, 2> retryDelays100ns{-25 * 10000ll, -100 * 10000ll};
    wil::unique_handle retryTimer;
    for (std::size_t attempt = 0;; ++attempt)
    {
        if (MoveFileW(backup.c_str(), dest.c_str()))
            return true;
        failure = GetLastError();
        if ((failure != ERROR_ACCESS_DENIED && failure != ERROR_SHARING_VIOLATION) ||
            attempt == retryDelays100ns.size())
            return false;
        if (!retryTimer)
        {
            if (RequestedFailure(faultPolicy, ProjectSaveFaultStage::RecoveryTimerCreate) != 0)
                return false; // Test trigger only; keep reporting the native rename error in failure.
            retryTimer.reset(CreateWaitableTimerW(nullptr, FALSE, nullptr));
        }
        LARGE_INTEGER due{};
        due.QuadPart = retryDelays100ns[attempt];
        if (!retryTimer || RequestedFailure(faultPolicy, ProjectSaveFaultStage::RecoveryTimerArm) != 0 ||
            !SetWaitableTimer(retryTimer.get(), &due, 0, nullptr, nullptr, FALSE))
            return false; // Preserve the initiating native rename error and the complete recovery backup.
        Report(observer, ProjectIoStage::RollbackRetryScheduled, failure);
        if (RequestedFailure(faultPolicy, ProjectSaveFaultStage::RecoveryTimerWait) != 0 ||
            WaitForSingleObject(retryTimer.get(), 1000) != WAIT_OBJECT_0)
            return false;
    }
}

[[nodiscard]] ProjectIoStatus RotateDestination(const std::wstring& dest, const std::wstring& backup,
                                                std::uint32_t& failure, CancelToken cancellation,
                                                ProjectSaveFaultPolicy faultPolicy, ProjectIoObserver observer) noexcept
{
    failure = RequestedFailure(faultPolicy, ProjectSaveFaultStage::DestinationToBackupRename);
    if (failure != 0)
        return ProjectIoStatus::Encode; // injected errors are final; never hide deterministic faults with retries
    constexpr std::array<LONGLONG, 2> retryDelays100ns{-25 * 10000ll, -100 * 10000ll};
    wil::unique_handle retryTimer;
    for (std::size_t attempt = 0;; ++attempt)
    {
        if (cancellation.IsCancelled())
            return ProjectIoStatus::Cancelled; // no first rename has succeeded; current destination is intact
        if (MoveFileW(dest.c_str(), backup.c_str()))
            return ProjectIoStatus::Ok;
        failure = GetLastError();
        if ((failure != ERROR_ACCESS_DENIED && failure != ERROR_SHARING_VIOLATION) ||
            attempt == retryDelays100ns.size())
            return ProjectIoStatus::Encode;
        if (!retryTimer)
        {
            if (RequestedFailure(faultPolicy, ProjectSaveFaultStage::DestinationTimerCreate) != 0)
                return ProjectIoStatus::Encode;
            retryTimer.reset(CreateWaitableTimerW(nullptr, FALSE, nullptr));
        }
        LARGE_INTEGER due{};
        due.QuadPart = retryDelays100ns[attempt];
        if (!retryTimer || RequestedFailure(faultPolicy, ProjectSaveFaultStage::DestinationTimerArm) != 0 ||
            !SetWaitableTimer(retryTimer.get(), &due, 0, nullptr, nullptr, FALSE))
            return ProjectIoStatus::Encode;
        Report(observer, ProjectIoStage::DestinationRetryScheduled, failure);
        if (cancellation.IsCancelled())
            return ProjectIoStatus::Cancelled;
        if (RequestedFailure(faultPolicy, ProjectSaveFaultStage::DestinationTimerWait) != 0 ||
            WaitForSingleObject(retryTimer.get(), 1000) != WAIT_OBJECT_0)
            return ProjectIoStatus::Encode;
    }
}

[[nodiscard]] ProjectIoStatus SaveExploded(const std::wstring& dest, const std::wstring& staging,
                                           CancelToken cancellation, ProjectIoObserver observer,
                                           ProjectSaveFaultPolicy faultPolicy, TreeCleanupBuffer& cleanupScratch)
{
    if (cancellation.IsCancelled())
        return ProjectIoStatus::Cancelled;
    const std::wstring backup = dest + L".old";
    bool rotated = false;
    Report(observer, ProjectIoStage::Staged);
    if (cancellation.IsCancelled())
        return ProjectIoStatus::Cancelled;
    // Once the first rename succeeds, finish or recover publication without interpreting a later stop
    // as a rollback. The caller receives Ok only when the new external destination was actually published.
    // Remove an older backup only while the current destination still exists. When a previous rollback failed,
    // .old can be the only surviving project; keep it until new publication or restore it on another failure.
    if (IsDirectory(dest.c_str()))
    {
        if (!DeleteTree(backup, ProjectSaveFaultStage::BackupCleanup, ProjectIoStage::BackupCleanupFailed, faultPolicy,
                        observer, cleanupScratch))
            return ProjectIoStatus::Encode;
        std::uint32_t failure = 0;
        const auto rotation = RotateDestination(dest, backup, failure, cancellation, faultPolicy, observer);
        if (rotation != ProjectIoStatus::Ok)
        {
            if (rotation != ProjectIoStatus::Cancelled)
                Report(observer, ProjectIoStage::DestinationToBackupFailed, failure);
            return rotation;
        }
        rotated = true;
    }
    std::uint32_t publishFailure = 0;
    const auto publication =
        PublishDirectory(staging, dest, publishFailure, rotated, cancellation, faultPolicy, observer);
    if (publication != ProjectIoStatus::Ok)
    {
        if (publication == ProjectIoStatus::Cancelled)
            return publication; // Only reachable before rotation; leave any earlier recovery backup intact.
        Report(observer, ProjectIoStage::StagingToDestinationFailed, publishFailure);
        if (IsDirectory(backup.c_str()))
        {
            std::uint32_t rollbackFailure = 0;
            if (!RestoreDirectory(backup, dest, rollbackFailure, faultPolicy, observer))
                Report(observer, ProjectIoStage::RollbackFailed, rollbackFailure);
        }
        return ProjectIoStatus::Encode;
    }
    // This is the irreversible publication fact. Cleanup faults may retain the old backup but cannot turn a
    // published document into a failed or cancelled save.
    Report(observer, ProjectIoStage::Published);
    (void)DeleteTree(backup, ProjectSaveFaultStage::BackupCleanup, ProjectIoStage::BackupCleanupFailed, faultPolicy,
                     observer, cleanupScratch);
    return ProjectIoStatus::Ok;
}

[[nodiscard]] ProjectIoStatus SaveZip(const wchar_t* path, const std::string& json, const std::wstring& images,
                                      const std::vector<std::string>& files, CancelToken cancellation,
                                      ProjectIoObserver observer, ProjectSaveFaultPolicy faultPolicy)
{
    if (cancellation.IsCancelled())
        return ProjectIoStatus::Cancelled;
    const std::wstring dest = FullPath(path);
    if (dest.empty())
    {
        return ProjectIoStatus::Encode;
    }
    const std::wstring temp = UniqueSiblingPath(dest);
    bool ownsTemp = false;
    auto cleanupTemp = wil::scope_exit(
        [&]() noexcept
        {
            if (ownsTemp)
                (void)DeleteFileW(temp.c_str());
        });
    // Declared before archive ownership: callbacks keep their borrowed state even on early discard.
    ProjectIoObserver progressObserver = observer;
    unique_zip zip = OpenZipArchive(temp.c_str(), ZIP_CREATE | ZIP_EXCL);
    if (!zip)
    {
        return ProjectIoStatus::Encode;
    }
    ownsTemp = true; // archive ownership is destroyed before the cleanup guard on every early return
    // libzip borrows this stack token through zip_close/discard. The callback reads only stop state, never UI.
    if (zip_register_cancel_callback_with_state(
            zip.get(), [](zip_t*, void* state) noexcept
            { return static_cast<CancelToken*>(state)->IsCancelled() ? 1 : 0; }, nullptr, &cancellation) != 0)
        return ProjectIoStatus::Encode;
    if (observer.notify && zip_register_progress_callback_with_state(
                               zip.get(), 0.05,
                               [](zip_t*, double progress, void* state) noexcept
                               {
                                   const auto value = static_cast<unsigned int>(std::clamp(progress, 0.0, 1.0) * 100.0);
                                   Report(*static_cast<ProjectIoObserver*>(state), ProjectIoStage::ArchiveWriting,
                                          value);
                               },
                               nullptr, &progressObserver) != 0)
        return ProjectIoStatus::Encode;
    zip_source_t* manifestSource = zip_source_buffer(zip.get(), const_cast<char*>(json.data()), json.size(), 0);
    if (manifestSource == nullptr)
    {
        return ProjectIoStatus::Encode;
    }
    const zip_int64_t manifestIndex =
        zip_file_add(zip.get(), "manifest.json", manifestSource, ZIP_FL_ENC_UTF_8 | ZIP_FL_OVERWRITE);
    if (manifestIndex < 0)
    {
        // zip_file_add only takes ownership of the source on success; on failure it is still ours to free
        // (OpenZipArchive frees zip_source_win32w_create's source the same way on its own failure path).
        zip_source_free(manifestSource);
        return ProjectIoStatus::Encode;
    }
    if (zip_set_file_compression(zip.get(), static_cast<zip_uint64_t>(manifestIndex), ZIP_CM_DEFLATE, 6) != 0)
    {
        return ProjectIoStatus::Encode;
    }
    for (const auto& file : files)
    {
        if (cancellation.IsCancelled())
            return ProjectIoStatus::Cancelled;
        const std::string entry = std::string("images/") + file;
        const std::wstring assetPath = images + L"\\" + Utf8ToWide(file);
        zip_source_t* source = zip_source_win32w(zip.get(), assetPath.c_str(), 0, -1);
        if (source == nullptr)
        {
            return ProjectIoStatus::Encode;
        }
        const zip_int64_t index = zip_file_add(zip.get(), entry.c_str(), source, ZIP_FL_ENC_UTF_8 | ZIP_FL_OVERWRITE);
        if (index < 0)
        {
            zip_source_free(source);
            return ProjectIoStatus::Encode;
        }
        if (zip_set_file_compression(zip.get(), static_cast<zip_uint64_t>(index), ZIP_CM_STORE, 0) != 0)
        {
            return ProjectIoStatus::Encode;
        }
    }
    const auto commitFailure = RequestedFailure(faultPolicy, ProjectSaveFaultStage::ArchiveCommit);
    if (commitFailure != 0)
    {
        Report(observer, ProjectIoStage::CloseFailed, commitFailure);
        return ProjectIoStatus::Encode; // Discard precedes exclusive temporary cleanup.
    }
    if (!ZipCommit(zip))
    {
        return cancellation.IsCancelled() ? ProjectIoStatus::Cancelled : ProjectIoStatus::Encode;
    }
    // libzip has already closed its own handle to `temp`; flush it to disk before it is renamed or replaces
    // another file. Publication is refused when that flush or its transferred handle close fails.
    if (!FlushFileToDisk(temp.c_str(), observer, faultPolicy))
    {
        return ProjectIoStatus::Encode;
    }
    Report(observer, ProjectIoStage::Staged);
    if (cancellation.IsCancelled())
        return ProjectIoStatus::Cancelled;
    // An acknowledged stop before this boundary leaves dest intact. Publication is not cancelled midway.
    const auto replaceFailure = RequestedFailure(faultPolicy, ProjectSaveFaultStage::ArchiveReplace);
    if (replaceFailure != 0)
    {
        Report(observer, ProjectIoStage::StagingToDestinationFailed, replaceFailure);
        return ProjectIoStatus::Encode;
    }
    if (IsFile(dest.c_str()))
    {
        if (!ReplaceFileW(dest.c_str(), temp.c_str(), nullptr, REPLACEFILE_IGNORE_MERGE_ERRORS, nullptr, nullptr) &&
            !MoveFileExW(temp.c_str(), dest.c_str(), MOVEFILE_REPLACE_EXISTING))
        {
            return ProjectIoStatus::Encode;
        }
        cleanupTemp.release();
        Report(observer, ProjectIoStage::Published);
        return ProjectIoStatus::Ok;
    }
    if (!MoveFileW(temp.c_str(), dest.c_str()))
    {
        return ProjectIoStatus::Encode;
    }
    cleanupTemp.release();
    Report(observer, ProjectIoStage::Published);
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

ProjectLoadResult LoadProject(const wchar_t* path, CancelToken cancellation, ProjectIoObserver observer,
                              ProjectLoadPixelLimits pixelLimits)
{
    ProjectLoadResult result{};
    if (cancellation.IsCancelled())
    {
        result.status = ProjectIoStatus::Cancelled;
        return result;
    }
    if (path == nullptr)
    {
        result.status = ProjectIoStatus::Invalid;
        return result;
    }
    if (pixelLimits.imagePixels < 0 || pixelLimits.maskPixels < 0)
    {
        result.status = ProjectIoStatus::Invalid;
        return result;
    }
    pixelLimits.imagePixels = std::min(pixelLimits.imagePixels, kMaxLayerPixels);
    pixelLimits.maskPixels = std::min(pixelLimits.maskPixels, kMaxMaskPixels);
    pixelLimits.layerCount = (std::min)(pixelLimits.layerCount, static_cast<std::size_t>(kMaxLayerCount));
    pixelLimits.manifestBytes = (std::min)(pixelLimits.manifestBytes, kMaxProjectManifestBytes);
    pixelLimits.encodedAssetBytes = (std::min)(pixelLimits.encodedAssetBytes, kMaxProjectEncodedAssetBytes);
    Manifest manifest{};
    const ProjectIoStatus status = IsDirectory(path)
                                       ? LoadExploded(FullPath(path), manifest, cancellation, observer, pixelLimits)
                                       : LoadZip(path, manifest, cancellation, observer, pixelLimits);
    result.status = status;
    if (cancellation.IsCancelled())
    {
        result.status = ProjectIoStatus::Cancelled;
        return result;
    }
    if (status == ProjectIoStatus::UnsupportedVersion)
        result.formatVersion = manifest.version;
    if (status == ProjectIoStatus::Ok)
    {
        result.document = std::move(manifest.document);
        result.activeLayerID = manifest.activeLayerID;
        result.formatVersion = manifest.version;
    }
    return result;
}

namespace
{
[[nodiscard]] ProjectIoStatus ReadProjectAssetHeader(const std::wstring& root, const std::string& name, zip_t* archive,
                                                     std::array<std::uint8_t, 33>& header, std::size_t& encodedBytes,
                                                     CancelToken cancellation)
{
    if (cancellation.IsCancelled())
        return ProjectIoStatus::Cancelled;
    if (archive != nullptr)
    {
        const std::string entry = "images/" + name;
        zip_stat_t info{};
        zip_stat_init(&info);
        if (zip_stat(archive, entry.c_str(), 0, &info) != 0 || (info.valid & ZIP_STAT_SIZE) == 0)
            return ProjectIoStatus::MissingImage;
        if (info.size > kMaxProjectEncodedAssetBytes)
            return ProjectIoStatus::TooLarge;
        if (info.size < header.size())
            return ProjectIoStatus::MissingImage;
        unique_zip_file file{zip_fopen(archive, entry.c_str(), 0)};
        if (!file)
            return ProjectIoStatus::MissingImage;
        for (std::size_t offset = 0; offset < header.size();)
        {
            if (cancellation.IsCancelled())
                return ProjectIoStatus::Cancelled;
            const auto read = zip_fread(file.get(), header.data() + offset, header.size() - offset);
            if (read <= 0 || static_cast<std::uint64_t>(read) > header.size() - offset)
                return ProjectIoStatus::MissingImage;
            offset += static_cast<std::size_t>(read);
        }
        encodedBytes = static_cast<std::size_t>(info.size);
    }
    else
    {
        const std::wstring path = FullPath((root + L"\\images\\" + Utf8ToWide(name)).c_str());
        const auto attributes = [](const std::wstring& value) -> std::uint32_t
        { return GetFileAttributesW(value.c_str()); };
        if (!ExplodedMemberPathAllowed(root, path, attributes))
            return ProjectIoStatus::Invalid;
        wil::unique_hfile file{CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                           FILE_FLAG_OPEN_REPARSE_POINT | FILE_ATTRIBUTE_NORMAL, nullptr)};
        if (!file)
            return ProjectIoStatus::MissingImage;
        BY_HANDLE_FILE_INFORMATION info{};
        LARGE_INTEGER size{};
        if (!GetFileInformationByHandle(file.get(), &info) ||
            (info.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) != 0 ||
            !GetFileSizeEx(file.get(), &size) || size.QuadPart < static_cast<LONGLONG>(header.size()))
            return ProjectIoStatus::MissingImage;
        if (static_cast<ULONGLONG>(size.QuadPart) > kMaxProjectEncodedAssetBytes)
            return ProjectIoStatus::TooLarge;
        DWORD read = 0;
        if (!ReadFile(file.get(), header.data(), static_cast<DWORD>(header.size()), &read, nullptr) ||
            read != header.size())
            return ProjectIoStatus::MissingImage;
        encodedBytes = static_cast<std::size_t>(size.QuadPart);
    }
    return cancellation.IsCancelled() ? ProjectIoStatus::Cancelled : ProjectIoStatus::Ok;
}

[[nodiscard]] ProjectIoStatus ProjectHeaderPixels(const std::array<std::uint8_t, 33>& header, bool mask,
                                                  int& pixels) noexcept
{
    constexpr std::array<std::uint8_t, 16> prefix{137, 80, 78, 71, 13, 10, 26, 10, 0, 0, 0, 13, 73, 72, 68, 82};
    if (!std::equal(prefix.begin(), prefix.end(), header.begin()) || (mask && (header[24] != 8 || header[25] != 0)))
        return ProjectIoStatus::MissingImage;
    const auto be32 = [&](std::size_t index) noexcept
    {
        return static_cast<std::uint32_t>(header[index]) << 24 | static_cast<std::uint32_t>(header[index + 1]) << 16 |
               static_cast<std::uint32_t>(header[index + 2]) << 8 | header[index + 3];
    };
    const auto width = be32(16);
    const auto height = be32(20);
    if (width == 0 || height == 0)
        return ProjectIoStatus::MissingImage;
    const auto count = static_cast<std::uint64_t>(width) * height;
    if (width > static_cast<std::uint32_t>(kMaxCanvasSide) || height > static_cast<std::uint32_t>(kMaxCanvasSide) ||
        count > kMaxLayerPixels)
        return ProjectIoStatus::TooLarge;
    pixels = static_cast<int>(count);
    return ProjectIoStatus::Ok;
}
} // namespace

ProjectLoadProbeResult ProbeProjectLoad(const wchar_t* path, CancelToken cancellation, ProjectIoObserver observer)
{
    ProjectLoadProbeResult result{};
    if (cancellation.IsCancelled())
    {
        result.status = ProjectIoStatus::Cancelled;
        return result;
    }
    if (path == nullptr || path[0] == L'\0')
        return result;
    const bool folder = IsDirectory(path);
    const auto root = folder ? FullPath(path) : std::wstring{};
    unique_zip archive;
    std::vector<std::uint8_t> bytes;
    if (folder)
    {
        const auto metadata = FullPath((root + L"\\manifest.json").c_str());
        const auto attributes = [](const std::wstring& value) -> std::uint32_t
        { return GetFileAttributesW(value.c_str()); };
        if (!ExplodedMemberPathAllowed(root, metadata, attributes))
            return result;
        result.status = ReadFileBytes(metadata.c_str(), kMaxProjectManifestBytes, bytes, cancellation);
    }
    else
    {
        archive = OpenZipArchive(path, ZIP_RDONLY);
        if (!archive)
            return result;
        result.status = ReadZipEntry(archive.get(), "manifest.json", kMaxProjectManifestBytes, bytes, cancellation);
        if (result.status == ProjectIoStatus::MissingImage)
            result.status = ProjectIoStatus::Invalid;
    }
    if (result.status != ProjectIoStatus::Ok)
        return result;
    Manifest manifest;
    result.status = ParseManifest(bytes, manifest, cancellation);
    result.formatVersion = manifest.version;
    if (result.status != ProjectIoStatus::Ok)
        return result;
    ProjectLoadLimits limits{0, 0, manifest.records.size(), bytes.size(), 0};
    Report(observer, ProjectIoStage::ManifestReady);
    unsigned int inspected = 0;
    for (const auto& record : manifest.records)
    {
        if (cancellation.IsCancelled())
        {
            result.status = ProjectIoStatus::Cancelled;
            return result;
        }
        const auto inspect = [&](const std::string& file, bool mask) -> ProjectIoStatus
        {
            if (file.empty())
                return ProjectIoStatus::Ok;
            std::array<std::uint8_t, 33> header{};
            std::size_t encoded = 0;
            auto status = ReadProjectAssetHeader(root, file, archive.get(), header, encoded, cancellation);
            if (status != ProjectIoStatus::Ok)
                return status;
            int pixels = 0;
            status = ProjectHeaderPixels(header, mask, pixels);
            if (status != ProjectIoStatus::Ok)
                return status;
            auto& total = mask ? limits.maskPixels : limits.imagePixels;
            const auto maximum = mask ? kMaxMaskPixels : kMaxLayerPixels;
            if (pixels > maximum - total)
                return ProjectIoStatus::TooLarge;
            total += pixels;
            limits.encodedAssetBytes = (std::max)(limits.encodedAssetBytes, encoded);
            Report(observer, ProjectIoStage::AssetInspected, ++inspected);
            return ProjectIoStatus::Ok;
        };
        result.status = inspect(record.imageFile, false);
        if (result.status == ProjectIoStatus::Ok)
            result.status = inspect(record.maskFile, true);
        if (result.status != ProjectIoStatus::Ok)
            return result;
    }
    if (cancellation.IsCancelled())
        result.status = ProjectIoStatus::Cancelled;
    else
        result.limits = limits;
    return result;
}

ProjectIoStatus SaveProject(const wchar_t* path, const CanvasDocument& document,
                            const std::optional<Guid>& activeLayerID, bool zipArchive, CancelToken cancellation,
                            ProjectIoObserver observer, ProjectSaveFaultPolicy faultPolicy)
{
    if (cancellation.IsCancelled())
        return ProjectIoStatus::Cancelled;
    if (path == nullptr || ValidateDocument(document) != DocumentStatus::Ok)
    {
        return ProjectIoStatus::Invalid;
    }
    std::string json;
    if (!WriteManifest(document, activeLayerID, json, cancellation))
    {
        if (cancellation.IsCancelled())
            return ProjectIoStatus::Cancelled;
        return json.size() > kMaxManifestBytes ? ProjectIoStatus::TooLarge : ProjectIoStatus::Encode;
    }
    const std::wstring destination = FullPath(path);
    if (destination.empty() || !std::filesystem::path(destination).has_filename())
        return ProjectIoStatus::Invalid;
    const std::wstring staging = UniqueSiblingPath(destination);
    const auto cleanupScratch = std::make_unique_for_overwrite<TreeCleanupBuffer>();
    if (!CreateDirectoryW(staging.c_str(), nullptr))
    {
        Report(observer, ProjectIoStage::CreateFailed, GetLastError());
        return ProjectIoStatus::Encode;
    }
    const ProjectSaveFaultPolicy stagingFaultPolicy = zipArchive ? ProjectSaveFaultPolicy{} : faultPolicy;
    auto cleanup = wil::scope_exit(
        [&]() noexcept
        {
            (void)DeleteTree(staging, ProjectSaveFaultStage::StagingCleanup, ProjectIoStage::StagingCleanupFailed,
                             stagingFaultPolicy, observer, *cleanupScratch);
        });
    const std::wstring images = staging + L"\\images";
    if (!CreateDirectoryW(images.c_str(), nullptr))
    {
        Report(observer, ProjectIoStage::CreateFailed, GetLastError());
        return ProjectIoStatus::Encode;
    }
    const auto manifestStatus =
        WriteFileBytes((staging + L"\\manifest.json").c_str(), reinterpret_cast<const std::uint8_t*>(json.data()),
                       json.size(), cancellation, observer, faultPolicy);
    if (manifestStatus != ProjectIoStatus::Ok)
        return manifestStatus;
    // Assets are bounded direct-to-file encodes. libzip later streams these files using STORE instead of
    // retaining every PNG vector (and a second WIC memory-stream copy) through archive close.
    std::vector<std::string> files;
    const ProjectIoStatus encoded = EncodeLayerAssets(document, images, files, cancellation, observer);
    if (encoded != ProjectIoStatus::Ok)
    {
        return encoded;
    }
    const auto status = zipArchive
                            ? SaveZip(path, json, images, files, cancellation, observer, faultPolicy)
                            : SaveExploded(destination, staging, cancellation, observer, faultPolicy, *cleanupScratch);
    if (status == ProjectIoStatus::Ok && !zipArchive)
        cleanup.release(); // rotation transferred the directory; this name no longer denotes our staging.
    return status;
}

ProjectIoStatus ImportImageOnto(CanvasDocument& document, const wchar_t* path, std::optional<Guid>& activeLayerID,
                                std::optional<Vec2> center, CancelToken cancellation, ProjectIoObserver observer,
                                int maximumImagePixels, std::uint64_t maximumEncodedBytes)
{
    if (cancellation.IsCancelled())
        return ProjectIoStatus::Cancelled;
    if (document.layers.size() >= static_cast<std::size_t>(kMaxLayerCount))
        return ProjectIoStatus::TooLarge;

    // Match the aggregate image limit before WIC allocates output. Validate dimensions before multiplying;
    // malformed source records cannot wrap the count or increase the available allowance.
    std::int64_t usedImagePixels = 0;
    for (const ImageLayer& layer : document.layers)
    {
        if (cancellation.IsCancelled())
            return ProjectIoStatus::Cancelled;
        if (layer.asset.has_value())
        {
            const Raster& image = layer.asset.value().image;
            const std::int64_t width = image.Width();
            const std::int64_t height = image.Height();
            if (width <= 0 || height <= 0)
                return ProjectIoStatus::Invalid;
            if (width > kMaxCanvasSide || height > kMaxCanvasSide)
                return ProjectIoStatus::TooLarge;
            const std::int64_t pixels = width * height;
            if (pixels > static_cast<std::int64_t>(kMaxLayerPixels) - usedImagePixels)
                return ProjectIoStatus::TooLarge;
            usedImagePixels += pixels;
        }
    }
    const int remainingPixels =
        (std::min)(maximumImagePixels, static_cast<int>(static_cast<std::int64_t>(kMaxLayerPixels) - usedImagePixels));
    if (remainingPixels <= 0)
        return ProjectIoStatus::TooLarge;

    DecodedImage decoded{};
    const ImageCodecStatus status = ImportImageFile(path, remainingPixels, decoded, cancellation, maximumEncodedBytes);
    if (status == ImageCodecStatus::Cancelled || cancellation.IsCancelled())
        return ProjectIoStatus::Cancelled;
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
    const std::int64_t decodedPixels = static_cast<std::int64_t>(decoded.image.Width()) * decoded.image.Height();
    if (decodedPixels <= 0)
        return ProjectIoStatus::Invalid;
    if (decodedPixels > remainingPixels)
        return ProjectIoStatus::TooLarge;
    Report(observer, ProjectIoStage::AssetDecoded, 1);
    if (cancellation.IsCancelled())
        return ProjectIoStatus::Cancelled;
    ImageLayer layer = ImageLayer::Blank(
        decoded.name.empty() ? L"Layer" : decoded.name,
        Vec2{static_cast<double>(decoded.image.Width()), static_cast<double>(decoded.image.Height())});
    layer.asset = RasterAsset{decoded.image, decoded.thumbnail, layer.name};
    const bool establishesCanvas = document.layers.empty();
    if (!establishesCanvas)
    {
        // legacy EditorSession.insert: centered on the drop point, else on the canvas, above the active layer inside
        // its folder (`document.layers.append` puts it topmost of whichever layers share its parentID; parentID is
        // the active layer's group, or the group it is inside).
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
        const Vec2 middle =
            center.has_value() && std::isfinite(center->x) && std::isfinite(center->y)
                ? center.value()
                : Vec2{static_cast<double>(document.width) / 2.0, static_cast<double>(document.height) / 2.0};
        layer.transform.origin = Vec2{std::floor(middle.x - layer.transform.size.x / 2.0),
                                      std::floor(middle.y - layer.transform.size.y / 2.0)};
        if (active != nullptr)
        {
            layer.parentID = active->isGroup ? std::optional<Guid>(active->id) : active->parentID;
        }
    }
    if (cancellation.IsCancelled())
        return ProjectIoStatus::Cancelled;
    auto draft = CanvasDocumentDraft::Clone(document, document.layers.size() + 1, cancellation);
    if (!draft)
        return cancellation.IsCancelled() ? ProjectIoStatus::Cancelled : ProjectIoStatus::TooLarge;
    if (establishesCanvas)
    {
        draft.value().DocumentState().width = decoded.image.Width();
        draft.value().DocumentState().height = decoded.image.Height();
    }
    const Guid importedID = layer.id;
    if (!draft.value().Append(std::move(layer)))
        return ProjectIoStatus::TooLarge;
    auto after = draft.value().Seal(cancellation);
    if (!after)
        return cancellation.IsCancelled() ? ProjectIoStatus::Cancelled : ProjectIoStatus::Invalid;
    const DocumentStatus validated = ValidateDocument(after.value());
    if (validated != DocumentStatus::Ok || cancellation.IsCancelled())
    {
        if (cancellation.IsCancelled())
            return ProjectIoStatus::Cancelled;
        return validated == DocumentStatus::TooLarge ? ProjectIoStatus::TooLarge : ProjectIoStatus::Invalid;
    }
    std::swap(document, after.value());
    activeLayerID = importedID;
    return ProjectIoStatus::Ok;
}

ProjectIoStatus ExportDocumentPng(const Raster& flattened, double resolution, const wchar_t* path,
                                  CancelToken cancellation)
{
    const auto status = EncodePngFile(flattened, resolution, path, cancellation);
    return status == ImageCodecStatus::Ok          ? ProjectIoStatus::Ok
           : status == ImageCodecStatus::Cancelled ? ProjectIoStatus::Cancelled
                                                   : ProjectIoStatus::Encode;
}

ProjectIoStatus ExportDocumentJpeg(const Raster& flattened, const JpegExportOptions& options, const wchar_t* path,
                                   CancelToken cancellation)
{
    const auto status = EncodeJpegFile(flattened, options, path, cancellation);
    return status == ImageCodecStatus::Ok          ? ProjectIoStatus::Ok
           : status == ImageCodecStatus::Cancelled ? ProjectIoStatus::Cancelled
                                                   : ProjectIoStatus::Encode;
}
