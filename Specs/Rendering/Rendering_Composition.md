# Canvas composition

Status: current normative product contract
Last reviewed: 2026-09-21
Owner: `RedPrism/CanvasRenderer.*`, `RedPrism/CanvasViewport.h`, HLSL under `RedPrism/Shaders/`

## Scope

This contract owns document-space GPU compositing and the view transform. The swap chain and Present stay in
`Renderer`. The numeric expectations and tolerances below, together with the independent rendering tests,
own layer, mask and clipping behavior.

## Viewport

`CanvasViewport` maps the canvas hole (pixels) to document space. Opening a document fits the canvas (`followsFit`).
Zoom is clamped to **0.001–32**. Pixel grid draws when zoom ≥ **8**. Wheel zoom is around the cursor; Space or the
Hand tool pans; View menu commands fit (Ctrl+0), show actual pixels (Ctrl+1, zoom 1), and step by 1.25 (Ctrl+= /
Ctrl+-) around the hole center. Pointer mapping uses hole-relative view coordinates and the window DPI
(`backingScale = dpi/96`). `Fit` keeps a 96 point margin on both axes (legacy `CanvasViewport.fit`).

Sharp downsample when zoomed out uses mipmapped layer textures (`GenerateMips` when the device allows; otherwise a
single-level fallback). `LayerSampling::Nearest` samples with point filtering; `Smooth` and `High` sample linearly.

## Document-space model

Document space has its origin at the top-left, +Y down, one unit per document pixel. A `LayerTransform` places the
unit square of a raster: rotation is clockwise around the center, `flipX`/`flipY` mirror the unit square, and the
shader inverts that mapping per pixel (`DocumentToUnit`), so pixels outside the transform rectangle are transparent.

Every visible pixel layer draws with one fullscreen pass (`CompositionPixel.hlsl`, pass `Layer`) into ping-pong
BGRA8 canvas textures. The premultiplied layer color is:

```text
color = sample(image, layerUnit) × opacity × rasterMask × liveCoverage × folderCoverage
```

- **Raster mask**: a Gray8 image sampled at the layer's unit coordinates. A mask moved apart from its layer
  (`LayerMask::placement`) samples at the placement's unit coordinates instead; outside the placement it shows
  its edge tone (`MaskEdgeBackground` of the thumbnail: 1 reveal or 0 hide, legacy `LayerMask.background`).
  A disabled mask contributes nothing.
- **Live coverage** (`maskSourceID`): the source layer's own coverage in document space — its image alpha through
  its transform, times its opacity, its raster mask and its own upstream live coverage — independent of the
  source's visibility and color (legacy `LiveMaskRenderer.coverage`). A source without pixels covers nothing, so
  the clipped layer is hidden. Coverage textures are R8, document sized, rendered by pass `LayerCoverage` and
  cached per composition by source id.
- **Folder coverage**: an enabled folder mask is a Gray8 image placed by the folder's mask transform (placement
  or the folder transform); outside its rectangle the coverage is 0. Nested folder masks multiply. Pass
  `MaskCoverage` renders one R8 texture per masked folder chain (`GroupCoverage`), cached per composition.

## Clipping stacks

A base layer with no `maskSourceID` and no adjustment, followed in render order by clipped siblings whose
`maskSourceID` is that base and whose `parentID` matches, forms a **clipping stack** (legacy
`LiveMaskRenderer.prepareStacks`). The stack composites through a scratch surface:

1. `StackBegin` writes the base as straight color plus its alpha (opacity applied).
2. `StackChild` blends each clipped layer over the stack's straight color with the child's blend mode and opacity
   while keeping the base alpha, so a soft base edge never darkens (no black fringe).
3. `StackResolve` premultiplies the stack, applies the base's folder coverage, and composites it onto the canvas
   with the **base's** blend mode.

Clipped layers that are not directly above their source (or sit in another folder) draw as ordinary layers
multiplied by the source's live coverage.

## Blend modes

Modes 1–8 (Multiply, Screen, Overlay, Darken, Lighten, Difference, Color Dodge, Color Burn) are the separable PDF
formulas on straight colors, composited as `Co = (1−αs)·Cb + (1−αb)·Cs + αs·αb·B(cb,cs)`. Color Dodge and Color
Burn follow the PDF / Core Image definition, never Core Graphics. Hue, Saturation, Color and Luminosity (modes
9–12) pass the source color until C12 ships the HSL mixes. Adjustment layers do not alter pixels in this phase
(C12); groups are pass-through.

## Composition lifetime and residency

- `Compose` re-renders only when the document changed (`InvalidateDocument`, called by `Application::DocumentChanged`
  after every session edit, undo/redo, open and import) or the canvas surfaces were rebuilt (document size, device
  loss, hidden→visible). A settled document composites zero times per frame; `CompositeCount()` proves it and
  `--self-test` asserts a second frame does not re-composite.
- Uploaded rasters are cached by `Raster::Identity()` and keep their storage alive while cached, so a freed address
  can never alias a different image. Every composition releases entries it did not use, which bounds GPU residency to
  the rasters of the current document.
- Canvas surfaces: two BGRA8 canvases, two BGRA8 stack surfaces (allocated on the first clipping stack), and a pool
  of R8 coverage textures reused across compositions. `CanvasSurfaceBytes()` reports all of them. Hidden, minimized,
  or occluded documents drop them (`ReleaseDocumentSurfaces`, `CanvasSurfaceBytes() == 0`) and MUST NOT Present.
- Device loss rebuilds shaders, surfaces and the raster cache as one operation.
- Constants are updated with `Map` `DISCARD`; steady-state frames that only blit do not allocate.

## Present order

Clear → chrome `Prepare` (layout, hole) → `Compose` (only when dirty) + `BlitToTarget` (discard outside the hole,
checkerboard + optional pixel grid) → DxUi `Composite` over the canvas (its surface is transparent where no control
paints, so tooltips and popups can overlap the canvas) → one `Present`. Occlusion stays on the C1 scheduler.

## Validation

`Tests/CompositionTests` runs on WARP with nearest sampling on 2×2 documents unless stated; tolerances are named
here so goldens are not silently rebaselined:

| Golden (legacy source) | Expected |
| --- | --- |
| Blend modes, 0.4 gray under 0.8 gray (`LayerAppearanceTests`) | Normal 0.8, Multiply 0.32, Screen 0.88, Overlay 0.64, Darken 0.4, Lighten 0.8, Difference 0.4, Dodge 1, Burn 0.25 (±5/255); opacity 0.5 → 0.6; opacity 0 → 0.4 |
| Multiply red over white; Dodge red over 50 % gray | `(0,0,255,255)` ±2; `(128,128,255,255)` ±3 |
| Order, hidden layer, blank layer, empty document (`LayerTests`) | top wins; hidden top reveals red; blank layer transparent; empty document alpha 0 |
| Raster masks (`LayerMaskTests`) | reveal-all 255; hide-all 0; disabled 255; coverage `[255,0,128,255]` ±1; ×opacity 0.5 `[128,0,64,128]` ±1; flipX `[0,255,255,128]`; rotation keeps the coverage total ±4; placement +1 px `[255,255,255,128]`; placed hide-all stays 0 |
| Live masks (`LiveMaskTests`) | hidden black source `[255,0,128,255]`; source opacity 0.5 `[128,0,64,128]`; target mask 0.5 lowers alpha; moved source `[0,255,0,128]`; chain `[0,0,128,255]`; blank source hides |
| Clipping stack (`clippingColorPreservesSoftBaseAlphaWithoutBlackFringe`) | alpha stays `[255,128,32,0]`, RGB equals alpha (white at base coverage); child opacity keeps alpha; over a white background fully white and opaque; base Multiply applies to the stack (200 gray × 128 gray ≈ 100 ±3) |
| Folder masks (`folderMasksClipEveryLayerInside…`) | hide-all 0; disabled 255; coverage `[255,0,128,255]`; × layer mask `[255,0,64,255]` ±1; nested outer hide-all 0; coverage pool ≤ 2 textures; hidden parent 0 |
| Transform (`renderingRotatesScalesAndFlipsWithoutReplacingPixels`) | 8×8 red/blue at (8,8) 16×16 rot 90: (16,12) red, (16,20) blue; flipX → (16,12) blue; 4× scale and flipY keep columns |
| Residency and caching | settled document not re-composited; `InvalidateDocument` re-composites; hidden surfaces 0; device-loss recreate; resize rebuilds |
| Viewport (`CompositionTests.swift`) | round trip at zoom 0.25/1/3.75 and backing 1/2 within 1e-6; cursor pixel fixed under zoom; fit margin ≤ 704×504 in 800×600 at 2×; resize keeps the center; limits 0.001 and 32 |
