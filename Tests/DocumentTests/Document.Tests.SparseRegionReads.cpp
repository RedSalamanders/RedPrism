#include "Document.Tests.SparseRegionReads.h"

#include "Document.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <latch>
#include <span>
#include <thread>
#include <vector>

namespace
{
int gFailures = 0;

void Expect(bool condition, const char* label) noexcept
{
    std::printf("%s sparse-region-reads.%s\n", condition ? "PASS" : "FAIL", label);
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

template <std::size_t Size>
bool Equals(const Raster& raster, RasterFormat format, int width, int height,
            const std::array<std::uint8_t, Size>& expected)
{
    const std::size_t rowBytes =
        static_cast<std::size_t>(width) * static_cast<std::size_t>(format == RasterFormat::Gray8 ? 1 : 4);
    if (!raster.HasStorage() || raster.Format() != format || raster.Width() != width || raster.Height() != height ||
        expected.size() != rowBytes * static_cast<std::size_t>(height))
    {
        return false;
    }
    const std::uint8_t* const actual = raster.Data();
    if (actual == nullptr)
    {
        return false;
    }
    for (int y = 0; y < height; ++y)
    {
        const auto* const row = actual + static_cast<std::size_t>(y) * static_cast<std::size_t>(raster.Stride());
        const auto expectedOffset = static_cast<std::size_t>(y) * rowBytes;
        if (!std::equal(expected.begin() + static_cast<std::ptrdiff_t>(expectedOffset),
                        expected.begin() + static_cast<std::ptrdiff_t>(expectedOffset + rowBytes), row))
        {
            return false;
        }
    }
    return true;
}

void NestedBgraReadRegionUsesClippedWindowsWithoutPublishing()
{
    constexpr std::array<std::uint8_t, 48> leafBytes{
        1,  2,  3,  4,  11, 12, 13, 14, 21, 22, 23, 24, 31, 32, 33, 34, 41,  42,  43,  44,  51,  52,  53,  54,
        61, 62, 63, 64, 71, 72, 73, 74, 81, 82, 83, 84, 91, 92, 93, 94, 101, 102, 103, 104, 111, 112, 113, 114};
    const Raster leaf = Bgra(4, 3, leafBytes);
    const Raster inner =
        Raster::MakeSparse(3, 3, RasterFormat::Bgra8888, Raster{}, 0, 0, {RasterTile{0, 0, leaf, 1, 1, 2, 2}}, 41);
    // Clipping this tile at x=-1 advances its retained sourceX from 0 to 1.
    const Raster outer =
        Raster::MakeSparse(4, 3, RasterFormat::Bgra8888, Raster{}, 0, 0, {RasterTile{-1, 0, inner, 0, 0, 3, 3}}, 77);
    constexpr std::array<std::uint8_t, 56> expected{9,  9,  9,  9,  61,  62,  63,  64,  41, 41, 41,   41,   77,   77,
                                                    77, 77, 77, 77, 77,  77,  9,   9,   9,  9,  0xCC, 0xCC, 0xCC, 0xCC,
                                                    9,  9,  9,  9,  101, 102, 103, 104, 41, 41, 41,   41,   77,   77,
                                                    77, 77, 77, 77, 77,  77,  9,   9,   9,  9,  0xCC, 0xCC, 0xCC, 0xCC};
    std::array<std::uint8_t, expected.size()> actual{};
    actual.fill(0xCC);
    const std::uint64_t epoch = Raster::ResidencyEpoch();
    outer.ReadRegion(-1, 0, 6, 2, actual.data(), 28, 9);

    const bool sourceWindows = outer.Tiles().size() == 1 && outer.Tiles()[0].sourceX == 1 &&
                               inner.Tiles().size() == 1 && inner.Tiles()[0].sourceX == 1 &&
                               inner.Tiles()[0].sourceY == 1;
    Expect(sourceWindows && actual == expected, "bgra.nested-nonzero-source-window-clipped-read-fill-and-row-padding");
    Expect(outer.IsSparse() && inner.IsSparse() && !outer.IsMaterialized() && !inner.IsMaterialized() &&
               !leaf.IsSparse() && Raster::ResidencyEpoch() == epoch,
           "bgra.readregion-keeps-all-sparse-source-caches-and-epoch-unchanged");
}

Raster MakeGrayNested()
{
    constexpr std::array<std::uint8_t, 6> leafBytes{10, 20, 30, 40, 50, 60};
    const Raster leaf = Gray(3, 2, leafBytes);
    const Raster inner =
        Raster::MakeSparse(2, 2, RasterFormat::Gray8, Raster{}, 0, 0, {RasterTile{1, 0, leaf, 1, 0, 1, 2}}, 100);
    return Raster::MakeSparse(4, 2, RasterFormat::Gray8, Raster{}, 0, 0, {RasterTile{1, 0, inner}}, 200);
}

void NestedGrayReadRegionPreservesFillOverrideAndPadding()
{
    const Raster sparse = MakeGrayNested();
    constexpr std::array<std::uint8_t, 16> expected{9, 200, 100, 20, 200, 9, 0xCC, 0xCC,
                                                    9, 200, 100, 50, 200, 9, 0xCC, 0xCC};
    std::array<std::uint8_t, expected.size()> actual{};
    actual.fill(0xCC);
    const std::uint64_t epoch = Raster::ResidencyEpoch();
    sparse.ReadRegion(-1, 0, 6, 2, actual.data(), 8, 9);

    Expect(actual == expected, "gray8.nested-fill-overridden-by-tile-and-clipped-read-preserves-padding");
    Expect(sparse.IsSparse() && sparse.Tiles().size() == 1 && sparse.Tiles()[0].pixels.IsSparse() &&
               !sparse.IsMaterialized() && !sparse.Tiles()[0].pixels.IsMaterialized() &&
               Raster::ResidencyEpoch() == epoch,
           "gray8.readregion-leaves-nested-inputs-unmaterialized");
}

void CropProducesPrivatePixelsFromSparseReads()
{
    const Raster sparse = MakeGrayNested();
    constexpr std::array<std::uint8_t, 12> expected{9, 200, 100, 20, 200, 9, 9, 200, 100, 50, 200, 9};
    const std::uint64_t epoch = Raster::ResidencyEpoch();
    const Raster crop = sparse.Crop(-1, 0, 6, 2, 9);
    const bool privateOutput =
        crop.HasStorage() && !crop.IsSparse() && Equals(crop, RasterFormat::Gray8, 6, 2, expected);

    Expect(privateOutput, "crop.private-contiguous-literal-bytes-include-both-fills");
    Expect(sparse.IsSparse() && !sparse.IsMaterialized() && !sparse.Tiles()[0].pixels.IsMaterialized() &&
               Raster::ResidencyEpoch() == epoch,
           "crop.does-not-publish-a-shared-source-cache");
}

void ReadRegionAndCropWorkUnderHeldResidencyLease()
{
    constexpr std::array<std::uint8_t, 8> leafBytes{7, 8, 9, 10, 11, 12, 13, 14};
    const Raster leaf = Bgra(2, 1, leafBytes);
    const Raster sparse =
        Raster::MakeSparse(3, 1, RasterFormat::Bgra8888, Raster{}, 0, 0, {RasterTile{1, 0, leaf}}, 33);
    constexpr std::array<std::uint8_t, 12> readExpected{33, 33, 33, 33, 7, 8, 9, 10, 11, 12, 13, 14};
    constexpr std::array<std::uint8_t, 8> cropExpected{33, 33, 33, 33, 7, 8, 9, 10};
    std::array<std::uint8_t, readExpected.size()> actual{};
    const std::uint64_t epoch = Raster::ResidencyEpoch();
    Raster crop;
    bool heldRead = false;
    {
        auto lease = Raster::TryResidencyLease();
        const bool dataRefused = lease.Held() && sparse.Data() == nullptr;
        sparse.ReadRegion(0, 0, 3, 1, actual.data(), 12, 0);
        crop = sparse.Crop(0, 0, 2, 1, 0);
        heldRead = dataRefused && actual == readExpected && lease.Held() && sparse.IsSparse() &&
                   !sparse.IsMaterialized() && Raster::ResidencyEpoch() == epoch;
        lease.Reset();
    }
    const bool privateCrop = Equals(crop, RasterFormat::Bgra8888, 2, 1, cropExpected);

    Expect(heldRead, "lease.readregion-and-private-crop-succeed-while-data-refuses-source-assembly");
    Expect(privateCrop && !sparse.IsMaterialized() && Raster::ResidencyEpoch() == epoch,
           "lease.released-before-reading-private-crop-output");
}

void NestedReadRegionAndCropWorkUnderHeldResidencyLease()
{
    const Raster sparse = MakeGrayNested();
    constexpr std::array<std::uint8_t, 12> expected{9, 200, 100, 20, 200, 9, 9, 200, 100, 50, 200, 9};
    std::array<std::uint8_t, expected.size()> actual{};
    const auto epoch = Raster::ResidencyEpoch();
    Raster crop;
    bool completeRead = false;
    {
        auto lease = Raster::TryResidencyLease();
        sparse.ReadRegion(-1, 0, 6, 2, actual.data(), 6, 9);
        crop = sparse.Crop(-1, 0, 6, 2, 9);
        completeRead = lease.Held() && actual == expected;
        lease.Reset();
    }
    Expect(completeRead, "lease.nested-tiles-copy-all-literal-pixels");
    Expect(Equals(crop, RasterFormat::Gray8, 6, 2, expected), "lease.nested-crop-has-all-literal-pixels");
    Expect(!sparse.IsMaterialized() && !sparse.Tiles()[0].pixels.IsMaterialized() && Raster::ResidencyEpoch() == epoch,
           "lease.nested-read-and-crop-publish-no-cache");
}

void ExplicitDataKeepsItsNestedPublicationContract()
{
    const Raster sparse = MakeGrayNested();
    const auto identity = sparse.Identity();
    const auto epoch = Raster::ResidencyEpoch();
    constexpr std::array<std::uint8_t, 8> expected{200, 100, 20, 200, 200, 100, 50, 200};
    std::array<std::uint8_t, 8> region{};
    sparse.ReadRegion(0, 0, 4, 2, region.data(), 4, 0);
    Expect(region == expected && !sparse.IsMaterialized() && !sparse.Tiles()[0].pixels.IsMaterialized() &&
               Raster::ResidencyEpoch() == epoch,
           "explicit-data.region-first-has-literal-pixels-without-publication");
    const auto* pixels = sparse.Data();
    Expect(pixels && std::equal(expected.begin(), expected.end(), pixels) && sparse.IsMaterialized() &&
               sparse.Tiles()[0].pixels.IsMaterialized() && Raster::ResidencyEpoch() == epoch + 2,
           "explicit-data.still-publishes-inner-and-outer-caches-with-literal-pixels");
    const auto publishedEpoch = Raster::ResidencyEpoch();
    Expect(sparse.Identity() == identity && pixels && sparse.Data() == pixels &&
               Raster::ResidencyEpoch() == publishedEpoch,
           "explicit-data.borrowed-pointer-and-storage-identity-stay-stable");
}

void LaterTileOverridesAnEarlierNestedWindow()
{
    constexpr std::array<std::uint8_t, 2> leafBytes{10, 20};
    constexpr std::array<std::uint8_t, 1> replacementBytes{99};
    const auto inner =
        Raster::MakeSparse(2, 1, RasterFormat::Gray8, {}, 0, 0, {RasterTile{0, 0, Gray(2, 1, leafBytes)}}, 100);
    const auto outer =
        Raster::MakeSparse(3, 1, RasterFormat::Gray8, {}, 0, 0,
                           {RasterTile{0, 0, inner}, RasterTile{1, 0, Gray(1, 1, replacementBytes)}}, 200);
    const auto epoch = Raster::ResidencyEpoch();
    constexpr std::array<std::uint8_t, 6> expected{9, 10, 99, 200, 9, 0xCC};
    std::array<std::uint8_t, 6> region{};
    region.fill(0xCC);
    outer.ReadRegion(-1, 0, 5, 1, region.data(), 6, 9);
    Expect(region == expected, "overlap.last-window-wins-and-read-padding-stays-untouched");
    Expect(!outer.IsMaterialized() && !inner.IsMaterialized() && Raster::ResidencyEpoch() == epoch,
           "overlap.nested-reader-publishes-no-source-cache");
}

void ConcurrentRegionReadersAndExplicitPublicationAgree()
{
    const Raster source = MakeGrayNested();
    constexpr std::array<std::uint8_t, 8> expected{200, 100, 20, 200, 200, 100, 50, 200};
    std::latch ready{3};
    std::latch go{1};
    std::array<bool, 2> readComplete{true, true};
    bool publicationComplete = false;
    const auto read = [&](std::size_t index)
    {
        ready.count_down();
        go.wait();
        for (int iteration = 0; iteration < 4096; ++iteration)
        {
            std::array<std::uint8_t, 12> actual{};
            actual.fill(0xCC);
            source.ReadRegion(0, 0, 4, 2, actual.data(), 6, 0);
            constexpr std::array<std::uint8_t, 12> padded{200, 100, 20, 200, 0xCC, 0xCC, 200, 100, 50, 200, 0xCC, 0xCC};
            readComplete[index] = readComplete[index] && actual == padded;
        }
    };
    std::thread first{read, 0};
    std::thread second{read, 1};
    std::thread publisher{[&]
                          {
                              ready.count_down();
                              go.wait();
                              const auto* pixels = source.Data();
                              publicationComplete = pixels && std::equal(expected.begin(), expected.end(), pixels);
                          }};
    ready.wait();
    go.count_down();
    first.join();
    second.join();
    publisher.join();
    Expect(readComplete[0] && readComplete[1],
           "concurrency.two-event-gated-region-readers-preserve-every-pixel-and-padding");
    Expect(publicationComplete && source.IsMaterialized() && source.Tiles()[0].pixels.IsMaterialized(),
           "concurrency.explicit-publisher-keeps-the-same-literal-nested-result");
}

} // namespace

int RunSparseRegionReadsTests()
{
    gFailures = 0;
    NestedBgraReadRegionUsesClippedWindowsWithoutPublishing();
    NestedGrayReadRegionPreservesFillOverrideAndPadding();
    CropProducesPrivatePixelsFromSparseReads();
    ReadRegionAndCropWorkUnderHeldResidencyLease();
    NestedReadRegionAndCropWorkUnderHeldResidencyLease();
    ExplicitDataKeepsItsNestedPublicationContract();
    LaterTileOverridesAnEarlierNestedWindow();
    ConcurrentRegionReadersAndExplicitPublicationAgree();
    return gFailures;
}
