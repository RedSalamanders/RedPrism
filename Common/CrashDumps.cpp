#include "CrashDumps.h"

#include "SettingsStore.h"
#include "Utf8.h"

#include <algorithm>
#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <new>
#include <string_view>
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
constexpr DWORD kDumpType =
    MiniDumpWithDataSegs | MiniDumpWithHandleData | MiniDumpWithThreadInfo | MiniDumpWithUnloadedModules | MiniDumpWithIndirectlyReferencedMemory;
constexpr DWORD kTerminateCode        = 0xE0000001;
constexpr DWORD kInvalidParameterCode = 0xE0000002;
constexpr DWORD kPureCallCode         = 0xE0000003;
constexpr DWORD kAbortCode            = 0xE0000004;
constexpr DWORD kProbeCode            = 0xE0000005;
constexpr wchar_t kPendingName[]      = L"pending.txt";
constexpr std::wstring_view kReportPrefix{L"RedPrism-report-"};

wchar_t g_directory[kPathCapacity] = {};
volatile LONG g_ownerThread        = 0;
wil::unique_handle g_dumpFinished;
wil::unique_handle g_competingFault;
std::atomic<bool> g_handlersInstalled{false};
bool g_workerProbe = false; // installed before any worker is created

[[nodiscard]] bool IsPlainDirectory(const wchar_t* path) noexcept
{
    const DWORD attributes = GetFileAttributesW(path);
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) == FILE_ATTRIBUTE_DIRECTORY;
}

// Creates every missing component of the crash directory using a stack copy of the path.
[[nodiscard]] bool CreateDirectoryTree(const wchar_t* directory) noexcept
{
    const std::size_t length = wcsnlen_s(directory, kPathCapacity);
    if (length < 3 || length >= kPathCapacity || directory[1] != L':' || directory[2] != L'\\')
    {
        return false;
    }
    wchar_t path[kPathCapacity]{};
    std::copy_n(directory, length, path);
    for (wchar_t* cursor = path + 3; *cursor; ++cursor) // past the "C:\" drive root
    {
        if (*cursor == L'\\' || *cursor == L'/')
        {
            *cursor = 0;
            (void)CreateDirectoryW(path, nullptr);
            if (! IsPlainDirectory(path))
            {
                return false;
            }
            *cursor = L'\\';
        }
    }
    (void)CreateDirectoryW(path, nullptr);
    return IsPlainDirectory(path);
}

[[nodiscard]] bool JoinPath(const wchar_t* directory, const wchar_t* name, wchar_t* out, std::size_t capacity) noexcept
{
    return swprintf_s(out, capacity, L"%s\\%s", directory, name) > 0;
}

[[nodiscard]] bool IsOwnedReportName(std::wstring_view name) noexcept
{
    constexpr std::size_t timeDigits = 20;
    constexpr std::size_t pidDigits  = 10;
    constexpr std::wstring_view extension{L".dmp"};
    if (name.size() != kReportPrefix.size() + timeDigits + 1 + pidDigits + extension.size() || ! name.starts_with(kReportPrefix) || ! name.ends_with(extension))
    {
        return false;
    }
    const auto digits = [](std::wstring_view value) noexcept
    {
        for (const wchar_t character : value)
        {
            if (character < L'0' || character > L'9')
            {
                return false;
            }
        }
        return true;
    };
    name.remove_prefix(kReportPrefix.size());
    return name[timeDigits] == L'-' && digits(name.substr(0, timeDigits)) && digits(name.substr(timeDigits + 1, pidDigits));
}

LONG WINAPI UnhandledFilter(EXCEPTION_POINTERS* pointers) noexcept
{
    const LONG thread = static_cast<LONG>(GetCurrentThreadId());
    const LONG owner  = InterlockedCompareExchange(&g_ownerThread, thread, 0);
    if (owner != 0)
    {
        if (owner != thread)
        {
            // A different crashing thread must not terminate the process before the owner finishes the marker.
            // Only a recursive fault on the dumping thread gives up immediately.
            if (g_workerProbe)
            {
                (void)SetEvent(g_competingFault.get());
            }
            (void)WaitForSingleObject(g_dumpFinished.get(), INFINITE);
        }
        return EXCEPTION_EXECUTE_HANDLER;
    }
    if (g_workerProbe && WaitForSingleObject(g_competingFault.get(), 5000) != WAIT_OBJECT_0)
    {
        // Probe failure bound, not production behavior: no dump means the two-worker gate fails decisively.
        TerminateProcess(GetCurrentProcess(), kProbeCode);
    }
    wchar_t path[kPathCapacity];
    if (WriteMiniDump(g_directory, pointers, path, kPathCapacity))
    {
        (void)RecordPending(g_directory, path);
    }
    (void)SetEvent(g_dumpFinished.get());
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
        TerminateProcess(GetCurrentProcess(), code);
    }
    ExitProcess(code);
}

void __cdecl OnInvalidParameter(const wchar_t*, const wchar_t*, const wchar_t*, unsigned int, uintptr_t) noexcept
{
    FailFast(kInvalidParameterCode);
}

void __cdecl OnPureCall() noexcept
{
    FailFast(kPureCallCode);
}

void OnTerminate() noexcept
{
    FailFast(kTerminateCode);
}

void __cdecl OnAbort(int) noexcept
{
    FailFast(kAbortCode);
}
} // namespace

std::wstring DefaultDirectory()
{
    const std::wstring root = SettingsStore::DefaultDirectory();
    return root.empty() ? std::wstring{} : root + L"\\Crashes";
}

std::wstring HarnessDirectory(const wchar_t* requested)
{
    wchar_t executable[kPathCapacity]{};
    const DWORD length = GetModuleFileNameW(nullptr, executable, static_cast<DWORD>(kPathCapacity));
    if (length == 0 || length >= kPathCapacity)
    {
        return {};
    }
    constexpr std::wstring_view buildSuffix{L"\\.build"};
    std::wstring build{executable, length};
    do
    {
        const auto separator = build.rfind(L'\\');
        if (separator == std::wstring::npos || separator <= 2)
        {
            return {};
        }
        build.resize(separator);
    } while (! build.ends_with(buildSuffix));
    const auto solution    = build.substr(0, build.size() - buildSuffix.size()) + L"\\RedPrism.sln";
    const DWORD attributes = GetFileAttributesW(solution.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != 0)
    {
        return {};
    }

    const std::wstring selected = requested ? std::wstring{requested} : build;
    wchar_t canonical[kPathCapacity]{};
    const DWORD size = GetFullPathNameW(selected.c_str(), static_cast<DWORD>(kPathCapacity), canonical, nullptr);
    if (size == 0 || size >= kPathCapacity || selected != canonical || selected.size() < 3 || selected[1] != L':' || selected[2] != L'\\' ||
        (requested && ! selected.starts_with(build + L"\\")))
    {
        return {};
    }
    for (std::wstring current = selected; current.size() > 3; current.resize(current.rfind(L'\\')))
    {
        if (! IsPlainDirectory(current.c_str()))
        {
            return {};
        }
    }
    if (requested)
    {
        return selected;
    }
    const auto artifacts = build + L"\\crash-probe-artifacts";
    if (GetFileAttributesW(artifacts.c_str()) != INVALID_FILE_ATTRIBUTES && ! IsPlainDirectory(artifacts.c_str()))
    {
        return {};
    }
    const auto run = std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64());
    return artifacts + L"\\" + run;
}

// WriteMiniDump and RecordPending are the crash path: fixed buffers, CreateFileW, a raw HANDLE closed by hand
// rather than a WIL wrapper. A heap corruption is the case they exist for (crash-handling skill), so they add
// nothing beyond the Win32 calls themselves; TakePending and PruneDumps below run on an ordinary start only and
// use the usual RAII.
bool WriteMiniDump(const wchar_t* directory, _EXCEPTION_POINTERS* exception, wchar_t* pathOut, std::size_t pathCapacity) noexcept
{
    if (pathOut == nullptr || pathCapacity == 0)
    {
        return false;
    }
    pathOut[0] = 0;
    if (directory == nullptr || *directory == 0 || ! CreateDirectoryTree(directory))
    {
        return false;
    }
    FILETIME time{};
    GetSystemTimePreciseAsFileTime(&time);
    const auto timestamp = (static_cast<unsigned long long>(time.dwHighDateTime) << 32) | time.dwLowDateTime;
    if (swprintf_s(pathOut, pathCapacity, L"%s\\RedPrism-report-%020llu-%010lu.dmp", directory, timestamp, static_cast<unsigned long>(GetCurrentProcessId())) <=
        0)
    {
        pathOut[0] = 0;
        return false;
    }
    const HANDLE file = CreateFileW(pathOut, GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
    {
        pathOut[0] = 0;
        pathOut[0] = 0;
        return false;
    }
    MINIDUMP_EXCEPTION_INFORMATION information{};
    information.ThreadId          = GetCurrentThreadId();
    information.ExceptionPointers = exception;
    information.ClientPointers    = FALSE;
    const bool written =
        MiniDumpWriteDump(
            GetCurrentProcess(), GetCurrentProcessId(), file, static_cast<MINIDUMP_TYPE>(kDumpType), exception ? &information : nullptr, nullptr, nullptr) &&
        FlushFileBuffers(file);
    (void)CloseHandle(file);
    if (! written)
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
    wchar_t temporary[kPathCapacity];
    if (! JoinPath(directory, kPendingName, marker, kPathCapacity) ||
        swprintf_s(temporary, kPathCapacity, L"%s\\pending-%lu-%llu.tmp", directory, GetCurrentProcessId(), GetTickCount64()) <= 0)
    {
        return false;
    }
    char utf8[kPathCapacity * 3];
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, dumpPath, -1, utf8, static_cast<int>(sizeof(utf8)) - 1, nullptr, nullptr);
    if (bytes <= 1)
    {
        return false;
    }
    utf8[bytes - 1]   = '\n';
    const HANDLE file = CreateFileW(temporary, GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
    {
        return false;
    }
    DWORD writtenBytes = 0;
    const bool ok =
        WriteFile(file, utf8, static_cast<DWORD>(bytes), &writtenBytes, nullptr) && writtenBytes == static_cast<DWORD>(bytes) && FlushFileBuffers(file);
    (void)CloseHandle(file);
    if (! ok || ! MoveFileExW(temporary, marker, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        (void)DeleteFileW(temporary);
        return false;
    }
    return true;
}

std::optional<std::wstring> TakePending(const std::wstring& directory)
{
    if (directory.empty() || ! IsPlainDirectory(directory.c_str()))
    {
        return std::nullopt;
    }
    const std::wstring marker = directory + L"\\" + kPendingName;
    wil::unique_hfile file{CreateFileW(marker.c_str(), GENERIC_READ | DELETE, 0, nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr)};
    BY_HANDLE_FILE_INFORMATION information{};
    if (! file || ! GetFileInformationByHandle(file.get(), &information) ||
        (information.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != 0)
    {
        return std::nullopt;
    }
    FILE_DISPOSITION_INFO claimed{TRUE};
    if (! SetFileInformationByHandle(file.get(), FileDispositionInfo, &claimed, sizeof(claimed)))
    {
        return std::nullopt;
    }
    char buffer[kPathCapacity * 3]{};
    DWORD read = 0;
    if (! ReadFile(file.get(), buffer, sizeof(buffer), &read, nullptr) || read == 0 || read == sizeof(buffer))
    {
        return std::nullopt;
    }
    std::string_view bytes{buffer, read};
    while (! bytes.empty() && (bytes.back() == '\n' || bytes.back() == '\r'))
    {
        bytes.remove_suffix(1);
    }
    std::wstring path         = Utf8ToWide(bytes);
    const std::wstring prefix = directory + L"\\";
    if (! path.starts_with(prefix))
    {
        return std::nullopt;
    }
    const std::wstring_view leaf = std::wstring_view{path}.substr(prefix.size());
    if (! leaf.starts_with(L"RedPrism-") || ! leaf.ends_with(L".dmp"))
    {
        return std::nullopt;
    }
    for (const wchar_t character : leaf)
    {
        if (character == L'\\' || character == L'/' || character == L':' || character == L'\0')
        {
            return std::nullopt;
        }
    }
    const DWORD attributes = GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != 0)
    {
        return std::nullopt;
    }
    return path;
}

std::size_t PruneDumps(const std::wstring& directory, std::size_t keep)
{
    if (directory.empty() || ! IsPlainDirectory(directory.c_str()))
    {
        return 0;
    }
    std::vector<std::wstring> names;
    WIN32_FIND_DATAW found{};
    wil::unique_hfind search{FindFirstFileW((directory + L"\\RedPrism-report-*.dmp").c_str(), &found)};
    if (! search)
    {
        return 0;
    }
    do
    {
        if ((found.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) == 0 && IsOwnedReportName(found.cFileName))
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

HRESULT InstallCrashHandler(const wchar_t* directory, bool workerProbe) noexcept
{
    if (g_handlersInstalled.load(std::memory_order_acquire))
    {
        return HRESULT_FROM_WIN32(ERROR_ALREADY_INITIALIZED);
    }
    g_dumpFinished.reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    if (! g_dumpFinished)
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }
    if (workerProbe)
    {
        g_competingFault.reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
        if (! g_competingFault)
        {
            return HRESULT_FROM_WIN32(GetLastError());
        }
    }
    g_workerProbe = workerProbe;
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
    g_handlersInstalled.store(true, std::memory_order_release);
    return S_OK;
}

void InstallWorkerTerminateHandler() noexcept
{
    thread_local bool installed = false;
    if (! installed && g_handlersInstalled.load(std::memory_order_acquire))
    {
        (void)std::set_terminate(OnTerminate);
        installed = true;
    }
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
