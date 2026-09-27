#pragma once

#include "BrushStroke.h" // GridRect and the raster model.
#include "Executor.h"

#include <cstddef>
#include <cstdint>
#include <span>

// Worker-side reads never assemble or wait for a shared sparse cache. The explicit traversal stack bounds
// temporary storage; ordinary raster producers have one level of contiguous tiles. Deeper-than-limit graphs
// fail the operation, without publishing a partial result. Padding in the destination is never written.
inline constexpr std::size_t kRasterWorkDepthLimit = 64;
[[nodiscard]] bool ReadRasterRegionForWork(const Raster& image, GridRect region, std::span<std::uint8_t> output,
                                           std::size_t outputStride, std::uint8_t outsideFill,
                                           CancelToken cancellation = {}) noexcept;

// Empty on cancellation/allocation failure/invalid input. Every output byte is initialized before publication.
[[nodiscard]] PixelBuffer CopyRasterPixelsForWork(const Raster& image, CancelToken cancellation = {}) noexcept;
// Shares an already published contiguous cache; otherwise builds privately without mutating the source cache.
// Storage-header allocation may throw; the owning job boundary translates bad_alloc to a failed operation.
[[nodiscard]] Raster ContiguousRasterForWork(const Raster& image, CancelToken cancellation = {});
[[nodiscard]] Raster CropRasterForWork(const Raster& image, GridRect region, std::uint8_t outsideFill,
                                       CancelToken cancellation = {});
// Worker equivalents of Raster::MakeThumbnail and CarriedMask. They check cancellation before returning even
// shared/no-op results and never publish partially initialized pixels.
[[nodiscard]] Raster ThumbnailRasterForWork(const Raster& source, int maxSide = 96, CancelToken cancellation = {});
[[nodiscard]] Raster CarriedMaskForWork(const Raster& mask, int oldWidth, int oldHeight, int x, int y, int width,
                                        int height, std::uint8_t outside, CancelToken cancellation = {});
