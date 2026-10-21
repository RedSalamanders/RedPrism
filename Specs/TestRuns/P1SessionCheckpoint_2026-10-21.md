# Resume RedPrism P1 — saved 21 October 2026

Execution is stopped at the user's request to continue in another session. P1 is **WIP**.
This checkpoint supersedes older progress text that still says profiles or source freezes are active.
No implementation, benchmark or new CI dispatch should run until the user resumes work.

## 1. Start here

Use the existing checkout:

```text
C:\Users\eric\.codex\worktrees\p1-opacity-host-proof\RedPrism
branch: codex/p1-opacity-host-proof
source/test/spec component: f4c712c21c0cfcacdeaf16db9e1e225662bd38a9
published checklist:       1d1f71d4e301dd6b6b96b0b4a285a78c56c236a4
remote: https://github.com/RedSalamanders/RedPrism.git
```

The remote branch SHA was verified and this worktree was clean before adding the handoff documentation.
The handoff commit follows the published checklist commit; its exact SHA is in the checkpoint's
`final-publication.json`. Verify HEAD, branch and status before making changes. Do not reset unexpected changes.

`Z:\src\RedPrism` is an older, separate checkout. Its ignored `.build` folders hold current evidence and drafts;
it is **not** the P1 source checkout. Other `rf/*` worktrees now registered in the repository belong to other work.
Do not archive, edit, reset or remove them as part of this continuation.

Read the current checkout's `AGENTS.md`, `Specs/README.md`, relevant repo skills, the opening checklist and
§14 of `Specs/Plans/WIP/UiNeverBlocks_2026-09-22.md`. It is the P1 delivery plan.
`ImageProcessingAccelerationAndRaw_2026-10-04.md` retains the detailed P2–P6 roadmap. Finish P1 first.

## 2. User instructions and authorization that remain in force

- Finish P1 with code, tests, performance evidence, domain specs and guide; keep the checklist current.
- Commit and push each qualified step to the existing approved repository and branch above.
- Delegate bounded simpler work to cheaper agents; independently validate their conclusions and code.
  Main owns concurrency, GPU, publication/lifetime boundaries and performance acceptance.
- Do not declare P1 Done or merge main until its actual delivery and §14 gates close.
- The user has no personal native ARM64 machine. Cross-builds are never runtime evidence.
- Publicly licensed RAW samples and their download/recreation script were requested for later phases;
  use the existing corpus/roadmap rather than restarting that work during P1 closeout.
- Do not terminate a running RedPrism instance to rebuild. No extra managed worktree is authorized.
- One temporary DxUi checkout is explicitly authorized at the exact path in §5. Library publication and
  consumer pin adoption need a concrete qualified result; that approval is not an automatic pin change.
- Do not message other chats except within the already granted quiet-window scheduling scope. No quiet
  window is pending. The last one was explicitly released at 06:12:31 UTC on 21 October.
- Prior exact cache relocations/cleanups are completed or separately recorded; do not infer permission for
  new deletions. Preserve original failed receipts, assertions, goldens and timing misses.

## 3. Latest completed component and proof

Load Layer Selection and Load Mask Selection ordinary commands now use the admitted original-ticket worker
graph shared with layer Wand. Main traced the real synchronous owner calls; a helper's broad claim that no
ordinary path remained was false. There are no production DxUi changes in this RedPrism component.

The change includes immutable source kind, source-sized admission, mandatory canvas clipping even for New,
private sparse reads, alpha >=128 / mask <128, linked/unlinked transforms, worker history/outline preparation,
exact no-match no-op versus present-empty off-canvas behavior, stale/cancel/failure refusal and off-owner
retirement. Direct Common synchronous New/Add/Subtract reference APIs remain available.

The old detailed-selection refusal fixture now waits for the actual asynchronous acknowledgement while
keeping its original predicates. Its first candidate never reached runtime, so do not invent an observed
runtime failure or claim the sequencing correction causally repaired one.

The exact ISA scanner proof recognizes a validated bounded unsigned R9D / RCX image-base / R8 jump-table
shape in anonymous terminal data. It changes neither the runtime allowlist nor the pinned DxUi library.
Guard/body/PE/PDB/target/end-boundary proofs remain mandatory; real VEX/EVEX instructions remain gated.
All 4,368 predecessor scanner PASS records plus 85 independently encoded new controls pass.

All **seven** local attempts are closed and independently qualified on the same 1,901 frozen inputs:
`AB67BC90251ACF1C8870236A39C57832F721A1C958ED6A0090F9D94CD0C42B0F`.

| Attempt | Profile | Archive manifest SHA-256 |
| --- | --- | --- |
| load1r2 | x64 Release | 47F5D02D8D5C0CA138848C40CCFD889F7FE97C886B2324DC7B8537AEDB94B223 |
| load1d1 | x64 Debug | 7FA212E7664F281561410DC1A9DD13F4358B5012EF9FEEEBDDA977BDEA0A3828 |
| load1a1 | x64 ASan Debug | 833F4C08F92A0EA63C94C9354BAF1F3C616AAE1740A208036C0DC434902D6A2D |
| load1armd2 | ARM64 Debug cross-build | 9CCABE8A7430C114D8977A6E78EE5C6387177A0D9A22B6FDA0156082D8F49ADA |
| load1armr1 | ARM64 Release cross-build | CC175DD064A91CA72711C2816375A0301F8A6CA6EE18876EA954C67308E13E40 |
| load1arma1 | ARM64 ASan Debug cross-build | F896C133EB68530169E30C2406EA29D348C72274F0D099FE1B9297136673EB56 |
| load1an1 | x64 Release analysis | 46D1381005797C3D6556FD16D8C53C2101AAA598CFF8EC8AFF14EB448FCBF06B |

Every profile verifies 1,022 actual compiler/object/link records and five matching architecture/full-PDB
pairs. x64 runtime retains all 12,301 Application stdout facts / 155 stages, adds exactly 102 stderr facts
across 14 actual command cases and 32 Document facts, preserving all predecessor rosters. Do not combine
stdout/stderr totals into a misleading predecessor comparison. Genuine x64 ASan detection/stress, probes,
smokes and nine captures pass. Analysis includes effective flags and fresh passing/failing SAL controls.

Evidence and executable readers:

```text
Z:\src\RedPrism\.build\P1Evidence\p1-loaded-layer-selection-r1
Z:\src\RedPrism\.build\P1Drafts\LoadedSelectionQualification-Main-r1
  run-canonical.py, archive.py
  verify-profile-main-r2.py
  verify-runtime-main-r2.py
  verify-analysis-controls-main-r1.py
Z:\src\RedPrism\.build\P1Drafts\IsaR9dRcxR8-Main-r1
Z:\src\RedPrism\.build\P1Drafts\LoadedSelectionPublication-Main-r1
```

The public run documents are `P1LoadedLayerSelection_2026-10-21.md` and
`IsaR9dRcxR8_2026-10-21.md` under `Specs/TestRuns/`. All source freezes are CLOSED.
Do not rerun one-shot adoption, closeout, archive or publication scripts into existing directories.
Qualified predecessors need not be rebuilt merely to resume. Changed code requires its affected and final gates.

The normative history startup sentence was reconciled with actual ordinary startup and 44 independently
verified facts in each of three profiles. The helper's count of 43 was wrong. This was a documentation
reconciliation of already qualified behavior, not a new production startup implementation.

## 4. Native ARM64 r5: preserved failure, not a pass

```text
run: 39541110772, attempt 1
https://github.com/RedSalamanders/RedPrism/actions/runs/39541110772
source: c805d5d638eda67858a4d39f838f3656a5e78298
job: 119028817034, Native ARM64 Release feasibility
evidence: Z:\src\RedPrism\.build\P1Evidence\p1-native-arm-hosted-r5
```

It completed failed at 05:52:30 UTC. Canonical child PID 9936 exited 1; Application exited 2 with:
`The default client area does not match the current monitor DPI.` All 36 downloaded artifact files and
run/job/transport manifests are preserved. Independently verify source/environment/product/failure evidence
next. Download success is not semantic qualification. Do not redispatch automatically or run success readers.

Observed Polygonal subsets pass: dispatch 729/0 and press 1,002/0. The final stdout BEGIN for
`RunJobShutdownSelfTest` does not identify the failed function. The actual generic assertion is later in
`Application::RunSelfTest` (~744–762) after creating the normal hidden main window. Its combined predicate
checks nonzero DPI, two window styles, GetClientRect success and actual scaled 1280×800 DIP dimensions.
Individual values are not logged, so the precise cause is unproven.

`CreateMainWindow` (~2969) already always recalculates using actual window DPI and calls SetWindowPos.
Do not implement a guessed missing second resize. `OnGetMinMaxInfo` preserves OS maximum-track limits;
an OS clamp is a possibility, not established cause. Read `Specs/UI/UI_Windowing.md`; do not waive the
assertion or silently change the normative default-size contract. Introduce exact diagnostics or an
independent reproduction before selecting a repair. Keep the original failed receipts.

This hosted run precedes the new loaded-selection/scanner component and cannot qualify its native runtime.
The default 1,200-second wrapper bound is unchanged; 5,400 is the explicitly recorded native failure backstop.

```text
P1Drafts\NativeArmDpiCi-Main-r1\watch-published-run.py
P1Drafts\NativeArmDpiCi-Main-r1\resume-published-run-main-r1.py
  Both are completed one-shot observers. Do not rerun over existing evidence.
P1Drafts\verify-observed-arm-failure-main-r3.py
  Older failure reader: adapt into a NEW r5 reader with exact source binding.
P1Drafts\NativeArmDpiSuccessReader-Main-r1
  Success-only readers; MUST NOT be used on failed r5.
```

## 5. Preserve the unfinished DxUi experiment

Exactly one authorized temporary checkout exists:

```text
Z:\src\RedPrism\.build\dxui-p1-paint-r1
branch: ej/p1-bounded-embedded-repaint
base: f65e4a4b9311370b3e3070088e2a8abe29adcb27
```

It has six uncommitted fixture/plan/inventory paths. No production library source changes exist yet.
Those six files, a tracked binary patch and the exact base/branch/hash manifest are independently copied
and byte-verified in the session checkpoint folder in §8. Keep the checkout; do not reset or delete it.
The sibling `Z:\src\DxUi` has unrelated dirty documentation and must remain untouched.

New native fixture `Tests/Embedded/Embedded.Tests.BoundedRepaint.h` is registered and compiles, with route:
`DxUi.EmbeddedTests.exe --benchmark-bounded-repaint <report.json>`.
It uses the same macro-guarded baseline/candidate harness: 1280×720 WARP, existing complex scene, one cold
paint, one first mutation and 32 warm mutations, separate Prepare/Composite/frame/allocation data and
untimed outside-region pixel checks. There is no hidden warmup. The fixture is **not yet executed**;
compiled code does not establish pixels, state or performance acceptance.

Unchanged-library canonical x64 Release build succeeded with zero warnings/errors. Retained 18 files include
products/full-PDB/library and fixture sources. The retained manifest is
`366FCDF888FC98D5488BBA8628EBC718740D11CAFFBDFD1B40CB80E325C7945E`.

```text
P1Evidence\dxui-bounded-repaint-baseline-r1\
  started.json, result.json, preserved.json, inputs-before.json, inputs-after.json
  Retained\manifest.json
  Retained\Attestations\{build,incremental,supplement-manifest}.json
P1Drafts\Invoke-DxUiBoundedBaselineBuild-Main-r1.ps1
P1Drafts\Preserve-DxUiBoundedBaseline-Main-r1.ps1
P1Drafts\BoundedChromeRepaintExperiment_2026-10-21.md
P1Drafts\BoundedChromeRepaintDesign-Main-r1.md
```

The small build receipts are not complete effective measurement provenance. Before production edits, capture
the actual `Tools/PerformancePolicy.psm1` measurement/build state and matching logs/tlogs/source identity,
then execute/qualify the baseline. Use canonical nonforeground Embedded scope as appropriate. Do not invoke
a paired script that creates another checkout. Do not start foreground-input suites without the required
desktop-time agreement. Record other active load and qualify performance only under its real conditions.

Design to implement directly with Main review:

- Opaque attachment-lifetime weak owner plus private visual serial; tokens authorize no interaction/UIA.
- Optional bounded Prepare with complete caller-certified damage, outward pixel rounding and aliased clip;
  transparent clear plus normal full z-order painting/overlays inside clip, preserve outside pixels.
- Full fallback for initial/invalid/stale/foreign/retired/unknown mutation, bad geometry, tooltip/animation,
  resize/DPI/device/recovery or exhausted serial. Independent dirty tracking cannot lose invalidation.
- Balance clip/EndDraw on failures; hold actual COM/context/attachment lifetimes across paint callbacks.
  Inspect detach/root/device reentrancy before retaining State references or raw pointers.
- Independent literal pixels and full-versus-bounded WARP equality at 96/144/192 DPI, overlap, text overhang,
  overlays, fractional edges; exact stale/retired/reentrant/exhaustion/failure/recovery controls.
- One surface, no extra composition pass or owner CPU readback/copy. Report cold and unchanged CPU traversal
  costs even if clipping does not improve them. Measure before adding speculative traversal culling.

Two separate adoption constraints remain explicit: this DxUi base is API4 while RedPrism pins API3
`8e8994079e82f79e6765e6a885c03fc687957591`, and migrated paired policy is unqualified
(`policy-review-required`). Do not silently change the pin/API claim, approve the candidate's own judge,
or turn diagnostic measurements into accepted speedup/G1. Publication and explicit consumer adoption remain due.

## 6. Actual P1 work still open

The parent checklist and §14 control the exact boundary. Major open areas:

1. Complete ordinary prepared display/row/tool activation: Gradient/Pixel Move, live strokes, Shape/Warp and
   all consumers, with complete admission, capture readiness, UIA, epoch/retry and lifetime proof.
2. Real GPU readback/composition utility callers and device/suspend/restart/cancellation/shutdown grids.
3. Remaining large Wand/overlap/geometry/coverage/outline and deferred transform settlement work.
4. Files/clipboard complete phase/admission bounds and retained historical failure investigation.
5. Constant-time immutable capture, incremental history/memory accounting and weak/cache/late-source retention.
6. First upload/row/chrome Prepare costs; meet Image Size Apply <=2 ms including history/chrome/display ownership.
7. Every P1-due 45 MP /100 MP R1/W G1 measurement, latency/cancellation misses and exact allowed phase exceptions.
8. Latest-source final Debug/Release/real ASan, analysis, ARM64 cross-builds, native integration/availability
   records, formatting/skills, host acceptance, contracts/guide/child plan reconciliation and final publication.

Only move the parent plan to Done and update indexes after all required delivery gates close. The estimate
given to the user was 3–7 working days, explicitly low confidence, for P1 only. It is not a deadline or waiver.

`InitializeResponsiveSourcesOwner` remains restricted to explicit fixture/benchmark paths. Its ordered
acquisition is rows -> committed/pending display -> stroke -> optional outline/lasso, then async finish/
sample/warp enablement; partial-prefix failures join shutdown. Read owning contracts and all actual consumers
before enabling it ordinarily. Ordinary history, CPU cursor and recorded Hue have qualified independent
startup; do not confuse that with complete display activation or duplicate existing worker jobs.

Latest row diagnostics retain hardware Prepare 6.383/8.621 ms and WARP 7.387/6.520 ms for initial/first rename.
Apply maxima are 0.260/0.240 ms. These unpaired diagnostics establish no complete G1 or causal speedup.

## 7. Safe next session sequence

1. Read this handoff, verify source checkout/branch/current SHA/status and all copied experiment file hashes.
   Read current contracts and repo skills; reuse no proof across changed inputs.
2. Independently verify the failed native r5 source/environment/PE-PDB/rosters, then add exact DPI/style/client
   diagnostics or reproduce its actual condition. Preserve failures and avoid guessed cause/waivers.
3. Capture unchanged DxUi measurement/build state, run and retain baseline correctness/diagnostics, then
   implement the optional repaint boundary and independent tests with Main review.
4. Continue the remaining P1 checklist. Qualify each coherent step, record evidence, commit/push and verify SHA.
5. Run final complete gates only when implementation and measured acceptance are ready. Avoid repeating an
   unchanged large matrix solely for process ceremony or promoting a focused receipt to complete acceptance.

Build/test commands are the repository entrypoints. C: worktree writes and pushes may require the normal
escalated tool permission, with prior human authorization preserved. Never reroute to stale Z: source because
of a sandbox path issue. No app must be killed for a rebuild. Generated evidence stays ignored under `.build`.

## 8. Snapshot locations and recovery

```text
Z:\src\RedPrism\.build\P1Evidence\P1SessionCheckpoint-20261021T063520Z
  initial-state.json
  final-publication.json                 # written after the handoff commit is pushed
  owned-process-audit.json
  DxUi-working-tree-manifest.json        # exact six paths, sizes and SHA-256
  DxUi-working-tree\...                 # complete copies, including both untracked files
  DxUi-tracked-changes.patch             # supplemental; does not include untracked files
  Continuation-scripts\...              # 20 existing small script/design/checkpoint copies
  final-files-manifest.json             # handoff/backup verification
Z:\src\RedPrism\.build\P1Drafts\CURRENT-P1-VALIDATION.md
Z:\src\RedPrism\.build\P1Drafts\RESUME-P1.md
```

The same handoff is committed as `Specs/TestRuns/P1SessionCheckpoint_2026-10-21.md` in the current source
checkout and linked from the parent/index. All existing large evidence/product archives remain in place;
this save neither deletes nor relocates them. Existing archive manifests/readers remain the proof anchors.
The detailed log contains historical active-freeze messages; this latest handoff supersedes them.

For DxUi recovery, first inspect the current checkout and user work. The preserved base is exact and the
six copied files are complete; after verifying their manifest, they can restore the experiment at that base.
The supplemental patch alone omits untracked files. Do not overwrite later changes without reconciling them.
No P1-owned build/test/observer process is left running at the saved checkpoint; audit confirms the local query.
No automation or new task was created to resume work without the user.
