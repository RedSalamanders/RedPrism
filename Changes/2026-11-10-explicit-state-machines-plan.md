- Added the indexed WIP handoff plan (`Specs/Plans/WIP/ExplicitStateMachines_2026-11-10.md`) cataloging internal explicit state-machine refactors and session-lifetime hardening across `Common/`, `RedPrism/`, and `Build/` with zero `.prism` schema or `DxUi` pin changes.
  Validation: `validate.ps1`, `Tools/Test-Changelog.ps1 -Base origin/main`, and `Tools/Invoke-ProjectCi.ps1 -Phase Static`.
