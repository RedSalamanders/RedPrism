# P1 selection gesture parent, cleanup and charged command — private proof

## Checklist and scope

- [x] Implement and independently review optional charged Select All/Deselect output and opaque prepared gesture cleanup.
- [x] Qualify 120 new assertions and all 22,100 predecessors in private CPU Release R3.
- [ ] Qualify the same frozen source under real ASan with an actual UAF detection control.
- [ ] Complete physical Down/Move/Up/Lost, overflow, ordinal exhaustion and ordinary host caller integration.
- [ ] Adopt reviewed code/tests/contracts and qualify latest native profiles, complete consumers and G1.

The source is private at `.build/P1Drafts/S5PressGesture-r1-Main/SourceFull`. Its real-ASan attempt is
`S5PressGestureAsan-r1-Main`. Neither is native adoption or ordinary activation; P1 remains WIP.

## Implementation and independent references

The optional selection-command memory route retains every new history metadata/control owner. Select All's
four-point geometry and its final weak control retain the output owner too. `OutputMemoryBytes` includes
local metadata, the complete geometry envelope and owner allowance with overflow checks. Changed publication
refuses unbound/underbound output before mutation; default/reference and no-op behavior remain unchanged.
Independent literals are the antialiased 8×8 rectangle, Deselect's absent selection, exact Undo/Redo results,
redo/generation preservation on no-op, and unbound/underbound/stale/cancel/weak-owner refusal and retention.

An opt-in prepared selection opening includes its exact predecessor-bound no-op End. The opaque live gesture
witness renews only after its own validated scalar Move. Cleanup publishes that prebuilt End, discards the
scalar preview by swaps, retains replaced roots in the original payload for deferred destruction and matches
the reference Cancel's two generation invalidations. Foreign document, generation, history, active target or
pending transform refuses without overwrite. Transform and transferred brush/warp states have the complete
settled-state guards; a targeted transform regression proves generation/history alone cannot authorize them.

The real two-worker parent fixture admits 8 MiB once, with a 64 KiB continuing-output limit and preclaimed
terminal slot. Literal selection vertices are `(1,2),(3,2),(3,4),(1,4)` and translated
`(-1,5),(1,5),(1,7),(-1,7)`. The original ticket survives opening, movement, zero-offset End then freshly
captured Deselect, pointer-loss End, failed worker, cancellation and joined Close. No useful work follows final
memory transfer. Last strong and weak controls retain original charges; joined Close is measured through the
foreign retained endpoint's `ResidentBytes`, because Close clears the scheduler's former admission counter.
This is a CPU caller protocol fixture, not physical Win32/Application dispatch or GPU first-display proof.

## Retained failed attempts

R1 compilation exits 1 on nine fixture API diagnostics: missing Selection header, treating void Undo/Redo as
bool and a nonexistent CancelAll method. No runtime launches. Main independently verifies all 1,574 archived
files before fixing those API calls, retaining the original behavior predicates. Completion:
`E1339243A4D799B2210E6A2C352AD02A1D9DB2D8E25CB9CABBBBCBE9C73C8B95`.

R2 compilation/ISA pass; actual runtime exits 1 (PID 73996; 20:19:45.622623–20:19:56.747232 UTC).
Four new lifetime predicates fail in two modules; all 57 opaque cleanup assertions pass. Main independently
verifies all 1,598 archive files. The command fixture's diagnostic weak memory observers themselves retain
leases, so they must release before testing the actual last geometry control/refusal refund. The Close fixture
must query the live retained endpoint rather than the deliberately zeroed closed scheduler counter.
R3 changes these fixture lifetimes/observations only; production code and literal geometry/history expectations
remain unchanged. Completion: `ECB875F5D8B45CFAD8A713B367DA4617E616D43271C560531EC4345355148E50`.

## Release R3 independently qualified

Actual runtime exits 0 (PID 90476; 20:22:19.167172–20:22:30.185292 UTC). Build, ISA and strict full-stream reader
also exit 0. All 30 command, 33 original-parent and 57 opaque cleanup assertions pass with all 22,100
predecessors: 22,220 total. The reader's source-defined positive and seven rejection controls pass, preserving
the complete actual stream, summaries, footer and unchanged predecessor expectations.

Main verifies all 1,602 archived copies, 268 actual compiler records, all source/dependency/product bookends
and exact executable/PDB identity. Only the two repaired fixture objects change; the other 266 retain exact
hashes. ISA reports zero violations. PE/PDB GUID: `CB6305B9-F05D-4EC6-8C5B-266344B5EFED`, age 2.
Completion: `D47F84BDB66F788A710C984968E3B3F67863F6812E5E95A67BA41201AD105BF9`.
[Main qualification](../../.build/P1Drafts/S5PressGesture-r3-Main/main-qualified.json).

Fresh real ASan is building this same frozen private source. Physical event routing, complete display/admission
and lifecycle consumers, native adoption, all remaining P1 delivery areas and measured G1 remain due.
