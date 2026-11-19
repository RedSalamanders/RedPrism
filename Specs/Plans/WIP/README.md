# Active work

Files in this directory are non-normative execution or decision records; current requirements stay in the owning
domain spec under `Specs/<Domain>/`. This index is routing only. Every direct plan in this directory is listed
exactly once, as a bullet that opens with its bold status and a colon (`- **ACTIVE**:`), links its title to its
file, and ends with an em dash and its purpose. `ACTIVE` is executable work, `HOLD` paused work, `DECISION`
unresolved product intent, and `RETIRED` a bounded tombstone that names its retirement date, reason, current
owner, and a `git show` recovery command.

P1 and every child in the HOLD rows are paused since 21 October 2026. Resume only from [`UiNeverBlocks_2026-09-22.md`](UiNeverBlocks_2026-09-22.md). The October checkpoint narrative that used to open this file lives in that plan. `P1MainIntegration_2026-10-21/*.json` is evidence referenced from that plan and from [`P1MainIntegration_2026-10-21.md`](../../TestRuns/P1MainIntegration_2026-10-21.md). It is not a plan.

Re-checked against `main` `e79a83a` on 1 November 2026. `package.ps1` calls `Get-DxUiSourcePath`. `.github/workflows/ci.yml` triggers `push` to `main`. The busy string, the Inverse and Wand admission ceilings, and the format-7 writer are still the code. Correction notes are in the MergeReview, RedTeam, and DeliveryReview files. Line numbers in [`AdversarialReview_2026-10-21.md`](AdversarialReview_2026-10-21.md) are the 16 October snapshot.

The Windows DirectX port master program is historical:
[`WindowsDirectXPort_2026-09-20.md`](../Done/WindowsDirectXPort_2026-09-20.md). The C0–C7 review and the DxUi editor-control adoption are
recorded in [`PortReview_2026-09-21.md`](../Done/PortReview_2026-09-21.md). C8 Transform is recorded in
[`Transform_2026-09-20.md`](../Done/Transform_2026-09-20.md), C9 Selection in
[`Selection_2026-09-20.md`](../Done/Selection_2026-09-20.md), C10 Brush in
[`Brush_2026-09-20.md`](../Done/Brush_2026-09-20.md), C11 Retouch in
[`Retouch_2026-09-20.md`](../Done/Retouch_2026-09-20.md), C9b Pixel edits in
[`PixelEdits_2026-09-21.md`](../Done/PixelEdits_2026-09-21.md), C12 Adjust and filter in
[`AdjustFilter_2026-09-20.md`](../Done/AdjustFilter_2026-09-20.md) C13 Smart operations (Remove Background
rejected for v1) in [`SmartOps_2026-09-20.md`](../Done/SmartOps_2026-09-20.md) C14 Product hardening in
[`ProductHardening_2026-09-20.md`](../Done/ProductHardening_2026-09-20.md) C15 Packaging in
[`Packaging_2026-09-20.md`](../Done/Packaging_2026-09-20.md) and C16 Completeness (the whole legacy command surface, the
unimplemented-function notice, images opened as a layer or a new project, the size cards, the Crop tool, rename and
merge) in [`Completeness_2026-09-21.md`](../Done/Completeness_2026-09-21.md). Session undo/redo, cut, save-before-replace, exploded-project reparse checks, hostile zip tests, the guide, device recovery, and marching ants are in
[`PortProgress_2026-09-21.md`](../Done/PortProgress_2026-09-21.md). Tree multi-select and drag-reorder stay on the DxUi plan `TreeReorder_2026-09-21.md`, tracked here by [`DxUiFollowUps_2026-10-01.md`](DxUiFollowUps_2026-10-01.md). Brush measured follow-ups stay in `Specs/TestRuns/Brush_2026-09-21.md`.
The 2026-09-23 whole-codebase review and its fixes are in [`ReviewFixes_2026-09-23.md`](../Done/ReviewFixes_2026-09-23.md).
Its deferred rows RF-1003, RF-1004 and RF-1115 belong to `UiNeverBlocks_2026-09-22.md`.

- **DECISION**: [Merge review (merges of 21–30 October 2026)](MergeReview_2026-10-28.md) — Q1–Q15 open; CI-10 / Q16 resolved against pinned engine `19710a8a` and `Core_Changes.md`. CI-8's packaging join is fixed; its parity-test skip and comment remain. Findings owner for the live product. Do not copy its rows into another plan. Owning specs: `Core_Architecture`, `Core_PerformanceAndResources`, `Core_CrashHandling`, `Core_Changes`, `Document_Tools`, `Rendering_Composition`, `UI_ApplicationChrome`, `UI_Windowing`, `Build_Toolchain`, `Build_Packaging`, `Testing_Validation`.

- **DECISION**: [Red-team delivery review (15-day window ending 31 October 2026)](RedTeamDelivery_2026-10-31.md) — WP0–WP6 sequence the open MergeReview work. CI-2, CI-3, and the ZIP-blocker reading of CI-8 are not open. Delivery sequence. Tick MergeReview when a row closes; do not keep a second copy. Owning specs: Same contracts as MergeReview, plus `Core_DxUiIntegration` and `IO_ProjectFormat`.

- **ACTIVE**: [Delivery review 2026-10-31](DeliveryReview_2026-10-31.md) — DX-2's fix arrived with the API-revision-4 pin (2 November); its product acceptance has not run. This file does not say the product has no other defect. Run DX-2's menu hover-timer acceptance on the product. Owning specs: `Core_DxUiIntegration`.

- **DECISION**: [DxUi follow-ups: pin update, Tree multi-select, registered host messages, vcpkg toolset and pin restore](DxUiFollowUps_2026-10-01.md) — Lock is `f2e328a0`, API revision 4, which has Tree multi-select (DxUi #35). Decide where the editor uses Tree multi-select. The `271bd54` pin step is historical. Owning specs: `Core_DxUiIntegration`, `Build_Toolchain`.

- **HOLD**: [P1 — Responsive raster editor delivery milestone](UiNeverBlocks_2026-09-22.md) — since 21 October 2026. Nine delivery gates in that file remain open. The saved continuation names another machine and DxUi pin `8e899407`; the lock on this main is `f2e328a0` (API revision 4). Responsive editor milestone. Do not resume from the saved machine paths. Owning specs: `Core_PerformanceAndResources`, `Core_Architecture`, `Document_*`, `Rendering_*`, `IO_*`, `UI_*`, `Testing_Validation`.

- **HOLD**: [Ordered selection workers (P1.6)](SelectionWorkers_2026-10-01.md) — with P1. P1.6 selection workers. Ordinary activation stays off. Owning specs: `Document_Tools`, `Document_Model`, `Document_History`, `Core_PerformanceAndResources`, `Rendering_Composition`, `Testing_Validation`.

- **HOLD**: [Bounded lasso input, display and completion (P1 / S5)](LassoWorkers_2026-10-12.md) — with P1. P1 lasso input, draft, and completion. Owning specs: `Document_Tools`, `Document_Model`, `Document_History`, `Core_PerformanceAndResources`, `UI_ApplicationChrome`, `Rendering_Composition`, `Testing_Validation`.

- **HOLD**: [Asynchronous raster edits (P1.5)](AsyncRasterEdits_2026-10-01.md) — with P1. P1.5 detached raster edits. Owning specs: `Document_Tools`, `Document_Model`, `Document_History`, `Core_PerformanceAndResources`, `Rendering_Brush`, `UI_ApplicationChrome`, `Testing_Validation`.

- **HOLD**: [History storage and ownership (P1.9)](HistoryStorageAccounting_2026-09-30.md) — with P1. P1.9 history storage and ownership. Owning specs: `Document_History`, `Document_Model`, `Core_PerformanceAndResources`, `Testing_Validation`.

- **HOLD**: [P1.9 H3 — Incremental history storage accounting](IncrementalHistoryStorage_2026-10-12.md) — with P1. P1.9 incremental history accounting. Owning specs: `Document_History`, `Document_Model`, `Core_PerformanceAndResources`, `Testing_Validation`.

- **HOLD**: [Document lifecycle workers (P1.9 H1)](DocumentLifecycleWorkers_2026-10-02.md) — with P1. P1.9 document lifecycle workers. Owning specs: `Document_Model`, `Document_History`, `Core_PerformanceAndResources`, `Testing_Validation`.

- **HOLD**: [Shutdown CPU retirement (P1.7 F5d)](ShutdownCpuRetirement_2026-10-02.md) — with P1. P1.7 shutdown CPU retirement. Owning specs: `Core_Architecture`, `Core_PerformanceAndResources`, `Testing_Validation`.

- **HOLD**: [Asynchronous file processing (P1.7)](AsyncFileProcessing_2026-09-30.md) — with P1. P1.7 file and clipboard workers. Owning specs: `IO_ImageCodecs`, `IO_ProjectFormat`, `Core_PerformanceAndResources`, `UI_ApplicationChrome`, `Testing_Validation`.

- **HOLD**: [Asynchronous Image Size and Canvas Size](AsyncDocumentSizing_2026-09-30.md) — with P1. P1.8 Image Size and Canvas Size. The 2 ms apply target stays open. Owning specs: `Document_Tools`, `Document_Model`, `Core_PerformanceAndResources`, `Testing_Validation`.

- **HOLD**: [GPU completion and asynchronous readback](GpuCompletionReadback_2026-09-30.md) — with P1. P1.3 GPU completion and readback. Owning specs: `Rendering_Composition`, `Core_Architecture`, `Core_PerformanceAndResources`, `Testing_Validation`.

- **HOLD**: [P1.3 — asynchronous canvas color sampler](SamplerReadback_2026-10-04.md) — with P1. P1.3 canvas color sampler. Owning specs: `Rendering_Composition`, `Core_PerformanceAndResources`, `Testing_Validation`.

- **HOLD**: [P1.3 — All Layers wand New-selection readback](WandCompositeReadback_2026-10-04.md) — with P1. P1.3 All Layers wand readback. Owning specs: `Rendering_Composition`, `Document_Tools`, `Core_PerformanceAndResources`, `Testing_Validation`.

- **HOLD**: [Deferred canvas input during opacity closure (P1.9 H2.2)](DeferredCanvasInput_2026-10-04.md) — with P1. P1.9 deferred canvas input. Owning specs: `Core_PerformanceAndResources`, `UI_ApplicationChrome`, `Document_Tools`, `Rendering_Brush`, `Testing_Validation`.

- **HOLD**: [Deferred canvas lifetime closure (P1.9 H2.2)](DeferredCanvasLifetime_2026-10-04.md) — with P1. P1.9 close, new, and shutdown preflight. Owning specs: `Document_History`, `UI_ApplicationChrome`, `UI_Windowing`, `Core_PerformanceAndResources`, `Testing_Validation`.

- **HOLD**: [Deferred color display construction and retirement (P1.9 H2.2)](DeferredColorDisplay_2026-10-04.md) — with P1. P1.9 deferred color display. Owning specs: `Document_Model`, `Document_Tools`, `Document_History`, `Core_PerformanceAndResources`, `Testing_Validation`.

- **HOLD**: [Cached layer-record ownership bounds (P1.9 H2.2)](LayerRecordOwnershipBounds_2026-10-04.md) — with P1. P1.9 cached layer-record bounds. Owning specs: `Document_Model`, `Document_History`, `Core_PerformanceAndResources`, `Testing_Validation`.

- **HOLD**: [P1.9 H4 — Bounded raster uploads and first display](BoundedRasterUploads_2026-10-05.md) — with P1. P1.9 bounded raster uploads. Owning specs: `Rendering_Composition`, `Core_Architecture`, `Core_PerformanceAndResources`, `UI_Windowing`, `Document_Model`, `Testing_Validation`.

- **HOLD**: [Private immutable layer and typed opacity host candidate](OpacityHostCandidate_2026-10-04.md) — with P1. Experimental checkpoint, not a release step. Private opacity-host candidate. Overlaps the deferred-canvas plans. Owning specs: `Document_Model`, `Document_History`, `Core_PerformanceAndResources`, `Testing_Validation`.

- **HOLD**: [P2–P6 — CPU/GPU acceleration, RAW and nondestructive development](ImageProcessingAccelerationAndRaw_2026-10-04.md) — until P1 qualifies. Decode, develop, and Photoshop comparison stay open. The RAW corpus is acquired. P2–P6 acceleration, RAW, and high bit depth. Do not start from this file. Owning specs: `Core_PerformanceAndResources`, `Document_*`, `Rendering_*`, `IO_*`, `Testing_*`, `UI_*`.

- **ACTIVE**: [Scoped testing and validation cost](ScopedTesting_2026-10-05.md) — closeout. The original eight-box sequence is superseded. Native and CI qualification on the resumed checklist remain. Affected scopes, exact evidence, and PrePush delegation. Owning specs: `Testing_Validation`.

- **ACTIVE**: [Native build policy, terminal results and short DxUi paths](NativeBuildPolicyCoherence_2026-10-28.md) — closeout. Policy is merged. Changelog fold and final gates remain. Evidence paths are machine-local. Shared /Wall policy, terminal results, and 16-hex DxUi folders. Owning specs: `Build_Toolchain`, `Build_ConsoleAndPaths`.

- **ACTIVE**: [MSVC 14.52 and Release optimization](Msvc1452ReleaseOptimization_2026-10-23.md) — closeout. Local six-build and three Full runs are recorded. Size comparison and the fold remain. MSVC 14.52 and Release optimization measurements. Owning specs: `Build_Toolchain`.

- **ACTIVE**: [RedPrism RedMonitor adoption](RedMonitor_Adoption_2026-10-23.md) — The adapter is in. Native ARM64 runtime, interactive, and paired-performance gates remain. Optional diagnostic client. Those gates are shared, not a second feature. Owning specs: `Core_RedMonitorIntegration`.

- **ACTIVE**: [Explicit state machines and lifetime safety](ExplicitStateMachines_2026-11-10.md) — Plan only; code implementation has not started. Internal value-type state machines (`ExclusiveSessionLease`, `WindowShutdownStateMachine`, GPU slot/stream phases, `ScopedTesting`/`TestRunEvidence` locks) with zero `.prism` or `DxUi` ABI changes. Owning specs: `Core_Architecture`, `Core_PerformanceAndResources`, `Document_Tools`, `Rendering_Composition`, `UI_Windowing`, `Testing_Validation`, `Testing_RunEvidence`.

- **RETIRED**: [RedPrism Adversarial Review](AdversarialReview_2026-10-21.md) — as a work queue. Snapshot `97dd25b`, 16 October 2026. Not re-triaged as a patch list. Keep the historical findings. Do not implement from its line numbers. Owning specs: `Core_Architecture`, `Core_PerformanceAndResources`, `Testing_Validation`.

On completion of a plan, merge durable requirements into the owning domain spec, move the plan to
`Specs/Plans/Done/`, and remove its row here. Child phase plans spawned from a parent must be
indexed here before they authorize implementation.
