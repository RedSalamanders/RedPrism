# P1 private history topology delta qualification — 2026-10-12

## Result and scope

The private `HistoryTopologyAdapter-r2-Main` x64 Release candidate built and ran `DocumentTests.exe` successfully. The Main readback reports 18,785 predecessor records preserved in their original order and 213 added records: 212 independently rostered controls plus one summary (`passed=212 failed=0`). It reports no `FAIL` records in either the predecessor or current document-test records.

This qualifies only the private managed topology/history adapter and its declared bounds. It does not qualify native production integration, host consumers, operation admission or original-ticket ownership, actual host/ticket phase peaks, retirement performance, ordinary activation, G1, or P1. No normative contract changes follow from this evidence.

## Actual private build and runtime

The recorded build invokes Visual Studio 2026 Insiders MSBuild 18 for `Source/tests/documenttests/DocumentTests.vcxproj`, x64 Release, toolset `14.51.36231`, with `/m:1`, `/nr:false`, `/p:CL_MPCount=2`, and project references disabled. The build child (PID 37120) exited 0 from 2026-10-12 05:46:28.086150 UTC through 05:50:07.004271 UTC. The actual `Products/x64/Release/DocumentTests.exe` child (PID 66196) exited 0 from 05:50:08.256305 UTC through 05:50:18.740150 UTC. Both stderr files are empty. These are private-candidate receipts, not a build or runtime of the native checkout.

## Preserved records and controls

The Main reader takes its ordered predecessor stream from the qualified private Raster/history journal run. The roster identifies 9,204 original native DocumentTests records; the journal qualification preserves 18,733 records before adding its own controls and summaries. The resulting topology run's immediate predecessor stream has 18,785 records. All 18,785 remain in order in the topology output. The reader normalizes only the known variable private-wand measurement fields before that ordered comparison; it also verifies the exact topology-control multiset against the pre-runtime roster.

The 212 controls plus summary cover:

- Five direct layer source roles, independent legacy eight-owner closure, and complete managed wrapper/shared-root bounds.
- Six scalar selection edits reusing the exact raster-root group; changed-image replacement retaining the original group; and two-layer aliases deduplicating five direct roots.
- Nested sparse caches staying unmaterialized, a pre-cancelled topology update leaving the original unchanged, and 96 nested-DAG edits with 96 shared-child mask/shape/selection charge comparisons against the reference plus three late-cache-growth budget corpora.
- Scalar history through an active parent edge: publication of the original shared DAG, ten direct roots visited with zero live descendants, zero shared-raster charge, and cancellation during parent-edge processing preserving the already-published history.

The topology adapter uses actual Raster-owned storage serial identities from the qualified journal lineage, and accounts direct roots through owning raster identities and active parent-edge references. It preserves the original published accounting owners when reference-snapshot topology is unavailable; the preparation receipt records refusal of an unbound replacement owner. The producer-bound receipt records root/wrapper metadata sealed before publication-object construction and conservative node/control/path-copy and fixed-journal workspace declarations. The tests and receipts do not establish a production admission reservation or measured retirement bound.

## Receipts and independently checked identities

All paths below are relative to the repository root. SHA-256 values were recomputed from these files in the specified worktree.

| Receipt or input | SHA-256 | Recorded fact |
| --- | --- | --- |
| `.build/P1Drafts/HistoryTopologyAdapter-r2-Main/qualified.json` | `4C6CEBA37434F4F40AF20A8E3C7DD02857AA0FF71AD504A489977ECDE25E9BD3` | PASS; 18,785 predecessors; 212 controls; 213 added records; predecessors preserved in order |
| `.build/P1Drafts/HistoryTopologyAdapter-r2-Main/before.json` | `59A732F99A046E78B04331052099B6A1E2E254A91BD5E7ABCEB755B833E1794B` | Exact pre-runtime roster and source/input identities |
| `.build/P1Drafts/qualify-history-topology-r2.py` | `273839227DC8C8188389A216AE0C74C45AA01216AE00EAEBB68362FE9461A5CD` | Main reader hash recorded in `qualified.json` |
| `.build/P1Drafts/HistoryTopologyAdapter-r2-Main/build.stdout.log` | `AC527354968C6343A66F90BCC7AD55F4CE1FB64C5DD9ECC4DB4F28849E2D9523` | Successful x64 Release build output |
| `.build/P1Drafts/HistoryTopologyAdapter-r2-Main/document.stdout.log` | `0F478C64CEA2E8C606694A6004C513E649C89C7D74458AFA77D60441A92284E0` | Actual private DocumentTests output |
| `.build/P1Drafts/HistoryTopologyAdapter-r2-Main/build.stderr.log` and `document.stderr.log` | `E3B0C44298FC1C149AFBF4C8996FB92427AE41E4649B934CA495991B7852B855` each | Both empty |
| `.build/P1Drafts/HistoryJournalAdapter-r3-Main/qualified.json` | `6CA2C169F1CE02D26AF108447FA6BA13BFF0D006005F50613B595D94DFB31DC8` | Exact immediate predecessor qualification bound by the Main reader |
| `.build/P1Drafts/HistoryJournalAdapter-r3-Main/document.stdout.log` | `F10F1F38061F16A5C3BD5487DA8E53DDB314340BD49AE7BD419EE30C68B6ACFE` | Predecessor document-test stream bound by its qualification |
| `.build/P1Drafts/HistoryTopologyAdapter-r2-Main/preparation.json` | `EB46567C374970542BF90AB9CFE38DCBB911A215DD6294B3D15B12292FCC9ECC` | Original-owner fallback/refusal receipt |
| `.build/P1Drafts/HistoryTopologyAdapter-r2-Main/pre-runtime-owner-preflight.json` | `3AEDBB65FDF79B68E44BAD498B54DE9312E6DEBDE9F68261CBA159074DC54A99` | Owner-preflight receipt |
| `.build/P1Drafts/HistoryTopologyAdapter-r2-Main/pre-runtime-bounds-seal.json` | `1D973C1EE174C0E463BC2F92462F4C0194A4B293EE64474ACC1042987C288C77` | Producer bounds sealed before publication and binding |

The Main reader verifies the build/runtime result hashes and ordering, the origin qualification and predecessor log, the source/native-binding/product hashes, absence of `FAIL` records, and exact expected-control multiplicities. Its successful receipt records `original_native_document_records=9204`, `predecessor_records=18785`, `independent_new_controls=212`, and `added_records=213`.

## Preserved failures and remaining limits

The qualified predecessor and current record streams contain no failure records; the Main reader asserts that condition rather than dropping or rewriting failures. Earlier fixture-only compilation failures for private history adapter r1 remain documented in `Specs/Plans/WIP/IncrementalHistoryStorage_2026-10-12.md`; they are not part of this successful r2 runtime. The private r2 result does not close native integration or any of the production and G1 limits listed above.
