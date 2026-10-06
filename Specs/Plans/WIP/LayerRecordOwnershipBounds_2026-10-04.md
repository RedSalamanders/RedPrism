# Cached layer-record ownership bounds (P1.9 H2.2)

## Implementation checklist

- [x] Identify deferred color display's missing prerequisite: root capture is constant-time, but preview admission and final display retirement still need record/child ownership bounds.
- [x] Build a private immutable-root cache for metadata bytes, complete conservative ownership bytes and raster node occurrences at Seal; copies and reads never walk records.
- [x] Preserve allocator ownership, allocation-before-move retry, equality, adopted buffer address and existing root goldens in the reviewed Debug candidate.
- [x] Independently prove exact metadata/capacity accounting, conservative shared/sparse ownership, overflow/unavailable refusal, empty/moved/copy/swap behavior and unchanged source roots: 87 new controls plus the unchanged 6,226-control document suite pass Debug.
- [x] Qualify the entire private DocumentTests suite in Debug/Release/ASan with 627 identical owned C++/header/HLSL/RC inputs, actual build/run exits, real ASan instrumentation and verified evidence.
- [x] Integrate cached root bounds into the managed candidate and update the owning contracts.
- [x] Consume the cache in managed color-display worker preparation, admission, publication and retirement;
  register root, planner and actual host controls.
- [x] Qualify the current identical managed source: full Debug r267, Release r268, real ASan r270,
  Release analysis r271 and all three ARM64 r38 cross-builds pass on the 867-input `023602…` freeze.
  Exact predecessor/source/product proofs and independently verified archives pass. Native runtime and G1
  remain separate below.
- [ ] Integrate and qualify the cache and display consumer in `Z:\src\RedPrism`; that checkout still has the
  older root implementation and lacks `DocumentDraft` and the color-display worker sources.
- [ ] Complete separate incremental history accounting and measured G1; cached conservative bounds do not
  establish those results.

Status: ACTIVE managed implementation; native integration and complete qualification remain open. Prerequisite of
[P1](UiNeverBlocks_2026-09-22.md) and [H2.2](HistoryStorageAccounting_2026-09-30.md).
Owners: `Common/RecordRoot.h`, `Document.h`, `DocumentDraft.h`; contracts: `Document_Model`,
`Document_History`, `Core_PerformanceAndResources` and `Testing_Validation`.

## Contract for the candidate

- A published root owns both immutable records and its immutable cached bounds. Its handle retains constant
  size and constant-time copy/read; capacity queries and metadata/ownership/node bound queries allocate nothing.
- Bounds are computed on exclusive construction/sealing before publication. Metadata covers vector capacity,
  root/control allowance, names and adjustment curves. Complete ownership adds cached raster graph bounds,
  including possible sparse assembly and shape/mask/image thumbnails; shared occurrences may overcount.
- Every operation uses checked unsigned arithmetic. An unmeasured record policy, overflow or unavailable
  child raster bound yields an unavailable bound; it does not invent zero memory or reject an otherwise valid
  root. A consumer requiring the bound refuses admission explicitly. No raster is materialized by measurement.
- Existing allocator-aware `allocate_shared` and strong sealing failure semantics remain. No mutable borrow
  survives adoption or publication. Newly cloned roots compute their own bounds; old roots stay unchanged.
- This is a conservative admission/retirement cache, not the exact retained-history index. It does not change
  `ByteSize`, `RetainedBytes`, trim/Undo/Redo goldens, budget policy or G1 qualification. H3 must still account
  late sparse assembly with its residency epoch and deduplicated retained identities.

The following deferred color display slice will use these constant-time bounds for source/new display overlap
and old-display retirement; it must retain original preview sequence, target and unrelated metadata semantics.

## Current qualification

The private candidate is `%LocalAppData%\RedPrism\PrivateWork\p1-layer-record-bounds-proof`.
It adds `RecordOwnershipBounds`, an opt-in child policy and immutable root storage holding records and their
cached bounds. `ImageLayer` measures actual retained name/curve capacities and cached raster graph occurrences.
`CanvasDocumentDraft` forwards sealing cancellation. Root handles remain 16 bytes; failed allocation or cancellation
does not consume the builder. Overflow/unavailable bounds do not prevent valid record publication.

The primary implemented the core and reviewed the cheaper agent's test draft. Incorrect copy/move, overflow,
capacity and repeated-raster fixtures were corrected before qualification. The full Debug document suite passes
6,313 controls in Debug/Release/ASan. Every archived copy is independently verified: 1,478, 1,476 and 1,481 copies
respectively; the same 627 owned sources run in every profile. The ASan compiler command tracking contains
`/fsanitize=address`. [Evidence](../../TestRuns/LayerRecordOwnershipBounds_2026-10-04.md).
The managed candidate now uses `CanvasDocument::RetirementMemoryBytes` and
`ColorDisplayMemoryBytes`/`ColorDisplayPreviewMemoryBytes` to consume the cached root envelope before
worker construction and deferred retirement. `RecordOwnershipBoundsTests`, `ColorDisplayWorkTests`,
`ColorDisplayPublicationTests` and the actual color-display host/admission fixtures are registered.
The current integration's matrix is recorded in
[Clone qualification](../../TestRuns/P1CloneReadback_2026-10-06.md); older private receipts above remain
qualified for their exact snapshots. The native checkout still lacks these consumer sources. No source
inspection or managed receipt establishes native activation, exact incremental history accounting or G1.
