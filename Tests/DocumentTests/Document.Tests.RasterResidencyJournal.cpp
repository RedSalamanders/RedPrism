#include "Document.Tests.RasterResidencyJournal.h"

#include "Executor.h"
#include "ResidencyChangeJournal.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <span>
#include <thread>
#include <vector>

namespace
{
int controls = 0;
void Expect(bool condition, const char* label, int& failures)
{
    std::cout << (condition ? "PASS " : "FAIL ") << "raster-residency-journal." << label << '\n';
    ++controls;
    failures += condition ? 0 : 1;
}

using Journal = RasterResidencyJournal<3>;
using Change = RasterResidencyChange;

constexpr Change kEpoch3{22, 3, 1024};
constexpr Change kEpoch4{33, 4, 64};
constexpr Change kEpoch5{44, 5, 4096};

bool IsRecord(const Change& actual, const Change& expected) noexcept
{
    return actual.storageSerial == expected.storageSerial && actual.epoch == expected.epoch &&
           actual.residentSelfBytes == expected.residentSelfBytes;
}

struct VisitCount final
{
    std::size_t visits = 0;
};

void CountVisit(void* context, std::size_t) noexcept
{
    ++static_cast<VisitCount*>(context)->visits;
}

struct CancelAtVisit final
{
    CancelSource* source = nullptr;
    std::size_t cancelAt = 0;
    std::size_t visits = 0;
};

void CancelAfterChosenVisit(void* context, std::size_t copied) noexcept
{
    auto& action = *static_cast<CancelAtVisit*>(context);
    ++action.visits;
    if (copied == action.cancelAt)
        static_cast<void>(action.source->Cancel());
}

void TestJournalRingSuffixAndWitnesses(int& failures)
{
    Journal journal(1);
    const bool initialEpoch = journal.Epoch() == 1;
    const bool appended =
        journal.Append(11, 256) && journal.Append(22, 1024) && journal.Append(33, 64) && journal.Append(44, 4096);
    Expect(initialEpoch && appended && journal.Epoch() == 5, "journal.literal-append-wrap-epoch", failures);

    std::array<Change, 3> exactCapacity{};
    std::size_t exactCount = 99;
    const auto exactResult = journal.CopySince(2, exactCapacity, exactCount);
    Expect(exactResult == Journal::CopyResult::Complete && exactCount == 3 && IsRecord(exactCapacity[0], kEpoch3) &&
               IsRecord(exactCapacity[1], kEpoch4) && IsRecord(exactCapacity[2], kEpoch5),
           "journal.exact-capacity-suffix-literal-order-after-wrap", failures);

    std::array<Change, 2> shortSuffix{};
    std::size_t shortCount = 99;
    const auto shortResult = journal.CopySince(3, shortSuffix, shortCount);
    Expect(shortResult == Journal::CopyResult::Complete && shortCount == 2 &&
               IsRecord(shortSuffix[0], Change{33, 4, 64}) && IsRecord(shortSuffix[1], Change{44, 5, 4096}),
           "journal.shorter-suffix-literal-order", failures);

    std::array<Change, 1> oneSuffix{};
    std::size_t oneCount = 99;
    const auto oneResult = journal.CopySince(4, oneSuffix, oneCount);
    Expect(oneResult == Journal::CopyResult::Complete && oneCount == 1 && IsRecord(oneSuffix[0], Change{44, 5, 4096}),
           "journal.latest-record-suffix", failures);

    VisitCount emptyVisits;
    std::size_t emptyCount = 99;
    const auto emptyResult =
        journal.CopySince(5, std::span<Change>{}, emptyCount, {}, Journal::CopyObserver{&emptyVisits, &CountVisit});
    Expect(emptyResult == Journal::CopyResult::Complete && emptyCount == 0 && emptyVisits.visits == 0,
           "journal.empty-suffix-empty-span-zero-visits", failures);

    std::array<Change, 3> shortBuffer{};
    std::size_t shortBufferCount = 99;
    const auto shortBufferResult = journal.CopySince(2, std::span<Change>{shortBuffer.data(), 2}, shortBufferCount);
    Expect(shortBufferResult == Journal::CopyResult::BufferTooSmall && shortBufferCount == 0,
           "journal.small-buffer-refuses-with-zero-published-count", failures);

    std::array<Change, 3> overwrittenBuffer{};
    std::size_t overwrittenCount = 99;
    const auto overwrittenResult = journal.CopySince(1, overwrittenBuffer, overwrittenCount);
    Expect(overwrittenResult == Journal::CopyResult::Overwritten && overwrittenCount == 0,
           "journal.overwritten-witness-refuses-with-zero-published-count", failures);

    std::size_t invalidZeroCount = 99;
    const auto invalidZero = journal.CopySince(0, exactCapacity, invalidZeroCount);
    std::size_t futureCount = 99;
    const auto futureWitness = journal.CopySince(6, exactCapacity, futureCount);
    Expect(invalidZero == Journal::CopyResult::InvalidWitness && invalidZeroCount == 0 &&
               futureWitness == Journal::CopyResult::InvalidWitness && futureCount == 0,
           "journal.zero-and-future-witness-refuse-with-zero-count", failures);

    Journal invalidInitialEpoch(0);
    std::array<Change, 1> invalidInitialOutput{};
    std::size_t invalidInitialCount = 99;
    const auto invalidInitialResult = invalidInitialEpoch.CopySince(1, invalidInitialOutput, invalidInitialCount);
    Expect(invalidInitialEpoch.Epoch() == 0 && !invalidInitialEpoch.Append(11, 256) &&
               invalidInitialResult == Journal::CopyResult::InvalidWitness && invalidInitialCount == 0,
           "journal.zero-initial-epoch-is-invalid-and-append-refused", failures);

    const auto epochBeforeInvalidAppends = journal.Epoch();
    const bool zeroSerialRefused = !journal.Append(0, 128);
    const bool zeroBytesRefused = !journal.Append(55, 0);
    std::array<Change, 3> afterInvalidAppends{};
    std::size_t afterInvalidCount = 99;
    const auto afterInvalidResult = journal.CopySince(2, afterInvalidAppends, afterInvalidCount);
    Expect(zeroSerialRefused && zeroBytesRefused && journal.Epoch() == epochBeforeInvalidAppends &&
               afterInvalidResult == Journal::CopyResult::Complete && afterInvalidCount == 3 &&
               IsRecord(afterInvalidAppends[0], kEpoch3) && IsRecord(afterInvalidAppends[1], kEpoch4) &&
               IsRecord(afterInvalidAppends[2], kEpoch5),
           "journal.invalid-appends-preserve-epoch-and-literal-content", failures);
}

void TestJournalCancellationPublication(int& failures)
{
    Journal journal(1);
    const bool appended =
        journal.Append(11, 256) && journal.Append(22, 1024) && journal.Append(33, 64) && journal.Append(44, 4096);

    CancelSource preCancelled;
    static_cast<void>(preCancelled.Cancel());
    std::array<Change, 3> preCancelledScratch{};
    std::size_t preCancelledCount = 99;
    const auto preCancelledResult = journal.CopySince(2, preCancelledScratch, preCancelledCount, preCancelled.Token());
    Expect(appended && preCancelledResult == Journal::CopyResult::Cancelled && preCancelledCount == 0,
           "journal.pre-cancelled-copy-publishes-zero", failures);

    CancelSource midpointCancellation;
    CancelAtVisit action{&midpointCancellation, 2, 0};
    std::array<Change, 3> partialScratch{};
    std::size_t partialCount = 99;
    const auto partialResult = journal.CopySince(2, partialScratch, partialCount, midpointCancellation.Token(),
                                                 Journal::CopyObserver{&action, &CancelAfterChosenVisit});
    const bool partialWasWritten = IsRecord(partialScratch[0], kEpoch3) && IsRecord(partialScratch[1], kEpoch4);
    std::array<Change, 3> remaining{};
    std::size_t remainingCount = 99;
    const auto remainingResult = journal.CopySince(2, remaining, remainingCount);
    Expect(partialResult == Journal::CopyResult::Cancelled && action.visits == 2 && partialWasWritten &&
               partialCount == 0,
           "journal.midpoint-cancel-allows-scratch-prefix-but-publishes-zero", failures);
    Expect(remainingResult == Journal::CopyResult::Complete && remainingCount == 3 &&
               IsRecord(remaining[0], Change{22, 3, 1024}) && IsRecord(remaining[1], Change{33, 4, 64}) &&
               IsRecord(remaining[2], Change{44, 5, 4096}),
           "journal.midpoint-cancel-leaves-independent-literal-journal-readable", failures);
}

void TestJournalEpochExhaustion(int& failures)
{
    constexpr std::uint64_t maximum = (std::numeric_limits<std::uint64_t>::max)();
    Journal journal(maximum - 1);
    const bool lastAppend = journal.Append(77, 512);
    const auto epochAtMaximum = journal.Epoch();
    const bool exhaustedAppendRefused = !journal.Append(88, 1024);
    std::array<Change, 1> latest{};
    std::size_t latestCount = 99;
    const auto latestResult = journal.CopySince(maximum - 1, latest, latestCount);
    Expect(lastAppend && epochAtMaximum == maximum && exhaustedAppendRefused && journal.Epoch() == maximum &&
               latestResult == Journal::CopyResult::Complete && latestCount == 1 &&
               IsRecord(latest[0], Change{77, maximum, 512}),
           "journal.maximum-epoch-append-then-exhausts-without-wrap", failures);
}

void TestRasterStorageSerialSequence(int& failures)
{
    RasterStorageSerial serials;
    const auto first = serials.Allocate();
    const auto second = serials.Allocate();
    const auto third = serials.Allocate();
    Expect(first == 1 && second == 2 && third == 3, "serial.default-start-and-sequential-uniqueness", failures);

    constexpr std::uint64_t maximum = (std::numeric_limits<std::uint64_t>::max)();
    RasterStorageSerial nearMaximum(maximum - 1);
    const auto penultimate = nearMaximum.Allocate();
    const auto finalValue = nearMaximum.Allocate();
    const auto exhaustedFirst = nearMaximum.Allocate();
    const auto exhaustedAgain = nearMaximum.Allocate();
    Expect(penultimate == maximum - 1 && finalValue == maximum && exhaustedFirst == 0 && exhaustedAgain == 0,
           "serial.maximum-value-then-permanent-zero-exhaustion", failures);
}

void TestRasterStorageSerialConcurrentRange(int& failures)
{
    constexpr std::size_t workerCount = 4;
    constexpr std::size_t valuesPerWorker = 512;
    constexpr std::size_t totalValues = workerCount * valuesPerWorker;

    RasterStorageSerial serials;
    std::vector<std::array<std::uint64_t, valuesPerWorker>> values(workerCount);
    std::array<std::jthread, workerCount> workers{};
    for (std::size_t worker = 0; worker < workerCount; ++worker)
    {
        workers[worker] = std::jthread(
            [&, worker]() noexcept
            {
                for (std::size_t index = 0; index < valuesPerWorker; ++index)
                    values[worker][index] = serials.Allocate();
            });
    }
    for (std::jthread& worker : workers)
        worker.join();

    std::vector<std::uint8_t> seen(totalValues + 1, 0);
    bool inRangeAndUnique = true;
    for (const auto& workerValues : values)
    {
        for (const std::uint64_t value : workerValues)
        {
            if (value == 0 || value > totalValues || seen[static_cast<std::size_t>(value)] != 0)
            {
                inRangeAndUnique = false;
                continue;
            }
            seen[static_cast<std::size_t>(value)] = 1;
        }
    }
    bool exactContinuousRange = inRangeAndUnique;
    for (std::size_t expected = 1; expected <= totalValues; ++expected)
        exactContinuousRange = exactContinuousRange && seen[expected] == 1;
    Expect(exactContinuousRange && serials.Allocate() == totalValues + 1,
           "serial.concurrent-four-workers-cover-exact-continuous-nonzero-range", failures);
}
void MainFirstRecordAndExhaustedSerial(int& failures)
{
    Journal journal;
    std::array<Change, 1> output{};
    std::size_t count = 99;
    const bool appended = journal.Append(11, 256);
    const auto copied = journal.CopySince(1, output, count);
    Expect(appended && copied == Journal::CopyResult::Complete && count == 1 && IsRecord(output[0], Change{11, 2, 256}),
           "journal.first-record-literal-before-wrap", failures);
    RasterStorageSerial exhausted(0);
    Expect(exhausted.Allocate() == 0 && exhausted.Allocate() == 0, "serial.zero-initial-value-is-permanently-exhausted",
           failures);
}
void MainFinalCopyCancellation(int& failures)
{
    Journal journal;
    const bool appended = journal.Append(11, 256) && journal.Append(22, 1024) && journal.Append(33, 64);
    CancelSource source;
    CancelAtVisit action{&source, 3, 0};
    std::array<Change, 3> scratch{};
    std::size_t count = 99;
    const auto result = journal.CopySince(1, scratch, count, source.Token(), {&action, &CancelAfterChosenVisit});
    Expect(appended && result == Journal::CopyResult::Cancelled && count == 0 && action.visits == 3 &&
               IsRecord(scratch[0], Change{11, 2, 256}) && IsRecord(scratch[1], Change{22, 3, 1024}) &&
               IsRecord(scratch[2], Change{33, 4, 64}),
           "journal.final-record-cancel-publishes-no-complete-suffix", failures);
}

} // namespace

int RunRasterResidencyJournalTests()
{
    int failures = 0;
    controls = 0;
    MainFirstRecordAndExhaustedSerial(failures);
    MainFinalCopyCancellation(failures);
    TestJournalRingSuffixAndWitnesses(failures);
    TestJournalCancellationPublication(failures);
    TestJournalEpochExhaustion(failures);
    TestRasterStorageSerialSequence(failures);
    TestRasterStorageSerialConcurrentRange(failures);
    std::cout << (failures ? "FAIL " : "PASS ") << "raster-residency-journal.summary passed=" << controls - failures
              << " failed=" << failures << " controls=" << controls << '\n';
    return failures;
}
