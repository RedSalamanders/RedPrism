// Continuous brush coverage (Rendering_Brush.md), the legacy MetalBrushCoverage kernel. One dispatch per 256 x 256
// tile: every thread is one grid pixel. `permanent` accumulates the settled paint (soft: optical density; hard:
// coverage) and `preview` receives the coverage of the settled paint plus the provisional tail, 0-255, so replacing
// the tail leaves nothing behind. Coordinates: a tile pixel center maps through the grid's affine placement to
// document pixels; outside the canvas nothing is painted.

cbuffer BrushUniforms : register(b0)
{
    float4 mapping;  // a, b, c, d of the grid-to-document map
    float4 geometry; // document position of the tile's top-left corner, radius, hardness
    float4 canvas;   // width, height, antialias width, deposition spacing
    uint4 counts;    // tile width, tile height, settled segment count, total segment count
};

StructuredBuffer<float4> g_segments : register(t0);
RWStructuredBuffer<float> g_permanent : register(u0);
RWStructuredBuffer<uint> g_preview : register(u1);

static const float kDensityCap = 20.0;
static const float kFar = 1.0e30;

float SegmentDistanceSquared(float2 p, float4 segment)
{
    float2 v = segment.zw - segment.xy;
    float t = clamp(dot(p - segment.xy, v) / max(dot(v, v), 1e-12), 0.0, 1.0);
    float2 delta = p - (segment.xy + t * v);
    return dot(delta, delta);
}

float TipCoverage(float distanceSquared)
{
    float distance = sqrt(distanceSquared);
    float radius = geometry.z;
    float result = 0.0;
    if (geometry.w >= 1.0)
    {
        result = clamp((radius - distance) / canvas.z + 0.5, 0.0, 1.0);
    }
    else
    {
        float t = clamp((distance / radius - geometry.w) / (1.0 - geometry.w), 0.0, 1.0);
        result = max(0.0, (exp(-2.5 * t * t) - exp(-2.5)) / (1.0 - exp(-2.5)));
    }
    return result;
}

// Optical density adds along the stroke; coverage is 1 - exp(-density).
float TipDensity(float distanceSquared)
{
    return -log(max(1.0 - TipCoverage(distanceSquared), 0.001));
}

// The tip integrated along the part of the segment within reach (8-point Gauss-Legendre), per deposition spacing.
float SegmentDensity(float2 p, float4 segment)
{
    float2 v = segment.zw - segment.xy;
    float len = length(v);
    float result = 0.0;
    if (len < 1e-6)
    {
        result = TipDensity(dot(p - segment.xy, p - segment.xy)); // the initial click
    }
    else
    {
        float2 direction = v / len;
        float projection = dot(p - segment.xy, direction);
        float2 perpendicular = p - segment.xy - projection * direction;
        float perpendicularSquared = dot(perpendicular, perpendicular);
        float radiusSquared = geometry.z * geometry.z;
        if (perpendicularSquared < radiusSquared)
        {
            float reach = sqrt(radiusSquared - perpendicularSquared);
            float lo = max(0.0, projection - reach);
            float hi = min(len, projection + reach);
            if (hi > lo)
            {
                float midpoint = (lo + hi) * 0.5;
                float halfLength = (hi - lo) * 0.5;
                const float nodes[4] = {0.1834346425, 0.5255324099, 0.7966664774, 0.9602898565};
                const float weights[4] = {0.3626837834, 0.3137066459, 0.2223810345, 0.1012285363};
                float integral = 0.0;
                [unroll]
                for (uint i = 0; i < 4; ++i)
                {
                    float a = midpoint - halfLength * nodes[i] - projection;
                    float b = midpoint + halfLength * nodes[i] - projection;
                    integral += weights[i] * (TipDensity(perpendicularSquared + a * a) +
                                              TipDensity(perpendicularSquared + b * b));
                }
                result = integral * halfLength / canvas.w;
            }
        }
    }
    return result;
}

[numthreads(16, 16, 1)]
void BrushMain(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= counts.x || id.y >= counts.y)
    {
        return;
    }
    uint index = id.y * counts.x + id.x;
    float2 local = float2(id.xy) + 0.5;
    float2 p = geometry.xy + local.x * mapping.xy + local.y * mapping.zw;
    if (p.x < 0.0 || p.y < 0.0 || p.x >= canvas.x || p.y >= canvas.y)
    {
        g_preview[index] = 0;
        return;
    }
    if (geometry.w >= 1.0)
    {
        // Hard tips already have a solid interior: keep the pixel-edge antialiasing.
        float settled = kFar;
        float tail = kFar;
        [loop]
        for (uint i = 0; i < counts.z; ++i)
        {
            settled = min(settled, SegmentDistanceSquared(p, g_segments[i]));
        }
        [loop]
        for (uint j = counts.z; j < counts.w; ++j)
        {
            tail = min(tail, SegmentDistanceSquared(p, g_segments[j]));
        }
        float value = max(g_permanent[index], TipCoverage(settled));
        g_permanent[index] = value;
        g_preview[index] = uint(round(255.0 * max(value, TipCoverage(tail))));
    }
    else
    {
        float value = g_permanent[index];
        float tail = 0.0;
        [loop]
        for (uint i = 0; i < counts.z; ++i)
        {
            value += SegmentDensity(p, g_segments[i]);
        }
        [loop]
        for (uint j = counts.z; j < counts.w; ++j)
        {
            tail += SegmentDensity(p, g_segments[j]);
        }
        value = min(value, kDensityCap);
        g_permanent[index] = value;
        g_preview[index] = uint(round(255.0 * (1.0 - exp(-min(value + tail, kDensityCap)))));
    }
}
