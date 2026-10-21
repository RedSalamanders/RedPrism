# P1 Pixel Move target capture and 100×40 goldens — 2026-10-19

## Scope and delivery status

P1 remains WIP. This component moves the full target-layer and target-identity copies for Pixel Move commits
and prepared-display previews from the owner to the worker, after the existing complete reservation. It adds
independent numeric CPU-model move/duplicate/outline goldens. Initial physical Ctrl/lift, the unprepared-preview
capture, ordinary activation, the remaining delivery areas and measured G1 are still open.

## Source and ownership review

Base: `99d5c8eeb7a79cf88126484201d9c426fed3ad45`. Exact candidate files are adopted from
`.build/P1Drafts/PixelMoveTargetCapture-Main-r4/RedPrism/` (four files) and
`.build/P1Drafts/PixelMoveLegacy100x40-Main-r4/PixelMoveStateTests.cpp` in the primary Z: checkout.
Pre-adoption and adopted bytes plus SHA-256 identities are retained at
`Z:\src\RedPrism\.build\P1Evidence\p1-pixel-capture-native-r1/source-adoption.json`.

Main checked the admission and retirement envelopes in `SubmitPixelMoveJob`, `PixelMoveMemoryBytes`,
`RasterEditMemoryBytes` and history preparation. Commits already charge source/history ownership; prepared
previews already charge the full document source and preview metadata. Their owner capture now retains the
immutable document root and a targetless token. The worker resolves and copies the complete layer and target
identity, including mask/adjustment metadata. A prepared preview releases its extra source root on the worker;
a commit keeps it for publication. Cancellation/failure cannot publish a successful targetless result. The
unprepared-preview path retains its previous owner copy because its source envelope is incomplete.

The test gate is included by exactly the three changed Application translation units. Tests observe actual
Win32 worker/owner thread IDs. The successful commit carries image and mask asset names of 1,048,576 UTF-16
units each through worker capture. Existing `RunRasterEdit` output policy normalizes the image asset name to
the layer name; the exact large mask name remains. Pixel values, one history entry and Undo remain unchanged.

## Private native qualification, before canonical integration

`D:\RedPrismBuildCaches\.build\p1-pixel-target-capture-main-r4/{ASanDebug,Release}` retains source/build/runtime
bookends, 3 fresh Application objects and 348 exact inherited matching-profile objects, native logs, PE/full
PDB pairs, ISA reports and Main's predecessor review. ASan PID69176 exited0 at07:10:41.785259 UTC; Release
PID68936 exited0 at07:18:44.824420 UTC. Each complete Application run passes152 stages /11,323 stdout facts,
preserving every11,313 previous matching-profile fact and adding exactly ten target-capture facts. The unchanged
product control fails only the four added prepared-preview worker-capture predicates and preserves438 existing
pending-raster facts; it stops before the later commit stage.

`D:\RedPrismBuildCaches\.build\p1-pixel-legacy-goldens-main-r5/{ASanDebug,Release}` retains the focused CPU proof:
two fresh translation units and283 exact inherited Document objects. Each native runtime exits0 with75
PixelMoveState assertions (all52 predecessors plus23 new assertions). This is not a complete Document suite.
Main checks literal references against `retained numeric golden cases–287,331–345`:
100×40 red-left/blue-right source, selection(10,10,10,10), final absolute rounded offset(60,5), hole(15,15),
red landing(75,20), untouched blue(85,20)/red(5,5), bounds(70,15,80,25), one Move/Duplicate undo, and translated
outline before CPU completion and owner application. The duplicate final-selection assertion also follows
`Document_Tools`; no legacy oracle is changed.

All four private images independently pass x64 PE/RSDS and full-PDB GUID/age checks, actual fresh compiler
profile checks and zero ISA violations. The allowlist remains
`7870AF49195841A9B9B88DFD15378D550B127754683666C83AED83753FD80E1E`. DxUi stays pinned; no library source changes.
Receipts: `main-image-profile-qualified.json`, `main-predecessor-review.json` and focused `runtime-facts-qualified.json`.

## Retained failures and corrections

- The earlier private link/library-path refusals remain retained.
- An earlier target-capture ASan run stops at the inherited save-held settlement boundary, after the target
  capture tests pass. No precise historical cause or causal product fix is established by the passing repeat.
- Main rejects a draft expectation that the source image asset name survives commit; the existing product
  intentionally normalizes it. The corrected fixture verifies that policy and full mask metadata.
- Main rebases the numeric additions onto the current immutable test-fixture API after the stale draft fails
  compilation. Missing include-path preparation and that actual compilation failure remain retained.
- A private link used lowercase object arguments and failed the unchanged case-sensitive runtime ISA exception
  attribution for `SettingsStore.obj!wmemchr`. The helper now uses canonical object spelling and proves those
  objects have identical bytes. The scanner/allowlist are unchanged; rejected images/reports remain retained.
- The private full-PDB reader originally tried to decode the whole raw LLVM module dump as UTF-8. Its corrected
  reader decodes only the bounded summary and hashes the full dump, qualifying the same native images.

## Canonical integration

Current component source: freeze `ADB760658300F21465F8F9A923451311CE8089D372E8E531FCDA186639F2994C`,
1,829 inputs; working delta `9D39D54F68E476DD45D616382136205E8CA7684CFC30B15B06953EAB48652522` on base99d5c8e.

| Attempt | Result and independent qualification |
| --- | --- |
| p1d1, full x64 Debug | Exit0; all five suites,152 Application stages /11,323 assertions,23,637 Document assertions /75 PixelMoveState facts, nine fresh harness captures and both crash probes pass |
| p1a1, full x64 real ASan | Exit0; same current Application/Document roster, genuine isolated UAF detection,256-round raster /64-worker stress, nine fresh harness captures and both crash probes pass |
| p1armd1 /p1armr1 /p1arma1 | All ARM64 Debug/Release/instrumented-ASan cross-builds exit0;994 actual compiler records and five full matching AA64 PE/PDB pairs per profile |
| p1an1, full Release analysis | Exit0 at10:27:11.647869UTC;994 actual analyzed compiler records, five full matching PE/PDB pairs, zero image ISA violations, all14,133 named default controls and native positive/negative concurrency SAL probes pass |
| Canonical Release runtime | Pending; qualification of the next combined capture/preflight/fixture integration remains separate |

Each archive inventory and every copied artifact are independently verified. Archive manifests:
Debug `CB49B963523259EE07A486D9DEAAC00B40FA349648A3A4352620B87E6B8991B1`,
ASan `CDDFB5EB9FF2D73EAB84A8AA7E014B7EB5D6F4F4973DED5C11007E8B4DC04E18`,
analysis `7CDC90E331345AE2C9A88F6D9AEC280CB0D41B4B1029216D0D7FBF3D62818916`.
The four-profile ARM64/ASan source/object bridge proves the four changed C++ units compiled freshly and other inherited
objects match their qualified predecessors. Debug has its independent full994-record/profile qualification.

Receipts under the evidence base: `p1a1-runtime-qualified.json`, `p1d1-runtime-qualified.json`,
`p1an1-analysis-qualified.json`, per-attempt `*-profile-qualified.json`, `*-archive-qualified.json`, and
`source-and-object-bridge-qualified.json`. The first analysis reader counted synthetic probe scan markers as
the five product images; its second iteration assumed an unshortened MSBuild tlog folder. Both refusals are
retained. The corrected reader explicitly selects the five product images and requires one actual tlog;
it verifies the same archived native result without rerunning analysis.

Formatting/skills passed before the frozen runs; repeat for the final combined change. Layer-row trials are
functional smoke tests with an unmeasured performance gate. Native ARM64 execution requires another host and
remains unavailable here. Current ordinary activation and all nine milestone delivery areas remain open.


## Latest combined canonical qualification

The integrated capture/preflight/fixture correction passes current full x64 Debug/Release/real-ASan and all three ARM64 cross-build profiles. See the [exact-source matrix](P1CaptureMatrix_2026-10-19.md); it supersedes pending combined-matrix statements above. Native ARM64 execution and ordinary activation remain unqualified; the historical panel/Wand failure cause remains open.
