# GPU adjustment previews qualification

Date: 2026-09-30. Scope: P1.2 following production continuation checkpoint `3fab9e3`.

## Implemented behavior

Workers produce immutable lookup/source/selection inputs for all six adjustment kinds. The UI records a layer-grid
pass before normal layer composition, retains source alpha, mixes selection coverage and keys destructive grain
in layer pixels. LUT and cube textures are reused. Live adjustment-layer ticks also use worker lookup preparation.
CPU inline goldens and destructive CPU commit arithmetic are unchanged; GPU lookup output cannot masquerade as a
reusable CPU commit raster.

The host preserves one running/latest pending request, completed intermediate previews, preview off, identity,
Cancel/reopen and stale target checks. The transient target follows ordinary transform, mask, opacity, blend,
clipping/folder passes. Committed readback bypasses it. Hidden/device reconstruction retains owning CPU inputs,
releases all graphics surfaces and rebuilds on the current device; settled frames record no new composition.

## Validation

| Gate | Result |
| --- | --- |
| Skills / formatting | All 15 repository skills validate; format check passed |
| x64 Debug complete test.ps1 | Passed |
| x64 Release complete test.ps1 | Passed |
| x64 ASan Debug complete test.ps1 | Passed, including stress and the deliberate AddressSanitizer diagnostic |
| ARM64 Debug / Release / ASan Debug | All cross-builds passed; native runtime remains unqualified |
| Release hardware real-loop benchmark | All 15 operations completed, 10 measured trials after warmup |

There are 25 new lookup/state assertions and 119 composition assertions per normal run. CPU state tests prove
coverage reuse, independent histogram arrival, cancellation/invalidation and non-reuse at commit with literal
Levels pixels. WARP tests cover every kind, exact alpha including transparency, translated/scaled placement,
selection, mask/opacity/clipping/folder dependencies, unrelated layers, readback bypass, texture reuse, hidden
release and a replacement device. Grain at preview scale 0.5 matches CPU RunFilter at 0.5 and differs from scale 1.
Production event gates prove latest live lookup/GPU pixels and immediate preview off while its worker is held.

The first full Debug run caught fixture leakage: the new live-adjustment test left that layer active for subsequent
image-panel tests. Restoring the fixture's image target fixed it; the complete six-configuration matrix was then
rerun successfully. No legacy golden was changed. Each x64 gate verified all 19 local RAW files with exact
CC0/manifest evidence, without downloads or qualification skips. This is provenance validation, not RAW decoding.

All 270 tracked build/test inputs in `.build/p1-gpu-source-hashes.json` remained unchanged through qualification.
Ignored logs: `.build/p1-gpu-x64-*.log`, `.build/p1-gpu-ARM64-*.log`, `.build/p1-gpu-selftest.*.log`,
`.build/p1-gpu-skills.log`, `.build/p1-gpu-format-check.log` and `.build/p1-gpu-ui-release*.log`.

## Release 4K measurements

Ryzen 9 9950X3D, 16 cores/32 logical processors, 64 GiB RAM, RTX 5080 driver 32.0.16.1714, Windows build 26200,
Balanced power, scalar kernels and 32 workers. Fixture checksum `04c311cabfc8b925`. Timings cover the real command,
worker/completion and invalidated Present submission; they do not measure scanout or an isolated GPU kernel.

The before column is the independently recorded same-machine checkpoint in
[HostContinuationActivation_2026-09-30.md](HostContinuationActivation_2026-09-30.md); this was not an alternating paired run.

| Operation | Before median | After median | After max UI dispatch | After max apply |
| --- | ---: | ---: | ---: | ---: |
| Levels open / histogram | 60.511 ms | 65.334 ms | 3.929 ms | 4.109 ms |
| Levels update | 78.856 ms | 5.718 ms | 1.834 ms | 0.034 ms |
| Hue/Saturation update | 171.437 ms | 8.398 ms | 2.046 ms | 0.054 ms |
| Exposure update | 78.864 ms | 8.327 ms | 2.005 ms | 0.027 ms |
| Grain update | 133.355 ms | 8.240 ms | 2.272 ms | 0.040 ms |
| Levels commit | 8.661 ms | 83.861 ms | 0.044 ms | 4.425 ms |

The commit regression is intentional at this boundary: the previous full-grid CPU preview already held the exact
commit raster. GPU preview owns no such raster, so OK computes the CPU graph on a worker. This avoids introducing
approximate committed bytes or synchronous GPU readback into P1.2. Exact SIMD/threaded CPU commit optimization and
qualified GPU commit policy remain P2/P3. The faster preview removes full CPU filtering and output uploads per tick.

G1 remains open. Levels commit's max UI action was 5.938 ms; history/apply work still needs P1.9. Invert, Fill and
Clear still reached 170.548 / 237.863 / 260.545 ms UI dispatch respectively and need their worker migration.
Opening histograms, IO, readback, tool/size work and first uploads retain their owning remaining P1 steps.
There is no native ARM64, unavailable-machine, RAW decode or Photoshop comparison claim in this record.
