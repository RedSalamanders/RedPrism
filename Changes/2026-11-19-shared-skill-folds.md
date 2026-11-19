- Retired four repository-local agent skills now folded into shared skills in `RedHome` (convergence CV-2-12):
  `build-redprism` into `native-build`, `test-harness` into `test-evidence`, `settings-store` into `app-settings`,
  and `crash-handling` into `crash-dumps`. Every rule and code line of the four is in the shared skills, with
  RedPrism's suites, commands, paths and product rules in each one's `references/repository-facts.md`. Claude Code
  (the `redhome` plugin), Codex, and Copilot list the shared skills from this repository. `AGENTS.md` points at
  them, and four active plans name the new skills. No build, test or product input changes.
