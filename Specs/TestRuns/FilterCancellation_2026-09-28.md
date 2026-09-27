# Cancellable filter work — 2026-09-28

Status: P1.1b complete on x64; all ARM64 configurations cross-built

## Scope

The complete destructive-filter computation graph now propagates cancellation through private sparse reads,
scalar adjustments, histograms, Gaussian/motion blur, fill, selection coverage, blending, trim, carried masks and
thumbnails. Output is published only after successful completion. Large scratch initializes in bounded chunks.
The scalar operation order, seeds, coordinate rules and numeric goldens are preserved. Far-off-canvas coverage
also avoids integer conversion overflow and an unbounded walk through invisible columns.

The commit graph declares its additional allocation peak before dispatch: live coverage, rounded image buffers,
private source copies, kernel scratch, blend/crop outputs, eager mask thumbnails and metadata. Existing immutable
inputs and an exact reusable preview are not charged again. Selection producers cache point counts so the estimate
does not traverse the outline. Host dispatch and file/close continuations remain P1.1c work; this slice alone does
not establish G1 or a Photoshop comparison.

## Tests and review

Independent literal expectations cover sparse fill/base/tile windows, row padding, nested/deep graphs, extreme
coordinates, area coverage and nearest mask/thumbnail sampling. Every filter has a pre-cancel case; fill-without-
donors remains distinct. Six dispatch entries cover cancelled input and odd-width padded rows. Memory tests cover
Gaussian scratch, persistent coverage, fill arrays, sparse copies and exact-preview reuse.
The final Debug, Release and ASan runs each pass 71 `coloreditwork.*`, 22 `filtercancel.*` and 13 `rasterwork.*`
checks alongside the existing document, host, I/O and WARP suites.

The primary checked helper contributions and corrected invalid assumptions about contiguous raster materialization,
a nested tile's location and transform composition order. The primary also removed a settings-copy `noexcept`
boundary that could prevent allocation failure from reaching the job handler, and replaced repeated per-pixel
division in carried masks with a reusable column map and row sampling. A subsequent review found a secant vector
allocated inside the `noexcept` curve lookup. It now computes only the adjacent secants, retaining the same
arithmetic. Independent cubic-Hermite midpoint tests cover harmonic and opposing-slope cases, alongside the legacy
curve alpha/identity goldens. Final review also found finite extreme endpoints whose differences overflow before
clipping. Both coverage paths now refuse the whole result on non-finite edge/intersection arithmetic, including
transformed overflow; independent tests cover both axes and keep the legitimate far-off-canvas expectations.
The final configuration gates include these corrections.

## Release cancellation measurement

R1: Ryzen 9 9950X3D, Windows 11 build 26200, 61.6 GiB RAM, Balanced power plan, scalar CPU dispatch. No build or
other benchmark ran concurrently. `HostTests.exe --bench-filter-cancel` uses one scheduler job at a time on
8192×5464 pixels, one warmup and ten observations. It requests cancellation after a 20 ms event wait following
worker entry. Values below include scratch destruction and OS scheduling; they do not directly measure the gap
between adjacent cancellation checks. All cases reported `status=ok`.

| Case | Cancelled samples | Completed early / overlap | Median (ms) | p95 / maximum (ms) |
| --- | ---: | ---: | ---: | ---: |
| Levels | 10 | 0 / 0 | 5.902 | 7.289 |
| Hue/saturation | 10 | 0 / 0 | 5.719 | 6.709 |
| Curves | 10 | 0 / 0 | 5.947 | 6.034 |
| Exposure | 10 | 0 / 0 | 5.619 | 6.566 |
| Gradient map | 10 | 0 / 0 | 5.495 | 5.897 |
| Grain | 10 | 0 / 0 | 5.755 | 6.642 |
| Gaussian, radius 250 | 10 | 0 / 0 | 0.063 | 0.084 |
| Motion, distance 2000 | 10 | 0 / 0 | 0.041 | 0.057 |
| Noise | 10 | 0 / 0 | 5.730 | 6.124 |
| Content-aware fill | 10 | 0 / 0 | 7.074 | 8.145 |
| Lens | 10 | 0 / 0 | 6.020 | 6.531 |
| Histogram | 10 | 0 / 0 | 0.006 | 0.008 |
| Antialiased coverage | 10 | 0 / 0 | 7.809 | 9.659 |
| Hard coverage | 0 | 10 / 0 | — | — |
| Downscale | 10 | 0 / 0 | 5.494 | 5.987 |
| Carried mask | 3 | 6 / 1 | 1.355 | 1.434 |
| Private sparse copy | 10 | 0 / 0 | 5.706 | 6.048 |

The maximum observed cancelled return was 9.659 ms. Most full-image copies pay the worker's buffer release cost;
the histogram has no such output. Hard coverage finished before every cancellation, and three carried-mask samples
are insufficient for a strong tail estimate. These observations cover these inputs and cancellation timing only.

## Full-output parity and timing

`RedPrism.exe --bench kernels` completed all 13 3840×2160 rows, ten samples after a warmup. All 13 checksums match
the original `.build/logs/kernel-baseline-kernels.txt`, including the full Gaussian result and the double histogram.
Selected current medians: Levels scalar 81.027 ms, HSV scalar 154.195 ms, histogram 46.540 ms, complete Levels
output 85.414 ms, complete HSV output 161.586 ms, Gaussian sigma 10 4653.889 ms. Baseline Gaussian was 4490.961 ms;
this separate-run difference is recorded without inferring a precise cancellation overhead or speedup. P2 owns
the measured kernel acceleration. No UI, GPU, RAW or Photoshop result is inferred from these CPU measurements.

## Provenance and gates

- Cancellation log: `C:\RedPrism.Perf\filter-cancel-2026-09-28.log`, SHA-256
  `0AF0450A34F77AAF8166A26CDE7816DB0C5681DD0AE6FC7530BA68AB00DD5BBD`.
- Kernel log: `C:\RedPrism.Perf\filter-kernels-2026-09-28.log`, SHA-256
  `ADAC057F2F678C3C9093F44384F9FB82FAE39EE2A1B19529875046106F02F6E8`.
- Measured HostTests executable SHA-256:
  `6D2DDDB34219E9F893EBCB564B14CD1484F2E5D8DAB62638E110E52DECA81F15`.
- Measured RedPrism executable SHA-256:
  `E4B3F8561B7422C3304E0C60C83CB6CF47B7ECDEF7410800C5A3215019639653`.

The measured build preceded a benchmark-only empty-before-cancel failure guard, extra extreme-coordinate tests,
the curve lookup allocation fix and overflow rejection. These do not affect the 13 measured kernel rows, which
contain neither curves nor selection coverage; their numeric checks are separate. Both the initial and final
complete x64 Debug, Release and ASan gates and all three ARM64 cross-builds passed. The final configuration rerun
includes the review corrections. Skill validation, formatting and whitespace checks passed.
Native ARM64 execution is unavailable. Final configuration logs are
`.build/logs/filter-cancellation-x64-{Debug,Release,ASan-Debug}-final-tests.log` and
`.build/logs/filter-cancellation-arm64-{Debug,Release,ASan-Debug}-final-build.log`.
