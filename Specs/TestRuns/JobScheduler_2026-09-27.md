# Job scheduler and outer message pump — 2026-09-27

Status: P0.6a complete on x64; all ARM64 configurations cross-compiled. Native ARM64 execution remains unqualified.

## Scope

P0.6a adds `Common/Jobs.*`, a 128-record scheduler with seven job classes, three OS priorities, FIFO serial keys,
two intrusive completion nodes per record, coalesced progress, an eight-action UI queue and the shared outer
message/completion/action pump. `Application` owns it and waits on its event in both idle and frame waits.
`Main.cpp` explicitly shuts it down before releasing localization and COM on all mode returns.

Large-image operations still run through their existing synchronous paths until P1. This slice establishes
ownership, cancellation and delivery; it does not claim G1 latency, a RAW implementation or a Photoshop speedup.

## Review and independent expectations

- The primary implemented the scheduler and host integration. A smaller agent added completion-priority frame
  cases; another implemented event-gated scheduler tests; another updated bounded normative contracts. The
  primary reviewed all changes and ran the final gates.
- Fixed capacity includes running and undrained jobs, preventing the Windows pool from becoming a second,
  unbounded payload queue. Rejected submissions retain the caller's ownership.
- A serial key is held through UI apply/drop. A successor cannot start while its predecessor's result is waiting
  to apply. Keys are collision-free host IDs; path normalization belongs to the later I/O migration.
- Both the SLIST head and every entry use `MEMORY_ALLOCATION_ALIGNMENT`. A producer never touches a final
  record after publication; progress producers must join before Run returns. Detached progress nodes remain
  claimed until their links have been read by the drain.
- A message-only window sentinel opens a running worker's gate. The sentinel precedes the one successful apply;
  cancellation yields no successful apply and both variants destroy every job payload.
- FIFO, independent keys, queue saturation, stale generations, progress ordering, `bad_alloc` failure,
  nonrecursive callbacks/actions and shutdown lifetime have state-based expectations with bounded failure waits.
- The WARP self-test simulates a mode's early failure with a gated worker, then uses the production shutdown:
  no completion callback applies, the worker returns before payload destruction, its MTA remains valid through
  the callback, and the owner's STA remains alive after shutdown.

## Validation matrix

| Gate | Result |
| --- | --- |
| Initial x64 Debug build | Passed |
| Initial hidden WARP self-test, including early-failure shutdown | Passed |
| Complete x64 Debug tests | Passed: 89 scheduler checks, all suites, WARP, both crash probes, kernel smoke, nine screenshots |
| Complete x64 Release tests | Passed: all suites, WARP, both crash probes, kernel smoke, nine screenshots |
| Complete x64 ASan Debug tests and extended worker/job stress | Passed: 256 raster stress rounds, 64 worker stress rounds, repeated job gates, actual sanitizer detection, WARP and nine screenshots |
| Release native concurrency analysis | Passed, including positive SAL and deliberate negative C261xx controls |
| ARM64 Debug / Release / ASan Debug cross-builds | Passed; compilation only |
| Skill validation, formatting and diff checks | Passed: 15 skills and all 132 C++ files checked |

Logs belong under `.build/logs/jobs-*`; generated executables, dumps and screenshots are not committed.
Native ARM64 execution and Application Verifier remain separately unqualified; cross-compilation and ASan do
not establish those results. ETW, UI-stall measurement and end-to-end image latency remain P0.1/P1 work.

## Scheduler overhead on R1

Release `HostTests.exe --bench-jobs`, 32 workers, eight warmups followed by 200 single Commit jobs:

| Measurement | Median | p95 | Maximum |
| --- | ---: | ---: | ---: |
| Submit | 2.6 µs | 5.3 µs | 6.8 µs |
| Submit through worker, completion drain and payload release | 19.3 µs | 53.4 µs | 301.9 µs |

The independently checked checksum was `645550820096`. Payload construction occurs before timing; scheduler
admission and cancellation-state allocation are inside Submit. This synthetic job quantifies the newly added
dispatch cost, without rendering or image work. It is not a baseline speedup or a G1/G2/G3 result.
Source output: `.build/logs/jobs-scheduler-benchmark.log`.
