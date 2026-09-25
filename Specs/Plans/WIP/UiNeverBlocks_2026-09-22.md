# The UI thread never waits on a filter or any other action

- **Status**: ACTIVE
- **Priority**: P0
- **Planned at**: 2026-09-22, after the splitter drag fix
- **Reviewed**: 2026-09-25 against `0035f1b` and the legacy oracle. This revision replaces the first draft; the
  corrections are listed under "What the review changed".
- **Owning specs**: `Core/Core_PerformanceAndResources.md` (the rule, the budget row), `Core/Core_Architecture.md`
  (worker ownership, the completion message, the wait set), `Core/Core_CrashHandling.md` (worker threads),
  `Document/Document_Tools.md` (split edits, busy, committing, stroke sources), `Document/Document_History.md`
  (saving a captured revision), `UI/UI_ApplicationChrome.md` (busy look, status, panel, files),
  `Rendering/Rendering_Composition.md` (asynchronous readback), `IO/IO_ProjectFormat.md` (asynchronous save),
  `Testing/Testing_Validation.md` (new gates)
- **Legacy oracle**: `Document/Filters.swift`, `Levels.swift`, `HueSaturation.swift`, `AdjustmentEditing.swift`
  (preview and commit tasks), `EditorSession.swift` (`isProjectBusy`, `showsBusy`, `busyIndicatorDelay`,
  `canEditLayers`, `canUseHistory`, `canStartProjectOperation`, `waitForFileRequest`), `EditorSession+Brush.swift`
  and `BrushStroke.swift` (`BrushCommit`), `SelectionEdits.swift`, `MagicWand.swift`, `IO/ProjectController.swift`,
  `IO/ImageResizer.swift`, `IO/CanvasResizer.swift`, `IO/ProjectStore.swift`, `IO/ImageExporter.swift`,
  `IO/ImageImporter.swift`

## Why

A Gaussian blur, a levels commit, a content-aware fill, a merge, an image resize or a project save runs to completion
on the UI thread before the window can paint or cancel. The pump is stuck for the whole kernel. At radius 250 on a
4K layer, the blur alone is about 2 × 10¹¹ multiply-adds (estimated: minutes). The performance contract forbids
busy-polling and requires idle windows to block on events. It does not yet say that a command must return to the pump
within a frame.

Modal Windows loops stay as they are: `IFileDialog`, `TaskDialogIndirect`, `MessageBoxW`, native popup menus and the
caption's move/size loop. Everything RedPrism computes itself must not hold the pump.

Today RedPrism starts no thread at all (no `std::thread`, atomic, mutex or thread pool in `Common/` or `RedPrism/`),
so this plan also introduces the first shared-memory concurrency in the process.

## What the review changed

1. **Previews coalesce; they are never cancelled by a newer value.** The first draft had "a newer job cancels the
   older one". The oracle tried that and reverted it: "Cancelling instead starved the preview during a drag,
   because slider changes arrive faster than a render completes" (`HueSaturation.swift`, `updateHueSaturation`).
2. **The split lives in `DocumentSession`, not in the host.** `DocumentTests` call `CommitFilter`, `CommitLevels`
   and `FinishBrush` about 30 times and expect the entry to exist on return. Each edit becomes Prepare (UI) → Run
   (any thread) → Apply (UI), and the synchronous call stays as `Apply(Run(Prepare()))`. That keeps the tests as
   they are and makes "the worker result equals the direct call" hold by construction.
3. **Shared rasters are not thread-safe today.** `Raster::Data()` assembles a sparse raster into `mutable` fields
   of shared storage with no synchronization (`Document.h` `Storage::bytes`, `materialized`). A worker reading a
   painted layer races the composition, `ReadRegion`, `MakeThumbnail` and `ByteSize`. Fixing this blocks everything
   else.
4. **Cancellation has to reach inside the kernels.** A cancelled job that runs to the end holds the lane for the
   whole kernel. That is seconds for a blur and minutes at radius 250, so the next preview and the Escape feedback
   wait just as long.
5. **Busy state, guards and file waits come from the oracle.** While a commit runs, overlapping edits are refused
   at once. The interface dims only after 250 ms, so quick edits never flash. File requests wait until the session
   is idle rather than failing.
6. **Save needs more than a thread hop.** It needs:
   - `MarkSaved(revision)` with the revision captured when the snapshot is taken, because the current
     `MarkSaved()` would also mark edits made during the save as saved;
   - a continuation for the five save-prompt callers;
   - COM on the worker;
   - row-band encoding, so the save stops assembling sparse layers into shared storage.
7. **The inventory was incomplete.** These operations also block, and about half of them the oracle already runs
   off the main thread (see the Legacy column):
   - opening a panel (downscale, histogram), and the GPU readbacks;
   - selection coverage at every stroke start;
   - Smudge/Liquify start, the gradient drag;
   - fill, clear, invert, pixel moves, shapes, resampling transforms;
   - the magic wand and mask tracing;
   - Image Size, Canvas Size, export, copy to and paste from the system clipboard.
8. **Measurement records the numbers; it no longer decides what moves.** At the 100-megapixel document limit no
   per-pixel loop fits a frame, whatever it costs on a 4K layer. So what moves is decided by class. The bench
   records before and after, and gates what stays on the UI thread.
9. **Two lanes, not one worker.** A multi-second save must not freeze previews. The UI thread keeps the order
   between the lanes: a save takes its snapshot only after a pending commit has been applied.
10. **Corrected premises.**
    - The adjustment card is fixed at the top right of the canvas hole; it is not movable. What must keep moving
      is the window.
    - The status strip has three fixed sections (zoom, size, tool) and no free text.
    - The oracle ignores Cancel while a commit runs. The first draft's Done requires Escape to cancel it, so that
      is recorded below as an intentional deviation.

## Rule

This is the target normative text. It merges into `Core_PerformanceAndResources.md` at closeout.

1. The UI thread pumps messages, snapshots job inputs, applies finished results, composes and presents.
2. **Off the UI thread, always:**
   - every CPU loop over the pixels of a layer, mask, selection coverage, composite or document;
   - file, codec and clipboard encode, decode and I/O;
   - every wait on the GPU for a readback.
3. **Stays on the UI thread:**
   - metadata edits (order, names, visibility, opacity, blend, transforms that do not resample);
   - history moves;
   - per-event tool work bounded by the tip or by one 256² tile (dabs, warp dabs, tile uploads);
   - outline edits bounded by the path, as long as they measure within budget;
   - DxUi layout, `Compose` and `Present`;
   - the Windows-owned modal loops;
   - the OS-synchronous clipboard contract (`WM_RENDERFORMAT`, copying bytes out of an open clipboard).
4. **Budget:** every message handler, job snapshot and result application takes at most **8 ms p95** on a
   3840 × 2160 layer on the reference machine, Release x64, measured by `--bench ui`. The reference machine is the
   one in `Specs/TestRuns/Brush_2026-09-21.md`; its 120 Hz display gives an 8.3 ms frame.
5. **A job holds values only:**
   - shared `Raster` handles, transforms, selection loops, settings, seeds drawn on the UI thread, the edit serial
     and the history revision;
   - never a pointer or reference to the session, the document, a stroke, the composition, the device context, an
     HWND or a UI-thread COM object;
   - its destructor is safe on any thread.
6. **Previews coalesce.** At most one preview is in flight per edit, plus one pending slot that keeps only the
   newest request. A newer value never cancels the one in flight.
7. **Commits.** A commit is applied as one history entry or dropped when stale (edit serial, layer image identity,
   transform). Cancel, Escape and closing the panel abandon a running commit and leave the document as it was.
8. **Busy** means a commit, document or file job is in flight.
   - Document commands refuse at once.
   - File commands wait and run when the session is idle.
   - View commands (pan, zoom, window move and resize, scrolling, hovering) never wait.
   - Nothing visible changes until the job has run for 250 ms.
9. **Prohibited:** polling, periodic timers, nested message loops of our own, and `catch (...)`.
   - `std::bad_alloc` keeps today's semantics: kernels that already return empty still do. Anything that escapes
     a worker's `noexcept` entry terminates with a crash dump, as it does on the UI thread today.
   - Worker threads block on a condition variable when idle.

## Decisions

These are the questions the first draft left open. The owner may overturn any of them; each has a default so work
can start.

| # | Question | Decision | Why |
| --- | --- | --- | --- |
| D1 | Where is the async boundary? | Per edit family in `DocumentSession`: `Prepare…Job()` on the UI thread (values only, O(1) or O(layers)); `static Run…(job, cancel)` on any thread, touching only the job; `Apply…(result)` on the UI thread (validate, then install one entry or drop). Today's synchronous entry points stay and become `Apply(Run(Prepare()))` | Keeps the ~30 synchronous `DocumentTests` calls; makes worker == direct hold structurally; keeps kernels and goldens untouched |
| D2 | How many threads? | Two lanes, each one thread, created on first use and blocked when idle. **Compute**: color edits, raster edits, stroke sources, selection work, merge, resizes. **Files**: open, save, import, export, clipboard encode/decode | A save must not freeze previews. Two idle blocked threads cost nothing. The UI thread orders across lanes |
| D3 | Queue shape | Keyed slots, each with one pending job and one running job; a submit to a slot replaces its pending job and never touches the running one. Compute slots, highest priority first: `Commit`, `Preview`, `Histogram`, `Source`, `Coverage`. Files slots: `Project`, `Clipboard` | Bounded memory (at most seven pending jobs and two running); latest-wins without starving; the user's commit goes first |
| D4 | Cancellation | `Common/CancelToken.h`: a pointer to the slot's `std::atomic<std::uint64_t>` generation plus the generation the job started under, read with a relaxed load. `Cancel(slot)` bumps it; `CancelAll()` bumps every slot. The long kernels take `const CancelToken& cancel = {}` as a last parameter and check it once per row, per pass, per `Solve` iteration or per 4096 queue pops. A cancelled kernel returns empty; a cancelled result is never applied, not even partly | Without it a stale 4K blur holds the lane for seconds. The default token never cancels, so direct calls and goldens do not change |
| D5 | Is a newer preview a cancel? | No (see Rule 6). The in-flight preview finishes and shows; the pending slot runs next | Oracle, verbatim (`HueSaturation.swift`) |
| D6 | OK: disabled until ready, or start the job? | OK starts the commit and the edit enters **Committing**: panel values are refused (oracle `committing` guard), the preview stays on the canvas until the pixels are installed (oracle: "the canvas never flashes the original"), and the session is busy. **Reuse:** if a finished preview has `previewScale == 1` and the committed settings (table kinds, Levels and Hue/Saturation when the layer fits 4096 px; blurs and lens when the grown grid fits 2048 px; noise, grain and content-aware fill always), its pixels are the commit's pixels and the job runs only the post-steps (trim, mask carry, thumbnail). A full-scale preview still in flight with those settings is awaited instead of restarted | Oracle reuses `preparedPreview` for automatic kinds; the reuse is exact because the inputs and the code path are identical (tested) |
| D7 | Cancel while committing | Escape, Cancel and closing the panel cancel the commit: the layer is unchanged and nothing is recorded. **Intentional deviation**: the oracle ignores Cancel while `committing` | The Done requires it; with seconds-long blurs a stuck panel is worse |
| D8 | What is refused while busy or while a panel is open? | Port the oracle's guards as session predicates: `CanEditLayers()` (layer commands, merge, masks, grouping, reorder, rename), `CanUseHistory()`, `CanStartProjectOperation()` (files). All of them fail while busy. `CanPaint` and `CanAdjustColors` also check busy | Closes two gaps found in review: Merge with a pixel-filter panel open merges the unfiltered layer; Save during an adjustment-layer edit saves the live record and marks it saved with the history bracket open |
| D9 | What does the user see? | Nothing for 250 ms (oracle `busyIndicatorDelay`), through a one-shot `SetTimer` that is killed on its first `WM_TIMER` or when idle. After that: the rail, headers, layers panel and adjustment card dim, except Cancel; the status strip's tool section reads the activity; the cursor over the window is `IDC_APPSTARTING`. No progress bar, no animation | An animation would render at display rate for the whole job; the oracle has none |
| D10 | Status strings | New `STRINGTABLE` entries (`localization` skill): `IDS_STATUS_APPLYING` "Applying {0}…" ({0} is the panel's localized title), `IDS_STATUS_SAVING` "Saving {0}…", `IDS_STATUS_OPENING` "Opening {0}…", `IDS_STATUS_IMPORTING`, `IDS_STATUS_EXPORTING` "Exporting {0}…", `IDS_STATUS_MERGING`, `IDS_STATUS_RESIZING`, `IDS_STATUS_WORKING` (fallback) | `ChromeHost::SetStatus` has three fixed sections; reusing the tool section needs no DxUi layout change |
| D11 | Save or Open while a commit runs | The request is kept as the pending **file intent** (one slot; Exit replaces anything, anything else replaces an older non-Exit intent) and runs once the commit is applied or cancelled. The save takes its snapshot then | Oracle `waitForFileRequest`. The worker never holds the post-commit document, so ordering belongs to the UI thread |
| D12 | Save-before-replace (New, New Canvas, Open, Import as project, Close Project, Exit) | `ConfirmDiscardChanges` returns `Proceed`, `Cancel` or `Saving`. `Saving` stores the caller's intent in `_afterSave`. A successful save runs it; a failure or a cancelled Save As clears it and shows the error | The five callers need a continuation; "proceeds only when it saved" is kept |
| D13 | Marking saved | `DocumentHistory::MarkSaved(Guid revision)` with `history.CurrentRevision()` captured with the snapshot, skipped when the history was `Reset` after the snapshot. Undo and redo restore revisions, so a later undo back to the saved state reads as unmodified | `MarkSaved()` would mark edits made during the save |
| D14 | Completion delivery | Results wait in a mailbox owned by the lane. The worker posts `kBackgroundMessage = WM_APP + 8`, which carries no payload, only when no notification is already outstanding (an atomic flag). The handler clears the flag, then drains. A failed post or a destroyed window leaves results in the mailbox, which is destroyed with the lanes | Removes payload ownership from the message. A posted message wakes `MsgWaitForMultipleObjectsEx(QS_ALLINPUT)` in both existing waits |
| D15 | Re-entrancy | `ModalScope` (RAII, UI thread) wraps every nested modal loop RedPrism starts: `ShowError`, `AskThreeWay`, the file pickers, the native popup menus, the unsupported-version box and About. While any scope is open, completions stay in the mailbox; the last scope to close re-posts if results are pending. The caption's move/size loop is not wrapped: applying there is safe | A completion dispatched inside a menu or dialog would mutate the session under a command still on the stack |
| D16 | Stroke-start sources (Blur, Clone on this layer, Smudge/Liquify) | A **prefetch** job fills a session cache keyed by the layer image (the `Raster` held, not its address), transform, mask target and its edge tone, σ (clamped 1.5–30, so the key is σ, not the diameter), and document size. Triggers: the tool is chosen, the layer, target or σ changes, a stroke commits, undo, redo. A press before the source lands starts a **pending stroke**: points are recorded, not painted, then replayed through the unchanged stroke code when the source lands. The stroke's pixels equal today's because commands are refused while the stroke is pending | Every committed stroke replaces the layer raster, so a lazy cache would always miss; prefetch moves the same computation earlier |
| D17 | Selection coverage at stroke start | A session cache keyed by the selection value and document size, filled by a `Coverage` prefetch when the selection changes while a painting tool is active, and used by `BeginBrush` | Today `SelectionGeometry::Coverage` (66 MB of floats at 4K) runs on every press with a selection |
| D18 | Spot healing at mouse-up | `Flush()` stays on the UI thread (engine render). A value snapshot (source raster, union bounds, touched tiles' coverage and base, selection clip, settings, seed from `NextHealSeed()`) runs the heal, rebuilds the touched tiles and makes the snapshot on the Compute lane. Apply goes through `CommitStroke`'s existing guard, so it records one `Spot Healing` entry. The stroke object ends on the UI thread before the job runs: its destructor clears the GPU engine's tiles. The working texture shows until the entry lands. Painting waits, because the session is busy | The oracle is synchronous here to keep order; busy keeps the same order without blocking |
| D19 | GPU readbacks | `CanvasRenderer::BeginReadback(purpose)`: `CopyResource` into a reusable staging texture (released with the document surfaces), then `ID3D11DeviceContext4::Signal` and `ID3D11Fence::SetEventOnCompletion` on an event that joins both `MsgWaitForMultipleObjectsEx` handle sets. When the event is signalled, `FinishReadback` maps without waiting and copies once into a `Raster` through a new strided factory. At most one readback is in flight plus one pending, and a newer request for the same purpose replaces the pending one. Fallback: without `ID3D11Device5` fences, keep today's blocking `Map` and record it. Device loss drops the readback and the command reports `IDS_ERROR_READBACK` (samples fail silently). Purposes: merge, adjustment-layer input, wand, clone all layers, copy merged, color sampling, export | Removes the only GPU wait from the pump. Today every readback also allocates a new staging texture and copies twice |
| D20 | Threads, COM, crashes | The lanes are `std::thread` with a `std::mutex` and `std::condition_variable`, at normal priority (the work is user-initiated) with no power throttling. The app supplies start and exit hooks: `SetThreadDescription` ("RedPrism compute", "RedPrism files"); `CrashDumps::InstallThreadCrashHandlers()` (new export, because `std::set_terminate` is per-thread in the MSVC runtime); `CoInitializeEx(COINIT_MULTITHREADED)` and `CoUninitialize` on the Files lane. The crash filter's second-fault guard waits for the first dump instead of terminating while it is being written | WIC calls fail as "unreadable" without COM on the thread; a worker terminate would otherwise dump under the abort code, not `0xE0000001` |
| D21 | Shutdown | `WM_CLOSE` while busy stores the Exit intent and returns. Previews and prefetches are cancelled at once. A running commit or file job is awaited through the intent, so a filter already accepted with OK lands before the save prompt. `CloseMainWindow` cancels every slot and joins both threads; the per-row checks bound the join. `WM_QUERYENDSESSION` during a Files job calls `ShutdownBlockReasonCreate` (`IDS_STATUS_SAVING`), returns `FALSE`, and destroys the reason when the job ends | Never lose a save or an accepted edit; never `TerminateThread` |
| D22 | Clipboard | Copy and Cut: `SetClipboardData(format, nullptr)` for the PNG and DIBV5 formats (delayed rendering), and a Files job pre-encodes. `WM_RENDERFORMAT` and `WM_RENDERALLFORMATS` hand over the pre-encoded bytes, waiting for that job if it is running (an OS-synchronous contract, like the modal loops); `WM_DESTROYCLIPBOARD` drops them. Paste: open, copy the bytes out, close, decode on the Files lane, apply `PasteImage` | Today Copy runs a full PNG encode plus a DIB conversion on the pump (est. 180–550 ms at 4K) even when nothing else ever pastes |

## Inventory

Every row moves unless it says "measure". A "measure" row stays on the UI thread if `--bench ui` shows at most 8 ms
p95 at 4K; otherwise it moves in its slice. The "Legacy" column names where the oracle ran the operation off the
main thread, or says "sync" when the oracle is synchronous.

| # | Operation | Today on the UI thread | Legacy | Slice |
| --- | --- | --- | --- | --- |
| P1 | Open a filter panel | `BeginFilter`: `PrepareFilterPreview` (`Adjust::Downscaled`), `GrowFilterGrid`, then the first `RenderFilterPreview` | prepare sync, preview detached | 2 |
| P2 | Filter preview on every change | `UpdateFilter`: grow on a larger radius, `SelectionCoverageOn`, `RunFilter`, `BlendThrough`; runs on every `WM_MOUSEMOVE` of a slider, stepper, curve or spectrum drag, with no coalescing | detached, coalesced | 2 |
| P3 | Filter and content-aware fill OK | `CommitFilter`: `RunFilter` at full size, `BlendThrough`, `TrimmedToAlpha`, mask carry, `MakeThumbnail` | detached | 2 |
| P4 | Open Levels | `BeginLevels`: `Downscaled` and `Histogram` of the original (which materializes a sparse layer) | `histogramTask` detached | 2 |
| P5 | Levels and Hue/Saturation preview and OK | `UpdateLevels`, `UpdateHueSaturation`, `CommitLevels`, `CommitHueSaturation` (`ApplyAdjustment`, `BlendThrough`) | detached, coalesced | 2 |
| P6 | A tool switch applying an open panel | `SettleInteractions(true)` → `ApplyColorEdit` runs the full commit | async commit | 2 |
| P7 | Open an adjustment-layer editor | `CompositeBelow` (Compose, blocking `Readback`, `Raster::Make`) then `Histogram(below)`; the panel opens when the input lands | render awaited, histogram detached | 4 |
| P8 | Rebuild the Hue/Saturation adjustment layer's cube | `Adjust::ColorCube` in `CanvasRenderer::AdjustmentInputs` on each settings change | — | measure |
| R1 | Spot healing at mouse-up | `FinishBrush` → `BrushStroke::Heal` (search, multigrid solve, tile rebuild) → `CommitStroke` | sync | 3 |
| R2 | Blur stroke start | `BlurSample`: `RenderLayerPixels` or `RenderMaskPixels` at document size, then `GaussianBlur` (σ 1.5–30) | sync | 3 |
| R3 | Clone stroke start (this layer) | `CloneSample` → `RenderLayerPixels` | sync | 3 |
| R4 | Clone stroke start (all layers) | `WandComposite` per stroke; it runs even when `CanPaint` then refuses | sync | 4 |
| R5 | Selection coverage at every stroke start | `SelectionGeometry::Coverage` in `BeginBrush` | — | 3 |
| R6 | Smudge and Liquify start and finish | `BeginWarp` → `RenderLayerPixels`; `FinishWarp` paints along the stroke | sync | 3 |
| R7 | Gradient drag and Apply | `RefreshGradient` → `FillGradient` over the whole layer on every move; `CommitGradient` → `CommitStroke` | commit async | 3 |
| R8 | Fill, Clear, Invert | `FillSelection`, `ClearSelectedPixels`, `InvertPixels` | async | 3 |
| R9 | Pixel move finish and nudge, Layer via Copy, Cut, internal Paste, the floating selection merge | `FinishPixelMove`, `NudgePixels`, `LayerViaCopy`, `CutSelection`, `Paste`, `MergeFloating` | async (`commitRasterEdit`); the floating merge is sync | 3 |
| R10 | Shape finish and redraw on scaling | `FinishShape` → `ShapeImage` (up to 100 MP), `RedrawShape` | sync | 3 |
| R11 | Transform Apply that resamples (distortion) | `CommitTransform` | sync | 3 |
| R12 | Add Mask from a selection | `AddMask` (coverage on the layer grid) | sync | 3 |
| S1 | Every GPU readback | `CanvasRenderer::Readback`: a new staging texture per call, a blocking `Map`, a row copy, then `Raster::Make` copies again | — | 4 |
| S2 | Magic Wand | `WandSample` (`RenderLayerPixels` or a readback), `MagicWand::Select` (flood fill, tracing) | async | 4 |
| S3 | Load a layer or mask as a selection | `LoadLayerSelection`, `LoadMaskSelection` (tracing) | sync | 4 |
| S4 | Expand, Contract, Invert selection; non-antialiased coverage | Clipper2 offsets; `CenterCoverage` is O(rows × vertices) | sync | measure |
| S5 | Copy, Copy Merged | `RenderSelectedPixels`, `RenderMergedPixels` plus a readback | sync (Cut is async) | 4 |
| S6 | Eyedropper, Hue/Saturation sampling, targeted-drag press | `WandComposite` once per press; the color applies when the readback lands | sync | 4 |
| D1 | Merge | `MergeLayersCommand` (Compose, `Readback`), `MergeLayers` (`TrimmedToAlpha`, `MakeThumbnail`) | sync | 5 |
| D2 | Image Size | `ResizeImage` resamples every layer | `ImageResizer` actor, cancellable | 5 |
| D3 | Canvas Size with an extension | `ResizeCanvas` builds the `Canvas Extension` layer | `CanvasResizer` actor | 5 |
| D4 | Crop, New, New Canvas | metadata; Crop moves content without resampling | Crop through the `CanvasResizer` actor; New sync | measure |
| F1 | Open a project, and project drops | `LoadProject`: inflate, WIC decode, thumbnails; several drops run in sequence | `ProjectStore` actor | 6 |
| F2 | Save, Save As, save from the prompt | `SaveProject`: manifest, a PNG encode per asset and mask, deflate of already-deflated PNGs, write, `ReplaceFileW` | `ProjectStore` actor | 6 |
| F3 | Import images (as layers or as a project), image drops | `ImportImageOnto` decodes and inserts inside one edit; split it into decode (job) and insert (apply) | `ImageImporter` actor | 6 |
| F4 | Export PNG or JPEG | Compose and `Readback` **before** the dialog, then encode and write | `ImageExporter` actor | 6 |
| F5 | Copy and Cut mirrored to the system clipboard | `MirrorClipboard` → PNG encode plus a DIB un-premultiply | — | 6 |
| F6 | Paste from the system clipboard | `ReadImage` decodes with the clipboard still open | — | 6 |
| U1 | Upload of a new 4K raster (a preview or a result) | `UploadRaster`: `UpdateSubresource`, `GenerateMips` | — | measure |
| U2 | Undo or Redo of a 4K filter | the evicted texture is uploaded again | — | measure |
| U3 | First composite after opening a 4K project | one upload per layer | — | measure |
| U4 | `DisplayDocument` copy per preview result; history `Trim` and `RetainedBytes` | O(layers) | — | measure |
| U5 | `MakeThumbnail` of a sparse raster with many tiles | O(9 216 samples × tiles) | — | measure |

**U1, U2, U3, U5 and P8** stay on the UI thread in this plan. Any of them over budget is recorded in the closeout
TestRun and opens the matching follow-up (below). It does not widen this plan.

## Design

### Files and types

| New or changed | Content |
| --- | --- |
| `Common/CancelToken.h` | `struct CancelToken { const std::atomic<std::uint64_t>* generation; std::uint64_t expected; bool Cancelled() const noexcept; }`; no Win32 types |
| `Common/BackgroundLane.{h,cpp}` | `BackgroundJob` (`virtual void Run(const CancelToken&) noexcept = 0; virtual void Complete() noexcept = 0;`), `BackgroundLane` (slots, priority, generations, mailbox, `Submit(slot, job)`, `Cancel(slot)`, `CancelAll()`, `TakeCompleted()`, `WaitIdleForTest()`, start/exit/notify hooks). Standard library only, so HostTests and DocumentTests compile it |
| `RedPrism/BackgroundWork.{h,cpp}` | Owns the two lanes; the hooks (thread description, crash handlers, MTA COM, `PostMessageW`); `ModalScope`; the busy bookkeeping and the 250 ms timer (`kBusyTimerId = 8`) |
| `RedPrism/ApplicationJobs.cpp` | The app's `BackgroundJob` subclasses. Each holds a job value plus `Application*`, and uses it **only** in `Complete()` (UI thread) |
| `Common/Document.h`, `AdjustEdit.cpp`, `BrushStroke.cpp`, `Selection.cpp`, `CanvasSize.cpp` | Per family: `…Job`, `…Result`, `Prepare…Job`, `static Run…`, `Apply…`; an edit `serial`; `committing`; the busy flag and the guard predicates; the stroke-source and coverage caches; the pending stroke |
| `Common/Adjust.*`, `ImageFilters.*`, `Heal.*`, `Selection.*`, `CanvasSize.*`, `ImageCodecs.*`, `ProjectStore.*` | A trailing `const CancelToken& cancel = {}` on the long entry points; row-band encoding for sparse rasters |
| `Common/Document.cpp` | Thread-safe materialization; `Raster::MakeStrided`; `DocumentHistory::MarkSaved(Guid)` |
| `RedPrism/CanvasRenderer.*` | `BeginReadback`, `FinishReadback`, `ReadbackEvent`; a reusable staging texture; a fence |
| `RedPrism/Application*.cpp`, `ChromeHost*.cpp` | Jobs, intents, `_afterSave`, `ConfirmDiscardChanges` tri-state, `ChromeHost::SetBusy(bool, std::wstring)`, `WM_SETCURSOR`, `WM_QUERYENDSESSION`, clipboard delayed rendering |
| `Common/CrashDumps.*` | `InstallThreadCrashHandlers()`; the second-fault guard waits |
| `RedPrism/CommandLine.h`, `Application.cpp` | `--bench ui` |

### Raster thread safety (slice 1, before any job)

- `Storage::materialized` becomes `mutable std::atomic<bool>`.
- `Data()` does an acquire load. If the raster is not assembled, it takes a striped mutex (a static array of 16
  mutexes indexed by the storage address), re-checks, assembles, then publishes with a release store.
- `ReadRegion`, `IsMaterialized`, `ByteSize` and `MakeThumbnail` read the flag with acquire before they touch
  `bytes`.
- Contiguous storages are created already materialized. Their cost is one extra load, and their memory is
  unchanged.
- Jobs may call `Data()` on shared storage only after this fix. It stays exactly what today's UI thread does.
- Save, export and clipboard encoding never assemble shared storage. They feed WIC row bands read through
  `ReadRegion`. This also stops a save from growing `RetainedBytes` and evicting undo entries.

### Panel edits (slice 2)

1. `Begin…` is O(1): it records the original, transform, selection, settings, seed and a fresh serial, then asks
   for a preview. Downscaling, growing the grid and the histogram happen in jobs.
2. `Update…` stores the settings and submits a preview job to the `Preview` slot; the slot coalesces (D3, D5). The
   first preview result also carries the prepared preview source, the scale and the grown grid; the edit caches
   them.
3. `Apply…Preview` needs the same serial, no `committing`, and Preview on. It installs `previewRaster`, bumps
   `_editRevision`, and the host calls `DocumentChanged`.
4. The Levels histogram is its own `Histogram` job. `histogramReady` stays false until it lands, and the Auto
   buttons already follow that flag.
5. OK is D6; Cancel and Escape are D7.
   - `SettleInteractions(true)` starts the commit and lets the tool switch finish.
   - `SettleInteractions(false)` (document replaced) cancels.
6. Adjustment layers keep their live GPU preview. Only `CompositeBelow` and its histogram move, in slice 4.

### Commit family for raster edits (slice 3)

`CommitStroke` gains the split. The UI thread snapshots the stroke's values: source, tiles, grid, selection clip,
name and `alsoApply` data. The job builds `PaintSnapshot` (`Raster::Replacing`, `MakeThumbnail`). Apply keeps
today's guard: the layer image and transform must be unchanged. Gradient, fill, clear, invert, pixel moves, warp
finish and heal (D18) all feed it, like the oracle's `BrushCommit` actor.

Brush and eraser commits stay synchronous. They are tile-bounded and already measured in
`Specs/TestRuns/Brush_2026-09-21.md`; a pending commit also refuses the next stroke through busy.

### Files (slice 6)

- **Open**
  - Order: pick the file (modal dialog), then the prompt (D12), then an `Open` job running `LoadProject`.
  - Apply calls `ReplaceDocument`.
  - A multi-project drop is one intent holding a list of paths.
- **Save**
  - Order: pick the file if needed, then the `Save` job. The job holds a copy of the document (shared rasters),
    the active layer, the path, zip or folder, and the revision.
  - Apply sets the path, calls `MarkSaved(revision)` and `SyncChrome`, or shows the error.
- **Import**
  - The job decodes (`ImportImageFile` with the remaining pixel budget).
  - Apply inserts the layers as one `Import Image(s)` entry.
- **Export**
  - The dialog comes first, then the readback (D19), then the encode-and-write job.
  - The JPEG quality is captured as a value.

## Measurement: `--bench ui` (slice 0)

- **Command:** `RedPrism.exe --bench ui`, and `--warp --bench ui`. `CommandLine.h` accepts `ui` beside `brush`.
  HostTests gets a `cli.bench-ui` case, and `Testing_Validation.md` updates the HostTests and benchmark rows.
- **Fixture:**
  - a hidden window with a 3840 × 2160 document;
  - an opaque layer of hash noise (fixed seed), and a painted (sparse) twin;
  - a centered 512 × 512 selection for fill and content-aware fill;
  - a wand outline traced from the noise layer, for S4;
  - a 4K three-layer document, for merge and U3.
- **What runs:** every inventory row that has a host path, driven through the same `Application` entry points as
  the input (quiet UI), with 1 warm-up and 5 runs.
- **Output:** `UI BENCH op=<row> handler=<ms> apply=<ms> frame=<ms> job=<ms>` as median / p95 / max.
  - `handler`: time until the command returns to the pump.
  - `apply`: the completion's application.
  - `frame`: the first composite and present that show the result.
  - `job`: the worker's time.
  - Before the move, `handler` includes the whole synchronous work, and `apply` and `job` are 0.
- **Records:**
  - `Specs/TestRuns/UiNeverBlocks_<date>-before.md` at slice 0, and `-after.md` at closeout.
  - Same header, column and "Reading" format as the brush run, on the reference machine plus WARP.
  - Not a `test.ps1` gate (`Testing_Validation.md`: benchmarks are recorded, not gated). The Done checks the
    after-run.
- **File rows:** F1–F4 are not timed. Timing them would need the fail-closed `X:\RedPrism.Perf` sandbox, which
  does not exist yet. They move by class anyway. Their UI-thread steps (the document copy for Save,
  `ReplaceDocument` for Open) are timed in memory.

## Slices

Each slice ships on its own with the full local matrix green:

- `.\validate-skills.ps1`
- `.\format.ps1 -Check`
- `.\test.ps1` x64 Debug, Release and "ASan Debug"
- `.\build.ps1 -Platform ARM64`

Each slice is committed as model, host, then records.

- [ ] **Slice 0 — Baseline** (no behavior change)
  - `--bench ui` with the rows that exist today.
  - The "before" TestRun.
  - The `cli.bench-ui` case.
- [ ] **Slice 1 — Foundation** (no visible change)
  1. Raster thread safety, with DocumentTests `raster.concurrent-materialize`: two threads assemble one sparse
     raster and get the same pointer and the bytes `ReadRegion` returns. It also runs under ASan Debug.
  2. `CancelToken`, `BackgroundLane` and HostTests `lane.*`:
     - priority order;
     - a submit replaces the pending job and never the running one;
     - `Cancel` reaches a running job's token and drops a pending one;
     - one notification for several results;
     - no completion after `CancelAll`;
     - destruction joins a job blocked in `Run` once it is cancelled;
     - a sentinel message posted to a hidden top-level window is dispatched while a gate job blocks the lane.
       The gate is a `std::promise` the test releases: no sleep, poll or clock.
  3. `BackgroundWork`:
     - `kBackgroundMessage`, `ModalScope` and the deferral;
     - the thread hooks (D20);
     - `CloseMainWindow` cancels and joins;
     - `CrashDumps::InstallThreadCrashHandlers` and the second-fault wait.
  4. Busy:
     - the session flag and the guard predicates (D8) wired into the commands;
     - the 250 ms timer, `ChromeHost::SetBusy`, `IDC_APPSTARTING` and the D10 strings;
     - file intents and `_afterSave`. The file work still runs inline, but through the intent path.
  5. `Application::DrainBackground()` (self-test and bench only: wait for both lanes to be idle, then dispatch
     the completions).
  6. A self-test case: a gate job holds the Compute lane while a posted sentinel is dispatched and a frame
     renders.
- [ ] **Slice 2 — Color panels → Milestone M1** (P1–P6)
  - The D1 split for filters, Levels and Hue/Saturation.
  - Preview coalescing, the histogram job, Committing, reuse, Cancel during the commit, the tool-switch commit.
  - `CancelToken` in: `RunFilter` and every kernel it calls, `ApplyAdjustment`, `Histogram`, `Downscaled`,
    `BlendThrough`, `TrimmedToAlpha`, `SelectionGeometry::Coverage`.
  - DocumentTests `split.*`:
    - for every `FilterKind`, Levels and Hue/Saturation on twin sessions, Prepare → Run → Apply produces the same
      bytes, transform and entry name as `Commit…`;
    - a result with a stale serial is dropped;
    - a cancelled token records nothing and keeps the layer's image identity (also filling today's gap:
      `CancelFilter` has no count check);
    - a reused full-scale preview equals a fresh commit;
    - the histogram job equals `Adjust::Histogram`.
  - The self-test drains wherever it assumed a synchronous preview or commit (the Levels preview and Enter,
    Gaussian Blur, the content-aware fill), then runs the M1 scenario. The Hue/Saturation adjustment-layer case
    keeps its live GPU preview and is unchanged until slice 4.
- [ ] **Slice 3 — Retouch and raster edits** (R1–R3, R5–R12)
  - The `CommitStroke` split, stroke-source and coverage prefetch, the pending stroke with replay, the heal job.
  - DocumentTests:
    - `split.stroke-*`: byte equality with the synchronous path for heal, gradient, fill, clear, invert, pixel
      move, warp finish, shape and the resampling transform;
    - `source.*`: a prefetched sample equals `BlurSample`/`CloneSample`; the key misses after a commit and hits
      after an undo;
    - `pending-stroke.*`: replayed points give the stroke's synchronous bytes.
  - The existing `FinishBrush` expectations (`DocumentTests.cpp` Spot Healing) stay green on the synchronous path.
- [ ] **Slice 4 — Readback, sampling, selection** (S1–S6, P7, R4)
  - The asynchronous readback (D19) with its fallback.
  - The wand and tracing jobs; Copy and Copy Merged; sampling on readback.
  - The adjustment-layer editor opens when its input lands.
  - Each composite is composed from the document it is meant to read, not "whatever was composed last":
    `WandComposite` and `ExportFlattened` rely on the last composition today.
  - CompositionTests `readback.*` (WARP): the asynchronous bytes equal the blocking bytes; the staging texture is
    reused and released when hidden; a readback in flight during `RecoverDeviceForTest` is dropped.
- [ ] **Slice 5 — Document-wide** (D1–D3)
  - The merge job after the readback; `ResizeImage` and `ResizeCanvas` as cancellable jobs.
  - DocumentTests `split.merge`, `split.image-size` and `split.canvas-size` equal the synchronous results
    (`merge.*`, `imagesize.*` and `canvas.*` goldens unchanged).
- [ ] **Slice 6 — Files and clipboard** (F1–F6)
  - D11–D13, D21 and D22.
  - Row-band encoding, MTA COM on the Files lane, `MarkSaved(Guid)`.
  - IoTests `save.row-bands-equal`: a sparse layer saves byte-identical PNG entries without assembling its
    storage.
  - IoTests `save.revision`: edits after the snapshot stay modified.
  - Self-test:
    - Save from the prompt continues New only when it succeeded;
    - `WM_CLOSE` during a save waits, then closes;
    - the system clipboard mirror still round-trips through delayed rendering.
- [ ] **Slice 7 — Closeout**
  - Merge the Rule and the D-decisions into the owning specs (the header list).
  - The `Testing_Validation.md` rows: HostTests `lane.*` and `cli.bench-ui`; DocumentTests `raster.*`, `split.*`,
    `source.*` and `pending-stroke.*`; CompositionTests `readback.*`; IoTests `save.*`; the self-test sentence.
  - `docs/` for the busy cursor and status.
  - The "after" TestRun.
  - Move this plan to `Done/` and remove its index row.

## Milestones and Done

**M1 (after slice 2)**, run by the self-test with a fixed seed.

- Fixture: a 3840 × 2160 layer and Gaussian Blur at radius 2.
- A gate job holds the Compute lane.
- OK returns to the pump at once: the edit is Committing and the session is busy.
- While the gate holds:
  - a posted sentinel message is dispatched;
  - a frame renders;
  - the window position changes through `SetWindowPos`.
- Escape cancels: the layer image keeps its identity, the undo count is unchanged, and no panel is left.
- Reopen, OK, release the gate, drain: exactly one `Gaussian Blur` entry, whose bytes equal `CommitFilter` on a
  twin session.

**Done**

- [ ] Every inventory row is moved, or its "measure" row shows ≤ 8 ms p95 in the after-run.
- [ ] Every U-row is recorded with its follow-up where over budget.
- [ ] `--bench ui` after-run on the reference machine: `handler`, `apply` and `frame` ≤ 8 ms p95 for every moved row
      (U1–U3 excluded as above).
- [ ] The M1 self-test.
- [ ] `lane.*`, `raster.*`, `split.*`, `source.*`, `pending-stroke.*`, `readback.*` and `save.*` are green in Debug,
      Release and ASan Debug; the ARM64 build is green.
- [ ] No golden was rebaselined.
- [ ] Every owning spec states the Rule and the behavior.
- [ ] The plan is in `Done/`.

## STOP

- Cancelling a running preview because a newer value arrived.
- A modal progress dialog, or any nested message loop of ours.
- Running a kernel on the UI thread "just this once" for OK, a tool switch or a save prompt.
- A job holding a pointer or reference into the session, the document, a stroke, the composition, the device context,
  an HWND or a UI-thread COM object.
- A job destructor that touches UI state.
- A worker calling `Raster::Data()` on shared storage before the slice 1 fix.
- Applying a result without the serial and identity checks, or applying part of a cancelled result.
- `MarkSaved()` (the current revision) after an asynchronous save.
- Polling: a `GetData` loop on a query, `Sleep` in a loop, a periodic timer, or a progress animation that renders
  at display rate.
- The UI thread waiting on a worker, except in `CloseMainWindow` after cancelling, in `WM_RENDERFORMAT` (D22), and in
  the self-test or bench `DrainBackground`.
- `catch (...)`, or catching `std::bad_alloc` in a job to "keep going".
- A second D3D device. Creating D3D resources on a worker (follow-up 4 decides that separately).
- Changing kernel math, or rebaselining a golden because the worker reordered floating-point work: the result must
  match the direct call byte for byte.
- Parallelizing a kernel inside a job (follow-up 2).
- Killing `RedPrism.exe`.

## Follow-ups (separate plans; the after-run numbers set their priority)

These are the speed levers this plan deliberately leaves alone. It only moves work; they make the work itself
cheaper.

1. **Fast Gaussian blur.**
   - The kernel is a direct separable convolution with r = ⌈3σ⌉ taps each way, double accumulators and a
     `W × H × 4` float intermediate (132 MB at 4K).
   - At radius 250 each output needs about 1 500 taps per pass, so a 4K commit runs for minutes.
   - An O(1)-per-pixel method (for example an extended box or recursive Gaussian) is the largest wall-clock win for
     the M1 scenario. It needs a spec change and goldens; the ported blur goldens are ranges, so they leave room.
   - Motion blur (up to 96 bilinear taps) is next.
2. **Row-band parallelism inside a job.**
   - Bit-exact for the per-pixel kernels (tables, gradient map, cube, lens, blur passes, `RenderLayerPixels`,
     `RenderMaskPixels`) and for noise and grain when bands pass the absolute row and the full width.
   - Not bit-exact for `ContentFill` (a sequential LCG over queue order), `Histogram` (double sums in raster order)
     and the heal `Solve` (in-place SOR). Those stay sequential.
3. **GPU preview for the table kinds, Hue/Saturation and Grain.** It uses the composition's adjust passes, which
   already exist for adjustment layers: zero CPU per slider tick, at full resolution.
4. **Texture creation on a worker.** The device is created without `D3D11_CREATE_DEVICE_SINGLETHREADED`, so it is
   free-threaded; that is not a second device. This is for U1–U3 if they are over budget.
5. **Store PNG entries in the ZIP** (`ZIP_CM_STORE`) instead of deflating PNGs again. Check the legacy reader
   first.
6. **Keep one evicted texture generation** so an undo right after a commit does not upload again (U2).
7. **Refresh a blur sample incrementally** after a stroke (the dirty bounds plus 3σ). It is exact only while the
   transform is unchanged.

## Found in passing

Recorded here. The fixes that belong to this plan are marked.

- Merge with a pixel-filter panel open merges the unfiltered layer, and the edit is dropped at OK. Fixed by D8,
  slice 1.
- Save during an adjustment-layer edit writes the live record and marks it saved while the history bracket is open.
  Fixed by D8, slice 1.
- `WandComposite` and `ExportFlattened` read back whatever was composed last, which can include a preview. Fixed in
  slice 4.
- A Clone stroke sampling all layers pays the readback even when `CanPaint` refuses it. Fixed in slice 4.
- Export composes and reads back before its dialog, so a cancelled dialog wastes the work. Fixed in slice 6.
- `std::set_terminate` is per thread, so worker threads would lose the crash handler's terminate code. Fixed in
  slice 1.
- Durability, not fixed here:
  - the exploded-folder save is not atomic across a crash (between the two `MoveFileW` calls, nothing recovers
    `.old` or `.saving`);
  - no save calls `FlushFileBuffers`;
  - export writes the destination in place.
- `Adjust::CurveValue` allocates a vector on each call inside `noexcept` (1 536 calls per table build).
- Undo and Redo stay enabled in the menu while a panel refuses them.
