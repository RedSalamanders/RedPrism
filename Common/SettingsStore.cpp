#include "SettingsStore.h"

#include "Utf8.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <memory>

#include <shlobj.h>
#include <wil/resource.h>
#include <windows.h>
#include <yyjson.h>

#pragma comment(lib, "shell32.lib")

namespace SettingsStore
{
namespace
{
using unique_yyjson_doc = wil::unique_any<yyjson_doc*, decltype(&yyjson_doc_free), yyjson_doc_free>;
using unique_yyjson_mut_doc = wil::unique_any<yyjson_mut_doc*, decltype(&yyjson_mut_doc_free), yyjson_mut_doc_free>;

struct FreeDeleter
{
    void operator()(char* text) const noexcept
    {
        std::free(text);
    }
};

constexpr std::size_t kMaxFileBytes = 16u * 1024u * 1024u;

[[nodiscard]] std::optional<int> SchemaOf(yyjson_val* root) noexcept
{
    yyjson_val* schema = yyjson_obj_get(root, "schema");
    if (schema == nullptr || !yyjson_is_int(schema))
    {
        return std::nullopt;
    }
    const std::int64_t value = yyjson_get_sint(schema);
    if (value < 1 || value > INT_MAX)
    {
        return std::nullopt;
    }
    return static_cast<int>(value);
}

[[nodiscard]] bool ReadNumber(yyjson_val* object, const char* key, double& out) noexcept
{
    yyjson_val* value = yyjson_obj_get(object, key);
    if (value == nullptr || !yyjson_is_num(value))
    {
        return false;
    }
    out = yyjson_get_num(value);
    return std::isfinite(out);
}

[[nodiscard]] bool ReadInt(yyjson_val* object, const char* key, int& out) noexcept
{
    yyjson_val* value = yyjson_obj_get(object, key);
    if (value == nullptr || !yyjson_is_int(value))
    {
        return false;
    }
    const std::int64_t number = yyjson_get_sint(value);
    if (number < INT_MIN || number > INT_MAX)
    {
        return false;
    }
    out = static_cast<int>(number);
    return true;
}

[[nodiscard]] bool ReadBool(yyjson_val* object, const char* key, bool& out) noexcept
{
    yyjson_val* value = yyjson_obj_get(object, key);
    if (value == nullptr || !yyjson_is_bool(value))
    {
        return false;
    }
    out = yyjson_get_bool(value);
    return true;
}

[[nodiscard]] std::optional<std::string_view> ReadString(yyjson_val* object, const char* key) noexcept
{
    yyjson_val* value = yyjson_obj_get(object, key);
    if (value == nullptr || !yyjson_is_str(value))
    {
        return std::nullopt;
    }
    return std::string_view{yyjson_get_str(value), yyjson_get_len(value)};
}

// A BCP-47 tag as Windows spells it: letters, digits and hyphens, 2 to 35 characters, no hyphen at an end.
[[nodiscard]] bool LanguageTagIsPlausible(std::string_view tag) noexcept
{
    if (tag.size() < 2 || tag.size() > 35 || tag.front() == '-' || tag.back() == '-')
    {
        return false;
    }
    for (const char ch : tag)
    {
        const bool letter = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z');
        const bool digit = ch >= '0' && ch <= '9';
        if (!letter && !digit && ch != '-')
        {
            return false;
        }
    }
    return true;
}

[[nodiscard]] const char* ThemeName(ThemePreference theme) noexcept
{
    switch (theme)
    {
    case ThemePreference::Light:
        return "light";
    case ThemePreference::Dark:
        return "dark";
    case ThemePreference::System:
        break;
    }
    return "system";
}

[[nodiscard]] ThemePreference ThemeFrom(std::string_view name) noexcept
{
    if (name == "light")
    {
        return ThemePreference::Light;
    }
    if (name == "dark")
    {
        return ThemePreference::Dark;
    }
    return ThemePreference::System;
}

[[nodiscard]] bool EnsureDirectoryFor(const std::wstring& path)
{
    const std::size_t slash = path.find_last_of(L"\\/");
    if (slash == std::wstring::npos || slash < 2)
    {
        return true; // a relative name in the working directory
    }
    const std::wstring directory = path.substr(0, slash);
    for (std::size_t index = 3; index < directory.size(); ++index)
    {
        if (directory[index] == L'\\' || directory[index] == L'/')
        {
            (void)CreateDirectoryW(directory.substr(0, index).c_str(), nullptr);
        }
    }
    (void)CreateDirectoryW(directory.c_str(), nullptr);
    const DWORD attributes = GetFileAttributesW(directory.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}
} // namespace

std::wstring LocalAppDataFolder()
{
    wchar_t path[MAX_PATH] = {};
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, path)) || path[0] == 0)
    {
        return {};
    }
    return std::wstring{path};
}

std::wstring DefaultDirectory()
{
    const std::wstring root = LocalAppDataFolder();
    return root.empty() ? std::wstring{} : root + L"\\RedPrism";
}

const wchar_t* FileName(bool debugBuild) noexcept
{
    return debugBuild ? L"settings.debug.json" : L"settings.json";
}

std::wstring DefaultPath(bool debugBuild)
{
    const std::wstring directory = DefaultDirectory();
    return directory.empty() ? std::wstring{} : directory + L"\\" + FileName(debugBuild);
}

std::wstring BackupPathFor(const std::wstring& path)
{
    SYSTEMTIME time{};
    GetLocalTime(&time);
    wchar_t suffix[64];
    (void)swprintf_s(suffix, L".invalid-%04u%02u%02u-%02u%02u%02u-%03u", time.wYear, time.wMonth, time.wDay, time.wHour,
                     time.wMinute, time.wSecond, time.wMilliseconds);
    return path + suffix;
}

std::optional<AppSettings> Parse(std::string_view json)
{
    if (json.empty())
    {
        return std::nullopt;
    }
    const unique_yyjson_doc doc{yyjson_read(json.data(), json.size(), 0)};
    if (!doc)
    {
        return std::nullopt;
    }
    yyjson_val* root = yyjson_doc_get_root(doc.get());
    if (root == nullptr || !yyjson_is_obj(root) || !SchemaOf(root).has_value())
    {
        return std::nullopt;
    }
    AppSettings settings;
    if (const std::optional<std::string_view> theme = ReadString(root, "theme"); theme.has_value())
    {
        settings.theme = ThemeFrom(theme.value());
    }
    if (const std::optional<std::string_view> language = ReadString(root, "language");
        language.has_value() && LanguageTagIsPlausible(language.value()))
    {
        settings.language = Utf8ToWide(language.value());
    }
    double number = 0;
    if (ReadNumber(root, "layersPanelWidth", number))
    {
        settings.layersPanelWidth = std::clamp(number, AppSettings::kLayersPanelMin, AppSettings::kLayersPanelMax);
    }
    bool flag = false;
    if (ReadBool(root, "sampleRing", flag))
    {
        settings.sampleRing = flag;
    }
    if (ReadNumber(root, "jpegQuality", number))
    {
        settings.jpegQuality = std::clamp(number, 0.0, 1.0);
    }
    if (yyjson_val* window = yyjson_obj_get(root, "window"); window != nullptr && yyjson_is_obj(window))
    {
        WindowPlacementSetting placement;
        placement.valid = ReadInt(window, "x", placement.x) && ReadInt(window, "y", placement.y) &&
                          ReadInt(window, "width", placement.width) && ReadInt(window, "height", placement.height) &&
                          placement.width >= AppSettings::kWindowMinWidth &&
                          placement.height >= AppSettings::kWindowMinHeight && std::abs(placement.x) <= 32767 &&
                          std::abs(placement.y) <= 32767 && placement.width <= 32767 && placement.height <= 32767;
        if (placement.valid)
        {
            (void)ReadBool(window, "maximized", placement.maximized);
            settings.window = placement;
        }
    }
    return settings;
}

std::string Serialize(const AppSettings& settings, std::string_view existingJson)
{
    unique_yyjson_mut_doc doc;
    int schema = AppSettings::kSchema;
    if (!existingJson.empty())
    {
        const unique_yyjson_doc existing{yyjson_read(existingJson.data(), existingJson.size(), 0)};
        yyjson_val* existingRoot = existing ? yyjson_doc_get_root(existing.get()) : nullptr;
        if (existingRoot != nullptr && yyjson_is_obj(existingRoot))
        {
            if (const std::optional<int> existingSchema = SchemaOf(existingRoot); existingSchema.has_value())
            {
                doc.reset(yyjson_doc_mut_copy(existing.get(), nullptr));
                schema = std::max(schema, existingSchema.value());
            }
        }
    }
    yyjson_mut_val* root = nullptr;
    if (doc)
    {
        root = yyjson_mut_doc_get_root(doc.get());
    }
    else
    {
        doc.reset(yyjson_mut_doc_new(nullptr));
        if (!doc)
        {
            return {};
        }
        root = yyjson_mut_obj(doc.get());
        yyjson_mut_doc_set_root(doc.get(), root);
    }
    if (root == nullptr || !yyjson_mut_is_obj(root))
    {
        return {};
    }
    const auto put = [&](const char* key, yyjson_mut_val* value) noexcept
    {
        yyjson_mut_val* name = yyjson_mut_strcpy(doc.get(), key);
        return name != nullptr && value != nullptr && yyjson_mut_obj_put(root, name, value);
    };
    const std::string language = WideToUtf8(settings.language);
    bool ok = put("schema", yyjson_mut_int(doc.get(), schema));
    ok = put("theme", yyjson_mut_strcpy(doc.get(), ThemeName(settings.theme))) && ok;
    ok = put("language", yyjson_mut_strcpy(doc.get(), language.c_str())) && ok;
    ok = put("layersPanelWidth", yyjson_mut_real(doc.get(), settings.layersPanelWidth)) && ok;
    ok = put("sampleRing", yyjson_mut_bool(doc.get(), settings.sampleRing)) && ok;
    ok = put("jpegQuality", yyjson_mut_real(doc.get(), settings.jpegQuality)) && ok;
    if (settings.window.valid)
    {
        yyjson_mut_val* window = yyjson_mut_obj(doc.get());
        ok = window != nullptr && yyjson_mut_obj_add_int(doc.get(), window, "x", settings.window.x) &&
             yyjson_mut_obj_add_int(doc.get(), window, "y", settings.window.y) &&
             yyjson_mut_obj_add_int(doc.get(), window, "width", settings.window.width) &&
             yyjson_mut_obj_add_int(doc.get(), window, "height", settings.window.height) &&
             yyjson_mut_obj_add_bool(doc.get(), window, "maximized", settings.window.maximized) &&
             put("window", window) && ok;
    }
    else
    {
        (void)yyjson_mut_obj_remove_str(root, "window");
    }
    if (!ok)
    {
        return {};
    }
    std::size_t length = 0;
    const std::unique_ptr<char, FreeDeleter> text{yyjson_mut_write(doc.get(), YYJSON_WRITE_PRETTY, &length)};
    if (!text)
    {
        return {};
    }
    std::string result(text.get(), length);
    result += '\n';
    return result;
}

SettingsLoadResult LoadCold(const std::wstring& path)
{
    SettingsLoadResult result;
    std::string bytes;
    unsigned long error = 0;
    if (!ReadFileBytes(path, bytes, &error))
    {
        if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND)
        {
            result.status = SettingsLoadStatus::Missing;
            result.templateWritten = WriteFileAtomic(path, Serialize(result.settings));
        }
        else
        {
            result.status = SettingsLoadStatus::Unreadable;
        }
        return result;
    }
    if (const std::optional<AppSettings> parsed = Parse(bytes); parsed.has_value())
    {
        result.settings = parsed.value();
        return result;
    }
    result.status = SettingsLoadStatus::Invalid;
    const std::wstring backup = BackupPathFor(path);
    if (MoveFileExW(path.c_str(), backup.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        result.backupPath = backup;
        result.templateWritten = WriteFileAtomic(path, Serialize(result.settings));
    }
    return result;
}

std::optional<AppSettings> LoadLive(const std::wstring& path)
{
    std::string bytes;
    if (!ReadFileBytes(path, bytes))
    {
        return std::nullopt;
    }
    return Parse(bytes);
}

bool Save(const std::wstring& path, const AppSettings& settings, std::wstring* backupPath)
{
    std::string existing;
    std::string_view merge;
    if (ReadFileBytes(path, existing))
    {
        if (Parse(existing).has_value())
        {
            merge = existing;
        }
        else
        {
            const std::wstring backup = BackupPathFor(path);
            if (MoveFileExW(path.c_str(), backup.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) &&
                backupPath != nullptr)
            {
                *backupPath = backup;
            }
        }
    }
    const std::string text = Serialize(settings, merge);
    return !text.empty() && WriteFileAtomic(path, text);
}

bool ReadFileBytes(const std::wstring& path, std::string& bytes, unsigned long* lastError)
{
    const wil::unique_hfile file{CreateFileW(path.c_str(), GENERIC_READ,
                                             FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                             OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)};
    if (!file)
    {
        if (lastError != nullptr)
        {
            *lastError = GetLastError();
        }
        return false;
    }
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file.get(), &size) || size.QuadPart < 0 ||
        static_cast<std::uint64_t>(size.QuadPart) > kMaxFileBytes)
    {
        if (lastError != nullptr)
        {
            *lastError = ERROR_FILE_TOO_LARGE;
        }
        return false;
    }
    bytes.resize(static_cast<std::size_t>(size.QuadPart));
    std::size_t offset = 0;
    while (offset < bytes.size())
    {
        DWORD read = 0;
        const DWORD chunk = static_cast<DWORD>(std::min<std::size_t>(bytes.size() - offset, 1u << 20));
        if (!ReadFile(file.get(), bytes.data() + offset, chunk, &read, nullptr))
        {
            if (lastError != nullptr)
            {
                *lastError = GetLastError();
            }
            return false;
        }
        if (read == 0)
        {
            bytes.resize(offset);
            break;
        }
        offset += read;
    }
    if (lastError != nullptr)
    {
        *lastError = ERROR_SUCCESS;
    }
    return true;
}

bool WriteFileAtomic(const std::wstring& path, std::string_view bytes)
{
    if (path.empty() || !EnsureDirectoryFor(path))
    {
        return false;
    }
    const std::wstring temporary = path + L".tmp-" + std::to_wstring(GetCurrentProcessId());
    {
        const wil::unique_hfile file{
            CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr)};
        if (!file)
        {
            return false;
        }
        std::size_t offset = 0;
        while (offset < bytes.size())
        {
            DWORD written = 0;
            const DWORD chunk = static_cast<DWORD>(std::min<std::size_t>(bytes.size() - offset, 1u << 20));
            if (!WriteFile(file.get(), bytes.data() + offset, chunk, &written, nullptr) || written == 0)
            {
                (void)DeleteFileW(temporary.c_str());
                return false;
            }
            offset += written;
        }
        (void)FlushFileBuffers(file.get());
    }
    if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        (void)DeleteFileW(temporary.c_str());
        return false;
    }
    return true;
}
} // namespace SettingsStore
