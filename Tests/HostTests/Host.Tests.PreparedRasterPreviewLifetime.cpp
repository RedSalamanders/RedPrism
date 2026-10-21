#include "Host.Tests.PreparedRasterPreviewLifetime.h"

#include "../../Common/BrushStroke.h"
#include "../../Common/DeferredRelease.h"
#include "../../Common/Jobs.h"
#include "../../Common/PreparedRasterPreview.h"
#include "../../Common/RasterEditWork.h"
#include "../../Common/SharedPublicationMemory.h"

#include <windows.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <thread>
#include <utility>

namespace
{
constexpr std::array<std::uint8_t, 16> kLiteralBgra{11, 23, 37,  255, 47,  59,  71,  255,
                                                    83, 97, 109, 255, 127, 139, 151, 255};
constexpr std::uint64_t kAdmissionBytes = 4ull * 1024 * 1024;
constexpr std::uint64_t kAvailableBytes = kMemoryAdmissionHeadroomBytes + kAdmissionBytes;
constexpr std::uint32_t kTimeoutMs = 10000;
constexpr std::size_t kDeferredCapacity = 8;
unsigned int gPassed = 0;
unsigned int gFailed = 0;

void Expect(bool condition, const char* name) noexcept
{
    std::printf("%s prepared-raster-preview-lifetime.%s\n", condition ? "PASS" : "FAIL", name);
    std::fflush(stdout);
    condition ? ++gPassed : ++gFailed;
}

[[noreturn]] void SetupFailure(const char* name, std::int32_t result = 0) noexcept
{
    std::printf("FAIL prepared-raster-preview-lifetime.setup.%s result=%ld\n", name, static_cast<long>(result));
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
    // Release publication wrappers on the owner before starting this consumer. DeferredRelease's sink is TLS,
    // so installing it on the worker would incorrectly model an application owner sink.
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

enum class PreviewKind : std::uint8_t
{
    Gradient,
    PixelMove,
};

enum class WeakOwner : std::uint8_t
{
    Stroke,
    Display,
    PixelMoveOpening,
    None,
};

struct PreviewState final
{
    JobScheduler* scheduler = nullptr;
    JobTicket ticket{};
    RasterEditPreviewResult preview;
    unsigned int callbacks = 0;
    JobResult completion{JobStatus::Failed};
    std::int32_t bindResult = E_UNEXPECTED;
    std::uint64_t callbackChargeBefore = 0;
    std::uint64_t callbackChargeAfter = 0;
};

class PreviewJob final : public Job
{
  public:
    PreviewJob(PreviewState& state, RasterEditInput input) noexcept : _state(state), _input(std::move(input)) {}

    JobResult Run(JobContext& context) override
    {
        _state.preview = RunRasterEditPreview(_input, context.cancellation);
        return {};
    }

    void Completed(JobResult result) noexcept override
    {
        ++_state.callbacks;
        _state.completion = result;
        _state.callbackChargeBefore = _state.scheduler->ReservedMemoryBytes();
        _state.bindResult = _state.preview.memory
                                ? _state.preview.memory->BindAfterCompletedWork(*_state.scheduler, _state.ticket)
                                : E_POINTER;
        _state.callbackChargeAfter = _state.scheduler->ReservedMemoryBytes();
    }

  private:
    PreviewState& _state;
    RasterEditInput _input;
};

struct Fixture final
{
    Fixture(PreviewKind kind)
        : source(Raster::Make(4, 1, RasterFormat::Bgra8888, kLiteralBgra.data(), kLiteralBgra.size()))
    {
        if (!source.HasStorage())
            SetupFailure("literal-raster");
        layer = ImageLayer::Blank(L"Prepared preview lifetime", Vec2{4, 1});
        layer.asset = RasterAsset{source, source, layer.name};
        input.layer = layer;
        input.canvasWidth = 4;
        input.canvasHeight = 1;
        input.pixelLimit = 4;
        input.prepareDisplay = true;
        input.ownerThread = std::this_thread::get_id();
        if (kind == PreviewKind::Gradient)
        {
            input.kind = RasterEditKind::Gradient;
            input.start = {0.0, 0.5};
            input.end = {4.0, 0.5};
            input.color = {0.8, 0.1, 0.2, 1.0};
            input.endColor = {0.1, 0.3, 0.9, 1.0};
        }
        else
        {
            input.kind = RasterEditKind::PixelMove;
            input.selection =
                DocumentSelection{false, SelectionLoops{{Vec2{1, 0}, Vec2{3, 0}, Vec2{3, 1}, Vec2{1, 1}}}};
            input.offset = {1, 0};
        }
    }

    [[nodiscard]] bool SourceBytesUnchanged() const noexcept
    {
        std::array<std::uint8_t, kLiteralBgra.size()> actual{};
        source.ReadRegion(0, 0, 4, 1, actual.data(), 16, 0);
        return actual == kLiteralBgra;
    }

    Raster source;
    ImageLayer layer;
    RasterEditInput input;
};

void SubmitAndSettle(JobScheduler& scheduler, PreviewState& state, RasterEditInput input)
{
    state.scheduler = &scheduler;
    std::unique_ptr<Job> job = std::make_unique<PreviewJob>(state, std::move(input));
    JobOptions options{};
    options.kind = JobClass::Background;
    options.memoryBytes = kAdmissionBytes;
    options.reserveRetainedMemory = true;
    if (scheduler.Submit(job, options, state.ticket) != S_OK || job)
        SetupFailure("job-submit");
    if (SettleJobs(scheduler, kTimeoutMs) != SettleResult::Settled)
        SetupFailure("jobs-settle");
}

void StartScheduler(JobScheduler& scheduler)
{
    if (scheduler.Initialize(1, {}, {SampleMemory, nullptr}) != S_OK)
        SetupFailure("scheduler-initialize");
}

void ExpectSuccessfulPreview(const PreviewState& state, PreviewKind kind, const char* prefix)
{
    const bool kindOutput = state.preview.stroke && state.preview.stroke->HasPaint() && state.preview.display &&
                            !state.preview.display->tiles.empty() &&
                            (kind == PreviewKind::Gradient || state.preview.pixelMoveOpening);
    Expect(state.callbacks == 1 && state.completion.status == JobStatus::Finished &&
               state.preview.status == RasterEditStatus::Ready && kindOutput,
           prefix);
    Expect(state.bindResult == S_OK && state.callbackChargeBefore == kAdmissionBytes &&
               state.callbackChargeAfter == kAdmissionBytes && state.preview.memory &&
               state.preview.memory->IsBound() && state.preview.memory->Bytes() == kAdmissionBytes &&
               state.preview.memory->ResidentBytes() == kAdmissionBytes,
           "original-successful-completed-callback-binds-whole-literal-4mib-reservation");
}

void TestWeakRetention(PreviewKind kind, WeakOwner weakOwner, const char* prefix)
{
    DeferredSinkScope sink;
    Expect(sink.installed, "temporary-fixed-owner-sink-installed");
    if (!sink.installed)
        return;

    Fixture fixture(kind);
    JobScheduler scheduler;
    StartScheduler(scheduler);
    PreviewState state;
    SubmitAndSettle(scheduler, state, std::move(fixture.input));
    ExpectSuccessfulPreview(state, kind, prefix);

    std::weak_ptr<BrushStroke> weakStroke;
    std::weak_ptr<const PreparedRasterPreview> weakDisplay;
    std::weak_ptr<const PixelMoveOpening> weakOpening;
    switch (weakOwner)
    {
    case WeakOwner::Stroke:
        weakStroke = state.preview.stroke;
        break;
    case WeakOwner::Display:
        weakDisplay = state.preview.display;
        break;
    case WeakOwner::PixelMoveOpening:
        weakOpening = state.preview.pixelMoveOpening;
        break;
    case WeakOwner::None:
        break;
    }

    // Drop every strong owner published by the completed worker. The owner sink retains the detached wrappers
    // in fixed storage until a separate, joined worker destroys them below.
    state.preview.stroke.reset();
    state.preview.display.reset();
    state.preview.pixelMoveOpening.reset();
    state.preview.memory.reset();
    Expect(!sink.queue.overflow && sink.queue.count == (kind == PreviewKind::Gradient ? 2u : 3u),
           "last-strong-release-defers-all-preview-nodes-into-fixed-owner-queue");
    const bool cleared = ClearDeferredReleaseSink(&sink.queue);
    Expect(cleared, "owner-sink-cleared-before-joined-node-destruction");
    if (!cleared)
        SetupFailure("owner-sink-clear");
    sink.installed = false;
    DestroyQueuedOnJoinedWorker(sink.queue);
    Expect(sink.queue.destroyed == (kind == PreviewKind::Gradient ? 2u : 3u) &&
               sink.queue.destroyThread != std::this_thread::get_id(),
           "queued-preview-nodes-destroyed-by-real-joined-worker");

    const bool selectedWeakExpired = weakOwner == WeakOwner::Stroke    ? weakStroke.expired()
                                     : weakOwner == WeakOwner::Display ? weakDisplay.expired()
                                                                       : weakOpening.expired();
    Expect(selectedWeakExpired && scheduler.ReservedMemoryBytes() == kAdmissionBytes,
           "only-selected-weak-control-keeps-whole-original-admission-after-worker-destruction");
    Expect(fixture.SourceBytesUnchanged(), "literal-4x1-input-bytes-unchanged");

    (void)DrainCompletions(scheduler);
    Expect(scheduler.ReservedMemoryBytes() == kAdmissionBytes,
           "owner-drain-cannot-refund-while-selected-weak-control-remains");
    weakStroke.reset();
    weakDisplay.reset();
    weakOpening.reset();
    (void)DrainCompletions(scheduler);
    Expect(scheduler.ReservedMemoryBytes() == 0, "last-selected-weak-reset-and-owner-drain-refund-whole-reservation");
    scheduler.Close();
}

void TestStrongPixelMoveOpeningRetainsCharge()
{
    DeferredSinkScope sink;
    Expect(sink.installed, "opening-cache-temporary-fixed-owner-sink-installed");
    if (!sink.installed)
        return;

    Fixture fixture(PreviewKind::PixelMove);
    JobScheduler scheduler;
    StartScheduler(scheduler);
    PreviewState state;
    SubmitAndSettle(scheduler, state, std::move(fixture.input));
    ExpectSuccessfulPreview(state, PreviewKind::PixelMove, "pixel-move-exclusive-preview-ready-with-opening");

    std::shared_ptr<const PixelMoveOpening> cachedOpening = std::move(state.preview.pixelMoveOpening);
    state.preview.stroke.reset();
    state.preview.display.reset();
    state.preview.memory.reset();
    Expect(static_cast<bool>(cachedOpening) && !sink.queue.overflow && sink.queue.count == 2,
           "cache-keeps-opening-strong-while-stroke-and-metadata-retire");
    const bool cleared = ClearDeferredReleaseSink(&sink.queue);
    Expect(cleared, "opening-cache-owner-sink-cleared-before-worker-drain");
    if (!cleared)
        SetupFailure("opening-cache-owner-sink-clear");
    sink.installed = false;
    DestroyQueuedOnJoinedWorker(sink.queue);
    Expect(sink.queue.destroyed == 2 && scheduler.ReservedMemoryBytes() == kAdmissionBytes,
           "opening-alone-keeps-whole-charge-after-stroke-and-metadata-node-destruction");

    std::thread::id cacheReleaseThread;
    std::jthread releaseCache(
        [cached = std::move(cachedOpening), &cacheReleaseThread]() mutable noexcept
        {
            cacheReleaseThread = std::this_thread::get_id();
            cached.reset();
        });
    releaseCache.join();
    Expect(cacheReleaseThread != std::this_thread::get_id() && scheduler.ReservedMemoryBytes() == kAdmissionBytes,
           "cached-opening-final-owner-retires-on-joined-worker-before-owner-refund");
    (void)DrainCompletions(scheduler);
    Expect(scheduler.ReservedMemoryBytes() == 0, "cached-opening-release-and-owner-drain-refund-reservation");
    Expect(fixture.SourceBytesUnchanged(), "pixel-move-literal-4x1-input-bytes-unchanged");
    scheduler.Close();
}
} // namespace

int RunPreparedRasterPreviewLifetimeTests()
{
    TestWeakRetention(PreviewKind::Gradient, WeakOwner::Stroke, "literal-gradient-exclusive-output-ready");
    TestWeakRetention(PreviewKind::Gradient, WeakOwner::Display, "literal-gradient-exclusive-output-ready");
    TestWeakRetention(PreviewKind::PixelMove, WeakOwner::PixelMoveOpening, "literal-pixel-move-exclusive-output-ready");
    TestStrongPixelMoveOpeningRetainsCharge();
    std::printf("SUMMARY prepared-raster-preview-lifetime passed=%u failed=%u controls=%u\n", gPassed, gFailed,
                gPassed + gFailed);
    std::fflush(stdout);
    return gFailed == 0 ? 0 : 1;
}
