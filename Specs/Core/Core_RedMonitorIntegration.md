# RedMonitor diagnostics

RedMonitor's [consumer contract](https://github.com/RedSalamanders/RedMonitor/blob/main/Specs/Build/Build_Consumption.md#consumer-contract)
owns the rules every application that adopts its client follows: the pinned client files and their verification, the
provider and its message format, viewer discovery and launch, the `--redmonitor-probe` command, and the validation
gates. This file keeps only RedPrism's adapter facts.

## Adapter facts

- The client header and its MIT notice live under `ThirdParty/RedMonitor/`, recorded in
  [the lock](../../Dependencies/RedMonitor.lock.json). The root `Verify-RedMonitorClient.ps1` verifies them and
  `Update-RedMonitorClient.ps1 -Commit FULL_SHA` updates them with the lock.
- Messages carry the `RedPrism` label. `Common/MonitorDiagnostics.cpp` and `Common/MonitorDiagnostics.h` hold the
  `Client.h` include, the provider owner and the `--redmonitor-probe` handler.
- `Tests/BuildProcessTests/RedMonitorConsumption.Tests.ps1` holds the offline verification controls.
