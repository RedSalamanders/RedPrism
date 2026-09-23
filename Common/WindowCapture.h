#pragma once

#include <windows.h>

namespace RedPrism
{
[[nodiscard]] HRESULT SaveWindowScreenshot(HWND window, const wchar_t* pngPath,
                                           const RECT* clientCrop = nullptr) noexcept;
} // namespace RedPrism
