#pragma once

#include <format>
#include <string>
#include <string_view>
#include <utility>
#include <windows.h>

// STRINGTABLE access. Strings use positional std::format placeholders ({0}, {1:08X}) so translators can reorder
// arguments; a resource string is never a printf format. `module` is the module that owns the table
// (RedPrism.exe: nullptr; tests that load the exe as a data file pass that handle).

// A satellite string table (Common/Localization.h) answers nullptr-module loads first; an id it lacks falls back
// to the executable's own table. An explicit module is read as given.
inline HMODULE g_stringResourceSatellite = nullptr;

inline void SetStringResourceSatellite(HMODULE module) noexcept
{
    g_stringResourceSatellite = module;
}

[[nodiscard]] inline std::wstring LoadStringResource(HMODULE module, UINT id)
{
    const wchar_t* text = nullptr;
    int length = 0;
    if (module == nullptr && g_stringResourceSatellite != nullptr)
    {
        length = LoadStringW(g_stringResourceSatellite, id, reinterpret_cast<wchar_t*>(&text), 0);
    }
    if (length <= 0 || text == nullptr)
    {
        text = nullptr;
        length = LoadStringW(module, id, reinterpret_cast<wchar_t*>(&text), 0);
    }
    if (length <= 0 || text == nullptr)
    {
        return {};
    }
    return std::wstring(text, static_cast<std::size_t>(length));
}

template <typename... Args> [[nodiscard]] std::wstring FormatStringResource(HMODULE module, UINT id, Args&&... args)
{
    const std::wstring pattern = LoadStringResource(module, id);
    if (pattern.empty())
    {
        return {};
    }
    // A malformed placeholder is a resource-authoring defect. It must not take the process down from a
    // chrome path, so the raw pattern is shown instead of the formatted text.
    try
    {
        return std::vformat(std::wstring_view{pattern}, std::make_wformat_args(args...));
    }
    catch (const std::format_error&)
    {
        return pattern;
    }
}

// Menu captions keep a "\t" accelerator column; the caption without it names the command elsewhere.
[[nodiscard]] inline std::wstring StripAcceleratorColumn(std::wstring caption)
{
    const std::size_t tab = caption.find(L'\t');
    if (tab != std::wstring::npos)
    {
        caption.resize(tab);
    }
    std::wstring result;
    result.reserve(caption.size());
    for (const wchar_t ch : caption)
    {
        if (ch != L'&')
        {
            result.push_back(ch);
        }
    }
    return result;
}
