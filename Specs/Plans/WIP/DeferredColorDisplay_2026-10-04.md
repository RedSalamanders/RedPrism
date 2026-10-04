# Deferred color display construction and retirement (P1.9 H2.2)

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
- [ ] Replace owned settings snapshots in the host's pending mailbox with bounded request descriptors; admit the whole graph before capture in the same resumable record.
  Implementation is in the managed candidate: scalar mailbox requests, borrowed complete planning, admitted
  capture/resume, preallocated failure publication, histogram capture after admission, and retained cancelled
  tickets. Fresh normal qualification is in progress; this row remains open until the actual host gates pass.
- [x] Private core keeps a compatible completed display and selects committed data on invalidation/off/identity. Opt-in settings ticks retain invisible preview caches and one old grown cache until prepared transfer; shader visibility uses `ColorLookupPreviewReady`.
- [ ] Reoffer a bounded latest display request after unrelated source metadata changes without changing the opening target token or restoring older metadata.
- [ ] Qualify actual host before/after-preparation gates, intermediate/latest ordering, cancellation/close/reopen/device/shutdown, faults and off-owner retirement.
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
  comparisons remain pending analysis and runtime qualification. The candidate has no passing current full
  matrix. Active-desktop G1/Release ETW remains an independent requirement.
- [x] r31 default-desktop Debug analysis passes all five images, SAL controls, ISA/PDB/scanner gates and provenance on the repaired source; its complete archive is independently verified. Full Debug/Release/ASan runtime and ARM64 cross-build qualification remain open.
- [ ] Activate with the immutable-root integration, full normal x64 suites / ARM64 cross-builds, owning contracts and measured due G1 rows.

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
