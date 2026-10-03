# P1 processing initialization: acquired-prefix safety

Date: 2026-10-02.

## Scope

The change covers processing initialization after a bounded prefix of worker and GPU resources has been acquired. Private Debug/WARP proof exercises acquired-prefix cleanup and sixteen reached retry initialization failures. Its stated limit is explicit: it does not claim an actual worker-pool, GPU API, or allocator failure was induced.

The native r3 qualification is the scoped 390-control processing suite, the unchanged 52-control application-shutdown suite, all existing x64 suites, and three ARM64 cross-builds. These results do not close full A3, G1, or RAW work.

## Protocol

The private candidate used the frozen `r1` inputs and completed Debug/WARP with exit code 0 and image SHA-256 `1CD5D3526BC49C021AC6E20EB36DD67824D21A28C9F230C9BFEB5D9394D1F5C9`. Build completion UTC: `2026-10-02T13:01:04.0280655Z`; run completion UTC: `2026-10-02T13:14:02.7568114Z`; archive completion UTC: `2026-10-02T13:15:19.7018477Z`. Its qualified archive has 561 source inputs and 23 evidence files. The source and evidence manifest SHA-256 values are `432B40A87F88C91B7ECF515576D567E5DBB0AFB36056F422F7F011F79946866B` and `10F736B1CC0F5496BE78B3AE5E7AAF61145E6E8A4204A8C44380611CCB41E728`.

The native r3 run required a forced six-profile rebuild. Each profile result receipt, the final matrix receipt, and verified-controls receipt were read as JSON strings; their UTC timestamps are preserved verbatim below. Current frozen inputs were SHA-256 checked against both the freeze and the qualified archive source manifest. Every archived source and evidence entry was independently hash checked.

Native r3 source manifest SHA-256: `0C3C948D9C18E2146BD739D63256B5A5298D162C622BE6D0252BB8FF28B87054` (584 inputs). Evidence manifest SHA-256: `433A13C21EF1E99546FA8ACEF768962AD707CBF5D1AC560C4DDDEA35D5CB47C3` (329 files).

## Results

The private processing log reports exactly 390 passing acquired-prefix controls and 52 passing shutdown controls. The native x64 Debug, Release, and ASan Debug application logs independently report the same 390 and 52 passing control counts and contain no failed controls. Existing full x64 suite summaries and expected suite control counts were also checked for all three configurations.

| Official profile receipt | Exit code | Completed UTC (receipt string) |
| --- | ---: | --- |
| `p1-processing-init-native-r3-x64-Debug.log.result.json` | 0 | 2026-10-02T14:17:48.4123367Z |
| `p1-processing-init-native-r3-x64-Release.log.result.json` | 0 | 2026-10-02T14:10:31.9546987Z |
| `p1-processing-init-native-r3-x64-ASanDebug.log.result.json` | 0 | 2026-10-02T14:05:06.1652921Z |
| `p1-processing-init-native-r3-ARM64-Debug.log.result.json` | 0 | 2026-10-02T13:53:58.1627123Z |
| `p1-processing-init-native-r3-ARM64-Release.log.result.json` | 0 | 2026-10-02T13:51:35.7845763Z |
| `p1-processing-init-native-r3-ARM64-ASanDebug.log.result.json` | 0 | 2026-10-02T13:48:50.5667799Z |

The native r3 start receipt is `.build/p1-processing-init-native-r3-start.json` (started UTC `2026-10-02T13:46:36.8909507Z`). The complete matrix receipt is `.build/p1-processing-init-native-r3-process.json`: exit code 0, completed UTC `2026-10-02T14:17:48.4508939Z`. Exact controls are recorded in `.build/p1-processing-init-native-r3-verified-controls.json`; the frozen input list is `.build/p1-processing-init-native-r3-freeze.json`. The exact verified-controls scope is `Fresh six-profile forced-rebuild processing acquired-prefix qualification, full x64 suites and unchanged prior integration controls; broader A3/P1/G1 remain open`. Three ARM64 profiles cross-built successfully; no ARM64 runtime qualification is implied.

## Retained failure cause

Native r1 remains an archived failure: Release actual self-test PID 108148 terminated with `0xC0000005` in `Application::Shutdown` at `Application.cpp:3211`; x64 Debug was not run. The failure archive receipt `.build/p1-processing-init-native-r1-failure-archive.json` completed UTC `2026-10-02T13:37:42.5058001Z` and records 584 source inputs and 230 evidence files. The ARM64 Debug, Release, and ASan Debug profiles passed; the x64 ASan Debug profile passed. The release failure is not attributed to stdout buffering.

The supplementary build-tracking manifest has 162 entries and SHA-256 `811C4B290B2B63F9C0BA00B1E13E4EC498A39D86E58349846F73273FEDA6D100`. Its archived `Application.obj` timestamp is `2026-10-02T13:00:00.8575053Z`; `Application.cpp` and `Application.h` timestamps are `2026-10-02T12:58:46.7434051Z` and `2026-10-02T12:58:46.7459067Z`. Copied source timestamps predate the object. The failed PDB contains two `Application` sizes, 44912 and 44920 bytes; the faulting instructions access the scheduler through the old layout, where the candidate inserted its borrowed probe pointer. The forced r3 rebuild produces a single 44920-byte layout and an `Application.obj` timestamp of `2026-10-02T14:07:24.6656021Z`, after matrix start. Its Release application passes all 390 initialization controls and the complete suite. The retained evidence identifies the mixed object/layout image; stdout buffering was a separate harness correction.

Native r2 is also retained as a failed attempt: formatting failed before any configuration was built or run. Its format-failure archive receipt `.build/p1-processing-init-native-r2-format-archive.json` completed UTC `2026-10-02T13:46:36.4438615Z` and contains 584 source inputs and 7 evidence files. Native r3 moved stdout configuration into `Main` before new tests and removed the old late call; formatting was corrected before the forced rebuild.

## Limits

No actual pool, GPU API, or allocation API initialization failure is claimed. The private retry evidence covers only the sixteen reached retry paths. The scoped 390 controls and unchanged 52 shutdown controls do not establish full A3, G1, or RAW completion. ARM64 results are cross-builds only.

## Evidence archives

- Native r3 qualified archive manifests: `C:\Users\eric\AppData\Local\RedPrism\QualificationArchives\p1-processing-init-native-r3-qualified\source-manifest.json` SHA-256 `0C3C948D9C18E2146BD739D63256B5A5298D162C622BE6D0252BB8FF28B87054`; `C:\Users\eric\AppData\Local\RedPrism\QualificationArchives\p1-processing-init-native-r3-qualified\evidence-manifest.json` SHA-256 `433A13C21EF1E99546FA8ACEF768962AD707CBF5D1AC560C4DDDEA35D5CB47C3`.
- Private Debug/WARP archive manifests: `C:\Users\eric\AppData\Local\RedPrism\QualificationArchives\p1-processing-init-private-r1-qualified\source-manifest.json` SHA-256 `432B40A87F88C91B7ECF515576D567E5DBB0AFB36056F422F7F011F79946866B`; `C:\Users\eric\AppData\Local\RedPrism\QualificationArchives\p1-processing-init-private-r1-qualified\evidence-manifest.json` SHA-256 `10F736B1CC0F5496BE78B3AE5E7AAF61145E6E8A4204A8C44380611CCB41E728`.
- Native r1 failure archive manifests: `C:\Users\eric\AppData\Local\RedPrism\QualificationArchives\p1-processing-init-native-r1-failure\source-manifest.json` SHA-256 `30C200282BAE83EFE493391CB1F4DA5D278B99C62A97076795941DADE20662A7`; `C:\Users\eric\AppData\Local\RedPrism\QualificationArchives\p1-processing-init-native-r1-failure\evidence-manifest.json` SHA-256 `137A463DCD7C7CCBFC7EB961701813EB9AE2E72CDE592E1B9DA046B9DEDCE76E`.
- Native r2 format-failure archive manifests: `C:\Users\eric\AppData\Local\RedPrism\QualificationArchives\p1-processing-init-native-r2-format-failure\source-manifest.json` SHA-256 `49CA8B507B3BB7565EF2EB4B189147950F200ED809CC5847CD8B90E20EAD35E0`; `C:\Users\eric\AppData\Local\RedPrism\QualificationArchives\p1-processing-init-native-r2-format-failure\evidence-manifest.json` SHA-256 `CCA85EC466073A961027FF50722991A69ED58CA8D69DD2E42E5DC8D594A0FFA4`.
