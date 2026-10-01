# DxUi follow-ups: pin update, Tree multi-select, registered host messages, vcpkg toolset and pin restore

## Why

- **Pin age.** The DxUi pin `c403052` (DxUi #19, 22 September) was 125 commits behind DxUi main when this plan was
  written. Since then:
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
  repository's own `vcpkg-install.ps1` did not (an ARM64 restore failed on a toolset without an x64-hosted ARM64
  compiler).
- **Pin restore.** DxUi kept tracked paths up to 176 characters until it set a 150-character budget. `restore-dxui.ps1`
  restored without Git long paths, so a deep root could still fail.

## Implementation checklist

- [x] Update the DxUi pin to a validated DxUi main with the registered messages and the path budget: `271bd54`
  (DxUi #45, API revision 3; the rollback is `c403052`). The #29 renames are applied (this product implements only
  `ITreeModel` and `ITreeDelegate`), and no Python validator is called here, so `validate-build-matrix.ps1` replaces
  nothing.
- [ ] Move the pin once more to a DxUi main that includes Tree multi-select (DxUi #35, not merged at `271bd54`).
- [ ] Decide where the editor uses Tree multi-select (for example the layers list) and its drag-reorder, per DxUi's
  `TreeReorder_2026-09-21`. This needs the pin above.
- [x] Confirm that every window that attaches a `DxUi::WindowHost` forwards all messages, registered ones
  (0xC000–0xFFFF) included, and update `Core_DxUiIntegration.md`. No window attaches one: the main window hosts one
  `EmbeddedHost` and hands every message to `DefWindowProcW` last; the contract records the rule and this check.
- [x] Make this repository's `vcpkg-install.ps1` use the Visual Studio installation the build discovers and its
  default toolset, as DxUi's now does (`Build/VcpkgToolset.psm1`, `Tests/BuildProcessTests/VcpkgToolsetTests.ps1`; the
  first restore rebuilds the packages, because the triplet change alters their ABI hash).
- [x] Restore the pin with Git long paths and as a sparse checkout that leaves out `Measurements/`, `docs/gallery` and
  `Specs/` (`Build/DxUiRestore.psm1`, `Tests/BuildProcessTests/DxUiRestoreTests.ps1`, a restore into a destination of about
  210 characters). Qualified in x64 Debug, Release and ASan Debug and in the three ARM64 builds, rollback pin recorded,
  `Core_DxUiIntegration.md` and `Build_Toolchain.md` updated
  ([record](../../TestRuns/DxUiRevision3_2026-10-01.md)).
- [ ] Run the checks that need the user's clipboard at the new pin on a machine whose clipboard is free: IoTests'
  `clipboardtransfer.*` and the self-test's system-clipboard checks. The record above explains why they were not run.
- [ ] Move this plan to Done once the three open items are closed.

Source: the DxUi plan `MergeAndHostHygiene_2026-10-01` (RedSalamanders/DxUi).
