# P1 selected cursor — canonical native qualification, 2026-10-17

## Status and source identity

The adopted cursor worker and clipboard fixture source is under canonical qualification. P1 remains WIP.
The source base is `7fa882a9d969cd93f5dade19b6f5456797ebfa66`, plus the nine reviewed caller/fixture
changes documented in [the component evidence](P1SelectionCursorHost_2026-10-17.md).
The common freeze has 1,463 inputs (1,459 repository code, policy and domain inputs), SHA-256
`43B6701FA1B81DBCA986B769DFC25EC523520C4326902E5CA7F2AA4F82C1EE28`.
The exact project roster is 982 translation units: Application 345, Host 135, Document 280,
Composition 138 and I/O 84. DxUi remains pinned to `8e8994079e82f79e6765e6a885c03fc687957591`.

## Qualified profiles

| Profile | Actual result | Independent checks | Archive completion SHA-256 |
| --- | --- | --- | --- |
| x64 Debug full test r1114 | PID 39840, exit 0; 13:23:26–14:20:54 UTC | All 1,743 archive files, exact 982 compiler records, five executable/full-PDB pairs, complete five-suite roster and nine harness PNGs independently verified | `F4D20FCEDF05E775BBE71029830F4B17B92100642CB25C4880C9EB651002C98A` |
| x64 ASan Debug full test r1110 | PID 18660, exit 0; 11:34:08–12:25:24 UTC | All 1,866 archive files, exact 982 instrumented compiler records, five exact executable/PDB pairs, complete runtime roster, both stress suites, genuine UAF detection and nine harness PNGs | `5C90BBEFE3E8500C44AFAAB6BD6CF844A39D6481DA36E5F197C035DE1ECF862B` |
| ARM64 Debug Rebuild r1111 | PID 63748, exit 0; 11:38:28–12:08:34 UTC | All 1,535 archived input/evidence files, exact 982 compiler records, current/frozen sources and five AA64 executable/full-PDB pairs | `7F29696CCDFC3F4FE7DAD04DC391424F487325CF667AE3D5D620A26DDD14DAA0` |
| ARM64 Release Rebuild r1112 | PID 39924, exit 0; 12:14:51–12:54:41 UTC | All 1,535 archive files, exact 982 compiler records and five AA64 executable/full-PDB pairs independently verified | `D33525723DB62B5D3B89D63EE7FEEC3886F4EFE8FDA1DAF26F76AEA2C1A596E1` |
| ARM64 ASan Debug Rebuild r1113 | PID 73152, exit 0; 13:18:53–13:57:38 UTC | All 1,540 archive files, exact 982 instrumented compiler records, unchanged 1,459 code/policy/domain inputs and five AA64 executable/full-PDB pairs independently verified | `0B836999E5F966CBE0AA435FA4C1C2D53AEB1BF2D1A57CA96B0A0097F783BC5B` |

Main reviews the archive/profile and PE/PDB readers and verifies their actual successful log hashes.
The durable raw receipts and readback reports are beneath
`.build/P1Integration/CursorNative-r1-Main/` and `.build/P1Drafts/CursorNative-r1-Main/` in the
`p1-opacity-host-proof` native checkout. ARM64 is cross-build qualification; a native ARM64 runtime
host is unavailable. No ARM64 runtime result is inferred.

## ARM64 instrumented build and storage identity

Canonical `build.ps1 -Platform ARM64 -Configuration "ASan Debug" -MaxCpuCount 1 -Rebuild`
exits 0. Main verifies every archive file, all 982 real sanitizer compile commands, the five AA64
executable/full-PDB pairs and every successful readback step's actual exit/log hash. Readback completes
at 13:58:22 UTC. This closes the Debug/Release/instrumented-ASan ARM64 cross-build matrix for the adopted
cursor source; it does not establish native ARM64 runtime behavior.

Fresh V: output/intermediate and readback archive storage preserves the canonical logical paths and all
prior caches. The new transport freeze is
`71500BFDCBA1EBD2586850CD38652BC42C948BFDC7EFD7AD6179AA22CB2D8714`.
Main proves all 1,459 repository code/policy/domain inputs and all 1,462 common frozen rows are identical
to the original profile freeze. Only the storage coordinator's path/content changes; no dependency,
product source or validation guard is relaxed. Raw receipts/readback are beneath
`.build/P1Integration/CursorNativeV-r1-Main/` and `.build/P1Drafts/CursorNativeV-r1-Main/`.

## Complete ASan runtime evidence

Main independently verifies all 23,039 Document assertions, 150 Application stages (9,111 stdout and
7,613 stderr facts), 9,138 Host PASS records, 7,391 composition records and 1,953 I/O records. The exact
50 cursor facts, two native clipboard setup controls and 53 predecessor clipboard contention facts are
included. No assertion failure, missing/duplicate/reordered family or unknown post-footer data is accepted.
The native sanitizer control produces a genuine `AddressSanitizer: heap-use-after-free` diagnostic,
SHA-256 `6FD29BC531F5D1D14865142F79E60E67B917F36F67C796F6BCEC98D02EF64B10`.
All nine source-defined harness cases are captured, archived and hash-checked. Both canonical concurrent
raster and worker stress suites complete successfully. Full archive inventory, raw compiler commands,
current/frozen source identity, ISA reports and exact executable/full-PDB identities also pass.

Readback completes at 12:26:54 UTC. Main additionally checks every successful readback step's actual exit
and log SHA-256; no native runtime or build is replayed for qualification.

## Full Debug runtime and retained reader refusals

The canonical Debug run passes all 23,039 Document assertions, 150 Application stages (9,111 stdout /
7,613 stderr facts), 9,137 Host records, 7,391 composition records, 1,953 I/O records and all nine harness
captures. Actual parent exit is 0. Main verifies all 1,743 archive files, 982 actual compiler records,
five exact executable/full-PDB pairs and the complete ordered runtime streams. Readback completes
at 14:33:27 UTC. The transport freeze is
`F9BED09C057DD8AD0D747F17C6A1073BFD1C4D4D5801B019B168B0B1A961DD06`; its 1,459 repository
code/policy/domain inputs remain identical to the other adopted-cursor profiles.

The first readback preserves a Debug allocator-view `KeyError`: the historical witness dictionary has
Debug/ASan entries; the source-derived Release rows live in the baseline roster. Main first preserves and
rehashes the complete archive and raw extracts, then validates the raw Debug witness exactly and maps only
those 60 known statistic rows to their source-derived Release representation. Semantic record identities
must match. Two newline-positive and nine mutation-negative controls pass; image/geometry goldens,
raw logs, actual Debug compiler flags and profile identity remain unchanged.

The next readback refuses a missing pinned Host-tail reader directory in the new storage cohort. Main
restores its exact already-qualified bytes, SHA-256
`8C4829866A9E2A155654431760B1D3D015BC46796E838E247BF1C89FA62D760B`, and runs its controls.
The complete five-suite reader then passes. Both original refusals remain retained; no build or runtime is
replayed. Raw/native reports are in `.build/P1Integration/CursorNativeVX64-r1-Main/`; the preserved
reader corrections and controls are in the corresponding `CursorNativeVX64-r2-Main` / `r3-Main` draft folders.

## Remaining qualification

- ARM64 Debug, Release and instrumented ASan cross-builds are qualified above; native ARM64 runtime is unavailable.
- Current-source full x64 Debug and real ASan are qualified; Release and Release analysis remain due on the latest adopted milestone source.
- Ordinary activation, every remaining P1 delivery area and measured G1 remain open.

The earlier profile receipts keep their original source and scope. No passing result is inferred from a
build-only result, a private component run or an incomplete native attempt.
