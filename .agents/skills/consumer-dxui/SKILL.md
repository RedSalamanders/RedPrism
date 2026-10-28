---
name: consumer-dxui
description: Add or update the pinned DxUi dependency, MSBuild imports, RedPrism theme or host adapters, and chrome built from DxUi controls. Use for ChromeHost work and pin bumps; not for composition canvas work.
---

# DxUi consumer integration

Read the [consumer contract](../../../Specs/Core/Core_DxUiIntegration.md) and
[application chrome contract](../../../Specs/UI/UI_ApplicationChrome.md) before touching `ChromeHost` or the pin.
Use `Dependencies/DxUi.lock.json` for current identity and the consumer contract's evidence links for qualification
and rollback history. A skill is not a pin manifest or a successful test receipt. Do not float onto sibling `main`.

## Pin

- Lock `Dependencies/DxUi.lock.json` to the reviewed repository, exact commit, API revision and targets required by
  the consumer contract. A rollback must also restore the matching consumer adapter. Rebuild DxUi and RedPrism
  from matching headers and library even when the numeric API revision is unchanged.
- Call or import only what the pinned `capabilities.json` lists under `consumerInterface` (scripts, module functions with their
  parameters, MSBuild files, the header root); `restore-dxui.ps1` imports `Tools/ConsumerBuild.psm1` and
  `Tools/ConsumerUpdate.psm1`. Revision 3 renamed the model and delegate interfaces `IGridModel`, `IGridDelegate`, `ITreeModel`
  and `ITreeDelegate`, and registers DxUi's private window messages by name: forward every message, registered ones included,
  to `HandleMessage` of a window that attaches a `DxUi::WindowHost` (RedPrism attaches none: `Core_DxUiIntegration.md`).
- `build.ps1` passes `-CheckUpdates` to `restore-dxui.ps1`. The pinned `Show-DxUiUpdateNotice` prints one advisory
  when DxUi `main` is not the lock. It does not edit the lock or fail a valid restore. Yellow means a validated
  newer `main`; red means `main` moved without a successful `ci.yml` run; dark yellow means the pin is ahead,
  diverged, or the lookup failed. The same pin is quiet.
- Restore into an isolated `.build/dependencies/DxUi/` fingerprint. Restore clones from `Z:\src\DxUi` when that
  checkout already contains the commit, so a library commit made locally is consumable before it is pushed. Do not
  checkout, reset, or edit `Z:\src\DxUi` from RedPrism restore. The clone is a sparse checkout with Git long paths (no
  `Measurements/`, `docs/gallery` or `Specs/`; `Build/DxUiRestore.psm1`): read the library's headers and sources in the
  pinned checkout and its specifications in the DxUi repository.
- Bump the pin only after the library commit is green under DxUi `test.ps1`; then requalify RedPrism with
  `test.ps1` in Debug, Release and ASan Debug and update the consumer contract/evidence. Edit this skill only when
  the integration workflow changes, not to duplicate a new commit hash or passing matrix.
- Import consumer props/targets and reference `src/DxUi.vcxproj` once. Do not enumerate library `.cpp` files.
- Match CRT (`/MDd` Debug and ASan Debug, `/MD` Release), toolset, architecture, and headers.

## Facts about the pinned library (read the pinned sources under `.build/dependencies/DxUi/source/<commit>`)

- `Toolbar` is a `Panel`: it lays out nothing. Set every child's bounds in `ChromeHost::Arrange`.
- `Toggle::Paint` draws a switch even for `ButtonVariant::IconOnly`, and `Toolbar::AddToggleButton` gives that
  switch (the rail lost its glyphs once). A latched tool button is an icon-only `Button` plus `SetPressedVisual(true)`;
  `Button::OnMouseUp` clears `_pressed` before invoking the click, so sync the latch from `DebugIsPressed()` once the
  pointer is up (`ChromeHost::SyncToolVisuals` defers while `_pointerPressed`), never on every `Prepare` and never
  during a press.
- `Panel` paints no background; the embedded surface is cleared transparent and composited with premultiplied
  alpha. Composite chrome after the canvas blit so tooltips and popups over the hole stay visible.
- `Tree` expects a borrowed `ITreeModel` (`GetVisibleItem` fills `TreeItemData` with `id`, `text`, `depth`,
  `hasChildren`, `expanded`, `iconText`) and reports `OnTreeSelectionChanged` / `OnTreeToggleExpanded`.
  `SetReorderEnabled` lets a completed row drag call `ITreeDelegate::OnTreeReorder` once; the library does not move
  the model. This API exists at both the qualified commit and its nearest rollback.
- The qualified API revision 3 header adds optional `ITreeModel::CapturePreparedAccessibilityRows()` and
  `IPreparedTreeAccessibilityRows`. A null source keeps legacy capture; a non-null source represents the exact
  prepared model epoch including offscreen rows. Capture only retains the existing shared owner, and its indexed
  reads are bounded and allocation-free. Published UIA snapshots retain that owner through provider reads and
  replacement. Since the public interface changed while the API revision stayed 3, rebuild the consumer with the
  matching DxUi library; an API-revision match alone is insufficient.
- Icon-only buttons draw `_text` with `FontRole::Icon` when Segoe Fluent Icons is installed, and with `FontRole::Body`
  otherwise so a Unicode stand-in does not become a missing-glyph box.
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
  keep accessibility attach/update calls out of a press. Mark the active tool with `SetPressedVisual` written only
  between presses, never with a toolbar toggle (a switch). The self-test holds rail presses across frames to keep
  this true.
- `EmbeddedHost::Prepare(width, height, dpi)` lays out and rasterizes; `Composite(context, viewport)` draws with the
  app's context. Both run on the idle Present path; neither creates a swap chain.
- `PrepareInteraction` only acknowledges caller-arranged live bounds at the last painted extent/DPI. `SyncChrome`
  finishes arranging controls before acknowledging rapid input between frames; full painting stays on the frame
  path. Initial/failed/hidden/zero/device/extent/DPI transitions require full `Prepare`. It publishes no text/UIA
  geometry and changes no surface/preparation/composition counters. Never acknowledge partial or paint-derived layout.
- The embedded UI Automation tree is a snapshot that DxUi republishes only when `EmbeddedHost::UpdateAccessibility`
  is called after a preparation (with nothing prepared since and the same placement it returns `S_FALSE` at once), and
  each call walks the whole control tree. `ChromeHost::Prepare` calls it after a preparation while a client listens
  (`UiaClientsAreListening`), when the accessible structure changed (`_accessibleStructureChanged`: a tool switch, a
  card, `ShowControl`, the layer rows, the title) or when a client asked meanwhile; otherwise the snapshot waits until
  `WM_GETOBJECT` or `ChromeHost::AccessibilityRoot` publishes it (`PublishAccessibilityForClient`). Never publish only
  when the placement moves, or a panel or card shown later never reaches UI Automation, and never on every preparation,
  or every pointer move walks the tree.
- Most DxUi setters invalidate even when the value is unchanged (`ComboBox::SetSelectedIndex`,
  `StatusStrip::SetSectionText`, `NumericStepper::SetValue`, `ColorSwatch::SetSwatchValue`, `TabControl::SetTabTitle`),
  and any invalidation repaints the whole embedded surface: ChromeHost writes a region only when its state changed
  (`_resync`, RF-1001).

## Host boundary

- The application owns the D3D11 device, swap chain, and Present. `EmbeddedHost` borrows the device.
- Direct2D objects the host caches for its own drawing (the rail's Lucide SVG documents) are released in
  `ChromeHost::ReplaceDevice` and `ChromeHost::Detach`, while DxUi's `GraphicsDevice` still holds the factory. Never
  leave a Direct2D object in static storage for the exit-time destructors: d2d1 frees a factory inside the `Release`
  that drops its last reference and then leaves the factory's lock (RF-1110, access violations in `d2d1.dll` at exit).
- Keep DxUi C++ inside `RedPrism.exe`. Never pass DxUi types, STL, WIL, or exceptions across a plugin ABI.
- Product theme adapters stay in RedPrism (`ThemePalette.h`). Splitter, ColorPicker, and NumericStepper ship in the
  pinned library (DxUi `Specs/Plans/Done/EditorConsumerControls_2026-09-20.md`); consume them through `ChromeHost`
  (splitter owns the layers clamp, picker on a CardPanel over the hole, stepper for layer opacity), never clone them.
- Never call `SetBounds` from inside a captured DxUi drag callback such as `Splitter` Preview. Record the new
  width and arrange on the next `Prepare`. A bounds revision keeps the drag while the splitter stays in the tree,
  enabled and visible. Hiding, disabling, removing it, or resizing the view still cancels. Sync the viewport
  through the layout handler, not from inside the callback.
- Library `test.ps1` does not replace RedPrism product tests.
- The prepared layer-row/accessibility consumer path is an experimental draft with production disabled. Do not
  describe the consumer matrix, client-reentrant UIA behavior, actual UIA latency, largest-count/name owner cost,
  G1, production activation, or P1 as qualified until their separate consumer gates pass.
