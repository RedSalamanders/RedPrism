# Polygonal native ARM64 Debug / Release / ASan — 2026-10-17

Status: Debug, Release and instrumented ASan cross-builds qualify committed `d5b3f8f`.
P1 remains WIP. Native ARM64 runtime is unavailable.

## Source and artifact scope

All three canonical `build.ps1 -Platform ARM64 -Rebuild -MaxCpuCount 1` children exit 0. Main independently
verifies every archived file, all 987 actual compiler source records across five projects, exact target
and optimization/CRT flags, source/product bookends and five matching AA64 executable/full-PDB pairs.
The projects contain 346 Application, 135 Host, 284 Document, 138 Composition and 84 I/O TUs.

All three profiles have the same 1,469 root code, policy, toolchain and domain inputs. Four separately pinned
runner helpers make each complete freeze 1,473 rows. Release uses a fresh separately pinned runner
for its retry; the original Debug/ASan runner remains immutable. No source/profile gate is relaxed.
The artifact reader's source-only controls have four positives and 22 refusals per cohort.

## Actual qualification

| Profile | Attempt | Actual PID | Archived files | Archive completion SHA-256 |
| --- | --- | ---: | ---: | --- |
| Debug | r1120 | 90524 | 1546 | `452800A23ECF402CC0F14B946EE76C265BED91F1FCE72C5DEEC11E2BC6F6EBF6` |
| Release | r1123 | 29372 | 1546 | `6556517CAB9631ED07D19635FB53DC1C98603428B46390B92B552AE9337E016B` |
| Instrumented ASan Debug | r1122 | 96108 | 1550 | `3A6419A0BC9F342D97E9AC2396C145DC1E2EFC8FE97CFF86829E7DC75F107B0B` |

## Preserved failures and storage

- Release `r1121` stops before MSBuild because simultaneous dependency installation holds vcpkg's
  filesystem lock. Preserve all 1,483 archive files, actual exit 1 and completion
  `22DE18CD2AB4711928DF33D9F91E8E9BC93A7C6EEC462005E323A778F0DD7AFE`.
  No lock is removed. Confirm Debug dependency installation has ended and no vcpkg process remains
  before fresh Release attempt `r1123`; the failed attempt identifier is never reused.
- The first Release readback refuses the aggregate freeze hash because its source-only reference
  sorted strings while the original runner sorts Windows Path objects. Main proves all 1,473 indexed
  rows, paths, lengths and hashes identical; derive the corrected order from the original source rows.
  Corrected hash `80AB3E6B1C1BA877A2EF2C0FDC11A67B9DA6807E9D53FA781CDCFBB3F40A21FF`
  equals the actual unchanged freeze. Retain the original reader/refusal, repeat every reader control
  and qualify those exact completed products without rerunning a compiler or changing source.
- Six old canonical output/intermediate junctions and all original D/V target data remain preserved.
  Fresh build targets and archive junctions use task-owned D storage; no data is deleted. Main reviews
  and corrects the cheaper agent's helper before execution, then independently verifies all six new
  and six preserved target resolutions. The native 6 GiB C / 18 GiB build-volume guards stay unchanged.

Private recipes and readback: `.build/P1Drafts/PolygonalNative-r1-Main/` for Debug/ASan;
`PolygonalNativeRelease-r1-Main/` retains the failed Release reader, and
`PolygonalNativeRelease-r2-Main/` contains its verified source-order correction. The completed
qualification reports live under the matching `.build/P1Integration/` cohorts.

## Remaining validation

Canonical ARM64 ASan `r1122` exits 0 on the same root source. Main independently verifies all 987
actual sanitizer compiler records, all 1,550 archive files and five exact AA64 PE/PDB identities.
The original process-handle watcher joins before readback; source remains frozen through that readback.
Cross-compilation establishes no native ARM64 runtime claim. Latest full x64 Debug/Release/ASan,
analysis, later changed-source qualification, remaining code, ordinary activation and measured G1 remain
open for P1 Done.
