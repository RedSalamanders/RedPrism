# Canvas composition

Status: current normative product contract
Last reviewed: 2026-09-20
Owner: `RedPrism/CanvasRenderer.*`, `RedPrism/CanvasViewport.h`, HLSL under `RedPrism/Shaders/`

## Scope

This contract owns document-space GPU compositing and the view transform. The swap chain and Present stay in
`Renderer`. DxUi chrome is composited first; the canvas is then blitted into the chrome hole with pixels outside the
hole discarded.

## Viewport

`CanvasViewport` maps the canvas hole (pixels) to document space. Opening a document fits the canvas (`followsFit`).
Zoom is clamped to **0.001–32**. Pixel grid draws when zoom ≥ **8**. Wheel zoom is around the cursor; Space or the
Hand tool pans. Pointer mapping uses hole-relative view coordinates and the window DPI (`backingScale = dpi/96`).

Sharp downsample when zoomed out uses mipmapped layer textures (`GenerateMips` when the device allows; otherwise a
single-level fallback). Nearest sampling follows `LayerSampling::Nearest`.

## GPU path

One borrowed `ID3D11Device` / immediate context from `Renderer`. Ping-pong BGRA8 (`DXGI_FORMAT_B8G8R8A8_UNORM`)
canvas textures, fullscreen triangle, `Map` `DISCARD` constants after warmup. Texture cache is keyed by
`Raster::Identity()`. Hidden, minimized, or occluded documents MUST drop canvas surfaces (`CanvasSurfaceBytes() == 0`)
and MUST NOT Present. Device loss rebuilds shaders, ping-pong, and the cache.

Blend math is HLSL (PDF Color Dodge / Color Burn), not Core Graphics. Modes 1–8: Multiply, Screen, Overlay, Darken,
Lighten, Difference, Color Dodge, Color Burn. Hue / Saturation / Color / Luminosity currently pass the source color
(C12 may replace them with HSL mixes).

Coverage: raster masks (Gray8), live `maskSourceID` (document-space alpha of the source layer), and the nearest
ancestor folder mask. Adjustment layers do not alter pixels in this phase; C12 applies Levels/Curves/HSV/Exposure/
Gradient Map/Grain. Groups are pass-through.

## Present order

Clear → DxUi `Composite` → `Compose` + `BlitToTarget` (discard outside the hole, checkerboard + optional pixel grid)
→ one `Present`. Occlusion stays on the C1 scheduler (`DXGI_PRESENT_TEST` only).

## Validation

`Tests/CompositionTests` on WARP: 1×1 Multiply golden BGRA `(0,0,255,255)` slop 2; Color Dodge gray+red ≈
`(128,128,255,255)` slop 3; device-loss recreate; hidden surfaces 0. Tolerance is documented here so goldens are not
silently rebaselined.
