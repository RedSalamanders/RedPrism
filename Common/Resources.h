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

// A menu caption without its "\t" accelerator column: what the menu row shows before the chord ("&Save As...").
[[nodiscard]] inline std::wstring_view CaptionWithoutChord(std::wstring_view caption) noexcept
{
    return caption.substr(0, caption.find(L'\t'));
}

// A menu caption as a plain command name (the Command List, notices): no accelerator column, no mnemonic marker
// ("&&" stays one "&"), no trailing ellipsis ("..." or "…") or space. "Save &As...\tCtrl+Shift+S" is "Save As".
[[nodiscard]] inline std::wstring PlainCommandName(std::wstring_view caption)
{
    caption = CaptionWithoutChord(caption);
    std::wstring name;
    name.reserve(caption.size());
    for (std::size_t index = 0; index < caption.size(); ++index)
    {
        if (caption[index] == L'&')
        {
            if (index + 1 < caption.size() && caption[index + 1] == L'&')
            {
                name.push_back(L'&');
                ++index;
            }
            continue;
        }
        name.push_back(caption[index]);
    }
    while (!name.empty() && (name.back() == L'.' || name.back() == L'…' || name.back() == L' '))
    {
        name.pop_back();
    }
    return name;
}
