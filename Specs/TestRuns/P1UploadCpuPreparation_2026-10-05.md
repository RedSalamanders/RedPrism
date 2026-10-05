# P1 import-residency and raster-upload qualification — 2026-10-05

Status: original managed residency/row-copy/transport foundations qualified and pushed as `74c327d`.
Later private proxy/layout, GPU faults and terminal-retirement drafts remain unqualified for activation.
The earlier sections below retain their historical scope; the complete managed matrix and commit are recorded
at the end. Native activation, G1 and the P1 phase exit remain open.

## Initially reviewed private work

Primary implementation adds history permit/refresh forwarding for replacement publications and prepared image
imports, with explicit move/no-op/single-use authority. The original pixel candidate and saved baseline survive
residency refresh. Literal tests cover cache growth from a four-byte leaf to leaf plus eight-byte assembly,
trim, allocation/cancellation refusal, unchanged live document/generation/revision, original Undo/Redo and
replacement saved-baseline behavior. No host import/mask/copy/shape job opts into this protocol yet.

The cheaper agent drafted the worker row-copy helper and independent pitched-row tests; primary reviewed both
and corrected one BGRA padding expectation to match the initialized destination guard. The helper uses
ReadRegion, validates before writing, never materializes sparse sources and checks cancellation per row.
Tests cover literal Gray8/BGRA, offsets, base/tile/fill, pitch/padding/guards, exact and short capacity,
invalid geometry/pointers/stride, observer order, pre/mid/final-row cancellation and sparse residency.

The registered DocumentTests project/entrypoint changes and mask/copy/shape forwarding headers are subsequent
private drafts. They are not included in these earlier linked harness freezes. Normal integration and the
required complete Debug/Release/ASan/analysis/ARM64 gates remain mandatory. GPU queue, resource creation,
display/utility integration and G1 are open under the [H4 plan](../Plans/WIP/BoundedRasterUploads_2026-10-05.md).

## Actual probes

Strict MSVC Debug code generation uses /W4 /WX, /permissive-, /std:c++latest, /utf-8 and the Windows SDK.
The linked CPU harness rebuilds all Common modules from the private DocumentTests project, includes the complete
import-publication suite and row-copy cases, and routes failures away from dialogs. It borrows the pinned Main
dependency include/library/runtime inputs. It is a diagnostic executable, not an alternative normal build or
ISA/PDB/provenance gate. Generated products and source/log snapshots stay under ignored `.build` folders.

| Probe | Actual child / exit | UTC interval | Inputs / freeze | Actual PASS counters |
| --- | --- | --- | --- | --- |
| `private-import-upload-codegen-r1-20261005` | 90200 / 0 | 2026-10-05T09:46:49.330428+00:00 to 2026-10-05T09:46:55.712729+00:00 | 313 / `A160E92EAAD47E89924414E4087A6739DF64947F1FB0BC7D23A5B102370796E5` | import 0; rows 0 |
| `private-import-upload-runtime-r1-20261005` | 82168 / 0 | 2026-10-05T09:48:41.653715+00:00 to 2026-10-05T09:49:33.584264+00:00 | 361 / `145615A7C0A9A7FD719B5A50893027E384841B2F94776FA40BD263BAFB918CB0` | import 190; rows 71 |
| `private-import-upload-runtime-r2-20261005` | 75988 / 0 | 2026-10-05T09:52:07.548848+00:00 to 2026-10-05T09:55:04.987749+00:00 | 361 / `2E9CCADD048ACD41302114F8F2E5055A549E3694DA29E279AD62113823D9E25B` | import 196; rows 71 |

The r2 harness adds late-cache replacement and saved New Canvas checks. Original import controls stay present;
the added API creates no new revision while refreshing. Stdout, stderr, exact source copies, compile commands,
objects, diagnostic executable and PDB are retained in each named local probe folder. The source/hash receipts
record actual child exits; these limited probes establish no native activation, GPU responsiveness, sanitizer
qualification, complete H3 accounting or first-display/G1 performance claim.


## Subsequent primary import host and reviewed BGRA controls

The private r3 linked CPU harness passes 201 import controls and 71 row controls. It includes borrowed-permit
replacement moves and moved/applied New Canvas refusal, preserving the earlier controls. The cheaper agent's
read-only review found one useful gap: padded sparse BGRA needed its own explicit nonmaterialization and epoch
checks. Primary accepted and reviewed its two-label patch. r4 passes 201 import / 73 row controls and preserves
every r3 literal control/multiplicity. Both new labels are `raster-upload.sparse-bgra-source-not-materialized` and
`raster-upload.sparse-bgra-residency-epoch-unchanged`. The agent is closed.

Primary separately drafted the production import job's same-ticket residency decision/refresh, original-plus-refresh
history reservation and permit release before every chrome/replacement callback. A separate actual-host sidecar
adds existing insertion, multiple-image replacement and single-image saved-baseline cases, each with success,
refresh allocation failure and cancellation. It witnesses exact ticket equality, one capture/decode, retained
reservation, 4-to-12 byte sparse growth, unchanged failed intent, saved Undo/Redo lineage and off-owner retirement.
It is not yet integrated or runtime-qualified. Private strict host codegen r1 caught an incorrect fixture call
to MakeSparse; primary corrected its offsets/vector arguments. r2 compiles all four intended TUs. Normal matrix,
actual-host controls, GPU queue/resource/composition and G1 remain mandatory before activation or completion.

| Probe | Actual child / exit | UTC interval | Inputs / freeze | Stdout SHA-256 |
| --- | --- | --- | --- | --- |
| `private-import-upload-runtime-r3-20261005` | 90260 / 0 | 2026-10-05T10:03:59.369461+00:00 to 2026-10-05T10:04:51.556338+00:00 | 361 / `CC559B89E5C45F57288F8495F9847529CAAEAA10E7E379CE6379CA1E839F0407` | `03EBBFF05926BBDD0CC3B073EBAEB4CACE9F2123AE813891A2DA44038B247FE7` |
| `private-import-upload-runtime-r4-20261005` | 45704 / 0 | 2026-10-05T10:25:03.031827+00:00 to 2026-10-05T10:25:55.778368+00:00 | 361 / `18D5CE0561B21345B50DE526F6D08FFA6711D0442DA1C16F53FD01C6359B1C71` | `8DCAC827BF67FDD8D96B097291DF8E5B0A2EC5F19CF5B43EF66134A7855C5110` |
| `private-import-host-codegen-r1-20261005` | 88916 / 2 | 2026-10-05T10:23:20.985489+00:00 to 2026-10-05T10:23:31.041811+00:00 | 312 / `F9F93FCE36F38D340D7CAB314268CD9C5CA9699BA458139394F102F2C3D27C12` | `9842ED2CE5C1B91D63EFD24DE4EC85C48B74394266B648F48B323C283418D1BD` |
| `private-import-host-codegen-r2-20261005` | 8776 / 0 | 2026-10-05T10:24:52.420612+00:00 to 2026-10-05T10:25:03.508181+00:00 | 312 / `3CE9AFFBC9DA1E5FD65126E5A1701205AD29BC85C1A869C0E85569D6FD87CE2D` | `6DCE5546A3AAC39D535D1C49AF83C20006E31B80DB74A0633A7019D578992211` |

### Image publication wrappers: private CPU and host code generation

Primary drafted checked additional-history overlap, same-ticket permit/refresh and permit release before callbacks
for mask, Layer via Copy/Duplicate and shape host jobs. Their original pixel/mask/shape candidate remains owned;
refresh never reruns coverage or pixel construction. A separate actual-host sidecar covers reveal/hide masks,
selected/unselected layer copy and rectangle shape, each with success, refresh allocation refusal and cancel.
It preserves the preceding host summary/stage registry. These actual host additions are not runtime-qualified yet.

The cheaper agent added five LayerCopyWork controls. Primary review found its apply-after-move lacked the mandatory
fresh permit. Primary corrected the test to prove refusal before a fresh decision, then successful reacquisition,
single publication and explicit release before Undo/Redo. The agent is closed. The r5 linked CPU diagnostic passes
201 import, 73 row and 39 layer-copy controls,
with all r4 literal controls/multiplicities intact. This test specifically proves entry-limit trim with a live
sparse duplicate; actual byte-budget wrapper scenarios remain in the host sidecar. Do not infer unique history
byte-budget or G1 qualification from this entry-cap test.

| Probe | Actual child / exit | UTC interval | Inputs / freeze | Stdout SHA-256 |
| --- | --- | --- | --- | --- |
| `private-import-host-codegen-r3-20261005` | 43388 / 0 | 2026-10-05T10:38:57.128577+00:00 to 2026-10-05T10:39:34.588721+00:00 | 316 / `B5DE06EEF45800CF7AFE123D944FE663E04B92B5B4A73CFBF26735D23E7F935B` | `32F83C8D045BEA34C9C2B658E6031B3D60246A3549979BC8CA844338BC8B5A12` |
| `private-import-upload-runtime-r5-20261005` | 74380 / 0 | 2026-10-05T10:39:07.660536+00:00 to 2026-10-05T10:40:26.158869+00:00 | 362 / `E53DD8150E92089A16AA0252DF4B92789E370E25441E6BEE034CF50250DE71A4` | `DAFCA8E770336ED80376E749805A2433B5F7CD828E00ACC98DCE8F20FD0CB99B` |

These borrowed-dependency private diagnostics do not replace normal ISA/PDB/provenance, full runtime, analysis, ASan, ARM64 or G1 gates.

### Registered image-wrapper integration: first actual host failure retained

The primary integrated 30 reviewed paths over qualified checkpoint `2547f7e`, preserving the no-growth history
index fast path and newer continuation diagnostics. Normal Debug build r111 passes all five images, matching-PDB
ISA/scanner controls and the unchanged DxUi pin. The actual application-only child r112 exits 5 at the new import
residency fixture's Undo/Redo assertions. No production pixel/history assertion was rebaselined. Its 68 reached
stages, exact source/products and failed output are independently archived before repair. Analysis r113 passes
for that earlier source; it does not qualify the subsequent fixture repair.

The new fixture originally gave history an eight-byte cap, but Undo retains two distinct imported 2x1 BGRA rasters
(sixteen bytes). The existing oldest-first trim therefore correctly drops Redo. The repaired fixture fixes the cap
at sixteen bytes throughout: its older 4x1 sparse root begins with only a four-byte leaf, then grows to sixteen
root bytes plus the four-byte leaf (twenty). That growth still forces trimming of the older entry while the
original import's sixteen-byte Redo fits. Independent CPU twelve-byte cases are unchanged. Production history
limits, oldest-first policy, goldens, running-process guard, test tolerances and ISA allowlists remain unchanged.

Full Debug r114 and Release analysis r115 are now running on the repaired source. All three r5 ARM64 builds
cross-compile only. Normal Release/real ASan, full predecessor registry checks and final archive receipts remain
required. Skills validate 15 entries and formatting checks 661 files. No Native/H3/H4/G1/P1 completion is claimed.

| Attempt | Actual child / exit | UTC interval | Owned source freeze | Archive completion SHA-256 |
| --- | --- | --- | --- | --- |
| `incremental-x64-build-Debug-r111` | 67492 / 0 | 2026-10-05T11:03:58.917507+00:00 to 2026-10-05T11:09:09.617896+00:00 | `4919287C21649C78763F8CC57F918E15889D4B26124771D16A991B236D03254D` | `p1-image-wrapper-x64-Debug-r111-build-qualified` / `3A02107308F39B077D2BE7E59C977204739BBE6999E164E398951782B6DE2ACE` |
| `incremental-x64-runtime-Debug-r112` | 35484 / 5 | 2026-10-05T11:11:28.358769+00:00 to 2026-10-05T11:14:37.547104+00:00 | `4919287C21649C78763F8CC57F918E15889D4B26124771D16A991B236D03254D` | `p1-image-wrapper-x64-Debug-r112-import-undo-failure` / `8D8EE38142B9DE0292BF1A10C1317ECCB869227B98C0C0FE90EBC106AA531C6B` |
| `incremental-x64-analysis-Release-r113` | 11508 / 0 | 2026-10-05T11:11:42.157850+00:00 to 2026-10-05T11:18:56.354584+00:00 | `4919287C21649C78763F8CC57F918E15889D4B26124771D16A991B236D03254D` | `p1-image-wrapper-x64-Release-r113-analysis-before-fixture-repair` / `29A3B3008710492ED16020A477464D0734FAD0A071604328ED56694425163A2B` |

The next separate U2 draft is `GpuUploadWork.h/.cpp`: eight records, two lent/in-flight chunks, one transition per
owner pump, worker-only row copying, exact queue/slot generation acknowledgement, nonblocking WRITE Map and one
real completion-barrier retry, and retained mapped contexts through removal. A failed post-copy barrier quarantines
its staging class until notification producers join, preventing overwrite while the GPU could still read it.
The draft compiled strictly in a private one-TU probe. A cheaper agent added bounded admission/lease cases;
the primary owns the cropped BGRA/Gray literals, worker acknowledgement, event, cancellation and rebind cases.
Private link diagnostics uncovered missing harness scheduler/SettingsStore dependencies; those attempts are
retained. The queue is unregistered and not in the normal build or application. Whole resource/display admission,
creation, owner-latency, full fault/shutdown controls and the normal configuration matrix remain separate U2–U8 work.

### Upload transport registration and import fixture name repair

Private strict linked WARP transport probe r4 passes **165 literal controls**, including BGRA/Gray cropped pixels
and untouched destination guards, real event completion, worker-only row copying, both queue/slot generation
refusals, cancellation/failed fill suppression, lent-device replacement, one event-driven Map retry and persistent
Busy refusal. Two loans block further mapping without a ready spin; each Pump performs one transition. Eight
receivers complete once, cached staging reuses without granting an old lease, and an actual admitted scheduler
worker stays mapped through device retirement and shutdown until Close joins it. Notifications join before
Unmap/detach; repeated close invokes no late callback. The default route is capability-selected; forcing the real
EnqueueSetEvent route does not assert that this WARP adapter supports monitored fences.

Actual child 91552 / exit 0, UTC 2026-10-05T11:37:55.419855+00:00 to 2026-10-05T11:38:55.679001+00:00;
370 owned inputs, freeze `DF618430A19E74CF8BAD8E9CD03BD13EFBEE97DEE6BC8264943DA13967B7A4B7`, stdout `F8FD4955EC309DA63073D3FF802E2A7D584187D6E2F37C4EEC39F4EB4758FBE3`, driver `DCBC8A98A4600FC555A5C83A3A355480AF07C4483387EA38CE0D892529B161DF`.
These are private diagnostics with borrowed pinned dependencies, not normal analysis/ASan/ISA/provenance or G1
qualification. Earlier link refusals and r3's two invalid MSAA fixture expectations remain in their original roots.
The primary repaired the MSAA descriptor as a render target, keeping the requirement that the created multisample
resource must be rejected. The r2 dependency-only diagnostic's helper was changed while its child ran; its recorded
end-of-run DriverHash is not a bound launch-driver proof and is not used for qualification. Its frozen sources,
compile.cmd, exit and failed linker output remain inspectable.

Full Debug r114 passes the new CPU/upload rows but fails one new replacement-import Undo assertion at stage 68.
The source's import batch deliberately calls the one additional image **Import Image**; importing two images into
an existing document calls its entry **Import Images**. The new test had expected the plural in both cases.
The primary corrected that literal target-specific name, retaining saved-baseline, layer-count and Redo checks;
production behavior is unchanged. Its verified failure archive is
`p1-image-wrapper-x64-Debug-r114-replacement-undo-failure`, completion
`D1C942EAAE80B41FC64289A72D11762A035862E5A2D44A873004102B761D7E33`.
Analysis r115 and all three r5 cross-builds passed the earlier source and remain separately archived; they do not
qualify the new literal name or upload registration.

The primary then registered the reviewed queue, admission and transport tests in the normal managed projects,
preserving all previous registrations. The application compiles the queue but does not create/use it yet.
Debug build r116 is running on this combined source. Full actual app, Debug/Release/real ASan, current-source
analysis/cross-builds and strict predecessor comparisons remain open. Resource/display planning, fault injection,
first-upload activation and U2–U8/G1 exits remain open.

### Registered upload pass and single-image no-trim fixture repair

Registered Debug CompositionTests r117 passes on the combined 787-input source: actual child 83660,
UTC 2026-10-05T11:50:02.180726+00:00 to 2026-10-05T11:50:07.114140+00:00, exit 0, binary unchanged. All 746 predecessor
controls are present alongside 165 new upload controls (58 admission, 107 transport). Release analysis r119
and Debug/Release/ASan Debug ARM64 cross-builds r6 also exit 0 on freeze `3BFCB9E9125DD15BCE3BF56D189CF632A3196497E97E90B453609E0746A9E316`.
This is scoped queue/compilation evidence, with no application activation, native ARM64 runtime, whole-plan
allocation/mip/first-compose qualification or G1 claim.

Actual application runtime r118 (child 42388, UTC 2026-10-05T11:56:48.080692+00:00 to 2026-10-05T11:59:05.561092+00:00)
exits 5 at the single-image replacement capture/refresh expectation. Existing and multiple-image success,
refresh-allocation failure and refresh-cancellation cases pass. A single replacement intentionally has equal
saved baseline/final documents and no history entry: PrepareReplacement forwards PrepareEdit's no-op path,
which samples no retained-residency trim. Unrelated old-source cache growth must not invent a refresh or run
its failure/cancellation hooks. The primary corrected that new fixture to assert zero refreshes, no refresh
reservation/ticket witness, successful saved replacement and no error for all three irrelevant-hook settings.
Three explicit no-trim controls and the single-image success branches add six controls
to the new image-import group (118 total). Production behavior and predecessor expectations are unchanged.
The failure archive `p1-image-wrapper-upload-x64-Debug-r118-replacement-refresh-failure` independently verifies every copied source/product/evidence file;
completion hash `4BC96918B048198302DE017CEF4B330246120FF72E51DCDF6565D5B228994BEF`. New build r120 and analysis r123 must
qualify the corrected fixture; earlier passes do not qualify it.

Worker crop-grid reduction is a separate Caller draft, with primary-owned production code and cheaper-agent
literal/edge/scratch/cancellation tests under primary review. It fixes a crop-relative block grid so separately
uploaded windows cannot shift averaging at chunk seams, uses caller-lent row/uint64 accumulators without
allocation or sparse materialization, and preserves encoded/premultiplied byte rounding and clipped counts.
Its independent private/registered gates and subsequent application integration remain open.

### Corrected host registry and worker crop-grid reduction proof

Corrected actual Debug application r121 exits 0: child 69020, UTC 2026-10-05T12:13:46.588932+00:00 to
2026-10-05T12:17:00.389617+00:00, 787 inputs, freeze `80854C75F7BBFF291222904D9C24981A0CA9239D970CB55CB089458A2BF4001D`, binary unchanged. Independent strict
counter comparison preserves every preceding literal/multiplicity on its original stdout/stderr route and
all **79 ordered stages**; **9,744 named controls** include exactly **118 import** and **180 mask/copy/shape**
residency controls. The primary also declares every expected case label independently (nine import and fifteen
wrapper cases), matches them to the frozen test source, and requires the complete counter, rather than just
total/prefix membership. Proof hash `528E34E9762B5E1C030E63A3A0DCD64830AB8AA3999961D13774D8B81B21A357`. The old helper expected 112
import controls before the no-trim repair; the repaired single-image success branches and three no-refresh
assertions add six. No predecessor expectation changed. The r121 archive independently verifies all 1,048 copies.

Corrected Release analysis r123 and all three r7 ARM64 cross-builds pass on that same freeze. Full Debug r122 is
running; full Release/ASan and current registered combined closure remain required before commit/activation.
Native ARM64 execution and G1 remain unavailable/unqualified separately.

Private strict worker copy/reduction CPU child 84284 exits 0, UTC 2026-10-05T12:17:08.595557+00:00 to
2026-10-05T12:18:01.347873+00:00; 370 inputs, freeze `9B38B4E7D647B8A7281DCCADD1FEC6A0B2C3FEF5A31D544AD02402F075A32404`, driver `A16571A77CC44E3796A1C23B0977C6FCB1843515E6593CAD948F3943B5CEACAB`.
Its **73 predecessor copy controls and 41 new reduction controls** pass: independent encoded BGRA/Gray literals,
varied alpha, crop-relative chunk seams/right/bottom partial blocks, nonzero output window, sparse base/tile/fill,
exact capacity/pitch/scratch, unchanged guards and residency epoch, invalid geometry and valid INT_MAX scale,
real scale-2 precancel/mid/last-output-row cancellation and shutdown token. Initial cheaper-agent tests incorrectly
covered cancellation only via scale-1 copy; primary review required the corrected reduction cases and scratch
sentinels. Generated ProbeMain, transformed driver inputs and commands are bound. Borrowed pinned dependencies,
private-only scope: no normal matrix, owner timing or consumer activation is claimed.

The primary has drafted one-shot upload initialization/allocation/event/wait/owner registration and staging/Map/
pitch/rearm/signal faults, conservative quarantine after a failed Map recovery signal, and actual WARP transport
refusal/reuse cases. A cheaper agent supplied startup/partial-wait reclamation tests; primary review added failure
counter reset, guarded fixture admission and compact complete retry/cleanup assertions. Their private linked
probe and later normal registration/configuration qualification remain open; Main's current frozen queue is
unchanged while its gates run.

### Complete Debug matrix, preserved Release failure and private fault/layout probes

Full normal x64 Debug r122 exits 0: actual child 74416, UTC 2026-10-05T12:21:54.280593+00:00 to
2026-10-05T12:32:28.370060+00:00; 787 inputs, freeze `80854C75F7BBFF291222904D9C24981A0CA9239D970CB55CB089458A2BF4001D`. All five images, ISA/PDB/scanner controls,
dependency provenance, CPU/IO/host/composition tests, crash probes, the actual hidden WARP application and
screenshots pass. Independent registry proofs preserve every predecessor and require **6,963 document,
911 composition and 9,744 named application controls / 79 ordered stages**. Document additions are 44 import,
73 worker row-copy and five layer-copy controls; composition additions are 58 admission and 107 transport;
host additions are 118 import and 180 mask/copy/shape controls. The initial verification helper incorrectly
guessed 43 import controls and two source prefixes; primary comparison against frozen literals and actual
counters repaired that bookkeeping without changing the tested code. Archive `p1-image-wrapper-upload-x64-
Debug-r122-full-qualified` verifies 1,056 copies. Its original receipt is unchanged; two post-archive registry
proofs and the verifying helpers are bound in `Supplements/registry-proofs-r1/receipt.json`, SHA256
`D02F48C7116BBCF89C4BE434FC42EFAC6AF4A2564A05090AEE067DECDA38298C`. Four failed supplemental-helper attempts resolved manifest fields before the exact
five-file partial was verified and resumed without overwrite. This is Debug qualification of this source.

Full normal x64 Release r125 exits 1: actual child 86932, UTC 2026-10-05T12:33:32.115266+00:00 to
2026-10-05T12:39:28.800394+00:00, the identical 787 inputs/freeze. The actual application reached 73 stages and failed
`clipboard-work`: `clipboard-host.copy-retry.bounded-write-attempts`, then
`clipboard-host.paste-worker-allocation.fixture-clipboard-install error=5`. Its 1,056-copy failure archive
`p1-image-wrapper-upload-x64-Release-r125-clipboard-failure` is verified, receipt SHA256
`43407455CADAE5AEEC73BA557EF87EFBD5EF4206234F2C32E75C13058CCD2BB9`. No Release pass is inferred from Debug or analysis.
The original output gives no holder/attempt breakdown for the failed Publish; primary is adding scoped
diagnostics and a message-pumping, bounded test-only fixture contention wait in the private caller source.
Exact production six-attempt retry semantics and every assertion stay unchanged. Current-source full ASan
r124 is running independently and Main's code stays frozen until it ends and is archived.

Private strict Debug WARP fault/initialization child 76992 exits 0, UTC 2026-10-05T12:29:25.184205+00:00 to
2026-10-05T12:30:26.482874+00:00; 380 inputs, freeze `0C919D0A95E05B3A9ABC6CD13FD1FE14C8C234DFB035278D9ACA833B543E7ADE`, driver `B9B43ECB9E130D7C16B02A60277CF4998F3BF316CC8FB015A04A06A872BF4564`.
Its **346 controls** preserve 165 original queue controls and add 58 initialization and 123 fault checks.
They cover partial event/wait/owner startup cleanup, real wait-capacity exhaustion/reclamation, worker row
loans, allocation/Map/actual mapped-pitch refusal, rearm/recovery/copy signalling, conservative quarantine,
unchanged destination guards, reuse and retirement/close. Primary reviewed the cheaper-agent startup cases
and wrote the transport fault cases. Failed recovery arming now quarantines its class to exclude a late event
from a future loan. This private source is newer than Main's frozen queue and is not normally registered yet.

Private immutable arithmetic layout child 89740 exits 0, UTC 2026-10-05T12:52:56.928069+00:00 to
2026-10-05T12:53:49.760418+00:00; 377 inputs, freeze `2EA2D2026F7DF7EB99B4F809AEF7172904529AAF3C65EA6E4B86E4BB98191DD6`, driver `F6B87A26B6D5C66ADB30D74A870EF6A5CF54DA9B4FBEDD3D3F92323101E02DDE`.
Its **73 copy + 41 reduction + 34 layout controls** pass. Literal mip totals, crop-relative seams, source
retention, scratch charges and checked device/row/chunk limits include a fill-only 8192x5464 sparse raster:
1 MiB windows give 32 rows, 171 chunks and a final 24 rows without materialization or residency-epoch growth.
Primary corrected the delegated chunk-cap expectation and the Gray8 fixture's invalid 20-byte buffer for
five pixels; the first probe's one failed assertion remains preserved. This layout estimates logical texels
and bounded base-level windows only. Complete CPU/VRAM graph admission, device resources, mip generation,
current display publication and consumer activation remain U3-U8 work.

Primary is drafting an opt-in `Job::DeferRetirement` protocol in the private caller source: retain original
ticket/FIFO/reservation after a worker returns, including failure/cancellation, until external mapped/device
work acknowledges. Existing jobs and successful-stage `DeferCompletion` rules remain unchanged by default.
Independent scheduler state tests, normal registration and full qualification remain open; no scheduler or
upload consumer activation is claimed. G1, Native activation, native ARM64 and P1 Done remain open.

### Image-wrapper residency and original upload transport: complete managed qualification

Full managed x64 Debug r122, Release r126 and real ASan Debug r124 all exit 0 on the identical 787-input
source (`80854C75F7BBFF291222904D9C24981A0CA9239D970CB55CB089458A2BF4001D`). Release analysis r123 and all three r7 ARM64 cross-builds also pass on that source.
Every predecessor literal/multiplicity, stdout/stderr route and ordered application stage is preserved:
**6,963 document / 911 composition / 9,744 application controls / 79 stages**. The original 165 upload queue
controls, 73 row-copy controls and actual 24 image-import/mask/copy/shape host scenarios are normally registered.
Skills validate 15 entries, format checks 667 files, and scoped diff checks pass. No numeric golden, assertion,
retry budget, stack threshold, ISA allowlist, DxUi pin or running-application build guard was relaxed.

| Attempt | Actual child / exit | UTC interval | Verified archive completion SHA256 |
| --- | --- | --- | --- |
| `incremental-x64-test-Debug-r122` | 74416 / 0 | 2026-10-05T12:21:54.280593+00:00 to 2026-10-05T12:32:28.370060+00:00 | `p1-image-wrapper-upload-x64-Debug-r122-full-qualified` / `7FF7FF0636EE6606C56C32960CB6BD4D8E786E30BC5910F06457788B4273FC38` |
| `incremental-x64-test-Release-r126` | 85436 / 0 | 2026-10-05T13:04:14.711704+00:00 to 2026-10-05T13:09:18.535578+00:00 | `p1-image-wrapper-upload-x64-Release-r126-full-qualified` / `193C3290E18AD3162DB4FFEBD54323A450B25A5DFACC6AA555ACD4EFF3182F55` |
| `incremental-x64-test-ASanDebug-r124` | 94144 / 0 | 2026-10-05T12:48:07.297063+00:00 to 2026-10-05T13:02:13.220693+00:00 | `p1-image-wrapper-upload-x64-ASanDebug-r124-full-qualified` / `6885C5508DE62A01599986119B175846B124C1D5D2F3CE110C793DBF6386B0B3` |
| `incremental-x64-analysis-Release-r123` | 92716 / 0 | 2026-10-05T12:06:33.754773+00:00 to 2026-10-05T12:17:51.391408+00:00 | `p1-image-wrapper-upload-x64-Release-r123-analysis-qualified` / `D38854FD9477006133D8424AA1E1AB41DFB0F0405D8EBC51CFBC1D9273F22A27` |

ASan's actual isolated expected-failure diagnostic contains `AddressSanitizer: heap-use-after-free`; SHA256
`3E7B0B3D78C1DBF6E841F47C0732328E86C4B6F8B114E8F464305591C7F0F852`. It is bound separately under the completed archive's
`Supplements/asan-diagnostic-r1/receipt.json` (`01B4562F8775AF4F7320E04A31C3C784DFC4B6244300103FF276AC302631D5F7`); the original receipts are unchanged.
This separates real instrumentation proof from build success.

Original r112/r114/r118 fixture failures and full Release r125's clipboard failures remain archived.
The same unchanged code passes the clipboard stage in Debug, ASan and the repeated full Release run.
This does not establish the clipboard holder or repair the intermittent failure: private diagnostics and
message-pumping fixture work remain separate P1.7 qualification. No failed run is counted as a pass.

This qualifies the managed image-wrapper residency refresh and original row-copy/transport foundations.
The queue is compiled but unused by the application. The newer private proxy reduction/layout, queue faults,
opt-in terminal-retirement hook and upload job need review, normal registration and their full matrix before
activation. Complete CPU/VRAM graph admission, first display/mips/chrome/retirement, Native activation,
largest-fixture G1 and P1 Done remain open. Native ARM64 execution is unavailable; cross-builds make no runtime claim.

### Qualified step committed and pushed

Managed code/spec/test step `74c327d133503908ee5f35578c1b930898a8df05` is committed and pushed to `origin/codex/p1-opacity-host-proof`.
The exact working-source input hashes remain bound to the completed matrix above. This preserves the
qualified foundations while work continues on terminal upload ownership and full first display.
No Native activation, overall G1, RAW/Photoshop speed or P1 Done claim is made.
