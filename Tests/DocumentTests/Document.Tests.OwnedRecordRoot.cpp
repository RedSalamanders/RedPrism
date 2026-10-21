#include "Document.Tests.OwnedRecordRoot.h"

#include "RecordRoot.h"
#include "SharedPublicationMemory.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <memory>
#include <optional>
#include <utility>

namespace
{
int gFailures = 0;
int gChecks = 0;

void Expect(bool condition, const char* name) noexcept
{
    ++gChecks;
    std::printf("%s document.owned-record-root.%s\n", condition ? "PASS" : "FAIL", name);
    if (!condition)
        ++gFailures;
}

struct OwnedRecord final
{
    std::uint64_t key = 0;
    std::uint32_t value = 0;
    std::uint32_t metadata = 0;
    [[nodiscard]] bool operator==(const OwnedRecord&) const noexcept = default;
};

struct UnavailableRecord final
{
    std::uint32_t value = 0;
    [[nodiscard]] bool operator==(const UnavailableRecord&) const noexcept = default;
};

struct OverflowRecord final
{
    std::uint32_t value = 0;
    [[nodiscard]] bool operator==(const OverflowRecord&) const noexcept = default;
};

} // namespace

template <> struct RecordOwnershipPolicy<OwnedRecord>
{
    static constexpr bool enabled = true;
    [[nodiscard]] static std::optional<RecordOwnershipBounds> Measure(const OwnedRecord& record) noexcept
    {
        return RecordOwnershipBounds{11 + record.metadata, 17 + record.metadata, 2, 3 + record.value, 5 + record.value};
    }
};

template <> struct RecordLookupPolicy<OwnedRecord>
{
    static constexpr bool enabled = true;
    static constexpr std::size_t maximumRecords = 4;
    using Key = std::uint64_t;
    [[nodiscard]] static Key KeyOf(const OwnedRecord& record) noexcept
    {
        return record.key;
    }
};

template <> struct RecordOwnershipPolicy<UnavailableRecord>
{
    static constexpr bool enabled = true;
    [[nodiscard]] static std::optional<RecordOwnershipBounds> Measure(const UnavailableRecord&) noexcept
    {
        return std::nullopt;
    }
};

template <> struct RecordOwnershipPolicy<OverflowRecord>
{
    static constexpr bool enabled = true;
    [[nodiscard]] static std::optional<RecordOwnershipBounds> Measure(const OverflowRecord&) noexcept
    {
        constexpr auto maximum = (std::numeric_limits<std::uint64_t>::max)();
        return RecordOwnershipBounds{maximum, maximum, 0, 0, 0};
    }
};

namespace
{
using Root = RecordRoot<OwnedRecord>;

Root MakeReferenceRoot()
{
    Root::Builder builder(2);
    (void)builder.Append(OwnedRecord{17, 101, 2});
    (void)builder.Append(OwnedRecord{29, 202, 4});
    auto sealed = builder.Seal();
    return sealed ? std::move(sealed.value()) : Root{};
}
} // namespace

int RunOwnedRecordRootTests()
{
    gFailures = 0;
    gChecks = 0;

    {
        Root original = MakeReferenceRoot();
        const void* const oldIdentity = original.Identity();
        Root captured = original;
        auto builder = Root::Builder::Clone(original, 3);
        bool editPublished = false;
        if (builder)
        {
            editPublished = builder.value().Replace(0, OwnedRecord{31, 303, 6});
            auto sealed = builder.value().Seal();
            editPublished = editPublished && sealed.has_value();
            if (sealed)
            {
                Root updated = std::move(sealed.value());
                editPublished = editPublished && updated.Identity() != oldIdentity && updated.size() == 2 &&
                                updated[0].key == 31 && updated[0].value == 303 && updated[0].metadata == 6 &&
                                updated[1].key == 29 && updated[1].value == 202;
            }
        }
        Expect(original.size() == 2 && original[0].key == 17 && original[0].value == 101 && original[0].metadata == 2 &&
                   original[1].key == 29 && original[1].value == 202,
               "literal-original-record-values-remain-unchanged");
        Expect(captured.Identity() == oldIdentity && original.Identity() == oldIdentity &&
                   captured[0] == OwnedRecord{17, 101, 2} && editPublished,
               "captured-old-root-keeps-identity-and-new-copy-has-independent-identity");
    }

    {
        Root::Builder builder(3);
        (void)builder.Append(OwnedRecord{41, 7, 1});
        (void)builder.Append(OwnedRecord{53, 11, 2});
        auto sealed = builder.Seal();
        bool boundsConsistent = false;
        bool lookupConsistent = false;
        if (sealed)
        {
            const Root& root = sealed.value();
            const auto bounds = root.OwnershipBounds();
            boundsConsistent = bounds.has_value() && root.capacity() >= root.size() &&
                               bounds.value().metadataBytes >= root.capacity() * sizeof(OwnedRecord) &&
                               bounds.value().ownershipBytes >= bounds.value().metadataBytes &&
                               bounds.value().ownershipBytes - bounds.value().metadataBytes == 12 &&
                               bounds.value().rasterNodes == 4 && bounds.value().imagePixels == 24 &&
                               bounds.value().maskPixels == 28;
            const auto first = root.FindIndex(41);
            const auto second = root.FindIndex(53);
            const auto missing = root.FindIndex(99);
            lookupConsistent = first == 0 && second == 1 && !missing && root.FindKey(41) == &root[0] &&
                               root.FindKey(99) == nullptr && root.HasUniqueKeys();
        }
        Expect(boundsConsistent, "cached-bounds-match-literal-policy-totals-and-actual-record-capacity");
        Expect(lookupConsistent, "cached-index-returns-ordered-records-and-refuses-missing-key");
    }

    {
        Root::Builder builder(2);
        (void)builder.Append(OwnedRecord{61, 13, 3});
        (void)builder.Append(OwnedRecord{67, 17, 5});
        auto memory = SharedPublicationMemory::CreateForWork();
        std::weak_ptr<const SharedPublicationMemory> memoryLifetime = memory;
        const bool startsUnbound = memory && !memory->IsBound();
        auto sealed = builder.SealOwned(memory);
        std::weak_ptr<const void> storageControl;
        bool retainedThroughWeakControl = false;
        if (sealed)
        {
            Root published = std::move(sealed.value());
            storageControl = published.WeakStorageForTest();
            published = Root{};
            memory.reset();
            retainedThroughWeakControl = storageControl.expired() && !memoryLifetime.expired();
            storageControl.reset();
        }
        Expect(startsUnbound && sealed.has_value(), "unbound-original-owner-seals-owned-record-root");
        Expect(retainedThroughWeakControl && memoryLifetime.expired(),
               "original-owner-lives-through-last-weak-publication-control");
    }

    {
        Root::Builder cancelledBuilder(2);
        (void)cancelledBuilder.Append(OwnedRecord{71, 19, 7});
        CancelSource cancellation;
        (void)cancellation.Cancel();
        auto memory = SharedPublicationMemory::CreateForWork();
        auto cancelled = cancelledBuilder.SealOwned(memory, cancellation.Token());
        const bool cancellationPreserved = !cancelled && cancelledBuilder.CanSeal() &&
                                           cancelledBuilder.Read().size() == 1 &&
                                           cancelledBuilder.Read()[0] == OwnedRecord{71, 19, 7};
        Expect(cancellationPreserved, "cancelled-owned-seal-refuses-and-preserves-builder");

        Root::Builder emptyOwnerBuilder(2);
        (void)emptyOwnerBuilder.Append(OwnedRecord{73, 23, 8});
        std::shared_ptr<const SharedPublicationMemory> noOwner;
        auto ownerless = emptyOwnerBuilder.SealOwned(std::move(noOwner));
        Expect(!ownerless && emptyOwnerBuilder.CanSeal() && emptyOwnerBuilder.Read().size() == 1 &&
                   emptyOwnerBuilder.Read()[0] == OwnedRecord{73, 23, 8},
               "empty-owner-refusal-preserves-builder");
    }

    {
        using UnavailableRoot = RecordRoot<UnavailableRecord>;
        UnavailableRoot::Builder builder(1);
        (void)builder.Append(UnavailableRecord{79});
        auto memory = SharedPublicationMemory::CreateForWork();
        auto refused = builder.SealOwned(memory);
        Expect(!refused && builder.CanSeal() && builder.Read().size() == 1 && builder.Read()[0].value == 79,
               "unavailable-ownership-bound-refuses-and-preserves-builder");
    }

    {
        using OverflowRoot = RecordRoot<OverflowRecord>;
        OverflowRoot::Builder builder(1);
        (void)builder.Append(OverflowRecord{83});
        auto memory = SharedPublicationMemory::CreateForWork();
        auto refused = builder.SealOwned(memory);
        Expect(!refused && builder.CanSeal() && builder.Read().size() == 1 && builder.Read()[0].value == 83,
               "overflowing-ownership-bound-refuses-and-preserves-builder");
    }

    {
        Root::Builder builder(1);
        (void)builder.Append(OwnedRecord{89, 31, 9});
        auto sealed = builder.Seal();
        const bool referenceSealWorks = sealed && sealed.value().size() == 1 &&
                                        sealed.value()[0] == OwnedRecord{89, 31, 9} &&
                                        sealed.value().OwnershipBounds().has_value();
        Expect(referenceSealWorks, "default-reference-seal-retains-values-and-bound-reporting");
    }

    if (gChecks != 11)
        ++gFailures;
    std::printf("%s document.owned-record-root.summary checks=%d failures=%d\n", gFailures == 0 ? "PASS" : "FAIL",
                gChecks, gFailures);
    return gFailures;
}
