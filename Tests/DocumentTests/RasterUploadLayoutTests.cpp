#include "RasterUploadLayoutTests.h"

#include "../../Common/RasterUploadLayout.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <limits>

namespace
{
int gFailures = 0;

void Expect(bool condition, const char* name) noexcept
{
    std::printf("%s raster-upload-layout.%s\n", condition ? "PASS" : "FAIL", name);
    gFailures += condition ? 0 : 1;
}

void LogicalTextureByteLiterals()
{
    constexpr std::array<std::uint8_t, 60> bgraPixels{};
    const Raster bgra = Raster::Make(5, 3, RasterFormat::Bgra8888, bgraPixels.data(), bgraPixels.size());
    const auto bgraMips = RasterUploadLayout::Make(bgra, 0, 0, 5, 3, 1, 16384, 1024, 1024, true);
    Expect(bgraMips.IsValid() && bgraMips.Width() == 5 && bgraMips.Height() == 3, "bgra-5x3-valid");
    Expect(bgraMips.MipLevels() == 3 && bgraMips.LogicalTextureBytes() == 72, "bgra-5x3-full-mip-literal-72");

    constexpr std::array<std::uint8_t, 5> grayPixels{};
    const Raster gray = Raster::Make(1, 5, RasterFormat::Gray8, grayPixels.data(), grayPixels.size());
    const auto grayMips = RasterUploadLayout::Make(gray, 0, 0, 1, 5, 1, 16384, 1024, 1024, true);
    Expect(grayMips.IsValid() && grayMips.MipLevels() == 3 && grayMips.LogicalTextureBytes() == 8,
           "gray-1x5-floor-halved-mips-literal-8");

    const auto noMips = RasterUploadLayout::Make(bgra, 0, 0, 5, 3, 1, 16384, 1024, 1024, false);
    Expect(noMips.IsValid() && noMips.MipLevels() == 1 && noMips.LogicalTextureBytes() == 60,
           "bgra-no-mips-literal-60");
    Expect(noMips.SourceRowScratchBytes() == 0 && noMips.ChannelSumElements() == 0 &&
               noMips.ReductionScratchBytes() == 0,
           "scale-one-requires-no-reduction-scratch");
}

void ChunkRowsAreBoundedAndCoverHeight()
{
    constexpr std::array<std::uint8_t, 60> pixels{};
    const Raster raster = Raster::Make(5, 3, RasterFormat::Bgra8888, pixels.data(), pixels.size());
    const auto layout = RasterUploadLayout::Make(raster, 0, 0, 5, 3, 1, 16384, 40, 20, false);
    const auto first = layout.BaseChunkAt(0);
    const auto last = layout.BaseChunkAt(1);
    Expect(layout.IsValid() && layout.BaseChunkCount() == 2, "chunk-count-two-at-two-rows-per-chunk");
    Expect(first.outputY == 0 && first.outputHeight == 2 && last.outputY == 2 && last.outputHeight == 1,
           "chunk-windows-respect-byte-limit-and-final-remainder");
    Expect(first.outputHeight > 0 && first.outputHeight <= 2 && last.outputHeight > 0 && last.outputHeight <= 2,
           "chunk-windows-never-zero-and-never-exceed-row-bound");

    const auto exactRow = RasterUploadLayout::Make(raster, 0, 0, 5, 3, 1, 16384, 20, 20, false);
    Expect(exactRow.IsValid() && exactRow.BaseChunkCount() == 3 && exactRow.BaseChunkAt(2).outputHeight == 1,
           "exact-single-row-chunk-capacity");
}

void CroppedScaleWindowsKeepOneBlockGrid()
{
    constexpr std::array<std::uint8_t, 252> pixels{};
    const Raster raster = Raster::Make(9, 7, RasterFormat::Bgra8888, pixels.data(), pixels.size());
    const auto layout = RasterUploadLayout::Make(raster, 2, 1, 5, 4, 2, 16384, 12, 12, false);
    const std::array<RasterUploadBlockRegion, 2> chunks{layout.BaseChunkAt(0), layout.BaseChunkAt(1)};
    Expect(layout.IsValid() && layout.Width() == 3 && layout.Height() == 2 && layout.BaseChunkCount() == 2,
           "crop-scale-two-ceil-clipped-output-dimensions");
    Expect(layout.SourceRowScratchBytes() == 20 && layout.ChannelSumElements() == 12 &&
               layout.ReductionScratchBytes() == 116,
           "cropped-scale-scratch-literal-20-plus-12-uint64-sums");

    std::array<bool, 2> covered{};
    bool fixedCropScaleAndNoGaps = true;
    for (const RasterUploadBlockRegion& chunk : chunks)
    {
        fixedCropScaleAndNoGaps = fixedCropScaleAndNoGaps && chunk.sourceX == 2 && chunk.sourceY == 1 &&
                                  chunk.sourceWidth == 5 && chunk.sourceHeight == 4 && chunk.scale == 2 &&
                                  chunk.outputX == 0 && chunk.outputWidth == 3 && chunk.outputHeight == 1 &&
                                  chunk.outputY >= 0 && chunk.outputY < 2 &&
                                  !covered[static_cast<std::size_t>(chunk.outputY)];
        if (chunk.outputY >= 0 && chunk.outputY < 2 && chunk.outputHeight == 1)
        {
            covered[static_cast<std::size_t>(chunk.outputY)] = true;
        }
    }
    Expect(fixedCropScaleAndNoGaps && covered[0] && covered[1],
           "crop-scale-windows-cover-exact-rows-without-seam-shift");
}

void RejectInvalidAndOverLimitLayouts()
{
    constexpr std::array<std::uint8_t, 16> pixels{};
    const Raster raster = Raster::Make(2, 2, RasterFormat::Bgra8888, pixels.data(), pixels.size());
    const Raster empty;
    const auto invalid = [](const Raster& source, int x, int y, int width, int height, int scale,
                            unsigned int maxDimension, std::uint64_t chunkBytes, std::uint64_t maxRowBytes) noexcept
    {
        return !RasterUploadLayout::Make(source, x, y, width, height, scale, maxDimension, chunkBytes, maxRowBytes,
                                         false)
                    .IsValid();
    };
    Expect(invalid(empty, 0, 0, 1, 1, 1, 64, 64, 64), "reject-empty-source");
    Expect(invalid(raster, 0, 0, 0, 1, 1, 64, 64, 64), "reject-empty-crop");
    Expect(invalid(raster, -1, 0, 1, 1, 1, 64, 64, 64), "reject-negative-origin");
    Expect(invalid(raster, 0, 0, -1, 1, 1, 64, 64, 64), "reject-negative-crop-size");
    Expect(invalid(raster, 2, 0, 1, 1, 1, 64, 64, 64), "reject-origin-at-edge");
    Expect(invalid(raster, 0, 0, 1, 1, 0, 64, 64, 64), "reject-zero-scale");
    Expect(invalid(raster, 0, 0, 2, 2, 1, 1, 64, 64), "reject-oversized-device-dimension");
    Expect(invalid(raster, 0, 0, 2, 2, 1, 64, 64, 7), "reject-row-bytes-over-cap");
    Expect(invalid(raster, 0, 0, 2, 2, 1, 64, 7, 64), "reject-row-larger-than-chunk");
    Expect(invalid(raster, 0, 0, 1, 1, 1, 0, 64, 64), "reject-zero-texture-cap");
    Expect(invalid(raster, 0, 0, 1, 1, 1, 64, 0, 64), "reject-zero-chunk-cap");
    Expect(invalid(raster, 0, 0, 1, 1, 1, 64, 64, 0), "reject-zero-row-byte-cap");
}

void ExtremeScaleAndChunkArithmetic()
{
    constexpr std::array<std::uint8_t, 4> pixel{7, 11, 13, 17};
    const Raster raster = Raster::Make(1, 1, RasterFormat::Bgra8888, pixel.data(), pixel.size());
    const auto extremeScale =
        RasterUploadLayout::Make(raster, 0, 0, 1, 1, (std::numeric_limits<int>::max)(), 16384, 1024, 1024, false);
    Expect(extremeScale.IsValid() && extremeScale.Width() == 1 && extremeScale.Height() == 1,
           "int-max-scale-valid-1x1-ceiling");
    Expect(extremeScale.SourceRowScratchBytes() == 4 && extremeScale.ChannelSumElements() == 4 &&
               extremeScale.ReductionScratchBytes() == 36,
           "int-max-scale-clipped-scratch-arithmetic");

    constexpr std::array<std::uint8_t, 3> tallPixels{};
    const Raster tall = Raster::Make(1, 3, RasterFormat::Gray8, tallPixels.data(), 3);
    const auto hugeChunk =
        RasterUploadLayout::Make(tall, 0, 0, 1, 3, 1, 16384, (std::numeric_limits<std::uint64_t>::max)(), 1, false);
    Expect(hugeChunk.IsValid() && hugeChunk.BaseChunkCount() == 1 && hugeChunk.BaseChunkAt(0).outputHeight == 3,
           "uint64-max-chunk-clamped-to-output-height");
}

void ChunkIndexBoundariesAndInvalidDefault()
{
    constexpr std::array<std::uint8_t, 5> pixels{};
    const Raster raster = Raster::Make(1, 5, RasterFormat::Gray8, pixels.data(), pixels.size());
    const auto layout = RasterUploadLayout::Make(raster, 0, 0, 1, 5, 1, 16384, 1, 1, false);
    const auto first = layout.BaseChunkAt(0);
    const auto middle = layout.BaseChunkAt(2);
    const auto last = layout.BaseChunkAt(4);
    const auto outside = layout.BaseChunkAt(5);
    Expect(first.outputY == 0 && first.outputHeight == 1 && middle.outputY == 2 && middle.outputHeight == 1 &&
               last.outputY == 4 && last.outputHeight == 1,
           "chunk-index-first-middle-last");
    Expect(outside.outputWidth == 0 && outside.outputHeight == 0 &&
               layout.BaseChunkAt((std::numeric_limits<std::uint64_t>::max)()).outputHeight == 0,
           "chunk-index-out-of-range-and-uint64-max-invalid");
    const RasterUploadLayout invalid;
    const auto invalidRegion = invalid.BaseChunkAt(0);
    Expect(!invalid.IsValid() && invalidRegion.outputWidth == 0 && invalidRegion.outputHeight == 0,
           "default-layout-and-query-invalid");
}

void LayoutRetainsSourceAndSparseArithmeticStaysCold()
{
    constexpr std::array<std::uint8_t, 4> pixel{19, 23, 29, 31};
    Raster source = Raster::Make(1, 1, RasterFormat::Bgra8888, pixel.data(), pixel.size());
    const void* const identity = source.Identity();
    const auto retained = RasterUploadLayout::Make(source, 0, 0, 1, 1, 1, 16384, 1024, 1024, false);
    source = {};
    Expect(retained.IsValid() && retained.Source().Identity() == identity && retained.Source().Width() == 1,
           "layout-strongly-retains-source-after-caller-clears-it");

    const Raster sparse = Raster::MakeSparse(8192, 5464, RasterFormat::Bgra8888, Raster{}, 0, 0, {}, 0xA5);
    const std::uint64_t epoch = Raster::ResidencyEpoch();
    const auto large = RasterUploadLayout::Make(sparse, 0, 0, 8192, 5464, 1, 16384, 1024U * 1024U, 32768, false);
    Expect(sparse.IsSparse() && !sparse.IsMaterialized() && large.IsValid() && !sparse.IsMaterialized(),
           "large-fill-only-sparse-layout-does-not-materialize");
    Expect(large.Width() == 8192 && large.Height() == 5464 && large.BaseChunkCount() == 171 &&
               large.BaseChunkAt(170).outputY == 5440 && large.BaseChunkAt(170).outputHeight == 24,
           "large-sparse-1mib-chunks-32-rows-171-count-final-24-rows");
    Expect(Raster::ResidencyEpoch() == epoch, "large-sparse-layout-leaves-residency-epoch-unchanged");
}
} // namespace

int RunRasterUploadLayoutTests()
{
    gFailures = 0;
    LogicalTextureByteLiterals();
    ChunkRowsAreBoundedAndCoverHeight();
    CroppedScaleWindowsKeepOneBlockGrid();
    RejectInvalidAndOverLimitLayouts();
    ExtremeScaleAndChunkArithmetic();
    ChunkIndexBoundariesAndInvalidDefault();
    LayoutRetainsSourceAndSparseArithmeticStaysCold();
    return gFailures;
}
