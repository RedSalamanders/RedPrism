# RedPrism crash handling

Status: current normative repository contract
Last reviewed: 2026-11-24
Owner: `Common/CrashDumps.*`, `RedPrism/Main.cpp`

## Scope

What happens when RedPrism fails: a local minidump, a marker for the next start, and a prompt that names the file.
Nothing is uploaded, ever. The gates live in [`Testing_Validation.md`](../Testing/Testing_Validation.md).

## Where

`%LocalAppData%\RedPrism\Crashes\RedPrism-report-<UTC file time>-<pid>.dmp`, next to `pending.txt` and
the settings file's folder ([`Core_Settings.md`](Core_Settings.md)). New report names identify the
post-adoption collection and sort by UTC creation time. An ordinary interactive start keeps the newest ten
and prunes older entries in that collection only. Legacy names and unrelated files stay in place.
RedPrism uses a single minidump per report.

A harness run (`--self-test`, `--screenshot`, `--demo`, `--bench`) uses a unique repository-owned folder under
`.build\crash-probe-artifacts` for its dump and marker. If that location is unavailable, harness crash output
is disabled. Harness modes leave the ordinary crash store untouched. Raw probe dumps stay outside CI uploads.
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

This fixed-buffer crash writer is the deliberate exception to the ordinary `std::format_to_n` formatting policy.
Do not replace it during generic style cleanup; a crash-path redesign must preserve the failure-context allocation
constraint and receive its own validation under this contract.

The scheduler's owner-wake endpoint treats failure to signal a live owned event after work publication as an
invariant failure and calls `std::terminate`; the existing installed handler records code `0xE0000001`. Startup
event/wait creation failures instead propagate an HRESULT and unwind before work can be accepted. Endpoint
callbacks install the worker terminate handler before accessing state. A private injected-failure qualification
runs separate children and verifies the actual dump, marker and exception code; it never installs handlers in
the in-process Host suite.

Debug CRT assertion dialogs are separate and stay as they are for developers; the test executables route them to
stderr ([`Testing_Validation.md`](../Testing/Testing_Validation.md)).

## The next start

The crash path flushes a completed dump and atomically publishes `pending.txt`. A healthy interactive start
claims the marker exclusively. A valid current or legacy dump in the same folder triggers one message box.
It describes the crash, names the local file and offers to select it in Explorer. Startup then prunes the
post-adoption collection.

## The probe

`RedPrism.exe --crash-probe <directory>` verifies an existing directory under the repository `.build`, installs
the handlers for that directory and raises an access violation through `RaiseException` before any window
exists. An AddressSanitizer build raises the
application-defined `0xE0000005` instead: the sanitizer runtime reports access violations itself and exits before
any filter runs. `test.ps1` runs the installed-filter probe in every configuration and requires a nonzero
exit, exactly one `RedPrism-report-*.dmp` with an `MDMP` header, and a `pending.txt` naming that file.
The runner retains the diagnostics locally under `.build\crash-probe-artifacts`.

`--crash-probe <directory> --crash-probe-worker` starts two private-pool callbacks behind an event gate. One calls
`std::terminate`, verifying the per-worker UCRT hook; the other raises the normal probe fault. In this child only,
the dump owner waits until the competing fault enters the filter, so the gate proves overlapping faults, not just
two submitted callbacks. It must still produce exactly one nonempty dump and its marker. The probe has bounded
failure waits and uses no window; the pool temporarily owns implicit MTA support. Normal crash handling never
waits for a second fault.

## Validation

`HostTests` `crash.*` checks the default directory, retention at zero, ten and eleven owned reports,
preservation of legacy and unrelated files, and a real minidump in an isolated fixture. It checks that the
marker is claimed once for both current and legacy dumps, rejects a path outside the store and refuses an
empty directory. The crash probes exercise the installed filter end to end.
