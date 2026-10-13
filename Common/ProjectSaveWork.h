#pragma once

#include "Document.h"
#include "OutputPathLimits.h"

#include <algorithm>
#include <cstdint>
#include <limits>

// Declared save peak for owned snapshot metadata, manifest construction/write buffers, file-source paths and
// one serial codec. Published raster/selection inputs already resident are shared, never charged twice.
// WIC/libzip allocator bookkeeping and opaque codec internals remain covered by the process admission headroom.
// Zero rejects invalid dimensions/count/path length or arithmetic overflow before an owning graph is created.
[[nodiscard]] inline std::uint64_t ProjectSaveMemoryBytes(const CanvasDocument& document,
                                                          std::size_t pathUnits) noexcept
{
    if (pathUnits == 0 || pathUnits > kMaxProjectOutputPathUnits ||
        document.layers.size() > static_cast<std::size_t>(kMaxLayerCount) || document.width < 1 ||
        document.height < 1 || document.width > kMaxCanvasSide || document.height > kMaxCanvasSide)
        return 0;
    // Bounded manifest payload, JSON text copies and codec/scratch allowance (including the 64 KiB directory
    // cleanup buffer allocated before staging). Per-record nodes and dynamic
    // strings are charged below even for documents whose serialized manifest will ultimately be refused.
    std::uint64_t bytes = 16ull * 1024 * 1024;
    const auto add = [&](std::uint64_t value) noexcept
    {
        if (value > (std::numeric_limits<std::uint64_t>::max)() - bytes)
            return false;
        bytes += value;
        return true;
    };
    // Constant-time borrowed summary of the exact immutable root. No layer, curve, name, raster or
    // sparse graph is visited, copied or materialized before admission. Seal prepared this projection.
    const auto& plan = document.layers.SaveMemoryPlan();
    if (!plan.valid || !add(plan.metadataBytes))
        return 0;
    // Two path copies per libzip source plus exclusive staging/member names and fixed source bookkeeping.
    // Asset files are streamed; no 512 MiB encoded vector per source is retained.
    const auto sourceBytes = (static_cast<std::uint64_t>(pathUnits) + 128) * 4 * sizeof(wchar_t) + 4096;
    if (plan.assetCount > (std::numeric_limits<std::uint64_t>::max)() / sourceBytes)
        return 0;
    return add(plan.largestAssetBytes) && add(plan.assetCount * sourceBytes) && add(sourceBytes * 8) ? bytes : 0;
}
