# Changelog

Entries are generated from [Changes](Changes/README.md). Existing release notes remain in their original locations.

## Unreleased

- Adopt shared CI v1.0.6: DxUi SDK ZIPs stay internal with no publication workflow; application publication remains manual and artifact-only. Document the organization token route and separate implementation closeout from source-bound hosted acceptance; no product or winget publication is performed.
- Pin the common CI engine to v1.0.5: verify native portable winget registration, every installed command alias, declared smoke probes and owned cleanup; retain the compiler-free custom dependency status observer and recorded owner-only emergency PR merge route. Publication remains an explicit action requiring both matching native Release regression and production ZIP receipts.
- Retain stdout/stderr and real exit codes for windowless hosted native commands through the common pinned CI engine v1.0.4.
- Introduce the pinned organization CI adapter: compiler-free PR feedback, quiet-window main admission, native Release test-to-production gates, ZIP qualification and explicit publication. Preserve canonical project test, dependency, toolchain and evidence contracts; hosted rollout acceptance remains in progress. Production ZIPs now require test-disabled build evidence and prove test-entry refusal plus hidden startup capture.
- Extend the merge-review plan to PRs #19–#22 and re-anchor every row at `b6ec336`. PR #19 changed no product
  behavior. The pass adds:
  - **CI-8**: `package.ps1` still reads DxUi's license from the old 40-character source folder, so packaging
    fails.
  - **CI-9**: with `/WX` off, a second incremental build passes the zero-warning gate.
  - **CI-10**: `Core_Changes.md` describes push checks that `ci.yml` never runs.
  - **MR-24**: hand-edited generated BoundedClipper sources whose manifest no longer matches.
  - **MR-25**: the checkerboard fixture's unexplained Release failure matches MR-03's memory admission.
  - **AR-12**: replace 767 scoped implicitly-deleted suppressions (about 3,900 lines) with a policy entry.
  - **AR-13**: tooling consolidation.
  - **Q13–Q16**: questions on these items.

  It closes CI-2 and CI-3 and corrects four first-pass counts. Review only: nothing was built or run.
- Document the explicit organization-owner, PR-only emergency merge route with a recorded reason and exact-head match; preserve actual checks and native publication qualification.
- Fold changelog fragments on `main` after merge, identically in DxUi, RedSalamander, RedXe, RedPrism, RedTilde and RedMonitor. A change keeps its fragment and never edits `CHANGELOG.md`, so parallel pull requests no longer conflict on it; `Tools/Submit-ChangelogFold.ps1` folds the default branch in a temporary worktree and opens a fold pull request without touching the caller's checkout. `Test-Changes.ps1 -Mode PrePush` (`-Base` the base ref, `origin/main` by default) and the CI tooling job (`HEAD^1` for a pull request) refuse any `CHANGELOG.md` change that is not an exact fold of fragments already on `main`, while `test.ps1` validates fragments without requiring them to be folded. The fold also flushes its staged changelog, reads it as strict UTF-8, limits reparse checks to the repository and ignores its lock and stage files in Git. Validation: the 27 shared changelog cases pass through `Tests/BuildProcessTests/ChangelogWorkflow.Tests.ps1`, including this repository's PrePush runner and a fold submission to a local bare remote; `Tools/Test-Changelog.ps1` (with and without `-Base origin/main`), `validate-skills.ps1`, the 60 skill-package integrity controls and the 26 scoped-testing cases pass, the last with the reviewed CI workflow digest in `Tests/test-scopes.json` refreshed for the new step. The first real fold pull request, the hosted CI step and native qualification remain unrun.
- Publish the production-readiness review of the 2026-10-21..2026-10-28 merges (`0ce2070..85eaef7`, PRs #4–#18) as a
  DECISION plan with WIP index routing: verified High/Medium/Low production rows, latent P1 rows, CI/tooling rows,
  simplification items AR-1–AR-11, open questions Q1–Q12 and work packages WP0–WP6. Documentation only; no product
  code changed, nothing was built or run for the review, and every fix waits on the recorded decisions.
- Record the validated build-policy matrix, preserved binary-size evidence, completed quiet performance study and explicit merge handoff; retain unresolved performance uncertainty and any hosted CI limits.
- Record passing local x64 runtime qualification and ARM64 cross-builds for the common build policy, with immutable Release size evidence and remaining performance/CI gates explicit.
- Correct the utility routing fixture's reversed memory-source fields; prove that all 128 original admissions use its injected sampler and retain their synthetic charges, while preserving the explicit memory-notification and one-drain idle assertions. Native and full runtime requalification remain required.
- Record the current native profile and pinned-archive policy audit, qualified native builds and explicit remaining runtime, performance and integration gates.
- Align build guidance and packaged DxUi license provenance with the verified 16-character source folder convention.
- Preserve per-stage acknowledgement, error counts/ids, selection state and canvas facts for the existing checkerboard refusal fixture, with its scoped Wand/Layer worker trace. Keep all original assertions and limits while retaining the failed Release image and logs for diagnosis.
- Confine the ARM64 sanitizer probe's intentional volatile use-after-free observation to a definition-local C4746 scope. Keep the allocation, release and faulting read unchanged so real sanitizer instrumentation remains required.
- Include the shared capacity test helper through the existing repository include root, keeping each test's include path explicit without parent-directory warning C4464.
- Annotate SDK WRL's ARM64 volatile reference-count definitions before the UI Automation fixture includes application headers. Keep the scope on the SDK include, retain owned-code diagnostics and leave COM ownership and the target volatile model unchanged.
- Clarify build and WIL guidance: `/WX` remains disabled while the build gate requires zero warnings, with narrowly scoped external include annotations.
- Keep intentional worker page touches and runtime capacity observations under narrow ARM64 C4746 diagnostic scopes, preserving the volatile model, capacity assertions and synchronization behavior. Retain the failed clean-build evidence and require a fresh native matrix.
- Occupy the private raster storage base's ARM64 tail padding to address MSVC C5291 without suppressing the diagnostic. Preserve x64 layout and charged lifetime accounting, check the derived size at compile time, and verify charged pixels after source and caller lifetime release.
- Advance the API3 maintenance lock to the test-only leased-foreground followup after six upstream builds and all three zero-skip desktop profiles pass. Preserve matching public headers/library and requalify the consumer through normal restore and build entrypoints.
- Restore the previous CRT allocation hook with a documented statement-local annotation for the CRT-erased noexcept type; preserve the allocation-failure test behavior.
- Select the API3 DxUi maintenance candidate with stable posted-payload type identity under Release ICF and warning repairs. Preserve the experimental prepared-row boundary and historical qualification receipts; track fresh consumer and desktop-focus gates in the indexed native-policy plan.
- Exercise the real build entrypoint with warnings, errors and missing diagnostic logs even when the native command returns zero; ensure those cases fail before provenance and preserve the caller environment. Use the common whole-entrypoint timer for the final result.
- Make owned raster, geometry, host and test conversions explicit for the zero-warning /Wall policy; preserve image arithmetic and constructor/capture lifetimes, spell out enum fallbacks and array subobjects, and remove unused private test helpers. Full native and runtime qualification remains tracked in the build-policy plan.
- Unify native builds on /Wall without /WX and require zero diagnostics before accepting success; align Debug /ZI and ASan /Zi, enable Release-only /OPT:ICF, print green artifact/log links with quoted launch commands, and shorten consumer DxUi source/output folders while preserving complete collision-checked identities. Native qualification remains tracked in the active build-policy plan.
- Limit recognized Markdown fence openers and closers to zero to three leading spaces, preserving required-resource and unfinished-instruction checks across four-space and tab-indented markers; add boundary controls.
- Keep line-leading same-line backtick spans as inline prose so they cannot hide following required resources or unfinished instructions; retain tilde-fence info handling and cover both cases with read-only controls.
- Recognize only complete unquoted date/timestamp metadata scalars so date-prefixed guidance stays valid prose; retain quoted strings and typed temporal rejection controls.
- Validate hidden skill packages and preserve quoted ISO metadata strings; keep inline-code spans within paragraphs and validate continued Markdown references and parenthesized titles, with focused read-only controls including literal file-URI filenames.
- Check package paths before metadata reads, validate local drive/file-URI references, preserve balanced links and inline examples, recognize literal aliases and ShouldProcess parameters without executing scripts, and scope only the two reviewed skill-validator modules to Tooling while retaining broad shared-build fallback.
- Run routine skill validation and behavioral fixtures on PowerShell 7 without Python/PyYAML; retain explicit metadata/instruction checks while keeping manual Clipper source-generation prerequisites separate.
- Preserve the deliberate crash-formatting exception, route ported guards through current pending-edit rules and keep dependency history in the integration contract; add independent skill-reference and literal-command-signature controls without executing examples or native builds.
- Bind hosted build and restore to the verified MSVC 14.52 installation's MSBuild, preserving strict compiler-family checks and local read-only behavior; cover stale ambient discovery and missing-MSBuild refusal.
- Wait for the hosted Visual Studio GUI installer and use its explicit result before verifying MSVC 14.52; retain installer diagnostics and cover delayed completion, failures and strict tool/refusal checks without a real installation.
- Integrate the standalone RedMonitor pinned header client with MSVC 14.52 Release optimization and current main, preserving existing tests, source-bound qualification evidence and separate native/manual/release gates. Correct independent scoped command argument forwarding and test strict zero-argument and named-switch scripts.
- Report the WAND fixture's job-settlement timeout, quit or failure on redirected stderr while preserving the existing ten-second bound and exit code, so a failed hidden WARP run retains an actionable reason.
- Prepare and verify the required MSVC 14.52 toolchain in active Windows CI, with read-only local checks and refusal controls; hosted installation and native ARM64 runtime results remain separate qualification gates.
- Require MSVC 14.52.x and enable Release function-level/whole-program compilation and unused-function elimination with full LTCG; forward exact tools and WPO into the adopted DxUi archive with a separate policy fingerprint. Validation and measured before/after evidence remain tracked in the active build-policy plan.

- Point RedMonitor contract and adoption-plan links to its merged main branch so retiring the delivery branch preserves documentation navigation.
- Share only the pinned RedMonitor client header and MIT notice, verify them offline during builds, and discover the separately installed production Release helper for explicit launch or optional bare Debug startup; keep normalized bounded diagnostics and application-only packages.
- Added explicit optional pinned RedMonitor portable distribution with verified runtime/resource/license closure and behavioral rejection controls.
- Add the exact pinned RedMonitor producer SDK, bounded application-labelled diagnostics and explicit optional adjacent viewer command; preserve existing logs, crash handling and application ownership, with consumer qualification tracked separately.
- Verify retained run inventories using fresh pinned-handle metadata and stable path/stream ordering; preserve before/after move diagnostics and exact safety checks, with stale-metadata and reversed-enumeration regressions.
- Retain the selected suites and requested self-test timeout in RedPrism run metadata when using the current scoped runner; preserve the original failure bound and source/build checks.
- Preserve completed, failed and interrupted test-run payloads in unique verified evidence folders, retain prior logs and crash probes, and collect hidden evidence in CI without modifying shared read-only files or weakening scoped qualification checks.
- Publish the historical paused-branch adversarial review, with WIP index routing and explicit snapshot/validation limits. P1 integration and current-head review triage remain open.
- Record the paused P1 main consolidation: stable x64 Release build, linked-image ISA gates and selected Host/Document/IO/Composition/Runtime suites pass.
  Preserve failed attempts and satellite alternative source in history. Pure ISA parser controls now run once in independent tooling;
  native image/compiler controls retain profile ownership. Full/platform/resource gates and unfinished P1 activation remain on HOLD.
- Consolidate paused P1 source with current main while keeping P1 HOLD, ordinary activation and all unfinished acceptance gates open.
  Preserve satellite histories and exact divergent-source inventory; reconcile hosted viewport fixtures, contextual native test names,
  build attestations, archive tooling and independent tooling ownership. Current integration checks retain all scanner/runtime failures;
  this merge is source preservation and does not certify P1 or reuse historical qualification for changed code.
- Record the owner's authorization to publish locally verified qualification-archive tooling while hosted checks are billing-blocked; preserve unavailable CI results and all separate native/platform qualification gates.
- Archive explicitly frozen qualification inputs through verified, read-only shared payload storage while keeping run records and logs independent; retain fail-closed path, metadata, locking, corruption and readback checks, exercise filesystem scenarios through existing tooling discovery, and preserve all native/platform qualification gates.
- Record the owner's authorization to merge the locally tested canvas-fixture repair while hosted CI is billing-blocked; retain successful local evidence, failed CI results and the active plan's remaining qualification gates.
- Keep the concurrent thumbnail test's independent 23x15 pixel oracle explicit in loop bounds and divisors so newer MSVC Release optimization can prove the divisors are nonzero without suppressing warnings; the focused Release Document suite passes.
- Frame synthetic actual-pixel canvas fixtures at the document origin so their literal drag points reach the canvas at 96 DPI as well as larger DPI scales; retain all selection/brush/pixel assertions and add independent viewport regressions. Native CI qualification remains required.
- Prefer the discovered Visual Studio formatter bundle over unrelated PATH LLVM tools, report its version, and include formatter binary bytes in tooling evidence identity.
- Name active native tests by context, run explainable affected scopes from the last successful iteration, reuse exact successful evidence, and qualify full profiles through one PR matrix with explicit pending and manual gates.
- Keep hidden/capture harness canvas sizes exact on small CI desktops, verify an independent owned hidden HWND without changing display settings, report actual DPI/client dimensions, and install pinned validation dependencies in CI.
- Ignore the legacy root-level journal-release.exe polygonal-input-journal probe; retain its local bytes and keep generated binaries out of source control.
- Require explicit user authorization for each exact project-specific `.Perf` root, keep other generated output inside approved workspaces, and preserve evidence and live references during artifact cleanup.
- Resolve explicit relative Changes roots against the current PowerShell location; regression coverage verifies validation, preview and folding leave a different process-directory checkout untouched.
- Keep new entries under Unreleased when a released heading uses a tab separator, and terminate an owned timed-out tooling fixture child tree before cleanup. Add independent released-heading and process-timeout regressions, and preserve the concurrent top-level drive-root guidance layout.
- Standardize the Changes workflow across DxUi, RedSalamander, RedXe, RedPrism and RedTilde with shared validation, safe folding and behavioral fixtures. Fold before PrePush/final handoff and immediately after integration, even for one fragment; retain native and product qualification as separate gates.
