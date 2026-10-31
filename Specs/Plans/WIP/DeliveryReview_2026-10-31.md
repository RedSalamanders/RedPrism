# Delivery review 2026-10-31

Status: not started
Date: 2026-10-31

This file is a non-normative execution record. It does not override
[`Core_DxUiIntegration.md`](../../Core/Core_DxUiIntegration.md) or any other domain contract.
The review found no product defect to implement in RedPrism. It exists so that conclusion
and the one pin action are not lost.

## Coordinates

The review tip is `origin/main` `e29d1ebd731f24533d703a9b51919c5667a4d7af` (31 Oct 2026).
`ej/unified-ci-20261030` at `96d0fb96` is an ancestor of that tip. At review time its product
C++ matched the tip. Anchors below are that product C++.

The DxUi lock is `a059e07c39c58cbf5218c34f7b9f1a9ac29baf3b`, API revision 3, equal to DxUi
`origin/ej/wall-compat-redprism-20261028`. That commit is not an ancestor of DxUi `origin/main`.
RedPrism cannot take DxUi `origin/main` `f2940cc2` until it adopts API revision 4. A menu fix
that lands only on DxUi `origin/main` does not reach this product. Restore checks out the locked
commit (`Dependencies/DxUi.lock.json`, [`Core_DxUiIntegration.md`](../../Core/Core_DxUiIntegration.md)).

## Work

- [ ] **DX-2** · Medium. The pinned DxUi has no keyboard cancel of menu submenu hover timers.
  That cancel is DxUi `762983a0` (2026-10-28) on DxUi `origin/main` only. On this pin a pending
  400 ms hover timer survives arrow keys and UI Automation focus, and can open the old hover row
  or close a focused child. DxUi `Specs/UI/UI_InputAndAccessibility.md` requires keyboard navigation
  to cancel those timers throughout the popup chain, and a previously queued timer must not replace
  or close a keyboard-focused child.
  **Repair:** port the keyboard cancel and a UI Automation focus cancel onto
  `ej/wall-compat-redprism-20261028`, then re-pin RedPrism to that commit. Keep API revision 3.
  Update the pin sentence in `Core_DxUiIntegration.md` in the same change as
  `Dependencies/DxUi.lock.json`. Do not bump to DxUi `origin/main` as the fix.
  **Acceptance:** hover a submenu, move with the keyboard, deliver `WM_TIMER`; one popup remains
  on the focused row. Repeat for UI Automation focus and a pending close.
  RedPrism's own windowing code was not the defect. `WM_PAINT` (`RedPrism/Application.cpp`)
  invalidates one coalesced frame and validates the rect; it does not draw or Present
  ([`UI_Windowing.md`](../../UI/UI_Windowing.md)). The frame presents once with `Present(1, 0)`
  (`RedPrism/Renderer.cpp`). Native menus use DxUi's modal loop. The product posts no menu-bar hover.

No other work item is open.

## Closed

### RP-1 killed

`UniformTiles` returns an empty tile list when `Raster::Adopt` returns no storage
(`Common/Document.cpp`, the `TryAllocate` / `Adopt` / `HasStorage` sequence around 1128–1134).
`Raster::Replacing` continues (the `UniformTiles` call around 1176–1180). `BrushStroke::PaintSnapshot`
(`Common/BrushStroke.cpp`) and `CarriedMask` (`Common/Document.cpp`) only require `HasStorage()`.

On this call the block is 1–256 pixels on a side and the buffer was just allocated to the matching
size, so `Adopt`'s empty return is `AllocateStorage` / `AllocateChargedStorage` returning null.
That null is the storage serial exhausted (`gRasterStorageSerial.Allocate()`). `MakeSparse` then
also returns empty, so the commit returns no image: `PaintSnapshot` returns `nullopt`, and
`CarriedMask` returns an empty raster. A real allocation failure throws from `std::make_unique`
inside `AllocateStorage` before that null return. `UniformTiles` already throws `std::bad_alloc`
when `PixelBuffer::TryAllocate` fails.

Do not add a second failure path. The successful-allocation padding test is not a corruption hole.

### Checked mechanisms, left standing

- **Zip-slip.** `ReadZipEntry` rejects a null name, `..`, and `\`. `AssetNameMatches` binds an
  asset file name to the layer id plus the literal `.png` or `.mask.png` suffix. Exploded members
  reject a reparse point after the project root ([`IO_ProjectFormat.md`](../../IO/IO_ProjectFormat.md)).
- **Failed save clearing dirty.** `ProjectSaveJob` sets `_published` only from
  `ProjectIoStage::Published`. `Completed` calls `MarkSaved` only when that flag is set and the
  save is still current (`RedPrism/ApplicationProjectSaveJobs.cpp`).
- **`WM_PAINT` drawing.** Paint invalidates one frame and does not Present. See DX-2.
- **A second Present.** The frame path calls `Present(1, 0)` once. `Renderer::ProbeOcclusion`
  calls `Present(0, DXGI_PRESENT_TEST)` only while the swap chain is already occluded. That probe
  is not a second frame.
- **Crash-dump upload.** `Common/CrashDumps.cpp` writes a local minidump. `RedPrism/Main.cpp`
  tells the user nothing is uploaded. [`Core_CrashHandling.md`](../../Core/Core_CrashHandling.md)
  owns that local capture.
- **Golden rebaseline.** Existing legacy goldens stay the numeric expectation. This review found
  none to rebaseline.
- **Ordinary selection input on the new jobs.** The marquee, lasso, and polygonal jobs stay off
  unless `_selectionPressTestEnabled`, `_polygonalDrawingTestEnabled`, or `_polygonalPressTestEnabled`
  is set (`RedPrism/Application.h`). Those default false. `package.ps1` forces
  `RedPrismEnableTests` false for the ZIP. That property compiles `REDPRISM_ENABLE_TESTS`
  (`Directory.Build.props`); it does not set the gesture flags. If delivery later enables those
  routes, that is a separate qualification gate, not a defect found here. The selection work stays
  on [`SelectionWorkers_2026-10-01.md`](SelectionWorkers_2026-10-01.md). Lasso completion stays on
  [`LassoWorkers_2026-10-12.md`](LassoWorkers_2026-10-12.md). This review does not open another plan.

### Checked and left as specified

- The click-selection job. `SelectionOutlineGestureJob` sets `_click` when the gesture ends on the
  original point and was not lost, then close applies the prepared move closure and continues into
  the click command (`RedPrism/SelectionOutlineGestureJob.cpp`). The review left that path as specified.
- `PendingLasso().value()` in the lasso-draft overlay (`RedPrism/Application.cpp`) runs only after
  `PendingLasso().has_value()`.
- An oversized resize is `ResizeStatus::TooLarge`, and the size job reports that as
  `HostOutcome::Failed` (`RedPrism/ApplicationSizeJobs.cpp`). It is not a successful apply.
- The ARM64 `ChargedStorage` tail (`Raster::Storage::arm64AbiTailPadding` in `Common/Document.h`)
  occupies the base tail so deriving through it does not trip MSVC C5291. The charged-size
  assertion stays `sizeof(ChargedStorage) == sizeof(Storage) + sizeof(std::shared_ptr<const void>)`.

## Preserve

These stay as they are when DX-2 lands:

- Zip entry name binding: read rejection of `..` and `\`, and `AssetNameMatches` against the layer id.
- Exploded-save rollback: stage, rotate `dest` to `dest.old`, publish staging to `dest`, and restore
  the backup when publication fails ([`IO_ProjectFormat.md`](../../IO/IO_ProjectFormat.md)).
- `MarkSaved` only after `ProjectIoStage::Published` on a still-current save.
- One frame `Present(1, 0)`. The occlusion probe stays `Present(0, DXGI_PRESENT_TEST)`.
- Local minidumps only.
- Worker GPU recording refusing a non-deferred context
  (`GpuRecordingCommands::InitializeForWork` requires `D3D11_DEVICE_CONTEXT_DEFERRED`).

## Residue

The pre-window body of `RedPrism/CanvasRenderer.cpp` was not reread. The window diff was.
Do not schedule a blind composition rewrite. This residue is not a work item.

## Closeout

DX-2 is the only item. When it lands, move the new API-revision-3 commit into the lock and into
`Core_DxUiIntegration.md`, move this file to `Specs/Plans/Done/`, and remove its index row.
The keyboard-cancel requirement stays in DxUi's input contract. The UI Automation focus cancel
belongs in that same DxUi contract when the port lands. RedPrism does not grow a second menu spec.
