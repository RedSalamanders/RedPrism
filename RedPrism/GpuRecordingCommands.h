#pragma once

#include "GpuCommandStream.h"

#include <cstdint>
#include <d3d11_1.h>

// Worker-only bounded recording into a private deferred context. No immediate context, execute, readback,
// document mutation or GPU-completion assertion. Parent retains all sources, full admission and the stream.
class GpuRecordingCommands final
{
  public:
    GpuRecordingCommands() = default;
    GpuRecordingCommands(const GpuRecordingCommands&) = delete;
    GpuRecordingCommands& operator=(const GpuRecordingCommands&) = delete;
    static constexpr unsigned int kTileSide = 512;
    static constexpr unsigned int kUploadBytes = 1u << 20;
    [[nodiscard]] HRESULT InitializeForWork(ID3D11DeviceContext* context, GpuCommandStream& stream,
                                            CancelToken cancellation = {}) noexcept;
    // Same private context/stream after its actual successful joined RestartOwner; keep resident state.
    [[nodiscard]] HRESULT RestartForWork(ID3D11DeviceContext* context, GpuCommandStream& original,
                                         CancelToken cancellation) noexcept;
    [[nodiscard]] HRESULT StatusForWork() const noexcept;
    // Same private context/device, on the initialized producer thread; numeric generations are insufficient.
    [[nodiscard]] HRESULT ContextStatusForWork(ID3D11Device* device, ID3D11DeviceContext* context) const noexcept;
    void CheckpointForWork() noexcept;
    void UpdateForWork(ID3D11Resource* resource, unsigned int subresource, const D3D11_BOX* box, const void* data,
                       unsigned int rowPitch, unsigned int depthPitch) noexcept;
    void ClearForWork(ID3D11RenderTargetView* target, const float* color) noexcept;
    void CopyForWork(ID3D11Resource* destination, unsigned int destinationSubresource, unsigned int destinationX,
                     unsigned int destinationY, unsigned int destinationZ, ID3D11Resource* source,
                     unsigned int sourceSubresource, const D3D11_BOX* region) noexcept;
    void GenerateMipsForWork(ID3D11ShaderResourceView* source) noexcept;
    [[nodiscard]] std::uint64_t RecordedUploadBytes() const noexcept
    {
        return _uploadBytes;
    }

  private:
    void FailForWork(HRESULT error) noexcept;
    [[nodiscard]] bool IsWorker() const noexcept;
    wil::com_ptr_nothrow<ID3D11DeviceContext1> _context;
    wil::com_ptr_nothrow<ID3D11Device> _device;
    wil::com_ptr_nothrow<ID3D11VertexShader> _mipVertex;
    wil::com_ptr_nothrow<ID3D11PixelShader> _mipPixel;
    wil::com_ptr_nothrow<ID3D11SamplerState> _mipSampler;
    wil::com_ptr_nothrow<ID3D11RasterizerState> _mipRasterizer;
    wil::com_ptr_nothrow<ID3D11DepthStencilState> _mipDepth;
    GpuCommandStream* _stream = nullptr;
    CancelToken _cancellation;
    std::uint64_t _uploadBytes = 0;
    std::uint64_t _productionEpoch = 0;
    std::uint32_t _workerThread = 0;
    HRESULT _result = E_PENDING;
};
