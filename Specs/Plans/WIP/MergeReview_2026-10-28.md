# Merge review (merges of 21–30 October 2026)

> Correction (2026-11-01, `main` `e79a83a`). Line anchors below were read at `b6ec336`.
> - **CI-8:** `package.ps1` calls `Get-DxUiSourcePath` since `18fd8ba`. That is not a ZIP blocker. `VcpkgToolsetTests.ps1:165` still joins the 40-hex commit and skips if the folder is missing. The `package.ps1` comment still says `source\<commit>`.
> - **CI-10 / Q16:** verified shared engine `19710a8aeac979a82e83bec82e8b866cee3d3deb`. PR static checks validate the exact head against the supplied immutable base; pushes to `main` invoke the quiet-window coordinator. `Core_Changes.md` now describes that routing. No second push trigger or `github.event.before` changelog comparison is needed.
> - **Dropped files:** more than 256 paths still refuse. The cap is `HostInputBudget::kMaxPaths` in `ContinuationRequests.h`. The drop handler uses `ERROR_NOT_ENOUGH_QUOTA` and `ERROR_INVALID_DATA`, not `E_INVALIDARG` at `Application.cpp:6139`.
> - **CI-3's `ci.yml:44-49` and CI-7's `ci.yml:106-120`** no longer describe this workflow. CI-6's pin to `codex/p1-opacity-host-proof` and `C:\P1NativeTools1452` still match.
>
> Spawned on 2026-10-28 from the request "review all the code merge in the last 10 days for any bugs problem or
> architecture misfit … review the code around the modification for side effect … study if you could find some
> simplification in code or architecture … all this code must be ready for production from resiliency, quality and
> user experience … if you have some question write them in the plan". The same request on 2026-10-30 asked for
> this plan to be reviewed and updated; that second pass is folded in below.

- **Status**: DECISION. Findings are verified at `b6ec336`, with the corrections above; questions Q1–Q15 are open; Q16 is resolved; no product fix has started.
- **Reviewed**:
  - First pass (2026-10-28): `0ce2070..85eaef7` on `main`, 15 merges (PRs #4–#18). That is 1,716 files,
    +296,939/−11,027 lines: about 66k production lines in `RedPrism/` and `Common/` (7k of them the generated
    `Common/BoundedClipper` engine), 126k test lines, 44k spec/evidence lines and 7.5k tooling lines. PR #13
    (paused P1 consolidation) is 285k of it.
  - Second pass (2026-10-30): `85eaef7..b6ec336`, PRs #19, #20 and #22 (#21 is this plan).
    - PR #19 (shared native build policy) is 655 files, +6,488/−1,035. That breaks down into 322 product files
      (+3,098/−411), about 305 test files, and the build tooling.
    - PR #20 (fold changelogs on `main`) is 16 files, +732/−101.
    - PR #22 is the first fold, one entry.
- **Method**:
  - **First pass**: nine read-only area reviews (scheduler/lifecycle, GPU transport, display
    publication/readback, document/history/IO, brush/stroke, selection gestures, selection workers +
    BoundedClipper, Application/chrome, build/CI). The lead reviewer re-read the source for every High and Medium
    claim, and ran the cross-cutting checks.
  - **Second pass**: four read-only helper reviews:
    - PR #19's `RedPrism/` product hunks;
    - PR #19's test hunks;
    - build and changelog tooling;
    - re-anchoring every row of this file.

    The lead reviewed PR #19's `Common/` hunks directly. The lead also re-read the source for every retained
    helper claim, and rejected four claims (see "Rejected in the second pass").
  - **What ran**: read-only git, script parsers, `Test-Changelog.ps1`, `Test-Changes.ps1 -Explain` and an
    MSBuild evaluation with no target. **Nothing was built or tested for either pass.** Memory magnitudes are
    arithmetic from source constants, not measurements.
- **Owning specs**: `Core/Core_Architecture.md`, `Core/Core_PerformanceAndResources.md`,
  `Core/Core_CrashHandling.md`, `Core/Core_Changes.md`, `Document/Document_Tools.md`, `Document/Document_History.md`,
  `Rendering/Rendering_Composition.md`, `Rendering/Rendering_Brush.md`, `UI/UI_ApplicationChrome.md`,
  `UI/UI_Windowing.md`, `IO/IO_ProjectFormat.md`, `Build/Build_Toolchain.md`, `Build/Build_ConsoleAndPaths.md`,
  `Build/Build_Packaging.md`, `Testing/Testing_Validation.md`.
- **Coordinates with**:
  - [`UiNeverBlocks_2026-09-22.md`](UiNeverBlocks_2026-09-22.md) (HOLD). Rows marked *latent* only block P1
    activation.
  - [`NativeBuildPolicyCoherence_2026-10-28.md`](NativeBuildPolicyCoherence_2026-10-28.md) (ACTIVE) owns PR #19's
    remaining gates. CI-8, CI-9, MR-24 and AR-12 belong to its follow-up.
  - [`AdversarialReview.md`](AdversarialReview.md) is the older static review at `97dd25b`. This file does not
    re-triage it.
- **Line anchors**: `path:line` references are to `b6ec336`. The second pass re-anchored every row. PR #19 moved
  lines but changed no reviewed behavior.

This file is a non-normative execution record. Requirements move into the owning domain spec in the slice that
lands them.

## Verdict

The core machinery is careful. No reviewer could construct a Critical defect, meaning ordinary-use data
corruption, a crash, or a hang. Several mechanisms traced clean:

- the `JobScheduler` (ticket/generation ABA, FIFO under suspension, refunds, lock discipline, shutdown order);
- the GPU transport (no immediate context on workers, deferred-context ownership, stale command lists);
- project save durability (temp + `ReplaceFileW`, dirty flag by revision);
- history ownership.

PR #19 holds its own claim: it changed no product behavior.

- In `RedPrism/`, 361 of 364 product hunks are provably identical. The other 3 are self-test-only diagnostics.
- The `Common/` hunks are behavior-neutral:
  - casts and `case` labels;
  - warning-pragma scopes;
  - brace-to-parenthesis constructions with no order-dependent arguments;
  - an ARM64-only tail member, guarded by a `static_assert`.

PR #19 did add two maintenance costs: 3,872 of its 5,558 added C++ lines are suppression boilerplate (AR-12), and
it hand-edits generated sources (MR-24).

`main` is **not production-ready** yet, for five reasons:

1. **Ordinary commands are refused by memory admission on typical machines.**
   - Select › Inverse and Expand/Contract reserve about 2.9 GB for any selection, even a rectangle.
   - On any canvas of 2 MP or more, Magic Wand reserves about 4.8 GB in New mode. It reserves 7–9 GB for
     Shift/Alt-click, and so do Load Mask Selection and Load Layer Selection.
   - Admission needs that much plus 1 GiB of available memory, so the command fails.
   - This already breaks a test on the qualification host. The unexplained Release failure of the checkerboard
     fixture in PR #19 matches it exactly (MR-25).
   - Brush presses, eyedropper, Hue and readbacks charge whole-document bytes. The scheduler's own contract
     (`Common/Jobs.h:54`) says resident inputs are not charged again.
2. **User feedback is broken.**
   - Every "busy" refusal opens a modal notice card that literally reads `{0}…`. This window raised those call
     sites from 9 to 19.
   - Many worker failures say nothing at all.
   - The selection cursor flickers while hovering a selection.
3. **The assurance gap is open.**
   - Hosted CI ran on none of PRs #13–#22: GitHub refused the jobs for billing.
   - Local Full runs exist for:
     - the integrated P1 tree (x64 Release);
     - the RedMonitor tree (PR #15: x64 Debug, Release and ASan);
     - PR #19 (six native builds; Debug, Release and ASan Full).
   - Native ARM64, interactive/manual and paired-performance gates remain open.
   - The zero-warning gate can now be passed by running the build twice (CI-9).
4. **The shipping binary carries non-product code.**
   - It holds about 65k lines of test code: 140 test/benchmark TUs, where the base had 4.
   - It holds about 12–14k lines of dormant P1 code.
   - 317 new `std::terminate()` sites were added to product code. There is no document recovery, so any
     invariant slip loses unsaved work.
5. **The DxUi toolset parity test can skip.** `package.ps1` already uses `Get-DxUiSourcePath` since `18fd8ba`;
   the remaining CI-8 work is the test's stale 40-hex path and the packaging comment. This is not proof that a ZIP
   was built or that release qualification passed.

## Production activation map

A user run (no `--self-test`, `--bench`, `--demo` or `--screenshot`) enables these, from
`Application::Run`/`InitializeProcessing`:

- `HistoryResidencySource` (`Application.cpp:308`);
- the async selection cursor (`:319`);
- recorded Hue readback (`:322`);
- `UtilityReadbackOwnerService` (`:229`), with `RecordedDocumentReadbackJob` serving the eyedropper, Hue,
  Copy Merged, Merge, Export, Wand composite, Clone (all layers) and adjustment opening;
- the worker selection commands (Inverse, Expand/Contract, nudge, Wand, Load Mask/Layer Selection, masks);
- the worker brush-press opening (`CanDeferCanvasBrush`).

`InitializeResponsiveSourcesOwner` (`Application.cpp:313-316`) runs only for a test probe or `--bench`. The
following are therefore **dormant** in production:

- async stroke finish/samples and warp live;
- prepared layer rows;
- the committed/pending display;
- selection press/outline/lasso-draft display;
- polygonal press/drawing;
- selection pixel final publication;
- the GPU upload stack;
- color display jobs.

A user run registers 6 of the 11 owner-work sources: GPU work, retry wait, path read wait, utility readback,
command pump (`:232`) and history residency.

Severity below rates what a user can reach.

## Findings — production reachable

Each row: severity · category · verification (CONFIRMED = mechanism read end to end at source; PLAUSIBLE =
mechanism read, trigger or magnitude unproven).

### High

- [ ] **MR-01** · High · ux · CONFIRMED — **Every busy notice reads `{0}…`.**
  - **Where**: `RedPrism/RedPrism.rc:548` (`IDS_STATUS_BUSY "{0}…"`).
    `RedPrism/ApplicationFiles.cpp:243-248`: `FormatPostedNotice` formats only `IDS_ERROR_UNSUPPORTED_VERSION`.
    `RedPrism/ChromeHost.cpp:1718-1730` shows the raw text in a modal notice card.
  - **Reached from**: 19 `PostNotice(IDS_STATUS_BUSY)` sites (9 at base). `ReportHostAdmission`
    (`ApplicationContinuations.cpp:450-458`) maps every admission failure to it: full queue, quota,
    `E_INVALIDARG`, more than 256 dropped files (`Application.cpp:6139-6143`).
  - **Fix**: use a plain string, or format with the running operation's caption (see Q10). Split
    `ReportHostAdmission` by cause. Add a test that asserts the rendered text, not just the id.
- [ ] **MR-02** · High · resiliency/ux · CONFIRMED — **Inverse, Expand and Contract reserve the worst-case
  engine envelope for any selection.**
  - **Where**: `Common/InverseSelectionWork.cpp:86-111` and `Common/SelectionOffsetWork.cpp:87-111` charge
    `Limits::outputPoints` 8M × 16 B, `outputLoops` 2.67M × (24 B + 256 B ownership) and a 128 MiB workspace,
    about 1.0 GB. That is counted in both preparation and retirement.
    `RedPrism/ApplicationInverseSelectionJobs.cpp:370` (and `ApplicationSelectionOffsetJobs.cpp:374`) adds
    `PreparedSelectionOutline::OutputMemory(2.67M, 8M)`, about 0.8 GB.
  - **Admission**: the request must fit in `min(availPhys, availCommit) − 1 GiB − outstanding`
    (`Common/MemoryAdmission.h:22-46`). With nothing outstanding it is refused at once.
  - **Impact**: Ctrl+Shift+I on a rectangle shows "The selection edit could not be completed" when less than
    about 4 GB is free. While it runs, it starves every other admission.
  - **Fix**: AR-4 (size from the actual input, with hard caps as refusal limits, not reservations).
- [ ] **MR-03** · High · resiliency/ux · CONFIRMED — **Magic Wand and Load Mask/Layer Selection reserve
  4.8–9 GB on ordinary canvases.**
  - **Where**: `Common/WandLayerSelectionWork.cpp:597` sets
    `geometryGrowthBytes = 3·edges·16 + 3·loops·(sizeof loop + 128)`, with `edges = min(4·pixels, 8M)`. That is
    about 1.6 GB for any canvas of 2 MP or more.
  - **Boolean path**: `:612-651` adds 2 × the boolean output (about 1.75 GB) plus the workspace, in both the
    phase and retirement. `RedPrism/ApplicationWandLayerHostJobs.cpp:389-396` adds the outline.
  - **Which commands take the boolean path**:
    - Add, and Subtract when a selection exists (`:612-613`);
    - every non-ColorMatch source, because `loaded` is true for it (`:272`, `:521`). So Load Mask Selection
      and Load Layer Selection always take it, even in replace mode.
  - **Routing**: New/Add/Subtract go to the worker whenever `CanApplyHistoryRestore` holds, which is the normal
    state (`Application.cpp:7579-7587`). Otherwise they fall back to the synchronous route. `sampleAllLayers`
    uses the composite job, with its own admission (MR-05). A panel-opening wand has its own branch
    (`:7567-7572`).
  - **Impact**: Shift/Alt-click with the wand, Load Mask Selection and Load Layer Selection fail with the
    "not enough memory" message unless about 8 GB is free. MR-25 is this failure on the test host.
  - **Fix**: AR-4. Interim (Q1): bound the trace by the filled-region edge count after the flood fill or mask
    scan, not by canvas pixels.

### Medium

- [ ] **MR-04** · Medium · resiliency · CONFIRMED — **A brush, eraser, heal, blur or clone press charges the
  whole resident document with no waiting.**
  - **Where**: `Common/StrokeOpeningWork.cpp:195-243` starts from `source.RetirementMemoryBytes()` and adds a
    tile bound and the layer again. `RedPrism/ApplicationStrokeOpeningJobs.cpp:343` submits with
    `allowMemoryWaiting=false`.
  - **Impact**: on large documents with modest free RAM the press does nothing, and silently (MR-08). The
    pre-window synchronous path never refused.
  - **Fix**: AR-4.
- [ ] **MR-05** · Medium · performance/resiliency · CONFIRMED — **Every utility readback (eyedropper, Hue,
  Copy Merged, Merge, Export, Wand composite, Clone all layers) rebuilds full-document GPU residency privately
  and reserves 4× the document.**
  - **Where**:
    - `Common/UtilityReadbackAdmission.cpp:44-56` sets GPU = 4 × dense document bytes + …, and CPU = 2 ×.
    - `RedPrism/RecordedUtilityGraph.cpp:49-61` creates a fresh `CanvasRenderer` per job
      (`RecordedDocumentReadbackJob.cpp:228`).
    - `CanvasRenderer.cpp:1058-1074` (`RasterView` → `TextureFor`) uploads whole rasters plus mip chains, even
      for a 1×1 region.
  - **Impact**: first-sample latency scales with document size (unmeasured). On integrated or 4 GB GPUs, large
    documents get refused (silently for sampler/Hue/Copy Merged/Clone, MR-08).
  - **Fix**: AR-4 plus AR-11 (region-bounded upload for utility recording; reuse pipeline objects per device
    generation).
- [ ] **MR-06** · Medium · resiliency · CONFIRMED — **Merge and Copy Merged keep their whole admission envelope
  charged for the life of the output raster.**
  - **Where**:
    - `RedPrism/ApplicationMergeJobs.cpp:468` builds `held·2 + memory·2`.
    - The default `BindForRetirement` (`RedPrism/RecordedDocumentReadbackJob.cpp:1019-1023`) moves
      `record.reserved` into a retained slot.
    - `Common/Document.cpp:1051` (`AllocateChargedStorage`) ties it to the merged raster.
  - **Impact**: each merge lowers every later admission (it double-counts against OS availability) and pins one
    of 64 retained slots. This compounds MR-02 to MR-05. The behavior is spec-intended:
    `Core_PerformanceAndResources.md:1448` for Merge, `:1456-1463` for Copy Merged.
  - **Fix**: override `BindReadbackMemoryForRetirement` with `BindPersistentOutputForRetirement(outputBytes)`,
    as the Wand composite already does (`ApplicationWandCompositeHostJobs.cpp:124-130`). Update those spec lines.
- [ ] **MR-07** · Medium · ux · CONFIRMED — **The selection cursor flickers or lags over a selection.**
  - **Where**: `RedPrism/ApplicationSelectionCursorJobs.cpp:105-164`, `RedPrism/SelectionCursorRequests.h:44-66`,
    `Application.cpp:7019-7030`.
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
  - **Where**: `FinishHostContinuation` (`ApplicationContinuations.cpp:460-538`) posts nothing. Silent paths
    include:
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
  - **Mechanism**: `ApplicationHistoryResidencyJobs.cpp:321-371` takes the single history barrier as a
    `JobClass::Commit` job. That drives the global busy indicator (`Common/JobActivity.h:31-35`). It is not in
    the Escape cancel chain, and `allowMemoryWaiting` keeps its default of true.
  - **Effect while it runs**:
    - these refuse with MR-01's card: canvas presses (`Application.cpp:4559`), `OnSelectionAction` (`:7155`),
      `OnTransformAction` (`:6565`), `OnGradientAction` (`:8362`) and `OnShapeAction` (`:9258`);
    - deferred brush presses queue behind it, and their 512-sample pointer ring can overflow (MR-13).
  - **Trigger**: any change of the global `Raster::ResidencyEpoch` (`Common/DocumentHistory.cpp:1848-1852`).
  - **Fix**: measure frequency and duration first. Then run it as Background without the user-visible barrier,
    cancel it on user input, or make it refuse without waiting (Q9).
- [ ] **MR-10** · Medium · data-loss risk · PLAUSIBLE — **The unsaved-changes prompt can be skipped after a
  save or export that blocked logoff.**
  - **Where**: `RedPrism/ApplicationFiles.cpp:835-845` returns FALSE for a running writer before checking
    `HasUnsavedWork()`, and never sets `_endSessionPending`. `ClearEndSessionWriteBlock` (`:809-820`) restores
    the unsaved reason only when `_endSessionPending` is set.
  - **Impact**: dirty document + PNG export (or a failed save) during logoff. After the writer ends, no block
    reason and no prompt remain. If Windows then proceeds, edits are lost. Whether Windows re-queries is
    unproven.
  - **Fix**: record "unsaved check owed" in the write-blocked branch, and run the unsaved tail when the writer
    ends. Add a test for Export + dirty + logoff.
- [ ] **MR-11** · Medium · resiliency · CONFIRMED — **All recorded readbacks refuse on drivers reporting
  `DriverConcurrentCreates == FALSE`.**
  - **Where**: `RedPrism/RecordedUtilityGraph.cpp:39-42` and `RedPrism/GpuRecordingCommands.cpp:172-177` return
    `DXGI_ERROR_UNSUPPORTED`. That is not retryable (`RecordedDocumentReadbackJob.cpp:17-21`), and there is no
    fallback.
  - **Impact**: Export, Copy Merged, Merge, eyedropper, Hue, Wand composite and Clone fail on such adapters
    (some VM, remote and old drivers).
  - **D3D11 note**: FALSE means the runtime serializes creation with a coarse lock. Concurrent creation is still
    legal, so the refusal is stricter than D3D11 requires.
  - **Fix**: accept FALSE, or fall back to owner composition with a clear notice (Q8).
- [ ] **MR-12** · Medium · ux · CONFIRMED (pre-existing) — **Copy/Cut reports success when the OS clipboard
  was never written.**
  - **Where**: `RedPrism/ApplicationClipboardJobs.cpp:467-493` returns `Succeeded` once the private copy
    finished, even after six busy retries or an emptied/partial publish. `_clipboardWriteSucceeded` is
    diagnostics only.
  - **Impact**: pasting into another app gives stale or empty data with no message.
  - **Fix**: post a "copied inside RedPrism; the system clipboard was unavailable" notice.
- [ ] **MR-13** · Medium · resiliency/ux · PLAUSIBLE — **Input is not coalesced where queues are bounded.**
  - **Pointer ring**: `PointerInputStream<…, 512>` (`Application.h:418`) seals a stroke as `Lost` when the
    worker opening or a preceding job is slow (`Common/PointerInputStream.h:71-77`). That is about 0.5 s at
    1 kHz, or 4 s at 125 Hz.
  - **Nudge autorepeat**: each arrow-key autorepeat is one `HostSelectionNudgeInput` (`Application.cpp:5073`)
    in an 8-entry queue (`Common/ContinuationQueue.h:50`). `EnqueueAdjacent` coalescing is used only for
    opacity (`ApplicationContinuations.cpp:390`). Extra keys are dropped with MR-01's card.
  - **Fix**: coalesce interior Moves when the ring is half full. Sum adjacent pending nudges.
- [ ] **MR-14** · Medium · ux · CONFIRMED (pre-existing) — **A color-preview failure opens a native
  `MessageBoxW` from the UI-action loop, with no deduplication.**
  - **Where**: `RedPrism/ApplicationAdjustJobs.cpp:488-521`, `ApplicationFiles.cpp:118-126`. Up to 8 queued
    boxes can appear while a slider drags; completions and frames stall while one is open.
  - **Fix**: post `IDS_ERROR_FILTER` as a notice, once per panel opening.
- [ ] **MR-15** · Medium · resiliency · CONFIRMED (design) — **317 new `std::terminate()` sites in product
  code, and no document recovery.**
  - **Where**: 319 in product files at `b6ec336` (2 at `0ce2070`). The rule is `RedPrism/` and `Common/`,
    excluding paths containing `Tests` or `Benchmark`. They sit in active paths (`GpuCommandPump`,
    `RecordedDocumentReadbackJob`, the cursor `Acknowledge`, `HistoryResidencySource::CloseAfterWorkers`) as
    well as dormant ones (`ApplicationStrokeFinishJobs.cpp` alone has 60).
  - **Impact**: any invariant slip is a crash that loses unsaved work. `Core_CrashHandling.md` provides only a
    minidump and a prompt, and the dump does not name the invariant.
  - **Fix**: AR-8. Name invariants, demote those reachable from cancellation or admission to job failure, and
    decide on document recovery (Q11).
- [ ] **MR-16** · Medium · resiliency · PLAUSIBLE — **A GPU TDR could wedge all readbacks until restart.**
  - **Where**: `RedPrism/GpuWork.cpp:732-753` (`RetireDevice`) only flags slots. Waiting slots wait for their
    original event by design (`:833-855`). If a removed device never signals, `inFlight` stays at
    `kMaxInFlight == 2`.
  - **Fix**: a forced-TDR hardware test first (`dxcap -forcetdr`). If it wedges, check
    `GetDeviceRemovedReason` for Waiting slots in `RetireDevice`/`Pump`.
- [ ] **MR-25** · Medium · test reliability · PLAUSIBLE (exact signature match) — **The checkerboard
  load-selection fixture depends on the host's free memory.**
  - **Where**: `RedPrism/Application.cpp:9132-9194`. RF-1102 loads a 2100 × 2100 checkerboard mask and layer
    and expects `IDS_ERROR_SELECTION_DETAILED`. The scheduler samples live OS memory (`Application.cpp:198-200`;
    no stable `MemorySource` is injected).
  - **Mechanism**:
    - The load reserves about 7 GB through MR-03's boolean path.
    - On a busy host, admission refuses it with `E_OUTOFMEMORY`.
    - Every failure other than TooDetailed posts `IDS_ERROR_WAND_MEMORY` and counts one error
      (`ApplicationWandLayerHostJobs.cpp:283-293`).
  - **Evidence**: the unexplained PR #19 Release failure (recorded in `NativeBuildPolicyCoherence_2026-10-28.md`)
    was exactly that signature: mask false, layer false, blank layer silent, 2 errors. Its cause was never
    established, and a passing repeat proved nothing. The same live-memory dependence caused the ASan failure
    that 5069ec39 fixed in another fixture.
  - **Next failure**: the per-stage ids added by 715f400e now tell admission (`IDS_ERROR_WAND_MEMORY`) apart
    from a logic failure.
  - **Fix**: AR-4 removes the cause. Until then, either the fixture asserts that its reservation fits before it
    runs, or it uses an injected `MemorySource` with the real reservation still recorded. Do not hide MR-03 by
    shrinking the canvas.

### Low

- [ ] **MR-17** · Low · ux · CONFIRMED — **Wrong or generic messages.**
  - A Clone press with no source now shows "busy" (`Application.cpp:4576-4580`; silent before).
  - Paste failures all show "pixel edit" (`ApplicationClipboardJobs.cpp:1070-1092`).
  - Save failure drops its cause: access denied, disk full, path too long
    (`ApplicationProjectSaveJobs.cpp:254-262`).
  - A metadata admission failure says "pixel edit" (`Application.cpp:5235, 5369, 5438`).
  - More than 256 command-line paths gives "Unknown argument" (`RedPrism/CommandLine.h:331-334`).
  - A residency subscription failure exits 5 with the graphics message (`Application.cpp:308-311`).
- [ ] **MR-18** · Low · bug · CONFIRMED — `Common/PathClassificationRead.cpp:91, 111-114` pass the raw path to
  `GetFileAttributesW`/`CreateFileW` without `Win32FilePath::Prepare`. A long-path project drop is then
  misclassified as an image when `LongPathsEnabled=0`.
- [ ] **MR-19** · Low · resiliency · PLAUSIBLE — a crash or kill mid-save leaves `<dest>.~<guid>` staging (which
  can be GBs) beside the project, and nothing sweeps it (`Common/ProjectStore.cpp:274-278`).
- [ ] **MR-20** · Low · resiliency · PLAUSIBLE — the BoundedClipper arena overhead is about 180 B per 48-byte
  `OutPt` (`Common/BoundedClipper/Workspace.h:123-143`). The advertised 16M/8M point limits are therefore
  unreachable inside 128 MiB. Measure the real ceiling and set `Limits` from it.
- [ ] **MR-21** · Low · provenance · CONFIRMED — `Common/BoundedClipper/offset-generation-manifest.json` hashes
  CRLF bytes while the committed blobs are LF. It says `"runtime_qualified": false` although Expand/Contract
  ship on it, and nothing re-verifies either manifest. MR-24 widens this.
- [ ] **MR-22** · Low · performance — two owner-side costs:
  - `GpuCommandStream::IsStopped` does `WaitForSingleObject(…, 0)` per owner pass (`GpuCommandStream.cpp:60-63`).
  - Per-tile command lists each carry their own fence and flush (`GpuRecordingCommands.cpp:272-409`),
    unmeasured.
- [ ] **MR-23** · Low · robustness — `OwnerWorkMultiplexer::kCapacity == 11` equals the maximum source count
  (`Common/OwnerWorkMultiplexer.h:14`). A 12th source makes startup fail. A user run registers 6; all 11 attach
  only when every responsive source is armed (test probe or `--bench`). Add a `static_assert` or headroom.
- [ ] **MR-24** · Low · provenance · CONFIRMED — **PR #19 hand-edited the generated BoundedClipper engine.**
  - **Where**: `clipper.core.h`, `clipper.engine.h`, `clipper.engine.cpp` and `clipper.offset.cpp` gained
    casts, `case` labels and pragma scopes. `Build/GenerateBoundedClipper.py` was not changed.
  - **Hashes**: `generation-manifest.json`'s `generated_sha256` matched the LF blobs before PR #19. It no longer
    matches for three of the four engine files.
  - **Impact**: the documented reproduction (`Common/BoundedClipper/README.md`) now yields different sources.
    Re-running it would drop the `/Wall` repairs and break the zero-warning gate.
  - **Behavior**: unchanged. Every cast reproduces the old implicit conversion, and every label shares the old
    `default:` body.
  - **Fix** (Q14): either move the conversions into the generator's transformations and regenerate both
    manifests, or restore generator output and compile the vendored engine as third-party code (external
    warning level), as the vcpkg Clipper2 reference already is.

## Findings — latent (dormant today; block P1 activation)

Record these in the owning P1 plans when P1 resumes.

- **F1-2**: the prepared overlays drop the last good frame. The lasso draft and ants vanish while dragging,
  panning or zooming (`LassoDraftDisplayController.cpp:250-257, 452-454`).
- **F1-3**: press, polygonal and freehand failures post no notice (no `PostNotice` in
  `ApplicationSelectionPressJobs.cpp`, `ApplicationPolygonalPressJobs.cpp`, `ApplicationLassoOpeningJobs.cpp` or
  `ApplicationLassoFinishJobs.cpp`).
- **F1-4**: the cold lasso opening reserves an 8M-point arena, about 140 MB, per press
  (`ApplicationLassoOpeningJobs.cpp:231`).
- **F1-5**: the ants raster covers the whole hole × 9 planes per cell (`ApplicationSelectionOutlineDisplay.cpp:44-57`).
- **F1-6**: the polygonal journal (512 entries, `HostPolygonalInputJournal.h:62`) does not coalesce hover moves.
- **F1-7**: freehand is capped at 65,536 vertices (`Common/LassoFinishGeometry.h:79`) and the whole gesture fails
  at the cap.
- **E-5**: a failed async finish discards the completed stroke silently.
- **E-7**: per-sample admission plus a DXGI video-memory query on the owner.
- **G-3**: prepared layer rows disable the Tree on every generation bump.
- **C-3**: the committed route draws its cached graph without `GraphForFrameOwner()` validation
  (`Application.cpp:534-537`; the pending route validates).

## Architecture and simplification

- [ ] **AR-1** · Split the harness out of the product executable.
  - **Size**: `RedPrism.vcxproj` compiles 140 `*.Tests.*`/benchmark TUs (about 65k lines; 4 of 163 at base)
    beside 219 product TUs (124.5k).
  - **Header**: `Application.h` is 1,916 lines, with 433 members, 34 `TestGate` members and about 175
    self-test declarations.
  - **Product TUs carrying test code**:
    - 14 include `Tests/Support/Support.Tests.DocumentFixture.h` (`ApplicationProjectOpenJobs.cpp`,
      `ApplicationImageImportJobs.cpp`, `ApplicationHistoryRestoreJobs.cpp`, `ApplicationFiles.cpp`,
      `ApplicationContinuations.cpp` and others);
    - they host self-test bodies (for example `ApplicationProjectOpenJobs.cpp:396-1031`);
    - they contain harness `std::_Exit(5)` calls (`ApplicationFiles.cpp` ×3, `ApplicationProjectOpenJobs.cpp` ×1).
  - **Proposal**: build `RedPrismCore.lib` + `RedPrism.exe` + `RedPrism.Harness.exe`. Use one
    `ApplicationTestAccess` friend and one injected `ApplicationTestHooks` instead of 34 gates. This touches
    `test.ps1`, `package.ps1`, CI and the test-harness skill (Q3).
- [ ] **AR-2** · Dormant P1 code in the shipping image, about 12–14k lines:
  - `ApplicationStrokeFinishJobs.cpp` (2,154) and Warp/StrokeSample/StrokeFinish work (about 2.9k);
  - committed/pending display and final publication (about 4.7k);
  - prepared layer rows (about 1.2k);
  - selection outline/lasso draft/polygonal routes;
  - the GPU upload stack (about 2.6k, zero production callers).

  Keep it on `main` as is, compile it only into the harness until activation, or move it to a branch (Q2).
- [ ] **AR-3** · One host-history job skeleton (about 4–5k lines).
  - **Repetition**: about 35 `Application*Jobs.cpp` files repeat the same pieces:
    - a payload + `Retire*` deleter, `Current()`, and the `DeferCompletion` capture/`Resume`;
    - the `Completed` epilogue: outcome map, ticket/stamp reset, `Finish`, `ResumeHostHistory`;
    - the `Start*` prologue: `NextOrdinal`, `Begin`/`Continue`, checked byte sums, `Submit`, rollback.
  - **Duplicates**:
    - `PassGate` is defined in 15 files;
    - there are five identical error-mapping blocks (`ApplicationContinuations.cpp:1163-1301`);
    - there are four `*ByteSum` types and 17 `Add*Bytes` free helpers.
  - **Proposal**: a `HostHistoryJob<Traits>` base plus `StartHostHistoryJob`. `HistoryWork` would own its
    barrier ticket. That deletes:
    - about 30 ticket/stamp members;
    - the 156-line cancel chain (`ApplicationContinuations.cpp:1792-1947`);
    - the 120-line shutdown block (`Application.cpp:4020-4143`).
  - **Also**: it fixes MR-08/MR-13-class behavior once.
- [ ] **AR-4** · One admission model (root cause of MR-02 to MR-07 and MR-25).
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
  - Replace the 10 `SharedPublicationMemory::Bind*` methods and the 11 public `JobScheduler::Retain*`
    overloads with one mode/shape API (about 250 lines).
  - Replace the four parallel callback-authority fields in `Jobs.cpp:143-147` with a bitmask.
- [ ] **AR-7** · Delete dead code:
  - `ClassifyPathBatch` + scratch seam (`Common/PathClassificationWork.cpp`, about 170; tests only);
  - `DocumentReadbackJob.*` (461; only a fixture uses it);
  - the synchronous cursor branch and its members (`Application.cpp:7102-7134`);
  - `LayerTreeModel::badgeText/badgeTone` (write-only, `ChromeHost.h:856-857`);
  - test-only `ReplaceDocument`, `WriteFileBytesToDisk`, `PreviewLayerOpacity`, and the session
    `RenameLayer`/`ToggleVisibility` paths;
  - `SelectionMappingForTest()` in production paint (`CanvasOverlay.cpp:421`).
- [ ] **AR-8** · Fatal invariants (MR-15).
  - Replace bare `std::terminate()` with `[[noreturn]] FatalInvariant(id)`, which records the id in the crash
    marker or dump.
  - Demote cancellation/admission-reachable sites to job failure.
  - Add Main-level document recovery if fail-fast stays the policy (Q11).
- [ ] **AR-9** · ISA gate.
  - `Build/IsaScan.cs` is 5,518 lines, with 41 `Isa*.ps1` tests (about 13.5k lines) and 56 commits. It adds
    about 73 s to every x64 build (`build.ps1:159-166`).
  - It guards against VEX/EVEX outside AVX tier objects, but no AVX tier exists: only
    `Kernels_Scalar_Adjust`, and no `/arch:AVX` in any project.
  - Replace it with a compile-flag/TU-allowlist gate until a tier lands, and keep the binary scan as an opt-in
    qualification step (Q6).
- [ ] **AR-10** · Tooling consolidation.
  - `test.ps1` defaults to Tooling, so the AGENTS.md matrix runs `validate.ps1` three times and
    `validate-skills` four times.
  - `Tests/native-test-files.json` (902 lines) duplicates the naming check.
  - There are five copies of the reparse-point ancestor walk.
  - `Tools/**`, `.github/**`, `.gitignore` and `*.md` have no rule in `Tests/test-scopes.json`, so they select
    the full native scope. Editing `Submit-ChangelogFold.ps1` runs every native suite.
  - `Build/RedMonitor.props` references a nonexistent `HostPluginTests`.
  - `validate.ps1:21-22` is a one-off check for a Done plan.
- [ ] **AR-11** · Utility readback pipeline (MR-05).
  - Region-bounded upload when recording a utility tile.
  - Cache shaders, states and samplers per device generation instead of per job
    (`RecordedUtilityGraph.cpp:49-61`, `GpuRecordingCommands.cpp:156-209`).
  - Fold the four copies of the fence `Signal`/`SetEventOnCompletion` sequence (`GpuWork.cpp:269-271, 343-345,
    687-689`; `GpuUploadWork.cpp:247-249`).
- [ ] **AR-12** · Warning-suppression boilerplate from PR #19 (Q13).
  - **Size**: 3,872 of PR #19's 5,558 added C++ lines are suppression scaffolding:
    - 2,338 `#pragma warning` lines;
    - 1,534 lines of one repeated two-line comment ("Existing identity/RAII/reference traits intentionally
      delete these implicit operations … review: 2027-01-31").

    The tree now holds 767 copies of that comment across 372 files. Every one carries the same review date.
  - **What they suppress**: almost all are C4623/C4625/C4626/C5026/C5027, notices that a default
    constructor, copy or move was implicitly deleted. They fire for every type holding a reference, a const, a
    `unique_ptr` or an atomic, which is correct RAII. Any actual copy or move of such a type is already a hard
    error, so the notice is never actionable.
  - **Proposal**: add the five codes to the policy's non-actionable list in `Build/NativeBuildPolicy.targets:24`,
    beside 4514/4710/4711/4820/5045, and delete the scoped blocks. This also removes the declarator splits, where
    a pragma sits between a local struct's `}` and its variable (`state{…};`). There are ten in product code (for
    example `Executor.h`, `Jobs.cpp`, `Main.cpp`) and about 69 in tests.
  - **Related clean-ups**:
    - `GpuRecordingCommands.cpp:22-141` lists all 120 `DXGI_FORMAT` enumerators to silence C4061 on a
      three-case switch. Any SDK that adds a format re-fires C4061 and fails the zero-warning gate. Use an `if`
      chain or a one-line `suppress`.
    - `ApplicationClipboardJobs.cpp:544-620` scopes C4355 over the whole class rather than the constructor.
- [ ] **AR-13** · Build-path tooling (PR #19).
  - Make `Get-DxUiSourcePath` the only spelling of the source-folder convention. It is hard-coded in
    `DxUiProvenance.psm1:21` and `ReleaseBuildPolicy.Tests.ps1:21`, and stale in
    `VcpkgToolsetTests.ps1` (CI-8). `package.ps1` already calls the helper; only its source-folder comment is stale.
  - Fold `DxUiOutputPath.psm1`, a rename wrapper with three callers, into `DxUiPaths.psm1`.
  - Drop the duplicate settings in `Directory.Build.props:39-40` and `Directory.Build.targets:7`, which
    `NativeBuildPolicy.targets` already sets. ASan `/Zi` is set both there and in `NativeBuildPolicy.targets:49`.
  - Have `Submit-ChangelogFold.ps1` use a sparse worktree (`Changes`, `Tools`, `CHANGELOG.md`) instead of a full
    checkout. Merge the duplicate git wrappers `Invoke-FoldGit` and `Read-ChangelogGit`.

## CI and tooling

- [ ] **CI-0** · process · CONFIRMED — **No hosted check ran on PRs #13–#22.**
  - Every job was refused with "recent account payments have failed or your spending limit needs to be
    increased", so `native-scope` and `tooling` show FAILURE and `native` SKIPPED.
  - The user authorized local validation for these merges. The authorizations are recorded in
    `RedMonitor_Adoption_2026-10-23.md` and `NativeBuildPolicyCoherence_2026-10-28.md`.
  - Until CI runs, AGENTS.md's `CI_PENDING` rule is not enforceable (Q4).
- [ ] **CI-1** · High (CI reliability) · CONFIRMED — `Tools/Get-CiNativeScope.ps1` fails its own fall-back.
  - Its degrade paths (`:34`, `:44`, `:56`) leave `$LASTEXITCODE` at 1 with no final reset, and GitHub's `pwsh`
    wrapper exits with it. One cancelled run with no artifact keeps the PR red on every rerun.
  - Fix: `$global:LASTEXITCODE = 0` at the end, plus a Tooling test with a failing fake `gh`.
- [x] **CI-2** · FIXED by PR #20 — `test.ps1:53` now runs `Tools/Test-Changelog.ps1` (fragment validation only),
  so pending fragments no longer fail Affected runs.
- [x] **CI-3** · SUPERSEDED by PR #20 — fragments now reach `main` by design and are folded afterwards. The
  PR static adapter runs `Test-Changelog.ps1 -Base`; CI-10 records the reconciled workflow routing.
- [ ] **CI-4** · Low · CONFIRMED — native-gated `Tests/BuildProcessTests` scripts (`FailureReportTests`,
  `Ui*Benchmark*`, `LayerRowsBenchmarkSmokeTests`, `IsaScanTests`, `CodeAnalysisTests`) map to Tooling only
  (`Tests/test-scopes.json:120-126`).
- [ ] **CI-5** · Low · CONFIRMED — `Build/IsaScan.cs:5479-5480` accepts `<unknown>` at C4/C5/62 lead bytes
  without proof. The current Release report shows two such bytes. Pin the decoder, or require a proven range.
- [ ] **CI-6** · Low · CONFIRMED — `.github/workflows/native-arm64-feasibility.yml` is pinned to
  `codex/p1-opacity-host-proof`. Its helper (`.github/scripts/Run-NativeArm64.ps1:44`) installs to
  `C:\P1NativeTools1452`, a drive-root output that AGENTS.md forbids. Delete the trio (934 lines).
- [ ] **CI-7** · Low — three smaller items:
  - CI uploads crash-probe `.dmp` files on passing runs: the `if: always()` upload covers
    `.build/evidence/test-runs/**` (`ci.yml:106-120`), which holds the crash-probe folder (`test.ps1:144`);
  - a `Complete-TestRunEvidence` failure in `finally` masks the real error (`test.ps1:440`);
  - the ISA scan runs on every build with no hash skip (`build.ps1:159-166`).
- [ ] **CI-8** · Medium · residue after `18fd8ba` — **the packaging join is fixed; the parity test still skips.**
  - At `b6ec336`, `package.ps1` joined `source\<40-hex commit>` after PR #19 had moved the clone to `source/<first-16-hex>` (`Get-DxUiSourcePath`).
  - On `e79a83a`, `package.ps1` calls `Get-DxUiSourcePath`. A missing license under the 40-hex folder does not block the ZIP.
  - The comment above that call still describes `source\<commit>`.
  - `Tests/BuildProcessTests/VcpkgToolsetTests.ps1:165` still joins `.build/dependencies/DxUi/source/$($lock.commit)` and skips when `Test-Path` fails.
  - **Remaining fix**: point that test at `Get-DxUiSourcePath` and fail when the restored source is missing. Correct the `package.ps1` comment. Do not change the `Get-DxUiSourcePath` call that already ships the license.
- [ ] **CI-9** · Medium · CONFIRMED — **The zero-warning gate passes on a second build.**
  - **Mechanism**: PR #19 replaced `/W4 /WX` (`TreatWarningAsError=true` at its base) with `/Wall` and no `/WX`.
    Warnings now fail only through the log scan (`build.ps1:144`, `Assert-NativeBuildDiagnostics`). A warning
    no longer stops the object file from being written.
  - **Impact**: an incremental rerun (`/t:Build`) does not recompile the translation units that warned. The new
    log is clean, so the run writes `build-attestation.json` (`:176`) and prints green. Scoped testing then
    reuses that attestation, and CI's clean builds are not running (CI-0). Read from source, not executed.
  - **Fix** (Q15): when the diagnostic gate fails, write a marker beside the log that forces the next run to
    rebuild (or deletes the offending objects), and add a two-run tooling test.
- [x] **CI-10** · RESOLVED (2026-11-01) — **the changelog contract now matches the pinned shared engine.**
  - At `b6ec336`, the "no push trigger" sentence matched the workflow. On `e79a83a`, `ci.yml` already triggers pushes to `main` and delegates them to shared `coordinate.yml`.
  - Verified engine [`19710a8aeac979a82e83bec82e8b866cee3d3deb`](https://github.com/RedSalamanders/.github/tree/19710a8aeac979a82e83bec82e8b866cee3d3deb): [`pr.yml`](https://github.com/RedSalamanders/.github/blob/19710a8aeac979a82e83bec82e8b866cee3d3deb/.github/workflows/pr.yml) checks out the exact PR head and supplies its immutable base to the project's Static adapter, which runs `Test-Changelog.ps1 -Base`.
  - Shared [`coordinate.yml`](https://github.com/RedSalamanders/.github/blob/19710a8aeac979a82e83bec82e8b866cee3d3deb/.github/workflows/coordinate.yml) calls `Invoke-Coordinator.ps1` / `Invoke-CiCoordinator`; main pushes coordinate native qualification and do not perform a `github.event.before` changelog comparison.
  - [`Core_Changes.md`](../../Core/Core_Changes.md) is reconciled with that routing. Q16 is resolved without changing workflow code. CI-0 and native/release qualification remain open.
- [ ] **CI-11** · Low · CONFIRMED — **A fold silently drops a fragment whose text matches an unreleased entry.**
  - `Merge-ChangelogFragments` seeds its duplicate set with every Unreleased entry
    (`Tools/Modules/Documentation/Changelog.psm1:224-231`). The set exists for retry idempotence. Any fragment
    with the same text is then dropped, and its file is deleted. The `-Base` check uses the same function, so it
    agrees.
  - Fix: name each dropped fragment in a warning, or refuse unless the entry is the one this fold already
    published.
- [ ] **CI-12** · Low · CONFIRMED — **Fold submission robustness.**
  - **Unverified reuse**: `Tools/Submit-ChangelogFold.ps1:69-71` accepts an already-pushed
    `changelog/fold-<short>` without checking its parent or tree, then opens a pull request for it.
  - **Version skew**: `:88-90` folds with `main`'s `Fold-Changelog.ps1` but validates with the caller's
    `Test-Changelog.ps1`.
  - **Fails open**: `Changelog.psm1:157-159` turns any `cat-file` failure into "before adoption" and skips the
    exact-fold check.
  - **Orphaned branch**: if `gh pr create` fails after the push, the branch stays. It is reused only while
    `main` has not moved (PLAUSIBLE).
  - **Fix**: verify a reused branch against the fold, use the worktree's validator, and distinguish "path does
    not exist" from other git failures.
- [ ] **CI-13** · Low · CONFIRMED — **The DxUi build-identity marker is not written atomically.**
  - **Mechanism**: `Build/DxUiPaths.psm1:37-45` creates `DxUi.build-identity.json` with `CreateNew`, then
    writes it. An interrupted restore leaves an empty or partial marker.
  - **Impact**: every later run fails in `Assert-DxUiOutputIdentity` with a parameter-binding error that does
    not say to delete the folder.
  - **Junction**: the helper's probe also found that a junction carrying a valid marker is accepted and written
    through. That probe was not re-run.
  - **Fix**: write to a temporary file and `File.Move` it. Reject reparse points on the output. Name the
    remedy in the error.

## Questions

- **Q1 — Admission policy.** Should user-initiated commands be admitted from input-sized estimates, with engine
  limits as refusal limits, resident inputs uncharged and memory waiting allowed (AR-4)? Or is the conservative
  worst-case envelope intended, with refusal as the accepted outcome? This decides MR-02 to MR-07 and MR-25. If
  AR-4 is too large for the first release, may Inverse/Offset/Wand Add-Subtract and Load Mask/Layer Selection
  temporarily return to the pre-window synchronous route?
- **Q2 — Dormant P1 code.** Keep the ~12–14k dormant lines in `RedPrism.exe`, compile them into the harness only
  until activation, or move them to a branch? P1 is HOLD; you said you would restart it from this `main`.
- **Q3 — Harness split (AR-1).** May the build produce a separate harness executable? This changes `test.ps1`,
  `package.ps1`, CI and the test-harness skill.
- **Q4 — CI.** Hosted CI has now been refused for ten merges (PRs #13–#22). Will billing be restored before
  production? Or should AGENTS.md formally adopt "local Full matrix authorizes merge", with the required local
  evidence named? Either way, CI-9 must be fixed first, because today a local green can be produced without a
  clean compile.
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
- **Q13 — Implicitly-deleted notices (AR-12).** The native policy lists five non-actionable diagnostics and
  says they "do not justify suppressing other warnings". May C4623, C4625, C4626, C5026 and C5027 join that list,
  so that the 767 scoped blocks (about 3,900 lines) can be deleted? This applies to all five repositories that
  share the policy. The alternative is one shared macro header, which keeps the scopes but holds the review date
  in one place.
- **Q14 — Vendored engine (MR-24).** Should `Common/BoundedClipper/clipper.*` be treated as third-party code
  (generator output restored, external warning level, manifests regenerated)? Or should the `/Wall` conversions
  become generator transformations? The first keeps upstream diffs minimal. The second keeps `/Wall` coverage
  of the fork.
- **Q15 — Zero-warning gate (CI-9).** The agreed policy keeps `/WX` off. Is a failure marker that forces the
  next `build.ps1` run to rebuild an acceptable repair? Or should `build.ps1` pass `/WX` for compiler warnings
  while the log scan keeps covering linker and D9xxx diagnostics?
- **Q16 — CI routing (CI-10), RESOLVED 2026-11-01.** Preserve the existing unified routing: PR static checks
  validate the exact head against its supplied immutable base; main pushes invoke the quiet-window native
  coordinator. The pinned engine was traced and `Core_Changes.md` now agrees. No additional push trigger is needed.

## Proposed execution order

Each package lands with its spec text, its test (a legacy numeric golden where the oracle covers the behavior),
one `Changes/` fragment, and the AGENTS.md validation matrix. Folding happens on `main` after merge.

1. **WP0 — decisions and measurements (no code).**
   - Answer Q1–Q5 and Q13–Q15. Q16 is resolved above.
   - Measure, on 8 GB and 16 GB machines:
     - the actual Inverse/Offset/Wand/Load-selection reservations (the existing test gates record
       `reservationBytes`);
     - residency maintenance frequency and duration;
     - first-eyedropper latency on a 45 MP document;
     - a forced TDR during Export.
   - Re-run the RF-1102 fixture under memory pressure and read its per-stage ids (MR-25).
2. **WP1 — quick wins (small, independent).**
   - Release and gate blockers first: CI-8, CI-9 and CI-1.
   - User-facing messages: MR-01, MR-12, MR-17, MR-18.
   - Provenance and robustness: MR-21 with MR-24, MR-23.
   - Tooling: CI-6, CI-7, CI-11, CI-12, CI-13. CI-10 is resolved by the documentation reconciliation.
3. **WP2 — admission model.** AR-4, fixing MR-02 to MR-07, MR-25 and MR-06's retained charge, with spec updates
   in `Core_PerformanceAndResources.md`.
4. **WP3 — feedback and input.** MR-08 (central failure notice), MR-07, MR-13, MR-14, MR-10, MR-09.
5. **WP4 — resiliency.** MR-11, MR-16 (test first), MR-19, MR-20, AR-8 (named invariants, demotion; recovery
   per Q11).
6. **WP5 — structure.**
   - AR-12 and AR-13 first, because they are mechanical and shrink every later diff.
   - Then AR-1, AR-2, AR-3, AR-5, AR-6, AR-7 and AR-11.
   - Then AR-9 and AR-10, per Q6.
7. **WP6 — release qualification.** Hosted CI green (Q4), the Full matrix on the integrated result,
   `package.ps1` end to end, and the gates from Q5.

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
  - `contents: read` and `actions: read` only, and actions pinned by SHA;
  - no untrusted `${{ }}` in `run:`; the changelog base ref passes through `env`;
  - the planner falls back to Full for unknown files;
  - `build.ps1` rejects, never kills, a running `RedPrism.exe`;
  - native-proof reuse checks the input key, repository, image version, head SHA and producer job, not just
    the branch (`Get-CiNativeScope.ps1:36-53`).
- **RedMonitor**: the client is ETW TraceLogging only (no pipe or connect); a missing viewer is harmless; the
  lock hashes are verified before the build.
- **PR #19 product code**:
  - `RedPrism/`: 361 of 364 hunks are provably identical; 3 are self-test-only diagnostics with no hot-path
    allocation, blocking, ordering change or new state.
  - No hunk adds a `default:`, `catch`, `terminate`, member, or layout change outside the ARM64 tail member.
  - Push/pop counts balance in every changed file. No C4061/C4062/C4365/C4242/C4244/C4267 disable was added;
    those were fixed with labels and casts.
  - `Common/` casts reproduce the old implicit conversions. That includes the Heal float sums and the clipper
    int64→double products.
  - Every added `case` label shares an existing `default:` body or an existing `break`.
  - No brace-to-parenthesis construction (15 product sites) has order-dependent arguments, and none binds a
    temporary to an aggregate reference member.
- **PR #19 ARM64 tail member**: `Raster::Storage` gains `arm64AbiTailPadding` only on ARM64. The x64 layout is
  unchanged. Accounting uses `sizeof` (`Document.cpp:592, 698`; `HistoryEntries.h:71, 131`), and `Document.h:390`
  asserts the derived size.
- **PR #19 other `Common/` changes**: the `ProjectStore` raw-pointer copies keep their explicit length checks,
  and the `noexcept` CRT handlers still convert to the CRT handler types.
- **PR #19 tests**: no assertion changed meaning; no timing, tolerance or golden value changed.
  - 5069ec39 correctly un-reverses the `MemorySource` fields and proves the injected sampler on all 128
    admissions.
  - 4226ec61 adds a retained-pixel assertion.
  - The PowerShell policy tests gain negative controls.
- **Build policy**: an MSBuild evaluation gives `/Wall`, no `/WX`, Debug `/ZI`, ASan `/Zi` and Release-only
  `/OPT:ICF` on all 359 `ClCompile` items, including items with per-file metadata. C4746 is disabled only for
  ARM64 Release.
- **PR #20 / #22 changelog**:
  - PR #22 is an exact fold: parent `d9939753`, one entry added, its fragment removed, and
    `Test-Changelog.ps1 -Base` passes.
  - `Submit-ChangelogFold.ps1` lists open pull requests before creating anything, never force-pushes, removes
    only its own worktree and resets `$LASTEXITCODE`.
- **Drive roots**: no drive-root output in PRs #19–#22.

## Rejected in the second pass

- *"PR #19 hand-edits `CHANGELOG.md`."* At PR #19's base, AGENTS.md required folding inside the change; PR #20
  changed that rule afterwards.
- *"CI-7: native-proof reuse is keyed by branch name only."* The first pass overstated this. Only discovery uses
  the branch; acceptance checks full identity. The item was withdrawn from CI-7.
- *"PR #20's changelog entry says the first fold remains unrun."* That was true when written; entries record
  their own time.
- *"Brace-to-parenthesis initialization reorders evaluation."* No converted site depends on argument order.

## Corrections to the first pass

- MR-01: the base had 9 busy-notice sites, not 6.
- AR-1: the base compiled 4 test/benchmark TUs (of 163), not 26.
- AR-3: there are four `*ByteSum` types, not six.
- AR-6: `JobScheduler` has 11 public `Retain*` overloads, not 9.
- MR-03: worker routing is conditional on `CanApplyHistoryRestore`. Load Mask/Layer Selection share the boolean
  envelope.
- MR-06: the spec lines are `:1448` and `:1456-1463`, not `:1453`.
- The first pass said the CI-row files were unchanged since its snapshot. PR #20 changed `test.ps1` and
  `ci.yml`, which closed CI-2 and CI-3.

## Coverage and limits

- **First pass**: all 365 changed production files in `RedPrism/` and `Common/` were assigned to exactly one
  area, and every changed build/CI/tooling file to area H.
- **Second pass**: every PR #19 product hunk was reviewed (`Common/` by the lead, `RedPrism/` by a helper),
  every PR #19 test hunk, and all build and changelog tooling in PRs #19, #20 and #22. Every row above was
  re-anchored and re-checked at `b6ec336`.
- **Not reviewed in depth**:
  - generated `Common/BoundedClipper/clipper.*` beyond PR #19's hunks;
  - self-test bodies and `Tests/**` beyond coverage questions;
  - dormant files flagged as such in each area (for example `GpuUploadWork.cpp` 420–933 and
    `PolygonalDrawingJob.cpp`);
  - DxUi's `validate_consumer.ps1`, which the DxUi policy keeps out of scope;
  - whether `Directory.Build.targets` and `PinnedDxUiPolicy.targets` can import `NativeBuildPolicy.targets`
    twice for the DxUi project;
  - ARM64 `/ZI` behavior.
- **Not executed**:
  - no build, test, profiler or hardware repro was run;
  - no `package.ps1` run;
  - no hosted CI run;
  - no real `gh` or network path beyond PR #22's own evidence;
  - the external `E:\*.Perf` evidence was not opened.

  Every magnitude is source arithmetic. WP0 must measure the High rows before WP2 sizes its fix.
- **Retained notes**: the per-area reports, helper reports and verification ledgers are kept outside the
  repository in the review sessions' scratchpads. This file is their verified summary.
