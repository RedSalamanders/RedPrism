# RedPrism validation

Status: current normative repository contract
Last reviewed: 2026-09-20
Owner: `test.ps1`, `Tests/HostTests`, and `RedPrism.exe` diagnostic switches

## Scope

This contract owns how RedPrism is proven. Domain specs name the behavior; this file names the gates that MUST pass
before a phase is Done. Screenshot policy for later chrome lives here so C7 does not invent a second rule.

## Entrypoint

`.\test.ps1 [-Platform x64|ARM64] [-Configuration Debug|Release|'ASan Debug'] [-Rebuild]` builds through `build.ps1`,
then runs every assertion below. It MUST exit 0 only when all of them pass. Expected nonzero exits from isolated
negative-test children (`HostTests.exe --asan-probe`, `RedPrism.exe --no-such-switch`) MUST NOT become the test
entrypoint's success exit code.

ARM64 runtime qualification requires a native ARM64 host. Cross-built ARM64 binaries are produced by `build.ps1` only.

## C1 gates

| Gate | Pass condition |
| --- | --- |
| Version identity | `RedPrism.exe` `OriginalFilename` is `RedPrism.exe` and `ProductName` is `RedPrism` |
| HostTests | `HostTests.exe` exit 0 (adapter-of-monitor, frame-action selection, command catalog) |
| ASan Debug only | `HostTests.exe --asan-probe` MUST NOT exit 0 and the captured log MUST contain `AddressSanitizer: heap-use-after-free`. An ordinary crash without that diagnostic is a failed probe. |
| Help | `RedPrism.exe --help` exit 0; redirected stdout contains `--self-test` |
| Unknown switch | `RedPrism.exe --no-such-switch` exit 2 |
| WARP self-test | Hidden `RedPrism.exe --self-test --warp` exit 0 |

`ASAN_OPTIONS` for the probe is `halt_on_error=1:abort_on_error=0:detect_leaks=0`.

## Later gates (record now, implement with the feature)

| When | Gate |
| --- | --- |
| C2 | Provenance test rejects the wrong DxUi pin; overlay screenshot from the **application harness** |
| C4 | Document/history unit tests under Debug and ASan Debug; 256 MiB trim test |
| C5 | ASan I/O tests; exploded v7 fixture round-trip; path-traversal reject; missing HEIF is a clean error |
| C6 | WARP composite golden; device-loss recreate; occlusion does not Present |
| C7 | Harness screenshots of the empty-document shell at 96 and 144 DPI |
| C8–C13 | Tool and filter goldens ported from retained numeric golden cases behavior, not UI-driver scripts |

In-process tests live under `Tests/` and are invoked by `test.ps1`. GUI tests default to no-activation. Interactive
desktop focus requires an explicit lease in a later spec.

## Screenshots

Screenshots for specs and docs MUST come from the application harness (Windows.Graphics.Capture of an owned window, or
an equivalent harness helper). Computer Use and desktop screenshot tools are prohibited as evidence.

## Fail-closed data root

When a test needs a writable sandbox for documents or perf traces, it MUST use `X:\RedPrism.Perf` (any local drive
letter plus a directory ownership marker), never repository `.build/` as a data sandbox. C1 does not create that tree.

## Skills

Skill or `AGENTS.md` edits MUST pass `.\validate-skills.ps1` in the same change.

## Required local matrix for C1 closeout

```powershell
.\validate-skills.ps1
.\build.ps1 -Platform x64 -Configuration Debug
.\build.ps1 -Platform x64 -Configuration Release
.\build.ps1 -Platform x64 -Configuration "ASan Debug"
.\test.ps1 -Platform x64 -Configuration Debug
.\test.ps1 -Platform x64 -Configuration "ASan Debug"
```
