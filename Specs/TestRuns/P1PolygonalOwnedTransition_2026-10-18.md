# P1 owned selection predecessor transition — 2026-10-18

Status: fresh complete CPU Release and real-ASan qualification, independently verified. P1 remains WIP.

## Problem and contract

A physical Down can be accepted before the older original parent's exact containment or selection publication. It must retain its real generation/restore/target metadata. After an owned inside move or Polygonal close publishes, the next original parent needs proof that this exact ordered selection-only transition accounts for the observed generation change. An arbitrary generation replacement would also accept foreign edits.

`SelectionOutlineGestureTransition` has a private constructor usable only by the original job. It owns fixed scalar metadata: journal identity/family, selected terminal ordinal, minimum owned generation, exact post-publication document read authority and opaque editor/history authority. It owns no document/history/raster/UI/GPU roots. Copy/default construction and move assignment are deleted; moving invalidates the source. A completed original parent's `TakeSuccessor` yields its private receipt once, after controller clearance and parent acknowledgement. Failed, cancelled and unclosed parents yield none.

The next journal parent validates and consumes this receipt after its complete admission and before retaining any geometry/history roots. Validate journal/family, later Down ordinal, same session/document/restore/dimensions, captured generation inside the proven contiguous owned range, current exact post-document/editor/history authority and current captured target. Reject receipts on the legacy press protocol. Preserve every stored event byte. A chain carries its proven minimum plus the latest post-publication authority, so a third earlier-accepted Down can follow two successful owned parents without a root list or traversal. An intermediate owned-preview generation is valid only within this exact unbroken lineage. Generation wrap cannot grant credit.

Successful drawing closes and inside outline moves both issue receipts. A foreign history Begin without a generation change invalidates the opaque history authority; target/crop/document changes refuse too. Family retirement/reuse and moved-from values cannot authorize another parent. This is CPU authority plumbing; the physical caller still must reserve/order conditional receipts and arbitrate Save/tool/key/lifetime work.

## Required proof

The existing 105 dispatcher assertions remain, and ten scenarios add 34 independent assertions (139 total). Whole suite expectation: 23,594 PASS lines. Retain all 23,559 matching-profile predecessor assertion records, excluding only the replaced old module summary.

- Three queued inside original parents: original Down authority remains byte-identical for the second and third. Literal rectangles are (1,1)-(5,5), then (4,3)-(8,7), then (6,4)-(10,8), then (5,3)-(9,7). Each parent owns its complete admission/ticket; three entries, six existing Move/End generation invalidations, exact three Undo/Redo results and final refunds.
- Chosen Polygonal close followed by already accepted next Down: latter hit uses the owned committed polygon, with exact later inside movement and two history entries.
- Down accepted during an owned move preview captures the genuine intermediate generation; its receipt validates without event rewriting.
- Missing receipt, wrong journal, retired/reused family, foreign history/crop/target and moved-from receipt refuse before source capture and preserve the completed predecessor's selection/history. The single valid moved receipt can then consume the still-untouched Down.
- Static traits enforce no public default/copy construction and no-throw move construction. The original 24 scenarios, full predecessor suite, real instrumentation/UAF, ISA, source/artifact/PE-PDB and lifetime gates must still pass.

## Receipts

`D:\RedPrismBuildCaches\.build\p1-polygonal-transition-cpu-r1`: two fresh 285-TU profiles, frozen source identity, complete logs/products/manifests and independent reader. No cache/object or runtime reuse qualifies this component. Both profiles pass independent verification; the five qualified source files are adopted byte-for-byte.


## Independently verified results

Each profile has 285 fresh compiler/source/object records, all 1,498 source bookends, all matching-profile predecessor records, the 139-assertion module and 23,594 total PASS records. Actual build/runtime/ISA and independent reader exits are zero. ASan carries `/fsanitize=address`, debug CRT and the forced instrumentation header in every TU; its UAF probe exits 1 and diagnoses heap-use-after-free.

| Profile | Archived files | Manifest SHA256 | Matching PE/PDB GUID | Age |
| --- | ---: | --- | --- | ---: |
| Release | 316 | `B854F75092D5C5B08B800E028B261D7673FF42F9FFA14538E12B8F0DD03E4289` | `F96561E2-AA1F-4394-84E5-D43096BC2FF7` | 1 |
| ASan Debug | 323 | `1C21F8C23717723BB92909C611264B4D6148E409FEBB89D493484B71668C0A7D` | `3296B1B2-BE66-4B82-92BE-CE6BCB05D195` | 1 |

Both images are native x64 machine 8664. Full hashes:

- Release EXE: `5EDBEDD8E05E3152E747A9C8E999F56A348DD38BC114D44A161AC89FAD8728A4`; full PDB: `79C87D613D3A3E0F815F0AEF108033942BBBB0DFEA606CD211238790AD484889`.
- ASan Debug EXE: `70BB84DEE9DA5EEA878224BEF920A875F2B92BADA1598F8D19DBD616C5DE06EB`; full PDB: `6DB32A7F8C19E7F5C896699299FD870481F43B222F93F47B600DB00EFFD21E7E`.

Main implements and independently reviews the important authority path. The separate cheaper documentation agent reconciles only one proven historical row; Main verifies its exact TestRuns evidence and ancestor commit, preserves the context-free patch refusal and applies one exact text replacement. No ordinary activation, host dispatcher or G1 gate is marked complete. The preceding component's original failures remain in [its evidence](P1PolygonalGestureDispatcher_2026-10-18.md).


After adoption, all 15 skills validate, formatting checks all 1,309 files and `git diff --check` passes. No ordinary activation, DxUi/pin, ISA allowlist or legacy golden changes.
