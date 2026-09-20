cbuffer LayerConstants : register(b0)
{
    float2 canvasSize;
    float2 layerCenter;
    float2 layerSize;
    float rotationSin;
    float rotationCos;
    float2 flipSign;
    uint blendMode;
    uint filterMode;
    float opacity;
    uint hasMask;
    uint hasLiveMask;
    uint hasFolderMask;
    float4 pad;
};

Texture2D<float4> g_source : register(t0);
Texture2D<float4> g_dest : register(t1);
Texture2D<float> g_mask : register(t2);
Texture2D<float4> g_live : register(t3);
Texture2D<float4> g_folder : register(t4);
SamplerState g_linear : register(s0);
SamplerState g_point : register(s1);

struct PixelInput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

float2 DocumentToLayerUv(float2 document)
{
    float2 delta = document - layerCenter;
    float2 local = float2(delta.x * rotationCos + delta.y * rotationSin,
                          -delta.x * rotationSin + delta.y * rotationCos);
    local *= flipSign;
    return local / max(layerSize, float2(1e-5, 1e-5)) + 0.5;
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

float4 SampleImage(float2 uv)
{
    float4 color = g_source.Sample(g_linear, uv);
    if (filterMode == 0)
    {
        color = g_source.Sample(g_point, uv);
    }
    return color;
}

float4 PixelMain(PixelInput input) : SV_TARGET
{
    int2 destPixel = int2(input.position.xy);
    float4 dest = g_dest.Load(int3(destPixel, 0));
    float2 document = input.position.xy;
    float2 uv = DocumentToLayerUv(document);
    if (uv.x < 0.0 || uv.y < 0.0 || uv.x > 1.0 || uv.y > 1.0)
    {
        return dest;
    }

    float4 src = SampleImage(uv);
    src *= opacity;
    if (hasMask != 0)
    {
        float coverage = (filterMode == 0) ? g_mask.Sample(g_point, uv) : g_mask.Sample(g_linear, uv);
        src *= coverage;
    }
    if (hasLiveMask != 0)
    {
        float4 live = g_live.Load(int3(destPixel, 0));
        src *= saturate(max(live.a, (live.r + live.g + live.b) * (1.0 / 3.0)));
    }
    if (hasFolderMask != 0)
    {
        float4 folder = g_folder.Load(int3(destPixel, 0));
        src *= saturate(max(folder.a, (folder.r + folder.g + folder.b) * (1.0 / 3.0)));
    }
    return PremulOver(src, dest, blendMode);
}
