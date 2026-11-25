#include "Composition.Tests.CommandStreamRestart.h"

#include "GpuCommandPump.h"
#include "GpuRecordingCommands.h"
#include "OwnerWorkMultiplexer.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <memory>
#include <thread>

namespace RedPrism::CompositionTests
{
namespace
{
unsigned failures = 0;
unsigned passed   = 0;
void Expect(bool condition, const char* name) noexcept
{
    std::printf("%s command-stream-restart.%s\n", condition ? "PASS" : "FAIL", name);
    failures += condition ? 0u : 1u;
    passed += condition ? 1u : 0u;
}
bool IsInvalidArgument(HRESULT status) noexcept
{
    return static_cast<std::int32_t>(status) == static_cast<std::int32_t>(E_INVALIDARG);
}
bool Memory(void*, MemorySnapshot& snapshot) noexcept
{
    snapshot = {4ull * 1024 * 1024 * 1024, 4ull * 1024 * 1024 * 1024};
    return true;
}
struct Observation final
{
    JobTicket original;
    unsigned joinedRecorders     = 0;
    unsigned retiredStreams      = 0;
    unsigned gpuAcknowledgements = 0;
    std::array<GpuCommandLease, 3> commands{};
    bool finished               = false;
    bool allRetirementsOffOwner = true;
    JobResult terminal;
};
class Parent;
class RestartPump final : public OwnerWorkPump
{
public:
    Parent* parent = nullptr;
    bool HasPending() const noexcept override;
    bool HasReady() const noexcept override;
    void Pump() noexcept override;
};
// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027)
class Parent final : public Job, public GpuCommandSourceReceiver
{
    enum class Stage : unsigned char
    {
        Open,
        Record,
        AwaitGpu,
        Retire
    };
    JobScheduler& _jobs;
    GpuWorkQueue& _queue;
    GpuCommandPump& _commands;
    RestartPump& _pump;
    Observation& _observation;
    ID3D11Device& _device;
    const DWORD _ownerThread = GetCurrentThreadId();
    GpuCommandStream _stream;
    GpuCommandSourceLease _source;
    GpuCompletionContext _retiredCapture;
    Stage _stage    = Stage::Open;
    unsigned _round = 0;
    wil::com_ptr_nothrow<ID3D11DeviceContext> _deferred;
    std::unique_ptr<GpuRecordingCommands> _recording;
    bool _producerFinished       = false;
    std::uint32_t _firstProducer = 0;

    void Resume(Stage next, JobExecutionLane lane = JobExecutionLane::Compute) noexcept
    {
        _stage = next;
        if (! _jobs.Resume(_observation.original, lane))
            std::terminate();
    }

public:
    Parent(JobScheduler& j, GpuWorkQueue& q, GpuCommandPump& c, RestartPump& p, Observation& o, ID3D11Device& d) noexcept
        : _jobs(j),
          _queue(q),
          _commands(c),
          _pump(p),
          _observation(o),
          _device(d)
    {
    }
    JobResult Run(JobContext& context) override
    {
        if (_stage != Stage::Record)
            return {};
        _producerFinished  = false;
        HRESULT result     = E_FAIL;
        const auto produce = [&]() noexcept
        {
            const auto finish = wil::scope_exit([&]() noexcept
            {
                _stream.FinishProducerForWork(result);
                _producerFinished = true;
            });
            if (_round == 0)
            {
                _firstProducer = GetCurrentThreadId();
                result         = _device.CreateDeferredContext(0, _deferred.put());
                if (SUCCEEDED(result))
                    result = _recording->InitializeForWork(_deferred.get(), _stream, context.cancellation);
                Expect(SUCCEEDED(result) && _stream.ProductionEpochForWork() == 1, "private-policy-initializes-on-original-first-epoch");
            }
            else
            {
                Expect(static_cast<std::int32_t>(_recording->StatusForWork()) == static_cast<std::int32_t>(HRESULT_FROM_WIN32(ERROR_INVALID_THREAD_ID)),
                       "old-private-policy-cannot-record-before-fresh-epoch-rebind");
                Expect(_round != 1 || GetCurrentThreadId() != _firstProducer, "second-production-stage-uses-a-different-actual-thread");
                std::stop_source cancelled;
                (void)cancelled.request_stop();
                Expect(_recording->RestartForWork(_deferred.get(), _stream, CancelToken(cancelled.get_token())) == E_ABORT,
                       "cancelled-rebind-refuses-without-consuming-fresh-epoch");
                GpuCommandStream foreign;
                Expect(IsInvalidArgument(_recording->RestartForWork(_deferred.get(), foreign, {})), "foreign-original-stream-cannot-rebind-private-policy");
                Expect(IsInvalidArgument(_recording->RestartForWork(nullptr, _stream, {})), "foreign-context-cannot-rebind-private-policy");
                result = _recording->RestartForWork(_deferred.get(), _stream, context.cancellation);
                Expect(SUCCEEDED(result) && _stream.ProductionEpochForWork() == _round + 1, "joined-original-restart-rebinds-existing-private-residency");
            }
            if (FAILED(result))
                return;
            Expect(static_cast<std::int32_t>(_recording->RestartForWork(_deferred.get(), _stream, {})) ==
                       static_cast<std::int32_t>(HRESULT_FROM_WIN32(ERROR_INVALID_STATE)),
                   "same-production-epoch-cannot-rebind-private-policy-twice");
            Expect(_recording->ContextStatusForWork(&_device, _deferred.get()) == S_OK, "rebound-policy-keeps-exact-private-device-and-context");
            _recording->CheckpointForWork();
            result = _recording->StatusForWork();
        };
        if (! _recording)
            _recording = std::make_unique<GpuRecordingCommands>();
        if (_round == 1)
        {
            Expect(static_cast<std::int32_t>(_recording->StatusForWork()) == static_cast<std::int32_t>(HRESULT_FROM_WIN32(ERROR_INVALID_STATE)),
                   "previous-producer-cannot-use-policy-after-original-epoch-advances");
            std::thread nextProducer(produce);
            nextProducer.join();
        }
        else
            produce();
        return FAILED(result) ? JobResult{JobStatus::Failed, result} : JobResult{};
    }
    void RetireWorkerStage(JobContext&, JobResult result) noexcept override
    {
        if (_stage == Stage::Record && ! _producerFinished)
            _stream.FinishProducerForWork(result.status == JobStatus::Finished ? S_OK : E_FAIL);
        if (_stage == Stage::Retire)
        {
            _observation.allRetirementsOffOwner &= GetCurrentThreadId() != _ownerThread;
            if (! _stream.RetireForWork())
                std::terminate();
            _retiredCapture = {};
            if (_round == 2)
            {
                _recording.reset();
                _deferred.reset();
            }
        }
    }
    bool DeferRetirement(JobResult result, JobTicket ticket) noexcept override
    {
        if (ticket != _observation.original || result.status != JobStatus::Finished)
            std::terminate();
        switch (_stage)
        {
            case Stage::Open:
                Expect(IsInvalidArgument(_stream.RestartOwner()), "uninitialized-stream-refuses-restart");
                if (FAILED(_stream.InitializeOwner(_jobs, ticket, _queue.DeviceGeneration())) || FAILED(_commands.AttachOwner(_stream, ticket, *this, _source)))
                    std::terminate();
                Expect(IsInvalidArgument(_stream.RestartOwner()), "unretired-producer-refuses-restart-in-exact-callback");
                Resume(Stage::Record, JobExecutionLane::GpuRecording);
                return true;
            case Stage::Record:
                ++_observation.joinedRecorders;
                Expect(IsInvalidArgument(_stream.RestartOwner()), "joined-recording-without-worker-retirement-refuses-restart");
                _stage = Stage::AwaitGpu;
                _jobs.NotifyOwnerWork();
                return true;
            case Stage::Retire:
            {
                ++_observation.retiredStreams;
                if (++_round == 3)
                {
                    _stream.RequestStopOwner();
                    Expect(IsInvalidArgument(_stream.RestartOwner()), "stopped-stream-cannot-restart-even-after-join");
                    return false;
                }
                HRESULT foreignResult = S_OK;
                std::thread foreign([&]() noexcept { foreignResult = _stream.RestartOwner(); });
                foreign.join();
                Expect(IsInvalidArgument(foreignResult), "foreign-thread-cannot-restart-joined-stream");
                Expect(SUCCEEDED(_stream.RestartOwner()), "exact-original-joined-callback-restarts-existing-stream");
                Expect(_stream.BelongsToOwner(_jobs, ticket, _queue.DeviceGeneration()), "restart-retains-original-ticket-and-device");
                if (FAILED(_commands.AttachOwner(_stream, ticket, *this, _source)))
                    std::terminate();
                Resume(Stage::Record, JobExecutionLane::GpuRecording);
                return true;
            }
            case ::RedPrism::CompositionTests::Parent::Stage::AwaitGpu:
            default: std::terminate();
        }
    }
    void Completed(JobResult result) noexcept override
    {
        Expect(IsInvalidArgument(_stream.RestartOwner()), "terminal-callback-has-no-restart-authority");
        _observation.terminal = result;
        _observation.finished = true;
        _pump.parent          = nullptr;
    }
    void CommandAcknowledgedOwner(JobTicket ticket, GpuCommandLease command, HRESULT result) noexcept override
    {
        if (ticket != _observation.original || command.source != ticket || FAILED(result) || _round >= 3)
            std::terminate();
        _observation.commands[_round] = command;
        ++_observation.gpuAcknowledgements;
        _jobs.NotifyOwnerWork();
    }
    void CommandSourceFailedOwner(JobTicket, HRESULT) noexcept override
    {
        std::terminate();
    }
    bool Ready() const noexcept
    {
        return _stage == Stage::AwaitGpu && _stream.CanRetireOwner();
    }
    void PumpOwner() noexcept
    {
        if (! Ready())
            return;
        Expect(IsInvalidArgument(_stream.RestartOwner()), "owner-pump-without-original-joined-callback-refuses-restart");
        if (_round == 2)
        {
            const auto old     = _observation.commands[0];
            const auto current = _observation.commands[2];
            Expect(old.ordinal == 1 && current.ordinal == 3 && old.slot == current.slot, "reused-physical-slot-has-monotonic-original-command-ordinal");
            Expect(! _stream.AcknowledgeOwner(old) && ! _stream.BeginExecuteOwner(old), "old-command-lease-cannot-acknowledge-or-execute-reused-slot");
        }
        if (! _commands.DetachOwner(_source, _retiredCapture) || ! _stream.AuthorizeRetirementOwner())
            std::terminate();
        _source = {};
        Resume(Stage::Retire);
    }
};
#pragma warning(pop)

bool RestartPump::HasPending() const noexcept
{
    return parent != nullptr;
}
bool RestartPump::HasReady() const noexcept
{
    return parent && parent->Ready();
}
void RestartPump::Pump() noexcept
{
    if (parent)
        parent->PumpOwner();
}
} // namespace

int RunCommandStreamRestartTests()
{
    failures = 0;
    passed   = 0;
    wil::com_ptr_nothrow<ID3D11Device> device;
    wil::com_ptr_nothrow<ID3D11DeviceContext> immediate;
    constexpr auto kLevel      = D3D_FEATURE_LEVEL_11_0;
    const HRESULT deviceStatus = D3D11CreateDevice(
        nullptr, D3D_DRIVER_TYPE_WARP, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, &kLevel, 1, D3D11_SDK_VERSION, device.put(), nullptr, immediate.put());
    Expect(SUCCEEDED(deviceStatus), "warp-device-created");
    if (FAILED(deviceStatus))
    {
        std::printf("SUMMARY command-stream-restart passed=%u failed=%u\n", passed, failures);
        return 1;
    }
    JobScheduler jobs;
    OwnerWorkMultiplexer routes;
    GpuWorkQueue queue;
    GpuCommandPump commands;
    RestartPump pump;
    if (FAILED(jobs.Initialize(1, {}, {Memory, nullptr})) || FAILED(jobs.EnableGpuRecordingLane()) || ! jobs.RegisterOwnerWork(routes) ||
        FAILED(queue.Initialize(jobs, &routes)) || FAILED(queue.Bind(device.get(), immediate.get())) || FAILED(commands.InitializeOwner(jobs, queue, routes)) ||
        ! routes.Attach(pump))
        std::terminate();
    Observation observation;
    auto concrete               = std::make_unique<Parent>(jobs, queue, commands, pump, observation, *device.get());
    pump.parent                 = concrete.get();
    std::unique_ptr<Job> parent = std::move(concrete);
    if (FAILED(jobs.Submit(parent, {.kind = JobClass::Stroke, .serialKey = 71, .memoryBytes = 65536, .deferredRetirement = true}, observation.original)))
        std::terminate();
    const auto deadline = GetTickCount64() + 10000;
    while (! observation.finished)
    {
        (void)DrainCompletions(jobs);
        if (observation.finished)
            break;
        if (jobs.HasUiWork() || routes.HasReady())
            continue;
        const auto now = GetTickCount64();
        if (now >= deadline || WaitForSingleObject(static_cast<HANDLE>(jobs.WakeHandle()), static_cast<DWORD>(deadline - now)) != WAIT_OBJECT_0)
            std::terminate();
    }
    Expect(observation.terminal.status == JobStatus::Finished && observation.joinedRecorders == 3 && observation.retiredStreams == 3 &&
               observation.gpuAcknowledgements == 3,
           "three-samples-share-one-original-record-and-three-real-GPU-acknowledgements");
    Expect(observation.allRetirementsOffOwner && jobs.IsIdle() && jobs.ReservedMemoryBytes() == 0,
           "every-transport-retirement-is-off-owner-before-final-reservation-refund");
    if (! routes.Detach(pump))
        std::terminate();
    commands.StopOwner();
    queue.StopNotifications();
    jobs.Close();
    queue.CloseAfterWorkers();
    if (! commands.CloseOwner())
        std::terminate();
    std::printf("SUMMARY command-stream-restart passed=%u failed=%u\n", passed, failures);
    return failures ? 1 : 0;
}
} // namespace RedPrism::CompositionTests
