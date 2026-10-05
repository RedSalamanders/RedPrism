#include "RasterUploadWorkTests.h"

#include "../../Common/Document.h"
#include "../../Common/RasterUploadWork.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <stop_token>

namespace
{
int gFailures = 0;

void Expect(bool condition, const char* name) noexcept
{
    std::printf("%s raster-upload.%s\n", condition ? "PASS" : "FAIL", name);
    gFailures += condition ? 0 : 1;
}

struct ObserverState final
{
    std::stop_source* stop = nullptr;
    std::size_t calls = 0;
    std::size_t cancelAfter = 0;
    std::array<std::size_t, 8> observedRows{};
};

void ObserveRows(void* context, std::size_t completedRows) noexcept
{
    auto& state = *static_cast<ObserverState*>(context);
    if (state.calls < state.observedRows.size())
    {
        state.observedRows[state.calls] = completedRows;
    }
    ++state.calls;
    if (state.stop != nullptr && completedRows == state.cancelAfter)
    {
        (void)state.stop->request_stop();
    }
}

void DenseBgraAndPitchedGuards()
{
    constexpr std::array<std::uint8_t, 24> pixels{1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12,
                                                  13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24};
    constexpr std::array<std::uint8_t, 20> expected{5,  6,  7,  8,  9,  10, 11, 12, 0xCC, 0xCC,
                                                    17, 18, 19, 20, 21, 22, 23, 24, 0xCC, 0xCC};
    const Raster raster = Raster::Make(3, 2, RasterFormat::Bgra8888, pixels.data(), pixels.size());
    std::array<std::uint8_t, 24> destination{};
    destination.fill(0xE1);
    const auto status = CopyRasterRowsForUpload(raster, 1, 0, 2, 2, destination.data() + 2, 20, 10);
    Expect(status == RasterUploadWorkStatus::Ready, "dense-bgra.ready");
    Expect(destination[0] == 0xE1 && destination[1] == 0xE1 && destination[22] == 0xE1 && destination[23] == 0xE1,
           "dense-bgra.external-guards-untouched");
    bool rowsMatch = true;
    for (std::size_t index = 0; index < expected.size(); ++index)
    {
        const std::uint8_t expectedByte = index % 10 < 8 ? expected[index] : 0xE1;
        rowsMatch = rowsMatch && destination[index + 2] == expectedByte;
    }
    Expect(rowsMatch, "dense-bgra-subset-pitch-padding");
}

void DenseGrayRows()
{
    constexpr std::array<std::uint8_t, 12> pixels{11, 22, 33, 44, 55, 66, 77, 88, 99, 111, 122, 133};
    constexpr std::array<std::uint8_t, 8> expected{55, 66, 0xD4, 0xD4, 99, 111, 0xD4, 0xD4};
    const Raster raster = Raster::Make(4, 3, RasterFormat::Gray8, pixels.data(), pixels.size());
    std::array<std::uint8_t, 8> destination{};
    destination.fill(0xD4);
    const auto status = CopyRasterRowsForUpload(raster, 0, 1, 2, 2, destination.data(), destination.size(), 4);
    Expect(status == RasterUploadWorkStatus::Ready, "dense-gray.ready");
    Expect(destination == expected, "dense-gray-subset-pitch-padding");
}

void SparseBaseTileAndFill()
{
    constexpr std::array<std::uint8_t, 6> basePixels{10, 20, 30, 40, 50, 60};
    constexpr std::array<std::uint8_t, 4> tilePixels{70, 80, 90, 100};
    const Raster base = Raster::Make(3, 2, RasterFormat::Gray8, basePixels.data(), basePixels.size());
    const Raster tile = Raster::Make(2, 2, RasterFormat::Gray8, tilePixels.data(), tilePixels.size());
    const Raster sparse = Raster::MakeSparse(6, 4, RasterFormat::Gray8, base, 1, 0, {RasterTile{3, 1, tile}}, 0xA7);
    constexpr std::array<std::uint8_t, 8> expected{40, 50, 70, 80, 0xA7, 0xA7, 90, 100};
    std::array<std::uint8_t, 10> destination{};
    destination.fill(0xF2);
    const std::uint64_t epoch = Raster::ResidencyEpoch();
    const auto status = CopyRasterRowsForUpload(sparse, 1, 1, 4, 2, destination.data() + 1, 8, 4);
    Expect(status == RasterUploadWorkStatus::Ready, "sparse.ready");
    Expect(destination[0] == 0xF2 && destination[9] == 0xF2, "sparse.external-guards-untouched");
    bool contentsMatch = true;
    for (std::size_t index = 0; index < expected.size(); ++index)
    {
        contentsMatch = contentsMatch && destination[index + 1] == expected[index];
    }
    Expect(contentsMatch, "sparse-base-tile-fill-values");
    Expect(!sparse.IsMaterialized() && Raster::ResidencyEpoch() == epoch, "sparse-source-residency-unchanged");
}

void InvalidInputsAreUntouched()
{
    constexpr std::array<std::uint8_t, 4> pixels{1, 2, 3, 4};
    const Raster raster = Raster::Make(2, 2, RasterFormat::Gray8, pixels.data(), pixels.size());
    std::array<std::uint8_t, 32> destination{};
    destination.fill(0xB9);
    const auto unchanged = [&]() noexcept
    {
        for (const std::uint8_t byte : destination)
        {
            if (byte != 0xB9)
            {
                return false;
            }
        }
        return true;
    };

    Expect(CopyRasterRowsForUpload(raster, -1, 0, 1, 1, destination.data(), destination.size(), 1) ==
               RasterUploadWorkStatus::Invalid,
           "invalid-negative-origin");
    Expect(unchanged(), "invalid-negative-origin-no-write");
    Expect(CopyRasterRowsForUpload(raster, 1, 1, 2, 1, destination.data(), destination.size(), 2) ==
               RasterUploadWorkStatus::Invalid,
           "invalid-out-of-bounds-rectangle");
    Expect(unchanged(), "invalid-bounds-no-write");
    Expect(CopyRasterRowsForUpload(raster, 0, 0, 2, 1, destination.data(), destination.size(), 1) ==
               RasterUploadWorkStatus::Invalid,
           "invalid-short-pitch");
    Expect(unchanged(), "invalid-short-pitch-no-write");
    Expect(CopyRasterRowsForUpload(raster, 0, 0, 1, 2, destination.data(), 1, 1) == RasterUploadWorkStatus::Invalid,
           "invalid-short-capacity");
    Expect(unchanged(), "invalid-short-capacity-no-write");
    Expect(CopyRasterRowsForUpload(raster, 0, 0, 1, 1, destination.data(), destination.size(),
                                   static_cast<std::size_t>(std::numeric_limits<int>::max()) + 1U) ==
               RasterUploadWorkStatus::Invalid,
           "invalid-readregion-stride-narrowing");
    Expect(unchanged(), "invalid-stride-no-write");
    Expect(CopyRasterRowsForUpload(raster, 0, 0, 1, 1, nullptr, destination.size(), 1) ==
               RasterUploadWorkStatus::Invalid,
           "invalid-null-destination");
    Expect(unchanged(), "invalid-null-no-write");
}

void InvalidGeometryBoundaries()
{
    constexpr std::array<std::uint8_t, 4> pixels{1, 2, 3, 4};
    const Raster raster = Raster::Make(2, 2, RasterFormat::Gray8, pixels.data(), pixels.size());
    std::array<std::uint8_t, 8> destination{};
    destination.fill(0xA6);
    ObserverState observer{};
    const auto invalid = [&](int sourceX, int sourceY, int width, int height, const char* label) noexcept
    {
        observer.calls = 0;
        const auto status = CopyRasterRowsForUpload(raster, sourceX, sourceY, width, height, destination.data(),
                                                    destination.size(), 2, {}, ObserveRows, &observer);
        Expect(status == RasterUploadWorkStatus::Invalid, label);
        Expect(observer.calls == 0, "invalid-geometry-observer-not-called");
        Expect(destination == std::array<std::uint8_t, 8>{0xA6, 0xA6, 0xA6, 0xA6, 0xA6, 0xA6, 0xA6, 0xA6},
               "invalid-geometry-destination-untouched");
    };

    const Raster empty;
    Expect(CopyRasterRowsForUpload(empty, 0, 0, 1, 1, destination.data(), destination.size(), 1) ==
               RasterUploadWorkStatus::Invalid,
           "invalid-default-raster");
    invalid(0, 0, 0, 1, "invalid-zero-width");
    invalid(0, 0, -1, 1, "invalid-negative-width");
    invalid(0, 0, 1, 0, "invalid-zero-height");
    invalid(0, 0, 1, -1, "invalid-negative-height");
    invalid(3, 0, 1, 1, "invalid-origin-beyond-right-edge-positive-width");
    invalid(2, 0, 1, 1, "invalid-origin-at-right-edge-positive-width");
    invalid(0, 3, 1, 1, "invalid-origin-beyond-bottom-edge-positive-height");
    invalid(0, 2, 1, 1, "invalid-origin-at-bottom-edge-positive-height");
    invalid(std::numeric_limits<int>::max(), 0, 1, 1, "invalid-int-max-origin");
    invalid(0, 0, std::numeric_limits<int>::max(), 1, "invalid-int-max-width");
}

void CapacityBoundaries()
{
    constexpr std::array<std::uint8_t, 4> pixels{17, 29, 41, 53};
    const Raster raster = Raster::Make(2, 2, RasterFormat::Gray8, pixels.data(), pixels.size());
    std::array<std::uint8_t, 6> destination{0xB4, 0xB4, 0xB4, 0xB4, 0xB4, 0xB4};
    const auto exact = CopyRasterRowsForUpload(raster, 0, 0, 2, 2, destination.data(), destination.size(), 4);
    Expect(exact == RasterUploadWorkStatus::Ready, "capacity-exact-required-with-pitched-rows");
    Expect(destination == std::array<std::uint8_t, 6>{17, 29, 0xB4, 0xB4, 41, 53},
           "capacity-exact-required-preserves-final-row-padding");

    destination.fill(0xB4);
    ObserverState observer{};
    const auto shortByOne =
        CopyRasterRowsForUpload(raster, 0, 0, 2, 2, destination.data(), 5, 4, {}, ObserveRows, &observer);
    Expect(shortByOne == RasterUploadWorkStatus::Invalid, "capacity-short-by-one-invalid");
    Expect(destination == std::array<std::uint8_t, 6>{0xB4, 0xB4, 0xB4, 0xB4, 0xB4, 0xB4},
           "capacity-short-by-one-untouched");
    Expect(observer.calls == 0, "capacity-short-by-one-observer-not-called");
}

void CancellationBoundaries()
{
    constexpr std::array<std::uint8_t, 6> pixels{1, 2, 3, 4, 5, 6};
    const Raster raster = Raster::Make(2, 3, RasterFormat::Gray8, pixels.data(), pixels.size());
    std::array<std::uint8_t, 6> destination{};
    destination.fill(0xC3);
    std::stop_source preStop;
    (void)preStop.request_stop();
    const auto preCancelled = CopyRasterRowsForUpload(raster, 0, 0, 2, 3, destination.data(), destination.size(), 2,
                                                      CancelToken{preStop.get_token()});
    Expect(preCancelled == RasterUploadWorkStatus::Cancelled, "cancel-before-first-row");
    ObserverState preCancelObserver{};
    const auto preCancelledWithObserver =
        CopyRasterRowsForUpload(raster, 0, 0, 2, 3, destination.data(), destination.size(), 2,
                                CancelToken{preStop.get_token()}, ObserveRows, &preCancelObserver);
    Expect(preCancelledWithObserver == RasterUploadWorkStatus::Cancelled, "cancel-before-first-row-observed-status");
    Expect(preCancelObserver.calls == 0, "cancel-before-first-row-observer-not-called");
    Expect(destination == std::array<std::uint8_t, 6>{0xC3, 0xC3, 0xC3, 0xC3, 0xC3, 0xC3},
           "cancel-before-first-row-no-write");

    std::stop_source midStop;
    ObserverState observer{&midStop, 0, 2};
    destination.fill(0xC3);
    const auto midCancelled = CopyRasterRowsForUpload(raster, 0, 0, 2, 3, destination.data(), destination.size(), 2,
                                                      CancelToken{midStop.get_token()}, ObserveRows, &observer);
    Expect(midCancelled == RasterUploadWorkStatus::Cancelled, "cancel-between-rows");
    Expect(observer.calls == 2, "cancel-observer-called-after-completed-rows");
    Expect(destination[0] == 1 && destination[1] == 2 && destination[2] == 3 && destination[3] == 4 &&
               destination[4] == 0xC3 && destination[5] == 0xC3,
           "cancel-only-completed-prefix-written");

    ObserverState countObserver{};
    const auto ready = CopyRasterRowsForUpload(raster, 0, 0, 2, 3, destination.data(), destination.size(), 2, {},
                                               ObserveRows, &countObserver);
    Expect(ready == RasterUploadWorkStatus::Ready && countObserver.calls == 3,
           "observer-called-once-per-completed-row");
    Expect(countObserver.observedRows[0] == 1 && countObserver.observedRows[1] == 2 &&
               countObserver.observedRows[2] == 3,
           "observer-sequence-exactly-one-through-height");

    std::stop_source afterFinalRowStop;
    ObserverState afterFinalRowObserver{&afterFinalRowStop, 0, 3};
    destination.fill(0xC3);
    const auto cancelledAfterFinalRow =
        CopyRasterRowsForUpload(raster, 0, 0, 2, 3, destination.data(), destination.size(), 2,
                                CancelToken{afterFinalRowStop.get_token()}, ObserveRows, &afterFinalRowObserver);
    Expect(cancelledAfterFinalRow == RasterUploadWorkStatus::Cancelled, "cancel-after-final-row-still-cancelled");
    Expect(afterFinalRowObserver.calls == 3 && afterFinalRowObserver.observedRows[0] == 1 &&
               afterFinalRowObserver.observedRows[1] == 2 && afterFinalRowObserver.observedRows[2] == 3,
           "cancel-after-final-row-observed-all-rows");
}

void SparseBgraOffsetsPitchAndGuards()
{
    constexpr std::array<std::uint8_t, 16> basePixels{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
    constexpr std::array<std::uint8_t, 8> tilePixels{21, 22, 23, 24, 25, 26, 27, 28};
    const Raster base = Raster::Make(2, 2, RasterFormat::Bgra8888, basePixels.data(), basePixels.size());
    const Raster tile = Raster::Make(1, 2, RasterFormat::Bgra8888, tilePixels.data(), tilePixels.size());
    const Raster sparse = Raster::MakeSparse(5, 3, RasterFormat::Bgra8888, base, 1, 0, {RasterTile{3, 1, tile}}, 0xA7);
    constexpr std::array<std::uint8_t, 56> expected{
        // Source row 0 and its full pitch.
        1, 2, 3, 4, 5, 6, 7, 8, 0xA7, 0xA7, 0xA7, 0xA7, 0xA7, 0xA7, 0xA7, 0xA7, 0xD9, 0xD9, 0xD9, 0xD9,
        // Source row 1 and its full pitch.
        9, 10, 11, 12, 13, 14, 15, 16, 21, 22, 23, 24, 0xA7, 0xA7, 0xA7, 0xA7, 0xD9, 0xD9, 0xD9, 0xD9,
        // Source row 2 needs no trailing pitch padding in the required capacity.
        0xA7, 0xA7, 0xA7, 0xA7, 0xA7, 0xA7, 0xA7, 0xA7, 25, 26, 27, 28, 0xA7, 0xA7, 0xA7, 0xA7};
    std::array<std::uint8_t, 60> destination{};
    destination.fill(0xD9);
    const std::uint64_t residencyEpoch = Raster::ResidencyEpoch();
    const auto status = CopyRasterRowsForUpload(sparse, 1, 0, 4, 3, destination.data() + 2, 56, 20);
    Expect(status == RasterUploadWorkStatus::Ready, "sparse-bgra-offset-row-pitch-ready");
    Expect(!sparse.IsMaterialized(), "sparse-bgra-source-not-materialized");
    Expect(Raster::ResidencyEpoch() == residencyEpoch, "sparse-bgra-residency-epoch-unchanged");
    Expect(destination[0] == 0xD9 && destination[1] == 0xD9 && destination[58] == 0xD9 && destination[59] == 0xD9,
           "sparse-bgra-outer-guards-untouched");
    bool bytesMatch = true;
    for (std::size_t index = 0; index < expected.size(); ++index)
    {
        bytesMatch = bytesMatch && destination[index + 2] == expected[index];
    }
    Expect(bytesMatch, "sparse-bgra-base-tile-fill-offsets-and-pitch-padding");
}
} // namespace

int RunRasterUploadWorkTests()
{
    gFailures = 0;
    DenseBgraAndPitchedGuards();
    DenseGrayRows();
    SparseBaseTileAndFill();
    InvalidInputsAreUntouched();
    InvalidGeometryBoundaries();
    CapacityBoundaries();
    CancellationBoundaries();
    SparseBgraOffsetsPitchAndGuards();
    return gFailures;
}
