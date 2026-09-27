# Image pipeline foundations — 2026-09-27

Status: x64 implementation qualified; all three ARM64 configurations cross-compiled; native ARM64 runtime untested
Plan: [`UiNeverBlocks_2026-09-22.md`](../Plans/WIP/UiNeverBlocks_2026-09-22.md), P0.1 subset, P0.2 and P0.3

## Scope and machine

This record covers concurrent immutable raster reads and elimination of redundant full-image result buffers.
The CPU baseline is a subset of P0.1. It does not qualify UI responsiveness, SIMD, GPU scheduling, RAW or
performance against Photoshop. Those remain open in the plan's checklist.

- Host: Ryzen 9 9950X3D, 16 physical / 32 logical cores; 66,155,405,312 bytes of physical RAM reported by Windows.
- OS: Windows 11 Pro, 10.0.26200, x64. Power plan: Balanced (`381b4222-f694-41f0-9685-ff5bb260df2e`).
- GPUs installed: RTX 5080 (driver 32.0.16.1714), AMD Radeon Graphics (32.0.21036.18). These CPU benchmarks create
  neither a D3D device nor a window and do not measure GPU work.
- Build: Release x64, Visual Studio 2026 / MSVC v145; app version 0.1.77.0; pinned DxUi
  `c403052bad26c97ba23995e136672c32c85ccbfc`.
- Source: working-tree implementation based on `4ca2274`. The before binary already contains the new benchmark
  and atomic raster publication, with the original vector/copying filter wrappers. The after binary also
  converts those wrappers to `PixelBuffer` / `Raster::Adopt` and skips the redundant fill for contained reads.
- Paired comparison binary SHA-256: `fa7786b2a177b7c92b673b309a18239c008cccca7284352cd19b216a30917298`.

## Changes under test

1. The sparse assembly cache is written once under a per-storage mutex and published with release/acquire
   ordering. Pixel and metadata readers never inspect the vector while it is being built. Raster identity and
   returned pixel addresses remain stable.
2. Eight event-gated readers exercise assembly, region reads with padded rows, thumbnail generation and memory
   accounting. Gray8 and BGRA8888, one shared raster and separate rasters with a shared base are covered. Expected
   pixels come from independently calculated base/tile/fill values. Normal suites run eight rounds; ASan stress
   runs 256, with twelve iterations per reader. ASan is not a race detector.
3. `PixelBuffer` uniquely owns 64-byte-aligned CRT memory. Allocation is uninitialized and returns empty on
   failure. `Raster::Adopt` publishes only an exact-sized, fully initialized buffer without copying; invalid input
   keeps the caller's ownership. Tests cover alignment, move ownership, invalid sizes/formats, shared lifetime,
   contained region padding and adopted sparse bases.
4. Adjustment and filter wrappers, Gaussian output, resize outputs, WIC decoded frames and production composition
   readback now adopt their output. Existing numeric goldens cover their pixel semantics. WARP readback tests also
   compare the raster overload with the vector overload, including a 17,000-pixel-wide image and failure cleanup.

For a 8192×5464 BGRA result, one eliminated full buffer is 179,044,352 bytes (170.75 MiB). This is a calculation
from the buffer dimensions, not a process working-set measurement. Kernel arithmetic, sampling and seeds are
unchanged. Partial renderers still explicitly fill uncovered pixels; row padding remains untouched.

## CPU measurement method

`--bench kernels` uses 3840×2160; `--bench kernels45` uses 8192×5464. Synthetic input is premultiplied BGRA with
varying alpha. Each operation runs one untimed warmup and ten timed samples, except the 45 MP Gaussian which uses
three samples because the direct scalar kernel is slow. p95 is nearest rank (therefore the maximum with these
sample counts); it is descriptive, not a statistically qualified tail-latency gate.

Setup, resetting input and computing output checksums are outside each timer. Whole-output wrapper rows include
allocation, execution and publication, but exclude document history, host notifications, GPU upload and Present.
Each measured output must have the warmup checksum. Before and after checksums are also compared by operation.
Measurements run sequentially, without builds or tests at the same time. This is one local before/after run;
frequency, scheduling and cache noise still apply. Raw-kernel and allocation rows provide unchanged controls.

Logs: `.build/logs/kernel-{baseline,after}-{kernels,kernels45}.txt`, with matching stderr files. The small
`--bench kernels-smoke` runs the same thirteen operations at 32×24, with two samples, in every `test.ps1` gate;
it checks completion and output shape, never elapsed time.

## Results

All **26 before/after output checksums match** (thirteen operations at both sizes). The first separate timing
runs are retained below, but their ratios are **not accepted as optimization evidence**: unchanged scalar
controls also drifted substantially. During the after run, a three-second process sample showed another running
application consume 6.58 CPU seconds while the single-threaded benchmark consumed 3.05. No other process or
system power setting was changed. This establishes concurrent load, not a proof that load explains every timing
difference.

| Operation | 4K before median ms | 4K after median ms | 45 MP before median ms | 45 MP after median ms |
| --- | ---: | ---: | ---: | ---: |
| Levels scalar | 80.565 | 82.410 | 492.968 | 685.284 |
| Exposure scalar | 73.351 | 74.617 | 467.371 | 654.621 |
| Hue/Saturation scalar | 152.852 | 225.956 | 1016.366 | 1273.338 |
| Uniform noise scalar | 119.848 | 187.417 | 754.106 | 1016.696 |
| Histogram scalar | 44.973 | 74.024 | 327.653 | 451.905 |
| Levels output wrapper | 91.916 | 134.131 | 570.704 | 734.216 |
| Hue/Saturation output wrapper | 177.992 | 235.269 | 1039.808 | 1269.962 |
| Uniform noise output wrapper | 131.194 | 171.577 | 873.869 | 1020.087 |
| Gaussian sigma 10 | 4490.961 | 6106.380 | 29865.058 | 30322.046 |
| Raster zero / first touch | 2.767 | 3.113 | 14.828 | 14.316 |
| Raster copy | 3.243 | 3.715 | 21.844 | 20.564 |
| Vector zero / first touch | 2.847 | 3.249 | 14.987 | 14.192 |
| Vector copy | 3.279 | 3.876 | 22.454 | 20.132 |

### Alternating ownership comparison

To separate ownership costs from drift between binaries, `--bench buffers` and `--bench buffers45` run ten
pairs after a warmup pair. They alternate which variant runs first, on the same input in the same process. The
reference uses the old vector / `Raster::Make` result-copy strategy with today's `ReadRegion` fast path; the
production variant calls `ApplyAdjustment` or `RunFilter`. Thus the reference already benefits from the removed
fill pass and is conservative. HSV cube creation and the Levels tables are inside both variants' timers.
Every pair must match every earlier pair's checksum and the corresponding scalar-kernel checksum in the smoke
gate. Output validation, hashing and final raster destruction are outside each timer.

The runner assigns logical CPU 0 (`ProcessorAffinity = 1`) to each comparison process immediately after launch;
input generation and warmup are untimed. No affinity or priority of any other process changes. The report gives the
median of the per-pair saved milliseconds and speedup ratios, rather than deriving them from unrelated runs.
This is a synthetic kernel-result measurement, not the whole UI commit or a Photoshop comparison.

| Size / operation | Copy median ms | Adopt median ms | Median paired saving ms | Median paired speedup |
| --- | ---: | ---: | ---: | ---: |
| 4K Levels | 94.004 | 89.083 | 5.273 | 1.059× |
| 4K Hue/Saturation | 178.294 | 174.441 | 4.076 | 1.023× |
| 4K uniform noise | 137.601 | 131.542 | 6.127 | 1.047× |
| 45 MP Levels | 588.641 | 541.709 | 51.349 | 1.103× |
| 45 MP Hue/Saturation | 1002.180 | 994.146 | 8.876 | 1.009× |
| 45 MP uniform noise | 781.408 | 755.186 | 19.913 | 1.026× |

All six comparisons completed successfully with matching pixels throughout. Median paired differences/ratios
need not equal the differences/ratios of the two marginal medians. The small HSV delta is close to the likely
noise floor and does not support a broad throughput claim. The Levels saving and the eliminated 170.75 MiB buffer
support retaining adoption; the remaining compute-heavy kernels still need the planned parallel/SIMD work.
Logs: `.build/logs/kernel-paired-{buffers,buffers45}.txt`.

Reproduce the paired run after a Release build:

```powershell
foreach ($mode in 'buffers', 'buffers45') {
    $process = Start-Process .\.build\x64\Release\RedPrism.exe -ArgumentList '--bench', $mode `
        -WindowStyle Hidden -PassThru -RedirectStandardOutput ".build\logs\kernel-paired-$mode.txt" `
        -RedirectStandardError ".build\logs\kernel-paired-$mode.err.txt"
    $process.ProcessorAffinity = [IntPtr]1
    $process.WaitForExit()
    if ($process.ExitCode -ne 0) { throw "Benchmark failed: $mode" }
}
```

## Validation

| Gate | Result |
| --- | --- |
| `validate-skills.ps1` | PASS: 15 skills; required normal host access to the installed Python launcher |
| `format.ps1 -Check`, `git diff --check` | PASS |
| `test.ps1 -Platform x64 -Configuration Debug` | PASS: all four suites, WARP self-test, kernel smoke, crash/CRT probes, build contracts and nine harness captures |
| `test.ps1 -Platform x64 -Configuration Release` | PASS: all four suites, WARP self-test, kernel smoke, crash probe, build contracts and nine harness captures; debug CRT probe not applicable |
| `test.ps1 -Platform x64 -Configuration "ASan Debug"` | PASS: all four suites, 256-round concurrency stress, a real ASan detection probe, WARP self-test, kernel smoke, crash/CRT probes, build contracts and nine harness captures |
| `build.ps1 -Platform ARM64 -Configuration Debug` | PASS: solution and pinned dependencies |
| `build.ps1 -Platform ARM64 -Configuration Release` | PASS: solution and pinned dependencies |
| `build.ps1 -Platform ARM64 -Configuration "ASan Debug"` | PASS: solution and instrumented dependencies; native runtime not available on this host |

Logs live under `.build/logs/pipeline-*-tests.log`; self-test, smoke and diagnostic logs live beside each output
executable; captures are under `.build/screenshots/`. Generated products are not committed.

The final runs, including the paired smoke extension, are `pipeline-Debug-tests-final.log`,
`pipeline-release-tests-final.log` and `pipeline-ASan-Debug-tests-final.log`. ARM64 build logs are
`pipeline-ARM64-{Debug,Release,ASan-Debug}-build.log`. Native ARM64 timing, race stress and WARP execution remain
unqualified; cross-compilation is not runtime evidence.

## Review and remaining limits

Cheaper agents handled the initial checklist, benchmark implementation, CLI integration and smoke gate. The
primary agent reviewed their changes, implemented publication and buffer ownership, and ran the gates. A further
read-only review found no uninitialized output or byte-count regression in the converted paths.

The review confirmed two existing allocation-failure paths: `CopyFrame` is `noexcept` and storage/thumbnail
allocation can terminate; `CommitColorEdit` can reach the host's `noexcept` boundary through storage, thumbnail or
history allocation. This matches the current fatal `bad_alloc` policy. Pixel-buffer allocation failure returns
empty, but storage-header and other STL allocations are not universally recoverable. The scheduled job boundaries
and transaction work in P0.5/P0.6a and P1 must resolve this before the reliability exit. No claim of general
allocation-failure recovery is made here.

The aligned CRT allocator is used at all sizes so ASan tracks allocations normally. A separate `VirtualAlloc`
allocator and recycle pool remain deferred until measurements justify their extra lifetime and poisoning rules.
GPU readback is still synchronous. Heavy commands still run on the UI thread. P0.4 generation tracking, worker
scheduling, cancellation, SIMD and asynchronous GPU work remain outstanding.
