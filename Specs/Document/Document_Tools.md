# Document tools: transform (C8)

Status: current normative contract (created with C8; later tool phases extend it)
Last reviewed: 2026-09-21
Owner: `Common/TransformEdit.*`, `DocumentSession` transform members in `Common/Document.*`,
`RedPrism/TransformOverlay.h`, `RedPrism/CanvasOverlay.*`, the Move-tool code in `RedPrism/Application.cpp`

## Scope

This contract owns how a layer's placement is edited: the drag geometry, snapping, free distortion, the pending-edit
lifecycle in `DocumentSession`, flips, and the rules the Move tool and its header follow. Rendering of a
transform is the composition's job ([`Rendering_Composition.md`](../Rendering/Rendering_Composition.md)); the chrome
that shows it is described in [`UI_ApplicationChrome.md`](../UI/UI_ApplicationChrome.md).

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

## Validation

`DocumentTests`: geometry (`transform.*`), drag modes (`drag.*`), snapping (`snap.*`), the session lifecycle
(preview keeps the document, commit/undo/redo, no-op commit, invalid previews, select-commits, undo-cancels-pending,
nudge), duplicate (`duplicate.*`), group (`group.*`), masks (`mask.*`), flips (`flip.*`) and the warp
(`distort.*`), each ported from the legacy `TransformTests`, `TransformPressTests`, `MaskTransformTests` and
`DistortTests`. `HostTests` proves the overlay geometry (rotated edges, corners, rotation knob, zoomed document
mapping, resize cursor directions, distortion midpoints). The WARP self-test drives a pointer drag, Free Transform
with a typed rotation, Apply, Escape, Shift+arrow and both flips through `Application`.
