# P1 caller qualification after resume

## Checklist

- [x] Resume from the pushed `833ae76` checkpoint and verify all 877 actual source inputs are unchanged.
- [x] Preserve the earlier user-stopped Release analysis r282 as an interrupted result.
- [x] Complete fresh Release analysis r283 alone with actual `/MP2` and `/analyze` compiler responses.
- [x] Independently verify exact tested source, fresh linked images/PDBs, instruction scans and compiler options.
- [x] Archive and independently read back all analysis source/product/evidence files.
- [x] Recheck all 15 repository skills and formatting of the unchanged 750 C++ files.
- [x] Commit/push this qualified caller closeout and update the parent opening checklist.
- [ ] Integrate and qualify history startup, final stroke coverage/healing and typed GPU readback.
- [ ] Complete the actual asynchronous stroke caller, other remaining P1 requirements and measured G1 gates.
- [ ] Move P1 to Done only after its complete exit criteria hold.

Status: **877-input caller slice qualified; P1 remains WIP.** Parent:
[P1](../Plans/WIP/UiNeverBlocks_2026-09-22.md).

## Exact boundary

No production source changed between the saved checkpoint and analysis r283. The exact freeze is
`14178E65857A11148E49C3D1FD548DDC39298FF2BBAFF9FFDC55244656AAABDF` (877 inputs).
It is the source already qualified by full x64 Debug r278, Release r280 and real ASan Debug r281,
plus Debug/Release/ASan Debug ARM64 r40 cross-builds. Those runtime and cross-build archives remain
independently verified; this analysis does not replace their runtime evidence.

The strict runtime inventory preserves every predecessor and verifies 10,394 named application controls
across 98 stages, including all 80 Clone/folder, 48 clipboard and 108 file session-end additions.
Host/Document/Composition/I/O control counts are 7,675 / 7,225 / 1,694 / 1,736; ASan has the existing
extra Host marker (7,676), the actual sanitizer diagnostic probe, one raster stress control and 8,073
worker stress controls. ARM64 PE/PDB checks establish cross-compilation only.

The three caller records preserve the detailed expectations and every preceding failure:
[Clone/folder](P1CloneLayerSource_2026-10-06.md),
[clipboard session end](P1ClipboardSessionEnd_2026-10-06.md),
[Save/export session end](P1FileSessionEnd_2026-10-06.md).

## Fresh Release analysis

Canonical command: `build.ps1 -Platform x64 -Configuration Release -MaxCpuCount 2 -Analyze`,
with `CL_MPCount=2`. Actual child PID `55084` ran
`2026-10-07T06:09:10.959236Z`–`06:31:04.052839Z`, exit 0.
Two independently copied descendant compiler response files contain both actual `/MP2` and `/analyze`.

Primary verifier `.build/P1Drafts/SessionEndWrites/verify-analysis-r3.py`, SHA-256
`D0F4D3325A113CCAA44E59F8D527D39CDE626898A66F234E372B5EA96CAE1778`, requires the exact
real-ASan-tested freeze, actual successful analysis command/exit, fresh five-image reports, matching
PE/PDB/allowlist hashes, zero ISA violations, all 119 compact-switch controls and both actual compiler
option captures. It passes and writes `analysis-independent-verification.json` in the attempt directory.
No application runtime or G1 timing is inferred from analysis.

| Image | Fresh binary SHA-256 |
| --- | --- |
| RedPrism | `BE6169022018E77104A0DF8BB2F1DE2E9732D732906B85B6122301985E475950` |
| HostTests | `8198FD2808BEDAB302C03A7837CDD1B3C82C803F56BF0A0C5B8C7D0078D35EF2` |
| DocumentTests | `2C4C0AA8332B0AD960C2A5D49303CB0EFB712BB69E4B2FB56F91BCFBF5537954` |
| CompositionTests | `79958947CB04D1B0AF5A4D32F73769AD9F66BD334CCEB8FF9121E84D63E8C0A0` |
| IoTests | `2DB5D7CC7C7F5C22A0514FF00427DDEBDF2131B45E0672A428475DF55598DBA7` |

Archive: `D:\RedPrismQualificationArchives\P1-file-session-877-Analysis283-2026-10-07`.
All 1,180 copies verify at `2026-10-07T06:33:04.6367470Z`; completion SHA-256
`E479807856389ACB453C2643B5C47AA512593DE0DC99A12B578E38F677BDFC51`.

## Continuing work

The private stroke/readback/healing and normal-history-startup drafts remain unintegrated at this closeout.
Primary review has added real final-status assertions to mixed GPU buffer/texture reuse, rejected an
oversized healing patch before pixel allocation, and prepared a typed stroke input/publication that binds
the exact opening, immutable source root and captured history. A cheaper agent supplied constant-surface
healing handoff tests; publication tests are being reviewed separately. These drafts are not compilation,
host activation or mouse-up latency evidence. The original managed/native checkout boundary is unchanged,
and no DxUi source or pin changed during this resume.
