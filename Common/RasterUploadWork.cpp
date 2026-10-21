#include "RasterUploadWork.h"

#include "Document.h"

#include <limits>

RasterUploadWorkStatus CopyRasterRowsForUpload(const Raster& source, int sourceX, int sourceY, int width, int height,
                                               std::uint8_t* destination, std::size_t destinationCapacity,
                                               std::size_t rowPitch, CancelToken cancellation,
                                               RasterUploadRowObserver observer, void* observerContext) noexcept
{
    if (!source.HasStorage() || destination == nullptr || sourceX < 0 || sourceY < 0 || width <= 0 || height <= 0 ||
        (source.Format() != RasterFormat::Gray8 && source.Format() != RasterFormat::Bgra8888) ||
        sourceX > source.Width() || sourceY > source.Height() || width > source.Width() - sourceX ||
        height > source.Height() - sourceY)
    {
        return RasterUploadWorkStatus::Invalid;
    }

    const std::size_t channels = source.Format() == RasterFormat::Gray8 ? 1U : 4U;
    const std::size_t unsignedWidth = static_cast<std::size_t>(width);
    if (unsignedWidth > std::numeric_limits<std::size_t>::max() / channels)
    {
        return RasterUploadWorkStatus::Invalid;
    }
    const std::size_t rowBytes = unsignedWidth * channels;
    if (rowPitch < rowBytes || rowPitch > static_cast<std::size_t>(std::numeric_limits<int>::max()))
    {
        return RasterUploadWorkStatus::Invalid;
    }

    const std::size_t precedingRows = static_cast<std::size_t>(height - 1);
    if (precedingRows != 0 && rowPitch > (std::numeric_limits<std::size_t>::max() - rowBytes) / precedingRows)
    {
        return RasterUploadWorkStatus::Invalid;
    }
    const std::size_t requiredCapacity = precedingRows * rowPitch + rowBytes;
    if (destinationCapacity < requiredCapacity)
    {
        return RasterUploadWorkStatus::Invalid;
    }
    if (cancellation.IsCancelled())
    {
        return RasterUploadWorkStatus::Cancelled;
    }

    for (int row = 0; row < height; ++row)
    {
        if (cancellation.IsCancelled())
        {
            return RasterUploadWorkStatus::Cancelled;
        }
        const std::size_t rowIndex = static_cast<std::size_t>(row);
        source.ReadRegion(sourceX, sourceY + row, width, 1, destination + rowIndex * rowPitch,
                          static_cast<int>(rowPitch), 0);
        if (observer != nullptr)
        {
            observer(observerContext, rowIndex + 1);
        }
        if (cancellation.IsCancelled())
        {
            return RasterUploadWorkStatus::Cancelled;
        }
    }
    return RasterUploadWorkStatus::Ready;
}
