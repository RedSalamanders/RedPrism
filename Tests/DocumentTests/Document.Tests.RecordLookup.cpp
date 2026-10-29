#include "Document.Tests.RecordLookup.h"

#include "RecordRoot.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <memory>
#include <new>
#include <optional>
#include <type_traits>
#include <utility>

namespace
{
int gFailures = 0;
int gAssertions = 0;
int gAdditionalAssertions = 0;

void Expect(bool condition, const char* name) noexcept
{
    ++gAssertions;
    std::printf("%s record-lookup.%s\n", condition ? "PASS" : "FAIL", name);
    gFailures += condition ? 0 : 1;
}

void ExpectAdditional(bool condition, const char* name) noexcept
{
    ++gAdditionalAssertions;
    std::printf("%s record-lookup.%s\n", condition ? "PASS" : "FAIL", name);
    gFailures += condition ? 0 : 1;
}

struct LookupKey final
{
    std::uint64_t value = 0;
    [[nodiscard]] bool operator<(const LookupKey& other) const noexcept;
    [[nodiscard]] bool operator==(const LookupKey&) const noexcept = default;
};

struct LookupControl final
{
    inline static std::atomic<std::uint64_t> comparisons{0};
    inline static std::atomic<std::uint64_t> keyCalls{0};
    inline static std::atomic<std::uint64_t> cancelAtKeyCall{0};
    inline static std::atomic<CancelSource*> cancellation{nullptr};

    static void Reset() noexcept
    {
        comparisons.store(0, std::memory_order_relaxed);
        keyCalls.store(0, std::memory_order_relaxed);
        cancelAtKeyCall.store(0, std::memory_order_relaxed);
        cancellation.store(nullptr, std::memory_order_relaxed);
    }
};

bool LookupKey::operator<(const LookupKey& other) const noexcept
{
    LookupControl::comparisons.fetch_add(1, std::memory_order_relaxed);
    return value < other.value;
}

struct LookupRecord final
{
    LookupKey key;
    std::uint64_t payload = 0;
    std::uint64_t metadata = 0;
    std::uint64_t ownership = 0;
    std::uint64_t nodes = 0;
    std::uint64_t imagePixels = 0;
    std::uint64_t maskPixels = 0;
    [[nodiscard]] bool operator==(const LookupRecord&) const noexcept = default;
};

using LookupEntry = std::pair<LookupKey, std::size_t>;

template <typename T> struct LookupAllocator;

enum class AllocationClass
{
    None,
    RecordBuffer,
    KeyIndex,
    DebugProxy,
    RootControl,
    Unclassified,
};

template <typename T> struct IsRootControlAllocation : std::false_type
{
};

#if defined(_MSVC_STL_VERSION)
// MSVC's non-array allocate_shared rebinds to this control-block family in Debug and Release.
// Object is opaque: this matcher neither names nor accesses RecordRoot's private Storage.
template <typename Object, typename Allocator>
struct IsRootControlAllocation<std::_Ref_count_obj_alloc3<Object, Allocator>>
    : std::is_same<Allocator, LookupAllocator<LookupRecord>>
{
};
#endif

template <typename T> [[nodiscard]] constexpr AllocationClass ClassifyAllocation() noexcept
{
    if constexpr (std::is_same_v<T, LookupRecord>)
        return AllocationClass::RecordBuffer;
    else if constexpr (std::is_same_v<T, LookupEntry>)
        return AllocationClass::KeyIndex;
#if defined(_MSVC_STL_VERSION)
    else if constexpr (std::is_same_v<T, std::_Container_proxy>)
        return AllocationClass::DebugProxy;
#endif
    else if constexpr (IsRootControlAllocation<T>::value)
        return AllocationClass::RootControl;
    else
        return AllocationClass::Unclassified;
}

// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027)
struct AllocationProbe final
{
    std::atomic<std::uint64_t> calls{0};
    std::atomic<std::uint64_t> indexBytes{0};
    std::atomic<AllocationClass> faultTarget{AllocationClass::None};
    std::atomic<std::uint64_t> recordAttempts{0};
    std::atomic<std::uint64_t> indexAttempts{0};
    std::atomic<std::uint64_t> indexSuccesses{0};
    std::atomic<std::uint64_t> proxyAttempts{0};
    std::atomic<std::uint64_t> rootAttempts{0};
    std::atomic<std::uint64_t> unclassifiedAttempts{0};
    std::atomic<std::uint64_t> indexFailures{0};
    std::atomic<std::uint64_t> rootFailures{0};
    std::atomic<std::uint64_t> keyCallsAtFailure{0};
    std::atomic<std::uint64_t> indexSuccessesAtFailure{0};

    void Reset() noexcept
    {
        calls.store(0, std::memory_order_relaxed);
        indexBytes.store(0, std::memory_order_relaxed);
        faultTarget.store(AllocationClass::None, std::memory_order_relaxed);
        recordAttempts.store(0, std::memory_order_relaxed);
        indexAttempts.store(0, std::memory_order_relaxed);
        indexSuccesses.store(0, std::memory_order_relaxed);
        proxyAttempts.store(0, std::memory_order_relaxed);
        rootAttempts.store(0, std::memory_order_relaxed);
        unclassifiedAttempts.store(0, std::memory_order_relaxed);
        indexFailures.store(0, std::memory_order_relaxed);
        rootFailures.store(0, std::memory_order_relaxed);
        keyCallsAtFailure.store(0, std::memory_order_relaxed);
        indexSuccessesAtFailure.store(0, std::memory_order_relaxed);
    }

    void BeforeAllocate(AllocationClass kind, std::size_t count)
    {
        calls.fetch_add(1, std::memory_order_relaxed);
        switch (kind)
        {
        case AllocationClass::RecordBuffer:
            recordAttempts.fetch_add(1, std::memory_order_relaxed);
            break;
        case AllocationClass::KeyIndex:
            indexAttempts.fetch_add(1, std::memory_order_relaxed);
            indexBytes.store(static_cast<std::uint64_t>(count) * sizeof(LookupEntry), std::memory_order_relaxed);
            break;
        case AllocationClass::DebugProxy:
            proxyAttempts.fetch_add(1, std::memory_order_relaxed);
            break;
        case AllocationClass::RootControl:
            rootAttempts.fetch_add(1, std::memory_order_relaxed);
            break;
        case ::AllocationClass::None:
        case ::AllocationClass::Unclassified:
        default:
            unclassifiedAttempts.fetch_add(1, std::memory_order_relaxed);
            break;
        }

        // A proxy or unknown rebind is never a fault target. The root target also requires completed preparation.
        const std::uint64_t keys = LookupControl::keyCalls.load(std::memory_order_relaxed);
        const std::uint64_t preparedIndexes = indexSuccesses.load(std::memory_order_relaxed);
        const bool rootReady = kind == AllocationClass::RootControl && count == 1 && preparedIndexes == 1 && keys == 3;
        if (kind != AllocationClass::KeyIndex && !rootReady)
            return;
        AllocationClass expected = kind;
        if (!faultTarget.compare_exchange_strong(expected, AllocationClass::None, std::memory_order_relaxed))
            return;

        keyCallsAtFailure.store(keys, std::memory_order_relaxed);
        indexSuccessesAtFailure.store(preparedIndexes, std::memory_order_relaxed);
        if (kind == AllocationClass::KeyIndex)
            indexFailures.fetch_add(1, std::memory_order_relaxed);
        else
            rootFailures.fetch_add(1, std::memory_order_relaxed);
        throw std::bad_alloc{};
    }
};
#pragma warning(pop)

template <typename T> struct LookupAllocator final
{
    using value_type = T;
    using propagate_on_container_move_assignment = std::true_type;
    using is_always_equal = std::false_type;

    AllocationProbe* probe = nullptr;

    LookupAllocator() noexcept = default;
    explicit LookupAllocator(AllocationProbe* value) noexcept : probe(value) {}
    template <typename U> LookupAllocator(const LookupAllocator<U>& other) noexcept : probe(other.probe) {}

    [[nodiscard]] T* allocate(std::size_t count)
    {
        if (probe != nullptr)
            probe->BeforeAllocate(ClassifyAllocation<T>(), count);
        T* const values = std::allocator<T>{}.allocate(count);
        if constexpr (std::is_same_v<T, LookupEntry>)
            if (probe != nullptr)
                probe->indexSuccesses.fetch_add(1, std::memory_order_relaxed);
        return values;
    }

    void deallocate(T* values, std::size_t count) noexcept
    {
        std::allocator<T>{}.deallocate(values, count);
    }

    template <typename U> [[nodiscard]] bool operator==(const LookupAllocator<U>& other) const noexcept
    {
        return probe == other.probe;
    }
};

} // namespace

template <> struct RecordLookupPolicy<LookupRecord> final
{
    static constexpr bool enabled = true;
    static constexpr std::size_t maximumRecords = 1024;
    using Key = LookupKey;

    [[nodiscard]] static Key KeyOf(const LookupRecord& record) noexcept
    {
        const std::uint64_t call = LookupControl::keyCalls.fetch_add(1, std::memory_order_relaxed) + 1;
        if (call == LookupControl::cancelAtKeyCall.load(std::memory_order_relaxed))
            if (CancelSource* source = LookupControl::cancellation.load(std::memory_order_relaxed); source != nullptr)
                (void)source->Cancel();
        return record.key;
    }
};

template <> struct RecordOwnershipPolicy<LookupRecord> final
{
    static constexpr bool enabled = true;

    [[nodiscard]] static std::optional<RecordOwnershipBounds> Measure(const LookupRecord& record) noexcept
    {
        return RecordOwnershipBounds{record.metadata, record.ownership, record.nodes, record.imagePixels,
                                     record.maskPixels};
    }
};

namespace
{

using LookupRoot = RecordRoot<LookupRecord, LookupAllocator<LookupRecord>>;

struct RecordBufferSnapshot final
{
    // This borrow is compared only while the failed builder still owns the original buffer, before retry.
    const LookupRecord* buffer = nullptr;
    std::size_t capacity = 0;
    std::size_t limit = 0;
    std::array<LookupRecord, 3> records{};
    bool ready = false;

    explicit RecordBufferSnapshot(const LookupRoot::Builder& builder) noexcept
        : buffer(builder.Read().data()), capacity(builder.Read().capacity()), limit(builder.Limit()),
          ready(builder.CanSeal() && builder.Read().size() == records.size())
    {
        if (ready)
            for (std::size_t index = 0; index < records.size(); ++index)
                records[index] = builder.Read()[index];
    }

    [[nodiscard]] bool SameRecords(const LookupRoot::Container& actual) const noexcept
    {
        if (!ready || actual.size() != records.size())
            return false;
        for (std::size_t index = 0; index < records.size(); ++index)
            if (actual[index] != records[index])
                return false;
        return true;
    }

    [[nodiscard]] bool Unchanged(const LookupRoot::Builder& builder) const noexcept
    {
        return builder.CanSeal() && builder.Read().data() == buffer && builder.Read().capacity() == capacity &&
               builder.Limit() == limit && SameRecords(builder.Read());
    }
};

struct PlainRecord final
{
    int value = 0;
    [[nodiscard]] bool operator==(const PlainRecord&) const noexcept = default;
};

template <typename Root>
concept HasKeyLookup = requires(const Root& root) { root.FindIndex(std::uint64_t{}); };

static_assert(!HasKeyLookup<RecordRoot<PlainRecord>>);

[[nodiscard]] LookupRecord MakeRecord(std::uint64_t key, std::uint64_t payload) noexcept
{
    return LookupRecord{LookupKey{key}, payload, 2, 5, 3, 7, 11};
}

[[nodiscard]] bool Append(LookupRoot::Builder& builder, std::uint64_t key, std::uint64_t payload) noexcept
{
    return builder.Append(MakeRecord(key, payload));
}

[[nodiscard]] std::optional<LookupRoot> TrySeal(LookupRoot::Builder& builder, CancelToken cancellation = {}) noexcept
{
    // The injected allocator deliberately throws; this test boundary translates only that expected fault to absence.
    try
    {
        return builder.Seal(cancellation);
    }
    catch (const std::bad_alloc&)
    {
        return std::nullopt;
    }
}

} // namespace

int RunRecordLookupTests()
{
    gFailures = 0;
    gAssertions = 0;
    gAdditionalAssertions = 0;
    LookupControl::Reset();

    AllocationProbe allocation;
    LookupRoot emptyHandle;
    Expect(!emptyHandle.FindIndex(LookupKey{10}).has_value() && emptyHandle.FindKey(LookupKey{10}) == nullptr,
           "default-empty-handle-misses");
    ExpectAdditional(emptyHandle.HasUniqueKeys(), "default-empty-handle-has-unique-keys");

    RecordRoot<PlainRecord>::Builder plainBuilder(1);
    const bool plainAppended = plainBuilder.Append(PlainRecord{17});
    auto plainRoot = plainAppended ? plainBuilder.Seal() : std::nullopt;
    Expect(plainAppended && plainRoot.has_value() && plainRoot->size() == 1 && plainRoot.value()[0].value == 17,
           "default-disabled-policy-retains-ordinary-root-behavior");

    LookupRoot::Builder emptyBuilder(0, LookupAllocator<LookupRecord>(&allocation));
    auto publishedEmpty = TrySeal(emptyBuilder);
    Expect(publishedEmpty.has_value(), "published-empty-root-seals");
    if (publishedEmpty.has_value())
        Expect(publishedEmpty.value().empty() && !publishedEmpty->FindIndex(LookupKey{10}).has_value() &&
                   publishedEmpty->FindKey(LookupKey{10}) == nullptr,
               "published-empty-root-misses");
    else
        Expect(false, "published-empty-root-misses");
    ExpectAdditional(publishedEmpty.has_value() && publishedEmpty->HasUniqueKeys(),
                     "published-empty-root-has-unique-keys");

    LookupControl::Reset();
    allocation.Reset();
    LookupRoot::Builder builder(4, LookupAllocator<LookupRecord>(&allocation));
    const bool baseAppended = Append(builder, 30, 300) && Append(builder, 10, 100) && Append(builder, 20, 200);
    Expect(baseAppended, "unsorted-three-record-fixture-appends");
    auto sealed = TrySeal(builder);
    Expect(sealed.has_value(), "unsorted-three-record-root-seals");
    if (!sealed.has_value() || sealed->size() != 3)
    {
        Expect(false, "literal-indexes-return-source-order");
        Expect(false, "key-lookup-borrows-matching-record");
        Expect(false, "missing-key-returns-empty-and-null");
        Expect(false, "copied-and-moved-handles-retain-root-owned-index");
        Expect(false, "edited-cloned-erased-root-index-is-independent");
        Expect(false, "duplicate-key-keeps-first-source-record");
        ExpectAdditional(false, "unique-three-key-root-has-unique-keys");
        ExpectAdditional(false, "duplicate-key-root-reports-nonunique-keys");
    }
    else
    {
        LookupRoot root = std::move(sealed.value());
        const auto index30 = root.FindIndex(LookupKey{30});
        const auto index10 = root.FindIndex(LookupKey{10});
        const auto index20 = root.FindIndex(LookupKey{20});
        Expect(index30.has_value() && index30.value() == 0 && index10.has_value() && index10.value() == 1 &&
                   index20.has_value() && index20.value() == 2,
               "literal-indexes-return-source-order");
        const LookupRecord* const found = root.FindKey(LookupKey{10});
        Expect(found != nullptr && found == &root[1] && found->payload == 100, "key-lookup-borrows-matching-record");
        Expect(!root.FindIndex(LookupKey{99}).has_value() && root.FindKey(LookupKey{99}) == nullptr,
               "missing-key-returns-empty-and-null");
        ExpectAdditional(root.HasUniqueKeys(), "unique-three-key-root-has-unique-keys");

        LookupRoot copied = root;
        LookupRoot moved = std::move(copied);
        Expect(moved.FindKey(LookupKey{20}) == &moved[2] && copied.empty() && root.FindKey(LookupKey{30}) == &root[0],
               "copied-and-moved-handles-retain-root-owned-index");

        auto edited = LookupRoot::Builder::Clone(root, 4, {}, LookupAllocator<LookupRecord>(&allocation));
        bool editSealed = false;
        if (edited.has_value())
        {
            LookupRoot::Builder editBuilder = std::move(edited.value());
            const bool editOkay = editBuilder.Replace(0, MakeRecord(40, 400)) &&
                                  editBuilder.Insert(1, MakeRecord(15, 150)) && editBuilder.Erase(2);
            auto editedRoot = editOkay ? TrySeal(editBuilder) : std::nullopt;
            editSealed = editedRoot.has_value() && editedRoot->size() == 3 &&
                         editedRoot->FindKey(LookupKey{40}) == &editedRoot.value()[0] &&
                         editedRoot->FindKey(LookupKey{15}) == &editedRoot.value()[1] &&
                         !editedRoot->FindIndex(LookupKey{30}).has_value();
        }
        Expect(editSealed && root.size() == 3 && root[0].payload == 300 && root.FindKey(LookupKey{30}) == &root[0],
               "edited-cloned-erased-root-index-is-independent");

        LookupRoot duplicates;
        LookupRoot::Builder duplicateBuilder(3, LookupAllocator<LookupRecord>(&allocation));
        const bool duplicatesAppended =
            Append(duplicateBuilder, 5, 51) && Append(duplicateBuilder, 2, 20) && Append(duplicateBuilder, 5, 52);
        auto duplicateRoot = duplicatesAppended ? TrySeal(duplicateBuilder) : std::nullopt;
        duplicates = duplicateRoot.has_value() ? std::move(duplicateRoot.value()) : LookupRoot{};
        const auto duplicateIndex = duplicates.FindIndex(LookupKey{5});
        const LookupRecord* const duplicateRecord = duplicates.FindKey(LookupKey{5});
        Expect(duplicatesAppended && duplicateRoot.has_value() && duplicates.size() == 3 &&
                   duplicateIndex.has_value() && duplicateIndex.value() == 0 && duplicateRecord == &duplicates[0] &&
                   duplicateRecord->payload == 51,
               "duplicate-key-keeps-first-source-record");
        ExpectAdditional(duplicatesAppended && duplicateRoot.has_value() && !duplicates.HasUniqueKeys(),
                         "duplicate-key-root-reports-nonunique-keys");
    }

    {
        LookupControl::Reset();
        LookupRoot::Builder cancelledBuilder(3);
        const bool appended =
            Append(cancelledBuilder, 3, 30) && Append(cancelledBuilder, 1, 10) && Append(cancelledBuilder, 2, 20);
        CancelSource beforeSource;
        (void)beforeSource.Cancel();
        const auto beforeCancelled = TrySeal(cancelledBuilder, beforeSource.Token());
        Expect(appended && !beforeCancelled.has_value() && cancelledBuilder.CanSeal() &&
                   cancelledBuilder.Read().size() == 3 && cancelledBuilder.Read()[0].payload == 30,
               "pre-cancelled-seal-preserves-retryable-builder");

        CancelSource duringSource;
        LookupControl::cancellation.store(&duringSource, std::memory_order_relaxed);
        LookupControl::cancelAtKeyCall.store(2, std::memory_order_relaxed);
        const auto duringCancelled = TrySeal(cancelledBuilder, duringSource.Token());
        Expect(!duringCancelled.has_value() && duringSource.Token().IsCancelled() && cancelledBuilder.CanSeal() &&
                   cancelledBuilder.Read().size() == 3 && LookupControl::keyCalls.load(std::memory_order_relaxed) == 2,
               "cancel-during-key-preparation-preserves-all-records-for-retry");
        LookupControl::Reset();
        const auto retried = TrySeal(cancelledBuilder);
        Expect(retried.has_value() && retried->size() == 3 && retried->FindKey(LookupKey{1}) == &retried.value()[1],
               "cancelled-builder-can-seal-intact-on-retry");
    }

    {
        LookupControl::Reset();
        AllocationProbe indexAllocation;
        LookupRoot::Builder allocationBuilder(3, LookupAllocator<LookupRecord>(&indexAllocation));
        const bool appended =
            Append(allocationBuilder, 8, 80) && Append(allocationBuilder, 4, 40) && Append(allocationBuilder, 6, 60);
        const RecordBufferSnapshot beforeFailure(allocationBuilder);
        indexAllocation.faultTarget.store(AllocationClass::KeyIndex, std::memory_order_relaxed);
        const auto indexFailed = TrySeal(allocationBuilder);
        const bool indexPreserved = appended && !indexFailed.has_value() && allocationBuilder.CanSeal() &&
                                    allocationBuilder.Read().size() == 3 && allocationBuilder.Read()[0].payload == 80;
        const bool indexFailureConfirmed =
            !indexFailed.has_value() && indexAllocation.indexFailures.load(std::memory_order_relaxed) == 1 &&
            indexAllocation.rootFailures.load(std::memory_order_relaxed) == 0 &&
            indexAllocation.indexAttempts.load(std::memory_order_relaxed) == 1 &&
            indexAllocation.recordAttempts.load(std::memory_order_relaxed) == 1 &&
            indexAllocation.rootAttempts.load(std::memory_order_relaxed) == 0 &&
            indexAllocation.unclassifiedAttempts.load(std::memory_order_relaxed) == 0 &&
            indexAllocation.keyCallsAtFailure.load(std::memory_order_relaxed) == 0 &&
            indexAllocation.indexSuccessesAtFailure.load(std::memory_order_relaxed) == 0 &&
            indexAllocation.faultTarget.load(std::memory_order_relaxed) == AllocationClass::None;
        ExpectAdditional(indexFailureConfirmed, "typed-index-allocation-fails-once");
        ExpectAdditional(indexPreserved && beforeFailure.Unchanged(allocationBuilder),
                         "index-failure-preserves-original-buffer-and-all-records-before-retry");
        const auto indexRetried = TrySeal(allocationBuilder);
        Expect(indexPreserved && indexFailureConfirmed && indexRetried.has_value() && indexRetried->size() == 3 &&
                   beforeFailure.SameRecords(indexRetried->Read()) &&
                   indexAllocation.indexFailures.load(std::memory_order_relaxed) == 1 &&
                   indexRetried->FindKey(LookupKey{6}) == &indexRetried.value()[2],
               "index-allocation-failure-preserves-builder-and-retry");
    }

    {
        LookupControl::Reset();
        AllocationProbe rootAllocation;
        LookupRoot::Builder allocationBuilder(3, LookupAllocator<LookupRecord>(&rootAllocation));
        const bool appended =
            Append(allocationBuilder, 9, 90) && Append(allocationBuilder, 7, 70) && Append(allocationBuilder, 8, 80);
        const RecordBufferSnapshot beforeFailure(allocationBuilder);
        rootAllocation.faultTarget.store(AllocationClass::RootControl, std::memory_order_relaxed);
        const auto rootFailed = TrySeal(allocationBuilder);
        const bool rootPreserved = appended && !rootFailed.has_value() && allocationBuilder.CanSeal() &&
                                   allocationBuilder.Read().size() == 3 && allocationBuilder.Read()[1].payload == 70;
        const bool rootFailureConfirmed =
            !rootFailed.has_value() && rootAllocation.rootFailures.load(std::memory_order_relaxed) == 1 &&
            rootAllocation.indexFailures.load(std::memory_order_relaxed) == 0 &&
            rootAllocation.rootAttempts.load(std::memory_order_relaxed) == 1 &&
            rootAllocation.recordAttempts.load(std::memory_order_relaxed) == 1 &&
            rootAllocation.indexAttempts.load(std::memory_order_relaxed) == 1 &&
            rootAllocation.unclassifiedAttempts.load(std::memory_order_relaxed) == 0 &&
            rootAllocation.keyCallsAtFailure.load(std::memory_order_relaxed) == 3 &&
            rootAllocation.indexSuccessesAtFailure.load(std::memory_order_relaxed) == 1 &&
            rootAllocation.faultTarget.load(std::memory_order_relaxed) == AllocationClass::None;
        ExpectAdditional(rootFailureConfirmed, "typed-root-allocation-fails-once-after-index-and-three-key-calls");
        ExpectAdditional(rootPreserved && beforeFailure.Unchanged(allocationBuilder),
                         "root-failure-preserves-original-buffer-and-all-records-before-retry");
        const auto rootRetried = TrySeal(allocationBuilder);
        Expect(rootPreserved && rootFailureConfirmed && rootRetried.has_value() && rootRetried->size() == 3 &&
                   beforeFailure.SameRecords(rootRetried->Read()) &&
                   rootAllocation.rootFailures.load(std::memory_order_relaxed) == 1 &&
                   rootRetried->FindKey(LookupKey{7}) == &rootRetried.value()[1],
               "root-allocation-failure-after-index-preserves-builder-and-retry");
    }

    {
        AllocationProbe boundsAllocation;
        LookupRoot::Builder boundsBuilder(3, LookupAllocator<LookupRecord>(&boundsAllocation));
        const bool appended = boundsBuilder.Append(LookupRecord{LookupKey{1}, 10, 2, 5, 3, 7, 11}) &&
                              boundsBuilder.Append(LookupRecord{LookupKey{2}, 20, 4, 8, 5, 13, 17}) &&
                              boundsBuilder.Append(LookupRecord{LookupKey{3}, 30, 6, 9, 7, 19, 23});
        const auto boundRoot = appended ? TrySeal(boundsBuilder) : std::nullopt;
        const auto bounds = boundRoot.has_value() ? boundRoot->OwnershipBounds() : std::nullopt;
        const std::uint64_t indexBytes = boundsAllocation.indexBytes.load(std::memory_order_relaxed);
        const std::uint64_t recordBytes =
            boundRoot.has_value() ? static_cast<std::uint64_t>(boundRoot->capacity()) * sizeof(LookupRecord) : 0;
        const bool boundsReady = bounds.has_value() && boundRoot.has_value();
        Expect(boundsReady, "pixel-bound-root-seals-with-complete-measurement");
        if (boundsReady)
        {
            Expect(bounds->imagePixels == 39 && bounds->maskPixels == 51,
                   "image-and-mask-pixel-bounds-sum-literal-record-values");
            Expect(indexBytes >= 3 * sizeof(LookupEntry) &&
                       bounds->ownershipBytes >= indexBytes + 128 + recordBytes + 22,
                   "ownership-bound-covers-actual-index-allocation-and-control-allowance");
        }
        else
        {
            Expect(false, "image-and-mask-pixel-bounds-sum-literal-record-values");
            Expect(false, "ownership-bound-covers-actual-index-allocation-and-control-allowance");
        }

        LookupRoot::Builder overflowBuilder(2);
        const bool overflowAppended = overflowBuilder.Append(LookupRecord{
                                          LookupKey{1}, 1, 0, 0, 0, (std::numeric_limits<std::uint64_t>::max)(), 0}) &&
                                      overflowBuilder.Append(LookupRecord{LookupKey{2}, 2, 0, 0, 0, 1, 0});
        const auto overflowRoot = overflowAppended ? TrySeal(overflowBuilder) : std::nullopt;
        Expect(overflowAppended && overflowRoot.has_value() && !overflowRoot->OwnershipBounds().has_value(),
               "pixel-total-overflow-makes-bound-unavailable");

        LookupRoot::Builder maskOverflowBuilder(2);
        const bool maskOverflowAppended =
            maskOverflowBuilder.Append(
                LookupRecord{LookupKey{1}, 1, 0, 0, 0, 0, (std::numeric_limits<std::uint64_t>::max)()}) &&
            maskOverflowBuilder.Append(LookupRecord{LookupKey{2}, 2, 0, 0, 0, 0, 1});
        const auto maskOverflowRoot = maskOverflowAppended ? TrySeal(maskOverflowBuilder) : std::nullopt;
        ExpectAdditional(maskOverflowAppended && maskOverflowRoot.has_value() &&
                             !maskOverflowRoot->OwnershipBounds().has_value(),
                         "mask-pixel-total-overflow-makes-bound-unavailable");
    }

    {
        LookupRoot::Builder lifetimeBuilder(3);
        const bool appended =
            Append(lifetimeBuilder, 30, 300) && Append(lifetimeBuilder, 10, 100) && Append(lifetimeBuilder, 20, 200);
        auto original = appended ? TrySeal(lifetimeBuilder) : std::nullopt;
        if (original.has_value() && original->size() == 3)
        {
            LookupRoot held = original.value();
            const LookupRecord* const borrowed = original->FindKey(LookupKey{10});
            original.reset();
            ExpectAdditional(!original.has_value() && held.size() == 3 && borrowed != nullptr &&
                                 borrowed == held.FindKey(LookupKey{10}) && borrowed == &held[1] &&
                                 borrowed->key.value == 10 && borrowed->payload == 100,
                             "copied-root-keeps-borrowed-record-live-after-original-reset");
        }
        else
            ExpectAdditional(false, "copied-root-keeps-borrowed-record-live-after-original-reset");
    }

    {
        constexpr std::size_t refusedCount = 1025;
        AllocationProbe refusedAllocation;
        LookupRoot::Builder refusedBuilder(refusedCount, LookupAllocator<LookupRecord>(&refusedAllocation));
        bool appended = true;
        for (std::size_t index = 0; index < refusedCount; ++index)
            if (!Append(refusedBuilder, index, index * 9))
            {
                appended = false;
                break;
            }
        const LookupRecord* const originalBuffer = refusedBuilder.Read().data();
        const std::size_t originalCapacity = refusedBuilder.Read().capacity();
        const std::uint64_t allocationsBefore = refusedAllocation.calls.load(std::memory_order_relaxed);
        LookupControl::Reset();
        const auto refused = TrySeal(refusedBuilder);
        const std::uint64_t keysAfter = LookupControl::keyCalls.load(std::memory_order_relaxed);
        const std::uint64_t allocationsAfter = refusedAllocation.calls.load(std::memory_order_relaxed);
        ExpectAdditional(appended && !refused.has_value(), "1025-record-seal-refuses-policy-maximum");
        ExpectAdditional(appended && !refused.has_value() && keysAfter == 0 && allocationsAfter == allocationsBefore &&
                             refusedAllocation.indexAttempts.load(std::memory_order_relaxed) == 0 &&
                             refusedAllocation.rootAttempts.load(std::memory_order_relaxed) == 0,
                         "1025-record-refusal-precedes-key-and-allocator-calls");
        bool allRecordsIntact = refusedBuilder.Read().size() == refusedCount;
        for (std::size_t index = 0; allRecordsIntact && index < refusedCount; ++index)
            allRecordsIntact = refusedBuilder.Read()[index] == MakeRecord(index, index * 9);
        ExpectAdditional(appended && !refused.has_value() && refusedBuilder.CanSeal() &&
                             refusedBuilder.Limit() == refusedCount && refusedBuilder.Read().data() == originalBuffer &&
                             refusedBuilder.Read().capacity() == originalCapacity && allRecordsIntact,
                         "1025-record-refusal-preserves-buffer-and-every-record");
    }

    {
        constexpr std::size_t recordCount = 1024;
        AllocationProbe queryAllocation;
        LookupRoot::Builder manyBuilder(recordCount, LookupAllocator<LookupRecord>(&queryAllocation));
        bool appended = true;
        for (std::size_t index = 0; index < recordCount; ++index)
            if (!Append(manyBuilder, static_cast<std::uint64_t>(recordCount - index - 1), index * 9))
            {
                appended = false;
                break;
            }
        auto manyRoot = appended ? TrySeal(manyBuilder) : std::nullopt;
        Expect(appended && manyRoot.has_value() && manyRoot->size() == recordCount,
               "1024-record-unsorted-root-fixture-seals");
        if (manyRoot.has_value() && manyRoot->size() == recordCount)
        {
            const std::uint64_t allocationsBefore = queryAllocation.calls.load(std::memory_order_relaxed);
            LookupControl::comparisons.store(0, std::memory_order_relaxed);
            const auto middle = manyRoot->FindIndex(LookupKey{511});
            const LookupRecord* const found = manyRoot->FindKey(LookupKey{511});
            const std::uint64_t comparisonCount = LookupControl::comparisons.load(std::memory_order_relaxed);
            const std::uint64_t allocationsAfter = queryAllocation.calls.load(std::memory_order_relaxed);
            Expect(middle.has_value() && middle.value() == 512 && found == &manyRoot.value()[512] &&
                       found->payload == 512 * 9,
                   "1024-record-query-returns-source-index-and-record");
            Expect(comparisonCount <= 32, "1024-record-query-uses-bounded-logarithmic-comparisons");
            Expect(allocationsAfter == allocationsBefore, "published-key-queries-allocate-zero-times");
        }
        else
        {
            Expect(false, "1024-record-query-returns-source-index-and-record");
            Expect(false, "1024-record-query-uses-bounded-logarithmic-comparisons");
            Expect(false, "published-key-queries-allocate-zero-times");
        }
    }

    std::printf("record-lookup assertions=%d failures=%d\n", gAssertions, gFailures);
    std::printf("record-lookup additional-assertions=%d total-assertions=%d failures=%d\n", gAdditionalAssertions,
                gAssertions + gAdditionalAssertions, gFailures);
    return gFailures;
}
