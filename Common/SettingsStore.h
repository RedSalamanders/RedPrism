#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

// The user settings document (Specs/Core/Core_Settings.md): a JSON object under %LocalAppData%\RedPrism whose
// `schema` field carries compatibility (the file name tells Debug from Release). Fields the store does not know
// survive a save; a field it knows but cannot use falls back to its default.

enum class ThemePreference : std::uint8_t
{
    System, // follow Windows' light or dark apps setting
    Light,
    Dark,
};

struct WindowPlacementSetting
{
    bool valid = false; // false: Windows places the window
    int x = 0;          // the restored (normal) rectangle in screen pixels
    int y = 0;
    int width = 0;
    int height = 0;
    bool maximized = false;
    [[nodiscard]] bool operator==(const WindowPlacementSetting&) const noexcept = default;
};

struct AppSettings
{
    static constexpr int kSchema = 1;
    static constexpr double kLayersPanelMin = 100; // DIP; the chrome clamps again to its own range
    static constexpr double kLayersPanelMax = 2000;
    static constexpr int kWindowMinWidth = 200; // pixels; smaller placements are ignored
    static constexpr int kWindowMinHeight = 150;

    ThemePreference theme = ThemePreference::System;
    std::wstring language;         // BCP-47 tag of a satellite string table; empty follows Windows
    double layersPanelWidth = 240; // DIP (the legacy layersPanelWidth)
    bool sampleRing = true;        // View > Sample Ring
    double jpegQuality = 0.8;      // 0..1 (the legacy JPEG export quality)
    WindowPlacementSetting window;
    [[nodiscard]] bool operator==(const AppSettings&) const noexcept = default;
};

enum class SettingsLoadStatus : std::uint8_t
{
    Loaded,     // the file's settings
    Missing,    // no file: the defaults, and the template was written
    Invalid,    // not a settings document: the bytes were backed up, the template written, the defaults used
    Unreadable, // an I/O failure: the defaults, nothing written
};

struct SettingsLoadResult
{
    AppSettings settings;
    SettingsLoadStatus status = SettingsLoadStatus::Loaded;
    std::wstring backupPath; // Invalid: where the bytes went (empty when the backup itself failed)
    bool templateWritten = false;
};

namespace SettingsStore
{
// <LocalAppData>; empty when the known folder is unavailable.
[[nodiscard]] std::wstring LocalAppDataFolder();
// <LocalAppData>\RedPrism
[[nodiscard]] std::wstring DefaultDirectory();
// settings.json (Release) or settings.debug.json (Debug builds keep their own file).
[[nodiscard]] const wchar_t* FileName(bool debugBuild) noexcept;
[[nodiscard]] std::wstring DefaultPath(bool debugBuild);
// <path>.invalid-<yyyyMMdd-HHmmss>
[[nodiscard]] std::wstring BackupPathFor(const std::wstring& path);

// A settings document: an object with an integral `schema` of at least 1. Known fields are validated one by one
// (an unusable value takes its default); unknown ones are ignored. nullopt for anything else.
[[nodiscard]] std::optional<AppSettings> Parse(std::string_view json);
// The document for `settings`, pretty-printed UTF-8. When `existingJson` is a settings document its other fields
// and a newer schema are kept.
[[nodiscard]] std::string Serialize(const AppSettings& settings, std::string_view existingJson = {});

// Startup: a missing file writes the template, an invalid one is backed up and replaced by the template.
[[nodiscard]] SettingsLoadResult LoadCold(const std::wstring& path);
// A running app: the document, or nullopt when the file is missing or invalid (the caller keeps its last-good
// settings). Never writes.
[[nodiscard]] std::optional<AppSettings> LoadLive(const std::wstring& path);
// Writes atomically (a temporary file replaced into place), creating the directory; an invalid existing file is
// backed up first (`backupPath` names it), a valid one keeps its unknown fields.
[[nodiscard]] bool Save(const std::wstring& path, const AppSettings& settings, std::wstring* backupPath = nullptr);

// Whole-file helpers shared with the crash marker. ReadFileBytes is false for a missing or unreadable file;
// `lastError` (optional) receives GetLastError. WriteFileAtomic writes <path>.tmp-<pid> and moves it into place.
[[nodiscard]] bool ReadFileBytes(const std::wstring& path, std::string& bytes, unsigned long* lastError = nullptr);
[[nodiscard]] bool WriteFileAtomic(const std::wstring& path, std::string_view bytes);
} // namespace SettingsStore
