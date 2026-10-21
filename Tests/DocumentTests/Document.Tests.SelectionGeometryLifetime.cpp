#include "Document.h"
#include "DeferredRelease.h"

#include <atomic>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <stop_token>
#include <thread>
#include <utility>
#include <vector>

namespace
{
void Expect(bool condition, const char* label, int& failures)
{
    std::cout << (condition ? "PASS " : "FAIL ") << label << '\n';
    failures += condition ? 0 : 1;
}

[[nodiscard]] std::vector<SelectionLoop> Rectangles()
{
    std::vector<SelectionLoop> loops;
    loops.reserve(2);
    loops.push_back({Vec2{0, 0}, Vec2{2, 0}, Vec2{2, 2}, Vec2{0, 2}});
    loops.push_back({Vec2{4, 4}, Vec2{6, 4}, Vec2{6, 6}, Vec2{4, 6}});
    return loops;
}

struct SingleNodeSink final
{
    DeferredReleaseNode* pending = nullptr;
    std::uint32_t enqueueCount = 0;
    bool overflow = false;

    static void Enqueue(void* context, DeferredReleaseNode& node) noexcept
    {
        auto& sink = *static_cast<SingleNodeSink*>(context);
        ++sink.enqueueCount;
        if (sink.pending)
        {
            sink.overflow = true;
            return;
        }
        sink.pending = &node;
    }
};

struct CancellationCheckpoint final
{
    std::stop_source* source = nullptr;
    std::uint64_t lastWork = (std::numeric_limits<std::uint64_t>::max)();
    std::uint32_t calls = 0;

    static void StopAtWork64(void* context, std::uint64_t work) noexcept
    {
        auto& checkpoint = *static_cast<CancellationCheckpoint*>(context);
        checkpoint.lastWork = work;
        ++checkpoint.calls;
        if (work == 64)
            static_cast<void>(checkpoint.source->request_stop());
    }
};

struct MarkerTrace final
{
    std::thread::id destructionThread{};
    std::atomic<std::uint32_t> destructionCount{0};
};

struct LifetimeMarker final
{
};

struct MarkerDeleter final
{
    MarkerTrace* trace = nullptr;

    void operator()(LifetimeMarker* marker) const noexcept
    {
        trace->destructionThread = std::this_thread::get_id();
        trace->destructionCount.fetch_add(1, std::memory_order_release);
        std::default_delete<LifetimeMarker>{}(marker);
    }
};

[[nodiscard]] std::shared_ptr<const LifetimeMarker> MakeLifetimeMarker(MarkerTrace& trace)
{
    std::unique_ptr<LifetimeMarker, MarkerDeleter> marker{std::make_unique<LifetimeMarker>().release(),
                                                          MarkerDeleter{&trace}};
    return std::shared_ptr<const LifetimeMarker>{std::move(marker)};
}

void RunValueControls(int& failures)
{
    SelectionLoops fallback;
    Expect(fallback.Identity() == nullptr && fallback.PointCount() == 0 && !fallback.Bounds().has_value() &&
               fallback.StorageBytes() == 0,
           "private-geometry.uncharged-fallback", failures);

    auto emptyLifetimeOwner = std::make_shared<int>(17);
    std::shared_ptr<const void> emptyLifetime{emptyLifetimeOwner};
    std::weak_ptr<int> weakEmptyLifetime{emptyLifetimeOwner};
    emptyLifetimeOwner.reset();
    auto empty = SelectionLoops::AdoptForWork(std::vector<SelectionLoop>{}, {}, emptyLifetime);
    Expect(empty.has_value(), "private-geometry.empty-adoption", failures);
    if (empty)
    {
        const SelectionLoops& geometry = empty.value();
        Expect(geometry.Identity() == nullptr && geometry.size() == 0 && geometry.PointCount() == 0 &&
                   !geometry.Bounds().has_value() && geometry.StorageBytes() == 0 &&
                   geometry.WeakStorageForTest().expired(),
               "private-geometry.empty-zero-metadata-no-owner", failures);
    }
    Expect(emptyLifetime.use_count() == 1 && !weakEmptyLifetime.expired(),
           "private-geometry.empty-does-not-retain-lifetime", failures);

    Expect(SelectionLoops::WorkOwnershipAllowance((std::numeric_limits<std::uint64_t>::max)()) == 0,
           "private-geometry.allowance-overflow-refused", failures);

    auto loops = Rectangles();
    const Vec2* const firstPoint = loops[0].data();
    const Vec2* const secondPoint = loops[1].data();
    auto adopted = SelectionLoops::AdoptForWork(std::move(loops));
    Expect(adopted.has_value(), "private-geometry.literal-adoption", failures);
    if (!adopted)
        return;

    SelectionLoops original = std::move(adopted.value());
    const bool adoptedPointers = original.size() == 2 && original[0].size() == 4 && original[1].size() == 4 &&
                                 original[0].data() == firstPoint && original[1].data() == secondPoint;
    Expect(adoptedPointers, "private-geometry.vector-storage-adopted", failures);
    Expect(original.Get() == Rectangles(), "private-geometry.all-eight-literal-vertices-preserved", failures);
    Expect(original.PointCount() == 8, "private-geometry.cached-point-count", failures);
    const auto bounds = original.Bounds();
    Expect(bounds.has_value() && bounds.value() == SelectionBounds{0, 0, 6, 6}, "private-geometry.cached-bounds",
           failures);

    SelectionLoops reader = original;
    const void* const identity = original.Identity();
    Expect(identity != nullptr && reader.Identity() == identity, "private-geometry.copy-identity", failures);
    original = SelectionLoops{};
    Expect(reader.Identity() == identity && reader.PointCount() == 8 && reader[1][3] == Vec2{4, 6},
           "private-geometry.reader-survives-publication-reset", failures);

    SelectionLoops moved = std::move(reader);
    Expect(reader.Identity() == nullptr && reader.PointCount() == 0 && !reader.Bounds().has_value() &&
               reader.StorageBytes() == 0 && moved.Identity() == identity && moved.PointCount() == 8,
           "private-geometry.move-empties-source", failures);
    moved = std::move(moved);
    Expect(moved.Identity() == identity && moved.PointCount() == 8 && moved.Bounds().has_value(),
           "private-geometry.self-move-stable", failures);

    const double infinity = (std::numeric_limits<double>::infinity)();
    const double nan = (std::numeric_limits<double>::quiet_NaN)();
    std::vector<SelectionLoop> mixed;
    mixed.push_back({Vec2{1, 2}, Vec2{infinity, 3}, Vec2{4, 5}});
    auto mixedGeometry = SelectionLoops::AdoptForWork(std::move(mixed));
    Expect(mixedGeometry.has_value(), "private-geometry.nonfinite-adoption", failures);
    if (mixedGeometry)
    {
        const auto mixedBounds = mixedGeometry.value().Bounds();
        Expect(mixedGeometry.value().PointCount() == 3 && mixedBounds.has_value() &&
                   mixedBounds.value() == SelectionBounds{1, 2, 4, 5},
               "private-geometry.nonfinite-counted-bounds-exclude", failures);
    }

    std::vector<SelectionLoop> nonfinite;
    nonfinite.push_back({Vec2{infinity, 0}, Vec2{0, nan}});
    auto nonfiniteGeometry = SelectionLoops::AdoptForWork(std::move(nonfinite));
    Expect(nonfiniteGeometry.has_value(), "private-geometry.all-nonfinite-adoption", failures);
    if (nonfiniteGeometry)
        Expect(nonfiniteGeometry.value().PointCount() == 2 && !nonfiniteGeometry.value().Bounds().has_value(),
               "private-geometry.all-nonfinite-bounds-null", failures);
}

void RunCancellationControls(int& failures, SingleNodeSink& sink)
{
    std::stop_source preCancelled;
    static_cast<void>(preCancelled.request_stop());
    auto preCancelledGeometry = SelectionLoops::AdoptForWork(Rectangles(), CancelToken{preCancelled.get_token()});
    Expect(!preCancelledGeometry.has_value(), "private-geometry.precancel-returns-null", failures);

    std::stop_source duringWork;
    CancellationCheckpoint checkpoint{&duringWork};
    SelectionLoop manyPoints;
    manyPoints.reserve(512);
    for (std::uint32_t i = 0; i < 512; ++i)
        manyPoints.push_back(Vec2{static_cast<double>(i), static_cast<double>(i % 7)});
    std::vector<SelectionLoop> manyLoops;
    manyLoops.push_back(std::move(manyPoints));
    auto cancelledGeometry = SelectionLoops::AdoptForWork(std::move(manyLoops), CancelToken{duringWork.get_token()}, {},
                                                          &CancellationCheckpoint::StopAtWork64, &checkpoint);
    Expect(!cancelledGeometry.has_value() && checkpoint.calls != 0 && checkpoint.lastWork == 64 &&
               duringWork.stop_requested(),
           "private-geometry.checkpoint-cancels-at-64", failures);
    Expect(sink.enqueueCount == 0 && sink.pending == nullptr && !sink.overflow,
           "private-geometry.cancelled-work-publishes-no-node", failures);
}

void RunDeferredLifetimeControls(int& failures)
{
    SingleNodeSink sink;
    const bool installed = InstallDeferredReleaseSink(&SingleNodeSink::Enqueue, &sink);
    Expect(installed, "private-geometry.sink-installed", failures);
    if (!installed)
        return;
    Expect(sink.pending == nullptr && sink.enqueueCount == 0 && !sink.overflow, "private-geometry.sink-starts-empty",
           failures);

    int wrongContext = 0;
    Expect(!ClearDeferredReleaseSink(&wrongContext), "private-geometry.sink-rejects-wrong-context-clear", failures);
    RunCancellationControls(failures, sink);

    MarkerTrace trace;
    auto marker = MakeLifetimeMarker(trace);
    std::weak_ptr<const LifetimeMarker> weakLifetime{marker};
    std::shared_ptr<const void> lifetime{marker, nullptr};
    Expect(!lifetime && lifetime.use_count() > 0, "private-geometry.null-alias-false-but-owned", failures);

    std::weak_ptr<const void> weakGeometry;
    std::uint64_t geometryStorageBytes = 0;
    {
        auto publication = SelectionLoops::AdoptForWork(Rectangles(), {}, lifetime);
        Expect(publication.has_value(), "private-geometry.lifetime-geometry-created", failures);
        if (publication)
        {
            weakGeometry = publication.value().WeakStorageForTest();
            geometryStorageBytes = publication.value().StorageBytes();
        }
        lifetime.reset();
        marker.reset();
        publication.reset();
    }

    Expect(sink.enqueueCount == 1 && sink.pending != nullptr && !sink.overflow,
           "private-geometry.last-owner-enqueues-exactly-one", failures);
    Expect(weakGeometry.expired(), "private-geometry.publication-weak-reader-expires", failures);
    Expect(!weakLifetime.expired() && trace.destructionCount.load(std::memory_order_acquire) == 0,
           "private-geometry.no-marker-destruction-before-node", failures);

    DeferredReleaseNode* const node = sink.pending;
    const auto allowance = SelectionLoops::WorkOwnershipAllowance(2);
    const auto maximum = (std::numeric_limits<std::uint64_t>::max)();
    Expect(node && allowance <= maximum - geometryStorageBytes && node->bytes >= geometryStorageBytes + allowance,
           "private-geometry.node-bytes-cover-storage-and-allowance", failures);

    std::atomic<std::uint32_t> workerDestroyCalls{0};
    std::thread::id workerThreadId{};
    if (node && node->destroy)
    {
        std::thread worker(
            [node, &workerDestroyCalls, &workerThreadId]() noexcept
            {
                workerThreadId = std::this_thread::get_id();
                node->destroy(node);
                workerDestroyCalls.fetch_add(1, std::memory_order_release);
            });
        worker.join();
        sink.pending = nullptr;
    }

    const auto workerCalls = workerDestroyCalls.load(std::memory_order_acquire);
    Expect(workerCalls == 1 && workerThreadId != std::this_thread::get_id(),
           "private-geometry.actual-node-destroyed-on-worker-once", failures);
    Expect(!weakLifetime.expired() && trace.destructionCount.load(std::memory_order_acquire) == 0,
           "private-geometry.weak-control-retains-lifetime-after-node", failures);

    weakGeometry.reset();
    const auto markerDestructions = trace.destructionCount.load(std::memory_order_acquire);
    Expect(weakLifetime.expired() && markerDestructions == 1, "private-geometry.weak-reset-releases-lifetime-once",
           failures);
    Expect(markerDestructions == 1 && trace.destructionThread == std::this_thread::get_id(),
           "private-geometry.marker-deleter-records-owner-thread", failures);
    Expect(sink.enqueueCount == 1 && sink.pending == nullptr && !sink.overflow,
           "private-geometry.no-duplicate-retirement", failures);
    Expect(ClearDeferredReleaseSink(&sink), "private-geometry.sink-cleared-exact-context", failures);
}
} // namespace

int RunSelectionGeometryLifetimeTests()
{
    int failures = 0;
    RunValueControls(failures);
    RunDeferredLifetimeControls(failures);
    return failures;
}
