# P1 lasso keyboard, allocation and cancellation proof — 2026-10-13

Status: **private actual x64 Release scoped PASS**. Native integration, ordinary startup, complete caller
lifetimes, opaque resource bounds and measured S5/G1 remain open.

## Scenarios and independent references

The original 50 physical/FIFO facts remain in order. Forty-six additional assertions cover:

- Seven keyboard/short-outline facts: actual accelerator WM_KEYDOWN/character/key-up routing, short New
  double-click and Enter deselection, short Add preserving the original selection/history, polygon
  Backspace/Delete, and Escape without selection/history mutation. Expectations preserve the legacy
  Selection.swift and EditorCanvas.swift behavior; no minimum-point gate or golden rebaseline.
- Twenty-four facts at four reached worker allocation/preparation boundaries. Each original runner
  translates injected `std::bad_alloc` to `E_OUTOFMEMORY`; exact capture counts distinguish failures
  before capture from failures after preparation. No partial selection/history/generation publication,
  synchronous fallback, busy draft or retained memory owner survives settlement.
- Twelve facts for cancellation while the original opening/finish worker is held after preparation.
  The receipt stays Pending until worker acknowledgement, then becomes Cancelled with `E_ABORT`;
  selection/history remain unchanged and original weak owners expire through off-owner retirement.
- Three whole-memory-refusal facts: zero physical/commit admission fails before worker allocation or
  capture, leaves source/history/generation intact, and does not invoke the synchronous reference path.

The test gate reuses the existing borrowed processing probe and the original job's cancellation/shutdown
lifetime. Its owned event wait has a 10-second deadlock failure bound. Gate configuration precedes the
corresponding job admission; weak controls are read after joined settlement. Default production behavior
and Application's previously qualified layout are unchanged.

## Actual run and independent readback

Actual build PID 61884 exits 0, zero warnings/errors, 15:43:52–15:59:59 UTC. Linked ISA exits 0.
Actual application PID 30540 exits 0, 16:00:16–16:01:42 UTC, `--self-test --warp` on the inherited desktop.
Main independently runs the expected-only source-derived reader: **138 application stages, 96 lasso
assertions, 7,701 stdout PASS records, 7,613 stderr PASS records and 55 summaries**. Every predecessor
assertion/order/duplicate is retained. All 728 declared source/dependency inputs and 360 products have
unchanged bookends. The complete archive also retains and verifies those products, including intermediates.

Application stdout SHA-256: `E7BA283F84DD2207C0D817B85E519289F3D87A57E2BFC323CDBC03EBF07BB078`.
Application stderr SHA-256: `437E82887C9BEBC1DE07127134767FBF970942EF8DC6876E027D808B4150A592`.
Independent ordered-readback SHA-256: `BC298F02AB13167AABA28874017B44AB5F373D486B5F4A6525207689A8F64668`.
Archive: `Z:\CodexArtifacts\RedPrism-P1\2026-10-12\P1-LassoPhysicalInput-r2-passed-20261013`.
Archive manifest SHA-256: `78AAC55906948B1AA035AD8EE29A2A5ABC7C1CF2DFBA4560631BCD96753510C1`.

The independent source review finds no additional defect in the gate/probe/joined weak-owner fixture.
That review is separate from the runtime evidence above. This scoped private result does not replace the
required latest-source native Debug/Release/real ASan, analysis, ARM64 cross-builds or large-lasso latency
measurements. Remaining delivery is tracked in [the lasso checklist](../Plans/WIP/LassoWorkers_2026-10-12.md).
