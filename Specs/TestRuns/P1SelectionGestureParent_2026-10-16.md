# P1 selection gesture parent, cleanup and charged command — private proof

## Checklist and scope

- [x] Implement and independently review optional charged Select All/Deselect output and opaque prepared gesture cleanup.
- [x] Qualify 120 new assertions and all 22,100 predecessors in private CPU Release R3.
- [x] Include the separately verified geometry allocator/control allowance in charged Select All output/admission, strengthen the independent bound check, add moved-loss and post-Close final-refund witnesses.
- [x] Qualify the corrected source under real ASan with an actual UAF detection control.
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

## Additional review findings before adoption

R3's successful runtime is a functional checkpoint. Further independent review finds that `StorageBytes`
explicitly excludes geometry allocator/shared-control overhead, so the R3 charged Select All getter and its
bound fixture omit `WorkOwnershipAllowance(1)`. Main confirms this against `SealPreparedStorageForWork` and
prepares the correction in `S5PressGestureBudget-r1-Main/Source`, including whole-operation admission and a
separately measured control allowance in the test. R3 is not complete memory-accounting qualification.

That next overlay also adds moved pointer-loss End (R3 Lost is zero-offset) and an independent settled 8 KiB
endpoint observer. The observer can verify the exact original parent's refund after joined Close and final
weak-control release, while its own separate charge remains readable. These corrections are uncompiled;
the ongoing ASan source remains frozen. All physical/native/ordinary/G1 delivery gates stay open.

## Complete-accounting Release R4 and real-ASan R2 qualified

The reviewed correction caches geometry storage **plus** `WorkOwnershipAllowance`, includes the geometry
controls and both publication owners in admission, and checks those terms independently in the command
fixture. The parent fixture now also tests Lost after a nonzero move. A settled, independently admitted
8 KiB accounting observer verifies the live endpoint after Close: it still sees the full parent charge after
all parent strong readers die, then exactly its own 8 KiB after both final parent weak controls release.
This makes the original parent's final refund observable without querying the cleared scheduler counter.

Release R4 actual runtime exits 0 (PID 97284; 20:36:47.975754–20:36:58.763241 UTC). All 30 command, 41 parent
and 57 cleanup assertions pass with all 22,100 predecessors: 22,228 total. Main verifies all 1,602 archive
files, 268 compiler records, source/dependency/product bookends, zero ISA violations and exact executable/PDB
identity. Only SelectionCommandWork and its two updated fixtures recompile; the other 265 objects match.
Completion: `951868D0F449D2FDE7AE056D3A956BDA3C0E7307678E8675BBD83AB7943730D7`.
[Release qualification](../../.build/P1Drafts/S5PressGesture-r4-Main/main-qualified.json).

The earlier real-ASan R1 also passes all 22,220 functional assertions and its actual UAF probe, independently
verifying 1,610 archive files and 268 instrumented records. Its receipt explicitly excludes the then-known
geometry control gap. It is not reused to qualify the corrected source.
Completion: `4BB30FA8BB2A32739D81AB98DD88D8683911F867D0CB0B4AB72BE674CF9858B9`.

Corrected real-ASan R2 exits 0 (PID 93716; 20:38:59.571210–20:42:32.114078 UTC). Its own actual UAF probe,
ISA and complete strict reader also pass. Main independently verifies all 1,610 archived files, 268 real
instrumented compiler records, all source/dependency/product bookends, exact unchanged ASan predecessor
witness and exact executable/PDB identity. Only the same three objects recompile; 265 instrumented objects
match their preceding qualified hashes. All 22,228 assertions and the reader's seven rejection controls pass.
PE/PDB GUID `75ED3E6A-5C19-49CB-88A1-5F437270D306`, age 2.
Completion: `6E6BD718D1F31F45589ACDD2A97DB51A17859280AD39B3E6AE15C7509033BEDA`.
[Real-ASan qualification](../../.build/P1Drafts/S5PressGestureAsan-r2-Main/main-qualified.json).

The reviewed native adoption inventory is prepared in
`.build/P1Integration/SelectionGesturePrerequisites-r1-Main/source-adoption-inventory.json`: 14 modified
code/project/entry paths and 12 new test files. Native inputs remain frozen for the separate current matrix.
Physical Win32 dispatch, all display/admission/lifecycle consumers, native adoption and measured G1 remain due.
