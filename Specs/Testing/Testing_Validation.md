# RedPrism validation

Status: current normative repository contract
Last reviewed: 2026-09-21
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

## Gates

| Gate | Pass condition |
| --- | --- |
| Version identity | `RedPrism.exe` `OriginalFilename` is `RedPrism.exe` and `ProductName` is `RedPrism` |
| HostTests | `HostTests.exe` exit 0: adapter-of-monitor, frame-action selection (including display-off), command catalog and `--dpi` rejection, `EditorTool` key/glyph/name tables, every rail glyph present in Segoe Fluent Icons (skipped with a `SKIP` line when the font is absent), every STRINGTABLE id loaded from `RedPrism.exe` as a data file with positional placeholders only, transform overlay geometry (rotated edge and corner hits, rotation knob, zoomed document mapping, resize cursor directions, distortion midpoints), `--demo transform`, `--demo selection` and `--demo brush` parsing, `--demo retouch` parsing, `--bench brush` parsing, `--demo adjust`, `--demo canvassize` and `--demo crop` parsing, `--crash-probe <directory>` parsing and its refusal without a directory, rejection of other names; the crash-report helpers (`crash.*`: default directory, pruning to the newest ten, a minidump of the test process, the pending marker round trip, an empty directory refused); the satellite string tables (`locale.*`: candidate order and de-duplication, a missing satellite, the system languages, the `SetStringResourceSatellite` override and fallback) |
| ASan Debug only | `HostTests.exe --asan-probe` MUST NOT exit 0 and the captured log MUST contain `AddressSanitizer: heap-use-after-free`. An ordinary crash without that diagnostic is a failed probe. |
| Help | `RedPrism.exe --help` exit 0; redirected stdout contains `--self-test` and `--dpi` |
| Unknown switch | `RedPrism.exe --no-such-switch` exit 2 |
| Crash probe | `RedPrism.exe --crash-probe <directory>` (a directory under `.build\`) exits nonzero and leaves exactly one `RedPrism-*.dmp` above zero bytes with a `pending.txt` naming it ([`Core_CrashHandling.md`](../Core/Core_CrashHandling.md)); the directory is removed afterwards |
| WARP self-test | Hidden `RedPrism.exe --self-test --warp` exit 0: visible DxUi surface within 64 MiB with ≥ 1 composite, canvas surfaces allocated, a second frame does not re-composite the settled document, a splitter drag through `DispatchPointer` widens the layers panel by 40 DIP and narrows the hole after the layout re-sync, the foreground well opens the picker and a sampled color commits into the session palette, a cancelled background edit leaves it, X/D swap and reset, the opacity stepper records one `Layer Opacity` undo entry at 50 %, a painted layer shows the Move tool's box, a pointer drag commits one `Transform Layer` entry, Free Transform previews a typed rotation until Apply, Escape cancels, Shift+Right nudges ten pixels, Flip Canvas Horizontal and Flip Layer Vertical mirror, a Marquee drag selects a whole-pixel rectangle and a Shift drag adds one, M switches the shape once per press, dragging inside the selection moves it as one `Move Selection` entry, a click inside deselects, the wand selects a painted layer's pixels with exact bounds, Ctrl+Shift+I / Ctrl+A / Ctrl+D drive the Select menu, three polygonal clicks and Enter close a `Polygonal Lasso`, Expand from the header grows it by five pixels, Escape cancels an outline, a Brush stroke through the pointer path with the compute engine shows a live preview and commits one sparse `Brush Stroke` entry drawn from the adopted texture without materializing, the Eraser clears it, `[` `]` Shift-`[` and `5` change the tip, Escape drops a stroke, Clone Stamp stamps a mark from a source, Spot Healing blends it away, Smudge drags it through a live warp, Blur commits, a Gradient previews and applies with Enter after a digit set its opacity, a Shape drag adds a Rectangle layer, Shift-U switches the kind, Escape drops a draft, the Eyedropper samples the shape's color, Fill with Foreground Color fills a Marquee selection, Ctrl+X cuts it to the clipboard (mirrored to the system clipboard), Ctrl+V pastes it in place as a new layer, Ctrl+I inverts it, a Ctrl-drag moves the selected pixels through the live preview as one `Move Pixels` entry with the outline following, Ctrl+Right nudges them, Delete clears them, Ctrl+T on a selection floats them (Escape restores exactly, Enter merges one `Transform Selection` entry), Ctrl+J copies them to a layer, Add Mask hides them, Ctrl+Shift+C copies the composite, Ctrl+L opens the Levels panel and its output white previews black through the display document, Escape restores, an input white of 128 turns the gray white and Enter records one `Levels` entry, the Filter menu's Gaussian Blur applies one entry, Shift+Delete opens Content-Aware Fill on a red block filled into a selection of the white plate and Enter restores it as one `Content-Aware Fill` entry, with everything selected the fill reports no source, refuses Enter and closes on Escape, a Hue/Saturation adjustment layer opens its panel and a hue of 120 rotates the red beneath it live on the canvas, Enter keeps its record, Shift+= steps the blend mode, a rail click through the pointer path activates its tool (plainly, with the window focused, under the button's tooltip, and held across settled frames after a tool switch, after a click on another rail button and with the opacity stepper focused), the File menu's flyout anchor lies under its item on screen and a point on the open color picker card counts as chrome while the hole beside it counts as canvas, every interactive chrome control has an accessible name and the window's UIA root exposes the DxUi tree (its parent link, at least eight named elements, a hit under the menu bar), a settings file of the self-test's own reloads live (a light theme, the sample ring off and a 300 DIP layers panel apply, a broken file changes nothing and is not rewritten, restoring it applies again), the high-contrast palette applies and `RefreshTheme` follows the system state, and a save records the window placement; an image opened with the forced Add as Layer answer joins the document as a layer and with New Project replaces it by an unmodified untitled document of the image's size, a modified document survives New under the forced Cancel answer and is replaced under Don't Save, Close Project empties the shell (status `—`, a frame still renders) and New brings a document back, Import Images adds two files as one `Import Images` entry, Move Layer Up and Down swap siblings as `Reorder Layers` entries, Group Selected Layers wraps the active layer in a folder, Ctrl+Alt+G clips a layer to the one below and releases it, each function the UI spec lists as unimplemented raises the notice count by one and changes neither the document, the history nor the tool, Canvas Size shrinks the document to half in percent with the ratio locked around the top-left anchor on Enter and grows it with a white Canvas Extension layer from the bottom-right anchor, Escape cancels a pending size, Image Size resamples to 325 × 205 at 300 ppi with nearest sampling (the red layer's pixels and box scale with it) and three undos restore the document, New Canvas creates a 300 × 200 unmodified document, and the Crop tool frames the canvas when chosen, resizes the frame to 640 × 400 by a handle drag through the pointer path, crops on Enter as one `Crop` entry, moves the frame by a drag inside it, drops it on Escape, squares it through the header's ratio, cancels, undoes and leaves the tool without a frame, the Rename Layer card renames the active layer through typed characters and Enter and leaves the name alone on Escape, Merge Down composites a blue layer onto a red one into one trimmed layer named after the lower one and undo restores both, then 0 DxUi bytes and 0 canvas bytes when hidden |
| DocumentTests | `DocumentTests.exe` exit 0 (graph, `ProjectStore`-identical validation, history budget, mask placement helpers, collapsed rows, clip release on delete, legacy insertion/deletion/naming semantics, palette, transform geometry/drag/snap/session/duplicate/group/mask/flip/distort cases ported from the legacy transform suites, selection geometry/coverage/session/move/modify/load/marquee and wand kernel/session cases ported from the legacy `SelectionTests` and `MagicWandTests`, sparse rasters and the brush kernel, strokes, masks, opacity, curves, crossings, snapshots, budgets, keys, expansion and selection clips from the legacy brush suites, listed in [`Rendering_Brush.md`](../Rendering/Rendering_Brush.md), and the retouch cases: spot healing for every mode, clone stamp alignment and tip families, blur, smudge, liquify, warp tiles, gradients, shapes with the redraw on scaling, and the eyedropper; and the pixel edits ported from the legacy `SelectionEditTests` and `SelectionClipboardTests`: fills, Clear and the Delete key, invert, masks from the selection, pixel moves and nudges, the clipboard and the floating selection transform; and the color adjustments and filters ported from the legacy `LevelsTests`, `HueSaturationTests`, `ImageAdjustmentTests`, `FilterTests`, `AdjustmentLayerTests` and `BlendShortcutTests`: levels, hue/saturation, exposure, gradient map, grain, blurs, noise, lens correction, adjustment layers edited through the panels, the blend-mode step and the HSL formulas; and the content-aware fill ported from the legacy `SmartEditTests` (`smart.*`): the stripes continue through the filled block, a flat background reconstructs and undoes, Cancel and a no-source fill leave the original, the grid grows to a selection past the layer's edge; and the Layer menu edits ported from the legacy `LayerTests`, `GroupingSelectionTests` and `LiveMaskTests` gates (`order.*`, `wrap.*`, `clip.*`, `clear.*`): sibling swaps and their refusal, single, folder and empty grouping with undo, the clipping toggle with a shared base, and the cleared session; and the sizes ported from the legacy `CanvasSizeTests` and `ImageSizeTests` (`canvasdraft.*`, `canvas.*`, `imagedraft.*`, `imagesize.*`) and the crop cases from the legacy `CropTests` (`crop.*`) listed in [`Document_Tools.md`](../Document/Document_Tools.md); and the merge plan and merge (`merge.*`: no plan for a bottom layer or an empty folder, Merge Down onto the lower layer in the anchor's slot with clips re-targeted and undo, Merge Group replacing the folder, a stale plan refused) |
| CompositionTests | `CompositionTests.exe` exit 0: the golden table in [`Rendering_Composition.md`](../Rendering/Rendering_Composition.md) including the C12 adjustment layers and HSL blends (blend modes, order, raster masks with placement, live masks and chains, clipping stacks, folder masks, transforms, residency and caching, viewport, sparse upload, stroke preview and adoption, compute brush parity with the CPU kernel) |
| IoTests | `IoTests.exe` exit 0: the list in [`IO_ProjectFormat.md`](../IO/IO_ProjectFormat.md) plus PNG/Gray8/JPEG codec round trips, HEIF clean error, import onto a document (a PNG is not a ZIP project), and the settings store (`settings.*`: file names and paths, parsing with per-field fallbacks and rejects, serialization keeping unknown fields and a newer schema, the cold load of a missing, invalid and unreadable file, the live load, and saves that back up an invalid file, keep unknown fields, leave no temporary and create the directory) |
| Provenance | `DxUi.provenance.json` matches the lock; wrong-pin fixtures are rejected |
| Build presentation | Framed pixel-art banner identity, even scene geometry, and interactive-terminal selection |
| Harness screenshot | `--screenshot <png> --warp --dpi 96` and `--dpi 144` each write a PNG of the empty-document shell (populated rail), or exit 8 when capture is unavailable; `--demo transform --dpi 96` writes the transform demo (two painted layers, the rotated one's box and the header) and `--demo selection --dpi 96` the selection demo (an elliptical and a rectangular selection with the Marquee header) and `--demo brush --dpi 96` the brush demo (strokes on a backdrop with the Brush header and the tip cursor) the same way, and `--demo retouch --dpi 96` the retouch demo (a radial gradient, a rounded rectangle and an ellipse under the Shape header) and `--demo adjust --dpi 96` the adjustment demo (a Levels adjustment layer's panel with its histogram over a painted backdrop), and `--demo canvassize --dpi 96` the Canvas Size card over the transform layers (relative +200 × +100, the top-left anchor, a white extension), and `--demo crop --dpi 96` the Crop tool framing 640 × 400 of the transform layers with the shade and the header |
| Brush benchmark | `RedPrism.exe --bench brush` (hardware) and `--warp --bench brush` exit 0 and print `BRUSH BENCH` lines; the numbers are recorded under `Specs/TestRuns/` at each closeout ([`Rendering_Brush.md`](../Rendering/Rendering_Brush.md)), not gated |
| Release packaging | `package.ps1 -Platform x64` exit 0 with the clean-extraction smoke (`--help`, `--self-test --warp` with a system-only `Path`, the packaged version identity) and `package.ps1 -Platform ARM64` exit 0 (the smoke is skipped on an x64 host); a release gate run before a release and at a closeout, not part of `test.ps1` ([`Build_Packaging.md`](../Build/Build_Packaging.md)) |

`ASAN_OPTIONS` for the probe is `halt_on_error=1:abort_on_error=0:detect_leaks=0`.

## Test style

Tests are in-process executables under `Tests/` with one `Expect(condition, "suite.case")` per observable fact,
`PASS`/`FAIL` lines on stdout, and a nonzero exit on any failure. Goldens are numbers taken from the legacy Swift
tests, never "looks fine". GPU tests create their own WARP device; nothing in `Tests/` opens a visible window. Fixture
trees are materialized under `%TEMP%\RedPrism.IoTests`; a test that writes a file removes any stale copy first.

## Later gates (record now, implement with the feature)

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
letter plus a directory ownership marker), never repository `.build/` as a data sandbox. C1 does not create that tree.

## Skills

Skill or `AGENTS.md` edits MUST pass `.\validate-skills.ps1` in the same change.

## Required local matrix for a closeout

```powershell
.\validate-skills.ps1
.\format.ps1 -Check
.\test.ps1 -Platform x64 -Configuration Debug
.\test.ps1 -Platform x64 -Configuration Release
.\test.ps1 -Platform x64 -Configuration "ASan Debug"
.\build.ps1 -Platform ARM64 -Configuration Debug
```
