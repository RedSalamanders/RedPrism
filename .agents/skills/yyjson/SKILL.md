---
name: yyjson
description: Use yyjson safely in RedPrism for parsing and serializing JSON. Apply when changing settings, project manifests, or reviewing yyjson ownership and string lifetimes.
---

# yyjson

Use yyjson from `vcpkg.json`; do not vendor its source. Apply `wil-raii` to document and write-buffer ownership.

- A `yyjson_doc*` or `yyjson_mut_doc*` owns all values and borrowed strings obtained from it. Never retain a
  `yyjson_val*` or `yyjson_get_str()` result after the document is freed.
- Own documents unconditionally with WIL:

```cpp
using unique_yyjson_doc = wil::unique_any<yyjson_doc*, decltype(&yyjson_doc_free), yyjson_doc_free>;
using unique_yyjson_mut_doc = wil::unique_any<yyjson_mut_doc*, decltype(&yyjson_mut_doc_free), yyjson_mut_doc_free>;
```

- Pass a mutable buffer to `yyjson_read_opts()`, capture `yyjson_read_err`, check the root and every value's type,
  and reject invalid ranges before narrowing numeric values.
- Convenience `yyjson_mut_obj_add_*` functions borrow their keys. Use `*_strcpy` / `*_strncpy` for temporary strings.
- For a dynamic key, allocate both key and value as document-owned `yyjson_mut_val*` values and call
  `yyjson_mut_obj_add()`.

Project manifests (C5) and settings (C14) must follow this ownership. Do not introduce a second JSON library.
