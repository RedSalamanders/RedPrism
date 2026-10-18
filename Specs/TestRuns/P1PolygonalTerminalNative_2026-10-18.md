# P1 Polygonal terminal native cross-build qualification — 2026-10-18

Status: ARM64 Debug passes; Release is running; instrumented ARM64 ASan Debug remains due. P1 stays WIP.

## Exact source and scope

Compiled source base: `58388b3633dcde1536a4ae39076b67204a1cd14d`. The Debug build starts at docs-only `f51ef753f8054e5733ffa0742e17ed41cbb15cbd`, build number 389; qualification records later docs-only `a86068be7fff5b598d0e266f4ff737a8ef66ef96`. All 1,473 compiled/project/toolchain/policy/domain inputs are unchanged; the four pinned runner inputs bring the complete freeze to 1,477 rows.

Freeze SHA-256: `C6F7B8C3DCF3CD0C6301641919B4B2047354B429328EA811AC6A7D9D60371BCD`.
Five projects contain exactly 990 compiler records: Application 349, HostTests 135, DocumentTests 284, CompositionTests 138 and IoTests 84. Source bookends, original compiler commands, source/object coverage, profile/CRT flags, full artifacts and matching PE/PDB identity are checked independently. Reader source-only controls pass four positive and 22 refusal cases before use; those controls are not native qualification.

## Canonical profile matrix

| Profile | Attempt | Actual parent exit | Archived files | Compiler records | Five AA64 PE/full-PDB pairs | Result |
| --- | --- | ---: | ---: | ---: | --- | --- |
| ARM64 Debug | `r1127` | 0 | 1,550 | 990 | exact | qualified |
| ARM64 Release | `r1125` | pending | pending | pending | pending | running |
| ARM64 ASan Debug | `r1126` | not launched | pending | pending | pending | due |

Debug runs canonical `build.ps1 -Platform ARM64 -Configuration Debug -MaxCpuCount 1 -Rebuild`, 2026-10-18T08:16:58Z–08:51:10Z, actual PowerShell child PID 92288. Independent archive/artifact/PE-PDB readers complete at 08:53:20Z. No build/runtime is repeated to repair a reader.

Archive completion SHA-256: `A537A0354926702C038DD9159E08B1908D34098248966B62FA42C8BA97F9810F`.
Main receipt: `.build/P1Integration/PolygonalTerminalNativeProfiles-r1-Main/r1127-main-qualified.json`.
Preserved products and archives live under `D:/RedPrismBuildCaches/.build/p1-polygonal-terminal-native-r1`; canonical logical output paths remain unchanged through verified junctions. The original six old-cache junctions and their physical targets remain preserved.

## Retained failures and limits

The original `r1124` disk-space failure occurred while creating its receipt, before MSBuild. It has a zero-byte roster and no successful native child claim. The approved S5 cache relocation restores the unchanged disk guard; fresh attempt `r1127` is used. [Storage and worktree evidence](P1WorktreeStorageRecovery_2026-10-18.md).

These are cross-build claims only. Native ARM64 runtime is unavailable. Complete latest-source x64 suites/analysis, ordinary-route activation, complete Polygonal dispatcher, G1 measurements and remaining P1 delivery areas stay open. The separate focused journal module is generated/private and excluded from this compiled Root cohort.
