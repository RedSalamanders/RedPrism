#pragma once

#include <string>
#include <string_view>
#include <vector>

#include <windows.h>

// Satellite string tables (Specs/UI/UI_ApplicationChrome.md, Strings): <exe dir>\Languages\<tag>\RedPrism.resources.dll
// holds a translated STRINGTABLE with the executable's ids. A loaded satellite answers LoadStringResource(nullptr, id)
// first (Common/Resources.h); an id it lacks falls back to the embedded English.
namespace Localization
{
inline constexpr wchar_t kSatelliteFolder[] = L"Languages";
inline constexpr wchar_t kSatelliteName[] = L"RedPrism.resources.dll";

// The user's Windows display languages, most preferred first ("fr-FR", "en-US", …); the user locale when the
// list is unavailable.
[[nodiscard]] std::vector<std::wstring> SystemUiLanguages();

// The satellite paths to try in order: the preferred tag (the `language` setting) then its primary subtag, then
// each system language and its primary subtag, without repeats. Empty tags are skipped.
[[nodiscard]] std::vector<std::wstring> SatelliteCandidates(std::wstring_view executableDirectory,
                                                            std::wstring_view preferredTag,
                                                            const std::vector<std::wstring>& systemLanguages);

// The first candidate that exists and loads as a resource-only module (never executed); null when none does.
// The caller owns the module for the life of the process (FreeLibrary retires it).
[[nodiscard]] HMODULE LoadSatelliteModule(const std::vector<std::wstring>& candidates) noexcept;

// The running executable's directory without a trailing separator; empty when unknown.
[[nodiscard]] std::wstring ExecutableDirectory();
} // namespace Localization
