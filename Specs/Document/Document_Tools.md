# Document tools: transform (C8), selection (C9), brush (C10) and retouch (C11)

Status: current normative contract (created with C8; later tool phases extend it)
Last reviewed: 2026-09-21
Owner: `Common/TransformEdit.*`, `Common/Selection.*`, `Common/BrushStroke.*` (the session's brush and retouch
members), `Common/Heal.*`, `Common/ImageFilters.*`, `Common/WarpStroke.*`,
`DocumentSession` transform and selection members in `Common/Document.*`, `RedPrism/TransformOverlay.h`,
`RedPrism/CanvasOverlay.*`, the Move-tool, selection-tool and brush code in `RedPrism/Application.cpp`

## Scope

This contract owns how a layer's placement is edited: the drag geometry, snapping, free distortion, the pending-edit
lifecycle in `DocumentSession`, flips, and the rules the Move tool and its header follow. Rendering of a
transform is the composition's job ([`Rendering_Composition.md`](../Rendering/Rendering_Composition.md)); the chrome
that shows it is described in [`UI_ApplicationChrome.md`](../UI/UI_ApplicationChrome.md). Since C9 it also owns the
selection: its geometry, the session operations on it, and the Magic Wand and mask-tracing kernels.

## Transform model

`LayerTransform` is the unrotated box (origin, size in document pixels), a clockwise rotation in degrees about the
box center, two flips and the sampling. Its helpers are the legacy ones: `Point(unit)` maps the unit square (y
down) to the document without flips, `Contains(point)`, `Corners()` in handle order (top-left, top-right,
bottom-right, bottom-left), `ScalePercent(pixelSize)` / `ScaledToPercent(percent, pixelSize)` (100 % draws the
pixels 1:1; scaling keeps the center, rotation and flips), `Rounded()` (whole pixels and degrees, sizes at least 1),
`UnitToDocument()` (an `Affine2` with flips), `Placing(map)` (the rotated, maybe flipped rectangle a map places the
unit square on; shear is dropped, the horizontal flip and the rotation nearest the current one are kept),
`Following(from, to)` (a placement carried along as a layer moves; a plain move carries exactly),
`SamePlacement(other)` (equal ignoring sampling) and `Mirrored(horizontally, axis)` (flip toggled, rotation negated,
center reflected across the axis). `LayerTransform::kHandles` lists the eight handle unit points: corners at even
indexes clockwise from top-left, edge midpoints at odd indexes.

`Affine2` is the row-vector affine map of `CGAffineTransform` (`Concat` applies this map then the other).

## Drag geometry (`TransformDrag`)

A drag records its mode (Move, Resize with a handle index, Rotate, Distort with a handle index), the transform at
the press, the press point in document pixels and, for a distortion, the corners at the press.
`Updated(point, lockRatio, shift, option)`:

- Move adds the pointer delta; Shift keeps the larger axis only.
- Rotate adds the angle swept around the center; Shift snaps to 15°.
- Resize keeps the opposite handle fixed (Alt/option: the center, so both sides grow), works in the box's own
  axes, never lets a side go below 1 pixel, and keeps the aspect ratio when `lockRatio != shift` (a corner projects
  onto the original diagonal; an edge scales both sides by its own factor).
- The result is used only when `IsValid()`; otherwise the original stays.

`Corners(point, shift)` moves a distortion's corners: a corner handle its corner, an edge handle both corners of
the edge, the body all four; Shift keeps one axis. It is nullopt unless corners were recorded.

The host rounds drag results (`Rounded()`); typed values stay exact.

## Snapping (`TransformSnap`, `DocumentSession::SnappedMove`)

A moving box snaps its left, center or right (and top, middle or bottom) to the nearest target within a tolerance,
each axis on its own; the smallest move wins and the first candidate breaks ties. Targets are the canvas edges and
center plus every other visible pixel layer's rounded bounds and center (`TransformSnapTargets`, the moving layers
excluded). The tolerance is `TransformSnap::kDistanceDip` (10 DIP) divided by the viewport's points per pixel, so
the pull feels the same at any zoom. The targets met are kept in `LastSnapGuides` until `ClearSnapGuides` so the
overlay can draw them. Resizing and rotating do not snap; Ctrl during a move drags freely.

## Pending edit (`DocumentSession`)

`BeginTransform(persistent)` opens a `TransformEdit` when `CanTransform()`: a visible pixel layer is active (blank
layers have no pixels and cannot transform; folders alone cannot either), or the selection transforms as a group
(several selected layers, or one selected folder: its visible pixel layers). The draft starts at the layer's
transform, the mask's placement when an unlinked mask is selected (`SelectLayerTarget(id, mask)` and
`TransformTargetsMask`), or the upright box around the group's members.

`PreviewTransform(value)` changes only the draft (invalid values are ignored); the document, history and
`ActiveLayer()->transform` do not change while previewing. Rendering reads `DisplayDocument()`: the real document
when no edit is pending, else a cached copy sharing every raster where each moved layer carries
`PendingTransform` (the draft, or for a group each original `Following` the box) and `DisplayedMaskPlacement` (a
linked placement follows; an unlinked mask stays where it was; a uniform 1 × 1 mask never needs a placement).
`EditedTransform(layer)` is where the handles sit. `TransformPixelSize()` is what 100 % scale draws 1:1.

`CommitTransform()` records one history entry and clears the edit: `Transform Layer`, `Transform Layers` (group,
masks following), `Transform Layer Mask` (placement only; nothing when it lands on the layer),
`Distort` / `Distort Layers` / `Distort Layer Mask`. A draft equal to the original records nothing.
`CancelTransform()` discards the edit. Both clear the snap guides. Non-persistent edits (pointer drags, arrow
nudges) commit when the drag ends; persistent ones (Free Transform, typed header values) wait for Apply or Cancel.
Selecting another layer commits a pending edit; switching tools does too (host rule); Undo or Redo while an edit is
pending cancels it instead of touching history. `NudgeLayer(dx, dy)` moves a pending draft, or commits a move at
once.

`BeginDuplicateTransform()` (Alt-drag) commits the pending edit, duplicates the active layer as `<name> copy`
sharing its pixels (`DuplicateActiveLayer`, one `Duplicate Layer` entry that also spans the following move) and opens
a non-persistent edit on the copy; cancelling removes the copy again and leaves no entry. Groups do not duplicate.

## Free distortion (`DistortWarp`)

`BeginDistort()` turns a pending edit into a distortion whose four corners move freely (`PreviewCorners`; a twisted
or collapsed shape is ignored, `IsUsable`). The edit becomes persistent. A distortion is drawn live by warping the
pixels at a bounded preview size (longest side 1024) into the shape; Apply (`CommitTransform`) resamples at full
resolution (`WarpTrimmed`: the shape's whole-pixel bounds, then cropped to the visible pixels so a stroke hugs what
is there), replaces the layer's pixels and thumbnail, and sets an ordinary axis-aligned transform over the result.
A linked mask without a placement is warped and cropped with the pixels; a uniform mask passes through. The warp is
the perspective map of the unit square onto the corners (`MakeHomography`), sampled bilinearly (nearest when the
layer's sampling is Nearest) from the source with the layer's flips honored, edges antialiased by four sub-samples.
Group distortions carry each member's transform by the same perspective (`Carried`). A distorted mask edited alone
is warped over its shape with its edge background outside (`WarpMask`).

## Flips

`FlipLayers(horizontally)` mirrors the active layer about its own center, or the group's members about the box
center, as one `Flip Horizontal` / `Flip Vertical` entry; masks follow the link rule. `FlipCanvas(horizontally)`
mirrors every layer and placed mask across the canvas middle and reflects the selection outline, as one
`Flip Canvas Horizontal` / `Flip Canvas Vertical` entry. Both settle a pending edit first.

## Session flags

`locksTransformRatio` (default on), `transformAutoSelect` (default off) and `showsTransformControls` (default on)
are session state the header toggles; nothing here is serialized beyond the layer transforms already in `.prism`.

## Selection model (C9)

`DocumentSelection` is a set of closed loops in document pixels (`SelectionLoop`, a `std::vector<Vec2>`) filled by
the nonzero winding rule, plus `antialiased`. It lives on `CanvasDocument::selection`, so history covers it and
document equality includes it; it is never saved. `nullopt` means no selection. A selection whose loops enclose no
area (`IsEmpty`: no finite point, or zero-width or zero-height bounds) is an explicit empty selection that later
edits must treat as "touch nothing", never as "touch everything". Loops from the boolean operations are
Clipper2-normalized (outer loops and holes, no overlaps) and kept to a hundredth of a pixel; wand and mask traces are
pixel-edge loops, outer loops clockwise and holes counterclockwise in top-left coordinates, which the nonzero rule
fills exactly.

`SelectionGeometry` (`Common/Selection.*`, Clipper2 from vcpkg) provides `Rect`; `Ellipse` (a polygon with segments
about two pixels long, a multiple of four so the extremes are exact); `Bounds`; `Union`, `Subtract` and `Intersect`
under the nonzero rule; `Offset(delta)` (round-join inflation: a positive delta is Photoshop's Expand, a negative one
Contract, and contracting past the middle leaves no loops); `Translated`; `Transformed(Affine2)`; `Contains`
(nonzero winding, a point on the outline counts as inside); and `Coverage(width, height, antialiased)`, a Gray8
raster at document resolution. Antialiased coverage is the exact area of the pixel inside the loops (signed-area
accumulation, |winding| clamped to 1); hard coverage selects a pixel whose center lies inside, a center exactly on an
edge counting for the left and top edges only (the Direct3D top-left rule). Geometry beyond the canvas still covers
the canvas pixels it overlaps. `DragBox(anchor, point, square, fromCenter)` is the box a drag spans in whole pixels:
the pointer rounds, `square` evens the sides toward the drag direction, `fromCenter` grows around the anchor.

## Selection session

Selection state on `DocumentSession`: `lassoKind` (Freehand default, Polygonal), `marqueeKind` (Rectangle default,
Ellipse), `selectionModeChoice` (New default, Add, Subtract), `heldSelectionMode`, `selectionAntialiased` (default
on), `wandSettings`, `selectionExpandAmount` and `selectionContractAmount` (1–500, default 1). `CanEditSelection`
needs a document and no pending transform (commands settle the transform first).

- `SelectionModeFor(shift, option)`: Alt subtracts (with or without Shift), Shift adds, else the header choice.
  `LassoCursorMode` keeps an outline's starting mode; `DisplayedSelectionMode` (what the header highlights) uses the
  tracked held keys (`UpdateHeldSelectionKeys`, true when the displayed mode changed).
- `BeginMarquee(point, mode)` starts a draft at a whole-pixel anchor in `marqueeKind`; `DragMarquee(point, square,
  fromCenter)` reshapes it through `DragBox`. `BeginLasso(point, mode)` starts a draft in `lassoKind`;
  `ExtendLasso` appends a point (points closer than a quarter pixel are skipped); `MoveLassoCursor` sets the
  polygonal rubber band; `RemoveLastLassoPoint` drops the last corner (an emptied draft ends);
  `CancelLasso` drops the draft; `ToggleMarqueeKind` / `ToggleLassoKind` cancel the draft and switch the kind.
  Neither begins while an outline move is in progress.
- `FinishLasso` closes the draft and combines it: an ellipse fills the drag's whole-pixel box; fewer than three points
  or a zero-area outline deselects in New mode and does nothing otherwise. History names: `Lasso`,
  `Polygonal Lasso`, `Rectangular Marquee`, `Elliptical Marquee`.
- `ApplySelection(shape, mode, name)` clips the shape to the canvas, then New replaces, Add unions, Subtract
  subtracts (subtracting from no selection changes nothing). The result takes `selectionAntialiased`.
  `SetSelection(value, name)` records one entry when the value differs.
- `CanMoveSelection(point)`: a non-empty selection contains the point and no draft is in progress.
  `BeginSelectionMove` opens one `Move Selection` entry; `MoveSelection(dx, dy)` places the original loops at the
  rounded offset from the drag start without re-clipping (the outline can leave the canvas and come back intact);
  `EndSelectionMove` closes the entry; `NudgeSelection` is one entry per press.
- `CanModifySelection`: a non-empty selection and no draft. `ExpandSelection(amount)` offsets outward and clips to
  the canvas (`Expand Selection`); `ContractSelection(amount)` offsets inward, away from the canvas edges too, and
  can leave an empty selection (`Contract Selection`). Amounts outside 1–500 are ignored.
- `SelectAll` (`Select All`, antialiased), `Deselect` (`Deselect`; nothing without a selection), `InvertSelection`
  (`Inverse`, canvas minus the selection).
- `LoadMaskSelection(id, mode)`: the mask's pixels darker than 50 % (`MaskTracing::DarkPixels`), mapped through the
  mask's placement (`PixelToDocument(MaskTransform)`), combined by `mode` (`Load Mask Selection`); false when the
  layer has no mask or no dark pixel. `LoadLayerSelection(id, mode)`: the layer's pixels at least 50 % opaque,
  ignoring its mask, mapped through the layer transform, flips included (`Load Layer Selection`); false for a
  folder, a blank layer or an image with no opaque pixel.
- Undo and Redo drop a draft in progress. `FlipCanvas` mirrors the loops with the layers.

## Magic Wand

`WandSettings`: `tolerance` 0–255 (default 32), `sampleSize` (Point, 3 × 3, 5 × 5: the radius averaged around the
click), `contiguous` (default on), `sampleAllLayers` (default off). `MagicWand::Mask` is the legacy `wand_mask`: the
reference color is the rounded average over the sample square clipped to the image; a premultiplied BGRA pixel
matches when every channel, alpha included, lies within `tolerance` of it; contiguous fills 4-connected from the
seed with a scanline flood (nothing when the seed itself does not match), otherwise every matching pixel counts.
`MagicWand::Trace` is `wand_trace`: directed pixel edges walked clockwise around selected pixels, turning right
where loops meet so touching corners stay apart; more than 8 000 000 edges is `WandFailure::TooDetailed`, an
allocation failure `WandFailure::Memory`. `MagicWand::Select(image, point, settings)` floors the point to a pixel,
masks and traces; nullopt when nothing matches or the point is outside the image.

`DocumentSession::MagicWand(point, mode, composite)` needs a document, no outline move in progress and a point on
the canvas. `WandSample` is what it reads at document size: with All Layers the caller's composite (the host's
flattened composition readback; a missing or wrongly sized composite makes the click do nothing), otherwise the
active layer's own pixels at its displayed transform without its mask (`RenderLayerPixels`: bilinear, edges
clamped inside the placed box, transparent outside it); a folder or blank layer reads as transparent, so the whole
canvas matches. Nothing matched deselects in New mode. A New selection takes the traced loops without the canvas
clip (they already lie on it); Add and Subtract go through `ApplySelection`. One `Magic Wand` entry.

## Brush session (C10)

The stroke model, the coverage engines, the commit and the live preview live in
[`Rendering_Brush.md`](../Rendering/Rendering_Brush.md); this is the session's contract. Brush state on
`DocumentSession`: `brushSettings` (`BrushSettings`: diameter 1–2000 px, default 40; hardness 0–1, default 1;
straight red/green/blue 0–1, default black; opacity 0.01–1, default 1; `IsValid` gates every field), `brushMode`
(Paint default, Erase: the Brush tool's own erase mode) and `maskPaintWhite` (default false: a mask is painted
black, hiding). `CanPaint` needs a document, no pending transform, no stroke in progress, an active layer that is
visible with its ancestors and is not an adjustment layer (a folder only through its mask), an enabled mask when the
mask is selected, and no explicitly empty selection.

- `SetBrushEngine(engine)` chooses the coverage engine for later strokes (nullptr: the CPU kernel); the host binds
  its compute engine with the device and drops it with the device.
- `BeginBrush(point, erase)` starts a stroke on the active layer, or on its mask when the mask is selected (a mask is
  never erased: it is painted black or white per `maskPaintWhite`), clipped to the selection's coverage when one
  exists, with the layer's pixel budget (`RasterEditPixelLimit`); false when painting cannot start or the first
  sample already fails. `ContinueBrush(point)` appends a sample; a budget or engine failure drops the stroke
  (`PendingBrush` becomes null). `FinishBrush` settles the tail and records one entry (`Brush Stroke`, `Erase`,
  `Paint Mask`) unless nothing was painted or the layer's pixels or transform changed under the stroke;
  `TakeLastBrushCommit` hands the host that commit (raster and grid rectangle) once, for the preview's adoption.
  `CancelBrush` drops the stroke. Undo and Redo of a stroke share every untouched tile.
- `ShiftLineStart` is the last sample of the previous stroke on the same layer or mask, for Shift-click lines.
- `ChangeBrushSize(increase)` steps the diameter by a fifth, at least one pixel, within 1–2000;
  `ChangeBrushHardness(increase)` snaps to the next quarter; `TypeOpacityDigit(digit, seconds)` is Photoshop's
  opacity typing (1–9 set 10–90 %, 0 sets 100 %, a second digit within 0.6 s makes a two-digit percent, never below
  1 %). All three ignore keys while a stroke is in progress.
- `AddLayerMask(revealing)` gives the active layer a uniform 1×1 mask (`Add Reveal-All Mask` / `Add Hide-All Mask`)
  and selects it; `ToggleLayerMask(id)` records `Enable Layer Mask` / `Disable Layer Mask`.

## Retouch session (C11)

The brush pipeline takes a kind (`BrushKind`: Paint, Erase, Heal, Clone, Blur) through `BeginBrush(point, kind,
composite)`; the kernels, the tile composition and the warp model live in
[`Rendering_Brush.md`](../Rendering/Rendering_Brush.md).

- **Spot Healing** (`spotHealingMode`: Content-Aware default, Create Texture, Proximity Match; `SpotHealing::Heal` in
  `Common/Heal.*` is the legacy `HealPixels.c`): the stroke shows a dark wash while painting; `FinishBrush` rebuilds
  the painted area from nearby texture and records `Spot Healing`. Never on a mask.
- **Clone Stamp** (`cloneSettings`: `aligned` default on, `sampleAllLayers` default off; `cloneSource`):
  `SetCloneSource(point)` (Alt-click) starts a new alignment; `CloneStrokeOffset(point)` is the whole-pixel offset a
  stroke would copy with (aligned strokes keep the first stroke's, otherwise from the brush to the source);
  `CloneSamplePoint(point)` is where the crosshair sits; `CloneSample(composite)` is what a stroke copies from at
  document size (the active layer's pixels as placed, or the host's composite, required for All Layers). A stroke
  without a source does not start. Records `Clone Stamp`. Never on a mask.
- **Smear** (`smearMode`: Liquify default, Blur, Smudge). Blur is a brush stroke that paints `BlurSample(mask)`, the
  layer (or its mask, with its edge tone beyond its pixels) softened by sigma = diameter / 10 clamped to 1.5–30,
  through the tip at the Strength opacity (`Blur`). Liquify and Smudge are warps (`WarpStroke`): a document-size copy
  of the layer as shown, changed dab by dab (`BeginWarp` / `ContinueWarp`; spacing 8 % of the diameter for Smudge,
  2.5 % for Liquify; hardness at most 0.98; strength = opacity); Smudge carries the color it picked up, taking on
  what it leaves by the strength; Liquify moves pixels with the brush, most at the center. `FinishWarp` paints the
  result into the layer along the dabs with a hard tip four pixels wider, replacing the pixels under it (`Liquify`,
  `Smudge`); `CancelWarp` drops it; `PendingWarp` is the stroke in progress. Never on a mask.
- **Tip families**: `SwitchBrushTipFamily(from, to)` parks the size, hardness and opacity per family (0 the Brush,
  Eraser and Spot Healing; 1 Clone Stamp; 2 Smear; the parked families start soft at 40 px).
- **Gradient** (`gradientSettings`: shape Linear / Radial, style Foreground to Transparent default / Foreground to
  Background, `reversed`, `opacity`): `BeginGradient(point)` opens a pending raster edit on the active layer or mask
  (a new line on the same target replaces the pending one); `MoveGradient(start, end)` and `RefreshGradient`
  re-render from the endpoints, settings and palette (the palette setters refresh it); `GradientColors` are the two
  straight ends; `EndGradientDrag` drops a click without a line (under half a pixel); `CommitGradient` records
  `Gradient` / `Gradient Mask`; `ResolveGradient` commits a pending edit and runs on layer and target switches and
  before a new layer is inserted; `CancelGradient` and the first Undo drop it. The fill covers the canvas (before the
  start and past the end take the end colors; radial is centered on the start with the end on its rim) within the
  selection, composited over the original pixels at the opacity; a mask takes the colors' first component as gray.
- **Shape** (`shapeKind` Rectangle default / Ellipse, `shapeCornerRadius` 0–5000): `BeginShape` anchors at a whole
  pixel; `DragShape(point, square, fromCenter)` uses `DragBox`; `FinishShape` adds a layer above the active one named
  `Rectangle N` / `Ellipse N` filled with the foreground color (`ShapeImage`: exact-area coverage of the rectangle,
  its corners rounded by the radius at most half the shorter side, or the ellipse), one entry (`Rectangle`,
  `Ellipse`), keeping the selection; a drag under a pixel makes nothing; over 100 M pixels is refused.
  `ToggleShapeKind` (Shift-U) drops a draft. A shape layer stays live (`LiveShape`: its pixels are still the ones the
  shape drew, tracked by `ImageLayer::shapeImage`) and redraws at its new size when a transform commits, keeping a
  rounded corner's radius; a mask on its grid takes an explicit placement so it stays put. Loaded shape layers are
  live; painting on one makes it plain pixels.
- **Eyedropper**: `SampleCompositeColor(composite, point)` is the straight sRGB color of the host's composite at a
  document pixel (nullopt outside or over transparency); `showsSampleRing` (default on) shows the comparison ring.
- `AddPixelLayer(image, origin, name, editName, dropsSelection, shape)` inserts pixels as a layer above the active
  one (inside its folder) in one entry, the base for shapes and pastes.

## Pixel edits (C9b)

The selection edits of the legacy `SelectionEdits.swift`, `FloatingSelection.swift` and `SelectionClipboard.swift`:
raster edits built on the brush stroke's canvas passes and lifted pixels
([`Rendering_Brush.md`](../Rendering/Rendering_Brush.md)), each one history entry through `CommitStroke` (which can
apply a moved outline inside the same entry). `CanEditPixels` is `CanPaint`: a paintable active layer or targeted
mask, no pending transform or stroke, and no explicitly empty selection.

- **Fill** (`FillSelection(source)`; `Fill` / `Fill Mask`): the selection, or the whole canvas without one, takes the
  foreground or background color; an explicitly empty selection changes nothing. A mask has its own palette: black
  hides and white reveals, the foreground per `maskPaintWhite` and the background the other, so the default
  foreground fill hides the selected area.
- **Clear** (`ClearSelectedPixels`; `Clear`): with a selection, the selected image pixels become transparent
  through the selection coverage (soft edges fade; the clip follows a scaled layer); on a mask the selection fills
  with the background color, as in Photoshop. `DeleteKeyPressed`: Clear with a selection, otherwise
  `DeleteLayerOrMask` (`DeleteLayerMask`, `Delete Layer Mask`, when the mask is targeted, else `DeleteLayer`).
- **Invert** (`CanInvert`, `InvertPixels`; `Invert` / `Invert Mask`): colors invert keeping transparency
  (premultiplied, each channel becomes alpha − channel) and a mask flips, inside the selection or across the whole
  layer (its off-canvas pixels too) without one. Available in every tool: a pending transform commits and a pending
  gradient applies first. A uniform 1×1 mask with a selection expands to the layer grid. Not on a group's pixels, a
  hidden layer, an adjustment layer or a disabled mask.
- **Pixel moves** (`PixelMove`: the lifted raster, the outline it started from, `duplicate`, `offset`):
  `BeginPixelMove(duplicate)` cuts the selected image pixels out of the layer through the coverage (false without a
  non-empty selection, with a mask targeted, or over no pixels); `MovePixels(dx, dy)` previews them a whole-pixel
  offset away (the hole stays unless duplicating; the raster's tiles feed the host's stroke preview);
  `FinishPixelMove` commits the raster and the moved outline as one `Move Pixels` / `Duplicate Pixels` entry (a zero
  offset commits nothing); `CancelPixelMove` drops it; `NudgePixels(dx, dy)` is one entry per press.
  `DisplayedSelection` is the outline to draw: during a move the original shifted by the offset; while transforming
  selected pixels, the outline mapped through the handles (`FloatingSelectionMap`) or the distortion's homography.
- **Masks from the selection** (`AddMask(revealing)`): with a selection, a mask on the layer's own pixel grid (the
  loops mapped through the layer transform and rasterized with the selection's antialiasing): revealing hides the
  selected area (`255 − coverage`), otherwise shows only it; the selection is dropped and the mask targeted, one
  `Add Mask from Selection` entry. Without a selection, `AddLayerMask` (`Add Reveal-All Mask` / `Add Hide-All Mask`).
  Nothing on a layer that already has a mask, or during a transform.
- **Floating selection transform** (`TransformEdit::floating`, `FloatingTransform`): `CanTransformSelection` needs a
  non-empty selection, a paintable image layer (not its mask) and no pending edit; `TransformCommand` (Ctrl+T)
  starts it, else `BeginTransform`. `BeginSelectionTransform` opens a `Transform Selection` entry, clears the
  selected pixels and inserts a `Floating Selection` layer above the source (the selected pixels at their place,
  the source's opacity and blend mode) as the pending persistent edit. `CommitTransform` with an unchanged draft and
  no distortion restores the document exactly (no entry, so soft selection edges never pick up a seam); otherwise it
  merges the floating pixels into the source (bilinear through the draft, a distortion warped first), growing the
  layer and a mask on its grid where they extend past it (a uniform mask stays), moves the outline with them,
  removes the floating layer and reactivates the source, one entry. `CancelTransform` (Escape, Undo) restores the
  document and the active layer.
- **Clipboard** (`PixelClipboard`: the pixels and their document origin): `SelectionCopyRegion` is the selection's
  bounds (the canvas without one) rounded with a 0.001 tolerance and clipped to the canvas; `CanCopyPixels` needs
  an image layer or a targeted mask and no explicitly empty selection; `RenderSelectedPixels(layer, mask)` crops the
  layer as shown (a mask as opaque gray) to the region through the selection coverage; `CanCopyMerged` needs a
  layer with pixels and `RenderMergedPixels(composite)` crops the host's document-size composite the same way.
  `CopySelection` and `CopyMergedSelection(composite)` keep the copy on the session (`Clipboard`); `CutSelection`
  copies then clears; `CanPaste` / `Paste` insert the copy in place as a new layer (`Layer N`; `Paste`; the selection
  dropped); `PasteImage(image)` centers another app's pixels; `LayerViaCopy` copies the selection to a new layer
  without the clipboard (`Layer via Copy`), or `DuplicateActiveLayer` without one. `NextLayerName` is the first
  unused `Layer N`.

## Validation

`DocumentTests`: geometry (`transform.*`)
, drag modes (`drag.*`), snapping (`snap.*`), the session lifecycle
(preview keeps the document, commit/undo/redo, no-op commit, invalid previews, select-commits, undo-cancels-pending,
nudge), duplicate (`duplicate.*`), group (`group.*`), masks (`mask.*`), flips (`flip.*`) and the warp
(`distort.*`), each ported from the legacy `TransformTests`, `TransformPressTests`, `MaskTransformTests` and
`DistortTests`. `HostTests` proves the overlay geometry (rotated edges, corners, rotation knob, zoomed document
mapping, resize cursor directions, distortion midpoints). The WARP self-test drives a pointer drag, Free Transform
with a typed rotation, Apply, Escape, Shift+arrow and both flips through `Application`.

Selection: `DocumentTests` `geometry.*` (Clipper2 booleans, holes, offsets, ellipse, drag boxes), `coverage.*`
(exact rectangles, half-pixel edges, the top-left rule, the antialiased diagonal, off-canvas loops, rings),
`selection.*`, `move.*`, `modify.*`, `load.*`, `marquee.*` (the legacy `SelectionTests` cases) and `wand.*`,
`wandsession.*` (the legacy `MagicWandTests` cases plus the layer sampler). The WARP self-test drives a Marquee
drag, a Shift add, the M key, an outline move, a deselecting click, a wand click with exact bounds, Ctrl+Shift+I,
Ctrl+A, Ctrl+D, a polygonal lasso closed with Enter, Expand from the header and Escape through `Application`.

Brush: `DocumentTests` `sparse.*`, `kernel.*`, `brush.*`, the brush `mask.*` cases, `opacity.*`, `accumulate.*`,
`ripple.*`, `curve.*`, `tail.*`, `crossing.*`, `snapshot.*`, `ridges.*`, `bounds.*`, `budget.*`, `reject.*`,
`line.*`, `keys.*`, `expand.*` and `clip.*` (the legacy `BrushStrokeTests`, `BrushIntersectionTests`,
`RasterSnapshotTests` and mask painting cases), listed in [`Rendering_Brush.md`](../Rendering/Rendering_Brush.md).
The WARP self-test paints, erases, keys and cancels a stroke through `Application`.

Retouch: `DocumentTests` `heal.*` (the legacy `SpotHealingTests` blemish for every mode), `clone.*` and `tips.*`
(`CloneStampTests`), `blur.*`, `smudge.*`, `liquify.*` and `warp.*` (own goldens: a softened edge, carried color, a
pushed edge, changed tiles, cancel), `gradient.*` (`GradientTests`), `shape.*` (`ShapeToolTests` plus the redraw on
scaling) and `eyedropper.*`. The WARP self-test drives each tool through `Application`.

Pixel edits: `DocumentTests` `edit.*` (the legacy `SelectionEditTests`: brush and gradient clipped to the selection,
an empty selection edits nothing, fills, Clear and the Delete key, mask fills, the clip on scaled layers, masks from
the selection on the layer grid, mask deletion, invert keeping transparency, inside the selection, on masks, on a
uniform mask with a selection and after a pending gradient), `pixelmove.*` (moves and duplicates as one entry with
the outline, nudges, masks refusing, the outline never at its old spot, the preview tiles), `clipboard.*` (the
legacy `SelectionClipboardTests`: copy and paste in place, cut, layer via copy, a lasso-shaped copy, copy merged
with a hand-built composite, an external paste centered) and `floating.*` (Ctrl+T on a selection as one entry with
the outline following the handles, Escape restoring exactly, an unchanged transform leaving soft edges untouched,
growth past the layer edge, Undo dropping the edit). The WARP self-test drives the Edit menu, the clipboard keys, a
Ctrl-drag through the live preview, Ctrl+Right, Delete, Ctrl+T with Escape and Enter, Ctrl+J, Add Mask and
Ctrl+Shift+C through `Application`.
