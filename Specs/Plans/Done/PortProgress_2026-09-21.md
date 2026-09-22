# Continue the Windows port past the Done checkboxes

> **Executor instructions**: This plan is the next program after C0–C16. Do the slices in order. Finish the
> code, the tests, and the named domain spec in the same slice. If a STOP condition fires, stop and report.
> Do not compile or edit archived behavioral reference. Do not add a vcpkg dependency. Do not implement a second control toolkit
> inside RedPrism. A DxUi change is a plan in `Z:\src\DxUi`, then a pin bump here, never a checkout of DxUi
> `main` from this repo.
>
> **Drift check (run first)**:
> ```powershell
> git -C Z:\src\RedPrism rev-parse --short HEAD
> git -C Z:\src\RedPrism status --short
> ```
> Planned at `30ccf13`. If `Common/Document.cpp` `Undo`/`Redo`, `Common/BrushStroke.cpp` `CutSelection` /
> `BeginSelectionTransform`, `RedPrism/ApplicationFiles.cpp` `ConfirmDiscardChanges` / `ReplaceDocument`, or
> `Common/ProjectStore.cpp` `LoadExploded` no longer match the excerpts below, re-read them before editing.

- **Status**: DONE (2026-09-21). Slices 0–4, 7, and 8 landed. Slices 5 and 6 are HOLD with named owners.
- **Priority**: P0
- **Effort**: L (slices are S or M except the brush GPU compose and the DxUi Tree work)
- **Risk**: MED (slice 6 and slice 7 are HIGH if started early)
- **Depends on**: C16 [`Completeness_2026-09-21.md`](../Done/Completeness_2026-09-21.md)
- **Planned at**: 2026-09-21, RedPrism `30ccf13` (main, 12 commits ahead of `origin/main`)
- **Owning specs**: `Document/Document_History.md`, `Document/Document_Tools.md`, `IO/IO_ProjectFormat.md`,
  `UI/UI_ApplicationChrome.md`, `Rendering/Rendering_Brush.md`, `Core/Core_PerformanceAndResources.md`,
  `Testing/Testing_Validation.md`, `Build/Build_Toolchain.md`

## Why the master plan is not the end

[`WindowsDirectXPort_2026-09-20.md`](../Done/WindowsDirectXPort_2026-09-20.md) is a historical program index.
Its own done rule was “C0–C7 shipped and C8–C15 exist as child plans,” and those children plus C16 are under
`Specs/Plans/Done/`. That checkbox does **not** mean every legacy scenario is implemented, tested, and
correct. The file’s opening still describes an empty repository. Its C3 closeout still says the DxUi pin did
not move; the pin is now `7e3be94` and the shell uses Splitter, ColorPicker, and NumericStepper. Do not
“finish” the port by editing that Done file back to ACTIVE. Record new behavior here and in the domain specs.

What is already true, and must not be rebuilt:

- One Win32 HWND, one D3D11 device, one flip-model swap chain, DxUi `EmbeddedHost`, no GDI chrome, no plugin ABI.
- `.prism` ZIP and exploded load/save, WIC import/export, history budget, blend goldens, brush/retouch/adjust/
  selection/transform/crop/canvas-size/image-size/merge/rename, settings, local crash dumps, portable ZIP.
- vcpkg is wil, yyjson, libzip (`default-features: false`), clipper2. Baseline
  `9e593bb18ea69cc5095e012465dcd675a822ed0d`, the same commit as DxUi and RedXe. RedSalamander’s newer
  baseline and its 7zip, sqlite, libraw, libjpeg-turbo, pe-parse, curl, webview2, ftxui, and aws-sdk-cpp
  packages are that product’s. They are not this one. Image I/O stays WIC (decision A7). Remove Background
  stays rejected for v1, so `onnxruntime` stays out (decision recorded in `SmartOps_2026-09-20.md`).

What is not true:

- Undo and Redo do not treat a live brush, warp, pixel-move, or crop the way Undo already treats a gradient.
- Cut can copy a selection and leave the pixels in place.
- Save-before-replace looks at history before it settles a live stroke, so the stroke is missing from the
  save and then thrown away.
- An exploded project rejects a reparse point only on the final file, not on a parent junction.
- A selection float continues when the clear does not paint.
- The layers Tree cannot multi-select or drag-reorder. DxUi `Tree` exposes selection, invoke, expand, and
  context menu only (`Z:\src\DxUi\include\DxUi\DxUi.h`, `IDxTreeDelegate`). `TabControl` and `Grid` already
  drag-reorder; `Tree` does not. That is the DxUi enhancement this product still needs.
- The user guide still says Windows 10 is enough and that filters are a future phase.
- Several failure paths have no test: truncated `.prism`, device-loss recovery, the session bugs above.

## Current state

`DocumentSession::Undo` cancels a color edit (by returning), a gradient, and a transform. It does not cancel
`_brush`, `_warp`, `_pixelMove`, or `_cropRect`. `Redo` cancels a transform and then restores history even
when `_gradient` is set (`Common/Document.cpp` `Undo` at 1141 and `Redo` at 1167). Ctrl+Z and Ctrl+Y reach
those functions from `Application::HandleAccelerator` with no stroke guard.

`DocumentSession::CutSelection` (`Common/BrushStroke.cpp` 3419) calls `CopySelection()` then
`ClearSelectedPixels()` and ignores a clear that returned without painting. `CanCopyPixels` does not require
`CanPaint`. `CanPaint` is false while a color panel, stroke, or transform is active, and while the layer is
effectively hidden.

`Application::NewDocument` does call `ConfirmDiscardChanges`, then `SettleInteractions(false)`, then
`ClearDocument`. `ReplaceDocument` settles, then assigns the new document. The hole is the order inside
`ConfirmDiscardChanges` (`RedPrism/ApplicationFiles.cpp` 167): it reads `history.IsModified()` and may
`SaveDocument` **before** `SettleInteractions`. A stroke still in `_brush` is not a history entry yet, so a
clean document plus a live stroke is replaced with no prompt, and a dirty document is saved without that
stroke. `FinishBrush` then commits it and `ClearDocument` drops it.

`BeginSelectionTransform` (`Common/BrushStroke.cpp` 3010) calls `ClearSelectedPixels()` and still inserts the
floating layer when that call paints nothing.

`LoadExploded` (`Common/ProjectStore.cpp` 1279) accepts a path when `PathIsInside(root, FullPath(...))` and
`HasReparse` is false on the leaf. `GetFullPathNameW` does not expand a directory junction. Zip loads are a
different path and already reject `..`.

`ClearDocument` (`Common/Document.cpp` 1755) does clear brush, warp, gradient, crop, and color edits. Do not
“fix” New by clearing twice. Fix the commands that skip it.

Commands (from `AGENTS.md`; do not invent others):

| Purpose | Command | Expected |
| --- | --- | --- |
| Build | `.\build.ps1 -Platform x64 -Configuration Debug` | exit 0, `.build\x64\Debug\RedPrism.exe` |
| Test | `.\test.ps1 -Platform x64 -Configuration Debug` | exit 0 |
| Test | `.\test.ps1 -Platform x64 -Configuration Release` | exit 0 |
| Test | `.\test.ps1 -Platform x64 -Configuration "ASan Debug"` | exit 0 |
| ARM64 | `.\build.ps1 -Platform ARM64 -Configuration Debug` | exit 0 (build only) |
| Format | `.\format.ps1 -Check` | exit 0 |
| Skills | `.\validate-skills.ps1` | exit 0 |

A running `RedPrism.exe` whose path is the selected output blocks the build. Do not terminate it.

## Slices

### Slice 0 — Confirm the tree that C16 called green

Run the three x64 `test.ps1` configurations and `format.ps1 -Check`. This review did not re-run them.

**STOP** if the suite is red for a reason this plan does not name. Fix only a failure you can tie to a
slice below, after that slice’s code exists. Do not rebaseline a golden.

### Slice 1 — One rule for a live edit

Pending work that is not yet a history entry is cancelled by the first Undo, the same way a gradient is.
Redo uses the same cancel. The composition stroke is dropped in the same call (`Application` Undo/Redo
commands must end the GPU stroke, not only the session).

Cover, with a DocumentTests case and a `--self-test` case each:

- Undo during a brush stroke cancels the stroke, leaves the previous history entry, and a later mouse-up
  does not paint.
- Redo during a gradient cancels the gradient and does not restore under it.
- The same cancel for warp, pixel-move, and a crop frame.
- Cut on a hidden layer, and Cut while a color panel is open, leaves the clipboard unchanged and the pixels
  in place. Cut on a paintable layer still clears and publishes one clipboard.
- `BeginSelectionTransform` restores the pre-edit document when `ClearSelectedPixels` does not paint.
- `ConfirmDiscardChanges` settles first. A live stroke on an otherwise clean document counts as modified.
  A save writes the committed stroke. Discard drops it without a history entry left behind.

Specs: `Document_History.md` (undo/redo vs pending sessions), `Document_Tools.md` (cut and float),
`UI_ApplicationChrome.md` only if a command’s visible result changes.

**Verify**: `.\test.ps1 -Platform x64 -Configuration Debug` exit 0, including the new cases.

### Slice 2 — Exploded projects do not follow a junction out of the tree

Before opening any exploded path, walk each component from the project root to the file and reject
`FILE_ATTRIBUTE_REPARSE_POINT` on any component. Fail with `ProjectIoStatus::Invalid`. Do not change the
zip path. Add an IoTests case that builds a junction under `images\` only when the test root allows it;
if the test host cannot create a junction, the test skips with an explicit recorded reason and the
lexical walk still has a unit test against a fake attribute check. Do not document a recipe for planting
a junction at a user’s folder.

Spec: `IO_ProjectFormat.md` — the symlink rule applies to every ancestor, not only the leaf.

**Verify**: `.\test.ps1 -Platform x64 -Configuration "ASan Debug"` with IoTests green.

### Slice 3 — Tests for paths that already fail closed

No production behavior change unless a test proves the status code is wrong.

- IoTests: truncated bytes, an empty zip, and a zip whose PNG member reads short. Expect
  `Invalid`, `MissingImage`, or `TooLarge` as `IO_ProjectFormat.md` already defines, and no partial
  document.
- A host or composition seam that forces `Renderer::RecoverDevice` and then asserts the composition and
  the brush engine are rebound and a compose succeeds. Do not pull the real GPU out from under the
  process in CI.

**Verify**: the new tests fail if the status mapping or the rebind is removed, and pass on
`.\test.ps1 -Platform x64 -Configuration Debug`.

### Slice 4 — The user guide matches the specs

`docs/README.md` line 19 says Windows 10 supplies the rest. `AGENTS.md` and `Build_Toolchain.md` require
Windows 11 build 22000.2600. `docs/README.md` line 107 says filters are later phases. C12 is Done and the
same file already describes the Filter menu. Rewrite those two sentences. Do not change product behavior.

**Verify**: `rg -n "Windows 10|later phases" docs\README.md` returns no matches.

### Slice 5 — DxUi Tree can multi-select and reorder

This is the library enhancement. Do it in `Z:\src\DxUi` under a new indexed WIP plan there
(`control-development` skill, catalog, interaction tests, gallery). The contract to add, matching
`TabControl` / `Grid` drag-reorder rather than a new gesture language:

- `IDxTreeDelegate` gains a reorder callback (source id, insert-before id, parent id) and a
  multi-select change callback.
- Pointer drag previews a row insertion; release commits once; Escape or capture loss cancels.
- Modifier click extends the selection. The model stays UI-thread only.
- Tests live in DxUi `Tests/Controls`. Gallery tile updated. `test.ps1` Debug, Release, and ASan Debug
  in DxUi, plus the ARM64 build.

Then, in RedPrism, bump `Dependencies/DxUi.lock.json` only to the DxUi commit whose gates are green.
Wire the layers Tree: several selected rows, drag-reorder within a folder, Group Selected Layers and
Merge Layers using that selection (the session plan already accepts several ids; the panel currently
selects one). Spec: `UI_ApplicationChrome.md` replaces “Tree has no drag API.”

**STOP** if the DxUi pin move would require editing `LocalizedAdaptiveLayout` files or resetting
`Z:\src\DxUi`. **STOP** if the reorder gesture cannot be expressed on `IDxTreeModel` without giving
the library a document type.

**Out of scope for this slice**: project tabs. One document per window stays the recorded C16 deviation.

### Slice 6 — Brush cost, one measured step at a time

`Specs/TestRuns/Brush_2026-09-21.md` already measured an 800 px tip at about four times the legacy
per-update cost. The follow-ups, in that file’s order, are the only ones authorized:

1. Regenerate mips for the dirty region, or once per present, not on every `UpdateStrokeTiles`
   (`RedPrism/CanvasRenderer.cpp` `GenerateMips` after `WriteTiles`).
2. Read the preview back on the next frame instead of `Map` inside `GpuBrushCoverage::Render`.
3. Compose the tile in reusable storage (`BrushStroke::ComposeTile` allocates every update).
4. GPU tile compose. This one needs goldens and a WIP note in `Rendering_Brush.md` before coding.
   Do not start it in the same change as 1–3.

Each step archives a new `Specs/TestRuns/` row from `RedPrism.exe --bench brush` on the same
configuration as the 2026-09-21 run. A step that is not faster is reverted.

Blur/Clone stroke begin still builds a full-canvas sample (`DocumentSession::BlurSample`). That is a
separate change after the paint path, same evidence rule.

### Slice 7 — Marching ants move without waking a settled window

`UI_ApplicationChrome.md` says the dash is static until an animation timer exists. Drive the phase from
the existing animation tick. Rebuild stroke style only. A settled document with no selection, and any
occluded or hidden window, still blocks on the queue (`Core_PerformanceAndResources.md`). Self-test
asserts the phase advances while a selection exists and that a second frame of a settled document with
no selection still composites zero times.

### Slice 8 — Package the result

After slices 1–5 (6 and 7 if they landed), run `.\package.ps1 -Platform x64` and record the stamp in
`Build_Packaging.md` only if the packaging contract changed. C16 named this follow-on and did not do it.

## Test plan

Pattern: `Tests/DocumentTests` for session rules (see the existing undo and cut cases), `Tests/IoTests`
for project bytes, `RedPrism.exe --self-test --warp` for the shell path. Assertions are numeric or
history-name checks, never a screenshot comparison. Harness screenshots only when a slice changes
chrome the user sees (slice 5). Capture with `--screenshot`, not a desktop tool.

## Done criteria

- [x] Slices 0–4 are green on x64 Debug, Release, and ASan Debug, and ARM64 Debug builds.
- [x] `Document_History.md`, `Document_Tools.md`, and `IO_ProjectFormat.md` describe the new rules.
- [x] `docs/README.md` no longer claims Windows 10 or that filters are unfinished.
- [x] Slice 5 is HOLD. Owner: `Z:\src\DxUi\Specs\Plans\WIP\TreeReorder_2026-09-21.md`.
- [x] Slice 6 is HOLD. Owner: `Specs/TestRuns/Brush_2026-09-21.md`. Slice 7 landed. Slice 8 packaged
      `RedPrism-0.1.52-x64.zip` and its clean-extraction smoke passed.
- [x] No vcpkg dependency was added.
- [x] This file moves to `Specs/Plans/Done/`.

## STOP

- Rebaselining a legacy golden to make a test pass.
- Adding `onnxruntime`, `libjpeg-turbo`, `libpng`, `libraw`, `curl`, `sqlite3`, `7zip`, `webview2`,
  `ftxui`, `aws-sdk-cpp`, or `pe-parse`.
- A second D3D device, D3D12, CMake, a plugin ABI, or GDI text.
- Implementing Tree drag inside RedPrism because the DxUi pin lacks it.
- Killing `RedPrism.exe`. Committing `.build/`.
- Editing archived behavioral reference.
- Treating a same-user `pending.txt` quote check as this plan’s security slice. The crash folder is the
  user’s profile. Slice 2 is the project-load boundary.

## Considered and not in this plan

- `BeginStroke` ignored HRESULT. `BeginStroke` succeeds for any positive size. The CPU stroke is the
  commit path.
- Full-resolution Levels preview under 4096 px. The cap is documented as a maximum, not a bug.
- Project tabs and dragging layers between projects. C16 recorded one document per window.
- Remove Background. Rejected for v1 with a written decision.
- Rewriting the Done master plan so its old “repository is empty” paragraphs disappear. It is history.

## Closeout (2026-09-21)

Slices 0–4, 7, and 8 landed. x64 Debug, Release, and ASan Debug `test.ps1` passed, ARM64 Debug built, and
`package.ps1 -Platform x64 -SkipBuild` wrote `RedPrism-0.1.52-x64.zip` whose clean-extraction smoke passed
(`--help`, `--self-test --warp`).

- Pending edits: `DocumentSession::CancelPendingEdit` drops a gradient, transform, brush, warp, pixel move, shape draft, or crop frame on the first Undo and the first Redo, and history does not move. The host drops the composition stroke in the same command. `ConfirmDiscardChanges` settles a live stroke before it reads `IsModified`. `CutSelection` does not publish the clipboard unless the clear paints. `BeginSelectionTransform` restores the document when the clear paints nothing.
- Tests: `DocumentTests` `pending.*` (all passed), WARP self-test Ctrl+Z during a stroke, the save prompt committing that stroke, marching ants leaving `CompositeCount` unchanged, and `RecoverDeviceForTest` presenting a later frame.
- Exploded loads reject a reparse point on every component after the project root (`ExplodedMemberPathAllowed`). IoTests cover the walk, a real directory symlink (`path.junction-load`), and empty, garbage, and truncated `.prism` files.
- The guide states Windows 11 build 22000.2600 and no longer calls filters a future phase.
- Device recovery waits out an outstanding Present and gives chrome the new device before `CreateSwapChainForHwnd`. Occlusion registration failure does not fail the swap chain.
- Marching ants advance on a 400 ms cycle while a non-empty outline is showing. Hidden, unpowered, and occluded windows still wait.

HOLD, with owners:

- **Slice 5** — DxUi `Tree` multi-select and drag-reorder. Owner: `Z:\src\DxUi\Specs\Plans\WIP\TreeReorder_2026-09-21.md`. RedPrism does not ship a second tree. The layers panel keeps keyboard reorder until that plan lands and the pin moves.
- **Slice 6** — Brush measured follow-ups (mips, deferred readback, reusable tile compose, then GPU compose). Owner: `Specs/TestRuns/Brush_2026-09-21.md` and `Rendering_Brush.md`. They stay out of this closeout because the GPU step is a golden-risk change the plan forbids mixing with the session fixes.

No vcpkg dependency was added.
