# Immutable color-edit work — 2026-09-28

Status: P1.1a complete on x64; ARM64 cross-builds passed

## Scope

P1.1a separates destructive panel computation from validated session mutation. The direct host still executes
the graph inline. This is a prerequisite for asynchronous dispatch, not a G1 or Photoshop performance result.

`Common/ColorEditWork.*` owns immutable preparation/results, full-source computation or reuse of an exact current
full-grid preview, blur trimming, carried masks and both thumbnails. Apply validates opening inputs and the panel
sequence before any mutation, rechecks current pixel budgets and records one successful edit. The settings used
for future panels change only after success. Worker failures, cancellation and missing fill donors remain distinct.

The host treats an open panel as unsaved work. Save and Export refuse to write when attempted application leaves
the panel open, so save-before-close cannot silently discard the edit. Mask thumbnails remain eager because their
edge samples determine the mask's behavior beyond its pixels.

## Independent checks

- Legacy numeric oracle: retained numeric golden cases,
  `inputClippingGammaOutputInversionAndAlpha`: clipping 64/128 gives 0/255, gamma 2 gives 128/181 within the legacy
  one-byte tolerance, and alpha stays 255. These cases explicitly disable preview reuse to exercise computation.
- Separate preview-on reuse and preview-off recomputation, one named entry and exact undo.
- Opening target changed before or after preparation: image placement, mask, selection and canvas size.
- New settings, Cancel/reopen, another session with the same document/layer identities, replacement and restore
  epochs reject stale work. Compatible layer naming and print-resolution changes survive valid apply.
- Failed/cancelled work preserves document, mask, undo count and remembered settings; Content-Aware Fill without
  donors is separate from invalid input or a processing failure.
- Blur growth carries a mask and its edge tone atomically. Another layer consuming the budget and a refused
  larger blur grid cannot install a clipped or over-budget result.
- WARP host checks cover Cancel at the save prompt on a clean document's changed Levels panel and a failed fill
  that creates no save file; the successful adjustment-panel save/reload test remains required.

The cheaper test and review agents were checked against the implementation by the primary. The primary added
the refused-grid guard, distinguished processing failure from missing donors, and implemented save refusal.

## Validation and remaining work

Full x64 Debug, Release and ASan Debug gates passed, including all 63 new `coloreditwork.*` checks, existing
adjustment/filter/HSV numeric goldens, both WARP save-path checks, CPU/real-loop smoke, instruction scans,
crash probes and harness captures. ASan includes its actual detection probe. ARM64 Debug, Release and ASan Debug
cross-builds passed. Native ARM64 execution is unavailable on this host.
Formatting, skill validation and the staged whitespace check passed.

Logs: `.build/logs/color-edit-work-x64-{Debug,Release,ASan-Debug}-final-tests.log` and
`.build/logs/color-edit-work-arm64-{Debug,Release,ASan-Debug}-build.log`. The earlier Debug run also passed;
the final matrix includes the preview correction that reserves the no-source message for actual missing donors.

Cancellation currently checks between graph stages. P1.1b must thread cancellation through kernels, coverage,
copies and trimming, and declare the complete additional memory peak. P1.1c must wire preview/histogram/commit
jobs together with every settle/discard/save continuation. Existing history allocation and trimming remain in
the session apply path until their owning migration; this record claims neither bounded UI time nor recovery from
history allocation failure.
