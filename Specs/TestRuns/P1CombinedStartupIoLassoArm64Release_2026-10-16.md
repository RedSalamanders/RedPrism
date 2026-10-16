# P1 combined source: ARM64 Release cross-build

Date: 2026-10-16
Status: PASS, cross-build and artifact identity only. No ARM64 program executed.

The combined code qualified on x64 in [the native checkpoint](P1CombinedStartupIoLassoX64_2026-10-16.md)
is committed as `67a77dd`. ARM64 Release r1043 rebuilds the same 1,356 repository source/build-policy inputs.
The full 1,360-input freeze substitutes only the pinned ARM runner among the four validation wrappers:
`D25CE657E19D70AB359CE6163F492CF563D2CEEF62CC2C29C8EABBF48C6EA296`.

- Actual parent PID: 86996; exit: 0.
- UTC interval: 2026-10-16T08:12:57.695236+00:00 to 2026-10-16T08:54:00.625323+00:00.
- All five executable/PDB pairs independently match CodeView GUID/age; every executable is ARM64 (`0xaa64`).
- All 932 actual compiler command records have the Release profile without ASan.
- Main reads the completed source/archive/build-profile and executable/PDB reports; all archived sizes/hashes pass.
- DxUi remains at `8e8994079e82f79e6765e6a885c03fc687957591`, API revision 3.

Archive: `Z:\CodexArtifacts\RedPrism-P1\2026-10-12\P1-LassoHost-ARM64-Release-r1043-passed-20261016` (1430 verified files).

Completion SHA-256: `30C9F87A1DAB57BE2FF5598C1EAA3F6D34C38516DADFE3F665FB2EA9CA0E90A9`.

ARM64 instrumented ASan r1044 remains running. Native ARM64 runtime, ordinary activation,
complete caller admission/retirement, measured G1 and P1 Done remain open. No unchanged x64 gate
was rerun for this cross-build.
