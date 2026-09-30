# Host input admission — 2026-09-30

## Implemented slice

`HostInputBudget` bounds declared request storage across queued and active inputs to 1 MiB. A path batch is limited
to 256 paths, each at most 32,767 UTF-16 code units on Windows. Footprints charge retained string/vector capacity,
terminators, conservative container overhead and fixed request/result allowance. Checked arithmetic refuses an
oversized request before receipt construction or input transfer. At most 256 paths are inspected; no pixels,
allocation, worker, synchronization, wake or polling are involved in accounting.

`HostRequest::TryCreate` reserves before receipt allocation and moves input only on success. A fault after
reservation returns its ticket and leaves the caller's input unchanged. Tickets follow requests into the queue,
active state and moves. Publication precedes input destruction; charge retirement follows input destruction.
Externally retained receipts own no input/ticket. Admitted inputs now expose const access only. Application's
budget member precedes its queue/active owners and remains alive through their teardown.

The initial numeric panel bridge uses this admission path, returning an immediate `ERROR_NOT_ENOUGH_QUOTA`
failure on refusal and `E_OUTOFMEMORY` at the owning allocation boundary. The existing hidden harness fault flag
now throws at the receipt-allocation boundary after reservation, rather than before budget admission.

## Evidence

The standard-header HostTests add independent boundary/state cases:

- Exact aggregate boundary, `SIZE_MAX` refusal and unchanged input on quota refusal.
- Nearly empty strings/vectors with retained capacity above the limit.
- 256-path acceptance, 257-path refusal and 32,768-unit path refusal.
- Receipt-allocation failure preserves both the original path and an earlier 128-byte reservation.
- Optional write/name inputs, queue-to-active transfer, moved-from destruction, cancellation/stale retirement,
  publication while the charge remains held, and move overwrite with surviving external receipts.

The suite now reports 38 request checks per normal invocation. The actual Application WARP bridge independently
checks full-budget refusal leaves panel/history/queue/wake state unchanged, receipt-allocation failure returns
zero charge, and cleared requests release their charges. Existing gated worker/apply/cancel/stale/shutdown checks
remain in place.

Cheaper agents wrote bounded tests and reviewed the accounting and downloader separately; the primary owned the
budget/factory/host wiring and reviewed/refined the tests. One review found that the earlier OOM control preceded
reservation; the new control closes that gap. Another questioned same-alternative string move overwrite; the
installed v145 STL deallocates the destination before taking incoming storage, preserving capacity accounting.

## Qualification and limits

The final local matrix and offline acquisition evidence are logged under `.build/raw-research/`.
All required configuration gates passed after the source and offline test integration were finalized:

| Gate | Result |
| --- | --- |
| `validate-skills.ps1`, `format.ps1 -Check` | Passed |
| x64 Debug, Release, ASan Debug `test.ps1` | Passed; 38 request checks each normal run, 76 with ASan stress |
| WARP host, legacy numeric goldens, crash/ASan probes, stress and harness captures | Passed in the applicable complete x64 gates |
| Offline RAW corpus gate in each x64 run | 24 controls passed, all 19 actual image hashes and exact license/manifest bytes; no corpus SKIP |
| ARM64 Debug, Release, ASan Debug `build.ps1` | Cross-builds passed; native ARM64 runtime unexecuted |

Final logs are `gate-x64-debug.log`, `gate-x64-release.log`, `gate-x64-asan-debug.log`, and matching
`gate-arm64-*.log`; `.build/raw-research/qualification.json` lists each result. No source changed during this matrix.

This is declared storage accounting with the normal
allocator headroom, not an operating-system page commitment cap or a bound on pre-existing caller buffers.

Production OK, Save/Close/import and other variable-length producers remain inactive under the child plan's
atomic migration rule. They must preflight borrowed event arguments before copying and reserve later selected
paths/staged state. Pixel/worker reservations remain separate. This slice does not claim P1 responsiveness,
RAW decode readiness, SIMD/GPU speed or a Photoshop advantage.
