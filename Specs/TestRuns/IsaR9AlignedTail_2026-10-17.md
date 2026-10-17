# Exact R9-byte switch alignment recognition — 2026-10-17

Scope: build-time ISA parser repair found during the selected Polygonal host build. P1 remains WIP.

## Original result preserved

The fresh 349-TU Release compile and link succeeded. ISA child PID 103132 exited 1 at `0x14024c200`
in `Application.obj`: DWORD switch data decoded as `vfmadd213bf16`. Main independently verified all
1,741 archived files, 349 compiler records/objects, source/dependency bookends and the matching x64
executable/full-PDB pair before correction. The original failure remains intact.

Build archive completion SHA256: `0EDD164F19F63018B6C658F5F181A727CB47C638E49A8A9D2D140AF2842D7991`.

## Correction and tests

`ReadGuardedR9ByteSwitchTable` now recognizes its exact `RET; CALL rel32; INT3; 66 90` terminal sibling.
The existing one-byte NOP form remains covered. A short JNE into the shared JA is accepted only with
an immediately preceding equal-maximum R9B comparison. All guard, dispatch, tail and complete body
rows retain PE/listing checks; anonymous data start, guard-sized terminal extent and case/default
boundaries retain the same owning PDB/contribution checks. The new sibling corroborates reported
vector-data bytes. Only the proved table receives data status.

The 109 added controls pass: 7 accepted and 102 refused. Cases include bounds 1/12/127, matching short
and long comparisons, split LOCK/XADD, wrong guard/index/base, malformed/missing/unknown rows and
tails, wrong padding, symbols/extents/targets, and real body/target/adjacent vector instructions.
DxUi and all ISA allowlists are unchanged.

The complete Release parser stages emit 13,680 PASS records. Original child PID 85580 then fails at the compiler-control invocation because the runner corrupted the MSBuild path; this failure is preserved. The exact unchanged compiler-control tail runs separately with the corrected path, child PID 57920 exits 0, and contributes the remaining passing/misplaced tier, runtime-owner and missing/mismatched-PDB controls (13,691 combined PASS records). Main verifies exact tail lineage, all frozen inputs and every raw-log hash. Parser/compiler/runtime code is not replayed to correct this invocation error.

## Unchanged failed-image readback

Actual rescan PID 100496 exited 0: 1,471,669 decoded instructions, 488 runtime instructions,
9 inline-table vector-data rows and zero violations. The original image was not rebuilt.

| Identity | SHA256 |
| --- | --- |
| Executable | `D6E8731BBA003A865EDDB5C815BBE9D54BAB2C01ED5167D6376EFC880239460A` |
| Full PDB | `92E20CFC36BFB8DB4FF5F158126CB9EF9BBE7491278DF6F42E7D78C52E71A803` |
| Unchanged allowlist | `7870AF49195841A9B9B88DFD15378D550B127754683666C83AED83753FD80E1E` |

## Retained evidence and limits

Local `.build/P1Drafts/IsaR9AlignedTail-r1-Main/` retains independent controls, joined child results,
complete raw logs, frozen-input/hash bookends and the actual rescan.
`S5PolygonalPhysicalHostRelease-r1-Main/main-failed-isa-preserved.json` identifies the original full
failed artifact archive; `main-build-qualified.json` records its corrected scan separately.
Main checks every retained archive byte and the exact PE/PDB/profile lineage. Formatting, all 15
skills and diff checks pass. Native host runtime, ordinary activation, latest full profile/ARM64
qualification and measured G1 remain separate obligations.
