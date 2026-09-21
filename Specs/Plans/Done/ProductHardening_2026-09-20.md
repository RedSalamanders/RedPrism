# C14 Product hardening

> Spawned from `Specs/Plans/Done/WindowsDirectXPort_2026-09-20.md`. Index before coding.

- **Status**: DONE
- **Depends on**: C7
- **Owning specs**: `Core/Core_Settings.md`, `Core/Core_CrashHandling.md`, `UI/UI_ApplicationChrome.md`

## Outcome

Settings store (paths, schema, cold recovery, UI-thread live reload), local minidumps / prior-crash UI (no upload),
localization pass (STRINGTABLE, no `swprintf_s` user strings), UIA for chrome, high contrast.

## Remaining work

- [x] Settings skill + `Specs/Core/Core_Settings.md`
- [x] Crash skill + `Specs/Core/Core_CrashHandling.md`
- [x] Localization of chrome strings
- [x] UIA / high contrast for DxUi chrome
- [x] Tests for settings recovery and crash harness (no desktop automation)

## STOP

Uploading dumps; busy-poll watchers; settings off the UI thread.

## Done

Checklist green, specs updated, plan moved to Done.

## Closeout (2026-09-21)

Shipped: `Common/SettingsStore.*` (the JSON document with `schema`, Debug/Release file names, per-field
validation, unknown fields kept through a merge-save, cold missing → template, cold invalid → backup + template,
live invalid → nullopt, atomic saves) with `IoTests` `settings.*`; `RedPrism/ApplicationSettings.cpp` (one
directory change notification waited beside the message queue, a 250 ms coalescing timer, reload and apply on the
UI thread, the theme, the save on close with the window placement, and the self-test gates);
`Common/CrashDumps.*` (the unhandled-exception filter plus terminate, invalid-parameter, pure-call and abort
routed through one heap-free minidump writer, the `pending.txt` marker, pruning to ten) installed first in
`Main.cpp`, the next-start prompt that names the dump and opens its folder, `--crash-probe <directory>` and the
`test.ps1` gate that requires the dump and marker, `HostTests` `crash.*`; `Common/Localization.*` satellite
tables (`Languages\<tag>\RedPrism.resources.dll`, candidate order from the `language` setting and Windows'
display languages, `SetStringResourceSatellite` with per-id fallback) with `HostTests` `locale.*`, and the last
literal user string (the unknown-argument message) moved to the table; `RedPrism/AccessibilityRoot.*` and
`ChromeHostAccessibility.cpp` (the HWND's UIA root with the DxUi chrome as its child through
`EmbeddedHost::AttachAccessibility`, `WM_GETOBJECT`, placement on size, move and focus, accessible names on every
interactive control and the audit the self-test enforces); `ThemePalette.h` light and high-contrast palettes with
`RefreshTheme` on the system notifications; a crash-handling skill and the settings and localization skills
brought current.

Decisions and deviations: `theme` defaults to `system` for ordinary runs while every harness switch pins the dark
palette so screenshots stay deterministic; the language and the window placement apply at the next start rather
than live; the sample ring and panel width persist (the legacy `layersPanelWidth` and JPEG quality are the two
legacy preferences; the JPEG quality has no sheet in the port and is read from the file); no translation ships
(no translators), only the mechanism and its tests; `abort` in Release reaches the filter through the `SIGABRT`
handler rather than the CRT's fast-fail, which is why the signal is installed; the crash filter answers
`EXCEPTION_EXECUTE_HANDLER`, so Windows Error Reporting never shows its own dialog; the probe raises the
application-defined `0xE0000005` in AddressSanitizer builds because the sanitizer reports access violations itself; UIA is proven in-process (the
provider tree from the window root, names, a hit test), never through desktop automation. Gates: `test.ps1` Debug,
Release and ASan Debug (each now runs the crash probe), `build.ps1 -Platform ARM64 -Configuration Debug`,
`format.ps1 -Check`, `validate-skills.ps1`.
