#include "PerfRoot.h"

#include <array>
#include <new>
#include <string_view>

#include <windows.h>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/resource.h>
#pragma warning(pop)

namespace
{
constexpr wchar_t kPerfRootName[] = L"RedPrism.Perf";
constexpr wchar_t kMarkerName[] = L".redprism-perf-owner";
// Changing the ownership marker requires an intentional format-version change here.
constexpr char kMarkerContent[] = "RedPrism performance root owner v1\n";
constexpr std::size_t kMarkerContentBytes = sizeof(kMarkerContent) - 1;
constexpr std::size_t kMarkerReadLimit = kMarkerContentBytes + 1;

[[nodiscard]] HRESULT Win32Error(DWORD error) noexcept
{
    return HRESULT_FROM_WIN32(error == ERROR_SUCCESS ? ERROR_GEN_FAILURE : error);
}

[[nodiscard]] bool IsAsciiDriveLetter(wchar_t value) noexcept
{
    return (value >= L'A' && value <= L'Z') || (value >= L'a' && value <= L'z');
}

[[nodiscard]] wchar_t UpperDriveLetter(wchar_t value) noexcept
{
    return value >= L'a' && value <= L'z' ? static_cast<wchar_t>(value - (L'a' - L'A')) : value;
}

[[nodiscard]] HRESULT OpenDirectoryNoFollow(std::wstring_view path, wil::unique_hfile& directory)
{
    std::wstring terminated{path};
    directory.reset(CreateFileW(terminated.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    if (!directory)
    {
        return Win32Error(GetLastError());
    }

    BY_HANDLE_FILE_INFORMATION information{};
    if (!GetFileInformationByHandle(directory.get(), &information))
    {
        return Win32Error(GetLastError());
    }
    if ((information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0 ||
        (information.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
    {
        return HRESULT_FROM_WIN32(ERROR_CANT_ACCESS_FILE);
    }
    return S_OK;
}

[[nodiscard]] HRESULT ValidateOwnedRoot(std::wstring_view path)
{
    if (!IsValidPerfRootPath(path))
    {
        return E_INVALIDARG;
    }

    wil::unique_hfile directory;
    HRESULT status = OpenDirectoryNoFollow(path, directory);
    if (FAILED(status))
    {
        return status;
    }

    std::wstring markerPath{path};
    markerPath.push_back(L'\\');
    markerPath.append(kMarkerName);
    wil::unique_hfile marker{CreateFileW(markerPath.c_str(), GENERIC_READ | FILE_READ_ATTRIBUTES, FILE_SHARE_READ,
                                         nullptr, OPEN_EXISTING,
                                         FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN, nullptr)};
    if (!marker)
    {
        return Win32Error(GetLastError());
    }

    BY_HANDLE_FILE_INFORMATION markerInformation{};
    if (!GetFileInformationByHandle(marker.get(), &markerInformation))
    {
        return Win32Error(GetLastError());
    }
    if ((markerInformation.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != 0 ||
        markerInformation.nFileSizeHigh != 0 || markerInformation.nFileSizeLow > kMarkerContentBytes)
    {
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }

    std::array<char, kMarkerReadLimit> content{};
    DWORD bytesRead = 0;
    if (!ReadFile(marker.get(), content.data(), static_cast<DWORD>(content.size()), &bytesRead, nullptr))
    {
        return Win32Error(GetLastError());
    }
    if (bytesRead != kMarkerContentBytes ||
        std::string_view{content.data(), bytesRead} != std::string_view{kMarkerContent, kMarkerContentBytes})
    {
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    return S_OK;
}

[[nodiscard]] HRESULT CreateOwnedRoot(std::wstring_view path)
{
    std::wstring terminated{path};
    if (!CreateDirectoryW(terminated.c_str(), nullptr))
    {
        const DWORD error = GetLastError();
        if (error != ERROR_ALREADY_EXISTS)
        {
            return Win32Error(error);
        }
        return ValidateOwnedRoot(path);
    }

    // Hold a no-follow handle that denies delete sharing before creating the marker. This pins the
    // newly created directory against replacement between the directory creation and marker write.
    wil::unique_hfile directory;
    const HRESULT directoryStatus = OpenDirectoryNoFollow(path, directory);
    if (FAILED(directoryStatus))
    {
        return directoryStatus;
    }

    std::wstring markerPath{path};
    markerPath.push_back(L'\\');
    markerPath.append(kMarkerName);
    wil::unique_hfile marker{
        CreateFileW(markerPath.c_str(), GENERIC_WRITE | FILE_READ_ATTRIBUTES, 0, nullptr, CREATE_NEW,
                    FILE_ATTRIBUTE_HIDDEN | FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_WRITE_THROUGH, nullptr)};
    if (!marker)
    {
        return Win32Error(GetLastError());
    }

    DWORD bytesWritten = 0;
    if (!WriteFile(marker.get(), kMarkerContent, static_cast<DWORD>(kMarkerContentBytes), &bytesWritten, nullptr))
    {
        return Win32Error(GetLastError());
    }
    if (bytesWritten != kMarkerContentBytes)
    {
        return HRESULT_FROM_WIN32(ERROR_WRITE_FAULT);
    }
    if (!FlushFileBuffers(marker.get()))
    {
        return Win32Error(GetLastError());
    }
    marker.reset();
    return ValidateOwnedRoot(path);
}

[[nodiscard]] std::wstring RootForDrive(wchar_t drive)
{
    std::wstring path;
    path.reserve(16);
    path.push_back(UpperDriveLetter(drive));
    path.append(L":\\");
    path.append(kPerfRootName);
    return path;
}

[[nodiscard]] HRESULT ResolveConfiguredRoot(std::wstring_view configured, std::wstring& result)
{
    if (!IsValidPerfRootPath(configured))
    {
        return E_INVALIDARG;
    }
    std::array<wchar_t, 4> drivePath{UpperDriveLetter(configured[0]), L':', L'\\', L'\0'};
    if (GetDriveTypeW(drivePath.data()) != DRIVE_FIXED)
    {
        return HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);
    }
    HRESULT status = ValidateOwnedRoot(configured);
    if (status == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND) || status == HRESULT_FROM_WIN32(ERROR_PATH_NOT_FOUND))
    {
        status = CreateOwnedRoot(configured);
    }
    if (SUCCEEDED(status))
    {
        result.assign(configured);
    }
    return status;
}

[[nodiscard]] bool IsExistingOwnedRoot(wchar_t drive, std::wstring& result)
{
    std::wstring path = RootForDrive(drive);
    if (SUCCEEDED(ValidateOwnedRoot(path)))
    {
        result.swap(path);
        return true;
    }
    return false;
}

[[nodiscard]] HRESULT ResolveDefaultRoot(std::wstring& result)
{
    const DWORD driveMask = GetLogicalDrives();
    if (driveMask == 0)
    {
        return Win32Error(GetLastError());
    }

    constexpr wchar_t kPreferredDrive = L'X';
    const auto tryDrive = [&](wchar_t drive)
    {
        const unsigned int bit = static_cast<unsigned int>(UpperDriveLetter(drive) - L'A');
        if ((driveMask & (1u << bit)) == 0)
        {
            return false;
        }
        std::array<wchar_t, 4> drivePath{drive, L':', L'\\', L'\0'};
        if (GetDriveTypeW(drivePath.data()) != DRIVE_FIXED)
        {
            return false;
        }
        return IsExistingOwnedRoot(drive, result);
    };

    if (tryDrive(kPreferredDrive))
    {
        return S_OK;
    }
    for (wchar_t drive = L'A'; drive <= L'Z'; ++drive)
    {
        if (drive != kPreferredDrive && tryDrive(drive))
        {
            return S_OK;
        }
    }

    std::array<wchar_t, MAX_PATH> windowsDirectory{};
    const UINT windowsDirectoryLength =
        GetWindowsDirectoryW(windowsDirectory.data(), static_cast<UINT>(windowsDirectory.size()));
    if (windowsDirectoryLength == 0 || windowsDirectoryLength >= windowsDirectory.size() ||
        !IsAsciiDriveLetter(windowsDirectory[0]) || windowsDirectory[1] != L':')
    {
        return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
    }

    const wchar_t systemDrive = UpperDriveLetter(windowsDirectory[0]);
    std::array<wchar_t, 4> systemDrivePath{systemDrive, L':', L'\\', L'\0'};
    if (GetDriveTypeW(systemDrivePath.data()) != DRIVE_FIXED)
    {
        return HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);
    }

    std::wstring path = RootForDrive(systemDrive);
    const HRESULT status = CreateOwnedRoot(path);
    if (SUCCEEDED(status))
    {
        result.swap(path);
    }
    return status;
}
} // namespace

bool IsValidPerfRootPath(std::wstring_view path) noexcept
{
    constexpr std::size_t kExpectedLength = 3 + (sizeof(kPerfRootName) / sizeof(kPerfRootName[0])) - 1;
    if (path.size() != kExpectedLength || !IsAsciiDriveLetter(path[0]) || path[1] != L':' || path[2] != L'\\')
    {
        return false;
    }

    constexpr std::wstring_view expected{kPerfRootName};
    for (std::size_t index = 0; index < expected.size(); ++index)
    {
        wchar_t value = path[index + 3];
        if (value >= L'A' && value <= L'Z')
        {
            value = static_cast<wchar_t>(value + (L'a' - L'A'));
        }
        wchar_t expectedValue = expected[index];
        if (expectedValue >= L'A' && expectedValue <= L'Z')
        {
            expectedValue = static_cast<wchar_t>(expectedValue + (L'a' - L'A'));
        }
        if (value != expectedValue)
        {
            return false;
        }
    }
    return true;
}

std::int32_t ResolvePerfRoot(std::wstring& root) noexcept
{
    try
    {
        std::array<wchar_t, 64> configured{};
        SetLastError(ERROR_SUCCESS);
        const DWORD configuredLength =
            GetEnvironmentVariableW(L"REDPRISM_PERF_ROOT", configured.data(), static_cast<DWORD>(configured.size()));
        if (configuredLength != 0)
        {
            if (configuredLength >= configured.size())
            {
                return static_cast<std::int32_t>(HRESULT_FROM_WIN32(ERROR_BUFFER_OVERFLOW));
            }
            std::wstring resolved;
            const HRESULT status = ResolveConfiguredRoot(
                std::wstring_view{configured.data(), static_cast<std::size_t>(configuredLength)}, resolved);
            if (SUCCEEDED(status))
            {
                root.swap(resolved);
            }
            return static_cast<std::int32_t>(status);
        }
        if (GetLastError() != ERROR_ENVVAR_NOT_FOUND)
        {
            return static_cast<std::int32_t>(HRESULT_FROM_WIN32(ERROR_INVALID_DATA));
        }

        std::wstring resolved;
        const HRESULT status = ResolveDefaultRoot(resolved);
        if (SUCCEEDED(status))
        {
            root.swap(resolved);
        }
        return static_cast<std::int32_t>(status);
    }
    catch (const std::bad_alloc&)
    {
        // Fail the complete resolution operation instead of hiding allocation failure.
        return static_cast<std::int32_t>(E_OUTOFMEMORY);
    }
}
