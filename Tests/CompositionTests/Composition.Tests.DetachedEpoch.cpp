#include "Composition.Tests.DetachedEpoch.h"

#include "BrushCompute.h"
#include "GpuWork.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <thread>

#include <Windows.h>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/com.h>
#include <wil/resource.h>
#pragma warning(pop)

namespace
{
constexpr DWORD kFailureTimeoutMs = 10000;
constexpr int kWidth = 16;
constexpr int kHeight = 16;
constexpr std::size_t kPixelCount = static_cast<std::size_t>(kWidth) * kHeight;
constexpr std::uint64_t kTileKey = 0xD37AC4E1u;
constexpr std::uint8_t kPreviewSentinel = 0xA5;
constexpr std::uint64_t kTilePayload = kPixelCount * 8 + 4 * 4096;
constexpr std::uint64_t kSegmentPayload = 2 * 4096 + 64 * 4 * sizeof(float);
constexpr std::uint64_t kReadbackReservation = kPixelCount * sizeof(std::uint32_t);

constexpr BrushSegment kFirstChord{Vec2{2.0, 6.0}, Vec2{9.0, 6.0}};
constexpr BrushSegment kFinalChord{Vec2{9.0, 6.0}, Vec2{12.0, 10.0}};

int gFailures = 0;

void Expect(bool condition, const char* name) noexcept
{
    std::printf("%s detached-epoch.%s\n", condition ? "PASS" : "FAIL", name);
    gFailures += condition ? 0 : 1;
}

bool StableMemory(void*, MemorySnapshot& snapshot) noexcept
{
    constexpr std::uint64_t available = 8ULL * 1024 * 1024 * 1024;
    snapshot = {available, available};
    return true;
}

struct Device final
{
    wil::com_ptr_nothrow<ID3D11Device> device;
    wil::com_ptr_nothrow<ID3D11DeviceContext> context;

    HRESULT Create() noexcept
    {
        const D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
        return D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, &level, 1,
                                 D3D11_SDK_VERSION, device.put(), nullptr, context.put());
    }
};

class Rig final
{
  public:
    ~Rig()
    {
        queue.StopNotifications();
        scheduler.Close();
        queue.CloseAfterWorkers();
    }

    bool Initialize() noexcept
    {
        const HRESULT deviceResult = device.Create();
        Expect(SUCCEEDED(deviceResult), "setup.warp-device");
        if (FAILED(deviceResult))
            return false;

        const std::int32_t schedulerResult = scheduler.Initialize(1, {}, {StableMemory, nullptr});
        Expect(schedulerResult == S_OK, "setup.one-bounded-worker-stable-memory");
        if (schedulerResult != S_OK)
            return false;

        const HRESULT queueResult = queue.Initialize(scheduler);
        Expect(SUCCEEDED(queueResult), "setup.queue-initialize");
        if (FAILED(queueResult))
            return false;

        const HRESULT bindResult = queue.Bind(device.device.get(), device.context.get(), true);
        Expect(SUCCEEDED(bindResult), "setup.queue-binds-original-warp-device");
        if (FAILED(bindResult))
            return false;

        const bool eventRoute = queue.CompletionRoute() == GpuCapabilities::CompletionRoute::EnqueueSetEvent;
        Expect(eventRoute, "setup.actual-enqueue-set-event-route");
        return eventRoute;
    }

    Device device;
    JobScheduler scheduler;
    GpuWorkQueue queue;
};

struct EpochWorker final
{
    wil::unique_handle ready;
    wil::unique_handle retire;
    wil::unique_handle resetAcknowledged;
    wil::unique_handle finish;
    wil::unique_handle destroyed;
    std::atomic<GpuBrushCoverage::StrokeEpoch*> published{};
    std::atomic<DWORD> threadId{};
    std::atomic<std::uint64_t> resetCpuBytes{};
    std::atomic<std::uint64_t> resetGpuBytes{};
    std::atomic<std::size_t> resetTileCount{};
    std::atomic<bool> resetHasResources{};
    DWORD ownerThread = 0;
    std::thread thread;

    ~EpochWorker()
    {
        // A live epoch may still own resources named by queued GPU commands. Never unwind it implicitly.
        if (thread.joinable())
            std::_Exit(1);
    }

    bool Start()
    {
        ready.reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
        retire.reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
        resetAcknowledged.reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
        finish.reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
        destroyed.reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
        if (!ready || !retire || !resetAcknowledged || !finish || !destroyed)
            return false;

        ownerThread = GetCurrentThreadId();
        thread = std::thread(
            [this]() noexcept
            {
                {
                    GpuBrushCoverage::StrokeEpoch epoch;
                    published.store(&epoch, std::memory_order_release);
                    if (!SetEvent(ready.get()) || WaitForSingleObject(retire.get(), INFINITE) != WAIT_OBJECT_0)
                        std::_Exit(1);

                    epoch.ResetForWork();
                    threadId.store(GetCurrentThreadId(), std::memory_order_release);
                    resetCpuBytes.store(epoch.CpuBytes(), std::memory_order_release);
                    resetGpuBytes.store(epoch.GpuBytes(), std::memory_order_release);
                    resetTileCount.store(epoch.TileCount(), std::memory_order_release);
                    resetHasResources.store(epoch.HasResources(), std::memory_order_release);
                    if (!SetEvent(resetAcknowledged.get()) ||
                        WaitForSingleObject(finish.get(), INFINITE) != WAIT_OBJECT_0)
                        std::_Exit(1);
                }
                published.store(nullptr, std::memory_order_release);
                if (!SetEvent(destroyed.get()))
                    std::_Exit(1);
            });

        if (WaitForSingleObject(ready.get(), kFailureTimeoutMs) != WAIT_OBJECT_0)
            std::_Exit(1);
        return published.load(std::memory_order_acquire) != nullptr;
    }

    bool RetireAndJoinAfterGpuAcknowledgement(bool gpuAcknowledged) noexcept
    {
        if (!gpuAcknowledged || !SetEvent(retire.get()) ||
            WaitForSingleObject(resetAcknowledged.get(), kFailureTimeoutMs) != WAIT_OBJECT_0)
            std::_Exit(1);
        if (!SetEvent(finish.get()) || WaitForSingleObject(destroyed.get(), kFailureTimeoutMs) != WAIT_OBJECT_0)
            std::_Exit(1);
        thread.join();
        return threadId.load(std::memory_order_acquire) != 0 &&
               threadId.load(std::memory_order_relaxed) != ownerThread &&
               resetCpuBytes.load(std::memory_order_acquire) == 0 &&
               resetGpuBytes.load(std::memory_order_acquire) == 0 &&
               resetTileCount.load(std::memory_order_acquire) == 0 &&
               !resetHasResources.load(std::memory_order_acquire) &&
               published.load(std::memory_order_acquire) == nullptr;
    }
};

struct ReadbackState final
{
    std::array<std::uint8_t, kPixelCount> expected{};
    std::array<std::uint8_t, kPixelCount> fresh{};
    GpuBufferReadbackValues values{};
    GpuReadbackLease lease{};
    JobTicket ticket{};
    HRESULT enqueueResult = E_PENDING;
    HRESULT deliveryResult = E_PENDING;
    JobStatus status = JobStatus::Failed;
    unsigned int deliveries = 0;
    unsigned int callbacks = 0;
    int maximumCpuDifference = 0;
    int maximumFreshDifference = 0;
    bool callbackDelivered = false;
    bool resumeAccepted = false;
    bool ticketMatches = false;
    bool workerThread = false;
    bool byteRange = false;
    bool cpuWithinTolerance = false;
    bool hasNonzero = false;
    bool differsFromFresh = false;
    bool leaseReleased = false;
    bool duplicateReleaseRejected = false;
    bool epochRetiredAfterAck = false;
};

class EpochReadbackJob final : public Job, public GpuBufferReadbackReceiver
{
  public:
    EpochReadbackJob(JobScheduler& scheduler, GpuWorkQueue& queue, ID3D11Buffer* source, ReadbackState& state,
                     DWORD ownerThread, EpochWorker* epochWorker = nullptr) noexcept
        : _scheduler(scheduler), _queue(queue), _source(source), _state(state), _ownerThread(ownerThread),
          _epochWorker(epochWorker)
    {
    }

    JobResult Run(JobContext&) override
    {
        if (!_submitted)
            return {};
        if (!_state.callbackDelivered)
            std::_Exit(1); // Never retire resources after a failed admission without a GPU completion callback.

        _state.workerThread = GetCurrentThreadId() != _ownerThread;
        if (SUCCEEDED(_state.deliveryResult) && _state.values.data && _state.values.count == kPixelCount &&
            _state.values.width == static_cast<unsigned int>(kWidth) &&
            _state.values.height == static_cast<unsigned int>(kHeight))
        {
            _state.byteRange = true;
            _state.cpuWithinTolerance = true;
            for (std::size_t i = 0; i < kPixelCount; ++i)
            {
                const std::uint32_t value = _state.values.data[i];
                _state.byteRange = _state.byteRange && value <= 255u;
                _state.hasNonzero = _state.hasNonzero || value != 0;
                const int difference = std::abs(static_cast<int>(value) - static_cast<int>(_state.expected[i]));
                _state.maximumCpuDifference = (std::max)(_state.maximumCpuDifference, difference);
                _state.cpuWithinTolerance = _state.cpuWithinTolerance && difference <= 2;
                const int freshDifference = std::abs(static_cast<int>(value) - static_cast<int>(_state.fresh[i]));
                _state.maximumFreshDifference = (std::max)(_state.maximumFreshDifference, freshDifference);
            }
            _state.differsFromFresh = _state.maximumFreshDifference >= 3;
        }

        if (_epochWorker != nullptr)
        {
            const bool gpuAcknowledged =
                _state.callbackDelivered && SUCCEEDED(_state.deliveryResult) && _state.values.data != nullptr;
            _state.epochRetiredAfterAck = _epochWorker->RetireAndJoinAfterGpuAcknowledgement(gpuAcknowledged);
        }
        return SUCCEEDED(_state.deliveryResult) && _state.values.data && _state.values.count == kPixelCount
                   ? JobResult{}
                   : JobResult{JobStatus::Failed, FAILED(_state.deliveryResult) ? _state.deliveryResult : E_UNEXPECTED};
    }

    bool DeferCompletion(JobResult, JobTicket ticket) noexcept override
    {
        if (_submitted)
            return false;
        _submitted = true;
        _state.enqueueResult = _queue.EnqueueBufferReadback(_source, static_cast<unsigned int>(kWidth),
                                                            static_cast<unsigned int>(kHeight), ticket, *this);
        if (FAILED(_state.enqueueResult))
            _state.resumeAccepted = _scheduler.Resume(ticket);
        return true;
    }

    void BufferReadbackReady(JobTicket ticket, GpuBufferReadbackValues values, HRESULT result) noexcept override
    {
        ++_state.deliveries;
        _state.callbackDelivered = true;
        _state.ticketMatches = ticket == _state.ticket;
        _state.values = values;
        _state.lease = values.lease;
        _state.deliveryResult = result;
        _state.resumeAccepted = _scheduler.Resume(ticket);
    }

    void Completed(JobResult result) noexcept override
    {
        ++_state.callbacks;
        if (_state.values.data)
        {
            _state.leaseReleased = _queue.ReleaseMapped(_state.lease);
            _state.duplicateReleaseRejected = !_queue.ReleaseMapped(_state.lease);
        }
        _state.status = result.status;
    }

  private:
    JobScheduler& _scheduler;
    GpuWorkQueue& _queue;
    ID3D11Buffer* _source;
    ReadbackState& _state;
    DWORD _ownerThread;
    EpochWorker* _epochWorker;
    bool _submitted = false;
};

void SubmitAndSettle(Rig& rig, ID3D11Buffer* source, ReadbackState& state, EpochWorker* epochWorker = nullptr)
{
    std::unique_ptr<Job> job =
        std::make_unique<EpochReadbackJob>(rig.scheduler, rig.queue, source, state, GetCurrentThreadId(), epochWorker);
    const std::int32_t submitted =
        rig.scheduler.Submit(job, {JobClass::Commit, 1, kReadbackReservation, true}, state.ticket);
    Expect(submitted == S_OK, epochWorker ? "job.final-submit" : "job.initial-submit");
    if (submitted != S_OK)
        std::_Exit(1); // A recorded dispatch may still reference the epoch; do not unwind its owner.

    const SettleResult settled = SettleJobs(rig.scheduler, kFailureTimeoutMs);
    Expect(settled == SettleResult::Settled, epochWorker ? "job.final-event-settles" : "job.initial-event-settles");
    if (settled != SettleResult::Settled)
    {
        std::fflush(stdout);
        std::_Exit(1); // An admitted readback still borrows its receiver and mapped data.
    }
}

void MakeCpuReferences(BrushKernelParams& params, std::array<std::uint8_t, kPixelCount>& initial,
                       std::array<std::uint8_t, kPixelCount>& continued, std::array<std::uint8_t, kPixelCount>& fresh)
{
    params.radius = 2.5;
    params.hardness = 0.0;
    params.canvasWidth = kWidth;
    params.canvasHeight = kHeight;
    params.antialiasWidth = 1.0;
    params.spacing = 1.0;

    std::array<float, kPixelCount> density{};
    BrushKernelTile tile{kTileKey, 0, 0, kWidth, kHeight, density.data(), initial.data()};
    const std::array<BrushSegment, 1> first{kFirstChord};
    const std::array<BrushSegment, 1> final{kFinalChord};
    BrushKernel::RenderTile(tile, first, {}, params);
    continued = initial;
    tile.preview = continued.data();
    BrushKernel::RenderTile(tile, final, {}, params);

    std::array<float, kPixelCount> freshDensity{};
    BrushKernelTile freshTile{kTileKey, 0, 0, kWidth, kHeight, freshDensity.data(), fresh.data()};
    BrushKernel::RenderTile(freshTile, final, {}, params);
}

bool RunDetachedEpochCase(Rig& rig)
{
    EpochWorker worker;
    if (!worker.Start())
    {
        Expect(false, "worker.preallocates-epoch-on-worker");
        return false;
    }
    auto* const epoch = worker.published.load(std::memory_order_acquire);
    Expect(epoch != nullptr && epoch->TileCount() == 0 && !epoch->HasResources(),
           "worker.preallocates-empty-epoch-directory-before-publication");
    if (epoch == nullptr)
        std::_Exit(1);

    GpuBrushCoverage engine;
    const HRESULT initialized = engine.Initialize(rig.device.device.get(), rig.device.context.get());
    Expect(SUCCEEDED(initialized), "engine.initialize-on-original-device");
    if (FAILED(initialized))
        std::_Exit(1);

    BrushKernelParams params;
    std::array<std::uint8_t, kPixelCount> cpuInitial{};
    std::array<std::uint8_t, kPixelCount> cpuContinued{};
    std::array<std::uint8_t, kPixelCount> cpuFresh{};
    MakeCpuReferences(params, cpuInitial, cpuContinued, cpuFresh);

    std::array<std::uint8_t, kPixelCount> previewSentinel{};
    previewSentinel.fill(kPreviewSentinel);
    const BrushKernelTile gpuTile{kTileKey, 0, 0, kWidth, kHeight, nullptr, previewSentinel.data()};
    const std::array<BrushKernelTile, 1> tiles{gpuTile};
    const std::array<BrushSegment, 1> first{kFirstChord};
    const std::array<BrushSegment, 1> final{kFinalChord};

    engine.BeginStroke();
    const bool firstRecorded = engine.RecordCoverage(tiles, first, {}, params);
    auto* const firstBuffer = engine.CoverageBuffer(kTileKey, kWidth, kHeight);
    const auto firstGpuBytes = engine.StrokeGpuOwnershipBytes();
    Expect(firstRecorded && firstBuffer != nullptr &&
               std::all_of(previewSentinel.begin(), previewSentinel.end(),
                           [](std::uint8_t value) noexcept { return value == kPreviewSentinel; }),
           "first.record-chord-does-not-write-cpu-preview");
    Expect(firstGpuBytes == kTilePayload + kSegmentPayload, "first.record-owns-no-staging-readback-buffer");
    if (!firstRecorded || firstBuffer == nullptr)
        std::_Exit(1);

    ReadbackState firstRead;
    firstRead.expected = cpuInitial;
    SubmitAndSettle(rig, firstBuffer, firstRead);
    Expect(SUCCEEDED(firstRead.enqueueResult) && firstRead.callbackDelivered && firstRead.resumeAccepted &&
               firstRead.ticketMatches && firstRead.deliveries == 1 && firstRead.callbacks == 1 &&
               firstRead.workerThread && firstRead.cpuWithinTolerance && firstRead.byteRange && firstRead.hasNonzero &&
               firstRead.maximumCpuDifference <= 2 && firstRead.status == JobStatus::Finished &&
               firstRead.leaseReleased && firstRead.duplicateReleaseRejected && rig.queue.LentCount() == 0 &&
               rig.queue.InFlightCount() == 0,
           "first.original-gpu-density-is-nonzero-and-matches-independent-sequential-cpu-within-2-of-255");
    if (!firstRead.callbackDelivered)
        std::_Exit(1);

    const auto ownerCpuBytes = engine.StrokeCpuOwnershipBytes();
    const auto ownerGpuBytes = engine.StrokeGpuOwnershipBytes();
    const bool took = engine.TakeStrokeEpoch(*epoch);
    Expect(took, "take.transfers-settled-density-after-first-gpu-ack");
    Expect(took && engine.TileCount() == 0 && engine.StrokeGpuOwnershipBytes() == 0 &&
               engine.CoverageBuffer(kTileKey, kWidth, kHeight) == nullptr,
           "take.leaves-owner-engine-empty");
    Expect(took && epoch->TileCount() == 1 && epoch->HasResources() && epoch->CpuBytes() >= ownerCpuBytes &&
               epoch->GpuBytes() == ownerGpuBytes && epoch->GpuBytes() == kTilePayload + kSegmentPayload,
           "take.epoch-retains-complete-cpu-and-gpu-ownership-without-staging");
    if (!took)
        std::_Exit(1);

    const auto emptyOwnerCpuBytes = engine.StrokeCpuOwnershipBytes();
    const auto cachedCpuBeforeRecord = epoch->CpuBytes();
    const auto cachedGpuBeforeRecord = epoch->GpuBytes();
    const bool finalRecorded = engine.RecordEpochCoverage(*epoch, tiles, final, params);
    auto* const epochBuffer = engine.EpochCoverageBuffer(*epoch, kTileKey, kWidth, kHeight);
    Expect(finalRecorded && epochBuffer != nullptr &&
               std::all_of(previewSentinel.begin(), previewSentinel.end(),
                           [](std::uint8_t value) noexcept { return value == kPreviewSentinel; }),
           "epoch.records-exact-final-chord-without-cpu-preview-write-or-readback");
    Expect(finalRecorded && engine.TileCount() == 0 && engine.StrokeGpuOwnershipBytes() == 0 &&
               engine.StrokeCpuOwnershipBytes() == emptyOwnerCpuBytes &&
               engine.CoverageBuffer(kTileKey, kWidth, kHeight) == nullptr && epoch->TileCount() == 1 &&
               epoch->HasResources() && epoch->CpuBytes() == cachedCpuBeforeRecord &&
               epoch->GpuBytes() == cachedGpuBeforeRecord,
           "epoch.final-record-preserves-empty-owner-and-cached-complete-ownership");
    if (!finalRecorded || epochBuffer == nullptr)
        std::_Exit(1);

    bool freshIsDistinct = false;
    for (std::size_t i = 0; i < kPixelCount; ++i)
    {
        freshIsDistinct =
            freshIsDistinct || std::abs(static_cast<int>(cpuContinued[i]) - static_cast<int>(cpuFresh[i])) >= 3;
    }
    Expect(std::any_of(cpuInitial.begin(), cpuInitial.end(), [](std::uint8_t value) noexcept { return value != 0; }),
           "continuation.cpu-first-chord-has-nonzero-settled-density");
    Expect(freshIsDistinct, "continuation.final-chord-distinguishes-preserved-density-from-fresh-restart");

    const bool validLookup = engine.EpochCoverageBuffer(*epoch, kTileKey, kWidth, kHeight) == epochBuffer;
    const bool invalidLookup = engine.EpochCoverageBuffer(*epoch, kTileKey + 1, kWidth, kHeight) == nullptr &&
                               engine.EpochCoverageBuffer(*epoch, kTileKey, kWidth - 1, kHeight) == nullptr &&
                               engine.EpochCoverageBuffer(*epoch, kTileKey, kWidth, kHeight + 1) == nullptr;
    Expect(validLookup, "lookup.exact-key-and-size-returns-epoch-uint-buffer");
    Expect(invalidLookup, "lookup.wrong-key-or-either-dimension-refuses");

    const auto epochCpuBeforeRefusal = epoch->CpuBytes();
    const auto epochGpuBeforeRefusal = epoch->GpuBytes();
    const auto epochTilesBeforeRefusal = epoch->TileCount();
    constexpr std::uint64_t kRejectedKey = kTileKey + 9;
    const BrushKernelTile rejectedTile{kRejectedKey, 0, 0, kWidth, kHeight, nullptr, previewSentinel.data()};
    const std::array<BrushKernelTile, 1> rejectedTiles{rejectedTile};
    const bool liveRecorded = engine.RecordCoverage(rejectedTiles, final, {}, params);
    auto* const liveBuffer = engine.CoverageBuffer(kRejectedKey, kWidth, kHeight);
    const auto liveCpuBeforeRefusal = engine.StrokeCpuOwnershipBytes();
    const auto liveGpuBeforeRefusal = engine.StrokeGpuOwnershipBytes();
    const bool refused = !engine.RecordEpochCoverage(*epoch, rejectedTiles, final, params);
    Expect(liveRecorded && refused && engine.TileCount() == 1 && liveBuffer != nullptr &&
               engine.CoverageBuffer(kRejectedKey, kWidth, kHeight) == liveBuffer &&
               engine.StrokeCpuOwnershipBytes() == liveCpuBeforeRefusal &&
               engine.StrokeGpuOwnershipBytes() == liveGpuBeforeRefusal &&
               epoch->TileCount() == epochTilesBeforeRefusal && epoch->CpuBytes() == epochCpuBeforeRefusal &&
               epoch->GpuBytes() == epochGpuBeforeRefusal &&
               engine.EpochCoverageBuffer(*epoch, kTileKey, kWidth, kHeight) == epochBuffer,
           "refusal.nonempty-live-engine-preserves-both-complete-resource-graphs");

    engine.EndStroke();
    engine.Release();
    Device replacement;
    const HRESULT replacementCreated = replacement.Create();
    const HRESULT replacementBound = SUCCEEDED(replacementCreated)
                                         ? engine.Initialize(replacement.device.get(), replacement.context.get())
                                         : replacementCreated;
    Expect(SUCCEEDED(replacementCreated) && SUCCEEDED(replacementBound),
           "rebind.create-and-bind-replacement-warp-device");
    const auto epochCpuBeforeRebindRefusal = epoch->CpuBytes();
    const auto epochGpuBeforeRebindRefusal = epoch->GpuBytes();
    const auto epochTilesBeforeRebindRefusal = epoch->TileCount();
    const bool replacementRefused = !engine.RecordEpochCoverage(*epoch, tiles, final, params);
    Expect(replacementRefused && engine.TileCount() == 0 && epoch->TileCount() == epochTilesBeforeRebindRefusal &&
               epoch->CpuBytes() == epochCpuBeforeRebindRefusal && epoch->GpuBytes() == epochGpuBeforeRebindRefusal &&
               epoch->HasResources(),
           "rebind.recording-refuses-replacement-device-and-preserves-old-epoch");
    Expect(engine.EpochCoverageBuffer(*epoch, kTileKey, kWidth, kHeight) == nullptr,
           "rebind.exact-buffer-lookup-refuses-replacement-device");
    if (FAILED(replacementBound))
        std::_Exit(1);

    ReadbackState finalRead;
    finalRead.expected = cpuContinued;
    finalRead.fresh = cpuFresh;
    SubmitAndSettle(rig, epochBuffer, finalRead, &worker);
    Expect(SUCCEEDED(finalRead.enqueueResult) && finalRead.callbackDelivered && finalRead.resumeAccepted &&
               finalRead.ticketMatches && finalRead.deliveries == 1 && finalRead.callbacks == 1 &&
               finalRead.workerThread && finalRead.byteRange && finalRead.cpuWithinTolerance &&
               finalRead.maximumCpuDifference <= 2 && finalRead.status == JobStatus::Finished,
           "gpu.final-epoch-uint-buffer-after-real-event-matches-sequential-cpu-within-2-of-255");
    Expect(finalRead.differsFromFresh && finalRead.maximumFreshDifference >= 3,
           "gpu.final-buffer-proves-prior-density-survived-owner-take");
    Expect(finalRead.epochRetiredAfterAck && worker.threadId.load(std::memory_order_acquire) != worker.ownerThread &&
               worker.published.load(std::memory_order_acquire) == nullptr,
           "retirement.original-gpu-event-then-worker-reset-and-destruction");
    Expect(finalRead.leaseReleased && finalRead.duplicateReleaseRejected && rig.queue.LentCount() == 0 &&
               rig.queue.InFlightCount() == 0 && rig.scheduler.ReservedMemoryBytes() == 0,
           "retirement.owner-releases-readback-lease-after-worker-ack-with-no-job-reservation-left");

    engine.Release();
    return finalRead.status == JobStatus::Finished;
}
} // namespace

int RunDetachedEpochTests()
{
    gFailures = 0;
    Rig rig;
    if (!rig.Initialize())
        return gFailures;
    (void)RunDetachedEpochCase(rig);
    return gFailures;
}
