#pragma once

#include "GpuResidencyBudget.h"
#include "LassoDraftRasterWork.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <d3d11.h>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/com.h>
#pragma warning(pop)

// One worker-created immutable premultiplied BGRA texture. The original parent admits the complete CPU graph and this
// GPU reservation before capture. Initial-data copies and all resource creation run on the worker.
// The parent joins creation before Publish, excludes future borrows at Detach, acknowledges the final
// original-context draw, then joins worker retirement before CompleteRetirement. No method supplies a
// fence acknowledgement or a producer join. Device removal alone never authorizes unrelated retirement.
class PreparedLassoDraftGpu final
{
  public:
    using GridKey = PreparedLassoDraftRaster::GridKey;
    struct Views final
    {
        ID3D11ShaderResourceView* color = nullptr;
        unsigned int width = 0;
        unsigned int height = 0;
        [[nodiscard]] explicit operator bool() const noexcept
        {
            return color != nullptr;
        }
    };
    struct Observer final
    {
        void* context = nullptr;
        void (*afterResourceCall)(void*, unsigned int) noexcept = nullptr;
    };
    PreparedLassoDraftGpu() noexcept = default;
    ~PreparedLassoDraftGpu();
    PreparedLassoDraftGpu(const PreparedLassoDraftGpu&) = delete;
    PreparedLassoDraftGpu& operator=(const PreparedLassoDraftGpu&) = delete;
    [[nodiscard]] static std::int32_t GpuMemoryBytes(unsigned int width, unsigned int height,
                                                     std::uint64_t& output) noexcept;
    // A refusal keeps the caller's reservation and this empty graph unchanged. Capture creates no resource.
    [[nodiscard]] HRESULT CaptureOwner(ID3D11Device* device, ID3D11DeviceContext* immediate, std::uint64_t generation,
                                       GpuResidencyReservation& reservation) noexcept;
    [[nodiscard]] HRESULT CreateOnWorker(PreparedLassoDraftRaster raster, CancelToken cancellation = {},
                                         Observer observer = {}) noexcept;
    // All source/view/device values must still match the current requested display. A stale candidate
    // remains unpublished and retains its original charge for worker retirement.
    [[nodiscard]] HRESULT PublishOwner(ID3D11Device* device, ID3D11DeviceContext* immediate, std::uint64_t generation,
                                       const BoundedLassoGesture::Display& source, const GridKey& grid) noexcept;
    // One bounded color-view borrow. No allocation, COM query, resource creation, pixel copying or point walk.
    [[nodiscard]] Views BorrowOwner(ID3D11Device* device, ID3D11DeviceContext* immediate, std::uint64_t generation,
                                    const BoundedLassoGesture::Display& source, const GridKey& grid) const noexcept;
    [[nodiscard]] bool DetachOwner() noexcept;
    [[nodiscard]] bool RetireDetachedOnWorker() noexcept;
    [[nodiscard]] bool RetireUnpublishedOnWorker() noexcept;
    [[nodiscard]] bool CompleteRetirementOwner() noexcept;
    [[nodiscard]] std::uint64_t ReservedBytesOwner() const noexcept;
    [[nodiscard]] unsigned int ResourceCallsOwner() const noexcept;
    [[nodiscard]] HRESULT ResultOwner() const noexcept;
    // Refusal-only diagnostics installed on the owner before Run; never manufacture driver support.
    [[nodiscard]] bool RestrictConcurrencyForTest() noexcept;
    [[nodiscard]] bool SetResourceFailureForTest(unsigned int call, HRESULT error = E_FAIL) noexcept;

  private:
    enum class Phase : std::uint8_t
    {
        Empty,
        Captured,
        Creating,
        Produced,
        Published,
        Detached,
        Retiring,
        Retired
    };
    [[nodiscard]] bool IsOwner() const noexcept;
    [[nodiscard]] bool Matches(ID3D11Device* device, ID3D11DeviceContext* immediate, std::uint64_t generation,
                               const BoundedLassoGesture::Display& source, const GridKey& grid) const noexcept;
    [[nodiscard]] bool RetireForWork(Phase expected) noexcept;
    // Declaration order retains admission after graphics and CPU-source owners; actual cleanup is explicit.
    GpuResidencyReservation _reservation;
    BoundedLassoGesture::Display _source;
    GridKey _grid;
    wil::com_ptr_nothrow<ID3D11Device> _device;
    wil::com_ptr_nothrow<ID3D11DeviceContext> _immediate;
    wil::com_ptr_nothrow<ID3D11Texture2D> _texture;
    wil::com_ptr_nothrow<ID3D11ShaderResourceView> _view;
    std::atomic<Phase> _phase{Phase::Empty};
    std::uint64_t _generation = 0;
    std::uint32_t _ownerThread = 0;
    HRESULT _result = E_PENDING;
    HRESULT _failureResult = E_FAIL;
    unsigned int _failureCall = 2;
    unsigned int _resourceCalls = 0;
    bool _concurrentCreates = false;
};
