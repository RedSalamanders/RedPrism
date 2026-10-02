# Prepared active-layer New wand selection

Date: 2026-10-02.

## Scope

This record qualifies the S4b core preparation and publication API, with active-layer sampling and New mode.
It preserves captured point/settings/AA, complete document/history publication, no-match Deselect, true no-op
and redo behavior, nested transactions, saved markers and off-owner aggregate retirement. It does not qualify
production host routing, scheduler admission, live selection gestures, All Layers, Add/Subtract, Clipper bounds,
G1 latency, RAW or Photoshop comparisons. Existing legacy numeric goldens are unchanged.

## Implementation and declared memory

Owner capture owns the before-document, immutable history view and settled-session authority. The worker owns
sampling, trace, the after-document, complete history publication and replaced owners. Publication validates
document/session/generation/history/active/mask/crop authority before nonthrowing swaps. Mask, visibility and
opacity do not affect active-layer sampling. Blank/folder/no-active sources use the literal full-canvas rectangle
without allocating a document-sized image or mask. Cancellation publishes no partial selection.

For N = width x height, V = (width + 1) x (height + 1), and E = min(4N, 8,000,000), the planner accounts sampled
pixel buffers, sparse source copies, N mask bytes, V vertex bytes, scanline seeds, coordinate/loop growth,
document/history preparation and the full retirement graph. The sampled source and sparse copy remain charged
in later phases, including a copy retained by the pixel recycle pool. A flood run consumes disjoint selected
pixels; at most 2N neighbor visits plus the initial seed bound the stack. Loop count is bounded by ceil(E/3).
Pinned vector growth allows 3x old/new capacity overlap. This is a declared graph bound, with separate opaque
process/allocator headroom; it is not process commit, RSS, constant-time H2/H3 or a cancellation deadline.

The installed MSVC 14.51.36231 vector/xmemory/yvals snapshots and their receipt are frozen with the private
attempt. The pinned STL revision is 202604, version 145. Requalify the growth proof when this toolset changes.

## Private attempts

| Attempt | Retained outcome | Exact inputs / evidence | Archive completion UTC |
| --- | --- | ---: | --- |
| r1 | Disk full before compilation (MSB3191), no executable qualification | 296 / 5 | `2026-10-02T18:24:07.2474916+00:00` |
| r2 | Full Debug DocumentTests, 116 unique new controls; before sparse recycle carry refinement | 296 / 136 | `2026-10-02T18:29:28.1092005+00:00` |
| r3 | Full Debug DocumentTests, 117 unique new controls; before observation-owner retirement refinement | 296 / 136 | `2026-10-02T18:37:53.8467267+00:00` |
| r4 | Final full Debug DocumentTests, 117 unique new controls | 296 / 136 | `2026-10-02T18:53:09.4856715+00:00` |

The r4 build started at `2026-10-02T18:49:36.5862475+00:00` and completed at
`2026-10-02T18:49:46.6105638+00:00`, exit 0. Its normal full DocumentTests run started at
`2026-10-02T18:52:11.8499062+00:00` and completed at `2026-10-02T18:52:41.1545240+00:00`, exit 0.
The archived stdout contains exactly 117 unique `wand-layer` checks, 54 `wand-cancel` checks and 17 `wand-chunk`
checks, all corresponding summaries and the full DocumentTests completion marker, with no FAIL line.

The 117 checks include literal clockwise geometry, disconnected component ordering, sparse/blank/group sources,
captured settings/AA, no match/no-op/history titles, nested transactions, saved markers, moved/foreign/single-use
and reached stale authority refusals. Cancellation is reached at every preparation milestone and all six history
stages. Injected bad_alloc observers prove all six history failure stages and fresh recovery; they do not simulate
OS allocator faults. Capacity tests cover maximum canvas planning without allocating its pixels. A gated real
worker proves apply and stale-drop aggregate disposal on the worker after UI observation snapshots and the
session's old roots are released. This handoff does not prove application close or the production scheduler.

## Qualification environment

The first failure is archived before any source edit or retry. The exact generated native intermediates and old
private products were copied and hash-verified on the local disk before removing their original generated paths.
The normal repository `.build/Intermediate` path is a junction to a local `.build` cache; output executable paths
and the running-app build guard are unchanged. These runs are functional qualification, without timing claims.
An older clipboard qualification archive was also relocated recoverably: all 783 files were hash-verified and
its original path retained by a junction. No source or qualified evidence was discarded.

## Native qualification

Native r1 completes at `2026-10-02T19:17:39.9387342Z`, exit 1. All ARM64 cross-builds and full x64 ASan pass;
x64 Release stops at linked ISA scanning before runtime, and x64 Debug does not run. Exact 595 inputs and
297 evidence files retain the failed Release image/PDB, object/compiler tracking and ISA inputs before repair.
Archive completion: `2026-10-02T19:22:16.5674449+00:00`.

The rejected `Adjust.obj` row at `0x140256839` is DWORD switch-table data, after RET and alignment, in RunFilter.
The scanner now recognizes the exact RDI-index/RDX-base/EAX-target dispatch using its existing PDB data-start,
same-procedure/decoded-target and table-boundary proof. It adds no new runtime allowlist or guard-dominance claim.
On the unchanged failed image and PDB, 678,392 decoded instructions and 167 runtime vector instructions remain
unchanged: the single apparent EVEX becomes one verified inline table-data row. Two undecoded data prefixes
remain unchanged. Twenty-three controls prove refusal of malformed dispatch/PDB/targets and adjacent real
VEX/EVEX instructions. A first synthetic attempt failed because its missing-PDB fixture did not reach its
intended mutation; original source/fixtures/logs are archived before correcting that fixture and passing r2.

The final strict proof archives four declared inputs and 4,668 copies at `2026-10-02T19:39:44.7149169+00:00`.
Image SHA-256: `E91D5C86EA63913F7E9B433355C9048C4458894D259FE669CA1C43DC712D40B3`.
PDB SHA-256: `F02137CE1D554801C7F7191DF947A7EDD783A799983A9A13E1677EE24532350D`.
Scanner SHA-256: `DA07F8A0CE79F5B73EF66475C7C74041E39077B56DBB44BCD4A7AE392B999A04`.
Unchanged allowlist SHA-256: `6F55A40AB0CE23E98BB10839EC68765531129EF375B00DD29B298C3DC97EE560`.
Proof manifest SHA-256: `B0B560BAEB2958EA725673964A7DFB72CF328A1C242549310423E22685C4B974`.

Combined native r2 qualifies S4b and [S2a](SelectionPreservation_2026-10-02.md) against 598 unchanged inputs.
Skills and formatting pass; all x64 full suites pass the exact 117 wand, 136 preservation, 71 cancellation/chunk
and 23 ISA names, unchanged document goldens, WARP and harness gates. Actual ASan detection passes. ARM64 is
cross-build only; the native checks do not qualify host routing or G1.

| Profile | Actual serial completion UTC | Qualification |
| --- | --- | --- |
| ARM64 ASan Debug | `2026-10-02T19:44:26.1939491Z` | Cross-build |
| ARM64 Release | `2026-10-02T19:47:20.6723607Z` | Cross-build |
| ARM64 Debug | `2026-10-02T19:49:26.5869654Z` | Cross-build |
| x64 ASan Debug | `2026-10-02T20:01:16.2711664Z` | Full test suite and actual sanitizer probe |
| x64 Release | `2026-10-02T20:06:13.0430556Z` | Full test suite |
| x64 Debug | `2026-10-02T20:13:23.2744462Z` | Full test suite |

Matrix process completion is `2026-10-02T20:13:23.3037530Z`, exit 0. Independent verification completes at
`2026-10-02T20:15:19.1295699+00:00`; SHA-verified archival completes at `2026-10-02T20:20:56.8817217+00:00`.
Final native archive: `C:\Users\eric\AppData\Local\RedPrism\QualificationArchives\p1-wand-layer-selection-native-qualified-r2`.
It retains 598 frozen inputs and 7,223 evidence copies, including all six-profile products, ISA reports, named
harness artifacts, private proofs and original failure evidence. Source manifest SHA-256:
`5C9ADF904C61A5DB52F470E5CC35689687AAFF27D7CB04E1F56932D659513339`.
Evidence manifest SHA-256: `9AE1C719F94DBDD2BF5D36A6B21A3EF8A96957DB43CF37E17B3ED90387E65D52`.

Two independent helper assumptions were corrected before finalization: an older manifest omits an explicit
Length field, and the archival source-skip predicate needed normalized path separators. Original helper bytes,
diagnostics and the partial copy attempt remain retained. Neither changed native inputs, products or test results.

## Private archive provenance

Final archive: `C:\Users\eric\AppData\Local\RedPrism\QualificationArchives\p1-wand-layer-selection-private-r4-Debug-qualified`.
Image SHA-256: `C67CD86262B60521D7E4BA2781E33F7725C05519222731DCB5EC07A5F5C6153B`.
PDB SHA-256: `78999E318ADF4AD09FDA147ABDAD2B705C623B71F77E8796111824C110C8666E`.
Source manifest SHA-256: `E44618540D64263EC7F9A3C5738803BBE221D5CCCC216743450C4C7471C6777A`.
Evidence manifest SHA-256: `788AE5D0A085A7E79D276B8B209FCF3366DDFFAA6294E81F4BC982BCF29CA5DA`.
Every archived source/evidence copy, manifest hash/count and product hash was verified before using this proof
for native activation. Earlier attempts retain their exact inputs, output images when produced, receipts and logs.
