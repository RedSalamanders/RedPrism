---
name: wil-raii
description: Use WIL RAII for RedPrism Windows handles, COM interfaces, and cleanup. Apply when creating, owning, transferring, or releasing HWND, COM objects, or temporary cleanup actions.
---

# WIL RAII

Use the WIL package from `vcpkg.json`. Do not add a second WIL copy or return to WRL.

- Use `wil::com_ptr_nothrow<T>` in `noexcept`/`HRESULT` code, `.put()` for output parameters, `.get()` for borrowed
  access, and `.reset()` for release. Never call `Release()` manually.
- Use `wil::unique_hwnd` and matching wrappers for owned Windows resources.
- Destroy an owned HWND with `owner.reset()`, not `DestroyWindow(owner.get())`. Use `.release()` only to clear an
  already-destroyed HWND during `WM_NCDESTROY`.
- Use `wil::scope_exit` for temporary cleanup. Do not add `goto` cleanup paths.
- Keep every resource under one explicit owner immediately after successful creation.

Use scoped WIL include annotations to keep external diagnostics out of the zero-warning build gate; `/WX` remains disabled:

```cpp
#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/com.h>
#include <wil/resource.h>
#pragma warning(pop)
```
