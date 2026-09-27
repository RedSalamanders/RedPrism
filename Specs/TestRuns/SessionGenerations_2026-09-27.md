# P0.4 — Session generations, owning tokens and shared selections

Status: P0.4 complete on x64; all ARM64 cross-builds passed; native ARM64 runtime untested
Date: 2026-09-27
Source: `dd53f03` plus this working tree; follows the raster/buffer foundation in `4c303fa`.
Plan: [UiNeverBlocks](../Plans/WIP/UiNeverBlocks_2026-09-22.md), opening checklist and P0.4.

## Changes and evidence

- Removed mutable document/layer readers. Production imports install a validated document copy as one entry;
  typed opacity previews/restores go through the session. Application harness and DocumentTests fixtures use a
  scoped mutation hook. A normal compile now rejects writes through `Document()` or `ActiveLayer()`.
- Added monotonic document generations, document serials and restore serials. Nested edits and live document
  changes invalidate before the enclosing history entry finishes. Undo/Redo cannot revive a prior token.
- The independent review found that separate sessions could share counter values. Tokens now also check an owner
  identity, refreshed on copy/move/assignment; moves retire the source identity. Tests cover both distinct and
  copied sessions, as well as moved-from sessions whose optional document remains engaged.
- Tokens retain raster, mask and selection ownership. Checks cover document/restore identity, sequence, canvas
  size, target image/transform/mask/kind/adjustment and selection handles. Unrelated layer changes can preserve a
  target token; whole-document jobs require the generation to match.
- Selection snapshots share immutable point vectors; geometry edits publish replacements. Existing numeric
  selection goldens are unchanged. Structural tests prove sharing and that a snapshot survives edits/clear.
- `MarkSaved(revision)` records the revision actually written. Tests cover intervening edits, Undo/Redo, a saved
  Undo-restored revision, a discarded branch, history trimming and reset.

Cheaper agents migrated host/test call sites and implemented saved-revision handling. Primary review corrected a
missed mask-reset fixture and changed the history tests to restore the local document on Undo/Redo. The primary
implemented mutation tracking, immutable selection storage, token validation and its scenario tests. The same
reviewer verified the owner-identity correction and reported no remaining issue in that focused check.

The first Debug test exposed an incorrect new expectation about a normalized polygon's starting vertex. The test
now asserts independently expected bounds before/after translation and shared storage identity. No legacy golden
was changed.

Mutation audit:

| Owner | Generation publication |
| --- | --- |
| `Document.cpp` | Entry-based layer, mask, ordering, size, import and transform edits use `EndEdit`, including nested ends. Create also advances the document serial. Clear/Replace and Restore explicitly invalidate; every history restore retires prior result tokens. Typed opacity has its own immediate publication. |
| `Selection.cpp` | Selection replacement uses `EndEdit`; a live outline move and its restoration publish while the entry remains open. |
| `BrushStroke.cpp` | Raster, mask, gradient, warp, shape, clipboard and pixel-move results use `EndEdit`. Floating-selection insertion publishes before its outer transform entry closes; merge/cancel publishes through the closing entry. |
| `AdjustEdit.cpp` | Destructive commits use `EndEdit`; adjustment-layer parameter changes publish immediately, including Preview off and Cancel restoration. |
| Host/test callers | Const readers enforce the migration at compile time. Production writes use import/opacity APIs; fixtures explicitly invalidate through `EditForTest`. |

## Validation

The final x64 Debug, Release and ASan Debug gates passed, including 105 generation cases, 15 saved-revision cases,
all legacy suites, WARP, crash/CRT probes, kernel smoke and nine harness screenshots per configuration (27 total).
ASan also passed the 256-round, eight-reader raster stress and the real sanitizer detection probe. ARM64 Debug,
Release and ASan Debug cross-builds passed. Native ARM64 runtime validation is unavailable on this x64 host.

| Gate | Result | Local log under `.build/logs/` |
| --- | --- | --- |
| `validate-skills.ps1` | PASS, 15 skills | `generations-skills.log` |
| `format.ps1 -Check` | PASS, 108 files | `generations-format-check.log` |
| `test.ps1 -Platform x64 -Configuration Debug` | PASS | `generations-Debug-tests-final.log` |
| `test.ps1 -Platform x64 -Configuration Release` | PASS | `generations-Release-tests-final.log` |
| `test.ps1 -Platform x64 -Configuration 'ASan Debug'` | PASS | `generations-ASan-Debug-tests-final.log` |
| `build.ps1 -Platform ARM64 -Configuration Debug` | PASS, build only | `generations-ARM64-Debug-build.log` |
| `build.ps1 -Platform ARM64 -Configuration Release` | PASS, build only | `generations-ARM64-Release-build.log` |
| `build.ps1 -Platform ARM64 -Configuration 'ASan Debug'` | PASS, build only | `generations-ARM64-ASan-Debug-build.log` |

All final runtime logs end with `All tests passed.` and have no failing assertions. The owner-identity correction
was included in all six final configurations. Generated logs, executables and screenshots are not committed.

## Limits

These changes establish ownership and stale-result prerequisites; the current image operations are still
synchronous. No claim is made about UI latency, recovery from job allocation failure, SIMD/GPU throughput, RAW
support or Photoshop comparison. Those remain later phase gates.

There is no new timing benchmark in this slice: the selection improvement is structurally verified (copies retain
the same point storage). Full document snapshots still copy the layer vector; history byte accounting and deferred
destruction remain P1.9/P0.7. Import/replacement/size validation still scans the document synchronously; P1 must move
preparation/validation off the UI thread along with those operations, leaving only token checks and installation.

Cancellation and preview-open checks belong to the scheduler/Prepare–Run–Apply slices. `CanApplyJobResult` checks
snapshot currency; it does not by itself accept, schedule or install a result.
