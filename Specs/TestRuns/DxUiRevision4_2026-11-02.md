# DxUi API revision 4 pin — 2026-11-02

Status: x64 qualification below; ARM64 runtime, physical input and DX-2's product acceptance not run. Owning contract:
[Core_DxUiIntegration.md](../Core/Core_DxUiIntegration.md).

## Pins

| | Commit | API revision |
| --- | --- | --- |
| Before (rollback) | `a059e07c39c58cbf5218c34f7b9f1a9ac29baf3b` (DxUi `ej/wall-compat-redprism-20261028`, not on DxUi `main`) | 3 |
| After | `f2e328a0a26a18ca66bf4b2aea77bf010c9515e1` (DxUi `main`, merge of PR #161) | 4 |

The new pin is a documentation-only descendant (only `Changes/` and `Specs/` differ) of `5fc4d4c3` (PR #160, the consumer
upgrade guide `docs/upgrade.md`), which follows the runtime delivery `6df17007875ae731a3808b475b10bd056328bbdf` (PR #159).
DxUi `ci.yml` passed on `f2e328a0` (run 41481797938). It contains `762983a0`, the keyboard cancel of pending menu hover
timers (DX-2). The sibling `Z:\src\DxUi` did not have `f2e328a0`, so restore cloned it blobless from the public repository.

Toolchain (`.build/dependencies/DxUi/DxUi.identity.x64.json`): v145, MSVC 14.52.37025, Windows SDK 10.0.26100.0, x64 host,
`/MDd` Debug and ASan Debug, `/MD` Release, native build policy `Wall-noWX-DebugZI-ASanZi-ReleaseICF-v1`.

## Adapter changes

- `restore-dxui.ps1` requires API revision 4; the lock names `f2e328a0` and revision 4.
- DxUi controls now delete their copy and move operations, so the first rebuild reported C4625, C4626, C5026 and C5027 on
  nine RedPrism subclasses (`CanvasOverlay`, `CurveEditor`, `LevelsBar`, `SpectrumEditor`, `GradientRamp`,
  `LucideToolButton`, `LucideIconButton`, `PaletteZone`, `PaletteControl`). Each now declares them deleted.
- Device recovery. The first x64 Debug `test.ps1` at the new pin failed the WARP self-test at
  `application-selection-outline-gpu.actual-renderer-device-replacement`: `Renderer::RecoverDeviceForTest` returned
  `E_ACCESSDENIED` from `CreateSwapChainForHwnd`. Diagnosis (temporary prints, removed): the renderer held the only
  reference to the old swap chain and released it, but `ReleaseDeviceResources` flushed the context *before* that
  release; the revision 3 library's `ControlHost::PrepareForSwapChainResize` cleared and flushed the shared immediate
  context during `EmbeddedHost::ReplaceDevice`, which retired the swap chain by accident. Revision 4 skips that for
  embedded hosts (`! _embedded`). `ReleaseDeviceResources` now clears state, releases the view and swap chain, then
  flushes. `Rendering_Composition.md` records the order.
- Prepared layer-row retirement. The first x64 Release `test.ps1` failed the WARP self-test at
  `application-prepared-layer-rows-accessibility.shutdown.final-mta-release-retires-complete-graph-without-owner`
  (reproduced twice directly; Debug passed). A temporary probe showed one strong reference left after the owner,
  the survivor and the foreign MTA reader released theirs: with the STA message pump serviced the graph retired
  62 ms later, without it not within 5 s. DxUi's notification work item (MTA, not joined on detach) reaches the
  root provider by a GIT proxy, and that proxy's last release is a COM message to the owner's STA. The fixture now
  services messages with a bounded `MsgWaitForMultipleObjectsEx` wait until the graph retires; the assertion
  itself is unchanged.
- `EmbeddedHost::Prepare`'s `E_ABORT` (a callback superseded the frame) makes `Renderer::Render` return `S_FALSE`, so
  `Application::RenderFrame` draws again instead of counting a failed frame. New self-test: `RunRenderingSelfTest`
  injects `E_ABORT` at chrome Prepare and requires no failed frame, no pending invalidation, the same device generation
  and exactly one more Present attempt.
- `PointerAction::MiddleDown`/`MiddleUp` are named in `Application::OnPointer`'s switch (RedPrism sends neither).
- Both tree models write the new `TreeItemData::badgeIsDot` (false). `PreparedLayerRows::GetItem` builds
  `TreeAccessibilityItemView` by member; its new `badgeText` stays empty on that experimental path.
- Reviewed with no change needed: `AddChild`/`AddTab` null results (all insertions run in `ChromeHost::Build` on a root
  not yet attached), Tree selection versus focus notifications, described menu names, `ContextMenuSessionCallbacks`,
  `Prepare`'s render-state mutation (every later draw binds its own state), `ComboBox::Item` and
  `StatusStrip::Section` layouts (unchanged).

## Gates

All runs are `test.ps1` (which builds first) from `Z:\src\RedPrism` on `main` `7e8cd2f` plus this change, x64 host,
WARP for the self-test. Every build reported `Diagnostics: 0 warning(s), 0 error(s)` and passed the ISA gate.

| Gate | Result |
| --- | --- |
| `test.ps1` x64 Debug, final sources | Passed in 1,961 s |
| `test.ps1` x64 Release, final sources | Passed in 806 s. Release `RedPrism.exe` SHA256 `7ac1ad9ed3fa972fa2612bebdb3896449d3615ea67c625f48c3a653b08d31dc4`; `DxUi.provenance.json` names `f2e328a0`, API 4, public header tree `afca31c268d9d750beecc7d8c4023467d9ce4d1e` |
| `test.ps1` x64 ASan Debug, final sources | Passed in 2,108 s, with the Stress suite |
| `format.ps1 -Check`, `validate-skills.ps1`, `Tools/Test-Changelog.ps1` | Passed |

Each passing run covered the Host, Document, IO, Composition, Runtime, Performance, Captures and Tooling suites: the
crash probes, the hidden WARP self-test, the benchmark and cancellation smokes and nine harness screenshots. Two SKIPs
are not caused by this change. `existing-old-snapshot` skips because `.build/p1-gradient-qualified` is absent.
`vcpkg-toolset.same-behavior-as-the-dxui-pin` never runs: `VcpkgToolsetTests.ps1` looks for
`.build/dependencies/DxUi/source/<full commit>`, while restore uses the 16-character prefix.

Runs that failed on the way, in order:

1. Debug: the device-recovery failure described above (fixed).
2. Debug: `UiCancellationBenchmarkSmokeTests.ps1` stopped with `reason=cancel-ambient-keyboard-state` at `motion2000`
   trial 1, after operations 0 to 6 had passed. That guard reads the real `GetKeyState` for Ctrl, Alt, Shift and Escape.
   The prepared smoke then passed three runs of three trials in a row on the same binary. Ambient input.
3. Release: the prepared layer-row retirement failure described above (fixed; the run's Debug had passed in 1,256 s).
4. Release: `application-ellipse-press.inside.exact-receipt-prefix-and-history-stamp-acknowledged` failed once.
   Two direct full Release self-tests on the same binary passed (264 s and 260 s), and so did the final run.
   `Application.Tests.RectanglePress.cpp` is the press fixture that does not use `KeyboardState::ScopedSynthetic`,
   so it reads and writes the real keyboard state.

## Not run

- ARM64: no local cross-build at the new pin, and no runtime (needs a native ARM64 host). Main's native qualification
  runs the ARM64 Release regression after the merge.
- DX-2's product acceptance (hover a submenu, move with the keyboard, deliver `WM_TIMER`; the same for UI Automation
  focus and a pending close). Owned by `DeliveryReview_2026-10-31.md`.
- Physical assistive technology, IME and touch input.
- Packaging (`package.ps1`) and the size of the packaged output. The upgrade guide reports a 24.42% larger DxUi archive
  for the library alone; the product's size was not measured.
