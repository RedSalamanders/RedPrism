#include "Composition.Tests.RecordedParentCancellation.h"

#include "DeferredRelease.h"
#include "GpuCommandPump.h"
#include "GpuWork.h"
#include "OwnerWorkMultiplexer.h"
#include "RecordedDocumentReadbackJob.h"
#include "SharedPublicationMemory.h"
#include "UtilityReadbackAdmission.h"
#include "UtilityReadbackOwnerService.h"

#include <Windows.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <memory>
#include <objbase.h>
#include <type_traits>
#include <utility>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/com.h>
#include <wil/resource.h>
#pragma warning(pop)

namespace RedPrism::CompositionTests
{
namespace
{
constexpr DWORD kFailureBoundMs = 10000;
constexpr int kCanvasSide = 1;
unsigned gPassed = 0;
unsigned gFailed = 0;

void Expect(bool condition, const char* name) noexcept
{
    std::printf("%s recorded-parent-cancellation.%s\n", condition ? "PASS" : "FAIL", name);
    gPassed += condition ? 1u : 0u;
    gFailed += condition ? 0u : 1u;
}

bool InMta() noexcept
{
    APTTYPE type = APTTYPE_CURRENT;
    APTTYPEQUALIFIER qualifier = APTTYPEQUALIFIER_NONE;
    return SUCCEEDED(CoGetApartmentType(&type, &qualifier)) && type == APTTYPE_MTA;
}

bool Memory(void*, MemorySnapshot& snapshot) noexcept
{
    snapshot = {4ull << 30, 4ull << 30};
    return true;
}

struct Observation final
{
    Observation() noexcept
    {
        if (FAILED(entered.create(wil::EventOptions::ManualReset)) ||
            FAILED(release.create(wil::EventOptions::ManualReset)))
            return;
        eventsReady = true;
    }

    const DWORD ownerThread = GetCurrentThreadId();
    wil::unique_event_nothrow entered;
    wil::unique_event_nothrow release;
    bool eventsReady = false;
    std::atomic<unsigned> bodyEntries{0};
    std::atomic<unsigned> processEntries{0};
    std::atomic<bool> sameOriginal{true};
    std::atomic<std::size_t> firstSlot{0};
    std::atomic<std::uint64_t> firstGeneration{0};
    std::atomic<bool> openingCompute{false};
    unsigned captureCalls = 0;
    HRESULT captureResult = E_PENDING;
    bool captureOnOwner = false;
    bool captureAdmitted = false;
    unsigned detachedCaptureCalls = 0;
    bool processInActualComputeBody = false;
    bool processInMta = false;
    DWORD processThread = 0;
    bool processReleasedByTest = false;
    bool processObservedActualCancellation = false;
    bool enteredEventSignaled = false;
    bool releaseEventSignaled = false;
    bool completedOnOwner = false;
    bool completedInsideUiCallback = false;
    bool completedExactlyOnce = false;
    unsigned completedCalls = 0;
    JobResult terminal;
    bool terminalOutputEmpty = false;
    bool terminalMemoryBoundToOriginal = false;
    std::atomic<bool> terminalCallbackEntered{false};
    std::atomic<bool> aggregateDestroyed{false};
    std::atomic<bool> aggregateDestroyedOffOwner{false};
    std::atomic<bool> aggregateDestroyedInMta{false};
    std::atomic<bool> aggregateSawTerminal{false};
    std::atomic<bool> aggregateMemoryChargeIntact{false};
};

struct ActualBody final
{
    Observation* observation = nullptr;
    JobTicket ticket{};
    JobExecutionLane lane = JobExecutionLane::Count;
};
thread_local ActualBody gActualBody;

void ObserveExecution(void* opaque, JobExecutionNotice notice) noexcept
{
    auto& observation = *static_cast<Observation*>(opaque);
    if (notice.boundary == JobExecutionBoundary::BodyEntered)
    {
        const auto previous = observation.bodyEntries.fetch_add(1, std::memory_order_relaxed);
        if (previous == 0)
        {
            observation.firstSlot.store(notice.ticket.slot, std::memory_order_relaxed);
            observation.firstGeneration.store(notice.ticket.generation, std::memory_order_relaxed);
            observation.openingCompute.store(notice.lane == JobExecutionLane::Compute, std::memory_order_relaxed);
        }
        else if (notice.ticket.slot != observation.firstSlot.load(std::memory_order_relaxed) ||
                 notice.ticket.generation != observation.firstGeneration.load(std::memory_order_relaxed))
            observation.sameOriginal.store(false, std::memory_order_relaxed);
        gActualBody = {&observation, notice.ticket, notice.lane};
    }
    else if (notice.boundary == JobExecutionBoundary::BodyExited)
        gActualBody = {};
}

#pragma warning(push)
#pragma warning(disable : 4324)
struct Retirement final : DeferredReleaseNode
{
    Retirement(Observation& value, std::uint64_t expected) noexcept : observation(value), expectedCpuBytes(expected)
    {
        bytes = expected;
        destroy = [](DeferredReleaseNode* node) noexcept
        {
            auto* self = static_cast<Retirement*>(node);
            auto& state = self->observation;
            state.aggregateDestroyedOffOwner.store(GetCurrentThreadId() != state.ownerThread,
                                                   std::memory_order_relaxed);
            state.aggregateDestroyedInMta.store(InMta(), std::memory_order_relaxed);
            state.aggregateSawTerminal.store(state.terminalCallbackEntered.load(std::memory_order_acquire),
                                             std::memory_order_relaxed);
            state.aggregateMemoryChargeIntact.store(self->memory && self->memory->IsBound() &&
                                                        self->memory->Bytes() == self->expectedCpuBytes &&
                                                        self->memory->ResidentBytes() == self->expectedCpuBytes,
                                                    std::memory_order_relaxed);
            delete self;
            state.aggregateDestroyed.store(true, std::memory_order_release);
        };
    }

    Observation& observation;
    const std::uint64_t expectedCpuBytes;
    // The shared accounting owner is declared before every retained CPU owner and therefore dies last.
    std::shared_ptr<SharedPublicationMemory> memory;
    CanvasDocument document;
    PixelBuffer pixels;
    Raster output;
};
#pragma warning(pop)

class CancellationProbe final : public RecordedDocumentReadbackJob
{
  public:
    CancellationProbe(JobScheduler& jobs, GpuWorkQueue& queue, GpuCommandPump& commands,
                      UtilityReadbackOwnerService& owners, GpuResidencyBudget& budget,
                      UtilityReadbackAdmission::Envelope envelope, Observation& observation, JobTicket& original)
        : RecordedDocumentReadbackJob(jobs, queue, commands, owners, budget, envelope, {},
                                      D3D11_BOX{0, 0, 0, kCanvasSide, kCanvasSide, 1}, true,
                                      RecordedReadbackCaptureKind::CpuOnly),
          _jobs(jobs), _observation(observation), _original(original),
          _aggregate(std::make_unique<Retirement>(observation, envelope.cpuBytes)), _expectedCpuBytes(envelope.cpuBytes)
    {
    }

    ~CancellationProbe() override
    {
        if (!_aggregate)
            return;
        TransferReadbackOwners(_aggregate->document, _aggregate->pixels, _aggregate->output, _aggregate->memory);
        if (!TryDeferRelease(*_aggregate, DeferredReleasePolicy::AlwaysOnOwner))
            std::terminate();
        (void)_aggregate.release();
    }

  protected:
    HRESULT CaptureReadbackDocument(CanvasDocument&) noexcept override
    {
        ++_observation.captureCalls;
        _observation.captureOnOwner =
            GetCurrentThreadId() == _observation.ownerThread && gActualBody.observation == nullptr;
        _observation.captureAdmitted = _jobs.ReservedMemoryBytes() == _expectedCpuBytes;
        _observation.captureResult = S_FALSE;
        return S_FALSE; // CPU-only detached continuation: no captured document, GPU source, or allocation.
    }

    bool ProcessReadbackDetached() const noexcept override
    {
        return true;
    }

    HRESULT CaptureDetachedReadback(Raster&) noexcept override
    {
        ++_observation.detachedCaptureCalls;
        return E_UNEXPECTED; // This regression must never enter an owner capture or construct useful output.
    }

    JobResult ProcessReadback(Raster&, JobContext& context) override
    {
        ++_observation.processEntries;
        _observation.processThread = GetCurrentThreadId();
        _observation.processInActualComputeBody = gActualBody.observation == &_observation &&
                                                  gActualBody.ticket == _original &&
                                                  gActualBody.lane == JobExecutionLane::Compute;
        _observation.processInMta = InMta();
        if (!_observation.processInActualComputeBody || !_observation.processInMta ||
            _observation.processThread == _observation.ownerThread)
            return {JobStatus::Failed, E_UNEXPECTED};

        _observation.enteredEventSignaled = SetEvent(_observation.entered.get()) != FALSE;
        if (!_observation.enteredEventSignaled)
            return {JobStatus::Failed, HRESULT_FROM_WIN32(GetLastError())};
        const DWORD waited = WaitForSingleObject(_observation.release.get(), kFailureBoundMs);
        _observation.processReleasedByTest = waited == WAIT_OBJECT_0;
        _observation.processObservedActualCancellation =
            _observation.processReleasedByTest && context.cancellation.IsCancelled();
        if (!_observation.processReleasedByTest)
            return {JobStatus::Failed, HRESULT_FROM_WIN32(ERROR_TIMEOUT)};
        return context.cancellation.IsCancelled() ? JobResult{JobStatus::Cancelled} : JobResult{};
    }

    HRESULT PrepareReadbackDevice(HRESULT) noexcept override
    {
        return E_UNEXPECTED; // Explicit CpuOnly/S_FALSE path must not prepare a graphics device.
    }

    void ReadbackCompleted(JobResult result, Raster image, std::uint64_t) noexcept override
    {
        ++_observation.completedCalls;
        _observation.terminal = result;
        _observation.completedOnOwner =
            GetCurrentThreadId() == _observation.ownerThread && gActualBody.observation == nullptr;
        _observation.completedInsideUiCallback = _jobs.InUiCallback();
        _observation.terminalOutputEmpty = !image.HasStorage();
        _observation.terminalCallbackEntered.store(true, std::memory_order_release);
        TransferReadbackOwners(_aggregate->document, _aggregate->pixels, _aggregate->output, _aggregate->memory);
        _observation.terminalMemoryBoundToOriginal =
            _aggregate->memory && _aggregate->memory->BoundTo(_jobs, _original);
        if (!TryDeferRelease(*_aggregate, DeferredReleasePolicy::AlwaysOnOwner))
            std::terminate();
        (void)_aggregate.release();
        _observation.completedExactlyOnce = _observation.completedCalls == 1;
    }

  private:
    JobScheduler& _jobs;
    Observation& _observation;
    JobTicket& _original;
    std::unique_ptr<Retirement> _aggregate;
    const std::uint64_t _expectedCpuBytes;
};

struct StartupPrefix final
{
    bool jobsInitialized = false;
    bool releaseEnabled = false;
    bool recordersEnabled = false;
    bool routesRegistered = false;
    bool queueAttempted = false;
    bool queueInitialized = false;
    bool queueBound = false;
    bool commandsInitialized = false;
    bool serviceInitialized = false;
};

bool StartInfrastructure(JobScheduler& jobs, OwnerWorkMultiplexer& routes, GpuWorkQueue& queue,
                         GpuCommandPump& commands, UtilityReadbackOwnerService& owners, ID3D11Device* device,
                         ID3D11DeviceContext* context, Observation& observation, StartupPrefix& prefix) noexcept
{
    prefix.jobsInitialized = SUCCEEDED(jobs.Initialize(1, {}, {Memory, nullptr}, {ObserveExecution, &observation}));
    Expect(prefix.jobsInitialized, "startup-actual-scheduler");
    if (!prefix.jobsInitialized)
        return false;
    prefix.releaseEnabled = jobs.EnableDeferredRelease();
    Expect(prefix.releaseEnabled, "startup-actual-deferred-release-sink");
    if (!prefix.releaseEnabled)
        return false;
    prefix.recordersEnabled = SUCCEEDED(jobs.EnableGpuRecordingLane());
    Expect(prefix.recordersEnabled, "startup-actual-recording-lane");
    if (!prefix.recordersEnabled)
        return false;
    prefix.routesRegistered = jobs.RegisterOwnerWork(routes);
    Expect(prefix.routesRegistered, "startup-actual-owner-router");
    if (!prefix.routesRegistered)
        return false;
    prefix.queueAttempted = true;
    prefix.queueInitialized = SUCCEEDED(queue.Initialize(jobs, &routes));
    Expect(prefix.queueInitialized, "startup-actual-gpu-queue");
    if (!prefix.queueInitialized)
        return false;
    prefix.queueBound = SUCCEEDED(queue.Bind(device, context));
    Expect(prefix.queueBound, "startup-actual-queue-warp-setup");
    if (!prefix.queueBound)
        return false;
    prefix.commandsInitialized = SUCCEEDED(commands.InitializeOwner(jobs, queue, routes));
    Expect(prefix.commandsInitialized, "startup-actual-command-pump");
    if (!prefix.commandsInitialized)
        return false;
    prefix.serviceInitialized = SUCCEEDED(owners.InitializeOwner(jobs, routes));
    Expect(prefix.serviceInitialized, "startup-actual-readback-owner-service");
    return prefix.serviceInitialized;
}

struct CloseContext final
{
    JobScheduler& jobs;
    GpuWorkQueue& queue;
    GpuCommandPump& commands;
    UtilityReadbackOwnerService& owners;
    OwnerWorkMultiplexer& routes;
    GpuResidencyBudget& budget;
    const StartupPrefix& prefix;
    const DWORD ownerThread;
    DWORD joinedOwnerThread = 0;
    DWORD finalWorkerThread = 0;
    bool joinedOwnerHook = false;
    bool joinedAcknowledgment = false;
    bool queueClosedInOwnerHook = false;
    bool commandsClosedInOwnerHook = false;
    bool finalWorkerHook = false;
    bool finalWorkerMta = false;
    bool workerSawClosedOwnerInfrastructure = false;
    std::atomic<unsigned> closeSequence{0};
};

void JoinedOwnerClose(void* opaque) noexcept
{
    auto& state = *static_cast<CloseContext*>(opaque);
    state.joinedOwnerThread = GetCurrentThreadId();
    state.joinedOwnerHook = state.joinedOwnerThread == state.ownerThread;
    if (state.prefix.commandsInitialized)
        state.commands.AcknowledgeJoinedShutdownOwner();
    state.joinedAcknowledgment = true;
    state.closeSequence.store(1, std::memory_order_release);
    if (state.prefix.queueAttempted)
        state.queue.CloseAfterWorkers();
    state.queueClosedInOwnerHook = !state.queue.HasPending() && !state.queue.HasReady() &&
                                   state.queue.InFlightCount() == 0 && state.queue.LentCount() == 0 &&
                                   state.queue.StagingBytes() == 0 && state.queue.DeviceGeneration() == 0;
    state.closeSequence.store(2, std::memory_order_release);
    state.commandsClosedInOwnerHook = !state.prefix.commandsInitialized || state.commands.CloseOwner();
    state.closeSequence.store(3, std::memory_order_release);
}

void FinalWorkerClose(void* opaque) noexcept
{
    auto& state = *static_cast<CloseContext*>(opaque);
    state.finalWorkerThread = GetCurrentThreadId();
    state.finalWorkerHook = true;
    state.finalWorkerMta = InMta();
    state.workerSawClosedOwnerInfrastructure = state.closeSequence.load(std::memory_order_acquire) == 3 &&
                                               state.joinedOwnerHook && state.joinedAcknowledgment &&
                                               state.queueClosedInOwnerHook && state.commandsClosedInOwnerHook;
}

void CloseInfrastructure(CloseContext& state) noexcept
{
    if (state.prefix.serviceInitialized)
        state.owners.StopOwner();
    if (state.prefix.commandsInitialized)
        state.commands.StopOwner();
    if (state.prefix.queueAttempted)
        state.queue.StopNotifications();
    if (state.prefix.jobsInitialized)
        state.jobs.Close({JoinedOwnerClose, FinalWorkerClose, &state});
    const bool chargesReleased = !state.prefix.serviceInitialized || state.owners.ReleaseJoinedChargesOwner();
    const bool serviceClosed = !state.prefix.serviceInitialized || state.owners.CloseOwner();

    Expect(!state.prefix.jobsInitialized || (state.joinedOwnerHook && state.joinedAcknowledgment &&
                                             state.queueClosedInOwnerHook && state.commandsClosedInOwnerHook),
           "close-joined-owner-ack-queue-command-pump-order");
    Expect(!state.prefix.jobsInitialized ||
               (state.finalWorkerHook && state.finalWorkerMta && state.finalWorkerThread != state.ownerThread &&
                state.workerSawClosedOwnerInfrastructure),
           "close-final-mta-callback-after-owner-infrastructure-close");
    Expect(chargesReleased, "close-service-charges-after-jobs-close");
    Expect(serviceClosed, "close-owner-service-after-jobs-close");
    Expect(!state.routes.HasPending() && !state.routes.HasReady(), "close-owner-router-empty");
    Expect(!state.jobs.WakeHandle() && state.jobs.ReservedMemoryBytes() == 0 && state.budget.ReservedOwner() == 0 &&
               state.budget.ActiveOwner() == 0,
           "close-cpu-and-gpu-accounting-released");
    if ((state.prefix.jobsInitialized && !state.commandsClosedInOwnerHook) || !chargesReleased || !serviceClosed)
    {
        std::fflush(stdout);
        std::terminate();
    }
}

DWORD Remaining(ULONGLONG deadline) noexcept
{
    const auto now = GetTickCount64();
    if (now >= deadline)
        return 0;
    return static_cast<DWORD>((std::min)(deadline - now, static_cast<ULONGLONG>(MAXDWORD)));
}

bool WaitForEntered(JobScheduler& jobs, OwnerWorkMultiplexer& routes, Observation& observation) noexcept
{
    const ULONGLONG deadline = GetTickCount64() + kFailureBoundMs;
    HANDLE handles[2] = {observation.entered.get(), static_cast<HANDLE>(jobs.WakeHandle())};
    if (!handles[0] || !handles[1])
        return false;
    for (;;)
    {
        if (WaitForSingleObject(handles[0], 0) == WAIT_OBJECT_0)
            return true;
        if (Remaining(deadline) == 0)
            return false;
        if (jobs.HasUiWork() || routes.HasReady())
        {
            (void)DrainCompletions(jobs);
            if (WaitForSingleObject(handles[0], 0) == WAIT_OBJECT_0)
                return true;
            if (Remaining(deadline) == 0)
                return false; // Check the deadline before each ready-work continuation.
            continue;
        }
        const DWORD waited = WaitForMultipleObjects(2, handles, FALSE, Remaining(deadline));
        if (waited == WAIT_OBJECT_0)
            return true;
        if (waited != WAIT_OBJECT_0 + 1 && waited != WAIT_TIMEOUT)
            return false;
        if (waited == WAIT_TIMEOUT)
            return false;
        (void)DrainCompletions(jobs);
        if (WaitForSingleObject(handles[0], 0) == WAIT_OBJECT_0)
            return true;
        if (Remaining(deadline) == 0)
            return false;
    }
}

bool WaitForTerminal(JobScheduler& jobs, OwnerWorkMultiplexer& routes, const Observation& observation) noexcept
{
    const ULONGLONG deadline = GetTickCount64() + kFailureBoundMs;
    while (observation.completedCalls == 0)
    {
        if (Remaining(deadline) == 0)
            return false;
        (void)DrainCompletions(jobs);
        if (observation.completedCalls != 0)
            return true;
        if (Remaining(deadline) == 0)
            return false;
        if (jobs.HasUiWork() || routes.HasReady())
        {
            if (Remaining(deadline) == 0)
                return false; // Check the deadline before each ready-work continuation.
            continue;
        }
        const HANDLE wake = static_cast<HANDLE>(jobs.WakeHandle());
        if (!wake)
            return false;
        const DWORD waited = WaitForSingleObject(wake, Remaining(deadline));
        if (waited != WAIT_OBJECT_0)
            return false;
    }
    return true;
}

void RunRegression()
{
    wil::com_ptr_nothrow<ID3D11Device> device;
    wil::com_ptr_nothrow<ID3D11DeviceContext> context;
    constexpr D3D_FEATURE_LEVEL requestedLevel = D3D_FEATURE_LEVEL_11_0;
    const HRESULT deviceResult =
        D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, &requestedLevel, 1,
                          D3D11_SDK_VERSION, device.put(), nullptr, context.put());
    Expect(SUCCEEDED(deviceResult), "setup-actual-warp-device-for-queue-only");
    if (FAILED(deviceResult))
        return;

    Observation observation;
    Expect(observation.eventsReady, "setup-entered-and-release-events");
    if (!observation.eventsReady)
        return;

    JobScheduler jobs;
    OwnerWorkMultiplexer routes;
    GpuWorkQueue queue;
    GpuCommandPump commands;
    UtilityReadbackOwnerService owners;
    GpuResidencyBudget budget;
    StartupPrefix prefix;
    CloseContext close{jobs, queue, commands, owners, routes, budget, prefix, observation.ownerThread};
    if (!StartInfrastructure(jobs, routes, queue, commands, owners, device.get(), context.get(), observation, prefix))
    {
        CloseInfrastructure(close);
        return;
    }

    // A valid one-layer immutable descriptor drives the actual CPU admission estimate. Capture itself returns
    // S_FALSE, so this layer is never captured by the job and no GPU resource is made from it.
    CanvasDocument source;
    source.id = Guid::Generate();
    source.width = kCanvasSide;
    source.height = kCanvasSide;
    auto layer = ImageLayer::Blank(L"Cancellation fixture", {kCanvasSide, kCanvasSide});
    layer.transform.origin = {0, 0};
    layer.transform.size = {kCanvasSide, kCanvasSide};
    layer.transform.rotation = 0;
    layer.transform.flipX = false;
    layer.transform.flipY = false;
    layer.transform.sampling = LayerSampling::Nearest;
    layer.opacity = 1;
    layer.blendMode = LayerBlendMode::Normal;
    constexpr std::array<std::uint8_t, 4> sourcePixel{17, 29, 43, 255};
    layer.asset = RasterAsset{
        Raster::Make(kCanvasSide, kCanvasSide, RasterFormat::Bgra8888, sourcePixel.data(), sourcePixel.size()),
        {},
        L"Cancellation fixture"};
    RecordRoot<ImageLayer>::Builder layerBuilder(1);
    if (!layerBuilder.Append(std::move(layer)))
        std::terminate();
    auto layerRoot = layerBuilder.Seal();
    if (!layerRoot)
        std::terminate();
    source.layers = std::move(layerRoot.value());

    const auto outputBytes = RecordedDocumentReadbackJob::RequiredMemory({0, 0, 0, 1, 1, 1});
    const auto envelope = UtilityReadbackAdmission::Estimate(source, 1, outputBytes + sizeof(Retirement), true);
    Expect(envelope.has_value() && envelope->cpuBytes != 0 && envelope->tileSide == 1,
           "admission-actual-source-and-preallocated-retirement-aggregate");
    if (!envelope)
    {
        CloseInfrastructure(close);
        return;
    }
    Expect(budget.ReservedOwner() == 0, "cpu-only-case-starts-without-gpu-charge");

    JobTicket ticket{};
    std::unique_ptr<Job> pending =
        std::make_unique<CancellationProbe>(jobs, queue, commands, owners, budget, *envelope, observation, ticket);
    JobOptions options;
    options.kind = JobClass::Background;
    options.memoryBytes = envelope->cpuBytes;
    options.deferredRetirement = true;
    options.reserveRetainedMemory = true;
    options.allowMemoryWaiting = false;
    options.executionLane = JobExecutionLane::Compute;
    const HRESULT submitted = jobs.Submit(pending, options, ticket);
    Expect(SUCCEEDED(submitted) && !pending && ticket.generation != 0,
           "one-actual-submit-with-original-retained-slot-and-no-memory-wait");
    Expect(SUCCEEDED(submitted) && jobs.ReservedMemoryBytes() == envelope->cpuBytes,
           "original-complete-cpu-envelope-held-after-submit");
    if (FAILED(submitted))
    {
        pending.reset();
        CloseInfrastructure(close);
        return;
    }

    const bool entered = WaitForEntered(jobs, routes, observation);
    Expect(entered, "bounded-event-wait-for-actual-detached-compute-entry");
    if (!entered)
    {
        (void)jobs.Cancel(ticket);
        SetEvent(observation.release.get());
        CloseInfrastructure(close);
        return;
    }

    Expect(observation.captureCalls == 1 && observation.captureResult == S_FALSE && observation.captureOnOwner &&
               observation.captureAdmitted && observation.detachedCaptureCalls == 0,
           "s-false-capture-is-owner-only-and-never-allocates");
    Expect(observation.processEntries.load(std::memory_order_relaxed) == 1 && observation.processInActualComputeBody &&
               observation.processInMta && observation.processThread != observation.ownerThread &&
               observation.openingCompute.load(std::memory_order_relaxed),
           "process-is-blocked-inside-actual-original-compute-body");

    const bool cancelled = jobs.Cancel(ticket);
    Expect(cancelled, "cancel-original-ticket-while-process-is-blocked");
    const std::size_t oneBlockedDrain = DrainCompletions(jobs); // Exactly one owner drain in the blocked window.
    const bool routeReadyWhileBlocked = routes.HasReady();
    const bool parentReadyWhileBlocked = owners.HasReady();
    const auto schedulerReadyWhileBlocked = jobs.UiReadinessForTest();
    const auto reservedWhileBlocked = jobs.ReservedMemoryBytes();
    const auto gpuWhileBlocked = queue.Statistics();
    Expect(oneBlockedDrain == 0 && observation.completedCalls == 0, "one-blocked-drain-delivers-no-terminal-callback");
    Expect(!routeReadyWhileBlocked && !parentReadyWhileBlocked && !schedulerReadyWhileBlocked.owner &&
               !schedulerReadyWhileBlocked.resumes,
           "cancelled-running-body-has-no-owner-readiness-or-pending-resume");
    Expect(reservedWhileBlocked == envelope->cpuBytes,
           "original-retained-slot-and-complete-reservation-stay-held-during-body");
    Expect(observation.enteredEventSignaled && observation.processEntries.load(std::memory_order_relaxed) == 1 &&
               WaitForSingleObject(observation.entered.get(), 0) == WAIT_OBJECT_0,
           "blocked-body-has-not-been-resumed-or-dispatched-twice");
    Expect(budget.ReservedOwner() == 0 && commands.AttachedSourceCountOwnerForTest() == 0 && !commands.HasPending() &&
               !queue.HasPending() && !queue.HasReady() && gpuWhileBlocked.copies == 0 && gpuWhileBlocked.maps == 0 &&
               gpuWhileBlocked.readinessBarriers == 0,
           "blocked-cpu-only-route-has-no-gpu-source-copy-map-or-barrier");

    observation.releaseEventSignaled = SetEvent(observation.release.get()) != FALSE;
    Expect(observation.releaseEventSignaled, "test-release-event-signaled-after-blocked-observations");
    const bool terminal = WaitForTerminal(jobs, routes, observation);
    Expect(terminal, "bounded-event-driven-settle-after-worker-release");
    if (terminal)
    {
        Expect(observation.completedCalls == 1 && observation.completedExactlyOnce &&
                   observation.terminal.status == JobStatus::Cancelled && SUCCEEDED(observation.terminal.error),
               "original-terminal-cancelled-exactly-once-on-owner");
        Expect(observation.completedOnOwner && observation.completedInsideUiCallback,
               "terminal-callback-runs-on-owner-inside-real-scheduler-callback");
        Expect(observation.sameOriginal.load(std::memory_order_relaxed) &&
                   observation.firstSlot.load(std::memory_order_relaxed) == ticket.slot &&
                   observation.firstGeneration.load(std::memory_order_relaxed) == ticket.generation,
               "every-observed-body-keeps-the-original-ticket");
        Expect(observation.processReleasedByTest && observation.processObservedActualCancellation,
               "process-returns-cancelled-from-actual-token-after-test-release");
        Expect(observation.terminalOutputEmpty, "cancelled-terminal-has-no-useful-output");
        Expect(observation.detachedCaptureCalls == 0, "cancelled-path-never-calls-detached-capture");
        const auto after = queue.Statistics();
        Expect(after.copies == 0 && after.maps == 0 && after.readinessBarriers == 0 &&
                   observation.bodyEntries.load(std::memory_order_relaxed) == 2 &&
                   commands.AttachedSourceCountOwnerForTest() == 0 && !commands.HasPending() && !queue.HasPending(),
               "cpu-only-cancellation-has-zero-gpu-work-and-only-opening-plus-process-bodies");
        Expect(!routes.HasReady() && !routes.HasPending(), "terminal-owner-routes-are-settled");
    }

    CloseInfrastructure(close);
    Expect(observation.aggregateDestroyed.load(std::memory_order_acquire) &&
               observation.aggregateDestroyedOffOwner.load(std::memory_order_relaxed) &&
               observation.aggregateDestroyedInMta.load(std::memory_order_relaxed),
           "transferred-aggregate-is-finally-destroyed-on-mta-worker");
    Expect(observation.aggregateSawTerminal.load(std::memory_order_relaxed) &&
               observation.terminalMemoryBoundToOriginal &&
               observation.aggregateMemoryChargeIntact.load(std::memory_order_relaxed),
           "same-original-bound-memory-and-terminal-state-survive-to-final-destruction");
}
} // namespace

int RunRecordedParentCancellationTests()
{
    gPassed = gFailed = 0;
    RunRegression();
    std::printf("SUMMARY recorded-parent-cancellation passed=%u failed=%u\n", gPassed, gFailed);
    return gFailed == 0 ? 0 : 1;
}
} // namespace RedPrism::CompositionTests
