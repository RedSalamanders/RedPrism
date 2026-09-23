# RedPrism crash handling

Status: current normative repository contract
Last reviewed: 2026-09-23
Owner: `Common/CrashDumps.*`, `RedPrism/Main.cpp`

## Scope

What happens when RedPrism fails: a local minidump, a marker for the next start, and a prompt that names the file.
Nothing is uploaded, ever. The gates live in [`Testing_Validation.md`](../Testing/Testing_Validation.md).

## Where

`%LocalAppData%\RedPrism\Crashes\RedPrism-<yyyyMMdd-HHmmss>-<pid>.dmp`, next to `pending.txt` (the marker) and
the settings file's folder ([`Core_Settings.md`](Core_Settings.md)). The newest ten dumps are kept; an ordinary
start prunes the rest (`CrashDumps::PruneDumps`, name order is age order).

A harness run (`--self-test`, `--screenshot`, `--demo`, `--bench`) never writes into the user's folder: its dumps
and marker go to `%TEMP%\RedPrism.Harness\<pid>\` (`Main.cpp` `HarnessCrashDirectory`; the ordinary folder only
when the temp path is unavailable), one folder per process, so concurrent runs never share one (review fix RF-708).
Nothing reads, prompts for or prunes that folder; a crashed harness run's dump stays there for its log to name.
A Debug harness run also ends with exit code 3 at the first CRT assertion or error report, which
`HarnessCrtReportHook` writes to stderr, since no one is there to answer its dialog (RF-702).

## Handlers

`CrashDumps::InstallCrashHandler(directory)` runs before anything else in `wWinMain`'s `RunApplication`, once the
command line parsed:

| Failure | Handler | Exit code |
| --- | --- | --- |
| unhandled SEH exception (access violation, …) | `SetUnhandledExceptionFilter` | the exception code |
| `std::terminate` (an escaped C++ exception, a `noexcept` violation) | `std::set_terminate` | `0xE0000001` |
| invalid CRT parameter | `_set_invalid_parameter_handler` | `0xE0000002` |
| pure virtual call | `_set_purecall_handler` | `0xE0000003` |
| `abort` | `signal(SIGABRT)` | `0xE0000004` |

The CRT-level handlers raise a synthetic exception with their code so the same filter writes the dump with the
failing call's context, then the process ends with `TerminateProcess`. The filter writes with `MiniDumpWriteDump`
(data segments, handles, thread information, unloaded modules and indirectly referenced memory) into fixed stack
buffers: no heap, no CRT formatting beyond `swprintf_s` into those buffers. A second fault while a dump is written
ends the process without recursion (`InterlockedCompareExchange` guard). The Windows Error Reporting dialog never
appears: the filter answers `EXCEPTION_EXECUTE_HANDLER`.

Debug CRT assertion dialogs are separate and stay as they are for developers; the test executables route them to
stderr ([`Testing_Validation.md`](../Testing/Testing_Validation.md)).

## The next start

An ordinary start (not a harness switch) reads and removes `pending.txt`; when it named a dump, a message box says
RedPrism closed unexpectedly, names the file and offers to open its folder (`explorer.exe /select,<dump>`); Yes
opens it, No does nothing. The text says nothing is sent anywhere, because nothing is. Then the directory is
pruned.

## The probe

`RedPrism.exe --crash-probe <directory>` installs the handlers with `<directory>` for its dumps and raises an
access violation through `RaiseException` before any window exists. An AddressSanitizer build raises the
application-defined `0xE0000005` instead: the sanitizer runtime reports access violations itself and exits before
any filter runs (its own gate proves that), and the probe's job is the installed filter. `test.ps1` runs it in every configuration and
requires a nonzero exit, exactly one `RedPrism-*.dmp` in the directory with a size above zero, and a `pending.txt`
naming that file. The directory is under `.build\` and removed afterwards; the user's crash folder is untouched.

## Validation

`HostTests` `crash.*`: the default directory ends in `\RedPrism\Crashes`; pruning twelve dumps to ten deletes the
two oldest and nothing on a second pass; `WriteMiniDump` without an exception writes a non-empty `RedPrism-*.dmp`
into a temporary directory; `RecordPending` / `TakePending` round-trip the path and the marker is gone afterwards;
an empty directory is refused with an empty path. The crash probe above proves the installed filter end to end.
