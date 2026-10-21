# P1 combined startup, long-path I/O and lasso retirement: native x64

Date: 2026-10-16
Status: PASS for the exact combined-source x64 correctness checkpoint. All three ARM64 cross-builds also pass ([matrix](P1CombinedStartupIoLassoArm64Matrix_2026-10-16.md)); ordinary activation, complete admission/retirement, performance and P1 Done remain open.

## Implementation

- Normalize resolved Windows API path spellings for project/image streams and same-directory staging. Bound temporary leaf components, preserve exclusive creation/cleanup and keep project path containment checks in the original namespace.
- Add explicit complete responsive startup acquisition for selection-outline and lasso-draft sources. Publish asynchronous engine flags only after every acquisition/readiness boundary; reached refusal or reentrant shutdown prevents later acquisition. Ordinary activation remains a separate gate.
- Extend lasso retirement tests for refusal/retry, original event/worker ownership and joined Close. Preserve the corrected fair two-pass fixture setup and every original assertion.

The code, tests, project entries and owning architecture/I/O contracts are integrated together. DxUi remains at `8e8994079e82f79e6765e6a885c03fc687957591`, API revision 3; no library source or ISA allowlist change.

## Exact native validation

Canonical Release analysis r1034 passes its SAL controls, all five linked-image scans and eight scan rejection gates. Its 1,775-file archive completion SHA-256 is `B57A0B8802205F63906F5B3823A9A0421E866D1DD42FF1050F8636734F654CB8`.

All three full `test.ps1 -Platform x64 -Configuration <profile> -Rebuild -MaxCpuCount 1` runs exit zero. They use the same 1,360 frozen inputs: 1,356 repository source/build-policy inputs and four pinned validation wrappers. Source-freeze SHA-256:

`73EF58966817381856C0F7FAC0089040D22BD919CD5BBDEBE74BB5071B4A3F25`

| Attempt | Profile | Actual parent PID | UTC interval | Compile command records | Archive files |
| --- | --- | --- | --- | --- | --- |
| r1035 | Release | 20512 | 2026-10-14T03:52:00.956698+00:00 to 2026-10-14T04:29:36.196537+00:00 | 932 | 1763 |
| r1036 | Debug | 50328 | 2026-10-14T04:29:54.513774+00:00 to 2026-10-14T05:12:23.394998+00:00 | 932 | 1783 |
| r1041 | ASan Debug | 47072 | 2026-10-16T06:44:03.123601+00:00 to 2026-10-16T07:40:34.888993+00:00 | 932 | 1747 |

Every profile passes 139 Application stages, 21,720 Document records, 7,391 composition records and 1,953 I/O records. The integrated 160 host-input facts, 29 draft-display facts and all 50 lasso controller facts remain present. Original numeric goldens and ordered predecessor records are unchanged.

Full entrypoint gates also pass failure reporting, help/crash probes, direct/prepared UI and cancellation smoke, restore checks and application-harness screenshots. Separate mandatory `validate-skills.ps1` and `format.ps1 -Check` checks pass on the same repository source: 15 skills and 1,208 files. Smoke and hidden diagnostic timings are not G1 measurements.

Real ASan r1041 instruments all 932 actual native compile command records. Concurrent raster stress and the isolated heap-use-after-free detection probe pass. Probe SHA-256:

`B18872B50242F9FCDFF36839868A63DCF5E00CA9D2D7C9F920ADD299C080A728`

Main independently reads the exact rosters, every archived file, source bookends, linked ISA reports, actual configuration commands and all five executable/PDB CodeView GUID/age pairs before accepting each profile.

## Retained evidence

- r1035: `Z:\CodexArtifacts\RedPrism-P1\2026-10-12\P1-LassoHost-x64-Release-r1035-passed-20261014`; completion SHA-256 `0F5DF9ED8EAC6AE5EB895994B759BD013E8240516AD01730A7E4DCD4285D96FE`.
- r1036: `Z:\CodexArtifacts\RedPrism-P1\2026-10-12\P1-LassoHost-x64-Debug-r1036-passed-20261014`; completion SHA-256 `895332FF3FC8A4275A2C6679F6530422EEDF92CF259749CAA930DE5213C3C392`.
- r1041: `Z:\CodexArtifacts\RedPrism-P1\2026-10-12\P1-LassoHost-x64-ASanDebug-r1041-passed-20261016`; completion SHA-256 `EF6E7E66636415DF1DCACE482FEADF461EC8EA3FF34590CDF88DAE2B84F5C292`.

Interrupted r1037 has no completed child receipt and no inferred exit or success. Its 2,404-file partial source/object/product snapshot remains under `.build/P1Drafts/CombinedNativeResume-r1-Main/Interrupted-r1037/`, with independent size/hash readback in `snapshot.json`. Fresh r1041 replaces the interrupted attempt without changing source or expected predicates.

Earlier private failures and successful receipts remain retained: [long-path I/O](P1LongPathIoRepair_2026-10-14.md), [complete startup](P1CompleteResponsiveStartup_2026-10-14.md), [lasso retirement](P1LassoDraftRetirement_2026-10-14.md). The separate explicit-Z long-path receipt is scoped to its named new fixtures; it does not mean all OS-selected predecessor fixtures ran on Z:.

## Remaining qualification

- ARM64 Debug r1042, Release r1043 and instrumented ASan r1044 pass as recorded in the separate matrix. Native ARM64 runtime remains unavailable.
- Complete production caller admission/retirement, ordinary activation, broader F6 clipboard/file cases and the original R1/W G1 targets remain open.
- The private Hue and preserving-panel Wand candidates are unbuilt/unadopted at this checkpoint and have no native qualification.
- This receipt establishes no RAW/Photoshop speed comparison and does not move P1 to Done.

r1041-skills.log SHA-256: `56B97A223C140CC95065920380EE9999EB4BDDEFEA8D354EE1A321B7A520124D`.

r1041-format.log SHA-256: `B588086FD7C7C1EAE274572603E5B2A4214ABEBCAAB9315A4C6E3692F687782A`.

## Existing F6 header-read behavior reconciled

Main rereads the archived Application stdout bound to each native profile report (r1035/r1036/r1041).
All three retain `RunStandalonePathReadContinuationSelfTest`, its 150 path-read checks and 34 boundary
checks with zero failures. The same one-worker test uses an independent compute job to provide bytes to
a real pending native pipe read; it cannot finish if that read occupies the compute lane. Cancellation,
premature and early readiness, replacement, original FIFO, exact nonwrapping read cycles and joined shutdown
retain their original predicates. Source `ApplicationFileJobs.cpp` suspends the same original ticket on
`PathReadWait`; native acknowledgement precedes resource reuse/retirement. The old resource summary saying
the production header wait still occupied a compute lane was stale. Only the direct `ClassifyPathBatch`
reference helper retains that wait. This reconciliation adds no runtime claim beyond the recorded matrix
and closes neither complete phase bounds nor OS stop-to-return, F6 or G1.
