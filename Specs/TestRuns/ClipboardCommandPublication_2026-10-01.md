# Atomic clipboard command publication

Status: A5.4b core publication qualified; production clipboard graphs and P1 remain open.

## Scope

Sealed worker Copy/Copy Merged/Cut preparation binds exact session/document/target/selection/crop/private-copy
serial and history authority. Direct successful copies advance the serial; failed direct Cut restores it with
its old clipboard. Cut prepares both copied pixels and one complete Clear/Fill Mask history publication before
either half may apply. Apply performs nonthrowing swaps and retains all replaced owners for aggregate retirement.
Copy preserves document generation, history and live brush/lasso/warp drafts. Copy Merged carries the full-document
composition token captured before rendering; stale composition cannot acquire a fresh generation.

## Independent controls

The private Copy/Cut prototype passed 63, 81 then 83 checks. Primary review added actual publication of live drafts,
actual redo preservation, stale Cut serial refusal, no-paint/hidden/no-selection refusals, captured mask palette,
live warp closure, long token mask name and complete color/mask/semantic thumbnail held-owner memory floors.
Native adds the stable encoding getter and eight literal Copy Merged authority/grid/crop/history controls, for
92 native assertions. All pass in each x64 configuration; private proof is recorded separately.

The native runner clean-rebuilds and runs full x64 Debug, Release and ASan Debug suites, then clean-rebuilds all
three ARM64 configurations against one immutable 446-input freeze. Real sanitizer instrumentation, linked-image
ISA controls, skills and formatting are required. No native source changes occur during the matrix.

## Remaining work

Only A5.4b core publication can close after those gates pass. Production Copy/Cut/Paste/New Canvas routing, receipt
binding to encoded payload, complete codec/OS admission, retry/cancel acknowledgement, constant-time document
capture, real-loop measurement/G1 and native ARM64 runtime remain open. Existing pixel/history goldens are unchanged.
## Frozen qualification result

All full x64 Debug, Release and ASan Debug suites pass, including 92 new assertions per configuration, existing
numeric goldens and the real sanitizer probe. All three ARM64 clean cross-builds pass against the same 446-input
freeze; native ARM64 runtime is not claimed. Skills (15), formatting (395) and all linked-image ISA controls pass.
The runtime allowlist remains unchanged. No UI timing or Photoshop comparison is qualified.

Release version: `0.1.135.0`. All 446 physical inputs, source/binary manifests, five Release images and matching
PDBs, app-local DLLs, DxUi provenance and eight gate logs are byte-preserved and hash-verified under
`.build/p1-clipboard-command-r1-qualified` before the next native change.

- Source freeze SHA-256: `4CDF1E41EF8E3C7A9BD02B34F273275C41B339ED66671E7D288EF9641E6DB07E`.
- Release application SHA-256: `A56251615D98BB524E903021A1EBFDE66AEFB0B31EFB824225C7806C0248C4F3`.
- Unchanged ISA allowlist SHA-256: `6F55A40AB0CE23E98BB10839EC68765531129EF375B00DD29B298C3DC97EE560`.

Only A5.4b closes. The archive provenance receives the implementation commit after publication.