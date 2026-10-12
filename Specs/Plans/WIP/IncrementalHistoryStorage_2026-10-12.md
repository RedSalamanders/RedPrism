# P1.9 H3 — Incremental history storage accounting

Status: **WIP; private persistent-index foundation and bounded private actual-adapter Release controls qualify; native integration and ordinary activation remain open**
Parent: [P1 milestone](UiNeverBlocks_2026-09-22.md)
Contract: [Document history](../../Document/Document_History.md)
Owners: `Common/DocumentHistory.cpp`, raster residency in `Common/Document.*`, publication owners and the existing scheduler.

## Current checklist

- [x] Inspect the current index and its callers: complete map clones, current-topology traversal, charge sampling,
  bounds scans and diagnostic `RetainedBytes` traversal still occur.
- [x] Draft an immutable identity index with owning values, AVL path copying, checked cached size/height/retirement,
  transactional cancellation and original publication-memory ownership.
- [x] Independently review the cheaper agent's `std::map` reference tests and add partial-allocation failures,
  logarithmic height bounds, in-place transaction checks and shared original-owner lifetime checks.
- [x] Qualify the actual private full DocumentTests compilation/runtime and independent readback. Preserve every
  original test record and each failure. This qualifies a foundation, not the history adapter.
- [x] Compile and run the private actual `DocumentHistory::StorageIndex` adapter against the complete original
  DocumentTests suite and independent reference controls. The bounded result covers O(1) root copies, incremental
  unique self-storage bounds, and unchanged-epoch sampling skip only; see the [adapter receipt](../../TestRuns/P1PersistentHistoryAdapter_2026-10-12.md).

- [ ] Integrate the adapter into native sources and prove operation admission, original-ticket ownership, and
  bounded retirement on actual production callers.
- [x] In the private adapter, update unique self-storage contributions incrementally and skip charge sampling
  when the residency epoch is unchanged.
- [ ] Replace changed-epoch global charge resampling with bounded residency deltas and an explicit overflow fallback.
- [ ] Prepare changed live topology from changed layer/storage owners; avoid full DAG walks for scalar edits.
- [ ] Qualify original-ticket publication/retirement, real host consumers, cancellation and all G1 phases.
- [ ] Integrate and qualify exact-source native Debug, Release, real ASan, analysis and ARM64 cross-builds;
  reconcile the domain contract and close H3 only when every requirement below is met.

Foundation evidence: [qualified private reference tests](../../TestRuns/P1PersistentHistoryFoundation_2026-10-12.md),
`.build/P1Drafts/HistoryPersistentIndexCore-r2-Main/qualified.json`, `8AC83369…`. Actual Release MSBuild child
33080 exits 0, 04:22:03.249667–04:25:16.494275 UTC, zero warnings/errors. Actual DocumentTests child 76732 exits
0, 04:25:18.082042–04:25:28.521614 UTC. Independent Main readback preserves all 9,204 original DocumentTests
records, 37 previously qualified raster records and 9,055 history controls plus summary, including their
pre-runtime multiplicities. Those controls intentionally repeat over the 1,024-key operation corpus. The initial
preparation's incorrect unique-label count refuses before any build/runtime; its source, runner and receipt
remain preserved. Main corrects only the pre-runtime metadata count (98 to 96), with no changed product/test
predicate. The scope remains private CPU index foundation.

The private actual history adapter r1 was retained after its fixture failed compilation on eight fixture-only API
errors (`RedoCount` and the missing sparse-raster fill argument); no runtime started. Main r2 corrected those fixture
calls to `CaptureState().FutureCount()` and the required literal zero fill while preserving assertions. The unchanged
private adapter then built and ran in a fresh isolated packet. Its narrow result and exact evidence identities are
recorded in the [adapter receipt](../../TestRuns/P1PersistentHistoryAdapter_2026-10-12.md). This remains private
qualification only; no native source or normative contract changed, and H3/G1/P1 remain open.

## 1. Existing behavior that must stay exact

Unique raster storage is counted once across history and excluded when the live document retains it. This
includes source images, image/mask thumbnails, shape sources, sparse bases and nested tile storage. Immutable
selection identity and retained capacity count independently, excluding the live selection. Undo/Redo moves do
not change membership. Trimming removes oldest past entries, then oldest future entries, until both existing
entry and byte limits hold. Shared storage must remain charged while any surviving history entry retains it.

All owning roots, selection loops, rasters, topology objects, entries and publication controls keep their original
accounting owner. Cancellation, failed allocation, stale document/history witnesses or unavailable bounds publish
nothing. The owner validates the residency witness and swaps handles only. No owner traversal or allocation is
authorized by this plan. The reference traversal remains an independent diagnostic oracle until parity is proved.

## 2. Persistent index foundation

The private `PersistentOwnerIndex<Value, OwnerPolicy>` draft stores immutable balanced nodes. A value strongly
owns the object identified by its key. Keys use a total ordering; a lookup never dereferences a key. A copied
index retains one root. A mutation copies only a search path and rotation nodes, sharing unchanged subtrees.
No previous-version chain is retained. Sorted visitation uses a fixed stack bounded by the maximum AVL height.

Each node owns its original `SharedPublicationMemory` through both its data and shared-control allocator.
`AlwaysOnOwner` applies even to small nodes. Its sealed retirement bound includes its own allocation, its value
and reachable children; checked arithmetic refuses unavailable or overflowing bounds. The complete operation
must admit source ownership, path/rotation allocation peaks and retirement before constructing these nodes.
The index performs no admission and does not itself prove scheduler-ticket or host authority.

The independent reference corpus uses 1,024 strongly owned identities, sorted, reverse-sorted and deterministically
shuffled insertions, replacements, two deletion patterns and retained old versions. It compares exact payloads,
ordered visitation and literal retirement sums against `std::map`. Additional controls cover logarithmic height,
absent erase with no allocations, visitor stop, invalid owners/bounds, cancellation, five successive allocation
fault positions, aliasing output/source and original-owner retention through a shared leaf. Actual runtime
records must match a pre-runtime roster including multiplicity; no expected roster may be harvested from a run.

## 3. Actual history adapter

Replace the three complete map clones with immutable owning indexes for raster records, selection records and
entry records. Keep mutable preparation local to the admitted worker. Each changed record is constructed from
its previous immutable value and installed by path copying. Underflow/overflow, unknown removals or invalid
topology refuse the entire candidate.

Track cached retained charges by subtracting the old record charge and adding the new charge with checked
arithmetic. Live transitions charge zero; history-only transitions charge the witnessed resident self-storage
or selection capacity. Removing the last history reference drops a non-live record. Unchanged records retain
their exact owner and charge. Replacing cached bounds must not silently recount complete child graphs as
additional physical ownership or double-charge shared pixels.

Cached metadata/ownership aggregates need distinct definitions. Raster self-storage contributes once to a unique
ownership aggregate, while a node's deferred destruction bound must cover everything that node can release.
Entry names, snapshot wrappers, unique topology objects and unique layer roots require owning reference counts
and checked cached bounds. Any conservative overcount used for retirement must be documented separately from
the exact history byte limit. Avoid an unbounded admission inflation as history grows.

The entry membership list is bounded by the existing history entry limit. Membership changes may inspect that
bounded list, but must not clone or sample all raster records for an unchanged member set. Removing several
entries during trim must update the candidate incrementally, preserving the same oldest-first result.

## 4. Late sparse residency

The current global residency epoch and owner try-acquire permit stay authoritative. A sparse cache publishes
bytes under the existing lease; a history candidate samples under that same lease. A stale publication requires
a worker refresh before an owner swap. Querying history must never trigger sparse materialization.

A bounded change journal is a proposed next implementation, not current code. Its record key must resist
allocation-address reuse. Prefer a monotonic storage serial assigned at construction and held as immutable
data in the storage, with explicit exhaustion refusal; do not retain bare raster addresses or a journal of
publication-owned weak pointers that pins entire old job reservations. Journal storage is fixed and included
in the resource contract. It holds only serial, epoch and resident self-byte charge.

Journal writes and reads use the existing residency lease, with no extra lock ordering. A captured epoch can
consume only a complete contiguous suffix. Missing/overwritten entries, epoch gaps, invalid serials or checked
arithmetic failure refuse the delta and use a cancellable worker rescan. Never authorize a cached charge from
a partial suffix. Sampling an unrelated document's change must remain a bounded lookup/no-op. Epoch/serial
exhaustion must not wrap and authorize stale state. The owner retains the existing constant-time permit check.

Before selecting this route, prove the serial's construction/refusal contract on all charged and uncharged
storage constructors and the complete journal memory/cancellation bound. A journal design alone is not H3 done.

## 5. Changed live topology

An immutable per-layer topology retains that layer's own source/mask/thumbnail/shape raster roots and its unique
storage closure. Sharing a scalar-only edit reuses this topology. A changed raster visits only its changed
subgraph; shared children are represented by owning identity counts. The complete live topology is an immutable
aggregate of these layer contributions and the selection contribution.

Record-root, per-layer topology and storage-count changes must be prepared together. Layer insertion/deletion,
mask replacement, shape rebuilding, nested sparse replacement, transform-only metadata, image resizing and
selection changes require separate parity scenarios. Reordering layers must not change history storage charges.
No delta can be inferred solely from a reused pointer or a missing change notification: validate retained source
identities, document/history generations and the exact previous aggregate.

## 6. Proof and integration gates

1. Preserve original DocumentTests and legacy history goldens without rebaselining. Compare exact retained bytes,
   entry order, Undo/Redo, pending transactions, selection capacities and history limits to the independent traversal.
2. Exercise large shared sparse DAGs, repeated children, unrelated materializations, journal overflow, live/history
   transitions, last-reference removal, failed allocation at partial mutations and cancellation during every scan.
3. In actual scheduler tests, prove one original admission through published/retired roots, stale/cancel failures,
   last strong/weak controls and joined Close. Verify complete refund only after final retirement acknowledgement.
4. Instrument preparation work: unchanged-owner metadata updates allocate/search in proportion to changed owners;
   no full raster map clone, global resample or complete source DAG walk may hide behind a cached getter.
5. Run actual host consumers and the P1 G1 documents/operations. Owner capture, apply, UI synchronization and first
   display all remain inside their specified bounds. Separate timing evidence from structural test assertions.
6. Only after native qualification may the parent H3 checklist close. Default startup, H4, other P1 areas and the
   overall P1 Done move remain independent requirements.
