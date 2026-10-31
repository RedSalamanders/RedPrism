# Deferred color display construction and retirement (P1.9 H2.2)

> **HOLD** with [P1](UiNeverBlocks_2026-09-22.md) since 21 October 2026. Do not resume this file until that plan leaves HOLD. Checked boxes below are historical qualification, not permission to continue.

## Implementation checklist

- [x] Identify owner-side `DisplayDocument` record cloning and final-owner release for deferred destructive previews.
- [x] Qualify cached layer-record ownership bounds in private Debug/Release/ASan; use them as the planning prerequisite.
- [x] Implement the private metadata-only display primitive and constant-time checked planning; primary-review the cheaper agent's independent tests and add missing hierarchy/mask cases.
- [x] Private full Debug/Release/ASan document suites pass 6,313 unchanged existing controls plus 79 display controls/profile; all 631 owned inputs match, actual ASan instrumentation and every source/evidence copy are verified.
- [x] Private worker preparation builds complete immutable color display documents, preserving mask placement and raster-preview rules; actual host activation remains open.
- [x] Prove the pure display primitive, cancellation/faults, metadata planning, source sharing and unchanged original records against independent expectations.
- [x] Primary implements private full-source publication and prepared-owner transfer; independent session tests and actual host qualification remain open.
- [x] Private core publication validates opening/source/settings and scalar history authority, swaps the complete display, and retains replaced owners in its move-only result. Actual host admission and worker retirement remain open.
- [x] Fresh private Debug/Release/ASan suites pass all 6,392 preceding controls plus 96 publication/planning/owner-retention controls; 635 owned inputs match and every archive copy is verified.
- [x] Replace owned settings snapshots in the host's pending mailbox with bounded request descriptors; admit the preview graph before capture in the same resumable record.
  Implementation is in the managed candidate: scalar mailbox requests, borrowed complete planning, admitted
  capture/resume, preallocated failure publication, histogram capture after admission, and retained cancelled
  tickets. Current Debug r267 and real ASan r270 pass the actual admission-wait/resume and waiting-cancel
  cases. Complete histogram/preview overlap and admission-boundary qualification remain separate below.
- [x] Private core keeps a compatible completed display and selects committed data on invalidation/off/identity. Opt-in settings ticks retain invisible preview caches and one old grown cache until prepared transfer; shader visibility uses `ColorLookupPreviewReady`.
- [x] Reoffer a bounded latest display request after unrelated source metadata changes without changing the opening target token or restoring older metadata. Retain the preceding ticket until its acknowledgement; the later request rebuilds current sibling metadata with the same panel opening/settings sequence.
- [x] Qualify the named actual-host slice: 14 main-route, two admission and two shutdown checks pass in current Debug r267 and real ASan r270 on the same 867-input source. This includes latest-sequence coalescing, before/after-preparation device recovery, Cancel/reopen, source reoffer, maximum-record publication, faults and worker retirement.
- [ ] Finish remaining admission and acceptance proof: complete histogram/preview/retirement overlap, refusal/overflow and changing-headroom boundaries, continuous settings and remaining failure/retry interleavings. The two blocker-based admission cases prove their bounded wait/resume/cancel state transitions.
  Earlier checkpoint evidence follows; its source and failures remain preserved.
  The first actual host cases are integrated. Primary review corrected display-versus-committed readback,
  immutable fault configuration and the real Cancel acknowledgement before reopen. Device/shutdown,
  large-metadata and full acceptance coverage remain open.
  Normal Debug r17 passed all ten initial cases and the 6,491-control document suite, then failed the later
  clipboard compatibility fixture. Its null clipboard hold is now independently reproduced and corrected;
  the full normal matrix and added device/maximum-record/shutdown/admission cases still require qualification.
  The corrected 18-case route is implemented and pushed in WIP checkpoint `92bd2ae`. Debug r19–r26 retained process/tool-start and clipboard fixture
  failures before the main application registry; all attempts are independently archived. The standalone
  6,491-control document suite passed in r21, but this does not qualify the application. r27 used a private
  temporary WinSta0 desktop, closed after its child exited, and process-scoped `MSBUILDDISABLENODEREUSE=1`.
  The unchanged forced-rebuild `build.ps1` analysis entrypoint reached compiler diagnostics, then exited 1.
  Source repairs for in-place color results, heap path scratch, test splitting/array bounds and exact HRESULT
  comparisons remained pending analysis and runtime qualification at that checkpoint. That candidate had no passing full
  matrix. Active-desktop G1/Release ETW remains an independent requirement.
- [x] r31 default-desktop Debug analysis passes all five images, SAL controls, ISA/PDB/scanner gates and provenance on the repaired source; its complete archive is independently verified. Full Debug/Release/ASan runtime and ARM64 cross-build qualification remain open.
- [x] Earlier 754-input candidate's fresh Debug full runtime passes every preceding control, all 99 publication and 18 actual-host controls; its three ARM64 cross-builds pass. These precede the current HWND fix and do not qualify it; current Debug/Release/static-analysis/ARM64 qualification remains open.
- [x] Preserve Release r36's ISA refusal before runtime; isolate six shared-editor fixtures while retaining every assertion and scanner rule.
- [x] Fresh fixture-isolated Release r37 full runtime passes exactly 6,491 document / 8,980 application controls and 78 stages; its independently verified archive retains 1,021 copies.
- [x] Preserve fresh x64 ASan Debug r38's full-test exit 1: three of 45 `clipboard-delayed` controls failed, the application self-test exited 5, and the original failure archive's 1,028 copies are independently verified. No sanitizer violation is established.
- [x] ARM64 Debug r39, Release r40 and ASan Debug r41 cross-builds exit 0 on the same 754-input freeze; their 956/956/960 archive copies are independently verified. These are builds only, not native runtime qualifications.
- [x] Preserve ASan r42's actual overall exit 1: build and Host/Document/Composition suites passed, then IoTests clipboard-transfer reported 11 failures in 139 checks and stopped before the application (zero stages). Independently verify all 1,028 archive copies; retain the archive name and receipt.
- [x] Record isolated-station diagnostics r43–r45 as pre-child failures (`CreateWindowStationW` errors 5, 5 and 183); these are not qualification runs and establish no environmental cause.
- [x] Replace null-owner clipboard opening in worker Inspect/Copy with a fresh worker-owned `STATIC` message HWND and RAII scope; preserve the existing busy retry budget and empty failure outputs. Add the actual two-MTA-worker held-owner regression, owner-thread/HWND checks, post-release 2x1 metadata check, and pre-open invalid-window refusal; existing goldens remain unchanged.
- [x] Preserve build-only ASan r46 (exit 0; not analysis), focused runtime failures r47/r48 (clipboard errors 1418; archives independently verified), and read-only native diagnostics r49/r50. The concurrent null-owner scope failure and distinct-owner busy/close behavior are reproduced; the original r47/r48 actor remains unidentified.
- [x] Complete full x64 ASan Debug `test.ps1` r51 after the worker-owned clipboard scope change: actual exit 0; all 146 clipboard-transfer checks; unchanged 8,980 application controls and 78 ordered stages; 6,491 document controls; crash, ASan-detection, ISA and other test gates pass; all 1,028 archive copies independently verified.
- [x] Current Debug static analysis r52 passes after replacing constant template switches with `if constexpr`; all 17 fixture `Expect` expressions and all 983 whole-file expressions retain identical multisets. No production, golden, scanner, threshold or suppression changes.
- [x] Finish the current exact-source matrix: full Debug r267, Release r268, real ASan r270,
  Release analysis r271 and all three ARM64 r38 cross-builds pass with independently verified archives.
  Old HWND receipts qualify their original snapshots only.
- [ ] Finish native immutable-root/display activation, remaining owning-contract reconciliation and measured G1.

**Current status: ACTIVE managed candidate.** The 18 named color-display host checks independently match in
full Debug r267, Release r268 and real ASan r270 on 867 frozen inputs, hash
`0236021DC5A19194AA4AC527FF628A7980A131A78D2A7337C7E40A5D157E75E4`. ARM64 r38 cross-builds pass all
three configurations on that source; Release analysis r271 also passes. Native activation, complete
admission accounting and G1 remain open. See the [current integration record](../../TestRuns/P1CloneReadback_2026-10-06.md).

**Historical qualification:** Fresh full Release r37 qualifies the fixture-isolated candidate, with exact
6,491 document / 8,980 application controls and 78 stages. Its 754 inputs and all archive copies are verified.
ASan Debug r38 subsequently failed three `clipboard-delayed` controls (overall exit 1); its original archive is
preserved and independently verified. ARM64 Debug/Release/ASan Debug r39–r41 each cross-built successfully on
the same freeze, with no runtime claim. Earlier Debug r32 and analysis r31 precede fixture isolation. ASan r42
also exited 1: its 754-input freeze built and passed HostTests, DocumentTests and CompositionTests, then
`IoTests.exe` failed 11 of 139 clipboard-transfer checks and stopped before application execution (zero stages).
Its separately named archive has 1,028 independently verified copies and the completion receipt recorded in
the [evidence record](../../TestRuns/DeferredColorDisplay_2026-10-04.md). Clipboard-holder retries later
succeeded, so those records do not establish the cause. Isolated-station diagnostics r43–r45 failed before child
startup with `CreateWindowStationW` errors 5, 5 and 183; they are neither qualification attempts nor proof of an
environmental cause. The r46 incremental ASan build exits 0 and is build-only, not analysis. Focused r47 IoTests
and r48 application runs fail at native clipboard calls with error 1418; both 1,028-copy archives have
independently verified receipts, and their original clipboard actor remains unidentified. Read-only r49 passes
100 opens/closes of the current Unicode clipboard but proves neither application qualification nor universal
stability. Read-only concurrent r50 reproduces corruption with two null-owner scopes and confirms a distinct
owner HWND makes the competing open busy while preserving the first scope. Worker Inspect/Copy now use a fresh
worker-owned `STATIC` message HWND and RAII scope, with an actual two-worker held-owner regression. Full x64
ASan Debug `test.ps1` r51 exits 0: all 146 clipboard-transfer checks pass, the hidden WARP application registry
records the unchanged 8,980 controls and 78 ordered stages, and the document registry records 6,491 controls.
Crash, ASan-detection, ISA and other test gates pass; the independent archive receipt verifies 1,028 copies
with completion SHA-256 `AC69CB61894D66FC58A4ED78AA3856CE6D48110270FFB7EDF435FC55A9BDA2CE`. Complete
normal/native integration, P1 exit requirements and measured G1 remain open; the milestone is incomplete.
See the [evidence record](../../TestRuns/DeferredColorDisplay_2026-10-04.md).
Status: ACTIVE private prerequisite work; part of [P1](UiNeverBlocks_2026-09-22.md) and H2.2.
Bound prerequisite: [LayerRecordOwnershipBounds](LayerRecordOwnershipBounds_2026-10-04.md).
Owners: `ColorEditWork`, `DocumentSession`, `ApplicationAdjustJobs`; contracts: `Document_Model`,
`Document_Tools`, `Document_History`, `Core_PerformanceAndResources`, `Testing_Validation`.
Private primitive evidence: [DeferredColorDisplay](../../TestRuns/DeferredColorDisplay_2026-10-04.md).

## Display semantics and publication

The worker shares the committed raster graph and privately clones layer metadata. Replace exactly the preview
target's image/transform. When a mask has no separate placement and the preview transform changes, retain its
original layer placement as today's display path does. Keep names, ids, order, parents, clipping, blend, opacity,
adjustments, shape fields, explicit mask placement and semantic mask thumbnail unchanged. Preview construction
does not create history or mutate the committed document. No raster is materialized for display metadata work.

Direct inline color APIs retain their numeric goldens and immediate behavior. Live adjustment-layer lookup
previews continue their existing composition route. The new deferred route must validate opening, retained target,
settings/seed/grid sequence and complete display source authority before publication. Current settings never roll
back to a completed intermediate request. A source change invalidates a complete old display; unrelated metadata
must not be overwritten by an old full-document snapshot. A new bounded request may reuse compatible acknowledged
pixels/lookup/coverage while rebuilding current metadata. Preserve the existing layer-scoped commit token rule.

Apply swaps the fully prepared display and transfers every replaced CPU owner into an already allocated worker
retirement aggregate. Dropped, failed and cancelled results use that same ownership boundary. Deferred
`DisplayDocument` only selects a compatible complete display or the current committed document. It allocates,
clones, validates, processes and retires no large graph. Off/identity/source invalidation may retain an invisible
old graph until admitted retirement acknowledges; its resident/retirement ownership must remain charged.

## Planning and host ordering

The pure metadata primitive uses constant-time cached bounds and checked unsigned arithmetic. Its declared
metadata peak covers snapshot/draft/sealed-header overlap, complete record-child capacity and root/control
allowances. Its retirement declaration covers held source and new display graph occurrences, including the
preview raster's possible sparse assembly. It must refuse unavailable/overflowing estimates. These partial
declarations cannot authorize a complete host job: add settings/token/panel/selection/lookup, render scratch,
current display and complete aggregate retirement before any owning capture. Existing resident pixels are not
new allocations; retirement occurrence overcount remains separately reported. H3 exact accounting remains open.

The host retains one active and one latest pending primitive descriptor. A descriptor identifies session/document,
opening/settings sequence and source generation; replacing it releases no document graph or curve-vector snapshot.
One admitted resumable record captures the exact current source/settings on the owner, then resumes worker
render/display construction. A settings tick retains the active intermediate request. Cancellation invalidates
publication immediately but retains tickets, complete reservations, receivers and owners until acknowledgement.
Failures report through deferred UI work with no idle retry, timer, extra pool or blocking production wait.

## Required proof

Independent core cases cover exact target replacement, grown/downsampled grid, linked/unlinked/explicit/uniform
mask placement, all untouched records/fields, raster sharing, sparse non-materialization, missing/invalid targets,
admission refusal and cancellation/allocation checkpoints with no partial output. Publication cases cover
consumed/foreign/source/history/opening/settings refusal and unchanged committed/history/clipboard state.
Host gates hold work before and after preparation while view/chrome messages dispatch, then prove exact one-time
publication/drop and retirement off owner. Include 10,000-record metadata, source changes during rendering,
preview off/on, continuous settings, failure/retry, closure and shutdown. Structural tests establish no timing
claim; original P1 owner/apply/frame/cancel measurements remain required on G1 fixtures A/B.
