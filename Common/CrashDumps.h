#pragma once

#include <cstddef>
#include <optional>
#include <string>

#include <windows.h>

// Local crash reports (Specs/Core/Core_CrashHandling.md): a minidump per crash under %LocalAppData%\RedPrism\Crashes
// and a marker the next start reads. Nothing here uploads anything.
namespace CrashDumps
{
inline constexpr std::size_t kPathCapacity = 1024; // wchar_t, the buffers the crash path formats into
inline constexpr std::size_t kKeepNewest   = 10;   // post-adoption dumps kept by interactive startup

// <LocalAppData>\RedPrism\Crashes; empty when the known folder is unavailable.
[[nodiscard]] std::wstring DefaultDirectory();

// Returns a unique repository .build path or verifies an existing requested probe directory under .build.
// An empty result disables harness crash output when no owned location is available.
[[nodiscard]] std::wstring HarnessDirectory(const wchar_t* requested = nullptr);

// Writes <directory>\RedPrism-report-<UTC file time>-<pid>.dmp for the running process, with the exception when given
// (a snapshot without one). Creates the directory tree. Uses no heap, so an exception filter may call it.
// `pathOut` receives the file written; false leaves nothing behind.
[[nodiscard]] bool WriteMiniDump(const wchar_t* directory, _EXCEPTION_POINTERS* exception, wchar_t* pathOut, std::size_t pathCapacity) noexcept;

// Atomically publishes <directory>\pending.txt holding the completed dump path as UTF-8.
[[nodiscard]] bool RecordPending(const wchar_t* directory, const wchar_t* dumpPath) noexcept;
// Claims the marker once, then validates its local dump path. Legacy dump names remain valid.
[[nodiscard]] std::optional<std::wstring> TakePending(const std::wstring& directory);

// Deletes post-adoption reports beyond the newest `keep` and returns the number removed.
std::size_t PruneDumps(const std::wstring& directory, std::size_t keep);

// Installs the process-wide handlers: an unhandled exception, an invalid CRT parameter, a pure virtual call,
// std::terminate and abort each write a dump into `directory`, record it as pending and end the process with the
// exception code (0xE0000001 terminate, 0xE0000002 invalid parameter, 0xE0000003 pure call, 0xE0000004 abort).
[[nodiscard]] HRESULT InstallCrashHandler(const wchar_t* directory, bool workerProbe = false) noexcept;

// The UCRT terminate handler is per-thread. Private-pool callbacks call this before user code; it installs
// nothing unless the process crash handler was installed (in-process HostTests keep their normal handlers).
void InstallWorkerTerminateHandler() noexcept;

// Raises a fault through whatever handlers are installed (the --crash-probe gate): an access violation, or the
// application-defined 0xE0000005 in AddressSanitizer builds, whose runtime reports access violations itself.
// Never returns.
[[noreturn]] void RaiseProbeCrash() noexcept;
} // namespace CrashDumps
