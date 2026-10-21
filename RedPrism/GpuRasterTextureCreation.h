#pragma once

#include "Executor.h"
#include "GpuMipSlice.h"
#include "RasterUploadLayout.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <d3d11.h>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/com.h>
#pragma warning(pop)

enum class GpuRasterCreationFailure : std::uint8_t
{
    None,
    Texture,
    ShaderView,
    MipVertexShader,
    MipPixelShader,
    MipSampler,
    MipRasterizer,
    MipDepthStencil,
};

// One admitted resource, not a document graph. Capture on the graphics owner, create on a compute worker,
// acknowledge after producer completion, then borrow/adopt on that same owner. The exact device remains held
// through cancellation/failure/retirement. No immediate context, pixel upload, mip recording or publication.
// Caller admission includes texture/mip bytes, this record and opaque driver headroom. Unsupported driver
// concurrency refuses before any resource allocation; a large owner allocation is never the fallback.
class GpuRasterTextureCreation final
{
  public:
    GpuRasterTextureCreation() noexcept = default;
    ~GpuRasterTextureCreation();
    GpuRasterTextureCreation(const GpuRasterTextureCreation&) = delete;
    GpuRasterTextureCreation& operator=(const GpuRasterTextureCreation&) = delete;
    GpuRasterTextureCreation(GpuRasterTextureCreation&&) = delete;
    GpuRasterTextureCreation& operator=(GpuRasterTextureCreation&&) = delete;

    [[nodiscard]] HRESULT CaptureOwner(ID3D11Device* device, std::uint64_t generation) noexcept;
    [[nodiscard]] HRESULT CreateOnWorker(const RasterUploadLayout& layout, CancelToken cancellation = {}) noexcept;
    // Nonblocking publication handshake. True exactly once after the creator has returned its final access.
    // Callers must additionally join/acknowledge the scheduler producer before destroying this record.
    [[nodiscard]] bool AcknowledgeOwner() noexcept;
    [[nodiscard]] ID3D11Texture2D* TextureOwner() const noexcept;
    [[nodiscard]] ID3D11ShaderResourceView* ShaderViewOwner() const noexcept;
    // Optional parent SRV and next-level RTV pairs plus the mip pipeline, created on the worker.
    // Each owner pump draws at most one child level from its already acknowledged parent.
    [[nodiscard]] bool PrepareMipSlicesOwner() noexcept;
    [[nodiscard]] GpuMipSlice MipSliceOwner(unsigned int slice) const noexcept;
    [[nodiscard]] unsigned int MipSliceCountOwner() const noexcept;
    [[nodiscard]] bool SetMipViewFailureForTest(unsigned int slice, HRESULT error = E_FAIL) noexcept;
    // Drops at most one texture/view/device set. Returns false while creation is still running. Caller has
    // already acknowledged any upload/context use or transferred independent COM owners before retirement.
    [[nodiscard]] bool ReleaseOwner() noexcept;
    [[nodiscard]] std::uint64_t DeviceGeneration() const noexcept
    {
        return _generation;
    }
    [[nodiscard]] HRESULT ThreadingQueryResult() const noexcept
    {
        return _threadingResult;
    }
    [[nodiscard]] bool DriverConcurrentCreates() const noexcept
    {
        return _concurrentCreates;
    }
    [[nodiscard]] unsigned int ResourceCallsOwner() const noexcept;
    [[nodiscard]] HRESULT ResultOwner() const noexcept;
    // Owner-only restrictions/faults before Run. They can refuse a supported route, never fabricate support.
    [[nodiscard]] bool RestrictConcurrencyForTest() noexcept;
    [[nodiscard]] bool SetFailureForTest(GpuRasterCreationFailure point, HRESULT error = E_FAIL) noexcept;

  private:
    enum class State : std::uint8_t
    {
        Empty,
        Captured,
        Running,
        Finished,
        Acknowledged,
        Retired
    };
    [[nodiscard]] bool IsOwner() const noexcept;
    wil::com_ptr_nothrow<ID3D11Device> _device;
    wil::com_ptr_nothrow<ID3D11Texture2D> _texture;
    wil::com_ptr_nothrow<ID3D11ShaderResourceView> _view;
    static constexpr unsigned int kMipSliceCapacity = D3D11_REQ_MIP_LEVELS - 1;
    std::array<wil::com_ptr_nothrow<ID3D11ShaderResourceView>, kMipSliceCapacity> _mipViews;
    std::array<wil::com_ptr_nothrow<ID3D11RenderTargetView>, kMipSliceCapacity> _mipTargets;
    std::array<unsigned int, kMipSliceCapacity> _mipWidths{};
    std::array<unsigned int, kMipSliceCapacity> _mipHeights{};
    wil::com_ptr_nothrow<ID3D11VertexShader> _mipVertex;
    wil::com_ptr_nothrow<ID3D11PixelShader> _mipPixel;
    wil::com_ptr_nothrow<ID3D11SamplerState> _mipSampler;
    wil::com_ptr_nothrow<ID3D11RasterizerState> _mipRasterizer;
    wil::com_ptr_nothrow<ID3D11DepthStencilState> _mipDepth;
    std::atomic<State> _state{State::Empty};
    std::uint64_t _generation = 0;
    std::uint32_t _ownerThread = 0;
    HRESULT _threadingResult = E_PENDING;
    HRESULT _result = E_PENDING;
    HRESULT _failureResult = E_FAIL;
    GpuRasterCreationFailure _failure = GpuRasterCreationFailure::None;
    unsigned int _resourceCalls = 0;
    unsigned int _mipCount = 0;
    unsigned int _mipFailureSlice = kMipSliceCapacity;
    HRESULT _mipFailureResult = E_FAIL;
    bool _prepareMipSlices = false;
    bool _concurrentCreates = false;
};
