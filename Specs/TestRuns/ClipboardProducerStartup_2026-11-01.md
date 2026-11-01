# Delayed clipboard producer startup control

This record preserves a deterministic fixture startup observation on native x64 Debug. It establishes no
latency target or completed native qualification.

- Hosted source `1d4ec79d3478fc5565d2dfa704d1b66b9fa16318`, run
  [41347772813](https://github.com/RedSalamanders/RedPrism/actions/runs/41347772813), x64 regression job
  `124971286955`, failed exactly `clipboard-delayed.paste.unrelated-read-before-worker-cannot-enter-event-gate`.
  The other 44 delayed-clipboard checks passed, including actual held rendering, literal foreign pixels/history,
  cancellation, replacement, shutdown and reservation retirement. That attempt had no immediate early-read
  diagnostics, so its cause remains unproven. All earlier repaired Polygonal, Wand, sizing and Clone controls passed.
- The existing fixture signalled readiness before entering its producer message loop. The new control holds
  that loop behind an owned event after preparation. Readiness must remain unsignalled while dispatch is held;
  after release, exactly one actual startup message must dispatch before the UI's original native early read.
- With the old readiness signal retained, run `20261101T215822997Z-98892-87e2c5fe3a6d410ba191290c19c28856`
  exits 5. Both new startup assertions fail; the old signal lets the next ready check race dispatch.
  The first scenario's unchanged native early-read assertion and subsequent original paste checks pass.
  A later scenario fails its combined installation/dispatch guard; individual guard fields were not printed.
  The canonical outcome is `failed-or-interrupted`.
  The build has zero warnings/errors and passes the existing linked-image ISA controls.
- The exact formatted negative patch against source `1d4ec79d3478fc5565d2dfa704d1b66b9fa16318` has SHA-256
  `f2d71219159342af6f0cab05cfdcf4430aa08f3eb78cc5e4fbab2baa6a8eb218`.
- The correction signals successful readiness from the owned producer window's startup message handler.
  Native initialization failures still wake the caller and fail its installation/dispatch checks. Cleanup releases
  both owned waits and joins the original thread. Production clipboard transfer and retry policy are unchanged.
  All original 45 assertions remain required, alongside the two new startup controls and immediate native diagnostics.

The controlled startup defect does not identify the unrecorded cause of the hosted early-read failure. Following
positive validation and hosted matrix attempts have their own exact-source receipts. Raw failures, negative source
patches and canonical payloads remain preserved; persistent archives use `archive-qualification.ps1`.
