// Document-space layer pass. One fullscreen triangle per pass; the pass mode selects what the pixel writes.
//
//   0  Layer onto the canvas: premultiplied source over the destination with the layer blend mode.
//   1  Stack begin: the clipping base written as straight color plus its alpha into an empty stack surface.
//   2  Stack child: a clipped layer blended over the stack's straight color; the stack alpha (base alpha) is kept.
//   3  Stack resolve: the stack surface premultiplied and composited onto the canvas with the base blend mode.
//   4  Layer coverage: the layer's alpha after transform, opacity, raster mask and upstream live coverage (R8).
//   5  Mask coverage: a Gray8 mask placed by the mask transform, times the parent coverage (R8); 0 outside.
//
// Coordinates: SV_POSITION.xy is the document pixel center (top-left origin, +Y down). A transform maps document
// pixels to the unit square of the layer or mask raster: rotation is clockwise, flips mirror the unit square.

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
};

static const uint kFlagHasMask = 1u;
static const uint kFlagHasLive = 2u;
static const uint kFlagHasFolder = 4u;
static const uint kFlagMaskPlaced = 8u;

Texture2D<float4> g_source : register(t0);
Texture2D<float4> g_dest : register(t1);
Texture2D<float> g_mask : register(t2);
Texture2D<float> g_live : register(t3);
Texture2D<float> g_folder : register(t4);
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

// Separable PDF blend of straight colors. Modes 9-12 (Hue, Saturation, Color, Luminosity) pass the source until C12.
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
    return result;
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

float4 SampleSource(float2 uv)
{
    float4 color = g_source.Sample(g_linear, uv);
    if (filterMode == 0)
    {
        color = g_source.Sample(g_point, uv);
    }
    return color;
}

float SampleMask(float2 uv)
{
    float value = g_mask.Sample(g_linear, uv);
    if (filterMode == 0)
    {
        value = g_mask.Sample(g_point, uv);
    }
    return value;
}

// The premultiplied layer color at one document pixel: image through its transform, times opacity, raster mask
// (through the mask placement when it sits apart from the layer), upstream live coverage and folder coverage.
float4 LayerColor(float2 document, int2 destPixel, bool useFolder)
{
    float4 src = float4(0, 0, 0, 0);
    float2 uv = DocumentToUnit(document, layerCenter, layerSize, layerSinCos, layerFlip);
    if (!OutsideUnit(uv))
    {
        src = SampleSource(uv) * opacity;
        if ((flags & kFlagHasMask) != 0)
        {
            float coverage = maskBackground;
            float2 maskUv = uv;
            if ((flags & kFlagMaskPlaced) != 0)
            {
                maskUv = DocumentToUnit(document, maskCenter, maskSize, maskSinCos, maskFlip);
            }
            if (!OutsideUnit(maskUv))
            {
                coverage = SampleMask(maskUv);
            }
            src *= coverage;
        }
        if ((flags & kFlagHasLive) != 0)
        {
            src *= saturate(g_live.Load(int3(destPixel, 0)));
        }
        if (useFolder && (flags & kFlagHasFolder) != 0)
        {
            src *= saturate(g_folder.Load(int3(destPixel, 0)));
        }
    }
    return src;
}

float4 PixelMain(PixelInput input) : SV_TARGET
{
    int2 destPixel = int2(input.position.xy);
    float2 document = input.position.xy;

    if (passMode == 3)
    {
        float4 stack = g_source.Load(int3(destPixel, 0));
        float4 premultiplied = float4(stack.rgb * stack.a, stack.a);
        if ((flags & kFlagHasFolder) != 0)
        {
            premultiplied *= saturate(g_folder.Load(int3(destPixel, 0)));
        }
        float4 dest = g_dest.Load(int3(destPixel, 0));
        return PremulOver(premultiplied, dest, blendMode);
    }
    if (passMode == 5)
    {
        float2 maskUv = DocumentToUnit(document, maskCenter, maskSize, maskSinCos, maskFlip);
        float coverage = OutsideUnit(maskUv) ? 0.0 : SampleMask(maskUv);
        if ((flags & kFlagHasFolder) != 0)
        {
            coverage *= saturate(g_folder.Load(int3(destPixel, 0)));
        }
        return float4(coverage, coverage, coverage, coverage);
    }

    float4 src = LayerColor(document, destPixel, passMode == 0);
    if (passMode == 4)
    {
        float coverage = saturate(src.a);
        return float4(coverage, coverage, coverage, coverage);
    }
    if (passMode == 1)
    {
        float sa = saturate(src.a);
        float3 straight = (sa > 1e-5) ? src.rgb / sa : 0.0;
        return float4(straight, sa);
    }
    float4 dest = g_dest.Load(int3(destPixel, 0));
    if (passMode == 2)
    {
        float sa = saturate(src.a);
        float3 s = (sa > 1e-5) ? src.rgb / sa : 0.0;
        float3 blended = BlendStraight(dest.rgb, s, blendMode);
        float3 rgb = (1.0 - sa) * dest.rgb + sa * blended;
        return float4(rgb, dest.a);
    }
    return PremulOver(src, dest, blendMode);
}
