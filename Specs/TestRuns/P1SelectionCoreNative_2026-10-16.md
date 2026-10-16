# P1 selection core and fixture native qualification — 2026-10-16

Status: native qualification in progress; P1 remains WIP.

## Checklist

- [x] Preserve failed analysis r1053 and independently read all 1,780 archive files.
- [x] Review fixture heap repairs without changing assertions, warning limits or goldens.
- [x] Adopt private-qualified S5 model and fixture repairs in 29 code/test/project paths.
- [x] Reconcile three owning contracts; 15 skills, 1,241 formatted files and diff checks pass.
- [x] Preserve and independently read failed r1059; apply shared-helper and equivalent capacity-proof repairs.
- [x] Qualify fresh canonical Release analysis r1066: all linked images, compiler records, complete archive and independent checks pass.
- [x] Qualify full native Release r1067 on the same source, with all 22,010 Document facts and 142 Application stages.
- [ ] Finish full x64 Debug/real-ASan and ARM64 Debug/Release/instrumented-ASan cross-builds.
- [ ] Independently read actual results, complete archives, compile records and five PE/PDB pairs per profile.

## Failed predecessor retained

Canonical analysis r1053 runs from 13:46:00.440446 to 15:13:15.262472 UTC, PID 756, exit 1, on 1,380 inputs.
Exactly two project-code C6262 stack diagnostics appear (with warnings-as-errors C2220):

- `Application.Tests.StrokeSampleParity.cpp`: 16,464 bytes against the 16 KiB budget.
- `ClipboardPasteTests.cpp`, `SerialAndStaleAuthorities`: 131,752 bytes against the 128 KiB budget.

Its coordinator joins with failure and never launches r1054–r1058. No SAL/ISA/runtime pass is inferred.
Main verifies all 1,780 archived files and the original source bookend before edits.
Archive: `Z:\CodexArtifacts\RedPrism-P1\2026-10-12\P1-LassoHost-x64-Release-r1053-failed-20261016`.
Source manifest: `F37E9C1D669D0E39C8D12438BDB6421A39C31048391348A76B8E955E300B1995`.
Evidence manifest: `79A9396A67E081D05CCE6D2B5190CA45FA96B01A0E3E991D7C91B55578CE7B7D`.
Completion: `BBDC643DEA8CE37C65E3BEC442B59BC20AE820BB5EF7B410545133A0DDEA1ACE`.
Actual stdout: `A48524C687DF17E1896173F643F9A03317BF45D195E7AB7268B59BEAF4C38BEF`.
Actual stderr: `65EB0966008C0008E13F4068BD571CDC13474AC3E0D65B2A90AACEA11E36F604`.

## Reviewed repair and new source

The brush parity reference and clipboard authority scenarios heap-own their `DocumentSession` instances.
Direct prvalue construction avoids a large caller temporary; the explicit moved-session test still moves its
source. Main reverses only those ownership edits and the explicit memory include to reproduce the original
fixture source byte-for-byte. No assertion, label, tolerance, case, warning limit or golden is changed.

The corrected private S5 source contributes 147 independent model facts, retaining all 21,863 predecessors;
[its Release/real-ASan receipts and controlled parity failure](P1DeferredSelectionCore_2026-10-16.md) remain
separate from native integration. Actual pointer/host dispatch is unwired.

The combined reviewed adoption contains 29 code/test/project paths, including 13 new inputs and eight new
compile records across the five native images. Fresh r1059 starts at 15:36:45.503140 UTC through canonical
`build.ps1 -Platform x64 -Configuration Release -Rebuild -MaxCpuCount 2 -Analyze`.
Its 1,393-input freeze is `D9245D46C864F752672E58FE27AB3BE1D5404ADCF1B1784093E360B05C9B885E`;
ARM64's separately pinned runner yields `5E00638793B16CC5E3D3B7554DEF7EE1BBF8B0ED0E7C77331F160A4B8F6B8740`.
Expected actual compile commands: 953. Expected Document facts: 22,010. Application stages remain 142.
These expected counts are source-derived, not successful-run evidence. No fresh gate is yet claimed.

Ordinary activation, complete admission/retirement, G1, native ARM64 execution and P1 Done remain separate.

## Second failed native analysis retained

r1059 actually exits 1, PID 84668, at 16:09:23.291814 UTC after starting at 15:36:45.503140.
Main verifies all 1,821 copied inputs/evidence and all three manifest/completion files. The coordinator joins
with failure; r1060–r1065 never launch. No SAL/ISA/runtime result is inferred.

Three subset projects (Host, I/O and Composition) report the missing `TranslateSelectionLoopsForWork` link
symbol. The Document capacity fixture reports C6239/C6326 for a compile-time constant combined with a runtime
condition. The earlier two C6262 stack-frame diagnostics are absent in this actual run.

Archive: `Z:\CodexArtifacts\RedPrism-P1\2026-10-12\P1-SelectionCore-x64-Release-r1059-failed-20261016`.
Source manifest: `23D95A3F8E054891EE79C53C3900AFEB41808A865B2622D4E5DD71132508F0F6`.
Evidence manifest: `626ABAE1E85A8734A8C4B5E23AD8C9FE3334981EF67B4CF07531D56E8CB7F9ED`.
Completion: `2BCA2856C75ADDF90D1FE81917C028618D2B003408CDFB78B650D2A958E9C075`.
Actual stdout: `283890560D126EB1A59B6A0521B12EC457411514F616567843DCD63354AC8D0C`.

## Corrected integration under validation

Main extracts the shared builder/free translation helper verbatim into `SelectionGeometryWork`, already
compiled by all five native projects. Nudge uses that same helper. No source input or compile-record count
is added. The exact 512-capacity predicate becomes `static_assert`; all 37 runtime labels and the Begin check
remain. After reversing that placement, C++ tokens and string literal contents match the original fixture.
The first byte comparison refusal reflects only clang-format joining the shorter call; it is retained.
No warning limit, suppression, golden, dependency pin or ISA policy changes.

Fresh analysis r1066 starts at 16:16:09.720813 UTC through canonical Rebuild/Analyze with two build workers.
Source freeze: `5FF8F7DD06F1F2A67751496DB145884E1A5628E837F2C738CBC7713B91863DE6` (1,393 inputs).
ARM64 runner freeze: `2A7A8EBD71D3C13AC11BCC0A81DBDEFECC77F824424BE24455A14169860C7BD9`.
Source-derived expectations remain 953 compiler records, 22,010 Document facts and 142 Application stages.
The strict S5 reader's source pin changes only for the equivalent capacity fixture; its 147 labels and four
summaries remain identical, and all 22 source-only controls pass again.

After analysis and independent archive/readback pass, the serial plan is Release r1067, Debug r1068,
real ASan r1069, then ARM64 Debug r1070 / Release r1071 / instrumented ASan r1072. These results remain due.
A separate private actual-host scalar closure lifetime implementation and ten-case/71-fact fixture are being
compiled. They are not adopted and do not activate the physical-press route or qualify native delivery.

## Canonical analysis r1066 qualified

Actual analysis exits zero, PID 91008, 16:16:09.720813–16:56:46.461262 UTC. Main reviews independently verified
1,810 copied inputs/evidence, the source bookend, all 953 actual compiler records, all 1,389 compiled/project/
toolchain inputs, five linked zero-violation ISA reports, eight negative scan controls, SAL controls and five
matching full executable/PDB pairs. Both earlier analysis failures remain retained.

Source manifest: `AF64CD26830B47E200B63DEEEE406C7A1A4AB756A5CCA77BF1C2B2E34F8EE4B4`.
Evidence manifest: `EB0C9499D8FFCEDDEC887BBB8C9C90482BBFBD25C628F36B0FFBACFC19B87F5E`.
Completion: `8308062428B789236B4E2BBE670B9C252C5C10F2ED9ADB80E8A35AAEE77E9817`.
Actual stdout: `6F9936AB70B65B27697BA9D3B378F89FAC5DD1C2B57A12ACEA023368CA89A7D7`.
Actual stderr is empty. Archive:
`Z:\CodexArtifacts\RedPrism-P1\2026-10-12\P1-SelectionCore-x64-Release-r1066-passed-20261016`.

Fresh full Release r1067 starts at 16:57:18.870258 UTC on that same source freeze. Debug r1068, real ASan r1069
and ARM64 r1070–r1072 remain pending. This analysis checkpoint qualifies compilation and static/linked checks;
it does not substitute for the runtime or remaining P1 delivery gates.

## Full native Release r1067 qualified

The canonical test parent exits 0, PID 51300, 16:57:18.870258–17:21:28.993496 UTC, on the same 1,393-input freeze. Main independently reads all 1,798 archive copies and the source/evidence/completion manifests; the retained compiler and profile readers verify 953 actual compiler records, 1,389 compiled/project/toolchain inputs and all five exact executable/PDB pairs. Runtime retains 22,010 Document assertions, 142 Application stages, 9,023 Host PASS records plus the Hue summary, 7,391 composition and 1,953 I/O facts.

Completion: `A717705914B94C13AAD87745C5AC3C23153CDA876ABA3ABA86EDAD0FA7CB8E89`. Actual stdout: `E6D165B5CE9B68424716C376BCF4E985AC8A47712E91F30D9F55D7D18DCB9F7E`; stderr: `BEA23A660A3F34C4CC4776B9AB4CD42BE9C6036F93523E8AB1D8652CF4D5648D`. Main receipt: [`r1067-main-qualified.json`](../../.build/P1Integration/LassoHost-r1-Main/r1067-main-qualified.json). Debug r1068 is running; real ASan r1069 and ARM64 r1070–r1072 remain due. Physical press routing, ordinary activation and G1 are separate open gates.

## Debug r1068 wrapper refusal and fresh r1073

The r1068 canonical test log reaches `All tests passed`, but its wrapper does not persist the numeric native-parent result. Its snapshot contains 1,395 inputs: the original unchanged 1,393 plus the helper's two temporarily misplaced, uncompiled draft files. Moving those files into their assigned private folder causes the final input check to throw `FileNotFoundError`. The matrix stops and never launches r1069–r1072. No successful Debug numeric exit is inferred.

Main preserves 1,822 input/evidence files, all five linked products/PDBs, compiler records, scans, runtime logs and screenshots. All original 1,393 inputs match; neither extra file appears in any of the five project lists. Completion `C0ADB5F38F5C540D8A4B65BDFE3C1466E73B2458C9130370D1BEC1A3ECC8B155`; [receipt](../../.build/P1Integration/LassoHost-r1-Main/r1068-wrapper-refusal-archive.json).

Fresh r1073 runs canonical `test.ps1` with its normal incremental Build on the clean 1,393-input snapshot. Its 953 existing objects and linked products are hashed before dispatch. All full tests, ISA controls, probes and screenshots run again; this is not a skip-build or runtime-only pass. The transport-only freeze is `C6E2E92B2B51F54877E00E878ED0E41BAA08445479E0299F016406EBB0FF35FC`; all 1,389 compiled/project/toolchain inputs remain identical. After its independent checks, the serial controller proceeds to real ASan r1069 and ARM64 r1070–r1072, reclaiming only qualified, regenerable `.obj` caches after each archive closes. Exact executables, PDBs, sources, logs and compiler evidence remain retained. Native ARM64 execution remains unavailable.

## Clean-snapshot Debug r1073 failed; successful-publication gate repair

The actual canonical Debug parent exits 1 (PID 68136; 18:07:56.624740–18:22:23.356457 UTC).
Its Application child exits 5 after 134 stages. The sole recorded failing predicate is
`clipboard-host.copy-foreign-replacement-after-close.replacement-after-close-leaves-stale-private-receipt-preference`.
At that callback the actual gate reports zero closed publications and zero receipt placements; the independent
foreign DIB installation and subsequent literal Paste controls pass. Main reviews the helper's verifier and
its complete 1,820-file hash/size readback. Completion: `85DFB1571263B3865F84592586E9F47645D87A472A4555018FBA1C38D986832F`.
ASan r1069 and ARM64 r1070–r1072 never launch.

Source inspection identifies a concrete harness error: `CopyPublished` fires after every Publish attempt,
including Busy, before the original-ticket retry has published or closed anything. The repair adds a separate
`CopyPublicationSucceeded` phase, selected only after Publish returns Ok. The foreign-replacement fixture now
forces two busy attempts and independently requires one successful closed publication, all three formats and
one gate visit. All original after-close, literal Paste, history and refund assertions remain intact. Native
fault fixtures retain their attempted-publication phase. This establishes a reproducible regression boundary;
the old log does not identify which native call first caused its unavailable clipboard.

The initial r1073 transport preflight hash was computed with LF JSON; its actual Windows JSON uses CRLF.
That wrapper metadata discrepancy is separate from the real nonzero test exit. Any subsequent source-bound
validation must verify the exact row contents and actual serialized file bytes before comparing a fresh hash.
Fresh Debug/Release/ASan, analysis and ARM64 validation of this repair remain due.

## Native successful-publication repair Debug r1074 qualified

Canonical Debug `test.ps1` exits 0 (PID 86420; 18:41:05.229015–19:00:03.381006 UTC).
The forced two-busy-attempt regression passes with one successful closed receipt/PNG/DIB publication;
all original after-close foreign Paste, literal pixels, history and retirement assertions remain intact.
Runtime retains 22,010 Document facts, 142 Application stages, 8,272 Application stdout assertions,
7,613 stderr assertions, 9,023 Host PASS facts plus Hue summary, 7,391 composition and 1,953 I/O facts.
The source-only delta adapter proves all 27 original `expect` calls are unchanged, checks the new fact at
its exact position and the summary change from 285 to 286, then projects only that verified addition for
immutable predecessor readers. All eight positive/rejection tests pass independently.

Two reader-only refusals remain preserved: missing dataclass module registration, then an omitted unchanged
S5 `expectations.json` dependency. The corrected fresh reader checks the existing actual successful log;
no native test or golden is rerun/rebaselined. Main verifies all 1,822 archived copies, all 953 compiler
records, 1,389 compiled/project/toolchain inputs and all five exact executable/PDB identities.
Source freeze: `E5438956D876ED5E8122E454F239916E09FD67CCBFCA3CB571BFA323F901A693`.
Completion: `E594F2479334251EE5694C5CF63A78678A8764E3779CE463E302AD61F27C1CFA`.
[Main qualification](../../.build/P1Integration/LassoHost-r1-Main/r1074-main-qualified.json).
Skills (15), format (1,241 files) and diff checks pass.

Future archives use `.build/P1EvidenceArchives/2026-10-16/` on C: to avoid depending on Z: headroom.
Real ASan r1075 is qualified on the same frozen source. ARM64 Debug r1076 has started through the
`ClipboardPhaseNative-r5-Main` controller; ARM64 Release r1077, instrumented ASan r1078, Release analysis
r1079 and complete Release r1080 remain pending. No native ARM64 runtime, ordinary activation, complete P1
delivery or G1 is claimed.

## Native real-ASan r1075 qualified

The canonical x64 ASan Debug parent exits 0 (PID 96624; 19:04:08.113067–19:44:12.473715 UTC) on the same
1,393-input source freeze `E5438956D876ED5E8122E454F239916E09FD67CCBFCA3CB571BFA323F901A693`. Main and the
independent receipts agree on the actual exit, freeze, archive completion, 953 instrumented compile records,
1,389 compiled/project/toolchain inputs, and 1,782 archived files. The independently compared PE/PDB receipts
agree on all five exact executable/PDB hash pairs. Runtime counts are 22,010 Document records, 142 Application
stages, 8,272 Application stdout assertions, 9,024 Host PASS records, 7,391 WARP records and 1,953 I/O records.

The source-only ASan annotation adapter checks three positive and eight negative controls; its two ASan
poison/unpoison assertions are retained in the existing full actual stream. The source-only recorded-brush
callback adapter checks four positive and eight negative controls against that same stream, accounting for the
four command acknowledgements legally interleaving with the recording-stage acknowledgements. Neither adapter repeats runtime.
The two reader refusals are retained in the qualification receipt.

Completion SHA-256: `D762C3FD62ABD67E3EF0A29149B37347113C2E9290C16166BF54F3C8EB3896A8`.
Main qualification: [`r1075-main-qualified.json`](../../.build/P1Integration/LassoHost-r1-Main/r1075-main-qualified.json);
independent roster, archive/artifact and PE/PDB receipts:
[`roster`](../../.build/P1Integration/LassoHost-r1-Main/r1075-independent-roster.json),
[`artifacts`](../../.build/P1Integration/LassoHost-r1-Main/r1075-independent-artifacts.json),
[`PE/PDB identities`](../../.build/P1Integration/LassoHost-r1-Main/r1075-independent-pe-pdb.json).

This records native x64 ASan test evidence for the frozen candidate. The separate private host/ownership
changes remain unadopted; physical press routing, ordinary activation, G1, P1 Done and native ARM64 runtime remain open. ARM64 r1076–r1078, analysis r1079 and
Release r1080 remain separate gates.

## ARM64 disk-full failure retained and Debug r1086 qualified

The actual r1076 ARM64 Debug parent exits 1 (19:58:21.986883–20:13:46.840867 UTC).
The compiler reports C1088 while flushing its temporary intermediate: no space left on device. Main verifies
all 1,463 archived source/evidence files and the source/evidence/completion manifests. No later profile runs
from that controller. Completion: `27CAD5A52CDA5BA4BC5CA3C2D6C3DC17DB1B0E8E00C72247D452376A9C31A852`.
[Failure verification](../../.build/P1Integration/LassoHost-r1-Main/r1076-main-failed-verified.json).

To recover headroom, the closed r1074/r1075 archives move to the explicit Z: artifact directory only after
all 1,825/1,785 files, including manifests, compare by length and SHA-256. Junctions preserve both original
archive paths. Every product, PDB, source, log and manifest remains retained with its original completion hash.
A recovery-controller preflight refuses an attempt outside the old transport allowlist before any native
child launches; its log is preserved. A separate serial runner replaces exactly one of the 1,393 transport
rows; every one of the 1,389 compiled/project/toolchain inputs is unchanged.

Fresh canonical ARM64 Debug r1086 exits 0 (PID 83260; 20:16:53.745329–20:17:24.591034 UTC) with one build worker.
Main checks the independent 1,463-file archive verification, all 953 actual compile records, every unchanged
compiled/project/toolchain input and all five exact ARM64 PE/PDB pairs. The source freeze is
`C7D41B2E802CE18215D7F043A1EBE08D1AE0959857C1056FC9628488CAC0ED4B`.
Completion: `7E2AEE6A3D0C7A9538384DF39414F45DD19A060E22DBC0AE533210672873038C`.
[Debug cross-build qualification](../../.build/P1Integration/LassoHost-r1-Main/r1086-main-qualified.json).

Only after qualification, 953 regenerable ARM64 Debug objects are hashed and reclaimed; all linked products,
PDBs, source and evidence remain. ARM64 Release r1087 is building, followed by instrumented ARM64 ASan r1088,
x64 Release analysis r1089 and full Release r1090. Native ARM64 runtime remains unavailable; this is
cross-build evidence only. The private gesture APIs and tests remain separate, unadopted work.
