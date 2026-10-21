# P1 native gate repairs — 2026-10-13

## Qualification checklist

- [x] Preserve complete failed native r971, r972 and r973 source/product/process/log archives.
- [x] Repair the lasso ticket fixture: the worker observes its immutable JobContext; the owner checks the live original reservation before resuming that same ticket and allocating the arena.
- [x] Preserve all 39 ticket assertions; isolated ASan DocumentTests has 21,447 PASS records, empty stderr and a real heap-use-after-free probe diagnostic.
- [x] Move known capacity constants to static_assert; retain the runtime capacity and overflow assertions.
- [x] Check each absolute raster input offset against the validated available byte extent before reading.
- [x] Scope the ARM64 intentional alignment-padding diagnostic around the controller that embeds its aligned cells; layout is unchanged.
- [x] Diagnose the isolated manual relink's case-sensitive module-key mismatch against the already-reviewed `SettingsStore.obj!wmemchr` entry. Its refused report remains preserved; no scanner/allowlist change or private ISA pass is claimed.
- [x] Qualify repaired native Release analysis r976 and ARM64 Debug r977, Release r978 and ASan Debug r979 cross-builds.
- [x] Qualify full native Release r980 and Debug r981, including all 21,447 document records and 136 application stages.
- [x] Preserve complete ASan r982 scanner failure and prove the two exact inline tables against unchanged image/PDB bytes.
- [x] Qualify full native real ASan r984, its exact 21,447-record document roster, real detection probe and 136 application stages.
- [x] Integrate reviewed 63 short-stack parser controls and the generic-reader exclusion, preserving all inherited expectations.
- [x] Qualify final scanner-only native build r985 and immutable runtime-product equivalence before commit/push.
- [ ] Complete ordinary host activation, lasso/display/admission and every parent P1 delivery gate.

## Preserved failures

| Attempt | Actual outcome | Archive completion SHA-256 |
| --- | --- | --- |
| ASan Debug r971 | Build/ISA pass; DocumentTests aborts at owner-thread scheduler assertion. No full runtime pass. | `67C68EFEDBFDC2CF81B07BA616832A1050C18EBE6924BEA3B920824996B9B7F9` |
| Release analysis r972 | C6239/C6326 in capacity fixture and C6385 in raster worker; exit 1. | `AA69E44CC820E657049E290D8539141E5F29754D92120790B31CF67EFEE8FEB5` |
| ARM64 Debug r973 | C4324 alignment padding; exit 1. Successor profiles did not launch. | `CFC0E5BC1598BF0C8525E866510DDB69355CE27CCEAB3BE0D8E5994A95039B64` |

Archives are under `Z:\CodexArtifacts\RedPrism-P1\2026-10-12`, with unique r971/r972/r973 failure names.
The native x64 attempts froze 1,317 inputs at `3B2BBC9F1A0CA9A623599F24375F7AB2F8F996D9EF8D9574E8EFDF147F727FE5`.
They do not qualify the repaired inputs.

## Isolated repair proof and limits

Private `LassoTicketOwner-r1-Main/actual-asan-r3` recompiled the repaired ticket with the native ASan compiler
options and relinked the preserved document objects. All 21,447 document records and the actual ASan probe
pass with source/dependency/product bookends. Allocator-prefix statistics are configuration-specific; their
ASan expectations are independently bound to unchanged native r971 predecessor assertions and object bytes.
Numeric geometry, kernel, history and all ticket predicates remain unchanged.

The isolated link's ISA scan refuses 15 decoded VEX instructions attributed to unchanged SettingsStore.obj.
The native r971 linked images passed the same scanner. The later diagnosis below identifies the existing entry's module-case mismatch; neither a scanner exemption
nor a new private ISA qualification is inferred from the runtime result. This receipt covers the repaired
fixture only, not application behavior, ordinary activation, complete admission, native full gates or G1.

## Repaired native build gates

All four completed attempts freeze 1,317 inputs and exit 0. ARM64 is cross-compile evidence only.

| Attempt | Actual PID | Freeze / archive completion SHA-256 |
| --- | --- | --- |
| Release analysis r976 | 9096 | `CFDAD504F0D365C88C1F9DAF043CE903CBBC60B73D354863FA69519B3234303C` / `FFA2E6101E115AB799D99222FEAFA2CF77A917DBBE652215A01D2DA49C7547B9` |
| ARM64 Debug r977 | 91152 | `20F48A23F01FE57CE78CBD6A6AD5DCD558BDB1BB25BE3E86B40645AC31AACDD9` / `810E5B749C5A9C323A907BEB6B45DEC831AF7EA37828E76684D121C601ADC387` |
| ARM64 Release r978 | 96540 | `20F48A23F01FE57CE78CBD6A6AD5DCD558BDB1BB25BE3E86B40645AC31AACDD9` / `E394B87E70388349F3E310AA77BDD98447FF33D08A2CEA281C6C5364F28736F1` |
| ARM64 ASan Debug r979 | 10224 | `20F48A23F01FE57CE78CBD6A6AD5DCD558BDB1BB25BE3E86B40645AC31AACDD9` / `8B0A4023FD8F97C9521CE533875BAAE4B43E41AF9BAC430341D73825BB64D7D3` |

The preserved private manual link differs in PDB module spelling (`SETTINGSSTORE.OBJ`), while native PDBs
spell `SettingsStore.obj`; the scanner's exact key lookup explains the isolated refusal. The first flagged
instruction is +0x138 within the reviewed runtime intrinsic, not its procedure start. Native full ASan remains
required. No original allowlist entry, surrounding consumer code or scanner classification rule changed.

## Full native runtime profiles and scanner successor

Release r980 (PID 5408) and Debug r981 (PID 94916) exit 0. Their complete archive hashes are
`C89B71C3A6EF34D253E8BA10385AF692D1ECBC7172B0DAD7BDAC1B5E4E55E69B` and
`D21A2DAFF8E192B9D4E3C338F02D3CFFB8D7FA5497840B392F3285EA1583B614`.
Both retain all 21,447 document records. Debug allocator/cancellation statistics are bound to the unchanged
r966 precursor; functional expectations are unchanged. No timing/G1 claim is inferred from suite success.

ASan r982 (PID 37692, exit 1) compiled and linked but failed the first image scan, before any runtime gate.
Its complete archive hash is `12D45F94C34937421AF6C4777546AEBE1B5DB7C805AA4F9762B186CC4123A36B`.
Two six-DWORD switch tables in Menu have a short unsigned stack guard, signed index load, RCX image-base
dispatch, a distinct default, and RET/one-byte NOP before the exact anonymous PDB table/procedure end.
Linear decoding reports VEX in those data bytes. The new proof checks every guard/body/terminal/target byte
and decoded boundary, exact table ownership/extent, and direct control-flow exclusions. It does not exempt
an object or function. Rescan of the exact preserved image passes: image
`6E3A7D7E4225D7AB78A50647174D1799CA25D9E587DC8ACD46D850849FB6A85F`, PDB
`0ADE3B4F9363E4F9ED8AE30AB02B210D7D7B665D6DDF161261A96957AA94718D`.
The allowlist remains `7870AF49195841A9B9B88DFD15378D550B127754683666C83AED83753FD80E1E`.
Independent synthetic controls and the canonical full ASan successor are due before this step is committed.


### Retained strict-proof fixture refusal, r983

Canonical ASan r983 (PID 89504, exit 1) passed all five linked image scans, then refused the existing
`TerminalDwordValid` synthetic fixture before runtime. Its old listing omitted the procedure body and
RET/NOP rows required by the stronger exact-form proof. The complete archive hash is
`4E80A75DD19426B24684C58F9CE0E93463BCDF8E246E9264FC5AF826FDCF2CA8`.
Main supplies complete matching scalar body and terminal rows. The case target is an independently encoded
two-byte NOP so the existing +1 invalid target/default remains inside an instruction. All original acceptance,
refusal, vector-data counts and executable-vector expectations remain unchanged. Full inline controls pass,
including 37 terminal DWORD controls and 63 separate-short-default controls. New independent controls and
full native runtime successor remain due; r983 has zero application stages and is not sanitizer runtime proof.


### Independent current-source control refinement

Main reviews the cheaper 61-control draft and corrects no-op contribution mutations, invalid-target cases
that had removed all vector-looking bytes, and branch cases that originally targeted valid body code.
Rejected draft diagnostics remain preserved. Main adds the lowest bound and contradictory table bytes:
63 controls pass privately (five accepted, 58 refused). A genuine vector at the table end exposed a broader
forward-reader gap. The private correction selects the exact short form before the generic reader, preserves
the distinct long-JA form, and refuses unmapped candidate probing without changing older parser behavior.
All inherited inline controls preserve their original expectations. Native integration and its full successor
remain required; the earlier r984 native run uses the preceding scanner input and cannot qualify this change.


### Full native sanitizer qualification, r984

Canonical ASan r984 (actual child 104952) exits 0 at 08:46:18.460497 UTC. Its freeze is
`96AE67C5A444A3E65D219AFE9C501E626742115992A83305FE2A7B13B30CA90D` and stdout
`45753F18325503FF697CA33B9CB0D15D89D8F38442F6A94E720A393FD1B5DB1F`.
All 21,447 document records match the independent Release functional reference with the unchanged r971
ASan statistic witness. Concurrent raster/worker stress, the real heap-use-after-free detection probe,
main/worker crash probes, all 136 hidden WARP stages, benchmark/parser smoke checks and harness captures pass.
The complete 1,697-copy archive hash is
`930592802BB9C35C707B6331D4B88B3E1C1B850A659986F67B5C3801E404DA53`.

The final private control review subsequently tightens only the scanner/fixture harness, not C++ or HLSL.
Final native r985 build scans all five images and executes the complete existing/new ISA controls. Reuse of
r984 runtime evidence requires exact source/compiler/dependency and output-product equality; its canonical
run is preserved, and r985 is not described as another full runtime run. P1 activation and G1 remain open.


Final native tool build r985 (actual child 77924) exits 0 at 08:58:09.727789 UTC. Its freeze is
`39FCBB45F88D3C19BD4DBDC81CC90A62091F5E9B7BBF33FCFFDC440ECA7DDF37`; all five linked scans,
63 new controls and every existing compiler/parser control pass. All 1,194 compiler/resource/machine inputs
and 14 executable/PDB/DLL products remain byte-identical to r984. The complete tool archive hash is
`029BB72920557AE65D9EB8167F6C59DFED33384F1C8308D5A08DF8AD72F7CC0A`.
The unchanged C++ candidate retains Release r980, Debug r981 and full real ASan r984 runtime qualification;
analysis and ARM64 evidence above remains scoped as stated. Formatting (1,166 files), all 15 skills and
normal-policy Git diff checks pass. This closes the native foundation/scanner repair step. Actual lasso host
migration, every parent delivery area, ordinary activation and G1 remain open.
