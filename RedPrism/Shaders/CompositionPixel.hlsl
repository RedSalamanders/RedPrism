// Document-space layer pass. One fullscreen triangle per pass; the pass mode (CompositionShared.h) selects what the
// pixel writes:
//
//   Layer          the layer onto the canvas: premultiplied source over the destination with its blend mode.
//   StackBegin     the clipping base written as straight color plus its alpha into an empty stack surface.
//   StackChild     a clipped layer blended over the stack's straight color; the stack alpha (base alpha) is kept.
//   StackResolve   the stack surface premultiplied and composited onto the canvas with the base blend mode.
//   LayerCoverage  the layer's alpha after transform, opacity, raster mask and upstream live coverage (R8).
//   MaskCoverage   a Gray8 mask placed by the mask transform, times the parent coverage (R8); 0 outside.
//   Adjust         the composite beneath an adjustment layer reworked through its tables (C12), blended back with
//                  the layer blend mode at its opacity, mask and folder coverage; alpha is kept and transparent
//                  pixels left alone.
//   StackAdjust    a clipped adjustment reworks the stack's straight base color, keeping the stack alpha.
//
// Coordinates: the canvas is a window of the document. Canvas pixel (i, j) covers the document square of side
// `viewScale` at `viewOrigin + (i, j) * viewScale`, so SV_POSITION.xy is the document point
// `viewOrigin + SV_POSITION.xy * viewScale` (top-left origin, +Y down). The window is the whole document at scale 1
// unless the document is larger than the device's textures: the display then draws a reduced proxy (scale 2, 4, ...)
// and a full-resolution readback composes it in tiles (origin = the tile). A transform maps document pixels to the
// unit square of the layer or mask raster: rotation is clockwise, flips mirror the unit square. A texture may hold
// only part of its raster, reduced: `sourceRect` and `maskRect` map the raster's unit square onto the texels.

#include "CompositionShared.h"

cbuffer LayerConstants : register(b0)
{
    float2 canvasSize;
    float2 layerCenter;
    float2 layerSize;
    float2 layerSinCos;
    float2 layerFlip;
    float2 maskCenter;
    float2 maskSize;
    float2 maskSinCos;
    float2 maskFlip;
    float maskBackground;
    float opacity;
    uint blendMode;
    uint filterMode;
    uint passMode;
    uint flags;
    uint adjustKind;
    float grainAmount;
    float grainSize;
    float grainRoughness;
    uint grainSeed;
    float2 viewOrigin;
    float viewScale;
    float4 sourceRect; // the raster's unit square onto the texels: (uv - xy) * zw
    float4 maskRect;
};

Texture2D<float4> g_source : register(t0);
Texture2D<float4> g_dest : register(t1);
Texture2D<float> g_mask : register(t2);
Texture2D<float> g_live : register(t3);
Texture2D<float> g_folder : register(t4);
Texture2D<float4> g_lut : register(t5);  // 256 x 1: per-channel tables (r, g, b), or the gradient map's colors
Texture3D<float4> g_cube : register(t6); // 33^3: the Hue/Saturation color cube
SamplerState g_linear : register(s0);
SamplerState g_point : register(s1);

struct PixelInput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

float2 DocumentToUnit(float2 document, float2 center, float2 size, float2 sinCos, float2 flip)
{
    float2 delta = document - center;
    float2 local = float2(delta.x * sinCos.y + delta.y * sinCos.x, -delta.x * sinCos.x + delta.y * sinCos.y);
    local *= flip;
    return local / max(size, float2(1e-5, 1e-5)) + 0.5;
}

bool OutsideUnit(float2 uv)
{
    return uv.x < 0.0 || uv.y < 0.0 || uv.x > 1.0 || uv.y > 1.0;
}

float DodgeChannel(float backdrop, float source)
{
    float result = 0.0;
    if (source >= 0.999)
    {
        result = 1.0;
    }
    else if (backdrop <= 1e-5)
    {
        result = 0.0;
    }
    else
    {
        result = min(1.0, backdrop / (1.0 - source));
    }
    return result;
}

float BurnChannel(float backdrop, float source)
{
    float result = 0.0;
    if (source <= 1e-5)
    {
        result = 0.0;
    }
    else if (backdrop >= 0.999)
    {
        result = 1.0;
    }
    else
    {
        result = 1.0 - min(1.0, (1.0 - backdrop) / source);
    }
    return result;
}

// The PDF non-separable helpers: luminosity, saturation, and the clip that keeps a shifted color in gamut.
float Lum(float3 c)
{
    return dot(c, float3(0.3, 0.59, 0.11));
}

float3 ClipColor(float3 c)
{
    float l = Lum(c);
    float n = min(c.r, min(c.g, c.b));
    float x = max(c.r, max(c.g, c.b));
    if (n < 0.0)
    {
        c = l + (c - l) * l / max(l - n, 1e-6);
    }
    if (x > 1.0)
    {
        c = l + (c - l) * (1.0 - l) / max(x - l, 1e-6);
    }
    return c;
}

float3 SetLum(float3 c, float l)
{
    return ClipColor(c + (l - Lum(c)));
}

float Sat(float3 c)
{
    return max(c.r, max(c.g, c.b)) - min(c.r, min(c.g, c.b));
}

float3 SetSat(float3 c, float s)
{
    float mx = max(c.r, max(c.g, c.b));
    float mn = min(c.r, min(c.g, c.b));
    float3 result = float3(0, 0, 0);
    if (mx > mn)
    {
        result = (c - mn) * s / (mx - mn);
    }
    return result;
}

// The PDF blend of straight colors: modes 1-8 separable, 9-12 (Hue, Saturation, Color, Luminosity) non-separable.
float3 BlendStraight(float3 backdrop, float3 source, uint mode)
{
    float3 result = source;
    if (mode == 1)
    {
        result = backdrop * source;
    }
    else if (mode == 2)
    {
        result = 1.0 - (1.0 - backdrop) * (1.0 - source);
    }
    else if (mode == 3)
    {
        result = lerp(2.0 * backdrop * source, 1.0 - 2.0 * (1.0 - backdrop) * (1.0 - source), step(0.5, backdrop));
    }
    else if (mode == 4)
    {
        result = min(backdrop, source);
    }
    else if (mode == 5)
    {
        result = max(backdrop, source);
    }
    else if (mode == 6)
    {
        result = abs(backdrop - source);
    }
    else if (mode == 7)
    {
        result = float3(DodgeChannel(backdrop.r, source.r), DodgeChannel(backdrop.g, source.g),
                        DodgeChannel(backdrop.b, source.b));
    }
    else if (mode == 8)
    {
        result = float3(BurnChannel(backdrop.r, source.r), BurnChannel(backdrop.g, source.g),
                        BurnChannel(backdrop.b, source.b));
    }
    else if (mode == 9)
    {
        result = SetLum(SetSat(source, Sat(backdrop)), Lum(backdrop));
    }
    else if (mode == 10)
    {
        result = SetLum(SetSat(backdrop, Sat(source)), Lum(backdrop));
    }
    else if (mode == 11)
    {
        result = SetLum(source, Lum(backdrop));
    }
    else if (mode == 12)
    {
        result = SetLum(backdrop, Lum(source));
    }
    return saturate(result);
}

float4 PremulOver(float4 src, float4 dst, uint mode)
{
    float sa = saturate(src.a);
    float da = saturate(dst.a);
    float3 s = (sa > 1e-5) ? src.rgb / sa : 0.0;
    float3 d = (da > 1e-5) ? dst.rgb / da : 0.0;
    float3 blended = BlendStraight(d, s, mode);
    float outA = sa + da * (1.0 - sa);
    float3 outRgb = (1.0 - sa) * dst.rgb + (1.0 - da) * src.rgb + sa * da * blended;
    return float4(outRgb, outA);
}

// A texture coordinate with the exact affine screen footprint; no lane derivatives at clipped boundaries.
struct TexelCoordinate
{
    float2 texel;
    float2 dx;
    float2 dy;
};

TexelCoordinate Coordinate(float2 uv, float4 rect, float2 size, float2 sinCos, float2 flip)
{
    TexelCoordinate coordinate;
    coordinate.texel = (uv - rect.xy) * rect.zw;
    // DocumentToUnit is affine. Its exact footprint is independent of neighboring lanes, including helpers
    // outside a thin/clipped layer. FXC can sink ddx/ddy into the unit-square branches even when the source
    // computes them before those branches; that selects incorrect mips in optimized builds.
    float2 extent = max(size, float2(1e-5, 1e-5));
    float2 scale = viewScale * flip * rect.zw / extent;
    coordinate.dx = float2(sinCos.y, -sinCos.x) * scale;
    coordinate.dy = float2(sinCos.x, sinCos.y) * scale;
    return coordinate;
}

// The raster's unit square onto the texels the texture holds (the whole raster, or a part of it, reduced).
TexelCoordinate SourceCoordinate(float2 uv)
{
    return Coordinate(uv, sourceRect, layerSize, layerSinCos, layerFlip);
}

TexelCoordinate PlacedMaskCoordinate(float2 uv)
{
    return Coordinate(uv, maskRect, maskSize, maskSinCos, maskFlip);
}

TexelCoordinate MaskCoordinate(float2 uv)
{
    TexelCoordinate coordinate = Coordinate(uv, maskRect, layerSize, layerSinCos, layerFlip);
    if ((flags & kCompositionFlagMaskPlaced) != 0)
    {
        coordinate = PlacedMaskCoordinate(uv);
    }
    return coordinate;
}

float4 SampleSource(TexelCoordinate coordinate)
{
    float4 color = g_source.SampleGrad(g_linear, coordinate.texel, coordinate.dx, coordinate.dy);
    if (filterMode == 0)
    {
        color = g_source.SampleGrad(g_point, coordinate.texel, coordinate.dx, coordinate.dy);
    }
    return color;
}

float SampleMask(TexelCoordinate coordinate)
{
    float value = g_mask.SampleGrad(g_linear, coordinate.texel, coordinate.dx, coordinate.dy);
    if (filterMode == 0)
    {
        value = g_mask.SampleGrad(g_point, coordinate.texel, coordinate.dx, coordinate.dy);
    }
    return value;
}

// Where a layer's mask is read: through its placement when it sits apart from the layer, else the layer's own uv.
float2 MaskUnit(float2 document, float2 layerUv)
{
    float2 maskUv = layerUv;
    if ((flags & kCompositionFlagMaskPlaced) != 0)
    {
        maskUv = DocumentToUnit(document, maskCenter, maskSize, maskSinCos, maskFlip);
    }
    return maskUv;
}

// The premultiplied layer color at one document pixel: image through its transform, times opacity, raster mask
// (through the mask placement when it sits apart from the layer), upstream live coverage and folder coverage.
float4 LayerColor(float2 document, int2 destPixel, bool useFolder)
{
    float4 src = float4(0, 0, 0, 0);
    float2 uv = DocumentToUnit(document, layerCenter, layerSize, layerSinCos, layerFlip);
    float2 maskUv = MaskUnit(document, uv);
    TexelCoordinate source = SourceCoordinate(uv);
    TexelCoordinate mask = MaskCoordinate(maskUv);
    if (!OutsideUnit(uv))
    {
        src = SampleSource(source) * opacity;
        if ((flags & kCompositionFlagHasMask) != 0)
        {
            float coverage = maskBackground;
            if (!OutsideUnit(maskUv))
            {
                coverage = SampleMask(mask);
            }
            src *= coverage;
        }
        if ((flags & kCompositionFlagHasLive) != 0)
        {
            src *= saturate(g_live.Load(int3(destPixel, 0)));
        }
        if (useFolder && (flags & kCompositionFlagHasFolder) != 0)
        {
            src *= saturate(g_folder.Load(int3(destPixel, 0)));
        }
    }
    return src;
}

// ── Adjustment layers (C12) ────────────────────────────────────────────────

uint Mix32(uint x)
{
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

// A value in -1..1 for an integer lattice point, fixed by the point and the seed (the legacy adjust_grain).
float Lattice(int ix, int iy, uint seed)
{
    uint h = Mix32(asuint(ix) * 0x9E3779B1U ^ Mix32(asuint(iy) * 0x85EBCA77U ^ seed));
    return float(h & 0xFFFFU) / 65535.0 + float(h >> 16) / 65535.0 - 1.0;
}

// Film grain on a straight color at a document pixel center: the same brightness change on every channel,
// strongest in the midtones, fixed in document space by the seed.
float3 GrainStraight(float3 c, float2 document)
{
    float strength = min(grainAmount, 100.0) / 100.0 * 0.35 * 255.0;
    float rough = saturate(grainRoughness / 100.0);
    uint fineSeed = Mix32(grainSeed ^ 0xA511E9B3U);
    float size = max(grainSize, 1e-6);
    float u = document.x;
    float v = document.y;
    float cellX = floor(u / size);
    float cellY = floor(v / size);
    float tx = u / size - cellX;
    float ty = v / size - cellY;
    tx = tx * tx * (3.0 - 2.0 * tx);
    ty = ty * ty * (3.0 - 2.0 * ty);
    int ix = (int)cellX;
    int iy = (int)cellY;
    float n00 = Lattice(ix, iy, grainSeed);
    float n10 = Lattice(ix + 1, iy, grainSeed);
    float n01 = Lattice(ix, iy + 1, grainSeed);
    float n11 = Lattice(ix + 1, iy + 1, grainSeed);
    float top = n00 + (n10 - n00) * tx;
    float bottom = n01 + (n11 - n01) * tx;
    float smoothNoise = (top + (bottom - top) * ty) * 1.6;
    float fine = Lattice((int)floor(u), (int)floor(v), fineSeed);
    float noise = smoothNoise + (fine - smoothNoise) * rough;
    float3 rgb255 = c * 255.0;
    float level = min(1.0, dot(rgb255, float3(0.2126, 0.7152, 0.0722)) / 255.0);
    float delta = noise * strength * (0.4 + 2.4 * level * (1.0 - level));
    return saturate((rgb255 + delta) / 255.0);
}

// A straight color through the adjustment: the cube (Hue/Saturation), the per-channel tables (Levels, Curves,
// Exposure, interpolated between neighboring entries), the gradient map by luminance, or grain.
float3 AdjustStraight(float3 c, float2 document)
{
    float3 result = c;
    if (adjustKind == kCompositionAdjustCube)
    {
        float3 coordinate = c * (32.0 / 33.0) + 0.5 / 33.0;
        result = g_cube.SampleLevel(g_linear, coordinate, 0).rgb;
    }
    else if (adjustKind == kCompositionAdjustGradientMap)
    {
        float level = round(dot(c * 255.0, float3(0.2126, 0.7152, 0.0722)));
        result = g_lut.Load(int3((int)min(level, 255.0), 0, 0)).rgb;
    }
    else if (adjustKind == kCompositionAdjustGrain)
    {
        result = GrainStraight(c, document);
    }
    else
    {
        float3 u = (c * 255.0 + 0.5) / 256.0;
        result = float3(g_lut.SampleLevel(g_linear, float2(u.r, 0.5), 0).r,
                        g_lut.SampleLevel(g_linear, float2(u.g, 0.5), 0).g,
                        g_lut.SampleLevel(g_linear, float2(u.b, 0.5), 0).b);
    }
    return saturate(result);
}

// Where the adjustment applies: its opacity, its own raster mask (through its placement, `maskUv`, sampled at `mask`)
// and its folders.
float AdjustCoverage(float2 maskUv, TexelCoordinate mask, int2 destPixel, bool useFolder)
{
    float coverage = opacity;
    if ((flags & kCompositionFlagHasMask) != 0)
    {
        float maskValue = maskBackground;
        if (!OutsideUnit(maskUv))
        {
            maskValue = SampleMask(mask);
        }
        coverage *= maskValue;
    }
    if (useFolder && (flags & kCompositionFlagHasFolder) != 0)
    {
        coverage *= saturate(g_folder.Load(int3(destPixel, 0)));
    }
    return saturate(coverage);
}

float4 AdjustPass(float2 document, int2 destPixel, bool inStack)
{
    // The mask's coordinate before the branch on the destination's alpha (see TexelCoordinate).
    float2 maskUv = MaskUnit(document, DocumentToUnit(document, layerCenter, layerSize, layerSinCos, layerFlip));
    TexelCoordinate mask = MaskCoordinate(maskUv);
    float4 dest = g_dest.Load(int3(destPixel, 0));
    float4 result = dest;
    // The stack holds straight color at the base alpha; the canvas holds premultiplied color.
    float da = inStack ? 1.0 : saturate(dest.a);
    if (da > 1e-5)
    {
        float3 d = saturate(dest.rgb / da);
        float3 adjusted = AdjustStraight(d, document);
        if (blendMode != 0)
        {
            adjusted = BlendStraight(d, adjusted, blendMode);
        }
        float coverage = AdjustCoverage(maskUv, mask, destPixel, !inStack);
        result = float4(lerp(dest.rgb, adjusted * da, coverage), dest.a);
    }
    return result;
}

float4 PixelMain(PixelInput input) : SV_TARGET
{
    int2 destPixel = int2(input.position.xy);
    float2 document = viewOrigin + input.position.xy * viewScale;

    if (passMode == kCompositionPassSelectionOutline)
    {
        // This overlay is already rasterized on its exact device-pixel grid. The owner only selects a
        // complete phase; no document geometry, reduction, derivatives or texture upload happens here.
        float2 local = input.position.xy - viewOrigin;
        float4 outline = float4(0, 0, 0, 0);
        if (local.x >= 0 && local.y >= 0 && local.x < layerSize.x && local.y < layerSize.y)
        {
            int2 maskPixel = int2(local);
            float solid = saturate(g_source.Load(int3(maskPixel, 0)).r);
            float dash = saturate(g_mask.Load(int3(maskPixel, 0)));
            // White underlay, then black dash: premultiplied source-over, composed once over the target.
            float white = solid * (1.0 - dash);
            outline = float4(white, white, white, dash + white);
            if ((flags & kCompositionFlagSelectionClip) != 0)
            {
                // Clip the combined result once. Independent plane clips change fractional edge colors.
                float2 cell = float2(maskPixel);
                float2 coverage = saturate(min(cell + 1.0, sourceRect.zw) - max(cell, sourceRect.xy));
                outline *= coverage.x * coverage.y;
            }
        }
        return outline;
    }
    if (passMode == kCompositionPassLayerAdjust)
    {
        // The transient target is on the layer's pixel grid, before its transform, opacity and real mask.
        // Match the CPU byte result before BlendThrough; the source alpha is retained exactly.
        float4 src = g_source.Load(int3(destPixel, 0));
        float3 color = float3(0, 0, 0);
        if (src.a > 0)
        {
            float3 adjusted = AdjustStraight(saturate(src.rgb / src.a), document);
            adjusted = floor(saturate(adjusted * src.a) * 255.0 + 0.5) / 255.0;
            float coverage = 1;
            if ((flags & kCompositionFlagHasMask) != 0)
            {
                coverage = saturate(g_mask.Load(int3(destPixel, 0)));
            }
            color = floor(lerp(src.rgb, adjusted, coverage) * 255.0 + 0.5) / 255.0;
        }
        return float4(color, src.a);
    }
    if (passMode == kCompositionPassAdjust || passMode == kCompositionPassStackAdjust)
    {
        return AdjustPass(document, destPixel, passMode == kCompositionPassStackAdjust);
    }
    if (passMode == kCompositionPassStackResolve)
    {
        float4 stack = g_source.Load(int3(destPixel, 0));
        float4 premultiplied = float4(stack.rgb * stack.a, stack.a);
        if ((flags & kCompositionFlagHasFolder) != 0)
        {
            premultiplied *= saturate(g_folder.Load(int3(destPixel, 0)));
        }
        float4 dest = g_dest.Load(int3(destPixel, 0));
        return PremulOver(premultiplied, dest, blendMode);
    }
    if (passMode == kCompositionPassMaskCoverage)
    {
        float2 maskUv = DocumentToUnit(document, maskCenter, maskSize, maskSinCos, maskFlip);
        TexelCoordinate mask = PlacedMaskCoordinate(maskUv);
        float coverage = 0.0;
        if (!OutsideUnit(maskUv))
        {
            coverage = SampleMask(mask);
        }
        if ((flags & kCompositionFlagHasFolder) != 0)
        {
            coverage *= saturate(g_folder.Load(int3(destPixel, 0)));
        }
        return float4(coverage, coverage, coverage, coverage);
    }

    float4 src = LayerColor(document, destPixel, passMode == kCompositionPassLayer);
    if (passMode == kCompositionPassLayerCoverage)
    {
        float coverage = saturate(src.a);
        return float4(coverage, coverage, coverage, coverage);
    }
    if (passMode == kCompositionPassStackBegin)
    {
        float sa = saturate(src.a);
        float3 straight = (sa > 1e-5) ? src.rgb / sa : 0.0;
        return float4(straight, sa);
    }
    float4 dest = g_dest.Load(int3(destPixel, 0));
    if (passMode == kCompositionPassStackChild)
    {
        float sa = saturate(src.a);
        float3 s = (sa > 1e-5) ? src.rgb / sa : 0.0;
        float3 blended = BlendStraight(dest.rgb, s, blendMode);
        float3 rgb = (1.0 - sa) * dest.rgb + sa * blended;
        return float4(rgb, dest.a);
    }
    return PremulOver(src, dest, blendMode);
}
