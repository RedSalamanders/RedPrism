#pragma once

#include "ContinuationRequests.h"
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
// File › Open: one project or image.
[[nodiscard]] HRESULT PickOpenRequest(HWND owner, HostInputBudget& budget,
                                      std::optional<HostRequest>& request) noexcept;
// File › Import Images…: any number of images, in the order the dialog lists them.
[[nodiscard]] HRESULT PickImageRequest(HWND owner, HostInputBudget& budget,
                                       std::optional<HostRequest>& request) noexcept;
[[nodiscard]] HRESULT PickOutputForRequest(HWND owner, const wchar_t* defaultName, const wchar_t* defaultExt,
                                           const COMDLG_FILTERSPEC* filters, UINT filterCount, HostRequest& request,
                                           HostInputBudget& budget) noexcept;
// The message for a failed project load.
[[nodiscard]] UINT LoadErrorStringId(ProjectIoStatus status) noexcept;
