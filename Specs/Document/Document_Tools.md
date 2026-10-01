# Document tools: transform (C8), selection (C9), brush (C10) and retouch (C11)

Status: current normative contract (created with C8; later tool phases extend it)
Last reviewed: 2026-09-30
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

## Asynchronous tool ordering foundations

`Common/PreviewMailbox.h`, `ContinuationQueue.h` and `HistoryWork.h` specify UI-owned state machines. Their actual
tool-command migrations are P1 work; the existing direct session operations retain their synchronous contracts.

- A preview mailbox has one launched interactive request and one latest pending request. Pending changes never
  revoke the running request's right to publish. A finish releases the next request; failure releases it too.
  Close, preview-off and reopening invalidate old epochs. Every apply also validates the owning document token.
  Refinement may start once per settled successful sequence and is invalidated by any newer settings.
  This preserves `Levels.swift`/`Filters.swift` coalescing and `stalePreviewCannotReturnAfterOffOrReopen`.
- An eight-entry continuation ring owns parameters, not document snapshots. A full ring refuses a command while
  preserving caller ownership. A multi-file drop is one entry. Consecutive New wand clicks on the same document
  coalesce, even when full; Add/Subtract/Intersect do not. An ordinal gap (for example an interleaved stroke) prevents
  coalescing. A replaced-document entry is dropped and counted for a notice. Imports targeting an opening document
  must be bound to that open intent when P1 wires file flows, rather than accidentally dropped on replacement.
- `HistoryWork` issues shared monotonic input ordinals for commands and strokes and admits one history operation
  across all scheduler classes. Busy lasts from Begin through the UI apply/drop transaction. The host picks the
  oldest input before Begin and takes immutable pixels, selection and its job token only afterwards. Worker completion
  alone does not release the barrier. Old or cross-owner completion stamps cannot release newer work.
  A selected request that needs another history operation in the same document can Continue only from its exact
  latest finished stamp before a younger input starts. Every acquisition has a fresh generation, even for the
  same ordinal and kind; a late first-stage completion cannot finish the second stage. Begin still refuses reused
  ordinals. The host validates prerequisite success and document lifetime before Continue; the ordering primitive
  does not turn a failed or cancelled result into permission to execute dependent work.
- A stroke recording begins immediately even while history is busy. On pointer-up its `StrokeReplay` becomes Pending;
  it can launch only after earlier input and history work apply/drop. It snapshots the current surviving document,
  whether its predecessor succeeded, failed or was cancelled. The recording retains tool settings, target, samples
  and random seed through apply. Escape can cancel recording before pointer-up; it cannot cancel a released stroke.
  Escape cancels Commit/Selection and clears command continuations. A stroke's own failure retains its input in a
  Failed state for a reported, explicit retry with a new ordinal, never an automatic idle retry loop. A changed
  document does not consume the recording; replacement must settle or explicitly resolve it first.

The state machines do not impose a stroke-count cap or own pixel/sample allocations. P1 supplies recording storage
and replay; memory admission accounts for it. Scheduler capacity pressure must leave accepted input retained. HostTests
use literal state/ownership expectations. DocumentTests combine these states with direct session operations and
independent pixel/history expectations before the later worker integration tests.

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
`TransformTargetsMask`), or the upright box around the group's members. Nothing transforms while a color or filter
panel is open (`HasColorEdit`): the panel's edit is anchored to the layer's placement, so moves, Free Transform,
nudges, the Alt-drag duplicate and both flips wait for OK or Cancel (legacy `canEditLayers`).

`PreviewTransform(value)` changes only the draft (invalid values are ignored); the document, history and
`ActiveLayer()->transform` do not change while previewing. Rendering reads `DisplayDocument()`: the real document
when no edit is pending, else a cached copy sharing every raster where each moved layer carries
`PendingTransform` (the draft, or for a group each original `Following` the box) and `DisplayedMaskPlacement` (a
linked placement follows; an unlinked mask stays where it was; a uniform 1 × 1 mask never needs a placement).
The copy is rebuilt after every document edit (each `EndEdit`), so an opacity, blend mode, visibility, order, deletion
or flip made while a transform or a panel preview is pending shows at once. `EditedTransform(layer)` is where the
handles sit. `TransformPixelSize()` is what 100 % scale draws 1:1.

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
A linked mask without a placement is warped and cropped with the pixels; a uniform mask passes through. A linked
mask placed apart takes the same perspective over its own bounds: its placement is carried with the layer
(`Following`), its corners through the distortion (`Carried`), and it is warped into them with its edge tone outside
(`WarpMask`). An unlinked mask keeps its place on the document: its placement becomes the layer's transform before
the distortion. The preview does the same at the bounded size (legacy `Distort.distort` and `distortPreview`). The
warp is
the perspective map of the unit square onto the corners (`MakeHomography`), sampled bilinearly (nearest when the
layer's sampling is Nearest) from the source with the layer's flips honored, edges antialiased by four sub-samples.
Group distortions carry each member's transform by the same perspective (`Carried`). A distorted mask edited alone
is warped over its shape with its edge background outside (`WarpMask`).

## Flips

`FlipLayers(horizontally)` mirrors the active layer about its own center, or the group's members about the box
center, as one `Flip Horizontal` / `Flip Vertical` entry; masks follow the link rule. `FlipCanvas(horizontally)`
mirrors every layer and placed mask across the canvas middle and reflects the selection outline, as one
`Flip Canvas Horizontal` / `Flip Canvas Vertical` entry. Both commit a pending transform and apply a pending gradient
first; `FlipCanvas` also drops the crop frame (legacy `flipCanvas`).

## Header flags

Lock ratio (default on), Auto-Select (default off) and Show Transform Controls (default on) are the host's header state
(`Application`), not `DocumentSession`'s; nothing here is serialized beyond the layer transforms already in `.prism`.

## Selection model (C9)

`DocumentSelection` is a set of closed loops in document pixels (`SelectionLoop`, a `std::vector<Vec2>`) filled by
the nonzero winding rule, plus `antialiased`. It lives on `CanvasDocument::selection`, so history covers it and
document equality includes it; it is never saved. `nullopt` means no selection. A selection whose loops enclose no
area (`IsEmpty`: no finite point, or zero-width or zero-height bounds) is an explicit empty selection that later
edits must treat as "touch nothing", never as "touch everything". Loops from the boolean operations are
Clipper2-normalized (outer loops and holes, no overlaps) and kept to a hundredth of a pixel; wand and mask traces are
pixel-edge loops, outer loops clockwise and holes counterclockwise in top-left coordinates, which the nonzero rule
fills exactly.

Published loops use immutable shared storage (`SelectionLoops`,
[`Document_Model.md`](Document_Model.md) › Immutable selection snapshots). Copies for previews and history share
the points; geometry edits publish a replacement. This changes ownership only: winding, antialiasing, point
values, emptiness and selection equality retain the same legacy semantics.

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
on) and `wandSettings`; the Expand and Contract amounts (1–500, `kSelectionModifyLimit`, default 1) are the host's
header state. `CanEditSelection` needs a document and no pending transform (commands settle the transform first).

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
  `EndSelectionMove` closes the entry; `CancelSelectionMove` (Escape) puts the outline back where the move began and
  closes the entry unrecorded; `NudgeSelection` is one entry per press.
- `CanModifySelection`: a non-empty selection and no draft. `ExpandSelection(amount)` offsets outward and clips to
  the canvas (`Expand Selection`); `ContractSelection(amount)` offsets inward, away from the canvas edges too, and
  can leave an empty selection (`Contract Selection`). Amounts outside 1–500 are ignored.
- `SelectAll` (`Select All`, antialiased), `Deselect` (`Deselect`; nothing without a selection), `InvertSelection`
  (`Inverse`, canvas minus the selection).
- `LoadMaskSelection(id, mode)`: the mask's pixels darker than 50 % (`MaskTracing::DarkPixels`), mapped through the
  mask's placement (`PixelToDocument(MaskTransform)`), combined by `mode` (`Load Mask Selection`); false when the
  layer has no mask or no dark pixel. `LoadLayerSelection(id, mode)`: the layer's pixels at least 50 % opaque,
  ignoring its mask, mapped through the layer transform, flips included (`Load Layer Selection`); false for a
  folder, a blank layer or an image with no opaque pixel. Both refuse an outline past 8 000 000 pixel edges (the
  wand's limit) or one that cannot be allocated: false with `failure` TooDetailed or Memory and the selection
  unchanged, so the host can say so instead of silently loading nothing (`failure` stays None for nothing to trace).
- Undo and Redo drop a draft in progress; during an outline move they put the outline back as `CancelSelectionMove`
  does and history does not move (RF-904 of the 2026-09-23 review: the move holds its entry open, so history could
  not move before either). `FlipCanvas` mirrors the loops with the layers.

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
active layer's own pixels at its displayed transform without its mask (`RenderLayerPixels`: nearest when the
layer's sampling is Nearest, else one bilinear tap with edges clamped inside the placed box — `RenderPlacedImage`,
the same sampler `CanvasSize` places a layer with, collapsing High to Smooth since this is a transient read, not a
resample; transparent outside the box or for an invalid transform); a folder or blank layer reads as transparent,
so the whole canvas matches. Nothing matched deselects in New mode. A New selection takes the traced loops without
the canvas clip (they already lie on it); Add and Subtract go through `ApplySelection`. One `Magic Wand` entry.

## Brush session (C10)

The stroke model, the coverage engines, the commit and the live preview live in
[`Rendering_Brush.md`](../Rendering/Rendering_Brush.md); this is the session's contract. Brush state on
`DocumentSession`: `brushSettings` (`BrushSettings`: diameter 1–2000 px, default 40; hardness 0–1, default 1;
straight red/green/blue 0–1, default black; opacity 0.01–1, default 1; `IsValid` gates every field). The red,
green and blue channels are the foreground color: `SetForegroundColor`, `SwapPaletteColors` and `ResetPaletteColors`
write them,
and a stroke copies them when it begins (a mask still paints black or white). `brushMode`
(Paint default, Erase: the Brush tool's own erase mode) and `maskPaintWhite` (default false: a mask is painted
black, hiding). A targeted mask has its own palette (legacy `ColorPalette.swift`): with the mask selected,
`SwapPaletteColors` flips `maskPaintWhite` and `ResetPaletteColors` sets it false, the RGB colors untouched; either
re-renders a pending mask gradient, as the host does when the brush header's Paint choice changes. `CanPaint` needs
a document, no pending transform, no stroke in progress, an active layer that is visible with its ancestors and is
not an adjustment layer (a folder only through its mask), an enabled mask when the mask is selected, and no
explicitly empty selection.

- `SetBrushEngine(engine)` chooses the coverage engine for later strokes (nullptr: the CPU kernel); the host binds
  its compute engine with the device and drops it with the device.
- `BeginBrush(point, erase)` starts a stroke on the active layer, or on its mask when the mask is selected (a mask is
  never erased: it is painted black or white per `maskPaintWhite`), clipped to the selection's coverage when one
  exists, with the layer's pixel budget (`RasterEditPixelLimit`); false when painting cannot start or the first
  sample already fails. `ContinueBrush(point)` appends a sample; a budget or engine failure drops the stroke
  (`PendingBrush` becomes null). `FinishBrush` settles the tail and records one entry (`Brush Stroke`, `Erase`,
  `Paint Mask`) unless nothing was painted or the layer's pixels or transform changed under the stroke;
  `TakeLastBrushCommit` hands the host that commit (raster and grid rectangle) once, for the preview's adoption.
  A commit keeps a painted sparse mask's own fill, so a hide-all mask painted any number of times still hides
  everything no stroke touched; a mask whose pixel size differs from its layer's (stretched over it) is resampled
  onto the layer's grid by the nearest pixel, as its tiles were read, the first time a stroke commits on it (a
  uniform 1 × 1 mask stays one value). When the layer grows, a mask on its grid is carried along (`CarriedMask`):
  the old area keeps its pixels and fill and only the new area reveals (255); a uniform mask stays uniform. The
  floating selection's merge and a filter's grown grid carry masks the same way (with the edge tone for filters).
  `CancelBrush` drops the stroke. The first Undo or Redo while a stroke, warp, pixel move or shape draft is pending
  drops that edit and does not move history (a crop frame is dropped and history still moves, see
  [`Document_History.md`](Document_History.md)); a committed stroke's Undo and Redo still share every untouched
  tile.
- `ShiftLineStart` is the last sample of the previous stroke on the same layer or mask, for Shift-click lines.
- `ChangeBrushSize(increase)` steps the diameter by a fifth, at least one pixel, within 1–2000;
  `ChangeBrushHardness(increase)` snaps to the next quarter; `TypeOpacityDigit(digit, seconds)` is Photoshop's
  opacity typing (1–9 set 10–90 %, 0 sets 100 %, a second digit within 0.6 s makes a two-digit percent, never below
  1 %). All three ignore keys while a stroke is in progress. `SetOpacity` (the layer's, one `Layer Opacity` entry)
  refuses while a panel owns the document (`HasColorEdit`), as `StepBlendMode` does: the panel's entry stays open
  until OK or Cancel, and an opacity would fold into it (legacy `canEditAppearance`).
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
  layer (or its mask, with its edge tone beyond its pixels; `RenderMaskPixels` samples a placed mask the same way
  `RenderLayerPixels` samples a layer, nearest included) softened by sigma = diameter / 10 clamped to 1.5–30,
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
  `Gradient` / `Gradient Mask`; `ResolveGradient` commits an inline pending edit and returns false for an
  unresolved deferred host opening, without mutating document/history. It runs on every layer or target switch (a
  new layer, folder, shape, paste, duplicate or mask, a selected, moved, grouped, merged or deleted layer, a deleted
  mask) and before every edit that reads or changes the target's pixels or placement (brush, warp, fill, Clear, Cut,
  Copy, Layer via Copy, pixel moves and nudges, the floating selection, transforms, both flips, Crop, the color
  panels, Invert), so a pending gradient is never dropped under another edit; a line started on another target
  applies the pending one first. Decision D3 of the 2026-09-23 review: legacy refuses those edits while a gradient
  is pending (`canEditLayers`); RedPrism applies the gradient, as its Invert already did. `CancelGradient` and the
  first Undo drop it. The fill covers the canvas (before the
  start and past the end take the end colors; radial is centered on the start with the end on its rim) within the
  selection, composited over the original pixels at the opacity. On a mask the ends come from the mask palette
  (legacy `paletteColor`): the foreground is black, or white with `maskPaintWhite`, the background the other one,
  whatever the RGB palette holds (`GradientColors` follows the pending gradient's target, else the current one).
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
  document pixel (nullopt outside or over transparency); the host shows the comparison ring while View › Sample Ring
  (the `sampleRing` setting, [`Core_Settings.md`](../Core/Core_Settings.md), default on) is checked.
- `AddPixelLayer(image, origin, name, editName, dropsSelection, shape)` inserts pixels as a layer above the active
  one (inside its folder) in one entry, the base for shapes and pastes.

## Pixel edits (C9b)

The selection edits of the legacy `SelectionEdits.swift`, `FloatingSelection.swift` and `SelectionClipboard.swift`:
raster edits built on the brush stroke's canvas passes and lifted pixels
([`Rendering_Brush.md`](../Rendering/Rendering_Brush.md)), each one history entry. Synchronous session callers use
`CommitStroke` (which can apply a moved outline inside the same entry); production basic edits use the detached
preparation and owner publication below. `CanEditPixels` is `CanPaint`: a paintable active layer or targeted
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
### Detached basic raster edits (P1.5)

Production Fill, Clear, Invert and Delete with a selection retain the selected host input ordinal through one
Commit job. Target acquisition follows earlier accepted history and color-panel settlement. The complete peak
reservation precedes capture of the layer, selection, palette, mask target, canvas dimensions and pixel budget.
The admitted record reaches its owner capture hook and resumes without another reservation or FIFO entry.

`RunRasterEdit` reads only owned metadata and immutable raster/selection handles. Its private CPU path preserves
the `PaintCanvas` formulas, transformed document-center clipping and legacy history names above. It does not
borrow a session or graphics engine. Coverage, tile bases/results, sparse replacement windows, stretched/carried
masks and eager semantic mask thumbnails are prepared on the worker. Sparse inputs MUST remain unassembled.
Cancellation is checked between coverage rows/edges, source rows, edit rows/tiles, sparse-window splits and result
stages. A partial/failed/canceled preparation returns no layer. Pixel-limit refusal is `TooLarge`, invalid input
is `Invalid`, allocation refusal is `Memory`, and no painted area is `Unchanged`; none adds a history entry.

`ApplyRasterEditIfCurrent` validates session/document/restore identities, whole mutation generation, retained
target image/transform/full mask, selection identity, canvas dimensions and active image/mask target. It rejects
unsettled edits and unrelated layer metadata changes. It validates independent image/mask budgets and moves the
complete prepared target inside one named transaction. It MUST NOT rasterize coverage, grow masks, compute
thumbnails or run any pixel kernel. Refusal retains the caller's candidate so its aggregate owner can retire it
off-thread. A layer-grid nonuniform mask grows atomically with its image; preparation failure fails the edit.

The host suppresses even a Finished result after cancellation of its barrier. A stale completion records no
entry and does not overwrite newer content. Input/barrier/reservation ownership persists through acknowledgement.
Workers catch allocation failure only through the shared runner; capture/admission `noexcept` boundaries fail
the whole request. Failure displays one localized pixel-edit/size notice. No-selection Delete remains the layer
or mask metadata operation, and Clear without selection changes nothing.

Pending distortion settlement still uses existing synchronous prerequisites until A6 in the
active raster-edit plan. History, chrome refresh and display upload remain measured P1.9/G1 costs. This contract
does not claim that these remaining callers or all P1 timing targets are qualified.

### Detached gradient previews and settlement (P1.5)

Production pointer input uses `BeginDeferredGradient`. Opening identity, target/mask, endpoints and monotonically
increasing settings sequence are metadata only. Move, palette and header changes do not allocate pixels or
coverage, and valid mouse-up leaves the line pending. A redrag with no valid line clears the old raster; an older
worker cannot republish it while there is no line. Direct `BeginGradient`/Commit APIs retain the inline numeric
oracle behavior. A Common operation that needs settlement MUST refuse an unresolved deferred opening before any
preceding transform/history mutation; its Application caller supplies ordered settlement.

Each opening has one active InteractivePreview and one latest pending primitive request. Event-time endpoints,
straight endpoint colors, opacity and linear/radial shape are copied when offered. The complete reservation
precedes owning layer/selection/token capture; the same admitted record resumes after capture. Workers use the
existing exact CPU gradient math and private raster path, including selected/placed masks. No session, UI, GPU
engine or published stroke is borrowed. Progress/context/cancellation borrows end before publishing a stroke.
The stroke and its source/tile/coverage graph retire together through the release sink.

An active result may show before the latest pending settings render, preventing continuous dragging from
starving all previews. Publication validates mailbox epoch, opening, generation, source token, target/mask and
strictly increasing published metadata sequence. It refuses future, duplicate/backward or invalidated results
without consuming the caller's candidate. Opening/close/cancel/commit invalidation requests cancellation, retaining
the old ticket until acknowledgement. Launch after completion is rearmed through the event-backed owner source
after the old record's reservation retires; no second full reservation is acquired inside completion.

Enter/header Apply is an opening-owned input. Gradient settlement follows color settlement before dependent
commands, layer/tool switches, size, readback or writes. It retains the selected ordinal with a fresh acquisition
and a complete Commit reservation. Only an exact current metadata sequence and whole source/session token can
apply one `Gradient`/`Gradient Mask` entry; cancellation/failure/staleness stops the dependent action. Valid
Unchanged drops the exact pending line with no entry. Undo/Redo drop an uncommitted line; Swap/Reset Palette only
refresh it. Confirmed discard skips settlement intentionally; Save before discard settles before snapshot/write.

Production mutation/palette callbacks cannot change a frozen commit. Forced model changes invalidate it. The
owner may adopt an already uploaded gradient preview only when its published settings and source generation
match that commit and it belongs to the same target; the worker supplies the crop coordinates. Otherwise the
result uploads through the normal sparse path. Numeric GPU equality and existing adoption counts remain gates.
First preview upload, capture/history/chrome costs and full-loop drag latency remain P1.9/G1; SIMD/GPU gradient
shading remain P2/P3. Worker migration alone establishes no one-frame latency or Photoshop speed claim.

### Other selection pixel edits

- **Pixel moves** (`PixelMove`: the lifted raster, the outline it started from, `duplicate`, `offset`):
  `BeginPixelMove(duplicate)` cuts the selected image pixels out of the layer through the coverage (false without a
  non-empty selection, with a mask targeted, or over no pixels); `MovePixels(dx, dy)` previews them a whole-pixel
  offset away (the hole stays unless duplicating; the raster's tiles feed the host's stroke preview): an update that
  rounds to the offset already shown rebuilds nothing, and a new offset rebuilds only the tiles the pixels landed on
  last time and land on now (the hole's tiles once), each from its original content;
  `FinishPixelMove` commits the raster and the moved outline as one `Move Pixels` / `Duplicate Pixels` entry (a zero
  offset commits nothing); `CancelPixelMove` drops it; `NudgePixels(dx, dy)` is one entry per press.
  `DisplayedSelection` is the outline to draw: during a move the original shifted by the offset; while transforming
  selected pixels, the outline mapped through the handles (`FloatingSelectionMap`) or the distortion's homography.
  Detached preparation is also available through `RasterEditKind::PixelMove`: copied target/selection/canvas,
  duplicate intent and document offset enter `RunRasterEdit` or `RunRasterEditPreview`. It uses a private CPU
  stroke, cancellable coverage/source reads/lift rows/landing tiles and source-over samples, with no shared sparse
  materialization. Offsets round half away from zero in document space. Whole-pixel landing uses exact reads;
  stretched/rotated fractional landing uses the same bilinear formula as the direct API. The complete result
  owns the prepared target, crop metadata and translated outline; zero rounded offset or no overlap of selection
  coverage with the source grid is `Unchanged`. Transparent pixels inside that grid retain the direct move behavior.
  Nonfinite/unrepresentable mapping, pixel-budget refusal, failed allocation or cancellation
  publishes no target/outline. `PixelMoveMemoryBytes` includes lifted pixels, landing/hole keys, all existing
  raster-edit planes/windows/masks and immutable moved geometry. Borrowed progress state is cleared before a
  preview escapes. Direct session APIs remain inline references. Production openings use `BeginDeferredPixelMove`:
  target, original immutable selection, duplicate intent, opening generation and rounded offset/sequence are
  metadata only. One active/latest pending preview privately prepares pixels under whole-graph admission;
  publication checks the opening generation, source token and increasing completed sequence. Returning to zero
  offset invalidates the mailbox and drops the preview. An invalidated opening cannot acquire a newer source.
  Pointer release owns `HostPixelMoveApplyInput`; Ctrl-arrow owns its event-time document offset, including Shift.
  Both prepare and apply under the selected history ordinal after prior color/gradient settlement, with the
  exact opening/settings acquisition. Owner application moves the prepared image/mask/outline in one named edit.
  Refusal retains caller ownership; zero/no-overlap leaves redo intact. Early/late cancellation keeps the ticket,
  admission and history barrier until acknowledgement. Terminal commit failure drops only that opening's gesture
  without changing its document; a newer opening survives. Tool/history termination keeps the existing drop rule.
  Copy reads the unchanged original document during a move, as its direct reference does. Opening lift/coverage
  reuse, displayed geometry caching, bounded history/upload costs and real-loop measurement remain open gates;
  this functional migration alone is no whole-host nonblocking or performance claim.
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
  copies then clears, and when the clear does not paint (`CanPaint` is false, or the clear allocates nothing) the
  clipboard is left as it was and the pixels stay (the clear and `FillSelection` report whether they recorded their
  entry, so a full history, which trims one entry as it adds one, never reads as a refusal); `CanPaste` / `Paste`
  insert the copy in place as a new layer (`Layer N`; `Paste`; the selection dropped); `PasteImage(image)` centers
  another app's pixels; `LayerViaCopy` copies the selection to a new layer without the clipboard (`Layer via Copy`),
  or `DuplicateActiveLayer` without one. `NextLayerName` is the first unused `Layer N`. `BeginSelectionTransform`
  restores the document and records nothing when `ClearSelectedPixels` does not paint.

## Color adjustments and filters (C12)

The legacy `Levels.swift`, `HueSaturation.swift`, `Curves.swift`, `ImageAdjustments.swift`, `Filters.swift` and
`AdjustmentEditing.swift`: the Image menu's Curves, Levels, Hue/Saturation, Exposure, Gradient Map and Grain and
the Filter menu's Gaussian Blur, Motion Blur, Add Noise and Lens Correction run on the active image layer, inside
the selection when there is one, with a live preview and one entry on OK; adjustment layers keep the same settings
live and edit through the same panels. The kernels live in `Common/Adjust.*` (the legacy `LevelsPixels.c`,
`AdjustPixels.c`, `NoisePixels.c`, `LensPixels.c` number for number on premultiplied BGRA, the Hue/Saturation
cube of `HueSaturationFilter`, the exposure table, Hermite curves), the panels in
[`UI_ApplicationChrome.md`](../UI/UI_ApplicationChrome.md), the composition's live application in
[`Rendering_Composition.md`](../Rendering/Rendering_Composition.md).

- `CanAdjustColors`: a document, no open panel, no stroke, warp or pixel move, a visible image layer (not a group,
  not its mask) with pixels and no explicitly empty selection. While a panel is open (`HasColorEdit`) painting,
  transforms, nudges, flips, Undo and Redo wait; a new panel, a tool switch or another layer's panel applies the
  open one first (the host).
- **One panel session** (`ColorEdit`, `PendingColorEdit`, `HasColorEdit(kind)`) runs every kind (`FilterKind`: the
  Filter menu's filters and the Image menu's Curves, Exposure, Gradient Map, Grain, Levels and Hue/Saturation) with
  one settings record (`FilterSettings`; each kind reads its own) and one rule for all of them.
  `BeginColorEdit(kind)` commits a pending transform, applies a pending gradient and drops a lasso draft, then reads
  the layer's pixels, placement and selection and starts from the last applied settings (`filterSettings`; Levels
  and Hue/Saturation at identity, a Gradient Map from the foreground to the background color); Add Noise and Grain
  draw a pattern seed of their own (the session's heal seed, which no other kind advances).
  `UpdateColorEdit(settings, preview)` (`UpdateLevels` and `UpdateHueSaturation` change their kind's part) renders
  inline for direct callers, or captures metadata for a deferred caller, using a copy no larger than 4096 px on the longest side for the table kinds (Levels,
  Hue/Saturation, Curves, Exposure, Gradient Map), 2048 for blurs and the lens, full size for noise, grain and the
  fill (a smaller copy would coarsen them), blended through the selection coverage on that grid; `DisplayDocument`
  shows it in place of the layer (a mask on the layer's grid keeps its place over a grown grid). An edit that
  changes nothing (identity Levels or Hue/Saturation, lens 0, the default exposure, grain 0) previews the layer's
  own pixels and closes without an entry on OK; a Curves panel on its identity curve still records its entry, as in
  the legacy. `CommitColorEdit` uses the immutable work graph below and records the kind's name (`FilterKindName`)
  only while its opening inputs remain valid; a stale current panel closes without an entry.
  `CancelColorEdit` drops it. RF-801 of the 2026-09-23 review: legacy `beginHueSaturation` keeps a lasso draft and
  `commitHueSaturation` checks only the pixels, applying wherever the layer has gone; every kind follows the legacy
  Levels and filter rule instead (nothing transforms while a panel is open, so the placement check guards changes
  the host did not settle first, such as a resize).
- **Filters** (`FilterSettings` normalized: radius 0.1–250, angle −90–90, distance 1–2000, amount 0.1–400,
  distortion −100–100, the curves, exposure, gradient map and grain settings): a blur pads the layer out by its
  reach (3 × radius + 2, or half the streak + 2, growing again when raised) so it spreads instead of stopping at the
  border, and OK trims it back to its pixels (`TrimmedToAlpha`), carrying a mask onto the new grid with its edge
  tone beyond. `Adjust::RunFilter` is the kernel for every kind: the Image menu's kinds run their adjustment layer's
  kernel (`ApplyAdjustment`; Grain in layer pixels at the panel's seed); Gaussian Blur is
  `ImageFilters::GaussianBlur` (sigma = radius), Motion Blur an even streak of the distance along the angle
  (counterclockwise from horizontal on screen) sampled up to 96 times, Add Noise the legacy hash noise (uniform
  ±amount % of half the range, Gaussian at two thirds, monochromatic one value per pixel) with a seed fixed while
  the panel is open, Lens Correction the radial warp (k = distortion / 100 × 0.35).
- **Levels**: opening builds the alpha- and selection-weighted histogram of the original (`ColorEdit::histogram`,
  `Adjust::Histogram`; RGB is the mean of the channels); `UpdateLevels` previews the tables; `AutoLevels`
  (Contrast: one shared interval from the 0.1 % clipped ends; Color: per channel; Neutral: per channel with the
  gamma that centers the mean); the eyedroppers `SetLevelsSampleMode` / `SampleLevels(point)` read the original
  layer's straight color under a document point (transparent pixels are ignored) and calibrate black, gray or
  white; OK records `Levels`. `LevelRange` normalizes to black 0–254, white black + 1…255, gamma 0.1–9.99, outputs
  0–255; `LevelsApply` runs the channel range then the RGB range.
- **Hue/Saturation** (`HueSaturationSettings`: Master and six ranges with their bands, `colorize`, `invertRange`):
  `UpdateHueSaturation` previews through the cube; the eyedroppers (`SetHueSampleMode`,
  `SampleHueRange(composite, point)`) re-center, widen or narrow the selected range's band on the hue of the
  visible composite (near-neutral pixels have none; Master and Colorize refuse); the targeted adjustment
  (`SetHueTargeting`, `BeginHueTargeting` picks the range claiming the sampled hue most,
  `DragHueTargeting(viewDelta, adjustsHue)` changes its saturation, or hue with Ctrl, by half a unit per view point
  measured from the start; `EndHueTargeting`); Colorize starts at hue 0, saturation 25; OK records
  `Hue/Saturation`. Band math: `BandWeight` ramps through the shoulders and wraps, `BandCentered`, `BandInclude`,
  `BandExclude`, `BandSetHandle` (a crossing move is refused).
- **Adjustment layers**: `AddAdjustment(kind)` inserts a canvas-size layer above the active one with the kind's
  defaults (a Gradient Map from the palette, a Grain seed of its own), one `New <Kind> Adjustment` entry, and marks
  it for editing (`AdjustmentEditingID`); `UpdateAdjustment(id, value)` changes a valid record live (no entry).
  `BeginAdjustmentEditing(id, below)` opens the kind's panel (`AdjustmentFilterKind`; `FilterAdjustmentKind` maps
  back) over the composite beneath the layer (the host renders it with the layer and everything above hidden),
  starting from the record's settings (`Adjust::AdjustmentSettings`), inside one `Edit <Kind> Adjustment` entry:
  the panel's changes update the record (`PreviewAdjustmentEditing`, `Adjust::AdjustmentWithSettings`; Preview off
  shows the original), OK keeps the settings and Cancel restores them (`FinishAdjustmentEditing`). The source is
  only for the histogram and sampling and never replaces pixels.
- `StepBlendMode(forward)`: the active layer's blend mode one step forward or back through the thirteen modes,
  wrapping, one `Layer Blend Mode` entry (Shift+= and Shift+- in the host).

### Immutable destructive-panel work

`PrepareColorEditCommit` captures owning inputs for `RunColorEditJob` (`Common/ColorEditWork.*`), which has no
session, HWND, chrome or device access. `ApplyColorEditResult` runs on the session owner and executes no pixel
kernel. The direct `CommitColorEdit` API uses this same graph with an `InlineExecutor`; the production host still
runs it synchronously in this slice. Preview grids, histogram arithmetic and committed kernel arithmetic retain
their existing contracts above.

The opening token retains the image, placement, mask, selection, canvas size, document lifetime and restore epoch.
Every panel begin, settings/preview update and end advances a sequence. A result from another session, an older
sequence or a different panel MUST NOT alter or close the current panel. A matching result whose opening inputs
are stale closes that old panel without an entry. Unrelated metadata edits, such as a name or print resolution,
survive a valid targeted apply. A result still rechecks the current document's image and mask budgets before any
mutation, because another layer can consume budget without invalidating the target.

The worker renders full-size pixels, trims a blur, carries its opening mask and creates both thumbnails before
returning a ready result. The mask thumbnail is semantic and MUST be installed with the mask. Failure to carry it
or to allocate selection coverage MUST fail the entire operation. A blur whose requested margin exceeds the
successfully prepared grid MUST fail; it cannot render on the smaller grid after a growth refusal. An enabled,
successful preview may supply the committed pixels only when it was rendered with the current settings and seed
on the exact full source grid and placement. Preview off always computes the full result.

Ready results install the image, placement and mask in one named history transaction, then update the last-applied
settings. Processing/allocation failure and cancellation leave pixels, mask, history and last-applied settings
unchanged and keep the panel available for retry or Cancel. A processing failure sets `commitFailed`; the host
reports `IDS_ERROR_FILTER`, or the existing budget notice when appropriate. A fill without donor patches remains
the distinct `previewFailed` no-source state. Identity closes without an entry. The inline construction/computation
boundary translates `bad_alloc` to a failed operation; existing history allocation still follows the owning
history policy. The token is propagated into pixel, selection, copy, trim, carried-mask and thumbnail work. A
cancelled result never installs partial pixels. Missing donors, cancellation and allocation failure remain distinct;
`ContentFill` returns 0, -2 and -1 respectively. The direct in-place kernel may have written a prefix on cancellation,
so callers MUST discard its private output. `FilterSettings::Normalized` may allocate and must allow `bad_alloc`
to reach the owning computation boundary.

`ColorEditJobMemoryBytes` declares the complete commit graph's additional allocation peak before submission.
It accounts for rounded pixel buffers, private sparse reads, coverage, kernel scratch, blending, crop, masks,
thumbnails and metadata. An exact resident preview avoids another filter/coverage reservation. Geometry producers
cache selection point counts; querying the estimate does not traverse selection points or loops. The host submits
this graph through the ordered commit engine and selected continuation described below.

### Deferred panel computation (P1.1c.1)

`BeginColorEdit(kind, true)` and `BeginAdjustmentEditing(id, below, true)` select deferred computation for that
opening. Once preceding interactions have settled, begin/update capture owning inputs and compute growth metadata;
they do not create histogram, coverage, padded image, downscaled source or preview pixels. `grownGrid` records the
union of requested extents on the original image grid. A worker prepares its padding with cancellable private
reads. Growth beyond supported coordinates or the pixel budget is refused before integer conversion or allocation.
An unresolved growth refusal cannot commit pixels on the previous smaller grid.

`PrepareColorEditPreview` captures the opening and current settings sequence. `RunColorEditPreviewJob` prepares
the grid/source and renders the same arithmetic used by the direct API; `RunColorEditHistogramJob` reads the fixed
opening image and selection once. Both return explicit status and owning values. Missing coverage or private source
storage is a failure, never a successful empty histogram. `ColorEditPreviewMemoryBytes` and
`ColorEditHistogramMemoryBytes` reserve the maximum overlapping preparation/compute allocations, including padding,
the downscale read, coverage and sparse reads. `ColorEditJobMemoryBytes` also includes unprepared growth at commit.

`openingSequence` remains stable while settings change. Histogram results validate that lifetime and the opening
source, so a settings tick does not discard a valid measurement. Adjustment-layer histograms remain measurements
of their opening composite while that layer's own parameters change. Cancel/reopen, restore and document
replacement invalidate the old opening. Destructive results retain the existing target, placement, mask and
selection checks.

An intermediate preview may display without replacing the panel's current settings or growth plan. Its prepared
source enters the cache only for the same grid and placement. `previewSequence` names the settings/seed/grid
request actually rendered; commit reuse requires this exact current sequence plus matching source, scale, placement
and dimensions. Preview off, identity settings and refused growth invalidate all earlier preview results, including
ones arriving after preview is re-enabled. Failed/cancelled results never install partial pixels. A current result
with inconsistent settings, grid or output shape is rejected.

The default direct API runs these same worker functions inline and immediately applies them, preserving its numeric
goldens. The Application explicitly chooses deferred mode for destructive and adjustment-layer panels.

### Host preview and histogram jobs (P1.1c.2)

`ApplicationAdjustJobs.cpp` owns one stable preview mailbox. Settings ticks retain at most one running request and
one latest pending request; they do not cancel the running interactive preview on each tick. A completed
intermediate result may publish before the pending request starts. Pending launch may pick up a compatible
prepared source from that completion without changing its captured settings sequence. Each submitted job owns its
input, result, cancellation ticket and declared memory peak. Workers cannot access the Application or session.

Levels submits one Background histogram attempt per panel opening, independently of preview changes. Completion
publishes only for the same opening image and lifetime. It updates chrome without recompositing unchanged canvas
pixels. Failed admission/computation reports through deferred UI work and has no idle retry loop. Cancellation and
obsolete results cannot publish partial images or histograms.

Preview off, identity settings, refused growth, Cancel, reopening, document replacement and shutdown invalidate
the appropriate mailbox/tickets. Completions apply outside message/control dispatch and then invalidate the frame.
The host never waits for these jobs in a production command. Shutdown invalidates inputs before stopping and joining
the scheduler. An adjustment-layer panel uses worker-prepared lookup values for live parameters; its opening composite
readback remains the P1.3 migration.

### GPU adjustment previews (P1.2)

For Hue/Saturation, Levels, Curves, Exposure, Gradient Map and Grain, the Application requests immutable
`AdjustmentLookup` values from the preview worker instead of a filtered CPU raster. Destructive previews retain
their prepared source and placement and an optional Gray8 selection coverage on that source grid. Compatible
source/coverage handles are reused across settings ticks; only the lookup is rebuilt. The reservation includes
lookup construction, downscale preparation and any uncached selection coverage, without reserving a nonexistent
CPU filtered output. The direct inline API remains the scalar pixel reference.

`previewSequence` names either a CPU raster or a GPU lookup result. Only a complete CPU raster at the exact current
full-resolution grid can be reused for commit. A GPU preview always commits through the existing CPU worker graph.
An adjustment-layer job builds only lookup metadata; it does not filter its opening composite. While that job is
pending the composition retains the last completed lookup or the opening adjustment. Preview off immediately uses
the opening adjustment, even if a previously requested lookup completes. Cancel/reopen and target validation remain
the same epoch barriers. LUT/cube updates and all graphics ownership stay on the UI thread, outside worker bodies.

Destructive OK and dependent file/close requests use the ordered worker engine and owning continuations below.
Earlier tool settlement, GPU composite sampling, file IO and other P1 operations remain blocking paths until their
owning migration; this slice does not establish whole-application G1.

### Ordered commit engine (P1.1c.3)

A Shape pointer release or ordinary pointer leave during admitted history work captures its draft and foreground
color in the same bounded host queue, then removes the live gesture without writing history. Its layer is added
only after preceding work finishes, using those captured values. Lost-pointer cancellation still drops a draft.
Worker rasterization of this owned Shape action remains P1.5.

`ApplicationCommitJobs.cpp` implements the production commit engine. Normal OK and dependent write/discard
requests enter it through the owned continuation source.
The caller supplies the oldest accepted input's ordinal from the shared `HistoryWork`. Admission acquires that
barrier before capturing immutable work. `Pending` means this particular ordinal owns an admitted job; a
competing ordinal cannot attach itself to another request's completion.

The Commit job owns its input, result, cancellation ticket and full graph reservation. Its worker cannot access
the Application or session. The panel's mutation callbacks freeze through apply/drop; Cancel and view input remain
available. Preview publication closes before the commit can apply; the independent opening histogram may finish.
Construction/admission failure releases the barrier and fails the operation. A worker failure retains request
identity so completion marks the correct panel failed without installing default or partial pixels.

Completion distinguishes `Succeeded`, `Cancelled`, `Failed` and `Stale`. It validates the panel opening,
settings sequence, document lifetime and complete target before interpreting even `Unchanged` as success.
Validation, apply/drop and its history transaction precede release of the barrier. Cancel requests cancellation
and retains the barrier until the old completion drops, including a worker that already finished pixels.
No failure dialog runs inside completion; feedback uses deferred UI work. Pending, failed, cancelled or stale
settlement MUST NOT authorize Save/export/import/resize/replacement/close. Explicit panel Cancel retires queued
inputs; document replacement instead drops old-document inputs as stale. Whole-application responsiveness still
requires the remaining GPU, IO, tool and history migrations.

Adjustment-layer settlement uses the same ordinal/barrier but only commits live metadata. A missing target or
changed adjustment kind returns `Stale`, retains the panel for explicit resolution and records no engine entry.
It cannot authorize a dependent operation merely because the inline session API could close the panel.

## Content-aware fill (C13)

The legacy `ContentFill.swift` and `Rendering/ContentFill.c`: Edit › Content-Aware Fill… (Shift+Delete) synthesizes
the selected pixels of the active image layer from its unselected opaque pixels through the panel session
above (`FilterKind::ContentAwareFill`, no settings; Remove Background is not in v1, see the C13 plan's decision).
`CanContentAwareFill` is `CanAdjustColors` with a selection that has area. `BeginColorEdit(ContentAwareFill)` grows
the layer's grid to the selection's canvas bounds (`GrowFilterGrid`, whole layer pixels, the grown transform kept
centered as for a blur, which now grows through the same helper) so a selection reaching past the layer's edge
fills new pixels; the preview renders at full size (a smaller copy would change the synthesis) and shows through
`DisplayDocument` like any filter. `Adjust::ContentFill(bgra, width, height, stride, mask, maskStride)` is the C
kernel number for number: selected pixels (coverage nonzero) are targets, unselected pixels with alpha 255 are
known, donors are the known pixels whose whole patch (radius 2 when both sides are at least 5 px, else 0) is
known; targets bordering known pixels are queued first; each dequeued pixel scores 28 candidates — 4 offsets
propagated from its neighbours' choices and 24 random donors from the linear congruential generator `state ×
1664525 + 1013904223` seeded with `0x6d2b79f5` — by the mean squared difference over the known patch pixels of
all four channels, refines the best by random jitters of 64, 32 … 1, copies that donor's four bytes, becomes
known and queues its selected neighbours; a selected area touching only transparency restarts from the first
remaining target with random donors. The result blends through the selection coverage. No donor (nothing
unselected and opaque) sets `ColorEdit::previewFailed`: the panel says so, `CommitColorEdit` refuses and keeps the
edit open for Cancel, and the layer is untouched; a fill is never trimmed. OK records `Content-Aware Fill`.

## Canvas size, image size and crop (C16)

`Common/CanvasSize.*` ports `CanvasSize.swift`, `CanvasResizer.swift` and `ImageResizer.swift`.

- `CanvasSizeDraft` (the Canvas Size card's numbers): `Displayed(axis)` shows the pixels (minus the original when
  `relative`) in the unit — pixels, percent of the original, inches or centimeters at the document resolution;
  `Set(value, axis)` converts back, adds the original when relative, and moves the other axis by the original
  ratio when `locked`; `Valid` needs both rounded sides in 1…30 000.
- `CanvasSizeOptions::Offset(from)`: `contentOffset` when given (the crop), else `floor((new − old) · (anchor % 3)
  / 2)` horizontally and `floor((new − old) · (anchor / 3) / 2)` vertically for the row-major anchor 0…8: floor
  puts the extra pixel right/bottom when expanding and takes it from the left/top when shrinking around the center.
- `ResizeCanvas`: sides 1…30 000 and anchor 0…8 (else `TooLarge`), a finite offset within ±1 000 000 (else
  `Invalid`); the same size with no offset is `Unchanged`. Every layer's origin and every placed mask move by the
  offset (an invalid transform is `TooLarge`) and the selection outline is dropped. A `fill` while growing adds a
  bottom layer `Canvas Extension` of the new size, opaque in the color with the old canvas cut out, so the old
  transparency stays; it counts against the 100-megapixel and 10 000-layer limits (`TooLarge`) and needs finite
  0…1 components (`Invalid`). Shrinking never adds a fill; a transparent resize allocates nothing.
- `ImageSizeDraft` (the Image Size card's numbers): with `resample`, `Set` converts the unit to pixels and moves
  the other axis by the current ratio when `locked`; without it the pixels stay and the resolution follows the
  typed print size (`pixels / value`, centimeters through 2.54). `SetResolution` scales the pixels with it in
  inches or centimeters while resampling; `SetResample(false)` restores the original pixels, locks the ratio and
  turns a pixel or percent unit into inches; `Valid` needs a finite resolution 1…9 600, rounded sides 1…30 000
  and, when resampling, at most 100 megapixels.
- `ResizeImage`: sides 1…30 000 and resolution 1…9 600 (else `TooLarge`); the same size changes only the
  resolution; otherwise at most 100 megapixels. Each layer rasterizes on its own at the document scale (`sx = new
  / old` per axis): the box its four transformed corners span after scaling, floored and ceiled to whole pixels,
  becomes its upright transform with the chosen sampling (rotation and flips baked into the pixels; a
  nonuniformly scaled rotated layer shears, which width/height/angle cannot hold). A pixel layer draws its image
  through pixel → document → scale → box (`RenderPlacedImage`: Nearest picks the nearest source pixel, Smooth
  samples bilinearly, High also averages up to a 4×4 grid per pixel when the map shrinks the image) into a new
  asset with a fresh thumbnail and stops being a live shape; masks on their own placement and 1×1 uniform masks
  keep their pixels with the placement scaled (`Placing` of the scaled unit map), other masks rasterize the same
  way with 0 beyond their pixels (`RenderPlacedMask`); folders and adjustment layers keep only the box. Layer
  pixels and mask pixels each stay within 100 megapixels (`TooLarge`); a failed allocation is `Memory`.
- `DocumentSession::ApplyDocumentSize(document, name)` installs a resized document (same id) as one entry named
  `Canvas Size`, `Image Size` or `Crop`, dropping any outline draft and keeping the active layer.

### Asynchronous size-card preparation (P1.8)

The application's Image Size and Canvas Size Apply actions submit immutable Commit jobs after all preceding
history work has applied. Options include the selected draft's dimensions, sampling, resolution, anchor and
captured extension color. Workers never read the card, palette, session, window or GPU. New Canvas and Crop retain
their existing paths. A size action that first commits a panel continues the same input ordinal with a new exact
acquisition stamp. A younger Undo or mutation starts only after the size result has applied or been dropped.

`ResizeCanvas`, `ResizeImage` and the placed rasterizers accept a cancellation token; direct callers use its empty
default. They preserve the existing sampling order, rounding and pixel goldens. Source sparse pixels are assembled
privately, without publishing `Raster::Data()` on a shared sparse input. Checks occur between layers, rows and
256-pixel spans; row initialization/clear checks can cover at most 120 KiB. Cancellation returns no partial raster
or resized document. These are structural bounds; allocation, page faults and destruction can delay acknowledgement.

`SizingMemoryBytes` declares metadata, all rounded output allocations and eager thumbnail allocations, plus the
largest private sparse-source assembly (renderers release it before starting the next layer or mask). Refused
late-layer bounds still reserve outputs already produced before the refusal. Metadata allows the immutable input,
detached document, apply validation and existing history copies. Resident source pixels and immutable selection
geometry are shared. An unrepresentable reservation or failed admission fails the whole action.

`ApplyDocumentSizeIfCurrent` validates the full session/document generation and restore token, same document id
and document graph immediately before the one named transaction. The host also validates the exact history stamp
and size-card invocation. Closing/cancelling a running operation requests cancellation and retains the request,
reservation and history barrier through worker acknowledgement and owner apply/drop. Reopening a card invalidates
the old result; its completion cannot close the newer card. A late Finished result after cancellation is dropped.
Only a successfully applied changed result enables Fit and invalidates document display. Unchanged, refusal,
failure and stale results do not add history. Worker failures use posted localized notices outside the completion
callback; TooLarge retains the distinct Too Large notice.

The Crop tool's model (`Common/Crop.*`, legacy `Crop.swift`) works on `CropRect` (x, y, width, height in document
pixels; the session holds the pending frame, `CropFrame`, and shows the whole canvas when there is none,
`VisibleCropRect`):

- `CropGeometry::Snapped` standardizes and rounds a rectangle to whole pixels with at least one pixel a side;
  `Valid` needs finite numbers, 1…30 000 a side and an origin within ±1 000 000; `Create(start, end, ratio,
  symmetric)` is the frame dragged from `start` to `end`, the shorter axis grown to the ratio (width / height)
  when one is given, and grown out from `start` as its center when symmetric.
- `CropDrag::Updated(point, ratio, symmetric)`: Create as above; Move offsets the original by the drag and snaps
  to whole pixels; Resize runs `TransformDrag` (mode Resize, the handle index, the original as an upright
  transform) with the ratio locked when one is given and Alt about the center, then snaps.
- `CropSnap::Apply(rect, drag, point, ratio, symmetric)`: with a positive tolerance, a move shifts the frame by the
  smallest offset that lands one of its edges on a target per axis (its size kept); creating or resizing snaps only
  the edge on the pointer's side of each dragged axis (a corner handle both axes, an edge handle one) and never
  with a fixed ratio; symmetric mirrors the snapped edge about the center (the press point while creating),
  rounded to the half-pixel grid so both edges stay on whole pixels. Decision D6 of the 2026-09-23 review: legacy
  mirrors about the unrounded press point, which leaves an Alt-drawn frame between pixels.
- `DocumentSession`: `SetCropFrame`, `CancelCrop`, `cropRatioChoice` with `CropRatio` (Free, Original = the
  canvas ratio, 1:1, 4:3, 16:9), `ChangeCropRatio` (the visible frame re-shaped to the ratio about its center,
  keeping its width, when valid), `CommitCrop` (the frame rounded to whole pixels first, then `ResizeCanvas` with
  `contentOffset` = −origin as one `Crop` entry: the content moves by whole pixels, nothing resamples; a frame equal
  to the canvas records nothing) and
  `CropSnapTargets` (the canvas edges and every visible pixel layer's rounded upright bounds). `ClearDocument`
  drops the frame.

## Validation

`DocumentTests`: geometry (`transform.*`)
, drag modes (`drag.*`), snapping (`snap.*`), the session lifecycle
(preview keeps the document, commit/undo/redo, no-op commit, invalid previews, select-commits, undo-cancels-pending,
nudge), duplicate (`duplicate.*`), group (`group.*`), masks (`mask.*`), flips (`flip.*`) and the warp
(`distort.*`, `distortmask.*`: an unlinked mask staying on the document and a linked placed mask taking the
perspective, in the preview and on Apply), each ported from the legacy `TransformTests`, `TransformPressTests`,
`MaskTransformTests` and `DistortTests`, plus `displaycopy.*` (opacity, blend mode and visibility changed under a
pending transform show in the display copy). `HostTests` proves the overlay geometry (rotated edges, corners,
rotation knob, zoomed document mapping, resize cursor directions, distortion midpoints). The WARP self-test drives
a pointer drag, Free Transform with a typed rotation, Apply, Escape, Shift+arrow and both flips through
`Application`.

Selection: `DocumentTests` `geometry.*` (Clipper2 booleans, holes, offsets, ellipse, drag boxes), `coverage.*`
(exact rectangles, half-pixel edges, the top-left rule, the antialiased diagonal, off-canvas loops, rings),
`selection.*`, `move.*`, `modify.*`, `load.*`, `marquee.*` (the legacy `SelectionTests` cases), `loadrefused.*` (a
2100 × 2100 checkerboard mask or layer refused as TooDetailed, nothing to trace not a refusal) and `wand.*`,
`wandsession.*` (the legacy `MagicWandTests` cases plus the layer sampler). The WARP self-test drives a Marquee
drag, a Shift add, the M key, an outline move (put back by Escape and by Ctrl+Z mid-drag), a deselecting click, a
wand click with exact bounds, Ctrl+Shift+I,
Ctrl+A, Ctrl+D, a polygonal lasso closed with Enter, Expand from the header and Escape through `Application`.

Brush: `DocumentTests` `sparse.*`, `kernel.*`, `brush.*`, the brush `mask.*` cases, `opacity.*`, `accumulate.*`,
`ripple.*`, `curve.*`, `tail.*`, `crossing.*`, `snapshot.*`, `ridges.*`, `bounds.*`, `budget.*`, `reject.*`,
`line.*`, `keys.*`, `expand.*` and `clip.*` (the legacy `BrushStrokeTests`, `BrushIntersectionTests`,
`RasterSnapshotTests` and mask painting cases), listed in [`Rendering_Brush.md`](../Rendering/Rendering_Brush.md),
plus `maskfill.*` (a hide-all mask on a 1024² layer painted on two tiles keeps hiding the rest, and keeps hiding its
old area when the layer grows) and `stretchmask.*` (a 300 × 150 mask stretched over a 600 × 300 layer keeps its
place when painted and when the layer grows).
The WARP self-test paints, erases, keys and cancels a stroke through `Application`.

Retouch: `DocumentTests` `heal.*` (the legacy `SpotHealingTests` blemish for every mode), `clone.*` and `tips.*`
(`CloneStampTests`), `blur.*`, `smudge.*`, `liquify.*` and `warp.*` (own goldens: a softened edge, carried color, a
pushed edge, changed tiles, cancel), `gradient.*` (`GradientTests`; the mask row also with a colored palette and with
`maskPaintWhite`), `palette.mask-*` (X and D on a targeted mask flip and reset `maskPaintWhite` and a pending mask
gradient follows; off the mask they change the colors), `gradientfirst.*` (a fill, both flips, Cut, a
pixel nudge, a brush stroke, a layer deletion and a mask switch under a pending gradient record the gradient first;
Flip Canvas drops the crop frame), `shape.*` (`ShapeToolTests` plus the redraw on
scaling) and `eyedropper.*`. The WARP self-test drives each tool through `Application`.

Sizes: `DocumentTests` `canvasdraft.*` (the legacy `CanvasSizeTests` draft case plus the anchor offsets),
`canvas.*` (every anchor's offset for growing and shrinking with the source and transform kept, the colored
extension's cut-out applied and undone through the session, the allocation-free transparent resize, no fill on a
shrink, the over-budget fill, the unchanged same size), `imagedraft.*` (the Image Size sheet's math) and
`imagesize.*` (the legacy `ImageSizeTests`: layer identity and the nearest pixels, undo restoring the source
raster, the resolution-only change, the rotated hidden layer's box, the rejected size; plus a mask rasterized and
a placed mask's scaled placement). The WARP self-test drives Canvas Size (percent, locked, anchored top-left, a
white extension growing from the bottom-right anchor, Escape), Image Size (nearest, 300 ppi) and New Canvas
through the cards' actions and keys. Crop: `DocumentTests` `crop.*` (the legacy `CropTests`: the reverse ratio
create, the move, every handle keeping the ratio, the translating commit with undo and redo, the same-size offset
and the expansion, the cancelling and the whole-canvas commit that record nothing, the ratio re-shape, the snap
cases and the snap targets, the cleared session; D6: Alt-drawn and Alt-resized frames on whole pixels, a fractional
frame committed on whole pixels); the WARP self-test drives the tool through the pointer path.

Pixel edits: `DocumentTests` `edit.*` (the legacy `SelectionEditTests`: brush and gradient clipped to the selection,
an empty selection edits nothing, fills, Clear and the Delete key, mask fills, the clip on scaled layers, masks from
the selection on the layer grid, mask deletion, invert keeping transparency, inside the selection, on masks, on a
uniform mask with a selection and after a pending gradient), `pixelmove.*` (moves and duplicates as one entry with
the outline, nudges, masks refusing, the outline never at its old spot, the preview tiles), `movetiles.*` (a repeated
offset rebuilds nothing, a far move rebuilds only the old and new landings, a dragged path commits the pixels of one
direct move), `clipboard.*` (the
legacy `SelectionClipboardTests`: copy and paste in place, cut, layer via copy, a lasso-shaped copy, copy merged
with a hand-built composite, an external paste centered) and `floating.*` (Ctrl+T on a selection as one entry with
the outline following the handles, Escape restoring exactly, an unchanged transform leaving soft edges untouched,
growth past the layer edge, Undo dropping the edit). The WARP self-test drives the Edit menu, the clipboard keys, a
Ctrl-drag through the live preview, Ctrl+Right, Delete, Ctrl+T with Escape and Enter, Ctrl+J, Add Mask and
Ctrl+Shift+C through `Application`.

Color adjustments and filters: `DocumentTests` `levels.*` (the legacy `LevelsTests`: identity no-ops, input
clipping, gamma, inversion through alpha, channel order, the alpha- and selection-weighted histogram, preview,
cancel, commit, undo and redo, the display scale, the auto algorithms and eyedropper calibration, the eyedropper's
transparent and outside rejections), `hsv.*` (`HueSaturationTests`: an exact no-op, hue, saturation and lightness
ranges, colorize, the selection, the live preview that never accumulates, band weights and handles, independent
ranges, invert, the sliders and the after bar, the eyedroppers, the targeted drag, sampling refusals), `adjust.*`
(`ImageAdjustmentTests`: exposure in linear light, the gradient map and its reverse, grain fixed in document space,
validity, new layers from the palette with their own grain seed, every adjustment kind paired with its Image menu
panel, the Image menu's Exposure as one entry), `filter.*`
(`FilterTests`: a Gaussian blur growing the layer and softening its edges as one entry, motion blur along its
angle, noise on color but never alpha, lens correction opening only the pincushion corners), `adjustlayer.*`
(`AdjustmentLayerTests`: settings surviving undo, redo and duplicate, the identity curve and the Image command
keeping alpha, every kind's editor keeping pixels dynamic and supporting cancel, the blend-mode step), `blend.*`
(the HSL formulas), `panelowns.*` (Levels, Hue/Saturation and a filter open: a Move drag, a nudge, Free
Transform and both flips change nothing and OK applies in place) and `coloredit.*` (one session, one rule: opening
Levels, Hue/Saturation, Curves, Exposure or Add Noise drops a lasso draft, and OK after the layer moved under the
panel closes without an entry while the same edit in place records one; an edit that changes nothing previews the
layer's own pixels; Levels and Hue/Saturation open at identity; only Add Noise and Grain advance the heal seed; the
Image menu's kinds run their adjustment layer's kernel, Grain in layer pixels at the panel's seed). `CompositionTests`
`adjust.*` and `blend.hsl-golden` prove the live layers and the blends on the GPU
([`Rendering_Composition.md`](../Rendering/Rendering_Composition.md)). The WARP self-test drives Ctrl+L with a
preview, Escape and Enter, a channel switch that leaves the RGB range on RGB and the new channel at identity
(RF-809: choosing a channel only switches to it, as in the legacy), the Filter menu's Gaussian Blur, a
Hue/Saturation adjustment layer edited live and kept with Enter, and Shift+= through `Application`.

Content-aware fill: `DocumentTests` `smart.*` ports the legacy `SmartEditTests` (four-pixel stripes continue
through a filled block, at least 114 of its 120 pixels within 1 of the stripe value; a flat background with a red
block reconstructs within 1 everywhere as one `Content-Aware Fill` entry and undoes to the identical raster; Cancel
and a no-source fill leave the original and the failed fill refuses to commit; no selection refuses) and adds the
grown grid (a selection past a 40 × 30 layer's edge fills a 50 × 36 layer with opaque pixels). The WARP self-test
drives Shift+Delete, Enter and the no-source refusal through `Application`.
