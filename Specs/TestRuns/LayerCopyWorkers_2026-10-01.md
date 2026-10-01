# Layer via Copy worker preparation and publication

Status: the complete private DocumentTests suite passes; private full application and native configuration
qualification remain open. No measured P1/G1 bound follows from this functional proof.

## Implementation checklist

- [x] Seal source document, active/mask target, displayed placement and history authority after whole admission.
- [x] Prepare selected pixels/origin or the complete unselected duplicate layer and history on workers.
- [x] Preserve private clipboard pixels, origin, session identity and serial through publication.
- [x] Pass 34 independent controls and the complete private document suite.
- [ ] Pass full private WARP host cancellation, faults, staleness, younger Undo and aggregate-retirement controls.
- [ ] Activate production routes, qualify all x64 suites and ARM64 clean builds, archive and commit/push.
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

No native ARM64 runtime, RAW throughput or Photoshop comparison is claimed.
