# Size-card history publication — 2026-10-01

Status: qualified correctness and ownership slice of P1.9 H1. P1.8's measured apply target, other H1 callers and
the complete P1/G1 exit remain open. This run establishes no RAW or Photoshop performance comparison.

## Implemented boundary

Borrowed options and document/history metadata declare the complete preparation peak before owning capture.
The admitted record captures source, token, active layer and immutable history handles on the owner, then resumes
the unchanged cancellable resizer and complete history preparation on its worker. A private move-only candidate
binds the resized document to its publication authority. Owner apply checks the whole document token, active
layer and exact history epoch before publishing and swapping document, lasso and selection-move owners.

The complete retirement bound exists before capture and includes current, pending, past/future, retained draft
capacities and private output headroom. Success, cancellation, stale results and construction failure retain
these owners through worker retirement. Active layer, mask targeting, crop and existing numeric size behavior
remain unchanged. Source capture still copies value metadata; H2/H3 and chrome/uploads remain separate work.

## Independent proof

- 50 document assertions: literal colored extension and nearest-resize pixels, masks/selection/crop/active target,
  resolution-only Undo/Redo, nested/no-op history, foreign/moved/single-use authority, five history allocation and
  five cancellation stages, and retained lasso capacity/transfer.
- Active-layer-only selection leaves the document token current, but refuses a candidate with stale history
  active selection before either document or history changes.
- All 22 actual size-card host scenarios pass in each x64 profile, including admitted capture refusal, all five
  history allocation checkpoints, epoch-only invalidation, source/seeded redo preservation and off-owner complete
  aggregate destruction. Existing ordering, cancellation, stale-card and size goldens pass unchanged.

## Configuration and identity

| Gate | Result |
| --- | --- |
| Skills / formatting | Passed: 15 skills / 320 native files |
| x64 Debug / Release / ASan Debug | Full clean-build suites passed, including sanitizer probe and stress |
| ARM64 Debug / Release / ASan Debug | Clean cross-builds passed; native runtime unavailable |
| Frozen native/project/script inputs | Same 370 inputs verified across all six configurations |

Source-freeze SHA-256: `A086204B04F5C406CC564A5CC33D8D05BA63D76FED8C74FF5146C4CE8241545C`.
Release 0.1.124.0 executable SHA-256: `9C3DB1F8F7D9F3301235F36406CC2B266C2EEDD7F79C25500FDE28083ECA8E64`.

Final logs: `.build/p1-size-history-r2-driver.log`,
`.build/p1-size-history-r2-{x64,ARM64}-{Debug,Release,ASanDebug}.log`, skills/format/source-freeze files,
and three `r2-x64-*-selftest.err.log` copies. The preserved Release snapshot is
`.build/p1-size-history-r2-qualified/`, with binary hashes and qualification provenance.

## Preserved failure

The first frozen sizing matrix passed Debug and Release, then failed ASan's existing folder save-in-place case:
the fixture was written/opened, but the asynchronous save did not report success and the reload failed.
It stopped before ARM64 qualification. The original failure is preserved in
`.build/p1-size-history-first-ASan-failure.log`, `-selftest.err.log` and `-image.json`.

The initial fixture-save trace did not cover that asynchronous write. A bounded trace-only harness gate now records
its actual worker IO stages/native values/status and terminal receipt after acknowledgement. A separate ASan
preflight and the final r2 matrix passed; neither identifies the original cause. F6 remains open. The first
failed matrix is excluded from qualification; only the distinct final r2 freeze qualifies this slice.

## Remaining work

Merge/import/selection/stroke/floating/adjustment-layer history publication and production Undo/Redo still require
migration. H2 shared immutable metadata/indexes, H3 incremental retained accounting and H4 thumbnails/uploads/chrome
remain open. No measured 2 ms apply target, complete G1 exit or native ARM64 runtime is claimed.
