// Constants shared by RedPrism/CanvasRenderer.cpp and the composition shaders (Rendering_Composition.md): the pass
// modes, the layer flags, the adjustment kernels and the blit flags. C++ and HLSL both include this file, so a value
// can never mean one thing on the CPU and another on the GPU.
#ifndef REDPRISM_COMPOSITION_SHARED_H
#define REDPRISM_COMPOSITION_SHARED_H

#ifdef __cplusplus
#include <cstdint>
#define REDPRISM_SHARED_UINT inline constexpr std::uint32_t
#else
#define REDPRISM_SHARED_UINT static const uint
#endif

// Pass modes (`passMode`): what one fullscreen pass of CompositionPixel.hlsl writes.
REDPRISM_SHARED_UINT kCompositionPassLayer = 0;         // the layer over the canvas with its blend mode
REDPRISM_SHARED_UINT kCompositionPassStackBegin = 1;    // a clipping base as straight color plus its alpha
REDPRISM_SHARED_UINT kCompositionPassStackChild = 2;    // a clipped layer over the stack's straight color
REDPRISM_SHARED_UINT kCompositionPassStackResolve = 3;  // the stack premultiplied onto the canvas
REDPRISM_SHARED_UINT kCompositionPassLayerCoverage = 4; // a live source's coverage (R8)
REDPRISM_SHARED_UINT kCompositionPassMaskCoverage = 5;  // a folder mask's coverage (R8)
REDPRISM_SHARED_UINT kCompositionPassAdjust = 6;        // an adjustment layer over the canvas beneath it
REDPRISM_SHARED_UINT kCompositionPassStackAdjust = 7;   // a clipped adjustment over the stack's base color
REDPRISM_SHARED_UINT kCompositionPassLayerAdjust = 8;   // destructive preview on the layer's own pixel grid

REDPRISM_SHARED_UINT kCompositionPassSelectionOutline = 9; // view-grid solid/dash A8 masks

// Layer flags (`flags`).
REDPRISM_SHARED_UINT kCompositionFlagHasMask = 1u;
REDPRISM_SHARED_UINT kCompositionFlagHasLive = 2u;
REDPRISM_SHARED_UINT kCompositionFlagHasFolder = 4u;
REDPRISM_SHARED_UINT kCompositionFlagMaskPlaced = 8u;
REDPRISM_SHARED_UINT kCompositionFlagSelectionClip = 16u; // pass 9 only; sourceRect is the local physical clip

// Adjustment kernels (`adjustKind`): which math an adjustment pass runs. CanvasRenderer maps every
// `AdjustmentKind` to one of these explicitly; the enum's order never reaches the shader.
REDPRISM_SHARED_UINT kCompositionAdjustTables = 0;      // Levels, Curves, Exposure: three 256-entry tables
REDPRISM_SHARED_UINT kCompositionAdjustCube = 1;        // Hue/Saturation: the 33^3 color cube
REDPRISM_SHARED_UINT kCompositionAdjustGradientMap = 2; // Gradient Map: 256 colors by luminance
REDPRISM_SHARED_UINT kCompositionAdjustGrain = 3;       // Grain: procedural in document space

// Blit flags (`blitFlags`), decided by CanvasViewport so the grid and the sampling share one zoom scale.
REDPRISM_SHARED_UINT kCompositionBlitCrisp = 1u; // point sampling: document pixels drawn hard-edged
REDPRISM_SHARED_UINT kCompositionBlitGrid = 2u;  // the pixel grid

#undef REDPRISM_SHARED_UINT

#endif
