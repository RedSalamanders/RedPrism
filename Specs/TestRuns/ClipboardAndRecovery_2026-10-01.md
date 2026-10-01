# Clipboard byte preparation and save recovery — 2026-10-01

Status: bounded P1.7 F5a/F6a qualification passed. P1/G1, production clipboard migration and overall F6 remain open.

## Implemented boundary

`ClipboardDibWork` separates pure DIB metadata/decode/encode from Win32 clipboard ownership. It parses explicit
little-endian 24/32-bit headers, masks, palettes and padded/directed rows, checks signed dimensions and independent
limits, decodes into an adopted PixelBuffer, and encodes sparse input rows directly into the final DIBV5 bytes.
It does not borrow a session, window, COM interface or GPU. Memory PNG inspection uses WIC metadata only and
creates no pixel output or thumbnail. Invalid/cancelled output is empty; active rows and WIC calls are not
guaranteed interruptible. The production `SystemClipboard` callers still require F5b–F5d migration.

Exploded save has per-call faults for each rename/cleanup stage, with exact captured error observations and
independent publication truth. Failed publication attempts restore the old project even after cancellation.
When rollback fails, retries preserve the backup-only recovery tree until restoration or successful new
publication. Post-publication backup cleanup failure retains a truthful Ok/Published outcome.

Cleanup uses one 64 KiB path buffer allocated before staging ownership. The deletion path performs no C++ heap
allocation, including during worker OOM unwinding, and uses extended Win32 paths independently of the caller's
long-path manifest. It bounds depth to 128, removes reparse entries without following targets, and leaves real
read-only or depth failures as reported local failures. It touches only the exact owned staging/reserved backup.
The buffer fits the already declared fixed save scratch allowance; no additional reservation is acquired later.

## Independent correctness evidence

- 42 `clipboarddib.*` checks: literal 24-bit padded RGB, 32-bit opaque padding, top/bottom direction, V4/V5 alpha,
  bitfields and palette offsets; bad planes/formats/masks/header/length; signed extrema, exact/lower/hard budgets,
  cancellation with empty output, literal V5 header/channel/row/unpremultiplication bytes and unchanged sparse input.
- 11 `imagecodecwork.clipboard-png-probe.*` checks: literal 2×3 PNG dimensions, exact/lower/zero/negative budgets,
  missing/truncated/wrong-container input, valid oversized IHDR, encoded cap and precancelled empty metadata.
  Over-cap null PNG and the exact/over-cap DIB size predicate avoid fabricating enormous borrowed spans. They
  do not establish a successful 512 MiB DIB allocation or actual system clipboard transfer.
- 24 `rotation-fault.*` checks: byte-identical original/recovery trees and independently reloaded literal pixels;
  every injected rename/cleanup boundary; cancellation after the second-rename failure; exact dual errors;
  repeated failed rollback, successful restoration and successful publication from backup-only state; truthful
  post-publication cleanup failure; unique staging versus an intact unrelated sibling. Real OS cases cover
  read-only failure, nested files, a mount-point junction whose target survives, and depth refusal beyond the
  ordinary Win32 path length. The junction fixture requires no symbolic-link privilege.
- All these controls pass in x64 Debug, Release and ASan Debug. The complete existing host, document, I/O,
  composition, WARP, crash, ISA, UI smoke and harness screenshot gates also pass. Native ARM64 execution is unavailable.

## Qualification corrections and open investigation

The initial agent drafts had two compile mistakes: a removed encoded-cap constant was still referenced and a
test used a nonexistent UTF conversion helper. Both were corrected before qualification; the original compile
log remains `.build/p1-clipboard-recovery-initial-compile-failure.log`.

The first new depth control exposed ordinary-path dependence and a fixture whose deep setup/teardown was not
using extended paths. Both the cleanup and the fixture now use extended paths. The original seven failures
(including dependent recovery assertions) remain `.build/p1-clipboard-recovery-longpath-fixture-failure.log`.

A subsequent targeted run failed one combined rollback observation. Separate tree, reload, backup and staging
assertions with exact failure diagnostics now pass in the targeted run and all three frozen configuration suites.
Its precise cause is not established; preserve `.build/p1-clipboard-recovery-rollback-observation-failure.log`.
The child plan's intermittent real rotation/cleanup investigation remains open. Passing injected boundaries
does not explain the prior intermittent save observations or qualify flush/close/device faults.

## Configuration gates and source identities

| Gate | Result |
| --- | --- |
| Skills / formatting | Passed: 15 skills / 276 native files |
| x64 Debug / Release full tests | Passed: 42 DIB + 11 PNG metadata + 24 rotation checks in each |
| x64 ASan Debug full tests, extended checks and actual sanitizer detection probe | Passed: all 77 new checks |
| ARM64 Debug / Release / ASan Debug | Cross-builds passed; no native runtime claim |
| Frozen inputs through all six configurations | Passed: 324 code/project/script inputs |

Driver: `.build/qualify-clipboard-recovery.ps1`; logs: `.build/p1-clipboard-recovery-driver.log`,
`.build/p1-clipboard-recovery-{x64,ARM64}-*.log`, skills/format logs and source freeze JSON.
SHA-256 identities:

- Freeze manifest: `F3D0BF1BEB1D7D3EEEB82D00A39D2B8FDC989F5C6273DED767DEE0D50D7C7878`.
- x64 Release RedPrism 0.1.117.0: `FCD6AF20777AF880697ADF5A9897AA90D5D15B399FC6C563256EBB5983E08D09`.

## Limits

The full suites include the existing 24-row UI smoke; there are no new performance or cancellation-latency
claims from these helper/fault controls. F5b–F5d still own admitted clipboard transfer, immediate publication,
bounded event retries, ordered Copy/Cut/Paste and guarded New Canvas prefill. F6 still owns shared pending IO,
slow-source/cancellation/shutdown measurements and the intermittent save cause. Basic image edits still block
in production and are P1.5 work; history/chrome/upload apply overruns remain P1.9/G1. RAW and Photoshop
qualification remain later phases.
