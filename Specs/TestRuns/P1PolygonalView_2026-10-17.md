# Polygonal live zoom and pointer-leave qualification — 2026-10-17

Scope: selected gated caller, queued Zoom In/Out and native mouse leave/reentry. P1 stays WIP.

## Code and independent references

The fixture adds `zoom-live` and `pointer-leave-live` in New/Add/Subtract: six cases, 78 assertions and
63 cases / 791 facts total. All old 57 cases / 713 facts remain in source-defined order, joined Close
stays last, and the complete application reaches 152 stages / 10374 stdout assertions. Production C++
and existing legacy goldens are unchanged.

Queued view commands must succeed with `S_OK`, change zoom in the expected direction and preserve the
same original job/controller, live three-corner writer, committed selection and history revision. The
next click is mapped through the current viewport and still produces the independent 4096-center result
(New rectangle30 / Add union37 / Subtract remainder7), one entry and exact Undo/Redo.

Actual `WM_MOUSEMOVE` establishes native tracking; `WM_MOUSELEAVE` clears tracking and transient drag
without ending the original drawing. Reentry's next native corner stays on that same writer. Full joined
strong/weak CPU/GPU refunds remain required.

## Main review of the cheaper agent draft

Main checks actual WindowProcedure → OnPointer → EndPointerDrag → EndSelectionInteraction ownership and
the queued ZoomBy/SyncView path. The initial draft tests the next corner immediately after SendMessage;
Main corrects it to wait for event-driven worker replay. Main captures the original controller identity
once, requires exact Zoom `S_OK` and verifies that leave clears the drag as well as tracking.

The agent estimates only nine extra facts and a 722 total. Main rejects that count: the six new cases
repeat eleven common facts each and add twelve scenario-specific facts, yielding 78 extra and 791 total.
The independent source-derived reader establishes the exact roster before actual execution. It pins the
new source, requires all original 527 and 713 rows unchanged, and forwards the full 151-stage predecessor
stdout outside this fixture and complete stderr unchanged. It passes four byte-preservation positives,
two inherited-chain profile positives and 26 refusals. No passing native output is used as an expectation.

## Actual profile evidence

| Profile | Actual PID / exit | Recompiled / exact reused objects | Build files | Runtime files | Runtime completion SHA256 |
| --- | --- | --- | --- | --- | --- |
| Release | 33336 / 0 | 1 / 348 | 28 | 21 | `85CA88E7DBCE0F46A645777BA423D1A968378C19ABC5674F81C7F93C5D66750E` |
| ASanDebug | 54220 / 0 | 1 / 348 | 27 | 22 | `9C00626AF99D34B0F4F24D663F105DE886D07A1D78169D02A5B8E58FE8F6D004` |

- Release complete build archive: `0E49E63D2CEBCE6630220966CEE9A559B3F9ADF005AE4078CC052941321919BD`.
- ASanDebug complete build archive: `9D21CDE7152BB8D47C05BDE4C2E12086FB39BCE6657CC63F5BD98D2C5BECF7AC`.


Main verifies every archive byte, original compile flags, all 348 reused object identities, source/
dependency/product bookends, matching x64 PE/full-PDB identity and zero ISA violations. ASan instrumentation
remains real in every one of the 349 application objects. Exact actual MSBuild object spelling/order,
SDK paths, response files and private output flags, including LTCG intermediates, stay preserved.
The exact unchanged CPU artifacts/UAF prerequisite and ASan DLL closure are reused separately from the
two fresh full application runs under the existing source/dependency/artifact/profile/scope mapping.

## Local proof and remaining scope

Source/recipes/Main qualifications/adoption hashes live under `.build/P1Drafts/PolygonalViewNative-Main-r1/`;
the Main-reviewed source and reader controls are under `PolygonalViewFixture-Main-r1` and
`PolygonalViewReader-Main-r1`. The original agent draft remains under `PolygonalViewFixture-Agent-r1`.
Complete cache archives live under `D:/RedPrismBuildCaches/.build/p1-polygonal-view-{release,asan}-r1`.

Complete DPI/device/hidden-display and terminal/overflow/source scenarios, New/nonempty/Ctrl-lift exact
classification, final full native/analysis/ARM64, ordinary activation and measured G1 remain open. Earlier
ARM64 receipts qualify earlier source; native ARM64 runtime is unavailable. The inherited unexplained
held-gate ASan exit 5 remains an audit item; later passes do not prove a causal fix. This evidence makes
no RAW/Photoshop or presentation-latency claim and does not move P1 to Done.
