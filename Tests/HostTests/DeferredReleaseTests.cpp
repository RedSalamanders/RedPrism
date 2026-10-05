#include "DeferredReleaseTests.h"

#include "../../Common/DeferredRelease.h"

#include <atomic>
#include <cstdio>
#include <thread>
#include <type_traits>

namespace
{
int g_failures = 0;

void Expect(bool condition, const char* name) noexcept
{
    std::printf("%s deferred-release.%s\n", condition ? "PASS" : "FAIL", name);
    if (!condition)
    {
        ++g_failures;
    }
}

struct Payload final
{
    DeferredReleaseNode node;
    std::uint64_t marker = 0;
    std::atomic<int> destroyed{0};
    std::uint32_t alignmentPadding = 0;
};

static_assert(std::is_standard_layout_v<Payload>);
static_assert(offsetof(Payload, node) == 0);

// Metadata-only simulation: count destruction without freeing or touching a real raster allocation.
void DestroyPayload(DeferredReleaseNode* node) noexcept
{
    auto* payload = reinterpret_cast<Payload*>(node);
    payload->destroyed.fetch_add(1, std::memory_order_relaxed);
}

struct FixedSink final
{
    DeferredReleaseNode* record = nullptr;
    int enqueueCount = 0;
};

void EnqueueOne(void* context, DeferredReleaseNode& node) noexcept
{
    auto& sink = *static_cast<FixedSink*>(context);
    if (sink.record == nullptr)
    {
        sink.record = &node;
    }
    ++sink.enqueueCount;
}

struct SinkScope final
{
    FixedSink sink;
    bool installed = false;

    SinkScope() noexcept : installed(InstallDeferredReleaseSink(EnqueueOne, &sink)) {}
    ~SinkScope()
    {
        if (installed)
        {
            (void)ClearDeferredReleaseSink(&sink);
        }
    }

    SinkScope(const SinkScope&) = delete;
    SinkScope& operator=(const SinkScope&) = delete;
};

void TestOwnershipAndThreshold()
{
    SinkScope scope;
    Expect(scope.installed, "install-sink");
    if (!scope.installed)
    {
        return;
    }

    Payload below;
    below.node.bytes = kDeferredReleaseThreshold - 1;
    below.node.destroy = DestroyPayload;
    below.marker = 0xBEEFULL;
    DeferredReleaseNode* const belowAddress = &below.node;
    const bool belowAccepted = TryDeferRelease(below.node);
    Expect(!belowAccepted && below.destroyed.load() == 0 && scope.sink.enqueueCount == 0,
           "below-threshold-retains-caller-ownership");
    if (!belowAccepted)
    {
        below.node.destroy(&below.node);
    }
    Expect(below.destroyed.load() == 1 && below.marker == 0xBEEFULL && &below.node == belowAddress,
           "below-threshold-inline-destroy-preserves-metadata");

    Payload exact;
    exact.node.bytes = kDeferredReleaseThreshold;
    exact.node.destroy = DestroyPayload;
    exact.marker = 0x12345678ULL;
    DeferredReleaseNode* const exactAddress = &exact.node;
    const bool exactAccepted = TryDeferRelease(exact.node);
    Expect(exactAccepted && scope.sink.record == exactAddress && scope.sink.enqueueCount == 1 &&
               exact.destroyed.load() == 0 && exact.marker == 0x12345678ULL,
           "threshold-accepted-node-and-payload-unchanged");
    // Fake consumer ownership ends only at this explicit manual drain.
    if (scope.sink.record != nullptr)
    {
        scope.sink.record->destroy(scope.sink.record);
        scope.sink.record = nullptr;
    }
    Expect(exact.destroyed.load() == 1 && &exact.node == exactAddress && exact.marker == 0x12345678ULL,
           "fake-consumer-drain-destroys-once-stable-node");

    Payload above;
    above.node.bytes = kDeferredReleaseThreshold + 1;
    above.node.destroy = DestroyPayload;
    const bool aboveAccepted = TryDeferRelease(above.node);
    Expect(aboveAccepted && scope.sink.enqueueCount == 2 && scope.sink.record == &above.node &&
               above.destroyed.load() == 0,
           "above-threshold-accepted-consumer-owns-until-drain");
    if (scope.sink.record != nullptr)
    {
        scope.sink.record->destroy(scope.sink.record);
        scope.sink.record = nullptr;
    }
    Expect(above.destroyed.load() == 1, "above-threshold-fake-consumer-drain-destroys-once");

    Expect(ClearDeferredReleaseSink(&scope.sink), "clear-owner-sink");
    scope.installed = false;
}

void TestRefusalsAndInstallRules()
{
    Payload payload;
    payload.node.bytes = kDeferredReleaseThreshold;
    payload.node.destroy = DestroyPayload;

    Expect(!TryDeferRelease(payload.node), "missing-sink-refuses");
    Expect(!InstallDeferredReleaseSink(nullptr, &payload), "missing-enqueue-refused");
    Expect(!InstallDeferredReleaseSink(EnqueueOne, nullptr), "missing-context-refused");

    SinkScope scope;
    Expect(scope.installed, "install-for-refusal-cases");
    if (!scope.installed)
    {
        payload.node.destroy(&payload.node);
        return;
    }

    DeferredReleaseNode missingDestroy;
    missingDestroy.bytes = kDeferredReleaseThreshold;
    Expect(!TryDeferRelease(missingDestroy) && scope.sink.enqueueCount == 0, "missing-destroy-refuses-without-enqueue");
    Expect(!InstallDeferredReleaseSink(EnqueueOne, &payload) && scope.sink.enqueueCount == 0,
           "duplicate-install-rejected-without-replacing-original");
    const bool queued = TryDeferRelease(payload.node);
    Expect(!ClearDeferredReleaseSink(&payload) && queued && scope.sink.record == &payload.node &&
               scope.sink.enqueueCount == 1,
           "wrong-context-clear-keeps-original-sink");
    // Release the fake consumer's ownership before clearing the sink or leaving this scope.
    if (scope.sink.record == &payload.node)
    {
        scope.sink.record->destroy(scope.sink.record);
        scope.sink.record = nullptr;
    }
    Expect(payload.destroyed.load() == 1, "queued-refusal-fixture-drained-exactly-once");
    (void)ClearDeferredReleaseSink(&scope.sink);
    scope.installed = false;

    Payload afterClear;
    afterClear.node.bytes = kDeferredReleaseThreshold;
    afterClear.node.destroy = DestroyPayload;
    Expect(!TryDeferRelease(afterClear.node), "clear-restores-inline-eligibility");
    afterClear.node.destroy(&afterClear.node);
    Expect(afterClear.destroyed.load() == 1, "fresh-post-clear-payload-inline-destroy-once");
}

void TestExplicitOwnerPolicyKeepsDeclaredBytesAndThreadIsolation()
{
    Payload ownerPayload;
    ownerPayload.node.bytes = 7;
    ownerPayload.node.destroy = DestroyPayload;
    Expect(!TryDeferRelease(ownerPayload.node, DeferredReleasePolicy::AlwaysOnOwner),
           "always-owner-missing-sink-retains-caller-ownership");
    SinkScope scope;
    Expect(scope.installed, "always-owner-installs-exact-owner-sink");
    if (!scope.installed)
        return;
    Expect(!TryDeferRelease(ownerPayload.node) && scope.sink.enqueueCount == 0,
           "always-owner-default-policy-still-refuses-small-node");
    const bool deferred = TryDeferRelease(ownerPayload.node, DeferredReleasePolicy::AlwaysOnOwner);
    Expect(deferred && scope.sink.record == &ownerPayload.node && scope.sink.enqueueCount == 1 &&
               ownerPayload.node.bytes == 7 && ownerPayload.destroyed.load() == 0,
           "always-owner-hands-off-small-node-with-original-byte-declaration");
    if (deferred)
    {
        scope.sink.record->destroy(scope.sink.record);
        scope.sink.record = nullptr;
    }
    Expect(ownerPayload.destroyed.load() == 1, "always-owner-consumer-destroys-small-node-once");
    DeferredReleaseNode incomplete;
    incomplete.bytes = 9;
    Expect(!TryDeferRelease(incomplete, DeferredReleasePolicy::AlwaysOnOwner) && scope.sink.enqueueCount == 1,
           "always-owner-missing-destroy-still-refuses-without-enqueue");
    Payload workerPayload;
    workerPayload.node.bytes = 11;
    workerPayload.node.destroy = DestroyPayload;
    std::atomic<bool> workerDeferred{true};
    std::thread worker(
        [&]() noexcept
        {
            workerDeferred.store(TryDeferRelease(workerPayload.node, DeferredReleasePolicy::AlwaysOnOwner));
            if (!workerDeferred.load())
                workerPayload.node.destroy(&workerPayload.node);
        });
    worker.join();
    Expect(!workerDeferred.load() && workerPayload.destroyed.load() == 1 && scope.sink.enqueueCount == 1 &&
               scope.sink.record == nullptr,
           "always-owner-worker-cannot-inherit-owner-sink-and-destroys-inline");
    Expect(ClearDeferredReleaseSink(&scope.sink), "always-owner-clears-only-after-worker-join");
    scope.installed = false;
    Payload afterClear;
    afterClear.node.bytes = 13;
    afterClear.node.destroy = DestroyPayload;
    Expect(!TryDeferRelease(afterClear.node, DeferredReleasePolicy::AlwaysOnOwner) && afterClear.destroyed.load() == 0,
           "always-owner-after-clear-refuses-with-caller-ownership-preserved");
    afterClear.node.destroy(&afterClear.node);
}

void TestThreadIsolation()
{
    SinkScope scope;
    Expect(scope.installed, "install-owner-thread-sink");
    if (!scope.installed)
    {
        return;
    }

    Payload workerPayload;
    workerPayload.node.bytes = kDeferredReleaseThreshold;
    workerPayload.node.destroy = DestroyPayload;
    std::atomic<bool> workerDeferred{true};
    std::thread worker(
        [&]() noexcept
        {
            workerDeferred.store(TryDeferRelease(workerPayload.node), std::memory_order_relaxed);
            if (!workerDeferred.load(std::memory_order_relaxed))
            {
                workerPayload.node.destroy(&workerPayload.node);
            }
        });
    worker.join();

    Expect(!workerDeferred.load(std::memory_order_relaxed) && workerPayload.destroyed.load() == 1 &&
               scope.sink.enqueueCount == 0 && scope.sink.record == nullptr,
           "worker-thread-cannot-see-ui-sink-destroys-inline");
    Expect(ClearDeferredReleaseSink(&scope.sink), "clear-after-worker-join");
    scope.installed = false;
}
} // namespace

int RunDeferredReleaseTests()
{
    g_failures = 0;
    TestOwnershipAndThreshold();
    TestRefusalsAndInstallRules();
    TestThreadIsolation();
    TestExplicitOwnerPolicyKeepsDeclaredBytesAndThreadIsolation();
    return g_failures;
}
