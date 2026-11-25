#include "Localization.h"

#include <algorithm>
#include <cwctype>
#include <utility>

namespace Localization
{
namespace
{
void AddCandidate(std::vector<std::wstring>& out, std::wstring path)
{
    if (std::find(out.begin(), out.end(), path) == out.end())
    {
        out.push_back(std::move(path));
    }
}

void AddTagPaths(std::vector<std::wstring>& out, std::wstring_view directory, std::wstring_view tag)
{
    std::wstring path{directory};
    path += L"\\Lang\\RedPrism-";
    path.append(tag);
    path += L".dll";
    AddCandidate(out, std::move(path));

    std::wstring previous{directory};
    previous += L"\\Languages\\";
    previous.append(tag);
    previous += L"\\RedPrism.resources.dll";
    AddCandidate(out, std::move(previous));
}

// "fr-FR" then "fr": a regional table when there is one, the language's otherwise.
[[nodiscard]] bool AddTagFamily(std::vector<std::wstring>& out, std::wstring_view directory, std::wstring_view tag)
{
    const auto isTagCharacter = [](wchar_t character) noexcept
    {
        return (character >= L'a' && character <= L'z') || (character >= L'A' && character <= L'Z') || (character >= L'0' && character <= L'9') ||
               character == L'-';
    };
    if (tag.empty() || tag.size() >= LOCALE_NAME_MAX_LENGTH || tag.front() == L'-' || tag.back() == L'-' || tag.find(L"--") != std::wstring_view::npos ||
        ! std::all_of(tag.begin(), tag.end(), isTagCharacter))
    {
        return false;
    }

    AddTagPaths(out, directory, tag);
    const std::size_t dash = tag.find(L'-');
    if (dash != std::wstring_view::npos)
    {
        AddTagPaths(out, directory, tag.substr(0, dash));
    }
    return true;
}

// Whether `tag` is the embedded table's language ("en", "EN-gb").
[[nodiscard]] bool IsEmbeddedLanguage(std::wstring_view tag) noexcept
{
    const std::wstring_view primary = tag.substr(0, tag.find(L'-'));
    return primary.size() == kEmbeddedLanguage.size() && std::equal(primary.begin(), primary.end(), kEmbeddedLanguage.begin(), [](wchar_t left, wchar_t right) {
        return std::towlower(left) == std::towlower(right);
    });
}
} // namespace

std::vector<std::wstring> SystemUiLanguages()
{
    std::vector<std::wstring> languages;
    ULONG count = 0;
    ULONG size  = 0;
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

std::vector<std::wstring> SatelliteCandidates(std::wstring_view executableDirectory,
                                              std::wstring_view preferredTag,
                                              const std::vector<std::wstring>& systemLanguages)
{
    std::vector<std::wstring> candidates;
    std::wstring_view directory = executableDirectory;
    while (! directory.empty() && (directory.back() == L'\\' || directory.back() == L'/'))
    {
        directory.remove_suffix(1);
    }
    if (directory.empty())
    {
        return candidates;
    }
    // The embedded table is English: an English tag ends the list (after a regional English table, if one ships),
    // so `language: "en"` or an English display language ahead of another one keeps the English strings.
    if (! preferredTag.empty())
    {
        if (AddTagFamily(candidates, directory, preferredTag) && IsEmbeddedLanguage(preferredTag))
        {
            return candidates;
        }
    }
    for (const std::wstring& language : systemLanguages)
    {
        if (AddTagFamily(candidates, directory, language) && IsEmbeddedLanguage(language))
        {
            break;
        }
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
        if (const HMODULE module = LoadLibraryExW(path.c_str(), nullptr, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE))
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
