---
name: modern-cpp-windows
description: Apply RedPrism modern C++ ownership, error-handling, warnings, Unicode, and formatting conventions when writing or reviewing C++ project code.
---

# Modern C++ for Windows

- Compile as Unicode `stdcpplatest` with `/W4`, `/permissive-`, SDL checks, and warnings as errors.
- Prefer value types and deterministic RAII. Use `wil::com_ptr_nothrow<T>` for COM in HRESULT code, WIL unique
  wrappers for Windows resources, and standard smart pointers for heap ownership.
- Delete copying for unique resource owners. Make cleanup idempotent so partial initialization remains safe.
- Use `HRESULT` for Windows and DirectX operations. Capture `GetLastError()` immediately after a failing Win32 call.
- Do not throw through `wWinMain`, `WndProc`, or other ABI callbacks. `catch (...)` is forbidden.
- `std::optional` is accessed with `.value()` / `.value_or()`, never `*`.
- Naming: PascalCase types and public methods, camelCase locals, `_` members, `kConstants`.
- No raw `new`/`delete`. `new (std::nothrow)` is allowed only at the `Application` allocation boundary in `Main.cpp`.
- Format C++ with `.\format.ps1`; do not hand-reformat unrelated files.
- Preserve UTF-8 source files while calling wide-character Win32 APIs.
