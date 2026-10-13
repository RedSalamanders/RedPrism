# P1 bounded lasso core and hidden frame qualification — 2026-10-13

Status: **native x64 Release analysis, Debug, Release and real ASan PASS**. Latest-wrapper ARM64 Debug, Release and instrumented ASan cross-builds also pass. Actual physical lasso host integration, ordinary activation, opaque allocation/cancellation
bounds, S5/G1 and P1 delivery remain open.

## Qualified code and assertions

All five native targets now include the worker-created bounded lasso opening, geometry/history Finish and
current-only session bridge. Application draft readers use constant-sized point-count/first-point accessors.
The fixed host descriptor preserves ordered Point/Cursor/Marquee/Backspace operations and a separate physical
terminal/AA slot; actual host replay/Finish is the next integration step.

The four suites add 32 bounded-gesture, 43 Finish-geometry, 67 session-bridge and 80 input assertions plus
four summary records. Main independently reviews/runs the cheaper agent's strict reader, requiring the exact
21,447-record predecessor and exact 226-record delta: **21,673 document records and 136 application stages
in each full profile**. Debug/ASan retain only the existing 60-row configuration projection from the unchanged
wand assertion unit and the existing three bounded positive area-statistic normalizations.

Reader result SHA-256: `A3FF73BF2DCC443F7C5AB1F9FBC67DC6997B2DEBD639C41CA6E18255685801FF`.
Source freeze SHA-256 for all four replacement runs:
`2C895D59E5C0DA59BB6C17847CD5A1EF927F98D655C276EDD369867049EB6768` (1,334 inputs).

| Attempt | Gate | Actual PID / exit | Complete archive SHA-256 |
| --- | --- | --- | --- |
| r1005 | Release analysis | 92480 / 0 | `D806C0FACAB94D32D0283B9318EC6A0C273BCDA795446848ED25298F6BB05B00` |
| r1006 | Release test | 44740 / 0 | `8F5319989E6BEB22DB304E0B8D40DDDF61451EA200FA4B0F24AFE1A3348F6DA0` |
| r1007 | Debug test | 59204 / 0 | `62280896AD70B65FD8688B0A7AEB50D673A17A16382CC6A77256F3CE19021FC7` |
| r1008 | ASan Debug test | 67892 / 0 | `F0D40DB88EA56C56E7FAADC9785A623EA4889D2383861BC5755DB9B7683C5D4C` |

All archives reside under `Z:\CodexArtifacts\RedPrism-P1\2026-10-12`, with names
`P1-LassoBounded-<configuration>-<mode>-r<attempt>-passed-20261013`. They retain source, full stdout/stderr,
compiler/linked ISA/parser controls, test/sanitizer evidence, stage roster, product identities and manifests.
Real ASan r1008 includes the deliberate detection probe, main/worker crash probes and complete canonical
`test.ps1` suite. Runtime smoke is structural evidence; it does not establish visible G1.

## Hidden committed-display frame contract

Debug r990 originally refuses the stage-24 conjunction requiring a visible Present count, successful canvas
and zero composition. The original failure did not distinguish the failed component. Main adds failure-only
diagnostics without changing those predicates; focused Debug r996 passes every stage. That passing repeat
establishes no cause for r990.

The revised hidden fixture requires one **real Present attempt**, an actual completed canvas blit, handled
S_OK/occlusion and correct visible counting, with zero owner composition. An explicit handled-occlusion
control makes the policy observable; its summary now has 42 checks. PresentCount retains its visible-frame
meaning; separate attempt/result diagnostics describe the hidden call. A fixture-injected handled result is
not an observed DXGI return. This amendment permits legitimate hidden occlusion and supplies no visible
presentation-latency claim. Owning validation documentation records that boundary.

## Retained refusals and repairs

| Attempt | Preserved refusal | Repair / subsequent evidence |
| --- | --- | --- |
| r986 | C6262 fixture stack frames 175,152 / 16,440 bytes | Heap-owned fixture sessions; original assertions/scopes unchanged |
| r987 | Another 263,300-byte fixture stack frame | Remaining new fixture sessions use heap ownership; r988 analysis passes |
| r990 | Committed hidden-frame conjunction at stage 24 | Original archive retained; r991 never launches; r996 focused repeat establishes no cause |
| r995 | Main edited fixture while source bookend was finishing | Correctly refused source bookend; full archive retained; not a passing build |
| r997 | Signed diagnostic argument C6340 | Explicit unsigned cast of the nonnegative counter; r998 analysis passes |
| r999 | MSBuild/CLR paging-file/commit-capacity allocation refusal before runtime | Full failure retained; r1000/r1001 never launch |
| Compiler tracking preflight | Incorrect expectation of an emitted `/MP2` switch | Original refusal retained; actual installed targets show `UseMultiToolTask=false` |

Closed generated proof products are copied, hash-verified and relocated to Z-backed directories with original
paths retained as junctions. `test.ps1 -MaxCpuCount 2` forwards the explicit project cap to `build.ps1`;
`build.ps1` supplies `/m:2` and requests `CL_MPCount=2`. Actual target properties are recorded. This is not
proof of emitted `/MP2`, a global compiler process limit, or a causal explanation for the earlier OOM.
No machine paging-file setting, DxUi source/pin, legacy golden or ISA runtime allowlist changes.

## Remaining qualification

Fresh r1009–r1011 close the latest build-wrapper/source scope. Main independently verifies successful
actual exits, every current source bookend and each complete source/evidence/archive manifest. Cross-build
source freeze is `A8AA2DBA403807FE6F90C1373174B8DB316CFAB7C0BEBD66016C9524A33CD03E`.

| Attempt | Configuration | Actual child / exit | Complete archive SHA-256 |
| --- | --- | --- | --- |
| r1009 | Debug | 105764 / 0 | `71D081CF49288AD131D337EDCCEB9D5FD3A452BF785D26335DE6822D40F79F17` |
| r1010 | Release | 74384 / 0 | `6C315A3CB86CA7256B6E1425356B26ACF63274A6AF4F953FA1DABC98557B529C` |
| r1011 | ASan Debug | 65972 / 0 | `9EAC2D319D926B5E5AAD453E9CF3911A9A65278F392B32F09B92EDD0C639854C` |

Native ARM64 runtime remains unavailable and is not claimed. Formatting passes for 1,182 files and all
15 repository skills validate. The reviewed native checkpoint is ready for commit/push; the
[lasso child](../Plans/WIP/LassoWorkers_2026-10-12.md) retains actual physical caller/host integration,
ordinary activation and measured S5/G1 work. The [P1 milestone](../Plans/WIP/UiNeverBlocks_2026-09-22.md)
keeps all delivery areas open.
