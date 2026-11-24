# Changelog

Entries are generated from [Changes](Changes/README.md). Existing release notes remain in their original locations.

## Unreleased

- Moved the build profile out of the five project files into `Directory.Build.props`: the toolset (`v145`), the
  Windows SDK (`10.0.26100.0`), the Unicode character set, the debug-library choice, the runtime library (`/MDd`,
  `/MD`) and the optimization level (`/Od`, `/O2`), so a project file holds only what is its own (`RedHome`
  convergence CV-6-04 part 1, register C-11). `Build/NativeBuildPolicy.targets` stays equal to the copy in the
  pinned DxUi commit, which compiles the DxUi archive and is hashed into its build identity; it takes DxUi's newer
  text with the next DxUi pin.
  No compiler or linker input changes: an MSBuild evaluation of the five projects in the six profiles before and
  after shows the same properties read by the build targets and the same metadata on every compile, link,
  library, resource, manifest and build-event item.
- Include the full notices from the pinned DxUi source and verify their bytes in the portable ZIP.
- Use the pinned engine for application vcpkg checkouts, overlay generation and manifest arguments.
- Bind the root RedMonitor client commands to the producer-owned tooling hashes recorded in the existing lock.
- Validate RedPrism skills, guidance and plan indexes with the pinned shared CI engine.
- RedPrism now uses the pinned shared scoped-testing runner, planner and evidence modules. PrePush covers the
  six supported local build profiles, with interactive clipboard suites listed separately. Native tests publish
  fresh per-suite reports alongside their retained logs and preserve separate test and production attestations.
- Import the build console from the pinned shared CI engine (convergence CV-5-03). `Build/BuildConsole.psm1` is now the
  engine's module shim (`Templates/Project/Build/BuildConsole.psm1`): `build.ps1` and the tooling tests keep importing
  the same path and get the same four functions from `.ci-engine/Tools/Modules/Ci.BuildConsole.psm1`. The zero-warning
  gate also refuses a structured native tool exception dump despite a green MSBuild footer, as RedTilde's did. The
  import refuses with one message when that checkout is missing, at another commit or changed. Tooling-only; no
  compiler or linker setting changes. The build-entrypoint fixture copies the engine implementation.
- Give the DxUi consumer and HLSL composition skills explicit usage triggers for agent discovery.
- Moved the vcpkg baseline (`vcpkg.json` `builtin-baseline`) and tool pin (`vcpkg-tool.json` `commit`), one value in
  this repository's toolchain contract, from `9eec002d` to `aeb162b7` (microsoft/vcpkg, 2026-10-22), RedSalamander's
  baseline and pin, so every repository uses one vcpkg commit (RedHome CV-4-07, register C-06 and C-07). Between the
  two commits only `7zip` and `sqlite3` change, and this repository uses neither, so no dependency version changes;
  the bootstrap stays on vcpkg-tool 2026-09-26. The managed checkout's scripts and triplets come from the new
  commit, so the first restore may rebuild the dependencies. `Specs/Build/Build_Toolchain.md` names the new commit.
  Compiler-free checks pass; native qualification runs on `main` after merge.
- Gave `Specs/Build/Build_UnifiedCI.md` the skeleton every RedSalamanders repository now shares (convergence
  CV-6-03, B-08): the same headings in the same order, one common text above `## Project adapter` built from the
  paragraphs the six files already shared, five stale statements corrected to the engine's `Specs/CI.md`, and every
  repository-specific sentence moved unchanged into the fixed adapter subsections. Prose only; no CI
  behavior changed. Validated with the repository's specification validators and `Tools/Test-Changelog.ps1`.
- Gave the specifications the same shape as every RedSalamanders repository (convergence CV-6-03): the common
  `Specs/README.md` structure and WIP and Done index formats, a dated name for the retired
  `AdversarialReview_2026-10-21.md`, the `Measurements/` archive rule in `Testing_RunEvidence.md` (records under
  `Specs/TestRuns/` stay; new ones go to `Measurements/`), a README title and CI pointer, the common CODEOWNERS
  file, and a `Core_RedMonitorIntegration.md` that keeps only RedPrism's adapter facts and links to RedMonitor's
  consumer contract. No behavior, build input or test changed. Validated with `validate-skills.ps1` and
  `Tools/Test-Changelog.ps1`.
- Retired four repository-local agent skills now folded into shared skills in `RedHome` (convergence CV-2-12):
  `build-redprism` into `native-build`, `test-harness` into `test-evidence`, `settings-store` into `app-settings`,
  and `crash-handling` into `crash-dumps`. Every rule and code line of the four is in the shared skills, with
  RedPrism's suites, commands, paths and product rules in each one's `references/repository-facts.md`. Claude Code
  (the `redhome` plugin), Codex, and Copilot list the shared skills from this repository. `AGENTS.md` points at
  them, and four active plans name the new skills. No build, test or product input changes.
- Agent guidance now matches the other RedSalamanders repositories (`RedHome` convergence step CV-6-02). There is
  one identical `.github/copilot-instructions.md` pointer to `AGENTS.md`. `AGENTS.md` is titled "RedPrism development
  guidance", with the shared-skill pointer directly under `## Skills`. The drive-root, scoped-testing, Changes and
  Unified CI sections use the text every repository shares, and a new shared "Interactive desktop tests" section
  states the focus rule. RedPrism's own scoped-testing facts move into the validation section, which no longer
  reads as an unconditional full run: PrePush accounts for those commands. `AGENTS.md` and `consumer-dxui` name the
  sibling `../DxUi` checkout that `restore-dxui.ps1` uses, not `Z:\src\DxUi`. The common `.gitignore` block ignores
  `.claude/settings.local.json` and `.claude/scheduled_tasks.lock`, and a new `capabilities.json` records what
  RedPrism supports and what is pending, each entry restating the docs or the WIP index. Nothing compiles
  differently.
- Converged the root files with every RedSalamanders repository (convergence CV-6-03, owner decision D18 and the
  B-12 default): `LICENSE.txt` names RedSalamander as every repository's does, and the new root
  `THIRD-PARTY-NOTICES.md` lists every component the package compiles in or ships. `package.ps1` stages it and the
  inherited Wonder Assembly MIT notice, which the package did not carry before, and `Build_Packaging.md` lists both
  and the RedMonitor client notice. The common `CONTRIBUTING.md`, pull request template and `.vsconfig` are added.
  Validated with `validate-skills.ps1` and `Tools/Test-Changelog.ps1`; native packaging runs in main qualification.
- Added `legacy/**` to the `.ci/project.json` prose exclusions, after `Changes/**`, so the list matches the other
  five repositories (RedHome CV-6-07, register G-04). RedPrism has no `legacy/` folder today, so no current file
  changes disposition; a folder added later is prose, as everywhere else. Compiler-free checks pass.
- Moved the shared CI engine pin from `07bc6f1` to `f965c79`, engine `main` today, with `Update-ProjectPin.ps1`:
  `.ci/project.json` and the workflow callers name the same full commit. This is this repository's step of the
  `RedHome` convergence wave (CV-4-04, CV-4-05) that puts every repository on one engine commit. Between the two
  commits the engine adds local SDK build and binding guards, the shared changelog commands with their product shim
  templates (CV-5-01, CV-5-02) and the harness exclusions, and changes no workflow template. The pin also adds the
  agent harness paths `.codex/**`, `.github/agents/**`, `.github/copilot-instructions.md` to the prose exclusions,
  so editing them no longer changes native inputs (CV-6-01). Compiler-free checks pass; native qualification runs on
  `main` after merge.
- Named the workflows and the Dependabot configuration on their own lines in the common CODEOWNERS file, which
  every RedSalamanders repository shares (convergence CV-6-03, B-12); `/.github/` already owned both, so no owner
  changes. Prose only. Validated with `Tools/Test-Changelog.ps1`.
- Planned the code-convention rows of the RedHome convergence register (CV-6-08) that move RedPrism, one pull
  request per row after the 155-line reformat, in `Specs/Plans/WIP/CodeConventionsConvergence_2026-11-19.md`. Plan
  only; no code changes.
- Run the changelog commands from the pinned shared CI engine (convergence CV-5-02). `Tools/Test-Changelog.ps1`,
  `Fold-Changelog.ps1`, `Submit-ChangelogFold.ps1`, `Test-ChangelogWorkflow.ps1` and the new `Release-Changelog.ps1`
  are now copies of the engine's shims (`Templates/Project/Tools` at `f965c79`): the same names and parameters, running
  the engine implementation in `.ci-engine`, and refusing with one message when that checkout is missing, at another
  commit or changed. `Tools/Modules/Documentation/Changelog.psm1` is removed. The behavior suite now also proves these
  shims equal the pinned templates and that `Test-Changes.ps1 -Mode PrePush` checks the changelog first. Two engine
  changes reach this repository: two pending fragments with the same new text are refused by the fold, and a Git error
  while reading the base changelog fails `-Base` instead of skipping it. `SkillPackageIntegrity.Tests.ps1` gives its validator fixture
  its own manifest and `.ci-engine` clone; `Specs/Core/Core_Changes.md` now points to the engine contract. Tooling-only;
  native runtime coverage is unchanged.
- Restores vcpkg from complete clones again. vcpkg 2026-07-27 extracts each versioned port with `git read-tree` and
  `git checkout-index` in an environment that holds only `GIT_INDEX_FILE`; without `SystemRoot` that Git resolves no
  host, so a blob a `--filter=blob:none` clone left on the server failed the restore with "Could not resolve host:
  github.com". It started on 2026-11-18, when microsoft/vcpkg `master` (de36db0455) stopped carrying the
  `vcpkg-cmake` tree the baseline selects, and it stopped every fresh restore, ours and the pinned DxUi's.
  - `vcpkg-install.ps1` clones completely, fetches the pinned commit only when the default branch lacks it and never
    with `--depth`, and completes a partial checkout from an earlier version in place (unshallow, then
    `fetch --refetch --no-filter`; the filter setting goes only afterwards).
  - The pinned DxUi `f2e328a0` still makes the partial clone, and moving the pin is out of scope (D20), so
    `restore-dxui.ps1` first prepares `<output>\vcpkg-tool` with `Build/VcpkgToolCheckout.psm1`: a complete, clean
    checkout at the commit the pin's `vcpkg-tool.json` names, which that installer then reuses. The module is the same
    file in the other DxUi consumers; DxUi's own fix is DxUi pull request 267.
  - `Tests/BuildProcessTests/VcpkgToolCheckoutTests.ps1` (offline `file://` fixture) covers a fresh clone, a rerun,
    completing a partial shallow clone, extracting an older tree with no network, and refusing another repository
    or a directory that is not a checkout. `VcpkgToolsetTests.ps1` pins the installer's clone, fetch and completion
    order. The native restore and build are proven by the post-merge qualification.
- Adopted the shared 155-line `.clang-format` of DxUi, RedSalamander, and RedMonitor (SHA-256 `768439d2…` as
  committed) and reformatted every C and C++ source under the `formatRoots` of `.ci/project.json` with the pinned
  clang-format 22.1.3: 989 files, each identical to `main` once whitespace is removed, and no include moved.
  `format.ps1` is now the shared script: it takes clang-format from `Install-CiFormatter` in the pinned
  `.ci-engine` instead of the Visual Studio, `PATH`, or standalone LLVM lookup, covers the eight extensions CI
  formats, and keeps `-Check`. `Find-RedPrismClangFormat` is removed; tooling evidence identifies the formatter by
  the engine's `Policy/clang-format.json`. `Build_Toolchain.md` and the BoundedClipper README describe it.
  `Common/BoundedClipper/clipper.*` is formatted like the other sources, not regenerated: its generator output
  no longer matches the committed files, which carry hand-made `/Wall` repairs (MergeReview MR-24, question Q14),
  so regenerating would drop them. Its manifests stay as stale as before (MR-21, MR-24).
  This is step CV-6-05 of the `RedHome` convergence plan (register D-01, D-02, D-05, D-06).
- `.gitattributes` and `.gitignore` start with the common blocks that every RedSalamanders repository now
  shares, and `.editorconfig` is the shared file. The `.gitattributes` block stores text with LF and checks out
  C and C++ sources, PowerShell, and Visual Studio files with CRLF and Markdown, JSON, and YAML with LF; the
  repository's own lines follow it unchanged and still take precedence. No tracked file needs renormalizing:
  every file with CRLF or mixed line endings in the index is already `-text`, and the new
  `* text=auto` leaves `ThirdParty/Notices/InheritedCode.txt` byte for byte through a `-text` line.
  This is step CV-6-05 of the `RedHome` convergence plan (register D-03, D-04, and the `.editorconfig` part of B-12).
- Aligned agent guidance and dependency automation with the other RedSalamanders repositories (convergence step 1,
  planned in `RedHome`): `CLAUDE.md` is the same thin pointer to `AGENTS.md` in every repository,
  `.claude/settings.json` enables the shared `redhome` Claude Code skill plugin from the `RedHome` marketplace, and
  Dependabot now ignores `RedSalamanders/.github/*` so it stops opening one notice per shared-workflow caller; engine
  pins keep moving through `Update-ProjectPin.ps1` only. The active `AsyncDocumentSizing` plan cites the
  shared skills at their `RedHome` paths. No build, test or product input changes.
- Consolidated the eight shared skills (`direct3d11-rendering`, `localization`, `modern-cpp-windows`,
  `performance-resources`, `spec-workflow`, `wil-raii`, `win32-windowing`, and `yyjson`) into the organization
  `RedHome` repository, retaining RedPrism-specific rules in `AGENTS.md` and `.agents/skills/` and validating with
  `validate-skills.ps1` and `Tools/Test-Changelog.ps1` (documentation/tooling only; native qualification unchanged).
  Shared-skill discovery names `../RedHome/AGENTS.md`, the package directory, and the clone URL while preserving
  local product authority.
  Skill-package fixtures also recognize Windows error 1314 as unavailable symbolic-link privilege and report the
  documented capability skips; other I/O errors still fail.
- Hold the existing shared session admission around real-clipboard I/O and hidden WARP tests, including owned child drain, so sibling project tests cannot corrupt their clipboard fixtures; background suites and ordinary builds remain independent.
- Retain elapsed self-test stage and final cleanup observations so native timeout evidence distinguishes slow progress, body completion and shutdown without changing assertions, failure bounds or main gates.
- Make the 512-point sampler overflow test wait for strictly advancing acknowledgements and final retirement, preserving its exact prefix, source-capture, error and ownership checks without imposing one aggregate speed limit on asynchronous ASan work.
- Run explicit local SDK Runtime/Captures through the existing CLI, WARP and harness checks with original byte-verified inputs and separate local-only evidence; preserve ordinary local/main gates and drain owned screenshot processes without terminating them.
- Add an explicit local Release SDK application build through the shared broker, with separate leased outputs and actual linker-input proof. Existing vcpkg dependencies and ordinary profile outputs remain separate; the result grants no test, ISA, repository or production qualification.
- Serialize ASan solution project builds so MSVC's shared app-local sanitizer runtime copies cannot race and invalidate zero-warning builds. Keep per-project C++ compilation, sanitizer instrumentation and detection, Debug/Release concurrency and all test gates.
- Report the hidden WARP test's exact terminal clipboard acknowledgement failure after a five-second exit grace when its process remains alive, instead of waiting for the general watchdog. Keep the process and failure logs, all original assertions and ordinary failure bounds; this does not diagnose or repair the native clipboard failure.
- Give each owned MSBuild launch a private compiler metadata endpoint while preserving native arguments, output, exit codes and the existing qualification gates.
- Adopt bounded native lanes, queued candidate freshness checks and an optional
  trusted reporter for original native evidence. Preserve current local, platform,
  review and production requirements while hosted gate acceptance is completed.
- Retain byte-verified completed tooling and formatting evidence before later native
  execution, so a native failure does not force identical independent work to repeat.
  Preserve full input/tool/environment checks and separate native/platform acceptance.
- Bind local build and test reuse to the actual pinned CI engine inputs, refusing missing, modified or mismatched engine checkouts while preserving the existing local and main gates.
- Retain ordered rendering fixture, frame, readback and device-recovery markers in hidden self-test output so a native timeout identifies the last entered operation without changing assertions or failure bounds. The original native ARM64 hang remains unresolved until diagnosed on the matching hosted runner.
- Reuse verified pinned Release DxUi SDK inputs through the shared reader and
  actual MSBuild ABI guard, retaining source fallback, linker/byte provenance,
  exact package notices and all native regression, ISA and extraction gates.
  Isolate bounded correctness-smoke modifier state from typing in other apps,
  retaining real cancellation messages and full performance input guards.
- Add the optional isolated native shadow caller and exact shared-engine admission tooling while retaining current required checks, local coverage and production qualification.
- Enable authenticated qualified DxUi SDK reuse for eligible hosted Release builds, preserving the existing dependency pin and all native regression, detector and package checks.
- Added the indexed WIP handoff plan (`Specs/Plans/WIP/ExplicitStateMachines_2026-11-10.md`) cataloging internal explicit state-machine refactors and session-lifetime hardening across `Common/`, `RedPrism/`, and `Build/` with zero `.prism` schema or `DxUi` pin changes.
  Validation: `validate.ps1`, `Tools/Test-Changelog.ps1 -Base origin/main`, and `Tools/Invoke-ProjectCi.ps1 -Phase Static`.
- Adopt the shared guard and explicit owned-prose exclusions so new runtime Markdown and notice inputs retain native qualification; preserve existing native gates and historical artifact identity.
- Preserve the latest local failure during affected iteration: suspend old no-work state before execution, revoke it on failure/interruption and direct test attempts, and restore only unchanged successful explicit work. Force bypasses the iteration baseline, with a reentrant per-profile test lease and real process/failure refusal controls.
- Preserve the original native test or timeout failure when evidence finalization also fails, while retaining the additional diagnostic and all lossless verification requirements.
- Adopt the shared engine's ABI-indexed vcpkg fallback cache and manual native dependency caching; keep compiler-free PRs, exact dependency pins and every native qualification gate.
- Add an explicit x64 production-continuation option to the approved manual native workflow, verifying both preserved regression receipts and the exact qualified source while preserving ARM64 feasibility and extended qualification.
- The vcpkg toolset parity check against DxUi's pinned `Get-DxUiDefaultToolset` and
  `Update-DxUiVcpkgOverlayTriplet` now runs. `Tests/BuildProcessTests/VcpkgToolsetTests.ps1` finds the restored
  pin with `Get-DxUiSourcePath`, the 16-character source folder `restore-dxui.ps1` uses. Before, it looked under
  the 40-character commit and always skipped. With the source restored, `vcpkg-toolset.same-behavior-as-the-dxui-pin`
  passes against `a059e07c` (API 3) and `f2e328a0` (API 4), and `Build/VcpkgToolset.psm1` needs no change. A
  checkout that has not restored DxUi still skips the check.
- Run the rectangle and ellipse press self-test fixtures on the self-test's synthetic keyboard for their whole
  run, like the other standalone press fixtures, and hold the Shift and Alt scenarios' modifiers there, so a key
  pressed on the desktop during the hidden WARP self-test can no longer change the press mode at button-down.
  The native thread key state is saved and restored around the run; every assertion and expected value is
  unchanged.
- Run the original loaded-selection complexity and pixel-budget refusal tests in the existing isolated host
  with stable declared memory capacity. Add real low-capacity Load Layer and Load Mask refusal controls;
  preserve the tracer limit, exact notices, selection/history expectations and settlement deadlines.
- Move the DxUi pin from the API revision 3 maintenance commit `a059e07c` to DxUi `main` `f2e328a0`
  (API revision 4, the 2 November 2026 delivery with its consumer upgrade guide), which also brings the
  keyboard cancel of pending menu hover timers (DX-2). Device recovery now flushes the old context after
  releasing the swap chain, since the new embedded host no longer does it and the next swap chain was refused
  with `E_ACCESSDENIED`. A superseded chrome preparation (`E_ABORT`) draws again instead of counting as a
  failed frame; the chrome controls declare their copy and move operations deleted, tree rows write the new
  badge field, and the prepared layer rows build their accessibility view by member. The prepared layer-row
  accessibility fixture services the owner's messages until DxUi's notification thread lets go of the rows.
  `test.ps1` passes in x64 Debug, Release and ASan Debug; ARM64 and DX-2's product acceptance remain open.
  Restore requires revision 4; `a059e07c` with this adapter reverted is the rollback.
- Drain ready owner work before the Ctrl-pixel test fixture waits for another scheduler notification. Add a
  three-action control for consumed wake signals and keep final-display suspension consistent with the hidden
  window policy, preserving the original publication, cancellation, history and retirement assertions.
- Correct the WIP index and the 28–31 October review notes against `main` `e79a83a`: `package.ps1` already calls `Get-DxUiSourcePath`, including in the packaging summary and AR-13, and P1 with its child plans stays HOLD. Resolve CI-10 / Q16 and reconcile the Changes contract with pinned engine `19710a8a`: PR static checks validate the exact head against its supplied immutable base; main pushes coordinate native qualification. No product code changed. DX-2, the busy string, the admission ceilings, the format-7 writer, and the parity-test path skip remain open.
- Rename canvas rendering components, shaders, tests, tooling and diagnostics consistently; remove the obsolete reference tree from rewritten history and use the RedPrism project-format marker. Existing packages with a different marker require explicit conversion. Align the bootstrap checkout with the manifest baseline already selected on main so its declared dependency versions resolve.
- Use `.prism` for project filenames, open/save filters and defaults, classification, fixtures and documentation throughout rewritten history. Derive suffix length from the extension and cover missing, uppercase, mixed-case and misleading-suffix paths; retain the existing ZIP layout and `com.redprism.project` marker.
- Run the active-layer and composite Wand functional suites on the existing isolated owner with stable declared capacity. Preserve all original Boolean FIFO, pixel/history, reservation and settlement assertions; add twelve low-capacity refusal controls and detailed settlement diagnostics.
- Bind sizing's held-state check to its original worker ticket and exact reservation. A composed panel/size/Undo control retains a separate real reservation while sizing is held, then requires the unchanged exact final clipboard baseline after both retire.
- Isolate scheduler message-pump and capacity fixtures from ambient OS memory notifications when checking post-settlement idle. Preserve every ordering, cancellation and lifetime assertion, and add a deterministic late-memory-notification control requiring owner drain before idle.
- Preserve retryable device recovery when an owner stops an attached command source before recording begins. Return `E_ABORT` for that valid stopped producer while keeping invalid inputs strict, with controlled pre-start coverage and the original two-restart, one-capture and joined-retirement bounds.
- Run panel/Wand functional and lifetime fixtures on the existing isolated startup owner with a deterministic memory snapshot. Preserve all 126 host and 32 lifecycle assertions, and require six additional real-host controls proving that insufficient declared capacity refuses before capture with unchanged panel, selection, source and history.
- Establish normal native tracking bounds before the live Polygonal DPI fixture opens its drawing, preserving exact DPI, rectangle, original-owner, and restore assertions on small hosted desktops.
- Settle and verify each Clone input-policy fixture installation before synthetic pointer input, and scope its keyboard independently. Preserve all original literal source/offset/pixel/history assertions and retain detailed failure diagnostics.
- Wait for the delayed-clipboard test producer's actual message dispatch before its native early read. Add event-controlled startup checks and immediate read diagnostics while preserving all original clipboard, pixel, history, cancellation and retirement assertions.
- Use the existing 90-minute native ARM64 self-test observation bound in the shared CI adapter, retaining the x64 default, complete suites, failure propagation, assertion probes, and successful-evidence requirements.
- Record the 31 October red-team delivery plan: re-confirm the 21–28 October merge-review findings at `a7e81222`, add the leftover test-isolation and always-on benchmark gaps, and keep the WP0–WP6 work sequence. No product code changed; MergeReview questions and the DxUi pin action stay open.
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
