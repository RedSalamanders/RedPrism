---
name: settings-store
description: Implement or revise RedPrism settings (the JSON document under %LocalAppData%\RedPrism, its schema, cold recovery, atomic saves and the UI-thread live reload). Use when a setting is added, its range changes, or how the app applies it changes.
---

# RedPrism settings store

Read `Specs/Core/Core_Settings.md` first; it is the contract. The store is `Common/SettingsStore.*`, the
application side `RedPrism/ApplicationSettings.cpp`, the startup load `RedPrism/Main.cpp`.

- One JSON object; `schema` (integer ≥ 1) carries compatibility, the file name only tells Debug
  (`settings.debug.json`) from Release (`settings.json`). Never encode a version in the name.
- Adding a setting: a field in `AppSettings` with its default, a `Parse` branch that validates or clamps it (an
  unusable value takes the default, never the whole document), a `Serialize` `put`, a row in the spec's field
  table, an `IoTests` `settings.*` case for the field and its fallback, and the application side (`ApplySettings`
  for a live setting, the start-only path for the rest). Unknown fields must keep surviving a save.
- Cold invalid: back up the bytes (`<path>.invalid-<stamp>`) and write the template. Live invalid: keep the file,
  keep the last-good settings, say so on the debugger output, write nothing. A live load must never write the
  watched file.
- Watching is one `FindFirstChangeNotification` on the directory waited beside the message queue
  (`MsgWaitForMultipleObjectsEx`), coalesced by a 250 ms timer; parse and apply only on the UI thread. No polling,
  no worker thread.
- The app's own save calls `NoteSettingsFileState` so it does not reload itself; saves are atomic (temporary file,
  `MoveFileExW` replace, write-through).
- `--self-test`, `--screenshot`, `--demo`, `--bench` and `--crash-probe` never read or write the user's file; the
  self-test proves the live reload from a file under `%TEMP%`.
- Use yyjson with the `yyjson` skill (`yyjson_mut_obj_put` replaces a key; `yyjson_doc_mut_copy` keeps the rest).
