# Application chrome

Status: current normative chrome contract
Last reviewed: 2026-10-08
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

Languages (C14): a translated table ships as a resource-only module `<exe dir>\Languages\<tag>\RedPrism.resources.dll`
built from the same `.rc` with the same ids (`Common/Localization.*`). An ordinary start tries the `language`
setting's tag then its primary subtag, then each Windows display language and its primary subtag
(`SatelliteCandidates`), loads the first that exists as a data file and hands it to `SetStringResourceSatellite`;
`LoadStringResource(nullptr, id)` reads the satellite first and falls back to the embedded English for an id it
lacks. The embedded table is English, so an English tag ends the candidates after its own family: `language: "en"`
keeps English over a satellite for the Windows display language, and an English display language ahead of another
one keeps it too (a regional English table such as `en-GB` may still load). No translation ships in v1; the harness
switches stay English. `HostTests` `locale.*` proves the candidate order, the English stop and the override.

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
across resizes and reports every change (`ChromeEvents::OnChromeLayout`), and the host re-syncs the viewport on the next
frame. Pane bounds are applied on the next `Prepare`, never inside the change callback. That layout keeps the drag
while the splitter stays enabled and visible. There is no app-owned split geometry any more.

## Tool rail

The rail is a DxUi `Toolbar` holding fifteen icon-only `Button`s in `EditorTool` order (Move, Marquee, Lasso, Magic
Wand, Crop, Brush, Eraser, Spot Healing Brush, Clone Stamp, Smear, Gradient, Shape, Eyedropper, Hand, Zoom).
`Toolbar` is a plain panel with no automatic layout and `Toggle` paints a switch, so the host lays the buttons out
itself: 48 DIP wide by 40 DIP tall, flush with the rail so a hover or click on the left edge hits the tool, the
glyph centered, 2 DIP apart, top to bottom from the tab band (`ChromeHost::ToolButtonBoundsDip`),
hidden when they would overlap the palette wells or the status strip. When at least one tool does not fit, the last
slot that still fits is a More button (`ellipsis-vertical`, `IDS_MORE_TOOLS`) and the tools that do not fit move
into a card opened from that button, to the right of the rail. Choosing a tool, pressing Escape, or clicking
outside the card closes it. The selected tool is the button with the pressed visual
(`Button::SetPressedVisual`), re-applied on every `Prepare`. Each tool button strokes its SVG at about 22 DIP in the
button text color. Lucide icons keep their upstream paths and are drawn with a 1.25 stroke (the shipped files use 2,
which is too heavy at this size). The bandage is the Lucide icon rotated 45°. Smear uses Lucide `droplet`. Gradient
and Shape are authored pictures (`RedPrism/Icons/gradient.svg`, a currentColor fade, and `shape.svg`, a square
overlapping a circle), not Lucide files. Segoe Fluent Icons (`EditorToolGlyph`) are the fallback when that
document cannot be created, drawn with DxUi `FontRole::Icon` when that font is installed and with a Unicode
stand-in otherwise. A tooltip is `"<name> (<chord>)"` with the tool's effective chord (a settings replacement shows
there; without a chord the tooltip is the name) and the accessible name is the tool. `HostTests` proves every
Fluent fallback glyph exists in the installed icon font and that keys, glyphs and names are unique. The self-test
creates all fifteen documents and requires every visible tool button to draw its icon. The documents belong to the
chrome's device: a device replacement releases them with the old device and the rail makes all fifteen again on
the new one, and none survives the chrome ([`Core_DxUiIntegration.md`](../Core/Core_DxUiIntegration.md) "Host
boundary"). A tool that does not fit
the rail is created but not drawn until More opens it. The self-test opens More when the rail overflows, chooses
the first hidden tool from the card, and returns to Move.

| Tool | Key | Lucide | Fluent fallback |
| --- | --- | --- | --- |
| Move | V | move | U+E759 |
| Marquee | M | square-dashed | U+EF20 |
| Lasso | L | lasso | U+F1BE |
| Magic Wand | W | wand-sparkles | U+F4A5 |
| Crop | C | crop | U+E7A8 |
| Brush | B | brush | U+E76D |
| Eraser | E | eraser | U+E75C |
| Spot Healing Brush | J | bandage | U+E95E |
| Clone Stamp | S | stamp | U+E8C8 |
| Smear | R | droplet | U+F271 |
| Gradient | G | authored gradient | U+EF90 |
| Shape | U | authored square and circle | U+E739 |
| Eyedropper | I | pipette | U+EF3C |
| Hand | H | hand | U+EB8A |
| Zoom | Z | search | U+E8A3 |

Tool keys apply when the canvas has focus, with or without Shift (as the legacy canvas read the letter); Space pans
while held with the canvas focused. X and D are palette commands (`kCommandSwapPalette`, `kCommandResetPalette`),
not anonymous keys: the canvas key and the rail hot zone both call `Application::OnCommand`.

The rail buttons are icon-only DxUi buttons (a DxUi toggle paints as a switch, glyph or not); the active tool's
button carries the pressed look as its latch, and `SyncToolVisuals` writes a latch only between presses — while a
press is in flight it defers to the release — and only when a button's own pressed state differs (DxUi clears it on
release). Three rules keep a mouse press alive across the frames it spans (DxUi cancels a press held across a
frame when its focus leaves another control or a layout write reaches the tree): a press on the chrome first
releases DxUi's focus (`ChromeHost::ReleaseFocusForPress`; a text edit commits when the press lands outside its
control and continues when it lands inside), the arrange paths write bounds and visibility only when they change
(`PlaceControl`, `ShowControl`), and the pressed looks of the adjustment panel's eyedroppers and targeting button
change only when their state does. The accessibility placement flushes and the UIA attachment wait for the pointer
to be up. The self-test clicks rail buttons through the pointer path: plainly, with the window focused, under a
tooltip, and held across frames after a tool switch, after a click on another rail button and with the opacity
stepper focused.

## Canvas interactions

One pointer interaction runs at a time (`Application::_drag`; a brush stroke, a warp and a shape draft are the
session's own pending edits). A press on the canvas with nothing in flight begins the one its tool picks (with Space
held or the Hand, a pan); its moves and its release go to it until it ends, whatever the tool or Space say by then and
over the chrome too, as the view that took the press keeps the legacy canvas's drag; another button pressed while one
is in flight does nothing. The pointer leaving the window with no capture holding it ends a drag as its release would;
a pointer loss ends it as [`UI_Windowing.md`](UI_Windowing.md) "Pointer loss" says.

The edits under way have one order that Escape, Enter, Undo and a settle consult (`Application::CanvasEdit`): the size
card and an open panel first (Modal cards), then lifted pixels, a warp, a stroke, a shape draft, an outline move, a
lasso or marquee outline, a pending gradient, a pending transform and the Crop tool's frame. Escape cancels the first
one under way (an outline move goes back where its drag began), and Enter applies the first that has an OK (lifted
pixels, a warp, a stroke, a shape draft and an outline move have none). The first Undo or Redo drops the first one
under way with its drag and preview and leaves history where it is; an open panel keeps history where it is until OK
or Cancel; a lasso or marquee outline and the crop frame go and history still moves; the size card is not a document
edit and history moves under it. A tool switch or a document on its way out ends every one: a stroke, a warp, a
gradient and a transform record their entries, a panel applies (a tool switch) or cancels (a document on its way out),
an outline move keeps its place, and lifted pixels, a shape draft, an outline in progress, the crop frame and the size
card drop. Save, Save As and Export record what Save would (Decision D2, Files) and leave the rest.

When a pending edit does not consume Undo or Redo, the command enters the ordered history-work stream. A worker
prepares the complete restore and history before either stack or the live document changes. A younger command
waits for acknowledgement and observes the restored state; document replacement/removal invalidates older
queued lineage. An empty stack or an open transaction preserves the ignored-key behavior and crop without
capturing history. Current failure posts the localized history-restore notice and preserves the document.

An admitted ordinary brush whose pointer input is sealed is no longer an uncommitted gesture consumed by
Escape or the first Undo. Escape preserves that original stroke and clears other queued inputs; Undo/Redo
queues behind its commit and observes history at dequeue. This applies while worker opening is pending as
well as after preview starts, and while the stroke waits behind an older opacity or sampling request.

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
Ctrl is held; drags land on whole pixels and degrees and commit on release. Losing the pointer capture or the focus
mid-drag puts the box back where the drag started (a drag-only edit drops, an Alt-drag's duplicate with it;
[`UI_Windowing.md`](UI_Windowing.md)). Cursors: the resize arrows follow the box's rotation, the knob and a
distortion show a cross, a movable layer shows the four-way arrow.

Keys with the canvas focused: arrows nudge by one pixel (Shift ten), Enter applies and Escape cancels a pending
edit, Ctrl+T starts Free Transform (a persistent edit) and selects the Move tool; with a non-empty selection it
transforms the selected pixels instead (`TransformCommand`: a `Floating Selection` layer that merges back as one
`Transform Selection` entry on Apply or Enter, restored exactly by Escape or Undo, the outline following the handles
and a distortion). Undo during a pending edit cancels it and ends a drag in progress with it, so moving on neither
moves nor duplicates (an Alt-press, Ctrl+Z, then a drag adds no layer); switching tools or selecting another layer
commits it. The transform contract itself lives in
[`Document_Tools.md`](../Document/Document_Tools.md).

The bottom 48 DIP of the rail are one palette control. The foreground and background squares, 20 DIP each, overlap
by 6 DIP, foreground in front, so both colors stay readable. Clicking a square opens the color picker for that
color. The step leaves two 14 DIP dark notches, and each notch is the icon and its hotspot. `move-diagonal-2` fills
the notch at the black square's top-right and swaps the two colors (the same exchange as X). `rotate-ccw` fills the
notch under the black square and left of the white one, and restores black over white (the same reset as D).
Neither notch covers a swatch, so the glyph stays on the rail and a click there does not open the picker. Hover and
press fill that notch; the glyph stays on top. Both have accessible names (`IDS_PALETTE_SWAP`, `IDS_PALETTE_RESET`)
and tooltips that add the command's effective chord (`"<name> (<chord>)"`, `IDS_TOOL_TOOLTIP`); the brush
header's mode, size and hardness tooltips add the chords of the tool, size and hardness commands the same way.

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

`CanvasOverlay` draws the selection outline in every tool as marching ants (a white line under a black 4/4 dash).
The dash offset steps one DIP every 120 ms through its 8 DIP cycle (legacy `EditorCanvas` ants timer) while a
non-empty outline is showing and the window is visible, powered, and not occluded: the frame loop waits for the
next step with a timed message wait and presents one frame per step, never a frame per vsync; each step's dash style
is made once and kept. The overlay holds its immutable source loops and caches points relative to the source's
construction-cached minimum bounds. Pixel Move carries its rounded offset separately. Pan, zoom, DPI and Pixel
Move offsets update only the scalar world mapping; they neither translate the loop arrays nor rebuild the path.
The white stroke and black 4/4 dash use fixed stroke transforms: their width and dash lengths stay in DIP while
the geometry follows the view. The path is rebuilt when its source identity or Direct2D factory changes; the
cache retains both owners to prevent address reuse. Explicit empty selection remains distinct from no selection;
either stops animation and releases the preceding path. The pinned single-threaded factory remains owner-only.
Initial point conversion/path construction and large last-owner retirement remain P1/S5 work. A settled
document with no selection does not stay awake for the dash. The draft being drawn (freehand and polygonal
polylines, the polygonal rubber band to the cursor and an 8 DIP handle on the first corner, the Marquee's rectangle
or ellipse) is a white line over a dark one. Outline and draft changes re-composite chrome only, never the document.

Pointer rules in the hole (`Application::SelectionPress` / `SelectionPointerMove` / `SelectionRelease`): the
modifiers at the press pick the mode (Shift adds, Alt subtracts). In New mode a press inside a non-empty selection
moves its outline (Shift keeps the move on the axis it has gone further along; one `Move Selection` entry; Escape
or Undo during the drag puts it back where it began and ends the drag, with no entry), and a press released without
moving deselects (with the wand, it selects afresh from that pixel). A Ctrl press inside the
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
Selection for the active layer; an outline the tracer refuses says why, as the wand's refusals do
(`IDS_ERROR_SELECTION_DETAILED` past 8 000 000 pixel edges, `IDS_ERROR_WAND_MEMORY` when it cannot be allocated),
while a layer or mask with nothing to trace loads nothing without a word. Pixel edits: Ctrl+arrows nudge the selected pixels in any tool (one entry each);
Delete clears them, and without a selection deletes the targeted mask, or the layer; Shift+Delete opens
Content-Aware Fill on them ([`Document_Tools.md`](../Document/Document_Tools.md)); Ctrl+X, Ctrl+C, Ctrl+Shift+C
and Ctrl+V cut, copy, copy merged and paste (`ApplicationClipboardJobs` admits the selected command and
`ClipboardTransfer` publishes PNG and a 32-bit DIB with straight alpha or reads another app's owned PNG/DIB on a
worker. Foreign pixels are centered on the canvas; a sealed receipt binds the private copy to system content.
A private copy that could not reach the system clipboard remains the newest until the system changes again.
Only a nonzero matching clipboard sequence may prove that private preference. A zero/unavailable query
must continue through the ordinary worker inspection and bounded retries, never substitute private pixels
for an offered foreign image. Inspection's no-image fall-through and malformed-image refusal remain unchanged.
Acquisition retries retain the original job and reservation, with at most six total Inspect/Copy acquisitions.
Copy and Copy
Merged take the pixels the canvas shows, so a pending gradient applies first as its own entry and its preview ends
with it); Alt+Backspace and
Ctrl+Backspace fill with the foreground and background
colors; Ctrl+I inverts; Ctrl+J is Layer via Copy. Ctrl+J uses the ordered worker path: with a selection it adds
the copied pixels at their document origin as a sibling layer and clears the outline; without one it duplicates
the active layer record. The new layer is selected and the mask target clears in either case. The operation leaves
the private and OS clipboards untouched. The selection and publication contract itself lives in
[`Document_Tools.md`](../Document/Document_Tools.md).

## Crop tool

The Crop tool (C, legacy `Crop.swift` / `CropControls.swift`) frames the canvas to come. Choosing the tool frames
the whole canvas (`DocumentSession::SetCropFrame(VisibleCropRect())`); leaving it, a document on its way out or a
tool switch drops the frame. The frame draws as the transform box without its rotation knob (`SetTransformOverlay`
with `showsRotation` off) and everything outside it dims (`CanvasOverlay::SetCropShade`, 45 % black). Header:
the title, Ratio (Free, Original — the canvas ratio —, 1:1, 4:3, 16:9; a choice re-shapes the visible frame about
its center keeping its width, `ChangeCropRatio`), the frame's size in pixels, Cancel and the primary Apply Crop.

Pointer (canvas hole, the Crop tool): a press on a handle resizes (`CropDrag::Mode::Resize`, Alt about the center,
a fixed ratio kept through `TransformDrag`'s ratio lock), a press inside the frame moves it, a press outside starts
a new frame that a drag draws from the press point (a click alone leaves the frame as it is); the pointer's
document point drives `CropDrag::Updated`, then `CropSnap::Apply` with the canvas edges and every visible pixel
layer's upright bounds as targets and `TransformSnap::kDistanceDip` scaled to document pixels as the tolerance,
and a valid result becomes the frame on every move. Cursors: the resize arrows over a handle, the four-way arrow
inside the frame, a crosshair outside. Enter or Apply Crop resizes the canvas to the frame as one `Crop` entry
(`CommitCrop`: the content moves by the frame's origin and is never resampled; a frame equal to the canvas records
nothing); Escape or Cancel drops the frame and the whole canvas frames again. The self-test frames the canvas by
choosing the tool, drags the bottom-right handle to 640 × 400 through the pointer path, crops with Enter, moves
the frame by a drag inside it, drops it with Escape, squares it through the header's ratio, cancels, chooses the tool
again and undoes the crop with Ctrl+Z (the tool's own whole-canvas frame takes no Undo; the restored canvas frames
whole), and leaves the tool.

## Brush tools

The Brush and Eraser share one header (`ChromeHost::BuildBrushHeader`, mirrored from the session through
`BrushInspectorState`, never re-laid out from a chrome callback): the tool name; for the Brush a Mode (Paint /
Erase); Size (1–2000 px), Hardness (0–100 %) and Opacity (1–100 %) steppers; and, while the active layer's mask is
selected, Paint (Black - Hide / White - Reveal) with a `Mask` note pinned right, otherwise Color with the foreground
`ColorSwatch`, which opens the color picker like the rail's well. The stroke paints that foreground color. Strings are `IDS_BRUSH_*`; the budget error is
`IDS_ERROR_BRUSH_BUDGET`.

`CanvasOverlay` draws the tip as a ring of the brush diameter around the pointer (a white 2.5 DIP line under a
black 1 DIP one; a crosshair when the ring would be under 4 DIP across) whenever a brush tool is over the canvas of
a document, and the system cursor is hidden there so the ring reads alone; over the chrome, outside the hole and
without a document the arrow or crosshair returns. The ring follows the zoom.

Pointer rules in the hole (`Application::BeginBrushStroke` / `ContinueBrushStroke` / `FinishBrushStroke`): a left
press starts a stroke on the active layer, or on its mask when the mask is selected (Shift-click paints a straight
line from the end of the previous stroke on that target); moving adds samples and shows them at once through the
composition's stroke preview; release, the pointer capture or the focus going elsewhere (a dialog, a menu, another
app; [`UI_Windowing.md`](UI_Windowing.md)), the pointer leaving the window once no drag holds the capture, and
switching tools each commit one history entry (`Brush Stroke`, `Erase`, `Paint Mask`); Escape drops the stroke while
its pointer sampling remains open. After accepted release/loss, Escape preserves the recorded prefix through
worker acknowledgement and Undo queues behind its commit. The Eraser and the Brush's Erase mode both
erase; a mask is never erased but painted black or white. A stroke over the pixel budget or an engine failure is
dropped with `IDS_ERROR_BRUSH_BUDGET`. A press does nothing on a folder without its mask, a hidden or adjustment
layer, a disabled mask or an empty selection.

Keys with the canvas focused: B chooses the Brush and E the Eraser; `[` and `]` step the size by a fifth (at least
one pixel); Shift-`[` and Shift-`]` step the hardness by quarters; 1–9 set the opacity to 10–90 % and 0 to 100 %,
two digits within 0.6 s make a two-digit percent (with the Move tool a digit sets the active layer's opacity the
same way, one `Layer Opacity` entry per key that changes it: legacy `LayerAppearanceTests.
moveToolNumberKeysSetSelectedLayersOpacityAsOneUndo`), and holding a digit sets it once (its auto-repeat would pair
with the press: 5 then 5 is 55 %); Escape cancels a stroke in progress. The stroke contract lives in
[`Rendering_Brush.md`](../Rendering/Rendering_Brush.md), the session API in
[`Document_Tools.md`](../Document/Document_Tools.md).

## Retouch tools

Spot Healing, Clone Stamp and Smear share the brush header (`BrushInspectorState`), which adds per tool: Type
(Content-Aware / Create Texture / Proximity Match) for Spot Healing; Aligned and This Layer / All Layers for Clone
Stamp, with `Alt-click to set the source` pinned right until a source exists; Mode (Liquify / Blur / Smudge) for
Smear, whose opacity is labelled Strength. Neither Clone Stamp nor Smear shows the color swatch. The tip ring is
their cursor, except while Alt is held with Clone Stamp: the ring hides and the system cursor becomes a crosshair,
so the click that sets the source is waiting. Clone Stamp adds a crosshair at the point it copies from. The crosshair stays on the source until a
stroke fixes the offset, then follows the pointer while that stroke is dragged and afterwards. Pointer rules: Alt-click with Clone Stamp sets the source; a press starts a stroke of the tool's kind
(Smear's Liquify and Smudge start a warp instead, shown live through the stroke preview), Shift-click a line;
release, capture or focus loss, leaving the window without a capture, or a tool switch commits, Escape drops; a stroke on a mask does nothing for Spot Healing
and Clone Stamp and reports `IDS_ERROR_SMEAR_MASK` for Liquify and Smudge; Clone Stamp without a source reports
`IDS_ERROR_CLONE_SOURCE`. Switching between the Brush family (Brush, Eraser, Spot Healing), Clone Stamp and Smear
parks each family's size, hardness and opacity.

The Gradient header (`ChromeHost::BuildGradientHeader`, `GradientInspectorState`): Shape (Linear / Radial), the two
end colors as swatches (either opens the foreground picker), Colors (Foreground to Background / Foreground to
Transparent), Reverse, Opacity (1–100 %), a `Mask` note, and Cancel / Apply pinned right while a line is pending.
Pointer rules: a press starts a line and drags its end; a press within 10 DIP of a pending line's end drags that end
(Shift snaps the line to 45°); a click without a line pends nothing. The pending gradient shows through the
composition's stroke preview and the overlay draws the line with a handle at each end. Enter or Apply commits, Escape
or Cancel drops, Undo drops, and switching tools, layers or targets applies it: its preview ends by adopting its
working texture for that layer and target only, and a line then started on another layer previews on a texture of
its own (the self-test commits a line on one layer, chooses another, commits a line there with Enter and finds each
layer drawn exactly as its raster). Digits set the gradient's opacity. Palette changes re-render the pending line,
and so does the brush header's Paint choice for a line on a mask.

The Shape header (`BuildShapeHeader`, `ShapeInspectorState`): Rectangle / Ellipse, Radius (px, rectangles only) and
Fill (the foreground swatch). A drag draws the shape as a draft on the overlay, filled with the foreground color and
outlined (Shift squares, Alt grows from the center); release adds the layer; Escape or a tool switch drops the
draft; Shift-U switches the kind (U alone selects the tool). Release owns its draft and foreground and queues one
worker commit. Its outline remains visible until acknowledged apply/drop while view input dispatches; younger
mutations wait behind that record. Ordinary pointer leave commits, and capture loss cancels the live gesture.

The Eyedropper (I), or Alt with the Brush, Spot Healing and Gradient between strokes, reads the composited canvas
under the pointer while pressed into the foreground color, or into the open color picker; the overlay shows the
legacy comparison ring (the new color above, the color before the press below) unless Sample Ring is off in the View
menu. Its cursor, and the Gradient's and Shape's, is the crosshair. Every string is a STRINGTABLE entry
(`IDS_HEAL_*`, `IDS_CLONE_*`, `IDS_SMEAR_*`, `IDS_GRADIENT_*`, `IDS_SHAPE_*`).

## Adjustment panel

Every color adjustment and filter edits in one card (`ChromeHost::BuildAdjustPanel`, `ChromeHostAdjust.cpp`) that
floats over the top-right of the canvas hole below the header while a session edit is open, except during its
accepted matching cancellation (`AdjustInspectorState`
mirrors the session; the panel never re-lays out inside a drag callback). It shows the edit's title (`<Kind>
Adjustment` when an adjustment layer is edited), the kind's controls, a Preview checkbox, a `Limited to the
selection` note for a pixel layer with a selection, and Cancel / OK (Enter and Escape do the same, with the canvas
focused or as the keys a chrome control leaves: Modal cards). Value rows are a label, a `Slider` (logarithmic
for radius, distance, amount, gamma and grain size, so the small values get most of the travel) and an exact
`NumericStepper`; typing into one previews live and is never reformatted out from under the typing (the stepper
stays in its own edit until Enter, a step button or focus loss commits it, the same rule the transform, selection,
brush, gradient and shape headers use). The drawn controls live in `RedPrism/AdjustControls.*`:

`AdjustInspectorState::busy` freezes mutation controls while preserving Cancel. Disabled drawn editors use the
theme's disabled colors and stop their captured gestures through the pinned host's input-coherence checks.
Leaving busy restores the existing prerequisites: Levels Auto requires the opening histogram, Curves Remove
requires an interior selected point, Hue/Saturation's range remains disabled while Colorize is selected, and a
donorless fill keeps OK disabled. Busy suppresses late mutation callbacks, including a disabled stepper's focus-loss
commit. Numeric text follows the existing focus rule: partial text stays while editing, focus loss formats its
parsed value, and a complete decimal such as `2.5` keeps that value. This control contract is
used by normal OK/write callers through the ordered engine and their owned continuations. An accepted OK freezes
mutation callbacks immediately, before the source admits its worker snapshot. Cancel stays available.

An accepted Cancel or Escape hides that exact opening's card immediately. The logical panel, source, settings,
original ticket and history barrier remain owned until the closure acknowledges its outcome. A refused admission
leaves the card visible. A failed or stale closure, or a queued cancellation that is discarded, restores the still-open
card with its unchanged values and normal prerequisites; pending display is requested again through its admitted
owner route. Another opening or document is never hidden by an old receipt. Shutdown does not reopen or refresh
chrome. Visibility feedback does not acknowledge pixel work, native I/O, GPU use or resource retirement.

- **Curves**: the channel choice, a `CurveEditor` (the 256² map with a grid, the channel's Hermite curve and its
  points; a click adds a point, a drag moves one with its x kept between its neighbors and the ends fixed, the
  selected point shows `Input · Output`), Remove point (never the ends) and Reset.
- **Levels**: the channel choice, a `LevelsBar` with the channel's histogram (the display scale of
  `Adjust::HistogramDisplayScale`) and the black, gamma and white handles, the input steppers, the output ramp
  with its two handles and steppers, the Black / Gray / White eyedroppers (an armed one samples the original layer
  on the next canvas click; pressing it again stops) and Auto Contrast / Color / Neutral.
- **Hue/Saturation**: the range choice, the Sample / Add / Remove eyedroppers and the Targeted toggle (a color
  range only; a canvas click samples, a Targeted drag changes that color's saturation, or its hue with Ctrl), the
  Hue (±180°, 0–360° colorizing), Saturation and Lightness rows, the `SpectrumEditor` (the hues as they are, the
  four band handles in the row between the two hue strips, the hues as they leave; a press in that handle row
  drags the nearest handle to it, a press on either strip does nothing, matching legacy) with
  `Apply outside this range instead`, and Colorize.
- **Exposure**: Exposure, Offset, Gamma. **Gradient Map**: a `GradientRamp` of the ends, the Shadows and
  Highlights swatches (each opens the color picker on that end; its preview follows the working color, Cancel
  restores it, OK keeps it, the palette is untouched) and Reverse. **Grain**: Amount, Size, Roughness.
- **Gaussian Blur**: Radius. **Motion Blur**: Angle, Distance. **Add Noise**: Amount, Uniform / Gaussian,
  Monochromatic. **Lens Correction**: Remove Distortion with its hint.
- **Content-Aware Fill** (the Edit menu, Shift+Delete; C13): a hint only. When nothing unselected and opaque
  surrounds the selection the card adds the legacy no-source message under it and OK is disabled; Enter is refused
  by the session, Escape closes, and a tool switch drops the failed fill instead of applying it.

Pointer input on the card belongs to the chrome (`DispatchPointer` excludes it from the hole). The Image and Filter
menus, the Layer menu's `New Adjustment Layer` submenu (Levels, Curves, Hue/Saturation, Exposure, Gradient Map,
Grain) and `Edit Adjustment…` (the active adjustment layer) open the panel; a new adjustment layer opens it at
once, over the composite of the layers beneath it (`Application::CompositeBelow`). Switching tools or opening
another panel applies the open one. The session contract lives in
[`Document_Tools.md`](../Document/Document_Tools.md).

## Menus and accelerators

The MenuBar lists File, Edit, Image, Layer, Select, Filter, View, Help. Flyouts are native `HMENU`s shown through
`DxUi::ShowNativeHMenuContextMenu` (the DxUi-supported path); a menu with no items opens nothing. A flyout hangs
from its item's bottom-left on screen: the embedded view owns no window, so `ChromeHost::MenuAnchorScreen` takes
the item rectangle DxUi reports and translates it by the difference between the owner's client origin and the
origin DxUi assumed (`DipPointToScreenPoint` of the view's origin); the self-test checks the File menu's anchor.

| Menu | Commands |
| --- | --- |
| File | New (Ctrl+N), New Canvas…, Open… (Ctrl+O), Import Images…; Save (Ctrl+S), Save As… (Ctrl+Shift+S); Export PNG… (Ctrl+Shift+E), Export JPEG… (Ctrl+Alt+Shift+S); Close Project (Ctrl+W); Exit |
| Edit | Undo <name> (Ctrl+Z), Redo <name> (Ctrl+Shift+Z, Ctrl+Y); captions carry the history entry name and gray out when unavailable; Cut (Ctrl+X), Copy (Ctrl+C), Copy Merged (Ctrl+Shift+C), Paste (Ctrl+V); Fill with Foreground Color (Alt+Backspace), Fill with Background Color (Ctrl+Backspace), Clear Selection Pixels, Content-Aware Fill… (Shift+Delete); Invert / Invert Mask (Ctrl+I); Free Transform / Transform Selection (Ctrl+T); the Invert and transform captions follow the target and the selection (`SetEditTargets`) |
| Image | Curves… (Ctrl+M), Levels… (Ctrl+L), Hue/Saturation… (Ctrl+U), Exposure…, Gradient Map…, Grain…; Canvas Size… (Ctrl+Alt+C), Image Size… (Ctrl+Alt+I); Flip Canvas Horizontal, Flip Canvas Vertical |
| Layer | New Layer (Ctrl+Shift+N), New Folder (Ctrl+G), Layer via Copy (Ctrl+J), New Adjustment Layer › Levels / Curves / Hue/Saturation / Exposure / Gradient Map / Grain, Edit Adjustment…; Group Selected Layers (the active layer, or an empty folder, wrapped in a new `Folder N`), Move Out of Folder (Ctrl+Shift+G), Rename Layer… (F2), Delete Layer / Delete Layer Mask (the caption follows the target; the Delete key clears selected pixels first, canvas focused), Toggle Visibility, Move Layer Up (Ctrl+]), Move Layer Down (Ctrl+[) (a swap with the next sibling, `Reorder Layers`); Create Clipping Mask / Release Clipping Mask (Ctrl+Alt+G; the caption follows the active layer: a non-folder layer clips to the sibling below it, sharing that sibling's base when it is clipped itself), Merge Down / Merge Group / Merge Layers (Ctrl+E; the caption follows the plan); Add Mask, Add Black Mask (from the selection when there is one; production commands use one ordered Commit whose apply/drop acknowledgement gates younger mutations), Target Layer Mask / Target Layer Pixels, Unlink / Link Layer Mask, Disable / Enable Layer Mask (the active layer's mask: the captions follow it, grayed without one; no default chords, as legacy had none); Flip Layer Horizontal, Flip Layer Vertical |
| Select | All (Ctrl+A), Deselect (Ctrl+D), Inverse (Ctrl+Shift+I), Load Layer Selection, Load Mask Selection |
| Filter | Gaussian Blur…, Motion Blur…, Add Noise…, Lens Correction… |
| View | Fit on Screen (Ctrl+0), Actual Pixels (Ctrl+1), Zoom In (Ctrl+=), Zoom Out (Ctrl+-), Sample Ring (a check, on by default) |
| Help | Command List, About RedPrism (the running module's file version from its own `VS_VERSIONINFO`, which carries the stamped build number, through `RedPrism/VersionInfo.h`; the compile-time `Common/Version.h` string only when that cannot be read). Help is the right-justified item on the menu bar |

Every finished user action is a command, with no exception for an action whose only control is local. A canvas key,
a menu item, and a rail hot zone are the same kind of action. Each one MUST have one stable integer id in the
command catalog (`RedPrism/CommandCatalog.h`: `CommandIds`, which `ChromeHost` derives from, so the ids read as
`ChromeHost::kCommand…`). Settings name that command with a stable text id (`cmd/file/new`,
`cmd/edit/swapPalette`) held on the same catalog row; the integer does not change when the chord does. The row also
holds the command's caption, where its keys apply (its scope and condition, below), whether Shift may be added to
its chord and whether auto-repeat runs it again; the catalog's default chords (`DefaultChords`) list every command's
chords, a first one and its aliases, including commands without a menu row. The caption's tab column is the first
default chord's canonical label (`CanonicalChordText`), which `HostTests` compares to the STRINGTABLE. The Command
List lists the catalog's rows. A key, menu item, or control runs the action only by calling `Application::OnCommand`
with that id. The key or the control does not call the session operation itself. Swap foreground and background is
`kCommandSwapPalette` (default X). Restore black over white is `kCommandResetPalette` (default D). A letter chord
(`Ctrl+S`, `V`, `X`) is the virtual key of that letter, so it follows the key that the active layout labels with that
letter. It is not a fixed column on a US keyboard. The chords whose legend is not a letter are a physical key
instead, set-1 scan code, the same rule RedSalamander uses for zoom. A key message without a scan code (a macro pad,
`SendInput` with a virtual key only) takes the scan code the active layout gives its virtual key. A layout change
retitles the displayed key and does not move the binding. Numpad `+` and `-` stay `VK_ADD` and `VK_SUBTRACT`, separate
from the number row.

One effective binding table (`RedPrism/CommandBindings.h`, built by `Application::ApplySettings` from the catalog
defaults and the settings' replacements: [`Core_Settings.md`](../Core/Core_Settings.md) "Command chords") is what the
keys do and what the chrome shows. A key down goes, in order (`RedPrism/ApplicationKeyboard.cpp`):

1. Escape closes the floating card on top, wherever the focus is: More tools, the notice, the Command List.
2. With the canvas focused, the gestures of an edit under way, which are not commands, in the order of Canvas
   interactions: Enter and Escape of the size card, an open panel, the Crop frame, a pending gradient or transform;
   Escape of a pixel drag, a warp, a shape draft or a stroke; Enter, Escape, Backspace and Delete of a lasso outline
   in progress. Enter and Escape take that path also from a chrome control a click focused
   (`ChromeHost::FocusTakesEnterAndEscape`, as Space in 4): after a click on Flip Horizontal during Ctrl+T, Enter
   applies the transform with that one flip instead of pressing the button again and Escape cancels it with no
   entry, and after a click on a size card's checkbox or closed dropdown they are the card's OK and Cancel (the
   legacy Apply and Cancel are the default and cancel buttons, and a macOS button never takes the keyboard). A
   control the keyboard reached, a text input (Escape reverts its typed edit), a dropdown showing its list (Enter
   chooses from it, Escape closes it) and a control in a card that took the keyboard (the notice, the Command List,
   the Rename card, the picker) keep both keys; with no edit under way a clicked control keeps them too.
3. The table: the first binding whose chord matches and whose scope and condition hold runs its command.
4. Space, which pans while held wherever the keyboard is, except in a chrome text input and on a chrome control the
   keyboard reached (Tab, or a key that moved the focus to it), which takes Space as its press. A click leaves its
   control focused and Space after it still pans: a clicked layer row opens no Rename card, a clicked button or
   palette zone does not run again (a macOS button never takes the keyboard from the legacy canvas, so there too
   Space after a click pans).
5. With the canvas focused, the canvas's own keys: the opacity digits.
6. The chrome (DxUi), unless the canvas has the focus: a key the canvas did not use goes nowhere, never to the chrome
   control that held the focus before the canvas took it; Escape and Tab move the keyboard to the chrome. A held
   Space or Enter presses a chrome control once: its auto-repeats go nowhere. An Enter or Escape (without Ctrl) the
   focused control leaves is the OK or Cancel of a modal card (below).

Modal cards (`ChromeHost::PushCard` / `PopCard`): the notice, the Command List, the Rename card, the size cards, the
adjustment panel and the color picker stack in the order they open, each remembering where the keyboard was. The
notice, the Command List, the Rename card and the picker take the keyboard when they open (their OK button, search
field, name field, picker); the size cards and the panel do not. A card that holds the keyboard when it closes gives
it back: to the chrome control that had it (never the menu bar a click left it on), else to that of the card now on
top, else to the canvas. An Enter or Escape its focused control leaves goes to the card holding the focus, else to the
top card: the notice's OK, the Command List's Escape (Enter runs a row only from its field or list), the Rename card's
OK and Cancel, the size card's and the panel's OK and Cancel, the picker's OK and Cancel. DxUi's embedded host routes
no default or cancel button, so the chrome sets none.

A key a command or a gesture took types no character. The message loop dispatches a key down before it translates it
(`Application::DispatchQueuedMessage`, the order `TranslateAccelerator` keeps) and translates it only when the key down
left its character: no character reaches a dialog the command opened, no flag is left to drop a later one, and a dead
key a command sits on (French `^` is Brush Smaller) arms no accent for the next text. A key the menu bar used types
nothing either; a loop that translates first (a modal one) still has the character dropped (`WM_CHAR` / `WM_SYSCHAR`).
Neither does a key down that moved the keyboard (Tab, a button or a row that opened a card, a command that closed one),
and until that key is released its repeats go nowhere: Space or Enter held on the Command List's Rename Layer row
neither fills the card's field with spaces nor presses its OK. Tab and the arrows keep moving the keyboard while held.
Matching:

- Exact modifiers: a chord matches with its own Ctrl, Alt and Shift only. A command marked for it also takes its
  chord with Shift added: the tool letters, X and D (the legacy canvas read the letter, Shift or not), the M and L
  switches, and the nudges, which step by ten with Shift added (a nudge whose own chord holds Shift, a replacement
  on Shift+J, steps by one). An exact chord wins over one with Shift added (Shift+U switches the shape kind on the
  Shape tool; elsewhere it chooses the Shape tool).
- AltGr arrives as Ctrl+Alt. When the layout types a character for the key with those modifiers (€ on AltGr+E, @ on
  AZERTY AltGr+0, Polish ż on AltGr+Z), the key is text and matches no chord; a chrome text field then sees it
  without Ctrl, so AltGr+Z does not undo in the field. Ctrl+Alt never matches a Ctrl-only chord. Text needs AltGr
  itself (the right Alt without the left one) or a chrome text input: left Ctrl with left Alt stays a chord, so
  Ctrl+Alt+G toggles the clipping mask even where AltGr+G types a character (Czech, Slovak, Hungarian, Croatian,
  Slovenian, Bosnian and Serbian Latin: `]`), while in a text field left Ctrl with left Alt types the AltGr character
  as Windows does. The same holds for the legacy File and Image chords with Ctrl+Alt (Export JPEG, Canvas Size, Image
  Size): the left keys run them on every layout, AltGr only where it types nothing for the key (Polish AltGr+C is
  `ć`, AltGr+Shift+S is `Ś`).
- Scope. Canvas commands (tools, palette keys, nudges of the layer and the outline, brush keys, blend modes, the
  Delete key, Alt+Backspace) need the canvas focused. App commands (the menu chords, F2, Ctrl-arrows) run from the
  canvas and from chrome that is not a text input. Decision D4: while a chrome text input has the focus (the Rename
  card, the Command List search, a stepper being typed in), the field keeps every key; only a global command (the
  File menu: New, Open, Save, Save As, Close Project, the exports, Exit) runs, and only for a chord with Ctrl, Alt or a
  function key.
- Conditions share chords: the arrows nudge the outline with a selection tool and a selection, the layer with the
  Move tool; Ctrl-arrows move the selected pixels in any tool while no outline is drawn; `[` `]` need a brush tool;
  M and L switch the Marquee's shape and the Lasso's kind on that tool (they come before choosing the tool).
- Auto-repeat runs a command again, except the M, L and Shift+U switches: holding the key switches once (the
  opacity digits, a canvas key, set their opacity once too). A replacement keeps its command's scope, condition,
  Shift and repeat rules.
- Decision D5: Alt+F4 (close) and Alt+Space (the system menu) stay the window manager's whatever the settings say;
  Exit shows Alt+F4 and never dispatches it. F10 and a tapped Alt give the keyboard to the menu bar (below).

The chrome shows each command's effective chord: its replacement, else its first default (no aliases). Decision
D9: the modifier names are STRINGTABLE entries (`IDS_KEY_CTRL`, `IDS_KEY_ALT`, `IDS_KEY_SHIFT`, joined by
`IDS_KEY_JOIN`), the key names the active layout's: a letter or digit is the character (its chord follows the key
that produces it); a position key, a named key (arrows, Delete, F2, Backspace, Enter, Space…) and a numpad key use
`GetKeyNameTextW` with the key's set-1 scan code (extended for the navigation keys, so Delete is not the numpad's
`Num Del`), which names numpad `+` and `-` apart from the number row. A key the layout gives no name takes the
table's (`IDS_KEY_NUMBER_ROW_MINUS`, `IDS_KEY_NUMBER_ROW_PLUS`, `IDS_KEY_BRACKET_LEFT`, `IDS_KEY_BRACKET_RIGHT`: the
position keys' US legends; `IDS_KEY_FUNCTION` `F{0}`; `IDS_KEY_VIRTUAL` `Key {0:02X}`), never an English name from
code. The menus' accelerator column
(`ChromeHost::CommandMenuCaption`, also for the captions that follow the target: Undo and Redo with the history
name, Invert Mask, Transform Selection, Delete Layer Mask, Release Clipping Mask, Merge Group, Merge Layers, Target
Layer Pixels, Link Layer Mask and Enable Layer Mask), the
Command List, the rail's tool tooltips, the palette hot zones' tooltips and the brush header's tooltips follow the
table and `WM_INPUTLANGCHANGE` (the table itself is rebuilt then, since a replacement's `vk` may name a position key
on one layout and not on another); a translated caption keeps its own name, its column is rebuilt.

The menu bar takes the keyboard from a tapped Alt or F10 (`WM_SYSCOMMAND` `SC_KEYMENU`, never the system-menu loop
of a window without an `HMENU`): its first item highlights; Left and Right move, Enter, Space or Down open, a letter
opens the menu with that mnemonic, Escape (or Alt, or F10) leaves, and the focus returns where it was, also before
a command chosen from a menu opened this way runs. A pointer press anywhere (a press on the canvas included), the
canvas taking the keyboard, and the window losing the focus or the activation (`WM_KILLFOCUS`,
`WM_ACTIVATEAPP`) leave it the same way, and so does the next key when DxUi dropped the bar's focus (it prunes a
hidden or disabled control); keys go to the menu bar only while the canvas does not have the keyboard.
Alt+letter opens that menu from anywhere; an Alt chord a command took opens nothing and an Alt+key nothing took
does nothing (no beep). Releasing Alt after an Alt chord a command took (Alt+Backspace), before or after the
chord's key, is no tap: every key up, and the key down of an Alt chord a command took, still reach `DefWindowProc`,
which tells a tapped Alt from a chord (it sends `SC_KEYMENU` only for the tap).

| Command | Shown chord | Identity |
| --- | --- | --- |
| Zoom In | Ctrl++ | number row, scan `0x0D`, the key immediately left of Backspace (US `=` / `+`); also Ctrl+Shift with that key and numpad `+` |
| Zoom Out | Ctrl+- | number row, scan `0x0C`, the key immediately right of `0` (US `-` / `_`); also Ctrl+Shift with that key and numpad `-` |
| Next Blend Mode | Shift+= | the same number-row plus key, scan `0x0D` |
| Previous Blend Mode | Shift+- | the same number-row minus key, scan `0x0C` |
| Larger / Smaller Brush | `]` / `[` | scans `0x1B` and `0x1A` (US `]` and `[`) |
| Harder / Softer Brush | Shift+`]` / Shift+`[` | those same bracket keys |
| Move Layer Up / Down | Ctrl+`]` / Ctrl+`[` | those same bracket keys |
| Zoom Fit / Actual Pixels | Ctrl+0 / Ctrl+1 | the number-row digit keys |
| Arrows, Delete, F2, Space, Backspace, Enter, Escape | as labeled | those named keys |

Choosing a tool (`kCommandSelectToolBase` plus the `EditorTool` index),
nudging the selection, the layer, or the selected pixels, a smaller or larger brush, a softer or harder brush,
the previous or next blend mode, and the Delete key (`kCommandDeleteKey`) are commands in that same catalog.
Shift added to a nudge command's chord steps by ten. Holding Space to pan, typing into an open edit, and Escape or Enter
that confirms or cancels an edit already under way are gestures of that edit, not separate commands.

Help › Command List lists every command of the catalog, menu rows and local commands alike, by its plain name
(`PlainCommandName`: the caption without its mnemonic, accelerator column or ellipsis). It opens fresh (no query, no
selection) with the keyboard in a search field that filters the list by the command name; the shortcut column is
the effective chord as the chrome shows it. A row's id is its command id, so a selection survives filtering only
while its command is listed and never moves to another command's row. In the search field, Up and Down move the
selection and Enter runs the selected row, or the first listed row when none is selected (nothing when the list is
empty); on the list, Enter, Space (once the keyboard reached the list: after a click Space pans) or a double-click
runs its row, except that with no row selected the first Enter or Space only selects the first row (DxUi's tree
would select it and run it at once, so a stray Enter replaced the document with New). Running a row closes the list
first. Enter runs a
row only while the list's search field or list has the keyboard: in a card underneath (the Rename card's field) it
belongs to that card. Escape or OK closes the list; when the list held the keyboard it goes back where it was (the
Rename card's field, the color picker) or else to the canvas (Modal cards).
StatusStrip shows zoom percent, document size, and the current tool with its effective chord
(`ChromeHost::CommandChordText`). The tab title is the file name (or Untitled) with a dirty mark when history is
modified.

The StatusStrip keeps those three text sections: zoom and document size have fixed widths, and the current tool
section stretches. A fixed-width fourth section reserves room for a sibling `DxUi::ProgressBar`, overlaid inside the
section with the bar's accessible name `IDS_A11Y_JOB_PROGRESS`. The bar is hidden by default. `ChromeHost::SetJobProgress`
shows or hides it and accepts an optional integer percent: a value is clamped to 0–100, while `nullopt` selects the
ProgressBar's indeterminate animation. Control writes and placement happen only when their values or bounds change.
Hidden progress updates do not invalidate chrome. During a held chrome press, a visibility transition waits until
release (or pointer leave), preserving the click across frames with the pinned library's interaction revision rules.
Only the newest requested visibility/progress survives that brief deferral.
The existing `EmbeddedHost` animation path advances the indeterminate bar only while the visible control requests
animation; ChromeHost adds no timer. `Application` controls delayed visibility and waits 250 ms before showing the
indicator for a busy operation, then hides it when the operation ends. This chrome contract does not assert that image
operations are asynchronous or that any particular operation reports progress.

`IDS_STATUS_BUSY` formats the busy label as `{0}…`. `IDS_STATUS_QUEUED_STALE` is the message for a queued action
cancelled because its document changed before execution; the owning application flow decides when to present it.

## Notices

The shell exposes the documented command surface through menus and the tool rail. No exposed function awaits an implementation since C16 closed
([`Completeness_2026-09-21.md`](../Plans/Done/Completeness_2026-09-21.md)); a function surfaced ahead of its
implementation would show a notice card here (a stub still changes nothing: no document, history, selection, tool
or viewport change, no side effect before or after the notice) and join a table here until it lands.

A color or filter command that cannot run on the active layer shows a card over the canvas
(`ChromeHost::ShowNotice`, not a message box) with `IDS_CMD_UNAVAILABLE` ("{0} cannot be used on this layer.") and
does not open an empty panel. An edit the document's 100 M pixel budget refuses (Decision D7 of the 2026-09-23
review, [`Document_Model.md`](../Document/Document_Model.md): a paste, Layer via Copy, a duplicate, a mask, the
floating selection's merge, a filter whose grid grows, a merge, a distortion, a shape) shows the card with
`IDS_ERROR_TOO_LARGE` once: `SyncChrome`, which every document change runs, reads
`DocumentSession::TakeBudgetRefusal`, so whichever path ran the edit reports it; a refused filter or merge names the
budget instead of `IDS_CMD_UNAVAILABLE` or `IDS_ERROR_FLATTEN`. The budget notice posts itself
(`Application::kNoticeMessage`) so it never lands inside a DxUi dispatch (a header's Apply, a menu command, a layer
row) or a modal loop, and shows from the message loop once the command or click that raised it returns; the
unavailable notice shows directly, since nothing calls it from inside a dispatch. OK or Escape dismisses either
card.

Every prompt and notice (these two, `Application::ShowError`'s message box, the save and image-open prompts, and
Help > About) goes through one small interface, `Prompts` (`RedPrism/Prompts.h`): `RealPrompts()` shows the real UI;
quiet runs (`--self-test`, `--screenshot`, `--bench`) install `QuietPrompts()`, which never shows anything and
answers asked questions with a forced or default value instead. Application counts what each call meant for the
self-test itself (`_errorsShown`, `_budgetNotices`, and so on), unconditionally, whichever `Prompts` is installed.

## Layers panel

`Application::SyncChrome` synchronizes retained values and layout geometry without painting the chrome surface.
`Renderer::Render` performs `ChromeHost::Prepare` at the frame boundary before canvas composition and chrome
composition. Worker-result publication and command dispatch therefore do not run that preparation; accessibility
publication still follows a coherent prepared view. Explicit harness preparation remains permitted.
After all retained pushes, SyncChrome finishes arranging bounds and acknowledges interaction through the pinned
DxUi PrepareInteraction boundary at the last painted extent/DPI. Rapid gestures then use current live bounds while
pixels stay dirty for the next frame. This does not publish UIA/text geometry or allocate a surface. A new or failed
view, resize, hide/zero extent and device replacement still require full frame preparation before interaction.

The layers Tree lists rows top-first (`DocumentSession::LayerRows`): names, `(hidden)` annotation for rows whose
effective visibility is off, a folder glyph for groups (Segoe Fluent Icons when that font is installed), and depth
indentation. Selecting a row selects the layer; toggling a group's expander updates
`DocumentSession::CollapsedGroupIDs` (rows carry `HierarchyEntry::collapsed`). Dragging a row previews an insertion
line and, on release, calls `DocumentSession::PlaceLayer`: the top or bottom of a row inserts beside it, the middle
of a folder drops inside it, and Escape or capture loss cancels. A folder cannot be dropped into its own descendant.

The experimental prepared-row path, currently enabled only explicitly by self-tests, shares one immutable
`PreparedLayerRows` graph between the Application publisher, Chrome's Tree model and UIA snapshots. On replacement
or detach, Chrome drops its model reference before the Application drops its cache reference; older snapshots may
continue to retain the old epoch. Its original admission remains charged while any reader owns the graph, and the
complete graph is retired and acknowledged only after its final owner releases it. During same-document refresh,
previous labels may remain visible but the stale tree is disabled:
selection, reorder and mask-badge hits cannot mutate through its obsolete hierarchy. Document replacement clears
old labels immediately. A candidate publishes only against its exact current document/generation/collapse/read
authority/root. Prepared top-first depth, inherited visibility, collapsed descendants, localized hidden labels
and mask flags preserve `HierarchyEntries` semantics. Active selection uses the owning Guid index; a collapsed
descendant has no selected row. An unchanged borrowed graph/active target causes no row rewrite.
Process-startup localization supplies the hidden pattern once; per-row painting and settled sync do not load it.
If preparation fails or replacement admission refuses, a STRINGTABLE-named Retry layers button replaces the
opacity control in the layer header. One explicit press requests one new original ticket; further input while
that ticket is pending is ignored. Memory events or unchanged-key sync alone do not retry. Success restores the
opacity control. Retry changes derived display state and does not settle or mutate an open document edit.
Full interaction, accessibility/reentrancy, largest-count/name owner cost, production activation and G1 remain
separate qualification requirements; prepared publication alone does not close them.

In the experimental prepared mode, the same immutable `PreparedLayerRows` owner backs the Application publisher,
Chrome's Tree model and DxUi's UIA snapshots. Its semantic source contains every visible model row, including
offscreen rows, with the full localized display name; UIA capture retains that owner instead of copying rows or
reading the live Tree model. A provider may therefore keep an older row epoch alive across replacement or shutdown.
The production flag remains false, and this consumer behavior remains draft pending the RedPrism matrix,
client-reentrant UIA and latency, largest-count/name owner-cost, activation and G1 qualification.

In the experimental prepared path, a Tree item identity belongs to a layer Guid within one document serial;
visible position is not its identity. The Application reserves a monotonically advancing, checked 64-bit range
only after original job admission succeeds. Values begin at 2^32, remain disjoint from legacy ordinal IDs, and
are never reused after failure, cancellation or document replacement. Worker preparation preserves IDs for
surviving Guids in the same serial, including hidden collapsed descendants. Exact admitted arrays provide
visible-index-to-ID access and logarithmic ID/Guid lookup; a newly published epoch stores copied identities
rather than retaining a chain of previous epochs. Tree selection, expansion, drag/drop and UIA all resolve the
same map. Rename and reorder preserve a surviving layer's runtime identity. A hidden or deleted layer's old
provider reports UIA_E_ELEMENTNOTAVAILABLE and cannot select a different layer. Reopening a collapsed group
restores its surviving child's identity. Replacing the document, even with identical Guids, gives fresh IDs and
invalidates old providers. Range overflow refuses before document capture, keeps the prior admitted epoch, and
uses the existing explicit Retry layers action. Independent actual-provider tests and the complete matrix,
owner cost, production activation and G1 remain required; the path stays disabled in production.

Rename (C16): Layer › Rename Layer…, F2 or a double-click on a row (`ITreeDelegate::OnTreeItemInvoked`, which
selects the row first) opens the Rename Layer card (`ChromeHost::OpenRenameCard`, a `CardPanel` with a `TextField`
holding the name selected, Cancel and the primary OK) over the canvas hole beside the layers panel; Enter or OK
commits the name through `DocumentSession::RenameLayer` (trimmed, a blank name ignored) as one `Rename Layer`
entry, Escape or Cancel leaves it; the keyboard goes back where it was (the layers panel after a double-click, the
canvas after F2). The card renames the layer it was opened for, whichever row is selected when it
commits; it closes without renaming when that layer goes away (Undo, a delete, a merge), when the document is
replaced, and when an interaction settles (a tool switch). Merge (C16, legacy `LayerMerge.swift`): Ctrl+E follows
`DocumentSession::PlanMerge` — the active layer merges with the layer beneath it in its folder (`Merge Down`), a
folder merges its contents and goes (`Merge Group`), several selected layers merge together (`Merge Layers`); the
Layer menu's caption names the plan. A pending transform applies and a pending gradient commits first, each as its
own entry, so the merge holds what the canvas showed. The host composites the planned layers alone — cut loose from
any parent or clipping source outside the plan — through the canvas composition (`Compose` on that subset,
`Readback`, the document invalidated before and after) and `DocumentSession::MergeLayers` installs the result
trimmed to its pixels in the anchor's place, named after the plan, with layers clipped to any merged layer clipping
to it, as one entry; a failed composite reports `IDS_ERROR_FLATTEN`, a result past the document's pixel budget
`IDS_ERROR_TOO_LARGE`. The self-test renames through typed characters and Enter, leaves a name alone on Escape,
merges a blue layer down onto a red one into one 14 × 14 layer, and merges a layer under a pending gradient into
one that holds the gradient.

Mask target (RF-907, legacy `NativeLayerList.swift` and `LayerMask.swift`): a row whose layer has a mask shows a
`Mask` badge (`IDS_LAYER_MASK_BADGE`; `Mask off` in the error tone for a disabled mask, `IDS_LAYER_MASK_OFF_BADGE`),
drawn in the accent while that mask is the target (`HierarchyEntry::hasMask`, `maskEnabled`). The badge is the legacy
mask thumbnail: a press on it targets that layer's mask (`DocumentSession::SelectLayerTarget(id, true)`), Shift also
turns the mask off or on (`ToggleLayerMask`, one `Disable Layer Mask` / `Enable Layer Mask` entry), and the press goes
no further (no row drag, no rename on a double-click). A press elsewhere on the row whose mask is the target targets
that layer's pixels and then selects the row as usual (legacy `clickedLayer`); selecting another row targets its
pixels, as it always did. The Layer menu's Target Layer Mask / Target Layer Pixels, Unlink / Link Layer Mask
(`ToggleMaskLink`, one `Unlink Layer Mask` / `Link Layer Mask` entry) and Disable / Enable Layer Mask act on the
active layer's mask; their captions follow it and they gray out without one. They have no default chords (the legacy
menu had none); the Command List and the settings' chords reach them (`cmd/layer/targetMask`,
`cmd/layer/toggleMaskLink`, `cmd/layer/toggleMask`). A target switch records a typed opacity first and applies a
pending transform (legacy `clickedLayer` commits it) and a pending gradient (Decision D3); a switch, the link and the
mask's on/off do nothing while a stroke or warp is in progress (legacy `canEditLayers`). The pinned DxUi `Tree` draws
text rows with one badge — no thumbnails or per-row buttons, and its embedded pointer events carry no button, so no
row context menu — so the badge is the one mask zone and the Layer menu stands in for the legacy link button and
context menu; the host tests the press against the badge rectangle (`Tree::GetItemLayoutMetrics`) before the tree sees
it (`ChromeHost::LayerTargetAt`), skipping a floating card over the panel. The self-test grays the commands without a
mask, targets the pixels after selecting a masked layer, goes to the mask and back through the menu (captions and
badge tone), unlinks and disables it, turns it back on with a Shift-press on the badge and targets it with a plain
press after a press on the row's name went back to the pixels.

The 32 DIP title row holds the `Layers` label and a `DxUi::NumericStepper` (0–100 %, step 1, Shift 10, unit `%`,
accessible name `Layer opacity`) bound to the active raster layer's opacity. Typing previews live on the layer the
edit started on; Enter, focus loss, the buttons, Up/Down and `RequestValue` commit; Escape restores the start value.
One typed edit is one `Layer Opacity` history entry from the value before the first preview; a cancelled edit leaves
no entry, and so does one typed back to its start value (the stepper reports nothing then). No history transaction
stays open while the field edits: the preview changes the document outside history, and anything else first records
the typed value as its own entry — a command (menu, key, Command List), a tool switch, a press on the canvas,
another layer selected or a row dragged, a save, or the document replaced — so no other edit folds into it or bakes
the preview in; the field's later commit of that same value adds nothing, to another layer or another document
alike. Replacement retains that acknowledged value until the old field's late commit is consumed; clearing its
pending edit MUST NOT erase this suppression marker. The stepper is disabled for folders or without an active layer and is never rewritten while the user is
typing in it. While a panel owns the document (an adjustment layer's or an Image menu panel, `HasColorEdit`), the
layer opacity waits for its OK or Cancel, as legacy `canEditAppearance`: the stepper is disabled, the Move tool's
digits change nothing and `DocumentSession::SetOpacity` refuses, so no opacity folds into the panel's entry and its
Cancel leaves the opacity as it was.

## Palette and color picker

The palette (opaque ARGB foreground and background) is session state outside history
(`DocumentSession::ForegroundColor` / `BackgroundColor`, `SwapPaletteColors`, `ResetPaletteColors`), as the legacy
brush settings were. A brush stroke paints that foreground color; swapping or resetting changes the next stroke. The rail hot zones and the default keys run `kCommandSwapPalette` and `kCommandResetPalette`.
Clicking a well opens the `DxUi::ColorPicker` on a `CardPanel` just above that well, 8 DIP
right of the rail, so the card sits beside the palette instead of under the tool header. If the card would leave
the window it stays below the tabs and above the status strip. Chrome composites over the canvas with premultiplied
alpha, so the card is visible there. The picker edits the clicked well: field and hue-strip drags, typed R/G/B, six-digit hex and
host-sampled colors (`ChromeHost::SampleColor`, the eyedropper feed) preview into the well; OK or Enter commit it
(`ChromeEvents::OnPickerColor`, which reports every change of the one picker with its `PickerTarget`: a well, a
Gradient Map end, the Canvas Size custom color); Cancel, Escape (also when a picker child has focus) or capture loss
restore the well.
Canvas-fed samples update DxUi's chosen color through `SampleColor`; `PickerColor` reads `GetColor`. Its
`GetCurrentColor`/`SetCurrentColor` swatch is the original opening color reserved for Cancel, not the sampled
choice. Sampling MUST NOT replace that original. OK commits the chosen sample; Cancel restores the opening value.
Deferred sampled-picker publication can use `CapturePickerSampleAuthority` and `ApplySampledPickerColor`.
`CapturePickerIntentAuthority` and `IsPickerIntentCurrent` separately track explicit input. A successful checked
sample renews full publication authority through its own Preview callback while retaining explicit intent.
Public sampling, raw pointer/key/character input and Open/Close/Detach advance explicit intent, including equal
values and A–B–A. Opening lifetime changes remain explicit even during a callback. Foreground/background Preview
updates chosen chrome color only; the session palette changes on Commit.
Authority identifies the exact chrome owner, target and input revision. Opening, closing, detaching and every
manual/DxUi or direct sampled write advance the revision, including equal colors and A–B–A input. Pointer
interaction in the picker and keyboard/text input in its focused subtree also invalidate
older samples even when an equal or unfinished typed value emits no change callback. Canvas pointer sampling and
view input outside the card do not advance this manual-input authority. Checked success renews the caller's
authority; stale/foreign/closed/reopened results change no color. Counter exhaustion permanently
refuses checked publication. These fixed scalar guards add no control, worker, timer or allocation per input and
do not activate the asynchronous canvas sampler by themselves.
Pointer input inside the card belongs to chrome even though it lies in the hole; a canvas click outside it focuses
the canvas and leaves the picker open. Captions come from the STRINGTABLE (`IDS_PICKER_*`, `IDS_PALETTE_*`).
With a layer's mask targeted, the swap and reset (the keys and the rail's hot zones alike) act on the mask palette
instead (legacy `ColorPalette.swift`): swap flips the brush header's Paint between Black - Hide and White -
Reveal, reset returns it to Black, and the RGB colors stay; a pending mask gradient re-renders with it. The rail's
wells show that palette meanwhile (RF-1113, legacy `paletteColor`): the foreground black or white as Paint says, the
background the other. No picker opens on the palette then (legacy `openColorPicker`): a well, the brush header's
color, the Gradient header's end swatches and the Shape header's fill do nothing, and a picker open on a well closes
unchanged once a mask becomes the target (`ChromeHost::SetPalette` with the mask palette). The legacy wells' Black ·
Hide / White · Reveal popover is not ported: the brush header's Paint, X and D make the same choice. The self-test
sets red and green colors, targets a mask and requires black and white wells (swapped by X, reset by D), no picker
from a well click through the pointer path, the colors back on the pixels, and a background picker closed without a
change when the mask is targeted again.

The C3 fallback (app-owned split rectangles, a ColorSwatch-launched dialog from existing controls, TextField
numeric nudge) is retired: RedPrism consumes the library controls and does not implement a second toolkit.

## Theme

`RedPrism/ThemePalette.h` builds the chrome palette: dark (the C1 clear color behind the canvas) or DxUi's light
default, chosen by the `theme` setting (`system` follows Windows' app mode, [`Core_Settings.md`](../Core/Core_Settings.md)).
Windows high contrast overrides both: `MakeHighContrastThemePalette` maps every role to the active contrast
theme's system colors (`highContrast` set, accent = highlight, borders and text = window text) and the renderer's
clear color becomes the system window color. `Application::RefreshTheme` reapplies on `WM_SETTINGCHANGE`,
`WM_THEMECHANGED` and `WM_SYSCOLORCHANGE` and on a live settings change; native menus take the same palette.

## Accessibility

The main window answers `WM_GETOBJECT` with `WindowAccessibilityRoot` (`RedPrism/AccessibilityRoot.*`): the HWND's
UIA provider (`UiaHostProviderFromHwnd`) whose one child is the DxUi chrome's fragment root, attached through
`EmbeddedHost::AttachAccessibility` after the first `Prepare` (DxUi attaches lazily and drops the attachment on hide or
a device swap; the next `Prepare` re-arms it) with a `ChromeAccessibilitySite` (the window root as parent and fragment root,
focus requests to the HWND, a frame after a UIA action). The placement (client rectangle in screen pixels, whether
the chrome rather than the canvas holds the keyboard focus) follows `WM_SIZE`, `WM_MOVE`, focus changes and the
canvas-focus toggle. DxUi republishes its tree only when told after a preparation
(`EmbeddedHost::UpdateAccessibility`, a walk of every control), so `ChromeHost::Prepare` tells it after a preparation
that changed the chrome while a client listens (`UiaClientsAreListening`), when the accessible structure changed (a
tool switch, a card or panel, a header control shown or hidden, the layer rows, the document title: a panel, a card or
a header joins the tree with the frame that lays it out) or when a client asked while it could not publish; anything
else waits until a client asks: `WM_GETOBJECT` and the in-process root publish the waiting snapshot before answering
([`Core_PerformanceAndResources.md`](../Core/Core_PerformanceAndResources.md), "Pointer input and the chrome"). A frame
that prepared nothing publishes nothing. Every interactive control carries an accessible name:
buttons and checkboxes from their captions, steppers and sliders from their labels, combo boxes from their tooltips,
the wells, swatches, tree, tabs, rail, splitter, picker and the drawn adjustment controls from `IDS_A11Y_*` strings.
`ChromeHost::UnnamedInteractiveControls` walks the tree and the WARP self-test requires it empty, walks the UIA
tree from the window root (at least eight named elements), resolves a point under the menu bar, and finds the
palette's four zones and, with their panels open, the curve editor, both levels bars and the hue spectrum as named
Buttons in that tree. Detaching the chrome retires the provider (`UiaReturnRawElementProvider(hwnd, 0, 0, nullptr)`,
`UiaDisconnectProvider`).

Known DxUi limitation (RF-1114): the menu bar and the tab strip carry an accessible name and RedPrism delegates the
whole tree to DxUi correctly, but neither publishes a UIA fragment. The pinned library's
`GetControlTypeId` (`src/Controls/DxUi.Accessibility.cpp`) is an exhaustive `dynamic_cast` chain over `Checkbox`,
`Toggle`/`Button`, `TextField`, `ComboBox`, `Slider`, `ProgressBar`, `ThroughputGraph`, `Tree`, `Grid` and
`ColorSwatch` with no case for `MenuBar` or `TabControl`; unlike `Panel` and `PageHost`, `MenuBar` (`include/DxUi/DxUi.h`)
does not override `GetLogicalChildCount` / `GetLogicalChild`, so its items have no accessibility walk to join even if
the bar itself did. A screen reader reaches both only by client coordinates, not by name or role. Not fixable from
RedPrism; a fix belongs in DxUi.

## Size cards

New Canvas…, Canvas Size… and Image Size… open a card (`ChromeHost::BuildSizeCard`, a `CardPanel` floating over the
canvas hole at the layers side like the adjustment panel; a point on it is chrome). The `Application` owns the draft
(`SizeDialog`: a `CanvasSizeDraft`, an `ImageSizeDraft`, the anchor, the extension choice and its custom color, the
New Canvas size) and mirrors it through `SetSizeCard`; every control reports one `SizeAction` with one value, a typed
number counting when committed (Enter, a step button, the focus leaving) so a locked ratio never follows a
half-typed value. Enter is the card's OK and Escape its Cancel (the keys a chrome control leaves, Modal cards; the
accelerators with the canvas focused); a tool switch or a document on its way out cancels the card. Apply first
settles the interactions in flight as a document on its way out does (a pending gradient or transform commits as its
own entry), then resizes that document through `ResizeCanvas` / `ResizeImage`
([`Document_Tools.md`](../Document/Document_Tools.md)) as one `Canvas Size` / `Image Size` entry and fits the viewport; `TooLarge` reports `IDS_ERROR_TOO_LARGE`, any other failure
`IDS_ERROR_RESIZE`; an unchanged size closes the card.

Image Size/Canvas Size Apply closes the submitted card and starts cancellable worker preparation. Fit and
the single named history entry occur only when its result is successfully applied. Escape during a running size
job requests cancellation; the busy/history state remains until acknowledged drop. Reopening a card invalidates
the earlier result and its completion leaves the new card open. Size failures are posted localized notices from
outside the completion callback. The job's dimensions, resolution, sampling, anchor and color come from its copied
request; subsequent draft/palette changes cannot alter that work. New Canvas opens immediately with its current
draft and requests a metadata-only worker clipboard suggestion. It decodes no pixels for this hint. Publish only
for the same card invocation, session and document lifetime while the user has not edited Width or Height.
Typing either dimension, closing/reopening the card or admitting its copied Create draft invalidates the older
suggestion. Optional failure leaves the draft unchanged; it creates no notice or history barrier. Busy inspection
uses at most six acquisition attempts with acknowledged one-shot waits under the original job reservation.

| Card | Rows |
| --- | --- |
| New Canvas | The hint, Width and Height in pixels (1…30 000, else the invalid note replaces the sRGB note and Create Canvas is disabled), Open Project… and Import Image… (they close the card and run the command), Cancel, Create Canvas: after the save prompt a fresh unmodified untitled document with one blank layer. The size starts at 1920×1080, then the last typed size; an image on the clipboard suggests its size when the card opens |
| Canvas Size | Current W × H and its uncompressed RGBA size, Units (Pixels, Percent, Inches, Centimeters), Width and Height in the unit (three decimals, two for percent), Relative to current dimensions, Lock original aspect ratio (locking settles the height on the width), the New size line or the invalid note, the 3×3 anchor grid (pressed = chosen, named for accessibility, the name and hint beside it), Canvas extension (Transparent, Foreground, Background, Black, White, Custom: the color picker opens on the custom color and the swatch shows it; Cancel restores the previous custom color), Cancel, OK |
| Image Size | Current W × H, Units (only Inches and Centimeters without resampling), Width and Height, Lock aspect ratio (disabled without resampling), Resolution in pixels/inch (1…9 600), Resample, Sampling (Nearest, Smooth, High quality) while resampling, the note (pixels resize, or only the print size changes), the Result line or the invalid note, Cancel, Resize |

## Files

An owned Open/drop/startup batch is classified once on an Io worker before project/image routing. Directories,
case-insensitive `.prism` names and the existing ZIP signatures retain their routing rules; unreadable/short
headers retain best-effort image routing. Explicit Images As Project/Layer requests retain their explicit action.
The owner consumes a fixed cached classification instead of inspecting files in continuation actions. Empty
host entries are skipped. The selected request remains pending and prevents younger mutations until worker
acknowledgement, including Escape, late cancellation and document replacement. Classification completion
posts an existing localized notice on failure; it never opens a modal from the completion drain. Project save now
runs as an owned Io worker after file selection and document settlement; its receipt, ordinal and memory
reservation remain held through publication and cancellation acknowledgement. PNG/JPEG export uses asynchronous
utility readback and detached worker encoding under the same owned continuation. Project open and image import
inspect/decode on workers. Copy/Cut/Paste and Copy Merged use admitted worker preparation and owned OS
clipboard transfers under the selected ordinal; retries preserve the original ticket and reservation with
six total acquisition attempts. New Canvas opens immediately and requests guarded metadata-only prefill.
Private Copy/Cut application survives later OS-mirror failure; a successful Empty starts one irreversible
publication attempt. Complete session-end/contention and measured P1/G1 qualification remain separate.

New creates a 1280×800 blank layer and marks history saved; New Canvas… opens its size card (below).
New, accepted New Canvas Create and Close Project retain their selected request and history ordinal through
cancellable worker preparation and owner apply/drop acknowledgement. New Canvas owns the accepted dimensions and
card invocation. Nonfinite, overflow or rounded dimensions outside 1…30 000 fail before settling destructive
prerequisites; the card, old document/drafts, path and history stay intact. A failure or cancellation after
preparation likewise leaves the old document and path. Successful publication alone clears the path, applies Fit
and closes the submitted New Canvas card. Private clipboard, palette and settings survive replacement. Close
without a document is a no-op that preserves an existing redo lineage. A younger input captured for the replaced
lifetime becomes stale; fresh Undo after New returns to no document and Redo returns its saved blank canvas.
Open and Save use `IFileDialog`. Import Images… opens a multi-select image dialog and adds every chosen image as a
layer above the active one, in one `Import Images` history entry (`Import Image` for a single file); with no
document open the first image becomes the document (see below) and the rest join it.

Image import inspection and preparation retain the selected input's receipt, history ordinal and reservation
until owner apply/drop acknowledgement. View commands remain available; younger document mutations wait.
Successful worker preparation validates the complete new saved baseline and optional additional import document
before either applies. Existing-canvas successes form one import entry; ordinary failed files retain successful
files and produce one posted localized summary. Cancellation, stale lineage/generation or preparation allocation
failure publishes neither document. A batch containing only projects completes before image-import dispatch.
Only the validated replacement rebinds its selected batch; younger input captured on the previous document is
stale. Existing-canvas younger Undo runs after the import and undoes its one entry. A failed mixed project/image
batch retains its first exact error even if image import also fails. Notices run outside completion callbacks.

An image (PNG, JPEG, TIFF, HEIC) chosen through File › Open or dropped on the window is not imported silently: the
shell asks, in a task dialog owned by the main window (`IDS_OPEN_IMAGE_TITLE`, `IDS_OPEN_IMAGE_TEXT`), whether to
**Add as Layer** (the default: the image becomes a layer above the active layer as one `Import Image` entry and the
canvas keeps its size) or to open it as a **New Project** (the current document is replaced by a document of the
image's size holding the image as its only layer, the history reset and marked saved, no path, the viewport
fitted); Cancel changes nothing. A drop of several files asks once for the batch (`IDS_OPEN_IMAGES_TITLE`): New
Project takes the first image that loads (legacy `importImages`: one that fails before it is reported with the batch
and the next is tried) and the others join it as layers; projects in the batch open as projects first. With
no document open an image opens as a new project without asking. Quiet runs take a forced answer (Add as Layer
unless the self-test sets another). Images added as layers join the document as it stands: the interactions in flight
settle first, as a tool switch settles them (an open panel applies, so the import never folds into its entry).
Import failures report `IDS_ERROR_IMPORT_IMAGE` or `IDS_ERROR_TOO_LARGE` once for the batch and keep the images that
loaded.

Before a document is replaced or dropped — New, New Canvas…, Open (a project or a new-project image), Close
Project, Exit and the window's close button — a modified document asks `IDS_CONFIRM_SAVE_TITLE` ("Save changes to
<name>?") with Save, Don't Save and Cancel: Save runs the Save command and proceeds only when it saved, Don't Save
proceeds, Cancel keeps everything as it was. Nothing settles before the answer, so Cancel leaves open panels, cards,
the crop frame, drafts, a pending transform and a live stroke exactly as they were; the document's interactions
settle only once Save or Don't Save lets it go. A pending edit that Save would record — a live stroke or warp, a
pending gradient, a changed transform, a typed opacity or an open color panel — counts as a change, so a clean document with one still
asks. Quiet runs never ask (they proceed, or take the self-test's forced answer). Close Project (Ctrl+W) leaves the
shell without a document: the status strip shows `—`, the layers panel is empty, the canvas hole shows the clear
color and the document commands do nothing; New, Open, Import Images… and a drop start the next document. A document
that replaces another (a project opened, a new project from images) takes the session over through
`DocumentSession::ReplaceDocument` ([`Document_Model.md`](../Document/Document_Model.md)): its active layer's image is
the target, never a mask targeted in the document it replaced (not even when the reopened active layer is that same
layer with a mask of its own), and no selected set, draft or history carries over.

Save, Save As and Export write what the canvas shows (review-fix Decision D2): before writing they settle the
document as Enter would — a typed opacity records its entry, a live stroke or warp and a pending gradient commit, a
pending transform applies, and an open color panel applies as its OK does. An adjustment layer's panel previews
inside the document, so a file written under it would hold a value the panel's Cancel later takes back while the
document shows clean; applying first leaves nothing to take back. If applying the color panel fails or finds no
Content-Aware Fill donors, Save, Save As and Export MUST stop before writing. Save-before-proceed returns failure,
keeps the document and panel, and cannot continue a close or replacement. These actions are owned, staged
continuations: accepting a request is Pending, not successful execution. Output selection and admission happen
before settlement and export readback. Cancel before output selection or discard approval preserves every draft.
The panel remains available for retry
or Cancel, with the filter failure or no-source feedback. A crop frame, a shape draft, a lifted-pixel drag
and the size card are not document edits and stay. (The legacy app refuses file operations while a stroke, a panel
or an adjustment edit is open; RedPrism applies instead.) Save writes a ZIP `.prism` for a `.prism` name or a ZIP
file; a project opened from a folder — an exploded project, whatever its name, `*.prism` included — saves in place as
a folder, and Save As with the folder filter writes a folder.

Ordered file batches, complete size drafts and layer/tool inputs retain their event-time arguments until execution.
Fill, Clear, selected Delete and Invert also use this ordered input path. The selected ordinal owns their
complete worker reservation through token-checked publication or acknowledged cancellation; younger document
actions wait while view input remains available. Escape cancels the raster-edit barrier without discarding its
request early. Failed preparation/admission reports one localized pixel-edit or size notice. Gradient pointer,
header and palette input now updates metadata and offers one active/latest pending worker preview. Apply/Enter
owns the opening; color then gradient settlement precedes dependent actions or writes. Failed/cancelled/stale
gradient settlement stops that action. Undo/Redo drop an uncommitted line, while X/D only refresh it. A frozen
commit refuses palette mutation with the existing Busy notice. View input and cancellation remain available;
preview cancellation retains ownership until acknowledgement. Distortion settlement remains tracked P1.5 work.
They share the eight-entry pending ring plus one active request and 1 MiB combined host-input budget. A refused
input reports the busy/budget notice; earlier entries keep their order. View and chrome input remain available
while color commits, project opens or project saves run. The write stage first settles typed opacity and live canvas edits under
the selected ordinal; a settled write changes chrome without forcing a new document composition. A project save snapshots the settled document, active layer, output path
and exact history revision before submission; edits and view input cannot change the saved snapshot. Only checked
publication updates the document path and marks that captured revision saved. Cancellation before publication
preserves the previous destination. If cancellation arrives after publication, the file and captured path/revision
are recorded while a dependent close or replacement is suppressed. A failure leaves the prior path/revision state
and destination intact and reports a localized notice. The selected input ordinal blocks younger mutations until
the worker acknowledges and the owner applies or drops its result. PNG/JPEG export captures its destination,
quality and resolution, reads the committed whole document asynchronously and releases the last mapped tile
and utility GPU targets before worker encoding. Later device changes cannot repeat encoding/publication.
Export never changes the project path or saved revision. Cancellation before publication preserves the destination;
after publication the receipt can be cancelled while the written file remains. Failure posts one localized notice.
Project opening probes metadata and decodes on workers under the same selected input. A failed file leaves the
current document intact and the batch continues; the final receipt retains its first failure. Cancellation keeps
ownership through acknowledgement and suppresses later files. A current successful replacement alone may bind
the remaining batch to its new session lineage. An unrelated replacement drops the result, including when IDs
are reused. Load failures post one localized notice per failed file; unsupported versions, including zero, are
formatted from the posted payload when the notice is delivered. Clipboard, wand/clone and other
readback callers remain later P1.7/P1 work.

When Windows ends the session (sign-out, restart, shutdown, an installer's restart request), a document with unsaved
work still refuses a noncritical end with `IDS_SHUTDOWN_UNSAVED`, which Windows shows while the queued Save/Don't
Save/Cancel prompt runs. Save or Don't Save closes the window and permits Windows to continue; Cancel keeps it.
A critical end is never refused. A queued or writing project save, or an export already
writing, also refuses a noncritical query with the localized saving reason; an earlier unsaved prompt remains
pending. Confirmed end waits at most 1500 ms total for queued/writing Save or entered export writers, without pumping UI or applying
their results. Export still in readback is cancelled if that wins before writer entry. A cancelled session waits for
no worker. Settings are saved on confirmed end as on close. The message and worker policy lives in
[`UI_Windowing.md`](UI_Windowing.md).

Dropped files (long paths included) open a `.prism` or exploded folder, or import an image as above. Images added as
layers center on the drop point (`DragQueryPoint` mapped through the viewport) when the drop lands on the canvas —
the hole, not a floating card — and on the canvas's middle otherwise (legacy `ContentView.onDrop`); a drop that also
holds a project, or opens a new project, centers them, as the point belongs to the document it landed on. Load failures
report the `ProjectIoStatus` through a localized message (unsupported version names the version); `--screenshot`
runs suppress dialogs.

## Validation

Harness screenshots of the empty-document shell at **96** and **144** DPI (`--screenshot <png> --warp --dpi 96|144`)
show the populated rail. `--self-test --warp` composites chrome, proves a settled document is not re-composited, drags the splitter
through `DispatchPointer` (layers +40 DIP, hole narrower after the layout re-sync), opens the picker from the
foreground well and commits a sampled color into the session, cancels a background edit without changing it, swaps
and resets with X and D, commits 50 % through the opacity stepper as one `Layer Opacity` undo entry, then proves
0 DxUi and 0 canvas surface bytes when hidden. With a painted layer it also drags the layer with the Move
tool (one `Transform Layer` entry), previews a typed rotation through Free Transform until Apply, cancels with
Escape, nudges with Shift+Right and flips the canvas and the layer. With the selection tools it drags a Marquee
rectangle, adds one with Shift, switches the shape with M, moves the outline (and puts a drag back with Escape and
with Ctrl+Z), deselects with a click, selects a
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
The session checks drive the typed opacity (typed back to its start, a command before Enter, a new document, Escape,
a commit), the Rename card's own layer, Cancel at the save prompt over a panel, the size card, a crop frame, a shape
draft and a pending transform, a clean document with a live stroke, Save under an adjustment layer's panel, a folder
project saved in place, an image batch whose first file fails, Canvas Size over a pending gradient, a copy the system
clipboard refused (the user's clipboard is set aside, cleared and restored around every clipboard step, and each
mirror is read back), capture loss mid-stroke and mid-drag, a splitter drag cancelled, a leave without a capture, a
stroke dragged over the layers panel keeping the pointer,
Space panning across a focus loss, Alt-press then Undo then a drag, a close inside a chrome callback and the session
end. The review's integration checks add a reopened project that keeps no mask target, a drop centered on its
point, Load Selection and budget refusals that say why, Copy, Copy Merged and Merge applying a pending gradient, a
gradient pending on another layer, the mask palette's Paint, X and D (and its black and white wells, which open no
picker), and the adjustment panel's live controls (a typed decimal, the Gradient Map picker's lifetime, the
spectrum's handle strip, the drawn editors in the UI Automation tree);
[`Testing_Validation.md`](../Testing/Testing_Validation.md) lists them.
The self-test's keys all go through the window procedure (`Application::HandleAccelerator` sends the key down —
a system key with Alt —, the character `TranslateMessage` would post when the key down left it, as the message loop
translates, and the key up, with a real scan code and the modifiers in the self-test's keyboard, after a frame); one
step posts keys and runs them through the loop's own step (X swaps and posts no character, the French dead `^` runs
Brush Smaller and arms no accent, `q` still types into the Command List). Its keyboard steps (`RunKeyboardSelfTest`)
replace X with Q (Shift+Q too), let the later of two entries for one command win and show it in the palette tooltip,
prove a zoom replacement leaves no default or alias live, put Zoom Out on Ctrl with the number-row plus key's virtual
key (Zoom In no longer shows that chord, Zoom Out does, the key zooms out), move the Delete key's command to Q (Delete
does nothing, Q clears, Delete Layer loses and regains the layout's Delete label), keep a replaced Marquee switch to
once per press, nudge a layer by one pixel with a replacement on Shift+J and by ten with Shift+Left, type into the
Command List's search field without running Q, Ctrl+A, Ctrl+Backspace, Ctrl+Z or Delete, run no Ctrl chord from
Ctrl+Alt, run a Ctrl+Alt replacement on a letter AltGr types a character for (E on French and German layouts; none on
US) from the left Ctrl and Alt but not from AltGr, open the Canvas Size and Image Size cards with Ctrl+Alt+C and
Ctrl+Alt+I from the left keys (from AltGr only where the layout types nothing for the key), run the exports with
Ctrl+Shift+E and Ctrl+Alt+Shift+S and show those four chords in the menus and the Command List, filter the list to
Sample Ring and run it with Enter (a typed space is text, not a pan), keep a selection from moving to another command's
row and run the first match without one, run a selected row with Enter on the list, only select the first row with the
first Enter or Space on a list without a selection (the next Enter runs it), fill the row item DxUi reuses for every row
without dropping its buffers (a repaint of the open list allocates nothing per row, as the layers tree), rename through
the Rename card with Enter while a Delete Layer row is selected in the open list, give the keyboard back to the Rename
card when the list closes, give it back to the canvas when a notice or the Rename card opened with F2 closes, close the
Canvas Size card with an Escape the focused layers panel leaves (the panel keeping the keyboard), pan with Space only
while it is held, pan with Space after a click on a layer row (no Rename card opens) and on the palette's swap zone (no
second swap), press a control the keyboard reached (Tab to the Command List) with Space once per press (an auto-repeated
Space runs nothing) and keep a Space held on the Rename Layer row out of the card it opened, cancel a transform with
Escape after Ctrl+T and a click on Flip Horizontal (no entry) and apply one with Enter (one flip, the button not
pressed again), close the Canvas Size card's open units list with Enter or Escape and apply or cancel the card with the
next, keep Enter for Flip Horizontal once Tab and Shift+Tab reached it and Enter and Escape for the opacity field,
give the menu bar the keyboard from a tapped Alt, F10 and `SC_KEYMENU` without a system-menu loop (a timer ends one if
it starts), open the File menu with Alt+F but not when Alt+F is a command, take the keyboard back from the menu bar with
a canvas press (B then chooses the Brush), `WM_KILLFOCUS` and `WM_ACTIVATEAPP`, and with the next key once the bar lost
DxUi's focus, release Alt after Alt+Backspace (each key's own down and up messages, Backspace or Alt let go first)
without the menu bar taking the keyboard, set the Move tool's opacity with 5, 0, Undo, 4 then 5 as the legacy golden
does (the 5 follows the 4 with no frame between, so a slow frame cannot stretch the pair past 0.6 s), set 50 % once from
a held 5 (no 55 %, no entry per repeat), keep the position of a key message without a scan code (the layout's right
bracket key grows the brush, Ctrl with its number-row plus key zooms in), keep digits typed after a canvas press out of
the focused opacity field (the Move tool's 7 sets 70 %, the Marquee's 3 types nothing), leave the layer opacity alone
under an open adjustment panel (the digit, the stepper and a typed preview; Cancel keeps it), show the table's name for
a key the layout does not name (VK_E7), and show the layout's key names, the STRINGTABLE modifiers and replacements in
the Command List, the menus (Undo, Invert) and the rail tooltip. `HostTests` covers tool tables, glyphs, the string
table (every command caption's column is its first default chord), the key names, the catalog and the binding table.
