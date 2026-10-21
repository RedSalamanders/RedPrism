# Scoped testing and validation cost

Status: ACTIVE: implementation complete; owner authorized merge on 2026-10-21 with native/CI qualification still open.
Date: 2026-10-05
Owner: [current validation contract](../../Testing/Testing_Validation.md)

## Accepted request

All active native test sources and helpers use `Scope.Tests.Something.h/.cpp`; Scope identifies the component and Something the scenario, runner, fixture, or helper. Migrate project entries, includes, inventories, tooling and current documentation together. Immutable historical evidence and imported/external source are not active test files.

Everyday iteration selects affected scopes from committed changes since the merge base plus staged, unstaged, deleted, renamed and untracked paths. Report paths and reasons. Unknown executable inputs, shared infrastructure and dependency/build changes conservatively widen coverage. A focused pass is never a full repository verdict.

Reuse only successful local evidence for identical source/dependency closure, executable/runtime bytes, architecture, configuration, runner arguments and environment. Invalidate before execution; publish only after success and an unchanged post-run identity. Corrupt/missing evidence executes again. Explicit force runs, performance investigations and environment-dependent/manual acceptance remain available.

Before a PR push, full coverage is required across local and CI execution. Delegate only the identical scopes/configuration that the enabled PR workflow actually runs. A nightly gate is not a PR gate. A base/merge-tree change, configuration difference or modified runner invalidates equivalence. Keep main/release acceptance distinct from PR feedback.

## Original review evidence (2026-10-05, historical)

- DxUi: test.ps1 supports suite and named-test filters, but unconditionally runs tooling and a benchmark; CI runs six native profiles plus a separate validation job and paired benchmark. The reviewed latest main validation passed; an earlier main ASan menu fixture failed even though its PR was green.
- RedXe: test.ps1 is an unconditional sequence of standalone suites, tooling, host WARP and crash checks. PR CI runs all tests on x64 Release; main runs all six profiles. Latest main CI passed.
- RedSalamander: governed Affected, ExplainPlan and exact Resume already exist. Reuse these contracts. PR CI runs Suite PR on x64/ARM64 Debug; long in-product CI suites run nightly and sanitizer suites weekly. Latest CI failed in format, migration and native jobs.
- RedPrism: test.ps1 orchestrates document, IO, host, composition and in-product checks on the current UI branch. No checked-in or enabled GitHub workflow was found. Plan only; do not rename files, edit runners, build, or change CI during this work.

## Original implementation sequence (superseded by resumed checklist)

- [ ] Inventory active source files, test scopes, dependencies, desktop/network requirements and CI coverage.
- [ ] Rename active native tests/helpers and reconcile all current callers; enforce naming.
- [ ] Provide explainable affected/default iteration and explicit full/pre-push entrypoints.
- [ ] Prove conservative fallback, rename/delete/untracked discovery, dependency fan-out and invalid selectors.
- [ ] Prove unchanged reuse, source/binary/configuration/environment invalidation, failures and post-run mutations.
- [ ] Align CI commands and pre-push delegation without weakening existing main/release gates.
- [ ] Run focused tooling/scenario checks, applicable builds and noninteractive native tests; record unavailable/manual gates separately.
- [ ] Persist durable requirements in the owning contract and agent guidance; close only after required qualification.

## Qualification constraints

No test may seize the person's foreground or pointer without agreement to the time. Use existing harness desktop leases. Native ARM64 claims require ARM64 execution; cross-build evidence is separate. No consumer pin changes are implied by library changes. Retain baseline/candidate fixture and source provenance for resource comparisons.

## Original preparation snapshot (2026-10-05, historical)

This paragraph records the original deferred preparation, not the current implementation status. The user resumed implementation on 2026-10-21; the execution checklist below and the current domain contract supersede this snapshot. At preparation time, only this plan and its WIP index were changed. Re-inventory after the current UI/worker work is finished: that branch is still adding tests and contracts, so today's list must not become a frozen migration target.

The current `test.ps1` builds first and then always runs failure-report routing, four native executables, the hidden WARP application self-test, crash/command-line probes, kernel and UI benchmark smoke, build/provenance/tool-discovery tests, and screenshot validation. ASan additionally runs document/worker stress and the deliberate sanitizer detection probe. No checked-in or enabled GitHub Actions workflow was found during the 2026-10-05 review. Consequently no future PrePush scope can be delegated until a real PR workflow is implemented and reviewed.

| Proposed scope | Existing executable or entrypoint | Inputs and required integration fan-out |
| --- | --- | --- |
| Host | `HostTests.exe`, `Tests/HostTests` | Jobs, executor/worker/wait pools, continuation and preview queues, memory admission, pointer streams; include Runtime for owner/lifetime integration. |
| Document | `DocumentTests.exe`, `Tests/DocumentTests` | Document/history, rasters, selection, transform, masks, brush and filters; include Composition and Runtime when publication or rendering semantics change. |
| IO | `IoTests.exe`, `Tests/IoTests` | ProjectStore, codecs, capped streams, clipboard transfer/import/publication; include Runtime for application routes and cancellation. COM, installed codecs and process-global clipboard effects require explicit capability accounting. |
| Composition | `CompositionTests.exe`, `Tests/CompositionTests` | Canvas composition/viewport, shaders, brush coverage, upload queues and WARP readback; include Runtime for device and presentation integration. |
| Runtime | `RedPrism.exe --self-test --warp` and crash/CLI probes | Application, renderer, chrome and owner integration. Preserve hidden, synthetic-input behavior. Initially retain the complete application self-test; split only after adding a source-derived registry and proving the union equals the existing catalog. |
| Tooling | `Tests/BuildProcessTests`, validation commands | Build/preflight, failure reports, dependency/provenance, toolset discovery, runner contracts and specification policy. Shared test support, project imports, resources, dependency locks and unknown inputs widen conservatively. |
| Stress | Existing `--stress` and `--asan-probe` paths | Separate exact sanitizer profiles and arguments; deliberate probe failure is required evidence, not reusable successful stress coverage. |
| Performance and captures | Existing benchmark and `--screenshot` commands | Preserve explicit measurement/capture operations and provenance. Functional success cannot substitute for a paired resource comparison or G1 acceptance. |

Proposed iteration commands are `Test-Changes.ps1 -Explain`, its affected default, and explicit `-Mode Full` / `-Mode PrePush`. These commands do not exist yet. Full remains local while CI is absent. Cache only exact successful source/dependency, artifact, profile, runner-argument and environment identities, with post-run mutation checks and explicit Force. IO/clipboard and measurements should initially stay non-cacheable until their complete external-resource contract is modeled. Do not reuse build receipts from another configuration or bypass the running-executable guard.

Rename native sources and support headers together with project entries, filters, includes, source inventories, scripts and current spec anchors. Example targets are `Document.Tests.Selection.cpp`, `Host.Tests.PointerInputStream.cpp`, `IO.Tests.ClipboardAvailability.cpp`, `Composition.Tests.Runner.cpp`, `Application.Tests.SelectionNudge.cpp` and `Selection.Tests.CommandGate.h`. Keep executable names, test IDs, numeric goldens and public APIs stable. Exclude immutable `Specs/TestRuns`, Done history, dependency trees and `legacy`; preserve historical names with explicit current-path mappings where tooling resolves them.

Before implementation, read the then-current Testing_Validation, Testing_Benchmarks, Core_PerformanceAndResources and domain contracts, and the spec-workflow, build-redprism and test-harness skills. The migration must preserve the current P1/S3/G1 and native-activation boundaries; a naming or runner change cannot close those projects.

Later acceptance must prove committed/staged/working/deleted/renamed/untracked discovery, safe unknown-input fallback, shared-header fan-out, invalid selectors, precise scenario unions, unchanged success reuse, source/DLL/argument/environment invalidation, stale SkipBuild refusal, and failure/interruption/post-run-mutation refusal. Then run the required x64 Debug/Release/ASan gates and three ARM64 cross-builds against the final frozen inputs. Native ARM64 runtime, codec/clipboard capabilities, and paired performance remain separately labeled. Introduce PR/main/release CI only in that later implementation; do not claim hypothetical CI coverage now.

## Implementation resumed 2026-10-21

- [x] Re-inventory current default-branch sources and live GitHub workflows; preserve unrelated primary/continuation checkouts.
- [x] Migrate contextual active native names and inventories; preserve historical evidence and legacy source.
- [x] Implement explainable affected selection and exact local result/build attestation.
- [x] Add source/index baselines from successful default affected runs; explicit partial runs and failures do not advance them.
- [x] Retain complete explicit Full and pre-push coverage with CI_PENDING distinct from passed evidence.
- [x] Define one independent tooling job and six exact native PR profiles; no duplicate identical main-push matrix.
- [x] Complete focused tooling/format verification and review the final implementation diff.
- [x] Publish PR #8 for the owner-authorized merge with failed/pending qualification recorded below.
- [ ] Complete native/CI qualification, retain every failure, and move this plan to Done only after the remaining gates pass.

The current private-repository GitHub account rejects branch-ruleset access with its plan/visibility restriction. Do not change visibility or claim protected checks are enforced. The normal merge policy verifies all named candidate checks; the owner's explicit exception below permits this merge without closing qualification. Existing RedPrism manual ARM64 feasibility results are historical and do not qualify the new current-main candidate.

Selection, exact evidence, source/index mutation, runtime closure, Unicode rename discovery, native ownership and clean/dirty/concurrent CI delegation have executable fixtures. Native filenames and group IDs do not rewrite legacy golden expectations. Foreground/clipboard desktop operation is not used locally by this tooling work; hosted native runners own their desktop. Resource measurements are not rebaselined and no production performance claim is made.

## Initial CI findings and correction

[Run 39623304709](https://github.com/RedSalamanders/RedPrism/actions/runs/39623304709) retained the initial failures. Tooling lacked the existing pinned PyYAML package. All three x64 builds and the standalone native suites completed, then the existing default-client/DPI check failed in the hidden WARP application self-test. No assertion, golden or ISA control is relaxed. The correction installs validation requirements, separates build/test steps, and adds actual client/style/desktop diagnostics. [Run 39631307275](https://github.com/RedSalamanders/RedPrism/actions/runs/39631307275) then established that the hosted 1024x768 display cannot be enlarged; all profiles refused the unavailable display fixture before compilation. The final implementation sizes only initial hidden/capture harness windows independently of desktop tracking limits, preserves exact DPI/style/client assertions, and preflights an owned hidden HWND without changing display settings. The local independent probe passed at 144 DPI; full native qualification remains required.

[Run 39634425945](https://github.com/RedSalamanders/RedPrism/actions/runs/39634425945) passed the independent exact-size hidden-window preflight on all six 1024x768 hosted desktops and all 24 scoped regressions. Its formatting step exposed global LLVM 20 overriding the repository's Visual Studio LLVM 22 style. Formatter discovery now prefers the supported VS bundle, reports its version, and binds its binary hash into reusable tooling evidence.

## Owner-authorized merge and outstanding qualification

On 2026-10-21 the owner instructed "merge anyway" after GitHub reported that account payments/spending limits prevented the remaining checks from starting. This is a merge exception, not successful test evidence. [PR #8](https://github.com/RedSalamanders/RedPrism/pull/8) may merge with this plan kept active.

Implementation candidate `4cf777b` passed tooling and native-scope checks in [run 39637882755](https://github.com/RedSalamanders/RedPrism/actions/runs/39637882755). All 24 scoped regressions and formatting of 563 files passed. All three x64 Full native jobs failed; ARM64 jobs were still running when the merge exception was recorded. The Release job passed its build, standalone Host/Document/Composition/IO suites and exact hidden-window fixture (`dpi=96 client=1280x800 desktop=1024x768 hidden=1`), then failed the application keyboard self-test with "Dragging the Marquee did not select a whole-pixel rectangle." The failed assertion is retained; no golden or predicate is relaxed. Investigate and repair this failure, inspect each remaining native result, and qualify the final corrected inputs before closing this plan. Existing P1/S3/G1, physical-capability and paired-resource gates remain separate.

## Canvas fixture repair and qualification hold (2026-10-21)

- [x] Inspect all three failed x64 artifacts: each reports the same Marquee assertion after its build and standalone suites pass.
- [x] Inspect the three ARM64 outcomes: they were cancelled, not passed. Debug and ASan reached the hidden application self-test; no additional compiler/test failure is established by the cancelled logs.
- [x] Reproduce the geometry independently: the centered 1280x800 document in a 936x656 canvas maps the literal document press (40, 40) to (-132, -32), outside the input surface. Setting zoom to 1.0 alone does not establish fixture framing.
- [x] Share a harness-only actual-pixel origin-framing helper between the application fixtures and composition controls. Preserve product viewport/window/input behavior and every existing literal assertion and golden.
- [x] Run the centered negative control: exactly 21 new pointer-mapping assertions fail, with zero unexpected composition failures. The repaired helper passes all 25 new assertions, including 96/144/216-DPI canvas sizes and prior zoom/pan state.
- [x] Pass local canonical x64 Debug and Release builds, including all linked-image ISA scans and positive/negative controls. Debug Composition/Runtime and Release Document/Composition/Runtime pass; both hidden WARP application self-tests pass at the local 144-DPI fixture. The Debug run precedes the separate thumbnail-oracle edit below; it does not qualify that edit or an actual 96-DPI application run.
- [x] Preserve the initial local Release C4723 warning-as-error receipt. The existing concurrent thumbnail test now uses its independently required 23x15 dimensions as literal loop/divisor expectations, retaining its byte oracle and strict compiler warnings; the corrected Release Document suite passes.
- [x] Pass independent validation, all 24 scoped regressions and formatting; keep native inventory/project entries synchronized. Retain raw failed and successful logs outside the checkout.
- [ ] Publish and qualify the corrected final candidate through all eight named checks and a successful Scoped validation workflow. Never count skipped, cancelled, pending or billing-refused jobs as success.
- [ ] Complete eligible cleanup after the corrected candidate merges; retain active checkouts, uncommitted work and useful ignored evidence.

The long jobs made observable progress: a historical x64 Debug cold build took about 29 minutes, including dependencies and ISA controls; ARM64 Debug took about 43 minutes in run 39637882755 before testing. Fresh hosted jobs restore dependencies and compile the pinned DxUi library and application with two MSBuild workers. Cancellation by a newer revision loses this in-progress qualification; no stalled build was demonstrated.

PR #8 merged as `73fee0c` under the separately recorded owner exception above. Its documentation-only head `e3ddcb5` received no replacement qualification: [run 39644658262](https://github.com/RedSalamanders/RedPrism/actions/runs/39644658262) refused both independent jobs before any steps because account payments/spending limits blocked hosted execution, and skipped its native matrix. The fixture repair is a follow-up change subject to the normal successful-check merge policy. This plan remains ACTIVE; no ASan, native ARM64, full-profile, physical-capability or paired-resource acceptance is inferred from the local focused passes.
