# P1 private import-residency and raster-upload CPU probes — 2026-10-05

Status: private diagnostics only; not a normal application/registry/configuration or phase-exit qualification.

## Reviewed work

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
