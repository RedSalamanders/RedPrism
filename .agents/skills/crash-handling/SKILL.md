---
name: crash-handling
description: Change how RedPrism records a crash (local minidumps, the pending marker, the next-start prompt, the --crash-probe gate) or add a failure path that must leave a dump. Use for Common/CrashDumps.* and the handler install in Main.cpp; never for uploading anything.
---

# Crash handling

Read `Specs/Core/Core_CrashHandling.md` first; it is the contract.

- `CrashDumps::InstallCrashHandler(directory)` is the first thing `RunApplication` does after parsing the command
  line. The unhandled-exception filter, `std::terminate`, the invalid-parameter handler, the pure-call handler and
  `SIGABRT` all end in one filter that writes `RedPrism-<yyyyMMdd-HHmmss>-<pid>.dmp` and `pending.txt` into
  `%LocalAppData%\RedPrism\Crashes`, then the process exits with the exception code (`0xE000000n` for the CRT
  paths).
- The crash path allocates nothing: fixed `wchar_t[kPathCapacity]` buffers, `swprintf_s`, `CreateFileW`,
  `MiniDumpWriteDump`. Keep it that way; a heap corruption is the case it exists for. A recursive fault on the
  dumping thread gives up; another faulting thread waits on the pre-created event until the dump/marker attempt
  finishes. Initialization returns an HRESULT and fails startup when the event cannot be created.
- Never upload, never open a network connection, never phone home. The next start shows a message box naming the
  dump and offering its folder; that is the whole reporting flow.
- Nothing in the crash path runs in a harness switch except `--crash-probe <directory>`, which installs the handler
  with that directory and raises an access violation. `test.ps1` requires a nonzero exit, one dump above zero bytes
  and a marker naming it. Keep the ordinary probe before any window or COM initialization. The companion
  `--crash-probe-worker` creates two private-pool threads (and their implicit MTA cookie), verifies the per-thread
  terminate handler and overlapping faults, and still produces one dump/marker. In an AddressSanitizer build
  the probe raises `0xE0000005` rather than an access violation, because the sanitizer runtime reports access
  violations itself and exits before any filter runs.
- Pruning (`PruneDumps`, ten newest) and the prompt run on an ordinary start only; the self-test, screenshots,
  demos and benchmarks never touch the user's crash folder.
- In-process tests (`HostTests` `crash.*`) cover the writer without an exception, pruning and the marker; they
  write under `%TEMP%` and clean up. A test must never install the handlers in its own process.
