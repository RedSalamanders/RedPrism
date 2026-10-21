#include "Composition.Tests.EmptyJoinedShutdown.h"

#include "GpuCommandPump.h"
#include "GpuCommandStream.h"
#include "GpuWork.h"
#include "Jobs.h"
#include "OwnerWorkMultiplexer.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cwchar>
#include <memory>
#include <objbase.h>
#include <utility>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/com.h>
#include <wil/resource.h>
#pragma warning(pop)

namespace
{
constexpr std::uint64_t kReservationBytes = 65536;
constexpr DWORD kDeadlockBoundMs = 10000;
int gFailures = 0;

void Expect(bool condition, const char* name) noexcept
{
    std::printf("%s empty-shutdown.%s\n", condition ? "PASS" : "FAIL", name);
    std::fflush(stdout);
    gFailures += condition ? 0 : 1;
}

enum class Mode
{
    EmptyNeverResumed,
    FinishedProducer,
    StartedAfterStop,
    PublishedCommand
};

struct State final : GpuCommandSourceReceiver
{
    State(JobScheduler& owner, GpuCommandPump& commandPump, ID3D11Device& sourceDevice, std::uint64_t generation,
          Mode runMode) noexcept
        : scheduler(owner), pump(commandPump), device(sourceDevice), deviceGeneration(generation), mode(runMode)
    {
    }

    JobScheduler& scheduler;
    GpuCommandPump& pump;
    ID3D11Device& device;
    const std::uint64_t deviceGeneration;
    const Mode mode;
    GpuCommandStream stream;
    GpuCompletionContext detached;
    GpuCommandSourceLease source;
    JobTicket original;
    wil::unique_handle published{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    bool initialized = false;
    bool attached = false;
    bool openStageSuspended = false;
    bool recorderStageSuspended = false;
    bool prematureCloseRefused = false;
    bool stoppedWithoutCompletion = false;
    bool preStopEmptyAcknowledgementRefused = false;
    bool emptyAcknowledgedInJoinedHook = false;
    bool detachedInJoinedHook = false;
    bool sourceLeaseClearedAfterDetach = false;
    bool authorizedInJoinedHook = false;
    bool pumpClosedInJoinedHook = false;
    bool ownerHookKeptCharge = false;
    bool workerHookAfterChargedOwner = false;
    bool ownerCannotRetireStream = false;
    bool retiredOnMta = false;
    bool workerRetiredStream = false;
    bool workerDroppedCapture = false;
    unsigned int resumeCount = 0;
    unsigned int usefulAcknowledgements = 0;
    unsigned int failureCallbacks = 0;
    const DWORD ownerThread = GetCurrentThreadId();
    DWORD workerThread = 0;
    std::atomic<HRESULT> publishResult{E_PENDING};
    bool nonOwnerAcknowledgementRefused = false;
    JobResult producerResult{};

    void CommandAcknowledgedOwner(JobTicket, GpuCommandLease, HRESULT) noexcept override
    {
        ++usefulAcknowledgements;
    }

    void CommandSourceFailedOwner(JobTicket, HRESULT) noexcept override
    {
        ++failureCallbacks;
    }
};

HRESULT CreateNonEmptyCommandList(ID3D11Device& device, wil::com_ptr_nothrow<ID3D11CommandList>& command) noexcept
{
    wil::com_ptr_nothrow<ID3D11DeviceContext> deferred;
    HRESULT result = device.CreateDeferredContext(0, deferred.put());
    if (FAILED(result))
        return result;

    constexpr std::array<std::uint32_t, 4> initialValues{0x13579BDFu, 0x2468ACE0u, 0x10203040u, 0x50607080u};
    D3D11_BUFFER_DESC description{};
    description.ByteWidth = static_cast<UINT>(sizeof(initialValues));
    description.Usage = D3D11_USAGE_DEFAULT;
    description.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    D3D11_SUBRESOURCE_DATA initial{};
    initial.pSysMem = initialValues.data();
    wil::com_ptr_nothrow<ID3D11Buffer> buffer;
    result = device.CreateBuffer(&description, &initial, buffer.put());
    if (FAILED(result))
        return result;

    deferred->UpdateSubresource(buffer.get(), 0, nullptr, initialValues.data(), 0, 0);
    return deferred->FinishCommandList(FALSE, command.put());
}

class EmptyShutdownJob final : public Job
{
  public:
    explicit EmptyShutdownJob(State& state) noexcept : _state(state) {}

    JobResult Run(JobContext& context) override
    {
        if (!_recording)
            return {};

        if (_state.mode == Mode::FinishedProducer)
        {
            _state.stream.FinishProducerForWork(S_OK);
            return {};
        }

        if (_state.mode == Mode::StartedAfterStop || _state.mode == Mode::PublishedCommand)
        {
            if (_state.mode == Mode::StartedAfterStop)
                _state.nonOwnerAcknowledgementRefused = !_state.stream.AcknowledgeEmptyJoinedShutdownOwner();
            wil::com_ptr_nothrow<ID3D11CommandList> command;
            HRESULT result = CreateNonEmptyCommandList(_state.device, command);
            if (SUCCEEDED(result))
                result = _state.stream.PublishForWork(std::move(command), context.cancellation);
            _state.publishResult.store(result, std::memory_order_release);
            if (_state.mode == Mode::StartedAfterStop)
            {
                if (!_state.published || !SetEvent(_state.published.get()))
                {
                    const HRESULT error = HRESULT_FROM_WIN32(GetLastError());
                    _state.stream.FinishProducerForWork(error);
                    return {JobStatus::Failed, error};
                }
                const DWORD wait = WaitForSingleObject(static_cast<HANDLE>(context.shutdownEvent), INFINITE);
                const HRESULT terminal = wait == WAIT_OBJECT_0 ? E_ABORT : HRESULT_FROM_WIN32(GetLastError());
                _state.stream.FinishProducerForWork(terminal);
                return {JobStatus::Cancelled};
            }
            if (FAILED(result))
            {
                _state.stream.FinishProducerForWork(result);
                return {JobStatus::Failed, result};
            }
            if (!_state.published || !SetEvent(_state.published.get()))
            {
                const HRESULT error = HRESULT_FROM_WIN32(GetLastError());
                _state.stream.FinishProducerForWork(error);
                return {JobStatus::Failed, error};
            }
            const HANDLE stop = static_cast<HANDLE>(context.shutdownEvent);
            const DWORD wait = WaitForSingleObject(stop, INFINITE);
            const HRESULT terminal = wait == WAIT_OBJECT_0 ? E_ABORT : HRESULT_FROM_WIN32(GetLastError());
            _state.stream.FinishProducerForWork(terminal);
            return {JobStatus::Cancelled};
        }
        return {};
    }

    bool DeferCompletion(JobResult result, JobTicket ticket) noexcept override
    {
        if (!_recording)
        {
            _state.original = ticket;
            _state.producerResult = result;
            _state.initialized =
                SUCCEEDED(_state.stream.InitializeOwner(_state.scheduler, ticket, _state.deviceGeneration));
            const HRESULT attached =
                _state.initialized ? _state.pump.AttachOwner(_state.stream, ticket, _state, _state.source) : E_FAIL;
            _state.attached = SUCCEEDED(attached);
            Expect(_state.attached && ticket == _state.original, "open-stage-suspends-original-ticket-before-attach");
            if (!_state.attached)
                return false;
            _state.openStageSuspended = true;

            const bool closed = _state.pump.CloseOwner();
            _state.prematureCloseRefused = !closed;
            Expect(_state.prematureCloseRefused, "pump-close-refuses-attached-source-before-recorder-resume");
            if (_state.mode == Mode::EmptyNeverResumed)
                return true;

            if (_state.mode == Mode::StartedAfterStop)
                Expect(_state.pump.StopSourceOwner(_state.source), "stop-source-before-recorder-start");

            _recording = true;
            ++_state.resumeCount;
            const bool resumed = _state.scheduler.Resume(ticket, JobExecutionLane::GpuRecording);
            Expect(resumed, "recorder-resumes-same-admitted-ticket");
            return resumed;
        }

        _state.recorderStageSuspended = true;
        _state.producerResult = result;
        return true;
    }

    void Completed(JobResult result) noexcept override
    {
        _state.producerResult = result;
    }

  private:
    State& _state;
    bool _recording = false;
};

bool WaitForHandle(HANDLE event, DWORD timeoutMs) noexcept
{
    return event != nullptr && WaitForSingleObject(event, timeoutMs) == WAIT_OBJECT_0;
}

template <typename Predicate> bool DrainUntil(JobScheduler& scheduler, Predicate&& ready) noexcept
{
    const ULONGLONG start = GetTickCount64();
    while (!ready())
    {
        (void)DrainCompletions(scheduler);
        if (ready())
            break;
        const ULONGLONG elapsed = GetTickCount64() - start;
        if (elapsed >= kDeadlockBoundMs)
            return false;
        const DWORD remaining = static_cast<DWORD>(kDeadlockBoundMs - elapsed);
        if (WaitForSingleObject(static_cast<HANDLE>(scheduler.WakeHandle()), remaining) != WAIT_OBJECT_0)
            return false;
    }
    return true;
}

void JoinedOwnerHook(void* opaque) noexcept
{
    auto& state = *static_cast<State*>(opaque);
    state.ownerHookKeptCharge = state.scheduler.ReservedMemoryBytes() == kReservationBytes;
    HRESULT producerResult = E_PENDING;
    const bool hadProducerResult = state.stream.ProducerFinishedOwner(producerResult);
    state.pump.AcknowledgeJoinedShutdownOwner();
    const bool hasProducerResult = state.stream.ProducerFinishedOwner(producerResult);
    // Empty shutdown publishes E_ABORT in the joined hook. A started producer keeps its own terminal result.
    state.emptyAcknowledgedInJoinedHook = !hadProducerResult && hasProducerResult && producerResult == E_ABORT;
    state.ownerCannotRetireStream = !state.stream.RetireForWork();
    state.detachedInJoinedHook = state.pump.DetachOwner(state.source, state.detached);
    if (state.detachedInJoinedHook)
    {
        state.source = {};
        state.sourceLeaseClearedAfterDetach = state.source.generation == 0;
    }
    state.authorizedInJoinedHook = state.detachedInJoinedHook && state.stream.AuthorizeRetirementOwner();
    state.pumpClosedInJoinedHook = state.authorizedInJoinedHook && state.pump.CloseOwner();
}

void JoinedWorkerHook(void* opaque) noexcept
{
    auto& state = *static_cast<State*>(opaque);
    APTTYPE apartment = APTTYPE_CURRENT;
    APTTYPEQUALIFIER qualifier = APTTYPEQUALIFIER_NONE;
    const HRESULT apartmentResult = CoGetApartmentType(&apartment, &qualifier);
    state.retiredOnMta = SUCCEEDED(apartmentResult) && apartment == APTTYPE_MTA;
    state.workerThread = GetCurrentThreadId();
    // ReservedMemoryBytes is owner-only. Close joins the owner hook before this final callback and
    // refunds reservations only after it returns. Observe that ordering, then check the actual refund
    // on the owner after Close; never read the owner's mutable budget from the worker.
    state.workerHookAfterChargedOwner = state.ownerHookKeptCharge;
    state.workerRetiredStream = state.stream.RetireForWork();
    state.detached = {};
    state.workerDroppedCapture = !state.detached;
}

void RunScenario(Mode mode)
{
    wil::com_ptr_nothrow<ID3D11Device> device;
    wil::com_ptr_nothrow<ID3D11DeviceContext> immediate;
    constexpr D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_0;
    const HRESULT deviceResult =
        D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, &featureLevel, 1,
                          D3D11_SDK_VERSION, device.put(), nullptr, immediate.put());
    Expect(SUCCEEDED(deviceResult) && device && immediate, "actual-warp-device-and-immediate-context");
    if (FAILED(deviceResult) || !device || !immediate)
        return;

    JobScheduler scheduler;
    HRESULT result = static_cast<HRESULT>(scheduler.Initialize(1));
    if (SUCCEEDED(result))
        result = static_cast<HRESULT>(scheduler.EnableGpuRecordingLane());
    Expect(SUCCEEDED(result), "actual-compute-and-recorder-workers");
    if (FAILED(result))
        return;

    OwnerWorkMultiplexer routes;
    GpuWorkQueue queue;
    GpuCommandPump pump;
    const bool routeRegistered = scheduler.RegisterOwnerWork(routes);
    const HRESULT queueInitialized = routeRegistered ? queue.Initialize(scheduler, &routes) : E_FAIL;
    const HRESULT queueBound =
        SUCCEEDED(queueInitialized) ? queue.Bind(device.get(), immediate.get()) : queueInitialized;
    const HRESULT pumpInitialized = SUCCEEDED(queueBound) ? pump.InitializeOwner(scheduler, queue, routes) : queueBound;
    Expect(routeRegistered && SUCCEEDED(pumpInitialized), "bind-real-original-context-and-register-pump");
    if (FAILED(pumpInitialized))
    {
        queue.StopNotifications();
        scheduler.Close();
        queue.CloseAfterWorkers();
        return;
    }

    State state{scheduler, pump, *device.get(), queue.DeviceGeneration(), mode};
    std::unique_ptr<Job> job = std::make_unique<EmptyShutdownJob>(state);
    JobTicket submitted{};
    result = static_cast<HRESULT>(scheduler.Submit(
        job, {.kind = JobClass::Background, .memoryBytes = kReservationBytes, .deferredRetirement = true}, submitted));
    Expect(SUCCEEDED(result) && !job, "one-original-admission-and-reservation");
    if (FAILED(result))
    {
        pump.StopOwner();
        queue.StopNotifications();
        scheduler.Close();
        queue.CloseAfterWorkers();
        return;
    }
    Expect(scheduler.ReservedMemoryBytes() == kReservationBytes,
           "reservation-is-held-while-original-ticket-is-suspended");

    const bool attached = DrainUntil(scheduler, [&]() noexcept { return state.openStageSuspended; });
    Expect(attached && state.openStageSuspended && submitted == state.original,
           "real-stage-acknowledged-suspended-before-source-attach");
    if (!state.openStageSuspended)
    {
        pump.StopOwner();
        queue.StopNotifications();
        scheduler.Close();
        queue.CloseAfterWorkers();
        return;
    }

    if (mode == Mode::EmptyNeverResumed)
    {
        state.preStopEmptyAcknowledgementRefused = !state.stream.AcknowledgeEmptyJoinedShutdownOwner();
        const bool cancelled = scheduler.Cancel(submitted);
        const bool stopped = pump.StopSourceOwner(state.source);
        HRESULT producerResult = E_PENDING;
        state.stoppedWithoutCompletion =
            stopped && !state.stream.ProducerFinishedOwner(producerResult) && !state.stream.CanRetireOwner();
        Expect(cancelled && stopped && state.preStopEmptyAcknowledgementRefused && state.stoppedWithoutCompletion &&
                   state.usefulAcknowledgements == 0,
               "ack-refuses-before-stop-and-cancel-stop-alone-do-not-complete-empty-source");
    }
    else if (mode == Mode::FinishedProducer)
    {
        const bool ran = DrainUntil(scheduler, [&]() noexcept { return state.recorderStageSuspended; });
        HRESULT producerResult = E_PENDING;
        const bool producerFinished = state.stream.ProducerFinishedOwner(producerResult);
        const bool refused = !state.stream.AcknowledgeEmptyJoinedShutdownOwner();
        Expect(ran && state.recorderStageSuspended && producerFinished && producerResult == S_OK && refused,
               "finished-producer-refuses-empty-shutdown-acknowledgement");
    }
    else if (mode == Mode::StartedAfterStop)
    {
        const bool signalled = state.published && WaitForHandle(state.published.get(), kDeadlockBoundMs);
        HRESULT producerResult = E_PENDING;
        const bool stillUnfinished = !state.stream.ProducerFinishedOwner(producerResult);
        const bool refused = !state.stream.AcknowledgeEmptyJoinedShutdownOwner();
        Expect(signalled && state.publishResult.load(std::memory_order_acquire) == E_ABORT && stillUnfinished &&
                   refused && state.nonOwnerAcknowledgementRefused && state.usefulAcknowledgements == 0,
               "wrong-owner-and-started-producer-with-empty-slots-refuse-acknowledgement");
    }
    else
    {
        const bool signalled = state.published && WaitForHandle(state.published.get(), kDeadlockBoundMs);
        const auto ready = state.stream.NextOwner();
        const bool refused = !state.stream.AcknowledgeEmptyJoinedShutdownOwner();
        Expect(signalled && ready.command != nullptr && refused && state.usefulAcknowledgements == 0,
               "nonempty-real-command-refuses-empty-shutdown-acknowledgement");
    }

    pump.StopOwner();
    queue.StopNotifications();
    scheduler.Close({JoinedOwnerHook, JoinedWorkerHook, &state});
    queue.CloseAfterWorkers();

    Expect(state.ownerHookKeptCharge && state.workerHookAfterChargedOwner,
           "original-charge-observed-after-producer-join-before-final-worker-retirement");
    Expect(state.detachedInJoinedHook && state.sourceLeaseClearedAfterDetach && state.authorizedInJoinedHook &&
               state.pumpClosedInJoinedHook,
           "joined-hook-detaches-clears-lease-authorizes-and-closes-pump");
    Expect(state.ownerCannotRetireStream && state.workerRetiredStream && state.workerDroppedCapture &&
               state.retiredOnMta && state.workerThread != state.ownerThread,
           "stream-and-captured-context-retire-only-on-final-mta-worker");
    Expect(scheduler.ReservedMemoryBytes() == 0 && state.usefulAcknowledgements == 0 && state.failureCallbacks == 0,
           "final-retirement-refunds-charge-without-useful-shutdown-callback");
    if (mode == Mode::EmptyNeverResumed)
        Expect(state.resumeCount == 0 && state.emptyAcknowledgedInJoinedHook,
               "empty-source-acknowledged-only-after-actual-join-before-any-recorder-resume");
    else
        Expect(state.resumeCount == 1 && !state.emptyAcknowledgedInJoinedHook,
               "nonempty-shutdown-retains-producer-terminal-state");
}
} // namespace

int RunEmptyJoinedShutdownTests() noexcept
{
    gFailures = 0;
    RunScenario(Mode::EmptyNeverResumed);
    RunScenario(Mode::FinishedProducer);
    RunScenario(Mode::StartedAfterStop);
    RunScenario(Mode::PublishedCommand);
    return gFailures;
}
