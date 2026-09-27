# RedPrism performance and resource contract

Status: current normative product contract
Last reviewed: 2026-09-27
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
  `ResizeBuffers` to 0×0 as a way to keep presenting. Hidden documents MUST drop canvas GPU surfaces
  (`CanvasRenderer::CanvasSurfaceBytes() == 0`).
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

These numbers are binding as soon as the feature exists. C1 does not implement them; it MUST NOT choose a host shape
that makes them impossible.

| Budget | Limit |
| --- | --- |
| History retained bytes (C4) | ≤ 256 MiB, 100 entries, shared immutable rasters |
| Document pixels | 30 000 px/side; 100 M image pixels + 100 M mask pixels; ≤ 10 000 layers |
| Manifest / asset | 4 MiB manifest; 512 MiB per asset |
| Visible DxUi surfaces (C2) | Library cap (64 MiB visible, 0 when hidden) |
| Hidden document GPU (C6) | 0 canvas surface bytes; no Prepare/Composite |
| Composition (C6) | Only when the document changed or surfaces were rebuilt; a settled document composites 0 times per frame (`--self-test` proves it) |
| GPU raster residency (C6) | Bounded to the rasters the last composition used; entries keep their `Raster` alive so identities never alias |
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

`JobActivity` uses 128 fixed records and updates only on scheduler notices. The oldest foreground operation owns
the global progress indicator; Background, Refinement and InteractivePreview do not. A single deadline timer shows
the indicator after 250 ms, is killed before dispatch, and is removed at completion/shutdown. It never polls job
state. Hidden progress values do not repaint chrome. Visible indeterminate progress uses the existing frame-paced
DxUi animation; hidden, suspended and occluded frame scheduling still waits on events.

This foundation does not mean existing image operations run asynchronously. P0.7 owns memory admission and
related resource policy; P1 owns migration of blocking operations away from the UI thread and input replay storage.

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

## Validation

`HostTests` asserts idle vs render vs occlusion-probe selection. `RedPrism.exe --self-test --warp` presents exactly one
hidden frame and exits. Interactive idle is qualified by running the titled window: a settled visible window MUST not
spin the CPU in `Renderer::Render`.
