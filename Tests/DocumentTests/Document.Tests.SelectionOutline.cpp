#include "Document.Tests.SelectionOutline.h"

#include "RedPrism/SelectionOutlineWork.h"

#include "DeferredRelease.h"
#include "SharedPublicationMemory.h"
#include "WandLayerSelectionWork.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <memory>
#include <thread>
#include <utility>
#include <vector>

namespace
{
unsigned int gPassed = 0;
unsigned int gFailed = 0;

void Expect(bool condition, const char* name) noexcept
{
    std::printf("%s selection-outline.%s\n", condition ? "PASS" : "FAIL", name);
    condition ? ++gPassed : ++gFailed;
}

struct OutputOwner final
{
    OutputOwner() : owner(SharedPublicationMemory::CreateForWork()), weakOwner(owner) {}

    void ReleaseExternal() noexcept
    {
        owner.reset();
    }

    std::shared_ptr<const SharedPublicationMemory> owner;
    std::weak_ptr<const SharedPublicationMemory> weakOwner;
};

struct DeferredQueue final
{
    std::array<DeferredReleaseNode*, 4> nodes{};
    std::size_t count = 0;
    bool overflow = false;
    std::size_t destroyed = 0;
    std::thread::id destroyThread{};

    static void Enqueue(void* context, DeferredReleaseNode& node) noexcept
    {
        auto& queue = *static_cast<DeferredQueue*>(context);
        if (queue.count == queue.nodes.size())
        {
            queue.overflow = true;
            return;
        }
        queue.nodes[queue.count++] = &node;
    }
};

struct DeferredSinkScope final
{
    DeferredQueue queue;
    bool installed = false;

    DeferredSinkScope() noexcept : installed(InstallDeferredReleaseSink(DeferredQueue::Enqueue, &queue)) {}
    ~DeferredSinkScope()
    {
        if (installed)
            (void)ClearDeferredReleaseSink(&queue);
    }
    DeferredSinkScope(const DeferredSinkScope&) = delete;
    DeferredSinkScope& operator=(const DeferredSinkScope&) = delete;
};

void DestroyQueuedOnJoinedWorker(DeferredQueue& queue)
{
    // The sink is owner-thread TLS. The joined worker consumes only nodes already detached by that owner.
    std::thread worker(
        [&queue]() noexcept
        {
            queue.destroyThread = std::this_thread::get_id();
            for (std::size_t index = 0; index < queue.count; ++index)
            {
                DeferredReleaseNode* const node = queue.nodes[index];
                if (node && node->destroy)
                {
                    node->destroy(node);
                    ++queue.destroyed;
                }
                queue.nodes[index] = nullptr;
            }
        });
    worker.join();
    queue.count = 0;
}

std::vector<SelectionLoop> LiteralLoops()
{
    return {{{-7.25, 3.5}, {-6.875, 3.625}, {-6.5, 4.0}, {-7.125, 4.25}},
            {{-6.75, 3.75}, {-6.25, 3.5}, {-6.625, 4.125}, {-7.0, 4.25}}};
}

bool LiteralPointsMatch(const PreparedSelectionOutline::Loops& actual) noexcept
{
    constexpr std::array<std::array<float, 2>, 4> first{{
        {{0.0F, 0.0F}},
        {{0.375F, 0.125F}},
        {{0.75F, 0.5F}},
        {{0.125F, 0.75F}},
    }};
    constexpr std::array<std::array<float, 2>, 4> second{{
        {{0.5F, 0.25F}},
        {{1.0F, 0.0F}},
        {{0.625F, 0.625F}},
        {{0.25F, 0.75F}},
    }};
    if (actual.size() != 2 || actual[0].size() != first.size() || actual[1].size() != second.size())
        return false;
    for (std::size_t index = 0; index < first.size(); ++index)
    {
        if (actual[0][index].x != first[index][0] || actual[0][index].y != first[index][1] ||
            actual[1][index].x != second[index][0] || actual[1][index].y != second[index][1])
            return false;
    }
    return true;
}

void TestNumericPreparationAndSourceRetention()
{
    SelectionLoops source{LiteralLoops()};
    SelectionLoops witness = source;
    const void* const sourceIdentity = source.Identity();
    const auto* const outerBuffer = source.Get().data();
    const auto* const firstBuffer = source.Get()[0].data();
    const auto* const secondBuffer = source.Get()[1].data();
    const auto planBytes = PreparedSelectionOutline::OutputMemory(source);
    const auto planBytesByCounts = PreparedSelectionOutline::OutputMemory(source.size(), source.PointCount());
    OutputOwner output;
    const auto weakOwner = output.weakOwner;

    auto prepared = PreparedSelectionOutline::Prepare(std::move(source), output.owner);
    Expect(prepared.has_value(), "literal-two-loop-prepare-succeeds-with-publication-memory-owner");
    if (!prepared)
        return;

    Expect(prepared.value().HasStorage(), "literal-transport-reports-owned-storage");
    Expect(prepared.value().Anchor() == Vec2{-7.25, 3.5}, "literal-anchor-is-double-minimum");
    Expect(prepared.value().PointCount() == 8 && prepared.value().Points().size() == 2 &&
               prepared.value().Points()[0].size() == 4 && prepared.value().Points()[1].size() == 4,
           "literal-cached-count-is-eight-across-two-loops");
    Expect(planBytes.has_value() && prepared.value().DeclaredOutputBytes() == planBytes.value(),
           "literal-declared-output-bytes-equal-preparation-plan");
    Expect(planBytes.has_value() && planBytesByCounts.has_value() && planBytes.value() == planBytesByCounts.value(),
           "count-output-memory-matches-literal-source-plan");
    Expect(LiteralPointsMatch(prepared.value().Points()), "literal-eight-float-points-match-independent-fractions");
    Expect(prepared.value().SourceIdentity() == sourceIdentity,
           "prepared-retains-original-source-identity-after-caller-move");
    Expect(witness.Identity() == sourceIdentity && witness.Get().data() == outerBuffer &&
               witness.Get()[0].data() == firstBuffer && witness.Get()[1].data() == secondBuffer &&
               witness.Get()[0][1] == Vec2{-6.875, 3.625} && witness.Get()[1][2] == Vec2{-6.625, 4.125},
           "source-vector-buffers-and-literal-values-remain-unchanged");
    witness = {};
    output.ReleaseExternal();
    Expect(!weakOwner.expired() && prepared.value().SourceIdentity() == sourceIdentity,
           "prepared-retains-source-and-output-control-after-caller-reset");
}

void TestLargeCoordinateSubtraction()
{
    constexpr double originX = 1'000'000'000'000.0;
    constexpr double originY = -1'000'000'000'000.0;
    SelectionLoops source{std::vector<SelectionLoop>{
        {{originX, originY}, {originX + 0.125, originY + 0.375}, {originX + 0.25, originY + 0.5}}}};
    OutputOwner output;
    auto prepared = PreparedSelectionOutline::Prepare(std::move(source), output.owner);
    Expect(prepared.has_value(), "large-coordinate-prepare-succeeds");
    if (!prepared)
        return;

    const auto& points = prepared.value().Points();
    Expect(prepared.value().Anchor() == Vec2{originX, originY}, "large-coordinate-anchor-remains-double");
    Expect(points.size() == 1 && points[0].size() == 3 && points[0][0].x == 0.0F && points[0][0].y == 0.0F &&
               points[0][1].x == 0.125F && points[0][1].y == 0.375F && points[0][2].x == 0.25F &&
               points[0][2].y == 0.5F,
           "large-coordinate-double-subtraction-precedes-float-narrowing");
}

void TestEmptyAndDefaultValues()
{
    OutputOwner emptyLoopOutput;
    SelectionLoops emptyLoopSource{std::vector<SelectionLoop>{SelectionLoop{}}};
    const void* const emptyLoopIdentity = emptyLoopSource.Identity();
    auto emptyLoop = PreparedSelectionOutline::Prepare(std::move(emptyLoopSource), emptyLoopOutput.owner);
    Expect(emptyLoop.has_value() && emptyLoop.value().Points().size() == 1 && emptyLoop.value().Points()[0].empty() &&
               emptyLoop.value().PointCount() == 0 && emptyLoop.value().Anchor() == Vec2{} &&
               emptyLoop.value().SourceIdentity() == emptyLoopIdentity,
           "one-empty-loop-is-preserved-with-zero-anchor-and-count");

    OutputOwner emptySourceOutput;
    SelectionLoops emptySource;
    const auto emptyPlan = PreparedSelectionOutline::OutputMemory(emptySource);
    auto preparedEmptySource = PreparedSelectionOutline::Prepare(std::move(emptySource), emptySourceOutput.owner);
    Expect(preparedEmptySource.has_value() && preparedEmptySource.value().Points().empty() &&
               preparedEmptySource.value().SourceIdentity() == nullptr &&
               preparedEmptySource.value().PointCount() == 0 && emptyPlan.has_value() &&
               preparedEmptySource.value().DeclaredOutputBytes() == emptyPlan.value(),
           "empty-source-prepares-empty-transport-with-planned-bytes");

    const PreparedSelectionOutline defaultTransport;
    Expect(!defaultTransport.HasStorage(), "default-transport-reports-no-storage");
    Expect(defaultTransport.Points().empty() && defaultTransport.SourceIdentity() == nullptr &&
               defaultTransport.Anchor() == Vec2{} && defaultTransport.PointCount() == 0 &&
               defaultTransport.DeclaredOutputBytes() == 0 && defaultTransport.WeakStorageForTest().expired(),
           "default-transport-is-empty-and-unowned");
}

void TestEmptyOutputOwnerRefusalPreservesSource()
{
    SelectionLoops source{LiteralLoops()};
    const void* const identity = source.Identity();
    const auto* const outerBuffer = source.Get().data();
    const auto* const firstBuffer = source.Get()[0].data();
    const Vec2 firstPoint = source.Get()[0][0];
    const std::shared_ptr<const SharedPublicationMemory> emptyOwner;

    auto refused = PreparedSelectionOutline::Prepare(source, emptyOwner);
    Expect(!refused, "empty-output-owner-is-refused");
    Expect(source.Identity() == identity && source.Get().data() == outerBuffer &&
               source.Get()[0].data() == firstBuffer && source.Get()[0][0] == firstPoint && source.PointCount() == 8,
           "empty-owner-refusal-preserves-caller-source-and-buffers");
}

struct CancelAt64 final
{
    CancelSource* cancellation = nullptr;
    std::array<std::uint64_t, 4> offsets{};
    std::size_t count = 0;

    static void Checkpoint(void* context, std::uint64_t offset) noexcept
    {
        auto& state = *static_cast<CancelAt64*>(context);
        if (state.count < state.offsets.size())
            state.offsets[state.count++] = offset;
        if (offset == 64)
            (void)state.cancellation->Cancel();
    }
};

SelectionLoops LargeCancellationSource()
{
    SelectionLoop loop;
    loop.reserve(130);
    for (std::uint32_t index = 0; index < 130; ++index)
    {
        loop.push_back(Vec2{static_cast<double>(index) * 0.125, static_cast<double>(index % 7) * 0.25});
    }
    std::vector<SelectionLoop> loops;
    loops.push_back(std::move(loop));
    return SelectionLoops{std::move(loops)};
}

void TestCancellationAndRecovery()
{
    SelectionLoops source = LargeCancellationSource();
    const void* const identity = source.Identity();
    const auto* const outerBuffer = source.Get().data();
    const auto* const pointBuffer = source.Get()[0].data();

    CancelSource preCancelled;
    (void)preCancelled.Cancel();
    CancelAt64 unusedObserver;
    unusedObserver.cancellation = &preCancelled;
    OutputOwner preCancelOutput;
    auto preCancelResult =
        PreparedSelectionOutline::Prepare(source, preCancelOutput.owner, preCancelled.Token(),
                                          PreparedSelectionOutline::Observer{&unusedObserver, CancelAt64::Checkpoint});
    Expect(!preCancelResult && unusedObserver.count == 0, "pre-cancel-discards-before-observer-or-output-publication");
    Expect(source.Identity() == identity && source.Get().data() == outerBuffer &&
               source.Get()[0].data() == pointBuffer && source.PointCount() == 130,
           "pre-cancel-preserves-caller-input");
    preCancelOutput.ReleaseExternal();
    Expect(preCancelOutput.weakOwner.expired(), "pre-cancel-releases-unpublished-output-owner");

    CancelSource cancelAt64;
    CancelAt64 observer;
    observer.cancellation = &cancelAt64;
    OutputOwner cancelledOutput;
    auto cancelled =
        PreparedSelectionOutline::Prepare(source, cancelledOutput.owner, cancelAt64.Token(),
                                          PreparedSelectionOutline::Observer{&observer, CancelAt64::Checkpoint});
    Expect(!cancelled && observer.count == 2 && observer.offsets[0] == 0 && observer.offsets[1] == 64,
           "observer-cancels-at-exact-large-input-checkpoint-64-and-discards-output");
    Expect(source.Identity() == identity && source.Get().data() == outerBuffer &&
               source.Get()[0].data() == pointBuffer && source.PointCount() == 130,
           "checkpoint-cancellation-preserves-source-buffers");
    cancelledOutput.ReleaseExternal();
    Expect(cancelledOutput.weakOwner.expired(), "checkpoint-cancel-releases-unpublished-output-owner");

    OutputOwner recoveryOutput;
    auto recovered = PreparedSelectionOutline::Prepare(source, recoveryOutput.owner);
    Expect(recovered.has_value() && recovered.value().SourceIdentity() == identity &&
               recovered.value().PointCount() == 130 && recovered.value().Points().size() == 1 &&
               recovered.value().Points()[0].size() == 130,
           "fresh-prepare-recovers-after-checkpoint-cancellation");
}

void TestLastStrongAndWeakLifetime()
{
    DeferredSinkScope sink;
    Expect(sink.installed, "owner-thread-fixed-deferred-sink-installed");
    if (!sink.installed)
        return;

    SelectionLoops source{LiteralLoops()};
    const void* const sourceIdentity = source.Identity();
    OutputOwner output;
    const auto weakOwner = output.weakOwner;
    auto prepared = PreparedSelectionOutline::Prepare(std::move(source), output.owner);
    Expect(prepared.has_value(), "lifetime-fixture-prepares");
    if (!prepared)
        return;

    std::weak_ptr<const void> weakStorage = prepared.value().WeakStorageForTest();
    auto copy = prepared.value();
    Expect(copy.Points().data() == prepared.value().Points().data() &&
               copy.Points()[0].data() == prepared.value().Points()[0].data(),
           "immutable-copy-shares-outer-and-point-buffers");
    output.ReleaseExternal();
    prepared.reset();
    Expect(sink.queue.count == 0 && copy.SourceIdentity() == sourceIdentity,
           "copied-immutable-transport-keeps-final-strong-owner");

    copy = PreparedSelectionOutline{};
    Expect(!sink.queue.overflow && sink.queue.count == 1 && weakStorage.expired(),
           "final-owner-release-enqueues-one-node-and-expires-storage-weak");
    const bool sinkCleared = ClearDeferredReleaseSink(&sink.queue);
    Expect(sinkCleared, "owner-sink-cleared-before-worker-destruction");
    if (sinkCleared)
        sink.installed = false;
    DestroyQueuedOnJoinedWorker(sink.queue);
    Expect(sink.queue.destroyed == 1 && sink.queue.destroyThread != std::this_thread::get_id(),
           "final-publication-node-destroyed-by-real-joined-worker");
    Expect(!weakOwner.expired(), "expired-storage-weak-control-retains-publication-memory");
    weakStorage.reset();
    Expect(weakOwner.expired() && !sink.queue.overflow, "final-weak-reset-releases-publication-memory-control");
}

void TestOutputMemoryCountBounds()
{
    const auto emptyByCounts = PreparedSelectionOutline::OutputMemory(0, 0);
    const auto emptyBySource = PreparedSelectionOutline::OutputMemory(SelectionLoops{});
    Expect(emptyByCounts.has_value() && emptyBySource.has_value() && emptyByCounts.value() == emptyBySource.value(),
           "count-output-memory-matches-empty-source-plan");

    constexpr auto maximum = (std::numeric_limits<std::uint64_t>::max)();
    Expect(!PreparedSelectionOutline::OutputMemory(maximum, 0), "count-output-memory-refuses-loop-count-overflow");
    Expect(!PreparedSelectionOutline::OutputMemory(0, maximum), "count-output-memory-refuses-point-count-overflow");

    WandLayerSelectionMemoryPlan plan;
    Expect(!PreparedSelectionOutline::AdmissionMemory(plan), "admission-refuses-missing-trace-envelope");
    plan.geometryGrowthBytes = 4096;
    const auto trace = PreparedSelectionOutline::AdmissionMemory(plan);
    const auto rectangle = PreparedSelectionOutline::OutputMemory(1, 4);
    Expect(trace && rectangle && trace.value() >= rectangle.value(), "shortcut-envelope-covers-one-literal-rectangle");
    plan.booleanOutputBytes = 8192;
    const auto boolean = PreparedSelectionOutline::AdmissionMemory(plan);
    Expect(boolean && emptyByCounts && boolean.value() == emptyByCounts.value() + 8192,
           "boolean-envelope-dominates-trace-before-capture");
    plan.booleanOutputBytes = maximum;
    Expect(!PreparedSelectionOutline::AdmissionMemory(plan), "admission-refuses-node-and-buffer-overflow");
}
} // namespace

int RunSelectionOutlineTests()
{
    gPassed = 0;
    gFailed = 0;
    TestNumericPreparationAndSourceRetention();
    TestLargeCoordinateSubtraction();
    TestEmptyAndDefaultValues();
    TestEmptyOutputOwnerRefusalPreservesSource();
    TestCancellationAndRecovery();
    TestLastStrongAndWeakLifetime();
    TestOutputMemoryCountBounds();
    std::printf("%s selection-outline.summary passed=%u failed=%u controls=%u\n", gFailed == 0 ? "PASS" : "FAIL",
                gPassed, gFailed, gPassed + gFailed);
    return static_cast<int>(gFailed);
}
