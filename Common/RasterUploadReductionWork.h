#pragma once

#include "RasterUploadWork.h"

#include <span>

// The source crop fixes the block grid. Output windows address that grid, so separate upload chunks have the
// same texels as one complete reduction, including partial blocks at the crop's right and bottom edges.
struct RasterUploadBlockRegion final
{
    int sourceX = 0;
    int sourceY = 0;
    int sourceWidth = 0;
    int sourceHeight = 0;
    int scale = 1;
    int outputX = 0;
    int outputY = 0;
    int outputWidth = 0;
    int outputHeight = 0;
};

// Worker-only encoded/premultiplied byte averaging: (sum + count / 2) / count, as ReduceBlocks. No allocation or
// materialization. For scale > 1 the caller lends one source-span row (clipped outputWidth * scale * channels)
// and outputWidth * channels uint64 sums. Scratch, destination and the immutable source must be disjoint and
// remain owned through acknowledgement. Scale 1 needs no scratch. Invalid input/precancel writes nothing;
// later cancellation leaves unpublished scratch/output that the consumer must discard. The observer reports
// completed output rows. Cancellation is also checked between source rows and at most 4096 source pixels apart.
[[nodiscard]] RasterUploadWorkStatus ReduceRasterBlocksForUpload(
    const Raster& source, RasterUploadBlockRegion region, std::uint8_t* destination, std::size_t destinationCapacity,
    std::size_t rowPitch, std::span<std::uint8_t> sourceRow, std::span<std::uint64_t> channelSums,
    CancelToken cancellation = {}, RasterUploadRowObserver observer = nullptr,
    void* observerContext = nullptr) noexcept;
