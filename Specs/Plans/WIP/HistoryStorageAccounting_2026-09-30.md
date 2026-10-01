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
