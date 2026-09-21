#pragma once

#include "Document.h"

#include <Windows.h>

#include <cstdint>

// The Win32 clipboard for pixels (C9b, Specs/Document/Document_Tools.md): a copy goes out as PNG (alpha kept) and a
// 32-bit DIB with an alpha channel for apps that read only bitmaps; a paste prefers the PNG another app put there
// and falls back to its DIB. The session keeps its own copy with the pixels' place; the sequence number tells the
// host whether the system clipboard still holds that copy.
namespace SystemClipboard
{
// Writes `image` (premultiplied BGRA) as PNG and CF_DIBV5; false when the clipboard could not be opened or filled.
[[nodiscard]] bool WriteImage(HWND owner, const Raster& image) noexcept;
// True when the clipboard holds a PNG or a DIB.
[[nodiscard]] bool HasImage() noexcept;
// Decodes the clipboard's PNG or DIB into a premultiplied BGRA raster within `pixelBudget`; false when there is no
// image, it is too large, or it cannot be read.
[[nodiscard]] bool ReadImage(HWND owner, std::int64_t pixelBudget, Raster& image) noexcept;
// The system's clipboard sequence number (changes on every write by any app).
[[nodiscard]] DWORD SequenceNumber() noexcept;
} // namespace SystemClipboard
