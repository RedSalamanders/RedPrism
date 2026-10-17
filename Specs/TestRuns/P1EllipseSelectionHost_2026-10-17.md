# P1 Ellipse Win32 host qualification — 2026-10-17

Status: **scoped Release R2 qualified; combined native ASan/ARM64 due**. P1 remains WIP.

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

## Corrected Release R2 — qualified

Separate corrected source: `.build/P1Drafts/S5PressMarqueeHost-r2-Main/SourceFull`.
Actual build child 64624, ISA child, host child 97248 and strict reader child 17752 all exit 0.
Host runtime runs from 06:01:04.898208 to 06:02:23.538652 UTC. Main verifies all 341 exact compiler
commands and objects, all 1,710 archive files, source/dependency/product bookends, matching executable/full-PDB
GUID and age, zero ISA violations, all 89 Ellipse facts, 83 Rectangle facts and the complete 147-stage roster.
The four positive and 21 negative source-only reader controls pass; no complete passing transcript is fabricated.

Archive completion SHA-256: `A4D1B13909A00BFEA7F3476DE5E75C4F32A2478D984170CC41B0E4957A3FEEF8`.
Local independent receipt: `.build/P1Drafts/S5PressMarqueeHost-r2-Main/main-qualified.json`.

## Remaining qualification

Integrate the selected source and run the complete native Debug/Release/real-ASan/analysis and ARM64 matrix.
The full native ASan run will cover the actual host and all five executables; a duplicate private host-ASan
rebuild is unnecessary. Rectangle's earlier ASan receipt does not qualify the new Ellipse host by itself.
Ordinary activation, remaining selection branches and measured G1 remain open. DxUi source and pin are unchanged.
