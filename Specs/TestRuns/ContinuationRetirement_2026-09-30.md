# Per-entry continuation retirement — 2026-09-30

Status: per-entry retirement primitives qualified; production continuation activation remains open

## Changes

`ContinuationQueue` has retirement-aware overloads for admission, dequeue and Clear. The owner receives each
superseded, stale or cancelled entry before its input releases. Coalescing installs the replacement before
retirement; stale dequeue and Clear detach first. Clear retires in input order across ring wrap. Refused admission
retains caller ownership without a callback. A taken active entry remains the owner's responsibility.

Callbacks are synchronous, non-throwing and never retained. They may move the entry to another owner, but cannot
access the same queue, dispatch UI, pump or wait. The queue adds no fields, storage, allocation, worker or wakeup.
Receipt-owning callers must explicitly retire queued entries before teardown; the destructor does not invoke an
owner callback. Count-only forms remain for requests without observed outcomes.

## Evidence

Focused HostTests retain external observer references while requests move through the queue. Expected retirement
metadata, FIFO order and destruction counts are literal state-machine expectations. The primary owns the queue
implementation and reviewed the cheaper agent's test draft before qualification. Review corrected invalid
smart-pointer access, strengthened external receipt identity and exact terminal-state checks, and added explicit
payload ownership and trace-overflow assertions. Callbacks use fixed trace arrays; observer allocations occur
before admission. Existing legacy image goldens are unchanged.

Refused Full/InvalidOrder inputs retain both their payload and Pending receipt; the owner then publishes Failed
through that same owned receipt. Superseded requests become Cancelled, stale drops become Stale, and Clear
publishes Cancelled while a taken active request remains Pending until its owner publishes Succeeded. A callback
can retain a cancelled entry across Clear and releases it only when that new owner drops it. Destruction counters
prove callback ownership and progressive FIFO release, including wrapped entries.

## Validation

| Gate | Result | Evidence under `.build/logs/` |
| --- | --- | --- |
| Skill validation and formatting | Passed: 15 skills, 201 source files | Matrix preflight |
| x64 Debug full tests | All tests passed; 154 continuation assertions | `continuation-retirement-x64-Debug-tests.log` |
| x64 Release full tests | All tests passed | `continuation-retirement-x64-Release-tests.log` |
| x64 ASan Debug full tests | All tests passed, including actual sanitizer detection, stress and WARP | `continuation-retirement-x64-ASan-Debug-tests.log` |
| ARM64 Debug cross-build | Passed, pinned dependency provenance verified | `continuation-retirement-arm64-Debug-build.log` |
| ARM64 Release cross-build | Passed, pinned dependency provenance verified | `continuation-retirement-arm64-Release-build.log` |
| ARM64 ASan Debug cross-build | Passed, pinned dependency provenance verified | `continuation-retirement-arm64-ASan-Debug-build.log` |

No compiled source changed during the final matrix, and no benchmark ran concurrently with builds. ARM64 results
are cross-compilation only; no native ARM64 runtime or image-speed claim is made.

## Limits

These primitives do not register the Application UI-action source, introduce its typed requests/receipts or
activate production OK/Save/Close/import. The host must publish explicit outcomes on every retirement and on
admission refusal. The integrated caller matrix, remaining P1 migrations and P2–P6 remain outstanding.
