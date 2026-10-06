# P1 production startup for history residency maintenance

## Checklist

- [x] Reconcile the integrated index/epoch/maintenance slices and independently verify the missing normal startup call.
- [x] Prepare primary-owned startup wiring and actual normal `Run(SW_HIDE)` controls in an ignored review folder.
- [ ] Integrate after the preceding frozen file/Clone/clipboard matrix finishes; preserve its exact qualified source.
- [ ] Compile/run the startup and unchanged residency controls; repair concrete issues without relaxing references.
- [ ] Qualify the same-source required configurations, analysis, cross-builds and exact retained predecessors.
- [ ] Update the owning runtime/history/resource contracts and commit/push the qualified activation.
- [ ] Finish remaining reference writers, incremental accounting cost, resource activation and G1 before P1 Done.

Status: **reviewed unintegrated source and test draft**. No new compile/runtime qualification.
Parent: [P1](../Plans/WIP/UiNeverBlocks_2026-09-22.md).

## Source finding

The earlier managed index, raster lease/epoch and same-ticket maintenance pipelines already have matching
qualified receipts. Current `HistoryResidencySource::Start` attaches the bounded owner pump and subscribes
to sparse-residency publication; its readiness callback only publishes readiness and wakes the scheduler.
`StopNotifications` joins that callback before scheduler shutdown, and `CloseAfterWorkers` detaches the route.
Before this activation, all callers of `Start` are explicit self-tests. Normal `Application::Run` never arms
the subscriber. The duplicate prototype checklist row was reconciled; whole H3 remains open.

## Prepared activation

Normal `Run` starts the existing source after document initialization and settings application, before startup
paths or ordinary input. A full route or subscription registry fails startup, preserving the existing exit 5
policy; the source rolls back a partial attachment and ordinary shutdown closes the acquired prefix. This adds
no polling, timer or new history writer class. Actual maintenance admission, FIFO, residency permits, worker
publication and deferred aggregate retirement remain the existing qualified implementation.

The draft lives in `.build/P1Drafts/HistoryResidencyStartup/Reviewed-r3`. Its mapping preserves the five original
source hashes and six reviewed file hashes. It is not integrated while the previous 877-input source is frozen.

## Independent controls

Four actual `Run` scenarios yield 36 named controls:

- Normal startup observes the real subscriber before input, then a worker materializes a sparse historical
  image. Its independent eight bytes are `(10,20,30,255,1,1,1,1)`. The history-only budget is eight bytes:
  the four-byte retained leaf plus eight-byte newly resident parent must drop the old undo entry. Current
  opaque `(90,80,70,255)` pixels, immutable document, generation, current revision and saved state stay intact.
  The original admitted maintenance graph retires off owner and its charges retire before ordinary close.
- Failure immediately after successful subscription reaches the real startup checkpoint once and exercises
  shutdown of its acquired notification source.
- Every raster subscription slot is occupied by a bounded fixture. Actual normal startup refuses before the
  checkpoint and rolls back its owner attachment.
- Every remaining owner-pump route slot is occupied by a dormant fixture. Actual startup refuses attachment
  before the checkpoint without creating notice authority.

All four cases verify callback/prefix policy, joined notification/worker/event/reservation retirement, route
detachment and repeated shutdown. Primary inspection confirmed the cheaper agent's finding that startup
failure returns before the Run loop consumes the quit posted by window destruction. Four additional controls
drain each fixture's messages after callback/worker joins and check quit isolation before the next owner.
The borrowed startup hook is absent in production and exists solely to
observe or fail the actual `Run` prefix. Its normal-case worker pump is a test operation. The production path
performs no startup wait for pixels or history. Hidden functional tests do not establish visible frame latency,
G1, native ARM64 runtime or full H3 completion.
