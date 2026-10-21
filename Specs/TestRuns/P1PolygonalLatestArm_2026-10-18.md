# Latest original Polygonal ARM64 Release and ASan — 2026-10-18

Status: independently qualified canonical cross-builds for source `9af34e41e0d6fa80868ee3fb20e27233b1aed4f9`. P1 remains WIP. Native ARM64 runtime is unavailable.

## Actual build and source identity

Both actual children run `build.ps1 -Platform ARM64 -Configuration <profile> -Rebuild -MaxCpuCount 1` through the repository entrypoint, then join with exit 0. All 994 translation units are rebuilt: Application 351, HostTests 136, DocumentTests 285, CompositionTests 138 and IoTests 84. Each profile freezes the same 1,485 inputs (1,481 code/policy/domain inputs and four coordinator inputs), SHA256 `E0C35FCB05897260AE1B8B6A465DA0241260BBEC6B2FE185521866D827D0C0E0`; every source copy, live bookend and actual command source set is independently checked.

The later `833a05b` commit changes only the two non-normative WIP checklists. Those files are outside the compiled/domain-input freeze. It does not change either build's code identity or resolved version number. No compiler/source input is changed during either rebuild.

| Profile | Attempt | Actual child PID | Verified archived files | Archive completion SHA256 |
| --- | --- | ---: | ---: | --- |
| Release | r1128 | 78448 | 1,558 | `8E45E96A46A69A5AAA5DD1659EAEA94505856F18F942B6B2F38EC08ACB3FE232` |
| ASan Debug | r1129 | 13016 | 1,562 | `D4BD614B5B432D510B84970636690C7CE0F9F292A4F0A11F806F8595C509C93B` |

## Independent checks

- Complete inventory, manifest hashes and every archived byte pass, including source copies, actual result/logs, compiler tlogs and linked products.
- Five AA64 PE/full-MSF-PDB pairs per profile pass CodeView GUID/age matching through discovered `llvm-pdbutil`; the separate identity reader records every executable/PDB hash.
- Every one of the 994 actual commands per profile has W4, WX, SDL, stdcpplatest, conformance and UTF-8. CRT/optimization/defines are profile exact. ASan has real `/fsanitize=address`, debug CRT, `/Zi` and the exact forced `RequireAddressSanitizer.h` in every TU; conflicting or absent instrumentation is refused.
- Main independently reviews the cheaper agent's reader, replaces hardcoded tool discovery, preserves four positive and 22 negative source-only controls, and verifies all five project source sets. Source expectations are established before build completion, never copied from passing output.
- Main hashes every actual object against its CL.write record and preserves the two objects a later caller/fixture change would replace. No old cache is deleted. Canonical products/intermediates and scratch remain on D through the existing verified logical output paths.
- DxUi source/pin remains `8e8994079e82f79e6765e6a885c03fc687957591`; no dependency source/pin or ISA allowlist change.

Durable records: `D:\RedPrismBuildCaches\.build\p1-polygonal-latest-arm-native-r1`, including both `r112x-main-qualified.json`, archived manifests, separate artifact/PE-PDB/mandatory-C++-profile readers and baseline object maps.

## Scope

These are ARM64 compilation/link/artifact/source qualifications. They establish no native ARM64 runtime, UAF detection on ARM64, ordinary activation, performance/G1 result or P1 closeout. Debug still has the earlier 990-TU terminal-cohort evidence and needs the final latest-source gate.

The private foreign-history guard and inside-Save fixture are subsequent source changes and are not covered by these two rebuilds. Their x64 functional qualification is separate. A later affected canonical ARM64 build must prove every changed source and unchanged reused object before its current-source claim is made.


## Adopted guard source `2cb0b64`: affected canonical builds

Current caller and fixture source `2cb0b6458dc4d553bada97283081a97b5b64920c` now qualifies in both ARM64 profiles. These runs use canonical `build.ps1 -Platform ARM64 -Configuration <profile> -MaxCpuCount 1` Build. A prelaunch comparison proves exactly two C++ changes and three owning-contract updates from the full-rebuild baseline; headers, projects, kernels, dependency pin, toolchain and other compile inputs remain exact. Expectations are fixed before either current build, with the same four positive and 22 negative reader controls.

| Profile | Attempt | Actual child PID | Fresh + byte-exact inherited objects | Verified archived files | Archive completion SHA256 |
| --- | --- | ---: | ---: | ---: | --- |
| Release | r1133 | 105036 | 2 + 992 | 1,558 | `18102F07ACA43E99851F545E3A54D6942632A8600DA385CB7B80CEA043389851` |
| ASan Debug | r1132 | 15548 | 2 + 992 | 1,562 | `CE97DAE9572F088E9C65FBA24D1F66B1A5A8B51838C767BB63306F0BA5E39132` |

Both actual build exits are zero. Main checks all 994 actual source/command/profile records, every current source/header/flag identity, changed-source object bytes and 992 unchanged object hashes per profile. All 351 application objects occur in the actual link. The two freshly compiled objects are exactly `ApplicationPolygonalPressJobs.cpp` and `Application.Tests.PolygonalDispatch.cpp`. Every mandatory C++ flag, real ASan instrumentation, full archive/inventory/source bookend and five exact AA64 PE/full-PDB pairs pass. This is an affected-build qualification with explicit inheritance, not a claim that all 994 objects were freshly compiled again.

Preserve Release `r1131` separately: actual child 91976 exits 1 during DxUi's shared vcpkg setup because concurrent configuration startup cannot acquire its filesystem lock (error 16). It reaches no C++/MSBuild stage. Main verifies all 1,558 archived files, completion SHA256 `BE301DD929C94D32622B6C0CAE4CF6896464416BB5B2A862CE277DCEFBAC937C`, and every diagnostic. The separate serial `r1133` passes with unchanged product/dependency source; no lock is deleted and no process is terminated. Serialize canonical dependency setup for future configuration runs.

Also preserve the first object-reader refusal: the console banner contains non-UTF8 bytes, so decoding the entire stdout as UTF8 fails after the successful archive/artifact/PDB/C++ checks. Main retains the original reader and corrects only the exact ASCII compiler-name check to operate on unchanged bytes. The same completed ASan products pass; no build or runtime is replayed for that reader correction.

Current durable proof adds `D:\RedPrismBuildCaches\.build\p1-polygonal-guard-arm-native-r1` (ASan and retained failure) and `D:\RedPrismBuildCaches\.build\p1-polygonal-guard-arm-release-r2` (serial Release), with prelaunch source pins, baseline object maps, manifests, independent readers and final `r113x-object-lineage-qualified.json` records. Native ARM64 runtime, latest Debug/full x64/analysis, ordinary activation, measured G1 and P1 closeout remain open.
