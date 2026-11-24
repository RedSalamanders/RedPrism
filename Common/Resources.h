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

[[nodiscard]] inline std::wstring LoadEmbeddedStringResource(HMODULE module, UINT id)
{
    const wchar_t* text = nullptr;
    const int length    = LoadStringW(module, id, reinterpret_cast<wchar_t*>(&text), 0);
    if (length <= 0 || text == nullptr)
    {
        return {};
    }
    return std::wstring(text, static_cast<std::size_t>(length));
}

[[nodiscard]] inline std::wstring LoadStringResource(HMODULE module, UINT id)
{
    if (module == nullptr && g_stringResourceSatellite != nullptr)
    {
        auto translated = LoadEmbeddedStringResource(g_stringResourceSatellite, id);
        if (! translated.empty())
        {
            return translated;
        }
    }
    return LoadEmbeddedStringResource(module, id);
}

namespace ResourceDetail
{
template <typename... Args> [[nodiscard]] std::wstring Format(std::wstring pattern, Args&&... args)
{
    if (pattern.empty())
    {
        return {};
    }
    // Keep the raw pattern when a resource's placeholders do not match the call.
    try
    {
        return std::vformat(std::wstring_view{pattern}, std::make_wformat_args(args...));
    }
    catch (const std::format_error&)
    {
        return pattern;
    }
}
} // namespace ResourceDetail

template <typename... Args> [[nodiscard]] std::wstring FormatStringResource(HMODULE module, UINT id, Args&&... args)
{
    return ResourceDetail::Format(LoadStringResource(module, id), std::forward<Args>(args)...);
}

template <typename... Args> [[nodiscard]] std::wstring FormatEmbeddedStringResource(HMODULE module, UINT id, Args&&... args)
{
    return ResourceDetail::Format(LoadEmbeddedStringResource(module, id), std::forward<Args>(args)...);
}

inline int MessageBoxResource(HWND owner, HMODULE module, UINT textId, UINT captionId, UINT type)
{
    const auto text    = LoadStringResource(module, textId);
    const auto caption = LoadStringResource(module, captionId);
    return MessageBoxW(owner, text.c_str(), caption.c_str(), type);
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
    while (! name.empty() && (name.back() == L'.' || name.back() == L'…' || name.back() == L' '))
    {
        name.pop_back();
    }
    return name;
}
