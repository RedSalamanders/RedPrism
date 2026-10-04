# Deferred color display primitive — 2026-10-04

## Qualification checklist

- [x] Primary implements and reviews metadata-only preparation, constant-time checked cached planning and source/preview validation.
- [x] Cheaper agent writes independent bounded tests; primary reviews all code, fixes fixture mistakes, adds hierarchy/adjustment/uniform-mask cases and registers the suite.
- [x] Preserve the failed first Debug attempt and independently verify all 1,483 archive copies before repair.
- [x] Full private Debug build/run exit 0: 6,313 existing named controls unchanged plus 79 new controls; all 1,484 archive copies independently verified.
- [x] Full private Release/ASan document suites pass the same 6,392 controls; all 631 owned inputs match and every archive copy is verified. The actual ASan compiler tracking includes `/fsanitize=address`.
- [ ] Actual session/host publication, complete admission and retirement, source/settings authority and full normal/native integration.
- [ ] Original P1 G1 measurements; this primitive establishes no application timing or milestone completion.

Plan: [DeferredColorDisplay](../Plans/WIP/DeferredColorDisplay_2026-10-04.md), part of
[P1](../Plans/WIP/UiNeverBlocks_2026-09-22.md). Private source:
`%LocalAppData%\RedPrism\PrivateWork\p1-color-display-proof`.
Archives are under `%LocalAppData%\RedPrism\QualificationArchives\`.

| Attempt | Actual scope/result | Archive | Completed receipt SHA-256 |
| --- | --- | --- | --- |
| r1 Debug | Build 0, runtime 1: boundary fixture incorrectly expected rejection at exactly 100 M pixels | `p1-color-display-r1-Debug-failure` | `ED36ECCFE4DD38548F6708D37B3B69D651081CA180C970DAAA3E1E8AF047A4B9` |
| r2 Debug | Build/runtime 0; 6,392 named controls, including 79 new independent display controls | `p1-color-display-r2-Debug-qualified-private-document-suite` | `1D23AE87612667495F4ACF196301D683FF06C7399C5A89B149824515D57DD813` |
| r3 Release | Build/runtime 0; same 6,392 controls; 1,482 copies verified | `p1-color-display-r3-Release-qualified-private-document-suite` | `B43299577273A433023F74D4C29C280FBB400775A59AFF324C6960B711693E12` |
| r4 ASan Debug | Build/runtime 0; same 6,392 controls; 1,487 copies verified; real instrumentation | `p1-color-display-r4-ASanDebug-qualified-private-document-suite` | `E154380CBDACF5F58159061CFF9C953E9D316C9AF408874673FF26CD3889CB48` |

Primary review corrected the selection-loop retirement expectation and replaced a prohibited sparse-base
chain with nested sparse replacement tiles. The worker now explicitly refuses a non-BGRA target source;
manifest validation alone does not establish that image format. The combined-pixel budget test preserves
the inclusive 100 M contract: exactly 100 M succeeds; 100,000,100 pixels refuses without assembling its
99,990,000-pixel sparse source. Existing literal pixel/history expectations are unchanged.

Independent cases cover exact target replacement and untouched document/record fields (including fields
omitted from layer equality), implicit/explicit/linked/unlinked/uniform mask placement, semantic thumbnails,
hierarchy, clipping and unrelated curves, selection/raster sharing and sparse non-materialization; invalid
source/preview/target refusals; literal planning totals and each overflow/unavailable bound; cancellation
before construction and at every preparation stage/per-record checkpoint; and reached `bad_alloc` observers
with unchanged source and successful retry.

The worker clones/seals metadata and validates the complete resulting document; sealing alone is not validation.
It runs no pixel kernel, `Data`, history mutation or GPU operation. Cached planning is partial: host capture,
settings/render allocation and replaced-display ownership must be included before actual admission. The
existing full document validator remains an unsliced worker call; cancellation is checked around it, and
this suite does not establish the original 1 ms cancellation target for that call or the UI/apply/frame targets.
