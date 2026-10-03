# RedPrism validation

Status: current normative repository contract
Last reviewed: 2026-10-03
Owner: `test.ps1`, `Tests/*`, and `RedPrism.exe` diagnostic switches

## Scope

This contract owns how RedPrism is proven. Domain specs name the behavior; this file names the gates that MUST pass
before a phase is Done. Screenshot policy for chrome lives here so later phases do not invent a second rule.

## Entrypoint

### Detached basic raster edits

`DocumentTests` runs `rastereditwork.*` for literal legacy Fill/Clear/Invert bytes, Gray8 masks, transformed
selection coordinates, sparse fill/base/tile inputs without shared materialization, carried mask growth,
exact/lower/invalid budgets, memory floors and pre/mid-tile cancellation. `rasterwork.stretch.*` and
`rasterwork.thumbnail.*` use independent nearest-coordinate literals for dense/nested sparse reads and
cancellation. Owner publication covers one-entry Undo/Redo and refusals of stale tokens or malformed candidates.

The WARP application self-test runs `rasteredit-host.*` through all five production commands on image and mask
targets at gates before and after preparation. Held states retain selected input/history/whole reservation while
view input remains usable. Controls cover early and late cancellation, returned/injected allocation errors,
capture/admission failure, stale image/transform/full mask/selection/generation/restore/document, younger Undo,
and aggregate release of a large sparse canceled result on a worker. No-selection Delete remains metadata;
Clear without selection adds no entry. Terminal states retain no tickets/barriers/host-input or memory charges
and perform no hidden Present. Gate deadlines detect deadlocks, not performance thresholds. All x64 suites and
ARM64 cross-builds remain required; pending distortion callers and real-loop timings stay open in P1.

### Detached transforms and floating transactions

`distort-work.*` compares private dense/sparse, trim, preview, edge-background and tile sampling with the unchanged
numeric warp references. `transform-pixels.*` and `transform-commit-work.*` cover ordinary/group/mask/live-shape
after-documents, later-member budget reservation, duplicate member refusal, sealed target/draft/session authority,
nested duplicate closure, single-use publication, cancellation and construction faults. `prepared-history-following.*`
proves that a dependent End requires its exact predecessor roots/pending owner, including competing transactions
at the same epoch/revision and moved views.

`floating-opening-work.*`, `floating-work.*` and `floating-merge-work.*` prove preparation exposes no partial hole,
complete prebuilt rollback, unchanged/cancelled/oversized resolution, one outer entry and exact Undo/Redo, translated
premultiplied bytes/outline, carried-mask growth/eager thumbnail, clipboard identity, newer-draft/history/source/reopen
refusal and reached worker/history fault boundaries. Near-cap tests use a virtual 9999-square sibling: a one-pixel
clear on a 512-square canvas keeps the exact 2-square source; Clear crossing a tile boundary with a four-pixel limit
preserves placement and untouched bytes. A committed grid beyond its caller limit still refuses. Nested floating
opening, merge and prebuilt restoration close only their child scope, preserving an existing parent's name/depth.
`transform-cancellation.*` proves ordinary draft-only cancellation, duplicate removal, nested parent/redo retention,
single-use/moved-input authority and changed draft/source/active/history/replacement/foreign refusals. Snapshot,
entry, stack-growth and trim faults must be reached and leave the live opening intact.
`transform-preview.*` adds 44 independent controls for complete display-only publication, affine raster sharing,
bounded color/mask distortion, carried groups, placed/unlinked/uniform mask semantics, literal standalone-mask
bytes, private sparse reads, invalid-corner fallback, allocation/cancellation boundaries and consumed/stale
draft/target/document/history/reopen/replacement/foreign authority. The prior 467 core controls and all legacy
numeric goldens remain required. Production coalescing, host routing and measured G1 qualify separately.
These are core qualification gates. Production transform/opening/preview/settlement and aggregate retirement,
and measured G1 remain explicit A5/A6 follow-ups. Core native configuration results are recorded separately in
[`TransformWorkerCore_2026-10-02.md`](../TestRuns/TransformWorkerCore_2026-10-02.md) and
[`TransformCancellation_2026-10-02.md`](../TestRuns/TransformCancellation_2026-10-02.md) and
[`TransformPreviewCore_2026-10-02.md`](../TestRuns/TransformPreviewCore_2026-10-02.md).

The WARP application self-test adds `transform-host.*`: 344 assertions in the settlement foundation. Its gates
before and after preparation prove held input/history/reservation, responsive view input, acknowledged cancellation,
exact failure errors, reached history stages, stale ordinary geometry/source/history/same-ID replacement, younger
Undo and aggregate off-owner retirement. Floating controls prove exact prepaid rollback for admission, capture,
worker and reached opening/merge history faults, silent cancellation or one failure notice, idle Cancel without
new work and retirement of the persistent restoration owner. Layer nudge controls use literal 10/1-pixel offsets
across modifier transitions, persistent draft metadata, one-entry publication and younger Undo order. Repeated
Ctrl+T retains the opening/draft with no entry. Full native profiles remain required; deferred preview coalescing,
remaining bypass/duplicate/pointer-loss routes, device/shutdown and measured G1 are separate follow-ups.

Duplicate-opening tests add 82 independent controls for shared original image/mask storage, grouped insertion,
exact-cap color/mask budgets, one duplicate entry/Undo, prepaid rollback, nested parent history/redo, stale
authority and reached preparation faults. The real host adds 131 event-gated checks: opening ownership,
coalesced Shift/control geometry against a literal axis reference, before/after-prepared release and Escape,
capture loss, release followed by capture loss, full release queue, younger Undo, admission/capture/worker/history
faults and failed-copy-commit rollback. Complete document/history/clipboard state and off-owner retirement are
checked independently; failure bounds detect missing acknowledgement, not performance. Ordinary transform
activation, distortion/other lifetime cases and G1 require their remaining checklist gates.
The complete clean native matrix qualifies these 82 core and 131 host controls with the approved DxUi
API-revision-3 integration, previous preview/settlement controls and unchanged goldens against 559 inputs;
see [DuplicateGestureWorkers_2026-10-02.md](../TestRuns/DuplicateGestureWorkers_2026-10-02.md).

Deferred-model tests add 47 controls for opt-in display ownership, inline Commit/Cancel refusal, same-opening
cache retention, source-generation invalidation and prepared single-use publication. Deferred writer/outline tests
add 100 controls: every direct writer must leave the opening intact, and independent analytic homographies
verify affine/projective floating outlines, reflections and asymmetric geometry. Cancellation never installs a
partial outline; literal selection handles remain with the displayed pixel snapshot.

The opt-in `transform-preview-host.*` test uses actual event-gated jobs before/after preparation: 90 controls
cover admission before capture, latest-pending replacement, exact committed/history/clipboard preservation,
view input while gated, changed source/history, close/reopen acknowledgement, reached admission/capture/worker
failures, retained complete previews, queued full-resolution Apply and draft-only Undo cancellation, and payload
retirement off owner. The existing capture-loss fixture awaits cancellation acknowledgement before applying its
unchanged document/history assertions. Full native clean qualification remains required before closing this
foundation. Production opt-in, Alt-duplicate, complete caller/device/shutdown and G1 remain separate gates.
### Clipboard and copy preparation

`clipboardtransfer.*` runs a dedicated MTA test worker and restores saved user clipboard contents. Its independent
literal PNG/DIB fixtures prove exact premultiplied versus straight bytes, PNG priority, separately inspected DIB
fallback and hard/lower/wide budgets. Actual OS calls prove reads after immediate owner destruction, a real held
clipboard, sealed stale retries and complete/partial/empty publication. Every injected allocation/native fault
must be reached; pre-empty cancellation preserves content, and late cancellation finishes both independent
formats. A gated delayed renderer proves the API worker remains pending until explicit external release, then
captures the sequence after both formats render. Individual foreign-thread joins have five-second failure bounds;
the complete suite has a separate 60-second bound. Test-only one-shot readiness retries do not qualify production
timers, host input ordering, shutdown or G1. The exact held sequence is checked at its placement stage, without
assuming a later-open sequence remains equal.

`copypixels.*` uses literal BGRA/Gray8 grids, independent pixel-center mappings and rectangle/cell intersection
areas. It covers clipped origin, transient sampling, semantic mask outside bytes, sparse/merged inputs, invalid
grids/formats, reached cancellation/observer allocation failure and independent memory floors. Metadata-only
16 MP sparse inputs make omitted private/canvas/output/AA planes visible above bookkeeping headroom. It does
not qualify atomic Cut history or production host admission.

`clipboard-command.*` independently controls sealed Copy/Copy Merged/Cut publication. Literal image/mask bytes,
origin and unchanged Copy history are required. Cut publishes one exact Clear/Fill Mask entry with both halves,
or neither half after pre-cancel, no painted pixels, injected cancellation/allocation failure at every history
preparation stage, or stale session/source/private serial/history. Live brush/lasso/warp Copy drafts survive;
Cut leaves a live warp and its later closure refuses the changed source. A full-document composition token
captured before Copy Merged rendering cannot be replaced by a fresh token after a document change. Held owners,
long token mask names, selection and semantic thumbnail memory floors remain independent controls. This core
suite establishes command semantics; the production host graph and current private matrix are recorded below.
Constant-time capture and G1 remain unqualified.

`owner-work.*` uses independent leaf mocks and literal round-robin call order. It proves one leaf per pass,
pending versus ready, bounded/duplicate/self registration refusal, quiescent detach/reused slot, recursive pump
refusal and callback mutation refusal. Native GPU/retry sources are multiplexed by Application. Production
clipboard Inspect/Copy retries retain their admitted record and reservation, with Paste sharing six total
acquisitions across Inspect and owned Copy; host qualification and its remaining limits are recorded below.

`ClipboardReceipt.*` uses independent literal normal/high-bit packets and malformed version/reserved/length/
session/serial values, proving exact cleared outputs. `clipboardfingerprint.*` uses literal empty/abc SHA-256
and a 65,537-byte coordinate fixture whose digest comes from an independent PowerShell calculation. Exact
64 KiB bands, pre/stage cancellation, reached native failures, cleared output and all-format key/content matching
are gates. `clipboardbinding.*` runs inside the existing saved/restored dedicated MTA transfer suite. It proves
receipt-first empty/partial/complete truth, exact held sequence/error, read after owner destruction, receipt-only
refusal, positive controls for invalid receipt fallback, one-byte original-block tamper with valid PNG, identical
payload copy-back, private serial refusal and late cancellation. These primitive tests alone do not qualify the
full host callers; the private host matrix is recorded below, and G1 remains open.

### Detached gradients

`gradientwork.*` keeps the independent legacy linear/radial/opacity tolerances and literal pixel-center BGRA/Gray8
expectations for transparent source-over, selection exterior, placed/stretched masks and sparse inputs. Invalid
finite projection arithmetic, enums/colors, exact/lower budgets and pre/mid-tile cancellation publish no result.
`gradientstate.*` proves metadata-only deferred openings, independent acquisition/settings clocks, active/latest
publication order, no-line redrag, refusal before synchronous settlement mutation and exact named apply/Undo.
`gradientresource.*` checks independent coverage/output/mask/metadata floors, shared last-owner retirement,
retained source/private tiles and callback-borrow termination without shared sparse materialization.

`gradient-host.*` exercises the actual preview and selected commit callers at event gates. Capture follows full
reservation; input/history/tickets survive early/late cancellation. Admission/capture/worker OOM, returned statuses,
source/selection/generation/restore/opening changes cannot authorize dependent edits. Coalesced endpoints and
palette refreshes finish at the latest settings with no history entry. Image/mask commits use literal pixels and
one named entry; Undo drops an uncommitted line. Existing GPU checks await acknowledgement, retaining their
canvas-vs-raster tolerance and adoption count. Full x64 suites and ARM64 cross-builds are required. No event-gate
deadline is a performance assertion; real-loop drag/settle timing and owner upload/history budgets remain open.

### Detached lifted-pixel moves

`pixelmovework.*`, `pixel-lift.*` and `pixelmovestate.*` keep the literal coverage/source-over, transformed
landing, duplicate, transparent-source, mask-carry and outline expectations. Deferred openings allocate no
pixels. Refused preview/apply candidates remain owned by their caller; exact opening/generation/settings checks
prevent an older worker from changing a newer gesture or retargeting a changed source.

`pixelmove-host.*` runs production preview and selected pointer-release/nudge jobs at event gates. It covers
admission/capture/worker failure, early and late cancellation, stale source generation/selection/document,
one named entry and Undo, and a younger Undo queued behind the commit. A younger Shift nudge retains its
event-time ten-pixel offset after the modifier changes. Coalesced previews show an acknowledged active result
and then the latest offset. Refused nudge admission posts one notice and preserves source/history. A canceled
large graph proves retirement on a worker. Complete migrated job/stroke/opening aggregates retain release headroom
before capture; a small component alone cannot lower the complete aggregate below its retirement threshold.
Empty source-region moves preserve history. All terminal states drop tickets, barriers, input and memory charges
without hidden presentation. Full configuration qualification is required; floating
transactions, display geometry, owner history/first upload and real-loop bounds remain separate P1 work.

The new opening suites add 33 independent primitive/opaque-worker assertions for literal move/partial-alpha
duplicate output, placed/sparse sources, wrong source/grid/selection/AA/canvas/refusal, moved-from/second-install,
cancelled preparation and exact complete-node release ownership. Actual host controls count one opening
preparation across two coalesced previews plus commit, check latest literal pixels/outline and one undo entry,
and invalidate/rebuild on gesture reopen. Every terminal commit clears the cache and returns reservations;
weak-owner controls check final retirement. These additions enter native qualification and make no drag-time claim.

### Retained selection and history ownership

`DocumentTests` runs `selectionstorage.*` and `history-accounting.*`: empty/shared/moved geometry, reserved
capacity without changed geometry, each selection identity charged once with live exclusion, equal-geometry
no-op preserving redo, selection-only byte-budget trimming, and a valid owned Undo result after trimming erases
its future entry. Normal Undo/Redo restore the expected document, active id and revision. Existing history,
saved revision, pending-edit, mask/crop and 256 MiB trim numeric expectations stay unchanged. Full matrix and
real application-loop qualification are recorded in [HistoryStorage_2026-09-30.md](../TestRuns/HistoryStorage_2026-09-30.md).

### Prepared history publication and retirement (P1.9 H1 foundation)

`DocumentTests` includes the five foundation prepared-history groups: `HistoryStateViewTests`, `PreparedHistoryTests`,
`HistoryPreparationFaultTests`, `PreparedRasterHistoryTests` and `HistoryRetirementBoundTests`. Their expectations
cover immutable captured views and owner/epoch/revision invalidation; detached edit/restore preparation and
allocation-free publication with stale, foreign, moved-from and malformed candidates refused; cancellation and
`std::bad_alloc` leaving live history/document state unchanged at the named preparation checkpoints; basic raster,
gradient and pixel-move apply/Undo/Redo ownership; and conservative graph bounds for current, pending, past, future
and additional raster owners. Raster-bound cases include sparse bases/tiles and a possible lazy materialization
cache, shared-DAG occurrence overcount/overflow refusal, and refusal beyond depth 128 without materialization.

The WARP `rasteredit-host.*` self-test also injects `std::bad_alloc` at five history-preparation stages for the
basic Invert image commit: `BeforeSnapshot`, `AfterSnapshot`, `Entry`, `StackGrowth` and `TrimIndex`. Its expected
result is `E_OUTOFMEMORY`, unchanged document/history, and retirement of the aggregate payload off the owner
thread. The required x64 Debug, Release and ASan Debug suites and all ARM64 clean cross-builds pass;
[current evidence](../TestRuns/PreparedHistoryPublication_2026-10-01.md). Other H1 callers, production Undo/Redo,
H2/H3/H4, P1 and G1 remain open.

Destructive color/filter commits add `PreparedColorHistoryTests`: literal Gamma 2
BGRA output and Undo/Redo, applied settings/identity Levels reopening, independent saved revision,
stale generation/history epoch, foreign/moved/single-use authority, five-stage cancellation/allocation refusal,
and retired lookup/capacity ownership. Its application gate adds five history-allocation failures, admitted
capture refusal and a changed-history-epoch result, retaining source pixels and seeded redo. Harness waits pump
real admission/capture/completion events; production commands do not wait. All 45 independent assertions and
actual host cases pass the full x64 configuration matrix; ARM64 clean cross-builds pass
([evidence](../TestRuns/ColorHistoryPublication_2026-10-01.md)). Suspension tests include an unrelated memory
wake while the worker remains held: a wake alone cannot prove that an owner continuation has suspended.

Adjustment opening/closure add independent `AdjustmentClosureTests`, `AdjustmentOpeningTests` and
`AdjustmentNestedTests`. Literal parameter records/source bytes cover every kind, preview on/off, stored-record
no-op versus implicit-default promotion, nested named transactions, template/redo/saved-revision retention and
single-use/moved/foreign/stale authority. Same-sequence mismatched settings are refused. The named allocation and
cancellation checkpoints MUST be reached before unchanged-state assertions; closure reuses its opening and cannot
claim the unreachable BeforeSnapshot stage. Every acknowledged End advances generation, including no-op/nested
closure, and an old whole-document token is refused.

The actual WARP host checks complete admitted capture/reservation, retained source/redo/transaction on failure,
late cancellation drop, precise terminal receipt and off-owner retirement for all six kinds. Opening checks literal
below-composite bytes, unchanged live visibility and history baseline, GPU mapping/target release before detached
Begin preparation, device recovery after copied pixels and a younger queued rename that waits for opening and OK.
An open transaction disables Undo/Redo commands; retention assertions therefore inspect its immutable redo stack,
not `CanRedo()`. No production command pumps a nested wait. Final identities and configuration results live in the
[opening/OK record](../TestRuns/AdjustmentOpeningAndOkHistoryPublication_2026-10-01.md).

`ColorDismissalTests` uses independent literal state for Levels/Gaussian/Content-Aware Fill, nested outer history,
stale lifetime/settings/active/mask/crop/history epoch and moved/foreign/single-use refusal. A real preview lookup
must survive application solely in the prepared retirement owner and expire when that owner releases it. Destructive
dismissal changes neither generation nor any document/history/template state. Metadata Cancel still performs End.
The actual owner/worker host tests require repeated queued/active cancellation to retain one request, held and
already-computed OK to drop before Cancel closes, construction failure to retain the panel/error and a stale old
opening to leave the new panel intact. Younger save/tool/card/Close Project must stay Pending through that close.
All owners/reservations return, release is off owner and hidden Present stays unchanged. Ordinary fixture cleanup
waits for real acknowledgement, with old failure injection excluded from its new cleanup descriptor; the direct
engine lifetime-invalidating reference case remains explicit. Nested-token refusal must compare the same sequence
before and after closure and prove the token was current beforehand. Identities/results are tracked in the
[cancellation record](../TestRuns/PanelCancellationHistoryPublication_2026-10-01.md).

`DocumentSizeHistoryTests` adds 50 independent assertions for literal colored extension and nearest resize pixels,
masks/selection/crop/active target, resolution-only Undo/Redo, nested/no-op history, moved/foreign/single-use
authority, an active-layer-only change with a still-valid document token, five history allocation and cancellation
stages, and retained lasso capacity/ownership. The actual
size-card host gate covers 22 terminal/ordering scenarios, including admitted capture refusal, all five history
allocation points, epoch-only staleness, retained redo/source and off-owner complete aggregate destruction.
The full frozen x64 suites and ARM64 cross-builds pass ([evidence](../TestRuns/SizeHistoryPublication_2026-10-01.md)); these correctness gates make no timing claim.

`.\test.ps1 [-Platform x64|ARM64] [-Configuration Debug|Release|'ASan Debug'] [-Rebuild]` builds through `build.ps1`,
then runs every assertion below. It MUST exit 0 only when all of them pass. Expected nonzero exits from isolated
negative-test children (`HostTests.exe --asan-probe`, `RedPrism.exe --no-such-switch`) MUST NOT become the test
entrypoint's success exit code and MUST NOT be reused as the pass/fail signal for a later gate.

ARM64 runtime qualification requires a native ARM64 host. Cross-built ARM64 binaries are produced by `build.ps1` only.

The hidden WARP self-test runs under a bounded timeout; a run that does not finish in time fails with the process id
reported, never killed.

A test run never waits on a dialog. In Debug and ASan Debug, `RedPrism.exe`'s harness modes (`--self-test`,
`--screenshot`, a `--demo`, a `--bench`) install a CRT report hook (`_CrtSetReportHook2`) that ends the process at the
first `_CRT_ERROR` or `_CRT_ASSERT` report with exit code 3, the report on stderr, instead of the report being printed
while the run still exits 0 (a debug-heap corruption message would not otherwise stop it). It is installed once the crash
handler and the kernels are initialized. Every harness mode and `--crash-probe` also set
`SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX`, so a fast-fail that bypasses the crash handler, or an unreadable drive,
opens neither Windows Error Reporting's dialog nor a critical-error box.

Each of the four test executables first calls `RedPrismTestFailureReports::RouteAwayFromDialogs()`
(`Tests/Support/FailureReports.h`). In every configuration `_set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT)`
and the same `SetErrorMode` flags leave an abort, a fast-fail or a crash without a dialog. In Debug and ASan Debug a
wide report hook (`_CrtSetReportHookW2`, which the CRT calls for every report, ANSI ones included) ends the run at the
first error or assertion report (an STL range check, a CRT assertion, a debug-heap error; a warning keeps the CRT's own
handling) with the report on stderr and exit code 3, the dialog's Abort; the CRT's own report modes route to stderr as
well. A loader failure before `wmain` (a missing DLL) is outside this routing: only the process that starts the suite can
set the error mode its children inherit. The hidden switch `--failure-report-self-test` of each executable fails such a
check on purpose.
`Tests/BuildProcessTests/FailureReportTests.ps1` runs it for all four first in `test.ps1`, each run bounded, the way
`--asan-probe` proves AddressSanitizer: exit code 3 with the report on stderr in Debug and ASan Debug, exit code 0 in
Release, which has no such checks. A run that does not end is blocked on a dialog; the test ends its own child and fails.

The self-test reads a keyboard of its own (`RedPrism/KeyboardState.h`): its drivers hold exactly the modifiers a
step names, and the input code reads Shift, Ctrl and Alt from that keyboard, never from the thread's key state.
Windows writes every physical key transition into each thread's key state at once, the hidden self-test window's
thread included, so without it a Shift or Ctrl the user types in another application while a gate runs changes a
step at random (RF-1110); with the thread's key state, a Ctrl the user held turned a typed S into Save and left
the run waiting on a Save As dialog. A quiet run never opens a file dialog: Open, Import Images, Save As and the
exports return as cancelled. The self-test's clipboard steps borrow the machine's clipboard, which other
applications' clipboard listeners open for tens of milliseconds after every change: the test's own reads, writes and
the set-aside wait them out (`SystemClipboard::kHarnessAttempts`, about 3 s), and it waits for a free clipboard
before a copy whose mirror it checks, while the product's copy and paste keep their short wait.

The IoTests availability and receipt-binding fixtures share that roughly 3 s acquisition budget: at most 30
acquisitions with 100 ms event waits, retrying only Busy. Fault-injected Busy still uses one acquisition, and
assertions are never retried. Failure diagnostics enumerate offered format IDs without calling GetClipboardData;
they record inspection status, attempts, clipboard sequence, immediate native error and the process holding an
unsuccessful OpenClipboard acquisition when available. These fixture waits do not change production retry limits.

### Prepared selection publication (P1.6 core)

The application WARP self-test's `wand-layer-host.*` controls exercise the settled active-layer New production
actor. Independent placed-pixel clockwise literals, settings/AA changed before capture, no-op redo, unmatched
sample deselection, queued outside/nonfinite/empty acknowledgement, actual younger Deselect/Undo, adjacent/full-
ring coalescing with distinct latest output and a reached shared-ordinal gap are required. Held stale active/mask
authority, cancellation acknowledgement, exact worker error, capture/entry allocation faults, real record-table
refusal/recovery and complete aggregate retirement preserve document/history and release all ownership. Direct
zoom and an actual window message are observed before held-worker release. These are structural controls; no
additional physical pointer acceptance, broad gesture routing or G1 timing claim follows.

The application WARP self-test's `selection-host.*` controls exercise the settled Select All/Deselect production
actor. Literal clockwise full-canvas/nullopt output, complete layer/target preservation, no-op redo/revision,
nested depth/title, updated saved marker and ordered younger opposite command/Undo are checked. A held worker
proves reservation before one capture, pending receipt/history ownership, direct zoom and a posted window message
before acknowledgement, and aggregate retirement off owner. Reached generation/active/mask/crop/epoch/replacement/
lasso/brush changes refuse stale output. Capture and six history-stage allocation faults preserve live state;
actual full scheduler records refuse admission and a later command recovers after drain. The mask fixture MUST
begin on image pixels with an existing mask, so its held target switch is a reached change.
These 162 unique checks across 28 modes run inside the full application suite; they do not qualify live gesture
or header routing, every retained raster's last-release thread, allocator/API faults, constant-time capture or G1.
Harness setup/cleanup waits for queued selection acknowledgement before starting later pointer gestures.

`selection-preserve.*` adds literal 4-by-4 source, mask and clockwise selection controls for the separate
preserving-command boundary. Live brush/warp/shape opening identity and continued input survive publication;
reached cancellation/replacement and identical-geometry shape reopening refuse old output. Weak authority does
not keep cancelled gesture objects alive. A white hard brush continues outside its original 2-by-2 opening clip
after Select All/Deselect: committed outside pixels remain the literal source, the center is white, and separate
Undo steps restore pixels and then the original outline/AA. History depth/title, true no-op redo, exact target/
crop/session/history staleness and reached cancellation/bad_alloc recovery use independent expectations.
Existing settled factory and history-restore refusals remain in their original tests. Production pointer buffering,
writer ordering, close/device cases, all geometry modes and measured G1 are not qualified by this core suite.

`selection-command.*` uses literal 4-by-1 pixels/mask, full-canvas geometry and coverage bytes for Select All,
absent versus explicit empty Deselect, no-op/redo, named nested transactions and a distinct later saved marker.
Both operations reach each SelectionSnapshot/BeforeSnapshot/AfterSnapshot/Entry/StackGrowth/TrimIndex cancellation
and bad_alloc boundary before unchanged-state assertions. Active/mask-only changes prove generation/epoch did not
change. Foreign, copied/moved-session, moved-from, single-use, draft and stale results cannot publish.

`selection-move-closure.*` independently checks x=1..2 moved to x=2..3, End's one generation and Cancel's two,
exact history title, Undo/Redo, retained seeded redo, no-op and named outer transaction, distinct saved marker,
source/mask/crop/target preservation and independent authority-field refusals. End and a Cancel with an additional
real transaction change each reach every AfterSnapshot/Entry/StackGrowth/TrimIndex cancellation/OOM boundary.
A fully undone Cancel proves that changed-history construction checkpoints are absent. Failed preparation
retains the live move and complete history; a separate clean candidate can still close. Pre-cancelled, foreign,
copied/moved-session and moved-from/single-use candidates are refused. No timing threshold is asserted.

`selection-move-opening.*` adds 66 unique checks for absent/explicit-empty eligibility, stable opening through
samples, nonfinite input, duplicate Begin, wrong zero/maximum/real prior-gesture End and Cancel capture, and
equal-geometry reopen. Stale prepared apply preserves the entire reopened state. Fresh End/Cancel, no-op redo
and their generation acknowledgements use literal geometry and named history expectations. Replacement and
clear during an active move never reuse its opening. The cached original-outline retirement contribution and
complete session bound preserve moved geometry and never materialize a sparse fixture. Counter exhaustion is
source-reviewed; maximum expected opening is a mismatch control, not a forced private-counter overflow test.
Retained table-driven suites may repeat labels; qualification compares their exact named multiset, including
repetitions. New opening labels are unique. Application closure/order/retirement and G1 remain separate gates.

`selection-move-host.*` exercises the moved-release and direct Escape/first Undo/Redo close boundary in the full
Application suite: 242 unique assertions across 31 modes, retained in the full ordered stage sequence. Held real workers prove
pending receipt/reservation ownership, stopped late samples, duplicate receipt reuse, younger-writer ordering,
exact opening and equal-geometry reopen refusal before capture, generation/serial/active/mask/crop/history
staleness, nested transaction/saved-marker preservation, capture/history-allocation faults, worker cancellation
and failure, input-budget/allocation refusal, real scheduler-record saturation and retry. Literal rectangles and
independent history expectations check publication. Retirement probes cover the prepared aggregate rather than
every raster's final reference; window-message/zoom controls establish no timing threshold. Qualified private
and six-profile native runs, with the retained failed native r1, are recorded in
[`SelectionMoveHost_2026-10-03.md`](../TestRuns/SelectionMoveHost_2026-10-03.md). Remaining callers and G1 stay open.

`selection-move-caller.*` adds 276 unique assertions across 49 modes, retained in the full Application suite.
Real changed/same tool and tool-command inputs, all 27 End-before-command refusal routes, capture loss,
selected Undo/Redo, Select All/Deselect ordering and younger Undo bind the event-time opening. Held workers
prove stopped late samples, pending receipt/reservation ownership, nested title/depth, actual budget/allocation
refusal and retry, reached worker failure and equal-geometry reopen refusal before capture. Exact unique
names and source/runtime stage order must match the independently archived private proof in every x64 suite.
Six fresh native profiles and immutable source/product/evidence archival are recorded in
[`SelectionMoveCallers_2026-10-03.md`](../TestRuns/SelectionMoveCallers_2026-10-03.md). Remaining header/close and
coexisting-edit policies, heavy dependent actions and measured G1 remain separate requirements.

`selection-move-followup.*` adds 84 unique assertions across 16 modes through actual zero-offset pointer
release, with the complete 52-stage Application order. Exact private and native PASS names/summary and
source/runtime stage order must match; retain caller 276/49, close 242/31, selection 162 and wand 86/16 checks.
Literal geometry/Undo, event-time press point/settings/AA, same-ordinal End/selection gates, younger tool/Undo
order, no-op/redo, nested title/depth, reached capture/worker/admission failure and real cancellation at either
phase must qualify. Refusal freezes samples and retry preserves the opening; equal-geometry reopening refuses
before capture. A live deferred-panel fixture retains its original selection clip on the existing caller route.
The held follow-up processes Zoom and an actual posted window message before release; this is a structural
control, with no timing threshold. All Layers/coexisting publication, remaining headers/close and measured G1
stay separate. Actual qualification and retained failed attempts are recorded in
[`SelectionMoveFollowups_2026-10-03.md`](../TestRuns/SelectionMoveFollowups_2026-10-03.md).

The native configuration matrix qualifies these preparation methods; production command/pointer ordering,
live gesture/sample ownership, deferred job retirement, general Clipper bounds and G1 remain separate gates.

### Cancellable wand and trace kernels (P1.6 S4a)

`wand-cancel.*` covers pre-cancelled local/shutdown tokens, reached Source/MaskInitialize/Sample/MatchScan/
FloodSeeds/FloodRun/FloodNeighbors/TraceInitialize/TraceEdges/TraceStarts/TraceWalk/Complete checkpoints and
whole-output refusal. Owner-driven event gates hold a real worker at matching step 4096 before local or shutdown
stop; acknowledgement precedes join, and a fresh token recovers complete literal bytes. Literal references cover
channel/alpha tolerance, rounded clipped sampling, disconnected stripes, holes, actual diagonal corner contact,
sparse reads without source-cache publication, nearest placement and transparent blank-layer sampling.

`wand-chunk.*` records fixed-size checkpoint arrays with overflow rejection. Odd-width 257-by-65 and skinny/
8193-by-1 inputs prove exact ordered 4096-step boundaries across rows and tails, cumulative flood run/neighbor
counts, literal full masks and clockwise rectangles. Late matching/vertex-start cancellation must reach its
final chunk boundary exactly once, discard partial output and recover with a fresh token. Existing `wand.*`,
`wandsession.*`, `loadrefused.*`, selection and coverage goldens remain unchanged; no elapsed threshold is asserted.
Paired Release CPU measurements retain the pre-change source as a performance reference only, with validation
outside timing. Native configuration qualification, host activation, full admission/Clipper/GPU/history lifetimes
and UI/G1 measurements are distinct gates.

### Prepared active-layer wand publication (P1.6 S4b)

`wand-layer.*` uses literal BGRA pixels and clockwise coordinate lists to check translated nearest sampling,
hidden/masked/zero-opacity source rules, captured settings/AA, sparse cache isolation, transparent blank/folder/
no-active inputs, contiguous versus disconnected components and rounded-sample no-match deselection. History
checks cover exact titles/Undo/Redo, unchanged-outline redo retention, nested outer transactions and saving while
publication is in flight. Existing legacy goldens remain unchanged.

Reached active-only, mask-only and crop-only fixtures prove invalidation without generation/history changes;
history-epoch and document-generation changes, moved/copied/foreign sessions and single-use ownership refuse
stale application without changing the current owner. Cancellation reaches every publication milestone and all
six history preparation stages; separately thrown `bad_alloc` at those stages preserves current state. Fresh
tokens recover complete literal geometry. These are named stage injections, not OS allocator fault coverage.

Memory controls inspect literal pixel/vertex capacities, sparse source overlap and recycling carry, actual default
vector growth/old-new overlap, complete retirement charges, invalid-grid/settings refusal and 64-bit maximum-grid
planning without allocating its pixels. A 30,000-square transparent document produces only the literal quad.
Real gated workers prepare both applied and stale aggregates, then dispose of the whole result on the worker after
the owner closes the document. This proves the core handoff; scheduler/host lifecycle, ordering, failure/refusal
presentation and measured G1 remain separate gates.

## Gates

The pipeline foundation adds `DocumentTests.exe --stress`: 256 rounds of eight simultaneous readers of shared
sparse rasters (assembly, region reads, thumbnails and byte accounting), with independently computed expected
pixels and stable-pointer checks. Eight rounds also run in the normal document suite. `test.ps1` runs the extended
stress in ASan Debug. Neither mode asserts elapsed time; ASan diagnoses memory misuse but does not detect races.

| Gate | Pass condition |
| --- | --- |
| Version identity | `RedPrism.exe` `OriginalFilename` is `RedPrism.exe` and `ProductName` is `RedPrism` |
| HostTests | `HostTests.exe` exit 0: adapter-of-monitor, frame-action selection (including display-off), command catalog and `--dpi` rejection, `EditorTool` key/glyph/name tables, every rail glyph present in Segoe Fluent Icons (skipped with a `SKIP` line when the font is absent), every STRINGTABLE id loaded from `RedPrism.exe` as a data file with positional placeholders only, transform overlay geometry (rotated edge and corner hits, rotation knob, zoomed document mapping, resize cursor directions, distortion midpoints), `--demo transform`, `--demo selection` and `--demo brush` parsing, `--demo retouch` parsing, `--bench brush` parsing, `--demo adjust`, `--demo canvassize` and `--demo crop` parsing, `--crash-probe <directory>` parsing and its refusal without a directory, rejection of other names; the crash-report helpers (`crash.*`: default directory, pruning to the newest ten, a minidump of the test process, the pending marker round trip, an empty directory refused); the satellite string tables (`locale.*`: candidate order and de-duplication, a missing satellite, the system languages, the `SetStringResourceSatellite` override and fallback); the About version reader (`version.*`: `FormatFileVersion`, a null path and a module without a version resource refused with the fallback shown, `RedPrism.exe`'s own `VS_VERSIONINFO` read as the `Common/Version.h` major and minor with the stamped build) |
| ASan Debug only | `HostTests.exe --asan-probe` MUST NOT exit 0 and the captured log MUST contain `AddressSanitizer: heap-use-after-free`. An ordinary crash without that diagnostic is a failed probe. |
| Help | `RedPrism.exe --help` exit 0; redirected stdout contains `--self-test` and `--dpi` |
| RAW corpus preparation | Offline `RawCorpusTests.ps1` validates the real nine-extension manifest, allowlist, roles, original license/legal-text hashes, safe root and refused downloads; present matching corpus files additionally qualify exact image sizes/hashes and local license/manifest snapshots. Missing corpus byte controls explicitly SKIP in ordinary tests; P4/P5 required-file qualification must run without SKIP. |
| Unknown switch | `RedPrism.exe --no-such-switch` exit 2 |
| Crash probe | `RedPrism.exe --crash-probe <directory>` (a directory under `.build\`) exits nonzero and leaves exactly one `RedPrism-*.dmp` above zero bytes with a `pending.txt` naming it ([`Core_CrashHandling.md`](../Core/Core_CrashHandling.md)); the directory is removed afterwards |
| Failure reports | Each test executable's hidden `--failure-report-self-test`, run first by `test.ps1` through `Tests/BuildProcessTests/FailureReportTests.ps1` within a bound, ends with exit code 3 and the CRT report on stderr in Debug and ASan Debug and with exit code 0 in Release; a run that blocks on a dialog fails the gate |
| WARP self-test | Hidden `RedPrism.exe --self-test --warp` exit 0: visible DxUi surface within 64 MiB with ≥ 1 composite, canvas surfaces allocated, a second frame does not re-composite the settled document, a splitter drag through `DispatchPointer` widens the layers panel by 40 DIP and narrows the hole after the layout re-sync, the foreground well opens the picker and a sampled color commits into the session palette, a cancelled background edit leaves it, X/D swap and reset, the opacity stepper records one `Layer Opacity` undo entry at 50 %, a painted layer shows the Move tool's box, a pointer drag commits one `Transform Layer` entry, Free Transform previews a typed rotation until Apply, Escape cancels, Shift+Right nudges ten pixels, Flip Canvas Horizontal and Flip Layer Vertical mirror, a Marquee drag selects a whole-pixel rectangle and a Shift drag adds one, M switches the shape once per press, dragging inside the selection moves it as one `Move Selection` entry, Escape and Ctrl+Z during such a drag put the outline back without an entry, a click inside deselects, the wand selects a painted layer's pixels with exact bounds, Ctrl+Shift+I / Ctrl+A / Ctrl+D drive the Select menu, three polygonal clicks and Enter close a `Polygonal Lasso`, Expand from the header grows it by five pixels, Escape cancels an outline, a Brush stroke through the pointer path with the compute engine shows a live preview and commits one sparse `Brush Stroke` entry drawn from the adopted texture without materializing, the Eraser clears it, `[` `]` Shift-`[` and `5` change the tip, Escape drops a stroke, Clone Stamp stamps a mark from a source, Spot Healing blends it away, Smudge drags it through a live warp, Blur commits, a Gradient previews and applies with Enter after a digit set its opacity, a Shape drag adds a Rectangle layer, Shift-U switches the kind, Escape drops a draft, the Eyedropper samples the shape's color, Fill with Foreground Color fills a Marquee selection, Ctrl+X cuts it to the clipboard (mirrored to the system clipboard), Ctrl+V pastes it in place as a new layer, Ctrl+I inverts it, a Ctrl-drag moves the selected pixels through the live preview as one `Move Pixels` entry with the outline following, Ctrl+Right nudges them, Delete clears them, Ctrl+T on a selection floats them (Escape restores exactly, Enter merges one `Transform Selection` entry), Ctrl+J copies them to a layer, Add Mask hides them, Ctrl+Shift+C copies the composite, Ctrl+L opens the Levels panel and its output white previews black through the display document, Escape restores, an input white of 128 turns the gray white and Enter records one `Levels` entry, the Filter menu's Gaussian Blur applies one entry, Shift+Delete opens Content-Aware Fill on a red block filled into a selection of the white plate and Enter restores it as one `Content-Aware Fill` entry, with everything selected the fill reports no source, refuses Enter and closes on Escape, a Hue/Saturation adjustment layer opens its panel and a hue of 120 rotates the red beneath it live on the canvas, Enter keeps its record, Shift+= steps the blend mode, a rail click through the pointer path activates its tool (plainly, with the window focused, under the button's tooltip, and held across settled frames after a tool switch, after a click on another rail button and with the opacity stepper focused), the File menu's flyout anchor lies under its item on screen and a point on the open color picker card counts as chrome while the hole beside it counts as canvas, every interactive chrome control has an accessible name and the window's UIA root exposes the DxUi tree (its parent link, at least eight named elements, a hit under the menu bar), a settings file of the self-test's own reloads live (a light theme, the sample ring off and a 300 DIP layers panel apply, a broken file changes nothing and is not rewritten, restoring it applies again), the high-contrast palette applies and `RefreshTheme` follows the system state, and a save records the window placement; an image opened with the forced Add as Layer answer joins the document as a layer and with New Project replaces it by an unmodified untitled document of the image's size, a modified document survives New under the forced Cancel answer and is replaced under Don't Save, Close Project empties the shell (status `—`, a frame still renders) and New brings a document back, Import Images adds two files as one `Import Images` entry, Move Layer Up and Down swap siblings as `Reorder Layers` entries, Group Selected Layers wraps the active layer in a folder, Ctrl+Alt+G clips a layer to the one below and releases it, each function the UI spec lists as unimplemented raises the notice count by one and changes neither the document, the history nor the tool, Canvas Size shrinks the document to half in percent with the ratio locked around the top-left anchor on Enter and grows it with a white Canvas Extension layer from the bottom-right anchor, Escape cancels a pending size, Image Size resamples to 325 × 205 at 300 ppi with nearest sampling (the red layer's pixels and box scale with it) and three undos restore the document, New Canvas creates a 300 × 200 unmodified document, and the Crop tool frames the canvas when chosen, resizes the frame to 640 × 400 by a handle drag through the pointer path, crops on Enter as one `Crop` entry, moves the frame by a drag inside it, drops it on Escape, squares it through the header's ratio, cancels, is chosen again and undoes the crop with Ctrl+Z (its own whole-canvas frame takes no Undo) and leaves the tool without a frame, the Rename Layer card renames the active layer through typed characters and Enter and leaves the name alone on Escape, Merge Down composites a blue layer onto a red one into one trimmed layer named after the lower one and undo restores both, the Layer menu's mask commands gray out without a mask and a layer's mask is targeted again once another layer was chosen (Target Layer Mask and back, with the captions and the row's badge tone; Unlink and Disable Layer Mask as their entries with `Mask off` on the row; a Shift-press on the row's Mask badge choosing that layer and turning its mask on again; a press on the row's name targeting the pixels; a plain press on the badge the mask), the rendering steps listed in [`Rendering_Composition.md`](../Rendering/Rendering_Composition.md) Validation pass (actual pixels 1:1, wheel and Zoom tool zoom, device removal at every frame stage rebuilt and drawn on, a failed canvas that keeps the chrome, a 17 000 pixel document through its proxy and read back at full resolution, readbacks without the preview, the adopted curve equal to its raster, a gradient on another layer previewing there, surfaces dropped while minimized or occluded, the minimum window size, the screenshot exit codes, repeated failures that ask to close through the save prompt, a gradient left pending on one layer when another is chosen ending with its own commit so each layer draws exactly as its raster), the keyboard steps listed in [`UI_ApplicationChrome.md`](../UI/UI_ApplicationChrome.md) Validation (every key through the window procedure: replacements, text fields, AltGr, the Command List, the menu bar, the Move tool's digits, the displayed chords), the session checks (a typed opacity typed back to its start, closed by a command, carried to a new document, cancelled and committed; the Rename card's own layer; Cancel at the save prompt keeping a panel, the size card, a crop frame, a shape draft and a pending transform; a live stroke on a clean document; Save under an adjustment layer's panel; a folder project saved in place; an image batch whose first file fails; Canvas Size over a pending gradient; a copy the system clipboard refused; capture loss mid-stroke and mid-drag; a cancelled splitter drag; a leave without a capture; a stroke dragged over the layers panel keeping the pointer; Space across a focus loss; Alt-press, Undo and a drag; a close inside a chrome callback; the session end and its settings save) and the integration checks of the 2026-09-23 review (a reopened project keeping no mask target; a dropped image centered on the drop point on the canvas and on the canvas's middle off it; Load Mask and Load Layer Selection saying why a 2100 × 2100 checkerboard loads nothing; budget refusals raising one notice each for Layer via Copy with and without a selection, Gaussian Blur and Merge Down; Copy and Copy Merged applying a pending gradient first; Merge Down under a pending gradient holding it; a pending mask gradient following Paint, and X and D flipping and resetting the mask palette; the rail's wells showing that palette in black and white whatever the colors, a well opening no picker while a mask is the target and a picker open on a well closing unchanged once one is; a typed "2.5" in Gaussian Blur's radius kept as typed; the Gradient Map picker closing with its panel and the next well pick reaching the palette; Image Size's unit pick without resampling storing the unit; the rail parsing no SVG while settled and at most two per tool switch; the palette zones, the curve editor, the levels bars and the hue spectrum reachable in the UI Automation tree; a press on the spectrum's hue bars moving no handle while one in its handle strip does), what a pointer move costs the chrome and its UI Automation snapshot ([`Core_PerformanceAndResources.md`](../Core/Core_PerformanceAndResources.md), "Pointer input and the chrome": a Hand pan writing and preparing nothing, a Space pan, a wheel step and a brush move writing only the overlay and the zoom text, a transform drag rebuilding no layer row or menu, a settled sync writing nothing, no snapshot published without a listening client and one per preparation with one, `WM_GETOBJECT` and the in-process root publishing a waiting snapshot, a card published without a client) and the canvas cursor's per-position decisions (no layer scan with auto-select off, one scan and one selection test per position, none outside the selection's bounds, the cursors unchanged), then 0 DxUi bytes and 0 canvas bytes when hidden |
| DocumentTests | `DocumentTests.exe` exit 0 (graph, `ProjectStore`-identical validation, history budget, mask placement helpers, collapsed rows and their mask state, clip release on delete, legacy insertion/deletion/naming semantics, palette, transform geometry/drag/snap/session/duplicate/group/mask/flip/distort cases ported from the legacy transform suites, selection geometry/coverage/session/move/modify/load/marquee and wand kernel/session cases ported from the legacy `SelectionTests` and `MagicWandTests`, sparse rasters and the brush kernel, strokes, masks, opacity, curves, crossings, snapshots, budgets, keys, expansion and selection clips from the legacy brush suites, listed in [`Rendering_Brush.md`](../Rendering/Rendering_Brush.md), and the retouch cases: spot healing for every mode, clone stamp alignment and tip families, blur, smudge, liquify, warp tiles, gradients, shapes with the redraw on scaling, and the eyedropper; and the pixel edits ported from the legacy `SelectionEditTests` and `SelectionClipboardTests`: fills, Clear and the Delete key, invert, masks from the selection, pixel moves and nudges, the clipboard and the floating selection transform; and the color adjustments and filters ported from the legacy `LevelsTests`, `HueSaturationTests`, `ImageAdjustmentTests`, `FilterTests`, `AdjustmentLayerTests` and `BlendShortcutTests`: levels, hue/saturation, exposure, gradient map, grain, blurs, noise, lens correction, adjustment layers edited through the panels, the blend-mode step and the HSL formulas; and the content-aware fill ported from the legacy `SmartEditTests` (`smart.*`): the stripes continue through the filled block, a flat background reconstructs and undoes, Cancel and a no-source fill leave the original, the grid grows to a selection past the layer's edge; and the Layer menu edits ported from the legacy `LayerTests`, `GroupingSelectionTests` and `LiveMaskTests` gates (`order.*`, `wrap.*`, `clip.*`, `clear.*`): sibling swaps and their refusal, single, folder and empty grouping with undo, the clipping toggle with a shared base, and the cleared session; and the sizes ported from the legacy `CanvasSizeTests` and `ImageSizeTests` (`canvasdraft.*`, `canvas.*`, `imagedraft.*`, `imagesize.*`) and the crop cases from the legacy `CropTests` (`crop.*`) listed in [`Document_Tools.md`](../Document/Document_Tools.md); and the merge plan and merge (`merge.*`: no plan for a bottom layer or an empty folder, Merge Down onto the lower layer in the anchor's slot with clips re-targeted and undo, Merge Group replacing the folder, a stale plan refused) |
| CompositionTests | `CompositionTests.exe` exit 0: the golden table in [`Rendering_Composition.md`](../Rendering/Rendering_Composition.md) including the C12 adjustment layers and HSL blends (blend modes, order, raster masks with placement, live masks and chains, clipping stacks, folder masks, transforms, residency and caching, viewport, sparse upload, stroke preview and adoption, compute brush parity with the CPU kernel) |
| IoTests | `IoTests.exe` exit 0: the list in [`IO_ProjectFormat.md`](../IO/IO_ProjectFormat.md) plus PNG/Gray8/JPEG codec round trips, remaining aggregate project budgets before allocation, adaptive project probes/reopened-input ceilings and detached ordered image imports, HEIF clean error, import onto a document (a PNG is not a ZIP project), and the settings store (`settings.*`: file names and paths, parsing with per-field fallbacks and rejects, serialization keeping unknown fields and a newer schema, the cold load of a missing, invalid and unreadable file, the live load, and saves that back up an invalid file, keep unknown fields, leave no temporary and create the directory) |
| Provenance | `DxUi.provenance.json` matches the lock; wrong-pin fixtures are rejected |
| vcpkg toolset pin | `Tests/BuildProcessTests/VcpkgToolsetTests.ps1` (no restore, no network): the default toolset is read from the installation's `Microsoft.VCToolsVersion.default.txt` (LF, CRLF, BOM) and a missing, empty or malformed file fails before anything is cloned; the overlay triplet is the source text in its own line endings plus the installation and `major.minor` pins, escaped for CMake and rewritten only when its bytes change; a chainloaded `asan-toolchain.cmake` is copied beside the sanitizer overlays and a missing one fails; the installation is the one holding the MSBuild in use; `vcpkg-install.ps1` pins all four triplets, reads the toolset before it clones and runs no vswhere of its own; whenever the DxUi pin is restored, the overlay text and the toolset read (and each failure) equal those of the pinned `Tools/VcpkgTriplet.psm1` and `Tools/VisualStudio.psm1` |
| DxUi source restore | `Tests/BuildProcessTests/DxUiRestoreTests.ps1` (hermetic): a fixture repository with tracked paths of 148 to 175 characters in `Specs/`, `Measurements/` and `docs/gallery` and a 107-character `src/` path restores into a destination deep enough that the `src/` file's Win32 path passes 259 characters (a destination of about 210 characters from a checkout at `D:\RedPrism-dxui`; the test refuses one of 245 or more, which could not be created) as the exact commit, with a clean `git status`, `core.longpaths` stored in the clone, the canonical origin and none of the three directories checked out; an existing source is left alone and an unknown commit is rejected. `-RealPin` does the same for the locked commit and runs the pinned `validate_consumer.ps1` (not part of `test.ps1`: it needs the sibling checkout or the network) |
| Build presentation | Framed pixel-art banner identity, even scene geometry, and interactive-terminal selection |
| Harness screenshot | `--screenshot <png> --warp --dpi 96` and `--dpi 144` each write a PNG of the empty-document shell (populated rail) captured after the first frame that reached the display (drawn even with the display off), whose client-area pixels are neither a single uniform color nor fully transparent nor smaller than 32×32 (`test.ps1` decodes the PNG and samples a grid of pixels), or exit 8 only when this system cannot capture a window — `test.ps1` treats exit 8 alone as SKIP, while exit 9 (a capture that failed or never happened) and any other nonzero exit fail the gate, and passes the output path to RedPrism.exe quoted as its own argument; `--demo transform --dpi 96` writes the transform demo (two painted layers, the rotated one's box and the header) and `--demo selection --dpi 96` the selection demo (an elliptical and a rectangular selection with the Marquee header) and `--demo brush --dpi 96` the brush demo (strokes on a backdrop with the Brush header and the tip cursor) the same way, and `--demo retouch --dpi 96` the retouch demo (a radial gradient, a rounded rectangle and an ellipse under the Shape header) and `--demo adjust --dpi 96` the adjustment demo (a Levels adjustment layer's panel with its histogram over a painted backdrop), and `--demo canvassize --dpi 96` the Canvas Size card over the transform layers (relative +200 × +100, the top-left anchor, a white extension), and `--demo crop --dpi 96` the Crop tool framing 640 × 400 of the transform layers with the shade and the header |
| Brush benchmark | `RedPrism.exe --bench brush` (hardware) and `--warp --bench brush` exit 0 and print `BRUSH BENCH` lines; the numbers are recorded under `Specs/TestRuns/` at each closeout ([`Rendering_Brush.md`](../Rendering/Rendering_Brush.md)), not gated |
| Kernel benchmark smoke | In Debug, Release and ASan Debug, hidden `RedPrism.exe --bench kernels-smoke` exits 0 within a bounded wait with redirected stdout and stderr. Stdout has exactly 13 unique `KERNEL BENCH name=` operation rows, each for 32×24 with `samples=2`, `warmup=1` and a nonempty hexadecimal checksum, plus exactly one `KERNEL BENCH REPORT status=ok width=32 height=24 operations=13` line. Three `BUFFER BENCH` pairs compare copy/adopt ownership in alternating order and must match the corresponding scalar-kernel checksums, with one successful completion report. `test.ps1` prints one PASS summary. This checks benchmark output and deterministic completion; it makes no wall-clock performance assertion and runs no full-size kernel benchmark. |
| Release packaging | `package.ps1 -Platform x64` exit 0 with the clean-extraction smoke (`--help`, `--self-test --warp` with a system-only `Path`, the packaged version identity) and `package.ps1 -Platform ARM64` exit 0 (the smoke is skipped on an x64 host); a release gate run before a release and at a closeout, not part of `test.ps1` ([`Build_Packaging.md`](../Build/Build_Packaging.md)) |

`ASAN_OPTIONS` for the probe is `halt_on_error=1:abort_on_error=0:detect_leaks=0`.

## Test style

### Immutable color-edit work (P1.1a)

`DocumentTests` runs `coloreditwork.*`: independent legacy Levels clipping/gamma/alpha numbers, preview reuse,
preview-off computation, one history entry and exact undo, and state-based no-op, cancellation and failure cases.
Opening tokens must reject moved layers, changed masks, changed selections and resized canvases both before and
after preparation. Newer settings, a reopened panel, another session, document replacement and undo restoration
must reject old results without touching a newer edit. Mask carrying includes its eager semantic thumbnail;
compatible name/resolution edits survive. Another layer consuming the remaining budget, or a refused blur-grid
increase, must preserve pixels, mask, history and last-applied settings. Content-Aware Fill without donors and a
processing failure have distinct results. Existing `hsv.preview-*`, filter and adjustment goldens remain required.

The WARP self-test must keep a clean document's changed Levels panel under Cancel at the save prompt, and refuse
Save before creating a file when Content-Aware Fill has no donors. The panel, document and history survive that
refusal. The existing successful adjustment-panel save/reload case still passes. This slice does not qualify
asynchronous dispatch, bounded inner-kernel cancellation or whole-host G1 timing.

### Cancellable filter graph (P1.1b)

`rasterwork.*` uses literal bytes for padded crops, clipped bases, sparse inside/outside fills, replacement windows,
nested graphs, the 64-frame traversal limit, extreme coordinates and cancelled/invalid reads. No failed operation
may publish a raster or mutate a shared source cache. `filtercancel.*` covers every filter's pre-cancelled result,
distinct no-donor failure, private sparse reads through public helpers, area/nearest-sampling literals and
transformed coverage. Existing noise/grain/blur/filter numeric goldens remain unchanged.

`coloreditwork.memory.*` checks selection counts through sharing/move, maximum Gaussian scratch, sparse source
copies, persistent coverage, fill scratch and exact-preview reservation reuse. HostTests exercises cancellation
and odd-width/padded rows through all six dispatched scalar entry points. The opt-in
[`--bench-filter-cancel`](Testing_Benchmarks.md#filter-cancellation-diagnostic) records real stop-to-return
distributions; those timings are not ordinary test assertions and do not establish whole-host G1.

### Deferred panel computation (P1.1c.1)

`coloreditdeferred.*` tests the actual immutable preview/histogram/commit functions and session apply paths.
Independent pixels and histogram bins prove that an old preview cannot supply a newer commit, while an opening
histogram may arrive after a settings tick. State cases cover sparse capture without materialization, off/on
invalidation, Cancel/reopen/replacement, target moves and selection changes, current-result metadata mismatch,
exact reuse, grown-grid cache changes, pre-cancellation and preparation memory. Adjustment-layer parameter ticks
preserve the fixed opening histogram but cannot create a destructive commit. `selection-loops-bounds.*` covers
finite filtering, empty/degenerate geometry, shared copies, moves and unchanged geometry equality.

These tests qualify the data and publication boundary. Host mailboxes, real-loop input, busy controls and
file/close continuations have separate P1.1c gates; no host responsiveness claim follows from these direct tests.

`--self-test --warp` additionally gates the production histogram and preview jobs with owned events. While the
histogram is blocked, the shared pump dispatches a message and the Zoom In command; the unchanged opening histogram
then arrives after a settings tick. Thirteen pending settings changes produce exactly one running and one final
preview, with the final settings sequence displayed. A completed-but-undrained old preview is cancelled and its panel
reopened; that result cannot enter the new panel. All gate waits are harness failure bounds, never timing assertions
or production waits. The real UI benchmark smoke verifies the command/result/frame ordering in the outer host loop.
Busy Commit controls and file/close continuations remain separate P1.1c.3 gates.

P1.2 extends `coloreditdeferred.*` with immutable GPU lookup/source publication, selection coverage reuse,
pre-cancellation, preview off/identity/Cancel/reopen and stale selection checks. Its CPU commit uses independent
literal Levels pixels and must not reuse the GPU preview as a CPU result. Live adjustment lookup publication may
display an intermediate result without rolling controls back. `CompositionTests` compares pass 8 against CPU
filter/selection mix and ordinary composition for every kind, including partial alpha, layer-space grain,
translated/scaled placement, masks, opacity, clipping, folders and unrelated layers. Lookup counters prove reuse;
hidden release/device recreation, settled zero-pass frames and committed readback bypass are explicit gates.
The real host event gates additionally prove live worker lookup publication, the final GPU pixels, immediate
preview off while the worker is held and cancellation of late lookup work.

### CPU dispatch and instruction boundary (P0.8)

The 56 `isa-compact-rcx-byte-remap.*` controls prove the compact ASan stack guard, same-displacement EAX reload
and eight contiguous PE/listing rows; both anonymous tables, exact procedure end, literal index range and decoded
body/default targets are required. Missing/unknown/duplicate/overlapping or PE-mismatched rows fail independently.
Extreme valid bounds/displacements pass; a later data symbol cannot rescue a missing guard. Adjacent real VEX
and EVEX remain violations while the seven synthetic DWORD rows remain proved data. The runtime allowlist is unchanged.

`IsaRdiBaseByteRemapTests.ps1` adds 57 synthetic controls for Release OnCommand's RDI image-base remap:
seven accepted forms/boundaries (guard maxima 1 and 4095, canonical alignment/no padding and an unknown-decoded
table row proved from PE bytes) and 50 refusals. The exact nine-row unsigned guard/CDQE/base/byte-and-DWORD
dispatch requires PE/listing agreement, same-owner anonymous PDB starts, procedure/contribution bounds, valid
byte indices and decoded body/default targets. Direct bypass into the protected sequence or table data fails.
Malformed bytes/rows, symbols/owners/bounds, counts/indices/targets and adjacent real VEX/EVEX are independent
controls. No synthetic code is executed and no runtime allowance is added. The unchanged failed Release image
and all nine parser suites are retained with source hashes in the selection-move host test-run record;
this standalone proof does not replace the native configuration gates. A valid decoded body target and the
same row marked unknown form a paired control; this isolates target decode validity from table bounds.

The 183 `isa-inline-table.R10AlGuard*` controls prove the exact non-clobbering R10 prelude and AL-bound dispatch:
every protected row independently rejects missing, duplicate, unknown, overlap and PE/listing mismatch; valid
extreme bounds, separate default, backward table target and direct entry at the base definition pass. Bypassing
that definition fails. Same-owner table/procedure ends, decoded body targets and terminal return/security-failure
rows are required. Real VEX/EVEX in the body and immediately after the table remain violations with the proved
table data count intact. The original archived Release image is rechecked independently of a rebuild.

The synthetic ISA parser controls cover DxUi typography's exact `LEA RCX,imagebase; MOVZX EAX,DL; DWORD load;
ADD; JMP` form. They require the anonymous same-owner untyped PDB start and retain negative controls for wrong
register/opcode, extra or missing instructions, invalid symbols, contribution/decoded-target bounds and real
vector code beside the valid table. This proves data recognition; it does not grant the containing object or
function an instruction exemption.

The exact WIC RDX-base/RCX-target dispatch additionally has seven-DWORD fixtures with required anonymous PDB
start, malformed dispatch/symbol/target/bounds negatives and adjacent real-vector rejection. The standalone
parser suite has 368 controls including 29 RDX-form cases, 45 exact Menu R9-base/R8-target cases and 24
signed-byte RBP+disp32 cases. The signed form requires the exact MOVSX/LEA/load/ADD/JMP encoding, an anonymous
same-owner table start including its biased default entry, and decoded target bounds; wrong REX/opcode/frame
base/SIB, noncontiguity, invalid symbols/targets and adjacent real vector code remain rejected. The Menu form
requires its byte-index load, unchanged base/indices/targets, anonymous PDB start and decoded-target bounds;
missing/changed operands, noncontiguity, malformed PDB evidence, truncation and adjacent real VEX remain rejected.
[Original forms record](../TestRuns/IsaTableForms_2026-09-30.md).

The ASan grid switch additionally has 37 short-stack-guard/terminal-DWORD controls. Recognition requires the
exact unsigned CMP/JA/MOVSXD and contiguous RCX dispatch in the PE and matching decoded listing, anonymous
same-owner data start, guard-derived entry count equal to the exact PDB procedure end, and every target at a
same-procedure decoded boundary. An unrelated later data symbol does not extend the table. Missing/changed
bytes, mismatched stack slots, ranges, symbols, procedure ends and targets refuse; real vector code after the
proved table remains gated. No DxUi object, procedure or new runtime allowlist exemption is added.

`IsaWideStackGuardTests.ps1` reruns the original inline-table controls and adds 54 independent
`isa-inline-table.TerminalDwordWideStack*` controls for the observed ASan EndCanvasEdit switch. The seven
contiguous CMP/JA/MOVSXD/RCX-dispatch rows require exact PE/listing agreement, full disp32 stack-slot equality,
unsigned bound 1…127, same-owner anonymous table start, exact terminal PDB procedure end and decoded body/default
targets. Every high displacement byte is tested independently; malformed opcodes/SIB/registers, missing/unknown/
overlapping/mismatched rows, incorrect metadata/end/targets and an unrelated later symbol refuse the data proof.
Adjacent real VEX remains a violation while all eleven DWORD data rows are still recognized. Recheck the exact
archived failing image before rebuilding, with the runtime allowlist and executable/runtime/tier counts preserved.

The ASan `std::variant` visit switch additionally has 73 QWORD-stack/RAX-base controls: four valid tables and
69 refusals. The seven contiguous CMP/JA/LEA/MOV/load/ADD/JMP rows require exact PE/listing agreement, equal
full stack displacements, an unsigned bound 1…127, an exact image base, anonymous same-owner aligned data,
an immediately preceding RET, an exact terminal procedure end, decoded body targets and a guarded fallback
among those targets. Changed operands, guards, row boundaries, symbols, bounds, targets and incoming direct
edges into the guard interior or table from elsewhere in the procedure refuse. Adjacent executable VEX stays
a violation. Reparse the original failing ASan image and retained Release image before rebuilding, keep all
57 RDI controls and the existing runtime allowlist: [proof](../TestRuns/IsaQwordStackRax_2026-10-03.md).

The Release clipboard status switch additionally has 91 AL-guard controls. Its exact contiguous MOVZX EAX from
RSI+disp32, CMP AL/unsigned JA, RDX image-base/load/ADD/JMP rows require matching PE bytes and decoded listing.
The anonymous same-owner PDB start and guard-derived table end must match the containing procedure; all default
and table targets must be decoded boundaries inside it before the terminal table. RET and at most three standard
alignment bytes precede the data. Bounds 1/6/127 and a changed source displacement remain valid; malformed
registers, scales, bounds, instructions, listing/PE evidence, PDB ownership, targets and alignment refuse. Real
VEX and EVEX after the table remain rejected. This is parser evidence; it does not execute synthetic code or
grant an object/procedure exemption.

The Release `std::variant` destruction dispatch (signed byte index, R8 image base, RDX target, byte table biased by one entry)
has 93 controls that rebuild the bytes observed in `ApplicationFileJobs.obj` at their observed addresses (DWORD table at RVA
0x962dc, byte table at 0x962ec ending the 114-byte procedure). Three images are accepted: the observed one, one with a leading
instruction in the procedure and one in a section at another address. Every refusal changes one thing: an instruction of the
dispatch (28: each register, width, opcode and prefix, a short displacement, a missing byte load, an unsigned, unbiased or
ECX-target variant and each noncontiguity), a row's listing against the PE (24: mismatch, unknown, overlapping or missing, per
row), a PDB record (missing, one byte early or late, wrong owner, named, typed, wrong size, or a second record inside either table),
the bias (none, two, negative, or the DWORD load biased by one entry), the procedure or contribution bound (6), an index byte at
or above the entry count (2), or a target in the previous or next function, inside an instruction, undecoded, or inside either
table (6). A genuine VEX instruction in the procedure, in the alignment before the table, on the first byte after the procedure
or in the next function (4) stays a violation while the two table rows stay data. The real Release images of the merged tree
pass: [record](../TestRuns/IsaSignedBiasedByteTable_2026-10-01.md).

HostTests validates every required AVX2 feature/OS-state bit independently, irrelevant-bit tolerance, host
baseline detection, Scalar/Auto selection, unavailable-tier refusal and preservation of the previous table.
It checks the actual six scalar function entries and padded, odd-width alpha/transparent adjustment inputs.
Existing DocumentTests and WARP numeric goldens still exercise the extracted arithmetic through the public API.
The CLI table tests all five names plus missing, empty, duplicate, unknown and `auto` rejection.

The kernel smoke runs with `--isa scalar` and requires `isa=scalar` in all 13 operation rows. Separate hidden
processes prove unimplemented, foreign-architecture and malformed tier requests exit 2 with the expected
diagnostic. The ordinary WARP self-test also exercises Auto initialization.
All x64 builds run the PE/PDB-attributed VEX/EVEX gate and compiled passing/failing controls described in
`Build_Toolchain.md`; these controls never execute their high-ISA code. ARM64 cross-builds are compile evidence,
not native execution. Pre-AVX execution under Intel SDE remains optional and must be recorded if performed.

### Job input and progress policies (P0.6b)

HostTests covers one-active/latest-pending preview ownership, stale epochs and refinements, all eight continuation
slots, refusal ownership, ring wrap, wand coalescing, shared stroke ordinals, cross-class history exclusion and
explicit failed-stroke retry. Literal virtual-QPC cases pin the 250 ms threshold without sleeps or host-speed
assertions. All 32 end-session flag combinations have literal outcomes. Fixed activity records reject stale progress
and retirement after slot reuse; an event-gated scheduler observer case proves UI-thread notice ordering and lifetime.

Retirement-aware queue tests retain independent receipt references across superseded, stale and cleared entries.
Literal metadata and FIFO expectations cover ring wrap, callback-before-release ownership, active entries surviving
Clear, transferred entry lifetime, no callback while history is busy, and full/invalid admission retaining caller
ownership. These qualify per-entry retirement, not the production runner's typed payloads or receipt publication.

`continuation-receipt.*` tests the real observer/publisher model: first terminal outcome/error, invalid and pending
publication refusal, copies/moves surviving publisher destruction, moved-from destruction, unique publication,
pending abandonment and immediate failed admission values. Actual retirement-aware queues cover full/invalid
refusal retaining ownership, stale and superseded outcomes, FIFO Clear keeping the active input pending, and
destructor cancellation fallback. This does not qualify the production construction boundary's allocation failure.
`continuation-request.*` tests the actual typed host inputs: original Unicode path order/drop point, write options,
all size-draft fields, layer/name ownership, panel/document and session-end keys, move-overwrite retirement and
retained outcomes after input release. These qualify owned inputs/results; actual caller effects, bounded payload
admission and staged runner activation remain P1.1c.3c work.

The hidden WARP `continuation-host.*` checks the Application's actual registered source with owned numeric panel
inputs and immutable commits. It covers construction `bad_alloc` injection, eight pending plus ninth refusal,
Clear and idle, an ordinary UI-action queue at capacity, source fairness/disarming, exact acquisition validation,
real pixel/history apply before later terminal receipt publication, acknowledged active cancellation, scheduler
`bad_alloc` error preservation, late success before Cancel/replacement, reopened panel/document lifetimes,
Cancel after acknowledged success/failure preserving that terminal outcome and its effects,
immediate adjustment metadata settlement and unregister/retirement at shutdown. An independent earlier color
commit and explicit finisher seams for Selection/Stroke/Open/Import prove waiting-request wake-up through the
common completion hook; the seam does not qualify those operations' future asynchronous implementations. Its wait
has a fixed failure bound, not a latency gate; production UI never uses that wait. These tests do not activate Save,
Close, import or other producers, nor qualify variable-length payload admission or the complete caller matrix.

DocumentTests runs actual direct session operations through the ordering state machines to check that a stroke
uses the predecessor's applied pixels (or the surviving pixels after failure/cancel), separate undo entries, Undo
after a pending stroke, and replacement/stale-preview guards. This qualifies ordering foundations, not the P1
asynchronous tool migrations. Existing brush/adjustment numeric goldens remain required.

The WARP self-test submits a gated job through the production scheduler observer, advances visibility using exact
virtual QPC boundaries, prepares the status control at 96/144 DPI, tests determinate/indeterminate states, and
settles the job through the shared pump. Completion removes its indicator and timer; a stale timer cannot revive it.
Both showing and hiding the bar across a held rail click preserve that click through an intervening preparation.

### Job scheduler and host pump (P0.6a)

HostTests may create a **message-only HWND**, without a device or visible window, to prove the actual shared
`PumpMessages`, `DrainCompletions`, `RunUiActions` and `SettleJobs` path. A worker signals entry and waits behind
an event; a posted window-message sentinel must dispatch before its completion, and releases that event. The
finished result applies once, the cancelled variant applies no successful result, and both release every payload.
Expected values come from these independent state transitions, not the implementation's queue layout.

Additional gates cover bounded admission with retained caller ownership on refusal, FIFO through apply/drop,
independent serial keys, stale tickets, progress, the job-runner `bad_alloc` failure boundary, one action per
pass, rejected reentrant drains/actions, and shutdown that joins before destroying active payloads. Gate waits
are failure bounds only; no sleeps or timing assertions. Existing legacy image goldens remain unchanged.

`job-stage.*` covers successful multi-stage deferral, retained reservation/FIFO, exact resumed generations,
duplicate/stale/slot-reused tickets, resume inside a hook without worker re-entry, false-hook retirement even
after requesting resume, cancelled/failing stages and suspended shutdown. An external owned event publishes
readiness through a joined wait callback. Pending-only owner work prevents idle settlement without claiming
UI work; ready Pump executes under the reentrancy guard and refuses callback-time registration/removal.
Synthetic memory checks prove successor admission remains blocked through suspended, resumed and final apply
stages. State/event references are independent of elapsed-time performance; no hook waits on another stage.

`retrywait.*` checks literal 50/100/200/400/800 ms deadline arguments, eight registrations/ninth refusal, invalid
and duplicate tickets, exact reached creation/registration/reset/arm errors, real event acknowledgement and
slot reuse. A real relative deadline also completes without a test signal. Three gated actual-job scenarios
prove an unrelated job runs on the single compute worker while the original 4096-byte reservation and receiver
remain held: ordinary waiting, cancellation and failed cancellation signalling. Callback publication stays
behind an event until the unrelated job completes. Cancellation skips the resumed worker body; a failed signal
retains its exact error until the original deadline acknowledges. There is one receipt/final result and no
leaked slot/reservation. Wait bounds detect deadlock; they assert no performance threshold. Multiplexed GPU
copies additionally initialize both native sources, exercise normal and EnqueueSetEvent cancellation routes,
and detach them after shutdown. The independent transformed Copy fixtures cover rotation, reflection,
nearest/smooth centers, High sampling and a placed mask's outside tone through a cropped selection.

`gpu-capability.*` covers literal completion-route prerequisites and unavailable/failed-query combinations,
plus real WARP immediate-context probing, null/deferred/mismatched-device refusal and report reset.
`gpu-work.*` uses independent BGRA coordinate formulas for cropped row-pitch copies and single-pixel reads.
Actual scheduler stages copy mapped rows only on a worker and acknowledge them on the owner. Cases include
cancel after mapping, a device rebind while rows remain lent, duplicate lease refusal, eight-slot admission,
two-copy in-flight bounds and 64 MiB staging saturation that waits for an owned acknowledgement message.
Shutdown holds an actual mapped copy stage until the shared shutdown event, checks that its pointer remains valid
while the scheduler joins, drops its final callback, and releases the mapping/reservation only after join.
The queue runs without an HWND or Present. On a supported WARP device, additional copies explicitly select
the EnqueueSetEvent fallback, including cropped rows, cancellation and lent-device lifetime.
Full 2048 squared copies additionally inject first-map busy and persistent busy on both completion routes.
One additional real DXGI event permits one further nonblocking Map; persistent busy has one failed delivery and
no lease or partial pixels. Holding owner consumption of the recovery event proves cancellation, replacement
and shutdown while that stage remains pending. Negative rearm/enqueue/removal seams prove exact errors and
released ownership. Reusing the queue for a full then cropped copy proves each admission gets a fresh recovery
allowance, with independent coordinate-formula bytes and bounded storage. These are injected lifecycle failures,
not claims of physical device removal or a maximum GPU-event delay.

`document-readback.*` exercises the actual tiled worker graph and distinct utility targets, with a serial BGRA
coordinate formula for full, cropped and single-pixel output. It verifies one reservation, immutable input,
owner-only recording/mapping, worker-only copying, adopted output, hidden display release and settled display
pixel preservation. A folder/clipping/mask/adjustment fixture compares the asynchronous result with the committed
synchronous traversal at tile seams. Actual replacement WARP devices prove whole-graph restart, no partial
publication and failure after two restarts. These are deliberate rebind tests, not physical device-removal tests.
An injected persistent busy second tile proves that an acknowledged first tile is never published as a partial
Raster: exactly two copies, three Map attempts, one readiness barrier and one terminal failure, with no restart.

The application WARP self-test holds a real Merge worker with owned events after final GPU acknowledgement.
A younger Invert must apply after exactly one merge history entry. Cancel stays pending until acknowledgement;
injected failure, stale opacity and device replacement prove exact receipts, unchanged pixels on drop, hidden
completion without Present, and release of utility, staging and scheduler ownership. At the CPU gate lent mappings
and utility targets are zero. Replacement after detachment retains authoritative bytes and performs one CPU
preparation; the separate tiled-graph tests still prove bounded restart before acknowledgement. Merge adds all
five history-allocation faults, readback/probe capture refusal, worker probe allocation refusal, epoch-only
invalidation and probe cancellation/staleness with seeded redo preserved. A 30,000-square blank canvas completes
its no-plan command with a metadata-only reservation, no readback capture and no history change. Every owned probe
and readback aggregate retires off the owner; each terminal case leaves no barrier, GPU work or reservation.
`MergeHistoryTests` and `MergePlanProbeTests` add 71 independent literal-pixel, trim/group/clip/budget, immutable
planner, no-plan, exact-authority, moved/single-use, fault and cancellation assertions. Native qualification is
tracked in P1.9 H1. Remaining readback callers and G1 timing remain open.

`HistoryRestoreTests` adds 63 independent restore-publication assertions: literal pixels/opacity/mask/active id,
actual-restore crop removal/no-entry crop retention, no-document Redo, selected-snapshot memory floors,
default/foreign/moved/single-use authority, exact history epoch and both directions of all three restore allocation
and cancellation stages. A later saved revision and newly begun unrecorded lasso cannot corrupt live state.
The production self-test adds 28 `history-restore-host.mode=` scenarios, including ordered younger edits,
selected receipt lineage across document removal, empty/nested no-op, stale active/mask/crop/generation/epoch/lasso,
capture/worker/history refusal, cancellation acknowledgement and complete aggregate retirement off the owner.
Every terminal case requires no hidden Present and returned reservations/barrier/ticket. Crop/opacity host tests
wait for acknowledged real restores before inspecting results; pending-edit consumes retain their immediate rules.
All six configurations qualify this slice ([evidence](../TestRuns/HistoryRestorePublication_2026-10-01.md));
these functional gates assert no latency threshold.

`ImportHistoryTests` adds 55 independent reset/baseline/edit/trim-publication assertions, including literal
Undo/Redo and all six replacement/edit construction and cancellation checkpoints with the old state preserved.
`ImportPublicationTests` adds 157 complete session assertions: initial/extra/existing image semantics, exact
authority/history staleness, moved/foreign/single-use refusal, later saved revision, project active-id/empty
document rules and refusal without mutation. Twelve controls cover both project and first-image replacement
snapshot construction, injected allocation/cancellation, exact old history/target and literal source pixels.
Actual import/project-open host tests additionally require
capture reservation, exact history faults, isolated active/mask/crop/epoch invalidation and aggregate retirement
off the owner. The live-context controls are described below. Final native qualification is recorded in
[ImageImportHistoryPublication_2026-10-01.md](../TestRuns/ImageImportHistoryPublication_2026-10-01.md);
functional gates make no measured latency claim.

`HistoryEntriesTests` adds 28 independent immutable-container assertions: zero handle copies during capture/read,
captured integer contents, clone-before-mutation, empty/moved/swap state, weak-owner retention and four concurrent
readers with a separate cloned writer. Deterministic throwing-handle fixtures prove partial copy refusal leaves
root identity, contents and original ownership intact for push/pop/erase; they do not inject the vector allocator.
Existing prepared-history allocation checkpoints still gate the complete construction/publication boundary.
`OptionalImageThumbnailTests` adds 30 assertions covering empty/explicit derived image caches, literal sparse
nearest samples without materialization, equality/no-op redo, invert/resize/prepared color/prepared merge/CPU
brush pixels, and eager white-to-black semantic mask thumbnails. The legacy retained-thumbnail byte golden supplies
an explicit nonzero image cache. IoTests retains its original mixed-gray fixture and checks all four literal
thumbnail bytes, while successful image decode leaves no thumbnail. Native qualification is P1.9 H2.1/H4.1 work.

`HostTests` command-line cases additionally prove bounded positional paths, `--` leading-dash paths, empty/null/
overlong/count refusal, harness mode conflicts, marker-aware help/self-test preflight and atomic parse output.
Accepted startup paths are copied into an owning request before the argv storage can be released.

The registered UI-action-source gates cover a full ordinary action queue, retained ownership on refusal,
coalesced readiness, source/ordinary fairness, self-rearm on a later pass, event/idle state, refused reentrant
drains and callback-time removal, exact identity, stop and borrowed-source teardown. History-work gates cover
sequential stages under one selected ordinal with distinct acquisitions, late same-kind completion refusal,
strict initial Begin ordering and document/owner replacement. DocumentTests combines a committed pixel edit and
Canvas Size under one request, then a younger queued Undo: the first Undo removes only size and the next restores
the original numeric pixels. These qualify primitives; other production host continuations remain separate work.

The WARP self-test also simulates an early mode failure with a worker waiting on the shutdown event. The actual
`Application::Shutdown` must join it without applying its result, retain the implicit MTA through the callback,
destroy its payload after Run returns, and leave the caller's STA alive. `Main.cpp` calls that same shutdown
before clearing the resource satellite or leaving its COM scope on every mode return, including failures.

Standalone `JobShutdownRetirementTests` owns its own TLS sink. Its 28 independent controls use real 16 MiB CPU
owners and running, queued, memory-waiting, undrained jobs plus an owned UI action. Producer join precedes wrapper
destruction on owner; CPU destruction is exactly once on an MTA worker while reservations/events remain alive.
Useful callbacks stay suppressed. Stopped idle pools still run the ordered final hooks; repeated Close does not
repeat them, and invalid initialization/restart refuses safely. This primitive proof does not qualify the full
application's remaining live session/graphics graph or shutdown timing.

`JobCloseHookTests` adds 26 assertion records (excluding its summary): stopped idle pool, gated producer and
uninitialized Close. The joined owner hook follows wrapper destruction; final worker retirement is ordered,
exactly once, off owner and on MTA while borrowed context, reservations and wake events remain alive. Repeated
and uninitialized Close cannot run hooks.

Before constructing the ordinary application, `RunStandaloneProcessingSelfTest` drives 26 independent Application
owners: eight faults after successful owned-stage acquisition, sixteen faults at the existing retry observer
seams (EventCreation/RegisterWait in all eight slots), successful initialization and never-started shutdown.
Its 390 controls require the exact original HRESULT, each acquired checkpoint once with no later stage, exact
retry observer prefix/reached fault, matching live pool/routing/source/sink state, first/second Shutdown, detached
retry/GPU routes, empty ownership and unchanged caller apartment. Acquired pools acknowledge exactly one final
off-owner MTA hook; a prefix with no scheduler runs no hook. Each case owns its own trace/pool/sink and supplies
no window/device/chrome. The following complete graphics-host tests retain their separate scope.
Full x64 logs must report `application-processing summary: 390 checks, 0 failures` and 390 named PASS controls.
After-stage checkpoints test prefix cleanup, not failure inside those successful APIs. Retry seams return before
the selected native call; they prove the reached unwind boundary after earlier slots succeed. Native allocation,
scheduler/pool/GPU and other unintercepted initialization failures remain separate A3 work. No legacy golden changes.

`RunStandaloneLeaseShutdownSelfTest` constructs four independent hidden WARP/chrome Application owners with
their own pool/sink. Two actual DocumentReadbackJob workers hold real 2048-squared mapped rows through shutdown
cancellation, one after explicit queue device retirement. Literal BGRA rows must remain readable until worker
return; joined wrappers acknowledge the exact lease on owner before queue cleanup, where a stale release fails.
The other cases hold an actual RetryWaitQueue callback before readiness publication, or leave its readiness
undrained. A separately entered worker releases the callback after the actual shutdown stop token is signalled;
Shutdown and close hooks never pump or wait on a test gate. Callback join precedes receiver destruction, useful
callbacks stay suppressed, and the real 16 MiB CPU graph is destroyed once on MTA. Original ticket/reservation,
queue/event retirement, repeated Shutdown, caller apartment and consumed quit are independent observations.
The complete application must report `application-lease-shutdown summary: 56 checks, 0 failures` and 56 named
PASS controls. The complete private Debug WARP candidate passes these and unchanged application suites;
the fresh six-profile native matrix passes all three complete x64 suites and all ARM64 cross-builds
([qualification](../TestRuns/P1LeaseShutdown_2026-10-02.md)). Explicit queue retirement does not inject a driver/device-loss
failure. Clipboard leases, other live owners, initialization API faults and timing remain separate.

`RunStandaloneLiveShutdownSelfTest` constructs 24 independent hidden WARP/chrome Application owners: ordinary
and mask Paint/Erase, three healing modes, Clone, ordinary/mask Blur, Liquify and Smudge, each with lifecycle work
held before/after preparation. Its 432 controls require real 16 MiB source pixels, actual brush tiles or Warp
points, exact original target/transform/mask/history authority at joined owner detachment, then weak-only draft
expiration observed on the final off-owner MTA callback. Borrowed brush engines are live before shutdown and
detached on owner before CPU destruction. A separately retained literal BGRA source remains unchanged afterward.
Useful lifecycle publication stays suppressed; reservations/events/apartment survive the acknowledgement;
second Shutdown repeats no hooks and each destroyed owner consumes its quit before the next fixture.
The complete x64 application must report `application-live-shutdown summary: 432 checks, 0 failures` and 432
named PASS controls. The fresh six-profile forced rebuild qualifies these 432 controls in every complete x64
suite, with all ARM64 cross-builds and exact archived inputs/receipts
([record](../TestRuns/P1LiveShutdown_2026-10-02.md)). Mapped GPU/event/clipboard leases, other live owners,
initialization API faults and shutdown timing remain separate.

Before ordinary Application processing initialization, `RunStandaloneShutdownSelfTest` constructs two actual
hidden WARP/chrome owners with their own sink and pool. Its 52 controls cover lifecycle work held before/after
preparation, a real 16 MiB document/history, private clipboard and a live graphics-backed gradient draft. Producer
join precedes owner graphics detachment and final MTA CPU-root retirement; callbacks remain suppressed and tickets,
barriers, events, reservations and sink retire before acknowledgement. The caller's STA survives and second Shutdown
cannot repeat hooks. Each destroyed fixture's full message queue must consume its quit before constructing the next
owner. Fourteen added controls retain real 16 MiB sampling pixels, explicit four-point overlay geometry and
independent benchmark image roots before Shutdown, then require their retirement on the acknowledged final MTA
worker. Benchmark object identity, timing address, completion/result metadata and known-path cleanup survive;
second Shutdown cannot repeat the callback. The original 38 controls remain unchanged and separate.
Full x64 application logs must report `application-shutdown summary: 52 checks, 0 failures`.
Preview and delayed-clipboard shutdown controls retain their exact original source/history authority checks at
joined owner detachment, before CPU retirement, then assert empty session and independently retained literal pixels
after acknowledgement. These suites require 112 preview and 45 delayed-clipboard controls; existing legacy goldens
remain unchanged. Unintercepted initialization API failures, other live owners and GPU/event leases, residual hash capacity and
shutdown timing remain in P1.7 F5d A3.

The application lifecycle controls cover 115 New/Close/order/state assertions, 58 accepted-dimension/refusal
assertions and 85 Close cancellation/fault assertions. They test actual selected host inputs, pending-to-ACK
receipts, literal saved New Undo/Redo and empty Close state, palette/private clipboard retention, reached admission,
capture and history allocation faults, stale authority and younger input retirement. Invalid New Canvas dimensions
refuse before destroying a live draft; changing the card after admission cannot change the copied request. Existing
typed-opacity replacement goldens MUST still pass. Event-gated failure bounds detect hangs and assert no timing.

`HostTests.exe --bench-jobs` measures scheduler overhead with eight warmups and 200 single Commit jobs. It reports
submission and full submit-to-drain median/p95/max in microseconds, with an independently checked worker result.
Payload construction is outside the timed intervals; admission, cancellation-state allocation, worker dispatch,
completion delivery and payload release are included. Record Release numbers without a timing assertion. This
is not the UI/image/RAW benchmark and proves no image speed target.

### Asynchronous sizing and project durability (P1.8 / P1.7)

P1.8 adds `sizingwork.*` document checks for independent extension/nearest/mask bytes, private sparse-source
reads, pre-cancellation with no partial output, full-token checked apply, stale/invalid/replaced-document refusal,
one-entry undo/redo and rounded whole-graph reservation lower bounds. Existing `canvas.*` and `imagesize.*`
legacy numeric expectations remain unchanged.

The production WARP host route additionally holds actual size jobs on owned worker events before or after
preparation. It checks retained reservation/history ownership, copied dimensions/anchor/color/sampling/resolution,
panel commit → size → younger Undo order, early cancellation and a late Finished result after cancellation,
runner allocation failure, exact TooLarge/Invalid/Memory/failure errors, unchanged results, generation/restore/
document/card replacement, hidden completion without Present and complete terminal cleanup. Gate deadlines are
failure bounds, not latency assertions. Kernel cancellation intervals are structural bounds; these events alone
do not measure in-kernel stop latency.

P1.7 project durability checks inspect ZIP methods (STORE encoded PNG, DEFLATE manifest), load an independently
rewritten legacy DEFLATE archive, exercise ZIP and folder overwrite, preserve unrelated same-prefix paths,
require owned staging cleanup and refuse a locked destination while preserving its distinct previous bytes and
resolution. Separate `rotation-fault.*` controls inject every folder rotation/rollback/cleanup stage, retain
exact reported errors, compare literal loaded pixels and byte-identical old trees, and check publication truth.
Backup-only retries cover repeated failed rollback, successful restoration and successful new publication;
unrelated siblings survive. Real cleanup cases cover a read-only file, nested files, a mount-point junction with
an intact external target and the 128-level refusal. Injected flush/close faults remain open.
Native rollback-reader controls hold a descendant manifest open after rotation. One releases at the first
`RollbackRetryScheduled` observation and requires exact restoration despite late cancellation; another remains
open through exactly two event waits/three attempts and requires the exact access-denied error, complete `.old`
tree, cleaned staging and no false publication. Closing that reader then permits backup-only recovery. Test
observers control ownership transitions, never sleep or assert nominal timer latency. Read-only attribute reset
and deep-fixture removal are asserted explicitly.
Recovery timer create/arm/wait faults additionally preserve exact native error 5/32, the byte-identical backup,
staging cleanup and publication truth. Each surviving backup subsequently restores after its reader closes.
The directory fixture locks the backup directory itself; a child-file lock cannot stand in for native sharing
violation. Healthy save and final injected rollback-rename controls require zero recovery-hook visits.

Native `destination-retry.*` controls hold the current destination's child manifest or directory without delete
sharing before publication. Release at the first retry observation requires a complete replacement; exhaustion
and each timer resource failure require the exact old tree and initiating native error. Cancellation at the
retry observation leaves the original destination intact. Healthy and injected final rename failures require
zero destination timer create/arm/wait visits. Every case checks publication count, no rollback before rotation
and complete owned-staging cleanup. These controls bound attempts and preserve ownership; they do not identify
  an external reader/filter or assert wall-clock timer latency.

Native `publication-retry.*` controls hold the owned staging manifest or directory without delete sharing,
after all writers close. First-save and replacement cases separately verify release/retry, exhaustion,
pre-rotation cancellation, late cancellation with successful publication or rollback, Published count,
literal pixels, byte-identical original trees, earlier backup preservation, and owned staging cleanup.
Timer create/arm/wait faults retain the immediate native error; injected rename errors remain final with no
timer hook visits. A healthy native rename creates no timer. These are independently controlled filesystem
and cancellation transitions; no observer sleeps or asserts nominal timer latency.

P1.7 F5a adds literal `clipboarddib.*` and `imagecodecwork.clipboard-png-probe.*` byte/metadata controls.
They exercise independent padded/directed RGB, alpha/masks/palette offsets, premultiplication and literal V5
output, invalid/truncated headers, signed/hard budgets, empty precancelled output and unchanged sparse sources.
The 512 MiB size predicate and over-cap null PNG input avoid fabricated borrowed spans. These helpers alone do
not qualify production transfer or host contention. The production clipboard host's private matrix is recorded
below; frozen native qualification and F5d/G1 measurements remain open.

`project-save-memory.*` independently checks refusal of zero/overlong paths, invalid canvas dimensions and excess
layers; acceptance at path/canvas/layer bounds; name/path-sensitive charges; BGRA/Gray8 frame accounting; shared
raster references without charging a frame per asset; sparse-source identity without materialization; and invalid
curve limits. The production WARP `project-save-host.case=*` suite covers thirteen scenarios. Worker cases use owned events:
ordinary successful publication; a younger command held behind the save ordinal and then resumed; cancellation
before publication; cancellation after publication; cancelled close after publication; returned writer failure;
actual worker `bad_alloc`; stale document lineage; a later edit made after publication; pending transform settlement;
pending typed-opacity settlement; allocation failure at owning admission, before Run; and path suffix-space
refusal before Run. It checks pending
receipt/history ownership, reservation retention and retirement, caller-path copying, no destination before
publication, the independently loaded saved pixel/active layer, old path and revision preservation on prepublish
failure, captured revision marking without erasing a later edit, close suppression after late cancellation, younger
command success only after the save stage, persisted transform/opacity with their own history entry before the
captured saved revision, localized failure reporting and full terminal cleanup. These event gates
have bounded deadlock deadlines, not performance assertions. They provide evidence for project save ownership and
ordering. Clipboard host qualification is recorded below. Image-import host qualification is recorded below.

`project-open-host.phase=* after=* case=*` exercises 36 actual host-loop gates: probe/decode before/after
preparation, success, younger command staleness after replacement, cancellation acknowledgement, returned
failure, actual worker/admission allocation failure, reused persisted IDs, same-lineage generation change and
empty-session opening. Decode gates assert the complete declared decode charge without a surviving probe charge.
Nine `project-open-host.batch=*` gates additionally exercise two ordered replacements,
missing first file, first worker failure and decode-admission refusal followed by success, unsupported versions
99 and zero through real notice posting/shared deferred formatting, source growth after probe,
cancellation of the second project and external replacement during the second probe. They require exact receipt
errors, literal loaded pixels/active id, saved baseline/reset history, preserved path/document on refusal, one
notice per failed file, no hidden Present, view input while held, and zero retained tickets/charges/barriers.
Pure `history-work.*` replacement controls require the exact completed acquisition and the explicitly recorded
new lineage; old/unrelated lineages and late completion are refused. Independent load-resource floors compare
against PixelBuffer allocation, document record storage and manifest input contracts. The 22-row UI smoke includes
`project-open` after the independently completed save; timed readiness uses receipts and session/metadata state,
not synchronous file reads. Full configuration and performance qualification are recorded separately.

`export-readback-memory.*` adds nineteen checks for 4K/45 MP/100 MP admission, monotonic/path/metadata charges,
invalid side/count/curve/path limits, rounded full-canvas readback and shared sparse identity without materialization.
The fifteen `export-host.case=*` WARP scenarios cover PNG/JPEG literal output, younger mutation ordering, pre/post
publication cancellation, returned failure, worker/admission allocation failure, stale document, device changes
before encoding and after publication, captured quality/resolution, oversized output refusal before allocation,
path suffix-space refusal before Run, and a persistent second-tile Map failure after the first tile copied.
The latter requires no encode visit and no output file, exactly one failure receipt/notice and unchanged history.
Worker gates prove the request/barrier/reservation remain held with zero lent mappings/utility bytes; hidden
completion makes no Present. Device-change cases require one encode attempt. Failure notices occur exactly once;
terminal paths leave no barrier, reserved bytes, slot or utility target. PNG is exact opaque 191; JPEG RGB is within
3/255 and opaque. Export leaves project path/saved revision unchanged. Full qualification is separate from these
structural assertions; no event deadline is a performance threshold.

### File classification and banded codecs (P1.7 F1/F2)

`pathclasswork.*` IO tests independently cover directories, case-insensitive `.prism`, all three ZIP signatures,
short/missing/unreadable headers, counted nonterminated views, max/beyond-max counts and path lengths,
embedded NUL, invalid empty entries, empty batches and precancelled empty/nonempty results. `executor.*`
checks independent local/shutdown stop registrations, already-requested stops and retained local identity.
The WARP `path-classification-host.case=*` event gates exercise copied caller input, skipped empty entries,
queued younger mutation, before/after cancellation, returned failure, actual allocation failure, document
replacement, hidden completion and reservation/receipt retirement. These gates surround preparation;
pending driver IO and cancellation inside a WIC call are not fault-injected here.

`imagecodecwork.*` checks literal alpha PNG bytes, sparse Gray/BGRA multi-band round trips and sparse JPEG
quality-1 white-matte stripes straddling a band boundary (12/255 RGB tolerance away from edges). They require
the shared source to remain unmaterialized, complete precancelled output clearing, preserved distinct previous
destination bytes on cancelled/failed export, unrelated temp preservation and owned-temp cleanup.

### UI timing foundations (P0.1a)

HostTests runs `ui-timing-histogram.*` and `ui-timing.*` numeric and state checks. Independent expectations cover bucket
edges and nearest-rank percentiles, exact maximum and total overflow, QPC conversion (including one tick above
4 ms and integer overflow), nested captures, worker isolation, real message dispatch and scheduler apply/action
hooks. No test asserts an elapsed-time target. The private `pipeline-trace.*` ETW test also decodes timing event
names, durations and detail fields, owner-thread restriction and the strictly-above-4-ms thresholds; recording
or decoding failures and lost events fail the gate.

`Tests/BuildProcessTests/WprProfileTests.ps1` checks the trace profile with `wpr -profiles` and `-profiledetails`.
It never starts, stops or cancels a system trace; missing WPR or an invalid profile fails with diagnostics.
`HostTests --bench-ui-timing` reports 31 batches of 20,000 empty scopes with capture off/on, QPC medians, p95 and
maxima including loop cost. It refuses an already-enabled external timing session so its disabled-path numbers
are interpretable. This is instrumentation overhead only; operation baselines and G1 remain in the WIP plan.

`RedPrism.exe --bench ui|ui45|ui100|ui-smoke` is the opt-in application-loop measurement described in
[`Testing_Benchmarks.md`](Testing_Benchmarks.md). Smoke validates completion and output with no timing threshold;
its 24 operation rows include production project open/save, image imports and PNG/JPEG export workers.
normal test runs never download the RAW corpus.

Smoke may render with the session display off, while full measurements refuse that state. It validates ordered
elapsed distributions and the total Present count over two trials. Scheduler tests that inspect undrained job
completion use an injected stable memory source: an OS memory notification can wake a production scheduler before
any job has published its result. Memory notification/admission behavior is independently covered by JobMemoryTests.

### Worker foundations (P0.5)

HostTests also runs the standard-header `ExecutorTests` and Windows `WorkerPoolTests`. Their independent
reference is the execution/lifetime state table in `Core_Architecture.md`: irregular and maximum grid geometry,
single visitation, serial-formula equality, cancellation and incomplete-tile refusal, N simultaneous jobs on
N=1/2/4 workers, nested execution, implicit MTA, close retaining an active frame, bounded one-shot waits, stale
wait-ticket rejection, and a wait completion while every compute worker is occupied. Event gates force the
interleavings; 10-second waits are failure bounds only. A failed gate exits the isolated test immediately to avoid
destroying context still borrowed by callbacks.

`HostTests.exe --stress` adds 64 saturated/nested/close rounds. `test.ps1` runs this extended mode in ASan Debug,
plus both main-thread and two-worker crash probes in every configuration. The worker child must write exactly one
nonempty dump and its pending marker after overlapping faults, including one worker `std::terminate` path.

`HostTests.exe --bench-executor` is an opt-in Release measurement: an independently checked 4096-square coordinate
kernel through inline and 1/2/4/8/16/logical-processor executors, with warmup and 11 timed samples, plus empty-tile
invocation overhead, cancel-token check cost and a separate synthetic row polling-gap measurement. It makes no
timing assertions. Record it separately from image-kernel or UI/Photoshop claims. Native
concurrency SAL analysis uses `build.ps1 -Analyze`; Application Verifier qualification records the actual enabled
checks and executed binary under `Specs/TestRuns/`, and never substitutes for ASan or native ARM64 execution.

DocumentTests also runs `generation.*` and `history-revision.*`: owning result tokens and document/restore serials,
all session mutation families (including changes inside open edits), const session readers, shared immutable
selection snapshots, and marking the revision actually saved after intervening edits, Undo, branching and trim.
These are structural ownership/history checks; the existing legacy numeric tool goldens remain unchanged.

Tests are in-process executables under `Tests/` with one `Expect(condition, "suite.case")` per observable fact,
`PASS`/`FAIL` lines on stdout, and a nonzero exit on any failure. Goldens are numbers taken from the legacy Swift
tests, never "looks fine". GPU tests create their own WARP device; nothing in `Tests/` opens a visible window. Fixture
trees are materialized under `%TEMP%\RedPrism.IoTests`; a test that writes a file removes any stale copy first.

For diagnostic children that are started and then waited with a deadline, `test.ps1` retains the process handle
before waiting and reads the terminal exit code while it remains owned. A missing exit code never passes a gate,
including a crash probe that left its dump. Timeout handling and product assertions are unchanged. Qualification
records an unobserved process result as a failed run rather than inferring success from partial PASS output.

## Later gates (record now, implement with the feature)

The P0.7 HostTests additions use independent byte/accounting and event-order references: `memory-admission.*`,
`deferred-release.*`, `job-memory.*`, `pixel-recycle.*` and `pipeline-trace.*`. They cover no partial reservations,
waiters outside workers, failure/cancel/drop paths, serial-key admission through retirement, release with all job
slots occupied, pool capacity/slot limits, live buffers surviving pool close and concurrent borrowers. Under
ASan, checks inspect poisoning of cached payload and size-class slack and unpoisoning on reuse.

The ETW gate starts a private in-process TraceLogging session, includes synthetic and actual 1 MiB UI free
positive controls, releases real 16 MiB direct/adopted/vector storage through the worker queue, and decodes the
resulting ETL. It asserts no lost events, exact positive-control bytes and no UI free of at least 16 MiB. It needs
no elevated system-wide tracing session. Failure to record or decode is a failure, never an omitted test.

`HostTests --bench-memory` is opt-in measurement only: 8192×5464 BGRA, two warmup pairs and ten alternating fresh
and reused pairs; allocation, touch and release have separate QPC regions. Full checksums, page-fault deltas and
process-memory samples are outside those regions. No elapsed time is asserted by the ordinary test suite.

| When | Gate |
| --- | --- |
| C8–C13 | Tool and filter goldens ported from retained numeric golden cases behavior, not UI-driver scripts |
| C14 | Settings, crash, localization, UIA |
| C15 | Portable ZIP packaging |

GUI tests default to no-activation. Interactive desktop focus requires an explicit lease in a later spec.

## Screenshots

Screenshots for specs and docs MUST come from the application harness (Windows.Graphics.Capture of an owned window, or
an equivalent harness helper). Computer Use and desktop screenshot tools are prohibited as evidence.

## Fail-closed data root

When a test needs a writable sandbox for documents or perf traces, it MUST use `X:\RedPrism.Perf` (any local drive
letter plus a directory ownership marker), never repository `.build/` as a data sandbox. `ResolvePerfRoot` accepts
only that exact root grammar on fixed local drives. `REDPRISM_PERF_ROOT` selects an explicit root; otherwise the
resolver prefers an already marked X: root, then another marked local root, and finally creates a new root on the
Windows drive. An existing unmarked, malformed-marker or reparse root is never claimed or recursively cleaned.
The marker is `.redprism-perf-owner` with exact ASCII bytes `RedPrism performance root owner v1` followed by LF.
The ETW harness deletes only its own PID-named trace files and leaves the ownership marker in place.

## Skills

Skill or `AGENTS.md` edits MUST pass `.\validate-skills.ps1` in the same change.

## Required local matrix for a closeout

P1.1c.3 preparation adds two hidden WARP checks. The busy-control check uses real attached-host pointer dispatch
to prove Curves, Levels and Spectrum report while enabled, stop reporting through busy transition and preparation,
and accept fresh input after restoration. It also checks Cancel/OK enable states, numeric focus-loss behavior,
completed decimal preservation, suppression of late disabled-field callbacks,
and the histogram, Colorize and curve-removal prerequisites.

The ordered-commit check gates the actual worker on owned events. It proves no history/pixel change before apply,
mutation callback/Enter suppression, view input, rejection of a competing ordinal and release only after one
successful apply. Four gray inputs under Gamma 2 have the independent square-root expectation
`0, 128, 181, 255`. Cancel after pixels finish cannot close a new panel; stale identity cannot authorize a
dependent operation; valid identity adds no entry; returned `E_OUTOFMEMORY`, an actual throwing `Job::Run`
translated by the scheduler's `bad_alloc` catch, and actual donorless fill
retain their panel/original image and release their barrier. Ten-second bounds detect deadlocks, never gate speed.
Replacement deliberately reuses persisted document/layer IDs after the old pixels finish and proves session serials
prevent publication. Adjustment-layer metadata settles once without destructive pixels; a removed target returns
`Stale` without an engine history entry.
Production OK/write activation uses the typed continuation source. The hidden WARP `host-flow.*` matrix gates
21 selected inputs across actual donorless-fill failure, successful Gamma 2 settlement, active cancellation and
stale document/session-end lifetimes (84 cases). It checks output absence before acknowledgement, saved Gamma 2
pixels, surviving failure/cancel panels and history, distinct layer selection/reorder/rename/opacity effects, view
input during work and complete receipt/input-budget retirement. Header mutation callbacks cannot edit beneath
admitted work. File/drop/size/key harness observations pump only after their real producer returns.
`host-flow.shape-release-after-commit-with-owned-color` releases a real Shape draft behind a gated color commit,
changes the palette before execution and checks the captured red pixels and two separately ordered undo entries.
Session-end message tests cover generation-tagged deferred prompts, cancelled and critical queries, reason
ownership and callback-safe close. Remaining GPU/IO/history costs belong to the later P1 migration gates.

Host request admission checks the 1 MiB aggregate declared-storage boundary, overflow refusal, retained capacity
of small logical strings/vectors, 256/257 path counts and 32,768-unit path refusal. Receipt-allocation fault
injection runs after reservation and before moving input; the previous charge and caller's original paths survive.
Queue/active transfer, clear, stale retirement and move overwrite preserve charges through publication and return
them after input destruction. The hidden bridge independently proves quota/OOM refusal leaves its panel, queue,
history and wake state untouched and cancellation returns the charge. These checks do not activate file producers.

Borrowed-input request tests additionally prove original Unicode values/order after caller edits, absent versus
present-empty optional values, bounded terminated-pointer scans, maximum path/batch acceptance and whole-batch
aggregate refusal. Enabling the copy-allocation failure control during quota/length/count refusal must not throw:
refusal precedes copying. Faults after a partial batch/string copy and before receipt allocation preserve prior
reservations and borrowed values and return all temporary charges. Successful admission reconciles to actual
retained capacities. Later selected output tests cover the same budget, pending-owner restriction, immutable
copied value, old/new replacement peak, failure preservation, queue/active transfer, terminal publication while
storage remains charged, and move-overwrite/destruction with independently retained receipts.
`host-path-read.*` additionally proves direct admitted population after whole-batch length preflight: refusal
before reader invocation, reader failure after a partial batch, partial input/receipt allocation faults, owning
Unicode order, maximum counts/lengths, queue/active transfer and terminal-publication charge lifetime. Shell/drop
producers retain their source owners through this immediate copy boundary; no reader survives in a queued input.

Offline corpus controls assert the real default manifest covers all nine planned v1 extensions, the extension
allowlist, role selection, traversal/schema/license/ownership rejection and preservation of mismatched images.
When all local selected files exist, they verify every size/hash and the byte-identical local CC0 text and exact
manifest snapshot. Missing local corpus is reported as SKIP in this ordinary suite; P4/P5 qualification must run
the required real-file checks without SKIP. No test downloads files from the internet.

File IO cancellation qualification includes independent stream-cap/COM/lifetime tests, direct sparse PNG/JPEG
file round trips across codec bands, and project stops at manifest, asset, ZIP close, staged and published
boundaries. Cancelled loads expose no partial result; cancelled imports preserve original canvas/active/layers;
prepublication saves preserve the complete destination/sibling tree and clean owned staging. A stop after
publication still returns Ok and the saved literal pixels/metadata. The separate explicit `--bench-file` harness
records time/memory before output verification; its measurements and the remaining file-I/O host workers,
pending IO waits, faults and cancellation latency qualification are tracked by the P1.7 child. Clipboard host
qualification is recorded separately below.

The hidden import host gates cover existing/grouped/new/empty targets across probe/preparation before/after
boundaries: literal pixels, captured paths, view input, exact reservation retirement, one named history entry,
new saved baseline, younger Undo ordering/staleness, acknowledged cancellation, returned/actual worker/admission
allocation failures, reused persisted IDs and generation changes. The admitted owner capture hook observes its
full reservation before source copying and has an actual allocation-failure control. A device change after
preparation cannot repeat decoding or prevent CPU-only apply. Batch cases require cached failure/partial-success
policy, changed pixel/encoded ceilings, mixed project/image ordering and retained first error. They assert no
hidden Present and zero terminal ticket/barrier/input/graph charges. Existing all-project gates continue to prove
completion without dispatching an empty image group. The 24-row smoke includes new-canvas and existing-layer
imports from the preceding successful PNG export; timed readiness reads receipts and session metadata only.

Prepared import publication also has independent live-context controls for destructive/adjustment panels,
layer/floating transforms, lasso, shape, brush, warp, gradient, pixel move and outline move. Actual allocation
and cancellation refusal preserve each live draft, source pixels, target, transaction depth, revision and redo.
Replacement clears them only after complete publication; weak preview owners remain in the release aggregate,
and a coverage-engine thread witness must observe exactly one owner-thread close before worker destruction.
Nested existing imports keep the outer name/opening/depth and record once on outer End, with literal Undo/Redo.
Actual selected project/image host routes include successful and failed live replacements and nested import;
fixtures explicitly clear a prior intentionally preserved draft before constructing the next independent case.

Io import controls independently verify opened-stream caps, literal first-frame metadata/pixels (including
two-frame TIFF), signed/hard pixel ceilings, per-path and aggregate preparation limits, cached failures and
PixelBuffer/record memory floors without materializing shared sparse sources. These gates do not prove
successful HEIF decoding on a host without its codec, RAW support, in-flight codec interruption or full G1 timing.

```powershell
.\validate-skills.ps1
.\format.ps1 -Check
.\test.ps1 -Platform x64 -Configuration Debug
.\test.ps1 -Platform x64 -Configuration Release
.\test.ps1 -Platform x64 -Configuration "ASan Debug"
.\build.ps1 -Platform ARM64 -Configuration Debug
```

### Shape worker matrix

`ShapeEditWorkTests` requires 76 numeric/state controls: literal rectangle/ellipse/rounded coverage and color,
alpha policy, checked dimensions and memory floors, names/group insertion, selection/live-shape identity,
one-entry Undo/Redo, cancellation checkpoints and stale publication refusal. Existing ShapeToolTests remain
unchanged. `RunShapeWorkSelfTest` requires 199 event-gated checks across 36 scenarios: actual pointer release,
ordinary leave/capture loss, input-time color, view dispatch while held, admission/capture/worker/history faults,
late cancellation, same-ID replacement, younger Undo and complete off-owner aggregate retirement without hidden
Present or residual tickets/barriers/charges. No focused-only bypass may enter native sources. Full native gates
and exact passing images are recorded in `Specs/TestRuns/ShapeWorkers_2026-10-01.md`; timing qualification remains G1.

### Mask worker matrix

`MaskEditWorkTests` requires 84 document controls (57 core plus 27 extra) for selection coverage on the layer grid,
reveal/hide polarity and explicit-empty behavior, fractional and invalid grids, eager semantic thumbnail, unchanged
source bytes, complete selected-layer IDs, one mask/selection/history publication, cancellation and memory refusal.
`RunMaskWorkSelfTest` requires 201 event-gated host checks for ordered admission and held view input,
before/after-preparation cancellation acknowledgement, allocation/history faults, stale source/selection/history/
replacement refusal, younger Undo, literal target/thumbnail/history results and complete off-owner retirement without
hidden Present or residual tickets, barriers or charges. The immutable private evidence records these control counts
and a passing full private WARP run (`Specs/TestRuns/MaskWorkers_2026-10-01.md`); they are private functional proof,
not by itself a native matrix result. The subsequent native incremental matrix passes all three x64 full suites
and all three ARM64 cross-builds against 496 frozen inputs, including 84 document and 201 host controls in each
x64 profile. Exact receipts and the post-qualification XML whitespace-only verification are in the same record.
Live brush/warp/selection/transform settlement remains P1.4/A6/S3;
no G1 or native ARM64 runtime claim follows from these controls.

### Layer via Copy worker matrix

`LayerCopyWorkTests` requires the 34 literal document controls recorded in
[`LayerCopyWorkers_2026-10-01.md`](../TestRuns/LayerCopyWorkers_2026-10-01.md): selected image/mask pixels and exact
origin, same-parent insertion, outline and mask-target state, complete unselected duplicate semantics, clipboard
identity/serial preservation, limits, cancellation and stale-publication refusal. The full private WARP application
run records 214 host checks for ordered admission, view input, cancellation acknowledgement, allocation/history
faults, staleness, younger Undo and off-owner aggregate retirement. The final incremental native r3 matrix then
qualifies 503 frozen inputs in x64 Debug, Release and ASan Debug suites and all three ARM64 cross-builds. Each
x64 profile passes the same 34 document/214 host controls and all unchanged legacy numeric goldens. Exact profile
receipts and the 297-file evidence archive are identified in that record; its earlier failures remain preserved.
Live brush/warp/selection/transform settlement remains P1.4/A6/S3; owner capture is H2, and G1 measurements and
the full P1 clean exit remain open. Cross-build results do not establish native ARM64 runtime behavior.

### Clipboard host matrix

`Specs/TestRuns/ClipboardHost_2026-10-01.md` records the final private full-application run. The actual WARP host
matrix passed 268 checks across 52 scenarios; New Canvas prefill passed 56 checks across 14 scenarios; delayed
rendering passed 39 checks across seven scenarios; publication/no-paint/retry fault controls passed 81 checks
across eight scenarios. Separate Copy Merged readback-budget controls passed 30 checks, the clipboard encoding
budget probe passed 37. Native DocumentTests also includes two independent aliased-ID replacement controls.
The later corrected native r9 matrix passes all three x64 gates and all three ARM64 clean builds against
480 frozen inputs: 268 command, 61 prefill, 41 delayed-render and 88 fault checks. The earlier private inventories
remain historical proof, and none establishes a timing bound. See the record for exact images and retained failures.

The production paths retain the selected host ordinal and acknowledged history barrier. Copy/Cut apply the
private result before encoding and OS mirroring, so a later encoding or mirror failure does not undo the private
publication. Paste shares
six total clipboard acquisitions between metadata Inspect and owned-byte Copy; retries retain the same admitted
record, reservation and wait receiver. `GetClipboardData` executes on a worker, including delayed-render calls.
Copy Merged keeps the opening document authority through tiled utility readback, releases utility mappings and
targets, then performs CPU Copy Merged work on the detached pixels. Copy Merged reservation includes the merged
pixel/readback owners and the phased encoder estimate. The encoder estimate sequences PNG and DIB phase peaks;
its 16 MiB WIC allowance is planning headroom, not a proven bound on opaque codec workspace.

The final private run exited zero and passed its application assertions. The later frozen native r9 matrix
qualifies all three x64 runtime suites and all three ARM64 clean cross-builds. G1, native ARM64 runtime behavior,
the hard WIC workspace bound, floating/stroke work, headroom and required measurements remain open.

### Capped memory encoder controls

IoTests runs independent IStream controls for literal read/write bytes, exact/over-cap unchanged state, signed
seeks, shrink/regrow/gap zeros, OS page rounding, bounded lazy commits, native reserve/commit failures, reached
cancellation, sticky cap refusal and retained COM byte-view lifetime. Real WIC PNG/JPEG controls prove exact
encoded-size limits and one-byte-short/one/zero refusal with empty public output, larger-ceiling clipping and
unchanged premultiplied/sparse bytes. Existing ImageCodecWork pixel/file preservation goldens remain unchanged.
Require full native x64 Debug/Release/ASan gates and ARM64 clean cross-builds before closing F5b.2b.1; full host
reservation/caller/retry/retirement/measurement proof remains a separate gate.

### Detached Paste and clipboard availability controls

DocumentTests verifies literal private/foreign origins and pixels, negative floor centering, first-unused names,
group descendants and expansion, exact 100 M aggregate equality without sparse materialization, selection/mask
clearing, prepared history, Undo/Redo, move/single-use and session/generation/history/private-serial refusal.
Cancellation and injected allocation failures at every history preparation stage leave owner state unchanged;
foreign Paste remains valid after a newer private copy.

IoTests independently verifies metadata-only availability even for malformed/over-budget offered image formats,
producer-order DIB selection, refusal to request synthesized variants of malformed/over-budget sources,
malformed PNG fallback, cleared Busy/cancel/native-close output and a deliberate
post-close clipboard replacement. Its availability and inspection retain the held sequence, and Copy rejects
the replacement. Fixture helpers use bounded event waits for external listeners; injected Busy remains one
  attempt. Pixel-preservation observations use the independent C9b `SystemClipboard::ReadImage` reference,
  holding one clipboard acquisition through decode; direct Inspect/Copy/Stale authority assertions stay separate.
  Literal dimensions and every expected channel byte remain unchanged. This prevents a verification-only
  gap between metadata Inspect and Copy from invalidating an otherwise coherent preservation observation.
  Direct Copy Busy diagnostics retain the immediate native error, attempt, clipboard-holder process and sealed
  inspection sequence. The foreign-replacement refusal check additionally reports its exact returned status,
  held sequence at the Opened observation, acquisition count and retained output bytes on failure. Diagnostics
  do not renew inspection authority, accept Busy as Stale or weaken the assertion.
  Fixture source-sequence acquisition also reports immediate failed Open/Close errors, holder/caller process,
  attempt and owner-window validity. Test fixture acquisition now uses the existing C9b harness allowance:
  at most 30 attempts with lazy 100 ms one-shot event waits between contention results (about 3 s scheduled
  waits; 200 ms per timer wait is a deadlock failure bound). Source capture, Inspect, Copy and Publish use this
  test-only policy; Copy retains its sealed Inspection and Publish retains the same PreparedImage. Stale,
  cancellation, partial publication and failed timer waits stop retries. Fourteen deterministic supplied-trace
  controls verify immediate success without a wait, success after five Busy results, exact 30/29 exhaustion,
  failed wait, Stale refusal, cancellation and partial publication. Existing 125 transfer controls, literal
  dimensions/channel bytes and direct one-attempt Busy fault checks remain unchanged. Production's admitted
  transfer retry budget is unchanged. Exhausted fixture acquisition still fails the original assertion.
  Full frozen native qualification is required before closing these foundations. Production host,
retry/lifetime, G1 and native ARM64 runtime claims remain separate.
