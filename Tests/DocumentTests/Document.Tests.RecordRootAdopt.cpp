#include "Document.Tests.RecordRootAdopt.h"

#include "RecordRoot.h"

#include <cstddef>
#include <cstdio>
#include <limits>
#include <memory>
#include <new>
#include <string>
#include <type_traits>
#include <utility>

namespace
{
int gFailures = 0;
int gChecks = 0;

void Expect(bool condition, const char* name) noexcept
{
    ++gChecks;
    std::printf("%s document-record-adopt.%s\n", condition ? "PASS" : "FAIL", name);
    if (!condition)
        ++gFailures;
}

struct AllocationProbe final
{
    std::size_t attempts = 0;
    std::size_t successes = 0;
    std::size_t releases = 0;
    std::size_t failAt = 0;
    std::size_t failures = 0;
    std::size_t bufferAttempts = 0;
    std::size_t failBufferAt = 0;
};

struct Record;

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
        const std::size_t attempt = probe == nullptr ? 0 : ++probe->attempts;
        bool failBuffer = false;
        if constexpr (std::is_same_v<T, Record>)
            failBuffer = probe != nullptr && ++probe->bufferAttempts == probe->failBufferAt;
        if (probe != nullptr && (failBuffer || attempt == probe->failAt))
        {
            ++probe->failures;
            throw std::bad_alloc{};
        }
        T* result = std::allocator<T>{}.allocate(count);
        if (probe != nullptr)
            ++probe->successes;
        return result;
    }
    void deallocate(T* values, std::size_t count) noexcept
    {
        std::allocator<T>{}.deallocate(values, count);
        if (probe != nullptr)
            ++probe->releases;
    }
    template <typename U> [[nodiscard]] bool operator==(const FaultAllocator<U>& other) const noexcept
    {
        return probe == other.probe;
    }
};

struct Child final
{
    int value = 0;
};

struct Record final
{
    inline static int copies = 0;
    inline static int moves = 0;

    int value = 0;
    std::shared_ptr<const Child> child;
    std::shared_ptr<const std::string> name;

    Record(int recordValue, std::shared_ptr<const Child> recordChild, std::shared_ptr<const std::string> recordName)
        : value(recordValue), child(std::move(recordChild)), name(std::move(recordName))
    {
    }
    Record(const Record& other) : value(other.value), child(other.child), name(other.name)
    {
        ++copies;
    }
    Record& operator=(const Record&) = default;
    Record(Record&& other) noexcept
        : value(std::exchange(other.value, -1)), child(std::move(other.child)), name(std::move(other.name))
    {
        ++moves;
    }
    Record& operator=(Record&& other) noexcept
    {
        if (this != &other)
        {
            value = std::exchange(other.value, -1);
            child = std::move(other.child);
            name = std::move(other.name);
            ++moves;
        }
        return *this;
    }
};

using Root = RecordRoot<Record, FaultAllocator<Record>>;

Record MakeRecord(int value, const char* name)
{
    return Record(value, std::make_shared<const Child>(Child{value * 10}), std::make_shared<const std::string>(name));
}

bool Balanced(const AllocationProbe& probe) noexcept
{
    return probe.successes == probe.releases;
}

template <typename T>
concept MutableRootRead = requires(T& root) { root.Read()[0].value = 9; };

template <typename T>
concept MutableBuilderRead = requires(T& builder) { builder.Read()[0].value = 9; };

static_assert(!MutableRootRead<Root>);
static_assert(!MutableBuilderRead<Root::Builder>);
static_assert(std::is_same_v<decltype(std::declval<Root&>().Read()), const Root::Container&>);
static_assert(std::is_same_v<decltype(std::declval<Root::Builder&>().Read()), const Root::Container&>);
static_assert(std::is_same_v<decltype(std::declval<Root&>()[0]), const Record&>);
} // namespace

int RunRecordRootAdoptTests()
{
    gFailures = 0;
    gChecks = 0;

    {
        AllocationProbe probe;
        bool adoptedOkay = false;
        bool valuesAndHandlesOkay = false;
        bool appendAndSealOkay = false;
        {
            Root::Container records{FaultAllocator<Record>{&probe}};
            records.reserve(3);
            records.emplace_back(17, std::make_shared<const Child>(Child{170}),
                                 std::make_shared<const std::string>("adopt-a"));
            records.emplace_back(29, std::make_shared<const Child>(Child{290}),
                                 std::make_shared<const std::string>("adopt-b"));
            records.emplace_back(43, std::make_shared<const Child>(Child{430}),
                                 std::make_shared<const std::string>("adopt-c"));
            const void* const sourceAddress = records.data();
            const std::size_t capacity = records.capacity();
            const auto childA = records[0].child;
            const auto nameA = records[0].name;
            const auto childC = records[2].child;
            const auto nameC = records[2].name;
            Record::copies = 0;
            Record::moves = 0;
            const auto buffersBefore = probe.bufferAttempts;

            // No mutable borrow of records or its elements remains live here.
            auto adopted = Root::Builder::Adopt(std::move(records), capacity);
            adoptedOkay = adopted.has_value() && adopted.value().CanSeal() && adopted.value().Limit() == capacity &&
                          adopted.value().Read().data() == sourceAddress && records.empty() && Record::copies == 0 &&
                          Record::moves == 0 && probe.bufferAttempts == buffersBefore &&
                          adopted.value().Read().get_allocator().probe == &probe;
            if (adopted.has_value())
            {
                const auto& view = adopted.value().Read();
                valuesAndHandlesOkay = view.size() == 3 && view.capacity() == capacity && view[0].value == 17 &&
                                       view[1].value == 29 && view[2].value == 43 && view[0].child == childA &&
                                       view[0].name == nameA && view[0].child->value == 170 &&
                                       *view[0].name == "adopt-a" && view[2].child == childC && view[2].name == nameC &&
                                       view[2].child->value == 430 && *view[2].name == "adopt-c";
                Record rejected = MakeRecord(61, "full");
                const bool appendRefused = !adopted.value().Append(std::move(rejected)) && rejected.value == 61 &&
                                           rejected.name && *rejected.name == "full";
                auto sealed = adopted.value().Seal();
                Record afterSeal = MakeRecord(71, "consumed");
                appendAndSealOkay = appendRefused && sealed.has_value() && sealed.value().size() == 3 &&
                                    sealed.value()[0].value == 17 && !adopted.value().CanSeal() &&
                                    !adopted.value().Seal().has_value() &&
                                    !adopted.value().Append(std::move(afterSeal)) && afterSeal.value == 71 &&
                                    afterSeal.name && *afterSeal.name == "consumed";
            }
        }
        Expect(adoptedOkay, "sufficient-capacity-transfers-buffer-without-record-copy-or-move");
        Expect(valuesAndHandlesOkay, "ordered-values-and-held-immutable-child-name-handles-survive");
        Expect(appendAndSealOkay, "full-and-consumed-builder-refuse-rvalue-without-consuming-it");
        Expect(Balanced(probe), "adopted-and-consumed-builder-allocations-release-at-scope-exit");
    }

    {
        AllocationProbe sizeProbe;
        bool refused = false;
        bool preserved = false;
        {
            Root::Container records{FaultAllocator<Record>{&sizeProbe}};
            records.reserve(3);
            records.emplace_back(3, std::make_shared<const Child>(Child{30}),
                                 std::make_shared<const std::string>("size-refusal"));
            records.emplace_back(5, std::make_shared<const Child>(Child{50}),
                                 std::make_shared<const std::string>("size-refusal-b"));
            records.emplace_back(7, std::make_shared<const Child>(Child{70}),
                                 std::make_shared<const std::string>("size-refusal-c"));
            const void* const address = records.data();
            const std::size_t beforeAttempts = sizeProbe.attempts;
            auto result = Root::Builder::Adopt(std::move(records), 2);
            refused = !result.has_value();
            preserved = records.size() == 3 && records.data() == address && records[0].value == 3 &&
                        records[1].value == 5 && records[2].value == 7 && sizeProbe.attempts == beforeAttempts;
        }
        Expect(refused && preserved, "size-over-limit-refusal-preserves-source-buffer-and-owner");
        Expect(Balanced(sizeProbe), "size-refusal-source-allocation-releases-once");
    }

    {
        AllocationProbe capacityProbe;
        bool refused = false;
        bool preserved = false;
        {
            Root::Container records{FaultAllocator<Record>{&capacityProbe}};
            records.reserve(4);
            records.emplace_back(11, std::make_shared<const Child>(Child{110}),
                                 std::make_shared<const std::string>("capacity-refusal"));
            const void* const address = records.data();
            const std::size_t beforeAttempts = capacityProbe.attempts;
            auto result = Root::Builder::Adopt(std::move(records), 3);
            refused = !result.has_value();
            preserved = records.size() == 1 && records.capacity() > 3 && records.data() == address &&
                        records[0].value == 11 && capacityProbe.attempts == beforeAttempts;
        }
        Expect(refused && preserved, "capacity-over-limit-refusal-preserves-source-buffer-and-owner");
        Expect(Balanced(capacityProbe), "capacity-refusal-source-allocation-releases-once");
    }

    {
        AllocationProbe limitProbe;
        bool refused = false;
        bool preserved = false;
        {
            Root::Container records{FaultAllocator<Record>{&limitProbe}};
            records.reserve(2);
            records.emplace_back(13, std::make_shared<const Child>(Child{130}),
                                 std::make_shared<const std::string>("max-size-refusal"));
            const void* const address = records.data();
            const std::size_t beforeAttempts = limitProbe.attempts;
            auto result = Root::Builder::Adopt(std::move(records), std::numeric_limits<std::size_t>::max());
            refused = !result.has_value();
            preserved = records.size() == 1 && records.data() == address && records[0].value == 13 &&
                        limitProbe.attempts == beforeAttempts;
        }
        Expect(refused && preserved, "limit-over-max-size-refusal-preserves-source-before-reserve");
        Expect(Balanced(limitProbe), "max-size-refusal-source-allocation-releases-once");
    }

    {
        AllocationProbe probe;
        bool success = false;
        bool sealedEmpty = false;
        {
            Root::Container records{FaultAllocator<Record>{&probe}};
            auto adopted = Root::Builder::Adopt(std::move(records), 0);
            success = adopted.has_value() && adopted.value().CanSeal() && adopted.value().Limit() == 0 &&
                      adopted.value().Read().empty();
            if (adopted.has_value())
            {
                auto sealed = adopted.value().Seal();
                sealedEmpty = sealed.has_value() && sealed.value().empty() && !adopted.value().CanSeal();
            }
        }
        Expect(success && sealedEmpty, "empty-source-adopts-and-seals-at-zero-limit");
        Expect(Balanced(probe), "empty-zero-limit-root-allocation-releases-at-scope-exit");
    }

    {
        AllocationProbe probe;
        bool caught = false;
        bool preserved = false;
        bool reserveRetry = false;
        {
            Root::Container records{FaultAllocator<Record>{&probe}};
            records.reserve(2);
            records.emplace_back(19, std::make_shared<const Child>(Child{190}),
                                 std::make_shared<const std::string>("reserve-failure"));
            records.emplace_back(23, std::make_shared<const Child>(Child{230}),
                                 std::make_shared<const std::string>("reserve-failure-b"));
            const void* const address = records.data();
            const std::size_t oldCapacity = records.capacity();
            const std::size_t beforeAttempts = probe.attempts;
            probe.failBufferAt = probe.bufferAttempts + 1;
            try
            {
                auto result = Root::Builder::Adopt(std::move(records), oldCapacity + 1);
                (void)result;
            }
            catch (const std::bad_alloc&)
            {
                caught = true;
            }
            preserved = probe.failures == 1 && probe.bufferAttempts == 2 && probe.attempts == beforeAttempts + 1 &&
                        records.size() == 2 && records.capacity() == oldCapacity && records.data() == address &&
                        records[0].value == 19 && records[1].value == 23;
            probe.failBufferAt = 0;
            Record::copies = 0;
            Record::moves = 0;
            auto retry = Root::Builder::Adopt(std::move(records), oldCapacity + 1);
            if (retry)
            {
                const void* reservedAddress = retry.value().Read().data();
                auto root = retry.value().Seal();
                reserveRetry = root && records.empty() && root.value().size() == 2 &&
                               root.value().capacity() == oldCapacity + 1 &&
                               root.value().Read().data() == reservedAddress && root.value()[0].value == 19 &&
                               root.value()[1].value == 23 && *root.value()[0].name == "reserve-failure" &&
                               *root.value()[1].name == "reserve-failure-b" && Record::copies == 0 &&
                               Record::moves == 2 && root.value().Read().get_allocator().probe == &probe;
            }
        }
        Expect(caught && preserved, "reached-reserve-bad-alloc-preserves-source-address-and-values");
        Expect(reserveRetry, "reserve-retry-moves-each-record-once-and-seals-the-owned-buffer");
        Expect(Balanced(probe), "reserve-failure-source-allocation-releases-once");
    }

    {
        AllocationProbe probe;
        bool failedSealPreserved = false;
        bool retryPublished = false;
        {
            Root::Container records{FaultAllocator<Record>{&probe}};
            records.reserve(2);
            records.emplace_back(31, std::make_shared<const Child>(Child{310}),
                                 std::make_shared<const std::string>("seal-retry-a"));
            records.emplace_back(37, std::make_shared<const Child>(Child{370}),
                                 std::make_shared<const std::string>("seal-retry-b"));
            const void* const address = records.data();
            const std::size_t capacity = records.capacity();
            auto adopted = Root::Builder::Adopt(std::move(records), capacity);
            if (adopted.has_value())
            {
                const std::size_t beforeAttempts = probe.attempts;
                probe.failAt = beforeAttempts + 1;
                bool caught = false;
                try
                {
                    auto root = adopted.value().Seal();
                    (void)root;
                }
                catch (const std::bad_alloc&)
                {
                    caught = true;
                }
                failedSealPreserved =
                    caught && probe.failures == 1 && adopted.value().CanSeal() && adopted.value().Limit() == capacity &&
                    adopted.value().Read().data() == address && adopted.value().Read().size() == 2 &&
                    adopted.value().Read()[0].value == 31 && *adopted.value().Read()[0].name == "seal-retry-a" &&
                    adopted.value().Read()[1].value == 37 && *adopted.value().Read()[1].name == "seal-retry-b";
                probe.failAt = 0;
                auto retry = adopted.value().Seal();
                retryPublished = retry.has_value() && retry.value().size() == 2 && retry.value()[0].value == 31 &&
                                 *retry.value()[0].name == "seal-retry-a" && retry.value()[1].value == 37 &&
                                 *retry.value()[1].name == "seal-retry-b" && !adopted.value().CanSeal();
            }
        }
        Expect(failedSealPreserved, "reached-root-seal-bad-alloc-keeps-adopted-builder-retryable");
        Expect(retryPublished, "retry-after-seal-allocation-failure-publishes-complete-order");
        Expect(Balanced(probe), "adopted-seal-retry-and-consumed-builder-allocations-balance");
    }

    std::printf("SUMMARY document-record-adopt checks=%d failures=%d\n", gChecks, gFailures);
    return gFailures;
}
