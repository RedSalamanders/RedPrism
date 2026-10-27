# Hosted MSVC setup followup

Status: ACTIVE validation and merge
Date: 2026-10-27

- [x] Confirm the retained MSVC 14.52 work is already integrated; preserve its original worktree.
- [x] Port explicit GUI process waiting and diagnostic retention without changing compiler or target requirements.
- [x] Add delayed-GUI success/failure and required-tool/refusal controls to the ordinary tooling entrypoint.
- [x] Pass affected tooling, source validation and changelog folding.
- [ ] Verify available hosted checks and merge the qualified followup; preserve any external CI restriction explicitly.

This change owns CI provisioning only. Existing native, performance, physical-input and release gates retain their owners. RedTilde's separate Release keyboard-helper repair and historical qualification evidence remain in its rendering completion plan.
Local validation: the complete independent validator and tooling runner passed, including 30 evaluated native profiles, six pinned archive profiles, compiler/linker negative controls and 12 delayed-GUI/refusal cases. Format Check covered all 1,357 owned native files. PowerShell syntax, diff and changelog fold/check passed. No native product source, dependency pin, compiler policy or performance limit changes in this followup. Hosted qualification remains separate; an account restriction cannot be reported as a passing CI run.
