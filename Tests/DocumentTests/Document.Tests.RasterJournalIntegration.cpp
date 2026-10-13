#include "Document.Tests.RasterJournalIntegration.h"

#include "Document.h"
#include "ResidencyChangeJournal.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace
{
int gAssertions = 0;
int gFailures = 0;

void Expect(bool condition, const char* name) noexcept
{
    ++gAssertions;
    std::printf("%s raster-journal-integration.%s\n", condition ? "PASS" : "FAIL", name);
    gFailures += condition ? 0 : 1;
}

void ContiguousStorageIdentity()
{
    constexpr std::array<std::uint8_t, 1> grayPixel{0x7B};
    const std::uint64_t epochBefore = Raster::ResidencyEpoch();
    const Raster gray = Raster::Make(1, 1, RasterFormat::Gray8, grayPixel.data(), grayPixel.size());
    const Raster copy = gray;
    const Raster other = Raster::Make(1, 1, RasterFormat::Gray8, grayPixel.data(), grayPixel.size());

    Expect(Raster{}.StorageSerial() == 0, "serial.empty-is-zero");
    Expect(gray.StorageSerial() != 0, "serial.contiguous-is-nonzero");
    Expect(copy.StorageSerial() == gray.StorageSerial(), "serial.copied-handle-keeps-identity");
    Expect(other.StorageSerial() != 0 && other.StorageSerial() != gray.StorageSerial(),
           "serial.different-storage-has-different-identity");
    Expect(gray.Data() != nullptr && Raster::ResidencyEpoch() == epochBefore,
           "contiguous.data-does-not-advance-global-epoch");
}

void SparseMaterializationAndLeaseRules()
{
    constexpr std::array<std::uint8_t, 8> expectedPixels{0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80};
    constexpr std::array<std::uint8_t, 8> tileBytes{0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80};
    const Raster tile = Raster::Make(2, 1, RasterFormat::Bgra8888, tileBytes.data(), tileBytes.size());
    const RasterTile replacement{0, 0, tile};
    const std::uint64_t epochBefore = Raster::ResidencyEpoch();
    std::uint64_t sparseSerial = 0;
    {
        const Raster sparse = Raster::MakeSparse(2, 1, RasterFormat::Bgra8888, Raster{}, 0, 0, {replacement}, 0);
        sparseSerial = sparse.StorageSerial();
        Expect(sparse.IsSparse() && !sparse.IsMaterialized() && sparseSerial != 0,
               "materialize.fixture-is-distinct-sparse-storage");

        std::array<RasterResidencyChange, 1> changes{};
        std::size_t count = 99;
        const auto withoutLease = Raster::CopyResidencyChanges(epochBefore, changes, count);
        Expect(withoutLease == Raster::ResidencyJournal::CopyResult::InvalidWitness && count == 0,
               "copy.requires-exclusive-residency-lease");

        auto initialLease = Raster::AcquireResidencyLease();
        const std::uint64_t epochBeforeRefusedData = Raster::ResidencyEpoch();
        Expect(initialLease.Held() && sparse.Data() == nullptr && Raster::ResidencyEpoch() == epochBeforeRefusedData,
               "materialize.data-under-existing-lease-refuses-without-epoch-change");
        initialLease.Reset();

        const std::uint8_t* pixels = sparse.Data();
        const std::uint64_t epochAfterMaterialize = Raster::ResidencyEpoch();
        Expect(pixels != nullptr && sparse.IsMaterialized() && epochAfterMaterialize == epochBefore + 1,
               "materialize.retry-after-lease-release-publishes-one-global-epoch");
        bool matchesExpected = pixels != nullptr;
        if (pixels != nullptr)
            for (std::size_t index = 0; index < expectedPixels.size(); ++index)
                matchesExpected = matchesExpected && pixels[index] == expectedPixels[index];
        Expect(matchesExpected, "materialize.produces-literal-two-by-one-bgra-bytes");

        const auto* repeatedPixels = sparse.Data();
        Expect(repeatedPixels != nullptr && Raster::ResidencyEpoch() == epochAfterMaterialize,
               "materialize.repeated-data-does-not-advance-epoch");

        auto lease = Raster::AcquireResidencyLease();
        count = 0;
        const auto copied = Raster::CopyResidencyChanges(epochBefore, changes, count);
        Expect(lease.Held() && copied == Raster::ResidencyJournal::CopyResult::Complete && count == 1,
               "copy.lease-returns-complete-single-change");
        Expect(count == 1 && changes[0] == RasterResidencyChange{sparseSerial, epochAfterMaterialize, 8},
               "copy.change-has-storage-serial-epoch-and-eight-resident-bytes");

        std::array<RasterResidencyChange, 1> baseline{};
        std::size_t baselineCount = 0;
        const auto baselineResult = Raster::CopyResidencyChanges(epochBefore, baseline, baselineCount);
        Expect(baselineResult == Raster::ResidencyJournal::CopyResult::Complete && baselineCount == 1,
               "copy.cancellation-baseline-is-complete");

        CancelSource cancelled;
        static_cast<void>(cancelled.Cancel());
        count = 41;
        const auto cancelledResult = Raster::CopyResidencyChanges(epochBefore, changes, count, cancelled.Token());
        Expect(cancelledResult == Raster::ResidencyJournal::CopyResult::Cancelled && count == 0,
               "copy.cancelled-under-lease-publishes-zero-count");

        std::array<RasterResidencyChange, 1> subsequent{};
        std::size_t subsequentCount = 0;
        const auto subsequentResult = Raster::CopyResidencyChanges(epochBefore, subsequent, subsequentCount);
        Expect(subsequentResult == Raster::ResidencyJournal::CopyResult::Complete && subsequentCount == baselineCount &&
                   subsequent == baseline,
               "copy.cancelled-read-preserves-exact-subsequent-copy");

        lease.Reset();
    }

    std::array<RasterResidencyChange, 1> retained{};
    std::size_t retainedCount = 0;
    auto lease = Raster::AcquireResidencyLease();
    const auto retainedResult = Raster::CopyResidencyChanges(epochBefore, retained, retainedCount);
    Expect(retainedResult == Raster::ResidencyJournal::CopyResult::Complete && retainedCount == 1,
           "lifetime.event-remains-copyable-after-last-raster-dies");
    Expect(retainedCount == 1 && retained[0] == RasterResidencyChange{sparseSerial, epochBefore + 1, 8},
           "lifetime.retained-record-is-value-only-with-no-raster-owner");
}
} // namespace

int RunRasterJournalIntegrationTests()
{
    gAssertions = 0;
    gFailures = 0;
    ContiguousStorageIdentity();
    SparseMaterializationAndLeaseRules();
    std::printf("raster-journal-integration.summary assertions=%d failures=%d\n", gAssertions, gFailures);
    return gFailures;
}
