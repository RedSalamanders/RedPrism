# Exact QWORD-switch terminal padding proof

## Implementation checklist

- [x] Preserve Close Project native r2's actual failed image/PDB and all source/evidence before repair.
- [x] Identify the seven contiguous QWORD-stack/RAX guard and dispatch instructions and terminal PDB table.
- [x] Prepare the exact RET/three-byte NOP extension in a separate workspace, without allowlist changes.
- [x] Review independent positive/refusal controls drafted by a cheaper agent; correct alignment, body/target
  layout, metadata and empty-padding fixture construction before accepting their results.
- [x] Freeze and run all twelve parser suites, retaining all eleven predecessor assertion multisets.
- [x] Reparse all three preceding failed images and this unchanged failed ASan image/PDB.
- [x] Independently verify and immutably archive the actual proof before activation.
- [x] Activate exact scanner/registered-test sources, then qualify the complete fresh native matrix.

## Failure and proof contract

The source-only workspace is
`C:\Users\eric\AppData\Local\RedPrism\PrivateWork\isa-qword-padding-proof`.
It inherits the current scanner and all preceding parser suites. Private r4 passes after three archived
proof/fixture failures; independent verification and exact archival pass. Native qualification remains required.
The failed native archive is `p1-selection-closeproject-outline-native-r2-intermediate`, completion
`698544D4BCD20C2F6EE727D817D4105C8DCE1C911E66CC4A6836F8746081D8CC`;
629 inputs and 26,394 evidence copies are preserved. See [actual failure](SelectionCloseProjectOutline_2026-10-03.md).

The new form differs from the existing QWORD-stack/RAX proof only at the table's terminal instructions:
a decoded one-byte `RET` and decoded three-byte `0f 1f 00` alignment NOP immediately precede the table.
Both rows must match the PE without overlap. Seven guard/dispatch rows still require the same full stack
displacement, bounded unsigned comparison, exact image-base load, DWORD RVA table load, ADD and indirect JMP.
Anonymous same-owner data, exact terminal procedure/table extent and decoded target boundaries remain required.

For the padded form, every procedure-body row before the table must be contiguous, known, nonoverlapping and
byte-identical to the PE. Table/default targets must precede the padding and avoid the guard interior. Known
direct branches/calls, LOOP-family and legacy-prefixed transfers cannot enter padding/table data or the protected
guard interior from elsewhere. The old unpadded proof and its 73 controls remain covered. Real VEX/EVEX code
outside proved table bytes is still subject to the normal gate. This proves data classification, without claiming
a complete CFG or adding a runtime object/function exception.

Validation must retain all 108 R9 controls, 73 QWORD controls, 57 RDI controls and every other predecessor parser
assertion multiset. The runtime allowlist hash remains
`6F55A40AB0CE23E98BB10839EC68765531129EF375B00DD29B298C3DC97EE560`.
Reparse exact archived original images; do not relink them to obtain a passing address layout.

## Private r1 metadata failure

All four unchanged images pass the extended scanner with zero violations. The first three retain exactly
27, 8 and 14 proved vector-looking table rows. The new failed ASan image proves 54 rows, not the initial
receipt expectation of 53: the extension proves the rejected EVEX-looking row and one formerly undecoded
data prefix. The scanner source is unchanged for the retry; the parser suites had not run at the failure.

The actual process returns exit 1. Immutable archive `isa-qword-padding-proof-r1-failure` preserves all 20
inputs and 46 evidence copies at `2026-10-03T12:55:06.5602959+00:00`, completion
`E5706A518E458FC77B2CD1D6CBDFB6175C8552C5098BFB1325F826A73AD3BF32`.
Only the expected receipt row count changes from 53 to the observed 54; this is scanner metadata,
not a changed legacy golden. Private r2 must still run every retained suite and the new independent controls.

## Private r2/r3 fixture failures

Private r2 passes all eleven retained parser suites and four unchanged images, then the first new fixture
fails because its PDB module headers lack the required colon. Actual exit 1 at
`2026-10-03T12:59:08.8537862+00:00` is preserved before correction. The immutable archive retains 20 inputs and
21,977 evidence copies at `2026-10-03T13:01:59.6237373+00:00`, completion
`2801E825A063900367068BC43443DE5EC3468A2474D3C5AAA61C3069A24C7F44`.

Private r3 runs the new suite first. Valid table controls and guard/refusal cases pass, then the MissingPadding
fixture attempts to read Length on a null PowerShell conditional result. The scanner is unchanged. Exact
20 inputs and 226 evidence copies are archived at `2026-10-03T13:06:05.127804+00:00`, completion
`6D46B495861DA45C9E88390F115761212814B0FA9A0BA6202CF63A9F57D9F284`, before correcting the fixture.

## Private r4 passed and archived

Actual PID 45028 runs from `2026-10-03T13:07:03.6604482+00:00` to `2026-10-03T13:10:21.7464268+00:00`
and returns exit 0. All 113 new controls pass: four accepted tables and 109 refusals. All eleven predecessor
assertion multisets remain identical, including 108 R9, 73 QWORD and 57 RDI controls. All four unchanged
archived images/PDBs pass; their proved table-row counts are 27, 8, 14 and 54. Runtime allowlist is unchanged.
The native verifier will require these 113 exact controls in every fresh x64 profile, alongside the retained
controls, full application stages and actual normal process receipts.

Immutable archive `isa-qword-padding-proof-r4-qualified` retains 20 exact inputs and 22,425 evidence copies
at `2026-10-03T13:14:31.0210143+00:00`, completion
`6205CE00B9BD228899C29E8DDBD8287EBBADDFC2E7BBDAF4D648CA4F04265ACF`.
Source manifest: `265DC2E21D271D5F4CFC83925709BCC30EB1989EBECEA3C9750AB446D0DD191E`.
Evidence manifest: `0A089D424ABB56FBE2FE7328CD629A0B4551A59E13763590C3F4E3B2D7BA3257`.
Independent verification rechecks all copies, actual process/result chronology, exact parser names and
the four original images before native source activation.

Close Project native r3 subsequently passes all six profiles. Every x64 suite retains these exact 113 controls
and all preceding suites. Independent verification and the 630-input/23,526-evidence qualified archive complete
before the next compiler ([native qualification](SelectionCloseProjectOutline_2026-10-03.md)).
