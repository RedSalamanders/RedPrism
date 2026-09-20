# C14 Product hardening

> Spawned from `Specs/Plans/Done/WindowsDirectXPort_2026-09-20.md`. Index before coding.

- **Status**: ACTIVE (sequel; not started)
- **Depends on**: C7
- **Owning specs**: `Core/Core_Settings.md` (create), `Core/Core_CrashHandling.md` (create), `UI/UI_ApplicationChrome.md`

## Outcome

Settings store (paths, schema, cold recovery, UI-thread live reload), local minidumps / prior-crash UI (no upload),
localization pass (STRINGTABLE, no `swprintf_s` user strings), UIA for chrome, high contrast.

## Remaining work

- [ ] Settings skill + `Specs/Core/Core_Settings.md`
- [ ] Crash skill + `Specs/Core/Core_CrashHandling.md`
- [ ] Localization of chrome strings
- [ ] UIA / high contrast for DxUi chrome
- [ ] Tests for settings recovery and crash harness (no desktop automation)

## STOP

Uploading dumps; busy-poll watchers; settings off the UI thread.

## Done

Checklist green, specs updated, plan moved to Done.
