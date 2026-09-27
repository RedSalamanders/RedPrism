# Job input, previews and busy chrome — 2026-09-27

Status: P0.6b complete on x64; all ARM64 configurations cross-compiled. Native ARM64 execution remains unqualified.

## Scope and review

The slice adds UI-owned preview coalescing, an eight-command continuation ring, a cross-class history barrier,
stroke replay states, the pure end-session policy, fixed scheduler activity records and a 250 ms busy indicator.
Existing image commands remain synchronous until P1. The state machines establish ordering and retained ownership;
they do not yet record/replay production stroke samples or implement asynchronous file end-session waits.

Smaller agents authored bounded state tests, the DxUi progress control and resource strings. The primary authored
the state machines, scheduler observer and host timer, reviewed all changes and owns the configuration gates.
A focused independent review of mailbox epochs, queue ownership and history/stroke state transitions found no
additional actionable issues after integration. Its scope did not include P1's future host migrations.

Issues corrected during review/integration:

- The preview plan mixed newest pending and newest launched sequences. Interactive previews now explicitly finish
  and publish before the newest pending request launches; refinements are invalidated immediately by new settings.
- Command/stroke order crosses scheduler classes. `HistoryWork` holds busy through apply/drop, and one input ordinal
  stream prevents wand coalescing across intervening strokes. Snapshots are taken only after acquiring the barrier.
- A failed stroke retains an unresolved replay state for explicit retry. No automatic retry or completed-stroke
  Escape cancellation is introduced. Memory accounting and production input storage remain P0.7/P1 responsibilities.
- A delegated document fixture initially called a nonexistent mutable-raster API; it now assigns an immutable
  fixture through `EditForTest`. The primary strengthened tests to check untouched filtered pixels, commands still
  busy after dequeue, and Undo enqueued before the stroke applies.
- The pinned DxUi cancels ordinary held buttons when sibling visibility changes the interaction revision. Busy
  visibility changes now retain the latest request until release. Both directions have a WARP regression check.
- Hidden progress changes skip setters entirely so quick/background jobs do not repaint the retained chrome surface.
- An observer test's shutdown gate needed a valid second handle. Resource-catalog size and unsigned optional
  comparisons were corrected under the warnings-as-errors build.

## Evidence required

| Gate | Result |
| --- | --- |
| Complete x64 Debug tests | Passed: 269 new policy/ordering checks, all suites, WARP, crash probes, kernel smoke and nine screenshots |
| Complete x64 Release tests | Passed: all suites, WARP, crash probes, kernel smoke and nine screenshots |
| Complete x64 ASan Debug tests and stress | Passed: all suites, extended raster/worker/job stress, actual sanitizer detection, WARP, crash probes and nine screenshots |
| Release native concurrency analysis | Passed, including positive SAL and deliberate negative C261xx controls |
| ARM64 Debug / Release / ASan Debug cross-builds | Passed; compilation only |
| Skill validation, formatting and diff checks | Passed: 15 skills; 149 C++ files checked, including unregistered P0.7 preparation files |

The HostTests checks use literal state outcomes and virtual QPC values, not elapsed-time thresholds. The actual
scheduler observer test uses events to order submit, progress, apply, successor submission and retirement; it also
proves shutdown drops notices. DocumentTests combines these policies with existing direct session operations and
numeric pixel/history expectations. The WARP self-test drives the production observer, checks timer retirement,
prepares the bar at 96/144 DPI and holds rail presses through visibility changes and intervening preparations.

Logs are `.build/logs/job-input-*`; generated products stay under `.build/`. Native ARM64 execution, Application
Verifier, G1 image latency, and RAW/Photoshop comparisons remain unqualified by this slice. No speedup is claimed.
