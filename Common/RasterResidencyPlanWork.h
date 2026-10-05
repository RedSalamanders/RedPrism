#pragma once

#include "DeferredRelease.h"
#include "Document.h"
#include "RasterUploadLayout.h"

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

// Dormant committed-document preparation. No preview/brush override, utility tile, device allocation, mip
// recording or display publication is included. Those consumers must admit their additional overlapping graph.
struct RasterResidencyPlanOptions final
{
    unsigned int textureLimit = 16384;
    std::uint64_t deviceGeneration = 0;
    std::uint64_t chunkBytes = 1024 * 1024;
    std::uint64_t maximumRowBytes = 128 * 1024;
    // Declared queue allocation and opaque driver allowances, charged to BOTH residency domains. These are
    // conservative policy inputs, not observations of a driver's actual allocation or a responsiveness proof.
    std::uint64_t stagingBytes = 16 * 1024 * 1024;
    std::uint64_t driverHeadroomBytes = 64 * 1024 * 1024;
    bool operator==(const RasterResidencyPlanOptions&) const noexcept = default;
};

struct RasterResidencyOverlap final
{
    std::uint64_t liveCpuBytes = 0;
    std::uint64_t retiredCpuBytes = 0;
    std::uint64_t liveGpuBytes = 0;
    std::uint64_t retiredGpuBytes = 0;
};

// Descriptor only; it captures no document/raster owner. An invalid/overflow bound refuses before capture.
// Use immediately under the owner's document authority, reserve both totals, then capture immutable roots.
class RasterResidencyAdmission final
{
  public:
    [[nodiscard]] static RasterResidencyAdmission Estimate(const CanvasDocument& source,
                                                           RasterResidencyPlanOptions options,
                                                           RasterResidencyOverlap overlap = {}) noexcept;
    [[nodiscard]] bool IsValid() const noexcept
    {
        return _cpuBytes != 0 && _gpuBytes != 0;
    }
    [[nodiscard]] std::uint64_t CpuBytes() const noexcept
    {
        return _cpuBytes;
    }
    [[nodiscard]] std::uint64_t GpuBytes() const noexcept
    {
        return _gpuBytes;
    }
    [[nodiscard]] std::uint64_t DeviceGeneration() const noexcept
    {
        return _options.deviceGeneration;
    }

  private:
    friend class RasterResidencyPlanWork;
    Guid _documentId;
    const void* _layerIdentity = nullptr; // immediate descriptor; never a cache key or owning authority
    std::size_t _layerCount = 0;
    int _width = 0;
    int _height = 0;
    std::uint64_t _sourceBytes = 0;
    std::uint64_t _metadataBytes = 0;
    std::uint64_t _scratchBytes = 0;
    std::uint64_t _surfaceBytes = 0;
    std::uint64_t _lookupBytes = 0;
    std::uint64_t _cpuBytes = 0;
    std::uint64_t _gpuBytes = 0;
    std::size_t _tableSlots = 0;
    RasterResidencyPlanOptions _options;
};

enum class RasterResidencyRole : std::uint8_t
{
    Image,
    Mask
};

struct RasterResidencyBinding final
{
    Guid layerId;
    std::size_t layoutIndex = 0;
    RasterResidencyRole role = RasterResidencyRole::Image;
};

#pragma warning(push)
#pragma warning(disable : 4324)
class RasterResidencyPlanWork final : public DeferredReleaseNode
{
  private:
    struct ConstructionTag final
    {
    };

  public:
    // Live owner consumers keep the admitted deferred-release sink installed until all handles drain.
    // Without that sink, release is inline: this fallback is for worker construction or hidden shutdown only.
    struct Release final
    {
        void operator()(RasterResidencyPlanWork* plan) const noexcept;
    };
    using Handle = std::unique_ptr<RasterResidencyPlanWork, Release>;
    explicit RasterResidencyPlanWork(ConstructionTag) noexcept {}
    RasterResidencyPlanWork(const RasterResidencyPlanWork&) = delete;
    RasterResidencyPlanWork& operator=(const RasterResidencyPlanWork&) = delete;
    RasterResidencyPlanWork(RasterResidencyPlanWork&&) = delete;
    RasterResidencyPlanWork& operator=(RasterResidencyPlanWork&&) = delete;
    // Worker only, AFTER complete two-domain reservation. No materialization, source pixel reads or GPU calls.
    // Source and layout owners survive through the plan's admitted worker retirement lane.
    [[nodiscard]] static Handle Prepare(CanvasDocument source, const RasterResidencyAdmission& admission,
                                        CancelToken cancellation = {});
    [[nodiscard]] const CanvasDocument& Source() const noexcept
    {
        return _source;
    }
    [[nodiscard]] std::span<const RasterUploadLayout> Layouts() const noexcept
    {
        return _layouts;
    }
    [[nodiscard]] std::span<const RasterResidencyBinding> Bindings() const noexcept
    {
        return _bindings;
    }
    [[nodiscard]] const RasterResidencyPlanOptions& Options() const noexcept
    {
        return _options;
    }
    [[nodiscard]] int CanvasScale() const noexcept
    {
        return _canvasScale;
    }
    [[nodiscard]] std::uint64_t LogicalRasterBytes() const noexcept
    {
        return _rasterBytes;
    }
    [[nodiscard]] std::uint64_t SurfaceAllowanceBytes() const noexcept
    {
        return _surfaceBytes;
    }
    [[nodiscard]] std::uint64_t LookupAllowanceBytes() const noexcept
    {
        return _lookupBytes;
    }

  private:
    CanvasDocument _source;
    std::vector<RasterUploadLayout> _layouts;
    std::vector<RasterResidencyBinding> _bindings;
    RasterResidencyPlanOptions _options;
    std::uint64_t _rasterBytes = 0;
    std::uint64_t _surfaceBytes = 0;
    std::uint64_t _lookupBytes = 0;
    int _canvasScale = 1;
};
#pragma warning(pop)
