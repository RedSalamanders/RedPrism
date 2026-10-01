# Document history

Status: current normative product contract
Last reviewed: 2026-10-01
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
storage (Swift `ObjectIdentifier` of `CGImage`). Thumbnails count separately when they are distinct storage.
Immutable selection storage also counts once by its held `SelectionLoops` identity, excluding the live
document's selection identity. Its cached charge includes the outer vector object, outer retained capacity
and each loop's retained point capacity; spare capacity counts. Opaque allocator/control-block overhead is
covered by global headroom. The byte query never traverses selection points. Empty geometry owns no storage.

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
retirement includes retained root capacity. Full document record capture and admission traversal remain H2 work.
Begin, End (including nested/no-op completion), Undo, Redo, Reset and prepared publication invalidate that epoch.
`MarkSaved` remains independent and does not invalidate a prepared edit or allow it to replace a later saved
revision. A view from another history owner, or a moved-from view, has no publication authority.

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
the oldest-first trim policy. Whole document metadata traversal/capture remains H2 work.

Direct reference methods retain their existing behavior; caller migration remains P1.9 H1.4/H1.5. The prepared
boundary does not qualify constant-time document capture, incremental storage accounting, other operation
groups or the measured G1 bound. These remain explicit H2/H3/H4 work.

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
Explicit Cancel routing and dependent dismissal callers remain H1.6b work; the prepared cancellation API alone
does not qualify them. Whole document capture/admission traversal, incremental accounting, first upload and measured
G1 remain separate. Qualification is recorded in the [opening/OK record](../TestRuns/AdjustmentOpeningAndOkHistoryPublication_2026-10-01.md).

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
