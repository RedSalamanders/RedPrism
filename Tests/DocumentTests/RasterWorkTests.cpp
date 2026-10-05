#include "RasterWorkTests.h"

#include "Document.h"
#include "RasterWork.h"

#include <algorithm>
#include <array>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <span>
#include <stop_token>
#include <utility>
#include <vector>

namespace
{
int gFailures = 0;

void Expect(bool condition, const char* name)
{
    std::printf("%s rasterwork.%s\n", condition ? "PASS" : "FAIL", name);
    gFailures += condition ? 0 : 1;
}

Raster Bgra(int width, int height, std::span<const std::uint8_t> bytes)
{
    return Raster::Make(width, height, RasterFormat::Bgra8888, bytes.data(), bytes.size());
}

Raster Gray(int width, int height, std::span<const std::uint8_t> bytes)
{
    return Raster::Make(width, height, RasterFormat::Gray8, bytes.data(), bytes.size());
}

bool ReadEquals(const Raster& image, GridRect region, std::size_t stride, std::size_t size, std::uint8_t outsideFill,
                std::span<const std::uint8_t> expected, std::uint8_t initial = 0xCC)
{
    if (size != expected.size())
    {
        return false;
    }
    std::vector<std::uint8_t> output(size, initial);
    return ReadRasterRegionForWork(image, region, output, stride, outsideFill) &&
           std::equal(output.begin(), output.end(), expected.begin());
}

void ContiguousCropsAndRows()
{
    constexpr std::array<std::uint8_t, 24> bgra{1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12,
                                                13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24};
    const Raster color = Bgra(3, 2, bgra);
    constexpr std::array<std::uint8_t, 44> paddedExpected{
        90, 90, 90, 90, 90, 90, 90,   90,   90,   90,   90, 90, 0xCC, 0xCC, 0xCC, 0xCC, 5,  6,  7,  8,  9,  10,
        11, 12, 90, 90, 90, 90, 0xCC, 0xCC, 0xCC, 0xCC, 17, 18, 19,   20,   21,   22,   23, 24, 90, 90, 90, 90};
    Expect(ReadEquals(color, {1, -1, 3, 3}, 16, paddedExpected.size(), 90, paddedExpected),
           "bgra.crop-exterior-nonzero-x-padding-and-final-row");

    constexpr std::array<std::uint8_t, 6> grayPixels{11, 22, 33, 44, 55, 66};
    const Raster gray = Gray(3, 2, grayPixels);
    constexpr std::array<std::uint8_t, 6> grayExpected{22, 33, 88, 55, 66, 88};
    Expect(ReadEquals(gray, {1, 0, 3, 2}, 3, grayExpected.size(), 88, grayExpected),
           "gray8.crop-exterior-nonzero-x-no-final-row-padding");
}

void SparseFillBaseAndTiles()
{
    constexpr std::array<std::uint8_t, 4> tileBytes{21, 22, 23, 24};
    const Raster tile = Gray(2, 2, tileBytes);
    const Raster ownFill = Raster::MakeSparse(3, 2, RasterFormat::Gray8, Raster{}, 0, 0, {RasterTile{1, 0, tile}}, 91);
    constexpr std::array<std::uint8_t, 15> ownFillExpected{7, 7, 7, 7, 7, 7, 91, 21, 22, 7, 7, 91, 23, 24, 7};
    Expect(ReadEquals(ownFill, {-1, -1, 5, 3}, 5, ownFillExpected.size(), 7, ownFillExpected) &&
               !ownFill.IsMaterialized(),
           "sparse.own-fill-inside-outside-fill-outside-and-tile");

    constexpr std::array<std::uint8_t, 6> basePixels{31, 32, 33, 41, 42, 43};
    const Raster base = Gray(3, 2, basePixels);
    const Raster clippedBase = Raster::MakeSparse(4, 3, RasterFormat::Gray8, base, -1, 1, {}, 99);
    constexpr std::array<std::uint8_t, 12> baseExpected{99, 99, 99, 99, 32, 33, 99, 99, 42, 43, 99, 99};
    Expect(ReadEquals(clippedBase, {0, 0, 4, 3}, 4, baseExpected.size(), 6, baseExpected) &&
               !clippedBase.IsMaterialized(),
           "sparse.clipped-offset-base");

    constexpr std::array<std::uint8_t, 24> oldPixels{1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12,
                                                     13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24};
    constexpr std::array<std::uint8_t, 2> freshPixels{201, 202};
    const Raster oldTile = Gray(6, 4, oldPixels);
    const Raster freshTile = Gray(2, 1, freshPixels);
    const Raster oldGrid =
        Raster::MakeSparse(6, 4, RasterFormat::Gray8, Raster{}, 0, 0, {RasterTile{1, 1, oldTile, 2, 1, 4, 2}}, 80);
    const Raster replacement = Raster::Replacing(oldGrid, 0, 0, {RasterTile{2, 1, freshTile}}, 0, 0, 6, 4, 80);
    constexpr std::array<std::uint8_t, 24> replacementExpected{80, 80, 80, 80, 80, 80, 80, 9,  201, 202, 12, 80,
                                                               80, 15, 16, 17, 18, 80, 80, 80, 80,  80,  80, 80};
    bool disjoint = true;
    bool keptSourceOffset = false;
    for (std::size_t first = 0; first < replacement.Tiles().size(); ++first)
    {
        const RasterTile& a = replacement.Tiles()[first];
        keptSourceOffset = keptSourceOffset || (a.pixels == oldTile && (a.sourceX != 0 || a.sourceY != 0));
        for (std::size_t second = first + 1; second < replacement.Tiles().size(); ++second)
        {
            const RasterTile& b = replacement.Tiles()[second];
            const bool overlap =
                a.x < b.x + b.Width() && b.x < a.x + a.Width() && a.y < b.y + b.Height() && b.y < a.y + a.Height();
            disjoint = disjoint && !overlap;
        }
    }
    Expect(ReadEquals(replacement, {0, 0, 6, 4}, 6, replacementExpected.size(), 80, replacementExpected) && disjoint &&
               keptSourceOffset && !oldGrid.IsMaterialized() && !replacement.IsMaterialized(),
           "sparse.replacement-splits-shared-tile-windows-with-source-offsets");
}

void NestedSparseAndSharing()
{
    constexpr std::array<std::uint8_t, 4> leafPixels{61, 62, 63, 64};
    const Raster leaf = Bgra(1, 1, leafPixels);
    const Raster inner = Raster::MakeSparse(2, 2, RasterFormat::Bgra8888, Raster{}, 0, 0, {RasterTile{1, 0, leaf}}, 44);
    const Raster middle =
        Raster::MakeSparse(3, 2, RasterFormat::Bgra8888, Raster{}, 0, 0, {RasterTile{1, 0, inner}}, 45);
    const Raster outer =
        Raster::MakeSparse(4, 3, RasterFormat::Bgra8888, Raster{}, 0, 0, {RasterTile{1, 1, middle}}, 46);
    constexpr std::array<std::uint8_t, 4> pixelExpected{61, 62, 63, 64};
    std::array<std::uint8_t, 4> pixel{};
    const bool nestedRead = ReadRasterRegionForWork(outer, {3, 1, 1, 1}, pixel, 4, 0) && pixel == pixelExpected;
    Expect(nestedRead && !outer.IsMaterialized() && !middle.IsMaterialized() && !inner.IsMaterialized() &&
               outer.IsSparse() && middle.IsSparse() && inner.IsSparse(),
           "nested.sparse-sources-remain-unmaterialized-through-tile-descendants");
    constexpr std::array<std::uint8_t, 2> grayPixels{71, 72};
    const Raster contiguous = Gray(2, 1, grayPixels);
    const Raster published =
        Raster::MakeSparse(2, 1, RasterFormat::Gray8, Raster{}, 0, 0, {RasterTile{0, 0, contiguous}}, 0);
    const std::uint8_t* const publishedBytes = published.Data();
    const Raster shared = ContiguousRasterForWork(published);
    Expect(publishedBytes != nullptr && shared == published && published.IsMaterialized(),
           "contiguous.copy-shares-already-materialized-storage");
}

Raster NestedRaster(std::size_t sparseLevels, const Raster& leaf)
{
    Raster current = leaf;
    for (std::size_t level = 0; level < sparseLevels; ++level)
    {
        current = Raster::MakeSparse(1, 1, RasterFormat::Gray8, Raster{}, 0, 0, {RasterTile{0, 0, current}}, 0);
    }
    return current;
}

void DepthAndPublication()
{
    constexpr std::array<std::uint8_t, 1> leafByte{173};
    const Raster leaf = Gray(1, 1, leafByte);
    const Raster limit = NestedRaster(kRasterWorkDepthLimit - 1, leaf);
    std::array<std::uint8_t, 1> result{};
    const bool atLimit = ReadRasterRegionForWork(limit, {0, 0, 1, 1}, result, 1, 0) && result[0] == 173;
    Expect(atLimit && !limit.IsMaterialized(), "depth.limit-succeeds");

    const Raster tooDeep = NestedRaster(kRasterWorkDepthLimit, leaf);
    const Raster rejected = CropRasterForWork(tooDeep, {0, 0, 1, 1}, 0);
    Expect(!rejected.HasStorage() && !tooDeep.IsMaterialized(),
           "depth.over-limit-crop-fails-without-publishing-raster");
}

void CancellationAndInvalidInputs()
{
    constexpr std::array<std::uint8_t, 4> sourceBytes{1, 2, 3, 4};
    const Raster graySource = Gray(2, 2, sourceBytes);
    const Raster source =
        Raster::MakeSparse(2, 2, RasterFormat::Gray8, Raster{}, 0, 0, {RasterTile{0, 0, graySource}}, 0);
    CancelSource canceled;
    (void)canceled.Cancel();
    std::array<std::uint8_t, 6> output{9, 9, 9, 9, 9, 9};
    const auto before = output;
    const bool canceledRead = !ReadRasterRegionForWork(source, {0, 0, 2, 2}, output, 3, 0, canceled.Token());
    const PixelBuffer canceledCopy = CopyRasterPixelsForWork(source, canceled.Token());
    const Raster canceledContiguous = ContiguousRasterForWork(source, canceled.Token());
    const Raster canceledCrop = CropRasterForWork(source, {0, 0, 2, 2}, 0, canceled.Token());
    Expect(canceledRead && output == before && !canceledCopy && !canceledContiguous.HasStorage() &&
               !canceledCrop.HasStorage() && !source.IsMaterialized(),
           "cancel.pre-cancel-preserves-output-and-publishes-nothing");

    const bool invalid =
        !ReadRasterRegionForWork(source, {0, 0, 2, 2}, output, 0, 0) &&
        !ReadRasterRegionForWork(source, {0, 0, 2, 2}, output, 1, 0) &&
        !ReadRasterRegionForWork(source, {0, 0, 2, 2}, std::span<std::uint8_t>{output.data(), 3}, 2, 0) &&
        !ReadRasterRegionForWork(source, {0, 0, 2, 2}, std::span<std::uint8_t>{output.data(), 5}, 4, 0);
    Expect(invalid && !source.IsMaterialized(), "validation.short-span-and-invalid-strides");

    constexpr std::array<std::uint8_t, 6> rightExpected{77, 77, 77, 77, 77, 77};
    constexpr std::array<std::uint8_t, 6> leftExpected{77, 77, 77, 77, 77, 77};
    constexpr std::array<std::uint8_t, 2> verticalExpected{77, 77};
    const bool extreme = ReadEquals(source, {INT_MAX - 1, 0, 3, 2}, 3, rightExpected.size(), 77, rightExpected) &&
                         ReadEquals(source, {INT_MIN, 0, 3, 2}, 3, leftExpected.size(), 77, leftExpected) &&
                         ReadEquals(source, {0, INT_MAX, 2, 1}, 2, verticalExpected.size(), 77, verticalExpected) &&
                         ReadEquals(source, {0, INT_MIN, 2, 1}, 2, verticalExpected.size(), 77, verticalExpected);
    Expect(extreme && !source.IsMaterialized(), "bounds.extreme-x-and-y-do-not-wrap");
}

void CopyAndCropOutputs()
{
    constexpr std::array<std::uint8_t, 6> pixels{9, 18, 27, 36, 45, 54};
    const Raster tile = Gray(2, 2, std::span<const std::uint8_t>{pixels}.subspan(0, 4));
    const Raster sparse = Raster::MakeSparse(3, 2, RasterFormat::Gray8, Raster{}, 0, 0, {RasterTile{1, 0, tile}}, 99);
    const PixelBuffer copied = CopyRasterPixelsForWork(sparse);
    constexpr std::array<std::uint8_t, 6> copyExpected{99, 9, 18, 99, 27, 36};
    const bool copyMatches = copied && copied.Size() == copyExpected.size() &&
                             std::equal(copyExpected.begin(), copyExpected.end(), copied.Data());
    const Raster contiguous = ContiguousRasterForWork(sparse);
    const bool contiguousMatches = contiguous.HasStorage() && contiguous.Width() == 3 && contiguous.Height() == 2 &&
                                   std::equal(copyExpected.begin(), copyExpected.end(), contiguous.Data());
    const Raster crop = CropRasterForWork(sparse, {-1, 0, 4, 2}, 7);
    constexpr std::array<std::uint8_t, 8> cropExpected{7, 99, 9, 18, 7, 99, 27, 36};
    const bool cropMatches = crop.HasStorage() && crop.Width() == 4 && crop.Height() == 2 &&
                             crop.Format() == RasterFormat::Gray8 &&
                             std::equal(cropExpected.begin(), cropExpected.end(), crop.Data());
    Expect(copyMatches && contiguousMatches && cropMatches && !sparse.IsMaterialized(),
           "copy-and-crop-match-independent-literal-bytes");
}

void StretchedRasterForWorkCases()
{
    constexpr std::array<std::uint8_t, 12> grayPixels{10, 11, 12, 13, 20, 21, 22, 23, 30, 31, 32, 33};
    const Raster gray = Gray(4, 3, grayPixels);
    if (!gray.HasStorage())
    {
        Expect(false, "stretch.gray-fixture-created");
        return;
    }
    const Raster grayDown = StretchedRasterForWork(gray, 2, 2);
    constexpr std::array<std::uint8_t, 4> grayDownExpected{10, 12, 20, 22};
    Expect(grayDown.HasStorage() && grayDown.Width() == 2 && grayDown.Height() == 2 &&
               grayDown.Format() == RasterFormat::Gray8 && grayDown.Data() != nullptr &&
               std::equal(grayDownExpected.begin(), grayDownExpected.end(), grayDown.Data()),
           "stretch.gray4x3-to2x2-nearest-literal");

    constexpr std::array<std::uint8_t, 16> bgraPixels{1, 2, 3, 4, 11, 12, 13, 14, 21, 22, 23, 24, 31, 32, 33, 34};
    const Raster bgra = Bgra(2, 2, bgraPixels);
    if (!bgra.HasStorage())
    {
        Expect(false, "stretch.bgra-fixture-created");
        return;
    }
    const Raster bgraUp = StretchedRasterForWork(bgra, 3, 3);
    constexpr std::array<std::uint8_t, 36> bgraUpExpected{1,  2,  3,  4,  1,  2,  3,  4,  11, 12, 13, 14,
                                                          1,  2,  3,  4,  1,  2,  3,  4,  11, 12, 13, 14,
                                                          21, 22, 23, 24, 21, 22, 23, 24, 31, 32, 33, 34};
    Expect(bgraUp.HasStorage() && bgraUp.Width() == 3 && bgraUp.Height() == 3 &&
               bgraUp.Format() == RasterFormat::Bgra8888 && bgraUp.Data() != nullptr &&
               std::equal(bgraUpExpected.begin(), bgraUpExpected.end(), bgraUp.Data()),
           "stretch.bgra2x2-to3x3-nearest-literal");

    const Raster identity = StretchedRasterForWork(gray, 4, 3);
    Expect(identity == gray, "stretch.exact-dimensions-share-source");

    constexpr std::array<std::uint8_t, 6> basePixels{1, 2, 3, 4, 5, 6};
    constexpr std::array<std::uint8_t, 1> leafPixels{99};
    const Raster base = Gray(3, 2, basePixels);
    const Raster leaf = Gray(1, 1, leafPixels);
    if (!base.HasStorage() || !leaf.HasStorage())
    {
        Expect(false, "stretch.sparse-fixtures-created");
        return;
    }
    const Raster nested = Raster::MakeSparse(2, 2, RasterFormat::Gray8, Raster{}, 0, 0, {RasterTile{1, 0, leaf}}, 70);
    if (!nested.HasStorage())
    {
        Expect(false, "stretch.nested-sparse-fixture-created");
        return;
    }
    const Raster sparse = Raster::MakeSparse(5, 3, RasterFormat::Gray8, base, 1, 0, {RasterTile{2, 1, nested}}, 9);
    if (!sparse.HasStorage())
    {
        Expect(false, "stretch.outer-sparse-fixture-created");
        return;
    }
    const Raster sparseUp = StretchedRasterForWork(sparse, 3, 2);
    // Source coordinates are (0,0),(1,0),(3,0) and (0,1),(1,1),(3,1).
    // The outer fill is 9; the base begins at (1,0); the nested sparse tile contributes 70/99.
    constexpr std::array<std::uint8_t, 6> sparseExpected{9, 1, 3, 9, 4, 99};
    Expect(sparseUp.HasStorage() && sparseUp.Data() != nullptr &&
               std::equal(sparseExpected.begin(), sparseExpected.end(), sparseUp.Data()) && sparse.IsSparse() &&
               !sparse.IsMaterialized() && nested.IsSparse() && !nested.IsMaterialized(),
           "stretch.sparse-base-nested-tile-fill-private-read");

    CancelSource cancelled;
    (void)cancelled.Cancel();
    Expect(!StretchedRasterForWork(sparse, 3, 2, cancelled.Token()).HasStorage() &&
               !StretchedRasterForWork(gray, 0, 2).HasStorage() &&
               !StretchedRasterForWork(gray, 2, kMaxCanvasSide + 1).HasStorage() && !sparse.IsMaterialized() &&
               !nested.IsMaterialized(),
           "stretch.precancel-and-invalid-dimensions-publish-nothing");
}

void ThumbnailRasterForWorkCases()
{
    constexpr std::array<std::uint8_t, 6> smallPixels{3, 7, 11, 15, 19, 23};
    const Raster small = Gray(3, 2, smallPixels);
    if (!small.HasStorage())
    {
        Expect(false, "thumbnail.small-fixture-created");
        return;
    }
    Expect(ThumbnailRasterForWork(small, 3) == small, "thumbnail.max-side-at-dimension-returns-identity");

    constexpr std::array<std::uint8_t, 4> basePixels{11, 12, 21, 22};
    constexpr std::array<std::uint8_t, 1> leafPixels{99};
    const Raster base = Gray(2, 2, basePixels);
    const Raster leaf = Gray(1, 1, leafPixels);
    if (!base.HasStorage() || !leaf.HasStorage())
    {
        Expect(false, "thumbnail.sparse-fixtures-created");
        return;
    }
    const Raster nested = Raster::MakeSparse(2, 2, RasterFormat::Gray8, Raster{}, 0, 0, {RasterTile{1, 0, leaf}}, 70);
    if (!nested.HasStorage())
    {
        Expect(false, "thumbnail.nested-fixture-created");
        return;
    }
    const Raster sparse = Raster::MakeSparse(4, 4, RasterFormat::Gray8, base, 0, 0, {RasterTile{1, 1, nested}}, 7);
    if (!sparse.HasStorage())
    {
        Expect(false, "thumbnail.outer-fixture-created");
        return;
    }
    const Raster thumbnail = ThumbnailRasterForWork(sparse, 2);
    // 4x4 -> 2x2 samples (0,0),(2,0),(0,2),(2,2): base, outer fill, outer fill, nested fill.
    constexpr std::array<std::uint8_t, 4> thumbnailExpected{11, 7, 7, 70};
    Expect(thumbnail.HasStorage() && thumbnail.Width() == 2 && thumbnail.Height() == 2 &&
               thumbnail.Format() == RasterFormat::Gray8 && thumbnail.Data() != nullptr &&
               std::equal(thumbnailExpected.begin(), thumbnailExpected.end(), thumbnail.Data()) && sparse.IsSparse() &&
               !sparse.IsMaterialized() && nested.IsSparse() && !nested.IsMaterialized(),
           "thumbnail.sparse-rectangle-reduction-literal-private-rows");

    CancelSource cancelled;
    (void)cancelled.Cancel();
    Expect(!ThumbnailRasterForWork(sparse, 2, cancelled.Token()).HasStorage() &&
               !ThumbnailRasterForWork(small, 0).HasStorage() && !sparse.IsMaterialized() && !nested.IsMaterialized(),
           "thumbnail.precancel-and-invalid-max-side-publish-nothing");
}

void FittingSparseMaskThumbnailCases()
{
    std::vector<std::uint8_t> largeLeafBytes(128U * 128U, 61);
    largeLeafBytes[126U * 128U + 126U] = 201;
    largeLeafBytes[126U * 128U + 127U] = 202;
    largeLeafBytes[127U * 128U + 126U] = 203;
    largeLeafBytes[127U * 128U + 127U] = 204;
    const Raster largeLeaf = Gray(128, 128, largeLeafBytes);
    if (!largeLeaf.HasStorage())
    {
        Expect(false, "thumbnail-reference.nested-mask-fixtures-created");
        return;
    }
    const Raster nested =
        Raster::MakeSparse(128, 128, RasterFormat::Gray8, Raster{}, 0, 0, {RasterTile{0, 0, largeLeaf}}, 61);
    const Raster sparse =
        Raster::MakeSparse(2, 2, RasterFormat::Gray8, Raster{}, 0, 0, {RasterTile{0, 0, nested, 126, 126, 2, 2}}, 9);
    if (!nested.HasStorage() || !sparse.HasStorage())
    {
        Expect(false, "thumbnail-reference.nested-mask-fixtures-created");
        return;
    }

    const std::size_t sourceBytesBefore = sparse.ByteSize();
    const std::size_t nestedBytesBefore = nested.ByteSize();
    const std::size_t leafBytesBefore = largeLeaf.ByteSize();
    const Raster thumbnail = Raster::MakeThumbnail(sparse, 2);
    constexpr std::array<std::uint8_t, 4> expected{201, 202, 203, 204};
    const bool privatePixels = thumbnail.HasStorage() && thumbnail.Width() == 2 && thumbnail.Height() == 2 &&
                               thumbnail.Format() == RasterFormat::Gray8 && thumbnail.IsMaterialized() &&
                               thumbnail.Identity() != sparse.Identity() && thumbnail.ByteSize() == expected.size() &&
                               std::equal(expected.begin(), expected.end(), thumbnail.Data());
    Expect(privatePixels && sparse.ByteSize() == sourceBytesBefore && nested.ByteSize() == nestedBytesBefore &&
               largeLeaf.ByteSize() == leafBytesBefore && !sparse.IsMaterialized() && !nested.IsMaterialized() &&
               largeLeaf.IsMaterialized(),
           "thumbnail-reference.fitting-nested-sparse-gray-copies-only-private-thumbnail");

    constexpr std::array<std::uint8_t, 4> denseGrayBytes{7, 8, 9, 10};
    constexpr std::array<std::uint8_t, 16> denseBgraBytes{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
    const Raster denseGray = Gray(2, 2, denseGrayBytes);
    const Raster denseBgra = Bgra(2, 2, denseBgraBytes);
    const Raster grayAlias = Raster::MakeThumbnail(denseGray, 2);
    const Raster bgraAlias = Raster::MakeThumbnail(denseBgra, 2);
    Expect(denseGray.HasStorage() && denseBgra.HasStorage() && grayAlias == denseGray && bgraAlias == denseBgra,
           "thumbnail-reference.fitting-dense-gray-and-bgra-preserve-alias");

    constexpr std::array<std::uint8_t, 4> belowThresholdBytes{127, 127, 127, 127};
    constexpr std::array<std::uint8_t, 4> exactThresholdBytes{127, 128, 128, 127};
    const Raster belowThreshold = Gray(2, 2, belowThresholdBytes);
    const Raster exactThreshold = Gray(2, 2, exactThresholdBytes);
    Expect(belowThreshold.HasStorage() && exactThreshold.HasStorage() && MaskEdgeBackground(belowThreshold) == 0.0 &&
               MaskEdgeBackground(exactThreshold) == 1.0,
           "thumbnail-reference.mask-edge-127-128-threshold-remains-exact");
}

void FittingSparseMaskWorkerThumbnailCases()
{
    std::vector<std::uint8_t> largeLeafBytes(128U * 128U, 61);
    largeLeafBytes[126U * 128U + 126U] = 201;
    largeLeafBytes[126U * 128U + 127U] = 202;
    largeLeafBytes[127U * 128U + 126U] = 203;
    largeLeafBytes[127U * 128U + 127U] = 204;
    const Raster largeLeaf = Gray(128, 128, largeLeafBytes);
    if (!largeLeaf.HasStorage())
    {
        Expect(false, "thumbnail-worker.nested-mask-fixtures-created");
        return;
    }
    const Raster nested =
        Raster::MakeSparse(128, 128, RasterFormat::Gray8, Raster{}, 0, 0, {RasterTile{0, 0, largeLeaf}}, 61);
    const Raster sparse =
        Raster::MakeSparse(2, 2, RasterFormat::Gray8, Raster{}, 0, 0, {RasterTile{0, 0, nested, 126, 126, 2, 2}}, 9);
    if (!nested.HasStorage() || !sparse.HasStorage())
    {
        Expect(false, "thumbnail-worker.nested-mask-fixtures-created");
        return;
    }

    const std::size_t sourceBytesBefore = sparse.ByteSize();
    const std::size_t nestedBytesBefore = nested.ByteSize();
    const std::size_t leafBytesBefore = largeLeaf.ByteSize();
    const Raster thumbnail = ThumbnailRasterForWork(sparse, 2);
    constexpr std::array<std::uint8_t, 4> expected{201, 202, 203, 204};
    const bool privatePixels = thumbnail.HasStorage() && thumbnail.Width() == 2 && thumbnail.Height() == 2 &&
                               thumbnail.Format() == RasterFormat::Gray8 && thumbnail.IsMaterialized() &&
                               thumbnail.Identity() != sparse.Identity() && thumbnail.ByteSize() == expected.size() &&
                               std::equal(expected.begin(), expected.end(), thumbnail.Data());
    Expect(privatePixels && sparse.ByteSize() == sourceBytesBefore && nested.ByteSize() == nestedBytesBefore &&
               largeLeaf.ByteSize() == leafBytesBefore && !sparse.IsMaterialized() && !nested.IsMaterialized() &&
               largeLeaf.IsMaterialized(),
           "thumbnail-worker.fitting-nested-sparse-gray-copies-only-private-thumbnail");

    constexpr std::array<std::uint8_t, 4> denseGrayBytes{7, 8, 9, 10};
    constexpr std::array<std::uint8_t, 16> denseBgraBytes{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
    const Raster denseGray = Gray(2, 2, denseGrayBytes);
    const Raster denseBgra = Bgra(2, 2, denseBgraBytes);
    const Raster grayAlias = ThumbnailRasterForWork(denseGray, 2);
    const Raster bgraAlias = ThumbnailRasterForWork(denseBgra, 2);
    Expect(denseGray.HasStorage() && denseBgra.HasStorage() && grayAlias == denseGray && bgraAlias == denseBgra,
           "thumbnail-worker.fitting-dense-gray-and-bgra-preserve-alias");

    CancelSource localCancellation;
    (void)localCancellation.Cancel();
    std::stop_source shutdown;
    (void)shutdown.request_stop();
    const CancelToken shutdownCancellation{std::stop_token{}, shutdown.get_token()};
    const Raster locallyCancelled = ThumbnailRasterForWork(sparse, 2, localCancellation.Token());
    const Raster shutdownCancelled = ThumbnailRasterForWork(sparse, 2, shutdownCancellation);
    Expect(!locallyCancelled.HasStorage() && !shutdownCancelled.HasStorage() &&
               sparse.ByteSize() == sourceBytesBefore && nested.ByteSize() == nestedBytesBefore &&
               largeLeaf.ByteSize() == leafBytesBefore && !sparse.IsMaterialized() && !nested.IsMaterialized() &&
               largeLeaf.IsMaterialized(),
           "thumbnail-worker.local-and-shutdown-cancel-publish-nothing-or-grow-source");
}

void CarriedLargeSparseMaskUsesSampledRows()
{
    constexpr std::array<std::uint8_t, 1> sample{99};
    const Raster leaf = Gray(1, 1, sample);
    if (!leaf.HasStorage())
    {
        Expect(false, "carried-sparse.fixture");
        return;
    }
    const Raster source = Raster::MakeSparse(10000, 10000, RasterFormat::Gray8, {}, 0, 0, {RasterTile{0, 0, leaf}}, 17);
    if (!source.HasStorage())
    {
        Expect(false, "carried-sparse.large-logical-fixture");
        return;
    }
    const Raster carried = CarriedMaskForWork(source, 2, 2, 1, 1, 4, 3, 255);
    // The old 2x2 grid samples source (0,0),(5000,0),(0,5000),(5000,5000). Its top left lands at (1,1).
    constexpr std::array<std::uint8_t, 12> expected{255, 255, 255, 255, 255, 99, 17, 255, 255, 17, 17, 255};
    Expect(carried.HasStorage() && carried.Data() && std::equal(expected.begin(), expected.end(), carried.Data()),
           "carried-sparse.large-source-small-grid-literal-coordinate-reference");
    Expect(!source.IsMaterialized(), "carried-sparse.large-source-remains-unassembled");
    CancelSource cancelled;
    (void)cancelled.Cancel();
    Expect(!CarriedMaskForWork(source, 2, 2, 1, 1, 4, 3, 255, cancelled.Token()).HasStorage(),
           "carried-sparse.precancel-publishes-nothing");
}
} // namespace

int RunRasterWorkTests()
{
    gFailures = 0;
    ContiguousCropsAndRows();
    SparseFillBaseAndTiles();
    NestedSparseAndSharing();
    DepthAndPublication();
    CancellationAndInvalidInputs();
    CopyAndCropOutputs();
    StretchedRasterForWorkCases();
    ThumbnailRasterForWorkCases();
    FittingSparseMaskThumbnailCases();
    FittingSparseMaskWorkerThumbnailCases();
    CarriedLargeSparseMaskUsesSampledRows();
    return gFailures;
}
