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
    - [ ] A5.4a — Cancellable immutable image/mask/merged sampling, exact full-grid selection coverage and
      copied-region origin; independent pixel/placement/mask/sparse/cancellation and allocation-floor proof.
    - [ ] A5.4b — Seal the selected source/private clipboard/history authority, prepare Cut pixels and history
      before publishing either part, and retain the complete replaced graph through off-owner retirement.
    - [ ] A5.4c — Admit and activate ordered Copy/Cut callers with F5 transfer/retry stages; native host failure,
      stale/cancel/younger-input proof and the full configuration matrix. Primitive proof alone leaves A5.4 open.
  - [ ] A5.5 — Deferred floating-selection opening; preserve the nested history transaction.
  - [ ] A5.6 — Worker floating merge, carried masks/distorted outline, full opening restore on failure and owned input.
  - [ ] A5.7 — Full configuration and measured G1 closeout of the complete A5 graph.
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

