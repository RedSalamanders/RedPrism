# Existing history-budget decision reconciliation

Status: existing D13 policy and scoped controls verified; complete H3 and P1 remain WIP.

## Checklist

- [x] Read the actual uint64 formula, application constructor and current history contract.
- [x] Independently verify all ten budget/default/boundary facts in each qualified x64 profile.
- [x] Reconcile D13 in both P1 and the P2-P6 roadmap without changing runtime policy.
- [ ] Complete the separately tracked H3 consumers, phase admission, retirement and G1 qualification.

## Policy and evidence

`Common/Document.h:HistoryLimitForPhysicalMemory` returns 256 MiB for a failed RAM query and
min(RAM / 4, 2 GiB) otherwise. Division happens before the cap and uses uint64 arithmetic.
`Application` supplies this limit once during construction; its startup self-test checks that value.
The default DocumentHistory and DocumentSession retain the legacy 256 MiB class budget.

Main verifies the exact existing `cb1d1` Debug, `cb1r1` Release and `cb1a1` real-ASan archives,
successful profile/runtime receipts and ten Document facts in each: default entry limit, class and
session defaults, RAM-query fallback, quarter-RAM, cap/UINT64_MAX, division rounding, explicit
uint64 session limit, negative-entry clamping/uint64 preservation, and legacy 256 MiB trimming.
The budget header, boundary tests and owning history contract match those archived source bytes.
The application's pre-Run constructor/helper prefix is also unchanged. No new runtime is claimed.

This resolves a stale open decision cell; it does not qualify the complete managed history index,
all operation phases, source/cache/weak-owner retention, ordinary activation or performance.
The [captured-binding matrix](P1CapturedBindingRepair_2026-10-20.md) retains the complete profile evidence.
