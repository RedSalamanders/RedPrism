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

## Session publication work in progress

Private continuation: `%LocalAppData%\RedPrism\PrivateWork\p1-color-publication-proof`. Primary implemented
move-only capture/publication, complete source and scalar history authority, and transfer of replaced preview
and display owners. History authority holds no undo stack: display preparation does not read that stack.
Borrowed planning now covers render scratch, settings/target copies, immutable metadata construction, the
old display and panel owners, with additional allocation and conservative retirement reported separately.
Host job/payload records must still be added before admission.

The cheaper agent supplied 60 independent session controls; primary corrected move-only test handling and
a missing selection include. The first compile failure (`p1-color-publication-r1-Debug-failure`, completed
receipt `2BFCF30BD03BD0968190FFD351553B98C40A12B2E5041F88BDE26296F42735A9`) retains 1,475 verified copies.
The second build and full runtime both exited 0 with 6,452 controls, but its archival wrapper rejected a wrong
new-test prefix in registry comparison. That qualification failure is preserved as
`p1-color-publication-r2-Debug-failure`, completed receipt
`3D9A8681DD63FF0CF317292CB13FEC49FA5CB5E98FFE56ECFC017BAC0625F3A1`, all 1,489 copies verified.
The wrapper is repaired. Those executions preceded the scalar-history/planning changes and do not qualify
the subsequent source. Isolated history invalidation, moved publication, failed-display invalidation and
borrowed-planning cases are being added before the fresh three-profile matrix. No host or timing claim follows.

## Qualified private publication core

Fresh forced builds and full document suites each execute 6,392 unchanged preceding controls plus 96 independent
publication/planning/owner-retention controls. All 635 owned `.cpp/.h/.hlsl/.rc` inputs match by individual
SHA-256. The ASan compiler command includes `/fsanitize=address` (tracking hash
`BACAE1165358AC96F0C36B3F4EEA117499BB80DA2677579321A3AB8280279974`).

| Attempt | Actual build/runtime result | Archive and independently verified copies | Completed receipt SHA-256 |
| --- | --- | --- | --- |
| r3 Debug | 0 / 0; 6,488 controls | `p1-color-publication-r3-Debug-qualified-private-document-suite`, 1,490 | `F3DB3EA1353347BBF4214DE45CAC386374FC4DFF9DA489ADB127057FE779D12E` |
| r4 Release | 0 / 0; identical controls | `p1-color-publication-r4-Release-qualified-private-document-suite`, 1,488 | `B3C000E2634824B684A432FE2E4C306364EC8ABCB027761166137F2050BE2D5D` |
| r5 ASan Debug | 0 / 0; identical controls, actual instrumentation | `p1-color-publication-r5-ASanDebug-qualified-private-document-suite`, 1,493 | `ADDBDD7DED54C673BB4EBE98A5D87D68A4D747A160219618F3E5C9256C9518FB` |

Independent cases separate history-only structural invalidation from complete-source invalidation; changing the
save marker alone keeps publication valid. Moved-from/consumed, copied/foreign session, replaced document,
changed source/history, out-of-order and intermediate settings cases preserve current data and controls.
An allocation-free failure factory transfers captured owners without copying; only a current latest failure
invalidates the shown preview. Worker cancellation/allocation observers preserve source/history and permit retry.

Primary found and fixed another final-owner path: opt-in settings ticks formerly reset grown/preview caches.
Off/identity now retain invisible handles; growth moves the old grown cache into one fixed slot, repeated growth
retains that same owner, and a matching publication moves it into the prepared retirement aggregate. Visibility
checks exclude those stale handles. A weak lookup owner remains alive across invalidation and publication, then
expires with the prepared aggregate. Default inline behavior and all existing goldens remain unchanged.

The complete CPU planner covers render scratch, settings/token copies, record construction and old display/panel
ownership without traversing undo history or materializing pixels. It reports resident retirement separately from
new allocation. Actual host job/payload records, reservation-before-capture/resume, mailbox descriptors, shader
visibility wiring and worker retirement still require integration and host tests. Direct test destruction of a
prepared aggregate does not prove retirement off the application owner. No application/G1/native activation is claimed.
