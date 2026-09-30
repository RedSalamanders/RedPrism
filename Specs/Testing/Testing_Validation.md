# RedPrism validation

Status: current normative repository contract
Last reviewed: 2026-09-30
Owner: `test.ps1`, `Tests/*`, and `RedPrism.exe` diagnostic switches

## Scope

This contract owns how RedPrism is proven. Domain specs name the behavior; this file names the gates that MUST pass
before a phase is Done. Screenshot policy for chrome lives here so later phases do not invent a second rule.

## Entrypoint

`.\test.ps1 [-Platform x64|ARM64] [-Configuration Debug|Release|'ASan Debug'] [-Rebuild]` builds through `build.ps1`,
then runs every assertion below. It MUST exit 0 only when all of them pass. Expected nonzero exits from isolated
negative-test children (`HostTests.exe --asan-probe`, `RedPrism.exe --no-such-switch`) MUST NOT become the test
entrypoint's success exit code and MUST NOT be reused as the pass/fail signal for a later gate.

ARM64 runtime qualification requires a native ARM64 host. Cross-built ARM64 binaries are produced by `build.ps1` only.

The hidden WARP self-test runs under a bounded timeout; a run that does not finish in time fails with the process id
reported, never killed. In Debug and ASan Debug, `RedPrism.exe`'s harness modes (`--self-test`, `--screenshot`, a
`--demo`, `--crash-probe`) and each of the four test executables install a CRT report hook (`_CrtSetReportHook2`)
alongside the existing `_CRTDBG_MODE_FILE` / `_CRTDBG_FILE_STDERR` routing to stderr: an `_CRT_ERROR` or `_CRT_ASSERT`
report (for example a debug-heap corruption message that would not otherwise stop the run) now fails it —
`RedPrism.exe` ends the process immediately, and each test executable counts the report and fails at the end —
instead of the report being printed while the run still exits 0. `HostTests.exe --crt-report-probe` deliberately
trips one so `Tests/BuildProcessTests/CrtReportHookTests.ps1` can prove the hook works, the way `--asan-probe` proves
AddressSanitizer.

The self-test reads a keyboard of its own (`RedPrism/KeyboardState.h`): its drivers hold exactly the modifiers a
step names, and the input code reads Shift, Ctrl and Alt from that keyboard, never from the thread's key state.
Windows writes every physical key transition into each thread's key state at once, the hidden self-test window's
thread included, so without it a Shift or Ctrl the user types in another application while a gate runs changes a
step at random (RF-1110); with the thread's key state, a Ctrl the user held turned a typed S into Save and left
the run waiting on a Save As dialog. A quiet run never opens a file dialog: Open, Import Images, Save As and the
exports return as cancelled. The self-test's clipboard steps borrow the machine's clipboard, which other
applications' clipboard listeners open for tens of milliseconds after every change: the test's own reads, writes and
the set-aside wait them out (`SystemClipboard::kHarnessAttempts`, about 3 s), and it waits for a free clipboard
before a copy whose mirror it checks, while the product's copy and paste keep their short wait.

## Gates

The pipeline foundation adds `DocumentTests.exe --stress`: 256 rounds of eight simultaneous readers of shared
sparse rasters (assembly, region reads, thumbnails and byte accounting), with independently computed expected
pixels and stable-pointer checks. Eight rounds also run in the normal document suite. `test.ps1` runs the extended
stress in ASan Debug. Neither mode asserts elapsed time; ASan diagnoses memory misuse but does not detect races.

| Gate | Pass condition |
| --- | --- |
| Version identity | `RedPrism.exe` `OriginalFilename` is `RedPrism.exe` and `ProductName` is `RedPrism` |
| HostTests | `HostTests.exe` exit 0: adapter-of-monitor, frame-action selection (including display-off), command catalog and `--dpi` rejection, `EditorTool` key/glyph/name tables, every rail glyph present in Segoe Fluent Icons (skipped with a `SKIP` line when the font is absent), every STRINGTABLE id loaded from `RedPrism.exe` as a data file with positional placeholders only, transform overlay geometry (rotated edge and corner hits, rotation knob, zoomed document mapping, resize cursor directions, distortion midpoints), `--demo transform`, `--demo selection` and `--demo brush` parsing, `--demo retouch` parsing, `--bench brush` parsing, `--demo adjust`, `--demo canvassize` and `--demo crop` parsing, `--crash-probe <directory>` parsing and its refusal without a directory, rejection of other names; the crash-report helpers (`crash.*`: default directory, pruning to the newest ten, a minidump of the test process, the pending marker round trip, an empty directory refused); the satellite string tables (`locale.*`: candidate order and de-duplication, a missing satellite, the system languages, the `SetStringResourceSatellite` override and fallback); the About version reader (`version.*`: `FormatFileVersion`, a null path and a module without a version resource refused with the fallback shown, `RedPrism.exe`'s own `VS_VERSIONINFO` read as the `Common/Version.h` major and minor with the stamped build) |
| ASan Debug only | `HostTests.exe --asan-probe` MUST NOT exit 0 and the captured log MUST contain `AddressSanitizer: heap-use-after-free`. An ordinary crash without that diagnostic is a failed probe. |
| Help | `RedPrism.exe --help` exit 0; redirected stdout contains `--self-test` and `--dpi` |
| Unknown switch | `RedPrism.exe --no-such-switch` exit 2 |
| Crash probe | `RedPrism.exe --crash-probe <directory>` (a directory under `.build\`) exits nonzero and leaves exactly one `RedPrism-*.dmp` above zero bytes with a `pending.txt` naming it ([`Core_CrashHandling.md`](../Core/Core_CrashHandling.md)); the directory is removed afterwards |
| WARP self-test | Hidden `RedPrism.exe --self-test --warp` exit 0: visible DxUi surface within 64 MiB with ≥ 1 composite, canvas surfaces allocated, a second frame does not re-composite the settled document, a splitter drag through `DispatchPointer` widens the layers panel by 40 DIP and narrows the hole after the layout re-sync, the foreground well opens the picker and a sampled color commits into the session palette, a cancelled background edit leaves it, X/D swap and reset, the opacity stepper records one `Layer Opacity` undo entry at 50 %, a painted layer shows the Move tool's box, a pointer drag commits one `Transform Layer` entry, Free Transform previews a typed rotation until Apply, Escape cancels, Shift+Right nudges ten pixels, Flip Canvas Horizontal and Flip Layer Vertical mirror, a Marquee drag selects a whole-pixel rectangle and a Shift drag adds one, M switches the shape once per press, dragging inside the selection moves it as one `Move Selection` entry, Escape and Ctrl+Z during such a drag put the outline back without an entry, a click inside deselects, the wand selects a painted layer's pixels with exact bounds, Ctrl+Shift+I / Ctrl+A / Ctrl+D drive the Select menu, three polygonal clicks and Enter close a `Polygonal Lasso`, Expand from the header grows it by five pixels, Escape cancels an outline, a Brush stroke through the pointer path with the compute engine shows a live preview and commits one sparse `Brush Stroke` entry drawn from the adopted texture without materializing, the Eraser clears it, `[` `]` Shift-`[` and `5` change the tip, Escape drops a stroke, Clone Stamp stamps a mark from a source, Spot Healing blends it away, Smudge drags it through a live warp, Blur commits, a Gradient previews and applies with Enter after a digit set its opacity, a Shape drag adds a Rectangle layer, Shift-U switches the kind, Escape drops a draft, the Eyedropper samples the shape's color, Fill with Foreground Color fills a Marquee selection, Ctrl+X cuts it to the clipboard (mirrored to the system clipboard), Ctrl+V pastes it in place as a new layer, Ctrl+I inverts it, a Ctrl-drag moves the selected pixels through the live preview as one `Move Pixels` entry with the outline following, Ctrl+Right nudges them, Delete clears them, Ctrl+T on a selection floats them (Escape restores exactly, Enter merges one `Transform Selection` entry), Ctrl+J copies them to a layer, Add Mask hides them, Ctrl+Shift+C copies the composite, Ctrl+L opens the Levels panel and its output white previews black through the display document, Escape restores, an input white of 128 turns the gray white and Enter records one `Levels` entry, the Filter menu's Gaussian Blur applies one entry, Shift+Delete opens Content-Aware Fill on a red block filled into a selection of the white plate and Enter restores it as one `Content-Aware Fill` entry, with everything selected the fill reports no source, refuses Enter and closes on Escape, a Hue/Saturation adjustment layer opens its panel and a hue of 120 rotates the red beneath it live on the canvas, Enter keeps its record, Shift+= steps the blend mode, a rail click through the pointer path activates its tool (plainly, with the window focused, under the button's tooltip, and held across settled frames after a tool switch, after a click on another rail button and with the opacity stepper focused), the File menu's flyout anchor lies under its item on screen and a point on the open color picker card counts as chrome while the hole beside it counts as canvas, every interactive chrome control has an accessible name and the window's UIA root exposes the DxUi tree (its parent link, at least eight named elements, a hit under the menu bar), a settings file of the self-test's own reloads live (a light theme, the sample ring off and a 300 DIP layers panel apply, a broken file changes nothing and is not rewritten, restoring it applies again), the high-contrast palette applies and `RefreshTheme` follows the system state, and a save records the window placement; an image opened with the forced Add as Layer answer joins the document as a layer and with New Project replaces it by an unmodified untitled document of the image's size, a modified document survives New under the forced Cancel answer and is replaced under Don't Save, Close Project empties the shell (status `—`, a frame still renders) and New brings a document back, Import Images adds two files as one `Import Images` entry, Move Layer Up and Down swap siblings as `Reorder Layers` entries, Group Selected Layers wraps the active layer in a folder, Ctrl+Alt+G clips a layer to the one below and releases it, each function the UI spec lists as unimplemented raises the notice count by one and changes neither the document, the history nor the tool, Canvas Size shrinks the document to half in percent with the ratio locked around the top-left anchor on Enter and grows it with a white Canvas Extension layer from the bottom-right anchor, Escape cancels a pending size, Image Size resamples to 325 × 205 at 300 ppi with nearest sampling (the red layer's pixels and box scale with it) and three undos restore the document, New Canvas creates a 300 × 200 unmodified document, and the Crop tool frames the canvas when chosen, resizes the frame to 640 × 400 by a handle drag through the pointer path, crops on Enter as one `Crop` entry, moves the frame by a drag inside it, drops it on Escape, squares it through the header's ratio, cancels, is chosen again and undoes the crop with Ctrl+Z (its own whole-canvas frame takes no Undo) and leaves the tool without a frame, the Rename Layer card renames the active layer through typed characters and Enter and leaves the name alone on Escape, Merge Down composites a blue layer onto a red one into one trimmed layer named after the lower one and undo restores both, the Layer menu's mask commands gray out without a mask and a layer's mask is targeted again once another layer was chosen (Target Layer Mask and back, with the captions and the row's badge tone; Unlink and Disable Layer Mask as their entries with `Mask off` on the row; a Shift-press on the row's Mask badge choosing that layer and turning its mask on again; a press on the row's name targeting the pixels; a plain press on the badge the mask), the rendering steps listed in [`Rendering_Composition.md`](../Rendering/Rendering_Composition.md) Validation pass (actual pixels 1:1, wheel and Zoom tool zoom, device removal at every frame stage rebuilt and drawn on, a failed canvas that keeps the chrome, a 17 000 pixel document through its proxy and read back at full resolution, readbacks without the preview, the adopted curve equal to its raster, a gradient on another layer previewing there, surfaces dropped while minimized or occluded, the minimum window size, the screenshot exit codes, repeated failures that ask to close through the save prompt, a gradient left pending on one layer when another is chosen ending with its own commit so each layer draws exactly as its raster), the keyboard steps listed in [`UI_ApplicationChrome.md`](../UI/UI_ApplicationChrome.md) Validation (every key through the window procedure: replacements, text fields, AltGr, the Command List, the menu bar, the Move tool's digits, the displayed chords), the session checks (a typed opacity typed back to its start, closed by a command, carried to a new document, cancelled and committed; the Rename card's own layer; Cancel at the save prompt keeping a panel, the size card, a crop frame, a shape draft and a pending transform; a live stroke on a clean document; Save under an adjustment layer's panel; a folder project saved in place; an image batch whose first file fails; Canvas Size over a pending gradient; a copy the system clipboard refused; capture loss mid-stroke and mid-drag; a cancelled splitter drag; a leave without a capture; a stroke dragged over the layers panel keeping the pointer; Space across a focus loss; Alt-press, Undo and a drag; a close inside a chrome callback; the session end and its settings save) and the integration checks of the 2026-09-23 review (a reopened project keeping no mask target; a dropped image centered on the drop point on the canvas and on the canvas's middle off it; Load Mask and Load Layer Selection saying why a 2100 × 2100 checkerboard loads nothing; budget refusals raising one notice each for Layer via Copy with and without a selection, Gaussian Blur and Merge Down; Copy and Copy Merged applying a pending gradient first; Merge Down under a pending gradient holding it; a pending mask gradient following Paint, and X and D flipping and resetting the mask palette; the rail's wells showing that palette in black and white whatever the colors, a well opening no picker while a mask is the target and a picker open on a well closing unchanged once one is; a typed "2.5" in Gaussian Blur's radius kept as typed; the Gradient Map picker closing with its panel and the next well pick reaching the palette; Image Size's unit pick without resampling storing the unit; the rail parsing no SVG while settled and at most two per tool switch; the palette zones, the curve editor, the levels bars and the hue spectrum reachable in the UI Automation tree; a press on the spectrum's hue bars moving no handle while one in its handle strip does), what a pointer move costs the chrome and its UI Automation snapshot ([`Core_PerformanceAndResources.md`](../Core/Core_PerformanceAndResources.md), "Pointer input and the chrome": a Hand pan writing and preparing nothing, a Space pan, a wheel step and a brush move writing only the overlay and the zoom text, a transform drag rebuilding no layer row or menu, a settled sync writing nothing, no snapshot published without a listening client and one per preparation with one, `WM_GETOBJECT` and the in-process root publishing a waiting snapshot, a card published without a client) and the canvas cursor's per-position decisions (no layer scan with auto-select off, one scan and one selection test per position, none outside the selection's bounds, the cursors unchanged), then 0 DxUi bytes and 0 canvas bytes when hidden |
| DocumentTests | `DocumentTests.exe` exit 0 (graph, `ProjectStore`-identical validation, history budget, mask placement helpers, collapsed rows and their mask state, clip release on delete, legacy insertion/deletion/naming semantics, palette, transform geometry/drag/snap/session/duplicate/group/mask/flip/distort cases ported from the legacy transform suites, selection geometry/coverage/session/move/modify/load/marquee and wand kernel/session cases ported from the legacy `SelectionTests` and `MagicWandTests`, sparse rasters and the brush kernel, strokes, masks, opacity, curves, crossings, snapshots, budgets, keys, expansion and selection clips from the legacy brush suites, listed in [`Rendering_Brush.md`](../Rendering/Rendering_Brush.md), and the retouch cases: spot healing for every mode, clone stamp alignment and tip families, blur, smudge, liquify, warp tiles, gradients, shapes with the redraw on scaling, and the eyedropper; and the pixel edits ported from the legacy `SelectionEditTests` and `SelectionClipboardTests`: fills, Clear and the Delete key, invert, masks from the selection, pixel moves and nudges, the clipboard and the floating selection transform; and the color adjustments and filters ported from the legacy `LevelsTests`, `HueSaturationTests`, `ImageAdjustmentTests`, `FilterTests`, `AdjustmentLayerTests` and `BlendShortcutTests`: levels, hue/saturation, exposure, gradient map, grain, blurs, noise, lens correction, adjustment layers edited through the panels, the blend-mode step and the HSL formulas; and the content-aware fill ported from the legacy `SmartEditTests` (`smart.*`): the stripes continue through the filled block, a flat background reconstructs and undoes, Cancel and a no-source fill leave the original, the grid grows to a selection past the layer's edge; and the Layer menu edits ported from the legacy `LayerTests`, `GroupingSelectionTests` and `LiveMaskTests` gates (`order.*`, `wrap.*`, `clip.*`, `clear.*`): sibling swaps and their refusal, single, folder and empty grouping with undo, the clipping toggle with a shared base, and the cleared session; and the sizes ported from the legacy `CanvasSizeTests` and `ImageSizeTests` (`canvasdraft.*`, `canvas.*`, `imagedraft.*`, `imagesize.*`) and the crop cases from the legacy `CropTests` (`crop.*`) listed in [`Document_Tools.md`](../Document/Document_Tools.md); and the merge plan and merge (`merge.*`: no plan for a bottom layer or an empty folder, Merge Down onto the lower layer in the anchor's slot with clips re-targeted and undo, Merge Group replacing the folder, a stale plan refused) |
| CompositionTests | `CompositionTests.exe` exit 0: the golden table in [`Rendering_Composition.md`](../Rendering/Rendering_Composition.md) including the C12 adjustment layers and HSL blends (blend modes, order, raster masks with placement, live masks and chains, clipping stacks, folder masks, transforms, residency and caching, viewport, sparse upload, stroke preview and adoption, compute brush parity with the CPU kernel) |
| IoTests | `IoTests.exe` exit 0: the list in [`IO_ProjectFormat.md`](../IO/IO_ProjectFormat.md) plus PNG/Gray8/JPEG codec round trips, HEIF clean error, import onto a document (a PNG is not a ZIP project), and the settings store (`settings.*`: file names and paths, parsing with per-field fallbacks and rejects, serialization keeping unknown fields and a newer schema, the cold load of a missing, invalid and unreadable file, the live load, and saves that back up an invalid file, keep unknown fields, leave no temporary and create the directory) |
| Provenance | `DxUi.provenance.json` matches the lock; wrong-pin fixtures are rejected |
| Build presentation | Framed pixel-art banner identity, even scene geometry, and interactive-terminal selection |
| Harness screenshot | `--screenshot <png> --warp --dpi 96` and `--dpi 144` each write a PNG of the empty-document shell (populated rail) captured after the first frame that reached the display (drawn even with the display off), whose client-area pixels are neither a single uniform color nor fully transparent nor smaller than 32×32 (`test.ps1` decodes the PNG and samples a grid of pixels), or exit 8 only when this system cannot capture a window — `test.ps1` treats exit 8 alone as SKIP, while exit 9 (a capture that failed or never happened) and any other nonzero exit fail the gate, and passes the output path to RedPrism.exe quoted as its own argument; `--demo transform --dpi 96` writes the transform demo (two painted layers, the rotated one's box and the header) and `--demo selection --dpi 96` the selection demo (an elliptical and a rectangular selection with the Marquee header) and `--demo brush --dpi 96` the brush demo (strokes on a backdrop with the Brush header and the tip cursor) the same way, and `--demo retouch --dpi 96` the retouch demo (a radial gradient, a rounded rectangle and an ellipse under the Shape header) and `--demo adjust --dpi 96` the adjustment demo (a Levels adjustment layer's panel with its histogram over a painted backdrop), and `--demo canvassize --dpi 96` the Canvas Size card over the transform layers (relative +200 × +100, the top-left anchor, a white extension), and `--demo crop --dpi 96` the Crop tool framing 640 × 400 of the transform layers with the shade and the header |
| Brush benchmark | `RedPrism.exe --bench brush` (hardware) and `--warp --bench brush` exit 0 and print `BRUSH BENCH` lines; the numbers are recorded under `Specs/TestRuns/` at each closeout ([`Rendering_Brush.md`](../Rendering/Rendering_Brush.md)), not gated |
| Kernel benchmark smoke | In Debug, Release and ASan Debug, hidden `RedPrism.exe --bench kernels-smoke` exits 0 within a bounded wait with redirected stdout and stderr. Stdout has exactly 13 unique `KERNEL BENCH name=` operation rows, each for 32×24 with `samples=2`, `warmup=1` and a nonempty hexadecimal checksum, plus exactly one `KERNEL BENCH REPORT status=ok width=32 height=24 operations=13` line. Three `BUFFER BENCH` pairs compare copy/adopt ownership in alternating order and must match the corresponding scalar-kernel checksums, with one successful completion report. `test.ps1` prints one PASS summary. This checks benchmark output and deterministic completion; it makes no wall-clock performance assertion and runs no full-size kernel benchmark. |
| Release packaging | `package.ps1 -Platform x64` exit 0 with the clean-extraction smoke (`--help`, `--self-test --warp` with a system-only `Path`, the packaged version identity) and `package.ps1 -Platform ARM64` exit 0 (the smoke is skipped on an x64 host); a release gate run before a release and at a closeout, not part of `test.ps1` ([`Build_Packaging.md`](../Build/Build_Packaging.md)) |

`ASAN_OPTIONS` for the probe is `halt_on_error=1:abort_on_error=0:detect_leaks=0`.

## Test style

### Immutable color-edit work (P1.1a)

`DocumentTests` runs `coloreditwork.*`: independent legacy Levels clipping/gamma/alpha numbers, preview reuse,
preview-off computation, one history entry and exact undo, and state-based no-op, cancellation and failure cases.
Opening tokens must reject moved layers, changed masks, changed selections and resized canvases both before and
after preparation. Newer settings, a reopened panel, another session, document replacement and undo restoration
must reject old results without touching a newer edit. Mask carrying includes its eager semantic thumbnail;
compatible name/resolution edits survive. Another layer consuming the remaining budget, or a refused blur-grid
increase, must preserve pixels, mask, history and last-applied settings. Content-Aware Fill without donors and a
processing failure have distinct results. Existing `hsv.preview-*`, filter and adjustment goldens remain required.

The WARP self-test must keep a clean document's changed Levels panel under Cancel at the save prompt, and refuse
Save before creating a file when Content-Aware Fill has no donors. The panel, document and history survive that
refusal. The existing successful adjustment-panel save/reload case still passes. This slice does not qualify
asynchronous dispatch, bounded inner-kernel cancellation or whole-host G1 timing.

### Cancellable filter graph (P1.1b)

`rasterwork.*` uses literal bytes for padded crops, clipped bases, sparse inside/outside fills, replacement windows,
nested graphs, the 64-frame traversal limit, extreme coordinates and cancelled/invalid reads. No failed operation
may publish a raster or mutate a shared source cache. `filtercancel.*` covers every filter's pre-cancelled result,
distinct no-donor failure, private sparse reads through public helpers, area/nearest-sampling literals and
transformed coverage. Existing noise/grain/blur/filter numeric goldens remain unchanged.

`coloreditwork.memory.*` checks selection counts through sharing/move, maximum Gaussian scratch, sparse source
copies, persistent coverage, fill scratch and exact-preview reservation reuse. HostTests exercises cancellation
and odd-width/padded rows through all six dispatched scalar entry points. The opt-in
[`--bench-filter-cancel`](Testing_Benchmarks.md#filter-cancellation-diagnostic) records real stop-to-return
distributions; those timings are not ordinary test assertions and do not establish whole-host G1.

### Deferred panel computation (P1.1c.1)

`coloreditdeferred.*` tests the actual immutable preview/histogram/commit functions and session apply paths.
Independent pixels and histogram bins prove that an old preview cannot supply a newer commit, while an opening
histogram may arrive after a settings tick. State cases cover sparse capture without materialization, off/on
invalidation, Cancel/reopen/replacement, target moves and selection changes, current-result metadata mismatch,
exact reuse, grown-grid cache changes, pre-cancellation and preparation memory. Adjustment-layer parameter ticks
preserve the fixed opening histogram but cannot create a destructive commit. `selection-loops-bounds.*` covers
finite filtering, empty/degenerate geometry, shared copies, moves and unchanged geometry equality.

These tests qualify the data and publication boundary. Host mailboxes, real-loop input, busy controls and
file/close continuations have separate P1.1c gates; no host responsiveness claim follows from these direct tests.

`--self-test --warp` additionally gates the production histogram and preview jobs with owned events. While the
histogram is blocked, the shared pump dispatches a message and the Zoom In command; the unchanged opening histogram
then arrives after a settings tick. Thirteen pending settings changes produce exactly one running and one final
preview, with the final settings sequence displayed. A completed-but-undrained old preview is cancelled and its panel
reopened; that result cannot enter the new panel. All gate waits are harness failure bounds, never timing assertions
or production waits. The real UI benchmark smoke verifies the command/result/frame ordering in the outer host loop.
Busy Commit controls and file/close continuations remain separate P1.1c.3 gates.

### CPU dispatch and instruction boundary (P0.8)

HostTests validates every required AVX2 feature/OS-state bit independently, irrelevant-bit tolerance, host
baseline detection, Scalar/Auto selection, unavailable-tier refusal and preservation of the previous table.
It checks the actual six scalar function entries and padded, odd-width alpha/transparent adjustment inputs.
Existing DocumentTests and WARP numeric goldens still exercise the extracted arithmetic through the public API.
The CLI table tests all five names plus missing, empty, duplicate, unknown and `auto` rejection.

The kernel smoke runs with `--isa scalar` and requires `isa=scalar` in all 13 operation rows. Separate hidden
processes prove unimplemented, foreign-architecture and malformed tier requests exit 2 with the expected
diagnostic. The ordinary WARP self-test also exercises Auto initialization.
All x64 builds run the PE/PDB-attributed VEX/EVEX gate and compiled passing/failing controls described in
`Build_Toolchain.md`; these controls never execute their high-ISA code. ARM64 cross-builds are compile evidence,
not native execution. Pre-AVX execution under Intel SDE remains optional and must be recorded if performed.

### Job input and progress policies (P0.6b)

HostTests covers one-active/latest-pending preview ownership, stale epochs and refinements, all eight continuation
slots, refusal ownership, ring wrap, wand coalescing, shared stroke ordinals, cross-class history exclusion and
explicit failed-stroke retry. Literal virtual-QPC cases pin the 250 ms threshold without sleeps or host-speed
assertions. All 32 end-session flag combinations have literal outcomes. Fixed activity records reject stale progress
and retirement after slot reuse; an event-gated scheduler observer case proves UI-thread notice ordering and lifetime.

Retirement-aware queue tests retain independent receipt references across superseded, stale and cleared entries.
Literal metadata and FIFO expectations cover ring wrap, callback-before-release ownership, active entries surviving
Clear, transferred entry lifetime, no callback while history is busy, and full/invalid admission retaining caller
ownership. These qualify per-entry retirement, not the production runner's typed payloads or receipt publication.

`continuation-receipt.*` tests the real observer/publisher model: first terminal outcome/error, invalid and pending
publication refusal, copies/moves surviving publisher destruction, moved-from destruction, unique publication,
pending abandonment and immediate failed admission values. Actual retirement-aware queues cover full/invalid
refusal retaining ownership, stale and superseded outcomes, FIFO Clear keeping the active input pending, and
destructor cancellation fallback. This does not qualify the production construction boundary's allocation failure.
`continuation-request.*` tests the actual typed host inputs: original Unicode path order/drop point, write options,
all size-draft fields, layer/name ownership, panel/document and session-end keys, move-overwrite retirement and
retained outcomes after input release. These qualify owned inputs/results; actual caller effects, bounded payload
admission and staged runner activation remain P1.1c.3c work.

The hidden WARP `continuation-host.*` checks the Application's actual registered source with owned numeric panel
inputs and immutable commits. It covers construction `bad_alloc` injection, eight pending plus ninth refusal,
Clear and idle, an ordinary UI-action queue at capacity, source fairness/disarming, exact acquisition validation,
real pixel/history apply before later terminal receipt publication, acknowledged active cancellation, scheduler
`bad_alloc` error preservation, late success before Cancel/replacement, reopened panel/document lifetimes,
Cancel after acknowledged success/failure preserving that terminal outcome and its effects,
immediate adjustment metadata settlement and unregister/retirement at shutdown. An independent earlier color
commit and explicit finisher seams for Selection/Stroke/Open/Import prove waiting-request wake-up through the
common completion hook; the seam does not qualify those operations' future asynchronous implementations. Its wait
has a fixed failure bound, not a latency gate; production UI never uses that wait. These tests do not activate Save,
Close, import or other producers, nor qualify variable-length payload admission or the complete caller matrix.

DocumentTests runs actual direct session operations through the ordering state machines to check that a stroke
uses the predecessor's applied pixels (or the surviving pixels after failure/cancel), separate undo entries, Undo
after a pending stroke, and replacement/stale-preview guards. This qualifies ordering foundations, not the P1
asynchronous tool migrations. Existing brush/adjustment numeric goldens remain required.

The WARP self-test submits a gated job through the production scheduler observer, advances visibility using exact
virtual QPC boundaries, prepares the status control at 96/144 DPI, tests determinate/indeterminate states, and
settles the job through the shared pump. Completion removes its indicator and timer; a stale timer cannot revive it.
Both showing and hiding the bar across a held rail click preserve that click through an intervening preparation.

### Job scheduler and host pump (P0.6a)

HostTests may create a **message-only HWND**, without a device or visible window, to prove the actual shared
`PumpMessages`, `DrainCompletions`, `RunUiActions` and `SettleJobs` path. A worker signals entry and waits behind
an event; a posted window-message sentinel must dispatch before its completion, and releases that event. The
finished result applies once, the cancelled variant applies no successful result, and both release every payload.
Expected values come from these independent state transitions, not the implementation's queue layout.

Additional gates cover bounded admission with retained caller ownership on refusal, FIFO through apply/drop,
independent serial keys, stale tickets, progress, the job-runner `bad_alloc` failure boundary, one action per
pass, rejected reentrant drains/actions, and shutdown that joins before destroying active payloads. Gate waits
are failure bounds only; no sleeps or timing assertions. Existing legacy image goldens remain unchanged.

The registered UI-action-source gates cover a full ordinary action queue, retained ownership on refusal,
coalesced readiness, source/ordinary fairness, self-rearm on a later pass, event/idle state, refused reentrant
drains and callback-time removal, exact identity, stop and borrowed-source teardown. History-work gates cover
sequential stages under one selected ordinal with distinct acquisitions, late same-kind completion refusal,
strict initial Begin ordering and document/owner replacement. DocumentTests combines a committed pixel edit and
Canvas Size under one request, then a younger queued Undo: the first Undo removes only size and the next restores
the original numeric pixels. These qualify primitives; production host continuations remain separate work.

The WARP self-test also simulates an early mode failure with a worker waiting on the shutdown event. The actual
`Application::Shutdown` must join it without applying its result, retain the implicit MTA through the callback,
destroy its payload after Run returns, and leave the caller's STA alive. `Main.cpp` calls that same shutdown
before clearing the resource satellite or leaving its COM scope on every mode return, including failures.

`HostTests.exe --bench-jobs` measures scheduler overhead with eight warmups and 200 single Commit jobs. It reports
submission and full submit-to-drain median/p95/max in microseconds, with an independently checked worker result.
Payload construction is outside the timed intervals; admission, cancellation-state allocation, worker dispatch,
completion delivery and payload release are included. Record Release numbers without a timing assertion. This
is not the UI/image/RAW benchmark and proves no image speed target.

### UI timing foundations (P0.1a)

HostTests runs `ui-timing-histogram.*` and `ui-timing.*` numeric and state checks. Independent expectations cover bucket
edges and nearest-rank percentiles, exact maximum and total overflow, QPC conversion (including one tick above
4 ms and integer overflow), nested captures, worker isolation, real message dispatch and scheduler apply/action
hooks. No test asserts an elapsed-time target. The private `pipeline-trace.*` ETW test also decodes timing event
names, durations and detail fields, owner-thread restriction and the strictly-above-4-ms thresholds; recording
or decoding failures and lost events fail the gate.

`Tests/BuildProcessTests/WprProfileTests.ps1` checks the trace profile with `wpr -profiles` and `-profiledetails`.
It never starts, stops or cancels a system trace; missing WPR or an invalid profile fails with diagnostics.
`HostTests --bench-ui-timing` reports 31 batches of 20,000 empty scopes with capture off/on, QPC medians, p95 and
maxima including loop cost. It refuses an already-enabled external timing session so its disabled-path numbers
are interpretable. This is instrumentation overhead only; operation baselines and G1 remain in the WIP plan.

`RedPrism.exe --bench ui|ui45|ui100|ui-smoke` is the opt-in application-loop measurement described in
[`Testing_Benchmarks.md`](Testing_Benchmarks.md). Smoke validates completion and output with no timing threshold;
normal test runs never download the RAW corpus.

Smoke may render with the session display off, while full measurements refuse that state. It validates ordered
elapsed distributions and the total Present count over two trials. Scheduler tests that inspect undrained job
completion use an injected stable memory source: an OS memory notification can wake a production scheduler before
any job has published its result. Memory notification/admission behavior is independently covered by JobMemoryTests.

### Worker foundations (P0.5)

HostTests also runs the standard-header `ExecutorTests` and Windows `WorkerPoolTests`. Their independent
reference is the execution/lifetime state table in `Core_Architecture.md`: irregular and maximum grid geometry,
single visitation, serial-formula equality, cancellation and incomplete-tile refusal, N simultaneous jobs on
N=1/2/4 workers, nested execution, implicit MTA, close retaining an active frame, bounded one-shot waits, stale
wait-ticket rejection, and a wait completion while every compute worker is occupied. Event gates force the
interleavings; 10-second waits are failure bounds only. A failed gate exits the isolated test immediately to avoid
destroying context still borrowed by callbacks.

`HostTests.exe --stress` adds 64 saturated/nested/close rounds. `test.ps1` runs this extended mode in ASan Debug,
plus both main-thread and two-worker crash probes in every configuration. The worker child must write exactly one
nonempty dump and its pending marker after overlapping faults, including one worker `std::terminate` path.

`HostTests.exe --bench-executor` is an opt-in Release measurement: an independently checked 4096-square coordinate
kernel through inline and 1/2/4/8/16/logical-processor executors, with warmup and 11 timed samples, plus empty-tile
invocation overhead, cancel-token check cost and a separate synthetic row polling-gap measurement. It makes no
timing assertions. Record it separately from image-kernel or UI/Photoshop claims. Native
concurrency SAL analysis uses `build.ps1 -Analyze`; Application Verifier qualification records the actual enabled
checks and executed binary under `Specs/TestRuns/`, and never substitutes for ASan or native ARM64 execution.

DocumentTests also runs `generation.*` and `history-revision.*`: owning result tokens and document/restore serials,
all session mutation families (including changes inside open edits), const session readers, shared immutable
selection snapshots, and marking the revision actually saved after intervening edits, Undo, branching and trim.
These are structural ownership/history checks; the existing legacy numeric tool goldens remain unchanged.

Tests are in-process executables under `Tests/` with one `Expect(condition, "suite.case")` per observable fact,
`PASS`/`FAIL` lines on stdout, and a nonzero exit on any failure. Goldens are numbers taken from the legacy Swift
tests, never "looks fine". GPU tests create their own WARP device; nothing in `Tests/` opens a visible window. Fixture
trees are materialized under `%TEMP%\RedPrism.IoTests`; a test that writes a file removes any stale copy first.

## Later gates (record now, implement with the feature)

The P0.7 HostTests additions use independent byte/accounting and event-order references: `memory-admission.*`,
`deferred-release.*`, `job-memory.*`, `pixel-recycle.*` and `pipeline-trace.*`. They cover no partial reservations,
waiters outside workers, failure/cancel/drop paths, serial-key admission through retirement, release with all job
slots occupied, pool capacity/slot limits, live buffers surviving pool close and concurrent borrowers. Under
ASan, checks inspect poisoning of cached payload and size-class slack and unpoisoning on reuse.

The ETW gate starts a private in-process TraceLogging session, includes synthetic and actual 1 MiB UI free
positive controls, releases real 16 MiB direct/adopted/vector storage through the worker queue, and decodes the
resulting ETL. It asserts no lost events, exact positive-control bytes and no UI free of at least 16 MiB. It needs
no elevated system-wide tracing session. Failure to record or decode is a failure, never an omitted test.

`HostTests --bench-memory` is opt-in measurement only: 8192×5464 BGRA, two warmup pairs and ten alternating fresh
and reused pairs; allocation, touch and release have separate QPC regions. Full checksums, page-fault deltas and
process-memory samples are outside those regions. No elapsed time is asserted by the ordinary test suite.

| When | Gate |
| --- | --- |
| C8–C13 | Tool and filter goldens ported from retained numeric golden cases behavior, not UI-driver scripts |
| C14 | Settings, crash, localization, UIA |
| C15 | Portable ZIP packaging |

GUI tests default to no-activation. Interactive desktop focus requires an explicit lease in a later spec.

## Screenshots

Screenshots for specs and docs MUST come from the application harness (Windows.Graphics.Capture of an owned window, or
an equivalent harness helper). Computer Use and desktop screenshot tools are prohibited as evidence.

## Fail-closed data root

When a test needs a writable sandbox for documents or perf traces, it MUST use `X:\RedPrism.Perf` (any local drive
letter plus a directory ownership marker), never repository `.build/` as a data sandbox. `ResolvePerfRoot` accepts
only that exact root grammar on fixed local drives. `REDPRISM_PERF_ROOT` selects an explicit root; otherwise the
resolver prefers an already marked X: root, then another marked local root, and finally creates a new root on the
Windows drive. An existing unmarked, malformed-marker or reparse root is never claimed or recursively cleaned.
The marker is `.redprism-perf-owner` with exact ASCII bytes `RedPrism performance root owner v1` followed by LF.
The ETW harness deletes only its own PID-named trace files and leaves the ownership marker in place.

## Skills

Skill or `AGENTS.md` edits MUST pass `.\validate-skills.ps1` in the same change.

## Required local matrix for a closeout

P1.1c.3 preparation adds two hidden WARP checks. The busy-control check uses real attached-host pointer dispatch
to prove Curves, Levels and Spectrum report while enabled, stop reporting through busy transition and preparation,
and accept fresh input after restoration. It also checks Cancel/OK enable states, numeric focus-loss behavior,
completed decimal preservation, suppression of late disabled-field callbacks,
and the histogram, Colorize and curve-removal prerequisites.

The ordered-commit check gates the actual worker on owned events. It proves no history/pixel change before apply,
mutation callback/Enter suppression, view input, rejection of a competing ordinal and release only after one
successful apply. Four gray inputs under Gamma 2 have the independent square-root expectation
`0, 128, 181, 255`. Cancel after pixels finish cannot close a new panel; stale identity cannot authorize a
dependent operation; valid identity adds no entry; returned `E_OUTOFMEMORY`, an actual throwing `Job::Run`
translated by the scheduler's `bad_alloc` catch, and actual donorless fill
retain their panel/original image and release their barrier. Ten-second bounds detect deadlocks, never gate speed.
Replacement deliberately reuses persisted document/layer IDs after the old pixels finish and proves session serials
prevent publication. Adjustment-layer metadata settles once without destructive pixels; a removed target returns
`Stale` without an engine history entry.
Production OK/write activation and dependent caller checks remain in P1.1c.3c–d.

```powershell
.\validate-skills.ps1
.\format.ps1 -Check
.\test.ps1 -Platform x64 -Configuration Debug
.\test.ps1 -Platform x64 -Configuration Release
.\test.ps1 -Platform x64 -Configuration "ASan Debug"
.\build.ps1 -Platform ARM64 -Configuration Debug
```
