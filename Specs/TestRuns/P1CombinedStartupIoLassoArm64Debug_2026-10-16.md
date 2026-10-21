# P1 combined source: ARM64 Debug cross-build

Date: 2026-10-16
Status: PASS, cross-build and artifact identity only. No ARM64 program executed.

The combined code qualified on x64 in [the native checkpoint](P1CombinedStartupIoLassoX64_2026-10-16.md)
is committed as `67a77dd`. ARM64 Debug r1042 rebuilds the same 1,356 repository source/build-policy inputs.
The full 1,360-input freeze substitutes only the pinned ARM runner among the four validation wrappers:
`D25CE657E19D70AB359CE6163F492CF563D2CEEF62CC2C29C8EABBF48C6EA296`.

- Actual parent PID: 50532; exit: 0.
- UTC interval: 2026-10-16T07:41:01.724012+00:00 to 2026-10-16T08:12:44.573011+00:00.
- All five executable/PDB pairs independently match CodeView GUID/age; every executable is ARM64 (`0xaa64`).
- All 932 actual compiler command records have the Debug profile without ASan.
- Main independently reads every archived size/hash, current source bookend and build-profile report.
- DxUi remains at `8e8994079e82f79e6765e6a885c03fc687957591`, API revision 3.

Archive: `Z:\CodexArtifacts\RedPrism-P1\2026-10-12\P1-LassoHost-ARM64-Debug-r1042-passed-20261016` (1430 verified files).

Completion SHA-256: `4F5C3DD22555A79CCA008B1315537D25A9F88DDE9D94E3856526CC12156E7A4B`.

ARM64 Release r1043 and instrumented ASan r1044 remain in progress/pending. Native ARM64 runtime,
ordinary activation, complete caller admission/retirement, measured G1 and P1 Done are not established
by this receipt. No unchanged x64 gate was rerun for this cross-build.
