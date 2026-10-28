# PowerShell tooling migration

Date: 2026-10-28. Status: complete. Priority: tooling dependency reduction.
Planned at: a1656702eddc1df8a6104f795981a18678fff477. Drift check: git diff a1656702eddc1df8a6104f795981a18678fff477 -- Build Tools Tests .github/workflows.

## Scope and ownership

RedPrism owns its supported commands, CI callers, validation identity and developer contract.
The cross-project change removes Python from routine first-party skill validation and formatter installation.
Historical evidence, RedPrism manual Clipper generators, native product code, dependency pins and upstream
Ghostty/Meson Python requirements remain outside this migration. DxUi provides a reference, not an implicit dependency.

## Checklist

- [x] Implement the repository's bounded PowerShell tooling change with explicit prerequisite and format contracts.
- [x] Add behavioral regression coverage, including malformed input and dependency-free execution.
- [x] Update owning Build/Testing specs, guidance, CI/scope manifests and changelog.
- [x] Run focused tooling and governed affected checks; record actual results and limits.
- [x] Fold the changelog and move this completed plan to Done after all required work passes.

## Verification and done criteria

Run Test-Changes.ps1 -Explain, the focused tooling regression scripts and affected checks for the changed surface.
Run Tools/Test-Changelog.ps1, Tools/Fold-Changelog.ps1 and Tools/Fold-Changelog.ps1 -Check.
PowerShell syntax, failing fixtures, actual repository validation and caller/dependency closure must pass.
RedSalamander also validates its tooling inventory and specification inventory in the governed sandbox.
No product/native runtime qualification is inferred from tooling tests. No desktop focus is needed.

## Stop conditions

Preserve concurrent work. Stop on unexpected generated native changes, evidence mutation, required unapproved
sandbox initialization or desktop interaction. Record an unavailable gate without manufacturing a pass.

## Executed evidence and limits

Skill validation passed 58 behavioral cases, including aggregate and actual-repository validation without Python discovery, multiline scalar text and Unicode code-point limits. Test-Changes.ps1 -Explain -Scopes Tooling was inspected. Test-Changes.ps1 -Scopes Tooling -Force passed: all independent validators/tooling and 25 scoped-testing cases passed; SELECTED_PASSED, repository NOT_EVALUATED. Full native/product/platform qualification was not run. Initial validator exit-state and obsolete interpreter-identity fixture failures were corrected and retained in .build/powershell-tooling/validation.log; the passing affected run is in affected-final.log.

The routine gate no longer installs PyYAML or discovers an interpreter. Manual Clipper generators and all historical evidence remain unchanged by this migration. Concurrent skill-audit edits were preserved. Work began on ej/powershell-tooling. Concurrent audit work subsequently switched the shared checkout to ej/agent-skill-remediation-20261028; this migration preserved that branch and the other work. This migration made no commit, merge or push and claims no product qualification.
