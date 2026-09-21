---
name: consumer-dxui
description: Add or update the pinned DxUi dependency, MSBuild imports, RedPrism theme or host adapters, and chrome built from DxUi controls. Use from C2 onward for ChromeHost work and pin bumps; not for composition canvas work.
---

# DxUi consumer integration

Read `Specs/Core/Core_DxUiIntegration.md` and `Specs/UI/UI_ApplicationChrome.md` before touching `ChromeHost` or
the pin. Do not float onto sibling `main`.

## Pin

- Lock `Dependencies/DxUi.lock.json` with repository `https://github.com/RedSalamanders/DxUi`, a 40-character
  commit, `apiRevision` 2, and `targets: ["DxUi"]`. Current pin: `7e3be94c9e3caa46d2997d28ce2251fbb9556619` (DxUi branch
  `codex/editor-consumer-controls`; move to the merge commit once it lands on `main`).
- Restore into an isolated `.build/dependencies/DxUi/` fingerprint. Restore clones from `Z:\src\DxUi` when that
  checkout already contains the commit, so a library commit made locally is consumable before it is pushed. Do not
  checkout, reset, or edit `Z:\src\DxUi` from RedPrism restore.
- Bump the pin only after the library commit is green under DxUi `test.ps1`; then requalify RedPrism with
  `test.ps1` in Debug, Release and ASan Debug and update `Core_DxUiIntegration.md` and this file.
- Import consumer props/targets and reference `src/DxUi.vcxproj` once. Do not enumerate library `.cpp` files.
- Match CRT (`/MDd` Debug and ASan Debug, `/MD` Release), toolset, architecture, and headers.

## Facts about the pinned library (read the pinned sources under `.build/dependencies/DxUi/source/<commit>`)

- `Toolbar` is a `Panel`: it lays out nothing. Set every child's bounds in `ChromeHost::Arrange`.
- `Toggle::Paint` draws a switch even for `ButtonVariant::IconOnly`. A latched tool button is an icon-only `Button`
  plus `SetPressedVisual(true)`; `Button::OnMouseUp` clears `_pressed` before invoking the click, so re-apply the
  latch in the click handler and on every `Prepare`.
- Icon-only buttons draw `_text` with `FontRole::Icon` (Segoe Fluent Icons). Pass a code point that exists;
  `HostTests` checks the rail glyphs against the installed font.
- `Panel` paints no background; the embedded surface is cleared transparent and composited with premultiplied
  alpha. Composite chrome after the canvas blit so tooltips and popups over the hole stay visible.
- `Tree` expects a borrowed `IDxTreeModel` (`GetVisibleItem` fills `TreeItemData` with `id`, `text`, `depth`,
  `hasChildren`, `expanded`, `iconText`) and reports `OnTreeSelectionChanged` / `OnTreeToggleExpanded`. No drag API.
- Native flyouts go through `DxUi::ShowNativeHMenuContextMenu(owner, screen, HMENU, palette)`; an empty `HMENU`
  must not be shown.
- An embedded view owns no window, so a DxUi "screen" point (menu items, context menus) is relative to the origin
  the library assumed: translate it by the owner's client origin minus `DipPointToScreenPoint({0, 0})`
  (`ChromeHost::MenuAnchorScreen`), and hit-test the canvas hole minus the floating cards through one helper
  (`ChromeHost::IsCanvasPoint`) for pointer routing and cursors alike.
- A DxUi press held across a frame is cancelled when the library's focus leaves another control or a layout
  write (`SetBounds`, `SetVisible`, `SetPressedVisual`, `SetChecked`, a value) reaches the tree while the pointer
  is down. Release DxUi's focus before dispatching a chrome press (`ChromeHost::ReleaseFocusForPress`), write
  layout only on change (`PlaceControl`, `ShowControl`), never re-issue a pressed or checked look every sync, and
  keep accessibility attach/update calls out of a press. Mark the active tool with a toolbar toggle's checked state,
  not `SetPressedVisual` on a button. The self-test holds rail presses across frames to keep this true.
- `EmbeddedHost::Prepare(width, height, dpi)` lays out and rasterizes; `Composite(context, viewport)` draws with the
  app's context. Both run on the idle Present path; neither creates a swap chain.

## Host boundary

- The application owns the D3D11 device, swap chain, and Present. `EmbeddedHost` borrows the device.
- Keep DxUi C++ inside `RedPrism.exe`. Never pass DxUi types, STL, WIL, or exceptions across a plugin ABI.
- Product theme adapters stay in RedPrism (`ThemePalette.h`). Splitter, ColorPicker, and NumericStepper ship in the
  pinned library (DxUi `Specs/Plans/Done/EditorConsumerControls_2026-09-20.md`); consume them through `ChromeHost`
  (splitter owns the layers clamp, picker on a CardPanel over the hole, stepper for layer opacity), never clone them.
- Never call `SetBounds` (or change the tree, visibility or enabled state) from inside a captured DxUi drag callback
  such as `Splitter` Preview: the EmbeddedHost interaction revision changes and the next pointer event cancels the
  drag. Record the new width and arrange on the next `Prepare`; sync the viewport through the layout handler.
- Library `test.ps1` does not replace RedPrism product tests.
