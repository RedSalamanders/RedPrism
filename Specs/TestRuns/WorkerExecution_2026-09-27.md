# P0.5 — Worker execution and crash handling

Status: P0.5 complete on x64; all ARM64 cross-builds passed; native ARM64 runtime untested
Date: 2026-09-27
Source: `237fed2` plus this working tree.
Plan: [UiNeverBlocks](../Plans/WIP/UiNeverBlocks_2026-09-22.md), opening checklist and P0.5.

## Changes

- Added a standard C++ executor interface, immutable cancellation tokens, checked tile grids and dynamic tile
  assignment. The caller participates and joins every helper before releasing inputs. Disjoint output regions
  have exactly one writer; allocation and blocking are forbidden inside production tile bodies.
- Added a private Windows worker pool with three reusable priority dispatch objects and bounded wake counts.
  Closing rejects work, requests cancellation and joins both dispatch callbacks and external parallel callers.
  Helpers belong to their caller, outside the dispatch cleanup group. This prevents competing owners from closing
  the same work object and keeps the stack context alive without another shared allocation.
- Added a separate one-thread wait pool with 32 bounded registrations, one-shot rearming and generation tickets.
  Events remain usable when every compute worker is occupied. Event/context owners retire a registration before
  destroying its inputs. Remove and Close are serialized owner-thread operations.
- Each pool retains its own COM MTA cookie until every callback has returned. Tests create WIC factories on
  workers and verify that an already initialized STA owner keeps its original apartment. Callback scopes apply
  foreground or background QoS and restore system-managed policy afterward.
- Competing crash threads now wait for the dump owner to finish writing the dump and pending marker. The worker
  probe gates two simultaneous faults, including `std::terminate` from a pool thread, and requires exactly one dump.
- Added native code analysis with concurrency SAL annotations and actual guarded/unguarded compiler controls.
  Analysis checks repository code, retaining production stack diagnostics at 16 KiB. Test fixture translation
  units have an explicit 128 KiB budget. Pinned vendor headers are external; their versions are unchanged.

The helper join follows the documented [Windows cancellation/join API](https://learn.microsoft.com/en-us/windows/win32/api/threadpoolapiset/nf-threadpoolapiset-waitforthreadpoolworkcallbacks).
Pool COM lifetime follows [CoIncrementMTAUsage](https://learn.microsoft.com/en-us/windows/win32/api/combaseapi/nf-combaseapi-coincrementmtausage).
The lock wrapper retains WIL ownership while supplying the effects needed by
[concurrency SAL analysis](https://learn.microsoft.com/en-us/cpp/code-quality/annotating-locking-behavior?view=msvc-170).

## Review and independent expectations

Cheaper agents implemented the CLI catalog/tests, executor edge tests and the analysis entrypoint, and reviewed
the pool contracts. The primary implemented pool ownership, parallel execution, cancellation, crash handling,
stress tests and measurements, then reviewed all delegated edits.

New Windows behavior uses independent state-machine expectations under D8. Existing legacy numeric goldens are
unchanged. Tests cover:

- Empty/invalid/maximum tile grids; source/token lifetime; pre-cancel and row-boundary cancel; incomplete tiles.
- Serial-coordinate expected output and exactly one visit per tile for inline, 1-, 2- and 4-worker execution.
- N top-level jobs occupying N workers before each starts parallel execution, plus nested tile execution.
- Actual caller/helper overlap with event gates, and cancellation while Close retains an active caller's frame.
- Cancelled queued wakes, stale wait tickets, the 33rd wait refusal, rearm/remove/close, and a wait completion
  while the compute pool is fully occupied.
- 64 additional saturated/nested/shutdown rounds under ASan; the existing 256-round raster stress remains.
- CLI flag ordering and refusal of a worker probe without an explicit crash-probe directory.

The first STA-preservation expectation was corrected to accept Windows' `MAINSTA` designation and compare the
actual before/after apartment and qualifier. WIC creation and apartment preservation execute in the final gates;
implicit MTA behavior was not inferred solely from the existence of a cookie.

Analysis also found existing diagnostics: a reserved version-information argument, an unchecked color-range
index, crash-path string bounds, an empty exception handler, a nullable CLI benchmark pointer, and a large
benchmark stack frame. These have targeted fixes. The benchmark LUT allocation is outside measured regions;
kernel arithmetic and legacy expected bytes are unchanged. Gradient, pixel-edit and hue test suites were split
into smaller functions to stay below the explicit test stack budget, preserving every expectation. Definitions inherit their
declared SAL contracts, including the Windows entrypoint.

## Validation

| Gate | Result | Local log under `.build/logs/` |
| --- | --- | --- |
| `validate-skills.ps1` | PASS, 15 skills | `workers-skills-final.log` |
| `format.ps1 -Check` | PASS, 116 files | `workers-format-final.log` |
| `git diff --check` | PASS | `workers-diff-check.log` |
| `build.ps1 -Platform x64 -Configuration Release -Analyze` | PASS, full rebuild and both compiler controls | `workers-analysis-verified.log` |
| `test.ps1 -Platform x64 -Configuration Debug` | PASS | `workers-Debug-tests-final.log` |
| `test.ps1 -Platform x64 -Configuration Release` | PASS | `workers-Release-tests-final.log` |
| `test.ps1 -Platform x64 -Configuration 'ASan Debug'` | PASS | `workers-ASan-Debug-tests-final.log` |
| `build.ps1 -Platform ARM64 -Configuration Debug` | PASS, build only | `workers-ARM64-Debug-build.log` |
| `build.ps1 -Platform ARM64 -Configuration Release` | PASS, build only | `workers-ARM64-Release-build.log` |
| `build.ps1 -Platform ARM64 -Configuration 'ASan Debug'` | PASS, build only | `workers-ARM64-ASan-Debug-build.log` |

All three final x64 logs end with `All tests passed.` without failing assertions. Each includes HostTests,
DocumentTests, CompositionTests, IoTests, WARP self-test, both crash probes, kernel/checksum smoke and nine harness
screenshots (27 total). ASan additionally passed 256 raster-stress rounds, 64 worker-stress rounds and the isolated
probe that proves real sanitizer instrumentation detects invalid memory access. Generated artifacts are not committed.

The positive analysis control compiles the annotated WIL-backed lock. The separate negative control fails with
C26130 specifically at `ConcurrencyAnalysisProbe::Unsafe`; its expected failure is not a solution failure.
Probe logs are retained under `.build/AnalysisProbe/x64/Release/`.

## Measurements

The Release `HostTests.exe --bench-executor` benchmark is separate from correctness gates. It fills 4096×4096
32-bit coordinate values in an existing 64 MiB buffer, using 256×256 tiles and a cancellation check per row.
An independent serial sum validates every output. Each executor has one warmup and 11 measured runs; p95 is the
nearest-rank 11th sample. Empty-tile dispatch has one warmup and 101 samples. All timed results exclude pool
initialization, buffer allocation and checksum validation. The caller uses the same foreground QoS as helpers.

Cancellation-check overhead and the maximum observed polling gap of the synthetic row body are separate runs.
Neither measurement qualifies a future image kernel's cancellation interval. Each migrated kernel must measure
its own interval before its phase exit.

Recorded on R1: AMD Ryzen 9 9950X3D, 16 physical / 32 logical processors, Windows 11 Pro 10.0.26200, Balanced power
scheme (`381b4222-f694-41f0-9685-ff5bb260df2e`), x64 Release, no concurrent repository build. Command:
`.build/x64/Release/HostTests.exe --bench-executor`; log: `.build/logs/workers-executor-benchmark.log`.

| Executor | Threads | Coordinate fill median / p95 (ms) | Empty tiles median / p95 (µs) |
| --- | ---: | ---: | ---: |
| Inline | 1 | 6.228 / 7.063 | < 0.1 / 0.1 |
| Pool | 1 | 6.225 / 8.206 | 0.1 / 0.1 |
| Pool | 2 | 3.449 / 4.169 | 0.4 / 3.2 |
| Pool | 4 | 2.872 / 4.189 | 0.3 / 0.6 |
| Pool | 8 | 2.581 / 2.852 | 0.4 / 1.2 |
| Pool | 16 | 2.293 / 2.785 | 0.7 / 2.5 |
| Pool | 32 | 2.360 / 2.580 | 1.0 / 7.5 |

Every result has checksum `99537471537152`. The 32-thread synthetic fill median is about 2.64× faster than inline.
The small 16-versus-32 median difference in this one ordered sweep does not justify changing the default pool
size; representative image kernels need their own comparison. Empty-tile measurements may be completed wholly
by the caller, so they describe the cancel-before-join fast path, not guaranteed worker dispatch latency.
The QPC frequency is 10 MHz; reported zero-duration samples are below its 0.1 µs resolution, not free operations.

One million cancellation checks averaged 0.280 ns each in the hot loop. The maximum observed polling gap in the
separate synthetic 256-pixel row run was 16.2 µs, below the 1 ms foundation criterion. These are observed Release
measurements, not deadlines guaranteed against OS preemption.

## Limits

- Actual image operations remain synchronous until P0.6/P1 connects them to the scheduler. This slice establishes
  execution and lifetime primitives; it makes no UI-latency, RAW, GPU/SIMD or Photoshop speed claim.
- Application Verifier is installed but cannot start under this unelevated process (Windows requires elevation).
  Its phase-closeout qualification remains pending. SAL and ASan do not replace a race detector.
- Native ARM64 execution is unavailable on this x64 host. Cross-build results, when recorded, prove compilation only.
- Optional helper allocation falls back to caller execution. Recoverable job allocation failure and memory admission
  belong to P0.6a/P0.7. No image-kernel exception boundary has been changed by this slice.
