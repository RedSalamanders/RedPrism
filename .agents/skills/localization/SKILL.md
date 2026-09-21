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
- Menu captions keep a `\t` accelerator column (`"&Save\tCtrl+S"`); `StripAcceleratorColumn` yields the plain name.
- Tool names come from `EditorToolNameId(tool)`; tooltips use `IDS_TOOL_TOOLTIP` with the name and key.
- Never `MessageBoxW(..., L"literal", ...)` or `SetWindowTextW(..., L"literal")` in product code. Diagnostics that
  only reach `OutputDebugStringW` stay literal and English.
- Document data that the model persists or compares (default `Layer N` / `Folder N` names, blend mode names on
  disk) is not chrome; it stays in `Common/Document.cpp` exactly as the legacy app writes it.
- Tests load the executable's table with `LoadLibraryExW(..., LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE)`
  and pass that module to the helpers; nothing in `Tests/` runs `RedPrism.exe` code to read strings.

Satellite resource DLLs and language switching are C14 work (`ProductHardening_2026-09-20.md`); until then the
embedded English table is the only language, but the discipline above keeps it translatable.
