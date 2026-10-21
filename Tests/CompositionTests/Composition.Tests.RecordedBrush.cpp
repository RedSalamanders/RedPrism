#include "Composition.Tests.RecordedBrush.h"

#include "BrushCompute.h"
#include "GpuCommandPump.h"
#include "GpuRecordingCommands.h"
#include "GpuWork.h"
#include "Jobs.h"
#include "OwnerWorkMultiplexer.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <memory>
#include <objbase.h>
#include <vector>

namespace
{
constexpr std::uint64_t kCharge = 128ULL * 1024 * 1024;
std::atomic<unsigned int> gFailures{0};
void Expect(bool passed, const char* name) noexcept
{
    std::printf("%s recorded-brush.%s\n", passed ? "PASS" : "FAIL", name);
    std::fflush(stdout);
    gFailures += passed ? 0u : 1u;
}
enum class Phase
{
    Opening,
    Recording,
    StreamRetirement,
    Readback,
    Copy,
    Barrier,
    Retirement,
    Terminal
};

struct State final : GpuCommandSourceReceiver, GpuBufferReadbackReceiver, GpuCompletionReceiver
{
    JobScheduler& jobs;
    GpuWorkQueue& queue;
    GpuCommandPump& pump;
    ID3D11Device* device;
    ID3D11DeviceContext* immediate;
    DWORD ownerThread = GetCurrentThreadId();
    double hardness;
    std::size_t segmentCount;
    std::unique_ptr<GpuBrushCoverage::StrokeEpoch> epoch;
    std::unique_ptr<GpuBrushCoverage::StrokeEpoch> returnedEpoch;
    GpuBrushCoverage live;
    GpuBrushCoverage reference;
    GpuCommandStream stream;
    GpuCommandSourceLease source;
    GpuCompletionContext originalContext;
    GpuCompletionContext commandRetirement;
    JobTicket original;
    std::array<BrushKernelTile, 2> tiles{};
    std::array<std::vector<std::uint8_t>, 2> preview;
    std::array<std::vector<std::uint8_t>, 2> expected;
    std::vector<BrushSegment> settled;
    BrushKernelParams params;
    GpuBufferReadbackValues mapped;
    Phase phase = Phase::Opening;
    JobResult result;
    bool stageReady = false;
    bool streamInitialized = false;
    bool streamAuthorized = false;
    bool streamRetired = false;
    bool producerJoined = false;
    bool failed = false;
    bool terminal = false;
    bool parity = true;
    bool workerRetired = false;
    unsigned int acknowledgements = 0;
    std::size_t nextTile = 0;

    State(JobScheduler& jobs, GpuWorkQueue& queue, GpuCommandPump& pump, ID3D11Device* device,
          ID3D11DeviceContext* immediate, double hardness, std::size_t segmentCount) noexcept
        : jobs(jobs), queue(queue), pump(pump), device(device), immediate(immediate), hardness(hardness),
          segmentCount(segmentCount)
    {
    }

    void MakeForWork()
    {
        epoch = std::make_unique<GpuBrushCoverage::StrokeEpoch>();
        returnedEpoch = std::make_unique<GpuBrushCoverage::StrokeEpoch>();
        params.pixelToDocument = {0.9, 0.1, -0.1, 0.9, 7.0, 9.0};
        params.canvasWidth = 512;
        params.canvasHeight = 128;
        params.radius = 5.0;
        params.hardness = hardness;
        params.antialiasWidth = 0.9;
        params.spacing = 0.25;
        constexpr std::array<std::array<int, 4>, 2> grid{{{0, 0, 65, 49}, {128, 0, 17, 33}}};
        for (std::size_t i = 0; i < tiles.size(); ++i)
        {
            const auto& extent = grid[i];
            const auto count = static_cast<std::size_t>(extent[2]) * extent[3];
            preview[i].assign(count, 0xA5);
            expected[i].assign(count, 0x5A);
            tiles[i] = {19 + i, extent[0], extent[1], extent[2], extent[3], nullptr, preview[i].data()};
        }
        settled.assign(segmentCount, BrushSegment{{400, 400}, {401, 401}});
        settled.front() = {{10, 18}, {60, 42}};
        settled.back() = {{126, 26}, {146, 39}};
    }

    bool PrepareOwner() noexcept
    {
        const BrushSegment initial{{12, 12}, {58, 12}};
        const BrushSegment provisional{{58, 12}, {61, 24}};
        if (FAILED(live.Initialize(device, immediate)) || FAILED(reference.Initialize(device, immediate)))
            return false;
        auto referenceTiles = tiles;
        for (std::size_t i = 0; i < tiles.size(); ++i)
            referenceTiles[i].preview = expected[i].data();
        if (!live.Render(tiles, {&initial, 1}, {&provisional, 1}, params) ||
            !reference.Render(referenceTiles, {&initial, 1}, {&provisional, 1}, params) ||
            !reference.Render(referenceTiles, settled, {}, params))
            return false;
        reference.EndStroke();
        if (!live.TakeStrokeEpoch(*epoch) || FAILED(queue.CaptureCompletionContext(originalContext)))
            return false;
        Expect(epoch->TileCount() == 2 && live.TileCount() == 0 && epoch->GpuBytes() != 0,
               "original-density-transfers-before-private-recording");
        if (FAILED(stream.InitializeOwner(jobs, original, queue.DeviceGeneration())))
            return false;
        streamInitialized = true;
        return SUCCEEDED(pump.AttachOwner(stream, original, *this, source));
    }

    void CommandAcknowledgedOwner(JobTicket ticket, GpuCommandLease lease, HRESULT status) noexcept override
    {
        Expect(ticket == original && lease.source == original && lease.deviceGeneration == queue.DeviceGeneration(),
               "every-real-command-ack-keeps-original-parent-ticket");
        failed = failed || FAILED(status) || ticket != original || lease.source != original;
        ++acknowledgements;
    }
    void CommandSourceFailedOwner(JobTicket ticket, HRESULT status) noexcept override
    {
        Expect(ticket == original && FAILED(status), "command-failure-never-fabricates-success");
        failed = true;
    }
    void BufferReadbackReady(JobTicket ticket, GpuBufferReadbackValues values, HRESULT status) noexcept override
    {
        Expect(phase == Phase::Readback && ticket == original && SUCCEEDED(status) && values.data &&
                   values.deviceGeneration == queue.DeviceGeneration(),
               "typed-readback-delivers-original-source");
        if (phase != Phase::Readback || ticket != original || FAILED(status) || !values.data)
        {
            failed = true;
            return;
        }
        mapped = values;
        phase = Phase::Copy;
        if (!jobs.Resume(original, JobExecutionLane::Compute))
            failed = true;
    }
    void GpuCompletionReady(JobTicket ticket, std::uint64_t generation, HRESULT status) noexcept override
    {
        Expect(phase == Phase::Barrier && ticket == original && generation == originalContext.DeviceGeneration() &&
                   SUCCEEDED(status),
               "post-readback-original-use-barrier-acknowledges-before-retirement");
        if (FAILED(status) || ticket != original || phase != Phase::Barrier)
        {
            failed = true;
            return;
        }
        auto* coverage =
            GpuBrushCoverage::RecordedEpochBufferOwner(*epoch, device, tiles[0].key, tiles[0].width, tiles[0].height);
        const auto gpuBytes = epoch->GpuBytes();
        Expect(live.RestoreStrokeEpoch(*epoch) && live.TileCount() == 2 && !epoch->HasResources() &&
                   live.StrokeGpuOwnershipBytes() == gpuBytes &&
                   live.CoverageBuffer(tiles[0].key, tiles[0].width, tiles[0].height) == coverage,
               "joined-original-epoch-returns-without-resetting-density-or-allocating-tiles");
        Expect(!live.RestoreStrokeEpoch(*epoch), "returned-epoch-is-single-use");
        // Swap it back to an empty preallocated owner for MTA destruction. No new GPU use was recorded.
        Expect(live.TakeStrokeEpoch(*returnedEpoch) && live.TileCount() == 0 && returnedEpoch->TileCount() == 2,
               "returned-density-transfers-back-for-final-worker-retirement");
        phase = Phase::Retirement;
        if (!jobs.Resume(original, JobExecutionLane::Compute))
            failed = true;
    }
    void EnqueueNextOwner() noexcept
    {
        const auto& tile = tiles[nextTile];
        auto* buffer = GpuBrushCoverage::RecordedEpochBufferOwner(*epoch, device, tile.key, tile.width, tile.height);
        Expect(buffer &&
                   !GpuBrushCoverage::RecordedEpochBufferOwner(*epoch, nullptr, tile.key, tile.width, tile.height),
               "joined-epoch-borrow-requires-original-device-and-exact-tile");
        phase = Phase::Readback;
        if (!buffer || FAILED(queue.EnqueueBufferReadback(buffer, static_cast<unsigned int>(tile.width),
                                                          static_cast<unsigned int>(tile.height), original, *this)))
            failed = true;
    }
    void AdvanceOwner() noexcept
    {
        if (failed)
            return;
        if (phase == Phase::Recording && producerJoined && stream.CanRetireOwner())
        {
            if (!pump.DetachOwner(source, commandRetirement) || !stream.AuthorizeRetirementOwner())
            {
                failed = true;
                return;
            }
            source = {};
            streamAuthorized = true;
            producerJoined = false;
            phase = Phase::StreamRetirement;
            if (!jobs.Resume(original, JobExecutionLane::Compute))
                failed = true;
            return;
        }
        if (!stageReady)
            return;
        stageReady = false;
        if (result.status != JobStatus::Finished)
        {
            failed = true;
            return;
        }
        if (phase == Phase::Opening)
        {
            if (!PrepareOwner())
            {
                failed = true;
                return;
            }
            phase = Phase::Recording;
            if (!jobs.Resume(original, JobExecutionLane::GpuRecording))
                failed = true;
        }
        else if (phase == Phase::Recording)
            producerJoined = true;
        else if (phase == Phase::StreamRetirement)
        {
            Expect(streamRetired && acknowledgements != 0, "actual-producer-join-and-all-acks-precede-buffer-borrow");
            if (!streamRetired || acknowledgements == 0)
            {
                failed = true;
                return;
            }
            EnqueueNextOwner();
        }
        else if (phase == Phase::Copy)
        {
            Expect(queue.ReleaseMapped(mapped.lease), "exact-typed-map-is-released-after-worker-copy");
            mapped = {};
            if (++nextTile < tiles.size())
                EnqueueNextOwner();
            else
            {
                phase = Phase::Barrier;
                if (FAILED(queue.EnqueueRetirementCompletion(originalContext, original, *this)))
                    failed = true;
            }
        }
    }
    void RetireForWork() noexcept
    {
        if (streamInitialized && streamAuthorized && !streamRetired)
            streamRetired = stream.RetireForWork();
        epoch.reset();
        returnedEpoch.reset();
        originalContext = {};
        commandRetirement = {};
        std::vector<BrushSegment>{}.swap(settled);
        for (auto& pixels : preview)
            std::vector<std::uint8_t>{}.swap(pixels);
        for (auto& pixels : expected)
            std::vector<std::uint8_t>{}.swap(pixels);
        APTTYPE apartment = APTTYPE_CURRENT;
        APTTYPEQUALIFIER qualifier = APTTYPEQUALIFIER_NONE;
        workerRetired = GetCurrentThreadId() != ownerThread && SUCCEEDED(CoGetApartmentType(&apartment, &qualifier)) &&
                        apartment == APTTYPE_MTA;
    }
};

class RecordingJob final : public Job
{
  public:
    explicit RecordingJob(State& state) noexcept : _state(state) {}
    JobResult Run(JobContext& context) override
    {
        auto& state = _state;
        if (state.phase == Phase::Opening)
        {
            state.MakeForWork();
            return {};
        }
        if (state.phase == Phase::Recording)
        {
            wil::com_ptr_nothrow<ID3D11DeviceContext> deferred;
            HRESULT result = state.device->CreateDeferredContext(0, deferred.put());
            if (FAILED(result))
                return {JobStatus::Failed, result};
            GpuRecordingCommands commands;
            result = commands.InitializeForWork(deferred.get(), state.stream, context.cancellation);
            if (FAILED(result))
                return {JobStatus::Failed, result};
            const auto unchanged = state.epoch->GpuBytes();
            const auto refused = GpuBrushCoverage::RecordEpochForWork(*state.epoch, state.immediate, commands,
                                                                      state.tiles, state.settled, {}, state.params);
            Expect(refused == E_INVALIDARG && state.epoch->GpuBytes() == unchanged,
                   "worker-refuses-immediate-context-before-changing-captured-graph");
            Expect(!GpuBrushCoverage::RecordedEpochBufferOwner(*state.epoch, state.device, 19, 65, 49),
                   "owner-buffer-borrow-refuses-the-recorder-thread");
            result = GpuBrushCoverage::RecordEpochForWork(*state.epoch, deferred.get(), commands, state.tiles,
                                                          state.settled, {}, state.params);
            return FAILED(result) ? JobResult{JobStatus::Failed, result} : JobResult{};
        }
        if (state.phase == Phase::Copy)
        {
            const auto& values = state.mapped;
            bool equal = values.count == state.expected[state.nextTile].size();
            for (std::size_t i = 0; equal && i < values.count; ++i)
                if (values.data[i] != state.expected[state.nextTile][i])
                {
                    std::printf("INFO recorded-brush.mismatch tile=%zu pixel=%zu expected=%u actual=%u hardness=%g "
                                "segments=%zu\n",
                                state.nextTile, i, state.expected[state.nextTile][i], values.data[i], state.hardness,
                                state.segmentCount);
                    equal = false;
                }
            state.parity = state.parity && equal;
            return equal ? JobResult{} : JobResult{JobStatus::Failed, E_FAIL};
        }
        return {};
    }
    void RetireWorkerStage(JobContext&, JobResult result) noexcept override
    {
        auto& state = _state;
        if (state.phase == Phase::Recording && state.streamInitialized)
            state.stream.FinishProducerForWork(result.status == JobStatus::Finished    ? S_OK
                                               : result.status == JobStatus::Cancelled ? E_ABORT
                                                                                       : result.error);
        else if (state.phase == Phase::StreamRetirement)
            state.streamRetired = state.stream.RetireForWork();
        else if (state.phase == Phase::Retirement)
            state.RetireForWork();
    }
    bool DeferRetirement(JobResult result, JobTicket ticket) noexcept override
    {
        auto& state = _state;
        Expect(ticket == state.original && state.jobs.ReservedMemoryBytes() == kCharge,
               "each-stage-retains-original-ticket-and-complete-fixture-charge");
        state.result = result;
        state.stageReady = true;
        return state.phase != Phase::Retirement;
    }
    void Completed(JobResult result) noexcept override
    {
        auto& state = _state;
        Expect(result.status == JobStatus::Finished && state.parity && state.workerRetired &&
                   state.jobs.ReservedMemoryBytes() == kCharge,
               "exact-coverage-and-MTA-retirement-precede-final-refund");
        state.terminal = true;
        state.phase = Phase::Terminal;
    }

  private:
    State& _state;
};

void RunFixture(double hardness, std::size_t segmentCount)
{
    wil::com_ptr_nothrow<ID3D11Device> device;
    wil::com_ptr_nothrow<ID3D11DeviceContext> immediate;
    const D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
    HRESULT result = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, &level,
                                       1, D3D11_SDK_VERSION, device.put(), nullptr, immediate.put());
    if (FAILED(result))
    {
        Expect(false, "actual-warp-device-initializes");
        return;
    }
    JobScheduler jobs;
    OwnerWorkMultiplexer routes;
    GpuWorkQueue queue;
    GpuCommandPump pump;
    if (FAILED(jobs.Initialize(1)) || FAILED(jobs.EnableGpuRecordingLane()) || !jobs.RegisterOwnerWork(routes) ||
        FAILED(queue.Initialize(jobs, &routes)) || FAILED(queue.Bind(device.get(), immediate.get())) ||
        FAILED(pump.InitializeOwner(jobs, queue, routes)))
        std::terminate();
    State state{jobs, queue, pump, device.get(), immediate.get(), hardness, segmentCount};
    std::unique_ptr<Job> job = std::make_unique<RecordingJob>(state);
    result = jobs.Submit(job, {.kind = JobClass::Stroke, .memoryBytes = kCharge, .deferredRetirement = true},
                         state.original);
    Expect(SUCCEEDED(result) && !job, "one-original-parent-admits-the-complete-fixture-peak");
    const auto started = GetTickCount64();
    while (SUCCEEDED(result) && !state.terminal && !state.failed)
    {
        (void)DrainCompletions(jobs);
        state.AdvanceOwner();
        const auto elapsed = GetTickCount64() - started;
        if (elapsed >= 15000)
        {
            state.failed = true;
            break;
        }
        if (state.terminal || state.failed || jobs.HasUiWork() || routes.HasReady() || state.stageReady ||
            (state.phase == Phase::Recording && state.producerJoined && state.stream.CanRetireOwner()))
            continue;
        if (WaitForSingleObject(static_cast<HANDLE>(jobs.WakeHandle()), static_cast<DWORD>(15000 - elapsed)) !=
            WAIT_OBJECT_0)
            state.failed = true;
    }
    Expect(state.terminal && !state.failed && state.parity && state.workerRetired,
           "recorded-coverage-matches-exact-full-tile-reference");
    pump.StopOwner();
    queue.StopNotifications();
    jobs.Close({[](void* opaque) noexcept
                {
                    auto& state = *static_cast<State*>(opaque);
                    state.pump.AcknowledgeJoinedShutdownOwner();
                    if (state.source.generation)
                    {
                        if (!state.pump.DetachOwner(state.source, state.commandRetirement))
                            std::terminate();
                        state.source = {};
                    }
                    if (state.streamInitialized && !state.streamAuthorized)
                    {
                        HRESULT producer = E_PENDING;
                        if ((!state.stream.ProducerFinishedOwner(producer) &&
                             !state.stream.AcknowledgeEmptyJoinedShutdownOwner()) ||
                            !state.stream.AuthorizeRetirementOwner())
                            std::terminate();
                        state.streamAuthorized = true;
                    }
                    state.queue.CloseAfterWorkers();
                },
                [](void* opaque) noexcept { static_cast<State*>(opaque)->RetireForWork(); }, &state});
    Expect(pump.CloseOwner() && jobs.ReservedMemoryBytes() == 0 && jobs.ReleasingMemoryBytes() == 0 &&
               state.workerRetired,
           "actual-joined-close-detaches-routes-and-refunds-after-MTA-retirement");
}
} // namespace

int RunRecordedBrushCoverageTests()
{
    gFailures = 0;
    RunFixture(1.0, 16);
    RunFixture(0.35, 1024);
    return static_cast<int>(gFailures.load());
}
