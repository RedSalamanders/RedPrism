# DxUi integration

Status: current normative consumer contract
Last reviewed: 2026-09-21
Owner: `restore-dxui.ps1`, `Dependencies/DxUi.lock.json`, `RedPrism/ChromeHost.*`, `build.ps1` provenance

## Scope

This contract owns how RedPrism consumes the standalone DxUi library: the exact source pin, restore/build isolation,
the single `DxUi.lib` archive linked into `RedPrism.exe`, and the EmbeddedHost overlay on the application swap chain.
Library behavior remains in DxUi's own contracts. Splitter, ColorPicker, and NumericStepper are library controls
(DxUi `Specs/Plans/Done/EditorConsumerControls_2026-09-20.md`) consumed as-is by `ChromeHost`.

## Pin and restore

`Dependencies/DxUi.lock.json` identifies `https://github.com/RedSalamanders/DxUi`, commit
`c403052bad26c97ba23995e136672c32c85ccbfc`, API revision 2, and lock target `["DxUi"]`. That commit is the merge of
[DxUi pull request 19](https://github.com/RedSalamanders/DxUi/pull/19) on `main` (tree row drag reports one drop; splitter
drag survives sibling layout). `build.ps1` runs
`restore-dxui.ps1` for the selected platform after vcpkg. Restore clones that exact commit under
`.build/dependencies/DxUi/source/<commit>` (from the sibling checkout when it already has the commit, else from the
public repository) and isolates vcpkg/library outputs under a fingerprint. It never checks out, resets, or edits a
sibling `Z:\src\DxUi` working tree. A mismatched or dirty pin fails restore.

`build.ps1` passes `-CheckUpdates`. `Show-DxUiUpdateNotice` from the pinned `Tools/ConsumerUpdate.psm1` then compares
the lock with DxUi `main`. The notice is build output only: yellow when a newer `main` commit has a successful
completed `ci.yml` push run, red when that validation is missing, dark yellow when the pin is ahead of or diverged
from `main` or the lookup cannot run. The same pin prints nothing. The check never edits the lock and never fails a
valid restore.

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
builds the editor shell described in [`UI_ApplicationChrome.md`](../UI/UI_ApplicationChrome.md): MenuBar, TabControl,
Toolbar rail of icon-only Buttons, tool header Label, layers Tree, StatusStrip. Pointer, key, and character messages
dispatch into the host. Preparation runs before the canvas blit (it decides the hole) and composition runs after
it, into the same back buffer, on the idle Present path. The DxUi surface is cleared transparent and composited with
premultiplied alpha, so controls that overlap the canvas hole (tooltips, popups) stay visible. Hidden or zero-extent
EmbeddedHost holds no cached surface (0 bytes). Visible surface stays within the library 64 MiB cap.

Facts about the pinned library that the host designs around (verified against the pinned sources):

- `Toolbar` is a `Panel` subclass with no child layout; consumers set every button's bounds.
- `Toggle` paints a switch regardless of `ButtonVariant::IconOnly`; a latched tool button is an icon-only `Button`
  with `SetPressedVisual(true)`.
- Icon-only buttons draw their text with `FontRole::Icon` (Segoe Fluent Icons); Latin letters do not render there.
- `Tree` reports expander toggles through `IDxTreeDelegate::OnTreeToggleExpanded` and has no drag API.
- `Splitter` hit-tests only its separator plus 2 DIP of slop; the host sizes it over both panes and places the pane
  controls itself from the committed position. `NumericStepper` and `ColorPicker` are Panels with their own children;
  `ColorPicker` ships no strings, so every caption comes from the RedPrism STRINGTABLE.

DxUi C++ stays inside `RedPrism.exe`. Never pass DxUi types, STL, WIL, or exceptions across a future plugin ABI.

## Library promotion status

Shipped. `EditorConsumerControls_2026-09-20.md` closed on 2026-09-21 at DxUi `7e3be94` (catalog 30, API revision 2,
19 library suites green in Debug, Release and ASan Debug, ARM64 Debug build, gallery republished). The C3 fallbacks
are retired; [`UI_ApplicationChrome.md`](../UI/UI_ApplicationChrome.md) describes the consumed splitter, palette
wells with the color picker, and the opacity stepper. Do not implement those three controls in RedPrism.

## Screenshot harness

`--screenshot <png>` shows the titled window with `SW_SHOWNOACTIVATE`, presents chrome and the canvas, waits 800 ms
on a one-shot timer so DxUi animations settle, captures the owned window through `Common/WindowCapture.cpp`
(Windows.Graphics.Capture, no cursor, no activation), and exits 0 when the PNG is written or 8 on capture failure.
Computer Use and desktop screenshot tools are not evidence.

## Validation

```powershell
.\test.ps1 -Platform x64 -Configuration Debug
.\test.ps1 -Platform x64 -Configuration Release
.\test.ps1 -Platform x64 -Configuration "ASan Debug"
```

`--self-test --warp` MUST prove a visible chrome surface with at least one composite and a hidden surface of 0 bytes.
`Tests/BuildProcessTests/DxUiProvenanceTests.ps1` MUST reject a wrong pin.
