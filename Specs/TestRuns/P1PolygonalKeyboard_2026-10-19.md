# Original Polygonal keyboard command ordering — 2026-10-19

Status: production command barrier and selected keyboard regressions adopted after focused Release and
real-ASan qualification. P1 remains WIP; full latest Application/native integration is separate.

## Defect and correction

The existing 41-case fixture exercised typed `QueueHostInput` and missed the real key dispatcher.
The unchanged-product Release control (revision 1, PID 106748, exit 5) fails the first new Ctrl+D
queue assertion: 427 checks, one failure, 41 completed cases. Revision 2 adds diagnostics only and
reproduces the same stdout/failure (PID 13776, exit 5): default binding matched command701 at canvas
focus, queue size0, original incomplete, generation1→2, no pending lasso and no history barrier.
Thus Deselect mutated committed state before the admitted original job acknowledged cancellation.

`Application::OnCommand` now includes the original Polygonal journal's scalar `Busy()` in its existing
non-view/non-file command barrier. The old legacy Polygonal owner check addresses a different producer.
The selected original keeps its existing typed command, ordinal, cancellation acknowledgment and
history execution. This adds no source capture, geometry traversal or fallback. Ordinary routing stays
disabled pending the remaining P1 consumer/resource/lifetime/timing gates.

Three new cases use `HandleAccelerator`, which sends actual WM_KEYDOWN/optional WM_CHAR/WM_KEYUP
with the isolated self-test keyboard: Ctrl+D, Ctrl+Z, Ctrl+Shift+Z. Before any queue mutation or pump,
require one matching HostCommandInput, the current document serial, an ordinal after the last corner,
no active request and a Pending receipt. Retain that exact owning observer; never dispatch a second
typed command. The existing intermediate cancellation barrier and literal selection/history/revision/
generation, fixed corner receipts, original ticket/capture counts and final refunds then all execute.
The fixture diagnostic records individual predicate operands only on failure.

## Focused native proof

Revision4, cohort `D:/RedPrismBuildCaches/.build/p1-polygonal-keyboard-focus-native-r4`:

| Profile | Actual runtime | Fixture | Fresh / exact inherited TUs | Verified artifacts |
| --- | --- | --- | --- | --- |
| Release | PID89188, exit0, 2026-10-18 22:17:36.455190 UTC | 44 cases / 469 PASS facts | 2 / 349 | 65 |
| ASan Debug | PID78376, exit0, 2026-10-18 22:17:13.464679 UTC | 44 cases / 469 PASS facts | 2 / 349 | 67 |

The two fresh TUs are Application.cpp and Application.Tests.PolygonalDispatch.cpp. All 351 linked objects
have exact matching-profile ancestry; Main independently verifies inherited source/object hashes,
fresh compiler flags/source sets, every linked object, all frozen source inputs, PE/full-PDB GUID/age
and executable/PDB hashes. ASan uses real instrumentation and its forced header. The standalone
private driver executes this fixture only; it grants zero full-Application stages and no new UAF-probe claim.
All 420 original matching-profile assertion names are preserved. Both stdout hashes are
`E33F27146A999864B2D366788943FDE029A1C2E240ACE13D7E743071C4EF931B`; stderr is empty.

Both final ISA scans pass with zero violations (Release2, ASan59 inline vector-data rows).
Independent saved-package/PNG/JPEG readback and exact output inventory pass on each run's own outputs.
Release artifact manifest: `30CE788E40E5C481672E83E3A07DF7FDB66A5012E02857142946551C46630DAD`;
ASan: `0208CF9D659AE36080EA04BCBAA84047383BECA82F6371A2AD7DC630FFE01D6B`.
Readers are under `Z:/src/RedPrism/.build/P1Drafts/PolygonalKeyboardReaders-Main-r4`.

## Retained failures and ownership

Revision1's failed native runtime and unlaunched ASan runtime are preserved. Revision2's diagnostic
reproduction changes no production bytes. Revision3's generated diagnostic extraction is malformed and
both native compiles fail; there is no runtime. Main fixes the source extraction before revision4.
The initial revision3 preparation substring failure also precedes compilation. None is hidden or replaced.

All seven frozen build maps and 2,034 distinct original source blobs are independently verified in
`original-source-blobs.zip`, SHA256 `FC77DF58BF2AB245C8798EAB96A7DBB3CA1317E3BB1E7894CDD65D48F3741668`.
Main reviews/runs the cheaper agent's failure reader and separately checks all seven maps and actual
failure diagnostic; the agent's limited Release-only PE/PDB claim is not expanded into runtime proof.
No DxUi pin/library or ISA allowlist change.

## Remaining qualification

Latest full integration must retain 152 Application stages and 11,053 stdout facts with every predecessor.
Latest ARM64 Release/instrumented-ASan cross-builds, full native Debug/Release/real-ASan/analysis,
actual menu widgets, physical keyboard devices, other overlap/held-I/O/view/device/lifetime branches,
Ctrl/lift, ordinary responsive activation and measured G1 remain open. Native ARM64 runtime is unavailable.
The earlier fully qualified ASan r1141 remains valid for its previous 41/420 source, not this correction.

Repository gates after adoption: validate-skills.ps1 passes all 15 skills; format.ps1 -Check passes all 1,311 files; git diff --check passes. Root Application.cpp and dispatcher fixture are byte-exact to the qualified revision4 sources.
