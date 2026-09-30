# RedPrism architecture ownership

Status: current normative product contract
Last reviewed: 2026-09-30
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
| `Kernels` (`Common/Kernels.*`) | Immutable process-lifetime CPU function tables and CPU/OS feature detection; noinline tier implementations in separate translation units | Document/session state, HWND, GPU context, scheduler |
| `WorkerPool` (`Common/WorkerPool.*`) | Private fixed-size Windows pool, bounded payload-free dispatch wakeups, callback cleanup group, implicit MTA lifetime, cancellation and parallel helpers | HWND, document mutation, GPU context, a second scheduler |
| `WaitPool` (`Common/WorkerPool.*`) | Dedicated one-thread pool and at most 32 one-shot event waits | Pixel work, waits inside callbacks, document mutation |
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

One optional stable borrowed `OwnerWorkPump` shares completion draining with the production loop and `SettleJobs`.
Its pending work prevents idle settlement; only ready work requests an owner pass. Pump runs under the same
reentrancy guard as completion apply and MUST NOT wait, show a modal or recursively drain. Registration/removal
occur outside callbacks, and removal refuses pending or ready work. `NotifyOwnerWork` publishes and wakes only;
external producers are joined before scheduler Close. It creates no queue entry, allocation, worker or timer.
Device completion registrations use the scheduler's existing one-thread WaitPool through bounded standard-only
event tickets. Handles and contexts remain borrowed until owner teardown removes/joins them or Close joins the
pool. Register/remove inside callbacks and stale generation rearms/removals are refused.

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
an owner-thread member declared before queued/active requests so it outlives them. Payload access is const after
construction. Borrowed path batches, write paths and layer names use the length/count preflight and copy
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
count by default), a cleanup group and one reusable work object for each of HIGH, NORMAL and LOW priority. A
dispatch callback carries only its owner/lane, never a job payload. Each lane coalesces outstanding wakeups at the
thread count; `JobScheduler` drains its bounded records and owns job admission and state. No callbacks poll when idle.

Parallel execution dynamically claims disjoint tiles; the caller participates. A call owns at most one helper
work object, whose destruction cancels unstarted callbacks and joins running callbacks. The work object is outside
the dispatch cleanup group to prevent shutdown racing its local owner. No callback is disassociated. This join
retains the caller's frame without a separate shared allocation. Failure to allocate an optional helper falls
back to the caller. Tile bodies allocate nothing and never wait on jobs, I/O, or memory admission; nested
`ParallelFor` uses the same cancel-before-join rule. The UI thread MUST NOT call this synchronous join.

`WorkerPool::Close` requests cancellation, stops dispatch admission, cancels queued dispatch callbacks and joins running ones.
It also waits on a condition variable for external parallel callers to release their helpers. Only then are the
cleanup group, private pool and COM cookie released. Close is idempotent and runs on the owning teardown thread,
never inside a callback. Initialization precedes publication; closed pools cannot restart. Cancellation does not
forcibly interrupt a body: it checks its token per row or a measured smaller interval.

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

The scheduler's owner grants memory reservations before dispatch and holds them through UI application and
payload destruction. Waiting admission uses records, never blocked workers; memory events and retirements retry
from an OS snapshot. An independent intrusive release queue can return memory even at the scheduler record cap.
`Raster` and `PixelBuffer` own their release nodes. A thread-local sink binds only the application owner to the
queue; Close clears that binding before joining/draining. The process recycle pool holds only unowned pixel
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

C4 owns `CanvasDocument` / `ImageLayer` / `DocumentHistory` as C++ value types with shared immutable `Raster`
storage. `Application` owns `CanvasRenderer` (borrowed device). DxUi `EmbeddedHost` composites chrome into the same
back buffer; the composition then blits the document into the chrome hole. Those types MUST NOT own the swap chain.

## Validation

`Tests/HostTests` covers adapter selection, frame-action selection, and the command catalog without creating a device.
`Tests/DocumentTests` covers the document graph, validation, and history. `Tests/IoTests` covers `.prism` load/save.
`Tests/CompositionTests` covers WARP blend goldens. `RedPrism.exe --self-test --warp` covers HWND + WARP Present. See
[`Testing/Testing_Validation.md`](../Testing/Testing_Validation.md).
