# Memory admission, deferred release and recycling — 2026-09-27

Status: P0.7 complete on x64; all ARM64 configurations cross-compiled. The final Debug rerun also passed with the
appended measurement helper. Native ARM64 runtime remains unqualified.

## Scope

P0.7 adds whole-graph reservation before dispatch, event-driven admission retries, allocation-failure translation,
an intrusive release lane independent of job capacity, owner-thread large-release routing, capped aligned-buffer
recycling, low/high memory notifications and TraceLogging evidence. The host initializes the memory facilities
before image work and closes them after hiding the window and joining producers.

Existing image operations still need P1 migration and operation-specific peak declarations. This slice does not
establish G1 image latency, RAW support, or a Photoshop speed comparison.

## Before measurement and decision

`HostTests.exe --bench-memory`, x64 Release, 8192×5464 BGRA (179,044,352 requested bytes), two warmup pairs followed
by ten alternating fresh/retained pairs. QPC allocation/touch/free regions are separate; full checksums, process
memory samples and page-fault counters are outside those regions. These are memory costs, not image-kernel times.

| Before run | Fresh allocation median | Fresh touch median | Retained touch median | Fresh free median | Fresh faults / 10 results |
| --- | ---: | ---: | ---: | ---: | ---: |
| Initial | 16.05 µs | 16.847 ms | 6.750 ms | 5.789 ms | 437,980 |
| Confirmation 1 | 16.55 µs | 17.509 ms | 6.734 ms | 6.356 ms | 437,990 |
| Confirmation 2 | 21.05 µs | 16.986 ms | 6.627 ms | 6.018 ms | 438,000 |
| Confirmation 3 | 15.70 µs | 16.644 ms | 6.513 ms | 6.020 ms | 437,980 |

Retained touch generated zero page faults in all four runs. Every result checksum was 16,113,991,680. The three
confirmations exceed the plan's 10 ms fresh-page trigger (20% of the 50 ms Levels target); each fresh free also
exceeds the 4 ms UI duty budget. This justifies recycling and deferred destruction. Requested pixels are rounded
to 16 MiB classes (184,549,376 capacity here), with a separate 64-byte ownership record; the pool is at most 32
slots and min(initial available ceiling / 8, 512 MiB). Global headroom covers opaque CRT allocator bookkeeping.

Logs: `.build/logs/memory-before-benchmark.log` and `memory-before-confirmation.log`. R1: Ryzen 9 9950X3D,
16 physical / 32 logical processors, Windows 11 Pro 10.0.26200, Balanced power scheme. No repository build ran
concurrently with these measurements. They are short paired measurements; no whole-image speedup or absolute
deadline guarantee is inferred from them.

## Implemented path measurement

The Release after run keeps the original baseline pair loop and appends the actual `PixelBuffer` → intrusive
worker release → recycle reuse path, with the same requested bytes and full per-result checksum.

| Region | Median | p95 / maximum |
| --- | ---: | ---: |
| Fresh allocation after metadata change | 16.75 µs | 22.60 µs |
| Fresh touch after metadata change | 17.068 ms | 19.248 ms |
| Fresh inline free, control only | 5.969 ms | 6.737 ms |
| Recycled allocation | 0.30 µs | 1.00 µs |
| Recycled full touch | 6.669 ms | 7.254 ms |
| UI ownership retirement (enqueue only) | 7.95 µs | 9.50 µs |
| Subsequent harness settle to worker return | 29.15 µs | 58.10 µs |

The warm recycle samples incur zero page faults. Cache bytes equal 184,549,440, and ten full checksums total
161,139,916,800. Small ownership metadata has not caused a material fresh-allocation regression in this paired
control: 16.75 µs is within the before runs' 15.70–21.05 µs medians. The retirement row measures an enqueue,
not the actual free; the worker owns destruction/recycling after that point. This is not a UI/image benchmark.
Log: `.build/logs/memory-after-benchmark.log`.

## Review and corrections

Smaller agents wrote bounded admission, ownership, recycling and ETW test helpers. The primary implemented the
scheduler, storage/allocator ownership, host lifetime and pressure policy, reviewed every helper, and added actual
large direct/adopted/vector releases, decoded trace byte counts and release/trim-triggered admission tests.

- Pure-policy test expectations were corrected for existing reservations and saturating headroom at UINT64_MAX.
- Buffer-class tests initially under-budgeted their two different classes and expected a tiny allocation to reuse
  a large class; both expectations were corrected to the specified size-class policy.
- The reservation-lifetime fixture initially checked a child value before the worker wrote it; event ordering was
  corrected and serial successor validation was strengthened to include predecessor destruction.
- Empty completion drains no longer query memory. A regression test proves frames do not poll a waiting request.
- Existing observer tests assumed every scheduler wake was a completion; pressure events legitimately share that
  wake. They now settle through the production pump while retaining exact observer-order expectations.
- Private ETW stop now uses fresh properties as required by
  [ControlTrace's contract](https://learn.microsoft.com/en-us/windows/win32/api/evntrace/nf-evntrace-controltracew).
  The captured stream checks lost-event counters and actual `UiFree.bytes`, with two positive controls.
- The allocator keeps pixels separate from metadata and explicitly poisons cached bytes and class slack under
  ASan. The small record adds one allocation on a fresh buffer, which the after measurements must account for.
- A focused independent static review of scheduler, release, recycler and storage lifetime found no additional
  actionable issue. It did not replace runtime/sanitizer qualification.

## Qualification

| Gate | Result |
| --- | --- |
| Focused x64 Debug HostTests | Passed, including real ETW decoding and memory event/trim admission; 307 P0.7 checks in Release |
| Complete x64 Debug | Passed: all suites, WARP, probes and nine harness captures |
| Complete x64 Release | Passed: all suites, WARP, probes and nine harness captures |
| Complete x64 ASan Debug and stress | Passed: all suites, extended worker/raster/job stress, cache/slack poison checks, actual ASan probe, WARP and nine captures |
| Native concurrency analysis and controls | Passed: complete Release analysis, positive SAL control and deliberate C261xx negative control |
| ARM64 Debug / Release / ASan Debug cross-build | Passed; compile qualification only |
| Skill, formatting and diff checks | Passed: 15 skills, 162 C++ files, no whitespace errors |
| Actual recycle/deferred-release measurement | Passed with matching full checksums; values above |

Native ARM64 runtime, Application Verifier, the remaining P0.1 instrumentation and P1–P6 exits remain unqualified.
ETL tests use an owned `X:\RedPrism.Perf` root on an available fixed local drive, validated by marker and reparse
checks; they remove only their own trace files. Build logs and binaries remain under `.build/`.
