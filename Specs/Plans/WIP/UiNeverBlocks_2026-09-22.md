# Image processing never blocks the UI, runs at hardware speed, and opens camera RAW faster than Photoshop

## Implementation checklist

- [ ] P0.1 — Measure the baseline and establish performance instruments. CPU benchmarks and paired ownership measurements recorded; UI/ETW/IO coverage remains open.
  - [x] Add the scalar and whole-output CPU benchmark with checksums and a bounded smoke gate.
  - [x] Capture the 4K and 45 MP before measurements on R1.
  - [ ] Add the remaining UI, ETW, IO and operation-classification measurements.
  - [x] Add private ETW capture/decode and the marked local-drive perf-root resolver for the P0.7 free gate.
- [x] P0.2 — Make raster materialization thread-safe and stress-test races. Full x64 Debug, Release and ASan gates passed.
  - [x] Publish immutable pixel pointers with acquire/release ordering and serialize first assembly.
  - [x] Add independent byte expectations, padded reads, thumbnail checks and shared-base concurrency coverage.
  - [x] Pass the extended 256-round stress and full ASan gate.
- [x] P0.3 — Add `PixelBuffer`, `TryAllocate` and `Raster::Adopt`; remove result copies. Pixel equality and paired result-production timings verified; full x64 gates passed.
  - [x] Implement aligned ownership and no-copy raster adoption with ownership and size tests.
  - [x] Convert adjustment/filter/resize, WIC decode and production composition readback outputs.
  - [x] Preserve legacy numeric goldens in the complete Debug and Release gates.
  - [x] Compare all 26 before/after checksums and record alternating ownership timings with matching pixels.
  - [x] Finish ASan qualification.
- [x] P0.4 — Add document generations and tokens; route mutations through validated paths. Full x64 gates and all ARM64 cross-builds passed.
  - [x] Close mutable document/layer access; cover committed changes and changes inside open edits.
  - [x] Add session document serials and owning result tokens with stale-result checks.
  - [x] Share immutable selection loops across snapshots without changing selection goldens.
  - [x] Mark the revision actually saved, including a save completed after another edit.
  - [x] Migrate imports, opacity previews and test/benchmark fixtures; run the full validation matrix.
- [x] P0.5 — Build the worker pool, parallel execution, cancellation and worker crash handling. Full x64 gates and all ARM64 cross-builds passed.
  - [x] Implement the private worker and wait pools, cancellation, tile execution, COM lifetime and QoS. Full x64 matrix passed.
  - [x] Prove saturated-pool, nested execution, cancellation and shutdown behavior with independent state-machine tests. Extended ASan stress passed.
  - [x] Extend crash handling for competing worker faults and the worker probe. Both child probes passed in all x64 configurations.
  - [x] Add concurrency annotations and the analysis build, including guarded/unguarded compiler controls.
  - [x] Complete the final Debug, Release, ASan stress and native-analysis regression gates.
  - [x] Record executor overhead, cancel-check cost and remaining qualification limits. Synthetic row polling gap: 16.2 µs on R1.
- [x] P0.6a — Implement the job scheduler, completion pump, settle path and shutdown order. Full x64 gates, native analysis and all ARM64 cross-builds passed.
  - [x] Define the bounded scheduler, intrusive completion channel and seven job classes.
  - [x] Wire the shared pump and event waits into the host, including hidden-window completion priority.
  - [x] Verify cancellation, FIFO, progress, modal reentrancy, failed jobs and all-exit shutdown with event gates.
  - [x] Pass the configuration matrix and update normative contracts for the completed slice.
- [x] P0.6b — Implement preview mailboxes, queued input, stroke chaining and busy state. Full x64 gates, native analysis and all ARM64 cross-builds passed.
  - [x] Implement the one-active/latest-pending preview mailbox and ordered eight-command queue.
  - [x] Implement the cross-class history barrier and retained stroke-replay states.
  - [x] Wire scheduler activity into the one-shot busy deadline and localized DxUi progress control.
  - [x] Verify state machines, document replay, observer lifetime, DPI layout and event-driven activity; complete all configuration gates.
  - [x] Update normative contracts and the validation record for the completed slice.
- [x] P0.7 — Add memory admission, deferred release and justified recycling. Full x64 gates, native analysis and all ARM64 cross-builds passed.
  - [x] Add the pure admission policy and whole-reservation accounting with overflow-safe boundaries.
  - [x] Measure fresh-buffer page-touch and free costs against reuse before deciding the recycling policy. Three confirming 10-pair runs: fresh touch 16.64–17.51 ms versus reused 6.51–6.73 ms; free 6.02–6.36 ms. Implement a bounded recycle pool.
  - [x] Gate dispatch on whole-graph reservations and retry from release/pressure events. Debug event/state tests pass, including an empty-frame-drain no-polling regression.
  - [x] Transfer large buffer/storage destruction to a non-cancellable worker release queue; prove teardown lifetime. Debug tests and actual ETW positive controls pass.
  - [x] Trace and stress admission, allocation failure and deferred frees; complete the validation matrix. 307 checks in Release, actual ETW byte counts and sanitizer poison checks pass.
- [ ] P0.8 — Add scalar kernel dispatch and the x64 VEX scan.
- [ ] P1 — Move blocking operations off the UI thread and meet the G1 exit criteria.
- [ ] P2 — Add exact SIMD and threaded CPU kernels; meet the G2 exit criteria.
- [ ] P3 — Add sliced GPU work, previews and tiled residency; meet the phase exit criteria.
- [ ] P4 — Implement RAW v1 decode, develop, panel and measurements.
- [ ] P5 — Optimize RAW performance to the Photoshop targets.
- [ ] P6 — Implement non-destructive develop, high-bit-depth documents and revised pixel budgets in a detailed child plan.
- [ ] Final validation closeout — Update owning specs and docs, complete the closeout matrix, and move this plan to Done.

Implementation started 2026-09-27. Slices P0.2–P0.7 are complete on x64; the P0 phase remains open. ARM64
Debug, Release and ASan Debug cross-builds passed. Native ARM64 execution is unavailable on this host.

Execution scope, 2026-09-27: the user explicitly authorized implementation through P6, with this opening checklist
updated during work and each completed, validated slice committed and pushed. This supersedes the former P6
authorization restriction. Record concrete decisions in §11 as their phases begin, and expand P6 into its child
plan before coding the format/depth transition. Continue between slices without another implementation approval.
Hardware qualification and Photoshop comparisons remain measured exit criteria; implementation alone cannot
check those boxes. Smaller agents handle bounded changes and independent checks; the primary reviews them and
owns scheduler, GPU, storage, and RAW architecture.

Progress, 2026-09-27: the user authorized implementation. Cheaper agents handled the initial checklist, scalar
benchmark, CLI integration, smoke gate and bounded reviews. The primary agent reviewed their work, implemented
raster publication, buffer ownership and adoption, the concurrency tests and the paired benchmark, and ran the gates.
P0.2 uses a standard per-storage mutex to keep `Document.h` independent of Windows types; only a first assembly
takes it. All readers use the acquire/release pointer from §4.6. The new stress covers competing readers of one
sparse storage and two sparse storages sharing a base. P0.1 measurement coverage remains open. The P0.3 buffer
and adoption APIs, filter/resize/WIC conversions and production composition readback preserve kernel arithmetic.
All 26 before/after checksums match. Alternating ownership comparisons showed a median paired saving of 51.349 ms
for a 45 MP Levels result, with one fewer 170.75 MiB pixel buffer. This measures result production; full host
commit latency, responsiveness and the G2/G3 targets remain open. See the
[foundation record](../../TestRuns/PipelineFoundations_2026-09-27.md) for controls, noise and qualification limits.

P0.4 progress, 2026-09-27: session writes now pass through tracked APIs, result tokens retain their inputs and
reject stale/session-mismatched results, selections share immutable geometry, and history can mark a captured
saved revision. All x64 gates and ARM64 cross-builds passed, including 120 new checks. The
[P0.4 record](../../TestRuns/SessionGenerations_2026-09-27.md) documents the reviewed fixes and remaining limits.

P0.5 progress, 2026-09-27: worker/wait ownership, nested tile execution, cancellation and competing-fault crash
handling are implemented. Full Release native analysis and its positive/negative controls passed. The checked
synthetic fill takes 2.360 ms at 32 threads versus 6.228 ms inline; this is an executor measurement, with no claim
about real image operations or Photoshop. The full x64 Debug, Release and ASan gates passed, including worker
stress, both crash probes and 27 harness screenshots. All three ARM64 cross-builds passed. See the
[worker record](../../TestRuns/WorkerExecution_2026-09-27.md) for controls, metrics and remaining qualification.

P0.6a progress, 2026-09-27: the bounded scheduler and shared message/completion/action pump now serve the host,
including hidden-window completion delivery and shutdown on every mode exit. The complete x64 matrix, native
analysis with its compiler controls, and all ARM64 cross-builds passed. Release synthetic scheduler dispatch
through completion and payload release measured 19.3 µs median and 53.4 µs p95. The
[scheduler record](../../TestRuns/JobScheduler_2026-09-27.md) gives the state-based tests, measurement scope and
qualification limits. Image commands remain synchronous until their P1 migrations; G1 is still open.
This completed slice was committed and pushed to `origin/main` as `048b1cd`.

P0.6b progress, 2026-09-27: preview coalescing, queued command ownership, shared history/stroke ordering and
the delayed busy indicator passed 269 new policy/ordering checks plus the full x64 gates. The WARP self-test covers
production activity notifications, exact visibility boundaries, 96/144 DPI and held chrome clicks. Native analysis
and all ARM64 cross-builds passed. See the [input-policy record](../../TestRuns/JobInputPolicies_2026-09-27.md).
P1 still owns actual tool snapshot/worker/apply migration, stroke sample storage and file-flow wiring; these
foundation gates do not claim that image processing is already nonblocking.
This slice was committed and pushed to `origin/main` as `3bdc87a`.

P0.7 progress, 2026-09-27: whole-graph admission, event-only retries, large deferred releases and bounded recycling
passed the full x64 matrix, native analysis and all ARM64 cross-builds. The actual ETW stream has two positive
controls and no UI free of 16 MiB or more for direct/adopted/vector releases. Recycled 45 MP buffers measure
6.669 ms full touch and 7.95 µs UI ownership retirement (enqueue only), with matching checksums and zero warm
page faults. The [memory record](../../TestRuns/MemoryPipeline_2026-09-27.md) gives controls, after measurements,
review corrections and qualification limits. P1 still owns every operation's declared peak and async migration.

Code drift checked against the current source: the later review already added display proxies and tiled
full-resolution readback for images beyond the texture-size limit, EXIF orientation and ICC import, atomic
export, and the duplicate-layer pixel-budget refusal. The corresponding later slices extend these paths;
they must not reintroduce the older limitations described in the original survey.

Validation progress: the complete x64 Debug, Release and ASan gates, skill validation and formatting check passed.
ASan includes the extended 256-round concurrency stress and a real detection probe. The kernel smoke gate runs
13 operations and three copy/adopt comparisons on 32×24 pixels in every configuration. The review confirmed the existing fatal
`std::bad_alloc` policy still reaches `CopyFrame` and `CommitColorEdit` through allocation of storage headers,
thumbnails and history. P0.3 removes redundant large allocations; it does not introduce the recoverable job
boundary. P0.5/P0.6a and P1.1/P1.7 must replace those failure paths before G1/reliability closes.

- **Status**: ACTIVE. P0.2–P0.7 are complete on x64; all ARM64 cross-builds passed. D2, D3 and D8 are recorded; P0.8 is next.
  P0.1 and P0.8 remain executable without an open decision.
  The open decisions in §11 gate the other slices:

  | Decisions | Gate |
  | --- | --- |
  | D8 | P0.5–P0.7 (the AGENTS golden amendment lands in P0.5) |
  | D2, D3 | P0.6b |
  | D1, D2, D3, D12 | P1 |
  | D1, D12 | P2, P3 |
  | D1, D5, D6 (v1), D8, D14, D15 | P4 |
  | D4, D9, D11, D13, D16, D17 | The targets and records they name |
  | D6 (v2), D7, D14 | P6 |

  A decision is recorded in §11's "Decided" column, with the date, before its first gated slice starts.
- **Priority**: P0
- **Planned at**: 2026-09-22, after the splitter drag fix. Rewritten 2026-09-23 from a code survey of every
  kernel, the host command path, the composition, file I/O, tests, the legacy oracle and external research. It was
  then revised twice. The first revision used 89 findings from an adversarial review in seven lenses, each
  checked by an independent verifier. The second used a recheck of every accepted finding plus an
  internal-consistency pass. The 2026-09-22 text (one worker, move the blocking commands) is superseded. Its
  rule, STOP list and Done line survive in §3, §13 and §14.
- **Owning specs**: `Core/Core_PerformanceAndResources.md`, `Core/Core_Architecture.md`, `Core/Core_CrashHandling.md`,
  `Document/Document_Model.md`, `Document/Document_History.md`, `Document/Document_Tools.md`,
  `Rendering/Rendering_Composition.md`, `Rendering/Rendering_Brush.md`, `IO/IO_ImageCodecs.md`,
  `IO/IO_ProjectFormat.md`, `Build/Build_Toolchain.md`, `Build/Build_Packaging.md`, `Testing/Testing_Validation.md`,
  `UI/UI_ApplicationChrome.md`, `UI/UI_Windowing.md`, `Specs/README.md` and `AGENTS.md`. New specs:
  `IO/IO_RawDecode.md` and `Rendering/Rendering_RawDevelop.md` (P4).
- **Depends on**: nothing outside this plan. The pinned DxUi (`c403052`) already ships `ProgressBar`
  (determinate and indeterminate).

This file is a non-normative execution record. A rule written here as MUST becomes normative only when the slice
that lands it moves the text into the owning spec (§9).

## 1. Goals and how they are measured

**G1 — The UI thread never waits.** It pumps messages, applies results that are already finished, records GPU work
in bounded slices, and presents. Nothing RedPrism computes, reads, writes, encodes or reads back runs to
completion on it, unless §5 classifies it S (measured ≤ 4 ms p99 on the largest supported input). The only other
exceptions:

- Windows-owned modal loops (`IFileDialog`, `TaskDialogIndirect`, `MessageBoxW`, native menus) run from a UI
  action.
- The bounded `WM_ENDSESSION` wait for a running save (§4.4).
- `Shutdown` after the window is hidden (§4.3).

**G2 — Every kernel runs at hardware speed.** Kernels use SIMD on x64 and ARM64, all cores, and the D3D11 GPU
wherever that is the fastest correct engine. Speed never changes a committed byte that a legacy golden fixes.

**G3 — Camera RAW opens and develops faster than Photoshop.** On the same machine and the same file, RAW opens and
develops faster than Adobe Photoshop with Camera Raw: M2 and M4 in §1.2. The claim is made only from a recorded
same-machine measurement (§7).

### 1.1 Reference machines

| Id | Machine | Role |
| --- | --- | --- |
| R1 | Ryzen 9 9950X3D (16C/32T, AVX-512), RTX 5080, 120 Hz, Windows 11 26200: the brush-bench host (`Specs/TestRuns/Brush_2026-09-21.md`) | Phase-exit measurements for G1, G2, G3 |
| R2 | A hybrid laptop whose panel is driven by an integrated GPU (Core Ultra or Ryzen AI class) | Recorded at the P1, P3 and P4 exits (D9); the D10 cross-adapter measurement |
| R3 | Snapdragon X (ARM64, Adreno X1) | Recorded on native ARM64 at the P1, P3 and P4 exits (D9) |
| W | WARP on R1 | Correctness floor. `--self-test` asserts G1's structural invariants here (a message dispatches while a gated job runs, the result applies exactly once, Escape leaves no entry). G1 timing on W is measured by `--bench ui` in Release and recorded; it is never a `test.ps1` assertion |

"Gate" in this plan means a phase-exit criterion checked against a recorded Release measurement under
`Specs/TestRuns/`. Tests never assert wall-clock time (test-harness rule).

### 1.2 Targets

The "today" figures are estimates from reading the code, except the brush row, which was measured. P0.1 measures all
of them before any change. A missed target is recorded with its reason in the phase's `Specs/TestRuns/` record; it
is never silently dropped.

**Fixtures.** All of these fit the 100 M-pixel budget.

- **G1 document A**: 45 MP (8192×5464), 2 full-size image layers (89.5 M image pixels), one full-size mask and one
  adjustment layer.
- **G1 document B**: 10 000 × 10 000, single layer (the 100 M ceiling).
- **Panorama**: 30 000 × 3 000, from P3.3 on, for costs that follow side length.
- **Merge fixture**: 20 layers of 2048×2048 on a 45 MP canvas (83.9 M pixels).

**Reliability (G1).**

| Metric | Target | Phase exit |
| --- | --- | --- |
| UI-thread time per dispatched message, per `DrainCompletions`, per UI action, per frame record (CPU) | p99 ≤ 4 ms, max ≤ 16 ms for every command in §5, excluding the G1 exceptions and device create/recover | R1 and W at P1 exit with documents A and B, for every §5 row except the brush update and the distort preview, which gate at P3 exit. If P0.1 shows B does not compose on W before P3.3, B on W gates at P3 exit |
| Apply of a finished result | ≤ 2 ms on the UI thread (handle swaps, history push); no pixel work at apply | R1 and W at P1 exit |
| Frames while jobs run | An invalidated frame presents within 2 refresh intervals while any job runs | R1 at P1 exit with A and B. W at P3 exit with a 4K document (see below) |
| GPU work per presented frame from background jobs | ≤ 25 % of the refresh interval (2 ms at 120 Hz, 4 ms at 60 Hz), timestamp queries | R1 at P3 exit (the slicer arrives in P3.1) |
| Escape or Cancel during a job | Document restored in the next frame. Cancellable kernels stop within one cancel interval (≤ 1 ms at the active tier). The non-cancellable steps listed in §4.3 finish their current call | R1 at P1 exit |
| Memory | No job starts without reserving its declared peak (§4.6). An allocation that still fails completes the job as `Failed(OutOfMemory)`: a notice appears, nothing is recorded, and the process does not terminate | Stress test at P0.7 exit |

W targets for the frame row, with a 4K document: frames that recompose no document pixels (chrome, pan, zoom) or
only touched tiles present within 2 refresh intervals. Frames that recompose a whole layer present within the
P0.1-measured WARP compose time of that document plus one refresh interval. 45 MP on W is recorded only.

**Speed (G2) on R1, Release x64, 45 MP BGRA8 layer.**

| Operation | Today (est., single thread, UI thread) | Target preview | Target commit |
| --- | --- | --- | --- |
| Levels, Curves, Exposure, Gradient Map | 0.25–0.6 s per slider tick; 1–2 s OK | ≤ 1 frame (per-layer GPU adjust draw) | ≤ 50 ms, exact |
| Hue/Saturation | 0.4 s per tick; ~2 s OK | ≤ 1 frame | ≤ 80 ms, exact |
| Gaussian blur σ = 10 | 0.66 s per tick (2048 px preview); 11–22 s OK | ≤ 1 frame at view resolution | ≤ 250 ms, exact |
| Gaussian blur σ = 250 | ~13 s per tick; ~13 min OK | ≤ 2 frames | ≤ 600 ms (needs D4) |
| Motion blur, 96 samples | ~3 s per tick; ~50 s OK | ≤ 2 frames | ≤ 1 s, exact |
| Add Noise (Gaussian) / Grain | 3.9 s / 2.2 s per tick at full size | ≤ 1 frame | ≤ 400 ms / ≤ 150 ms, exact |
| Lens correction | 90 ms per tick; 1.7 s OK | ≤ 1 frame | ≤ 150 ms, exact |
| Content-aware fill, hole = 1 % of 45 MP | 7–9 s, run twice (preview + OK) | runs once | ≤ 4 s off-thread, exact |
| Invert / Fill / Clear, whole layer | 0.85–1.7 s | — | ≤ 60 ms, exact |
| Gradient drag | 1.1–1.8 s per pointer move | ≤ 1 frame | ≤ 100 ms, exact |
| Shape tool mouse-up (45 MP shape) | ~0.8 s | draft outline | ≤ 60 ms, exact |
| Blur / Clone / Liquify stroke start | 0.9 s – 65 s at pointer-down | next frame | — |
| Brush 800 px, 4000×4000 document (measured 2026-09-21): update 11.9 ms median alone, mouse-up ≈ 21–28 ms. A 45 MP row is measured in P0.1 | — | ≤ 4 ms UI, preview on GPU (P3) | mouse-up UI ≤ 2 ms |
| Distort apply / preview with a linked mask | 1.4–2.3 s / 1.1 s per move | ≤ 1 frame (GPU projective pass, P3) | ≤ 250 ms, exact |
| Image Size 45 MP → 3840 wide, High (3×3 bilinear samples per channel) | 2–3 s per layer | — | ≤ 200 ms per layer, exact |
| Merge Down / Merge Layers / Merge Group, merge fixture | 120–700 ms | — | ≤ 150 ms total, UI ≤ 2 ms |
| Eyedropper / wand "all layers" press | 60–120 ms full readback | — | 1×1 or ROI readback, UI ≤ 1 ms |
| Open 45 MP JPEG | 0.5–0.9 s blocking | first pixels ≤ 150 ms | full layer ≤ 400 ms |
| Save 45 MP one-layer `.prism` | 5–15 s blocking | — | UI ≤ 2 ms; background ≤ ½ today (≤ 1.5 s with D11) |
| Copy to system clipboard | 2.5–6.5 s blocking | — | UI ≤ 2 ms |

**RAW (G3) on R1.** One ~45 MP file per format class: Sony lossless ARW, Canon CR3, Nikon lossless NEF, Fuji
compressed RAF, tiled DNG. M1, M2 and M4 gate on warm opens (the second open, as Puget measures); cold opens are
recorded. M2 and M4 gate on the pair measured with sharpening 0 and noise reduction 0 in both applications (§7).

| Metric (§7 defines each) | P4 target | P5 target |
| --- | --- | --- |
| M1 open → first pixels on screen (embedded preview) | ≤ 150 ms | ≤ 100 ms |
| M2 open → full-quality developed, editable layer (bake included) | ≤ Photoshop for CR3 and RAF. ARW, DNG and NEF recorded (LibRaw decodes them sequentially) | ≤ 0.5 × Photoshop for all five classes |
| M3 develop slider → presented frame | ≤ 1 frame at fit; visible region at full resolution ≤ 60 ms after the pointer settles | same |
| M4 open with Puget's Camera Raw tone and presence values → developed layer (Photoshop reads them from an XMP sidecar) | ≤ Photoshop for CR3 and RAF; the others recorded | ≤ 0.5 × Photoshop for all five classes |
| M5 export the developed file to JPEG, matched quality, 4:4:4 | recorded | ≤ Photoshop |

For scale, Photoshop takes 1.00–1.01 s for "File Open – RAW" of a 36 MP D810 NEF in Puget Bench for Photoshop 1.0
(Photoshop 27.6–27.10). That is an average over mixed systems; §7 replaces it with same-machine numbers.

## 2. Starting inventory and implementation deltas

The survey below explains the work from the pre-implementation code. Current completion is tracked in the
opening checklist and the owning specs. The 2026-09-27 deltas include atomic raster publication, adopted result
buffers, CPU benchmark modes and the already-landed review fixes noted above. Function names for the unified
`ColorEdit` path are updated here; proposed job APIs in later sections are still future work.

These facts come from reading the code, with anchors from the working tree at `0035f1b`. Line numbers in
`Application.cpp` drift because of uncommitted edits, so the slices cite function names.

- **No image kernel uses threads or SIMD.** The only concurrency today:
  - COM reference counting (`AccessibilityRoot`);
  - the crash handler's interlocked guard (`CrashDumps.cpp`);
  - the harness-only free-threaded capture pool (`WindowCapture`).

  Kernels are scalar: double for blur, resample, paint and histogram; float for tables, cube, noise and grain. x64
  builds at the SSE2 default under `/fp:precise`, ARM64 at armv8.0. Release uses `/GL` for the app only. Each test
  project compiles its own hand-picked subset of `Common/*.cpp`; CompositionTests also compiles
  `RedPrism/CanvasRenderer.cpp` and `BrushCompute.cpp`. The static MSVC STL already contains runtime-dispatched
  AVX2 code (`vector_algorithms`).
- **The message loop** (`Application::Run`) drains with an inline `PeekMessageW` loop, asks
  `SelectHostFrameAction`, then blocks in `MsgWaitForMultipleObjectsEx(QS_ALLINPUT, MWMO_INPUTAVAILABLE)` on the
  settings watch, plus the frame-latency waitable before a frame. The UI thread is COM STA (`Main.cpp`). The
  self-test and the bench never enter `Run`: they call `OnCommand` and `Renderer::Render` directly and assert on the
  next line.
- **Every image operation is synchronous on the UI thread**, including each preview tick, every commit, stroke
  starts and finishes, readbacks, open/import/save/export, clipboard, merge, resize, shapes and first uploads. The
  inventory is in §5.
- **Kernel style**:
  - per-pixel `std::lround`, per-pixel div/mod, and `std::function` calls in `BrushStroke::PaintCanvas`;
  - the Gaussian blur is an O(σ) direct convolution with a 16 B/px float intermediate (`ImageFilters::GaussianBlur`);
  - panel previews re-run the CPU kernel per tick at up to 4096 px, or at full size for noise, grain and fill
    (`AdjustEdit.cpp` preview limits);
  - a new raster identity per tick forces a new mipped texture and a full `UpdateSubresource`
    (`CanvasRenderer::UploadRaster`).
- **Every result is copied twice.** `Raster::Make` copies its input, and there is no adopting constructor. Kernel
  wrappers value-initialize a vector, `ReadRegion` memsets and blits, then `Make` copies again.
- **`Raster::Data()` becomes a data race as soon as a second thread reads a raster.** A sparse raster assembles
  itself lazily into `mutable std::vector bytes` / `mutable bool materialized` with no synchronization.
  `ReadRegion` reads the same fields non-atomically, for the raster and for its base. Save calls `Data()`
  (`EncodeToFormat`), and the assembled copy is then kept forever. `DuplicateActiveLayer` inserts a copy without
  checking the 100 M-pixel budget.
- **GPU**:
  - There is one device, created with `BGRA_SUPPORT` and without `SINGLETHREADED`, so creating resources is
    free-threaded. DxUi uses a single-threaded D2D factory on the same immediate context, so all submission stays
    on the UI thread.
  - Nothing uses fences, queries, `DO_NOT_WAIT`, a capability probe, a VRAM budget or tiling.
  - Readback creates a staging texture per call and blocks in `Map(READ)` (`CanvasRenderer::Readback`). The
    brush blocks in `Map(READ)` per tile per pointer update (`GpuBrushCoverage::Render`).
  - Canvas and stack surfaces are document-sized BGRA8 with one mip level. A layer above 16384 px, or above the
    per-resource guarantee (min(max(128 MB, 25 % of dedicated VRAM), 2 GB)), fails `CreateTexture2D`; Compose
    then fails and the window closes.
  - The device is rebuilt synchronously inside `Render` (Present reports the device lost), in `ProbeOcclusion`,
    and in `CheckDeviceAdapter` on `WM_DPICHANGED`, `WM_EXITSIZEMOVE` and `WM_DISPLAYCHANGE`.
  - On WARP the brush runs the compute engine; the WARP self-test requires it.
- **Adjustment layers already run on the GPU** as pass 6/7 of `CompositionPixel.hlsl`. That pass adjusts the canvas
  composite beneath the adjustment layer, not one layer. It has no selection-coverage input, and it keys grain in
  document space, while destructive Grain keys it in layer pixels (`Adjust::RunFilter`). So pass 6 cannot serve
  directly as a destructive panel preview.
- **Mask thumbnails carry semantics.** `MaskEdgeBackground(thumbnail)` decides what a placed mask shows beyond its
  pixels; a missing thumbnail reveals. Compose, `CommitColorEdit`, strokes and distort all read it synchronously.
- **I/O**:
  - WIC only; 16-bit input is truncated to 8; there is no EXIF orientation, no ICC and no RAW (the container
    whitelist in `ImageCodecs::DecodeFromDecoder`).
  - ZIP saves already write `dest.tmp` then call `ReplaceFileW`. Folder projects use fixed staging and rotation
    names. Neither flushes.
  - PNGs are encoded one after another, then libzip re-deflates each at level 9, because
    `zip_set_file_compression` is never called.
  - Export truncates the target with `CREATE_ALWAYS`. `MarkSaved()` marks whatever revision is current.
  - The WIC JPEG encoder is given only `ImageQuality`, so it encodes 4:2:0.
- **History** copies whole `CanvasDocument` values: 3 per `End`, about 4 per `Undo`. The copies include selection
  loops of up to 8 M edges, which the 256 MiB cap does not count. The limit is an `int`. `Trim` recomputes
  `RetainedBytes` from scratch once per dropped entry.
- **Synchronous contracts that async work must preserve**:
  - `ConfirmDiscardChanges` calls `SettleInteractions`, which commits the transform, stroke, warp, gradient and
    panel, and reads `history.IsModified()` on the next line.
  - `OnCommand` settles the transform, then runs the command.
  - Undo drops a live stroke.
  - `CommitStroke` refuses a stroke whose source image changed.
- **Staleness guards before the job split**: pixel commits check image identity. The shared `CommitColorEdit` and image
  `CommitStroke` also check the transform. Mask identity is checked only for mask strokes. `MergeLayers` checks
  only that the ids exist. `CanvasDocument::id` persists across save, load and resize.
- **Tests**:
  - `DocumentTests` calls some kernels directly and many through the synchronous `DocumentSession` API
    (`CommitColorEdit`, `MergeLayers`). It writes fixtures through the mutable `Document()` at dozens
    of sites.
  - `levels.histogram-ready` and `adjustlayer.levels-histogram` assert that the histogram is ready synchronously.
  - `--self-test` asserts on the next line.
  - `HostTests` has no window and compiles no `RedPrism/*.cpp`.
  - GPU parity runs on WARP only.
  - P0.1 has added `kernels`, `kernels45`, `buffers`, `buffers45` and `kernels-smoke` beside `brush`.
    `--crash-probe` takes a directory.
  - The fail-closed sandbox `X:\RedPrism.Perf` is specified but not implemented.
  - There is no CI.
- **The crash handler is single-threaded.** A second thread that faults while a dump is being written returns
  `EXCEPTION_EXECUTE_HANDLER` and ends the process mid-dump. `std::set_terminate` is per-thread in the UCRT and is
  installed only on the UI thread.
- **Legacy oracle**:
  - The Swift app ran panel previews and commits in `Task.detached`. It kept one render in flight and one pending
    slot holding the latest settings; its authors recorded that cancelling on each slider change starved the
    preview (`HueSaturation.swift`, `Filters.swift`).
  - Commit disabled the panel, kept the preview visible, and applied one entry only if the layer's image identity
    and transform were unchanged.
  - A busy indicator appeared after 250 ms, and edits were blocked while the project was busy.
  - I/O ran on serial actors.
  - It had no RAW support and no depth above 8 bits, so it is no oracle for G3.

  Three legacy tests that specify async behavior were never ported: `LevelsTests.stalePreviewCannotReturnAfterOffOrReopen`,
  `ProjectTests.projectOperationsBlockEditsAndQueueImageImports` and `ImageImportTests.queuedImportsAreNotLost`.
  `SelectionEditTests.quickOperationsNeverDimTheInterface` is timing-based and becomes a pure function.

## 3. Principles

These become normative text in the owning specs (§9).

1. **UI-thread duties.** The UI thread pumps messages, validates and applies finished results, runs UI actions,
   records GPU work in bounded slices, and presents. An operation may stay synchronous only if its p99 UI time is
   ≤ 4 ms, measured on the largest supported input: G1 document B, plus, from P3.3, the panorama for costs that
   follow side length. P0.1 measures this; §5 records the result.
2. **Snapshots, not locks.** A job's input is a value snapshot of immutable handles (`Raster`, `LayerTransform`,
   settings, seed, a shared selection handle), taken on the UI thread. Jobs never touch `DocumentSession`, history,
   the composition or chrome. Results are new immutable values.
3. **Apply or drop.** A result is applied on the UI thread as one history entry, or dropped, after its token is
   validated (§4.4). There are no partial applies.
4. **Latest wins, without starving.** A settings change never cancels an interactive preview. One job stays in
   flight and one pending slot holds the latest settings, as in the legacy app. Only refinements are superseded by
   a newer tick (§4.2).
5. **Nothing the user did is lost.** Input that arrives while earlier work is pending is either chained (strokes)
   or queued and replayed in order (§4.4). It is never silently dropped.
6. **Exact means exact.** A kernel that a legacy golden fixes produces the same bytes on every ISA tier, thread
   count and tile size as its retained scalar reference. GPU engines serve previews within named tolerances. RAW
   develop is new behavior and commits within tolerances named in its own spec (D1).
7. **One device, UI-thread submission, sliced and fenced.** GPU work is recorded only on the UI thread's immediate
   context, in slices that fit the per-frame budget. Completion arrives through fences as events. Nothing maps
   before its fence has signaled.
8. **Nothing polls.** Workers block on queues; the UI thread blocks on the message queue plus handles. GPU and
   system events arrive through thread-pool waits. There are no `DO_NOT_WAIT` loops, no `GetData` spins, no OpenMP
   spin-waits, no periodic timers, no unbounded retry timers, and no re-armed level-triggered waits.
9. **Bounded everything.** Every job reserves its declared peak memory before it starts. Every queue has a fixed
   depth or uses intrusive nodes. Staging rings, scratch pools and caches have byte caps and respond to
   memory-pressure and VRAM-budget events.
10. **Cancel is cheap and safe.** Kernels check a cancel token once per cancel interval: a row, or k rows, sized to
    ≤ 1 ms at the kernel's measured per-pixel cost on the active tier. A cancel check never changes the order of
    floating-point work.
11. **Measure first, keep only wins.** Each speed slice records before/after numbers under `Specs/TestRuns/` on the
    same configuration. A step that is not faster is reverted (the `PortProgress` rule), unless the cost is
    intentional, measured and justified in its record.

## 4. Architecture

### 4.1 Threads and owners

```text
UI thread (STA)                                private pool "RedPrism.Workers" (N threads)
  Application::Run                               lanes are scheduler queues; pool callbacks pop from them
   ├─ PumpMessages()        (Common/Jobs)         ├─ Interactive (HIGH)   previews, refinements, samples, sample copies
   ├─ DrainCompletions()    (Common/Jobs) ◄─┐     ├─ Stroke     (HIGH, serial, chained)   stroke/warp/heal finish, dabs
   ├─ RunUiActions()        (one per pass)  │     ├─ Selection  (HIGH, serial)            wand, load, expand, booleans
   ├─ SelectHostFrameAction                 │     ├─ Commit     (NORMAL, FIFO)            panel OK, raster edits, merge,
   ├─ Renderer::Render / Present            │     │                                       resize, shapes, settle
   ├─ GpuWorkQueue::Pump()  (after Present) │     ├─ Io         (NORMAL, per-path serial) open, import, save, export,
   └─ MsgWaitForMultipleObjectsEx           │     │                                       clipboard, RAW
        [jobWake, settingsWatch, frameLatency]    └─ Background (LOW, EcoQoS)             thumbnails, caches; deferred
                      ▲                                                                   frees at NORMAL
                      │                        one-thread pool "RedPrism.Waits": fence events, device removed,
                      │                        VRAM budget, memory low/high (edge-triggered), end-session waits
                      └── SetEvent(jobWake) ◄── completion SLIST (two intrusive nodes per job; push never blocks)
```

- **`JobScheduler`** (`Common/Jobs.{h,cpp}`) is owned by `Application`. It holds the lane queues of job
  records, the completion list, the manual-reset cancel event and the `jobWake` auto-reset event. The same host
  owns stable per-target preview mailboxes, the history barrier, stroke recordings and the continuation queue
  (§4.4); workers never access these UI-owned state machines.
  - Pool callbacks carry no payload. They pop the next record from the scheduler's lane queue, so a callback
    cancelled before it starts leaks nothing, and the scheduler frees queued records itself.
  - Its Win32 members sit behind a pimpl, so `Jobs.h` has only fixed-width types.
  - `PumpMessages()`, `DrainCompletions(JobScheduler&, ApplyFn)` and `RunUiActions` are free functions in
    `Common/Jobs`, so `Application::Run`, `SettleJobs`, the bench and the HostTests gate test all run the same
    code. They also fill the UI stall histogram (§4.9).
- **`Executor`, `TileGrid`, `CancelToken`, `ParallelFor`** live in `Common/Executor.h`, which is standard C++ only,
  so kernel headers never include Windows types.
- **`WorkerPool`** is `Common/WorkerPool.{h,cpp}`, a Win32 header with local WIL `unique_any` aliases for
  `PTP_POOL` and `PTP_CLEANUP_GROUP`.
  - It is a private pool: `CreateThreadpool` with min = max = N, a cleanup group, and `TP_CALLBACK_PRIORITY_*` per
    lane.
  - N defaults to the logical processor count (32 on R1); P0.1 measures it against the physical-core count.
  - Kernels take an `Executor&`, so `DocumentTests` can pass an inline executor or a pool of 1, 2 or N threads.
  - `std::execution::par`, OpenMP and oneTBB are not used: each is a second scheduler, blocks its caller or
    spin-waits.
- **`RedPrism.Waits`** is a dedicated one-thread pool for thread-pool waits (`CreateThreadpoolWait`). Waits never
  occupy a worker, so a full `RedPrism.Workers` cannot block the fence or memory event that would free it.
- **`GpuWorkQueue`** (new, `RedPrism/GpuWork.{h,cpp}`, P1.3) is UI-thread only and owned by `Application` next to
  `CanvasRenderer`. It borrows the device. It owns fences, the readback and upload rings, the slice budget and the
  capability probe.
- **COM on workers**: each application-owned pool calls `CoIncrementMTAUsage` at initialization and retains its
  WIL cookie through callback/helper teardown, so pool threads run in the implicit MTA and WIC works there. The
  UI thread stays STA. Pool close releases the cookie only after its callbacks finish. Each Io job creates its own WIC factory, the conservative choice
  until a test proves that sharing one is safe.
- **QoS**:
  - While a user-initiated job runs, worker threads are tagged HighQoS (`SetThreadInformation`
    `ThreadPowerThrottling`, ControlMask = `EXECUTION_SPEED`, StateMask = 0). The window's own QoS drops when it is
    occluded or minimized, and an export started before alt-tab must not crawl on E-cores.
  - Background callbacks run as EcoQoS with `IoPriorityHintLow` handles, except deferred frees (§4.4).
  - Each callback restores thread state before it returns.
- **Crash handling across threads** (`Core_CrashHandling.md`, P0.5):
  - `UnhandledFilter` records the owning thread id in its guard. Another thread that faults while a dump is being
    written blocks on a manual-reset event instead of returning, so it cannot end the process before the owner has
    written the dump and `pending.txt`. Only a recursive fault on the owning thread returns at once.
  - Every pool callback installs `std::set_terminate(OnTerminate)` on its first run on each thread, because the UCRT
    handler is per-thread.
  - The existing probe gains a companion switch: `--crash-probe <directory> --crash-probe-worker` faults on two pool
    threads at once and must produce exactly one non-empty dump plus `pending.txt`.
- **Ownership changes** go to `Core_Architecture.md` and the `AGENTS.md` ownership table in the slice that
  introduces each owner (§9):
  - `Application` may own `JobScheduler`, `WorkerPool`, `RedPrism.Waits` and `GpuWorkQueue`.
  - Document code may run on workers and receives an `Executor&`, but never owns a thread.
  - `Renderer` still owns the only device. `DxUi EmbeddedHost` still owns no worker.

### 4.2 Jobs

```cpp
struct TargetIdentity {                 // holds handles, compares with Raster::operator== (storage identity)
    Guid layerId;
    Raster image;                       // never a bare Identity() address (AGENTS rule)
    LayerTransform transform;
    std::optional<LayerMask> mask;      // placement, linking and enablement also affect a result
    bool isGroup;
    std::optional<LayerAdjustment> adjustment;
};
struct JobToken {                       // captured on the UI thread when the job's snapshot is taken
    Guid sessionIdentity;              // separate sessions (including copies) never accept each other's tokens
    std::uint64_t documentSerial;       // session-local counter bumped by Create/Replace/ClearDocument
                                        // (the persisted CanvasDocument::id survives reopen and resize)
    std::uint64_t generation;           // DocumentSession::Generation() at snapshot
    std::uint64_t restoreSerial;        // every Undo/Redo restore; prevents a token returning after Undo + Redo
    std::optional<TargetIdentity> target;
    std::optional<DocumentSelection> selection; // immutable geometry handles, never a deep snapshot copy
    int canvasWidth;
    int canvasHeight;
    std::uint64_t sequence;             // per-target monotonic; newer supersedes older
};
enum class JobClass { InteractivePreview, Refinement, Stroke, Selection, Commit, Io, Background };
```

| Class | Examples | Coalescing | Cancel | Records history (busy) |
| --- | --- | --- | --- | --- |
| InteractivePreview | Slider tick, gradient drag, pixel move, distort corner drag, content-aware fill preview | Per target: 1 in flight + 1 pending (latest). The in-flight job finishes and shows; then the pending one starts | On close, Cancel, or a change that invalidates the target | No |
| Refinement | Full-resolution preview of the visible region after the pointer settles; the RAW full-quality pass | A newer preview of the same target supersedes it at the next cancel check | Same, plus superseded | No |
| Stroke | Brush/eraser/clone/blur mouse-up snapshot, Liquify/Smudge dabs and finish, Spot Healing | Serial, FIFO, never coalesced. A stroke started while earlier history-recording work is pending chains behind it (§4.4) | Never by Escape once the pointer is up; only at teardown | Yes |
| Selection | Magic Wand, Load Layer/Mask Selection, Expand, Contract, Inverse, booleans | Serial, FIFO; queued clicks coalesce per §4.4 | Escape cancels running and queued selection work | Yes |
| Commit | Panel OK, Invert/Fill/Clear/Delete, transform settle, distort apply, merge, Image Size, Canvas Size fill, shapes, Add Mask, Layer via Copy, RAW bake | FIFO per document, never coalesced | Escape or Cancel while running; teardown after the discard prompt (§4.3). The document is untouched until apply | Yes |
| Io | Open, import, save, export, clipboard read/write, RAW read + decode | FIFO, serialized per target path; see §4.4 for queued imports | Cancelled at teardown (§4.3). A close waits for a running save, which is never cancelled. Saves are atomic | Open/import: yes. Save/export: no |
| Background | Thumbnails, encoded-PNG cache, develop cache trims | Bounded queue that drops the oldest entry. Deferred frees use their own intrusive list and are never dropped | Always cancellable, except deferred frees (§4.4) | No |

- **Lifecycle**: `Queued → Admitted (memory) → Running → Finished | Cancelled | Failed → Applied | Dropped`. Every
  transition is an ETW event (§4.9).
- **The job runner is the `bad_alloc` translation boundary for the job path.**
  - Job bodies may throw only `std::bad_alloc`. They run inside the scheduler's runner, which is the `noexcept`
    pool-callback frame one level above the kernel and outside every tile loop.
  - The runner catches `const std::bad_alloc&` only. It releases the reservation and completes
    `Failed(OutOfMemory)`; the UI shows a notice and records nothing. `AGENTS.md` is amended in P0.6a to name the
    job runner as a permitted boundary that fails the job.
  - Kernels allocate their outputs and scratch before `ParallelFor` starts (through `PixelBuffer::TryAllocate`,
    §4.6), so tile bodies never allocate, throw or catch.
  - The 39 existing `catch (const std::bad_alloc&)` kernel boundaries in Adjust, BrushStroke, Selection,
    CanvasSize, ImageFilters, Heal, WarpStroke and Document leave the kernels as each kernel moves behind a job. The
    direct synchronous `Commit*` wrappers catch around `Run` instead, so they keep today's failed-operation
    results. The 9 boundaries in WindowCapture, BrushCompute, CanvasRenderer, ChromeHost and SystemClipboard stay.
- **Progress**: a kernel publishes `unitsDone / unitsTotal` in an atomic. The job pushes its progress node when the
  fraction advances by at least 1 %, at most once every 100 ms. That completion invalidates only the progress bar.
  It is an event, not a poll.

### 4.3 Completion channel and the UI loop

- **Completion nodes are intrusive**: each job record holds two, one for progress and one for the final result.
  Producers push with `InterlockedPushEntrySList`, then `SetEvent(jobWake)`. `DrainCompletions` takes the whole list
  with `InterlockedFlushSList` and reverses it, so a push and a drain never wait on each other.
- **The UI wait arrays**: `jobWake` is auto-reset. It is the only job-related handle in the UI wait arrays, and it
  joins both `MsgWaitForMultipleObjectsEx` calls beside the settings watch, following that precedent.
- **System events** set a coalescing bit and push at most one node per kind: device removed
  (`RegisterDeviceRemovedEvent`), VRAM budget (`RegisterVideoMemoryBudgetChangeNotificationEvent`), and memory low
  or high. The memory watcher on `RedPrism.Waits` waits on `LowMemoryResourceNotification`. When that fires, it
  re-arms on `HighMemoryResourceNotification`, and the other way round, which makes it edge-triggered. It never
  re-arms on the handle that just fired.
- **Fences**: fence events are watched only by thread-pool waits. Each callback reads
  `ID3D11Fence::GetCompletedValue` and retires every batch at or below that value. The `EnqueueSetEvent` fallback
  uses one event per batch.
- **Apply happens only in `DrainCompletions`**, which runs at two points:
  - at the top of `Application::Run`, after `PumpMessages` and before `SelectHostFrameAction`;
  - in the self-test's `SettleJobs`.

  A completion therefore never applies inside a nested modal loop (menu, TaskDialog, IFileDialog, MessageBox,
  move/size) or inside a DxUi dispatch; it waits until the loop returns. Posted-message delivery was rejected for
  three reasons: it re-enters `WndProc` inside nested loops while `OnCommand` is mid-flight, it has a 10 000-message
  quota, and a destroyed HWND drops the payload.
- **`DrainCompletions` never opens a modal loop.**
  - It applies results without calling anything that can enter one. Failures become chrome notices
    (`ChromeHost::ShowNotice`), never `ShowError`/`MessageBoxW`.
  - Continuations that prompt, save or close become **UI actions**. `RunUiActions` runs one per `Run` iteration
    after the drain, and each re-validates its token before it acts.
  - A `CloseMainWindow` request that arrives while a drain or UI action is on the stack only sets a flag; `Run`
    tears down at the top of the next iteration.
- **`SelectHostFrameAction`** gains `completionsPending` (drain, then re-evaluate) in P0.6a, and `gpuWorkPending`
  with the `SubmitGpuWork` action in P3.1. `gpuWorkPending` is evaluated before the visibility and suspension rule,
  so a hidden window with GPU work pending still returns `SubmitGpuWork`. HostTests extends the decision table in
  each slice.
- **Close and shutdown**:
  - **A close is confirmed first.** `WM_CLOSE` runs the `ConfirmDiscardChanges` UI action after its settle ticket
    (§4.4). If a save it started is still running, the window stays up until the save applies.
  - **Then `Application::Shutdown()` runs.** It sets the cancel event, cancels every remaining job, hides the
    window, stops the lanes, and waits for running callbacks through
    `CloseThreadpoolCleanupGroupMembers(cancel = TRUE)`.
    - Cancellable kernels stop within one cancel interval.
    - Blocking waits inside jobs wait on {their handle, cancel event}, and Io reads use overlapped I/O with
      `CancelIoEx`.
    - **Non-cancellable steps finish their current call**: a single WIC decode or encode call, `FlushFileBuffers`,
      `ReplaceFileW`, clipboard calls, and a Clipper2 boolean. Each is listed in `Core_PerformanceAndResources.md`
      with its P0.1-measured worst case.
    - Queued records and undrained completions are freed without being applied. Retiring GPU slots are released
      (§4.7). `CoDecrementMTAUsage` follows.
  - **`Shutdown` runs on every exit path.** `RunApplication` calls it before `SetStringResourceSatellite(nullptr)`
    and `CoUninitialize`, including on the self-test and bench failure returns. `~Application` only asserts that it
    ran.

### 4.4 Apply protocol, busy state and continuations

**`DocumentSession::Generation()`**

- A `uint64` bumped on every mutation of `_document`:
  - recorded `EndEdit`, `Restore`, `ReplaceDocument` and `ClearDocument`;
  - imports;
  - opacity, blend and rename (now routed through `EndEdit`); live opacity and adjustment previews, outline moves,
    and floating-selection insertion also invalidate before the outer transaction ends.
- Every write through the mutable `Document()` moves behind a session API:
  - `ApplicationFiles.cpp`'s imports and `ReplaceDocument`;
  - `InsertPaintedLayer`;
  - the self-test, demo and bench helpers in `Application.cpp`, `ApplicationSize.cpp` and
    `ApplicationLayerEdits.cpp`.
- `DocumentTests` fixtures use a test-only `DocumentSession::EditForTest(fn)` that bumps `Generation()`.
- The mutable overload is then removed.
- Selection loops become `shared_ptr<const>` handles in the same slice (P0.4), so snapshots never copy them.

**Validation per job kind**:

| Kind | Applies when |
| --- | --- |
| Layer target (filters, adjustments, raster edits, distort, shapes, Add Mask) | The layer exists and its image, transform and (if used) mask equal the token's `TargetIdentity`. Before the split, `CommitColorEdit` checks image identity and transform; it uses the current layer mask without an identity check. Image `CommitStroke` also checks transform; mask strokes check the mask identity. P1.1 adds the transform and mask checks to every `Apply*`. `DocumentTests` covers each: mask edited, layer moved, then a stale apply is refused |
| Stroke | The token's base, which is the chained predecessor's result or the layer at snapshot, is still the layer's current content |
| Selection | `generation` is unchanged since the job's snapshot. Otherwise the result is dropped with the status `IDS_STATUS_SELECTION_STALE` |
| Whole document (merge, Image Size, Canvas Size fill) | `generation` is unchanged |
| Save, export | Never touch the document; they complete from their snapshot. Save completes with `MarkSaved(revisionAtSnapshot)`. Revisions are opaque GUIDs; a save after Undo can legitimately write an older revision. The scheduler orders writes/completions per path and refuses a completion from another document serial; history records the revision actually written |
| Previews | The stamp is the current launched request in its still-open mailbox epoch, and the target token remains valid. A newer pending interactive request does not invalidate the running request (D2); only a newer launched request or a closed/reopened edit does. A refinement must still be for the newest offered settings (covers the legacy `stalePreviewCannotReturnAfterOffOrReopen`) |

**Busy state and the continuation queue** (D3). This keeps the legacy
`projectOperationsBlockEditsAndQueueImageImports` semantics without losing input.

- **Busy.** A document is busy from the snapshot of any job, in any lane, whose apply records history (Stroke,
  Selection, Commit, open or import). It stays busy until the UI thread applies or drops that job. Busy is UI-thread
  state.
- **Queued commands.** On a busy document, these go into a bounded UI-side continuation queue (depth 8): mutating
  commands, Undo/Redo, menu and accelerator commands, the commit step of selection gestures, wand clicks, and
  imports.
  - Each entry records `documentSerial` at enqueue. It takes its snapshot and token when it is dequeued, after its
    predecessor applies, which is exactly today's synchronous ordering. An entry whose `documentSerial` changed is
    dropped with a status.
  - Consecutive New-mode wand clicks coalesce in the queue: the latest replaces the earlier entry. Add, Subtract and
    Intersect clicks never coalesce.
  - A multi-file drop is one entry.
- **Strokes never queue and never wait.**
  - A brush-family pointer-down draws at once over the displayed preview. When a Commit, Selection, open or import
    job, or an earlier stroke, is pending, the Stroke job is ordered behind it. It takes its snapshot and token from
    that job's result when that result applies, then composes its coverage tiles over that base (brush, eraser) or
    re-samples from it (clone, blur, smudge, heal).
  - If the predecessor is cancelled or fails, the chained strokes re-run from their recorded input on the surviving
    image. No stroke is ever refused or dropped.
  - A stroke's own allocation/device failure retains its input and reports failure; it never starts an automatic
    retry loop. An explicit retry uses a fresh input ordinal. Document replacement must first settle or explicitly
    resolve retained strokes. P0.6b implements these ownership states; P1 supplies replay storage and engine wiring,
    with storage charged to P0.7 admission. No bounded scheduler slot refusal may discard recorded stroke input.
- **Refusals.**
  - Other canvas gestures (Move drag, crop, gradient, shape) on a layer with a pending Commit or Stroke job are
    refused with `IDS_STATUS_BUSY` ("{0}…", the running action's name).
  - Commands are refused with the same status when the queue is full.
- **Never queued**: view commands (pan, zoom, menus, panel layout) and Cancel.
- **Escape and Cancel** cancel a running Commit or Selection job and empty the continuation queue. They never cancel
  a Stroke job whose pointer is up.
- **Undo after a stroke**: an Undo queued behind a stroke commit takes its snapshot at dequeue, so it undoes that
  stroke, as it does today.

**Settle tickets** replace the synchronous settle-then-read sequences (P1.1):

- `SettleInteractions` returns a settle ticket: every job that currently makes the document busy, plus the Commit
  or Stroke jobs it submits.
- `ConfirmDiscardChanges` becomes a UI action that runs only after its ticket's jobs have applied or failed. It then
  evaluates `IsModified` and prompts. A document with a pending ticket counts as modified. If Escape cancels a job
  in the ticket, the action and its close, open or new are abandoned.
- Every caller goes through that action: open, new, the New Canvas card, close project, import-as-new, `WM_CLOSE`
  and the self-test.
- `ReplaceDocument` and `ClearDocument` never cancel a job whose ticket is pending.
- `OnCommand`'s settle-then-command becomes a settle job plus the command in the continuation queue (P1.5).
- **Save-before-proceed**: after the prompt, `SaveDocument(false)` becomes an Io job. The pending "after save" UI
  action runs when the save applies: open, new, close project, import-as-new or close window. It is abandoned when
  the save fails or is cancelled.

**Session end and close** (policy table in P0.6b, wiring in P1.7):

- `WM_CLOSE` during a save leaves the window up, with the status `IDS_STATUS_CLOSE_AFTER_SAVE` ("Saving… the window
  closes when done"). Escape abandons the close.
- While a save, or an export past its GPU stage, runs:
  - A noncritical `WM_QUERYENDSESSION` calls `ShutdownBlockReasonCreate` with `IDS_SHUTDOWN_SAVING` and returns
    FALSE. A critical end is allowed, preserving the current host contract. The loop keeps pumping. The reason is
    destroyed when the job applies, fails or is cancelled.
  - `WM_ENDSESSION(TRUE)` waits only on the worker-side completion event of that job, bounded by the end-session
    timeout, and applies nothing.
- An export still in its GPU or readback stage is cancelled at end session, and so is any clipboard write.
- `UI_Windowing.md` records all of this.

**Busy indicator** (the legacy 250 ms rule and the pinned DxUi as it is):

- A pure function `BusyIndicatorVisible(startQpc, nowQpc, frequency)`. A one-shot `SetTimer` armed at job start
  requests the frame that shows it; it is not a periodic timer.
- The indicator is a `ProgressBar` sibling control that `ChromeHost::Arrange` places over a fixed-width fourth
  status section. The pinned `StatusStrip` sections are text only. Layout is written only on change. The adjust
  panel and the Develop panel have their own bars.
- A job that reports progress drives a determinate bar through its progress completions. Otherwise the bar is
  indeterminate, and its `Tick` requests frames while it is visible.
- Quick jobs never show the indicator.

**Deferred release**:

- Large frees move off the UI thread through a thread-local release sink in `Common/`. `Application` registers
  the function/context pair on its UI thread after the lanes start and clears it on that thread before stopping
  them. Other threads have no sink and free inline. This implementation decision (2026-09-27) removes the race
  between loading a process-global function and its separately retired context, with no per-release lock or
  reference count.
- `Storage`'s deleter and a directly owned `PixelBuffer` transfer allocations of 16 MiB or more to that sink.
  The buffer's allocation carries its own aligned intrusive header, so cancelled/unadopted results also avoid a
  large UI-thread free. Normal worker destruction and final shutdown destruction remain inline.
- The node is intrusive inside `Storage`, so queuing never allocates.
- A NORMAL-priority Background callback drains the list. Frees are never dropped or cancelled.
- With no sink registered (tests, shutdown, non-UI threads), the deleter frees inline.
- Shutdown drains the list after the pool stops. Admission counts queued-free bytes as releasing.

### 4.5 Data-parallel execution

- **`ParallelFor(executor, TileGrid, token, fn)`** hands out tiles through an atomic counter. The default partition
  unit is the 256×256 brush tile. Handout is dynamic, so P-cores and E-cores balance. The caller participates.
- **Deadlock-freedom invariant**:
  - The join waits only for tiles that a running thread has already claimed. It never waits for a helper callback
    to start.
  - Helpers are one `TP_WORK` per call. P0.5 retains the caller's frame until WIL's cancel/join/close returns,
    avoiding a second shared allocation. A helper that starts after the counter is exhausted returns at once;
    no callback is disassociated or can outlive the join. Helpers are outside the dispatch cleanup group so
    local closure cannot race cleanup-group ownership.
  - After its own claim loop, the caller releases `wil::unique_threadpool_work`, which calls
    `WaitForThreadpoolWorkCallbacks(helperWork, TRUE)` before close, cancels unstarted helpers and waits only for
    running ones. Pool close also joins external parallel calls before destroying the pool.
  - Tile bodies never block: no admission, I/O, gate or completion waits. There are two named exceptions: a nested
    `ParallelFor` join, which waits only on running helpers, and the LibRaw hook bodies (§6.2), which allocate
    through `LibRaw_memmgr`, catch inside the DLL and take the stream and allocator SRWLOCKs. Neither waits on a
    job, on the pool or on I/O.
  - P0.5 exit tests: N concurrent jobs, each running `ParallelFor` on a pool of N threads and on a pool of 1, all
    complete; a nested `ParallelFor` completes; a close during them completes.
- **Cancel interval**: kernels check the token per output row, or every k rows, so each check covers ≤ 1 ms at the
  kernel's measured per-pixel cost on the active tier. Motion blur at 96 samples, for example, checks every row
  inside a tile. Content-aware fill checks every N target pixels and Heal's `Solve` every sweep; neither changes the
  operation order.
- **Determinism rule**: each output pixel is computed by one call, with the same inputs and the same operation order
  whatever the partition. Floating-point reductions in exact kernels keep the reference's sequential order in one
  task, unless a spec change redefines the reference itself as an integer reduction (the histogram, D12) or as a
  fixed-partition reduction (the RAW highlight pre-pass, §6.4). Per-tile floating-point partials are not allowed in
  exact kernels.
- **Halo rule**: separable and neighborhood kernels read their halo from the immutable source, never from another
  tile's output. The Gaussian vertical pass reads horizontal results for its band plus 2r rows, which the band
  computes itself. That is exact, and it caps the float intermediate at (band + 2r) × width.
- **Coordinate keys**: Add Noise keys `y × width + x` on the whole grid. Destructive Grain keys layer-pixel positions
  (origin 0,0, units 1/scale); adjustment-layer grain keys document positions. Tiles pass global coordinates.

### 4.6 Memory

- **Thread-safe rasters (P0.2)**:
  - `Storage` holds `std::atomic<const std::uint8_t*> assembled`, stored once with release ordering and loaded with
    acquire ordering.
  - `Data()`, `ReadRegion` (for the raster and its base), `IsMaterialized`, `ByteSize` and `MakeThumbnail` read only
    that pointer.
  - Publication takes a per-storage `std::mutex` and double-checks the pointer. A failed allocation leaves the pointer null, and
    the call can be retried (no `call_once`).
  - Later (P1.7, spec change), encoders and kernels read sparse rasters in bands through `ReadRegion`, so no save
    permanently doubles a painted layer. The test `sparse.materialize-on-data` changes with that spec change.
- **`PixelBuffer`** is a 64-byte-aligned, uninitialized allocation.
  - `PixelBuffer::TryAllocate` is nothrow and initially uses `_aligned_malloc` / `_aligned_free` at every size.
    This keeps both small and large allocations under the CRT's ASan tracking without a second custom
    allocator/poisoning path. Zero size and bookkeeping overflow return empty; allocation failure fails the
    operation at its boundary. A `VirtualAlloc` threshold is deferred until measurements justify it.
  - `Raster::Adopt(width, height, format, PixelBuffer&&)` takes ownership without copying. `Raster::Make` stays for
    goldens.
  - Kernel wrappers, readbacks and decoders write straight into the buffer they adopt.
- **Scratch pools**: per-lane pools of size-classed buffers (float bands, coverage rows, tile buffers) with a byte
  cap. The brush, gradient and pixel-move paths stop allocating 256 KiB twice per tile per update.
- **Large-buffer recycling**, subject to the P0.1 measurement:
  - Every fresh 179 MB result (45 MP BGRA8) pays about 44 K demand-zero page faults. If P0.1 measures that above
    20 % of a commit budget, buffers of 16 MiB or more released by deferred release go into a size-classed recycle
    pool.
  - The pool's cap is a fixed fraction of `MemoryBudget`, and it is trimmed on memory pressure.
  - When a panel opens, the Background lane may pre-fault one result buffer of the target size.
  - P0.7 implementation decision (2026-09-27): three independent 10-pair Release runs confirm more than
    10 ms additional fresh-page cost at 179,044,352 bytes (20% of the 50 ms Levels budget). Pool capacity is
    one eighth of the available admission ceiling at initialization, capped at 512 MiB and 32 slots. Large
    allocations round to 16 MiB classes. The actual requested size remains distinct from capacity; peak estimates
    use `PixelBuffer::RequiredBytes`, including the 64-byte ownership record. Cache returns and trims run on
    workers, caps drop to zero during trim/low-memory handling, and a fresh OS snapshot decides admission after
    trim completion. No available physical pages are credited twice. Scalar scratch/row pools remain part of
    the corresponding P1/P2 kernel migrations.
  - `PixelBuffer` now separates its small intrusive allocation record from the aligned pixels. This adds one
    small CRT allocation per fresh buffer, permits allocation-free deferred release, and keeps pixel redzones
    separate from metadata. Cached payloads and class slack are poisoned under ASan; reuse unpoisons only the
    requested bytes. Measure the resulting wrapper overhead with the P0.7 benchmark before accepting the slice.
- **Admission**:
  - `MemoryBudget` is the live admission ceiling: min(`ullAvailPageFile`, `ullAvailPhys`) from
    `GlobalMemoryStatusEx` at each admission, minus outstanding reservations, minus 1 GiB of headroom. Recycle-pool
    bytes count as available, and the pool is trimmed before a job is refused.
  - The scheduler admits jobs on its submit and release paths, never from a blocked pool thread. A job reaches the
    pool only after its reservation is granted.
  - A job's declared peak covers its whole graph. Child stages (readback copy, bake, trim) draw from the parent's
    reservation instead of reserving again.
  - A job whose reservation is refused waits in its queue while other reservations are outstanding, since something
    will release. Otherwise it fails with a notice.
  - A job never holds part of a reservation while waiting for more.
- **Memory pressure** shrinks caches: encoded PNGs, develop pyramids, staging rings and the recycle pool. GPU caches
  are released (`OfferResources`) on VRAM-budget events.

### 4.7 GPU work queue

- **Capability probe at device creation** (P1.3), logged and asserted in CompositionTests on WARP:
  - `ID3D11Device5`/`ID3D11DeviceContext4` and `DXGI_ADAPTER_FLAG3_SUPPORT_MONITORED_FENCES`;
  - `D3D11_FEATURE_D3D11_OPTIONS2` (`TypedUAVLoadAdditionalFormats`, `UnifiedMemoryArchitecture`,
    `MapOnDefaultTextures`);
  - `D3D11_FEATURE_THREADING` (`DriverConcurrentCreates`);
  - `FORMAT_SUPPORT2` typed UAV stores for `R16G16B16A16_FLOAT`, `R32G32B32A32_FLOAT`, `R32_FLOAT` and `R16_UINT`;
  - `D3D11_FEATURE_SHADER_MIN_PRECISION_SUPPORT`;
  - dedicated and shared memory, and `QueryVideoMemoryInfo`.

  The `direct3d11-rendering` skill amendment states whether `ID3D11DeviceContext1` is still queried.
- **Completion**: `Signal(fence, n)`, `Flush`, `SetEventOnCompletion(n, event)`, then the thread-pool wait of §4.3.
  Without monitored fences, the fallback is `IDXGIDevice2::EnqueueSetEvent`, with one event per batch. `Map` uses
  `D3D11_MAP_FLAG_DO_NOT_WAIT` only after the fence has signaled, so it cannot stall.
  `DXGI_ERROR_WAS_STILL_DRAWING` there is a logged bug, not a retry loop.
- **Readback ring**: staging textures pooled per size class, capped at 64 MiB, with a tiled readback beyond that.
  1. The UI thread copies into staging and signals.
  2. On completion, it maps with `DO_NOT_WAIT` and lends the slot to the requesting job's copy stage: an Interactive
     job for samples, or a child stage of the parent Commit or Io job (§4.6). The copy stage memcpys into an adopted
     `PixelBuffer`.
  3. The UI unmaps when that stage's completion arrives.

  The eyedropper and the hue sampler read back a 1×1 (or n×n) box; the wand reads back its region of interest.
- **Lent slots survive device rebuilds.**
  - A lent readback or upload slot belongs to its job until the job's completion arrives. It holds `com_ptr`s to its
    staging texture and to the immediate context that mapped it.
  - `RecoverDevice`, an adapter switch (`CheckDeviceAdapter`) and `Shutdown` move lent slots to a retiring list and
    never release them there. The UI unmaps and releases a retiring slot only when the lending job completes, and
    drops that result by device generation. `Shutdown` releases whatever is still retiring after the pools have
    closed.
  - A retiring slot may keep the previous device alive until its job completes. Nothing is ever recorded on that
    device. `Rendering_Composition.md` states this next to the one-device rule.
  - Workers may call only two device methods, and never a context method:
    - `CreateTexture2D`, when `DriverConcurrentCreates` is TRUE, through a device `com_ptr` the job copied at
      submit;
    - `ID3D11Device3::WriteToSubresource`, only into a dedicated `D3D11_USAGE_DEFAULT` upload texture created with
      `D3D11_CPU_ACCESS_WRITE`. The UI mapped that texture with `Map(..., nullptr)` before the job; no pass binds it
      while the job runs; the UI unmaps it on the job's completion before `CopySubresourceRegion`.
- **Upload path**:
  - When `DriverConcurrentCreates` is TRUE, a worker creates level-0 textures or tiles with initial data.
  - Otherwise, and for updates, the staging upload ring is used: the UI maps a free slot and lends it, the worker
    fills it, and the UI unmaps it and calls `CopySubresourceRegion`.
  - On UMA devices that report `MapOnDefaultTextures`, the `WriteToSubresource` bracket above is used.
  - The UI thread never issues an `UpdateSubresource` above 4 MiB.
- **Slicing (P3.1)**: a GPU job is a list of dispatch slices over tiles.
  - After `Present`, `Pump()` submits slices until the estimated GPU time reaches the frame budget (§1.2), with at
    most 2 batches in flight, each ending in a fence.
  - `TIMESTAMP` queries, read with `DONOTFLUSH` once per rendered frame, correct the estimate. That is not a spin.
  - A slice stays under 8 ms on the slowest target, far below the 2 s TDR. `DISABLE_GPU_TIMEOUT` is prohibited.
  - When nothing is invalidated, `SubmitGpuWork` submits unpaced batches without presenting, and waits on `jobWake`
    and the queue.
  - Before P3.1, P1.3's utility composes are recorded whole at submit, fenced, and completed through the same
    channel.
- **Hidden or minimized windows**: jobs continue.
  - `Core_PerformanceAndResources.md` is amended so that "no render-target create" and "Hidden document GPU: no
    Prepare/Composite" apply to the display canvas and chrome.
  - Job targets and off-screen utility composes may be created and recorded while hidden. They are freed when the
    job ends.
- **Device loss**: every GPU job keeps its CPU-side inputs, so on `RecoverDevice` it restarts from scratch. After 2
  losses within one job, it falls back to its CPU engine where one exists. GPU completions for the old device are
  dropped by device generation.
- **WARP policy**:
  - Compute-shader filter previews run the CPU kernels at view resolution, and filter commits use the CPU engines.
  - The per-layer adjust draw (P1.2) and composition stay on WARP, since the frame composes anyway.
  - The brush keeps its compute engine, which `Rendering_Brush.md` and the self-test require.
  - Compose-based utilities (merge, export, Copy Merged, wand and clone all-layers, `CompositeBelow`) have no CPU
    engine. They run as off-screen GPU composes: whole in P1, sliced from P3.1.
- **Tiled residency and canvas mips (P3.3)**. These keep the AGENTS rule that the composition composites only when
  the document changed.
  - **Layer textures** become 2048² tiles: 16 MiB as BGRA8, 32 MiB as RGBA16F. That is under the 128 MB guarantee,
    and it lifts the 16384 px limit to the 30 000 px model limit.
  - **The display canvas** is composed in document space into 2048² canvas tiles, and only for the tiles a document
    change touched: the union of the old and new bounds of the changed layers, or everything when that is unknown.
    Each touched tile regenerates its mips. `BlitToTarget` samples the mip level that matches the zoom. That fixes
    zoom-out aliasing, and pan and zoom stay blit-only.
  - **Utilities** (merge, export, Copy Merged, wand all-layers, the adjustment-editor source) compose off-screen at
    full resolution, tile by tile, into their own targets, and cache the result by generation. They no longer borrow
    or dirty the display canvas.
  - **Tests**: a document-coordinate compose-and-readback remains for `CompositionTests::Render` and every self-test
    `pixelAt`.
  - **The raster cache** follows `QueryVideoMemoryInfo` and budget events instead of failing `Compose` and closing
    the window. The `GPU raster residency` row of `Core_PerformanceAndResources.md` names the off-screen composite
    cache and the develop tiles, with their byte caps.

### 4.8 SIMD kernel layer

**Tiers.**

- **x64**:
  - `Scalar`: the retained reference, and the floor.
  - `Sse2`: always present on x64. It emulates what SSE2 lacks, exactly:
    - trunc and floor through `cvttps2dq`/`cvttpd2dq` round trips, with the precondition |x| < 2^31, which holds
      for every clamped kernel value;
    - the 32-bit low multiply (Mix32) through `pmuludq` on the even and odd lanes.
  - `Avx2`: detected by CPUID.1:ECX[12] FMA, ECX[29] F16C and ECX[27] OSXSAVE; CPUID.(7,0):EBX[5] AVX2 and EBX[8]
    BMI2; and XCR0[2:1] = 11b. `__check_isa_support(__IA_SUPPORT_VECTOR256)` may replace only the AVX2 and XCR0
    part.
  - Optionally `Avx512` (D16).
- **ARM64**: a `Neon` floor. FP16 and dot-product helpers are used only where `IsProcessorFeaturePresent` reports
  them (Windows 11 24H2 and later; older builds report false, which is safe).
- **Dispatch**: a `KernelTable` of function pointers is chosen once at startup. `--isa scalar|sse2|avx2|avx512|neon`
  (a new harness switch) and `SetIsaForTest` force a tier.

**No mixed-`/arch` translation units.**

- Tier kernels are written with intrinsics, in TUs compiled at the **default** `/arch`. MSVC emits VEX/EVEX only for
  functions that use AVX2, F16C, BMI2 or AVX-512 intrinsics. No shared COMDAT (an STL template, an implicit special
  member, a `<cmath>` wrapper) is ever compiled for a higher ISA.
- Every tier entry point, in any module, is an extern `__declspec(noinline)` function named
  `Kernels_<Tier>_<Module>_<Name>` (for example `Kernels_Avx2_RawDevelop_Demosaic`, `Kernels_Avx2_RawDecode_Lj92`).
  It lives in its own tier TU (`Kernels_<Tier>_<Module>.cpp`) and is reached only through the `KernelTable`.
- **Mechanical check**: in every x64 configuration, `build.ps1` finds VEX/EVEX instructions by prefix (C4/C5/62) in
  executable sections. It attributes each one to its contributing object through the linker map or PDB section
  contributions, and fails on any that is not in a `Kernels_Avx2_*`/`Kernels_Avx512_*` object.
  - A checked-in allowlist names the runtime-dispatched MSVC STL and CRT objects (`vector_algorithms`, `wmemchr`)
    and any third-party library with its own CPUID dispatch.
  - P0.8 proves the current binary passes with only that allowlist.
  - An `--isa sse2` self-test under Intel SDE with a pre-AVX CPU model is optional and recorded at closeout. An
    `--isa sse2` run on an AVX2 host cannot detect a leak.

**Exactness technique.**

- Vectorize **across output pixels, never across the terms of one pixel's sum**.
- Use only IEEE-exact vector ops: add, sub, mul, div, sqrt, and conversions with explicit rounding.
- `min`/`max` copy the reference's `std::min`/`std::max` operand order, because `minps`/`maxps` and NEON
  `FMIN`/`FMAX` treat NaN differently.
- Hand-written FMA is banned wherever the reference does `a * b + c`.
- Emulate `std::lround`/`std::round` (half away from zero) as `trunc(x) + (|x − trunc(x)| ≥ 0.5 ? sign(x) : 0)` in
  the same precision. That is exact for every finite input; it differs only for −0.0 and NaN, which kernels clamp
  away. Never use `cvtps2dq`, which rounds half to even.
- `/fp:precise` stays. MSVC 14.51 does not contract under it on x64 or ARM64.

**Transcendentals and parameters.**

- Kernels that call the CRT per pixel get thread parallelism only: Gaussian Add Noise, the brush tip density, Spot
  Healing's Create Texture grain, and the radial gradient's `std::hypot` (sqrt(x² + y²) is not bit-equal to it).
- Every other kernel is split into two parts:
  - **parameter derivation**: the CRT calls, run once per settings (blur weights, the Levels/Curves/Exposure float
    tables, motion dx/dy, Heal's candidate offsets from `cos`/`sin` → `lround`);
  - **a pixel stage** built only from IEEE basic operations and integer math.
- The UCRT switches `exp`/`pow`/`log` to FMA3 implementations on CPUs that have FMA3 (`_set_FMA3_enable`), and
  ARM64 has its own CRT. Parameters can therefore differ in the last bit between machines, so cross-architecture
  hashes cover the pixel stage only (§8).

**Exact reformulations**, each proven equal by an exhaustive or matrix test:

- **Levels/Curves/Exposure**: a (value, alpha) byte LUT per channel, 3 × 65 536 bytes, built by running the scalar
  reference over every pair. The test is exhaustive equality over all (v, a).
- **Gradient Map**: integer SIMD.
- **HSV cube**: float trilinear with the 8 weights hoisted and the same summation order.
- **BlendThrough**: fused into the adjustment kernel, with the same lerp and rounding.
- **AlphaBounds**: a min/max reduction.
- **Premultiply/unpremultiply**: integer reciprocal tables.
- **Invert, Fill, Clear, Delete**: integer blends `(c·s + o·(255−s) + 127) / 255`, because every factor is m/255,
  opacity is 1 and the color alpha is 1. The exact value is N/255, which is never within 1/510 of a half-integer.
  The proof and an exhaustive (c, o, s) test per mode go in `Document_Tools.md`.
- **Gradients**: the linear gradient keeps the double formula, vectorized lane by lane. The radial gradient calls
  scalar `std::hypot` per pixel. Both have a tier-equality test.
- **Not an exact reformulation**: the Levels histogram. Its integer form is a redefinition (the D12 spec change).

**Half floats**: F16C (`_mm256_cvtph_ps`) and NEON `vcvt_f32_f16` in the RAW CPU path. Scalar DirectXPackedVector
conversion is never used in a hot loop.

### 4.9 Observability

- **ETW**: a TraceLogging provider `RedPrism.Pipeline` emits:
  - job transitions, lane depths and admission waits;
  - GPU slices (CPU record time and timestamp GPU time);
  - `UiMessage{msg, durationUs}` when a handler exceeds 4 ms;
  - `UiFree{bytes}` for every `Storage` release of 1 MiB or more on the UI thread;
  - frame build time and RAW stage timings;
  - page-fault time per result allocation (P0.1).

  It costs nothing while no trace session is enabled. `Build/RedPrism.wprp` records it with CPU and GPU stacks in
  WPR.
- **UI stall histogram**: the `Common/Jobs` loop functions and the frame record time each dispatch, drain, UI action
  and frame build with QPC into a fixed bucket array, with no allocation. `Run`, `SettleJobs` and the bench share it.
- **`--bench <scenario>`** grows from `brush` to `brush, adjust, filters, tools, readback, io, raw, ui`, with these
  options:
  - `--bench-input <path>`;
  - `--bench-develop <json>` (develop settings for M4);
  - `--bench-cold --bench-prime <path>`: opens the untimed prime file (a different file of the same format), waits
    on a named event that the §7 script sets after the purge, then opens `--bench-input`.

  `--bench ui` runs the real `Application::Run` loop with posted input and scripted commands, and exits on a posted
  quit.

  Every scenario prints a `BENCH MACHINE` line: CPU brand, cores/CCDs/L3, RAM, adapter/driver/LUID, OS build, power
  scheme, configuration, version, ISA tier, pool size and `_get_FMA3_enable()`.

  Each command reports UI-thread time, time to first preview, job latency, apply time, and the largest gap between
  dispatches during the job. Values are median/p95/max over ≥ 10 runs after a warm-up, except under `--bench-cold`.
- **Positional path** (P1.7): `RedPrism.exe <path>…` opens files through `OpenPaths`. This is a product feature, and
  it makes double-click and scripted head-to-head timing possible (`UI_Windowing.md`, `CommandLine.h`, HostTests).

## 5. Per-operation design

Placement legend:

| Code | Meaning |
| --- | --- |
| S | Stays synchronous (measured ≤ 4 ms p99) |
| J | Job on a worker with an exact CPU kernel (lane in brackets) |
| G | GPU preview within a named tolerance |
| R | Async readback |
| Q | Queued behind the busy state |

| Operation (entry point) | Today | New placement | Preview | Commit / result |
| --- | --- | --- | --- | --- |
| Levels, Curves, Exposure, Gradient Map, Hue/Saturation, Grain panels (`ApplicationAdjust.cpp` `OnAdjustAction` → `AdjustEdit.cpp` `Update*`) | CPU kernel per tick on the UI thread; full size at 4K | G + J [Commit] | **Per-layer adjust draw.** A new pass mode renders the pass-6 adjust math into a transient layer-sized target, with the layer's resident texture as source, opacity 1 and the adjustment-layer mask off. The edit's selection coverage on the layer grid (cached once per panel session) is bound as the coverage input. Grain is keyed by layer pixel ((x+0.5)·1/previewScale), matching `RunFilter`. The transient replaces the layer's texture in its normal layer pass (mode, opacity, mask). Per tick, only the 256×1 LUT (DYNAMIC, `WRITE_DISCARD`) or the 33³ cube (DEFAULT, `UpdateSubresource`) changes. Pass 6 stays for adjustment layers | Exact SIMD kernel at full size. A preview is reused when its grid equals the full grid (§5.1) |
| Levels histogram at panel open, adjustment-layer editor | Full-resolution histogram on the UI thread | J [Interactive] on the host path; inline on the direct API | Levels shows "loading" until ready (legacy `histogramTask`); Auto buttons wait on `histogramReady` | D12: the integer histogram of an 8000 px source. The direct API keeps computing inline, so `levels.histogram-ready` stays valid there |
| Gaussian / Motion blur / Lens / Add Noise panels | CPU at ≤ 2048 px or full size per tick | G (J on WARP) + J [Commit] | Compute shader on the view-resolution region, σ and distance scaled; noise keeps full-grid keys | Exact; σ > 32 per D4 |
| Content-aware fill | Full fill at open and again at OK | J [Interactive], once, on the full grid | Progress bar; OK enabled when the fill is ready | OK reuses the preview bytes (§5.1 step 3) |
| Invert, Fill, Clear, Delete (`MakeRasterEdit` + `PaintCanvas`) | Scalar per pixel with `std::function` | J [Commit] | — | Exact integer blends; selection coverage cached and bounded to the selection |
| Gradient drag (`DragGradient` → `RefreshGradient`) | Whole canvas per move | J [Interactive] (P1); G (P3) | GPU shade into the stroke working texture (P3) | J [Commit], exact at mouse-up |
| Shape tool mouse-up (`FinishShape` → `ShapeImage` → `AddPixelLayer`) | Full coverage, scalar fill, `Make` and thumbnail on the UI thread | J [Commit] | Draft outline stays until apply | Exact; rows in parallel; coverage bounded to the shape; adopted buffer |
| Transform commit of live shape layers (`CommitTransform` → `RedrawShape`) | Per-layer re-rasterize on the UI thread | J [Commit], layers in parallel | Transformed preview stays until apply | Exact |
| Brush / eraser update (`GpuBrushCoverage::Render`) | Dispatch + blocking `Map(READ)` per tile + CPU compose + upload + whole-texture mips | G (P3) | Tiles composed on the GPU from coverage into the working texture; mips only for touched tiles, once per frame | Mouse-up: J [Stroke] (P1.4). Fenced async readback of the touched tiles, `PaintSnapshot` on a worker, O(n) `Replacing` |
| Blur / Clone / Liquify / Smudge stroke start (`BlurSample`, `CloneSample`, `BeginWarp`) | Document-size render (+ full Gaussian) at pointer-down | J [Interactive], lazy per tile | Tiles the tip reaches are rendered on demand, and blurred with their 3σ halo, with edges treated as the document-size version treats them. Exact | — |
| Liquify / Smudge dabs and finish (`ContinueWarp`, `FinishWarp`) | Dabs on the UI thread; finish replays every dab with GPU round trips | J [Stroke], serial (dabs cannot be dropped) | Changed tiles published per completion | Finish renders all hard-tip segments in one pass per tile. Max coverage does not depend on order, so the result is identical |
| Spot Healing mouse-up (`BrushStroke::Heal`) | 10 ms – 0.7 s synchronous | J [Stroke] | The wash stays until apply | Exact; `Score` candidates in parallel with an in-order argmin; `Solve` sequential |
| Pixel move / nudge / Free Transform of a selection | Tiles rebuilt per move on the UI thread | J [Interactive] (P1); G (P3) | Offset composite in the working texture | J [Commit], exact: `MergeFloating` over the footprint, sparse result |
| Distort preview / apply (`DisplayDocument`, `CommitTransform`) | 1024 px warp plus the full-resolution linked mask per move; full resolution at apply | J [Interactive] (P1, warp at 1024 px, mask capped at 1024); G (P3, projective pass, no CPU raster) | See placement | J [Commit], exact, rows in parallel |
| Implicit transform settle before commands (`OnCommand` → `CommitTransform`) | Synchronous full-resolution distort | J [Commit] + Q | The preview stays until apply | The command follows in the continuation queue |
| Merge Down / Merge Layers / Merge Group (`MergeLayersCommand`) | Compose into the display canvas + blocking readback + trim | G + R + J [Commit] | — | Off-screen compose → ring readback → trim in a child stage; generation check |
| Eyedropper, hue sample, wand all-layers, Copy Merged, clone all-layers | Full compose + full blocking readback per press | R | — | 1×1, n×n or ROI readback of an off-screen composite cached by generation |
| Adjustment-layer editor open (`CompositeBelow`) | Compose + readback + full histogram | R + J | The panel opens at once; the histogram arrives | — |
| Magic Wand, Load Layer/Mask Selection, Expand, Contract, Inverse, complex booleans | UI thread (render, flood fill, trace, Clipper2) | J [Selection] | "Selecting…" after 250 ms | One entry per applied job, in submission order |
| Add Mask from selection, Layer via Copy | Full coverage + copy on the UI thread | J [Commit] | — | Exact. The mask thumbnail (edge tone) is computed in the job and applied with the mask |
| Selection coverage (`SelectionGeometry::Coverage`) | Full document, two float planes, per operation | J, cached | — | Cached per (selection revision, grid), bounded to the selection, rows in parallel |
| Image Size (`ResizeImage` → `RenderPlaced`) | Per layer, up to 4×4 bilinear samples per channel | J [Commit], layers in parallel | — | Exact: coordinates hoisted out of the channel loop, identical math |
| Canvas Size with an extension fill; Crop | Fill loop on the UI thread | J [Commit] / S | — | Crop stays metadata-only (S) |
| Open `.prism` (`LoadProject`) | Sequential inflate + PNG decode on the UI thread | J [Io]: entries read sequentially, then `ParallelFor` inflate + decode | Status "Opening…" | `ReplaceDocument` at apply; uploads through the ring, sliced |
| Import / open image (`ImportImagePaths`) | WIC decode on the UI thread; P0.3 adopts its output buffer, and EXIF/ICC handling is already present | J [Io], files in parallel | First pixels from a WIC scaled decode (`IWICBitmapSourceTransform`) | Preserve adoption, EXIF orientation and ICC → sRGB (`IWICColorTransform`) while moving decode to jobs |
| Save / Save As (`SaveProject`), ZIP and folder projects | Sequential PNG encode + level-9 re-deflate on the UI thread | J [Io]: `ParallelFor` encode into memory, then sequential write | Status "Saving…" | §5.3 |
| Export PNG/JPEG (`ExportFlattened`) | Compose + readback before the dialog and encoding on the UI thread; atomic temp-file replacement already present | Dialog first, then G + R + J [Io] | — | Off-screen compose; encode in bands from `ReadRegion`; preserve flush and atomic replacement |
| Copy / Cut / Copy Merged → system clipboard (`MirrorClipboard`) | PNG + DIBv5 encode on the UI thread; `Sleep(10)` retry loop | J [Io] | The internal clipboard updates at once | A worker builds both `HGLOBAL`s. The UI step (`OpenClipboard(hwnd)`, `EmptyClipboard`, two `SetClipboardData`, `CloseClipboard`) stays short and synchronous. A busy clipboard is retried at most 5 times through one-shot timers (50, 100, 200, 400, 800 ms), then a notice |
| Paste from another app (`ReadImage`) | Decode while the clipboard is held open | J [Io], entirely | — | The worker runs `OpenClipboard(nullptr)` (reading needs no owner window), `GetClipboardData`, `GlobalLock`, copy, `GlobalUnlock` and `CloseClipboard` in one callback, then decodes. A busy clipboard gets the same bounded retries. The UI thread never holds the clipboard across a dispatch |
| New Canvas card size prefill (`OpenSizeCard`) | Full decode of the clipboard image on the UI thread | J [Io] | The card opens at once with the last size; the clipboard size fills in when the job completes | Header only (`BITMAPINFOHEADER` / WIC `GetSize`) |
| History `End` / `Undo` / `Trim` | 3–4 document copies; `RetainedBytes` per dropped entry | S | — | Moves instead of copies; incremental identity→bytes accounting; shared selections counted (D13) |
| Thumbnails (`MakeThumbnail`, 21 call sites) | Per commit; image-layer thumbnails are never read | Image layers: Background, on demand. Masks: eager | — | Mask thumbnails carry the `MaskEdgeBackground` tone, so they are computed inside the job that produces the mask and applied atomically with it |
| First upload of a large raster (`UploadRaster`) | Full `UpdateSubresource` in the frame | Upload ring | — | Tiles; the UI thread copies ≤ 4 MiB per frame |
| Main-loop selection copy (`DisplayedSelection`) and pan/zoom rebuild (`SyncChrome`) | Copies every loop vertex per iteration | S | — | Cached by selection revision; view transform applied at draw |

### 5.1 Adjustment and filter panels (the model for every panel)

1. `Begin*` snapshots `{original Raster, transform, mask, selection handle, seed, grid, preview grid}`. It is cheap:
   the downscale and the host-path histogram move into the first jobs.
2. Each control change sets `pendingSettings`.
   - For per-layer adjust draw kinds, the next frame reads the settings directly: a table or cube rewrite, with no
     job.
   - Otherwise an InteractivePreview job is posted: one in flight, one pending.
3. `OK` (button, Enter, tool switch, `AddAdjustmentLayer`) submits a Commit job.
   - If the last CPU preview was rendered on the full grid with the same settings, seed and source identity, its
     bytes are the commit and apply at once.
   - Otherwise the kernel runs at full size. The panel shows "Applying…" (after 250 ms) and disables its controls
     except Cancel. The preview stays on screen until the entry applies, so the canvas never flashes the original.
4. `Cancel` or Escape before OK closes the panel and discards every result by sequence. Escape or Cancel during
   "Applying…" cancels the commit and closes the panel, leaving the layer unchanged. A document replacement cancels
   both lanes for the target.
5. `ApplyColorEditResult` validates the token (§4.4) and installs the image, placement and carried mask through the
   session, as one entry named for the kind.

The 2026-09-23 refactor already unified panels into `ColorEdit`: `BeginColorEdit`, `UpdateColorEdit`,
`CommitColorEdit` and `CancelColorEdit`. `UpdateLevels` and `UpdateHueSaturation` are thin wrappers. P1.1 extends
that shared pipeline; it MUST NOT restore separate per-kind sessions. In today's synchronous implementation,
commit reruns `Rendered` at full scale from `edit.Source()` with the latest settings. A failed Content-Aware Fill
keeps its panel open with `previewFailed`; other failed renders and stale image/transform checks close it without
an entry. `filterSettings` is assigned before those checks. P1.1 must explicitly implement and test the failure
and retry policy of the new job path rather than assuming current behavior already provides it.

The proposed new split is:

- `PrepareColorEditCommit(edit) → ColorEditJobInput`;
- `RunColorEditJob(input, executor, token) → ColorEditJobResult`, pure work in `Common/AdjustEdit.cpp`;
- `ApplyColorEditResult(result) → bool`.

`CommitColorEdit` stays as `Apply(Run(Prepare(edit)))` on the calling thread, with a `bad_alloc` boundary around
`Run`. So the existing `DocumentTests` goldens keep passing, including `hsv.preview-live` and
`hsv.preview-never-accumulates`. Levels and Hue/Saturation use that same split; raster edits, merge, resize and
shapes follow the same Prepare/Run/Apply pattern.

### 5.2 Brush and retouch

The plan adopts the brush bench's four follow-ups:

- an R8 typed-UAV preview, or no preview readback at all;
- tile composition on the GPU (paint color × coverage over the base, the legacy Metal kernel's job);
- mips only for touched tiles, once per frame;
- readback deferred behind a fence.

Mouse-up becomes a Stroke job (P1.4): a fenced readback of the touched tiles, then `PaintSnapshot` on a worker. The
next stroke chains from the pending result, so painting never waits and never drops input. `PaintSnapshot`'s
`Raster::Replacing` becomes an O(Told + Tnew) grid map instead of per-pair vector splitting. The image-layer
thumbnail moves to on-demand.

The CPU coverage engine stays the test engine and the fallback when the compute engine cannot initialize. On WARP
the brush keeps the compute engine, which the self-test requires. The GPU/CPU coverage tolerance (2/255) is
unchanged.

### 5.3 Files

- **Open and import**: per-file progress. The Io job reads the compressed ZIP entries sequentially (libzip handles
  are not thread-safe), then inflates and decodes them with `ParallelFor`.
- **Saves and exports are serialized per target path.**
  - A save requested while one is running sets a coalesced "save again at the latest revision" flag. That save
    starts when the running one completes.
  - Temp and staging names are unique (`dest + L".~" + random hex`). ZIP temp files are opened with
    `ZIP_CREATE | ZIP_EXCL`, and staging directories are created exclusively.
  - Before writing, a save of a path deletes stale `dest.~*` temps and staging directories. Per-path serialization
    guarantees none of them belongs to a running job.
- **ZIP projects**:
  - Layer PNGs are encoded in memory with `ParallelFor`.
  - PNG entries are written `ZIP_CM_STORE`; the manifest is deflated.
  - The ZIP is written to the temp file, then `FlushFileBuffers` (D17), then `ReplaceFileW`.
  - libzip's cancel and progress callbacks (`zip_register_cancel_callback_with_state`,
    `zip_register_progress_callback_with_state`) drive Cancel and the progress bar from inside libzip's write loop
    on the worker.
- **Folder projects**: PNGs are encoded in memory with `ParallelFor`. The Io job then writes each file into the
  staging directory and flushes it (D17), and the existing rotation runs. Cancel deletes the staging directory.
  Progress counts files.
- **Encoded-PNG cache**: an unchanged layer's pixels are immutable, so a re-save reuses its encoded bytes. The cache
  is keyed by `Raster::Identity()` and holds the `Raster`.
- **No UI-thread file I/O**: `LooksLikeProjectPath` and `PathLooksLikeZipProject` read the file, and can stall for
  seconds on a cloud placeholder or network path. They move into the Io job; the UI decides by extension only.

## 6. RAW pipeline

### 6.1 Scope of v1

- **Layouts** (`RawInfo.layout`):
  - **`BayerCfa` and `XTransCfa`**:
    - Canon CR2 and CR3;
    - Nikon NEF (lossless compressed, compressed, uncompressed);
    - Sony ARW (compressed, lossless);
    - Fuji RAF (compressed, uncompressed);
    - Panasonic RW2, Olympus/OM ORF, Pentax PEF;
    - DNG with a CFA.
  - **`LinearRaw(n)`**: linear DNG, including phone files and DNG Converter "linear" output. LibRaw leaves
    `raw_image` null and fills `color3_image`/`color4_image`.
  - **`YccRgb`**: Sony YCC pseudo-RAW. LibRaw decodes it as 3-channel RGB that is already white-balanced.
- **Out of v1.** Each returns a clean status (`RawUnsupportedCompression` or `RawUnsupportedCamera`) and offers the
  embedded preview:
  - Nikon HE/HE* (TicoRAW patents; D15);
  - DNG 1.7 JPEG XL (libjxl, a P5 option);
  - lossy (JPEG) DNG, unless D5 enables LibRaw's `dng-lossy`;
  - Nikon sRAW, unless routed through `YccRgb`;
  - Apple ProRAW local tone maps (`ProfileGainTableMap`);
  - Sony ARW 6.0 lossy and Fuji lossy RAF.
- **Already white-balanced files**: `RawInfo.whiteBalanceApplied` comes from `imgdata.color.as_shot_wb_applied`.
  This covers Canon, Nikon and Pentax multi-exposure files and the YCC files.
- **Size** (D14): any RAW whose oriented, cropped frame exceeds 100 000 000 pixels returns `TooLarge`, with the
  embedded preview offered as an 8-bit layer. That includes GFX100 (101.8 MP), X2D (101.9 MP) and IQ4 (151.3 MP).
  Files are limited to ≤ 512 MiB.

### 6.2 Decoder stack

**LibRaw ≥ 0.22.2**, the 2026-07-16 security release (the pinned baseline has 0.22.1), comes in through an overlay
port, `Build/vcpkg-ports/libraw`:

- LibRaw-cmake with `ENABLE_OPENMP=OFF`, `ENABLE_LCMS=OFF` and `ENABLE_JASPER=OFF`. Jasper is unused from 0.22 on.
  OpenMP spin-waits by default and is incompatible with ASan.
- zlib is kept for deflate DNG. `dng-lossy` only under D5.
- RedPrism links only `raw_r` (`libraw::libraw_r`). The non-`_r` `raw` library is built with `LIBRAW_NOTHREADS` and
  keeps static decoder state.
- LibRaw is a DLL (LGPL-2.1 compliance; the CDDL alternative stays available), built in the x64-windows-asan tree
  too. The patch is published with the LGPL notice.

**The `LIBRAW_USE_CALLBACK_PARALLEL` patch** replaces LibRaw's OpenMP loops with a RedPrism-supplied parallel-for.
Nothing may escape from it into RedPrism code.

- **Hook ABI**: `int (*)(void* ctx, void (*body)(void* bodyCtx, int64_t begin, int64_t end), void* bodyCtx,
  int64_t count)`.
  - It returns 0 when every iteration ran and 1 when `ParallelFor` stopped early on cancel.
  - Neither pointer type carries `noexcept`; the no-throw rule is part of the contract.
  - RedPrism implements the hook with `ParallelFor`.
- **Loops covered**:
  - `fuji_decode_loop` (768 px strips);
  - `crxLoadDecodeLoop` and `crxLoadFinalizeLoopE3` (CR3 planes);
  - `pana8`'s stripe loop (RW2 encoding 8).
- **Exceptions stay in the DLL.** Each body keeps the OpenMP-mode handling: a try/catch per iteration and an atomic
  error count. After the join, on the calling thread and still inside the DLL, LibRaw rethrows (for example
  `LIBRAW_EXCEPTION_IO_EOF`), or throws `LIBRAW_EXCEPTION_CANCELLED_BY_CALLBACK` when the hook returned 1. The
  public API then returns an error code. `derror()` state updates become atomic.
- **Stream serialization**: RedPrism opens files through `open_datastream` with a `LibRaw_buffer_datastream`
  subclass whose `lock()`/`unlock()` take an SRWLOCK. This is LibRaw's documented extension point: the base
  `lock()` is a no-op, and without it parallel strips would race on the shared read cursor.
- **Allocator**: `LibRaw_memmgr::mem_ptr`/`forget_ptr` take an SRWLOCK in this mode. Fuji strips `calloc` through
  it.
- **Cancel**: `fuji_decode_strip`, `crxDecodePlane` and `pana8_decode_strip` poll once per decoded line or block.
  The poll reads `_exitflag` and does not clear it; `checkCancel` would clear it.
- **Not covered**: the LJ92 decoders (`sony_ljpeg_load_raw`, `lossless_dng_load_raw`) and `nikon_load_raw`. They
  have no OpenMP loop and walk tiles or rows through one shared bit reader, so they stay sequential in LibRaw. That
  is why ARW, DNG and NEF reach their M2 targets only in P5.

**Wrapper** `Common/RawDecode.{h,cpp}`, a header with fixed-width types only:

- `RawProbe(bytes) → RawInfo`: make, model, dimensions, `layout`, CFA pattern, levels, white balance,
  `whiteBalanceApplied`, color data, orientation and the thumbnail list.
- `RawExtractPreview(bytes, maxSide)`: the smallest embedded JPEG at or above `maxSide`.
- `RawDecode(bytes, executor, token) → RawPlanes`: a `uint16` CFA, or linear planes, adopted into `PixelBuffer`s.

The file is read once into a buffer. `setCancelFlag` is driven by the job token. `LIBRAW_UNSUFFICIENT_MEMORY` fails
the job. `max_raw_memory_mb` and dimension caps apply before any allocation.

**DNG specifics**:

- LibRaw applies the DNG `LinearizationTable` through `imgdata.color.curve` during unpack. `RawDecode` saves that
  table after `open_datastream`, whose identify step loads it for the selected IFD, and sets the curve to the
  identity before `unpack()`. `raw_image` (or `color3/4_image` for `LinearRaw`) then holds the stored values, and
  the develop linearizes itself in spec order (§6.4). Non-DNG camera curves stay inside the decoder.
- LibRaw keeps only the rounded mean of `BlackLevelDeltaH/V` and parses no profile tables. A RedPrism **DNG tag
  reader** therefore reads:
  - `BlackLevel` and `BlackLevelDeltaH/V`;
  - `ProfileHueSatMapDims`/`Data1`/`Data2`/`Data3` and `ProfileHueSatMapEncoding`;
  - `ProfileLookTableDims`/`Data` and `ProfileLookTableEncoding`;
  - `BaselineExposure` and `BaselineExposureOffset`;
  - the calibration signatures and `ForwardMatrix1/2`.
- Opcode lists come from `dng_levels.rawopcodes[0..2]`.
- The tag reader is fuzzed from P4 on.

**RedPrism fast decoders (P5)** replace LibRaw's decode where it is the critical path. Each must produce a buffer
bit-identical to LibRaw's output for every corpus file: `raw_image` (identity curve for DNG, camera curve otherwise)
or `color3/4_image`. That is the golden, and a perfect oracle.

- **LJ92 tile decoder** for Sony lossless ARW (512² tiles) and DNG lossless tiles. Tiles decode in parallel with a
  64-bit bit reservoir and multi-symbol Huffman lookup tables. Estimate: 45 MP in ≈ 65–90 ms on R1, against ≈ 1 s
  sequentially in LibRaw.
- **Sequential LUT Huffman decoder** for NEF lossless (with its per-row vertical predictor) and CR2. A CR2 is one
  lossless-JPEG scan with no independent entry points, so it gets no tile parallelism. Estimate: 150–250 MP/s, or
  0.2–0.3 s for 45 MP. A speculative self-synchronizing parallel decode (Weißenberger & Schmidt, ICPP 2018) is
  attempted only if the LUT decoder misses M2.
- **CR3 CRX**: more than 4-way parallelism, if the tiles prove independent (verify against `crx.cpp` and the lclevy
  notes).

**Security**: RAW files are untrusted input parsed in-process.

- The LibRaw pin tracks security releases.
- The RedPrism decoders and the DNG tag reader get fuzz targets.
- Hostile-file tests cover truncation, huge dimensions, recursive IFDs and zero-size tiles.
- The multi-thread crash handler (§4.1) dumps a decoder crash like any other.

### 6.3 Open flow and budget (R1, 45 MP Sony lossless ARW, estimates)

| Step | Where | P4 | P5 |
| --- | --- | --- | --- |
| Read the whole file (60 MB, `FILE_FLAG_SEQUENTIAL_SCAN`, reused buffer) | Io lane | ≤ 6 ms warm; cold depends on the drive (recorded in P0.1) | same |
| `RawProbe` + pick the embedded preview (1616×1080) | Io lane | ≤ 5 ms | same |
| WIC scaled JPEG decode of the preview → upload → next frame (**M1**) | Io lane + ring + UI | ≈ 40–80 ms after open | same |
| CFA decode | Io lane + pool | ≈ 1.0 s (LibRaw, sequential LJ92 tiles) | ≈ 65–90 ms (RedPrism LJ92, pool of N) |
| CFA upload (90 MB R16) | Worker create or ring | 5–15 ms | same |
| GPU develop at full resolution: unpaced `SubmitGpuWork` while nothing is invalidated, otherwise ≤ 2 ms per presented frame (3–8 frames at 120 Hz) | UI records | 5–15 ms GPU | same |
| Bake to a layer (RGBA16F → dithered sRGB8, ring readback, adopt) | GPU + R + J | 30–50 ms | same |
| **M2 full quality, editable** | | ≈ 1.15–1.35 s | ≈ 180–300 ms |

The other formats follow the same shape. CR3 through the parallel hook is estimated at 0.15–0.3 s of decode in P4. A
40 MP RAF is ≈ 0.4 s with strips on the pool. NEF and CR2 need the P5 LUT decoder to reach 0.5 × Photoshop.

### 6.4 Develop pipeline ("RedPrism Develop 1")

The math is normative in `Rendering_RawDevelop.md`, with an fp32 scalar CPU reference in `Common/RawDevelop.{h,cpp}`.
The stage order follows the DNG specification (1.6/1.7), so one pipeline is well-defined for DNG and for every
LibRaw format. `LinearRaw` and `YccRgb` inputs skip steps 2–3 and do highlight handling in RGB.

1. **Raw prepare**, in spec order, per site:
   1. `OpcodeList1` on the stored integers (values 0…2^16−1; `FixBadPixelsConstant` compares the integer constant).
   2. `LinearizationTable`.
   3. Black: the `BlackLevel` repeat pattern plus per-row `BlackLevelDeltaV` and per-column `BlackLevelDeltaH`. Never
      add LibRaw's `black`, `dng_black` or `dng_fblack` to the deltas.
   4. Rescale by 1 / (White − max black), then clip to [0, 1], keeping a clip mask.
   5. `OpcodeList2` (`GainMap`, `MapTable`, `MapPolynomial`, `Delta/Scale PerRow/Column`, bad-pixel lists), with a
      clip after each opcode.
   6. White balance **W = k · diag(1/CameraNeutral)**:
      - CameraNeutral is `AsShotNeutral`; or the neutral that the interpolated XYZtoCamera computes for a
        Kelvin/tint target; or auto (gray-world on the preview level).
      - k normalizes the smallest multiplier to 1.
      - Already-balanced files apply the requested multipliers divided by the as-shot ones.
2. **Highlight reconstruction** on the CFA before demosaic.
   - The default is clip-and-blend, which is exactly the identity on unclipped data.
   - The quality mode is "inpaint opposed", darktable's method, run as two passes:
     - A whole-image pre-pass computes the chroma correction vector from partial sums over a fixed 1024² partition,
       merged in partition order. That partition is part of the Develop 1 definition, and the untiled CPU reference
       uses the same partition and merge order.
     - The vector is cached by (CFA identity, WB, clip levels). Tile, region-of-interest and level-of-detail passes
       all consume the cached vector, so preview and bake agree.
     - The stage cache re-runs the pre-pass when WB or the clip levels change.
3. **Demosaic**:

   | Pattern | Interactive | Final (bake, export, full-resolution refine) |
   | --- | --- | --- |
   | Bayer | 2×2 superpixel (½ size) at ≤ 50 % zoom; Malvar-He-Cutler 5×5 (2 px halo) above | RCD, 10 px halo |
   | X-Trans | 3×3 superpixel (⅓ size) at ≤ 33 % zoom; Markesteijn 1-pass (or fast X-Trans bilinear) above | Markesteijn 1-pass, 12 px halo (a multiple of 6) |

   - Superpixel levels involve no interpolation. They are preview levels and do not equal the final demosaic.
   - Bake and export always demosaic at full resolution with the final method.
   - Tiled output equals untiled output byte for byte on the CPU engine, and within the `Rendering_RawDevelop.md`
     tolerance on the GPU (§6.8).
4. **`OpcodeList3`** (`WarpRectilinear`, `FixVignetteRadial`) when present. Lens profiles beyond DNG opcodes are not
   in v1: lensfun is LGPL-3 with a CC BY-SA database (D5).
5. **Color**, fp32 unclamped, on white-balanced data:
   1. Camera → XYZ(D50).
      - With a `ForwardMatrix`: M = FM · D · inverse(AB · CC) · inverse(W), which reduces to FM/k when AB · CC is
        diagonal.
      - Without one: M = CA · inverse(AB · CC · CM) · inverse(W), where CA is the linear Bradford adaptation from
        the white xy to D50.
      - With two or three illuminants, CM, CC, FM and the HueSatMap data interpolate linearly in inverse correlated
        color temperature, as the spec defines.
      - Non-DNG files use LibRaw's `cam_xyz` (the `adobe_coeff` D65 matrix) as a single-illuminant CM through the
        no-ForwardMatrix formula. `rgb_cam` is not used.
   2. → linear ProPhoto → **HueSatMap**, honoring `ProfileHueSatMapEncoding`.
   3. → exposure = `BaselineExposure` + `BaselineExposureOffset` + user EV.
   4. → **LookTable**, honoring `ProfileLookTableEncoding`.
   5. → Rec.2020 linear working space.

   RedPrism does not ship Adobe DCP profiles and does not claim ACR color parity.
6. **Tone and presence**, with no exposure term, fused into one per-pixel pass except for the local operator:
   - a filmic base curve with white and black points;
   - contrast, highlights and shadows, through a local operator on log luminance computed on a ¼-resolution
     guided-filter pyramid, so its cost is independent of image size (the legacy `GuidedMatte` precedent);
   - vibrance, saturation, and a point tone curve (monotone Hermite, as in `Adjust::CurveValue`).
7. **Detail**: sharpening (a luminance unsharp mask: amount, radius, threshold) and noise reduction (luminance and
   chroma, edge-aware, on the pyramid). Noise reduction is optional in v1.
8. **Output**: working space → sRGB (the document's working space) → gamma encode.
   - Baking to `Bgra8888` adds a position-hashed TPDF dither of ±0.5 LSB, keyed by document coordinates and a fixed
     seed.
   - For a given engine, the bytes are reproducible across tiles and threads. The GPU and CPU engines agree within
     the `Rendering_RawDevelop.md` tolerance.
   - Orientation and crop apply last.

**Precision and formats**:

- The CFA is `R16_UINT`, read through SRVs.
- Raw prepare on the CFA uses `R32_FLOAT`.
- The demosaic output and the color stages (steps 3–5) use `R32G32B32A32_FLOAT`.
- Intermediates after the working-space conversion use `R16G16B16A16_FLOAT`.
- Kernels **read SRVs and write UAVs, ping-pong style**, so only typed UAV *stores* are needed. FL11_0 guarantees
  those for these formats; the P1.3 probe confirms them. Typed UAV loads are never required.
- Shader math is fp32; `min16float` is not relied on.
- The stage cache's VRAM budget counts the fp32 demosaiced tiles: 16 B/px, ≈ 720 MB for a fully resident 45 MP
  image. It keeps only the visible levels resident.

**Tiling, level of detail and region of interest**:

- The pipeline runs on 1024² tiles plus the halos above.
- The develop keeps a pyramid: full resolution, then the ½ or ⅓ superpixel level, then halvings.
- An interactive change renders the visible region at the view's level. When the pointer settles, a Refinement job
  renders the visible region at full resolution.
- The whole image is rendered at full quality only for bake and export.

**Stage cache**: a change re-runs only from the earliest stage it affects.

| Change | Re-runs from |
| --- | --- |
| Tone, color or curve | Step 6, from cached tiles |
| Exposure or profile tables | Step 5 |
| White balance | Step 1.6, plus the highlight pre-pass |
| Detail | Step 7 |

### 6.5 CPU engine

`Common/RawDevelop` holds three things:

- the fp32 scalar reference;
- the AVX2/NEON tile kernels for the same stages (`Kernels_Avx2_RawDevelop_*` / `Kernels_Neon_RawDevelop_*`), used
  on WARP or when the probe fails;
- the half-float helpers.

The CPU engine meets G1. It is expected at 20–40 ms per full-resolution per-pixel pass at 45 MP on R1, and it is
the parity reference for the GPU.

### 6.6 Document integration

- **Opening a RAW** (the open command, drag-drop, a positional path) goes through `OpenPaths` and its image-open
  prompt: new project, or layer in the current document. Until the develop is ready, the canvas shows the embedded
  preview with the status "Preview". Then the develop output replaces it.
- **The Develop panel** opens with:
  - white-balance presets, and temperature and tint;
  - exposure, contrast, highlights, shadows, whites and blacks;
  - vibrance, saturation and a curve;
  - sharpening and noise reduction;
  - a live histogram of the develop output: a GPU reduction with integer bins, reading back 4 KiB per settled change;
  - OK and Cancel.
- **OK bakes** the full-quality develop into a `Bgra8888` layer through a Commit job, and records one history entry,
  "Develop RAW".
- **Cancel** leaves the target document unchanged. For a new project, it closes the project.
- **Develop settings** travel with the layer only once D6 (the develop layer, `.prism` v8) is decided. v1 does not
  change the schema.
- **While the Develop panel is open**, the document is a pending edit, like any panel: painting and history wait,
  and the view stays live.

### 6.7 Caches and multiple files

- **In-memory develop cache**: the decoded planes and the demosaiced pyramid of the open RAW, bounded by the memory
  and VRAM budgets, and freed on close or under pressure. There is no disk cache in v1.
- **Several RAWs opened together** go through `OpenPaths`, with one image-open prompt. The first RAW becomes the
  document (New Project) or a layer (Add Layer). The others are placed as layers through Develop panels shown one at
  a time, in submission order. Probe, preview extraction and decode run ahead in parallel within the memory budget.
- **Multi-document windows** are out of scope. Application owns one `DocumentSession`.

### 6.8 RAW goldens (D8)

**Synthetic DNGs** are generated by IoTests from code, as the rule that binaries are not committed requires.

- **Contents**: 16-bit RGGB and X-Trans CFAs and a linear DNG, with known values for:
  - `LinearizationTable`, `BlackLevel` with its deltas, and `WhiteLevel`;
  - `AsShotNeutral`, plus `AnalogBalance` and `CameraCalibration` that are not the identity;
  - `ColorMatrix1/2` and `ForwardMatrix1/2`;
  - `HueSatMap` and `LookTable` in both encodings, and `BaselineExposure`;
  - each supported opcode;
  - a flat field, an edge and a clipped patch.
- **Expected values** are analytic, from the DNG equations:
  - linearize, black (with deltas), scale and each opcode are exact;
  - an as-shot neutral patch maps to XYZ D50 (0.9642, 1, 0.8249) within 1e-5, on both matrix branches;
  - the table order and both encodings match;
  - the `BaselineExposure` offset is applied;
  - every demosaic method and pattern gives flat-field equality and full channel coverage;
  - edge profiles stay within each method's documented bound;
  - clipped highlights are handled;
  - the dither has the expected statistics.

**Other goldens**:

- **Decoder bit-exactness**: RedPrism decoders against LibRaw's output (identity-curve `raw_image` for DNG), on the
  synthetic files and on the corpus.
- **Tiling**: tiled develop equals untiled byte for byte on the CPU engine, and within tolerance on the GPU.
- **GPU vs CPU reference**: per stage and per channel, |gpu − cpu| ≤ 1e-3 · |cpu| + 2^-16. In the baked 8-bit
  output, ≤ 1/255 on 99.9 % of pixels and ≤ 3/255 max. `Rendering_RawDevelop.md` names these numbers.
- **Corpus**: `Tests/Fixtures/raw-corpus.json` lists CC0 files from raw.pixls.us by camera, format and SHA-256. The
  files are materialized under the fail-closed `X:\RedPrism.Perf\corpus`, which P0.1 implements. Tests that need the
  corpus SKIP when it is absent, as the Fluent-font check does. Each P4/P5 closeout record shows that the
  corpus-dependent tests ran (not SKIP) in x64 ASan Debug.

## 7. Faster-than-Photoshop protocol

**Setup**

- **Machine and software**: R1 first (R2 and R3 per D9).
  - Photoshop and Camera Raw at the current release, versions recorded.
  - Camera Raw preferences at their defaults (GPU "Auto"; the cache size is recorded). Photoshop's "Use Graphics
    Processor" on.
  - RedPrism Release x64 at the measured commit, with the ISA tier, pool size and FMA3 state recorded.
- **Matched work**:
  - Camera Raw's workflow options are pinned to sRGB, 8 bits/channel, native size.
  - The gating pair runs with sharpening 0 and noise reduction 0 in both applications.
  - A second pair at each application's default detail settings is recorded. For Camera Raw that is sharpening 40
    and color noise reduction 25. That pair carries a ratio only if v1 ships chroma noise reduction.
- **Files**: one ~45 MP file per §1.2 class, each within the 100 M-pixel budget, given to both applications.
  Record-only extras:
  - one file per other v1 format: CR2, RW2, ORF, PEF, compressed ARW, uncompressed RAF;
  - one 100 MP-class file, to record the `TooLarge`-plus-embedded-preview path (M1).
- **Cache states**:
  - *Warm*: the same file reopened with no purge. M1, M2 and M4 gate on warm.
  - *Cold*: the application is running and has already opened a different file of the same format. Before each run
    the operator empties the standby list (RAMMap `-Es`, elevated) and purges the Camera Raw cache (Camera Raw
    Preferences → Performance → Purge Cache). Cold is recorded, not gated.
  - RedPrism's cold run: `--bench raw --bench-cold --bench-prime <other file> --bench-input <file>` opens the prime
    file, waits for the script's event after the purge, then opens the timed file.

**Timing**

- **Photoshop**: a checked-in UXP script, `Build/bench/photoshop-raw.psjs`, run via File > Scripts > Browse or the
  UXP Developer Tool.
  - It sets `app.displayDialogs = DialogModes.NO`, opens inside `executeAsModal`, and closes each document without
    saving before the next run.
  - It times with `Date.now()`, unless P4.8 confirms that `performance.now()` exists in UXP.
  - **M2**: `app.open` with default settings (Puget's "File Open – RAW").
  - **M4**: `app.open` of the same file with Puget's Camera Raw tone and presence values in an XMP sidecar: exposure
    −0.05, contrast 34, highlights −18, shadows 20, whites −30, blacks 3, vibrance 37, saturation 19. White balance
    stays as shot in both applications, because Puget's temperature and tint are relative adjustments for rendered
    documents and have no RAW equivalent.
  - **M5**: save as JPEG quality 10 (4:4:4).
  - **G2 filters**: Gaussian Blur, Levels, Hue/Saturation, Motion Blur and Image Size on the same 45 MP 8-bit
    document.

  The script only automates Photoshop. It is not a RedPrism test and never runs in `test.ps1`.
- **RedPrism**:
  - `RedPrism.exe --bench raw --bench-input <file>` measures:
    - **M1**: QPC from the open request to the `Present` that first shows preview pixels.
    - **M2**: from the open request to the `Present` of full quality after the bake. The bench issues OK with the
      open request, so the bake Commit queues behind the develop and M2 contains no idle time.
    - **M3**: a scripted slider sweep, from input timestamp to `Present`.
    - **M4**: the M2 path with the same values from `--bench-develop <json>`.
    - **M5**: JPEG with `WICJpegYCrCbSubsampling444`, at the `ImageQuality` whose file size is within ±5 % of
      Photoshop's.
  - `--bench filters --bench-input <tiff>` covers G2 on the document Photoshop used.
  - Frame timing is cross-checked with PresentMon (ETW), never with screenshots or screen capture.

**Reporting**

- **Output parity**: renderings differ, because Adobe's profiles are Adobe's. The report says so. Correctness is
  proven by §6.8, not by comparison with Photoshop.
- **Report**: `Specs/TestRuns/RawVsPhotoshop_<date>.md` and `Filters_<date>.md`, in the brush-record format:
  - the scenario, the `BENCH MACHINE` line, and both applications' versions and settings;
  - warm and cold medians and p95, and the ratio per metric;
  - the reproduce commands.

  `docs/` may claim "faster than Photoshop" only for the metrics, classes and machines where a record shows it.

## 8. Testing and validation

| Suite | Additions |
| --- | --- |
| HostTests | `SelectHostFrameAction` rows: `completionsPending` (P0.6a); `gpuWorkPending` and `SubmitGpuWork`, including hidden + `gpuWorkPending` (P3.1). `BusyIndicatorVisible` (the legacy `quickOperationsNeverDimTheInterface` as a pure function). The continuation-queue and stroke-chain state machines. The end-session policy table. CLI rows: the `--bench` scenarios and `--bench-input`, `--bench-develop`, `--bench-cold`, `--bench-prime`; `--isa`; `--crash-probe <dir> --crash-probe-worker` (and `--crash-probe-worker` alone refused); positional paths. The `X:\RedPrism.Perf` resolver. The **pump-while-job gate test** (below) |
| DocumentTests | The Prepare/Run/Apply split, with the direct `Commit*` APIs unchanged. The worker-vs-direct matrix (below). Exhaustive (v, a) LUT equality. Exhaustive integer-blend (c, o, s) tests per mode. Gradient tier equality, linear and radial. Staleness: an apply after Cancel, after an intervening edit, after Undo, of an older sequence, after the mask was edited or the layer moved, and a stale selection job are all refused. Wand-then-merge; settle-then-invert. Stroke chaining, including a stroke started during a pending filter commit, which applies on the filtered image. Continuation replay order and `documentSerial` drops. A `Generation()` bump for every mutation path. `DuplicateActiveLayer` refused over the pixel budget. Right after Add Mask from a selection with a placement, `MaskEdgeBackground` reads the new mask's tone (0 for hide-all), and a filter commit applied before any Background job bakes 0 outside the placement. Ported legacy tests: `stalePreviewCannotReturnAfterOffOrReopen`, `projectOperationsBlockEditsAndQueueImageImports` (adapted to the queue semantics) and `queuedImportsAreNotLost`. `hsv.preview-*` keep passing. The D12 histogram redefinition: legacy histogram goldens pass within 1e-5, and AutoLevels outputs are identical on those inputs. A `--stress` mode runs `Raster::Data()` against `ReadRegion` and `MakeThumbnail` on one sparse raster, and on two rasters that share a base. Reference hashes (below); the log prints the ISA tier and `_get_FMA3_enable()` |
| CompositionTests (WARP) | Capability probe assertions. Fence and `EnqueueSetEvent` completion. The readback ring (1×1, ROI, tiled) and the upload ring. Per-layer adjust draw parity against `RunFilter` + `BlendThrough` on a translated, scaled layer with a selection, grain included. Compute-preview parity per filter, with named tolerances. Tiled canvas and residency equal the untiled result. A document-coordinate compose for every existing golden. RAW develop, GPU against the CPU reference (mixed tolerance), and tiled against untiled |
| IoTests | Codecs on an MTA thread. ZIP STORE round trip, and loading old deflated archives. Folder-project parallel save. Per-path save serialization, unique temp names and stale-temp cleanup. Background save with `MarkSaved(revision)`. Atomic export (the old file survives a failed export). EXIF orientation and ICC goldens (port legacy `orientationAndColorConversion`). Synthetic DNG goldens (§6.8). LibRaw wrapper statuses. An ASan stress run: one RAF, one CR3 and one pana8 RW2 decode concurrently on an N-thread pool, then again with a cancel mid-decode, and again with in-memory truncated copies; each returns a status and nothing leaks (the files come from the corpus). RedPrism decoders bit-exact against LibRaw |
| FuzzTests (new) | Targets: the RedPrism LJ92 and LUT Huffman decoders, the DNG tag/IFD reader and the LibRaw wrapper. Each runs `-runs=<N> -seed=<fixed>` over a seed corpus the test generates. Every crash found is reproduced as a code-generated hostile-file case in IoTests; minimized inputs stay under `.build/` and are never committed. The project maps all six profiles; `test.ps1` runs it only in x64 ASan Debug |
| `--self-test --warp` | `SettleJobs(timeoutMs)` (below). Each synchronous assertion becomes `command; SettleJobs(); check`. **Gated Gaussian blur on a 4K layer.** Run 1: OK. A splitter drag, a render with `CompositeCount` + 1, and Escape all work; no entry is recorded and the layer is unchanged. Run 2: reopen the panel, Enter, open the gate, `SettleJobs`: one entry whose pixel hash equals the hash DocumentTests computes for the same generated input. Merge Down and Ctrl+Shift+C settle through `SettleJobs`. A clean document with a live stroke, then Close, prompts after the settle entry applies. `RecoverDeviceForTest`, and a forced adapter rebind through the `CheckDeviceAdapter` path, while a gated readback holds a lent slot: no access to freed memory under ASan, and after `SettleJobs` the job has restarted on the new device and applied exactly once. A self-test failure exit with a gated job in flight runs `Shutdown` before `CoUninitialize` |
| Crash probe | `--crash-probe <dir> --crash-probe-worker`: two pool threads fault together; exactly one non-empty dump, plus `pending.txt` |
| Stress (HostTests `--stress`, ASan Debug, no device) | Seeded submit/cancel/supersede/close sequences. Admission contention. An allocation failure injected after admission gives a notice, no entry and no terminate. `ParallelFor` with N jobs on N threads, on 1 thread, nested, and closed mid-run |
| Race detection without TSan | Concurrency SAL (`_Guarded_by_`, `_Requires_lock_held_`) on the scheduler, pool and caches, plus an `/analyze` pass (`build.ps1 -Analyze`, P0.5). A documented Application Verifier run (Locks, Handles, Heaps, Threadpool) over `HostTests.exe --stress` at each phase closeout, recorded in TestRuns |
| VEX scan | `build.ps1` in every x64 configuration (§4.8) |
| Bench (not gated) | The P0.1 baseline, then each phase's record under `Specs/TestRuns/` |

**Pump-while-job gate test (HostTests).** The test uses a message-only window (`HWND_MESSAGE`, never visible) and
the real `JobScheduler`.

1. A stand-in job waits on a gate event.
2. The test posts a sentinel message, then runs `PumpMessages` and `DrainCompletions` from `Common/Jobs`.
3. The test asserts three things: the sentinel dispatches while the job has started but not finished, the
   sentinel's handler opens the gate, and the completion applies exactly once.
4. A cancelled run yields zero applies and zero live payloads.

The only timeout is a failure bound; the settings self-test precedent uses 5 s. The test-harness skill's "no window"
note becomes "no visible window, no device".

**Worker-vs-direct matrix (DocumentTests).**

- **Every configuration**: every `FilterKind` and `AdjustmentKind`, crossed with:
  - sizes {1×1, 257×131, 4099×3, 300×530}, where 300×530 crosses 256 px tile edges both ways;
  - sparse and contiguous sources;
  - with and without a selection;
  - executors {inline, 1, 2, N};
  - every ISA tier the host has.

  Settings are bounded: σ ≤ 3, motion distance ≤ 8, and a content-aware hole of 1 % of the size.
- **Release only**: 4096×4096, with executors {1, N} and every tier. Its scalar reference is computed once per input
  and compared by hash within the run.
- `Testing_Validation.md` records a per-configuration time budget for the matrix.

**Reference hashes (DocumentTests).**

- They cover the pixel stages only. The pixel stages are fed checked-in bit patterns for the parameters: blur
  weights, the Levels/Curves/Exposure float tables, and motion dx/dy. The native ARM64 run therefore proves
  x64 = ARM64 for the pixel stages.
- Gaussian Add Noise, the brush tip density, Spot Healing's texture grain and the radial gradient call the CRT per
  pixel. They are excluded, or get one hash file per architecture.

**SettleJobs (self-test).** It repeats these steps until no job, GPU slice or completion is pending:

1. `DrainCompletions` and `RunUiActions`.
2. From P1.3 on, `GpuWorkQueue::Pump` (the `SubmitGpuWork` path, without Present).
3. `MsgWaitForMultipleObjectsEx` on `jobWake` with `QS_TIMER | QS_POSTMESSAGE`, dispatching timer and posted
   messages.

The timeout is only a failure bound.

**Closeout matrix**: every gate runs in the usual matrix: `validate-skills.ps1`, `format.ps1 -Check`, `test.ps1` x64
Debug, Release and ASan Debug, and `build.ps1 -Platform ARM64`. NEON runtime claims need R3 or another native ARM64
host. The existing ARM64 ASan restore gap is recorded as a known exception until an arm64 ASan overlay triplet
exists (`vcpkg-install.ps1` throws for non-x64 `-Asan`).

## 9. Spec changes

| Spec | Change | Slices |
| --- | --- | --- |
| `AGENTS.md` | Architecture ownership rows and the threading rules summary, as each owner lands. The D8 golden rule for new behavior. The job runner as a permitted `bad_alloc` translation boundary that fails the job. The dependency stack line (LibRaw). The composition rule is unchanged: document-space tiles keep "composites only when the document changed" | P0.5 (`Executor`, `WorkerPool`, `RedPrism.Waits`, D8), P0.6a (`JobScheduler`, `Jobs`, the runner boundary), P0.8 (`Kernels`), P1.3 (`GpuWorkQueue`), P4 (`RawDecode`, `RawDevelop`, LibRaw) |
| `Core_PerformanceAndResources.md` | UI-thread duties and the 4 ms rule. Job classes, coalescing, cancel intervals and the non-cancellable list. `MemoryBudget` and admission. No polling of GPU or system events. The GPU frame budget. The hidden-window amendment (§4.7). Reliability targets. `GPU raster residency` names the off-screen composite cache and develop tiles, with caps. `History retained bytes` (D13). `Document pixels` only if D14 changes. `X:\RedPrism.Perf` implemented | P0.1, P0.6a, P0.7, P1, P1.3, P1.9, P3, P4.5 |
| `Core_Architecture.md` | Ownership rows; Document code runs on workers; the `SelectHostFrameAction` rows; COM MTA usage; `Application::Shutdown` order; HostTests may create a message-only window but no device | Same slices as the `AGENTS.md` rows, plus P3.1 (frame action) |
| `Core_CrashHandling.md` | Multi-thread filter (owner id; other threads block); a per-thread terminate handler on pool threads; `--crash-probe-worker` | P0.5 |
| `Document_Model.md` | The atomic assembled pointer, then no retained assembly. `PixelBuffer`/`Adopt`. `Generation()`. Shared selection handles. Deferred release. Mask thumbnails eager, image thumbnails on demand. The `DuplicateActiveLayer` budget check | P0.2, P0.3, P0.4, P0.7, P1.7, P1.9 |
| `Document_History.md` | `MarkSaved(revision)`. Moves instead of copies. Incremental accounting. Selections counted. The host passes an explicit `uint64` limit (D13), while the 256 MiB default and `history.trim-256mib` stay | P0.4, P1.9 |
| `Document_Tools.md` | The panel protocol (§5.1). The busy state, continuation queue, stroke chaining and selection lane. Preview policy: per-layer adjust draw and view-resolution compute previews (D12). Gaussian large σ (D4). The histogram redefinition and its source (D12). The integer-blend proof. Content-aware fill run once. Selection coverage caching. The distort preview on the GPU. Shapes as jobs | P0.6b, P1–P3 |
| `Rendering_Composition.md` | The per-layer adjust draw pass mode and its tolerance. Off-screen utility composites. Readback/upload rings and the lent-slot and retiring-device rules. Fences. Tiled residency. Document-space canvas tiles with per-tile mips. The VRAM budget. The compute-filter tolerance table | P1.2, P1.3, P3 |
| `Rendering_Brush.md` | GPU tile composition, a fenced deferred readback, touched-tile mips, O(n) `Replacing`, and the Stroke lane; the WARP compute engine stays. The History row: "the 256 MiB history cap applies" becomes "the session's history limit (D13) applies" | P1.4, P1.9, P3 |
| `IO_ImageCodecs.md` | Codec policy: WIC for JPEG/PNG/TIFF/HEIF, the LibRaw overlay port for RAW, and RedPrism RAW decoders. (D11) a fast PNG encoder. EXIF orientation and ICC. Banded encode. The JPEG subsampling option. New statuses | P1.7, P4 |
| `IO_ProjectFormat.md` | ZIP PNG entries stored, and readers accept both. A flush before the existing atomic replace or rotation (D17). Unique temp names and stale-temp cleanup. (D6) a v8 develop layer | P1.7, P6 |
| `IO_RawDecode.md` (new) | Layouts, formats, statuses and limits; preview selection; the DNG tag reader; the LibRaw patch contract and curve handling; decoder bit-exactness; security; the corpus | P4 |
| `Rendering_RawDevelop.md` (new) | The Develop 1 math and stage order; precision; tiling and halos; the highlight pre-pass partition; LOD/ROI; the stage cache; tolerances; dither | P4 |
| `Specs/README.md` | Domain entry points for `IO_RawDecode.md` and `Rendering_RawDevelop.md` | P4 |
| `Build_Toolchain.md` | Intrinsic tier TUs at the default `/arch`. The VEX scan and its allowlist. `-Analyze`. The dependency table: the LibRaw overlay port (P4), libdeflate (P1.7, if D11), libjpeg-turbo (P4, if D5), libjxl (P5.6). Copied DLLs. FuzzTests in the solution shape | P0.5, P0.8, P1.7, P4, P5 |
| `Build_Packaging.md` | LibRaw DLL staging and notices (LGPL/CDDL + BSD-3), with the patch source offer. The DNG attribution line if DNG support is advertised | P4 |
| `Testing_Validation.md` | New suites and gates (§8), matrix time budgets, and the D8 golden rule | P0.5–P5 |
| `UI_ApplicationChrome.md` | Busy indicator placement and the 250 ms rule. Progress bars, "Applying…", OK gating, the refused-while-busy status. The Develop panel and the preview badge. Every new chrome text is a STRINGTABLE entry with positional `{0}` placeholders, added to `resource.h`, `RedPrism.rc` and the HostTests `kIds` gate in the slice that introduces it: busy, saving and close-after-save, opening, selecting, stale selection dropped, applying, histogram loading, the preview badge, out-of-memory, the RAW notices, the end-session block reason, clipboard busy, the Develop panel labels, and the `IDS_A11Y_*` names | P0.6b, P1, P4 |
| `UI_Windowing.md` | `WM_CLOSE` deferral during a save; `WM_QUERYENDSESSION`/`WM_ENDSESSION`; the `--bench` values and options; `--isa`; `--crash-probe-worker`; positional paths. The switch table catches up with `--demo`, `--bench` and `--crash-probe` (existing drift) | P0.1, P0.5, P0.8, P1.7 |
| Skills | `performance-resources` (jobs, admission, budgets). `hlsl-composition` (the per-layer adjust pass, compute passes, SRV-in/UAV-out, slicing). `direct3d11-rendering` (fences, rings, lent slots, the probe, the `ID3D11DeviceContext1` decision). `test-harness` (SettleJobs, the gate test, the message-only window, stress, matrix budget). `build-redprism` (tiers, the VEX scan). A new `raw-pipeline` skill in P4. `validate-skills.ps1` passes | Each phase |

## 10. Phases and slices

Each slice lands with its spec text and tests. A slice that claims speed also lands a TestRuns record. A slice that
makes anything slower on R1 is reverted, unless the cost is intentional, measured and justified in its record. D17's
flush is one such case; another is job-dispatch latency traded for UI-thread time in P1.

### P0 — Foundations

**P0.1 Baseline and instruments** (ACTIVE)

- The bench scenario table (§4.9), with synthetic 4K, G1 A/B and 45 MP documents.
- The machine line, the UI stall histogram, the ETW provider and `.wprp`, and the `X:\RedPrism.Perf` resolver.
- Record `Specs/TestRuns/PipelineBaseline_<date>.md` on R1 and W. It holds:
  - every row of §5, and the 45 MP brush row;
  - the demand-zero fault time for a 179 MB result;
  - the worst case of each non-cancellable call;
  - cold and warm read times;
  - whether G1 document B composes on W;
  - the pool size, logical versus physical cores.
- **Exit**: every "today" number in §1.2 is a measurement, and each §5 operation is classified S or not.

**P0.2 Raster thread safety** (COMPLETE on x64, 2026-09-27). The atomic assembled pointer (§4.6), and the
DocumentTests `--stress` race. **Exit met**: all x64 gates green, including 256 rounds in ASan Debug; goldens unchanged.

**P0.3 `PixelBuffer`, `TryAllocate`, `Raster::Adopt`** (COMPLETE on x64, 2026-09-27). Converted `CopyPixels`,
`Adjust::ApplyAdjustment`, `RunFilter`, `ImageFilters::*`, `CopyFrame` and `Readback` to adopt. **Exit**: bytes
unchanged, one full copy fewer per result, and a measured drop in result-production time within the commit path.
The [foundation record](../../TestRuns/PipelineFoundations_2026-09-27.md) records the paired timing and checksum
evidence. End-to-end host commit latency, including history, upload and Present, still belongs to P0.1/P1's gates.

**P0.4 Generation and tokens** (COMPLETE on x64; all ARM64 cross-builds passed)

- `DocumentSession::Generation()` and `EditForTest`.
- Migrate every host and DocumentTests write through the mutable `Document()` (the files, self-test, demo, bench,
  size and merge helpers), then remove the overload.
- `MarkSaved(revision)`, the session-local `documentSerial`, and selection loops as `shared_ptr<const>` handles.
- The `DuplicateActiveLayer` budget check, refused with a status notice. Bench fixtures are built only through
  validated paths.
- **Exit**: DocumentTests covers a generation bump for every mutation path.

Completed, 2026-09-27: const document/layer views, explicit import/opacity paths, shared immutable
selection storage, owning tokens and captured saved revisions are implemented. A restore serial also invalidates
layer results after Undo/Redo; checking only current image identity could otherwise revive an old job. Token
selection checks compare retained handles in constant time. The scheduler's cancellation/open-edit checks and
Prepare/Run/Apply wiring still belong to P0.6/P1. The complete x64 Debug, Release and ASan gates passed, with 105
generation/ownership checks, 15 saved-revision checks and unchanged legacy goldens. All three ARM64 profiles
cross-build; native runtime remains untested. See the [P0.4 record](../../TestRuns/SessionGenerations_2026-09-27.md).

**P0.5 Pool and crash handling** (COMPLETE on x64, 2026-09-27; D8)

- `Executor`, `WorkerPool`, `RedPrism.Waits`, `ParallelFor` with the deadlock-free join, cancel tokens, QoS, the WIL
  aliases, and the MTA cookie.
- The multi-thread crash filter, the per-thread terminate handler, and `--crash-probe-worker`.
- Concurrency SAL and `build.ps1 -Analyze`.
- The D8 AGENTS amendment.
- **Exit**: the §8 `ParallelFor` stress passes, the cancel interval is ≤ 1 ms, output is deterministic across
  executors, and the worker crash probe passes.

**Exit met for the executor foundation**: all x64 runtime gates, 64 additional ASan stress rounds, both crash
probes and full Release native analysis passed. The checked synthetic row polls at an observed maximum interval
of 16.2 µs. Every image kernel still has to qualify its own polling interval when migrated. See the
[worker record](../../TestRuns/WorkerExecution_2026-09-27.md); AppVerifier and native ARM64 remain qualification limits.

**P0.6a Scheduler core** (D8)

- `JobScheduler` with lanes, records, the runner and its `bad_alloc` boundary.
- Intrusive completions, `jobWake`, and `PumpMessages`/`DrainCompletions`/`RunUiActions`.
- The `completionsPending` frame input.
- `Application::Shutdown` and the `Main.cpp` order.
- `SettleJobs` (drain, UI actions, and a wait on `jobWake`; the `GpuWorkQueue::Pump` step joins in P1.3).
- **Exit**: the HostTests gate test passes, the self-test settles, and a close during a gated job leaks nothing on
  both the normal and the failure exit paths.

**P0.6b Busy state** (D2, D3, D8)

- Preview mailboxes.
- The continuation queue and stroke-chain state machines.
- The busy indicator.
- The end-session policy table (a pure function, HostTests).
- The first strings.
- **Exit**: the state-machine HostTests and DocumentTests pass.

**P0.7 Memory** (D8, after P0.6a)

- `MemoryBudget`, and admission on the submit and release paths.
- The deferred-release sink.
- The recycle pool, if P0.1 justifies it.
- **Exit**: admission and allocation-failure stress are green, and the `UiFree` trace shows no free of 16 MiB or
  more on the UI thread.

**P0.8 Kernel dispatch skeleton** (ACTIVE). `KernelTable` with the scalar tier, `--isa`, and the VEX scan in
`build.ps1` with its allowlist. **Exit**: the current binary passes the scan with only the STL/CRT allowlist in every
x64 configuration; goldens unchanged.

### P1 — Nothing blocks (placement changes; kernel math unchanged)

- **P1.1 Adjust and filter panels** (§5.1):
  - the split API, the preview and commit lanes, and preview reuse;
  - the host-path histogram as a job, with the direct API kept inline;
  - transform and mask checks in every `Apply*`;
  - the panel busy UI;
  - **settle tickets, and `ConfirmDiscardChanges` as a UI action for every caller (§4.4)**.
- **P1.2 Per-layer adjust draw**: the new pass mode, a DYNAMIC LUT and a DEFAULT cube updated in place per tick, and
  the coverage input. This also ends the per-tick IMMUTABLE texture creation for live adjustment layers.
- **P1.3 GPU completion infrastructure**:
  - `GpuWorkQueue` is created, and `Pump` is added to `Run` and to `SettleJobs`;
  - the capability probe, fences and `EnqueueSetEvent`;
  - the readback ring with its lent-slot and retiring rules, off-screen utility composes (recorded whole), and the
    1×1 sampler readback;
  - merge, `CompositeBelow`, export, Copy Merged and the wand/clone all-layers sources move to R + J;
  - the hidden-window amendment and the device-rebuild tests.
- **P1.4 Strokes**:
  - the Stroke lane and chaining, including chaining behind pending commits;
  - lazy tiled clone/blur/warp sources, async heal, and a single-pass `FinishWarp`;
  - the async mouse-up snapshot.
- **P1.5 Raster edits, transforms and shapes**:
  - Invert, Fill, Clear and Delete;
  - the gradient (Interactive lane until P3), pixel move, and selection Free Transform;
  - the distort commit, and the distort preview interim (§5);
  - `OnCommand`'s transform settle as a settle job, with the command in the continuation queue;
  - Shape tool mouse-up and `RedrawShape`;
  - Add Mask from a selection and Layer via Copy, with the mask thumbnail computed in the job.
- **P1.6 Selection**: the Selection lane (wand, load, expand/contract/inverse, booleans), the coverage cache and
  bounded coverage, and the `DisplayedSelection` cache.
- **P1.7 Files**:
  - async open, import, save, export and clipboard, with Paste entirely in an Io job, bounded clipboard retries, and
    the New Canvas card prefill;
  - save-before-proceed continuations, `WM_CLOSE`, and the end-session wiring;
  - ZIP STORE, the folder-project save, per-path serialization, unique temps and cleanup, and the flush (D17);
  - `ParallelFor` encode/decode, banded encode without `Data()`, and atomic export;
  - path classification off the UI thread, EXIF orientation and ICC;
  - positional paths (`RedPrism.exe <path>…` through `OpenPaths`; `CommandLine.h`; HostTests rows).
- **P1.8 Size**: Image Size and the Canvas Size fill as Commit jobs.
- **P1.9 Residual UI costs**: incremental history accounting and moves, the host-chosen history limit (D13),
  image-layer thumbnails on demand, and the upload ring for first uploads.

**P1 exit (G1)**, recorded in `Specs/TestRuns/NeverBlocks_<date>.md`:

- **R1**, with G1 documents A and B: every §1.2 reliability row that is due at P1 holds for every §5 row except the
  brush update and the distort preview (P3 exit).
- **W**: the UI-thread and apply rows hold, with A, and with B unless P0.1 deferred it.
- **R2 and R3**: recorded (D9).
- The self-test covers every moved command, and the ported legacy async tests pass.

### P2 — Fast exact CPU kernels

- **P2.1 Tier infrastructure**: intrinsic tier TUs (SSE2, AVX2, NEON; AVX-512 per D16), the rounding and emulation
  helpers, the half-float helpers, and the matrix harness.
- **P2.2 Tables and reductions**:
  - Levels/Curves/Exposure (v, a) LUT, gradient map, cube, fused BlendThrough, and downscale;
  - coverage resolve by rows, and AlphaBounds;
  - the integer histogram, with its spec change. Per-channel bins are Σ a·m in `uint64` divided once by 65025 when
    read; the RGB bin is (N_r + N_g + N_b) / (3 × 65025). The legacy histogram goldens pass within 1e-5, AutoLevels
    outputs are identical on those inputs, and the integer form is the scalar reference for the tier and thread
    matrix.
- **P2.3 Blurs, noise and grain**:
  - the Gaussian: SIMD across pixels in double, with the same tap order and a banded intermediate;
  - once D4 is decided, the σ > 32 approximation with its ≤ 2/255 test against the direct kernel;
  - motion blur, lens, grain, uniform noise, and Gaussian noise (threads only).
- **P2.4 The PaintCanvas family**:
  - templates instead of `std::function`, and nested loops instead of div/mod;
  - integer blends for Invert, Fill, Clear and Delete;
  - the gradient: linear lane-wise in double, radial with scalar `hypot` and threads only;
  - `ComposeTile`, `MoveLifted`, `LiftSelection`, the `MergeFloating` footprint, and `ShapeImage`;
  - the `RenderLayerPixels` identity and integer-translation fast path (memcpy);
  - the distort warp by rows, and `RenderPlaced` with separable weights and identical math.
- **P2.5 Content-aware fill and heal**: SIMD patch SSD with the same summation order, and a precomputed patch-known
  mask. Early exit only where it is proven to keep the same argmin and ties. Heal `Score` runs in parallel.
- **P2.6 Conversions**: premultiply/unpremultiply tables; BGRA→BGR (AVX2 `vpshufb`, NEON `vld4`/`vst3`, SSE2 shifts
  and masks); the DIB paths.

**P2 exit (the CPU half of G2)**: the small matrix is green in all three configurations, and the Release-only 4096²
matrix is green on every tier. `Specs/TestRuns/Kernels_<date>.md` shows the §1.2 commit targets on R1 and the
speedup of each tier over scalar.

### P3 — GPU acceleration

- **P3.1 Slicing**: `GpuWorkQueue` slicing with timestamp budgets; `gpuWorkPending` and `SubmitGpuWork` in
  `SelectHostFrameAction`, with their HostTests rows; device-loss restart; VRAM budget events. Utility composes
  become sliced.
- **P3.2 Compute previews**:
  - Gaussian (groupshared separable), motion, lens, noise, gradient shade;
  - the pixel-move composite, the distort projective pass, and clone/blur composition;
  - brush tile composition on the GPU, with touched-tile mips.

  Each has a named tolerance in `Rendering_Composition.md` and a WARP parity test.
- **P3.3 Tiled residency**: support above 16384 px, document-space canvas tiles with per-tile mips, and the
  document-coordinate test compose.
- **P3.4 Worker-side texture creation** (gated by `DriverConcurrentCreates`), and `WriteToSubresource` on UMA.
- **P3.5 GPU commit engines for blurs** with property goldens: a D1 option, taken only if P2 misses its targets.

**P3 exit**:

- **R1**: the §1.2 preview and GPU-budget targets hold, the brush 800 px update is ≤ 4 ms on the UI thread, and the
  distort preview is ≤ 1 frame.
- **W**: the frame row holds with a 4K document, against the §1.2 W targets.
- **Size limits**: the panorama composes, and G1 document B composes on a 2 GB-VRAM adapter without closing the
  window.
- **R2 and R3**: recorded (D9). On R2 a cross-adapter present is measured for D10.

### P4 — RAW v1 (needs D1, D5, D6 v1, D8, D14, D15)

- **P4.1 Specs and the port**: the spec changes (new specs, codec policy, toolchain, packaging, the AGENTS stack
  line, the `Specs/README.md` entry points); the overlay port with the full patch contract (§6.2); the ASan tree;
  notices.
- **P4.2 Wrapper and entry points**: the `RawDecode` wrapper with layouts, statuses, limits and cancel; curve
  handling; the DNG tag reader and its fuzz target; the dialog filter extensions (`FileDialogs.cpp`
  `kImagePatterns`); drag-drop; RAW through the positional path.
- **P4.3 First pixels** from the embedded preview (M1).
- **P4.4 CPU reference**: the `RawDevelop` CPU reference and the synthetic DNG goldens.
- **P4.5 GPU develop pipeline**: stages, halos, pyramid and ROI, the highlight pre-pass, the stage cache, slicing,
  and WARP/CPU engine selection.
- **P4.6 Develop panel**: the live histogram, the bake Commit job, the "Develop RAW" entry, and the multi-file flow
  of §6.7.
- **P4.7 CPU SIMD develop engine.**
- **P4.8 Measurement**: M1–M5 and the §7 G2 filter pairs against Photoshop on R1 (and R2/R3 per D9), in
  `RawVsPhotoshop_<date>.md` and `Filters_<date>.md`.

**P4 exit**: all §6.8 goldens are green; M1 and M3 are met; M2 and M4 are ≤ Photoshop for CR3 and RAF on R1, with
ARW, DNG and NEF recorded.

### P5 — RAW at 0.5 × Photoshop

- **P5.1 LJ92 tile decoder** (Sony lossless ARW, DNG lossless tiles), bit-exact against LibRaw.
- **P5.2 Sequential LUT Huffman decoder** (NEF lossless, CR2). The speculative parallel decoder only if needed.
- **P5.3 CR3 tile parallelism**, if the tiles prove independent.
- **P5.4 FuzzTests gate** for all RedPrism parsers, and the hostile-file set.
- **P5.5 Faster DNG and multi-file opens**: the DNG enhanced (`NewSubFileType` 16) and Fast Load Data preview paths,
  and decode-ahead for multi-file RAW opens.
- **P5.6 DNG 1.7 JPEG XL** (optional, D5), through a RedPrism tile reader with libjxl.

**P5 exit**, all warm on R1 and recorded:

- M1 ≤ 100 ms.
- M2 and M4 ≤ 0.5 × Photoshop for all five classes.
- M5 ≤ Photoshop.

### P6 — Non-destructive develop and depth (DECISION: D6 v2, D7, D14)

The user authorized P6 on 2026-09-27. Before its implementation, a linked child WIP plan specifies migration,
memory admission, color/alpha math, compatibility and round-trip goldens for:

- `.prism` v8 develop layers (source RAW embedded or referenced, settings, a rasterize command);
- RGBA16F documents (D7);
- raising the pixel budget (D14).

Record D6 v2, D7 and D14 choices using P4/P5 measurements before this phase starts. Preserve v1/v7 read and
export behavior while introducing v8 explicitly; never mark format/depth work complete from a decoder-only test.

### Closeout

- Merge the durable text into the owning specs (§9).
- Update `docs/`: RAW, busy behavior, the Develop panel, bench usage.
- Move this plan to Done with its closeout, and remove the index row.

## 11. Decisions

The recommended option is in bold. An empty "Decided" cell means the decision is open, and it blocks the slices in
its "Gates" column.

| Id | Question | Options | Recommendation | Gates | Decided |
| --- | --- | --- | --- | --- | --- |
| D1 | Engine for committed bytes | Exact CPU for everything; GPU with tolerances; mixed | **Exact CPU (SIMD + threads) for every legacy-golden kernel; GPU for previews within named tolerances; RAW develop commits from the GPU within `Rendering_RawDevelop.md` tolerances** | P1–P4 | |
| D2 | Preview coalescing | Cancel on each change (the 2026-09-22 text); legacy 1 in flight + latest pending | **Legacy for interactive previews; only refinements are superseded** (the legacy authors measured that cancel-on-change starves the preview) | P0.6b, P1 | 2026-09-27: adopt the recommendation under the user's through-P6 implementation instruction. |
| D3 | Input while earlier work is pending | Refuse mutations; allow and drop stale results; modal; queue and replay | **Chain strokes behind any pending history-recording job; queue mutating commands, wand clicks and imports (depth 8) with snapshots taken at dequeue; refuse other canvas gestures on a layer with a pending commit or stroke; the view stays live; Escape cancels commits and selections and empties the queue** | P0.6b, P1 | 2026-09-27: adopt the recommendation; state machines land before the first asynchronous image commit. |
| D4 | Gaussian σ > 32 | Keep O(σ) direct; O(1) approximation (downsample pyramid, extended box, Young–van Vliet) | **Approximation for σ > 32, specified and tested against the direct kernel at ≤ 2/255** (blur goldens are property-based; the legacy app used Core Image's GPU blur) | P2.3, σ > 32 target | |
| D5 | RAW dependency policy | WIC Raw Image Extension (8-bit, optional, lagging); LibRaw; rawspeed (no CR3, clang-cl); Adobe DNG SDK (clang toolset, indemnity) | **LibRaw overlay port (`raw_r` DLL, no OpenMP/jasper/lcms, the callback-parallel patch) + RedPrism decoders; no lensfun; `dng-lossy`/libjpeg-turbo only if lossy DNG is wanted or WIC export misses M5; libjxl optional in P5** | P4 | |
| D6 | RAW in the document | v1: bake to 8-bit through the Develop panel; v2: non-destructive develop layer (`.prism` v8, unreadable by legacy) | **v1 bake now; decide v2 after P4 measurements** | P4 (v1), P6 (v2) | |
| D7 | High-bit-depth documents | 8-bit only; RGBA16F layers throughout | **8-bit documents; RAW develops in float and bakes with dither; revisit with D6 v2** | P6 | |
| D8 | Goldens for new behavior with no legacy counterpart | — | **AGENTS.md amendment: the owning spec names an independent reference (DNG-spec synthetic vectors, LibRaw's output for decoders, the CPU reference develop within a named tolerance, state-machine goldens for jobs and crash handling); legacy goldens remain the only source for ported behavior and are never rebaselined** | P0.5–P0.7, P4 | 2026-09-27: adopt the recommendation under the user's implementation authorization; independent state-machine expectations cover the new Windows executor. |
| D9 | Reference machines | R1 only; R1 + R2 + R3 | **Phase exits on R1; the owner provides R2 and R3 for recorded runs** | P1, P3 and P4.8 records | |
| D10 | Hybrid laptops (panel on the iGPU) | Adapter-of-monitor (today; never uses the dGPU); honor the Windows "High performance" preference with a cross-adapter present | **Keep adapter-of-monitor until the P3 exit measures a cross-adapter present on R2; then decide** | none now | |
| D11 | Save speed beyond STORE + parallel | WIC PNG only; a RedPrism PNG writer (fpng-style filter + libdeflate; output any PNG reader accepts) | **Measure after P1.7; adopt the writer only if one 45 MP layer still takes more than 1.5 s** | P1.7 target | |
| D12 | Preview and histogram policy | Keep 4096/2048/full; view-resolution GPU previews; the legacy 8000 px histogram source; an integer histogram | **View-resolution GPU previews for filters; the per-layer adjust draw for table kinds; the histogram redefined as exact integer numerators N = Σ a·m per bin in `uint64`, with bins[c] = N_c / 65025.0 and bins[0] = (N_r + N_g + N_b) / (3 × 65025.0), each rounded once, from an 8000 px source (legacy)** | P1–P3 | |
| D13 | History budget | 256 MiB fixed; scale with RAM; count selections | **The host passes min(25 % of RAM, 2 GiB) as a `uint64`; the class default stays 256 MiB for the legacy test; selections are counted** | P1.9 | |
| D14 | Pixel budgets for RAW | 100 M per document (legacy); higher | **Keep 100 M until D6 v2; any RAW whose oriented, cropped frame exceeds 100 000 000 pixels returns `TooLarge`, with the embedded preview offered** | P4, P6 | |
| D15 | Nikon HE/HE* | Ship LibRaw's decoder when it lands; refuse | **Refuse, offering the embedded preview, until a legal review of TicoRAW** | P4 | |
| D16 | AVX-512 tier | None; opt-in tier | **Add only if it is ≥ 1.3× over AVX2 on R1 for the kernel and the exactness matrix is green** | P2 | |
| D17 | Save durability | No flush (today); `FlushFileBuffers` before replace or rotation | **Flush, as the settings store already does; the cost is measured in P1.7** | P1.7 | |
| D18 | Live-resize repaint | Out of scope; render during the size/move loop | **Out of scope; results apply when the modal loop returns** | none | |

## 12. Risks and mitigations

| Risk | Mitigation |
| --- | --- |
| Hidden races (no TSan on MSVC) | Snapshot-only inputs, the atomic raster pointer, SAL + `/analyze`, ASan stress, an Application Verifier record at each closeout |
| Async work silently drops user input | Stroke chaining, the continuation queue, settle tickets and the end-session guard (§4.4), each with a DocumentTests or self-test case |
| A slice leaves the product half-async | Settle tickets land with the first async commit (P1.1). Each slice keeps the self-test green |
| SIMD or threading changes a byte | The exactness rules (§4.8), a retained scalar reference, the per-tier matrix, exhaustive LUT and blend tests, and pixel-stage reference hashes |
| CRT transcendentals differ across CPUs (the FMA3 switch, ARM64) | Parameter derivation separated from the pixel stage; per-pixel CRT kernels threaded only; hashes cover pixel stages; the FMA3 state recorded |
| AVX code leaks into the SSE2 path | Intrinsics in default-`/arch` TUs, noinline entry points, the prefix-based VEX scan with object attribution in every x64 configuration, an optional SDE run |
| A worker touches freed GPU memory after a device rebuild | Lent slots are retired, never released, until their job completes; self-tests under ASan for recover and adapter rebind |
| Crash dumps lost when several threads fault | The multi-thread crash filter and the worker crash probe |
| GPU work starves frames or triggers TDR | Slices ≤ 8 ms on the slowest target, a timestamp-corrected budget, ≤ 2 batches in flight, no `DISABLE_GPU_TIMEOUT` |
| Fences unsupported on some driver or WARP | The `EnqueueSetEvent` fallback; the probe result asserted in CompositionTests |
| `DriverConcurrentCreates` FALSE: worker creates stall the UI | The probe gates worker creation; the staging ring is the default path |
| Memory spikes at 45–100 MP with concurrent jobs | Live `MemoryBudget` admission, the job-runner OOM boundary, banded intermediates, `Adopt`, scratch and recycle pools, deferred release, pressure events |
| History holds too little for large images | D13; tile-granular history already exists for strokes |
| LibRaw threading and exception hazards | The full callback-parallel patch contract (§6.2); `raw_r` only; concurrent decode and cancel stress under ASan |
| LibRaw security bugs | Track security releases; fuzz RedPrism parsers and the DNG tag reader; limits before allocation; multi-thread crash dumps |
| NEF and the LJ92 formats stay sequential in LibRaw | The P5 decoders; P4 targets CR3 and RAF only |
| iGPU/UMA resource limits | 2048² tiles, the LOD pyramid, the CPU engine on WARP |
| Color differs from Adobe | Stated in reports; correctness proven by DNG-spec goldens; no parity claim |
| An unfair Photoshop comparison | Matched workflow and detail settings, the ACR cache purge, matched cold priming, the warm/cold definitions, 4:4:4 JPEG, the same files (§7) |
| Legal: LGPL, TicoRAW, the DNG patent notice | DLL linkage and notices; HE refused (D15); the DNG attribution line if DNG support is advertised |
| Plan size | Each phase exits with value on its own. P1 alone meets G1's UI-thread and apply rows on R1 and W; the W frame row, the GPU budget and the brush update complete in P3. P2 alone meets most of G2 |

## 13. STOP

- A modal progress dialog, or any nested message loop other than the G1 Windows-owned modal loops run from a UI
  action.
- Running a kernel on the UI thread "just this once": for OK, for a test, or on WARP.
- Rebaselining a legacy golden because SIMD, threads, the GPU or a worker reordered floating-point work.
- Dropping user input: refusing or dropping a stroke, cancelling a stroke the user already sees, or applying a queued
  command against a stale snapshot.
- Showing a modal loop (`MessageBoxW`, TaskDialog) from inside `DrainCompletions`.
- Passing ownership in a window message's `lParam`.
- Any of the following without a spec change: a second D3D11 device, a D3D12, DirectML or Windows ML device,
  `ID3D11Multithread`-protected submission from workers, deferred contexts, or a worker calling any context method.
- Recording anything on a retiring device.
- Any polling: `Map(DO_NOT_WAIT)` or `GetData` loops, spin-waits, periodic or unbounded retry timers, re-armed
  level-triggered waits, OpenMP.
- `std::execution::par`, OpenMP or oneTBB as a second scheduler.
- Blocking the UI thread on a pool, except the two bounded cases: `Shutdown` after the window is hidden, and
  `WM_ENDSESSION(TRUE)` (§4.3, §4.4).
- Compiling shared code with a higher `/arch` than the build floor.
- `catch (...)` in RedPrism code. Catching `std::bad_alloc` anywhere except the job runner, the synchronous `Commit*`
  wrappers, and the existing `RedPrism/` and `WindowCapture` boundaries.
- `DISABLE_GPU_TIMEOUT`.
- Shipping Adobe DCP profiles; claiming ACR color parity; claiming "faster than Photoshop" without a same-machine
  TestRuns record.
- Committing camera RAW files, fuzz inputs or reference renders to the repository.
- Killing `RedPrism.exe`.

## 14. Done

- **G1**:
  - With G1 documents A and B, every §5 command on R1 keeps the UI thread under 4 ms p99 and 16 ms max, frames
    present within 2 refresh intervals while jobs run, and background GPU work stays ≤ 25 % of the refresh interval
    per presented frame.
  - On W, the UI-thread rows hold with A and B, and the frame row holds with a 4K document against the §1.2 W
    targets.
  - A 4K Gaussian blur OK returns to the pump immediately: the window still drags and paints, and Escape leaves the
    layer unchanged. A second run, completed, records one history entry whose pixel hash equals DocumentTests' hash
    for the same input.
- **G2**: the §1.2 commit and preview targets are met on R1 and recorded in `Specs/TestRuns/`. Every exact kernel is
  byte-identical to its scalar reference on every tier and at every thread count.
- **G3**, for the five §1.2 classes on R1, with warm opens:
  - M1 ≤ 100 ms;
  - M2 and M4 ≤ 0.5 × Photoshop's same-machine time;
  - M3 holds, and M5 ≤ Photoshop.

  These are recorded in `RawVsPhotoshop_<date>.md`, with all §6.8 goldens green. The other v1 formats are recorded
  without a ratio claim.
- The owning specs carry every rule in §3–§6, `docs/` describes the new behavior, the closeout matrix is green, and
  this plan is in `Specs/Plans/Done/`.
