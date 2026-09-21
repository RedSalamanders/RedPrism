# RedPrism performance and resource contract

Status: current normative product contract
Last reviewed: 2026-09-20
Owner: `Application` frame loop, `Renderer`, `CanvasRenderer`, and DxUi adapters

## Mandate

Performance and low resource consumption are primary RedPrism requirements, not optional cleanup. Every design,
implementation, review, and validation change MUST minimize CPU time, memory, allocations, copies, synchronization,
GPU submissions, wake-ups, and loaded resources while preserving correctness.

When several correct designs exist, use the one with the lowest steady-state resource cost unless a measured result or
a materially simpler and safer implementation justifies another choice. An intentional regression MUST be measured in
the affected configuration and justified in the owning contract or an active WIP plan.

"Zero CPU while idle" means RedPrism owns no polling loop or periodic wake-up when no frame, input, I/O completion, or
state change is pending. Operating-system scheduling noise is outside this contract.

## Mandatory runtime rules (C1 host)

- Idle, hidden, minimized, suspended, display-off, and occluded execution MUST block on events. Busy polling is
  prohibited. The shell MUST NOT arm a periodic timer to keep the window alive.
- Steady-state Present of an already-created swap chain MUST NOT heap-allocate. C1's clear-color frame uses the
  existing device, context, RTV, and waitable handle only.
- `Renderer::Render` MUST NOT run from `WM_PAINT`.
- Hidden, minimized, or zero-size client: no `Present`, no render-target create. The swap chain MAY remain allocated
  at the last non-zero size until a later resize; a zero-size `WM_SIZE` suspends Present and MUST NOT call
  `ResizeBuffers` to 0×0 as a way to keep presenting. Hidden documents MUST drop canvas GPU surfaces
  (`CanvasRenderer::CanvasSurfaceBytes() == 0`).
- After `Present` reports `DXGI_STATUS_OCCLUDED`, stop content presents, wait for the DXGI factory occlusion-status
  window message, and recover with `DXGI_PRESENT_TEST` only. Occlusion polling is prohibited.
- The swap chain MUST be created with `DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT` and maximum frame latency 1.
  `ResizeBuffers` keeps that flag. Before building a frame the UI thread waits on the waitable together with the
  message queue so `Present` does not block the UI thread. Settled idle waits on the message queue alone.
- Adapter of output: create the hardware device on the adapter whose output owns the window's monitor, never the
  default adapter while a matching output exists. Presentation MUST NOT cross adapters through DWM. Forced WARP never
  re-checks. Selection is a pure function (`SelectAdapterRecordForMonitor`) so HostTests can prove it without a
  topology.
- The top-level window is `WS_EX_NOREDIRECTIONBITMAP`. Nothing paints it with GDI, so DWM keeps no client-sized
  redirection bitmap for chrome. `gdi32` may be linked for Win32; GDI text and GDI fill of the swap-chain window are
  prohibited.
- Derived display state (DPI, client size, adapter LUID) is recomputed when its inputs change, not per idle wake.

## Budgets that later phases inherit

These numbers are binding as soon as the feature exists. C1 does not implement them; it MUST NOT choose a host shape
that makes them impossible.

| Budget | Limit |
| --- | --- |
| History retained bytes (C4) | ≤ 256 MiB, 100 entries, shared immutable rasters |
| Document pixels | 30 000 px/side; 100 M image pixels + 100 M mask pixels; ≤ 10 000 layers |
| Manifest / asset | 4 MiB manifest; 512 MiB per asset |
| Visible DxUi surfaces (C2) | Library cap (64 MiB visible, 0 when hidden) |
| Hidden document GPU (C6) | 0 canvas surface bytes; no Prepare/Composite |
| Composition (C6) | Only when the document changed or surfaces were rebuilt; a settled document composites 0 times per frame (`--self-test` proves it) |
| GPU raster residency (C6) | Bounded to the rasters the last composition used; entries keep their `Raster` alive so identities never alias |
| Compose/Present after warmup (C6) | No heap on the blit/Present path; composition may allocate transient CPU bookkeeping only when the document changed |
| Dirty DxUi prep (C2) | Stay within DxUi Release ≤ 64 allocs/frame |
| Brush (C10) | Tile 256²; mouse-up MUST NOT materialize a contiguous bitmap |

Fail-closed performance sandboxes live at `X:\RedPrism.Perf` (any local drive letter plus an ownership marker), never
under repository `.build/`. C1 does not yet create that tree.

## Validation

`HostTests` asserts idle vs render vs occlusion-probe selection. `RedPrism.exe --self-test --warp` presents exactly one
hidden frame and exits. Interactive idle is qualified by running the titled window: a settled visible window MUST not
spin the CPU in `Renderer::Render`.
