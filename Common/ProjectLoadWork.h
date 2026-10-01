#pragma once

#include "ProjectStore.h"

#include <algorithm>
#include <cstdint>

// Additional peak for bounded manifest/header inspection. No pixel decoder is created by that phase.
// Manifest storage covers input bytes, yyjson's bounded reader allocation, names and conversion temporaries.
// Per-layer storage covers records, document-vector growth, copied curves and validation bookkeeping.
[[nodiscard]] inline std::uint64_t ProjectLoadMetadataMemoryBytes(const ProjectLoadLimits& limits,
                                                                  std::size_t pathUnits) noexcept
{
    if (pathUnits == 0 || pathUnits > 32767 || limits.layerCount > static_cast<std::size_t>(kMaxLayerCount) ||
        limits.manifestBytes == 0 || limits.manifestBytes > kMaxProjectManifestBytes ||
        limits.encodedAssetBytes > kMaxProjectEncodedAssetBytes || limits.imagePixels < 0 || limits.maskPixels < 0 ||
        limits.imagePixels > kMaxLayerPixels || limits.maskPixels > kMaxMaskPixels)
        return 0;
    return 1024ull * 1024 + 64ull * limits.manifestBytes +
           limits.layerCount * (8ull * (sizeof(ImageLayer) + 4 * 32 * sizeof(CurvePoint)) + 4096) +
           (static_cast<std::uint64_t>(pathUnits) + 128) * 16 * sizeof(wchar_t);
}

// Probe values become enforced ceilings for the reopened decode graph. Changed input cannot grow its manifest,
// layer count, aggregate pixel outputs or retained encoded ZIP buffer beyond this reservation.
// Opaque WIC/libzip internals remain in global headroom; this is a declared peak, not a measured exact footprint.
[[nodiscard]] inline std::uint64_t ProjectLoadMemoryBytes(const ProjectLoadLimits& limits,
                                                          std::size_t pathUnits) noexcept
{
    const auto metadata = ProjectLoadMetadataMemoryBytes(limits, pathUnits);
    if (metadata == 0)
        return 0;
    const auto color = static_cast<std::uint64_t>(limits.imagePixels) * 4;
    const auto masks = static_cast<std::uint64_t>(limits.maskPixels);
    const auto maskThumbnails = (std::min)(masks, limits.layerCount * 96ull * 96);
    // Large PixelBuffers round by <16 MiB and have at least 16 MiB of useful pixels, so 2x logical bytes
    // bounds all output size classes. Small buffers add one 64-byte record each; thumbnails may briefly copy.
    return metadata + 2 * (color + masks) + 2 * maskThumbnails + limits.layerCount * 4ull * 64 +
           limits.encodedAssetBytes;
}
