# Application and image benchmarks

Status: current normative diagnostic contract
Last reviewed: 2026-10-03
Owner: `RedPrism/ApplicationBenchmark.cpp`, `Common/BenchmarkMachine.*`, and `Build/bench/`

## Immutable record primitive comparison

`DocumentTests.exe --bench-record-roots` compares the immutable record primitive with `vector<ImageLayer>` for
1, 100 and 10,000 records. Each record has a unique deterministic id, a 512-character name, a deterministic
transform and a held shared one-pixel BGRA raster. Fixture documents pass the ordinary document validator.
Full source/result content, layer order, names, placement and raster identity are checked outside timed blocks.
The same middle-record opacity change (0.375) is used for private Clone/Replace/Seal and vector copy/replace.

Five rounds alternate root/vector order. Each timed iteration includes construction, a fixed first/middle/last
record sample and final destruction; it never scans all names or all 10,000 records to consume a timed result.
Root captures use 65,536 iterations at each size. Vector captures use 1,024/256/16; both mutation paths use
256/64/8. Raw rows retain operation, path, record count, round, iterations, elapsed nanoseconds and checksum.
Different iteration counts MUST be normalized per operation before comparison. Checksum sums use defined
unsigned arithmetic and must match the independent expected sample multiplied by the iteration count.

Use a qualified Release image for cost evidence, retain its exact EXE/PDB/source binding and all raw rows, and
record machine/configuration context. Timing thresholds are not functional test gates. These measurements
include the consumer and destruction costs and do not isolate a bare shared-pointer increment. They qualify
neither migrated `CanvasDocument` capture/mutation nor G1, RAW or Photoshop performance.

## Filter cancellation diagnostic

`HostTests.exe --bench-filter-cancel` measures 20 serial CPU cases on a deterministic 8192×5464 image: eleven
filters, histogram, antialiased/hard coverage, downscale, carried mask, private sparse copy and three size jobs.
The size cases grow a single-layer canvas by 128 pixels per dimension with a red extension at anchor 8, or resize
the image to three quarters of each dimension using nearest and high sampling. They use `SizingMemoryBytes`,
check detached cancelled results are empty, and verify the original document remains unchanged. It runs one warmup
and ten observations each, using the production scheduler and cancellation token. An event marks worker entry;
the harness requests cancellation after an event wait of 20 ms. Entry does not identify which inner loop or
allocation was active at the stop request. Completed-before-cancel and deadline races are
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

The deterministic fixture checksum identifies generated pixel content; it is not a RAW corpus checksum.

The panel readiness check accepts either a changed CPU preview raster or a newer published GPU lookup sequence, then
requires the frame invalidated by that completion to be presented before recording the result. A GPU lookup is
never used as evidence that CPU commit pixels already exist.

The operation rows cover Zoom Actual and Zoom Fit view changes; opening Levels; updating Levels input gamma to 1.25 and
committing at that value; Hue/Saturation saturation +20; Exposure +0.5; Gaussian Blur radius 10; Add Noise amount
20; Grain amount 40; Lens Correction distortion 20; Invert; Fill with foreground; Clear Pixels; Undo; Canvas Size
with a foreground-colored extension by 16 pixels on each side dimension at bottom-right anchor; Image Size nearest
at half each dimension; Image Size resolution-only at 300 ppi; project save; PNG export; and JPEG export to an
exclusively owned temporary directory. Save requires a successful publication receipt, the captured path/revision marked saved, unchanged
image storage and a presented completion frame. Pixel/schema content is independently verified by the host and
IO suites, outside timed UI work. Subsequent save trials replace the same file atomically; cleanup follows worker
join and removes only the three known harness-owned results and empty directory. Export rows include utility
readback, detached encoding/publication, owner completion and a presented frame. They require successful receipts
and unchanged document path, revision, modified state and active raster identity. Output content is checked by
host/codec tests; no synchronous output decode or file read enters timed UI verification. Size cases require the expected dimensions,
resolution and named history entry after asynchronous completion, with no open size card. Opening
Levels is valid when its histogram is ready for the captured opening image; identity settings intentionally have no
preview raster and do not require an unused downscaled source. Adjustment updates must produce a new preview raster. Zoom operations must change the actual view.
Clear Pixels is preceded by Select All during untimed preparation. At 100 MP, Gaussian Blur is instead the explicit
`gaussian-refused-pixel-budget` case: current document limits refuse its initial grow, and the harness verifies
refusal without a document or history change. It does not report that refusal as successful image processing.
The colored Canvas Size extension is likewise `canvas-size-fill-refused-pixel-budget` wherever the fixture's
existing layers plus the requested extension exceed the pixel ceiling (the current 45 MP and 100 MP fixtures), with the unchanged
document/history required. The smoke gate covers all 26 operation rows, including `project-save`, which measures
the production project-save continuation through worker completion and a presented frame, plus `png-export`
and `jpeg-export` through their production graph. The saved result must
reach its expected state. This row is a harness measurement only; no hardware timing result or threshold follows
from smoke. Historical 18-row and 15-row reports remain evidence of their recorded version.

`gradient-preview-coalesced` drives repeated endpoint changes through the Application gradient drag producer while an
interactive preview is active. The latest metadata sequence must be the one published, while the committed layer
raster and history remain unchanged. Its independent endpoint oracle reads one pixel from each already prepared
preview tile image through `ReadRegion`; it does not assemble pixels or take a snapshot on the owner. `gradient-apply-ordered` invokes the Gradient header's `Apply` action, which
queues `HostGradientApplyInput` through the production host continuation admission path, and accepts the sample only
after its receipt succeeds, the pending gradient is
gone, and one `Gradient` history entry replaces the source raster. Its independent pixel oracle reads the first and
last pixels on the gradient row: opaque foreground BGRA `(39,100,147,255)` and white background BGRA
`(255,255,255,255)`. These operations use the existing dispatch, message, completion-drain, result-apply, UI-action,
frame, and outer elapsed metrics. Smoke timings remain correctness-only; no timing threshold is introduced.

`Build/bench/run-gradient-bench.ps1` requires an immutable qualified Release snapshot, exact executable/runtime
hashes and a matching `ui-gradient-ops-v1` capability declaration before launching the six hardware/WARP runs.
Each run rechecks those files, preserves unique output logs, and retains the full benchmark presentation guard.
`GradientBenchmarkRunnerTests.ps1` uses small invalid executable fixtures to prove refusals without running the
product. Expected refusal exits do not leak into the ordinary suite's success status. Neither these controls nor
the capability declaration alone qualifies performance.

Each timed operation begins after its fixture and command setup have idle jobs and a submitted frame. The outer interval starts immediately
before the command is posted and ends after worker jobs are idle and a subsequent frame has reached the renderer's
Present call. The invalidated-frame flag must also be clear: a completion can request a new frame after an earlier
one presented, and that earlier frame cannot stand for the completed result. A sample is accepted only after
verifying the command's result. It measures response through Present submission;
it does not measure display scanout or time until pixels are visible on the monitor. Nested UI timing rows report
message dispatch, window-message handling, completion draining, result application, UI actions, and frame work as
inclusive wall time on the owning thread.

Each row also reports GPU readback copies, nonblocking Map attempts, additional readiness barriers and terminal
busy failures summed over recorded trials (warmup excluded). A successful graph has one Map per copy plus at
most one extra Map/barrier per copy. These counters distinguish actual driver recovery from injected correctness
tests; they do not measure GPU time or guarantee a driver's completion deadline.

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

## File IO foundation measurements

`IoTests.exe --bench-file <mode> <width> <height> [layers]` explicitly runs one operation per process. Modes are
`png-file`, `png-memory`, `zip-save` and `folder-save`; invalid arguments return 2. Dimensions/aggregate image
pixels/layer count obey the current document limits. The adopted opaque noise fixture has a fixed coordinate
hash. Save layers have separate identities and share source pixels, so this measures asset encoding/storage
without claiming a multiple-distinct-raster working set. Normal IoTests execution does not run these timings.

`FILE BENCH` reports only the timed encode/save operation, private bytes before/after, process lifetime peak
working set through that operation, and output bytes. Peak working set includes fixture/setup; private before
and after are not peak private commit. Disk modes include their checked durability and publication work;
memory mode retains encoded bytes but performs no durable write. Compare memory retention with these scope
differences explicit. Output dimensions, persistent document/layer identity/order and three independently
computed opaque pixel samples per layer are verified after timing/memory capture. No timing threshold is an
ordinary correctness assertion. Use repeated serial Release processes and record machine/source identity.

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
