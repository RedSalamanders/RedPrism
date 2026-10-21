# P1 analysis repairs and integrated Release Application — 2026-10-19

Status: analysis repair candidate; complete canonical analysis and latest full native matrix are pending. P1 is WIP.

## Retained failure

Canonical x64 Release `build.ps1 -Analyze -Rebuild -MaxCpuCount 1`, r1145, actual PID 79644, exits 1 at
2026-10-19T00:22:06.966740+00:00. Source/helper pre/post inventory remains identical at
`CE86541DD4AD7192A11536B5D574F4AD08FBD0D3F28385D85A70B407BA1071E2`.
The complete log has 12 warnings in six test TUs: six C6221 exact status comparisons in the two
Application Polygonal fixtures; Document ContinuingPublication C6221; OpeningSettings C6239/C6326;
GestureAuthority C6240/C6326; and Dispatcher C6336. Warnings remain errors.

Main preserves 2,459 actual source, log, object and partial-product files with independently verified
bytes in `D:/RedPrismBuildCaches/.build/p1-polygonal-keyboard-release-analysis-r1/FailureArchive-r1145`.
`r1145-main-failure-preserved.json` binds the original joined exit and manifest. No native tests or
fresh SAL/ISA controls ran after that compile failure; older control directories do not qualify r1145.

## Minimal test-only repairs

Exact signed 32-bit status comparisons use explicit `std::int32_t` casts; broad success/failure
predicates cannot replace exact statuses. Only the constant positive-envelope requirement becomes `static_assert`. The prepared-opening
size equality, ownership allowance and measured ownership/memory predicates remain runtime checks
under their original named receipts. All prior check counts and fact names remain. Parentheses clarify odd/even key selection without changing input order. No
warning suppression, compiler-profile relaxation, oracle rebaseline or product behavior change.

## Integrated full Application Release proof

A private current-code candidate combines the header-kind, actual header widget and Ctrl-corner
fixes with the six Application status casts. It builds with three fresh TUs and 348 exact inherited
Release objects, including the normal full Application Main driver. Its original r1 helper selected
a different PREfast extension profile and failed C26432 in unchanged Common code; preserve that
failure. The corrected r2 child mirrors canonical `Esp.Extensions=...;ConcurrencyCheck.dll`,
records it and passes Release analysis for all three fresh TUs. Common code and warning policy are unchanged.

Actual full Application PID 98212 exits 0 at 2026-10-19T02:10:42.226683+00:00: 152 exact stages /
11,313 stdout assertions, all 64 dispatcher cases / 729 facts and every r1144 Application predecessor
pass. The separate scanner passes with matching image/full PDB and unchanged allowlist. Main independently
verifies compiler/link/object lineage, all source bookends, PE/CodeView identity, saved packages and
PNG/JPEG exports. All 2,400 exact source inputs are preserved as content-addressed blobs before adoption.
The 73-artifact manifest is `62D286F465046793CEFE896A75ACF9F0D68311FDEA68BA7BEA68E4121ABF9520`.
Cohort: `D:/RedPrismBuildCaches/.build/p1-polygonal-current-integration-main-r2/Release`.

This is full Application qualification, not a canonical five-suite run or complete Document repair
qualification. Native ARM64 runtime, latest complete native matrix/analysis, ordinary activation, initial
Ctrl/lift and measured G1 remain open. Historical sampler/inverse ASan failures retain their original scope.

## Rejected repair candidate

ASan build r1147 exits 1 before native tests: two C2131 errors reject `static_assert` calls to
non-constexpr `PreparedLassoOpening::MemoryBytes`. Main had reviewed the cheaper source draft
but missed this compile-time restriction. Its same-source owned Release analysis r1146 is stopped
explicitly before the later test compilation, with actual joined exit 4294967295 and cancellation
reason recorded; no user application is terminated. Both have exact matching pre/post source
bookends and retained logs, sources and partial products. They do not qualify analysis or runtime.
The corrected candidate moves only `envelope > 0` to compile time and keeps function-dependent
checks at runtime, preserving their original numeric expectations and PASS roster.

## Full native results and latent status diagnostic

The 12-warning repair candidate passes full canonical x64 ASan r1148, full x64 Debug r1152, and
ARM64 Release/real-ASan cross-builds r1150/r1151. ASan and ARM archives/profiles/runtime are independently
qualified in the linked reports; Debug independent archive/runtime readback is in progress.
Canonical Release analysis r1149 finishes with exit 1: all earlier diagnostics are gone, but
`StoppedOriginal` in LassoGestureAuthority exposes a further C6221 at line 709. The complete log has
only that warning; the failed attempt remains retained. Its exact `cancelled ? E_ABORT : E_FAIL`
comparison now receives the same explicit signed32 cast, with no expected outcome or predicate change.
Affected compilation/runtime qualification of that additional cast remains separate.
[Full ASan](P1AnalysisRepairsNative_2026-10-19.md), [ARM64](P1AnalysisRepairsArm64_2026-10-19.md).

Later checkpoint: [completed affected repair validation](P1AnalysisRepairLatest_2026-10-19.md). The older input sets above retain their original scope.
