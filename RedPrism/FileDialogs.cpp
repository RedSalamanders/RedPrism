#include "FileDialogs.h"

#include "Resources.h"
#include "resource.h"

#include <iterator>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/com.h>
#pragma warning(pop)

namespace
{
constexpr wchar_t kImagePatterns[] = L"*.png;*.jpg;*.jpeg;*.tif;*.tiff;*.heic;*.heif";

[[nodiscard]] bool ReadDialogResult(IFileDialog* dialog, std::wstring& path) noexcept
{
    wil::com_ptr_nothrow<IShellItem> item;
    if (FAILED(dialog->GetResult(item.put())) || !item)
    {
        return false;
    }
    wil::unique_cotaskmem_string file;
    if (FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, file.put())))
    {
        return false;
    }
    path = file.get();
    return !path.empty();
}
} // namespace

FilterStrings LoadFilterStrings()
{
    return FilterStrings{
        LoadStringResource(nullptr, IDS_FILTER_PROJECT), LoadStringResource(nullptr, IDS_FILTER_IMAGES),
        LoadStringResource(nullptr, IDS_FILTER_ALL),     LoadStringResource(nullptr, IDS_FILTER_EXPLODED),
        LoadStringResource(nullptr, IDS_FILTER_JPEG),    LoadStringResource(nullptr, IDS_FILTER_PNG)};
}

std::wstring FileTitle(const std::wstring& path)
{
    if (path.empty())
    {
        return LoadStringResource(nullptr, IDS_UNTITLED);
    }
    const size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? path : path.substr(slash + 1);
}

bool EndsWithNoCase(std::wstring_view text, std::wstring_view suffix) noexcept
{
    if (text.size() < suffix.size())
    {
        return false;
    }
    const std::wstring_view tail = text.substr(text.size() - suffix.size());
    return CompareStringOrdinal(tail.data(), static_cast<int>(tail.size()), suffix.data(),
                                static_cast<int>(suffix.size()), TRUE) == CSTR_EQUAL;
}

bool LooksLikeProjectPath(const wchar_t* path) noexcept
{
    const DWORD attributes = GetFileAttributesW(path);
    const bool directory = attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    return directory || EndsWithNoCase(path, L".prism") || PathLooksLikeZipProject(path);
}

bool PickOpenPath(HWND owner, std::wstring& path) noexcept
{
    wil::com_ptr_nothrow<IFileOpenDialog> dialog;
    HRESULT result = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(dialog.put()));
    if (FAILED(result) || !dialog)
    {
        return false;
    }
    const FilterStrings strings = LoadFilterStrings();
    const COMDLG_FILTERSPEC filters[] = {
        {strings.project.c_str(), L"*.prism"},
        {strings.images.c_str(), kImagePatterns},
        {strings.all.c_str(), L"*.*"},
    };
    (void)dialog->SetFileTypes(static_cast<UINT>(std::size(filters)), filters);
    FILEOPENDIALOGOPTIONS options = 0;
    (void)dialog->GetOptions(&options);
    (void)dialog->SetOptions(options | FOS_FILEMUSTEXIST | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
    result = dialog->Show(owner);
    if (FAILED(result))
    {
        return false;
    }
    return ReadDialogResult(dialog.get(), path);
}

bool PickImagePaths(HWND owner, std::vector<std::wstring>& paths) noexcept
{
    paths.clear();
    wil::com_ptr_nothrow<IFileOpenDialog> dialog;
    HRESULT result = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(dialog.put()));
    if (FAILED(result) || !dialog)
    {
        return false;
    }
    const FilterStrings strings = LoadFilterStrings();
    const COMDLG_FILTERSPEC filters[] = {
        {strings.images.c_str(), kImagePatterns},
        {strings.all.c_str(), L"*.*"},
    };
    (void)dialog->SetFileTypes(static_cast<UINT>(std::size(filters)), filters);
    FILEOPENDIALOGOPTIONS options = 0;
    (void)dialog->GetOptions(&options);
    (void)dialog->SetOptions(options | FOS_ALLOWMULTISELECT | FOS_FILEMUSTEXIST | FOS_FORCEFILESYSTEM |
                             FOS_PATHMUSTEXIST);
    result = dialog->Show(owner);
    if (FAILED(result))
    {
        return false;
    }
    wil::com_ptr_nothrow<IShellItemArray> items;
    if (FAILED(dialog->GetResults(items.put())) || !items)
    {
        return false;
    }
    DWORD count = 0;
    if (FAILED(items->GetCount(&count)))
    {
        return false;
    }
    for (DWORD index = 0; index < count; ++index)
    {
        wil::com_ptr_nothrow<IShellItem> item;
        if (FAILED(items->GetItemAt(index, item.put())) || !item)
        {
            continue;
        }
        wil::unique_cotaskmem_string file;
        if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, file.put())) && file && file.get()[0] != L'\0')
        {
            paths.emplace_back(file.get());
        }
    }
    return !paths.empty();
}

bool PickSavePath(HWND owner, const wchar_t* defaultName, const wchar_t* defaultExt, const COMDLG_FILTERSPEC* filters,
                  UINT filterCount, std::wstring& path) noexcept
{
    wil::com_ptr_nothrow<IFileSaveDialog> dialog;
    HRESULT result = CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(dialog.put()));
    if (FAILED(result) || !dialog)
    {
        return false;
    }
    if (filters && filterCount != 0)
    {
        (void)dialog->SetFileTypes(filterCount, filters);
    }
    if (defaultExt)
    {
        (void)dialog->SetDefaultExtension(defaultExt);
    }
    if (defaultName)
    {
        (void)dialog->SetFileName(defaultName);
    }
    FILEOPENDIALOGOPTIONS options = 0;
    (void)dialog->GetOptions(&options);
    (void)dialog->SetOptions(options | FOS_OVERWRITEPROMPT | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
    result = dialog->Show(owner);
    if (FAILED(result))
    {
        return false;
    }
    return ReadDialogResult(dialog.get(), path);
}

UINT LoadErrorStringId(ProjectIoStatus status) noexcept
{
    switch (status)
    {
    case ProjectIoStatus::UnsupportedVersion:
        return IDS_ERROR_UNSUPPORTED_VERSION;
    case ProjectIoStatus::TooLarge:
        return IDS_ERROR_TOO_LARGE;
    case ProjectIoStatus::MissingImage:
        return IDS_ERROR_MISSING_IMAGE;
    case ProjectIoStatus::Ok:
    case ProjectIoStatus::Invalid:
    case ProjectIoStatus::Encode:
    default:
        return IDS_ERROR_OPEN_PROJECT;
    }
}
