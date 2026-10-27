# RedPrism RedMonitor adoption

Status: ACTIVE qualification. The canonical detailed execution plan is
[RedMonitor extraction and adoption](https://github.com/RedSalamanders/RedMonitor/blob/main/Specs/Plans/WIP/RedMonitorExtraction_2026-10-21.md).
The current contract is [RedMonitor integration](../../Core/Core_RedMonitorIntegration.md).

- [x] Inspect current main and preserve diagnostics, crash and provider ownership.
- [x] Implement the bounded labelled native adapter and explicit installed-helper command.
- [x] Replace managed viewer builds/staging with one vendored client header, MIT notice and immutable lock.
- [x] Add local-only verification and explicit two-file update tooling.
- [x] Update contracts, usage and application-only packaging with the client MIT notice.
- [x] Run offline client controls and repository validators/format checks on the revised boundary.
- [x] Build all six profiles and run required x64 Debug/Release/ASan suites, including actual ASan detection.
- [x] Verify the actual app ETW Unicode/severity/PID/TID probe, missing-helper behavior and clean app-only package.
- [x] Retain source-bound receipts, fold changes, commit and push this branch.
- [ ] Close native ARM64 runtime, physical input/accessibility, controlled performance and release gates.

Earlier 2026-10-23 managed-viewer checkpoints remain historical evidence in Measurements and .build.
They do not qualify this revised dependency boundary. Tests never require an installed helper.
No ordinary build invokes the dependency updater. The viewer owns its separate Release packaging.

All six normal builds and all three x64 Full profiles passed. ASan included the deliberate detection probe and
Stress scope. Actual application ETW and absent-helper commands passed in all three profiles; final executable
hashes still match those observations. The portable ZIP passed clean-extraction help/WARP/version checks,
contains the exact SDK MIT bytes and carries no viewer payload. Reviewed identities and limitations are in
Measurements/RedMonitor_Adoption_2026-10-23. Native ARM64 execution, physical input/IME/AT, paired performance
and release handoff remain separate gates; no cross-build or smoke benchmark closes them.

## Main integration - 2026-10-27

The approved integration combines current main with the retained MSVC 14.52 work and the standalone header-only RedMonitor client. Qualification of this combined source is recorded separately from earlier branch receipts. Historical passes and remaining native, interactive, performance and release gates are retained; the merge does not close them automatically.
