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
inline constexpr std::size_t kKeepNewest = 10;     // dumps kept per directory

// <LocalAppData>\RedPrism\Crashes; empty when the known folder is unavailable.
[[nodiscard]] std::wstring DefaultDirectory();

// Writes <directory>\RedPrism-<yyyyMMdd-HHmmss>-<pid>.dmp for the running process, with the exception when given
// (a snapshot without one). Creates the directory tree. Uses no heap, so an exception filter may call it.
// `pathOut` receives the file written; false leaves nothing behind.
[[nodiscard]] bool WriteMiniDump(const wchar_t* directory, _EXCEPTION_POINTERS* exception, wchar_t* pathOut,
                                 std::size_t pathCapacity) noexcept;

// The marker a crash leaves for the next start: <directory>\pending.txt holding the dump path as UTF-8.
[[nodiscard]] bool RecordPending(const wchar_t* directory, const wchar_t* dumpPath) noexcept;
// Reads and removes the marker; nullopt when there is none.
[[nodiscard]] std::optional<std::wstring> TakePending(const std::wstring& directory);

// Deletes the RedPrism-*.dmp files beyond the newest `keep` (the names carry the time, so name order is age
// order) and returns how many went.
std::size_t PruneDumps(const std::wstring& directory, std::size_t keep);

// Installs the process-wide handlers: an unhandled exception, an invalid CRT parameter, a pure virtual call,
// std::terminate and abort each write a dump into `directory`, record it as pending and end the process with the
// exception code (0xE0000001 terminate, 0xE0000002 invalid parameter, 0xE0000003 pure call, 0xE0000004 abort).
void InstallCrashHandler(const wchar_t* directory) noexcept;

// Raises a fault through whatever handlers are installed (the --crash-probe gate): an access violation, or the
// application-defined 0xE0000005 in AddressSanitizer builds, whose runtime reports access violations itself.
// Never returns.
[[noreturn]] void RaiseProbeCrash() noexcept;
} // namespace CrashDumps
