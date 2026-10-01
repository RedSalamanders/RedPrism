# Prepared history publication foundation — 2026-10-01

Status: foundation qualified in all required profiles. P1.9 H1, other caller migrations, H2/H3/H4 and P1/G1 remain open.

## Implemented boundary

History entries and opening snapshots have immutable shared ownership. A captured view retains bounded stack
handles, pending/depth state, revision and a structural epoch; workers never dereference live history. Save
publication stays independent. Detached edit/begin/end/restore preparation constructs snapshots, entries,
container growth and trim before publication. Refusal preserves pixels, revision, redo and pending state.

Owner publication validates exact owner, epoch and revision and swaps complete containers without allocation.
Moving a candidate removes its old authority; a successful candidate cannot publish twice. Restore owns its
document independently when trimming removes its destination entry. End reuses the opening snapshot handle.

Production basic raster, gradient and pixel-move commits bind their exact prepared target/token to the history
candidate. Old live layer/selection/history, pending previews and brush-commit owners move into the job aggregate
for worker release. Before capture, the aggregate receives a conservative bound for all current/pending/past/future
owners, possible sparse assembly and private preparation/preview headroom. It remains eligible for worker release
on capture/preparation failure and after apply/drop. Cached raster bounds avoid recursive shared-DAG expansion;
overflow or excessive depth refuses admission before source capture.

## Independent proof

- 17 history-state-view checks and 21 publication checks: stale/foreign/moved authority, transactions, no-op/redo,
  save independence and restore-after-trim with literal pixels.
- 188 assertions across 34 cancellation/allocation cases at detached edit/begin/end/Undo/Redo preparation stages.
  Live revision, names, counts, depth, redo and literal pixels remain unchanged on refusal.
- 39 bound raster-publication checks: Invert, Gradient and PixelMove, exact source/selection/history validation,
  moved authority, literal output/Undo and old preview ownership through aggregate release.
- 20 retirement-bound checks: independent pixel/name/selection capacity floors, current/pending/past/future
  owners, adopted allocation, sparse bases/nested tiles, possible assembly, depth refusal and a repeatedly
  shared DAG whose occurrence count overflows despite only 67 physical storage records.
- Five actual application worker-runner history-allocation injections for image Invert: BeforeSnapshot,
  AfterSnapshot, Entry, StackGrowth and TrimIndex. Each returns E_OUTOFMEMORY with source/history intact and
  aggregate retirement off the owner. Existing raster, gradient and pixel-move host scenarios still pass.
- Existing numeric document/history/filter/selection/composition/I/O goldens remain unchanged.

## Configuration and identity

| Gate | Result |
| --- | --- |
| Skills / formatting | Passed: 15 skills / 312 native files |
| x64 Debug / Release / ASan Debug | Full clean-build suites passed; sanitizer stress and detection probe passed |
| ARM64 Debug / Release / ASan Debug | Clean cross-builds passed; native execution unavailable |
| Frozen native/project/script inputs | Same 362 inputs verified across the complete matrix |

Source-freeze SHA-256: `3F555CEB41FBE4D68D8810A228F34B70E6C66150A719BAABBE904C35237E759A`.
Release 0.1.122.0 executable SHA-256: `E96BD48C58A59222587720A3BCCFE00A94B83DD1B37B9C49935BF85A16A126A7`.

Logs: `.build/p1-history-publication-r1-driver.log`,
`.build/p1-history-publication-r1-{x64,ARM64}-{Debug,Release,ASanDebug}.log`, skills/format logs and source freeze.
The earlier first native clean Debug build/document/host logs remain `.build/p1-history-native-*r1.log`.

## Limits and next work

This foundation preserves the current retained-byte/trim contract. Conservative transient retirement charges
are separate from unique retained-history bytes. Whole-document metadata capture/traversal and trim-set rebuilds
remain H2/H3 costs. Image thumbnails, first uploads, chrome and the measured 2 ms apply/G1 target remain H4 work.

Production color/filter, sizing, merges/imports, selection, strokes, floating transactions and Undo/Redo still
need migration. A separate color draft has 45 focused Debug checks, but is outside the frozen production source.
Adjustment-layer OK/Cancel transaction closure also remains open. No timing, native ARM64 runtime, RAW decode
or Photoshop speed comparison is established by this correctness qualification.
