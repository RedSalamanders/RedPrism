# Existing H3 residency and semantic-thumbnail core reconciliation

Status: scoped existing core verified; production caller/admission/cost qualification and H3/P1 remain WIP.

## Checklist

- [x] Verify current scope-bound nonmovable leases and the held-thread source guard.
- [x] Verify 38 retained assertions in each qualified B21 x64 Debug/Release/real-ASan run.
- [x] Compare 1,451 relevant source/project/dependency inputs byte-for-byte with current source.
- [x] Confirm current complete `oh4a1` DocumentTests uses the exact B21 ASan executable/full-PDB pair.
- [ ] Complete all production caller admission, original-ticket retirement, ordinary activation and G1 gates.

## Existing implementation and proof

`Raster::ResidencyLease` already deletes move construction/assignment and checks its current thread while
held. The owner try path makes one CAS attempt; worker waiters block on atomic value changes. Existing
tests include compile-time nonmovability assertions, recursive assembly refusal before allocation/wait,
dense reads while held, reset/reacquire and subsequent cache publication. No new runtime foreign-thread
death probe is claimed.

The 38 retained assertions comprise five lease controls, four cancellation/waiter controls, nine live-index
maintenance controls, fifteen original-edit late-residency refresh controls and five semantic-thumbnail
ownership controls. They retain the independent 4/8/12-byte history expectations, cancellation/allocation
failures, saved revision, stale publication refusal, eager mask thumbnails and original weak-owner refunds.
They are existing core assertions, not the separate 38-control history-original-ticket fixture.

Main reads the actual-zero `cb1d1`, `cb1r1` and `cb1a1` full-profile/runtime receipts and verifies each
named assertion exactly once in each preserved original stream. All1,451 relevant source/project/dependency
inputs match current bytes. The three changed translation units in the current Hue/utility step are
explicitly excluded and are absent from DocumentTests' 299 compiled items. Current `oh4a1` independently
passes all 24,003 Document facts and uses this same B21 ASan executable/full-PDB pair:

- Executable SHA256: `DD36943F029A6C26D1C7D821FC19F70102AB4B26D50BA338572F2ECF95033EE2`.
- Full-PDB SHA256: `5EBAC299DBF33836348EE36FC7A79C1020878E965EFB61CC78EA72716A061A60`.

Reader evidence: `Z:\src\RedPrism\.build\P1Drafts\HistoryCoreReconciliation-Main-r1\verified.json`.
This reconciliation runs no new runtime, changes no product behavior/golden and establishes no ARM64 runtime,
timing, complete caller migration or H3/P1 completion. It closes stale core checklist wording only.
