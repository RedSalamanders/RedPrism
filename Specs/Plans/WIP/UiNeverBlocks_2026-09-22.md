# P1 — Responsive raster editor delivery milestone

## Delivery checklist

- [x] Split this milestone from the detailed [P2–P6 acceleration and RAW roadmap](ImageProcessingAccelerationAndRaw_2026-10-04.md). The original phase requirements and references are retained there.
- [x] P0 worker, scheduler, admission, cancellation, immutable raster publication and scalar-dispatch foundations qualified; baseline measurement coverage remains open.
- [x] Native deferred adjustment/filter commits, GPU adjustment previews, prepared history, file operations, sizing, selection and lifecycle slices qualified within the limits in the execution ledger.
- [x] Native fixed pointer input stream and dispatch-scoped menu timer repair qualified in all required profiles (`9e466a5`).
- [x] Experimental immutable layer roots, typed opacity, captured brush input and modal lifetime checkpoints preserved and pushed through `b22f974`.
- [x] Additional private lifetime authority, accepted close/end-session, failed-save and shutdown qualification: Debug/Release/ASan pass 8,962 controls/78 stages; all 520 owned inputs match and every archive copy is verified.
- [x] Normal managed Debug build-only: all five images compile/link, matching-PDB ISA controls and actual DxUi provenance pass; 733 archived inputs are independently verified. This does not qualify runtime or native activation.
- [x] Integrate the privately qualified lifetime source and portable project entries into the managed candidate; skills/formatting pass all 620 files.
- [x] Fresh normal managed x64 Debug/Release/ASan suites and ARM64 cross-builds pass for the combined lifetime/build/fixture candidate. All 627 owned inputs match; exact 78-stage / 8,962-control application registry, bounds suite, scanner controls and screenshots pass. Static analysis, diagnosis of the intermittent first-clean-run Wand failure and native activation remain open; [evidence](../../TestRuns/P1NormalIntegration_2026-10-04.md).
- [x] Preserve and push the reviewed complete-color-display host and stack/scratch candidate as WIP checkpoint `92bd2ae`; it is not a qualified phase exit or native activation.
- [x] Managed default-desktop Debug analysis qualifies the repaired candidate: all five images, unchanged stack thresholds, concurrency SAL controls, ISA/PDB/scanner gates and provenance pass; r31's 754 inputs / 1,038 archive copies are verified. The full current runtime matrix and native activation remain open.
- [x] First fresh current x64 Debug full suite and all three ARM64 cross-builds pass on identical 754 inputs; 6,491 document / 8,980 application controls, exact 78 stages and 12 new scratch controls are verified. Release/x64 ASan runtime and native activation remain open.
- [ ] P1.3: finish real sampler, wand and clone all-layers readback callers and device-loss/restart/fallback coverage.
- [ ] P1.4–P1.6: finish remaining stroke/tool producer ordering, shape redraw, geometry/coverage/display ownership and lifetime routes.
- [ ] P1.7–P1.8: close clipboard prefill/retry/publication/end-session residuals and measured file/size apply overruns.
- [ ] P1.9 H2: activate immutable record roots in the native application after complete pointer, adjustment and display writer/retirement qualification.
- [x] P1.9 H2.2 prerequisite: [cached layer-record ownership bounds](LayerRecordOwnershipBounds_2026-10-04.md) pass the full private Debug/Release/ASan document suites and latest normal Debug integration. Complete normal/native integration remains open.
- [ ] P1.9 H2.2: finish [deferred color display preparation, publication and retirement](DeferredColorDisplay_2026-10-04.md), including complete admission and actual host qualification.
- [ ] P1.9 H3: incremental retained-storage accounting, selections and late sparse materialization; maintain the live reservation and history budgets.
- [ ] P1.9 H4: bounded first uploads, layer/chrome synchronization and large-owner retirement.
- [ ] Measure every P1-due G1 row on the specified largest supported fixtures; fix and rerun actual overruns. Capture Release/ETW evidence in `Specs/TestRuns/NeverBlocks_<date>.md`.
- [ ] Finish the user workflow acceptance run, lasting domain specs and guide, required validation, and commit/push the qualified milestone.
- [ ] Move this P1 plan to Done and update its index and dependent links only after every P1 exit requirement is met.

**Status: ACTIVE / P1.** Split on 2026-10-04 at the user's request. P1 is not ready for Done yet. Checked
items identify qualified slices, not compliance with the overall G1 latency contract. Native ARM64 execution
is unavailable on this host; cross-build evidence does not substitute for it.

Current integration: the repaired complete-color-display route passes managed default-desktop Debug analysis
and the full Debug runtime, including all 18 new actual-host cases. All ARM64 configurations cross-build.
The exact 754 owned inputs match analysis and every current qualified attempt; every archive copy is verified.
Fresh Release and x64 ASan full runtime, native activation, the remaining P1 code routes and original G1
measurements still precede P1 Done. Earlier interrupted/failed attempts are retained in
[DeferredColorDisplay](../../TestRuns/DeferredColorDisplay_2026-10-04.md). Every P1 exit row remains unchanged.

## What this milestone delivers

A usable Windows raster editor whose supported editing, selection, merge, file and size workflows keep the
view and chrome responsive while work runs. Users can pan/zoom, observe progress, cancel, save and close with
ordered input, exact existing pixels, one correct undo entry per completed edit, and preserved data on failure.
P1 changes where work runs and how its ownership is published; existing legacy pixel goldens stay unchanged.

The next roadmap owns the original G2/G3 speed targets, SIMD tiers, sliced GPU kernels/tiled residency,
camera RAW decode/develop, paired Photoshop measurements and nondestructive high-bit-depth documents.
P1 does not claim those results. The original brush-update/distort-preview, background GPU budget and WARP
frame targets explicitly due at P3 remain P3 requirements. P1's owner-thread/apply/frame requirements below
are unchanged.

## Completed work and remaining work by slice

| Slice | Qualified work | Required before P1 Done |
| --- | --- | --- |
| P0 foundation | Thread-safe raster publication; buffer adoption; generations; worker/wait pools; scheduler, queues, admission, retirement and ISA floor | Remaining baseline/ETW coverage and phase-exit measurements |
| P1.1 panels | Deferred preview/commit/histogram; staleness checks; busy state; typed command continuations and prepared history | Remaining live-context integration, ownership and measured due G1 rows |
| P1.2 GPU adjustment preview | Completed per-layer preview, selection/grain parity and measured tradeoff | Preserve its goldens during readback/record-root integration |
| P1.3 GPU readback | Event-driven completion, lent slots, utility compose, merge/adjustment opening and rebind restart | Sampler, wand/clone all-layers sources, remaining callers and timing |
| P1.4 strokes | Snapshot/core and shutdown foundations; private ordered Brush/Eraser lifetime route | Complete tool/chaining/lazy source/mouse-up callers, native integration and timing |
| P1.5 raster/transforms/shapes | Basic edits, gradients, prepared Copy/Cut, mask/layer copy, deferred transforms and reviewed callers | Shape redraw, remaining live producer contexts, record/display ownership and timing |
| P1.6 selection | Worker geometry/history; cancellable wand/trace; prepared select/deselect/nudge and eligible outline callers | Remaining coexisting gestures/booleans, bounded geometry/coverage/display and timing |
| P1.7 files | Worker classification/codecs/project/save/export/import; prepared clipboard and ordered save/lifecycle foundations | Remaining retry/prefill/fault/durability/cancellation/end-session cases and measured overruns |
| P1.8 sizing | Functional async Image Size/Canvas Size matrix and recorded large-fixture runs | Finished-result apply within 2 ms and associated ownership/upload costs |
| P1.9 resources | Prepared history; shared history roots; selection budget; optional image thumbnails; settled metadata | Full native immutable layer-root activation, H3 incremental accounting, H4 upload/chrome bounds and measured G1 |

The [execution ledger](#appendix--p0p1-execution-ledger) retains completed slices, exact evidence, original failures
and their remaining limits. Child plans are indexed in [WIP/README.md](README.md); their unchecked P1 rows remain
requirements of this milestone. An obsolete intermediate paragraph in the historical ledger does not supersede
a later qualification record or this current checklist.

## P1 acceptance and closeout sequence

1. Finish the current immutable-root/lifetime integration, then the remaining P1.3–P1.9 child rows. Validate
   each behavior against independent state expectations and unchanged legacy numeric goldens.
2. Exercise actual host commands: adjustment/blur OK and Cancel; brush and retouch; selection and transform;
   copy/paste/merge; resize; project/image open; save/export; Save/Discard/Cancel before replacement/close;
   failed save; capture loss; device loss; cancellation and shutdown with work outstanding. Window dragging,
   pan/zoom and splitter/chrome input must continue while an admitted job is gated.
3. Run Release timing for every due operation on G1 A and B, hardware R1 and WARP as §1.2 specifies. Fix the
   measured owner/apply/frame overruns. Record unsupported/unavailable machine qualification explicitly under
   D9; never infer native ARM64 timings from a cross-build.
4. Run `validate-skills.ps1`, `format.ps1 -Check`, full x64 Debug/Release/ASan `test.ps1`, all ARM64 cross-builds,
   native analysis and the owning resource/lifetime validation. The latest integration must be the tested source.
5. Update owning specs and the guide, reconcile child status, capture any UX evidence with the application
   screenshot harness, and record the exact qualified source/results. Commit and push the completed step.
6. Only then move this plan to `Specs/Plans/Done/`, remove its WIP index row and update the P2 entry link.

Execution authorization through P6, cheaper-agent delegation, checklist updates and commit/push after qualified
steps remain in force. The user's current priority is to finish P1 as a deliverable before beginning P2.
Open decisions retain their recorded gates; a proposed recommendation alone does not qualify a result.

## 1. Goals and how they are measured

**G1 — The UI thread never waits.** It pumps messages, applies results that are already finished, records GPU work
in bounded slices, and presents. Nothing RedPrism computes, reads, writes, encodes or reads back runs to
completion on it, unless §5 classifies it S (measured ≤ 4 ms p99 on the largest supported input). The only other
exceptions:

- Windows-owned modal loops (`IFileDialog`, `TaskDialogIndirect`, `MessageBoxW`, native menus) run from a UI
  action.
- The bounded `WM_ENDSESSION` wait for a running save (§4.4).
- `Shutdown` after the window is hidden (§4.3).

G2/G3 acceleration and RAW goals are defined in the [P2–P6 roadmap](ImageProcessingAccelerationAndRaw_2026-10-04.md#1-goals-and-how-they-are-measured).

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

The exact CPU speed, RAW open/develop and Photoshop ratio tables are retained in the future roadmap §1.2.
They are separate phase exits. This milestone must satisfy every reliability row above whose exit is P1.
## 2. Starting inventory and implementation deltas

The original 2026-09-22 survey is retained in [the future roadmap §2](ImageProcessingAccelerationAndRaw_2026-10-04.md#2-starting-inventory-and-implementation-deltas). It is historical: current code already includes P0/P1 migrations. Read the execution ledger and current owning contracts before using a survey statement as today's implementation. The opening slice table identifies native, experimental and unqualified work separately.

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

Ownership and ordering contracts shared with later phases are retained here. Paragraphs explicitly marked P2/P3/P4 describe future dependencies; P1 closeout is governed by the phase column in §1.2 and §10's P1 exit. In particular, tiled GPU residency and GPU compute slicing do not become P1 requirements through this split.

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
  the function/context pair on its UI thread after the lanes start. Shutdown retains it through producer join and
  owner wrapper/resource detachment, then clears it before the final worker callback and pool teardown.
  Other threads have no sink and free inline. This implementation decision (2026-09-27) removes the race
  between loading a process-global function and its separately retired context, with no per-release lock or
  reference count.
- `Storage`'s deleter and a directly owned `PixelBuffer` transfer allocations of 16 MiB or more to that sink.
  The buffer's allocation carries its own aligned intrusive header, so cancelled/unadopted results also avoid a
  large UI-thread free. Normal worker destruction and the final MTA shutdown callback free inline on their worker.
- The node is intrusive inside `Storage`, so queuing never allocates.
- A NORMAL-priority Background callback drains the list. Frees are never dropped or cancelled.
- With no sink registered, the deleter frees inline on its calling thread. Standalone ownership tests install
  their own sink; shutdown clears the owner's sink only after joined owner cleanup.
- Shutdown drains the list through a preallocated, non-cancellable final MTA callback after producer join and
  before reservation/event/pool/apartment teardown. Admission counts queued-free bytes as releasing.

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
  A driver-busy Map permits one additional `EnqueueSetEvent` readiness barrier retaining the original copy,
  slot and in-flight charge. Its event permits one further nonblocking Map; persistent busy or barrier failure
  fails locally. There is no recopy, timer, polling loop or blocking Map. This bounds attempts, not driver latency.
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

### 4.8 Scalar dispatch and future SIMD work

P0.8's scalar dispatch and ISA floor remain P1 prerequisites. Detailed tier detection, noinline entry points, exactness proofs, matrix and SIMD kernels are retained in [the future roadmap §4.8](ImageProcessingAccelerationAndRaw_2026-10-04.md#48-simd-kernel-layer) for P2. P1 keeps committed kernel arithmetic unchanged.

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
| Gradient drag (`DragGradient` → `RefreshGradient`) | Whole canvas per move | J [Interactive] (P1); G (P3) | GPU shade into the stroke working texture (P3) | J [Commit], exact at Apply/Enter or ordered settlement; mouse-up keeps the pending line, as `Document_Tools` requires |
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
- `RunColorEditJob(input, executor, token) → ColorEditJobResult`, pure work in `Common/ColorEditWork.cpp`;
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

## 6. RAW work follows P1

The complete decoder/develop math, formats, statuses, precision, budgets, patch/license contracts, file-backed corpus and ViewerImgRaw references are in [the future roadmap §6](ImageProcessingAccelerationAndRaw_2026-10-04.md#6-raw-pipeline). The [RAW acquisition record](../../TestRuns/RawCorpus_2026-09-30.md) and checked-in downloader remain available. RAW correctness/develop/round-trip and performance qualification are P4/P5 work, not P1 Done conditions.

## 7. Photoshop comparison follows RAW qualification

The matched workflow, cold/warm definitions, cache purging, stage timing, output settings, statistics and claim restrictions remain in [the future roadmap §7](ImageProcessingAccelerationAndRaw_2026-10-04.md#7-faster-than-photoshop-protocol). P1 makes no Photoshop ratio claim.

## 8. Testing and validation

This shared test map retains later phase additions for continuity. Only P0/P1 additions and the current owning contract gates are due for this milestone; exhaustive new SIMD tiers, RAW/DNG goldens and fuzz targets enter with their future phases. P1 tests must preserve all existing legacy expectations and cover every actually moved command. State/assertion counts do not substitute for §1.2 Release measurements.

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

Apply the P0/P1 portions of each row before P1 closeout. Future phase changes are retained as a dependency map and are owned by the [P2–P6 roadmap](ImageProcessingAccelerationAndRaw_2026-10-04.md); they are not P1 implementation claims.

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

**P0.8 Kernel dispatch skeleton** (COMPLETE on x64, 2026-09-27). `KernelTable` with the scalar tier, `--isa`, and the VEX scan in
`build.ps1` with its allowlist. **Exit**: the current binary passes the scan with only the STL/CRT allowlist in every
x64 configuration; goldens unchanged. Exit met; all ARM64 cross-builds also passed. See the dispatch record above.

### P1 — Nothing blocks (placement changes; kernel math unchanged)

- **P1.1 Adjust and filter panels** (§5.1):
  - the split API, the preview and commit lanes, and preview reuse;
  - the host-path histogram as a job, with the direct API kept inline;
  - transform and mask checks in every `Apply*`;
  - the panel busy UI;
  - **settle tickets, and `ConfirmDiscardChanges` as a UI action for every caller (§4.4)**.

  P1.1c implementation ordering, refined against the host on 2026-09-28:
  1. The host explicitly selects deferred color computation; direct `DocumentSession` callers retain their inline
     behavior through the same graph. Begin/update only capture immutable handles, normalize bounded settings and
     compute grid extents. They must not generate histograms, downscale, walk selection points, flatten sparse
     tiles or render pixels. Cache selection bounds with their immutable geometry, as point counts already are.
  2. Worker preparation owns grid padding and preview sources, and reuses a prepared source only for the same
     original/grid/placement. Extend reservations to include padding and downscale temporaries; P1.1b's commit
     estimate alone does not cover this newly moved preparation. Return prepared handles atomically. A failed
     growth retains the panel and cannot authorize a clipped full commit.
  3. A panel-lifetime stamp differs from its settings sequence. Histograms remain valid across settings changes
     within that opening. Intermediate previews may publish while the latest request waits, without overwriting
     the latest settings. A full preview can be committed only when its rendered settings/seed/grid stamp matches
     the requested commit. Cancel/reopen, preview off, identity, replacement and shutdown invalidate the appropriate
     mailbox/job tickets. All result paths also use the existing owning document/target checks.
  4. OK freezes only the pending panel's mutation controls and starts ordered history work; pan, zoom, chrome and
     cancellation keep pumping. Commit completion applies or drops before releasing the history barrier. Errors
     and prompts are posted as owned UI actions, never shown inside completion drain. No UI continuation calls
     `SettleJobs`; harness calls alone may wait while pumping.
  5. Retain command parameters before waiting: project paths, ordered image batches and their drop point, Save As
     path, export format, new/size draft and close/session-end intent. Capture the document only after preceding
     history work applies. Save-before-proceed resumes its specific action only on successful commit and write;
     Cancel preserves all drafts, and explicit Don't Save may discard. Revalidate document lifetime before every
     resumed action. A batch is one queue item, and refusal of the ninth item is visible.
  6. The inspected Add-as-Layer path currently falls through after `ToolSwitch` cancels a failed panel apply;
     remove that failure fall-through. Canvas/Image Size currently use `DocumentGoes` to drop a panel; make their
     pending-panel decision explicit and covered, with no resize after a failed required commit. Cover New,
     New Canvas, project open, import-as-new, Close Project, WM_CLOSE and Windows session-end individually. There
     is no Recent/Revert command or positional file-open command line to invent in this slice.
  7. Exercise both direct numeric calls and deferred production calls. Use event gates for old preview/new settings,
     histogram arrival, OK/Cancel races, failed commit before every dependent action, document replacement, queue
     overflow and shutdown. Ordinary DPI/window resize does not settle an edit. Opening an adjustment-layer panel
     still uses the composition readback until P1.3 replaces that GPU wait; overall G1 remains open until every
     owning migration and measured exit criterion is complete.
- **P1.2 Per-layer adjust draw**: the new pass mode, a DYNAMIC LUT and a DEFAULT cube updated in place per tick, and
  the coverage input. This also ends the per-tick IMMUTABLE texture creation for live adjustment layers.
  Completed in [GpuAdjustmentPreviews_2026-09-30.md](../Done/GpuAdjustmentPreviews_2026-09-30.md), with
  [qualification and measured tradeoff](../../TestRuns/GpuAdjustmentPreviews_2026-09-30.md). GPU previews do not
  provide a CPU commit raster; CPU commits remain worker computations until their later optimization. P1.3 follows
  [GpuCompletionReadback_2026-09-30.md](GpuCompletionReadback_2026-09-30.md).
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

### Next milestone

After this P1 exit qualifies, begin [P2–P6](ImageProcessingAccelerationAndRaw_2026-10-04.md#10-phases-and-slices). Each phase delivers its own independently tested value and measured exit.

## 11. Decisions

The shared decision history is retained unchanged. Only decisions that gate P1 work must resolve before that slice. D9 machine availability and any missing native runtime evidence must be stated in the P1 exit record. Future phase decisions are also carried into the new roadmap and must be reconciled there as they are made.

The recommended option is in bold. An empty "Decided" cell means the decision is open, and it blocks the slices in
its "Gates" column.

| Id | Question | Options | Recommendation | Gates | Decided |
| --- | --- | --- | --- | --- | --- |
| D1 | Engine for committed bytes | Exact CPU for everything; GPU with tolerances; mixed | **Exact CPU (SIMD + threads) for every legacy-golden kernel; GPU for previews within named tolerances; RAW develop commits from the GPU within `Rendering_RawDevelop.md` tolerances** | P1–P4 | 2026-09-27: adopt the recommended mixed policy under the user's authorization through P6. Existing committed pixels remain the exact CPU reference; RAW tolerances must be specified and tested before that path ships. |
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
| D12 | Preview and histogram policy | Keep 4096/2048/full; view-resolution GPU previews; the legacy 8000 px histogram source; an integer histogram | **View-resolution GPU previews for filters; the per-layer adjust draw for table kinds; the histogram redefined as exact integer numerators N = Σ a·m per bin in `uint64`, with bins[c] = N_c / 65025.0 and bins[0] = (N_r + N_g + N_b) / (3 × 65025.0), each rounded once, from an 8000 px source (legacy)** | P1–P3 | 2026-09-27: adopt the recommended policy in stages. P1.1a retains existing preview grids and histogram arithmetic; P2.2 changes the histogram only with the named goldens and AutoLevels parity. P1.2/P3 add GPU previews with their parity gates. |
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

## 14. P1 Done

- Every remaining P1 child row in the opening checklist has qualified implementation or an explicit original
  phase assignment; later-phase assignment cannot be invented to avoid an overrun.
- On R1 with G1 documents A and B, every §5 operation due at P1 meets §1.2: owner dispatch/drain/action/frame
  CPU p99 ≤ 4 ms and max ≤ 16 ms; finished apply ≤ 2 ms with no pixel work; invalidated frames present within
  two refresh intervals while jobs run. Escape restores the document in the next frame and the stated
  cancellation interval holds. Brush update and distort preview remain the original P3 exit exceptions.
- On WARP, the UI-thread and apply rows meet the original P1 requirements with A and B, unless the recorded
  P0.1 B composition deferral applies. WARP's 4K frame gate and the background GPU budget remain P3.
- R2/R3 runs or their unavailability and the D9 decision are explicitly recorded; ARM64 cross-builds alone
  cannot qualify ARM64 runtime. Any unavailable required measurement remains an unchecked qualification gate.
- Actual host acceptance covers all moved commands, ordered input, save-before-proceed, cancellation/failure,
  device recovery and shutdown. A gated 4K Gaussian OK leaves the pump live; Cancel preserves the original;
  a completed run records one entry with the unchanged DocumentTests reference hash.
- The memory/admission/retirement and history accounting contracts hold on large fixtures, including late
  sparse materialization; no CPU pixel pass or large final free is hidden in an owner publication.
- The latest integrated source passes skills, formatting, full x64 Debug/Release/real ASan tests, ARM64
  cross-builds, ISA/provenance and owning analysis/resource gates. Tests retain their independent goldens.
- `Specs/TestRuns/NeverBlocks_<date>.md` records the source, fixtures, machine/settings, actual results,
  timing/ETW evidence, original failures and qualification limits. Owning domain specs and the user guide
  describe the delivered behavior. Completed code/spec steps are committed and pushed.
- Move this plan to Done, remove its WIP row, reconcile child plans and update future milestone links.

P1 Done is the next implementation entry for P2. G2/G3, RAW and full-program closeout remain in the future roadmap.

## Appendix — P0/P1 execution ledger

This chronological evidence ledger records intermediate states; use the opening checklist and later TestRuns
records for current status. Earlier failures and narrower qualifications are retained rather than rewritten.
## Implementation checklist

- [x] Current fixture qualification — Fourteen supplied-trace controls and all original 125 transfer assertions
  pass in x64 Debug/Release/ASan. Fixture helpers follow the existing 30-attempt/100 ms event-wait policy;
  production retry limits, sealed authority and literal assertions remain unchanged.
- [x] Current P1 functional integration, 2026-10-02 — Ordinary deferred transforms, admitted worker
  New/New Canvas/Close, acknowledged session CPU retirement, suspended/prequeued shutdown and bounded folder
  staging publication pass the clean x64 matrix and all ARM64 cross-builds. Strict CL/R8, compact RCX and R10/AL
  ISA proofs preserve the runtime allowlist. Exact 582 inputs and 327 evidence files are byte/SHA verified
  ([qualification](../../TestRuns/P1LifecycleShutdown_2026-10-02.md)); original failures remain preserved.
- [x] P1 shutdown CPU roots — Sampling/overlay/benchmark CPU roots: eight original failures among 52 controls
  are preserved before repair; the complete private WARP candidate passes all 52 and existing application suites.
  The fresh native matrix passes all six profiles against 582 unchanged inputs: 52 shutdown controls and full
  suites in each x64 configuration, plus all ARM64 cross-builds. The benchmark state/timing/metadata remain valid
  ([qualification](../../TestRuns/P1ShutdownCpuRoots_2026-10-02.md)).
  Broader shutdown, selection/stroke/readback/history paths and measured G1 remain open.
- [x] P1 shutdown acquired-prefix initialization: primary-owned controls cover eight
  successful-stage checkpoints, sixteen retry event/registration seams, successful and never-started processing.
  Its complete private Debug WARP application passes all 390 controls and unchanged suites; exact private proof
  is archived before native activation. Review confirms the 26-case prefix/count/lifetime expectations.
  The native candidate passes all six forced-rebuild profiles against 584 unchanged inputs, with 390 processing
  and unchanged 52 shutdown controls in each full x64 suite. Exact sources/products/receipts are archived
  ([qualification](../../TestRuns/P1ProcessingInitialization_2026-10-02.md)). These controls
  do not qualify native allocation/pool/GPU API failures or broader A3.
  Native r1 passes all ARM64 cross-builds and full x64 ASan, then Release's actual self-test crashes before
  buffered output reaches its log; x64 Debug does not run. Exact 584 inputs, 230 evidence files and PID 108148's
  dump are archived before repair. Symbol/disassembly and retained build timestamps show Release reused the
  pre-probe `Application.obj` with the older member layout: private file copying preserved source timestamps
  older than that object. A supplementary 162-entry tracking archive is retained. Move stdout configuration
  before all standalone tests and force a six-profile rebuild; the buffering issue is not the crash cause.
- [x] P1 live shutdown slice — Live Paint/Erase/Heal/Clone/Blur/Liquify/Smudge owners, including masks:
  the reviewed isolated Debug WARP candidate passes all 432 controls, unchanged 390 processing/52 shutdown checks
  and the full existing application suite at `2026-10-02T14:26:05.1417394Z`. Exact 562 private inputs and 25 evidence
  entries are archived before native activation. Eight reviewed files are activated with current write timestamps;
  six forced-rebuild profiles are required before completion/commit. GPU/event/clipboard leases, API faults and
  timing remain open.
  The fresh six-profile forced rebuild passes at `2026-10-02T15:06:20.9478751Z`; each full x64 suite passes
  all 432 live-owner controls and unchanged startup/shutdown checks. Exact 585 inputs and 327 evidence files
  are byte/SHA verified before the next private build
  ([qualification](../../TestRuns/P1LiveShutdown_2026-10-02.md)). This closes A3c only.
- [x] P1 shutdown GPU/retry lease slice — A3d actual mapped GPU rows and retry wait producers/receivers: prepare four
  standalone Application cases, with 56 event-gated assertions, in a private tree. Observe mapped-row
  acknowledgement before queue fallback cleanup, retain literal bytes through cancellation and device
  retirement, and join held/ready-undrained retry producers before receiver destruction. Preserve the first
  private link failure before serializing shared DxUi builds. Clipboard leases, actual API faults and timings
  remain separate; private runtime and native qualification are pending.
  The complete private Debug WARP application now passes all 56 actual map/retry controls and unchanged suites;
  exact 563 inputs and 40 evidence entries are archived before activating seven reviewed native files.
  Native r1 passes all ARM64 cross-builds, then stops at x64 ASan's linked ISA check before runtime;
  x64 Release/Debug do not build or run. Exact 586 inputs and 243 evidence files are preserved before repair.
  The reported instruction bytes belong to an eleven-entry terminal DWORD switch table; the observed matching
  RSP+disp32 guard needs a narrow data proof. Private controls pass without changing the runtime allowlist.
  Original-image recheck and fresh six-profile qualification remain required; A3d/P1/G1 stay open.
  The private proof now passes the exact original image and all parser controls: only three rows inside two
  bounded terminal tables change classification; runtime/tier/EVEX policy is unchanged. Exact proof sources and
  evidence are archived before activation. Fresh native r2 is running all six forced-rebuild profiles.
  Native r2 now passes all six profiles against 587 unchanged inputs; each full x64 suite passes all 56 lease
  and 54 new strict ISA controls, with existing goldens unchanged. Independent source/evidence verification
  retains 587 inputs and 325 evidence files
  ([qualification](../../TestRuns/P1LeaseShutdown_2026-10-02.md)). Broader A3/P1/G1 remain open.
- [x] P1 wand/trace cancellation foundation — S4a cancellable wand/flood/mask tracing and layer sampling: a private candidate
  prepares sparse reads without source-cache publication and bounded explicit-loop cancellation checkpoints.
  Primary reviews the cheaper agent's literal/checkpoint tests, fixes the actual corner-touch case and adds
  owner-driven local/shutdown cancellation. Private full DocumentTests and paired Release kernel measurements
  precede activation and a fresh native matrix. Ordered host/GPU caller migration and G1 remain separate.
  Private Release r1 passes all 54 cancellation checks and existing document goldens, but paired 45 MP medians
  regress by 34% matching, 39% flood and 19% trace. Exact inputs, image/PDB, compiler tracking, logs and machine
  metadata are preserved before tuning. Chunked r2 passes again; matching/trace regress by about 3%, flood by
  18%, so it also stays private and is archived. Further tuning and reviewed odd-width/tail checkpoint tests
  precede qualification; no production selection route has changed.
  Private r6 now passes all 54 cancellation and 17 boundary checks, unchanged document goldens and two paired
  measurements. Final matching medians add 2.9–4.2%; flood varies from 5.4% faster to 4.7% slower, solid trace
  adds 0.6–1.6%, and fragmented stripe trace varies from 3.1% faster to 1.3% slower. Retain shared-desktop outliers;
  these are kernel costs, not G1 qualification. Exact 289 inputs and 199 evidence entries are archived before
  activating nine reviewed native files with current write timestamps. The fresh six-profile forced rebuild is
  running; S4a remains unchecked until its native matrix and archive pass.
  The native matrix now passes all six profiles at `2026-10-02T17:49:56.7560093Z`: each full x64 suite passes
  all 71 named controls and unchanged goldens; ARM64 is build-only. Exact 591 frozen inputs and 328 evidence
  files are independently SHA-verified before another build
  ([qualification](../../TestRuns/WandCancellation_2026-10-02.md)). This completes S4a only.
- [x] Active-layer New publication core — S4b prepares settled active-layer New sampling/trace and
  complete history publication with captured point/settings/AA. A transparent-source rectangle shortcut and
  cheaper-agent literal/authority/history tests were reviewed and corrected. The final private Debug suite passes
  117 unique controls plus existing goldens; exact 296 inputs and 136 evidence files are archived before native
  activation. Capacity planning carries sparse-copy recycling through later phases; actual worker retirement
  releases the test's observation owners first. Native r1 passes ARM64 and full x64 ASan, then Release stops
  before runtime at an unrecognized RDI-indexed adjustment switch table in the linked ISA scan. Exact 595 inputs
  and 297 evidence files retain the failure before repair. The unchanged-image recheck and strict parser controls
  precede a fresh native matrix. Combined r2 now passes all six profiles with 117 exact wand checks in every
  full x64 suite. Independent source/control verification and archival retain 598 inputs and 7,223 evidence files
  ([record](../../TestRuns/WandLayerSelection_2026-10-02.md)). Host ordering, live gestures, All Layers, booleans and G1 stay open.
- [x] Preserving selection authority — S2a adds separate weak opening identity for live brush/warp/shape without
  weakening settled history restore. A primary-reviewed private Debug suite passes 136 unique controls, including
  literal committed pixels and separate Undo steps proving the original stroke clip survives Select All/Deselect.
  Exact 299 inputs and 137 evidence files are archived; original fixture/compile failures remain retained. S4b and
  this core share the fresh native qualification matrix. Production pointer buffering and writer barriers stay open.
  Combined native r2 now qualifies all 136 named controls in each full x64 suite and all ARM64 cross-builds;
  exact source parity and independent evidence hashes pass ([record](../../TestRuns/SelectionPreservation_2026-10-02.md)).
- [x] Settled selection host route — S2b is qualified: admit before owner capture,
  prepare history on the worker, retain the selected ordinal/input until apply/drop, and retire the aggregate
  off owner. Actual Application ordering/fault/pump/authority tests precede native activation. Header inputs,
  live brush/warp/shape, lasso/move closure, broader writer barriers and G1 remain separate requirements.
  Private r1 compiler failure, r2's unreached mask-target fixture and r3's clipboard setup ordering failure are
  archived before each correction. The r3 selection stage passes 162 exact named checks, but the full application
  suite fails: its pending-gradient fixture began pointer input before queued Deselect acknowledged. The fixture
  now waits through the existing asynchronous command helper; production clipboard/gradient logic is unchanged.
  The revised full private Debug suite must pass and be archived before native activation.
  Private r4 now passes the full WARP application suite and all 162 named selection checks across 28 modes.
  Exact 471 inputs and 188 evidence files are independently SHA-verified and archived before eleven native
  activations. A fresh serialized six-profile forced rebuild is running; S2b remains unchecked until exact native
  controls, source parity and archival pass ([record](../../TestRuns/SelectionHost_2026-10-02.md)).
  Native r1 stops in ARM64 ASan HostTests: its string catalogue's declared size omits the newly added ID. No
  complete profile qualifies. Exact 601 inputs and 3,850 evidence files are retained before a single-byte test
  count correction; private application inputs/qualification are unchanged. Native r2 runs the full fresh matrix.
  All ARM64 cross-builds and the complete x64 ASan suite now pass, including actual sanitizer detection,
  full WARP application controls and corpus/screenshot gates. x64 Release/Debug, independent verification and
  archival remain required before checking S2b.
  Native r2 now passes all six profiles at `2026-10-02T21:48:10.2541344Z`. Each full x64 suite passes the exact
  162 unique actor checks; ARM64 is cross-build only. Independent source/control verification and immutable
  archival retain 601 inputs and 2,858 evidence files, including a separately verified 45-file private runtime
  supplement for omitted archive log entries. Only the settled Select All/Deselect host boundary is complete;
  S2/S3b, broader P1 and measured G1 remain open.
- [x] Active-layer New wand host route — Prepare the next isolated source candidate while S2b native inputs
  are frozen. Own point/settings/AA at event time; use the qualified capacity planner before capture, worker
  sampling/flood/trace/history, current-only apply and aggregate retirement. Preserve queue coalescing/receipts,
  literal output, no-op/error/cancel semantics and the actual application caller. All Layers, Add/Subtract,
  outline/lasso closure, broader pointer acceptance/writer barriers and G1 remain separate migrations. Private
  code review, full application proof, archival and native qualification precede activation.
  Primary review strengthens the cheaper-agent test draft with distinct latest-click literals, a real shared-
  ordinal gap, a window message observed before worker release, event-time settings changed before capture,
  queued outside/nonfinite/empty acknowledgements and actual record-table refusal/recovery. The isolated source
  has sixteen test modes, independently qualified below.
  The complete private Debug/WARP application now passes all 86 new controls, unchanged selection/compatibility
  suites and all 49 registered stages. Exact 475 inputs and 197 evidence files are archived before nine reviewed
  native activations. All six forced native rebuild profiles, full x64 suites and ARM64 cross-builds pass. Exact
  control/source verification and independent archival retain 604 inputs and 877 evidence files; each x64 run
  passes 86/16 wand-host controls and 162 retained selection-host controls. Only this settled host boundary is
  complete ([record](../../TestRuns/WandLayerHost_2026-10-03.md)); all broader P1/G1 requirements stay open.
- [x] Outline-move opening core authority — S3b0 qualifies the private core prerequisite: a bounded scalar opening
  identifies the exact gesture before capture and survives equal-geometry reopen/refusal cases. Preserve all
  prepared End/Cancel generation/history semantics and account complete original-outline retirement. Private
  source, reviewed literal/state-machine controls and full core/native qualification precede host closure;
  S4c native inputs stayed frozen throughout preparation. The primary-reviewed private core passes 66 unique
  new checks and the complete DocumentTests suite, retaining all 185 closure controls and original goldens.
  Failed attempts are archived before corrections; final proof retains 554 inputs and 145 evidence files before
  eight reviewed native activations. The fresh forced six-profile matrix passes at `2026-10-02T23:17:48.9451202Z`;
  exact native controls/source verification and archival retain 606 inputs and 913 evidence files
  ([record](../../TestRuns/SelectionMoveOpening_2026-10-03.md)). Production outline closure remains open.
- [x] Outline-move host closure — S3b1a owns moved-pointer release and direct Escape/first Undo cancellation
  under the shared ordinal, stops later pointer samples and retains draft/history until acknowledgement.
  The primary-reviewed private full WARP Application proof passes 242 unique controls across 31 modes and all
  50 stages; its 618 exact inputs/199 evidence files are archived before nine host activations. The native
  r1 matrix freezes 609 inputs; ARM64 cross-builds and x64 ASan pass, then Release's ISA gate rejects a
  vector-looking OnCommand table row. Exact failed inputs/products and 4,286 evidence files are archived before
  scanner diagnosis. The unchanged failed image and all nine parser suites now pass the narrow RDI-base
  proof, including 56 new controls without changing the allowlist. Its immutable archive retains 13 inputs
  and 238 evidence files. Native r2 is running against 610 frozen inputs; verification/archive remain required
  Native r2 now passes all six profiles at `2026-10-03T01:07:26.4741875Z`, with 242 exact host controls, 56
  ISA controls and all 50 Application stages in each full x64 suite. Independent source/control verification
  and immutable archival retain 610 inputs and 1,298 evidence files
  ([record](../../TestRuns/SelectionMoveHost_2026-10-03.md)). S3b1b still requires no-move follow-ups and the
  remaining concrete End/Cancel callers while preserving Save/Enter and nested history. Broader P1 and measured
  G1 remain open; the private structural proof does not close them.
- [x] Ordered outline callers — S3b1b0 binds known End-before-command/tool events to their exact outline
  opening and preserves selected first Undo/Redo cancellation, same-tool behavior and capture loss.
  - [x] Full private Debug/WARP proof: 276 unique checks across 49 modes, all 51 Application stages and retained
    host checks. Exact 621 inputs/435 evidence files are archived before seven byte-identical activations;
    the two new Select All fixture failures are preserved before correcting their AA expectation.
  - [x] Full native matrix: all six forced-rebuild profiles pass with 276 unique caller checks/49 modes and
    retained suites in every x64 gate. ARM64 is cross-build only. Exact verification and immutable archival
    retain 611 inputs/1,546 evidence files; 57 refined ISA controls keep the scanner/allowlist unchanged
    ([record](../../TestRuns/SelectionMoveCallers_2026-10-03.md)).
- [x] Outline click follow-ups — S3b1b1a owns no-move End and its Deselect/active-layer New wand action in
  one selected request, retaining event-time point/settings/AA and the ordinal through both worker phases.
  Primary-owned isolated code and 16 reviewed modes are prepared. Private r1's missing-enum-header compiler
  failure retains 623 exact inputs/413 evidence files before correction. Private r2's full WARP suite passes
  84 unique checks/16 modes; 623 inputs/436 evidence files are archived before seven exact activations.
  Native r1 passes all ARM64 builds, then fails x64 ASan's linked ISA scan; 612 inputs/5,850 evidence files
  and the failed EXE/PDB are archived before diagnosis ([record](../../TestRuns/SelectionMoveFollowups_2026-10-03.md)).
  The narrow QWORD-stack/RAX table proof now passes 73 new controls and both unchanged failed images;
  its immutable archive and all 57 retained RDI controls verify before activation. Native r2 passes all six
  profiles at `2026-10-03T03:46:41.3855104+00:00`, with exact 84/16 follow-up and retained controls in every
  full x64 suite. Independent verification and archival retain 613 inputs/21,605 evidence files at
  `2026-10-03T03:51:14.2165680+00:00`. All Layers, remaining header/close callers and measured G1 remain open.
- [x] Idle outline header follow-ups — S3b1b1b0 owns kind/Deselect event-time values through acknowledged End
  at one ordinal. Primary-reviewed isolated code passes the full private Debug/WARP suite: 126 unique checks
  across 21 modes and 53 ordered Application stages, with retained suites/goldens unchanged. Exact 625 inputs/
  216 evidence files are archived at `2026-10-03T03:57:49.3365819+00:00` before seven byte-identical activations
  ([record](../../TestRuns/SelectionMoveHeaders_2026-10-03.md)). All six native profiles pass at
  `2026-10-03T04:29:21.8934408+00:00`; independent verification and immutable archival retain 614 inputs/
  20,351 evidence files at `2026-10-03T04:39:26.9716006+00:00`. Coexisting header/size/lifecycle policies and
  measured G1 stay open.

- [x] Selection nudge core — S1c1 privately qualifies 80 unique geometry/history/cancellation/worker controls;
  exact 629 inputs/158 evidence files are archived before eight native activations
  ([record](../../TestRuns/SelectionNudge_2026-10-03.md)). All six native profiles pass at
  `2026-10-03T06:34:34.8129025+00:00`; independent verification/archive retain 619 inputs/23,770 evidence files
  at `2026-10-03T06:41:31.8464318+00:00`. Typed keyboard host routing, other selection geometry and G1 stay open.
  Native r1 passes ARM64 cross-builds and full x64 ASan, then Release's ISA scan refuses terminal switch data;
  actual exit 1 and the exact failed image/618 sources/24,357 evidence files are archived before private repair.

- [x] Selection nudge host — S1c2 full private WARP proof passes 84 literal checks/11 modes and 173 fault/order
  checks/26 modes. Exact 635 inputs/216 evidence files are archived before ten byte-identical activations
  ([record](../../TestRuns/SelectionNudgeHost_2026-10-03.md)). Native r1 starts at
  `2026-10-03T07:01:39.6988523+00:00` against 623 frozen inputs. All six profiles pass; actual exit 0 at
  `2026-10-03T07:30:25.0520148+00:00`. Independent verification/archive retain 623 inputs/1,064 evidence files
  at `2026-10-03T07:36:56.8764451+00:00`. Other selection producers and measured G1 remain open.

- [x] Eligible Canvas/Image Size outline closure — S3b1b1b1a private r3 passes 46 literal and 242 fault/order
  checks, all 57 registered stages and exact retained suites. Immutable archival retains 637 inputs/214 evidence
  files before ten byte-identical activations; independent preflight verifies 468 unchanged counterparts.
  Native r1 PID 110580 starts at `2026-10-03T08:01:51.9057802+00:00` against 625 frozen inputs
  ([record](../../TestRuns/SelectionSizeOutline_2026-10-03.md)). Six profiles and independent archive remain required.
  Native r1 stops after ARM64 cross-builds and the passing new size controls: x64 ASan's existing clipboard fault
  fixture cannot acquire the clipboard. Its exact 625 inputs/4,374 evidence files are archived before two setup
  helpers adopt thirty readiness attempts. Native r2 PID 66964 starts at `2026-10-03T08:32:45.4833456+00:00`;
  r2 is interrupted without an actual exit receipt. Its 625 inputs/4,377 evidence files are archived
  unqualified, preserving the unknown exit. Fresh r3 PID 82736 starts at
  `2026-10-03T09:11:22.7778174+00:00` through a hidden durable launcher after exact preflight (466 unchanged
  counterparts). All six profiles and independent verification/archive are still required.
  Detached r3 returns actual exit 1 before compilation because rg is absent from its PATH. Its 625 unchanged
  inputs/33 evidence files are archived; r4 supplies explicit tool paths for the fresh complete matrix.
  R4 preflight passes; PID 91044 starts at `2026-10-03T09:21:55.6955366+00:00`. Qualification remains pending.
  ARM64 builds and the complete x64 Asan suite pass; Release/Debug and independent archive remain required.
  Primary review adds opacity/Crop authority guards in the private follow-up before size-card completion.
  R4 now passes all six profiles, actual exit 0 at `2026-10-03T09:56:45.0024453+00:00`; independent archival
  retains 625 inputs/1,739 evidence files at `2026-10-03T10:02:54.6386235+00:00`. The guard correction is next.
- [x] Eligible New Canvas outline closure — S3b1b1b1b has a separate source-only 511-input seed. Primary prepares
  Save/Discard/Cancel phases, exact opening/card/coexisting authority and one-ordinal close/write/replacement.
  Two reviewed literal scenarios and 52 primary fault/order scenarios include the shared size authority correction
  ([record](../../TestRuns/SelectionNewCanvasOutline_2026-10-03.md)). P1/G1 remain open.
  The qualified size r4 archive is pinned. Private r1's missing enum include is preserved before correction;
  fresh r2 is building. Runtime qualification and exact archive remain required.
  R2's ten history-observer fixture failures are archived before correction. Private r3 Rebuild and full WARP
  runtime pass: all 59 stages, 20 literal assertions and 536 fault/order assertions. Immutable archival retains
  639 inputs/216 evidence files at `2026-10-03T10:25:34.5400849+00:00`. Fresh native qualification remains required.
  Production and numeric legacy goldens remain unchanged by this fixture repair.
  Eight exact native activations and independent 472-counterpart preflight pass. Native r1 PID 23260 starts
  at `2026-10-03T10:36:27.9675830+00:00`, freezing 627 inputs for all six profiles. Eligible Close Project
  preparation has a separate 513-source seed; its compiler waits for this native proof/archive.
  All six profiles pass at `2026-10-03T11:05:34.9785354+00:00`; independent verification and immutable archival
  retain 627 inputs/23,056 evidence files at `2026-10-03T11:14:26.1987889+00:00`, completion
  `675F17599A2288391C7E24993101D326B46AEEBFB4FC77D75F55990876E366DD`. This also qualifies the shared size guard.
- [x] Eligible Close Project outline closure — S3b1b1b1c qualifies prompt-aware Cancel/Discard/Save and current-only
  close/save/lifecycle ordering, with no size card or coexisting edit. Full private and native suites retain
  14 literal assertions/two modes and 477 fault/order assertions/46 modes, all predecessor controls and 61
  ordered application stages. Native r3 passes all six profiles; independent verification and immutable archival
  retain 630 inputs/23,526 evidence files at `2026-10-03T14:18:25.4242700+00:00`, completion
  `5245F52417F232AB52E4F33BE125474FFE5C54C0802975549C5DD3890AC54E53`
  ([record](../../TestRuns/SelectionCloseProjectOutline_2026-10-03.md)). Original pause and ISA failure remain
  archived; the exact padding repair retains 113 new parser controls with the allowlist unchanged.
- [x] Eligible New/window/session outline closure — S3b1b1b1d private r2 passes all 63 stages, 43 literal
  assertions/six real-producer routes and 1,529 fault/order assertions/127 modes. All 645 inputs and 220 evidence
  files are independently verified and archived. All six native profiles and independent verification pass;
  immutable archival retains 632 inputs/23,532 evidence files at `2026-10-03T15:33:16.1738417+00:00`, completion
  `97C09D00D3CF1CD164BD0A3C11E6E0CAF1C12F7D4FA1BF210E66E7083D169E70`
  ([candidate](../../TestRuns/SelectionLifecycleOutline_2026-10-03.md)).
- [ ] P0.1 — Measure the baseline and establish performance instruments. CPU benchmarks and paired ownership measurements recorded; UI/ETW/IO coverage remains open.
  - [x] Add the scalar and whole-output CPU benchmark with checksums and a bounded smoke gate.
  - [x] Capture the 4K and 45 MP before measurements on R1.
  - [ ] Add the remaining UI, ETW, IO and operation-classification measurements.
  - [x] Add private ETW capture/decode and the marked local-drive perf-root resolver for the P0.7 free gate.
  - [x] P0.1a — Instrument message dispatch, window handlers, completion drains, individual result apply, UI actions and frame recording with optional allocation-free timing capture and ETW; validate a WPR profile. Full x64 gates and all ARM64 cross-builds passed; disabled scope costs 4.4 ns median, capture 42–43 ns.
  - [ ] P0.1b — Drive operation and UI/IO scenarios, record machine metadata and baseline distributions, and classify each §5 row.
    - [x] Implement the real application-loop driver, machine report and 15 operation smoke scenarios; all x64 gates and ARM64 cross-builds pass.
    - [x] Acquire five per-file CC0 RAW samples and verify sizes, SHA-256 and signatures; inspect the tiled DNG layout. Offline downloader controls pass.
    - [x] Expand the local corpus to all nine planned v1 extensions, with smaller correctness files, additional compression/layout candidates, and saved CC0 legal text/per-file provenance. Decode/develop qualification remains P4 work.
    - [x] Complete the benchmark driver configuration gates, including ASan, offline corpus controls and the tightened ISA parser.
    - [x] Record Release hardware/WARP distributions at 4K, 45 MP and 100 MP. All six size/device scenarios complete with 15 operation rows each.
    - [ ] Extend baseline coverage to the remaining §5 operations, I/O and the non-cancellable calls before their migrations.
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
- [x] P0.8 — Add scalar kernel dispatch and the x64 VEX scan. Full x64 gates, all-image instruction scans and all ARM64 cross-builds passed.
  - [x] Implement startup tier selection and scalar entry points without changing legacy pixel math. All six bodies and five helpers match the prior source exactly after whitespace/namespace normalization.
  - [x] Add strict `--isa` parsing and forced-tier tests; reject unavailable/unimplemented tiers. Only Scalar is implemented in this foundation slice.
  - [x] Scan decoded executable instructions, attribute them through PDB section contributions, and prove the gate with positive/negative compiled fixtures. All five images pass in every x64 configuration; exact runtime-owner exceptions and EVEX controls verified.
  - [x] Complete all configuration gates and record baseline ISA provenance before committing/pushing. 49 dispatch/CLI checks, real-process refusal tests, full x64 gates and all ARM64 cross-builds pass.
- [ ] P1 — Move blocking operations off the UI thread and meet the G1 exit criteria.
  - [x] P1.1a — Separate immutable adjustment/filter work from session apply; retain the direct inline API and prove stale-result, mask, history and failure behavior. Full x64 gates and all ARM64 cross-builds passed; 63 new document checks and the WARP save-path checks pass.
  - [x] P1.1b — Add cooperative cancellation through the complete filter graph, declare peak reservations and verify unchanged scalar pixels. Full x64 gates and all ARM64 cross-builds passed; all 13 measured 4K kernel checksums match the baseline.
    - [x] Add private cancellable sparse reads and thread cancellation through filters, coverage, blending, cropping, masks and thumbnails.
    - [x] Declare the complete commit allocation peak and cache selection point counts for constant-time admission estimates.
    - [x] Complete numeric/cancellation/peak tests, Release cancellation measurements and all configuration gates. Maximum observed stop-to-return was 9.659 ms on the measured inputs; this is not a worst-case cancellation guarantee.
  - [ ] P1.1c — Wire preview/histogram/commit jobs, settle continuations and all discard-confirmation callers; validate real-loop responsiveness and ordering.
    - [x] P1.1c.1 — Split panel metadata/grid planning from pixel preparation; keep the direct API inline over the same worker graph. Full x64 gates and all ARM64 cross-builds passed.
    - [x] P1.1c.1 — Add distinct panel-lifetime and settings stamps, cancellable preview/histogram work and complete preview/prepare allocation estimates. All 43 deferred-state checks pass in each x64 configuration.
    - [x] P1.1c.2 — Wire one-active/latest-pending previews and once-per-opening histograms; prove real worker coalescing, Cancel/reopen and view input during computation. Full x64 gates and all ARM64 cross-builds passed.
    - [x] P1.1c.3 — Wire ordered commits with a busy panel and working Cancel, together with all dependent action continuations.
      - [x] P1.1c.3a — Qualify the busy-panel control contract independently: captured Curves/Levels/Spectrum drags stop, mutation controls disable, Cancel remains enabled, and idle prerequisites/numeric values survive under the existing focus-loss rule. Full x64 gates and all ARM64 cross-builds passed; this preparation does not activate asynchronous commits.
      - [x] P1.1c.3b — Implement and qualify immutable commits under the shared history ordinal/barrier, keep settings frozen through UI apply/drop, and distinguish pending/succeeded/cancelled/failed/stale outcomes. Full x64 gates and all ARM64 cross-builds passed; activate production OK/write callers atomically with P1.1c.3c.
      - [x] P1.1c.3c — Production migration qualified in all x64 test configurations and ARM64 cross-builds. Route every write/discard caller through owned continuations, including ordered multi-path batches, copied size drafts and session-end generation. No write/replacement follows a failed required commit.
        - [x] Qualify the allocation-free scheduler UI-action source, full-queue wake-up, fairness, reentrancy and borrowed lifetime. Application registration and its initial bridge are tracked below; production caller activation remains open.
        - [x] Qualify sequential operations under one input ordinal with distinct acquisition stamps; prove commit-then-size before younger Undo. Full x64 gates and all ARM64 cross-builds passed.
        - [x] Qualify per-entry continuation retirement before observed request release, including retained observers and FIFO ring wrap. Full x64 gates and all ARM64 cross-builds passed; production receipt publication remains with the runner.
        - [x] Qualify owning completion receipts and typed event-time path batches, write arguments, complete size drafts and layer/lifetime inputs. All x64 configurations and ARM64 cross-builds passed; producer admission and runner activation remain open.
        - [x] Qualify the Application source bridge with real owned panel requests and event-gated worker commits; verify deferred outcome publication, error preservation, acknowledged cancellation, stale inputs and shutdown. All x64 configurations and ARM64 cross-builds passed; all production caller migrations remain open.
        - [x] Qualify capacity-based request admission and a combined queued/active byte budget, with immutable retained input and release after publication. Full x64 gates and all ARM64 cross-builds passed; actual production producer activation remains open.
        - [x] Preflight borrowed path/name values and reserve later selected output/staged state before production activation.
          - [x] Qualify borrowed-input builders and request-owned selected paths: all x64 gates and ARM64 cross-builds passed, with 77 request checks per normal run and 154 with ASan stress; producer/staged-runner migration remains open.
      - [x] P1.1c.3d — Guard all history/mutation entry points, prove the real commit/continuation order with event gates, migrate harness observations, and run the full configuration matrix.
    - [x] Replace settle/discard booleans with owned continuations across save, export, open/import, new/size, project/window close and session end.
    - [x] Prove pending/failure/replacement/ordering behavior with event-gated WARP tests, preserve direct numeric goldens and remeasure real-loop latency; all six configurations passed and all 15 Release 4K operations completed. Residual costs keep P1/G1 open.
  - [ ] P1.2–P1.3 — Add per-layer GPU adjustment previews and asynchronous GPU completion/readback.
    - [x] P1.2 — Per-layer adjustment target with selection coverage and layer-space grain; reusable LUT/cube textures and worker-built lookups. All x64 tests and ARM64 cross-builds pass. Release 4K Levels preview: 78.856 → 5.718 ms; CPU commit reuse tradeoff recorded; G1 remains open.
    - [ ] P1.3 — Device capability probe, event-driven fences, bounded readback slots, off-screen composition/sampling, hidden completion and device-loss lifetime tests; migrate all readback callers.
      - [x] P1.3a–c foundation — Capability route selection, resumable reservations/FIFO, shared event waits and bounded staging with native monitored-fence/lent-device/shutdown qualification; all x64 gates and ARM64 cross-builds pass ([evidence](../../TestRuns/GpuCompletionFoundation_2026-09-30.md)). Explicit fallback runtime coverage remains in the child plan.
      - [ ] P1.3d–f — Utility composition and real host caller migration, restart/error policy and real-loop/G1 measurements.
        - [x] Qualify separate utility targets, tiled adopted worker outputs, bounded rebind restart, explicit EnqueueSetEvent fallback, and Merge/adjustment-opening continuations. All x64 gates and ARM64 cross-builds pass ([evidence](../../TestRuns/GpuUtilityReadback_2026-09-30.md)).
        - [x] Migrate PNG/JPEG export through detached worker encoding and qualify bounded additional-event recovery on actual hardware and both completion routes; [evidence](../../TestRuns/ProjectWriters_2026-10-01.md).
        - [ ] Copy Merged qualifies below. Migrate wand, clone and sampler callers; measure the real loop before P1.3 closeout.
  - [ ] P1.4–P1.6 — Migrate strokes, raster edits, transforms, shapes and selection work with preserved input ordering.
    - [ ] P1.4 — Recorded strokes behind commits, lazy tiled sources, asynchronous heal/warp/mouse-up and younger-input close protection.
    - [ ] P1.5 — Worker raster edits, gradients, lifted pixels, transforms, shapes, masks and Layer via Copy; preserve eager semantic mask thumbnails.
      - [x] A1/A2 — Basic Fill, Clear/selected Delete and Invert preparation and host activation; exact pixels, cancellation/staleness and ordered history pass all configurations ([evidence](../../TestRuns/RasterEdits_2026-10-01.md)).
      - [ ] A3 — Basic raster-edit real-loop timing; six-profile functional qualification passed, but the display-off presentation guard refused measurement. Preserved qualified binaries await an active desktop; [raster-edit child](AsyncRasterEdits_2026-10-01.md). Owner/history/upload bounds remain P1.9/G1.
      - [x] A4.1–A4.5 — Ordered worker gradient previews/commits, deferred metadata and cancellation/stale/resource gates pass all configurations; 105 new document checks and 33 production host scenarios ([evidence](../../TestRuns/GradientWorkers_2026-10-01.md)).
      - [x] A5.1 — Detached lift/move preparation, private cancellation/admission and missing numeric references qualify with clean configuration rebuilds ([evidence](../../TestRuns/LiftedPixelsPreparation_2026-10-01.md)).
      - [x] A5.2.1 / A5.3 — Deferred production pixel-move previews and ordered pointer/nudge commits, exact ownership/staleness/cancellation and input-time Shift pass all configurations ([evidence](../../TestRuns/LiftedPixelsHost_2026-10-01.md)). Gradient benchmark scenarios/runner controls also qualify functionally.
      - [x] A5.2a — Immutable opening coverage/lift reuse, exact cache authority, one preparation across previews/commit and complete retirement qualify in the frozen six-configuration matrix: 33 independent assertions plus actual host controls ([evidence](../../TestRuns/PixelMoveOpeningReuse_2026-10-01.md)). Drag/geometry/upload measurements remain open.
      - [ ] A4.6 / A5.4–A8 — Gradient real-loop timing, Cut/floating transforms, distortion, shapes/masks and complete caller qualification; [execution sequence](AsyncRasterEdits_2026-10-01.md).
      - [x] A5.4b — Atomic private Copy/Copy Merged/Cut and exact Clear/Fill Mask history publication, sealed
        composition/private serial authority and held-owner memory floors; 92 controls and all six configurations
        pass ([record](../../TestRuns/ClipboardCommandPublication_2026-10-01.md)). Production clipboard callers qualify below;
        constant-time capture and G1 remain open.
      - [x] A7.1/A7.2a — Worker Shape release with exact pixels, owned event-time geometry/color, complete
        document/history publication and actual pointer/cancellation/retirement controls qualifies in all six
        profiles: 76 document controls and 199 host checks against 487 frozen inputs
        ([record](../../TestRuns/ShapeWorkers_2026-10-01.md)). Redraw, live producer settlement and measured G1 remain open; settled masks/Layer via Copy qualify below.
      - [x] A7.3a.1 — Worker selection/uniform masks with eager semantic thumbnail and joint target/selection/
        history publication qualify settled production callers in all six profiles: 84 document controls and
        201 host checks against 496 inputs ([record](../../TestRuns/MaskWorkers_2026-10-01.md)). Live gesture
        settlement and measured capture/apply/upload/G1 remain A7.3a.2/P1.4/A6/S3.
      - [x] A7.3b.1 — Settled Layer via Copy/Ctrl+J worker preparation and publication qualify against 503 frozen
        inputs in all three x64 suites and all three ARM64 cross-builds: 34 document and 214 host controls per
        x64 profile ([record](../../TestRuns/LayerCopyWorkers_2026-10-01.md)). Live producer settlement,
        source capture/history/chrome/upload/G1 and the full P1 clean exit remain open.
      - [x] A6.1 / A6.2 core — Detached distortion, transform pixels/publication, floating opening/merge/restore
        and exact-predecessor history qualify in all six clean profiles against 529 frozen inputs, including
        409 independent controls per x64 suite and unchanged legacy goldens
        ([record](../../TestRuns/TransformWorkerCore_2026-10-02.md)). Worker Clear preserves committed pixel
        limits without charging source-external scratch tiles. Production transform callers, bounded previews,
        settlement and G1 remain open.
      - [x] A6.3 T1 core — Sealed transform cancellation and nested floating closures qualify in all six
        profiles against 533 inputs: 467 core controls per x64 suite
        ([record](../../TestRuns/TransformCancellation_2026-10-02.md)). Persistent host rollback ownership,
        production routing, preview/settlement, retirement gates and G1 remain open.
      - [x] A6.3 T3 core — Complete sealed display-only previews qualify in all six profiles against 537 inputs:
        44 new assertions plus the prior 467 core controls per x64 suite
        ([record](../../TestRuns/TransformPreviewCore_2026-10-02.md)). Color and mask previews cap both dimensions
        at 1024; sparse assembly stays private. Production coalescing, deferred display, settlement and G1 remain open.
      - [x] A6.3 settlement foundation — Selected transform release/header/command, input-time nudge and
        floating opening/merge/cancel routes qualify against 540 frozen inputs in all six profiles: 344 real
        host controls and unchanged existing tests ([record](../../TestRuns/TransformSettlement_2026-10-02.md)).
        Deferred preview/inline-bypass/Alt-duplicate/device/shutdown/G1 and the full P1 clean exit remain open.
      - [x] A6.3 deferred preview foundation — The opt-in model, writer/outline guards and preview host pass
        all three clean x64 runtime suites and all three ARM64 cross-builds against 548 inputs: 147 model/guard
        controls, 90 preview host controls and the existing 344 settlement controls
        ([record](../../TestRuns/DeferredTransformPreview_2026-10-02.md)). Production ordinary-transform policy,
        Alt-duplicate/device/shutdown/G1 and the complete P1 exit remain open.
      - [x] A6.3 duplicate gesture — Same-ordinal worker source settlement/opening, latest pointer metadata,
        event-time release/Escape and prepaid rollback qualify in six clean profiles against 559 frozen inputs:
        82 core and 131 duplicate host controls plus the prior preview/settlement/goldens
        ([record](../../TestRuns/DuplicateGestureWorkers_2026-10-02.md)). The approved DxUi API-revision-3
        update and both strict ISA parser patterns qualify together. Ordinary deferred policy, remaining
        duplicate stale/distort/lifetime scenarios and measured G1 are separate unfinished gates.
    - [ ] P1.6 — Ordered selection jobs and bounded coverage/display caches; stale-result and cancellation tests;
      [execution child](SelectionWorkers_2026-10-01.md) covers geometry/history, every overlapping writer,
      gesture/source acquisition and required admission/cancellation proof.
      - [x] S0a/S1a/S3a core — Complete Select All/Deselect and outline-move End/Cancel preparation, exact
        authority/history and retained retirement pass 312 checks in every x64 suite and all three ARM64 clean
        cross-builds against 423 frozen inputs ([evidence](../../TestRuns/SelectionCommandPreparation_2026-10-01.md)).
        Host routing, live gesture/stroke ordering, Clipper/coverage/display bounds and G1 remain open.
  - [ ] P1.7–P1.9 — Migrate files and sizes; remove remaining history, thumbnail and upload stalls; measure every G1 operation.
    - [ ] P1.7 — Worker file classification/open/import/save/export/clipboard, positional paths, bounded retries, banded codecs, atomic outputs and per-path serialization.
      - [x] F1/F2 — Worker classification/cached routing and cancellable banded codecs with atomic export staging pass all x64 suites and ARM64 cross-builds; [file child](AsyncFileProcessing_2026-09-30.md), [evidence](../../TestRuns/FileFoundation_2026-09-30.md). Open/import/save/export/clipboard host workers, pending-IO faults and measured bounds remain open.
      - [x] F3a/F4a/F4a.1 — Cancellable project helpers and direct bounded file/ZIP asset streams pass all x64 suites and ARM64 cross-builds; [evidence](../../TestRuns/ProjectStreams_2026-09-30.md). Three 4K/45 MP Release observations per mode quantify memory retention and staging IO; production host graphs remain open.
      - [x] F3a.1 — Refuse oversized later project assets from frame dimensions before allocating output, using independent remaining image/mask totals; all x64 suites and ARM64 cross-builds pass ([evidence](../../TestRuns/ProjectPixelBudgets_2026-10-01.md)). Adaptive admission and open/import host jobs remain F3b.
      - [x] F3b.1/F3b.2 — Adaptive project metadata probes/reopened decode ceilings and detached ordered import preparation pass all configurations; 69 load/resource and 31 import checks ([evidence](../../TestRuns/FileLoadPreparation_2026-10-01.md)). Production open/import workers and measured admission/apply remain F3b.3.
      - [x] F3b.3a — Production project opening, separate probe/decode reservations, 45 host gates and replacement continuation pass all configurations and 22-row hardware/WARP runs; [evidence](../../TestRuns/ProjectOpenJobs_2026-10-01.md). Warm opening: 18.675 ms at hardware 4K / 120.355 ms at 45 MP; 5.079 ms apply / 50.517 ms frame at 45 MP keep G1 open.
      - [x] F3b.3b — Detached image-import probe/preparation/apply, opened-source caps and 130 host cases pass all configurations and 24-row hardware 4K/45 MP/100 MP and WARP 4K measurements; [evidence](../../TestRuns/ImageImportJobs_2026-10-01.md). Warm 45 MP new-canvas import: 169.732 ms; existing-layer apply 11.403 ms leaves P1.9/G1 open.
      - [x] F4b.1/F4b.2 — Project-save and PNG/JPEG export graphs pass all x64 suites, ARM64 cross-builds and 21-row hardware 4K/45 MP and WARP 4K measurements; [evidence](../../TestRuns/ProjectWriters_2026-10-01.md). Save retains captured revision/publication truth; export detaches GPU resources before encoding and cannot replay writes after device changes. Remaining IO faults and measured 2 ms overruns keep F4/P1/G1 open.
      - [x] F5a/F6a — Detached clipboard byte/metadata helpers and folder rotation/recovery faults, including allocation-safe bounded cleanup and backup-only retries; all 77 new checks and configurations pass ([evidence](../../TestRuns/ClipboardAndRecovery_2026-10-01.md)). Production clipboard migration qualifies below; pending IO waits, intermittent real save cause and measured cancellation bounds remain open.
      - [x] F5b.1 / A5.4a / owner routing primitive — Native Copy pixel, sealed Win32 transfer and fixed owner
        multiplexer pass 33/125/31 new assertions in every x64 suite and all three ARM64 clean cross-builds
        against 434 frozen inputs. Earlier ISA/folder/clipboard failures retain exact inputs and images
        ([record](../../TestRuns/CopyClipboardPreparation_2026-10-01.md)). Production callers and G1 stay open.
      - [x] F5b.2a — Registered receipt and worker SHA-256 payload binding, exact partial publication and
        private-origin controls qualify in all six configurations against 456 frozen inputs: 25/45/34 new
        assertions plus all 125 transfer controls ([record](../../TestRuns/ClipboardReceiptBinding_2026-10-01.md)).
        Full host/codec admission, production callers, retries and G1 remain open.
      - [x] F5b.2b.1 — Capped memory PNG/JPEG encoding with pre-growth derived/lower ceilings, bounded commits,
        cancellation and exact/over-cap proof: 160 stream/34 cap controls and 103 existing codec checks pass all
        six configurations against 462 frozen inputs ([record](../../TestRuns/CappedMemoryEncoding_2026-10-01.md)).
        Detached Paste and production clipboard admission/callers/retries qualify below; remaining file timing and lifecycle gates stay open.
      - [x] F5b.2b.2 — Detached Paste candidate document/history, guarded publication and producer-order clipboard
        inspection qualify in all six profiles: 62 Paste, 45 availability and unchanged 125 transfer controls
        ([record](../../TestRuns/ClipboardPastePreparation_2026-10-01.md)). Actual host graphs remain F5b.2b.3.
      - [x] F5b.2b.3 / A5.4c / F5c.2 — Native Copy/Cut/Paste/Copy Merged and metadata-only New Canvas graphs
        qualify against 480 frozen inputs: all three x64 gates and all three ARM64 clean builds pass. Native
        control sets contain 268 command, 61 prefill, 41 delayed-render and 88 publication-fault checks
        ([record](../../TestRuns/ClipboardHost_2026-10-01.md)). F5d/P1.9/G1 remain open.
      - [x] F5c.1 — Native one-shot retry waits and Application GPU/retry source routing; 117 retry controls,
        six additional transformed Copy fixtures and two routed GPU scenarios qualify in all six configurations
        ([record](../../TestRuns/RetryWaitRouting_2026-10-01.md)). Actual clipboard graphs remain F5c.2.
      - [x] F6a.1 — Bounded native rollback recovery and real child/directory-reader release/exhaustion controls; all configurations and 33 rotation checks pass ([evidence](../../TestRuns/RasterEdits_2026-10-01.md)). External reader/filter identity and overall F6 remain open; timer resource faults qualify below.
      - [x] F6a.2 — Native recovery timer create/arm/wait resource faults, healthy-path avoidance and backup-only retry qualify in all configurations; 40 rotation controls pass ([evidence](../../TestRuns/LiftedPixelsPreparation_2026-10-01.md)). Original save causes and overall F6 remain open.
      - [x] F6a.3 — Native destination-to-backup retry/cancellation and timer resource failures qualify in all
        six configurations with 52 real reader/fault assertions per x64 profile. Old bytes and injected final
        failures are preserved ([record](../../TestRuns/PanelCancellationHistoryPublication_2026-10-01.md)).
      - [x] F6a.4 — Bounded native staging-publication retry and complete backup/foreign-destination preservation;
        269 controls pass in every x64 profile and all ARM64 clean builds qualify ([record](../../TestRuns/P1LifecycleShutdown_2026-10-02.md)).
      - [x] Qualify D17 unique exclusive project staging, checked write/flush/close, STORE PNG members, DEFLATE compatibility and distinguishable failed-overwrite preservation. All x64 suites and ARM64 cross-builds pass; [bounded save record](../Done/ProjectSaveDurability_2026-09-30.md). Worker migration and fault injection remain open.
      - [x] Qualify bounded positional-path parsing, marker-aware mode preflight and owning startup handoff; all x64 gates and ARM64 cross-builds pass. File processing jobs remain pending.
    - [ ] P1.8 — Immutable Image Size/Canvas Size jobs under the shared history barrier; copied-draft and cancellation tests.
      - [x] Qualify cancellable resizers, immutable size jobs, peak reservations and event-gated host tests. All x64 suites and ARM64 cross-builds pass; hardware 4K/45 MP and WARP 4K each complete 18 operations. [Size child](AsyncDocumentSizing_2026-09-30.md) and [evidence](../../TestRuns/AsyncSizing_2026-09-30.md).
      - [ ] Meet the measured 2 ms apply target; history/chrome/first-upload work remains P1.9.
    - [ ] P1.9 — Incremental history accounting/moves, uint64 session limit, lazy image thumbnails and first-upload ring.
      - [x] P1.9a — Explicit unsigned 64-bit session limit min(physical RAM / 4, 2 GiB), 256 MiB query fallback/class default, retained legacy trim golden and machine host wiring. Full matrix passes; the previously failed 45 MP sizing benchmark now retains its undo and completes.
      - [x] P1.9b — Retained immutable selection capacity and safe pending/restore snapshot moves pass all x64 suites and ARM64 cross-builds; [history child](HistoryStorageAccounting_2026-09-30.md), [evidence](../../TestRuns/HistoryStorage_2026-09-30.md). Hardware 4K/45 MP each complete 18 operations. Incremental accounting, thumbnails, uploads and the 2 ms apply target remain open.
      - [x] P1.9 H1 foundation — Native immutable history views, preconstructed publication and aggregate release for basic raster, gradient and pixel-move commits pass all profiles ([evidence](../../TestRuns/PreparedHistoryPublication_2026-10-01.md)). Other callers, constant-time capture, incremental accounting and the measured G1 exit remain open; Undo/Redo qualification is recorded below.
      - [x] P1.9 H1 destructive color — Prepared after-layer/history, admitted capture and actual host failure gates pass all profiles ([evidence](../../TestRuns/ColorHistoryPublication_2026-10-01.md)). Adjustment-layer closure remains separate.
      - [x] P1.9 H1 size cards — Admitted capture, complete document/history publication and retirement pass all profiles; 50 independent assertions and 22 host scenarios ([evidence](../../TestRuns/SizeHistoryPublication_2026-10-01.md)). Measured apply remains open.
      - [x] P1.9 H1 merge — Admitted metadata probe, full survivor/history publication and complete deferred CPU
        retirement pass the frozen six-profile matrix; 71 document assertions and 17 host modes
        ([evidence](../../TestRuns/MergeHistoryPublication_2026-10-01.md)). Failed scheduler-wait, process-exit
        observation and IO rotation attempts remain preserved; the rotation cause, other callers and G1 remain open.
      - [x] P1.9 H1 Undo/Redo — Complete restore/history and deferred retirement pass the frozen six-configuration
        matrix: 63 document assertions and 28 host scenarios in every x64 suite
        ([evidence](../../TestRuns/HistoryRestorePublication_2026-10-01.md)). The preserved first crop shortcut
        failure is corrected by waiting for acknowledgement without changing its expectations. Other callers,
        metadata capture/accounting, uploads and measured G1 remain open.
      - [x] P1.9 H1 import/project open — Saved-baseline/edit/reset publication and complete session/path/draft
        retirement qualify in the frozen six-configuration matrix: 55 history/157 session assertions plus
        70 project/155 image-import host cases per x64 suite
        ([evidence](../../TestRuns/ImageImportHistoryPublication_2026-10-01.md)). Separate file-operation authority
        preserves live edits on failure and retires them on successful replacement; Undo remains settled-only.
        The history child retains original host/fixture/ISA failure evidence. Other H1 callers, H2/H3/H4 and G1 remain open.
      - [x] P1.9 H1 lifecycle primitive — Optional New/Close saved history, preallocated retirement and exact
        refusal/publication pass 65 independent controls per x64 suite and all six clean profiles against
        561 frozen inputs ([record](../../TestRuns/DocumentLifecycleCore_2026-10-02.md)). Production default
        New, New Canvas card and Close Project activation remains in
        [DocumentLifecycleWorkers](DocumentLifecycleWorkers_2026-10-02.md); H2/H3/H4 and G1 remain open.
      - [x] P1.9 H1 lifecycle host — Production New/New Canvas/Close, sealed admitted capture, saved optional
        history and worker retirement qualify in all six profiles: 115/58/85 actual application controls
        ([record](../../TestRuns/P1LifecycleShutdown_2026-10-02.md)). Broader live contexts, H2/H3/H4 and G1 remain open.
      - [x] P1.9 H2.1 / H4.1 — Immutable history-container roots and optional image thumbnails qualify in the
        frozen six-configuration matrix: 28 root/30 producer assertions and 331 parser controls per x64 suite
        ([record](../../TestRuns/HistoryRootsAndImageThumbnails_2026-10-01.md)).
        Capture shares immutable stack roots; image caches are optional and semantic mask thumbnails stay eager.
        The initial mask-fixture and ASan instruction-proof failures remain preserved in the history child.
        Full document
        capture, incremental accounting, first uploads and measured G1 remain separate.
      - [x] P1.9 H1 adjustment panels — Prepare Begin and OK/Cancel history before owner publication;
        coalesce Cancel, acknowledge prior OK drops, audit dependent callers and retire complete preview owners.
        - [x] H1.6a/H1.6c — Production opening and metadata OK qualify in all six clean configurations:
          515 closure, 210 opening and 58 nested assertions, plus 146 actual host cases in each x64 profile
          ([evidence](../../TestRuns/AdjustmentOpeningAndOkHistoryPublication_2026-10-01.md)).
        - [x] H1.6b/H1.6d–e — Production Cancel routing and combined caller qualification pass all six clean
          configurations: 264 dismissal assertions and 70 actual Cancel host rows per x64 profile
          ([record](../../TestRuns/PanelCancellationHistoryPublication_2026-10-01.md)). Measured owner costs,
          other H1 callers, full record capture/accounting and uploads remain separate in the
          [history child](HistoryStorageAccounting_2026-09-30.md).
    - [ ] G1 — Record every due operation against the UI/apply budgets on available hardware; keep unavailable hardware/Photoshop gates explicit.
Implementation started 2026-09-27. Slices P0.2–P0.8 are complete on x64; the P0 phase remains open. ARM64
Debug, Release and ASan Debug cross-builds passed. Native ARM64 execution is unavailable on this host.

Execution scope, 2026-09-27: the user explicitly authorized implementation through P6, with this opening checklist
updated during work and each completed, validated slice committed and pushed. This supersedes the former P6
authorization restriction. Record concrete decisions in §11 as their phases begin, and expand P6 into its child
plan before coding the format/depth transition. Continue between slices without another implementation approval.
Hardware qualification and Photoshop comparisons remain measured exit criteria; implementation alone cannot
check those boxes. Smaller agents handle bounded changes and independent checks; the primary reviews them and
owns scheduler, GPU, storage, and RAW architecture.

RAW acquisition and file-backed qualification details now live in the future roadmap §6.9 and its opening checklist; the original acquisition record and downloader remain unchanged.

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
This slice was committed and pushed to `origin/main` as `c16f5b6`.

P0.8 progress, 2026-09-27: the immutable Scalar dispatch table, guarded CPU/OS detection and strict `--isa` path
passed the full x64 gates and all ARM64 cross-builds. All five linked x64 images pass the PDB-attributed VEX/EVEX
gate in each configuration. Compiled controls prove baseline leaks, EVEX in AVX2, overbroad runtime exemptions
and missing/mismatched PDBs are rejected. See the [dispatch record](../../TestRuns/KernelDispatch_2026-09-27.md).
Only Scalar is implemented; P2 owns actual SIMD acceleration. P0.1 UI/IO measurements and the P1 migrations are next.
This slice was committed and pushed to `origin/main` as `a7c886c`.

P0.1a progress, 2026-09-27: allocation-free timing scopes now cover production dispatch, WndProc, drain, result
apply, deferred actions and frame construction. All 82 timing/histogram/trace checks pass in each x64 run,
including actual private ETW decode and sanitizer stress. The full x64 gates and all ARM64 cross-builds passed.
Three Release runs measured 4.4 ns median disabled and 42–43 ns with histogram capture. The
[timing record](../../TestRuns/UiTiming_2026-09-27.md) documents scope boundaries and limitations. P0.1b owns
real-loop operation/IO scenarios and baseline classification; P1 still owns asynchronous image work.
This slice was committed and pushed to `origin/main` as `db7fd52`.

P0.1b progress, 2026-09-28: the real-loop driver exercises 15 commands through posted messages, validates results
and waits for idle jobs plus Present before accepting each sample. Full x64 Debug, Release and ASan gates and all
ARM64 cross-builds pass. The [baseline record](../../TestRuns/PipelineBaseline_2026-09-27.md) preserves complete
hardware and WARP 4K/45 MP/100 MP runs. The original incomplete WARP 100 MP run
is retained separately from its successful retry. Public CC0 ARW, CR3, NEF, RAF and tiled DNG files
are acquired with matching upstream SHA-256 values; see the [corpus record](../../TestRuns/RawCorpus_2026-09-27.md).
The 100 MP Gaussian scenario explicitly measures the existing pixel-budget refusal, not an absent preview.
The x64 scan exposed compiler-generated switch-table data that decodes as VEX; a narrowly validated dispatch/table
recognizer replaces that false positive without adding an object or procedure exception.
All 13 parser controls and the current-image scans pass. The driver/corpus slice is qualified; P0.1 stays open for
the remaining operation and I/O baselines. No RAW decode or Photoshop speed comparison is claimed.
The driver/corpus slice was committed and pushed as `e114b9e`; the final WARP 45 MP run completed immediately after.

P1.1a progress, 2026-09-28: immutable filter input/result types and the shared Prepare/Run/Apply path are integrated,
including opening-input validation, successful full-grid preview reuse, eager carried-mask thumbnails and budget
rechecks. Processing failure keeps the panel and last-applied settings; Save/Export stop when the panel cannot
apply. All 63 new document checks, the WARP save-path checks and existing numeric goldens pass in full x64 Debug,
Release and ASan gates; all ARM64 cross-builds pass. See the [work-split record](../../TestRuns/ColorEditWork_2026-09-28.md).
P1.1b owns bounded inner-kernel cancellation and peak reservations, and P1.1c owns asynchronous host wiring.
P1.1a was committed and pushed as `c41805d`.

P1.1b progress, 2026-09-28: private worker reads avoid shared sparse materialization; filters, histogram, coverage,
blending, trim, carried masks and eager thumbnails now propagate cancellation. Large scratch arrays initialize in
bounded chunks. Selection coverage applies the transform per edge without copying the outline; hard coverage uses
cancellable in-place sorting. The allocation estimate covers each stage's live scratch and final result, including
rounding and sparse input copies, and avoids charging a resident exact preview again. All final x64 gates and all
ARM64 cross-builds passed, including curve-allocation and finite-coordinate-overflow review corrections. All 13
4K benchmark checksums match the original baseline; observed cancellation returns and their limits are recorded
in the [P1.1b record](../../TestRuns/FilterCancellation_2026-09-28.md). The host still invokes the graph synchronously
until P1.1c. P1.1b was committed and pushed as `c859bd0`.

P1.1c.1 progress, 2026-09-28: deferred panel metadata, worker padding/preview/histogram graphs, lifetime/sequence
validation, source-cache rules and preparation reservations are implemented. The direct API uses the same graph;
its existing document goldens pass. A cheaper agent supplied cached selection bounds and another supplied
the independent deferred-state tests; the primary owns the worker and publication logic and reviews their changes.
All x64 configuration gates and all ARM64 cross-builds passed. The benchmark no longer requires unused identity
preview preparation; the ISA gate now verifies an additional compiler switch-table pattern without an object
allowlist exception. Evidence is in the [P1.1c.1 record](../../TestRuns/ColorPanelWork_2026-09-28.md).
Production job/continuation wiring follows this boundary and is not yet complete.

P1.1c.1 was committed and pushed as `6a6443c`. P1.1c.2 is complete on x64: the host selects deferred
panels, submits one-active/latest-pending previews and once-per-opening histograms, and invalidates tickets on
panel/document/shutdown transitions. Its WARP test gates actual production jobs and checks input dispatch,
coalescing and Cancel/reopen. The UI benchmark also waits for the frame invalidated by job publication. Commit and
owned file/close continuations remain P1.1c.3; no whole-host nonblocking claim is made at this boundary. All three
x64 test configurations, skill validation, formatting and all ARM64 cross-builds passed. Evidence is in the
[P1.1c.2 record](../../TestRuns/ColorPanelHost_2026-09-28.md).

P1.1c.3a–b preparation, 2026-09-30: the busy-panel contract and ordered worker engine passed all x64 test
configurations, skill/format checks and all ARM64 cross-builds. Evidence is in the
[ordered commit record](../../TestRuns/OrderedColorCommit_2026-09-30.md).
A cheaper agent supplied bounded embedded-host input tests; the primary reviewed the patch, preserved concurrent
engine changes, strengthened restored-input/prerequisite checks, and implemented the commit engine and its event
gates directly. Production OK/write callers remain inline until their owned continuations activate together;
P1.1c.3 is not complete. No G1 or Photoshop speed claim follows from this preparation.
The indexed [completed continuation child plan](../Done/ColorCommitContinuations_2026-09-30.md) records the concrete source wake-up,
request ownership, staged settlement, caller inventory, repeated history acquisitions and activation gates for
P1.1c.3c–d. Its later stroke resolution remains integrated with P1.4; it does not claim that replay is implemented.

P1.1c.3 activation, 2026-09-30: the full typed production runner and caller migration passed all x64 test
configurations and ARM64 cross-builds. [Activation evidence](../../TestRuns/HostContinuationActivation_2026-09-30.md)
records the 84 caller outcome cases, owned Shape release, bounded direct input readers and session-end message
controls. Release's real 4K loop completed all 15 operations; Levels commit dispatch max was 0.083 ms, apply max
3.658 ms and action max 5.898 ms. Invert/Fill/Clear still block for 187/243/270 ms in this run. P1.2–P1.9 and
G1 remain open. The primitive notes below record the state before activation.

P1.1c.3c primitive progress, 2026-09-30: the stable UI-action source and repeated history acquisitions passed the
full x64 gates and all ARM64 cross-builds. Final review strengthened independent owner/document stamp tests.
Release link-layout changes exposed a switch-table ISA scanner false positive; exact PE/PDB and branch proof now
recognizes its observed hoisted image-base setup, with positive/negative controls and an unchanged allowlist.
Evidence is in the [continuation record](../../TestRuns/ContinuationSources_2026-09-30.md). The ownership and host
bridge records below extend these primitives; write/discard stages and production caller activation remain open.
P1 and G1 remain open.
Per-entry continuation retirement also passed the full x64 gates and all ARM64 cross-builds; the
[retirement record](../../TestRuns/ContinuationRetirement_2026-09-30.md) proves each dropped entry's ownership and
observable outcome through the callback. The [owned request/receipt record](../../TestRuns/OwnedContinuationRequests_2026-09-30.md)
also qualifies the actual model in all configurations, with 85 receipt and 21 typed-input assertions per HostTests
invocation (the ASan suite runs both normal and stress).
Production producer admission, staged publication and caller activation remain with the runner.
The [Application bridge record](../../TestRuns/HostContinuationBridge_2026-09-30.md) qualifies real owned numeric
panel requests, registered source ownership, admission/full refusal, exact completion wake-up, success/failure,
late cancellation/replacement, metadata settlement and shutdown in all configurations. All asynchronous history
finishers must notify the common hook after apply/drop; other kinds currently use explicit finisher test seams.
Cancel after acknowledged panel work preserves its known result. The full multi-stage prompt/write/discard runner,
variable-length payload admission and production caller activation remain open; P1/G1 and P2–P6 are not complete.

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

- **Status**: ACTIVE, P1 delivery milestone; the opening checklist is the current execution status.
- **Planned at**: 2026-09-22; detailed survey and adversarial reviews on 2026-09-23. Split into P1 and P2–P6 on 2026-10-04 at the user's request.
- **Pinned dependency**: DxUi `271bd54be24eadf3f03ba5221d1be7be069860f2`, API revision 3.
- **Authority**: This is a non-normative execution record. Current behavior lives in the owning domain specs; durable requirements land there with implementation.
