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

Status: ACTIVE. Parent: [UiNeverBlocks_2026-09-22.md](UiNeverBlocks_2026-09-22.md), P1.9.
Owner: `Common/Document.h`, `DocumentHistory.cpp`, session restore and host upload/chrome costs.

The bounded storage/move slice is qualified; [evidence](../../TestRuns/HistoryStorage_2026-09-30.md).
Incremental accounting, thumbnails, uploads and the measured apply target remain active.

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
