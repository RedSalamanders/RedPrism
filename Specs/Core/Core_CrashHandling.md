# RedPrism crash handling

Status: current normative repository contract
Last reviewed: 2026-09-27
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
buffers: no heap, no CRT formatting beyond `swprintf_s` into those buffers. An `InterlockedCompareExchange` guard
records the dumping thread's id. A recursive fault on that thread gives up immediately; a fault on another thread
waits on the pre-created manual-reset event until the dump and pending marker have been attempted. It MUST NOT
end the process while the owner writes. Initialization returns an HRESULT and fails startup if it cannot create
the event. Every private-pool callback installs the UCRT's per-thread terminate handler before executing work.
The Windows Error Reporting dialog never
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

`--crash-probe <directory> --crash-probe-worker` starts two private-pool callbacks behind an event gate. One calls
`std::terminate`, verifying the per-worker UCRT hook; the other raises the normal probe fault. In this child only,
the dump owner waits until the competing fault enters the filter, so the gate proves overlapping faults, not just
two submitted callbacks. It must still produce exactly one nonempty dump and its marker. The probe has bounded
failure waits and uses no window; the pool temporarily owns implicit MTA support. Normal crash handling never
waits for a second fault.

## Validation

`HostTests` `crash.*`: the default directory ends in `\RedPrism\Crashes`; pruning twelve dumps to ten deletes the
two oldest and nothing on a second pass; `WriteMiniDump` without an exception writes a non-empty `RedPrism-*.dmp`
into a temporary directory; `RecordPending` / `TakePending` round-trip the path and the marker is gone afterwards;
an empty directory is refused with an empty path. The crash probe above proves the installed filter end to end.
