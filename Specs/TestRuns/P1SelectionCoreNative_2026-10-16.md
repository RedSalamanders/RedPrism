# P1 selection core and fixture native qualification — 2026-10-16

Status: native qualification in progress; P1 remains WIP.

## Checklist

- [x] Preserve failed analysis r1053 and independently read all 1,780 archive files.
- [x] Review fixture heap repairs without changing assertions, warning limits or goldens.
- [x] Adopt private-qualified S5 model and fixture repairs in 29 code/test/project paths.
- [x] Reconcile three owning contracts; 15 skills, 1,241 formatted files and diff checks pass.
- [x] Preserve and independently read failed r1059; apply shared-helper and equivalent capacity-proof repairs.
- [ ] Finish fresh canonical Release analysis r1066 on the repaired exact source.
- [ ] Finish full x64 Debug/Release/real-ASan and ARM64 Debug/Release/instrumented-ASan cross-builds.
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
