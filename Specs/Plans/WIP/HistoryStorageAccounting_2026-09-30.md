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
- [ ] H1.5 — Prepare Undo/Redo restore, destination stack capacity and trim before removing the source entry.
  Even when trim discards the destination entry, the independently owned restore survives. Test each allocation
  refusal boundary and stale history epoch against literal revision, name, redo, depth and pixel expectations.

H1 removes failing construction from publication; it does not establish H2's constant-time document capture or
H3's incremental accounting. A temporary immutable-entry view can still contain value document snapshots.
Do not close H2 while any production capture/restore still copies 10,000 layers on the owner, or H3 while every
edit still rebuilds a set of all retained storages. Their measurements include metadata traversal and release,
in addition to pixels, and retain the original trim/current-document exclusion goldens.

## Immediate implementation

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
