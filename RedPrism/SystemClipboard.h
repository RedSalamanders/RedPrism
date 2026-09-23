#pragma once

#include "Document.h"

#include <Windows.h>

#include <cstdint>
#include <utility>
#include <vector>

// The Win32 clipboard for pixels (C9b, Specs/Document/Document_Tools.md): a copy goes out as PNG (alpha kept) and a
// 32-bit DIB with an alpha channel for apps that read only bitmaps; a paste prefers the PNG another app put there
// and falls back to its DIB. The session keeps its own copy with the pixels' place; the sequence number tells the
// host whether the system clipboard changed since that copy.
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

// What the clipboard held, for a run that borrows it and gives it back (the self-test): every memory format an app
// placed. The formats Windows synthesizes from them (a DIB from a DIBV5, ANSI from Unicode text) are not asked for,
// since asking converts and a restore synthesizes them again; a bitmap, metafile or palette placed as a GDI object,
// and private or display-only formats, do not come back.
struct SavedContents
{
    std::vector<std::pair<UINT, std::vector<std::uint8_t>>> formats;
};
// False when the clipboard could not be opened or a copy could not be allocated.
[[nodiscard]] bool SaveContents(HWND owner, SavedContents& contents) noexcept;
// Replaces the clipboard with `contents` (empty contents leave it empty).
[[nodiscard]] bool RestoreContents(HWND owner, const SavedContents& contents) noexcept;
[[nodiscard]] bool Clear(HWND owner) noexcept;
} // namespace SystemClipboard
