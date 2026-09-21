#include "Localization.h"

#include <algorithm>

namespace Localization
{
namespace
{
void AddCandidate(std::vector<std::wstring>& out, std::wstring_view directory, std::wstring_view tag)
{
    if (tag.empty())
    {
        return;
    }
    std::wstring path{directory};
    path += L'\\';
    path += kSatelliteFolder;
    path += L'\\';
    path.append(tag);
    path += L'\\';
    path += kSatelliteName;
    if (std::find(out.begin(), out.end(), path) == out.end())
    {
        out.push_back(std::move(path));
    }
}

// "fr-FR" then "fr": a regional table when there is one, the language's otherwise.
void AddTagFamily(std::vector<std::wstring>& out, std::wstring_view directory, std::wstring_view tag)
{
    AddCandidate(out, directory, tag);
    const std::size_t dash = tag.find(L'-');
    if (dash != std::wstring_view::npos && dash > 0)
    {
        AddCandidate(out, directory, tag.substr(0, dash));
    }
}
} // namespace

std::vector<std::wstring> SystemUiLanguages()
{
    std::vector<std::wstring> languages;
    ULONG count = 0;
    ULONG size = 0;
    if (GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &count, nullptr, &size) && size > 1)
    {
        std::wstring buffer(size, L'\0');
        if (GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &count, buffer.data(), &size))
        {
            std::size_t start = 0;
            while (start < buffer.size() && buffer[start] != L'\0')
            {
                std::size_t end = buffer.find(L'\0', start);
                if (end == std::wstring::npos)
                {
                    end = buffer.size();
                }
                languages.emplace_back(buffer.substr(start, end - start));
                start = end + 1;
            }
        }
    }
    if (languages.empty())
    {
        wchar_t locale[LOCALE_NAME_MAX_LENGTH] = {};
        if (GetUserDefaultLocaleName(locale, LOCALE_NAME_MAX_LENGTH) > 0 && locale[0] != L'\0')
        {
            languages.emplace_back(locale);
        }
    }
    return languages;
}

std::vector<std::wstring> SatelliteCandidates(std::wstring_view executableDirectory, std::wstring_view preferredTag,
                                              const std::vector<std::wstring>& systemLanguages)
{
    std::vector<std::wstring> candidates;
    std::wstring_view directory = executableDirectory;
    while (!directory.empty() && (directory.back() == L'\\' || directory.back() == L'/'))
    {
        directory.remove_suffix(1);
    }
    AddTagFamily(candidates, directory, preferredTag);
    for (const std::wstring& language : systemLanguages)
    {
        AddTagFamily(candidates, directory, language);
    }
    return candidates;
}

HMODULE LoadSatelliteModule(const std::vector<std::wstring>& candidates) noexcept
{
    for (const std::wstring& path : candidates)
    {
        if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES)
        {
            continue;
        }
        if (const HMODULE module =
                LoadLibraryExW(path.c_str(), nullptr, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE))
        {
            return module;
        }
    }
    return nullptr;
}

std::wstring ExecutableDirectory()
{
    std::wstring path(MAX_PATH, L'\0');
    for (;;)
    {
        const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (length == 0)
        {
            return {};
        }
        if (length < path.size() - 1)
        {
            path.resize(length);
            break;
        }
        path.resize(path.size() * 2);
    }
    const std::size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? std::wstring{} : path.substr(0, slash);
}
} // namespace Localization
