# Active plan index

Files in this directory are non-normative execution or decision records. Current requirements remain in the
owning domain spec under `Specs/<Domain>/` once those contracts exist. Domain contracts created with C0–C7 live under
`Specs/<Domain>/`. This index remains routing only.

Add every direct WIP plan to this table exactly once. Use `ACTIVE` for executable work, `HOLD` for paused work,
`DECISION` when product intent is unresolved, and `RETIRED` for a bounded tombstone.

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
[`PortProgress_2026-09-21.md`](../Done/PortProgress_2026-09-21.md). Tree multi-select and drag-reorder stay on the DxUi HOLD plan `TreeReorder_2026-09-21.md`. Brush measured follow-ups stay in `Specs/TestRuns/Brush_2026-09-21.md`.
The 2026-09-23 whole-codebase review and its fixes are in [`ReviewFixes_2026-09-23.md`](../Done/ReviewFixes_2026-09-23.md).
It covers the data-loss PNG fix, the keyboard binding table, the exit-time crash and self-test flakes, one
color-edit session, the chrome card stack, the pointer state, mask targeting and the chrome-sync performance. Its
deferred rows RF-1003, RF-1004 and RF-1115 belong to `UiNeverBlocks_2026-09-22.md`.

| Plan | Status | Owning domain spec | Purpose |
| --- | --- | --- | --- |
| [`AsyncRasterEdits_2026-10-01.md`](AsyncRasterEdits_2026-10-01.md) | ACTIVE; basic edits, gradients and deferred pixel-move previews/commits qualify in all configurations; opening reuse and atomic private Copy/Cut core publication also qualify; production clipboard, Shape release, settled mask creation and settled Layer via Copy also qualify; detached transform/floating core, cancellation, nested floating and preview core, selected host settlement and deferred preview/writer guards qualify; worker Alt-drag gestures qualify in six clean profiles with the merged DxUi API-revision-3 update; ordinary deferred callers qualify in six clean profiles; broader lifetime, timing, redraw and live producer settlement remain open | `Document_Tools`, `Document_Model`, `Document_History`, `Core_PerformanceAndResources`, `Rendering_Brush`, `UI_ApplicationChrome`, `Testing_Validation` | P1.5 detached raster edits, exact pixels and ordered cancellable application |
| [`HistoryStorageAccounting_2026-09-30.md`](HistoryStorageAccounting_2026-09-30.md) | ACTIVE; D13/selection/moves, prepared raster/gradient/pixel move/color/size/merge/restore/import, immutable history roots, optional image thumbnails, adjustment opening/OK/Cancel and production New/Close qualify; other callers, full record capture, incremental accounting, uploads and apply target open | `Document_History`, `Document_Model`, `Core_PerformanceAndResources`, `Testing_Validation` | P1.9 bounded history storage, safe ownership/restore and measured residual apply costs |
| [`DocumentLifecycleWorkers_2026-10-02.md`](DocumentLifecycleWorkers_2026-10-02.md) | ACTIVE; optional New/Close publication primitive and production New/New Canvas/Close qualify in six clean profiles, with 65 primitive and 258 actual host controls; broader live contexts and timing remain open | `Document_Model`, `Document_History`, `Core_PerformanceAndResources`, `Testing_Validation` | P1.9 H1 lifecycle callers; preserve saved/undo semantics and qualify faults/order/lifetime before native activation |
| [`ShutdownCpuRetirement_2026-10-02.md`](ShutdownCpuRetirement_2026-10-02.md) | ACTIVE; 28 retirement/26 hook/34 suspended controls qualify in six clean profiles; sampling/overlay/benchmark retirement and 52 actual Application controls qualify; 390 acquired-prefix initialization controls qualify in six forced-rebuild profiles; unintercepted API failures, lease/live-draft coverage and timing remain open | `Core_Architecture`, `Core_PerformanceAndResources`, `Testing_Validation` | P1.7 F5d: join producers, retire CPU owners on the existing MTA pool, acknowledge before teardown |
| [`SelectionWorkers_2026-10-01.md`](SelectionWorkers_2026-10-01.md) | ACTIVE; Select All/Deselect and outline-move closure core pass 312 assertions per x64 suite and all ARM64 clean cross-builds; production host/gesture routing and general geometry bounds remain open | `Document_Tools`, `Document_Model`, `Document_History`, `Core_PerformanceAndResources`, `Rendering_Composition`, `Testing_Validation` | P1.6 ordered geometry/history publication, gesture/source migration and bounded coverage/display caches |
| [`AsyncFileProcessing_2026-09-30.md`](AsyncFileProcessing_2026-09-30.md) | ACTIVE; classification, codecs, project streams, save/export, open/import graphs and clipboard byte/sealed native transfer/copy preparation/receipt binding/capped memory encoding/shared retry routing/save-recovery foundations and native recovery timer resource faults qualified in all configurations; 24-row hardware 4K/45 MP/100 MP and WARP 4K measurements recorded; production clipboard qualifies; F5d, F4/F6 and measured overruns remain open | `IO_ImageCodecs`, `IO_ProjectFormat`, `Core_PerformanceAndResources`, `UI_ApplicationChrome`, `Testing_Validation` | P1.7 worker classification, open/import/save/export and clipboard graphs with acknowledged cancellation |
| [`AsyncDocumentSizing_2026-09-30.md`](AsyncDocumentSizing_2026-09-30.md) | ACTIVE; functional matrix and 4K/45 MP/WARP runs qualified; 2 ms apply target remains open | `Document_Tools`, `Document_Model`, `Core_PerformanceAndResources`, `Testing_Validation` | P1.8 immutable Image Size/Canvas Size jobs, copied drafts, cancellation and ordered apply |
| [`GpuCompletionReadback_2026-09-30.md`](GpuCompletionReadback_2026-09-30.md) | ACTIVE; foundation, utility composition, explicit fallback, Merge/opening callers and bounded rebind restart qualified on x64 and ARM64 cross-builds; remaining callers and G1 open | `Rendering_Composition`, `Core_Architecture`, `Core_PerformanceAndResources`, `Testing_Validation` | P1.3 resumable jobs, event-driven GPU completion, bounded readback, utility composition and caller migration |
| [`UiNeverBlocks_2026-09-22.md`](UiNeverBlocks_2026-09-22.md) | ACTIVE; P0.1 driver/corpus gates, P0.2–P0.8, P1.1a–b and P1.1c.1–2 complete on x64; P1.1c.3 production continuations qualified on all x64 configurations and ARM64 cross-builds; remaining P1–P6 work tracked in the opening checklist | `Core_PerformanceAndResources`, `Core_Architecture`, `Core_CrashHandling`, `Document_*`, `Rendering_*`, `IO_*`, `Build_*`, `Testing_Validation`, `Testing_Benchmarks`, `UI_*`; new `IO_RawDecode`, `Rendering_RawDevelop` | Implement the UI-thread, exact-kernel and RAW performance plan; measured foundation work precedes the nonblocking, SIMD/GPU and Photoshop phase exits |
| [`DxUiFollowUps_2026-10-01.md`](DxUiFollowUps_2026-10-01.md) | ACTIVE; pin `271bd54` (API revision 3), the host-message rule, the vcpkg toolset pin and the sparse long-path restore qualified; Tree multi-select (DxUi #35) and the clipboard-dependent x64 checks at the new pin remain open | `Core_DxUiIntegration`, `Build_Toolchain` | Update the DxUi pin (#29 renames, Tree multi-select from `TreeReorder_2026-09-21`), forward registered DxUi messages, pin this repository's vcpkg restore to the default MSVC toolset, and restore the pin with Git long paths or a sparse checkout |

On completion of a plan, merge durable requirements into the owning domain spec, move the plan to
`Specs/Plans/Done/`, and remove its row here. Child phase plans spawned from the master program must be
indexed here before they authorize implementation.
