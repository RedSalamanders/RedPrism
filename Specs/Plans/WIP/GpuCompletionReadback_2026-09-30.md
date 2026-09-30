# GPU completion and asynchronous readback

## Implementation checklist

- [x] P1.3a — Add a qualified device capability report and fence/EnqueueSetEvent route selection.
  - [x] Implement optional-query HRESULT/availability, context/device identity validation and conservative route selection.
  - [x] Review and integrate the cheaper agent's capability truth table and WARP test sidecar.
- [x] P1.3b — Add resumable job stages that retain admission, ownership, FIFO and cancellation through GPU waits.
  - [x] Implement suspension/resume with delayed promotion after hooks and retained graph/FIFO ownership.
  - [x] Review stage tests, remove a blocking test hook and correct failed/younger-stage expectations; initial Debug HostTests pass.
- [x] P1.3b — Add an owner pump shared by the production loop and SettleJobs; distinguish ready work from waiting work.
- [x] P1.3c — Add the bounded readback ring, one-shot event waits, UI mapping after completion and worker copying.
  - [x] Implement eight slots, two in-flight copies, power-of-two staging classes, 64 MiB texel accounting and capacity waits.
  - [x] Share the scheduler's existing event wait pool; no second wait pool/thread.
  - [x] Qualify cropped/single-pixel copies, map cancellation, rebind retention and full-ring acknowledgement on the reported native monitored-fence route.
  - [x] Execute the EnqueueSetEvent fallback explicitly on a supported device; keep production selection conservative. All x64 gates and ARM64 cross-builds pass.
- [x] P1.3c — Retain mapped slots through device retirement and shutdown until the copying worker has joined.
- [x] P1.3d — Add separate off-screen utility composition, tiled full-image readback and region/1-pixel request infrastructure.
  - [x] Qualify separate targets/shared raster cache and atomic producer-compose/copy recording.
  - [x] Qualify the single-reservation tiled worker job, adopted output, cancellation and bounded restart.
- [ ] P1.3e — Migrate merge, opening adjustment composites, export/Copy Merged and wand/clone/sampler callers.
  - [x] Qualify Merge and adjustment-opening continuations, worker crop/thumbnail, cancellation and exact apply tokens.
  - [x] Migrate PNG/JPEG export with detached CPU encoding and qualify bounded Map-busy recovery, later-tile failure and actual hardware/WARP measurements ([evidence](../../TestRuns/ProjectWriters_2026-10-01.md)).
  - [ ] Migrate clipboard, wand, clone and sampler consumers.
- [ ] P1.3f — Prove hidden completion, cancellation, stale generation, device loss, saturation and shutdown with owned event gates.
- [ ] P1.3f — Complete caller qualification and real-loop measurements; reconcile domain specs and commit/push each slice.
  - [x] Utility/Merge/opening slice: all x64 gates and ARM64 cross-builds pass; specifications and evidence reconciled.
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
completion may the UI Map staging with DO_NOT_WAIT. A driver-busy result permits exactly one additional
EnqueueSetEvent barrier, retaining the original copy, slot/generation and in-flight charge. Its event permits one
further nonblocking Map; persistent busy or barrier failure is local failure. No recopy, timer, polling or blocking
Map is permitted, and retirement is checked before either attempt. The worker receives only a read-only pointer,
row pitch and bounds backed by an owning slot.
Its completed stage returns to the UI to Unmap before the slot can be reused. Large reads traverse tiles under the
64 MiB total staging cap; a 1-pixel sampler does not read back a full document. No slot is reused while lent.

Device replacement invalidates recording on the old context. Lent slots keep their mapped context/texture in a
retiring state until the copying worker completes; a new generation cannot borrow their storage. The staging cap
includes retiring storage. Requests retain CPU inputs for a restart; after two losses use a correct CPU engine
where one exists, otherwise fail locally with the document intact. Shutdown stops event producers, joins workers,
then unmaps/releases remaining retiring slots. Cancellation of an event wait does not prove a mapped pointer is no
longer in use.

## Next integration sequence (P1.3d–e)

1. Keep one composition shader/context binding and raster cache. Add a separate utility view state (canvases,
   stack/coverage surfaces and view bookkeeping), swapped only during owner recording. Shared raster entries
   retain last-display usage so a utility tile cannot evict visible residents. Hidden release drops display
   usage/targets while accepted utility work retains only its own required resources. Report utility bytes
   separately; a settled display's compose count, target pixels and dirty state remain unchanged.
2. The queue records utility composition and the staging Copy in the same owner pump call. Merely returning a
   shared reusable utility target for a later copy is unsafe: a second request could overwrite it before that
   copy records. A borrowed producer owned by the suspended job prepares the source at recording time, under
   the existing two-batch bound. It does not own a context on a worker or allocate a retained callable.
3. A document-readback job prepares its immutable snapshot, region/tile traversal and full output PixelBuffer
   on workers, then alternates owner GPU stages and worker row-copy stages under its original reservation.
   Each copied lease is acknowledged before the next tile. Publish only an adopted complete raster; no
   partially filled buffer escapes. Region and 1×1 callers traverse only their requested region.
4. Register the queue once in Application initialization, bind on exact renderer device generations, stop its
   wait producers before JobScheduler::Close, and release remaining lent owners after worker join. Utility
   requests retain CPU inputs for a restart. Retry the entire graph on generation/device loss at most twice;
   utilities with no CPU composition fail locally after that threshold. No old context records commands.
5. Migrate opening CompositeBelow, merge, export/Copy Merged and wand/clone/samplers through typed ordered host
   stages and exact target/document/request generations. A failed or cancelled readback releases its ordinal
   only after acknowledgement, preserves pixels/history and never authorizes a later write or replacement.
   Keep the synchronous ComposeReadback overload only for diagnostic numeric references/harnesses.

Qualified utility slice: separate utility targets and produced-source recording are wired to a tiled worker
job. The host owns the queue, binds it on renderer generations, and stops event producers before worker join.
Merge and adjustment-editor opening now use retained continuation/history acquisitions; Merge crop/thumbnail
preparation moves to the worker. All x64 gates and ARM64 cross-builds pass; see
[GpuUtilityReadback_2026-09-30.md](../../TestRuns/GpuUtilityReadback_2026-09-30.md).
Remaining caller migrations, physical device-removal qualification and G1 remain open.

## Validation

Use state-machine expectations for resumable jobs, readiness/idle distinction, FIFO, memory refusal, duplicate
tickets, cancellation and shutdown. Use WARP numeric goldens for utility composites and cropped/tiled readbacks;
compare with the existing committed synchronous reference without changing its goldens. Own event gates that hold
GPU/copy completion and force generation replacement, hidden execution, queued commands and shutdown. Verify
zero Present while hidden, zero polling, storage/slot bounds and exactly one final receipt on every outcome.
Record Release dispatch/apply distributions separately from fence/copy/end-to-end time; timing is not asserted in
ordinary tests. ARM64 cross-builds do not establish native runtime qualification.

Foundation record: P1.2 is committed/pushed as `faf3cbf`. The foundation passes x64 Debug/Release/ASan tests and all three
ARM64 cross-builds on the frozen source. Capability and stage sidecars were reviewed, corrected and integrated;
the primary agent implemented scheduler stages and queue ownership. WARP reports monitored-fence support and
executes that native route. At that foundation commit, explicit fallback runtime and production consumers remained
open. The utility slice above adds those fallback tests, Merge/opening consumers and bounded restart. Remaining
callers, real-loop measurements and P1.3 completion remain open.
Foundation evidence: [GpuCompletionFoundation_2026-09-30.md](../../TestRuns/GpuCompletionFoundation_2026-09-30.md).

The staging cap counts power-of-two texture texel storage, including cached, lent and retiring slots. Driver
allocation/row-padding overhead is not exposed as an exact resource size and remains within global memory
headroom; workers always use the validated returned row pitch. This scope is recorded in the domain contract.

API references checked for this slice: [EnqueueSetEvent](https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_2/nf-dxgi1_2-idxgidevice2-enqueuesetevent),
[fence event completion](https://learn.microsoft.com/en-us/windows/win32/api/d3d11_3/nf-d3d11_3-id3d11fence-seteventoncompletion),
and [Map error/DO_NOT_WAIT behavior](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-map).
