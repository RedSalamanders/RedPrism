#pragma once

#include <windows.h>

#include <cstddef>
#include <new>
#include <string>
#include <string_view>

namespace Win32FilePath
{
enum class Use
{
    File,
    Directory,
};

[[nodiscard]] inline bool IsVerbatim(std::wstring_view path) noexcept
{
    return path.starts_with(L"\\\\?\\") || path.starts_with(L"\\\\.\\");
}

[[nodiscard]] inline bool IsDosAbsolute(std::wstring_view path) noexcept
{
    return path.size() >= 3 && ((path[0] >= L'A' && path[0] <= L'Z') || (path[0] >= L'a' && path[0] <= L'z')) &&
           path[1] == L':' && path[2] == L'\\';
}

[[nodiscard]] inline bool IsUncAbsolute(std::wstring_view path) noexcept
{
    if (!path.starts_with(L"\\\\"))
        return false;
    const std::size_t serverEnd = path.find(L'\\', 2);
    if (serverEnd == std::wstring_view::npos || serverEnd == 2)
        return false;
    const std::size_t shareStart = serverEnd + 1;
    const std::size_t shareEnd = path.find(L'\\', shareStart);
    const std::size_t shareLimit = shareEnd == std::wstring_view::npos ? path.size() : shareEnd;
    return shareLimit > shareStart;
}

[[nodiscard]] inline bool HasOrdinaryParsingComponents(std::wstring_view path) noexcept
{
    if (path.find(L'/') != std::wstring_view::npos)
        return true;
    std::size_t start = IsDosAbsolute(path)         ? std::size_t{3}
                        : path.starts_with(L"\\\\") ? std::size_t{2}
                                                    : std::size_t{0};
    if (path.starts_with(L"\\\\") && IsUncAbsolute(path))
    {
        const std::size_t serverEnd = path.find(L'\\', 2);
        const std::size_t shareEnd = path.find(L'\\', serverEnd + 1);
        start = shareEnd == std::wstring_view::npos ? path.size() : shareEnd + 1;
    }
    while (start < path.size())
    {
        const std::size_t end = path.find(L'\\', start);
        const std::size_t limit = end == std::wstring_view::npos ? path.size() : end;
        const std::wstring_view component = path.substr(start, limit - start);
        if (component == L"." || component == L".." ||
            (!component.empty() && (component.back() == L'.' || component.back() == L' ')))
            return true;
        if (end == std::wstring_view::npos)
            break;
        start = end + 1;
    }
    return false;
}

[[nodiscard]] inline std::size_t Threshold(Use use) noexcept
{
    return use == Use::Directory ? static_cast<std::size_t>(MAX_PATH - 12) : static_cast<std::size_t>(MAX_PATH);
}

// The input must already be absolute and resolved. Ordinary slash and dot components are refused rather than
// silently acquiring verbatim-path meaning. Existing verbatim/device names are copied exactly, without trimming.
[[nodiscard]] inline HRESULT PrefixResolvedPath(const wchar_t* path, Use use, std::wstring& output) noexcept
{
    if (path == nullptr)
        return E_INVALIDARG;
    const std::wstring_view input{path};
    try
    {
        if (IsVerbatim(input))
        {
            output.assign(input);
            return S_FALSE;
        }
        if (input.size() < Threshold(use))
        {
            output.assign(input);
            return S_FALSE;
        }
        if (HasOrdinaryParsingComponents(input))
            return HRESULT_FROM_WIN32(ERROR_INVALID_NAME);

        std::wstring_view prefix;
        std::wstring_view suffix;
        if (IsDosAbsolute(input))
        {
            prefix = L"\\\\?\\";
            suffix = input;
        }
        else if (IsUncAbsolute(input))
        {
            prefix = L"\\\\?\\UNC\\";
            suffix = input.substr(2);
        }
        else
        {
            return HRESULT_FROM_WIN32(ERROR_BAD_PATHNAME);
        }
        if (suffix.size() > output.max_size() - prefix.size())
            return HRESULT_FROM_WIN32(ERROR_FILENAME_EXCED_RANGE);
        // path may point into output: retain that spelling until its complete prefixed copy exists.
        std::wstring prepared;
        prepared.reserve(prefix.size() + suffix.size());
        prepared.append(prefix);
        prepared.append(suffix);
        output.swap(prepared);
        return S_OK;
    }
    catch (const std::bad_alloc&)
    {
        return E_OUTOFMEMORY;
    }
}

// Keep the familiar destination suffix for ordinary leaves. For a near-limit destination component,
// choose a short GUID-bearing sibling leaf in the same directory instead of extending the destination leaf.
// The caller supplies the unique suffix and exclusively creates this exact path; no sibling enumeration occurs.
[[nodiscard]] inline HRESULT SiblingWithSuffix(std::wstring_view destination, std::wstring_view suffix,
                                               std::wstring& output) noexcept
{
    constexpr std::size_t maximumComponent = 255;
    constexpr std::wstring_view shortStem = L"RedPrism";
    const auto separator = destination.find_last_of(L"\\/");
    const auto leafStart = separator == std::wstring_view::npos ? 0 : separator + 1;
    const auto leafLength = destination.size() - leafStart;
    if (!leafLength || leafLength > maximumComponent || suffix.empty() ||
        suffix.size() > maximumComponent - shortStem.size() || suffix.find_first_of(L"\\/") != std::wstring_view::npos)
        return E_INVALIDARG;
    try
    {
        std::wstring prepared;
        if (leafLength <= maximumComponent - suffix.size())
            prepared.assign(destination);
        else
        {
            prepared.assign(destination.substr(0, leafStart));
            prepared.append(shortStem);
        }
        prepared.append(suffix);
        output.swap(prepared); // Input views may alias output; replace only after copying both.
        return S_OK;
    }
    catch (const std::bad_alloc&)
    {
        return E_OUTOFMEMORY;
    }
}

// Resolve an ordinary path once with GetFullPathNameW, then add a verbatim prefix only to the resolved absolute
// spelling. If the caller supplied an already verbatim/device path, preserve every character as-is.
[[nodiscard]] inline HRESULT Resolve(const wchar_t* path, Use use, std::wstring& output) noexcept
{
    if (path == nullptr)
        return E_INVALIDARG;
    if (IsVerbatim(path))
        return PrefixResolvedPath(path, use, output);
    const DWORD required = GetFullPathNameW(path, 0, nullptr, nullptr);
    if (required == 0)
        return HRESULT_FROM_WIN32(GetLastError());
    try
    {
        std::wstring resolved(static_cast<std::size_t>(required), L'\0');
        const DWORD written = GetFullPathNameW(path, required, resolved.data(), nullptr);
        if (written == 0)
            return HRESULT_FROM_WIN32(GetLastError());
        if (written >= required)
            return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
        resolved.resize(written);
        for (wchar_t& character : resolved)
        {
            if (character == L'/')
                character = L'\\';
        }
        return PrefixResolvedPath(resolved.c_str(), use, output);
    }
    catch (const std::bad_alloc&)
    {
        return E_OUTOFMEMORY;
    }
}

// Short ordinary spellings remain untouched. Long already-canonical absolute paths can be prefixed directly;
// relative, slash-containing, dot-segment, and ambiguous trailing-dot/space paths are resolved first.
[[nodiscard]] inline HRESULT Prepare(const wchar_t* path, Use use, std::wstring& output) noexcept
{
    if (path == nullptr)
        return E_INVALIDARG;
    const std::wstring_view input{path};
    if (IsVerbatim(input))
        return PrefixResolvedPath(path, use, output);
    if (input.size() < Threshold(use))
    {
        try
        {
            output.assign(input);
        }
        catch (const std::bad_alloc&)
        {
            return E_OUTOFMEMORY;
        }
        return S_FALSE;
    }
    if ((IsDosAbsolute(input) || IsUncAbsolute(input)) && !HasOrdinaryParsingComponents(input))
        return PrefixResolvedPath(path, use, output);
    return Resolve(path, use, output);
}
} // namespace Win32FilePath
