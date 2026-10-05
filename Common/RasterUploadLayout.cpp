#include "RasterUploadLayout.h"

#include <algorithm>
#include <limits>

RasterUploadLayout RasterUploadLayout::Make(const Raster& source, int x, int y, int width, int height, int scale,
                                            unsigned int maximumTextureDimension, std::uint64_t chunkBytes,
                                            std::uint64_t maximumRowBytes, bool mipChain) noexcept
{
    if (!source.HasStorage() || x < 0 || y < 0 || width <= 0 || height <= 0 || scale <= 0 || x > source.Width() ||
        y > source.Height() || width > source.Width() - x || height > source.Height() - y ||
        (source.Format() != RasterFormat::Bgra8888 && source.Format() != RasterFormat::Gray8) ||
        maximumTextureDimension == 0 || chunkBytes == 0 || maximumRowBytes == 0)
        return {};
    const int outputWidth = 1 + (width - 1) / scale;
    const int outputHeight = 1 + (height - 1) / scale;
    if (static_cast<unsigned int>(outputWidth) > maximumTextureDimension ||
        static_cast<unsigned int>(outputHeight) > maximumTextureDimension)
        return {};
    const std::uint64_t channels = source.Format() == RasterFormat::Gray8 ? 1U : 4U;
    const auto rowBytes = static_cast<std::uint64_t>(outputWidth) * channels;
    if (rowBytes > maximumRowBytes || rowBytes > chunkBytes)
        return {};

    RasterUploadLayout result;
    result._x = x;
    result._y = y;
    result._sourceWidth = width;
    result._sourceHeight = height;
    result._scale = scale;
    result._outputWidth = outputWidth;
    result._outputHeight = outputHeight;
    result._chunkRows = static_cast<int>((std::min)(static_cast<std::uint64_t>(outputHeight), chunkBytes / rowBytes));
    result._chunkCount = 1 + static_cast<std::uint64_t>(outputHeight - 1) / result._chunkRows;
    auto mipWidth = static_cast<std::uint64_t>(outputWidth);
    auto mipHeight = static_cast<std::uint64_t>(outputHeight);
    const auto maximum = (std::numeric_limits<std::uint64_t>::max)();
    while (true)
    {
        if (mipHeight > maximum / mipWidth || mipWidth * mipHeight > maximum / channels ||
            mipWidth * mipHeight * channels > maximum - result._textureBytes)
            return {};
        result._textureBytes += mipWidth * mipHeight * channels;
        ++result._mipLevels;
        if (!mipChain || (mipWidth == 1 && mipHeight == 1))
            break;
        mipWidth = (std::max)(std::uint64_t{1}, mipWidth / 2);
        mipHeight = (std::max)(std::uint64_t{1}, mipHeight / 2);
    }
    if (scale > 1)
    {
        result._sourceRowBytes = static_cast<std::uint64_t>(width) * channels;
        result._sumElements = rowBytes;
        if (result._sumElements > (maximum - result._sourceRowBytes) / sizeof(std::uint64_t))
            return {};
        result._scratchBytes = result._sourceRowBytes + result._sumElements * sizeof(std::uint64_t);
    }
    // Capture only after every arithmetic/device-dimension check. No pixel cache is read or materialized.
    result._source = source;
    return result;
}

RasterUploadBlockRegion RasterUploadLayout::BaseChunkAt(std::uint64_t index) const noexcept
{
    if (!IsValid() || index >= _chunkCount)
        return {};
    const auto y = static_cast<int>(index * static_cast<std::uint64_t>(_chunkRows));
    return {_x, _y, _sourceWidth, _sourceHeight, _scale, 0, y, _outputWidth, (std::min)(_chunkRows, _outputHeight - y)};
}
