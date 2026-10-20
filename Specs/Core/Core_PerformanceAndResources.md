# RedPrism performance and resource contract

Status: current normative product contract
Last reviewed: 2026-10-18
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

Outline-move closure event authority is a bounded session-local scalar opening. An expected opening is checked
before document/history capture; reopening equal geometry cannot authorize a delayed closure. Prepared apply
retains all generation/history/target checks and also binds that opening. Admission's complete retirement bound
includes construction-cached original loops, their control allowance, and the existing document/history/display
graph bound; future host jobs additionally charge wrappers and construction peak. Reading the original-loop
cache never walks paths or materializes rasters. Existing history/metadata traversals remain H2/H3 work, and the
core APIs alone establish no host closure, frame latency or G1 qualification.

The explicit deferred selection model updates scalar translation without copying loops. Worker closure may
retain the original admitted geometry lifetime in its translated storage and weak-control allocator. The
construction-cached geometry metadata is reused when sealing that storage; no second path walk is introduced.
The last owner-side geometry release defers point destruction, and the final weak control block retains its
original charge until worker release. Model byte estimators are prerequisites only: wrappers, retained roots,
construction peak, first display and all host phases MUST be admitted before a selected route is activated.
The existing synchronous numeric methods remain references, not an admission-failure fallback.

## Responsive source acquisition

The shared startup owner acquires prepared layer rows, committed/pending display and the stroke source in that
order after the renderer, chrome, document and history subscription are ready. No asynchronous finish, sample
or Warp engine flag is published until every acquisition and the final readiness boundary succeeds. A reached
boundary that closes the owner MUST prevent every later acquisition even when its callback returns success.
Any failed prefix returns startup failure before queued paths or input dispatch; existing joined `Shutdown`
retires acquired sources and held input. Duplicate or closed-owner acquisition refuses without callbacks.

The current shared route is explicitly exercised by the actual-Run startup fixture. Ordinary activation retains
the separate complete consumer, admission, lifetime and measured-cost requirements of the P1 plan. Startup
state tests grant no first-frame, gesture, Apply or G1 timing qualification.

## Mandatory runtime rules (C1 host)

Sampler source admission reads `CanvasDocument::RetirementMemoryBytes` before retaining its immutable document.
It charges the complete source, including selection and possible sparse assembly, plus readback and bounded job
metadata. It neither captures nor walks unrelated history. Unavailable/overflowed source bounds fail locally before
capture. Layer-record measurement remains with the producer; owner admission performs only cached scalar queries.
Exact retained-history accounting and bounded first uploads remain H3/H4 obligations.

History edit/restore/retirement admission reads cached immutable stack bounds and current/pending/selected-restore
source bounds. Producers measure stack entries before publishing each private root; reader queries allocate
nothing and traverse no entries, layers, names, curves, raster DAGs or selection points. Complete ownership
includes potential sparse assembly; occurrence sharing intentionally overcounts. Unavailable/overflowed bounds
refuse admission. Extra per-root scalar storage is bounded and replaces repeated owner queries. This conservative
admission cache does not replace incremental unique retained-storage accounting or its late-residency validation.
History trim includes the retained shape-source raster as well as images, masks and thumbnails, deduplicated
against those same owners in the live document.

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

Diagnostic exception: the bounded UI smoke correctness harnesses (`ui-smoke`, `ui-prepared-smoke`, focused
sampler smoke, focused Size smoke and direct/prepared cancellation smoke) may render with session display power off, as the
screenshot harness does. It keeps visibility, suspension and occlusion checks, and its timings are
never qualification evidence. Full UI benchmark modes fail if presentation is unavailable. See
[`Testing_Benchmarks.md`](../Testing/Testing_Benchmarks.md).

Opt-in cancellation diagnostics additionally own one bounded write-once observer log and manual-reset event,
created before scheduler initialization and retained through joined Close. The event joins the original
message/frame waits; only the outer owner loop drains at most 32 immutable records. Unpublished reservation
holes wait for their original producer. A separate one-shot native timer implements the declared stop delay;
it is disarmed before posting Escape and owns a monotonic identity so stale messages cannot target a newer
trial. These opt-in resources do not create an ordinary polling timer, default observer, worker wait or
permission for early retirement. Source-Present observations require the actual committed render argument
and non-occluded Present; elapsed clocks do not substitute for CPU/GPU acknowledgement.
An early WM_TIMER wake can admit at most two fresh one-shots with monotonic identities and requested intervals
of at least 10 ms. It retains the original QPC deadline; exhaustion is a diagnostic failure. It never busy-polls or
posts Escape before the declared minimum interval, and every completed row reports its actual re-arm count.
Declared large-fixture pixel-budget refusals retain their original notice, source/history and settled preparation
owners. They admit no Commit or Escape timer and contribute no cancellation or owner-cost duration. Setup
workers may have run; refusal cost must be measured separately by the original UI operation benchmark.

Cancellation-check diagnostics borrow a Run-local binding only when the scheduler's separate
`JobExecutionObserver.cancellationObserved` callback is installed. A true `CancelToken::IsCancelled` check
reports through that binding; false checks perform no TLS lookup, clock read, callback or diagnostic allocation.
A lock-free Run-local first guard reports at most once with exact original ticket/class/lane and no raw result.
The binding is never retained in a token, draft or cached operation. Joined WorkerPool helpers borrow the
captured group binding and restore their prior binding before return. Every useful Run joins participants and
restores the scope before BodyExited, allocation-failure translation, cancellation normalization or retirement.
The group capture adds one TLS binding copy per parallel group, whose affected throughput cost MUST be
measured. Default/body-only observers retain their existing notice inventory and never enable the check callback.

The first true check is separate from original Run exit. Checks before the stop-call return remain explicitly
before-stop or stop-overlap; they cannot manufacture positive acceptance latency. Source-frame qualification
captures the successful-visible Present ordinal at accepted receipt admission. Only that exact baseline plus
one, with held original authority/raster/history, hidden card and actual committed source selection, qualifies.
A stale first frame, skipped ordinal or changed original identity permanently refuses the trial; a later restore
cannot repair it. Unsuccessful/occluded attempts do not increment the visible ordinal. Mere job settlement
cannot qualify visible presentation, and baseline zero is valid. The ordinal proves ordering, not scanout time.

## Pointer input and the chrome

### Detached Warp CPU work

The admitted Warp factory retains the immutable placed layer and uses a lazy 256-square private tile directory.
Opening MUST NOT render or copy a document-sized working image. Sparse reads use four reusable 64-square source
windows and MUST NOT materialize shared sparse storage. Serial dabs retain the existing Smudge/Liquify arithmetic.
Cancellation or failed preparation closes every snapshot path over partially changed private pixels.

Before exclusive sample transfer, the known owner uses cached geometry, path/scratch capacities and the fixed
original-source bound to declare persistent private storage and old/new construction overlap. Planning MUST NOT
walk tiles, pixels or accumulated points. Explicit geometric path reserve and declared scratch reserve precede
mutation; an outside-only sample retains the previous allocation envelope. These private CPU declarations are
not complete operation admission: the original parent additionally charges captured document/history, retained
preview generations, recorder/transport/display owners, CPU/GPU storage and final retirement before capture.

Transfer requires sole strong ownership of the mutable draft. The session retains scalar opening and sample
sequence only until its useful producer joins. Competing edits and settled-history routes refuse during transfer.
Joined apply performs only current-authority checks and handle/scalar updates; discard matches both opening and
sequence. An old callback from the same gesture MUST NOT clear a newer transfer. Original accounting and queued
graph destruction survive both final semantic ownership and weak control-block ownership, including small drafts.
These core APIs do not activate the ordinary Warp host path or establish G1 timing qualification.

Immutable Warp preview preparation retains the placed source and complete touched tile versions, with a
producer-built dense tile directory. Metadata admission adds 1 MiB plus two `RasterTile` records and one
32-bit directory entry per possible document tile. This is additional container allowance, not a substitute
for source, mutable draft, old/new tile, history, transport, GPU, display or retirement overlap. Directory
construction and tile snapshots run only on the admitted worker. Readers use constant-time directory lookup
and bounded source windows; they allocate neither a full rendered image nor a reader-side tile index.
The preallocated preview retirement node uses `AlwaysOnOwner`, even below the usual threshold. Its graph and
allocator-owned weak control retain the same original accounting owner until both actually retire.
Completed CPU transfer may use an available retained slot; callers requiring guaranteed publication preclaim
that slot before work. Missing/stale tickets and duplicate binding refuse. GPU acknowledgement, original-parent
admission, normal Warp activation and G1 remain separate requirements.

Final Warp replay MUST sample the immutable placed-original/touched-tile representation on an exclusive worker,
without constructing a document-sized sample raster. Its bounded source cache and retained preview ownership
are included in the private stroke's retirement bound. One point phase at a time reuses the existing settled/tail
coverage protocol; no all-points-by-all-segments batch is retained. The original parent separately admits CPU/GPU
coverage, complete path, source/history/display and old/new ownership before replay. WARP continues to use compute
coverage; the CPU replay reference is not permission to switch the host to a different coverage engine.
Final capture copies only immutable handles and scalars and moves the sole mutable Warp owner. It performs no
path, pixel, tile or history traversal. The ordered replay facade uses a scalar cursor and one pending coverage
phase; it supplies centers from the retained original path without copying it. Private source-window, selection,
replay output, path and captured/prepared history coexist under the original whole-operation reservation.
Conversion cannot release the original path charge early; the prepared finish retains that owner until worker
retirement, with original accounting continuing through deferred graph and weak-control deallocation.

`PlanWarpFinishMemory` declares the complete supported replay rectangle from cached grid geometry and retained
source/history bounds before exclusive capture. It additionally covers immutable snapshot containers and tiles,
selection scratch, output, carried-mask construction and old/new history overlap. The CPU/GPU split declares
retained density and uint output for every possible replay tile; the host separately adds command, pipeline and
readback transport, and charges logical GPU storage in the CPU domain on WARP. Final display and layer rows
have their own complete admission before preparation. Wrong-owner, borrowed, detached or stale targets refuse.
These conservative declarations require measured large-document admission and G1 before ordinary activation.

The experimental final Warp parent uses the existing ordered recording/readback protocol for each captured
center and final coverage. It MUST keep density across point phases, install exact uint coverage only after
the original mapped-row worker joins, and acknowledge original command/GPU users before replay or graph
retirement. Its final history, display and rows retain the original ticket and host ordinal. Successful apply
transfers the old session Warp source cache to the prepared worker retirement aggregate. Failed detached
finishing discards only its matching marker; cancellation does not establish GPU completion.

`PointerInputStream` provides fixed owner-only storage for one admitted gesture. Captured context and samples are
trivial owning values; published samples cannot borrow session or control state. Accepted path samples retain FIFO
order. Up/Lost uses a separate terminal slot, including when the move ring is full. Overflow refuses the offered
move and terminates the accepted prefix once at its last accepted sample using the tool's pointer-loss policy.
The host reports that refusal. Openings are monotonic and cannot wrap or authorize another reused stream. Taking
an empty live ring does not close it; consumed terminal ownership remains until acknowledgement. Explicit failed,
stale and shutdown Drop also requires the exact opening. This reusable foundation performs no pixel work, document
mutation, allocation or polling. Production deferred canvas callers and their G1 measurements remain separate.

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
  changed itself since (a value the session did not take goes back to the control). After the control writes,
  `SyncChrome` lays out the regions (`ChromeHost::Layout`, so overlays follow the canvas hole), finishes its remaining
  overlay/control updates, then calls `ChromeHost::PrepareInteraction`. This is an optional no-paint acknowledgement
  of the explicitly arranged live bounds, accepted only at the last successfully painted extent and DPI. It does
  not allocate or paint a surface, publish accessibility or text geometry, or change preparation statistics; when
  unchanged or unavailable it may return `S_FALSE`, and interaction remains disabled when unavailable. Initial or
  failed full preparation, extent or DPI changes, hide, and device replacement still require successful full
  `ChromeHost::Prepare` in the frame path. A sync that changes nothing writes no controls and triggers no paint.
- UI Automation: only full `ChromeHost::Prepare` republishes the snapshot as part of preparation, and only while a
  client listens (`UiaClientsAreListening`), when the accessible structure changed (a tool switch, a card or panel
  opening or closing, a header control shown or hidden, the layers panel's rows, the document title) or when a client asked for
  the tree while it could not be published (during a press or a dispatch). Otherwise the snapshot waits, and it is
  published before a client reads it: `WM_GETOBJECT` (a client connecting later) and the in-process root. The optional
  `PrepareInteraction` acknowledgement does not publish accessibility or text geometry. Pointer moves change nothing
  structural, so without a listening client they publish nothing.
- The canvas cursor (`WM_SETCURSOR` comes with every pointer message) decides what depends on the pixel under the
  pointer once per pointer position and session state (every session change passes through SyncChrome): the Move
  tool's press rule is built once per change, the layer scan runs only when Ctrl or auto-select may pick another layer,
  and a pointer outside the selection's bounds skips the winding test.

The WARP self-test proves these (`Application::RunChromeSyncSelfTest`): a Hand pan writes nothing and adds no full
preparation; a Space pan, a wheel step and a brush move write only the overlay (and the zoom text); a transform drag
rebuilds no layer row and no menu; a settled sync writes nothing; none of them publishes without a listening client
and each full preparation publishes once with one; `WM_GETOBJECT` and the in-process root publish a waiting snapshot;
a card opening publishes without a client; the cursor scans the layers only when a rule needs it, once per position,
and tests the selection once per position and never outside its bounds. Its `CHROME SYNC` and `CURSOR` lines print
the counts and the time per input.

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

### Session-end file-write allowance (P1.7)

Project-save admission checks the complete reservation addition `sizeof(ProjectSaveJob) + 4096` before scheduler
submission. Export admission does the same with `sizeof(ExportReadbackJob) + 4096`. The 4096-byte amount is a fixed
reservation allowance for the `WorkerWriteCompletion` wrapper, shared control block and native event bookkeeping;
it is not a total process-memory or operating-system allocation bound. The scheduler's existing process
commit/physical-headroom checks still apply. Admission failure accepts no file writer.

The worker completion event acknowledges only the CPU write stage. It grants no receipt, publication, saved-revision,
path, document, GPU or owner-completion authority. Confirmed session end uses one shared 1500 ms event-wait budget
for admitted queued/writing project saves and entered export writers; see [`UI_Windowing.md`](../UI/UI_Windowing.md#job-activity-and-end-session-policy).

## Worker execution cost and lifetime

Wand/mask-trace preparation uses 4096-unit cancellation spans and a separate checkpoint call so optional
observer/token call state does not remain in each scalar pixel loop. Matching, flood neighbor scans and vertex
starts retain their numeric/visit order. Empty boundary vertices can be skipped with scalar 64-bit unaligned-safe
loads within the already checked span. Sparse sources use a private cancellable copy without cache publication.
The accepted private Release cost comparison records two runs at 45 MP and a fragmented-stripe trace: matching
median overhead is 2.9–4.2%, flood ranges from 5.4% faster to 4.7% slower, and solid trace adds 0.6–1.6%. This
bounded checkpoint/cancellation cost is accepted for the S4a foundation; first per-pixel candidates with 19–39%
overhead were retained and replaced. Shared-desktop outliers remain in evidence. These scalar CPU measurements
qualify no UI deadline or host activation. Complete admission, source/history lifetime and GPU sampling migration
remain P1.3/P1.6 obligations; SIMD/GPU throughput and RAW/Photoshop qualification remain later phases.

`PlanWandLayerSelectionMemory` declares the settled active-layer New graph before owner capture. Let `N = W*H`,
`V = (W+1)*(H+1)` and `E = min(4N, MagicWand::kTraceEdgeLimit)`. Sampled BGRA and a potential sparse private
copy use rounded `PixelBuffer::RequiredBytes`; charge the private copy through later phases because its pixels
can remain recycled. Mask and vertex arrays reserve exactly `N` and `V`. A contiguous flood processes disjoint
newly selected runs; above/below scans total at most `2N`, with at most one seed push per scan step plus the first
seed. Charge the 4096-slot initial reserve and old/new growth overlap for at most `2N+1` pushes.

Each traced coordinate push consumes an edge, and each accepted loop has at least three coordinates. The
qualified MSVC v145 default allocator returns the requested capacity and geometric vector growth is 1.5;
three times total pushed coordinates and potential loop headers bounds old/new overlap. The plan also charges
Debug vector proxies, immutable selection/control roots and complete metadata/history construction. Requalify
this implementation-based capacity proof when changing the toolset; it is not a portable `std::vector` guarantee.
The eight-million-edge limit bounds accepted tracing, not canvas area or Clipper booleans.

Settled Add/Subtract plans additionally declare the isolated bounded Clipper2 graph before source capture.
Its arena is128 MiB plus the aligned `PixelBuffer` allocation record. Engine containers, raw nodes, Debug
proxies and stable-sort scratch stay in that arena. The adapter accepts at most16,000,004 input points and
8,000,000 output points with2,666,667 output loops. The declared ordinary output envelope includes every
point/header capacity, cached geometry ownership, Debug proxies and shared controls. The trace graph, arena
and two ordinary output envelopes may coexist; admission includes that phase and complete retirement, even
for a small click. This deliberately conservative bound reserves accounting, not physical allocation of
every possible output point. Insufficient physical/commit capacity refuses locally; large supported-image
acceptance and measured owner/stop costs remain P1 gates. Subtract without a current selection needs no engine.
Old New-only plans still refuse boolean admission with `UnknownClipperWorkspace`.

The maintained engine fork preserves pinned Clipper2 2.0.1 numerical conversion, winding, topology and
equal-key order. Explicit engine/conversion checkpoints count at most64 work units; allocation, OS and
destruction latency are separate. No arena allocation escapes. Its final vectors are adopted on the worker
with the output accounting owner, and worker-prepared history keeps that owner through its last readable
and weak shared control. Compatibility, every reached allocation/cancellation prefix, capacity exhaustion,
large input and actual host ownership tests are required; source provenance alone does not qualify execution.

The settled selection-nudge producer translates vertices and constructs immutable cached geometry in one pass.
Cancellation checkpoints count both loops and points, at most 1,024 explicit work steps apart, including empty
loops. Allocation/OS stop latency is separate. A zero delta reuses geometry and prepares no after-document copy.
Admission adds a second geometry-sized peak and allocator/control allowances to the existing complete edit
bound, using cached point/loop counts; copied output never retains the source's unused vector capacity.
Its reserve-capacity assumption is also qualified against the pinned v145 STL and must be requalified on a
toolset change. History capture/accounting, full graph retirement charging and measured G1 remain separate gates.

Use the maximum of source, flood/mask and trace phases, with source/recycling carry, plus capture/history storage.
Charge complete retained current/history/display graphs and new geometry again for aggregate retirement. Sharing
overcounts intentionally; overflow/unavailable estimates refuse planning. For the active-layer graph only,
blank/folder/no-active sampling owns an exact four-point result plus metadata/history. Global allocator/codec/OS
headroom and live process pressure remain separate from declared storage. The core's owner metadata capture and
memory/history traversal still need H2/H3 work; this estimate and off-owner publication establish no G1 latency or
production scheduler admission.

The settled All Layers New wand graph uses `PlanWandCompositeSelectionMemory` and never takes the transparent-source
shortcut, even when the document has no active image. Its logical sample is the full `4N`-byte BGRA composite;
`sampledPixelsBytes` uses `PixelBuffer::RequiredBytes(4N)` and is charged again in aggregate retirement. Because the
source is the detached composite, `privateSourceBytes` is zero even when the document's active-layer planner would
need a private sparse-source copy. The host adds the tiled readback allocation, immutable source snapshot, job
metadata and full source retirement before source capture. Large supported fixtures have not qualified this private
graph; conservative refusal at those supported sizes remains a gap to close before P1 is Done. The parent
supported-image and G1 A/B acceptance gates remain required; this reservation is not capacity or timing
qualification. Rendering owns the row-copy and GPU lease boundary described in `Rendering_Composition.md`.

`Application` owns a bounded `JobScheduler`: at most 128 records across queued, running and undrained states, and
eight pending UI actions. The seven job classes share three OS priorities. The serial classes (`Stroke`,
`Selection`, `Commit`, `Io`) preserve FIFO within each class and key through apply or drop. Each record has two
aligned intrusive completion nodes; producers push to an SLIST and signal an auto-reset event. The owner drains
FIFO batches. Progress is monotonic and publication is limited to once per 100 ms. These fixed bounds prevent
unbounded scheduler queue growth; capacity exhaustion fails submission locally.

The scheduler optionally enables one private GPU-recording worker at owner startup, before the first accepted
descriptor. It shares the same 128 records, priorities, FIFO through final owner apply/drop, cancellation,
original reservations and terminal hooks. It has no second scheduler, memory budget or event-wait pool.
Compute remains the default lane and its worker count is unchanged. Invalid/disabled recording lanes refuse
without consuming the offered descriptor or assigning a ticket. Enablement allocates no recorder resources
until requested; failure refuses recording or fails the caller's startup. A bounded transport capacity wait
may block only this recorder on events, while ordinary CPU jobs remain independently runnable.

Only RedPrism's private recording components may own/use a deferred D3D11 context on that lane. Common's
public scheduler boundary remains standard-only. Workers may retain an owner-captured immediate context solely
for lifetime and identity; they must not invoke it, including observational `GetType` / `GetDevice` calls.
`GpuCompletionContext::MatchesBindingForWork` compares the immutable captured owner/device/context identities
without COM calls; it establishes neither current queue authority nor GPU acknowledgement. Type/device validation
happens on the original owner before capture. No worker maps for read, executes a command list, Presents or
mutates a document. The scheduler joins recording callbacks before closing
their borrowed general kernel executor/MTA and before owner cleanup or final worker graph retirement.
The independent lane-state reference is startup refusal, cross-lane FIFO through delayed external retirement,
event-driven cancellation and complete reservation return. This foundation does not activate display/strokes.

An actually acknowledged suspended stage may resume on a different enabled execution lane using its same
ticket. Only the execution lane changes: class/key ordering, cancellation, retained slot, complete reservation
and record sequence survive. Running, stale, duplicate, invalid or disabled handoffs refuse without advancing
the suspended stage. CPU preparation, private recording and CPU retirement therefore need no child descriptor
or independent admission. Finalization still waits for every external original-context use acknowledgement.

After a CPU planning stage describes additional owners, `IncreaseReservation` may grow that exact suspended
record's complete peak before their creation. It uses a fresh physical/commit observation outside the scheduler
lock, revalidates the stage and reserves only the complete additional amount. A failed snapshot, inadequate
capacity, cancellation, shrink or invalid stage retains the original ticket/charge and creates no partial
reservation, wait or queued retry. Equal totals need no snapshot. Future stages and final retention/retirement
keep the grown total. Software GPU graphs must charge their CPU-backed texture ownership before creation;
this API alone establishes neither their complete declaration nor production display activation.

The owner router serves one ready leaf per pump, with at most eleven leaves. The dormant utility service uses
fixed original-ticket slots and a two-word active bitmap; an empty service performs no parent scan, allocation,
wait registration or periodic wake. Processing startup alone does not activate ordinary utility parents. Joined
GPU charges remain held on the owner until actual final worker cleanup returns. Application's retry source adds
eight startup event/wait registrations to the existing shared 32-slot wait pool (GPU uses eight and memory
pressure uses two). Clipboard Inspect/Copy retries retain their admitted record, reservation and receiver while
waiting within each admitted phase; Paste shares a limit of six total Inspect plus owned-Copy acquisitions.
Successful metadata inspection and the separately admitted full read keep the same selected request ordinal
and history stamp while using their own job tickets. Count successful acquisitions as well as Busy attempts;
a wait ordinal must not replace the native acquisition count. A busy transfer MUST release its
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

Native clipboard read attempts use distinct worker-owned message windows to isolate native acquisition.
Their count is bounded by active compute callbacks: a Busy or completed attempt closes its scope and destroys
its window before retry suspension or completion. No HWND persists in queued records, wait slots, UI state or
idle work. Existing fixed clipboard native/metadata headroom covers this transient wrapper; no new worker,
timer, polling loop or synchronous owner acquisition is introduced. Publication already uses an owned window.

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

The eligible host Clone All Layers readback uses the original recorded utility parent. Cached-bound admission
includes complete output, immutable source/control/scratch and job/payload ownership overlap, WARP CPU-backed
graphics and a fresh actual adapter GPU reservation before capture or recording. Copied bytes detach from
private utility targets before CPU handoff. On current-only adoption, `SharedPublicationMemory` transfers
that same original reservation into the live clone sample graph, without a second admission. Accounting is declared
before all pixel/document owners and released last, after aggregate worker retirement following stroke finish,
cancel or stale drop. A finished readback job therefore does not expose uncharged live clone source bytes.
No partial source is published; device recovery keeps the original capture/ticket and total two-restart limit.
This source-readback slice does not establish bounded owner painting or first-display upload.

PNG/JPEG export adds its complete codec/path/output/control overlap to the cached utility estimate and
reserves the actual adapter budget before immutable capture. Serial regions use the smaller of 2048 and
the composition's device texture limit. One original retained CPU ticket covers all regions, encoding,
publication and joined retirement. Its shared accounting moves with the worker release aggregate without
another retained lease. Encoding starts only after acknowledged graphics retirement; a later device change
must not repeat encoding or publication. The existing exclusive staging and atomic file publication rules apply.

The eligible canvas sampler uses one original complete CPU/GPU admission for all accepted points of its
opening. Only real input or termination resumes an idle point graph; no source slot, mapped row or useful
owner readiness is held between points. Pixel conversion runs on Compute. Cancellation acknowledges its
original worker before aggregate destruction. New-mode All Layers Wand adds its conservative selection,
history and complete output overlap to the utility envelope; GPU retirement precedes selection/history
preparation. Both aggregates retain the original shared CPU accounting until worker destruction. A device
recovery prepares once per actual graph attempt, at most two restarts, without recapturing live source.

Eligible single-layer host Clone uses the same accepted pointer stream and original retained reservation.
Its capture occurs only after whole admission, retaining the immutable document, active image and displayed
placement. Admission adds the complete full-canvas BGRA output, a possible private contiguous source copy and
bounded job/payload metadata to the cached document retirement envelope, with checked arithmetic.
`RenderLayerPixels` runs on a worker; no owner sampling, composition preparation or GPU readback is permitted.
The captured All Layers choice selects the source producer even if the live setting later changes.
Cancellation and stale authority publish no stroke, and final sample retirement releases its original charge
after its complete CPU owners, including through actual application shutdown.

The managed H3 candidate seals a `DocumentHistory::StorageIndex` of owning unique entries, raster storage and
selections. Its worker clones containers, diffs changed-entry membership and samples distinct owners; Undo/Redo
transfer unchanged entry membership between stacks. This is not a measured delta-cost guarantee. Live identities
are excluded and the diagnostic `RetainedBytes` reads fresh charges. Cached retirement includes each raster's
self-storage capacity once, unique selection capacity and index/layer/entry metadata, plus O(1) sealed-stack own
metadata. Pending, unmatched, reference and additional sources retain conservative fallback bounds. A sealed
index uses explicit `AlwaysOnOwner` retirement with its true declared bound; partial construction stays uniquely
worker-owned. Full managed Debug r74, Release r75 and ASan Debug r76 qualify this slice. Late sparse-cache publication and
history trim/reprepare synchronization, remaining caller/reference costs, analysis/ARM64, native activation
and G1 remain open ([evidence](../TestRuns/HistoryStorageIndexCandidate_2026-10-05.md)).

Settled active-layer New wand host jobs reserve the qualified phased source/flood/trace/history peak, including
retirement and job metadata, before owner capture. Event-time settings and point are bounded owned input; source
sampling, geometry and history construction run on workers. Rejected and applied graphs retire off owner. Queued
coalescing preserves document/ordinal gaps and receipts. Metadata capture/planning traversal and chrome/display
costs remain H2/H3/S5/G1 work; this migration does not itself qualify latency or broader live pointer routes.

Settled Select All/Deselect host jobs reserve the complete selection-command/history peak before copying document
records. The resumable admission boundary captures once on the owner, then constructs all geometry, after-document
and history on the worker. Replaced and captured CPU owners remain in one deferred-release aggregate through
apply/drop. Already resident immutable pixels are shared, while complete aggregate retirement receives its own
conservative release charge. Queue/cancel refusal retains the selected request until exact acknowledgement.
The value-record capture and memory/history planners still traverse document/history metadata; H2/H3 and measured
owner apply, chrome and G1 remain open. Message pumping is a structural test, not a frame or latency measurement.

Preserving selection-command authority owns only weak brush/warp identities. The memory charge also includes
the inline BrushStroke/WarpStroke records and combined control storage: after a strong owner drops, weak
references can retain the combined allocation even though its vectors, rasters and engine ownership have been
destroyed. These records are distinct from owning mutable gesture buffers. The shape opening counter allocates
nothing. Capture/history traversal, geometry equality, actual host admission and G1 latency remain separate work.

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

Pixel Move commits and prepared-display previews MUST hold the complete existing phase reservation before
owner capture. That capture retains only the bounded immutable document root and a targetless job token;
the worker resolves and copies the full layer and target identity, including mask and adjustment metadata,
before preparing raster output. Prepared previews release their extra document root on the worker after target
capture; commits keep the root for history publication. Failed/cancelled capture cannot publish a successful
targetless result. The existing unprepared-preview route still copies its layer on the owner and remains an
open capture cost before ordinary activation. Initial physical Ctrl/lift and G1 qualification remain separate.

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

An opted-in `deferredRetirement` stage invokes `RetireWorkerStage` on its worker after all useful producers join
and cancellation or allocation failure is normalized, before completion publication. Cancellation that skips
`Run` still invokes this cleanup hook. The hook performs nonthrowing destruction of already detached owned
graphs; it starts no useful work or producer, invokes no owner/context callbacks and waits for no external fence.
The owner resumes such a retirement stage only after every external reader acknowledges. The same original
record, FIFO key and complete reservation survive worker cleanup and owner acknowledgement. Undispatched work
at shutdown continues to use the joined owner and final-worker cleanup protocol; no worker stage is fabricated.
The default hook is empty, and jobs without the opt-in receive no hook call. This does not activate application
stroke ownership or qualify owner/G1 costs by itself.

GPU upload acknowledgement and a cancelled worker acknowledgement may complete in either order. A mapped-row
or fence callback arriving while the original cancelled stage is queued/running/completed MUST retain that
same record and reservation; it cannot require a second successful Resume. Final apply waits for both producer
acknowledgements. Cancelled stages never restart useful pixel work, and an acknowledged pre-cancellation copy
is not rolled back by lifetime cleanup.

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

Scheduler idle requires zero outstanding jobs/releases/trims, no active memory-readiness publication and no
undelivered owner readiness. Deferred release and recycle-trim completion enter a publication guard, decrement
the completed count, publish memory readiness, leave the guard and issue one wake. Acquire/release ordering
prevents `SettleJobs` from observing the count as zero before the notification is visible. Notification consumers
also see the completed trim count when restoring the recycle cap. The harness drains finite notifications before
declaring settled; a closed, joined scheduler is idle.

`PreviewMailbox` retains one active interactive request and one newest pending request. A new tick replaces only
the pending input and invalidates a refinement; it never repeatedly cancels the interactive worker. The command
queue is a fixed eight-entry ring. Both structures allocate no container storage. `HistoryWork` and `StrokeReplay`
hold only ordering/state metadata; P1 owns retained stroke samples, and P0.7 accounts for their bytes.

`ContinuationQueue::EnqueueAdjacent` permits an explicit nonthrowing equivalence predicate for pending preview
inputs. It replaces only the immediately preceding tail with the same document, kind and consecutive shared
ordinal; a terminal, other command or ordinal gap prevents replacement. Refusal retains incoming ownership.
The predicate borrows inputs only for the call and cannot reenter the queue. `PendingAt` exposes const pending
entries by bounded ring offset without allocation or consumption; references expire on the next queue mutation.

`OpacityInputOrder` is an owner-only, allocation-free producer admission primitive. A proposal consumes no
sequence or stream until accepted after queue admission. Controller/epoch checks consume an accepted proposal
once; move consumes the original proposal. Accepted terminals/barriers close the producer stream. Exhaustion
refuses before integer wrap. Workers receive scalar stamps and never proposals or a controller pointer.
Producer stream identity is distinct from the model's acknowledged edit opening and history authority.
These primitives alone do not activate typed opacity worker routing or establish a UI timing bound.

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

The experimental prepared layer-row path is enabled only explicitly by self-tests until full H4 activation and
G1 qualification. Admission reads sealed source/name bounds before retaining the source; unavailable/overflowed
bounds refuse the complete operation. It conservatively charges the source's complete retirement ownership,
row/name/lookup/traversal capacities, pattern/collapse storage, job/input wrappers and construction overlap.
The one owner capture copies the immutable document handle and at most 10,000 collapse Guids (160 KB); it never
walks rows, names, history, selection geometry or pixels. This bounded metadata copy still requires owner timing
qualification. Locale input is the process-startup STRINGTABLE pattern, captured once before enabling.

Workers prepare and finally destroy the entire immutable source, rows, localized labels, collapse IDs, pattern
and Guid lookup together. Owner lookup has at most 14 binary-search iterations at 10,000 layers, including hash
collisions; each iteration performs one equality and at most one ordering comparison. Publication validates
document serial, generation, separate collapse revision, session document read
authority and held layer root; the source owns every pointer identity. One active request and one newest key
bound refreshes. Cancellation retains original record/source/accounting until acknowledgement.

A successful, nonzero, uncancelled original job reservation may transfer to one of 64 fixed retained-memory
slots only during that exact job's owner final apply or `Completed` callback. Pump/progress, foreign/stale/reused
jobs and another job's final callback (including a custom apply handler) have no such authority. Transfer performs
no second admission, memory query or allocation; tokens bind scheduler lifetime, slot and generation. Retained cache
accounting alone does not keep useful work awake. Replacement/clear detaches every chrome borrow before the
old cache's final handle queues its complete graph on the worker release lane. Queueing charges
`releasingBytes` before publication; worker destruction finishes before that charge is removed. Only after
queueing may the exact retained ticket be released once. Close performs owner detach while the sink/pool are
live, joins complete retirement before final MTA teardown, and invalidates residual tokens. Tokens own
accounting, never source lifetime.

Shared immutable CPU sources use a move-only `SharedRetainedMemoryLease` inside their shared graph. It transfers
the exact original successful callback reservation through the same 64-slot pool, without a second admission,
memory query or allocation. An occupied output, wrong callback, cancelled/failed job or exhausted pool refuses
the transfer and preserves the job's original charge. The graph's final destruction must precede lease reset.
Before Close, owner-final destruction of a complete large graph uses the retirement sink; foreign-final
destruction may destroy that pure CPU graph directly. A lease publishes one generation-checked acknowledgement
after destruction, and the next owner drain releases the reservation before retrying waiting admissions. Merely
dropping the active cache while old readers remain cannot release its charge. Held leases do not create useful
pending work or periodic wakes; acknowledgement readiness is event-driven and bounded to occupied slots.

The acknowledgement endpoint is preallocated at scheduler initialization and owns a duplicate of the wake event,
not a borrowed handle or scheduler/Application pointer. Endpoint live-graph bytes remain recorded while foreign
readers survive Close or scheduler destruction; final release may touch only that independent endpoint. Close
does not wait for a foreign reader, and no late acknowledgement can release another scheduler's reservation or a
reused slot generation. `ResidentBytes` reports live shared graph bytes; owner admission remains charged until
acknowledgement consumption. `ReservedMemoryBytes` and `ReleasingMemoryBytes` can overlap while an owner-retired
graph is queued and its original lease is still awaiting acknowledgement; diagnostics MUST NOT add these values as
disjoint totals. The existing owner-only `RetainedMemoryTicket` API remains available for caches whose lifetime is
owner-confined. This primitive is an experimental foundation; the prepared layer/accessibility consumer adoption
below remains a disabled draft until its full consumer matrix and H4/G1 gates pass.

#### Joined terminal destruction accounting

`RetainRetirementMemory` is a separate accounting transfer for a terminal original job with
`deferredRetirement` and a descriptor-reserved retained slot. It transfers the entire remaining reservation
without allocating, querying memory or admitting another job. Only the exact original terminal callback, or
that original job's `RetainJoinedRetirement` hook during joined Close, can call it. Wrong ticket/generation,
occupied output, a missing descriptor slot or an unrelated callback refuses without changing the charge.
Failed/cancelled jobs may use this transfer solely to keep already-created CPU graphs and allocator-owned weak
controls charged through final destruction. The successful `RetainMemory` and `RetainPublicationMemory` APIs
retain their existing success-only rules.

Close invokes `RetainJoinedRetirement` after all recorder and compute producers actually join and before
wrapper destruction or owner detachment. It grants no successful source-capture callback, publication,
document/history mutation, resume, command submission or GPU completion. External graphics users still need
their actual original last-use acknowledgements. A `SharedPublicationMemory::BindForRetirement` owner retains
the original endpoint and complete charge after scheduler Close; only final graph and weak-control destruction
can release it. No foreign lease may attach to a reconstructed scheduler.

The stroke parent retains a scalar accounting owner through whole-payload worker retirement, then binds it
only at its exact terminal callback. Joined Close handles an owner created before the first successful owner
acknowledgement as well. This accounting owner contains no draft, pixels, UI or GPU resource. Qualification
requires success, failure, cancellation and real joined shutdown in both final-strong/weak-release orders,
with an independently admitted endpoint observer proving weak-only residency after Close. These changes
alone do not activate the live Warp route or qualify G1.

#### Completed CPU raster-preview ownership

An opted-in `RunRasterEditPreview` prepares immutable `PreparedRasterPreview` metadata on its exclusive CPU
worker after pixel work completes. It preserves the source raster, source rectangle, paint transform, fill and
every touched immutable tile. Preparing this metadata MUST NOT materialize pixels, consume `TakeChanged`, mutate
the stroke, clone a document or retain a GPU engine. An unknown owner, the owner thread or a cancelled worker
refuses preparation. A consumer validates cached scalar source/target/geometry identities before publication;
graphics preparation and its complete resource admission remain separate operations.

The original admission includes source/output overlap and additional directory, container, control-block and
accounting-node storage before capture. `RasterPreviewMetadataMemoryBytes` describes the actual grid, including
inverse-placement growth; invalid sides or checked-arithmetic failure return zero. Its current directory bound
is 1 MiB plus two `RasterTile` records for every possible 256-pixel grid cell. This does not substitute for the
raster-edit pixel, coverage, lifted-opening or source-retirement bounds.

One worker-created `SharedPublicationMemory` owner spans the preview stroke, immutable metadata and any newly
prepared lifted-pixel opening. Raster-preview callers preclaim a retained-memory slot for guaranteed transfer;
a successful original `Completed` callback binds it through `BindAfterCompletedWork`. The general completed
transfer may also select an available retained slot when none was preclaimed. Wrong generation, foreign
scheduler, duplicate binding, failed completion or a call outside the original completion refuses transfer.
No output or cache may escape before this binding succeeds. This success-only completed-CPU transfer is distinct
from the acknowledged publication callback used by recording parents; neither authorizes document mutation.

Every semantic node and its allocator-owned weak control retain that original accounting owner. Expiration of
the final strong reference is insufficient for a refund: an expired weak witness can still own its control.
`PublicationGraphAllocator` keeps the lease alive through actual control deallocation. Last semantic release on
the live owner queues the complete graph for worker destruction even below the ordinary size threshold; foreign
readers may finish destruction themselves. The original scheduler consumes the independent final acknowledgement
before refunding. A lifted-pixel cache alone must preserve the whole original admission after the preview stroke
and metadata retire. Default raster-edit callers retain their existing behavior until consumer activation is
qualified. These foundations do not qualify ordinary display activation or G1.

#### Experimental prepared layer/accessibility graph

An original multi-root publication uses one worker-created `SharedPublicationMemory` owner for its complete
CPU reservation. Its bounded allocator holds the lease until the object and its final weak-owned control block
are destroyed. Prepared rows may bind that already-bound owner exactly once before sealing. Their separate
row-object and control-block ownership roles keep the publication owner alive until both the semantic row data
and any weak-owned control block are gone; replacement of the Application/Tree cache alone cannot refund it.
This supplements ordinary completion-bound row leases. Neither mechanism authorizes a document mutation.
Graph retirement and weak-control retirement are separately required lifetime controls.

The current consumer draft uses one immutable `PreparedLayerRows` shared owner for the Application cache, Chrome
Tree model and DxUi UIA snapshots. The prepared semantic source covers the complete current visible model epoch,
including offscreen rows and their full localized names. DxUi's optional capture retains this existing owner only;
it does not traverse, copy or reconstruct the rows. The wrapper/control-block allocation has an explicit bounded
allocator and its allowance is included in the original admission before construction.

The exact successful job reservation transfers to the graph's shared lease during owner completion. Replacing the
cache or detaching Chrome does not release the reservation while any snapshot/provider/CPU reader retains the graph.
When the final release occurs on the owner while the scheduler is live, the complete graph is queued to the worker
retirement sink; destruction finishes before lease acknowledgement lets the owner release the original reservation.
A foreign CPU reader may be the final owner after Application shutdown or scheduler destruction: the lease holds an
independent acknowledgement endpoint with its own wake handle, so final graph destruction can acknowledge without
accessing the scheduler or Application. Scheduler shutdown does not wait for that foreign reader. The endpoint keeps
live graph bytes observable until final destruction. While the scheduler is live, its original reservation remains
charged until the owner consumes acknowledgement; after Close, the scheduler ledger is gone while the independent
endpoint continues tracking the live graph. These are source-level draft properties, not consumer
qualification: production remains disabled, and the full RedPrism matrix, client-reentrant UIA, actual UIA latency,
largest-count/name owner cost, activation and G1 remain open.

Cache-replacement jobs opt out of memory waiting (`JobOptions::allowMemoryWaiting = false`); ordinary jobs retain
the default event-driven waiting policy. An insufficient replacement peak finishes once with `E_OUTOFMEMORY`
without source capture, worker execution, automatic recycle-trim retry or dependence on its own retained cache.
The old cache stays charged and same-document labels remain visible with their stale tree disabled. An unchanged
key or memory event cannot rearm the refused ticket. Explicit Retry layers input admits one fresh original ticket;
duplicate input while a ticket is pending is ignored. A still-unaffordable explicit retry may refuse once again.
Failure exposes the localized retry control; success hides it. This experimental path still requires complete
interaction, owner-cost, Native activation and G1 qualification.

Settled layer metadata jobs reserve the complete private record/history graph, request and replacement name
overlap, publication/control storage and retirement before capturing document/history. Borrowed rename text must
refer to the exact selected request; only the owner capture reads it, copies it under admission and clears the
view before worker resumption. Workers never dereference live session, history or chrome. A selected ordinal and
its reservation survive cancellation/failure through terminal acknowledgement. The prepared aggregate retains
replaced document/display/history and retires through the scheduler's worker release lane. Current vector capture
and metadata/storage admission traversal remain H2/H3 work; this route alone cannot satisfy the UI duration gate.

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

An opted-in terminal-retirement stage holds that same reservation after failure or cancellation while an
external map/copy owner still borrows its receiver or storage. No stage releases and reacquires its charge,
changes its serial ordinal or admits a successor before acknowledgement. The upload layout reports only
logical texture bytes and reduction scratch; callers MUST separately reserve resource/container capacities,
driver/staging headroom, overlapping residents and complete retirement ownership before capture/allocation.
The dormant base-chunk upload leaf does not establish whole-document CPU/VRAM admission or G1 compliance.

Image Size and Canvas Size jobs declare rounded aggregate output and required semantic mask-thumbnail
allocations, one maximum private sparse-source assembly, and conservative metadata for snapshot/result/validation/history.
Optional image thumbnails are not required outputs and are excluded from these reservations. Immutable source pixels
and selection geometry remain shared. A late bounds refusal still reserves the prefix outputs already allocated
before that refusal. Source assembly, fill, sampling and required mask-thumbnail work occur on the worker;
owner validation/history costs remain measured separately and are subject to the P1.9/G1 gates.

The owner installs a thread-local deferred-release sink. Last-reference storage or directly owned pixel buffers
of at least 16 MiB transfer an intrusive node to a non-cancellable NORMAL worker lane. This path allocates nothing,
needs no job slot/reservation, and cannot be refused by a full job queue. Smaller or non-owner-thread frees run
inline. Queued and executing releases count toward outstanding work. Close joins event sources and producers,
then destroys owner-affine job/action wrappers while their sink remains installed. Their CPU owners transfer to
the release lane. Close clears the sink and acknowledges a preallocated final MTA callback that drains releases
and recycled pixels before releasing reservations, pool/apartment ownership and events. Cancellation cannot skip
this final callback. After producer join and wrapper destruction, Application's borrowed owner hook detaches
coverage engines and joined graphics/retry resources; its final worker hook clears the large document, history,
display, draft and private-clipboard owners before release/recycle drain. It also clears the application sampling
raster, cached selection and benchmark document/image roots. Benchmark timing/results and known-path cleanup remain
owned by the same benchmark object until its callers finish. Nonthrowing document construction/assignment guards
the reset; these hooks allocate no application storage. Empty selected/collapsed hash bucket arrays survive until
host destruction. Broader failed-initialization,
live-draft/lease coverage and hidden owner cleanup costs require separate proof and measurement.

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

The scheduler's opted-in owner endpoint uses three inline counters, one checked startup event and one reusable
wait on the same pool. Owner requests use at most two CAS operations and an event signal; SRW acquisition and
native dispatch submission occur off owner. These are fixed startup resources, not per-job memory declarations:
creation failure unwinds startup, and endpoint Close joins before releasing its borrowed context. Foreign producers
and default pools keep the original synchronized dispatch route. This structural bound does not prove the
2 ms owner target or excuse measurement; broker scheduling and extra callback submission must be measured.

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
filesystem calls. One fixed-size terminated path buffer is allocated within that worker scratch allowance,
reused after each acknowledged read and released on the worker before completion. Scratch refusal fails the
whole request with `E_OUTOFMEMORY` and publishes no classifications; empty, invalid and pre-cancelled batches
allocate no scratch. No large automatic or per-thread persistent buffer is required.
Cancellation keeps the active request/reservation until acknowledgement, including a late
Finished result. Stop callbacks perform only event signaling. Production `PathClassificationJob` yields its
compute lane after native header issue and awaits the Application-owned `PathReadWait` on the scheduler's
shared event-wait pool. Readiness resumes the same original ticket; actual `GetOverlappedResult` acknowledgement
precedes reuse or retirement of its file, buffer and OVERLAPPED. Cancellation and joined shutdown preserve
that ownership. The direct `ClassifyPathBatch` reference helper retains its synchronous worker wait.
Attribute/open calls remain worker calls; no timing guarantee for OS/driver calls is implied. Complete phase
admission and measured stop-to-return/G1 requirements remain separate.

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

### Prepared history storage topology

History preparation owns immutable persistent indexes before publication. Cached keys hold their raster,
layer and selection owners. A copied AVL index shares one root; changed membership copies bounded search paths
and rotations. Exact direct-source equality reuses scalar-edit raster root groups. Sparse descendants change
membership only on first/last active parent edges. Unique metadata deduplicates shared root groups; conservative
retirement still covers everything each owner can release. Node/control allowances, temporary pending/dedup
storage and private candidate/refresh overlap stay inside admitted preparation peaks. No index producer runs
during owner publication. Reference methods keep their complete diagnostic traversal; final caller migration
and comparative cost measurements remain P1 work. The index retains a sampled resident total with a coherent
residency-epoch witness; diagnostic queries read fresh resident bytes.

The process residency journal retains exactly 4,096 value records of 24 bytes (96 KiB), plus its epoch and
the atomic serial allocator. It retains no raster or publication control. Reads and writes use the existing
exclusive residency lease. Changed-epoch sampling uses a separately admitted fixed 96 KiB worker heap scratch
buffer, allocated before acquiring the lease. Its allocation failure reaches the worker runner and publishes nothing.
Unchanged epochs avoid owner scans. A complete suffix updates matching records; overwritten/unavailable suffixes
fall back to a cancellable worker scan. Serial/epoch exhaustion refuses without wrapping.
Owner completion makes one nonblocking permit attempt and resumes the existing ticket on a worker when busy
or stale. Admission uses cached source-occurrence and existing-index metadata bounds for path/rotation peaks,
including one extra refresh candidate/index/container overlap before capture. It remains held through
apply/drop and retirement. These declarations still require actual production phase/cost qualification.
Bounded growth subscriptions publish readiness only and join notices before scheduler teardown. The history-only
source remains self-test scoped; migration of every caller, activation, comparative peak/cost measurement and G1
qualification remain required H3 work. See `Document_History.md` for the staged publication contract.


### Dormant committed-document residency preparation

`RasterResidencyAdmission::Estimate` reads sealed layer/selection ownership bounds, dimensions and layer count
without traversal, capture, allocation or materialization. It refuses missing/overflow bounds, unsupported
geometry/counts and invalid device/queue policy. Its descriptor is immediate and non-owning, never a cache key
or publication authority. The owner must revalidate session/document/generation authority, actually reserve
both complete CPU and GPU budgets, then capture immutable roots without reentrant dispatch.

`RasterResidencyPlanWork::Prepare` runs on an admitted worker. The plan holds its document root and each unique
raster layout; ordered layer/role bindings share those owners. Enumeration and a fixed-capacity hash table
belong to workers. The descriptor charges container/scratch capacity and separate live/retired CPU/GPU overlap,
full mip logical bytes, canvas/stack/coverage/lookup allowances, staging and declared opaque driver headroom.
These are conservative policy allowances, not measurements of actual driver allocation or complete application
reservation. Preview, borrowed brush and full-resolution utility graphs require their own additional charges.
The committed-document plan cannot substitute its reduced display source for utility pixels.

An intrusive plan cannot copy or move. Live owner consumers MUST keep the admitted deferred-release sink
installed until all handles drain; release queues the CPU graph with AlwaysOnOwner. Without that sink the
fallback destroys inline and is permitted only on workers or after the window is hidden during shutdown.
Device/resource/mip/display publication and complete actual admission remain dormant and unqualified.


### Dormant worker-created raster residency

The optional worker creation route holds an owner-captured exact device/generation and permits resource creation
only when its successful actual threading feature query advertises DriverConcurrentCreates. A worker never
receives an immediate context. Texture/view failure and cancellation retain partial owners until owner retirement;
no resource may be borrowed before producer join and owner acknowledgement. Unsupported concurrency refuses
without an owner allocation fallback. It requires live whole CPU/GPU admission and a production fallback before activation.

GPU admission uses fixed unique nonwrapping reservation records, conservative overlap and explicit driver headroom.
The admitting domain outlives its reservations. Persistent reusable staging retains an independent charge; uncertain
copies and joined mapped shutdown retain the exact request's charge until owner graphics/context release.
Post-copy completion refusal retains strong original endpoints in the existing fixed readback slot and
counts against the existing two-in-flight limit and staging cap. One captured-context retirement barrier is
offered; healthy refusal has zero readiness until an actual device transition. There is no additional event,
thread, retry timer, recopy or Map. Partial producer failure follows the same rule. A CPU destruction stage
must explicitly resume on the compute lane after recording, leaving the dedicated recorder available to the
next original source. Returning a slot or receiving a local HRESULT never acknowledges preceding commands.
Only a final successful original-ticket callback may transfer the created destination, held source Raster and
reservation. All old job graphics references are removed before charge transfer. A base-only request leaves
lower mips undefined;
no sampling or display publication precedes the consumer's admitted complete mip/fence stage. Full document
resources, actual DXGI snapshots, consumer/fallback integration and first-display G1 remain unqualified.

### Dormant explicit raster mip recording

An optional complete-chain request creates its bounded parent SRV/next-level RTV pairs and
shader/sampler/rasterizer/depth objects on the capability-qualified worker, observing cancellation between
creation calls. An owner pump records at most one linear-clamp premultiplied BGRA/R8 parent-to-child draw and
arms a real completion event. Only acknowledgement resumes the original ticket to advance another level. No
mip stage copies pixels on the owner, maps, polls or creates resources. It changes the immediate-context
state; pump outside frame recording and fully bind following consumers.

Partial/faulted/cancelled chains cannot be adopted as complete. All raster CPU/GPU reservations remain held
through the last level and uncertain signal/shutdown references. Final successful adoption carries
MipsComplete authority, which moves and clears with the owner. Future graphics users still require their own
completion before retirement. This is one raster chain, not complete document/display qualification. Whole
graph admission, shared-context consumer/fallback tests, live activation and original first-display/G1 remain
required.

### Dormant committed-resource description before GPU admission

A separate CPU-only planning admission MUST reserve the immutable source/root, complete CPU plan/descriptor/hash
capacities and serial upload scratch before capture. Worker preparation deduplicates owned raster layouts and
live coverage keys, describes two mipped canvases, possible clipping stacks, enabled group coverage and per-kind
adjustment resources. Missing live-source IDs need blank coverage. Tables are 256x1 RGBA32F; HSV uses a 33-cubed
RGBA32F texture; grain needs no lookup. Distinct group/live roles retain their ID even when they name the same
layer. Hidden inputs and unused potential stacks may conservatively overcount. Preparation neither reads or
materializes source pixels, computes adjustment lookups, nor creates/records/publishes GPU work.

The completed scalar requirement includes these logical textures and declared staging/driver allowances.
Before any GPU creation the graphics owner MUST reserve the complete described graph against actual adapter
usage and live/retired reservations. CPU admission/source ownership survives both phases, cancellation, refusal,
partial output and deferred worker retirement. This description cannot approve driver usage or replace the
separate preview/brush/utility/pipeline graph and its CPU/device metadata. Original conservative two-domain
RasterResidencyAdmission/Prepare remains available with its original contract. This dormant description alone
does not qualify live display integration, oversized mapping, complete resource creation/fallback or G1.

### Opening reservation transfer

An opening that publishes a live worker-built brush draft MUST preclaim a retained-memory slot at descriptor admission. Ordinary callback retainers cannot consume that slot. The complete CPU opening/live declaration remains charged to its original job through source retirement, then transfers once into the draft before any accepted sample replays. It survives every reader and whole-draft retirement, with acknowledgement returning capacity on the owner drain. Failed, cancelled, untransferred and joined-shutdown records return their preclaimed slots. A slot preclaim adds no second byte reservation. Clone source ownership and final coverage/history/GPU/residency need their complete additional admissions.

### Recorded GPU command retirement

`CaptureCompletionContext` captures the actual device, context, completion route and queue generation into an
empty immutable value before dependent commands. A strong queue-origin token rejects foreign captures and
captures surviving Close/reinitialization, including reuse of the queue object address. Capture creates no
resource and records no command. `DeviceStatusOwner` queries this captured device independently; healthy
status establishes no command completion.

`EnqueueRetirementCompletion` may record a retirement barrier on that captured healthy stream after device
retirement or rebind. This is the only retired-context recording exception: no useful image draw, compute,
copy, Map or publication may use it. It shares the fixed slots and event waits. Every admitted callback follows
the real original event and reports the original generation. Fence serials MUST increase across rebinds so
an old fence cannot mistake a reused completed value for completion of newer commands.

`GpuWorkQueue::EnqueueCompletion` shares the queue's fixed slots, two-command in-flight limit and event wait
pool. Successful admission records its completion event immediately on the captured immediate context; it
creates no staging resource, issues no pixel copy and returns no mapped lease. Capacity refusal records
nothing and produces no callback. Retry waits on existing completion/release notifications; it MUST NOT poll.

An admitted callback acknowledges commands preceding the actual event on that original context, including a
retired generation. Current healthy-device success permits current publication; retired generation `E_ABORT`
permits retirement only. Confirmed device removal is reported separately. Admission or event-recording failure
does not acknowledge outstanding device work: the caller retains its resource graph and GPU charge until a
real original-stream acknowledgement, confirmed removal, or joined shutdown. Completion does not prove swap
chain presentation. Stop/join event producers before scheduler close, and release queue owners after workers
join. Receivers outlive every admitted event or this joined close sequence.

### Fresh video-memory observations

`QueryVideoMemorySnapshot` borrows the actual device, reacquires its adapter and independently queries local
and nonlocal segments on every call. It creates no device, context, fence, texture or persistent owner.
Each segment preserves its exact HRESULT and availability. Failed segments discard all numeric fields,
including driver-written partial output; successful zero-budget observations remain available but cannot
admit a graph. Device/setup failure resets the complete output and returns its exact HRESULT. Recheck the
original device removal status after the segment queries. These live segment observations are not an atomic
aggregate, and an earlier capability probe is not a fresh admission snapshot. Unknown budgets MUST NOT be
replaced with guessed RAM/VRAM limits. Current consumer activation and whole-graph admission remain P1 work.

### Original retained lease growth

`JobScheduler::IncreaseRetainedReservation` is owner-only while the containing graph is exclusively idle.
It validates the exact live endpoint/slot/generation/current bytes and pending acknowledgement before and
after its fresh physical/commit memory query. Growth uses the existing aggregate admission budget. Success
updates the same lease and retained record; no new ticket, retained slot, replacement cache or premature
refund is created. No-op growth does not query. Shrink, foreign/moved/reset leases and closed schedulers
refuse. Failed snapshot/admission/revalidation leaves both lease and charge unchanged. The whole grown
charge survives final reader Reset until its real owner drain, or joined Close; foreign readers after Close
retain their endpoint's own charge until destruction. Growth may not race lease readers or Reset.

### Original GPU reservation and transport reuse

`GpuResidencyBudget::IncreaseOwner` grows an original live reservation under exclusive owner access. Validate
its budget, slot, ticket, current bytes and original device generation. A fresh successful actual segment
observation precedes any growth; charge only the delta against all live/retired reservations and the usual
headroom. Equal totals are a no-op; shrink, foreign or stale owners, unknown observations, overflow and
pressure refuse without changing either reservation or accounting. Growth uses no additional slot or ticket.

An original parent may restart its command stream only in its exact successful joined acknowledgement,
after the producer finished successfully, every submitted command acknowledged and worker transport
retirement completed. Reuse the existing events and slots without resetting produced/consumed ordinals.
Stopped, failed, foreign, unjoined or exhausted streams refuse. An old command lease cannot name a later
command occupying the same physical slot. This API does not establish GPU completion or permit reusing a
device generation after replacement.


### Shared recorder startup cost

Processing startup acquires one GPU recording worker before its first descriptor; all display and utility
parents share it. This bounded cold-start addition replaces display's private startup acquisition. The
worker blocks on its existing pool event while idle; an unattached command pump is not pending or ready.
No timer, polling, extra GPU device, frame or useful image work follows registration. A refused acquired
prefix joins the recording worker through the original scheduler Close. Startup cost and real caller
owner/frame costs remain measurement obligations before ordinary responsive-route activation.

### Recorded utility parent admission and retirement

Admit the caller's whole source, full readback output, processing/encoding/history overlap and retirement
aggregate before source capture. The utility estimate's additional CPU bytes include the full output
(`RecordedDocumentReadbackJob::RequiredMemory`) and caller-owned overlap. Keep one original reservation;
terminal or joined retirement binds the same preclaimed `SharedPublicationMemory`, never a second admission.
GPU charges remain through actual command, copy, map and graph destruction acknowledgements.
Recorded output adoption retains that same original accounting in the Raster storage and control-block allocator.
Every copied reader and last weak storage owner therefore remains charged after the parent/payload retires.
The two additional lifetime handles add32 bytes only to charged storage/control bounds; ordinary Storage layout,
Raster handle size and pixel allocations/copies are unchanged. Include this metadata in the caller's admission;
do not infer complete operation admission from the opaque lifetime or its existence. Actual weak-owner controls
qualify lifetime/accounting; activated caller and G1 costs remain due.

Before allocating a private graph, query the actual current adapter segment and validate headroom including
overlapping declared GPU reservations. A hardware-to-WARP recovery grows the original CPU reservation by
the GPU envelope before CPU-backed graphics allocation; refusal leaves original ownership intact. Recovery
requires actual original transport/graph retirement and a fresh queue context, with at most two restarts.
No device change replays a completed CPU/external-write handoff. A suspended sampler holds its admitted
private graph but no recorder/source slot; input resumes its original ticket without idle polling.
Sampler admission retains ordinary event-driven memory waiting: temporary shortage while another reservation
or deferred release exists preserves the original opening/ticket without source capture. Real memory-change
or release acknowledgement retries admission; cancellation and Escape acknowledge that same waiting ticket.
With no prospective release, hard refusal completes once with `E_OUTOFMEMORY` and no captured source.

Merge's metadata probe uses ordinary waiting and retains its original admitted source/plan allocation through
handoff. Its dependent readback opts out of waiting: it already owns the probe allocation, which cannot retire
while that phase retains it. A fresh owner snapshot that cannot admit the complete second phase returns
`E_OUTOFMEMORY` once, before readback source capture or graphics/output allocation; it preserves document/history
and releases the probe only through actual aggregate/control retirement. This exception creates no retry loop
or second charge for the probe. Successful crop output explicitly retains the second phase's original charge
through live/history/copied readers and the last weak storage control; no refund precedes its release acknowledgement.

Copy Merged may publish its completed immutable private clipboard before detached encoding and OS
offers finish. That private result retains the precreated opaque output lifetime; the still-live
original CPU record continues to reserve the complete envelope during every useful stage/wait.
No accounting transfer occurs early: terminal completion or original joined Close must bind that
same lifetime before the original reservation can refund. Readers neither inspect nor mutate the
accounting proxy. This permits no useful work after RetainPublicationMemory transfers authority.
Private success survives later encoding failure, exhausted OS retries and cancellation; its copied
and last weak-storage owners continue to carry the original charge through actual release acknowledgement.

The owner offers one ready parent per bounded utility service pump. It performs no pixel copy or private
composition; the dedicated recorder emits bounded command batches, the event pool waits for real completion,
and Compute copies mapped rows. G1 and real cache-miss costs remain caller activation requirements.

### Separate output and temporary retirement charges

An operation whose temporary processing graph is much larger than its published output MAY partition its
original reservation into two immutable `SharedRetainedMemoryLease` values. It MUST preclaim one retained
slot at descriptor admission and opt into deferred retirement. Both destinations must be empty and distinct;
the output charge must be positive and strictly smaller than the complete original reservation. The two
charges sum exactly to that original reservation. Successful Completed, successful DeferCompletion and
terminal/joined-Close retirement use their existing distinct callback authorities; partitioning supplies no
new publication, useful-work or graphics-completion authority. No fresh query, allocation or admission occurs.

The caller MUST give every temporary allocation and its readable/control owners the temporary lifetime,
and every published allocation and its readable/control owners the output lifetime, before either escapes.
Its declared output charge includes the output's actual capacities, immutable metadata, complete shared
control ownership and any published history/container overlap. Input storage already charged by another
live owner is not charged twice. Caller phase arithmetic and real host integration require their own proof;
merely holding two opaque lifetimes does not establish a complete admission plan.

Each part publishes its own generation and acknowledgement bit only after its actual final destruction.
The owner refunds that part only after consuming its acknowledgement. The original slot cannot be reused
until both parts have acknowledged. There is no shared decrement mailbox whose late producer could write
into a reused generation. The64-slot pool stays fixed; two pending words support at most two event signals
per empty-to-ready word transition, with no polling. Existing whole-reservation leases use the first word
and retain their original semantics. Partitioned leases refuse growth without a memory query or mutation.
Weak shared-control readers retain their part. After Close, foreign owners retain the independent endpoint
and its owned wake handle through final destruction; they cannot affect a reconstructed scheduler.

This primitive enables temporary processing memory to retire while a small result remains in history.
Actual caller migration, global unique-resident accounting, constant-time capture and G1 remain required.

For long-lived immutable output, `RetainPersistentOutput*` MAY convert an authorized partition at the same
original callback boundary. The output no longer occupies the temporary64-slot pool; the temporary phase
alone keeps that preclaimed slot until its real acknowledgement. Conversion allocates nothing, queries no
memory and preserves the exact reserved/resident sum. It grants no additional authority. Every output lease
is immutable and releases exactly once into its held endpoint's atomic byte accumulator, never a reused
slot. The owner refunds only the consumed release sum. A zero-to-positive accumulator transition signals
the owned wake event; it adds no polling or per-output release record. Outstanding admitted charges bound
the sum, including concurrent releases. Weak controls retain their original charge, and outputs surviving
Close cannot release into a replacement scheduler. Persistent outputs also refuse growth without mutation.

## Prepared Wand outline points

Layer and All Layers Wand include numeric outline preparation in the original admitted job. The numeric
output envelope is the larger of the existing trace geometry-growth and boolean-output bounds, plus the
publication node/control allowance. The trace bound covers exact-reserve float points and per-loop headers
with allocator/Debug bookkeeping; the bounded boolean output includes its per-loop ownership allowance.
This envelope is additional to the complete original source, history, trace, boolean and readback phases.
It does not authorize those phases by itself. Unknown/overflowing numeric or replaced-reference ownership
refuses before capture; the worker checks its actual cached-count output declaration before allocating.

Reference outline capacity is cached during the existing chrome counters traversal. Its old buffers move
into the original aggregate at publication and stay charged until real worker destruction. The numeric
node keeps its original persistent output owner through final strong/weak lifetime; no copied points or
new owner allocation are needed to adopt a matching source. Empty points retain no chrome node. The
same source identity is held strongly by the numeric transport, preventing address reuse. Selection math,
fixed-DIP stroke/dashes, scalar view mapping and the single-threaded Direct2D factory contract are unchanged.
Direct2D path construction/retirement, other selection producers and complete S5/G1 remain separate gaps.

### Sealed local allocation accounting for immutable metadata

A selected metadata job must reserve its complete construction/input/retirement envelope before source capture. Its immutable local output limit additionally constrains every newly owned publication allocation. The local ledger is not a second admission budget and does not authorize useful work, source capture, publication or GPU work. Counter updates use checked arithmetic; allocation and sealing failures fail the selected operation with the live state unchanged.

Before original terminal or joined-retirement accounting transfer, all useful producers must join and the output ledger must be sealed. The persistent output charge includes the actual newly owned data/control capacities and its accounting allowance. The temporary part is the exact remaining original reservation, including input and retirement overlap. No later output allocation is permitted. Older shared nodes retain their original charge. Both parts survive every readable or weak/control owner until actual destruction and consumed acknowledgement; idle useful work is not permission to refund a live graph. Complete caller costs and G1 qualification remain required.


### Bounded selection ownership components (P1)

The shared owner-work multiplexer has eleven fixed leaf slots. Its complete acquired set is GPU work,
retry wait, path-read wait, utility readback, recorded commands, history residency, layer rows, committed
display, pending display, stroke finish and selection outline. A twelfth leaf refuses; duplicate/self
attachment and mutation during a pump refuse. Each outer pump services one ready leaf with rotating
fairness. Waiting leaves create no polling wake. Both selection-first and display-first cold acquisition
must preserve exact membership; joined shutdown empties and detaches every leaf before refund.

`BoundedLassoPoints` is the CPU storage foundation for future admitted lasso callers. The opening worker
allocates and touches one bounded arena under the original whole-operation reservation. A sole writer
seals immutable 64-point blocks; snapshots retain the shared arena and copy at most one inline 64-point
tail plus cached bounds/count. Push, Pop, Clear and capture perform no prefix traversal or allocation.
Slots visible to old snapshots are never reused. Exhaustion refuses before mutation. Workers traverse
input order with an admitted index stack and cancellation per block. Moved writers leave no active state.

The arena's local allocation owner and its original scheduler reservation are distinct. Dropping a
strong snapshot may free its shared control while queuing the arena for worker destruction. The original
whole-operation charge remains retained until actual arena destruction and owner acknowledgement.
`Document.Tests.BoundedLassoPoints` uses an independent vector/bounds model; the ticket tests use the
actual scheduler through success, cancellation and deferred retirement. This component does not change
the current lasso point vector, activate a host limit, migrate display/finish/history callers or qualify G1.


### Lasso host and display ownership (P1)

The explicitly acquired lasso harness uses fixed 512-event storage plus a separate terminal and the existing
continuation/selection-outline owner routes. Each dispatch applies at most one accepted input operation;
an idle live gesture disarms until input. Close descriptors acquire their shared ordinal at physical input,
and counter exhaustion refuses before mutation. Opening, geometry/history and display each require their
complete original reservation before useful capture or allocation. The point arena keeps its distinct
original charge through writer/snapshot/terminal readers; later phases cannot charge it as newly owned
output or refund it at useful-work completion. Publication output and temporary/retirement partitions
remain bound to their respective original ticket until actual final retirement/acknowledgement.

Draft owner capture/borrow copies bounded metadata/tail only; workers traverse and rasterize. The display
child shares the existing eleven-slot route and its last-use event ownership. Failure or unsupported
acquisition provides no synchronous owner fallback. Private Direct2D headroom remains an unproven policy
allowance. Native correctness does not establish complete operation bounds, ordinary activation or G1.

### Hue and panel-preserving Wand ownership

Selected Hue reserves its complete source/readback/metadata/CPU/GPU/retirement envelope before source capture.
The one-pixel worker result keeps the original ticket and shared charge through actual acknowledgement and
aggregate retirement. Source, panel and cancellation refusals preserve pixels/history/settings. Publication
creates no replacement reservation, owner pixel copy or new UI source. Accepted physical input uses fixed
storage and the existing fair continuation; no idle Hue timer or poll is permitted.

Preserving-panel layer/composite Wand likewise retains its original admission through source/geometry/history
and retired owners. Queue/selected busy state freezes mutating color controls; Cancel remains available. Exact
control/weak-owner phase bounds, device faults, ordinary activation and measured G1 remain separately required.

The private post-Wand panel binding owns its immutable selection and is included in color-edit held-memory
bounds, including geometry capacity and ownership controls. It travels with the existing panel aggregate
through capture, replacement, cancellation and retirement. No validation traverses that geometry. A ready
histogram that becomes stale across an actual document-generation change is refused; after its original
acknowledgement the existing continuation may admit one fresh capture. An unchanged source or failed result
does not create a retry loop, poll or extra owner-work source.

Direct distortion sampling shares an already published contiguous raster or constructs a private contiguous
view with `ContiguousRasterForWork`. That view owns its sampled bytes for the complete warp; a cold sparse
source and its nested tiles acquire no shared assembly. Sampling/homography/edge/rounding math is unchanged.
The existing private traversal bound and failure policy apply; no cancellation or UI timing guarantee is added
to the direct numeric-reference API.

## Descriptor-bounded continuing output and selection geometry

An explicitly opted continuing-output job MUST preadmit its entire useful/input/retirement peak and terminal
slot before capture. Its intermediate output charge and the record's remaining reservation partition the
same original admission. No grow/query/allocation/new FIFO identity or extra retained slot is permitted at
transfer; terminal binding occurs only after the final useful producer joins. Data and allocator/control
owners, including expired weak controls, retain their charge through actual destruction. Joined Close clears
the former scheduler counter while foreign leases retain an independent endpoint; a cleared counter is not
proof of final foreign refund.

Selected Select All/Deselect output bounds include newly allocated history metadata and publication-owner
allowance. Select All additionally includes cached geometry storage plus WorkOwnershipAllowance; StorageBytes
alone excludes allocator/control overhead. Admission also covers both publication owners and construction
peak. Checked arithmetic or insufficient output refuses before mutation. Opaque prepared gesture cleanup
uses the exact prebuilt Begin-dependent End and latest own scalar authority, with no owner geometry/history
construction or fresh admission. Physical callers, every display consumer and G1 still require qualification.

## Selected deferred-outline live publication charges

A settled selected outline publication keeps its original charge while live history or geometry still owns
its data or shared controls. Move's history entry retains the charged opening snapshot and its final
entry/after/translated geometry; neither charge may refund at terminal acknowledgement alone. A zero-offset
End drops the unused opening before the separately charged Deselect publication. Exact-current abort returns
to the original history. Stale refusal preserves any still-live original opening charge through its real last
reader, including a foreign editor/session lifetime.

Residency refresh shares the complete original admission and retains its original source, prepared payload
and ticket. No owner wait, pixel read, index/geometry reconstruction, re-admission or unbounded temporary
allocation is allowed. Test-only readiness callbacks borrow immutable rasters/events and never an Application,
mutable document session, UI or GPU owner. The selected structural proof does not qualify ordinary input or G1.

## Lasso opening settings and history authority

Queued lasso opening may retain fixed-size primitive kind/AA settings and an exact history authority value. Capture and owner acquisition perform bounded identity/epoch checks without scanning history or allocating point vectors. The input, writer/control, arena, source roots and retirement must remain covered by the original admitted ticket; this small settings API does not provide complete physical-gesture admission. A history Begin invalidates old openings even when document generation/revision are unchanged. Final retained-resource refund remains independently required.

## Rectangle/New worker envelope and owned writer retention

Rectangle/New geometry may use the fixed `PlanNewRectangle` envelope: one arena block, no sealed-prefix traversal, 64 KiB clip workspace, 8 input points/2 input loops and 16 output points/4 output loops. The actual worker must use these same limits. Old selection geometry is retained/accounted separately and is not a New clip input; Add/Subtract cannot inherit this plan for a large prior. This geometry envelope excludes source/history, host prefix, display, refresh and retirement, which must be admitted before capture. Opted-in prepared lasso owners keep their original writer/arena charged after abort or document replacement until final strong and weak owner release; neither settlement nor a removed live draft alone permits refund.

## Original-parent Rectangle/New admission

The selected physical parent describes both inside-outline and outside Rectangle/New branch peaks before source capture. Cancellable containment chooses a branch without re-admission. Opening, retained input, final geometry/history, refresh overlap and retirement stay under the original ticket. The drawing payload is allocated by that worker; owner replay performs scalar authority checks and accepted marquee updates. Rectangle/New executes the admitted 64 KiB geometry workspace and never traverses a large old outline as a New boolean input. Terminal cleanup retains strong and weak owners until their exact original charges can refund. Actual ordinary callers, complete display ownership and G1 remain separate qualification.

## Selected physical Rectangle lifetime qualification

Pointer Down/Move/Up/Lost use bounded descriptor/prefix storage; original admission covers both selected branches before any outline/history root capture. Reuse the same ticket for useful stages, terminal preparation and residency refresh. Suppress useful completion on joined Close, retain borrowed owner state until its producer joins, and release only its exact history stamp/prefix. Final worker retirement must leave independent observer reservations intact. Selected real-Win32 Release/ASan proof does not establish ordinary activation, complete consumer admission or measured G1.

## Ellipse/New bounded geometry envelope

Ellipse/New admits and executes a 4 MiB clip workspace, 2,052 total input points/two loops and 4,096 output points/16 loops. One arena block supplies the marquee's four scalar corners; ellipse generation is capped at 2,048 vertices on the worker, with no sealed-prefix traversal. The captured-kind admission marker prevents use of the smaller Rectangle workspace. New ignores the old outline as clip input while retaining its separately charged source/history/control owners. Complete original-ticket phase/refresh/retirement admission remains mandatory; these bounds alone do not cover display consumers or G1.

## Selected physical Ellipse admission

Physical Ellipse/New reads the cached typed plan before geometry/history capture. It uses the existing
4 MiB worker workspace, 2,052 input-point and 4,096 output-point ceilings; the original parent also
charges retained source/history, opening, construction and retirement. The pointer callback captures
only fixed settings and accepted document coordinates; it never builds an oval or changes the declared
kind at a later header tick. This selected route supplies no ordinary-host or G1 timing qualification.

## Cached preserving-marquee capacity policy

Rectangle/Ellipse Add/Subtract describe a complete original admission from cached immutable source point
and loop counts before capture. `MarqueeLimits(kind, mode, sourcePoints, sourceLoops)` and the worker
`PlanMarquee` MUST use the same captured counts and derived limits. New retains its fixed existing bounds.
For preserving modes, let P/L be source counts and NP/NL/NW the selected New output-point/output-loop/
workspace ceilings. Input points/loops are P+NP and L+NL+1; output ceilings are min(8,000,000,8P+2NP+16)
and min(2,666,667,P+L+NL+16); workspace is min(128 MiB,NW+512(P+NP)+256(L+NL+1)).
Checked overflow, invalid counts or exhausted enforced capacities fail the original job without partial
publication, owner fallback or new admission. These coefficients are capacity policy, not a theorem that
every possible boolean result fits. The original reservation also covers retained source/history,
construction, output owners, refresh and retirement; the geometry plan alone cannot cover those consumers.

## Selected preserving marquee Win32 admission

The selected physical Add/Subtract caller reads the typed cached source-count plan before capture and
uses that exact original reservation for every stage. It performs no inside-outline containment and
cannot substitute a smaller New-only envelope. Physical-prefix overflow chooses cancellation without
allocating replacement input. Joined Close retains the producer/source until the original acknowledgement;
all source/geometry/history/control readers retain their declared charges through actual retirement.
The normal-memory structural proof does not replace complete consumer, ordinary-host or G1 measurements.

## Cursor request state prerequisite

`SelectionCursorRequests` holds at most one active request, one replaceable latest descriptor,
one cached reply and one failed-admission identity in at most 256 bytes of trivial storage.
Its key binds document serial, document generation, cursor epoch and finite document coordinates.
A replacement cannot dispatch until the exact original request is acknowledged; foreign or duplicate
acknowledgements cannot consume it. Invalid queries and Close clear publication authority while
retaining the active identity through acknowledgement. Close is permanent. A failed identical query
cannot retry on repeated pointer messages; a changed query can request admission again. Serials
never wrap. The helper owns no geometry, scheduler, document, history or graphics resources.

This scalar prerequisite alone neither moves the physical cursor consumer to a worker nor qualifies
ordinary activation, complete memory admission or G1. The selected host must still admit and capture
immutable geometry, cancel stale work, retire captured readers and acknowledge joined shutdown.

## Cursor geometry capture prerequisite

Cursor-probe description performs cached scalar queries before admission, without point traversal,
geometry/history capture or translated-vector construction. Its retained envelope includes original
geometry storage plus the existing loop-control allowance; a selected host must additionally admit
its wrapper/payload and every live phase. Exact-plan capture shares the immutable root only after
admission. Deferred offsets stay scalar, and invalid, overflowed or stale plans fail locally. These
model bounds do not establish physical caller activation, complete lifetime accounting or timing.

## Selected cursor probe scheduling and retirement

Ordinary Run startup enables the CPU cursor consumer after runtime-source setup and before startup paths
or ordinary input. It does not require prepared GPU display/stroke activation, and later prepared source
setup cannot disable it. The cursor consumer submits one bounded Background record with the complete cached geometry
envelope plus fixed wrapper/control allowance. Pending queries replace scalar metadata rather than
allocating or traversing path vectors. Source acquisition and useful work reuse the original record;
no extra admission is taken. Failed memory admission and cancellation before the first successful stage
still acknowledge that exact record: the success-only completion hook cannot supply its identity.

Captured geometry readers retire on the worker, including cancelled/skipped useful stages. Shutdown
closes publication authority, cancels the original ticket, joins workers and only then acknowledges
owner state; normal completion callbacks remain suppressed during joined Close. Source, reservation
and wrapper lifetimes cannot be shortened by a neutral cursor result. Enabling this cursor consumer does
not qualify other ordinary prepared consumers, complete-operation G1 or a Photoshop comparison.

## Selected Freehand whole-gesture envelope

`PlanDrawing` captures kind/mode, exact arena capacity, worker geometry limits and finish memory
before any source/history root capture. Freehand has 1,023 sealed 64-point blocks plus one inline tail
(65,536 accepted vertices); the worker allocates and prefaults every admitted arena page. New admits
32 MiB geometry workspace, 65,540 input points/two loops and 262,160 output points/65,552 loops.
Add/Subtract apply the checked preserving-source capacity formula to these base limits and cached
source point/loop counts. These are enforced capacity ceilings, not a promise that every possible
self-intersection fits. Exhaustion fails the original without larger re-admission or partial output.

The original operation also admits wrappers, retained source/history, arena/traversal, old/new outputs,
construction overlap, display/history refresh and final strong/weak retirement. The geometry plan
alone does not cover the host. The same captured plan governs allocation and actual worker limits.
Bounded owner replay uses at most sixteen accepted moves per continuation and one prefaulted
64-point chunk per metadata update. Only known pending input rearms work; settled empty input
causes no wake. The arena prefix is neither traversed nor recycled there. Complete path geometry,
coverage and history work retain worker ownership. Structural bounds establish no 2 ms/G1 timing
qualification, ordinary activation or permission to use the synchronous reference as a failure fallback.

## Polygonal scalar plan and guarded metadata

The Polygonal plan uses the same enforced 65,536-point/1,023-block base capacity policy as Freehand,
with source-aware preserving limits, complete worker workspace/output declarations and checked
refusal. Backspace/refill does not recycle a sealed block while readers exist, so exhausted arena
capacity can refuse before the live-point limit. Exact key validation precedes each bounded metadata
update; no cursor path becomes an accepted corner. Last-corner draft removal keeps its prepared
writer and held snapshots charged. These CPU bounds do not substitute for original-parent/host,
complete consumer, ordinary activation or measured G1 qualification.

## Polygonal original drawing admission and retirement

The drawing-only descriptor admits wrappers, retained source/history, finite arena, complete bounded
geometry/boolean preparation, old/new outputs, construction overlap, residency refresh and strong/weak
retirement before source capture. The original ticket/reservation survives every useful phase; no
second admission or reservation growth is permitted. Opening uses one continuing output partition.
Final success or retirement uses one output/temporary transfer from the remaining original reservation.
Before opening publication, bind opening/temporary for retirement; no finish graph can exist there.
Independent roots MUST NOT each consume the same descriptor-reserved terminal slot.

Descriptor submission may succeed before memory is sampled. Insufficient dispatch memory fails the
original job with `E_OUTOFMEMORY`, no Run/source capture and exact input drop. Retained live/weak
readers remain charged through worker destruction and refund after the actual last owner. Replay is
bounded to sixteen samples per continuation; full geometry/history and large graph retirement are
worker work. Structural qualification does not establish measured 2 ms/G1 or ordinary activation.

## Polygonal terminal metadata admission

The optional pre-admission physical terminal ordinal is a const scalar in the original drawing state.
It is included through the existing complete wrapper/state size bound and adds no per-event allocation,
copy, source traversal, wake or admission. The same original ticket/reservation owns the recorded
terminal through capture, publication/drop and final strong/weak retirement.

## Polygonal event-driven owner gates

Readiness and provisional-decision hooks use bounded owner scalar/function-pointer storage covered
by the original complete state/wrapper admission. Peek-before-Take preserves blocked fixed records.
A blocked original job keeps its ticket and full reservation; it allocates no new job/admission and
requests no polling wake. A known prerequisite/input change may resume it. Replay remains limited
to sixteen accepted metadata samples per continuation. Hooks run only on the owner; history/geometry
work and final graph destruction keep their existing admitted worker ownership.

## Selected Polygonal physical owner resources

The selected native caller keeps the original drawing ticket, complete admission and prepared writer after
its creator receipt completes at acknowledged opening. Metadata-only interactive parking holds no global
HistoryWork barrier. An ordered closing descriptor or cancelling host action acquires its own terminal
stamp; only original completion/join releases the writer, input and charges. Save reads committed document
state without ending the draft; later corners remain governed by shared physical/host ordinals.

Owner readiness and provisional-decision notifications resume the same parked parent through existing
scheduler events. They add no polling, source/geometry walk, replacement original admission or per-point drawing job. Derived draft/outline display work
keeps its existing latest-request protocol and budgets.
Input remains in the existing fixed ring and host descriptor queue. Unsupported competing producers
refuse before capture/mutation. Joined shutdown suppresses useful callbacks and refunds all retained
strong/weak CPU/GPU ownership. Structural native cases do not establish the measured 2 ms/G1 budgets,
complete view/device/scope qualification or ordinary activation.


## Polygonal unresolved-branch input journal

`HostPolygonalInputJournal` is an owner-only scalar prerequisite. It stores at most 512 accepted physical records plus one separate emergency Lost record. Each Down owns its complete event-time scalar context; Move/Up/Leave/Backspace/Enter/Escape preserve FIFO order and the shared nonzero monotonic ordinal. No record retains a document, history, raster, UI or GPU owner, and recording/replay performs no allocation or polling. An Up is not a journal-family terminal: it may end an inside drag while younger accepted Down records belong to later ordered parents.

Every sample owns its physical pointer position, document coordinates, document-space view origin, physical-pixels-per-document-pixel scale and physical-pixels-per-DIP scale. Deferred near-first evaluation projects the replay-selected actual first point using those captured operands, then applies the legacy 8-DIP radius in physical view space. Reconstructing the radius from rounded document coordinates or using the current viewport is forbidden; large pan offsets can change that decision. `NearFirst` computes proximity only and grants no three-corner, double-click, close or history authority. Invalid/nonfinite sample operands or first points refuse locally.

Capacity loss preserves all 512 earlier records and adds exactly one emergency Lost at the last accepted ordinal; it cannot wrap the counter or replace an earlier successful parent boundary. Sealing refuses later records. Explicit owner CloseInput and a fully drained prefix/emergency loss are required before Finish; the owner must retain the journal until all borrowing parents and conditional host receipts are acknowledged. Stream identities cannot repeat or wrap. Generation/restore/target values are preserved per Down; the dispatcher validates their semantic authority after admission and branch/order selection without arbitrary generation rewriting.

The helper and complete scoped HostTests proof do not implement the physical New/nonempty-prior dispatcher, choose a branch, reserve a job, change selection/history, or enable ordinary input. That dispatcher must admit both branch peaks before source capture, use one original containment/job and preserve younger gestures after an inside Up. Remaining caller, phase/retirement, ordinary-host and G1 gates stay mandatory.

## Polygonal original-parent journal branch resources

The original selection parent may borrow one journal family without owning or copying its ring.
Its view and last accepted scalar event are covered by the cached state/job wrapper envelope.
The journal overload admits the existing finite Polygonal opening/arena, complete worker geometry
and Boolean workspace/output, inside movement, history refresh and strong/weak retirement peaks
before retaining immutable source roots. Both branches stay on the same ticket/reservation.

Each Polygonal owner continuation consumes at most sixteen accepted physical records, including
Up/hover/keys; an unready ordinal or terminal remains unconsumed. Empty input and false readiness
park without polling, retry admission, geometry allocation or idle wake-up. This journal quantum
does not change the already qualified legacy press-stream batching. Conditional corner decisions
report only scalars. Pixel/geometry/history work stays on admitted workers; publication is current-only.

Acknowledging one borrowed parent never closes/drains the family. The owner must keep the ring
alive through every borrowing original parent and conditional receipt. An inside Up that precedes
full-ring overflow retains its successful outcome. Finish requires explicit owner close, drained
prefix/emergency loss and joined borrowers. Normal terminal callback is the last state access;
shutdown joins and retires without useful callbacks. An owned-predecessor transition uses the
explicit scalar receipt below; stored arrival generations remain immutable.

[Complete scoped CPU evidence](../TestRuns/P1PolygonalGestureDispatcher_2026-10-18.md).
Physical host activation, measured owner/apply budgets and final native matrix remain required.


## Owned selection transition resources

A transition receipt stores only fixed journal/family/ordinal metadata, a minimum generation and
opaque post-document/editor/history authority. It owns no document/history/raster/UI/GPU roots.
The original state/job wrapper envelope includes its two inline optional receipt slots and cached
minimum generation through `sizeof`; cached admission precedes all immutable-root capture.
Validation, move/consume, issue and `TakeSuccessor` perform no allocation, traversal, pixel copy,
wait or wake. Chaining three or more completed original parents costs constant metadata, rather
than a growing receipt/root list. The existing sixteen-record replay and retirement bounds apply.

Only acknowledged successful selection publication may issue a receipt. Its exact post authority
rejects foreign edits even when generation is unchanged (for example a pending History Begin).
The caller retains the stable journal until all borrowed parents and issued receipts are retired.
This core authority primitive does not reserve physical host conditional descriptors or establish
ordinary/G1/frame budgets. [Evidence](../TestRuns/P1PolygonalOwnedTransition_2026-10-18.md).


## Original physical Polygonal caller resources

The stable inline journal stores only the existing bounded physical metadata. Each accepted Down/key uses the existing eight-entry host descriptor queue; original parent/job allocation is a cold admitted operation and allocation failure is explicit. Journal recording/replay, predecessor handoff and pure readiness perform no geometry walk, document/history copy, pixel work or GPU ownership. Owner replay and orphan draining consume at most sixteen scalar records per action. The all-record bound includes ignored inside Enter/Backspace events.

Selected terminal admission is separate from pure readiness and precedes Take/source capture. Its only side effect is acquiring the exact host history barrier; it cannot reenter, dispatch UI or mutate session/input. Progress only signals the existing continuation source once per consumed batch or newly parked rendezvous, including no-change corners. An unready record stays unconsumed and cannot resume itself. These hooks add fixed scalar state included by the existing sizeof-based wrapper admission.

Outside Opened releases the interactive host barrier without another reservation or writer. Inside remains atomic through Up while Save waits. Temporary view pan and right selection input cannot acquire a second selection writer or invoke synchronous lasso geometry. Parent terminal clears controller/acknowledges before its final callback; joined shutdown retires borrowed state after producer join. Complete Release/real-ASan CPU and selected host rosters, exact ancestor-object/command lineage, linked ISA, full PE/PDB/artifact and genuine CPU UAF checks qualify this component. This does not establish ordinary activation, complete display/lifetime phase bounds or G1. [Evidence](../TestRuns/P1PolygonalOriginalHost_2026-10-18.md).

## Polygonal admission-loss resource closure

A stale queue entry can retire without selecting a job, so there may be no worker completion available to rearm cleanup. The no-active TakeReady path explicitly retires its raw family on the existing source. Orphan draining remains limited to 16 scalar records per owner action; no new pool, polling or large owner destruction is introduced.

Raw512/emergency and host-eight-descriptor limits are independent. Queue refusal seals the accepted paired prefix; overflow preserves an older accepted inside Up and moves cancellation to the younger parent. One failed scheduler record is permitted for memory admission failure, while worker runs, immutable-root captures and opened writers must remain zero. Complete selected Release/real-ASan native checks compare exact receipts, literals/history and final reservation/CPU/GPU/weak-owner refunds. The shared headers and CPU job implementation are unchanged from the prior qualification, so unchanged CPU/HostTests are not redundantly rebuilt for this caller-only correction. [Evidence](../TestRuns/P1PolygonalAdmissionBoundaries_2026-10-18.md).


## First-parent foreign-history admission and inside-Save ownership

The selected Polygonal first-parent guard reads transaction depth from the existing bounded history root snapshot before memory description/submission. It walks no entries, pixels or points and allocates nothing; the live history retains every shared root through destruction of the temporary view. Refusal creates no original, source capture or writer and cannot close a foreign transaction. A live original's conditional path bypasses this first-parent guard.

An inside drag retains its original reservation/ticket/barrier while ordered Save waits. Real Up records only bounded scalar input and resumes that original job; no synthetic terminal, competing selection parent, extra admission or busy polling is permitted. Save starts after the exact move/history acknowledgement. Final Release/real-ASan checks require unchanged prior assertions, exact source/object/profile lineage, final ISA/PE-PDB/artifact checks and independent readback of both actual saved pixel packages.

[Qualification](../TestRuns/P1PolygonalForeignHistoryAndSave_2026-10-18.md).

## Parked Polygonal document lifetime resources

DocumentChanged compares only the cached original admission documentSerial before existing display work. Replacement/Clear cancels the same original and resumes it only when parked. This constant-size readiness path performs no root/geometry/pixel traversal, new job admission or foreign-session mutation; joined original retirement remains responsible for its complete existing reservation. A full 128-record scheduler refuses the selected original before worker/root capture or writer opening; the scalar constructed diagnostic state may remain, with no controller/runs/captures/parent admissions. Accepted predecessor jobs must still complete and refund.

Actual PNG/JPEG and quiet picker-seam cases retain the existing writer reservation across export/cancellation. Full selected Release/real-ASan Application rosters, exact source/header/object lineage, PE/PDB/ISA/artifacts and real file readback qualify this component. They establish no ordinary activation, complete phase bounds or G1. [Evidence](../TestRuns/P1PolygonalLifetimeAndExport_2026-10-18.md).


## Scalar raster preflight and worker target capture

Raster edit validation and memory estimates, including Gradient and Pixel Move, MUST use the scalar
`BrushStroke::PlanGrid` geometry. Constructing a full BrushStroke merely to inspect its extent is forbidden
before admission: its containers can allocate even when no pixel data is painted. This includes invalid,
empty and outside-selection queries. Preserve the existing grid, byte-budget, overflow and status rules.

RasterEdit commits and Gradient commits/prepared-display previews capture the admitted immutable document
root and targetless token on the owner. A worker resolves the full layer and target identity, including mask
metadata. Assetless and mask-only targets retain the existing operation-specific validation. Prepared previews
drop their extra document-root owner on that worker; commits retain the source for prepared history.
Unprepared Gradient/Pixel Move previews still require a complete admission envelope before their remaining
owner capture can be removed. This component does not enable those ordinary display routes.


## Initial Ctrl pixel CPU foundation

The selected fixed-press CPU parent admits both the old selection/drawing envelope and its possible pixel
branch before source capture. Its current conservative extension is nine times `PixelMoveMemoryBytes` plus
three16KiB wrapper allowances: three units for continuing opening, four for final output and two temporary.
All additions use checked arithmetic; unavailable/overflowing bounds refuse. This declaration and small-fixture
proof are not a measured large-document phase bound or authorization for ordinary activation.

Hit, lift, full layer/mask/name capture, final pixels/outline/history and residency refresh execute on workers
under the same original ticket. Source, scalar offset and captured duplication remain fixed to that gesture.
No second per-tick PixelMove job is part of this CPU protocol. The real original output reservation survives
independent final raster/tile/selection owners and expired weak controls; only last-control destruction and
actual release acknowledgement refund it. Owner operations copy handles or swap prepared owners; failure
cannot acknowledge or discard a newer opening. The later frame consumer separately admits its full CPU/GPU
source, metadata, upload and actual-use retirement before producing a display graph.

### Procedural Ctrl host frame ownership

The opted-in physical Ctrl route MUST keep hit/lift/closure on its original fully admitted selection job.
Pointer moves describe scalar offsets; they MUST NOT schedule a legacy per-tick CPU PixelMove preview.
Each frame consumer admits its own declared CPU/GPU envelope before capturing the original immutable lift.
Frame roots are constructed and finally retired on workers, including failed, stale and undrained openings.
The frame's original complete CPU charge remains bound through surviving strong and expired weak metadata
controls after actual GPU/root retirement. A newer device or consumer cannot acknowledge an older use.

The current allowances are conservative declarations. Small-fixture accounting and actual-use tests do not
prove production peak/phase bounds or visible latency. Final original-parent rows/GPU publication and G1
remain separate qualification gates before ordinary activation.

### Colour capture cost and retained opening records

The selected deferred destructive and live adjustment-layer opening and preview/commit capture MUST NOT copy mask asset-name
strings. Valid mask asset names are not bounded by the layer-name limit. Capture shares the immutable
opening records and keeps only scalar/shared-handle target authority. Commit preparation copies carried
metadata on its worker. `ColorEditHeldMemoryBytes` includes the complete cached source-root ownership;
commit planning reads the original mask name size rather than the descriptor's empty string. Source,
temporary, history and display aliases still require complete operation admission and real retirement.
The declared bounds remain conservative; final production phase bounds and G1 are separate gates.

### Original-parent final publication admission and waits

The selected Ctrl/Polygonal final display envelope is included before the original Submit. Cached current
source plus final pixel/selection bounds cover future source ownership; rows/collapse controls, command
shadows, WARP storage, final output and positive temporary overlap also belong to that descriptor. All sums
and ID bounds are checked. Continuing-output transfer forbids later reservation growth. These conservative
declarations require complete production phase qualification before ordinary activation.

First-row epoch, recovery-slot, visibility and history-residency waits preserve the original ticket/admission/FIFO. Readiness
uses the existing final-work source and acknowledged stages, with no polling timer or extra mux leaf.
Current-source failure, cancel and Close must not leave accepted work dormant indefinitely. Shared final
publication retains all old rows/graphs and charges until worker retirement and actual GPU last use.
Output-aware resize APIs retain the caller's original accounting through new pixels, semantic thumbnails,
sealed roots and prepared history; their adoption is not a complete Size host/2 ms or G1 qualification.


The sizing job constructs its large source/history/result payload on its first admitted worker. The owner
acknowledgement captures into that existing payload; it must not allocate a replacement wrapper. Capture
failure, stale completion and Close still retire the complete payload on a worker. This structural rule
does not qualify Size final GPU/chrome publication or its complete 2 ms Apply target.


### Size final-display phase admission

Unlike the continuing Pixel Move output, Size exposes no intermediate output. Its initial reservation covers
CPU/history preparation, both worker-created accounting controls and positive old-source/temporary overlap.
An acknowledged original may grow once from the exact immutable final rows/raster/WARP descriptor, before
capturing new row/device owners or allocating that graph. Growth refusal fails locally with no CPU/history
publication. No reservation growth is allowed after final output binding. The complete old parent payload
retires on a worker, never in the owner's terminal wrapper destructor. Conservative bounds and small host
controls do not establish the 2 ms Apply target or measured G1.


### First-worker row holders and predelegate Size failures

An admitted prepared-row job constructs its RowsInput holder on its first worker entry. Owner acknowledgement
captures immutable source handles, the localized pattern and the existing bounded collapse-ID snapshot into
that holder. Allocation refusal before capture preserves the source/history and enables the existing explicit
retry. It does not rearm itself on unrelated memory events. This change does not remove the bounded owner
collapse-ID capture or qualify complete H4 phase peaks, UI Automation lifetime or G1.

For the selected Size final-display path, a failed or cancelled worker stage before delegate construction
destroys any private resize/history payload on that same original worker before terminal owner delivery.
It MUST NOT depend on admission of a second release job to avoid a large owner free. Descriptor-only original
document authority survives payload retirement so restore/session/invocation staleness still refuses. Source,
history, original accounting, actual GPU-use retirement and joined Close rules remain unchanged.
