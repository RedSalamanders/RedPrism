---
name: direct3d11-rendering
description: Implement or debug RedPrism Direct3D 11 rendering, including devices, DXGI swap chains, resize, presentation, WARP, and device loss. Do not use for unrelated Win32 message handling.
---

# Direct3D 11 rendering

Read `Specs/UI/UI_Windowing.md` and `Specs/Core/Core_PerformanceAndResources.md` for every rendering change. Keep host
graphics ownership inside `RedPrism/Renderer.*`.

- Own COM with `wil::com_ptr_nothrow`. Apply the `wil-raii` skill.
- Keep device resources and size-dependent resources distinct. A resize replaces the render target; device loss
  rebuilds the device, swap chain, and target as one operation.
- Create the device on the adapter whose output owns the window's monitor (`AdapterSelection.h`,
  `Renderer::FindAdapterForMonitor`). A window on no monitor or `--warp` uses the default/WARP path. Rebuild when
  the window moves to another adapter.
- Two-buffer flip-model swap chain with `DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT`, maximum latency 1.
  The UI thread waits on that object together with the message queue before building a frame. Pass the same flag to
  `ResizeBuffers`. Keep `DXGI_MWA_NO_ALT_ENTER` unless an explicit fullscreen policy is added.
- Zero width or height suspends rendering.
- Handle `DXGI_ERROR_DEVICE_REMOVED`, `DXGI_ERROR_DEVICE_RESET`, and `DXGI_ERROR_DRIVER_INTERNAL_ERROR` by recreating
  resources. Do not log handled occlusion as an error.
- Register a DXGI factory occlusion-status window message. After `DXGI_STATUS_OCCLUDED`, stop frames, wait for that
  message, and probe with `Present(0, DXGI_PRESENT_TEST)`. Do not poll.
- Create the device with `D3D11_CREATE_DEVICE_BGRA_SUPPORT` so DxUi can borrow it from C2.
- Query `ID3D11DeviceContext1` at device creation.
- Compile fixed HLSL at build time when shaders exist. Do not add runtime `d3dcompiler` for fixed shaders.
- Preserve `--warp`. Rendering happens on the idle side of the UI loop. Do not draw from `WM_PAINT`.
