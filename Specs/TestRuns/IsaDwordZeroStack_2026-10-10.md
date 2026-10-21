# ASan DWORD zero-stack terminal-table proof

Status: parser and unchanged-image proof passed; integrated native qualification pending.

## Checklist

- [x] Preserve original ASan r708 pre-runtime ISA failure and its exact source/image/PDB/log archive.
- [x] Verify the original rejected address lies in the anonymous terminal 11-entry DWORD table for
  `__vcasan::MinidumpCallback`, after RET plus literal `0f 1f 00`, with matching PE/listing/PDB ownership.
- [x] Recognize only the exact guarded DWORD `[RSP]` / short unsigned JA / RAX base / zero-extending ECX form.
- [x] Preserve all 73 QWORD-stack controls. The saved failed image now has zero violations, 52 proved table-data
  rows and the unchanged 11,690 real runtime vector instructions. The runtime allowlist is unchanged.
- [x] Independently review/correct the cheaper fixture and run 109 encoded controls, five accepted/104 refused.
- [x] Integrate controls into the ordinary linked-instruction gate and update the owning build/validation contracts.
- [ ] Qualify the combined current source through full Debug, Release, real ASan and Release analysis.

## Original controls and corrections

The cheaper sidecar first used Windows PowerShell's older compiler, failing `Add-Type` before scanning any bytes.
That original failure is retained. PowerShell 7 compiles the unchanged scanner candidate. The first new fixture
then failed because its target RVA bytes did not begin with a VEX prefix; it also duplicated body boundaries.
Main corrects fixture byte placement, emits each body boundary once and makes every body listing PE-matched.
Invalid-target mutations change one entry while retaining genuine vector-looking rows in the rest of the table.
A decoded RET is a valid target under the existing contract; main adds it as a positive and requires terminal
padding to be refused. PDB GUID/age controls require the exact identity exception. Missing/mutated rows,
metadata, symbols, counts, bounds, fallback, terminal and body proof still refuse. Genuine VEX in the body and
adjacent VEX/EVEX each remain a real vector violation. Expectations were not relaxed to accept malformed proof.

Final integrated output: `PASS isa-dword-zero-stack: 109 controls (5 accepted, 104 refused)`.
Private originals/corrections are retained under `.build/P1Drafts/IsaDwordZeroStack-r1-agent`,
`IsaDwordZeroStack-r1-Main` and `IsaDwordZeroStack-r2-Main`. Parser controls never execute their bytes and grant
no runtime, baseline-hardware, performance or P1 Done qualification.
