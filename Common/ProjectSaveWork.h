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
    std::uint64_t largestAsset = 0;
    std::uint64_t assets = 0;
    const auto asset = [&](const RasterAsset& value) noexcept
    {
        if (!value.image.HasStorage() || value.image.Width() < 1 || value.image.Height() < 1 ||
            value.image.Width() > kMaxCanvasSide || value.image.Height() > kMaxCanvasSide ||
            value.image.PixelCount() > kMaxLayerPixels)
            return false;
        largestAsset =
            (std::max)(largestAsset, static_cast<std::uint64_t>(value.image.Stride()) * value.image.Height());
        ++assets;
        const auto units = static_cast<std::uint64_t>(value.name.size());
        return units <= ((std::numeric_limits<std::uint64_t>::max)() - 128) / 32 && add(units * 32 + 128);
    };
    for (const auto& layer : document.layers)
    {
        if (layer.adjustment)
            for (const auto& channel : layer.adjustment->curves.channels)
                if (channel.size() > 32)
                    return 0;
        const auto units = static_cast<std::uint64_t>(layer.name.size());
        // Covers the snapshot, yyjson mutable per-layer values and pretty JSON expansion, including 4x32
        // curve points and every optional adjustment payload. The manifest's 4 MiB publication cap remains.
        if (units > (std::numeric_limits<std::uint64_t>::max)() / 32 || !add(units * 32) ||
            !add(sizeof(ImageLayer) + 64 * 1024) || (layer.asset && !asset(layer.asset.value())) ||
            (layer.mask && !asset(layer.mask->asset)))
            return 0;
    }
    // Two path copies per libzip source plus exclusive staging/member names and fixed source bookkeeping.
    // Asset files are streamed; no 512 MiB encoded vector per source is retained.
    const auto sourceBytes = (static_cast<std::uint64_t>(pathUnits) + 128) * 4 * sizeof(wchar_t) + 4096;
    return add(largestAsset) && add(assets * sourceBytes) && add(sourceBytes * 8) ? bytes : 0;
}
