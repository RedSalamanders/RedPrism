# Application chrome

Status: current normative chrome contract
Last reviewed: 2026-09-23
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
across resizes and reports every change through the layout handler, and the host re-syncs the viewport on the next
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
creates all fifteen documents and requires every visible tool button to draw its icon. A tool that does not fit
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
The dash offset advances one cycle every 400 ms while a non-empty outline is showing and the window is visible,
powered, and not occluded; the path geometry is rebuilt only when the selection or the view mapping changes. A
settled document with no selection does not stay awake for the dash. The draft being drawn (freehand and polygonal
polylines, the polygonal rubber band to the cursor and an 8 DIP handle on the first corner, the Marquee's rectangle
or ellipse) is a white line over a dark one. Outline and draft changes re-composite chrome only, never the document.

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
Delete clears them, and without a selection deletes the targeted mask, or the layer; Shift+Delete opens
Content-Aware Fill on them ([`Document_Tools.md`](../Document/Document_Tools.md)); Ctrl+X, Ctrl+C, Ctrl+Shift+C
and Ctrl+V cut, copy, copy merged and paste (`RedPrism/SystemClipboard.*` mirrors a copy to the system clipboard as
PNG and a 32-bit DIB with straight alpha and reads another app's PNG or DIB, centered on the canvas, when the
system clipboard changed since that copy; a copy that could not reach the system clipboard, because another app held
it open, is still the newest, so Paste keeps taking it until the system clipboard changes again); Alt+Backspace and
Ctrl+Backspace fill with the foreground and background
colors; Ctrl+I inverts; Ctrl+J is Layer via Copy. The selection contract itself lives in
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
the frame by a drag inside it, drops it with Escape, squares it through the header's ratio, cancels, undoes and
leaves the tool.

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
switching tools each commit one history entry (`Brush Stroke`, `Erase`, `Paint Mask`); Escape drops the stroke. The Eraser and the Brush's Erase mode both
erase; a mask is never erased but painted black or white. A stroke over the pixel budget or an engine failure is
dropped with `IDS_ERROR_BRUSH_BUDGET`. A press does nothing on a folder without its mask, a hidden or adjustment
layer, a disabled mask or an empty selection.

Keys with the canvas focused: B chooses the Brush and E the Eraser; `[` and `]` step the size by a fifth (at least
one pixel); Shift-`[` and Shift-`]` step the hardness by quarters; 1–9 set the opacity to 10–90 % and 0 to 100 %,
two digits within 0.6 s make a two-digit percent (with the Move tool a digit sets the active layer's opacity the
same way, one `Layer Opacity` entry per key that changes it: legacy `LayerAppearanceTests.
moveToolNumberKeysSetSelectedLayersOpacityAsOneUndo`); Escape cancels a stroke in progress. The stroke contract lives in
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

## Adjustment panel

Every color adjustment and filter edits in one card (`ChromeHost::BuildAdjustPanel`, `ChromeHostAdjust.cpp`) that
floats over the top-right of the canvas hole below the header while a session edit is open (`AdjustInspectorState`
mirrors the session; the panel never re-lays out inside a drag callback). It shows the edit's title (`<Kind>
Adjustment` when an adjustment layer is edited), the kind's controls, a Preview checkbox, a `Limited to the
selection` note for a pixel layer with a selection, and Cancel / OK (the host's default and cancel buttons while
open; Enter and Escape with the canvas focused do the same). Value rows are a label, a `Slider` (logarithmic
for radius, distance, amount, gamma and grain size, so the small values get most of the travel) and an exact
`NumericStepper`; typing into one previews live and is never reformatted out from under the typing (the stepper
stays in its own edit until Enter, a step button or focus loss commits it, the same rule the transform, selection,
brush, gradient and shape headers use). The drawn controls live in `RedPrism/AdjustControls.*`:

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
| File | New (Ctrl+N), New Canvas…, Open… (Ctrl+O), Import Images…; Save (Ctrl+S), Save As… (Ctrl+Shift+S); Export PNG…, Export JPEG…; Close Project (Ctrl+W); Exit |
| Edit | Undo <name> (Ctrl+Z), Redo <name> (Ctrl+Shift+Z, Ctrl+Y); captions carry the history entry name and gray out when unavailable; Cut (Ctrl+X), Copy (Ctrl+C), Copy Merged (Ctrl+Shift+C), Paste (Ctrl+V); Fill with Foreground Color (Alt+Backspace), Fill with Background Color (Ctrl+Backspace), Clear Selection Pixels, Content-Aware Fill… (Shift+Delete); Invert / Invert Mask (Ctrl+I); Free Transform / Transform Selection (Ctrl+T); the Invert and transform captions follow the target and the selection (`SetEditTargets`) |
| Image | Curves… (Ctrl+M), Levels… (Ctrl+L), Hue/Saturation… (Ctrl+U), Exposure…, Gradient Map…, Grain…; Canvas Size…, Image Size…; Flip Canvas Horizontal, Flip Canvas Vertical |
| Layer | New Layer (Ctrl+Shift+N), New Folder (Ctrl+G), Layer via Copy (Ctrl+J), New Adjustment Layer › Levels / Curves / Hue/Saturation / Exposure / Gradient Map / Grain, Edit Adjustment…; Group Selected Layers (the active layer, or an empty folder, wrapped in a new `Folder N`), Move Out of Folder (Ctrl+Shift+G), Rename Layer… (F2), Delete Layer / Delete Layer Mask (the caption follows the target; the Delete key clears selected pixels first, canvas focused), Toggle Visibility, Move Layer Up (Ctrl+]), Move Layer Down (Ctrl+[) (a swap with the next sibling, `Reorder Layers`); Create Clipping Mask / Release Clipping Mask (Ctrl+Alt+G; the caption follows the active layer: a non-folder layer clips to the sibling below it, sharing that sibling's base when it is clipped itself), Merge Down / Merge Group / Merge Layers (Ctrl+E; the caption follows the plan); Add Mask, Add Black Mask (from the selection when there is one); Flip Layer Horizontal, Flip Layer Vertical |
| Select | All (Ctrl+A), Deselect (Ctrl+D), Inverse (Ctrl+Shift+I), Load Layer Selection, Load Mask Selection |
| Filter | Gaussian Blur…, Motion Blur…, Add Noise…, Lens Correction… |
| View | Fit on Screen (Ctrl+0), Actual Pixels (Ctrl+1), Zoom In (Ctrl+=), Zoom Out (Ctrl+-), Sample Ring (a check, on by default) |
| Help | Command List, About RedPrism (version from `Common/Version.h`). Help is the right-justified item on the menu bar |

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
instead, set-1 scan code, the same rule RedSalamander uses for zoom. A layout change retitles the displayed key and
does not move the binding. Numpad `+` and `-` stay `VK_ADD` and `VK_SUBTRACT`, separate from the number row.

One effective binding table (`RedPrism/CommandBindings.h`, built by `Application::ApplySettings` from the catalog
defaults and the settings' replacements: [`Core_Settings.md`](../Core/Core_Settings.md) "Command chords") is what the
keys do and what the chrome shows. A key down goes, in order (`RedPrism/ApplicationKeyboard.cpp`):

1. Escape closes the floating card on top, wherever the focus is: More tools, the notice, the Command List.
2. With the canvas focused, the gestures of an edit under way, which are not commands: Enter and Escape of the size
   card, an open panel, the Crop frame, a pending gradient or transform; Escape of a pixel drag, a warp, a shape
   draft or a stroke; Enter, Escape, Backspace and Delete of a lasso outline in progress.
3. The table: the first binding whose chord matches and whose scope and condition hold runs its command.
4. With the canvas focused, the canvas's own keys: the opacity digits, and Space, which pans while held.
5. The chrome (DxUi), unless the canvas has the focus: a key the canvas did not use goes nowhere, never to the chrome
   control that held the focus before the canvas took it; Escape and Tab move the keyboard to the chrome.

A key a command or a gesture took types no character (its `WM_CHAR` / `WM_SYSCHAR` is dropped). Matching:

- Exact modifiers: a chord matches with its own Ctrl, Alt and Shift only. A command marked for it also takes its
  chord with Shift added: the tool letters, X and D (the legacy canvas read the letter, Shift or not), the M and L
  switches, and the nudges, which step by ten with Shift. An exact chord wins over one with Shift added (Shift+U
  switches the shape kind on the Shape tool; elsewhere it chooses the Shape tool).
- AltGr arrives as Ctrl+Alt. When the layout types a character for the key with those modifiers (€ on AltGr+E, @ on
  AZERTY AltGr+0, Polish ż on AltGr+Z), the key is text and matches no chord; a chrome text field then sees it
  without Ctrl, so AltGr+Z does not undo in the field. Ctrl+Alt never matches a Ctrl-only chord.
- Scope. Canvas commands (tools, palette keys, nudges of the layer and the outline, brush keys, blend modes, the
  Delete key, Alt+Backspace) need the canvas focused. App commands (the menu chords, F2, Ctrl-arrows) run from the
  canvas and from chrome that is not a text input. Decision D4: while a chrome text input has the focus (the Rename
  card, the Command List search, a stepper being typed in), the field keeps every key; only a global command (the
  File menu: New, Open, Save, Save As, Close Project, the exports, Exit) runs, and only for a chord with Ctrl, Alt or a
  function key.
- Conditions share chords: the arrows nudge the outline with a selection tool and a selection, the layer with the
  Move tool; Ctrl-arrows move the selected pixels in any tool while no outline is drawn; `[` `]` need a brush tool;
  M and L switch the Marquee's shape and the Lasso's kind on that tool (they come before choosing the tool).
- Auto-repeat runs a command again, except the M, L and Shift+U switches: holding the key switches once. A
  replacement keeps its command's scope, condition, Shift and repeat rules.
- Decision D5: Alt+F4 (close) and Alt+Space (the system menu) stay the window manager's whatever the settings say;
  Exit shows Alt+F4 and never dispatches it. F10 and a tapped Alt give the keyboard to the menu bar (below).

The chrome shows each command's effective chord: its replacement, else its first default (no aliases). Decision
D9: the modifier names are STRINGTABLE entries (`IDS_KEY_CTRL`, `IDS_KEY_ALT`, `IDS_KEY_SHIFT`, joined by
`IDS_KEY_JOIN`), the key names the active layout's: a letter or digit is the character (its chord follows the key
that produces it); a position key, a named key (arrows, Delete, F2, Backspace, Enter, Space…) and a numpad key use
`GetKeyNameTextW` with the key's set-1 scan code (extended for the navigation keys, so Delete is not the numpad's
`Num Del`), which names numpad `+` and `-` apart from the number row. The menus' accelerator column
(`ChromeHost::CommandMenuCaption`, also for the captions that follow the target: Undo and Redo with the history
name, Invert Mask, Transform Selection, Delete Layer Mask, Release Clipping Mask, Merge Group and Merge Layers), the
Command List, the rail's tool tooltips, the palette hot zones' tooltips and the brush header's tooltips follow the
table and `WM_INPUTLANGCHANGE`; a translated caption keeps its own name, its column is rebuilt.

The menu bar takes the keyboard from a tapped Alt or F10 (`WM_SYSCOMMAND` `SC_KEYMENU`, never the system-menu loop
of a window without an `HMENU`): its first item highlights; Left and Right move, Enter, Space or Down open, a letter
opens the menu with that mnemonic, Escape (or Alt, or F10) leaves, and the focus returns where it was, also before
a command chosen from a menu opened this way runs. Alt+letter opens that menu from anywhere; an Alt chord a command
took opens nothing and an Alt+key nothing took does nothing (no beep).

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
Shift on a nudge command steps by ten. Holding Space to pan, typing into an open edit, and Escape or Enter that
confirms or cancels an edit already under way are gestures of that edit, not separate commands.

Help › Command List lists every command of the catalog, menu rows and local commands alike, by its plain name
(`PlainCommandName`: the caption without its mnemonic, accelerator column or ellipsis). It opens fresh (no query, no
selection) with the keyboard in a search field that filters the list by the command name; the shortcut column is
the effective chord as the chrome shows it. A row's id is its command id, so a selection survives filtering only
while its command is listed and never moves to another command's row. In the search field, Up and Down move the
selection and Enter runs the selected row, or the first listed row when none is selected (nothing when the list is
empty); on the list, Enter, Space or a double-click runs its row. Running a row closes the list first. Enter runs a
row only while the list's search field or list has the keyboard: in a card underneath (the Rename card's field) it
belongs to that card. Escape or OK closes the list; the card underneath gets its default and cancel buttons back
and, when the list held the keyboard, the focus: the Rename card's field, the color picker, or else the canvas.
StatusStrip shows zoom percent, document size, and the current tool with its effective chord
(`ChromeHost::CommandChordText`). The tab title is the file name (or Untitled) with a dirty mark when history is
modified.

## Unimplemented functions

The shell exposes the documented command surface through menus and the tool rail. A function that has no implementation yet MUST, when invoked, show
the localized notice `IDS_NOT_IMPLEMENTED` ("{0} is not implemented yet.", the function named by its own caption
without its mnemonic, accelerator column or ellipsis) through `Application::ShowNotImplemented`, and change nothing
else: no document, history, selection, tool or viewport change, no side effect before or after the notice. The
notice is a card over the canvas (`ChromeHost::ShowNotice`), not a message box, shown from the message loop after
the command or click that raised it returns (never from inside a DxUi dispatch). OK or Escape dismisses it. Quiet
runs (`--self-test`, `--screenshot`, `--bench`) count it instead of showing it. A color or filter command that
cannot run on the active layer shows the same card with `IDS_CMD_UNAVAILABLE` ("{0} cannot be used on this layer.")
and does not open an empty panel. A stub leaves with its implementation, and the not-implemented notice never
appears for an implemented function. No exposed function awaits an implementation since C16 closed
([`Completeness_2026-09-21.md`](../Plans/Done/Completeness_2026-09-21.md)); a function surfaced ahead of its
implementation joins a table here until it lands. The self-test raises the notice under quiet UI and checks that
the count rises by one while the document and the history stay as they were.

## Layers panel

The layers Tree lists rows top-first (`DocumentSession::LayerRows`): names, `(hidden)` annotation for rows whose
effective visibility is off, a folder glyph for groups (Segoe Fluent Icons when that font is installed), and depth
indentation. Selecting a row selects the layer; toggling a group's expander updates
`DocumentSession::CollapsedGroupIDs` (rows carry `HierarchyEntry::collapsed`). Dragging a row previews an insertion
line and, on release, calls `DocumentSession::PlaceLayer`: the top or bottom of a row inserts beside it, the middle
of a folder drops inside it, and Escape or capture loss cancels. A folder cannot be dropped into its own descendant.

Rename (C16): Layer › Rename Layer…, F2 or a double-click on a row (`IDxTreeDelegate::OnTreeItemInvoked`, which
selects the row first) opens the Rename Layer card (`ChromeHost::OpenRenameCard`, a `CardPanel` with a `TextField`
holding the name selected, Cancel and the primary OK) over the canvas hole beside the layers panel; Enter or OK
commits the name through `DocumentSession::RenameLayer` (trimmed, a blank name ignored) as one `Rename Layer`
entry, Escape or Cancel leaves it. The card renames the layer it was opened for, whichever row is selected when it
commits; it closes without renaming when that layer goes away (Undo, a delete, a merge), when the document is
replaced, and when an interaction settles (a tool switch). Merge (C16, legacy `LayerMerge.swift`): Ctrl+E follows
`DocumentSession::PlanMerge` — the active layer merges with the layer beneath it in its folder (`Merge Down`), a
folder merges its contents and goes (`Merge Group`), several selected layers merge together (`Merge Layers`); the
Layer menu's caption names the plan. The host composites the planned layers alone — cut loose from any parent or
clipping source outside the plan — through the canvas composition (`Compose` on that subset, `Readback`, the
document invalidated before and after) and `DocumentSession::MergeLayers` installs the result trimmed to its
pixels in the anchor's place, named after the plan, with layers clipped to any merged layer clipping to it, as one
entry; a failed composite reports `IDS_ERROR_FLATTEN`. The self-test renames through typed characters and Enter,
leaves a name alone on Escape, and merges a blue layer down onto a red one into one 14 × 14 layer.

The 32 DIP title row holds the `Layers` label and a `DxUi::NumericStepper` (0–100 %, step 1, Shift 10, unit `%`,
accessible name `Layer opacity`) bound to the active raster layer's opacity. Typing previews live on the layer the
edit started on; Enter, focus loss, the buttons, Up/Down and `RequestValue` commit; Escape restores the start value.
One typed edit is one `Layer Opacity` history entry from the value before the first preview; a cancelled edit leaves
no entry, and so does one typed back to its start value (the stepper reports nothing then). No history transaction
stays open while the field edits: the preview changes the document outside history, and anything else first records
the typed value as its own entry — a command (menu, key, Command List), a tool switch, a press on the canvas,
another layer selected or a row dragged, a save, or the document replaced — so no other edit folds into it or bakes
the preview in; the field's later commit of that same value adds nothing, to another layer or another document
alike. The stepper is disabled for folders or without an active layer and is never rewritten while the user is
typing in it.

## Palette and color picker

The palette (opaque ARGB foreground and background) is session state outside history
(`DocumentSession::ForegroundColor` / `BackgroundColor`, `SwapPaletteColors`, `ResetPaletteColors`), as the legacy
brush settings were. A brush stroke paints that foreground color; swapping or resetting changes the next stroke. The rail hot zones and the default keys run `kCommandSwapPalette` and `kCommandResetPalette`.
Clicking a well opens the `DxUi::ColorPicker` on a `CardPanel` just above that well, 8 DIP
right of the rail, so the card sits beside the palette instead of under the tool header. If the card would leave
the window it stays below the tabs and above the status strip. Chrome composites over the canvas with premultiplied
alpha, so the card is visible there. The picker edits the clicked well: field and hue-strip drags, typed R/G/B, six-digit hex and
host-sampled colors (`ChromeHost::SampleColor`, the eyedropper feed) preview into the well; OK or Enter commit and
notify the palette handler; Cancel, Escape (also when a picker child has focus) or capture loss restore the well.
Pointer input inside the card belongs to chrome even though it lies in the hole; a canvas click outside it focuses
the canvas and leaves the picker open. Captions come from the STRINGTABLE (`IDS_PICKER_*`, `IDS_PALETTE_*`).
Mask-selected black/white policy arrives with mask painting.

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
canvas-focus toggle. Every interactive control carries an accessible name: buttons and checkboxes from their
captions, steppers and sliders from their labels, combo boxes from their tooltips, the wells, swatches, tree,
tabs, rail, splitter, picker and the drawn adjustment controls from `IDS_A11Y_*` strings.
`ChromeHost::UnnamedInteractiveControls` walks the tree and the WARP self-test requires it empty, walks the UIA
tree from the window root (at least eight named elements) and resolves a point under the menu bar. Detaching the
chrome retires the provider (`UiaReturnRawElementProvider(hwnd, 0, 0, nullptr)`, `UiaDisconnectProvider`).

## Size cards

New Canvas…, Canvas Size… and Image Size… open a card (`ChromeHost::BuildSizeCard`, a `CardPanel` floating over the
canvas hole at the layers side like the adjustment panel; a point on it is chrome). The `Application` owns the draft
(`SizeDialog`: a `CanvasSizeDraft`, an `ImageSizeDraft`, the anchor, the extension choice and its custom color, the
New Canvas size) and mirrors it through `SetSizeCard`; every control reports one `SizeAction` with one value, a typed
number counting when committed (Enter, a step button, the focus leaving) so a locked ratio never follows a
half-typed value. Enter is the card's OK and Escape its Cancel (the card's default and cancel buttons inside it, the
accelerators with the canvas focused); a tool switch or a document on its way out cancels the card. Apply first
settles the interactions in flight as a document on its way out does (a pending gradient or transform commits as its
own entry), then resizes that document through `ResizeCanvas` / `ResizeImage`
([`Document_Tools.md`](../Document/Document_Tools.md)) as one `Canvas Size` / `Image Size` entry and fits the viewport; `TooLarge` reports `IDS_ERROR_TOO_LARGE`, any other failure
`IDS_ERROR_RESIZE`; an unchanged size closes the card.

| Card | Rows |
| --- | --- |
| New Canvas | The hint, Width and Height in pixels (1…30 000, else the invalid note replaces the sRGB note and Create Canvas is disabled), Open Project… and Import Image… (they close the card and run the command), Cancel, Create Canvas: after the save prompt a fresh unmodified untitled document with one blank layer. The size starts at 1920×1080, then the last typed size; an image on the clipboard suggests its size when the card opens |
| Canvas Size | Current W × H and its uncompressed RGBA size, Units (Pixels, Percent, Inches, Centimeters), Width and Height in the unit (three decimals, two for percent), Relative to current dimensions, Lock original aspect ratio (locking settles the height on the width), the New size line or the invalid note, the 3×3 anchor grid (pressed = chosen, named for accessibility, the name and hint beside it), Canvas extension (Transparent, Foreground, Background, Black, White, Custom: the color picker opens on the custom color and the swatch shows it; Cancel restores the previous custom color), Cancel, OK |
| Image Size | Current W × H, Units (only Inches and Centimeters without resampling), Width and Height, Lock aspect ratio (disabled without resampling), Resolution in pixels/inch (1…9 600), Resample, Sampling (Nearest, Smooth, High quality) while resampling, the note (pixels resize, or only the print size changes), the Result line or the invalid note, Cancel, Resize |

## Files

New creates a 1280×800 blank layer and marks history saved; New Canvas… opens its size card (below).
Open and Save use `IFileDialog`. Import Images… opens a multi-select image dialog and adds every chosen image as a
layer above the active one, in one `Import Images` history entry (`Import Image` for a single file); with no
document open the first image becomes the document (see below) and the rest join it.

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
pending gradient, a changed transform, a typed opacity — counts as a change, so a clean document with one still
asks. Quiet runs never ask (they proceed, or take the self-test's forced answer). Close Project (Ctrl+W) leaves the
shell without a document: the status strip shows `—`, the layers panel is empty, the canvas hole shows the clear
color and the document commands do nothing; New, Open, Import Images… and a drop start the next document.

Save, Save As and Export write what the canvas shows (review-fix Decision D2): before writing they settle the
document as Enter would — a typed opacity records its entry, a live stroke or warp and a pending gradient commit, a
pending transform applies, and an open color panel applies as its OK does. An adjustment layer's panel previews
inside the document, so a file written under it would hold a value the panel's Cancel later takes back while the
document shows clean; applying first leaves nothing to take back. A crop frame, a shape draft, a lifted-pixel drag
and the size card are not document edits and stay. (The legacy app refuses file operations while a stroke, a panel
or an adjustment edit is open; RedPrism applies instead.) Save writes a ZIP `.prism` for a `.prism` name or a ZIP
file; a project opened from a folder — an exploded project, whatever its name, `*.prism` included — saves in place as
a folder, and Save As with the folder filter writes a folder.

When Windows ends the session (sign-out, restart, shutdown, an installer's restart request) a document with unsaved
work asks too: the window refuses the end with the reason `IDS_SHUTDOWN_UNSAVED` ("<name> has unsaved changes.")
that Windows shows while it waits, and the save prompt above runs from the message queue; Save or Don't Save closes
the window without asking again and Windows goes on, Cancel keeps it and the user cancels the end in Windows. A
critical end is never refused. The settings are saved when the session ends, as a close saves them; the message
handling lives in [`UI_Windowing.md`](UI_Windowing.md).

Dropped files (long paths included) open a `.prism` or exploded folder, or import an image as above. Load failures
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
The session checks drive the typed opacity (typed back to its start, a command before Enter, a new document, Escape,
a commit), the Rename card's own layer, Cancel at the save prompt over a panel, the size card, a crop frame, a shape
draft and a pending transform, a clean document with a live stroke, Save under an adjustment layer's panel, a folder
project saved in place, an image batch whose first file fails, Canvas Size over a pending gradient, a copy the system
clipboard refused (the user's clipboard is set aside, cleared and restored around every clipboard step, and each
mirror is read back), capture loss mid-stroke and mid-drag, a splitter drag cancelled, a leave without a capture,
Space panning across a focus loss, Alt-press then Undo then a drag, a close inside a chrome callback and the session
end.
The self-test's keys all go through the window procedure (`Application::HandleAccelerator` sends the key down —
a system key with Alt —, the character `TranslateMessage` would post and the key up, with a real scan code and the
modifiers in the thread's key state, after a frame). Its keyboard steps (`RunKeyboardSelfTest`) replace X with Q
(Shift+Q too), let the later of two entries for one command win and show it in the palette tooltip, prove a zoom
replacement leaves no default or alias live, move the Delete key's command to Q (Delete does nothing, Q clears, Delete
Layer loses and regains the layout's Delete label), keep a replaced Marquee switch to once per press, type into the
Command List's search field without running Q, Ctrl+A, Ctrl+Backspace, Ctrl+Z or Delete, run no Ctrl chord from
Ctrl+Alt, filter the list to Sample Ring and run it with Enter (a typed space is text, not a pan), keep a selection
from moving to another command's row and run the first match without one, run a selected row with Enter on the
list, rename through the Rename card with Enter while a Delete Layer row is selected in the open list, give the
keyboard back to the Rename card when the list closes, pan with Space only while it is held, give the menu bar the
keyboard from a tapped Alt, F10 and `SC_KEYMENU` without a system-menu loop (a timer ends one if it starts), open the
File menu with Alt+F but not when Alt+F is a command, set the Move tool's opacity with 5, 0, Undo, 4 then 5 as the
legacy golden does, and show the layout's key names, the STRINGTABLE modifiers and replacements in the Command
List, the menus (Undo, Invert) and the rail tooltip. `HostTests` covers tool tables, glyphs, the string table (every
command caption's column is its first default chord), the key names, the catalog and the binding table.
