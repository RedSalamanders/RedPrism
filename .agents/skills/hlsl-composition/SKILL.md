---
name: hlsl-composition
description: Change or extend the RedPrism document-space GPU composition and its build-time HLSL (layer passes, coverage textures, clipping stacks, blend modes, blit). Use for CanvasRenderer and RedPrism/Shaders work; not for swap-chain or window code.
---

# HLSL composition

Read `Specs/Rendering/Rendering_Composition.md` first; it is the contract and the golden table. Use the owning contract and its independent numeric golden cases for semantic questions; see the
`numeric-goldens` skill.

## Shape of the pipeline

- One fullscreen triangle (`CompositionVertex.hlsl`), one layer pixel shader (`CompositionPixel.hlsl`) selected by
  `passMode`, one blit shader (`CompositionBlit.hlsl`). Add a pass mode before adding a shader; add a shader only for
  a different output contract (compute, different targets).
- HLSL compiles at build time through the `FxCompile` items in `RedPrism/RedPrism.vcxproj` **and**
  `Tests/CompositionTests/CompositionTests.vcxproj` (both embed the bytecode headers). Warnings are errors: give every
  helper a single return variable (`X4000 potentially uninitialized`) and keep loops bounded.
- Constant buffers are `float2`-packed 16-byte multiples; the C++ struct mirrors the HLSL layout field for field and
  `static_assert(sizeof % 16 == 0)`. A `uint` flag word beats many `uint` fields.
- Coordinates: `SV_POSITION.xy` is the document pixel center, origin top-left, +Y down. `DocumentToUnit` inverts the
  layer transform (clockwise rotation, flips mirror the unit square). Sample images at unit coordinates; `Load`
  document-space textures (canvas, coverage) at `int2(position)`.
- Premultiplied everywhere on the canvas; straight color only inside a clipping stack surface. Blend formulas take
  straight colors (`BlendStraight`) and composite with the PDF separable operator (`PremulOver`).

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
- Surfaces (canvas, stack, coverage) are released by `ReleaseDocumentSurfaces` when the window hides; the self-test
  asserts `CanvasSurfaceBytes() == 0` afterwards.

## Proving a change

Add a numeric golden to `Tests/CompositionTests/CompositionTests.cpp` (2×2 documents, nearest sampling, alphas or one
channel, tolerance named in the spec table) and run:

```powershell
.\build.ps1 -Platform x64 -Configuration Debug
.\.build\x64\Debug\CompositionTests.exe
.\test.ps1 -Platform x64 -Configuration Debug
```

Print the actual pixels on failure (`PrintPixels`) so a WARP-vs-hardware difference is visible in the log.
