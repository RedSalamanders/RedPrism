# Direct RBX guarded terminal-table scanner regression — 2026-10-18

Status: final 120-control and unchanged-image qualification passes; complete final scanner suite in progress.

## Captured failure and Main correction

The older private Layers geometry diagnostic Release image reports a vector at VA `0x1402b13b8` in
`ApplicationAdjustmentClosureJobs.obj`. Bytes `c5 fb 2a 00` are DWORD RVA `0x002afbc5` in a six-entry switch
table in `Application::RunAdjustmentClosureSelfTest`. The separately fixed/control startup images already
pass their ISA checks; their different link layouts do not establish this scanner correction.

Main proves the exact encoded CMP EBX / unsigned JA / RDX image-base / RBX-indexed DWORD / ADD / JMP
form. EBX comparison alone does not bound full RBX: require a reaching sign-extending DWORD producer,
explicitly modeled register effects, and refusal of entries bypassing the producer or guard. The complete
PE-matched contiguous/nonoverlapping body, exact same-owner PDB data extent, external CALL/INT3 and all
case/default boundaries must agree. Unmodeled effects refuse. Only exact table bytes are data; real vectors
remain gated. DxUi and the runtime allowlist are unchanged.

## Independently checked outcomes

- All 118 portable controls pass with the initial Main implementation. Main corrects the cheaper draft's
  register-extension bits, overlapping injected rows, terminal CALL locality and expectations for valid
  alternate producers, body starts and guard-preserving entries. The original unsuccessful streams remain.
- Final review finds an overly broad register-effect fallback. Two encoded VMCALL/INT controls reproduce
  its incorrect data proof, then refuse with the explicit final effect set. Their bytes never execute.
- All **120 final default-executed controls pass**, preserving the exact 118 predecessor labels and both
  new cases. This includes full-RBX writes, actual guard bypasses, missing/duplicate/unknown/PE-mismatched
  rows, PDB/terminal/target contradictions, and real body/adjacent VEX/EVEX beside successfully proved data.
- The complete preceding scanner suite preserves **13,892 exact PASS records**, including compiled controls
  and the 94 unsigned-tail controls. The full suite with the final stricter implementation is still running;
  its exit and full record comparison will be appended after completion.
- The **same original image** passes the final scanner: 1,487,931 instructions, 488 allowed runtime vectors,
  five proved data rows and zero violations. Original EXE, full PDB, report and decoder inputs remain exact.
  Independent PE/PDB identity: GUID `C616B600-76A5-459C-8175-48AA1594B521`, age 1.
- Skills: 15 pass; formatting: 1,311 files pass; diff checks pass. No C++ product source changes in this step.

## Immutable records

Cohort: `D:/RedPrismBuildCaches/.build/p1-isa-rbx-direct-guard-r1`.

| Record | Identity / result |
| --- | --- |
| Final `Build/IsaScan.cs` | `941E01060A2738925C857657EA6B60AA607DAA9E279F0F3B30B2F253EAA0EEB5` |
| Final `IsaRbxDirectGuardTests.ps1` | `F9E40EE7C6DD70D463BBA9800FD244C436F721F2929C3E074CB8D8B80F33FDD3` |
| Prior complete scanner log | `4D85F1C134F5DE09332083D19BBD9B8F3436289E371D66BC0AE6646CBDFCA164` |
| Original EXE | `D1D1D10B8D1BDE34D88C85DDC47C1134BF8766752DB9E3A509B0F8E1FB081C7B` |
| Original full PDB | `B4D786619DD9D2F567A297D900F9B08CACB0A764D7A3F9BF1AE1AD6ED2604ECD` |
| Runtime allowlist | `7870AF49195841A9B9B88DFD15378D550B127754683666C83AED83753FD80E1E` |

Main receipts: `main-new120-qualified.json`, `main-original-final-qualified.json`,
`main-predecessor-qualified.json`; source copies: `Sources-r3` and `Sources-final`; unknown-effect controls:
`ImplicitEffectControls`; retained fixture refusal: `agent-r3-first-run-refusal.log`.

This tool correction makes no new native Application, native ARM64 runtime, G1, ordinary activation or
P1-Done claim. Latest ARM64 and full-native qualification and the remaining P1 delivery gates stay open.
