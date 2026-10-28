#include "Host.Tests.WarpFactoryLifetime.h"

#include "Common/DeferredRelease.h"
#include "Common/Jobs.h"
#include "Common/SharedPublicationMemory.h"
#include "Common/WarpStroke.h"

#include <windows.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <memory>
#include <stop_token>
#include <thread>
#include <utility>

namespace
{
constexpr std::uint64_t kAdmissionBytes = 4ull * 1024 * 1024;
constexpr std::uint64_t kAvailableBytes = kMemoryAdmissionHeadroomBytes + kAdmissionBytes;
constexpr std::uint32_t kTimeoutMs = 10000;
constexpr std::size_t kDeferredCapacity = 4;
constexpr std::array<std::uint8_t, 16> kPremultipliedPixels{12, 24, 48, 96, 12, 24, 48, 96,
                                                            12, 24, 48, 96, 12, 24, 48, 96};
unsigned int gPassed = 0;
unsigned int gFailed = 0;

void Expect(bool condition, const char* name) noexcept
{
    std::printf("%s warp-factory-lifetime.%s\n", condition ? "PASS" : "FAIL", name);
    std::fflush(stdout);
    condition ? ++gPassed : ++gFailed;
}

[[noreturn]] void SetupFailure(const char* name, std::int32_t result = 0) noexcept
{
    std::printf("FAIL warp-factory-lifetime.setup.%s result=%ld\n", name, static_cast<long>(result));
    std::fflush(stdout);
    std::_Exit(1);
}

bool SampleMemory(void*, MemorySnapshot& snapshot) noexcept
{
    snapshot = {kAvailableBytes, kAvailableBytes};
    return true;
}

struct DeferredQueue final
{
    std::array<DeferredReleaseNode*, kDeferredCapacity> nodes{};
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
            }
        });
    worker.join();
    queue.nodes.fill(nullptr);
    queue.count = 0;
}

[[nodiscard]] Raster SparsePremultipliedInput()
{
    const Raster base =
        Raster::Make(2, 2, RasterFormat::Bgra8888, kPremultipliedPixels.data(), kPremultipliedPixels.size());
    if (!base.HasStorage())
        SetupFailure("literal-base-raster");
    Raster sparse = Raster::MakeSparse(2, 2, RasterFormat::Bgra8888, base, 0, 0, {}, 0);
    if (!sparse.HasStorage() || !sparse.IsSparse() || sparse.IsMaterialized())
        SetupFailure("sparse-source-raster");
    return sparse;
}

[[nodiscard]] BrushSettings Settings() noexcept
{
    BrushSettings settings{};
    settings.diameter = 2.0;
    settings.hardness = 0.5;
    settings.opacity = 0.75;
    return settings;
}

struct FactoryState final
{
    JobScheduler* scheduler = nullptr;
    JobTicket ticket{};
    Raster source;
    std::thread::id ownerThread{};
    std::thread::id factoryThread{};
    std::shared_ptr<SharedPublicationMemory> memory;
    std::shared_ptr<WarpStroke> stroke;
    JobResult completion{JobStatus::Failed};
    std::int32_t bindResult = E_UNEXPECTED;
    std::uint64_t callbackChargeBefore = 0;
    std::uint64_t callbackChargeAfter = 0;
    bool bytesMatchLiteral = false;
    bool sourceStillSparse = false;
    bool invalidCasesRefused = false;
    unsigned int callbacksCompleted = 0;
};

// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4626 5027)
class WarpFactoryJob final : public Job
{
  public:
    explicit WarpFactoryJob(FactoryState& state) noexcept : _state(state) {}

    JobResult Run(JobContext& context) override
    {
        // The actual admitted pool worker owns construction and allocation failure translation.
        const auto cancellation = context.cancellation;
        _state.factoryThread = std::this_thread::get_id();
        _state.memory = SharedPublicationMemory::CreateForWork();
        if (!_state.memory)
            return {JobStatus::Failed, E_UNEXPECTED};

        const BrushSettings settings = Settings();
        std::stop_source stopped;
        (void)stopped.request_stop();
        const CancelToken cancelled{stopped.get_token()};
        const BrushSettings nonFinite{.diameter = std::numeric_limits<double>::quiet_NaN()};
        const auto stale = WarpStroke::MakeForWork(Raster{}, SmearMode::Liquify, settings, _state.ownerThread,
                                                   cancellation, _state.memory);
        const auto nonFiniteResult = WarpStroke::MakeForWork(_state.source, SmearMode::Liquify, nonFinite,
                                                             _state.ownerThread, cancellation, _state.memory);
        const auto unsupported = WarpStroke::MakeForWork(_state.source, static_cast<SmearMode>(0xff), settings,
                                                         _state.ownerThread, cancellation, _state.memory);
        const auto cancelledResult = WarpStroke::MakeForWork(_state.source, SmearMode::Liquify, settings,
                                                             _state.ownerThread, cancelled, _state.memory);
        const auto emptyOwner =
            WarpStroke::MakeForWork(_state.source, SmearMode::Liquify, settings, {}, cancellation, _state.memory);
        const auto callerAsOwner = WarpStroke::MakeForWork(_state.source, SmearMode::Liquify, settings,
                                                           std::this_thread::get_id(), cancellation, _state.memory);
        const auto missingMemory =
            WarpStroke::MakeForWork(_state.source, SmearMode::Liquify, settings, _state.ownerThread, cancellation, {});
        _state.invalidCasesRefused = !stale && !nonFiniteResult && !unsupported && !cancelledResult && !emptyOwner &&
                                     !callerAsOwner && !missingMemory && !_state.memory->IsBound();

        _state.stroke = WarpStroke::MakeForWork(_state.source, SmearMode::Liquify, settings, _state.ownerThread,
                                                cancellation, _state.memory);
        if (!_state.stroke || !_state.stroke->IsValid())
            return {JobStatus::Failed, E_UNEXPECTED};

        const Raster image = _state.stroke->ImageForWork(cancellation);
        std::array<std::uint8_t, kPremultipliedPixels.size()> actual{};
        if (image.HasStorage())
            image.ReadRegion(0, 0, 2, 2, actual.data(), 8, 0);
        _state.bytesMatchLiteral = image.HasStorage() && actual == kPremultipliedPixels;
        _state.sourceStillSparse = _state.source.IsSparse() && !_state.source.IsMaterialized();

        return {};
    }

    void Completed(JobResult result) noexcept override
    {
        ++_state.callbacksCompleted;
        _state.completion = result;
        _state.callbackChargeBefore = _state.scheduler->ReservedMemoryBytes();
        if (result.status == JobStatus::Finished && _state.memory && _state.stroke)
            _state.bindResult = _state.memory->BindAfterCompletedWork(*_state.scheduler, _state.ticket);
        _state.callbackChargeAfter = _state.scheduler->ReservedMemoryBytes();
    }

  private:
    FactoryState& _state;
};
#pragma warning(pop)

void StartScheduler(JobScheduler& scheduler)
{
    if (scheduler.Initialize(1, {}, {SampleMemory, nullptr}) != S_OK)
        SetupFailure("scheduler-initialize");
}

void SubmitAndSettle(JobScheduler& scheduler, FactoryState& state)
{
    state.scheduler = &scheduler;
    state.source = SparsePremultipliedInput();
    state.ownerThread = std::this_thread::get_id();
    std::unique_ptr<Job> job = std::make_unique<WarpFactoryJob>(state);
    JobOptions options{};
    options.kind = JobClass::Background;
    options.memoryBytes = kAdmissionBytes;
    options.reserveRetainedMemory = true;
    if (scheduler.Submit(job, options, state.ticket) != S_OK || job)
        SetupFailure("job-submit");
    if (SettleJobs(scheduler, kTimeoutMs) != SettleResult::Settled)
        SetupFailure("jobs-settle");
}

void TestAlreadyBoundMemoryRefused(FactoryState& state)
{
    bool refused = false;
    std::thread::id workerId{};
    std::thread worker(
        [&]()
        {
            workerId = std::this_thread::get_id();
            const auto result = WarpStroke::MakeForWork(state.source, SmearMode::Liquify, Settings(), state.ownerThread,
                                                        CancelToken{}, state.memory);
            refused = !result;
        });
    worker.join();
    Expect(workerId != state.ownerThread && refused, "already-bound-memory-refused-on-real-worker");
}

void TestLifetimePermutation(bool releaseWeakFirst)
{
    DeferredSinkScope sink;
    Expect(sink.installed, "owner-deferred-sink-installed");
    if (!sink.installed)
        return;

    JobScheduler scheduler;
    StartScheduler(scheduler);
    FactoryState state;
    SubmitAndSettle(scheduler, state);

    Expect(state.callbacksCompleted == 1 && state.completion.status == JobStatus::Finished && state.stroke &&
               state.stroke->IsValid() && state.factoryThread != state.ownerThread &&
               state.factoryThread != std::this_thread::get_id() && state.sourceStillSparse,
           "factory-runs-on-joined-worker-and-leaves-sparse-source-unmaterialized");
    Expect(state.bytesMatchLiteral, "worker-copy-matches-independent-premultiplied-literal-bytes");
    Expect(state.invalidCasesRefused, "stale-nonfinite-unsupported-cancelled-owner-and-missing-memory-refused");
    Expect(state.bindResult == S_OK && state.callbackChargeBefore == kAdmissionBytes &&
               state.callbackChargeAfter == kAdmissionBytes && state.memory && state.memory->IsBound() &&
               state.memory->Bytes() == kAdmissionBytes && state.memory->ResidentBytes() == kAdmissionBytes &&
               scheduler.ReservedMemoryBytes() == kAdmissionBytes,
           "original-successful-completed-callback-binds-exact-whole-reservation");

    TestAlreadyBoundMemoryRefused(state);
    std::weak_ptr<WarpStroke> weakStroke = state.stroke;
    state.memory.reset();

    if (releaseWeakFirst)
        weakStroke.reset();
    state.stroke.reset();
    Expect(!sink.queue.overflow && sink.queue.count == 1 && sink.queue.nodes[0] &&
               sink.queue.nodes[0]->bytes < kDeferredReleaseThreshold,
           "owner-last-strong-release-always-queues-preallocated-subthreshold-node");
    const bool cleared = ClearDeferredReleaseSink(&sink.queue);
    Expect(cleared, "owner-sink-cleared-before-joined-node-destruction");
    if (!cleared)
        SetupFailure("owner-sink-clear");
    sink.installed = false;

    if (!releaseWeakFirst)
    {
        DestroyQueuedOnJoinedWorker(sink.queue);
        Expect(sink.queue.destroyed == 1 && sink.queue.destroyThread != state.ownerThread &&
                   scheduler.ReservedMemoryBytes() == kAdmissionBytes,
               "queued-graph-dies-first-while-last-weak-control-retains-original-charge");
        weakStroke.reset();
    }
    else
    {
        Expect(scheduler.ReservedMemoryBytes() == kAdmissionBytes && weakStroke.expired(),
               "last-weak-control-dies-first-while-queued-graph-retains-original-charge");
        DestroyQueuedOnJoinedWorker(sink.queue);
        Expect(sink.queue.destroyed == 1 && sink.queue.destroyThread != state.ownerThread,
               "queued-graph-dies-after-last-weak-control");
    }

    (void)DrainCompletions(scheduler);
    Expect(scheduler.ReservedMemoryBytes() == 0,
           "original-reservation-released-only-after-queued-graph-and-last-weak-control");
    scheduler.Close();
}

void TestCancelledAppendInvalidatesImage()
{
    const Raster source = SparsePremultipliedInput();
    const std::thread::id ownerThread = std::this_thread::get_id();
    bool workerObservedNoImage = false;
    std::thread worker(
        [&]()
        {
            const auto memory = SharedPublicationMemory::CreateForWork();
            if (!memory)
                return;
            const auto stroke =
                WarpStroke::MakeForWork(source, SmearMode::Liquify, Settings(), ownerThread, CancelToken{}, memory);
            if (!stroke || !stroke->IsValid())
                return;
            std::stop_source stop;
            (void)stop.request_stop();
            const bool appended = stroke->AppendForWork({1.0, 1.0}, CancelToken{stop.get_token()});
            const Raster imageAfterInvalidation = stroke->ImageForWork(CancelToken{});
            workerObservedNoImage = !appended && !stroke->IsValid() && !imageAfterInvalidation.HasStorage();
        });
    worker.join();
    Expect(workerObservedNoImage, "cancelled-numeric-work-invalidates-stroke-without-exposing-image");
    Expect(source.IsSparse() && !source.IsMaterialized(), "cancelled-work-keeps-original-sparse-source-unmaterialized");
}

void TestWorkerReleaseRunsInline()
{
    const Raster source = SparsePremultipliedInput();
    const std::thread::id ownerThread = std::this_thread::get_id();
    std::shared_ptr<WarpStroke> stroke;
    std::thread factory(
        [&]()
        {
            const auto memory = SharedPublicationMemory::CreateForWork();
            if (memory)
                stroke =
                    WarpStroke::MakeForWork(source, SmearMode::Liquify, Settings(), ownerThread, CancelToken{}, memory);
        });
    factory.join();
    if (!stroke)
    {
        Expect(false, "inline-release-fixture-created");
        return;
    }

    std::weak_ptr<WarpStroke> weakStroke = stroke;
    std::shared_ptr<WarpStroke> release = std::move(stroke);
    DeferredSinkScope ownerSink;
    bool workerSinkEmpty = false;
    std::thread reaper(
        [release = std::move(release), &workerSinkEmpty]() mutable
        {
            workerSinkEmpty = DeferredReleaseDetail::sink.enqueue == nullptr;
            release.reset();
        });
    reaper.join();
    Expect(ownerSink.installed && workerSinkEmpty && weakStroke.expired() && !ownerSink.queue.overflow &&
               ownerSink.queue.count == 0,
           "non-owner-last-strong-release-destroys-inline-with-worker-sink-empty");
    weakStroke.reset();
}
} // namespace

int RunWarpFactoryLifetimeTests()
{
    TestLifetimePermutation(false);
    TestLifetimePermutation(true);
    TestCancelledAppendInvalidatesImage();
    TestWorkerReleaseRunsInline();
    std::printf("SUMMARY warp-factory-lifetime passed=%u failed=%u controls=%u\n", gPassed, gFailed, gPassed + gFailed);
    std::fflush(stdout);
    return gFailed == 0 ? 0 : 1;
}
