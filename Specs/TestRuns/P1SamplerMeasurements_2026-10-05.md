# P1 sampler measurements — 2026-10-05

Status: recorded diagnostic measurements; P1 acceptance remains open

## Source and execution

Six serial actual-loop Release observations ran on the normal desktop after the full validation runs finished, 09:16:21–09:16:33 UTC. Each process exited zero and supplied one expected ten-run row after one excluded warmup, two boundary rows and the exact successful report. No guard was disabled. The earlier r1 display-off refusal remains preserved independently.

The executables/DLLs/PDBs were copied from `p1-residency-host-x64-Release-r95-full-qualified` into the owned `.build/p1-sampler-perf-r2-20261005/Snapshot` and rehashed before and after every child. Archive completion `32D764DC7223619FF53B8EBAD82F6B92E05C65A266DEE5AFBB4E503E06098EA7`; compiler input freeze `F773A1EC601C3F94091CF20398E08A13EAE4F4A585F2A32521B9879F604FB3A7`. Recorder `B60B9E2F6436BDD015EE5C715447517B60B0B7B74565E242A9E632134B223BBB`. Qualified source is committed as `8ab921d`.

R1: Ryzen 9 9950X3D, 16 physical / 32 logical cores, 64 GiB installed class, Windows build 26200, RTX 5080 driver 32.0.16.1714; separate WARP children. Machine headers and fixture checksums remain in every raw stdout. Scalar CPU dispatch. These are the accepted-prefix sampler fixtures, not the complete G1 A document (two images, mask and adjustment), every command, scanout latency, RAW decoding or a Photoshop comparison.

## Recorded timings

All values below are microseconds. P99 values are the existing histogram upper bounds; maxima are measured samples. Initial display and excluded warmup remain visible.

| Device / fixture | Sample median | Message p99 / max | Drain p99 / max | Apply max | Frame p99 / max | Initial frame max | Warmup drain max |
| --- | --- | --- | --- | --- | --- | --- | --- |
| hardware / 3840×2160 | 9523.5 | 64 / 55 | 250 / 320 | 172 | 2000 / 1780 | 12151 | 5809 |
| warp / 3840×2160 | 59994.0 | 250 / 152 | 500 / 552 | 37 | 4000 / 2453 | 18643 | 20012 |
| hardware / 8192×5464 | 9532.0 | 64 / 39 | 250 / 257 | 24 | 2000 / 1535 | 74692 | 1192 |
| warp / 8192×5464 | 276005.5 | 250 / 179 | 1000 / 805 | 52 | 4000 / 3609 | 53536 | 16105 |
| hardware / 10000×10000 | 9315.0 | 64 / 35 | 250 / 306 | 28 | 2000 / 1395 | 90416 | 6771 |
| warp / 10000×10000 | 241231.0 | 250 / 186 | 500 / 946 | 41 | 4000 / 2078 | 46982 | 4601 |

Each measured operation performs 30 readback copies and no busy failure. Hardware uses 30 additional readiness barriers / 60 maps; WARP uses 30 maps and no additional readiness barrier. The worker/lease/owner acknowledgement path remains intact. Ten samples characterize these observations; they do not establish a statistically strong tail-latency guarantee.

## Remaining work identified by the measurements

- Preserve steady sampler owner/apply behavior while completing all live tool/cancellation/device caller scenarios.
- First display records up to tens of milliseconds. `UploadRaster` still uploads the entire dense raster in one `UpdateSubresource`; the separate `UploadRows` bands run their complete loop synchronously. Eight-megabyte bands do not bound a whole frame. Implement admitted resumable residency and worker preparation before H4 closes.
- Excluded warmup has drain overruns. Do not drop the row to declare G1. Trace the exact cleanup/release/utility-cache cause and repair it, retaining its first/warmup timings in the rerun.
- Measure G1 A/B for every P1-due operation with the original latency targets and Release/ETW evidence; this sampler-only record cannot close that requirement.

## Receipt integrity

- `Z:\src\RedPrism\.build\p1-sampler-perf-r2-20261005\started.json`: `FF15B0180B140C0DAD58FA61FD4DCE8F36F09EAACB13A230730BAA5065DF82B9`.
- `Z:\src\RedPrism\.build\p1-sampler-perf-r2-20261005\binary-freeze.json`: `432F35333697131EFCBA6303653D058DADBCCF9F99E1B9C7983AA9644CBEE79A`.
- `Z:\src\RedPrism\.build\p1-sampler-perf-r2-20261005\result.json`: `2A2B61DB046BB5420481B0C9A41FC792D85449C6B8603856D656D18A739CB0C3`.
