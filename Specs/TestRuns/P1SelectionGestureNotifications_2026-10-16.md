# P1 Selection Gesture Notifications — 2026-10-16

Private x64 Release engine R3 is qualified for the shared inside-selection outline-move CPU protocol,
using real `HostSelectionPressInput`, two workers, the exact original parent, and opaque session authority.
`S5PressEngine-r3-Main/main-qualified.json` records exit 0 (PID 21304; 2026-10-16 22:43:49 UTC),
68 family assertions + 22,228 predecessors = 22,296 passing; 270 compile commands and 1,610 archive files.
The 68 comprise the unchanged prior 55, eight notification-count controls, and five foreign-callback controls.

Earlier attempts remain part of the evidence:

- Engine R1's `main-qualified.json` records functional exit 0, 55 + 22,228 = 22,283, with an explicit known presentation gap.
- `S5PressHost-r3-Main/owner-presentation-review.json` found per-move owner callbacks and missing post-callback authority validation.
- Engine R2's `main-failed-verified.json` records actual exit 1 and one new fixture failure:
  `selection-physical-prefix.foreign-notification.refusal-preserves-foreign-transform-original-selection-open-history-and-redo`.
  It contradicted `CanRedo()` by requiring true at history depth 1. R3 corrects this fixture to false while checking
  one retained future entry and its redo name. This repair changes no production code: 269 objects remain unchanged,
  and only `Document.Tests.SelectionPhysicalPrefix.obj` is recompiled.

Current source is under `.build/P1Drafts/S5PressHost-r2-Main/SourceFull/`:
`RedPrism/SelectionOutlineGestureJob.cpp`, `RedPrism/SelectionOutlineGestureState.h`, and
`Tests/DocumentTests/Document.Tests.SelectionPhysicalPrefix.cpp`. Nine job methods now live in the shared `.cpp`;
`Application.h` includes the stable state header and uses the job forward declaration, reducing Application-wide inline code.

`Consume()` applies every accepted move and preserves all scalar mutation/generation increments, then sends one
`Moved` notification for each consumed batch containing moves (zero for an empty batch). Overflow consumes all
511 accepted moves with one callback, generation delta 512, last terminal ordinal 512, and literal geometry
`{(512,2),(514,2),(514,4),(512,4)}`. The two async-shift batches produce two callbacks.

After the external callback, `NotifyMoves()` rechecks opaque gesture authority before closure capture or waiting.
The callback control opens `BeginTransform(true)` without changing generation, history revision, or depth during
that callback. The original parent fails with `E_ABORT` before closure preparation and preserves the foreign
transform's identity/draft, original selection, depth 1, and one future redo entry/name; `CanRedo()` remains false.
Generation is initial + 1 from the preceding accepted move. It neither closes nor aborts the foreign state.

The prior 55 numeric predicates retain exact-parent memory partition/refund, prepaid closing without new queries
or admission, FIFO acknowledgement before the younger writer, and zero reserved/releasing bytes after final readers.
Broader geometry-envelope and post-`Close` observer receipt flags remain inherited controls, separate from the 13 additions.

Selected identities below come directly from receipts/expected source hashes; source bytes were checked with
`Get-FileHash -Algorithm SHA256`. Receipt-file digests were calculated with the same command.

| Artifact | SHA-256 |
| --- | --- |
| Engine R3 `main-qualified.json` | `7EC3460216F0D2A57017260258E71FED32165A18519498DC1BD0BA50CC28F624` |
| Engine R2 `main-failed-verified.json` | `19545F9CA850DAADFF428E7132E497A115520896FE190F27EC428222E639CEB8` |
| R3 executable | `62A145A302190AF5BAAA1D82F33DDC28469D3B2900A0D832CD27EF5664BDCB8A` |
| R3 PDB | `5D3E4019BBC11803DC95AFB613CBE4353F7DCF2E33DA9B94146F65ABDB00A729` |
| R3 archive completion digest | `02BD3A18629A9507842D54E611AD444C6C72C764D3B4740057074173A84D7B53` |
| R3 archive manifest | `5F5655A1D7ED7E88F5A38A1D223EE19C8744BD5395993E65C8737C80338F9A94` |
| R2 retained failed archive completion digest | `1E2A4A1253C013E0DB02F8F9ECF9B495C6078445AD35657002AF7FCE93108BD3` |
| R1 functional archive completion digest | `661FED43D18543CA031DC82D84515846E06AE347CE423DA8123BE92B1915A450` |
| Current `SelectionOutlineGestureJob.cpp` | `88165A931C5A8446E8258A4BBC7743831CE0DE62A1239C313B755AAAB75409A1` |
| Current `SelectionOutlineGestureState.h` | `699C70C0BB22A30F9949AB4C4BC30EFBE343FB74A9A1F78BDDFBE67AC62B865B` |
| Current `Document.Tests.SelectionPhysicalPrefix.cpp` | `D32FD976E62CF0E4ADDA4E0D50A4C7C9CEDE98BB418D5AE6FAB2490C1AA0EED6` |

R3 CodeView: `36FEEA44-F750-4055-BB5F-056E94E68BFA`, age 3. Receipts are under `.build/P1Drafts/`;
R3 archive: `.build/P1EvidenceArchives/2026-10-16/P1-SelectionPressEngine-x64-Release-private-r3/`.

At this recording, real ASan engine `S5PressEngineAsan-r1-Main` is **PREPARED, NOT RUN**:
270 instrumented commands and a real UAF probe are required; preparation establishes no sanitizer pass.
Actual Win32 Application R4 (`S5PressHost-r4-Main`) is **BUILDING, NOT YET RUN**:
339 required compile commands, 144 application stages, and 41 new + 71 inherited scalar assertions are planned gates.
Ordinary activation, complete production memory bounds (the 8 MiB limit is a fixture), all other branches,
native adoption, G1, and P1 Done remain open. No application runtime result is inferred from the engine Release pass.

## Repaired shared engine real ASan qualified

The same shared engine passes real x64 ASan Debug, actual PID 71860, exit 0,
2026-10-16T23:05:24.792066–23:07:51.535798 UTC. All 68 family assertions and 22,228 predecessors
pass (22,296 total). Main reviews and executes the independent qualifier, verifies the full stream
and checks the exact 60-row sanitizer profile witness; all seven existing and seven independent
missing/duplicate/order/failure/profile/exit/count rejection controls pass.

All 270 actual commands require `/fsanitize=address`, `/MDd`, `/Od`, `/Zi` and the real sanitizer
requirement header. Its own same-executable UAF control exits 1 with an actual heap-use-after-free
diagnostic. The ordinary suite has no sanitizer diagnostic. Source/dependency/product bookends,
all 1,616 archived files, zero-violation ISA scan and executable/PDB GUID/age independently match.

Completion: `BE55409EA580391E7FDEFACE479B91F9C547D9C19D043297258AD196CA84036C`.
Runtime stdout: `3461A49847F45E2C968926700A320079C2D06686ED92F4572FA99B23E2F5AD31`.
Executable: `27738C7577EF527E7CD11B47F77997D6C86298E6A982C9F743F6B9FE8F4EBE8E`.
PDB: `45B30A0DD29F3B421037F37B73FB7C6C4DA4E58690B8E48F1B14396524687B64`.
CodeView/PDB: `831FDD02-FC5E-44D6-9DFD-AEA741EF9683`, age 1.
[Main qualification](../../.build/P1Drafts/S5PressEngineAsan-r1-Main/main-qualified.json).

This qualifies the CPU engine. The actual Win32 caller has build/ISA exit 0 with 339 sources,
but still owes its 144 stages/41 physical and 71 scalar controls. Its runtime waits for the
native Release suite. Complete caller admission, other physical branches, ordinary activation,
native integration and measured G1 remain open. The separate cache-derived admission draft
is uncompiled and does not alter this frozen qualified engine.
