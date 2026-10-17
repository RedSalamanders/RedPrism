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
| x64 ASan Debug full test r1110 | PID 18660, exit 0; 11:34:08–12:25:24 UTC | All 1,866 archive files, exact 982 instrumented compiler records, five exact executable/PDB pairs, complete runtime roster, both stress suites, genuine UAF detection and nine harness PNGs | `5C90BBEFE3E8500C44AFAAB6BD6CF844A39D6481DA36E5F197C035DE1ECF862B` |
| ARM64 Debug Rebuild r1111 | PID 63748, exit 0; 11:38:28–12:08:34 UTC | All 1,535 archived input/evidence files, exact 982 compiler records, current/frozen sources and five AA64 executable/full-PDB pairs | `7F29696CCDFC3F4FE7DAD04DC391424F487325CF667AE3D5D620A26DDD14DAA0` |
| ARM64 Release Rebuild r1112 | PID 39924, exit 0; 12:14:51–12:54:41 UTC | All 1,535 archive files, exact 982 compiler records and five AA64 executable/full-PDB pairs independently verified | `D33525723DB62B5D3B89D63EE7FEEC3886F4EFE8FDA1DAF26F76AEA2C1A596E1` |

Main reviews the archive/profile and PE/PDB readers and verifies their actual successful log hashes.
The durable raw receipts and readback reports are beneath
`.build/P1Integration/CursorNative-r1-Main/` and `.build/P1Drafts/CursorNative-r1-Main/` in the
`p1-opacity-host-proof` native checkout. ARM64 is cross-build qualification; a native ARM64 runtime
host is unavailable. No ARM64 runtime result is inferred.

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

## Remaining qualification

- ARM64 instrumented ASan Rebuild remains due on this same source; Debug and Release are qualified above.
- Current-source full x64 Debug/Release and Release analysis remain due.
- Ordinary activation, every remaining P1 delivery area and measured G1 remain open.

The earlier profile receipts keep their original source and scope. No passing result is inferred from a
build-only result, a private component run or an incomplete native attempt.
