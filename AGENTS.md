# RedPrism C++ development guidelines

RedPrism is a Windows-native raster editing application built with Win32, Direct3D 11, DXGI, DirectWrite,
WIL, yyjson, libzip, Clipper2, DxUi, and modern C++.

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
| [build-redprism](.agents/skills/build-redprism/SKILL.md) | `build.ps1`, `test.ps1`, `package.ps1`, configurations, outputs |
| [test-harness](.agents/skills/test-harness/SKILL.md) | In-process tests, goldens, WARP fixtures, harness screenshots |
| [direct3d11-rendering](.agents/skills/direct3d11-rendering/SKILL.md) | Device, swap chain, Present, WARP, device loss |
| [hlsl-composition](.agents/skills/hlsl-composition/SKILL.md) | Document-space passes, coverage, clipping stacks, the brush compute shader, build-time HLSL |
| [win32-windowing](.agents/skills/win32-windowing/SKILL.md) | HWND, WndProc, DPI, message loop |
| [performance-resources](.agents/skills/performance-resources/SKILL.md) | Idle, allocation, GPU submit, bounded storage |
| [wil-raii](.agents/skills/wil-raii/SKILL.md) | WIL handles and COM |
| [yyjson](.agents/skills/yyjson/SKILL.md) | JSON ownership, Swift Codable shapes |
| [modern-cpp-windows](.agents/skills/modern-cpp-windows/SKILL.md) | Ownership, HRESULT, warnings, style |
| [localization](.agents/skills/localization/SKILL.md) | STRINGTABLE, positional placeholders, `Common/Resources.h` |
| [numeric-goldens](.agents/skills/numeric-goldens/SKILL.md) | Independent numeric expectations, fixture formulas and documented tolerances |
| [settings-store](.agents/skills/settings-store/SKILL.md) | The settings document, its schema, cold recovery, atomic saves, UI-thread live reload |
| [crash-handling](.agents/skills/crash-handling/SKILL.md) | Local minidumps, the pending marker and prompt, the `--crash-probe` gate |
| [consumer-dxui](.agents/skills/consumer-dxui/SKILL.md) | Exact DxUi pin, pinned-library facts, pin bumps |

## Architecture ownership

| Owner | Responsibility |
| --- | --- |
| `Application` | HWND, messages, DPI, command line, frame scheduling, files, commands, `DocumentSession`, `JobScheduler`, `CanvasViewport`, `CanvasRenderer` |
| `Renderer` | D3D11 device, swap chain, Present, occlusion, adapter-of-monitor, frame order (chrome layout → canvas → chrome) |
| `CanvasRenderer` | Document-space GPU passes, coverage textures, clipping stacks, raster residency, sparse-raster assembly and the brush stroke preview; borrows the device |
| `GpuBrushCoverage` | The brush compute engine (`BrushCoverage.hlsl`) on the application device: per-tile density and preview buffers, readback of the preview only |
| `ChromeHost` | DxUi EmbeddedHost shell: menus, rail with palette wells, tabs, Move-tool, selection-tool, brush, Gradient and Shape headers, the adjustment panel card with its drawn curve, levels, spectrum and ramp controls (`AdjustControls.*`, `ChromeHostAdjust.cpp`), layers with splitter and opacity stepper, status, color picker card, canvas overlay (transform box, marching ants, lasso draft, brush tip, gradient line, shape draft, sample ring, clone source); strings from resources |
| Document (`Common/Document.*`, `DocumentHistory`, `TransformEdit`, `Selection`, `BrushStroke`, `WarpStroke`, `Heal`, `ImageFilters`, `Adjust`, `AdjustEdit`, `ProjectStore`, `ImageCodecs`) | Layers, validation, history, sparse rasters, transform editing (drag geometry, snapping, warp), selection (Clipper2 booleans, coverage, wand and mask tracing), brush strokes (tiles, CPU kernel, snapshots), retouch (the spot healing kernel, clone and blur samples, warps, gradients, shapes), pixel edits (lifted pixels, invert, masks from the selection, the floating selection transform, the clipboard model), color adjustments and filters (the levels, curves, exposure, gradient map, grain, hue/saturation cube, noise, lens and motion blur kernels, the panel sessions and adjustment layer editing), `.prism` I/O, WIC — never GPU device ownership |
| DxUi EmbeddedHost | Retained chrome; borrows the app device; no swap chain |
| `JobScheduler` (`Common/Jobs.*`), owned by `Application` | Bounded job records, class/priority scheduling, cancellation, ordered completion delivery and UI actions; standard-only public header with a private implementation; no document mutation or GPU ownership |
| `Executor`, `WorkerPool`, `WaitPool` | Standard C++ kernel execution interface; private bounded Windows compute pool and separate event-wait pool, cancellation, callback/COM lifetime; no document mutation or GPU ownership |

## Mandatory rules

- WIL RAII for owned Windows and COM resources. No manual `Release` / `DestroyWindow` of owned objects.
  Destroy HWNDs with `wil::unique_hwnd::reset()`.
- `wil::com_ptr_nothrow<T>` in HRESULT code. Assign with `.put()`. Never `AddRef` then `attach`.
- No exceptions across `wWinMain`, `WndProc`, or COM. `catch (...)` is forbidden. The `JobScheduler` worker runner
  catches only `std::bad_alloc`, translating it to a failed job with `E_OUTOFMEMORY`. Other allocation failures
  remain fatal except at explicit `noexcept` construction/admission boundaries that return `E_OUTOFMEMORY` only
  when the caller then fails the operation or exits. Hot paths do not catch.
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
- Every behavior ships with an independent numeric golden and a documented tolerance, never a screenshot
  comparison or "looks fine". New behavior with no legacy counterpart uses an independent reference
  named by its owning spec (for example, state-machine expectations for worker jobs and crash handling). Existing
  legacy goldens are never rebaselined to match an implementation.

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
