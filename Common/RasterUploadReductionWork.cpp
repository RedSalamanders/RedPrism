#include "RasterUploadReductionWork.h"

#include "Document.h"

#include <algorithm>
#include <limits>

RasterUploadWorkStatus ReduceRasterBlocksForUpload(const Raster& source, RasterUploadBlockRegion region,
                                                   std::uint8_t* destination, std::size_t destinationCapacity,
                                                   std::size_t rowPitch, std::span<std::uint8_t> sourceRow,
                                                   std::span<std::uint64_t> channelSums, CancelToken cancellation,
                                                   RasterUploadRowObserver observer, void* observerContext) noexcept
{
    if (!source.HasStorage() || destination == nullptr ||
        (source.Format() != RasterFormat::Bgra8888 && source.Format() != RasterFormat::Gray8) || region.sourceX < 0 ||
        region.sourceY < 0 || region.sourceWidth <= 0 || region.sourceHeight <= 0 || region.sourceX > source.Width() ||
        region.sourceY > source.Height() || region.sourceWidth > source.Width() - region.sourceX ||
        region.sourceHeight > source.Height() - region.sourceY || region.scale <= 0 || region.outputX < 0 ||
        region.outputY < 0 || region.outputWidth <= 0 || region.outputHeight <= 0)
    {
        return RasterUploadWorkStatus::Invalid;
    }

    // Avoid ceil's addition overflow for a scale larger than the crop (including INT_MAX).
    const int columns = 1 + (region.sourceWidth - 1) / region.scale;
    const int rows = 1 + (region.sourceHeight - 1) / region.scale;
    if (region.outputX >= columns || region.outputY >= rows || region.outputWidth > columns - region.outputX ||
        region.outputHeight > rows - region.outputY)
    {
        return RasterUploadWorkStatus::Invalid;
    }

    const std::size_t channels = source.Format() == RasterFormat::Gray8 ? 1U : 4U;
    const auto outputWidth = static_cast<std::size_t>(region.outputWidth);
    if (outputWidth > (std::numeric_limits<std::size_t>::max)() / channels)
    {
        return RasterUploadWorkStatus::Invalid;
    }
    const std::size_t rowBytes = outputWidth * channels;
    const auto precedingRows = static_cast<std::size_t>(region.outputHeight - 1);
    if (rowPitch < rowBytes || rowPitch > static_cast<std::size_t>((std::numeric_limits<int>::max)()) ||
        (precedingRows != 0 && rowPitch > ((std::numeric_limits<std::size_t>::max)() - rowBytes) / precedingRows) ||
        destinationCapacity < precedingRows * rowPitch + rowBytes)
    {
        return RasterUploadWorkStatus::Invalid;
    }
    if (region.scale == 1)
    {
        return CopyRasterRowsForUpload(source, region.sourceX + region.outputX, region.sourceY + region.outputY,
                                       region.outputWidth, region.outputHeight, destination, destinationCapacity,
                                       rowPitch, cancellation, observer, observerContext);
    }

    const auto sourceStartX = static_cast<int>(static_cast<std::int64_t>(region.outputX) * region.scale);
    const auto sourceEndX =
        static_cast<int>((std::min)(static_cast<std::int64_t>(region.sourceWidth),
                                    (static_cast<std::int64_t>(region.outputX) + region.outputWidth) * region.scale));
    const int sourceSpanWidth = sourceEndX - sourceStartX;
    const auto spanWidth = static_cast<std::size_t>(sourceSpanWidth);
    if (spanWidth > (std::numeric_limits<std::size_t>::max)() / channels ||
        spanWidth * channels > static_cast<std::size_t>((std::numeric_limits<int>::max)()) ||
        sourceRow.size() < spanWidth * channels || channelSums.size() < rowBytes)
    {
        return RasterUploadWorkStatus::Invalid;
    }
    if (cancellation.IsCancelled())
    {
        return RasterUploadWorkStatus::Cancelled;
    }

    for (int outputRow = 0; outputRow < region.outputHeight; ++outputRow)
    {
        if (cancellation.IsCancelled())
        {
            return RasterUploadWorkStatus::Cancelled;
        }
        const auto startY = static_cast<int>((static_cast<std::int64_t>(region.outputY) + outputRow) * region.scale);
        const auto endY = static_cast<int>((std::min)(static_cast<std::int64_t>(region.sourceHeight),
                                                      static_cast<std::int64_t>(startY) + region.scale));
        std::fill_n(channelSums.data(), rowBytes, std::uint64_t{0});
        for (int sourceY = startY; sourceY < endY; ++sourceY)
        {
            if (cancellation.IsCancelled())
            {
                return RasterUploadWorkStatus::Cancelled;
            }
            source.ReadRegion(region.sourceX + sourceStartX, region.sourceY + sourceY, sourceSpanWidth, 1,
                              sourceRow.data(), static_cast<int>(spanWidth * channels), 0);
            if (cancellation.IsCancelled())
            {
                return RasterUploadWorkStatus::Cancelled;
            }
            std::size_t pixelsUntilCheckpoint = 4096;
            std::size_t sourcePixel = 0;
            for (std::size_t outputColumn = 0; outputColumn < outputWidth; ++outputColumn)
            {
                std::size_t blockRemaining =
                    (std::min)(static_cast<std::size_t>(region.scale), spanWidth - sourcePixel);
                auto* sums = channelSums.data() + outputColumn * channels;
                while (blockRemaining != 0)
                {
                    if (pixelsUntilCheckpoint == 0)
                    {
                        if (cancellation.IsCancelled())
                        {
                            return RasterUploadWorkStatus::Cancelled;
                        }
                        pixelsUntilCheckpoint = 4096;
                    }
                    const auto count = (std::min)(blockRemaining, pixelsUntilCheckpoint);
                    const auto* pixel = sourceRow.data() + sourcePixel * channels;
                    if (channels == 4)
                    {
                        auto blue = sums[0];
                        auto green = sums[1];
                        auto red = sums[2];
                        auto alpha = sums[3];
                        for (std::size_t index = 0; index < count; ++index)
                        {
                            blue += pixel[0];
                            green += pixel[1];
                            red += pixel[2];
                            alpha += pixel[3];
                            pixel += 4;
                        }
                        sums[0] = blue;
                        sums[1] = green;
                        sums[2] = red;
                        sums[3] = alpha;
                    }
                    else
                    {
                        auto gray = sums[0];
                        for (std::size_t index = 0; index < count; ++index)
                        {
                            gray += pixel[index];
                        }
                        sums[0] = gray;
                    }
                    sourcePixel += count;
                    blockRemaining -= count;
                    pixelsUntilCheckpoint -= count;
                }
            }
        }
        if (cancellation.IsCancelled())
        {
            return RasterUploadWorkStatus::Cancelled;
        }
        auto* output = destination + static_cast<std::size_t>(outputRow) * rowPitch;
        std::size_t remainingWidth = spanWidth;
        for (std::size_t column = 0; column < outputWidth; ++column)
        {
            const auto blockWidth = (std::min)(static_cast<std::size_t>(region.scale), remainingWidth);
            const auto count = static_cast<std::uint64_t>(blockWidth) * static_cast<std::uint64_t>(endY - startY);
            for (std::size_t channel = 0; channel < channels; ++channel)
            {
                const auto index = column * channels + channel;
                output[index] = static_cast<std::uint8_t>((channelSums[index] + count / 2) / count);
            }
            remainingWidth -= blockWidth;
        }
        if (observer != nullptr)
        {
            observer(observerContext, static_cast<std::size_t>(outputRow) + 1);
        }
        if (cancellation.IsCancelled())
        {
            return RasterUploadWorkStatus::Cancelled;
        }
    }
    return RasterUploadWorkStatus::Ready;
}
