---
name: modern-cpp-windows
description: Apply RedPrism modern C++ ownership, error-handling, warnings, Unicode, header hygiene, and formatting conventions when writing or reviewing C++ project code.
---

# Modern C++ for Windows

- Compile as Unicode `stdcpplatest` with `/Wall`, `/permissive-`, SDL checks, /WX disabled, and zero-warning build validation.
- Prefer value types and deterministic RAII. Use `wil::com_ptr_nothrow<T>` for COM in HRESULT code, WIL unique
  wrappers for Windows resources, and standard smart pointers for heap ownership.
- Delete copying for unique resource owners. Make cleanup idempotent so partial initialization remains safe.
- Use `HRESULT` for Windows and DirectX operations. Capture `GetLastError()` immediately after a failing Win32 call.
- Do not throw through `wWinMain`, `WndProc`, or other ABI callbacks. `catch (...)` is forbidden; catch only named
  types at a `noexcept` boundary with a comment saying why. `std::bad_alloc` is never swallowed: translate it to
  `E_OUTOFMEMORY` only where the caller then fails the whole operation.
- `std::optional` is accessed with `.value()` / `.value_or()`, never `*`.
- No C-style casts; `static_cast` / `reinterpret_cast` with intent. No raw `new`/`delete`; `new (std::nothrow)` is
  allowed only at the `Application` allocation boundary in `Main.cpp`.
- Headers that are not Win32 wrappers (`EditorTool.h`, `Document.h`, `CommandLine.h`) must not rely on
  `windows.h` typedefs such as `UINT` or `DWORD`; use `unsigned int`, `std::uint32_t`, or `std::size_t` so any test
  can include them first.
- Formatting into fixed buffers uses `std::format_to_n`, never `swprintf_s`. User-visible text comes from the
  STRINGTABLE (`localization` skill), never a literal.
- Naming: PascalCase types and public methods, camelCase locals, `_` members, `kConstants`. One declaration per line.
- Format C++ with `.\format.ps1` before every build you intend to keep; `format.ps1 -Check` is a closeout gate.
- Preserve UTF-8 source files (`.rc` files carry `#pragma code_page(65001)`) while calling wide-character Win32 APIs.
- A cache keyed by a pointer identity (`Raster::Identity()`, COM pointers) must own a reference to the object that
  keeps the address stable; otherwise a freed address aliases a new object.
