# P1 bounded lasso host-input adapter — private proof, 2026-10-13

## Checklist

- [x] Preserve all 21,592 predecessor records and independently check 80 input facts plus summary (21,673 total).
- [x] Actual Release compilation, linked ISA and complete DocumentTests exit 0 with immutable input/product bookends.
- [x] Reference exact ordered event vectors, full 512-slot accepted prefix, and separate physical/lost terminal.
- [ ] Connect actual physical Application handlers, original admission, replay, worker finish/history and draft GPU display.
- [ ] Qualify native configurations, lifecycle/failure cases and measured G1 before ordinary activation.

## Behavior verified

The adapter reuses PointerInputStream. Captured document/active/crop/kind/mode/view/ordinal values and physical
document coordinates are fixed descriptors, with no geometry owner. Point, cursor set/clear, Backspace and
marquee square/center events keep their order. Terminal AA comes from the physical Finish event. The full ring
retains a separate Up. Overflow keeps its exact accepted prefix plus Lost, refuses later Finish, and remains
owned until acknowledgement. Stale, zero/reused/exhausted openings, nonfinite coordinates, invalid descriptors,
wrong-kind operations, premature acknowledgement and late events refuse without changing accepted state.

Independent tests compare literal expected sequences and a formula-generated 512-event reference prefix.
They also exercise Peek/Take/Drop/Busy and no-throw owner entrypoints. Main corrected the test's integration
include path and the reader's separately declared summary; all 80 predicates and legacy goldens are unchanged.
The initial reader refusal happened before a build and remains preserved. No failed build is inferred from it.

| Actual isolated run | Evidence |
| --- | --- |
| Qualified report | `9B56C9797EF2DD823117CC7C92B53213AAC74AE6E5B934DBF23DE7CFA4EEAC66` |
| Build | PID 87568, exit 0 |
| ISA | PID 39040, exit 0 |
| DocumentTests | PID 106088, exit 0; 21,673 PASS records and empty stderr |
| Document stdout | `E05F939C1F876A9D0ED622F8D7AF6392D41CA863331E40F2A9FC1F38FDC29367` |

Source, tools, dependency libraries and products are bookended in private
`LassoSessionBridge-r1-Main/actual-release-input-r1`. This proves the adapter/component; the actual application
still selects reference drafts. Ordered host acquisition/replay, draft display, native runtime, ordinary
activation, S5 and G1 remain unqualified by this receipt.
