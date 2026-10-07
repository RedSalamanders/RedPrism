# P1 stroke host closure â€” implementation work record

## Checklist

- [x] Preserve the qualified Debug/Release/real-ASan foundation and all three ARM64 cross-builds.
- [x] Preserve r292's actual test-fixture stack warning and archive it; repair array ownership without changing kernels/expectations.
- [ ] Qualify and archive the corrected resource candidate, then commit and push; see [its current record](P1StrokeResourceControls_2026-10-07.md).
- [x] Review actual opening, finalization, queued input, GPU capacity, cancellation and shutdown callers.
- [x] Draft cached tile retirement and fixed-grid sparse workspace metadata outside frozen source.
- [x] Review delegated sparse-window tests; correct hash algorithm/indexing/pixel checks; 44 draft controls remain unqualified.
- [x] Review delegated retirement tests; correct contiguous working-tile assumption; add sparse source, compatibility healing and MoveLifted; 48 draft controls remain unqualified.
- [x] Draft allocation-free final curve/tile/healing/mask/GPU memory planning and GPU density clear; both remain unintegrated.
- [ ] Validate the complete simultaneous allocation declaration, acceptance of supported shapes, inverse-geometry range checks and numeric references.
- [ ] Implement admitted opening preparation for selection coverage and Blur samples with event-time settings/source authority.
- [ ] Implement one original final job with exact coverage leases, event waiting, history refresh and publication.
- [ ] Retain accepted input/FIFO/history authority until apply/drop; retain the last complete preview until final residency is ready.
- [ ] Retire complete CPU/GPU owners safely on success, refusal, cancellation, stale authority, device change and shutdown.
- [ ] Qualify actual host scenarios, resource pressure and measured P1 deadlines; complete canonical gates and commit/push.

Status: **implementation planning and ignored drafts; no asynchronous application activation or G1 claim**.
Parent: [P1](../Plans/WIP/UiNeverBlocks_2026-09-22.md).
Foundation evidence: [exact source and configuration receipts](P1StrokeFinishFoundation_2026-10-07.md).
Work checkout: `C:\Users\eric\.codex\worktrees\p1-opacity-host-proof\RedPrism`.
The native checkout and unrelated worktrees remain protected. All resource profile evidence is now archived; the original sampler failure is preserved. Source edits resume after diagnostic ASan r298 archive readback. The following implementation decisions require code and executable evidence before closure.

Resource follow-up: cached retirement/workspace, GPU density clear, slot readiness and the private mask sampler
are now integrated. Original-source Debug r294, Release r295, analysis r297 and ARM64 r42 pass; diagnostic-only ASan r298 passes after the preserved r296 failure. All profiles are independently verified and archived. See
[the current resource qualification](P1StrokeResourceControls_2026-10-07.md) for remaining profiles. Historical
draft rows below describe their earlier state; they do not imply final host activation.

## Actual gaps confirmed in source

`Application::FinishBrushStroke` still calls `Flush`, preview upload, session `FinishBrush`, healing/snapshot/
history and document/chrome synchronization from the owner. The foundation exposes worker APIs but this caller
does not use them. `EndStrokePreview` also scans layers to find the installed raster; carry the typed target ID
through completion instead. The final GPU result differs from the provisional wash and cannot adopt that wash.

`CanDeferCanvasBrush` admits Clone or selected older-opacity/sampler dependencies. Ordinary Paint/Eraser are
not always normalized; Heal and Blur need captured policies. The Up/Lost replay currently finishes the host
continuation immediately. Seal an accepted terminal event, keep its original ordinal/receipt/history stamp
through publication, and do not let Escape cancel already accepted final work. Explicit close/session-end
cancellation still drains exact ownership. Failed/dropped paths must not invoke a synchronous finish fallback.

Opening also needs work. `DocumentSession::MakeRasterEdit` creates selection coverage synchronously, and
`BeginBrush` calls `BlurSample` for Blur. Capture immutable opening inputs only after whole admission, prepare
the coverage/sample on a worker, then attach the actual owner graphics engine and publish the live draft after
authority checks. Capture healing/brush/smear settings at the physical event; reading later live settings would
reinterpret a delayed gesture. Preserve WARP compute and the existing legacy numeric rules.

`MakeRasterEdit` currently uses `make_shared<BrushStroke>`. Its default last-reference destruction walks vectors
and hash nodes, even if constituent raster pixels defer themselves. Admission refusal and capture failure need
the same off-owner whole-draft retirement guarantees as success. Cached retirement bytes alone do not change
that destructor. Never invent an arbitrary small bound for an unknown graph, or claim that pixel deferral covers
metadata. End the owner graphics borrow before transferring a CPU draft, including shutdown.

## Final job and GPU protocol

Use a `Stroke` job admitted before capture. Initial empty worker completion permits owner-only handle/scalar
capture into the original charged payload. Worker preparation creates final chords, sorted tile metadata and
CPU buffers. CPU coverage uses the detached worker kernel. GPU coverage records bounded batches on the owner
and reads exact structured uint values through the existing queue. Workers clamp/install them and compose
private tiles; no session or context enters worker code.

A borrowed mapped lease survives cancellation until all worker readers return. The original job opts into
deferred retirement, so failed/skipped worker stages still acknowledge every loan on the owner. Copy admission
retains its receiver until its exactly-once callback; cancellation cannot destroy that receiver while the queue
can still call it. Device generations and the original ticket are checked before installing coverage. A changed
device must fail this final operation without CPU replay or double deposition of settled density.

Wait for an available queue slot before dispatching settled density. The draft `HasReadbackSlot` scans only the
eight fixed metadata slots; it neither reserves bytes nor replaces GPU admission. A retained owner source uses
existing release/device notifications and has no readiness while capacity is unavailable. The queue already
waits for bounded staging availability. Do not repeatedly dispatch the same settled segments after a quota
failure. Whole staging, density, driver/resource, upload, old/new epoch and retirement overlap remain explicit
parts of the graph declaration and tests.

After exact coverage acknowledgement, the worker performs healing, snapshot, carried mask/eager semantic mask
thumbnail, document sealing and history preparation. Late residency refresh resumes the same ticket and retains
its original reservation. Prepare final residency before owner publication; preserve the complete old preview
until then. Owner apply checks authority and swaps complete owners. The CPU payload retires on a worker. GPU
engine/cache retirement and final upload must also meet measured owner budgets; `EndStroke` clearing a large
map is not automatically constant-time.

Shutdown joins worker producers, then an owner hook detaches this final job's engine/leases/source registration
before `_brushEngine.Release` and `_gpuWork.CloseAfterWorkers`. Final worker cleanup releases the complete CPU
payload. Raw owner borrows must already be null before a worker destructor runs.

## Draft inventory and limits

Ignored outputs under `.build/P1Drafts/StrokeFinish/`:

| Draft | Purpose | Remaining proof |
| --- | --- | --- |
| `CachedTileRetirement-r2` | Producer-updated complete tile ownership; O(1) owner read | Every mutation, unknown/overflow refusal, whole destructor routing |
| `SparseWindowBound-r1` | Cached workspace for one uniform 256-pixel replacement grid | Constructor/clipping/splitting references; no arbitrary-rectangle admission claim |
| `SparseWindowTests-r2` | 44 literal/accounting/window tests | Compilation, actual expanded inventory, runtime/matrix |
| `CachedTileRetirementTests-r3` | 48 primary-reviewed producer/ownership/pixel tests | Compilation, runtime/matrix; no claim from drafting |
| `StrokeFinishPlan-r2` | Last-three-sample cubic hull; tile/healing/mask/GPU peak | Simultaneous allocation and node/index/refresh/retirement overlap; supported shapes |
| `GpuDensityClear-r1` | GPU clear replaces owner tile-sized CPU zero vector/upload | Empty/new/reused hard/soft WARP coverage and current debug-layer diagnostics |
| `GpuDensityClearTests-r1` | Literal zero/full hard coverage; independent soft CPU tolerance | Compilation/runtime; active debug layer has 40 controls, unavailable has 38 |
| `BufferSlotPreflight-r1` | Allocation-free readiness before next density dispatch | Full/pending/lent/released/device/stopped states and actual event waiting |

The final curve hull uses the same centripetal Catmull-Rom segment expressed as cubic controls; independent
curve sampling and actual final tile lists must demonstrate containment. Existing touched tile bounds exclude
the untouched full source extent, avoiding an unnecessary full-layer healing declaration for a small dab.
The r2 arithmetic draft also fixes wide mapped-coordinate subtraction before an int cast. Inspect every
`MappedBox` caller and verify extreme inverse placement refusal before adopting that change.

The cheap retirement test r2 incorrectly expects working tile images to be sparse. Primary r3 checks their
actual contiguous representation and separately checks unmaterialized committed snapshots and sparse sources.
No production kernel or existing golden changes follow from this test correction. The analysis-helper
preparation r1 fails its observer occurrence count before writing any result; r2 corrects three literal path
occurrences and preserves the executed r1. These drafts and helper corrections are not gate results.

## Primary source review follow-up (2026-10-07)

`StrokeFinishPlan-r3` is still ignored and unactivated. It adds rounded `PixelBuffer::RequiredBytes` for future
sparse assembly, stretched mask output and carried mask output, plus source-row/column-map and sparse outside-fill
metadata overlap. Inspection of the actual `CarriedMaskForWork` shows it already fuses stretching and crop:
it allocates one output plane and optional source row, not a second full old stretched plane. This supersedes
the earlier concern about two newly allocated carried-mask planes; the retained original remains charged.
Healing's input/coverage are `std::vector`, so their exact pixel counts stay in the estimate.

The constructor checks blank dimensions before `lround`/int conversion and computes canvas/source union in
64 bits before narrowing. `TileKeys` preflights tile-aligned projected commit dimensions/pixels before a potentially
huge hash enumeration. Final coverage carries immutable kernel parameters for owner recording. Independent geometry,
refusal/cancellation and complete peak controls still precede integration; no actual host deadline is established.

The reviewed lookup draft now also requires nothrow key assignment/destruction, preserves a bounded worker sort,
cached uniqueness, first-record lookup on invalid duplicate roots and conservative zero invalid-document budget.
Pending tests cover typed allocation failure and valid document pixel budget/reference behavior.
Every activated caller still needs a complete scratch/root/history/retirement overlap check.

## Next integration checkpoint

- [x] Preserve strict ASan r298 source/product/predecessor verification and archive readback; do not call the earlier sampler failure resolved.
- [ ] Integrate UTF-8-preserving `RecordLookup-r5`, cached import retirement and independent lookup/budget tests.
- [ ] Integrate primary-reviewed `StrokeFinishPlan-r3` and `StrokeFinishPlanTests-r4`; preserve the 100M rounded-allocation fixtures and independent cubic references.
- [ ] Connect admitted opening/final jobs and whole CPU/GPU retirement to the real caller; qualify source authority, FIFO and shutdown.

`RecordLookup-r5` reapplies only reviewed code hunks to the current UTF-8 document files. Earlier full-file lookup
drafts contain damaged comments and must not be copied. `StrokeFinishPlanTests-r4` retains large sparse size-class
controls and adds the reviewed multi-tile final curve; the prior draft's second cubic-control coordinate was
incorrect, while its sampled crossing of the 256-pixel boundary remains supported by the independent formula.
All these next-stage drafts remain uncompiled and unactivated at this checkpoint.
