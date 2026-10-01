# Destructive color history publication — 2026-10-01

Status: qualified correctness and ownership slice of P1.9 H1. Other caller groups and the measured P1/G1 exit
remain open. This run establishes no RAW or Photoshop performance comparison.

## Implemented boundary

Admission reads borrowed color settings and includes complete history preparation before source capture.
The admitted record captures owning source/token/history on the owner, resumes on the worker and prepares a
private after-layer/history candidate. Owner apply revalidates generation, opening settings, target and history
epoch before publishing and swapping pixels/settings/panel. Replaced panel lookup, preview, selection, settings,
source and history owners remain in the complete release aggregate through success, refusal and worker retirement.

Adjustment-layer OK/Cancel transaction closure retains its existing path and remains separate H1 work.

## Independent proof

- 45 prepared-color assertions: literal Gamma 2 BGRA output, one entry and Undo/Redo, applied settings and Levels
  reopening, independent saved revision, stale generation/epoch, foreign/moved/single-use authority, five-stage
  cancellation/allocation refusal, sparse input, lookup lifetime and independent retained-capacity floors.
- Actual application worker gates: all five history allocation checkpoints, capture refusal and epoch-only
  invalidation preserve source pixels and seeded redo. The complete aggregate retires off the owner.
- Existing queue/receipt ordering, color panel, numeric filter/history/composition/I/O goldens pass unchanged.
- Suspension controls explicitly wake the scheduler for memory while a worker remains held; draining that wake
  does not invoke the owner suspension hook. Tests wait for the actual hook predicate before asserting suspension.

## Configuration and identity

| Gate | Result |
| --- | --- |
| Skills / formatting | Passed: 15 skills / 316 native files |
| x64 Debug / Release / ASan Debug | Full clean-build suites passed; sanitizer detection probe and stress passed |
| ARM64 Debug / Release / ASan Debug | Clean cross-builds passed; native runtime unavailable |
| Frozen native/project/script inputs | Same 366 inputs verified across all six configurations |

Source-freeze SHA-256: `3D494A749DE574EAF799ACEE2E8C10EFFF6056D9030313F2254E8BF1B2D24D36`.
Release 0.1.123.0 executable SHA-256: `4394E3CA08A63D2E1DFE05EC739E497E8CCD9660B46531E73E22BE4774607009`.

Final logs: `.build/p1-color-history-r3-driver.log`,
`.build/p1-color-history-r3-{x64,ARM64}-{Debug,Release,ASanDebug}.log`, skills/format and source-freeze files.
The seven color host cases are also preserved in the three `r3-x64-*-selftest.err.log` copies.

## Failure investigation

The first native Debug attempt raw-waited for a worker gate before pumping the newly required admitted capture.
The shared harness event helper now pumps real owner capture/completion; its corrected native Debug run passes.
An initial Release run failed `job-stage.gate-timeout`. Its log does not identify which raw wait timed out;
the investigation exposed the invalid assumption that every scheduler wake means worker completion. Four
suspension waits now check the actual hook, and the unrelated-wake control above passes in all x64 profiles.

The original Release failure log and executable identity remain
`.build/p1-color-history-first-Release-failure.log` and `-image.json`.
An intermediate run detected a changed source freeze and stopped; it is excluded from qualification.
Only the final distinct r3 freeze and successful six-configuration driver qualify this slice.

## Remaining work

Size cards, merges/imports, selection, strokes, floating transactions, adjustment-layer closure and production
Undo/Redo still require migration. H2 shared immutable metadata/cached indexes, H3 incremental retained-storage
accounting, and H4 image thumbnails/first uploads/chrome remain open. Owner source capture still copies document
metadata; conservative transient retirement bounds are distinct from unique retained-history budget accounting.
No measured 2 ms apply target, full G1 exit or native ARM64 runtime is claimed.
