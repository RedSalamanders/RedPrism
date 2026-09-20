---
name: consumer-dxui
description: Add or update the pinned DxUi dependency, MSBuild imports, and RedPrism theme or host adapters. Use from C2 onward; not for composition canvas work.
---

# DxUi consumer integration

Read `Specs/Core/Core_DxUiIntegration.md` once it exists (C2). Until then follow the port plan and this skill; do
not float onto sibling `main`.

- Lock `Dependencies/DxUi.lock.json` with repository `https://github.com/RedSalamanders/DxUi`, a 40-character
  commit, `apiRevision` 2, and `targets: ["DxUi"]`. Initial pin:
  `f5c7fc7403e352425b8e82b7a4da1c042d942ede`.
- Restore into an isolated `.build/dependencies/DxUi/` fingerprint. Do not checkout, reset, or edit `Z:\src\DxUi`
  from RedPrism restore.
- Import consumer props/targets and reference `src/DxUi.vcxproj` once. Do not enumerate library `.cpp` files.
- Match CRT (`/MDd` Debug and ASan Debug, `/MD` Release), toolset, architecture, and headers.
- The application owns the D3D11 device, swap chain, and Present. `EmbeddedHost` borrows the device.
- Keep DxUi C++ inside `RedPrism.exe`. Never pass DxUi types, STL, WIL, or exceptions across a plugin ABI.
- Product theme adapters stay in RedPrism. Splitter, ColorPicker, and NumericStepper are DxUi library work owned by
  `Z:\src\DxUi\Specs\Plans\WIP\EditorConsumerControls_2026-09-20.md`.
- Library `test.ps1` does not replace RedPrism product tests. Bump the pin only after the library commit is green.
