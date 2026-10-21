// One next mip level, in its own viewport. The source view exposes one completed parent level only.
// Linear clamp sampling preserves premultiplied color (or the R8 mask channel); no straight-color conversion.
Texture2D<float4> g_parent : register(t0);
SamplerState g_linearClamp : register(s0);

struct PixelInput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

float4 PixelMain(PixelInput input) : SV_TARGET
{
    return g_parent.SampleLevel(g_linearClamp, input.uv, 0);
}
