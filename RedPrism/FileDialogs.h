#pragma once

#include "ProjectStore.h"

#include <shobjidl.h>
#include <string>
#include <string_view>
#include <vector>
#include <windows.h>

// The common file dialogs and path helpers the Application's file commands share (Application.cpp and
// ApplicationFiles.cpp; Specs/UI/UI_ApplicationChrome.md "Files").

struct FilterStrings
{
    std::wstring project;
    std::wstring images;
    std::wstring all;
    std::wstring exploded;
    std::wstring jpeg;
    std::wstring png;
};

[[nodiscard]] FilterStrings LoadFilterStrings();
// The file name of `path`, or the localized Untitled for an empty path.
[[nodiscard]] std::wstring FileTitle(const std::wstring& path);
[[nodiscard]] bool EndsWithNoCase(std::wstring_view text, std::wstring_view suffix) noexcept;
// A directory (an exploded project), a `.prism` name or a ZIP signature opens as a project, anything else as an image.
[[nodiscard]] bool LooksLikeProjectPath(const wchar_t* path) noexcept;
// File › Open: one project or image.
[[nodiscard]] bool PickOpenPath(HWND owner, std::wstring& path) noexcept;
// File › Import Images…: any number of images, in the order the dialog lists them.
[[nodiscard]] bool PickImagePaths(HWND owner, std::vector<std::wstring>& paths) noexcept;
[[nodiscard]] bool PickSavePath(HWND owner, const wchar_t* defaultName, const wchar_t* defaultExt,
                                const COMDLG_FILTERSPEC* filters, UINT filterCount, std::wstring& path) noexcept;
// The message for a failed project load.
[[nodiscard]] UINT LoadErrorStringId(ProjectIoStatus status) noexcept;
