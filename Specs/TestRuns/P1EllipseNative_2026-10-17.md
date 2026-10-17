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

- Full native x64 ASan r1100: failed resource attempt preserved below. Fresh full r1107 is independently qualified below.
- Full x64 Debug r1102; canonical Release Rebuild/Analyze r1104; full Release r1106: due.
- ARM64 Release r1103: independently qualified below; instrumented ASan r1105 cross-build: active.
- Native ARM64 runtime is unavailable; cross-build evidence is never described as runtime qualification.

The separate Add/Subtract draft remains uncompiled. The real clipboard-contention draft has prior focused
compilation, with its current full host build/runtime due after convention fixes. Neither draft is adopted into this source.
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

## ARM64 Release r1103 — qualified cross-build

Canonical `build.ps1 -Platform ARM64 -Configuration Release -Rebuild -MaxCpuCount 1` succeeds.
Actual child 41444 exits 0, 06:59:28.024071–07:28:05.707424 UTC.
Main independently verifies all 1,504 archive files, all 975 actual compiler records, the unchanged freeze,
and five exact AA64 executable/full-PDB pairs with matching GUID/age. No native ARM64 runtime is claimed.
Archive completion SHA-256: `27DBF60D20C4BE2004430D6BA0F64D33C0403C7908AFE0CAF82ACC55C7003A76`.
Local receipt: `.build/P1Integration/LassoHost-r1-Main/r1103-main-qualified.json`.

## Fresh ASan retry r1107 — preparation, now qualified below

The new runner adds only the explicit r1107 attempt and its filename; the canonical command, original TEMP,
all test predicates and all 1,429 compiled/project/toolchain inputs remain unchanged.
Its 1,433-input freeze is `83547287D310843B3956CEBE7FCA2E9F0FCBB355147874171E08030359DF7ED2`.
The separate artifact reader passes four positive/22 negative controls; full runtime readers retain their
source-derived expectations. The original r1100 archive and zero-byte completion file remain unchanged.
Closed caches are losslessly compressed with full hash readback; one closed private cache is relocated to a
verified copy with its original path retained by junction. The existing 6 GiB C:/12 GiB D: dispatch guards pass.
This preparation is not runtime, performance, ordinary-activation or P1 Done qualification.

## Full x64 ASan r1107 — independently qualified

Canonical `test.ps1 -Platform x64 -Configuration "ASan Debug" -MaxCpuCount 1` exits 0.
Actual child 55360 runs 07:20:22.821751–07:41:13.758496 UTC. The freeze is the separately pinned r1107
transport above; all 1,429 compiled/project/toolchain inputs remain the adopted `6a7afac` source.

Main independently verifies all 1,833 archived source/evidence files, all 975 real instrumented compiler
records, five exact executable/full-PDB GUID/age pairs and zero ISA violations. Strict runtime readers verify
22,693 Document facts; 9,024 Host PASS records; 7,391 composition records; 1,953 I/O records; and all 147
Application stages with 8,577 stdout / 7,613 stderr assertions. Both canonical concurrent stress suites and
the genuine heap-use-after-free detection diagnostic pass. Diagnostic SHA-256:
`381992E50BB9B581AA7A4C3352E195C39F9EEA909CD312158502994899217C25`.
All nine canonical harness PNGs are archived and independently checked against their exact capture rows.
Functional benchmark smoke checks do not establish G1 performance while compilation is active.

Before archiving, the private archive recipe is corrected to strip the configuration space in PNG filenames,
matching unchanged `test.ps1`. Its first extra screenshot reader refuses the OEM build-banner byte C9 when
trying to decode the whole mixed-encoding parent log as UTF-8. The refusal remains retained. Readback resumes
against the same archive by isolating raw screenshot rows before strict decoding; the original artifact,
profile, roster and source predicates remain unchanged. No build, runtime or archive is repeated.

Archive completion SHA-256: `0F37529608C2FAD7FBD9CC06BF9A84AA44BD9E3F7AC2CE7D2021912BCD241D20`.
Local receipt: `.build/P1Integration/LassoHost-r1-Main/r1107-main-qualified.json`.
ARM64 ASan r1105 remains active. Ordinary activation, remaining delivery work, measured G1 and P1 Done remain open.
