# Application and image benchmarks

Status: current normative diagnostic contract
Last reviewed: 2026-10-09
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
uses distance 2000. The headless harness runs one job at a time: non-sizing cases reserve a conservative 2 GiB;
size cases reserve their `SizingMemoryBytes` result and refuse a zero or greater-than-2-GiB graph.

Rows include median, p95 and maximum from the QPC sampled immediately before the accepted stop request to
the original job's `BodyExited` observation after `Run` returns or `bad_alloc` unwinds. This includes scheduling,
page faults and destruction of Run-local scratch; it excludes later worker retirement and owner apply/drop.
It is **not** a direct measurement of adjacent cancellation checks, external native/GPU acknowledgement or UI
Escape/visible feedback. Setup, scheduler admission and result drain are outside the interval. The observer is
borrowed through joined settlement/Close and matches the exact original ticket; it performs only QPC capture
and event signalling. A failed clock or invalid cancellation admission fails the diagnostic.

The report includes the exact method row
`FILTER CANCEL METHOD clock=QPC returnBoundary=job-body-exited warmup=1 samplesPerCase=10 excludes=worker-retirement,owner-apply`
and the shared `BENCH MACHINE` formatter with `harness=filter-cancel`, scalar ISA and the actual scheduler worker
count. Machine collection, scalar initialization and formatting occur outside timed intervals. The 20 result
rows retain fixed order, 8192×5464 dimensions, 20 ms requested delay, one warmup and ten observations per case.
Measured, early and overlap counts sum to ten; cancelled sizing results are empty and retain the unchanged source.
No timer or timing assertion belongs to the ordinary correctness suite. This diagnostic establishes neither UI
responsiveness nor SIMD speed; old pre-return measurements cannot qualify this returned-body boundary.

## UI Escape cancellation diagnostic

The opt-in `ui-cancel`, `ui-cancel45`, `ui-cancel100` and `ui-cancel-smoke` modes reuse the respective
UI fixtures and trial counts. `ui-cancel-prepared` and its `45`, `100` and `-smoke` variants require the
explicit prepared display/row route. Ordinary editor activation remains separate. The ten ordered cases
are Levels gamma 1.25, HSV saturation +20, Exposure +0.5, Curves with points (0,0)/(128,160)/(255,255),
reversed Gradient Map, Grain 40, Gaussian radius 250, Motion distance 2000, Noise 20 and Lens distortion 20.
The complete P1 operation roster, including content-aware fill and size/selection/tools, remains due.

Gaussian 250 and Motion 2000 switch Preview off through the actual checkbox before setting the extreme value,
outside the timed Commit interval. Their full-commit cancellation rows report `previewEnabled=0`; the other
eight cases keep Preview on and report `previewEnabled=1`. This avoids running the unrelated expanded preview
kernel during setup. These two cases do not qualify extreme-preview latency or restoration from changed
preview pixels. Their original source/history and accepted Cancel card still require exact restoration.
The prepared setup requires a current pending publication only with Preview on. With Preview off, it requires
the current committed publication and no pending graph selected for rendering; row identity and settled
ownership remain required in either case.

Preparation settles previews and presents the current panel before posting actual OK. The diagnostic binds
the exact admitted original ticket and selects its second compute Run; the first is the existing admission/
capture stub. A bounded write-once log retains immutable worker-entry/return notices. Its manual-reset event
joins the existing message/frame waits; the owner resets before reading at most 32 records. An unpublished
reservation hole is never skipped or rearmed into a busy loop. No callback reads Application or scheduler
state. The event/log are created before scheduler initialization and survive its joined Close. Ownership
transfers to the benchmark state without moving the borrowed object/event. Ordinary startup constructs neither.

One native timer requests Escape 20 ms after the observed useful entry, with clock rounding upward.
OS timer granularity, owner scheduling and queued-message delay are retained as actual entry-to-post and
post-to-dispatch intervals; this is not a guaranteed 20 ms delivery deadline. The timer is disarmed before
posting real WM_KEYDOWN/WM_KEYUP. Monotonic timer identities reject stale messages without affecting a newer
trial. Actual key dispatch stamps cancellation of the original ticket separately from admission of its
ordered Cancel receipt. Failed admission cannot be reported as hidden-card success. Unexpected keyboard
input messages or mutating pointer input fail the diagnostic; no physical modifier state is overwritten.
Bounded correctness smoke modes own a `KeyboardState::ScopedSynthetic` keyboard for the complete
application message loop, as the hidden self-test does. Typing modifiers in another application cannot
change their command semantics. Real posted Escape down/up messages, original-ticket cancellation,
unexpected-message checks and all cancellation assertions still execute. The scope restores the prior
keyboard state on every exit. Full performance modes retain the physical modifier-state refusal;
smoke results never establish performance qualification.

WM_TIMER is a wake-up rather than deadline authority. If it wakes before the original QPC deadline, disarm
that identity and admit a fresh one-shot requesting at least USER_TIMER_MINIMUM (10 ms) for the remaining interval.
At most two such re-arms are permitted; `deadlineRearms` reports 0–2. Exhaustion, clock failure or timer
admission failure fails the whole diagnostic. Escape is posted only at or after the original 20 ms QPC
deadline. This bounded event-driven correction neither polls nor changes the minimum delay or recorded
posting/dispatch timestamps. No early wake is falsely counted as a cancellation sample.

Rows report actual dispatch, receipt admission, accepted-stop-to-original-BodyExited, first restored-source
non-occluded Present and final original settlement separately. The selected render argument must be the
current original committed source, with unchanged document/history/target authority and hidden card; a
foreign equivalent document address or stale prepared publication cannot qualify a frame. Present submission
is not monitor scanout. BodyExited includes Run-local cleanup but grants no native/GPU acknowledgement.
A separate first-true-check notice from the selected useful Run is required to describe worker check latency;
BodyExited MUST NOT substitute. Active rows append exactly seven fields after the existing `g1=0`:
`checkKnown`, `checkOutcome`, `acceptedToCheckUs`, `checkLatencyKnown`, `acceptancePresentOrdinal`,
`sourcePresentOrdinal`, `firstFrameProved`. Allowed check outcomes are `unobserved`, `before-stop`,
`stop-overlap` and `after-acceptance`. Unknown, before-stop and overlap checks report zero duration and unknown
latency; an after-acceptance check requires known original raw body observation and duration no greater than
accepted-stop-to-body-return. A known zero microsecond duration remains valid clock quantization. Clock failures
or malformed notice order fail the diagnostic rather than inventing unknown success.

First-frame proof requires exact successful-visible ordinal `acceptancePresentOrdinal + 1` with unchanged
original held authority/raster/history, hidden card and committed source selected and successfully drawn in the actual render call.
A chrome-only successful Present after a canvas failure or skipped blit permanently refuses first-frame proof.
No overflow is allowed; a qualified source ordinal is nonzero. An unproved row reports source ordinal zero,
and `sourcePresent` equals `firstFrameProved`. Measured active rows require proof; a first stale frame permanently
refuses even if a later frame restores the source. The parser uses exact UInt64/decimal ordinal arithmetic;
large ordinals must not pass through lossy double or signed duration conversions. Refusal rows stay unchanged.
Check-boundary and first-frame controls establish causal evidence only; selected-tier check spans, ordinary
activation, first-frame latency and G1 still require actual affected throughput and owner measurements.

Only complete original owner/worker/GPU settlement permits fixture replacement or receipt retirement.
Published immutable row caches retain their completed memory-charge ticket while displayed. That identity
remains owned and charged; pending row preparation has a separate ticket. Settlement waits for pending work,
and current publication validates the row key without requiring its live cache ownership to disappear.

Each admitted Commit trial emits `UI CANCEL`; warmup is trial 0 and never measured. Early completion before Escape, completion
before the stop call and finish overlap are separate unmeasured outcomes. Worker failure and missing useful
work remain unmeasured; raw worker status/error and original OK/Cancel receipt outcomes/errors are retained
with explicit known flags. Command/settings budget refusal is never successful image processing. A returned-
after-acceptance measurement requires valid causal clocks, an accepted original cancellation, successful
Cancel acknowledgement, unchanged source/history, and an actual restored-source Present. No elapsed-time
threshold is asserted by a correctness test. Every output explicitly retains `ordinaryActivation=0`/`g1=0`.

The full 45 MP and 100 MP fixtures retain D7's 100 M aggregate image-pixel budget. Gaussian 250 and Motion
2000 exceed it: the 45 MP layered fixture refuses the extreme settings after opening; the 100 MP fixture
refuses the opening's positive blur growth. These contexts emit a distinct `UI CANCEL REFUSAL` row with
`stage=settings` or `stage=opening`, exactly one original budget notice, unchanged source/history,
`commitWorkerAdmitted=0`, and `kernelMeasured=0 cancelMeasured=0 ownerCostMeasured=0`. Default opening,
preview, histogram and display preparation may have run before a settings refusal; they must be settled.
Refusal is neither a cancellation nor a speed measurement. Its owner/notice cost remains due in the general
UI benchmark. No refusal row carries invented durations, receipts or a useful-body ticket.

Full output has 110 ordered contexts (ten cases, one warmup and ten recorded trials), followed by the exact
success report. At 4K all 110 are admitted-Commit rows. At 45/100 MP there are 88 admitted-Commit rows and
22 declared refusal rows: 80 active and 20 refusal recorded contexts, plus eight active and two refusal
warmups. The full reader reports refusal counts separately, excludes them from all latency distributions,
and preserves every recorded active owner-cost sample even when its worker finishes before Escape.
Missing, undeclared, wrong-stage or falsely measured refusals fail the entire reader. An empty measured
kernel distribution has count zero, an empty values array and null statistics; it never means zero latency.
The prepared refusal also requires a current committed publication, no pending render graph, current rows,
joined owners and a subsequent actual non-occluded Present. The reader alone grants no native qualification.

The bounded smoke gate executes 30 rows (one warmup and two trials for all ten operations) on both real
WARP direct/prepared routes. It requires exact ordered inventories, typed clocks/results, correct warmup
exclusion, receipt/source/card outcomes and final report. It permits truthful early completion without
requiring a measured cancellation count; it rejects refusal, worker failure, missing work or malformed output.
The hidden actual-HWND dispatch fixture independently checks original-ticket cancellation, accepted/refused
card visibility and joined history/source lifetime at an event-held worker boundary. It measures neither
visible presentation nor worker-return time. Capture/reducer/transport/CLI controls use independent state
expectations and real event-gated publication, never candidate timings as expected values. Qualified Release
measurements and original R1/W A/B/remaining-host requirements must still pass before G1 or P1 Done.

## UI application benchmark

`ui-prepared|ui-prepared45|ui-prepared100|ui-prepared-smoke` selects the explicit experimental prepared
display and row route on the corresponding fixture. These modes require exact current committed/pending
publication keys and current rows after all owner pumps settle; an older visible-opening fallback cannot
complete a sample. The row reports `UI BENCH ROUTE display=prepared rows=prepared asyncStroke=1 asyncWarp=1
ordinaryActivation=0`. The direct diagnostic reports its direct route with the same two tools disabled.
Prepared modes use the shared responsive initializer; rows, display and all three async tool flags activate together
only after successful acquisition. Partial startup refuses without a synchronous fallback. Normal editor activation is a separate milestone. These 27 cases do not cover every P1 operation or establish G1.

The prepared sampler must release its transient ticket/barrier/utility ownership and return to its settled
prepared-cache memory charge captured before the operation. The direct route still requires zero reserved
memory. This is an exact unchanged-cache check, not a larger leak allowance. The full smoke gate executes
both direct and prepared rosters; all pixel/history/readback/receipt and presentation expectations remain
required. Timings from either 64×48 smoke route are correctness diagnostics only.


`RedPrism.exe --bench ui|ui45|ui100|ui-smoke` is an opt-in measurement of application commands through the real
window, message, job-completion and frame loop. It uses the selected D3D device, runs quietly without prompts, and
prints `BENCH MACHINE`, one `UI BENCH` row per operation, and a final `UI BENCH REPORT` status. Every operation
must reach its expected state; a failed setup, command, frame, clock, or timeout fails the run.

Every `BENCH MACHINE` row for a UI benchmark MUST identify the actual window monitor by its device name, and MUST report that monitor's configured target refresh as the exact rational numerator/denominator returned by the display configuration query. This is configured mode metadata; it is not measured scanout or Present cadence. Do not substitute another output's refresh, infer a nominal integer rate, or treat unavailable metadata as known.

A full performance run MUST capture its display authority after setup and before measured operations. It MUST refuse performance qualification if monitor identity, device name, or rational refresh is unknown, or if window/display continuity is invalidated during the run. Continuity invalidation is monotonic through the run; later return to the original display does not restore qualification. The final report MUST distinguish a continuity/authority refusal from successful performance measurement. Correctness smoke MAY complete when display authority is unknown or changes, but MUST label its report `smokeCorrectnessOnly=1` and expose whether authority was known and continuity changed. Smoke durations are never performance evidence.

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

`ui-sampler`, `ui-sampler45`, `ui-sampler100` and `ui-sampler-smoke` use those respective fixture sizes and run
counts through the same production application loop, but report only `sampler-accepted-prefix` and
`operations=1`. They avoid executing the preceding 26 unrelated operations. The focused smoke mode has the same
display-power exception as `ui-smoke`; the performance modes retain every presentation guard.

Focused modes additionally print exactly two `UI BENCH BOUNDARY` rows: `initial-display` captures owner timing
from the application loop's initialization through the first prepared/presented fixture; `excluded-warmup`
captures the first successful sampler trial. Both report the existing owner histogram p99 upper bounds and
maxima, observed frame count and clock failures. They are separate from the ten-trial distribution. Initial
display timing excludes fixture pixel construction before `Run` and does not isolate GPU upload time or scanout.
Keep both rows when evaluating first-use cost; the warmed row alone cannot qualify H4 or cold G1. Smoke verifies
both boundaries observe frames and no clock failures, without timing thresholds.

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
document/history required. The smoke gate covers all 27 operation rows, including `project-save`, which measures
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

`sampler-accepted-prefix` drives the actual accepted Down, Move and end producers at exact document coordinates
(0.5,0.5), (1.5,0.5), (0.5,0.5). Fit-zoom client integer rounding is excluded from the coordinate fixture; source
capture, admission, repeated 1x1 GPU readback, checked palette publication, aggregate retirement and final frame
remain inside the real measured message/job loop. The end seals accepted points and adds no release sample.
The row requires a successful original receipt, exactly three GPU copies, unchanged document serial/history/
modified state/raster identity and no retained sampler ticket, barrier, reservation or utility surface. Its
independent final-pixel oracle is gray 55 for layered A: masked gray 29 over gray 71 with coverage 128 rounds to
50, and linear-sRGB exposure +0.25 rounds that to 55. Ceiling B contains only opaque gray 29. Smoke requires six
point copies over its two recorded trials. These correctness conditions do not establish the original P1 G1
timing gates; use qualified Release A/B hardware/WARP measurements and retain the full raw timing rows.

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
`*MaxUs` values are the exact largest observed duration. Recorded operation rows and the sampler's two excluded
boundary rows also expose `actionP99UpperUs` from their existing `UiAction` histograms. The completion report
retains its existing shape; no separate aggregate histogram is maintained.

`invalidationToPresentSamples`, `invalidationToPresentP99UpperUs` and `invalidationToPresentMaxUs` describe
capture-local first invalidation through the next actual non-occluded Present. Repeated invalidations preserve
the first timestamp. This includes owner scheduling, wait for presentation pacing and the frame; `frame*` still
reports frame duration separately. The hooks are passive: disabled captures read no clock, own no timer/event,
and alter no scheduling. Only a real Present-count increment closes the interval. Failed or occluded presentation
abandons it without recording a fake zero, exposed in `abandonedFrameIntervals`; invalid clocks increment
`clockFailures`. Existing settled trial resets exclude setup/warmup and their pending interval. Nested captures
have independent interval state. These observations do not assert display scan-out timing, Escape acceptance,
OS stop-to-return, ordinary prepared-route activation or G1.

Smoke uses the same behavior and output shape with two recorded runs, but makes no timing assertion. It requires
these fields, at least two completed invalidation intervals per recorded operation, valid percentile bucket
boundaries (which may exceed the exact maximum in the same finite bucket), and zero clock/abandonment diagnostics. Smoke verifies median ≤ p95 ≤ maximum and at least two total
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


### First-display resource stage diagnostics

UI benchmark reports additionally emit six `UI RESOURCE` rows per recorded operation and per focused
`initial-display`/`excluded-warmup` boundary: texture-create, shader-view-create, mip-generation, raster-upload,
composition-record and chrome-sync. Each row includes sample count, p99 upper bound, maximum wall microseconds
and `inclusive=1`. Zero samples means no observed call, never a measured zero-duration operation.

Stages use the fixed thread-local histograms and optional verbose ETW keyword 0x4. Without an active capture
or provider they perform no clock read, allocation or wakeup. Resource calls and nested composition/chrome
scopes measure inclusive owner wall time; never sum nested durations or call command-recording time GPU
completion/scanout. Chrome timing remains captured for all calls; ETW suppresses chrome-sync at or below
4000 microseconds to avoid a per-frame resource event. Existing UI BENCH rows and their gates remain unchanged.

Real WARP tests assert first-compose resource samples, exact opaque premultiplied pixels, source-resident
reuse after invalidation and zero resource/recomposition samples for a settled document. A separate capture
isolates diagnostic readback staging creation. Real private ETL decoding verifies literal stage ids 6–11,
payload types/values, owner thread, keyword and worker/chrome suppression. Tests never assert elapsed times.
Actual largest-fixture Release/ETW measurements and fixes remain necessary for H4/G1; instrumentation alone
cannot qualify first display or an accelerated pipeline.


### Selected Size final-publication diagnostic

The opt-in ui-size-prepared, ui-size-prepared45 and ui-size-prepared100 modes use the existing 4K,45MP and
100MP fixtures with prepared display/rows and original Size final publication. ui-size-prepared-smoke uses
64x48, one excluded warmup and two scored samples. Only canvas-size-fill, image-size-nearest and
image-size-resolution run, in that order. Canvas Size uses the
`canvas-size-fill-refused-pixel-budget` operation for both the 45MP and 100MP fixtures because adding the requested coloured extension
would exceed the aggregate 100 M image-pixel budget. Each is an expected refusal and is never counted as a published resize. Other benchmark and
ordinary startup routes
retain their explicit activation policy.

Each accepted Size sample requires exactly one successful original-job final publication; an expected refusal
requires zero. The counter advances from actual completed original publication after CPU/history, Fit, current
Chrome rows, GPU adoption and worker retirement. UI SIZE PUBLICATION records report scored recorded/expected
counts and warmupExcluded=1. A later retry or legacy fallback cannot satisfy that witness.

Each focused operation separately reports initial-display and excluded-warmup timing boundaries and the six
resource stages. These costs are outside the scored distribution but remain present in raw output and must be reviewed for owner overruns. Excluding setup or warmup from scored statistics does not waive their cost or
qualify them as free; frame and drain maxima and initial-display observations remain visible evidence. Size smoke
requires six unique operation/boundary pairs, all36 resource rows,
completed frame/latency observations, valid percentile buckets and zero clock/abandonment failures, plus its
three exact two-publication records. The sampler keeps its existing two boundary records. Smoke is correctness
only, including its display-power exception; full modes retain presentation/input guards. Qualified Release
measurements and the complete P1 G1 matrix remain necessary before performance or ordinary activation claims.

The benchmark route marker `ordinaryActivation=0` still denotes the complete experimental prepared
display/stroke route. Independent ordinary CPU cursor activation does not activate that complete route.

## Optional Size phase attribution

`REDPRISM_UI_TIMING_ATTRIBUTION=1` enables bounded diagnostic attribution for the selected Size UI
benchmark modes. It records at most 32 owner intervals of at least 1,000 microseconds per boundary,
with QPC wall time, available thread-cycle deltas and static source/function/line identity. Initial
display, excluded warmup and scored samples retain separate reports; overflow and clock failures are
reported explicitly. Renderer phases and command-list execution/completion registration use the same
opt-in sink. Disabled scopes read no clock and allocate nothing; enabling attribution adds no timer
or wakeup source. Rows marked `diagnostic=1` locate costs and do not qualify G1. Performance evidence
requires a separately qualified run with attribution disabled.

## Optional layer-row Prepare attribution

The layer-row benchmark also accepts `REDPRISM_UI_TIMING_ATTRIBUTION=1`. It retains the original initial
and rename timing rows and reports bounded owner phase records (32 slots, minimum 1,000 microseconds).
Chrome Prepare detail IDs are 3000 (tool visuals/layout), 3001 (DxUi Prepare), 3002 (accessibility attach)
and 3003 (accessibility placement publication). QPC and available thread-cycle deltas identify measured
intervals. Public embedded statistics before/after the original Prepare boundary show surface/cache changes;
reading/reporting those statistics occurs outside that boundary. No samples are discarded.

Overflow and clock failures are explicit. Disabled scopes read no clock and allocate nothing; attribution
adds no timer, polling or wakeup. Rows marked `diagnostic=1` locate costs and establish no G1 qualification.
Performance evidence requires a separate qualified run with attribution disabled. This hidden embedded-STA
provider harness establishes neither visible frame latency nor external UI Automation RPC latency.
