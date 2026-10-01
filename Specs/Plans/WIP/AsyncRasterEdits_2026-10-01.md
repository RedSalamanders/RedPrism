# Asynchronous raster edits (P1.5)

## Implementation checklist

- [x] A1 — Detached cancellable Fill/Clear/Invert preparation; literal legacy pixels, placed masks, sparse inputs, checked budgets and complete memory declarations. All configurations qualify ([evidence](../../TestRuns/RasterEdits_2026-10-01.md)).
- [x] A2 — Production commands under the selected host ordinal; capture after whole-graph admission, token-checked application, cancellation acknowledgement, one-entry undo and event-gated host scenarios. All configurations qualify ([evidence](../../TestRuns/RasterEdits_2026-10-01.md)).
- [ ] A3 — Qualify all configurations and hardware/WARP real-loop measurements; commit and push the basic raster-edit step. UI apply/history/upload overruns remain P1.9/G1.
- [ ] A4 — Ordered worker gradient rendering and application, copied palette/settings/endpoints and superseded preview cancellation.
- [ ] A5 — Lifted pixels, pixel move and floating selection Free Transform; atomic Cut preparation shared with F5, preserved private clipboard/origin and numeric goldens.
- [ ] A6 — Distortion commit/preview preparation and transform settlement continuations; preserve mapped image/mask geometry and cancellation/staleness.
- [ ] A7 — Shape mouse-up/redraw, selection-derived masks and Layer via Copy; semantic mask thumbnails prepared atomically on workers.
- [ ] A8 — All moved production callers, ordering/lifetime/failure gates and every due G1 operation measured; reconcile master and owning specs before closing P1.5.

Status: ACTIVE, executable P1.5 child; no completion claim.
Progress: A1/A2 pass all x64 suites and ARM64 cross-builds from 331 frozen inputs. F6a.1 bounded native
rollback recovery passes the same matrix, including real child/directory-reader release and exhaustion controls.
The first real-loop hardware measurement refused presentation because Windows reported displayOn=0; no timing
rows were produced. A3 remains open pending an active benchmark desktop. The qualified Release binary and DLLs
are preserved under .build/p1-raster-edit-qualified while A4 proceeds. No G1 latency or speed claim follows from
functional tests; history construction, first upload and owner capture remain measured P1.9 follow-ups.
Review: a carried-mask crop cannot exceed the pixel limit because every allocated tile checks its union with
the source/earlier tiles before allocation; snapshot alpha bounds stay inside that union. The reviewer withdrew
that proposed defect after checking this invariant. History construction was underdeclared for large layer
counts: admission now also includes complete document snapshot metadata, entry growth and existing-history trim
bookkeeping. Preflight still traverses history, and Begin/End still allocate; allocation-safe prepared history
publication and incremental accounting are required P1.9 follow-ups, with owner allocations retaining the
repository's current fatal-failure policy. This step cannot close the overall reliability/performance gate.
Owner: Common/BrushStroke.*, new Common/RasterEditWork.*, RedPrism/ApplicationRasterEditJobs.cpp,
host continuations and their document/host tests.
Owning contracts: Document_Tools, Document_Model, Document_History, Core_PerformanceAndResources,
Rendering_Brush, UI_ApplicationChrome and Testing_Validation.

## A1–A3 execution design

The production Invert, foreground/background Fill and Clear commands, plus Delete with a selection, use one
Commit job under the selected input ordinal. Delete without selection remains the existing layer/mask metadata
operation. The host settles preceding panel/transform/gradient work before capturing the target. Capture palette,
mask target, selection, canvas dimensions, target layer identity/transform/mask and independent image/mask budgets
only at that acquisition. Future gradient/distortion settlement is separately tracked by A4/A6; do not call a
still synchronous pixel settlement and declare that route nonblocking.

Declare the complete peak before submission. The admitted job first runs a trivial capture stage; its owner
hook copies target metadata and captures the full token under the held reservation, then resumes that same job.
Include input/result/token record names and adjustment curves, coverage/crossing/cell buffers, private tile bases
and final tile images, tile/index/patch growth, sparse-window splitting, one transient row/tile, stretched/carried
masks and eager thumbnails. Published raster/selection inputs are shared and not charged twice. Reject overflow,
invalid geometry or unavailable complete admission before preparation; never wait for a second reservation.

Workers borrow no session, Application, HWND, chrome or GPU engine. A worker-only raster edit uses the existing
BrushStroke geometry and exact PaintCanvas rounding, with a private CPU-only path and no coverage-engine buffers
that a raster fill does not use. Keep legacy pointer strokes and the WARP compute engine behavior intact. In
worker mode, allocation failure propagates to the runner; pixel-limit refusal and invalid/cancelled preparation
return explicit statuses, not successful empty edits. No partially initialized tile, mask or result is applied.

Prepare selection coverage and bounds privately with cancellation. Check between rows/tiles and snapshot
stages. Worker source reads use ReadRasterRegionForWork rather than shared sparse materialization. Snapshot
replacement splitting must observe cancellation while building windows; stretched and carried masks and eager
mask thumbnails use cancellable private helpers. A call already executing in an allocation/OS/library boundary
is not a wall-clock interruption guarantee. Record structural checkpoints separately from measured stop latency.

Return only the complete prepared target layer and small edit metadata. Owner application validates the exact
history acquisition, ordinal, session serial/generation/restore, retained target image/transform/full mask,
selection and canvas size. It performs one named history edit and moves prepared raster/mask/transform handles;
it does no coverage, pixel conversion, growing-mask or thumbnail work. Preserve the target's unrelated metadata,
mask placement/link/enablement and legacy history names. A grown image carries a nonuniform mask on its layer
grid with old pixels preserved and new area revealed; failure to prepare that mask fails the entire edit.

Cancellation retains the request, history barrier and reservation until acknowledgement. A late Finished result
after Escape is dropped. Younger Undo, tool/layer switches, size/import/write and close remain behind the selected
ordinal. A stale result never adds history or mutates a newer document. Failure posts one existing localized
notice outside worker preparation; retirement releases all owned data safely. Chrome/history/display uploads
remain measured P1.9 costs and must not be hidden by a worker-only elapsed metric.

## Independent validation

Retain the SelectionEditTests legacy expectations: selected red foreground, white whole background, selected
clear with unchanged exterior and undo, mask hide/reveal, scaled image with antialiased clear, selected image
inversion with retained alpha, and uniform mask selection inversion. Never rebaseline these goldens.

New controls use explicit pixel-center coordinate mapping and literal BGRA/Gray8 bytes for separately placed
masks, translation/nonuniform scale/rotation/flips, background fill through a selection, sparse base/tile/fill
regions and exact budget edges. Assert unchanged source identities and no shared materialization. Invalid,
precancelled and interrupted preparation returns no target. Memory floors are computed independently from
PixelBuffer, raster-window and coverage storage contracts rather than mirroring the planner.

Host event gates cover every basic command on image/mask targets, no-selection versus selected Delete,
before/after preparation, early/late cancellation, returned errors, actual worker/capture/admission bad_alloc,
stale image/transform/mask placement/selection/generation, reused persisted IDs, younger Undo and pending edit
settlement. Held assertions require input/history/reservation ownership and responsive view input with no hidden
Present. Terminal assertions require zero barriers, tickets, charges and unexpected history. Deadlines are failure
bounds, never performance assertions.

Run skills/format, all x64 Debug/Release/ASan suites and all ARM64 cross-builds from frozen sources. Then measure
actual command receipts and message/UI action/apply/drain/frame scopes at 4K and 45 MP on hardware and WARP,
including cancellation and a valid 100 MP scenario where supported. Report remaining overruns explicitly.

Inventory: .build/p1-raster-edit-goldens.md records the exact current/legacy anchors and six uncovered scenarios.
It is a source inventory, not runtime evidence. Move durable numeric requirements into the owning specs when
implementing A1/A2; keep the master opening checklist and this execution checklist synchronized.

## A4 execution sequence

- [ ] A4.1 — Extend detached raster preparation with the exact linear/radial gradient formula, copied straight
  endpoint colors, document-space endpoints and opacity. Validate finite arithmetic before rounding. Return a
  complete prepared layer for commits and a privately owned CPU stroke for previews. Preserve direct gradient
  APIs and all legacy numeric tolerances.
- [ ] A4.2 — Add a deferred pending-gradient mode whose opening, target, endpoints, settings sequence and line
  overlay are metadata only. Palette/header/pointer callbacks advance that sequence and never render pixels,
  selection coverage or thumbnails. Keep the direct synchronous mode for existing session tests.
- [ ] A4.3 — Admit one active and one latest pending InteractivePreview request per opening. Capture source
  layer/selection/token only under the complete reservation; copy each request's endpoint colors/settings at its
  event. Workers borrow no live session, GPU engine or chrome. A replaced CPU stroke's aggregate tile graph retires
  on the release lane; no worker mutates a stroke that the owner/composition is reading.
- [ ] A4.4 — Add a separate gradient settlement stage after color settlement and before a selected command,
  tool/layer switch, size/write or readback. A gradient commit retains that same ordinal with a fresh acquisition;
  success continues the dependent action, cancellation/failure/staleness stops it. Enter/header Apply admit an
  owned opening-specific request. Undo/Escape retain their existing pending-edit drop semantics.
- [ ] A4.5 — Exercise preview coalescing, admission/capture/worker refusal, early/late cancel, changed endpoints
  and palette after capture, mask placement and source/selection/restore staleness using real event gates. Adapt
  host observations to await acknowledgements, preserving their existing pixels and history expectations.
- [ ] A4.6 — Qualify the frozen six-profile matrix and real-loop drag/settle measurements, then commit/push.

The independent numeric anchors are `GradientTests.swift` and the port's `gradient.*` tests: linear endpoints
black/white with the midpoint near 128 (legacy tolerance 3), radial midpoint near 128 in four directions
(tolerance 5, direction agreement 1), reverse at half opacity with alpha near 128, foreground-to-transparent
over red retaining opaque red at the far end, redrag replacing the line, and Gray8 mask endpoints 0/255. New
worker controls use literal pixel-center coordinates and the documented source-over rounding for placed,
stretched and sparse masks; successful history application must preserve `Gradient` / `Gradient Mask` and exact
Undo restoration. A worker/direct comparison supplements these references and cannot replace them.

Preview GPU shading and SIMD remain P3/P2. A4 moves the existing exact CPU computation into the Interactive
lane; it does not by itself establish the one-frame drag target, first-upload bound or allocation-safe history
publication. Measure those costs explicitly and keep their owning gates open until their implementation passes.

