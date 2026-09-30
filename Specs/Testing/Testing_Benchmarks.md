# Application and image benchmarks

Status: current normative diagnostic contract
Last reviewed: 2026-09-27
Owner: `RedPrism/ApplicationBenchmark.cpp`, `Common/BenchmarkMachine.*`, and `Build/bench/`

## Filter cancellation diagnostic

`HostTests.exe --bench-filter-cancel` measures 17 serial CPU cases on a deterministic 8192×5464 image: eleven
filters, histogram, antialiased/hard coverage, downscale, carried mask and private sparse copy. It runs one warmup
and ten observations each, using the production scheduler and cancellation token. An event marks worker entry;
the harness requests cancellation after an event wait of 20 ms. Completed-before-cancel and deadline races are
reported separately and excluded from the cancellation distribution. The source has opaque BGRA channels
`(17p,13p,7p) mod 256`, alpha 255, with a selected central third in both axes. Gaussian uses radius 250 and motion
uses distance 2000. The headless harness reserves a conservative 2 GiB for one job at a time.

Rows include median, p95 and maximum from stop request to kernel return. These durations include scheduling,
page faults and destruction of private scratch; they are **not** a direct measurement of adjacent cancellation
checks. Setup, scheduler admission and result drain are outside the interval. No timer or timing assertion belongs
to the ordinary correctness suite, and this diagnostic establishes neither UI responsiveness nor SIMD speed.

## UI application benchmark

`RedPrism.exe --bench ui|ui45|ui100|ui-smoke` is an opt-in measurement of application commands through the real
window, message, job-completion and frame loop. It uses the selected D3D device, runs quietly without prompts, and
prints `BENCH MACHINE`, one `UI BENCH` row per operation, and a final `UI BENCH REPORT` status. Every operation
must reach its expected state; a failed setup, command, frame, clock, or timeout fails the run.

Only this opt-in benchmark holds a continuous display/system execution request, restoring the thread's previous
request on exit. It uses no periodic keep-alive. A hidden, suspended, display-off or occluded performance run fails with
the observed host states. The bounded `ui-smoke` correctness harness alone may render with session display power
off, like the screenshot harness; it still fails when hidden, suspended or occluded. Its durations are never
performance evidence. A failed frame is rejected on the next outer-loop pass, before any retry can enter a sample;
the failure report includes the HRESULT, canvas HRESULT and failed renderer stage. The normal editor's
power/idle behavior is unchanged. See [SetThreadExecutionState](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-setthreadexecutionstate).

| Mode | Fixture | Recorded runs |
| --- | --- | --- |
| `ui` | 3840×2160, two raster layers, a mask, and an exposure adjustment layer | 10 after one warmup |
| `ui45` | 8192×5464, same layered fixture | 10 after one warmup |
| `ui100` | 10000×10000, one raster layer | 10 after one warmup |
| `ui-smoke` | 64×48, same layered fixture | 2 after one warmup |

The deterministic fixture checksum identifies generated pixel content; it is not a RAW corpus checksum. The
operation rows cover Zoom Actual and Zoom Fit view changes; opening Levels; updating Levels input gamma to 1.25 and
committing at that value; Hue/Saturation saturation +20; Exposure +0.5; Gaussian Blur radius 10; Add Noise amount
20; Grain amount 40; Lens Correction distortion 20; Invert; Fill with foreground; Clear Pixels; and Undo. Opening
Levels is valid when its histogram is ready for the captured opening image; identity settings intentionally have no
preview raster and do not require an unused downscaled source. Adjustment updates must produce a new preview raster. Zoom operations must change the actual view.
Clear Pixels is preceded by Select All during untimed preparation. At 100 MP, Gaussian Blur is instead the explicit
`gaussian-refused-pixel-budget` case: current document limits refuse its initial grow, and the harness verifies
refusal without a document or history change. It does not report that refusal as successful image processing.

Each timed operation begins after its fixture and command setup have idle jobs and a submitted frame. The outer interval starts immediately
before the command is posted and ends after worker jobs are idle and a subsequent frame has reached the renderer's
Present call. The invalidated-frame flag must also be clear: a completion can request a new frame after an earlier
one presented, and that earlier frame cannot stand for the completed result. A sample is accepted only after
verifying the command's result. It measures response through Present submission;
it does not measure display scanout or time until pixels are visible on the monitor. Nested UI timing rows report
message dispatch, window-message handling, completion draining, result application, UI actions, and frame work as
inclusive wall time on the owning thread.

Setup does not fence the GPU. The elapsed interval may therefore include waiting for GPU work submitted by the
preceding setup frame; this particularly affects WARP and the view-only rows. Dispatch and frame scopes exclude
the frame-latency wait. These rows are reproducible command scenarios, not isolated GPU kernel or blit timings.
GPU-completion instrumentation belongs to the asynchronous GPU phase before using these rows for a GPU latency gate.

Warmup is excluded. `elapsedMedianUs`, `elapsedP95Us`, and `elapsedMaxUs` summarize the per-operation outer
intervals. Histogram `*P99UpperUs` values are the upper bound of the bucket containing the nearest-rank p99;
`*MaxUs` values are the exact largest observed duration. Smoke uses the same behavior and output shape with two
recorded runs, but makes no timing assertion. Smoke verifies median ≤ p95 ≤ maximum and at least two total
presentations across its two recorded trials. Zero durations are legal after clock quantization. No benchmark
timing threshold is part of the ordinary test suite.

The machine row records available CPU, topology, memory, OS, power, adapter/driver, executable, ISA, worker-count,
architecture and configuration facts. Unavailable facts are printed as `unknown`; this collector does not identify
CCD count, so `ccd_count` remains `unknown` and is not inferred from L3 cache count. `fma3` reports the current x64 CRT
FMA3 math-library setting, not a processor capability claim (`n/a` on ARM64).

## RAW corpus preparation

`Tests/Fixtures/raw-corpus.json` is the reviewed manifest for small correctness inputs and near-45 MP RAW inputs. Run
`pwsh -File Build/bench/get-raw-corpus.ps1` to preview the selected files and sizes. Network access requires an
explicit `-Download`; the default selection includes `gating` and `correctness` files, with the supplemental Leica DNG included
only by `-IncludeSupplemental`. `-Offline` verifies existing files and refuses missing files before creating an
HTTP client or sending a request. Ordinary tests never fetch corpus files or require network access.

Every manifest entry carries original per-file CC0-1.0 evidence, source identity, expected size, and a supplied
SHA-256. Download mode requires an existing marked `X:\RedPrism.Perf` root on a fixed local drive; it never claims
an unmarked root. Existing files must match the manifest. New downloads are checked against size and SHA-256 before
they are committed without replacing a target. The manifest and checksum provide corpus identity; separate decode
and performance qualification is required before treating a file as a measured RAW result.

The required extension matrix is ARW, CR2, CR3, DNG, NEF, ORF, PEF, RAF and RW2, matching the active plan's v1 scope;
it does not promise every format understood by LibRaw. Small files cover compressed/lossless/uncompressed NEF,
compressed ARW, uncompressed RAF and the remaining camera containers. Layout identification still requires the
pinned decoder reference. The five `gating` files retain their performance roles; additional `correctness` files
do not change the Photoshop speed thresholds.

The reviewed CC0 legal text is checked in at `Tests/Fixtures/Licenses/CC0-1.0.txt`, with its original URL and
SHA-256 in the manifest. Successful `-Download` (including an all-present `-Offline` run) saves that exact text and
`raw-corpus-<SHA256-of-exact-manifest-bytes>.json` beside the images. These snapshots carry the original per-file
license rows, URLs and hashes. Evidence writes use the same pinned, no-replacement file policy as images; differing
existing evidence fails without overwriting it. Preview never creates evidence. Updated manifests create distinct
snapshots, allowing prior acquisition evidence to remain intact. No license fetch occurs in tests or offline mode.
