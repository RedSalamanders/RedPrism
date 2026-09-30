# Borrowed host inputs and selected output paths — 2026-09-30

## Implemented slice

`HostRequest` now has borrowed-input builders for ordered path batches, write parameters and rename inputs.
Preflight accepts non-owning views, checks count/length/aggregate copy charge without allocation, and reserves
before constructing owned containers, strings or the shared result. A terminated Win32 path first uses a bounded
scan. The v145 direct-copy capacity allowance is reconciled to actual retained capacities before publication.
Refusal copies nothing; partial-input and receipt-allocation faults release temporary storage before their ticket.

Later selected output paths are owned by a still-pending request under the same combined 1 MiB input budget.
Replacement holds both old/new charges until successful ownership transfer. Refusal or allocation failure leaves
the previous path unchanged. Queue/active moves and retirement preserve publication before owned storage release
and reservation return. Selected output adds no receipt allocation, worker, event, timer or polling.

The installed v145 STL was checked for direct string construction rounding, exact vector reserve and string
move-assignment storage release. A compile-time check keeps fixed request state within its existing allowance.
This is declared storage accounting with ordinary allocator headroom, not an OS page-commit ceiling.

## Tests and review

Tests retain literal Unicode paths/names and receipts independently of caller/queue ownership. Cases cover:

- Original path order/action/drop point and copied write/name parameters after caller edits.
- Null/empty/Unicode terminated-pointer scans, 32,767-unit acceptance and 32,768-unit refusal.
- 256-path acceptance, 257-path refusal, aggregate/quota refusal before copy and optional absent/empty values.
- Faults after a partially copied batch/string and before receipt allocation, preserving earlier reservations.
- A name longer than the path-specific limit: retention remains bounded by bytes; document rename validity is
  still the existing trim/UTF-8 contract at apply.
- Same-budget/pending-owner restrictions for selected output, oversized-path refusal before copy, caller-value
  independence, old/new replacement peak, allocation-failure preservation and successful replacement charging.
- Queue-to-active and move-overwrite ownership, publication with retained charge, destruction returning zero,
  and independently retained terminal receipts.

A cheaper agent audited future producer copy sites and wrote the bounded test extension. The primary owned the
implementation, corrected replacement/name boundary assertions, and added successful replacement and oversized
later-output controls. A separate cheaper review found no implementation defects. No legacy golden changed.

## Qualification

All six configurations passed. Compiled inputs were frozen and checked by SHA-256 before/after every gate using
`.build/host-borrowed-input/qualify.ps1`; no compiled source changed during the matrix. Its `source-hashes.json`,
`qualification.json`, `gate-x64-*.log` and `gate-arm64-*.log` remain in that ignored directory.

| Gate | Result |
| --- | --- |
| `validate-skills.ps1`, `format.ps1 -Check` | Passed |
| x64 Debug, Release, ASan Debug `test.ps1` | Passed; 77 request checks per normal run, 154 with ASan stress |
| Existing legacy goldens, WARP integration, ISA scan/controls, crash/ASan probes, stress and harness captures | Passed in all applicable complete x64 gates |
| Actual 19-file RAW hashes and local license/manifest evidence in each x64 gate | Passed; all selected files present, no corpus SKIP |
| ARM64 Debug, Release, ASan Debug `build.ps1` | Cross-builds passed; native ARM64 execution remains untested |

## Limits and next work

Qualification of these builders and reservations is separate from the staged host runner. Existing inline shell,
dialog, drop and rename producers still need migration before their copies use these admission boundaries.
Other variable-size active state requires explicit admission when introduced. Production panel OK and all
dependent Save/Close/import callers remain under the child plan's atomic activation rule. No whole-host
responsiveness, RAW decoding, SIMD/GPU speed or Photoshop comparison is claimed by this slice.
