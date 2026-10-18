# Latest P1 full native x64 ASan qualification — 2026-10-18

Status: full canonical x64 ASan Debug qualification passes. P1 remains WIP.

## Actual run and source

`test.ps1 -Platform x64 -Configuration "ASan Debug" -MaxCpuCount 1`, attempt `r1141`,
actual PID 74848, exit 0. UTC interval: 21:16:08.966899–21:46:17.402156.
Source head: `f83a981db4783919ae06d6cbfa3f63de2617c3bd`; product viewport change: `4d7fac8`;
41-case dispatcher fixture: `8d6b2f3`; final scanner: `710bf4c`. Source remains unchanged throughout.

The 1,493-input freeze includes 1,483 Root code/policy/domain inputs and ten separately pinned runner inputs.
Freeze SHA256: `9531A68D2F6CD59FE50F21469972E809736BBA93EE2B1D4F3D82C116F97A5CD1`.
Before launch, two positive and 22 negative source-only profile controls pass. Main corrects the cheaper
helper's literal Windows-path substitution and source-array closing-line assumption before preparation;
the original source-only refusal is retained. No native run is repeated to repair a reader.

## Passed scope

- All five canonical suites, including the complete hidden WARP Application.
- All 994 actual compiler records, with required warnings/conformance/UTF-8 flags, profile CRT/optimization/
  definitions, real `/fsanitize=address` and the required forced header. Five exact x64 executable/full-PDB pairs.
- All linked-image ISA checks, complete parser/compiled controls and unchanged runtime allowlist/DxUi pin.
- All 152 Application stages and **11,004 stdout assertions**, preserving every exact matching-profile
  predecessor. Selected dispatcher: **41 cases / 420 facts**. Both initial Layers geometry assertions pass.
- Concurrent raster stress: 256 rounds; saturated 64-worker/nested-close stress and the Host stress rerun.
- A fresh isolated genuine heap-use-after-free diagnostic, plus main-thread and worker crash probes.
- All nine fresh harness captures. Main independently validates every PNG chunk CRC, bounded inflation,
  shape, alpha and nonuniform samples; unsupported/skipped captures cannot qualify.

The original sampler-capacity and inverse-selection held-gate ASan failures do not recur. Their earlier
failure records and unresolved-cause audit obligations remain; another successful run is not a causal repair.

## Independently verified archive

Cohort: `D:/RedPrismBuildCaches/.build/p1-latest-full-native-asan-r1`.
Archive: `Archives/P1-LatestFullNativeAsan-x64-ASanDebug-r1141`; all **1,766** source/evidence files pass
independent byte, length and inventory checks. Completion SHA256:
`3B2D0CE9F7632BF36E64A0E4D5EA91EA3DB61DFBC6EEE75E2545F1AB619E8860`.

All five completion readers exit 0: archive, source/artifact/profile, PE/PDB, mandatory compiler flags, and
runtime/probes/stress/captures. Final receipt: `r1141-main-qualified.json`. Reader-set SHA256:
`85D27A6D3E99B4558A2AB2F36B9AA891DE60023C3F85044283675302D8EACE83`.
Actual UAF diagnostic SHA256: `E5DB9774BB246C4055552337D4917B90CF040A0109D1E73BFB06A130B203DB43`.

This qualification includes the current 41-case fixture and viewport fix together. The private 44-case keyboard
draft is not compiled or included here. Latest ARM64 Release/ASan cross-builds qualify separately; native ARM64
runtime is unavailable. Latest Debug/Release/analysis, ordinary responsive activation, remaining native scenarios,
resource/capture/publication bounds, measured G1 and the other P1 delivery areas remain open. No Done or main merge.
