#include "Document.Tests.RasterConcurrency.h"

#include "Document.h"

#include <array>
#include <cstdio>
#include <cstring>
#include <latch>
#include <new>
#include <system_error>
#include <thread>
#include <vector>

namespace
{
constexpr int kWidth = 97;
constexpr int kHeight = 65;
constexpr int kThreads = 8;
constexpr int kRepeats = 12;

// Deliberately independent of Raster::ReadRegion: offset base, a windowed replacement, uncovered fill,
// and the separate outside fill. Both byte formats use the same geometric expectations.
std::uint8_t Expected(int x, int y, int channel) noexcept
{
    if (x < 0 || y < 0 || x >= kWidth || y >= kHeight)
    {
        return 219;
    }
    if (x >= 21 && x < 34 && y >= 15 && y < 32)
    {
        return static_cast<std::uint8_t>(((x - 21 + 2) * 7 + (y - 15 + 1) * 11 + channel * 13) % 256);
    }
    if (x < 26 && y >= 7 && y < 36)
    {
        return static_cast<std::uint8_t>(((x + 5) * 3 + (y - 7) * 5 + channel * 17) % 256);
    }
    return 47;
}

Raster Source(RasterFormat format)
{
    const int channels = format == RasterFormat::Gray8 ? 1 : 4;
    const auto make = [channels, format](int width, int height, bool tile)
    {
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(width * height * channels));
        for (int y = 0; y < height; ++y)
        {
            for (int x = 0; x < width; ++x)
            {
                for (int c = 0; c < channels; ++c)
                {
                    bytes[static_cast<std::size_t>((y * width + x) * channels + c)] = static_cast<std::uint8_t>(
                        (x * (tile ? 7 : 3) + y * (tile ? 11 : 5) + c * (tile ? 13 : 17)) % 256);
                }
            }
        }
        return Raster::Make(width, height, format, bytes.data(), bytes.size());
    };
    return Raster::MakeSparse(kWidth, kHeight, format, make(31, 29, false), -5, 7,
                              {RasterTile{21, 15, make(33, 23, true), 2, 1, 13, 17}}, 47);
}

bool CheckPixels(const Raster& raster, const std::uint8_t* pixels)
{
    if (pixels == nullptr)
    {
        return false;
    }
    for (int y = 0; y < kHeight; ++y)
    {
        for (int x = 0; x < kWidth; ++x)
        {
            for (int c = 0; c < raster.Channels(); ++c)
            {
                if (pixels[y * raster.Stride() + x * raster.Channels() + c] != Expected(x, y, c))
                {
                    return false;
                }
            }
        }
    }
    return true;
}

bool CheckRegion(const Raster& raster)
{
    const int stride = (kWidth + 2) * raster.Channels() + 13;
    std::vector<std::uint8_t> region(static_cast<std::size_t>(stride * (kHeight + 2)), 193);
    raster.ReadRegion(-1, -1, kWidth + 2, kHeight + 2, region.data(), stride, 219);
    for (int y = 0; y < kHeight + 2; ++y)
    {
        for (int x = 0; x < kWidth + 2; ++x)
        {
            for (int c = 0; c < raster.Channels(); ++c)
            {
                if (region[static_cast<std::size_t>(y * stride + x * raster.Channels() + c)] !=
                    Expected(x - 1, y - 1, c))
                {
                    return false;
                }
            }
        }
        for (int pad = (kWidth + 2) * raster.Channels(); pad < stride; ++pad)
        {
            if (region[static_cast<std::size_t>(y * stride + pad)] != 193)
            {
                return false;
            }
        }
    }
    return true;
}

bool CheckThumbnail(const Raster& raster)
{
    const Raster thumb = Raster::MakeThumbnail(raster, 23);
    if (thumb.Width() != 23 || thumb.Height() != 15 || thumb.Data() == nullptr)
    {
        return false;
    }
    for (int y = 0; y < thumb.Height(); ++y)
    {
        for (int x = 0; x < thumb.Width(); ++x)
        {
            for (int c = 0; c < raster.Channels(); ++c)
            {
                if (thumb.Data()[y * thumb.Stride() + x * raster.Channels() + c] !=
                    Expected(x * kWidth / thumb.Width(), y * kHeight / thumb.Height(), c))
                {
                    return false;
                }
            }
        }
    }
    return true;
}
} // namespace

int RunRasterConcurrencyTests(int rounds)
{
    for (int round = 0; round < rounds; ++round)
    {
        const Raster raster = Source(round % 2 == 0 ? RasterFormat::Gray8 : RasterFormat::Bgra8888);
        const Raster sibling = Raster::MakeSparse(kWidth, kHeight, raster.Format(), raster.Base(), raster.BaseX(),
                                                  raster.BaseY(), raster.Tiles(), raster.Fill());
        const bool sharedBase = round % 4 >= 2;
        if (raster.IsMaterialized() || raster.ByteSize() != 0 || sibling.IsMaterialized() || sibling.ByteSize() != 0 ||
            raster.Base() != sibling.Base())
        {
            std::printf("FAIL sparse.concurrent-starts-unassembled\n");
            return 1;
        }
        std::latch start{1};
        std::array<bool, kThreads> passed{};
        std::array<const std::uint8_t*, kThreads> pointers{};
        std::vector<std::jthread> threads;
        threads.reserve(kThreads);
        try
        {
            for (int thread = 0; thread < kThreads; ++thread)
            {
                threads.emplace_back(
                    [&, thread, owned = sharedBase && thread % 2 != 0 ? sibling : raster]
                    {
                        start.wait();
                        bool valid = true;
                        for (int repeat = 0; repeat < kRepeats; ++repeat)
                        {
                            if (thread < kThreads / 2)
                            {
                                const std::uint8_t* pixels = owned.Data();
                                valid &= CheckPixels(owned, pixels);
                                valid &= pointers[thread] == nullptr || pointers[thread] == pixels;
                                pointers[thread] = pixels;
                            }
                            else
                            {
                                valid &= CheckRegion(owned) && CheckThumbnail(owned);
                            }
                            const bool published = owned.IsMaterialized();
                            const std::size_t bytes = owned.ByteSize();
                            const std::size_t fullSize = static_cast<std::size_t>(owned.Stride() * owned.Height());
                            valid &= published ? bytes == fullSize : bytes == 0 || bytes == fullSize;
                            valid &=
                                owned.Identity() == (sharedBase && thread % 2 != 0 ? sibling : raster).Identity() &&
                                owned.IsSparse();
                            int storageCount = 0;
                            owned.ForEachStorage(
                                [&](const Raster& storage)
                                {
                                    (void)storage.ByteSize();
                                    ++storageCount;
                                });
                            valid &= storageCount == 3;
                        }
                        passed[thread] = valid;
                    });
            }
        }
        catch (const std::system_error& error)
        {
            // If thread creation fails, release already-started readers before their jthread destructors join.
            start.count_down();
            threads.clear();
            std::printf("FAIL sparse.concurrent-thread-create %s\n", error.what());
            return 1;
        }
        catch (const std::bad_alloc&)
        {
            start.count_down();
            threads.clear();
            std::printf("FAIL sparse.concurrent-thread-allocation\n");
            return 1;
        }
        start.count_down();
        threads.clear();
        for (int thread = 0; thread < kThreads; ++thread)
        {
            const Raster& target = sharedBase && thread % 2 != 0 ? sibling : raster;
            if (!passed[thread] || (thread < kThreads / 2 && pointers[thread] != target.Data()))
            {
                std::printf("FAIL sparse.concurrent-readers round=%d thread=%d\n", round, thread);
                return 1;
            }
        }
        if (sharedBase && raster.Data() == sibling.Data())
        {
            std::printf("FAIL sparse.concurrent-distinct-caches\n");
            return 1;
        }
        const Raster survivor = raster;
        if (!CheckPixels(survivor, pointers[0]) || !CheckRegion(survivor))
        {
            std::printf("FAIL sparse.concurrent-published-lifetime\n");
            return 1;
        }
    }
    std::printf("PASS sparse.concurrent-readers rounds=%d threads=%d repeats=%d\n", rounds, kThreads, kRepeats);
    return 0;
}
