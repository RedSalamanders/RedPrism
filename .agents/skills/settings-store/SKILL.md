---
name: settings-store
description: Implement or revise RedPrism settings paths, Debug and Release settings files, schemas, cold recovery, and UI-thread live reload. Use when settings files, schemas, or runtime settings application change.
---

# RedPrism settings store

Settings land in C14. Until that spec exists, do not invent an ad hoc configuration file. When adding settings,
create `Specs/Core/Core_Settings.md` in the same change and follow this contract.

- Normal user files live under `%LocalAppData%\RedPrism`. Debug and Release use distinct filenames. Schema
  compatibility comes from the document, never the filename.
- Cold invalid file: backup the bytes and install the template. Live invalid file: keep the file and last-good
  runtime.
- Watch with events; coalesce; parse and apply only on the UI thread. Do not poll. A live load must not write the
  watched file.
- Use yyjson with the `yyjson` skill. `--self-test` must not write the user settings directory.
