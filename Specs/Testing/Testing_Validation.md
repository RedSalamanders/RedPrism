# RedPrism validation

Status: current normative repository contract
Last reviewed: 2026-10-01
Owner: `test.ps1`, `Tests/*`, and `RedPrism.exe` diagnostic switches

## Scope

This contract owns how RedPrism is proven. Domain specs name the behavior; this file names the gates that MUST pass
before a phase is Done. Screenshot policy for chrome lives here so later phases do not invent a second rule.

## Entrypoint

### Detached basic raster edits

`DocumentTests` runs `rastereditwork.*` for literal legacy Fill/Clear/Invert bytes, Gray8 masks, transformed
selection coordinates, sparse fill/base/tile inputs without shared materialization, carried mask growth,
exact/lower/invalid budgets, memory floors and pre/mid-tile cancellation. `rasterwork.stretch.*` and
`rasterwork.thumbnail.*` use independent nearest-coordinate literals for dense/nested sparse reads and
cancellation. Owner publication covers one-entry Undo/Redo and refusals of stale tokens or malformed candidates.

The WARP application self-test runs `rasteredit-host.*` through all five production commands on image and mask
targets at gates before and after preparation. Held states retain selected input/history/whole reservation while
view input remains usable. Controls cover early and late cancellation, returned/injected allocation errors,
capture/admission failure, stale image/transform/full mask/selection/generation/restore/document, younger Undo,
and aggregate release of a large sparse canceled result on a worker. No-selection Delete remains metadata;
Clear without selection adds no entry. Terminal states retain no tickets/barriers/host-input or memory charges
and perform no hidden Present. Gate deadlines detect deadlocks, not performance thresholds. All x64 suites and
ARM64 cross-builds remain required; pending distortion callers and real-loop timings stay open in P1.

### Clipboard and copy preparation

`clipboardtransfer.*` runs a dedicated MTA test worker and restores saved user clipboard contents. Its independent
literal PNG/DIB fixtures prove exact premultiplied versus straight bytes, PNG priority, separately inspected DIB
fallback and hard/lower/wide budgets. Actual OS calls prove reads after immediate owner destruction, a real held
clipboard, sealed stale retries and complete/partial/empty publication. Every injected allocation/native fault
must be reached; pre-empty cancellation preserves content, and late cancellation finishes both independent
formats. A gated delayed renderer proves the API worker remains pending until explicit external release, then
captures the sequence after both formats render. Individual foreign-thread joins have five-second failure bounds;
the complete suite has a separate 60-second bound. Test-only one-shot readiness retries do not qualify production
timers, host input ordering, shutdown or G1. The exact held sequence is checked at its placement stage, without
assuming a later-open sequence remains equal.

`copypixels.*` uses literal BGRA/Gray8 grids, independent pixel-center mappings and rectangle/cell intersection
areas. It covers clipped origin, transient sampling, semantic mask outside bytes, sparse/merged inputs, invalid
grids/formats, reached cancellation/observer allocation failure and independent memory floors. Metadata-only
16 MP sparse inputs make omitted private/canvas/output/AA planes visible above bookkeeping headroom. It does
not qualify atomic Cut history or production host admission.

`owner-work.*` uses independent leaf mocks and literal round-robin call order. It proves one leaf per pass,
pending versus ready, bounded/duplicate/self registration refusal, quiescent detach/reused slot, recursive pump
refusal and callback mutation refusal. Actual Application GPU/timer integration remains F5c.

### Detached gradients

`gradientwork.*` keeps the independent legacy linear/radial/opacity tolerances and literal pixel-center BGRA/Gray8
expectations for transparent source-over, selection exterior, placed/stretched masks and sparse inputs. Invalid
finite projection arithmetic, enums/colors, exact/lower budgets and pre/mid-tile cancellation publish no result.
`gradientstate.*` proves metadata-only deferred openings, independent acquisition/settings clocks, active/latest
publication order, no-line redrag, refusal before synchronous settlement mutation and exact named apply/Undo.
`gradientresource.*` checks independent coverage/output/mask/metadata floors, shared last-owner retirement,
retained source/private tiles and callback-borrow termination without shared sparse materialization.

`gradient-host.*` exercises the actual preview and selected commit callers at event gates. Capture follows full
reservation; input/history/tickets survive early/late cancellation. Admission/capture/worker OOM, returned statuses,
source/selection/generation/restore/opening changes cannot authorize dependent edits. Coalesced endpoints and
palette refreshes finish at the latest settings with no history entry. Image/mask commits use literal pixels and
one named entry; Undo drops an uncommitted line. Existing GPU checks await acknowledgement, retaining their
canvas-vs-raster tolerance and adoption count. Full x64 suites and ARM64 cross-builds are required. No event-gate
deadline is a performance assertion; real-loop drag/settle timing and owner upload/history budgets remain open.

### Detached lifted-pixel moves

`pixelmovework.*`, `pixel-lift.*` and `pixelmovestate.*` keep the literal coverage/source-over, transformed
landing, duplicate, transparent-source, mask-carry and outline expectations. Deferred openings allocate no
pixels. Refused preview/apply candidates remain owned by their caller; exact opening/generation/settings checks
prevent an older worker from changing a newer gesture or retargeting a changed source.

`pixelmove-host.*` runs production preview and selected pointer-release/nudge jobs at event gates. It covers
admission/capture/worker failure, early and late cancellation, stale source generation/selection/document,
one named entry and Undo, and a younger Undo queued behind the commit. A younger Shift nudge retains its
event-time ten-pixel offset after the modifier changes. Coalesced previews show an acknowledged active result
and then the latest offset. Refused nudge admission posts one notice and preserves source/history. A canceled
large graph proves retirement on a worker. Complete migrated job/stroke/opening aggregates retain release headroom
before capture; a small component alone cannot lower the complete aggregate below its retirement threshold.
Empty source-region moves preserve history. All terminal states drop tickets, barriers, input and memory charges
without hidden presentation. Full configuration qualification is required; floating
transactions, display geometry, owner history/first upload and real-loop bounds remain separate P1 work.

The new opening suites add 33 independent primitive/opaque-worker assertions for literal move/partial-alpha
duplicate output, placed/sparse sources, wrong source/grid/selection/AA/canvas/refusal, moved-from/second-install,
cancelled preparation and exact complete-node release ownership. Actual host controls count one opening
preparation across two coalesced previews plus commit, check latest literal pixels/outline and one undo entry,
and invalidate/rebuild on gesture reopen. Every terminal commit clears the cache and returns reservations;
weak-owner controls check final retirement. These additions enter native qualification and make no drag-time claim.

### Retained selection and history ownership

`DocumentTests` runs `selectionstorage.*` and `history-accounting.*`: empty/shared/moved geometry, reserved
capacity without changed geometry, each selection identity charged once with live exclusion, equal-geometry
no-op preserving redo, selection-only byte-budget trimming, and a valid owned Undo result after trimming erases
its future entry. Normal Undo/Redo restore the expected document, active id and revision. Existing history,
saved revision, pending-edit, mask/crop and 256 MiB trim numeric expectations stay unchanged. Full matrix and
real application-loop qualification are recorded in [HistoryStorage_2026-09-30.md](../TestRuns/HistoryStorage_2026-09-30.md).

### Prepared history publication and retirement (P1.9 H1 foundation)

`DocumentTests` includes the five foundation prepared-history groups: `HistoryStateViewTests`, `PreparedHistoryTests`,
`HistoryPreparationFaultTests`, `PreparedRasterHistoryTests` and `HistoryRetirementBoundTests`. Their expectations
cover immutable captured views and owner/epoch/revision invalidation; detached edit/restore preparation and
allocation-free publication with stale, foreign, moved-from and malformed candidates refused; cancellation and
`std::bad_alloc` leaving live history/document state unchanged at the named preparation checkpoints; basic raster,
gradient and pixel-move apply/Undo/Redo ownership; and conservative graph bounds for current, pending, past, future
and additional raster owners. Raster-bound cases include sparse bases/tiles and a possible lazy materialization
cache, shared-DAG occurrence overcount/overflow refusal, and refusal beyond depth 128 without materialization.

The WARP `rasteredit-host.*` self-test also injects `std::bad_alloc` at five history-preparation stages for the
basic Invert image commit: `BeforeSnapshot`, `AfterSnapshot`, `Entry`, `StackGrowth` and `TrimIndex`. Its expected
result is `E_OUTOFMEMORY`, unchanged document/history, and retirement of the aggregate payload off the owner
thread. The required x64 Debug, Release and ASan Debug suites and all ARM64 clean cross-builds pass;
[current evidence](../TestRuns/PreparedHistoryPublication_2026-10-01.md). Other H1 callers, production Undo/Redo,
H2/H3/H4, P1 and G1 remain open.

Destructive color/filter commits add `PreparedColorHistoryTests`: literal Gamma 2
BGRA output and Undo/Redo, applied settings/identity Levels reopening, independent saved revision,
stale generation/history epoch, foreign/moved/single-use authority, five-stage cancellation/allocation refusal,
and retired lookup/capacity ownership. Its application gate adds five history-allocation failures, admitted
capture refusal and a changed-history-epoch result, retaining source pixels and seeded redo. Harness waits pump
real admission/capture/completion events; production commands do not wait. All 45 independent assertions and
actual host cases pass the full x64 configuration matrix; ARM64 clean cross-builds pass
([evidence](../TestRuns/ColorHistoryPublication_2026-10-01.md)). Suspension tests include an unrelated memory
wake while the worker remains held: a wake alone cannot prove that an owner continuation has suspended.

Adjustment opening/closure add independent `AdjustmentClosureTests`, `AdjustmentOpeningTests` and
`AdjustmentNestedTests`. Literal parameter records/source bytes cover every kind, preview on/off, stored-record
no-op versus implicit-default promotion, nested named transactions, template/redo/saved-revision retention and
single-use/moved/foreign/stale authority. Same-sequence mismatched settings are refused. The named allocation and
cancellation checkpoints MUST be reached before unchanged-state assertions; closure reuses its opening and cannot
claim the unreachable BeforeSnapshot stage. Every acknowledged End advances generation, including no-op/nested
closure, and an old whole-document token is refused.

The actual WARP host checks complete admitted capture/reservation, retained source/redo/transaction on failure,
late cancellation drop, precise terminal receipt and off-owner retirement for all six kinds. Opening checks literal
below-composite bytes, unchanged live visibility and history baseline, GPU mapping/target release before detached
Begin preparation, device recovery after copied pixels and a younger queued rename that waits for opening and OK.
An open transaction disables Undo/Redo commands; retention assertions therefore inspect its immutable redo stack,
not `CanRedo()`. No production command pumps a nested wait. Final identities and configuration results live in the
[opening/OK record](../TestRuns/AdjustmentOpeningAndOkHistoryPublication_2026-10-01.md).

`ColorDismissalTests` uses independent literal state for Levels/Gaussian/Content-Aware Fill, nested outer history,
stale lifetime/settings/active/mask/crop/history epoch and moved/foreign/single-use refusal. A real preview lookup
must survive application solely in the prepared retirement owner and expire when that owner releases it. Destructive
dismissal changes neither generation nor any document/history/template state. Metadata Cancel still performs End.
The actual owner/worker host tests require repeated queued/active cancellation to retain one request, held and
already-computed OK to drop before Cancel closes, construction failure to retain the panel/error and a stale old
opening to leave the new panel intact. Younger save/tool/card/Close Project must stay Pending through that close.
All owners/reservations return, release is off owner and hidden Present stays unchanged. Ordinary fixture cleanup
waits for real acknowledgement, with old failure injection excluded from its new cleanup descriptor; the direct
engine lifetime-invalidating reference case remains explicit. Nested-token refusal must compare the same sequence
before and after closure and prove the token was current beforehand. Identities/results are tracked in the
[cancellation record](../TestRuns/PanelCancellationHistoryPublication_2026-10-01.md).

`DocumentSizeHistoryTests` adds 50 independent assertions for literal colored extension and nearest resize pixels,
masks/selection/crop/active target, resolution-only Undo/Redo, nested/no-op history, moved/foreign/single-use
authority, an active-layer-only change with a still-valid document token, five history allocation and cancellation
stages, and retained lasso capacity/ownership. The actual
size-card host gate covers 22 terminal/ordering scenarios, including admitted capture refusal, all five history
allocation points, epoch-only staleness, retained redo/source and off-owner complete aggregate destruction.
The full frozen x64 suites and ARM64 cross-builds pass ([evidence](../TestRuns/SizeHistoryPublication_2026-10-01.md)); these correctness gates make no timing claim.

`.\test.ps1 [-Platform x64|ARM64] [-Configuration Debug|Release|'ASan Debug'] [-Rebuild]` builds through `build.ps1`,
then runs every assertion below. It MUST exit 0 only when all of them pass. Expected nonzero exits from isolated
negative-test children (`HostTests.exe --asan-probe`, `RedPrism.exe --no-such-switch`) MUST NOT become the test
entrypoint's success exit code and MUST NOT be reused as the pass/fail signal for a later gate.

ARM64 runtime qualification requires a native ARM64 host. Cross-built ARM64 binaries are produced by `build.ps1` only.

The hidden WARP self-test runs under a bounded timeout; a run that does not finish in time fails with the process id
reported, never killed.

A test run never waits on a dialog. In Debug and ASan Debug, `RedPrism.exe`'s harness modes (`--self-test`,
`--screenshot`, a `--demo`, a `--bench`) install a CRT report hook (`_CrtSetReportHook2`) that ends the process at the
first `_CRT_ERROR` or `_CRT_ASSERT` report with exit code 3, the report on stderr, instead of the report being printed
while the run still exits 0 (a debug-heap corruption message would not otherwise stop it). It is installed once the crash
handler and the kernels are initialized. Every harness mode and `--crash-probe` also set
`SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX`, so a fast-fail that bypasses the crash handler, or an unreadable drive,
opens neither Windows Error Reporting's dialog nor a critical-error box.

Each of the four test executables first calls `RedPrismTestFailureReports::RouteAwayFromDialogs()`
(`Tests/Support/FailureReports.h`). In every configuration `_set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT)`
and the same `SetErrorMode` flags leave an abort, a fast-fail or a crash without a dialog. In Debug and ASan Debug a
wide report hook (`_CrtSetReportHookW2`, which the CRT calls for every report, ANSI ones included) ends the run at the
first CRT report (an STL range check, a CRT assertion, a debug-heap error) with the report on stderr and exit code 3, the
dialog's Abort; the CRT's own report modes route to stderr as well. A loader failure before `wmain` (a missing DLL) is
outside this routing: only the process that starts the suite can set the error mode its children inherit. The hidden
switch `--failure-report-self-test` of each executable fails such a check on purpose.
`Tests/BuildProcessTests/FailureReportTests.ps1` runs it for all four first in `test.ps1`, each run bounded, the way
`--asan-probe` proves AddressSanitizer: exit code 3 with the report on stderr in Debug and ASan Debug, exit code 0 in
Release, which has no such checks. A run that does not end is blocked on a dialog; the test ends its own child and fails.

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

### Prepared selection publication (P1.6 core)

`selection-command.*` uses literal 4-by-1 pixels/mask, full-canvas geometry and coverage bytes for Select All,
absent versus explicit empty Deselect, no-op/redo, named nested transactions and a distinct later saved marker.
Both operations reach each SelectionSnapshot/BeforeSnapshot/AfterSnapshot/Entry/StackGrowth/TrimIndex cancellation
and bad_alloc boundary before unchanged-state assertions. Active/mask-only changes prove generation/epoch did not
change. Foreign, copied/moved-session, moved-from, single-use, draft and stale results cannot publish.

`selection-move-closure.*` independently checks x=1..2 moved to x=2..3, End's one generation and Cancel's two,
exact history title, Undo/Redo, retained seeded redo, no-op and named outer transaction, distinct saved marker,
source/mask/crop/target preservation and independent authority-field refusals. End and a Cancel with an additional
real transaction change each reach every AfterSnapshot/Entry/StackGrowth/TrimIndex cancellation/OOM boundary.
A fully undone Cancel proves that changed-history construction checkpoints are absent. Failed preparation
retains the live move and complete history; a separate clean candidate can still close. Pre-cancelled, foreign,
copied/moved-session and moved-from/single-use candidates are refused. No timing threshold is asserted.

The native configuration matrix qualifies these preparation methods; production command/pointer ordering,
live gesture/sample ownership, deferred job retirement, general Clipper bounds and G1 remain separate gates.

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
| RAW corpus preparation | Offline `RawCorpusTests.ps1` validates the real nine-extension manifest, allowlist, roles, original license/legal-text hashes, safe root and refused downloads; present matching corpus files additionally qualify exact image sizes/hashes and local license/manifest snapshots. Missing corpus byte controls explicitly SKIP in ordinary tests; P4/P5 required-file qualification must run without SKIP. |
| Unknown switch | `RedPrism.exe --no-such-switch` exit 2 |
| Crash probe | `RedPrism.exe --crash-probe <directory>` (a directory under `.build\`) exits nonzero and leaves exactly one `RedPrism-*.dmp` above zero bytes with a `pending.txt` naming it ([`Core_CrashHandling.md`](../Core/Core_CrashHandling.md)); the directory is removed afterwards |
| Failure reports | Each test executable's hidden `--failure-report-self-test`, run first by `test.ps1` through `Tests/BuildProcessTests/FailureReportTests.ps1` within a bound, ends with exit code 3 and the CRT report on stderr in Debug and ASan Debug and with exit code 0 in Release; a run that blocks on a dialog fails the gate |
| WARP self-test | Hidden `RedPrism.exe --self-test --warp` exit 0: visible DxUi surface within 64 MiB with ≥ 1 composite, canvas surfaces allocated, a second frame does not re-composite the settled document, a splitter drag through `DispatchPointer` widens the layers panel by 40 DIP and narrows the hole after the layout re-sync, the foreground well opens the picker and a sampled color commits into the session palette, a cancelled background edit leaves it, X/D swap and reset, the opacity stepper records one `Layer Opacity` undo entry at 50 %, a painted layer shows the Move tool's box, a pointer drag commits one `Transform Layer` entry, Free Transform previews a typed rotation until Apply, Escape cancels, Shift+Right nudges ten pixels, Flip Canvas Horizontal and Flip Layer Vertical mirror, a Marquee drag selects a whole-pixel rectangle and a Shift drag adds one, M switches the shape once per press, dragging inside the selection moves it as one `Move Selection` entry, Escape and Ctrl+Z during such a drag put the outline back without an entry, a click inside deselects, the wand selects a painted layer's pixels with exact bounds, Ctrl+Shift+I / Ctrl+A / Ctrl+D drive the Select menu, three polygonal clicks and Enter close a `Polygonal Lasso`, Expand from the header grows it by five pixels, Escape cancels an outline, a Brush stroke through the pointer path with the compute engine shows a live preview and commits one sparse `Brush Stroke` entry drawn from the adopted texture without materializing, the Eraser clears it, `[` `]` Shift-`[` and `5` change the tip, Escape drops a stroke, Clone Stamp stamps a mark from a source, Spot Healing blends it away, Smudge drags it through a live warp, Blur commits, a Gradient previews and applies with Enter after a digit set its opacity, a Shape drag adds a Rectangle layer, Shift-U switches the kind, Escape drops a draft, the Eyedropper samples the shape's color, Fill with Foreground Color fills a Marquee selection, Ctrl+X cuts it to the clipboard (mirrored to the system clipboard), Ctrl+V pastes it in place as a new layer, Ctrl+I inverts it, a Ctrl-drag moves the selected pixels through the live preview as one `Move Pixels` entry with the outline following, Ctrl+Right nudges them, Delete clears them, Ctrl+T on a selection floats them (Escape restores exactly, Enter merges one `Transform Selection` entry), Ctrl+J copies them to a layer, Add Mask hides them, Ctrl+Shift+C copies the composite, Ctrl+L opens the Levels panel and its output white previews black through the display document, Escape restores, an input white of 128 turns the gray white and Enter records one `Levels` entry, the Filter menu's Gaussian Blur applies one entry, Shift+Delete opens Content-Aware Fill on a red block filled into a selection of the white plate and Enter restores it as one `Content-Aware Fill` entry, with everything selected the fill reports no source, refuses Enter and closes on Escape, a Hue/Saturation adjustment layer opens its panel and a hue of 120 rotates the red beneath it live on the canvas, Enter keeps its record, Shift+= steps the blend mode, a rail click through the pointer path activates its tool (plainly, with the window focused, under the button's tooltip, and held across settled frames after a tool switch, after a click on another rail button and with the opacity stepper focused), the File menu's flyout anchor lies under its item on screen and a point on the open color picker card counts as chrome while the hole beside it counts as canvas, every interactive chrome control has an accessible name and the window's UIA root exposes the DxUi tree (its parent link, at least eight named elements, a hit under the menu bar), a settings file of the self-test's own reloads live (a light theme, the sample ring off and a 300 DIP layers panel apply, a broken file changes nothing and is not rewritten, restoring it applies again), the high-contrast palette applies and `RefreshTheme` follows the system state, and a save records the window placement; an image opened with the forced Add as Layer answer joins the document as a layer and with New Project replaces it by an unmodified untitled document of the image's size, a modified document survives New under the forced Cancel answer and is replaced under Don't Save, Close Project empties the shell (status `—`, a frame still renders) and New brings a document back, Import Images adds two files as one `Import Images` entry, Move Layer Up and Down swap siblings as `Reorder Layers` entries, Group Selected Layers wraps the active layer in a folder, Ctrl+Alt+G clips a layer to the one below and releases it, each function the UI spec lists as unimplemented raises the notice count by one and changes neither the document, the history nor the tool, Canvas Size shrinks the document to half in percent with the ratio locked around the top-left anchor on Enter and grows it with a white Canvas Extension layer from the bottom-right anchor, Escape cancels a pending size, Image Size resamples to 325 × 205 at 300 ppi with nearest sampling (the red layer's pixels and box scale with it) and three undos restore the document, New Canvas creates a 300 × 200 unmodified document, and the Crop tool frames the canvas when chosen, resizes the frame to 640 × 400 by a handle drag through the pointer path, crops on Enter as one `Crop` entry, moves the frame by a drag inside it, drops it on Escape, squares it through the header's ratio, cancels, is chosen again and undoes the crop with Ctrl+Z (its own whole-canvas frame takes no Undo) and leaves the tool without a frame, the Rename Layer card renames the active layer through typed characters and Enter and leaves the name alone on Escape, Merge Down composites a blue layer onto a red one into one trimmed layer named after the lower one and undo restores both, the Layer menu's mask commands gray out without a mask and a layer's mask is targeted again once another layer was chosen (Target Layer Mask and back, with the captions and the row's badge tone; Unlink and Disable Layer Mask as their entries with `Mask off` on the row; a Shift-press on the row's Mask badge choosing that layer and turning its mask on again; a press on the row's name targeting the pixels; a plain press on the badge the mask), the rendering steps listed in [`Rendering_Composition.md`](../Rendering/Rendering_Composition.md) Validation pass (actual pixels 1:1, wheel and Zoom tool zoom, device removal at every frame stage rebuilt and drawn on, a failed canvas that keeps the chrome, a 17 000 pixel document through its proxy and read back at full resolution, readbacks without the preview, the adopted curve equal to its raster, a gradient on another layer previewing there, surfaces dropped while minimized or occluded, the minimum window size, the screenshot exit codes, repeated failures that ask to close through the save prompt, a gradient left pending on one layer when another is chosen ending with its own commit so each layer draws exactly as its raster), the keyboard steps listed in [`UI_ApplicationChrome.md`](../UI/UI_ApplicationChrome.md) Validation (every key through the window procedure: replacements, text fields, AltGr, the Command List, the menu bar, the Move tool's digits, the displayed chords), the session checks (a typed opacity typed back to its start, closed by a command, carried to a new document, cancelled and committed; the Rename card's own layer; Cancel at the save prompt keeping a panel, the size card, a crop frame, a shape draft and a pending transform; a live stroke on a clean document; Save under an adjustment layer's panel; a folder project saved in place; an image batch whose first file fails; Canvas Size over a pending gradient; a copy the system clipboard refused; capture loss mid-stroke and mid-drag; a cancelled splitter drag; a leave without a capture; a stroke dragged over the layers panel keeping the pointer; Space across a focus loss; Alt-press, Undo and a drag; a close inside a chrome callback; the session end and its settings save) and the integration checks of the 2026-09-23 review (a reopened project keeping no mask target; a dropped image centered on the drop point on the canvas and on the canvas's middle off it; Load Mask and Load Layer Selection saying why a 2100 × 2100 checkerboard loads nothing; budget refusals raising one notice each for Layer via Copy with and without a selection, Gaussian Blur and Merge Down; Copy and Copy Merged applying a pending gradient first; Merge Down under a pending gradient holding it; a pending mask gradient following Paint, and X and D flipping and resetting the mask palette; the rail's wells showing that palette in black and white whatever the colors, a well opening no picker while a mask is the target and a picker open on a well closing unchanged once one is; a typed "2.5" in Gaussian Blur's radius kept as typed; the Gradient Map picker closing with its panel and the next well pick reaching the palette; Image Size's unit pick without resampling storing the unit; the rail parsing no SVG while settled and at most two per tool switch; the palette zones, the curve editor, the levels bars and the hue spectrum reachable in the UI Automation tree; a press on the spectrum's hue bars moving no handle while one in its handle strip does), what a pointer move costs the chrome and its UI Automation snapshot ([`Core_PerformanceAndResources.md`](../Core/Core_PerformanceAndResources.md), "Pointer input and the chrome": a Hand pan writing and preparing nothing, a Space pan, a wheel step and a brush move writing only the overlay and the zoom text, a transform drag rebuilding no layer row or menu, a settled sync writing nothing, no snapshot published without a listening client and one per preparation with one, `WM_GETOBJECT` and the in-process root publishing a waiting snapshot, a card published without a client) and the canvas cursor's per-position decisions (no layer scan with auto-select off, one scan and one selection test per position, none outside the selection's bounds, the cursors unchanged), then 0 DxUi bytes and 0 canvas bytes when hidden |
| DocumentTests | `DocumentTests.exe` exit 0 (graph, `ProjectStore`-identical validation, history budget, mask placement helpers, collapsed rows and their mask state, clip release on delete, legacy insertion/deletion/naming semantics, palette, transform geometry/drag/snap/session/duplicate/group/mask/flip/distort cases ported from the legacy transform suites, selection geometry/coverage/session/move/modify/load/marquee and wand kernel/session cases ported from the legacy `SelectionTests` and `MagicWandTests`, sparse rasters and the brush kernel, strokes, masks, opacity, curves, crossings, snapshots, budgets, keys, expansion and selection clips from the legacy brush suites, listed in [`Rendering_Brush.md`](../Rendering/Rendering_Brush.md), and the retouch cases: spot healing for every mode, clone stamp alignment and tip families, blur, smudge, liquify, warp tiles, gradients, shapes with the redraw on scaling, and the eyedropper; and the pixel edits ported from the legacy `SelectionEditTests` and `SelectionClipboardTests`: fills, Clear and the Delete key, invert, masks from the selection, pixel moves and nudges, the clipboard and the floating selection transform; and the color adjustments and filters ported from the legacy `LevelsTests`, `HueSaturationTests`, `ImageAdjustmentTests`, `FilterTests`, `AdjustmentLayerTests` and `BlendShortcutTests`: levels, hue/saturation, exposure, gradient map, grain, blurs, noise, lens correction, adjustment layers edited through the panels, the blend-mode step and the HSL formulas; and the content-aware fill ported from the legacy `SmartEditTests` (`smart.*`): the stripes continue through the filled block, a flat background reconstructs and undoes, Cancel and a no-source fill leave the original, the grid grows to a selection past the layer's edge; and the Layer menu edits ported from the legacy `LayerTests`, `GroupingSelectionTests` and `LiveMaskTests` gates (`order.*`, `wrap.*`, `clip.*`, `clear.*`): sibling swaps and their refusal, single, folder and empty grouping with undo, the clipping toggle with a shared base, and the cleared session; and the sizes ported from the legacy `CanvasSizeTests` and `ImageSizeTests` (`canvasdraft.*`, `canvas.*`, `imagedraft.*`, `imagesize.*`) and the crop cases from the legacy `CropTests` (`crop.*`) listed in [`Document_Tools.md`](../Document/Document_Tools.md); and the merge plan and merge (`merge.*`: no plan for a bottom layer or an empty folder, Merge Down onto the lower layer in the anchor's slot with clips re-targeted and undo, Merge Group replacing the folder, a stale plan refused) |
| CompositionTests | `CompositionTests.exe` exit 0: the golden table in [`Rendering_Composition.md`](../Rendering/Rendering_Composition.md) including the C12 adjustment layers and HSL blends (blend modes, order, raster masks with placement, live masks and chains, clipping stacks, folder masks, transforms, residency and caching, viewport, sparse upload, stroke preview and adoption, compute brush parity with the CPU kernel) |
| IoTests | `IoTests.exe` exit 0: the list in [`IO_ProjectFormat.md`](../IO/IO_ProjectFormat.md) plus PNG/Gray8/JPEG codec round trips, remaining aggregate project budgets before allocation, adaptive project probes/reopened-input ceilings and detached ordered image imports, HEIF clean error, import onto a document (a PNG is not a ZIP project), and the settings store (`settings.*`: file names and paths, parsing with per-field fallbacks and rejects, serialization keeping unknown fields and a newer schema, the cold load of a missing, invalid and unreadable file, the live load, and saves that back up an invalid file, keep unknown fields, leave no temporary and create the directory) |
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

P1.2 extends `coloreditdeferred.*` with immutable GPU lookup/source publication, selection coverage reuse,
pre-cancellation, preview off/identity/Cancel/reopen and stale selection checks. Its CPU commit uses independent
literal Levels pixels and must not reuse the GPU preview as a CPU result. Live adjustment lookup publication may
display an intermediate result without rolling controls back. `CompositionTests` compares pass 8 against CPU
filter/selection mix and ordinary composition for every kind, including partial alpha, layer-space grain,
translated/scaled placement, masks, opacity, clipping, folders and unrelated layers. Lookup counters prove reuse;
hidden release/device recreation, settled zero-pass frames and committed readback bypass are explicit gates.
The real host event gates additionally prove live worker lookup publication, the final GPU pixels, immediate
preview off while the worker is held and cancellation of late lookup work.

### CPU dispatch and instruction boundary (P0.8)

The synthetic ISA parser controls cover DxUi typography's exact `LEA RCX,imagebase; MOVZX EAX,DL; DWORD load;
ADD; JMP` form. They require the anonymous same-owner untyped PDB start and retain negative controls for wrong
register/opcode, extra or missing instructions, invalid symbols, contribution/decoded-target bounds and real
vector code beside the valid table. This proves data recognition; it does not grant the containing object or
function an instruction exemption.

The exact WIC RDX-base/RCX-target dispatch additionally has seven-DWORD fixtures with required anonymous PDB
start, malformed dispatch/symbol/target/bounds negatives and adjacent real-vector rejection. The standalone
parser suite has 226 controls including 29 RDX-form cases, 45 exact Menu R9-base/R8-target cases and 24
signed-byte RBP+disp32 cases. The signed form requires the exact MOVSX/LEA/load/ADD/JMP encoding, an anonymous
same-owner table start including its biased default entry, and decoded target bounds; wrong REX/opcode/frame
base/SIB, noncontiguity, invalid symbols/targets and adjacent real vector code remain rejected. The Menu form
requires its byte-index load, unchanged base/indices/targets, anonymous PDB start and decoded-target bounds;
missing/changed operands, noncontiguity, malformed PDB evidence, truncation and adjacent real VEX remain rejected.
[Original forms record](../TestRuns/IsaTableForms_2026-09-30.md).

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

`job-stage.*` covers successful multi-stage deferral, retained reservation/FIFO, exact resumed generations,
duplicate/stale/slot-reused tickets, resume inside a hook without worker re-entry, false-hook retirement even
after requesting resume, cancelled/failing stages and suspended shutdown. An external owned event publishes
readiness through a joined wait callback. Pending-only owner work prevents idle settlement without claiming
UI work; ready Pump executes under the reentrancy guard and refuses callback-time registration/removal.
Synthetic memory checks prove successor admission remains blocked through suspended, resumed and final apply
stages. State/event references are independent of elapsed-time performance; no hook waits on another stage.

`gpu-capability.*` covers literal completion-route prerequisites and unavailable/failed-query combinations,
plus real WARP immediate-context probing, null/deferred/mismatched-device refusal and report reset.
`gpu-work.*` uses independent BGRA coordinate formulas for cropped row-pitch copies and single-pixel reads.
Actual scheduler stages copy mapped rows only on a worker and acknowledge them on the owner. Cases include
cancel after mapping, a device rebind while rows remain lent, duplicate lease refusal, eight-slot admission,
two-copy in-flight bounds and 64 MiB staging saturation that waits for an owned acknowledgement message.
Shutdown holds an actual mapped copy stage until the shared shutdown event, checks that its pointer remains valid
while the scheduler joins, drops its final callback, and releases the mapping/reservation only after join.
The queue runs without an HWND or Present. On a supported WARP device, additional copies explicitly select
the EnqueueSetEvent fallback, including cropped rows, cancellation and lent-device lifetime.
Full 2048 squared copies additionally inject first-map busy and persistent busy on both completion routes.
One additional real DXGI event permits one further nonblocking Map; persistent busy has one failed delivery and
no lease or partial pixels. Holding owner consumption of the recovery event proves cancellation, replacement
and shutdown while that stage remains pending. Negative rearm/enqueue/removal seams prove exact errors and
released ownership. Reusing the queue for a full then cropped copy proves each admission gets a fresh recovery
allowance, with independent coordinate-formula bytes and bounded storage. These are injected lifecycle failures,
not claims of physical device removal or a maximum GPU-event delay.

`document-readback.*` exercises the actual tiled worker graph and distinct utility targets, with a serial BGRA
coordinate formula for full, cropped and single-pixel output. It verifies one reservation, immutable input,
owner-only recording/mapping, worker-only copying, adopted output, hidden display release and settled display
pixel preservation. A folder/clipping/mask/adjustment fixture compares the asynchronous result with the committed
synchronous traversal at tile seams. Actual replacement WARP devices prove whole-graph restart, no partial
publication and failure after two restarts. These are deliberate rebind tests, not physical device-removal tests.
An injected persistent busy second tile proves that an acknowledged first tile is never published as a partial
Raster: exactly two copies, three Map attempts, one readiness barrier and one terminal failure, with no restart.

The application WARP self-test holds a real Merge worker with owned events after final GPU acknowledgement.
A younger Invert must apply after exactly one merge history entry. Cancel stays pending until acknowledgement;
injected failure, stale opacity and device replacement prove exact receipts, unchanged pixels on drop, hidden
completion without Present, and release of utility, staging and scheduler ownership. At the CPU gate lent mappings
and utility targets are zero. Replacement after detachment retains authoritative bytes and performs one CPU
preparation; the separate tiled-graph tests still prove bounded restart before acknowledgement. Merge adds all
five history-allocation faults, readback/probe capture refusal, worker probe allocation refusal, epoch-only
invalidation and probe cancellation/staleness with seeded redo preserved. A 30,000-square blank canvas completes
its no-plan command with a metadata-only reservation, no readback capture and no history change. Every owned probe
and readback aggregate retires off the owner; each terminal case leaves no barrier, GPU work or reservation.
`MergeHistoryTests` and `MergePlanProbeTests` add 71 independent literal-pixel, trim/group/clip/budget, immutable
planner, no-plan, exact-authority, moved/single-use, fault and cancellation assertions. Native qualification is
tracked in P1.9 H1. Remaining readback callers and G1 timing remain open.

`HistoryRestoreTests` adds 63 independent restore-publication assertions: literal pixels/opacity/mask/active id,
actual-restore crop removal/no-entry crop retention, no-document Redo, selected-snapshot memory floors,
default/foreign/moved/single-use authority, exact history epoch and both directions of all three restore allocation
and cancellation stages. A later saved revision and newly begun unrecorded lasso cannot corrupt live state.
The production self-test adds 28 `history-restore-host.mode=` scenarios, including ordered younger edits,
selected receipt lineage across document removal, empty/nested no-op, stale active/mask/crop/generation/epoch/lasso,
capture/worker/history refusal, cancellation acknowledgement and complete aggregate retirement off the owner.
Every terminal case requires no hidden Present and returned reservations/barrier/ticket. Crop/opacity host tests
wait for acknowledged real restores before inspecting results; pending-edit consumes retain their immediate rules.
All six configurations qualify this slice ([evidence](../TestRuns/HistoryRestorePublication_2026-10-01.md));
these functional gates assert no latency threshold.

`ImportHistoryTests` adds 55 independent reset/baseline/edit/trim-publication assertions, including literal
Undo/Redo and all six replacement/edit construction and cancellation checkpoints with the old state preserved.
`ImportPublicationTests` adds 157 complete session assertions: initial/extra/existing image semantics, exact
authority/history staleness, moved/foreign/single-use refusal, later saved revision, project active-id/empty
document rules and refusal without mutation. Twelve controls cover both project and first-image replacement
snapshot construction, injected allocation/cancellation, exact old history/target and literal source pixels.
Actual import/project-open host tests additionally require
capture reservation, exact history faults, isolated active/mask/crop/epoch invalidation and aggregate retirement
off the owner. The live-context controls are described below. Final native qualification is recorded in
[ImageImportHistoryPublication_2026-10-01.md](../TestRuns/ImageImportHistoryPublication_2026-10-01.md);
functional gates make no measured latency claim.

`HistoryEntriesTests` adds 28 independent immutable-container assertions: zero handle copies during capture/read,
captured integer contents, clone-before-mutation, empty/moved/swap state, weak-owner retention and four concurrent
readers with a separate cloned writer. Deterministic throwing-handle fixtures prove partial copy refusal leaves
root identity, contents and original ownership intact for push/pop/erase; they do not inject the vector allocator.
Existing prepared-history allocation checkpoints still gate the complete construction/publication boundary.
`OptionalImageThumbnailTests` adds 30 assertions covering empty/explicit derived image caches, literal sparse
nearest samples without materialization, equality/no-op redo, invert/resize/prepared color/prepared merge/CPU
brush pixels, and eager white-to-black semantic mask thumbnails. The legacy retained-thumbnail byte golden supplies
an explicit nonzero image cache. IoTests retains its original mixed-gray fixture and checks all four literal
thumbnail bytes, while successful image decode leaves no thumbnail. Native qualification is P1.9 H2.1/H4.1 work.

`HostTests` command-line cases additionally prove bounded positional paths, `--` leading-dash paths, empty/null/
overlong/count refusal, harness mode conflicts, marker-aware help/self-test preflight and atomic parse output.
Accepted startup paths are copied into an owning request before the argv storage can be released.

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

### Asynchronous sizing and project durability (P1.8 / P1.7)

P1.8 adds `sizingwork.*` document checks for independent extension/nearest/mask bytes, private sparse-source
reads, pre-cancellation with no partial output, full-token checked apply, stale/invalid/replaced-document refusal,
one-entry undo/redo and rounded whole-graph reservation lower bounds. Existing `canvas.*` and `imagesize.*`
legacy numeric expectations remain unchanged.

The production WARP host route additionally holds actual size jobs on owned worker events before or after
preparation. It checks retained reservation/history ownership, copied dimensions/anchor/color/sampling/resolution,
panel commit → size → younger Undo order, early cancellation and a late Finished result after cancellation,
runner allocation failure, exact TooLarge/Invalid/Memory/failure errors, unchanged results, generation/restore/
document/card replacement, hidden completion without Present and complete terminal cleanup. Gate deadlines are
failure bounds, not latency assertions. Kernel cancellation intervals are structural bounds; these events alone
do not measure in-kernel stop latency.

P1.7 project durability checks inspect ZIP methods (STORE encoded PNG, DEFLATE manifest), load an independently
rewritten legacy DEFLATE archive, exercise ZIP and folder overwrite, preserve unrelated same-prefix paths,
require owned staging cleanup and refuse a locked destination while preserving its distinct previous bytes and
resolution. Separate `rotation-fault.*` controls inject every folder rotation/rollback/cleanup stage, retain
exact reported errors, compare literal loaded pixels and byte-identical old trees, and check publication truth.
Backup-only retries cover repeated failed rollback, successful restoration and successful new publication;
unrelated siblings survive. Real cleanup cases cover a read-only file, nested files, a mount-point junction with
an intact external target and the 128-level refusal. Injected flush/close faults remain open.
Native rollback-reader controls hold a descendant manifest open after rotation. One releases at the first
`RollbackRetryScheduled` observation and requires exact restoration despite late cancellation; another remains
open through exactly two event waits/three attempts and requires the exact access-denied error, complete `.old`
tree, cleaned staging and no false publication. Closing that reader then permits backup-only recovery. Test
observers control ownership transitions, never sleep or assert nominal timer latency. Read-only attribute reset
and deep-fixture removal are asserted explicitly.
Recovery timer create/arm/wait faults additionally preserve exact native error 5/32, the byte-identical backup,
staging cleanup and publication truth. Each surviving backup subsequently restores after its reader closes.
The directory fixture locks the backup directory itself; a child-file lock cannot stand in for native sharing
violation. Healthy save and final injected rollback-rename controls require zero recovery-hook visits.

Native `destination-retry.*` controls hold the current destination's child manifest or directory without delete
sharing before publication. Release at the first retry observation requires a complete replacement; exhaustion
and each timer resource failure require the exact old tree and initiating native error. Cancellation at the
retry observation leaves the original destination intact. Healthy and injected final rename failures require
zero destination timer create/arm/wait visits. Every case checks publication count, no rollback before rotation
and complete owned-staging cleanup. These controls bound attempts and preserve ownership; they do not identify
an external reader/filter or assert wall-clock timer latency.

P1.7 F5a adds literal `clipboarddib.*` and `imagecodecwork.clipboard-png-probe.*` byte/metadata controls.
They exercise independent padded/directed RGB, alpha/masks/palette offsets, premultiplication and literal V5
output, invalid/truncated headers, signed/hard budgets, empty precancelled output and unchanged sparse sources.
The 512 MiB size predicate and over-cap null PNG input avoid fabricated borrowed spans. These helpers are not
production clipboard transfer or host-contention qualification; F5b–F5d remain open.

`project-save-memory.*` independently checks refusal of zero/overlong paths, invalid canvas dimensions and excess
layers; acceptance at path/canvas/layer bounds; name/path-sensitive charges; BGRA/Gray8 frame accounting; shared
raster references without charging a frame per asset; sparse-source identity without materialization; and invalid
curve limits. The production WARP `project-save-host.case=*` suite covers thirteen scenarios. Worker cases use owned events:
ordinary successful publication; a younger command held behind the save ordinal and then resumed; cancellation
before publication; cancellation after publication; cancelled close after publication; returned writer failure;
actual worker `bad_alloc`; stale document lineage; a later edit made after publication; pending transform settlement;
pending typed-opacity settlement; allocation failure at owning admission, before Run; and path suffix-space
refusal before Run. It checks pending
receipt/history ownership, reservation retention and retirement, caller-path copying, no destination before
publication, the independently loaded saved pixel/active layer, old path and revision preservation on prepublish
failure, captured revision marking without erasing a later edit, close suppression after late cancellation, younger
command success only after the save stage, persisted transform/opacity with their own history entry before the
captured saved revision, localized failure reporting and full terminal cleanup. These event gates
have bounded deadlock deadlines, not performance assertions. They provide evidence for project save ownership and
ordering; clipboard host migration remains open. Image-import host qualification is recorded below.

`project-open-host.phase=* after=* case=*` exercises 36 actual host-loop gates: probe/decode before/after
preparation, success, younger command staleness after replacement, cancellation acknowledgement, returned
failure, actual worker/admission allocation failure, reused persisted IDs, same-lineage generation change and
empty-session opening. Decode gates assert the complete declared decode charge without a surviving probe charge.
Nine `project-open-host.batch=*` gates additionally exercise two ordered replacements,
missing first file, first worker failure and decode-admission refusal followed by success, unsupported versions
99 and zero through real notice posting/shared deferred formatting, source growth after probe,
cancellation of the second project and external replacement during the second probe. They require exact receipt
errors, literal loaded pixels/active id, saved baseline/reset history, preserved path/document on refusal, one
notice per failed file, no hidden Present, view input while held, and zero retained tickets/charges/barriers.
Pure `history-work.*` replacement controls require the exact completed acquisition and the explicitly recorded
new lineage; old/unrelated lineages and late completion are refused. Independent load-resource floors compare
against PixelBuffer allocation, document record storage and manifest input contracts. The 22-row UI smoke includes
`project-open` after the independently completed save; timed readiness uses receipts and session/metadata state,
not synchronous file reads. Full configuration and performance qualification are recorded separately.

`export-readback-memory.*` adds nineteen checks for 4K/45 MP/100 MP admission, monotonic/path/metadata charges,
invalid side/count/curve/path limits, rounded full-canvas readback and shared sparse identity without materialization.
The fifteen `export-host.case=*` WARP scenarios cover PNG/JPEG literal output, younger mutation ordering, pre/post
publication cancellation, returned failure, worker/admission allocation failure, stale document, device changes
before encoding and after publication, captured quality/resolution, oversized output refusal before allocation,
path suffix-space refusal before Run, and a persistent second-tile Map failure after the first tile copied.
The latter requires no encode visit and no output file, exactly one failure receipt/notice and unchanged history.
Worker gates prove the request/barrier/reservation remain held with zero lent mappings/utility bytes; hidden
completion makes no Present. Device-change cases require one encode attempt. Failure notices occur exactly once;
terminal paths leave no barrier, reserved bytes, slot or utility target. PNG is exact opaque 191; JPEG RGB is within
3/255 and opaque. Export leaves project path/saved revision unchanged. Full qualification is separate from these
structural assertions; no event deadline is a performance threshold.

### File classification and banded codecs (P1.7 F1/F2)

`pathclasswork.*` IO tests independently cover directories, case-insensitive `.prism`, all three ZIP signatures,
short/missing/unreadable headers, counted nonterminated views, max/beyond-max counts and path lengths,
embedded NUL, invalid empty entries, empty batches and precancelled empty/nonempty results. `executor.*`
checks independent local/shutdown stop registrations, already-requested stops and retained local identity.
The WARP `path-classification-host.case=*` event gates exercise copied caller input, skipped empty entries,
queued younger mutation, before/after cancellation, returned failure, actual allocation failure, document
replacement, hidden completion and reservation/receipt retirement. These gates surround preparation;
pending driver IO and cancellation inside a WIC call are not fault-injected here.

`imagecodecwork.*` checks literal alpha PNG bytes, sparse Gray/BGRA multi-band round trips and sparse JPEG
quality-1 white-matte stripes straddling a band boundary (12/255 RGB tolerance away from edges). They require
the shared source to remain unmaterialized, complete precancelled output clearing, preserved distinct previous
destination bytes on cancelled/failed export, unrelated temp preservation and owned-temp cleanup.

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
its 24 operation rows include production project open/save, image imports and PNG/JPEG export workers.
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

For diagnostic children that are started and then waited with a deadline, `test.ps1` retains the process handle
before waiting and reads the terminal exit code while it remains owned. A missing exit code never passes a gate,
including a crash probe that left its dump. Timeout handling and product assertions are unchanged. Qualification
records an unobserved process result as a failed run rather than inferring success from partial PASS output.

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
Production OK/write activation uses the typed continuation source. The hidden WARP `host-flow.*` matrix gates
21 selected inputs across actual donorless-fill failure, successful Gamma 2 settlement, active cancellation and
stale document/session-end lifetimes (84 cases). It checks output absence before acknowledgement, saved Gamma 2
pixels, surviving failure/cancel panels and history, distinct layer selection/reorder/rename/opacity effects, view
input during work and complete receipt/input-budget retirement. Header mutation callbacks cannot edit beneath
admitted work. File/drop/size/key harness observations pump only after their real producer returns.
`host-flow.shape-release-after-commit-with-owned-color` releases a real Shape draft behind a gated color commit,
changes the palette before execution and checks the captured red pixels and two separately ordered undo entries.
Session-end message tests cover generation-tagged deferred prompts, cancelled and critical queries, reason
ownership and callback-safe close. Remaining GPU/IO/history costs belong to the later P1 migration gates.

Host request admission checks the 1 MiB aggregate declared-storage boundary, overflow refusal, retained capacity
of small logical strings/vectors, 256/257 path counts and 32,768-unit path refusal. Receipt-allocation fault
injection runs after reservation and before moving input; the previous charge and caller's original paths survive.
Queue/active transfer, clear, stale retirement and move overwrite preserve charges through publication and return
them after input destruction. The hidden bridge independently proves quota/OOM refusal leaves its panel, queue,
history and wake state untouched and cancellation returns the charge. These checks do not activate file producers.

Borrowed-input request tests additionally prove original Unicode values/order after caller edits, absent versus
present-empty optional values, bounded terminated-pointer scans, maximum path/batch acceptance and whole-batch
aggregate refusal. Enabling the copy-allocation failure control during quota/length/count refusal must not throw:
refusal precedes copying. Faults after a partial batch/string copy and before receipt allocation preserve prior
reservations and borrowed values and return all temporary charges. Successful admission reconciles to actual
retained capacities. Later selected output tests cover the same budget, pending-owner restriction, immutable
copied value, old/new replacement peak, failure preservation, queue/active transfer, terminal publication while
storage remains charged, and move-overwrite/destruction with independently retained receipts.
`host-path-read.*` additionally proves direct admitted population after whole-batch length preflight: refusal
before reader invocation, reader failure after a partial batch, partial input/receipt allocation faults, owning
Unicode order, maximum counts/lengths, queue/active transfer and terminal-publication charge lifetime. Shell/drop
producers retain their source owners through this immediate copy boundary; no reader survives in a queued input.

Offline corpus controls assert the real default manifest covers all nine planned v1 extensions, the extension
allowlist, role selection, traversal/schema/license/ownership rejection and preservation of mismatched images.
When all local selected files exist, they verify every size/hash and the byte-identical local CC0 text and exact
manifest snapshot. Missing local corpus is reported as SKIP in this ordinary suite; P4/P5 qualification must run
the required real-file checks without SKIP. No test downloads files from the internet.

File IO cancellation qualification includes independent stream-cap/COM/lifetime tests, direct sparse PNG/JPEG
file round trips across codec bands, and project stops at manifest, asset, ZIP close, staged and published
boundaries. Cancelled loads expose no partial result; cancelled imports preserve original canvas/active/layers;
prepublication saves preserve the complete destination/sibling tree and clean owned staging. A stop after
publication still returns Ok and the saved literal pixels/metadata. The separate explicit `--bench-file` harness
records time/memory before output verification; its measurements and the remaining production host workers,
pending IO waits, faults and cancellation latency qualification are tracked by the P1.7 child.

The hidden import host gates cover existing/grouped/new/empty targets across probe/preparation before/after
boundaries: literal pixels, captured paths, view input, exact reservation retirement, one named history entry,
new saved baseline, younger Undo ordering/staleness, acknowledged cancellation, returned/actual worker/admission
allocation failures, reused persisted IDs and generation changes. The admitted owner capture hook observes its
full reservation before source copying and has an actual allocation-failure control. A device change after
preparation cannot repeat decoding or prevent CPU-only apply. Batch cases require cached failure/partial-success
policy, changed pixel/encoded ceilings, mixed project/image ordering and retained first error. They assert no
hidden Present and zero terminal ticket/barrier/input/graph charges. Existing all-project gates continue to prove
completion without dispatching an empty image group. The 24-row smoke includes new-canvas and existing-layer
imports from the preceding successful PNG export; timed readiness reads receipts and session metadata only.

Prepared import publication also has independent live-context controls for destructive/adjustment panels,
layer/floating transforms, lasso, shape, brush, warp, gradient, pixel move and outline move. Actual allocation
and cancellation refusal preserve each live draft, source pixels, target, transaction depth, revision and redo.
Replacement clears them only after complete publication; weak preview owners remain in the release aggregate,
and a coverage-engine thread witness must observe exactly one owner-thread close before worker destruction.
Nested existing imports keep the outer name/opening/depth and record once on outer End, with literal Undo/Redo.
Actual selected project/image host routes include successful and failed live replacements and nested import;
fixtures explicitly clear a prior intentionally preserved draft before constructing the next independent case.

Io import controls independently verify opened-stream caps, literal first-frame metadata/pixels (including
two-frame TIFF), signed/hard pixel ceilings, per-path and aggregate preparation limits, cached failures and
PixelBuffer/record memory floors without materializing shared sparse sources. These gates do not prove
successful HEIF decoding on a host without its codec, RAW support, in-flight codec interruption or full G1 timing.

```powershell
.\validate-skills.ps1
.\format.ps1 -Check
.\test.ps1 -Platform x64 -Configuration Debug
.\test.ps1 -Platform x64 -Configuration Release
.\test.ps1 -Platform x64 -Configuration "ASan Debug"
.\build.ps1 -Platform ARM64 -Configuration Debug
```
