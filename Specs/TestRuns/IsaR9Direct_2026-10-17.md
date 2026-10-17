# Immediate R9 switch-table ISA recognition — 2026-10-17

Status: qualified build-scanner correction; runtime allowlist and DxUi pin unchanged.

## Original failure and exact proof

A one-test-TU Release relink compiles/links successfully, then the scanner rejects
`0x14055cdbc: DxUi.Menu.obj: vandnpd xmm0,xmm6,[rax]`. Main verifies all 1,740 failed archive files,
627 object/command records, exact source/dependency closure and executable/full-PDB before correction.
The original gate exit 1 and archive completion remain:
`6870929E92722AF91D3690600E6107C3D25BF64F00A66F28613D9A6C8E7965F2`.

The PE and PDB show an anonymous 13-DWORD jump table at RVA `0x55cdb0`, immediately after RET at
`0x55cdaf`. Its fourth RVA `0x55c9c5` has bytes `c5 c9 55 00`, which linear disassembly interprets as
VEX. The executable dispatch is `MOVZX EAX,[RDI+1]; CMP AL,12; JA; LEA R9,imagebase;
MOV EAX,[R9+4*RAX+table]; ADD RAX,R9; JMP RAX`.

The new recognizer requires those exact seven PE/listing rows, immediate image-base definition,
unsigned guard, same-owner anonymous data start, exact guard-derived terminal PDB extent, known RET
and every forward default/case instruction boundary. Only the proved DWORD bytes receive data status;
their reported bytes must match the PE. There is no address/object/function exemption.

## Validation

- 64 new independent synthetic controls: three accepted ranges and 61 refusals. Wrong/missing/unknown
  guard/listing/RET, symbol/owner/extent/target, PE mismatch and real VEX in the body, target or adjacent
  code remain rejected. The synthetic bytes are never executed.
- Complete Release scanner control suite: actual exit 0, 13,582 emitted PASS rows
  (including repeated shared controls), existing parser families and compiled scalar/AVX2/EVEX positive
  and negative controls, runtime-ownership and PDB identity controls all pass.
- Exact original failed executable/PDB and archived raw metadata/disassembly rechecked without relinking:
  1,460,772 decoded instructions; 488 VEX/EVEX instructions, all in existing runtime allowances;
  three proved inline-data rows; zero violations. The original failed report remains retained.

The first new test-driver run incorrectly concatenates three guard-fault names through PowerShell
array/comma precedence; it therefore constructs a valid unchanged case. The old script/log and refusal
are retained. Separating the fault strings fixes the test driver only; scanner bytes stay unchanged.

## Identities and scope

- Executable: `A2041148F525F7368817BA12BEF71CC36FEA3DDDB1C262292327C017983924BF`.
- PDB: `CC01E24195ABE0D8FDAD9FC116C8E54121C2F7D1BE6AF66B0828132C33F859D0`.
- Scanner: `E87F5ECD11E1E40D8A2EA3009B56C6F89E83D383FDDD91C25FBF075A842BB395`.
- Full-control log: `76D2573DDB5FA243E45D5C5E38B49891A81B3F845653B478F47FAA3AE3E4F0C8`.
- Exact-image readback: `33916FF380E2874C6FAC3772B5EF2392DA86B7F2E3875E39CEA808C1005A6FB7`.
- Existing allowlist: `7870AF49195841A9B9B88DFD15378D550B127754683666C83AED83753FD80E1E`.

Recipes, original refusals and readback are under `.build/P1Drafts/IsaR9Direct-r1-Main/`.
This is build-tool qualification. [Freehand runtime evidence](P1FreehandHost_2026-10-17.md) is separate;
latest full native integration and all P1 delivery/activation/timing gates remain open.
