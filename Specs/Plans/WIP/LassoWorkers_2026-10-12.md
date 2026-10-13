# Bounded lasso input, display and completion (P1 / S5)

## Delivery checklist

- [x] Qualify the isolated point arena/ticket against an independent vector model, retained snapshots, boundary/refusal/cancellation and original lifetime accounting.
- [x] Qualify private bounded completion geometry/history against 43 independent assertions, preserving all 21,480 predecessor records (21,524 total). See [the private finish proof](../../TestRuns/P1LassoFinish_2026-10-13.md).
- [x] Qualify private worker-created/current-only bounded session bridge against 67 independent assertions (21,592 total records).
- [ ] Integrate and qualify actual host/display callers.
- [x] Qualify private lasso input descriptors/FIFO/terminal adapter against 80 independent facts (21,673 document records total), retaining all predecessors.
- [ ] Connect actual admitted opening and ordered host replay, with terminal/cancellation storage and non-wrapping authority.
- [ ] Replace owner vector copies and conversions with a bounded immutable display capture; prepare draft geometry/coverage on workers and draw it through the shared device route.
- [ ] Prepare complete lasso/marquee selection and history on the original admitted worker job; publish current-only with deferred old-owner retirement.
- [ ] Migrate every draft reader, cancellation/tool-switch/document-replacement and shutdown path; prove actual host ordering and failure semantics.
- [ ] Qualify native configurations and measure large lasso owner/apply/cancel/frame costs before closing S5.

Status (2026-10-12): **isolated CPU arena and original-ticket lifetime qualification PASS; host/storage integration remains unqualified.** Main's
`LassoPointArena-r1-Main/Source/Common/BoundedLassoPoints.h` is the tested private snapshot. The r3 qualified report binds 575 new assertions plus two summaries to all 20,782 predecessor DocumentTests records (21,359 total). The core header SHA-256 is `B4999A5CFA4B72C096B67C4A2CCEC8FE3AFB9F4523C0F3BD060A38B6A1919E8B`, unchanged. See [the isolated storage receipt](../../TestRuns/P1LassoStorage_2026-10-12.md).

This PASS covers only the isolated arena and original-ticket owner lifetime. Lasso host/display/finish, event acquisition, native integration, ordinary activation, and G1 remain open; it does not close the parent P1 checklist.

### Current execution (2026-10-13)

Private completion geometry/history passes 21,524 document records, including the 43 new independent facts.
The new session bridge passes private actual Release compilation, linked ISA and all 21,592 document records. Three earlier fixture compiler refusals (private constant access/StateView misuse/local shadowing) remain preserved, and no predicate was weakened. It allocates an accounted gesture writer on the
original worker, refuses owner acquisition until original memory is bound, binds a nonwrapping selected
opening intent, and cancels late acquisitions even before a draft exists. Owner point/cursor/marquee/backspace
operations use bounded chunks and scalar accessors; reference vectors stay separate and are never a refused-job
fallback. Closure captures the exact opening/revision/arena, document, active/crop/mask and history state, takes
physical terminal-event AA, owns the exact closed writer/snapshot, consumes the selected terminal once, and excludes cancellation before input capture. Core draft
replacement routes invalidate pending intents and retire existing accounted owners. Complete operation admission,
actual event buffering/display/host integration, native configurations and G1 remain due. The beginning
checklist deliberately leaves those delivery rows unchecked.

## Existing behavior and remaining owner work

`LassoDraft::points` is a mutable vector. `ExtendLasso` may grow it; `SyncSelectionOverlay` converts every point,
`ChromeHost::SetLassoDraft` converts again, `CanvasOverlay::SetDraft` compares the complete list, and draft painting
traverses it. `FinishLasso` closes the draft, copies its outline, computes bounds, runs selection booleans and
updates history synchronously. Removing one copy cannot qualify this route.

Preserve the numeric/interaction contract in `Specs/Document/Document_Tools.md` and the chrome contract:
quarter-pixel duplicate suppression; the opening's New/Add/Subtract mode; polygonal rubber band, backspace,
eight-DIP close/double-click/Enter/Escape; freehand release; whole-pixel rectangle and ellipse construction;
fewer than three points/zero area handling; exactly one named history entry. Existing legacy goldens stay unchanged.

## Storage design and invariants

1. The opening worker allocates one explicitly bounded arena under complete original admission. It touches one
   byte per 4 KiB with cancellation before owner acquisition so demand-zero preparation is worker work.
   Account the arena allocation, wrapper and shared control owner; its original lease remains charged through
   all writer/display/metadata readers and deferred retirement. The active writer is private to the owner.
2. Seal chunks of 64 points into fresh arena slots. A sealed block contains the immutable points, previous-block
   index, cumulative count and bounds. Published snapshots own their shared arena and an inline tail of at most
   64 points. Capture copies bounded metadata/tail and one owner reference; no allocation or point traversal.
3. Push writes one point, or seals one 64-point block before that write. Pop restores at most one block and
   recalculates bounds from at most 63 tail points plus cached prefix bounds. No operation walks the prefix.
   Clear/backspace never recycles a slot visible to an old reader. One raw arena deallocation replaces recursive
   shared-node destruction; its accounted wrapper always defers owner destruction to the original release sink.
4. Workers traverse a snapshot in input order using a bounded, admitted block-index stack. Cancellation checks
   every block, and each callback receives at most 64 points. Flattening/output capacity belongs to that worker's
   admission. A cancelled visitor's partial temporary output is never a document publication.
5. The draft uses a proposed hard ceiling of 8,388,608 accepted points and 131,071 sealed slots. Admission can
   choose a lower slot ceiling based on the original operation budget. Repeated pop/re-extension may exhaust
   slots sooner than the point ceiling. Refusal must preserve the accepted path and old snapshots; the host
   reports/acknowledges the documented cancellation/refusal outcome instead of silently dropping a sample.
   This new limit requires a domain-spec amendment at activation; it is not current product behavior.
6. Bind accounting only after the opening worker has joined. Later writer changes affect only unused slots
   and its private inline tail; readers cannot access the mutable allocation cursor. Every later useful worker
   operation has its own admission, while retaining the arena's distinct original charged owner.

## Event and display integration

Use the existing `PointerInputStream` model: captured document/tool/mode/view/opening context, bounded ordered
sample storage, and a separate terminal slot. Do not replace accepted polygon corners with the latest point.
An overflowing stream preserves the accepted prefix and has an explicit terminal/refusal policy. Opening and
point/cursor/view epochs refuse exhaustion before mutation; tests place counters at their maximum directly.

Owner sync passes a shared snapshot plus scalar kind/cursor/anchor/view metadata. It builds no point vector,
path, coverage, mask bitmap or texture. Worker preparation includes freehand/polygonal draft strokes and handles,
rectangle/ellipse geometry and the exact legacy shadow/white draw order. Prepared results are keyed by source,
opening/point revision, current view/DPI/hole and device authority. Borrow after chrome preparation and reject
stale keys before recording a draw. Cancellation/hidden-state/device-loss replacement retains original resource
charges until actual creator/last-use retirement. The eleven-owner budget must be reviewed if a new leaf is added;
prefer the existing shared pumps when ownership and fairness allow it.

## Completion and caller migration

The selected Finish intent owns the opening's mode/kind and terminal sample. Admit complete geometry/Clipper
scratch, document/history output, retirement and refresh overlap before capture. Capture the point snapshot and
immutable document/history roots in bounded time. Worker code constructs the exact original final outline,
uses the existing bounded cancellable selection boolean route, and prepares the complete history publication.
No synchronous fallback is permitted after an eligible job refuses or fails.

Current-only apply checks document/session/generation, active/crop/mask identity where relevant, opening and
point revision, selected history ordinal and sealed history/residency authority. Swap document/history and
acknowledge that exact host intent without destroying old point/geometry owners on the UI thread. Preserve
no-op/redo/saved-marker behavior and the original New-mode deselection rule for empty shapes.

Audit `PendingLasso` readers in Application pointer/keyboard routing, metadata/opacity contexts, selection jobs,
project/size replacement and session lifecycle. They use bounded scalar/snapshot accessors. Replace source
vector capacity estimates with the cached complete arena allowance, retaining distinct original-owner accounting.
Prove Escape, tool switch, backspace to empty, lost pointer, late finish, new gesture, document replacement,
held producer cancellation, allocation/admission refusal, device rebuild and joined shutdown in actual hosts.

## Required proof

Independent vector/bounds references cover 63/64/65 and 127/128/129, snapshot reuse after clear/pop/re-extension,
exact capacity refusal, nonfinite input, visitor failure/cancellation and moved writers. Account original leases
through retained snapshots, weak controls and owner/worker retirement. Actual frame references use the original
Direct2D draft paint order at fractional edges and 96/144/192 DPI; no screenshot rebaseline or relaxed tolerance.

Finish tests use existing legacy selection goldens plus independent history/authority expectations. Run the
mandatory native matrix and ARM64 cross-builds after integration. Measure owner/capture/apply/cancel and visible
frames at large point counts on the specified R1/W workloads, including hidden/resume and allocation pressure.
An isolated storage PASS does not establish host activation, full memory admission or G1.


Private host-input adapter progress (2026-10-13): reuses the existing fixed 512-slot PointerInputStream;
no second queue/worker/pump infrastructure is introduced. It owns physical document coordinates and captured
source/active/crop/tool-kind/mode/view/ordinal fields, ordered corners/cursor/backspace/marquee modifiers, and
terminal event AA. Overflow preserves the accepted prefix and a separate Lost terminal; a later Up cannot
turn it into successful history. Main corrected the independent fixture's integration include path and its
roster reader's separate summary field. Actual Release build/ISA/DocumentTests exit 0 (21,673 records), including
all 80 input facts. This remains an isolated adapter; Application event routing and draft rendering are due.
