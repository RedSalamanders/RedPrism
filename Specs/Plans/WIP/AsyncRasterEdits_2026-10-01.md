# Asynchronous raster edits (P1.5)

## Implementation checklist

- [x] A1 — Detached cancellable Fill/Clear/Invert preparation; literal legacy pixels, placed masks, sparse inputs, checked budgets and complete memory declarations. All configurations qualify ([evidence](../../TestRuns/RasterEdits_2026-10-01.md)).
- [x] A2 — Production commands under the selected host ordinal; capture after whole-graph admission, token-checked application, cancellation acknowledgement, one-entry undo and event-gated host scenarios. All configurations qualify ([evidence](../../TestRuns/RasterEdits_2026-10-01.md)).
- [ ] A3 — Qualify all configurations and hardware/WARP real-loop measurements; commit and push the basic raster-edit step. UI apply/history/upload overruns remain P1.9/G1.
- [ ] A4 — Ordered worker gradient rendering and application, copied palette/settings/endpoints, one active/latest pending preview and invalidation cancellation.
  - [x] A4.1–A4.5 — Preparation, deferred state, host producers/settlement and resource/outcome tests qualify in all configurations ([evidence](../../TestRuns/GradientWorkers_2026-10-01.md)).
  - [ ] A4.6 — Real-loop gradient timing and measured owner/first-upload costs; functional step committed separately.
    The two production gradient timing scenarios, 26-operation correctness smoke and eight qualified-runner
    refusal controls pass the full configuration matrix ([evidence](../../TestRuns/LiftedPixelsHost_2026-10-01.md)).
    A fresh binary/capability snapshot is preserved; full measurements remain open.
- [ ] A5 — Lifted pixels, pixel move and floating selection Free Transform; atomic Cut preparation shared with F5, preserved private clipboard/origin and numeric goldens.
  - [x] A5.1 — Detached move/lift helper and missing numeric references qualify in all configurations; clean rebuilds resolve the initial incremental layout mismatch ([evidence](../../TestRuns/LiftedPixelsPreparation_2026-10-01.md)).
    Production pointer/nudge activation, Copy/Cut and floating transactions remain A5.2–A5.7.
  - [ ] A5.2 — Deferred move model, production preview jobs and reusable opening coverage/lifted pixels.
    - [x] A5.2.1 — Functional model/host activation, whole admission, owning tokens and refusal/retirement
      controls qualify in all configurations: 52 document assertions and 20 host scenarios
      ([evidence](../../TestRuns/LiftedPixelsHost_2026-10-01.md)). Input-time Shift nudges are owned. The original
      folder-save failure is preserved and unreproduced with bounded exact-stage diagnostics; F6 remains open.
      ASan's two proven inline-table false positives have strict parser controls; the runtime allowlist is unchanged.
    - [x] A5.2a — Cache immutable opening coverage/lifted pixels across preview ticks ([evidence](../../TestRuns/PixelMoveOpeningReuse_2026-10-01.md)); displayed geometry remains P1.6 and bounded first upload P1.9.
      - [x] Bind opaque immutable coverage/lift/source/selection owners; validate exact grid, placement, selection
        identity and antialiasing on each fresh private worker stroke. Keep callback/cancellation/tile state private.
      - [x] Populate one exact-opening host cache only from successful acknowledged work; retain it across admitted
        commit capture, invalidate it on cancel/replacement/reopen/source change and retire its complete node off-owner.
      - [x] Prove one preparation across coalesced previews and commit, literal pixels and one history entry;
        preserve stale/failure/retirement controls and qualify the complete frozen native matrix.
      All 33 independent primitive/opaque-worker assertions and the actual host cache/coalescing/reopen/retirement
      controls pass native Debug, Release and ASan Debug suites; all three ARM64 clean cross-builds pass against
      the same 374 frozen inputs. Primary review corrected a test's nonexistent history accessor before execution.
      The complete preparation peak remains reserved even on reuse. No measured drag/G1 claim is made.
  - [x] A5.3 — Owned pointer release/nudge jobs, exact latest commit, cancellation acknowledgement and one named history entry qualify in all configurations ([evidence](../../TestRuns/LiftedPixelsHost_2026-10-01.md)). This caller's prepared history also qualifies ([H1 evidence](../../TestRuns/PreparedHistoryPublication_2026-10-01.md)); other H1 callers remain open.
  - [ ] A5.4 — Worker Copy/Cut preparation with an atomic private clipboard and origin, before mirroring through the F5 OS clipboard graph.
    - [x] A5.4a — Cancellable immutable image/mask/merged sampling, exact full-grid selection coverage and
      copied-region origin qualify in all six configurations; 33 independent pixel/placement/mask/sparse,
      cancellation and allocation-floor controls pass ([evidence](../../TestRuns/CopyClipboardPreparation_2026-10-01.md)).
    - [x] A5.4b — Seal the selected source/private clipboard/history authority, prepare Cut pixels and history
      before publishing either part, and retain the complete replaced graph through off-owner retirement.
      All six configurations pass against 446 frozen inputs, including 92 independent core controls
      ([record](../../TestRuns/ClipboardCommandPublication_2026-10-01.md)); the actual host transfer graph and
      measured capture/apply/retirement remain A5.4c/F5/G1.
    - [x] A5.4c — Admit and activate ordered Copy/Cut callers with F5 transfer/retry stages; native host failure,
      stale/cancel/younger-input proof and the full configuration matrix. Primitive proof alone leaves A5.4 open.
      Actual host graphs and the independent prefill, delayed-render and publication-fault suites pass the full
      private WARP run. All three x64 gates and all three ARM64 clean builds qualify 480 frozen native inputs
      ([record](../../TestRuns/ClipboardHost_2026-10-01.md)). Owner capture/apply and G1 remain open.
  - [ ] A5.5 — Deferred floating-selection opening; preserve the nested history transaction.
  - [ ] A5.6 — Worker floating merge, carried masks/distorted outline, full opening restore on failure and owned input.
  - [ ] A5.7 — Full configuration and measured G1 closeout of the complete A5 graph.
- [ ] A6 — Distortion commit/preview preparation and transform settlement continuations; preserve mapped image/mask geometry and cancellation/staleness.
  - [x] A6.1 — Qualify a detached warp kernel with private sparse reads, adopted output, executor tiles,
    row cancellation, exact legacy sampling/edge/background bytes and a checked source/output/trim peak.
    Work starts in a verified private draft while the native Shape matrix is frozen.
    The first complete private DocumentTests run passes 56 independent warp controls and all existing goldens
    against 488 frozen inputs. Its exact sources/image/log/result are archived under
    `.build/p1-distort-draft/core-qualified-r1/`. Native core qualification now passes all six clean profiles
    ([record](../../TestRuns/TransformWorkerCore_2026-10-02.md)); production host migration and G1 remain open.
  - [ ] A6.2 — Seal transform/group/floating authority and prepare complete pixel/mask/shape/history publication;
    preserve per-layer budget refusal, nested duplicate/floating history and unchanged-opening restoration.
    The detached non-floating draft passes 31 pixel controls and 64 publication/history controls plus the 56 warp
    controls and all existing DocumentTests goldens against 496 private inputs. Exact evidence is retained in
    `.build/p1-distort-draft/transform-core-qualified-r1/` (image SHA-256
    `E088D8A9C8DA694ECFA483AB1E8F3C8AF560977D6CC991CF898A9DFA603D4A20`). It closes duplicate history through
    prepared End and rejects newer draft/source/replacement authority. Group budget planning reserves later
    members' old rasters before accepting an earlier replacement; a virtual 99,990,000-pixel untouched source
    proves no final document can exceed the cap. Floating opening/merge, host activation and native gates remain open.
    - [x] Prepare an exact-predecessor history view privately, so an opening and its rollback can both be
      constructed before publication. The complete private DocumentTests run passes against 498 frozen inputs
      (image SHA-256 `4275F42AF998F661A38784059047BB893AB754E95D639089A53AB5C088CE9FFA`). A competing
      transaction with different roots, pending owner or depth cannot authorize the rollback; owning stack/pending witnesses
      preserve identity through moves. `.build/p1-distort-draft/transform-core-qualified-r2/` retains the proof.
    - [ ] Build floating opening and its complete preallocated restoration bundle before exposing the floating
      layer; prepare merge pixels, moved outline, mask growth and one outer history closure on workers.
      - [x] Private floating opening/restoration proof: 48 opening, 29 pixel and 43 exact-predecessor history
        controls pass the complete DocumentTests run against 506 frozen inputs, including all legacy goldens.
        `.build/p1-distort-draft/floating-core-qualified-r2/` retains the exact sources, image and evidence
        (image SHA-256 `CE06BB4BA6ACD163843D056EFF82A7E1A86F03A80246F4065B87A1D92861E7F3`).
        The failed r1 near-budget fixture is retained separately; the temporary canvas tile was incorrectly charged
        against the committed pixel budget. Worker Clear now clips its patches to the source grid.
        This private proof does not qualify native opening, merge or application callers.
      - [x] Seal merge success/refusal/cancellation to the exact captured draft and retained restoration;
        verify one outer entry, unchanged/failure rollback and refusal to discard newer geometry.
        The complete private suite passes 131 merge, 34 floating-pixel, 48 opening, 43 exact-predecessor history,
        64 non-floating publication, 33 transform-pixel and 56 warp controls against 510 frozen inputs.
        `.build/p1-distort-draft/floating-merge-core-qualified-r1/` retains the exact proof (image SHA-256
        `B4FFC8A9E5406D610E0F1F16098F14AC3FDC2BDCADE29CE2C16A165FA7059415`). The original 512-square near-budget
        opening and an offset source crossing the 256-tile boundary now pass without weakening the committed limit.
        All six clean native configuration gates pass against 529 frozen inputs, with 409 core controls per
        x64 suite and unchanged legacy goldens ([record](../../TestRuns/TransformWorkerCore_2026-10-02.md)).
        `.build/p1-transform-core-r1-qualified/` retains the 529-source/296-evidence proof. This qualifies
        detached helpers, including the corrected Clear budget; nested floating/cancellation extensions and
        production host opening, preview and settlement remain open.
  - [ ] A6.3 — Activate ordered preview, release, header and settlement callers; preserve pending controls,
    continuation outcomes and input-time geometry through acknowledgement and aggregate retirement.
    - [ ] T1 — Prepare transform/group/duplicate/floating opening and cancellation ownership before publication.
      Ordinary cancellation is draft-only; duplicate and floating closures preserve an existing parent's depth/name.
      Floating rollback remains preallocated for failure after exposing the temporary layer. Retain its complete
      CPU node until merge/cancel/replacement acknowledgement and release it off owner before scheduler teardown.
    - [ ] T2 — Route Ctrl+T, Apply/Cancel, pointer release/leave/capture loss, header/nudge/flip, tool/layer changes,
      writes and lifetime changes through an opening-specific owned host input and same-ordinal settlement stage.
      Worker failure cannot discard newer geometry; explicit Escape restores only its still-current opening after
      acknowledgement. A prerequisite must finish before the dependent input executes or captures its source.
    - [ ] T3 — Coalesce distortion previews into one active/latest pending worker graph with complete image,
      linked/placed mask, shape and outline owners. Publish only the captured opening/settings/source authority.
      Bound both color and mask preview dimensions; rendering retains the last complete preview while work runs.
      Remove synchronous warp/shape/outline reconstruction from the production display path.
    - [ ] T4 — Guard deferred session callers against inline commit/cancel/redraw bypass while preserving direct
      numeric reference APIs. Audit every writer, including Alt-duplicate, BeginTransform/selection transform,
      SetActiveLayerID, selection commands, flips, New/Close/Reset, source sampling and already selected requests.
    - [ ] T5 — Exercise real host success, stale, before/after-ready cancellation, reached allocation/history faults,
      refusal, reordered/newer input, hidden/device recovery, replacement and shutdown. All tickets, history barriers,
      reservations and retired graphs must acknowledge exactly once. Qualify all native profiles before activation
      closeout; capture/apply/history/chrome/upload and frame distributions remain measured A6.4/H2/H3/H4/G1.
  - [ ] A6.4 — Qualify all configurations and measure capture/apply/first-upload/G1 before closing A6.
- [ ] A7 — Shape mouse-up/redraw, selection-derived masks and Layer via Copy; semantic mask thumbnails prepared atomically on workers.
  - [x] A7.1 — Seal owned Shape release, rasterize with exact coverage and adopted pixels on workers, prepare
    the complete inserted document/history and publish through checked nonthrowing swaps. Preserve names,
    group insertion, selection, live-shape identity and the input-time foreground. The full native x64 suites
    and all three ARM64 cross-builds pass the clean r2 and final incremental r3 matrices against 487 frozen
    inputs: 76 document controls and 199 host checks, including actual release, ordinary leave and capture loss.
    Exact archives and preserved failures are identified in the [shape record](../../TestRuns/ShapeWorkers_2026-10-01.md).
  - [ ] A7.2 — Ordered production Shape release, cancellation/fault/stale/younger-input and aggregate-retirement
    gates; all configurations and measured owner/first-upload costs. Existing inline goldens stay unchanged.
    - [x] A7.2a — Actual host ordering, cancellation acknowledgement, stale/fault/drop and aggregate retirement
      qualify in all configurations. The functional slice is committed and pushed with its owning contracts.
    - [ ] A7.2b — Measure owner capture/apply, first upload and real-loop G1 before closing A7.2.
  - [ ] A7.3 — Live-shape redraw inside transform settlement, selection masks and Layer via Copy; complete
    pixel/history ownership and eager semantic mask thumbnail preparation for each caller.
    - [ ] A7.3a — Admitted selection-mask/uniform-mask preparation, transformed coverage with legacy
      hide/reveal bytes, eager edge thumbnail, exact mask target/selection/history publication and retirement.
      The complete isolated document suite passes 84 core/extra controls. The focused host proof passes 201
      controls and the complete private WARP application run passes. The subsequent native matrix qualifies settled
      callers; live producer settlement and G1 remain open ([mask record](../../TestRuns/MaskWorkers_2026-10-01.md)).
      - [x] A7.3a.1 — Settled-session native commands qualify in all six profiles against 496 frozen inputs,
        with 84 document controls and 201 actual host checks. Contracts and exact archives accompany the commit.
      - [ ] A7.3a.2 — Complete live producer/gesture settlement (P1.4/A6/S3) and measured capture/apply/upload/G1.
    - [ ] A7.3b — Layer via Copy with owned copied region/origin, no-selection duplicate semantics, complete
      inserted layer/history and target/mask state. Preserve clipboard identity and serial throughout.
      Detached preparation/publication and the full private document suite pass 34 independent controls. The
      complete private WARP application proof passes 214 Layer via Copy host checks with zero failures against
      484 frozen source inputs (executable SHA-256
      `C0439133FABB0F0AB02F19DAE9AF950CBCD711C1C8F79C2F9B6EB683A07A48F3`). The earlier r1 budget-notice
      self-test observed before its queued operation settled; r2 fixes observation timing only, with no missing
      production notice behavior. Live producer settlement and G1 remain open
      ([record](../../TestRuns/LayerCopyWorkers_2026-10-01.md)).
      - [x] A7.3b.1 — Settled production Ctrl+J commands qualify in all three x64 suites and all three ARM64
        cross-builds against 503 frozen inputs: 34 document controls and 214 actual host checks per x64 profile.
        The exact r3 archive retains all images, logs and receipts; the ISA and I/O failures are preserved.
      - [ ] A7.3b.2 — Complete live producer settlement and capture/apply/history/chrome/upload/G1 measurements.
    - [ ] A7.3c — Live-shape transform redraw with prepared carried mask and atomic after-document/history;
      coordinate with A6 so distortion, redraw and floating settlement cannot bypass the owning worker graph.
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

## A7 Shape execution design

The admitted host release owns its event-time draft and foreground color, with a transient outline until
publication. It settles preceding panel/gradient work and then captures document/history owners under the complete
reservation. The worker validates dimensions before integer conversion, uses the existing clockwise rounded
corner/ellipse geometry and float coverage order, adopts fully initialized BGRA pixels and prepares insertion,
unique naming, selection retention, live-shape identity, active target and one named history entry together.
Independent color tiles may run through the borrowed executor. A plain integer rectangle has exactly full
coverage, so it needs neither geometry nor Gray8/cell buffers. Curved coverage remains serial in its established
edge order. Its cancellation checks and memory declaration cover geometry, coverage cells, owned coverage/output,
record/name/hierarchy working sets, history and full retired owners.

Owner application checks session/document lifetime, generation, active/mask/crop authority, settled drafts and
history epoch, publishes completed history and swaps document/selection/display ownership. It expands only the
inserted parent and transfers replaced CPU owners as one aggregate to the release lane. Cancellation keeps the
selected input, barrier, record and reservation through acknowledgement. A same-ID replacement or stale history
cannot accept old pixels. Direct inline shape APIs remain legacy references; production pointer release routes
through the owned host input. Source capture and chrome/first-upload costs still need H2/H3/G1 measurements.

Private review corrected a sidecar's mixed-size `std::min` compile error and its assumption that `CreateDocument`
starts with zero undo entries. The stale-generation control now checks the exact captured history baseline;
its refusal expectation is unchanged. Failed private sources/binaries/logs are retained before repair.

### A7.3a Mask execution design

Capture a sealed full source, active target, selected layer IDs, immutable history and reveal/hide argument only
after whole admission. The worker uses layer-grid coverage through the exact inverse transform. Revealing masks
contain `255 - coverage`; hiding masks contain coverage. An explicit empty selection still exists and produces
the corresponding uniform mask over the layer grid. No selection produces the existing 1×1 all-white/all-black
mask. Adopt initialized Gray8 pixels and compute its semantic edge thumbnail before preparing publication.
Publish mask targeting, dropped selection, unchanged image pixels and complete selected-layer state with one
existing history name. No partial mask or missing thumbnail may reach composition. Cancellation/refusal retains
all source/history owners and never changes the live target. The host must settle any preceding live stroke,
transform, gradient, outline/lasso or panel according to its existing contract before the admitted capture;
the settled core alone does not qualify those producer migrations.

### A7.3b Layer via Copy execution design

Capture the settled active layer, displayed source/mask placement, outline, whole document and history after
complete admission. With a selection, reuse the qualified detached Copy pixel primitive: crop in document
coordinates, preserve origin and literal mask-as-gray behavior, then prepare a new pixel layer above the active
child with its existing parent, unique `Layer N` name, dropped outline and one `Layer via Copy` entry. Expand
only that parent at guarded publication. With no selection, duplicate the active non-group layer, including its
transform, blend/opacity, clipping source, adjustment/live shape and semantic mask thumbnail, use the existing
`name copy` and `Duplicate Layer` history name, and count shared image/mask pixels again against independent
document budgets. An explicit empty selection and a selected target without a usable image/mask do nothing.

Prepare naming, insertion, active/selected-layer target state and complete history together; no session or private
clipboard is consulted by the worker. The operation must leave private clipboard pixels, origin and serial
unchanged on success, failure, cancellation and stale refusal. Opening authority includes persisted/lifetime IDs,
generation, target/crop/mask state and history epoch. Reserve sampling/coverage/cropped output and metadata/history
peaks before capture, and retain the replaced CPU graph as one aggregate until off-owner retirement. The host
shares the original ordinal across panel/gradient/pixel-move/transform settlement and final publication.

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

- [x] A4.1 — Extend detached raster preparation with the exact linear/radial gradient formula, copied straight
  endpoint colors, document-space endpoints and opacity. Validate finite arithmetic before rounding. Return a
  complete prepared layer for commits and a privately owned CPU stroke for previews. Preserve direct gradient
  APIs and all legacy numeric tolerances.
- [x] A4.2 — Add a deferred pending-gradient mode whose opening, target, endpoints, settings sequence and line
  overlay are metadata only. Palette/header/pointer callbacks advance that sequence and never render pixels,
  selection coverage or thumbnails. Keep the direct synchronous mode for existing session tests.
- [x] A4.3 — Admit one active and one latest pending InteractivePreview request per opening. Capture source
  layer/selection/token only under the complete reservation; copy each request's endpoint colors/settings at its
  event. Workers borrow no live session, GPU engine or chrome. A replaced CPU stroke's aggregate tile graph retires
  on the release lane; no worker mutates a stroke that the owner/composition is reading.
- [x] A4.4 — Add a separate gradient settlement stage after color settlement and before a selected command,
  tool/layer switch, size/write or readback. A gradient commit retains that same ordinal with a fresh acquisition;
  success continues the dependent action, cancellation/failure/staleness stops it. Enter/header Apply admit an
  owned opening-specific request. Undo/Escape retain their existing pending-edit drop semantics.
- [x] A4.5 — Exercise preview coalescing, admission/capture/worker refusal, early/late cancel, changed endpoints
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

A4 progress: detached preparation/deferred model/resource tests and production color→gradient→dependent
continuations pass all x64 suites and ARM64 cross-builds ([evidence](../../TestRuns/GradientWorkers_2026-10-01.md)). Review corrected test assumptions about sparse
results and settlement guards, and ensured no-line redrag cannot resurrect old pixels. Exact preview generation/
sequence permits existing GPU adoption; stale previews upload anew. The next preview launches only after the old
job reservation retires. There are 105 new document/resource checks and 33 host scenarios. No latency claim yet.

Preview GPU shading and SIMD remain P3/P2. A4 moves the existing exact CPU computation into the Interactive
lane; it does not by itself establish the one-frame drag target, first-upload bound or allocation-safe history
publication. Measure those costs explicitly and keep their owning gates open until their implementation passes.

## A5 execution sequence

- [x] A5.1 — Add a detached lifted-pixel/move graph over the same CPU-only raster path. Compute coverage, lift,
  landing tiles, source-over samples, carried masks, commit metadata and moved selection privately. Check cancel
  between rows, tiles and outline batches; reject nonfinite/out-of-range mapping before integer conversion. No
  worker may materialize a published sparse source. Admission additionally includes the complete lifted plane,
  replacement windows, landing/hole indexes and new immutable outline storage.
- [ ] A5.2 — Add deferred pixel-move metadata (opening, source target, original selection, captured duplicate
  intent, rounded offset and sequence). Pointer motion updates only this metadata and the outline placement;
  one active/latest pending worker owns every pixel preview. Keep existing direct APIs as numeric references.
  A newly published preview is private to its worker until acknowledgement and immutable to the composition.
  A5.2a retains an immutable opening coverage/lifted-pixel cache for later private preview strokes. It never
  shares mutable tile/index/move state. Validate its opening token before reuse, charge the complete preparation
  peak on cache miss, and retire its last owner through the release sink. Measure reuse before the drag gate closes.
- [x] A5.3 — Retain accepted mouse-up/nudge input through a selected history ordinal. Commit the latest captured
  offset and outline as one `Move Pixels` / `Duplicate Pixels` edit. Zero offset preserves pixels, history and
  redo; cancel drops previews and blocks dependent commands until acknowledgement. Source/selection/target,
  restored generation, reopening and document replacement invalidate results before publication.
- [ ] A5.4 — Prepare Copy/Cut through one shared document-space extraction graph with F5. Capture region,
  displayed placement, mask edge tone, immutable selection and private clipboard origin after settlement.
  Copy Merged uses P1.3 region readback. Cut prepares both clipboard and cleared layer before either publishes;
  failed preparation preserves the previous private clipboard and pixels. System-clipboard refusal follows the
  existing product policy, with its publication fact independent of cancellation.
- [ ] A5.5 — Prepare floating-selection opening as a complete source-hole/floating-layer graph, with snapshot,
  raster, image thumbnail, transform and cancellation restore state allocated before installation. It consumes
  one `Transform Selection` transaction, closed only by acknowledged merge or exact restore. Account for the
  additional temporary layer and independent image/mask budgets. H1 owns allocation-safe history publication;
  H2 owns constant-time snapshot capture rather than owner copying of all layer records.
- [ ] A5.6 — Move floating merge (affine samples, carried mask and transformed/distorted selection) to detached
  work under the same transaction/ordinal. A6 supplies cancellable distortion. Failure/cancel restores the
  complete opening document; no partial source, temporary layer or moved outline survives. Plain transform
  geometry remains owner metadata. Capture draft and corner sequence before work, never from a worker.
- [ ] A5.7 — Qualify missing independent pixel/origin/outline goldens and event-gated host outcomes, preserve
  every existing clipboard/pixel-move/floating-transform golden, run all configurations and real-loop measures,
  then reconcile owning contracts, the opening checklist and commit/push.

The existing move algorithm lifts premultiplied bytes multiplied by selection coverage, clears the original
coverage unless duplicating, then applies the lifted pixels with exact whole-pixel reads or bilinear source-over
sampling. Every pointer update rebuilds from the original tile; samples cannot accumulate across previews.
Offsets round in document pixels, including on stretched or rotated layers. Clipboard extraction instead renders
on the document grid and retains the clipped selection/canvas region's integer origin. These coordinate rules
are separate and must stay numerically identical to their existing references.

Do not implement migration by sharing a mutable BrushStroke between owner and worker, by mutating a live session
on a worker, or by accepting a successful empty result after failed preparation. The preview mailbox retains one
active ticket through cancellation acknowledgement, and the next request can start only after its whole graph
reservation retires. Selection-overlay geometry caching and bounded owner uploads remain P1.6/P1.9 gates.

