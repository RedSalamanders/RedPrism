# Port review: challenge of C0–C7 and the DxUi enhancement path

- **Status**: DONE (2026-09-21)
- **Planned at**: RedPrism `e4f127c` (C0–C7 marked Done in `WindowsDirectXPort_2026-09-20.md`); DxUi
  `4099dde` (LocalizedAdaptiveLayout closed, merged as `78b3de3`); RedXe `d4f81b1`; RedSalamander `6661bfd`.
- **Owning specs**: `Rendering/Rendering_Composition.md`, `IO/IO_ProjectFormat.md`, `UI/UI_ApplicationChrome.md`,
  `Core/Core_DxUiIntegration.md`, `Document/Document_Model.md`, `Testing/Testing_Validation.md`
- **Drift check**: `git -C Z:\src\RedPrism status --short`; `git -C Z:\src\DxUi rev-parse HEAD`

## Why

The master plan declared C0–C7 shipped. A code-and-spec review against the legacy oracle found that several
"Done" behaviors did not work or diverged from the Swift semantics. This record lists what was wrong, what changed,
and what remains so the closeout of C7 is honest.

## Findings (fixed in this plan's first slice)

| Area | Defect found | Fix |
| --- | --- | --- |
| C7 rail | Tool rail rendered empty: DxUi `Toolbar` lays out nothing, `Toggle` paints a switch, letters were sent to the icon font | Icon-only `Button`s with Fluent glyphs, explicit 40 DIP layout, pressed-visual latch, tooltips; glyph presence proven by `HostTests` |
| C7 menus | Only File/Edit had items; no layer or view commands; Undo/Redo captions static | Layer and View menus, named Undo/Redo, Help/About, Exit; accelerators Ctrl+Shift+N, Ctrl+G, Ctrl+Shift+G, Delete, Ctrl+0/1/+/- |
| C7 chrome order | Chrome composited before the canvas blit, so anything over the hole (tooltips, popups) was overdrawn | Chrome `Prepare` → canvas → chrome `Composite` |
| C7 strings | Every chrome string was a C++ literal, contradicting A12 | `STRINGTABLE` + `Common/Resources.h`, positional placeholders, gate in `HostTests` |
| C7 tree | Folder expanders had no effect | `OnTreeToggleExpanded` → `CollapsedGroupIDs`, `HierarchyEntry::collapsed` |
| C7 drop | `WM_DROPFILES` truncated at `MAX_PATH` | Length query |
| C5 masks | Any project with a mask failed to load: `ValidateDocument` demanded mask pixels before assets attached | Format rule applies once storage exists; decoder enforces Gray8 |
| C5 adjustments | `adjustment` dropped `hsvSettings`, `levels`, `curves`, `exposureSettings`, `gradientMapSettings`, `grainSettings` on save and ignored them on load | Full Codable-shaped parse/write, Swift dictionary array form, always-written `levels`/`curves` |
| C5 validation | Mask metadata without `maskFile` accepted (Swift rejects) | Rejected |
| C6 texture cache | Keyed by a freed storage address; a later raster at the same address reused a stale texture (every second golden failed) | Cache holds the `Raster`; unused entries swept per composition |
| C6 live masks | Coverage sampled the source raster at the destination pixel (no transform, opacity, mask, chain) and mixed RGB luminance | Document-space R8 coverage passes matching `LiveMaskRenderer.coverage` |
| C6 folder masks | Loaded as R8 and multiplied by `alpha` (always 1): a no-op | `MaskCoverage` pass through the folder's mask transform; nested folders multiply |
| C6 clipping stacks | Each clipped layer blended individually; soft base edges got fringes; base blend mode ignored | Stack surfaces keep base alpha; base blend mode applies at resolve |
| C6 placed masks | `maskPlacement` ignored | Mask transform and edge background in the layer pass |
| C6 recomposition | Every invalidated frame re-composited the whole document | Compose only on `InvalidateDocument`; `--self-test` proves a settled document composites zero times |
| Tests | 33 composition cases, 20 I/O cases; no goldens for masks, live masks, stacks, folders, transforms, adjustment payloads, version gates | 86 composition, 165 I/O, 82 document, 256 host assertions ported from the legacy suites |
| Specs | `Core_DxUiIntegration` described the C2 hello button and a 300 ms timer; `Rendering_Composition` claimed masks and clipping shipped | Rewritten to the shipped behavior; DxUi HOLD status updated |
| Skills / AGENTS | No localization, shader, oracle, or test-harness guidance; `consumer-dxui` still said HOLD | Four new skills, three updated, AGENTS rules for strings, caches, bad_alloc, header hygiene |

## Remaining work

- [x] Slice 1 (this record): fixes above, specs, docs, skills, Debug/Release/ASan gates green.
- [x] Slice 2: promote the DxUi editor controls (Splitter, NumericStepper, ColorPicker) in `Z:\src\DxUi` under
      `EditorConsumerControls_2026-09-20.md` with catalog, tests, gallery, spec, changelog. Landed as DxUi `7e3be94`
      on branch `codex/editor-consumer-controls` (26 new cases in the `EditorControls` suite; 19 suites green in
      Debug, Release and ASan Debug; ARM64 Debug build; gallery republished; plan moved to Done).
- [x] Slice 3: bump `Dependencies/DxUi.lock.json` to the green library commit; replace the app-owned splitter with
      `DxUi::Splitter`; wire the FG/BG color well to `ColorPicker`; requalify; retire the C3 fallback paragraphs.
      Pin `7e3be94`; the splitter owns the layers clamp and reports layout changes; palette wells at the rail bottom
      open a `ColorPicker` card over the hole; the layers title row gained a `NumericStepper` for layer opacity
      (one `Layer Opacity` history entry per edit); X/D swap and reset the palette; `--self-test` exercises all of it.
      The pin is a local branch commit: move it to the public merge commit when the DxUi branch lands.
- [x] Slice 4: handed to `Transform_2026-09-20.md` (ACTIVE in the WIP index); C8 starts on the corrected composition and
      the shipped editor controls (the transform inspector uses `NumericStepper`).

## STOP

Rebaselining a golden to make it pass; touching archived behavioral reference; composing every frame again; adding a chrome string as a
literal; floating the DxUi pin onto `main` without the library gates.

## Done

Slices 1–3 green with specs updated; this file moves to `Specs/Plans/Done/` and leaves the index.

## Closeout (2026-09-21)

RedPrism gates after the pin bump, all x64: `test.ps1` Debug, Release and ASan Debug (HostTests 280 assertions with
the 91-id string table, DocumentTests incl. `palette.*`, CompositionTests, IoTests, provenance, WARP self-test with the
splitter/picker/stepper checks, 96 and 144 DPI harness screenshots), `build.ps1 -Platform ARM64 -Configuration Debug`,
`format.ps1`, `validate-skills.ps1`. `docs/screenshots/empty-shell-{96,144}.png` regenerated through the harness and
show the palette wells, the splitter grip and the opacity stepper.

Found during Slice 3 and recorded in `UI_ApplicationChrome.md` and the `consumer-dxui` skill: an EmbeddedHost cancels
a captured drag when control bounds change between two pointer events (interaction revision), so the splitter change
callback only records the width and the next `Prepare` arranges the panes. The DxUi pin (`7e3be94`) is a local branch
commit; it moves to the public merge commit when `codex/editor-consumer-controls` lands on `main`.
