# Merge publication and retirement — 2026-10-01

Status: qualified correctness and ownership slice of P1.9 H1. Other callers, H2/H3/H4, measured apply and G1
remain open. This establishes no RAW, Photoshop, native ARM64 runtime or physical device-removal result.

## Implemented boundary

One selected history ordinal covers an admitted metadata probe and a separately admitted readback/preparation
record. Planning and the composition subset are built on the worker. No-plan commands succeed without GPU or
pixel allocation, including a 30,000-square empty canvas. Complete retirement bounds are installed before
source/history capture can fail. GPU mapping and target cleanup remain on the owner.

After final mapping acknowledgement, copied CPU pixels detach from the device. Survivor records, clipping
rewrites, merged pixels, selected ids and history are prepared together. Publication validates active id,
generation/restore authority and exact history epoch, then swaps live owners without constructing history or
survivor containers. Both stages retire their complete CPU aggregates off the owner, including replaced display
and history state. A device rebind after detachment cannot replay CPU preparation or invalidate copied pixels.

## Proof

- 48 independent `merge-history.*` assertions cover literal premultiplied pixels, crop/transparent output,
  insertion/parent/group/clipping rules, Undo/Redo, D7 sparse budget refusal, five allocation and cancellation
  checkpoints, invalid/stale/foreign/moved/single-use publication, and unchanged history on refusal.
- 23 `merge-plan*` assertions cover down/group/multiselect plans, anchors, descendants/sibling boundaries,
  source immutability, no-plan and prework cancellation.
- All three x64 self-tests print 17 `utility-history mode=` successes: ordered merge followed by Invert,
  cancellation/failure/capture and history faults, opacity/history staleness, detached rebind, metadata-probe
  refusal/cancellation/staleness and no-plan. They require acknowledged receipts, no hidden Present, returned
  reservations, zero terminal GPU leases and aggregate retirement off the owner.

## Configuration and identity

| Gate | Result |
| --- | --- |
| Skills / formatting | Passed: 15 skills / 329 native files |
| x64 Debug / Release / ASan Debug | Full clean-build suites passed, including sanitizer probe and stress |
| ARM64 Debug / Release / ASan Debug | Clean cross-builds passed; native runtime unavailable |
| Frozen native/project/script inputs | Same 379 inputs verified across all six configurations |

Source-freeze SHA-256: `3D7410C18BC184414486191B5B76F1E7F41C15DC33FC2C59CE03F5D636A928B6`.
Release 0.1.126.0 executable SHA-256: `A518A64CE0F3AFCF1EDD372B128F5FF8551EFB7D122CD51DCD38E90D40933D8F`.

Qualified logs: `.build/p1-merge-history-r5-driver.log`, the six corresponding platform/configuration logs,
skills/format/freeze files and copied x64 self-test stderr logs. The Release snapshot with runtime DLL hashes
and provenance is `.build/p1-merge-history-r5-qualified/`.

## Failed attempts and limits

Only r5 qualifies the final source. r1 stops at a generic scheduler wait before merge; the suspension helper now
waits for its actual hook and tests an unrelated memory wake, preserving the original timeout. r2 and r4 report
missing child exit codes after bounded waits; exact images/logs remain preserved. Retaining process handles
passes fresh preflight and the final matrix, without establishing the cause or qualifying those unknown exits.
r3 fails the existing rotation timer-resource test. Failure-only predicate diagnostics preserve its assertions;
12 focused full I/O runs and r5 pass without explaining the original failure. F6 remains open.

Metadata capture/traversal, incremental storage accounting, other readback/history callers, chrome/first uploads
and the measured 2 ms apply target remain separate work. Functional event gates make no latency claim.
