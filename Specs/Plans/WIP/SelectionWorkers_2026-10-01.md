# Ordered selection workers (P1.6)

## Current delivery checklist — 2026-10-17

This child plan and parent P1 remain **WIP**. Earlier dated notes are historical.

- [x] Integrated `29a8ac2` passes canonical ARM64 Debug/Release/instrumented-ASan Rebuilds with 969 exact compiler records per profile and five verified AA64 PE/PDB pairs; native ARM64 runtime is unavailable. [Evidence](../../TestRuns/P1SelectionGestureNative_2026-10-17.md).
- [x] Inside-outline actual caller passes Release and real ASan: 41 physical, 21 shutdown and 71 scalar controls plus all 145 Application stages. The 51 reviewed paths and five contracts are adopted. [Evidence](../../TestRuns/P1SelectionPressHost_2026-10-17.md).
- [x] Settings/history epoch (`e03a80d`), own writer/Rectangle plan (`e695e32`) and outside-hit Rectangle/New CPU parent with exact cleanup (`3b7e55a`) each pass their exact-source Release and real-ASan rosters. The latest has 22,596 assertions, including 105 new facts. [Latest CPU evidence](../../TestRuns/P1RectangleGestureParent_2026-10-17.md).
- [x] Retain the actual compile, cancellation-driver and Alt compatibility failures; prove the correction against legacy canvas behavior while preserving explicit model-centered goldens.
- [x] Preserve Rectangle Win32 R1 API compile failure, R2 disk-preflight refusal, R3 new stale-outcome expectation failure and R4 post-shutdown driver failure. Independently verify every failed archive before its repair.
- [x] Qualify actual Rectangle Win32 Release R6: all 83 new facts and every predecessor record pass in 146 stages; retain the inherited 260-character reference-reader failure and the separate newline reader refusal. [Evidence](../../TestRuns/P1RectangleSelectionHost_2026-10-17.md).
- [x] Complete exact-source Rectangle Win32 real ASan: fresh 341 instrumented TUs, actual UAF detection, all 146 stages and both full archives independently pass. The 14 host paths, five contracts and evidence are pushed as `562b1b5`. [Evidence](../../TestRuns/P1RectangleSelectionHost_2026-10-17.md).
- [x] Qualify Ellipse/New CPU Release: 20 reviewed geometry/legacy-coverage facts and 77 original-parent/authority/lifetime facts preserve all 22,596 predecessors (22,693 total); retain the artifact-reader status-literal refusal without rerunning code.
- [x] Complete exact-source fresh 278-TU Ellipse real ASan, actual UAF control, all 22,693 assertions and complete archive/PE/PDB/ISA readback. Publish the 11 CPU code/test paths, four contracts and evidence as `cb72fb2`. [Evidence](../../TestRuns/P1EllipseGestureParent_2026-10-17.md).
- [x] Qualify actual Ellipse Win32 Release R2: all 89 new facts, 83 Rectangle facts and the complete 147-stage predecessor roster pass; independently verify all 1,710 archive files, 341 compiler records and executable/full-PDB identity. Retain R1's new inside-generation fixture failure. Candidate `819e604` includes the reviewed code, tests, contracts and evidence. [Evidence](../../TestRuns/P1EllipseSelectionHost_2026-10-17.md).
- [x] Qualify baseline `6a7afac` ARM64 Debug r1101, Release r1103 and instrumented ASan Debug r1105 cross-build qualification; retain explicit absence of native ARM64 runtime. [Native evidence](../../TestRuns/P1EllipseNative_2026-10-17.md).
- [x] Qualify actual Ellipse host in full native x64 ASan r1107 on adopted source `6a7afac`: all 975 instrumented compiler records, 1,833 archive files, five exact executable/PDB pairs, 22,693 Document facts, all 147 Application stages, both stress suites, the genuine UAF diagnostic and nine harness images independently pass. Retain the r1100 disk failure and the readback-only mixed-encoding refusal. [Native evidence](../../TestRuns/P1EllipseNative_2026-10-17.md).
- [x] Qualify and adopt the preserving Rectangle/Ellipse Add/Subtract CPU parent and adaptive cached capacity plan: 45 scalar and 268 parent facts, all 23,006 Document assertions, exact Release and real ASan (279 TUs), complete archives and actual UAF detection. Eleven CPU/test paths and four contracts are integrated; the physical extension remains separate. [Evidence](../../TestRuns/P1PreservingMarquee_2026-10-17.md).
- [x] Qualify and adopt the actual Rectangle/Ellipse Add/Subtract Win32 caller: all 429 new facts over 52 cases and the complete 149-stage predecessor roster pass exact Release and real ASan (343 App TUs). Five caller/fixture paths, original-ticket memory/lifetime proofs, normal-memory large sources and four contracts are integrated. Ordinary activation and cursor work remain open. [Evidence](../../TestRuns/P1PreservingMarqueeHost_2026-10-17.md).
- [x] Implement and independently verify the bounded cursor request-state prerequisite: 113 state-machine assertions plus the summary pass focused Release and instrumented ASan runs. The helper/test registration is adopted; complete HostTests and the physical consumer remain separate gates. [Focused evidence](../../TestRuns/P1SelectionCursorState_2026-10-17.md).
- [x] Qualify and adopt cursor-probe cached description and post-admission immutable-root capture: all 23,039 Document assertions pass complete scoped Release and real ASan (280 TUs), including genuine UAF detection, full roster/archive/source/binary verification and retained fixture failure. Seven code/test paths and three contracts are adopted. [Model evidence](../../TestRuns/P1SelectionCursorProbe_2026-10-17.md).
- [x] Qualify the selected cursor worker in complete private Release: all 50 cursor facts, 2 native clipboard setup controls, 53 original contention facts and the full 150-stage predecessor stream pass (9,111 stdout/7,613 stderr records). Main independently verifies the 345-record incremental object/source lineage and full archives; nine caller/fixture paths and four contracts are adopted for canonical native qualification. [Release evidence](../../TestRuns/P1SelectionCursorHost_2026-10-17.md).
- [x] Qualify adopted cursor source in canonical ARM64 Debug Rebuild `r1111`: actual exit 0, all 1,535 archive files, 982 compiler records and five exact AA64 executable/PDB pairs independently verified. [Native evidence](../../TestRuns/P1SelectionCursorNative_2026-10-17.md). ARM64 runtime is unavailable.
- [x] Qualify adopted cursor/clipboard source in canonical full x64 ASan `r1110`: actual exit 0, all 1,866 archive files, 982 instrumented compiler records, five exact executable/PDB pairs, full 23,039-fact Document/150-stage Application/Host/WARP/I/O rosters, both stress suites, genuine UAF detection and nine harness captures independently verified. [Native evidence](../../TestRuns/P1SelectionCursorNative_2026-10-17.md).
- [x] Qualify adopted cursor source in ARM64 Release Rebuild `r1112`: actual exit 0, all 1,535 archive files, 982 compiler records and five exact AA64 executable/PDB pairs independently verified. [Native evidence](../../TestRuns/P1SelectionCursorNative_2026-10-17.md).
- [x] Qualify adopted cursor source in ARM64 instrumented ASan Rebuild `r1113`: actual exit 0, all 1,540 archive files, exact 982 sanitizer compiler records and five AA64 executable/full-PDB pairs independently verified. Debug/Release/ASan cross-builds now all pass; native ARM64 runtime is unavailable. [Native evidence](../../TestRuns/P1SelectionCursorNative_2026-10-17.md).
- [x] Qualify adopted cursor source in canonical full x64 Debug `r1114`: actual exit 0, all 1,743 archive files, 982 compiler records, five exact executable/full-PDB pairs, complete five-suite roster and nine harness captures independently verified. Preserve the two reader-only refusals and corrected source/profile projection without replaying tests. [Native evidence](../../TestRuns/P1SelectionCursorNative_2026-10-17.md).
- [ ] Complete the latest adopted milestone full x64 Debug/Release/real-ASan, Release analysis and all three ARM64 cross-build gates. The full cursor-native receipts above qualify their own earlier source; Freehand has scoped Document/Application Release and real-ASan proof. Ordinary activation and measured G1 remain open; native ARM64 runtime is unavailable.
- [x] Qualify the private Freehand admission/input prerequisites in complete scoped Release and real ASan: all 23,123 assertions, exact 281-TU artifacts, full predecessor rosters and genuine sanitizer detection. The original reader-order failure is retained and repaired without replaying code. These prerequisites are now included in the qualified Freehand adoption below. [Evidence](../../TestRuns/P1FreehandPlan_2026-10-17.md).
- [x] Qualify and adopt the Freehand original parent and actual Win32 caller: 75 plan/9 queue/110 parent/472 host facts, all 23,233 Document assertions and the complete 151-stage Application roster pass scoped Release and real ASan. Main verifies full archives, actual UAF detection, exact 627-command/object lineage, executable/PDB identities and all retained failures. The selected gate remains off for ordinary input. [Evidence](../../TestRuns/P1FreehandHost_2026-10-17.md).
- [x] Correct exact immediate-R9 inline-table ISA recognition without changing DxUi or the allowlist: 64 new controls, the complete Release scanner suite and the unchanged original failed image pass. [Evidence](../../TestRuns/IsaR9Direct_2026-10-17.md).

- [ ] Complete the remaining polygonal/Wand/pixel-lift physical branches, overlapping writer/target guards, deferred transform settlement and worker-only geometry/coverage/outline consumers.
- [ ] Remove the remaining selection cursor owner containment/translated-geometry walk; preserve exact hit results with bounded latest-request scheduling, source authority and joined lifetime (implementation slice below).
- [ ] Prove complete original-ticket phase/retirement bounds, actual view/DPI/device behavior, ordinary activation and measured S5/G1 before closing this child or P1.

Previous private core, original-parent, physical-prefix, cached admission and residency prerequisites remain
qualified in their named evidence reports; they alone do not activate the ordinary host. The current
checklist supersedes the old duplicated pending/active summaries retained below.

## Historical continuation notes (through 2026-10-12)

Current continuation (2026-10-12, 13:44 UTC): private nine-plane raster view/DPI controls and the composition R8
mask draw component qualify independently; complete S5 admission, actual host routing and G1 remain open.
[Rational view references](../../TestRuns/P1SelectionOutlineRasterViews_2026-10-12.md) preserve all 9,241 prior
records plus twelve new records. [GPU numeric proof](../../TestRuns/P1SelectionOutlineGpu_2026-10-12.md) preserves
all 6,838 inherited occurrences and tests every solid/dash byte pair over three backgrounds. The original
GPU reader ordering refusal remains preserved alongside the narrowly verified callback-interleaving correction.

Main selects worker-prepared nine R8 coverage planes plus built-in composition pass9 as the next integration
candidate. This replaces the experiment's owner-created D2D mask bitmap route; it remains private until the
complete parent described below qualifies. Main's output-accounting adaptation and reviewed budget/lifetime tests now pass 20,834 private
DocumentTests records with preserved original numeric references; [evidence](../../TestRuns/P1SelectionOutlineAccounting_2026-10-12.md). Native selection/history qualification remains open after H3 Release r951 fails and the
private application progresses through separately preserved metadata-accounting fixtures; current r20 is diagnostic.

## Current continuation checklist (2026-10-11)

The qualified native Inverse/Expand/Contract checkpoint is committed and pushed as
`a70a2f21179babc719723caecf3edb2e6949ef0a` on `codex/p1-opacity-host-proof`, with matching
remote identity. Selection outline construction, remaining selection consumers and G1 stay open.
Private startup/opacity/timing r2 compiles and passes ISA, then fails actual owner initialization
(child 92040, exit 5). Main moves the independently owning new fixtures into the standalone phase
before the main owner's TLS sink and window-class registration; private r3 is running and unqualified.

Latest checkpoint (2026-10-12): current-source full Debug r923, Release r922,
real ASan r924 and Release analysis r925 qualify on 1,253 inputs and 20 contracts (`D69F9A3E…`).
ASan child 13480 exits 0 at 02:10:15 UTC; report `0DAA99FC…` preserves all original assertions,
131 application stages and real sanitizer/stress controls. Preserve the ARM64 controller's
ASan-count refusal and r103's duplicate-log creation refusal before any compiler launch.
Main corrects only runner bookkeeping; all three serial ARM64 r104 cross-builds pass. After the
preserved stale-folder reader refusal, exact archived-path correction retains every predicate
and verifies 15 PE/PDB pairs plus five real sanitizer controls (`F1E5E23D…`). Native ARM64 runtime
remains unavailable. Private startup/opacity/timing Release r1 fails compilation with C1090 PDB
API error 23 (child 46380, exit 1); no runtime starts. Preserve original source/products/logs
before a fresh unchanged-source attempt. Ordinary activation remains unqualified.
S5, the remaining selection consumers and G1 stay open.

Previous checkpoint (01:44 UTC, 2026-10-12): full Release r922 qualifies the exact 1,253-input,
20-contract `D69F9A3E…` source, actual child 68012 exits 0 at 01:15:45 UTC; report `310784E3…`
preserves all original assertions and 131 stages. The failed predecessor reader remains preserved;
the corrected reader checks the complete 791-record parser section, including one intentional
additional original-helper invocation. Analysis r925 also qualifies (`7688FC0E…`); r921 C1090
remains a failed run. Full Debug r923 is in its final application tests, then real ASan r924;
current-source ARM64 remains gated. Main corrects the agent's startup/timing composition before
preparing the private project. The raster utility, timing diagnostic and ordinary startup candidate
remain uncompiled. Admission, opaque API intervals, scalar view mapping, retirement and G1 stay open.

Current continuation (00:53 UTC, 2026-10-12): the integrated RSI64 source remains frozen at
1,253 inputs/20 contracts (`D69F9A3E…`). Analysis r921 fails during compilation with C1090 PDB API
error 23/invalid handle (actual child 78184, exit 1); its original archive and Document/IO compiler PDBs
are preserved. Main verifies every archived copy after a narrow extended-path reader correction;
completion `61643E87…`. The failed reader's 263-character path is real and present with the Windows
extended prefix; containment, missing-file, size, hash and duplicate-source refusals still pass. Fresh
unchanged-source analysis r925 has no C1090 and is running its original ISA controls before full
Release r922, Debug r923 and real ASan r924. No source/flag/pin/test-threshold change or failed-run pass.
Main reviews the cheaper worker raster draft, retains legacy anchor-relative point conversion, uses
one uninitialized nine-plane allocation and authors independent phase/DPI tests before integration.
Opaque Direct2D memory and cancellation intervals remain unqualified, alongside application admission,
bounded upload, view authority, retirement and ordinary/G1 gates. P1 stays WIP.

Current continuation (00:10 UTC, 2026-10-12): repaired fixture binding `DDEF8180…` retains 1,252 inputs
and all 20 contracts. Actual analysis r917 compiles/SAL-controls pass but linked ISA refuses an anonymous
20-entry DWORD table after `RunHostFlowSelfTest`'s return (child 43476, exit 1). Preserve its 1,651-copy
archive, completion `08C8C7D3…`; full r918–r920 and ARM64 have not launched. Main's narrow private RSI64
recognizer is undergoing original and new refusal controls before integration; no allowlist change.
S5 raster and nine-plane A8 probes now pass isolated Release, including all eight phases and independent
numeric values. They remove no native owner work yet. Admission, view/DPI/cancellation/retirement,
ordinary activation and G1 remain due.

Current continuation (23:49 UTC): native real ASan r916 qualifies the unchanged 1,252 inputs and 20
contracts (`AF22B204…`): actual child 85828 exits 0 at 23:39:13 UTC after starting 23:13:21. Original
assertions, 92 Document/433 application additions, 131 stages and the real negative sanitizer probe pass;
archive completion `E43692BC…`, report `767AF14C…`. Debug/Release remain qualified. Release analysis r914
fails at 23:44:34 (child 52552, exit 1), C6262's 18,428-byte frame at
`Application.Tests.SelectionOffset.cpp:29`; its 1,641-copy archive verifies. ARM64 did not launch and its
queued gate stopped. Main repairs only the fixture's `DocumentSession` allocation to heap, retaining semantic
assertions; changed source needs fresh validation. Corrected S5 Release geometry probe passes (compile 88864,
runtime 28284, source `9F73BC26`); separate-factory draw returns `D2DERR_WRONG_FACTORY`. WARP 2048x1024
first/warm ~68.9/~72.4 ms is diagnostic only, with no G1. WIC compile 88632 fails exit 2 on a Win32 macro
conflict; helper rename is in progress and runtime is unrun. S5/H4/G1 and all nine parent delivery areas
remain open; this plan remains WIP.

Current continuation (23:36 UTC): ASan r916 has passed compilation/ISA/suite/crash stages and is
running the application suite on the unchanged candidate. The original process handle gates analysis
qualification, three ARM64 r102 cross-builds and independent artifact verification. Main authors an
isolated CPU WIC raster snapshot transfer experiment with independent white/dash pixel expectations,
exact uploaded-byte verification and separately observed WARP differences. It is uncompiled; the
native candidate, dependency pin and all S5/G1 requirements stay unchanged.

Current continuation (23:15 UTC): native Release r911 and Debug r912 qualify on the same frozen
candidate. Actual ASan r913 fails during compilation with C1090; its failed archive and compiler PDB
are preserved, with no established cause and no runtime qualification. Fresh full ASan r916 is running
before analysis r914 and the reviewed ARM64 r102 protocol. The isolated S5 probe compiles but fails
its inappropriate hollow-figure bounds predicate. Same-factory draws succeed; separate single-threaded
and multithreaded worker factories both return `D2DERR_WRONG_FACTORY` from owner EndDraw. Preserve
that attempt, correct the diagnostic and qualify a supported outline route before ordinary activation.

Current continuation (23:02 UTC): native full Debug r912 now qualifies alongside Release on the exact
1,252-input/20-contract binding. Actual child 10676 exits 0 at 23:00:39 UTC; all original assertions,
92 Document/433 application additions and 131 stages verify, report `D3F1DE33…`. Real ASan r913 is
running, followed by analysis r914 and dedicated ARM64 r102. The reviewed S5 probe is still unexecuted;
its fixed 32,768-square case separates construction, sink close and first/warm draw observations.

Current continuation (22:44 UTC): native full Release r911 qualifies every original assertion and the
92 Document/433 application additions in 131 stages. Preserve the controller's LF/CRLF snapshot refusal;
Main proves the exact byte projection against the genuine earlier origin and all unchanged source hashes.
Projection `CC428781…`, exact reader r2 and archived report `A40E03E7…` qualify that actual run without a
rebuild or changed predicates. Debug r912 is running; real ASan, analysis and dedicated ARM64 gates remain
open. S5 factory/first-draw checks are detailed below; the isolated cheaper-agent probe remains uncompiled.

Current continuation (22:22 UTC): the repaired offset CPU/publication and actual host pass all three
private profiles. Main integrates the 39-file settled offset change and explicit int32 Inverse fixture
comparison only after every exact gate passes. Formatting/skills pass; the new native 1,252-input,
20-contract binding is `AF22B204…`. Forced full Release r911, Debug r912, real ASan r913 and analysis
r914 are sequenced; dedicated ARM64 r102 is prepared and gated on those reports. Native qualification,
commit/push, S5/H4/G1 and P1 completion remain due.

Current continuation (22:02 UTC): Release artifact repair r915 qualifies only the rebuilt artifact
identity on the same 1,228 native inputs, 20 contracts and unchanged DxUi pin/source. Debug private
closure remains exact. Fresh repaired host Release r24 and real ASan r25 are sequenced before the
prepared native activation. The prior r22 refusal is retained; no S5/H4/G1 or P1 gate is closed.

Current continuation (21:50 UTC): repaired CPU/publication Debug r18, Release r19 and real ASan r20
qualify 9,204 records each. Repaired actual host Debug r21 qualifies all original assertions and 433
additions in 131 stages, including the fixed large geometry and complete copied/final-weak output charge.
Release r22 refuses before compilation on a pinned library/provenance hash mismatch; its previous
artifacts are copied/read back before artifact-only native Release repair r915. Fresh Release/ASan
host proof and native source integration remain due; no S5/H4/G1 or P1 closure follows.

Current continuation (21:17 UTC): private host Debug r12 passes all 424 small controls, including
40 joined-Close records, but fails five of nine large Contract assertions. Release r13/ASan r14
remain unstarted. Diagnostics r15/r16 identify normalization scratch exhaustion inside the unchanged
128 MiB cap. Main prepares opt-in same-arena slot reuse for offset operations, preserving default
Boolean allocation and all original predicates; five fixed integer-grid CPU assertions are added.
The isolated project include-root failure r17 is retained before a fresh corrected candidate.
This is preparation and diagnosis, with no offset native activation, S5/H4/G1 or P1 closure.

Current continuation (20:49 UTC): preserve actual analysis r910 failure (child28436, exit1): C6221 flags
one Inverse fault fixture's HRESULT/int32 comparison atline343; the failed archive read-back completes.
ARM64 r101 has not run. Private offset Debug r9 builds successfully but actual app87972 exits5 at the
original header Expand check, which still expects synchronous completion. The caller must settle through
the real pump before checking its unchanged five-pixel golden; the private corrected source will also use
explicit int32 error comparisons. No failed attempt qualifies. Combine these corrections with the next
qualified offset integration before a fresh native matrix, avoiding a redundant intermediate full matrix.


Latest status (20:40 UTC): fresh native Release r907, Debug r908 and real ASan r909 are qualified on the
same1,228-input/20-contract binding. Preserve the old ASan reader's randomized log-path refusal; Main's
narrow corrected reader authenticates both archived diagnostics before projecting only those exact rows.
Release analysis r910 is running; ARM64 r101 awaits its audit. Private offset engine and publication core
qualify Debug/Release/real ASan with55/87 additions. The1,236-input,424-record host candidate is building
Debug r9; no native offset activation follows from private core qualification.

- [x] Authenticate native real ASan r909 and private offset engine r5/core r8 against exact same-profile predecessors.
- [x] Authenticate private offset core Debug r6 and Release r7 with all9,112 previous assertions and87 additions.
- [ ] Finish current-source analysis/ARM64, then commit/push the Inverse/scanner checkpoint.
- [x] Qualify the repaired offset host's 433 records/three stages, large output/capacity and native integration.
  Current-source native Debug/Release/real ASan, Release analysis and three ARM64 r104 cross-builds pass;
  [exact report](../../TestRuns/P1SelectionOffsetPrivate_2026-10-11.md). Ordinary activation, S5 and G1 remain separate.

- [x] Native layer and composite Wand New/Add/Subtract, bounded original-engine Boolean geometry,
  FIFO commands, no-match, cancellation/staleness, actual Undo/Redo and document/history Close qualify
  on the corrected 1,207-input source in full x64 Debug/Release/real ASan, Release analysis and all three
  ARM64 cross-builds. Persistent output survives copied/weak readers with its original exact charge.
  See [current qualification](../../TestRuns/P1WandPersistentOutput_2026-10-11.md).
- [x] Privately qualify numeric outline transport in Debug/Release/real ASan: exact anchor subtraction,
  cancellation and copied/final-weak ownership. This draft is not native consumer/resource qualification.
- [x] Integrate numeric transport into the two original Wand jobs with output and replaced reference-point
  admission before capture, source-checked owner adoption and original-ticket retirement. Full current-source
  x64 Debug/Release/real ASan, Release analysis and all three ARM64 r99 cross-builds pass. Direct2D path
  construction and other selection producers remain open. See [bound record](../../TestRuns/P1WandNumericOutline_2026-10-11.md).
- [ ] Complete large-image admission, overlapping gestures/writers, original-ticket outline consumer
  admission and bounded display/coverage/resource construction. The full owner Direct2D path build remains.
- [x] Privately qualify the Inverse CPU candidate in Debug/Release/real ASan: all 9,069 original Document
  assertions and 43 predeclared additions pass, with the actual sanitizer detection probe. The original
  Debug qualifier error is preserved before the narrow correction. [Record](../../TestRuns/P1InverseSelectionPrivate_2026-10-11.md).
- [x] Privately qualify the original Debug r4 ordered Inverse host: all original 126 stages and semantic assertions
  plus 190 named success/fault/order/stale/refresh/output-owner checks pass. Preserve compiler/API failures and the
  private-desktop presentation refusal; unchanged presentation checks pass on the inherited desktop.
- [x] Preserve expanded 219-control host r6–r12's unqualified attempts. In r9–r12, joined Close and admission checks pass,
  but the full runs fail at the large case. r9's 4,096-hole topology passes without establishing >16 MiB output accounting; r10/r11 65,536-hole and
  r12 32,768-hole cases fail cleanly with `E_OUTOFMEMORY`. Exact cause and pre-engine refusal are not established.
  See [private record](../../TestRuns/P1InverseSelectionPrivate_2026-10-11.md).
- [x] Privately qualify expanded Debug r13 with all original assertions/stages and 219 additions. The exact
  28,672-hole case charges 19,479,256 output bytes on its original ticket through copied and final weak owners;
  joined Close, actual queue saturation and exact refund pass. The fixed 128 MiB arena is unchanged.
- [x] Privately qualify the expanded 219-control host in Release r14 and real ASan r16. Together with Debug r13,
  all three private profiles preserve the original assertions/stages and pass 219 additions, including complete
  >16 MiB original-ticket output ownership; ASan r16 also passes the canonical HostTests detection probe.
  [Private record](../../TestRuns/P1InverseSelectionPrivate_2026-10-11.md).
- [x] Qualify native Debug r903 after preserving r902's missing-include build failure. The correction changes
  only the fixture include path; all earlier assertions plus 43 document/219 host additions, 128 stages and
  original parser/sizing/cleanup controls pass on the corrected 1,227-input source.
- [x] Finish native qualification on the current 1,253-input binding through r922–r925 and ARM64 r104.
  Release r904 compiles but its ISA gate refuses compiler-generated
  `OnCommand` jump-table data using a new RBX disp8 guard; its failed attempt is archived. Privately prove the
  exact new parser form and refusal controls before integration and fresh qualification; that work now passes
  with the original failure preserved. S5/H4/G1 remain open.
- [x] Integrate the exact RBX disp8 compiler-table proof after the independent linked-byte audit and actual
  59 new / 39 original encoded controls pass. Native r907–r910 run against the new 1,228-input binding;
  no runtime qualification is inferred from the static audit. The original failed r904 archive is retained.
- [x] Qualify fresh full native Release r907 and Debug r908 against the 1,228-input Inverse/scanner binding,
  preserving all predecessor assertions, 43 document/219 host additions, 128 stages and original controls.
  Real ASan r909 now qualifies; analysis r910 and ARM64 r101 remain due.
- [x] Qualify the isolated bounded Expand/Contract engine and its 55 reviewed controls in Debug r3 and
  Release r4. Preserve the failed compile, wrong cleanup branch and mismatched-profile reader attempts;
  original scale/winding/round-join semantics and exact legacy literals pass without test changes.
  [Record](../../TestRuns/P1SelectionOffsetPrivate_2026-10-11.md).
- [ ] Complete original-ticket host
  history/outline retention, ordered event-time header amounts, cancellation and joined Close before
  native activation. The isolated publication candidate freezes 32 further controls before Debug r6;
  its uncompiled host routing draft is separate from the currently frozen native source.
- [ ] Qualify ordinary activation and the original G1 measurements; P1 remains WIP. ARM64 runtime is unavailable.

The entries below retain earlier qualification scopes and failures; their older source identities do not
replace this current checkpoint. `Intersect` is an internal canvas clip, not a user-facing Wand mode.

Execution resumed by the user on 2026-10-03. The saved [checkpoint](../../TestRuns/UiNeverBlocksPause_2026-10-03.md)
retains the interruption. Close Project native r3 qualifies all six profiles, with actual exit 0 at
`2026-10-03T14:07:35.1376140+00:00`. Independent verification and immutable archival pass against 630 inputs and
23,526 evidence files, completion `5245F52417F232AB52E4F33BE125474FFE5C54C0802975549C5DD3890AC54E53`.
The exact QWORD padding proof preserves all prior controls and the allowlist; original failures remain archived.
Qualified step: S3b1b1b1d — eligible New, window close and noncritical session end with a live outline. Its isolated
source and reviewed real-producer literals are prepared. Private r1's C4456 compiler failure is archived before
repair; private r2 qualifies all 63 stages, 43 literal and 1,529 fault/order assertions. Independent verification
and 645-input/220-evidence archival complete at `2026-10-03T14:44:56.6931963+00:00`, receipt
`2A706D648A4051F62B3FB171A12482C35827145EEE07A0EAEE727378690739E9`. Native r1 qualifies all six profiles,
independent verification and immutable 632-input/23,532-evidence archival at
`2026-10-03T15:33:16.1738417+00:00`, completion
`97C09D00D3CF1CD164BD0A3C11E6E0CAF1C12F7D4FA1BF210E66E7083D169E70`.
Remaining selection work includes general geometry admission/cancellation, coexisting edits and gestures,
all-layers sources and measured G1. Broader P1/G1 remains open.

Qualified step: S3b1b1b1c Close Project with a live outline. New Canvas and the shared size opacity/Crop correction
qualify in all six fresh profiles; independent verification and immutable archival retain 627 inputs/23,056
evidence files at `2026-10-03T11:14:26.1987889+00:00`, completion
`675F17599A2288391C7E24993101D326B46AEEBFB4FC77D75F55990876E366DD`. The separate Close Project candidate
has two literal cases and 46 fault/order/admission cases; primary reviews the cheaper-agent literals and
prepares current-only close/save/lifecycle stages. Its compiler gate verifies the qualified prerequisite and 473
unchanged counterparts; private r1 Debug Rebuild and all 61 WARP stages pass against 641 frozen inputs.
All 14 literal and 477 fault/order assertions pass. Its immutable 641-input/218-evidence archive completes at
`2026-10-03T11:28:12.1916372+00:00`; nine exact native activations and independent preflight pass.
Native r1 PID 37776 starts at `2026-10-03T11:40:21.3075606+00:00`, freezing 629 inputs. A separate 515-source
candidate prepares eligible New, window close and noncritical session-end outline closure while that matrix runs.
Other geometry, gestures, lifecycle, constant-time metadata/history and measured G1 remain open.

Qualified preceding step: S3b1b1b1b New Canvas and the shared size opacity/Crop authority correction. Size native r4
passes all six profiles; independent archival retains 625 inputs/1,739 evidence files at
`2026-10-03T10:02:54.6386235+00:00`, completion `21DEC551A422A6EDAE95F5E0ADEF1CA1794DDF383CE6C87D76A0EF31BC6A1239`.
The earlier clipboard failure, unknown-exit interruption and precompiler PATH failure remain preserved.
The isolated New Canvas candidate has two literal scenarios and 52 fault/order scenarios. Private r1's missing
enum include and r2's ten history-checkpoint fixture failures are archived before correction. Private r3 passes
all 59 application stages, 20 literal assertions and 536 fault/order assertions. Its immutable archive retains
639 inputs/216 evidence files, completion `FDE0FC933778E8BDD03A7926C62BBCC07E2A2186977C14F3D6BF346519448BBF`.
Eight exact native activations and preflight pass; native r1 PID 23260 starts at
`2026-10-03T10:36:27.9675830+00:00` against 627 frozen inputs. All six configurations and exact immutable archival
now qualify size-card completion. S1c1/S1c2 are pushed through `bd4c969`. Other geometry, gestures, lifecycle and
measured G1 remain open.
Status: active execution child of [UiNeverBlocks](UiNeverBlocks_2026-09-22.md). S0a/S1a/S3a core preparation
qualifies in all six clean configurations against 423 frozen inputs: 127 command and 185 move-closure checks
per x64 suite. The 39 strict RDX controls and unchanged failed Release image pass with the runtime allowlist
unchanged. The initial parser failure and r2 ASan baseline-save failure retain exact images/source/logs;
the original baseline cause remains unknown. The r3 full matrix passes. Broader host commands, gesture routing,
general geometry admission and measured G1 remain unqualified.
S4a cancellable wand/trace/sampling also qualifies in six forced-rebuild profiles against 591 unchanged inputs:
all 71 named cancellation/chunk controls pass in each full x64 suite, with legacy goldens unchanged. Exact
sources, products and evidence are archived ([record](../../TestRuns/WandCancellation_2026-10-02.md)).
S4b active-layer New preparation passes 117 unique private Debug controls and existing document goldens. Exact
296 inputs and 136 evidence files are archived before core activation. Native r1 passes all ARM64 cross-builds
and full x64 ASan, then Release's linked ISA scan reports an EVEX-looking row in an adjustment switch table;
Release runtime and x64 Debug do not run. Exact 595 inputs and 297 evidence files, including the failed
image/PDB and compiler tracking, are archived before diagnosis. That attempt does not qualify native activation.
The strict RDI-index proof passes 23 controls against the unchanged failed image, with the allowlist unchanged.
Combined S4b/S2a native r2 passes all six profiles at `2026-10-02T20:13:23.3037530Z`: every full x64 suite contains
the exact 117 wand, 136 preserving-selection, 71 cancellation/chunk and 23 ISA controls. ARM64 is cross-build only.
Independent control/source verification and immutable archival retain 598 exact inputs and 7,223 evidence files.
That matrix qualifies only the S4b/S2a core boundaries. S2b's settled Select All/Deselect host now qualifies in
all six forced-rebuild profiles, with 162 unique Application controls in each x64 suite. Independent source/
control verification and archival retain 601 exact inputs and 2,858 evidence files, including the supplementary
private runtime logs ([record](../../TestRuns/SelectionHost_2026-10-02.md)). Active-layer New host routing is next;
broader gesture/writer routes, general geometry and G1 remain open.
Its first attempt failed before compilation because the workspace disk was full. The exact 296 inputs and
five failure receipts/logs are archived on the local disk before any edit/retry. Generated native intermediates
were copied, hash-verified and relocated recoverably; no source or qualified image was removed.
The owning selection behavior is [Document_Tools](../../Document/Document_Tools.md); existing legacy numeric
expectations remain unchanged. This plan does not add a Smooth command, which the current product does not expose.

## Implementation checklist

- [ ] S0 — Bound owned input, Clipper workspace/output and history/retirement peaks; identify library cancellation
  boundaries and measured stop latency. Overflow/refusal must preserve current geometry, history and pending input.
  - [x] S0a — Select All/Deselect use no Clipper or pixel output: account complete record/history capture,
    four-point result and retirement using the existing conservative history bounds. H2/H3 traversal stays open.
  - [ ] S0b — Boolean/offset/source geometry requires its own proved capacity and cancellation contract below.
    - [x] Verify the resolved Clipper2 2.0.1 call chain and immutable legacy tuples for Inverse/Expand/Contract.
      Primary checks the cheaper-agent inventory against the installed package and operation/test sources.
      Difference, offset's preliminary/cleanup Union and final canvas Intersect require separate peak accounting;
      stable-sort temporary buffers and the priority-queue backing storage also require coverage. This is source
      evidence only; no capacity bound, cancellation deadline or worker migration is qualified by the inventory.
- [ ] S1 — Prepare Select All, Deselect, Inverse, Expand and Contract geometry and complete history publication
  off owner; immutable authority and nonthrowing apply/retirement. Preserve nullopt versus explicit empty and AA.
  - [x] S1a — Sealed Select All/Deselect input/publication and 127 independent preparation checks qualify in all configurations.
  - [ ] S1b — Inverse/Expand/Contract only after S0b; reuse complete publication, preserve legacy geometry.
    - [ ] Inverse: prepare a command-specific sealed capture and bounded rectangle-minus-selection worker
      using the qualified boolean engine. Retain the existing selection's AA flag and `Inverse` history title;
      absent selection remains a true no-op, while a full-canvas inverse remains an explicit empty selection.
      Admit the engine arena, converted geometry, complete history and retiring source/display owners before
      capture; carry publication accounting through live/history/copied/weak readers. Main's
      `.build/P1Drafts/InverseSelection-r1-Main` contains an unintegrated core candidate with cancellable
      tuple comparison and overflow checks. No compilation, host activation or qualification is claimed.
      Its isolated full DocumentTests project retains the original 212 compile/166 include items, adds
      two compile/two include items, and rebuilds every translation unit against consistent private
      declarations. Main reviews 43 named candidate controls, including reached bounded-engine cancellation
      and an actual original-ticket persistent-output charge through history/copied/weak owners. The initial
      path-case preparation refusal is preserved; no runtime or refund result is claimed before execution.
    - [ ] Prove the legacy `(25,25)=0`, `(75,75)=255` coverage example, empty/null distinction, no-op redo,
      consumed capture, stale authority, reached cancellation/allocation/capacity faults and exact Undo/Redo.
      Then wire the original ordered command, numeric outline/output admission and terminal retirement under
      its own original ticket. Expand/Contract retain their separate offset-engine bounds.
  - [x] S1c — Selection nudge owns event-time whole-pixel delta/Shift state and prepares translated loops and
    complete `Move Selection` history off owner. The session's current NudgeSelection starts its own move
    only when no outline move is already live; keep that live-outline no-op policy. Qualify translation output
    bounds, no-op/redo, enclosing transaction and command ordering independently of Clipper operations.
    - [x] Seed 500 isolated sources from the qualified header private r1 archive; primary prepares sealed
      whole-pixel authority, one-pass cancellable translation/cached geometry, worker history and two-generation
      current-only acknowledgement. A cheaper agent drafts independent legacy and cancellation controls.
    - [x] Primary reviews and extends worker/capacity/retirement tests; the header native archive now qualifies.
    - [x] Compile and qualify the complete private DocumentTests suite against that verified predecessor:
      80 unique controls pass; exact 629 inputs/158 evidence files are archived before eight byte-identical
      native activations. The first archiver's retained-name uniqueness mistake is preserved before recovery.
    - [x] S1c1 — Qualify the settled prepared core in all six native profiles and independently archive.
      Full private proof, exact archival and fresh native qualification precede activation
      ([record](../../TestRuns/SelectionNudge_2026-10-03.md)). No nudge host route has changed.
      Native r1 returns actual exit 1: all ARM64 cross-builds and full x64 ASan pass, then Release's linked
      ISA scan rejects a vector-looking terminal DWORD in OnSelectionAction. Its 618 inputs and 24,357
      evidence files are archived before isolated scanner repair; Release runtime and x64 Debug remain open.
      Native r2 now passes all six profiles at `2026-10-03T06:34:34.8129025+00:00`; independent verification
      and immutable archival retain 619 inputs/23,770 evidence files at `2026-10-03T06:41:31.8464318+00:00`.
      All 80 new checks and exact retained suites pass; ARM64 is cross-build only. This completes S1c1.
    - [x] S1c2 — Activate typed event-time arrow/Shift inputs through ordered host publication, preserving
      live-outline no-op and existing settlement policy; qualify actual host faults, ordering, pump and disposal.
      A separate 504-source seed owns the source-only host candidate. Primary prepares the production path
      and fault/pump/stale/admission controls; reviewed cheaper-agent arrow goldens cover all eight input
      combinations, with independently literal nested history. Private compilation waits for S1c1's exact
      native completion/archive ([record](../../TestRuns/SelectionNudgeHost_2026-10-03.md)).
      That predecessor now qualifies and is pushed as `cc04ddf`; private Debug Rebuild starts after exact
      verification of 466 unchanged counterparts, freezing 635 inputs. Full WARP/native qualification remains open.
      Private r1's primary saved-state fixture API error is archived before correction. Full private r2 WARP
      passes 84 literal checks/11 modes and 173 fault/order checks/26 modes, with all 55 stages and exact retained
      suites. Immutable archival retains 635 inputs/216 evidence files at `2026-10-03T06:55:20.1712298+00:00`
      before ten byte-identical activations. Fresh native qualification and archival remain required.
      Native preflight verifies all ten activations and 466 unchanged counterparts; PID 77024 starts the
      six-profile forced matrix at `2026-10-03T07:01:39.6988523+00:00` against 623 frozen inputs.
      Actual exit 0 at `2026-10-03T07:30:25.0520148+00:00`; independent verification and immutable archival
      retain 623 inputs/1,064 evidence files at `2026-10-03T07:36:56.8764451+00:00`. S1c2 completes.
- [ ] S2 — Activate ordered command/header requests, capturing amounts/modes at input time and acquiring settled
  geometry only after earlier history work acknowledges. Gate every overlapping selection mutation and gesture.
  - [x] S2a — Qualify separate Select All/Deselect authority that preserves an already live brush, warp or shape.
    Keep settled history-restore authority unchanged. Bind brush/warp control-block identity without owning or
    reading their mutable state; bind the shape opening separately from its changing drag bounds. Refuse any
    replaced/closed gesture, document/history/target/crop change, panel, gradient, transform, lifted pixels,
    lasso or outline move. Prove literal selection/history results, unchanged captured stroke coverage, allowed
    continued samples, and reached gesture replacement/cancellation refusals privately before host routing.
    Production pointer buffering, selected ordinals, writer barriers and measured G1 remain S2/S3b/P1.4.
    Primary reviews the cheaper-agent draft and adds independent committed-pixel clip/Undo controls. Exact
    compile failures and the seven failing r3 fixture expectations are retained before correction: first-warp
    Append records only its opening, raster identity is not pixel equality, and an open transaction's UndoName
    still names its prior committed entry. The r4 full private Debug suite passes 136 unique controls and all
    existing goldens; 299 exact inputs and 137 evidence files are archived before activation. A combined fresh
    six-profile S4b/S2a core matrix follows the independently qualified ISA data proof. Neither slice is complete
    until native control/source verification and archival pass; production host routing stays open.
    The combined native r2 matrix and independent verification/archive now pass: 136 exact unique controls run
    in every full x64 suite, and all three ARM64 cross-builds pass. This completes the preserving authority core
    ([evidence](../../TestRuns/SelectionPreservation_2026-10-02.md)); production input/gesture routing stays open.
  - [x] S2b — Route settled Select All/Deselect through the production host ordinal, whole-graph admission,
    worker history preparation, exact completion acknowledgement and off-owner retirement. Capture only after
    reservation; preserve no-op/redo, nested title/depth, saved markers and cancellation/fault refusal. Queue
    younger document commands and keep view/chrome pumping. Qualify actual Application controls privately,
    then all native configurations. Live brush/warp/shape, selection move/lasso/lifted pixels and header inputs
    keep their current routes until separate pointer replay/closure/writer barriers qualify; this bounded slice
    does not complete S2/S3b or G1. Primary owns the host integration and reviews cheaper-agent test sidecars.
    Private r1 fails compilation; r2's mask fixture never changes target because Add Mask already selected it.
    An explicit image-target precondition corrects that fixture. In r3 all 162 selection actor checks pass, but
    the full clipboard suite starts a gradient pointer drag while its newly asynchronous Deselect is pending.
    Both failure attempts retain exact sources, compiler products and runtime logs before correction. The
    existing harness command-settlement helper now orders setup/cleanup; production clipboard code is unchanged.
    A fresh complete private Debug run and immutable archival precede native activation.
    The r4 complete WARP application suite passes all 162 named actor checks across 28 modes and unchanged
    compatibility suites. Exact 471 inputs and 188 evidence files are archived before eleven source activations.
    The fresh native six-profile forced rebuild is running; qualification remains pending
    ([record](../../TestRuns/SelectionHost_2026-10-02.md)).
    Native r1 fails the ARM64 ASan HostTests catalogue array size before any complete profile. Exact 601 inputs
    and 3,850 evidence files are archived before correcting only the declared count to 486. The private application
    proof is unchanged; native r2 restarts the full forced matrix. S2b remains unchecked.
    All ARM64 cross-builds and the complete x64 ASan suite pass in r2. Release/Debug and independent native
    verification/archive remain pending.
    Native r2 now passes every profile at `2026-10-02T21:48:10.2541344Z`. All 162 exact unique Application checks
    run in each full x64 suite; ARM64 cross-builds pass. Independent source/control verification and immutable
    archival preserve 601 inputs and 2,858 evidence files. Original runtime logs omitted by the archive helper's
    array construction are verified in a separately retained 45-file supplement; original archives stay intact.
    This completes the settled host boundary only; S2/S3b and G1 remain open.
- [ ] S3 — Move committed marquee/lasso booleans and layer/mask traces; own gesture arguments and exact source
  authority, retaining drafts until apply/drop. Preserve outline-move cancellation and undo semantics.
  - [x] S3a — Prepare outline-move End/Cancel history and retirement separately. The qualified native core passes
    185 assertions, preserving End's moved outline, Escape's original outline, nested name/depth, no-op/redo and
    exact acknowledged generation stamps in all x64 suites; all three ARM64 cross-builds pass.
  - [ ] S3b — Activate move/draft/gesture closure under the selected ordinal and exact retained input; the
    prepared core does not authorize synchronous host End/Cancel or altered live-stroke settlement.
    - [x] S3b0 — Qualify a session-local monotonically increasing outline-move opening before queue activation.
      A bounded event-time scalar must refuse a closed/reopened gesture before owner capture, even when its
      origin and current geometry are equal. Retain existing generation/history checks on prepared apply; bind
      the opening as an additional requirement. Keep the counter across document replacement and expose no
      owning document/history reference in the queued event. Complete retirement accounting must include the
      retained original outline, document/display/history graphs and wrapper storage. Primary prepares this
      isolated core source while S4c native inputs stay frozen; private literal/state-machine controls and full
      DocumentTests, archival and native qualification precede production closure activation.
      The reviewed private r4 source passes all 66 unique opening controls and complete DocumentTests, retaining
      the exact 185 closure/127 command/136 preserve/117 wand named multisets and original goldens. Preflight and
      failed compiler/runtime/verifier attempts are preserved before correction. Final proof retains 554 inputs
      and 145 evidence files before eight byte-identical native activations; the fresh six-profile matrix runs
      against 606 frozen inputs. All six profiles pass at `2026-10-02T23:17:48.9451202Z`; exact control/source
      verification and immutable archival retain 606 inputs and 913 evidence files
      ([record](../../TestRuns/SelectionMoveOpening_2026-10-03.md)). S3b0 completes the opening core prerequisite;
      production closure and G1 remain open.
    - [ ] S3b1 — Own the qualified opening and exact End/Cancel intent in a bounded typed host request at the
      pointer/command boundary. Select the shared ordinal before capture; reject a replaced opening without
      settling another gesture. Freeze subsequent outline samples while closure awaits acknowledgement. Reserve
      construction and complete retirement before capturing the sealed closure input, prepare history on the
      worker, and publish only the still-current opening/session/history aggregate. Retain receipt, draft and
      history ownership through cancellation, failure or stale acknowledgement; no synchronous fallback on
      admission failure. Keep click-without-move Deselect or New wand follow-up ordered ahead of younger writers
      and own its point/settings/AA at the original event. First enumerate mouse-up, capture loss, tool/header,
      Escape, Undo/Redo, replacement/save/close and shutdown callers and their direct reference semantics.
      Actual production-route controls must hold the worker, send later pointer samples and younger writers,
      assert independent literal End/Cancel/redo outcomes, and reach stale opening, record/memory refusal,
      capture/history allocation faults and cancellation acknowledgement. Qualify exact retirement and full
      configuration gates before claiming the host closure complete; lasso/boolean/All Layers and G1 stay open.
      - [x] S3b1a — Activate moved-pointer release and direct Escape/first Undo cancellation through the exact
        opening request. Bypass unrelated edit settlement so a coexisting outer edit keeps its reference depth
        and name. Stop further outline samples at admission; retain the opening/origin/history on admission,
        capture, worker failure or cancellation until a successful End/Cancel publication. Prove actual held
        worker receipts, posted-window-message/zoom handling, literal geometry/history, younger-writer ordering,
        stale opening/session/target and reservation/retirement behavior. Primary prepares the isolated host
        source and reviews the cheaper-agent control draft. Private r4's normal full WARP Application run
        passes 242 unique checks across 31 modes, retained host controls and all 50 stages. Failed attempts are
        archived before correction; the final exact 618-input/199-evidence archive precedes nine native source
        activations. Native r1 freezes 609 inputs: skills/format, three ARM64 cross-builds and the full x64 ASan
        gate pass. Release's ISA gate rejects a vector-looking row in OnCommand's RDI-base remap table; its
        runtime and x64 Debug do not run. The actual failed matrix retains 609 inputs and 4,286 evidence files
        before scanner diagnosis. The unchanged failed image and all nine parser suites now pass the narrow
        RDI-base proof, including 56 new malformed/adjacent-code controls, with no allowlist change. The
        create-new proof archive retains 13 inputs and 238 evidence files; native r2 runs against 610 inputs.
        Exact native verification and archival remain required
        ([record](../../TestRuns/SelectionMoveHost_2026-10-03.md)); S3b1a stays unchecked until they pass.
        Native r2 now passes all six forced-rebuild profiles at `2026-10-03T01:07:26.4741875Z`: all 242 exact
        unique host checks, 56 new ISA controls and retained opening/selection/wand controls run in each full
        x64 suite, with all 50 Application stages. ARM64 cross-builds pass. Independent verification and
        immutable archival retain 610 inputs and 1,298 evidence files. This completes the bounded S3b1a route.
        Aggregate off-owner retirement is observed; H2/H3, evolving cache growth, every raster's final reference
        and measured G1 remain separate open requirements.
      - [ ] S3b1b — Own no-move Deselect/New wand follow-ups and every remaining reference End/Cancel caller.
        Preserve Enter/Save/Export's live outline and operations that intentionally nest under its history entry.
        Tool/header/capture loss, selected Undo/Redo, replacement/close and shutdown need concrete caller/result
        controls before migration. A queued closure alone does not authorize its dependent command to run before
        acknowledgement, and settlement must not silently change the previous nested transaction semantics.
        - [x] S3b1b0 — Qualify the known End-before-command/tool callers and capture-loss outline route. Bind
          command/tool events to their observed opening, refuse a replacement before other settlement, freeze
          pointer sampling and wait for exact closure acknowledgement before Execute. Selected first Undo/Redo
          cancels the outline and consumes the command; younger Undo after a completed End restores its entry.
          Same-tool inputs preserve the move. Primary prepares a separate source-only candidate copied from
          the immutable qualified host proof while the repaired 610 native inputs remained frozen. A cheaper
          agent drafts actual caller controls; primary adds all 27 real End-before-command refusal boundaries,
          equal-geometry reopen, selected same-tool, admission refusal/retry, Select All/Undo ordering and
          enclosing-history/younger Undo controls. Private parser refinement also pairs a valid decoded body
          target with the same row marked unknown: 57 source-only controls pass, with its first wrong target
          fixture preserved. This strengthens the original UnknownTarget label, which additionally violated
          table-target bounds. The refinement keeps the scanner and runtime allowlist unchanged.
          Private r1 actually exits 5 with 274 passed caller assertions across 49 modes and two new Select All
          fixture failures: the literal expected AA=false instead of the owning contract's AA=true. Both Undo
          checks and the retained 242 closure checks pass. Exact 621 inputs/435 evidence files are archived
          before correcting only the new fixture and launching r2
          ([record](../../TestRuns/SelectionMoveCallers_2026-10-03.md)); production and existing goldens stay unchanged.
          Private r2 now passes its fresh compiler and normal full WARP Application runtime: 276 unique caller
          checks across 49 modes, all 51 stages and retained host controls. Independent verification and archival
          retain 621 inputs and 435 evidence files before seven native source activations. Native r1 now passes
          all six forced-rebuild profiles at `2026-10-03T01:59:42.3724882Z`: all 276 unique caller checks/49 modes,
          242 retained close checks/31 modes, all 51 stages, 57 ISA controls and unchanged retained suites run
          in every full x64 suite; ARM64 cross-builds pass. Independent verification and immutable archival
          retain 611 inputs and 1,546 evidence files before another build. This completes S3b1b0 only.
        - [ ] S3b1b1 — Own no-move follow-ups, remaining header/size/replacement/close callers and the concrete
          coexisting-edit semantics. Source classification of a command does not qualify its heavy dependent
          geometry/pixel operation; boolean/traces, All Layers and measured G1 remain their separate gates.
          - [x] S3b1b1a — Own click-without-drag End and its Deselect or active-layer New wand follow-up in one
            noncoalescing bounded input. Capture the exact opening and original point/settings/AA at release;
            refuse a reopened outline before capture. Preserve the enclosing transaction and reuse the selected
            ordinal through both acknowledged jobs. Freeze pointer samples on admission/refusal; a failed End
            runs no follow-up. A failed/cancelled follow-up after acknowledged End preserves its completed
            closure and publishes no dependent selection/history. Prove literal geometry and Undo, no-op/redo,
            original outer title/depth, two held worker phases, younger-writer order, settings changed after
            admission, actual budget/allocation/capture/worker/cancellation failures and retry. Primary prepares
            an isolated candidate from the immutable 497-file private caller source while 611 native inputs
            remain frozen; a cheaper agent drafts controls. Full private proof, immutable archive and native
            gates precede activation. The eligible route excludes other pending edits/gestures exactly as the
            settled selection factory does. Its typed request refuses before End when those are present;
            existing coexisting and All Layers callers retain their reference route until separately migrated.
            Admission refusal never enters a synchronous fallback. Primary adds a reached deferred-panel
            compatibility control; coexisting publication and All Layers remain separate open work.
            Private r1 fails compilation because the continuation file sees only the selection-command enum's
            forward declaration. Its exact 623 inputs and 413 evidence files are archived before adding the
            owning header and rebuilding r2. Runtime/native qualification remains pending
            ([record](../../TestRuns/SelectionMoveFollowups_2026-10-03.md)).
            - [x] Private r2 full WARP qualification: 84 unique checks/16 modes, 52 ordered Application stages
              and retained caller/close/selection/wand suites. Archive 623 exact inputs/436 evidence files;
              independent preflight and seven byte-identical activations complete before native rebuilding.
            - [x] Fresh native six-profile matrix, exact control verification and immutable archival.
              Matrix starts at `2026-10-03T02:23:16.4027680+00:00`, with 612 frozen inputs. All ARM64 builds pass;
              x64 ASan's linked ISA scan rejects a vector-looking Application table row, actual exit 1 at
              `02:33:28.7931999+00:00`. Exact 612 inputs/5,850 evidence files and failed EXE/PDB are archived
              before diagnosis. The strict QWORD-stack/RAX data proof now passes 73 new controls, retains
              all 57 RDI controls and rechecks both original failed images with the allowlist unchanged.
              Its 17-input/19,260-evidence immutable archive verifies before three scanner/test activations.
              Native r2 returns actual exit 0 at `2026-10-03T03:46:41.3855104+00:00` against 613 frozen inputs.
              All six profiles, 84 exact follow-up checks/16 modes and retained suites qualify. Independent
              verification and immutable archival retain 613 inputs/21,605 evidence files, completed at
              `2026-10-03T03:51:14.2165680+00:00`. The next private compiler may now consume these dependencies.
          - [ ] S3b1b1b — Own selection-header changes and remaining size/replacement/close callers only after
            their concrete reference settlement and coexisting-edit policy controls qualify.
            - [x] S3b1b1b0 — Eligible idle outline header kind/Deselect retains exact opening/action/value,
              freezes samples on admission/refusal and waits acknowledged End before bounded settings apply
              or prepared Deselect at the same ordinal. Preserve unchanged-kind End, zero/moved outline
              history, event-time value, nested title/depth, failure/cancellation/refusal/retry and younger order.
              Keep existing busy/coexisting header policy until its own preserving publication qualifies.
              Primary seeds 498 source files from the immutable follow-up private r2 and prepares isolated
              code; a cheaper agent drafts literal controls. No next compiler may consume native dependencies
              before the current follow-up matrix completes and archives. Private r1 now passes fresh Debug
              compilation and the full WARP Application suite: 126 unique checks/21 modes, 53 ordered stages
              and all retained suites. Immutable archival retains 625 inputs/216 evidence files, completed
              `2026-10-03T03:57:49.3365819+00:00` before seven byte-identical activations
              ([record](../../TestRuns/SelectionMoveHeaders_2026-10-03.md)). Independent preflight verifies
              460 unchanged common inputs; native r1 PID 50116 returns actual exit 0 at
              `2026-10-03T04:29:21.8934408+00:00` against 614 frozen inputs. All six profiles pass; independent
              verification and immutable archival retain 614 inputs/20,351 evidence files at
              `2026-10-03T04:39:26.9716006+00:00`. Remaining routes and measured G1 stay open.
            - [x] S3b1b1b1a — Eligible Canvas/Image Size Apply owns the captured existing outline and card
              invocation, cancels the outline on a worker, then starts the existing size worker on the same
              request ordinal. Foreground/background extension captures the input-time ARGB value. Refuse a
              replaced/closed outline, reopened card or newly coexisting edit before capture/apply. NewCanvas
              and prompt-driven lifecycle remain outside this slice; preserve their exact prompt order.
              - [x] Seed 509 verified sources in a separate candidate; primary owns the continuation and
                completion checks. A cheaper agent drafts four ordinary/nested literal Canvas/Image cases;
                primary fixes locked-ratio setup and worker cleanup, then adds dual-worker faults/order/pump,
                input-time palette, stale/capture/admission/refusal/retry and disposal controls. Source only.
              - [x] Wait for native nudge host qualification and immutable archive; `bd4c969` is pushed.
                Its completion is `AB96CD24B0A857C1987A9A43F9ED7CE4C80DA72A93FCAB226F2CA55FBFCE6877`.
              - [x] Full private r3 WARP proof: 46 literal checks/4 modes, 242 fault/order checks/29 modes and all
                57 registered stages pass. Exact archival retains 637 inputs/214 evidence files at
                `2026-10-03T07:52:00.4863863+00:00`. Both earlier failures are preserved before repair.
              - [x] Qualify all six fresh native profiles, independently verify/archive, update contracts and
                commit/push. Actual timing and ARM64 runtime qualification remain separate.
                Native r1 passes ARM64 builds and the new size assertions, then x64 ASan's existing clipboard
                fault fixture fails to open the clipboard (error 5), actual matrix exit 1 at
                `2026-10-03T08:21:30.8022590+00:00`. Immutable archival preserves 625 inputs/4,374 evidence files
                before the two remaining short clipboard setup helpers adopt thirty readiness attempts. The
                size slice remains unqualified; Release/Debug did not run. A fresh full matrix is required.
                Native r2 PID 66964 starts at `2026-10-03T08:32:45.4833456+00:00`, freezing the same 625 inputs
                with two explicitly bound fixture readiness repairs. Preflight verifies 466 unchanged common
                counterparts, ten original activations, exact private controls and the retained failed attempt.
                Its process/tool session disappears without an actual completion receipt. The observed exit
                remains unknown; immutable interrupted archival retains 625 inputs/4,377 evidence files at
                `2026-10-03T08:57:25.0198348+00:00`, completion SHA-256
                `CB8C2596990D4D0B2657C9D0AC6C11045ABD3947BC10FEE52D2B71AB53C3D468`.
                A hidden same-user launcher independently proves repository access outside the tool session.
                Fresh r3 PID 82736 starts at `2026-10-03T09:11:22.7778174+00:00` after exact preflight; six
                profiles, actual process receipt and independent verification/archive remain pending.
                Detached r3 returns actual exit 1 at `2026-10-03T09:12:07.8717412+00:00`, before source inventory
                or compilation, because its PATH cannot resolve rg. Precompiler failure archival retains 625
                independently unchanged inputs/33 evidence files at `2026-10-03T09:17:42.7051168+00:00`, completion
                SHA-256 `BE2C22A9B22A93698475ECC51713FE1239E609FCA531419CA7CDF3DEC05152D6`.
                Fresh r4 pins explicit rg/git/pwsh paths before launching the same full matrix. Source inputs
                remain unchanged; actual six-profile qualification is still pending.
                Independent r4 preflight passes, and PID 91044 starts at
                `2026-10-03T09:21:55.6955366+00:00`. Its launcher pins the detached executables by SHA-256;
                successful process creation remains separate from actual profile qualification.
                All six profiles pass; actual exit 0 at `2026-10-03T09:56:45.0024453+00:00`. Independent exact
                verification and archival retain 625 inputs/1,739 evidence files at
                `2026-10-03T10:02:54.6386235+00:00`, completion
                `21DEC551A422A6EDAE95F5E0ADEF1CA1794DDF383CE6C87D76A0EF31BC6A1239`.
                The New Canvas follow-up qualifies the reviewed shared opacity/Crop guard before completion.
            - [x] S3b1b1b1b — Eligible New Canvas Apply with a live outline preserves Save/Discard/Cancel prompt
              order. Cancel leaves the card, outline capture and exact live history unchanged; Discard prepares
              outline Cancel before replacement; Save prepares End before the captured project write and starts
              replacement only after its acknowledged success. One ordinal owns each acknowledged phase.
              - [x] Seed 511 exact sources from the qualified size private r3 in a separate source-only tree.
                Primary prepares prompt-aware stages and current-only closure/lifecycle checks. A cheaper agent
                drafts two independent literal Discard/Cancel cases; primary corrects the history reference and
                post-move capture, and owns Save, held-stage authority, fault/order/pump and retirement controls.
                Two literal scenarios and 52 fault/order scenarios are drafted; new color, same-value opacity
                and crop openings are refused at the selected and held-worker boundaries. Already coexisting
                opacity/crop paths keep their separate migration. Full compilation/runtime proof is pending.
                Primary review adds a shared size-specific opacity/Crop predicate and eight Canvas/Image
                selected/held cases that require no resize admission or capture. These guards complete the
                size-card authority correction alongside the New Canvas follow-up; the native r4 inputs stay frozen.
                Its native size prerequisite now qualifies (625 inputs/1,739 evidence files). Private r1's enum
                include failure is preserved (639/193); r2 returns actual exit 5 with 20 passing literal checks
                and ten failures among 535 fault assertions (639/216 preserved). Trace shows four End checkpoints
                and no BeforeSnapshot checkpoint; preparation failure precedes the held publication gate.
                Only new fixture expectations change afterwards. Private r3 passes all 59 stages, 20 literal
                assertions and 536 fault/order assertions; 639 inputs/216 evidence files are archived.
              - [x] Compile the private candidate only after the current size native matrix and immutable archive
                qualify; retain every failure before repair. Full WARP proof/archive precede native activation.
              - [x] Qualify all six fresh native profiles, independently verify/archive, update contracts and
                commit/push. Coexisting/invalid lifecycle callers and measured G1 remain open.
                Eight exact native activations and 472-counterpart preflight pass. Native r1 PID 23260 starts
                at `2026-10-03T10:36:27.9675830+00:00`, freezing 627 inputs.
                All six profiles and exact immutable archival now qualify at
                `2026-10-03T11:14:26.1987889+00:00` (627 inputs/23,056 evidence files).
            - [x] S3b1b1b1c — Eligible Close Project with a live outline and no coexisting edit preserves the
              Save/Discard/Cancel prompt. Cancel keeps the exact live outline/local drag/history/path; Discard
              acknowledges worker Cancel before close, and Save acknowledges End and original-document write
              before close. One selected ordinal owns all phases, with current-only publication and retirement.
              - [x] Seed 513 exact source-only inputs from New Canvas private r3. Native inputs remain frozen;
                compilation waits for the actual qualified New Canvas native archive. Primary owns production
                and fault/order controls; a cheaper agent drafts two independent literal Close/Cancel cases.
              - [x] Review and finish source-only production and independent tests: two literal cases and 46
                fault/order/admission cases; the primary fixes agent string literals and post-gesture capture.
                The candidate registers 61 stages, with 473 unchanged compiler counterparts
                ([record](../../TestRuns/SelectionCloseProjectOutline_2026-10-03.md)).
              - [x] Qualify full private WARP and archive after the exact native New Canvas prerequisite.
                The prerequisite qualifies and is pushed as `0ce2070`. Private r1 Debug Rebuild returns actual
                exit 0 at `2026-10-03T11:21:36.3289326+00:00`, freezing 641 inputs. The full 61-stage WARP run returns
                actual exit 0 at `2026-10-03T11:27:01.2532823+00:00`; 14 literal and 477 fault/order assertions pass.
                Immutable archival retains 641 inputs/218 evidence files at `2026-10-03T11:28:12.1916372+00:00`.
              - [x] Activate byte-identical sources, qualify all six fresh profiles and independently archive.
                Independent preflight passes; native r1 PID 37776 starts at
                `2026-10-03T11:40:21.3075606+00:00` against 629 frozen inputs.
                The user stops r1 during ARM64 Release, after ARM64 ASan Debug passes. Actual matrix exit 1 is
                retained at `2026-10-03T11:45:34.9856264+00:00`; full native qualification remains required.
                Interrupted archival retains 629 inputs/26,193 evidence files at
                `2026-10-03T11:55:11.3382133+00:00`. A new complete native attempt is required on resume.
                Resumed r2 passes every ARM64 cross-build, then fails the x64 ASan linked ISA gate before runtime
                at `2026-10-03T12:30:23.5883091+00:00`. Exact 629 inputs/26,394 evidence files are archived before
                repair. The exact RET/NOP QWORD-table proof passes 113 new controls, all retained suites and
                four unchanged failed images; immutable proof archival and independent verification pass.
                Native r3 preflight retains 471 unchanged counterparts and the exact nine host activations.
                PID 93628 starts the fresh 630-input matrix at `2026-10-03T13:34:35.7344420+00:00`
                ([record](../../TestRuns/IsaQwordPadding_2026-10-03.md)).
                All six profiles pass at `2026-10-03T14:07:35.1376140+00:00`; independent verification passes and
                immutable archival retains 630 inputs/23,526 evidence files at
                `2026-10-03T14:18:25.4242700+00:00`. This completes the eligible Close Project functional slice.
            - [x] S3b1b1b1d — Eligible default New, window close and noncritical session-end inputs preserve the
              prompt and acknowledge outline Cancel for Discard or End before Save. Reuse the same selected
              ordinal and editor/opening authority; a cancelled session generation cannot publish closure or
              post a late window close. Observe the callback-safe posted close in the harness before teardown.
              - [x] Seed 515 exact sources from the qualified Close Project private r1 archive, separately from
                frozen native sources. Primary owns production and fault/order tests; a cheaper agent drafts
                six independent literal New/window-close/session-end Discard/Cancel cases.
              - [x] Review source-only code and tests; compile only after actual Close Project native qualification
                and immutable archive. Preserve any failure before correction.
                Primary review completes the six literal and 127 fault/order/admission modes, registers 63
                application stages and replaces simulated younger cancellation with the actual continuation
                stop/unregister boundary. Primary corrects static helper lookup in the two external worker
                classes. The first compiler rejects a shadowed Close Window local; all 645 inputs and 197 evidence
                files are archived and independently verified before the isolated name correction. Fresh private
                r2 runs against the qualified Close Project predecessor
                ([record](../../TestRuns/SelectionLifecycleOutline_2026-10-03.md)).
              - [x] Qualify and independently archive the full private run: 43 literal checks/six modes, 1,529
                fault/order checks/127 modes and all 63 stages pass. Every archived hash/length and the exact
                519-owned/126-external input partition are verified before native activation.
              - [x] Qualify all six fresh native profiles, independent verification and exact archive: 632 inputs,
                23,532 evidence files, 43 literal and 1,529 fault/order checks per full x64 run, all 63 stages.
                Update the owning lifecycle contract; ARM64 is cross-build only.
              - [x] Commit and push the qualified step.
            - [ ] S3b1b1b1 — Remaining coexisting header, size and lifecycle routes need their exact prompt,
              cancel, snapshot/history nesting and acknowledged old-owner retirement policy before activation.
- [ ] S4 — Migrate wand with asynchronous all-layer composition and worker layer sampling/flood/trace; preserve
  all existing tolerance/channel/sample/contiguous/mode semantics and source-failure reporting.
  - [x] S4a — Qualify cancellable wand/mask-trace kernels and layer sampling before ordered host activation.
    Primary prepares a private candidate while the preceding native lease-shutdown matrix remains frozen.
    Share already contiguous pixels; otherwise read sparse pixels into private cancellable storage without
    publishing a source cache. Initialize mask/vertex storage in 4096-byte spans, and poll explicit matching,
    flood, vertex and edge walks at no more than 4096 work steps. Cancel returns no partial mask/geometry and
    a distinct cancellation result; retain existing None/Memory/TooDetailed and legacy numeric expectations.
    A cheaper agent supplies deterministic reached-checkpoint and literal parity tests; primary reviews them.
    Compile/runtime proof, paired kernel costs, original failure retention and all six native profiles remain
    required before activation/qualification. Allocator/OS latency, Clipper workspace, host ordinal/barrier
    integration, GPU all-layer sampling, memory admission and measured UI/G1 remain separate requirements.
    Private Release r1 passes all 54 deterministic/literal cancellation controls and unchanged document tests;
    paired 45 MP costs regress by 34%/39%/19% for matching/flood/trace. Its exact sources, executable/PDB,
    124 compiler tracking products, logs and machine metadata are preserved before tuning. Chunked r2 again
    passes all tests, reducing matching/trace overhead to about 3% while flood remains 18% slower; its exact
    287 inputs and 161 evidence files are archived. It is not activated. Primary reviews the cheaper agent's
    odd-width/tail controls and corrects the cumulative neighbor checkpoint count before r3 compilation.
    The revised r6 candidate passes all 71 controls and unchanged document tests. Two paired Release runs
    retain near-baseline matching/flood/trace costs, including fragmented stripes; kernel medians and shared-
    desktop outliers are recorded separately from UI timing. Exact 289 inputs and 199 evidence files are
    archived before nine native source/test activations; a fresh serial six-profile forced rebuild is running.
    The native matrix passes all six profiles at `2026-10-02T17:49:56.7560093Z`. All 71 named controls pass
    in each full x64 suite; ARM64 cross-builds pass without a runtime claim. Independent manifest/byte hashes
    retain 591 exact inputs and 328 evidence files before the next build. Only S4a is complete.
  - [x] S4b — Prepare a sealed New selection from a settled active layer, with private sampling/trace and the
    complete after-document/history publication. Build privately while S4a native inputs are frozen. Preserve
    event-time point/settings/AA, exact active/mask/crop/document/history authority, `Magic Wand` versus no-match
    `Deselect` titles, no-op/redo and whole-graph retirement. Blank/folder sources can produce the exact full-canvas
    rectangle directly because every sample is transparent; prove this shortcut with independent literals.
    This core preparation excludes All Layers, Add/Subtract and live gestures, and cannot activate until its
    complete reservation/retirement bounds and tests qualify. No existing authority rule is weakened.
    Capacity proof must cover private sparse source and sampling overlap, mask/vertex buffers, scanline stack,
    coordinate/loop-vector growth and old/new allocation overlap, semantic selection roots, document/history
    construction and releasing owners. The eight-million-edge limit bounds accepted traces; Clipper booleans
    retain S0b's separate requirements. Opaque allocator/process headroom and observed latency stay distinct.
    Host activation then requires S2/S3b ordered inputs and move closure plus P1.3 utility readback for All Layers;
    retain drafts through acknowledgement and gate every overlapping writer. Direct New requests and clicks
    inside the selection must keep their current order and error behavior.
    Primary reviewed the cheaper agent's test draft, correcting matching tolerance, placed-pixel and disconnected
    component literals, moved optional ownership and reached active/mask-only authority fixtures. Added local
    cancellation at all six history stages, recovery, nested transaction/saved-marker cases, phased memory
    planning and a real gated worker handoff of both applied and stale prepared-result aggregates. Final r4 passes
    117 unique assertions and the unchanged document suite. Earlier r2/r3 exact inputs/images are retained before
    strengthening recycle carry and dropping the test's observation owners before worker disposal. The final exact
    296 inputs, image/PDB and 136 evidence files are archived before native activation. Native r1's full x64 ASan
    and ARM64 builds pass; Release stops at linked ISA scanning before runtime and Debug does not run. Exact
    failure inputs/images/objects/ISA evidence are retained before repair. The original image recheck proves only
    one DWORD-table row changes classification under the proposed RDI-index data proof, with instruction count
    and runtime allowlist unchanged. Synthetic refusal controls and fresh native qualification remain required.
    Scheduler/host admission and G1 remain pending. Compiler intermediates use a recoverable local `.build` path
    through the repository's normal intermediate directory after the workspace-space failure.
    The fresh combined r2 matrix, exact source/control verification and independent archive now pass against
    598 inputs and 7,223 evidence files. All 117 wand controls run in each full x64 suite, with unchanged goldens;
    ARM64 builds pass. This completes S4b core preparation ([record](../../TestRuns/WandLayerSelection_2026-10-02.md)).
  - [x] S4c — Qualify the settled active-layer New production host route. Own point/settings/AA before queueing;
    complete phased admission precedes capture/resume, worker source/flood/trace/history, current-only apply,
    receipt acknowledgement and aggregate retirement. Consecutive queued New requests retain the existing
    coalescing/ordinal-gap rules and superseded receipts; this does not imply accepting additional pointer presses
    through the current busy guard. Ignore outside/nonfinite clicks as the direct reference does; in-canvas
    no-match deselects. Prove the actual RunMagicWand caller, literal source/output, captured settings, no-op redo,
    ordered younger history, cancellation, reached faults/stale authority and structural message pumping privately
    before activation. All Layers, Add/Subtract/Intersect, gesture closure and complete pointer replay remain S4/S3b.
    The primary-reviewed isolated source has sixteen Application modes, including actual scheduler record-table
    refusal/recovery. Latest coalesced clicks sample a different pixel with a distinct literal outline; the shared-
    ordinal gap is reached between adjacent wand requests. Posted-window pumping holds the worker until the
    message is observed. Outside/nonfinite/empty cases use actual queued receipts and preserve whole geometry/
    history. Settings/AA change before owner capture to prove event-time ownership. Helper assumptions are checked
    against the actual harness markers before execution.
    The complete private Debug/WARP suite now passes all 86 unique checks across sixteen modes and all 49 source-
    registered harness stages, with unchanged compatibility suites. Exact 475 inputs and 197 evidence files are
    independently verified and archived before nine native activations. The fresh serial six-profile forced rebuild
    passes full x64 suites and all ARM64 cross-builds. Exact control/source verification and independent archival
    retain 604 inputs and 877 evidence files. Every x64 run passes 86/16 wand-host controls and all 162 predecessor
    settled-selection host names; source-ordered 49-stage application execution and all core names are retained.
    This completes only S4c's settled host boundary ([record](../../TestRuns/WandLayerHost_2026-10-03.md)).
- [ ] S5 — Bound and cache coverage and displayed geometry under exact immutable selection/grid/transform keys.
  Avoid full outline construction during every pointer/frame/chrome sync; preserve exact coverage goldens.

### S5 selected raster/GPU integration sequence

- [x] Prove fixed-DIP coverage, eight dash phases, view clipping, zoom invariance and large anchor subtraction privately.
- [x] Prove the built-in two-R8 draw pass against independent source-over bytes, clipping and unbound borrowed inputs.
- [ ] Integrate output buffer/data/control accounting under one immutable local limit; qualify insufficient/sealed
  refusal, unchanged failed result, real original partition, copied/final weak ownership and consumed refund.
- [ ] Define one complete parent admitted before capture. CPU envelope includes held source/current/replaced
  ownership, nine-plane output, reusable four-byte WIC scratch, point/loop geometry, wrapper/control/retirement,
  upload windows and fixed objects. GPU envelope includes all nine R8 destinations, staging/copy overlap and driver
  headroom. Checked overflow or any unknown bound refuses the operation. A local output ledger is not admission.
- [ ] Bind authority to held selection identity, session/document serial, source anchor/translation, exact grid,
  zoom/DPI and a monotonic view epoch; graphics authority additionally holds the actual device and generation.
  Same numeric generation on another device cannot adopt. No pointer-only key may outlive its strong source.
- [ ] Prepare CPU data and create GPU resources on qualified worker lanes; use reported driver concurrency,
  complete reservation and bounded recorded upload batches. Unsupported concurrency cannot fall back to a large
  owner allocation. All nine planes publish atomically after actual producer/upload acknowledgements.
- [ ] Integrate one draw immediately before chrome composition, clipped to the physical canvas hole, preserving
  selection-under-draft/handle ordering. Pass9 selects solid plus one of eight phase textures; it updates only
  fixed constants and one draw. SyncChrome/SyncView/frame/paint do not walk contours, create paths or copy pixels.
- [ ] Replace every changed-selection producer's owner construction, including marquee/lasso, selection move,
  All/Deselect/Inverse/Expand/Contract/Wand, load, Undo/Redo, replace/resize and lifecycle publication. Draft-only
  overlays remain separately bounded. Preserve event-time caller policy and original ticket/order.
- [ ] Coalesce desired view epochs; reject obsolete worker/upload results and re-admit the latest exact key.
  A raster for another zoom/DPI cannot be rescaled as a final fixed-DIP result. Specify and test the pending-view
  rendering policy before changing visible behavior. Settled matching source/view performs no rebuild or upload.
- [ ] Keep current/replaced GPU use and CPU/control ownership charged until actual completion/destruction and
  consumed acknowledgement. Cover empty/absent selection, source replacement, hide/show, device rebuild,
  partial allocation/upload, cancellation, stale refusal, weak controls and joined Close without early refund.
- [ ] Qualify integrated application pixels/lifecycle in Debug/Release/real ASan and analysis/ISA/ARM64 cross builds,
  then measure complete source capture, preparation, upload, first/warm draw, Apply/retirement and inclusive G1.

The current 64 MiB opaque D2D allowance is planning headroom, not a proven allocator ceiling; DrawGeometry/EndDraw
are uninterruptible opaque calls. Keep these limits explicit and measure/categorize their cancellation and
memory behavior under the parent contract. Private component passes cannot turn that allowance into a bound or
close G1. Approximately 0.8-0.9 s of isolated CPU preparation remains a concrete latency risk to resolve.

### S5 next engineering checks

Private actual application Release r4 now preserves all predecessor assertions/stages plus 34 diagnostic
checks and 15 observations (`C81BF500…`). The fixed grid's path append is 7.06 ms and inclusive frames
are 51–142 ms; both draw calls return within 0.006 ms combined. Complete integrated/configuration proof,
long contours and G1 documents remain due. [Exact record](../../TestRuns/P1SelectionOutlineRendering_2026-10-12.md).

The qualified point transport removes numeric conversion from Apply. `CanvasOverlay::EnsureSelectionResources`
still creates a path, walks every loop, appends all points and closes the sink on the owner; the two
`DrawGeometry` calls remain another possible first-use cost. These are separate costs to measure and remove.

- [x] Freeze and run an independent 64x64 Direct2D compatibility probe: same-factory control, worker
  geometry from separate single-threaded/multithreaded factories, owner/peer stroke styles and exact `EndDraw`
  HRESULTs. Observe the actual `ID2D1DeviceContext` method instead of inferring compatibility from the generic
  error name. Corrected Release probe passes; separate worker factories return `D2DERR_WRONG_FACTORY`.
  The original hollow-bounds failure remains preserved. The isolated probe does not qualify an application route or latency.
- [ ] Measure path creation, point append, sink Close and first/subsequent draws separately with the application
  harness, retaining original inclusive owner/frame measurements. Include the fixed 32,768-region selection,
  long contours, holes, view changes and the G1 documents. Do not attribute the whole first frame to one API.
- [ ] Select and privately prove an admitted rendering route after those observations. It must prepare expensive
  geometry/tessellation/uploads off owner, use bounded owner submissions, retain exact source/view authority and
  transfer replaced CPU/GPU owners through the original ticket. Merely changing factory type does not prove
  non-blocking execution; Direct2D serializes access to resources from a multithreaded factory.
- [ ] Preserve closed contours, miter join/limit 10, fixed DIP stroke width, four DIP on/off dashes, the eight-step
  phase, white underlay/black dash, anchor precision, clipping and pan/zoom semantics. Add independent numeric
  references and real WARP parity/resource-lifetime cases before production activation.
- [ ] Qualify refusal, cancellation, stale source/view, device/factory replacement, hidden completion and joined
  Close; then measure the complete application path against G1. Coverage construction and the other selection
  producers retain their own required admission and parity gates.

The main-owned raster and nine-plane mask experiments also pass in isolated Release: all eight phases of line,
triangle and the fixed large grid match the independent source-over reference with zero maximum byte delta.
Nine A8 planes use nine bytes per viewport pixel instead of thirty-two for eight composed BGRA frames.
Large worker preparation remains about 0.8–0.9 seconds; admission, cancellation, view/DPI keys, upload windows,
retirement and inclusive application measurements remain required. These diagnostics do not close S5 or G1.
Private experiments live under `.build/P1Drafts/SelectionGeometryInterop-r1-Main`,
`SelectionOutlineRasterInterop-r1-Main` and `SelectionOutlineMaskInterop-r1-Main`. They do not change the pin.
Factory/threading constraints are documented in Microsoft's
[Direct2D threading guidance](https://learn.microsoft.com/en-us/windows/win32/direct2d/multi-threaded-direct2d-apps)
and [error-code reference](https://learn.microsoft.com/en-us/windows/win32/direct2d/direct2d-error-codes).
- [ ] S6 — Native ordering, failure, stale, cancellation, device/source and retirement proof; clean configuration
  matrix, active-desktop G1 measurements, owning specs/evidence update and commit/push each qualified slice.

## Current routes and parity

`OnCommand` routes settled Select All/Deselect through the qualified actor; the qualified outline callers also
acknowledge their exact End/Cancel prerequisite before the selected command or tool. Other live stroke/warp/shape
paths remain direct. Inverse and layer/mask selection still use their direct geometry engines after any required
outline closure. `OnSelectionAction` supplies Expand
and Contract amounts; lasso/marquee completion calls ApplySelection booleans. Settled active-layer New wand uses
the qualified actor; All Layers, booleans and live wand still synchronously compose or render the source before
flood/trace. The direct engine APIs stay as reference routes. Outline move is a live
gesture and must be explicitly ordered or suspended while a worker owns its base; leaving it outside a first
kernel slice does not exempt its overlapping mutation from the host barrier.

References include `selection.*`, `modify.*`, `marquee.*`, `load.*`, `loadrefused.*`, `wand.*`, `wandsession.*` and
the exact `coverage.*` cases in DocumentTests and the corresponding legacy SelectionTests/MagicWandTests. Inverse
keeps its existing history title `Inverse`. Contract collapse retains an explicit empty selection; Deselect removes
the optional selection. Select All uses AA=true; other modifications preserve their captured AA flag.

### Outline closure caller contract

The following classification comes from the direct Application/session routes. Migration must preserve each
caller policy; one generic "settle everything" rule is insufficient. End closes the live transaction at its
moved geometry, Cancel restores its original geometry, and Preserve keeps its opening/history depth live.

| Caller | Reference outline policy and dependent action | Owned migration/proof |
| --- | --- | --- |
| Moved `SelectionRelease` | End only; freeze further samples | S3b1a qualified typed exact opening; 242 controls in each full x64 suite and all ARM64 cross-builds |
| Direct Escape / first Undo / first Redo | Cancel; consume this edit before ordinary history restore | S3b1a; retained redo and exact original outline |
| `EndPointerDrag`, capture loss | End moved outline; preserve polygonal-lasso leave semantics separately | S3b1b0 qualified; real producer held-worker control |
| Changed tool / tool-selection command | End before tool state changes; same tool preserves | S3b1b0 qualified; exact event opening, failed closure never switches tool |
| Cut / Paste / Layer via Copy | End before their admitted pixel/clipboard action | S3b1b0 qualified; each real command reaches refused dependency control |
| Eleven destructive panel-opening commands | End before opening the selected panel | S3b1b0 qualified; each actual command refuses its panel if closure fails |
| Six new adjustment commands / Add Mask / Add Black Mask | End before adding the layer/mask | S3b1b0 qualified; per-command refusal controls |
| Select All / Deselect / Inverse / Load Layer Selection / Load Mask Selection | End before geometry publication | S3b1b0 qualified closes first; only Select All/Deselect already have the qualified prepared geometry engine |
| Queued Undo/Redo with only the outline active | Cancel and consume; Undo behind an older successful End restores its completed entry | S3b1b0 qualified; literal ordering and preserved redo |
| Zero-offset release inside outline | End, then Deselect; Wand instead performs New sampling at original press point | S3b1b1; retain point/settings/AA/tool and keep both actions ahead of younger writers |
| Selection header kind / Deselect | End even when a kind value is unchanged, then apply captured header action | S3b1b1; own kind/value and exact gesture opening |
| Header mode/AA/wand settings/amount controls | Preserve; mutate settings for later operations | Retain event-time values without silently closing the outline |
| Expand / Contract | Preserve outline transaction while applying current nested selection edit | S0b/S1b/S3b1b1; bounded geometry plus independent nested title/depth proof |
| Selection nudge | Starts and closes its own move when settled; an already live outline move makes Begin fail and remains unchanged | S1c; capture delta/Shift at input, prepare translation/history and preserve existing live-outline no-op |
| Enter / Save / Export | Preserve outline; other live edits follow their own caller policies | Keep existing behavior; actual writer/earlier-edit integration controls remain required |
| Copy / Copy Merged / raster edits without an explicit End | Preserve reference nesting; preceding gradient/color work has separate rules | Do not add them to End classification merely because they are document commands |
| Size / replacement / project/window/session close | Own caller-specific prompt and live-graph outcome | S3b1b1 plus qualified lifecycle boundaries; preserve Cancel answers and retire complete old ownership |

S3b1b0's source and controls qualify in all six native profiles. Its 27 actual End-before-command refusal routes do not
qualify the dependent Inverse/trace/filter/pixel kernels, general geometry capacity, coexisting earlier-edit
policies or measured G1. The next header private compiler must follow the current S3b1b1a native r2 qualification
and archive; its source preparation does not consume dependencies while that matrix runs.

## Authority and publication

An owned typed request retains event-time operation, amount/mode and gesture/source arguments. At its selected
ordinal, capture current immutable geometry and whole-document authority only after complete admission and earlier
settlement. Capture also retains active/mask/crop/pending-gesture identity and immutable history epoch. A source
trace retains its raster/placement/mask, and a wand sample must correspond to the same document/source token.
Workers must not access a live session, Application, HWND, chrome, immediate context or borrowed gesture storage.

Worker output binds the complete geometry to its private after-document and history publication. It includes every
snapshot, trim/index allocation and replaced geometry/display/history owner. Owner completion validates authority
before any swap and constructs no history. Cancellation, invalid geometry, source failure, allocation refusal and
stale input publish no partial outline and cannot execute a younger dependent action as success. A true unchanged
result keeps existing revision/redo semantics; acknowledged End still obeys the opaque mutation-stamp contract.

Prepare history together with the geometry result before owner publication. Full record capture and admission traversal remain H2/H3
costs until those shared-root/accounting slices qualify.

## Admission and cancellation requirements

### Current boolean implementation review (2026-10-09)

- [x] Obtain and independently verify the exact Clipper2 2.0.1 implementation against the pinned vcpkg
  SHA-512 and all nine installed public headers. The official `Clipper2_2.0.1` archive hash matches the installed
  SPDX package and baseline port; all thirteen implementation files match the archive byte for byte.
- [x] Review allocation and failure paths in `clipper.engine.cpp`, `clipper.engine.h` and `clipper.h`.
  This is source review, not an allocation ceiling or native cancellation/fault qualification.
- [ ] Implement an enforceable boolean workspace ceiling and interruption points before ordinary Add/Subtract
  activation. A geometric output estimate alone does not cover library working storage.
- [ ] Preserve one original Wand ticket, FIFO ordinal, source/history authority and reservation across trace,
  complete boolean admission, publication and retirement. Grow that same reservation only after the first
  worker has returned and actual immutable outline counts are available; refusal publishes nothing.
- [ ] Qualify both active-layer and detached all-layer sources, empty/no-match outcomes, captured AA/mode,
  selected/clipped geometry, exact history and actual Cancel/stale/shutdown at each boundary.

The verified source is the official
[Clipper2 2.0.1 release](https://github.com/AngusJohnson/Clipper2/releases/tag/Clipper2_2.0.1), SHA-512
`ce753ae3752b7516a9e0cb23c9788d9533c204819d5451ee50cf3b69a06e24165fa9f5270781764c036ad597eef83cf80532a5bebce5c96f5230179dc3ed499a`.
The local source and main verification receipt are under `.build/P1Drafts/WandBoolean-r1/`; these are preparation
artifacts, not a native feature receipt. Current production Add/Subtract still uses the direct reference route.

Specific remaining constraints:

1. `AddPaths_` allocates its vertex array before allocating local minima and before storing that array in
   `vertexLists`. `NewOutRec` likewise allocates a raw record before growing its owner list. Any bounded
   allocator/admission implementation must make these intermediate owners exception safe and inject refusal
   at the second allocation; catching `bad_alloc` at the job boundary alone does not prove cleanup.
2. Intersections, horizontal trial segments/joins, duplicated `OutPt` nodes and cleanup-created records all
   contribute to the peak. Include private sort buffers, converted `PathsD`/scaled `Paths64`, final loop growth,
   history construction and every retired source/result owner. Cached point counts are usable descriptors,
   not a proof that every library allocation is linear in those counts.
3. `ClipperD::Execute` reports success, but the free BooleanOp/Union/Difference/Intersect convenience wrappers
   discard that result. A cancellable/budgeted adapter must consume the explicit execution status and reject
   partial output; an interrupted operation must not be interpreted as a successful empty selection.
4. No cancellation callback bounds the existing sorting/intersection/cleanup loops. Adding a token check before
   or after the call does not establish the stop-to-return target. Any library change must preserve precision
   two, NonZero fill, path ordering where observable, existing numerical goldens and the default reference API.
5. Prefer scoped allocation enforcement to an invented linear estimate or a global allocator override. If a
   pinned library patch is required, version and reproduce it through vcpkg, keep the original source/license,
   and qualify cleanup and all existing geometry goldens in the affected native profiles. Do not silently
   lower the eight-million tracing limit or publish a speculative ceiling as complete admission.

Pinned Clipper2 2.0.1 review confirms that `BuildIntersectList` retains pair intersections in a vector, and the
offset path performs a preliminary Union and a cleanup Union after round-point generation. `CalcSolutionCapacity`
counts path slots, not vertex/workspace capacity. The eight-million-edge limit applies to wand/mask tracing;
ordinary Boolean/offset inputs have no corresponding universal limit. Coordinate range checks do not bound
allocation counts. A linear estimate based on traced-edge limits therefore cannot qualify these operations.

Current command review also confirms that Select All/Deselect leave active brush/warp/shape gestures live. Their
ToolSwitch finish/drop rules cannot be reused as settlement for a selection command. S2 must preserve those
pending owners and ordered samples under exact gesture authority, or qualify an explicitly specified ordering
policy. The settled-only S1a module does not authorize a production caller regression. Ending an outline move
also remains an allocating history caller until its prepared closure is implemented.
S1a proceeds independently because Select All creates exactly one four-point rectangle and Deselect creates no
geometry. Its sealed capture owns the before-document, history view and settled-session authority; changed output
owns the complete after-document/history publication. Geometrically equal Select All and already absent Deselect
acknowledge without changing history epoch, revision, redo or generation. Moved inputs/publications invalidate
their source explicitly. Host activation and the overlapping-writer barrier remain S2, with no functional claim
until independent preparation and native host qualification pass.

Geometry ownership already has cached retained bytes and point counts. Use those cached values before capture;
do not inspect every point in the command callback. A complete conservative bound must include input builders,
Clipper intersections/joins, concurrent workspace/result vectors, private document/history construction and release
owners. Derive the bound from the pinned implementation rather than assuming a linear multiple is universally
sufficient. An unbounded library allocation graph cannot pass S0; representable overflow refuses the operation.

Clipper calls currently offer no per-edge application cancellation hook. Record checkpoints before/after each
library call and measure the uninterrupted call separately. Do not claim a cancellation deadline from an early
token check. If the existing maximum input cannot fit a reliable admission/cancellation policy, propose and test
the exact contract change before publishing it; do not silently lower the legacy edge/shape limits.

Coverage and source raster work use private cancellable region reads and span/row checkpoints. A selected request
retains its complete reservation through final apply/drop and retirement; a memory wake alone does not mean a
worker finished. View input stays available and hidden work does not require rendering or Present.

## Required new proof

Independent literals cover operation parameters/AA, nullopt versus empty, coordinates, Undo/Redo/title, named nested
transactions, saved-revision and redo retention. Prove every injected allocation/cancellation checkpoint was reached
before unchanged-state assertions. A moved/foreign/single-use or stale selection/document/target/crop/history result
cannot publish. Keep source and geometry owners alive through off-owner retirement.

Host gates hold an operation while admitting younger Select All/Deselect/modify/gesture/wand/write/tool/close inputs;
then verify FIFO through acknowledged apply/drop, exact captured parameters, original error and zero leaked owner,
reservation, readback lease or hidden Present. The final clean Debug/Release/ASan suites and ARM64 cross-builds
qualify functionality only. Active-desktop measurement must separately satisfy the due G1 input/apply/frame rows.

## Next physical press integration requirement (2026-10-16)

The Main draft `.build/P1Drafts/S5PressHost-r1-Main/Source/RedPrism/ApplicationSelectionPressOpeningJobs.cpp` performs exact worker containment and prepares either scalar Move opening or bounded Lasso opening under one original admission. It remains uncompiled and unactivated; owner declarations/dispatch, actual prefix replay, terminal/lifecycle behavior and caller tests are unfinished. Its ownership prerequisite privately passes all 22,033 CPU Release and real-ASan facts; [evidence](../../TestRuns/P1SelectionHistoryOwnership_2026-10-16.md).

Before activation, resolve closure when ordinal or memory capacity is exhausted. An admitted terminal must not depend on obtaining a new ordinal, fresh job slot or unreserved closing memory after the gesture has begun. Prefer retaining an originally admitted resumable parent through accepted input and terminal settlement, with event-driven owner rearming and existing lifecycle preflight. Prove exact Move/Up/Lost/overflow and queued-writer ordering, cancellation/rollback, no-op click, original output/temporary/weak-owner charges and joined Close. Do not add an owner translation/history fallback or silently roll back the reference pointer-loss End policy. Ctrl pixel lift, Wand and preserving-panel authorities remain separately required. The prototype's opening success alone closes none of those delivery gates.

### Continuing-output ownership prerequisite

The existing final-publication memory API transfers the entire original record admission and seals both
output and temporary owners. Its contract permits only retirement after that transfer. Applying a gesture's
opening with that API and then resuming useful closing work would violate the ownership contract.

Main's private `S5IntermediatePublication-r1-Main` candidate introduces one explicit descriptor-bounded
continuing output. It moves only that output's actual accounted envelope into the existing persistent
endpoint, keeps the original ticket/FIFO/cancellation source and terminal slot, and supplies subsequent worker
stages only the originally admitted remainder. It performs no query, allocation, admission or early global
refund at transfer. The complete operation cannot grow its admission. The output seals independently;
remaining temporary/terminal owners stay available until their final acknowledgement or joined Close.
Default descriptors and final-publication APIs keep their existing behavior.

The candidate remains unadopted. Private Release independently qualifies 38 source-derived controls for real
resumed useful work, exact memory partitions, descriptor/ticket/bounds/repeat/growth refusal, failure,
cancellation, joined Close and last weak-control refunds. It must also prove terminal-slot pressure, source
history/geometry ownership and the full physical host before use. The older press-opening prototype is not
activated by this prerequisite. No ordinary activation, G1 or P1 Done claim follows from a private proof.

The extended private Release proof now qualifies all 22,100 facts: 44 continuing-output checks, 23 bound-history
checks and 22,033 predecessors. All 64 terminal slots are preclaimed before opening, a 65th is refused, and
the original job still completes its useful terminal stage. Real ASan of the same source independently qualifies all 22,100 facts, 265 instrumented compiler records, an actual UAF detection probe and all 1,604 archive files. The primitive remains private; actual physical dispatch and lifecycle qualification are due.
[Detailed evidence](../../TestRuns/P1ContinuingPublication_2026-10-16.md).

No-move physical Up needs an additional useful follow-up after End: Deselect for Marquee/Lasso, or the captured
New-wand operation. The zero-offset geometry helper already shares the original root without allocating.
A prepared zero-offset End can therefore publish no new graph while the admitted parent retains its closing
reservation; capture the follow-up only after that real End acknowledgement to preserve its token/generation.
Use a separately prepared charged final selection-command publication before terminal memory transfer.
Pointer-loss End preserves the selection and must not synthesize the click follow-up. Moved, no-move, cancelled
and Lost branches require real host tests before activation. Do not resume useful work after a final transfer.

### Original-parent CPU protocol and charged click follow-up (2026-10-16)

Private Release R3 independently qualifies 22,220 facts: 30 charged command, 33 original-parent lifecycle,
57 opaque cleanup and all 22,100 predecessors. It preserves the actual failed fixture compile and four lifetime
predicates before repair. Source/dependency/product bookends, all 1,602 archived files, 268 compiler records
and exact executable/PDB identity pass. Real ASan is building the same frozen source.
[Detailed evidence](../../TestRuns/P1SelectionGestureParent_2026-10-16.md).

The CPU protocol proves one original admitted parent through moved End, no-move End then newly captured
Deselect, Lost End, worker failure, cancellation and joined Close. Opaque cleanup validates the exact Begin
predecessor and latest own scalar witness, including foreign pending-transform and transferred gesture guards.
This does not activate the uncompiled Application press prototype. Physical prefix replay/overflow/ordinal
exhaustion, captured New-wand and Ctrl/preserving branches, complete display/first-upload consumers, native
adoption, ordinary activation and G1 remain required.

## Next selected consumer: exact cursor containment off the owner

Code inspection on 2026-10-17 confirms a remaining owner walk in `Application::CursorOverSelection`
(`Application.cpp`, the call to `CanMoveSelection`). The current memo avoids repeated work at one
position, but a new in-bounds pointer position can still visit the complete outline. `CanMoveSelection`
also materializes translated geometry for the deferred-move numeric reference. The 50,000-point cursor
fixture in `ApplicationChromeSync.cpp` currently expects those synchronous calls; it must be adapted with
its literal inside/corner/outside expectations intact. This is source inspection, not timing evidence.

Implementation slice after preserving-marquee host qualification:

1. Capture only a fixed request descriptor in the cursor callback: document/session lifetime, current
   selection/translation authority, view/cursor epoch and exact query point. Outside cached finite bounds
   is an immediate false. Do not retain the geometry root or construct translated vectors before complete
   admission. Describe retained/control/retirement needs from cached immutable metadata.
2. Bound outstanding work to one original cursor ticket and one replaceable latest descriptor. Repeated
   requests for the same source/point reuse the pending or exact cached result. A changed query cancels
   obsolete work and waits for its acknowledgement before replacement dispatch; no timer or polling loop.
   Finite-value/counter/admission failures must not fall back to an owner geometry walk.
3. Acquire the immutable display value only under the admitted original record and matching source witness.
   Run `ContainsSelectionForWork` on a worker, preserving per-vertex translation and the existing segment
   tolerance; subtracting translation from the query is not equivalent at large coordinates. Preserve
   `CanMoveSelection` eligibility and collapsed/explicit-empty geometry semantics separately.
4. Publish only if request, source, translation and cursor/view epoch still match. Pending probes use the
   neutral selection cursor; a stale result cannot affect a later document or pointer. Refresh only the
   current application's cursor through its existing message route, never a different window's cursor.
   Pointer Down must retain its independent exact worker classification.
5. Join cancellation/retirement before Application, HWND and source owners disappear. Apply performs no
   traversal, point construction or final large-owner destruction. Preserve weak-owner charges and existing
   scheduler wake/idle behavior.
6. Add independent host controls for literal inside/corner/outside and boundary/holes/translated cases,
   source replacement, repeated point, rapid latest-point replacement, refused admission, epoch exhaustion,
   hidden completion and joined Close. Assert bounded dispatch/capture counts and zero owner edge walks.
   Retain the existing full cursor fixture's geometry expectations; no measured timing gate is inferred
   from structural counters. Complete Release/real-ASan and final native qualification before activation.


## Freehand selected component — qualified 2026-10-17

The typed original drawing plan, admitted arena allocation, guarded Freehand writer and actual physical
caller are implemented and adopted with scoped Release/real-ASan proof. The earlier missing Lasso
routing and hardcoded one-page drawing opening are corrected. The sequence below records that
component's design and the remaining activation/Polygonal obligations; the opening checklist and
[receipt](../../TestRuns/P1FreehandHost_2026-10-17.md) state its current qualification.

Implementation sequence and boundaries:

1. Define a typed captured drawing-kind policy shared by cached admission, original parent and physical
   dispatch. Capture tool/kind/mode/AA at accepted Down; mutable header changes cannot rewrite them.
   First implement Freehand New/Add/Subtract. Polygonal multi-Down/Backspace/close semantics follow as
   their own component, rather than treating every polygon Down as a conflicting new gesture.
2. Establish one finite accepted-input/page ceiling from the existing bounded prefix/arena APIs and
   derive checked Down-time memory before any geometry/history capture. The descriptor must cover the
   source's cached points/loops, all reserved arena pages, traversal, clip/boolean workspace, maximum
   output/control owners, source/history/display refresh, construction and retirement. Use that exact
   page count in `PrepareLassoOpening`; the marquee one-page shortcut is not valid for long paths.
   Limits are enforced capacity policy. Overflow/capacity refusal cancels the original without a partial
   selection, replacement admission, unbounded vector growth or owner fallback.
3. Reuse the original record and accepted physical prefix for containment, move-versus-draw choice,
   prepared opening, ordered append, terminal closure and acknowledgement. Preserving modes always draw.
   Keep cursor/rubber-band display state separate from accepted vertices. Verify Up sampling, quarter-pixel
   skips, modifiers and no-point behavior against the current numeric path and the named Swift oracle
   before changing those semantics. Source/session/history/opening authority must be checked at every
   owner acquisition and publication boundary.
4. Use the normative bounded metadata route: the worker allocates and prefaults the complete arena;
   owner replay consumes at most sixteen accepted moves, each touching at most one 64-point chunk.
   This preserves the existing single-writer/snapshot protocol without a new mutable-writer transfer.
   Full traversal, clip/boolean/history preparation and graph retirement stay on workers under the
   original reservation. This refines the earlier worker-append proposal; preserve the 2 ms owner
   budget and keep ordinary activation/G1 unqualified until measured. Retain every arena, original
   geometry, temporary result and weak/control owner through actual retirement. Late Move/Up/Lost after cancellation
   or foreign writer cannot consume a new gesture or replace foreign state. Joined Close must release
   captured roots on workers before the owner's final acknowledgement.
5. Add independent CPU event-vector and literal geometry/coverage tests: absent/empty/inside/outside,
   New/Add/Subtract, more than one arena page, long collinear paths, self-intersection, minimum-point
   closure, accepted-point capacity, prefix overflow, cancelled opening/append/finish, stale document,
   target/history changes, duplicate terminal, allocation refusal and original-ticket memory lifetime.
   Extend the actual hidden-window fixture to send the production Freehand pointer sequence, capture
   loss and cancellation, including cancelled-before-capture and active joined Close. Preserve every
   predecessor record; keep fault receipts and full source/binary identity.
6. Close the component with code, contracts, opening checklist, affected validation and commit/push.
   Ordinary activation and G1 remain separate until every display consumer and complete operation bound
   is qualified. Then implement the polygonal continuation protocol and the remaining Wand/Ctrl-pixel-lift
   branches; do not declare the selection delivery area complete from Freehand alone.

## Next component: Polygonal original-parent continuation

1. Keep the multi-click protocol separate from drag-Up finishing. `PlanDrawing(Polygonal)` remains
   refused until its own caller exists. Reuse the existing 512-slot `HostLassoInput` point/cursor/
   Backspace/conditional-close/finish descriptors and shared nonwrapping host event ordinals. Pointer
   Up is nonterminal; hover changes only the rubber band. Capture kind/mode at the first accepted Down.
2. Use a finite checked plan for 65,536 live accepted corners and 1,023 arena blocks. Backspace/re-extend
   cannot recycle sealed nodes while snapshots exist, so arena exhaustion may precede the live-corner
   limit. Original admission includes retained source/history, traversal, worker clip/boolean/output,
   construction/refresh and strong/weak retirement. There is no replacement admission or partial result.
3. Add own-key guarded point/cursor/removal APIs. A quarter-pixel no-op preserves the key; hover never
   adds a vertex. Last-corner removal validates before Pop and resets only the matching draft/active
   marker in that same owner transition: Pop already ends the writer, so post-Pop ActiveOwner
   revalidation would incorrectly reject cleanup. Keep foreign writer/history/session state untouched.
4. Reconcile the long-lived original parent with ordered Save/tool/lifetime/selection commands before
   connecting physical input. Holding `_activeHostRequest`/HistoryWork indefinitely while waiting for
   more corners can deadlock a younger settlement command. Define ordered settle/cancel/yield
   arbitration without releasing original accounting or allowing foreign mutation early. Every older
   accepted corner/Backspace must precede its closing command; an idle live prefix requests no wake.
5. Preserve the actual conditional-close event ordinal. The legacy selected terminal has a default
   ordinal zero while its separate closing intent retains the physical ordinal; a new original parent
   must retain that real closing identity. Near-first closes only with at least three corners and the
   legacy eight-view-pixel hit region; double-click/Enter retain short-path behavior. Physical terminal
   AA belongs to the chosen close. Rejected early proximity presses acknowledge without history.
6. Independently qualify the finite plan, triangle/self-intersection coverage, guarded append/hover/
   Backspace, last-corner cleanup, immutable held snapshots and exhausted-arena refusal before host
   integration. Then actual Win32 tests cover multi-Down/Up, hover exclusion, two-versus-three-corner
   proximity, double-click, Enter/Escape/Backspace-to-zero, view/DPI/header changes, loss/overflow,
   foreign edits, queued commands, stale/duplicate close, memory refusal and joined strong/weak refunds.
7. Keep numeric reference goldens, complete predecessor rosters, code/contracts/evidence and the
   opening checklist synchronized. Commit/push the qualified component; complete remaining Wand,
   Ctrl-pixel-lift, coverage/display/transform consumers, ordinary activation and measured G1 before Done.
