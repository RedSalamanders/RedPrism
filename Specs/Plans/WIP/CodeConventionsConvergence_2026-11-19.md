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
- [x] **H-20 Occlusion.** Production rendering follows application-owned visibility, minimize, cloak and power signals.
  Physical replay remains in RedHome's D21 ledger. [RedPrism #94](https://github.com/RedSalamanders/RedPrism/pull/94)
- [x] **H-09 `yyjson` read flags.** Settings read JSON5 with BOM support, apply finite typed validation and retain
  additive values across saves. [RedPrism #109](https://github.com/RedSalamanders/RedPrism/pull/109)
- [x] **H-15 Settings reload.** A worker coalesces directory changes and posts to the UI thread. The UI thread
  applies valid edits and keeps its last good settings when the selected file cannot be used. Cold load
  recovers malformed JSON through a guarded backup and template. A readable incompatible file stays in place.
- [x] **H-25 Guarded publication.** Image, settings and project saves guard the accepted destination version through
  commit. Full Runtime and native ARM64 replay remain in RedHome's D21 ledger.
  [RedPrism #88](https://github.com/RedSalamanders/RedPrism/pull/88)
- [x] **H-26 Settings file names.** Debug uses `RedPrism-debug.settings.json` and Release uses
  `RedPrism.settings.json`. When the selected canonical file is absent, startup can copy the matching legacy
  profile into the vacant destination. The legacy file is preserved.
  [RedPrism #118](https://github.com/RedSalamanders/RedPrism/pull/118)
- [x] **H-07 Satellite layout.** The loader checks `Lang\RedPrism-<culture>.dll` and retains the legacy lookup.
  [RedPrism #106](https://github.com/RedSalamanders/RedPrism/pull/106)
- [x] **H-06 Resource string helpers.** The common embedded helpers and `MessageBoxResource` are in `Common/Resources.h`.
  [RedPrism #102](https://github.com/RedSalamanders/RedPrism/pull/102)

## Rows already at their target or not applicable

- **H-11** (`--self-test` forces WARP), **H-12** (`UI_Windowing.md`) and **H-23** (`RedPrism/KeyboardState.h`) are the
  targets other repositories move to.
- **H-13** is converged in RedHome: `AGENTS.md` already states the non-throwing `new` rule the code follows.
- **H-01, H-02, H-05, H-14** wait for API 6 (RedHome decision D20). **H-16** keeps the local dump and pending marker,
  claims one startup notice, and retains ten post-adoption reports under D24. [RedPrism #70](https://github.com/RedSalamanders/RedPrism/pull/70)
