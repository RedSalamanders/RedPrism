#include "Composition.Tests.GpuCommandPump.h"

#include "GpuCommandPump.h"
#include "GpuRecordingCommands.h"
#include "OwnerWorkMultiplexer.h"

#include <atomic>
#include <cstdio>
#include <memory>
#include <thread>

namespace
{
int gFailures = 0;
bool recoveryCase = false;
bool joinedSourceCase = false;
void Expect(bool passed, const char* name) noexcept
{
    std::printf("%s %s.%s\n", passed ? "PASS" : "FAIL",
                joinedSourceCase ? "recording-pump-joined-source"
                : recoveryCase   ? "recording-pump-recovery"
                                 : "recording-pump",
                name);
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
    const DWORD ownerThread = GetCurrentThreadId();
    std::uint64_t deviceGeneration;
    GpuCommandStream stream;
    GpuCompletionContext detached; // Transfer at owner detach, destroy only in the joined worker retirement.
    GpuCommandSourceLease source;
    JobTicket original;
    unsigned int acknowledgements = 0;
    unsigned int failures = 0;
    HRESULT failure = S_OK;
    GpuCommandFailureDisposition disposition = GpuCommandFailureDisposition::CancelOriginal;
    bool retireBodyEntered = false;
    bool retirementTokenCancelled = false;
    bool policyInitialized = false;
    bool policyStopped = false;
    bool recordingTokenLive = false;
    bool initialized = false;
    bool attached = false;
    bool recorderJoined = false;
    bool terminal = false;
    bool testJoinedSource = false;
    bool exactJoinedSource = false;
    bool wrapperDestroyed = false;
    bool joinedHookReturned = false;
    JobResult result;
    std::atomic<bool> retired{false};
    void CommandAcknowledgedOwner(JobTicket ticket, GpuCommandLease command, HRESULT status) noexcept override
    {
        Expect(ticket == original && command.source == original && command.deviceGeneration == deviceGeneration &&
                   command.ordinal == acknowledgements + 1 && SUCCEEDED(status),
               "real-callback-keeps-original-source-device-and-order");
        ++acknowledgements;
        if (disposition == GpuCommandFailureDisposition::RetainOriginalForRecovery)
            Expect(!pump.HasSourceSlotOwner(), "source-capacity-query-refuses-inside-original-callback");
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
    ~Recorder() override
    {
        if (_state.testJoinedSource)
        {
            Expect(_state.joinedHookReturned && _state.exactJoinedSource && !_state.attached && _state.detached &&
                       !_state.pump.HasPending() && !_state.terminal,
                   "original-source-detaches-before-actual-wrapper-destruction-without-useful-completion");
            _state.wrapperDestroyed = true;
        }
    }
    JobResult Run(JobContext& context) override
    {
        if (_state.stage != State::Stage::Record)
        {
            if (_state.stage == State::Stage::Retire &&
                _state.disposition == GpuCommandFailureDisposition::RetainOriginalForRecovery)
            {
                _state.retireBodyEntered = true;
                // The original failure remains a failure: live token alone grants no device recovery/pixels.
                return {JobStatus::Failed, _state.failure};
            }
            return {};
        }
        wil::com_ptr_nothrow<ID3D11DeviceContext> deferred;
        HRESULT result = _state.device.CreateDeferredContext(0, deferred.put());
        GpuRecordingCommands policy;
        if (SUCCEEDED(result) && _state.disposition == GpuCommandFailureDisposition::RetainOriginalForRecovery)
        {
            result = policy.InitializeForWork(deferred.get(), _state.stream, context.cancellation);
            _state.policyInitialized = SUCCEEDED(result);
        }
        for (unsigned int index = 0; SUCCEEDED(result) && index < 3; ++index)
        {
            wil::com_ptr_nothrow<ID3D11CommandList> commands;
            result = deferred->FinishCommandList(FALSE, commands.put());
            if (SUCCEEDED(result))
                result = _state.stream.PublishForWork(std::move(commands), context.cancellation);
        }
        if (_state.disposition == GpuCommandFailureDisposition::RetainOriginalForRecovery)
        {
            _state.policyStopped = FAILED(result) && policy.StatusForWork() == E_ABORT;
            _state.recordingTokenLive = !context.cancellation.IsCancelled();
        }
        return FAILED(result) ? JobResult{JobStatus::Failed, result} : JobResult{};
    }
    void RetireWorkerStage(JobContext& context, JobResult result) noexcept override
    {
        if (_state.stage == State::Stage::Record)
            _state.stream.FinishProducerForWork(result.status == JobStatus::Finished ? S_OK
                                                : result.status == JobStatus::Cancelled
                                                    ? E_ABORT
                                                    : static_cast<HRESULT>(result.error));
        else if (_state.stage == State::Stage::Retire)
        {
            _state.retirementTokenCancelled = context.cancellation.IsCancelled();
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
            if (_state.initialized && _state.disposition == GpuCommandFailureDisposition::RetainOriginalForRecovery)
            {
                GpuCommandSourceLease rejected;
                Expect(_state.pump.HasSourceSlotOwner(), "source-capacity-available-before-original-attachment");
                Expect(_state.pump.AttachOwner(_state.stream, ticket, _state, rejected,
                                               static_cast<GpuCommandFailureDisposition>(255)) == E_INVALIDARG &&
                           !rejected.generation && _state.pump.HasSourceSlotOwner(),
                       "unknown-failure-disposition-refuses-without-consuming-source-capacity");
            }
            const auto attached = _state.initialized ? _state.pump.AttachOwner(_state.stream, ticket, _state,
                                                                               _state.source, _state.disposition)
                                                     : initialized;
            _state.attached = SUCCEEDED(attached);
            Expect(_state.attached, "opening-ack-attaches-actual-admitted-source");
            if (!_state.attached)
                return false;
            if (_state.testJoinedSource)
                Expect(!_state.scheduler.InJoinedRetirementCallback(ticket) &&
                           !_state.pump.AcknowledgeSourceJoinedShutdownOwner(_state.source),
                       "ordinary-successful-opening-cannot-acknowledge-a-joined-close-source");
            _state.stage = State::Stage::Record;
            Expect(_state.scheduler.Resume(ticket), "recording-resumes-same-original-ticket");
            return true;
        }
        if (_state.stage == State::Stage::Record)
        {
            _state.recorderJoined = true;
            if (_state.disposition == GpuCommandFailureDisposition::RetainOriginalForRecovery)
            {
                Expect(_state.policyInitialized, "private-policy-initializes-on-original-live-record");
                Expect(_state.policyStopped && _state.recordingTokenLive,
                       "stopped-original-source-refuses-private-recording-with-retained-live-token");
            }
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

    void RetainJoinedRetirement(JobScheduler& jobs, JobTicket original) noexcept override
    {
        if (!_state.testJoinedSource)
            return;
        Expect(&jobs == &_state.scheduler && original == _state.original && jobs.InJoinedRetirementCallback(original) &&
                   !jobs.InAcknowledgedJobCallback(original) && _state.recorderJoined &&
                   jobs.ReservedMemoryBytes() == 65536,
               "actual-original-joined-hook-keeps-exact-ticket-charge-and-distinct-destruction-authority");
        auto stale = _state.source;
        ++stale.generation;
        Expect(!_state.pump.AcknowledgeSourceJoinedShutdownOwner(stale) && !_state.stream.CanRetireOwner() &&
                   _state.pump.QuarantinedSourceCountOwnerForTest() == 1,
               "stale-source-lease-cannot-resolve-the-original-executed-command");
        std::atomic<bool> foreignRefused{false};
        std::thread foreign(
            [&]() noexcept
            {
                foreignRefused.store(!_state.pump.AcknowledgeSourceJoinedShutdownOwner(_state.source),
                                     std::memory_order_release);
            });
        foreign.join();
        Expect(foreignRefused.load(std::memory_order_acquire) && !_state.stream.CanRetireOwner(),
               "foreign-thread-cannot-use-active-original-joined-authority");
        _state.exactJoinedSource = _state.pump.AcknowledgeSourceJoinedShutdownOwner(_state.source);
        Expect(_state.exactJoinedSource && _state.stream.CanRetireOwner() &&
                   _state.pump.QuarantinedSourceCountOwnerForTest() == 0 && _state.acknowledgements == 0 &&
                   _state.failures == 1 && !_state.terminal && jobs.ReservedMemoryBytes() == 65536,
               "exact-source-shutdown-acknowledges-destruction-only-without-callback-or-refund");
        const bool detached = _state.pump.DetachOwner(_state.source, _state.detached);
        _state.attached = !detached;
        Expect(detached && _state.detached && _state.stream.AuthorizeRetirementOwner(),
               "joined-source-transfers-all-original-context-owners-before-wrapper-dies");
        _state.joinedHookReturned = true;
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

void Run(bool failFirst, bool failRecovery, bool foreignDevice = false, bool retainOriginal = false,
         bool userCancellation = false, bool joinedSource = false)
{
    recoveryCase = retainOriginal;
    joinedSourceCase = joinedSource;
    const auto restoreCase = wil::scope_exit(
        []() noexcept
        {
            recoveryCase = false;
            joinedSourceCase = false;
        });
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
    state.testJoinedSource = joinedSource;
    state.disposition = retainOriginal ? GpuCommandFailureDisposition::RetainOriginalForRecovery
                                       : GpuCommandFailureDisposition::CancelOriginal;
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
                                 if (active.testJoinedSource)
                                     Expect(active.wrapperDestroyed && active.exactJoinedSource && !active.attached &&
                                                !active.pump.HasPending() && active.detached && !active.terminal,
                                            "application-close-hook-follows-original-wrapper-detach-and-destruction");
                                 active.pump.AcknowledgeJoinedShutdownOwner();
                                 if (active.attached)
                                 {
                                     const bool detached = active.pump.DetachOwner(active.source, active.detached);
                                     Expect(detached, "shutdown-detaches-only-after-joined-producers");
                                     active.attached = !detached;
                                     Expect(active.stream.AuthorizeRetirementOwner(),
                                            "shutdown-authorizes-destruction-only");
                                 }
                                 if (active.testJoinedSource)
                                     Expect(!active.scheduler.InJoinedRetirementCallback(active.original) &&
                                                !active.pump.AcknowledgeSourceJoinedShutdownOwner(active.source) &&
                                                active.acknowledgements == 0 && !active.terminal,
                                            "later-owner-close-hook-cannot-reuse-expired-original-authority");
                             },
                             [](void* opaque) noexcept
                             {
                                 auto& active = static_cast<Cleanup*>(opaque)->state;
                                 if (active.initialized && !active.retired.load(std::memory_order_acquire))
                                     active.retired.store(active.stream.RetireForWork(), std::memory_order_release);
                                 active.detached = {};
                                 if (active.testJoinedSource)
                                     Expect(active.retired.load(std::memory_order_acquire) && !active.detached &&
                                                GetCurrentThreadId() != active.ownerThread,
                                            "original-stream-and-transferred-contexts-retire-on-final-worker");
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
        if (joinedSource)
        {
            pump.StopOwner();
            Expect(!scheduler.InJoinedRetirementCallback(original) &&
                       !pump.AcknowledgeSourceJoinedShutdownOwner(state.source) && !state.stream.CanRetireOwner() &&
                       state.acknowledgements == 0 && scheduler.ReservedMemoryBytes() == 65536,
                   "stopped-pump-and-returned-producer-alone-cannot-claim-original-joined-close-authority");
        }
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
    if (userCancellation)
        Expect(scheduler.Cancel(original), "real-user-cancellation-keeps-original-ticket-and-stop-token");
    const bool detached = pump.DetachOwner(state.source, state.detached);
    Expect(detached && state.detached, "transfer-all-original-context-owners-to-worker-retirement");
    state.attached = !detached;
    const bool authorized = detached && state.stream.AuthorizeRetirementOwner();
    Expect(authorized, "authorize-after-actual-recording-and-gpu-acknowledgements");
    state.stage = State::Stage::Retire;
    const bool resumed = authorized && scheduler.Resume(original);
    Expect(resumed && Until(scheduler, [&]() { return state.terminal; }) &&
               state.result.status == (userCancellation ? JobStatus::Cancelled
                                       : retainOriginal ? JobStatus::Failed
                                       : fails          ? JobStatus::Cancelled
                                                        : JobStatus::Finished) &&
               state.retired.load(std::memory_order_acquire),
           "same-record-returns-only-after-worker-retirement");
    if (retainOriginal)
        Expect(state.retireBodyEntered == !userCancellation && state.retirementTokenCancelled == userCancellation &&
                   (userCancellation || state.result.error == static_cast<std::int32_t>(state.failure)),
               "retained-failure-token-allows-original-stage-but-real-cancellation-skips-useful-body");
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
    Run(true, false, false, true);
    Run(true, false, false, true, true);
    Run(true, true, false, true);
    Run(true, true, false, true, false, true);
    return gFailures;
}
