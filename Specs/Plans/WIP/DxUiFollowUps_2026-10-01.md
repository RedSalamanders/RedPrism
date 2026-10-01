# DxUi follow-ups: pin update, Tree multi-select, registered host messages, vcpkg toolset and pin restore

## Why

- **Pin age.** The DxUi pin `c403052` (DxUi #19, 22 September) is 125 commits behind DxUi main. Since then:
  - DxUi #29 renamed public interfaces without aliases: `IGridModel`, `IGridDelegate`, `ITreeModel`, `ITreeDelegate`,
    `RunModalLoop`, and the `DxUi::Typography` names `Spec` and `GetSpec`.
  - DxUi #30 replaced the Python `validate_build_matrix.py` with `validate-build-matrix.ps1`.
  - DxUi #35 adds Tree multi-select, which the DxUi plan `TreeReorder_2026-09-21` hands to this product with a pin
    update.
- **Host messages.** DxUi's private window messages were `WM_APP` offsets on the host window. DxUi now registers every
  private message by name (`RedSalamanders.DxUi.<Component>.<Purpose>.v1`) and uses no `WM_APP` value. This product's
  own messages (`Application.h` `WM_APP + 8`, `+ 9` and `+ 10`, the test gates at `+ 0x4B4` and `+ 0x4B6`, and
  `ApplicationAdjustJobs.cpp`'s `+ 0x7D0`) stay its own.
- **vcpkg toolset.** vcpkg builds dependencies with the newest MSVC toolset of the Visual Studio installation, while
  MSBuild uses its default toolset. DxUi's `vcpkg-install.ps1` now pins the discovered default toolset; this
  repository's own `vcpkg-install.ps1` does not yet.
- **Pin restore.** DxUi kept tracked paths up to 176 characters until it set a 150-character budget. `restore-dxui.ps1`
  restores without Git long paths, so a deep root can still fail.

## Implementation checklist

- [ ] Update the DxUi pin to a validated DxUi main that includes Tree multi-select, the registered messages and the
  path budget. Adapt to the #29 renames, and use `validate-build-matrix.ps1` wherever the Python validator was called.
- [ ] Decide where the editor uses Tree multi-select (for example the layers list) and its drag-reorder, per DxUi's
  `TreeReorder_2026-09-21`.
- [ ] Confirm that every window that attaches a `DxUi::WindowHost` forwards all messages, registered ones
  (0xC000–0xFFFF) included, and update `Core_DxUiIntegration.md`.
- [ ] Make this repository's `vcpkg-install.ps1` use the Visual Studio installation the build discovers and its
  default toolset, as DxUi's now does.
- [ ] Restore the pin with Git long paths, or as a sparse checkout that leaves out `Measurements/`, `docs/gallery` and
  `Specs/`. Qualify in all configurations, record the rollback pin, update `Core_DxUiIntegration.md` and
  `Build_Toolchain.md`, then move this plan to Done.

Source: the DxUi plan `MergeAndHostHygiene_2026-10-01` (RedSalamanders/DxUi).
