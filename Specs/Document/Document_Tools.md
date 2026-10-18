# Document tools: transform (C8), selection (C9), brush (C10) and retouch (C11)

Status: current normative contract (created with C8; later tool phases extend it)
Last reviewed: 2026-10-18
Owner: `Common/TransformEdit.*`, `Common/Selection.*`, `Common/BrushStroke.*` (the session's brush and retouch
members), `Common/Heal.*`, `Common/MaskEditWork.*`, `Common/ImageFilters.*`, `Common/WarpStroke.*`,
`Common/LayerCopyWork.*`, `Common/WandLayerSelectionWork.*`, `RedPrism/ApplicationLayerCopyJobs.cpp`,
`RedPrism/ApplicationWandCompositeHostJobs.cpp`,
`DocumentSession` transform and selection members in `Common/Document.*`, `RedPrism/TransformOverlay.h`,
`RedPrism/CanvasOverlay.*`, the Move-tool, selection-tool and brush code in `RedPrism/Application.cpp`, and the mask
command job in `RedPrism/ApplicationMaskJobs.cpp`

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

The single-layer eligibility query follows the layer's inherited visibility without constructing hierarchy rows,
copying unrelated layer names or allocating a complete visible-ID set. Collapsing a folder in the Layers panel
does not change eligibility. Group membership preparation remains a separate bounded-work obligation in P1.

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

### Detached transform preparation

`TransformPixelsWork` prepares an immutable after-document for ordinary, mask, group and live-shape transforms.
`TransformCommitWork` binds that document to captured session/lifetime/generation/active/mask/crop authority,
the exact draft revision and duplicate state, and prepared history. Ordinary transforms preserve nested history;
duplicate completion closes its existing outer entry once. Application validates all authority before history
publication and nonthrowing swaps, retaining old edit, guides, document and duplicate ownership for worker release.
For group distortion, each accepted replacement reserves the original image/mask budgets of later members;
refused members retain their old pixels without permitting the completed document to exceed either aggregate cap.
Groups follow the captured member order and reject duplicate member ids. Pixel sampling and mask semantics remain
the numeric legacy references. The direct inline APIs remain references; production caller migration is A6.3.

`TransformCancellationWork` binds cancellation to the captured draft, target, document lifetime/generation and
history state. Ordinary cancellation swaps only draft/display/guides into its retired owner; document, generation,
history and clipboard remain unchanged. Duplicate cancellation prepares removal of the duplicate and End of its
own scope privately. An existing parent transaction keeps its depth, opening record and name. Publication refuses
newer geometry, changed target/source/history, replacement, foreign-session or consumed results before any swap.

`TransformPreviewWork` prepares a complete private display document. Affine previews share immutable pixels and
change placements. Distorted color and linked/placed/standalone masks retain their transparent bounds and cap
both dimensions at 1024 pixels; group members use their carried homographies. Mask edge tone comes from the
original semantic thumbnail. Unlinked masks remain at their original document placement, and uniform masks
retain their source identity. Invalid distortion corners fall back to the affine display, matching the direct
warp reference. Sparse assembly is private and cancellable. A sealed, single-use publication validates the
captured session, document, target, history and draft revision before swapping the display cache. It changes no
document pixels, history, generation or private clipboard. Its replaced cache stays in the prepared owner for
worker retirement. Production display coalescing and removal of inline reconstruction remain A6.3/T3.

### Ordered transform settlement foundation

Application transform jobs share the selected host input's history ordinal with its prerequisites and subsequent
write or action. Whole-graph admission precedes document/history capture; workers prepare ordinary/group/mask
commits, floating opening/merge or ordinary cancellation privately. Release and Apply/Cancel own the exact layer
and draft revision. Validation, publication or drop and history acknowledgement precede dependent input execution.
Queued Undo ends the armed transform drag immediately and cancels that opening after worker acknowledgement.
Tool/layer/target/reorder changes settle their transform prerequisite before changing the target.

A successful floating opening retains a preallocated CPU restoration node. Merge cancellation and reached
admission, capture, worker or history allocation failure restore the exact original document and selection only
while that opening is current. Newer geometry or replacement cannot be discarded by an old completion. Idle
floating Cancel uses that prepaid restoration without a new job or reservation. Payloads and retained restoration
nodes retire off owner, including small graphs; replacement and shutdown release the application reference before
scheduler teardown. Layer nudge requests own their event-time Shift offset: a fresh nudge prepares one worker
commit, while an existing persistent draft changes metadata and waits for Apply. Repeated Ctrl+T keeps an existing
ordinary opening and creates no history entry. Deferred display, remaining inline-bypass/pointer-loss/duplicate
callers and the complete measured phase exit remain explicit A6.3/A6.4 work.

### Deferred transform API and display ownership

`BeginDeferredTransform` / `DeferTransform` explicitly opt an opening into prepared publication. Direct
Commit/Cancel and unrelated document/history/target writers refuse that unresolved opening; prepared results
retain their authority and checked, nonthrowing publication. The direct reference API remains available for
numeric tests. Application production opt-in waits for the complete caller migration in A6.3.

A deferred display read returns the last complete preview from the same opening/source generation, or the
committed source if none is current. It reconstructs no warp, shape, mask or floating outline inline. Prepared
preview jobs own one active/latest pending graph, reserve before capture, and publish the complete document and
selection only while opening, settings, source and history authority remain current. Both color and mask previews
are bounded to 1024 on the longest side. Floating outlines are mapped on workers with per-point cancellation and
published together with their matching pixels. Failed/newer work retains the last complete display; cancellation
retains the active ticket until acknowledgement, including a closed/reopened mailbox epoch. CPU display/source
payloads retire off owner. Confirmed local discard cancels its transform prerequisite before replacement;
save-prompt Cancel preserves the draft. Ephemeral pointer capture loss queues cancellation and acknowledges it.

For an Alt body drag of a single image layer, the application captures the duplicate intent and owns source
settlement and opening under one input ordinal. A worker prepares the complete duplicated document, the
`Duplicate Layer` Begin scope and exact prepaid rollback before exposing the copy. Image/mask storage is shared;
both pixel budgets count the copy again. Group and non-image targets retain their ordinary move behavior.
The new copy's opening is deferred. While opening is held, pointer metadata coalesces without changing the
captured source; publication rebases the drag onto the settled pixel grid and applies the latest owned point
and modifiers. Release and Escape take their ordinals at input time, including before preparation finishes.
Capture loss before release cancels; release followed by Win32 capture loss keeps the accepted commit. Queue,
admission, capture, worker or history failures expose no incomplete copy or restore the still-current copy from
its prepaid state. Failure reports use the existing localized notice. Commit closes one duplicate entry;
rollback preserves visible document/history/clipboard state while advancing generation and structural epochs.
Complete CPU rollback owners retire off owner, including replacement and shutdown. Qualification is tracked in
A6.3; ordinary transform production policy is a separate migration.

## Free distortion (`DistortWarp`)

`BeginDistort()` turns a pending edit into a distortion whose four corners move freely (`PreviewCorners`; a twisted
or collapsed shape is ignored, `IsUsable`). The edit becomes persistent. A distortion is drawn live by warping the
pixels at a bounded preview size (longest side 1024) into the shape; Apply (`CommitTransform`) resamples at full
resolution (`WarpTrimmed`: the shape's whole-pixel bounds, then cropped to the visible pixels so a stroke hugs what
is there), replaces the layer's pixels and may leave its optional image thumbnail empty, and sets an ordinary
axis-aligned transform over the result.
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

### Bounded lasso worker APIs

The separate bounded route captures a sealed LassoOpeningInput only after original complete admission.
Its worker creates the point arena and accounted gesture writer; current-only acquisition requires joined
creation and memory bound to that same original ticket. Cancellation before acquisition, changed session/
document/history/active/crop/mask/kind, consumed input and exhausted opening/intent counters MUST refuse.
LassoDraft exposes cached PointCount, FirstPoint and RetirementMemoryBytes; a bounded draft keeps its reference
vector empty. Point/cursor/marquee/Backspace changes and snapshot capture visit at most one 64-point chunk.
Capacity refusal MUST preserve accepted points and retained snapshots. Arena blocks are never reused while
readers exist; original accounting survives writer/display/terminal owners and off-owner final retirement.

Closure owns the exact writer and terminal key, arena/prefix/tail, kind/mode, cursor/anchor and physical-event AA.
PrepareClosedBoundedLassoFinishInput MUST reject older, foreign, altered or repeated terminals and source changes.
The admitted worker builds the original freehand/polygonal, whole-pixel rectangle or Ramanujan ellipse, clips,
applies New/Add/Subtract and prepares complete history. It MUST preserve absent versus present-empty selection,
one named entry, no-op/redo/saved-marker behavior and current-only source/history/residency publication.
The reference FinishLasso refuses a bounded draft; an eligible worker refusal has no synchronous fallback.

HostLassoInput reuses the fixed 512-slot PointerInputStream. Its source/active/crop/kind/mode/view/ordinal and
physical document coordinates are descriptors; later headers/views MUST NOT reinterpret accepted values.
Physical nonterminal samples use the same nonwrapping HistoryWork event ordinal as queued commands; replay
MUST select an older command/canvas lifetime before a later point/cursor/Backspace/marquee sample.
A kind-toggle command MUST cancel the exact paired input descriptor and session opening/writer before
switching its kind. An already accepted successor sample, command or canvas lifetime MUST retain an
event-driven continuation after a consumed, stale or rejected sample. An empty settled input requests no wake.
Point/cursor/Backspace/marquee events remain ordered, and terminal AA has a separate slot. Overflow retains the
accepted prefix plus Lost, rejects later Up and stays owned until acknowledgement. Physical terminal and
conditional polygon-close descriptors reserve the actual shared host ordinal at input time. Older accepted
corners/Backspaces replay before the close decision; a near-first press closes only after three corners,
while double-click/Enter preserve the reference short-outline behavior. A rejected early proximity press
acknowledges without history and keeps the gesture live. Captured event AA belongs to the selected terminal.

The explicitly cold-acquired harness route connects Application opening/replay/Finish to these APIs on the
existing continuation source. Original whole-operation admission precedes worker allocation and owner input
capture; joined acknowledgement/current-only validation precede publication and younger input. Eligible
admission/allocation/cancellation failure has no synchronous fallback. The reference route remains the ordinary
startup path until complete caller/lifetime/admission and S5/G1 gates qualify activation. Native tests and
private actual-host PASS alone do not establish those gates.


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
- `SelectionNudgeWork` supplies a separate settled-session core for whole-pixel nudges. Its sealed move-only input
  captures the document/history, session/generation/target/crop authority and exact outline-opening counter.
  Absent/empty selection, live outline, pending edits/gestures and exhausted counters refuse capture.
  Worker translation preserves every vertex, AA, holes and off-canvas geometry without clipping or coverage
  construction; the producer caches point count, bounds and retained capacity in that same cancellable pass.
  Complete `Move Selection` history is prepared before nonthrowing current-only publication and owner swaps.
  Publication advances the opening counter once and generation twice, matching MoveSelection/EndEdit.
  Zero offsets share geometry and preserve revision/redo, while acknowledging the prepared history epoch;
  nested transactions retain their original outer name/depth. Caller eligibility remains a host policy.
- Eligible settled keyboard arrow commands own a signed whole-pixel `HostSelectionNudgeInput` at event time:
  one pixel, or ten with Shift. They are noncoalescing ordered requests; later Shift changes cannot alter them.
  Selected execution refuses a newly opened outline or coexisting edit before capture or unrelated settlement.
  Complete scheduler admission precedes document/history capture; worker preparation resumes that same job.
  Current-only completion acknowledges the exact history ordinal, publishes geometry/history together and
  retires the prepared/old-owner aggregate through the worker release sink. Refusal, failure, stale authority
  and cancellation publish no partial result and never run a synchronous fallback for this eligible route.
  An already live outline keeps the existing no-op policy. Other coexisting routes remain separately tracked
  in the selection worker plan; this route alone does not complete selection migration or measured G1.
- `CanModifySelection`: a non-empty selection and no draft. `ExpandSelection(amount)` offsets outward and clips to
  the canvas (`Expand Selection`); `ContractSelection(amount)` offsets inward, away from the canvas edges too, and
  can leave an empty selection (`Contract Selection`). Amounts outside 1–500 are ignored.
- `SelectAll` (`Select All`, antialiased), `Deselect` (`Deselect`; nothing without a selection), `InvertSelection`
  (`Inverse`, canvas minus the selection).
- `SelectionCommandWork` supplies a separate settled-session preparation boundary for Select All/Deselect.
  Capture owns the whole document/history after admission; worker preparation builds complete changed
  geometry/history, and exact session/generation/active/mask/crop/epoch validation precedes nonthrowing apply.
  Geometrically unchanged commands preserve revision, redo, epoch and generation. Explicit empty remains
    distinct from absent selection. This factory refuses pending gestures and edits.
  - Application routes settled Select All/Deselect through its owned host request and history ordinal. The full
    command/history reservation precedes snapshot capture; the same suspended job resumes for worker preparation.
    Completion validates the exact stamp, settled session authority and sealed publication before apply. Cancel,
    stale authority, capture/history allocation faults and scheduler refusal acknowledge without partial geometry
    or history publication. The request survives until that acknowledgement; younger document commands wait,
    while view commands and messages continue. Replaced CPU owners retire in the worker release aggregate.
    Empty-document requests acknowledge without a worker. True no-ops retain redo/revision/generation; nested
    history joins its original outer entry and saved-marker changes during preparation survive publication.
    Existing live brush/warp/shape, lasso, outline-move and selection-header routes remain separate migrations.
    This settled route does not authorize synchronous gesture closure or establish the measured G1 exit.
- `InverseSelectionWork` supplies the settled Inverse command. The owned host request and exact history ordinal
  admit complete capture, fixed scratch, final output, replaced outline and retirement bounds before capture.
  Preparation resumes the original job and computes the canvas rectangle minus the captured selection using
  ClipperD precision 2 and NonZero filling, within the production 128 MiB scratch ceiling. Cancellation is checked
  at bounded engine checkpoints; capacity or construction refusal publishes no partial geometry or history.
  - Absent selection is a no-op preserving redo, revision, generation and outline. Explicit empty remains present
    and becomes the full canvas. The original antialiasing state and `Inverse` history title are preserved.
  - Exact settled session, document/generation, active target/mask, crop, history and opening authority precede
    nonthrowing publication. Changed raster residency refreshes on the same admitted job before apply; its permit
    remains valid through publication. Younger writes wait for acknowledgement; view input and messages continue.
  - Worker-prepared numeric outline buffers retain their immutable source and are adopted without an owner copy.
    Complete declared document/history/outline output stays charged to the original ticket through copied and
    final weak controls. Failed/cancelled/stale graphs retain their charge until actual retirement too.
  - Joined shutdown joins all producers before finishing the Inverse stamp and clearing its ticket. It grants no
    successful-work authority; final copied/weak output owners can outlive the scheduler wrapper.
  - Live/coexisting gesture closure retains the separate reference route. Owner Direct2D path construction,
    complete selection migration, ordinary activation and measured G1 remain separate requirements.

- `SelectionOffsetWork` supplies settled Expand/Contract commands. Their signed amount is captured
  at the input event (positive Expand, negative Contract), remains in the original ordered request and is
  validated within 1–500. Complete capture/history, 128 MiB scratch, geometry, replaced numeric outline
  and retirement bounds are admitted before immutable capture. Normalization retains binary-scale128
  ClipperD precision2/NonZero; round offset retains decimal-scale100 and original cleanup winding;
  only Expand intersects the canvas afterward. No numerical reference or antialiasing rule changes.
  - Offset scratch opts into same-arena power-of-two reuse after complete destruction. Slots, ownership
    headers and ASan guards remain inside the unchanged cap; Boolean/Inverse retain their monotonic policy.
    Allocation-count refusal and 64-unit cancellation remain global across every phase. Capacity, cancellation
    and construction failure publish no partial geometry or history and reach the original job boundary.
  - Absent/explicit-empty input is a successful no-op preserving revision/redo/generation; Contract past
    the center publishes explicit empty. Named history, Undo/Redo and source AA retain legacy semantics.
  - Exact session/document/generation/target/mask/crop/history authority precedes publication. Residency
    refresh resumes the same admitted job. Numeric outline buffers and complete persistent output retain
    original-ticket charge through copied and final weak controls. Joined Close joins producers before
    clearing the ticket and host barrier; escaped readers retain their exact charge afterward.
  - View input remains available while younger writes await acknowledgement. Existing live/coexisting
    gesture settlement, owner Direct2D path construction, complete ordinary activation and G1 stay separate.

- `PreparePreservingSelectionCommandInput` is a separate Select All/Deselect boundary for an existing brush,
  warp or shape opening. It preserves mutable gesture owners and captured stroke coverage. Opaque authority
  binds weak brush/warp control-block identities and the shape opening counter, plus the same document/history/
  target/crop stamps. The worker never locks or reads these gesture owners. Continued brush/warp samples and
  shape drag bounds do not invalidate this opening; cancellation, closure or replacement does. A shape reopened
  with identical geometry is a different opening. Panels, gradients, transforms, lifted pixels, lasso and outline
  moves remain refused. Settled history-restore authority and the original settled factory remain unchanged.
  This core API does not activate application commands or authorize synchronous gesture closure; host ordinals,
  retained pointer arguments and writer barriers remain S2/S3b/P1.4.
- `SelectionMoveWork` prepares active outline-move End/Cancel history before removing the original outline.
  A successful Begin owns a session-local monotonic opening; `SelectionMoveOpening` is absent after closure or
  document removal. Replacement and clear/recreate preserve the counter. Exhaustion refuses Begin before
  history opens. An expected event-time opening refuses a closed/reopened move before capture, including equal
  geometry. Prepared apply additionally binds that same opening; existing whole-document/history checks remain.
  End retains moved geometry and acknowledges one generation. Cancel restores the original and acknowledges
  two generations (restoration plus End), including a no-op. Changed history and named outer transaction depth
  follow the direct reference methods. Whole-document authority, original outline identity, active/mask/crop and
  exact history epoch are checked before publication. Replaced origin/display/document/history ownership
  remains in the prepared aggregate for worker retirement.
  The Application routes moved-pointer release through prepared End and direct Escape/first Undo/Redo through
  prepared Cancel. Its bounded typed input binds the exact opening and shared host ordinal; a replaced opening
  is refused before capture or unrelated edit settlement. Closing preserves an enclosing transaction's name
  and depth and reuses Begin's original outline snapshot. Admission immediately stops further outline samples,
  even on refusal. Repeated producers for the same opening/intent reuse the pending receipt. The receipt and
  history barrier stay owned through acknowledgement. A failed/cancelled worker retains the moved draft,
  original outline and pending history for retry; admission failure has no synchronous closure fallback.
  Known End-before-command and changed-tool events also retain their event-time opening and wait for End
  acknowledgement before Execute. Same-tool inputs preserve the move. Selected first Undo/Redo with only
  the outline active cancels and consumes the input; younger Undo after an older successful End restores its
  completed history entry. Capture loss closes the moved outline through the owned producer. A replaced
  opening is refused before unrelated settlement/capture, and failure runs no dependent action.
  An eligible zero-offset release retains End and Deselect, or active-layer New wand at the original press
  point, in one noncoalescing typed request. It binds the opening and event-time wand settings/AA, validates
  before unrelated settlement/capture, and keeps the selected ordinal through both acknowledged workers.
  Younger writers cannot pass either phase. Failure before End runs no dependent action; failure/cancellation
  after acknowledged End preserves the completed closure and publishes no dependent selection/history.
  Admission refusal stops pointer samples without a synchronous fallback. A coexisting pending edit or
  All Layers keeps its current caller policy until separately migrated; the typed settled route refuses
  those states before End. Enclosing history name/depth and true no-op/redo semantics remain unchanged.
  Eligible idle outline headers for SetMarqueeKind, SetLassoKind and Deselect own the exact opening/action/value
  in a noncoalescing request. Admission/refusal stops samples; opening validation precedes capture or unrelated
  settlement. Acknowledged End precedes the event-time kind change or prepared Deselect at the same ordinal.
  An unchanged kind still Ends the move. A failed End changes no setting and runs no Deselect; failure/cancellation
  after End retains its completed moved outline/history without publishing dependent selection/history.
  Zero-offset End preserves redo; moved End keeps its separate Undo before Deselect. Enclosing transaction
  name/depth survives. Existing busy and coexisting header policy remains separate; admission never falls back
  synchronously. The native header qualification is tracked in SelectionWorkers S3b1b1b0.
  The retirement observation covers the prepared aggregate, not each raster's final reference. Remaining
  coexisting headers/edits and replacement/close still require separate owned migration; Save/Export/Enter
  preserve their live outline behavior. End-before-action
  classification does not migrate its dependent geometry/pixel work. Graph capture/accounting costs,
  broader selection kernels and measured G1 remain separate requirements.
- The explicit deferred outline-move model keeps the original immutable loops plus a finite, rounded scalar
  offset. Effective samples change only that offset and generation; `DisplayedSelectionValue` exposes source
  and translation without constructing geometry. Numeric reference APIs may materialize the translated loops
  and MUST NOT be used as a synchronous fallback by a future selected host route.
  `PrepareDeferredSelectionMoveOpeningInput` captures settled authority and immutable document/history roots;
  worker preparation opens history privately. Publication checks the original authority, exact epoch and
  monotonic opening before atomically publishing one history depth and scalar mode. It refuses active panels;
  preserving-panel Wand authority does not authorize an outline move.
  Closure prepares translated geometry and history away from the owner. Apply binds the exact original loops,
  offset, document generation, opening, target/crop and history epoch before swapping the prepared result.
  Cancel restores the original; no-op, outer transaction and Undo/Redo semantics match the numeric reference.
  `ContainsSelectionForWork` uses the reference nonzero winding and segment tolerance on translated vertices,
  rather than inverse-translating the query (which changes rounding). It allocates nothing, checks cancellation
  at most every 1,024 loop/edge steps, and distinguishes cancellation from an outside result.
  `HostSelectionPressInput` is a bounded 512-event physical prefix with event-time document coordinates,
  modifiers, antialias choice and nonwrapping ordinal. Up/Lost are separate terminals; overflow preserves the
  accepted prefix. Lifecycle loss closes it without inventing a new physical sample. This model/descriptor
  API does not activate an Application route; complete host admission, replay, lifetime and S5/G1 remain due.
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

### Cancellable wand and trace preparation (P1.6 S4a)

`Mask`, `Trace`, `Select`, `MaskTracing::DarkPixels` and `OpaquePixels` accept an optional cancellation token;
direct callers use an empty token. Cancellation discards the entire private output. `Mask` reports count `-2`
(`MagicWand::kCancelledMaskCount`); selection/tracing report `WandFailure::Cancelled`. The existing `None`,
`Memory` and `TooDetailed` results and numeric/topological rules remain unchanged. A noncancelled empty result
retains the existing nullopt/zero-count behavior. Observers are optional borrowed `noexcept` checkpoint callbacks;
their lifetime ends with the call and they cannot publish document state.

Already materialized input pixels are shared read-only. Otherwise the kernel uses a private cancellable contiguous
copy; it never publishes the source's lazy cache. Mask and vertex storage are initialized in spans of at most
4096 bytes. Explicit matching/edge construction visits, flood seeds/run/neighbor steps, vertex starts and directed
edge walks check cancellation at no more than 4096 steps **in each stage**. Sampling retains the clipped sample
square and rounded four-channel reference. Flood visitation/seed order, right-turn corner separation, outline
order/winding and the eight-million-edge refusal are unchanged. Vertex-start scans can skip all-zero scalar
64-bit words within the checked span using unaligned-safe `memcpy`; they still find nonzero vertices in order.

`RenderLayerPixels` forwards cancellation to its placed rasterizer, including transparent blank/invalid-transform
results. Its sampling and placement rules below remain unchanged. Kernel checkpoints do not bound allocator,
OS call, vector growth, loop-vector compaction or Clipper latency. Direct session callers and the host's
live-context wand routes remain synchronous. The settled New/Add/Subtract host graphs are specified below; these cancellable
kernels alone establish no UI/G1 time bound.

`DocumentSession::MagicWand(point, mode, composite)` needs a document, no outline move in progress and a point on
the canvas. `WandSample` is what it reads at document size: with All Layers the caller's composite (the host's
flattened composition readback; a missing or wrongly sized composite makes the click do nothing), otherwise the
active layer's own pixels at its displayed transform without its mask (`RenderLayerPixels`: nearest when the
layer's sampling is Nearest, else one bilinear tap with edges clamped inside the placed box — `RenderPlacedImage`,
the same sampler `CanvasSize` places a layer with, collapsing High to Smooth since this is a transient read, not a
resample; transparent outside the box or for an invalid transform); a folder or blank layer reads as transparent,
so the whole canvas matches. Nothing matched deselects in New mode. A New selection takes the traced loops without
the canvas clip (they already lie on it); Add and Subtract go through `ApplySelection`. One `Magic Wand` entry.

### Prepared active-layer New wand (P1.6 S4b core)

`WandLayerSelectionInput` retains event-time point/settings/AA, an owned document value and immutable history
view. Capture requires finite on-canvas input, a supported sample size and a settled document; All Layers and
live gestures are refused without changing or settling them. Its authority retains session/document/restore
identity, generation, active/mask target and crop; history publication separately validates the exact epoch.
Changing header settings later does not change the captured request. Owner capture still copies layer metadata.

`PrepareWandLayerSelectionPublication` reads only this owned graph. The active image is placed using the sampler
above, ignoring its visibility, opacity and mask, then traced with the cancellable kernels. A source with no
storage produces the exact clockwise canvas rectangle directly: every sample is transparent for every supported
radius, tolerance and connectivity. This shortcut creates no canvas-sized pixels, mask or vertex grid.

Preparation owns the complete after-document and history publication. A changed traced selection is `Magic Wand`;
no match in New mode becomes a named `Deselect`. Geometric/AA equality is a true no-op preserving redo, revision,
generation and history epoch. Nested history retains its outer depth/title; publication does not replace a saved
marker changed while work was in flight. Cancellation, memory/refusal and thrown history allocation failure
install no partial selection or history.

Current apply is nonthrowing validation followed by history publication and document/display-owner swaps.
Applied and rejected aggregates retain their owners for worker disposal; they must not be destroyed on the owner
path by a production caller. This core does not route application input. S2/S3b ordering, gesture closure, complete
scheduler admission and live-gesture migration remain separate prerequisites; no UI/G1 bound follows from it.

Application routes settled active-layer New/Add/Subtract wand input through the owned host request and shared ordinal. Point,
settings and AA are event-time values; complete phased memory admission precedes capture/resume. Worker source
sampling/flood/trace and history preparation use only the captured graph. Current-only completion acknowledges
success/refusal before younger work and retires its complete aggregate off owner. Outside/nonfinite clicks and
empty documents acknowledge without a job or selection change; an in-canvas no-match publishes `Deselect`.
Consecutive queued New requests obey the existing document/adjacent-ordinal coalescing rules and retire the
superseded receipt. Add/Subtract requests preserve FIFO and never coalesce across an older request. All Layers
uses the composite graph below. Live-context wand requests retain their existing synchronous path.
Broader pointer replay/closure, metadata/accounting and G1
qualification remain separate.

For Add/Subtract, the worker first clips traced geometry to the canvas, then applies the original nonzero
winding union/difference with the captured selection. Subtract without a selection is the original no-op.
No match preserves the existing selection for these modes. The bounded engine is an isolated source fork
of pinned Clipper2 2.0.1 and preserves its precision-2 binary scale128 and geometry ordering. Complete arena,
conversion coexistence, output capacity/control and history ownership bounds precede capture; an incomplete
New-only plan cannot authorize a boolean. Capacity, cancellation or construction refusal publishes nothing.
Final geometry and all newly built history controls retain the original output charge through copied/weak
readers and Undo/Redo, independently of retiring capture/scratch. Actual final destruction and owner release
acknowledgement refund each phase. A job finishing does not imply zero memory while output/history remain live.

### Prepared All Layers New wand (core and host)

Only a settled New/Add/Subtract wand request with `sampleAllLayers` uses the composite worker graph. Event-time point,
settings and AA are captured with a move-only input whose borrowed `ReadbackDocument()` pointer is valid only while
that input remains valid. Capture retains the exact document, session/job and restore authority, and history view;
publication also checks the captured history epoch. Missing active image pixels do not remove the composite source.
The active-layer graph above remains a separate source path. Requests made while a
live context prevents settled admission keep their existing synchronous route.

The host reserves the composite planner, full readback, source snapshot, job metadata and aggregate retirement
before capturing the immutable document. Owner stages compose that snapshot and own every GPU `Map`/`Unmap`; mapped
rows are copied into worker-owned output storage. After the final lease is acknowledged and utility targets are
released, detached worker work runs wand sampling/tracing and prepares the complete after-document and history.
Current-only owner publication publishes history and swaps prepared document/display owners without allocating.
Cancellation, failed readback/preparation and stale authority publish no partial selection. Old owners retire through
the worker release aggregate. This graph adds no polling and establishes no large-image acceptance promise or G1
latency qualification.

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
- The P1 worker foundation `TakeStrokeFinishInput` captures immutable source/history handles and the exact
  pending brush opening after complete admission. It transfers exclusive CPU draft ownership without visiting
  pixels and returns the engine borrow separately to the owner. `PendingBrushOpening` retains a finish marker
  while `PendingBrush` is null; another brush cannot begin until that marker applies or drops. The worker
  `PrepareStrokeFinishPublication` builds the final sparse snapshot, carried mask, eager semantic mask thumbnail,
  sealed document and one history publication. Owner apply compares exact session/document/generation/restore,
  target and root/selection identities plus history authority, then swaps the complete owners without pixel or
  history construction. Prepared residency refresh retains this same publication. Old document/display/history
  and draft owners remain in the retirement aggregate. Discard clears only its exact opening. `FinalDocument`
  and `CommitInfo` expose immutable preparation views only while the input remains valid and unapplied. They
  preserve the exact target and final bytes, confer no apply authority and cannot escape the preparation owner's
  lifetime. Moved or applied preparations expose neither view; stale preparations still require the full
  current-authority check before apply. Direct reference
  Undo/Redo first clears the marker without moving older history; settled history restore refuses while it
  exists. Actual application FIFO activation and full resource/latency qualification remain P1 obligations.
- `ChangeBrushSize(increase)` steps the diameter by a fifth, at least one pixel, within 1–2000;
  `ChangeBrushHardness(increase)` snaps to the next quarter; `TypeOpacityDigit(digit, seconds)` is Photoshop's
  opacity typing (1–9 set 10–90 %, 0 sets 100 %, a second digit within 0.6 s makes a two-digit percent, never below
  1 %). All three ignore keys while a stroke is in progress. `SetOpacity` (the layer's, one `Layer Opacity` entry)
  refuses while a panel owns the document (`HasColorEdit`), as `StepBlendMode` does: the panel's entry stays open
  until OK or Cancel, and an opacity would fold into it (legacy `canEditAppearance`).
- `AddLayerMask(revealing)` gives the active layer a uniform 1×1 mask (`Add Reveal-All Mask` / `Add Hide-All Mask`)
  and selects it; production Add Mask / Add Black Mask commands use the ordered mask worker described below.
  `ToggleLayerMask(id)` records `Enable Layer Mask` / `Disable Layer Mask`.

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
  The eligible settled host press captures its All Layers choice, whole-pixel offset and tip settings at pointer
  press and queues samples in the original continuation ordinal. All Layers obtains the committed composite
  through admitted tiled readback; single-layer Clone samples the captured active image and displayed placement
  on a worker, using the existing `RenderLayerPixels` sampling rules without a composition or GPU readback.
  Both produce a complete document-sized source before beginning the stroke. Later live source/alignment/All Layers settings cannot
  reinterpret this accepted input. The explicit captured-offset `BeginBrush` overload requires a complete BGRA8
  source of exactly the document dimensions and finite offsets rounding within the integer range; invalid
  captured input refuses before gradient resolution, alignment changes or history. Direct core callers retain
  the original `CloneSample` behavior. Cancellation, stale document authority and readback failure publish no
  Clone pixels or history. A host Clone stroke without its admitted source refuses before graphics
  preparation; it cannot fall back to owner layer sampling or synchronous readback. Alt-click changes only the source; mask targets
  refuse before source capture. After the recorded paint producer closes, a new Space pan owns separate view
  coordinates, survives readback completion and cannot append paint samples or change the original ordinal.
  Physical pan loss ends the view gesture while the already sealed paint prefix remains queued.
  This migration covers source readback; stroke painting/commit and the remaining
  live-context routes still require their P1 ownership and timing qualification.
- **Smear** (`smearMode`: Liquify default, Blur, Smudge). Blur is a brush stroke that paints `BlurSample(mask)`, the
  layer (or its mask, with its edge tone beyond its pixels; `RenderMaskPixels` samples a placed mask the same way
  `RenderLayerPixels` samples a layer, nearest included) softened by sigma = diameter / 10 clamped to 1.5–30,
  through the tip at the Strength opacity (`Blur`). Liquify and Smudge are warps (`WarpStroke`): a document-size copy
  of the layer as shown, changed dab by dab (`BeginWarp` / `ContinueWarp`; spacing 8 % of the diameter for Smudge,
  2.5 % for Liquify; hardness at most 0.98; strength = opacity); Smudge carries the color it picked up, taking on
  what it leaves by the strength; Liquify moves pixels with the brush, most at the center. `FinishWarp` paints the
  result into the layer along the dabs with a hard tip four pixels wider, replacing the pixels under it (`Liquify`,
  `Smudge`); `CancelWarp` drops it; `PendingWarp` is the stroke in progress. Never on a mask.

  Exclusive worker primitives check operation/shutdown cancellation at each serial dab and tip/snapshot row.
  Refusal, cancellation or a throwing allocation invalidates the private draft: image and changed-tile APIs
  expose no partial pixels, even though its private dab prefix may remain. Session finish drops an invalid draft
  without changing document or history. Worker image/tile snapshots adopt initialized storage; dirty flags clear
  only after the entire successful tile publication. The original finite supported-input dab formulas and
  spacing remain unchanged. These primitives alone do not migrate the production Warp path.

  `PrepareWarpOpeningInput` captures the event-time mode, tip, point, owner, immutable document/history and
  opening counter without reading pixels or settling another gesture. `PrepareWarpOpeningPublication` constructs
  a lazy `WarpSourceView` draft on a worker. `PlacedRasterReader` samples the original placed layer with the
  transient Nearest/single-bilinear rules above, transparent outside; High remains Smooth. It borrows contiguous
  pixels or reads sparse pixels through four reusable 64-square private windows, retaining the source and never
  assembling its shared cache. Writable 256-square document-grid tiles appear only when a tip reads/writes them.
  A blank layer allocates no full transparent canvas. Owner apply validates the captured opening counter, session,
  document/root, target, history, selection and old Warp source before swapping the ready draft; it creates no
  pixels or history. Cancellation/refusal preserves document/history and retires private owners through the
  caller's original admission. These are detached core APIs; ordinary `BeginWarpStroke` still uses its earlier
  synchronous copy and requires migration.

  `AllTouchedTilesForWork` returns a complete immutable snapshot in row-major document-grid order, including
  clipped edge tiles. It shares unchanged published tile versions and adopts one new pixel allocation for each
  changed tile. Only successful completion clears the changed bits; the ever-touched bits survive. A recorder
  needs the original placed source plus this complete snapshot, independent of skipped/replaced earlier previews.
  `TakeChangedTilesForWork` continues to return only the consumed generation's changes. The complete old/new tile,
  source-window, point/scratch, preview, display and final-history overlaps require admission before allocation;
  the opening-only budget covers captured owners, metadata/windows, first-tip tiles and Smudge's pickup plane.

  `PlanSampleMemory` is designated-owner-only metadata planning before capture: cached allocation bounds,
  source bounds and path/scratch capacities declare the complete private draft plus one old/new snapshot overlap.
  It performs no pixel reads or tile traversal. Plans retain their producer's weak control and private original
  point; foreign, edited and stale plans refuse without mutating a valid draft. Explicit geometric point growth
  and pre-reserved scratch follow the declaration before dabs. The caller must add complete source/history,
  other retained preview, CPU/GPU transport, display and final-publication owners under its original admission.
  These draft bounds alone do not approve a whole application operation.

  `TakeWarpSampleInput` requires the original opening, current plan and sole strong draft owner. Capture retains
  immutable document/history, moves the mutable draft and leaves scalar pending-opening authority in the session.
  During transfer `PendingWarp`/`MutableWarp` expose no draft; competing pixel, selection, adjustment, settled-history
  and preview-teardown routes refuse. Preserved metadata tracks the original opening and waits for joined return.
  The input's known owner cannot append or borrow mutable work; the admitted worker appends at most once.
  Only joined current apply restores the same draft and last point, without pixel work, document mutation or history.
  Discard must match both opening and sample sequence; an old callback from the same gesture cannot clear a
  newer transfer. Cancel/replacement/shutdown invalidates late publication while
  the original worker input retains its graph until retirement. Normal Warp samples/finalization are still due.
  `PrepareWarpPreviewForWork` prepares an immutable placed original plus complete touched replacement tiles,
  in row-major 256-square cells clipped to the document. It requires the draft's known off-worker owner and
  unbound original accounting owner; unknown/current/foreign owners, sealed accounting or cancellation refuse.
  Its private directory and completion flag are producer-built; public metadata cannot forge reader authority.
  `WarpPreviewReader` reads only on an exclusive worker, using that directory and bounded placed-source windows.
  It does not materialize an unassembled sparse original or retain a mutable draft. Region reads preserve row
  padding; invalid regions, sizes and pre-cancellation refuse before writing. In-flight cancellation may leave
  only a private output prefix and grants no publication. Samples use the existing unrounded bilinear brush
  rule after placed-source byte rounding; outside/nonfinite samples leave the caller's output unchanged.
  Transparent document pixels remain successful samples. Old immutable previews survive subsequent dabs.
  This CPU representation alone grants no session, history, GPU or normal display publication authority.
  Private final Warp replay uses `BrushStroke::BeginWarpReplayForWork` with that immutable sample. The original
  parent admits source, path, tiles, coverage, history, display and retirement overlap before creating the draft.
  Replay retains the accepted point order and existing settled Catmull-Rom/provisional-tail/final-Flush rules.
  Every `PrepareWarpReplaySampleForWork` descriptor must complete exact coverage delivery and composition before
  another point or finish. Finite centers beyond the existing 1e7 limit acknowledge a no-op without changing the
  retained curve controls; nonfinite points refuse without changing the prefix. Cancellation invalidates the
  private prefix. `PrepareWarpReplayFinishForWork` seals only after final coverage composition; premature snapshots
  refuse. CPU coverage is a private reference mode; WARP and hardware host replay retain the compute engine and
  original uint delivery. The replay API grants no session/history publication authority.
  `TakeWarpFinishInput` additionally requires the exact opening and sole strong draft owner. It captures only
  immutable document/selection/history and scalar settings, leaving `_warpFinishingOpening` as the session barrier.
  Its worker builds the private replay and exposes only const metadata. `PrepareNextSampleForWork` supplies the
  next captured dab center; `ComposeSampleForWork` alone advances the path. Finish before the entire path or
  publication before final composition refuses. Conversion carries the explicit `StrokeGestureKind::Warp` plus
  the original path owner into prepared stroke/history publication. A brush finishing marker and Warp finishing
  marker cannot authorize or discard each other, even with equal numeric openings. Apply is current and once;
  cancellation, newer document/history/selection or reopening rejects late results. The joined original parent
  retains every captured/replay/publication owner through retirement. These APIs do not activate the ordinary host.
  `PlanWarpFinishMemory` is owner-only preflight before that capture, using cached complete source/history/grid
  bounds and explicit CPU/GPU replay overlap. Missing, borrowed, detached or changed targets refuse. The
  experimental host finishing parent preserves density through each recorded center, worker uint installation
  and final composition. Final history, display and rows publish under the original host ordinal. Successful
  apply moves the old Warp source cache into the prepared worker retirement owner. Ordinary Down, live preview,
  acceptance/fault scenarios and measured G1 remain separate activation gates.
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

Production Shape release uses `ShapeEditWork` and one ordered Commit record. The action owns its input-time
draft and foreground, and retains its outline while preparation is pending. Whole admission precedes source
document/history capture. Workers prepare exact coverage, adopted BGRA pixels, unique layer name, insertion,
selection/live-shape identity and complete history publication together. Checked owner swaps publish once;
cancelled, failed or stale work publishes no layer or history. The ordinal, reservation and history barrier
survive until acknowledgement, and the complete replaced CPU graph retires off owner. Capture loss cancels the
live gesture; ordinary pointer leave commits the owned release. Synchronous session primitives remain numeric
references. Transform-time live-shape redraw remains a separate P1.5/A6 migration.
- **Eyedropper**: `SampleCompositeColor(composite, point)` is the straight sRGB color of the host's composite at a
  document pixel (nullopt outside or over transparency); the host shows the comparison ring while View › Sample Ring
  (the `sampleRing` setting, [`Core_Settings.md`](../Core/Core_Settings.md), default on) is checked.
  `CapturePaletteSampleAuthority` binds a potential deferred foreground publication to this exact session and
  palette input revision. `ApplySampledForegroundColor` refuses foreign or superseded authority without mutation;
  success renews only the caller's authority. Explicit foreground/background writes, swap and reset invalidate
  earlier authority even for equal colors or A–B–A input. Session copy/move cannot inherit it. These counters remain
  outside document generation/history. Exhaustion permanently disables checked publication instead of wrapping.
  `ApplySampledBackgroundColor` has the same checked semantics. Separate `CapturePaletteIntentAuthority` /
  `IsPaletteIntentCurrent` track explicit input only: successful checked samples retain that intent while renewing
  publication authority. This permits ordered accepted sample prefixes without treating an earlier sample as a
  manual edit. A new manual input still invalidates both old publication and manual authority.
  The P1 sampler host captures only a scalar document-read authority and opening at physical Down. An admitted
  immutable committed source and one scheduler ticket/reservation serve its fixed FIFO of 1×1 samples. Manual
  intent is captured with each accepted point; same-pixel coalescing also requires equal intent. Transparent/outside
  points preserve color, and Up/leave/loss seal only accepted points without sampling the release coordinate.
  Foreground/picker publication checks manual intent and exact picker opening; a younger accepted Brush/Gradient/
  Shape owns the earlier scalar prefix even when a later manual edit suppresses the sample's palette publication.
  Tool selection, accepted path/settings and final worker edit preserve the original ordinal and one undo entry.
  Gradient pointer movement preserves captured colors/settings; explicit palette/header input to the already-open
  gradient adopts live values. Shape preview and commit use the same accepted color. Escape cancels pending samples
  through acknowledgement and keeps colors already published. Exact cancelled/failed outcomes cannot become success
  just because the FIFO is empty. A matching cancellation or boundary also releases this HWND's mouse capture.
  Foreground/background picker sampling previews only the chosen chrome color until OK; Canvas Size sampling
  previews only its custom extension draft. Gradient Map sampling updates only the bound endpoint. A live
  adjustment's exact own Preview acknowledgement renews authority within the same owner/document/restore/grid.
  Worker-mode preview retains both generation and the immutable document root; direct reference preview may
  update live parameters. Subsequent points continue to read the single admitted original root. Manual input and replacement picker
  openings still refuse old publication. Current private qualification and remaining native/matrix/G1 work are recorded
  in [SamplerReadback](../TestRuns/SamplerReadback_2026-10-04.md).
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

### Detached copy pixel preparation (P1.5 A5.4a)

`CopyPixelsWork` samples an immutable image/mask or complete document-grid composite on workers, with the same
pixel-center placement and transient Nearest versus Smooth rules (High collapses to Smooth). Selection bounds
use the existing 0.001 rounding tolerance, canvas clipping and exact origin. Full-grid coverage keeps the legacy
antialiasing and byte rounding, including premultiplied alpha. Gray masks copy as opaque gray with the captured
semantic outside tone; cropped output avoids a redundant full-canvas BGRA mask plane. Sparse reads remain private.

Source/coverage/row/publication checkpoints reject cancellation without publishing partial pixels. The cached
memory plan covers private source assembly, sampling planes, exact coverage workspace, cropped pixels and rows;
it excludes already shared inputs and separate host/history/codec/clipboard/retirement owners. No session is
mutated by this primitive. Atomic command publication and host transfer are defined below.

### Sealed clipboard command preparation (P1.5 A5.4b)

`ClipboardCommandWork` captures immutable Copy/Copy Merged sources and an exact private-copy serial after
complete admission and command-specific settlement. Successful direct copies also advance that serial; failed
direct Cut restores the previous serial with its private copy. Copy does not change document generation or
history and may coexist with live brush, warp, shape, lasso and outline drafts. It samples committed target
pixels using the displayed placement. Copy Merged requires the complete committed-document utility composite
and its full-document token captured before composition; layer-only, foreign or stale authority is refused
at input capture and checked again at publication with its original sequence. An old composite cannot acquire
a fresh generation at capture. A color/gradient/transform prerequisite must already be settled.

Cut requires selection interaction closure and the current paintable target. It does not finish an existing
warp. One worker prepares copied pixels, the complete Clear document and history together, with an eager mask
thumbnail and captured mask background palette. Its image history name remains `Clear`; a targeted mask uses
`Fill Mask`. A clear that did not paint, cancellation or allocation failure publishes neither half. Exact session,
document generation, target/mask/selection, active target, crop, private serial and history authority are rechecked
before nonthrowing history/document/private-copy swaps. A younger copy refuses both halves of an older Cut.

Prepared publication retains replaced document/display/history/brush-commit/private clipboard owners for caller
aggregate retirement on a worker. Its new copy remains separately available for later admitted OS encoding.
Preparation includes token mask names/adjustment vectors and all private sampling/clear/history allocations;
retirement includes token color, mask, semantic thumbnail and shared selection owners even for a mask copy.
Existing full document capture is still charged and remains H2 work. The production host graph is defined below;
this primitive alone does not establish its qualification or G1 timing.

### Detached clipboard Paste preparation (F5b.2b.2)

Deferred Paste callers MUST admit the complete preparation and retirement graph before capturing document,
history and clipboard owners. `ClipboardPasteWork` captures one source document, active layer, history view,
Paste authority, private clipboard and private-copy serial. Capture currently copies layer metadata on the
owner thread; constant-time capture and G1 timing remain separate P1.9 work.

Workers construct the complete candidate document and one prepared `Paste` history entry without mutating
the live owner. Private-origin Paste preserves the captured pixels and document origin exactly and refuses
publication after a newer private copy. Foreign Paste uses only the supplied raster and floor-centers it at
`(floor((canvasWidth-imageWidth)/2), floor((canvasHeight-imageHeight)/2))`, including negative half offsets.
A newer private copy does not invalidate an already prepared foreign Paste or determine its placement.

Use the first unused `Layer N` name. Insert above an active non-group as its sibling; with an active group,
insert as its child after the entire descendant subtree. Without an active layer, append at top level. Expand
the new layer's parent on successful application. The new layer becomes active/selected, the document selection
is cleared and the mask target is cleared. Enforce the existing 10,000-layer and 100 M aggregate image-pixel
limits against the complete result; equality is allowed. Sharing a sparse source MUST NOT materialize it.

Publication MUST validate captured session/document identity, document serial, generation, restore serial,
active/mask target, crop, prepared history lineage/epoch and, for private origin, private-copy serial. Stale,
foreign-session, moved-from and consumed inputs refuse without partial mutation. One non-allocating owner
publication swaps the complete document/history/selection state; Undo/Redo restore complete before/after states.
Cancellation, allocation failure and invalid/over-budget sources publish nothing. Large replaced/captured owners
remain in the admitted retirement aggregate and retire off the owner thread. Production caller and full graph
qualification remain tracked separately from this core API's validation.

### Ordered clipboard host processing

Production Copy/Cut/Paste/Copy Merged enter the shared selected-input history barrier. Acquire committed sources
after required panel/gradient/transform settlement, keep the selected ordinal until apply/drop acknowledges and
prevent younger mutations from starting. View commands continue dispatching. Copy preserves live brush/warp/shape
owners and samples committed pixels. Cut that cannot paint mirrors its previous private copy without a Clear
or new serial, as the direct route does.

Copy/Cut apply their complete private publication before OS encoding. Encoding, pre-Empty refusal, retry
exhaustion or later cancellation cannot roll back that completed private operation. Record the source sequence
before encoding. Bind the private session/serial to worker fingerprints and the registered receipt; use the
held transfer sequence and exact partial-publication truth, never a later owner-window sequence. Before Empty,
failure leaves the previous OS clipboard intact. After Empty, finish independent format offers and do not retry.

Paste first admits a fixed metadata probe, then admits its full capture/decode/history/retirement graph under
the same ordinal. An unchanged source sequence can prefer the current private copy; otherwise inspect offered
formats and copy bytes only while their source scope is held. No offered image permits private fallback;
malformed/over-budget offered images fail locally. A valid owned receipt and exact fingerprints can retain private
origin despite immediate-owner-window destruction. A newer private copy cannot rebind an older receipt. Foreign
owned bytes may still decode at their centered placement. Paste uses at most six total Inspect/Copy acquisitions.
The same job and reservation survive acknowledged event waits. Failed/cancelled/stale work publishes no Paste.
Each admitted native Inspect/Copy attempt uses its own invisible, worker-owned message window for
`OpenClipboard`. It MUST NOT use a null window or share an opener between simultaneous scopes. Window
creation failure is a local native failure with empty output; contention remains Busy with the original sealed
authority. Close the native scope before destroying its window on that worker. No HWND or locked OS pointer
escapes into the document, owner callback, retry wait or publication.

Copy Merged captures its original complete-document authority before asynchronous utility composition. GPU maps
and utility targets are released before CPU work; later device replacement cannot restart already copied bytes or
the OS writer. Private preparation, encoding, OS offers and retry waits resume the same original
record through its detached-readback helper. Private pixels explicitly retain its precreated opaque
output lifetime while that record still reserves the full envelope; terminal completion or joined
Close binds the original accounting before refund. A copied reader or last weak storage control
continues to hold that charge after the private clipboard is replaced. Closing a document alone
preserves the private clipboard. No early accounting transfer authorizes additional useful work.
Memory admission includes full readback, selected coverage/crop, capped encoding/global blocks,
history, the previous private clipboard's complete cached ownership (even after document Close),
and retirement. An unavailable/overflowing previous-copy bound refuses before capture. Opaque codec workspace and owner metadata/chrome/upload costs remain measured P1.9/F5d/G1
work; functional migration alone establishes no Photoshop speed or bounded native-call cancellation claim.

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
  Copy reads the unchanged original document during a move, as its direct reference does. One acknowledged opaque
  opening may supply immutable coverage/lifted pixels to later previews and the final commit. Its key includes
  document serial, opening and generation; each fresh worker stroke additionally validates retained target/source,
  grid/placement and selection identity/antialiasing. Mutable stroke state and cancellation remain private. An
  active older preview may populate this exact-opening cache before the pending newest tick, preserving mailbox
  publication order. Cancel/replacement/reopen/source change clears it; failed/cancelled/stale work never populates
  a newer opening. Its complete last owner retires through a cached conservative release bound. Full preparation
  admission is retained even on cache reuse. This reuse is in native qualification; displayed geometry caching,
  bounded history/upload costs and real-loop measurement remain open gates;
  this functional migration alone is no whole-host nonblocking or performance claim.
- **Masks from the selection** (`AddMask(revealing)`): with a selection, a mask on the layer's own pixel grid (the
  loops mapped through the layer transform and rasterized with the selection's antialiasing): revealing hides the
  selected area (`255 − coverage`), otherwise shows only it; the selection is dropped and the mask targeted, one
  `Add Mask from Selection` entry. Without a selection, `AddLayerMask` (`Add Reveal-All Mask` / `Add Hide-All Mask`).
  Nothing on a layer that already has a mask, or during a transform.

  Production Add Reveal-All Mask / Add Hide-All Mask and Add Mask from Selection are admitted ordered Commit jobs.
  Complete memory admission precedes source, complete selected-layer IDs and history capture. Workers map selection
  coverage through the inverse layer pixel-to-document grid, retain the existing reveal (`255 − coverage`) and hide
  (`coverage`) bytes, and build the Gray8 mask plus its eager semantic thumbnail (longest side at most 96 pixels)
  before preparing one publication that contains both.
  The publication selects the mask, preserves the complete selected-layer ID set, and commits mask
  target, selection removal (when present), and one named history entry together through the captured image-import
  authority. The no-selection reveal/hide masks retain their 1 × 1 white/black representation. Cancellation keeps
  the ordered record, reservation, history barrier and captured graph until acknowledgement; the complete retired CPU
  aggregate is released off owner. Settled session producers are covered; settlement from a live brush, warp,
  selection move/lasso or transform remains a later P1.4/A6/S3 qualification and is not established here.
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
  Detached `FloatingOpeningWork` prepares cleared/copied pixels, the opening Begin, sole floating target and a
  complete original-document/selection/active/history restoration before exposing either temporary layer or hole.
  The restoration is bound to the opening's exact prepared history predecessor and retained before-document owner.
  `FloatingMergeWork` captures that restoration with the complete source, draft and revision, prepares merge pixels,
  carried mask/eager thumbnail, moved outline and one outer End on workers, then validates before atomic publication.
  An unchanged/refused/cancelled merge returns a sealed resolution using the prebuilt restoration; it cannot discard
  newer geometry, a replaced source or newer history. Explicit Escape may restore newer preview geometry from the
  same opening after acknowledgement. No failure-path document/history construction is needed on the owner.
  Worker allocation failures leave the live opening and prebuilt restoration intact. Clipboard authority stays
  unchanged. These core boundaries do not yet qualify production floating callers; A5.5/A5.6/A6.3 track activation.
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

### Layer via Copy worker preparation

Production Layer via Copy is a selected Commit input on the shared `HistoryWork` stream. The host admits its complete
scheduler-memory reservation before capturing the immutable source document, active layer, image-or-mask target,
displayed placement, selection and history authority. The admitted record and reservation stay owned through worker
preparation and owner apply/drop acknowledgement; view input remains dispatchable while it runs.

With a selection, the worker uses the captured image or mask placement and the existing `CopyPixelsWork` path to
prepare cropped pixels and their exact document origin. It creates the first unused `Layer N` as a pixel layer
immediately above the source, under the same parent. Successful publication makes that layer the sole selected
layer, clears the selection outline and clears the mask target. Without a selection, the worker duplicates the full
source layer record immediately above it under the same parent, preserves its asset, mask, shape and adjustment or
clipping data and shared raster identities, assigns a fresh Guid, and appends ` copy` to its name. Publication makes
the duplicate the sole selected layer and clears the mask target. Each branch prepares the complete after-document
and its one history entry before publication (`Layer via Copy` or `Duplicate Layer`).

The operation leaves the private clipboard pixels, origin, session identity and serial unchanged and does not call
the OS clipboard. Group sources, explicitly empty selections and selected sources without pixels for the captured
target do not publish. Admission enforces the layer limit and independent aggregate image- and mask-pixel budgets.
Owner apply revalidates the captured session/document authority, active and mask target, generation and history
state before its nonallocating publication. Stale, cancelled, failed or over-budget work adds no layer or history.
Cancellation retains the record, reservation and captured graph through worker acknowledgement; the complete
retired CPU aggregate is released off owner.

These settled-source controls do not qualify settlement of a live brush, warp, selection outline/move or transform;
that remains P1.4/A6/S3 work. Owner metadata capture remains H2, and history/chrome/display costs and the full G1
measurements remain open. Functional controls establish no G1 bound.

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

### Scalar Hue sampling and preserving-panel Wand

`SampleHueRangeValue(optional<double>)` and `BeginHueTargetingValue(optional<double>)` accept an already sampled
hue while retaining the raster entry points as numeric references. Missing/nonfinite values or hues outside
[0,360) refuse without changing panel, targeting, document, global settings or history. Raster wrappers check
their existing panel prerequisites before sampling, then delegate. Range/band selection, near-neutral refusal
and half-unit targeting deltas keep the existing formulas; no color or kernel golden changes.

The explicit preserving-panel Wand route uses separate opaque `PanelWandOpening` and `PanelWandAuthority`.
The physical opening binds session, document serial, exact panel opening and color sequence. It may survive
older FIFO source changes; the admitted source witness is captured only after those prerequisites finish.
Global history-restore eligibility stays strict. Layer and committed-composite routes preserve the exact panel
and source pixels while publishing prepared selection/history atomically. Original press point, mode, source
choice, settings and antialiasing remain event-time values. Zero-offset outline closure and dependent Wand or
Deselect retain one input and its original ordinal. Invalid mode, cancellation, stale source or equal-valued
panel reopening cannot publish partial state. Other typed close policies remain unchanged.

An acknowledged preserving-panel Wand records a private binding to that panel opening and the exact new
immutable selection identity. Newly captured preview, histogram and commit validation tokens use that bound
selection while the public panel source token and original sampling/rendering coverage remain unchanged.
Pre-Wand produced work stays stale. Its selection-only refusal must leave the still-valid panel open; changed
session, restore, canvas, target image, transform or mask retain their normal strict refusal behavior. A later
direct selection change cannot acquire the Wand binding. Cancel retains the acknowledged Wand selection;
the next panel captures that current selection as its own opening coverage.

### Immutable destructive-panel work

`PrepareColorEditCommit` captures owning inputs for `RunColorEditJob` (`Common/ColorEditWork.*`), which has no
session, HWND, chrome or device access. `ApplyColorEditResult` runs on the session owner and executes no pixel
kernel. The direct `CommitColorEdit` API uses this same graph with an `InlineExecutor`; the production host uses
the admitted worker commit and prepared history boundary. Preview grids, histogram arithmetic and committed kernel arithmetic retain
their existing contracts above.

The opening token retains the image, placement, mask, selection, canvas size, document lifetime and restore epoch.
Every panel begin, settings/preview update and end advances a sequence. A result from another session, an older
sequence or a different panel MUST NOT alter or close the current panel. A matching result whose opening inputs
are stale closes that old panel without an entry. Unrelated metadata edits, such as a name or print resolution,
survive a valid targeted apply. A result still rechecks the current document's image and mask budgets before any
mutation, because another layer can consume budget without invalidating the target.

The worker renders full-size pixels, trims a blur, carries its opening mask and creates its required semantic
mask thumbnail before returning a ready result. The image thumbnail is optional derived state and may be empty.
The mask thumbnail MUST be installed with the mask. Failure to carry it
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
required semantic mask thumbnails and metadata; optional image thumbnails are excluded. An exact resident preview
avoids another filter/coverage reservation. Geometry producers
cache selection point counts; querying the estimate does not traverse selection points or loops. The host submits
this graph through the ordered commit engine and selected continuation described below.

The destructive color/filter host also reserves complete history metadata/trim preparation before source capture.
`ColorEditCommitMemoryBytes` reads borrowed opening metadata without copying settings or sparse pixels. An admitted
record captures the current source/generation and immutable history view, then its worker binds the ready output
to a complete after-layer/history publication. Owner application validates the opening token/settings, captured
generation and exact history epoch before swapping pixels, mask, applied settings and panel owners. Retired
panel lookup/coverage, old settings and source/history owners remain in the worker-release aggregate. Capture
refusal keeps the panel unchanged; a worker preparation failure retains the existing commit-failure state and
redo/source pixels. A stale prepared history epoch cannot publish or close a newer panel. Direct reference
history methods remain reference routes. Adjustment-layer opening and OK use the admitted preparation described
below; explicit Cancel uses the separate ordered dismissal boundary below.
This boundary passes the full native configuration matrix
([evidence](../TestRuns/ColorHistoryPublication_2026-10-01.md)); measured P1/G1 performance remains open.

### Prepared adjustment opening and OK

Production opening owns its descriptor and whole readback/history reservation before copying source records.
The worker creates the below-composite visibility snapshot, keeping all records for ancestry and live mask sources.
After final GPU row acknowledgement, worker preparation constructs the deferred panel, original settings and
named Begin before the owner installs any of them. Opening preserves document pixels/visibility, generation,
revision, global filter settings and retained redo. Failure installs no partial panel or transaction.

The recorded utility opening allocates its capture aggregate on the original admitted Compute stage.
It records the private below-composite through the shared recording lane and retires its graphics before
CPU panel/history preparation. Real adapter headroom and the complete source, output, history and retirement
overlap are admitted before capture. The panel's immutable original Raster retains that same original
accounting after the job acknowledges; copied readers and its last weak storage control retain it too.
Closing the panel does not refund live readers. Only actual final control destruction and the scheduler's
owner acknowledgement refund the charge. Terminal device rebind cannot replay already copied bytes.

OK is an admitted metadata job, including when Preview is off. It prepares the final record and End without a
destructive pixel kernel. A selected OK receipt stays Pending until exact-authority publication or drop is
acknowledged. True stored-record no-op preserves revision/redo; an old record whose absent HSV or Exposure settings
become explicit keeps the existing real-entry behavior. Every successful End advances the mutation stamp and panel
sequence. Original source bytes and the global filter template remain unchanged. Construction refusal, stale input
or cancellation retains the current panel and its open history; complete retained preview/document/history owners
retire off the UI thread. Synchronous reference APIs keep their goldens. See [history](Document_History.md) and the
[qualification record](../TestRuns/AdjustmentOpeningAndOkHistoryPublication_2026-10-01.md).

### Ordered panel dismissal

Production Cancel/Escape admits an owned request for the exact opening and returns to the outer pump. It requests
drop of prior selected work and retires previously queued dependent actions, then waits for acknowledgement before
acquiring the panel. Repeated Cancel for that document/opening coalesces without retiring its own request. Controls
remain frozen while the receipt is Pending; view input and Cancel stay available.

Adjustment Cancel restores the opening parameters and prepares End on a worker. Destructive dismissal restores no
pixels and changes no document/history/template state; it prepares fixed authority and transfers panel/display
ownership to worker retirement. Failure keeps the panel, source/redo and original error. Old openings and stale
authority cannot close a reopened/current panel. Cancel after acknowledged OK cannot reverse its committed effect.

Standalone dismissal finishes after its close acknowledgement. A size card uses a selected Cancel prerequisite
before acquiring dimensions; local discard also waits for closure before reset. Prepared file replacement keeps
its atomic discard-on-success/keep-draft-on-failure boundary. Production callbacks do not pump nested waits.
The [cancellation record](../TestRuns/PanelCancellationHistoryPublication_2026-10-01.md) tracks caller qualification;
complete local reset publication and measured P1/G1 costs remain separate gates.

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

With `SetColorDisplayJobsEnabled(true)`, a deferred adjustment-layer settings tick retains the original
immutable document root and generation. Its completed worker lookup supplies the visible preview; preview off
selects the original adjustment. Only the admitted adjustment closure constructs/publishes the final metadata
and history. The default reference mode keeps its existing live-metadata behavior and numeric expectations.
An exact `UpdateAdjustment` restore/no-op retains the root and generation. The application enables the worker
display mode; it must not obtain pending GPU pixels by mutating that committed source on every tick.

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

The immutable-root host queues a bounded scalar preview descriptor: session/document serial, opening, settings
sequence and full display-source generation. Owning settings/target/document capture occurs only after the
complete additional-allocation reservation is selected. The same record resumes worker render and complete
display construction. An admitted opening histogram follows the same capture and retirement boundary.
An older running settings request may publish an intermediate result; one latest pending descriptor follows
its acknowledgement. Off, identity, changed display source, Cancel and reopen invalidate publication immediately,
while the old ticket and reservation remain until that exact record acknowledges.

For destructive previews, `ColorDisplayPublication` binds the rendered target to the complete captured document
and scalar history authority. The owner validates current authority and swaps the prepared display/panel handles;
replaced owners remain in the preallocated retirement payload. `DisplayDocument` selects a compatible completed
display or committed document and does not clone or assemble pixels. Preview invalidation retains invisible
source/coverage/lookup caches and one old grown cache until prepared transfer; `ColorLookupPreviewReady` controls
shader visibility. Lookup-only adjustment-layer previews retain their original composition and epoch semantics.

The allocation-free failure publication cannot compare rebuilt outlines on the owner. It accepts only the same
captured selection root; successful preparation proves equality on the worker and binds that root. Missing,
foreign, moved-from, consumed or stale publications preserve current data. Complete CPU admission includes
settings/token metadata, render scratch, display construction and job/payload records; resident retirement
ownership is reported separately. Failed/cancelled/dropped aggregates retire off owner through the existing
scheduler release sink. These structural contracts do not establish G1 timing, large-metadata, device recovery,
shutdown or complete native migration qualification.

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
Its production worker rasterization/publication follows the Shape contract above. Complete owner capture,
chrome synchronization and first-upload timings remain P1.9/G1.

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
  asset whose optional image thumbnail may be empty and stops being a live shape; masks on their own placement and
  1×1 uniform masks
  keep their pixels with the placement scaled (`Placing` of the scaled unit map), other masks rasterize the same
  way with 0 beyond their pixels (`RenderPlacedMask`); folders and adjustment layers keep only the box. Layer
  pixels and mask pixels each stay within 100 megapixels (`TooLarge`); a failed allocation is `Memory`.
- `DocumentSession::ApplyDocumentSize(document, name)` installs a resized document (same id) as one entry named
  `Canvas Size`, `Image Size` or `Crop`, dropping any outline draft and keeping the active layer.

### Asynchronous size-card preparation (P1.8)

The application's Image Size and Canvas Size Apply actions submit immutable Commit jobs after all preceding
history work has applied. Options include the selected draft's dimensions, sampling, resolution, anchor and
captured extension color. Workers never read the card, palette, session, window or GPU. Eligible New Canvas Apply
with a live outline follows the prompt-aware preparation below; Crop retains its existing path. A size action that first commits a panel continues the same input ordinal with a new exact
acquisition stamp. A younger Undo or mutation starts only after the size result has applied or been dropped.

### Eligible size-card Apply with a live outline (P1.6 S3b1b1b1a)

An eligible valid Canvas Size or Image Size Apply, with an existing outline move and no coexisting canvas,
opacity or Crop edit, owns the input-time card invocation and exact outline opening. Stop outline pointer sampling when
admission is attempted. The noncoalescing request first cancels that opening on its admitted worker, then
starts the existing size worker after acknowledged publication, retaining one input ordinal through both
workers. The original outline is restored before size acquisition; resizing drops selection. A younger Undo
cannot pass either worker. Ordinary changes produce one Canvas Size/Image Size entry; an enclosing history
transaction retains its depth and original outer title.

Foreground/background extension owns the input-time ARGB value alongside the copied draft. A later palette
change cannot change that requested fill. Completion refuses a reopened card, replaced/closed outline,
changed document/history authority or new coexisting edit before closing or resizing. Refused admission and
failed/cancelled close preserve the live opening and moved outline for retry. After successful outline Cancel,
a failed/cancelled resize preserves the restored original outline and adds no size history entry; it does not
reopen the cancelled gesture. Captured/retired owners stay reserved through acknowledgement and retire on
workers. Invalid drafts, other prompt-driven lifecycle and already coexisting edits retain their existing
routes until separately migrated. Constant-time metadata capture, incremental history and measured G1 remain
separate requirements.

### Eligible New Canvas Apply with a live outline (P1.6 S3b1b1b1b)

A valid New Canvas Apply on the current card, with the original outline opening and no coexisting canvas,
opacity or Crop edit, owns its copied draft and card invocation through one noncoalescing input ordinal.
The existing Save/Discard/Cancel prompt precedes outline closure. Prompt Cancel preserves the current document,
card, moved outline, local outline drag and exact history/generation. Only confirmed Save or Discard stops
further outline sampling.

Discard prepares outline Cancel on its admitted worker and starts document replacement only after successful
publication. Save prepares outline End, writes the captured original document to the selected output path,
and starts replacement only after acknowledged successful save. The close, write and replacement phases retain
the same selected ordinal; younger Undo or mutations cannot pass them. The copied draft retains input-time
dimensions and the write retains its selected path even if later card/path values change. Successful replacement
uses the established New Canvas history entry and saved marker, closes the card and clears the old document path.

Every selected stage and worker publication checks the current card invocation and original document/history
authority. Closure also requires the exact original outline opening; after closure, save and replacement require
the outline to remain closed. A newly opened color, opacity or Crop editor refuses the request even when pixel
generation is unchanged. Refusal cannot close the newer edit/card/opening or publish dependent size work.
Cancellation remains pending through worker acknowledgement. Failure before closure preserves the original
document and live outline; failure after acknowledged closure or save preserves those completed phases and
adds no replacement history. Reservations cover captured/retired owners until acknowledgement and worker disposal.

Invalid drafts and already coexisting edits retain their existing route until separately migrated. This
qualified functional boundary does not establish constant-time metadata capture, incremental history, native
ARM64 runtime or the measured G1 limits.

### Eligible Close Project with a live outline (P1.6 S3b1b1b1c)

Close Project with the original outline opening, no size card and no coexisting canvas, opacity or Crop edit
retains the existing Save/Discard/Cancel prompt. Prompt Cancel preserves the current moved outline, local drag,
document/path and post-gesture history/generation. Only confirmed Save or Discard stops outline sampling.

Discard acknowledges prepared outline Cancel before closing the project. Save acknowledges prepared outline
End and the captured original-document write to the selected path before closing. One noncoalescing selected
ordinal owns close, save and lifecycle publication; younger old-document Undo waits and becomes stale after close.
Successful close installs an absent document and active layer, clears the path and selection, and installs empty
saved history with no Undo/Redo, matching the existing lifecycle reference.

Every selected stage and worker publication validates the original document/history authority and current
editors. Before closure the exact opening must remain current; afterwards it must stay closed. A newly opened
size card, color, opacity or Crop editor refuses the request even when pixel generation is unchanged. A later
failure preserves already acknowledged close/save phases. Cancellation retains ownership through acknowledgement;
reservations retain captured and retired owners until off-owner disposal.

Already coexisting lifecycle routes retain their existing policy until separately migrated. This functional
boundary does not qualify constant-time metadata/history, native ARM64 runtime or measured G1 limits.

### Eligible New, window close and session end with a live outline (P1.6 S3b1b1b1d)

Default New, ordinary window close and noncritical session end with the original outline opening, no size card
and no coexisting canvas, opacity or Crop edit retain their existing Save/Discard/Cancel prompt. Prompt Cancel
preserves the current moved outline, local drag, document/path and exact post-gesture history/generation.
Only a confirmed Save or Discard stops further outline samples.

Discard acknowledges prepared outline Cancel before the dependent replacement or close. Save acknowledges
prepared outline End before writing the captured original document; dependent replacement or window close
requires successful save acknowledgement. The selected input keeps one ordinal and all request/path ownership
through closure, save and the final action. Failure preserves already acknowledged phases and refuses the
dependent action; no successful phase is replayed.

Default New then uses the established saved 1280 by 800 blank-document replacement, New Canvas history and path
clearing policy. A younger old-document Undo waits through replacement and becomes stale. Window and session
close request one callback-safe posted WM_CLOSE after all acknowledgements. Actual continuation shutdown
unregisters the input source, cancels retained younger inputs and refuses fresh input before ownership teardown.

Before closure, the exact original outline opening and session/document authority must remain current.
After closure, the outline must stay closed. Every owner stage and worker publication also checks the absence
of a newly opened size card, color, opacity or Crop editor. A request cannot close or mutate a newer edit even
when that edit has not changed pixel generation.

Noncritical session end uses the ordinary WM_QUERYENDSESSION veto and generation-tagged deferred prompt.
Admission, prompt return, every stage and publication require the same pending session-end generation.
WM_ENDSESSION(FALSE) or a replaced generation prevents an unacknowledged closure and late window close;
already acknowledged closure/save phases remain completed. Critical termination retains its existing policy.

The normal full WARP self-test drives actual NewDocument, ordinary WM_CLOSE and WM_QUERYENDSESSION producers
for Cancel/Discard. Its typed fault/order controls separately reach Save, capture/preparation/publication,
allocation/admission refusal, stale editor/opening/authority, session-generation cancellation, output-path
ownership, replacement history, ordered younger Undo, message pumping and off-owner disposal. Final WM_CLOSE
is intercepted for harness inspection, including the actual StopHostContinuations boundary; the existing
separate-owner shutdown suite proves complete owner teardown. Structural controls do not assert a wall-clock
performance limit or qualify general coexisting edit routes.

`ResizeCanvas`, `ResizeImage` and the placed rasterizers accept a cancellation token; direct callers use its empty
default. They preserve the existing sampling order, rounding and pixel goldens. Source sparse pixels are assembled
privately, without publishing `Raster::Data()` on a shared sparse input. Checks occur between layers, rows and
256-pixel spans; row initialization/clear checks can cover at most 120 KiB. Cancellation returns no partial raster
or resized document. These are structural bounds; allocation, page faults and destruction can delay acknowledgement.

`SizingMemoryBytes` declares metadata, all rounded output allocations and required semantic mask-thumbnail
allocations, plus the largest private sparse-source assembly (renderers release it before starting the next layer
or mask). Optional image thumbnails are excluded. Refused
late-layer bounds still reserve outputs already produced before the refusal. Metadata allows the immutable input,
detached document, apply validation and existing history copies. Resident source pixels and immutable selection
geometry are shared. An unrepresentable reservation or failed admission fails the whole action.

`ApplyDocumentSizeIfCurrent` validates the full session/document generation and restore token, same document id
and document graph immediately before the one named transaction for direct reference callers. The production
size-card job captures its source/token/active id and immutable history view only after its complete resize/history
reservation exists. Its worker validates the resized graph and binds it to a detached history publication.
`ApplyPreparedDocumentSizeIfCurrent` checks the full document token, captured active layer and exact history epoch, publishes the prepared
history, and swaps document, outline draft and selection-move owners without constructing or destroying their
retired graphs on the owner. The aggregate's pre-capture retirement bound includes retained lasso/outline capacity.
The host also validates the exact history stamp and size-card invocation. Closing/cancelling a running operation requests cancellation and retains the request,
reservation and history barrier through worker acknowledgement and owner apply/drop. Reopening a card invalidates
the old result; its completion cannot close the newer card. A late Finished result after cancellation is dropped.
Only a successfully applied changed result enables Fit and invalidates document display. Unchanged, refusal,
failure and stale results do not add history. Worker failures use posted localized notices outside the completion
callback; TooLarge retains the distinct Too Large notice.

History preparation failure and capture refusal preserve pixels, revision and seeded redo. Changed history epochs
refuse publication even when the document generation is unchanged. This H1 migration is in native qualification;
constant-time metadata capture, incremental accounting and the measured P1.8/G1 exit remain open.

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

### Prepared wand mode and antialiasing authority

The prepared wand layer/composite API captures New/Add/Subtract and antialiasing with the event settings.
Invalid modes refuse before source capture. Matched New replaces, Add unions and Subtract removes the clipped
outline from the captured selection. Matched changes use captured antialiasing; no-match Add/Subtract preserve
null, explicit-empty or nonempty selections and their antialiasing/history. New no-match deselects. Publication
remains bound to the original session/document/selection/history authority. Boolean outputs may retain
collinear edge vertices and have a cyclic starting vertex; their exact geometry/winding remains required.
This core preparation contract alone does not activate Add/Subtract in the host's asynchronous New route.

## Selected selection publication ownership prerequisites

The optional charged Select All/Deselect route publishes only after its complete new history/geometry/control
output is bound. Its constant-time output bound is overflow checked; unbound or underbound results refuse
before document/history mutation. Select All uses the exact AA full-canvas rectangle; Deselect removes the
optional selection. True no-op output is zero and preserves history revision, redo and document generation.
Source, active/mask/crop, preserved gesture and history authority remain mandatory.

A prepared deferred outline opening can opt into opaque own-move/abort authority. Scalar movement retains
immutable source geometry and renews only that exact gesture's witness. Prebuilt abort preserves the original
selection root and redo, closes its Begin and matches reference Cancel invalidations. Transform, brush,
transferred brush/warp work, other gestures, foreign mutations and a consumed opening refuse. Default opening
and numeric reference routes retain their behavior. These model APIs do not enable physical dispatch or
change pointer-loss End, click follow-up, preserving callers, ordinary activation or G1 requirements.

## Worker lasso opening: accepted descriptor settings and history epoch

The `LassoOpeningSettings` overload of `PrepareLassoOpeningInput` records the accepted physical Down's numeric kind and antialias flag. Subsequent header preference changes cannot replace those settings. The bool overload retains its live-kind validation and live-AA-at-acquisition behavior. Invalid kind, selection mode or nonfinite point refuses before rotating the pending intent. Both overloads capture exact `DocumentHistory::Authority`; acquisition requires the same history epoch as well as the existing session/document/editor/token/intent authority. A foreign Begin refuses even when generation and revision are unchanged. Worker-created storage must bind to the original admitted parent before acquisition; moved-from, superseded, cancelled and already-applied results refuse. This API alone does not activate ordinary physical routing.

## Guarded ownership of a worker-created lasso writer

`PrepareLassoOpening` retains own-gesture authority only with its explicit default-false opt-in. Guarded marquee drag, abort and close require the same applied active writer and latest own key, plus unchanged session/document/editor/history/token authority. Own successful or unchanged drags retain that authority; a foreign direct writer mutation revokes it without permitting rollback. Closing consumes own active authority and seals the supplied physical terminal AA. General history restoration still refuses a live lasso. The default reference path retains its original ownership and live API behavior. These primitives alone do not enable ordinary physical input.

## Selected Rectangle/New physical-parent protocol

The explicitly opted-in parent may choose Rectangle/New after cancellable worker containment finds no hit, including absent or explicit-empty source selection. It reuses the original admitted parent, fixed Down settings, ordered samples and terminal AA. Physical Alt never requests `fromCenter`; physical Shift held at Down must first be released before constraining the draft. The model's explicit centered-drag API retains its numeric behavior. Lost/cancelled drawing preserves the original selection. This protocol alone does not activate ordinary input.

An opaque `LassoIntent` clears only its exact pending opening or finish identity. It cannot clear a newer intent, another kind, session/document, or foreign editor/history authority. Closed own-writer cleanup requires its exact original authority and retains old owners for worker retirement.

## Qualified selected Rectangle Win32 caller

With its explicit test gate enabled, the physical caller routes Rectangle/New inside and outside hits through the original admitted parent. Absent/empty selection, click, capture loss, cancellation, overflow, stale active target and joined Close retain their distinct terminal semantics. Accepted Down owns primitive kind; accepted Up supplies terminal AA without adding a Move. Physical Alt during drawing does not center it. Every terminal acknowledges the exact prefix and history stamp, and preserves foreign state. This selected caller remains disabled in ordinary operation until complete selection/display admission and activation gates close.

## Kind-bound New marquee preparation

The typed New-marquee descriptor supports Rectangle and Ellipse. It captures the accepted kind before source capture; worker preparation must use that same kind and its admitted limits. Unsupported kinds and mismatched Rectangle-only admission refuse before capture or document/history mutation. Inside-outline movement retains the prior outline; outside Ellipse/New generates the legacy oval under the same original parent, accepted samples, terminal AA, history authority and retirement. Physical Alt never means centered drawing. The explicit physical caller routes both New kinds, including inside-outline movement; accepted Up supplies AA without an invented Move. Ordinary activation remains a separate gate.

## Opted-in preserving marquee worker parent

The typed marquee parent can explicitly opt into Rectangle/Ellipse Add/Subtract. These modes always draw,
even when Down is inside the existing outline; only New may choose outline movement after containment.
Captured Down kind/mode, ordered physical samples and terminal AA remain authoritative. Add unions and
Subtract removes the worker shape using the existing clip-grid rules. Subtract with absent selection
preserves absence/revision/history; explicit-empty subtraction retains its empty-selection edit semantics.
A foreign target/editor/history change refuses without rolling back foreign state. Cancellation/loss
preserves the original outline, and joined Close retains the original parent until worker acknowledgement.
The new mode gate defaults off. This model protocol alone does not enable the ordinary physical caller.

## Selected preserving marquee physical caller

The explicit selected physical gate supports Rectangle/Ellipse Add/Subtract through the typed original
parent. Those modes always draw without containment, including inside an old outline. Accepted Down
owns kind/mode; accepted Up seals AA without inventing another Move. Physical Shift/Alt retain the legacy
Add/Subtract, rearm and non-centered rules. The same admission/ticket covers input, closure and retirement.
Absent, explicit-empty, foreign target, loss, cancellation, overflow and joined Close retain their distinct
semantics. This selected caller remains disabled in ordinary use until complete activation gates close.

## Cursor containment model prerequisite

`DescribeSelectionCursorProbe` returns only cached scalar authority, translated bounds and a
retained-geometry/control envelope. It captures no geometry or history, walks no points, and does
not materialize deferred translation. No document, absent/empty/collapsed selection, an incompatible
transform/lasso, nonfinite or collapsed translated bounds, or an unavailable/overflowed storage
bound returns no plan. The caller must add its job/payload/construction costs before admission.

After that original admission, `CaptureSelectionCursorProbe` recomputes and matches every plan
field before sharing the immutable original selection and scalar translation. Changed session,
document serial, generation, restore authority, bounds, translation or declared bytes refuses.
Persisted document identifiers alone cannot authorize capture. Captured roots survive replacement
or deselection. Worker containment retains the existing per-vertex translation, edge tolerance and
nonzero winding reference; inverse-query translation cannot substitute for it at large coordinates.

This API prerequisite does not activate the ordinary pointer consumer or qualify complete host
admission, retirement, native profiles or G1.

## Selected cursor containment worker

The explicitly selected Application route returns a neutral containment result while exact work is
pending or unavailable. It reads only finite scalar bounds and the model plan before original whole-job
admission; it captures immutable geometry only in the admitted owner continuation. Exact containment
runs on a worker with the existing per-vertex translation and cancellation checkpoints. Ordinary pointer
activation retains the separate complete-consumer and G1 gates.

At most one useful probe and one replaceable latest scalar request are retained. Document serial,
document generation, cursor epoch and query coordinates bind cached replies. In particular, equal
bounds and an unchanged chrome epoch cannot reuse an old result after geometry changes. A superseded
request retains its original ticket through exact acknowledgement. Stale results cannot publish; failed
identical queries do not spin retrying. There is no synchronous geometry fallback on refusal.

## Selected Freehand original parent

The explicit selected caller supports Freehand New/Add/Subtract with one immutable drawing plan shared
by accepted Down, cached admission and the original parent. Only New may choose outline movement
after worker containment; preserving modes always draw. Accepted kind/mode cannot be rewritten by
later headers. Up supplies terminal antialiasing without adding a Move or rubber-band point.

`ExtendPreparedFreehandIfCurrent` requires the exact applied writer and latest own key, original
session/document/editor/history/token authority and captured Freehand kind. A quarter-pixel no-op
preserves its key; a successful append renews only that key. Foreign writer, history or target edits
refuse without replacing or cancelling foreign state. Capacity refusal publishes no partial selection.

Worker-created, prefaulted storage holds at most 65,536 accepted vertices. The 512-slot pending input
ring is separate from that lifetime capacity. Owner replay consumes at most sixteen accepted moves
per continuation; each metadata update visits at most one prefaulted 64-point chunk. Full path
traversal, clipping, booleans, history preparation and final graph/control retirement run on workers
under the original admission. The existing owner budget and measured G1 gates remain mandatory.

While the original selected parent owns the pointer prefix or controller, generic pointer cleanup
MUST hand it Lost and return. It cannot cancel the live writer before the original acknowledgement.
Accepted Up remains authoritative over a later loss. Lost/cancelled drawing preserves its original
selection/history; final publication and joined Close retain the original ticket, input and history
barriers through acknowledged retirement. Activation of this worker route in ordinary operation
remains disabled pending complete consumer admission, remaining branches and activation/G1 qualification.

## Guarded Polygonal metadata primitives

`ExtendPreparedLassoPointIfCurrent` supports a captured Freehand/Polygonal writer only; the existing
Freehand wrapper retains its kind restriction. `MovePreparedPolygonCursorIfCurrent` changes only
the rubber band, rejects nonfinite positions and preserves the key on repeated hover.
`RemovePreparedPolygonPointIfCurrent` returns Continues or Ended. Last-corner Pop validates the
exact current writer first, then clears only its matching draft/active marker in that same owner
transition; Pop already ends the writer, so no subsequent ActiveOwner check may reject cleanup.
The prepared handle and held snapshots retain the original arena/control charge after draft removal.

All three APIs retain original session/document/editor/history/token and own-key checks. Foreign
writer/Begin, wrong kind and consumed authority refuse without replacing foreign state. Metadata
alone changes neither committed selection, history, generation nor redo. `PolygonalLimits` and
`PlanPolygonal` describe the finite arena/worker capacities; generic drag-Up `PlanDrawing` still
refuses Polygonal. These primitives do not enable the multi-click physical parent or ordinary routing.

## Polygonal original drawing job

`DescribePolygonalDrawing` uses cached scalar metadata only. Add/Subtract and New with absent or
explicitly empty prior selection use the original `PolygonalDrawingJob`; New with a nonempty prior
refuses this drawing-only descriptor until the separate exact containment dispatcher selects a branch.
Captured document/editor/history/target, kind, mode and opening AA govern preparation; live header
changes do not reinterpret the gesture. Point/hover/Backspace consume at most sixteen accepted
metadata samples per owner continuation. Empty input parks until known input rearms the same ticket.

Nonzero, strictly increasing physical ordinals order accepted samples. Near-first closes only after
three accepted corners, and double-click closes without appending that closing point. Preserve the
selected physical close ordinal and opaque host intent before the legacy prefix normalizes its terminal;
the intent is observational host metadata, never document mutation authority. An older accepted close
wins over a younger explicit Finish, including its AA. Last-corner Backspace ends with no history.
Overflow preserves the accepted prefix and chooses Lost; no partial selection can commit.

This CPU component is registered in DocumentTests. Its physical multi-click dispatcher, New inside-hit
choice, Save/tool/lifetime arbitration, ordinary activation and measured owner budgets remain separate.

## Polygonal terminal accepted before controller admission

A physical Finish/Lost accepted before the original controller exists retains its nonzero physical
ordinal in immutable descriptor metadata. The original controller consumes that recorded terminal; a
younger opposite terminal cannot replace its outcome or AA. The scalar is observational event order,
never model authority. Claimed terminal metadata while input is still sampling, or an ordinal not
after the captured Down, refuses with `E_ABORT` before any source/geometry/history capture.

## Polygonal owner readiness and provisional closure

Optional owner-only `mayReplay` and `mayFinish` hooks yield before taking the next fixed input record.
False parks the same original job with that record unconsumed; only known input/prerequisite changes
notify it. Readiness grants no model authority and never closes the writer or captures terminal history
early. Queries MUST NOT mutate session/input state or reenter the controller. Absent hooks preserve
the original CPU behavior. Invalid successful ordinals refuse instead of parking indefinitely.

`pressResolved` reports each nonzero opaque provisional intent exactly once after older corners and
Backspaces decide it. A rejected proximity close has appended its corner; a chosen close never appends
the closing point. Preserve physical sample ordinal and opaque intent before terminal normalization.
A separately queued host descriptor has a distinct ordinal. Actual physical dispatch remains separate.

## Selected Polygonal Win32 original job

The selected test host routes Polygonal Add/Subtract and New with absent/empty prior selection through
one admitted `PolygonalDrawingJob`. Pointer Down captures fixed event scalars and one physical ordinal;
Up is nonterminal while drawing. Corners, hover and Backspace replay on the original parent. The creator
receipt completes at acknowledged Opened while the original ticket, reservation and prepared writer remain
live. Metadata-only drawing does not hold the global history barrier.

Physical Enter records its ordinal/AA before controller admission. Proximity/double-click closure is
provisional until older corners and removals replay. A nonclosing provisional request completes only its
own receipt. A selected close takes the history barrier and permits the same original job to prepare and
publish its one changed selection/history entry. A younger terminal already queued behind an older
chosen close acknowledges that earlier outcome; it does not recapture or create another entry.

Save captures committed document state, excluding transient selection outlines from the file, while the
unfinished Polygonal draft stays live, retaining the original
ticket and writer on success, dialog cancellation and encode failure. Selected tool changes and explicit
Deselect cancel and join the original parent before their own action. A cancelled Close confirmation
keeps the live draft/window; joined shutdown suppresses useful callbacks and refunds every reservation.
Direct competing layer callbacks enter the ordered host queue and unsupported intents refuse before
source mutation or another serial Selection producer. New with nonempty prior, Ctrl-lift and other
unsupported dispatcher branches remain separate; this selected gate is disabled for ordinary input.


### Polygonal export while drawing

A selected live Polygonal draft permits ordered PNG/JPEG export of committed document pixels. Successful
export and picker cancellation preserve its original ticket, writer, fixed input, committed selection and
history revision. Export leaves the project path and saved marker unchanged, including when the committed
revision was already dirty. Later corners and the chosen terminal remain on the original drawing parent.
PNG preserves transparent blank pixels; JPEG uses the existing white matte and codec tolerance. This
selected qualification does not enable the ordinary route or complete view/device/dispatcher scope.


Selected Polygonal Zoom In/Out and native pointer leave/reentry preserve the original drawing writer,
ticket, committed selection and history revision. A view change affects future physical-to-document
mapping, not captured drawing kind/mode or accepted coordinates. Pointer leave clears the transient drag;
reentry adds its next corner on the same parent. The chosen terminal still publishes the original mode's
independent selection and one history entry, with unchanged Undo/Redo and joined ownership rules.


### Selected Polygonal terminal and captured-mode semantics

In the selected original-parent Polygonal caller, Escape discards the draft without selection publication
or history. Removing its last corner also discards only that draft. A near-first click with two stored
corners records the clicked point and keeps drawing; a true double-click selects the short terminal.
The New fixture with a present empty selection clears it once; Add/Subtract preserve their prior selection
and create no history for this short terminal. The native double-click requires both time and previous-point
position criteria. Drawing mode is captured at Down; a later header-mode callback does not retarget its writer.
These requirements do not enable the remaining New/nonempty or lifted-pixel dispatcher branches.
[Selected native terminal evidence](../TestRuns/P1PolygonalTerminal_2026-10-18.md).

## Polygonal original-parent branch selection

The explicit journal overload of `SelectionOutlineGestureState` permits Polygonal in the original
`SelectionOutlineGestureJob`. It uses the existing finite Polygonal arena/geometry plans through a
private descriptor mapping; generic drag-Up `PlanDrawing` still rejects Polygonal. Complete inside
and outside peaks are admitted before immutable source/history capture. New with nonempty prior
selection runs one exact cancellable worker hit; inside moves only the original outline and outside
opens a Polygonal draft on that same ticket. Add/Subtract draw without containment.

The scalar borrowing view preserves complete event-time journal samples and chooses its branch
terminal only after consuming that exact record. Inside Up ends only the current original parent,
leaving later Down records and contexts intact. Missing release before a later Down finishes only
the old inside drag at its last sample. Later family overflow cannot cancel an already accepted Up.
Outside Up is nonterminal; hover/Leave do not add corners. Deferred near-first uses the actual first
point and captured physical view/DPI operands; double-click closes without appending its point.
Escape and loss cancel only the draft; last-corner Backspace ends without publication/history.

Journal arrival restore/target authority must still be current at original capture. Generation must
also be current except when covered by the explicit owned transition below. The physical caller must
move and validate that exact receipt; no stored arrival metadata is rewritten. Owner readiness queries
are scalar and nonreentrant. False preserves the unconsumed record; only a useful readiness event
resumes the original reservation. Terminal callback is the final state access and observes cleared
controller plus acknowledgement. Joined shutdown emits no useful terminal callback.

This CPU branch qualification does not enable the physical dispatcher or ordinary input, settle
Save/tool/lifetime arbitration, close lifted-pixel/Wand branches or establish G1.
[Evidence](../TestRuns/P1PolygonalGestureDispatcher_2026-10-18.md).


## Owned selection predecessor receipts

Only the original journal job can construct `SelectionOutlineGestureTransition`, after successful
selection-only publication. Both inside outline movement and a chosen Polygonal close may issue it;
failed, cancelled or unclosed parents issue none. `TakeSuccessor` yields its private receipt once,
after completion, controller clearance and parent acknowledgement. Default/copy construction and
move assignment are unavailable; moving invalidates its source. The journal owner must outlive every
issued receipt. This metadata is owner-only and never becomes persisted document content.

A later original journal parent validates and consumes the receipt after its complete admission and
before retaining geometry/history roots. Require the exact journal/family, a later Down ordinal,
same session/document/restore/dimensions, captured generation inside the contiguous owned range,
current exact post-document/editor/history authority and current captured target. The legacy press
protocol refuses this receipt. Missing, moved-from, retired-family, foreign or stale authority
cannot grant a generation transition. Each successful chain step retains only its proven minimum
generation and latest exact post authority; it never walks or rewrites earlier events. An actual
intermediate owned-preview generation may be covered, while generation wrap grants no credit.

These CPU receipts preserve separate original admissions, numeric selection results and Undo/Redo
for already accepted later Down events. Actual conditional host descriptors, older Save ordering,
keys, tool/lifetime settlement, ordinary activation and measured G1 remain required.
[Evidence](../TestRuns/P1PolygonalOwnedTransition_2026-10-18.md).


## Selected original physical Polygonal dispatcher

The selected Win32 journal route admits one original exact-hit parent for the first primary Down. New with a nonempty prior selection chooses inside outline movement or outside Polygonal drawing on that worker; Add/Subtract choose drawing. Each Down/key reserves its own bounded ordered host descriptor before recording scalar event-time arguments. Inside retains its atomic gesture barrier through accepted Up/loss/cancel; outside Opened acknowledges only its creator and releases the host barrier while preserving the admitted job/writer. Every later corner or key acknowledges its own conditional receipt. Up alone cannot terminate an outside family. Successful selection-only parents hand their exact one-shot predecessor receipt to an already accepted later Down without rewriting arrival authority.

Pure replay/finish readiness never admits or mutates. A separate selected terminal admission hook obtains only the exact matching history barrier after readiness and before terminal consumption/source capture. Admission refusal preserves that raw terminal and prior document/history. Consumed-ordinal progress signals the existing owner source for partial/no-change input too; all records, including ignored inside keys, count toward the sixteen-record owner quantum. Input, state and scalar transition receipts survive all borrowers and outstanding host receipts. Failure retires dependent receipts explicitly and bounded orphan metadata drains before family retirement. Joined Shutdown joins producers before destroying borrowed state and emits no useful terminal callback.

HostWrite preserves an inside drag as one atomic gesture: accepted Up must progress while Save waits, without the outside replay ordinal gate. Outside Save reads committed project content and preserves the original unfinished writer. A stopping tool command joins its cancellation before applying the tool. Temporary Space-pan retains the Polygonal writer; normal Up or actual pan capture loss ends only the view drag. Right selection input cannot enter the synchronous selection/lasso fallback. This is a Win32 routing state-machine requirement; the legacy canvas routes right input through its separate brush-only handler (`EditorCanvas.swift::rightMouseDown`). Ctrl/lift is explicitly refused by this selected partial route.

This qualification covers fifteen named hidden-WARP native cases / 139 independent facts, the full 152-stage Application roster, and twenty additional CPU owner-hook facts (159 module assertions). Held-worker/foreign/admission limits, full512/emergency/after-Up overflow, inside-Save interleaving, all export/picker/header/Undo/Redo/Deselect/lifetime/DPI/device/hidden/restart cases, Ctrl/lift, ordinary activation and measured G1 remain open. Earlier drawing-only caller receipts do not qualify those cases for this new dispatcher. [Evidence](../TestRuns/P1PolygonalOriginalHost_2026-10-18.md).

## Original Polygonal refusal and capacity boundaries

An absent document is handled without recording input, changing counters or posting a busy/overflow notice; its diagnostic receipt is E_ABORT. Exhausted family/record identities return the exact arithmetic-overflow receipt without capture or wrap. These immediate refusals must not overwrite an older pending receipt used as a readiness guard.

When TakeReady retires every stale descriptor and selects none, the same existing owner action must retire the unborrowed raw journal too. Receipt completion alone cannot leave a family alive. Document replacement before a controller exists preserves the new document/target/selection and starts no old parent. A changed target refuses all dependent accepted receipts before worker/source capture. Memory admission may create one failed scheduler ticket, but must execute zero worker stages, capture no immutable roots and open no writer.

The ninth host descriptor fails explicitly while all eight accepted Down/Up pairs remain ordered; reserved loss joins the original writer without publishing a partial selection. The complete 512-record journal plus its separate emergency record also cancels only its selected current parent. An older inside Up preceding overflow must still publish its exact move/Undo entry; a younger outside parent then cancels. Missing Up may settle only the old inside movement at its last accepted sample while preserving the younger Down. All accepted dependent receipts and family storage retire before final joined refunds. These state-machine cases extend the selected native fixture to 25 cases / 212 facts; ordinary activation and the remaining live/held-worker/lifetime/device/DPI grid stay open. [Evidence](../TestRuns/P1PolygonalAdmissionBoundaries_2026-10-18.md).


## Original Polygonal transaction ownership and inside Save

Before describing/submitting a first selected original Down, refuse a pre-existing nonzero history transaction depth. Do not fold the new selection gesture into that unrelated transaction. Refusal preserves its opaque history authority, pending depth, document generation/revision and all prior selection geometry, executes no parent or source capture, and retires all accepted dependent receipts. Its owner must still be able to close the original no-op transaction without an editor-created entry. A live original reaches its conditional path before this first-parent guard; its own inside-move transaction remains valid.

An inside move is atomic through real Up. If ordered project Save arrives after Move but before Up, keep both receipts pending on the original barrier, with no synthetic terminal, child parent or early write. A later actual Up must progress on that same admitted parent despite the waiting Save. Publish exactly one move/history entry, release the original barrier and only then capture/write/mark the saved revision. Exact Undo/Redo and joined refunds must remain valid. This requirement does not add selection persistence to the .prism schema.

[Qualification](../TestRuns/P1PolygonalForeignHistoryAndSave_2026-10-18.md).

## Original Polygonal document lifetime and export

On documentSerial replacement or Clear, an existing selected original Polygonal controller MUST receive cancellation independently of further pointer input. If parked, resume its existing ticket to acknowledge cancellation. Never create another parent or apply old geometry/history into the replacement session. Already successful corner receipts remain successful; original and family retirement must preserve the exact foreign selection or absent document and refund all retained ownership. Same-document editor/history/crop/mask changes remain separately qualified requirements.

PNG/JPEG export from a parked outside draft must retain the original ticket/draft/corners, selection, revision, path and dirty marker; exporting must not close that draft. Quiet no-selected-path cancellation has exact ERROR_CANCELLED and writes no file. Later real Enter still commits one exact history entry with valid Undo/Redo. This test seam establishes no actual OS-picker qualification. Scheduler-record exhaustion refuses the new parent with ERROR_NOT_ENOUGH_QUOTA while preserving every accepted prior job, the committed document/history and the raw-prefix terminal acknowledgement.

[Qualification](../TestRuns/P1PolygonalLifetimeAndExport_2026-10-18.md).

## Selected Polygonal ordered commands and prepared-result refusal

Typed Deselect/Undo/Redo waits for cancellation acknowledgment of the existing outside Polygonal original before its own document/history change. Previously successful corner receipts stay fixed and the command creates no replacement original or repeated source capture. Real seed Undo/Redo entries retain their exact saved revisions.

A prepared original result that loses history/editor authority through a foreign pending transaction, crop frame or mask-target change fails with E_ABORT. It removes only its own pending writer/family; committed selection, generation/revision, foreign transaction depth/authority, exact crop or selected mask identity remain with the foreign owner. Borrowed test-gate state must outlive joined work. This selected-route qualification does not activate ordinary input. [Focused evidence](../TestRuns/P1PolygonalCommandAuthority_2026-10-18.md).


## Original Polygonal keyboard command barrier

While the selected original Polygonal journal is busy, non-view/non-file commands MUST enter the
existing ordered host queue before any synchronous editor mutation. Ctrl+D, Ctrl+Z and Ctrl+Shift+Z
use the same command/ordinal/cancellation path as typed requests. The original outside writer must
acknowledge cancellation before Deselect/Undo/Redo changes selection, generation or history.
Successful corner receipts remain fixed and no replacement original is captured. The command barrier
queries only the bounded journal scalar; the older legacy Polygonal producer cannot stand in for it.
This selected-route requirement does not activate ordinary input or establish measured G1.
[Qualification](../TestRuns/P1PolygonalKeyboard_2026-10-19.md).


## Original Polygonal selection-header boundary

The selected original Polygonal journal MUST also be recognized at the selection-header callback.
Deselect queues its exact existing host command and waits for original cancellation acknowledgment.
Expand/Contract refuse before geometry, capture or a second producer and post the existing localized
Busy notice, preserving the original draft, selection/history, receipts and reservation. They cannot
fall through the older producer's guard into synchronous selection work. Later Enter/Undo/Redo retain
the original literal semantics. Header-kind/captured-setting settlement remains separate selected-route
qualification; ordinary input is not activated by this callback fix.
[Evidence](../TestRuns/P1PolygonalHeader_2026-10-19.md).
