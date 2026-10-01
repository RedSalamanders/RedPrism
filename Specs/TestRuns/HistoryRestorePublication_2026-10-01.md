# Undo/Redo publication and retirement — 2026-10-01

Status: qualified correctness and ownership slice of P1.9 H1.5. Other H1 callers, shared metadata capture,
incremental accounting, thumbnails/uploads and measured G1 remain open. No RAW, Photoshop, native ARM64 runtime
or physical device-removal claim follows this result.

## Implemented boundary

Production commands capture constant-sized session, document/restore serial, generation, active/mask and crop
authority, including no-document Redo. Empty stacks and open transactions return without capturing history.
An admitted owner hook captures immutable history handles; the same record/reservation resumes on the worker.
The complete peak includes the selected restore snapshot even when its metadata exceeds the live document.

The worker prepares restored document, selected ids, destination stacks and trim. Publication checks complete
authority and exact history epoch before nonthrowing swaps. Replaced document/display/selected/history owners
stay in the complete release aggregate until worker retirement. Saved revision remains independent. Only the
selected receipt is rebound when restoration changes document presence/id; younger old-lineage input is stale.

## Proof

- 63 `history-restore.*` assertions cover literal BGRA/opacity/mask, active/mask target, crop/no-entry behavior,
  saved revisions, no-document Redo, selected-snapshot memory floor, default/foreign/moved/single-use authority,
  exact epoch/generation/target/crop and both directions of three allocation/cancellation checkpoints.
  A newly begun unrecorded lasso invalidates preparation without corrupting its draft.
- All three x64 self-tests print 28 `history-restore-host.mode=` successes: Undo/Redo, ordered younger edit,
  empty/nested no-op, crop/mask, New Canvas removal/restoration, capture/worker/history refusal, cancellation,
  epoch/generation/active/mask/crop/lasso/replacement staleness and saved revision during worker preparation.
  Every terminal case requires acknowledgement, returned reservation/barrier/ticket, no hidden Present and
  complete aggregate retirement off the owner.

## Configuration and identity

| Gate | Result |
| --- | --- |
| Skills / formatting | Passed: 15 skills / 335 native files |
| x64 Debug / Release / ASan Debug | Full clean-build suites passed, including sanitizer probe and stress |
| ARM64 Debug / Release / ASan Debug | Clean cross-builds passed; native runtime unavailable |
| Frozen native/project/script inputs | Same 385 inputs verified across all six configurations |

Source-freeze SHA-256: `F5BCD3CDE35631BC8D9211EE6A863E1C87A35A60DF0B18797C55FC5E274BCAA0`.
Release 0.1.127.0 executable SHA-256: `558E46CFBDB318715CC486B741431A64FCB3F991AF441A68CEDC46F1FB1E760F`.

Logs: `.build/p1-history-restore-r1-driver.log`, its six platform/configuration logs, skills/format/freeze files
and copied x64 self-test stderr. The Release/runtime/source/binary provenance snapshot is
`.build/p1-history-restore-r1-qualified/`.

## Preserved failure and limits

The first full Debug preflight passes all new assertions and host modes, then fails the existing crop shortcut
test because it reads restored dimensions immediately after accepting asynchronous Undo. Its exact executable,
PDB, runtime DLLs, source freeze and logs remain in `.build/p1-history-restore-first-Debug-crop-failure/`.
The test now waits for acknowledged restoration before reading dimensions or switching tools. Its original
pixels/history/crop expectations are unchanged. The second full Debug preflight and final frozen matrix pass.
Only the final frozen matrix qualifies this source; failed preflight is not retroactively qualified.

Value metadata capture/traversal, other history callers, incremental storage accounting, chrome/first uploads
and the measured 2 ms apply target remain separate work. Event gates establish behavior, not latency bounds.
