# DxUi integration

Status: current normative consumer contract
Last reviewed: 2026-09-20
Owner: `restore-dxui.ps1`, `Dependencies/DxUi.lock.json`, `RedPrism/ChromeHost.*`, `build.ps1` provenance

## Scope

This contract owns how RedPrism consumes the standalone DxUi library: the exact source pin, restore/build isolation,
the single `DxUi.lib` archive linked into `RedPrism.exe`, and the EmbeddedHost overlay on the application swap chain.
Library behavior remains in DxUi's own contracts. Splitter, ColorPicker, and NumericStepper are library work owned by
`Z:\src\DxUi\Specs\Plans\WIP\EditorConsumerControls_2026-09-20.md`.

## Pin and restore

`Dependencies/DxUi.lock.json` identifies `https://github.com/RedSalamanders/DxUi`, commit
`f5c7fc7403e352425b8e82b7a4da1c042d942ede`, API revision 2, and lock target `["DxUi"]`. `build.ps1` runs
`restore-dxui.ps1` for the selected platform after vcpkg. Restore clones that exact commit under
`.build/dependencies/DxUi/source/<commit>` and isolates vcpkg/library outputs under a fingerprint. It never checks
out, resets, or edits a sibling `Z:\src\DxUi` working tree. A mismatched or dirty pin fails restore.

`RedPrism` imports `Build/RedPrism.DxUi.props` / `.targets`, which reference `src/DxUi.vcxproj` once. Project
references keep Configuration/Platform; they do not enumerate library `.cpp` files. Debug and ASan Debug use `/MDd`,
Release `/MD`, matching DxUi. There is no `DxUi.dll` to stage.

Builds produce `DxUi.provenance.json` beside `RedPrism.exe`: exact source/API, public-header tree, archive hash, build
identity, and the `RedPrism.exe` module hash. `test.ps1` rejects wrong pins, profiles, missing/duplicate modules, and
replaced binaries. The sidecar is product evidence, not a separate release system.

## Host boundary

The application owns the HWND, D3D11 device, swap chain, and Present. `DxUi::GraphicsDevice::Create` borrows that
device. `EmbeddedHost` owns no HWND, swap chain, worker, or periodic timer.

`ChromeHost` attaches one EmbeddedHost, applies `MakeRedPrismThemePalette()` (local adapter, not a DxUi type), and
places a primary Button labeled `Hello DxUi`. Pointer, key, and character messages dispatch into the host. Preparation
and composition run on the idle Present path into the same back buffer as the clear color. Hidden or zero-extent
EmbeddedHost holds no cached surface (0 bytes). Visible surface stays within the library 64 MiB cap.

DxUi C++ stays inside `RedPrism.exe`. Never pass DxUi types, STL, WIL, or exceptions across a future plugin ABI.

## C7 fallback until library promotion

`EditorConsumerControls_2026-09-20.md` stays HOLD while LocalizedAdaptiveLayout owns the same DxUi control
files. C2 evidence (hello overlay + catalog gaps for Splitter, ColorPicker, and NumericStepper) is attached
there. Until the pin moves, chrome layout debt is in [`UI_ApplicationChrome.md`](../UI/UI_ApplicationChrome.md):
app-owned DIP split rectangles, a ColorSwatch-launched dialog from existing controls, and TextField numeric
nudge. Do not implement those three controls in RedPrism.

## Screenshot harness

`--screenshot <png>` shows the titled window with `SW_SHOWNOACTIVATE`, presents chrome, waits 300 ms on a one-shot
timer, captures the owned window through `Common/WindowCapture.cpp` (Windows.Graphics.Capture, no cursor, no
activation), and exits 0 when the PNG is written or 8 on capture failure. Computer Use and desktop screenshot tools
are not evidence.

## Validation

```powershell
.\test.ps1 -Platform x64 -Configuration Debug
.\test.ps1 -Platform x64 -Configuration Release
.\test.ps1 -Platform x64 -Configuration "ASan Debug"
```

`--self-test --warp` MUST prove a visible chrome surface with at least one composite and a hidden surface of 0 bytes.
`Tests/BuildProcessTests/DxUiProvenanceTests.ps1` MUST reject a wrong pin.
