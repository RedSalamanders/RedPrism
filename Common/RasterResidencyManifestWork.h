#pragma once

#include "RasterResidencyPlanWork.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

// O(1), owner-free CPU planning descriptor. Reserve CpuBytes before capturing the immutable source. GPU
// admission follows worker preparation; neither this descriptor nor Prepare permits GPU allocation/publication.
class RasterResidencyCpuAdmission final
{
  public:
    [[nodiscard]] static RasterResidencyCpuAdmission Estimate(const CanvasDocument& source,
                                                              RasterResidencyPlanOptions options,
                                                              RasterResidencyOverlap overlap = {}) noexcept;
    [[nodiscard]] bool IsValid() const noexcept
    {
        return _cpuBytes != 0;
    }
    [[nodiscard]] std::uint64_t CpuBytes() const noexcept
    {
        return _cpuBytes;
    }

  private:
    friend class RasterResidencyManifestWork;
    RasterResidencyAdmission _plan;
    std::uint64_t _cpuBytes = 0;
    std::uint64_t _metadataBytes = 0;
    std::size_t _resourceLimit = 0;
    std::size_t _coverageSlots = 0;
};

enum class RasterResidencyResourceKind : std::uint8_t
{
    Raster,
    Canvas,
    Stack,
    LayerCoverage,
    GroupCoverage,
    AdjustmentTable,
    AdjustmentCube
};

enum class RasterResidencyResourceFormat : std::uint8_t
{
    Bgra8,
    Gray8,
    Rgba32Float
};

// Device-independent complete allocation description for committed display inputs. Raster layout indices keep
// their owning immutable source. Coverage keys include their role: one ID may need both group and live coverage.
// Preview, brush, utility, pipeline/device objects and physical driver allocation are separate admitted graphs.
struct RasterResidencyResource final
{
    RasterResidencyResourceKind kind = RasterResidencyResourceKind::Raster;
    RasterResidencyResourceFormat format = RasterResidencyResourceFormat::Bgra8;
    Guid layerId;
    std::size_t layoutIndex = 0;
    unsigned int width = 0;
    unsigned int height = 0;
    unsigned int depth = 1;
    unsigned int mipLevels = 1;
    std::uint64_t logicalBytes = 0;
};

struct RasterResidencyManifestObserver final
{
    void (*checkpoint)(void* context, std::size_t resourceCount) noexcept = nullptr;
    void* context = nullptr;
};

#pragma warning(push)
#pragma warning(disable : 4324)
class RasterResidencyManifestWork final : public DeferredReleaseNode
{
  private:
    struct ConstructionTag final
    {
    };

  public:
    struct Release final
    {
        void operator()(RasterResidencyManifestWork* manifest) const noexcept;
    };
    using Handle = std::unique_ptr<RasterResidencyManifestWork, Release>;
    explicit RasterResidencyManifestWork(ConstructionTag) noexcept {}
    RasterResidencyManifestWork(const RasterResidencyManifestWork&) = delete;
    RasterResidencyManifestWork& operator=(const RasterResidencyManifestWork&) = delete;
    RasterResidencyManifestWork(RasterResidencyManifestWork&&) = delete;
    RasterResidencyManifestWork& operator=(RasterResidencyManifestWork&&) = delete;
    // Worker only, after complete CPU reservation. Describes resources without reading/materializing pixels,
    // constructing adjustment values or calling GPU APIs. The owner reserves RequiredGpuBytes from the real
    // adapter budget before any GPU creation; CPU/source charges survive both admissions and retirement.
    [[nodiscard]] static Handle Prepare(CanvasDocument source, const RasterResidencyCpuAdmission& admission,
                                        CancelToken cancellation = {}, RasterResidencyManifestObserver observer = {});
    [[nodiscard]] const RasterResidencyPlanWork& Plan() const noexcept
    {
        return *_plan;
    }
    [[nodiscard]] std::span<const RasterResidencyResource> Resources() const noexcept
    {
        return _resources;
    }
    [[nodiscard]] std::uint64_t LogicalGpuBytes() const noexcept
    {
        return _logicalBytes;
    }
    // New graph only, including declared staging/driver allowances. Live/retired GPU owners remain charged by
    // GpuResidencyBudget and its actual usage snapshot; this scalar is not a driver allocation measurement.
    [[nodiscard]] std::uint64_t RequiredGpuBytes() const noexcept
    {
        return _gpuBytes;
    }

  private:
    RasterResidencyPlanWork::Handle _plan;
    std::vector<RasterResidencyResource> _resources;
    std::uint64_t _logicalBytes = 0;
    std::uint64_t _gpuBytes = 0;
};
#pragma warning(pop)
