# Layer-record ownership bounds — 2026-10-04

## Qualification checklist

- [x] Primary review of immutable storage, checked accounting, cancellation and allocation-before-move semantics.
- [x] Correct independent test setup before qualification; preserve existing pixel/history/root expectations.
- [x] Private full Debug DocumentTests: actual build/run exit 0; 6,226 existing named controls unchanged plus 87 new controls.
- [x] Independently verify all 1,478 Debug archive copies before another attempt.
- [ ] Private full Release and ASan DocumentTests with identical owned source, real ASan instrumentation and independently verified evidence.
- [ ] Managed/native integration, owning contracts and complete normal configuration validation.
- [ ] Consume bounds in admitted worker display preparation and retirement; finish separate H3 accounting and G1 measurements.

Plan: [LayerRecordOwnershipBounds](../Plans/WIP/LayerRecordOwnershipBounds_2026-10-04.md),
part of [P1](../Plans/WIP/UiNeverBlocks_2026-09-22.md).
Private source: `%LocalAppData%\RedPrism\PrivateWork\p1-layer-record-bounds-proof`.
Archives: `%LocalAppData%\RedPrism\QualificationArchives\`.

| Attempt | Scope/result | Archive | Completed receipt SHA-256 |
| --- | --- | --- | --- |
| `r1`, Debug | Full private DocumentTests, actual exit 0; 6,313 named controls | `p1-layer-record-bounds-r1-Debug-qualified-private-document-suite` | `DAD9BA5A8DF67DCA1A6AFB50553C7F959C5E46C699A3AAB24B8E172B3956875E` |

The 87 `record-bounds.*` controls use a separate literal child policy to prove cached reads, capacity accounting,
concurrent immutable queries, disabled/unavailable policies, each arithmetic overflow, invalid child totals,
replacement, equal independent roots, copy/move/swap, adopted address, cancellation and a real allocator fault
followed by retry. ImageLayer controls use published capacities and actual raster fields, including spare curve
capacity, semantic mask/image/shape ownership, repeated owners, sparse non-materialization and unchanged bounds
after explicit lazy assembly. Root overhead is derived from an empty measured root instead of duplicating its layout.

These are conservative ownership/admission bounds. They deliberately sum shared graph occurrences and cover a
possible sparse assembly; they are not exclusive bytes or exact retained-history accounting. Missing bounds must
cause explicit refusal in a consumer requiring them. The existing `RetainedBytes` behavior is unchanged.
No application activation, frame timing, G1 compliance or ARM64 runtime result is established by this CPU suite.
