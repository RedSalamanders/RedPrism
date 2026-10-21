#pragma once

#include "Document.h"
#include "RasterUploadReductionWork.h"

#include <cstddef>
#include <cstdint>

// Immutable arithmetic layout for one owned raster crop. This describes logical texels and base-level upload
// windows, not complete CPU/VRAM admission, device resources, driver overhead, mip generation or publication.
class RasterUploadLayout final
{
  public:
    [[nodiscard]] static RasterUploadLayout Make(const Raster& source, int x, int y, int width, int height, int scale,
                                                 unsigned int maximumTextureDimension, std::uint64_t chunkBytes,
                                                 std::uint64_t maximumRowBytes, bool mipChain) noexcept;
    [[nodiscard]] bool IsValid() const noexcept
    {
        return _source.HasStorage();
    }
    [[nodiscard]] const Raster& Source() const noexcept
    {
        return _source;
    }
    [[nodiscard]] int Width() const noexcept
    {
        return _outputWidth;
    }
    [[nodiscard]] int Height() const noexcept
    {
        return _outputHeight;
    }
    [[nodiscard]] unsigned int MipLevels() const noexcept
    {
        return _mipLevels;
    }
    [[nodiscard]] std::uint64_t LogicalTextureBytes() const noexcept
    {
        return _textureBytes;
    }
    [[nodiscard]] std::uint64_t BaseChunkCount() const noexcept
    {
        return _chunkCount;
    }
    [[nodiscard]] std::uint64_t SourceRowScratchBytes() const noexcept
    {
        return _sourceRowBytes;
    }
    [[nodiscard]] std::uint64_t ChannelSumElements() const noexcept
    {
        return _sumElements;
    }
    [[nodiscard]] std::uint64_t ReductionScratchBytes() const noexcept
    {
        return _scratchBytes;
    }
    // No vector/allocation. Invalid/out-of-range returns the default invalid region. The crop and scale remain
    // identical for every window; splitting uploads never changes reduction origins or partial edge blocks.
    [[nodiscard]] RasterUploadBlockRegion BaseChunkAt(std::uint64_t index) const noexcept;

  private:
    Raster _source;
    int _x = 0;
    int _y = 0;
    int _sourceWidth = 0;
    int _sourceHeight = 0;
    int _scale = 1;
    int _outputWidth = 0;
    int _outputHeight = 0;
    int _chunkRows = 0;
    unsigned int _mipLevels = 0;
    std::uint64_t _textureBytes = 0;
    std::uint64_t _chunkCount = 0;
    std::uint64_t _sourceRowBytes = 0;
    std::uint64_t _sumElements = 0;
    std::uint64_t _scratchBytes = 0;
};
