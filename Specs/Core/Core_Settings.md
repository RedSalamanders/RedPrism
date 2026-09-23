# RedPrism settings

Status: current normative repository contract
Last reviewed: 2026-09-23
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
| `window` | object `{x, y, width, height, maximized}`; ignored unless width ≥ 200, height ≥ 150 and every value is an integral number (10 and 10.0 alike) within ±32767 | absent | next start |
| `shortcuts` | array of replacement chords. Each object names `commandId` (text, `cmd/...`, the same grammar RedSalamander uses) and exactly one of `vk` (a letter, digit, `F1`–`F24`, a named key such as `Delete` or `NumpadPlus`, or `VK_XX`; any case) or `keyPosition` (`numberRowMinus`, `numberRowPlus`, `bracketLeft`, `bracketRight`; any case). Optional `ctrl`, `alt`, `shift` booleans default to false. An entry that is not that shape, or whose `commandId` does not start with `cmd/`, is skipped (and kept in the file). Which entries apply, and how, is the binding table's rule (Command chords, below): a later entry for the same `commandId` replaces an earlier one | `[]` (omitted on save when empty) | live |

`theme: "system"` follows Windows' *Choose your app mode* (`AppsUseLightTheme`); Windows high contrast overrides
any theme with the system colors while it is on. A document that is not an object, has no integral `schema` ≥ 1
(an integer, or a number with no fraction) or does not parse is not a settings document. A UTF-8 byte order mark
before the object is allowed: the document is the same.

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
once (theme, sample ring, layers panel width, JPEG quality, shortcut replacements; the language and the window
placement wait for the next start). A file that is not a settings document keeps the file as the user left it and the last-good settings
in memory, and says so on the debugger output; nothing is written. The app's own save updates the seen state so it
never reloads itself. A watch that cannot be re-armed, or whose directory no longer exists (deleted or renamed
away), would stay signaled and wake the wait at once, forever: the app watches the directory afresh, and when there
is no directory left it stops watching (live reload then waits for the next start) and keeps the last-good settings.

STOP: settings are parsed and applied on the UI thread only; a live load never writes the watched file; no
busy-poll watcher.

## Save

`SettingsStore::Save(path, settings)` writes `<path>.tmp-<pid>` (flushed, and closed before a failed write deletes it)
and replaces the file with `MoveFileExW` (`REPLACE_EXISTING | WRITE_THROUGH`), creating the directory tree. A valid
existing document is merged: unknown fields and a newer `schema` survive, and so do the `shortcuts` entries this
version cannot read and the unknown fields inside the entries it keeps (when the entries already say the settings'
bindings the array is left exactly as the file has it; otherwise the unread entries stay first, then one entry per
binding, reusing an existing entry that reads the same). An invalid existing document is backed up to
`<path>.invalid-…` first. The application saves
when the main window closes: the window placement (`GetWindowPlacement`'s normal rectangle and whether it was
maximized), the layers panel width and the sample ring; `jpegQuality` and `theme` are what the file said.

## Startup application

`Main.cpp` loads the file before the window exists and hands the settings to `Application::UseSettings`; the window
takes a saved placement that still meets a monitor (`MonitorFromRect`) and opens maximized when it was; the chrome
palette, the sample ring and the layers width apply once the chrome exists.

## Command chords

Every finished command has one stable integer id, one stable text id and its default chords in the command catalog
(`RedPrism/CommandCatalog.h`; the table is in [`UI_ApplicationChrome.md`](../UI/UI_ApplicationChrome.md)).
`shortcuts` stores only the replacements. Each one is a RedSalamander binding: `commandId` is text
(`cmd/edit/swapPalette`, not the integer menu id), `ctrl` / `alt` / `shift` are booleans, and the key is either `vk`
or `keyPosition`. A key or a control still runs the integer command that `commandId` names. An absent or non-array
`shortcuts` field leaves every command on its catalog chords.

The application builds one effective binding table (`RedPrism/CommandBindings.h`, `Application::ApplySettings`)
whenever settings apply: the replacements in file order, then the catalog defaults of every command they do not
replace.

- An entry whose `commandId` names no command, whose key is a modifier key, or whose chord belongs to the window
  manager (Alt+F4, Alt+Space, F10 and Shift+F10: Decision D5) is ignored, and its command keeps its defaults.
- A later entry for a command replaces an earlier one. A later entry also takes its chord from an earlier entry of
  another command, which is then left without a chord.
- A replacement removes every default chord of its command, aliases included (Ctrl+Y for Redo; Ctrl+Shift+= and
  the numpad plus and minus of the zoom commands), and takes its chord from every other command's defaults.
- A shared label (Delete Layer's `Delete`, a key the Delete key's own command dispatches) stays only while a command
  still dispatches that chord.

The key path dispatches from that table and nothing else, with the command's scope, condition, Shift and repeat
rules applying to a replacement as to a default ([`UI_ApplicationChrome.md`](../UI/UI_ApplicationChrome.md), "Menus
and accelerators"). The menus, the Command List, the rail and palette tooltips and the brush header show its chords,
with the layout's key names.

## Validation

`IoTests` `settings.*`: the file names and default paths; parsing (a minimal document is the defaults, every field
reads, unusable values fall back, non-documents are rejected, shortcut names in any case, a byte order mark,
integral reals and the ±32767 range with INT_MIN); serialization (round trip, unknown fields and a newer schema
kept, the unread `shortcuts` entries and their unknown fields kept with the bindings unchanged and changed, an
unplaced window dropped, an invalid existing document ignored); the cold load of a missing file (template), a file
with a byte order mark (loaded), an invalid file (backup with the original bytes, template, defaults) and an
unreadable path; the live load of an invalid file (file untouched, nullopt), a valid one and a missing one; a save
over an invalid file (backup), keeping unknown fields, leaving no temporary and creating the directory. `HostTests`
`chords.*`, `commands.*` and `bindings.*` prove the key names and positions, the catalog (unique ids and text ids,
tool keys, only window-manager defaults reserved) and the binding table (defaults and aliases, a replacement
removing every default, the later entry winning, shadowing, the shared label, ignored entries, exact modifiers,
AltGr, the Shift and repeat rules, the scopes and conditions). The WARP self-test edits its own settings file and
proves the reload applies the theme, the sample ring and the layers width live, that a broken file changes nothing
and is not rewritten, that restoring it applies again, that a save records the window placement and that deleting
the watched directory leaves no watch armed; it applies replacement chords and drives them through the window
procedure (UI_ApplicationChrome.md "Validation").
