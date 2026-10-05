#include "RasterResidencyManifestWork.h"

#include <algorithm>
#include <functional>
#include <limits>
#include <utility>

namespace
{
constexpr std::uint64_t kAllocationAllowance = 64 * 1024;
constexpr auto kMaximum = (std::numeric_limits<std::uint64_t>::max)();
constexpr auto kEmpty = (std::numeric_limits<std::size_t>::max)();

[[nodiscard]] bool Add(std::uint64_t& total, std::uint64_t value) noexcept
{
    if (value > kMaximum - total)
        return false;
    total += value;
    return true;
}

[[nodiscard]] unsigned int MipLevels(unsigned int width, unsigned int height) noexcept
{
    unsigned int levels = 1;
    while (width != 1 || height != 1)
    {
        width = (std::max)(1u, width / 2);
        height = (std::max)(1u, height / 2);
        ++levels;
    }
    return levels;
}

[[nodiscard]] std::uint64_t TextureBytes(unsigned int width, unsigned int height, unsigned int levels,
                                         unsigned int pixelBytes) noexcept
{
    std::uint64_t bytes = 0;
    for (unsigned int level = 0; level < levels; ++level)
    {
        bytes += static_cast<std::uint64_t>(width) * height * pixelBytes;
        width = (std::max)(1u, width / 2);
        height = (std::max)(1u, height / 2);
    }
    return bytes;
}
} // namespace

RasterResidencyCpuAdmission RasterResidencyCpuAdmission::Estimate(const CanvasDocument& source,
                                                                  RasterResidencyPlanOptions options,
                                                                  RasterResidencyOverlap overlap) noexcept
{
    // Reuse the existing checked source/container/scratch envelope, not its conservative GPU reservation.
    // The first phase has no GPU owner: live and retired GPU usage is queried for the second admission.
    auto plan =
        RasterResidencyAdmission::Estimate(source, options, {overlap.liveCpuBytes, overlap.retiredCpuBytes, 0, 0});
    if (!plan.IsValid())
        return {};
    const auto count = source.layers.size();
    const auto resources =
        count * 5 + 4; // <= 2 raster, 1 live/group coverage each, 1 lookup per layer + canvases/stack
    std::size_t slots = 1;
    while (slots < count * 2)
        slots *= 2; // count <= 10000, already checked by the plan descriptor
    const std::uint64_t metadata = sizeof(RasterResidencyManifestWork) + 2 * kAllocationAllowance +
                                   static_cast<std::uint64_t>(resources) * sizeof(RasterResidencyResource) +
                                   static_cast<std::uint64_t>(slots) * sizeof(std::size_t);
    std::uint64_t cpu = plan.CpuBytes();
    if (!Add(cpu, metadata))
        return {};
    RasterResidencyCpuAdmission result;
    result._plan = plan;
    result._cpuBytes = cpu;
    result._metadataBytes = metadata;
    result._resourceLimit = resources;
    result._coverageSlots = slots;
    return result;
}

RasterResidencyManifestWork::Handle RasterResidencyManifestWork::Prepare(CanvasDocument source,
                                                                         const RasterResidencyCpuAdmission& admission,
                                                                         CancelToken cancellation,
                                                                         RasterResidencyManifestObserver observer)
{
    if (!admission.IsValid() || cancellation.IsCancelled())
        return {};
    auto plan = RasterResidencyPlanWork::PrepareCpu(std::move(source), admission._plan, cancellation);
    if (!plan || cancellation.IsCancelled())
        return {};
    auto constructed = std::make_unique<RasterResidencyManifestWork>(ConstructionTag{});
    Handle result(constructed.release());
    result->destroy = [](DeferredReleaseNode* node) noexcept
    { delete static_cast<RasterResidencyManifestWork*>(node); };
    result->bytes = plan->bytes;
    if (!Add(result->bytes, admission._metadataBytes))
        return {};
    result->_plan = std::move(plan);
    result->_resources.reserve(admission._resourceLimit);
    std::vector<std::size_t> coverageTable(admission._coverageSlots, kEmpty);
    const std::uint64_t actualMetadata = sizeof(RasterResidencyManifestWork) + 2 * kAllocationAllowance +
                                         result->_resources.capacity() * sizeof(RasterResidencyResource) +
                                         coverageTable.capacity() * sizeof(std::size_t);
    if (actualMetadata > admission._metadataBytes)
        return {};
    const auto append = [&](RasterResidencyResource resource) -> bool
    {
        if (cancellation.IsCancelled() || result->_resources.size() == admission._resourceLimit ||
            !Add(result->_logicalBytes, resource.logicalBytes))
            return false;
        result->_resources.push_back(resource);
        if (observer.checkpoint)
            observer.checkpoint(observer.context, result->_resources.size());
        return !cancellation.IsCancelled();
    };
    const auto& prepared = result->Plan();
    for (std::size_t index = 0; index < prepared.Layouts().size(); ++index)
    {
        const auto& layout = prepared.Layouts()[index];
        const auto format = layout.Source().Format() == RasterFormat::Gray8 ? RasterResidencyResourceFormat::Gray8
                                                                            : RasterResidencyResourceFormat::Bgra8;
        if (!append({RasterResidencyResourceKind::Raster,
                     format,
                     {},
                     index,
                     static_cast<unsigned int>(layout.Width()),
                     static_cast<unsigned int>(layout.Height()),
                     1,
                     layout.MipLevels(),
                     layout.LogicalTextureBytes()}))
            return {};
    }
    const auto& document = prepared.Source();
    const auto scale = prepared.CanvasScale();
    const auto width = static_cast<unsigned int>(1 + (document.width - 1) / scale);
    const auto height = static_cast<unsigned int>(1 + (document.height - 1) / scale);
    const auto canvasLevels = MipLevels(width, height);
    const auto surface = [&](RasterResidencyResourceKind kind, RasterResidencyResourceFormat format,
                             Guid layerId = {}) -> bool
    {
        const auto levels = kind == RasterResidencyResourceKind::Canvas ? canvasLevels : 1u;
        const auto pixelBytes = format == RasterResidencyResourceFormat::Gray8 ? 1u : 4u;
        return append(
            {kind, format, layerId, 0, width, height, 1, levels, TextureBytes(width, height, levels, pixelBytes)});
    };
    if (!surface(RasterResidencyResourceKind::Canvas, RasterResidencyResourceFormat::Bgra8) ||
        !surface(RasterResidencyResourceKind::Canvas, RasterResidencyResourceFormat::Bgra8))
        return {};
    bool needsStack = false;
    for (const auto& layer : document.layers)
    {
        if (cancellation.IsCancelled())
            return {};
        if (layer.maskSourceID)
        {
            // Every upstream link is present in this traversal, including hidden and missing source IDs. The
            // latter need a zero coverage surface, matching LayerCoverage's blank-source behavior.
            needsStack = true;
            const auto id = layer.maskSourceID.value();
            auto slot = std::hash<Guid>{}(id) & (coverageTable.size() - 1);
            while (coverageTable[slot] != kEmpty && result->_resources[coverageTable[slot]].layerId != id)
            {
                if (cancellation.IsCancelled())
                    return {};
                slot = (slot + 1) & (coverageTable.size() - 1);
            }
            if (coverageTable[slot] == kEmpty)
            {
                coverageTable[slot] = result->_resources.size();
                if (!surface(RasterResidencyResourceKind::LayerCoverage, RasterResidencyResourceFormat::Gray8, id))
                    return {};
            }
        }
        // Preparing all enabled group masks also covers their ancestors. Invisible groups may overcount; the
        // role remains separate from live coverage of this same ID. Mask input textures were already bound.
        if (layer.isGroup && layer.mask && layer.mask->enabled &&
            !surface(RasterResidencyResourceKind::GroupCoverage, RasterResidencyResourceFormat::Gray8, layer.id))
            return {};
        if (layer.adjustment)
        {
            const auto kind = layer.adjustment->kind;
            if (!layer.adjustment->IsValid())
                return {};
            if (kind == AdjustmentKind::Grain)
                continue; // grain is scalar constants, with no table/cube texture
            const bool cube = kind == AdjustmentKind::Hsv;
            const auto dimension = cube ? 33u : 256u;
            const auto other = cube ? 33u : 1u;
            const auto bytes = static_cast<std::uint64_t>(dimension) * other * other * 16;
            if (!append(
                    {cube ? RasterResidencyResourceKind::AdjustmentCube : RasterResidencyResourceKind::AdjustmentTable,
                     RasterResidencyResourceFormat::Rgba32Float, layer.id, 0, dimension, other, other, 1, bytes}))
                return {};
        }
    }
    if (needsStack && (!surface(RasterResidencyResourceKind::Stack, RasterResidencyResourceFormat::Bgra8) ||
                       !surface(RasterResidencyResourceKind::Stack, RasterResidencyResourceFormat::Bgra8)))
        return {};
    result->_gpuBytes = result->_logicalBytes;
    if (!Add(result->_gpuBytes, prepared.Options().stagingBytes) ||
        !Add(result->_gpuBytes, prepared.Options().driverHeadroomBytes) || cancellation.IsCancelled())
        return {};
    return result;
}

void RasterResidencyManifestWork::Release::operator()(RasterResidencyManifestWork* manifest) const noexcept
{
    if (manifest && !TryDeferRelease(*manifest, DeferredReleasePolicy::AlwaysOnOwner))
        delete manifest;
}
