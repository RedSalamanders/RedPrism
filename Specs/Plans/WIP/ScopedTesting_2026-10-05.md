# Scoped testing and validation cost

Status: ACTIVE: user resumed implementation on 2026-10-21; native/CI qualification remains open.
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
- [ ] Complete focused tooling/format verification and native/CI qualification; retain every failure.
- [ ] Review the final diff, publish/merge the PR after its exact candidate checks pass, and move this plan to Done.

The current private-repository GitHub account rejects branch-ruleset access with its plan/visibility restriction. Do not change visibility or claim protected checks are enforced. Explicitly verify all named candidate checks before merge. Existing RedPrism manual ARM64 feasibility results are historical and do not qualify the new current-main candidate.

Selection, exact evidence, source/index mutation, runtime closure, Unicode rename discovery, native ownership and clean/dirty/concurrent CI delegation have executable fixtures. Native filenames and group IDs do not rewrite legacy golden expectations. Foreground/clipboard desktop operation is not used locally by this tooling work; hosted native runners own their desktop. Resource measurements are not rebaselined and no production performance claim is made.
