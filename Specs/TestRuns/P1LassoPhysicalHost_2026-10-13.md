# P1 lasso event-time ordering and physical handlers — 2026-10-13

Status: **private x64 Release scoped physical-handler/FIFO PASS**. This record does not qualify complete
Win32 pointer capture, keyboard/lifetime/failure scenarios, ordinary startup, native profiles or measured G1.

## Verified repair

The previous 35-case host snapshot assigned its Finish ordinal during replay. The physical candidate now
admits its actual terminal descriptor at the event. A younger Deselect cannot overtake that descriptor;
the independent reference requires both receipts to succeed, the lasso history entry first, Deselect last,
and exactly two entries. Conditional near-first/double-click descriptors likewise own event-time ordinals.
After older corners/backspaces replay, a rejected early proximity close acknowledges without history and
keeps the gesture live; a chosen close uses the original physical press AA and discards only fixed future
samples. The existing source handles all replay/barriers; no new owner or UI-action pump.

The 15 added references cover the terminal-before-younger-command case, actual freehand press/move/release
handlers (literal triangle area 800), and two delayed polygon cases (square area 1024; an early near-first
click with fewer than three corners remains live and final trapezoid area 800). The original 35 cases remain.
Main independently runs the source-derived ordered reader: **138 stages, 50 host assertions, 7,655 stdout
PASS records, 7,613 stderr PASS records and 55 summaries**. Every predecessor assertion/order/duplicate is
retained. Complete declared source/dependency/product bookends remain unchanged.

Actual build PID 76892, exit 0, zero warnings/errors; linked x64 ISA exit 0. Actual application PID 58380,
exit 0, 15:18:41–15:20:12 UTC.

Application stdout SHA-256: `F44B0995552C44122BBCEB62DF3F38F98EA812D74D2CBA9B8F79FF7927CC8591`.
Application stderr SHA-256: `437E82887C9BEBC1DE07127134767FBF970942EF8DC6876E027D808B4150A592`.
Ordered-reader result SHA-256: `F0A10D2C22CE0AAF0A8D1F54744A7A92E4F8CE74B6C771DC24642E20AD924505`.
Complete source/logs/expected roster/reader are preserved at
`Z:\CodexArtifacts\RedPrism-P1\2026-10-12\P1-LassoPhysicalInput-r1-passed-20261013`.
Archive manifest SHA-256: `CB11CCE53279A22F95C28BE68FAAE55C624D5575E4AE7E8BA64A86E33C845546`.

## Review adjudication

A cheaper review proposed gating short polygon double-click/Enter on three points. Main rejects that
proposal against primary legacy source: Selection.swift:192–211 deselects short/zero-area replace outlines;
EditorCanvas.swift:1676–1682 finishes on double-click without a count gate, while proximity requires >=3;
EditorCanvas.swift:1469–1472 also finishes on Enter without that gate. The owning Document_Tools:319–321
and current degenerate New/Clear versus Add/Keep references preserve this behavior. Do not rebaseline those
rules or add the proposed gate. New actual keyboard/short-outline controls exercise this existing contract.

## Next qualification

After this complete archive closes, the next private candidate adds real WM_KEYDOWN/character/key-up routing
through HandleAccelerator, original-runner partial-allocation faults, held-original owner cancellation and
whole memory-admission refusal before capture. Fault controls reuse the borrowed processing probe, leaving
Application's qualified layout and default production behavior unchanged. The held worker waits only on its
owned release event or original shutdown event with a failure bound. Complete native integration/consumer
admission, full lifetime/reentry/capture cases, ordinary activation, opaque resource bounds and S5/G1 remain
open in the [lasso checklist](../Plans/WIP/LassoWorkers_2026-10-12.md).
