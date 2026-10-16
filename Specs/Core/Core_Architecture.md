# RedPrism architecture ownership

Status: current normative product contract
Last reviewed: 2026-10-16
Owner: `Application`, `Renderer`, Document (`Common/Document.*`), and DxUi adapters

## Scope

This contract owns process-wide ownership boundaries for the Windows composition. Window styles, DPI, and command-line
host behavior are in [`UI/UI_Windowing.md`](../UI/UI_Windowing.md). Idle, Present, and surface rules are in
[`Core/Core_PerformanceAndResources.md`](Core_PerformanceAndResources.md). The toolchain is in
[`Build/Build_Toolchain.md`](../Build/Build_Toolchain.md).

## Process shape

RedPrism is a single Win32 process (`RedPrism.exe`). v1 has no plugin ABI. DxUi C++ stays inside this executable when
C2 pins `DxUi.lib`. If a later plugin story appears, the ABI is COM/POD only — never DxUi types, STL, WIL, or
exceptions across a DLL boundary.

```text
Win32 top-level HWND (WS_EX_NOREDIRECTIONBITMAP)
  Application  : messages, DPI, files, commands, frame scheduling, CanvasRenderer, CanvasViewport
  Renderer     : D3D11 device, DXGI flip-model swap chain, Present, occlusion
  Document     : layers, history (never GPU device ownership)
  DxUi EmbeddedHost : retained chrome (borrows the app device; no swap chain)
```

There is one D3D11 device, one flip-model swap chain, and one `Present` per presented frame. `CanvasRenderer`
borrows that device. It MUST NOT own a swap chain.

## Ownership

| Owner | May own | MUST NOT own |
| --- | --- | --- |
| `Application` | Top-level `wil::unique_hwnd`, WndProc binding at `WM_NCCREATE`, display-power notify, command line, `SelectHostFrameAction` loop, `DocumentSession`, `JobScheduler` (`Common/Jobs.*`), `CanvasRenderer`, `CanvasViewport`, the settings file and its directory watch ([`Core_Settings.md`](Core_Settings.md)), the theme, the UIA window root's placement | D3D device, swap chain, Present |
| `JobScheduler` (owned by `Application`) | Bounded job records and UI-action queue, cancellation, progress/completion publication and owner-thread delivery | Window, chrome, document/session state, GPU context, image-operation policy |
| `GpuWorkQueue` (optional registered owner pump, `RedPrism/GpuWork.*`) | Bounded staging/copy records, events/fences and mapped-row leases, old context/texture references through acknowledgement | Swap chain, Present, new device creation, worker pixel computation, document mutation |
| `Renderer` | One `ID3D11Device`, one `ID3D11DeviceContext`, one `IDXGISwapChain1`, RTV, frame-latency waitable, occlusion registration | A second device, a chrome HWND, GDI paint, DxUi library lifetime, document rasters |
| Document | Layer graph, COW rasters, history budget | HWND, DXGI factory, DxUi host |
| `ShapeEditWork` (Document) | Detached draft/source/history authority, CPU coverage and adopted pixels, complete prepared layer/history publication | Live session mutation, HWND, chrome, GPU engine/context |
| `MaskEditWork` (Document) | Detached source, selected-layer IDs and history authority; inverse-grid coverage, Gray8 mask and semantic thumbnail; prepared mask-target/selection/history publication | Live session mutation, HWND, chrome, GPU engine/context |
| `LayerCopyWork` (Document) | Detached source/target/placement/selection/history authority; selected pixels or a complete duplicated layer; prepared document/history publication | Live session mutation, HWND, chrome, GPU engine/context, private or OS clipboard |
| `Kernels` (`Common/Kernels.*`) | Immutable process-lifetime CPU function tables and CPU/OS feature detection; noinline tier implementations in separate translation units | Document/session state, HWND, GPU context, scheduler |
| `WorkerPool` (`Common/WorkerPool.*`) | Private fixed-size Windows pool, bounded payload-free dispatch wakeups, callback cleanup group, implicit MTA lifetime, cancellation and parallel helpers | HWND, document mutation, GPU context, a second scheduler |
| `WaitPool` (`Common/WorkerPool.*`) | Dedicated one-thread pool and at most 32 one-shot event waits | Pixel work, waits inside callbacks, document mutation |
| `ClipboardTransfer` (`RedPrism/ClipboardTransfer.*`) | Private worker-local message-only immediate-format window, movable global blocks, scoped clipboard open/locks and standard sealed inspection/copy values | Application HWND/pointer, chrome, document mutation, delayed-format publication, borrowed OS pointer/handle in its public values |
| DxUi EmbeddedHost | Retained chrome models and their GPU surfaces | Swap chain, renderer HWND, worker, periodic timer |

There is exactly one D3D11 device, one flip-model swap chain, and one `Present` per frame that actually presents.

## Lifetime and RAII

- Bind `Application*` at `WM_NCCREATE` through `GWLP_USERDATA`. Release that pointer at `WM_NCDESTROY`.
- Destroy the HWND with `wil::unique_hwnd::reset()`. After `WM_NCDESTROY`, `release()` so the wrapper does not
  `DestroyWindow` a dead handle.
- Owned COM objects are `wil::com_ptr_nothrow<T>` assigned with `.put()`.
- `wWinMain` and `WndProc` MUST NOT let exceptions escape. `catch (...)` is forbidden. The job runner catches only
  `std::bad_alloc` and turns it into a failed job carrying `E_OUTOFMEMORY`; explicit `noexcept` construction and
  admission boundaries may return `E_OUTOFMEMORY` when the caller fails the operation or exits. Other allocation
  failures remain fatal.

## Worker execution foundations

`Common/Executor.h` and the public `Common/Jobs.h` boundary are standard C++ only. `JobScheduler` hides its
platform implementation behind a pimpl and is owned by `Application`. Its fixed capacity is 128 job records,
including queued, running, suspended and completed-but-undrained jobs, plus eight queued UI actions. The seven classes are
`InteractivePreview`, `Refinement`, `Stroke`, `Selection`, `Commit`, `Io` and `Background`; they map to HIGH
(the first four), NORMAL (`Commit`, `Io`) and LOW (`Background`) worker priorities. `Stroke`, `Selection`, `Commit`
and `Io` jobs run FIFO per `(class, serialKey)`, retaining their key until their result is applied or dropped.
Zero is the conservative shared key. Worker callbacks carry no job payload; records remain scheduler-owned.

Successful worker stages MAY defer final retirement through `Job::DeferCompletion(result, ticket)`. The record
retains its original sequence, cancellation source, job owner and complete-graph memory reservation. Failed and
cancelled stages skip this hook. Owner-thread `Resume` accepts only an exact suspended slot generation; duplicate,
reused and shutdown tickets fail. A resume requested inside a hook cannot start another worker until that hook
returns. A false hook finalizes once even if it requested resume. Cancellation while suspended waits for its
external owner's acknowledgement, then skips the next Run and delivers one final Cancelled callback. Shutdown
joins producers before destroying suspended payloads and drops callbacks.

`InAcknowledgedJobCallback(ticket)` identifies only the exact successful original stage while its owner
`DeferRetirement` or `DeferCompletion` callback runs after the producer returns. Progress, ordinary completion,
failed/cancelled hooks, wrong generations and shutdown cannot acquire that identity. It grants no document or
history mutation authority. A publication parent additionally needs its captured session/edit/history authority.

`RetainPublicationMemory` is restricted to that original successful `DeferCompletion` callback, a suspended
record with `deferredRetirement` and a descriptor-preclaimed retained-memory slot. It transfers the entire
remaining CPU reservation once, without allocation, a memory query or a partial refund. All useful producers
must have joined before this transfer; only bounded allocation-free cleanup may follow on the original record.
The existing ordinary-completion `RetainMemory` contract remains unchanged.

`JobOptions::continuingOutputLimit` is a separate explicit opt-in for one intermediate output from an
original resumable job. It requires deferred retirement, a retained-slot preclaim and a nonzero limit below
the whole descriptor admission. The descriptor MUST admit all future useful stages before source capture;
reservation growth is refused. `RetainContinuingOutputMemory` runs only in that exact successful, uncancelled,
joined DeferCompletion. It transfers only the declared completed output into the existing persistent endpoint,
leaves the original global charge, FIFO/ticket/cancellation identity and terminal slot intact, and supplies
later worker stages only the prepaid remainder. No new slot, allocation, memory query or early refund occurs.
Only that output seals; other temporary/final producers remain under the original record. Wrong, repeated,
over-limit and terminal/failed/cancelled/shutdown transfers refuse. Default descriptors and the terminal-only
RetainPublicationMemory/RetainMemory contracts remain unchanged. Accounting conveys no mutation authority.

Publication provenance MUST compare the original ticket and the live shared-memory endpoint. Wrapper addresses
and slot/generation numbers alone are insufficient after scheduler reconstruction. `OwnsRetainedMemoryEndpoint`
is an owner-only identity query: it allocates nothing, samples no memory and grants no callback or mutation authority.
Close removes the live endpoint, while older foreign graphs keep their independent charged endpoint until final release.

Jobs that lend mapped storage or retain an external receiver across a terminal worker result MUST opt in with
`JobOptions::deferredRetirement`. After the worker has returned (or its cancelled Run has been skipped), the
owner invokes `Job::DeferRetirement` for Finished, Failed and Cancelled results, before the success-only hook.
Returning true retains the original job, exact ticket generation, serial position and complete reservation
until its external owner acknowledges and resumes it. The job preserves its original terminal outcome across
that acknowledgement; cancellation still wins. Returning false finalizes normally and cannot dispatch a
requested resume. The default option and hook are disabled; existing failed/cancelled jobs do not acquire a
new continuation. Hooks never run during joined shutdown, which destroys wrappers before owner cleanup.

The dormant `GpuRasterUploadJob` is a leaf for one held raster layout and a precreated device destination.
It MUST refuse before lending rows if its scheduler record has not opted in. Preparation/reduction/copying
and CPU aggregate retirement belong to workers; queue recording, mapped-row acknowledgement and destination
retirement belong to the owner. Pending map and copy-fence receivers, skipped fill stages and early queue
completion retain their job/reservation until both producer and queue acknowledge. This leaf does not create
whole-document resources, generate mips, publish a display or authorize production activation.

One optional stable borrowed `OwnerWorkPump` shares completion draining with the production loop and `SettleJobs`.
Its pending work prevents idle settlement; only ready work requests an owner pass. Pump runs under the same
reentrancy guard as completion apply and MUST NOT wait, show a modal or recursively drain. Registration/removal
occur outside callbacks, and removal refuses pending or ready work. `NotifyOwnerWork` publishes and wakes only;
external producers are joined before scheduler Close. It creates no queue entry, allocation, worker or timer.
Device completion registrations use the scheduler's existing one-thread WaitPool through bounded standard-only
event tickets. Handles and contexts remain borrowed until owner teardown removes/joins them or Close joins the
pool. Register/remove inside callbacks and stale generation rearms/removals are refused.

`OwnerWorkMultiplexer` is a bounded standard-only router for the single owner-work source. It
borrows at most eleven distinct leaf pumps, routes one ready leaf per call with round-robin fairness, and refuses
recursive pumping or list mutation from a leaf callback. Pending-only leaves do not request a ready pass.
Detach refuses pending/ready leaves; their external producers must already be joined. This does not change the
scheduler's one-source registration contract. Application registers this router once and attaches GPU and
retry readiness sources. Processing startup attaches the dormant `UtilityReadbackOwnerService` after native-read
initialization, appending its checkpoint without changing the prior stage indices. The utility service is one bounded leaf;
the complete responsive startup described below can use all eleven. This promises no particular array slot
or activation of a utility parent. Stop the service
before producer shutdown, and release its joined charges/detach it only after actual scheduler Close returns.
Standalone GPU tests may retain direct registration.

`RetryWaitQueue` owns eight persistent manual-reset events registered on that same WaitPool. An admitted
suspended job owns its borrowed receiver and cancellation token until the wait callback acknowledges a
relative one-shot deadline or cancellation signal. Retry numbers 1–5 use 50/100/200/400/800 ms deadlines;
zero timeout retains the ordinary infinite event wait contract. The callback reads stable notification owners,
publishes readiness and never accesses request metadata afterward. Only the owner pump delivers a receipt and
releases the slot for reuse; it resumes the original ticket/reservation/FIFO position. Cancellation wakes the
owner and signals the existing event, without a stop-callback destructor or hot-path join. A signal failure
retains its native error and request until the original deadline acknowledges. Shutdown hides the window,
joins/disarms retry and GPU notifications, closes workers, then drops their receivers and detaches both sources.
This framework does not activate production clipboard transfers; F5b.2/F5c.2 retain those caller obligations.

The clipboard adapter is an explicit private platform exception to generic worker window access. Publishing
creates/closes/destroys its own built-in message-only window on the calling worker and never touches the
application window. It publishes fully rendered immediate formats. OS open/get/empty/close calls can invoke an
external clipboard owner and belong on a worker. The adapter does not schedule or retry work; production
admission, source/history ordering, event retries and caller activation remain F5b.2–F5d.

While open, inspection MUST enumerate and read only the first CF_DIBV5/CF_DIB representation, retaining the
producer's order. Windows lists synthesized formats after their source. Requesting an alternate representation
can invoke native conversion before validation; an invalid or over-budget first DIB MUST NOT trigger that request.
PNG remains preferred independently. Availability is a fixed record of the held source, published only after
successful close and cancellation validation, even when offered bytes are invalid. Malformed offered images
MUST NOT be treated as an empty clipboard for private fallback. See
[Microsoft's format enumeration contract](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-enumclipboardformats).

`ClipboardReceipt` is a standard-only fixed-packet codec with no allocation or platform ownership.
`ClipboardFingerprint` owns one CNG SHA-256 hash through WIL on its calling worker, using a borrowed algorithm
pseudo-handle and a checked 4096-byte stack object that outlives that hash. It never reads a live session or OS
clipboard. The transfer adapter keeps receipt bytes and image bytes inside the exact scoped source open, then
hashes owned copies after close. Fixed local key/size/digest values may cross to the owner; image payload hashing
belongs only to the worker. Production private-copy authority remains the selected clipboard graph's obligation.

Each record contains two aligned intrusive completion nodes, one for progress and one for final completion. Workers
publish through an SLIST and signal the scheduler's auto-reset event. The owner reverses each detached SLIST batch
to deliver its nodes FIFO. Progress is monotonic and published at most once per 100 ms. `PumpMessages`,
`DrainCompletions` and `RunUiActions` are shared host primitives: completions apply only on the outer loop, and at
most one UI action runs per outer-loop pass. Reentrant drain/action calls are ignored. The job event participates in
both the message-only wait and the frame-latency wait, so completions return to the outer pump while hidden or
suspended as well as while a frame is pending. `SettleJobs` is a harness/headless helper only, never a UI command.

The scheduler also admits one stable, borrowed owner-thread UI-action source. Registration and removal occur
outside callbacks; the object outlives registration through removal or Close. A ready request coalesces without
allocation or an ordinary action slot. Readiness participates in idle/work/wake accounting and is consumed before
the callback, so self-rearm schedules a later pass. When both kinds are ready, source and ordinary FIFO actions
alternate, starting with the source. RequestStop refuses new source requests and drops readiness; Close removes
the borrowed pointer without invoking or destroying the source. The host disarms while waiting for a prerequisite
and requests its source when that prerequisite completes. It MUST NOT rearm an unchanged waiting state.

`Executor`, `InlineExecutor`, `TileGrid`, `CancelSource`, `CancelToken` and `ParallelFor` remain standard C++ kernel
interfaces; kernel callers borrow an executor. No callbacks poll when idle.

The scheduler's optional observer publishes Submitted, Progress and Retired notices on its owner thread, outside
its lock. Submitted precedes worker dispatch; Retired follows the job's completion callback and payload release.
Shutdown drops notices with callbacks. Observers must not call the scheduler, pump, or wait. The context outlives
Close. `Application` uses these notices to update its fixed-capacity `JobActivity` and the delayed progress control.

An optional `JobExecutionObserver`, installed at scheduler initialization, publishes `BodyEntered`, `BodyExited`
and `BodySkipped` on the actual compute or recording worker, outside scheduler locks. Each notice carries the
original slot/generation ticket, class and lane, captured during locked worker selection. Callbacks borrow that
immutable identity outside the lock and do not inspect the guarded record array. Entry has no result; exit carries the raw `Run` result or the
runner's translated `bad_alloc`; a dispatched cancelled stage that never calls `Run` reports Cancelled/Skipped.
Body exit follows destruction of `Run` locals and precedes cancellation normalization, `RetireWorkerStage`,
completion publication and owner apply/drop. Resumed tickets may have multiple body pairs. Descriptors retired
without dispatch and queued work discarded by Close emit no body notice. A cancelled queued stage that is
dispatched still emits `BodySkipped`.

Execution callbacks are bounded, nonthrowing and safe for concurrent workers. They may write fixed atomic
samples and signal their own borrowed event; they MUST NOT call the scheduler, wait, pump, or access the UI or
document. Their context outlives joined Close, including final running-body notices during shutdown. Default
startup installs no callback, captures no clock, allocates no observation state and creates no diagnostic wake-up.
These notices acknowledge neither external native/GPU work nor final retirement and grant no apply authority.

UI-owned `PreviewMailbox`, `ContinuationQueue`, `HistoryWork` and `StrokeReplay` define input and apply ordering.
`HistoryWork` spans scheduler classes and keeps the document busy until apply/drop. Its shared input ordinals merge
the command queue with recorded strokes. The mailbox, barrier and application outlive all their workers; pointer
fields in stamps are opaque identities, not permission for workers to access UI state. See `Document_Tools.md`.
Each history acquisition has a distinct generation, including successive stages of the same selected input.
Only the exact latest finished acquisition can continue its ordinal, in the same document. Initial Begin retains
strict ordinal ordering. The host owns prerequisite outcome and document-lifetime validation; a finished barrier
stamp alone does not authorize dependent work.

`ContinuationQueue` offers synchronous owner-thread retirement callbacks for each superseded tail, stale
document entry and cancelled queued entry. The entry is detached, or its replacement installed, before callback
dispatch, and its input remains owned through that callback. Clear retires in input order, including ring wrap;
taken entries are the active owner's responsibility. Callbacks may move out the entry, but MUST NOT access the
same queue, dispatch UI, pump or wait. Receipt-owning callers MUST use retirement-aware overloads and explicitly
clear their queue before teardown; count-only forms do not publish results. Refused admission retains caller
ownership and invokes no retirement. The host publishes the corresponding outcome before releasing an observed
request. The queue neither stores a callback nor invokes one from its destructor.

`ContinuationReceipt` is an owning, copyable observer of one result (`Pending`, `Succeeded`, `Cancelled`, `Failed`
or `Stale`, plus a fixed-width error). `ContinuationCompletion` is its unique, movable publisher. Both observation
and publication stay on the UI owner thread; workers never access the result. Only the first terminal publication
is accepted. Dropping or overwriting a pending publisher publishes `Cancelled`; explicit stale/failure retirement
publishes its specific result first. Observers remain readable after request/queue/host ownership ends, without
borrowing a host, document or window. They do not pump, wait or dispatch callbacks. A pending receipt never
authorizes dependent work.

`HostRequest` owns a typed event-time input and publisher. Inputs include a whole ordered path batch, selected
write parameters, a full size-card draft, layer/tool parameters and panel/session-end lifetime keys. Inputs retain
no session/pixel snapshot, worker state, host pointer or callable. The selected runner obtains a snapshot only
after prerequisite history has applied/dropped. Request move-overwrite/destruction retires publication before
owned input storage releases. Production producers use this model; the staged runner's qualification is tracked
in `ColorCommitContinuations_2026-09-30.md`.

Budgeted host construction reserves declared retained storage before allocating the result publisher. An
allocation failure returns the reservation and leaves the input owned by its caller. The ticket moves with the
request across queued/active ownership and returns only after publication and input destruction. The budget is
an owner-thread member declared before queued/active requests so it outlives them. Payload access is const after construction except the original joined-stage path transfer. A pending
request may swap its admitted vector into an empty zero-capacity worker payload and restore that unchanged
vector to its original request after successful classification. The owner validates ordinal/session identity;
receipt and input-budget ticket remain owner-affine. Terminal or wrong-variant requests refuse transfer,
and payload destruction never returns the request's separate input charge. Borrowed path batches, write paths and layer names use the length/count preflight and copy
builders before any owned input copy. Terminated Win32 paths first use a bounded scan. The builders reserve the
complete declared copy peak before construction and reconcile it to retained capacities before publishing a
request; a partial input or result allocation failure releases storage and returns its reservation.
Length-query/fill sources use `TryReadPaths`: bound every count/length and reserve the whole batch before filling
its owned buffers. Readers are immediate, non-throwing callbacks, never retained with the request. Any read failure
drops the complete batch. File dialogs retain WIL-owned shell strings through preflight/copy and propagate read
errors instead of silently skipping selected files. Rename borrows the live field through its immediate callback.
Later selected output paths belong to the same request and budget. Selection is allowed only while the budgeted
request is pending; replacing an output holds the old and new charges until the replacement is complete. Failure
preserves the previous output. Request publication precedes destruction of both event input and selected output.
Other later-stage variable storage still requires separate admission before allocation. See
`Core_PerformanceAndResources.md` for the storage/count/length limits and production activation boundary.

`Application` now registers its stable continuation source during processing initialization and unregisters it
before scheduler stop/join. It admits typed production requests through an owning construction
boundary. It selects one owned
entry outside message/control/completion dispatch, validates document serial and panel opening, and starts the
immutable commit under that entry's ordinal. Waiting consumes source readiness; exact acquisition completion
stores the prerequisite outcome/error and requests a later UI action. Apply/drop finishes history before terminal
receipt publication. Every asynchronous history finisher MUST call the common `ResumeHostHistory` hook after
apply/drop and `HistoryWork::Finish`; it wakes queued inputs even when that earlier operation belongs to another
kind. Color, utility Merge/adjustment-opening and size finishers use this hook; remaining migrations must wire it at their own
apply/drop boundaries. Cancel retires queued inputs immediately and retains active ownership/barrier through acknowledged drop.
After a panel commit has acknowledged its terminal result, a later Cancel preserves that result and its already
applied pixels/history. It cannot relabel Succeeded or a known Failed as Cancelled during deferred publication.
Cancel of a multi-stage request suppresses its remaining write/replacement/close stages; successfully applied
prerequisite pixels remain committed. Discard confirmation precedes settlement, output selection precedes pixels
and readback, and the written document/revision is captured only after successful settlement. A path batch alone
may rebind its session serial after its own successful project replacement; younger inputs retain their old serial.
Size Apply owns its complete event-time draft and card invocation. Session end owns a separate request generation;
`WM_ENDSESSION(FALSE)` invalidates a late close even when the color worker completed successfully.
Shutdown retires requests and unregisters the borrowed source before joining their independently owned worker
inputs. Size-card preparation is an immutable Commit job: copied options and a full document token stay owned
through cancellation acknowledgement and checked owner apply/drop. Its selected request continues an earlier
panel commit's ordinal and retains the shared history barrier until completion. Merge and adjustment-opening
readback use the separate utility composition/resumable worker graph. Remaining GPU callers, IO and residual
canvas/history work still migrate in later P1 slices. Current staged writes
and imports do not establish whole-application responsiveness.

These foundations do not migrate existing image operations to asynchronous execution. Memory admission is
specified below; operation-specific input recording, snapshot preparation and worker/apply wiring remain P1 work tracked in
`UiNeverBlocks_2026-09-22.md`.

`WorkerPool::Initialize` creates a private pool with equal minimum and maximum thread counts (logical processor
count by default), a cleanup group, one reusable work object for each of HIGH, NORMAL and LOW priority, and one
preallocated final retirement work object outside that cleanup group. A
dispatch callback carries only its owner/lane, never a job payload. Each lane coalesces outstanding wakeups at the
thread count; `JobScheduler` drains its bounded records and owns job admission and state. No callbacks poll when idle.

The scheduler opts its compute and optional recording pools into `WorkerWake`, a payload-free owner endpoint.
Startup checks creation of one auto-reset event and one reusable one-shot wait per opted-in pool. Three inline,
lock-free 32-bit counters coalesce hints at the pool's existing thread count (1–1,024); each owner request performs
at most two strong CAS operations and signals the event, including at saturation. The owner publishes its real
queue state with the queue's existing synchronization before the hint. The counters do not publish work. Request
acquires no SRW lock, allocates no storage, submits no work callback and joins nothing. Its Win32 signal still
requires measured owner-latency qualification.

The wait borrows the existing compute/recording pool; it creates no second pool or thread. Its callback takes
the bounded hint counts and submits through the existing priority lanes off owner. It installs the worker
terminate handler before accessing state; actual dispatch retains the existing TLS, MTA and QoS scope. Rearm
is serialized with Close. Opted-in Initialize/Close run on that endpoint's owner; foreign producers retain the
original thread-safe Wake route, and default pools retain their existing serialized foreign-Close behavior.
Startup failures propagate the original HRESULT and unwind before publication. A live signal failure after
accepted work is published is an invariant failure: `std::terminate` invokes the installed crash handler rather
than silently stranding that work. Closed/invalid-priority requests refuse before publishing a hint.

Parallel execution dynamically claims disjoint tiles; the caller participates. A call owns at most one helper
work object, whose destruction cancels unstarted callbacks and joins running callbacks. The work object is outside
the dispatch cleanup group to prevent shutdown racing its local owner. No callback is disassociated. This join
retains the caller's frame without a separate shared allocation. Failure to allocate an optional helper falls
back to the caller. Tile bodies allocate nothing and never wait on jobs, I/O, or memory admission; nested
`ParallelFor` uses the same cancel-before-join rule. The UI thread MUST NOT call this synchronous join.

`WorkerPool::Close` requests cancellation, stops dispatch admission, cancels queued dispatch callbacks and joins running ones.
It also waits on a condition variable for external parallel callers to release their helpers. Its optional owner
cleanup hook then drops owner-affine wrappers. The final work object acknowledges non-cancellable CPU retirement
on the existing MTA pool before releasing work ownership, cleanup group, private pool and COM cookie. No allocation
or new thread is needed during Close. Close is idempotent and runs on the owning teardown thread,
never inside a callback. Initialization precedes publication; closed pools cannot restart. Cancellation does not
forcibly interrupt a body: it checks its token per row or a measured smaller interval.

For an opted-in endpoint, Close disarms and joins its wait before closing dispatch members or releasing the
borrowed context. The event, counters and callback storage survive that join. This adds no job payload, ticket,
queued release record or per-request allocation. Large source/private/cache phase admission and whole-editor
activation remain separately tracked P1 obligations.

Each pool owns a `CoIncrementMTAUsage` cookie through all of its callbacks. These cookies leave an initialized STA
caller in its original apartment and make worker COM use independent of the UI apartment. Callbacks install the
process's per-thread terminate handler and set HighQoS for user work or EcoQoS for background work, then restore
system-managed power policy on return. I/O handle priority belongs to the eventual I/O job.

`WaitPool` callbacks only publish completion/event state. Registrations retain borrowed handles/contexts until
owner-thread `Remove` or `Close` has disarmed and joined the wait. Slot generation tickets reject stale rearm and
remove requests. Add/rearm are synchronized with close; remove/close are serialized owner-thread teardown and
must never be called from the callback. The wait pool remains runnable even when every compute worker is busy.

`Application::Shutdown` requests job cancellation, hides the window, then closes and joins the scheduler before
releasing session inputs, chrome, renderer/device resources, the window, localization satellite or STA COM lifetime.
Shutdown drops pending completion callbacks and UI actions after producers have joined. Calls made during a job UI
callback defer teardown until the callback has returned.

Processing startup acquires trace registration, scheduler, owner routing, GPU wait slots, retry wait slots,
host continuation source, deferred-release binding, recycling, the native header-read event, the dormant
utility owner service and the shared command pump/recording lane in that order. `Main` calls Shutdown after
an initialization failure as well as after success. Cleanup must stop/join acquired producers, detach borrowed
routes and clear bindings before releasing their owners; a second Shutdown repeats no retirement hooks.
Harness-only borrowed startup checkpoints run after each successful stage; retry initialization reuses the
queue's event/registration observer seams. Their contexts outlive initialization and acknowledged shutdown.
Default startup supplies no observer. These controls qualify acquired-prefix cleanup, not failures inside
unintercepted scheduler/pool/GPU/allocation APIs. A prefix with no acquired scheduler runs no final worker hook.

The explicit complete responsive-source startup additionally acquires prepared layer rows, prepared display,
stroke continuation, selection outline and nested lasso draft display in that order. Its owner-routing counts
are 6 initially, 7 after rows, 9 after display, 10 after stroke and 11 after selection; lasso reuses that existing
owner. Stroke Finish, stroke sampling and live Warp producer flags remain disabled until the final Ready stage
succeeds. Failure or shutdown at any acquired prefix uses joined, idempotent Shutdown before owner release.
The main input loop and startup-path dispatch follow successful acquisition. Default activation remains
subject to complete consumer admission, lifetime and measured performance qualification.

The scheduler's owner grants memory reservations before dispatch and holds them through UI application and
payload destruction. Waiting admission uses records, never blocked workers; memory events and retirements retry
from an OS snapshot. An independent intrusive release queue can return memory even at the scheduler record cap.
`Raster` and `PixelBuffer` own their release nodes. A thread-local sink binds only the application owner to the
queue. Close joins producers while retaining the sink, then drops owner-affine job/action wrappers outside locks
so their admitted CPU aggregates enqueue retirement. An explicit native header read retains its file, buffer,
OVERLAPPED and stable event while its compute/recorder callbacks join. Its final worker cancels and
acknowledges that existing OS operation before freeing storage; a stop token or readiness callback is not
completion. Ordinary stages use only nonblocking acknowledgement and yield the compute lane. No wall-clock
bound is claimed for a driver that cannot complete cancellation. Borrowed `CloseHooks` may detach external owner resources
after wrapper destruction while the sink remains installed. Close clears the binding before the final MTA hook,
which runs the external CPU retirement hook before deferred/recycle drain. Hooks cannot submit, pump, wait or reenter Close; their context outlives acknowledgement. After those
hooks return, the final worker release drain may acknowledge an already-issued native read before
destroying its OS-owned storage. No useful work or new native producer is allowed in that drain. Uninitialized or repeated Close runs neither hook.
Application detaches session coverage engines, brush/composition resources and joined GPU/retry receivers on the
owner, then releases document/history/display/draft/private-clipboard CPU roots on the final worker. Application's
sampling raster, cached selection geometry and benchmark document/image roots also retire on that worker.
The benchmark object, timing address, result metadata and known-path cleanup remain alive for callers after Run.
Harness observations of live brush/Warp ownership hold only weak handles. They observe the same draft at joined
owner detachment and its expiration inside final MTA retirement; observation cannot keep the CPU graph alive.
Borrowed engines detach on owner before that destruction. Temporary history observations end while the session
still owns its roots, so a test cannot substitute its own retained history graph for the application's lifetime.
A borrowed harness callback may observe joined wrappers before queue cleanup, without pumping, submitting or
waiting. It proves mapped-row acknowledgement before GPU fallback cleanup and native retry callback join
before receiver teardown. Row observations are borrowed POD values; only the worker reads them while its
lease is live, and no test reads mapped bytes after producer return or owner acknowledgement.
Reservations, events and the pool apartment remain alive through acknowledgement. Empty selected/collapsed hash bucket capacity
remains until host destruction; broader initialization/lease/draft coverage and shutdown timing remain separate
qualification. The process recycle pool holds only unowned pixel
allocations, so detaching it cannot invalidate live images. Low/high physical-memory events use one-shot waits on
the separate wait pool. Exact budgets, cache caps and trace gates live in `Core_PerformanceAndResources.md`.

Independent state-machine expectations (no legacy counterpart) are: each tile/pixel is visited exactly once;
inline, 1-, 2- and 4-worker outputs equal a serial coordinate formula; N jobs on N workers and nested execution
complete; a cancelled row publishes no partial success; close retains active inputs until callbacks return; wait
tickets reject reuse; the 33rd registration fails locally. HostTests owns these checks with event gates and bounded
failure waits. Existing image goldens remain unchanged.

## Frame loop

`Application::Run` calls the shared message pump, completion drain and one UI action, then selects the next
frame action (`RedPrism/FrameScheduler.h`). The first matching row wins:

| State | Action |
| --- | --- |
| Completions or UI actions pending | Return to the outer completion/action pump, before visibility checks |
| Hidden, display-off, or swap chain suspended (zero client size) | Block on messages, job wake and settings watch |
| Occluded, no occlusion-status message | Block on messages, job wake and settings watch |
| Occluded, occlusion-status message received | `Present(0, DXGI_PRESENT_TEST)` then re-evaluate |
| Visible, powered, not occluded, not suspended, frame invalidated | Wait on frame latency, messages, job wake and settings watch; only frame latency proceeds to `Renderer::Render` |
| Visible and settled (not invalidated) | Block on messages, job wake and settings watch |

`Renderer::Render` is the only Present path. `WM_PAINT` MAY set the invalidation bit and MUST `ValidateRect`; it MUST
NOT call `Present`, `BeginPaint` text, or GDI fill.

The bounded `--bench ui-smoke` correctness harness treats display power as on for frame selection and surface
retention, while preserving the observed power state in diagnostics. It still honors visibility, suspension and
occlusion. Full benchmark modes use the ordinary frame policy and refuse unavailable-presentation measurements.

Frame construction and the shared message/completion/action primitives expose optional allocation-free timing
scopes (`Common/UiTiming.*`). The owner may install a scoped fixed-histogram sink; ETW may independently enable
the owner-thread provider. Neither owns work or changes event waits. Scope boundaries and inclusive-time caveats
are defined in [`Core_PerformanceAndResources.md`](Core_PerformanceAndResources.md#ui-duration-instrumentation).

## Device placement

Hardware device creation uses adapter-of-monitor (`RedPrism/AdapterSelection.h`): the first non-software DXGI adapter
that owns an output whose `HMONITOR` is the window's monitor. `--warp` and `--self-test` force WARP and skip that
search. If no matching output exists, creation MAY use `D3D_DRIVER_TYPE_HARDWARE`. Hardware init failure MAY fall back
to WARP unless `--warp` was already requested and failed.

When the window moves to a monitor scanned out by another adapter, `Renderer::EnsureDeviceForWindowMonitor` rebuilds
device resources. WARP devices do not follow monitors. Device loss (`RecoverDevice`) waits out an outstanding
Present, gives chrome the new device before creating the replacement swap chain, and still presents if occlusion
registration is refused. The WARP self-test calls `RecoverDeviceForTest` and requires a later frame to present.

## Command catalog

`RedPrism/CommandLine.h` is the single declaration of switches. Help, parse, and tests MUST read that table. Adding,
renaming, or removing a switch changes the header, [`UI/UI_Windowing.md`](../UI/UI_Windowing.md), and `HostTests` in
the same change.

## Document and chrome

`ApplicationFileJobs.cpp` owns the nonmutating Open-batch classification parent. One original admitted
Io ticket, FIFO position, ordinal, session identity and complete memory charge span allocation, joined
path transfer, every four-byte header read and worker payload retirement. Workers access only their private
payload and fixed result. `Common/PathClassificationRead.*` owns private Win32/WIL IO behind a standard-only
header; `PathReadWait` owns one startup event/registration on the shared WaitPool and ninth owner route.
An issued operation consumes registered readiness even on immediate completion, API refusal or cancellation before
issue. Its nonzero 64-bit identity advances only for a new operation. An incomplete-read rearm retains that
identity and exact original ticket, without resetting the event. A new read refuses arithmetic overflow before
reset or issue; the final valid read can still rearm and acknowledge without wrap. Awaiting starts only after the original worker returns. A premature event resets and nonblocking
rechecks the same operation before rearm, preserving a racing native completion. Cancellation requests
exact CancelIoEx on the worker; it never signals hEvent to fake completion. GetOverlappedResult acknowledges
the original operation before reuse/free. Shutdown disarms/joins wait callbacks, detaches borrowed owner
wrappers while retaining the event, and closes it only after final worker native acknowledgement and
scheduler Close. The legacy blocking `PathClassificationWork` helper remains a reference test surface.
Successful classification restores the unchanged path allocation to the same selected owner request for
ordered project/image routing; stale/failure/cancel paths publish no classification success.

C4 owns `CanvasDocument` / `ImageLayer` / `DocumentHistory` as C++ value types with shared immutable `Raster`
storage. `Application` owns `CanvasRenderer` (borrowed device). DxUi `EmbeddedHost` composites chrome into the same
back buffer; the composition then blits the document into the chrome hole. Those types MUST NOT own the swap chain.

## Validation

`Tests/HostTests` covers adapter selection, frame-action selection, and the command catalog without creating a device.
`Tests/DocumentTests` covers the document graph, validation, and history. `Tests/IoTests` covers `.prism` load/save.
`Tests/CompositionTests` covers WARP blend goldens. `RedPrism.exe --self-test --warp` covers HWND + WARP Present. See
[`Testing/Testing_Validation.md`](../Testing/Testing_Validation.md).

### Private bounded codec output stream

`Common/CodecMemoryStream.*` owns one reserved virtual buffer through WIL and provides the capped WIC IStream
implementation. Cursor/data access is confined to one caller; only COM reference counting is concurrent. It owns
no document, HWND, scheduler or GPU device. Admitted clipboard encoding uses it on the worker. A borrowed completed-byte view is retained only through exact
worker copying, never exposed as a result. The final public result is an owning vector.

### Ordered clipboard host graphs

`ApplicationClipboardJobs.cpp` owns selected Copy/Cut/Paste/Copy Merged jobs and independent New Canvas metadata
suggestions. Fixed descriptors precede complete admission; source document/history/pixel owners are captured
only afterwards. Workers prepare core publication, capped encoding, global blocks, receipts and owned decode.
Owner stages validate authority and apply complete publication; they perform no OS clipboard transfer, codec
work, nested completion drain or wait. An acknowledged one-shot retry retains the same ticket and reservation.
Each command allows at most six clipboard acquisitions; Paste shares that total between Inspect and Copy.

Copy Merged holds its opening whole-document authority across utility tiles, releases maps and utility targets,
then runs the CPU/private-copy/encoding/transfer stages under the same scheduler record and reservation. A device
change after owned CPU bytes exist cannot recompose or replay the transfer. Large captured/replaced CPU owners
retire as one aggregate off owner. Session shutdown keeps receivers, wait slots, windows and borrowed OS-call
state alive through worker acknowledgement; an external delayed renderer cannot be interrupted by a stop token.
The normal final join happens after the application hides its confirmed-close window.

### Layer-copy host graph

`ApplicationLayerCopyJobs.cpp` owns the ordered Layer via Copy Commit job. It reserves the complete scheduler-memory
graph before owner-thread capture of the source document, active/mask target, displayed placement and history;
worker stages prepare selected pixels/origin or the complete duplicate, after-document and prepared history. Owner
completion validates the captured host/session/history authority and publishes atomically or drops the result.
Cancellation and staleness retain the record, reservation and payload until acknowledgement, after which the
aggregate CPU owners retire off owner. This graph neither reads nor writes the private clipboard and makes no OS
clipboard calls. Live producer settlement and H2/G1 qualification remain open; see `Document_Tools.md` and
`Core_PerformanceAndResources.md`.

## Recorded utility parent

`RecordedDocumentReadbackJob` is a separate utility parent. Its construction descriptor contains the
complete admission envelope and original GPU charge; it captures immutable source owners only after its
original Compute opening acknowledges. One scheduler record, ticket, retained-slot preclaim and CPU
reservation span private recording, serial region copies, CPU processing, terminal adoption and retirement.
Clone All Layers uses this parent and transfers its original shared accounting into the live sample.
PNG/JPEG export uses the same parent: descriptor-only admission precedes source capture, the actual
private graph retires before detached encoding, and the original accounting covers encoding and aggregate
retirement. The eligible canvas sampler keeps its private excluded graph between real 1×1 inputs, with no
command-source slot or mapped row while idle, and resumes through the original parent acknowledgement.
New-mode All Layers Wand prepares its selection/history on Compute after acknowledged graphics retirement.
Both retain the original shared accounting through payload retirement. Adjustment opening retains its
below-composite through the panel and all copied/weak readers. Merge retains its independently charged
metadata probe through the separately admitted dependent readback; a newly allocated crop explicitly
owns that readback charge. Copy Merged resumes private preparation, encoding, OS offers and waits under
the same original ticket. Its private pixels carry a precreated opaque output lifetime while useful
work remains reserved; terminal or joined Close binds it before original refund. No early accounting
transfer authorizes further useful work. Complete configuration/lifetime and activated timing gates
remain P1 work.

The worker creates `SharedPublicationMemory` before the intrusive root. The root owns source/output bytes,
the private graph, command stream and captured contexts; it is also the queue's receiver/producer. Thus
actual joined shutdown may destroy the wrapper before the queue closes without invalidating borrowed
receiver storage. Stop notifications and join producers before detaching the wrapper's root pointer;
close queue/map owners before the final MTA destruction drain. Accounting is declared before payload owners
and dies last, including every caller adoption aggregate and weak accounting owner.

`InJoinedRetirementCallback(original)` grants authority only on the scheduler owner during that exact
original job's actual joined Close callback. It refuses ordinary acknowledgements, another ticket/thread
and post-Close queries. `AcknowledgeSourceJoinedShutdownOwner` additionally requires the stopped common
pump and exact source lease. It excludes the borrowed source before wrapper destruction and grants no
useful completion, new work, row/copy acknowledgement or reservation refund.

An explicit CPU-only capture descriptor requires `S_FALSE` and no GPU charge; CPU output is produced on
Compute. For detached CPU/event continuations, callers use `ResumeDetachedReadback(original)` or the
sampler's `ContinuePixelReadback`, preserving suspended readiness on refusal. Owner cancellation never
resumes an executing CPU body. Serial source-capacity waits and healthy GPU quarantine remain event driven.

## Selected Hue and panel-preserving Wand inputs

The explicit asynchronous Hue route queues a scalar descriptor before retaining source/pixel/GPU owners.
Invalid purpose/mode refuses before queue, opening and shared ordinal effects; every accepted descriptor
consumes its opening before binding, even if binding later refuses. The original admitted readback parent
captures the committed document, reads one pixel and derives hue on its worker. It never samples a panel or
stroke preview. Apply checks document-read authority, exact panel opening and current color sequence.

The fixed 512-event input retains physical delta, Ctrl choice and shared ordinal. Release/loss owns a separate
terminal and applies no new release coordinate. One existing continuation applies at most one event per turn.
Finishing the acquisition request must rearm acquired Hue with a pending prefix or clock-exhaustion closure;
an idle live target has no polling source or dummy worker. Younger accepted commands close the older gesture
at their physical boundary. Overflow retains the accepted prefix and Lost terminal; ordinal exhaustion never
wraps. Stale input cleanup retires only the matching opening. Explicit Cancel retains its cancellation outcome
through original acknowledgement, including after its panel card hides.

Preserving-panel Wand binds its scalar opening at the physical input and its complete source witness after
older FIFO work. It does not change global history-restore authority. Its original job owns capture, optional
recorded composite/readback, worker geometry/history, publication and complete retirement. These selected
routes do not authorize ordinary prepared startup or replace the remaining admission, device, lifetime and
G1 gates.
