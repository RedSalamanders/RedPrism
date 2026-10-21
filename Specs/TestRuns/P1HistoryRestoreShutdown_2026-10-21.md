# P1 Undo/Redo joined shutdown — 2026-10-21

Status: reproduced failure repaired and complete same-source local matrix qualified; publication pending. Native ARM64 runtime and P1 remain open.

## Checklist

- [x] Add four real caller cases and reproduce the exact missing acknowledgement.
- [x] Preserve/read-back the failed source, products, objects and raw output before mutation.
- [x] Implement exact-stamp acknowledgement after joined Close and strengthen held-worker proof.
- [x] Qualify repaired full x64 Debug, Release and real ASan; preserve every predecessor and 36 new facts.
- [x] Qualify ARM64 cross-builds and applicable analysis; record native runtime scope separately.
- [ ] Commit/push the completed qualified step and verify the destination SHA.

## Preserved reproduction

Actual canonical Release parent 85052 exits 1; hidden WARP child 49220 exits 5. The ISA gate and all four suites
pass first. The new fixture reports 36 checks: 32 pass and exactly four fail, each named
`joined-acknowledgement-clears-original-ticket-and-barrier` (Undo/Redo × before capture/prepared worker held).
No other Application FAIL is present. This is a failed application profile, not a full configuration pass.

The 1,896-input source freeze and source bookends remain exact. SHA256:
`82E6C454E70701367310B4E043B91EFF1C4F0462CA5F578F6E40A863D9EAC29F`.
All 3,228 archived files are read-back verified; manifest SHA256:
`B4353D90D9C10FD238182ACF3F29DD9E16E21EBD9B6B6B6C23CA5C7E5FF40BDC`.
Private evidence: `Z:\src\RedPrism\.build\P1Evidence\p1-history-restore-shutdown-r1`, attempt `hr1r0`.

## Repair and independent expectations

Close deliberately suppresses Completed. Normal Undo/Redo completion resets its ticket/stamp and finishes its
exact history stamp, while Shutdown previously omitted that acknowledgement. The repair performs those scalar
operations only after the actual producer/retirement join. It does not change restore pixels, history semantics,
admission, cancellation, residency, arithmetic, timeouts, GPU ownership or the DxUi pin.

The final fixture also requires the manual gate to remain unsignalled and an explicit shutdown-event witness.
Cancellation status or a timed-out body cannot satisfy that proof. Original fields must still exist at the
joined callback; source/history/saved state remain unchanged, no Present occurs, captured payloads retire off
owner, all pool/reservation owners retire and a second Shutdown is idempotent.

A cheaper helper prepares the runtime roster sidecar. Main finds and repairs its incorrect source-freshness
lookup: source is bound through the archived frozen manifest/content hash; only produced logs require run-time
mtimes. Separate compiler/object/link/ISA/PE/full-PDB qualification remains mandatory. All prior 12,262
Application facts and 155 stages must remain, plus exactly 36 new facts; all four same-profile suite rosters and
Application stderr remain exact. No result is inferred from prepared readers or a successful build alone.

## Other boundaries

Main also reconciles the existing distortion private-reader implementation and its 12 literal controls with
the exact archived Debug/Release/ASan source/test/registration inputs. That corrects a stale H3 note and does not
close other H3 callers, native ARM64 runtime, ordinary activation or G1. Additional shutdown record inventory
contains inspection leads only; no broader leak or lifetime qualification follows from this repair.

## Complete same-source local qualification

All seven canonical commands exit 0 against one unchanged 1,897-input source freeze:
`8DE6283A1F8B67BF5CDE356A7F800C58BEC4BEE32A356F574D7FC99C380C40E9`.
The working patch hash is `D38C492A560A72B982680435E62F293BFB0C465130016EA6253A610A552A03B8`.
Commands use `test.ps1` for full x64 Debug/Release/ASan, `build.ps1` for the three ARM64 cross-builds,
and `build.ps1 -Analyze` for x64 Release, with `-MaxCpuCount 1`. Analysis is a canonical rebuild.

| Profile | Attempt | Actual exit | Archived files | Read-back manifest SHA256 |
| --- | --- | --- | --- | --- |
| x64 Debug | `hr1d1` | 0 | 3238 | `4CE5BF98FF9D1934CBF3CDD63E0EBEB75C3178F114260AD8FED2C972532CA708` |
| x64 Release | `hr1r1` | 0 | 3241 | `6E1A11B5B4588849BEBD15499BB00D39C46AB90844E134BAD1288F04F56644B3` |
| x64 ASan Debug | `hr1a1` | 0 | 3374 | `EBE86CFDFCEA80CED84D4215BD3D86D37DC30CE233EA95DEDAE1F1D7CA12C540` |
| ARM64 Debug | `hr1armd1` | 0 | 3007 | `B17D541CA6F1DB1BBC47654B897A4220E7FB5CDE77436F1537DE633A42830218` |
| ARM64 Release | `hr1armr1` | 0 | 3007 | `25A6900F032A6715FA3324B7310B70AA3F8ADE4E5EEE2B02497C58F212903E1C` |
| ARM64 ASan Debug | `hr1arma1` | 0 | 3011 | `4F76BDA38D0F4168CECB322375A2E73510946F5634D24431511E9BDC54B31E63` |
| x64 Release analysis | `hr1an1` | 0 | 3255 | `C0A4095AF97C2C9061F2C49F3DCEB700601C41B5B542A54F32656EDBC12F4714` |

Each x64 test preserves the exact 12,262 same-profile Application predecessors and all 155 stages,
adds exactly 36 passing shutdown facts, preserves all four complete suite rosters and byte-identical
Application stderr, and passes the correctness smokes and nine harness captures. ASan includes genuine
detection and the ordinary stress/probe obligations. No focused receipt is promoted to a full run.

For every profile, Main independently verifies all 1,022 project translation-unit/compiler/object/link
records (358 Application, 136 Host, 299 Document, 145 Composition and 84 I/O), exact archived sources,
five executable/matching full-PDB pairs, and required configuration flags. The x64 ISA gates retain
their compiled controls and unchanged allowlist. Analysis records require the actual analysis switch
and plugin on every translation unit. Archive, compiler-profile and runtime-roster readers remain
separate, with the failed `hr1r0` archive intact. Format checks 1,354 files; skills validate 15.

The ARM64 builds prove compilation/instrumentation and AA64/PDB identity only. The separate native
Release run 39511716098 tests preceding source and fails a Polygonal DPI assertion; no native runtime,
ordinary activation, G1, dependency change, main merge or P1 Done claim follows from this local repair.
Documentation closeout occurs after these source-frozen commands; compiled inputs remain identical.
