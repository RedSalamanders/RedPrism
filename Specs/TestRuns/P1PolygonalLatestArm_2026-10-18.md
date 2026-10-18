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
