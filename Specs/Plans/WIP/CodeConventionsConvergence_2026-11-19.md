# Code conventions convergence (RedHome CV-6-08)

Status: ACTIVE — the local convention rows are in place. This plan owns the API 6 receiving work and source-bound replays.
Owner: RedPrism maintainers.
Prepared: 2026-11-19.

- **Origin**: step CV-6-08 of the cross-repository
  [convergence plan](https://github.com/RedSalamanders/RedHome/blob/main/wiki/Working-From-RedHome.md#plan-and-owner-decisions)
  and section H of its
  [divergence register](https://github.com/RedSalamanders/RedHome/blob/main/wiki/Working-From-RedHome.md#divergence-register).
  Each register row names one target for every repository; this plan lists the rows that move RedPrism.
- **Owning contracts**: [windowing](../../UI/UI_Windowing.md), [settings](../../Core/Core_Settings.md),
  [architecture](../../Core/Core_Architecture.md), [performance and resources](../../Core/Core_PerformanceAndResources.md),
  [DxUi integration](../../Core/Core_DxUiIntegration.md) and [changes workflow](../../Core/Core_Changes.md).
  The shared skills in `../RedHome/.agents/skills/`
  (`d3d11-win32-host`, `yyjson`, `app-settings`, `file-io-safety`, `localization`) state the target rules once.

## Rules

- One register row per pull request, with its `Changes/` fragment. A style or flag change never shares a pull
  request with a functional change.
- Merge gate (RedHome plan decision D21): the pull request checks pass and the six profiles (x64 and ARM64 ×
  Debug, Release, ASan Debug) build with zero warnings. Tests run where they can and are otherwise recorded as
  "tests deferred (D21)". A compile is not a runtime pass.
- No user-visible behavior changes beyond the row's target.

## Rows

- [x] **H-19 Device-loss results.** The renderer and GPU job retry paths include `DXGI_ERROR_DEVICE_HUNG`.
  Native and physical device-recovery replays are tracked in RedHome's D21 ledger.
  [RedPrism #91](https://github.com/RedSalamanders/RedPrism/pull/91)
- [x] **H-20 Occlusion.** Production rendering follows application-owned visibility, minimize, cloak and power signals.
  Physical replay remains in RedHome's D21 ledger. [RedPrism #94](https://github.com/RedSalamanders/RedPrism/pull/94)
- [x] **H-09 `yyjson` read flags.** Settings read JSON5 with BOM support, apply finite typed validation and retain
  additive values across saves. [RedPrism #109](https://github.com/RedSalamanders/RedPrism/pull/109)
- [x] **H-15 Settings reload.** A worker coalesces directory changes and posts to the UI thread. The UI thread
  applies valid edits and keeps its last good settings when the selected file cannot be used. Cold load
  recovers malformed JSON through a guarded backup and template. A readable incompatible file stays in place.
  [RedPrism #114](https://github.com/RedSalamanders/RedPrism/pull/114)
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

- [x] **H-27 Private member naming.** Private and protected non-static fields use `_camelCase` and public
  data fields use `camelCase`. The root `.clang-tidy` defines the naming and reserved-identifier checks.

## Rows already at their target or not applicable

- **H-11** (`--self-test` forces WARP), **H-12** (`UI_Windowing.md`) and **H-23** (`RedPrism/KeyboardState.h`) are the
  targets other repositories move to.
- **H-13** is converged in RedHome: `AGENTS.md` already states the non-throwing `new` rule the code follows.
- **H-16** keeps the local dump and pending marker,
  claims one startup notice, and retains ten post-adoption reports under D24. [RedPrism #70](https://github.com/RedSalamanders/RedPrism/pull/70)

## API 6 receiving work

- Select a qualified exact API 6 revision under D20. Adapt the consumer ABI and failure host (H-01 and H-02) and the
  published logging and posted-payload helpers (H-05 and H-14) where RedPrism uses them.
- Adopt the compatible producer restore, provenance and native build policy. Use the neutral triplet property and
  shared MSVC family guard where the producer contract applies. Preserve the public commands, package layout and
  documented build profiles. Bind headers, archive and policy to the same revision.
  [DxUi consumption contract](https://github.com/RedSalamanders/DxUi/blob/main/Specs/Build/Build_ToolchainAndConsumption.md)
- Verify all six evaluated profiles and exact builds, clean restore and mismatched pin refusal. Run affected
  regressions and admitted native Release and package checks on x64 and ARM64.

The [RedHome D21 ledger](https://github.com/RedSalamanders/RedHome/blob/main/wiki/Working-From-RedHome.md#qualification-and-replays)
records the accepted-project and session-end fixture repairs and the remaining runtime and physical replays.
