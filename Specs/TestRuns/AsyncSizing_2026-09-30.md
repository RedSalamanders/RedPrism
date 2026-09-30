# Asynchronous sizing qualification — 2026-09-30

## Scope and result

Immutable Image Size and Canvas Size jobs, checked owner application, cancellation and peak reservations pass
the full configuration matrix. Eleven independent `sizingwork.*` expectations preserve the existing numeric
size goldens and cover extension cutouts, nearest sparse/mask reads, no-op/precancel, stale/replaced/invalid
application, undo/redo, rounded reservation bounds and nonuniform rotated/uniform-mask behavior.
The real event-gated host path covers copied image/canvas drafts, panel → size → younger undo ordering,
cancellation before and after preparation, failure/allocation/status outcomes, changed generation, undo/redo
restore, document replacement and reopened-card lifetime. Hidden completion keeps no display surface or Present.

D13 supplies the application session an unsigned 64-bit min(physical RAM / 4, 2 GiB) history limit. Default
class/session construction and failed-query fallback remain 256 MiB; the legacy `history.trim-256mib` passes.
The earlier 45 MP real-loop run failed its required Image Size undo expectation with the old 256 MiB cap.
The larger limit retains that entry and the current run completes all 18 operations. This is a functional
history-cap correction, not a speed comparison with the earlier failed run.

## Configuration gates

`validate-skills.ps1` (15 skills), `format.ps1 -Check` (230 files), all x64 Debug/Release/ASan Debug `test.ps1`
runs and all ARM64 Debug/Release/ASan Debug cross-builds pass. ASan ran the real detection probe and stress.
Legacy document/composition/IO numeric expectations, ISA controls, WARP self-test, UI smoke, crash probes,
harness captures and offline provenance of 19 licensed RAW samples remain green. These RAW checks do not
qualify decode/develop. Native ARM64 runtime, other reference machines and Photoshop were not run.

Logs: `.build/p1-sizing-history-{x64,ARM64}-{Debug,Release,ASanDebug}.log`, plus the corresponding skills and
format-check logs. The 270-file source/project/script freeze was checked before every configuration and
before measurement; SHA256 `FB3AF47A088889F262F15E717AD5D0FB0F30C1F739F5A4879C832CAC1DC37933`.
Release RedPrism SHA256 `911B05D98BBAA44BEA1AB9146A7486E77A8D121992385ECFDD13D051BD2E1169`;
HostTests SHA256 `613DF3AFD2C1166F46F5B06845DCC572183AD19D3615F110577ADEA660951830`.

## Release cancellation observations

Each sizing case gates worker entry, waits 20 ms on an event and then requests stop. One warmup precedes ten
recorded observations; all ten acknowledge cancellation with empty detached output and unchanged source,
with no early completion or overlap. The event proves worker entry, not a particular inner loop/allocation.
Stop-to-return includes cleanup, page faults and scheduling; these observations are not worst-case guarantees.

| 8192×5464 single-layer case | Declared additional peak (bytes) | Median stop-to-return (ms) | P95/max (ms) |
| --- | ---: | ---: | ---: |
| Canvas colored extension (+128 each axis) | 201442048 | 1.313 | 1.758 |
| Image Size 3/4 each axis, Nearest | 117555968 | 2.352 | 4.108 |
| Image Size 3/4 each axis, High | 117555968 | 2.253 | 2.421 |

All 20 filter/raster/size cancellation cases report `status=ok` in `.build/p1-sizing-history-cancellation.log`.
Some older small cases complete before the stop; their sample/early counts are reported rather than hidden.

## Real application loop

Ryzen 9 9950X3D (16 cores/32 logical), 66,155,405,312 bytes physical RAM, Windows build 26200, Balanced power,
32 workers, scalar dispatch, Release version 0.1.104.0. Hardware: RTX 5080, driver 32.0.16.1714. WARP:
Microsoft Basic Render Driver, driver 10.0.26100.9278. Each operation has one warmup and ten measured trials.
The fixture has two full-size image layers, a mask and an exposure adjustment; it is not the master plan's
four-full-layer stress document. Checksums: 4K `04c311cabfc8b925`, 45 MP `0c5adfa7f839c325`.

All three reports complete 18 operations. Tables below are milliseconds, preserving measured maxima.
At 45 MP, Canvas Size colored fill is an explicit refusal under the unchanged 100 M image-pixel budget;
it is not a successful full-size fill timing. The separate cancellation case above exercises a real fill.

### Hardware 3840×2160

| Operation | Elapsed median | Dispatch max | Apply max | Action max | Frame max |
| --- | ---: | ---: | ---: | ---: | ---: |
| zoom-actual | 9.487 | 0.025 | 0.000 | 0.000 | 1.560 |
| zoom-fit | 9.659 | 0.062 | 0.000 | 0.000 | 1.762 |
| levels-open | 54.398 | 3.167 | 2.657 | 0.000 | 0.159 |
| levels-update | 4.961 | 2.040 | 0.035 | 0.000 | 0.286 |
| levels-commit | 82.009 | 0.051 | 6.761 | 4.392 | 2.869 |
| hsv-update | 8.449 | 3.011 | 0.042 | 0.000 | 0.259 |
| exposure-update | 8.363 | 1.938 | 0.028 | 0.000 | 0.219 |
| gaussian-update | 745.101 | 5.870 | 0.072 | 0.000 | 0.859 |
| noise-update | 124.028 | 3.277 | 0.085 | 0.000 | 2.391 |
| grain-update | 8.317 | 2.734 | 0.042 | 0.000 | 0.145 |
| lens-update | 109.174 | 2.982 | 0.089 | 0.000 | 0.902 |
| invert | 160.280 | 211.586 | 0.000 | 0.000 | 3.676 |
| fill | 186.157 | 228.608 | 0.000 | 0.000 | 3.444 |
| clear | 196.303 | 218.788 | 0.000 | 0.000 | 5.432 |
| undo | 6.786 | 2.003 | 0.000 | 0.000 | 0.218 |
| canvas-size-fill | 15.646 | 0.007 | 2.893 | 1.623 | 3.882 |
| image-size-nearest | 121.437 | 0.009 | 2.850 | 1.862 | 1.810 |
| image-size-resolution | 8.324 | 0.010 | 1.440 | 2.880 | 0.159 |

### Hardware 8192×5464

| Operation | Elapsed median | Dispatch max | Apply max | Action max | Frame max |
| --- | ---: | ---: | ---: | ---: | ---: |
| zoom-actual | 9.928 | 0.050 | 0.000 | 0.000 | 1.979 |
| zoom-fit | 9.936 | 0.031 | 0.000 | 0.000 | 1.915 |
| levels-open | 256.793 | 4.085 | 2.944 | 0.000 | 0.138 |
| levels-update | 276.716 | 3.351 | 0.078 | 0.000 | 4.971 |
| levels-commit | 437.915 | 0.111 | 3.954 | 15.606 | 28.792 |
| hsv-update | 281.162 | 10.781 | 0.128 | 0.000 | 5.609 |
| exposure-update | 276.275 | 10.885 | 0.093 | 0.000 | 4.143 |
| gaussian-update | 624.542 | 9.783 | 0.073 | 0.000 | 1.467 |
| noise-update | 648.118 | 11.021 | 0.066 | 0.000 | 9.043 |
| grain-update | 8.297 | 2.042 | 0.026 | 0.000 | 0.160 |
| lens-update | 216.530 | 7.020 | 0.065 | 0.000 | 0.752 |
| invert | 822.110 | 899.910 | 0.000 | 0.000 | 66.183 |
| fill | 1018.059 | 1162.585 | 0.000 | 0.000 | 59.080 |
| clear | 1079.328 | 1186.003 | 0.000 | 0.000 | 70.411 |
| undo | 12.318 | 2.899 | 0.000 | 0.000 | 0.215 |
| canvas-size-fill-refused-pixel-budget | 8.352 | 0.009 | 0.041 | 1.558 | 0.125 |
| image-size-nearest | 677.914 | 0.178 | 2.906 | 46.035 | 16.538 |
| image-size-resolution | 8.381 | 0.013 | 2.411 | 6.758 | 0.125 |

### WARP 3840×2160

| Operation | Elapsed median | Dispatch max | Apply max | Action max | Frame max |
| --- | ---: | ---: | ---: | ---: | ---: |
| zoom-actual | 60.481 | 0.124 | 0.000 | 0.000 | 2.918 |
| zoom-fit | 52.015 | 0.089 | 0.000 | 0.000 | 2.867 |
| levels-open | 116.469 | 10.835 | 5.350 | 0.000 | 0.522 |
| levels-update | 9.455 | 8.237 | 0.058 | 0.000 | 1.729 |
| levels-commit | 112.321 | 0.079 | 6.265 | 7.395 | 6.251 |
| hsv-update | 59.790 | 4.693 | 0.096 | 0.000 | 1.902 |
| exposure-update | 59.222 | 4.862 | 0.064 | 0.000 | 2.926 |
| gaussian-update | 788.122 | 6.884 | 0.108 | 0.000 | 2.404 |
| noise-update | 158.361 | 7.639 | 0.089 | 0.000 | 4.002 |
| grain-update | 73.912 | 4.688 | 0.053 | 0.000 | 0.610 |
| lens-update | 147.855 | 6.574 | 0.072 | 0.000 | 1.337 |
| invert | 202.987 | 215.103 | 0.000 | 0.000 | 29.478 |
| fill | 244.236 | 266.369 | 0.000 | 0.000 | 23.048 |
| clear | 304.586 | 308.146 | 0.000 | 0.000 | 25.335 |
| undo | 46.418 | 2.849 | 0.000 | 0.000 | 0.604 |
| canvas-size-fill | 68.491 | 0.014 | 4.421 | 56.363 | 7.521 |
| image-size-nearest | 129.682 | 0.010 | 3.355 | 3.298 | 3.455 |
| image-size-resolution | 58.269 | 0.033 | 6.352 | 7.354 | 0.537 |

## Remaining gates

P1.8 remains ACTIVE because several size apply maxima exceed 2 ms: hardware 4K Canvas/Image Nearest,
hardware 45 MP Image Nearest/resolution and all three WARP size cases. The 45 MP Image Nearest action max
is 46.035 ms and WARP Canvas Size action max is 56.363 ms; first uploads and chrome/frame preparation remain
P1.9 costs. The 45 MP synchronous Invert/Fill/Clear dispatch maxima are 899.910/1162.585/1186.003 ms;
P1.5 migration remains necessary. This report closes functional sizing and D13's limit slice, not P1/G1.
Additional command coverage, incremental history accounting/moves, selections counted, lazy image thumbnails,
upload preparation and the full G1 stress matrix remain open. No speed claim against Photoshop is made.
