# Document history

Status: current normative product contract
Last reviewed: 2026-10-05
Owner: `Common/Document.h`, `Common/DocumentHistory.cpp`

## Scope

This contract owns undo/redo for `CanvasDocument`. It does not own tools, I/O, or GPU tiles.

Independent history expectations are retained in the document tests.

## Budget

Default `entryLimit` is **100**, clamped to ≥0 at construction. The class and default `DocumentSession`
`retainedByteLimit` remains **256 MiB**. The byte limit is an unsigned 64-bit value, including values ≥2 GiB.
The application supplies an explicit session limit of **min(physical RAM / 4, 2 GiB)**, using integer division;
a failed physical-memory query falls back to 256 MiB. The limit is selected once at application construction.
`retainedBytes(current)` counts unique raster storage that appears only in history snapshots
(before/after), excluding images still referenced by the live document, using pointer identity of `Raster`
storage (Swift `ObjectIdentifier` of `CGImage`). Thumbnails and the retained `shapeImage` source count separately when they are distinct storage. The live
document excludes those same owners before history charges are accumulated.
Immutable selection storage also counts once by its held `SelectionLoops` identity, excluding the live
document's selection identity. Its cached charge includes the outer vector object, outer retained capacity
and each loop's retained point capacity; spare capacity counts. Opaque allocator/control-block overhead is
covered by global headroom. The byte query never traverses selection points. Empty geometry owns no storage.

## Managed P1.9 H3 storage index

The prepared path uses sealed immutable AVL indexes for entries, rasters and selections. Keys retain their
owning values. Raster records use nonwrapping storage serials; selection and entry keys retain the object whose
address identifies them. A copied index shares one root. Changes copy search paths and rotations, preserving
unchanged subtrees and their original publication-memory owners. No previous-version chain is retained.
Transferring an entry between Undo and Redo leaves membership unchanged. Complete live raster/selection
identities are excluded. Checked reference counts and cached unique contributions preserve exact retained-byte
limits. Cancellation, allocation failure and arithmetic refusal discard the private candidate.

Prepared topology retains deduplicated direct image, thumbnail, mask and shape roots. Exact equality of those
source fields reuses the root group for scalar or selection edits. Children of a sparse raster change membership
only when that parent's history or live reference count becomes active or inactive. New parents are activated
before old parents are removed, preserving shared descendants. Scalar edits visit direct roots, without walking
the unchanged sparse DAG. The reference topology API and reference history methods retain complete traversal
for diagnostics; `RetainedBytes` reads fresh resident bytes and does not authorize a cached trim decision.

Sparse cache publication appends one value-only serial/epoch/self-byte record under the existing exclusive
residency lease before publishing pixels and the matching epoch. The process journal has 4,096 records and no
pixel, weak-control or accounting-owner references. Serial and epoch exhaustion refuse without wrapping.
An unchanged epoch needs no owner scan. A complete contiguous journal suffix updates only matching retained
records. Overwrite or unavailable witnesses require a cancellable worker rescan; partial suffixes never authorize
publication. The existing owner permit still validates the exact sampled epoch without waiting.

Cached retirement charges unique raster self-storage capacity (including future sparse assembly), selection
capacity and layer/index/entry metadata. History-stack own metadata is an O(1) sealed-capacity calculation.
Pending, unmatched and additional-source bounds remain conservative. Partially constructed indexes stay uniquely
producer-owned; only successful checked bounds permit shared immutable publication. Owner publication swaps the
old index into the worker-retired result. Sealed indexes use `AlwaysOnOwner` with their true byte bound, including
small graphs; other release policies and the release-node layout remain unchanged. Workers without an owner TLS
sink destroy inline. Root groups and topology wrappers are sealed while uniquely producer-owned before shared
publication. Unique metadata deduplicates shared root groups separately from conservative retirement bounds.
Admission uses cached occurrence/metadata bounds for node and refresh overlap plus 96 KiB of fixed journal
worker heap scratch, allocated before acquiring the lease. Source capture and construction remain behind complete operation admission.
The previous map candidate's [historical matrix](../TestRuns/HistoryStorageIndexCandidate_2026-10-05.md) does not
qualify these changed sources. Private foundation, journal and topology controls pass; native configuration,
production admission/retirement cost, ordinary activation, remaining reference callers and G1 are still required
by the [H3 plan](../Plans/WIP/IncrementalHistoryStorage_2026-10-12.md).

## Transactions

`Begin(name, document, selection)` / `End(document, selection)` nest. Only `depth == 0` records an entry.
If the ended document equals the pending before-document, **do not** push an entry and **do not** clear redo
(selecting, navigating, and no-op edits preserve redo). A real change assigns a new revision, appends the
entry, and clears redo, then trims.

Undo/redo are ignored while `depth != 0`. Undo restores the entry’s before snapshot (document + active layer
id + revision). Redo restores after. Entries, opening snapshots and pending records have immutable shared
ownership. Finishing a transaction reuses its opening snapshot handle; it does not copy its document again.
Each restore result independently owns its document, active id and revision before trimming can erase
the destination stack entry. Session restoration takes that result by value and moves its document into place;
it preserves the existing crop, pending-edit, document/restore serial and mask-target rules.

A pending edit that is not a history entry yet (gradient, transform, brush, warp, pixel move, shape draft) is
dropped by the first Undo and by the first Redo, and history does not move on that call; an outline being moved
(its entry is open until the drag ends) goes back where the move began the same way. The host consults its edits
in one order for Escape, Enter, Undo and a settle ([`UI_ApplicationChrome.md`](../UI/UI_ApplicationChrome.md) Canvas
interactions). A crop frame is not an edit in progress (the Crop tool always shows one, the whole canvas by default):
Undo and Redo drop it and still move history, as legacy `restore()` cancels the crop; with nothing to undo the frame
stays. An open color panel also
consumes the key: history stays put until OK or Cancel. The host drops any composition stroke in the
same command. The save prompt (`ConfirmDiscardChanges`) settles nothing before it asks: a pending edit that Save
would record (a live stroke or warp, a pending gradient, a changed transform, a typed opacity, an open color panel) counts as unsaved
work beside `IsModified`, so a clean document with one still asks; Cancel leaves it pending, Save settles and
writes it, and Don't Save settles it as the document goes ([`UI_ApplicationChrome.md`](../UI/UI_ApplicationChrome.md)
› Files).

`MarkSaved(revision)` stores the revision of the document snapshot actually written to disk. Revisions are opaque
identifiers; an older revision restored by Undo is a valid saved revision. If writing snapshot A completes while
the live document is at revision B, B remains modified; undoing to A is clean and redoing to B is modified. The
no-argument `MarkSaved()` stores the current revision for synchronous callers. `IsModified` is revision ≠ saved
revision, regardless of whether the saved revision remains reachable in retained history. Asynchronous save's
scheduler MUST enforce completion ordering and session identity and supply the written snapshot's
revision. Production save supplies the revision of its captured snapshot.
`Reset` clears stacks and starts a new saved revision.

## Prepared publication

`CaptureState` retains immutable entry/pending handles, revision, transaction depth and a structural epoch under
the operation's whole memory reservation. The view never borrows a live history container for worker reads.
Past/future stacks have shared immutable container roots. `CaptureState` is nonthrowing and copies two root
handles and the pending handle without allocation or per-entry traversal. Reads, including non-const iteration,
never clone. Detached mutation constructs a completed private root before replacing the previous root; a relaxed
`shared_ptr::use_count` is never permission to mutate formerly shared storage. Failed construction leaves both
captured and live roots unchanged. Prepared stack growth/trim charges its private container and handle peaks;
retirement includes retained root capacity. Each completed stack root additionally caches complete conservative
metadata, ownership and raster-node occurrence bounds. Its producer measures immutable entry/snapshot bounds
before publication; queries and handle copies never revisit entries, layers, names, curves or selection points.
Default roots own nothing and have valid zero bounds. Null/unavailable/inconsistent/overflowed child bounds make
the complete bound unavailable while preserving the valid root and contents. Admission MUST refuse unavailable
bounds. Current/pending/selected-restore documents use their sealed layer/selection caches. Full document record
capture, exact unique retained-storage accounting and native activation remain separately qualified.
Begin, End (including nested/no-op completion), Undo, Redo, Reset and prepared publication invalidate that epoch.
`MarkSaved` remains independent and does not invalidate a prepared edit or allow it to replace a later saved
revision. A view from another history owner, or a moved-from view, has no publication authority.

`FollowingPreparedState` prepares a closure against the exact state that an unpublished predecessor would install.
It advances the expected structural epoch by one and retains the predecessor's completed past/future roots,
pending-record owner and depth. These witnesses must match at `IsCurrent` and `CanPublish`; a transaction with
different roots, pending owner or depth cannot authorize the closure even at the same epoch/revision. Identical
nested states are structurally equivalent; session publication also validates its document and target authority.
Already published, invalid or epoch-overflowing
predecessors refuse. Owning moves preserve these witnesses and invalidate the moved-from view. This permits a
floating opening and its complete rollback history to be allocated before either reaches the live session.

Detached edit/begin/end/restore preparation performs all snapshot, entry, stack-growth and trim allocations
before changing live state. Cancellation or `bad_alloc` fails the selected operation with its document, revision,
redo and pending transaction intact. A prepared nested edit preserves the opening transaction and its name;
an unchanged edit preserves redo/revision. Preparation of End reuses its immutable opening snapshot. Restore
owns its payload independently even when the trim decision drops both stack references to the original entry.

Publish validates exact owner, epoch and revision, then swaps prepared containers without allocation. It is a
single-use operation; moving its result removes the source's authority. Retired history containers remain in
the result until the worker release lane retires the complete owning aggregate. Raster/gradient/pixel-move
commits bind history and prepared pixels to one private result and validate their document token before either
publishes. Old live layers, selections, brush-commit metadata and pending previews transfer into that aggregate too.
Owner publication neither constructs history nor destroys their large retired graphs.

The migrated jobs set a conservative complete retirement bound before capturing document/history owners.
It covers current, pending, past and future snapshot capacities, names, curves, held rasters and selection,
plus private preparation and replaced-preview headroom. Cached raster bounds include possible sparse assembly
and nested descendants; reads do not recursively expand shared sparse graphs. Sharing may be overcounted;
overflow or excessive depth refuses admission. This transient bound remains at least the worker release
threshold through every failure/apply/drop path. It does not replace unique retained-byte accounting or change
the oldest-first trim policy. Construction-cached stack/document bounds remove metadata traversal from these history admission queries. Other
callers and full native record capture remain H2 work; unique storage topology and late residency remain H3.

Direct reference methods retain their existing behavior; caller migration remains P1.9 H1.4/H1.5. The prepared
boundary does not qualify constant-time document capture, incremental storage accounting, other operation
groups or the measured G1 bound. These remain explicit H2/H3/H4 work.

`PrepareEdit` may receive an already admitted publication-memory owner. Newly constructed topology,
snapshot, entry, stack and storage-index controls retain that owner; reusing an existing immutable control
preserves its original owner. Trim clones new containers with the new publication owner. The value graph
is destroyed before its accounting owner, and the shared allocator retains that owner until the last weak
control has actually deallocated. Owner-side final strong release uses deferred worker destruction, including
small graphs. An owned null-valued alias still has ownership; an empty shared handle keeps the direct
reference allocation/destruction behavior. The factory performs no admission. The caller must declare all
new capacities/control overlap and bind the original reservation before publication. This lifetime protocol
does not close unique resident accounting, late residency, ordinary activation or G1.

Destructive color/filter commits use this boundary. Their complete color result,
candidate layer/history, captured generation and retired settings/panel form one private publication/release
owner. Capturing their input follows full admission; no failure after mutation is caught. Other callers and
adjustment-layer cancellation routing remain open until their separate migration gates pass. The full native
configuration matrix passes for destructive color; [evidence](../TestRuns/ColorHistoryPublication_2026-10-01.md).

Adjustment-layer opening and OK also prepare their complete history publication off the owner. Opening admits
one descriptor/readback/history graph before source capture. Its worker retains the original records and builds
the sampling-only visibility snapshot, leaving the live document unchanged. The final mapped tile is acknowledged
and utility surfaces released before the worker prepares the panel, original parameters and named Begin. Only
exact document lifetime/generation/sequence, pending adjustment id, active/mask/crop and history epoch may install
that panel. A later device change cannot invalidate already copied opening pixels. Failed opening installs no
panel or transaction; it clears only its own pending editor id.

Metadata OK captures its exact opening/settings sequence, original parameters, source and immutable history under
complete admission. Worker End reuses the retained opening snapshot and builds the final document with cancellable
layer copies. Owner application validates all authority before nonthrowing document/history/panel/original/display
swaps. Every acknowledged End advances the opaque mutation stamp, including unchanged or nested closure, so old
whole-document tokens expire. Stored-record equality still preserves revision and redo; promoting omitted HSV or
Exposure settings to explicit defaults retains its existing real-entry semantics. The global filter template and
source pixel bytes remain unchanged. Capture/construction/cancellation/staleness preserve the panel, document,
pending transaction and retained redo. Complete old owners retire on the worker lane.
Explicit Cancel owns an opening-specific input behind any earlier apply/drop acknowledgement. Repeated Cancel for
the same document/opening reuses its pending observer before retiring queues; it MUST NOT cancel its own selected
closure. Adjustment Cancel prepares original-record restoration and End through the metadata publication graph.
Destructive dismissal captures a fixed authority and immutable history view, preparing no End or document copy.
Its application swaps panel/display ownership into the release aggregate and advances the panel sequence only;
pixels, active target, generation, history epoch/depth/revision/redo and global template remain unchanged.
Both variants retain the panel on admission/capture/preparation failure and validate exact authority before swaps.
The standalone Cancel receipt finishes after close acknowledgement without ordinary OK settlement. Cards and local
discard use a same-ordinal prerequisite; atomic file replacement retains its qualified discard-on-success aggregate.
No caller may enqueue a prerequisite behind itself or read admission as publication. Local reset construction,
whole document capture/admission traversal, incremental accounting, first upload and measured G1 remain separate.
Opening/OK qualify in their [record](../TestRuns/AdjustmentOpeningAndOkHistoryPublication_2026-10-01.md);
Cancel caller qualification is tracked in the [cancellation record](../TestRuns/PanelCancellationHistoryPublication_2026-10-01.md).

Size-card commits also bind their complete worker-prepared document and detached history to one private result.
Their source/token/history capture follows complete admission. Apply checks the whole document token, captured
active layer and history epoch before publishing and swapping the document and retired outline/selection-move owners. Active layer, mask
target and crop semantics remain unchanged. The full configuration matrix passes for this caller ([evidence](../TestRuns/SizeHistoryPublication_2026-10-01.md)); measured apply/G1 remains open.

Production Undo/Redo captures constant-sized session, document/restore serial, generation, active/mask and crop
authority, including no-document Redo. Its whole reservation includes the selected restore snapshot's metadata,
even when it is larger than the live document. The worker prepares the restore, selected ids and history;
publication validates authority and exact epoch before nonthrowing history/document/display swaps. Old owners
remain in the deferred aggregate. Empty stacks/open transactions return before capture, preserving crop and redo.
A later saved revision stays independent of restore preparation. All six configurations pass
([evidence](../TestRuns/HistoryRestorePublication_2026-10-01.md));
shared record capture, incremental accounting, other callers and measured G1 remain separate.

Image import and project opening now prepare replacement history off the owner. A new image establishes a fresh
saved baseline; its additional successful images form one named entry. Existing-document imports retain the
saved lineage. Project opening establishes a clean baseline. The generic replacement boundary selects nothing
for absent/unknown active ids; project parsing retains its existing manifest active-id validation.
Complete source/history capture follows whole-graph admission. Worker preparation owns final records, selected
ids and reset/edit/trim containers; apply checks file-operation session authority and exact history epoch before
nonthrowing document/history/target/display swaps. Old session/history/path owners retire in the aggregate.
Ordinary edits preserve a later saved revision; validated replacement deliberately installs its new baseline.
File-operation authority permits live drafts and open transactions; Undo/Redo's settled-only authority remains
separate. Existing imports inside a transaction preserve its opening, depth and name. Successful replacement
resets that transaction and swaps all transform/floating, lasso, outline move, brush, warp, gradient, shape,
pixel-move and color-panel owners into its already allocated retirement holder. Their complete CPU bounds are
declared before capture. Borrowed brush coverage engines close/detach on the owner before CPU destruction moves
to the worker; GPU resources retain application ownership. Already admitted preview graph peaks cover derived
buffers that can arrive during IO; later mutating inputs wait behind the selected ordinal. Host UI drag/card
state clears after publication without committing an old edit against reused layer ids.
Cancellation, construction refusal and stale authority preserve live state and pending drafts. The frozen
six-configuration matrix qualifies this caller ([evidence](../TestRuns/ImageImportHistoryPublication_2026-10-01.md));
shared metadata capture and measured apply remain separate.

New preserves one `New Canvas` entry from an absent document to the requested blank canvas; its final revision
is marked saved. Undo returns to no document and Redo returns to the saved canvas. Close Project resets history
to an absent, saved document with no entries. Closing an already absent document leaves its existing redo
lineage intact. `PrepareSavedReplacement` and `PrepareDocumentLifecyclePublication` construct these optional
states and the replacement owners privately before publication. Cancellation, invalid input and stale
authority preserve the live state. Host New/New Canvas/Close now capture sealed session authority and immutable
history roots after complete admission, without copying the old document records. Workers prepare the optional
replacement; owner publication transfers the old records and drafts into its prepaid retirement holder. The
source-owning preparation overload remains a compatibility wrapper. The selected host ordinal, captured card
invocation/dimensions and exact history epoch remain authoritative through apply/drop acknowledgement. Path
clearing, viewport Fit and rebinding that selected input to the new lifetime follow successful publication only.
Already queued inputs for the old lifetime become stale. Admission traversal and measured G1 remain open;
combined production qualification is tracked in
[`DocumentLifecycleWorkers`](../Plans/WIP/DocumentLifecycleWorkers_2026-10-02.md).

Prepared selection commands and outline-move closure bind complete worker-built history to their sealed owning
input. Select All/Deselect preserve history epoch/revision/redo on geometric equality; changed commands prepare
one named entry or preserve an existing outer transaction. Outline End always acknowledges its transaction,
including an unchanged outline; Cancel first restores the retained origin and then acknowledges End. Cancellation
of work, failed construction and stale whole-document/target/crop/origin/history authority leave the live move and
history unchanged. A later saved marker remains owned by the session. Publication swaps old geometry/display/
document/history owners into the result for worker retirement. These core boundaries do not activate production
selection/gesture callers or close shared capture/accounting and measured G1 gates.

### Settled layer metadata

`PrepareLayerMetadataPublication` constructs the complete metadata document and one legacy-named undo entry
before owner mutation. Rename, Show/Hide Layer, Layer Opacity and Layer Blend Mode preserve every unedited record,
pixel/mask handle, selection and active target. No-op acknowledgement does not publish history or clear redo.
The sealed input is consumed by move; changing and no-op results are single-use. Session/document replacement,
generation, active/mask target, crop, a reopened draft or history epoch changes refuse publication before either
history or document changes. Cancellation and construction failure preserve the complete live state. Successful
application swaps old document/display/history into the prepared result for worker retirement under the admitted
reservation. A later saved revision stays owned by the session.

The independent reference is the state contract above: `LayerMetadataWorkTests` checks literal values, undo/redo,
ownership, move consumption and failure checkpoints. `metadata-host` exercises the actual ordered job and caller
routes, held-message pumping, stale results, scheduler refusal and off-owner retirement. Qualification is tracked
in [DocumentRecordRoots](../TestRuns/DocumentRecordRoots_2026-10-03.md). Typed opacity previews/closure and shared
record capture/accounting remain separate work.
The native settled slice also preserves the reference route for commands during live gestures and nested
transactions. Its literal host checks retain the gesture, edited field and outer transaction name/depth. Migrating
that route to preserving worker authority remains required before metadata coverage or root activation is complete.

## Storage topology

Worker-prepared opening/final snapshots retain an immutable `HistoryStorageTopology`. Its producer holds the
layer root, selection loops and every distinct raster owner while taking the complete iterative base/tile
closure, including sparse tiles nested inside sparse tiles. Image/mask thumbnails and shape caches participate.
No pixel cache is materialized. Matching checks held layer and selection identities in constant time; a
mismatched topology refuses the snapshot admission bound. Recent pending/past/future snapshots can supply an
exact matching topology through a fixed five-candidate check, avoiding repeated producer walks for those roots.
Index object/control allowances and retained vector capacity are included in snapshot ownership bounds.

The index topology retains identity owners. Its prepared resident-byte total is sampled under a raster residency
lease and is valid only with the index's `residencyEpoch` witness. A later sparse-cache publication advances the
epoch; `RetainedBytes` independently reads fresh resident bytes for diagnostics. Prepared trim builds/reuses
its live-source index on the worker. Reference methods retain their existing document scan with one iterative
pending-owner vector and their shared dedup set; they construct no temporary topology object. Both routes take
the complete nested closure and preserve oldest-first removal. Migrating the remaining synchronous reference
callers and measuring their costs remain P1 requirements. `RetainedBytes` remains a reference/diagnostic query.

Late materialization may change byte charges after preparation. Staged publication below prevents a stale trim
decision for opted-in callers. Incremental unique accounting and final native qualification remain tracked by
P1.9 H3 before the milestone can be complete.

## Staged sparse-residency publication (opt-in)

A caller using indexed prepared history with sparse rasters MUST treat the sampled resident total and its
`residencyEpoch` as one witness. The worker MUST take the complete coherent charge sample while holding a
`Raster::ResidencyLease`; this lease is nonmovable, noncopyable, thread-affine, and cancellation-aware. The lease
MUST be released before callbacks, waits, or transfer of prepared work to another thread.

At owner completion, an opted-in caller makes exactly one nonblocking `ResidencyPermit::TryAcquire` decision
for that completion attempt. `Ready` permits the caller to continue ordinary authority checks and publication;
it does not grant document or session authority. `Busy` means the process-wide lease is occupied. `Refresh` means
the sampled epoch no longer matches the current residency epoch. `Invalid` means the publication is ineligible
for this protocol. The owner MUST NOT wait, allocate, read pixels, rebuild an index, or run callbacks while
holding the permit. On `Busy` or `Refresh`, it releases any permit and resumes the same ticket on a worker using
the original prepared intent and publication authority. The worker waits for the lease with that ticket's
cancellation token, then refreshes resident charges against the same prepared intent. If the epoch witness is
unchanged, refresh MUST retain the existing index without cloning or rebuilding it. A changed witness permits
the worker to prepare a refreshed candidate; the owner makes a new one-shot decision when that same ticket
completes. Cancellation or failed refresh leaves live history and the original intent unpublished.

Every successful publication releases its permit immediately after the history swap and before callbacks.
Resetting or destroying a permit releases it. Permits are nonmovable. Moving a publication with a borrowed permit releases that permit and
requires a fresh owner decision at the destination. All error, stale-authority, cancellation, drop, and shutdown
paths MUST release the permit. Workers waiting for the lease MUST observe ticket cancellation and shutdown;
shutdown MUST cancel those waits and drain accepted work before destroying borrowed callback contexts.

Residency-change subscriptions are bounded to eight registrations process-wide. They report readiness only:
the callback runs after raster cache locks are released and may publish readiness or wake an owner, but MUST NOT
read pixels, enter subscription management, decide a trim, or authorize publication. Subscription Start/Stop
belong to startup/teardown outside residency leases and callbacks. Stop is a fence: when it returns, no in-flight
callback can still access its borrowed context. Teardown MUST Stop subscriptions before destroying those contexts.

The managed metadata, Undo/Redo, settled Select All/Deselect and discrete selection-nudge hosts opt into staged
sparse-residency publication. Each reserves the original prepared graph plus one refresh candidate before capture.
Restore refresh uses the original selected optional document, including no-document Undo; selection refresh uses
the original prepared after-document, and zero-offset nudge keeps its original source witness. Refresh MUST NOT
recapture session input, reconstruct pixel edits, regenerate the intended user revision or replace the saved marker.
Moved and applied wrappers refuse reuse; a changing candidate cannot apply using a stale sampled charge.
No-op selection candidates retain the original no-history-change acknowledgement.

Ordinary `Application::Run` starts the independent history-only notification source before startup paths
or input. A full route/subscription registry or failed acquisition stops startup and joins the acquired prefix.
The actual-Run startup and shutdown controls cover this activation; see the
[current receipt reconciliation](../TestRuns/P1HistoryResidencyStartup_2026-10-06.md#current-receipt-reconciliation--2026-10-21).
Other source callers and reference history methods remain required P1 migration work. This source activation
does not qualify G1, O(delta) work, complete native runtime coverage or every live-source caller. Preserve the
legacy oldest-past-then-oldest-future trim order,
revision assignment, no-op redo preservation, and saved-revision semantics above.

The actual shutdown path MUST suppress an uncaptured ready maintenance notice and cancel/join a held prepared
maintenance worker without publishing history or changing document, generation, user or saved revision. No caller
may release the borrowed subscription context or worker input before this join. Stopping subscriptions fences
later process-wide cache notifications; repeated shutdown is idempotent.

Undo/Redo joined shutdown retains its original ticket and exact HistoryWork stamp through producer and
payload retirement. Close suppresses useful completion callbacks; only its completed join may acknowledge
that record, finish that exact stamp and clear both optionals. No document/history/saved-marker publication
is permitted during this acknowledgement. A never-captured restore owns no payload; a prepared held restore
must wake from the actual shutdown event and retire off owner. Repeated Shutdown cannot repeat the join.

## Trim

Production merge binds its complete survivor document and selected-id set to prepared history. Its metadata probe
and full readback record share one history ordinal; a no-plan command never reserves pixel output. Ready plans keep
an immutable source and worker-built composition subset through separately admitted history capture. Publication
checks active id, whole document generation/restore authority and exact history epoch before swapping live owners.
Replaced document/selection/display and captured history/readback owners retain complete deferred release bounds.
GPU mapping/target cleanup stays on the owner. Native qualification passes
([evidence](../TestRuns/MergeHistoryPublication_2026-10-01.md)); shared metadata,
incremental accounting and measured G1 are separate gates.

While `past.size + future.size > entryLimit` **or** retained bytes exceed the budget, drop the oldest past
entry, else the oldest future entry. A 256 MiB trim test MUST exist (`history.trim-256mib`).

## Validation

`Tests/DocumentTests` covers saved-revision capture (current snapshot, delayed save after an edit, undo/redo,
branching, trimmed history, and reset), the unsigned 64-bit limit and physical-memory formula including failed
query, rounding and overflow boundaries, nested transactions, no-op rename/empty name preserving redo, entry-limit trim,
shared live rasters reporting 0 retained bytes, the 256 MiB unique-pixel trim, and `pending.*` (Undo/Redo drop a
live brush, gradient, warp and pixel move without moving history; with a crop frame they drop it and still move
history, and keep it when there is nothing to undo; Cut on a hidden layer or under an
open Levels panel leaves the clipboard and the pixels; a paintable Cut clears; a float aborts when the clear
paints nothing) and `cutfull.*` (Cut on a mask and Clear with a full 100-entry history keep the copy and report
the clear). The WARP self-test covers Ctrl+Z during a stroke, and the save prompt asking for a live stroke on a
clean document whose Cancel leaves the stroke live for its release to record.
The WARP self-test also verifies that the application session receives the machine-derived limit. Debug and
ASan Debug `test.ps1` MUST run `DocumentTests.exe`. Immutable selection capacities are included as specified above;
incremental raster/history accounting remains tracked by P1.9.

## Original allocation ownership for metadata publications

An admitted metadata producer records every newly allocated record root, topology/root group, history snapshot, entry, stack and persistent-index node against its original publication-memory owner. Object, retained child capacity and shared-control allowances are separate from the complete operation admission. Reused immutable nodes retain their existing original owners; they are not reassigned to the younger edit. Reference allocations without an accounting owner retain their established layout and release policy.

The producer's local allocation limit is immutable before construction. Checked overflow, insufficient budget and an attempted allocation after sealing refuse before a publication can escape. Once useful producers have joined, seal the ledger and partition the original complete reservation into its persistent output and temporary retirement charges. Publication, stale refusal, failure, cancellation and joined Close keep their distinct existing authority. No new reservation, admission query or early refund is permitted at partitioning.

Worker destruction releases data contributions only after those bytes have actually been destroyed. A copied or weak shared control retains its original accounting owner through actual control deallocation. Therefore an idle scheduler may have a nonzero charge while the live document or history still owns metadata. A completed edit does not imply zero reserved memory. Final graph/control destruction and owner acknowledgement return the original charge exactly once.

Reference history callers must enumerate the complete storage closure from the held direct raster roots, even when a managed topology has no preflattened RasterOwners vector. Nested sparse bases/tiles and shared children participate in the existing dedup set. A reference byte query never materializes a sparse cache; oldest-first trim and legacy saved/revision/Undo/Redo semantics remain unchanged. Native qualification and measured phase costs remain separate requirements.

## Optional prepared Begin/End publication ownership

PrepareBegin and PrepareEnd accept an optional shared publication-memory owner. Every new snapshot, pending,
entry, stack and topology/index node retains that owner and its local allocation charge; reused nodes retain
their original owners. The default reference route remains unchanged. Selected move opening/closure binds
its output before publication; unbound results refuse before mutation. Original and replaced owners retire
through their real last strong/weak lifetime under the complete original operation admission.

An opt-in prepared deferred move opening may prebuild its exact dependent no-op End before Begin publishes.
Only its opaque latest own-move authority can use that cleanup. Foreign document/generation/history/target or
pending gesture state refuses. Cleanup discards the scalar preview, closes only that Begin, preserves redo
and matches reference Cancel's two invalidations. It grants no successful cancelled-output authority.

## Selected deferred-outline close residency

The opt-in deferred outline controller validates its exact opaque opening authority before every Close
completion, including after residency refresh. Document generation and history revision alone are insufficient:
a foreign editor can open without changing either. Refusal preserves that editor and the original opening;
it MUST NOT run the old opening's abort through a foreign editor. The subsequent no-move Deselect validates its
separate complete command/restore authority after the no-op End has acknowledged.

`PreparedSelectionMoveClosure` exposes the existing one-shot residency try and cancellable worker refresh
against its original prepared after-document. Busy/Refresh resumes the original ticket and reservation; it
MUST NOT recapture session input, regenerate a revision, repeat geometry/End preparation or query admission.
Ready is held only through the scheduler's bounded, callback-free transition to final partition/publication.
Reset the permit before the next useful worker stage, rollback, retirement binding, prefix acknowledgement,
host callback or joined shutdown cleanup. The original source/history/saved-marker checks still apply.

The selected CPU protocol is independently tested. Actual pointer callers, native integration, ordinary
activation, the other gesture branches and measured G1 remain separate gates.

## Owned lasso authority and history exclusion

A selected own-writer lasso may validate its original immutable history epoch while ignoring only its exact currently owned lasso draft in the editor exclusion check. Every other editor exclusion remains mandatory; a foreign Begin invalidates the writer even when generation/revision are unchanged. General `CanApplyHistoryRestore` continues to reject every live lasso. Own drag/abort/close before a prepared selection publication preserve generation, revision, Undo/Redo and transaction depth.

## Exact lasso intent cleanup and Rectangle/New parent

Pending lasso intent cleanup is a one-shot scalar capability for its original session, document serial, intent serial and opening/finish kind. A stale or foreign capability leaves pending edits untouched. Closed own-writer cleanup validates the original editor/history epoch, removes only that closed writer and swaps retained history owners into the prepared payload for original-ticket retirement. It never rolls back an unrelated transaction. Rectangle/New history preparation, residency try/refresh and final publication use the same parent admission; cancelled or stale results do not create a successful history entry.

## Physical selection parent acknowledgement

The selected physical parent retains its single HistoryWork stamp through publication, failed/stale refusal, cancellation or joined shutdown. Finish only that exact stamp before resuming its host receipt. A changed target detected before dispatch reports Stale without creating history or capturing a source; memory refusal reports Failed. Joined shutdown drops never-started input or retires its existing controller without a useful completion callback, preserving independently retained observer charges.

## Ellipse/New original-parent history

Ellipse/New shares the selected original-parent publication/cleanup protocol. Its changed result creates one Elliptical Marquee entry and one generation change, with literal prior selection restored by Undo and terminal AA restored by Redo. Lost/cancellation and stale foreign history/transform preserve their exact source/authority. A kind/envelope mismatch refuses before acquiring source/history roots and must refund the original ticket after its last reader.

## Selected physical Ellipse history

An admitted outside Ellipse/New drag publishes one `Elliptical Marquee` entry and one document
generation advance. A Down inside the old outline moves that outline instead: the scalar Move and
acknowledged End retain their two generation advances and one `Move Selection` entry. Click, Lost,
cancellation and joined Close keep their existing distinct history behavior.

## Selected Freehand history and pointer loss

The original selected Freehand parent owns opening, ordered accepted samples, terminal geometry,
history preparation/residency refresh and publication through one ticket and HistoryWork stamp.
A changed result records one `Lasso` entry; no-op, absent versus explicit-empty, Undo/Redo and saved
marker behavior remain those of the numeric reference. Metadata-only drawing does not publish history.
Generic pointer cleanup cannot cancel an owned live writer ahead of the original Lost acknowledgement.
Lost, cancellation, stale refusal and joined Close preserve the original selection/history and cannot
roll back a foreign Begin or editor. Final strong and weak owners retain the original charge until
worker retirement. Selected runtime proof remains separate from ordinary activation and measured G1.

## Polygonal metadata and last-corner removal

Guarded corner/hover/Backspace operations publish no selection/history/generation change. Removing
the last corner ends only the original validated writer, preserving the original selection, saved
state and redo branch. Its prepared handle and immutable snapshots keep the original memory owner
until actual strong/weak retirement. Foreign Begin/editor state cannot be rolled back by these APIs.

## Polygonal original drawing history

Opening, collected corners, terminal geometry/history preparation, residency refresh and final apply
retain the original admitted job ticket. A changed Polygonal drawing publishes one `Polygonal Lasso`
edit and one generation; Undo restores the exact absent/empty/prior root and Redo restores the result.
Hover and Backspace alone publish no edit. Cancellation, stale authority, overflow and joined Close
cannot replace selection/history or roll back a foreign Begin. Joined Close runs no useful completion
callback. All surviving output/temporary/arena/control and weak owners retain their original charge.

## Polygonal terminal ordering before controller creation

An already accepted Finish/Lost retains its physical ordinal through original controller admission.
Only the prepared writer and current document/editor/history capabilities can publish. Successful
Finish records one edit; Lost and contradictory descriptor metadata record none. Contradictory
metadata must refuse before source capture and release only its original fixed input prefix.

## Polygonal closure readiness and saved revision

A blocked original terminal retains its live writer and fixed terminal record; no finish geometry/history
input is captured until owner readiness permits it and prepared source/editor/history guards still pass.
Metadata-only replay and a saved-revision publication create no history edit. A later valid finish
preserves the owner saved marker: Undo to the saved committed revision is unmodified, and Redo to the
new selection is modified. This CPU invariant does not itself qualify an actual Save/Export caller.
Cancellation, joined Close and foreign Begin behind a gate retain original accounting, drop only own
input and never roll back foreign history/writer authority.

## Selected Polygonal physical history arbitration

A live selected Polygonal drawing keeps its original admitted job/reservation/prepared writer while the
creator receipt yields at acknowledged opening. The interactive metadata park holds no global HistoryWork
barrier. Save captures committed document state and the saved revision, omitting transient selection outlines, without ending that draft. Only the
selected closing descriptor or an ordered cancelling action owns the terminal barrier; completion or
joined cleanup releases that exact stamp once. One changed drawing creates one history entry, and Undo
returns to the revision saved while the draft was live. A younger explicit terminal behind an already
chosen close acknowledges the original result without a second transaction.


PNG/JPEG export during a selected live Polygonal draft leaves the project path, history revision and saved
marker unchanged on success and picker cancellation. Closing that draft still creates only its one changed
selection entry; Undo restores the original committed selection and its previously dirty marker, and Redo
restores the new selection. Export never marks the committed revision as saved.

## Final edit source and original publication ownership

Prepared Size final-source access is valid only before Apply; moves invalidate their sources. Current-only
validation checks the original full token, active layer and history epoch. Residency refresh keeps that same
prepared intent. Its supplied original output owner remains behind the candidate, prepared history and
retired metadata, including last immutable history readers.

The selected native pixel final publication swaps its prepared CPU/history before rows and GPU adoption in
one nonreentrant original acknowledgement. A busy residency permit resumes the same original worker for
refresh rather than blocking the owner or dropping an otherwise current edit. A changed source/history
refuses before mutation. Replaced owners retire under the original temporary account on workers; terminal
receipt notification follows complete publication/retirement. Ordinary Size activation and measured G1 remain
separate P1 gates.
