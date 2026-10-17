# Exact R8/RAX/RDX interior-table recognition — 2026-10-18

Scope: build-time ISA scanner repair required to run a Polygonal terminal failure diagnostic. P1 remains WIP.

## Original failure

The private terminal diagnostic R3 compiled and linked successfully. The original scanner rejected
`0x1404d7aa8` in the pinned `DxUi.WindowHost.obj` contribution as EVEX. The matching full PDB
identifies an anonymous six-DWORD table ending at `0x1404d7ac0`; all six RVAs are `0x4d5162`.
The original refusal and all 27 archive files remain verified and unchanged.

Build archive completion SHA256: `721441F8055C21C9D20C711095147D5FBA1CFA8A8AEE2D40F732E9EE97A9F0A6`.

## Correction

Recognize only the observed contiguous seven-row, 36-byte form: biased `LEA EAX,[R15+disp32]`,
unsigned `CMP EAX,max; JA rel32`, `LEA R8,imagebase`, `MOV EDX,[R8+4*RAX+tableRva]`,
`ADD RDX,R8; JMP RDX`. Maximum is 1–127. Matching PE bytes, nonoverlapping decoded boundaries,
same contribution and exact anonymous PDB start/end prove maximum+1 DWORDs. A unique containing
procedure provides the complete code body before its first anonymous data symbol. Known code,
case/default routing, no guard-interior/data transfers, and RET or a backward JMP with at most
three alignment bytes remain required. Later tail data is not granted data status.

LLVM's separate LOCK row is accepted only beside a PE-matching memory INC/DEC/XADD row. Orphan,
register-only, unknown, missing, mismatching and non-atomic suffix controls refuse this proof.
Only the proved table bytes receive data status; genuine body/target vector instructions remain gated.
No DxUi, runtime/object/function/tier/address allowlist or application behavior change.

## Validation

The complete Release scanner regression exits 0 (actual child PID 19336) with all 13,691 predecessor PASS records, including compiled tier, runtime-owner and missing/mismatched-PDB controls. Main verifies every frozen input and both complete raw logs. Formatting, all 15 skills and diff checks pass.

The 107 added encoded controls pass: 22 accepted and 85 refused, including bounds 1/5/12/127,
alternate bias, exact guard-start entry, RET/backward-JMP padding, canonical split LOCK forms,
malformed guard/base/load/add/jump/target/symbol/body/listing data, conflicting ownership and real
VEX/EVEX code. Main independently reviews the cheaper agent's source and retains its fixture-only
empty-array failure before correction. All control expectations are unchanged in the portable test.
These controls never execute their encoded instructions.

Read-only replay of the unchanged failed image and original decoder/PDB output yields 1,473,537
decoded instructions, 488 runtime vector instructions, 11 table-data rows and zero violations.

| Identity | SHA256 |
| --- | --- |
| Executable | `5AD74A9BBAFBAD007BF2B14184D5F76D58B5C2D3871933E29D3892D691EBCFCD` |
| Full PDB | `5ABD244C5A084FABD7B986FB62D8D25C9129098C6636E1783043108CA7BCD9AB` |
| Unchanged allowlist | `7870AF49195841A9B9B88DFD15378D550B127754683666C83AED83753FD80E1E` |

## Retained evidence and limits

Private `.build/P1Drafts/IsaR8RaxRdx-r2-Main/` retains frozen source, original raw-image replay,
all encoded fixtures, complete regression logs, compiler controls and independent readback.
`PolygonalTerminalNative-Main-r3/main-isa-failure-preserved.json` retains the original failure.
The diagnostic's earlier acknowledgement failure remains a separate issue; scanner repair alone
claims no terminal runtime, full native profile, ARM64, ordinary activation or G1 completion.
