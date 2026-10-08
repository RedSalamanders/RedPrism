#include "Composition.Tests.GpuCommandPump.h"

#include "GpuCommandPump.h"
#include "OwnerWorkMultiplexer.h"

#include <atomic>
#include <cstdio>
#include <memory>

namespace
{
int gFailures = 0;
void Expect(bool passed, const char* name) noexcept
{
    std::printf("%s recording-pump.%s\n", passed ? "PASS" : "FAIL", name);
    std::fflush(stdout);
    gFailures += passed ? 0 : 1;
}

struct State final : GpuCommandSourceReceiver
{
    enum class Stage
    {
        Open,
        Record,
        Retire
    } stage = Stage::Open;
    State(JobScheduler& jobs, GpuCommandPump& owner, ID3D11Device& source, std::uint64_t generation) noexcept
        : scheduler(jobs), pump(owner), device(source), deviceGeneration(generation)
    {
    }
    JobScheduler& scheduler;
    GpuCommandPump& pump;
    ID3D11Device& device;
    std::uint64_t deviceGeneration;
    GpuCommandStream stream;
    GpuCompletionContext detached; // Transfer at owner detach, destroy only in the joined worker retirement.
    GpuCommandSourceLease source;
    JobTicket original;
    unsigned int acknowledgements = 0;
    unsigned int failures = 0;
    HRESULT failure = S_OK;
    bool initialized = false;
    bool attached = false;
    bool recorderJoined = false;
    bool terminal = false;
    JobResult result;
    std::atomic<bool> retired{false};
    void CommandAcknowledgedOwner(JobTicket ticket, GpuCommandLease command, HRESULT status) noexcept override
    {
        Expect(ticket == original && command.source == original && command.deviceGeneration == deviceGeneration &&
                   command.ordinal == acknowledgements + 1 && SUCCEEDED(status),
               "real-callback-keeps-original-source-device-and-order");
        ++acknowledgements;
        GpuCompletionContext rejected;
        Expect(!pump.DetachOwner(source, rejected) && !rejected, "callback-cannot-detach-its-own-receiver");
    }
    void CommandSourceFailedOwner(JobTicket ticket, HRESULT status) noexcept override
    {
        Expect(ticket == original && FAILED(status), "failure-reports-original-ticket-without-acknowledgement");
        ++failures;
        failure = status;
    }
};

class Recorder final : public Job
{
  public:
    explicit Recorder(State& state) noexcept : _state(state) {}
    JobResult Run(JobContext& context) override
    {
        if (_state.stage != State::Stage::Record)
            return {};
        wil::com_ptr_nothrow<ID3D11DeviceContext> deferred;
        HRESULT result = _state.device.CreateDeferredContext(0, deferred.put());
        for (unsigned int index = 0; SUCCEEDED(result) && index < 3; ++index)
        {
            wil::com_ptr_nothrow<ID3D11CommandList> commands;
            result = deferred->FinishCommandList(FALSE, commands.put());
            if (SUCCEEDED(result))
                result = _state.stream.PublishForWork(std::move(commands), context.cancellation);
        }
        return FAILED(result) ? JobResult{JobStatus::Failed, result} : JobResult{};
    }
    void RetireWorkerStage(JobContext&, JobResult result) noexcept override
    {
        if (_state.stage == State::Stage::Record)
            _state.stream.FinishProducerForWork(result.status == JobStatus::Finished ? S_OK
                                                : result.status == JobStatus::Cancelled
                                                    ? E_ABORT
                                                    : static_cast<HRESULT>(result.error));
        else if (_state.stage == State::Stage::Retire)
        {
            _state.retired.store(_state.stream.RetireForWork(), std::memory_order_release);
            _state.detached = {};
        }
    }
    bool DeferRetirement(JobResult result, JobTicket ticket) noexcept override
    {
        if (_state.stage == State::Stage::Open)
        {
            _state.original = ticket;
            if (result.status != JobStatus::Finished)
                return false;
            const auto initialized = _state.stream.InitializeOwner(_state.scheduler, ticket, _state.deviceGeneration);
            _state.initialized = SUCCEEDED(initialized);
            const auto attached = _state.initialized
                                      ? _state.pump.AttachOwner(_state.stream, ticket, _state, _state.source)
                                      : initialized;
            _state.attached = SUCCEEDED(attached);
            Expect(_state.attached, "opening-ack-attaches-actual-admitted-source");
            if (!_state.attached)
                return false;
            _state.stage = State::Stage::Record;
            Expect(_state.scheduler.Resume(ticket), "recording-resumes-same-original-ticket");
            return true;
        }
        if (_state.stage == State::Stage::Record)
        {
            _state.recorderJoined = true;
            return true; // The test owner waits for original GPU use acknowledgements before retirement.
        }
        Expect(ticket == _state.original, "final-retirement-keeps-original-ticket");
        return false;
    }
    void Completed(JobResult result) noexcept override
    {
        _state.result = result;
        _state.terminal = true;
    }

  private:
    State& _state;
};

class Independent final : public Job
{
  public:
    explicit Independent(bool& done) noexcept : _done(done) {}
    JobResult Run(JobContext&) override
    {
        return {};
    }
    void Completed(JobResult result) noexcept override
    {
        _done = result.status == JobStatus::Finished;
    }

  private:
    bool& _done;
};

template <typename Predicate> bool Until(JobScheduler& scheduler, Predicate ready) noexcept
{
    const auto started = GetTickCount64();
    while (!ready())
    {
        (void)DrainCompletions(scheduler);
        if (ready())
            return true;
        const auto elapsed = GetTickCount64() - started;
        if (elapsed >= 10000 || WaitForSingleObject(static_cast<HANDLE>(scheduler.WakeHandle()),
                                                    static_cast<DWORD>(10000 - elapsed)) != WAIT_OBJECT_0)
            return false;
    }
    return true;
}

void Run(bool failFirst, bool failRecovery, bool foreignDevice = false)
{
    wil::com_ptr_nothrow<ID3D11Device> device;
    wil::com_ptr_nothrow<ID3D11DeviceContext> immediate;
    const D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
    const auto created = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                                           &level, 1, D3D11_SDK_VERSION, device.put(), nullptr, immediate.put());
    Expect(SUCCEEDED(created), "actual-warp-device");
    if (FAILED(created))
        return;
    wil::com_ptr_nothrow<ID3D11Device> recordedDevice;
    if (foreignDevice)
    {
        const HRESULT second =
            D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, &level, 1,
                              D3D11_SDK_VERSION, recordedDevice.put(), nullptr, nullptr);
        Expect(SUCCEEDED(second) && recordedDevice.get() != device.get(), "distinct-real-warp-recording-device");
        if (FAILED(second))
            return;
    }
    JobScheduler scheduler;
    const auto initialized = static_cast<HRESULT>(scheduler.Initialize(1));
    const auto enabled =
        SUCCEEDED(initialized) ? static_cast<HRESULT>(scheduler.EnableGpuRecordingLane()) : initialized;
    Expect(SUCCEEDED(enabled), "one-compute-worker-and-private-recorder");
    if (FAILED(enabled))
        return;
    OwnerWorkMultiplexer routes;
    GpuWorkQueue queue;
    GpuCommandPump pump;
    Expect(scheduler.RegisterOwnerWork(routes), "register-original-owner-router");
    const auto queueInitialized = queue.Initialize(scheduler, &routes);
    const auto bound = SUCCEEDED(queueInitialized) ? queue.Bind(device.get(), immediate.get()) : queueInitialized;
    Expect(SUCCEEDED(bound), "bind-original-real-completion-queue");
    if (FAILED(bound))
    {
        queue.StopNotifications();
        scheduler.Close();
        queue.CloseAfterWorkers();
        return;
    }
    const auto pumpInitialized = pump.InitializeOwner(scheduler, queue, routes);
    Expect(SUCCEEDED(pumpInitialized), "startup-register-fixed-command-pump");
    if (FAILED(pumpInitialized))
    {
        queue.StopNotifications();
        scheduler.Close();
        queue.CloseAfterWorkers();
        return;
    }
    State state{scheduler, pump, *(foreignDevice ? recordedDevice.get() : device.get()), queue.DeviceGeneration()};
    struct Cleanup final
    {
        State& state;
    } cleanup{state};
    const auto close = wil::scope_exit(
        [&]() noexcept
        {
            pump.StopOwner();
            queue.StopNotifications();
            scheduler.Close({[](void* opaque) noexcept
                             {
                                 auto& active = static_cast<Cleanup*>(opaque)->state;
                                 active.pump.AcknowledgeJoinedShutdownOwner();
                                 if (active.attached)
                                 {
                                     const bool detached = active.pump.DetachOwner(active.source, active.detached);
                                     Expect(detached, "shutdown-detaches-only-after-joined-producers");
                                     active.attached = !detached;
                                     Expect(active.stream.AuthorizeRetirementOwner(),
                                            "shutdown-authorizes-destruction-only");
                                 }
                             },
                             [](void* opaque) noexcept
                             {
                                 auto& active = static_cast<Cleanup*>(opaque)->state;
                                 if (active.initialized && !active.retired.load(std::memory_order_acquire))
                                     active.retired.store(active.stream.RetireForWork(), std::memory_order_release);
                                 active.detached = {};
                             },
                             &cleanup});
            queue.CloseAfterWorkers();
            Expect(pump.CloseOwner(), "close-detaches-stable-owner-route");
            Expect(scheduler.ReservedMemoryBytes() == 0 && state.retired.load(std::memory_order_acquire),
                   "joined-worker-retirement-returns-original-charge");
        });
    pump.SetCompletionRefusalsForTest(failFirst ? E_FAIL : S_OK, failRecovery ? E_FAIL : S_OK);
    std::unique_ptr<Job> recorder = std::make_unique<Recorder>(state);
    JobTicket original{};
    const auto admitted = static_cast<HRESULT>(scheduler.Submit(recorder,
                                                                {.kind = JobClass::Background,
                                                                 .memoryBytes = 65536,
                                                                 .deferredRetirement = true,
                                                                 .executionLane = JobExecutionLane::GpuRecording},
                                                                original));
    Expect(SUCCEEDED(admitted) && !recorder, "whole-parent-admitted-before-opening");
    if (FAILED(admitted))
        return;
    // The registered owner router executes batches and drains their real original completion events.
    const bool fails = failFirst || foreignDevice;
    const bool joined =
        Until(scheduler,
              [&]() { return state.recorderJoined && (fails ? state.failures == 1 : state.acknowledgements == 3); });
    Expect(joined && original == state.original, "actual-recording-and-gpu-protocol-reaches-owner");
    Expect(scheduler.ReservedMemoryBytes() == 65536 && !state.terminal,
           "original-charge-held-through-producer-and-gpu-stages");
    bool independent = false;
    std::unique_ptr<Job> cpu = std::make_unique<Independent>(independent);
    JobTicket cpuTicket{};
    const auto cpuAdmitted = static_cast<HRESULT>(scheduler.Submit(cpu, {.memoryBytes = 1024}, cpuTicket));
    Expect(SUCCEEDED(cpuAdmitted) && Until(scheduler, [&]() { return independent; }),
           "ordinary-compute-remains-independent-of-suspended-source");
    if (failRecovery)
    {
        (void)DrainCompletions(scheduler);
        Expect(state.acknowledgements == 0 && state.failures == 1 && !state.stream.CanRetireOwner(),
               "failed-signal-and-recovery-never-acknowledge-executed-command");
        Expect(!pump.HasReady() && pump.HasPending() && scheduler.ReservedMemoryBytes() == 65536,
               "healthy-quarantine-is-dormant-and-fully-charged");
        return; // The joined shutdown cleanup above resolves ownership without fabricating a useful callback.
    }
    Expect(Until(scheduler, [&]() { return state.stream.CanRetireOwner(); }),
           "all-submitted-uses-resolve-before-retirement");
    Expect(state.acknowledgements == (foreignDevice ? 0u
                                      : failFirst   ? 1u
                                                    : 3u),
           "exact-original-acknowledgement-count");
    if (foreignDevice)
        Expect(state.failure == E_INVALIDARG && state.failures == 1 && state.acknowledgements == 0,
               "same-numeric-generation-never-authorizes-another-actual-device");
    GpuCommandSourceLease foreign = state.source;
    ++foreign.generation;
    GpuCompletionContext rejected;
    Expect(!pump.DetachOwner(foreign, rejected) && !rejected, "foreign-source-lease-cannot-detach");
    const bool detached = pump.DetachOwner(state.source, state.detached);
    Expect(detached && state.detached, "transfer-all-original-context-owners-to-worker-retirement");
    state.attached = !detached;
    const bool authorized = detached && state.stream.AuthorizeRetirementOwner();
    Expect(authorized, "authorize-after-actual-recording-and-gpu-acknowledgements");
    state.stage = State::Stage::Retire;
    const bool resumed = authorized && scheduler.Resume(original);
    Expect(resumed && Until(scheduler, [&]() { return state.terminal; }) &&
               state.result.status == (fails ? JobStatus::Cancelled : JobStatus::Finished) &&
               state.retired.load(std::memory_order_acquire),
           "same-record-returns-only-after-worker-retirement");
    Expect(!state.detached && scheduler.ReservedMemoryBytes() == 0 && scheduler.IsIdle(),
           "last-context-reference-and-charge-retire-together");
}
} // namespace

int RunGpuCommandPumpTests()
{
    gFailures = 0;
    Run(false, false);
    Run(true, false);
    Run(true, true);
    Run(false, false, true);
    return gFailures;
}
