# Ordered color commit and busy controls — 2026-09-30

Status: P1.1c.3a–b preparation qualified; production activation remains P1.1c.3c–d

## Change

The host has an immutable Commit worker with explicit pending/succeeded/cancelled/failed/stale outcomes and a
shared history ordinal/barrier. Snapshot follows admission; completion validates, applies or drops, records
history, then releases the barrier. Cancel retains ownership through acknowledgement, including pixels already
computed before drain. Failure preserves the current panel and reports outside completion. Opening histograms
remain independent; preview publication closes at commit admission. The engine freezes panel callbacks and
uses the busy-control contract, whose drawn editors use theme colors and the pinned host's capture rules.

This is preparation for atomic host activation. **Normal OK, Save/Close/import and other dependent callers still
use their existing paths.** P1.1c.3c–d owns their continuation migration, full caller coverage and activation.

## Evidence

The real worker gate proves no pixel/history mutation before apply, frozen settings/Enter, available Zoom In,
rejection of a competing ordinal and one history entry before barrier release. Gamma 2 over opaque gray
`0,64,128,255` is checked against independently rounded square roots `0,128,181,255`. Further cases cover a
successful result already computed when Cancel/reopen occurs, stale and current identity results, a runner
`E_OUTOFMEMORY`, a `Job::Run` throwing `bad_alloc` through the actual scheduler catch, and actual donorless fill.
The stale identity case explicitly observes the worker's `Unchanged` status. They verify outcome, original pixels,
retained panel and history.

A replacement test deliberately reuses persisted document/layer IDs after the old worker computes its pixels;
its cancelled completion leaves the replacement image and fresh history intact. Adjustment-layer settlement
records one metadata edit without replacing base pixels. A missing adjustment target returns Stale with its
panel retained and no engine history entry.

The busy-control test uses the attached ChromeHost and actual pointer dispatch for Curves, Levels and Spectrum,
with input before/after busy transition and preparation, then fresh input after restoration. It checks Cancel/OK,
numeric focus-loss behavior, completed decimal preservation, late callback suppression and the idle histogram,
Colorize and curve-removal prerequisites. The Levels restoration test presses the moved handle, rather than its
old position; disabling a focused stepper retains the existing rule that focus loss formats its parsed value.
An agent supplied the
draft; the primary reviewed and integrated only its bounded changes, then strengthened these checks.

Event waits have ten-second failure bounds. No elapsed-time assertion establishes performance.

## Final validation

| Gate | Result | Evidence under `.build/logs/` |
| --- | --- | --- |
| Skill validation and formatting | Passed: 15 skills, 201 formatted source files | Final matrix output |
| x64 Debug full tests | All tests passed | `color-commit-x64-Debug-tests.log` |
| x64 Release full tests | All tests passed | `color-commit-x64-Release-tests.log` |
| x64 ASan Debug full tests | All tests passed, including instrumented sanitizer gates | `color-commit-x64-ASan-Debug-tests.log` |
| ARM64 Debug cross-build | Passed, pinned DxUi provenance verified | `color-commit-arm64-Debug-build.log` |
| ARM64 Release cross-build | Passed, pinned DxUi provenance verified | `color-commit-arm64-Release-build.log` |
| ARM64 ASan Debug cross-build | Passed, pinned DxUi provenance verified | `color-commit-arm64-ASan-Debug-build.log` |

No source changed during the final matrix. No benchmark ran concurrently with these builds. ARM64 native
execution and production continuation activation are separate, outstanding gates.

## Limits

Production activation, dependent writes/replacements, queue overflow, session-end generations and all caller
guards remain open. Existing GPU sampling, history/IO and other P1 paths can block. ARM64 cross-builds do not
qualify native execution; whole-host G1 and the Photoshop comparison remain unproven.
