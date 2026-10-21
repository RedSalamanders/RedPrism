#pragma once

#include "Executor.h"

#include <cstddef>
#include <cstdint>

class Raster;

enum class RasterUploadWorkStatus : std::uint8_t
{
    Ready,
    Invalid,
    Cancelled,
};

using RasterUploadRowObserver = void (*)(void* context, std::size_t completedRows) noexcept;

// Copies an in-bounds rectangle into caller-owned pitched storage, one row at a time. Sparse sources stay sparse.
// Pitch padding is untouched, and the observer runs after each completed row. If cancellation occurs mid-copy,
// the written prefix is unpublished and must be discarded.
[[nodiscard]] RasterUploadWorkStatus CopyRasterRowsForUpload(const Raster& source, int sourceX, int sourceY, int width,
                                                             int height, std::uint8_t* destination,
                                                             std::size_t destinationCapacity, std::size_t rowPitch,
                                                             CancelToken cancellation = {},
                                                             RasterUploadRowObserver observer = nullptr,
                                                             void* observerContext = nullptr) noexcept;
