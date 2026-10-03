# Guarded QWORD stack / RAX table proof

Date: 2026-10-03. Parser qualification and the corrected native matrix pass and are archived.

## Checklist

- [x] Preserve the failed native image, PDB, listing, symbols and source inputs before correction.
- [x] Prove the exact seven-instruction guard/dispatch and terminal table boundaries.
- [x] Run 73 new controls: four accepted tables and 69 refusals; retain all 57 RDI controls.
- [x] Reparse both unchanged failed images with the existing runtime allowlist.
- [x] Complete and verify the immutable parser archive before activation.
- [x] Qualify the corrected six-profile native matrix and independently verify/archive its exact controls.
- [x] Commit and push the step.

## Reason and scope

The ASan follow-up image reports `Application.obj` at `0x1407d3094` as `vhsubps`. Those bytes are a DWORD RVA
inside a terminal `std::variant` visit table. The guard compares a QWORD stack slot against unsigned maximum 64,
loads the same slot into RCX, and dispatches through DWORD RVAs using RAX as the image base. The table has 65
entries and ends exactly at the owning PDB procedure's end. The immediately preceding decoded PE byte is RET.

The recognizer requires matching PE/listing bytes at all seven boundaries, equal full stack displacements,
the unsigned nonnegative imm8 guard, the exact image base, aligned anonymous data in the same contribution,
an exact owning procedure end, decoded body targets, and the guarded fallback among those targets. It rejects
incoming direct branches from elsewhere in that procedure into the guard's interior or the table. This is
a proof of this encoding; no object, function, address or additional runtime exemption is added.

Controls alter each instruction's presence, listing bytes and decoded status; guard type/range/branch;
registers, SIB and all four stack displacement bytes; image base; terminal RET; PDB ownership/bounds/data;
target boundaries and direct incoming edges. An adjacent executable VEX row remains a violation even when
the neighboring table is valid. Existing fixtures and legacy goldens are unchanged.

## Parser attempts

Attempt r1 stops before either image is scanned: a helper formed one combined path instead of six paths.
Its actual failure, 17 frozen source inputs and six evidence files are preserved in
`C:\Users\eric\AppData\Local\RedPrism\QualificationArchives\isa-qword-stack-rax-proof-r1-failure`
before correcting the helper's array construction. This attempt supplies no parser success claim.

Attempt r2 returns actual process exit zero. All ten parser suites pass. The unchanged ASan image decodes
4,587,958 instructions with 27 proved vector-looking table rows and zero violations. The unchanged earlier
Release image decodes 852,901 instructions with eight such rows and zero violations. The allowlist SHA-256
remains `6F55A40AB0CE23E98BB10839EC68765531129EF375B00DD29B298C3DC97EE560`.

The immutable archive retains 17 frozen inputs and 19,260 evidence files at
`C:\Users\eric\AppData\Local\RedPrism\QualificationArchives\isa-qword-stack-rax-proof-r2-qualified`.
Actual process completion is `2026-10-03T03:09:33.0669270+00:00`; archival completes at
`2026-10-03T03:11:46.3605703+00:00`. Every copy is verified by SHA-256 and length. Independent validation
of the archived manifests, actual receipt, two original images and exact control names passes before activation.

| Receipt | SHA-256 |
| --- | --- |
| Scanner source | `19FE5CE3A89C2D4BFBE6B4B0BA0D2A994CC387A311095ED8CE16118A0DAC54AB` |
| Source manifest | `9E30506DA69595FC342979F6A183274FDCCB2C7EB169B532DA90BA09E4652F98` |
| Evidence manifest | `0D33EFE71FBA28495E713A9667C00E926C013DE3A966FEBBE0D0A93ED1584464` |
| Archive completion | `847BC5FF74751DBA330282198BCFC194FB0CDF5F1528068F34A98D34DC491131` |

The corrected native matrix also passes all ARM64 cross-builds and the full x64 Debug/Release/ASan gates.
All 73 new and 57 retained controls match the qualified proof's exact names in every x64 suite. The combined
613-input/21,605-evidence archive completes at `2026-10-03T03:51:14.2165680+00:00`
([record](SelectionMoveFollowups_2026-10-03.md)). G1 remains its separate performance gate.
