# Ordered selection workers (P1.6)

Status: active execution child of [UiNeverBlocks](UiNeverBlocks_2026-09-22.md). S0a/S1a/S3a core preparation
qualifies in all six clean configurations against 423 frozen inputs: 127 command and 185 move-closure checks
per x64 suite. The 39 strict RDX controls and unchanged failed Release image pass with the runtime allowlist
unchanged. The initial parser failure and r2 ASan baseline-save failure retain exact images/source/logs;
the original baseline cause remains unknown. The r3 full matrix passes. Host commands, gesture routing,
general geometry admission and measured G1 remain unqualified.
The owning selection behavior is [Document_Tools](../../Document/Document_Tools.md); existing legacy numeric
expectations remain unchanged. This plan does not add a Smooth command, which the current product does not expose.

## Implementation checklist

- [ ] S0 — Bound owned input, Clipper workspace/output and history/retirement peaks; identify library cancellation
  boundaries and measured stop latency. Overflow/refusal must preserve current geometry, history and pending input.
  - [x] S0a — Select All/Deselect use no Clipper or pixel output: account complete record/history capture,
    four-point result and retirement using the existing conservative history bounds. H2/H3 traversal stays open.
  - [ ] S0b — Boolean/offset/source geometry requires its own proved capacity and cancellation contract below.
- [ ] S1 — Prepare Select All, Deselect, Inverse, Expand and Contract geometry and complete history publication
  off owner; immutable authority and nonthrowing apply/retirement. Preserve nullopt versus explicit empty and AA.
  - [x] S1a — Sealed Select All/Deselect input/publication and 127 independent preparation checks qualify in all configurations.
  - [ ] S1b — Inverse/Expand/Contract only after S0b; reuse complete publication, preserve legacy geometry.
- [ ] S2 — Activate ordered command/header requests, capturing amounts/modes at input time and acquiring settled
  geometry only after earlier history work acknowledges. Gate every overlapping selection mutation and gesture.
- [ ] S3 — Move committed marquee/lasso booleans and layer/mask traces; own gesture arguments and exact source
  authority, retaining drafts until apply/drop. Preserve outline-move cancellation and undo semantics.
  - [x] S3a — Prepare outline-move End/Cancel history and retirement separately. The qualified native core passes
    185 assertions, preserving End's moved outline, Escape's original outline, nested name/depth, no-op/redo and
    exact acknowledged generation stamps in all x64 suites; all three ARM64 cross-builds pass.
  - [ ] S3b — Activate move/draft/gesture closure under the selected ordinal and exact retained input; the
    prepared core does not authorize synchronous host End/Cancel or altered live-stroke settlement.
- [ ] S4 — Migrate wand with asynchronous all-layer composition and worker layer sampling/flood/trace; preserve
  all existing tolerance/channel/sample/contiguous/mode semantics and source-failure reporting.
  - [ ] S4a — Qualify cancellable wand/mask-trace kernels and layer sampling before ordered host activation.
    Primary prepares a private candidate while the preceding native lease-shutdown matrix remains frozen.
    Share already contiguous pixels; otherwise read sparse pixels into private cancellable storage without
    publishing a source cache. Initialize mask/vertex storage in 4096-byte spans, and poll explicit matching,
    flood, vertex and edge walks at no more than 4096 work steps. Cancel returns no partial mask/geometry and
    a distinct cancellation result; retain existing None/Memory/TooDetailed and legacy numeric expectations.
    A cheaper agent supplies deterministic reached-checkpoint and literal parity tests; primary reviews them.
    Compile/runtime proof, paired kernel costs, original failure retention and all six native profiles remain
    required before activation/qualification. Allocator/OS latency, Clipper workspace, host ordinal/barrier
    integration, GPU all-layer sampling, memory admission and measured UI/G1 remain separate requirements.
- [ ] S5 — Bound and cache coverage and displayed geometry under exact immutable selection/grid/transform keys.
  Avoid full outline construction during every pointer/frame/chrome sync; preserve exact coverage goldens.
- [ ] S6 — Native ordering, failure, stale, cancellation, device/source and retirement proof; clean configuration
  matrix, active-desktop G1 measurements, owning specs/evidence update and commit/push each qualified slice.

## Current routes and parity

`OnCommand` calls direct Select All/Deselect/Inverse and layer/mask selection. `OnSelectionAction` supplies Expand
and Contract amounts; lasso/marquee completion calls ApplySelection booleans; wand may synchronously compose or
render the source before flood/trace. The direct engine APIs stay as reference routes. Outline move is a live
gesture and must be explicitly ordered or suspended while a worker owns its base; leaving it outside a first
kernel slice does not exempt its overlapping mutation from the host barrier.

References include `selection.*`, `modify.*`, `marquee.*`, `load.*`, `loadrefused.*`, `wand.*`, `wandsession.*` and
the exact `coverage.*` cases in DocumentTests and the corresponding legacy SelectionTests/MagicWandTests. Inverse
keeps its existing history title `Inverse`. Contract collapse retains an explicit empty selection; Deselect removes
the optional selection. Select All uses AA=true; other modifications preserve their captured AA flag.

## Authority and publication

An owned typed request retains event-time operation, amount/mode and gesture/source arguments. At its selected
ordinal, capture current immutable geometry and whole-document authority only after complete admission and earlier
settlement. Capture also retains active/mask/crop/pending-gesture identity and immutable history epoch. A source
trace retains its raster/placement/mask, and a wand sample must correspond to the same document/source token.
Workers must not access a live session, Application, HWND, chrome, immediate context or borrowed gesture storage.

Worker output binds the complete geometry to its private after-document and history publication. It includes every
snapshot, trim/index allocation and replaced geometry/display/history owner. Owner completion validates authority
before any swap and constructs no history. Cancellation, invalid geometry, source failure, allocation refusal and
stale input publish no partial outline and cannot execute a younger dependent action as success. A true unchanged
result keeps existing revision/redo semantics; acknowledged End still obeys the opaque mutation-stamp contract.

Prepare history together with the geometry result before owner publication. Full record capture and admission traversal remain H2/H3
costs until those shared-root/accounting slices qualify.

## Admission and cancellation requirements

Pinned Clipper2 2.0.1 review confirms that `BuildIntersectList` retains pair intersections in a vector, and the
offset path performs a preliminary Union and a cleanup Union after round-point generation. `CalcSolutionCapacity`
counts path slots, not vertex/workspace capacity. The eight-million-edge limit applies to wand/mask tracing;
ordinary Boolean/offset inputs have no corresponding universal limit. Coordinate range checks do not bound
allocation counts. A linear estimate based on traced-edge limits therefore cannot qualify these operations.

Current command review also confirms that Select All/Deselect leave active brush/warp/shape gestures live. Their
ToolSwitch finish/drop rules cannot be reused as settlement for a selection command. S2 must preserve those
pending owners and ordered samples under exact gesture authority, or qualify an explicitly specified ordering
policy. The settled-only S1a module does not authorize a production caller regression. Ending an outline move
also remains an allocating history caller until its prepared closure is implemented.
S1a proceeds independently because Select All creates exactly one four-point rectangle and Deselect creates no
geometry. Its sealed capture owns the before-document, history view and settled-session authority; changed output
owns the complete after-document/history publication. Geometrically equal Select All and already absent Deselect
acknowledge without changing history epoch, revision, redo or generation. Moved inputs/publications invalidate
their source explicitly. Host activation and the overlapping-writer barrier remain S2, with no functional claim
until independent preparation and native host qualification pass.

Geometry ownership already has cached retained bytes and point counts. Use those cached values before capture;
do not inspect every point in the command callback. A complete conservative bound must include input builders,
Clipper intersections/joins, concurrent workspace/result vectors, private document/history construction and release
owners. Derive the bound from the pinned implementation rather than assuming a linear multiple is universally
sufficient. An unbounded library allocation graph cannot pass S0; representable overflow refuses the operation.

Clipper calls currently offer no per-edge application cancellation hook. Record checkpoints before/after each
library call and measure the uninterrupted call separately. Do not claim a cancellation deadline from an early
token check. If the existing maximum input cannot fit a reliable admission/cancellation policy, propose and test
the exact contract change before publishing it; do not silently lower the legacy edge/shape limits.

Coverage and source raster work use private cancellable region reads and span/row checkpoints. A selected request
retains its complete reservation through final apply/drop and retirement; a memory wake alone does not mean a
worker finished. View input stays available and hidden work does not require rendering or Present.

## Required new proof

Independent literals cover operation parameters/AA, nullopt versus empty, coordinates, Undo/Redo/title, named nested
transactions, saved-revision and redo retention. Prove every injected allocation/cancellation checkpoint was reached
before unchanged-state assertions. A moved/foreign/single-use or stale selection/document/target/crop/history result
cannot publish. Keep source and geometry owners alive through off-owner retirement.

Host gates hold an operation while admitting younger Select All/Deselect/modify/gesture/wand/write/tool/close inputs;
then verify FIFO through acknowledged apply/drop, exact captured parameters, original error and zero leaked owner,
reservation, readback lease or hidden Present. The final clean Debug/Release/ASan suites and ARM64 cross-builds
qualify functionality only. Active-desktop measurement must separately satisfy the due G1 input/apply/frame rows.
