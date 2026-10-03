#include "RecordRootTests.h"

#include "RecordRoot.h"

#include <array>
#include <atomic>
#include <cstdio>
#include <initializer_list>
#include <latch>
#include <limits>
#include <memory>
#include <new>
#include <stop_token>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>

namespace
{
int gFailures = 0;
int gChecks = 0;

void Expect(bool condition, const char* name) noexcept
{
    ++gChecks;
    std::printf("%s document-record-root.%s\n", condition ? "PASS" : "FAIL", name);
    if (!condition)
        ++gFailures;
}

struct Child final
{
    int value = 0;
    std::string name;
    std::thread::id* destroyedOn = nullptr;

    Child(int childValue, std::string childName, std::thread::id* thread = nullptr)
        : value(childValue), name(std::move(childName)), destroyedOn(thread)
    {
    }

    ~Child()
    {
        if (destroyedOn != nullptr)
            *destroyedOn = std::this_thread::get_id();
    }
};

struct CopyControl final
{
    inline static std::atomic<int> copies{0};
    inline static std::atomic<int> throwOnCopy{0};
    inline static std::atomic<int> cancelOnCopy{0};
    inline static std::atomic<CancelSource*> cancellation{nullptr};

    static void Reset() noexcept
    {
        copies.store(0, std::memory_order_relaxed);
        throwOnCopy.store(0, std::memory_order_relaxed);
        cancelOnCopy.store(0, std::memory_order_relaxed);
        cancellation.store(nullptr, std::memory_order_relaxed);
    }

    static void Copied()
    {
        const int count = copies.fetch_add(1, std::memory_order_relaxed) + 1;
        if (count == throwOnCopy.load(std::memory_order_relaxed))
            throw std::bad_alloc{};
        if (count == cancelOnCopy.load(std::memory_order_relaxed))
        {
            if (CancelSource* source = cancellation.load(std::memory_order_relaxed); source != nullptr)
                (void)source->Cancel();
        }
    }
};

struct Record final
{
    int value = 0;
    std::string name;
    std::shared_ptr<const Child> child;

    Record(int recordValue, std::string recordName, std::shared_ptr<const Child> recordChild = {})
        : value(recordValue), name(std::move(recordName)), child(std::move(recordChild))
    {
    }

    Record(const Record& other) : value(other.value), name(other.name), child(other.child)
    {
        CopyControl::Copied();
    }
    Record& operator=(const Record&) = default;
    [[nodiscard]] bool operator==(const Record&) const noexcept = default;
    Record(Record&& other) noexcept
        : value(std::exchange(other.value, -1)), name(std::move(other.name)), child(std::move(other.child))
    {
    }
    Record& operator=(Record&& other) noexcept
    {
        if (this != &other)
        {
            value = std::exchange(other.value, -1);
            name = std::move(other.name);
            child = std::move(other.child);
        }
        return *this;
    }
};

using Root = RecordRoot<Record>;

struct AllocationProbe final
{
    std::atomic<std::size_t> attempts{0};
    std::atomic<std::size_t> successes{0};
    std::atomic<std::size_t> releases{0};
    std::atomic<std::size_t> failAt{0};
    std::atomic<std::size_t> failures{0};
    std::atomic<std::size_t> bufferAttempts{0};
    std::atomic<std::size_t> failBufferAt{0};
};

template <typename T> struct FaultAllocator final
{
    using value_type = T;
    using propagate_on_container_move_assignment = std::true_type;
    using is_always_equal = std::false_type;
    AllocationProbe* probe = nullptr;

    FaultAllocator() noexcept = default;
    explicit FaultAllocator(AllocationProbe* value) noexcept : probe(value) {}
    template <typename U> FaultAllocator(const FaultAllocator<U>& other) noexcept : probe(other.probe) {}

    [[nodiscard]] T* allocate(std::size_t count)
    {
        const auto attempt = probe ? probe->attempts.fetch_add(1) + 1 : 0;
        bool failBuffer = false;
        if constexpr (std::is_same_v<T, Record>)
            failBuffer = probe && probe->bufferAttempts.fetch_add(1) + 1 == probe->failBufferAt.load();
        if (probe && (failBuffer || attempt == probe->failAt.load()))
        {
            probe->failures.fetch_add(1);
            throw std::bad_alloc{};
        }
        auto* result = std::allocator<T>{}.allocate(count);
        if (probe)
            probe->successes.fetch_add(1);
        return result;
    }
    void deallocate(T* values, std::size_t count) noexcept
    {
        std::allocator<T>{}.deallocate(values, count);
        if (probe)
            probe->releases.fetch_add(1);
    }
    template <typename U> [[nodiscard]] bool operator==(const FaultAllocator<U>& other) const noexcept
    {
        return probe == other.probe;
    }
};

using FaultRoot = RecordRoot<Record, FaultAllocator<Record>>;

std::shared_ptr<const Child> MakeChild(int value, std::string name)
{
    return std::make_shared<const Child>(value, std::move(name));
}

Record MakeRecord(int value, std::string name)
{
    return Record(value, std::move(name), MakeChild(value * 10, "child-" + std::to_string(value)));
}

Root MakeRoot(std::initializer_list<std::pair<int, const char*>> values)
{
    Root::Builder builder(values.size());
    for (const auto& [value, name] : values)
    {
        Record record = MakeRecord(value, name);
        (void)builder.Append(std::move(record));
    }
    auto root = builder.Seal();
    return root.has_value() ? std::move(root.value()) : Root{};
}

bool SameRecord(const Record& record, int value, const char* name, int childValue, const char* childName)
{
    return record.value == value && record.name == name && record.child && record.child->value == childValue &&
           record.child->name == childName;
}

template <typename T>
concept MutableRootRecordRead = requires(T& root) { root.Read()[0].value = 9; };

template <typename T>
concept MutableBuilderRecordRead = requires(T& builder) { builder.Read()[0].value = 9; };

static_assert(std::is_nothrow_move_constructible_v<Record>);
static_assert(std::is_nothrow_move_assignable_v<Record>);
static_assert(std::is_same_v<decltype(std::declval<Root&>().Read()), const Root::Container&>);
static_assert(std::is_same_v<decltype(std::declval<Root&>()[0]), const Record&>);
static_assert(std::is_same_v<decltype(std::declval<Root&>().begin()), Root::const_iterator>);
static_assert(std::is_same_v<decltype(std::declval<Root::Builder&>().Read()), const Root::Container&>);
static_assert(!MutableRootRecordRead<Root>);
static_assert(!MutableBuilderRecordRead<Root::Builder>);
static_assert(noexcept(std::declval<const Root&>() == std::declval<const Root&>()));
} // namespace

int RunRecordRootTests()
{
    gFailures = 0;
    gChecks = 0;
    CopyControl::Reset();

    {
        Root empty;
        Expect(empty.empty() && empty.size() == 0 && empty.Identity() == nullptr && empty.Read().empty(),
               "default-root-is-empty");
        Root captured = MakeRoot({{4, "four"}});
        const void* const identity = captured.Identity();
        Root moved(std::move(captured));
        Expect(moved.size() == 1 && moved.Identity() == identity && captured.empty() && captured.Identity() == nullptr,
               "captured-and-moved-root-behavior");
    }

    {
        constexpr int recordCount = 10000;
        Root::Builder builder(recordCount);
        for (int index = 0; index < recordCount; ++index)
        {
            Record record(index * 3, "literal-record-" + std::to_string(index),
                          MakeChild(index * 7, "literal-child-" + std::to_string(index)));
            if (!builder.Append(std::move(record)))
                Expect(false, "large-fixture-append");
        }
        CopyControl::copies.store(0, std::memory_order_relaxed);
        auto sealed = builder.Seal();
        Expect(sealed.has_value(), "large-fixture-seals");
        Root published = sealed.has_value() ? std::move(sealed.value()) : Root{};
        CopyControl::copies.store(0, std::memory_order_relaxed);

        Root firstCapture = published;
        Root secondCapture = published;
        const Root::Container& nonConstRead = published.Read();
        const Root& constPublished = published;
        const bool literalEdges =
            nonConstRead.size() == recordCount && nonConstRead.front().name == "literal-record-0" &&
            nonConstRead[recordCount / 2].value == 15000 && nonConstRead.back().child->name == "literal-child-9999" &&
            constPublished.size() == recordCount &&
            SameRecord(constPublished[0], 0, "literal-record-0", 0, "literal-child-0") &&
            SameRecord(constPublished[recordCount / 2], 15000, "literal-record-5000", 35000, "literal-child-5000") &&
            SameRecord(constPublished[recordCount - 1], 29997, "literal-record-9999", 69993, "literal-child-9999");
        std::atomic<bool> readerOkay{false};
        std::thread reader(
            [capture = std::move(secondCapture), &readerOkay]() mutable
            {
                const auto& records = capture.Read();
                readerOkay.store(
                    records.size() == 10000 && records.front().name == "literal-record-0" &&
                        records[5000].value == 15000 && records[5000].child->name == "literal-child-5000" &&
                        records.back().name == "literal-record-9999" && records.back().child->value == 69993,
                    std::memory_order_relaxed);
            });
        reader.join();
        Expect(firstCapture.Identity() == published.Identity() && literalEdges &&
                   readerOkay.load(std::memory_order_relaxed),
               "10000-captures-and-concurrent-reads-preserve-literal-content");
        Expect(CopyControl::copies.load(std::memory_order_relaxed) == 0, "10000-captures-and-reads-copy-no-records");
    }

    {
        Root published = MakeRoot({{1, "original-a"}, {2, "original-b"}});
        const void* const originalIdentity = published.Identity();
        auto siblingA = Root::Builder::Clone(published, 3);
        auto siblingB = Root::Builder::Clone(published, 3);
        const bool buildersReady = siblingA.has_value() && siblingB.has_value();
        bool siblingSeals = false;
        if (buildersReady)
        {
            (void)siblingA.value().Append(MakeRecord(3, "sibling-a"));
            (void)siblingB.value().Replace(0, MakeRecord(8, "sibling-b"));
            auto rootA = siblingA.value().Seal();
            auto rootB = siblingB.value().Seal();
            siblingSeals = rootA.has_value() && rootB.has_value() && rootA.value().size() == 3 &&
                           rootB.value().size() == 2 && (rootA.value())[0].name == "original-a" &&
                           (rootA.value())[2].name == "sibling-a" && (rootB.value())[0].name == "sibling-b" &&
                           (rootB.value())[1].name == "original-b";
        }
        Expect(buildersReady && siblingSeals && published.Identity() == originalIdentity && published.size() == 2 &&
                   published[0].name == "original-a" && published[1].name == "original-b",
               "sibling-builders-leave-sole-published-root-unchanged");
    }

    {
        Root::Builder builder(1);
        Record first = MakeRecord(1, "first");
        Record refused = MakeRecord(2, "must-remain-unmoved");
        const bool appended = builder.Append(std::move(first));
        const bool refusedAppend = builder.Append(std::move(refused));
        Expect(appended && !refusedAppend && refused.value == 2 && refused.name == "must-remain-unmoved" &&
                   refused.child && refused.child->value == 20,
               "capacity-refusal-precedes-rvalue-mutation");
    }

    {
        Root::Builder builder(6);
        (void)builder.Append(MakeRecord(1, "one"));
        (void)builder.Append(MakeRecord(3, "three"));
        const bool inserted = builder.Insert(1, MakeRecord(2, "two"));
        const bool replaced = builder.Replace(2, MakeRecord(30, "thirty"));
        const bool erased = builder.Erase(0);
        constexpr std::array<std::pair<int, const char*>, 2> expected{{{2, "two"}, {30, "thirty"}}};
        bool matches = builder.Read().size() == expected.size();
        for (std::size_t i = 0; matches && i < expected.size(); ++i)
            matches = builder.Read()[i].value == expected[i].first && builder.Read()[i].name == expected[i].second;
        Expect(inserted && replaced && erased && matches, "insert-replace-erase-match-independent-sequence");
    }

    {
        Root::Builder source(2);
        (void)source.Append(MakeRecord(1, "one"));
        Root::Builder moved(std::move(source));
        Record rejected = MakeRecord(9, "moved-builder-refusal");
        const bool movedRefuses = !source.CanSeal() && !source.Append(std::move(rejected)) && rejected.value == 9 &&
                                  rejected.name == "moved-builder-refusal";
        auto sealed = moved.Seal();
        Record afterSeal = MakeRecord(10, "consumed-builder-refusal");
        const bool consumedRefuses = sealed.has_value() && !moved.CanSeal() && !moved.Append(std::move(afterSeal)) &&
                                     afterSeal.value == 10 && afterSeal.name == "consumed-builder-refusal" &&
                                     !moved.Seal().has_value();
        Expect(movedRefuses && consumedRefuses, "moved-and-consumed-builders-refuse-and-seal-once");
    }

    {
        Root::Builder builder(2);
        (void)builder.Append(MakeRecord(1, "one"));
        const bool invalidOps = !builder.Insert(2, MakeRecord(2, "bad-insert")) &&
                                !builder.Replace(1, MakeRecord(3, "bad-replace")) && !builder.Erase(1) &&
                                !builder.Insert(99, MakeRecord(4, "far-insert"));
        Expect(invalidOps && builder.Read().size() == 1 && builder.Read()[0].name == "one",
               "invalid-builder-indices-refuse-without-changing-records");
    }

    {
        Root source = MakeRoot({{1, "before-a"}, {2, "before-b"}, {3, "before-c"}, {4, "before-d"}});
        CopyControl::Reset();
        CancelSource alreadyCancelled;
        (void)alreadyCancelled.Cancel();
        auto beforeCopy = Root::Builder::Clone(source, 4, alreadyCancelled.Token());
        Expect(!beforeCopy.has_value() && CopyControl::copies.load(std::memory_order_relaxed) == 0,
               "clone-cancellation-before-copy");

        CopyControl::Reset();
        CancelSource nthCopy;
        CopyControl::cancelOnCopy.store(2, std::memory_order_relaxed);
        CopyControl::cancellation.store(&nthCopy, std::memory_order_relaxed);
        auto duringCopy = Root::Builder::Clone(source, 4, nthCopy.Token());
        Expect(!duringCopy.has_value() && CopyControl::copies.load(std::memory_order_relaxed) == 2,
               "clone-cancellation-at-reached-nth-copy");

        CopyControl::Reset();
        CancelSource finalCopy;
        CopyControl::cancelOnCopy.store(4, std::memory_order_relaxed);
        CopyControl::cancellation.store(&finalCopy, std::memory_order_relaxed);
        auto atFinalCopy = Root::Builder::Clone(source, 4, finalCopy.Token());
        Expect(!atFinalCopy.has_value() && CopyControl::copies.load(std::memory_order_relaxed) == 4,
               "clone-cancellation-at-reached-final-copy");
        Expect(source.size() == 4 && source[0].name == "before-a" && source[3].name == "before-d",
               "cancelled-clones-preserve-source-root");
        CopyControl::Reset();
    }

    {
        Root source = MakeRoot({{11, "fault-old-a"}, {12, "fault-old-b"}, {13, "fault-old-c"}});
        const void* const identity = source.Identity();
        CopyControl::Reset();
        CopyControl::throwOnCopy.store(2, std::memory_order_relaxed);
        bool reachedBadAlloc = false;
        try
        {
            auto failedClone = Root::Builder::Clone(source, 3);
            (void)failedClone;
        }
        catch (const std::bad_alloc&)
        {
            reachedBadAlloc = true;
        }
        const bool preserved = CopyControl::copies.load() == 2 && source.Identity() == identity && source.size() == 3 &&
                               source[0].name == "fault-old-a" && source[1].value == 12 &&
                               source[2].name == "fault-old-c";
        CopyControl::Reset();
        auto retry = Root::Builder::Clone(source, 3);
        auto retriedRoot = retry.has_value() ? retry.value().Seal() : std::optional<Root>{};
        Expect(reachedBadAlloc && preserved && retriedRoot.has_value() && retriedRoot.value().size() == 3 &&
                   (retriedRoot.value())[1].name == "fault-old-b",
               "reached-bad-alloc-preserves-published-root-and-fresh-retry-succeeds");
        CopyControl::Reset();
    }

    {
        std::thread::id retirementThread;
        std::thread::id observedRetirementThread;
        std::weak_ptr<const Child> childOwner;
        Root::Builder builder(1);
        auto child = std::make_shared<const Child>(71, "retired-child", &retirementThread);
        childOwner = child;
        Record record(7, "retired-record", child);
        child.reset();
        (void)builder.Append(std::move(record));
        auto sealed = builder.Seal();
        Root root = sealed.has_value() ? std::move(sealed.value()) : Root{};
        const bool retainedByRoot = !childOwner.expired() && root.size() == 1 && root[0].child->name == "retired-child";
        const std::thread::id expectedThread = std::this_thread::get_id();
        std::thread retirement(
            [root = std::move(root), &observedRetirementThread]() mutable
            {
                observedRetirementThread = std::this_thread::get_id();
                root = Root{};
            });
        retirement.join();
        Expect(retainedByRoot && childOwner.expired() && retirementThread == observedRetirementThread &&
                   retirementThread != expectedThread,
               "child-weak-owner-lives-through-root-and-final-release-runs-on-retirement-thread");
    }

    {
        AllocationProbe probe;
        // Debug STL proxy construction is noexcept. Inject into the actual record buffer reserve instead;
        // proxy allocations remain counted and must still be released after refusal/retirement.
        probe.failBufferAt.store(1);
        bool refused = false;
        try
        {
            FaultRoot::Builder builder(2, FaultAllocator<Record>{&probe});
            (void)builder.CanSeal();
        }
        catch (const std::bad_alloc&)
        {
            refused = true;
        }
        Expect(refused && probe.failures.load() == 1 && probe.bufferAttempts.load() == 1 &&
                   probe.successes.load() == probe.releases.load(),
               "constructor-allocation-fault-reached-before-any-record-owner");
        probe.failBufferAt.store(0);
        {
            FaultRoot::Builder retry(2, FaultAllocator<Record>{&probe});
            const bool appended = retry.Append(MakeRecord(42, "constructor-retry"));
            auto root = retry.Seal();
            Expect(appended && root && root.value().size() == 1 && (root.value())[0].name == "constructor-retry",
                   "constructor-allocation-refusal-has-fresh-successful-retry");
        }
        Expect(probe.successes.load() > 0 && probe.releases.load() == probe.successes.load(),
               "constructor-retry-vector-and-root-allocations-fully-released");
    }

    {
        AllocationProbe probe;
        {
            FaultRoot::Builder builder(2, FaultAllocator<Record>{&probe});
            (void)builder.Append(MakeRecord(12, "seal-before-a"));
            (void)builder.Append(MakeRecord(13, "seal-before-b"));
            const auto initialAllocations = probe.attempts.load();
            probe.failAt.store(initialAllocations + 1);
            bool refused = false;
            try
            {
                auto root = builder.Seal();
                (void)root;
            }
            catch (const std::bad_alloc&)
            {
                refused = true;
            }
            Expect(refused && probe.failures.load() == 1 && probe.attempts.load() == initialAllocations + 1 &&
                       builder.CanSeal() && builder.Limit() == 2 && builder.Read().size() == 2 &&
                       builder.Read()[0].name == "seal-before-a" && builder.Read()[1].child->value == 130,
                   "seal-root-allocation-fault-preserves-complete-retryable-builder");
            probe.failAt.store(0);
            auto root = builder.Seal();
            Expect(root && root.value().size() == 2 && (root.value())[0].name == "seal-before-a" &&
                       (root.value())[1].name == "seal-before-b" && !builder.CanSeal() && !builder.Seal(),
                   "seal-allocation-retry-publishes-exact-records-once");
            const auto allocatedBeforeCapture = probe.attempts.load();
            probe.failAt.store(allocatedBeforeCapture + 1);
            CopyControl::Reset();
            FaultRoot capture = root.value();
            Expect(capture.Identity() == root.value().Identity() && capture.Read()[0].name == "seal-before-a" &&
                       CopyControl::copies.load() == 0 && probe.attempts.load() == allocatedBeforeCapture,
                   "capture-and-nonconst-read-do-not-call-record-or-root-allocator");
            capture = FaultRoot{};
            probe.failAt.store(0);
            root.reset();
        }
        Expect(probe.releases.load() == probe.successes.load(),
               "seal-retry-releases-exact-root-and-record-buffer-allocations");
    }

    {
        AllocationProbe sourceProbe;
        AllocationProbe cloneProbe;
        {
            FaultRoot::Builder builder(1, FaultAllocator<Record>{&sourceProbe});
            (void)builder.Append(MakeRecord(63, "separate-allocation-source"));
            auto root = builder.Seal();
            const auto sourceAttempts = sourceProbe.attempts.load();
            cloneProbe.failBufferAt.store(1);
            CopyControl::Reset();
            auto tooSmall = FaultRoot::Builder::Clone(root.value(), 0, {}, FaultAllocator<Record>{&cloneProbe});
            Expect(!tooSmall && cloneProbe.attempts.load() == 0 && CopyControl::copies.load() == 0,
                   "clone-output-limit-refuses-before-allocation-or-record-copy");
            bool refused = false;
            try
            {
                auto clone = FaultRoot::Builder::Clone(root.value(), 1, {}, FaultAllocator<Record>{&cloneProbe});
                (void)clone;
            }
            catch (const std::bad_alloc&)
            {
                refused = true;
            }
            Expect(refused && cloneProbe.failures.load() == 1 && CopyControl::copies.load() == 0 &&
                       sourceProbe.attempts.load() == sourceAttempts && root.value().size() == 1 &&
                       (root.value())[0].name == "separate-allocation-source",
                   "clone-uses-new-allocation-owner-and-refusal-preserves-old-root");
            cloneProbe.failBufferAt.store(0);
            CopyControl::Reset();
            CopyControl::throwOnCopy.store(1);
            bool copyRefused = false;
            try
            {
                auto clone = FaultRoot::Builder::Clone(root.value(), 1, {}, FaultAllocator<Record>{&cloneProbe});
                (void)clone;
            }
            catch (const std::bad_alloc&)
            {
                copyRefused = true;
            }
            Expect(copyRefused && CopyControl::copies.load() == 1 &&
                       cloneProbe.successes.load() == cloneProbe.releases.load() &&
                       (root.value())[0].child->value == 630 && sourceProbe.attempts.load() == sourceAttempts,
                   "clone-copy-fault-retires-new-buffer-and-preserves-old-allocation-owner");
            CopyControl::Reset();
            auto clone = FaultRoot::Builder::Clone(root.value(), 1, {}, FaultAllocator<Record>{&cloneProbe});
            auto next = clone.value().Seal();
            Expect(next && next.value().size() == 1 && (next.value())[0].value == 63 &&
                       (next.value())[0].child->value == 630,
                   "clone-new-allocation-owner-fresh-retry-has-literal-content");
            next.reset();
            clone.reset();
            root.reset();
        }
        Expect(sourceProbe.releases.load() == sourceProbe.successes.load() &&
                   cloneProbe.releases.load() == cloneProbe.successes.load(),
               "clone-old-and-new-allocation-owners-retire-independently");
    }

    {
        Root original = MakeRoot({{17, "concurrent-old-a"}, {19, "concurrent-old-b"}});
        std::latch readerStarted(1);
        std::latch writerPublished(1);
        bool readerOkay = false;
        std::thread reader(
            [capture = original, &readerStarted, &writerPublished, &readerOkay]()
            {
                readerStarted.count_down();
                writerPublished.wait();
                readerOkay = capture.size() == 2 && capture[0].value == 17 && capture[1].name == "concurrent-old-b" &&
                             capture[1].child->value == 190;
            });
        readerStarted.wait();
        auto builder = Root::Builder::Clone(original, 2);
        (void)builder.value().Replace(0, MakeRecord(23, "concurrent-new-a"));
        auto next = builder.value().Seal();
        writerPublished.count_down();
        reader.join();
        Expect(readerOkay && next && (next.value())[0].value == 23 && (next.value())[1].name == "concurrent-old-b" &&
                   original[0].name == "concurrent-old-a" && original.Identity() != next.value().Identity(),
               "event-ordered-reader-keeps-old-root-through-new-builder-publication");
    }

    {
        AllocationProbe probe;
        FaultRoot::Builder invalid((std::numeric_limits<std::size_t>::max)(), FaultAllocator<Record>{&probe});
        auto incoming = MakeRecord(88, "capacity-overflow-refusal");
        Expect(!invalid.CanSeal() && invalid.Limit() == 0 && invalid.Read().empty() &&
                   !invalid.Append(std::move(incoming)) && incoming.value == 88 && !invalid.Seal() &&
                   probe.bufferAttempts.load() == 0,
               "unrepresentable-capacity-refuses-before-record-buffer-or-input-move");
        Root empty;
        Expect(!Root::Builder::Clone(empty, (std::numeric_limits<std::size_t>::max)()),
               "unrepresentable-clone-capacity-refuses-even-for-empty-source");
    }

    {
        Root source = MakeRoot({{101, "equality-a"}, {103, "equality-b"}});
        Root captured = source;
        CopyControl::Reset();
        auto builder = Root::Builder::Clone(source, 3);
        auto equal = builder.value().Seal();
        CopyControl::Reset();
        Expect(source == captured && source == equal.value() && source.Identity() != equal.value().Identity() &&
                   CopyControl::copies.load() == 0,
               "value-equality-compares-records-across-independent-roots-without-copying");
        auto replacement = Root::Builder::Clone(source, 2);
        auto changed = MakeRecord(107, "equality-changed");
        (void)replacement.value().Replace(1, std::move(changed));
        auto different = replacement.value().Seal();
        auto shorterBuilder = Root::Builder::Clone(source, 2);
        (void)shorterBuilder.value().Erase(1);
        auto shorter = shorterBuilder.value().Seal();
        Root empty;
        Root::Builder zero(0);
        auto sealedEmpty = zero.Seal();
        Expect(source != different.value() && source != shorter.value() && empty == sealedEmpty.value(),
               "value-equality-preserves-differences-order-and-empty-sequences");
        auto reorderedBuilder = Root::Builder::Clone(source, 2);
        auto first = source[0];
        auto last = source[1];
        (void)reorderedBuilder.value().Replace(0, std::move(last));
        (void)reorderedBuilder.value().Replace(1, std::move(first));
        auto reordered = reorderedBuilder.value().Seal();
        Expect(source != reordered.value() && source[0].value == 101 && source[1].value == 103,
               "value-equality-retains-sequence-order-and-original-root");
    }

    std::printf("SUMMARY document-record-root checks=%d failures=%d\n", gChecks, gFailures);
    return gFailures;
}
