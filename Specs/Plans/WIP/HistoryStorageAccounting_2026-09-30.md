# History storage and ownership (P1.9)

## Implementation checklist

- [x] Back up the qualified private immutable layer/typed opacity candidate: `7dd28d7`, `codex/p1-opacity-host-proof`.
- [x] D13 explicit uint64 application limit, class default and machine-query fallback; 45 MP sizing retains undo.
- [x] Cache retained selection container capacity at geometry construction; constant-time immutable reads.
- [x] Count unique selection storage in history with the same live-document exclusion as raster storage.
- [x] Move pending snapshots and history entries; return a complete restore snapshot even when trim removes its entry.
- [x] Move the returned snapshot into the session; preserve mask targeting, crop/pending-edit and revision semantics.
- [x] Independent sharing, spare-capacity, trim, restore-after-trim and unchanged legacy history tests.
- [x] Full configuration matrix and measured real-loop history/apply costs; record outstanding P1.9 gates.
- [ ] Incremental accounting, lazy image thumbnails and first uploads; complete the remaining P1.9 work before closure.
  - [ ] Current H2.2 caller integration: complete private immutable-root and preserving-metadata applications pass
    Debug/Release/ASan. Producer opacity ordering, adjacent previews and bounded pending views pass 96 native
    controls and all six profiles. Another 47 private preserved-opacity core controls and 67 complete typed host
    checks pass all x64 profiles. Finish pointer/lifetime closure
    and adjustment/display worker construction before native root activation. Admission/accounting and measured
    G1 remain open ([current evidence](../../TestRuns/DocumentRecordRoots_2026-10-03.md)).
  - [ ] H1 — Construct complete history publication before mutating live state; injected allocation refusal preserves pixels, revision, redo and pending transaction. Include whole metadata/trim peak in every migrated operation's reservation.
    - [x] New/default canvas, New Canvas card and Close Project: optional-document preparation, saved New
      entry and complete draft/history retirement are tracked in
      [DocumentLifecycleWorkers](DocumentLifecycleWorkers_2026-10-02.md). The private primitive passes
      65 independent controls and the complete DocumentTests suite. The primitive passes all six native profiles
      ([record](../../TestRuns/DocumentLifecycleCore_2026-10-02.md)). The reviewed host is activated after private
      258-control qualification; its combined six-profile matrix passes
      ([record](../../TestRuns/P1LifecycleShutdown_2026-10-02.md)). Broader shutdown, admission/capture
      traversal and measured G1 remain open.
    - [x] Immutable views, detached publication and complete aggregate release for basic raster, gradient and pixel-move commits; all profiles pass ([evidence](../../TestRuns/PreparedHistoryPublication_2026-10-01.md)). Other callers remain open.
    - [x] Destructive color/filter and size-card publication and complete retirement qualify in all configurations
      ([color](../../TestRuns/ColorHistoryPublication_2026-10-01.md), [size](../../TestRuns/SizeHistoryPublication_2026-10-01.md)).
    - [x] Merge: metadata-only worker probe, admitted readback/history capture, full survivor publication and CPU
      retirement pass all profiles; 71 document assertions and 17 host modes
      ([evidence](../../TestRuns/MergeHistoryPublication_2026-10-01.md)).
    - [x] Production Undo/Redo: prepare restore, selected target and exact publication/retirement before history moves.
      The implementation has constant-sized authority (including no-document Redo), admitted immutable-history
      capture, worker preparation and nonthrowing swaps. Full Debug preflight passes 63 document assertions and
      28 host ordering, fault and retirement scenarios. Empty stacks/open transactions use a constant-time
      no-op before capture, preserving crop/redo and the existing ignored-key rule. After merge commit `030fb59`,
      the production route qualifies in all six configurations against the same 385 frozen inputs
      ([evidence](../../TestRuns/HistoryRestorePublication_2026-10-01.md)).
      The first preflight passes all 63 document assertions and 28 host scenarios, then exposes an existing crop
      shortcut test's immediate restore assertion. Its exact binary, DLLs, source freeze and logs are preserved
      under `.build/p1-history-restore-first-Debug-crop-failure/`. The test now waits for acknowledgement before
      reading restored dimensions or switching tools; its pixel/history/crop expectations remain unchanged.
      The second full Debug preflight passes, including the unchanged crop test. All five projects include the
      new history module and the string catalog checks the localized notice. The final frozen matrix passes.
    - [x] Imports: preconstruct new-document saved baseline, additional-image entry and retired session/history
      owners before publication. A primary-owned replacement-history primitive passes private strict syntax and
      55 independent baseline/Undo/Redo/fault assertions. The complete session candidate also passes strict syntax;
      49 primary-reviewed session assertions also pass, including isolated active/mask/crop/epoch refusal and
      later saved-revision behavior. After qualified Undo/Redo commit `82f1123`, native import and project-open
      publication is activated. Full Debug preflight is running; actual host fault/retirement execution and
      final frozen configuration qualification remain open. Project replacement covers absent/unknown active
      ids and empty projects. Exact selected host commit stamps are checked before either publication.
      The first native preflight passes 55 history and 49 session assertions, then exposes a new host test
      injecting BeforeSnapshot into a clean replacement that creates no history entry. The exact failed image,
      DLLs/PDB/source freeze and logs remain under `.build/p1-import-history-first-Debug-project-failure/`.
      A real ReplacementSnapshot checkpoint now precedes the worker's replacement-document copy, with twelve
      added first-image/project allocation/cancellation controls. The second full Debug preflight passes 55
      history/61 session assertions and 52 project/149 image-import host cases. The first final matrix passes
      x64 Debug, then stops at Release's ISA parser reporting switch-table data in the pinned DxUi image as
      instructions. Its exact exe/PDB/DLLs, source freeze and logs are preserved in
      `.build/p1-import-history-first-Release-isa-failure/`; parser proof/controls precede another matrix.
      Caller review also finds an unqualified live-context gap: the settled restore authority currently refuses
      replacement over a live panel/stroke/transform/draft and existing imports inside a transaction. Before this
      caller closes, retain original discard-on-success/preserve-on-failure behavior, nested history semantics
      and complete retirement of all replaced preview owners. The running matrix qualifies settled contexts only;
      it cannot close this row or authorize a completion claim while the live-context route regresses.
      A separate file-operation authority now preserves the settled-only Undo contract. The activated live-context
      candidate prepares replacement over an open transaction, keeps nested existing-import openings intact,
      transfers every discarded CPU draft/preview to an allocated retirement holder, and detaches borrowed coverage
      engines on their owner before worker destruction. Eleven independent pending-context refusal/success controls,
      weak-owner release controls, nine actual project contexts and three actual image-import contexts are added.
      The first live-context Debug run exposes a fixture calling the layer-transform API instead of
      BeginSelectionTransform. Its exact image and source freeze are retained in
      `.build/p1-import-live-first-Debug-floating-fixture-failure/`. The corrected run passes all 157 session
      assertions, then exposes host-fixture reuse: CreateDocument preserves a refused panel/transaction, so the
      next floating fixture could not begin. Its exact image/logs/freeze are retained in
      `.build/p1-import-live-r2-Debug-fixture-state-failure/`. Fixtures now explicitly clear the previous session
      before constructing each new case. The focused native build/WARP preflight passes all 70 project-open
      and 155 image-import host cases; no production expectations or literal pixels were changed.
      Parser integration passes 287 synthetic controls, then the first live-context frozen matrix exposes a
      terminal byte table with no following data symbol in Debug's pinned DxUi image. Its exact image/PDB/DLLs,
      parser, freeze and disassembly are retained in `.build/p1-import-history-r2-Debug-isa-failure/`.
      The primary adds the exact common procedure/contribution-end proof, six terminal-span controls and
      stricter malformed-last-entry/procedure-bound controls. All 293 synthetic controls and preserved
      Debug/Release/ASan parser scans pass with the runtime allowlist unchanged. Final qualification and commit
      remain open. The next matrix passes the app scan, then exposes a byte table followed by three-byte NOP
      alignment in HostTests. Its exact image/parser/freeze/disassembly are retained in
      `.build/p1-import-history-r3-Debug-host-isa-failure/`. Four alignment acceptance/refusal controls keep the
      suffix exact and reject repeated padding, changed bytes and an invalid preceding index. All 297 synthetic
      controls and all five executable scans in Debug, Release and ASan Debug now pass. Their compiled
      misplaced-ISA, unapproved runtime owner/function and PDB rejection controls pass unchanged.
      The final clean matrix passes all x64 suites and all three ARM64 clean cross-builds.
      Each x64 suite passes 55 history/157 session assertions and 70 project/155 image-import host cases.
      The same 391 inputs remain unchanged after the complete matrix
      ([record](../../TestRuns/ImageImportHistoryPublication_2026-10-01.md)); no measured G1 claim follows.
    - [x] Selection core: sealed Select All/Deselect and outline-move End/Cancel prepare complete history and
      replaced document/display/origin owners; 312 independent assertions per x64 suite and all ARM64 clean
      cross-builds pass against 423 frozen inputs ([evidence](../../TestRuns/SelectionCommandPreparation_2026-10-01.md)).
      Production command/gesture routing and full metadata capture/accounting remain open.
  - [ ] H2 — Shared immutable document/history record handles and cached owned storage indexes; capture/restore must not copy 10,000 layer records on the owner.
    - [ ] H2.2 — Immutable document record roots. Primary prepares the ownership API in a separate private
      workspace while the selection lifecycle native matrix stays frozen. Capture/read must copy only a root
      handle, including non-const reads; no mutable pointer or iterator exposes a published root. A private
      builder owns all edits and seals a complete replacement before publication. Never mutate using use_count.
      Independent tests must cover 10,000 records without capture copies, retained strings/storage owners,
      old-root lifetime, reader concurrency, cancelled/failed cloning and retry. Native document integration,
      complete reservation/retirement updates, all goldens and six profiles follow the privately qualified
      foundation; full accounting and measured G1 remain separate gates.
      - [x] Seed 519 exact owned inputs into `p1-document-record-roots-proof` and prepare the primary-owned
        immutable root/exclusive builder API. No compiler or native activation has run
        ([candidate](../../TestRuns/DocumentRecordRoots_2026-10-03.md)).
      - [x] Review independent ownership/cancellation/concurrent-reader tests and register them privately.
        Primary adds actual allocator faults for constructor/seal/clone and separate new-allocation-owner
        lifetime controls, retaining standard allocation by default.
      - [x] Initial private foundation: r2 passes 29 root controls and preserves all 5,704 predecessor document
        assertions. The r1 injected Debug-container proxy allocation abort remains archived before repair.
      - [x] Review the writer/pointer/equality inventory; snapshots must retain record-value and ordering semantics.
      - [x] Private r3 qualifies 34 root/equality/capacity controls and all 5,704 preceding document assertions;
        a separate Debug benchmark run checks all 60 paired raw rows and complete result/source content.
      - [x] Native foundation: r1 passes all ARM64 cross-builds and full x64 ASan, then Release's linked ISA
        check rejects the benchmark fixture's compiler-vectorized Guid byte pattern. Exact 637-input/3,587-evidence
        failure is archived before its literal-equivalent repair. Repaired r2 passes all six profiles with 637
        unchanged inputs; its 217 evidence copies and complete Release comparison are independently verified.
        Root capture stays about 0.052 microseconds at 10,000 records; a full changed root still costs about 6 ms.
        This closes the primitive foundation only, not document integration or G1.
      - [x] Prepare and privately qualify pure read spans with array/immutable-root borrowing controls in a second
        isolated workspace. Its Debug build and full document suite pass 12 new view, 34 root and 5,704 preceding
        assertions; all 650 inputs / 153 evidence copies are independently verified. Native activation requires
        the completed repaired foundation. Full writer/record capture migration, H3 accounting and measured G1 stay open.
      - [x] Native read/transfer preparation: pure read spans and exclusive worker-buffer adoption qualify with
        all six profiles against the same 641 inputs. Each x64 suite passes 18 adoption, 12 view, 34 root and all
        preceding document controls. The original Release clipboard/text interference and complete failure are
        preserved; four already passed profiles are retained and Release/Debug rerun without source changes.
        All 220 final evidence copies are independently verified ([record](../../TestRuns/DocumentRecordRoots_2026-10-03.md)).
      - [ ] Full layer-root integration: a separate 528-input seed now has private drafts for structural,
        brush/transform/resize/mask and prepared raster/color publication. Primary reviews four cheaper-agent
        fixture groups and preserves the original retired-layer accessor contract. The complete private Debug
        suites pass all 5,768 preceding assertions plus eight draft/six fixture controls in Debug, Release and
        ASan Debug. Their identical owned sources and all frozen source/evidence copies are independently verified.
        Original compile/runtime failures are preserved. Production metadata/typed-opacity/adjustment/display worker placement, complete
        reservation/retirement, native activation and measured G1 remain open
        ([record](../../TestRuns/DocumentRecordRoots_2026-10-03.md)).
        - [x] Sealed settled metadata core: 98 independent rename/visibility/opacity/blend ownership, history,
          stale-result and failure assertions pass the complete private Debug/Release/ASan document suites.
        - [x] Production settled metadata route: full native Debug, Release and ASan pass 98 document assertions and
          139 host checks across 43 cases, including ordering, message pumping, cancellation, allocation failure
          and retirement, plus all ARM64 cross-builds. The preserved ASan r6 fixture race has an acknowledgement
          fence. All actual r7 runs pass; r8 repairs only the collector's ASan stress-run accounting and independently
          verifies the final source/evidence archive. A
          temporary reference route preserves legal edits during live gestures/nested transactions; it remains
          a separate worker migration requirement. Typed opacity and other record writers remain open.
        - [x] Private typed-opacity core: preview/restore without history and original-to-final publication pass
          112 independent controls and all 5,880 preceding controls in Debug/Release/ASan, with identical 316
          core/test inputs and independently verified archives.
        - [x] Tested typed-opacity host ordering, requested/shown separation, coalescing and late-value
          suppression: the later complete WARP application r6/r7/r8 matrix qualifies 31 typed and 36 context
          controls in Debug/Release/real ASan, with all predecessor labels preserved. The earlier private
          plumbing-only status is superseded by [the typed host matrix](../../TestRuns/DocumentRecordRoots_2026-10-03.md#typed-host-application-matrix-qualified-2026-10-04).
        - [ ] Remaining typed-opacity pointer/lifetime and caller-prerequisite closure, ordinary activation,
          complete current-source qualification, H3/H4 and measured G1. The qualified ordering fixture does
          not close these separate requirements; do not reintroduce its tested ordering as generic missing code.
    - [x] H2.1 — Share immutable past/future container roots: capture copies only root handles, including on
      non-const reads. Every mutation constructs a private replacement before publication; `use_count` never
      authorizes writing a previously shared root. The private strict Debug probe passes 28 assertions covering
      zero handle copies on capture/read, independent integer content, weak-owner lifetime, concurrent readers
      and copy refusal. Native activation, reservations and full configuration qualification remain open.
      Full document snapshots, owner admission traversal and H3 accounting are separate unfinished costs.
      After import publication commit `de10147`, the container roots are integrated with explicit private-root
      growth/retirement bounds in all five projects. The native Debug build and all executable ISA controls pass;
      complete native qualification remains active.
      The first frozen resource matrix passes the complete x64 Debug and Release suites, then ASan's linked
      application fails the ISA parser on a terminal switch remap inside a larger contribution. The exact failed
      image/PDB, original parser, disassembly, source freeze and logs are retained in
      `.build/p1-resources-first-ASan-isa-failure/`. The exact unsigned stack guard and procedure-end proof now
      accept that preserved image with zero violations; all 297 previous parser controls still pass. New rejection
      controls and a fresh complete matrix precede qualification. ARM64 and ASan runtime have not passed this candidate.
      Reviewed private Release microbenchmarks quantify the ownership tradeoff: at 1/100/1000 handles, median
      capture is 6.43/6.81/7.15 ns versus 30.85/694.79/7659.90 ns for copying handle vectors. Every paired content
      checksum matches. Immutable cloning costs more on writes: at 100 handles, push/pop averages 876.70 ns per
      mutation versus 13.36 ns for exclusively owned vectors; oldest-entry erase is 2261.13 versus 292.86 ns.
      These 27 capture and 36 mutation pairs exclude setup, checksum walks and final destruction, and retain
      sources, hashes, raw timings and independent expectations in `.build/p1-history-root-benchmark/` and
      `.build/p1-history-root-mutation-benchmark/`. The extra worker/reference-API write cost is accepted here
      to keep captured roots immutable; it does not qualify document capture, admission, incremental trim or G1.
      The fresh matrix passes all x64 suites and all three ARM64 clean cross-builds against the same 396 frozen
      inputs, with 28 root assertions and 331 parser controls per x64 configuration
      ([qualified record](../../TestRuns/HistoryRootsAndImageThumbnails_2026-10-01.md)).
  - [ ] H3 — Incremental retained-storage accounting and bounded oldest-first trim/retirement; preserve current-document exclusion, identity lifetime and all trim/Undo/Redo goldens.
    - [x] Original history-residency startup/failure/shutdown prefix: normal `Application::Run` starts the source after
      `InitializeDocument`/`ApplySettings` and before startup paths or ordinary input. The actual-Run fixture covers
      normal notification and history-only growth/trim, failure after successful subscription, full subscription and
      owner-route admission failures, startup rollback, joined shutdown, route detachment, repeated shutdown and quit
      isolation (36 controls). The current pushed x64 full-suite Release548, Debug549 and real-ASan550 receipts use the
      same 1,084-input source freeze and include all 120 Application stages; `Main` invokes this self-test
      ([receipt](../../TestRuns/P1WarpLive_2026-10-09.md)). This qualifies the original prefix on x64 only; it does
      not close native ARM64 runtime, remaining direct/reference materializers, incremental H3 accounting/cost,
      measured G1 or P1.
    - [x] Managed immutable index and unique retirement slice passes full Debug r74, Release r75 and ASan Debug r76: 6,729 document / 9,298 application controls, unchanged 79 stages, 46 added document controls and nine owner-policy HostTests controls; the 1,041-copy Debug/Release and 1,048-copy ASan archives verify. Changed-entry membership, live exclusion, partial construction faults and retirement are covered. Worker container clones/sampling/bounds still scan unique owners; late-cache synchronization/reprepare, reference/delta cost, analysis/ARM64, native activation and G1 remain open ([evidence](../../TestRuns/HistoryStorageIndexCandidate_2026-10-05.md)).
    - [x] Cached immutable history-stack metadata/ownership/node bounds and constant-time edit/restore/retirement admission pass full Debug r71: 19 reviewed root-cache and 11 snapshot/history controls preserve the preceding registry. r69 compilation and r70 fixture failures remain archived. This conservative occurrence bound does not close exact identity accounting or late residency; current Release/analysis/ARM64 and native activation remain open.
    - [x] Shape-source trim omission repair passes full Debug r71: both direct/prepared paths charge history-only `shapeImage` and exclude the same live owner; four literal 1 MiB / half-MiB controls pass after the fixture performs a real rename. Original derived-cache equality and legacy goldens remain unchanged. Current matrix and native activation remain open.
    - [x] Qualify `HistoryStorageTopology`: complete iterative nested closure with held raster/layer/selection owners, cached index metadata and fixed recent-root reuse. Worker-prepared snapshots retain it; source matching is constant-time. Reference Begin stays a shared-handle capture; reference trim uses its existing layer scan and dedup set plus one pending-owner vector for complete closure, avoiding temporary topology objects. The mixed cached-history/live-exclusion repair and four literal reference/mixed budget checks pass in full ASan Debug r72 (25 topology/reference checks); the same run qualifies actual sampler Escape after pointer Up. Incremental unique accounting, reference caller migration, mutation cost, late residency and G1 remain open.
    - [x] Cache immutable storage topology on the worker, holding every raster/selection owner used as an identity
      key. Include nested sparse tiles/bases, distinct optional thumbnails and selection capacity without
      repeated layer/point walks. Keep current-document exclusion and numeric legacy charges unchanged. Full ASan
      Debug r72 and full Release r73 qualify this topology implementation on the same frozen snapshot; current
      Debug/analysis/ARM64 matrix, native activation and late-residency synchronization remain open.
    - [x] Integrate the immutable unique-owner `StorageIndex` prototype with changed-entry membership, live-document
      exclusion, oldest-past-then-future trim and retained identity owners. Full Debug r74 / Release r75 / real ASan
      r76 qualify the managed candidate; subsequent full managed caller matrices preserve it. See
      [candidate evidence](../../TestRuns/HistoryStorageIndexCandidate_2026-10-05.md). This closes the duplicate
      prototype row only. Private map cloning, membership/sampling/bound scans remain worker costs; incremental
      charge cost, uncovered arithmetic overflow, production startup, reference writers and G1 remain below.
    - [ ] Complete late sparse-materialization activation and synchronization. The managed nonmovable
      same-thread lease, epoch/notice, worker reprepare/permit, semantic mask thumbnail and several real callers
      are integrated and qualified in the r85–r88 core and later caller matrices. Full Debug r278 preserves these
      controls on its 877-input source; current-source x64 Debug/Release/ASan qualification is recorded in
      [P1WarpLive](../../TestRuns/P1WarpLive_2026-10-09.md). `HistoryResidencySource::Start` is wired into normal
      `Application::Run`; the original startup/failure/shutdown prefix is qualified in the checklist entry above.
      Remaining direct/reference writers and their caller-specific proofs stay open.
      Current ImageCodecs/ProjectStore encoders and Warp root copying use private read bands. Nested sparse tiles
      can still reach `Data()` through `BlitTile`; `TransformEdit::PixelsOf` is another direct materializer. A
      cached topology never authorizes stale resident totals. Keep the actual growth/refresh/trim gates and
      measured reference/delta cost under H3.
    - [ ] Qualify address reuse, checked overflow and oldest-first cases, plus concurrent late sparse materialization
      and resident-charge synchronization before trim preparation/publication. Current image/project encoders use
      private read bands; exercise actual `Data()` materializers. Verify cancellation/allocation refusal preserves
      revision/redo and releases every replaced index owner.
  - [ ] H4 — Lazy image-only thumbnails, eager semantic mask thumbnails, bounded first uploads and chrome/apply measurements; meet the complete measured G1 exit.
    - [x] H4.1 — Image-only codec/producer candidates remove unused eager generation while retaining every mask
      thumbnail. The primary reviewed the cheaper agent's codec tests, retained the original literal mask fixture,
      and added a final cancellation check. Complete producer/validator, accounting and I/O/Undo/Redo qualification
      precedes native activation; uploads and measured G1 remain separate.
      The private nine-group producer suite now has 30 primary-reviewed literal-pixel/equality/history assertions;
      semantic mask tests begin from an independent white mask and require a real new black stroke entry.
      The original retained-thumbnail accounting golden will supply an explicit optional image thumbnail so
      it continues to exercise nonzero retained bytes. Private tests are not yet native-qualified.
      Native codec/producer/validator/reservation changes and those tests are now activated. The first Debug
      preflight passes all 28 container assertions and 29/30 thumbnail assertions, exposing a fixture that requests
      a hide-all mask but expects white. Its exact DocumentTests image/PDB/DLLs, 396-input source freeze and logs
      remain in `.build/p1-resources-first-Debug-mask-fixture-failure/`. The corrected fixture requests reveal-all,
      preserving its independent white-to-black byte expectations and real new-entry requirement. Complete
      configuration qualification remains active; no first-upload or measured G1 claim follows.
      Final qualification passes all six configurations, including all 30 producer assertions, unchanged legacy
      history/retained-byte expectations and eager literal mask bytes in I/O
      ([record](../../TestRuns/HistoryRootsAndImageThumbnails_2026-10-01.md)). First uploads and measured G1 remain open.

Status: ACTIVE. Parent: [UiNeverBlocks_2026-09-22.md](UiNeverBlocks_2026-09-22.md), P1.9.
Owner: `Common/Document.h`, `DocumentHistory.cpp`, session restore and host upload/chrome costs.

The bounded storage/move slice is qualified; [evidence](../../TestRuns/HistoryStorage_2026-09-30.md).
Incremental accounting, thumbnails, uploads and the measured apply target remain active.
The raster-edit integration also identified allocating Begin/End publication as a reliability dependency.
Its new allocation-free admission estimate includes whole document metadata and existing-history trim scratch;
that declaration does not make the owner transaction allocation-safe. H1 must construct every potentially
failing snapshot/index/entry allocation before the live document or history revision changes. Only the resulting
non-throwing ownership transfer may publish. Preparation/capture/admission failure then fails the selected
operation locally with its original document/history intact. Retain the repository's exception policy until
this explicit construction/admission boundary is implemented and qualified; do not add a catch after mutation.

## H1 publication sequence

- [ ] H1.1 — Give history entries immutable shared ownership. Capture a bounded history-state view containing
  retained entry handles, pending/depth state, current revision and a mutation epoch. A worker must not borrow
  the live DocumentHistory. Saved revision is independent: a saved older revision cannot invalidate a valid edit
  or overwrite the owner's later saved state. Preserve existing names, entry limits and undo ordering.
  Native code now implements immutable entries/opening snapshots, state views and prepared edit/begin/end/restore containers.
  Moving a view/publication explicitly removes the source's publication authority. Independent state, nested,
  saved-revision, restore-after-trim, moved-from and construction-refusal controls pass isolated Debug execution
  and strict syntax checks. Native Debug, Release and ASan Debug suites and all ARM64 clean cross-builds pass.
  Basic raster, gradient and pixel-move production commits are activated; other caller groups remain open.
- [ ] H1.2 — Prepare the before/after snapshots, entry/container growth, exact current-document storage
  exclusion, trim decision and retirement owners entirely before publication. Include all record/string/curve,
  storage-index and scratch capacity in admission. Preparation must fail locally on bad_alloc or cancellation;
  no revision, redo, pending transaction or live pixel changes occur on refusal. Zero-change publication keeps
  the current revision and redo. Every stored identity retains its corresponding raster/selection owner.
- [ ] H1.3 — Validate document token and exact history epoch on the owner before either document or history
  changes. Publish only by nonthrowing moves/swaps into already prepared storage. Keep replaced containers in
  the prepared release aggregate until the release lane retires them; no synchronous whole-history destructor
  is hidden inside the apply scope. Worker result wrappers must account for their retained input/result graphs.
- [ ] H1.4 — Wire raster edits/gradients, color commits, sizes, merges/imports, selection and stroke operations
  to the prepared boundary. Nested floating-selection transactions need explicit opening/finish/cancel state;
  they cannot use a catch after clearing pixels or inserting a temporary layer. Direct reference APIs retain
  their behavior, with production routing audited before closure.
  Raster, gradient and lifted-pixel production commits bind their complete history publication to the prepared
  target and captured token. Owner application swaps retired layer/selection/history owners into the job
  aggregate, which retires on a worker. Pending gradient/move previews and brush-commit owners also transfer
  instead of being destroyed during apply; independent weak-owner controls pass in native DocumentTests.
  Finishing a transaction reuses its immutable opening snapshot rather than copying layer records again.
  Their value document capture remains an explicit H2 cost. The complete native configuration matrix passes
  for these three groups; the other caller groups remain open.
- [x] H1.5 — Prepare Undo/Redo restore, destination stack capacity and trim before removing the source entry.
  Even when trim discards the destination entry, the independently owned restore survives. Test each allocation
  refusal boundary and stale history epoch against literal revision, name, redo, depth and pixel expectations.

H1 removes failing construction from publication; it does not establish H2's constant-time document capture or
H3's incremental accounting. A temporary immutable-entry view can still contain value document snapshots.
Do not close H2 while any production capture/restore still copies 10,000 layers on the owner, or H3 while every
edit still rebuilds a set of all retained storages. Their measurements include metadata traversal and release,
in addition to pixels, and retain the original trim/current-document exclusion goldens.

### Native foundation qualified, 2026-10-01

Before owner capture, each migrated commit reserves its preparation/history peak and supplies a conservative
release bound for current, pending, past and future owners, plus private output and retired-preview headroom.
The bound remains in the aggregate through failed capture, preparation, apply/drop and worker destruction;
accounting cannot lower it below the 16 MiB release threshold. Raster construction caches complete descendant
capacity and node-count upper bounds, including a possible sparse assembly. Reads do not traverse or
materialize nested sparse graphs. Sharing is deliberately overcounted; overflow or depth above 128 refuses
admission. This is a transient release bound, separate from the legacy retained-history budget.

New state/publication/fault/raster-publication/capacity controls pass native DocumentTests. Full native
Debug, Release and ASan Debug suites and all three ARM64 clean cross-builds pass against the same 362 frozen
inputs; [evidence](../../TestRuns/PreparedHistoryPublication_2026-10-01.md). H1 remains open for the other caller groups and
production Undo/Redo; H2/H3/H4 and every measured apply/G1 claim remain open.

### Qualified H1.4 caller: destructive color/filter commits

- [x] Borrow immutable panel metadata to estimate the whole color/history peak before copying worker inputs.
  Capture the source document, opening token and history handles only after the full reservation exists.
- [x] Bind ready color output to its complete worker-built after-layer/history state and captured generation.
  Owner apply validates the panel/settings token and exact history epoch, then swaps layer/settings/panel owners.
  Unchanged, donorless, cancelled, failed, replaced and newer-panel outcomes retain their existing semantics.
- [x] Keep the retired panel lookup/coverage, old settings and source/history owners in the complete worker-release
  aggregate, including failed capture and every history-allocation checkpoint.
- [x] Pump admitted capture in the native harness, inject all five history failures and capture refusal,
  preserve seeded redo and source pixels, reject changed epochs and run the complete frozen configuration matrix.
- [ ] Migrate adjustment-layer OK/Cancel transaction closure separately; its live metadata semantics must remain
  intact. Production destructive-color activation alone cannot close this row or the other H1 caller groups.

The native full x64 suites and ARM64 clean cross-builds pass against the same 366 frozen inputs. All 45 independent
prepared-color assertions and actual host fault/retirement cases pass; [evidence](../../TestRuns/ColorHistoryPublication_2026-10-01.md).
The initial continuation raw-wait and Release scheduler-wake failures are preserved there. Suspension tests now
check the actual hook and include an unrelated memory wake while the worker remains held. An intermediate run
with changed inputs stopped and is excluded; only the final r3 six-configuration matrix qualifies this slice.
Adjustment-layer closure, other H1 callers, H2/H3/H4 and measured P1/G1 performance remain open.

### Qualified H1.4 caller: size-card commits

- [x] Reserve the whole resize/history peak before capturing the source, token, active id and history handles.
- [x] Bind the complete resized document to detached history publication; validate full document token and history
  epoch before nonthrowing document/draft swaps. Preserve active layer, mask target, crop and existing C16 goldens.
- [x] Transfer retired document, lasso and selection-move owners into the job's worker-release aggregate, including
  failed capture and cancelled/stale preparation. Include retained draft capacity in its pre-capture release bound.
- [x] Exercise all five history allocation failures, capture refusal and epoch-only invalidation through the host;
  retain seeded redo, literal source pixels, barrier/reservation ownership and off-owner aggregate destruction.
- [x] Integrate the draft, pass the frozen native configuration matrix and update the owning C16 contract/evidence.

The final frozen x64 suites and ARM64 clean cross-builds pass 50 independent assertions: literal
extension/nearest pixels, masks/selection/crop and active-only authority,
Undo/Redo, resolution-only, nested/no-op history, moved/foreign authority, five allocation and five cancellation
stages, and lasso capacity/owner transfer. The native host now adds the admitted capture/history/epoch faults and
off-owner aggregate release. The complete native matrix qualifies this caller; [evidence](../../TestRuns/SizeHistoryPublication_2026-10-01.md).
The first frozen sizing run passes Debug and Release, then fails ASan's existing folder save-in-place scenario.
Its original image/source identities and failure log are preserved. The initial fixture-save trace does not cover
the asynchronous in-place write, so a fixed-capacity trace-only harness gate now records that actual worker's
IO stages, native values, status and terminal receipt. A rerun passes this scenario; its original cause remains
F6 work. The final distinct r2 six-configuration matrix qualifies the sizing source against the same 370 inputs.

## Immediate implementation

### H1.6 adjustment-panel transactions — next caller

- [x] H1.6a — Prepare adjustment-layer OK as an admitted metadata job. Capture whole-document authority,
  exact panel opening/settings sequence, active/mask/crop and immutable history only under its complete reservation.
  On the worker, copy final records with per-layer cancellation, install the edited adjustment and prepare End.
  Validate all authority/epoch fields before nonthrowing document/history/panel/original/display swaps; retain
  every replaced owner until worker aggregate retirement. Preview-off OK still applies editable settings.
- [x] H1.6b — Prepare Cancel restoration and End without owner snapshot/index construction. Cancellation of an
  active OK must acknowledge its drop before that same panel can close. Coalesce repeated Cancel by opening;
  keep younger writes/replacements behind the closure and preserve the current draft on construction failure.
  Audit every production close/discard caller; no dependent action may treat accepted closure as completed.
- [x] H1.6c — Prepare transaction opening before installing a live adjustment panel. Preserve its fixed histogram
  source, original parameters and named opening snapshot, including nested history. Keep below-composite GPU
  acquisition with P1.3 rather than hiding synchronous readback in this metadata migration.
- [x] H1.6d — Independent literal parameter/pixel/history tests for preview on/off, no-change/redo, nested
  transactions, all adjustment kinds, later saved revision, stale panel/settings/session/target/epoch and
  each capture/construction/cancellation boundary. Native host gates cover OK/Cancel/reopen, queued save/close,
  exactly once publication, barrier/reservation acknowledgement and off-owner retirement.
- [x] H1.6e — Complete frozen x64 suites/ARM64 cross-builds, update owning contracts/evidence and commit/push.
  Constant-time whole document capture, other readback/H1 callers and measured G1 stay separately gated.

Opening and metadata OK qualify in all six clean configurations against 408 frozen inputs: 515 closure,
210 opening and 58 nested assertions, plus 74 closure and 72 opening host cases in each x64 profile
([evidence](../../TestRuns/AdjustmentOpeningAndOkHistoryPublication_2026-10-01.md)). Production Cancel and the
combined caller gates now qualify against 414 frozen inputs in all six configurations: 264 dismissal assertions,
70 Cancel and the existing 146 opening/closure host rows per x64 profile
([record](../../TestRuns/PanelCancellationHistoryPublication_2026-10-01.md)). Measured owner costs remain G1 work.
Native Cancel integration now runs through the owned queue and prepared metadata/destructive publications. The first
Debug preflight passes those 264 assertions and 58 strengthened nested assertions, plus 45 failure/coalescing and
four early/late selected OK cases. It exposes an invalid new save-outline expectation, corrected against the existing
project format contract with its failed image/414-input source and logs preserved. The second preflight captures
an unexplained existing exploded-save failure; bounded observer diagnostics now report any recurrence without
changing save behavior or expectations. The third passes all 58 Cancel host rows, then catches DocumentGoes
clearing queued inputs before replacement could retire them Stale. That production routing error is fixed with
the original terminal expectations intact. The fourth full Debug preflight passes. Twelve additional selected
card/New/Close success/capture-failure cases are included in the final 414-input matrix, whose Debug and Release
suites pass. ASan then fails an existing Copy Merged/system-clipboard assertion before the new Cancel host suite.
Its exact images/414-input source/freeze/logs are preserved; bounded assertion diagnostics retain the original
expectations and add no write/retry. The second final matrix records native destination-to-backup rename error 5
in Debug's existing durability test. Bounded prepublication retry/cancellation and actual reader/timer controls
pass the fifth full Debug preflight and the fresh final six-configuration matrix
([qualified evidence](../../TestRuns/PanelCancellationHistoryPublication_2026-10-01.md)).

Native preparation now includes complete OK/Cancel document/history/panel/original/display ownership and a
separate opening publication that prepares Begin before installing the panel. The first native Debug preflight
passes the 500 independent closure assertions. Reviewed private opening tests add 203 assertions with literal
kind/settings/source/placement/seed expectations, cancellation/allocation checkpoints and stale authority.
Metadata OK and opening production routing now pass the frozen matrix. Closure has 74 actual host cases and
opening has 72, including a younger queued rename that must wait for opening and subsequent closure. Reviewed
nested tests add 58 independent assertions. The first host run exposed an invalid default-HSV no-op fixture:
the existing conversion promotes absent optional settings to explicit defaults, a real stored-record change.
Separate implicit-default promotion and genuine explicit-record no-op cases preserve that behavior. Review also
requires acknowledged End to advance Generation even for unchanged metadata, expiring old whole-document tokens;
the closure suite now has 515 assertions, including exact same-sequence settings refusal and later saved-revision
controls. Opening has 210 assertions. The original executable, PDB, dependencies, freeze and failure log remain
under `.build/p1-adjustment-first-Debug-hsv-noop-failure` (executable SHA-256
`19028BDBD6FA7827B7A2D01C5D24B67C333AB84D339FA13F79FA48A0A8D70C12`).
The r3 build stopped on a missing direct ChromeHost include; its diagnostic log is retained. Later preflights
corrected the new open-transaction redo predicate and bottom-first layer fixture; their exact failed images and
inputs remain in the linked evidence. Production Cancel is qualified in its separate record; measured owner costs remain pending.
Test review removes an unreachable BeforeSnapshot closure checkpoint:
End reuses the already retained opening snapshot. The global filter template must remain unchanged in this
adjustment route; Undo/Redo restores document adjustment metadata, not that separate template.
Cancellation routing must own an opening-specific request, coalesce repeated cancellation, wait for any prior
OK drop acknowledgement and preserve FIFO ordering for younger actions. Every caller that immediately reads
closed-panel state must wait for the receipt; card/tool/document changes cannot invalidate a still-running
closure or treat request admission as publication. File replacement retains its separately qualified atomic
discard-on-success behavior. The combined caller/host proof now passes in all x64 configurations.

### H1.5 production Undo/Redo — private proof

- [x] Capture constant-sized session/serial/generation/restore/active/mask/crop authority, including a session
  without a document. Capture immutable history handles only after whole restore/trim admission.
- [x] Prepare restored document, destination history and selected-id set on the worker. Check authority and exact
  history epoch before nonthrowing history/document/display swaps; retain old owners in the release aggregate.
- [x] Preserve mask targeting, actual-restore crop cancellation, no-entry crop retention, current/saved revisions,
  single-use and moved/foreign refusal. Empty stacks and open transactions do not capture or prepare a restore.
- [x] Pass 63 isolated document assertions and strict module/host syntax. Both directions exercise all three restore
  allocation/cancellation checkpoints; later saved revisions and a newly begun unrecorded lasso cannot corrupt state.
- [x] Activate production command routing and the 28 actual host cases after merge qualification. Test a younger
  edit, no-document Redo, selected receipt lineage after document removal, refusal/epoch/generation/target/crop/lasso,
  late cancellation, no hidden Present and worker retirement. Qualify native suites and all ARM64 cross-builds.
- [x] Record exact frozen inputs/binaries, update the durable history/host contracts, commit and push this slice.
  H2/H3/H4 and measured apply/G1 remain separate gates.

### Next H1.4 caller: merge readback and publication

- [x] Move merge source capture after metadata-probe admission, then plan on the worker. No plan succeeds without
  pixel allocation or GPU work, including a 30,000-square blank canvas. A ready plan keeps its immutable source
  and selected ordinal while a separately admitted record reserves full readback, crop/thumbnail, survivor/clipping
  graph and history/trim peaks before history capture.
  Keep the selected ordinal and exact generation/token through admitted capture, GPU tiles and final apply/drop.
- [x] Prepare the complete survivor document, clipping-source rewrites, merged layer, active id, selected-id set
  and history on the worker. Preserve D7 budget refusal, insertion anchor, parent/group and clipping goldens.
  No owner apply may allocate an unordered set, rebuild survivor records or call allocating Begin/End.
- [x] Retain CPU snapshot/readback/prepared result/history and retired document/selected owners in complete release
  aggregates. GPU mapping and utility-target acknowledgement stay on the owner; job retirement cannot move D3D
  cleanup to a worker. Device restart releases CPU previous-attempt state on a worker and never publishes partial pixels.
- [x] Validate exact document generation/restore serial, history epoch, acquisition and readback device authority
  before nonthrowing publication. Explicitly define whether CPU-only processing occurs after final map acknowledgement;
  device changes after that detached boundary cannot invalidate already copied bytes. Update the existing rebind gate
  if its held-mapping predicate changes, preserving bounded restart and no writer restart requirements.
- [x] Add literal merged pixels, clipping/group survivors and Undo/Redo, all five history fault stages, source-capture
  refusal, cancellation/epoch invalidation and off-owner aggregate retirement. Qualify full configurations and update
  the owning rendering/document/history contracts before checking this caller complete.

At the start of this slice, `UtilityReadbackJob` captured a value source/token before submission, prepared only the merged pixels,
and let `ApplyPreparedMerge` allocate survivor sets/vectors and history on the owner. `DocumentReadbackJob` also
owned value CPU document storage beside owner-only GPU cleanup. Restructuring preserved the shared
export/adjustment readback lifecycle.

The primary-owned merge draft now passes strict module/host syntax with a cheap descriptor and admitted capture,
complete detached survivor/history publication, nonthrowing owner swaps and separate CPU retirement. Existing
export and adjustment-opening routes retain their current lifecycle. Independent agent tests were corrected
against actual stacking order and valid premultiplied pixels before execution. All 48 focused assertions pass,
including group/transparent behavior and D7 refusal using a physically tiny large sparse source. A cheaper agent
adds 23 independent immutable-planner/no-plan/group/multiselection/cancellation assertions, reviewed and executed
by the primary. The planner expands selected descendants once and validation indexes source ids once. The new
metadata-only probe preserves one history ordinal across readback admission; its source/cancel/stale/refusal and
huge no-plan host scenarios were initially draft proof, with native runtime qualification still pending.
The reviewed draft was integrated after `c2e2904`; native
Debug preflight passes all 71 document assertions and 17 actual host scenarios. The first frozen matrix stops
in Debug HostTests after `generation.old-submit` and a generic gate timeout, before merge tests. Its exact binary,
source freeze and logs are preserved under `.build/p1-merge-history-first-Debug-failure/`. The old suspension helper
still assumed one scheduler wake proved completion; memory notifications may arrive first. It now waits for the
actual hook, retains the same 10-second failure bound and adds an unrelated-memory-wake control and labelled wait
diagnostics. The original log does not identify which generic wait expired. The r2 matrix passes CPU suites and
records all 17 merge host modes, then stops because Windows PowerShell reports an empty process ExitCode after
the bounded self-test wait. The actual child result is unknown and that run is excluded. Its exact image, runtime
DLLs, source freeze and logs are preserved under `.build/p1-merge-history-r2-Debug-observation-failure/`.
The runner now retains each asynchronously waited process handle before waiting and explicitly rejects an empty
crash-probe result. The preserved r2 image completes a fresh pinned-handle self-test with exit 0; this does not
retroactively qualify the unknown first child result. The distinct r3 matrix then stops in Debug IoTests at
`rotation-fault.timer-create-failure-keeps-native-child-lock-error-and-backup`, before the application self-test.
Its image, runtime DLLs, freeze and logs are retained in `.build/p1-merge-history-r3-Debug-rotation-failure/`.
Failure-only diagnostics now identify every status/native-error/hook/tree/staging predicate; the original assertions
and policy are unchanged. All 12 focused full I/O runs pass without reproducing or explaining the original failure.
That cause remains F6 work. The r4 matrix passes Debug CPU suites, WARP self-test and kernel smoke, then stops
because the nested UI-smoke wrapper reports an empty process ExitCode. The exact image, runtime DLLs, freeze,
wrapper and logs are preserved under `.build/p1-merge-history-r4-Debug-smoke-observation-failure/`; printed
operation results do not qualify an unknown child exit. The UI-smoke and gradient benchmark wrappers now retain
their process handles, and the CRT expected-failure control explicitly rejects a missing exit code. A fresh
26-operation UI-smoke preflight passes. The distinct r5 frozen six-configuration matrix is next; only its final
results may qualify the integrated merge slice. The final r5 matrix passes all three x64 gates and ARM64
cross-builds against the same 379 inputs; [evidence](../../TestRuns/MergeHistoryPublication_2026-10-01.md).
Both probe and readback use complete deferred aggregates. The worker builds the
composition subset, avoiding another owner-side layer copy; cached display ownership also transfers on publish.

Published selection loops already share immutable storage and cache point counts/bounds. Cache their vector
object, outer retained capacity and inner retained capacities when the producer owns them. Opaque allocator
and shared-pointer control-block bookkeeping stays covered by global headroom. A read must not walk the outline.
History counts each immutable selection identity once across before/after and past/future, excluding the live
document's selection identity. Geometry equality still preserves no-op/redo even when separately allocated
equal loops have different retained capacities. The legacy 256 MiB raster trim expectation remains unchanged.

`End` moves its completed pending snapshot. Undo/Redo copy only the restore snapshot that history must also
retain, move the entry into the destination stack, and trim using the separately owned restore. Never return a
reference into a stack that trim can erase. Session restore receives ownership of that snapshot and moves its
document into place after checking old/new identity and mask-target conditions. Current revision, saved revision,
entry order, pending-edit cancellation and active layer/mask/crop behavior remain unchanged.

## Tests and limits

Use a small independent fixture where Undo trimming removes its destination entry, then verify the returned
document/revision/active id remain valid. Verify normal Redo ownership and revision restoration. Count shared and distinct selections across
history/live states, spare reserved capacity and a tiny budget that drops a selection-only retained entry.
Keep all existing numeric history/session/pending-edit goldens unchanged. Run all required x64 configurations
and ARM64 cross-builds; native ARM64 runtime requires another host. Timing is evidence, not an ordinary test gate.

This first slice still traverses raster identities for retained accounting. It does not claim incremental
accounting or the 2 ms apply/G1 target; continue the later checklist rows with measurements before closure.

Sizing authority also compares the captured active layer before publishing history: changing only active selection
can leave the document token current, and must still refuse a candidate with stale Undo/Redo selection. Two independent
controls cover that case. The final r2 frozen six-configuration matrix qualifies these controls.
