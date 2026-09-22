# RedPrism settings

Status: current normative repository contract
Last reviewed: 2026-09-21
Owner: `Common/SettingsStore.*`, `RedPrism/ApplicationSettings.cpp`, `RedPrism/Main.cpp`

## Scope

This contract owns the user settings document: where it lives, what it holds, how a missing, invalid or edited
file is treated, and how the running application applies it. The chrome that shows the settings' effects lives in
[`UI_ApplicationChrome.md`](../UI/UI_ApplicationChrome.md); the tests in
[`Testing_Validation.md`](../Testing/Testing_Validation.md).

## Paths

| Build | File |
| --- | --- |
| Release | `%LocalAppData%\RedPrism\settings.json` |
| Debug, ASan Debug | `%LocalAppData%\RedPrism\settings.debug.json` |

`SettingsStore::DefaultPath(debugBuild)` names the file; `LocalAppDataFolder` comes from `SHGetFolderPathW`
(`CSIDL_LOCAL_APPDATA`). The harness switches (`--self-test`, `--screenshot`, `--demo`, `--bench`, `--crash-probe`)
MUST NOT read or write the user's file: they run with the defaults (dark chrome, English) and the self-test drives
the live reload from a file of its own under `%TEMP%`.

## Document

A JSON object (UTF-8, pretty-printed by the store) whose `schema` field (an integer ≥ 1) carries compatibility. The
file name never does. Fields the store does not know are kept through a save; a field it knows but cannot use
takes its default, field by field, so one bad value never discards the rest.

| Field | Type, range | Default | Applies |
| --- | --- | --- | --- |
| `schema` | integer ≥ 1 (currently 1; a newer value is kept on save) | 1 | — |
| `theme` | `"system"`, `"light"`, `"dark"` | `"system"` | live |
| `language` | BCP-47 tag (letters, digits, hyphens, 2–35 characters) or empty for Windows' display language | `""` | next start (the satellite table, [`UI_ApplicationChrome.md`](../UI/UI_ApplicationChrome.md) Strings) |
| `layersPanelWidth` | number, clamped to 100–2000 DIP (the chrome clamps again to its 160–480) | 240 | live |
| `sampleRing` | boolean (View › Sample Ring) | `true` | live |
| `jpegQuality` | number clamped to 0–1 (File › Export JPEG) | 0.8 | live |
| `window` | object `{x, y, width, height, maximized}`; ignored unless width ≥ 200, height ≥ 150 and every value fits ±32767 | absent | next start |

`theme: "system"` follows Windows' *Choose your app mode* (`AppsUseLightTheme`); Windows high contrast overrides
any theme with the system colors while it is on. A document that is not an object, has no integral `schema` ≥ 1
or does not parse is not a settings document.

## Cold load (startup)

`SettingsStore::LoadCold(path)`:

| File | Result |
| --- | --- |
| a settings document | its settings (`Loaded`) |
| missing | the defaults; the template (the defaults, serialized) is written (`Missing`) |
| not a settings document | the bytes move to `<path>.invalid-<yyyyMMdd-HHmmss-fff>`, the template is written, the defaults are used (`Invalid`) |
| unreadable (I/O error, larger than 16 MiB) | the defaults; nothing is written (`Unreadable`) |

## Live reload

The application holds one `FindFirstChangeNotification` watch on the settings directory (last write, size, name and
creation changes) and waits on it beside the message queue and the frame-latency waitable
(`MsgWaitForMultipleObjectsEx`); nothing polls. A signal arms a 250 ms timer so an editor's several writes coalesce;
the timer, on the UI thread, compares the file's last-write time and size with what the app last saw and, when
they differ, calls `SettingsStore::LoadLive`. A settings document that differs from the last-good one applies at
once (theme, sample ring, layers panel width, JPEG quality; the language and the window placement wait for the
next start). A file that is not a settings document keeps the file as the user left it and the last-good settings
in memory, and says so on the debugger output; nothing is written. The app's own save updates the seen state so it
never reloads itself.

STOP: settings are parsed and applied on the UI thread only; a live load never writes the watched file; no
busy-poll watcher.

## Save

`SettingsStore::Save(path, settings)` writes `<path>.tmp-<pid>` and replaces the file with `MoveFileExW`
(`REPLACE_EXISTING | WRITE_THROUGH`), creating the directory tree. A valid existing document is merged (unknown
fields and a newer `schema` survive); an invalid one is backed up to `<path>.invalid-…` first. The application saves
when the main window closes: the window placement (`GetWindowPlacement`'s normal rectangle and whether it was
maximized), the layers panel width and the sample ring; `jpegQuality` and `theme` are what the file said.

## Startup application

`Main.cpp` loads the file before the window exists and hands the settings to `Application::UseSettings`; the window
takes a saved placement that still meets a monitor (`MonitorFromRect`) and opens maximized when it was; the chrome
palette, the sample ring and the layers width apply once the chrome exists.

## Command chords

A finished command's default chord is not a settings field. It lives on the command id in
[`UI_ApplicationChrome.md`](../UI/UI_ApplicationChrome.md). A later override will name that same id. The settings
document does not store a replacement chord today, and a key or control still runs the command through that id.

## Validation

`IoTests` `settings.*`: the file names and default paths; parsing (a minimal document is the defaults, every field
reads, unusable values fall back, non-documents are rejected); serialization (round trip, unknown fields and a
newer schema kept, an unplaced window dropped, an invalid existing document ignored); the cold load of a missing
file (template), an invalid file (backup with the original bytes, template, defaults) and an unreadable path; the
live load of an invalid file (file untouched, nullopt), a valid one and a missing one; a save over an invalid file
(backup), keeping unknown fields, leaving no temporary and creating the directory. The WARP self-test edits its
own settings file and proves the reload applies the theme, the sample ring and the layers width live, that a
broken file changes nothing and is not rewritten, that restoring it applies again, and that a save records the
window placement.
