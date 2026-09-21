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

## Move tool

With the Move tool the header is the transform inspector and the canvas shows the active layer's box through
`CanvasOverlay`, a DxUi control over the hole that is never hit: eight handles, a rotation knob 28 DIP above the
top edge, a dark outline under the accent line, and accent snap guides across the hole while a move is snapped.
The box follows `DocumentSession::EditedTransform` (the pending draft, the group box, or the mask placement) and
hides when Show Controls is off unless a persistent edit is pending. The header holds, left to right as room
allows with Cancel and Apply pinned right: the title (`Transform` / `Transform Mask`), X, Y, W, H (whole pixels),
Lock (aspect ratio), Scale (% of the layer's pixels, one decimal), Angle (whole degrees), Flip H, Flip V, Sampling,
Auto Select and Show Controls. Typing a number opens a persistent edit and previews it; Apply commits, Cancel
discards; while a distortion is pending the numbers are read-only.

Pointer rules in the hole (`Application::OnPointer`, geometry from `TransformOverlay.h`): a press on a handle
resizes (Alt about the center; Shift inverts the ratio lock; Ctrl, or an already distorted edit, distorts), on the
knob rotates (Shift snaps to 15°), anywhere else drags the layer a press would move: the layer under the pointer
with Ctrl or Auto Select, else the active layer, even from empty canvas. Alt-drag duplicates. Moves snap unless
Ctrl is held; drags land on whole pixels and degrees and commit on release. Cursors: the resize arrows follow the
box's rotation, the knob and a distortion show a cross, a movable layer shows the four-way arrow.

Keys with the canvas focused: arrows nudge by one pixel (Shift ten), Enter applies and Escape cancels a pending
edit, Ctrl+T starts Free Transform (a persistent edit) and selects the Move tool; with a non-empty selection it
transforms the selected pixels instead (`TransformCommand`: a `Floating Selection` layer that merges back as one
`Transform Selection` entry on Apply or Enter, restored exactly by Escape or Undo, the outline following the handles
and a distortion). Undo during a pending edit cancels it; switching tools or selecting another layer commits it. The transform contract itself lives in
[`Document_Tools.md`](../Document/Document_Tools.md).

The bottom 48 DIP of the rail hold the palette wells: the background `ColorSwatch` behind, the foreground in front
(26 DIP squares offset 14 DIP, the legacy palette layout), each with a tooltip and accessible name. Clicking a well
opens the color picker for that well.

## Selection tools

The Marquee, Lasso and Magic Wand share one header (`ChromeHost::BuildSelectionHeader`, mirrored from the session
through `SelectionInspectorState`, never re-laid out from a chrome callback): the tool name; the Marquee's Shape
(Rectangle / Ellipse) or the Lasso's kind (Freehand / Polygonal); Mode (New / Add / Subtract, showing a held Shift
or Alt live and the outline's starting mode while one is drawn; clicking sets the choice); for the wand Tolerance
(0–255), Sample Size (Point Sample / 3 by 3 Average / 5 by 5 Average), This Layer / All Layers and Contiguous;
Anti-alias for the Lasso, the wand and the elliptical Marquee (rectangles snap to whole pixels, so it does not apply);
Expand and Contract, each with a pixel amount (1–500), enabled with a non-empty selection and no outline in progress;
and, pinned right, Deselect when a selection exists and `Empty selection` when it encloses nothing. Every string is
a STRINGTABLE entry (`IDS_SELECTION_*`, `IDS_WAND_*`).

`CanvasOverlay` draws the selection outline in every tool as marching ants (a white line under a black 4/4 dash; the
dash is static until an animation timer arrives), clipped to the hole, from a path geometry rebuilt only when the
selection or the view mapping changes; and the draft being drawn (freehand and polygonal polylines, the polygonal
rubber band to the cursor and an 8 DIP handle on the first corner, the Marquee's rectangle or ellipse) as a white
line over a dark one. Outline and draft changes re-composite chrome only, never the document.

Pointer rules in the hole (`Application::SelectionPress` / `SelectionPointerMove` / `SelectionRelease`): the
modifiers at the press pick the mode (Shift adds, Alt subtracts). In New mode a press inside a non-empty selection
moves its outline (Shift keeps the move on the axis it has gone further along; one `Move Selection` entry), and a
press released without moving deselects (with the wand, it selects afresh from that pixel). A Ctrl press inside the
selection lifts its pixels instead (Alt duplicates): the drag shows them through the stroke preview with the outline
following (Shift keeps one axis), release commits one `Move Pixels` / `Duplicate Pixels` entry drawn from the adopted
texture, and Escape or capture loss drops it (`BeginPixelDrag` / `DragPixels` / `FinishPixelDrag`). Otherwise the Marquee
starts a whole-pixel box and reshapes it as the pointer or Shift moves; a Shift held from the press means Add and
squares the box only once let go and pressed again. The freehand Lasso follows the pointer and closes on release;
the polygonal Lasso adds a corner per click, tracks the cursor, and closes on a click within 8 DIP of the first corner,
a double-click (system double-click time and distance) or Enter; Backspace or Delete removes the last corner,
Escape drops the outline. The wand selects on the press. Capture loss ends a drag; a polygonal outline survives it.
Cursors: a crosshair, or the four-way arrow over a selection the press would move or Ctrl-move (the legacy
add/subtract and move-pixels badges wait on custom cursors). Switching tools drops a draft and ends an outline move.

Keys with the canvas focused: M chooses the Marquee and, pressed again, switches Rectangle and Ellipse; L chooses
the Lasso and, pressed again, switches Freehand and Polygonal (holding the key does not keep switching; the choice
stays while the project is open). Arrows nudge a non-empty selection by one pixel (Shift ten) with a selection tool.
Ctrl+A selects all, Ctrl+D deselects, Ctrl+Shift+I inverts. The Select menu adds Load Layer Selection and Load Mask
Selection for the active layer. Pixel edits: Ctrl+arrows nudge the selected pixels in any tool (one entry each);
Delete clears them, and without a selection deletes the targeted mask, or the layer; Ctrl+X, Ctrl+C, Ctrl+Shift+C
and Ctrl+V cut, copy, copy merged and paste (`RedPrism/SystemClipboard.*` mirrors a copy to the system clipboard as
PNG and a 32-bit DIB with straight alpha and reads another app's PNG or DIB, centered on the canvas, when the
system clipboard changed since that copy); Alt+Backspace and Ctrl+Backspace fill with the foreground and background
colors; Ctrl+I inverts; Ctrl+J is Layer via Copy. The selection contract itself lives in
[`Document_Tools.md`](../Document/Document_Tools.md).

## Brush tools

The Brush and Eraser share one header (`ChromeHost::BuildBrushHeader`, mirrored from the session through
`BrushInspectorState`, never re-laid out from a chrome callback): the tool name; for the Brush a Mode (Paint /
Erase); Size (1–2000 px), Hardness (0–100 %) and Opacity (1–100 %) steppers; and, while the active layer's mask is
selected, Paint (Black - Hide / White - Reveal) with a `Mask` note pinned right, otherwise Color with the foreground
`ColorSwatch`, which opens the color picker like the rail's well. Strings are `IDS_BRUSH_*`; the budget error is
`IDS_ERROR_BRUSH_BUDGET`.

`CanvasOverlay` draws the tip as a ring of the brush diameter around the pointer (a white 2.5 DIP line under a
black 1 DIP one; a crosshair when the ring would be under 4 DIP across) whenever a brush tool is over the canvas of
a document, and the system cursor is hidden there so the ring reads alone; over the chrome, outside the hole and
without a document the arrow or crosshair returns. The ring follows the zoom.

Pointer rules in the hole (`Application::BeginBrushStroke` / `ContinueBrushStroke` / `FinishBrushStroke`): a left
press starts a stroke on the active layer, or on its mask when the mask is selected (Shift-click paints a straight
line from the end of the previous stroke on that target); moving adds samples and shows them at once through the
composition's stroke preview; release, the pointer leaving the window and switching tools each commit one history
entry (`Brush Stroke`, `Erase`, `Paint Mask`); Escape drops the stroke. The Eraser and the Brush's Erase mode both
erase; a mask is never erased but painted black or white. A stroke over the pixel budget or an engine failure is
dropped with `IDS_ERROR_BRUSH_BUDGET`. A press does nothing on a folder without its mask, a hidden or adjustment
layer, a disabled mask or an empty selection.

Keys with the canvas focused: B chooses the Brush and E the Eraser; `[` and `]` step the size by a fifth (at least
one pixel); Shift-`[` and Shift-`]` step the hardness by quarters; 1–9 set the opacity to 10–90 % and 0 to 100 %,
two digits within 0.6 s make a two-digit percent (with the Move tool a digit sets the active layer's opacity the
same way); Escape cancels a stroke in progress. The stroke contract lives in
[`Rendering_Brush.md`](../Rendering/Rendering_Brush.md), the session API in
[`Document_Tools.md`](../Document/Document_Tools.md).

## Retouch tools

Spot Healing, Clone Stamp and Smear share the brush header (`BrushInspectorState`), which adds per tool: Type
(Content-Aware / Create Texture / Proximity Match) for Spot Healing; Aligned and This Layer / All Layers for Clone
Stamp, with `Alt-click to set the source` pinned right until a source exists; Mode (Liquify / Blur / Smudge) for
Smear, whose opacity is labelled Strength. Neither Clone Stamp nor Smear shows the color swatch. The tip ring is
their cursor; Clone Stamp adds a crosshair at the point it copies from, following the pointer once a stroke fixed
the offset. Pointer rules: Alt-click with Clone Stamp sets the source; a press starts a stroke of the tool's kind
(Smear's Liquify and Smudge start a warp instead, shown live through the stroke preview), Shift-click a line;
release, leaving the window or a tool switch commits, Escape drops; a stroke on a mask does nothing for Spot Healing
and Clone Stamp and reports `IDS_ERROR_SMEAR_MASK` for Liquify and Smudge; Clone Stamp without a source reports
`IDS_ERROR_CLONE_SOURCE`. Switching between the Brush family (Brush, Eraser, Spot Healing), Clone Stamp and Smear
parks each family's size, hardness and opacity.

The Gradient header (`ChromeHost::BuildGradientHeader`, `GradientInspectorState`): Shape (Linear / Radial), the two
end colors as swatches (either opens the foreground picker), Colors (Foreground to Background / Foreground to
Transparent), Reverse, Opacity (1–100 %), a `Mask` note, and Cancel / Apply pinned right while a line is pending.
Pointer rules: a press starts a line and drags its end; a press within 10 DIP of a pending line's end drags that end
(Shift snaps the line to 45°); a click without a line pends nothing. The pending gradient shows through the
composition's stroke preview and the overlay draws the line with a handle at each end. Enter or Apply commits, Escape
or Cancel drops, Undo drops, and switching tools, layers or targets applies it. Digits set the gradient's opacity.
Palette changes re-render the pending line.

The Shape header (`BuildShapeHeader`, `ShapeInspectorState`): Rectangle / Ellipse, Radius (px, rectangles only) and
Fill (the foreground swatch). A drag draws the shape as a draft on the overlay, filled with the foreground color and
outlined (Shift squares, Alt grows from the center); release adds the layer; Escape or a tool switch drops the
draft; Shift-U switches the kind (U alone selects the tool).

The Eyedropper (I), or Alt with the Brush, Spot Healing and Gradient between strokes, reads the composited canvas
under the pointer while pressed into the foreground color, or into the open color picker; the overlay shows the
legacy comparison ring (the new color above, the color before the press below) unless Sample Ring is off in the View
menu. Its cursor, and the Gradient's and Shape's, is the crosshair. Every string is a STRINGTABLE entry
(`IDS_HEAL_*`, `IDS_CLONE_*`, `IDS_SMEAR_*`, `IDS_GRADIENT_*`, `IDS_SHAPE_*`).

## Menus and accelerators

The MenuBar lists File, Edit, Image, Layer, Select, Filter, View, Help. Flyouts are native `HMENU`s shown through
`DxUi::ShowNativeHMenuContextMenu` (the DxUi-supported path); a menu with no items opens nothing. Filter fills in
with C12.

| Menu | Commands |
| --- | --- |
| File | New (Ctrl+N), Open (Ctrl+O), Save (Ctrl+S), Save As (Ctrl+Shift+S), Export PNG, Export JPEG, Exit |
| Edit | Undo <name> (Ctrl+Z), Redo <name> (Ctrl+Shift+Z, Ctrl+Y); captions carry the history entry name and gray out when unavailable; Cut (Ctrl+X), Copy (Ctrl+C), Copy Merged (Ctrl+Shift+C), Paste (Ctrl+V); Fill with Foreground Color (Alt+Backspace), Fill with Background Color (Ctrl+Backspace), Clear Selection Pixels; Invert / Invert Mask (Ctrl+I); Free Transform / Transform Selection (Ctrl+T); the Invert and transform captions follow the target and the selection (`SetEditTargets`) |
| Image | Flip Canvas Horizontal, Flip Canvas Vertical |
| Layer | New Layer (Ctrl+Shift+N), New Folder (Ctrl+G), Layer via Copy (Ctrl+J), Delete Layer / Delete Layer Mask (the caption follows the target; the Delete key clears selected pixels first, canvas focused), Toggle Visibility, Move Out of Folder (Ctrl+Shift+G), Add Mask, Add Black Mask (from the selection when there is one), Flip Layer Horizontal, Flip Layer Vertical |
| Select | All (Ctrl+A), Deselect (Ctrl+D), Inverse (Ctrl+Shift+I), Load Layer Selection, Load Mask Selection |
| View | Fit on Screen (Ctrl+0), Actual Pixels (Ctrl+1), Zoom In (Ctrl+=), Zoom Out (Ctrl+-), Sample Ring (a check, on by default) |
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
0 DxUi and 0 canvas surface bytes when hidden. With a painted layer it also drags the layer with the Move
tool (one `Transform Layer` entry), previews a typed rotation through Free Transform until Apply, cancels with
Escape, nudges with Shift+Right and flips the canvas and the layer. With the selection tools it drags a Marquee
rectangle, adds one with Shift, switches the shape with M, moves the outline, deselects with a click, selects a
painted layer's pixels exactly with the wand, drives Ctrl+Shift+I, Ctrl+A and Ctrl+D, closes a polygonal lasso with
Enter, expands from the header and cancels an outline with Escape. `--screenshot ... --demo transform` writes
`docs/screenshots/transform-96.png`: two painted layers, the rotated one's box and the transform header;
`--demo selection` writes `docs/screenshots/selection-96.png`: an elliptical and a rectangular selection with the
Marquee's header; `--demo brush` writes `docs/screenshots/brush-96.png`: a backdrop with soft, hard, translucent
and erased strokes under the Brush header and the tip cursor. With the brush tools the self-test paints a stroke
through the compute engine (a live preview, then one sparse `Brush Stroke` entry drawn from the adopted texture),
erases it, drives `[` `]` Shift-`[` and a digit, and cancels a stroke with Escape. Keyboard Escape leaves the
canvas. With the retouch tools the self-test paints a mark, stamps it from a source with Clone Stamp, blends the
stamp away with Spot Healing, drags it with Smudge through a live warp, blurs it, previews and applies a Gradient
with Enter after a digit set its opacity, adds a Rectangle layer with the Shape tool, switches the kind with Shift-U,
drops a draft with Escape and samples the shape's color with the Eyedropper. `--demo retouch` writes
`docs/screenshots/retouch-96.png`: a radial gradient, a rounded rectangle and an ellipse under the Shape header.
`HostTests` covers tool tables, glyphs and the string table.
