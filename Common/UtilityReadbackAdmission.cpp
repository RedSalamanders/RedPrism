#include "UtilityReadbackAdmission.h"

#include <algorithm>
#include <limits>

namespace UtilityReadbackAdmission
{
namespace
{
constexpr auto kMaximum = (std::numeric_limits<std::uint64_t>::max)();
constexpr std::uint64_t kAllocationAllowance = 64 * 1024;
constexpr std::uint64_t kFixedCpuBytes = 8 * 1024 * 1024;
constexpr std::uint64_t kDriverHeadroomBytes = 64 * 1024 * 1024;
constexpr std::uint64_t kStagingBytes = 64 * 1024 * 1024;
constexpr std::uint64_t kLookupPerLayer = (33ull * 33 * 33 + 256) * 16;
bool AddProduct(std::uint64_t& total, std::uint64_t count, std::uint64_t size) noexcept
{
    if ((size && count > kMaximum / size) || count * size > kMaximum - total)
        return false;
    total += count * size;
    return true;
}
} // namespace

std::optional<Envelope> Estimate(const Descriptor& descriptor) noexcept
{
    if (descriptor.width < 1 || descriptor.height < 1 || descriptor.width > kMaxCanvasSide ||
        descriptor.height > kMaxCanvasSide || descriptor.layerCount > kMaxLayerCount || !descriptor.tileSide ||
        descriptor.tileSide > 2048 || !descriptor.sourceBytes)
        return {};
    const auto retained = descriptor.sourceBytes;
    const auto nodes = descriptor.rasterNodes;
    const auto layers = descriptor.layerCount;
    const auto tileSide = descriptor.tileSide;
    const auto additionalCpuBytes = descriptor.additionalCpuBytes;
    const auto software = descriptor.software;
    const auto width = (std::min)(static_cast<std::uint64_t>(descriptor.width), std::uint64_t{tileSide});
    const auto height = (std::min)(static_cast<std::uint64_t>(descriptor.height), std::uint64_t{tileSide});
    const auto pixels = width * height; // validated sides <=2048
    std::uint64_t logical = 0;
    // Source includes dense logical bytes for every sparse storage; sharing/thumbnail/shape records overcount.
    // <=2x base for a rectangular mip chain, and old/new regional cache overlap: at most4x the source bound.
    // Utility caches prune unused regions only after the next ComposeView; this overlap is admitted here.
    if (!AddProduct(logical, retained, 4) || !AddProduct(logical, pixels, 16) ||
        !AddProduct(logical, layers, pixels * 2) || !AddProduct(logical, layers, kLookupPerLayer))
        return {};
    std::uint64_t gpu = logical;
    // Two generations of raster/region resources, two canvas + two stack targets and two coverage/lookup
    // objects per record. Physical driver allocation is queried separately; these are policy allowances.
    if (!AddProduct(gpu, nodes, 2 * kAllocationAllowance) || !AddProduct(gpu, layers, 4 * kAllocationAllowance) ||
        !AddProduct(gpu, 4, kAllocationAllowance) || !AddProduct(gpu, 1, kStagingBytes + kDriverHeadroomBytes))
        return {};
    std::uint64_t cpu = kFixedCpuBytes;
    // Immutable input and potential sparse assembly, reusable region/coverage scratch, bounded three-list
    // uploads and CPU container/control overlap. The consumer's separate bound is supplied before capture.
    if (!AddProduct(cpu, retained, 2) || !AddProduct(cpu, pixels, 4) || !AddProduct(cpu, nodes, 512) ||
        !AddProduct(cpu, layers, 4096) || !AddProduct(cpu, 1, 3 * 1024 * 1024) ||
        !AddProduct(cpu, 1, additionalCpuBytes) || (software && !AddProduct(cpu, 1, gpu)))
        return {};
    return Envelope{retained, nodes, cpu, gpu, logical, tileSide};
}
std::optional<Envelope> Estimate(const CanvasDocument& source, std::uint32_t tileSide, std::uint64_t additionalCpuBytes,
                                 bool software) noexcept
{
    const auto bounds = source.layers.OwnershipBounds();
    if (!bounds && source.layers.Identity())
        return {};
    return Estimate(Descriptor{source.width, source.height, static_cast<std::uint64_t>(source.layers.size()),
                               source.RetirementMemoryBytes(), bounds ? bounds->rasterNodes : 0, tileSide,
                               additionalCpuBytes, software});
}
} // namespace UtilityReadbackAdmission
