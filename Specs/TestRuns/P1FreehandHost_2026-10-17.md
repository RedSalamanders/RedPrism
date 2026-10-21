# Selected Freehand original parent and physical caller — 2026-10-17

Status: qualified scoped Release/real-ASan component, adopted; P1 remains WIP.

## Implemented scope

The captured immutable drawing plan governs original admission, allocation and worker execution for
Freehand New/Add/Subtract. Its worker creates/prefaults 1,023 sealed 64-point blocks plus an inline tail
(65,536 accepted vertices). The 512-slot pending queue remains a separate capacity. Original-ticket
containment, opening, ordered input, clipping/booleans, history/residency refresh and retirement retain
exact source/editor/history/writer authority. Only New may choose outline movement.

Owner replay accepts at most sixteen moves per continuation, touching at most one prefaulted 64-point
chunk per metadata update. The plan now explicitly reconciles this existing single-writer protocol
with the earlier proposed worker append. Full traversal/geometry/history and graph retirement remain
on workers. Structural bounds alone establish no 2 ms or G1 timing qualification.

Generic pointer cleanup now leaves an owned selected prefix/controller with its original Lost
acknowledgement. It cannot cancel that writer first. Accepted Up remains authoritative. The actual
fixture covers live loss for Freehand, Rectangle and Ellipse in all three modes.

## Exact successful validation

| Scope | Release | Real ASan Debug |
| --- | --- | --- |
| Current compiler records/artifacts | 281 Document + 346 Application, exact 627-object lineage | Same 627 records, forced sanitizer instrumentation |
| Complete Document assertions | 23,233; exact unchanged image/input/profile reuse from PID 34216 | 23,233; fresh PID 101976, exit 0 |
| Complete Application | PID 22500, exit 0 | PID 8020, exit 0 |
| Complete Application roster | 151 stages, 9,583 stdout / 7,613 stderr facts | Same complete roster |
| New component facts | 75 plan + 9 queue + 110 parent + 472 actual-host facts | Same independent references |
| Runtime archive | 34 files, independently verified | 38 files, independently verified |
| Sanitizer control | Not applicable | Actual UAF probe PID 3940, expected exit 1 |

All previous 23,039 Document facts and every predecessor Application record remain required. Literal
geometry includes areas 24/39/15, complete coverage grids and the 131-point/two-sealed-block case.
The Win32 fixture has 51 cases and 472 assertions, including absent/empty/no-layer, New-inside move,
physical/captured settings, multiblock, short paths, loss, cancellation, refusal, stale target, overflow
and joined Close. Independent readers check exact order, summaries and real numeric exits.

The first complete private builds compile all 627 TUs. The corrective builds replace exactly three
objects, then one test object; Main verifies unchanged object bytes, original command/dependency
lineage, source/product bookends and exact executable/full-PDB identities. This is verified incremental
qualification, not a claim that all 627 TUs recompiled after each repair.

Release's original ISA gate fails on inline DWORD data; the corrected scanner independently clears
the exact unchanged image. Its complete Release parser/compiled-control suite passes separately.
[Scanner evidence](IsaR9Direct_2026-10-17.md). DxUi and the ISA runtime allowlist are unchanged.

## Retained failures and corrections

1. R1 compilation fails on C4459/C2220: a header template iterator `current` shadows existing test globals.
   The identifier-only `sealedBlock` rename preserves behavior and warning policy. All 1,675 archived
   files and 278 objects were verified before repair.
2. First Release Document run has 23,232 PASS and one new failure: a minimum-four-output-vertices
   assumption on a collinear path. The contract permits retained collinear vertices. The repair keeps
   literal bounds and area 128 and independently checks all 288 pixel centers; no legacy golden changes.
   Failed 25-file archive: `120C9E0CC1F77C27C29CEB7476E24FC9C6DBF2B27EF5080BDD9E40D58E77ED5F`.
3. First actual Application run fails at live capture loss: generic cleanup cancels the writer before
   its original parent consumes Lost. Main verifies the 20-file failure archive, repairs ownership
   and adds live Rectangle/Ellipse regressions.
   Failed completion: `6275BD8B8CE64F4E1EAD2254DFB2D14DBD4DEE367A67F221FF24AC93F0D1319D`.
4. The corrected Release run passes every Document assertion, then fails an earlier clipboard test
   before Freehand starts. Its diagnostic records two armed waits/four native acquisitions after the
   original holder closes. The test incorrectly requires exactly one wait and its own holder at every
   wait. The repair preserves original-holder proof at the first release wait/all exhaustion waits,
   then requires the original holder closed at later release waits. Every original ticket, barrier,
   reservation, six-acquisition cap, zero-injection, literal pixel/history, join and refund predicate stays.
   Native diagnostics allow the contract's bounded additional release denials in their exact window;
   assertion labels/order and all unrelated bytes remain checked. Production clipboard code is unchanged.
   All 31 failed archive files are verified; completion
   `492D0318154EE61F1A59FAEB9FFEA1E69501E369FFC6F9FE2F02A4169D3BFA8E`.
5. Main's final UAF reader initially expects the wrong symbol spelling. The actual diagnostic names
   `RunAddressSanitizerProbe` in `Common/AddressSanitizerProbe.h:12`. The old reader and correction
   remain retained. Correcting that reader reuses the actual run; it does not replay tests.

## Receipt identities

- Release build archive (original failed ISA retained):
  `6870929E92722AF91D3690600E6107C3D25BF64F00A66F28613D9A6C8E7965F2`.
- ASan build archive: `F62BD3AC78B84EBF2185F3080D38AEFBBE8A27BD175969A01EB9F40E1DCCA5DA`.
- Release runtime: `D52284CEEECC7DB24375D45A81918BE6650394C295755CF14B8B8BE227382BF3`.
- ASan runtime: `3210FFB85D72453B4534CD6069AD62DA5C62A78711C3A68A31644F642DED9A5E`.
- Actual UAF diagnostic: `9E15F8C49F38F42783477358C940A2D6F93F2984917EB522DB6ADAB8FBCA9CCF`.

Private recipes/readback are under `.build/P1Drafts/S5FreehandHostRelease-r4-Main/`,
`S5FreehandHostAsan-r3-Main/` and `S5FreehandHost-r1-Main/`. The adopted-source manifest is
`S5FreehandHost-r1-Main/main-adopted-source.json`. Original source, objects and failed products remain
recoverable; storage compression preserves their bytes and exact last-write times.

## Limits and next work

This qualifies the explicit selected component. Ordinary activation remains off. Remaining Polygonal,
Wand/Ctrl-pixel-lift branches, complete coverage/display consumers, original phase/retirement bounds,
latest full native Debug/Release/ASan/analysis/ARM64 cross-builds and measured G1 remain open. Native
ARM64 runtime is unavailable. None of these scoped receipts is a P1 Done or Photoshop/RAW speed claim.
