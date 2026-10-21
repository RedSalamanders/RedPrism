#include "Document.Tests.WandBooleanWorkspace.h"

#include "BoundedClipper/Workspace.h"
#include "Executor.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <new>
#include <stop_token>

namespace
{
using RedPrismBoundedClipper::ArenaObject;
using RedPrismBoundedClipper::ArenaVector;
using RedPrismBoundedClipper::Workspace;

void Expect(bool condition, const char* name, int& failures) noexcept
{
    std::printf("%s workspace.%s\n", condition ? "PASS" : "FAIL", name);
    failures += condition ? 0 : 1;
}

struct LifetimeCounts final
{
    int constructed = 0;
    int destroyed = 0;
    int childDestroyed = 0;
    int parentDestroyed = 0;
};

class TrackedObject final : public ArenaObject<TrackedObject>
{
  public:
    TrackedObject(LifetimeCounts& counts, std::uint32_t payload) noexcept : payload(payload), _counts(counts)
    {
        ++_counts.constructed;
    }
    ~TrackedObject() noexcept
    {
        ++_counts.destroyed;
    }

    std::uint32_t payload;

  private:
    LifetimeCounts& _counts;
};

class TrackedChild final : public ArenaObject<TrackedChild>
{
  public:
    explicit TrackedChild(LifetimeCounts& counts) noexcept : _counts(counts) {}
    ~TrackedChild() noexcept
    {
        ++_counts.childDestroyed;
    }

  private:
    LifetimeCounts& _counts;
};

class ThrowingParent final : public ArenaObject<ThrowingParent>
{
  public:
    explicit ThrowingParent(LifetimeCounts& counts) : _counts(counts)
    {
        (void)new TrackedChild(counts);
        throw std::bad_alloc{};
    }
    ~ThrowingParent() noexcept
    {
        ++_counts.parentDestroyed;
    }

  private:
    LifetimeCounts& _counts;
};

bool DisjointAlignedAllocations()
{
    Workspace workspace{1024, {}};
    constexpr std::size_t firstSize = 13;
    constexpr std::size_t secondSize = 29;
    auto* first = static_cast<std::byte*>(Workspace::Allocate(firstSize, alignof(std::byte)));
    auto* second = static_cast<std::byte*>(Workspace::Allocate(secondSize, alignof(std::byte)));
    first[0] = std::byte{0x31};
    first[firstSize - 1] = std::byte{0x7a};
    second[0] = std::byte{0x42};
    second[secondSize - 1] = std::byte{0x6b};

    const auto firstAddress = reinterpret_cast<std::uintptr_t>(first);
    const auto secondAddress = reinterpret_cast<std::uintptr_t>(second);
    const bool aligned = firstAddress % Workspace::Alignment == 0 && secondAddress % Workspace::Alignment == 0;
    const bool disjoint = firstAddress + firstSize <= secondAddress || secondAddress + secondSize <= firstAddress;
    return aligned && disjoint && first[0] == std::byte{0x31} && first[firstSize - 1] == std::byte{0x7a} &&
           second[0] == std::byte{0x42} && second[secondSize - 1] == std::byte{0x6b};
}

bool ExactCapacityAndRefusal()
{
    Workspace workspace{256, {}};
    bool refusedAtCapacity = false;
    try
    {
        // The fixture input equals the configured capacity; it does not estimate allocator metadata or padding.
        (void)Workspace::Allocate(256, alignof(std::byte));
    }
    catch (const std::bad_alloc&)
    {
        refusedAtCapacity = true;
    }
    const Workspace::Statistics statistics = workspace.GetStatistics();
    return refusedAtCapacity && statistics.allocations == 0;
}

bool InvalidAlignmentRefused()
{
    Workspace workspace{512, {}};
    bool refused = false;
    try
    {
        (void)Workspace::Allocate(8, 3);
    }
    catch (const std::bad_alloc&)
    {
        refused = true;
    }
    return refused;
}

bool PreCancelledConstructionRefused()
{
    std::stop_source source;
    (void)source.request_stop();
    bool refused = false;
    try
    {
        Workspace workspace{256, CancelToken{source.get_token()}};
        (void)workspace;
    }
    catch (const std::bad_alloc&)
    {
        refused = true;
    }
    return refused;
}

struct CheckpointState final
{
    std::stop_source* source = nullptr;
    std::uint64_t observedUnits = 0;
    unsigned int calls = 0;
};

void CancelAtCheckpoint(void* context, std::uint64_t workUnits) noexcept
{
    auto& state = *static_cast<CheckpointState*>(context);
    state.observedUnits = workUnits;
    ++state.calls;
    (void)state.source->request_stop();
}

bool CancellationAtHeldMutationCheckpoint()
{
    LifetimeCounts counts;
    std::stop_source source;
    CheckpointState checkpoint{&source};
    bool refusedAtBoundary = false;
    bool mutationRetainedUntilScopeExit = false;
    {
        Workspace workspace{1024, CancelToken{source.get_token()}, CancelAtCheckpoint, &checkpoint};
        auto* object = new TrackedObject(counts, 0x6d2aU);
        for (std::uint64_t index = 0; index < Workspace::Quantum - 2; ++index)
            Workspace::Pulse();

        object->payload = 0x91c4U;
        try
        {
            Workspace::Pulse();
        }
        catch (const std::bad_alloc&)
        {
            refusedAtBoundary = Workspace::IsAborted();
        }
        mutationRetainedUntilScopeExit = object->payload == 0x91c4U && counts.destroyed == 0;
    }
    return refusedAtBoundary && mutationRetainedUntilScopeExit && checkpoint.calls == 1 &&
           checkpoint.observedUnits == Workspace::Quantum && counts.constructed == 1 && counts.destroyed == 1;
}

bool ConstructorFailureCleansOwnedChild()
{
    LifetimeCounts counts;
    bool constructorFailed = false;
    {
        Workspace workspace{1024, {}};
        try
        {
            (void)new ThrowingParent(counts);
        }
        catch (const std::bad_alloc&)
        {
            constructorFailed = true;
        }
        // The failed parent's matching delete retires its raw allocation. Its successfully constructed child
        // remains independently owned by the workspace and is cleaned when this scope closes.
        if (counts.childDestroyed != 0 || counts.destroyed != 0 || counts.parentDestroyed != 0)
            return false;
    }
    return constructorFailed && counts.childDestroyed == 1 && counts.constructed == 0 && counts.destroyed == 0 &&
           counts.parentDestroyed == 0;
}

bool DeletedObjectNotDestroyedAgain()
{
    LifetimeCounts counts;
    {
        Workspace workspace{512, {}};
        auto* object = new TrackedObject(counts, 0x52a1U);
        delete object;
        if (counts.constructed != 1 || counts.destroyed != 1)
            return false;
    }
    return counts.constructed == 1 && counts.destroyed == 1;
}

bool DeletedObjectLeavesNoLiveWorkspaceObjects()
{
    LifetimeCounts counts;
    Workspace workspace{512, {}};
    auto* object = new TrackedObject(counts, 0x36f0U);
    delete object;
    const Workspace::Statistics statistics = workspace.GetStatistics();
    return statistics.liveObjects == 0 && counts.constructed == 1 && counts.destroyed == 1;
}

bool ArenaVectorGrowthPreservesValues()
{
    Workspace workspace{16 * 1024, {}};
    ArenaVector<std::uint32_t> values;
    for (std::uint32_t index = 0; index != 97; ++index)
        values.push_back(index * 37U + 11U);

    if (values.size() != 97)
        return false;
    for (std::uint32_t index = 0; index != 97; ++index)
        if (values[index] != index * 37U + 11U)
            return false;
    return true;
}

bool RepeatedScopeReleaseBalancesDestructors()
{
    LifetimeCounts counts;
    constexpr int scopeCount = 9;
    for (int index = 0; index != scopeCount; ++index)
    {
        Workspace workspace{512, {}};
        (void)new TrackedObject(counts, static_cast<std::uint32_t>(0x4000 + index));
        if (counts.constructed != index + 1 || counts.destroyed != index)
            return false;
    }
    return counts.constructed == scopeCount && counts.destroyed == scopeCount;
}

bool MultipleOwnedObjectsReleaseToZeroOutstanding()
{
    LifetimeCounts counts;
    {
        Workspace workspace{1024, {}};
        (void)new TrackedObject(counts, 0x101U);
        (void)new TrackedObject(counts, 0x202U);
        (void)new TrackedObject(counts, 0x303U);
        if (counts.constructed != 3 || counts.destroyed != 0 || workspace.GetStatistics().liveObjects != 3)
            return false;
    }
    return counts.constructed == 3 && counts.destroyed == 3;
}

bool RawBufferBytesDoNotBecomeOwnedObjects()
{
    Workspace workspace{512, {}};
    auto* bytes = static_cast<std::byte*>(Workspace::Allocate(4, alignof(std::byte)));
    bytes[0] = std::byte{0x11};
    bytes[1] = std::byte{0x22};
    bytes[2] = std::byte{0x33};
    bytes[3] = std::byte{0x44};
    const Workspace::Statistics statistics = workspace.GetStatistics();
    return statistics.allocations == 1 && statistics.liveObjects == 0 && bytes[0] == std::byte{0x11} &&
           bytes[1] == std::byte{0x22} && bytes[2] == std::byte{0x33} && bytes[3] == std::byte{0x44};
}

template <class Test> void RunCase(Test test, const char* name, int& failures) noexcept
{
    bool passed = false;
    try
    {
        passed = test();
    }
    catch (const std::bad_alloc&)
    {
        // Any bad_alloc not consumed by a scenario's expected-refusal boundary is a fixture failure.
    }
    Expect(passed, name, failures);
}
} // namespace

int RunDocumentWandWorkspaceTests()
{
    int failures = 0;
    RunCase(DisjointAlignedAllocations, "allocation.disjoint-aligned-and-preserved", failures);
    RunCase(ExactCapacityAndRefusal, "allocation.request-equal-to-capacity-refused", failures);
    RunCase(InvalidAlignmentRefused, "allocation.invalid-alignment-refused", failures);
    RunCase(PreCancelledConstructionRefused, "cancellation.pre-cancelled-construction-refused", failures);
    RunCase(CancellationAtHeldMutationCheckpoint, "cancellation.checkpoint-aborts-held-mutation-scope", failures);
    RunCase(ConstructorFailureCleansOwnedChild, "ownership.constructor-failure-cleans-owned-child", failures);
    RunCase(DeletedObjectNotDestroyedAgain, "ownership.deleted-object-not-destroyed-twice", failures);
    RunCase(DeletedObjectLeavesNoLiveWorkspaceObjects, "ownership.deleted-object-zero-live-count", failures);
    RunCase(ArenaVectorGrowthPreservesValues, "container.vector-growth-preserves-literal-values", failures);
    RunCase(RawBufferBytesDoNotBecomeOwnedObjects, "allocation.raw-buffer-is-not-an-owned-object", failures);
    RunCase(RepeatedScopeReleaseBalancesDestructors, "scope.repeated-release-balances-destructors", failures);
    RunCase(MultipleOwnedObjectsReleaseToZeroOutstanding, "scope.multiple-objects-release-to-zero-outstanding",
            failures);
    return failures;
}
