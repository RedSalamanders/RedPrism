# P1 selection core and fixture native qualification — 2026-10-16

Status: native qualification in progress; P1 remains WIP.

## Checklist

- [x] Preserve failed analysis r1053 and independently read all 1,780 archive files.
- [x] Review fixture heap repairs without changing assertions, warning limits or goldens.
- [x] Adopt private-qualified S5 model and fixture repairs in 29 code/test/project paths.
- [x] Reconcile three owning contracts; 15 skills, 1,241 formatted files and diff checks pass.
- [ ] Finish canonical Release analysis r1059 on the new exact source.
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

## Observed integration diagnostic (run still active)

r1059 reports unresolved `TranslateSelectionLoopsForWork` from `SelectionMoveWork.obj` in `HostTests`.
The private Document project included the nudge TU that defined this helper; subset projects compile closure
without that TU. Main prepares a two-file extraction of the builder/free helper into `SelectionGeometryWork`,
which all five native projects already compile. The builder block is preserved verbatim; no new project input,
assertion, threshold or warning policy is needed. This remains a draft while the native process finishes.
No failed exit, archive completion or subsequent validation is inferred from an intermediate diagnostic.
