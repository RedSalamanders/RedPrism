# GPU completion and asynchronous readback

## Implementation checklist

- [ ] P1.3a — Add a qualified device capability report and fence/EnqueueSetEvent route selection.
- [ ] P1.3b — Add resumable job stages that retain admission, ownership, FIFO and cancellation through GPU waits.
- [ ] P1.3b — Add an owner pump shared by the production loop and SettleJobs; distinguish ready work from waiting work.
- [ ] P1.3c — Add the bounded readback ring, one-shot event waits, UI mapping after completion and worker copying.
- [ ] P1.3c — Retain mapped slots through device retirement and shutdown until the copying worker has joined.
- [ ] P1.3d — Add separate off-screen utility composition, tiled full-image readback and region/1-pixel sampling.
- [ ] P1.3e — Migrate merge, opening adjustment composites, export/Copy Merged and wand/clone/sampler callers.
- [ ] P1.3f — Prove hidden completion, cancellation, stale generation, device loss, saturation and shutdown with owned event gates.
- [ ] P1.3f — Run all configuration gates, record real-loop measurements, reconcile domain specs and commit/push.
- [ ] Move this plan to Done after all P1.3 callers and validation are complete.

Status: ACTIVE. Parent: [UiNeverBlocks_2026-09-22.md](UiNeverBlocks_2026-09-22.md), P1.3.
Owners: `Application`, `JobScheduler`, `GpuWorkQueue`, `GpuCapabilities`, utility composition and their tests.
P1.2 qualification/commit precedes integration of this slice into the primary checkout.

## Ownership and execution

The UI thread records D3D commands and owns all immediate-context calls. Workers prepare immutable inputs and
copy mapped rows into adopted PixelBuffers; they never Map, Unmap, Flush, compose, mutate a document or touch
Application state. A utility request owns its document snapshot/region, operation stamp, cancellation and device
generation. Display canvas resources remain separate from utility targets. Hidden windows release display
surfaces and can complete accepted utility work without Present.

A multi-stage job retains its scheduler record, reservation and FIFO position between worker stages. An owner
completion hook can defer final retirement while GPU work is pending. Resuming uses the exact live ticket; reused
slot generations, duplicate resumes and resumes after shutdown are refused. Cancellation suppresses later work,
but an accepted operation's history receipt remains pending until final apply/drop. An error at any stage releases
every acquired owner exactly once and never publishes partial pixels.

The scheduler interface is a default-false `Job::DeferCompletion(result, ticket)` hook before final Completed,
plus owner-thread `Resume(ticket)`. Existing jobs keep their one-stage behavior. A deferred record enters a distinct
suspended state before invoking the hook, so callback-driven resume can validate the exact state. A false hook
falls through to the existing final apply path. Suspended records count against capacity and retain the original
reservation/FIFO sequence; resume does not allocate a new record or reserve the graph twice. Cancelled/failing
stages cannot defer another computation. Shutdown joins producers and drops callbacks before destroying records.

`OwnerWorkPump` exposes `HasPending`, `HasReady` and `Pump`. Register one stable borrowed instance at startup;
unregister only after its pending work retires. `NotifyOwnerWork` is safe for joined external event callbacks and
publishes readiness before waking the shared scheduler event. The pump runs outside message dispatch with the
same reentrancy guard as result apply; it cannot show a modal, wait or recursively drain. GPU wait callbacks never
call `Resume` themselves.

The owner pump participates in both the real loop and SettleJobs. Ready work can run; in-flight work keeps settle
from returning idle but does not make HasUiWork true or cause a busy loop. GPU event callbacks publish only an
atomic readiness flag and wake the scheduler. They cannot invoke a job, context method, modal or host continuation.
Wait registrations and their borrowed contexts remain alive until callbacks are joined.

## Completion and staging

Probe optional interfaces/features explicitly. Record monitored-fence interface/adapter availability, options2,
driver concurrent creates, required typed UAV stores, minimum precision and memory budgets. Unsupported optional
queries are reported as unavailable. Prefer supported monitored fences; otherwise use IDXGIDevice2 EnqueueSetEvent.
Do not infer capabilities from a hardware name or a feature level.

After recording a bounded batch, arm its completion event and submit/flush as the chosen API requires. EnqueueSetEvent
already flushes outstanding commands and signals after completion or device removal; do not add a redundant Flush.
Fence registration uses a real event handle, never the blocking null-handle variant. Only after
completion may the UI Map staging with DO_NOT_WAIT. WAS_STILL_DRAWING is an operation failure and diagnostic,
never a polling retry. The worker receives only a read-only pointer, row pitch and bounds backed by an owning slot.
Its completed stage returns to the UI to Unmap before the slot can be reused. Large reads traverse tiles under the
64 MiB total staging cap; a 1-pixel sampler does not read back a full document. No slot is reused while lent.

Device replacement invalidates recording on the old context. Lent slots keep their mapped context/texture in a
retiring state until the copying worker completes; a new generation cannot borrow their storage. The staging cap
includes retiring storage. Requests retain CPU inputs for a restart; after two losses use a correct CPU engine
where one exists, otherwise fail locally with the document intact. Shutdown stops event producers, joins workers,
then unmaps/releases remaining retiring slots. Cancellation of an event wait does not prove a mapped pointer is no
longer in use.

## Validation

Use state-machine expectations for resumable jobs, readiness/idle distinction, FIFO, memory refusal, duplicate
tickets, cancellation and shutdown. Use WARP numeric goldens for utility composites and cropped/tiled readbacks;
compare with the existing committed synchronous reference without changing its goldens. Own event gates that hold
GPU/copy completion and force generation replacement, hidden execution, queued commands and shutdown. Verify
zero Present while hidden, zero polling, storage/slot bounds and exactly one final receipt on every outcome.
Record Release dispatch/apply distributions separately from fence/copy/end-to-end time; timing is not asserted in
ordinary tests. ARM64 cross-builds do not establish native runtime qualification.

Progress: the P1.2 source is frozen for its matrix. The bounded capability-probe sidecar has been prepared and
reviewed against SDK 10.0.26100, with integration/runtime tests pending. An independent bounded stage-test sidecar
is being prepared in the isolated checkout. Scheduler stage ownership and queue/readback integration remain
primary-agent work.

API references checked for this slice: [EnqueueSetEvent](https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_2/nf-dxgi1_2-idxgidevice2-enqueuesetevent),
[fence event completion](https://learn.microsoft.com/en-us/windows/win32/api/d3d11_3/nf-d3d11_3-id3d11fence-seteventoncompletion),
and [Map error/DO_NOT_WAIT behavior](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-map).
