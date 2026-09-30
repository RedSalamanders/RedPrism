# Retained selection and history ownership — 2026-09-30

## Scope and result

P1.9b counts each immutable selection identity once, excluding the live document. Geometry construction caches
the vector object and retained outer/inner capacities, including spare capacity. Byte queries do not walk points.
Empty/moved-from handles report zero. Equal geometry still preserves no-op/redo across distinct capacities.
Allocator and shared-control-block overhead stays covered by global headroom.

`End` moves its pending snapshot/name. Undo/Redo move stack entries and retain one independently owned restore
copy before trim. Session restoration moves that returned document into place. Nine selection-storage and
thirteen history-accounting checks pass in each x64 configuration, including the 1×1 → 16×16 raster Undo fixture
whose 256-byte limit erases its future entry while the returned document/active id remain valid. Existing saved
revision, pending-edit, crop/mask/session and 256 MiB raster-trim numeric expectations remain unchanged.

## Configuration gates

Final x64 Debug, Release and ASan Debug `test.ps1` runs and ARM64 Debug, Release and ASan Debug cross-builds pass.
Skill validation passes (15 skills); formatting checks 242 files. ASan executes its detection probe and stress;
existing composition/IO goldens, WARP self-test, UI smoke, crash probes and harness captures pass. Nineteen RAW
corpus provenance checks pass; RAW decoding is not qualified. Native ARM64 execution was not run.

Logs: `.build/p1-history-storage-final-{x64,ARM64}-{Debug,Release,ASanDebug}.log`, plus skills/format-check logs.
The 282-file source/project/script freeze was checked before every configuration and measurement:
SHA256 `520CD5D8B67284B39E267B15564B480BCFD53A4F829B6D17B3D6D6C4CB17DF3B`.
Release RedPrism SHA256 `2C3675982070A2C2E928F2A3B0B9FA4AD91C6EDC599AF91741179858443749E4`;
DocumentTests SHA256 `282209E0D351A1FBAB21EDF2C02504D96B04457FAB118033256CDC86BBBF4F8A`.

## Earlier IO failures

The initial ASan matrix stopped at `save-durability.folder-rotation`; the existing destination still reopened.
One of twelve isolated ASan IoTests repeat runs failed `v7.legacy-deflate-fixture-created`. A failure-only folder
status/Win32 diagnostic was added without changing assertions. All subsequent 24 diagnostic repeat runs and
the final full matrix passed. The root cause was not established; these failures are retained as an open F6
file reliability investigation, not dismissed or described as a proven history regression.

## Real application loop

Release 0.1.108.0, Ryzen 9 9950X3D (16 cores/32 logical), 66,155,405,312 physical RAM bytes, Windows build 26200,
Balanced power, RTX 5080 driver 32.0.16.1714, scalar dispatch, 32 workers. One warmup and ten observations per
operation. The fixture has two full-size image layers, a mask and an exposure adjustment; it is not the master
four-full-layer stress fixture. Checksums: 4K `04c311cabfc8b925`, 45 MP `0c5adfa7f839c325`.
Hardware 4K and 45 MP each complete all 18 operations. The 45 MP canvas extension is explicitly refused by the
pixel budget; its time is not a successful processing time. WARP timing and other reference machines were not
rerun. Photoshop was not run. No speedup claim follows from this storage/copy correction.

Apply maxima still exceed 2 ms, and synchronous pixel edits/Undo retain UI stalls. Incremental accounting,
lazy image thumbnails, first uploads, chrome costs and the remaining P1 caller migrations stay open. Timing
variation, including the repeated 45 MP run, is recorded below; these are observations, not worst-case guarantees.

## Measurements


### Hardware 4K

Times are milliseconds. Maxima are observations from this run.

| Operation | Median | P95 | Dispatch max | Apply max | UI action max | Frame max |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| zoom-actual | 9.791 | 34.757 | 0.033 | 0.000 | 0.000 | 2.138 |
| zoom-fit | 9.951 | 26.208 | 0.028 | 0.000 | 0.000 | 3.028 |
| levels-open | 61.825 | 95.991 | 3.999 | 9.083 | 0.000 | 0.129 |
| levels-update | 6.348 | 8.014 | 2.135 | 0.032 | 0.000 | 0.399 |
| levels-commit | 83.234 | 91.166 | 0.057 | 3.843 | 7.105 | 2.066 |
| hsv-update | 8.462 | 8.547 | 2.182 | 0.038 | 0.000 | 0.262 |
| exposure-update | 8.447 | 10.494 | 2.186 | 0.034 | 0.000 | 1.393 |
| gaussian-update | 775.317 | 935.160 | 9.486 | 0.146 | 0.000 | 1.091 |
| noise-update | 146.479 | 155.600 | 5.272 | 0.086 | 0.000 | 2.460 |
| grain-update | 8.307 | 10.071 | 2.156 | 0.038 | 0.000 | 0.196 |
| lens-update | 114.101 | 121.234 | 4.413 | 0.120 | 0.000 | 0.989 |
| invert | 192.537 | 207.326 | 205.177 | 0.000 | 0.000 | 2.811 |
| fill | 251.141 | 263.918 | 261.199 | 0.000 | 0.000 | 3.558 |
| clear | 285.053 | 296.825 | 291.430 | 0.000 | 0.000 | 5.581 |
| undo | 15.463 | 21.787 | 2.636 | 0.000 | 0.000 | 0.247 |
| canvas-size-fill | 15.474 | 25.825 | 0.008 | 2.517 | 2.549 | 4.614 |
| image-size-nearest | 136.927 | 146.227 | 0.018 | 7.346 | 3.155 | 2.231 |
| image-size-resolution | 7.426 | 24.254 | 0.011 | 3.191 | 2.456 | 0.225 |

### Hardware 45 MP

Times are milliseconds. Maxima are observations from this run.

| Operation | Median | P95 | Dispatch max | Apply max | UI action max | Frame max |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| zoom-actual | 10.170 | 10.375 | 0.026 | 0.000 | 0.000 | 2.204 |
| zoom-fit | 9.864 | 32.311 | 0.038 | 0.000 | 0.000 | 2.194 |
| levels-open | 388.962 | 457.238 | 4.656 | 3.513 | 0.000 | 0.151 |
| levels-update | 288.836 | 314.837 | 3.706 | 0.074 | 0.000 | 4.872 |
| levels-commit | 464.755 | 520.836 | 0.302 | 4.460 | 26.306 | 17.958 |
| hsv-update | 298.248 | 313.666 | 8.168 | 0.105 | 0.000 | 6.895 |
| exposure-update | 287.856 | 302.420 | 8.326 | 0.086 | 0.000 | 4.944 |
| gaussian-update | 687.378 | 721.743 | 5.481 | 0.139 | 0.000 | 3.021 |
| noise-update | 715.761 | 744.230 | 8.814 | 0.071 | 0.000 | 11.888 |
| grain-update | 8.115 | 14.928 | 2.952 | 0.058 | 0.000 | 0.190 |
| lens-update | 321.036 | 358.123 | 4.778 | 0.140 | 0.000 | 1.856 |
| invert | 1111.197 | 1197.329 | 1131.065 | 0.000 | 0.000 | 72.696 |
| fill | 1362.518 | 1413.006 | 1346.223 | 0.000 | 0.000 | 66.873 |
| clear | 1452.781 | 1524.291 | 1462.799 | 0.000 | 0.000 | 69.274 |
| undo | 11.025 | 96.444 | 95.804 | 0.000 | 0.000 | 0.254 |
| canvas-size-fill-refused-pixel-budget | 11.747 | 24.807 | 0.008 | 0.060 | 7.086 | 0.220 |
| image-size-nearest | 1481.983 | 1642.654 | 0.137 | 31.870 | 73.220 | 132.575 |
| image-size-resolution | 12.991 | 37.870 | 0.079 | 5.710 | 6.629 | 0.322 |

### Hardware 45 MP repeat

Times are milliseconds. Maxima are observations from this run.

| Operation | Median | P95 | Dispatch max | Apply max | UI action max | Frame max |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| zoom-actual | 9.839 | 10.100 | 0.035 | 0.000 | 0.000 | 1.835 |
| zoom-fit | 10.018 | 31.866 | 0.056 | 0.000 | 0.000 | 3.188 |
| levels-open | 464.557 | 506.297 | 8.327 | 4.900 | 0.000 | 0.179 |
| levels-update | 273.962 | 334.235 | 6.294 | 0.072 | 0.000 | 4.570 |
| levels-commit | 441.259 | 463.128 | 0.142 | 5.484 | 23.559 | 24.211 |
| hsv-update | 277.740 | 284.177 | 9.001 | 0.069 | 0.000 | 4.379 |
| exposure-update | 285.495 | 292.893 | 13.562 | 0.091 | 0.000 | 6.417 |
| gaussian-update | 657.763 | 670.386 | 7.478 | 0.079 | 0.000 | 1.599 |
| noise-update | 656.898 | 678.439 | 12.675 | 0.066 | 0.000 | 13.330 |
| grain-update | 8.262 | 8.402 | 2.965 | 0.032 | 0.000 | 0.136 |
| lens-update | 219.893 | 230.377 | 4.850 | 0.057 | 0.000 | 0.752 |
| invert | 714.470 | 729.728 | 672.868 | 0.000 | 0.000 | 57.673 |
| fill | 913.811 | 943.019 | 889.118 | 0.000 | 0.000 | 56.610 |
| clear | 944.023 | 991.451 | 938.341 | 0.000 | 0.000 | 56.924 |
| undo | 13.135 | 21.841 | 4.265 | 0.000 | 0.000 | 0.374 |
| canvas-size-fill-refused-pixel-budget | 8.360 | 8.446 | 0.007 | 0.019 | 1.448 | 0.127 |
| image-size-nearest | 682.756 | 690.644 | 0.185 | 1.859 | 44.576 | 19.935 |
| image-size-resolution | 8.347 | 8.392 | 0.007 | 2.456 | 3.664 | 0.139 |

The first 45 MP nearest resize median was 1,481.983 ms; the repeat was 682.756 ms, close to the earlier sizing
slice observation (677.914 ms). Undo dispatch maximum varied from 95.804 ms to 4.265 ms. This repeat resolves
whether the initial slowdown reproduced consistently, but does not establish its cause or prove a speedup.
The remaining synchronous edits and apply/action stalls remain explicit P1 work.
