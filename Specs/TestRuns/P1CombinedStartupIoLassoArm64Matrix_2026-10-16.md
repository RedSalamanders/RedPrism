# P1 combined source: completed ARM64 cross-build matrix

Date: 2026-10-16
Status: PASS for Debug, Release and instrumented ASan cross-builds. No ARM64 binary executed.

The qualified source is `67a77dd`, the same 1,356 repository source/build-policy inputs used by
the [full x64 checkpoint](P1CombinedStartupIoLassoX64_2026-10-16.md). The 1,360-input ARM freeze
substitutes only the pinned ARM wrapper among four validation wrappers:
`D25CE657E19D70AB359CE6163F492CF563D2CEEF62CC2C29C8EABBF48C6EA296`.

| Attempt | Profile | Actual parent PID | UTC interval | Compile records | Archive files |
| --- | --- | --- | --- | --- | --- |
| r1042 | Debug | 50532 | 2026-10-16T07:41:01.724012+00:00 to 2026-10-16T08:12:44.573011+00:00 | 932 | 1430 |
| r1043 | Release | 86996 | 2026-10-16T08:12:57.695236+00:00 to 2026-10-16T08:54:00.625323+00:00 | 932 | 1430 |
| r1044 | ASan Debug | 91836 | 2026-10-16T08:54:21.008020+00:00 to 2026-10-16T09:33:59.842713+00:00 | 932 | 1434 |

Every actual child exits zero. Main reads the exact source/archive/profile reports and five
executable/PDB CodeView GUID/age pairs per profile. Every executable is ARM64 (`0xaa64`); all 932
ASan compile records are instrumented. Main also rehashes all 1,360 current inputs after the matrix.
The six-profile closeout retains the original x64 full-suite receipts and these three cross-builds,
without rerunning unchanged x64 tests.

## Preserved archives

- Debug: `Z:\CodexArtifacts\RedPrism-P1\2026-10-12\P1-LassoHost-ARM64-Debug-r1042-passed-20261016`. Completion SHA-256: `4F5C3DD22555A79CCA008B1315537D25A9F88DDE9D94E3856526CC12156E7A4B`.
- Release: `Z:\CodexArtifacts\RedPrism-P1\2026-10-12\P1-LassoHost-ARM64-Release-r1043-passed-20261016`. Completion SHA-256: `30C9F87A1DAB57BE2FF5598C1EAA3F6D34C38516DADFE3F665FB2EA9CA0E90A9`.
- ASan Debug: `Z:\CodexArtifacts\RedPrism-P1\2026-10-12\P1-LassoHost-ARM64-ASanDebug-r1044-passed-20261016`. Completion SHA-256: `B0B86DEFC03A391B69A90972B580A29D838B4536678EBE0EA05263EC47030EF0`.

DxUi remains `8e8994079e82f79e6765e6a885c03fc687957591`, API3. There is no ISA allowlist change.
The current native checkpoint passes analysis, all full x64 profiles and all ARM64 cross-builds.
Native ARM64 runtime is unavailable. Complete caller admission/retirement, ordinary activation,
S5/H3/H4, measured G1 and P1 Done remain open. The later Hue/Wand/hard-brush draft is unbuilt and
unadopted at this checkpoint and receives no qualification from these receipts.
