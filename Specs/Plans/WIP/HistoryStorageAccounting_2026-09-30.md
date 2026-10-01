# History storage and ownership (P1.9)

## Implementation checklist

- [x] D13 explicit uint64 application limit, class default and machine-query fallback; 45 MP sizing retains undo.
- [x] Cache retained selection container capacity at geometry construction; constant-time immutable reads.
- [x] Count unique selection storage in history with the same live-document exclusion as raster storage.
- [x] Move pending snapshots and history entries; return a complete restore snapshot even when trim removes its entry.
- [x] Move the returned snapshot into the session; preserve mask targeting, crop/pending-edit and revision semantics.
- [x] Independent sharing, spare-capacity, trim, restore-after-trim and unchanged legacy history tests.
- [x] Full configuration matrix and measured real-loop history/apply costs; record outstanding P1.9 gates.
- [ ] Incremental accounting, lazy image thumbnails and first uploads; complete the remaining P1.9 work before closure.
  - [ ] H1 — Construct complete history publication before mutating live state; injected allocation refusal preserves pixels, revision, redo and pending transaction. Include whole metadata/trim peak in every migrated operation's reservation.
    - [x] Immutable views, detached publication and complete aggregate release for basic raster, gradient and pixel-move commits; all profiles pass ([evidence](../../TestRuns/PreparedHistoryPublication_2026-10-01.md)). Other callers remain open.
  - [ ] H2 — Shared immutable document/history record handles and cached owned storage indexes; capture/restore must not copy 10,000 layer records on the owner.
  - [ ] H3 — Incremental retained-storage accounting and bounded oldest-first trim/retirement; preserve current-document exclusion, identity lifetime and all trim/Undo/Redo goldens.
  - [ ] H4 — Lazy image-only thumbnails, eager semantic mask thumbnails, bounded first uploads and chrome/apply measurements; meet the complete measured G1 exit.

Status: ACTIVE. Parent: [UiNeverBlocks_2026-09-22.md](UiNeverBlocks_2026-09-22.md), P1.9.
Owner: `Common/Document.h`, `DocumentHistory.cpp`, session restore and host upload/chrome costs.

The bounded storage/move slice is qualified; [evidence](../../TestRuns/HistoryStorage_2026-09-30.md).
Incremental accounting, thumbnails, uploads and the measured apply target remain active.
The raster-edit integration also identified allocating Begin/End publication as a reliability dependency.
Its new allocation-free admission estimate includes whole document metadata and existing-history trim scratch;
that declaration does not make the owner transaction allocation-safe. H1 must construct every potentially
failing snapshot/index/entry allocation before the live document or history revision changes. Only the resulting
non-throwing ownership transfer may publish. Preparation/capture/admission failure then fails the selected
operation locally with its original document/history intact. Retain the repository's exception policy until
this explicit construction/admission boundary is implemented and qualified; do not add a catch after mutation.

## H1 publication sequence

- [ ] H1.1 — Give history entries immutable shared ownership. Capture a bounded history-state view containing
  retained entry handles, pending/depth state, current revision and a mutation epoch. A worker must not borrow
  the live DocumentHistory. Saved revision is independent: a saved older revision cannot invalidate a valid edit
  or overwrite the owner's later saved state. Preserve existing names, entry limits and undo ordering.
  Native code now implements immutable entries/opening snapshots, state views and prepared edit/begin/end/restore containers.
  Moving a view/publication explicitly removes the source's publication authority. Independent state, nested,
  saved-revision, restore-after-trim, moved-from and construction-refusal controls pass isolated Debug execution
  and strict syntax checks. Native Debug, Release and ASan Debug suites and all ARM64 clean cross-builds pass.
  Basic raster, gradient and pixel-move production commits are activated; other caller groups remain open.
- [ ] H1.2 — Prepare the before/after snapshots, entry/container growth, exact current-document storage
  exclusion, trim decision and retirement owners entirely before publication. Include all record/string/curve,
  storage-index and scratch capacity in admission. Preparation must fail locally on bad_alloc or cancellation;
  no revision, redo, pending transaction or live pixel changes occur on refusal. Zero-change publication keeps
  the current revision and redo. Every stored identity retains its corresponding raster/selection owner.
- [ ] H1.3 — Validate document token and exact history epoch on the owner before either document or history
  changes. Publish only by nonthrowing moves/swaps into already prepared storage. Keep replaced containers in
  the prepared release aggregate until the release lane retires them; no synchronous whole-history destructor
  is hidden inside the apply scope. Worker result wrappers must account for their retained input/result graphs.
- [ ] H1.4 — Wire raster edits/gradients, color commits, sizes, merges/imports, selection and stroke operations
  to the prepared boundary. Nested floating-selection transactions need explicit opening/finish/cancel state;
  they cannot use a catch after clearing pixels or inserting a temporary layer. Direct reference APIs retain
  their behavior, with production routing audited before closure.
  Raster, gradient and lifted-pixel production commits bind their complete history publication to the prepared
  target and captured token. Owner application swaps retired layer/selection/history owners into the job
  aggregate, which retires on a worker. Pending gradient/move previews and brush-commit owners also transfer
  instead of being destroyed during apply; independent weak-owner controls pass in native DocumentTests.
  Finishing a transaction reuses its immutable opening snapshot rather than copying layer records again.
  Their value document capture remains an explicit H2 cost. The complete native configuration matrix passes
  for these three groups; the other caller groups remain open.
- [ ] H1.5 — Prepare Undo/Redo restore, destination stack capacity and trim before removing the source entry.
  Even when trim discards the destination entry, the independently owned restore survives. Test each allocation
  refusal boundary and stale history epoch against literal revision, name, redo, depth and pixel expectations.

H1 removes failing construction from publication; it does not establish H2's constant-time document capture or
H3's incremental accounting. A temporary immutable-entry view can still contain value document snapshots.
Do not close H2 while any production capture/restore still copies 10,000 layers on the owner, or H3 while every
edit still rebuilds a set of all retained storages. Their measurements include metadata traversal and release,
in addition to pixels, and retain the original trim/current-document exclusion goldens.

### Native foundation qualified, 2026-10-01

Before owner capture, each migrated commit reserves its preparation/history peak and supplies a conservative
release bound for current, pending, past and future owners, plus private output and retired-preview headroom.
The bound remains in the aggregate through failed capture, preparation, apply/drop and worker destruction;
accounting cannot lower it below the 16 MiB release threshold. Raster construction caches complete descendant
capacity and node-count upper bounds, including a possible sparse assembly. Reads do not traverse or
materialize nested sparse graphs. Sharing is deliberately overcounted; overflow or depth above 128 refuses
admission. This is a transient release bound, separate from the legacy retained-history budget.

New state/publication/fault/raster-publication/capacity controls pass native DocumentTests. Full native
Debug, Release and ASan Debug suites and all three ARM64 clean cross-builds pass against the same 362 frozen
inputs; [evidence](../../TestRuns/PreparedHistoryPublication_2026-10-01.md). H1 remains open for the other caller groups and
production Undo/Redo; H2/H3/H4 and every measured apply/G1 claim remain open.

### Qualified H1.4 caller: destructive color/filter commits

- [x] Borrow immutable panel metadata to estimate the whole color/history peak before copying worker inputs.
  Capture the source document, opening token and history handles only after the full reservation exists.
- [x] Bind ready color output to its complete worker-built after-layer/history state and captured generation.
  Owner apply validates the panel/settings token and exact history epoch, then swaps layer/settings/panel owners.
  Unchanged, donorless, cancelled, failed, replaced and newer-panel outcomes retain their existing semantics.
- [x] Keep the retired panel lookup/coverage, old settings and source/history owners in the complete worker-release
  aggregate, including failed capture and every history-allocation checkpoint.
- [x] Pump admitted capture in the native harness, inject all five history failures and capture refusal,
  preserve seeded redo and source pixels, reject changed epochs and run the complete frozen configuration matrix.
- [ ] Migrate adjustment-layer OK/Cancel transaction closure separately; its live metadata semantics must remain
  intact. Production destructive-color activation alone cannot close this row or the other H1 caller groups.

The native full x64 suites and ARM64 clean cross-builds pass against the same 366 frozen inputs. All 45 independent
prepared-color assertions and actual host fault/retirement cases pass; [evidence](../../TestRuns/ColorHistoryPublication_2026-10-01.md).
The initial continuation raw-wait and Release scheduler-wake failures are preserved there. Suspension tests now
check the actual hook and include an unrelated memory wake while the worker remains held. An intermediate run
with changed inputs stopped and is excluded; only the final r3 six-configuration matrix qualifies this slice.
Adjustment-layer closure, other H1 callers, H2/H3/H4 and measured P1/G1 performance remain open.

### Qualified H1.4 caller: size-card commits

- [x] Reserve the whole resize/history peak before capturing the source, token, active id and history handles.
- [x] Bind the complete resized document to detached history publication; validate full document token and history
  epoch before nonthrowing document/draft swaps. Preserve active layer, mask target, crop and existing C16 goldens.
- [x] Transfer retired document, lasso and selection-move owners into the job's worker-release aggregate, including
  failed capture and cancelled/stale preparation. Include retained draft capacity in its pre-capture release bound.
- [x] Exercise all five history allocation failures, capture refusal and epoch-only invalidation through the host;
  retain seeded redo, literal source pixels, barrier/reservation ownership and off-owner aggregate destruction.
- [x] Integrate the draft, pass the frozen native configuration matrix and update the owning C16 contract/evidence.

The final frozen x64 suites and ARM64 clean cross-builds pass 50 independent assertions: literal
extension/nearest pixels, masks/selection/crop and active-only authority,
Undo/Redo, resolution-only, nested/no-op history, moved/foreign authority, five allocation and five cancellation
stages, and lasso capacity/owner transfer. The native host now adds the admitted capture/history/epoch faults and
off-owner aggregate release. The complete native matrix qualifies this caller; [evidence](../../TestRuns/SizeHistoryPublication_2026-10-01.md).
The first frozen sizing run passes Debug and Release, then fails ASan's existing folder save-in-place scenario.
Its original image/source identities and failure log are preserved. The initial fixture-save trace does not cover
the asynchronous in-place write, so a fixed-capacity trace-only harness gate now records that actual worker's
IO stages, native values, status and terminal receipt. A rerun passes this scenario; its original cause remains
F6 work. The final distinct r2 six-configuration matrix qualifies the sizing source against the same 370 inputs.

## Immediate implementation

### Next H1.4 caller: merge readback and publication

- [ ] Move merge source capture after metadata-probe admission, then plan on the worker. No plan succeeds without
  pixel allocation or GPU work, including a 30,000-square blank canvas. A ready plan keeps its immutable source
  and selected ordinal while a separately admitted record reserves full readback, crop/thumbnail, survivor/clipping
  graph and history/trim peaks before history capture.
  Keep the selected ordinal and exact generation/token through admitted capture, GPU tiles and final apply/drop.
- [ ] Prepare the complete survivor document, clipping-source rewrites, merged layer, active id, selected-id set
  and history on the worker. Preserve D7 budget refusal, insertion anchor, parent/group and clipping goldens.
  No owner apply may allocate an unordered set, rebuild survivor records or call allocating Begin/End.
- [ ] Retain CPU snapshot/readback/prepared result/history and retired document/selected owners in complete release
  aggregates. GPU mapping and utility-target acknowledgement stay on the owner; job retirement cannot move D3D
  cleanup to a worker. Device restart releases CPU previous-attempt state on a worker and never publishes partial pixels.
- [ ] Validate exact document generation/restore serial, history epoch, acquisition and readback device authority
  before nonthrowing publication. Explicitly define whether CPU-only processing occurs after final map acknowledgement;
  device changes after that detached boundary cannot invalidate already copied bytes. Update the existing rebind gate
  if its held-mapping predicate changes, preserving bounded restart and no writer restart requirements.
- [ ] Add literal merged pixels, clipping/group survivors and Undo/Redo, all five history fault stages, source-capture
  refusal, cancellation/epoch invalidation and off-owner aggregate retirement. Qualify full configurations and update
  the owning rendering/document/history contracts before checking this caller complete.

Current `UtilityReadbackJob` captures a value source/token before submission, prepares only the merged pixels,
and lets `ApplyPreparedMerge` allocate survivor sets/vectors and history on the owner. `DocumentReadbackJob` also
owns value CPU document storage beside owner-only GPU cleanup. This is an implementation dependency, not a qualified
merge path; its restructuring must preserve the shared export/adjustment readback lifecycle.

The primary-owned merge draft now passes strict module/host syntax with a cheap descriptor and admitted capture,
complete detached survivor/history publication, nonthrowing owner swaps and separate CPU retirement. Existing
export and adjustment-opening routes retain their current lifecycle. Independent agent tests were corrected
against actual stacking order and valid premultiplied pixels before execution. All 48 focused assertions pass,
including group/transparent behavior and D7 refusal using a physically tiny large sparse source. A cheaper agent
adds 23 independent immutable-planner/no-plan/group/multiselection/cancellation assertions, reviewed and executed
by the primary. The planner expands selected descendants once and validation indexes source ids once. The new
metadata-only probe preserves one history ordinal across readback admission; its source/cancel/stale/refusal and
huge no-plan host scenarios are drafted. This is draft proof; device/cancel/epoch/allocation/shutdown runtime gates
and native matrix qualification remain unchecked.

Published selection loops already share immutable storage and cache point counts/bounds. Cache their vector
object, outer retained capacity and inner retained capacities when the producer owns them. Opaque allocator
and shared-pointer control-block bookkeeping stays covered by global headroom. A read must not walk the outline.
History counts each immutable selection identity once across before/after and past/future, excluding the live
document's selection identity. Geometry equality still preserves no-op/redo even when separately allocated
equal loops have different retained capacities. The legacy 256 MiB raster trim expectation remains unchanged.

`End` moves its completed pending snapshot. Undo/Redo copy only the restore snapshot that history must also
retain, move the entry into the destination stack, and trim using the separately owned restore. Never return a
reference into a stack that trim can erase. Session restore receives ownership of that snapshot and moves its
document into place after checking old/new identity and mask-target conditions. Current revision, saved revision,
entry order, pending-edit cancellation and active layer/mask/crop behavior remain unchanged.

## Tests and limits

Use a small independent fixture where Undo trimming removes its destination entry, then verify the returned
document/revision/active id remain valid. Verify normal Redo ownership and revision restoration. Count shared and distinct selections across
history/live states, spare reserved capacity and a tiny budget that drops a selection-only retained entry.
Keep all existing numeric history/session/pending-edit goldens unchanged. Run all required x64 configurations
and ARM64 cross-builds; native ARM64 runtime requires another host. Timing is evidence, not an ordinary test gate.

This first slice still traverses raster identities for retained accounting. It does not claim incremental
accounting or the 2 ms apply/G1 target; continue the later checklist rows with measurements before closure.

Sizing authority also compares the captured active layer before publishing history: changing only active selection
can leave the document token current, and must still refuse a candidate with stale Undo/Redo selection. Two independent
controls cover that case. The final r2 frozen six-configuration matrix qualifies these controls.
