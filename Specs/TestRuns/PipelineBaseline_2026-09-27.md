# UI pipeline performance baseline — 2026-09-27

Status: partial operation coverage; benchmark driver passed the configuration gates
Scope: opt-in UI command benchmark (`--bench ui`, `ui45`, `ui100`)
Binary source: `db7fd525d917df84374b9207fe88809771dd5853` plus the uncommitted benchmark patch

## Machine and binary

R1 is an AMD Ryzen 9 9950X3D (16 physical / 32 logical processors, 128 MiB L3 reported, CCD count unknown), 61.6 GiB RAM, Windows build 26200, x64 Release, 32 workers, scalar ISA dispatch, FMA3 math library enabled. Hardware runs used an NVIDIA GeForce RTX 5080 with driver `32.0.16.1714`; WARP used Microsoft Basic Render Driver `10.0.26100.9278`. Both report application version `0.1.88.0`. The power scheme GUID was `{381B4222-F694-41F0-9685-FF5BB260DF2E}`.

Release executable SHA-256 (`.build/x64/Release/RedPrism.exe`): `BFA88CAB19DB75C7DFDE4D6A07923B0122F7A29FEC11336AD8F2D2DEEBFE93B2`.

## Results

Each cell is **elapsed median / p95; maximum message-dispatch time; maximum frame time**, all in milliseconds. Elapsed timing runs from posting the command through idle jobs and a subsequent frame reaching Present. Dispatch and frame values are nested inclusive wall times; frame ends at Present submission, not display scanout. Dispatch and WndProc durations overlap.

| Operation | R1 hardware 3840×2160 | WARP 3840×2160 | R1 hardware 8192×5464 | R1 hardware 10000×10000* | WARP 10000×10000* |
|---|---:|---:|---:|---:|---:|
| zoom-actual | 9.598 / 10.316; 0.032; 1.915 | 55.983 / 61.395; 0.152; 2.307 | 9.887 / 10.135; 0.037; 1.833 | 9.999 / 32.396; 0.043; 2.019 | 262.937 / 277.317; 0.112; 3.250 |
| zoom-fit | 9.739 / 27.712; 0.034; 2.064 | 52.070 / 52.355; 0.094; 2.755 | 9.958 / 10.309; 0.033; 2.025 | 9.884 / 12.238; 0.031; 1.942 | 193.877 / 202.702; 0.202; 3.057 |
| levels-open | 44.863 / 47.401; 46.366; 0.125 | 71.558 / 89.351; 88.501; 0.428 | 608.646 / 678.667; 678.211; 0.123 | 1052.789 / 1317.056; 1264.697; 0.169 | 1872.045 / 1902.449; 1901.254; 0.673 |
| levels-update | 80.317 / 82.594; 80.864; 1.881 | 117.576 / 160.568; 151.194; 5.087 | 144.904 / 187.804; 184.040; 21.611 | 252.065 / 298.627; 277.086; 18.033 | 549.505 / 586.970; 307.268; 285.873 |
| levels-commit | 130.385 / 138.815; 135.304; 2.224 | 107.399 / 117.303; 114.144; 3.206 | 581.628 / 683.703; 646.961; 51.250 | 1608.970 / 2044.205; 1958.095; 110.996 | 2095.456 / 2189.671; 1712.909; 491.215 |
| hsv-update | 207.496 / 236.399; 233.472; 4.866 | 196.993 / 203.145; 199.967; 4.229 | 270.221 / 327.264; 323.699; 10.840 | 563.802 / 1050.073; 1032.809; 17.221 | 785.920 / 827.038; 529.613; 294.867 |
| exposure-update | 105.634 / 120.412; 117.290; 2.188 | 108.529 / 116.945; 113.593; 3.755 | 155.167 / 188.667; 180.649; 9.630 | 289.382 / 315.089; 300.991; 15.282 | 535.222 / 610.103; 333.416; 279.901 |
| gaussian-update / refusal* | 742.818 / 747.192; 746.504; 0.686 | 775.688 / 788.834; 787.646; 1.478 | 653.845 / 682.981; 682.107; 0.915 | 418.277 / 426.475; 422.036; 0.172 | 459.082 / 479.120; 478.777; 0.265 |
| noise-update | 120.025 / 121.229; 119.508; 1.902 | 146.311 / 150.835; 147.785; 3.191 | 783.561 / 839.200; 821.511; 38.574 | 2389.499 / 3257.030; 3055.329; 294.827 | — |
| grain-update | 130.491 / 134.877; 132.680; 2.168 | 157.278 / 159.982; 157.128; 3.016 | 814.120 / 878.549; 851.835; 42.942 | 2305.723 / 3193.815; 3064.951; 207.164 | — |
| lens-update | 53.377 / 55.133; 54.491; 0.618 | 75.879 / 83.693; 82.486; 1.406 | 80.944 / 92.796; 92.010; 0.867 | 133.154 / 135.549; 133.068; 3.117 | — |
| invert | 126.860 / 129.043; 125.913; 3.496 | 164.481 / 175.991; 157.366; 20.992 | 919.554 / 1177.610; 1125.138; 68.958 | 2508.486 / 2546.594; 2429.091; 133.555 | — |
| fill | 170.335 / 173.697; 171.313; 2.383 | 205.366 / 211.311; 193.574; 19.232 | 1158.558 / 1297.481; 1234.001; 69.778 | 3219.217 / 3301.732; 3162.001; 149.507 | — |
| clear | 180.224 / 185.422; 180.995; 4.907 | 211.098 / 221.281; 197.281; 25.137 | 1510.033 / 1580.042; 1513.278; 78.628 | 3113.164 / 3552.551; 3420.893; 157.646 | — |
| undo | 17.768 / 24.896; 2.114; 0.178 | 44.855 / 49.824; 9.117; 0.568 | 10.408 / 17.781; 4.200; 0.283 | 2.791 / 4.028; 2.894; 0.271 | — |

### WARP 100 MP retry — 2026-09-28

This is a separate run using the revised benchmark executable (SHA-256
`6926FDF5E0E0D524AD4C152262B3D755B68EABDBA72834ED8C45191474A76293`). The retained log is
`C:\RedPrism.Perf\baseline-w-ui100-retry-2026-09-28.log` (SHA-256
`189F948607EDA2FC57567636741818CF7FF3EB80ADFA464866A2AD1F0BBB893B`). It completed all 15 operations with
`UI BENCH REPORT status=ok`. Values below are milliseconds; elapsed is median / p95 / maximum, followed by
maximum message-dispatch and maximum frame times.

| Operation | Elapsed median / p95 / max (ms) | Dispatch max (ms) | Frame max (ms) |
|---|---:|---:|---:|
| zoom-actual | 243.302 / 252.224 / 252.224 | 0.092 | 3.153 |
| zoom-fit | 177.026 / 185.933 / 185.933 | 0.108 | 3.448 |
| levels-open | 1336.270 / 1466.207 / 1466.207 | 1465.246 | 0.685 |
| levels-update | 443.550 / 499.574 / 499.574 | 256.414 | 266.891 |
| levels-commit | 1605.857 / 1758.389 / 1758.389 | 1313.957 | 444.014 |
| hsv-update | 640.013 / 702.359 / 702.359 | 442.193 | 267.577 |
| exposure-update | 449.800 / 517.798 / 517.798 | 267.392 | 262.174 |
| gaussian-refused-pixel-budget | 402.050 / 435.004 / 435.004 | 434.720 | 0.253 |
| noise-update | 2224.196 / 2389.309 / 2389.309 | 1983.491 | 432.453 |
| grain-update | 2271.218 / 2410.654 / 2410.654 | 2003.341 | 445.193 |
| lens-update | 124.862 / 187.881 / 187.881 | 137.005 | 4.002 |
| invert | 2594.688 / 3060.501 / 3060.501 | 2782.579 | 602.386 |
| fill | 3514.629 / 4429.232 / 4429.232 | 4192.346 | 247.751 |
| clear | 3780.674 / 3988.252 / 3988.252 | 3796.134 | 221.542 |
| undo | 257.068 / 562.252 / 562.252 | 4.831 | 0.818 |

As with the earlier WARP measurements, zoom elapsed time is not an isolated blit cost: `setupGPU` work can carry
over because preparation does not fence GPU completion. The Gaussian entry is a pixel-budget refusal, not a
successful 100 MP blur. These results make no Photoshop performance or RAW-decode claims.

The 100 MP Gaussian row is `gaussian-refused-pixel-budget`, not successful blur processing. Current document limits refuse its initial grow, and the harness verifies no document or history change. Its displayed duration is the measured refusal path. The fixture is 10000×10000 with one raster layer; the 3840×2160 and 8192×5464 fixtures each contain two raster layers, a mask, and an exposure adjustment layer. Fixture checksums identify generated pixel content, not a RAW corpus.

## WARP 45 MP — 2026-09-28

The revised executable above completed all 15 scenarios on the 8192×5464 layered fixture, exit 0.
This run uses the same measurement boundaries and warmup/run counts. Gaussian Blur succeeds at this size.

| Operation | Elapsed median / p95 / max (ms) | Dispatch max (ms) | Frame max (ms) |
|---|---:|---:|---:|
| zoom-actual | 264.235 / 268.864 / 268.864 | 0.203 | 2.702 |
| zoom-fit | 227.290 / 235.941 / 235.941 | 0.140 | 3.174 |
| levels-open | 948.603 / 1058.075 / 1058.075 | 1057.378 | 0.650 |
| levels-update | 144.942 / 183.377 / 183.377 | 177.528 | 5.802 |
| levels-commit | 941.309 / 1052.946 / 1052.946 | 753.758 | 309.571 |
| hsv-update | 274.870 / 353.048 / 353.048 | 349.136 | 5.604 |
| exposure-update | 165.779 / 196.728 / 196.728 | 192.090 | 4.755 |
| gaussian-update | 786.580 / 840.866 / 840.866 | 838.998 | 1.964 |
| noise-update | 1154.966 / 1204.053 / 1204.053 | 886.792 | 330.342 |
| grain-update | 1017.399 / 1138.776 / 1138.776 | 829.942 | 308.683 |
| lens-update | 68.276 / 71.894 / 71.894 | 70.444 | 1.525 |
| invert | 900.583 / 921.631 / 921.631 | 809.760 | 115.948 |
| fill | 1118.340 / 1164.454 / 1164.454 | 1057.202 | 126.213 |
| clear | 1175.994 / 1208.444 / 1208.444 | 1104.250 | 119.074 |
| undo | 209.606 / 213.262 / 213.262 | 2.668 | 0.489 |

## Method and interpretation

Each listed operation has 10 recorded runs after one excluded warmup, matching [Testing_Benchmarks.md](../Testing/Testing_Benchmarks.md). Median is the average of the two middle elapsed samples; p95 is nearest-rank (`ceil(0.95 × n)`, the largest sample for these 10-run sets); elapsed maximum is exact. Timing histogram p99 is reported by the upper bound of the bucket containing nearest-rank p99; histogram maxima are exact observed durations. Values above are converted from the logs’ microseconds to milliseconds and rounded to three decimals.

Elapsed measures response through Present submission, not monitor visibility. These measurements do not establish UI nonblocking behavior, image-quality correctness, or a performance win over Photoshop.

Preparation waits for idle worker jobs and a submitted frame, without fencing the GPU. The following elapsed
sample may include waiting for that setup frame's GPU work. This is visible in the WARP zoom rows, whose small
dispatch/frame scopes coexist with much larger elapsed intervals. Do not interpret their elapsed values as
isolated zoom or blit costs. GPU-completion measurements remain part of P1.3/P3 qualification.

Coverage remaining before phase exits: Curves/Gradient Map, Motion Blur, Content-Aware Fill, gradient and shape
tools, strokes and retouch, distort and pixel moves, merges/readbacks, selection operations, sizes, project/image
I/O and clipboard. The present rows exercise common history/thumbnail/upload work indirectly; dedicated worst-case
measurements, large selection geometry, non-cancellable library calls and physical-versus-logical worker counts
are still required before the corresponding migrations are qualified. P0.1 and G1 remain open.

## Run completeness and validation

| Record | State |
|---|---|
| `baseline-r1-ui-2026-09-27.log` | Complete: 15 operations; final `UI BENCH REPORT status=ok`. |
| `baseline-w-ui-2026-09-27.log` | Complete: 15 operations; final `UI BENCH REPORT status=ok`. |
| `baseline-r1-ui45-2026-09-27.log` | Complete: 15 operations; final `UI BENCH REPORT status=ok`. |
| `baseline-r1-ui100-2026-09-27.log` | Complete: 15 operations; final `UI BENCH REPORT status=ok`, exit 0. |
| `baseline-w-ui100-2026-09-27.log` | Incomplete: eight operations completed; stopped advancing during `noise-update`, trial 1. The responsive process was idle, its window visible and not minimized. After verifying the exact owned PID/path/arguments, the primary posted its existing benchmark failure-timer message; the normal failure handler/shutdown exited 5. No forced process termination was used. The old driver did not report display-power/occlusion/job state, so the stall's cause is unconfirmed. Rows after Gaussian are not accepted. |
| `baseline-w-ui45-2026-09-28.log` | Complete: 15 operations; final `UI BENCH REPORT status=ok`, exit 0. Revised executable hash above. |
| `baseline-w-ui100-retry-2026-09-28.log` | Complete: 15 operations; final `UI BENCH REPORT status=ok`, exit 0. Separate retry binary SHA-256 is recorded above; its log hash is recorded below. |

Follow-up on 2026-09-28: benchmark-only display/system execution requests now prevent idle display power-off and
restore the previous request on exit. Unavailable presentation fails with visibility, power, suspension, occlusion,
job-idle and Present counters. This diagnostics change does not alter image arithmetic. The four completed logs
and the partial WARP log above were captured by the earlier binary whose hash is recorded above; new validation
and any rerun use the revised binary and are recorded separately.

Full x64 Debug, Release and ASan Debug test gates passed (`.build/logs/ui-bench-x64-{Debug,Release,ASan}-tests.log`,
each ending `All tests passed.`), including the real-loop smoke, corpus offline controls, WARP self-test, crash
probes and harness captures. ARM64 Debug, Release and ASan Debug cross-builds passed; no native ARM64 runtime
qualification is claimed. Skill validation, formatting and the whitespace check passed. Hardware and WARP baselines at all three sizes are
complete; the remaining operation coverage and phase exit criteria remain open.

The tightened ISA parser passed all 13 synthetic dispatch/table controls, including rejection of an undecoded
target, a target inside another instruction, an unknown instruction, a mismatched contribution, an overlong decoded
span and real VEX code beside a valid table. All five Debug and Release images were scanned again using current
source and cached disassembly whose binary/PDB hashes were verified first; all passed. The ASan build ran the
current scanner and its compiled controls as part of its full gate. No application object was allowlisted.

Four scheduler tests now inject a fixed memory snapshot when testing job-completion wakeups. The OS high-memory
notification can legitimately signal the same wake event before a job finishes; it is not a completion. Independent
admission/notification tests still cover the real event behavior. Production scheduling was not changed by this fix.

SHA-256 for retained benchmark logs (including the explicitly incomplete WARP run):

| Log | SHA-256 |
|---|---|
| `baseline-r1-ui-2026-09-27.log` | `AD6E5B3A0B075D8BB05D8FBC76525A22E47EBCA3042D8BD42190C9CDFDCB2C4D` |
| `baseline-w-ui-2026-09-27.log` | `7D24BB3CABFCB5988138174C0BAAA7C745A5A1173EFB95FBF66B0762A09BA5F9` |
| `baseline-r1-ui45-2026-09-27.log` | `195D71489A241E49348B36AC4E714CE4A597FDBF1DD668885CF52FC0FE092C81` |
| `baseline-r1-ui100-2026-09-27.log` | `CEDDC9EF5CA5635A77E066053BC6425B7127712CC29CA82212947795314D711D` |
| `baseline-w-ui100-2026-09-27.log` | `BCD5AB77781ADE6BAFDA1F970BA185665F8A54CBEAC86321D1B55FF272B4A2B3` |
| `baseline-w-ui100-retry-2026-09-28.log` | `189F948607EDA2FC57567636741818CF7FF3EB80ADFA464866A2AD1F0BBB893B` |
| `baseline-w-ui45-2026-09-28.log` | `3E462609184678939AEA0B5A9B0415667D70A32976CBC02445760BB746734C15` |
