#include "Composition.Tests.CommandStreamRestart.h"

#include "GpuCommandPump.h"
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
unsigned passed = 0;
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
    unsigned joinedRecorders = 0;
    unsigned retiredStreams = 0;
    unsigned gpuAcknowledgements = 0;
    std::array<GpuCommandLease, 3> commands{};
    bool finished = false;
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
class Parent final : public Job, public GpuCommandSourceReceiver
{
    enum class Stage : unsigned char
    {
        Open,
        Record,
        AwaitGpu,
        Retire
    };
    JobScheduler& jobs;
    GpuWorkQueue& queue;
    GpuCommandPump& commands;
    RestartPump& pump;
    Observation& observation;
    ID3D11Device& device;
    const DWORD ownerThread = GetCurrentThreadId();
    GpuCommandStream stream;
    GpuCommandSourceLease source;
    GpuCompletionContext retiredCapture;
    Stage stage = Stage::Open;
    unsigned round = 0;

    void Resume(Stage next, JobExecutionLane lane = JobExecutionLane::Compute) noexcept
    {
        stage = next;
        if (!jobs.Resume(observation.original, lane))
            std::terminate();
    }

  public:
    Parent(JobScheduler& j, GpuWorkQueue& q, GpuCommandPump& c, RestartPump& p, Observation& o,
           ID3D11Device& d) noexcept
        : jobs(j), queue(q), commands(c), pump(p), observation(o), device(d)
    {
    }
    JobResult Run(JobContext&) override
    {
        if (stage != Stage::Record)
            return {};
        wil::com_ptr_nothrow<ID3D11DeviceContext> deferred;
        wil::com_ptr_nothrow<ID3D11CommandList> command;
        HRESULT status = device.CreateDeferredContext(0, deferred.put());
        if (SUCCEEDED(status))
            status = deferred->FinishCommandList(FALSE, command.put());
        if (SUCCEEDED(status))
            status = stream.PublishForWork(std::move(command));
        return FAILED(status) ? JobResult{JobStatus::Failed, status} : JobResult{};
    }
    void RetireWorkerStage(JobContext&, JobResult result) noexcept override
    {
        if (stage == Stage::Record)
            stream.FinishProducerForWork(result.status == JobStatus::Finished ? S_OK : E_FAIL);
        if (stage == Stage::Retire)
        {
            observation.allRetirementsOffOwner &= GetCurrentThreadId() != ownerThread;
            if (!stream.RetireForWork())
                std::terminate();
            retiredCapture = {};
        }
    }
    bool DeferRetirement(JobResult result, JobTicket ticket) noexcept override
    {
        if (ticket != observation.original || result.status != JobStatus::Finished)
            std::terminate();
        switch (stage)
        {
        case Stage::Open:
            Expect(IsInvalidArgument(stream.RestartOwner()), "uninitialized-stream-refuses-restart");
            if (FAILED(stream.InitializeOwner(jobs, ticket, queue.DeviceGeneration())) ||
                FAILED(commands.AttachOwner(stream, ticket, *this, source)))
                std::terminate();
            Expect(IsInvalidArgument(stream.RestartOwner()), "unretired-producer-refuses-restart-in-exact-callback");
            Resume(Stage::Record, JobExecutionLane::GpuRecording);
            return true;
        case Stage::Record:
            ++observation.joinedRecorders;
            Expect(IsInvalidArgument(stream.RestartOwner()),
                   "joined-recording-without-worker-retirement-refuses-restart");
            stage = Stage::AwaitGpu;
            jobs.NotifyOwnerWork();
            return true;
        case Stage::Retire:
        {
            ++observation.retiredStreams;
            if (++round == 3)
            {
                stream.RequestStopOwner();
                Expect(IsInvalidArgument(stream.RestartOwner()), "stopped-stream-cannot-restart-even-after-join");
                return false;
            }
            HRESULT foreignResult = S_OK;
            std::thread foreign([&]() noexcept { foreignResult = stream.RestartOwner(); });
            foreign.join();
            Expect(IsInvalidArgument(foreignResult), "foreign-thread-cannot-restart-joined-stream");
            Expect(SUCCEEDED(stream.RestartOwner()), "exact-original-joined-callback-restarts-existing-stream");
            Expect(stream.BelongsToOwner(jobs, ticket, queue.DeviceGeneration()),
                   "restart-retains-original-ticket-and-device");
            if (FAILED(commands.AttachOwner(stream, ticket, *this, source)))
                std::terminate();
            Resume(Stage::Record, JobExecutionLane::GpuRecording);
            return true;
        }
        default:
            std::terminate();
        }
    }
    void Completed(JobResult result) noexcept override
    {
        Expect(IsInvalidArgument(stream.RestartOwner()), "terminal-callback-has-no-restart-authority");
        observation.terminal = result;
        observation.finished = true;
        pump.parent = nullptr;
    }
    void CommandAcknowledgedOwner(JobTicket ticket, GpuCommandLease command, HRESULT result) noexcept override
    {
        if (ticket != observation.original || command.source != ticket || FAILED(result) || round >= 3)
            std::terminate();
        observation.commands[round] = command;
        ++observation.gpuAcknowledgements;
        jobs.NotifyOwnerWork();
    }
    void CommandSourceFailedOwner(JobTicket, HRESULT) noexcept override
    {
        std::terminate();
    }
    bool Ready() const noexcept
    {
        return stage == Stage::AwaitGpu && stream.CanRetireOwner();
    }
    void PumpOwner() noexcept
    {
        if (!Ready())
            return;
        Expect(IsInvalidArgument(stream.RestartOwner()), "owner-pump-without-original-joined-callback-refuses-restart");
        if (round == 2)
        {
            const auto old = observation.commands[0];
            const auto current = observation.commands[2];
            Expect(old.ordinal == 1 && current.ordinal == 3 && old.slot == current.slot,
                   "reused-physical-slot-has-monotonic-original-command-ordinal");
            Expect(!stream.AcknowledgeOwner(old) && !stream.BeginExecuteOwner(old),
                   "old-command-lease-cannot-acknowledge-or-execute-reused-slot");
        }
        if (!commands.DetachOwner(source, retiredCapture) || !stream.AuthorizeRetirementOwner())
            std::terminate();
        source = {};
        Resume(Stage::Retire);
    }
};
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
    passed = 0;
    wil::com_ptr_nothrow<ID3D11Device> device;
    wil::com_ptr_nothrow<ID3D11DeviceContext> immediate;
    constexpr auto kLevel = D3D_FEATURE_LEVEL_11_0;
    const HRESULT deviceStatus =
        D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, &kLevel, 1,
                          D3D11_SDK_VERSION, device.put(), nullptr, immediate.put());
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
    if (FAILED(jobs.Initialize(1, {}, {Memory, nullptr})) || FAILED(jobs.EnableGpuRecordingLane()) ||
        !jobs.RegisterOwnerWork(routes) || FAILED(queue.Initialize(jobs, &routes)) ||
        FAILED(queue.Bind(device.get(), immediate.get())) || FAILED(commands.InitializeOwner(jobs, queue, routes)) ||
        !routes.Attach(pump))
        std::terminate();
    Observation observation;
    auto concrete = std::make_unique<Parent>(jobs, queue, commands, pump, observation, *device.get());
    pump.parent = concrete.get();
    std::unique_ptr<Job> parent = std::move(concrete);
    if (FAILED(jobs.Submit(
            parent, {.kind = JobClass::Stroke, .serialKey = 71, .memoryBytes = 65536, .deferredRetirement = true},
            observation.original)))
        std::terminate();
    const auto deadline = GetTickCount64() + 10000;
    while (!observation.finished)
    {
        (void)DrainCompletions(jobs);
        if (observation.finished)
            break;
        if (jobs.HasUiWork() || routes.HasReady())
            continue;
        const auto now = GetTickCount64();
        if (now >= deadline || WaitForSingleObject(static_cast<HANDLE>(jobs.WakeHandle()),
                                                   static_cast<DWORD>(deadline - now)) != WAIT_OBJECT_0)
            std::terminate();
    }
    Expect(observation.terminal.status == JobStatus::Finished && observation.joinedRecorders == 3 &&
               observation.retiredStreams == 3 && observation.gpuAcknowledgements == 3,
           "three-samples-share-one-original-record-and-three-real-GPU-acknowledgements");
    Expect(observation.allRetirementsOffOwner && jobs.IsIdle() && jobs.ReservedMemoryBytes() == 0,
           "every-transport-retirement-is-off-owner-before-final-reservation-refund");
    if (!routes.Detach(pump))
        std::terminate();
    commands.StopOwner();
    queue.StopNotifications();
    jobs.Close();
    queue.CloseAfterWorkers();
    if (!commands.CloseOwner())
        std::terminate();
    std::printf("SUMMARY command-stream-restart passed=%u failed=%u\n", passed, failures);
    return failures ? 1 : 0;
}
} // namespace RedPrism::CompositionTests
