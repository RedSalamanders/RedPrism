# P1 Rectangle/New parent and exact lasso intent cleanup — 2026-10-17

## Qualified scope

The private CPU parent passes all **22,596** assertions in Release R5 and real x64 ASan R2: 75 parent facts,
30 exact-intent cleanup facts and all 22,491 predecessors. This qualifies the original-ticket protocol and
model publication. Actual Win32 qualification, ordinary activation, latest native integration, measured G1
and P1 Done remain separate gates.

The original admitted Down covers inside-outline movement and outside-hit Rectangle/New. Hit testing,
opening, geometry, history preparation and residency refresh reuse the original parent/ticket. The owner
captures the source only after admission. A worker allocates the retained drawing payload. Accepted samples
replay in order; release contributes terminal AA without inventing a Move. Lost/cancelled drawing preserves
the original selection. Click, absent selection, explicit empty, large prior and no-layer cases have explicit
numeric/state references. Rectangle/New uses the same bounded 64 KiB geometry limits as its admission plan.

Opaque `LassoIntent` can clear only its exact pending opening/finish intent. It cannot reset a newer intent,
a foreign session/document/editor/history epoch, or an unrelated draft. Closed own-writer cleanup swaps
only its captured history owner for retirement under the original ticket. Foreign state remains intact.

## Modifier compatibility and retained failures

Legacy `Composition/Document/Selection.swift` lines 139–140 and the existing Windows canvas establish that
physical Alt does not request centered marquee drawing. Down uses Alt for selection mode. The explicit
model `fromCenter` API and its original goldens remain supported. Physical Shift held at Down is rearmed
only after release. R4 changes only two new physical Alt expectations to the legacy bounds, reproduces both
failures, then R5 repairs production replay with those corrected expectations unchanged.

Retained attempts:

- Release R1: warning-as-error C4459 in the new fixture; rename its global scenario variable only.
- Release R2: cancellation settlement timeout; the driver omitted Resume after cancelling its suspended
  original ticket. Repair the driver to match Application's existing cancellation route.
- Release R3 and ASan R1: numeric protocol passes, subsequently refused as host-semantic qualification after
  the Alt source audit. ASan R1 remains explicitly scoped, not product-qualified.
- Release R4: actual two-predicate Alt regression, 22,594 PASS records and the aggregate one-family failure.
  The reader-only footer repair preserves the same process, source and failed stream; no C++ rerun.
- Actual Win32 R1: three new fixture API compile errors. R2: disk guard refuses before compilation.
  R3: the new target-change fixture expects Failed where the existing E_ABORT route correctly publishes
  Stale. That actual exit-5 attempt and archive are retained; separate host qualification remains open.

## Passing evidence

| Profile | Actual runtime PID / exit | Assertions | Compile records | Archive leaves | Completion SHA-256 |
| --- | --- | --- | --- | --- | --- |
| Release R5 | 95980 / 0 | 22,596 | 277 | 1,628 | `D8419F88090F51E334C32EC24CF02A66C5DE1FEE86D7F5EE116B49420EAC5F5D` |
| Real ASan R2 | 22060 / 0 | 22,596 | 277 instrumented | 1,634 | `8A5680697BD40403FC7574B2F2EAB5E6EBAF2D30E0C99F47B938F08CD3AFEE9E` |

ASan R2 recompiles the two changed TUs and proves unchanged source/dependency/object identity for 275
instrumented predecessors. Its own actual UAF probe is PID 37336, exit 1, with `heap-use-after-free`.
No fresh-277-build claim is made. Both profiles independently verify source/dependency/product bookends,
complete archives, exact compiler sets, full PE/PDB identity, zero ISA violations and every ordered test
record. Source-only reader controls pass four positive and 50 negative mutations; they do not manufacture
runtime evidence. Main reviews the 30 delegated cleanup facts and proves the old 45 authority predicates
and labels unchanged. Main implements and reviews the production parent and 75 new parent facts.

| Profile | EXE SHA-256 | PDB SHA-256 | RSDS GUID / age |
| --- | --- | --- | --- |
| Release R5 | `629BB0467EC1E807525F9FCB09B9DCA6DC6C51A9CD4194DB429E93C20AF9D48B` | `281A4EA4D1C89DE1CDE7B77B805B8D2D4B3B23B869B6A42F19F501E5294BF90D` | `36F920DB-42F6-4E41-BBF5-F1D4793421F1` / 4 |
| Real ASan R2 | `8D43BE2BEC46A6AA48485180C1DAA26B5E61D66AAD395A00CA9FFA797BAA3C45` | `C3EC5C2D87F1340139BAD292BAC341AB5209FF8E7C06303D67E853E4BA8AF10D` | `14FA8106-7DAA-462A-B36B-1607B6FBF606` / 2 |

Local independent receipts are `.build/P1Drafts/S5PressRectangleRelease-r5-Main/main-qualified.json` and
`.build/P1Drafts/S5PressRectangleAsan-r2-Main/main-qualified.json`. Generated archives, products, failed
attempts and full PDBs remain local under `.build/`. Ten closed C: ISA duplicate directories now junction
to their byte-identical D: archives, recovering 3.3 GB with every file hash checked before and after.
The exact relocation preflight and activation journals are retained.

The current native integrated `29a8ac2` source separately passes all three ARM64 cross-builds; native ARM64
runtime is unavailable. This private CPU extension still requires combined native qualification.
