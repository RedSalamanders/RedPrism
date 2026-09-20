cbuffer BlitConstants : register(b0)
{
    float2 viewSize;
    float2 holeOrigin;
    float2 holeSize;
    float2 documentSize;
    float2 pan;
    float zoom;
    float showGrid;
    float4 pad;
};

Texture2D<float4> g_canvas : register(t0);
SamplerState g_linear : register(s0);
SamplerState g_point : register(s1);

struct PixelInput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

PixelInput VertexMain(uint vertexId : SV_VertexID)
{
    PixelInput output;
    float2 uv = float2((vertexId << 1) & 2, vertexId & 2);
    output.position = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    output.uv = uv;
    return output;
}

float4 PixelMain(PixelInput input) : SV_TARGET
{
    float2 pixel = input.position.xy;
    if (pixel.x < holeOrigin.x || pixel.y < holeOrigin.y || pixel.x >= holeOrigin.x + holeSize.x ||
        pixel.y >= holeOrigin.y + holeSize.y)
    {
        discard;
        return float4(0, 0, 0, 0);
    }

    float2 view = pixel - holeOrigin;
    float2 origin = (holeSize - documentSize * zoom) * 0.5 + pan;
    float2 document = (view - origin) / max(zoom, 1e-5);
    if (document.x < 0 || document.y < 0 || document.x >= documentSize.x || document.y >= documentSize.y)
    {
        float checker = (fmod(floor(pixel.x / 8.0) + floor(pixel.y / 8.0), 2.0) < 1.0) ? 0.18 : 0.22;
        return float4(checker, checker, checker, 1);
    }

    float2 uv = document / documentSize;
    float4 color = (zoom >= 8.0) ? g_canvas.Sample(g_point, uv) : g_canvas.Sample(g_linear, uv);
    float3 checker = (fmod(floor(document.x) + floor(document.y), 2.0) < 1.0) ? 0.85 : 0.75;
    float3 rgb = color.rgb + checker * (1.0 - color.a);
    if (showGrid > 0.5)
    {
        float2 grid = abs(frac(document) - 0.5);
        float gridLine = step(0.46, max(grid.x, grid.y));
        rgb = lerp(rgb, float3(0.2, 0.2, 0.2), gridLine * 0.35);
    }
    return float4(rgb, 1);
}
