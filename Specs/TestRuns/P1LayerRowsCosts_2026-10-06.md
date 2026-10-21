# P1 layer-row owner costs and deferred chrome preparation

## Checklist

- [x] Commit/push the complete stable-identity functional matrix as `3fa176f`.
- [x] Add a registered 128-row smoke and a 10,000-row diagnostic, both with full 1,024-unit names,
  independent pixel/mask references, stable final-record selection, history and retained-charge checks.
- [x] Build the 858-input diagnostic in normal x64 Debug r230 and Release r231; archive and read back both.
- [x] Run smoke WARP r1, full hardware r2 and full WARP r3; preserve every initial and rename boundary.
- [x] Independently validate/copy all baseline receipts, helpers, logs and failed profiling request to D:.
- [x] Measure the repaired full fixture on quiet hardware r4 and WARP r5; every initial boundary is retained.
- [x] Qualify the repair in full Debug r240: exit 0, strict registry r23 passes 10,031 named application controls / 89 stages with all predecessors preserved, including 16 visibility, 3 CLI and 20 interaction checks.
- [x] Qualify full Release r241: exit 0 on the same 858-input freeze; strict registry r23 passes the same 10,031 named application controls / 89 stages and all predecessors; 1,133 archive copies independently verify.
- [x] Qualify full real ASan Debug r242: exit 0, strict registry r23 passes 10,031 named application controls / 89 stages and exact predecessors, 8,055 worker-stress controls and the real detection probe; 1,138 archive copies independently verify.
- [x] Complete ARM64 r33 Debug/Release/ASan Debug cross-builds: actual exits 0 on the same 858-input freeze; all three archives independently verify. Native ARM64 runtime remains unqualified.
- [x] Complete this repair's identical-source six-configuration matrix and Release analysis: x64 full Debug r240 / Release r241 / real ASan Debug r242, three ARM64 r33 cross-builds and Release analysis r237 pass on the same freeze; all archives independently verify. Preserve r236's I/O failure and its nonreproduction in unchanged-source/binary r239.
- [x] Measure the repaired largest fixture on quiet hardware r7 and WARP r8: both actual children exit 0; output proofs pass all 11 trials × 9 inclusive kinds on the current freeze. Retain initial costs and preparation overruns.
- [x] Independently verify the fresh cost archive supplement: all 37 copies pass byte/hash readback, including failed prelaunch recorder r6 and immutable helpers. Manifest `3EA2BEBB901A6BFF1814A061D08F49AB41F81FD510E344CEC697A55DFF385F46` binds the unchanged Release r241 archive.
- [ ] Resolve full preparation and visible-frame costs; fresh full preparation maxima are 7,754 µs hardware / 8,737 µs WARP. No preparation-budget or overall G1 pass is claimed.
- [ ] Finish group/multiselect costs, production/native activation, external client notifications and all due G1 rows.

Status: OPEN. Diagnostic build/correctness results are not P1 completion or production activation.

## Current qualification summary — 2026-10-06

The optional DxUi `PrepareInteraction` library prerequisite is qualified and pushed at `8e8994079e82f79e6765e6a885c03fc687957591`.
The consumer source is frozen at 858 inputs, SHA256
`79546F7D5FDF9EA013C93B8A6FA54735DD6F726DD7A489F92E554652AA8E4F60`.

Full Debug r236 exits 1 only on `IoTests` `ReleaseDirectory` assertion `destination-retry.real-refusal-reached-one-bounded-wait`; it did not run `RedPrism.exe`. Do not attribute the failure to a stale application log. Release analysis r237 exits 0. Actual RedPrism Debug runtime r238 exits 0 across 89 stages; prepared-row interaction passes 20/20 checks, including both new immediate no-paint controls. Its 1,118-copy archive is `D:\RedPrismQualificationArchives\P1-layer-rows-interaction-858-Debug238-runtime-2026-10-06`; source manifest `23B599AD4EF0F0FEDF646044D1E4D65733FBFD83D03CC2E902EAABED3F0A3585`, evidence manifest `5FD34447BB4AA0BA8302A5E7117A636BFF34CF243857E27BBDE60BB926A56C8B`, completion `D4316374EB1836076CB8ACF510ECFC154C3F00EFA5E09B9B0403F64A9411A592`.

Isolated actual `IoTests` Debug r239 exits 0 on the same 858-input freeze and unchanged `IoTests.exe` binary. Every exact-one-bounded-wait assertion passes; no assertion was relaxed and no source changed. The r236 failure remains preserved and was not reproduced; no root cause is established. The 1,118-copy archive `D:\RedPrismQualificationArchives\P1-layer-rows-interaction-858-Debug239-io-runtime-2026-10-06` was verified at 11:29:05.038963 UTC: source manifest `03F260DB22AEA50758C0DF0E19FF72066D5C91FCDE996E82A1F416C58D226C5A`, evidence manifest `CC1EED993C2DD7EEBA84AAB8DED6D4AACDB2B14C03297E1EFF5C3BA11985CD02`, completion `FF3E94F4EB5C2D0136140F5C9A6C8A68BD8698CC7C5080E40EC244215AE7A424`. Actual interval: 11:28:31.238641–11:28:38.074123 UTC; `IoTests.exe` SHA256 `8FD02936A740D39861F4268DC80415B4D4BB485631296C46CA901563A56CD14D`, stdout SHA256 `8F8CB6B923D630436B9068B08D1D90C642238288F004B5682DB6961EBA19154C`, stderr empty.

Full Debug r240 exits 0 (PID 96580, 11:29:11.394801–11:43:38.378539 UTC) on this same freeze. Its [result](../../.build/incremental-x64-test-Debug-r240/result.json) and [strict registry r23 proof](../../.build/incremental-x64-test-Debug-r240/strict-registry-r23.json) establish 10,031 named application controls / 89 stages, all predecessors preserved, 16 visibility, 3 CLI and 20 interaction checks. The [independent archive receipt](../../.build/incremental-x64-test-Debug-r240/independently-verified-archive.json) verifies 1,131 copies at `D:\RedPrismQualificationArchives\P1-layer-rows-interaction-858-Debug240-2026-10-06`: source manifest `665F01BB430A26C186711320CF2F4FB9D3F0DCCE1C443AFDD95BCBB5B954D4C0`, evidence manifest `25E78E8B6CD6D7DBE7CEDF61A1798EDF83CA7A8BA768359D26F1C47047D35405`, completion `AA5792935662B27127EB383C5652FF9ED892F426285F38C47859345529FDD022`.

Full Release r241 exits 0 (PID 69096, 11:45:16.725712–11:51:58.045239 UTC) on the same 858-input freeze. Its [result](../../.build/incremental-x64-test-Release-r241/result.json) and [strict registry r23 proof](../../.build/incremental-x64-test-Release-r241/strict-registry-r23.json) pass 10,031 named application controls / 89 stages with all predecessors preserved, including 16 visibility, 3 CLI and 20 interaction checks. The [independent archive receipt](../../.build/incremental-x64-test-Release-r241/independently-verified-archive.json) verifies 1,133 copies at 11:54:37.9415457 UTC in `D:\RedPrismQualificationArchives\P1-layer-rows-interaction-858-Release241-2026-10-06`: source manifest `6455B6F0B3CDFB57AD1E38B29635979AD6581BA84B7D0FADB515FF4BCD861527`, evidence manifest `DBCF0E33A36458A36A5B110B9DC4878A3B846D0FFFF877B2BEDFEE6C5F092A1E`, completion `C65708A4DEF2207A8BBF738F2BD25F2C8D71792C2D5B0611DEE7552721334EC3`.

Full real ASan Debug r242 exits 0 (PID 96164, 11:51:42.333610–12:02:25.750465 UTC) on the same freeze. Its [result](../../.build/incremental-x64-test-ASanDebug-r242/result.json) and [strict registry r23 proof](../../.build/incremental-x64-test-ASanDebug-r242/strict-registry-r23.json) pass 10,031 named application controls / 89 stages with exact predecessors and 8,055 worker-stress controls; the captured stdout records the real AddressSanitizer detection probe passing. The [independent archive receipt](../../.build/incremental-x64-test-ASanDebug-r242/independently-verified-archive.json) verifies 1,138 copies at 12:03:15.4119739 UTC in `D:\RedPrismQualificationArchives\P1-layer-rows-interaction-858-ASanDebug242-2026-10-06`: source manifest `DF97C6DAE0D3AFFC8CCA6F8CC2D98FB37C049C0DA42C3697AC8BF98EDCA76A39`, evidence manifest `19D4CE68E462B9649828840161E4AD5D948CEF344E6A0F63A7FBBDBF475BD9B1`, completion `006F471E85E5E817883BD67E543DAE2EDA226DAA82D61B1638CE7C6E23E1A94B`.

ARM64 r33 Debug, Release and ASan Debug actual children all exit 0 on the same 858 inputs. The [Debug result](../../.build/residency-arm-builds-r33/Debug/result.json) / [archive receipt](../../.build/residency-arm-builds-r33/Debug/independently-verified-archive.json), [Release result](../../.build/residency-arm-builds-r33/Release/result.json) / [archive receipt](../../.build/residency-arm-builds-r33/Release/independently-verified-archive.json), and [ASan Debug result](../../.build/residency-arm-builds-r33/ASanDebug/result.json) / [archive receipt](../../.build/residency-arm-builds-r33/ASanDebug/independently-verified-archive.json) verify 929, 929 and 933 copies respectively. Native ARM64 runtime remains unqualified. Release analysis r237's [result](../../.build/incremental-x64-analysis-Release-r237/result.json) exits 0 on the same freeze and its [archive receipt](../../.build/incremental-x64-analysis-Release-r237/independently-verified-archive.json) verifies 1,143 copies. This completes the repair's six-configuration matrix plus Release analysis. Fresh cost observations are recorded below; their 37-copy archival supplement independently verifies. Native/production activation, external-client notification/reentrancy, group/multiselect, visible-frame qualification and P1/G1 remain open.

### Fresh quiet cost observations r7/r8

Cost r6 failed in its recorder before `RedPrism.exe` launched: helper r2 attempted to decode the full gate-console byte stream as UTF-8. This is a helper failure, with no product execution or product-failure claim. Its original recorder stderr and unchanged helper r2 are retained in the verified archive supplement. The new immutable `run/start-layer-rows-cost-r3.py` matches the exact ASCII sentinel as raw bytes while retaining all source/product/proof checks.

Hardware r7 and WARP r8 use the preserved Release r241 executable/PDB and the same 858-input freeze. Both actual results and output proofs pass all eleven initial/rename trials with nine inclusive timing kinds per trial; no initial boundary is discarded. The fresh archive supplement independently verifies all 37 copies at `D:\RedPrismQualificationArchives\P1-layer-rows-interaction-858-Release241-2026-10-06\Supplements\layer-rows-interaction-r1`; manifest SHA256 `3EA2BEBB901A6BFF1814A061D08F49AB41F81FD510E344CEC697A55DFF385F46`. Original qualification manifests/completion are unchanged.

| Run | Actual child / exit | UTC interval | Evidence |
| --- | --- | --- | --- |
| cost-r7 hardware | 104684 / 0 | 12:04:14.284134–12:04:15.114844 | [result](../../.build/layer-rows-cost-r7/result.json), [output proof](../../.build/layer-rows-cost-r7/output-verified.json) |
| cost-r8 WARP | 108804 / 0 | 12:04:41.384950–12:04:42.051019 | [result](../../.build/layer-rows-cost-r8/result.json), [output proof](../../.build/layer-rows-cost-r8/output-verified.json) |

| Measured maximum inclusive elapsed time (µs), all eleven trials | Hardware r7 | WARP r8 |
| --- | ---: | ---: |
| Owner request | 327 | 350 |
| Result application | 346 | 372 |
| Chrome synchronization | 303 | 342 |
| Completion drain | 351 | 377 |
| Message dispatch | 1,606 | 1,945 |
| Window message | 2 | 2 |
| Full explicit preparation | 7,754 | 8,737 |
| UIA navigation server call | 12 | 19 |
| UIA full-name server call | 35 | 39 |

The following are the largest reported `P99UpperUs` histogram bucket bounds across the separate trial distributions. They are distinct from the measured maxima above and do not establish a pooled or exact p99. Nested inclusive scopes must not be added.

| Maximum per-trial p99 bucket upper bound (µs) | Hardware r7 | WARP r8 |
| --- | ---: | ---: |
| Owner request | 500 | 500 |
| Result application | 500 | 500 |
| Chrome synchronization | 500 | 500 |
| Completion drain | 500 | 500 |
| Message dispatch | 2,000 | 2,000 |
| Window message | 2 | 2 |
| Full explicit preparation | 8,000 | 16,000 |
| UIA navigation server call | 1 | 1 |
| UIA full-name server call | 1 | 2 |

Initial full preparation is 5,892 µs hardware / 8,737 µs WARP. Both initial and maximum preparation observations exceed the 4 ms budget; no preparation-budget pass is claimed. These are hidden metadata/embedded STA server diagnostics with `performanceGate=unmeasured`; visible-frame latency, external UIA RPC, complete GPU residency, group/multiselect and native/production activation remain unqualified. No overall G1 or P1 completion follows from the measured rows.

### Final documentation boundary

After qualification and measurement, only the consumer skill's qualification wording changed among the 858 frozen inputs. [The byte comparison](../../.build/layer-rows-doc-delta-r1.json) verifies the other 857 inputs are unchanged, including every compiled source, build/test input and lock. The original tested freeze and skill bytes remain archived. The current skill SHA256 is `F12D1878C5C87036ACEA20BD06D9B3029F25F044DDBA8817C36FFB85CDDF736A`; this documentation update does not claim another runtime qualification. Fresh `validate-skills.ps1` and `format.ps1 -Check` pass 15 skills and 733 files (`layer-rows-interaction-skills-r3.log`, `layer-rows-interaction-format-r3.log`).

## Baseline source and actual runs

The source manifest contains 858 inputs, SHA256
`2CDF720E757DC1D59E23AC16766FA0A0148A0EE657C7C08E149F58F71A11B5E9`.
Debug build r230 and Release build r231 exit 0 through the normal build entrypoint, all five linked-image ISA
checks and actual pinned-library provenance. They do not qualify the new runtime matrix.

Release r231 archive: `D:\RedPrismQualificationArchives\P1-layer-rows-858-Release231-build-2026-10-06`,
1,118 independently verified copies; completion SHA256
`2D7D967301FF28A70F708AB7CA33C879D3F5CBB298F86C2683B4E7051239825A`.
Debug r230 has 1,118 verified copies, completion
`FB1201B014B145EF7BB39B6E62E1EBD2641206A2244F7120F56FD5B8A45719A3`.

| Run | Actual child / exit | UTC interval | Fixture |
| --- | --- | --- | --- |
| cost-r1 | 74768 / 0 | 09:08:50.278507–09:08:50.585960 | WARP smoke, 128 layers, initial + 2 renames |
| cost-r2 | 113940 / 0 | 09:10:30.831862–09:10:31.894932 | Hardware, 10,000 layers, initial + 10 renames |
| cost-r3 | 101228 / 0 | 09:11:32.413702–09:11:33.316872 | WARP, 10,000 layers, initial + 10 renames |

All runs use the same Release executable/PDB and frozen source. Each full boundary reads all 10,000 complete
names (10,240,000 UTF-16 units), preserves literal first/last image and mask bytes, retains the correct Guid/item
identity and image storage, records exactly one history entry per rename and observes the complete retained row
charge after retirement. The separately invoked registered PowerShell smoke validator also passes.

Machine: AMD Ryzen 9 9950X3D, 16 physical / 32 logical processors, 66,155,405,312 physical-memory bytes,
Windows build 26200, Balanced power plan, x64 Release, scalar dispatch, 32 workers. Hardware is NVIDIA
GeForce RTX 5080, driver 32.0.16.1714; WARP is Microsoft Basic Render Driver, 10.0.26100.9278.
These are hidden 2×1-canvas metadata diagnostics with direct embedded STA provider calls. They do not measure
external UIA RPC, visible-frame latency, image throughput or complete GPU residency. Worker settling is reported
separately from owner scopes; nested timing scopes are inclusive and must not be summed.

## Measured defects retained

| Maximum inclusive elapsed time (microseconds, all boundaries) | Smoke WARP | Full hardware | Full WARP |
| --- | ---: | ---: | ---: |
| Owner request | 6,583 | 26,072 | 24,814 |
| Explicit chrome preparation | 13,454 | 6,006 | 8,644 |
| Result application | 2,831 | 22,172 | 22,469 |
| Chrome synchronization | 6,546 | 26,050 | 24,789 |
| One UIA full-name server call | 1 | 21 | 24 |
| One UIA navigation server call | 1 | 16 | 10 |

The full owner-request maximum exceeds 16 ms and result application exceeds 2 ms. No latency pass is claimed.
The first boundaries remain included; no warmup exclusion or target change hides them.

Source inspection identifies repeated `CanTransform` calls constructing complete `HierarchyEntries` and
`EffectiveVisibleIDs`, including unrelated full-name copies. The single-layer repair uses the existing
allocation-free inherited-visibility query. Group queries still require separate bounded-work qualification.
`SyncChrome` also paints the DxUi surface during owner callbacks. The repair leaves retained value/layout
updates there and defers painting to the existing `Renderer::Render` frame stage. The registered smoke now
checks that request/publication changes neither preparation, composition nor surface-allocation counts before
its explicit preparation. These changes await the complete current runtime/analysis matrix. Fresh measurements
below are actual observations of the repaired source, without an overall G1 claim.

## HISTORICAL — Earlier deferred-paint draft: source and quiet cost runs

The repaired source has 858 inputs, SHA256
`89993FD1F5EFA4DA2BBDF69704B4A540CED60EED5B4704929D824212BCE73BE6`.
Release build r232 exits 0 through the normal build/ISA/provenance gates. Its 1,120-copy verified D: archive is
`P1-layer-rows-state-only-858-Release232-build-2026-10-06`, completion
`4488685CB6DE7BA85150FE72877E24C39BD1FAE7D0FBFAE14F811966C599CF4A`.
Both cost children run after all builders exit and before the next gate starts. Their recorded executable/PDB
hashes match this preserved archive. All eleven initial/rename boundaries pass, including the new no-paint,
no-composite and no-surface-allocation publication invariant. The literal names/pixels/history/charge checks
are unchanged.

| Run | Actual child / exit | UTC interval |
| --- | --- | --- |
| cost-r4 hardware | 79980 / 0 | 09:31:31.568640–09:31:32.452518 |
| cost-r5 WARP | 104500 / 0 | 09:31:55.853754–09:31:56.557915 |

| Maximum inclusive microseconds, every boundary retained | Hardware r4 | WARP r5 |
| --- | ---: | ---: |
| Owner request | 377 | 357 |
| Result application | 354 | 325 |
| Chrome synchronization | 344 | 337 |
| Completion drain | 358 | 328 |
| Message dispatch | 1,846 | 1,629 |
| Explicit chrome preparation | 7,766 | 7,563 |

Request and apply now satisfy their absolute limits on this diagnostic. Initial explicit preparation remains
above 4 ms; the complete frame distribution, external UIA clients, group/multiselect fixtures, first-display
resource work and native activation remain open. Do not infer a complete G1 pass from these observations.
The repaired cost supplement has 24 verified copies; manifest
`DAAA7EF09B362F2B3EA971063B37E97D8D6A54F3F9B39D1C145D18DD6BCB761D`.

### HISTORICAL — Earlier deferred-paint draft: qualification attempts

The first full Debug r233 attempt exits 1 before compilation: two simultaneous x64 dependency restores compete
for `vcpkg-running.lock`, and the second fails to take its exclusive lock. No test runs or source defect is
claimed from it. The failed attempt is preserved in its 1,127-copy archive, completion
`FC7DE9294C1463F33690C4DF18F541BA54BE258DB419FEFF4F0C0F0EE88D761D`.
Full Debug r234 retries the unchanged source after dependency restore is quiet. Release analysis r235 starts
only after r234 finishes its dependency/build phase. Future x64 dependency restore phases are serialized.

All three ARM64 r32 cross-build children exit 0 on the same frozen inputs, with actual kernel process intervals
and verified archives: Debug 929 copies, completion
`3FE4D63F88863DCAC5354AE163F38D8EDCE88CD8F59D3A4A1CF4C38B2BA15421`;
Release 929 copies,
`602D982D0B01925D707CE1C4DB90869C39567CEE510CC73C650AA1781E9E8267`;
ASan Debug 933 copies,
`832001B45DB30CA895D46C8BBEC3F0B408DB4175E1CA25AD4A5D4DB5D81EC16E`.
This is cross compilation, not native ARM64 execution. The original launcher reports a stale output-root label
`r31`; its executed runner writes the fresh `r32` directory. Both the original receipt and a separately hashed
correction are retained; none of the actual child results or original helpers is rewritten.

### HISTORICAL — Earlier deferred-paint draft: r234/r235 outcome

The identical 858-input source freeze
`89993FD1F5EFA4DA2BBDF69704B4A540CED60EED5B4704929D824212BCE73BE6` was archived after the current runs.
Full Debug test r234 exits 1 (PID 58116, 09:32:27.285758–09:40:36.298099 UTC). The inherited-visibility
stage passes all 16 controls. The prepared-layer interaction stage passes 12 of 18 and fails six input/readiness
checks: sibling-row pointer selection, expander input while a prior tree awaits publication, held-worker stale
input handling and retirement, collapse publication, and A-B-A reopen row identities. Deferred chrome painting
therefore remains unqualified with the full interaction behavior; preserve this failed result.

Release analysis r235 exits 0 (PID 33380, 09:33:13.752879–09:41:25.861602 UTC) on the same 858-input freeze.
It is analysis-only and does not qualify runtime behavior. Earlier quiet cost runs and ARM64 cross-builds also
remain valid for their recorded source and scope, but do not qualify this functional repair.

Debug r234 archive: `D:\RedPrismQualificationArchives\P1-layer-rows-state-only-858-Debug234-interaction-failed-2026-10-06`,
1,127 verified copies, completion SHA256
`7F87EB8614DAB0DE06634C2C5E3A8774A4084CCF9A8537B73F805DED8E8F8C47`.
Release analysis r235 archive: `D:\RedPrismQualificationArchives\P1-layer-rows-state-only-858-analysis235-2026-10-06`,
1,143 verified copies, completion SHA256
`0D6E89D0B65DDA49976A402B28092ADD77521754244172AD570A68E37BAB5231`.
The later input-readiness repair, preserving deferred painting, is qualified on current freeze
`79546F7D5FDF9EA013C93B8A6FA54735DD6F726DD7A489F92E554652AA8E4F60` in the complete matrix recorded above.
The earlier r234 failure remains preserved. Current r7/r8 observations are separate; preparation/frame costs,
native/production activation and P1/G1 remain open.

## Evidence preservation and profiling limitation

The original build manifests/completion are unchanged. Their fresh supplement
`Supplements\layer-rows-baseline-r1` contains 37 verified copies, including all three raw run directories,
actual recorders, smoke logs and reviewed archive helper. Manifest SHA256:
`F8DE90624CCB20B86B3C650945A6F6DA2C6A8A330937560939B7A4609A657961`.
The primary corrected the cheaper agent's initial product-hash/device assumptions before executing the helper;
actual archive verification exits 0.

WPR was initially idle. Starting the repository's `RedPrism` profile failed with `0xc5585011`,
"Failed to enable the policy to profile system performance." The original `start.log` is preserved; there is
no ETW capture or successful profiling claim. Actual in-process timing remains available. Release ETW and
complete visible-frame G1 qualification remain open.


## Current functional receipt reconciliation — 2026-10-20

The corrected [monitor/cursor integration](P1MonitorAndCursor_2026-10-20.md) has the same 1,885-input
source freeze in full native x64 Debug, Release and real ASan. Main independently reads each exact archived
Application log and its successful profile/runtime receipts. All three contain the same 70 existing named
prepared-row controls and five separate zero-failure summaries: accessibility 17, retained capacity 12,
client notifications 11, stable identity 21 and grouping 9. Every predecessor Application fact is retained.
These are executed functional results; the earlier source-only/unexecuted descriptions are historical.

The cross-build/analysis rows and complete source/archive identities are recorded in that integration's table.
No new layer-row preparation timing, external-client RPC latency, ordinary activation or G1 result is measured
by this reconciliation. The older measured preparation misses above remain unresolved; they are not replaced
by passing functional assertions.


## Current exact-product cost diagnostics — 2026-10-20

The complete current monitor/cursor matrix passes, including recovered same-source analysis `m1an2`.
These two runs use the exact archived, full-runtime-qualified `m1r2` Release image and PDB, not the
analysis-linked image. Source freeze is `A9390781134537A6CFE7341F67D4CD9824A0EA1D35DDD80926BB898D2C92CFED`;
image SHA256 is `1141ED2D4EE1996259D69F72506E434D892E24A0302ECF6615BC6DE9898DE0CB`.
Main verifies raw reports, actual exits and source/product bookends. Each run includes the initial epoch
and ten renames: 11 boundaries, 99 timing rows, and all 10,000 full 1,024-unit names at every boundary.

Machine: Ryzen 9 9950X3D (16 cores / 32 logical processors), 66,155,405,312 reported physical-memory bytes, Windows build 26300,
RTX 5080 driver 32.0.16.1714, Balanced power scheme. The product reports scalar dispatch and enabled
CRT FMA3 policy. These metadata/provider measurements do not benchmark pixel kernels or visible frames.

All values below are microseconds. Trials 0 and 1 remain visible; trials 2–10 do not replace them.

| Device | Actual PID / exit | Initial Prepare | First rename Prepare | Largest Prepare, renames 2–10 | Largest Apply | Largest drain |
| --- | --- | --- | --- | --- | --- | --- |
| hardware | 77216 / 0 | 6,383 | 8,621 | 2,637 | 260 | 265 |
| WARP | 106544 / 0 | 7,387 | 6,520 | 3,537 | 240 | 245 |

Maximum per-trial p99 histogram upper bounds for Prepare are 16,000 us (hardware) and 8,000 us (WARP).
They are recorded bounds for these selected samples, not a pooled or complete G1 percentile. Request,
dispatch, Apply and drain remain below their corresponding owner targets in this selected run. Early
Prepare intervals exceed the 4 ms p99 target and need attribution before ordinary prepared-row activation.
`ChromeHost::Prepare` includes application layout, embedded-view preparation and accessibility attachment/update;
those existing boundaries must be measured separately before selecting a repair.
No sample is discarded, relabelled as an extra warmup or used to close the complete G1/first-display gate.

Direct embedded-STA provider navigation/name p99 upper bounds are at most 1 / 2 us per trial. Retained
maxima include 1,281 us for one hardware name read and 1,318 us for one WARP navigation read. These are
same-process server calls; external UIA RPC and visible-frame latency remain unmeasured.

The original build-free preflight predicates returned zero before each run. Continuous build-overlap
observation was not collected for these selected diagnostics, so no stronger overlap claim is made.
Evidence root: `Z:\src\RedPrism\.build\P1Evidence\p1-layer-rows-current-costs-main-r1`.
`identity-before.json`, raw outputs/results, `completed.json` and Main's `main-evidence-readback.json`
retain the selected product identities and all original measurements.

### Separate first-use GPU granularity diagnostic

An isolated WARP process, actual PID 74484 / exit 0, records 162 rows: six texture sizes, three retained
trials and nine phases. Original source/product bookends pass. No application rendering code changed.
Maximum owner Signal intervals are 33, 39, 453, 448, 1,437 and 3,676 us at sides 512, 1,024, 2,048,
4,096, 6,700 and 10,000 respectively. This does not establish the cause of an application overrun or
qualify a production GPU fix. Earlier R4 and actual application misses remain retained.

The coordinated sorting hold was released by its authorized owner. A subsequent original diagnostic
preflight refused with exit 7 before any compile/runtime while the DxUi build slot was occupied. That
receipt remains intact. A later attempt waited for the same unchanged predicate and completed; it did
not terminate another process. Evidence root: `Z:\src\RedPrism\.build\P1Evidence\p1-gpu-first-use-probe-private-r5`.


## Current Release CPU stack attribution — 2026-10-21

Status: diagnostic attribution verified; the remaining cost repair, controlled timing, ordinary
prepared-row activation and complete G1 stay open. The DxUi pin and product C++ source are unchanged.

The exact plain Release executable/full PDB and local runtime DLLs come from the qualified `oh4r1`
archive for published code `a2f56263c7a36a9d5bfb24c1c18cb297885bbae6`. The real WARP/scalar
10,000-layer, 1,024-unit-name benchmark child 71216 exits0 with its original correctness predicates.
The owner retains its creation-returned primary thread 36812, resumes it exactly once, joins it and
closes only its own collector GUID. The installed CPU-high configuration samples at 4,000 Hz.

Main verifies Running/Stopped state for all three zero-exit collector commands, the nonempty diagnostic
archive and unchanged product bookends. Archive SHA256:
`4A378C9323AECA0D1D7836D651B1632ED24B6A9AE7053080E99F6EF2DF7FC0D3`.
The extracted ETL has 49,812 sampled-profile events, 18,745 with stack walks. A filtered WPA export and
an independent direct reader agree on exactly 1,293 primary-thread samples and 323.077500 ms of sampled
CPU weight. Of those samples, 1,291 have stacks and 1,192 have a resolved application frame.

The direct reader uses pinned Microsoft.Windows.EventTracing.Processing.All 1.12.10 and its recorded
dependency lock. It reads the original ETL; no replacement benchmark is run to repair an export.
Its final parent 85848 exits0. [Microsoft's symbol API](https://learn.microsoft.com/en-us/windows/apps/trace-processing/symbols)
documents this direct CPU-sample/stack workflow. Full frames/source symbols and per-sample relative
timestamps remain in the private receipt. All 1,293 SDK timestamps are explicitly partial: raw QPC is
unavailable, so no exact per-trial alignment to benchmark QPC intervals is claimed.

The 156 samples whose actual call stack contains ChromeHost::Prepare have 38.545800 ms of sampled CPU
weight. Mutually exclusive categories are:

| Actual sampled call path | CPU weight across the recorded Prepare calls |
| --- | --- |
| Embedded preparation with a Direct2D EndDraw ancestor | 22.123900 ms |
| Other embedded painting/preparation | 13.131400 ms |
| Chrome accessibility publication | 2.785400 ms |
| Other chrome layout/accessibility attach | 0.505100 ms |

Observed EndDraw stacks include Direct2D deferred flush and WARP glyph drawing/shader compilation.
Other stacks include control painting, text layout, allocation and UI Automation snapshot work. These
are sampled CPU categories, not wall-clock budgets or a diagnosis of one specific rename interval.
Cold Attach also contains substantial typography/font-family work; it remains a separate startup path.
The evidence supports investigating bounded painting/flush work and cold resource preparation before
speculating about row-name truncation or adding hidden warmups. No causal performance repair follows
from observing a sampled frame.

Preserve the first collector's invalid-session refusal despite its zero CLI exits and absent trace;
the new transport creates its output directories and refuses explicit zero-exit errors/absent sessions.
Preserve all three aggregate-only WPA exports and the first direct-reader refusal on an unavailable
raw timestamp. The corrected reader records partial/null raw timestamps and retains every count/weight
check. No assertion, timing requirement, compiler/pin policy or product behavior changes.

An external PresentMon process is present in the ETL. The preflight's process-name/benchmark-argument
checks therefore do not establish a globally quiet host. This capture is attribution only; it qualifies
no G1 timing, paired speedup, first-display bound or host availability. Retain all original budget
misses. Next, use controlled hardware/WARP timing and a concrete painting/resource change to close
the remaining preparation and full consumer/activation gates.

Evidence: `Z:\src\RedPrism\.build\P1Evidence\p1-owner-cost-current-main-r1`;
`rows-cpu-high-r6/main-cpu-attribution-r1.json`, `DirectReader-r2/samples.jsonl` and all failed transports
are preserved. Sample JSONL SHA256:
`627C7205FED412529C955CF5CB368B38087864FA7B34318B9CF291D8F2666E77`.


### Matching hardware diagnostic

The separate hardware capture uses the same qualified executable/PDB/DLLs on the RTX 5080
(driver 32.0.16.1714), with the original 10,000-layer/1,024-unit-name fixture and all correctness checks.
Benchmark child 107908 and primary thread 76376 complete with actual child exit 0. All three owned collector
commands exit 0 and explicitly report Running/Running/Stopped. Source and product identities remain verified.
Diagnostic archive SHA256:
`790390F505E6C42FFAE6B035BB7B0A0A99AB666E12738169904CDF47BAC24EBF`.

A cheaper helper prepares the extraction/export sidecar. Main reviews it and adds independent target-thread,
staged product/archive hashes, empty collector stderr, a 128 MiB ETL bound and closed-input bookends before
execution. The raw export is explicitly aggregate-only; Main independently verifies its single primary-thread
row. The generic direct reader keeps the earlier qualified reader intact and requires the independent expected
sample count as a bounded explicit argument. Its actual parent 79000 exits 0 with empty stderr.

WPA and the direct reader agree on 1,840 samples / 459.004200 ms of primary-thread sampled CPU; 1,839 have
stacks and 1,716 have a resolved application frame. All raw timestamps remain partial/null. The 123 samples
containing the exact ChromeHost::Prepare frame have 30.824800 ms of sampled CPU weight:

| Actual sampled call path | CPU weight across the hardware Prepare calls |
| --- | --- |
| Embedded preparation with a Direct2D EndDraw ancestor | 11.768700 ms |
| Other embedded painting/preparation | 15.027500 ms |
| Chrome accessibility publication | 3.787000 ms |
| Other chrome layout/accessibility attach | 0.241600 ms |

The hardware trace also retains the first 5.650 ms and first-rename 10.183 ms Prepare intervals; subsequent
observed intervals are 2.176–3.546 ms. These are individual instrumented observations. The captures are
unpaired, run under the standing FrameView/PresentMon services and do not establish a speedup, globally
quiet host, exact per-trial sampled-QPC correspondence or G1 pass. Hardware acceleration alone does not
remove the shared painting and accessibility work; a bounded preparation repair still needs proof.

The new private reader's first build records NU1900 audit-transport warnings. A forced locked restore
refreshes the stale audit assets using its scoped cache; the following build reports 0 warnings/0 errors.
No package version, dependency lock or audit policy changes. This is diagnostic-tool preparation, not a
product build result. The hardware reader succeeds on its first execution; the earlier failed raw-timestamp
reader belongs to the WARP capture and stays separately preserved.

Evidence: `rows-hardware-cpu-high-r7/main-cpu-attribution-r1.json` and its independent
`main-cpu-attribution-review-r2.json` under the same private evidence root. Hardware sample JSONL SHA256:
`3F599AFCAB5A8147D0CB6121E5CDA5CD3828295B044096EF08B48DE922755828`.
No library or application code/pin changes in this attribution step.
