#include "Executor.h"

#include "ExecutorTests.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <stop_token>
#include <vector>

namespace
{
int gFailures = 0;

void Expect(bool condition, const char* name)
{
    if (condition)
    {
        std::printf("PASS %s\n", name);
        std::fflush(stdout);
        return;
    }
    std::printf("FAIL %s\n", name);
    std::fflush(stdout);
    ++gFailures;
}

bool IsZeroTile(Tile tile) noexcept
{
    return tile.x == 0 && tile.y == 0 && tile.width == 0 && tile.height == 0;
}

std::uint32_t Formula(std::uint32_t x, std::uint32_t y) noexcept
{
    return (x + 3U) * 1009U + (y + 5U) * 9176U + x * y * 37U;
}
} // namespace

int RunExecutorValueTests()
{
    gFailures = 0;

    const TileGrid uneven{10, 7, 4, 3};
    Expect(uneven.IsValid() && uneven.Columns() == 3 && uneven.Count() == 9, "executor.grid-uneven-count");
    const Tile topRight = uneven.At(2);
    Expect(topRight.x == 8 && topRight.y == 0 && topRight.width == 2 && topRight.height == 3,
           "executor.grid-uneven-right-edge");
    const Tile bottomLeft = uneven.At(7);
    Expect(bottomLeft.x == 4 && bottomLeft.y == 6 && bottomLeft.width == 4 && bottomLeft.height == 1,
           "executor.grid-uneven-bottom-edge");
    const Tile bottomRight = uneven.At(8);
    Expect(bottomRight.x == 8 && bottomRight.y == 6 && bottomRight.width == 2 && bottomRight.height == 1,
           "executor.grid-uneven-corner");
    Expect(IsZeroTile(uneven.At(uneven.Count())), "executor.grid-out-of-range");

    const TileGrid emptyWidth{0, 7, 4, 3};
    const TileGrid emptyHeight{10, 0, 4, 3};
    Expect(emptyWidth.IsValid() && emptyWidth.Columns() == 0 && emptyWidth.Count() == 0 && IsZeroTile(emptyWidth.At(0)),
           "executor.grid-empty-width");
    Expect(emptyHeight.IsValid() && emptyHeight.Count() == 0 && IsZeroTile(emptyHeight.At(0)),
           "executor.grid-empty-height");

    const TileGrid zeroTileWidth{10, 7, 0, 3};
    const TileGrid zeroTileHeight{10, 7, 4, 0};
    Expect(!zeroTileWidth.IsValid() && zeroTileWidth.Columns() == 0 && zeroTileWidth.Count() == 0 &&
               IsZeroTile(zeroTileWidth.At(0)),
           "executor.grid-zero-tile-width");
    Expect(!zeroTileHeight.IsValid() && zeroTileHeight.Count() == 0 && IsZeroTile(zeroTileHeight.At(0)),
           "executor.grid-zero-tile-height");

    InlineExecutor executor;
    int emptyGridCalls = 0;
    const bool emptyGridCompleted = ParallelFor(executor, TileGrid{0, 7, 4, 3}, CancelToken{},
                                                [&emptyGridCalls](Tile, CancelToken) noexcept
                                                {
                                                    ++emptyGridCalls;
                                                    return true;
                                                });
    Expect(emptyGridCompleted && emptyGridCalls == 0, "executor.empty-valid-grid-succeeds-without-body");
    int invalidGridCalls = 0;
    const bool invalidGridCompleted = ParallelFor(executor, TileGrid{7, 5, 0, 3}, CancelToken{},
                                                  [&invalidGridCalls](Tile, CancelToken) noexcept
                                                  {
                                                      ++invalidGridCalls;
                                                      return true;
                                                  });
    Expect(!invalidGridCompleted && invalidGridCalls == 0, "executor.invalid-zero-tile-grid-fails-without-body");

    constexpr std::uint32_t kMaxDimension = (std::numeric_limits<std::uint32_t>::max)();
    const std::uint64_t maxTileCount = static_cast<std::uint64_t>(kMaxDimension) * kMaxDimension;
    const TileGrid maximumGrid{kMaxDimension, kMaxDimension, 1, 1};
    const Tile lastMaximumTile = maximumGrid.At(maxTileCount - 1);
    Expect(maximumGrid.IsValid() && maximumGrid.Columns() == kMaxDimension && maximumGrid.Count() == maxTileCount &&
               lastMaximumTile.x == kMaxDimension - 1 && lastMaximumTile.y == kMaxDimension - 1 &&
               lastMaximumTile.width == 1 && lastMaximumTile.height == 1,
           "executor.grid-maximum-dimensions");
    const TileGrid maximumExtentGrid{kMaxDimension, kMaxDimension, kMaxDimension, kMaxDimension};
    const Tile fullExtent = maximumExtentGrid.At(0);
    Expect(maximumExtentGrid.Columns() == 1 && maximumExtentGrid.Count() == 1 && fullExtent.width == kMaxDimension &&
               fullExtent.height == kMaxDimension,
           "executor.grid-maximum-extent-rounding");

    CancelSource cancelledBeforeStart;
    cancelledBeforeStart.Cancel();
    int cancelledVisits = 0;
    const bool completedCancelled = ParallelFor(executor, uneven, cancelledBeforeStart.Token(),
                                                [&cancelledVisits](Tile, CancelToken) noexcept
                                                {
                                                    ++cancelledVisits;
                                                    return true;
                                                });
    Expect(!completedCancelled && cancelledVisits == 0, "executor.cancel-before-first-tile");

    CancelSource cancelInsideRow;
    unsigned int tilesStarted = 0;
    unsigned int rowsVisited = 0;
    const bool completedAfterRowCancel =
        ParallelFor(executor, TileGrid{8, 6, 4, 3}, cancelInsideRow.Token(),
                    [&cancelInsideRow, &tilesStarted, &rowsVisited](Tile tile, CancelToken token) noexcept
                    {
                        ++tilesStarted;
                        for (std::uint32_t row = 0; row < tile.height; ++row)
                        {
                            if (token.IsCancelled())
                            {
                                return false;
                            }
                            ++rowsVisited;
                            if (row == 0)
                            {
                                cancelInsideRow.Cancel();
                            }
                        }
                        return true;
                    });
    Expect(!completedAfterRowCancel && tilesStarted == 1 && rowsVisited == 1,
           "executor.cancel-inside-row-stops-later-tiles");

    unsigned int incompleteCalls = 0;
    const bool completedIncompleteBody = ParallelFor(executor, TileGrid{5, 1, 1, 1}, CancelToken{},
                                                     [&incompleteCalls](Tile, CancelToken) noexcept
                                                     {
                                                         ++incompleteCalls;
                                                         return incompleteCalls != 2;
                                                     });
    Expect(!completedIncompleteBody && incompleteCalls == 2, "executor.incomplete-body-aborts-remaining-tiles");

    CancelToken copiedToken;
    {
        CancelSource source;
        const CancelToken originalToken = source.Token();
        copiedToken = originalToken;
        source.Cancel();
    }
    Expect(copiedToken.IsCancelled(), "executor.cancel-token-copy-outlives-source");
    int expiredSourceVisits = 0;
    const bool completedWithExpiredSource = ParallelFor(executor, TileGrid{2, 1, 1, 1}, copiedToken,
                                                        [&expiredSourceVisits](Tile, CancelToken) noexcept
                                                        {
                                                            ++expiredSourceVisits;
                                                            return true;
                                                        });
    Expect(!completedWithExpiredSource && expiredSourceVisits == 0, "executor.expired-source-token-remains-cancelled");

    std::stop_source cancelledSecondarySource;
    cancelledSecondarySource.request_stop();
    const CancelToken cancelledSecondaryToken{{}, cancelledSecondarySource.get_token()};
    int cancelledSecondaryVisits = 0;
    const bool completedWithCancelledSecondary = ParallelFor(executor, TileGrid{2, 1, 1, 1}, cancelledSecondaryToken,
                                                             [&cancelledSecondaryVisits](Tile, CancelToken) noexcept
                                                             {
                                                                 ++cancelledSecondaryVisits;
                                                                 return true;
                                                             });
    Expect(!completedWithCancelledSecondary && cancelledSecondaryVisits == 0,
           "executor.cancelled-secondary-token-survives-inline-shutdown-token");

    CancelSource localCallbackSource;
    std::stop_source shutdownCallbackSource;
    const CancelToken callbackToken{localCallbackSource.Token().LocalToken(), shutdownCallbackSource.get_token()};
    bool localCallbackInvoked = false;
    bool shutdownCallbackInvoked = false;
    const std::stop_callback localCallback(callbackToken.LocalToken(), [&]() noexcept { localCallbackInvoked = true; });
    const std::stop_callback shutdownCallback(callbackToken.ShutdownToken(),
                                              [&]() noexcept { shutdownCallbackInvoked = true; });
    (void)localCallbackSource.Cancel();
    Expect(localCallbackInvoked && !shutdownCallbackInvoked, "executor.local-stop-callback-is-independent");
    (void)shutdownCallbackSource.request_stop();
    Expect(shutdownCallbackInvoked, "executor.shutdown-stop-callback-is-independent");

    CancelSource alreadyStoppedLocalSource;
    (void)alreadyStoppedLocalSource.Cancel();
    bool alreadyStoppedLocalObserved = false;
    const std::stop_callback alreadyStoppedLocalCallback(alreadyStoppedLocalSource.Token().LocalToken(),
                                                         [&]() noexcept { alreadyStoppedLocalObserved = true; });
    std::stop_source alreadyStoppedShutdownSource;
    (void)alreadyStoppedShutdownSource.request_stop();
    bool alreadyStoppedShutdownObserved = false;
    const std::stop_callback alreadyStoppedShutdownCallback(alreadyStoppedShutdownSource.get_token(),
                                                            [&]() noexcept { alreadyStoppedShutdownObserved = true; });
    Expect(alreadyStoppedLocalObserved, "executor.local-stop-callback-observes-prior-stop");
    Expect(alreadyStoppedShutdownObserved, "executor.shutdown-stop-callback-observes-prior-stop");

    CancelSource retainedLocalSource;
    std::stop_source retainedShutdownSource;
    const CancelToken retainedLocalToken = retainedLocalSource.Token();
    const CancelToken combinedToken = retainedLocalToken.WithShutdown(retainedShutdownSource.get_token());
    (void)retainedShutdownSource.request_stop();
    Expect(combinedToken.IsCancelled() && !combinedToken.LocalToken().stop_requested() &&
               combinedToken.ShutdownToken().stop_requested(),
           "executor.with-shutdown-exposes-independent-shutdown-state");
    (void)retainedLocalSource.Cancel();
    Expect(combinedToken.LocalToken().stop_requested(), "executor.with-shutdown-retains-local-state");

    constexpr std::uint32_t kWidth = 23;
    constexpr std::uint32_t kHeight = 13;
    const TileGrid fillGrid{kWidth, kHeight, 6, 4};
    const std::size_t pixelCount = static_cast<std::size_t>(kWidth) * kHeight;
    std::vector<std::uint32_t> pixels(pixelCount, 0);
    std::vector<unsigned int> visits(pixelCount, 0);
    std::uint64_t tiledChecksum = 0;
    const bool filled = ParallelFor(executor, fillGrid, CancelToken{},
                                    [&pixels, &visits, &tiledChecksum](Tile tile, CancelToken) noexcept
                                    {
                                        for (std::uint32_t yOffset = 0; yOffset < tile.height; ++yOffset)
                                        {
                                            const std::uint32_t y = tile.y + yOffset;
                                            for (std::uint32_t xOffset = 0; xOffset < tile.width; ++xOffset)
                                            {
                                                const std::uint32_t x = tile.x + xOffset;
                                                const std::size_t index = static_cast<std::size_t>(y) * kWidth + x;
                                                const std::uint32_t value = Formula(x, y);
                                                pixels[index] = value;
                                                ++visits[index];
                                                tiledChecksum += value;
                                            }
                                        }
                                        return true;
                                    });

    std::uint64_t serialChecksum = 0;
    bool matchesSerial = true;
    bool visitedOnce = true;
    for (std::uint32_t y = 0; y < kHeight; ++y)
    {
        for (std::uint32_t x = 0; x < kWidth; ++x)
        {
            const std::size_t index = static_cast<std::size_t>(y) * kWidth + x;
            const std::uint32_t expected = Formula(x, y);
            matchesSerial = matchesSerial && pixels[index] == expected;
            visitedOnce = visitedOnce && visits[index] == 1;
            serialChecksum += expected;
        }
    }
    Expect(filled && matchesSerial && tiledChecksum == serialChecksum, "executor.irregular-fill-matches-serial");
    Expect(visitedOnce, "executor.irregular-fill-visits-each-pixel-once");

    return gFailures;
}
