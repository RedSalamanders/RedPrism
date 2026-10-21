---
name: hlsl-composition
description: Change or extend the RedPrism document-space GPU composition and its build-time HLSL (layer passes, coverage textures, clipping stacks, blend modes, blit, the brush compute kernel). Use for CanvasRenderer and RedPrism/Shaders work; not for swap-chain or window code.
---

# HLSL composition

Read `Specs/Rendering/Rendering_Composition.md` first; it is the contract and the golden table. Use the owning contract and its independent numeric golden cases for semantic questions; see the
`numeric-goldens` skill.

## Shape of the pipeline

- One fullscreen triangle (`CompositionVertex.hlsl`), one layer pixel shader (`CompositionPixel.hlsl`) selected by
  `passMode`, one blit shader (`CompositionBlit.hlsl`). Add a pass mode before adding a shader; add a shader only for
  a different output contract (compute, different targets).
- The brush is the one compute shader (`BrushCoverage.hlsl`, `cs_5_0`, driven by `GpuBrushCoverage`): one dispatch
  per 256² tile in 16 × 16 groups, structured buffers for the segments, the settled density and the `uint` preview,
  constants per tile through `Map` `DISCARD`, only the preview read back. The CPU kernel in `Common/BrushStroke.cpp`
  is its golden (within 2/255); change both together and keep `Rendering_Brush.md` current.
- HLSL compiles at build time through the `FxCompile` items in `RedPrism/RedPrism.vcxproj` **and**
  `Tests/CompositionTests/CompositionTests.vcxproj` (both embed the bytecode headers). Warnings are errors: give every
  helper a single return variable (`X4000 potentially uninitialized`) and keep loops bounded.
- Constant buffers are `float2`-packed 16-byte multiples; the C++ struct mirrors the HLSL layout field for field and
  `static_assert(sizeof % 16 == 0)`. A `uint` flag word beats many `uint` fields.
- Coordinates: `SV_POSITION.xy` is the document pixel center, origin top-left, +Y down. `DocumentToUnit` inverts the
  layer transform (clockwise rotation, flips mirror the unit square). Sample images at unit coordinates; `Load`
  document-space textures (canvas, coverage) at `int2(position)`.
- Premultiplied everywhere on the canvas; straight color only inside a clipping stack surface. Blend formulas take
  straight colors (`BlendStraight`, the separable modes and the PDF non-separable Hue / Saturation / Color /
  Luminosity) and composite with the PDF separable operator (`PremulOver`); `Adjust::BlendStraight` is the CPU
  reference.
- Adjustment layers (C12) are passes 6 (`Adjust`, the canvas beneath) and 7 (`StackAdjust`, the stack's straight
  base color): lookup textures at t5 (`g_lut`, a 256 × 1 RGBA32F row) and t6 (`g_cube`, the 33³ Hue/Saturation
  cube) built by `CanvasRenderer::AdjustmentInputs` from `Common/Adjust.cpp`, Grain procedural in the shader;
  the CPU `Adjust::ApplyAdjustment` is the golden (1/255 for tables, 4 for the cube, 2 for grain). A function with
  several returns can trip FXC's X4000 with warnings as errors; keep one return per pass function.
- Destructive GPU previews use pass 8 (`LayerAdjust`) on the prepared layer grid before the ordinary layer pass.
  Keep source alpha, round the filtered premultiplied bytes before mixing through edit selection coverage, and key
  grain by `(pixel + 0.5) / previewScale` in layer pixels. Pass 6 adjusts the composite beneath an adjustment layer
  and cannot implement this operation. LUT/cube values come from the preview worker; reusable DYNAMIC LUT / DEFAULT
  cube textures and the transient target remain UI-owned. Prove parity against `RunFilter` plus `BlendThrough`,
  with the ordinary transform/mask/opacity/clipping/folder passes and the existing per-kind tolerances.

## Coverage and stacks

- Anything a layer depends on (a live source, a folder mask) is rendered into a pooled R8 document-space coverage
  texture once per composition and cached by id (`_liveCoverageIndex`, `_groupCoverageIndex`). Never sample a source
  layer's raster directly at the destination pixel: that ignores its transform, mask, opacity and upstream clips.
- A clipping stack (base + contiguous clipped siblings above it, same parent) composites through the stack
  surfaces so the base alpha is preserved; the base's blend mode applies to the whole stack at resolve.
- Folder coverage is 0 outside the mask rectangle; a placed layer mask shows `MaskEdgeBackground` outside its
  rectangle. Both come from the legacy contract, not from convenience.

## Residency and invalidation

- `Compose` runs only when `_documentDirty`; `Application::DocumentChanged` sets it. Do not add per-frame work to
  `Compose`; per-frame work belongs in `BlitToTarget`, which must stay allocation-free.
- `TextureFor` caches by `Raster::Identity()` and keeps the `Raster` alive; the sweep at the end of `Compose` releases
  textures the composition did not use. Keep that invariant when adding new texture kinds.
- A stroke in progress draws from a working texture (`BeginStroke` / `UpdateStrokeTiles` / `EndStroke`, adopted into
  the cache on commit) and sparse rasters assemble on the GPU (`CreateSparseTexture`); both are per-edit work, never
  per-frame, and never a full-canvas upload per pointer update.
- Surfaces (canvas, stack, coverage) are released by `ReleaseDocumentSurfaces` when the window hides; the self-test
  asserts `CanvasSurfaceBytes() == 0` afterwards.

## Proving a change

Add a numeric golden to `Tests/CompositionTests/Composition.Tests.Runner.cpp` (2×2 documents, nearest sampling, alphas or one
channel, tolerance named in the spec table) and run:

```powershell
.\build.ps1 -Platform x64 -Configuration Debug
.\.build\x64\Debug\CompositionTests.exe
.\test.ps1 -Platform x64 -Configuration Debug
```

Print the actual pixels on failure (`PrintPixels`) so a WARP-vs-hardware difference is visible in the log.
