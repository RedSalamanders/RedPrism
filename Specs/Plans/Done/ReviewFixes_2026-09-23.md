# Review fixes (whole-codebase review of 2026-09-23)

> Spawned on 2026-09-23 from the request "review all the code for bug or overdesign or simplification", then "make a
> plan in Specs/WIP with all your finding and a check list to update when you are progressing on all the fixes;
> launch sub session with cheaper model if this make sense for some task and check there outcome, do all the work in
> parallel when possible; continue until the plan is done". Index before coding.

- **Status**: DONE (2026-09-23). Every row is checked, or DEFERRED to its owner with a reason, and D1–D11 are
  decided. The RF-1006 gates are green on the main checkout, where the result sits as uncommitted changes on top of
  `0035f1b` (see Closeout).
- **Planned at**: 2026-09-23, from a max-effort review of `Common/`, `RedPrism/` (with `Shaders/`), `Tests/`,
  `Build/` and the root scripts, including the uncommitted shortcut-replacement / Command List slice: 18 finder
  passes (eight line-by-line subsystem scans and ten angles: removed behavior, cross-file contracts, language
  pitfalls, wrappers and caches, reuse, overdesign ×2, efficiency, altitude, conventions), 15 adversarial verifier
  passes and a gap sweep. About 45 findings were confirmed; one was partly refuted (noted in RF-304).
- **Owning specs**: `IO/IO_ImageCodecs.md`, `IO/IO_ProjectFormat.md`, `Document/Document_Model.md`,
  `Document/Document_Tools.md`, `Document/Document_History.md`, `Core/Core_Settings.md`, `Core/Core_Architecture.md`,
  `Core/Core_PerformanceAndResources.md`, `UI/UI_ApplicationChrome.md`, `UI/UI_Windowing.md`,
  `Rendering/Rendering_Composition.md`, `Rendering/Rendering_Brush.md`, `Build/Build_Toolchain.md`,
  `Build/Build_Packaging.md`, `Testing/Testing_Validation.md`
- **Depends on**: nothing. Coordinates with [`UiNeverBlocks_2026-09-22.md`](../WIP/UiNeverBlocks_2026-09-22.md) (see
  Coordination).

This file is a non-normative execution record. Requirements move into the owning domain spec in the slice that lands
them.

## Outcome

Every confirmed finding is fixed with its spec text and a test, or carries a recorded decision; the overdesign and
simplification findings are either done or explicitly declined with a reason; the AGENTS.md gates are green on the
integrated result.

## Rules

1. Each fix lands with its spec text when observable behavior changes, and with a test: a numeric golden from
   retained numeric golden cases where the oracle covers the behavior, otherwise a DocumentTests / IoTests / HostTests /
   CompositionTests case or a self-test step. A test that cannot fail does not count.
2. The legacy oracle wins. A deliberate deviation is recorded in the Decisions table here and in the owning spec.
3. Work runs in git worktrees under `.build/worktrees/<wp>` on local branches `rf/<wp>`, created from `rf/base`
   (HEAD `0035f1b` plus the uncommitted shortcut slice of the main checkout, commit `4a3151a`). Nothing is pushed.
   The main checkout's branch, index and HEAD are never touched. Worktrees link `.build/vcpkg_installed` and
   `.build/dependencies` from the main checkout and build with MSBuild directly (no `build.ps1` / `vcpkg-install.ps1`
   in worktrees, so no concurrent vcpkg runs). Test runs are serialized machine-wide by the gate script's mutex.
4. A work package edits only the files and regions it owns (Work packages table). A fix that needs another package's
   file is reported as a hand-off, not edited. New STRINGTABLE ids use the package's reserved range.
5. The lead merges package branches into `rf/integration`, resolves conflicts, runs the full gate (x64 Debug,
   Release, ASan Debug; ARM64 builds), applies the wave to the main checkout as uncommitted changes, and ticks this
   checklist. Only the lead edits this file.
6. Items marked DEFERRED or DECLINED keep their row and the reason.

## Work packages and ownership

| Package | Wave | Model | Owns |
| --- | --- | --- | --- |
| WP1 Codecs and project I/O | 1 | Sonnet | `Common/ImageCodecs.*`, `Common/ProjectStore.*`, `Tests/IoTests/*` except `TestSettingsStore`, `Specs/IO/*`; string ids 688–693 |
| WP2 Document model and engines | 1 | Opus | `Common/Document.*`, `BrushStroke.*`, `Selection.*`, `CanvasSize.*`, `TransformEdit.*`, `Crop.*`, `DocumentHistory.*`, `Heal.*`, `WarpStroke.*`, `ImageFilters.*`, `AdjustEdit.cpp`, `Adjust.*`; `Tests/DocumentTests/*`; `Specs/Document/*` |
| WP3 Keyboard, shortcuts, Command List, menus | 1 | Opus | `Application.cpp` key paths (KeyModifiers, KeyPosition…HandleAccelerator, WM_KEY*/WM_SYS*/WM_CHAR/WM_INPUTLANGCHANGE cases, keyboard self-test steps); `ApplicationSettings.cpp`; `ChromeHost.*` chord/menu/Command List/caption/DispatchKey code; `EditorTool.h`; `Common/SettingsStore.*`, `Common/Resources.h`, `Common/Localization.*`; `IoTests` `TestSettingsStore`; `HostTests`; `Core_Settings.md`, keyboard/menu sections of `UI_ApplicationChrome.md`; string ids 624–647 |
| WP4 Rendering, viewport, frame loop | 1 | Opus | `CanvasRenderer.*`, `Renderer.*`, `CanvasViewport.h`, `TransformOverlay.h`, `CanvasOverlay.*`, `BrushCompute.*`, `FrameScheduler.h`, `Shaders/*`, `ApplicationCrop.cpp`; `Application.cpp` Run loop, device binding, viewport sizing, wheel/zoom-tool, export/readback composes, stroke/gradient/warp preview code, WM_SIZE/WM_DPICHANGED/WM_DISPLAYCHANGE/WM_GETMINMAXINFO; `Tests/CompositionTests/*`; `Specs/Rendering/*`; string ids 680–687 |
| WP5 Chrome controls, rail, accessibility | 1 | Sonnet | `ChromeHostAdjust.cpp`, `ChromeHostSize.cpp`, `ChromeHostCrop.cpp`, `ChromeHostAccessibility.cpp`, `AdjustControls.*`, `PaletteControl.*`, `LucideToolButton.*`, `AccessibilityRoot.*`, `ThemePalette.h`; `ChromeHost.cpp` LayerTreeModel, ShowControl/pressed-visual helpers, Arrange, picker and brush-header code; string ids 648–663 |
| WP6 Session, files, interaction state | 1 | Opus | `Application.cpp` InitializeChrome handlers, OnCommand, OnTool/SettleInteractions, pointer drags, New/Open/Save, WM_CLOSE/WM_QUERYENDSESSION/WM_ENDSESSION/WM_CAPTURECHANGED/WM_CANCELMODE/WM_ACTIVATEAPP/WM_KILLFOCUS/WM_DROPFILES cases, CloseMainWindow, Paste; `ApplicationFiles.cpp`, `ApplicationSize.cpp`, `ApplicationLayerEdits.cpp`, `ApplicationAdjust.cpp`, `ChromeHostRename.cpp`, `SystemClipboard.*`, `FileDialogs.*`; string ids 664–679 |
| WP7 Tests, harness, build tooling | 1 | Sonnet | `test.ps1`, `build.ps1`, `vcpkg-install.ps1`, `restore-dxui.ps1`, `package.ps1`, `format.ps1`, `validate-skills.ps1`, `Build/*`, `Directory.Build.*`, `Tests/*/*.vcxproj`, `Tests/BuildProcessTests/*`, the test `wmain` CRT setup, `RedPrism/Main.cpp`, the About command, `RedPrism.rc` version block, `Specs/Build/*`, `Specs/Testing/*`; string ids 694–699 |
| WP8 Common simplification | 2 | Opus (RF-801), Sonnet (rest) | the Common files of RF-801…RF-808 after wave 1 |
| WP9 RedPrism simplification | 2 | Opus (RF-901…RF-904), Sonnet (rest) | the RedPrism files of RF-901…RF-906 after wave 1 |
| Wave 3 | 3 | Lead + Sonnet | performance follow-ups, docs, screenshots, gates, closeout |

Severity: **P0** loses or corrupts user data; **P1** wrong behavior in a normal workflow; **P2** edge case, spec
deviation or robustness; **TEST** a gate or test that cannot fail; **BUILD** tooling; **CONV** AGENTS.md rule;
**PERF** measurable steady-state waste; **CLEAN** overdesign or simplification. "review #n" is the rank in the
review report.

## Wave 1 — WP1 Codecs and project I/O

- [x] **RF-101 P0** PNG encoding writes premultiplied pixels as straight alpha (review #1). `ImageCodecs.cpp:230-265`:
      `SetPixelFormat(32bppPBGRA)` returns `32bppBGRA` for PNG and the result is not checked, so every project save
      and reopen halves semi-transparent colors (probe: (0,0,128,128) → (0,0,64,128)); Export PNG and the clipboard
      PNG get dark fringes. Fix: honor the returned format and convert premultiplied → straight (as
      `WindowCapture.cpp:79-84` checks). Test: round trip alpha 1/64/128/254 pixels within ±1; project save/load.
- [x] **RF-102 P1** Layer `transform` / `maskPlacement` JSON shape differs from the Swift oracle (review #14).
      `ProjectStore.cpp:268`, `907-916`; `IO_ProjectFormat.md:80`. Swift `CGPoint`/`CGSize` encode as `[x,y]` /
      `[w,h]`. Fix: write arrays; read arrays and the object shape RedPrism wrote before. Test: Swift-shaped fixture
      and both shapes load; writer emits arrays.
- [x] **RF-103 P0** A painted-over shape layer is saved as a live shape and redrawn clean after reopen (review #8).
      `ProjectStore.cpp:1128-1131`, `1231-1235`. Fix: write `shape` only for a live shape (legacy
      `liveShape?.style`). Test: paint over a shape, save, load, `LiveShape()` is empty and a scale keeps the paint.
- [x] **RF-104 P1** Import ignores EXIF orientation and embedded ICC profiles (review #15). `ImageCodecs.cpp:84-101`,
      `155-160`. Fix: `IWICBitmapFlipRotator` from the orientation tag; `IWICColorTransform` from the frame's color
      context to sRGB; spec text in `IO_ImageCodecs.md`. Test: legacy `ImageImportTests` orientation golden
      (64×32 with orientation 6 → 32×64) with a fixture written by WIC in the test.
- [x] **RF-105 P2** Imported image lands at the root top-left; a failed import leaves a stale active id and reports
      Invalid instead of TooLarge. `ProjectStore.cpp:1596-1611`. Fix: insert above the active layer inside its folder,
      centered on the canvas (legacy `EditorSession.insert`); restore the active id; return TooLarge. Tests.
- [x] **RF-106 P2** Saves and exports are not durable. `ProjectStore.cpp:156`, `1414-1428`, `1470`;
      `ImageCodecs.cpp:297`. Fix: flush before rename / `ReplaceFileW`; exports write a temp file and replace.
- [x] **RF-107 P2** `zip_source_t` leaks when `zip_file_add` fails. `ProjectStore.cpp:1447-1462`.
- [x] **RF-108 P2** JPEG export turns transparency black; verify the legacy exporter's background and match it.
- [x] **RF-109 P2** Masks decode any PNG to Gray8 while the spec says non-gray is rejected; reconcile code and spec.
- [x] **RF-110 P2** `imageFile` is compared case-sensitively although ids are read in either case
      (`ProjectStore.cpp:845-846`, `IO_ProjectFormat.md:50-51`).
- [x] **RF-111 TEST** IoTests that cannot fail: `guid.uppercase-36` compares a value with itself (`:154`);
      `reject.format` passes through an unrelated `||` branch (`:486-488`); `reject.v4-clip` is rejected as a
      self-cycle, not by the version gate (`:503-506`); `jpeg.quality` uses a 4×4 solid and `>=` (`:205`; legacy uses
      128×128 textured and strict `<`).
- [x] **RF-112 CLEAN** Dead `PathIsInside` (`ProjectStore.cpp:69-83`); `WriteAllBytes` duplicates `WriteFileBytes`.

## Wave 1 — WP2 Document model and engines

- [x] **RF-201 P0** The second stroke on a Hide-All mask reveals the layer (review #7). `Document.cpp:545-604`
      (`Raster::Replacing` stamps the caller's fill), `BrushStroke.cpp:866-878`, `1173-1174`, `3163-3164`. Fix: keep
      the sparse source's own fill; pad only new area. Test: 1024² layer, black mask, strokes at (100,100) and
      (800,800), pixel (600,600) stays 0.
- [x] **RF-202 P0** Edits under a pending gradient drop it silently; a new gradient on another target replaces it
      uncommitted (review #6, model part). `BrushStroke.cpp:919-921` (`CanPaint` has no gradient term),
      `1968-2004`, `2633-2694`, `2890-2897`, `3427-3441`; `Document.cpp:3163-3251` (flips; Flip Canvas keeps the crop
      frame). Fix per Decision D3. Tests. Host part: RF-407.
- [x] **RF-203 P1** Changing the active layer keeps a stale mask target and selection set (review #13).
      `Document.cpp:1247`, `1334`, `1380`, `1476`, `1720`, `1984-1986`, `2107`, `ReplaceDocument`. Fix: one setter
      mirroring legacy `activeLayerID didSet`. Tests: Add Mask then New Layer paints; Ctrl+G after New wraps Layer 1.
- [x] **RF-204 P1** Cut on a mask with a full history restores the old clipboard. `BrushStroke.cpp:2674-2676`,
      `3438-3441`. Fix: the fill reports whether it painted. Test with 100 entries.
- [x] **RF-205 P1** A gradient on a mask ignores the mask palette. `BrushStroke.cpp:1937-1944`,
      `Document_Tools.md:264`. Fix: black/white per `maskPaintWhite` (legacy `paletteColor`). Port the legacy
      `GradientTests` mask rows.
- [x] **RF-206 P1** Move / transform / nudge run while a color or filter panel owns the layer; OK then drops Levels
      or misplaces Hue/Saturation. `Document.cpp:2529-2545` and `BeginTransform`, nudges. Fix: refuse while
      `HasColorEdit()` ("the open panel owns the document"). Test.
- [x] **RF-207 P1** Distort ignores unlinked and separately placed masks. `Document.cpp:2845-2879`, `3368-3392`.
      Port legacy `Distort.swift:275-286`. Test.
- [x] **RF-208 P1** Release Clipping Mask releases one layer only. `Document.cpp:1765-1770`. Port legacy
      `LiveLayerMask` and the golden `LiveMaskTests.swift:73-74`.
- [x] **RF-209 P2** The canvas draws a stale display copy while a transform or panel is pending.
      `Document.cpp:3312-3320` plus mutators that never bump `_editRevision` (`SetOpacity`, `SetBlendMode`,
      `PlaceLayer`, `DeleteLayer`, `MoveActiveLayer`, `FlipCanvas`, `MergeLayers`, `ApplyDocumentSize`). Test.
- [x] **RF-210 CONV** `ComposeTile` swallows `std::bad_alloc` on the brush hot path and the stroke reports success.
      `BrushStroke.cpp:666-673`, `631-635` (AGENTS.md; `Core_Architecture.md:48`).
- [x] **RF-211 P2** A stretched mask whose pixel size differs from its layer collapses to one value when painted.
      `BrushStroke.cpp:870-876`, `1169-1174`. Fix: stretch the old base as legacy `RasterSnapshot.draw`.
- [x] **RF-212 P2** The document pixel budget is not enforced by `MergeFloating`, `AddMask`, `AddPixelLayer` and
      `GrowFilterGrid`, so a document becomes unsaveable. `BrushStroke.cpp:2216-2233`, `2921`, `3103-3107`;
      `AdjustEdit.cpp:195`. Decision D7.
- [x] **RF-213 P2** `Raster::Data()` null (failed sparse materialization) is dereferenced. `Selection.cpp:843`,
      `856`, `887`; `CanvasSize.cpp:152-156`; `TransformEdit.cpp:21-27`.
- [x] **RF-214 P2** With the Crop tool active the first Undo only drops the tool's default frame.
      `Document.cpp:1180-1184`; legacy `restore()` cancels the crop and still undoes. Update `Document_History.md`.
- [x] **RF-215 P2** An Alt-drawn crop frame lands between pixels and Apply truncates it. `Crop.cpp:196-217`,
      `Document.cpp:2060-2062`. Legacy has the same snap: Decision D6.
- [x] **RF-216 PERF** A pixel drag rebuilds every tile it ever touched on every move. `BrushStroke.cpp:2381-2489`,
      `2827-2828`. Early-out on an unchanged offset; rebuild only the old and new landing tiles.
- [x] **RF-217 P2** Load Mask / Layer Selection silently loads nothing when tracing is refused (> 8 M edges).
      `Selection.cpp:342-349`, `845-860`.

## Wave 1 — WP3 Keyboard, shortcuts, Command List and menus

- [x] **RF-301 P1** Keys typed in text fields run document commands (review #11): the Ctrl switch and the new
      `ShortcutOverride` run before the canvas-focus gate and before `ChromeHost::DispatchKey`, and AltGr arrives as
      Ctrl with Alt ignored (€ runs Merge Down, Polish ż/ś/ó/ń run Undo/Save/Open/New). `Application.cpp:69-80`,
      `3645`, `3653-3778`, `4263-4268`. Fix per Decision D4; AltGr never matches a Ctrl-only chord.
- [x] **RF-302 P1** Command List Enter runs a command the user didn't pick (review #12): Enter runs the selected row
      regardless of focus; positional item ids survive filtering; closing the list leaves the card underneath
      without focus. `Application.cpp:3636-3642`; `ChromeHost.cpp:334`, `1499-1579`, `1694-1707`.
- [x] **RF-303 P1** Shortcut replacements misfire: shared default chords (Delete, arrows, M/L) are suppressed by the
      first catalog row; modifier variants (Ctrl+Shift+X, Ctrl+Shift+=) stay live; auto-repeat is ignored; duplicate
      `commandId` entries all stay bound while the menu shows the first. `Application.cpp:3443-3611`. Fix: one
      effective binding table (catalog defaults with aliases + settings replacements, later entry wins) with context
      predicates and a repeat policy; `HandleAccelerator` dispatches from it (also removes the per-key catalog
      parse). Decision D5 for Alt+F4.
- [x] **RF-304 P1** Menus show the default chord for Undo, Redo, Invert, Free Transform, Delete Layer, the clip toggle
      and Merge, because `SetUndoRedoNames` / `SetEditTargets` rewrite captions from raw strings on every sync.
      `ChromeHost.cpp:204-215`, `2439-2479`. Fix: one caption builder. (The claim that a settings reload also resets
      the dynamic captions until the next edit was refuted: the next frame's sync restores them.)
- [x] **RF-305 P2** Chord display: keys the parser accepts (F5, Space, Enter, Home…, `VK_XX`) render empty (shown as
      "cleared") or as a dangling "Ctrl+"; a bare "+" renders empty; numpad keys show number-row names.
      `SettingsStore.cpp:587-665`; `ChromeHost.cpp:110-153`, `1538-1548`.
- [x] **RF-306 P2** Tool tooltips, the status bar and palette/brush hints bake the default keys ("Brush (B)", "(X)").
      `EditorTool.h:190-231`; `ChromeHost.cpp:767`, `802`; `Application.cpp:2163`; `IDS_PALETTE_*`, `IDS_BRUSH_*_TIP`.
- [x] **RF-307 P2** The menu accelerator column is rebuilt from English literals at every start, replacing a
      translated column. `ChromeHost.cpp:204-215`. Decision D9.
- [x] **RF-308 P1** Tapping Alt or F10 enters the system-menu loop (rendering freezes, the next key beeps); handled
      Alt chords beep through `WM_SYSCHAR`. `Application.cpp:4273-4290`, `4382`.
- [x] **RF-309 P2** Move-tool digit keys ignore the two-digit rule. `Application.cpp:3906-3909`;
      `UI_ApplicationChrome.md:245-248`.
- [x] **RF-310 P2** Space is swallowed even when a chrome control has focus. `Application.cpp:3774-3777`.
- [x] **RF-311 P2** Settings store: unparsed `shortcuts` entries and unknown per-entry fields are dropped on every
      save; a UTF-8 BOM makes the file invalid (reset at cold start); named `vk` values are case-sensitive;
      `std::abs(INT_MIN)`; integral reals rejected for ints; the temp file is deleted while still open.
      `SettingsStore.cpp:67`, `329`, `369`, `525-571`, `806`.
- [x] **RF-312 CLEAN** `TextIdForCommand` and `CommandForTextId` repeat the same 110-row table
      (`ChromeHost.cpp:1831-2088`). Make the text id a column of the command catalog.
- [x] **RF-313 CLEAN** Caption-to-name conversion exists three times with different rules
      (`Resources.h:62`, `ApplicationFiles.cpp:29`, `ChromeHost.cpp:1600-1625`).
- [x] **RF-314 P2** A failed `FindNextChangeNotification` is ignored (possible busy loop).
      `ApplicationSettings.cpp:210`. Verify, then handle.
- [x] **RF-315 TEST** The self-test cannot exercise real key messages: the scan-code-0 fallback exists only for it,
      Alt is never tested, Enter in the Command List is never sent. `Application.cpp:3423-3426`. Drive keys through
      the window procedure with real scan codes.
- [x] **RF-316 P2** `language: "en"` cannot force English when a satellite exists. `Localization.cpp:83-87`. Verify.

## Wave 1 — WP4 Rendering, viewport and frame loop

- [x] **RF-401 P0** A failed frame closes the window without the save prompt (review #2): canvases or layers over the
      D3D11 texture limit (16 384 px; the model allows 30 000) fail every frame, and a device lost before `Present`
      is never recovered. `Application.cpp:326-339`; `CanvasRenderer.cpp:220`, `454-457`, `771-774`, `1053`;
      `Renderer.cpp:485-516`. Fix: recover a removed device from any stage; never close without the prompt; handle
      oversize per Decision D1.
- [x] **RF-402 P0** Export PNG/JPEG, Copy Merged, the wand and eyedroppers read back the on-screen preview composite
      (review #5). `Application.cpp:4097-4106`, `5283`; `CanvasRenderer.cpp:595-603`, `1058`. Fix: compose the
      document (invalidate before and after, no stroke substitution). Test.
- [x] **RF-403 P1** Canvas zoom is off by the display scale (review #10). `CanvasViewport.h:22`;
      `Application.cpp:2157`, `2175`, `3042`, `4837`, `5042`, `5909`; `ApplicationCrop.cpp:94`;
      `TransformOverlay.h:22-23`; `CompositionBlit.hlsl:52`; golden `CompositionTests.cpp:205`. Fix: zoom = device
      pixels per document pixel; DIP constants scaled; one threshold for the grid and point sampling.
- [x] **RF-404 P1** Wheel and pinch zoom ignore the delta size; the zoom-tool drag steps per message.
      `Application.cpp:2612`, `2849`.
- [x] **RF-405 P1** The brush's final curve segment is never uploaded, so the screen and PNG export differ from the
      saved pixels. `Application.cpp:5518-5522`. Test: a curved three-sample stroke, GPU readback vs raster.
- [x] **RF-406 P1** Stroke, gradient, pixel-move and warp previews stretch an unplaced layer mask over the grown grid.
      `CanvasRenderer.cpp:595-602`, `622-637`; `CompositionPixel.hlsl:256-260`.
- [x] **RF-407 P1** A new gradient reuses another layer's stroke texture and `EndStroke` adopts it for the wrong
      raster (host part of RF-202). `Application.cpp:5993-5998`, `6026-6041`; `CanvasRenderer.cpp:517-543`.
- [x] **RF-408 PERF** Marching ants present a full frame at every vsync and copy the selection every loop pass.
      `Application.cpp:254-263`; `CanvasOverlay.cpp:44`, `155-166`. Legacy steps every 120 ms.
- [x] **RF-409 TEST** Harness screenshots are armed before the first frame and are blank while the display is off;
      every capture failure exits 8 ("unavailable"). `Application.cpp:215-219`, `236`, `270-278`, `351`.
- [x] **RF-410 P2** A failed occlusion registration leaves an occluded window frozen until resized.
      `Renderer.cpp:363-373`, `529`.
- [x] **RF-411 P2** Minimized or occluded windows keep document surfaces (spec requires release).
      `Application.cpp:4396-4401`.
- [x] **RF-412 P2** The zoomed-out canvas aliases: single-mip canvas, bilinear blit (`Rendering_Composition.md:21`).
      `CanvasRenderer.cpp:176`; `CompositionBlit.hlsl:52`.
- [x] **RF-413 P2** Debug builds crash when the window is shorter than about 122 DIP with the Crop tool active
      (`std::clamp` with inverted bounds). `CanvasOverlay.cpp:125-128`; add a minimum window size.
- [x] **RF-414 CLEAN** The shader picks the adjustment kernel from the raw enum order.
      `CanvasRenderer.cpp:842-844`, `943`; `CompositionPixel.hlsl:336-346`.
- [x] **RF-415 TEST** CompositionTests: the preview-after-reset pixels are never checked (`:726`); the failure label is
      always "gpu vs cpu" (`:998-999`); shaders compile without the app's `/Ges /O3`.

## Wave 1 — WP5 Chrome controls, rail and accessibility

- [x] **RF-501 P1** Typed decimals in adjust-panel fields are mangled ("2.5" → "2.5.0"). `ChromeHostAdjust.cpp:608`,
      `624-628`: add the `IsEditing()` guard the other headers have.
- [x] **RF-502 P1** Image Size's unit list is off by two when Resample is off (Centimeters stores Percent).
      `ChromeHostSize.cpp:132-133`, `336`, `343`.
- [x] **RF-503 P1** The Gradient Map color picker outlives its panel and swallows the next palette color.
      `ChromeHostAdjust.cpp:772-780`; `ChromeHost.cpp:2128-2147`, `2207-2219`.
- [x] **RF-504 P2** Latched buttons (Canvas Size anchor, eyedroppers, Targeted) lose their highlight.
      `ChromeHost.cpp:1138-1145`.
- [x] **RF-505 PERF** Every frame repaints the whole chrome (`Arrange` re-sets the splitter minimums) and the rail
      re-parses about 30 SVG documents per paint (one cache color per tool; raw `ID2D1DeviceContext*` key; manual
      `GlobalFree`). `ChromeHost.cpp:1206-1207`; `LucideToolButton.cpp:194-274`, `315`, `336-345`.
- [x] **RF-506 PERF** The layers panel allocates for every visible row on every paint. `ChromeHost.cpp:279-287`.
- [x] **RF-507 P1** The palette zones and the custom adjust controls are invisible to UI Automation.
      `PaletteControl.h:10`; `AdjustControls.h:20`, `57`, `97`, `120`.
- [x] **RF-508 CONV** Hard-coded chrome strings: `"in"` / `"cm"` (`ChromeHostSize.cpp:56-58`) and the `", "` join of
      an accessible name (`PaletteControl.cpp:173`).
- [x] **RF-509 P2** The brush size stepper's accessible name is overwritten by its tooltip. `ChromeHost.cpp:3372`,
      `3381`.
- [x] **RF-510 CLEAN** `SetSizeCard` / `SetAdjustInspector` re-implement `ShowControl` without its change check.
      `ChromeHostSize.cpp:310-314`; `ChromeHostAdjust.cpp:592`.
- [x] **RF-511 P2** A click anywhere on the hue spectrum moves the nearest handle (legacy: the handle strip only).
      `AdjustControls.cpp:506-519`.

## Wave 1 — WP6 Session, files and interaction state

- [x] **RF-601 P0** The layers-panel opacity field can leave the undo transaction open, so later work is unrecorded
      and closing discards it without a prompt (review #3). `Application.cpp:2028-2072`.
- [x] **RF-602 P0** Saving during an open adjustment-layer or opacity edit stores the preview and later shows the
      document clean (review #9). `Application.cpp:4052-4090`. Decision D2.
- [x] **RF-603 P0** Sign-out or restart discards unsaved work without a prompt; settings are not saved (review #4).
      `Application.cpp:4357-4382`. Coordinate with UiNeverBlocks P0.6b / P1.7.
- [x] **RF-604 P1** Canvas Size / Image Size install a document computed before the settle, dropping a committed
      gradient or transform. `ApplicationSize.cpp:175-198`.
- [x] **RF-605 P1** The Rename card renames whichever layer is active when it commits.
      `ApplicationLayerEdits.cpp:80-89`; `ChromeHostRename.cpp:86-122`; `SettleInteractions`.
- [x] **RF-606 P1** Cancel at the discard prompt still drops open panels, cards, the crop frame and shape drafts.
      `ApplicationFiles.cpp:183`.
- [x] **RF-607 P1** Losing capture or focus leaves drags and Space-pan live; DxUi swallows `WM_MOUSELEAVE`.
      `Application.cpp:2592-2601`, `2674-2677`, `4181-4188`; send `PointerAction::Cancel`.
- [x] **RF-608 P2** Undo before an Alt-drag moves still duplicates the layer. `Application.cpp:2989-2997`,
      `4803-4807`.
- [x] **RF-609 P1** A `WM_CLOSE` dispatched inside a modal loop that a chrome callback started frees `ChromeHost` under
      its own stack. `Application.cpp:4357-4364`; `ChromeHost.cpp:2647-2649`. Defer the close until dispatch unwinds.
- [x] **RF-610 P2** New Project from an image batch gives up when the first image fails. `ApplicationFiles.cpp:294-311`.
- [x] **RF-611 P2** Saving a project opened from a `*.prism` folder always fails (ZIP chosen by suffix).
      `Application.cpp:4076`.
- [x] **RF-612 P2** Paste can take an older foreign clipboard image after a failed write. `Application.cpp:6310-6331`.
- [x] **RF-613 CONV** `SystemClipboard.cpp` frees owned `HGLOBAL`s by hand (`:63`, `270`, `283`).
- [x] **RF-614 TEST** The self-test overwrites the user's clipboard and its mirror check accepts a stale image.
      `Application.cpp:1515`, `1657`.

## Wave 1 — WP7 Tests, harness and build tooling

- [x] **RF-701 TEST** The screenshot gate accepts blank or transparent PNGs, maps every exit 8 to SKIP and passes the
      path unquoted. `test.ps1:170-190`. (App side: RF-409.)
- [x] **RF-702 TEST** The self-test runs without a timeout and RedPrism.exe keeps the CRT dialog mode; the test
      executables print CRT errors and still pass. `test.ps1:136`; `Main.cpp`; `Tests/*/*.cpp` `wmain`.
- [x] **RF-703 BUILD** ARM64 ASan Debug cannot be built (`build.ps1:111`, `vcpkg-install.ps1:92-93`,
      `Directory.Build.props:10-16`); AGENTS.md requires it. Decision D8.
- [x] **RF-704 P2** Help › About shows 0.1.0.0 while the file version carries the build number.
      `Application.cpp:3076`; read the module's version resource at run time.
- [x] **RF-705 BUILD** `build.ps1` ignores a failed process enumeration (`:48`; `Build_Toolchain.md:85`).
- [x] **RF-706 BUILD** `RedPrism.DxUi.targets` imports without a condition, so a missing restore fails with MSB4019
      before the guard. `Build/RedPrism.DxUi.targets:2`.
- [x] **RF-707 BUILD** Tool discovery and packaging: `format.ps1` finds only LLVM or the Insiders clang-format; vswhere
      has no VS 18 range; `package.ps1` needs PowerShell 7.4 without `#Requires` and omits DxUi's license;
      `RedPrism.rc` tests `_DEBUG`, which rc never sees.
- [x] **RF-708 TEST** Fixed temp paths collide across runs; harness crash dumps land in the user's real LocalAppData.
      `IoTests.cpp:47`; `ApplicationFiles.cpp:386`; `Main.cpp:137-139`.
- [x] **RF-709 CLEAN** The build banner rasterizes a constant picture at run time; an identity notice formatter;
      copy-pasted test blocks in `test.ps1`; vswhere lookup three times. `Build/BuildPresentation.psm1:111-213`,
      `601-613`.
- [x] **RF-710 CONV** `Main.cpp` uninitializes COM before `~Application` on startup failure and hand-rolls a
      `LocalFree` deleter. `Main.cpp:27-36`, `230`.
- [x] **RF-711 CONV** `format.ps1 -Check` fails at HEAD (`ChromeHost.cpp:1177-1180`) and in the shortcut slice; the
      lead runs `format.ps1` at every wave merge.

## Wave 1b — integration hand-offs and flakes (on `rf/integration` after all wave-1 merges)

Hand-offs the packages left for code another package owned. They run on branch `rf/wp10` from the integrated
result, so every API they need exists.

- [x] **RF-1101 P1** Reopening a project keeps a stale mask target when the reopened layer also has a mask: the host's
      `ReplaceDocument` (`ApplicationFiles.cpp`) must call the session's new `ReplaceDocument(document, active)` (WP2).
- [x] **RF-1102 P2** Load Mask / Layer Selection refusals are silent: pass `WandFailure` and say "too detailed"
      (new string) or show `IDS_ERROR_WAND_MEMORY` (WP2, RF-217).
- [x] **RF-1103 P1** Budget refusals (D7) are silent: show `IDS_ERROR_TOO_LARGE` when `TakeBudgetRefusal()` reports
      one (paste, Layer via Copy, duplicate, Add Mask, floating Apply, merge, filters, Distort, shapes) (WP2).
- [x] **RF-1104 P1** Copy may now commit a pending gradient without a `DocumentChanged`; Merge must settle the pending
      gradient and transform before composing its composite (WP2).
- [x] **RF-1105 P1** A gradient line on another target: the session commits the old gradient (WP2) and the host must
      end the old composition stroke with that commit (`TakeLastBrushCommit`) before starting the new preview (WP4
      RF-407); `SetMaskPaint` must refresh a pending gradient's colors (RF-205); check legacy X/D on a mask target
      (swap/reset `maskPaintWhite`).
- [x] **RF-1106 P2** Imported and dropped images land at the canvas center, not the drop point (WP1 RF-105; host
      `WM_DROPFILES` → `DragQueryPoint`).
- [x] **RF-1107 TEST** Self-test steps for WP5's live-control fixes (typed decimals, Image Size units, picker
      lifetime, `LucideSvgCreateCount()` flat when settled, UIA reachability of the palette and adjust controls, the
      spectrum handle strip) and a HostTests case for the About version reader (WP7 RF-704).
- [x] **RF-1108** Spec reconciliation: `Document_History.md` save-prompt lines (WP6 changed the prompt),
      `Core_Settings.md` save at session end, `Testing_Validation.md` self-test list, `Core_CrashHandling.md` harness
      dumps under `%TEMP%`, `Rendering_Brush.md` commit and `MoveLifted` wording (WP2), docs of the new behaviors.
- [x] **RF-1109 P2** Check two WP6 observations against WP3's key handling: digits typed after a canvas press also
      reaching the focused opacity field; a typed opacity folding into an open adjustment-layer panel's entry.
- [x] **RF-1110 TEST** Intermittent self-test crash: an access violation in `d2d1.dll` on a thread-pool thread (3 of
      about 15 Debug runs on branches from `rf/base`; dumps in `%LOCALAPPDATA%\RedPrism\Crashes` from 14:37–15:37).
      Reproduce on the integrated result (where the icon cache holds its context and harness dumps go to `%TEMP%`),
      find the root cause and fix it; also chase the once-seen ASan Marquee-switch and Release selection-drag flakes.
- [x] **RF-1111 CLEAN** Drop WP4's `CanvasOverlay` context workaround if WP5's `com_ptr` icon cache makes it redundant
      (device-recovery self-test loop).
- [ ] **RF-1115 DEFERRED** The user's own Copy and Paste give up after about 40 ms when another program holds the
      clipboard. On this machine five listeners open it for up to 47 ms after every change: Logitech Options+,
      JDownloader, clipboard history, Radeon Software and Mouse Without Borders (WP11's measurement). The self-test
      now waits them out. The product fix is `UiNeverBlocks` (its clipboard rows: bounded retries through one-shot
      timers, then a notice), which keeps the UI thread free while it waits.
- [x] **RF-1117 P2** Legacy shortcuts missing from the catalog: Export PNG (Cmd+Shift+E), Export JPEG
      (Cmd+Option+Shift+S), Canvas Size (Cmd+Option+C), Image Size (Cmd+Option+I). Found by WP12; WP12 adds them.
- [x] **RF-1118 P2** Enter during an edit presses a clicked header button (Flip Horizontal during Ctrl+T) instead
      of committing; legacy's Apply is the default action. Found by WP12. A mouse-clicked control does not take
      Enter while an edit or panel is pending, as with Space.
- [x] **RF-1119 P2** Escape during an edit does nothing once a header button was clicked (legacy cancels). This is
      RF-1118's rule applied to Escape. WP12.
- [x] **RF-1116 TEST** IoTests failed four `settings.*` checks once on `rf/wp9b` (rerun passed). Find the shared
      state: a fixed path, the user's real settings, or a parallel run outside the gate mutex. Make it
      deterministic.
- [x] **RF-1113 P2** While a mask is targeted, legacy shows the mask palette (black / white) in the rail wells and
      refuses the color picker (`ColorPalette.swift`); RedPrism does neither (found by WP10). WP9b, with RF-907.
- [ ] **RF-1114 DEFERRED** Neither the menu bar nor the tabs appear in the UI Automation tree (found by WP10's
      UIA step). A pinned-DxUi limitation (WP9c): `GetControlTypeId` in `DxUi.Accessibility.cpp` has no MenuBar or
      TabControl case, and MenuBar has no logical children. Recorded in `UI_ApplicationChrome.md`; fixed by a DxUi
      pin bump, since RedPrism consumes the pinned library as it is.
- [x] **RF-1112** Findings of the independent review of WP3's keyboard rewrite (WP12, branch `rf/wp12`, Opus):
      (P1) Space no longer pans once a chrome control has focus, and presses that control; (P1) after an Alt or F10
      tap, a canvas click leaves the menu bar holding the keyboard and plain keys are dropped; (P1, to prove) a handled
      Alt chord's key-ups skip DefWindowProc, so releasing Alt may open the menu bar; (P2) Enter on the Command List
      with no row runs "New"; Ctrl+Alt+G on AltGr layouts; scan code 0 loses position shortcuts; dead-key shortcuts
      arm the accent and modal commands leak their character (plus RF-1109's two observations); the key self-test
      depends on Caps Lock and frame timing; held opacity digits, Shift in a replaced nudge, `VK_BB` display drift;
      English fallback key names and a per-repaint allocation in the Command List model.

## Wave 2 — simplification (after wave 1 is merged and green)

- [x] **RF-801 CLEAN** One color-edit session for Levels, Hue/Saturation and filters; Curves / Exposure / Gradient Map
      / Grain exist as both `FilterKind` and `AdjustmentKind`; one commit rule. `AdjustEdit.cpp:444-1024`;
      `Adjust.cpp:298-315`, `1394-1589`; `Document.h:1014-1076`; `ApplicationAdjust.cpp`; `ChromeHostAdjust.cpp`.
- [x] **RF-802 CLEAN** The placed-raster renderer exists three times; use `RenderPlacedImage` / `RenderPlacedMask`
      (honor Nearest sampling; one out-of-memory contract). `Selection.cpp:873-945`; `ImageFilters.cpp:123-218`.
- [x] **RF-803 CLEAN** The raster-edit setup block ×4; `WandSample` == `CloneSample`; `shared_ptr` members that are
      only moved; `ResolveGradient` alias; a single-user `std::function`. `BrushStroke.cpp:1044`, `1844`, `1980`,
      `2603`; `Selection.cpp:1329-1351`.
- [x] **RF-804 CLEAN** `WarpTrimmed` re-implements trim and crop; the sub-rectangle placement formula ×5.
- [x] **RF-805 CLEAN** The name-numbering loop ×4; hand-built hierarchy walks; `LayerIndex` duplicates `IndexOf`; the
      bounds-of-points loop ×10.
- [x] **RF-806 CLEAN** Dead or test-only code: `QueryWindowCaptureSupport`, `LayerBlendModeCount` (returns 14 for 13
      modes), `CurrentRevision`, `HasSample`, `kExtensionLayerNameLength`, `CanEditPixels`; `SelectLayerTarget` /
      `ToggleMaskLink` / `ToggleLayerMask` are specified but unreachable from the UI (wire or drop).
- [x] **RF-807 CLEAN** (partial: `SpotHealing::Mode` done; the rest is RF-810) Derived or impossible state: `BrushSettings::erasing/healing/healingMode`,
      `SpotHealing::Mode`, `LevelsEdit::histogramReady`, `sampleMode`/`targeting`, `showsSampleRing`, host-only fields
      on `DocumentSession`.
- [x] **RF-808 CLEAN** Shared JSON readers for the two stores; `WindowCapture` PNG writer vs the codec; `CrashDumps`
      UTF-8 conversion and WIL handles; `DocumentHistory` identity walks.
- [x] **RF-809 P2** Switching the Levels channel copies the previous channel's range into the new channel
      (`OnAdjustAction` writes the range read before `SetChannel` back after it; legacy only changes the channel).
      Found by WP8a; fixed on `rf/wp8a` with a self-test step.
- [x] **RF-810 CLEAN** The rest of RF-807 (wave 3, after WP9b and WP9c):
      - `BrushSettings::erasing/healing/healingMode` are recomputed from `BrushKind` and the healing mode on every
        stroke; fixing that needs a `BrushStroke` API change plus the host read in `Application.cpp`.
      - `ColorEdit::histogramReady`, if it is still always true.
      - `showsSampleRing` and the host-only `DocumentSession` fields (`locksTransformRatio`, `transformAutoSelect`,
        `showsTransformControls`, `selectionExpandAmount`, `selectionContractAmount`) move to the host; each has
        5–18 call sites in `Application*.cpp`.
- [x] **RF-901 CLEAN** 22 single-client `std::function` handlers on ChromeHost with 57 null checks → one events
      interface; picker target as an enum. `ChromeHost.h:510-572`; `Application.cpp:1975-2093`.
- [x] **RF-902 CLEAN** Control factories and the header-row layout repeated ×6; eight `_syncing` flags.
      `ChromeHost.cpp:2780-3900`; `ChromeHostCrop.cpp:64`.
- [x] **RF-903 CLEAN** Modal-card state (focus, default/cancel, Enter/Escape) juggled per card → one card stack.
- [x] **RF-904 CLEAN** Pointer interaction state as one variant; drag flags derived from the session; `Push*Tiles` ×3;
      preview teardown ×4; one pending-edit slot for Escape / Enter / Undo (including Escape and Undo during a
      selection-outline drag, which do nothing today). `Application.cpp:5451-6272`, `2989-2997`, `3781-3960`.
- [x] **RF-905 CLEAN** A prompts/notices interface replaces the nine `_quietUi` branches; delete the dead
      not-implemented machinery and dead members (`SetToolHandler`/`_onTool`, `_inspector`, `_cropInspector`,
      `_readback`, `TileBytes`, `BrushErases`, `AdjustPanelBoundsDip`, `DeviceInfo`, `CanvasOverlay::Has*`); the
      `Detach` nulling list.
- [x] **RF-906 CLEAN** The self-test output writer ×7; `SolidRaster` / `InsertPaintedLayer` duplicates; compose +
      readback ×4; overlay pixel→DIP conversions ×9.
- [x] **RF-907 P1** Once another layer is selected, an existing layer mask can never be targeted again: nothing in the
      UI calls `SelectLayerTarget` / `ToggleMaskLink` / `ToggleLayerMask` (specified in `Document_Tools.md`; legacy
      targets the mask from its thumbnail). Wire a target switch (layers panel and a Layer menu command) or record why
      not.

Wave 2 runs as WP8a (RF-801, Opus), WP8b (RF-802…RF-808, Sonnet) and WP9a (RF-901…RF-903, Opus) from
`rf/integration` in parallel with wave 1b; WP9b (RF-904…RF-907, Opus) starts after wave 1b merges because it edits
the same `Application*.cpp` regions. After wave 1b it runs as two packages from `9550e4b`:
- WP9b (Opus): RF-904, RF-907 and RF-1113.
- WP9c (Sonnet): RF-905 and RF-906; it also keys the adjust chrome on `FilterKind` (after RF-801,
  `ChromeHost::AdjustPanelKind` repeats it) and investigates RF-1114.

## Wave 3 — performance and closeout

- [x] **RF-1001 PERF** `SyncChrome` rebuilds the chrome on every pointer move (pan, zoom, drags). Since RF-1107,
      every chrome preparation that changed something also republishes the UIA snapshot (a control-tree walk), so a
      pan that updates the status text walks the tree. Republish only when a UIA client listens
      (`UiaClientsAreListening`) or the accessible structure changed.
      `Application.cpp:2130-2320`. The per-iteration selection copy (`DisplayedSelection`) is UiNeverBlocks P1.6's
      (cached by selection revision); this row covers what the chrome rebuilds and republishes.
- [x] **RF-1002 PERF** `TransformPressLayer` and `CanMoveSelection` run on every `WM_SETCURSOR`.
      `Application.cpp:4684`, `4911`.
- [ ] **RF-1003 DEFERRED** Selection coverage is re-rasterized over the whole image for every stroke begin and
      preview tick. `Selection.cpp:182-603`; `BrushStroke.cpp:1063`; `AdjustEdit.cpp:303`. Owned by UiNeverBlocks
      P1.6, which specifies the cache: per (selection revision, grid), bounded to the selection, rows in parallel,
      plus the `DisplayedSelection` cache. A different cache here would be reworked there. After RF-801 the panel
      session computes its coverage once per render (`AdjustEdit.cpp` `CoverageOn`), the single place UiNeverBlocks'
      "cached once per panel session" applies.
- [ ] **RF-1004 DEFERRED** History snapshots deep-copy selection loops outside the byte budget — owned by
      UiNeverBlocks P0.4 (`shared_ptr<const>` loops).
- [x] **RF-1005** Docs (`docs/usage.md`: shortcuts, Command List, changed behaviors), harness screenshots regenerated
      and viewed, specs reconciled.
- [x] **RF-1006** (ARM64 prerequisite: `restore-dxui.ps1 -Platform ARM64` must run from the main checkout. From
      a worktree path, DxUi's vcpkg checkout exceeds MAX_PATH: "The filename or extension is too long" under
      `…\worktrees\integration\.build\dependencies\DxUi\<fingerprint>\vcpkg_buildtrees\ARM64\versioning_\…`. The
      shared ARM64 identity still names `7e3be94`.) Gates on the main checkout: `validate-skills.ps1`, `format.ps1 -Check`, `test.ps1` x64 Debug /
      Release / ASan Debug, `build.ps1 -Platform ARM64` for all three configurations; memory updated; plan to Done.

## Decisions

| Id | Question | Proposed | Decided |
| --- | --- | --- | --- |
| D1 | Documents or layers over the D3D11 texture limit (16 384) | Keep the legacy 30 000 model limit; render oversize content through a display proxy or tiles, and compose readbacks at full resolution (or refuse that one operation with a notice); never lose work | 2026-09-23 (WP4): as proposed — reduced display proxy, visible-part layer uploads, full-resolution tiled readbacks; nothing refused |
| D2 | Save / Export while a panel or typed edit is open | Apply the typed edit, and settle panels as Enter would before writing (legacy refuses the operation instead) | 2026-09-23 (WP6): as proposed; recorded as a deviation in the chrome spec (Files) |
| D3 | Pixel and layer edits while a gradient is pending | Apply the pending gradient first, as Invert already does (legacy refuses) | 2026-09-23 (WP2): as proposed, for every edit that changes the gradient's layer; a line on another target commits the old gradient |
| D4 | Keys while a chrome text field has focus | Only chords marked global (Save, Open, New, Exit) run; everything else goes to the field | 2026-09-23 (WP3): only the File commands run, and only from a chord with Ctrl, Alt or an F-key; Ctrl+Z/A/C/V/X, Delete and the rest go to the field |
| D5 | Replacing a command whose default is an OS gesture (Alt+F4) | The OS gesture keeps working | 2026-09-23 (WP3): Alt+F4, Alt+Space, F10/Shift+F10 and bare modifiers are reserved; a replacement naming one is ignored; Exit shows Alt+F4 and Windows closes the window (the save prompt still runs) |
| D6 | Fractional Alt-drawn crop frames (legacy has the same snap) | Snap to whole pixels; record the deviation | 2026-09-23 (WP2): the symmetric frame mirrors about a half-pixel center, and Crop snaps the frame before resizing; recorded in `Document_Tools.md` |
| D7 | Budgets the legacy does not enforce | Enforce the document budget everywhere pixels grow | 2026-09-23 (WP2): as proposed; refusals leave the document unchanged and the session reports them (`TakeBudgetRefusal`) |
| D8 | ARM64 ASan Debug | Add the `arm64-windows-asan` overlay triplet; build (runtime needs an ARM64 host) | 2026-09-23: adopted (WP7) |
| D9 | Menu accelerator column vs translated tables | Modifier names from the STRINGTABLE, key names from the layout | 2026-09-23 (WP3): as proposed; the column is rebuilt from the effective chord on layout change and settings reload |
| D10 | A stroke or warp in progress when capture or focus is lost (new with RF-607) | Commit the stroke as a mouse-up would | 2026-09-23 (WP6): commit (legacy cancels on resignFirstResponder); recorded in `UI_Windowing.md` › Pointer loss |
| D11 | One rule for every color-edit panel (RF-801): Levels and the filters check the transform at OK; Hue/Saturation does not; Hue/Saturation keeps a lasso draft | Take the Levels / filter rule for all | 2026-09-23 (WP8a): opening any panel commits a pending transform, applies a pending gradient and drops a lasso draft; OK installs only while the layer keeps the pixels and placement the panel opened on, otherwise the panel closes without an undo entry; recorded in `Document_Tools.md` |

## Coordination

`UiNeverBlocks_2026-09-22.md` (ACTIVE, another session) overlaps: RF-603 (its P0.6b end-session policy and P1.7
wiring — this plan lands only the prompt and the settings save), RF-1004 (its P0.4), RF-210 (its P0.6a job boundary
for `bad_alloc`, consistent with removing this UI-thread catch), RF-1003 and RF-216 (its P0.8 kernels), RF-401 (its
P1.3 GPU queue). Neither plan edits the other's rows. After RF-801, `UiNeverBlocks` still names `CommitFilter`,
`CommitLevels` and `ReplaceLayerPixels`. These are now one `ColorEdit` (`BeginColorEdit` … `CommitColorEdit`), and
its P1.1 transform check already holds for every kind. Tell that session when this plan's changes reach the main
checkout.

## STOP

Rebaselining a legacy golden to pass; a test that cannot fail; killing `RedPrism.exe`; running `build.ps1` or
`vcpkg-install.ps1` inside a worktree while another vcpkg run may be active; switching the main checkout's branch or
committing on `main`; pushing; editing another package's files.

## Done

Every row is checked, DEFERRED or DECLINED with a reason; Decisions are filled; the gates in RF-1006 are green on the
main checkout.

## Closeout

**Landed.** Every finding is fixed, with its spec text and a test that was checked to fail without its fix,
except where a row says otherwise:
- Wave 1: RF-101…RF-711, seven packages.
- Wave 1b: RF-1101…RF-1119, the integration hand-offs, the crash hunt, the keyboard review and later finds.
- Wave 2: RF-801…RF-810 and RF-901…RF-907.
- Wave 3: RF-1001, RF-1002, RF-1005 and RF-1006.

Headline results:
- The P0 premultiplied-PNG data loss is fixed (RF-101).
- The exit-time `d2d1.dll` crash is found and fixed (RF-1110).
- The self-test flakes are fixed: user key state, clipboard listeners, file dialogs, and display power and occlusion.
- One effective keyboard binding table, with the review's 3 P1 and 7 P2 findings fixed.
- One color-edit session.
- One ChromeHost events interface, card stack and control factories.
- One pointer interaction state and edit order.
- The mask target is reachable from the UI.
- Chrome sync per input: a settled sync costs 4–9 µs instead of about 3 ms, and a hand pan 2 µs instead of 2.2 ms.
- Cursor decisions are cached.

The decisions D1–D11 are in the table above and in the owning specs.

**Deferred, each with its owner:**
- RF-1003 (the selection coverage cache): UiNeverBlocks P1.6.
- RF-1004 (history selection loops): UiNeverBlocks P0.4.
- RF-1115 (the user's clipboard retries): UiNeverBlocks' clipboard rows.
- RF-1114 (the menu bar and tabs in UI Automation): a DxUi pin bump, since the pinned `GetControlTypeId` has no
  MenuBar or TabControl case.

**Declined:** RF-1112 #9d. The modifier variants that exact matching dropped have no legacy counterpart.

**Gates on the main checkout**, after applying `rf/base..ae4517b`:
- `validate-skills.ps1`: 15 skills.
- `format.ps1 -Check`: 99 files.
- `test.ps1 -Platform x64` Debug, Release and ASan Debug: "All tests passed", harness screenshots included.
- `build.ps1 -Platform ARM64` Debug, Release and ASan Debug: all build, after `restore-dxui.ps1 -Platform ARM64`
  (DxUi ARM64 now at the `c403052` pin) and `vcpkg-install.ps1 -Platform ARM64 -Asan` (D8).
- ARM64 runtime claims still need a native ARM64 host.

**State.**
- At closeout the result was applied to the main checkout's working tree: 140 paths outside `Specs/Plans/WIP`, of
  which 19 are new and one (`Common/DocumentHistory.h`) is deleted. The 121 tracked files come to +17 648 / −8 179
  lines against `0035f1b`, the earlier shortcut slice included.
- At the user's request ("commit all"), `main` then took it as grouped commits, one per package in merge order,
  each with the tree of its gated `rf/integration` state. The UiNeverBlocks plan draft and the shortcut slice come
  first, then this record. Nothing is pushed.
- `rf/integration` (`ae4517b`) holds the same content, merged package by package. Each package's per-item history
  is on its `rf/wp*` branch, with worktrees under `.build/worktrees/`. They can be removed once nobody needs that
  history.

**Follow-ups, not required by any finding:**
- Transform drags still rebuild the layer rows and the merge plan on each move.
- DxUi repaints its whole surface when anything is drawn over the canvas.
- Grouping ChromeHost's control pointers would make `ResetControlPointers` one assignment.
- Legacy's Black·Hide / White·Reveal mask popover is not ported (Paint, X and D choose instead).
- Layer thumbnails, a link button and a row context menu wait for a DxUi Tree that draws them.
- The pinned DxUi keeps a static WIC factory, released after COM shuts down at exit. It is latent: RedPrism draws no
  bitmap menu icons.
- The self-test's clipboard restore overwrites anything the user copies while a run is in progress.

**For the user:**
- `%LOCALAPPDATA%\RedPrism\Crashes` holds nine dumps and a `pending.txt` from harness runs made before RF-708 and
  RF-1110. They are safe to delete; `pending.txt` makes the next normal start offer the crash folder.
- One stray scratch self-test (a WP11 build under the session scratchpad) was left waiting in a Save As dialog. End
  it in Task Manager; pressing Cancel would resume its clipboard steps.

## Progress log

- 2026-09-23: plan written; `rf/base` = `4a3151a`; worktrees `.build/worktrees/{integration,wp1…wp7}` created. The
  worktree gate (`rf-gate.ps1`: MSBuild on the solution, the four test executables, the hidden WARP self-test) passed on
  `rf/base` x64 Debug in the integration worktree. Wave 1 launched: WP1, WP5, WP7 on Sonnet; WP2, WP3, WP4, WP6 on
  Opus.
- WP1 merged into `rf/integration` (`b12842b`): RF-101…RF-112 fixed (Sonnet). Notes: RF-104 orientation is checked
  for all eight EXIF values pixel by pixel; RF-106/RF-107 tests are structural (their failure modes need a crash or an
  unreachable libzip error); hand-off: the drop point for imported images (host, `WM_DROPFILES` → `ImportImageOnto`).
- WP7 merged (Sonnet): RF-701…RF-710 fixed. D8: the `arm64-windows-asan` triplet builds the four ports and every
  RedPrism object for ARM64 ASan Debug; the ARM64 link stops (as plain ARM64 Debug does) on a stale shared DxUi ARM64
  resolution (`7e3be94` vs the `c403052` pin) — run `restore-dxui.ps1 -Platform ARM64` and
  `vcpkg-install.ps1 -Platform ARM64 -Asan` in the main checkout at RF-1006. Hand-offs: `Core_DxUiIntegration.md`
  exit codes after RF-409; a HostTests case for `FormatFileVersion`; the per-process self-test image path in
  `ApplicationFiles.cpp:386`; a `Core_CrashHandling.md` sentence on harness dumps under `%TEMP%`. Lesson: never run
  bare `format.ps1` in a worktree (it rewrites every file); use `-Check` or format only your hunks.
- WP5 merged (Sonnet, one review round): RF-501…RF-511 fixed. RF-505: the settled chrome no longer repaints every
  frame (splitter minimums written on change) and the rail makes zero SVG documents per frame (one icon per button,
  context held by `com_ptr`, `unique_hglobal`). RF-507 after review: palette zones are Buttons whose UIA Invoke runs
  their own action; the curve, levels and spectrum editors are Buttons for UIA but stay non-focusable; the gradient
  ramp is a Label; none of them runs Button's hover animation. Hand-offs for self-test steps (live DxUi controls):
  typed decimals, Image Size units, picker lifetime, `LucideSvgCreateCount()` staying flat, UIA reachability, the
  spectrum handle strip.
- WP3 merged (Opus): 14 of 16 fixed; RF-306 and RF-313 partial until two WP6-region lines land at integration (the
  status-bar tool key via `CommandChordText`, `CaptionName` → `PlainCommandName`). One effective binding table
  (`Common/KeyChord.h`, `RedPrism/CommandCatalog.h`, `RedPrism/CommandBindings.h`, `ApplicationKeyboard.cpp`)
  replaces the switch and `ShortcutOverride`. D4, D5, D9 decided as below. The self-test drives keys through the
  window procedure. An independent Opus review of the keyboard rewrite is running.
- WP6 merged (Opus): RF-601…RF-614 fixed; each test was checked to fail with its fix reverted (18 reverts). D2 as
  proposed (Save, Save As and Export apply what is open first; legacy refuses instead — deviation recorded in the
  chrome spec). New behavior: capture loss, cancel mode and focus loss end every drag; strokes and warps commit
  (legacy cancels). The WP3 hand-offs (status-bar tool chord, `CaptionName`) landed at integration (`57e4550`), which
  completes RF-306 and RF-313. Open hand-offs: `Document_History.md` save-prompt lines (WP2), `Core_Settings.md`
  save at session end, `Testing_Validation.md` self-test list, and "digits typed after a canvas press also reach the
  focused opacity field" (check against WP3's character suppression).
- WP4 merged (Opus): RF-401…RF-415 fixed; the behavioral tests were checked to fail with their fixes reverted. D1:
  the 30 000 model limit stays; oversize documents draw through a reduced display proxy, oversize layers upload only
  their visible part, and every readback composes the document at full resolution in tiles (`ComposeReadback`).
  Frame failures recover a removed device at any stage; a canvas failure keeps the chrome and shows
  `IDS_ERROR_CANVAS_DRAW`; three failed frames post `WM_CLOSE`, so the save prompt runs. Zoom is device pixels per
  document pixel (goldens corrected to the legacy meaning; crisp from 200 %, grid from 800 %). Exit 8 = capture
  unsupported, 9 = capture failed. Hand-offs applied at integration (`26a0af4`): merges and adjustment backdrops read
  back through `ComposeReadback`; help text; `kIds`. Pending: WP4's `CanvasOverlay` context workaround can go now that
  WP5 holds the icon cache's context in a `com_ptr` (wave 2).
- WP2 merged (Opus): RF-201…RF-217 fixed; tests checked to fail with their fixes reverted (except the out-of-memory
  guards). D3, D6, D7 as proposed. All seven wave-1 packages are now in `rf/integration` (`6a18c27`); the six-package
  state (`26a0af4`) passed x64 Debug, Release and ASan Debug. The cross-package hand-offs are collected as wave 1b
  (RF-1101…RF-1112).
- `6a18c27` (all of wave 1) passed x64 Debug, Release and ASan Debug (ASan after a `-Rebuild`: an object compiled
  across the WP2 merge was newer than the new header and linked stale signatures).
- Wave 1b and wave 2 launched from `6a18c27`, in parallel: WP10 (RF-1101…RF-1109, RF-1111, Opus), WP11 (RF-1110,
  Opus), WP8a (RF-801, Opus), WP8b (RF-802…RF-808, Sonnet), WP9a (RF-901…RF-903, Opus); the independent review of
  WP3 (RF-1112) is running. WP9b waits for wave 1b.
- The WP3 review reported 3 P1 and 7 P2 findings (listed under RF-1112). The lead confirmed the Space-pan and
  menu-keyboard P1s in the code. WP12 (Opus, `rf/wp12` from `6a18c27`, string IDs 760–779) fixes them. RF-1109
  moves to WP12 unless WP10 already fixed it. The debuggers the crash hunt needs are installed (Visual Studio 18
  Insiders' `vsdbg`, System32 `dbgeng`/`dbghelp`); only `cdb.exe` is absent.
- WP8a finished RF-801 on `rf/wp8a` (`71dcbe6`, Opus) and passed Debug, Release and ASan Debug.
  - One `ColorEdit` session serves Levels, Hue/Saturation, the Image-menu adjustments and the filters: one render
    step, one identity test and one layer check at OK.
  - `RunFilter`'s adjustment cases call `ApplyAdjustment`, with the math unchanged.
  - One table maps `FilterKind` to `AdjustmentKind`.
  - The host dispatches through one switch.
  - Net −335 lines of code.
  - Tests: every existing expectation passes with none rebaselined, and there are 18 new `coloredit.*` tests; each
    new test was checked to fail against the old code.
  - D11 decided.
  - Side effects, both in the spec: a neutral Lens, Exposure or Grain panel shows the layer's own pixels, and Levels
    and Hue/Saturation draw an open seed.
  - Follow-ups sent back to WP8a: RF-809, removing the family accessors (`PendingFilter`, `PendingLevels`,
    `PendingHueSaturation`, `LevelsView`), and keeping the seed sequence.
- WP8a follow-ups done (`ee93d02`, `17fbf12`, `343f8ab`):
  - RF-809: the self-test step fails without the fix.
  - The family accessors are gone.
  - Only Add Noise and Grain draw a seed (`coloredit.only-noise-and-grain-draw-a-seed`).
  - Merged into `rf/integration` as `8caab11`.
  - ASan self-test flakes seen during WP8a's runs (3 of 5, under load) went to WP11 and WP12: the Delete-key
    replacement step, the Command List filter step and the clipboard write.
- WP9a finished RF-901…RF-903 on `rf/wp9a` (`07f84f1`, Opus); Debug, Release and ASan Debug are green. ChromeHost
  files are net −558 lines.
  - `RedPrism/ChromeEvents.h` replaces the 22 handler setters (null checks 66 → 1). Pickers are one
    `OpenColorPicker(target, argb)` / `OnPickerColor(target, phase, argb)`.
  - `MakeCombo` / `MakeStepper` / `MakeCheck` / `MakeButton` and one `ArrangeHeaderRow`; one `_syncing` flag.
  - A modal-card stack returns focus when a card closes and routes unused Enter / Escape to the focused or top
    card. This makes the specified Enter / Escape for the size card, the adjust panel and the notice work in the
    embedded host.
  - Bug fixes that fell out of the picker enum: a palette commit sets only the picked color; the wells follow the
    palette while a Gradient Map or Canvas Size picker is open; the eyedropper no longer paints into a palette well.
  - Evidence:
    - A control-tree dump (every control's type, bounds, name, tooltip and value over a 40–1300 DIP sweep) is
      byte-identical to the base after each commit.
    - 15 of 16 demo screenshots are byte-identical. The 16th differs only in marching ants, which also differ between
      two runs of the same build.
    - The new keyboard self-test step fails against the old card behavior.
  - Found: `FocusLayerOpacityForTest()` cannot focus a stepper panel, so the rail-click-after-opacity step tests
    nothing (sent to WP12 with RF-1109). Also a one-off "Ctrl+L did not open the Levels panel" self-test flake.
- WP9a merged (`af9671b`; the one `ApplicationAdjust.cpp` conflict with WP8a was resolved as WP9a described,
  formatted in `7d8850a`). Debug and Release passed. ASan Debug passed the four suites. The self-test failed once
  with the Alt-tap step ("left false"), then passed 3 of 3 reruns. That step's flake goes to WP12 (key self-test
  determinism).
- WP10 finished wave 1b (Opus) and merged into `rf/integration` as `9550e4b`. Its branch already contained
  `7d8850a`. Every test fails with its fix reverted, except RF-1105(a), which needed no fix.
  - RF-1101: the host's reopen calls the session's `ReplaceDocument`.
  - RF-1102: `IDS_ERROR_SELECTION_DETAILED` (666).
  - RF-1103: budget refusals raise `IDS_ERROR_TOO_LARGE` once, through a notice posted from the message loop
    (`kNoticeMessage`). Filters and merges name the budget.
  - RF-1104: Copy, Copy Merged and Merge apply a pending gradient or transform first.
  - RF-1105: the other-target gradient is proven by a self-test. `SetMaskPaint` refreshes the gradient. X and D
    swap or reset the mask palette in the session, per legacy `ColorPalette.swift`.
  - RF-1106: a dropped image lands at the drop point (`DragQueryPoint`).
  - RF-1107: the live-control self-test steps.
    - A real bug surfaced: the UIA snapshot was republished only on placement changes, so cards and panels shown
      later never reached UI Automation.
    - The fix is in `ChromeHost::Prepare`, and the consumer-dxui skill records it. Its cost goes to RF-1001.
  - RF-1108: specs reconciled.
  - RF-1111: the overlay's context workaround is gone; 8 of 8 Debug and 8 of 8 Release self-tests pass.
  - RF-1109 goes to WP12:
    - (1) did not reproduce;
    - (2) is real: a typed opacity or a Move-tool digit folds into the open "Edit … Adjustment" entry, and Cancel
      does not undo it. Legacy disables the control, so the fix is to refuse `SetOpacity` / `OnLayerOpacity`
      while a panel is open.
  - New items: RF-1113 and RF-1114.
- WP9b (Opus) and WP9c (Sonnet) launched from `9550e4b`.
- WP8b finished RF-802…RF-808 on `rf/wp8b` (Sonnet, 7 commits from `6a18c27`, net −373 lines). Debug, Release and
  ASan Debug are green.
  - RF-802: one placed-raster renderer. The only behavior change: the Wand, Clone, Smear and mask samples honor
    `LayerSampling::Nearest` as the canvas does (legacy `LayerRenderer.draw`). Spec'd, with
    `wand.sampler-scales-nearest` and `mask.sampler-scales-nearest`.
  - RF-803: one `MakeRasterEdit(layer, mask, settings)`, one `LayerSample`, `optional` members, and no single-use
    `std::function`.
  - RF-804: one `PlacementOfRect`; `WarpTrimmed` uses `TrimmedToAlpha`.
  - RF-805: `UniqueLayerName`, `PointBounds`, `IndexOf` exported, and the hierarchy walks reused.
  - RF-806: dead code deleted, including `DocumentHistory.h`. `LayerBlendModeCount()` is fixed and used.
  - RF-807: partial. `SpotHealing::Mode` is gone; the rest becomes RF-810.
  - RF-808: `Common/JsonRead.h` shared by both stores; `CrashDumps` uses `Utf8ToWide` and `unique_hfind` outside
    the crash path; `DocumentHistory::Trim` is one O(n) pass. `WindowCapture`'s PNG writer stays: its captures are
    straight alpha, so the codec is not an exact fit.
  - Merged as `8d2ad32`. The one conflict was in `Document.h`: WP8b's helpers were kept, and the preview helpers
    WP8a removed stay removed. `WandSample` and `CloneSample` remain as thin wrappers over `LayerSample`.
  - Gate: Debug and ASan Debug passed. The Release build hit shared DxUi PDB/lib contention with another worktree
    (C1041, then LNK1181), so the gate script now also retries LNK1181.
- WP11 finished RF-1110 (Opus, from `6a18c27`) and merged as `bb9d425` with no conflicts.
  - The d2d1 crash:
    - It is an exit-time use-after-free on the main thread, not a thread-pool one. The static rail icon caches
      released their SVG documents in `exit()` after the chrome had let go of the Direct2D factory. The last
      `Release` freed the factory, then touched its lock.
    - 7 of the 9 dumps are this crash. The other 2 came from a wp6 intermediate build that was later rebuilt.
    - Reproduced with full symbols (1 in 30 on the old cache). The in-box `dbgeng.dll` was used; no symbols were
      downloaded.
    - WP5's `com_ptr` context kept it from firing only by accident.
    - Fix: ChromeHost releases the icon documents before a device change and before detach. Three self-test checks
      fail without it.
  - The flakes:
    - A real Shift or Ctrl press by the user writes into the hidden self-test window's key state. The self-test now
      has its own keyboard state (`RedPrism/KeyboardState.h`); the test-harness skill says new key-state reads must
      go through it.
    - Clipboard listeners hold the clipboard up to 47 ms after each change; the self-test waits them out (about
      3 s).
    - Self-test runs never open a file dialog.
  - Result: 0 failures in 85 runs (45 Debug, 18 Release, 22 ASan).
  - Not fixed:
    - RF-1115: the user's own clipboard waits.
    - The self-test's clipboard restore overwrites anything copied during a run (harness limitation).
    - The pinned DxUi keeps a static WIC factory for bitmap menu icons, which would be released after COM shuts
      down. It is latent: RedPrism draws none.
  - The crash folder's 9 dumps and `pending.txt` belong to the user; they are reported, not deleted.
- `bb9d425` passed x64 Debug, Release and ASan Debug.
- An early ARM64 DxUi restore from the integration worktree failed on MAX_PATH; it moves to the main checkout at
  RF-1006.
- RF-1003 deferred to UiNeverBlocks P1.6.
- WP9c finished on `rf/wp9c` (Sonnet; it merged `bb9d425` itself). Net −181 lines.
  - RF-905: `RedPrism/Prompts.h` (real and quiet implementations) replaces the nine `_quietUi` branches. The
    not-implemented notice is folded into the posted notice (`IDS_NOT_IMPLEMENTED` deleted). The dead members are
    deleted.
  - RF-906: one self-test failure writer, one `SolidRaster` / `InsertPaintedLayer`, one
    `ComposeDocumentReadback`, one `PixelToDip`.
  - `AdjustPanelKind` is gone; the adjust chrome keys on `std::optional<FilterKind>`.
  - RF-1114: a pinned-DxUi limitation. `GetControlTypeId` has no MenuBar or TabControl case, and MenuBar exposes no
    logical children. Recorded in the chrome spec.
  - The lead's review found that the `Detach` simplification leaves dangling control pointers. The pinned DxUi's
    `ControlHost::Detach` frees the tree, and `NoticeOpen`, `ShortcutsOpen` and the other open-card queries
    dereference those pointers with only a null check. Sent back: restore the nulling, with an ASan-proven
    self-test step.
  - Fixed in `3dec96c`: `ResetControlPointers()` nulls all about 160 control pointers, including headers the
    pre-review code also left dangling. The new self-test step detaches the chrome and requires every open-card
    query to be false. With the call removed, ASan reports heap-use-after-free in `Control::IsVisible` at that step.
  - Gates green, merged as `062ea52` (the same tree as the gated `3dec96c`).
  - Follow-up idea, not required: grouping the control pointers in one struct would make that reset one assignment.
- WP9b finished RF-904, RF-907 and RF-1113 (Opus). It merged `062ea52` itself and was gated green in all three
  configurations on `a61869a`. Merged as `98d1175`, the same tree.
  - RF-904:
    - The 18 drag members are one `std::variant _drag`, and the brush, warp and shape drag flags come from the
      session.
    - `OnPointer` is split into press, drag, hover and end.
    - There is one `PushPreviewTiles`, one `SyncStrokePreview` and one `CancelShapeDraft`.
    - One ordered list of edits under way (`CanvasEdit` × `EditEnd`) serves Escape, Enter, Undo and the settles.
    - Drift bug (b) is fixed: Escape or Ctrl+Z during an outline drag puts the outline back (`CancelSelectionMove`).
      Wave 1 had already fixed (a) and (c), and (c) gains a key-path self-test. (d) is one function.
    - Net +117 lines: an explicit edit-ending table replaces the implicit, drifting copies, so this item trades
      lines for one order.
  - RF-907:
    - Rows with a mask carry a `Mask` badge. Pressing it targets the mask, Shift-press turns the mask on or off, and
      pressing the row returns to the pixels.
    - The Layer menu gains Target Layer Mask / Pixels, Link / Unlink and Enable / Disable (ids 323–325, strings
      780–787, no default shortcuts, as in legacy).
    - Not portable to the pinned DxUi Tree: a thumbnail, a link button and a row context menu. The Tree draws text
      rows with one badge, and its pointer events carry no button.
  - RF-1113:
    - While a mask is targeted, the wells show black and white per Paint and no picker opens on the palette.
    - An open well picker closes without change.
    - Legacy's Black·Hide / White·Reveal popover is not ported; Paint, X and D make that choice.
  - Every new test was checked to fail without its fix, except `move.cancel-puts-it-back`, which calls the new
    function directly.
  - Flakes seen:
    - One IoTests run failed four `settings.*` checks, then passed (RF-1116).
    - Before WP11's merge, the user's keystrokes reached two experiment runs. One left a self-test Open dialog,
      which WP9b closed with WM_CLOSE; the process exited normally.
- Wave 3 launched from `98d1175`:
  - WP13 (Opus): RF-1001 and RF-1002, measured with counters; the host-only half of RF-810.
  - WP14 (Sonnet): the Common half of RF-810 (derived `BrushSettings` flags, `histogramReady`) and RF-1116.
  - WP12 merges `98d1175` and gates again.
  - RF-1005 (docs, screenshots) and RF-1006 follow once they land.
- WP12 finished RF-1112 and RF-1109 (Opus). Its head `b9b60d8` contains `98d1175` and is green in all three
  configurations; merged as `2f27ea9`, the same tree.
  - Every fix but one was checked to fail without it. The exception is the first-character drop, a guard DxUi
    already covers.
  - #1: Space pans unless a text field, or a control the keyboard moved the focus to, has it. A clicked control no
    longer takes Space, and auto-repeats never press a control.
  - #2: a canvas press, `WM_KILLFOCUS`, `WM_ACTIVATEAPP(FALSE)` or a dropped bar focus hands the keyboard back from
    the menu bar.
  - #3: key-ups and handled Alt chords reach DefWindowProc. Before the fix, both release orders opened the menu.
  - #4: the first Enter on an unselected Command List only selects.
  - #5: Ctrl+Alt is text only from AltGr or in a text field.
  - #6: scan code 0 is mapped from the virtual key.
  - #7: `DispatchQueuedMessage` dispatches a key first and translates only a key that left its character, so dead
    keys no longer arm accents.
  - #8: key drivers use WP11's `KeyboardState`, and the digit pair is sent without a frame between. ASan went from
    4 of 5 to 8 of 8 passing.
  - #9a: a held digit sets opacity once.
  - #9b: `KeyMatch::shiftAdded` means a Shift chord's nudge steps by one.
  - #9c: vk replacements are normalized to positions, and the table rebuilds on a layout change.
  - #9d: declined. Legacy has none of the dropped modifier variants.
  - #10: fallback key names come from the STRINGTABLE (760–765); Command List rows keep their buffers.
  - RF-1109:
    - (1) Not reproducible; a proof test is in place, and `FocusLayerOpacityForTest` now really focuses the field.
    - (2) Fixed: `SetOpacity` and `OnLayerOpacity` refuse while a panel is open, and the stepper is disabled.
  - New items RF-1117 and RF-1118, both sent back to WP12.
  - WP12 also noted that `format.ps1 -Check` (VS clang-format 22.1.3) flags four spots on integration (RF-711), and
    saw one Release self-test clipboard failure after WP11's wait (it passed on rerun).
- WP14 finished (Sonnet) and merged as `51fb588`, the same tree as its gated head (green in all three
  configurations).
  - RF-810, Common part:
    - `BrushSettings::healing` / `healingMode` are gone, and `BrushStroke::Settings()` too. `BrushStroke` takes the
      kind and healing mode and derives `Erasing()` / `Healing()`; the host read is one line.
    - `BrushSettings::erasing` stays, unread, only because 44 positional `BrushSettings{…, false}` initializers
      name it. The lead removes it after WP13 lands, since 13 of them are in `Application.cpp`.
    - `ColorEdit::histogramReady` stays: UiNeverBlocks P1.1 makes the Levels histogram an asynchronous job that
      waits on it.
  - RF-1116:
    - Not reproduced in 66 runs (33 before and 33 after, 9 of them 3 in parallel).
    - Hardened the determinism gap found: the ten file-backed `TestSettingsStore` checks shared one path, and each
      started from the state the previous one left. Each now has its own scratch directory.
    - Two concurrent `IoTests.exe` runs were already isolated per process.
- WP12 follow-ups merged as `9e50f80`, the same tree as the gated `ef0b50e`.
  - RF-1117: legacy `CompositionApp.swift` chords.
    - Export PNG is Ctrl+Shift+E, Export JPEG Ctrl+Alt+Shift+S, Canvas Size Ctrl+Alt+C, Image Size Ctrl+Alt+I.
    - No clash with any default or reserved gesture; AltGr keeps typing where the layout types something.
    - Captions, menus and the Command List follow.
    - Tests: HostTests `bindings.legacy-file-and-image-chords` and `commands.default-chords-unambiguous`, plus a
      self-test step. Both fail when the chords are reverted.
  - RF-1118: Enter from a clicked control goes to the edit order while an edit, panel or card is pending. A
    Tab-reached control, a text field, an open dropdown list and a card holding the keyboard keep Enter.
    The self-test fails with the routing reverted.
  - Escape on a clicked control during an edit is unchanged. It is the same idea; noted, not required by any
    finding.
  - RF-1119 was then taken: Escape follows the same routing through `ChromeHost::FocusTakesEnterAndEscape`, the
    single list of controls that keep both keys. The self-test step fails without the fix ("cancelled false").
    Merged as `6465064`, the same tree as the gated `2416851`.
- WP15 (Sonnet, RF-1005) launched from `6465064`: `docs/usage.md` and `docs/README.md` against the specs, and the
  nine harness screenshots regenerated and viewed. WP13 is the last code package.
- WP15 finished and merged as `94188d9`.
  - `usage.md` gains:
    - `--crash-probe`, exit codes 8 and 9, and the exit-code line;
    - the Command List and the `shortcuts` schema (checked against `Core_Settings.md`);
    - D2, D7 and the harness dump folder;
    - the Mask badge and the Layer menu mask commands;
    - the zoom meaning, Space, and held digits.
  - The tools row loses a stray "A": no tool uses A; only Ctrl+A is bound.
  - `README.md` gains the Command List, the mask palette and badge, the zoom meaning, the budget and Load
    Selection notices, D2 and D10.
  - All 9 screenshots are regenerated and were viewed by WP15 and by the lead.
    - Changes: the rail's Lucide icons, the empty shell's zoom readout at 144 DPI (162 % → 104 %, confirming WP4's
      zoom meaning), and the layers panel's opacity stepper disabled under the Levels panel.
    - No defects.
  - `validate-skills.ps1` passes (15 of 15).
- WP13 finished RF-1001, RF-1002 and the host half of RF-810 (Opus). Its head `3e7702d` contains `94188d9` and is
  green in all three configurations; merged as `4a7c77f`, the same tree.
  - RF-1001:
    - Pan and zoom go through `SyncView` (the zoom text and the overlays only).
    - Every other chrome region writes only when its state differs from what it last wrote.
    - One preparation per sync instead of two.
    - The UIA snapshot republishes only while a client listens, when the structure changed, or when a client asked.
      `WM_GETOBJECT` publishes the waiting snapshot first.
    - Release/WARP, per input, including the chrome's share of the next frame:

      | Input | Before | After |
      | --- | --- | --- |
      | Hand pan | 2.15–2.31 ms | 0.5–2 µs |
      | Space pan | 3.0–3.1 ms | 1.3–1.8 ms |
      | Wheel step | 2.9–3.4 ms | 1.3–1.8 ms |
      | Transform drag | 3.2 ms | 1.3–1.8 ms |
      | Settled sync | 2.9–3.3 ms | 4–9 µs |

      The remainder is DxUi's full-surface repaint whenever anything is drawn over the canvas.
  - RF-1002: the cursor decision is made once per pointer position and session state.
    - Move tool with 1000 layers: 171–193 µs → 0.4–0.8 µs per move (7–11 µs with auto-select).
    - Outside the selection's bounds: 256 µs → 0.1–0.2 µs.
    - Inside the bounds: about 255 µs once per new pixel; faster inside tests are UiNeverBlocks P1.6.
  - RF-810 host half:
    - The five header fields are `Application` members, and the sample ring reads `_settings.sampleRing`.
    - One narrow change: a settings file rewritten with otherwise identical content now restores the ring the menu
      had toggled.
  - `ApplicationChromeSync.cpp` self-test: 11 assertion variants, each checked to fail without its fix. WP10's UIA
    steps still pass.
  - The layers panel scrolls to the active row only when rows or the active layer change.
  - Specs: `Core_PerformanceAndResources.md` gains "Pointer input and the chrome". The chrome spec's Accessibility
    paragraph, `Testing_Validation.md`, `Document_Tools.md` and the consumer-dxui skill are updated.
  - Not required, noted: transform drags still rebuild the layer rows and the merge plan per move.
- Lead cleanup `89d1c14`: `BrushSettings::erasing` removed with the 45 positional initializers' trailing `false`.
  RF-810 is complete. Only `ColorEdit::histogramReady` stays, for UiNeverBlocks P1.1.
- `89d1c14` passed x64 Debug, Release and ASan Debug in the integration worktree.
- The stale stash entries from the WP12/WP13 collision were dropped by verified hash (`afe046cd`, `79018600`).
- RF-1006 started.
  - The main checkout still held `rf/base`, with no peer changes to code.
  - `rf-apply.ps1` applied `rf/base..89d1c14`: 143 paths, 19 of them new, and `Common/DocumentHistory.h` deleted. No
    merges, no conflicts. The main checkout's branch, index and HEAD are untouched.
  - `format.ps1 -Check` (Visual Studio clang-format 22.1.3) flagged 11 spots in 5 files, all pre-existing layout.
    They were formatted in the worktree (`6d65fa1`, RF-711) and applied the same way.
  - `format.ps1 -Check` then passed (99 files), and so did `validate-skills.ps1` (15 skills).
  - Next: `test.ps1` x64 Debug, Release and ASan Debug in the main checkout, then the ARM64 builds.
- `test.ps1` on the main checkout: Release and ASan Debug passed, including the harness screenshots.
  - Debug failed once in the RF-411 self-test step ("A minimized or occluded window kept its document surfaces"),
    then passed on rerun.
  - The step read the machine's display power and the occlusion DWM reports for the hidden harness window; a
    sleeping display reports both. It now pins both through the step (`Renderer::SetOccludedForTest(optional)`, and
    `_displayPoweredOn` saved and restored), and its failure names the check that failed (`ae4517b`, applied like
    the rest).
  - After that: `format.ps1 -Check` passes (99 files), and `test.ps1` x64 Debug, Release and ASan Debug all report
    "All tests passed".
  - The ARM64 restore and builds are running.
- Stash collision: `refs/stash` is shared by every worktree, and WP12 and WP13 popped each other's stash. WP13 put
  WP12's back (`git stash store`), and WP12 re-applies it. Rule for every agent: no `git stash`, WIP commits instead.
- ARM64 (main checkout):
  - `restore-dxui.ps1 -Platform ARM64` moved the shared ARM64 DxUi from `7e3be94` to the `c403052` pin.
  - `vcpkg-install.ps1 -Platform ARM64 -Asan` installed the `arm64-windows-asan` tree (D8).
  - `build.ps1 -Platform ARM64` built Debug, Release and ASan Debug with no errors; the Release executable is an
    ARM64 PE.
  - RF-1006 is done.
- Closeout: the plan moved to `Specs/Plans/Done/`, and its WIP index row became a sentence in the index's
  introduction. The UiNeverBlocks session was told that the result reached the main checkout, with the names its
  plan uses (Coordination).
