# P1 Ellipse Win32 host qualification — 2026-10-17

Status: **WIP; failed R1 retained, corrected R2 running**. P1 remains WIP.

## Scope

The private host extends the qualified Rectangle route to the Ellipse kind captured at physical Down.
The shared fixture retains all 83 Rectangle facts and adds 89 Ellipse facts over 14 scenarios,
including an inside-old-outline move. Expected complete roster: 147 Application stages,
8,577 stdout facts and 7,613 stderr facts. These are source-derived expectations, not passing results.
Ellipse CPU geometry/parent Release and real ASan are qualified separately in
[P1EllipseGestureParent_2026-10-17.md](P1EllipseGestureParent_2026-10-17.md).

## Retained Release R1 failure

Actual build child 79544 exits 0; ISA exits 0; actual host child 14280 exits 5.
The new Ellipse inside case reaches assertion 80, then fails its combined geometry/generation/history predicate.
The new fixture incorrectly expects one generation advance. The existing scalar move updates the preview
once and the acknowledged End expires authority once more. Existing physical move tests require two
advances and one history entry. The correction changes only this new expectation; it preserves all literal
geometry, AA, history and lifetime predicates, every label and production behavior.

All 1,706 archive files are independently checked against both original bytes and the manifest.
Completion SHA-256: `5B3B53CA48ABF7177004B2406E8A4014E636F8151E8349800C3C24B061A30EFE`.
Local failed receipt: `.build/P1Drafts/S5PressMarqueeHost-r1-Main/main-failed-verified.json`.
Original failed source and complete artifacts remain intact. No failed run is counted as qualified.

## Current work

Separate corrected source: `.build/P1Drafts/S5PressMarqueeHost-r2-Main/SourceFull`.
Fresh Release compiler set: 341 translation units; strict source-derived reader retains all predecessors.
Release runtime/readback, real host ASan and final combined native qualification remain due.
Ordinary activation, remaining selection branches and measured G1 remain open. DxUi source and pin are unchanged.
