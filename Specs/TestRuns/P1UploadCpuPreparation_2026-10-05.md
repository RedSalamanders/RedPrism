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
