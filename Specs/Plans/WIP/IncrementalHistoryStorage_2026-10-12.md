# P1.9 H3 — Incremental history storage accounting

Latest continuation (2026-10-12): native layer-row accounting integration is complete; fresh r962 analysis / r963 Release / r964 Debug / r965 ASan qualification is active on binding `83AEC5FF3CF381779D2F3BAC5F49EDD5D7531045A1B53C925E62733F38AF30A3` (1,289 inputs / 20 contracts). The standard PS5.1/PS7 bridge passed 18 controls plus summary. Preserve r959 native Release failure, r958 analysis PASS and historical ISA refusal. See [layer-rows receipt](../../TestRuns/P1LayerRowsRetirement_2026-10-12.md); full profiles, callers, activation and G1 remain due.

Status: **WIP; private producer/scheduler/reference r7, save-admission cache, paired ISA proof, and Accounting-r2 strict original-output sum smoke are scoped PASS. Native layer-row accounting integration is complete; fresh native r962–r965 qualification is active on 1,289 inputs / 20 contracts, binding `83AEC5FF3CF381779D2F3BAC5F49EDD5D7531045A1B53C925E62733F38AF30A3`. The standard PS5.1/PS7 bridge passed 18 controls plus summary. Native profile results, callers, activation, latency, G1 and P1 remain open.** See [layer-rows receipt](../../TestRuns/P1LayerRowsRetirement_2026-10-12.md) and [owned-metadata evidence](../../TestRuns/P1OwnedMetadataRoots_2026-10-12.md).
Parent: [P1 milestone](UiNeverBlocks_2026-09-22.md)
Contract: [Document history](../../Document/Document_History.md)
Owners: `Common/DocumentHistory.cpp`, raster residency in `Common/Document.*`, publication owners and the existing scheduler.

## Current checklist

Current native qualification: integrated layer-row accounting binding `83AEC5FF3CF381779D2F3BAC5F49EDD5D7531045A1B53C925E62733F38AF30A3` (1,289 inputs / 20 contracts); r962 analysis, r963 Release, r964 Debug and r965 ASan matrix active. PS5.1/PS7 standard bridge passes 18 controls plus summary. Preserve r954 analysis failure, r958 analysis PASS, r959 full Release failure, and the historical Accounting-r1 partial-link ISA refusal. r960/r961 and ARM111 remain NEVER LAUNCHED; ARM111 held. Full profiles, callers, activation, latency, G1 and P1 remain open; see [layer-rows receipt](../../TestRuns/P1LayerRowsRetirement_2026-10-12.md).

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

- [x] Selectively integrate the persistent indexes, actual Raster journal, direct-root topology and six
  focused test pairs into native sources; preserve the reference API and original goldens.
- [ ] Prove complete operation admission, original-ticket ownership and bounded retirement on actual production callers.
- [ ] Qualify the integrated original-ticket fixture: retain its 33 controls and add five admission controls (38 total). Main fixes the cheaper
  draft's pre-admission capture, retained diagnostic strong lock and expired-object/weak-control confusion
  before its first run. Release r943 then correctly refuses its below-headroom success snapshot; Main repairs
  the snapshot and adds real refusal controls before a fresh run. Native caller migration and measured retirement remain separate.
- [x] In the private adapter, update unique self-storage contributions incrementally and skip charge sampling
  when the residency epoch is unchanged.
- [x] Qualify the private actual history topology delta adapter: direct layer roots, scalar root-group sharing,
  active parent-edge membership, cancellation, original-owner fallback and sealed producer bounds. Preserve all
  18,785 predecessor records in order and verify 212 new controls plus summary in the actual x64 Release run;
  see [topology evidence](../../TestRuns/P1HistoryTopologyDelta_2026-10-12.md). Native host/admission/retirement
  and G1 integration remain open.
- [x] Private actual-Raster serial/journal and history-delta adapter: fixed 4,096-event journal, checked
  nonwrapping storage serials/epochs, cache/event publication under the existing exclusive residency lease,
  bounded one-event lookup, stale-witness refresh refusal, cancellation preservation and a
  33-record worker-rescan fallback after overwrite. Fresh x64 Release r3 preserves all 18,733 predecessor
  records in order and verifies 50 new controls plus two roster summaries; a separately checked Raster
  integration summary reports 18 assertions and zero failures
  ([receipt](../../TestRuns/P1HistoryResidencyJournal_2026-10-12.md)). This completes private bounded-journal
  controls only.
- [x] Implement native changed-epoch bounded residency deltas with an explicit worker overflow fallback; runtime qualification remains pending.
- [x] Implement native changed-owner direct-root topology and parent-edge updates, avoiding full DAG walks for scalar edits; runtime qualification remains pending.
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

The private actual-Raster serial/journal and history-delta adapter qualifies in fresh x64 Release r3. Its 50 rostered controls and separately checked 18-assertion integration summary cover nonwrapping serial/epoch exhaustion, fixed-capacity suffix ordering and overwrite refusal, publication under the existing exclusive residency lease, exact late sparse charge, one-event lookup, stale-witness refusal, cancellation preservation, unrelated/freed events and worker fallback after journal overflow. This is private adapter evidence only: native admission (including the 96 KiB worker scratch reservation), full original-ticket ownership and retirement bounds, changed-owner topology, production host consumers, ordinary activation, H3, G1 and P1 remain open. See [P1HistoryResidencyJournal](../../TestRuns/P1HistoryResidencyJournal_2026-10-12.md).

The private actual history topology delta adapter qualifies in x64 Release r2. It preserves 18,785 predecessor records in order and adds 212 rostered controls plus summary. Direct layer roots, scalar root-group sharing, active parent-edge membership, cancellation preservation, original-owner fallback and sealed producer bounds are covered. This remains private evidence: native production host/admission/retirement, G1 and P1 integration remain open. See [P1HistoryTopologyDelta](../../TestRuns/P1HistoryTopologyDelta_2026-10-12.md).

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

A bounded change journal now qualifies in the private actual Raster/history Release candidate; the native
code remains unchanged. See the [journal receipt](../../TestRuns/P1HistoryResidencyJournal_2026-10-12.md).
Its record key must resist
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
