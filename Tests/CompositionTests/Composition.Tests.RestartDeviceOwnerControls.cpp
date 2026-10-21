#include "Composition.Tests.RestartDeviceOwnerControls.h"

#include "GpuCommandPump.h"
#include "GpuCommandStream.h"
#include "OwnerWorkMultiplexer.h"

#include <atomic>
#include <cstdio>
#include <exception>
#include <memory>
#include <utility>

// This fixture stops at the stream API boundary: the test proves the
// original ticket, source generation, real queue callbacks, and retirement gates, but does not prove
// parent-level exclusion of unrelated graph/copy/map users. RestartDeviceOwner has no ticket argument;
// wrong-source authority is exercised by a second actual JobScheduler callback while the original
// source remains deferred. The outside-authority case runs from the registered owner route.
namespace RedPrism::CompositionTests
{
namespace
{
constexpr std::uint64_t kOriginalReservation = 65536;
constexpr std::uint64_t kProbeReservation = 1024;
constexpr DWORD kWaitFailureBoundMs = 10000;

unsigned int gRestartControlFailures = 0;

void Expect(bool condition, const char* name) noexcept
{
    std::printf("%s restart-device-owner-controls.%s\n", condition ? "PASS" : "FAIL", name);
    std::fflush(stdout);
    gRestartControlFailures += condition ? 0u : 1u;
}

bool Memory(void*, MemorySnapshot& snapshot) noexcept
{
    snapshot = {4ull * 1024 * 1024 * 1024, 4ull * 1024 * 1024 * 1024};
    return true;
}

class State;

class StateRoute final : public OwnerWorkPump
{
  public:
    State* state = nullptr;
    [[nodiscard]] bool HasPending() const noexcept override;
    [[nodiscard]] bool HasReady() const noexcept override;
    void Pump() noexcept override;
};

struct Observation final
{
    JobTicket original;
    JobResult terminal;
    GpuCommandLease firstAcknowledged;
    unsigned int acknowledgements = 0;
    unsigned int failures = 0;
    std::uint64_t originalGeneration = 0;
    std::uint64_t replacementGeneration = 0;
    bool finished = false;
    bool replacementAccepted = false;
    bool originalChargeObserved = false;
    bool refusalStatePreserved = false;
    bool outsideAttemptDone = false;
    bool wrongSourceAttemptDone = false;
    bool probeComplete = false;
};

// External root: the scheduler-owned OriginalJob below only borrows this state. The queue receiver,
// owner route, and both Close hooks therefore remain valid after JobScheduler destroys that wrapper.
class State final : public GpuCommandSourceReceiver
{
  public:
    enum class Stage : unsigned char
    {
        Open,
        Record,
        AwaitCommand,
        Retire,
        WaitForControls,
        Finish
    };

    State(JobScheduler& scheduler, GpuWorkQueue& queue, GpuWorkQueue& foreignQueue, GpuCommandPump& commands,
          StateRoute& route, ID3D11Device& initialDevice, ID3D11Device& replacementDevice,
          ID3D11DeviceContext& replacementImmediate, Observation& observation, bool validReplacement) noexcept
        : jobs(scheduler), queue(queue), foreignQueue(foreignQueue), commands(commands), route(route),
          initialDevice(initialDevice), replacementDevice(replacementDevice),
          replacementImmediate(replacementImmediate), observation(observation), validReplacement(validReplacement)
    {
    }

    JobScheduler& jobs;
    GpuWorkQueue& queue;
    GpuWorkQueue& foreignQueue;
    GpuCommandPump& commands;
    StateRoute& route;
    ID3D11Device& initialDevice;
    ID3D11Device& replacementDevice;
    ID3D11DeviceContext& replacementImmediate;
    Observation& observation;
    const bool validReplacement;

    GpuCommandStream stream;
    GpuCommandSourceLease source;
    GpuCompletionContext initialContext;
    GpuCompletionContext foreignContext;
    GpuCompletionContext currentContext;
    GpuCompletionContext detachedContext;
    JobTicket original;
    JobTicket probeTicket;
    Stage stage = Stage::Open;
    std::atomic<bool> retired{false};
    unsigned int round = 0;
    unsigned int acknowledgements = 0;
    unsigned int failures = 0;
    bool attached = false;
    bool streamInitialized = false;
    bool retirementAuthorized = false;
    bool producerFinished = false;
    bool outsideAttemptDone = false;
    bool wrongSourceAttemptDone = false;
    bool probeComplete = false;
    bool replacementAccepted = false;
    bool originalChargeObserved = false;
    bool refusalStatePreserved = false;
    bool joinedRootRetained = false;
    bool wrapperDestroyed = false;
    std::uint64_t originalGeneration = 0;
    std::uint64_t replacementGeneration = 0;
    std::uint64_t firstProducerEpoch = 0;
    std::uint64_t replacementProducerEpoch = 0;
    GpuCommandLease firstAcknowledged;

    JobResult Run(JobContext& context)
    {
        if (stage != Stage::Record)
            return {};

        producerFinished = false;
        HRESULT result = S_OK;
        const auto finish = wil::scope_exit(
            [&]() noexcept
            {
                stream.FinishProducerForWork(result);
                producerFinished = true;
            });

        if (round == 0)
            firstProducerEpoch = stream.ProductionEpochForWork();
        else
            replacementProducerEpoch = stream.ProductionEpochForWork();

        wil::com_ptr_nothrow<ID3D11DeviceContext> deferred;
        ID3D11Device& recordingDevice = round == 0 ? initialDevice : replacementDevice;
        result = recordingDevice.CreateDeferredContext(0, deferred.put());
        wil::com_ptr_nothrow<ID3D11CommandList> command;
        if (SUCCEEDED(result))
            result = deferred->FinishCommandList(FALSE, command.put());
        if (SUCCEEDED(result))
            result = stream.PublishForWork(std::move(command), context.cancellation);
        if (FAILED(result))
            return {JobStatus::Failed, result};
        return {};
    }

    void RetireWorkerStage(JobContext&, JobResult result) noexcept
    {
        if (stage == Stage::Record && !producerFinished)
        {
            stream.FinishProducerForWork(result.status == JobStatus::Finished ? S_OK : E_FAIL);
            producerFinished = true;
        }
        if (stage == Stage::Retire)
        {
            if (!stream.RetireForWork())
                std::terminate();
            retired.store(true, std::memory_order_release);
            retirementAuthorized = true;
            detachedContext = {};
        }
        if (stage == Stage::Finish)
        {
            // Test-held captures are released off-owner like the production parent retires its captured graph.
            initialContext = {};
            foreignContext = {};
            currentContext = {};
            detachedContext = {};
        }
    }

    [[nodiscard]] bool DeferRetirement(JobResult result, JobTicket ticket) noexcept
    {
        if (ticket != original && original.generation != 0)
            std::terminate();
        if (!original.generation)
            original = ticket;
        if (ticket != original || result.status != JobStatus::Finished)
            return false;

        switch (stage)
        {
        case Stage::Open:
        {
            Expect(jobs.InAcknowledgedJobCallback(original), "opening-uses-exact-original-ack-callback");
            HRESULT status = queue.CaptureCompletionContext(initialContext);
            if (SUCCEEDED(status))
                status = foreignQueue.CaptureCompletionContext(foreignContext);
            Expect(SUCCEEDED(status), "capture-real-original-and-foreign-queue-contexts");
            if (FAILED(status))
                return false;
            originalGeneration = initialContext.DeviceGeneration();
            Expect(originalGeneration == foreignContext.DeviceGeneration() &&
                       queue.DeviceGeneration() == foreignQueue.DeviceGeneration(),
                   "foreign-queue-starts-at-same-real-device-generation");
            ID3D11Device* originalBorrowedDevice = nullptr;
            ID3D11DeviceContext* originalBorrowedContext = nullptr;
            ID3D11Device* foreignBorrowedDevice = nullptr;
            ID3D11DeviceContext* foreignBorrowedContext = nullptr;
            const HRESULT originalBorrow =
                queue.BorrowCurrentContextOwner(initialContext, originalBorrowedDevice, originalBorrowedContext);
            const HRESULT foreignBorrow =
                foreignQueue.BorrowCurrentContextOwner(foreignContext, foreignBorrowedDevice, foreignBorrowedContext);
            Expect(originalBorrow == S_OK && foreignBorrow == S_OK && originalBorrowedDevice == foreignBorrowedDevice &&
                       originalBorrowedContext == foreignBorrowedContext,
                   "foreign-origin-witness-borrows-same-actual-device-and-immediate-context");
            status = stream.InitializeOwner(jobs, ticket, queue, initialContext);
            streamInitialized = SUCCEEDED(status);
            if (SUCCEEDED(status))
                status = commands.AttachOwner(stream, ticket, *this, source,
                                              GpuCommandFailureDisposition::RetainOriginalForRecovery);
            attached = SUCCEEDED(status);
            Expect(attached, "attach-real-original-ticket-and-stream");
            if (!attached)
                return false;
            stage = Stage::Record;
            Expect(jobs.Resume(original, JobExecutionLane::GpuRecording), "resume-original-recording-ticket");
            return true;
        }
        case Stage::Record:
            stage = Stage::AwaitCommand;
            jobs.NotifyOwnerWork();
            return true;
        case Stage::Retire:
            return RetiredStageAcknowledged();
        case Stage::WaitForControls:
            stage = Stage::Finish;
            Expect(jobs.Resume(original), "resume-original-after-refusal-controls");
            return true;
        case Stage::Finish:
            return false;
        case Stage::AwaitCommand:
            std::terminate();
        }
        std::terminate();
    }

    void Completed(JobResult result) noexcept
    {
        route.state = nullptr;
        observation.original = original;
        observation.terminal = result;
        observation.firstAcknowledged = firstAcknowledged;
        observation.acknowledgements = acknowledgements;
        observation.failures = failures;
        observation.originalGeneration = originalGeneration;
        observation.replacementGeneration = replacementGeneration;
        observation.finished = true;
        observation.replacementAccepted = replacementAccepted;
        observation.originalChargeObserved = originalChargeObserved;
        observation.refusalStatePreserved = refusalStatePreserved;
        observation.outsideAttemptDone = outsideAttemptDone;
        observation.wrongSourceAttemptDone = wrongSourceAttemptDone;
        observation.probeComplete = probeComplete;
        Expect(result.status == JobStatus::Finished, "original-job-finishes-after-controls");
    }

    void RetainJoinedRetirement(JobScheduler& scheduler, JobTicket ticket) noexcept
    {
        const bool exact = &scheduler == &jobs && ticket == original && scheduler.InJoinedRetirementCallback(ticket) &&
                           !scheduler.InAcknowledgedJobCallback(ticket);
        Expect(exact, "joined-close-validates-preexisting-external-root");
        joinedRootRetained = exact;
    }

    void CommandAcknowledgedOwner(JobTicket ticket, GpuCommandLease command, HRESULT status) noexcept override
    {
        const std::uint64_t expectedGeneration = round == 0 ? originalGeneration : replacementGeneration;
        Expect(ticket == original && command.source == original && SUCCEEDED(status),
               "only-real-original-queue-callback-acknowledges-command");
        Expect(command.deviceGeneration == expectedGeneration,
               "real-command-callback-reports-the-actual-current-stream-generation");
        Expect(command.ordinal == acknowledgements + 1, "original-command-ordinals-remain-monotonic");
        if (acknowledgements == 0)
            firstAcknowledged = command;
        ++acknowledgements;
        observation.original = original;
        observation.acknowledgements = acknowledgements;
        observation.firstAcknowledged = firstAcknowledged;
        jobs.NotifyOwnerWork();
    }

    void CommandSourceFailedOwner(JobTicket ticket, HRESULT error) noexcept override
    {
        Expect(ticket == original && FAILED(error), "source-failure-keeps-original-ticket");
        ++failures;
        observation.failures = failures;
    }

    [[nodiscard]] bool Ready() const noexcept
    {
        if (stage == Stage::AwaitCommand)
            return stream.CanRetireOwner();
        if (stage == Stage::WaitForControls)
            return !outsideAttemptDone || probeComplete;
        return false;
    }

    void PumpOwner() noexcept
    {
        if (stage == Stage::AwaitCommand && stream.CanRetireOwner())
        {
            const bool detached = commands.DetachOwner(source, detachedContext);
            attached = !detached;
            const bool authorized = detached && stream.AuthorizeRetirementOwner();
            Expect(detached && detachedContext && authorized,
                   "actual-queue-ack-and-joined-producer-authorize-retirement");
            if (!detached || !authorized)
                std::terminate();
            source = {};
            retirementAuthorized = true;
            if (round == 0 && !validReplacement)
                refusalStatePreserved = true;
            stage = Stage::Retire;
            retired.store(false, std::memory_order_release);
            Expect(jobs.Resume(original), "resume-same-ticket-for-worker-retirement");
            return;
        }

        if (stage == Stage::WaitForControls && !outsideAttemptDone)
        {
            const HRESULT outside = stream.RestartDeviceOwner(queue, currentContext);
            Expect(outside == E_INVALIDARG && !jobs.InAcknowledgedJobCallback(original),
                   "owner-pump-outside-source-callback-cannot-restart");
            Expect(jobs.ReservedMemoryBytes() >= kOriginalReservation && stream.NextOwner().command == nullptr &&
                       acknowledgements == 1,
                   "outside-callback-refusal-retains-original-charge-and-has-no-replay");
            outsideAttemptDone = true;
            observation.outsideAttemptDone = true;
            if (probeComplete)
                jobs.NotifyOwnerWork();
            return;
        }

        if (stage == Stage::WaitForControls && outsideAttemptDone && probeComplete)
        {
            stage = Stage::Finish;
            Expect(jobs.Resume(original), "resume-original-after-wrong-source-callback");
        }
    }

  private:
    [[nodiscard]] bool RetiredStageAcknowledged() noexcept;

    class WrongSourceProbe final : public Job
    {
      public:
        explicit WrongSourceProbe(State& source) noexcept : _source(source) {}

        JobResult Run(JobContext&) override
        {
            return {};
        }

        [[nodiscard]] bool DeferRetirement(JobResult result, JobTicket ticket) noexcept override
        {
            const bool authorityIsDifferent = result.status == JobStatus::Finished && ticket == _source.probeTicket &&
                                              _source.jobs.InAcknowledgedJobCallback(ticket) &&
                                              !_source.jobs.InAcknowledgedJobCallback(_source.original);
            const HRESULT status = _source.stream.RestartDeviceOwner(_source.queue, _source.currentContext);
            Expect(authorityIsDifferent && status == E_INVALIDARG,
                   "another-real-job-callback-cannot-restart-original-stream");
            Expect(_source.jobs.ReservedMemoryBytes() >= kOriginalReservation + kProbeReservation &&
                       _source.stream.NextOwner().command == nullptr && _source.acknowledgements == 1,
                   "wrong-source-refusal-retains-original-charge-and-has-no-replay");
            _source.wrongSourceAttemptDone = true;
            _source.observation.wrongSourceAttemptDone = true;
            return false;
        }

        void Completed(JobResult result) noexcept override
        {
            Expect(result.status == JobStatus::Finished, "wrong-source-probe-completes-normally");
            // DrainCompletions has already run this drain's owner route before reaching this callback.
            // Publish readiness now; the original ticket can resume only on a later event-driven drain.
            _source.probeComplete = true;
            _source.observation.probeComplete = true;
            _source.jobs.NotifyOwnerWork();
        }

      private:
        State& _source;
    };
};

class OriginalJob final : public Job
{
  public:
    explicit OriginalJob(State& root) noexcept : _root(root) {}
    ~OriginalJob() override
    {
        _root.wrapperDestroyed = true;
    }

    JobResult Run(JobContext& context) override
    {
        return _root.Run(context);
    }
    void RetireWorkerStage(JobContext& context, JobResult result) noexcept override
    {
        _root.RetireWorkerStage(context, result);
    }
    void RetainJoinedRetirement(JobScheduler& scheduler, JobTicket ticket) noexcept override
    {
        _root.RetainJoinedRetirement(scheduler, ticket);
    }
    [[nodiscard]] bool DeferRetirement(JobResult result, JobTicket ticket) noexcept override
    {
        return _root.DeferRetirement(result, ticket);
    }
    void Completed(JobResult result) noexcept override
    {
        _root.Completed(result);
    }

  private:
    State& _root;
};

bool State::RetiredStageAcknowledged() noexcept
{
    Expect(jobs.InAcknowledgedJobCallback(original) && retired.load(std::memory_order_acquire) &&
               stream.CanRetireOwner() && acknowledgements == round + 1 && !failures,
           "restart-controls-run-in-exact-original-callback-after-real-retirement");
    originalChargeObserved = jobs.ReservedMemoryBytes() >= kOriginalReservation;
    Expect(originalChargeObserved && jobs.InAcknowledgedJobCallback(original) && stream.NextOwner().command == nullptr,
           "original-ticket-callback-and-charge-remain-held-before-restart-decision");

    if (validReplacement && round == 0)
    {
        const HRESULT rebound = queue.Bind(&replacementDevice, &replacementImmediate);
        Expect(SUCCEEDED(rebound) && queue.DeviceGeneration() > originalGeneration,
               "same-queue-rebind-uses-real-new-warp-device-and-generation");
        const HRESULT captured = SUCCEEDED(rebound) ? queue.CaptureCompletionContext(currentContext) : rebound;
        Expect(SUCCEEDED(captured), "capture-real-current-replacement-context");
        const HRESULT restarted = SUCCEEDED(captured) ? stream.RestartDeviceOwner(queue, currentContext) : captured;
        replacementAccepted = restarted == S_OK;
        replacementGeneration = currentContext.DeviceGeneration();
        retired.store(false, std::memory_order_release);
        retirementAuthorized = false;
        Expect(replacementAccepted && replacementGeneration > originalGeneration &&
                   stream.BelongsToOwner(jobs, original, replacementGeneration) && firstProducerEpoch != 0,
               "valid-current-context-restarts-same-original-source");
        if (!replacementAccepted)
            return false;

        attached = SUCCEEDED(commands.AttachOwner(stream, original, *this, source,
                                                  GpuCommandFailureDisposition::RetainOriginalForRecovery));
        Expect(attached, "re-attach-restarted-stream-to-original-ticket");
        if (!attached)
            return false;
        round = 1;
        stage = Stage::Record;
        Expect(jobs.Resume(original, JobExecutionLane::GpuRecording), "record-after-valid-device-restart");
        return true;
    }

    if (validReplacement)
    {
        Expect(acknowledgements == 2 && firstAcknowledged.ordinal == 1 && firstAcknowledged.source == original &&
                   firstAcknowledged.deviceGeneration == originalGeneration &&
                   replacementProducerEpoch == firstProducerEpoch + 1,
               "replacement-executes-only-new-monotonic-command-on-original-source");
        stage = Stage::Finish;
        return false;
    }

    const std::size_t attachedBefore = commands.AttachedSourceCountOwnerForTest();
    // Queue B has the same device pointer and numeric generation as queue A, but its actual
    // owner-created identity token differs. This call must fail before borrowing its context.
    const HRESULT foreign = stream.RestartDeviceOwner(foreignQueue, foreignContext);
    Expect(foreign == E_INVALIDARG, "foreign-queue-origin-refused-with-same-device-and-generation");
    Expect(queue.Bind(&replacementDevice, &replacementImmediate) == S_OK &&
               queue.DeviceGeneration() > originalGeneration,
           "rebind-original-queue-to-create-real-stale-context-witness");
    const HRESULT freshCapture = queue.CaptureCompletionContext(currentContext);
    Expect(freshCapture == S_OK, "capture-current-context-after-rebind");
    const HRESULT stale = stream.RestartDeviceOwner(queue, initialContext);
    Expect(stale == E_ABORT, "same-origin-old-generation-context-refused-as-stale");

    const bool unchanged = stream.NextOwner().command == nullptr && acknowledgements == 1 && failures == 0 &&
                           commands.AttachedSourceCountOwnerForTest() == attachedBefore &&
                           jobs.InAcknowledgedJobCallback(original) &&
                           jobs.ReservedMemoryBytes() >= kOriginalReservation;
    refusalStatePreserved = unchanged;
    Expect(unchanged, "foreign-and-stale-refusals-preserve-source-charge-and-do-not-replay");

    // The owner pump exercises the outside-callback refusal. A second admitted job then calls
    // the same API in its own successful scheduler callback to prove wrong-source rejection.
    stage = Stage::WaitForControls;
    std::unique_ptr<Job> probe = std::make_unique<WrongSourceProbe>(*this);
    const HRESULT submitted = static_cast<HRESULT>(
        jobs.Submit(probe, {.kind = JobClass::Background, .memoryBytes = kProbeReservation, .deferredRetirement = true},
                    probeTicket));
    Expect(submitted == S_OK && !probe, "submit-real-wrong-source-callback-probe");
    if (submitted != S_OK)
        return false;
    jobs.NotifyOwnerWork();
    return true;
}

bool StateRoute::HasPending() const noexcept
{
    return state != nullptr;
}

bool StateRoute::HasReady() const noexcept
{
    return state && state->Ready();
}

void StateRoute::Pump() noexcept
{
    if (state)
        state->PumpOwner();
}

template <typename Predicate> bool Until(JobScheduler& scheduler, Predicate ready) noexcept
{
    const ULONGLONG started = GetTickCount64();
    for (;;)
    {
        (void)DrainCompletions(scheduler);
        if (ready())
            return true;
        const ULONGLONG elapsed = GetTickCount64() - started;
        if (elapsed >= kWaitFailureBoundMs)
            return false;
        const DWORD remaining = kWaitFailureBoundMs - static_cast<DWORD>(elapsed);
        const DWORD wait = WaitForSingleObject(static_cast<HANDLE>(scheduler.WakeHandle()), remaining);
        if (wait != WAIT_OBJECT_0)
            return false;
    }
}

int RunOne(bool validReplacement)
{
    wil::com_ptr_nothrow<ID3D11Device> initialDevice;
    wil::com_ptr_nothrow<ID3D11DeviceContext> initialImmediate;
    wil::com_ptr_nothrow<ID3D11Device> replacementDevice;
    wil::com_ptr_nothrow<ID3D11DeviceContext> replacementImmediate;
    constexpr D3D_FEATURE_LEVEL kFeatureLevel = D3D_FEATURE_LEVEL_11_0;
    const HRESULT initialStatus =
        D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, &kFeatureLevel, 1,
                          D3D11_SDK_VERSION, initialDevice.put(), nullptr, initialImmediate.put());
    Expect(SUCCEEDED(initialStatus), "initial-real-warp-device-created");
    if (FAILED(initialStatus))
        return 1;
    const HRESULT replacementStatus =
        D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, &kFeatureLevel, 1,
                          D3D11_SDK_VERSION, replacementDevice.put(), nullptr, replacementImmediate.put());
    Expect(SUCCEEDED(replacementStatus) && replacementDevice.get() != initialDevice.get(),
           "replacement-is-distinct-real-warp-device");
    if (FAILED(replacementStatus) || replacementDevice.get() == initialDevice.get())
        return 1;

    JobScheduler scheduler;
    OwnerWorkMultiplexer routes;
    GpuWorkQueue queue;
    GpuWorkQueue foreignQueue;
    GpuCommandPump commands;
    StateRoute route;
    Observation observation;
    State root{scheduler,
               queue,
               foreignQueue,
               commands,
               route,
               *initialDevice.get(),
               *replacementDevice.get(),
               *replacementImmediate.get(),
               observation,
               validReplacement};
    bool schedulerInitialized = false;
    bool queueInitialized = false;
    bool foreignQueueInitialized = false;
    bool commandsInitialized = false;
    bool routeAttached = false;
    bool parentSubmitted = false;
    struct Cleanup final
    {
        JobScheduler& scheduler;
        GpuWorkQueue& queue;
        GpuWorkQueue& foreignQueue;
        GpuCommandPump& commands;
        State& root;
        bool& schedulerInitialized;
        bool& queueInitialized;
        bool& foreignQueueInitialized;
        bool& commandsInitialized;
        bool& routeAttached;
    } cleanup{scheduler,
              queue,
              foreignQueue,
              commands,
              root,
              schedulerInitialized,
              queueInitialized,
              foreignQueueInitialized,
              commandsInitialized,
              routeAttached};

    const auto cleanupScope = wil::scope_exit(
        [&]() noexcept
        {
            if (commandsInitialized)
                commands.StopOwner();
            if (queueInitialized)
                queue.StopNotifications();
            if (foreignQueueInitialized)
                foreignQueue.StopNotifications();
            if (schedulerInitialized)
            {
                scheduler.Close(
                    {[](void* opaque) noexcept
                     {
                         auto& state = *static_cast<Cleanup*>(opaque);
                         if (state.commandsInitialized)
                         {
                             state.commands.AcknowledgeJoinedShutdownOwner();
                             if (state.root.joinedRootRetained)
                                 Expect(state.root.wrapperDestroyed,
                                        "joined-wrapper-is-destroyed-before-external-root-owner-cleanup");
                             if (state.root.attached)
                             {
                                 const bool detached =
                                     state.commands.DetachOwner(state.root.source, state.root.detachedContext);
                                 Expect(detached, "startup-or-close-detaches-only-after-joined-producers");
                                 state.root.attached = !detached;
                                 if (detached)
                                 {
                                     const bool authorized = state.root.stream.AuthorizeRetirementOwner();
                                     state.root.retirementAuthorized = authorized;
                                     Expect(authorized, "joined-close-authorizes-destruction-only-retirement");
                                 }
                             }
                         }
                     },
                     [](void* opaque) noexcept
                     {
                         auto& state = *static_cast<Cleanup*>(opaque);
                         if (state.root.streamInitialized)
                         {
                             if (state.root.retirementAuthorized && !state.root.retired.load(std::memory_order_acquire))
                             {
                                 const bool retired = state.root.stream.RetireForWork();
                                 state.root.retired.store(retired, std::memory_order_release);
                                 Expect(retired, "final-worker-retires-authorized-stream-after-join");
                             }
                         }
                         state.root.initialContext = {};
                         state.root.foreignContext = {};
                         state.root.currentContext = {};
                         state.root.detachedContext = {};
                     },
                     &cleanup});
            }
            if (queueInitialized)
                queue.CloseAfterWorkers();
            if (foreignQueueInitialized)
                foreignQueue.CloseAfterWorkers();
            if (commandsInitialized)
                Expect(commands.CloseOwner(), "close-gpu-command-pump");
            route.state = nullptr;
            if (routeAttached)
                Expect(routes.Detach(route), "detach-test-owner-route");
            if (schedulerInitialized)
                Expect(scheduler.ReservedMemoryBytes() == 0, "all-original-and-probe-reservations-refunded-at-close");
        });

    const HRESULT initialized = scheduler.Initialize(1, {}, {Memory, nullptr});
    schedulerInitialized = SUCCEEDED(initialized);
    Expect(schedulerInitialized, "one-worker-original-job-scheduler-initialized");
    if (!schedulerInitialized)
        return 1;
    const HRESULT lane = scheduler.EnableGpuRecordingLane();
    Expect(SUCCEEDED(lane), "private-gpu-recording-lane-enabled");
    if (FAILED(lane))
        return 1;
    const bool ownerRegistered = scheduler.RegisterOwnerWork(routes);
    Expect(ownerRegistered, "register-owner-work-multiplexer");
    if (!ownerRegistered)
        return 1;

    queueInitialized = true; // Cleanup also handles a partially completed Initialize call.
    HRESULT status = queue.Initialize(scheduler, &routes);
    if (SUCCEEDED(status))
        status = queue.Bind(initialDevice.get(), initialImmediate.get());
    Expect(SUCCEEDED(status), "bind-original-warp-queue");
    if (FAILED(status))
        return 1;

    foreignQueueInitialized = true; // Cleanup also handles a partially completed Initialize call.
    status = foreignQueue.Initialize(scheduler, &routes);
    if (SUCCEEDED(status))
        status = foreignQueue.Bind(initialDevice.get(), initialImmediate.get());
    Expect(SUCCEEDED(status), "bind-foreign-origin-to-same-real-device-and-context");
    if (FAILED(status))
        return 1;

    status = commands.InitializeOwner(scheduler, queue, routes);
    commandsInitialized = SUCCEEDED(status);
    Expect(SUCCEEDED(status), "initialize-existing-gpu-command-pump");
    if (FAILED(status))
        return 1;
    routeAttached = routes.Attach(route);
    Expect(routeAttached, "register-restart-control-owner-route");
    if (!routeAttached)
        return 1;

    route.state = &root;
    std::unique_ptr<Job> parent = std::make_unique<OriginalJob>(root);
    JobTicket ticket{};
    status = static_cast<HRESULT>(scheduler.Submit(parent,
                                                   {.kind = JobClass::Background,
                                                    .memoryBytes = kOriginalReservation,
                                                    .deferredRetirement = true,
                                                    .executionLane = JobExecutionLane::Compute},
                                                   ticket));
    parentSubmitted = SUCCEEDED(status);
    Expect(parentSubmitted && !parent, "admit-one-original-retained-source-job");
    if (status != S_OK)
    {
        route.state = nullptr;
        return 1;
    }
    root.original = ticket;

    const bool completed = Until(scheduler, [&]() { return observation.finished; });
    Expect(completed, "bounded-event-wait-reaches-terminal-original-callback");
    if (completed)
    {
        Expect(observation.original == ticket && observation.acknowledgements == (validReplacement ? 2u : 1u) &&
                   observation.failures == 0,
               "original-ticket-has-only-real-expected-queue-acknowledgements");
        if (validReplacement)
            Expect(observation.replacementAccepted &&
                       observation.replacementGeneration > observation.originalGeneration,
                   "valid-replacement-control-accepted");
        else
            Expect(observation.originalChargeObserved && observation.refusalStatePreserved &&
                       observation.outsideAttemptDone && observation.wrongSourceAttemptDone &&
                       observation.probeComplete,
                   "all-supported-refusal-controls-completed-with-original-charge-held");
    }
    return gRestartControlFailures ? 1 : 0;
}
} // namespace

int RunRestartDeviceOwnerControlsTests()
{
    gRestartControlFailures = 0;
    (void)RunOne(false);
    (void)RunOne(true);
    return gRestartControlFailures ? 1 : 0;
}
} // namespace RedPrism::CompositionTests
