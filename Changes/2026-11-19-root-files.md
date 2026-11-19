- Converged the root files with every RedSalamanders repository (convergence CV-6-03, owner decision D18 and the
  B-12 default): `LICENSE.txt` names RedSalamander as every repository's does, and the new root
  `THIRD-PARTY-NOTICES.md` lists every component the package compiles in or ships. `package.ps1` stages it and the
  inherited Wonder Assembly MIT notice, which the package did not carry before, and `Build_Packaging.md` lists both
  and the RedMonitor client notice. The common `CONTRIBUTING.md`, pull request template and `.vsconfig` are added.
  Validated with `validate-skills.ps1` and `Tools/Test-Changelog.ps1`; native packaging runs in main qualification.
