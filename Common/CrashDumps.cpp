#include "CrashDumps.h"

#include "SettingsStore.h"
#include "Utf8.h"

#include <algorithm>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <new>
#include <vector>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/resource.h>
#pragma warning(pop)

#include <dbghelp.h>

#pragma comment(lib, "dbghelp.lib")

namespace CrashDumps
{
namespace
{
constexpr DWORD kDumpType = MiniDumpWithDataSegs | MiniDumpWithHandleData | MiniDumpWithThreadInfo |
                            MiniDumpWithUnloadedModules | MiniDumpWithIndirectlyReferencedMemory;
constexpr DWORD kTerminateCode = 0xE0000001;
constexpr DWORD kInvalidParameterCode = 0xE0000002;
constexpr DWORD kPureCallCode = 0xE0000003;
constexpr DWORD kAbortCode = 0xE0000004;
constexpr DWORD kProbeCode = 0xE0000005;
constexpr wchar_t kPendingName[] = L"pending.txt";

wchar_t g_directory[kPathCapacity] = {};
volatile LONG g_active = 0;

// Creates every missing component of `directory`. A stack copy walks the separators; nothing allocates.
[[nodiscard]] bool CreateDirectoryTree(const wchar_t* directory) noexcept
{
    wchar_t path[kPathCapacity];
    if (wcscpy_s(path, directory) != 0 || wcslen(path) < 3)
    {
        return false;
    }
    for (wchar_t* cursor = path + 3; *cursor; ++cursor) // past the "C:\" drive root
    {
        if (*cursor == L'\\' || *cursor == L'/')
        {
            *cursor = 0;
            (void)CreateDirectoryW(path, nullptr);
            *cursor = L'\\';
        }
    }
    (void)CreateDirectoryW(path, nullptr);
    const DWORD attributes = GetFileAttributesW(path);
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

[[nodiscard]] bool JoinPath(const wchar_t* directory, const wchar_t* name, wchar_t* out, std::size_t capacity) noexcept
{
    return swprintf_s(out, capacity, L"%s\\%s", directory, name) > 0;
}

LONG WINAPI UnhandledFilter(EXCEPTION_POINTERS* pointers) noexcept
{
    // A second fault while the dump is written (or a fault inside the dumper) must not recurse.
    if (InterlockedCompareExchange(&g_active, 1, 0) != 0)
    {
        return EXCEPTION_EXECUTE_HANDLER;
    }
    wchar_t path[kPathCapacity];
    if (WriteMiniDump(g_directory, pointers, path, kPathCapacity))
    {
        (void)RecordPending(g_directory, path);
    }
    return EXCEPTION_EXECUTE_HANDLER;
}

// A CRT-level failure has no exception record of its own: raising one here carries the failing call's context into
// the same dump path as a hardware fault, then the process ends with that code.
[[noreturn]] void FailFast(DWORD code) noexcept
{
    __try
    {
        RaiseException(code, EXCEPTION_NONCONTINUABLE, 0, nullptr);
    }
    __except (UnhandledFilter(GetExceptionInformation()))
    {
    }
    TerminateProcess(GetCurrentProcess(), code);
    ExitProcess(code);
}

void __cdecl OnInvalidParameter(const wchar_t*, const wchar_t*, const wchar_t*, unsigned int, uintptr_t)
{
    FailFast(kInvalidParameterCode);
}

void __cdecl OnPureCall()
{
    FailFast(kPureCallCode);
}

void OnTerminate()
{
    FailFast(kTerminateCode);
}

void __cdecl OnAbort(int)
{
    FailFast(kAbortCode);
}
} // namespace

std::wstring DefaultDirectory()
{
    const std::wstring root = SettingsStore::DefaultDirectory();
    return root.empty() ? std::wstring{} : root + L"\\Crashes";
}

// WriteMiniDump and RecordPending are the crash path: fixed buffers, CreateFileW, a raw HANDLE closed by hand
// rather than a WIL wrapper. A heap corruption is the case they exist for (crash-handling skill), so they add
// nothing beyond the Win32 calls themselves; TakePending and PruneDumps below run on an ordinary start only and
// use the usual RAII.
bool WriteMiniDump(const wchar_t* directory, _EXCEPTION_POINTERS* exception, wchar_t* pathOut,
                   std::size_t pathCapacity) noexcept
{
    if (pathOut == nullptr || pathCapacity == 0)
    {
        return false;
    }
    pathOut[0] = 0;
    if (directory == nullptr || *directory == 0 || !CreateDirectoryTree(directory))
    {
        return false;
    }
    SYSTEMTIME time{};
    GetLocalTime(&time);
    if (swprintf_s(pathOut, pathCapacity, L"%s\\RedPrism-%04u%02u%02u-%02u%02u%02u-%lu.dmp", directory, time.wYear,
                   time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond,
                   static_cast<unsigned long>(GetCurrentProcessId())) <= 0)
    {
        pathOut[0] = 0;
        return false;
    }
    const HANDLE file = CreateFileW(pathOut, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
    {
        pathOut[0] = 0;
        return false;
    }
    MINIDUMP_EXCEPTION_INFORMATION information{};
    information.ThreadId = GetCurrentThreadId();
    information.ExceptionPointers = exception;
    information.ClientPointers = FALSE;
    const BOOL written =
        MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file, static_cast<MINIDUMP_TYPE>(kDumpType),
                          exception ? &information : nullptr, nullptr, nullptr);
    (void)CloseHandle(file);
    if (!written)
    {
        (void)DeleteFileW(pathOut);
        pathOut[0] = 0;
        return false;
    }
    return true;
}

bool RecordPending(const wchar_t* directory, const wchar_t* dumpPath) noexcept
{
    if (directory == nullptr || dumpPath == nullptr || *dumpPath == 0)
    {
        return false;
    }
    wchar_t marker[kPathCapacity];
    if (!JoinPath(directory, kPendingName, marker, kPathCapacity))
    {
        return false;
    }
    char utf8[kPathCapacity * 3];
    const int bytes =
        WideCharToMultiByte(CP_UTF8, 0, dumpPath, -1, utf8, static_cast<int>(sizeof(utf8)) - 1, nullptr, nullptr);
    if (bytes <= 1)
    {
        return false;
    }
    utf8[bytes - 1] = '\n';
    const HANDLE file = CreateFileW(marker, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
    {
        return false;
    }
    DWORD writtenBytes = 0;
    const BOOL ok = WriteFile(file, utf8, static_cast<DWORD>(bytes), &writtenBytes, nullptr);
    (void)CloseHandle(file);
    return ok && writtenBytes == static_cast<DWORD>(bytes);
}

std::optional<std::wstring> TakePending(const std::wstring& directory)
{
    const std::wstring marker = directory + L"\\" + kPendingName;
    std::string bytes;
    if (!SettingsStore::ReadFileBytes(marker, bytes))
    {
        return std::nullopt;
    }
    (void)DeleteFileW(marker.c_str());
    while (!bytes.empty() && (bytes.back() == '\n' || bytes.back() == '\r'))
    {
        bytes.pop_back();
    }
    if (bytes.empty())
    {
        return std::nullopt;
    }
    std::wstring path = Utf8ToWide(bytes);
    if (path.empty())
    {
        return std::nullopt;
    }
    return path;
}

std::size_t PruneDumps(const std::wstring& directory, std::size_t keep)
{
    std::vector<std::wstring> names;
    WIN32_FIND_DATAW found{};
    wil::unique_hfind search{FindFirstFileW((directory + L"\\RedPrism-*.dmp").c_str(), &found)};
    if (!search)
    {
        return 0;
    }
    do
    {
        if ((found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
        {
            names.emplace_back(found.cFileName);
        }
    } while (FindNextFileW(search.get(), &found));
    search.reset();
    if (names.size() <= keep)
    {
        return 0;
    }
    std::sort(names.begin(), names.end(), std::greater<>{});
    std::size_t deleted = 0;
    for (std::size_t index = keep; index < names.size(); ++index)
    {
        if (DeleteFileW((directory + L"\\" + names[index]).c_str()))
        {
            ++deleted;
        }
    }
    return deleted;
}

void InstallCrashHandler(const wchar_t* directory) noexcept
{
    if (directory == nullptr || wcscpy_s(g_directory, directory) != 0)
    {
        g_directory[0] = 0;
    }
    (void)SetUnhandledExceptionFilter(UnhandledFilter);
    (void)_set_invalid_parameter_handler(OnInvalidParameter);
    (void)_set_purecall_handler(OnPureCall);
    (void)std::set_terminate(OnTerminate);
    (void)std::signal(SIGABRT, OnAbort);
    (void)_set_abort_behavior(0, _WRITE_ABORT_MSG);
}

void RaiseProbeCrash() noexcept
{
#if defined(__SANITIZE_ADDRESS__)
    // AddressSanitizer owns access violations in its builds (its runtime reports them and exits before any filter
    // runs), so the probe raises the application-defined code there; it travels through the same installed filter.
    constexpr DWORD code = kProbeCode;
#else
    constexpr DWORD code = EXCEPTION_ACCESS_VIOLATION;
#endif
    RaiseException(code, EXCEPTION_NONCONTINUABLE, 0, nullptr);
    ExitProcess(code);
}
} // namespace CrashDumps
