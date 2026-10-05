# P1 committed residency preparation and resource stage qualification — 2026-10-05

## Qualification checklist

- [x] Preserve qualified predecessor `1cd2b69` and every earlier actual failure receipt.
- [x] Primary-review committed-document admission arithmetic, held source deduplication and worker retirement.
- [x] Private Main CPU probe r5 passes all 43 controls; plan is noncopyable/nonmovable.
- [x] Primary-review inclusive fixed-storage stage capture, benchmark boundary reporting and real ETL payloads.
- [x] Private WARP/ETL r4 passes 23 composition and 31 trace controls (26 existing plus five additions).
- [x] Register plan application/DocumentTests sources and new Document/Composition entrypoints exactly once.
- [x] Full identical-source Debug, Release, real ASan Debug, Release analysis and three ARM64 cross-builds.
- [x] Preserve all 805-input predecessor controls, application routes/stages and separate ASan stress controls.
- [ ] Verify archive copies, commit and push the qualified dormant step.
- [ ] Complete actual device resources, live admission/publication/retirement and largest-fixture G1.

**Status: registered dormant matrix qualified; commit/push pending. Native activation and P1 exit remain open.**

## Scope and reference

`RasterResidencyAdmission::Estimate` reads sealed committed-root bounds in O(1), checks dimensions/layer limits,
policy and overflow, and reports separate CPU/GPU totals including declared staging/driver allowances and four
live/retired overlap domains. It captures no owner; callers must reserve both totals and retain current authority
before immediately capturing immutable sources. `Prepare` runs on a worker, retains its root and deduplicated
raster layouts, checks actual vector capacities, never materializes pixels and supports cancellation. Its intrusive
retirement handle requires the live admitted release sink to remain installed until drain; inline fallback belongs
to worker construction/hidden shutdown. This primitive excludes preview, brush, utility, actual device allocation,
mips and publication. It does not claim complete live graph admission.

Six timing stages retain fixed histograms and an optional owner-only ETW event: texture/view creation, mip recording,
raster upload, composition recording and chrome sync. Times are inclusive CPU wall durations, not GPU completion.
Benchmark UI RESOURCE rows preserve initial-display/warmup/recorded boundaries and sample counts; missing samples
remain absence. ChromeSync <=4000 us is suppressed only in ETW, while histogram capture retains every call.

Independent CPU arithmetic/state controls and real WARP pixel/cache/settled checks accompany actual private ETL
payload validation. Existing numeric goldens and original timing ids remain unchanged.

## Private evidence and retained failures

Private stage attempts r1/r2 failed in the driver before a child existed (incorrect nonexistent CanvasViewport.cpp
assumption); their driver failure files remain. r3 actual child 77452 exited 2 on borrowed raw device/context `.get()`
compile errors; primary repair used the borrowed interfaces directly. r4 actual child 93248 exited 0, frozen inputs
389, freeze `60561A421F78B9F2DF0E7E36AE8F67E5109BC75DD76465D023EF8F5C23235E09`, stdout
`959C493393351839DC93062E70F6C96548B8209D28944937ADEFF42C714DC974`. Its selected source bytes match integration.

Private plan r1 exited 1 on an incorrect test mip literal. Primary review enumerated all 13 floor-halved levels,
corrected the independent sum to 59,679,180 bytes, strengthened indexing/cancellation labels, and added the
10,000-layer refusal control. r2/r3/r4 passes remain historical freezes; the current r5 includes deleted special
members and their static assertions. r5 Main actual child 89044 exited 0 at 15:05:28.497156 UTC; 382 inputs,
freeze `6BBDE52E13438B5157E0C40515F048C538FBDDFC4406240472FE0D4B6E65E704`, stdout
`8D0FB3C84B21E2562343A31E3B17C9FB10BF10992929997A005A0337BFDFAFDB`, 43 controls.

Private helpers and all attempt Sources/results/logs remain under Native `.build`; borrowed pinned Debug
libraries and generated shaders do not replace normal registries, static analysis, sanitizer or application gates.

## Required closeout

All predecessor literals retain exact multiplicity. New additions are 43 Document, 23 Composition and five Host
trace controls; ASan's separate Host stress registry adds the same five. The 9,744 named application controls
and 79 ordered stages/routes remain exact. Actual normal child exits and source/hash/archive receipts are required.
Native ARM64 runtime and R2/R3 machines remain unavailable; cross compilation is explicitly narrower evidence.

## Registered identical-source qualification

All 811 frozen inputs remain byte-identical through the last child exit; source-freeze SHA256
`1C243ED8FD920E9B61702163C879002A4302C16D9FBF3A41893588D1621AB754`. Fifteen skills, 691 formatted files and `git diff --check` pass.

| Actual attempt | Child PID | UTC interval | Exit | Verified archive completion SHA256 |
| --- | --- | --- | --- | --- |
| Debug full r135 | 63828 | 2026-10-05T15:07:08.097105+00:00 → 2026-10-05T15:17:42.674425+00:00 | 0 | `9ACD7353DD28205D5396BFC9BF862927D4C8D4D992A4B978EAD8EE8CDD6DC356` |
| Release full r137 | 78828 | 2026-10-05T15:21:39.660689+00:00 → 2026-10-05T15:27:31.663318+00:00 | 0 | `D2BA5D8A50795361DEC0CD6D846D7168FEDC67F8685D75D6DB82319410FA0E88` |
| ASanDebug full r138 | 97256 | 2026-10-05T15:30:07.233522+00:00 → 2026-10-05T15:41:14.567476+00:00 | 0 | `1B65368BDF68FF656D2F25CCDD4FE6702C9BA20CDC26C1D8C8303D6E098234D4` |
| Release analysis r136 | 25168 | 2026-10-05T15:08:54.611343+00:00 → 2026-10-05T15:16:06.125242+00:00 | 0 | `17C9AAB76F54CA348A3CBE9CE5AD1D25BE6F34104229185C9CCD700BDCE4A9BC` |
| ARM64 Debug r10 cross | 2436 | 2026-10-05T15:07:45.210004+00:00 → 2026-10-05T15:08:22.231974+00:00 | 0 | `B2C2534325F1CE4F5FBB789B592229367F0D4F4343406A59DE7A8666F8CECB58` |
| ARM64 Release r10 cross | 72832 | 2026-10-05T15:08:25.638366+00:00 → 2026-10-05T15:10:03.951989+00:00 | 0 | `C10F315CC4C19026D4CB5EA0AF613B59C8D3C6C049A9275B5471D2F978CFB88E` |
| ARM64 ASan Debug r10 cross | 31144 | 2026-10-05T15:10:13.386275+00:00 → 2026-10-05T15:11:05.642535+00:00 | 0 | `2EBE705E7125A8892B969A8B4504FE795088BE248E513A05976CE956652EBE0D` |

Debug/Release: 7,038 → 7,081 Document (+43), 7,519 → 7,524 Host (+5),
1,219 → 1,242 Composition (+23). ASan's main Host registry is 7,520 → 7,525;
its independent worker-stress Host registry is 7,917 → 7,922 (+5). Every preceding literal and
multiplicity remains. Application stays at 9,744 named controls / 79 ordered stages, separately
compared by output route. The helper binds both private proofs to the selected normal source bytes.

The new comparison helper initially omitted its validated private completion timestamp in its return
object and failed with `Completed`. Primary correction returned the already-validated timestamps;
no source/control gate was removed. The first Debug archive remains historical, and a fresh archive
including successful strict proof r2 was independently copied/verified. Release/ASan use that same helper.

Real sanitizer detection child 96168 reports the intentional heap-use-after-free at
`Common/AddressSanitizerProbe.h:12`; unique diagnostic SHA256
`A779BB110DD6AA141E274A7DC662B64066E3E53F153CA427A1E44052D27C2616` lies within
the actual r138 interval and is independently retained under the ASan archive's `Supplement/ASan`.
All archive source/product/PDB/scanner/provenance/log/screenshot copies are independently verified.
This closes the dormant plan/stage matrix, not live graph admission, resource creation/consumer
activation, first-display G1 or P1. Native ARM64 runtime remains unavailable.
