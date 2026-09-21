# RedPrism C++ development guidelines

RedPrism is a Windows-native raster composition ported from the Windows raster editor under archived behavioral reference. It is built
with Win32, Direct3D 11, DXGI, DirectWrite, WIL, yyjson, libzip, DxUi, and modern C++. archived behavioral reference is a behavioral
reference only and is never a build input.

## Requirements

- Windows 11 build 22000.2600 minimum
- Visual Studio 2026 (18.x), toolset `v145`, Desktop development with C++
- Windows 11 SDK 10.0.26100
- MSVC `stdcpplatest`, Unicode Win32 APIs
- Debug, Release, and ASan Debug on x64 and ARM64, including real sanitizer instrumentation
- Git for the pinned vcpkg bootstrap
- Package manager: vcpkg (`vcpkg.json` + `vcpkg-tool.json`)
- Graphics: Direct3D 11, DXGI, DirectWrite, DxUi (from C2). No GDI text. No chrome HWND over the swap chain.

## Canonical guidance

- This file is the repository-wide instruction source.
- `Specs/README.md` defines specification authority and the plan lifecycle.
- Normative product behavior lives in `Specs/<Domain>/`. Read the owning contract before changing behavior.
- Active work lives in `Specs/Plans/WIP/` and is indexed in `Specs/Plans/WIP/README.md`.
- Repo skills live in `.agents/skills/` and must pass `.\validate-skills.ps1`.
- Generated products belong under `.build/`; never commit them.
- Preserve the documented blend math, project schema and independent numeric goldens in the owning specs.

## Skills

| Skill | Use it for |
| --- | --- |
| [spec-workflow](.agents/skills/spec-workflow/SKILL.md) | Domain specs, WIP/Done lifecycle |
| [build-redprism](.agents/skills/build-redprism/SKILL.md) | `build.ps1`, `test.ps1`, configurations, outputs |
| [test-harness](.agents/skills/test-harness/SKILL.md) | In-process tests, goldens, WARP fixtures, harness screenshots |
| [direct3d11-rendering](.agents/skills/direct3d11-rendering/SKILL.md) | Device, swap chain, Present, WARP, device loss |
| [hlsl-composition](.agents/skills/hlsl-composition/SKILL.md) | Document-space passes, coverage, clipping stacks, build-time HLSL |
| [win32-windowing](.agents/skills/win32-windowing/SKILL.md) | HWND, WndProc, DPI, message loop |
| [performance-resources](.agents/skills/performance-resources/SKILL.md) | Idle, allocation, GPU submit, bounded storage |
| [wil-raii](.agents/skills/wil-raii/SKILL.md) | WIL handles and COM |
| [yyjson](.agents/skills/yyjson/SKILL.md) | JSON ownership, Swift Codable shapes |
| [modern-cpp-windows](.agents/skills/modern-cpp-windows/SKILL.md) | Ownership, HRESULT, warnings, style |
| [localization](.agents/skills/localization/SKILL.md) | STRINGTABLE, positional placeholders, `Common/Resources.h` |
| [numeric-goldens](.agents/skills/numeric-goldens/SKILL.md) | Independent numeric expectations, fixture formulas and documented tolerances |
| [settings-store](.agents/skills/settings-store/SKILL.md) | Future settings paths and recovery (C14) |
| [consumer-dxui](.agents/skills/consumer-dxui/SKILL.md) | Exact DxUi pin, pinned-library facts, pin bumps |

## Architecture ownership

| Owner | Responsibility |
| --- | --- |
| `Application` | HWND, messages, DPI, command line, frame scheduling, files, commands, `DocumentSession`, `CanvasViewport`, `CanvasRenderer` |
| `Renderer` | D3D11 device, swap chain, Present, occlusion, adapter-of-monitor, frame order (chrome layout → canvas → chrome) |
| `CanvasRenderer` | Document-space GPU passes, coverage textures, clipping stacks, raster residency; borrows the device |
| `ChromeHost` | DxUi EmbeddedHost shell: menus, rail with palette wells, tabs, Move-tool header, layers with splitter and opacity stepper, status, color picker card, canvas overlay; strings from resources |
| Document (`Common/Document.*`, `DocumentHistory`, `TransformEdit`, `ProjectStore`, `ImageCodecs`) | Layers, validation, history, transform editing (drag geometry, snapping, warp), `.prism` I/O, WIC — never GPU device ownership |
| DxUi EmbeddedHost | Retained chrome; borrows the app device; no swap chain |

## Mandatory rules

- WIL RAII for owned Windows and COM resources. No manual `Release` / `DestroyWindow` of owned objects.
  Destroy HWNDs with `wil::unique_hwnd::reset()`.
- `wil::com_ptr_nothrow<T>` in HRESULT code. Assign with `.put()`. Never `AddRef` then `attach`.
- No exceptions across `wWinMain`, `WndProc`, or COM. `catch (...)` is forbidden. `std::bad_alloc` is never
  swallowed: a `noexcept` construction boundary may translate it to `E_OUTOFMEMORY` only where the caller then
  fails the operation or exits; hot paths do not catch at all.
- Unicode `stdcpplatest`, `/W4`, `/permissive-`, SDL, warnings as errors, `/utf-8`. Headers that are not Win32
  headers do not depend on `windows.h` typedefs (`UINT`, `DWORD`); use fixed-width or plain types.
- One D3D11 device, one flip-model swap chain, one Present. Do not render from `WM_PAINT`.
- Idle, hidden, minimized, suspended, and occluded work blocks on events. Busy polling is prohibited.
- Steady-state hot paths use bounded reusable storage and avoid heap allocation. The composition composites only
  when the document changed; a settled document costs zero passes per frame.
- Every user-visible chrome string is a `STRINGTABLE` entry with positional `{0}` placeholders
  (`localization` skill). Document data such as default layer names is not chrome.
- Caches keyed by `Raster::Identity()` hold the `Raster`; a freed storage address may be reused by another image.
- Screenshots for specs and docs come from the application harness. Never Computer Use or a desktop screenshot tool.
- `build.ps1` rejects a running `RedPrism.exe` whose path is the selected output and MUST NOT terminate it.
- Consume DxUi only as pinned `DxUi.lib` (API revision 2). Do not enumerate library sources. Do not reset
  `Z:\src\DxUi` from restore. Design against the pinned library as it is (see `consumer-dxui` for the facts that
  matter: `Toolbar` has no layout, `Toggle` is a switch, icon-only buttons draw with the icon font).
- Keep DxUi C++ inside `RedPrism.exe`. If a plugin ABI appears later, it is COM/POD only.
- Every new behavior ships with its golden: a numeric expectation taken from retained numeric golden cases, never a
  screenshot comparison or "looks fine".

## Performance

`Specs/Core/Core_PerformanceAndResources.md` applies to every design. Among correct designs, prefer the lowest
steady-state CPU, memory, allocation, copy, synchronization, wake-up, and GPU-submission cost. Intentional
regressions need measurement and a spec or WIP justification.

## Validation before declaring a change complete

```powershell
.\validate-skills.ps1
.\format.ps1 -Check
.\test.ps1 -Platform x64 -Configuration Debug
.\test.ps1 -Platform x64 -Configuration Release
.\test.ps1 -Platform x64 -Configuration "ASan Debug"
```

ARM64 builds cross-compile with `build.ps1 -Platform ARM64`; runtime claims need a native ARM64 host.
