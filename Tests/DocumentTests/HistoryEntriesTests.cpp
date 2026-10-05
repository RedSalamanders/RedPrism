#include "HistoryEntriesTests.h"

#include "HistoryEntries.h"

#include <array>
#include <atomic>
#include <barrier>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <iterator>
#include <limits>
#include <memory>
#include <new>
#include <optional>
#include <thread>
#include <utility>

namespace
{
int gFailures = 0;

void Expect(bool condition, const char* name) noexcept
{
    std::printf("%s history-entries.%s\n", condition ? "PASS" : "FAIL", name);
    if (!condition)
        ++gFailures;
}

struct CopyCounter final
{
    std::atomic<std::size_t> copies{};
};

struct Handle final
{
    Handle(CopyCounter& counter, int value, std::shared_ptr<int> witness = {}) noexcept
        : counter_(&counter), value_(value), witness_(std::move(witness))
    {
    }

    Handle(const Handle& other) noexcept : counter_(other.counter_), value_(other.value_), witness_(other.witness_)
    {
        counter_->copies.fetch_add(1, std::memory_order_relaxed);
    }
    Handle& operator=(const Handle& other) noexcept
    {
        if (this != &other)
        {
            counter_ = other.counter_;
            value_ = other.value_;
            witness_ = other.witness_;
            counter_->copies.fetch_add(1, std::memory_order_relaxed);
        }
        return *this;
    }
    Handle(Handle&&) noexcept = default;
    Handle& operator=(Handle&&) noexcept = default;

    CopyCounter* counter_{};
    int value_{};
    std::shared_ptr<int> witness_;
};

using Entries = HistoryEntries<Handle>;

struct OwnershipMeasure final
{
    std::size_t calls{};
};

struct MeasuredEntry final
{
    MeasuredEntry(OwnershipMeasure& measure, int value, std::optional<RecordOwnershipBounds> bounds) noexcept
        : measure_(&measure), value_(value), bounds_(bounds)
    {
    }

    [[nodiscard]] std::optional<RecordOwnershipBounds> OwnershipBounds() const noexcept
    {
        ++measure_->calls;
        return bounds_;
    }

    OwnershipMeasure* measure_{};
    int value_{};
    std::optional<RecordOwnershipBounds> bounds_;
};

using MeasuredHandle = std::shared_ptr<const MeasuredEntry>;
using MeasuredEntries = HistoryEntries<MeasuredHandle>;
static_assert(sizeof(MeasuredEntries) == 16);

MeasuredHandle MakeMeasuredEntry(OwnershipMeasure& measure, int value, std::optional<RecordOwnershipBounds> bounds)
{
    return std::make_shared<const MeasuredEntry>(measure, value, bounds);
}

bool BoundsAre(const MeasuredEntries& entries, const RecordOwnershipBounds& expected) noexcept
{
    const auto actual = entries.OwnershipBounds();
    return actual.has_value() && actual.value() == expected;
}

bool HasUnavailableBounds(const MeasuredEntries& entries) noexcept
{
    return !entries.OwnershipBounds().has_value();
}

std::uint64_t ExpectedRootBytes(const MeasuredEntries& entries) noexcept
{
    return static_cast<std::uint64_t>(sizeof(MeasuredEntries::Container) +
                                      sizeof(std::optional<RecordOwnershipBounds>) + 64 +
                                      entries.capacity() * sizeof(MeasuredHandle));
}

void TestOwnershipCacheEmptyCopyAndQueries()
{
    OwnershipMeasure measure;
    MeasuredEntries empty;
    const RecordOwnershipBounds zero{};
    Expect(BoundsAre(empty, zero), "ownership-cache-default-root-is-valid-zero");
    const MeasuredEntries emptyCopy = empty;
    Expect(emptyCopy.Identity() == empty.Identity() && BoundsAre(emptyCopy, zero),
           "ownership-cache-empty-copy-preserves-zero-bounds");

    MeasuredEntries entries;
    entries.push_back(MakeMeasuredEntry(measure, 7, RecordOwnershipBounds{3, 5, 2}));
    const std::size_t callsAfterPublish = measure.calls;
    const void* const identityBeforeQueries = entries.Identity();
    const auto cached = entries.OwnershipBounds();
    const MeasuredEntries copy = entries;
    const auto copied = copy.OwnershipBounds();
    bool repeatedReadsMatch = cached.has_value() && copied == cached;
    for (int query = 0; query < 2048; ++query)
    {
        const auto queried = entries.OwnershipBounds();
        repeatedReadsMatch = repeatedReadsMatch && queried == cached;
    }
    Expect(repeatedReadsMatch && measure.calls == callsAfterPublish && entries.Identity() == identityBeforeQueries,
           "ownership-cache-thousands-of-queries-and-copy-do-not-remeasure");
    Expect(cached.has_value() && cached->metadataBytes >= 3 && cached->ownershipBytes >= 5 &&
               cached->ownershipBytes - cached->metadataBytes == 2 && cached->rasterNodes == 2,
           "ownership-cache-includes-root-and-preserves-child-components");
    Expect(cached.has_value() && cached->metadataBytes == ExpectedRootBytes(entries) + 3 &&
               cached->ownershipBytes == ExpectedRootBytes(entries) + 5,
           "ownership-cache-counts-root-header-capacity-and-control-allowance");
}

void TestOwnershipCacheMutationAndRootSnapshots()
{
    OwnershipMeasure measure;
    MeasuredEntries entries;
    entries.push_back(MakeMeasuredEntry(measure, 11, RecordOwnershipBounds{2, 4, 1}));
    const MeasuredEntries oldRoot = entries;
    const auto oldBounds = oldRoot.OwnershipBounds();
    const std::size_t callsBeforeMutation = measure.calls;
    entries.push_back(MakeMeasuredEntry(measure, 22, RecordOwnershipBounds{5, 9, 3}));
    const auto newBounds = entries.OwnershipBounds();
    Expect(oldBounds.has_value() && newBounds.has_value() && oldBounds != newBounds &&
               oldRoot.Identity() != entries.Identity() && oldRoot.size() == 1 && entries.size() == 2,
           "ownership-cache-mutation-publishes-new-root-and-keeps-old-snapshot");
    Expect(measure.calls == callsBeforeMutation + 2 && oldBounds.has_value() && newBounds.has_value() &&
               newBounds->metadataBytes > oldBounds->metadataBytes + 5 &&
               newBounds->ownershipBytes > oldBounds->ownershipBytes + 9 &&
               newBounds->rasterNodes == oldBounds->rasterNodes + 3,
           "ownership-cache-mutation-measures-complete-private-vector");

    const std::size_t callsBeforeMove = measure.calls;
    MeasuredEntries moved(std::move(entries));
    Expect(entries.empty() && BoundsAre(entries, RecordOwnershipBounds{}) && moved.size() == 2 &&
               moved.OwnershipBounds() == newBounds && measure.calls == callsBeforeMove,
           "ownership-cache-move-transfers-cache-without-measurement");

    MeasuredEntries other;
    other.push_back(MakeMeasuredEntry(measure, 33, RecordOwnershipBounds{1, 3, 4}));
    const auto otherBounds = other.OwnershipBounds();
    const std::size_t callsBeforeSwap = measure.calls;
    moved.swap(other);
    Expect(moved.OwnershipBounds() == otherBounds && other.OwnershipBounds() == newBounds &&
               measure.calls == callsBeforeSwap,
           "ownership-cache-swap-transfers-matching-caches");

    MeasuredEntries assigned;
    assigned = std::move(other);
    Expect(other.empty() && BoundsAre(other, RecordOwnershipBounds{}) && assigned.OwnershipBounds() == newBounds &&
               measure.calls == callsBeforeSwap,
           "ownership-cache-move-assignment-transfers-cache");
    assigned.clear();
    Expect(assigned.empty() && BoundsAre(assigned, RecordOwnershipBounds{}) && oldRoot.OwnershipBounds() == oldBounds,
           "ownership-cache-clear-restores-zero-with-snapshot-intact");
}

void TestOwnershipCachePopAndEraseRestoreCompleteBounds()
{
    OwnershipMeasure measure;
    MeasuredEntries entries;
    entries.push_back(MakeMeasuredEntry(measure, 1, RecordOwnershipBounds{2, 4, 1}));
    const auto oneEntryBounds = entries.OwnershipBounds();
    entries.push_back(MakeMeasuredEntry(measure, 2, RecordOwnershipBounds{3, 7, 2}));
    entries.push_back(MakeMeasuredEntry(measure, 3, RecordOwnershipBounds{5, 8, 4}));
    entries.pop_back();
    const auto afterPop = entries.OwnershipBounds();
    Expect(oneEntryBounds.has_value() && entries.size() == 2 && entries.back()->value_ == 2 && afterPop.has_value() &&
               afterPop->metadataBytes > oneEntryBounds->metadataBytes + 3 &&
               afterPop->ownershipBytes > oneEntryBounds->ownershipBytes + 7 &&
               afterPop->rasterNodes == oneEntryBounds->rasterNodes + 2,
           "ownership-cache-pop-remeasures-retained-complete-vector");

    entries.erase(entries.begin(), entries.begin() + 1);
    const auto afterErase = entries.OwnershipBounds();
    Expect(entries.size() == 1 && entries.back()->value_ == 2 && afterErase.has_value() && afterPop.has_value() &&
               afterErase->metadataBytes == ExpectedRootBytes(entries) + 3 &&
               afterErase->ownershipBytes == ExpectedRootBytes(entries) + 7 && afterErase->rasterNodes == 2,
           "ownership-cache-erase-rebuilds-bounds-for-surviving-entry");
}

void TestOwnershipCacheRefusesInvalidChildrenAndOverflow()
{
    OwnershipMeasure measure;
    const int kValue = 44;
    const auto assertRefusalPreservesContents = [&](MeasuredHandle entry, const char* name)
    {
        MeasuredEntries entries;
        entries.push_back(std::move(entry));
        Expect(HasUnavailableBounds(entries) && entries.size() == 1 && entries.back()->value_ == kValue, name);
    };

    assertRefusalPreservesContents(MakeMeasuredEntry(measure, kValue, std::nullopt),
                                   "ownership-cache-unavailable-child-keeps-valid-entry");
    assertRefusalPreservesContents(MakeMeasuredEntry(measure, kValue, RecordOwnershipBounds{8, 7, 1}),
                                   "ownership-cache-inconsistent-child-keeps-valid-entry");
    assertRefusalPreservesContents(
        MakeMeasuredEntry(measure, kValue,
                          RecordOwnershipBounds{(std::numeric_limits<std::uint64_t>::max)() - 1,
                                                (std::numeric_limits<std::uint64_t>::max)() - 1, 0}),
        "ownership-cache-byte-overflow-keeps-valid-entry");
    MeasuredEntries rasterOverflow;
    rasterOverflow.push_back(
        MakeMeasuredEntry(measure, kValue, RecordOwnershipBounds{0, 1, (std::numeric_limits<std::uint64_t>::max)()}));
    rasterOverflow.push_back(MakeMeasuredEntry(measure, kValue + 1, RecordOwnershipBounds{0, 1, 1}));
    Expect(HasUnavailableBounds(rasterOverflow) && rasterOverflow.size() == 2 &&
               (*rasterOverflow.begin())->value_ == kValue && rasterOverflow.back()->value_ == kValue + 1,
           "ownership-cache-raster-overflow-keeps-valid-entries");

    MeasuredEntries nullEntry;
    nullEntry.push_back({});
    Expect(HasUnavailableBounds(nullEntry) && nullEntry.size() == 1 && !nullEntry.back(),
           "ownership-cache-null-child-keeps-null-entry");
    HistoryEntries<std::shared_ptr<const int>> noPolicy;
    noPolicy.push_back(std::make_shared<const int>(9));
    Expect(!noPolicy.OwnershipBounds() && noPolicy.size() == 1 && *noPolicy.back() == 9,
           "ownership-cache-unmeasurable-handle-keeps-valid-entry");
}

bool ValuesAre(const Entries& entries, const int* expected, std::size_t count) noexcept
{
    if (entries.size() != count)
        return false;
    std::size_t index = 0;
    for (auto it = entries.begin(); it != entries.end(); ++it, ++index)
    {
        if (it->value_ != expected[index])
            return false;
    }
    return true;
}

void TestEmptySharingAndReads()
{
    CopyCounter counter;
    Entries entries;
    Expect(entries.empty() && entries.size() == 0, "default-is-empty");

    entries.push_back(Handle{counter, 11});
    entries.push_back(Handle{counter, 22});
    counter.copies.store(0, std::memory_order_relaxed);
    const void* const originalIdentity = entries.Identity();
    const Entries snapshot = entries;
    Expect(snapshot.Identity() == originalIdentity && counter.copies.load(std::memory_order_relaxed) == 0,
           "snapshot-copy-shares-root-without-copying-handles");

    int nonConstReadSum = 0;
    for (auto it = entries.begin(); it != entries.end(); ++it)
        nonConstReadSum += it->value_;
    const bool nonConstRead = entries.size() == 2 && entries.back().value_ == 22 && nonConstReadSum == 33 &&
                              entries.Identity() == originalIdentity &&
                              counter.copies.load(std::memory_order_relaxed) == 0;
    Expect(nonConstRead, "nonconst-reads-do-not-detach-or-copy-handles");

    const Entries& constEntries = entries;
    int constReadSum = 0;
    for (auto it = constEntries.begin(); it != constEntries.end(); ++it)
        constReadSum += it->value_;
    Expect(constReadSum == 33 && constEntries.back().value_ == 22 && constEntries.Identity() == originalIdentity &&
               counter.copies.load(std::memory_order_relaxed) == 0,
           "const-reads-do-not-detach-or-copy-handles");

    entries.push_back(Handle{counter, 33});
    const int originalValues[]{11, 22};
    Expect(counter.copies.load(std::memory_order_relaxed) == 2 && entries.Identity() != originalIdentity &&
               snapshot.Identity() == originalIdentity && ValuesAre(snapshot, originalValues, 2),
           "first-write-copies-exact-existing-count-and-preserves-shared-root");
}

void TestPopEraseAndNoOpErase()
{
    CopyCounter counter;
    Entries entries;
    entries.push_back(Handle{counter, 4});
    entries.push_back(Handle{counter, 8});
    entries.push_back(Handle{counter, 12});
    entries.push_back(Handle{counter, 16});

    const Entries beforePop = entries;
    const void* const beforePopIdentity = beforePop.Identity();
    counter.copies.store(0, std::memory_order_relaxed);
    entries.pop_back();
    const int allValues[]{4, 8, 12, 16};
    const int poppedValues[]{4, 8, 12};
    Expect(counter.copies.load(std::memory_order_relaxed) == 4 && beforePop.Identity() == beforePopIdentity &&
               ValuesAre(beforePop, allValues, 4) && ValuesAre(entries, poppedValues, 3),
           "pop-detaches-exactly-once-and-keeps-captured-root");

    const Entries beforeEmptyErase = entries;
    const void* const beforeEmptyEraseIdentity = entries.Identity();
    counter.copies.store(0, std::memory_order_relaxed);
    const auto emptyPosition = entries.begin() + 1;
    entries.erase(emptyPosition, emptyPosition);
    Expect(entries.Identity() == beforeEmptyEraseIdentity && beforeEmptyErase.Identity() == beforeEmptyEraseIdentity &&
               counter.copies.load(std::memory_order_relaxed) == 0 && ValuesAre(beforeEmptyErase, poppedValues, 3),
           "zero-length-erase-keeps-sharing-without-copying");

    const Entries beforeErase = entries;
    const void* const beforeEraseIdentity = beforeErase.Identity();
    counter.copies.store(0, std::memory_order_relaxed);
    entries.erase(entries.begin() + 1, entries.begin() + 2);
    const int erasedValues[]{4, 12};
    Expect(counter.copies.load(std::memory_order_relaxed) == 3 && beforeErase.Identity() == beforeEraseIdentity &&
               ValuesAre(entries, erasedValues, 2) && ValuesAre(beforeErase, poppedValues, 3),
           "erase-detaches-exactly-once-and-keeps-captured-root");
}

void TestClearSwapAndMoves()
{
    CopyCounter counter;
    Entries first;
    first.push_back(Handle{counter, 31});
    first.push_back(Handle{counter, 32});
    const Entries captured = first;
    const void* const capturedIdentity = captured.Identity();
    first.clear();
    const int firstValues[]{31, 32};
    Expect(first.empty() && captured.Identity() == capturedIdentity && ValuesAre(captured, firstValues, 2),
           "clear-keeps-captured-root-alive");

    Entries second;
    second.push_back(Handle{counter, 91});
    const void* const secondIdentity = second.Identity();
    first.swap(second);
    const int secondValues[]{91};
    Expect(ValuesAre(first, secondValues, 1) && first.Identity() == secondIdentity && second.empty() &&
               captured.Identity() == capturedIdentity && ValuesAre(captured, firstValues, 2),
           "swap-transfers-roots-without-changing-captured-content");

    Entries moved(std::move(first));
    Expect(first.empty() && ValuesAre(moved, secondValues, 1), "move-construction-leaves-source-empty");

    Entries assigned;
    assigned.push_back(Handle{counter, -1});
    assigned = std::move(moved);
    Expect(moved.empty() && ValuesAre(assigned, secondValues, 1), "move-assignment-leaves-source-empty");
}

void TestRootOwnsHandleWitness()
{
    CopyCounter counter;
    std::weak_ptr<int> witness;
    Entries entries;
    {
        auto lifetime = std::make_shared<int>(73);
        witness = lifetime;
        entries.push_back(Handle{counter, 73, std::move(lifetime)});
    }
    Entries captured = entries;
    entries.clear();
    Expect(!witness.expired() && captured.back().value_ == 73, "captured-root-retains-handle-witness-after-clear");
    captured.clear();
    Expect(witness.expired(), "handle-witness-expires-after-final-root-owner");
}

void TestConcurrentReadersAndPrivateWriter()
{
    constexpr std::size_t kReaderCount = 4;
    constexpr int kRounds = 24;
    CopyCounter counter;
    Entries base;
    for (int value = 0; value < 12; ++value)
        base.push_back(Handle{counter, value});

    Entries writer = base;
    std::array<bool, kReaderCount> readerResults{};
    bool writerResult = true;
    std::barrier<> rendezvous{static_cast<std::ptrdiff_t>(kReaderCount + 1)};
    std::array<std::jthread, kReaderCount> readers;
    for (std::size_t readerIndex = 0; readerIndex < kReaderCount; ++readerIndex)
    {
        readers[readerIndex] = std::jthread(
            [&, readerIndex]
            {
                const Entries privateRead = base;
                bool correct = true;
                for (int round = 0; round < kRounds; ++round)
                {
                    rendezvous.arrive_and_wait();
                    if (privateRead.size() != 12)
                        correct = false;
                    int value = 0;
                    for (auto it = privateRead.begin(); it != privateRead.end(); ++it, ++value)
                    {
                        if (it->value_ != value)
                            correct = false;
                    }
                    rendezvous.arrive_and_wait();
                }
                readerResults[readerIndex] = correct;
            });
    }
    std::jthread writerThread(
        [&]
        {
            for (int round = 0; round < kRounds; ++round)
            {
                rendezvous.arrive_and_wait();
                writer.push_back(Handle{counter, 100 + round});
                if (writer.size() != static_cast<std::size_t>(13 + round) || writer.back().value_ != 100 + round ||
                    base.size() != 12 || base.back().value_ != 11)
                {
                    writerResult = false;
                }
                rendezvous.arrive_and_wait();
            }
        });

    for (std::jthread& reader : readers)
        reader.join();
    writerThread.join();

    bool allReadersCorrect = true;
    for (const bool result : readerResults)
        allReadersCorrect = allReadersCorrect && result;
    Expect(allReadersCorrect, "bounded-concurrent-readers-see-stable-independent-integer-content");
    Expect(writerResult && base.size() == 12 && writer.size() == static_cast<std::size_t>(12 + kRounds),
           "private-cloned-writer-mutates-without-changing-reader-root");
}

struct RefusalState final
{
    std::size_t refuseOnAttempt{};
    std::size_t copyAttempts{};
    std::size_t successfulCopies{};
    std::size_t refusedCopies{};
};

struct RefusingHandle final
{
    RefusingHandle(RefusalState& state, int value, std::shared_ptr<int> witness) noexcept
        : state_(&state), value_(value), witness_(std::move(witness))
    {
    }

    RefusingHandle(const RefusingHandle& other) : state_(other.state_), value_(other.value_)
    {
        ++state_->copyAttempts;
        if (state_->copyAttempts == state_->refuseOnAttempt)
        {
            ++state_->refusedCopies;
            throw std::bad_alloc{};
        }
        witness_ = other.witness_;
        ++state_->successfulCopies;
    }
    RefusingHandle& operator=(const RefusingHandle&) = delete;
    RefusingHandle(RefusingHandle&&) noexcept = default;
    RefusingHandle& operator=(RefusingHandle&&) noexcept = default;

    RefusalState* state_{};
    int value_{};
    std::shared_ptr<int> witness_;
};

using RefusalEntries = HistoryEntries<RefusingHandle>;

void FillRefusalEntries(RefusalState& state, RefusalEntries& entries, std::weak_ptr<int>* witnesses, const int* values,
                        std::size_t count)
{
    for (std::size_t index = 0; index < count; ++index)
    {
        auto witness = std::make_shared<int>(values[index]);
        witnesses[index] = witness;
        entries.push_back(RefusingHandle{state, values[index], std::move(witness)});
    }
    state.copyAttempts = 0;
    state.successfulCopies = 0;
    state.refusedCopies = 0;
    state.refuseOnAttempt = 2;
}

bool RefusalValuesAre(const RefusalEntries& entries, const int* expected, std::size_t count) noexcept
{
    if (entries.size() != count)
        return false;
    std::size_t index = 0;
    for (auto it = entries.begin(); it != entries.end(); ++it, ++index)
    {
        if (it->value_ != expected[index])
            return false;
    }
    return true;
}

bool OriginalWitnessesHaveSingleOwner(const std::weak_ptr<int>* witnesses, std::size_t count) noexcept
{
    for (std::size_t index = 0; index < count; ++index)
    {
        if (witnesses[index].expired() || witnesses[index].use_count() != 1)
            return false;
    }
    return true;
}

void TestPushCopyRefusalPreservesRootsAndOwnership()
{
    constexpr int kValues[]{17, 29, 41};
    constexpr int kPushedValue = 53;
    RefusalState state;
    std::weak_ptr<int> witnesses[3];
    RefusalEntries entries;
    FillRefusalEntries(state, entries, witnesses, kValues, std::size(kValues));
    const RefusalEntries captured = entries;
    const void* const rootIdentity = entries.Identity();

    auto incomingWitness = std::make_shared<int>(kPushedValue);
    std::weak_ptr<int> incomingWeak = incomingWitness;
    bool caughtBadAlloc = false;
    try
    {
        entries.push_back(RefusingHandle{state, kPushedValue, std::move(incomingWitness)});
    }
    catch (const std::bad_alloc&)
    {
        caughtBadAlloc = true;
    }

    Expect(caughtBadAlloc && state.copyAttempts == 2 && state.successfulCopies == 1 && state.refusedCopies == 1,
           "push-copy-refusal-has-exact-partial-copy-counts");
    Expect(entries.Identity() == rootIdentity && captured.Identity() == rootIdentity,
           "push-copy-refusal-preserves-live-and-captured-root-identity");
    Expect(RefusalValuesAre(entries, kValues, std::size(kValues)) &&
               RefusalValuesAre(captured, kValues, std::size(kValues)),
           "push-copy-refusal-preserves-live-and-captured-contents");
    Expect(OriginalWitnessesHaveSingleOwner(witnesses, std::size(witnesses)) && incomingWeak.expired(),
           "push-copy-refusal-restores-handle-owners-and-releases-unpublished-value");
}

void TestPopCopyRefusalPreservesRootsAndOwnership()
{
    constexpr int kValues[]{61, 73, 89};
    RefusalState state;
    std::weak_ptr<int> witnesses[std::size(kValues)];
    RefusalEntries entries;
    FillRefusalEntries(state, entries, witnesses, kValues, std::size(kValues));
    const RefusalEntries captured = entries;
    const void* const rootIdentity = entries.Identity();

    bool caughtBadAlloc = false;
    try
    {
        entries.pop_back();
    }
    catch (const std::bad_alloc&)
    {
        caughtBadAlloc = true;
    }

    Expect(caughtBadAlloc && state.copyAttempts == 2 && state.successfulCopies == 1 && state.refusedCopies == 1,
           "pop-copy-refusal-has-exact-partial-copy-counts");
    Expect(entries.Identity() == rootIdentity && captured.Identity() == rootIdentity,
           "pop-copy-refusal-preserves-live-and-captured-root-identity");
    Expect(RefusalValuesAre(entries, kValues, std::size(kValues)) &&
               RefusalValuesAre(captured, kValues, std::size(kValues)),
           "pop-copy-refusal-preserves-live-and-captured-contents");
    Expect(OriginalWitnessesHaveSingleOwner(witnesses, std::size(witnesses)),
           "pop-copy-refusal-restores-original-handle-ownership");
}

void TestEraseCopyRefusalPreservesRootsAndOwnership()
{
    constexpr int kValues[]{101, 103, 107, 109};
    RefusalState state;
    std::weak_ptr<int> witnesses[std::size(kValues)];
    RefusalEntries entries;
    FillRefusalEntries(state, entries, witnesses, kValues, std::size(kValues));
    const RefusalEntries captured = entries;
    const void* const rootIdentity = entries.Identity();

    bool caughtBadAlloc = false;
    try
    {
        entries.erase(entries.begin() + 1, entries.begin() + 3);
    }
    catch (const std::bad_alloc&)
    {
        caughtBadAlloc = true;
    }

    Expect(caughtBadAlloc && state.copyAttempts == 2 && state.successfulCopies == 1 && state.refusedCopies == 1,
           "erase-copy-refusal-has-exact-partial-copy-counts");
    Expect(entries.Identity() == rootIdentity && captured.Identity() == rootIdentity,
           "erase-copy-refusal-preserves-live-and-captured-root-identity");
    Expect(RefusalValuesAre(entries, kValues, std::size(kValues)) &&
               RefusalValuesAre(captured, kValues, std::size(kValues)),
           "erase-copy-refusal-preserves-live-and-captured-contents");
    Expect(OriginalWitnessesHaveSingleOwner(witnesses, std::size(witnesses)),
           "erase-copy-refusal-restores-original-handle-ownership");
}
} // namespace

int RunHistoryEntriesTests()
{
    gFailures = 0;
    TestOwnershipCacheEmptyCopyAndQueries();
    TestOwnershipCacheMutationAndRootSnapshots();
    TestOwnershipCachePopAndEraseRestoreCompleteBounds();
    TestOwnershipCacheRefusesInvalidChildrenAndOverflow();
    TestEmptySharingAndReads();
    TestPopEraseAndNoOpErase();
    TestClearSwapAndMoves();
    TestRootOwnsHandleWitness();
    TestConcurrentReadersAndPrivateWriter();
    TestPushCopyRefusalPreservesRootsAndOwnership();
    TestPopCopyRefusalPreservesRootsAndOwnership();
    TestEraseCopyRefusalPreservesRootsAndOwnership();
    return gFailures;
}
