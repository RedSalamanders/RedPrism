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
