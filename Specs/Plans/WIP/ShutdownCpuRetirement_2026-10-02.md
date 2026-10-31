# Shutdown CPU retirement (P1.7 F5d)

> **HOLD** with [P1](UiNeverBlocks_2026-09-22.md) since 21 October 2026. Do not resume this file until that plan leaves HOLD. Checked boxes below are historical qualification, not permission to continue.

## Implementation checklist

- [x] Reproduce the standalone scheduler failure with its own deferred-release sink, real 16 MiB CPU payloads,
  running/queued/memory-waiting/undrained jobs and an owned UI action; retain the exact failed sources and binary.
- [x] Prepare a private two-stage close: join producers, drop owner-affine wrappers through the installed sink,
  then acknowledge CPU retirement on a preallocated MTA callback before pool/apartment/event teardown.
- [x] Private complete HostTests passes; all original lifetime controls and 28 added checks pass.
  The original failure has six failed controls, including early action reservation release; it is retained.
- [x] Extend private controls to ordered final hooks, stopped idle pools, repeated Close, reuse refusal and
  invalid initialization. Final retirement runs despite requested cancellation.
- [ ] Extend controls to deferred suspended records, prequeued release nodes and initialization API failures.
  - [x] Activated suspended-record and prequeued-release controls require the scheduler's own sink, real 16 MiB
    payload bytes, exact-once final MTA destruction, joined wrappers, suppressed callbacks and retained reservation/
    wake ordering. Primary review corrected the entered gate, stop/return race and an owner-only reservation
    query inside the draft's worker destructor. Focused native Release r4 passes all 34 controls and the complete
    HostTests suite. Clean r7 passes all ARM64 cross-builds and these controls in x64 ASan HostTests, but later
    stops at an independent clipboard-transfer assertion in IoTests. Fresh r9 qualifies this scoped control
    set in all six profiles; initialization API faults remain open
    ([record](../../TestRuns/P1LifecycleShutdown_2026-10-02.md)).
- [ ] Activate and test the real application shutdown with its own sink, before/after-ready lifecycle work,
  every borrowed graphics owner and GPU lease lifetime; no nested-host surrogate can close this gate.
  - [x] A1 — Add borrowed shutdown hooks to the scheduler's acknowledged close boundary: joined producers,
    owner resource detachment, complete session CPU retirement on MTA, then final deferred/recycle drain.
    Hooks allocate nothing, cannot run twice and keep reservations/events/apartment alive through acknowledgement.
  - [x] A2 — Before ordinary self-test initialization, drive standalone Application owners with their own sink,
    hidden HWND/WARP/chrome, real retained document/history/private clipboard and a graphics-backed draft.
    Gate selected lifecycle work before/after preparation and prove callbacks suppressed, exact resource ordering,
    worker retirement and empty tickets/barriers after actual Shutdown. Preserve an original failing run first.
    The first standalone actual host reaches its before-preparation gate and confirms a live draft remains
    after Shutdown acknowledgement. Its test cleanup then dereferences a reset owner in its scope guard; this
    fixture crash, minidump, 552 inputs and 17 evidence entries are retained as
    `p1-app-shutdown-draft-app-session-owner-retirement-failure-r1`. The guard now checks for an owner before
    cleanup so both phase cases can independently reproduce the CPU-root failure. No production fix is claimed.
    The corrected original r2 exits normally with two failures among 26 controls: the CPU draft survives both
    before/after-preparation Shutdown acknowledgements. Its 552 inputs and 19 evidence entries are retained
    as `p1-app-shutdown-draft-app-session-owner-retirement-failure-r2`.
    The private candidate adds ordered borrowed close hooks, owner coverage detachment and explicit session
    CPU-root release on the final MTA callback. Whole-session move was rejected by its noexcept proof because
    MSVC hash-container moves may allocate. The candidate instead clears owning roots without reconstructing
    default settings or allocating during close. Its first two compile failures (the move proof and a missing
    assertion header) are retained before repair; application proof and residual host capacity costs remain open.
    Candidate r3 passes 36 standalone actual-host shutdown controls and 26 scheduler hook controls, but the later
    ordinary full self-test fails at busy-indicator retirement. Exact 553 inputs and 32 evidence entries are
    retained as `p1-app-shutdown-candidate-app-shutdown-full-host-failure-r3`. Diagnostic-only r5 reports
    settlement enum value 2 with no applied result; its 553 inputs and 53 entries are retained as
    `p1-app-shutdown-candidate-app-shutdown-busy-timing-failure-r5`. The full candidate remains unqualified.
    R6 proves the gate worker returned successfully after its release event. Source verification identifies
    settlement value 2 as `Quit`, correcting the initial timeout interpretation. The fixture's filtered quit
    removal left a quit for the later owner's completion pump. Its exact 553 inputs and 65 evidence entries
    remain as `p1-app-shutdown-candidate-app-shutdown-completion-delivery-failure-r6`. R7 drains the full
    queue after each destroyed standalone owner and asserts that its quit is consumed before the next host.
    R7 passes 38 standalone controls and progresses beyond busy-indicator completion, then reaches two obsolete
    preview shutdown observations that expected the live session to remain populated after Shutdown. Its
    553 inputs and 77 entries are retained as `p1-app-shutdown-candidate-app-shutdown-preview-authority-failure-r7`.
    R9 preserves the exact original source/generation/history check on the joined owner before CPU retirement,
    adds an empty-session/advanced-authority check after retirement and checks the retained literal source pixel.
    Existing legacy pixel/history expectations are unchanged; the intervening local-name compile refusal is
    preserved separately. R9 then reaches two delayed-clipboard shutdown observations with the same obsolete
    post-Shutdown populated-session assumption. Its exact 553 inputs and 95 evidence entries are retained as
    `p1-app-shutdown-candidate-app-shutdown-delayed-clipboard-authority-failure-r9`. The original source/history/
    layer-count/Undo check now runs at joined owner detachment, before CPU retirement. Empty-session and retained
    literal-pixel checks run after acknowledgement; Cancel/stale/prefill expectations remain unchanged.
    R10 passes the complete private Debug HostTests and hidden WARP application: 38 standalone shutdown controls,
    26 scheduler-hook controls, 112 preview controls and 45 delayed-clipboard controls, with unchanged legacy
    goldens. Its exact 553 inputs and 107 evidence entries are retained as
    `p1-app-shutdown-candidate-app-shutdown-qualified-r10`. The reviewed sources are integrated into the native
    tree with native project registration. Clean combined r5 passes ARM64 Debug/Release/ASan builds, then stops
    at ASan's ISA gate before runtime. Its exact 576 inputs and 292 evidence entries are retained; compact
    stack-remap proof passes 56 new controls, all existing parser controls and the unchanged original-image
    recheck. The later native r8 passes all ARM64 cross-builds and the complete x64 ASan suite, including the
    38 actual-host/26 hook/34 suspended-release controls. Release then stops at independent clipboard fixture
    acquisition before its application run, and Debug does not run. Exact failure evidence is retained
    ([record](../../TestRuns/P1LifecycleShutdownFailure_2026-10-02.md)); a full native matrix and broader A3 remain open.
    A separate read-only ownership review confirms the selected/collapsed hash bucket capacity remains after
    `clear()`. That residual cleanup requires measurement or a complete ownership transfer before closing A3.
  - [ ] A3 — Cover failed initialization, undrained/suspended GPU/event/clipboard leases, live brush/warp/heal and
    first/second shutdown calls; qualify six profiles and measure remaining hidden owner cleanup.
  - [x] A3b — Add eight startup-only checkpoints after successfully acquired processing
    stages, sixteen reached retry event/registration faults (each of eight slots), full-success and never-started
    controls. Each standalone Application owns its own trace/pool/sink; prove exact HRESULT and visited prefix,
    no later stage, first/second Shutdown, once-only MTA hooks when a pool exists, empty ownership/routes and
    preserved caller apartment. Checkpoints prove prefix cleanup, not actual failures inside the eight stages.
    Retry controls use the existing queue fault seam before the selected native event/registration call after
    preceding slots succeed. Genuine scheduler/pool/GPU/allocation failures, graphics/lease/live-draft coverage
    and timing remain open. Primary owns this lifecycle implementation; preserve and review private proof before
    native activation. The complete private Debug WARP application passes all 390 processing controls and unchanged
    application suites at `2026-10-02T13:14:02.7568114Z`. Exact 561 inputs and 23 evidence entries are byte/SHA
    verified in `p1-processing-init-private-r1-qualified` before activation. Primary formatting refinement uses
    bounded `format_to_n` labels and separate local declarations; case expectations and production default
    startup behavior are unchanged. The reviewed six-file native candidate is activated; skills/format, all
    x64 full gates and ARM64 cross-builds remain required before this slice can complete or commit.
    Follow-up primary review found the older shutdown test configures stdout buffering after the new processing
    suite has already written. Move harness buffering to Main before every standalone test and remove the late
    reconfiguration. The correction is staged privately while r1 remains frozen; preserve r1 evidence before
    activation and fresh qualification of the final candidate. No runtime failure or production image bug is
    attributed to this test-logging issue.
    Native r1's full x64 ASan and all ARM64 builds pass; Release's actual application self-test then crashes with
    `0xC0000005`, and x64 Debug does not run. Before repair, retain 584 frozen inputs, 230 evidence files and the
    actual PID 108148 dump in `p1-processing-init-native-r1-failure`, plus 162 supplementary build-tracking
    entries. The dump faults in Shutdown's old-layout scheduler access; its PDB/disassembly and source/object
    timestamps show the Release `Application.obj` predates this layout change. Private activation preserved
    source timestamps older than the object, so incremental qualification was invalid for this mixed image.
    No production lifecycle conclusion is drawn from that run. Activate early stdout configuration separately
    and force fresh rebuilds of all six profiles before qualification; buffering is not the identified cause.
    R2 stops at Main's formatting gate before any build/runtime; its 584 inputs and seven logs/receipts are
    retained as `p1-processing-init-native-r2-format-failure` before formatting correction. R3 forces every
    profile rebuild with unchanged native inputs throughout. The complete matrix passes at
    `2026-10-02T14:17:48.4508939Z`: all full x64 suites and ARM64 cross-builds, 390 processing and unchanged
    52 shutdown controls in each x64 application. Exact 584 inputs and 329 evidence entries are SHA/byte verified
    before the next native change. The current Release PDB has one 44920-byte Application layout; its object
    was rebuilt after matrix start. Only this acquired-prefix slice completes
    ([record](../../TestRuns/P1ProcessingInitialization_2026-10-02.md)); broader A3 remains open.
  - [x] A3c — Prepare separate standalone live-owner controls for ordinary/mask Paint and Erase, all three Heal
    modes, Clone, image/mask Blur and both Warp modes, with own hidden WARP/chrome/pool/sink and lifecycle work
    held before/after preparation. Weak observations must establish the exact draft remains alive through joined
    owner detachment and expires on acknowledged final MTA retirement; do not retain a surrogate strong owner.
    Preserve original source/history observations before retirement and independent literal pixels afterward.
    Borrowed coverage engines must detach on owner before CPU destruction. This scope does not substitute for
    the separate mapped GPU/event/clipboard lease and initialization API fault controls or measured shutdown cost.
    Primary owns observation APIs and lifecycle hooks; delegate bounded case construction, then review/execute
    private proof before native activation. Keep the running A3b native source freeze unchanged.
    The cheaper agent repairs the isolated 12-case/two-phase draft. Primary review strengthens exact target,
    transform/mask and history observations; clears the harness history handle while session roots still own
    it; proves an engine is borrowed before owner detachment; and observes brush and Warp expiration inside
    the actual final MTA hook with a weak-only probe. The planned 432 assertions retain independent literal
    pixels and once-only/STA/source/ticket ordering checks. A separate private complete Debug build begins;
    native activation and GUI execution wait for the frozen A3b run and its verified archive.
    The complete private Debug WARP application passes all 432 live-owner, unchanged 390 processing/52 shutdown
    controls and existing suites. Exact 562 inputs and 25 evidence entries are byte/SHA verified in
    `p1-live-shutdown-private-r1-qualified` at `2026-10-02T14:28:34.7702547Z` before native activation. The reviewed
    eight-file candidate is activated with current write timestamps and relative native project registration.
    All six forced-rebuild profiles and exact native control verification remain required before this slice
    completes or commits. Other live owners, lease/API fault coverage, hash capacity and timings remain open.
    The final native matrix passes all six forced-rebuild profiles at `2026-10-02T15:06:20.9478751Z` against
    585 unchanged inputs. Each full x64 suite passes the 432 live-owner, 390 startup and 52 original shutdown
    controls; all ARM64 cross-builds pass. Exact sources, images, PDBs, harness captures, controls and receipts
    are byte/SHA verified in `p1-live-shutdown-native-r1-qualified`: 585 inputs and 327 evidence entries,
    archived at `2026-10-02T15:07:11.8094238Z` before further builds. Only this scoped A3c gate completes
    ([record](../../TestRuns/P1LiveShutdown_2026-10-02.md)); full A3 and G1 remain open.
  - [x] A3d — Drive real standalone Application shutdown while a DocumentReadbackJob worker holds its actual
    mapped GPU rows, both on the bound device and after device retirement; verify literal bytes before/after
    cancellation, joined worker before owner acknowledgement, stale lease refusal and off-owner CPU retirement.
    Also cover the actual RetryWaitQueue callback held before readiness publication and ready-but-undrained work.
    Release the held callback from a separately entered worker on the scheduler shutdown event, without pumping
    from Shutdown or its hooks. Verify producer/receiver order, original ticket/reservation, suppressed useful
    callbacks, empty routes and repeated shutdown. Primary prepares this in a separate private tree while A3c
    remains frozen. Queue-only tests and test-owned surrogate mappings cannot qualify this application gate.
    Clipboard transfer leases, genuine initialization API faults and measured shutdown cost remain separate.
    The first complete private compilation reaches link, then fails with `LNK1104` opening the shared Debug
    `DxUi.lib` while the native Debug profile rebuilds that same dependency. No private application ran.
    Exact 563 inputs, 171 evidence entries and object/link tracking are preserved before repair in
    `p1-lease-shutdown-private-r1-link-failure` at `2026-10-02T15:00:36.2808967Z`.
    Serialize dependency-mutating builds as well as GUI runs; the native Debug build links successfully and
    continues its gates. Primary review also consumes the auto-reset scheduler wake while the retry producer
    is held, so the ready-undrained assertion observes its actual new readiness signal, and uses the stop token
    acknowledgement rather than racing the earlier shutdown event signal. Private r2 remains pending.
    The serialized r2 rebuild and complete private Debug WARP application now pass: 56 map/retry checks,
    unchanged 432 live-owner, 390 startup and 52 shutdown controls, plus all existing application suites.
    Exact 563 inputs and 40 evidence entries are byte/SHA verified in
    `p1-lease-shutdown-private-r2-qualified` at `2026-10-02T15:15:18.6037552Z` before activation. Seven reviewed
    native files are activated with current timestamps and relative project registration; all six forced-rebuild
    profiles are required before A3d completes or commits. Queue retirement is controlled explicitly; no real
    driver device-loss is claimed.
    Native r1 passes all ARM64 cross-builds, then stops at the x64 ASan linked ISA check before runtime.
    Release/Debug do not build or run. Exact 586 inputs and 243 evidence entries, including the ASan image,
    PDB, objects, tracking and extracted listings, are preserved before repair in
    `p1-lease-shutdown-native-r1-ASan-isa-failure` at `2026-10-02T15:29:28.6539347Z`.
    The reported bytes are a DWORD target inside `Application::EndCanvasEdit`'s eleven-entry terminal switch
    table. Its observed CMP/MOVSXD uses matching RSP+disp32 slots; the scanner previously proved only disp8
    terminal guards. A private narrow extension passes 54 independent positive/refusal controls; original-image
    recheck, native activation and fresh six-profile qualification remain required. No runtime allowlist changes.
    Original-image r2 has zero ISA violations but fails an overly narrow recheck expectation: two `<unknown>`
    prefix rows also become proved data. Its ten proof inputs and five evidence entries are retained before
    correcting the check. A row trace identifies exactly two tables: EndCanvasEdit's 44-byte table and
    OnSelectionAction's 52-byte table. R3 passes all existing parser controls, all 54 new controls and the exact
    original-image recheck: three additional data rows, vector count 11691→11690, undecoded prefixes 6→4,
    runtime count 11690, tier/EVEX counts zero, and the unchanged allowlist. Eleven proof inputs and nine evidence
    entries are archived at `2026-10-02T15:51:57.4090880+00:00` before scanner/test activation. Fresh native r2
    starts at `2026-10-02T15:51:58.8860516Z`; six forced-rebuild profiles remain required.
    Native r2 passes all six forced-rebuild profiles at `2026-10-02T16:25:25.7852876Z` against 587 unchanged inputs.
    Each full x64 suite passes the exact 56 lease controls, 54 strict wide-stack ISA controls and unchanged
    application/legacy suites; ARM64 qualifies builds only. Independent verification retains 587 sources and
    325 evidence files in `p1-lease-shutdown-native-r2-qualified`, acknowledged at `2026-10-02T16:27:57.1089287Z`
    ([record](../../TestRuns/P1LeaseShutdown_2026-10-02.md)). This closes the scoped A3d GPU/retry cases only;
    clipboard leases, other live owners, actual initialization API failures and measured shutdown/G1 remain open.
- [ ] Run skills/format checks, full x64 Debug/Release/ASan suites and all ARM64 clean cross-builds; archive exact
  inputs/images/symbols/results, reconcile owning contracts and commit/push.
- [ ] Measure shutdown owner cleanup and remaining CPU graph costs; keep the full P1 G1 gate separate.

### Next A3 execution order

The reviewed residual-root inventory also identifies a document-sized ColorSampling raster in `_drag`, the
cached `_overlaySelection` geometry, and benchmark-only `_uiBenchmark` document/raster roots. Their explicit
retirement is now qualified by A3a. Primary caller review found that `RunUiBenchmark` reads completion metadata after `Run`, and its
stack `UiTimingCapture` borrows the state's timing storage. Retire only the benchmark's two document and two
raster roots; retain its state, timing, result metadata and known-path cleanup until those callers finish.
Resetting the whole benchmark state during Shutdown would invalidate these callers. Session hash bucket
capacities remain a separate measured cleanup cost. This inventory establishes ownership from code; it does
not establish a leak or a timing result.

- [x] A3a — Retire application sampling/overlay CPU roots and benchmark image roots on the acknowledged final
  worker. The private draft preserves the benchmark object, timing address, completion flags and known-path
  cleanup. The cheaper agent's draft and primary review add 14 controls to the existing 38 standalone application checks; primary review
  removes the fixture's extra sampling-raster reference before Shutdown so the test can exercise its last owner.
  The original session retirement assertion remains separate from the new host-root/MTA observation.
  Preserve a failing original run, activate only the reviewed private candidate, then qualify fresh native
  profiles before completing this slice or committing it.
  The isolated original Debug application reproduces eight failures among 52 controls; all original 38 checks
  pass. Exact 559 inputs and 23 evidence entries are byte/SHA verified in
  `p1-shutdown-roots-original-r1-failure` before candidate repair. The candidate builds with nonthrowing document
  construction/assignment checks. Its complete private Debug WARP application passes all 52 shutdown controls
  and unchanged application suites at `2026-10-02T12:44:05.0987339Z`. Exact 559 inputs and 39 evidence entries
  are byte/SHA verified in `p1-shutdown-roots-candidate-r1-qualified` before native activation. The reviewed
  three-file candidate passes the fresh six-profile native matrix at `2026-10-02T13:08:31.6078117Z` against
  582 unchanged inputs. Full x64 Debug/Release/ASan suites pass all 52 application shutdown controls;
  all three ARM64 cross-builds pass. Only these application sources/header change, so standard incremental
  builds qualify this slice; no dependency, project, build property or ABI pin changes. Exact sources/products/
  receipts are archived before another native edit
  ([record](../../TestRuns/P1ShutdownCpuRoots_2026-10-02.md)). Initialization/lease/live-draft/timing A3 stays open.

1. Add independent suspended-record and prequeued-release controls, each with a standalone scheduler/sink and
   real 16 MiB byte ownership. Hold a single worker by an event so queue ordering is deterministic; cancellation
   must suppress useful callbacks while final retirement still destroys each payload exactly once on MTA.
   Keep reservations and wake events alive until that destruction. A private test draft may proceed while the
   native source freeze is under qualification; integrate only after primary review and private execution.
2. Exercise partial processing initialization after each owned subsystem, with explicit reached-failure seams
   where needed: pool/final callback, wait/event registration, GPU pump, retry pump, continuation source, release
   sink and recycle ownership. No admitted payload may outlive a missing retirement callback. Check first and
   second Shutdown, caller apartment, empty event tickets and no stale thread-local binding independently.
3. Extend actual standalone application cases to live ordinary brush, healing/clone/blur brush and WarpStroke,
   plus gradient/pixel-move/transform/shape/color owners. Observe original authority at joined owner detachment;
   after acknowledgement prove draft/session CPU roots are gone and independent retained pixels are unchanged.
   Use borrowed weak-owner/destruction evidence; do not infer retirement from an empty ticket alone.
4. Hold actual suspended readback and retry-event stages at shutdown, including a mapped-row lease and delayed
   clipboard producer. Join native producers before owner Unmap/receiver/event teardown. Verify old resource/context
   ownership survives acknowledgement, no callbacks publish and each slot/reservation is returned once. Existing
   queue-only proofs remain useful but cannot substitute for the standalone Application boundary.
5. Measure Release hidden-owner detach/cleanup, final worker CPU retirement and acknowledgement separately.
   Include 4K/45 MP sparse tile graphs, retained selection/history and the 10,000-layer metadata limit; record
   cold/warm distributions, actual owner frees and hash bucket capacities. Thresholds remain measured phase exits,
   never unit-test timing assertions. Resolve any material remaining owner destruction before the full F5d/G1 gate.

Status: ACTIVE. Parent: [UiNeverBlocks_2026-09-22.md](UiNeverBlocks_2026-09-22.md), P1.7 F5d.
Owners: `JobScheduler`, `WorkerPool`, deferred CPU release and application shutdown.

## Evidence and failure

The original `JobScheduler::Close` cleared its TLS release sink before joining and destroyed job/action wrappers
after closing the pool. That bypassed worker retirement of admitted CPU payloads and released reservations before
queued action destruction. The reviewed two-stage fix qualifies in the fresh native r9 matrix.
A nested test host can use the outer host's sink and cannot prove standalone scheduler ownership.

Fresh native r9 qualifies A1/A2 and suspended/prequeued controls in all six clean profiles: 28 retirement,
26 hook, 34 suspended and 38 actual-host controls per x64 configuration, with ASan stress repeating HostTests.
Exact 582 inputs and 327 evidence entries are archived
([record](../../TestRuns/P1LifecycleShutdown_2026-10-02.md)). Broader A3 and measurements remain open.

The private independent controls construct real byte owners with literal size and boundary values. Job wrappers
remain owner-affine and are destroyed only after producer join, with the scheduler event and reservation alive.
CPU payloads must be destroyed exactly once away from the owner, on an MTA worker. Useful work and completion/UI
callbacks stay suppressed during shutdown. Repeated Close must not repeat destruction. Failure bounds detect
deadlocks; elapsed time is never asserted as performance.

Original failures and passing private scope are retained on the qualification archive volume as
`p1-shutdown-draft-shutdown-owner-retirement-failure-r2` and `p1-shutdown-draft-shutdown-core-qualified-r4`.
The passing r4 archive retains 522 source files and 48 evidence entries, including exact HostTests image/symbols.
The first test build's missing COM declaration header is retained separately before repair.

## Activated primitive lifecycle

Allocate one owned final threadpool-work callback during initialization, outside the ordinary dispatch cleanup
group. Closing joins dispatch and external parallel producers before calling the owner cleanup hook. That hook
destroys owner-affine wrappers outside scheduler locks, retaining the release sink while their CPU owners enqueue.
It clears the sink before final retirement, preventing any later owner release from targeting this closing pool.

The final callback drains deferred CPU ownership and recycled pixels regardless of the stop token. It runs on
the existing private pool while its MTA usage cookie remains alive. Close waits for its acknowledgement, then
releases reservations and closes events, callback ownership, cleanup group, pool and apartment. No new worker,
thread, allocation, completion application, message pump or polling loop is introduced during close.

Failed initialization has admitted no payload or recycling ownership and remains safely idempotent. Native
matrix and actual application ownership qualification remain required before completing this gate. The primitive
retires admitted job/action CPU ownership; remaining live session owners require separate application shutdown
work. This slice alone cannot qualify strokes, document metadata capture or all P1.
