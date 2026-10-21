#pragma once

#include <cstdint>
#include <d3d11.h>

// Borrowed worker-prepared state. The creator holds every object through the queue's real completion
// acknowledgement. The queue additionally retains them for uncertain recording and joined shutdown.
struct GpuMipSlice final
{
    ID3D11ShaderResourceView* source = nullptr;
    ID3D11RenderTargetView* destination = nullptr;
    ID3D11VertexShader* vertex = nullptr;
    ID3D11PixelShader* pixel = nullptr;
    ID3D11SamplerState* sampler = nullptr;
    ID3D11RasterizerState* rasterizer = nullptr;
    ID3D11DepthStencilState* depth = nullptr;
    unsigned int width = 0;
    unsigned int height = 0;
    std::uint64_t deviceGeneration = 0;
};
