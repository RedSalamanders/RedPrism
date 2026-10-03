# Terminal R9 byte-switch ISA data proof

Date: 2026-10-03. Isolated and full native matrix qualification pass; exact archival is complete.

## Checklist

- [x] Preserve native nudge r1 failure, including the exact Release EXE/PDB, symbols, disassembly and report.
- [x] Primary implements bounded terminal table recognition without changing the runtime allowlist.
- [x] Review cheaper-agent fixtures and scanner findings; correct the fixtures and close identified proof gaps.
- [x] Complete every new positive/refusal control and retain the ten existing parser suites.
- [x] Reparse three unchanged archived binaries; archive exact sources, actual process receipts and evidence.
- [x] Activate only archived sources, run a fresh complete native matrix, independently verify and archive.

## Proof boundary

The observed Release dispatch compares R9B with a nonnegative bound 1…127, branches with unsigned JA,
loads the image base into RDX, loads a DWORD RVA through `RDX + 4*R9`, adds RDX and jumps through RAX.
Its aligned anonymous same-owner PDB data start and containing procedure end define exactly bound+1 DWORDs.
Every table/default target is a decoded body boundary, outside the guard, terminal tail and table. The exact
RET/CALL/INT3/NOP tail, all guard rows and the uninterrupted known pre-table listing must match the PE.
Table bytes must fit within the section's raw data. Existing generic recognition and allowlist are retained.

Known direct transfers, including LOOP-family and prefixed transfers, cannot enter the guard interior or table.
The observed alternative entry is accepted only as an immediately preceding identical CMP followed by JNE
to the main JA, with both encodings corroborated. Missing/unknown/overlapping rows, mismatched listing bytes,
wrong ownership or bounds refuse classification. Executable VEX in the body or immediately after the table
continues to fail. This classifies inline data; it does not establish the compiler's complete CFG or R9 upper
bit provenance. Synthetic controls are parser inputs and are never executed as native machine code.

The refused Release binary is SHA-256
`DBCF0F35645DE9EE98165C1183431B3FE1C7C5F5F76FE13971A3A07697927206`; PDB
`8596E0BE772C848EAA8DF687C3AA72470696FB081282F244E79F1CB8147C1185`.
Native intermediate archive completion is
`2AD923E87894BC5BF0015F26815EAF995B217685AB9195CE81AB08973EAE774B`.
The new proof must also preserve the older ASan 4,587,958-instruction/27-table-row and Release
852,901-instruction/8-table-row reparses, all 73 QWORD and 57 RDI controls, and the ten exact retained
parser assertion multisets. The nudge Release image has 872,839 instructions and one newly proved vector-looking
table row, increasing its data-row count from 13 to 14 while executable runtime rows remain 459.

## Private r1 failure

The actual child process runs from `2026-10-03T05:46:31.3158453+00:00` to
`2026-10-03T05:50:14.1762840+00:00` and returns exit 1. Three unchanged-image reparses and all ten retained
parser suites pass. Eight new positive cases pass before a PowerShell variable collision stops the new suite;
the full new suite is unqualified. Source and evidence archival must finish before correction or retry.

Failure archival retains 19 exact source inputs and 21,559 evidence files. Completion SHA-256 is
`22606F4B47F1DEA7CD05B2D76CF1F8723B766032981697E028D09EAD8E86F6BC`;
source manifest `CEE061A59A94FF15D60624F214B537B47A2CF7C69EB641177050661870607561` and evidence manifest
`2A621AF629EAC99CE5806EF3EF4E0BCEEBE60B6F2B1D9EFD565755E56BE7779C`.

## Private r2 result

The corrected fixture retains the same scanner and allowlist. Its complete qualification process returns
actual exit 0: 108 unique new controls (8 accepted, 100 refused), all ten exact retained parser assertion
multisets, and the three unchanged EXE/PDB reparses pass. The new controls include matched comparison flags,
missing/unknown/listing gaps, real VEX body/target/adjacency refusals, LOOP-family/prefixed incoming transfers,
stored-section bounds and LLVM's separately listed LOCK prefix. Native activation waits for immutable archival.
Actual process interval: `2026-10-03T05:54:02.2102060+00:00` to
`2026-10-03T05:56:52.5558343+00:00`. Scanner SHA-256
`72099830B92A7B9E92C6717D6CF240CC63440EE097CA548889047EC6F8ED44B3`;
new control source `8C0B170EBFC3D4149D8A7E9B3D99BF6EFCF646462A8FC7982EA8B04FB4095249`.

Immutable r2 archival completes at `2026-10-03T05:59:56.8352612+00:00`, retaining 19 sources and
21,962 evidence files. Source manifest `17DCE4E8D8AD3D98436BF5B2B9F38745C77867EDAEBF0B987E735E44D1608B1B`;
evidence manifest `8844A13B66F4D6D84EBBF2D38DDCFCD36ADC91AAEB03D6AFC3F70B168AB7A307`;
completion `2898001FD0F6A6471C4900DAEEBBB4A56CE25D52479EB7BBBF5931E60B34D187`.
Archive: `C:\Users\eric\AppData\Local\RedPrism\QualificationArchives\isa-r9-byte-switch-proof-r2-qualified`.

Candidate root: `C:\Users\eric\AppData\Local\RedPrism\PrivateWork\isa-r9-byte-switch-proof`.
This proof alone establishes no application responsiveness or measured G1 result.

## Native qualification

The nudge core r2 matrix passes all six profiles at `2026-10-03T06:34:34.8129025+00:00`, with all 108 new
controls, retained ISA controls and unchanged runtime allowlist in every full x64 gate. Independent verification
and immutable archival preserve 619 inputs/23,770 evidence files at `2026-10-03T06:41:31.8464318+00:00`.
Completion SHA-256 is `565A7D49E5F6EED1A3A35F682FB5E46B8960BDFF6AD702816B164AD338700AF1`.
See the [native record](SelectionNudge_2026-10-03.md). ARM64 claims are cross-build only.
