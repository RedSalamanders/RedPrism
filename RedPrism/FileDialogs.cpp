#include "FileDialogs.h"

#include "Resources.h"
#include "resource.h"

#include <array>
#include <iterator>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/com.h>
#pragma warning(pop)

namespace
{
constexpr wchar_t kImagePatterns[] = L"*.png;*.jpg;*.jpeg;*.tif;*.tiff;*.heic;*.heif";

constexpr HRESULT kInvalidDialogInput = HRESULT_FROM_WIN32(ERROR_NOT_ENOUGH_QUOTA);
constexpr HRESULT kDialogCancelled = HRESULT_FROM_WIN32(ERROR_CANCELLED);

[[nodiscard]] HRESULT ReadShellPath(IShellItem* item, wil::unique_cotaskmem_string& path) noexcept
{
    if (!item)
    {
        return E_UNEXPECTED;
    }
    const HRESULT result = item->GetDisplayName(SIGDN_FILESYSPATH, path.put());
    if (FAILED(result))
    {
        return result;
    }
    return path ? S_OK : E_UNEXPECTED;
}

[[nodiscard]] HRESULT ReadDialogShellPath(IFileDialog* dialog, wil::unique_cotaskmem_string& path) noexcept
{
    wil::com_ptr_nothrow<IShellItem> item;
    const HRESULT result = dialog->GetResult(item.put());
    if (FAILED(result))
    {
        return result;
    }
    return ReadShellPath(item.get(), path);
}

[[nodiscard]] HRESULT ConfigureOpenDialog(IFileOpenDialog* dialog, const COMDLG_FILTERSPEC* filters, UINT filterCount,
                                          bool multiSelect) noexcept
{
    HRESULT result = dialog->SetFileTypes(filterCount, filters);
    if (FAILED(result))
    {
        return result;
    }
    FILEOPENDIALOGOPTIONS options = 0;
    result = dialog->GetOptions(&options);
    if (FAILED(result))
    {
        return result;
    }
    options |= FOS_FILEMUSTEXIST | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST;
    if (multiSelect)
    {
        options |= FOS_ALLOWMULTISELECT;
    }
    return dialog->SetOptions(options);
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

HRESULT PickOpenRequest(HWND owner, HostInputBudget& budget, std::optional<HostRequest>& request) noexcept
{
    try
    {
        wil::com_ptr_nothrow<IFileOpenDialog> dialog;
        HRESULT result =
            CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(dialog.put()));
        if (FAILED(result))
        {
            return result;
        }
        if (!dialog)
        {
            return E_UNEXPECTED;
        }
        const FilterStrings strings = LoadFilterStrings();
        const COMDLG_FILTERSPEC filters[] = {
            {strings.project.c_str(), L"*.prism"},
            {strings.images.c_str(), kImagePatterns},
            {strings.all.c_str(), L"*.*"},
        };
        result = ConfigureOpenDialog(dialog.get(), filters, static_cast<UINT>(std::size(filters)), false);
        if (FAILED(result))
        {
            return result;
        }
        result = dialog->Show(owner);
        if (result == kDialogCancelled)
        {
            return kDialogCancelled;
        }
        if (FAILED(result))
        {
            return result;
        }

        std::array<wil::unique_cotaskmem_string, 1> shellPaths{};
        std::array<std::wstring_view, 1> pathViews{};
        result = ReadDialogShellPath(dialog.get(), shellPaths[0]);
        if (FAILED(result))
        {
            return result;
        }
        const auto path = HostBoundedPathView(shellPaths[0].get());
        if (!path || path->empty())
        {
            return kInvalidDialogInput;
        }
        pathViews[0] = path.value();
        auto admitted = HostRequest::TryCopyPaths(std::span<const std::wstring_view>(pathViews.data(), 1),
                                                  HostPathAction::OpenPaths, std::nullopt, budget);
        if (!admitted)
        {
            return kInvalidDialogInput;
        }
        request = std::move(admitted);
        return S_OK;
    }
    catch (const std::bad_alloc&)
    {
        return E_OUTOFMEMORY;
    }
}

HRESULT PickImageRequest(HWND owner, HostInputBudget& budget, std::optional<HostRequest>& request) noexcept
{
    try
    {
        wil::com_ptr_nothrow<IFileOpenDialog> dialog;
        HRESULT result =
            CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(dialog.put()));
        if (FAILED(result))
        {
            return result;
        }
        if (!dialog)
        {
            return E_UNEXPECTED;
        }
        const FilterStrings strings = LoadFilterStrings();
        const COMDLG_FILTERSPEC filters[] = {
            {strings.images.c_str(), kImagePatterns},
            {strings.all.c_str(), L"*.*"},
        };
        result = ConfigureOpenDialog(dialog.get(), filters, static_cast<UINT>(std::size(filters)), true);
        if (FAILED(result))
        {
            return result;
        }
        result = dialog->Show(owner);
        if (result == kDialogCancelled)
        {
            return kDialogCancelled;
        }
        if (FAILED(result))
        {
            return result;
        }

        wil::com_ptr_nothrow<IShellItemArray> items;
        result = dialog->GetResults(items.put());
        if (FAILED(result))
        {
            return result;
        }
        if (!items)
        {
            return E_UNEXPECTED;
        }
        DWORD count = 0;
        result = items->GetCount(&count);
        if (FAILED(result))
        {
            return result;
        }
        if (count == 0 || count > HostInputBudget::kMaxPaths)
        {
            return kInvalidDialogInput;
        }

        std::array<wil::unique_cotaskmem_string, HostInputBudget::kMaxPaths> shellPaths{};
        std::array<std::wstring_view, HostInputBudget::kMaxPaths> pathViews{};
        for (DWORD index = 0; index < count; ++index)
        {
            wil::com_ptr_nothrow<IShellItem> item;
            result = items->GetItemAt(index, item.put());
            if (FAILED(result))
            {
                return result;
            }
            if (!item)
            {
                return E_UNEXPECTED;
            }
            result = ReadShellPath(item.get(), shellPaths[index]);
            if (FAILED(result))
            {
                return result;
            }
            const auto path = HostBoundedPathView(shellPaths[index].get());
            if (!path || path->empty())
            {
                return kInvalidDialogInput;
            }
            pathViews[index] = path.value();
        }

        auto admitted = HostRequest::TryCopyPaths(
            std::span<const std::wstring_view>(pathViews.data(), static_cast<std::size_t>(count)),
            HostPathAction::ImportImages, std::nullopt, budget);
        if (!admitted)
        {
            return kInvalidDialogInput;
        }
        request = std::move(admitted);
        return S_OK;
    }
    catch (const std::bad_alloc&)
    {
        return E_OUTOFMEMORY;
    }
}

HRESULT PickOutputForRequest(HWND owner, const wchar_t* defaultName, const wchar_t* defaultExt,
                             const COMDLG_FILTERSPEC* filters, UINT filterCount, HostRequest& request,
                             HostInputBudget& budget) noexcept
{
    try
    {
        wil::com_ptr_nothrow<IFileSaveDialog> dialog;
        HRESULT result =
            CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(dialog.put()));
        if (FAILED(result))
        {
            return result;
        }
        if (!dialog)
        {
            return E_UNEXPECTED;
        }
        if (filters && filterCount != 0)
        {
            result = dialog->SetFileTypes(filterCount, filters);
            if (FAILED(result))
            {
                return result;
            }
        }
        if (defaultExt)
        {
            result = dialog->SetDefaultExtension(defaultExt);
            if (FAILED(result))
            {
                return result;
            }
        }
        if (defaultName)
        {
            result = dialog->SetFileName(defaultName);
            if (FAILED(result))
            {
                return result;
            }
        }
        FILEOPENDIALOGOPTIONS options = 0;
        result = dialog->GetOptions(&options);
        if (FAILED(result))
        {
            return result;
        }
        result = dialog->SetOptions(options | FOS_OVERWRITEPROMPT | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
        if (FAILED(result))
        {
            return result;
        }
        result = dialog->Show(owner);
        if (result == kDialogCancelled)
        {
            return kDialogCancelled;
        }
        if (FAILED(result))
        {
            return result;
        }

        wil::unique_cotaskmem_string shellPath;
        result = ReadDialogShellPath(dialog.get(), shellPath);
        if (FAILED(result))
        {
            return result;
        }
        const auto path = HostBoundedPathView(shellPath.get());
        if (!path || path->empty())
        {
            return kInvalidDialogInput;
        }
        return request.TrySelectOutput(path.value(), budget) ? S_OK : kInvalidDialogInput;
    }
    catch (const std::bad_alloc&)
    {
        return E_OUTOFMEMORY;
    }
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
