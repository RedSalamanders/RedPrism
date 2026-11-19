- Gave the specifications the same shape as every RedSalamanders repository (convergence CV-6-03): the common
  `Specs/README.md` structure and WIP and Done index formats, a dated name for the retired
  `AdversarialReview_2026-10-21.md`, the `Measurements/` archive rule in `Testing_RunEvidence.md` (records under
  `Specs/TestRuns/` stay; new ones go to `Measurements/`), a README title and CI pointer, the common CODEOWNERS
  file, and a `Core_RedMonitorIntegration.md` that keeps only RedPrism's adapter facts and links to RedMonitor's
  consumer contract. No behavior, build input or test changed. Validated with `validate-skills.ps1` and
  `Tools/Test-Changelog.ps1`.
