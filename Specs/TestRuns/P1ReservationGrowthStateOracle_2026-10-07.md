# Reservation growth state oracle

This document defines an independent, externally observable reference for `JobScheduler::IncreaseReservation`. It is not a copy of the scheduler's private `Record` layout. The fixture observes only submitted tickets, `JobContext::reservedMemoryBytes`, `MemorySource` calls/snapshots, public reserved-byte accounting, events, final callbacks, successor execution, retained-memory transfer, and shutdown.

## Reference state

The fixture uses one initialized scheduler worker, a controllable `MemorySource`, one original job ticket, and manual-reset events. The first stage blocks on an owner-controlled event. After release it publishes a literal producer acknowledgement with release ordering, calls `NotifyOwnerWork`, and returns. The owner drains the actual worker completion; the successful `DeferCompletion` hook acquires the acknowledgement, publishes the observed ticket, and keeps that ticket suspended. This event is the only point at which growth calls are valid.

For an admitted original total `R`, a request `T` has the following reference transition:

| Preconditions | Result | Observable state |
| --- | --- | --- |
| Scheduler uninitialized or slot outside `[0, kCapacity)` | `E_INVALIDARG` | No sample and no charge change |
| Ticket is stale, running, resuming, empty, or scheduler closed | `HRESULT_FROM_WIN32(ERROR_INVALID_STATE)` | No sample and no charge change |
| Exact suspended ticket is cancelled | `E_ABORT` | Original total `R` remains charged |
| `T < R` | `E_INVALIDARG` | No sample and original total remains charged |
| `T == R` | `S_OK` | No sample and no state change |
| Exact suspended ticket, `T > R`, fresh sample fails | `E_OUTOFMEMORY` | Entire original record and `R` remain intact |
| Exact suspended ticket, `T > R`, either sampled capacity cannot admit `T-R` | `E_OUTOFMEMORY` | No partial growth; original ticket and `R` remain intact |
| Exact suspended ticket, `T > R`, both sampled capacities admit `T-R` | `S_OK` | The same ticket now owns total `T` |

The capacity reference applies the repository's memory-admission headroom to both snapshot dimensions independently. It does not infer admission from physical memory alone. A `UINT64_MAX` total is an ordinary refused request against the finite synthetic snapshots and must not wrap the delta or reservation accounting.

## Event scenarios

### Joined stage, repeated growth, FIFO, and retained transfer

1. Submit a `Commit` job with a fixed serial key, original total 4096, and a reserved retained-memory slot. Its worker enters stage one and waits on an event.
2. Admit a same-class, same-key zero-byte successor. The successor's worker-entry event stays unset while the predecessor is running and suspended.
3. Release stage one. It publishes its acknowledgement and returns. The owner drains the real completion and observes the successful suspended ticket only after the worker stage has returned.
4. Grow the original ticket from 4096 to 8192 and then 12288. Each successful growth uses exactly one fresh sample. Repeating 12288 returns `S_OK` without sampling. Shrinking to 8192 is refused without changing the charge.
5. Resume the original ticket once. A second resume is refused. Growth while the ticket is resuming or while stage two is running returns invalid state.
6. Stage two records `JobContext::reservedMemoryBytes` and must observe 12288. While it is held, the same-key successor remains outside `Run`.
7. Release stage two. The final owner callback transfers the whole reservation using `RetainMemory`. The charge observed before and after transfer remains exactly 12288. The successor may enter only after this callback's release-published marker and must acquire that marker.
8. Releasing the retained ticket returns the 12288-byte charge. The old job ticket remains invalid after its record retires.

### Refusal, unchanged original state, and recovery

At one real suspended stage, independently try a failing sample, a physical-capacity shortfall, a commit-capacity shortfall, and `UINT64_MAX`. Each returns `E_OUTOFMEMORY`; after each attempt the original total remains 4096, the same ticket remains suspended, and the worker-run count remains one. Restore a fitting snapshot and grow that same ticket to 8192. Resume it and require stage two and final retained transfer to observe/retain exactly 8192. This proves a refusal did not replace the record with a failed or partial reservation.

### Same-owner snapshot revalidation

On a separate suspended 4096-byte ticket, configure the next `MemorySource` sample with a one-shot callback that calls `Resume` on the same scheduler and exact ticket. The callback must run on the owner thread. `Resume` succeeds during sampling, changing the observable ticket state; once sampling returns, `IncreaseReservation(ticket, 8192)` must revalidate and return `HRESULT_FROM_WIN32(ERROR_INVALID_STATE)`. The scheduler charge remains 4096 and the callback consumes exactly one fresh sample. Drain the resumed worker stage and require its `JobContext` to observe 4096. Its final callback must transfer 4096 to the retained slot, and releasing that retained ticket must return the charge to zero. This scenario checks both that the memory source is called without the scheduler's exclusive lock held and that a stage changed during sampling cannot receive stale growth.

### Cancellation and full shutdown

For cancellation, first grow the suspended original ticket to 12288, then cancel it. A request for further growth returns `E_ABORT` without sampling and the whole 12288-byte charge remains until cancellation is acknowledged. Resume the ticket to deliver its one final `Cancelled` callback; stage two is skipped and the complete grown charge is returned.

For shutdown, grow a suspended ticket to 12288 and close the scheduler. Close joins the worker pool, refunds the complete grown charge, suppresses the final callback, and does not run stage two. A later growth attempt on the closed ticket returns invalid state.

## Synchronization and failure bounds

All coordination is event-driven. Producer values publish with release ordering and the owner reads with acquire ordering; `NotifyOwnerWork` supplies the scheduler's release/acquire wake path. Owner waits use the scheduler wake handle and drain completions before waiting again. There are no sleeps, periodic polls, throughput checks, or latency assertions. Ten seconds is only a deadlock/failure bound for a missing event or scheduler wake. The fixture contains 41 named behavior expectations. Event creation, worker initialization, initial admission, and waits that advance a fixture print as `setup` checks; they are not independent scheduler behavior controls. The test entry point returns the full number of failed expectations, including failed setup checks.

The test uses WIL `unique_handle` for native events, a fixed one-worker pool, `Expect` output flushed after each assertion, and an integer failure result. It creates no message window, graphics device, or unbounded worker pool.

## Source cross-check and limits

The reviewed public declaration is in `Common/Jobs.h`, adjacent to `Resume` and `RetainMemory`. The implementation in `Common/Jobs.cpp` checks initialized/slot bounds, owner thread, closed/closing state, exact `Suspended` state and generation, cancellation, monotonic totals, same-total no-op, fresh `MemorySource` sampling, and a delta reservation. It changes `record.options.memoryBytes` and `record.reserved` only after admission succeeds. `JobContext` is built from the record's reserved total for each worker stage; completion retention moves the record's reservation into the pre-reserved retained slot. `DrainCompletions` publishes suspension only after the worker's final node is drained, and resumed stages are promoted outside callback dispatch.

The fixture is integrated into normal `HostTests` as `Host.Tests.ReservationGrowth.cpp/.h`. Compilation and runtime qualification remain pending on this revision. The full profile matrix and predecessor inventories must pass before this source is qualified. No legacy golden is changed.
