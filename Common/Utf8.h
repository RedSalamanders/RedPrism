#pragma once

#include <string>
#include <string_view>
#include <windows.h>

[[nodiscard]] inline std::string WideToUtf8(std::wstring_view text)
{
    if (text.empty())
    {
        return {};
    }
    const int bytes =
        WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (bytes <= 0)
    {
        return {};
    }
    std::string utf8(static_cast<std::size_t>(bytes), '\0');
    (void)WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), utf8.data(), bytes, nullptr,
                              nullptr);
    return utf8;
}

[[nodiscard]] inline std::wstring Utf8ToWide(std::string_view text)
{
    if (text.empty())
    {
        return {};
    }
    const int chars = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (chars <= 0)
    {
        return {};
    }
    std::wstring wide(static_cast<std::size_t>(chars), L'\0');
    (void)MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), chars);
    return wide;
}
