# RedPrism performance and resource contract

Status: current normative product contract
Last reviewed: 2026-10-02
Owner: `Application` frame loop, `Renderer`, `CanvasRenderer`, and DxUi adapters

## Mandate

Performance and low resource consumption are primary RedPrism requirements, not optional cleanup. Every design,
implementation, review, and validation change MUST minimize CPU time, memory, allocations, copies, synchronization,
GPU submissions, wake-ups, and loaded resources while preserving correctness.

When several correct designs exist, use the one with the lowest steady-state resource cost unless a measured result or
a materially simpler and safer implementation justifies another choice. An intentional regression MUST be measured in
the affected configuration and justified in the owning contract or an active WIP plan.

"Zero CPU while idle" means RedPrism owns no polling loop or periodic wake-up when no frame, input, I/O completion, or
state change is pending. Operating-system scheduling noise is outside this contract.

## Mandatory runtime rules (C1 host)

- Idle, hidden, minimized, suspended, display-off, and occluded execution MUST block on events. Busy polling is
  prohibited. The shell MUST NOT arm a periodic timer to keep the window alive.
- Steady-state Present of an already-created swap chain MUST NOT heap-allocate. C1's clear-color frame uses the
  existing device, context, RTV, and waitable handle only.
- `Renderer::Render` MUST NOT run from `WM_PAINT`.
- Hidden, minimized, or zero-size client: no `Present`, no render-target create. The swap chain MAY remain allocated
  at the last non-zero size until a later resize; a zero-size `WM_SIZE` suspends Present and MUST NOT call
  `ResizeBuffers` to 0×0 as a way to keep presenting. Hidden documents MUST drop display canvas GPU surfaces
  (`CanvasRenderer::CanvasSurfaceBytes() == 0`).
  Accepted utility jobs may retain their separate bounded off-screen targets and shared raster residents until
  completion; they never call Present. The last utility owner releases those targets. No utility allocation
  is kept merely to keep a hidden window active.
- After `Present` reports `DXGI_STATUS_OCCLUDED`, stop content presents, wait for the DXGI factory occlusion-status
  window message, and recover with `DXGI_PRESENT_TEST` only. Occlusion polling is prohibited.
- The swap chain MUST be created with `DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT` and maximum frame latency 1.
  `ResizeBuffers` keeps that flag. Before building a frame the UI thread waits on the waitable together with the
  message queue, job event and settings watch so `Present` does not block the UI thread. Settled idle waits on
  messages, the job event and settings watch.
- Adapter of output: create the hardware device on the adapter whose output owns the window's monitor, never the
  default adapter while a matching output exists. Presentation MUST NOT cross adapters through DWM. Forced WARP never
  re-checks. Selection is a pure function (`SelectAdapterRecordForMonitor`) so HostTests can prove it without a
  topology.
- The top-level window is `WS_EX_NOREDIRECTIONBITMAP`. Nothing paints it with GDI, so DWM keeps no client-sized
  redirection bitmap for chrome. `gdi32` may be linked for Win32; GDI text and GDI fill of the swap-chain window are
  prohibited.
- Derived display state (DPI, client size, adapter LUID) is recomputed when its inputs change, not per idle wake.

Diagnostic exception: the bounded `--bench ui-smoke` correctness harness may render with session display power
off, as the screenshot harness does. It keeps visibility, suspension and occlusion checks, and its timings are
never qualification evidence. Full UI benchmark modes fail if presentation is unavailable. See
[`Testing_Benchmarks.md`](../Testing/Testing_Benchmarks.md).

## Pointer input and the chrome

DxUi's embedded host repaints its whole surface after any invalidation, and republishing its UI Automation snapshot
(`EmbeddedHost::UpdateAccessibility`) walks the whole control tree. A pointer move therefore pushes to the chrome only
what it changed (review fixes RF-1001, RF-1002):

- A pan or a zoom (the Hand, Space, the wheel, the Zoom tool, the zoom commands) changes only the view:
  `Application::SyncView` updates the status bar's zoom text and what the canvas overlay draws (the transform box and
  guides, the selection outline and lasso draft, the crop frame, the brush tip, the tool marks). No header, panel,
  card, menu, title or layer row is touched, and the frame prepares what changed.
- Everything else goes through `Application::SyncChrome`, which pushes every region; each `ChromeHost` region (the
  title, the layer rows, the dynamic menu rows, the status texts, each tool header, the adjust panel, the size card,
  the overlay) writes its controls only when its state differs from what it last wrote, or when one of its controls
  changed itself since (a value the session did not take goes back to the control). SyncChrome lays out first
  (`ChromeHost::Layout`, for the canvas hole the overlays follow) and prepares once at the end, so a sync that changes
  nothing writes nothing and costs no preparation, and the frame after a sync finds the chrome prepared.
- UI Automation: after a preparation, `ChromeHost::Prepare` republishes the snapshot only while a client listens
  (`UiaClientsAreListening`), when the accessible structure changed (a tool switch, a card or panel opening or
  closing, a header control shown or hidden, the layers panel's rows, the document title) or when a client asked for
  the tree while it could not be published (during a press or a dispatch). Otherwise the snapshot waits, and it is
  published before a client reads it: `WM_GETOBJECT` (a client connecting later) and the in-process root. Pointer
  moves change nothing structural, so without a listening client they publish nothing.
- The canvas cursor (`WM_SETCURSOR` comes with every pointer message) decides what depends on the pixel under the
  pointer once per pointer position and session state (every session change passes through SyncChrome): the Move
  tool's press rule is built once per change, the layer scan runs only when Ctrl or auto-select may pick another layer,
  and a pointer outside the selection's bounds skips the winding test.

The WARP self-test proves these (`Application::RunChromeSyncSelfTest`): a Hand pan writes and prepares nothing; a
Space pan, a wheel step and a brush move write only the overlay (and the zoom text); a transform drag rebuilds no layer
row and no menu; a settled sync writes nothing; none of them publishes without a listening client and each preparation
publishes once with one; `WM_GETOBJECT` and the in-process root publish a waiting snapshot; a card opening publishes
without a client; the cursor scans the layers only when a rule needs it, once per position, and tests the selection
once per position and never outside its bounds. Its `CHROME SYNC` and `CURSOR` lines print the counts and the time
per input.

## Budgets that later phases inherit

Shape release rasterization, complete layer insertion and prepared history construction run on workers after
whole admission. The owner publishes checked handles, retains pending outline metadata and dispatches view
input through the original acknowledged record. Cancellation cannot release copied source/history or reservation
before acknowledgement; complete replaced CPU owners retire together off owner. Metadata capture and final
chrome/display work remain measured P1.9 obligations; worker elapsed time alone does not establish UI latency.

Mask creation from a settled session also runs as an admitted ordered Commit job. Whole reservation precedes
source/history capture; worker preparation includes inverse-grid selection coverage, the Gray8 output, an eager
semantic thumbnail whose longest side is at most 96 pixels, and prepared mask/selection/history publication.
Cancellation retains the reservation and complete
source/history graph through acknowledgement, then the aggregate retires off owner. Settled producers are covered;
live brush, warp, selection and transform settlement remains P1.4/A6/S3 work. Functional qualification establishes
no G1 timing bound; capture, history publication, chrome/display work and required measurements remain open.

Layer via Copy also admits its whole scheduler-memory reservation before owner capture of document, active/mask
target, displayed placement, selection and history. The reservation covers detached pixel, after-document and
prepared-history work; admission applies the layer limit and independent 100 M aggregate image- and mask-pixel
budgets. The held-document retirement estimate is included in the deferred-release charge. Stale or cancelled work
publishes no layer/history and retains its record, reservation and source graph through acknowledgement; its
complete retired CPU aggregate releases off owner. Source/history metadata capture remains H2, and live brush,
warp, selection-outline/move and transform settlement remains P1.4/A6/S3. The private functional controls do not
establish G1 timing; capture, apply, history, chrome/display and full-loop measurements remain open.

These numbers are binding as soon as the feature exists. C1 does not implement them; it MUST NOT choose a host shape
that makes them impossible.

| Budget | Limit |
| --- | --- |
| History retained bytes (C4, D13) | Application: ≤ min(physical RAM / 4, 2 GiB), 100 entries, shared immutable rasters and selection storage; unsigned 64-bit limit. Class default/query-failure fallback: 256 MiB. Selection retained capacities are cached. Conservative raster retirement bounds and prepared publication qualify for basic raster/gradient/pixel move; other H1 callers and H2/H3 exact accounting/constant-time capture remain open |
| Document pixels | 30 000 px/side; 100 M image pixels + 100 M mask pixels; ≤ 10 000 layers |
| Manifest / asset | 4 MiB manifest; 512 MiB per asset |
| File encode buffers (P1.7) | Direct bounded temporary-file streams for PNG/JPEG; ZIP sources stream durable staged PNG files. No retention of all encoded asset vectors through archive close. Manifest/source-path/codec allocations remain in whole-graph admission/headroom; serial staging disk cost is measured separately |
| Visible DxUi surfaces (C2) | Library cap (64 MiB visible, 0 when hidden) |
| Hidden document GPU (C6/P1.3) | 0 display canvas surface bytes; no display Prepare/Composite/Present. Accepted utility work uses separate bounded targets and releases them at its last owner |
| Composition (C6) | Only when the document changed or surfaces were rebuilt; a settled document composites 0 times per frame (`--self-test` proves it) |
| GPU raster residency (C6/P1.3) | Bounded to the union of settled display rasters and the latest accepted utility tile's residents; entries keep their `Raster` alive so identities never alias |
| Compose/Present after warmup (C6) | No heap on the blit/Present path; composition may allocate transient CPU bookkeeping only when the document changed |
| Dirty DxUi prep (C2) | Stay within DxUi Release ≤ 64 allocs/frame |
| Brush (C10) | Tile 256²; mouse-up MUST NOT materialize a contiguous bitmap; changed tiles only per pointer update; history retains only the tiles a stroke replaced; measured per run under `Specs/TestRuns/` ([`Rendering_Brush.md`](../Rendering/Rendering_Brush.md)) |

Fail-closed performance sandboxes live at `X:\RedPrism.Perf` (any local drive letter plus an ownership marker), never
under repository `.build/`. C1 does not yet create that tree.

## Worker execution cost and lifetime

`Application` owns a bounded `JobScheduler`: at most 128 records across queued, running and undrained states, and
eight pending UI actions. The seven job classes share three OS priorities. The serial classes (`Stroke`,
`Selection`, `Commit`, `Io`) preserve FIFO within each class and key through apply or drop. Each record has two
aligned intrusive completion nodes; producers push to an SLIST and signal an auto-reset event. The owner drains
FIFO batches. Progress is monotonic and publication is limited to once per 100 ms. These fixed bounds prevent
unbounded scheduler queue growth; capacity exhaustion fails submission locally.

The owner router serves one ready leaf per pump, with at most eight leaves. Application's retry source adds
eight startup event/wait registrations to the existing shared 32-slot wait pool (GPU uses eight and memory
pressure uses two). Clipboard Inspect/Copy retries retain their admitted record, reservation and receiver while
waiting; Paste shares a limit of six total Inspect plus owned-Copy acquisitions. A busy transfer MUST release its
compute callback while retaining that state. Relative deadlines and cancellation share one event wait; no extra
pool, periodic timer, sleep, callback allocation or hot-path join is permitted. Slot reuse waits for published
acknowledgement. Idle registrations neither claim readiness nor create wake-ups. These structural bounds do not
establish a maximum OS clipboard delay or G1 latency.

Clipboard host work captures the selected ordinal and complete admission before worker allocations or native
clipboard calls. `GetClipboardData` and other OS transfer calls run on workers. Copy/Cut publish their private
document result before encoding and mirroring; a later encoder or OS publication failure therefore preserves
that private result. Copy Merged retains its opening document authority through utility readback, releases utility
targets and mapped owners, then performs CPU pixel copying, private publication, encoding and transfer from the
detached pixels. Receipt encoding is fixed 40-byte metadata. Payload SHA-256 runs only on workers, with a checked
4096-byte stack object and cancellation between 64 KiB bands. Hash full initialized exposed global blocks,
including zeroed padding. Neither receipt parsing nor digest comparison allocates a payload on the owner.

`ClipboardEncodingBudget` is a checked, dimension-based phased peak estimate, not a measurement. It plans PNG
encode and PNG global-copy/hash peaks, then the later DIB global-copy/hash peak, and charges the larger PNG/DIB
stage rather than summing sequential stages. The estimate includes capped memory sink and returned-vector
overlap, allocation/page-rounding slack, the conversion band, PNG/DIB/receipt globals, hash workspace and fixed
metadata. It rejects invalid dimensions and canvases above 100 million pixels. Its 16 MiB WIC workspace allowance
is not a proven hard bound on opaque codec memory. Complete host admission also covers incoming clipboard bytes,
decode, document/history preparation, Copy Merged readback/CPU owners where applicable, and aggregate retirement;
an adapter's partial byte report cannot authorize the graph, and a post-encoding block cap does not constrain
earlier memory-stream growth. Digest throughput, opaque codec/native-call interruption, process headroom and full
owner/first-upload costs remain native F5d/G1 qualification.

Basic raster-edit admission declares metadata/token/result names, private tile bases and result images,
tile/index/patch vector growth, sparse-window split scratch, selection coverage and its antialias/crossing
storage, stretched/carried masks and required semantic mask thumbnails. Optional image thumbnails are not part of
the operation's required output or reservation. A geometry-only planner allocates no pixel buffers; published
inputs stay shared. Target capture occurs under the complete reservation, followed by same-record resume.
Aggregate input/result retirement uses worker-computed reachable capacity, including shared sources; this is a
conservative releasing charge, not a measurement of newly allocated or exclusively owned bytes. Its temporary
overcount can delay admission until release acknowledgement and MUST be reported separately from preparation
elapsed time. History/chrome/upload and admission/apply scopes still require G1 measurements.
The raster-edit charge also includes complete document/history snapshot metadata, entry-vector growth and trim
set bookkeeping via `DocumentHistory::EditMemoryBytes`. This preflight allocates no storage, counts graph
occurrences conservatively and refuses overflow/depth beyond 128. Existing shared pixels are not charged as new
allocations. H1 adds a conservative whole-graph retirement bound and prepared history publication for basic raster
edits, gradients and pixel moves; its scoped native qualification is recorded in
[PreparedHistoryPublication_2026-10-01.md](../TestRuns/PreparedHistoryPublication_2026-10-01.md). Other production history callers and
production Undo/Redo have not migrated to prepared publication. Exact retained-byte accounting and constant-time
history capture remain H2/H3 work. Unmigrated synchronous Begin/End callers retain their existing owner-side
allocation behavior.

### Prepared history and aggregate retirement (P1.9 H1 foundation)

Each raster storage record caches a construction-time upper bound for its complete held graph: its aligned pixel
capacity, storage/control allowance and tile-vector capacity, plus cached bounds for its base and tile rasters. A
sparse record includes space for a possible lazy materialization cache. Child bounds are summed by occurrence, so
shared storage is deliberately overcounted without allocating a deduplication set or materializing pixels.
Checked arithmetic, a zero/unavailable child bound, overflow, or graph depth beyond 128 refuses the estimate.
Materializing a sparse raster does not increase this cached bound.

Before capturing a commit input, the basic raster-edit, gradient and pixel-move job callers conservatively estimate
retirement for the current document, pending snapshot, past/future history and (where applicable) the last brush
commit raster. The release bound adds twice the declared preparation/history/job peak to that held graph bound
for private output and retired-preview ownership; existing shared pixels are not charged as new allocations
in the operation's memory reservation. The estimate itself allocates no accounting set. Each commit payload is
offered to worker deferred release with a charge of at least 16 MiB, including failures before later worker-side
accounting. Charges are conservative release scheduling bounds, not measured exclusive bytes or performance
results. Preview graphs below the deferred-release threshold may still retire inline under the release policy.

Worker preparation builds the complete candidate history state, including snapshots, entries, stack growth and
trim bookkeeping, before publication. The candidate is tied to the exact history owner, structural epoch and
revision; owner publication revalidates all three and swaps the prepared state without allocating. Refusal leaves
the candidate owned by the caller for aggregate retirement. Basic raster edits, gradient commits, pixel moves,
destructive color, size cards, production Undo/Redo, merge and import/replacement use this path. Remaining history callers are open. H2/H3 must
replace occurrence overcount with exact accounting and make history capture constant-time. The foundation's
configuration evidence does not qualify complete H1, P1 or G1; remaining caller migration and owner
capture/history/chrome/upload measurements remain outstanding.

Destructive color/filter commits use the same prepared boundary. Their nonallocating admission
query borrows opening settings and adds history preparation before owning capture. A suspended/resumed admitted
record owns complete source/history and color input/result; its conservative release bound also includes current
panel raster/selection/lookup capacities, spare settings capacity and replaced persistent settings. Owner apply
swaps the retired panel and settings into that aggregate. The full native configuration matrix passes
([evidence](../TestRuns/ColorHistoryPublication_2026-10-01.md)); adjustment-layer cancellation
routing and the other H1 callers remain open.

Adjustment opening reserves complete readback, history/panel construction, original/sampling records and render-order
storage before owner capture. Worker visibility preprocessing retains every source record; no hidden-subset copy or
history Begin is built in the command callback. GPU tiles remain owner work. Final map acknowledgement and utility
target release precede detached worker panel/Begin preparation, so a later device change cannot discard copied pixels.
Metadata OK reserves history and complete original/settings/source/retirement graphs before capture. The worker
constructs the final document and End; apply validates exact authority/settings/history epoch before nonthrowing
ownership swaps. Every closed End expires whole-document mutation tokens. Failure preserves live document/panel,
pending depth and redo handles. Owner source copy and conservative traversal remain H2/H3 costs; this caller does not
qualify first upload or G1. See the
[opening/OK record](../TestRuns/AdjustmentOpeningAndOkHistoryPublication_2026-10-01.md).

Panel Cancel owns a bounded opening-specific request and retains the earlier selected graph until canceled drop
acknowledges. Same-opening repeated requests coalesce before queue retirement. Adjustment dismissal reserves the
complete metadata End/restoration graph; destructive dismissal admits a fixed descriptor/history view and constructs
no document or history snapshot. Both reserve complete held-panel/display/history retirement before capture and
transfer old CPU owners through the worker-release aggregate. Failure cannot release/close the live panel or lose
its original HRESULT. Local dependent actions use a selected prerequisite, avoiding queue-behind-self waits; atomic
file replacement keeps its existing ownership boundary. Functional qualification is tracked in the
[cancellation record](../TestRuns/PanelCancellationHistoryPublication_2026-10-01.md). Admission traversal, full record
capture, other pending-edit/reset callers, first upload and measured G1 remain open.

Size-card admission additionally includes complete history construction and trim scratch. Owner capture of its
source/token/history happens under that reservation; the worker constructs the bound resized document/history.
Publication swaps replaced document, lasso and selection-move owners into the release aggregate. Its retirement
bound includes retained draft capacities before capture. Token, active-layer and history-epoch validation precede
publication. The full native configuration matrix passes ([evidence](../TestRuns/SizeHistoryPublication_2026-10-01.md)). This removes history construction from size apply;
value metadata capture, exact incremental accounting, chrome and uploads remain unqualified costs.

Gradient events retain primitive endpoints/colors/settings in a one-active/latest-pending mailbox. Pixel/selection
capture follows full admission in the same suspended/resumed record; no published stroke is borrowed by a worker.
The complete private stroke/source/tile/coverage graph has one deferred release owner. Cancellation invalidates
publication immediately but keeps the ticket until acknowledgement. After completion, the registered host source
starts the next preview only after the previous reservation retires; idle has no readiness or retry loop. Commit
may reuse one acknowledged immutable pixel-move opening for the exact document serial, gesture opening and
generation. Worker validation additionally checks retained source, grid/placement and selection identity/AA.
The opening contains coverage and lifted pixels only; each preview/commit builds fresh mutable stroke state.
Cancel/replacement/reopen/source changes clear the cache through its complete deferred-release node. Admission
still charges the full preparation peak even on reuse; cached ownership bounds are constant-time conservative
retirement declarations, not a reduced allocation peak or measured latency claim. The selected commit
retains its input ordinal, acquires fresh history ownership after color settlement, and freezes its exact
metadata sequence. Matching uploaded previews are adopted only for the same source generation/target/settings;
stale or unshown results use ordinary sparse upload. First upload, owner capture/history/chrome and drag/settle
latency remain measured P1.9/G1 requirements, while CPU vectorization/GPU shading remain P2/P3.

Suspended worker stages count against the same 128-record capacity and retain their original reservation and
FIFO key through final apply/drop. Resume consumes no second reservation, container entry or job allocation.
Successful stage deferral cannot race worker re-entry into its UI hook. Cancellation preserves external mapped
owners until acknowledgement and skips further Run work. One ready-only owner pump participates in both shared
outer loops; in-flight work alone MUST NOT make `HasUiWork` true or cause polling. Its event callbacks share the
existing 32-registration wait pool with memory pressure, rather than creating another pool/thread.

One registered host UI-action source has a borrowed stable lifetime and an allocation-free coalesced readiness
flag, independent of the eight ordinary action slots. Registration alone is idle. A ready source prevents idle
waiting, wakes the same scheduler event and shares the one-action-per-pass bound with queued actions, alternating
fairly when both kinds are ready. Consumption precedes callback dispatch. Waiting continuations rearm only on
their prerequisite's completion. Source requests do not add a worker, timer, container allocation or polling loop.

The host pumps messages, drains completions and runs UI actions through shared outer-loop primitives. Completion
application stays outside nested window/control dispatch, and no more than one UI action runs per outer-loop pass.
Reentrant calls are ignored. The scheduler event is included in both the message wait and frame-latency wait, so
hidden or suspended windows still process completed work without polling or a periodic timer. `SettleJobs` exists
for harness/headless use only. Shutdown cancels and hides before joining workers, then drops undelivered callbacks
before chrome, device, satellite and COM teardown. The job runner catches only `std::bad_alloc` and translates it
to a failed operation with `E_OUTOFMEMORY`; explicit `noexcept` construction/admission boundaries may return that
error, while other allocation failures remain fatal.

`PreviewMailbox` retains one active interactive request and one newest pending request. A new tick replaces only
the pending input and invalidates a refinement; it never repeatedly cancels the interactive worker. The command
queue is a fixed eight-entry ring. Both structures allocate no container storage. `HistoryWork` and `StrokeReplay`
hold only ordering/state metadata; P1 owns retained stroke samples, and P0.7 accounts for their bytes.

Continuation retirement uses caller-supplied, non-throwing callbacks with no retained callable, allocation,
additional queue storage or worker. At most eight entries retire per TakeReady/Clear call. Queue mutation precedes
the callback; detached ownership survives until callback return or transfer. Callbacks only publish/transfer owned
state and MUST NOT pump, wait, dispatch UI or access the same queue. Receipt construction and request byte budgets
belong to the eventual host admission path. These primitives alone do not activate production continuations.

Each owned continuation creates one shared result allocation at request construction, outside hot dispatch.
Receipt reads/copies, terminal publication, publisher moves and abandonment allocate no storage, acquire no
lock, start no worker and signal no event. A unique publisher retires a pending result as `Cancelled` on
destruction or move-overwrite. Externally retained observers own only that result, never input paths or pixels.
The owning construction/admission boundary MUST fail the whole request on `std::bad_alloc`, returning the
allocation-free immediate `ContinuationReceipt::Failure(E_OUTOFMEMORY)` value. Full/invalid queue admission
retains the input for explicit failed publication. Production admission bounds and accounts for
path/name bytes; a fixed eight-entry ring alone does not bound variable-length payloads.

`HostInputBudget` now bounds the combined declared request storage of queued and active host inputs to 1 MiB.
An owned batch contains at most 256 paths, each at most 32,767 UTF-16 code units on Windows. Capacity-based
accounting includes vector slots, string terminators, conservative per-container overhead and fixed request/result
allowance; small logical contents cannot hide a large retained capacity. Checked admission inspects at most 256
paths and allocates no storage. This is a declared storage charge, with ordinary global allocator headroom; it is
not a bound on operating-system page commitment or caller-owned input created before admission.

`HostRequest::TryCreate` reserves that charge before creating its shared result, moves the payload only on
success, and returns the ticket on construction failure. A refused request preserves its caller-owned input and
must return an immediate failure. Tickets follow queue-to-active and move transfers; retirement publishes before
input destruction, then returns the charge. Receipt observers retain neither input bytes nor budget tickets.
Admitted payloads expose only const access, so their charged capacities cannot grow after admission. The budget
outlives all request owners. Typed Application producers use this path; bare model construction is confined
to tests.

Borrowed-input builders now accept non-owning path/name views and fixed event parameters. Preflight checks the
256-path count, 32,767-unit per-path length and aggregate declared copy peak without constructing an owned
container, string or result. A terminated Win32 path is scanned only through its maximum permitted terminator
position before becoming a view. Names are bounded by the aggregate byte charge here; the document's existing
trim and UTF-8 name validity still apply when executing a rename.

The owner reserves the complete declared copy peak before constructing the vector/strings or result. Exact
vector reservation and direct string construction avoid growth/reallocation. The v145 string-rounding allowance
is 32 code units per string, including inline storage and terminator; retained capacities are checked against the
reservation, and unused allowance is returned before publishing the request. A partial input or receipt allocation
failure destroys copied storage before returning its ticket. On quota/length/count refusal, no owned copy occurs.
Global allocator headroom remains separate from the declared charge.

Later selected output paths use `HostRequest::TrySelectOutput` under this same combined budget, only on a pending
budgeted owner. Path selection reserves before copying; replacement reserves the simultaneous old/new peak and
preserves the old output on refusal or allocation failure. The request owns the selected path and its reservation,
so queue/active moves and retirement keep publication before input/output destruction and charge return. No new
receipt, worker, event, timer or polling belongs to selection. Fixed request state fits within its charged allowance,
as enforced by a compile-time size check. Future variable-size staged state requires its own admission.
Production shell/dialog/drop/rename producers use these preflight builders at their first host-owned copy.
Drop sources query the whole batch's lengths and reserve before filling admitted buffers while the HDROP lives.
Shell-owned strings remain alive through bounded preflight and copy; any enumeration/read failure refuses the
whole request. Rename passes a borrowed field view through an immediate callback before the field closes.

The Application source admits typed panel, file, close, size and command inputs: at most eight pending entries plus
one active request, with one small shared result per observed input. An empty registration costs no wakeup or
worker. A waiting active commit and queued inputs behind earlier history consume readiness without rearming;
the common history completion hook signals their next UI action after apply/drop. The current color finisher uses
it; future history migrations MUST also notify it after barrier release. Cancellation keeps the active input
until worker acknowledgement; shutdown unregisters before join. Fixed stage state reuses the active request and
cannot grow outside its charged allowance. Path selection precedes pixel settlement/readback. The selected
request advances only after successful acknowledgement; synchronous file/GPU work remains for P1.3/P1.7.

`JobActivity` uses 128 fixed records and updates only on scheduler notices. The oldest foreground operation owns
the global progress indicator; Background, Refinement and InteractivePreview do not. A single deadline timer shows
the indicator after 250 ms, is killed before dispatch, and is removed at completion/shutdown. It never polls job
state. Hidden progress values do not repaint chrome. Visible indeterminate progress uses the existing frame-paced
DxUi animation; hidden, suspended and occluded frame scheduling still waits on events.

This foundation does not mean existing image operations run asynchronously. P1 owns migration of blocking
operations away from the UI thread, operation-specific peak estimates and retained input replay storage.

### Admission, release and recycled buffers

Every nonzero `JobOptions::memoryBytes` request covers the complete graph's additional allocation peak. At each
admission the owner samples `GlobalMemoryStatusEx`; the available ceiling is the smaller of physical and commit
availability, less 1 GiB headroom and outstanding reservations, saturating at zero. Inputs already resident are
not charged again. Children borrow `JobContext::reservedMemoryBytes`; no stage partially reserves and then waits
for more. Migrated image jobs MUST declare their rounded allocations, scratch, staging and result peak.

A waiter owns a bounded scheduler record but occupies no worker. Reservations survive completion apply/drop and
payload destruction. Submit, retirement, deferred-release completion and memory-pressure events retry admission;
no timer polls availability. An insufficient request waits only while a reservation, release or cache trim can
return memory; otherwise it completes `Failed(E_OUTOFMEMORY)` without running. Snapshot failure also fails the
operation. A cancelled waiter never runs. Same-key successors reserve only after predecessor retirement.

Image Size and Canvas Size jobs declare rounded aggregate output and required semantic mask-thumbnail
allocations, one maximum private sparse-source assembly, and conservative metadata for snapshot/result/validation/history.
Optional image thumbnails are not required outputs and are excluded from these reservations. Immutable source pixels
and selection geometry remain shared. A late bounds refusal still reserves the prefix outputs already allocated
before that refusal. Source assembly, fill, sampling and required mask-thumbnail work occur on the worker;
owner validation/history costs remain measured separately and are subject to the P1.9/G1 gates.

The owner installs a thread-local deferred-release sink. Last-reference storage or directly owned pixel buffers
of at least 16 MiB transfer an intrusive node to a non-cancellable NORMAL worker lane. This path allocates nothing,
needs no job slot/reservation, and cannot be refused by a full job queue. Smaller or non-owner-thread frees run
inline. Queued and executing releases count toward outstanding work. Close clears the sink, joins event sources
and workers, then drains leftovers while hidden, before destroying events or the receiving owner.

Measured fresh-page overhead justifies a process recycle pool for buffers of at least 16 MiB. It has at most 32
slots, uses 16 MiB size classes and initially retains at most one eighth of the sampled admission ceiling, capped
at 512 MiB. `PixelBuffer::RequiredBytes` includes rounded capacity and its 64-byte ownership record. A full cache
returns the buffer to the CRT. Low-memory notification or failed admission disables cache returns and schedules
worker trimming before refusal; a fresh OS sample follows completion. Cached bytes are never added twice to OS
availability. Low/high Windows notifications alternate one-shot waits, with no repeated wait on a signaled level.
Encoded-image, GPU and RAW cache pressure handlers land with the corresponding cache implementations.

`RedPrism.Pipeline` TraceLogging emits admission transitions and `UiFree{bytes}` for actual owner-thread frees of
at least 1 MiB. The P0.7 trace gate has positive controls and real direct-buffer/adopted/vector-raster releases;
normal deferred operation MUST have no owner-thread free of 16 MiB or more. Hidden shutdown may free inline.

The executor foundations in `Core_Architecture.md` use a private fixed-size pool and a separate one-thread wait
pool. Idle callbacks own no polling loop. Dispatch wakeups are coalesced at the thread count per priority;
event registrations are capped at 32 and fail locally at capacity. Parallel tiles use dynamic disjoint claims,
one optional Windows work object per call and a participating caller, without another C++ heap allocation for
the call state. A failed helper allocation runs on the caller. The UI must never enter this synchronous join.
Pool close retains active inputs, joins callbacks and helpers, then releases its implicit MTA cookie. Background
callbacks request EcoQoS; foreground callbacks request HighQoS, restoring system-managed policy on return.

Output allocation, exact image operation order and the measured cancellation interval belong to each migrated
kernel. The executor checks between tiles; bodies check between rows or smaller work units so their measured
interval is at most 1 ms. The coordinate-kernel benchmark qualifies executor overhead only. `--bench-executor`
on HostTests records its checked output and cancel-check cost; it is not proof of UI latency or image speed.

## UI duration instrumentation

`ScopedUiTiming` observes inclusive elapsed wall time on the calling thread. The production hooks cover each
outer message dispatch, each `Application::WindowProcedure`, completion drain, final result apply (including
payload retirement), deferred UI action, and host frame construction after the frame-latency wait. Frame timing
ends after Present/device-recovery handling; it does not measure GPU execution or input-to-display latency.
Nested messages and modal waits are included, so callers MUST identify modal/device-recovery scenarios before
using these samples for a latency gate. Dispatch and WndProc durations overlap and MUST NOT be added together.

With no capture or enabled ETW timing keyword, a scope reads no clock and allocates nothing. A scoped, optional
thread-local `UiTimingCapture` accumulates fixed histograms with no synchronization or allocation. Samples round
up from QPC ticks to microseconds, preserving a zero interval. Histogram percentiles are bucket upper bounds,
not exact quantiles; maxima are exact observed microseconds. Invalid clocks count failures; totals saturate
with an explicit overflow flag. Capture lifetime encloses every measured scope, and neither may move threads.

Provider `RedPrism.Pipeline` uses keyword `0x4` at Verbose level for `UiDispatch{msg,durationUs}`,
`UiMessage{msg,durationUs}`, `UiDrain{durationUs}`, `UiApply{jobClass,durationUs}`, `UiAction{durationUs}` and
`UiFrame{durationUs}`. The first three emit only above 4,000 microseconds; the others emit each operation.
Only the provider's initialization thread emits these events. `Build/RedPrism.wprp` enables those events plus
free/memory events, CPU samples, scheduler stacks, file/disk I/O, faults and graphics providers. Its collector
buffers total 24 MiB. Trace retention/collection is explicit and uses the owned performance root; the ordinary
test suite only validates the profile with read-only WPR metadata queries and uses a private in-process ETW
session for event proof. No polling, timer, worker, or new wakeup belongs to the timing facility.

`HostTests --bench-ui-timing` measures empty scope overhead with the provider registered, tracing off, and the
histogram capture off/on. It asserts counts, never elapsed-time limits. P0.1 scenario measurements and G1 remain
separate from this instrumentation proof.

## Image result ownership

Published selection outlines share immutable point storage across document snapshots, history, previews and
result tokens. Copying such a snapshot MUST NOT copy its selection points. A geometry edit publishes new storage;
layer-result validation compares retained selection handles and antialiasing without traversing the outline.
Document and restore serials prevent stale work from returning after replacement or Undo/Redo. These primitives
are specified in [`Document_Model.md`](../Document/Document_Model.md); migrating existing image operations to
asynchronous execution remains active work.

Image operations that create a full result use the 64-byte-aligned `PixelBuffer` and transfer it to
`Raster::Adopt`, without a second pixel allocation or copy. This covers adjustment/filter wrappers, Gaussian
blur, placed image/mask resampling, canvas extension, WIC frame decode and the production composition readback.
`Raster::Make` remains the copying/zero-filled constructor for callers that require those semantics. Kernels
initialize every output byte before publication. Partial renderers explicitly fill their untouched background.
`ReadRegion` skips its fill pass only when a published contiguous image covers the entire requested region;
row padding remains untouched.

`--bench kernels` (3840×2160) and `--bench kernels45` (8192×5464) report individual kernel and whole-output
wrapper times with a warmup, medians, p95, maxima and stable output checksums. Setup and checksum calculation are
outside each timed region; the wrapper rows include allocation and result publication. These are CPU baselines,
not UI latency, GPU, RAW or Photoshop comparisons. The bounded `--bench kernels-smoke` verifies the harness
without timing assertions. Phase qualification records the command, configuration, machine, power plan and
measurement limitations under `Specs/TestRuns/`.

When separate runs show drift in unchanged controls, `--bench buffers` and `--bench buffers45` compare copy and
adopt strategies in alternating pairs within one process. The reference uses the old vector/result-copy strategy
with the current `ReadRegion` fast path; it is a conservative ownership comparison, not a reconstruction of an
older binary. Each pair must produce identical pixels. Reports distinguish a measured kernel-result speedup
from full host commit latency, which also includes history, upload and presentation.

## Cancellable image work

Image jobs use private `RasterWork` reads for an unassembled sparse source. They MUST NOT enter or wait for the
shared lazy materialization path. Already published contiguous pixels may be borrowed with owning raster handles.
The traversal uses at most 64 frames, including the leaf; deeper graphs fail the worker operation without publishing
an image. Ordinary raster producers use a contiguous base and contiguous tiles and need at most two frames.
Destination padding is untouched; sparse inside-fill, outside-fill and replacement source windows remain distinct.

Copy/fill chunks span at most 64 KiB between checks. Scalar adjustment loops check at row starts and every 64
pixels; Gaussian and motion sampling check every eight pixels, and other pixel loops use bounded sub-row checks.
Coverage and content-fill scratch initialize in chunks, without an image-sized vector zero-fill or result copy.
Hard selection coverage sorts its crossings with a cancellable in-place heapsort. Within one maximum canvas width
to the left, antialiased coverage retains its original accumulation order; more distant exterior pieces are
aggregated to avoid unbounded off-canvas walks and integer conversion overflow. Pixel goldens and independent
far-off-canvas area expectations qualify this boundary correction. Non-finite transformed endpoints or overflowing
edge/intersection arithmetic fail the entire coverage operation before integer conversion; an invalid edge must
never be skipped to publish a partial selection.

Cancellation checks do not interrupt an OS allocator or release. Measurements MUST distinguish check granularity
from observed stop-to-return time, which includes page faults, scheduling and scratch retirement. Kernels still
run serially in this slice; SIMD and tile parallelism remain P2 work.

Panel growth is planned without touching pixels. Worker preparation allocates the padded image and optional
downscaled source; prepared handles are cached only for the same opening image, grid and placement. Preview
admission uses preview dimensions for render scratch and includes the downscale's full-size private read in its
preparation peak. Histogram admission covers its original private source and selection coverage; it is independent
of later settings ticks. No estimate walks selection points.

`ColorEditJobMemoryBytes` takes the maximum of overlapping stage allocations, includes unprepared padding, then adds 128 KiB for bounded settings,
stack, raster headers and job records plus four copies of any mask asset name. Pixel allocations use
`PixelBuffer::RequiredBytes`; the existing global headroom covers opaque allocator bookkeeping. Selection coverage
survives filtering and blending. Gaussian additionally needs four float intermediates per pixel and double tap
weights; content fill needs four byte and three integer arrays per pixel. Private copies are charged only for
unassembled inputs, except the destructive lens kernel's explicit source copy. Eager mask thumbnails and crop
outputs are included. A reusable full-grid preview is already resident and does not reserve another image/filter
graph. The Application submits InteractivePreview and Background histogram jobs with their declared peaks before
any worker preparation. It retains one active/latest pending preview and one histogram attempt per opening, with
event-driven completion and no idle retry. Commit reservation acquisition/dispatch follows in P1.1c.3.

P1.2 adjustment previews return immutable lookup/source/coverage handles rather than a filtered CPU image.
Their memory plan reserves 1 MiB for the bounded lookup build and metadata, the overlapping downscale/padding
peak and uncached selection coverage. Compatible resident source/coverage is not charged again. LUT/cube resources
are reused per layer and counted with the transient layer-grid target in `CanvasSurfaceBytes`; hidden release
removes all of them. GPU lookup output never qualifies as a reusable full-resolution CPU commit. Settled frames
record no additional preview or composition pass. First raster upload and readback are still synchronous costs
until P1.3/P1.9; this change alone does not establish G1.
The measured 4K tradeoff is recorded in
[`GpuAdjustmentPreviews_2026-09-30.md`](../TestRuns/GpuAdjustmentPreviews_2026-09-30.md): Levels preview median
falls from 78.856 to 5.718 ms, while its commit rises from 8.661 to 83.861 ms because no reusable CPU preview raster
exists. CPU commit stays off the UI thread; exact commit optimization remains P2/P3. Residual apply/history and
other raster operations keep G1 open.

## File work foundation (P1.7 F1/F2)

Open-batch classification is one bounded Io job per selected request, with at most 256 paths and 32,767 UTF-16
units per path. Admission charges the independent path snapshot and 96 KiB worker scratch allowance; the host
request retains its separate input charge. The owner caches the fixed result and performs no classification
filesystem calls. Cancellation keeps the active request/reservation until acknowledgement, including a late
Finished result. Stop callbacks perform only event signaling; synchronous attribute/open calls and the initial
acknowledged header wait occur on a worker. The wait still occupies a compute lane and remains F6 work for
shared event-pool migration. No timing guarantee for OS/driver calls is implied.

WIC decode/encode checkpoints bound pixel bands to 64 KiB or one wider row. Shared sparse rasters are read
privately without `Data()` assembly, and output is published only when complete. Codec calls and external
flush/rename are not interruptible in flight. Export staging uses unique exclusive siblings and owned cleanup.
Host IO processing, parallel asset scheduling, complete admission and measured cancellation remain separate
P1.7 work; this foundation does not establish G1.

### Project save graph admission (P1.7 F4b.1)

Production project Save/Save As submits one Io job after settle, owning a document snapshot, active layer, resolved
path and captured history revision. `ProjectSaveMemoryBytes` declares the additional whole-graph peak before job
construction/submission: a 16 MiB fixed manifest/JSON/codec allowance; per-layer nodes, names and adjustment
payload allowance; per-asset names/source metadata; one largest-asset frame allowance; path-sized source, staging
and member names; worker object; and resolved path. This frame allowance is not repeated for every asset, and
sparse sources are not materialized. Encoded PNGs go through
bounded staging files; they are not retained as one encoded vector per asset. Invalid limits and arithmetic
overflow refuse admission without submitting work. The scheduler's one complete-graph reservation remains held
through worker completion and owner apply/drop, while immutable raster/selection storage already resident is not charged
again. Global available-memory admission still uses the physical/commit ceiling less 1 GiB headroom and existing
reservations. Image import has its separately qualified host graph; clipboard host integration has a private
full-application pass and is undergoing frozen native qualification. This save slice does not establish G1 or a
hardware timing result.

### Project-open graph admission (P1.7 F3b.3a)

The selected input owns one history ordinal through a metadata-probe job and a separately admitted decode job.
Probe memory includes bounded manifest/parser/record storage and owned path; decode adds its enforced image,
mask, layer, manifest and encoded-asset ceilings, rounded outputs and required semantic mask-thumbnail transients.
Optional image thumbnails are absent. Each reservation survives its record's owner completion and destruction.
Both charges may coexist during second submission;
FIFO Io serialization prevents decode running until probe retirement. Refusal preserves the current document
and records the first batch error. No worker incrementally reserves memory or waits for a larger reservation.
Probe handles/parser owners die on the worker; only standard small plan values cross the completion boundary.
Source reopening may observe changed bytes within caps; no file identity snapshot is implied. Opaque codec
allocations remain in global headroom. Replacement, old document/history release, chrome and upload scope are
measured separately; this graph does not by itself qualify G1.

### Image-import graph admission (P1.7 F3b.3b)

One selected input retains its ordinal through metadata inspection and separately admitted preparation, with
owned absolute counted paths and standard per-path statuses/pixel/encoded ceilings. The two charges can overlap
at submission; Io FIFO prevents preparation entry until probe retirement. Preparation reserves rounded added BGRA
outputs, one serial frame allowance, path/name/curve/document-record copies and fixed codec allowance. Successful
image import has no image-thumbnail transient to reserve. Shared immutable source raster and selection storage is
not charged again. Invalid limits/overflow
refuse construction; failed probes require no pixel output and preserve their ordered failure status.

The preparation record first enters a trivial worker stage after admission, then captures source and history metadata
on its owner hook while its full reservation is held, then resumes that same record for decode/preparation. It
does not copy a large source document before memory admission or borrow a live session on a worker. Snapshot-copy
allocation failure fails the whole graph with no partial application. The copy and final history/chrome application
remain measured owner costs and P1.9 optimization work. Opaque WIC calls retain global headroom and are not claimed
interruptible. All files retain per-path ceilings and the whole batch an aggregate added-pixel ceiling on reopened
decode, in addition to current document-wide limits. No worker waits for a larger recursive reservation.

Only the current session/document serial, generation, restore serial, active/mask/crop target, history epoch and
selected ordinal/acquisition may apply the complete validated baseline/final pair. File-operation authority permits
pending edits; successful replacement retires their complete CPU owners, while failure preserves them. Borrowed
coverage engines are detached on the owner before those drafts can be destroyed on the release worker. Ordinary
imports inside an open transaction preserve its original opening, depth and name. Undo/Redo remains settled-only.
Cancellation/staleness/worker failure applies neither and retains input/owners until acknowledgement. Ordinary
partial success preserves valid files and posts one localized import summary. A new canvas establishes a saved
baseline; subsequent imports make one named history entry. Mixed project/image batches retain the first error.
This bounded graph does not establish full P1/G1 or bounded stop-to-return inside OS/codec calls.

### Utility export graph (P1.7 F4b.2)

PNG/JPEG export owns a settled committed-document snapshot without selection outline data, resolved destination,
format, JPEG quality/resolution, document token, history-work stamp and host ordinal. Submission reserves one
complete graph: the conservative project-save snapshot/metadata/one-codec allowance, rounded full-canvas BGRA
readback, worker and path. Bounds and overflow refuse work before readback allocation; canvas output retains
the codec's 100 M pixel limit. Sparse source rasters remain shared and unmaterialized. This is a declared allowance,
not a measured exact process peak; opaque library allocations remain covered by global admission headroom.

Only owner stages compose/enqueue/acknowledge GPU tiles. Workers copy mapped rows into the reserved CPU buffer
with row cancellation checks. After the final tile's generation check and mapping acknowledgement, the owner
releases utility targets and resumes the same record/reservation in its detached CPU-only encoding stage.
No mapped-row lease or utility surface survives into encoding. Device recovery may restart bounded GPU work
before that boundary; once detached, a new device cannot invalidate copied pixels or repeat encoding/publication.
Worker file encoding uses exclusive sibling temporaries and checked flush/close/atomic replacement.

Resolved destinations reserve room within the owned 32,767-unit buffer for every writer suffix and the final
terminator: image export permits at most 32,728 units, project save 32,637 including its staged mask asset's
own codec temporary. Larger destinations return filename-range failure before Run. These lexical bounds do
not replace OS component/path validation; the worker still reports other file-system errors normally.

Cancellation retains the selected request, barrier and reservation until acknowledgement. The writer's independent
publication fact records external-file truth even if a later stop changes the receipt to Cancelled. Completion
revalidates token, lineage, stamp and ordinal; exports never change project path or saved revision. A current failure
posts one localized notice; a cancelled/stale prerequisite cannot proceed to a dependent close/replacement.
Open/import migrations, remaining fault injection and measured G1 costs remain required. Clipboard host paths and
their private fault matrix are implemented; native frozen qualification and measured G1 costs remain pending.

## CPU instruction dispatch

`Common/Kernels.h` is a standard-only boundary. Startup selects an immutable `KernelTable` before workers start;
each adjustment wrapper loads that table once and calls the selected entry, with no dispatch in the pixel loop.
The retained Scalar table currently owns Tables, Gradient Map, Grain, Noise, Lens Distortion and Color Cube.
Its extracted arithmetic and loop order MUST remain identical to the legacy reference. Scalar means the portable
reference compiled at the platform baseline; the compiler may use baseline SSE2 on x64 and NEON on ARM64.

Only Scalar is implemented in P0.8. Auto therefore selects Scalar. A forced tier MUST fail if it is unsupported or
not implemented, with no silent fallback. `SetIsaForTest` uses the same selector and may be used only with no
kernels/jobs in flight; tests restore Auto. Rejected selections preserve the prior table. No AVX-512 subset is
advertised before D16. Windows ARM64 reports baseline NEON, with no optional FP16/dot-product implementation yet.

The x64 AVX2 tier requires SSE2; CPUID.1 ECX FMA, XSAVE, OSXSAVE, AVX and F16C; CPUID.7 EBX AVX2 and BMI2; and
XCR0 bits 1 and 2. The detector MUST test OSXSAVE before executing XGETBV. Absent CPUID leaves are zero.
Tier translation units retain the default `/arch`, noinline extern `Kernels_<Tier>_<Module>_<Name>` entry points,
and the linked-image check in `Build_Toolchain.md`. New SIMD tables and measured speed qualification remain P2.

## Validation

`HostTests` asserts idle vs render vs occlusion-probe selection. `RedPrism.exe --self-test --warp` presents exactly one
hidden frame and exits. Interactive idle is qualified by running the titled window: a settled visible window MUST not
spin the CPU in `Renderer::Render`.

### Memory encoder capacity and overlap

Memory PNG/JPEG sinks enforce `EncodeMemoryCapacity` before growth, with a lower caller ceiling supported.
Charge up to that capacity plus 64 KiB page-rounding allowance for the lazy committed stream and a separate
capacity-sized final vector while both owners coexist. Bounded commit/write/read/zero/copy bands are 64 KiB.
New pages are already zero; reserve/insert final copying avoids a redundant vector zero-fill pass. File encoding
keeps its direct file stream. This bound does not include opaque WIC workspace, retained source/private document/
history graphs, conversion rows, simultaneous PNG/DIB vectors/globals, receipt/hash owners or retirement.
Complete clipboard admission must add those overlapping owners and retain codec/native headroom. The primitive
cannot close F5b.2b, F5d or G1 by itself.
