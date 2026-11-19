# Code conventions convergence (RedHome CV-6-08)

Status: ACTIVE — plan written; code rows start after the 155-line reformat (#33) is merged.
Owner: RedPrism maintainers.
Prepared: 2026-11-19.

- **Origin**: step CV-6-08 of the cross-repository
  [convergence plan](https://github.com/RedSalamanders/RedHome/blob/main/Specs/Plans/WIP/Convergence_2026-11-17.md)
  and section H of its
  [divergence register](https://github.com/RedSalamanders/RedHome/blob/main/Specs/Plans/WIP/Convergence_2026-11-17/DivergenceRegister.md).
  Each register row names one target for every repository; this plan lists the rows that move RedPrism.
- **Owning contracts**: [windowing](../../UI/UI_Windowing.md), [settings](../../Core/Core_Settings.md),
  [architecture](../../Core/Core_Architecture.md), [performance and resources](../../Core/Core_PerformanceAndResources.md)
  and [changes workflow](../../Core/Core_Changes.md). The shared skills in `../RedHome/.agents/skills/`
  (`d3d11-win32-host`, `yyjson`, `app-settings`, `file-io-safety`, `localization`) state the target rules once.

## Rules

- No C or C++ source changes while the reformat pull request is open; each code row rebases on it.
- One register row per pull request, with its `Changes/` fragment. A style or flag change never shares a pull
  request with a functional change.
- Merge gate (RedHome plan decision D21): the pull request checks pass and the six profiles (x64 and ARM64 ×
  Debug, Release, ASan Debug) build with zero warnings. Tests run where they can and are otherwise recorded as
  "tests deferred (D21)". A compile is not a runtime pass.
- No user-visible behavior changes beyond the row's target.

## Rows

- [ ] **H-19 Device-loss results.** The renderer and the GPU job retry sets accept `DXGI_ERROR_DEVICE_REMOVED`,
  `DXGI_ERROR_DEVICE_RESET` and `DXGI_ERROR_DRIVER_INTERNAL_ERROR`. Target: the shared set, which adds
  `DXGI_ERROR_DEVICE_HUNG` (and `D2DERR_RECREATE_TARGET` only where Direct2D draws to the swap chain).
- [ ] **H-20 Occlusion.** The renderer acts on `DXGI_STATUS_OCCLUDED` from its flip-model `Present`. DxUi measured that
  status is not a usable signal for a flip-model swap chain on Windows 11 25H2 (DxUi
  `Measurements/FlipModelOcclusion_2026-11-19/`). Target: occlusion from signals the application owns, as
  `d3d11-win32-host` states.
- [ ] **H-09 `yyjson` read flags.** Settings are read with `YYJSON_READ_ALLOW_BOM` only. Target:
  `YYJSON_READ_JSON5 | YYJSON_READ_ALLOW_BOM`; every numeric field check rejects non-finite values first, since JSON5
  admits `NaN` and `Infinity`.
- [ ] **H-15 Settings reload.** One `FindFirstChangeNotification` is waited beside the message queue with a 250 ms
  timer. Target (RedHome decision D19): a worker that waits on the change and a stop event, coalesces, and posts one
  notification to the UI thread, which parses and applies.
- [ ] **H-25 Replace sequence.** Projects and images use `ReplaceFileW` with `REPLACEFILE_IGNORE_MERGE_ERRORS` and a
  `MoveFileExW(MOVEFILE_REPLACE_EXISTING)` fallback without write-through; settings use
  `MoveFileExW(MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)`. Target: the shared sequence. The register's
  common sequence is open again for RedXe's guarded POSIX rename (an owner decision), so this row waits for it.
- [ ] **H-26 Settings file names.** `settings.json` and `settings.debug.json`. Target: `RedPrism.settings.json` and
  `RedPrism-debug.settings.json`. Existing users' files must be found and migrated once, so the row first states
  that migration in the settings contract.
- [ ] **H-07 Satellite layout.** `Languages\<tag>\RedPrism.resources.dll`. Target: `Lang\<Module>-<culture>.dll`. No
  translation ships in v1, so only the loader and the build layout change.
- [ ] **H-06 Resource string helpers.** `Common/Resources.h` lacks the embedded variants and `MessageBoxResource`.
  Target: the shared helper names with the existing `std::format_error` fallback kept.

## Rows already at their target or not applicable

- **H-11** (`--self-test` forces WARP), **H-12** (`UI_Windowing.md`) and **H-23** (`RedPrism/KeyboardState.h`) are the
  targets other repositories move to.
- **H-13** is converged in RedHome: `AGENTS.md` already states the non-throwing `new` rule the code follows.
- **H-01, H-02, H-05, H-14** wait for API 6 (RedHome decision D20); **H-16** waits for an owner choice of one crash
  artifact shape.
