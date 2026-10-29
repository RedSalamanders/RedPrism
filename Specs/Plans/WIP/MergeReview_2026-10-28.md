# Merge review (merges of 21–28 October 2026)

> Spawned on 2026-10-28 from the request "review all the code merge in the last 10 days for any bugs problem or
> architecture misfit … review the code around the modification for side effect … study if you could find some
> simplification in code or architecture … all this code must be ready for production from resiliency, quality and
> user experience … if you have some question write them in the plan".

- **Status**: DECISION. Findings are verified; questions Q1–Q12 are open; no fix has started.
- **Reviewed**: `0ce2070..85eaef7` on `main`, 15 merges (PRs #4–#18). That is 1,716 files, +296,939/−11,027 lines:
  about 66k production lines in `RedPrism/` and `Common/` (7k of them the generated `Common/BoundedClipper`
  engine), 126k test lines, 44k spec/evidence lines and 7.5k tooling lines. PR #13 (paused P1 consolidation)
  is 285k of it.
- **Method**: nine read-only area reviews (scheduler/lifecycle, GPU transport, display publication/readback,
  document/history/IO, brush/stroke, selection gestures, selection workers + BoundedClipper, Application/chrome,
  build/CI). The lead reviewer then re-read the source for every High and Medium claim. The lead also ran the
  cross-cutting checks: CI state, production activation map, binary composition, admission arithmetic, notice
  text. **Nothing was built or run for this review.** Memory magnitudes are arithmetic from source constants,
  not measurements.
- **Owning specs**: `Core/Core_Architecture.md`, `Core/Core_PerformanceAndResources.md`,
  `Core/Core_CrashHandling.md`, `Document/Document_Tools.md`, `Document/Document_History.md`,
  `Rendering/Rendering_Composition.md`, `Rendering/Rendering_Brush.md`, `UI/UI_ApplicationChrome.md`,
  `UI/UI_Windowing.md`, `IO/IO_ProjectFormat.md`, `Build/Build_Toolchain.md`, `Testing/Testing_Validation.md`,
  `Core/Core_Changes.md`.
- **Coordinates with**: [`UiNeverBlocks_2026-09-22.md`](UiNeverBlocks_2026-09-22.md) (HOLD). Rows marked
  *latent* only block P1 activation. [`AdversarialReview.md`](AdversarialReview.md) is the older static review
  at `97dd25b`; this file does not re-triage it.

This file is a non-normative execution record. Requirements move into the owning domain spec in the slice that
lands them.

## Verdict

The core machinery is careful. No reviewer could construct a Critical defect, meaning ordinary-use data
corruption, a crash, or a hang. Several mechanisms traced clean:

- the `JobScheduler` (ticket/generation ABA, FIFO under suspension, refunds, lock discipline, shutdown order);
- the GPU transport (no immediate context on workers, deferred-context ownership, stale command lists);
- project save durability (temp + `ReplaceFileW`, dirty flag by revision);
- history ownership.

`main` is **not production-ready** yet, for four reasons:

1. **Ordinary commands are refused by memory admission on typical machines.** Select › Inverse and
   Expand/Contract reserve about 2.9 GB for any selection, even a rectangle. Magic Wand reserves about 4.8 GB in
   New mode and 7–9 GB for Shift/Alt-click on any canvas of 2 MP or more. Admission needs that much plus 1 GiB
   of available memory, so the command fails. Brush presses, eyedropper, hue and readbacks charge whole-document
   bytes. The scheduler's own contract (`Common/Jobs.h:54`) says resident inputs are not charged again.
2. **User feedback is broken.** Every "busy" refusal opens a modal notice card that literally reads `{0}…`.
   This window raised those call sites from 6 to 19. Many worker failures say nothing at all. The selection
   cursor flickers while hovering a selection.
3. **The assurance gap is open.** Hosted CI did not run for any of the 15 merges: GitHub refused the jobs for
   billing. The integrated P1 tree has a local x64 Release run; the RedMonitor tree (PR #15) has local x64
   Debug, Release and ASan Full runs. Native ARM64, interactive/manual and paired-performance gates remain open.
4. **The shipping binary carries non-product code.** It holds about 65k lines of test code (140 test/benchmark
   TUs; the base had 26) and about 12–14k lines of dormant P1 code. 317 new `std::terminate()` sites were added
   to product code, and there is no document recovery, so any invariant slip loses unsaved work.

## Production activation map

A user run (no `--self-test`, `--bench`, `--demo` or `--screenshot`) enables these, from
`Application::Run`/`InitializeProcessing`:

- `HistoryResidencySource` (`Application.cpp:308`);
- the async selection cursor (`:319`);
- recorded Hue readback (`:322`);
- `UtilityReadbackOwnerService` (`:229`), with `RecordedDocumentReadbackJob` serving the eyedropper, Hue,
  Copy Merged, Merge, Export, Wand composite, Clone (all layers) and adjustment opening;
- the worker selection commands (Inverse, Expand/Contract, nudge, Wand, masks);
- the worker brush-press opening (`CanDeferCanvasBrush`).

`InitializeResponsiveSourcesOwner` runs only for a test probe or `--bench`. The following are therefore
**dormant** in production:

- async stroke finish/samples and warp live;
- prepared layer rows;
- the committed/pending display;
- selection press/outline/lasso-draft display;
- polygonal press/drawing;
- selection pixel final publication;
- the GPU upload stack;
- color display jobs.

Severity below rates what a user can reach.

## Findings — production reachable

Each row: severity · category · verification (CONFIRMED = mechanism read end to end at source; PLAUSIBLE =
mechanism read, trigger or magnitude unproven).

### High

- [ ] **MR-01** · High · ux · CONFIRMED — **Every busy notice reads `{0}…`.**
  - **Where**: `RedPrism/RedPrism.rc:548` (`IDS_STATUS_BUSY "{0}…"`).
    `RedPrism/ApplicationFiles.cpp:238-243`: `FormatPostedNotice` formats only `IDS_ERROR_UNSUPPORTED_VERSION`.
    `RedPrism/ChromeHost.cpp:1712-1724` shows the raw text in a modal notice card.
  - **Reached from**: 19 `PostNotice(IDS_STATUS_BUSY)` sites (6 at base). `ReportHostAdmission`
    (`ApplicationContinuations.cpp:450-458`) maps every admission failure to it: full queue, quota,
    `E_INVALIDARG`, more than 256 dropped files (`Application.cpp:6136-6140`).
  - **Fix**: use a plain string, or format with the running operation's caption (see Q10). Split
    `ReportHostAdmission` by cause. Add a test that asserts the rendered text, not just the id.
- [ ] **MR-02** · High · resiliency/ux · CONFIRMED — **Inverse, Expand and Contract reserve the worst-case
  engine envelope for any selection.**
  - **Where**: `Common/InverseSelectionWork.cpp:86-111` and `Common/SelectionOffsetWork.cpp:87-111` charge
    `Limits::outputPoints` 8M × 16 B, `outputLoops` 2.67M × (24 B + 256 B ownership) and a 128 MiB workspace,
    about 1.0 GB. That is counted in both preparation and retirement.
    `RedPrism/ApplicationInverseSelectionJobs.cpp:358` (and the Offset twin) adds
    `PreparedSelectionOutline::OutputMemory(2.67M, 8M)`, about 0.8 GB.
  - **Admission**: the request must fit in `min(availPhys, availCommit) − 1 GiB − outstanding`
    (`Common/MemoryAdmission.h:22-46`). With nothing outstanding it is refused at once.
  - **Impact**: Ctrl+Shift+I on a rectangle shows "The selection edit could not be completed" when less than
    about 4 GB is free. While it runs, it starves every other admission.
  - **Fix**: AR-4 (size from the actual input, with hard caps as refusal limits, not reservations).
- [ ] **MR-03** · High · resiliency/ux · CONFIRMED — **Magic Wand New/Add/Subtract reserve 4.8–9 GB on
  ordinary canvases.**
  - **Where**: `Common/WandLayerSelectionWork.cpp:597` sets `geometryGrowthBytes = 3·edges·16 + 3·loops·(sizeof
    loop + 128)`, with `edges = min(4·pixels, 8M)`. That is about 1.6 GB for any canvas of 2 MP or more.
    `:612-651` adds 2 × the boolean output (about 1.75 GB) plus the workspace, in both phase and retirement.
    `RedPrism/ApplicationWandLayerHostJobs.cpp:378-385` adds the outline. All three modes route to the worker
    (`Application.cpp:7576-7584`).
  - **Impact**: Shift/Alt-click with the wand fails with the "not enough memory" message on most 16 GB machines.
  - **Fix**: AR-4. Interim (Q1): bound the trace by the filled-region edge count after flood fill, not by canvas
    pixels.

### Medium

- [ ] **MR-04** · Medium · resiliency · CONFIRMED — **A brush, eraser, heal, blur or clone press charges the
  whole resident document with no waiting.**
  - **Where**: `Common/StrokeOpeningWork.cpp:195-243` starts from `source.RetirementMemoryBytes()` and adds a
    tile bound and the layer again. `RedPrism/ApplicationStrokeOpeningJobs.cpp:329` submits with
    `allowMemoryWaiting=false`.
  - **Impact**: on large documents with modest free RAM the press does nothing, and silently (MR-08). The
    pre-window synchronous path never refused.
  - **Fix**: AR-4.
- [ ] **MR-05** · Medium · performance/resiliency · CONFIRMED — **Every utility readback (eyedropper, Hue,
  Copy Merged, Merge, Export, Wand composite, Clone all layers) rebuilds full-document GPU residency privately
  and reserves 4× the document.**
  - **Where**: `Common/UtilityReadbackAdmission.cpp:44-56` sets GPU = 4 × dense document bytes + …, CPU = 2 ×.
    `RedPrism/RecordedUtilityGraph.cpp:49-61` creates a fresh `CanvasRenderer` per job.
    `CanvasRenderer.cpp:1066-1074` (`RasterView` → `TextureFor`) uploads whole rasters plus mip chains even
    for a 1×1 region.
  - **Impact**: first-sample latency scales with document size (unmeasured). On integrated or 4 GB GPUs, large
    documents get refused (silently for sampler/Hue/Copy Merged/Clone, MR-08).
  - **Fix**: AR-4 plus AR-11 (region-bounded upload for utility recording; reuse pipeline objects per device
    generation).
- [ ] **MR-06** · Medium · resiliency · CONFIRMED — **Merge and Copy Merged keep their whole admission envelope
  charged for the life of the output raster.**
  - **Where**: `RedPrism/ApplicationMergeJobs.cpp:452` builds `held·2 + memory·2`. The default
    `BindForRetirement` (`RedPrism/RecordedDocumentReadbackJob.cpp:1017`) moves `record.reserved` into a
    retained slot. `Common/Document.cpp:1050` (`AllocateChargedStorage`) ties it to the merged raster.
  - **Impact**: each merge lowers every later admission (it double-counts against OS availability) and pins one
    of 64 retained slots. This compounds MR-02 to MR-05. Spec-intended (`Core_PerformanceAndResources.md:1453`).
  - **Fix**: override `BindReadbackMemoryForRetirement` with `BindPersistentOutputForRetirement(outputBytes)`,
    as the Wand composite already does (`ApplicationWandCompositeHostJobs.cpp:114-120`). Update the spec line.
- [ ] **MR-07** · Medium · ux · CONFIRMED — **The selection cursor flickers or lags over a selection.**
  - **Where**: `RedPrism/ApplicationSelectionCursorJobs.cpp:100-159`, `RedPrism/SelectionCursorRequests.h:44-66`,
    `Application.cpp:7016-7027`.
  - **Mechanism**: each `WM_SETCURSOR` at a new pixel creates a new latest request in the Pending state. The
    caller then sets `IDC_CROSS`. When the `JobClass::Background` (LOW priority) job finishes, it posts
    `WM_SETCURSOR`, which sets `IDC_SIZEALL`. So the cursor alternates while moving, and stays a crosshair under
    load.
  - **Memory**: the job also reserves the full selection geometry. A refusal means the arrow never appears.
  - **Before the window**: an inline bounds-prefiltered winding test.
  - **Fix**: return the last Ready answer while Pending (hysteresis). Answer inline below a point-count
    threshold. Charge only the job wrapper.
- [ ] **MR-08** · Medium · ux · CONFIRMED — **Worker-stage failures are silent unless each call site posts its
  own notice.**
  - **Where**: `FinishHostContinuation` posts nothing. Silent paths include:
    - brush opening, clone readback and layer source (`ApplicationCanvasBrushInput.cpp:281-308`);
    - sampler admission (`ApplicationSamplerInput.cpp:261-265`);
    - Hue press (`ApplicationContinuations.cpp:1563-1569`);
    - Copy Merged start;
    - layer-metadata stale outcomes.
  - **Contrast**: Export, Wand, Shape and the sampler job do post notices.
  - **Fix**: a central failure-notice policy in `FinishHostContinuation`, keyed by input kind and HRESULT
    (`ERROR_FILE_TOO_LARGE` → too large, `E_OUTOFMEMORY`/quota → memory, otherwise the kind's error). It covers
    Failed only, never Stale or Cancelled.
- [ ] **MR-09** · Medium · ux · PLAUSIBLE (needs measurement) — **Always-on history residency maintenance
  competes with user input.**
  - **Mechanism**: `ApplicationHistoryResidencyJobs.cpp:310-360` takes the single history barrier as a
    `JobClass::Commit` job. That drives the global busy indicator (`Common/JobActivity.h:31-35`). It is not in
    the Escape cancel chain, and `allowMemoryWaiting` keeps its default of true.
  - **Effect while it runs**:
    - these refuse with MR-01's card: canvas presses (`Application.cpp:4556`), `OnSelectionAction` (`:7152`),
      `OnTransformAction` (`:6562`), `OnGradientAction` (`:8347`) and `OnShapeAction` (`:9227`);
    - deferred brush presses queue behind it, and their 512-sample pointer ring can overflow (MR-13).
  - **Trigger**: any change of the global `Raster::ResidencyEpoch` (`Common/DocumentHistory.cpp:1847-1851`).
  - **Fix**: measure frequency and duration first. Then run it as Background without the user-visible barrier,
    cancel it on user input, or make it refuse without waiting (Q9).
- [ ] **MR-10** · Medium · data-loss risk · PLAUSIBLE — **The unsaved-changes prompt can be skipped after a
  save or export that blocked logoff.**
  - **Where**: `RedPrism/ApplicationFiles.cpp:828-838` returns FALSE for a running writer before checking
    `HasUnsavedWork()`, and never sets `_endSessionPending`. `ClearEndSessionWriteBlock` (`:802-813`) restores
    the unsaved reason only when `_endSessionPending` is set.
  - **Impact**: dirty document + PNG export (or a failed save) during logoff. After the writer ends, no block
    reason and no prompt remain. If Windows then proceeds, edits are lost. Whether Windows re-queries is
    unproven.
  - **Fix**: record "unsaved check owed" in the write-blocked branch, and run the unsaved tail when the writer
    ends. Add a test for Export + dirty + logoff.
- [ ] **MR-11** · Medium · resiliency · CONFIRMED — **All recorded readbacks refuse on drivers reporting
  `DriverConcurrentCreates == FALSE`.**
  - **Where**: `RedPrism/RecordedUtilityGraph.cpp:39-42` and `RedPrism/GpuRecordingCommands.cpp:52-57` return
    `DXGI_ERROR_UNSUPPORTED`. That is not retryable (`RecordedDocumentReadbackJob.cpp:17-21`), and there is no
    fallback.
  - **Impact**: Export, Copy Merged, Merge, eyedropper, Hue, Wand composite and Clone fail on such adapters
    (some VM, remote and old drivers).
  - **D3D11 note**: FALSE means the runtime serializes creation with a coarse lock. Concurrent creation is still
    legal, so the refusal is stricter than D3D11 requires.
  - **Fix**: accept FALSE, or fall back to owner composition with a clear notice (Q8).
- [ ] **MR-12** · Medium · ux · CONFIRMED (pre-existing) — **Copy/Cut reports success when the OS clipboard
  was never written.**
  - **Where**: `RedPrism/ApplicationClipboardJobs.cpp:455-481` returns `Succeeded` once the private copy
    finished, even after six busy retries or an emptied/partial publish.
  - **Impact**: pasting into another app gives stale or empty data with no message.
  - **Fix**: post a "copied inside RedPrism; the system clipboard was unavailable" notice.
- [ ] **MR-13** · Medium · resiliency/ux · PLAUSIBLE — **Input is not coalesced where queues are bounded.**
  - **Pointer ring**: `PointerInputStream<…, 512>` (`Application.h:412`) seals a stroke as `Lost` when the
    worker opening or a preceding job is slow (`Common/PointerInputStream.h:71-77`). That is about 0.5 s at
    1 kHz, or 4 s at 125 Hz.
  - **Nudge autorepeat**: each arrow-key autorepeat is one `HostSelectionNudgeInput` (`Application.cpp:5070`)
    in an 8-entry queue (`Common/ContinuationQueue.h:46`). `EnqueueAdjacent` coalescing is used only for
    opacity. Extra keys are dropped with MR-01's card.
  - **Fix**: coalesce interior Moves when the ring is half full. Sum adjacent pending nudges.
- [ ] **MR-14** · Medium · ux · CONFIRMED (pre-existing) — **A color-preview failure opens a native
  `MessageBoxW` from the UI-action loop, with no deduplication.**
  - **Where**: `RedPrism/ApplicationAdjustJobs.cpp:467-499`, `ApplicationFiles.cpp:118-126`. Up to 8 queued
    boxes can appear while a slider drags; completions and frames stall while one is open.
  - **Fix**: post `IDS_ERROR_FILTER` as a notice, once per panel opening.
- [ ] **MR-15** · Medium · resiliency · CONFIRMED (design) — **317 new `std::terminate()` sites in product
  code, and no document recovery.**
  - **Where**: 319 in product files in total. They sit in active paths (`GpuCommandPump`,
    `RecordedDocumentReadbackJob`, the cursor `Acknowledge`, `HistoryResidencySource::CloseAfterWorkers`) as well
    as dormant ones.
  - **Impact**: any invariant slip is a crash that loses unsaved work. `Core_CrashHandling.md` provides only a
    minidump and a prompt, and the dump does not name the invariant.
  - **Fix**: AR-8. Name invariants, demote those reachable from cancellation or admission to job failure, and
    decide on document recovery (Q11).
- [ ] **MR-16** · Medium · resiliency · PLAUSIBLE — **A GPU TDR could wedge all readbacks until restart.**
  - **Where**: `RedPrism/GpuWork.cpp:722-743` (`RetireDevice`) only flags slots. Waiting slots wait for their
    original event by design (`:823-845`). If a removed device never signals, `inFlight` stays at
    `kMaxInFlight == 2`.
  - **Fix**: a forced-TDR hardware test first (`dxcap -forcetdr`). If it wedges, check
    `GetDeviceRemovedReason` for Waiting slots in `RetireDevice`/`Pump`.

### Low

- [ ] **MR-17** · Low · ux · CONFIRMED — **Wrong or generic messages.**
  - A Clone press with no source now shows "busy" (`Application.cpp:4573-4577`; silent before).
  - Paste failures all show "pixel edit" (`ApplicationClipboardJobs.cpp:1031-1053`).
  - Save failure drops its cause: access denied, disk full, path too long
    (`ApplicationProjectSaveJobs.cpp:244-252`).
  - A metadata admission failure says "pixel edit" (`Application.cpp:5232, 5366, 5435`).
  - More than 256 command-line paths gives "Unknown argument" (`RedPrism/CommandLine.h:331-334`).
  - A residency subscription failure exits 5 with the graphics message (`Application.cpp:308-311`).
- [ ] **MR-18** · Low · bug · CONFIRMED — `Common/PathClassificationRead.cpp:86, 106-109` pass the raw path to
  `GetFileAttributesW`/`CreateFileW` without `Win32FilePath::Prepare`. A long-path project drop is then
  misclassified as an image when `LongPathsEnabled=0`.
- [ ] **MR-19** · Low · resiliency · PLAUSIBLE — a crash or kill mid-save leaves `<dest>.~<guid>` staging (which
  can be GBs) beside the project, and nothing sweeps it (`Common/ProjectStore.cpp:274-278`).
- [ ] **MR-20** · Low · resiliency · PLAUSIBLE — the BoundedClipper arena overhead is about 180 B per 48-byte
  `OutPt` (`Common/BoundedClipper/Workspace.h:123-143`). The advertised 16M/8M point limits are therefore
  unreachable inside 128 MiB. Measure the real ceiling and set `Limits` from it.
- [ ] **MR-21** · Low · provenance · CONFIRMED — `Common/BoundedClipper/offset-generation-manifest.json` hashes
  CRLF bytes while the committed blobs are LF. It says `"runtime_qualified": false` although Expand/Contract
  ship on it, and nothing re-verifies either manifest.
- [ ] **MR-22** · Low · performance — two owner-side costs:
  - `GpuCommandStream::IsStopped` does `WaitForSingleObject(…, 0)` per owner pass (`GpuCommandStream.cpp:60-63`).
  - Per-tile command lists each carry their own fence and flush (`GpuRecordingCommands.cpp:152-289`), unmeasured.
- [ ] **MR-23** · Low · robustness — `OwnerWorkMultiplexer::kCapacity == 11` equals the maximum source count
  (`Common/OwnerWorkMultiplexer.h:14`). A 12th source makes startup fail. Add a `static_assert` or headroom.

## Findings — latent (dormant today; block P1 activation)

Record these in the owning P1 plans when P1 resumes.

- **F1-2**: the prepared overlays drop the last good frame. The lasso draft and ants vanish while dragging,
  panning or zooming (`LassoDraftDisplayController.cpp:245-252, 447-449`).
- **F1-3**: press, polygonal and freehand failures post no notice.
- **F1-4**: the cold lasso opening reserves an 8M-point arena, about 140 MB, per press
  (`ApplicationLassoOpeningJobs.cpp:220`).
- **F1-5**: the ants raster covers the whole hole × 9 planes per cell (`ApplicationSelectionOutlineDisplay.cpp:44-57`).
- **F1-6**: the polygonal journal (512 entries) does not coalesce hover moves.
- **F1-7**: freehand is capped at 65,536 vertices and the whole gesture fails at the cap.
- **E-5**: a failed async finish discards the completed stroke silently.
- **E-7**: per-sample admission plus a DXGI video-memory query on the owner.
- **G-3**: prepared layer rows disable the Tree on every generation bump.
- **C-3**: the committed route draws its cached graph without `GraphForFrameOwner()` validation.

## Architecture and simplification

- [ ] **AR-1** · Split the harness out of the product executable.
  - **Size**: `RedPrism.vcxproj` compiles 140 `*.Tests.*`/benchmark TUs (about 65k lines; 26 at base) beside
    219 product TUs (124.5k).
  - **Header**: `Application.h` is 1,880 lines (+725) with 433 members, 34 `TestGate` members and 174 self-test
    declarations.
  - **Product TUs carrying test code**:
    - they include `../Tests/Support/Support.Tests.DocumentFixture.h` (`ApplicationProjectOpenJobs.cpp`,
      `ApplicationImageImportJobs.cpp`, `ApplicationHistoryRestoreJobs.cpp`, `ApplicationFiles.cpp`,
      `ApplicationContinuations.cpp` and others);
    - they host self-test bodies (for example `ApplicationProjectOpenJobs.cpp:385-1008`);
    - they contain harness `std::_Exit(5)` calls.
  - **Proposal**: build `RedPrismCore.lib` + `RedPrism.exe` + `RedPrism.Harness.exe`. Use one
    `ApplicationTestAccess` friend and one injected `ApplicationTestHooks` instead of 34 gates. This touches
    `test.ps1`, `package.ps1`, CI and the test-harness skill (Q3).
- [ ] **AR-2** · Dormant P1 code in the shipping image, about 12–14k lines:
  - `ApplicationStrokeFinishJobs.cpp` (2,128) and Warp/StrokeSample/StrokeFinish work (about 2.9k);
  - committed/pending display and final publication (about 4.7k);
  - prepared layer rows (about 1.2k);
  - selection outline/lasso draft/polygonal routes;
  - the GPU upload stack (about 2.6k, zero production callers).
  Keep it on `main` as is, compile it only into the harness until activation, or move it to a branch (Q2).
- [ ] **AR-3** · One host-history job skeleton (about 4–5k lines).
  - About 35 `Application*Jobs.cpp` files repeat the same pieces: a payload + `Retire*` deleter, `Current()`,
    `DeferCompletion` capture/`Resume`, the `Completed` epilogue (outcome map, ticket/stamp reset,
    `Finish`, `ResumeHostHistory`), and the `Start*` prologue (`NextOrdinal`, `Begin`/`Continue`, checked byte
    sums, `Submit`, rollback).
  - Duplicates: `PassGate` is defined in 15 files, there are five identical error-mapping blocks
    (`ApplicationContinuations.cpp:1163-1301`), and six `*ByteSum` helpers.
  - Proposal: a `HostHistoryJob<Traits>` base plus `StartHostHistoryJob`. `HistoryWork` would own its barrier
    ticket, which deletes about 30 ticket/stamp members, the 156-line cancel chain
    (`ApplicationContinuations.cpp:1792-1947`) and the 120-line shutdown block (`Application.cpp:4020-4143`).
  - It also fixes MR-08/MR-13-class behavior once.
- [ ] **AR-4** · One admission model (root cause of MR-02 to MR-07).
  - Today's envelopes are worst case by construction: fixed engine limits, whole-document retirement, 2–4×
    multipliers. They are evaluated against `min(availPhys, availCommit) − 1 GiB` without waiting.
  - Proposal:
    - size from the actual inputs (input point/edge count, filled region, requested readback region);
    - keep engine limits as refusal limits, not reservations;
    - do not charge resident immutable inputs (as `Jobs.h:54` already states);
    - count retirement once;
    - wait (with trim) instead of refusing for user-initiated commands.
  - Spec change in `Core_PerformanceAndResources.md` (Q1).
- [ ] **AR-5** · Selection simplification.
  - Merge the two prepared overlay stacks (`SelectionOutlineDisplayController` ≈ `LassoDraftDisplayController`,
    `PreparedSelectionOutlineGpu` ≈ `PreparedLassoDraftGpu`; about 650 lines).
  - Retire the superseded lasso routes (a) `HostLassoInput` + `ApplicationLasso*Jobs` and (b)
    `PolygonalDrawingJob`, about 2.1k product + 2.9k test lines (Q7).
  - Use one input journal (about 300).
  - Merge `InverseSelectionWork` and `SelectionOffsetWork` (about 200).
  - Collapse the Command/Nudge/Inverse/Offset host jobs into AR-3 (about 1.0–1.2k).
- [ ] **AR-6** · Publication and retention API.
  - Collapse the publication-object family (PublicationObject, AccountedPublicationObject,
    LocallyOwnedPublication, PublicationGraphAllocator, PublicationAllocationLedger) into one factory, about
    120 lines.
  - Replace the 10 `SharedPublicationMemory::Bind*` methods and 9 `JobScheduler::Retain*` entry points with one
    mode/shape API (about 250 lines).
  - Replace the four parallel callback-authority fields in `Jobs.cpp:132-136` with a bitmask.
- [ ] **AR-7** · Delete dead code:
  - `ClassifyPathBatch` + scratch seam (`Common/PathClassificationWork.cpp`, about 170);
  - `DocumentReadbackJob.*` (456; only a fixture uses it);
  - the synchronous cursor branch and its members (`Application.cpp:7099-7131`);
  - `LayerTreeModel::badgeText/badgeTone`;
  - test-only `ReplaceDocument`, `WriteFileBytesToDisk`, `PreviewLayerOpacity`, and the session
    `RenameLayer`/`ToggleVisibility` paths;
  - `SelectionMappingForTest()` in production paint.
- [ ] **AR-8** · Fatal invariants (MR-15).
  - Replace bare `std::terminate()` with `[[noreturn]] FatalInvariant(id)`, which records the id in the crash
    marker or dump.
  - Demote cancellation/admission-reachable sites to job failure.
  - Add Main-level document recovery if fail-fast stays the policy (Q11).
- [ ] **AR-9** · ISA gate.
  - `Build/IsaScan.cs` is 5,518 lines, with 41 `Isa*.ps1` tests (about 13.5k lines) and 56 commits. It adds
    about 73 s to every x64 build.
  - It guards against VEX/EVEX outside AVX tier objects, but no AVX tier exists: only
    `Kernels_Scalar_Adjust`, and no `/arch:AVX` in any project.
  - Replace it with a compile-flag/TU-allowlist gate until a tier lands, and keep the binary scan as an opt-in
    qualification step (Q6).
- [ ] **AR-10** · Tooling consolidation.
  - `test.ps1` defaults to Tooling, so the AGENTS.md matrix runs `validate.ps1` three times and
    `validate-skills` four times.
  - `Tests/native-test-files.json` (901 lines) duplicates the naming check.
  - There are five copies of the reparse-point ancestor walk.
  - `Tools/**` and `*.md` map to full native scope.
  - `Build/RedMonitor.props` references a nonexistent `HostPluginTests`.
  - `validate.ps1:21-22` is a one-off check for a Done plan.
- [ ] **AR-11** · Utility readback pipeline (MR-05).
  - Region-bounded upload when recording a utility tile.
  - Cache shaders, states and samplers per device generation instead of per job
    (`RecordedUtilityGraph.cpp:49-61`, `GpuRecordingCommands.cpp:36-89`).
  - Fold the four copies of the fence `Signal`/`SetEventOnCompletion` sequence (`GpuWork.cpp:256, 332, 671`;
    `GpuUploadWork.cpp:232`).

## CI and tooling

- [ ] **CI-0** · process · CONFIRMED — **No hosted check ran on PRs #13–#18.**
  - Every job was refused with "recent account payments have failed or your spending limit needs to be
    increased", so `native-scope` and `tooling` show FAILURE and `native` SKIPPED.
  - The user authorized local validation for these merges (recorded in `RedMonitor_Adoption_2026-10-23.md`).
  - Until CI runs, AGENTS.md's `CI_PENDING` rule is not enforceable (Q4).
- [ ] **CI-1** · High (CI reliability) · CONFIRMED — `Tools/Get-CiNativeScope.ps1` fails its own fall-back.
  - Its degrade paths (`:34`, `:44`, `:56`) leave `$LASTEXITCODE` at 1 with no final reset, and GitHub's `pwsh`
    wrapper exits with it. One cancelled run with no artifact keeps the PR red on every rerun.
  - Fix: `$global:LASTEXITCODE = 0` at the end, plus a Tooling test with a failing fake `gh`.
- [ ] **CI-2** · Medium · CONFIRMED — `test.ps1:53` always runs `Fold-Changelog.ps1 -Check`, so
  `Test-Changes.ps1` Affected fails whenever a fragment is pending. That contradicts `Changes/README.md:7` and
  `Core_Changes.md:14,22`. Fix: strict check only for PrePush/Full.
- [ ] **CI-3** · Low · CONFIRMED — a fragment-only commit yields `NATIVE_UNCHANGED`, so an unfolded fragment
  can reach `main`. Add `Fold-Changelog.ps1 -Check` to the `tooling` job.
- [ ] **CI-4** · Low · CONFIRMED — native-gated `Tests/BuildProcessTests` scripts (`FailureReportTests`,
  `Ui*Benchmark*`, `LayerRowsBenchmarkSmokeTests`, `IsaScanTests`, `CodeAnalysisTests`) map to Tooling only
  (`Tests/test-scopes.json:120-126`).
- [ ] **CI-5** · Low · CONFIRMED — `Build/IsaScan.cs:5479-5480` accepts `<unknown>` at C4/C5/62 lead bytes
  without proof. The current Release report shows two such bytes. Pin the decoder, or require a proven range.
- [ ] **CI-6** · Low · CONFIRMED — `.github/workflows/native-arm64-feasibility.yml` is pinned to
  `codex/p1-opacity-host-proof`. Its helper (`.github/scripts/Run-NativeArm64.ps1:44`) installs to
  `C:\P1NativeTools1452`, a drive-root output that AGENTS.md forbids. Delete the trio (about 930 lines).
- [ ] **CI-7** · Low — four smaller items:
  - CI uploads crash-probe `.dmp` files on passing runs (`ci.yml:106-114`);
  - native-proof reuse is keyed by branch name only (`Get-CiNativeScope.ps1:27-37`);
  - a `Complete-TestRunEvidence` failure in `finally` masks the real error (`test.ps1:438`);
  - the ISA scan runs on every build with no hash skip (`build.ps1:155-164`).

## Questions

- **Q1 — Admission policy.** Should user-initiated commands be admitted from input-sized estimates, with engine
  limits as refusal limits, resident inputs uncharged and memory waiting allowed (AR-4)? Or is the conservative
  worst-case envelope intended, with refusal as the accepted outcome? This decides MR-02 to MR-07. If AR-4 is
  too large for the first release, may Inverse/Offset/Wand Add-Subtract temporarily return to the
  pre-window synchronous route?
- **Q2 — Dormant P1 code.** Keep the ~12–14k dormant lines in `RedPrism.exe`, compile them into the harness only
  until activation, or move them to a branch? P1 is HOLD; you said you would restart it from this `main`.
- **Q3 — Harness split (AR-1).** May the build produce a separate harness executable? This changes `test.ps1`,
  `package.ps1`, CI and the test-harness skill.
- **Q4 — CI.** Will hosted CI billing be restored before production? Or should AGENTS.md formally adopt "local
  Full matrix authorizes merge", with the required local evidence named?
- **Q5 — Release gates.** Do native ARM64, interactive/manual (IME, accessibility, input) and paired-performance
  gates have to close before "production"? Is a release candidate x64-only acceptable?
- **Q6 — ISA gate.** May the binary scanner become an opt-in qualification step, replaced in ordinary builds by
  a compile-flag gate, until a vector tier exists (AR-9)?
- **Q7 — Lasso routes.** Are the journal routes (c)/(d) (`HostSelectionPressInput`/`HostPolygonalInputJournal` +
  `SelectionOutlineGestureJob`) the chosen design, so that routes (a) and (b) can be deleted (AR-5)?
- **Q8 — Concurrent-creates drivers (MR-11).** Accept `DriverConcurrentCreates == FALSE` (the runtime
  serializes), or fall back to owner-thread composition, which blocks the UI on those machines?
- **Q9 — History maintenance (MR-09).** Is it acceptable for residency maintenance to run without the
  user-visible history barrier and busy indicator, and to be cancelled by user input? What latency budget
  applies?
- **Q10 — Busy text (MR-01).** A plain "RedPrism is busy — try again in a moment", or the running operation's
  caption ("Saving…")? Should a refused input show a modal card at all, or a transient status message?
- **Q11 — Crash recovery (MR-15).** With fail-fast invariants as policy, do you want a document recovery
  snapshot (autosave), or only named invariants plus demotion of reachable ones?
- **Q12 — Delivery.** One fix branch per work package below, each with a PR? This depends on Q4. May
  independent packages run as parallel sessions?

## Proposed execution order

Each package lands with its spec text, its test (a legacy numeric golden where the oracle covers the behavior),
one `Changes/` fragment, folding, and the AGENTS.md validation matrix.

1. **WP0 — decisions and measurements (no code).**
   - Answer Q1–Q5.
   - Measure, on 8 GB and 16 GB machines:
     - the actual Inverse/Offset/Wand reservations (the existing test gates record `reservationBytes`);
     - residency maintenance frequency and duration;
     - first-eyedropper latency on a 45 MP document;
     - a forced TDR during Export.
2. **WP1 — quick wins (small, independent).** MR-01, MR-12, MR-17, MR-18, MR-21, MR-23, CI-1, CI-2, CI-3, CI-6,
   CI-7.
3. **WP2 — admission model.** AR-4, fixing MR-02 to MR-07 and MR-06's retained charge, with spec updates in
   `Core_PerformanceAndResources.md`.
4. **WP3 — feedback and input.** MR-08 (central failure notice), MR-07, MR-13, MR-14, MR-10, MR-09.
5. **WP4 — resiliency.** MR-11, MR-16 (test first), MR-19, MR-20, AR-8 (named invariants, demotion; recovery
   per Q11).
6. **WP5 — structure.** AR-1, AR-2, AR-3, AR-5, AR-6, AR-7, AR-11, then AR-9 and AR-10 per Q6.
7. **WP6 — release qualification.** Hosted CI green (Q4), the Full matrix on the integrated result, and the gates
   from Q5.

## Checked, no issue

- **Scheduler** (`Common/Jobs.*`):
  - slot generation equals the unique sequence;
  - FIFO `Blocked()` counts suspended predecessors;
  - hooks never promote a resumed worker inside the hook;
  - the `Retain*` refund paths cannot double-refund;
  - one SRW lock, with no callback under it;
  - the wake counter is published before its signal;
  - shutdown order is RequestStop → stop notifications → Close → drop callbacks.
- **GPU**:
  - the device is created without `D3D11_CREATE_DEVICE_SINGLETHREADED`;
  - the immediate context is used only by the owner pumps;
  - deferred contexts are thread-checked;
  - generation and device checks stop stale command lists;
  - `Map` uses `DO_NOT_WAIT`;
  - the settled frame still costs zero passes;
  - the HLSL edits do not change blend math (`Coordinate()` is equivalent to the old `ddx/ddy`).
- **Files**:
  - ZIP save is temp + flush + `ReplaceFileW`; exploded save rotates and rolls back;
  - `MarkSaved` applies only when the revision is current;
  - a failed save aborts its Close/New/Open follow-up;
  - the end-session wait is bounded at 1.5 s, then cancels;
  - the `.prism` schema is unchanged.
- **History**:
  - trim order protects the current entry;
  - storage indexes are keyed by `StorageSerial()` and hold owners (no address ABA);
  - the residency permit is reset before callbacks.
- **Selection and other jobs**:
  - every selection job re-validates authority and token at apply, and the document swaps only after publish;
  - BoundedClipper replaces every `throw` with `Workspace::Refuse()` (`bad_alloc`), lives in its own namespace
    and include guards, and has no ODR clash with vcpkg Clipper2;
  - Opacity Preview/Commit ordering is final-value-exact with a single history entry;
  - commands while busy queue in FIFO order.
- **CI**:
  - only `pull_request`/`workflow_dispatch` triggers;
  - `contents: read`, and actions pinned by SHA;
  - no untrusted `${{ }}` in `run:`;
  - the planner falls back to Full for unknown files;
  - `build.ps1` rejects, never kills, a running `RedPrism.exe`.
- **RedMonitor**: the client is ETW TraceLogging only (no pipe or connect); a missing viewer is harmless; the
  lock hashes are verified before the build.

## Coverage and limits

- **Fully covered**: all 365 changed production files in `RedPrism/` and `Common/` were assigned to exactly one
  area, and every changed build/CI/tooling file to area H.
- **Not reviewed in depth**:
  - generated `Common/BoundedClipper/clipper.*` (the generator transformations were reviewed instead);
  - self-test bodies and `Tests/**` beyond coverage questions;
  - dormant files flagged as such in each area (for example `GpuUploadWork.cpp` 420–933 and
    `PolygonalDrawingJob.cpp`).
- **Not executed**: no build, test, profiler or hardware repro was run. Every magnitude is source arithmetic, and
  WP0 must measure the High rows before WP2 sizes its fix.
- **Retained notes**: the per-area reports and the verification ledger are kept outside the repository in the
  review session's scratchpad. This file is their verified summary.
