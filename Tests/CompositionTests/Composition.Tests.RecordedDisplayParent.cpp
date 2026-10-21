#include "Composition.Tests.RecordedBlit.h"
#include "GpuCommandPump.h"
#include "GpuWork.h"
#include "Jobs.h"
#include "OwnerWorkMultiplexer.h"
#include "RecordedDisplayGraph.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <utility>
#include <vector>

#include <d3d11.h>
#include <dxgi.h>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/com.h>
#include <wil/resource.h>
#pragma warning(pop)

namespace
{
constexpr std::size_t kGraphCount = 3;
constexpr std::uint64_t MiB = 1024ull * 1024;
// Fixture-only complete peak: source/root allowance 4 MiB + all three retained display/stack/mip texture sets
// 48 MiB + three graph/context/driver shadow owners 64 MiB + private deferred command transport 8 MiB +
// simultaneous blit target/readback storage 4 MiB. This is a declared harness reservation, not a production
// admission estimate or a VRAM guarantee.
constexpr std::uint64_t kFixtureParentCharge = 4 * MiB + 48 * MiB + 64 * MiB + 8 * MiB + 4 * MiB;
static_assert(kFixtureParentCharge == 128 * MiB);
unsigned int gFailures = 0;

enum class Phase : unsigned int
{
    Opening,
    Recording,
    StreamRetirement,
    UnpublishedRetirement,
    LastUseBarrier,
    GraphRetirement,
    Terminal
};

enum class Fixture : std::size_t
{
    Pair,
    Stripes,
    Tiled
};

void Expect(const char* name, bool passed) noexcept
{
    std::printf("%s recorded-blit-parent.%s\n", passed ? "PASS" : "FAIL", name);
    std::fflush(stdout);
    std::fflush(stderr);
    if (!passed)
        ++gFailures;
}

Raster Solid(int width, int height, const std::array<std::uint8_t, 4>& bgra)
{
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(width) * height * 4);
    for (std::size_t offset = 0; offset < bytes.size(); offset += bgra.size())
        std::copy(bgra.begin(), bgra.end(), bytes.data() + offset);
    return Raster::Make(width, height, RasterFormat::Bgra8888, bytes.data(), bytes.size());
}

Raster Columns(int width, int height)
{
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(width) * height * 4);
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
        {
            const std::uint8_t value = (x & 1) == 0 ? 0 : 255;
            const std::size_t offset = (static_cast<std::size_t>(y) * width + x) * 4;
            bytes[offset + 0] = value;
            bytes[offset + 1] = value;
            bytes[offset + 2] = value;
            bytes[offset + 3] = 255;
        }
    return Raster::Make(width, height, RasterFormat::Bgra8888, bytes.data(), bytes.size());
}

CanvasDocument MakeDocument(int width, int height, Raster image)
{
    CanvasDocument document;
    document.width = width;
    document.height = height;
    RecordRoot<ImageLayer>::Builder layers(1);
    ImageLayer layer =
        ImageLayer::Blank(L"Recorded display fixture", Vec2{static_cast<double>(width), static_cast<double>(height)});
    layer.transform.sampling = LayerSampling::Nearest;
    layer.asset = {std::move(image), {}, L"Recorded display fixture"};
    if (!layers.Append(std::move(layer)))
        return {};
    auto root = layers.Seal();
    if (root)
        document.layers = std::move(*root);
    return document;
}

bool MakeFixtures(std::array<CanvasDocument, kGraphCount>& documents)
{
    std::vector<std::uint8_t> pairPixels{0, 0, 0, 255, 255, 255, 255, 255}; // literal BGRA black, white
    Raster pair = Raster::Make(2, 1, RasterFormat::Bgra8888, pairPixels.data(), pairPixels.size());
    Raster stripes = Columns(64, 64);

    // A true sparse 513x513 raster: the opaque base is shared and the replacement is a separately owned tile.
    Raster base = Solid(513, 513, {31, 63, 95, 255});
    std::vector<RasterTile> tiles;
    tiles.push_back(RasterTile{256, 256, Solid(1, 1, {255, 191, 127, 255})});
    Raster tiled = Raster::MakeSparse(513, 513, RasterFormat::Bgra8888, base, 0, 0, std::move(tiles), 0);
    if (!pair.HasStorage() || !stripes.HasStorage() || !base.HasStorage() || !tiled.IsSparse() ||
        tiled.IsMaterialized())
        return false;

    documents[static_cast<std::size_t>(Fixture::Pair)] = MakeDocument(2, 1, std::move(pair));
    documents[static_cast<std::size_t>(Fixture::Stripes)] = MakeDocument(64, 64, std::move(stripes));
    documents[static_cast<std::size_t>(Fixture::Tiled)] = MakeDocument(513, 513, std::move(tiled));
    return documents[0].layers.size() == 1 && documents[1].layers.size() == 1 && documents[2].layers.size() == 1;
}

class ParentState final : public GpuCommandSourceReceiver, public GpuCompletionReceiver
{
  public:
    ParentState(JobScheduler& scheduler, GpuWorkQueue& queue, GpuCommandPump& pump, ID3D11Device* device,
                ID3D11DeviceContext* immediate) noexcept
        : scheduler(scheduler), queue(queue), pump(pump), device(device), immediate(immediate),
          deviceGeneration(queue.DeviceGeneration())
    {
    }

    JobScheduler& scheduler;
    GpuWorkQueue& queue;
    GpuCommandPump& pump;
    ID3D11Device* const device;
    ID3D11DeviceContext* const immediate;
    const std::uint64_t deviceGeneration;
    std::array<CanvasDocument, kGraphCount> documents;
    std::array<RecordedDisplayGraph, kGraphCount> graphs;
    std::array<GpuCommandStream, kGraphCount> streams;
    std::array<GpuCompletionContext, kGraphCount> retiredCapture;
    GpuCompletionContext lastUseCapture;
    std::array<bool, kGraphCount> streamInitialized{};
    std::array<bool, kGraphCount> streamAuthorized{};
    std::array<bool, kGraphCount> streamRetired{};
    std::array<bool, kGraphCount> graphDetached{};
    std::array<bool, kGraphCount> graphRetired{};
    GpuCommandSourceLease sourceLease{};
    JobTicket original{};
    Phase phase = Phase::Opening;
    JobResult workerResult{};
    bool stageAcknowledged = false;
    bool producerJoined = false;
    bool sourceAttached = false;
    bool sourceFailed = false;
    HRESULT sourceError = S_OK;
    unsigned int originalAcknowledgements = 0;
    bool lastUseAcknowledged = false;
    HRESULT lastUseResult = E_PENDING;
    bool blitsPassed = false;
    bool terminal = false;
    bool completedSuccessfully = false;
    bool cleanupPassed = true;
    std::size_t index = 0;

    void CommandAcknowledgedOwner(JobTicket ticket, GpuCommandLease command, HRESULT result) noexcept override
    {
        Expect("gpu-callback-carries-original-ticket",
               ticket == original && command.source == original && command.deviceGeneration == deviceGeneration);
        if (ticket != original || command.source != original || command.deviceGeneration != deviceGeneration)
        {
            sourceFailed = true;
            sourceError = E_INVALIDARG;
            return;
        }
        if (SUCCEEDED(result))
            ++originalAcknowledgements;
        else
        {
            sourceFailed = true;
            sourceError = result;
        }
    }

    void CommandSourceFailedOwner(JobTicket ticket, HRESULT error) noexcept override
    {
        Expect("source-failure-is-not-an-acknowledgement", ticket == original && FAILED(error));
        sourceFailed = true;
        sourceError = FAILED(error) ? error : E_UNEXPECTED;
    }

    void GpuCompletionReady(JobTicket ticket, std::uint64_t generation, HRESULT result) noexcept override
    {
        Expect("post-blit-barrier-is-captured-original-stream", ticket == original && generation == deviceGeneration);
        lastUseAcknowledged = ticket == original && generation == deviceGeneration;
        lastUseResult = result;
        if (FAILED(result))
            sourceFailed = true;
    }

    [[nodiscard]] bool StartStream(std::size_t graphIndex) noexcept
    {
        auto& stream = streams[graphIndex];
        const HRESULT initialized = stream.InitializeOwner(scheduler, original, deviceGeneration);
        if (FAILED(initialized))
        {
            sourceFailed = true;
            sourceError = initialized;
            return false;
        }
        streamInitialized[graphIndex] = true;
        const HRESULT attached = pump.AttachOwner(stream, original, *this, sourceLease);
        if (FAILED(attached))
        {
            sourceFailed = true;
            sourceError = attached;
            return false;
        }
        sourceAttached = true;
        producerJoined = false;
        return true;
    }

    void DetachGraphsForRetirement() noexcept
    {
        for (std::size_t graph = 0; graph < kGraphCount; ++graph)
        {
            if (!graphDetached[graph] && !graphRetired[graph])
            {
                graphDetached[graph] = graphs[graph].DetachOwner();
                cleanupPassed = cleanupPassed && graphDetached[graph];
            }
        }
    }

    void BeginGraphRetirement() noexcept
    {
        DetachGraphsForRetirement();
        phase = Phase::GraphRetirement;
        stageAcknowledged = false;
        Expect("same-original-ticket-resumes-for-graph-retirement",
               scheduler.Resume(original, JobExecutionLane::Compute));
    }

    void AdvanceOwner() noexcept
    {
        if (phase == Phase::Recording && producerJoined && streamInitialized[index] && streams[index].CanRetireOwner())
        {
            HRESULT producerStatus = E_PENDING;
            const bool finished = streams[index].ProducerFinishedOwner(producerStatus);
            Expect("joined-producer-and-stream-terminal-before-detach", finished);
            if (!finished)
                return;

            if (sourceAttached)
            {
                const bool detached = pump.DetachOwner(sourceLease, retiredCapture[index]);
                Expect("pump-capture-detaches-after-actual-original-acks", detached);
                if (!detached)
                    return;
                sourceAttached = false;
                sourceLease = {}; // Detach accepts the lease by value; the next Attach requires an empty output.
            }
            const bool authorized = streams[index].AuthorizeRetirementOwner();
            Expect("stream-retirement-authorized-after-real-acks", authorized);
            if (!authorized)
                return;
            streamAuthorized[index] = true;
            phase = Phase::StreamRetirement;
            Expect("same-ticket-worker-retires-captured-transport",
                   scheduler.Resume(original, JobExecutionLane::Compute));
            return;
        }

        if (phase == Phase::LastUseBarrier && lastUseAcknowledged)
        {
            if (lastUseResult != S_OK)
                return; // Keep all graphs and the original charge until joined shutdown destruction.
            Expect("last-use-barrier-is-successful-before-graph-detach", true);
            BeginGraphRetirement();
            return;
        }

        if (!stageAcknowledged)
            return;
        stageAcknowledged = false;

        if (phase == Phase::Opening)
        {
            original = workerTicket;
            if (workerResult.status != JobStatus::Finished)
            {
                BeginGraphRetirement();
                return;
            }
            index = 0;
            if (!StartStream(index))
            {
                // The initialized stream has no producer yet. Resume its real recorder stage so it publishes
                // a failed terminal state; an uninitialized stream has no resources to retire.
                if (streamInitialized[index])
                {
                    phase = Phase::Recording;
                    workerResult = {JobStatus::Failed, static_cast<std::int32_t>(sourceError)};
                    Expect("failed-attach-still-finishes-original-stream-on-worker",
                           scheduler.Resume(original, JobExecutionLane::GpuRecording));
                }
                else
                {
                    BeginGraphRetirement();
                }
                return;
            }
            phase = Phase::Recording;
            Expect("opening-ack-precedes-recording-lane-resume",
                   scheduler.Resume(original, JobExecutionLane::GpuRecording));
            return;
        }

        if (phase == Phase::Recording)
        {
            producerJoined = true;
            if (workerResult.status != JobStatus::Finished)
            {
                sourceFailed = true;
                if (workerResult.status == JobStatus::Failed)
                    sourceError = static_cast<HRESULT>(workerResult.error);
            }
            return; // AdvanceOwner waits for stream.CanRetireOwner and every executing callback.
        }

        if (phase == Phase::StreamRetirement)
        {
            if (!streamRetired[index])
            {
                cleanupPassed = false;
                sourceFailed = true;
            }
            const bool graphCanPublish = workerResult.status == JobStatus::Finished && !sourceFailed &&
                                         streams[index].CanRetireOwner() && streamRetired[index];
            if (!graphCanPublish)
            {
                if (!streamRetired[index] || !streams[index].CanRetireOwner())
                {
                    // Leave the graph attached until joined shutdown retires the actual stream first.
                    BeginGraphRetirement();
                    return;
                }
                phase = Phase::UnpublishedRetirement;
                stageAcknowledged = false;
                Expect("same-ticket-retires-unpublished-graph-after-original-stream",
                       scheduler.Resume(original, JobExecutionLane::Compute));
                return;
            }

            const HRESULT published = graphs[index].PublishOwner(immediate);
            Expect("publish-only-after-joined-recording-and-retired-original-stream", SUCCEEDED(published));
            if (FAILED(published))
            {
                sourceFailed = true;
                phase = Phase::UnpublishedRetirement;
                stageAcknowledged = false;
                Expect("publish-failure-retains-worker-only-graph-retirement",
                       scheduler.Resume(original, JobExecutionLane::Compute));
                return;
            }
            const bool exactPublished = graphs[index].IsPublishedOnOwner(device, immediate);
            Expect("published-graph-matches-actual-device-and-original-context", exactPublished);
            if (!exactPublished)
            {
                sourceFailed = true;
                BeginGraphRetirement();
                return;
            }
            ++index;
            if (index < kGraphCount)
            {
                if (!StartStream(index))
                {
                    if (streamInitialized[index])
                    {
                        phase = Phase::Recording;
                        workerResult = {JobStatus::Failed, static_cast<std::int32_t>(sourceError)};
                        Expect("later-attach-failure-finishes-stream-on-original-job",
                               scheduler.Resume(original, JobExecutionLane::GpuRecording));
                    }
                    else
                    {
                        BeginGraphRetirement();
                    }
                    return;
                }
                phase = Phase::Recording;
                Expect("next-graph-uses-same-original-ticket-and-recording-lane",
                       scheduler.Resume(original, JobExecutionLane::GpuRecording));
                return;
            }

            const bool allPublished = std::all_of(graphs.begin(), graphs.end(), [this](const auto& graph) noexcept
                                                  { return graph.IsPublishedOnOwner(device, immediate); });
            Expect("all-three-exact-graphs-published-before-blit-tests", allPublished);
            if (!allPublished)
            {
                sourceFailed = true;
                BeginGraphRetirement();
                return;
            }
            RecordedBlitGraphHarness pair{*graphs[0].CompositionForTestOwner(), device, immediate};
            RecordedBlitGraphHarness stripes{*graphs[1].CompositionForTestOwner(), device, immediate};
            RecordedBlitGraphHarness tiled{*graphs[2].CompositionForTestOwner(), device, immediate};
            blitsPassed = RunRecordedBlitTests(pair, stripes, tiled) == 0;
            Expect("recorded-blit-tests-run-after-three-actual-publishes", blitsPassed);
            const HRESULT captured = queue.CaptureCompletionContext(lastUseCapture);
            Expect("capture-original-context-after-recorded-blits", SUCCEEDED(captured));
            if (FAILED(captured))
            {
                sourceFailed = true;
                return; // No barrier means no graph detach; joined shutdown owns destruction.
            }
            const HRESULT barrier = queue.EnqueueRetirementCompletion(lastUseCapture, original, *this);
            Expect("enqueue-real-post-blit-original-completion-barrier", SUCCEEDED(barrier));
            if (FAILED(barrier))
            {
                sourceFailed = true;
                return; // Refusal is not a GPU acknowledgement.
            }
            phase = Phase::LastUseBarrier;
            return;
        }

        if (phase == Phase::UnpublishedRetirement)
        {
            if (workerResult.status != JobStatus::Finished || !graphRetired[index])
                cleanupPassed = false;
            BeginGraphRetirement();
            return;
        }
    }

    JobTicket workerTicket{};

    void ShutdownOwnerBeforeQueueClose() noexcept
    {
        pump.AcknowledgeJoinedShutdownOwner();
        if (sourceAttached && index < kGraphCount)
        {
            const bool detached = pump.DetachOwner(sourceLease, retiredCapture[index]);
            cleanupPassed = cleanupPassed && detached;
            sourceAttached = !detached;
            if (detached)
                sourceLease = {};
        }
        for (std::size_t graph = 0; graph < kGraphCount; ++graph)
        {
            if (streamInitialized[graph] && !streamAuthorized[graph] && streams[graph].CanRetireOwner())
                streamAuthorized[graph] = streams[graph].AuthorizeRetirementOwner();
        }
    }

    void ShutdownGraphsOwner() noexcept
    {
        DetachGraphsForRetirement();
    }

    void ShutdownWorker() noexcept
    {
        for (std::size_t graph = 0; graph < kGraphCount; ++graph)
        {
            if (streamInitialized[graph] && streamAuthorized[graph] && !streamRetired[graph])
                streamRetired[graph] = streams[graph].RetireForWork();
            retiredCapture[graph] = {};
            if (graphDetached[graph] && !graphRetired[graph])
                graphRetired[graph] = graphs[graph].RetireForWork();
            cleanupPassed = cleanupPassed && (!streamInitialized[graph] || streamRetired[graph]) && graphRetired[graph];
        }
        documents = {};
        lastUseCapture = {};
    }
};

class RecordedBlitParentJob final : public Job
{
  public:
    explicit RecordedBlitParentJob(ParentState& state) noexcept : _state(state) {}

    JobResult Run(JobContext& context) override
    {
        if (_state.phase == Phase::Opening)
            return MakeFixtures(_state.documents) ? JobResult{} : JobResult{JobStatus::Failed, E_OUTOFMEMORY};
        if (_state.phase == Phase::Recording)
        {
            if (!_state.sourceAttached)
                return {JobStatus::Failed, static_cast<std::int32_t>(_state.sourceError)};
            const HRESULT result =
                _state.graphs[_state.index].PrepareForWork(_state.device, std::move(_state.documents[_state.index]),
                                                           _state.streams[_state.index], context.cancellation);
            return FAILED(result) ? JobResult{JobStatus::Failed, static_cast<std::int32_t>(result)} : JobResult{};
        }
        return {};
    }

    void RetireWorkerStage(JobContext&, JobResult result) noexcept override
    {
        if (_state.phase == Phase::Opening && result.status != JobStatus::Finished)
        {
            _state.documents = {};
        }
        else if (_state.phase == Phase::Recording && _state.streamInitialized[_state.index])
        {
            const HRESULT producer = result.status == JobStatus::Finished    ? S_OK
                                     : result.status == JobStatus::Cancelled ? E_ABORT
                                                                             : static_cast<HRESULT>(result.error);
            _state.streams[_state.index].FinishProducerForWork(producer);
        }
        else if (_state.phase == Phase::StreamRetirement)
        {
            _state.streamRetired[_state.index] = _state.streams[_state.index].RetireForWork();
            _state.retiredCapture[_state.index] = {};
        }
        else if (_state.phase == Phase::UnpublishedRetirement)
        {
            _state.graphRetired[_state.index] = _state.graphs[_state.index].RetireUnpublishedForWork();
        }
        else if (_state.phase == Phase::GraphRetirement)
        {
            _state.ShutdownWorker();
        }
    }

    bool DeferRetirement(JobResult result, JobTicket ticket) noexcept override
    {
        _state.workerTicket = ticket;
        if (_state.phase == Phase::Opening && result.status == JobStatus::Failed)
            return false;
        if (_state.phase == Phase::GraphRetirement)
            return false; // The worker retirement hook ran before this owner acknowledgement.
        _state.workerResult = result;
        _state.stageAcknowledged = true;
        return true;
    }

    void Completed(JobResult result) noexcept override
    {
        _state.completedSuccessfully = result.status == JobStatus::Finished && _state.blitsPassed &&
                                       _state.lastUseAcknowledged && _state.lastUseResult == S_OK &&
                                       _state.cleanupPassed;
        Expect("complete-reservation-still-held-through-final-worker-retirement",
               _state.scheduler.ReservedMemoryBytes() == kFixtureParentCharge);
        _state.terminal = true;
        _state.phase = Phase::Terminal;
        Expect("original-charge-refunded-only-after-terminal-worker-retirement", _state.completedSuccessfully);
    }

  private:
    ParentState& _state;
};

bool WaitForParent(JobScheduler& scheduler, OwnerWorkMultiplexer& routes, ParentState& state) noexcept
{
    const ULONGLONG started = GetTickCount64();
    while (!state.terminal)
    {
        (void)DrainCompletions(scheduler); // Also pumps one ready owner source through the real router.
        state.AdvanceOwner();
        if (state.terminal)
            break;
        if (routes.HasReady() || state.stageAcknowledged ||
            (state.phase == Phase::Recording && state.producerJoined && state.streams[state.index].CanRetireOwner()) ||
            (state.phase == Phase::LastUseBarrier && state.lastUseAcknowledged && state.lastUseResult == S_OK))
            continue;
        const ULONGLONG elapsed = GetTickCount64() - started;
        if (elapsed >= 10000)
            return false;
        const DWORD wait = static_cast<DWORD>(10000 - elapsed); // Failure bound only; readiness is event driven.
        if (WaitForSingleObject(static_cast<HANDLE>(scheduler.WakeHandle()), wait) != WAIT_OBJECT_0)
            return false;
    }
    return state.completedSuccessfully;
}

bool RunDevice(D3D_DRIVER_TYPE driver, bool optionalHardware)
{
    wil::com_ptr_nothrow<ID3D11Device> device;
    wil::com_ptr_nothrow<ID3D11DeviceContext> immediate;
    constexpr D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
    HRESULT result = D3D11CreateDevice(nullptr, driver, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, &level, 1,
                                       D3D11_SDK_VERSION, device.put(), nullptr, immediate.put());
    if (FAILED(result))
    {
        Expect(optionalHardware ? "hardware-device-unavailable-skipped" : "warp-device-created", optionalHardware);
        return optionalHardware;
    }
    D3D11_FEATURE_DATA_THREADING threading{};
    result = device->CheckFeatureSupport(D3D11_FEATURE_THREADING, &threading, sizeof(threading));
    if (FAILED(result) || !threading.DriverConcurrentCreates)
    {
        Expect(optionalHardware ? "hardware-concurrent-create-unavailable-skipped"
                                : "warp-supports-worker-resource-creation",
               optionalHardware);
        return optionalHardware;
    }

    JobScheduler scheduler;
    result = static_cast<HRESULT>(scheduler.Initialize(1));
    if (SUCCEEDED(result))
        result = static_cast<HRESULT>(scheduler.EnableGpuRecordingLane());
    if (FAILED(result))
    {
        Expect("scheduler-and-single-recorder-lane-initialize", false);
        scheduler.Close();
        return false;
    }

    OwnerWorkMultiplexer routes;
    GpuWorkQueue queue;
    GpuCommandPump pump;
    bool routed = scheduler.RegisterOwnerWork(routes);
    result = routed ? queue.Initialize(scheduler, &routes) : E_FAIL;
    if (SUCCEEDED(result))
        result = queue.Bind(device.get(), immediate.get());
    if (SUCCEEDED(result))
        result = pump.InitializeOwner(scheduler, queue, routes);
    if (FAILED(result))
    {
        queue.StopNotifications();
        scheduler.Close();
        queue.CloseAfterWorkers();
        (void)pump.CloseOwner();
        Expect("actual-original-gpu-queue-and-command-pump-initialize", false);
        return false;
    }

    ParentState state{scheduler, queue, pump, device.get(), immediate.get()};
    std::unique_ptr<Job> parent = std::make_unique<RecordedBlitParentJob>(state);
    JobTicket original{};
    result = static_cast<HRESULT>(scheduler.Submit(parent,
                                                   {.kind = JobClass::Background,
                                                    .memoryBytes = kFixtureParentCharge,
                                                    .deferredRetirement = true,
                                                    .executionLane = JobExecutionLane::Compute},
                                                   original));
    state.original = original;
    Expect("one-fixture-only-parent-reserves-complete-three-graph-peak",
           SUCCEEDED(result) && !parent && scheduler.ReservedMemoryBytes() == kFixtureParentCharge);
    if (FAILED(result))
    {
        queue.StopNotifications();
        scheduler.Close();
        queue.CloseAfterWorkers();
        (void)pump.CloseOwner();
        return false;
    }
    Expect("original-ticket-retains-full-charge-through-all-graphs",
           scheduler.ReservedMemoryBytes() == kFixtureParentCharge);

    const bool passed = WaitForParent(scheduler, routes, state);
    if (!passed)
    {
        Expect("10-second-event-wait-bound-reports-failure-without-fabricated-ack", false);
        pump.StopOwner();
        queue.StopNotifications();
        scheduler.Close({[](void* context) noexcept
                         {
                             auto& state = *static_cast<ParentState*>(context);
                             state.ShutdownOwnerBeforeQueueClose();
                             state.queue.CloseAfterWorkers();
                             state.ShutdownGraphsOwner();
                         },
                         [](void* context) noexcept { static_cast<ParentState*>(context)->ShutdownWorker(); }, &state});
        (void)pump.CloseOwner();
        return false;
    }

    pump.StopOwner();
    queue.StopNotifications();
    scheduler.Close({[](void* context) noexcept
                     {
                         auto& state = *static_cast<ParentState*>(context);
                         state.ShutdownOwnerBeforeQueueClose();
                         state.queue.CloseAfterWorkers();
                         state.ShutdownGraphsOwner();
                     },
                     [](void* context) noexcept { static_cast<ParentState*>(context)->ShutdownWorker(); }, &state});
    const bool pumpClosed = pump.CloseOwner();
    Expect("stable-pump-route-closes-after-all-borrowers", pumpClosed);
    Expect("all-three-real-graph-and-stream-owners-retired",
           state.cleanupPassed && state.streamRetired[0] && state.streamRetired[1] && state.streamRetired[2] &&
               state.graphRetired[0] && state.graphRetired[1] && state.graphRetired[2]);
    Expect("whole-fixture-charge-is-zero-after-worker-retirement", scheduler.ReservedMemoryBytes() == 0);
    std::printf("INFO recorded-blit-parent.driver=%s original=%zu:%llu acks=%u charge=%llu\n",
                driver == D3D_DRIVER_TYPE_WARP ? "WARP" : "hardware", original.slot,
                static_cast<unsigned long long>(original.generation), state.originalAcknowledgements,
                static_cast<unsigned long long>(kFixtureParentCharge));
    return passed && pumpClosed && state.cleanupPassed;
}
} // namespace

int RunRecordedDisplayParentTests()
{
    gFailures = 0;
    const bool warp = RunDevice(D3D_DRIVER_TYPE_WARP, false);
    const bool hardware = RunDevice(D3D_DRIVER_TYPE_HARDWARE, true);
    return warp && hardware && gFailures == 0 ? 0 : 1;
}
