# P1 milestone and P2–P6 roadmap split

Date: 2026-10-04
Scope: documentation restructuring requested by the user; no product behavior changes in this step.

## Result

- [`UiNeverBlocks_2026-09-22.md`](../Plans/WIP/UiNeverBlocks_2026-09-22.md) is the P1 responsive raster editor
  milestone. Its opening checklist and slice table distinguish qualified native work, experimental work and
  remaining implementation/measurement. Its chronological P0/P1 evidence ledger is retained at the end.
- [`ImageProcessingAccelerationAndRaw_2026-10-04.md`](../Plans/WIP/ImageProcessingAccelerationAndRaw_2026-10-04.md)
  owns detailed P2–P6 implementation, exactness, GPU/RAW math, licenses, corpus/downloader, measurements,
  decisions and full-program exit gates. Its implementation entry follows qualified P1.
- Both plans remain ACTIVE and are indexed once. Through-P6 authorization, cheaper-agent review, checklist
  maintenance and commit/push after qualified steps are preserved.

## Verification

The immutable pre-split snapshot has SHA-256
`385E7731E88F626E6F5003B620B513C3D87839B7BE9F81CB7FBD389A961D823F`.
An independent comparison of the **written** documents verifies, ignoring only CRLF/LF encoding:

- Future sections 1–9 and 11–14 retain the complete original text.
- Original P2, P3, P4, P5, P6 and Closeout phase text is unchanged.
- The original P1 phase exit, G1 reliability table and per-operation design are unchanged in P1.
- All 125 local file links in the two plans resolve, including absolute source references with line numbers.
- A cheaper independent reviewer rechecked the final documents and index and reports no remaining finding.
- `validate-skills.ps1` validates all 15 skills; `git diff --check` passes.

The review caught a heading-formatting error in the initial extraction and a missing build-only progress row;
both were corrected and the written output reverified. No gate was removed to make P1 appear complete.

## Current implementation boundary

Private lifetime Debug/Release/ASan qualification now passes 8,962 controls/78 stages, with 520 identical owned
inputs and every archive copy verified ([record](P1DeferredCanvasLifetime_2026-10-04.md)). The managed normal
Debug build-only also passes, and its 733 source copies are verified. The qualified lifetime source has entered
the managed candidate; fresh normal runtime validation is in progress.

Native immutable layer-root integration, remaining tool/display/adjustment ownership, incremental accounting,
upload/chrome bounds and actual P1 G1 timing remain open. Licensed RAW acquisition is complete; actual decode,
develop and Photoshop comparisons are future phase work. P1 cannot move to Done until its original due gates
and current owning-contract validation are satisfied.
