# Retry wait and owner routing qualification

Status: retry framework and Application source routing qualified; actual clipboard graphs remain open.

## Scope and proof

Eight persistent manual-reset events use the scheduler's existing one-thread wait pool. Relative one-shot
deadlines use the literal 50/100/200/400/800 ms arguments; cancellation signals the same wait. Callback readiness
is acknowledged before slot reuse. A failed signal retains ownership until its original deadline acknowledges.
Application registers one owner multiplexer and attaches GPU/retry sources; shutdown hides before joins.

The reviewed private prototype passed 117 retry assertions and six independent transformed Copy fixtures.
Primary review corrected a sidecar's unique_ptr type, removed a deadline-dependent unrelated-job fixture and
replaced it with three event-gated actual-job scenarios. Native tests also run two GPU copies through the actual
multiplexer with both sources initialized. No production clipboard
caller, six-attempt transfer graph, private-origin authority, elapsed-time target or native ARM64 runtime is
qualified by this framework.

The complete admitted job remains suspended with its original ticket, FIFO position and reservation; no retry
occupies a compute callback or creates a younger job. Structural tests prove unrelated work on the sole compute
worker, retained reservation until acknowledgement and exactly one final callback. Timing bounds only detect
deadlock. Existing clipboard/tool pixel and history goldens are unchanged.

## Frozen native qualification

All x64 Debug, Release and ASan Debug full tests pass, including the real sanitizer probe, and all three ARM64
clean cross-builds pass against the same 442 frozen inputs. Each normal x64 run passes 117 retry assertions,
six transformed Copy assertions and both routed GPU scenarios. ASan additionally repeats HostTests in stress
mode. Skills (15), formatting (391) and linked-image ISA gates pass; the runtime allowlist is unchanged.

Release version: `0.1.134.0`. Exact source/binary manifests, five Release images and matching PDBs, app-local DLLs,
DxUi provenance and eight gate logs are byte-preserved and hash-verified under `.build/p1-retry-routing-r1-qualified`.

- Source freeze SHA-256: `2BE60CF06B4D4BFCA01D4B02DA857DFE6A41BD98B30FAB34E12643DC80A27285`.
- Release application SHA-256: `1C49C9547F24623551301BD0C9ECBE7CE527B1148660644649B3CE0919D81762`.
- Unchanged ISA allowlist SHA-256: `6F55A40AB0CE23E98BB10839EC68765531129EF375B00DD29B298C3DC97EE560`.

The reviewed callback/owner lifetime rules found no additional defect. Only F5c.1 closes. Actual transfer retries,
origin receipt binding, complete clipboard admission, production callers, UI timing/G1 and native ARM64 runtime
remain open. The archive provenance receives the implementation commit after publication.
