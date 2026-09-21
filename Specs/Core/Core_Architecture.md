# RedPrism architecture ownership

Status: current normative product contract
Last reviewed: 2026-09-20
Owner: `Application`, `Renderer`, Document (`Common/Document.*`), and DxUi adapters

## Scope

This contract owns process-wide ownership boundaries for the Windows composition. Window styles, DPI, and command-line
host behavior are in [`UI/UI_Windowing.md`](../UI/UI_Windowing.md). Idle, Present, and surface rules are in
[`Core/Core_PerformanceAndResources.md`](Core_PerformanceAndResources.md). The toolchain is in
[`Build/Build_Toolchain.md`](../Build/Build_Toolchain.md).

## Process shape

RedPrism is a single Win32 process (`RedPrism.exe`). v1 has no plugin ABI. DxUi C++ stays inside this executable when
C2 pins `DxUi.lib`. If a later plugin story appears, the ABI is COM/POD only — never DxUi types, STL, WIL, or
exceptions across a DLL boundary.

```text
Win32 top-level HWND (WS_EX_NOREDIRECTIONBITMAP)
  Application  : messages, DPI, files, commands, frame scheduling, CanvasRenderer, CanvasViewport
  Renderer     : D3D11 device, DXGI flip-model swap chain, Present, occlusion
  Document     : layers, history (never GPU device ownership)
  DxUi EmbeddedHost : retained chrome (borrows the app device; no swap chain)
```

There is one D3D11 device, one flip-model swap chain, and one `Present` per presented frame. `CanvasRenderer`
borrows that device. It MUST NOT own a swap chain.

## Ownership

| Owner | May own | MUST NOT own |
| --- | --- | --- |
| `Application` | Top-level `wil::unique_hwnd`, WndProc binding at `WM_NCCREATE`, display-power notify, command line, `SelectHostFrameAction` loop, `DocumentSession`, `CanvasRenderer`, `CanvasViewport`, the settings file and its directory watch ([`Core_Settings.md`](Core_Settings.md)), the theme, the UIA window root's placement | D3D device, swap chain, Present |
| `Renderer` | One `ID3D11Device`, one `ID3D11DeviceContext`, one `IDXGISwapChain1`, RTV, frame-latency waitable, occlusion registration | A second device, a chrome HWND, GDI paint, DxUi library lifetime, document rasters |
| Document | Layer graph, COW rasters, history budget | HWND, DXGI factory, DxUi host |
| DxUi EmbeddedHost | Retained chrome models and their GPU surfaces | Swap chain, renderer HWND, worker, periodic timer |

There is exactly one D3D11 device, one flip-model swap chain, and one `Present` per frame that actually presents.

## Lifetime and RAII

- Bind `Application*` at `WM_NCCREATE` through `GWLP_USERDATA`. Release that pointer at `WM_NCDESTROY`.
- Destroy the HWND with `wil::unique_hwnd::reset()`. After `WM_NCDESTROY`, `release()` so the wrapper does not
  `DestroyWindow` a dead handle.
- Owned COM objects are `wil::com_ptr_nothrow<T>` assigned with `.put()`.
- `wWinMain` and `WndProc` MUST NOT let exceptions escape. `catch (...)` is forbidden. `std::bad_alloc` is fatal.

## Frame loop

`Application::Run` drains the message queue, then calls `SelectHostFrameAction` (`RedPrism/FrameScheduler.h`):

| State | Action |
| --- | --- |
| Hidden, display-off, or swap chain suspended (zero client size) | Block on the message queue |
| Occluded, no occlusion-status message | Block on the message queue |
| Occluded, occlusion-status message received | `Present(0, DXGI_PRESENT_TEST)` then re-evaluate |
| Visible, powered, not occluded, not suspended, frame invalidated | Wait on the frame-latency object together with the queue, then `Renderer::Render` |
| Visible and settled (not invalidated) | Block on the message queue |

`Renderer::Render` is the only Present path. `WM_PAINT` MAY set the invalidation bit and MUST `ValidateRect`; it MUST
NOT call `Present`, `BeginPaint` text, or GDI fill.

## Device placement

Hardware device creation uses adapter-of-monitor (`RedPrism/AdapterSelection.h`): the first non-software DXGI adapter
that owns an output whose `HMONITOR` is the window's monitor. `--warp` and `--self-test` force WARP and skip that
search. If no matching output exists, creation MAY use `D3D_DRIVER_TYPE_HARDWARE`. Hardware init failure MAY fall back
to WARP unless `--warp` was already requested and failed.

When the window moves to a monitor scanned out by another adapter, `Renderer::EnsureDeviceForWindowMonitor` rebuilds
device resources. WARP devices do not follow monitors.

## Command catalog

`RedPrism/CommandLine.h` is the single declaration of switches. Help, parse, and tests MUST read that table. Adding,
renaming, or removing a switch changes the header, [`UI/UI_Windowing.md`](../UI/UI_Windowing.md), and `HostTests` in
the same change.

## Document and chrome

C4 owns `CanvasDocument` / `ImageLayer` / `DocumentHistory` as C++ value types with shared immutable `Raster`
storage. `Application` owns `CanvasRenderer` (borrowed device). DxUi `EmbeddedHost` composites chrome into the same
back buffer; the composition then blits the document into the chrome hole. Those types MUST NOT own the swap chain.

## Validation

`Tests/HostTests` covers adapter selection, frame-action selection, and the command catalog without creating a device.
`Tests/DocumentTests` covers the document graph, validation, and history. `Tests/IoTests` covers `.prism` load/save.
`Tests/CompositionTests` covers WARP blend goldens. `RedPrism.exe --self-test --warp` covers HWND + WARP Present. See
[`Testing/Testing_Validation.md`](../Testing/Testing_Validation.md).
