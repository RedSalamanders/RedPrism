# Lifted-pixel preparation and native recovery resource faults — 2026-10-01

Status: P1.5 A5.1 and P1.7 F6a.2 functional qualification passed. Deferred production moves, clipboard/floating
work, real-loop measurements, remaining P1 and G1 are open.

## Implemented boundary

`RasterEditKind::PixelMove` prepares image selection coverage, source reads, lifted pixels, hole/landing tiles,
source-over samples, carried masks, eager thumbnails, crop metadata and translated selection on a private CPU
stroke. No live session, HWND or GPU engine crosses this boundary. Sparse inputs remain unmaterialized.
Offsets round in document coordinates; nonfinite/unrepresentable mapping is refused before integer conversion.
Cancellation is checked between coverage/read/lift rows, landing tiles, snapshot stages and outline batches.
Borrowed cancellation/progress state is cleared before a prepared preview escapes.

`PixelMoveMemoryBytes` includes the two coexisting lifted planes, landing/hole index growth and moved immutable
outline storage in addition to the complete existing raster-edit graph. Invalid/overflowing planning refuses
admission. Zero offset or no overlap of coverage with the source grid returns Unchanged. Transparent pixels
inside that grid retain the existing direct selection-move behavior.

Native folder rollback now has test-only resource fault stages for recovery timer creation, arming and waiting.
They are consulted only after an actual rollback rename error 5/32. Resource failure retains that initiating
error and the complete backup; publication remains false. Healthy saves and final injected rollback failure
never visit the timer hooks. Three rename attempts and the existing worker event waits remain bounded.

## Independent proof

- 14 `pixelmovework` checks: literal move/duplicate/source-over bytes, zero offsets, off-canvas removal, translated
  outlines, empty/outside selection, invalid mapping/targets, pre/lift-row/move-tile cancellation, sparse reads,
  exact pixel budgets and independent reservation floors.
- 8 `pixel-lift` numeric references: translated and stretched layers, consecutive offsets, translucent duplication,
  alpha edges, clipboard crop origin, placed Gray8 mask edge tone and cancellation/history restoration.
- 40 `rotation-fault` checks, including 7 added controls for healthy timer-hook avoidance, all three resource
  faults and backup-only recovery after the native reader closes. Exact native errors and complete bytes are
  asserted; waits are failure bounds, never timing assertions.
- Existing document/clipboard/gradient/filter/selection goldens, I/O, composition and host suites, WARP application
  self-test, 24-row benchmark smoke, screenshots, ISA controls and crash probes pass. ASan detection and extended
  stress pass.

## Configuration gates and identities

| Gate | Result |
| --- | --- |
| Skills / formatting | Passed: 15 skills / 296 native files |
| x64 Debug / Release | Clean rebuilds and full suites passed |
| x64 ASan Debug | Clean rebuild, full suite, actual detection and extended stress passed |
| ARM64 Debug / Release / ASan Debug | Clean cross-builds passed; native execution unavailable |
| Frozen native/project/script inputs | Same 344 inputs verified across qualification |

Source-freeze SHA-256: `2F2A002D4F6174038D46CAD4FCB831B7A79EDD987233508F235351AB9529B88F`.
Release 0.1.120.0 executable SHA-256: `2EB64E6378E508E8D926411C06553B78A71638D962C3AD6A1928FE20E979BF72`.

Logs: `.build/p1-pixelmove-retry1-driver.log`, `.build/p1-pixelmove-retry1-{x64,ARM64}-{Debug,Release,ASanDebug}.log`
(x64 ASan is instead `.build/p1-pixelmove-asan-clean-rebuild.log` and `.build/p1-pixelmove-asan-clean-test.log`),
skills/format logs and `.build/p1-pixelmove-freeze.json`.

## Failures retained and limits

The initial directory-reader fixture opened a child manifest instead of the directory and produced error 5.
It was corrected to lock the directory; the original exact error-32 assertion then passed. The original targeted
failure is preserved in `.build/p1-pixelmove-target-io.log`.

The first incremental ASan binary reported a stack read outside the caller's raster-edit input at
`PaintForWork`. Caller and callee layouts in that binary were inconsistent. A clean rebuild resolved the failure;
Debug/Release and ARM64 were also rebuilt cleanly from the unchanged frozen sources. The exact incremental
tracking cause is unconfirmed. The failed output remains `.build/p1-pixelmove-x64-ASanDebug.log`.

This qualifies detached preparation, not production pointer/nudge activation or a latency target. Worker
selection construction still has an uncancellable cached-bounds pass; source/coverage reuse and displayed
outline caching remain A5.2a/P1.6. History allocation, owner capture/chrome and first upload remain P1.9/G1.
Codec/OS interruption, intermittent real save causes, flush/close/pending-I/O controls remain F6. No RAW decode
or Photoshop speed qualification follows from this step.
