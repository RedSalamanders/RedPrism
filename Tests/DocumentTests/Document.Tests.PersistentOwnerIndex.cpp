#include "Document.Tests.PersistentOwnerIndex.h"

#include "PersistentOwnerIndex.h"
#include "SharedPublicationMemory.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <new>
#include <stop_token>
#include <utility>
#include <vector>

namespace
{
constexpr std::uint64_t kLiteralValueBytes = 1024;
constexpr std::size_t kKeyCount = 1024;

int gFailures = 0;
int gControls = 0;

void Expect(bool condition, const char* name) noexcept
{
    std::printf("%s persistent-owner-index.%s\n", condition ? "PASS" : "FAIL", name);
    ++gControls;
    if (!condition)
        ++gFailures;
}

struct Marker final
{
    explicit Marker(std::uint64_t markerID) noexcept : id(markerID) {}
    std::uint64_t id;
};

struct Value final
{
    std::shared_ptr<const Marker> marker;
    std::uint64_t payload = 0;
    std::uint64_t retirementBytes = kLiteralValueBytes;
};

struct OwnerPolicy final
{
    [[nodiscard]] const void* operator()(const Value& value) const noexcept
    {
        return value.marker.get();
    }

    [[nodiscard]] static std::uint64_t RetirementBytes(const Value& value) noexcept
    {
        return value.retirementBytes;
    }
};

using Index = PersistentOwnerIndex<Value, OwnerPolicy>;
using Model = std::map<const void*, std::uint64_t, std::less<const void*>>;

[[nodiscard]] std::shared_ptr<const SharedPublicationMemory> NewMemory()
{
    return SharedPublicationMemory::CreateForWork();
}

[[nodiscard]] Value MakeValue(const std::shared_ptr<const Marker>& marker, std::uint64_t payload)
{
    return {marker, payload, kLiteralValueBytes};
}

bool Insert(Index& index, const Value& value, std::size_t* allocationCount = nullptr)
{
    Index::Observer observer{};
    if (allocationCount)
    {
        observer.context = allocationCount;
        observer.allocating = [](void* context) { ++*static_cast<std::size_t*>(context); };
    }
    Index output;
    if (!index.TrySet(value, NewMemory(), {}, output, observer))
        return false;
    index = std::move(output);
    return true;
}

bool Erase(Index& index, const void* key, std::size_t* allocationCount = nullptr)
{
    Index::Observer observer{};
    if (allocationCount)
    {
        observer.context = allocationCount;
        observer.allocating = [](void* context) { ++*static_cast<std::size_t*>(context); };
    }
    Index output;
    if (!index.TryErase(key, NewMemory(), {}, output, observer))
        return false;
    index = std::move(output);
    return true;
}

void VerifyModel(const Index& index, const Model& expected)
{
    Expect(index.size() == expected.size(), "x.model.size");
    std::size_t power = 1;
    unsigned int logarithm = 0;
    while (power < expected.size() + 1)
    {
        power *= 2;
        ++logarithm;
    }
    Expect(index.HeightForTest() <= 2 * logarithm, "x.model.height-within-independent-logarithmic-bound");

    bool exactFinds = true;
    for (const auto& [key, payload] : expected)
    {
        const Value* const found = index.Find(key);
        exactFinds = exactFinds && found && found->marker.get() == key && found->payload == payload &&
                     found->retirementBytes == kLiteralValueBytes;
    }
    Expect(exactFinds, "x.model.find-exact-key-value");
    Expect(index.Find(nullptr) == nullptr, "x.model.null-find-is-missing");

    bool ordered = true;
    std::size_t visited = 0;
    auto expectedIt = expected.begin();
    const bool completed = index.Visit(
        [&](const void* key, const Value& value)
        {
            const bool matches = expectedIt != expected.end() && key == expectedIt->first &&
                                 value.marker.get() == expectedIt->first && value.payload == expectedIt->second &&
                                 value.retirementBytes == kLiteralValueBytes;
            ordered = ordered && matches;
            if (expectedIt != expected.end())
                ++expectedIt;
            ++visited;
            return true;
        });
    Expect(completed, "x.model.visit-completes");
    Expect(ordered, "x.model.visit-matches-map-order");
    Expect(expectedIt == expected.end(), "x.model.visit-covers-every-map-entry");
    Expect(visited == expected.size(), "x.model.visit-count-matches-map-size");

    const std::uint64_t independentlyExpectedBytes =
        static_cast<std::uint64_t>(expected.size()) * (Index::NodeAllocationAllowance() + kLiteralValueBytes);
    Expect(index.RetirementBytes() == independentlyExpectedBytes, "x.model.retirement-is-independent-literal-sum");
}

void CountAllocation(void* context)
{
    ++*static_cast<std::size_t*>(context);
}

struct CancelAtVisit final
{
    CancelSource* source = nullptr;
    std::size_t target = 0;
    std::size_t count = 0;

    static void Visit(void* context) noexcept
    {
        auto& state = *static_cast<CancelAtVisit*>(context);
        ++state.count;
        if (state.count == state.target)
            static_cast<void>(state.source->Cancel());
    }
};

struct ThrowAtAllocation final
{
    static void Run(void*)
    {
        throw std::bad_alloc{};
    }
};

[[nodiscard]] std::vector<std::shared_ptr<const Marker>> MakeMarkers()
{
    std::vector<std::shared_ptr<const Marker>> markers;
    markers.reserve(kKeyCount);
    for (std::size_t i = 0; i < kKeyCount; ++i)
        markers.push_back(std::make_shared<Marker>(static_cast<std::uint64_t>(i)));
    return markers;
}

[[nodiscard]] std::vector<std::size_t> AddressOrder(const std::vector<std::shared_ptr<const Marker>>& markers)
{
    std::vector<std::size_t> order(markers.size());
    for (std::size_t i = 0; i < order.size(); ++i)
        order[i] = i;
    std::sort(order.begin(), order.end(), [&](std::size_t left, std::size_t right)
              { return std::less<const void*>{}(markers[left].get(), markers[right].get()); });
    return order;
}

void ShuffleDeterministically(std::vector<std::size_t>& order)
{
    std::uint32_t state = 0x6D2B79F5U;
    for (std::size_t remaining = order.size(); remaining > 1; --remaining)
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        const std::size_t selected = static_cast<std::size_t>(state) % remaining;
        std::swap(order[remaining - 1], order[selected]);
    }
}

void VerifyStoppedVisit(const Index& index)
{
    std::size_t visited = 0;
    const bool completed = index.Visit(
        [&](const void*, const Value&)
        {
            ++visited;
            return visited < 7;
        });
    Expect(!completed, "x.visit.false-stops");
    Expect(visited == 7, "x.visit.false-stops-at-current-entry");

    CancelSource cancelled;
    static_cast<void>(cancelled.Cancel());
    visited = 0;
    const bool cancelledVisit = index.Visit(
        [&](const void*, const Value&)
        {
            ++visited;
            return true;
        },
        cancelled.Token());
    Expect(!cancelledVisit, "x.visit.pre-cancel-returns-false");
    Expect(visited == 0, "x.visit.pre-cancel-calls-no-visitor");
}

void RotationPrelude(const std::vector<std::shared_ptr<const Marker>>& markers,
                     const std::vector<std::size_t>& addressOrder)
{
    constexpr std::array<std::array<std::size_t, 3>, 4> orders{{{{0, 1, 2}}, {{2, 1, 0}}, {{0, 2, 1}}, {{2, 0, 1}}}};
    for (std::size_t pattern = 0; pattern < orders.size(); ++pattern)
    {
        Index index;
        Model model;
        bool accepted = true;
        for (const std::size_t addressRank : orders[pattern])
        {
            const std::size_t item = addressOrder[addressRank];
            const Value value = MakeValue(markers[item], 100 + markers[item]->id);
            accepted = Insert(index, value) && accepted;
            model.emplace(markers[item].get(), value.payload);
        }
        Expect(accepted, "x.rotation.prelude-updates-accepted");
        VerifyModel(index, model);
    }
}

void RunAddressPattern(const std::vector<std::shared_ptr<const Marker>>& markers, std::vector<std::size_t> order,
                       const char* pattern)
{
    Index index;
    Model model;
    std::size_t acceptedUpdates = 0;
    bool accepted = true;
    for (const std::size_t item : order)
    {
        const auto& marker = markers[item];
        const Value value = MakeValue(marker, marker->id * 17 + 3);
        std::size_t allocations = 0;
        const bool inserted = Insert(index, value, &allocations);
        accepted = accepted && inserted;
        if (inserted)
        {
            model.emplace(marker.get(), value.payload);
            ++acceptedUpdates;
        }
        Expect(allocations <= 3 * Index::MaximumHeight + 1, "x.update.allocations-within-avl-ceiling");
    }
    Expect(accepted, "x.pattern.all-insertions-succeeded");
    Expect(acceptedUpdates >= kKeyCount, pattern);

    Index originalVersion = index;
    const Model originalModel = model;
    VerifyModel(index, model);
    VerifyStoppedVisit(index);

    for (std::size_t position = 0; position < order.size(); position += 3)
    {
        const auto& marker = markers[order[position]];
        const Value replacement = MakeValue(marker, 0xB000000000000000ULL + marker->id);
        std::size_t allocations = 0;
        const bool replaced = Insert(index, replacement, &allocations);
        accepted = accepted && replaced;
        if (replaced)
            model[marker.get()] = replacement.payload;
        Expect(allocations <= 3 * Index::MaximumHeight + 1, "x.update.replacement-allocation-ceiling");
    }
    Index replacedVersion = index;
    const Model replacedModel = model;
    VerifyModel(index, model);
    VerifyModel(originalVersion, originalModel);

    const auto absentKey = std::make_shared<Marker>(std::numeric_limits<std::uint64_t>::max());
    Index missingEraseOutput;
    std::size_t missingEraseAllocations = 0;
    Index::Observer missingEraseObserver{&missingEraseAllocations, nullptr, &CountAllocation};
    const bool missingErase =
        index.TryErase(absentKey.get(), NewMemory(), {}, missingEraseOutput, missingEraseObserver);
    Expect(missingErase, "x.erase.missing-key-is-successful-no-op");
    Expect(missingEraseOutput.RootIdentityForTest() == index.RootIdentityForTest(),
           "x.erase.missing-key-keeps-root-identity");
    Expect(missingEraseAllocations == 0, "x.erase.missing-key-allocates-no-nodes");

    for (std::size_t position = 1; position < order.size(); position += 2)
    {
        const void* const key = markers[order[position]].get();
        std::size_t allocations = 0;
        const bool erased = Erase(index, key, &allocations);
        accepted = accepted && erased;
        if (erased)
            model.erase(key);
        Expect(allocations <= 3 * Index::MaximumHeight + 1, "x.erase.first-pattern-allocation-ceiling");
    }
    VerifyModel(index, model);

    for (std::size_t reverse = order.size(); reverse > 0; --reverse)
    {
        const std::size_t position = reverse - 1;
        if (position % 3 != 0)
            continue;
        const void* const key = markers[order[position]].get();
        const bool existed = model.contains(key);
        std::size_t allocations = 0;
        const bool erased = Erase(index, key, &allocations);
        accepted = accepted && erased;
        if (erased)
            model.erase(key);
        Expect(erased, "x.erase.pattern-update-succeeds");
        Expect(!existed || !model.contains(key), "x.erase.pattern-removes-present-map-key");
        Expect(allocations <= 3 * Index::MaximumHeight + 1, "x.erase.second-pattern-allocation-ceiling");
    }
    VerifyModel(index, model);
    VerifyModel(replacedVersion, replacedModel);
    VerifyModel(originalVersion, originalModel);
    Expect(accepted, "x.pattern.all-updates-succeeded");
}

void RunInputRefusalAndFaultTransactions(const std::vector<std::shared_ptr<const Marker>>& markers)
{
    Index source;
    const Value sourceValue = MakeValue(markers[0], 71);
    Expect(Insert(source, sourceValue), "x.transaction.source-seeded");
    Index output;
    const Value outputValue = MakeValue(markers[1], 91);
    Expect(Insert(output, outputValue), "x.transaction.output-seeded");
    const void* const sourceRoot = source.RootIdentityForTest();
    const void* const outputRoot = output.RootIdentityForTest();

    Value ownerlessValue = MakeValue(markers[2], 123);
    Expect(!source.TrySet(ownerlessValue, {}, {}, output), "x.transaction.null-memory-refused");
    Expect(source.RootIdentityForTest() == sourceRoot, "x.transaction.null-memory-keeps-source-root");
    Expect(output.RootIdentityForTest() == outputRoot, "x.transaction.null-memory-keeps-output-root");

    Value nullOwnerValue{{}, 124, kLiteralValueBytes};
    Expect(!source.TrySet(nullOwnerValue, NewMemory(), {}, output), "x.transaction.null-owner-refused");
    Expect(source.RootIdentityForTest() == sourceRoot, "x.transaction.null-owner-keeps-source-root");
    Expect(output.RootIdentityForTest() == outputRoot, "x.transaction.null-owner-keeps-output-root");

    Value zeroBoundValue = MakeValue(markers[2], 125);
    zeroBoundValue.retirementBytes = 0;
    Expect(!source.TrySet(zeroBoundValue, NewMemory(), {}, output), "x.transaction.zero-retirement-bound-refused");
    Expect(output.RootIdentityForTest() == outputRoot, "x.transaction.zero-bound-keeps-output");

    CancelSource preCancelled;
    static_cast<void>(preCancelled.Cancel());
    Expect(!source.TrySet(ownerlessValue, NewMemory(), preCancelled.Token(), output),
           "x.transaction.pre-cancelled-update-refused");
    Expect(source.RootIdentityForTest() == sourceRoot, "x.transaction.pre-cancel-keeps-source-root");
    Expect(output.RootIdentityForTest() == outputRoot, "x.transaction.pre-cancel-keeps-output");

    CancelSource duringBuild;
    CancelAtVisit cancelState{&duringBuild, 3, 0};
    Index::Observer cancelObserver{&cancelState, &CancelAtVisit::Visit, nullptr};
    Expect(!source.TrySet(ownerlessValue, NewMemory(), duringBuild.Token(), output, cancelObserver),
           "x.transaction.mid-build-cancellation-refused");
    Expect(cancelState.count == cancelState.target, "x.transaction.mid-build-cancellation-reached");
    Expect(source.RootIdentityForTest() == sourceRoot, "x.transaction.mid-cancel-keeps-source-root");
    Expect(output.RootIdentityForTest() == outputRoot, "x.transaction.mid-cancel-keeps-output-root");

    Value overflowValue = MakeValue(markers[2], 126);
    overflowValue.retirementBytes = (std::numeric_limits<std::uint64_t>::max)();
    Expect(!source.TrySet(overflowValue, NewMemory(), {}, output), "x.transaction.retirement-overflow-refused");
    Expect(source.RootIdentityForTest() == sourceRoot, "x.transaction.overflow-keeps-source-root");
    Expect(output.RootIdentityForTest() == outputRoot, "x.transaction.overflow-keeps-output");

    Index::Observer throwingObserver{};
    throwingObserver.allocating = &ThrowAtAllocation::Run;
    bool propagated = false;
    try
    {
        static_cast<void>(source.TrySet(ownerlessValue, NewMemory(), {}, output, throwingObserver));
    }
    catch (const std::bad_alloc&)
    {
        propagated = true;
    }
    Expect(propagated, "x.transaction.observer-bad-alloc-propagates");
    Expect(source.RootIdentityForTest() == sourceRoot, "x.transaction.bad-alloc-keeps-source-root");
    Expect(output.RootIdentityForTest() == outputRoot, "x.transaction.bad-alloc-keeps-output-root");
    const Value* const retainedSourceValue = source.Find(markers[0].get());
    const Value* const retainedOutputValue = output.Find(markers[1].get());
    Expect(retainedSourceValue != nullptr, "x.transaction.bad-alloc-keeps-source-value");
    Expect(retainedOutputValue != nullptr, "x.transaction.bad-alloc-keeps-output-value");
    Expect(retainedSourceValue && retainedSourceValue->payload == sourceValue.payload,
           "x.transaction.bad-alloc-keeps-source-payload");
    Expect(retainedOutputValue && retainedOutputValue->payload == outputValue.payload,
           "x.transaction.bad-alloc-keeps-output-payload");
}

void RunPublicationLifetime()
{
    auto memory = NewMemory();
    std::weak_ptr<const SharedPublicationMemory> weakMemory{memory};
    auto marker = MakePublicationObject<Marker>(memory, kLiteralValueBytes, 9001);
    std::shared_ptr<const Marker> strongMarker = marker;
    std::weak_ptr<const Marker> weakMarker{strongMarker};
    const void* const ownedKeyAddress = strongMarker.get();
    Value value = MakeValue(strongMarker, 9002);

    Index first;
    Index published;
    const bool set = first.TrySet(value, memory, {}, published);
    first = std::move(published);
    Index copiedVersion = first;
    memory.reset();
    marker.reset();
    strongMarker.reset();
    value.marker.reset();

    const Value* const retained = copiedVersion.Find(ownedKeyAddress);
    Expect(set, "x.lifetime.set-succeeds");
    Expect(retained != nullptr, "x.lifetime.key-is-findable-after-external-owner-drop");
    Expect(retained && retained->marker.get() == ownedKeyAddress, "x.lifetime.index-value-strongly-owns-key");
    Expect(retained && retained->payload == value.payload, "x.lifetime.payload-survives-external-owner-drop");
    Expect(!weakMarker.expired(), "x.lifetime.copied-version-retains-marker");
    Expect(!weakMemory.expired(), "x.lifetime.copied-version-retains-memory");

    first = {};
    Expect(!weakMarker.expired(), "x.lifetime.copied-version-outlives-original-version");
    Expect(!weakMemory.expired(), "x.lifetime.copied-version-retains-memory-after-original-drop");
    copiedVersion = {};
    Expect(weakMarker.expired(), "x.lifetime.dropping-last-version-expires-marker");
    Expect(!weakMemory.expired(), "x.lifetime.expired-weak-publication-control-retains-memory");
    weakMarker.reset();
    Expect(weakMemory.expired(), "x.lifetime.last-weak-publication-control-releases-memory");
}
void RunMainAdditionalTransactions(const std::vector<std::shared_ptr<const Marker>>& markers)
{
    Index source;
    bool seeded = true;
    for (const auto& marker : markers)
        seeded = Insert(source, MakeValue(marker, marker->id + 200)) && seeded;
    Expect(seeded, "x.main.partial-fault-source-seeded");
    Index output;
    Expect(Insert(output, MakeValue(markers[0], 900)), "x.main.partial-fault-output-seeded");
    const auto sourceRoot = source.RootIdentityForTest();
    const auto outputRoot = output.RootIdentityForTest();
    auto freshMarker = std::make_shared<Marker>(3000);
    const auto freshValue = MakeValue(freshMarker, 4000);

    struct PartialFault final
    {
        std::size_t count = 0;
        std::size_t target = 0;
        static void Allocate(void* context)
        {
            auto& fault = *static_cast<PartialFault*>(context);
            if (++fault.count == fault.target)
                throw std::bad_alloc{};
        }
    };
    for (std::size_t target = 1; target <= 5; ++target)
    {
        PartialFault fault{0, target};
        bool propagated = false;
        try
        {
            static_cast<void>(
                source.TrySet(freshValue, NewMemory(), {}, output, {&fault, nullptr, &PartialFault::Allocate}));
        }
        catch (const std::bad_alloc&)
        {
            propagated = true;
        }
        Expect(propagated && fault.count == target, "x.main.partial-allocation-fault-propagates");
        Expect(source.RootIdentityForTest() == sourceRoot && source.size() == kKeyCount,
               "x.main.partial-fault-preserves-source");
        Expect(output.RootIdentityForTest() == outputRoot && output.size() == 1,
               "x.main.partial-fault-preserves-output");
        Expect(source.Find(freshMarker.get()) == nullptr, "x.main.partial-fault-does-not-publish-new-owner");
    }

    Expect(!source.TryErase(nullptr, NewMemory(), {}, output), "x.main.null-erase-key-refuses");
    Expect(output.RootIdentityForTest() == outputRoot, "x.main.null-erase-keeps-output");
    CancelSource eraseCancellation;
    CancelAtVisit eraseState{&eraseCancellation, 3, 0};
    const auto ordered = AddressOrder(markers);
    Expect(!source.TryErase(markers[ordered.front()].get(), NewMemory(), eraseCancellation.Token(), output,
                            {&eraseState, &CancelAtVisit::Visit, nullptr}),
           "x.main.mid-erase-cancellation-refuses");
    Expect(eraseState.count == eraseState.target, "x.main.mid-erase-cancellation-reached");
    Expect(source.RootIdentityForTest() == sourceRoot && output.RootIdentityForTest() == outputRoot,
           "x.main.mid-erase-preserves-both-roots");

    Index inPlace = source;
    Expect(inPlace.TrySet(freshValue, NewMemory(), {}, inPlace), "x.main.in-place-update-accepted");
    const auto* found = inPlace.Find(freshMarker.get());
    Expect(found && found->payload == 4000 && inPlace.size() == kKeyCount + 1, "x.main.in-place-update-literal-value");
    Expect(inPlace.TryErase(freshMarker.get(), NewMemory(), {}, inPlace), "x.main.in-place-erase-accepted");
    Expect(!inPlace.Find(freshMarker.get()) && inPlace.size() == kKeyCount, "x.main.in-place-erase-literal-state");
    Expect(source.RootIdentityForTest() == sourceRoot && source.Find(freshMarker.get()) == nullptr,
           "x.main.in-place-operation-preserves-old-version");

    auto memory = NewMemory();
    std::shared_ptr<const SharedPublicationMemory> owningNull{memory, nullptr};
    Index aliasOutput;
    Expect(source.TrySet(freshValue, owningNull, {}, aliasOutput), "x.main.owning-null-accounting-alias-accepted");
    Expect(aliasOutput.Find(freshMarker.get()) && aliasOutput.size() == kKeyCount + 1,
           "x.main.owning-null-alias-publishes-value");

    Index empty;
    std::size_t emptyVisits = 0;
    Expect(empty.Visit(
               [&](const void*, const Value&)
               {
                   ++emptyVisits;
                   return true;
               }),
           "x.main.empty-visit-completes");
    Expect(emptyVisits == 0, "x.main.empty-visit-calls-no-visitor");
}

void RunMainOriginalOwnerSharing()
{
    auto firstMemory = NewMemory();
    auto secondMemory = NewMemory();
    std::weak_ptr<const SharedPublicationMemory> firstWeak{firstMemory};
    std::weak_ptr<const SharedPublicationMemory> secondWeak{secondMemory};
    auto firstMarker = std::make_shared<Marker>(7000);
    auto secondMarker = std::make_shared<Marker>(7001);
    Index older;
    Index newer;
    Expect(older.TrySet(MakeValue(firstMarker, 1), firstMemory, {}, newer),
           "x.main.original-owner-first-value-published");
    older = std::move(newer);
    Expect(older.TrySet(MakeValue(secondMarker, 2), firstMemory, {}, newer),
           "x.main.original-owner-second-value-published");
    older = std::move(newer);
    // The first inserted key is the two-node root regardless of address ordering. Replacing that key
    // copies the root on the second owner and shares the other original leaf on the first owner.
    Expect(older.TrySet(MakeValue(firstMarker, 3), secondMemory, {}, newer), "x.main.path-copy-new-owner-published");
    const auto* oldValue = older.Find(firstMarker.get());
    const auto* newValue = newer.Find(firstMarker.get());
    Expect(oldValue && newValue && oldValue->payload == 1 && newValue->payload == 3,
           "x.main.old-new-payloads-independent");
    firstMemory.reset();
    secondMemory.reset();
    older = {};
    Expect(!firstWeak.expired(), "x.main.shared-original-leaf-retains-first-owner");
    Expect(!secondWeak.expired(), "x.main.new-root-retains-second-owner");
    const auto* sharedLeaf = newer.Find(secondMarker.get());
    Expect(sharedLeaf && sharedLeaf->payload == 2, "x.main.shared-original-leaf-value-retained");
    newer = {};
    Expect(firstWeak.expired(), "x.main.last-shared-leaf-releases-first-owner");
    Expect(secondWeak.expired(), "x.main.last-new-root-releases-second-owner");
}

} // namespace

int RunPersistentOwnerIndexTests()
{
    gFailures = 0;
    gControls = 0;
    const auto markers = MakeMarkers();
    const auto ascending = AddressOrder(markers);
    auto descending = ascending;
    std::reverse(descending.begin(), descending.end());
    auto shuffled = ascending;
    ShuffleDeterministically(shuffled);

    Expect(markers.size() >= kKeyCount, "x.setup-at-least-1024-strong-keys");
    RotationPrelude(markers, ascending);
    RunAddressPattern(markers, shuffled, "x.pattern.shuffled-1024-plus-keys");
    RunAddressPattern(markers, ascending, "x.pattern.ascending-address-1024-plus-keys");
    RunAddressPattern(markers, descending, "x.pattern.descending-address-1024-plus-keys");
    RunInputRefusalAndFaultTransactions(markers);
    RunPublicationLifetime();
    RunMainAdditionalTransactions(markers);
    RunMainOriginalOwnerSharing();
    std::printf("%s persistent-owner-index.summary passed=%d failed=%d controls=%d\n", gFailures ? "FAIL" : "PASS",
                gControls - gFailures, gFailures, gControls);
    return gFailures;
}
