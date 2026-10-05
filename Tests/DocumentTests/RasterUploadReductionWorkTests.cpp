#include "RasterUploadReductionWorkTests.h"

#include "../../Common/Document.h"
#include "../../Common/RasterUploadReductionWork.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <span>
#include <stop_token>

namespace
{
int gFailures = 0;

void Expect(bool condition, const char* name) noexcept
{
    std::printf("%s raster-upload-reduction.%s\n", condition ? "PASS" : "FAIL", name);
    gFailures += condition ? 0 : 1;
}

struct ObserverState final
{
    std::stop_source* localStop = nullptr;
    std::size_t calls = 0;
    std::size_t cancelAfter = 0;
    std::array<std::size_t, 8> completedRows{};
};

void ObserveRows(void* context, std::size_t completedRows) noexcept
{
    auto& state = *static_cast<ObserverState*>(context);
    if (state.calls < state.completedRows.size())
    {
        state.completedRows[state.calls] = completedRows;
    }
    ++state.calls;
    if (state.localStop != nullptr && completedRows == state.cancelAfter)
    {
        (void)state.localStop->request_stop();
    }
}

// Independent serial oracle over caller-supplied literal pixels. It deliberately does not call Raster or the
// production reducer; output windows address the crop-anchored grid and edge blocks use their clipped count.
template <std::size_t SourceBytes, std::size_t OutputBytes>
bool ReduceLiteralSerialOracle(const std::array<std::uint8_t, SourceBytes>& source, int sourceWidth, int channels,
                               RasterUploadBlockRegion region, std::array<std::uint8_t, OutputBytes>& output) noexcept
{
    if (region.outputWidth * channels * region.outputHeight != static_cast<int>(OutputBytes))
    {
        return false;
    }
    for (int outputY = 0; outputY < region.outputHeight; ++outputY)
    {
        for (int outputX = 0; outputX < region.outputWidth; ++outputX)
        {
            std::array<std::uint64_t, 4> sums{};
            std::uint64_t count = 0;
            const int left = region.sourceX + (region.outputX + outputX) * region.scale;
            const int top = region.sourceY + (region.outputY + outputY) * region.scale;
            for (int y = top; y < top + region.scale && y < region.sourceY + region.sourceHeight; ++y)
            {
                for (int x = left; x < left + region.scale && x < region.sourceX + region.sourceWidth; ++x)
                {
                    const std::size_t pixel =
                        static_cast<std::size_t>(y * sourceWidth + x) * static_cast<std::size_t>(channels);
                    for (int channel = 0; channel < channels; ++channel)
                    {
                        sums[static_cast<std::size_t>(channel)] += source[pixel + static_cast<std::size_t>(channel)];
                    }
                    ++count;
                }
            }
            if (count == 0)
            {
                return false;
            }
            const std::size_t outputPixel =
                static_cast<std::size_t>(outputY * region.outputWidth + outputX) * static_cast<std::size_t>(channels);
            for (int channel = 0; channel < channels; ++channel)
            {
                output[outputPixel + static_cast<std::size_t>(channel)] =
                    static_cast<std::uint8_t>((sums[static_cast<std::size_t>(channel)] + count / 2U) / count);
            }
        }
    }
    return true;
}

constexpr std::array<std::uint8_t, 80> kBgraSource{
    1,  10,  100, 255, 3,  20,  110, 0, 5,  30,  120, 128, 7,  40,  130, 255, 9,  50,  140, 0,
    11, 60,  150, 255, 13, 70,  160, 0, 15, 80,  170, 128, 17, 90,  180, 255, 19, 100, 190, 0,
    21, 110, 200, 255, 23, 120, 210, 0, 25, 130, 220, 128, 27, 140, 230, 255, 29, 150, 240, 0,
    31, 160, 250, 255, 33, 170, 240, 0, 35, 180, 230, 128, 37, 190, 220, 255, 39, 200, 210, 0};

void CropGridBgraAndChunkWindows()
{
    constexpr RasterUploadBlockRegion full{1, 1, 3, 3, 2, 0, 0, 2, 2};
    constexpr std::array<std::uint8_t, 16> expected{19, 100, 190, 64, 22, 115, 205, 255,
                                                    34, 175, 235, 64, 37, 190, 220, 255};
    std::array<std::uint8_t, 16> oracle{};
    Expect(ReduceLiteralSerialOracle(kBgraSource, 5, 4, full, oracle) && oracle == expected,
           "bgra-literal-serial-oracle-check");
    constexpr RasterUploadBlockRegion bottomRightWindow{1, 1, 3, 3, 2, 1, 1, 1, 1};
    constexpr std::array<std::uint8_t, 4> bottomRightExpected{37, 190, 220, 255};
    std::array<std::uint8_t, 4> oneWindowOracle{};
    Expect(ReduceLiteralSerialOracle(kBgraSource, 5, 4, bottomRightWindow, oneWindowOracle) &&
               oneWindowOracle == bottomRightExpected,
           "nonzero-output-window-independent-literal");

    const Raster raster = Raster::Make(5, 4, RasterFormat::Bgra8888, kBgraSource.data(), kBgraSource.size());
    std::array<std::uint8_t, 20> destination{};
    destination.fill(0xE7);
    std::array<std::uint8_t, 12> sourceRow{}; // crop-clipped source span: 3 pixels × 4 channels
    std::array<std::uint64_t, 8> sums{};
    const auto status = ReduceRasterBlocksForUpload(raster, full, destination.data() + 2, 18, 10, sourceRow, sums);
    Expect(status == RasterUploadWorkStatus::Ready, "bgra-odd-crop-ready");
    constexpr std::array<std::uint8_t, 20> pitchedExpected{0xE7, 0xE7, 19, 100, 190, 64, 22, 115, 205, 255,
                                                           0xE7, 0xE7, 34, 175, 235, 64, 37, 190, 220, 255};
    Expect(destination == pitchedExpected, "bgra-rounding-alpha-crop-edges-pitch-and-guards");

    std::array<std::uint8_t, 4> bottomRightActual{};
    std::array<std::uint8_t, 4> edgeSourceRow{};
    std::array<std::uint64_t, 4> edgeSums{};
    const auto edgeStatus = ReduceRasterBlocksForUpload(raster, bottomRightWindow, bottomRightActual.data(),
                                                        bottomRightActual.size(), 4, edgeSourceRow, edgeSums);
    Expect(edgeStatus == RasterUploadWorkStatus::Ready && bottomRightActual == bottomRightExpected,
           "nonzero-output-window-matches-independent-literal");

    std::array<std::uint8_t, 16> chunked{};
    chunked.fill(0xA1);
    std::array<std::uint8_t, 8> oneBlockRow{};
    std::array<std::uint64_t, 4> oneBlockSums{};
    const RasterUploadBlockRegion upperLeft{1, 1, 3, 3, 2, 0, 0, 1, 1};
    const RasterUploadBlockRegion upperRight{1, 1, 3, 3, 2, 1, 0, 1, 1};
    const RasterUploadBlockRegion lowerRow{1, 1, 3, 3, 2, 0, 1, 2, 1};
    const auto left = ReduceRasterBlocksForUpload(raster, upperLeft, chunked.data(), 4, 4, oneBlockRow, oneBlockSums);
    const auto right =
        ReduceRasterBlocksForUpload(raster, upperRight, chunked.data() + 4, 4, 4, oneBlockRow, oneBlockSums);
    const auto bottom = ReduceRasterBlocksForUpload(raster, lowerRow, chunked.data() + 8, 8, 8, sourceRow, sums);
    Expect(left == RasterUploadWorkStatus::Ready && right == RasterUploadWorkStatus::Ready &&
               bottom == RasterUploadWorkStatus::Ready,
           "crop-grid-chunk-windows-ready");
    Expect(chunked == expected, "crop-grid-chunks-match-independent-complete-literal");
}

void GrayAndScaleOne()
{
    constexpr std::array<std::uint8_t, 15> gray{0, 10, 20, 30, 40, 50, 60, 70, 80, 90, 100, 110, 120, 130, 140};
    constexpr RasterUploadBlockRegion crop{1, 0, 3, 3, 2, 0, 0, 2, 2};
    constexpr std::array<std::uint8_t, 4> grayExpected{40, 55, 115, 130};
    std::array<std::uint8_t, 4> grayOracle{};
    Expect(ReduceLiteralSerialOracle(gray, 5, 1, crop, grayOracle) && grayOracle == grayExpected,
           "gray-literal-serial-oracle-check");
    const Raster grayRaster = Raster::Make(5, 3, RasterFormat::Gray8, gray.data(), gray.size());
    std::array<std::uint8_t, 4> grayActual{};
    std::array<std::uint8_t, 6> grayRow{};
    std::array<std::uint64_t, 2> graySums{};
    const auto grayStatus =
        ReduceRasterBlocksForUpload(grayRaster, crop, grayActual.data(), grayActual.size(), 2, grayRow, graySums);
    Expect(grayStatus == RasterUploadWorkStatus::Ready && grayActual == grayExpected, "gray-odd-crop-edge-blocks");

    constexpr std::array<std::uint8_t, 12> bgra{3, 9, 17, 0, 21, 31, 41, 51, 61, 71, 81, 91};
    constexpr std::array<std::uint8_t, 8> copied{21, 31, 41, 51, 61, 71, 81, 91};
    const Raster bgraRaster = Raster::Make(3, 1, RasterFormat::Bgra8888, bgra.data(), bgra.size());
    std::array<std::uint8_t, 8> actual{};
    const RasterUploadBlockRegion scaleOne{1, 0, 2, 1, 1, 0, 0, 2, 1};
    const auto copyStatus = ReduceRasterBlocksForUpload(bgraRaster, scaleOne, actual.data(), actual.size(), 8, {}, {});
    Expect(copyStatus == RasterUploadWorkStatus::Ready && actual == copied, "scale-one-direct-copy-needs-no-scratch");
}

void SingleBlockAndSparseSampling()
{
    constexpr std::array<std::uint8_t, 4> onePixel{17, 87, 149, 233};
    const Raster one = Raster::Make(1, 1, RasterFormat::Bgra8888, onePixel.data(), onePixel.size());
    std::array<std::uint8_t, 4> actual{};
    std::array<std::uint8_t, 4> row{};
    std::array<std::uint64_t, 4> sums{};
    const RasterUploadBlockRegion largerScale{0, 0, 1, 1, 11, 0, 0, 1, 1};
    const auto oneStatus = ReduceRasterBlocksForUpload(one, largerScale, actual.data(), actual.size(), 4, row, sums);
    Expect(oneStatus == RasterUploadWorkStatus::Ready && actual == onePixel,
           "scale-larger-than-crop-uses-one-clipped-source-pixel");
    const RasterUploadBlockRegion maximumScale{0, 0, 1, 1, std::numeric_limits<int>::max(), 0, 0, 1, 1};
    actual.fill(0);
    const auto maximumScaleStatus =
        ReduceRasterBlocksForUpload(one, maximumScale, actual.data(), actual.size(), 4, row, sums);
    Expect(maximumScaleStatus == RasterUploadWorkStatus::Ready && actual == onePixel,
           "int-max-scale-on-valid-one-pixel-crop");

    constexpr std::array<std::uint8_t, 4> baseBytes{10, 20, 30, 40};
    constexpr std::array<std::uint8_t, 2> tileBytes{50, 60};
    const Raster base = Raster::Make(2, 2, RasterFormat::Gray8, baseBytes.data(), baseBytes.size());
    const Raster tile = Raster::Make(1, 2, RasterFormat::Gray8, tileBytes.data(), tileBytes.size());
    const Raster sparse = Raster::MakeSparse(5, 3, RasterFormat::Gray8, base, 1, 0, {RasterTile{3, 1, tile}}, 90);
    constexpr RasterUploadBlockRegion sparseRegion{1, 0, 3, 3, 2, 0, 0, 2, 2};
    constexpr std::array<std::uint8_t, 4> sparseExpected{25, 70, 90, 60};
    std::array<std::uint8_t, 4> sparseActual{};
    std::array<std::uint8_t, 3> sparseRow{};
    std::array<std::uint64_t, 2> sparseSums{};
    const std::uint64_t epoch = Raster::ResidencyEpoch();
    const auto sparseStatus = ReduceRasterBlocksForUpload(sparse, sparseRegion, sparseActual.data(),
                                                          sparseActual.size(), 2, sparseRow, sparseSums);
    Expect(sparseStatus == RasterUploadWorkStatus::Ready && sparseActual == sparseExpected,
           "sparse-base-tile-fill-reduced-literal");
    Expect(sparse.IsSparse() && !sparse.IsMaterialized() && Raster::ResidencyEpoch() == epoch,
           "sparse-read-preserves-residency-and-epoch");
}

void ScratchCapacityAndInvalidInputs()
{
    constexpr std::array<std::uint8_t, 9> pixels{1, 2, 3, 4, 5, 6, 7, 8, 9};
    const Raster raster = Raster::Make(3, 3, RasterFormat::Gray8, pixels.data(), pixels.size());
    constexpr RasterUploadBlockRegion region{0, 0, 3, 3, 2, 0, 0, 2, 2};
    constexpr std::array<std::uint8_t, 4> expected{3, 5, 8, 9};
    std::array<std::uint8_t, 16> destination{};
    destination.fill(0xBC);
    std::array<std::uint8_t, 3> exactRow{};
    std::array<std::uint64_t, 2> exactSums{};
    const auto exact = ReduceRasterBlocksForUpload(raster, region, destination.data(), 5, 3, exactRow, exactSums);
    Expect(exact == RasterUploadWorkStatus::Ready && destination[0] == expected[0] && destination[1] == expected[1] &&
               destination[3] == expected[2] && destination[4] == expected[3] && destination[2] == 0xBC &&
               destination[5] == 0xBC,
           "exact-required-capacity-scratch-and-pitched-padding");

    ObserverState observer{};
    const auto invalid = [&](RasterUploadBlockRegion invalidRegion, std::uint8_t* output, std::size_t outputCapacity,
                             std::size_t pitch, std::span<std::uint8_t> rowScratch,
                             std::span<std::uint64_t> channelSums, const char* label) noexcept
    {
        constexpr std::array<std::uint8_t, 3> rowSentinels{0xA5, 0xA5, 0xA5};
        constexpr std::array<std::uint64_t, 2> sumSentinels{0x12345678U, 0x87654321U};
        destination.fill(0xBC);
        exactRow = rowSentinels;
        exactSums = sumSentinels;
        observer.calls = 0;
        const auto status = ReduceRasterBlocksForUpload(raster, invalidRegion, output, outputCapacity, pitch,
                                                        rowScratch, channelSums, {}, ObserveRows, &observer);
        const bool outputUntouched = std::all_of(destination.begin(), destination.end(),
                                                 [](std::uint8_t byte) noexcept { return byte == 0xBC; });
        Expect(status == RasterUploadWorkStatus::Invalid && outputUntouched && exactRow == rowSentinels &&
                   exactSums == sumSentinels && observer.calls == 0,
               label);
    };
    invalid(region, destination.data(), 4, 3, exactRow, exactSums,
            "short-output-capacity-invalid-before-write-or-observe");
    invalid(region, destination.data(), destination.size(), 3, std::span<std::uint8_t>{exactRow.data(), 2}, exactSums,
            "short-source-span-row-invalid-before-write");
    invalid(region, destination.data(), destination.size(), 3, exactRow, std::span<std::uint64_t>{exactSums.data(), 1},
            "short-channel-sums-invalid-before-write");
    invalid(region, destination.data(), destination.size(), 1, exactRow, exactSums,
            "short-output-pitch-invalid-before-write");
    invalid({0, 0, 3, 3, 0, 0, 0, 2, 2}, destination.data(), destination.size(), 3, exactRow, exactSums,
            "zero-scale-invalid-before-write");
    invalid({0, 0, 3, 3, -1, 0, 0, 2, 2}, destination.data(), destination.size(), 3, exactRow, exactSums,
            "negative-scale-invalid-before-write");
    invalid({-1, 0, 1, 1, 1, 0, 0, 1, 1}, destination.data(), destination.size(), 1, exactRow, exactSums,
            "negative-crop-x-invalid-before-write");
    invalid({0, -1, 1, 1, 1, 0, 0, 1, 1}, destination.data(), destination.size(), 1, exactRow, exactSums,
            "negative-crop-y-invalid-before-write");
    invalid({0, 0, 1, 1, 1, -1, 0, 1, 1}, destination.data(), destination.size(), 1, exactRow, exactSums,
            "negative-output-x-invalid-before-write");
    invalid({0, 0, 1, 1, 1, 0, -1, 1, 1}, destination.data(), destination.size(), 1, exactRow, exactSums,
            "negative-output-y-invalid-before-write");
    invalid({2, 0, 2, 1, 1, 0, 0, 2, 1}, destination.data(), destination.size(), 2, exactRow, exactSums,
            "crop-width-outside-source-invalid-before-write");
    invalid({0, 0, 3, 4, 1, 0, 0, 3, 4}, destination.data(), destination.size(), 3, exactRow, exactSums,
            "crop-height-outside-source-invalid-before-write");
    invalid({3, 0, 1, 1, 1, 0, 0, 1, 1}, destination.data(), destination.size(), 1, exactRow, exactSums,
            "crop-origin-at-right-edge-invalid-before-write");
    invalid({0, 3, 1, 1, 1, 0, 0, 1, 1}, destination.data(), destination.size(), 1, exactRow, exactSums,
            "crop-origin-at-bottom-edge-invalid-before-write");
    invalid({0, 0, 3, 3, 2, 0, 1, 2, 2}, destination.data(), destination.size(), 2, exactRow, exactSums,
            "output-window-height-outside-grid-invalid-before-write");
    invalid({0, 0, 3, 3, 2, 1, 0, 2, 1}, destination.data(), destination.size(), 2, exactRow, exactSums,
            "output-window-width-outside-grid-invalid-before-write");
    invalid({0, 0, 3, 3, 2, 2, 0, 1, 1}, destination.data(), destination.size(), 1, exactRow, exactSums,
            "output-window-origin-at-grid-end-invalid-before-write");
    invalid({0, 0, 1, 1, 1, 0, 0, 1, 1}, nullptr, destination.size(), 1, exactRow, exactSums,
            "null-output-invalid-before-write");
    invalid({0, 0, 1, 1, 1, 0, 0, 1, 1}, destination.data(), destination.size(),
            static_cast<std::size_t>(std::numeric_limits<int>::max()) + 1U, exactRow, exactSums,
            "pitch-over-int-max-invalid-before-write");
    const RasterUploadBlockRegion extreme{0, 0, std::numeric_limits<int>::max(), 1, 1, 0, 0, 1, 1};
    invalid(extreme, destination.data(), destination.size(), 1, exactRow, exactSums,
            "extreme-crop-out-of-bounds-invalid-before-write");
    const Raster empty;
    destination.fill(0xBC);
    exactRow.fill(0xA5);
    exactSums.fill(0x12345678U);
    observer.calls = 0;
    const auto emptyStatus = ReduceRasterBlocksForUpload(empty, region, destination.data(), destination.size(), 3,
                                                         exactRow, exactSums, {}, ObserveRows, &observer);
    const bool emptyOutputUntouched =
        std::all_of(destination.begin(), destination.end(), [](std::uint8_t byte) noexcept { return byte == 0xBC; });
    Expect(emptyStatus == RasterUploadWorkStatus::Invalid && emptyOutputUntouched &&
               exactRow == std::array<std::uint8_t, 3>{0xA5, 0xA5, 0xA5} &&
               exactSums == std::array<std::uint64_t, 2>{0x12345678U, 0x12345678U} && observer.calls == 0,
           "empty-source-invalid-before-write");
}

void CancellationAndRowObserver()
{
    constexpr std::array<std::uint8_t, 9> pixels{1, 2, 3, 4, 5, 6, 7, 8, 9};
    const Raster raster = Raster::Make(3, 3, RasterFormat::Gray8, pixels.data(), pixels.size());
    constexpr RasterUploadBlockRegion region{0, 0, 3, 3, 2, 0, 0, 2, 2};
    std::array<std::uint8_t, 4> destination{};
    std::array<std::uint8_t, 3> sourceRow{};
    std::array<std::uint64_t, 2> channelSums{};
    destination.fill(0xD3);
    sourceRow.fill(0xA5);
    channelSums.fill(0x12345678U);
    std::stop_source preStop;
    (void)preStop.request_stop();
    ObserverState preObserver{};
    const auto preStatus =
        ReduceRasterBlocksForUpload(raster, region, destination.data(), destination.size(), 2, sourceRow, channelSums,
                                    CancelToken{preStop.get_token()}, ObserveRows, &preObserver);
    Expect(preStatus == RasterUploadWorkStatus::Cancelled && preObserver.calls == 0 &&
               destination == std::array<std::uint8_t, 4>{0xD3, 0xD3, 0xD3, 0xD3} &&
               sourceRow == std::array<std::uint8_t, 3>{0xA5, 0xA5, 0xA5} &&
               channelSums == std::array<std::uint64_t, 2>{0x12345678U, 0x12345678U},
           "precancel-no-write-no-observer");

    std::stop_source localStop;
    ObserverState middle{&localStop, 0, 1};
    destination.fill(0xD3);
    const auto middleStatus =
        ReduceRasterBlocksForUpload(raster, region, destination.data(), destination.size(), 2, sourceRow, channelSums,
                                    CancelToken{localStop.get_token()}, ObserveRows, &middle);
    Expect(middleStatus == RasterUploadWorkStatus::Cancelled && middle.calls == 1 && middle.completedRows[0] == 1 &&
               destination == std::array<std::uint8_t, 4>{3, 5, 0xD3, 0xD3},
           "local-stop-after-output-row-publishes-only-completed-prefix");

    std::stop_source shutdownStop;
    (void)shutdownStop.request_stop();
    std::stop_source activeLocal;
    destination.fill(0xD3);
    sourceRow.fill(0xA5);
    channelSums.fill(0x12345678U);
    const auto shutdownStatus = ReduceRasterBlocksForUpload(
        raster, region, destination.data(), destination.size(), 2, sourceRow, channelSums,
        CancelToken{activeLocal.get_token(), shutdownStop.get_token()}, ObserveRows, &preObserver);
    Expect(shutdownStatus == RasterUploadWorkStatus::Cancelled && preObserver.calls == 0 &&
               destination == std::array<std::uint8_t, 4>{0xD3, 0xD3, 0xD3, 0xD3} &&
               sourceRow == std::array<std::uint8_t, 3>{0xA5, 0xA5, 0xA5} &&
               channelSums == std::array<std::uint64_t, 2>{0x12345678U, 0x12345678U},
           "precancelled-worker-shutdown-token-no-write");

    ObserverState everyRow{};
    const auto ready = ReduceRasterBlocksForUpload(raster, region, destination.data(), destination.size(), 2, sourceRow,
                                                   channelSums, {}, ObserveRows, &everyRow);
    Expect(ready == RasterUploadWorkStatus::Ready && everyRow.calls == 2 && everyRow.completedRows[0] == 1 &&
               everyRow.completedRows[1] == 2 && destination == std::array<std::uint8_t, 4>{3, 5, 8, 9},
           "observer-reports-each-completed-output-row-through-last");

    std::stop_source lastStop;
    ObserverState last{&lastStop, 0, 2};
    const auto lastStatus =
        ReduceRasterBlocksForUpload(raster, region, destination.data(), destination.size(), 2, sourceRow, channelSums,
                                    CancelToken{lastStop.get_token()}, ObserveRows, &last);
    Expect(lastStatus == RasterUploadWorkStatus::Cancelled && last.calls == 2 && last.completedRows[1] == 2 &&
               destination == std::array<std::uint8_t, 4>{3, 5, 8, 9},
           "cancel-from-last-row-observer-suppresses-ready");
}
} // namespace

int RunRasterUploadReductionWorkTests()
{
    gFailures = 0;
    CropGridBgraAndChunkWindows();
    GrayAndScaleOne();
    SingleBlockAndSparseSampling();
    ScratchCapacityAndInvalidInputs();
    CancellationAndRowObserver();
    return gFailures;
}
