#include "SettingsStore.h"

#include "Utf8.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <format>
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
// Editors save UTF-8 with a byte order mark; the document is the same.
constexpr yyjson_read_flag kReadFlags = YYJSON_READ_ALLOW_BOM;

// An integral JSON number in int range: an integer, or a real with no fraction (10.0), as ProjectStore reads ints.
[[nodiscard]] std::optional<int> IntegralValue(yyjson_val* value) noexcept
{
    if (value == nullptr)
    {
        return std::nullopt;
    }
    if (yyjson_is_int(value))
    {
        if (yyjson_is_uint(value))
        {
            const std::uint64_t number = yyjson_get_uint(value);
            return number <= static_cast<std::uint64_t>(INT_MAX) ? std::optional<int>(static_cast<int>(number))
                                                                 : std::nullopt;
        }
        const std::int64_t number = yyjson_get_sint(value);
        return number >= INT_MIN && number <= INT_MAX ? std::optional<int>(static_cast<int>(number)) : std::nullopt;
    }
    if (yyjson_is_real(value))
    {
        const double number = yyjson_get_real(value);
        if (std::isfinite(number) && number == std::floor(number) && number >= static_cast<double>(INT_MIN) &&
            number <= static_cast<double>(INT_MAX))
        {
            return static_cast<int>(number);
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<int> SchemaOf(yyjson_val* root) noexcept
{
    const std::optional<int> schema = IntegralValue(yyjson_obj_get(root, "schema"));
    if (!schema.has_value() || schema.value() < 1)
    {
        return std::nullopt;
    }
    return schema;
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
    const std::optional<int> number = IntegralValue(yyjson_obj_get(object, key));
    if (!number.has_value())
    {
        return false;
    }
    out = number.value();
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

// One `shortcuts` entry: an object with a `commandId` starting with `cmd/`, optional boolean `ctrl`, `alt` and
// `shift`, and exactly one of `vk` (Common/KeyChord.h spellings, any case) or `keyPosition`. nullopt otherwise.
[[nodiscard]] std::optional<ShortcutBinding> ParseShortcut(yyjson_val* entry)
{
    if (entry == nullptr || !yyjson_is_obj(entry))
    {
        return std::nullopt;
    }
    const std::optional<std::string_view> commandId = ReadString(entry, "commandId");
    if (!commandId.has_value() || commandId->size() < 5 || !commandId->starts_with("cmd/"))
    {
        return std::nullopt;
    }
    ShortcutBinding binding;
    const auto readFlag = [entry](const char* name, bool& out) noexcept
    {
        yyjson_val* value = yyjson_obj_get(entry, name);
        if (value == nullptr)
        {
            return true;
        }
        if (!yyjson_is_bool(value))
        {
            return false;
        }
        out = yyjson_get_bool(value);
        return true;
    };
    if (!readFlag("ctrl", binding.chord.ctrl) || !readFlag("alt", binding.chord.alt) ||
        !readFlag("shift", binding.chord.shift))
    {
        return std::nullopt;
    }
    const bool hasVk = yyjson_obj_get(entry, "vk") != nullptr;
    const bool hasPosition = yyjson_obj_get(entry, "keyPosition") != nullptr;
    if (hasVk == hasPosition)
    {
        return std::nullopt;
    }
    if (hasVk)
    {
        const std::optional<std::string_view> vk = ReadString(entry, "vk");
        if (!vk.has_value() || !ParseStableKeyName(vk.value(), binding.chord.virtualKey))
        {
            return std::nullopt;
        }
    }
    else
    {
        const std::optional<std::string_view> position = ReadString(entry, "keyPosition");
        binding.chord.position = position.has_value() ? ParseKeyPositionName(position.value()) : KeyPosition::None;
        if (binding.chord.position == KeyPosition::None)
        {
            return std::nullopt;
        }
    }
    binding.commandId = Utf8ToWide(commandId.value());
    return binding;
}

// The entries of a `shortcuts` array that parse, in file order.
[[nodiscard]] std::vector<ShortcutBinding> ParseShortcuts(yyjson_val* array)
{
    std::vector<ShortcutBinding> bindings;
    if (array == nullptr || !yyjson_is_arr(array))
    {
        return bindings;
    }
    const std::size_t count = yyjson_arr_size(array);
    bindings.reserve(count);
    for (std::size_t index = 0; index < count; ++index)
    {
        if (std::optional<ShortcutBinding> binding = ParseShortcut(yyjson_arr_get(array, index)); binding.has_value())
        {
            bindings.push_back(std::move(binding.value()));
        }
    }
    return bindings;
}

// A fresh `shortcuts` entry for `binding`; nullptr when the document cannot allocate it.
[[nodiscard]] yyjson_mut_val* ShortcutEntry(yyjson_mut_doc* doc, const ShortcutBinding& binding)
{
    yyjson_mut_val* entry = yyjson_mut_obj(doc);
    if (entry == nullptr)
    {
        return nullptr;
    }
    const std::string commandId = WideToUtf8(binding.commandId);
    bool ok = yyjson_mut_obj_add_strcpy(doc, entry, "commandId", commandId.c_str());
    if (binding.chord.ctrl)
    {
        ok = yyjson_mut_obj_add_bool(doc, entry, "ctrl", true) && ok;
    }
    if (binding.chord.alt)
    {
        ok = yyjson_mut_obj_add_bool(doc, entry, "alt", true) && ok;
    }
    if (binding.chord.shift)
    {
        ok = yyjson_mut_obj_add_bool(doc, entry, "shift", true) && ok;
    }
    if (binding.chord.position != KeyPosition::None)
    {
        const std::string position{KeyPositionName(binding.chord.position)};
        ok = yyjson_mut_obj_add_strcpy(doc, entry, "keyPosition", position.c_str()) && ok;
    }
    else
    {
        const std::string vk = StableKeyName(binding.chord.virtualKey);
        ok = yyjson_mut_obj_add_strcpy(doc, entry, "vk", vk.c_str()) && ok;
    }
    return ok ? entry : nullptr;
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
    return path + std::format(L".invalid-{:04}{:02}{:02}-{:02}{:02}{:02}-{:03}", time.wYear, time.wMonth, time.wDay,
                              time.wHour, time.wMinute, time.wSecond, time.wMilliseconds);
}

std::optional<AppSettings> Parse(std::string_view json)
{
    if (json.empty())
    {
        return std::nullopt;
    }
    const unique_yyjson_doc doc{yyjson_read(json.data(), json.size(), kReadFlags)};
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
        // Every value fits ±32767 (compared directly: std::abs of INT_MIN has no int result).
        const auto fits = [](int value) noexcept { return value >= -32767 && value <= 32767; };
        placement.valid = ReadInt(window, "x", placement.x) && ReadInt(window, "y", placement.y) &&
                          ReadInt(window, "width", placement.width) && ReadInt(window, "height", placement.height) &&
                          placement.width >= AppSettings::kWindowMinWidth &&
                          placement.height >= AppSettings::kWindowMinHeight && fits(placement.x) && fits(placement.y) &&
                          fits(placement.width) && fits(placement.height);
        if (placement.valid)
        {
            (void)ReadBool(window, "maximized", placement.maximized);
            settings.window = placement;
        }
    }
    settings.shortcuts = ParseShortcuts(yyjson_obj_get(root, "shortcuts"));
    return settings;
}

std::string Serialize(const AppSettings& settings, std::string_view existingJson)
{
    unique_yyjson_mut_doc doc;
    unique_yyjson_doc existing;
    yyjson_val* existingShortcuts = nullptr;
    int schema = AppSettings::kSchema;
    if (!existingJson.empty())
    {
        existing.reset(yyjson_read(existingJson.data(), existingJson.size(), kReadFlags));
        yyjson_val* existingRoot = existing ? yyjson_doc_get_root(existing.get()) : nullptr;
        if (existingRoot != nullptr && yyjson_is_obj(existingRoot))
        {
            if (const std::optional<int> existingSchema = SchemaOf(existingRoot); existingSchema.has_value())
            {
                doc.reset(yyjson_doc_mut_copy(existing.get(), nullptr));
                schema = std::max(schema, existingSchema.value());
                existingShortcuts = yyjson_obj_get(existingRoot, "shortcuts");
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
    // `shortcuts`: left exactly as the file has it when its entries already say the settings' bindings. Otherwise
    // the entries this version cannot read stay (verbatim, first), then one entry per binding in order, reusing an
    // existing entry that reads the same so the fields inside it this version does not know survive.
    if (ParseShortcuts(existingShortcuts) != settings.shortcuts)
    {
        yyjson_mut_val* shortcuts = yyjson_mut_arr(doc.get());
        ok = shortcuts != nullptr && ok;
        std::vector<yyjson_val*> reusable;
        std::vector<ShortcutBinding> reusableBindings;
        if (shortcuts != nullptr && existingShortcuts != nullptr && yyjson_is_arr(existingShortcuts))
        {
            const std::size_t count = yyjson_arr_size(existingShortcuts);
            for (std::size_t index = 0; index < count; ++index)
            {
                yyjson_val* entry = yyjson_arr_get(existingShortcuts, index);
                if (std::optional<ShortcutBinding> binding = ParseShortcut(entry); binding.has_value())
                {
                    reusable.push_back(entry);
                    reusableBindings.push_back(std::move(binding.value()));
                }
                else
                {
                    ok = yyjson_mut_arr_add_val(shortcuts, yyjson_val_mut_copy(doc.get(), entry)) && ok;
                }
            }
        }
        for (const ShortcutBinding& binding : settings.shortcuts)
        {
            if (shortcuts == nullptr)
            {
                break;
            }
            yyjson_mut_val* entry = nullptr;
            for (std::size_t index = 0; index < reusable.size(); ++index)
            {
                if (reusable[index] != nullptr && reusableBindings[index] == binding)
                {
                    entry = yyjson_val_mut_copy(doc.get(), reusable[index]);
                    reusable[index] = nullptr;
                    break;
                }
            }
            if (entry == nullptr)
            {
                entry = ShortcutEntry(doc.get(), binding);
            }
            ok = yyjson_mut_arr_add_val(shortcuts, entry) && ok;
        }
        if (shortcuts != nullptr && yyjson_mut_arr_size(shortcuts) == 0)
        {
            (void)yyjson_mut_obj_remove_str(root, "shortcuts");
        }
        else
        {
            ok = put("shortcuts", shortcuts) && ok;
        }
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
        wil::unique_hfile file{
            CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr)};
        if (!file)
        {
            return false;
        }
        bool complete = true;
        std::size_t offset = 0;
        while (offset < bytes.size())
        {
            DWORD written = 0;
            const DWORD chunk = static_cast<DWORD>(std::min<std::size_t>(bytes.size() - offset, 1u << 20));
            if (!WriteFile(file.get(), bytes.data() + offset, chunk, &written, nullptr) || written == 0)
            {
                complete = false;
                break;
            }
            offset += written;
        }
        complete = complete && FlushFileBuffers(file.get()) != FALSE;
        // Closed before any delete: the temporary was opened without sharing, so DeleteFileW on the open file
        // fails and would leave it behind.
        file.reset();
        if (!complete)
        {
            (void)DeleteFileW(temporary.c_str());
            return false;
        }
    }
    if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        (void)DeleteFileW(temporary.c_str());
        return false;
    }
    return true;
}
} // namespace SettingsStore
