- Consolidated the eight shared skills (`direct3d11-rendering`, `localization`, `modern-cpp-windows`,
  `performance-resources`, `spec-workflow`, `wil-raii`, `win32-windowing`, and `yyjson`) into the organization
  `RedHome` repository, retaining RedPrism-specific rules in `AGENTS.md` and `.agents/skills/` and validating with
  `validate-skills.ps1` and `Tools/Test-Changelog.ps1` (documentation/tooling only; native qualification unchanged).
  Shared-skill discovery names `../RedHome/AGENTS.md`, the package directory, and the clone URL while preserving
  local product authority.
  Skill-package fixtures also recognize Windows error 1314 as unavailable symbolic-link privilege and report the
  documented capability skips; other I/O errors still fail.
