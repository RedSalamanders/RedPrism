---
name: localization
description: Add, change, or load user-visible RedPrism chrome strings through the STRINGTABLE and Common/Resources.h. Use when adding menus, tooltips, status text, dialog filters, or error messages; not for document data such as layer names.
---

# Localization

Read `Specs/UI/UI_ApplicationChrome.md` (Strings) before adding chrome text.

- Every user-visible chrome string is a `STRINGTABLE` entry: declare the `IDS_*` id in `RedPrism/resource.h`,
  define the text in `RedPrism/RedPrism.rc` (`#pragma code_page(65001)`, UTF-8 source), and add the id to the
  `kIds` table in `Tests/HostTests/HostTests.cpp` so the string-table gate proves it resolves.
- Load with `LoadStringResource(nullptr, IDS_X)`; format with `FormatStringResource(nullptr, IDS_X, args...)`.
  Placeholders are positional `std::format` specs (`{0}`, `{1:08X}`) so translators can reorder arguments. Bare `{}`
  and printf specs (`%s`) are forbidden; `HostTests` fails on them.
- Introduce placeholders in argument order (`{0}` then `{1}`) and pass arguments in that order.
- Menu captions may keep a `\t` column in the table, but menus show the effective chord: `CaptionWithoutChord`
  drops the column and `PlainCommandName` yields the plain name (both in `Common/Resources.h`). Chord text is built
  from `IDS_KEY_*` modifier names and the keyboard layout's key names, never from literals.
- Tool names come from `EditorToolNameId(tool)`; tooltips use `IDS_TOOL_TOOLTIP` with the name and the effective
  chord (no key is baked into a string).
- Never `MessageBoxW(..., L"literal", ...)` or `SetWindowTextW(..., L"literal")` in product code. Diagnostics that
  only reach `OutputDebugStringW` stay literal and English.
- Document data that the model persists or compares (default `Layer N` / `Folder N` names, blend mode names on
  disk) is not chrome; it stays in `Common/Document.cpp` exactly as the legacy app writes it.
- Tests load the executable's table with `LoadLibraryExW(..., LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE)`
  and pass that module to the helpers; nothing in `Tests/` runs `RedPrism.exe` code to read strings.

Languages (C14): a translated table is a resource-only DLL built from the same `.rc` and ids, placed at
`<exe dir>\Languages\<tag>\RedPrism.resources.dll`. `Common/Localization.*` picks the candidates (the `language`
setting's tag, its primary subtag, then Windows' display languages and theirs) and `SetStringResourceSatellite`
makes `LoadStringResource(nullptr, id)` read it first, falling back to the embedded English for a missing id. No
translation ships in v1; keep the ids stable so one can. The harness switches stay English; tests prove the
candidate order and the override in `HostTests` `locale.*`.

Accessible names are chrome strings too: every interactive DxUi control gets `SetAccessibleName` from the same
table (a label id, a tooltip id or an `IDS_A11Y_*` id), and the self-test fails on a control without one.
