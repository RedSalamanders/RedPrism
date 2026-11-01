# Active plan index

Files in this directory are non-normative execution or decision records. Current requirements stay in the owning domain spec under `Specs/<Domain>/`. This index is routing only.

Use `ACTIVE` for executable work, `HOLD` for paused work, `DECISION` when product intent is unresolved, and `RETIRED` for a bounded tombstone. Add every direct plan to the table exactly once.

P1 and every child in the HOLD rows are paused since 21 October 2026. Resume only from [`UiNeverBlocks_2026-09-22.md`](UiNeverBlocks_2026-09-22.md). The October checkpoint narrative that used to open this file lives in that plan. `P1MainIntegration_2026-10-21/*.json` is evidence referenced from that plan and from [`P1MainIntegration_2026-10-21.md`](../../TestRuns/P1MainIntegration_2026-10-21.md). It is not a plan.

Re-checked against `main` `e79a83a` on 1 November 2026. `package.ps1` calls `Get-DxUiSourcePath`. `.github/workflows/ci.yml` triggers `push` to `main`. The busy string, the Inverse and Wand admission ceilings, and the format-7 writer are still the code. Correction notes are in the MergeReview, RedTeam, and DeliveryReview files. Line numbers in [`AdversarialReview.md`](AdversarialReview.md) are the 16 October snapshot.

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

| Plan | Status | Owning domain spec | Purpose |
| --- | --- | --- | --- |
| [`MergeReview_2026-10-28.md`](MergeReview_2026-10-28.md) | DECISION. Q1–Q15 open; CI-10 / Q16 resolved against pinned engine `19710a8a` and `Core_Changes.md`. CI-8's packaging join is fixed; its parity-test skip and comment remain | `Core_Architecture`, `Core_PerformanceAndResources`, `Core_CrashHandling`, `Core_Changes`, `Document_Tools`, `Rendering_Composition`, `UI_ApplicationChrome`, `UI_Windowing`, `Build_Toolchain`, `Build_Packaging`, `Testing_Validation` | Findings owner for the live product. Do not copy its rows into another plan |
| [`RedTeamDelivery_2026-10-31.md`](RedTeamDelivery_2026-10-31.md) | DECISION. WP0–WP6 sequence the open MergeReview work. CI-2, CI-3, and the ZIP-blocker reading of CI-8 are not open | Same contracts as MergeReview, plus `Core_DxUiIntegration` and `IO_ProjectFormat` | Delivery sequence. Tick MergeReview when a row closes; do not keep a second copy |
| [`DeliveryReview_2026-10-31.md`](DeliveryReview_2026-10-31.md) | ACTIVE. DX-2 has not started. This file does not say the product has no other defect | `Core_DxUiIntegration` | Port the menu hover-timer cancel onto the API-revision-3 DxUi branch and re-pin. Keep API revision 3 |
| [`DxUiFollowUps_2026-10-01.md`](DxUiFollowUps_2026-10-01.md) | DECISION. Lock is `a059e07c`, API revision 3. Tree multi-select (DxUi #35) was not re-checked inside DxUi | `Core_DxUiIntegration`, `Build_Toolchain` | Decide where the editor uses Tree multi-select. The `271bd54` pin step is historical |
| [`UiNeverBlocks_2026-09-22.md`](UiNeverBlocks_2026-09-22.md) | HOLD since 21 October 2026. Nine delivery gates in that file remain open. The saved continuation names another machine and DxUi pin `8e899407`; the lock on this main is `a059e07c` | `Core_PerformanceAndResources`, `Core_Architecture`, `Document_*`, `Rendering_*`, `IO_*`, `UI_*`, `Testing_Validation` | Responsive editor milestone. Do not resume from the saved machine paths |
| [`SelectionWorkers_2026-10-01.md`](SelectionWorkers_2026-10-01.md) | HOLD with P1 | `Document_Tools`, `Document_Model`, `Document_History`, `Core_PerformanceAndResources`, `Rendering_Composition`, `Testing_Validation` | P1.6 selection workers. Ordinary activation stays off |
| [`LassoWorkers_2026-10-12.md`](LassoWorkers_2026-10-12.md) | HOLD with P1 | `Document_Tools`, `Document_Model`, `Document_History`, `Core_PerformanceAndResources`, `UI_ApplicationChrome`, `Rendering_Composition`, `Testing_Validation` | P1 lasso input, draft, and completion |
| [`AsyncRasterEdits_2026-10-01.md`](AsyncRasterEdits_2026-10-01.md) | HOLD with P1 | `Document_Tools`, `Document_Model`, `Document_History`, `Core_PerformanceAndResources`, `Rendering_Brush`, `UI_ApplicationChrome`, `Testing_Validation` | P1.5 detached raster edits |
| [`HistoryStorageAccounting_2026-09-30.md`](HistoryStorageAccounting_2026-09-30.md) | HOLD with P1 | `Document_History`, `Document_Model`, `Core_PerformanceAndResources`, `Testing_Validation` | P1.9 history storage and ownership |
| [`IncrementalHistoryStorage_2026-10-12.md`](IncrementalHistoryStorage_2026-10-12.md) | HOLD with P1 | `Document_History`, `Document_Model`, `Core_PerformanceAndResources`, `Testing_Validation` | P1.9 incremental history accounting |
| [`DocumentLifecycleWorkers_2026-10-02.md`](DocumentLifecycleWorkers_2026-10-02.md) | HOLD with P1 | `Document_Model`, `Document_History`, `Core_PerformanceAndResources`, `Testing_Validation` | P1.9 document lifecycle workers |
| [`ShutdownCpuRetirement_2026-10-02.md`](ShutdownCpuRetirement_2026-10-02.md) | HOLD with P1 | `Core_Architecture`, `Core_PerformanceAndResources`, `Testing_Validation` | P1.7 shutdown CPU retirement |
| [`AsyncFileProcessing_2026-09-30.md`](AsyncFileProcessing_2026-09-30.md) | HOLD with P1 | `IO_ImageCodecs`, `IO_ProjectFormat`, `Core_PerformanceAndResources`, `UI_ApplicationChrome`, `Testing_Validation` | P1.7 file and clipboard workers |
| [`AsyncDocumentSizing_2026-09-30.md`](AsyncDocumentSizing_2026-09-30.md) | HOLD with P1 | `Document_Tools`, `Document_Model`, `Core_PerformanceAndResources`, `Testing_Validation` | P1.8 Image Size and Canvas Size. The 2 ms apply target stays open |
| [`GpuCompletionReadback_2026-09-30.md`](GpuCompletionReadback_2026-09-30.md) | HOLD with P1 | `Rendering_Composition`, `Core_Architecture`, `Core_PerformanceAndResources`, `Testing_Validation` | P1.3 GPU completion and readback |
| [`SamplerReadback_2026-10-04.md`](SamplerReadback_2026-10-04.md) | HOLD with P1 | `Rendering_Composition`, `Core_PerformanceAndResources`, `Testing_Validation` | P1.3 canvas color sampler |
| [`WandCompositeReadback_2026-10-04.md`](WandCompositeReadback_2026-10-04.md) | HOLD with P1 | `Rendering_Composition`, `Document_Tools`, `Core_PerformanceAndResources`, `Testing_Validation` | P1.3 All Layers wand readback |
| [`DeferredCanvasInput_2026-10-04.md`](DeferredCanvasInput_2026-10-04.md) | HOLD with P1 | `Core_PerformanceAndResources`, `UI_ApplicationChrome`, `Document_Tools`, `Rendering_Brush`, `Testing_Validation` | P1.9 deferred canvas input |
| [`DeferredCanvasLifetime_2026-10-04.md`](DeferredCanvasLifetime_2026-10-04.md) | HOLD with P1 | `Document_History`, `UI_ApplicationChrome`, `UI_Windowing`, `Core_PerformanceAndResources`, `Testing_Validation` | P1.9 close, new, and shutdown preflight |
| [`DeferredColorDisplay_2026-10-04.md`](DeferredColorDisplay_2026-10-04.md) | HOLD with P1 | `Document_Model`, `Document_Tools`, `Document_History`, `Core_PerformanceAndResources`, `Testing_Validation` | P1.9 deferred color display |
| [`LayerRecordOwnershipBounds_2026-10-04.md`](LayerRecordOwnershipBounds_2026-10-04.md) | HOLD with P1 | `Document_Model`, `Document_History`, `Core_PerformanceAndResources`, `Testing_Validation` | P1.9 cached layer-record bounds |
| [`BoundedRasterUploads_2026-10-05.md`](BoundedRasterUploads_2026-10-05.md) | HOLD with P1 | `Rendering_Composition`, `Core_Architecture`, `Core_PerformanceAndResources`, `UI_Windowing`, `Document_Model`, `Testing_Validation` | P1.9 bounded raster uploads |
| [`OpacityHostCandidate_2026-10-04.md`](OpacityHostCandidate_2026-10-04.md) | HOLD with P1. Experimental checkpoint, not a release step | `Document_Model`, `Document_History`, `Core_PerformanceAndResources`, `Testing_Validation` | Private opacity-host candidate. Overlaps the deferred-canvas plans |
| [`ImageProcessingAccelerationAndRaw_2026-10-04.md`](ImageProcessingAccelerationAndRaw_2026-10-04.md) | HOLD until P1 qualifies. Decode, develop, and Photoshop comparison stay open. The RAW corpus is acquired | `Core_PerformanceAndResources`, `Document_*`, `Rendering_*`, `IO_*`, `Testing_*`, `UI_*` | P2–P6 acceleration, RAW, and high bit depth. Do not start from this file |
| [Scoped testing](ScopedTesting_2026-10-05.md) | ACTIVE closeout. The original eight-box sequence is superseded. Native and CI qualification on the resumed checklist remain | `Testing_Validation` | Affected scopes, exact evidence, and PrePush delegation |
| [`NativeBuildPolicyCoherence_2026-10-28.md`](NativeBuildPolicyCoherence_2026-10-28.md) | ACTIVE closeout. Policy is merged. Changelog fold and final gates remain. Evidence paths are machine-local | `Build_Toolchain`, `Build_ConsoleAndPaths` | Shared /Wall policy, terminal results, and 16-hex DxUi folders |
| [`Msvc1452ReleaseOptimization_2026-10-23.md`](Msvc1452ReleaseOptimization_2026-10-23.md) | ACTIVE closeout. Local six-build and three Full runs are recorded. Size comparison and the fold remain | `Build_Toolchain` | MSVC 14.52 and Release optimization measurements |
| [`RedMonitor_Adoption_2026-10-23.md`](RedMonitor_Adoption_2026-10-23.md) | ACTIVE. The adapter is in. Native ARM64 runtime, interactive, and paired-performance gates remain | `Core_RedMonitorIntegration` | Optional diagnostic client. Those gates are shared, not a second feature |
| [`AdversarialReview.md`](AdversarialReview.md) | RETIRED as a work queue. Snapshot `97dd25b`, 16 October 2026. Not re-triaged as a patch list | `Core_Architecture`, `Core_PerformanceAndResources`, `Testing_Validation` | Keep the historical findings. Do not implement from its line numbers |

On completion of a plan, merge durable requirements into the owning domain spec, move the plan to
`Specs/Plans/Done/`, and remove its row here. Child phase plans spawned from a parent must be
indexed here before they authorize implementation.
