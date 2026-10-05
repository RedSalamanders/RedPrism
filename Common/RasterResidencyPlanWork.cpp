#include "RasterResidencyPlanWork.h"

#include <algorithm>
#include <limits>
#include <utility>

namespace
{
constexpr auto kMaximum = (std::numeric_limits<std::uint64_t>::max)();
constexpr std::uint64_t kAllocationAllowance = 64 * 1024;
// RGBA32F 33^3 color cube plus a 256-entry RGBA32F table for EVERY record. Overcounts non-adjustments.
constexpr std::uint64_t kLookupPerLayer = (33ull * 33 * 33 + 256) * 16;

[[nodiscard]] bool Add(std::uint64_t& total, std::uint64_t value) noexcept
{
    if (value > kMaximum - total)
        return false;
    total += value;
    return true;
}
[[nodiscard]] bool Multiply(std::uint64_t left, std::uint64_t right, std::uint64_t& result) noexcept
{
    if (left && right > kMaximum / left)
        return false;
    result = left * right;
    return true;
}
[[nodiscard]] int ScaleFor(int width, int height, unsigned int limit) noexcept
{
    int scale = 1;
    while (static_cast<unsigned int>(1 + (width - 1) / scale) > limit ||
           static_cast<unsigned int>(1 + (height - 1) / scale) > limit)
        scale *= 2; // admitted document/raster sides <= 30000; no signed overflow
    return scale;
}
[[nodiscard]] std::uint64_t MipPixels(int width, int height) noexcept
{
    std::uint64_t total = 0;
    for (;;)
    {
        total += static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height);
        if (width == 1 && height == 1)
            return total;
        width = (std::max)(1, width / 2);
        height = (std::max)(1, height / 2);
    }
}
[[nodiscard]] std::size_t HashIdentity(const void* identity) noexcept
{
    auto value = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(identity));
    value ^= value >> 30;
    value *= 0xbf58476d1ce4e5b9ull;
    value ^= value >> 27;
    value *= 0x94d049bb133111ebull;
    value ^= value >> 31;
    return static_cast<std::size_t>(value);
}
} // namespace

RasterResidencyAdmission RasterResidencyAdmission::Estimate(const CanvasDocument& source,
                                                            RasterResidencyPlanOptions options,
                                                            RasterResidencyOverlap overlap) noexcept
{
    if (source.width < 1 || source.height < 1 || source.width > kMaxCanvasSide || source.height > kMaxCanvasSide ||
        source.layers.size() > kMaxLayerCount || options.textureLimit == 0 || options.textureLimit > 16384 ||
        !options.deviceGeneration || !options.chunkBytes || !options.maximumRowBytes ||
        options.chunkBytes > options.stagingBytes || !options.driverHeadroomBytes)
        return {};
    const auto sourceBytes = source.RetirementMemoryBytes();
    if (!sourceBytes || source.layers.size() > (std::numeric_limits<std::size_t>::max)() / 4)
        return {};

    RasterResidencyAdmission result;
    const auto maximumEntries = static_cast<std::uint64_t>(source.layers.size()) * 2;
    std::size_t slots = 1;
    const auto neededSlots = static_cast<std::size_t>(maximumEntries) * 2;
    while (slots < neededSlots)
    {
        if (slots > (std::numeric_limits<std::size_t>::max)() / 2)
            return {};
        slots *= 2;
    }
    std::uint64_t metadata = sizeof(RasterResidencyPlanWork) + 4 * kAllocationAllowance;
    std::uint64_t entriesBytes = 0;
    std::uint64_t tableBytes = 0;
    if (!Multiply(maximumEntries, sizeof(RasterUploadLayout) + sizeof(RasterResidencyBinding), entriesBytes) ||
        !Multiply(static_cast<std::uint64_t>(slots), sizeof(std::size_t), tableBytes) || !Add(metadata, entriesBytes) ||
        !Add(metadata, tableBytes))
        return {};
    // One serial upload worker: widest possible source row, reduced-channel sums and allocator rounding.
    std::uint64_t scratch = options.maximumRowBytes;
    if (!Multiply(scratch, sizeof(std::uint64_t), scratch) ||
        !Add(scratch, static_cast<std::uint64_t>(kMaxCanvasSide) * 4) || !Add(scratch, 2 * kAllocationAllowance))
        return {};

    const int canvasScale = ScaleFor(source.width, source.height, options.textureLimit);
    const int width = 1 + (source.width - 1) / canvasScale;
    const int height = 1 + (source.height - 1) / canvasScale;
    const auto pixels = static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height);
    // Two mipmapped BGRA canvases, two nonmipped BGRA stack targets, and up to TWO R8 coverage targets/record.
    std::uint64_t surfaces = MipPixels(width, height) * 8 + pixels * 8;
    std::uint64_t coverage = 0;
    std::uint64_t lookups = 0;
    std::uint64_t rasterUpper = 0;
    if (!Multiply(maximumEntries, pixels, coverage) || !Add(surfaces, coverage) ||
        !Multiply(static_cast<std::uint64_t>(source.layers.size()), kLookupPerLayer, lookups) ||
        !Multiply(sourceBytes, 2, rasterUpper))
        return {};
    // Every source bound includes its complete logical dense bytes, including sparse assembly potential.
    // A rectangular mip chain occupies <= 2*base bytes, even for 1xN. Shared/thumbnail/shape data overcounts.
    std::uint64_t cpu = sourceBytes;
    std::uint64_t gpu = rasterUpper;
    if (!Add(cpu, metadata) || !Add(cpu, scratch) || !Add(cpu, options.stagingBytes) ||
        !Add(cpu, options.driverHeadroomBytes) || !Add(cpu, overlap.liveCpuBytes) ||
        !Add(cpu, overlap.retiredCpuBytes) || !Add(gpu, surfaces) || !Add(gpu, lookups) ||
        !Add(gpu, options.stagingBytes) || !Add(gpu, options.driverHeadroomBytes) || !Add(gpu, overlap.liveGpuBytes) ||
        !Add(gpu, overlap.retiredGpuBytes))
        return {};
    result._documentId = source.id;
    result._layerIdentity = source.layers.Identity();
    result._layerCount = source.layers.size();
    result._width = source.width;
    result._height = source.height;
    result._sourceBytes = sourceBytes;
    result._metadataBytes = metadata;
    result._scratchBytes = scratch;
    result._surfaceBytes = surfaces;
    result._lookupBytes = lookups;
    result._cpuBytes = cpu;
    result._gpuBytes = gpu;
    result._tableSlots = slots;
    result._options = options;
    return result;
}

RasterResidencyPlanWork::Handle RasterResidencyPlanWork::Prepare(CanvasDocument source,
                                                                 const RasterResidencyAdmission& admission,
                                                                 CancelToken cancellation)
{
    if (cancellation.IsCancelled() || !admission.IsValid() || source.id != admission._documentId ||
        source.layers.Identity() != admission._layerIdentity || source.layers.size() != admission._layerCount ||
        source.width != admission._width || source.height != admission._height ||
        source.RetirementMemoryBytes() != admission._sourceBytes)
        return {};
    auto constructed = std::make_unique<RasterResidencyPlanWork>(ConstructionTag{});
    Handle plan(constructed.release());
    plan->destroy = [](DeferredReleaseNode* value) noexcept { delete static_cast<RasterResidencyPlanWork*>(value); };
    plan->bytes = admission._sourceBytes + admission._metadataBytes; // already checked by Estimate
    plan->_source = std::move(source);
    plan->_options = admission._options;
    plan->_canvasScale = ScaleFor(plan->_source.width, plan->_source.height, plan->_options.textureLimit);
    plan->_surfaceBytes = admission._surfaceBytes;
    plan->_lookupBytes = admission._lookupBytes;
    const auto entryLimit = admission._layerCount * 2;
    plan->_layouts.reserve(entryLimit);
    plan->_bindings.reserve(entryLimit);
    constexpr auto empty = (std::numeric_limits<std::size_t>::max)();
    std::vector<std::size_t> table(admission._tableSlots, empty);
    std::uint64_t actualMetadata = sizeof(RasterResidencyPlanWork) + 4 * kAllocationAllowance;
    std::uint64_t allocation = 0;
    if (!Multiply(plan->_layouts.capacity(), sizeof(RasterUploadLayout), allocation) ||
        !Add(actualMetadata, allocation) ||
        !Multiply(plan->_bindings.capacity(), sizeof(RasterResidencyBinding), allocation) ||
        !Add(actualMetadata, allocation) || !Multiply(table.capacity(), sizeof(std::size_t), allocation) ||
        !Add(actualMetadata, allocation) || actualMetadata > admission._metadataBytes)
        return {};

    const auto bind = [&](const Guid& layerId, const Raster& raster, RasterResidencyRole role) -> bool
    {
        if (!raster.HasStorage())
            return true;
        if (raster.Width() < 1 || raster.Height() < 1 || raster.Width() > kMaxCanvasSide ||
            raster.Height() > kMaxCanvasSide || cancellation.IsCancelled())
            return false;
        const auto mask = table.size() - 1;
        auto slot = HashIdentity(raster.Identity()) & mask;
        while (table[slot] != empty && plan->_layouts[table[slot]].Source().Identity() != raster.Identity())
        {
            if (cancellation.IsCancelled())
                return false;
            slot = (slot + 1) & mask;
        }
        if (table[slot] == empty)
        {
            const int scale = ScaleFor(raster.Width(), raster.Height(), plan->_options.textureLimit);
            auto layout = RasterUploadLayout::Make(raster, 0, 0, raster.Width(), raster.Height(), scale,
                                                   plan->_options.textureLimit, plan->_options.chunkBytes,
                                                   plan->_options.maximumRowBytes, true);
            if (!layout.IsValid() || layout.ReductionScratchBytes() > admission._scratchBytes ||
                !Add(plan->_rasterBytes, layout.LogicalTextureBytes()))
                return false;
            table[slot] = plan->_layouts.size();
            plan->_layouts.push_back(std::move(layout));
        }
        plan->_bindings.push_back({layerId, table[slot], role});
        return true;
    };
    for (const auto& layer : plan->_source.layers)
    {
        if (cancellation.IsCancelled() ||
            (layer.asset && !bind(layer.id, layer.asset->image, RasterResidencyRole::Image)) ||
            (layer.mask && !bind(layer.id, layer.mask->asset.image, RasterResidencyRole::Mask)))
            return {};
    }
    if (cancellation.IsCancelled() || plan->_rasterBytes > admission._sourceBytes * 2)
        return {};
    return plan;
}

void RasterResidencyPlanWork::Release::operator()(RasterResidencyPlanWork* plan) const noexcept
{
    if (plan && !TryDeferRelease(*plan, DeferredReleasePolicy::AlwaysOnOwner))
        delete plan;
}
