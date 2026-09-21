#pragma once

#include <format>
#include <string>
#include <string_view>
#include <utility>
#include <windows.h>

// STRINGTABLE access. Strings use positional std::format placeholders ({0}, {1:08X}) so translators can reorder
// arguments; a resource string is never a printf format. `module` is the module that owns the table
// (RedPrism.exe: nullptr; tests that load the exe as a data file pass that handle).

[[nodiscard]] inline std::wstring LoadStringResource(HMODULE module, UINT id)
{
    const wchar_t* text = nullptr;
    const int length = LoadStringW(module, id, reinterpret_cast<wchar_t*>(&text), 0);
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
