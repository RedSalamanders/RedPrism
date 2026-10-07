#pragma once

#include "BrushStroke.h"

#include <cstdint>
#include <d3d11_1.h>
#include <memory>
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

  public:
    // Construct the empty map/control owner on a worker under complete admission. TakeStrokeEpoch swaps
    // the entire graph without hash iteration/allocation. No immediate context or GPU reservation lives here.
    // Destroy/reset only on a worker after every coverage copy/map user has acknowledged; the original owner
    // releases its separate GPU reservation only after that worker acknowledgement (after Close on shutdown).
    class StrokeEpoch final
    {
      public:
        StrokeEpoch();
        ~StrokeEpoch();
        StrokeEpoch(const StrokeEpoch&) = delete;
        StrokeEpoch& operator=(const StrokeEpoch&) = delete;
        StrokeEpoch(StrokeEpoch&&) = delete;
        StrokeEpoch& operator=(StrokeEpoch&&) = delete;
        [[nodiscard]] std::size_t TileCount() const noexcept
        {
            return _tiles ? _tiles->size() : 0;
        }
        [[nodiscard]] std::uint64_t CpuBytes() const noexcept
        {
            return _cpuBytes;
        }
        [[nodiscard]] std::uint64_t GpuBytes() const noexcept
        {
            return _gpuBytes;
        }
        [[nodiscard]] bool HasResources() const noexcept;
        void ResetForWork() noexcept;

      private:
        friend class GpuBrushCoverage;
        using TileMap = std::unordered_map<std::uint64_t, TileBuffers>;
        std::unique_ptr<TileMap> _tiles;
        wil::com_ptr_nothrow<ID3D11Buffer> _segments;
        wil::com_ptr_nothrow<ID3D11ShaderResourceView> _segmentsView;
        wil::com_ptr_nothrow<ID3D11Device> _device;
        std::size_t _segmentCapacity = 0;
        std::uint64_t _tileGpuBytes = 0;
        std::uint64_t _cpuBytes = 0;
        std::uint64_t _gpuBytes = 0;
        std::uint32_t _capturingThread = 0;
    };
    [[nodiscard]] std::uint64_t StrokeCpuOwnershipBytes() const noexcept;
    [[nodiscard]] std::uint64_t StrokeGpuOwnershipBytes() const noexcept;
    // Non-mutating owner preflight. Serialized access must preserve this exact engine and empty destination
    // until Take; this does not reserve resources or acknowledge GPU completion.
    [[nodiscard]] bool CanTakeStrokeEpoch(const StrokeEpoch& empty) const noexcept;
    [[nodiscard]] bool TakeStrokeEpoch(StrokeEpoch& empty) noexcept;
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
    // Owner-only final coverage recording. Performs the same compute dispatch without copying,
    // mapping or converting preview values. The final job retains each borrowed buffer through
    // GpuWorkQueue::EnqueueBufferReadback before EndStroke may release this engine's owners.
    [[nodiscard]] bool RecordCoverage(std::span<const BrushKernelTile> tiles, std::span<const BrushSegment> settled,
                                      std::span<const BrushSegment> tail, const BrushKernelParams& params) noexcept;
    [[nodiscard]] ID3D11Buffer* CoverageBuffer(std::uint64_t key, int width, int height) const noexcept;
    // Owner-only final recording against the captured density graph. The live engine must be empty and bound
    // to the epoch's original device. Swaps the graph for one bounded recording call, restores it on every
    // outcome, never reads pixels and leaves no captured stroke resources in the live engine.
    [[nodiscard]] bool RecordEpochCoverage(StrokeEpoch& epoch, std::span<const BrushKernelTile> tiles,
                                           std::span<const BrushSegment> settled,
                                           const BrushKernelParams& params) noexcept;
    [[nodiscard]] ID3D11Buffer* EpochCoverageBuffer(const StrokeEpoch& epoch, std::uint64_t key, int width,
                                                    int height) const noexcept;
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

    [[nodiscard]] TileBuffers* BuffersFor(const BrushKernelTile& tile) noexcept;
    [[nodiscard]] HRESULT EnsureSegments(std::size_t count) noexcept;
    [[nodiscard]] HRESULT EnsureStaging(TileBuffers& buffers) noexcept;
    [[nodiscard]] bool RenderImpl(std::span<const BrushKernelTile> tiles, std::span<const BrushSegment> settled,
                                  std::span<const BrushSegment> tail, const BrushKernelParams& params,
                                  bool readback) noexcept;

    ID3D11Device* _device = nullptr;
    ID3D11DeviceContext* _context = nullptr;
    wil::com_ptr_nothrow<ID3D11ComputeShader> _pipeline;
    wil::com_ptr_nothrow<ID3D11Buffer> _uniforms;
    wil::com_ptr_nothrow<ID3D11Buffer> _segments;
    wil::com_ptr_nothrow<ID3D11ShaderResourceView> _segmentsView;
    std::size_t _segmentCapacity = 0;
    std::uint64_t _tileGpuBytes = 0;
    std::unordered_map<std::uint64_t, TileBuffers> _tiles;
};
