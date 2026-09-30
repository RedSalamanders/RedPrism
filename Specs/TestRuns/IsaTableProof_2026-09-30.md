# Linked switch-table proof qualification — 2026-09-30

## Scope

The strict x64 ISA scanner recognizes the observed signed-byte switch dispatch only with its exact opcode
sequence, same-owner anonymous 16-byte PDB table, valid decoded targets and contribution bounds. It includes
the signed default slot rather than trusting the address of the second DWORD as a complete table.

An objdump row starting inside a proven table may extend into adjacent padding only when its entire row
matches the PE bytes, is at most 15 bytes and every byte outside the table is `0xCC` in the backed section.
A vector instruction starting at/after the table end, non-padding bytes, mismatched PE bytes, an overlong row,
an unproven table or targets outside its contribution remain rejected. Runtime allowlists are unchanged.

## Executed evidence

All 100 synthetic inline-table controls pass in each x64 Debug, Release and ASan Debug `test.ps1` run. Each
configuration also scans all five linked images and passes the existing compiled allowed/rejected ISA controls.
The complete repository suites, hidden WARP checks, sanitizer detection probe, skill validation, formatting
and all three ARM64 cross-builds pass on the combined sizing/save/history source following `68f0428`.

Logs: `.build/p1-sizing-history-x64-{Debug,Release,ASanDebug}.log` and
`.build/p1-sizing-history-ARM64-{Debug,Release,ASanDebug}.log`. The 270-file source/project/script freeze was
checked before each configuration and after the matrix; SHA256:
`FB3AF47A088889F262F15E717AD5D0FB0F30C1F739F5A4879C832CAC1DC37933`.

This qualifies the scanner proof and regression controls. It makes no SIMD performance or native ARM64 runtime claim.
