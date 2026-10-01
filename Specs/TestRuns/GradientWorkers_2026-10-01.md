# Detached gradient previews and commits — 2026-10-01

Status: P1.5 A4.1–A4.5 functional qualification passed. A4.6 measurement, remaining P1 and G1 are open.

## Implemented boundary

Production gradient pointer/header/palette callbacks now change deferred metadata only. An InteractivePreview
worker captures the source/selection/token under complete admission and privately prepares the exact CPU stroke.
One active/latest pending request per opening coalesces pointer changes. Publication never shares mutable stroke
state with a worker. Aggregate retirement includes private tiles, coverage and retained source owners.

Gradient settlement follows color settlement under the selected host ordinal, before dependent mutation, size,
write or readback. Apply/Enter owns its opening identity. Commit captures endpoint colors/settings and prepares
the complete target layer/mask on a worker; owner application validates generation, target/selection, history
acquisition and settings sequence. Cancellation retains ticket/barrier/reservation until acknowledgement and
stops dependent commands. Mask thumbnails remain eager. Direct session APIs retain their inline numeric path.

An acknowledged preview can reuse its GPU texture only with matching settings sequence, published generation,
retained preview source and layer target. Existing adoption/count goldens still pass. A newer source or settings
requires ordinary upload. The next preview is armed after the old job record and reservation retire.

## Independent correctness evidence

- 36 `gradientwork.*` checks cover literal linear/radial pixels, legacy tolerances, source-over opacity, masks,
  sparse sources, checked geometry, cancellation and private input ownership.
- 48 `gradientstate.*` checks cover deferred metadata, settings/opening clocks, no-line redrag invalidation,
  exact publication/apply guards, direct-call refusal before mutation, history and source preservation.
- 21 `gradientresource.*` checks cover independent complete reservation floors, aggregate release ownership,
  source sharing and clearing borrowed progress/cancellation state before publication.
- 33 `gradient-host.*` scenarios exercise the production preview/commit producers, coalescing with literal
  intermediate/final pixels, image/mask commits, palette refresh, pending Undo, admission/capture/worker OOM,
  returned errors, early/late cancel, stale metadata/selection/target/generation/restore/replacement/opening and
  a cancelled gradient prerequisite preventing dependent Invert. Held assertions require responsive view input,
  retained admission/history ownership and no hidden Present; terminal assertions require zero tickets/charges.
- Existing gradient, clipboard, layer switch, GPU adoption, document, I/O, composition and host goldens pass.
  WARP self-test, 24-row UI smoke, ISA gates, crash probes, screenshots and real ASan detection pass.

Deadlines are failure bounds, not performance thresholds. Host observation helpers now await asynchronous
acknowledgement before reading pixels/history; their existing expected numeric values were not changed.
Draft fixtures incorrectly assumed contiguous output and immediate preview visibility; those assumptions were
corrected before qualification. A metadata change may publish the active older preview under the existing
coalescing rule, then the latest result; it is not a preview-refusal case. Commit still requires exact metadata.

## Configuration gates and identities

| Gate | Result |
| --- | --- |
| Skills / format | Passed: 15 skills / 292 native files |
| x64 Debug / Release | Full suites passed |
| x64 ASan Debug | Full suite, extended stress and actual detection probe passed |
| ARM64 Debug / Release / ASan Debug | Cross-builds passed; native execution unavailable |
| Frozen native/project/script inputs | Passed: 340 files across all six configurations |

Driver/logs: `.build/qualify-gradient.ps1`, `.build/p1-gradient-driver.log`,
`.build/p1-gradient-{x64,ARM64}-{Debug,Release,ASanDebug}.log`, and skills/format logs.
The targeted complete WARP self-test output `.build/p1-gradient-target2.out.log` records all 33 host scenarios;
the matrix's GUI self-test stdout is not redirected into its individual test logs.

- Source-freeze manifest SHA-256: `B3BA9C2284EB65E51432BF426D9277764AB2054891032ED358FA89DE9DEB4FA7`.
- x64 Release 0.1.119.0 executable SHA-256: `92A205EB28CB503CE3EEE861952314524449438DAF05DBE08079DE79AE783FD2`.

## Remaining qualification

This uses the exact existing scalar CPU gradient computation. SIMD/GPU preview acceleration remains P2/P3.
History Begin/End still allocate; allocation-safe publication, bounded capture/chrome and first upload remain
P1.9. The suite establishes ordering/ownership/pixels, not one-frame drag, 2 ms apply, G1 or Photoshop speed.
Add real-loop gradient drag/coalescing/settlement/cancel rows and measure them on an active benchmark desktop.
The preceding basic-raster benchmark refused display-off presentation; its preserved logs/binary are separate
evidence and cannot supply gradient timing. A5–A8 and all remaining P1 caller migrations continue.
