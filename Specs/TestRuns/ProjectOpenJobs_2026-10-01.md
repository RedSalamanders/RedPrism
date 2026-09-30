# Asynchronous project opening — 2026-10-01

Status: bounded F3b.3a activation and measurement qualification passed. P1/G1 remain open.

## Implemented boundary

Interactive project opening now probes manifest/PNG metadata on a worker, then admits a separately capped decode
job under the same selected host input and history ordinal. Probe handles/parser owners die on its worker; only
small standard limits/version values reach completion. FIFO Io serialization prevents decode before probe
retirement. The two reservation charges may coexist at submission. Refused decode admission leaves the document
intact and proceeds to the next project while retaining the first batch error.

Current completion validates serial, generation, ordinal and acquisition before replacement. Only an explicitly
validated replacement authorizes continuing that selected batch on its new lineage. External replacement,
including reused persisted IDs, makes the result stale. Cancellation retains owners through acknowledgement
and suppresses later files. Ordinary failed files preserve the current document and continue the batch. Posted
notices format the file's version outside completion, including version zero. Owner replacement, release,
chrome and uploads remain measured costs rather than worker-migration claims.

## Correctness evidence

- All 36 probe/decode before/after host cases pass: success, younger-input staleness after replacement,
  cancellation acknowledgement, returned failure, actual worker/admission allocation failure, reused IDs,
  generation changes and an empty session. Decode gates observe only the complete declared decode charge,
  proving the probe reservation has retired before decode entry.
- Nine two-project cases pass: ordered replacements, missing first file, worker failure followed by success,
  decode-admission refusal followed by success, unsupported versions 99 and zero through real notice posting
  and deferred formatting, after-probe source growth, second-file cancellation and external replacement.
- Cases require exact failure/cancel/stale receipts, captured path, independent literal pixels/active id,
  saved baseline/reset history, view input while held, no hidden Present and zero tickets/charges/barriers.
- Twelve pure replacement-continuation controls require the exact completed acquisition and authorized new
  lineage. Four independent allocation floors compare reservations with PixelBuffer output allocation,
  document layer records and manifest input. There are 73 load/probe/resource checks in each x64 IoTests run.
- The real-loop smoke includes project opening after a completed project save: 22 operations, 64×48,
  two trials after one warmup. Readiness uses receipts and session/metadata state without timed file reads.

## Qualification corrections

An initial test setup borrowed the session's active id into a replacement that clears the session. Copying the
active id before the test replacement fixed that setup; production load results already own their active ids.
MSVC inlined AVX2 for a new test's string search. A scalar assertion removes it; the ISA scanner and runtime
allowlist were not relaxed. All 202 scanner controls pass.

The first full Debug gate failed `job-stage.external-owner.pending-is-idle-blocker-not-ui-work`. That test used
real OS memory notifications, which legitimately request unrelated UI readiness. The isolated owner state had
no ready producer, and the scheduler's remaining readiness branch was its asynchronous memory-change signal.
The corrected test uses the existing controlled memory source and explicitly checks that a separate memory
wake requests a drain without pumping an unready owner. The original failed log remains
`.build/p1-project-open-original-memory-event-failure.log`; the OS high/low producer was not separately traced.

## Configuration gates

| Gate | Result |
| --- | --- |
| Skills / formatting | Passed: 15 skills / 270 files |
| x64 Debug / Release tests | Passed |
| x64 ASan Debug, extended stress and real sanitizer probe | Passed |
| ARM64 Debug / Release / ASan Debug cross-builds | Passed; native execution unavailable |
| Hardware 4K / 45 MP and WARP 4K, 22 real-loop operations | Passed; ten trials after one warmup per row |

Logs: `.build/p1-project-open-driver.log`, `.build/p1-project-open-{x64,ARM64}-*.log` and
`.build/p1-project-open-freeze.json`. The frozen matrix covers 318 code/project/script inputs unchanged through
all six configurations. SHA-256 identities:

- Freeze manifest: `1661B358490E04CC99DE944607C1FC41C6135AF0F8BEC867B9CE1754FACFD470`.
- x64 Release RedPrism: `0844DDF3D5B93ABD6F0B08950E666402E8733EA04555AAF6B81F6C83F548BA3F`.

## Measurements and limits

Real-loop hardware 4K/45 MP and WARP 4K measurements use ten trials after one warmup, filesystem caches warmed by the immediately preceding production save,
two full raster layers, one mask and an exposure adjustment. This is neither cold-storage/RAW qualification nor
the master four-full-layer G1 fixture. No Photoshop comparison is established.

Ryzen 9 9950X3D, RTX 5080 driver 32.0.16.1714, Windows build 26200, 66,155,405,312 physical-memory bytes,
x64 Release 0.1.115.0, Scalar kernels and 32 workers. WARP uses driver 10.0.26100.9278. Values are milliseconds.

| Device / canvas | Open elapsed median | p95 / maximum | Apply maximum | Drain maximum | UI action maximum | Frame maximum |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Hardware 4K | 18.675 | 20.722 | 1.970 | 1.973 | 0.027 | 5.586 |
| Hardware 45 MP | 120.355 | 128.810 | 5.079 | 5.086 | 0.062 | 50.517 |
| WARP 4K | 22.852 | 24.363 | 2.965 | 2.967 | 0.050 | 6.723 |

The ten-trial p95 is the maximum. Each open row records 20 frames and zero readback copies/maps/events/failures.
Message dispatch maxima are 11/12/14 microseconds respectively. Logs:
`.build/p1-project-open-benchmark-driver.log` and `.build/p1-project-open-{hardware-4k,hardware-45mp,warp-4k}.log`.
All three runs completed all 22 operations with no persistent-busy failure. The apply/drain overruns at hardware
45 MP and WARP, and the hardware 45 MP frame cost, leave G1 open. These scopes do not independently attribute
the remaining cost; release, session/history, chrome and upload attribution/optimization remain P1.9/P3 work.

Image-import/clipboard workers, remaining processing migrations, pending-IO/codec interruption and faults,
incremental history, thumbnail/upload work, full operation timing and the G1 exit remain open. The import review
also identified a negative-budget unsigned cast in the current WIC frame guard; correction and opened-stream
caps belong to the immediately following import slice. Native ARM64 execution remains unavailable.
