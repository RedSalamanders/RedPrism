#pragma once

#include "BrushStroke.h"

#include <cstdint>
#include <d3d11_1.h>
#include <unordered_map>
#include <vector>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/com.h>
#pragma warning(pop)

// The Direct3D 11 compute engine behind the brush (Rendering_Brush.md): `BrushCoverage.hlsl` per tile, with the
// settled density kept on the GPU for the stroke's lifetime and only the 0-255 preview read back. Borrows the
// application device; owns no full-canvas allocation, only buffers for the tiles a stroke touches.
class GpuBrushCoverage final : public BrushCoverageEngine
{
  public:
    GpuBrushCoverage() = default;
    ~GpuBrushCoverage() override;
    GpuBrushCoverage(const GpuBrushCoverage&) = delete;
    GpuBrushCoverage& operator=(const GpuBrushCoverage&) = delete;

    // Compiles nothing: the shader is build-time bytecode. Creates the pipeline on `device`.
    HRESULT Initialize(ID3D11Device* device, ID3D11DeviceContext* context) noexcept;
    void Release() noexcept;
    [[nodiscard]] bool Ready() const noexcept
    {
        return _pipeline != nullptr;
    }

    [[nodiscard]] bool Render(std::span<const BrushKernelTile> tiles, std::span<const BrushSegment> settled,
                              std::span<const BrushSegment> tail, const BrushKernelParams& params) noexcept override;
    void BeginStroke() noexcept override;
    void EndStroke() noexcept override;
    [[nodiscard]] bool UsesCpuPermanent() const noexcept override
    {
        return false;
    }
    [[nodiscard]] std::size_t TileCount() const noexcept
    {
        return _tiles.size();
    }

  private:
    struct Uniforms
    {
        float mapping[4];
        float geometry[4];
        float canvas[4];
        std::uint32_t counts[4];
    };

    struct TileBuffers
    {
        wil::com_ptr_nothrow<ID3D11Buffer> permanent;
        wil::com_ptr_nothrow<ID3D11UnorderedAccessView> permanentView;
        wil::com_ptr_nothrow<ID3D11Buffer> preview;
        wil::com_ptr_nothrow<ID3D11UnorderedAccessView> previewView;
        wil::com_ptr_nothrow<ID3D11Buffer> staging;
        int width = 0;
        int height = 0;
    };

    [[nodiscard]] TileBuffers* BuffersFor(const BrushKernelTile& tile) noexcept;
    [[nodiscard]] HRESULT EnsureSegments(std::size_t count) noexcept;

    ID3D11Device* _device = nullptr;
    ID3D11DeviceContext* _context = nullptr;
    wil::com_ptr_nothrow<ID3D11ComputeShader> _pipeline;
    wil::com_ptr_nothrow<ID3D11Buffer> _uniforms;
    wil::com_ptr_nothrow<ID3D11Buffer> _segments;
    wil::com_ptr_nothrow<ID3D11ShaderResourceView> _segmentsView;
    std::size_t _segmentCapacity = 0;
    std::unordered_map<std::uint64_t, TileBuffers> _tiles;
};
