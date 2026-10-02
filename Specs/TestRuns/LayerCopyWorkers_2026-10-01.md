# Layer via Copy worker preparation and publication

Status: production Layer via Copy passes all three native x64 suites and all three ARM64 cross-builds.
Live producer settlement and measured P1/G1 bounds remain open.

## Implementation checklist

- [x] Seal source document, active/mask target, displayed placement and history authority after whole admission.
- [x] Prepare selected pixels/origin or the complete unselected duplicate layer and history on workers.
- [x] Preserve private clipboard pixels, origin, session identity and serial through publication.
- [x] Pass 34 independent controls and the complete private document suite.
- [x] Pass full private WARP host cancellation, faults, staleness, younger Undo and aggregate-retirement controls:
  214 Layer via Copy host checks pass with zero failures.
- [x] Activate production routes, qualify all x64 suites and ARM64 cross-builds, and archive exact results.
- [ ] Qualify live producer settlement with P1.4/A6/S3 and measure owner capture/apply/history/chrome/upload.

## Reference and ownership

The existing selection clipboard contract and retained numeric golden cases own crop
origin, insertion, duplicate semantics and the `Layer via Copy`/`Duplicate Layer` history names. Selected input
uses the independently qualified CopyPixels primitive with the displayed image or mask placement. The worker
builds the whole after-document and prepared history; owner application uses the existing guarded nonthrowing
publication. It never reads or writes the private clipboard or invokes OS clipboard APIs.

Literal controls use a 2×3 source at document origin (1,0), a selection at (1,1) of size 2×2 and four explicit
BGRA pixels. Mask copying supplies literal Gray8 bytes and expects opaque grayscale BGRA output. Selected
insertion uses the first unused `Layer N` name, retains its parent and expands it. Without a selection, duplicate
retains the complete layer record, asset/mask/shape pixel identities and eager semantic mask thumbnail, obtains
a fresh Guid and appends ` copy`. It permits an unselected blank or adjustment layer; groups and explicitly
empty selections refuse. A selected blank source also refuses.

Admission checks independent image/mask budgets and the layer limit before allocation. Source/history capture
still copies metadata on the owner; this remains H2/G1. Live brush/transform/outline/lasso settlement is not
qualified by these settled-input controls.

## Private evidence and reviewed repairs

`.build/p1-layer-copy-draft/core-run-r2-result.json` records exit zero at
`2026-10-01T20:31:17.8026540Z`, executable SHA-256
`6F9215A956831324D7C9D397D96E4290DE8126DDE7E7CD894F572129BD764A28`. The complete document suite passes
34 Layer via Copy controls. `core-qualified-r2/` preserves 484 source inputs and the exact image/log/result
evidence; no uncompiled host input in that snapshot is qualified merely by its presence.

The cheaper agent wrote only the two numeric test files. Primary review corrected an insertion assertion to
keep the source at index zero, changed its no-asset refusal fixture to include a selection and added the positive
unselected blank-layer duplicate control. Its original proposal is preserved under `agent-layer-tests-original/`.
`core-build-failure-r1/` preserves the failed 484-input candidate: the new core needed BrushStroke.h's complete
GridRect definition and the existing image-budget constant; the moved-input tests needed an explicit observer.
`core-runtime-failure-r1/` preserves a later invalid adjustment fixture that combined an adjustment and raster
asset. The owning contract forbids that combination, so the repaired fixture removes its asset, verifies document
validity and retains exact exposure parameters. Production duplicate behavior and old goldens are unchanged.

`layer-copy-full-r2-result.json` records exit zero at `2026-10-01T20:42:58.1929281Z` for the full private WARP
application proof, with executable SHA-256
`C0439133FABB0F0AB02F19DAE9AF950CBCD711C1C8F79C2F9B6EB683A07A48F3`. Its output reports 214 Layer via Copy
host checks and zero failures. `full-qualified-r2/source-freeze.json` contains 484 source inputs; all 484 source
hashes and all 27 entries in `full-qualified-r2/evidence-manifest.json` matched SHA-256 recomputed from
`ReadAllBytes`. The full-qualified snapshot retains the result, output/error logs, source freeze and evidence
manifest.

The earlier full-app r1 run exited 5 on the budget-notice self-test: its observation ran before the queued
operation settled. R2 changes only that observation timing. This was a self-test ordering defect, not missing
production notice behavior. Native configuration qualification, live producer settlement and measured P1/G1
costs remain open.

No native ARM64 runtime, RAW throughput or Photoshop comparison is claimed.

## Native qualification

Production sources are integrated. Native r1 stopped before tests at the ASan ISA scan: the decoder read
Application::EditUnderWay's terminal switch table at `0x14065bcd8` as EVEX instructions. Its exact image,
PDB, scan inputs and 503 frozen sources remain in `.build/p1-layer-copy-r1-ASan-failure/`. The strict
long unsigned-JA stack-guard proof accepts the same archived binary with zero violations; the runtime
allowlist is unchanged. All existing synthetic controls and 48 long-JA controls pass. The failed controls
and pre-repair rescans are retained separately. These results qualify parser data recognition only.

Native r2 passed the scanner and then exited on four clipboard-binding assertions in IoTests. Publication
diagnostics showed `Invalid` before any sequence/format mutation; the helper could have refused its fixture
installation or preparation. The exact cause is unproven. All 503 source inputs and 88 evidence files remain
in `.build/p1-layer-copy-r2-ASan-failure/`. The exact archived IoTests image passed a subsequent full replay.
R3 adds preparation/fixture-install diagnostics without changing assertions or production clipboard behavior.

The complete incremental native r3 matrix exited zero at `2026-10-01T22:42:19.6601803Z` with 503 frozen inputs.
Each x64 Debug, Release and ASan Debug suite passes all unchanged numeric goldens, 34 Layer via Copy document
controls and 214 actual host controls. Existing mask/Shape/clipboard host suites also report 201/199/268 checks
with zero failures in each profile. All three ARM64 cross-builds pass; no ARM64 runtime was executed.

`.build/p1-layer-copy-r3-qualified/` preserves the 503 sources and 297 evidence files, including all images/PDBs,
ISA inputs/reports, suite/application logs, profile receipts and the full matrix receipt. Every archived file
was verified against its source SHA-256. The source manifest hash is
`275D0A84A609A6586CD9F35B3EBAE5A66CFF1AE754DED8A7B699843E2791771C`; the evidence manifest hash is
`866EC0F7748656A5105453E50CA49BD7BA7EB44246912CA2EFBDED4514B328B0`.
Live producer settlement, owner capture/history/chrome/upload costs and the complete P1 clean exit remain open.
