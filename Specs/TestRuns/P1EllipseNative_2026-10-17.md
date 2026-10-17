# P1 integrated New marquee native qualification — 2026-10-17

Status: **WIP**. Integrated code is `6a7afac`; ordinary activation, G1 and P1 Done remain open.

## Exact source and expected profiles

The freeze covers 1,433 inputs, including 1,429 compiled/project/toolchain inputs and four explicit runners.
SHA-256: `3D216C65FAB7636D322B40CD5E4310E3355BB3C0EC590FB568F0D733242E7FE2`.
Expected project compiler sets: App341, Document278, Host134, composition138, I/O84 — **975 total**.
Each profile independently checks its full archive, exact compiler commands/profile flags, target architecture,
five executable/full-PDB identities and source bookends. X64 correctness additionally requires all five actual
suite streams, source-defined configuration witnesses, 22,693 Document facts and all 147 Application stages.
ASan requires real instrumentation, both stress suites and the actual isolated heap-use-after-free diagnostic.

## ARM64 Debug r1101 — qualified cross-build

Canonical `build.ps1 -Platform ARM64 -Configuration Debug -Rebuild -MaxCpuCount 1` succeeds.
Actual child 28616 exits 0, 06:10:51.202862–06:37:46.600959 UTC.
Main independently verifies all 1,504 archive files, all 975 actual compiler records, all frozen inputs,
and five exact AA64 executable/full-PDB pairs with matching CodeView GUID and age.
The artifact reader passes four positive and 22 negative source-only controls.
Archive completion SHA-256: `6EEBE8B99BFEBB810B0132A62BF553C3722C43CADFCB831C221522290DF91C5A`.
Local receipt: `.build/P1Integration/LassoHost-r1-Main/r1101-main-qualified.json`.

## Remaining matrix

- Full native x64 ASan r1100: failed resource attempt, preserved below; a fresh complete attempt is due.
- Full x64 Debug r1102; canonical Release Rebuild/Analyze r1104; full Release r1106: due.
- ARM64 Release r1103: active canonical Rebuild; instrumented ASan r1105 cross-build: due.
- Native ARM64 runtime is unavailable; cross-build evidence is never described as runtime qualification.

The separate Add/Subtract and real clipboard-contention drafts are not compiled or adopted into this source.
Both checklists retain all remaining delivery gates. DxUi's source pin/API remains unchanged; canonical
builds may rebuild that exact pinned dependency for the selected profile.

## ASan r1100 — retained disk-exhaustion failure

All five images compile, then `IsaInlineTableTests.ps1:520` fails writing the
`UnsignedStackTerminalListingRow2Seed` synthetic listing because C: is full. Runtime suites are not reached.
The runner also fails writing its completion JSON (`errno 28`); the original result is zero bytes.
The outer helper exits 1. The canonical child PID and numeric exit were not persisted and remain unknown.
No ASan correctness pass is claimed.

Main preserves frozen source, products/full PDBs, TLOGs, scanner evidence, partial control trees and actual
logs in a compressed failure archive, then independently reads every selected entry and its SHA-256:
62,585 files, 2,424,675,583 logical bytes, 532,944,126 compressed bytes.
Archive SHA-256: `AF6FF086511A4C910F9B4B6B14B48DCB6A3778FDE3157526C8544102CC676167`.
Local receipt: `.build/P1Drafts/EllipseNative-r1-Main/r1100-main-failed-preserved.json`.
The failed zero-byte completion file is retained unchanged. Storage preparation keeps the current ASan
objects available for a canonical incremental build and complete test retry; it does not waive scanner,
stress, instrumentation or runtime predicates.
