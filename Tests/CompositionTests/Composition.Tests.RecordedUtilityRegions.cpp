// Actual original parent, command acknowledgements and mapped rows;
// no ordinary utility caller, application activation, complete product admission or performance claim.
#include "RecordedUtilityGraph.h"

#include "GpuCommandPump.h"
#include "OwnerWorkMultiplexer.h"

#include <array>
#include <cstdio>
#include <exception>
#include <memory>
#include <vector>

namespace RedPrism::CompositionTests
{
namespace
{
const char* deviceName = "uninitialized";
unsigned passed = 0;
unsigned failures = 0;
enum class Scenario : std::uint8_t
{
    SerialRegions,
    InvalidRegion,
    CancelMapped
};
void Expect(bool value, const char* name) noexcept
{
    std::printf("%s recorded-utility-regions.%s.%s\n", value ? "PASS" : "FAIL", deviceName, name);
    passed += value ? 1u : 0u;
    failures += value ? 0u : 1u;
}
bool Memory(void*, MemorySnapshot& snapshot) noexcept
{
    snapshot = {4ull << 30, 4ull << 30};
    return true;
}
constexpr std::uint64_t kFixtureCharge = 512ull << 20;
constexpr std::array<D3D11_BOX, 3> kRegions{{{0, 0, 0, 3, 2, 1}, {2, 1, 0, 5, 4, 1}, {4, 3, 0, 7, 5, 1}}};
constexpr std::array<std::uint8_t, 4> kBase{20, 40, 60, 128};
constexpr std::array<std::uint8_t, 4> kReplacement{45, 15, 5, 64};
// Independent truth table: the 7x5 image replaces [3,5)x[2,4) with these premultiplied bytes.
// The sole layer uses identity placement, nearest sampling, normal blend and unit opacity.
std::array<std::uint8_t, 4> Expected(unsigned x, unsigned y) noexcept
{
    return x >= 3 && x < 5 && y >= 2 && y < 4 ? kReplacement : kBase;
}
CanvasDocument MakeSourceForWork()
{
    std::vector<std::uint8_t> base(7 * 5 * 4);
    std::vector<std::uint8_t> replacement(2 * 2 * 4);
    for (std::size_t i = 0; i < base.size(); ++i)
        base[i] = kBase[i % 4];
    for (std::size_t i = 0; i < replacement.size(); ++i)
        replacement[i] = kReplacement[i % 4];
    const auto baseRaster = Raster::Make(7, 5, RasterFormat::Bgra8888, base.data(), base.size());
    const auto replacementRaster = Raster::Make(2, 2, RasterFormat::Bgra8888, replacement.data(), replacement.size());
    auto sparse =
        Raster::MakeSparse(7, 5, RasterFormat::Bgra8888, baseRaster, 0, 0, {RasterTile{3, 2, replacementRaster}}, 0);
    CanvasDocument document;
    document.id = Guid::Generate();
    document.width = 7;
    document.height = 5;
    auto layer = ImageLayer::Blank(L"Literal sparse utility source", {7, 5});
    layer.transform.sampling = LayerSampling::Nearest;
    layer.asset = RasterAsset{std::move(sparse), {}, L"Literal sparse utility source"};
    RecordRoot<ImageLayer>::Builder layers(1);
    if (!layers.Append(std::move(layer)))
        std::terminate();
    auto root = layers.Seal();
    if (!root)
        std::terminate();
    document.layers = std::move(root.value());
    return document;
}
struct Observation final
{
    JobTicket original;
    bool complete = false;
    bool parity = true;
    bool retiredOffOwner = false;
    unsigned regions = 0;
    unsigned copyBodies = 0;
    std::uint64_t acknowledgedOrdinal = 0;
    std::array<std::uint64_t, 3> recordedUploads{};
    JobResult terminal;
};
class Parent;
class FixturePump final : public OwnerWorkPump
{
  public:
    Parent* parent = nullptr;
    bool HasPending() const noexcept override;
    bool HasReady() const noexcept override;
    void Pump() noexcept override;
};
class Parent final : public Job, public GpuCommandSourceReceiver, public GpuReadbackReceiver, public GpuReadbackProducer
{
    enum class Stage : std::uint8_t
    {
        Open,
        Record,
        AwaitGpu,
        RetireStream,
        AwaitRows,
        Copy,
        RetireGraph
    };
    JobScheduler& _jobs;
    GpuWorkQueue& _queue;
    GpuCommandPump& _commands;
    FixturePump& _pump;
    Observation& _observation;
    const Scenario _scenario;
    ID3D11Device* _device;
    ID3D11DeviceContext* _immediate;
    const std::uint32_t _owner = GetCurrentThreadId();
    Stage _stage = Stage::Open;
    CanvasDocument _source;
    std::unique_ptr<RecordedUtilityGraph> _graph;
    GpuCommandStream _stream;
    GpuCommandSourceLease _attached;
    GpuCompletionContext _retiredCapture;
    GpuReadbackRows _rows;
    JobResult _terminal;
    bool _stopped = false;
    unsigned _round = 0;

    void Resume(Stage next, JobExecutionLane lane = JobExecutionLane::Compute) noexcept
    {
        _stage = next;
        if (!_jobs.Resume(_observation.original, lane))
            std::terminate();
    }
    void Fail(JobResult failure) noexcept
    {
        if (_terminal.status == JobStatus::Finished)
            _terminal = failure;
    }

  public:
    Parent(JobScheduler& jobs, GpuWorkQueue& queue, GpuCommandPump& commands, FixturePump& pump,
           Observation& observation, ID3D11Device* device, ID3D11DeviceContext* immediate, Scenario scenario) noexcept
        : _jobs(jobs), _queue(queue), _commands(commands), _pump(pump), _observation(observation), _scenario(scenario),
          _device(device), _immediate(immediate)
    {
    }

    JobResult Run(JobContext& context) override
    {
        if (_stage == Stage::Open)
        {
            _source = MakeSourceForWork();
            _graph = std::make_unique<RecordedUtilityGraph>(_owner);
        }
        else if (_stage == Stage::Record)
        {
            const auto region = _scenario == Scenario::InvalidRegion ? D3D11_BOX{0, 0, 0, 8, 5, 1} : kRegions[_round];
            const auto result = _round == 0
                                    ? _graph->PrepareForWork(_device, _source, region, _stream, context.cancellation)
                                    : _graph->RecordNextForWork(region, _stream, context.cancellation);
            _observation.recordedUploads[_round] = _graph->RecordedUploadBytesForWork();
            return FAILED(result) ? JobResult{JobStatus::Failed, result} : JobResult{};
        }
        else if (_stage == Stage::Copy)
        {
            ++_observation.copyBodies;
            if (!_rows.data)
                return {JobStatus::Failed, E_FAIL};
            const auto region = kRegions[_round];
            bool equal = _rows.width == region.right - region.left && _rows.height == region.bottom - region.top;
            for (unsigned y = 0; equal && y < _rows.height; ++y)
                for (unsigned x = 0; equal && x < _rows.width; ++x)
                {
                    const auto expected = Expected(region.left + x, region.top + y);
                    for (unsigned channel = 0; channel < 4; ++channel)
                        equal &= _rows.data[static_cast<std::size_t>(y) * _rows.rowPitch + x * 4 + channel] ==
                                 expected[channel];
                }
            _observation.parity &= equal;
            Expect(equal, "mapped-worker-pixels-match-independent-sparse-region-truth-table");
            return equal ? JobResult{} : JobResult{JobStatus::Failed, E_FAIL};
        }
        return {};
    }
    void RetireWorkerStage(JobContext&, JobResult result) noexcept override
    {
        if (_stage == Stage::Record)
            _stream.FinishProducerForWork(result.status == JobStatus::Finished    ? S_OK
                                          : result.status == JobStatus::Cancelled ? E_ABORT
                                                                                  : result.error);
        else if (_stage == Stage::RetireStream)
        {
            if (!_stream.RetireForWork())
                std::terminate();
            _retiredCapture = {};
        }
        else if (_stage == Stage::RetireGraph)
        {
            if (_graph && !_graph->RetireForWork() && !_graph->RetireUnpublishedForWork())
                std::terminate();
            _graph.reset();
            _source = {};
            _observation.retiredOffOwner = GetCurrentThreadId() != _owner;
        }
    }
    bool DeferRetirement(JobResult result, JobTicket ticket) noexcept override
    {
        Expect(ticket == _observation.original && _jobs.ReservedMemoryBytes() == kFixtureCharge,
               "every-stage-borrows-original-ticket-and-whole-fixture-reservation");
        if (result.status != JobStatus::Finished)
            Fail(result);
        switch (_stage)
        {
        case Stage::Open:
            if (_terminal.status != JobStatus::Finished)
            {
                Resume(Stage::RetireGraph);
                return true;
            }
            if (FAILED(_stream.InitializeOwner(_jobs, ticket, _queue.DeviceGeneration())) ||
                FAILED(_commands.AttachOwner(_stream, ticket, *this, _attached)))
                std::terminate();
            Resume(Stage::Record, JobExecutionLane::GpuRecording);
            return true;
        case Stage::Record:
            _stage = Stage::AwaitGpu;
            _jobs.NotifyOwnerWork();
            return true;
        case Stage::RetireStream:
            if (_terminal.status != JobStatus::Finished)
                Resume(Stage::RetireGraph);
            else
            {
                ID3D11Texture2D* unavailable = nullptr;
                D3D11_BOX unavailableBox{};
                Expect(_graph->BorrowRegionOwner(_device, _immediate, unavailable, unavailableBox) == E_ABORT &&
                           !unavailable && unavailableBox.right == 0,
                       "joined-recording-alone-cannot-borrow-unpublished-region");
                Expect(_graph->PublishRegionOwner(nullptr) == E_INVALIDARG,
                       "missing-original-immediate-refuses-without-consuming-region");
                Expect(_graph->PublishRegionOwner(_immediate) == S_OK,
                       "region-publication-follows-joined-recorder-and-all-original-command-acks");
                _stage = Stage::AwaitRows;
                const auto region = kRegions[_round];
                const auto queued = _queue.EnqueueProducedReadback(region.right - region.left,
                                                                   region.bottom - region.top, ticket, *this, *this);
                if (FAILED(queued))
                {
                    Fail({JobStatus::Failed, queued});
                    if (!_graph->ExcludeRegionOwner())
                        std::terminate();
                    Resume(Stage::RetireGraph);
                }
            }
            return true;
        case Stage::Copy:
            if (_rows.data && !_queue.ReleaseMapped(_rows.lease))
                std::terminate();
            _rows = {};
            if (!_graph->ExcludeRegionOwner())
                std::terminate();
            {
                ID3D11Texture2D* excluded = nullptr;
                D3D11_BOX excludedBox{};
                Expect(_graph->BorrowRegionOwner(_device, _immediate, excluded, excludedBox) == E_ABORT && !excluded &&
                           excludedBox.right == 0,
                       "excluded-region-refuses-new-borrow-before-worker-retirement");
            }
            ++_observation.regions;
            if (_terminal.status != JobStatus::Finished || ++_round == kRegions.size())
                Resume(Stage::RetireGraph);
            else
            {
                Expect(_stream.RestartOwner() == S_OK,
                       "next-region-restarts-only-after-original-copy-row-and-transport-acknowledgements");
                if (FAILED(_commands.AttachOwner(_stream, ticket, *this, _attached)))
                    std::terminate();
                Resume(Stage::Record, JobExecutionLane::GpuRecording);
            }
            return true;
        case Stage::RetireGraph:
            return false;
        default:
            std::terminate();
        }
    }
    void CommandAcknowledgedOwner(JobTicket ticket, GpuCommandLease command, HRESULT result) noexcept override
    {
        if (ticket != _observation.original || command.source != ticket ||
            command.ordinal <= _observation.acknowledgedOrdinal)
            std::terminate();
        _observation.acknowledgedOrdinal = command.ordinal;
        if (FAILED(result))
            Fail({JobStatus::Failed, result});
        _jobs.NotifyOwnerWork();
    }
    void CommandSourceFailedOwner(JobTicket ticket, HRESULT result) noexcept override
    {
        if (ticket != _observation.original || SUCCEEDED(result))
            std::terminate();
        Fail({JobStatus::Failed, result});
        _jobs.NotifyOwnerWork();
    }
    void ReadbackReady(JobTicket ticket, GpuReadbackRows rows, HRESULT result) noexcept override
    {
        if (ticket != _observation.original || _stage != Stage::AwaitRows)
            std::terminate();
        _rows = rows;
        if (FAILED(result))
            Fail({JobStatus::Failed, result});
        else if (_scenario == Scenario::CancelMapped && !_jobs.Cancel(ticket))
            std::terminate(); // Actual successful mapping remains borrowed until the cancelled stage joins.
        Resume(Stage::Copy);
    }
    HRESULT RecordReadbackSource(ID3D11Texture2D*& source, D3D11_BOX& box) noexcept override
    {
        // Actual owner staging callback borrows ready pixels; it performs no source traversal or composition.
        return _graph->BorrowRegionOwner(_device, _immediate, source, box);
    }
    bool Ready() const noexcept
    {
        return (_terminal.status != JobStatus::Finished && !_stopped &&
                (_stage == Stage::Record || _stage == Stage::AwaitGpu)) ||
               (_stage == Stage::AwaitGpu && _stream.CanRetireOwner());
    }
    void PumpOwner() noexcept
    {
        if (_terminal.status != JobStatus::Finished && !_stopped)
        {
            if (!_commands.StopSourceOwner(_attached))
                std::terminate();
            _stopped = true;
        }
        if (_stage != Stage::AwaitGpu || !_stream.CanRetireOwner())
            return;
        if (!_commands.DetachOwner(_attached, _retiredCapture) || !_stream.AuthorizeRetirementOwner())
            std::terminate();
        _attached = {};
        Resume(Stage::RetireStream);
    }
    void Completed(JobResult result) noexcept override
    {
        _observation.terminal = _terminal.status == JobStatus::Finished ? result : _terminal;
        _observation.complete = true;
        _pump.parent = nullptr;
    }
};
bool FixturePump::HasPending() const noexcept
{
    return parent != nullptr;
}
bool FixturePump::HasReady() const noexcept
{
    return parent && parent->Ready();
}
void FixturePump::Pump() noexcept
{
    if (parent)
        parent->PumpOwner();
}
} // namespace

int RunRecordedUtilityRegionDeviceTests(D3D_DRIVER_TYPE driver, Scenario scenario)
{
    deviceName = driver == D3D_DRIVER_TYPE_WARP ? "warp" : "hardware";
    wil::com_ptr_nothrow<ID3D11Device> device;
    wil::com_ptr_nothrow<ID3D11DeviceContext> immediate;
    constexpr auto level = D3D_FEATURE_LEVEL_11_0;
    const auto created = D3D11CreateDevice(nullptr, driver, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, &level, 1,
                                           D3D11_SDK_VERSION, device.put(), nullptr, immediate.put());
    if (driver == D3D_DRIVER_TYPE_HARDWARE && (created == DXGI_ERROR_UNSUPPORTED || created == DXGI_ERROR_NOT_FOUND))
    {
        std::printf("INFO recorded-utility-regions.hardware.device-unavailable-skipped hr=%ld\n",
                    static_cast<long>(created));
        return 0;
    }
    Expect(SUCCEEDED(created), "actual-device-created");
    if (FAILED(created))
        return 1;
    JobScheduler jobs;
    OwnerWorkMultiplexer routes;
    GpuWorkQueue queue;
    GpuCommandPump commands;
    FixturePump pump;
    if (FAILED(jobs.Initialize(1, {}, {Memory, nullptr})) || FAILED(jobs.EnableGpuRecordingLane()) ||
        !jobs.RegisterOwnerWork(routes) || FAILED(queue.Initialize(jobs, &routes)) ||
        FAILED(queue.Bind(device.get(), immediate.get())) || FAILED(commands.InitializeOwner(jobs, queue, routes)) ||
        !routes.Attach(pump))
        std::terminate();
    Observation observation;
    auto concrete =
        std::make_unique<Parent>(jobs, queue, commands, pump, observation, device.get(), immediate.get(), scenario);
    pump.parent = concrete.get();
    std::unique_ptr<Job> parent = std::move(concrete);
    if (FAILED(jobs.Submit(
            parent,
            {.kind = JobClass::Stroke, .serialKey = 72, .memoryBytes = kFixtureCharge, .deferredRetirement = true},
            observation.original)))
        std::terminate();
    const auto deadline = GetTickCount64() + 10000;
    while (!observation.complete)
    {
        (void)DrainCompletions(jobs);
        if (observation.complete)
            break;
        if (jobs.HasUiWork() || routes.HasReady())
            continue;
        const auto now = GetTickCount64();
        if (now >= deadline || WaitForSingleObject(static_cast<HANDLE>(jobs.WakeHandle()),
                                                   static_cast<DWORD>(deadline - now)) != WAIT_OBJECT_0)
            std::terminate();
    }
    if (scenario == Scenario::SerialRegions)
    {
        Expect(observation.terminal.status == JobStatus::Finished && observation.parity && observation.regions == 3,
               "three-disjoint-overlapping-and-edge-regions-return-exact-original-source-pixels");
        Expect(observation.acknowledgedOrdinal >= 3 && observation.copyBodies == 3,
               "three-original-command-epochs-and-mapped-copy-bodies-acknowledge");
        Expect(observation.recordedUploads[0] != 0 &&
                   observation.recordedUploads[1] == observation.recordedUploads[0] &&
                   observation.recordedUploads[2] == observation.recordedUploads[0],
               "serial-regions-reuse-private-source-residency-with-zero-repeat-raster-upload");
    }
    else if (scenario == Scenario::InvalidRegion)
        Expect(observation.terminal.status == JobStatus::Failed && observation.terminal.error == E_INVALIDARG &&
                   observation.regions == 0 && observation.acknowledgedOrdinal == 0 &&
                   observation.recordedUploads[0] == 0 && observation.copyBodies == 0,
               "invalid-region-retires-captured-source-without-command-or-pixel-publication");
    else
        Expect(observation.terminal.status == JobStatus::Cancelled && observation.regions == 1 &&
                   observation.acknowledgedOrdinal != 0 && observation.copyBodies == 0,
               "cancelled-mapped-stage-skips-useful-copy-and-acknowledges-original-row-lease");
    Expect(observation.retiredOffOwner && jobs.IsIdle() && jobs.ReservedMemoryBytes() == 0,
           "worker-resource-retirement-precedes-original-reservation-refund");
    if (!routes.Detach(pump))
        std::terminate();
    commands.StopOwner();
    queue.StopNotifications();
    jobs.Close();
    queue.CloseAfterWorkers();
    if (!commands.CloseOwner())
        std::terminate();
    return failures ? 1 : 0;
}

int RunRecordedUtilityRegionTests()
{
    passed = failures = 0;
    for (const auto scenario : {Scenario::SerialRegions, Scenario::InvalidRegion, Scenario::CancelMapped})
    {
        (void)RunRecordedUtilityRegionDeviceTests(D3D_DRIVER_TYPE_WARP, scenario);
        (void)RunRecordedUtilityRegionDeviceTests(D3D_DRIVER_TYPE_HARDWARE, scenario);
    }
    std::printf("SUMMARY recorded-utility-regions passed=%u failed=%u\n", passed, failures);
    return failures ? 1 : 0;
}
} // namespace RedPrism::CompositionTests
