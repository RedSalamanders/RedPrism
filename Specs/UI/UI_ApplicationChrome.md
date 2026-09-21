# Application chrome

Status: current normative chrome contract
Last reviewed: 2026-09-21
Owner: `RedPrism/ChromeHost.*`, `RedPrism/EditorTool.h`, `RedPrism/resource.h`, `RedPrism/RedPrism.rc`,
`Application` file/session commands

## Scope

This contract owns how editor chrome is laid out on the one HWND and which commands it exposes. Widget catalog work
lives in DxUi. Document pixels live in the canvas composition
([`Rendering/Rendering_Composition.md`](../Rendering/Rendering_Composition.md)), not inside a DxUi control. Common file
dialogs (`IFileDialog`) and `WM_DROPFILES` are host jobs, not chrome HWNDs.

## Strings

Every visible chrome string (menus, tool names, tooltips, status text, dialog filters, error messages) is a
`STRINGTABLE` entry in `RedPrism/RedPrism.rc` declared in `RedPrism/resource.h` and loaded through
`Common/Resources.h` (`LoadStringResource`, `FormatStringResource`). Format strings use positional `std::format`
placeholders (`{0}`, `{1}`); a resource string is never a printf format and never a bare `{}`. Document data such as
default layer names (`Layer 1`, `Folder 1`) stays in the document model because it is persisted and compared by the
legacy tests; it is not chrome. `HostTests` loads `RedPrism.exe` as a data file and proves every catalogued id
resolves with positional placeholders only.

## Shell layout (DIP)

At 96 DPI one DIP equals one pixel. Physical size scales with window DPI (or `--dpi` during `--screenshot`).

| Band | Height/width |
| --- | --- |
| MenuBar | 30 DIP |
| TabControl | 32 DIP |
| Tool header | 36 DIP |
| StatusStrip | 24 DIP |
| Tool rail | 48 DIP |
| Layers splitter | 6 DIP (`DxUi::Splitter`, 2 DIP hit slop each side) |
| Layers panel | 160–480 DIP, default 240; canvas keeps at least 200 DIP |
| Layers title row | 32 DIP (title and opacity stepper) |
| Palette wells | bottom 48 DIP of the rail |

The canvas hole is the remaining rectangle. Escape moves focus from the canvas to chrome. Pointer down in the hole
focuses the canvas and is not consumed by DxUi.

The layers splitter is a `DxUi::Splitter` sized over canvas and layers between the tab band and the status strip;
only its separator is hittable, so the hole and the panel beneath keep their own input. Dragging previews, release
commits, Escape or capture loss restores the drag start; Left/Right step 8 DIP (Shift 32), Home/End go to the
limits. The splitter owns the clamp (canvas ≥ 200 DIP, layers 160–480 DIP); `ChromeHost` keeps the preferred width
across resizes and reports every change through the layout handler, and the host re-syncs the viewport on the next
frame. Pane bounds are applied on the next `Prepare`, never inside the change callback: in an EmbeddedHost a bounds
change during a captured drag invalidates the interaction revision and the next pointer event would cancel the drag.
There is no app-owned split geometry any more.

## Tool rail

The rail is a DxUi `Toolbar` holding sixteen icon-only `Button`s in `EditorTool` order (Move, Marquee, Lasso, Magic
Wand, Crop, Brush, Eraser, Spot Healing Brush, Clone Stamp, Smear, Gradient, Shape, Eyedropper, Hand, Zoom, No Tool).
`Toolbar` is a plain panel with no automatic layout and `Toggle` paints a switch, so the host lays the buttons out
itself: 40 DIP squares inset 4 DIP, 2 DIP apart, top to bottom from the tab band (`ChromeHost::ToolButtonBoundsDip`),
hidden when they would overlap the palette wells or the status strip. The selected tool is the button with the pressed visual
(`Button::SetPressedVisual`), re-applied on every `Prepare`. Each button carries a Segoe Fluent Icons glyph
(`EditorToolGlyph`), a tooltip `"<name> (<key>)"` and an accessible name. `HostTests` proves every glyph exists in
the installed Segoe Fluent Icons font and that keys, glyphs and names are unique.

| Tool | Key | Glyph |
| --- | --- | --- |
| Move | V | U+E7C2 |
| Marquee | M | U+EF20 |
| Lasso | L | U+EC87 |
| Magic Wand | W | U+F4A5 |
| Crop | C | U+E7A8 |
| Brush | B | U+E70F |
| Eraser | E | U+E75C |
| Spot Healing Brush | J | U+E90F |
| Clone Stamp | S | U+E8C8 |
| Smear | R | U+EA99 |
| Gradient | G | U+EB42 |
| Shape | U | U+E7FB |
| Eyedropper | I | U+EF3C |
| Hand | H | U+E7C9 |
| Zoom | Z | U+E71E |
| No Tool | A | U+E711 |

Tool keys apply when the canvas has focus; Space pans while held. X swaps the foreground and background wells and D
resets them to black over white, the legacy canvas key bindings.

The bottom 48 DIP of the rail hold the palette wells: the background `ColorSwatch` behind, the foreground in front
(26 DIP squares offset 14 DIP, the legacy palette layout), each with a tooltip and accessible name. Clicking a well
opens the color picker for that well.

## Menus and accelerators

The MenuBar lists File, Edit, Image, Layer, Select, Filter, View, Help. Flyouts are native `HMENU`s shown through
`DxUi::ShowNativeHMenuContextMenu` (the DxUi-supported path); a menu with no items opens nothing. Image, Select and
Filter fill in with C8–C13.

| Menu | Commands |
| --- | --- |
| File | New (Ctrl+N), Open (Ctrl+O), Save (Ctrl+S), Save As (Ctrl+Shift+S), Export PNG, Export JPEG, Exit |
| Edit | Undo <name> (Ctrl+Z), Redo <name> (Ctrl+Shift+Z, Ctrl+Y); captions carry the history entry name and gray out when unavailable |
| Layer | New Layer (Ctrl+Shift+N), New Folder (Ctrl+G), Delete Layer (Delete, canvas focused), Toggle Visibility, Move Out of Folder (Ctrl+Shift+G) |
| View | Fit on Screen (Ctrl+0), Actual Pixels (Ctrl+1), Zoom In (Ctrl+=), Zoom Out (Ctrl+-) |
| Help | About RedPrism (version from `Common/Version.h`) |

StatusStrip shows zoom percent, document size, and the current tool with its key. The tab title is the file name
(or Untitled) with a dirty mark when history is modified.

## Layers panel

The layers Tree lists rows top-first (`DocumentSession::LayerRows`): names, `(hidden)` annotation for rows whose
effective visibility is off, a folder glyph for groups, and depth indentation. Selecting a row selects the layer;
toggling a group's expander updates `DocumentSession::CollapsedGroupIDs` (rows carry `HierarchyEntry::collapsed`).
Tree has no drag API on the current DxUi pin; reorder stays a later interaction (C8) rather than a second retained
toolkit.

The 32 DIP title row holds the `Layers` label and a `DxUi::NumericStepper` (0–100 %, step 1, Shift 10, unit `%`,
accessible name `Layer opacity`) bound to the active raster layer's opacity. Typing previews live; Enter, focus
loss, the buttons, Up/Down and `RequestValue` commit; Escape restores the start value. One typed edit is one
`Layer Opacity` history entry: the snapshot is taken before the first preview and closed on commit, and a cancelled
edit leaves no entry. The stepper is disabled for folders or without an active layer and is never rewritten while
the user is typing in it.

## Palette and color picker

The palette (opaque ARGB foreground and background) is session state outside history
(`DocumentSession::ForegroundColor` / `BackgroundColor`, `SwapPaletteColors`, `ResetPaletteColors`), as the legacy
brush settings were. Clicking a well opens the `DxUi::ColorPicker` on a `CardPanel` floating over the canvas hole,
8 DIP right of the rail and below the tool header; chrome composites over the canvas with premultiplied alpha, so the
card is visible there. The picker edits the clicked well: field and hue-strip drags, typed R/G/B, six-digit hex and
host-sampled colors (`ChromeHost::SampleColor`, the eyedropper feed) preview into the well; OK or Enter commit and
notify the palette handler; Cancel, Escape (also when a picker child has focus) or capture loss restore the well.
Pointer input inside the card belongs to chrome even though it lies in the hole; a canvas click outside it focuses
the canvas and leaves the picker open. Captions come from the STRINGTABLE (`IDS_PICKER_*`, `IDS_PALETTE_*`).
Mask-selected black/white policy arrives with mask painting.

The C3 fallback (app-owned split rectangles, a ColorSwatch-launched dialog from existing controls, TextField
numeric nudge) is retired: RedPrism consumes the library controls and does not implement a second toolkit.

## Files

New creates a 1280×800 blank layer and marks history saved. Open/Save use `IFileDialog`. Dropped files (long paths
included) open a `.prism` or exploded folder, or import an image onto the current document as one undo step
(`Import Image`). Load failures report the `ProjectIoStatus` through a localized message (unsupported version names
the version); `--screenshot` runs suppress dialogs.

## Validation

Harness screenshots of the empty-document shell at **96** and **144** DPI (`--screenshot <png> --warp --dpi 96|144`)
show the populated rail. `--self-test --warp` composites chrome, proves a settled document is not re-composited, drags the splitter
through `DispatchPointer` (layers +40 DIP, hole narrower after the layout re-sync), opens the picker from the
foreground well and commits a sampled color into the session, cancels a background edit without changing it, swaps
and resets with X and D, commits 50 % through the opacity stepper as one `Layer Opacity` undo entry, then proves
0 DxUi and 0 canvas surface bytes when hidden. Keyboard Escape leaves the canvas. `HostTests` covers tool
tables, glyphs and the string table.
