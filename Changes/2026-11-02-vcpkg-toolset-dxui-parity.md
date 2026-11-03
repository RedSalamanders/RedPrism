- The vcpkg toolset parity check against DxUi's pinned `Get-DxUiDefaultToolset` and
  `Update-DxUiVcpkgOverlayTriplet` now runs. `Tests/BuildProcessTests/VcpkgToolsetTests.ps1` finds the restored
  pin with `Get-DxUiSourcePath`, the 16-character source folder `restore-dxui.ps1` uses. Before, it looked under
  the 40-character commit and always skipped. With the source restored, `vcpkg-toolset.same-behavior-as-the-dxui-pin`
  passes against `a059e07c` (API 3) and `f2e328a0` (API 4), and `Build/VcpkgToolset.psm1` needs no change. A
  checkout that has not restored DxUi still skips the check.
