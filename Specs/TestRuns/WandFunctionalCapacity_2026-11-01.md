# Wand functional capacity controls

The observations below establish deterministic admission and fixture-state facts on native x64 Debug. They
assert no physical-memory capacity, latency target or completed hosted qualification.

- Hosted source `1d4ec79d3478fc5565d2dfa704d1b66b9fa16318`, run
  [41347772813](https://github.com/RedSalamanders/RedPrism/actions/runs/41347772813), ARM64 regression job
  `124971286923`, passed active-layer literal capture, publication and final output refund. Its next Boolean
  New/Add/Subtract mode failed 15-second settlement and subsequent FIFO, literal/history and ownership assertions.
  Actual document Close and final acknowledgement refunded the original charge. That attempt did not log
  individual receipts or the admission snapshot; its cause remains unproven.
- Ordinary memory admission waits while an outstanding reservation can return capacity. A functional Boolean
  case therefore needs an adequate declared envelope throughout the sequence, including while the earlier
  successful selection output remains charged. Production policy and its scheduler tests remain unchanged.
- Controlled run `20261101T224331007Z-93940-691393be241349c99922d970a7318e0b` uses the actual isolated
  application owner. It starts with 16 GiB, then supplies a 2 GiB logical snapshot only after New publishes its
  first Magic Wand history entry while younger requests remain queued. No sleep or elapsed-time condition
  chooses that transition. The exact controlled patch against source `1d9b439adcdddf66a63861ef0fbe56adcc3b4c07`
  has SHA-256 `b10c37438710be9e53ccfca2267b9985ff0a3651a80fee1994f06476a8b0b255`.
- The control reproduces the same assertion pattern and exits 5. At the unchanged settlement boundary, New is
  `Succeeded/0`, Add and Subtract are `Pending/0`, one request remains in the ring, the active ticket/stamp/history
  barrier remain owned, and exactly one published output holds `315768` bytes with zero releasing bytes.
  Actual document Close later joins and refunds that output. The canonical outcome is `failed-or-interrupted`.
  Build diagnostics are zero warnings/errors and existing linked-image ISA controls pass. This reproduction
  does not identify the unrecorded snapshot in the older ARM64 attempt.
- All twelve new low-capacity refusal assertions pass in that same controlled run. With no other reservation,
  the real layer Add requires `4749142384` bytes and composite Add requires `4896686396`, against a 2 GiB
  snapshot. Both return the original `E_OUTOFMEMORY`, perform zero captures/readback preparations, preserve
  literal source/selection/history and join without a new reservation.
- The correction reuses the existing isolated two-worker owner and its stable 16 GiB snapshot for the original
  layer and composite suites. Their 144/199 original checks plus summary records, complete source/readback routes,
  FIFO, literal vertices, history, output lifetime and 15-second failure bounds remain unchanged. The temporary
  pressure injection belongs only to the retained negative patch. Failure diagnostics remain in the normal suites.

Following positive local validation and hosted matrix attempts have independent exact-source receipts. Preserve
every failed source and canonical payload; persistent storage uses `archive-qualification.ps1`.
