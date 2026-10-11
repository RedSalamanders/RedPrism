#include "Composition.Tests.RecordedParentSampler.h"

#include "DeferredRelease.h"
#include "GpuCapabilities.h"
#include "GpuCommandPump.h"
#include "GpuResidencyBudget.h"
#include "GpuWork.h"
#include "OwnerWorkMultiplexer.h"
#include "RecordedDocumentReadbackJob.h"
#include "SharedPublicationMemory.h"
#include "UtilityReadbackOwnerService.h"

#include <Windows.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <memory>
#include <new>
#include <objbase.h>
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
constexpr DWORD kBoundMs = 10000;
constexpr std::uint32_t kTileSide = 1;
constexpr D3D11_BOX kFirstPixel{0, 0, 0, 1, 1, 1};
// Literal source bytes are independent of the composition and of the expected sample table.
constexpr std::array<std::uint8_t, 16> kSourceBgra{{
    0x11,
    0x22,
    0x33,
    0xFF,
    0x44,
    0x55,
    0x66,
    0xFF,
    0x77,
    0x88,
    0x99,
    0xFF,
    0xAA,
    0xBB,
    0xCC,
    0xFF,
}};
constexpr std::array<std::array<std::uint8_t, 4>, 4> kExpectedSamples{{
    {{0x11, 0x22, 0x33, 0xFF}},
    {{0x44, 0x55, 0x66, 0xFF}},
    {{0x77, 0x88, 0x99, 0xFF}},
    {{0xAA, 0xBB, 0xCC, 0xFF}},
}};
constexpr std::array<POINT, 4> kPoints{{{0, 0}, {1, 0}, {0, 1}, {1, 1}}};
const char* gCaseName = "unset";
unsigned gPassed = 0;
unsigned gFailed = 0;

void Expect(bool value, const char* name) noexcept
{
    std::printf("%s recorded-parent-sampler.%s.%s\n", value ? "PASS" : "FAIL", gCaseName, name);
    gPassed += value ? 1u : 0u;
    gFailed += value ? 0u : 1u;
}

bool InMta() noexcept
{
    APTTYPE type = APTTYPE_CURRENT;
    APTTYPEQUALIFIER qualifier = APTTYPEQUALIFIER_NONE;
    return SUCCEEDED(CoGetApartmentType(&type, &qualifier)) && type == APTTYPE_MTA;
}

CanvasDocument MakeSource()
{
    Raster pixels = Raster::Make(2, 2, RasterFormat::Bgra8888, kSourceBgra.data(), kSourceBgra.size());
    CanvasDocument document;
    document.id = Guid::Generate();
    document.width = 2;
    document.height = 2;
    auto layer = ImageLayer::Blank(L"Recorded parent sampler fixture", {2, 2});
    layer.transform.origin = {0, 0};
    layer.transform.size = {2, 2};
    layer.transform.rotation = 0;
    layer.transform.flipX = false;
    layer.transform.flipY = false;
    layer.transform.sampling = LayerSampling::Nearest;
    layer.opacity = 1;
    layer.blendMode = LayerBlendMode::Normal;
    layer.asset = RasterAsset{std::move(pixels), {}, L"Recorded parent sampler fixture"};
    RecordRoot<ImageLayer>::Builder layers(1);
    if (!layers.Append(std::move(layer)))
        std::terminate();
    auto root = layers.Seal();
    if (!root)
        std::terminate();
    document.layers = std::move(root.value());
    return document;
}

bool HostMemory(void*, MemorySnapshot& snapshot) noexcept
{
    snapshot = {4ull << 30, 4ull << 30};
    return true;
}

struct Observation final
{
    Observation(bool gateWorker, bool withholdFirst) noexcept : gated(gateWorker), holdFirst(withholdFirst)
    {
        if (FAILED(entered.create(wil::EventOptions::ManualReset)) ||
            FAILED(release.create(wil::EventOptions::ManualReset)))
            return;
        eventsReady = true;
    }
    const DWORD ownerThread = GetCurrentThreadId();
    const bool gated;
    const bool holdFirst;
    wil::unique_event_nothrow entered;
    wil::unique_event_nothrow release;
    bool eventsReady = false;
    unsigned captureCalls = 0;
    bool captureOnOwner = false;
    bool captureRootMatches = false;
    unsigned processCalls = 0;
    unsigned detachedCaptureCalls = 0;
    unsigned detachedCallbacks = 0;
    unsigned publishedSamples = 0;
    unsigned terminalPasses = 0;
    bool literalSamplesMatch = true;
    bool workerWasActualCompute = true;
    bool workerWasMta = true;
    bool gateEntered = false;
    bool gateReleased = false;
    bool gateObservedCancel = false;
    bool completedOnOwner = false;
    unsigned completedCalls = 0;
    JobResult terminal{};
    bool memoryBoundToOriginal = false;
    std::atomic<bool> retired{false};
    std::atomic<bool> retiredOffOwner{false};
    std::atomic<bool> retiredMta{false};
    std::atomic<bool> accountingLiveAtSinkRetirement{false};
};

struct ActiveBody final
{
    Observation* observation = nullptr;
    JobTicket ticket{};
    JobExecutionLane lane = JobExecutionLane::Count;
};
thread_local ActiveBody gActiveBody;

void ObserveExecution(void* opaque, JobExecutionNotice notice) noexcept
{
    auto& observation = *static_cast<Observation*>(opaque);
    if (notice.boundary == JobExecutionBoundary::BodyEntered)
        gActiveBody = {&observation, notice.ticket, notice.lane};
    else if (notice.boundary == JobExecutionBoundary::BodyExited)
        gActiveBody = {};
}

#pragma warning(push)
#pragma warning(disable : 4324)
struct Retirement final : DeferredReleaseNode
{
    Retirement(Observation& state, std::uint64_t bytes) noexcept : observation(state), expectedBytes(bytes)
    {
        this->bytes = bytes;
        destroy = [](DeferredReleaseNode* node) noexcept
        {
            auto* self = static_cast<Retirement*>(node);
            auto& state = self->observation;
            state.retiredOffOwner.store(GetCurrentThreadId() != state.ownerThread, std::memory_order_relaxed);
            state.retiredMta.store(InMta(), std::memory_order_relaxed);
            state.accountingLiveAtSinkRetirement.store(self->memory && self->memory->IsBound() &&
                                                           self->memory->Bytes() == self->expectedBytes &&
                                                           self->memory->ResidentBytes() == self->expectedBytes,
                                                       std::memory_order_relaxed);
            std::unique_ptr<Retirement> owned(self);
            owned.reset();
            state.retired.store(true, std::memory_order_release);
        };
    }
    Observation& observation;
    const std::uint64_t expectedBytes;
    // The accounting owner is declared before every adopted CPU/document owner and dies last.
    std::shared_ptr<SharedPublicationMemory> memory;
    CanvasDocument document;
    PixelBuffer pixels;
    Raster output;
    Raster completedImage;
};
#pragma warning(pop)

class SamplerProbe final : public RecordedDocumentReadbackJob
{
  public:
    SamplerProbe(JobScheduler& jobs, GpuWorkQueue& queue, GpuCommandPump& commands, UtilityReadbackOwnerService& owners,
                 GpuResidencyBudget& budget, UtilityReadbackAdmission::Envelope envelope,
                 GpuResidencyReservation gpuCharge, Observation& observation, const CanvasDocument& source,
                 JobTicket& original)
        : RecordedDocumentReadbackJob(jobs, queue, commands, owners, budget, envelope, std::move(gpuCharge),
                                      kFirstPixel, true),
          _jobs(jobs), _observation(observation), _source(source), _original(original),
          _retirementBytes(envelope.cpuBytes)
    {
    }

    ~SamplerProbe() override
    {
        if (!_retirement)
            return;
        TransferReadbackOwners(_retirement->document, _retirement->pixels, _retirement->output, _retirement->memory);
        if (!TryDeferRelease(*_retirement, DeferredReleasePolicy::AlwaysOnOwner))
            std::terminate();
        (void)_retirement.release();
    }

    [[nodiscard]] bool ContinueAt(JobTicket ticket, unsigned int x, unsigned int y) noexcept
    {
        return ContinuePixelReadback(ticket, x, y);
    }
    [[nodiscard]] bool FinishOriginal(JobTicket ticket) noexcept
    {
        _terminalRequested = true;
        const bool resumed = ResumeDetachedReadback(ticket);
        if (!resumed)
            _terminalRequested = false;
        return resumed;
    }
    [[nodiscard]] unsigned int SampleCount() const noexcept
    {
        return _observation.publishedSamples;
    }

  protected:
    HRESULT CaptureReadbackDocument(CanvasDocument& destination) noexcept override
    {
        ++_observation.captureCalls;
        _observation.captureOnOwner = GetCurrentThreadId() == _observation.ownerThread && !gActiveBody.observation;
        destination = _source;
        _observation.captureRootMatches = destination.layers.Identity() == _source.layers.Identity();
        return S_OK;
    }
    JobResult PrepareReadbackSnapshot(CanvasDocument&, JobContext&) override
    {
        if (!_retirement)
            _retirement = std::make_unique<Retirement>(_observation, _retirementBytes);
        return {};
    }
    [[nodiscard]] bool ProcessReadbackDetached() const noexcept override
    {
        return true;
    }
    [[nodiscard]] HRESULT CaptureDetachedReadback(Raster& image) noexcept override
    {
        ++_observation.detachedCaptureCalls;
        // Owner checks metadata only. Actual mapped pixels are copied/validated by the original Compute body.
        return image.HasStorage() && image.Width() == 1 && image.Height() == 1 &&
                       image.Format() == RasterFormat::Bgra8888
                   ? S_OK
                   : E_UNEXPECTED;
    }
    [[nodiscard]] bool KeepReadbackGraphForPixelContinuation() const noexcept override
    {
        return true;
    }
    JobResult ProcessReadback(Raster& image, JobContext& context) override
    {
        _observation.workerWasActualCompute =
            _observation.workerWasActualCompute && gActiveBody.observation == &_observation &&
            gActiveBody.ticket == _original && gActiveBody.lane == JobExecutionLane::Compute &&
            context.deferredRetirement;
        _observation.workerWasMta =
            _observation.workerWasMta && InMta() && GetCurrentThreadId() != _observation.ownerThread;
        if (_terminalRequested)
        {
            ++_observation.terminalPasses;
            return {};
        }
        ++_observation.processCalls;
        if (context.cancellation.IsCancelled())
            return {JobStatus::Cancelled};
        const auto index = _observation.publishedSamples;
        if (!image.HasStorage() || image.Width() != 1 || image.Height() != 1 ||
            image.Format() != RasterFormat::Bgra8888 || index >= kExpectedSamples.size())
        {
            _observation.literalSamplesMatch = false;
            return {JobStatus::Failed, E_UNEXPECTED};
        }
        const bool matches = std::memcmp(image.Data(), kExpectedSamples[index].data(), 4) == 0;
        _observation.literalSamplesMatch = _observation.literalSamplesMatch && matches;
        if (_observation.gated && !_observation.gateEntered)
        {
            _observation.gateEntered = SetEvent(_observation.entered.get()) != FALSE;
            if (!_observation.gateEntered || WaitForSingleObject(_observation.release.get(), kBoundMs) != WAIT_OBJECT_0)
                return {JobStatus::Failed, HRESULT_FROM_WIN32(ERROR_TIMEOUT)};
            _observation.gateReleased = true;
            _observation.gateObservedCancel = context.cancellation.IsCancelled();
            if (_observation.gateObservedCancel)
                return {JobStatus::Cancelled};
        }
        return matches ? JobResult{} : JobResult{JobStatus::Failed, E_FAIL};
    }
    HRESULT PrepareReadbackDevice(HRESULT reason) noexcept override
    {
        return reason;
    }
    void ReadbackCompleted(JobResult result, Raster image, std::uint64_t) noexcept override
    {
        ++_observation.completedCalls;
        _observation.completedOnOwner = GetCurrentThreadId() == _observation.ownerThread && _jobs.InUiCallback();
        _observation.terminal = result;
        if (_retirement)
        {
            _retirement->completedImage = std::move(image);
            TransferReadbackOwners(_retirement->document, _retirement->pixels, _retirement->output,
                                   _retirement->memory);
            _observation.memoryBoundToOriginal = _retirement->memory && _retirement->memory->BoundTo(_jobs, _original);
            if (!TryDeferRelease(*_retirement, DeferredReleasePolicy::AlwaysOnOwner))
                std::terminate();
            (void)_retirement.release();
        }
    }
    bool DeferDetachedCompletion(JobResult result, JobTicket ticket) noexcept override
    {
        if (_terminalRequested || result.status != JobStatus::Finished)
            return false;
        if (ticket != _original || _observation.publishedSamples >= kExpectedSamples.size())
            return false;
        ++_observation.detachedCallbacks;
        if (_observation.holdFirst && _observation.publishedSamples == 0)
            return true;                 // Leave the real completed pixel pending while the original parent is idle.
        ++_observation.publishedSamples; // Acknowledged actual worker result, not a fabricated GPU acknowledgement.
        return true; // Retain the original detached ticket until test input or an explicit terminal resume.
    }

  private:
    JobScheduler& _jobs;
    Observation& _observation;
    const CanvasDocument& _source;
    JobTicket& _original;
    std::unique_ptr<Retirement> _retirement;
    const std::uint64_t _retirementBytes;
    std::atomic<bool> _terminalRequested{false};
};

struct Startup final
{
    bool jobs = false;
    bool release = false;
    bool recording = false;
    bool routes = false;
    bool queueAttempted = false;
    bool queue = false;
    bool commands = false;
    bool owners = false;
};

struct CloseState final
{
    JobScheduler& jobs;
    GpuWorkQueue& queue;
    GpuCommandPump& commands;
    UtilityReadbackOwnerService& owners;
    OwnerWorkMultiplexer& routes;
    GpuResidencyBudget& budget;
    Startup& started;
    DWORD ownerThread = GetCurrentThreadId();
    bool joinedOwner = false;
    bool queueClosed = false;
    bool commandsClosed = false;
    bool finalWorker = false;
    bool finalMta = false;
    bool finalSawOwnerClose = false;
    std::atomic<unsigned> closeOrder{0};
};

bool Start(JobScheduler& jobs, OwnerWorkMultiplexer& routes, GpuWorkQueue& queue, GpuCommandPump& commands,
           UtilityReadbackOwnerService& owners, ID3D11Device* device, ID3D11DeviceContext* context,
           Observation& observation, Startup& state) noexcept
{
    state.jobs = SUCCEEDED(jobs.Initialize(1, {}, {HostMemory, nullptr}, {ObserveExecution, &observation}));
    if (!state.jobs)
        return false;
    state.release = jobs.EnableDeferredRelease();
    if (!state.release)
        return false;
    state.recording = SUCCEEDED(jobs.EnableGpuRecordingLane());
    if (!state.recording)
        return false;
    state.routes = jobs.RegisterOwnerWork(routes);
    if (!state.routes)
        return false;
    state.queueAttempted = true;
    state.queue = SUCCEEDED(queue.Initialize(jobs, &routes)) && SUCCEEDED(queue.Bind(device, context));
    if (!state.queue)
        return false;
    state.commands = SUCCEEDED(commands.InitializeOwner(jobs, queue, routes));
    if (!state.commands)
        return false;
    state.owners = SUCCEEDED(owners.InitializeOwner(jobs, routes));
    return state.owners;
}

void JoinedOwnerClose(void* opaque) noexcept
{
    auto& state = *static_cast<CloseState*>(opaque);
    state.joinedOwner = GetCurrentThreadId() == state.ownerThread;
    if (state.started.commands)
        state.commands.AcknowledgeJoinedShutdownOwner();
    state.closeOrder.store(1, std::memory_order_release);
    if (state.started.queueAttempted)
        state.queue.CloseAfterWorkers();
    state.queueClosed = !state.queue.HasPending() && !state.queue.HasReady() && state.queue.InFlightCount() == 0 &&
                        state.queue.LentCount() == 0 && state.queue.StagingBytes() == 0 &&
                        state.queue.DeviceGeneration() == 0;
    state.closeOrder.store(2, std::memory_order_release);
    state.commandsClosed = !state.started.commands || state.commands.CloseOwner();
    state.closeOrder.store(3, std::memory_order_release);
}

void FinalWorkerClose(void* opaque) noexcept
{
    auto& state = *static_cast<CloseState*>(opaque);
    state.finalWorker = true;
    state.finalMta = InMta() && GetCurrentThreadId() != state.ownerThread;
    state.finalSawOwnerClose = state.closeOrder.load(std::memory_order_acquire) == 3 && state.joinedOwner &&
                               state.queueClosed && state.commandsClosed;
}

void Close(CloseState& state) noexcept
{
    if (state.started.owners)
        state.owners.StopOwner();
    if (state.started.commands)
        state.commands.StopOwner();
    if (state.started.queueAttempted)
        state.queue.StopNotifications();
    if (state.started.jobs)
        state.jobs.Close({JoinedOwnerClose, FinalWorkerClose, &state});
    const bool charges = !state.started.owners || state.owners.ReleaseJoinedChargesOwner();
    const bool serviceClosed = !state.started.owners || state.owners.CloseOwner();
    Expect(!state.started.jobs || (state.joinedOwner && state.queueClosed && state.commandsClosed),
           "close-joined-worker-then-queue-and-command-owner-hooks");
    Expect(!state.started.jobs || (state.finalWorker && state.finalMta && state.finalSawOwnerClose),
           "close-final-mta-hook-runs-after-owner-close-hooks");
    Expect(charges && serviceClosed, "close-owner-service-and-joined-charges");
    Expect(!state.routes.HasPending() && !state.routes.HasReady(), "close-router-empty");
    Expect(!state.jobs.WakeHandle() && state.jobs.ReservedMemoryBytes() == 0 && state.budget.ReservedOwner() == 0 &&
               state.budget.ActiveOwner() == 0,
           "close-original-cpu-and-gpu-accounting-retired");
    if (!charges || !serviceClosed || (state.started.jobs && !state.commandsClosed))
    {
        std::fflush(stdout);
        std::terminate();
    }
}

DWORD Remaining(ULONGLONG deadline) noexcept
{
    const auto now = GetTickCount64();
    return now >= deadline ? 0 : static_cast<DWORD>((std::min)(deadline - now, ULONGLONG(MAXDWORD)));
}

bool WaitForCount(JobScheduler& jobs, OwnerWorkMultiplexer& routes, const Observation& observation,
                  unsigned target) noexcept
{
    const auto deadline = GetTickCount64() + kBoundMs;
    while (observation.publishedSamples < target && observation.completedCalls == 0)
    {
        if (!Remaining(deadline))
            return false;
        (void)DrainCompletions(jobs);
        if (observation.publishedSamples >= target || observation.completedCalls != 0)
            break;
        if (jobs.HasUiWork() || routes.HasReady())
            continue;
        const HANDLE wake = static_cast<HANDLE>(jobs.WakeHandle());
        if (!wake || WaitForSingleObject(wake, Remaining(deadline)) != WAIT_OBJECT_0)
            return false;
    }
    return observation.publishedSamples >= target;
}

bool WaitForDetachedCallback(JobScheduler& jobs, OwnerWorkMultiplexer& routes, const Observation& observation) noexcept
{
    const auto deadline = GetTickCount64() + kBoundMs;
    while (!observation.detachedCallbacks && !observation.completedCalls)
    {
        if (!Remaining(deadline))
            return false;
        (void)DrainCompletions(jobs);
        if (observation.detachedCallbacks || observation.completedCalls)
            break;
        if (jobs.HasUiWork() || routes.HasReady())
            continue;
        const HANDLE wake = static_cast<HANDLE>(jobs.WakeHandle());
        if (!wake || WaitForSingleObject(wake, Remaining(deadline)) != WAIT_OBJECT_0)
            return false;
    }
    return observation.detachedCallbacks != 0;
}

bool WaitForGate(JobScheduler& jobs, OwnerWorkMultiplexer& routes, Observation& observation) noexcept
{
    const auto deadline = GetTickCount64() + kBoundMs;
    HANDLE handles[2]{observation.entered.get(), static_cast<HANDLE>(jobs.WakeHandle())};
    if (!handles[0] || !handles[1])
        return false;
    while (Remaining(deadline))
    {
        if (WaitForSingleObject(handles[0], 0) == WAIT_OBJECT_0)
            return true;
        if (jobs.HasUiWork() || routes.HasReady())
            (void)DrainCompletions(jobs);
        else
        {
            const DWORD result = WaitForMultipleObjects(2, handles, FALSE, Remaining(deadline));
            if (result == WAIT_OBJECT_0)
                return true;
            if (result != WAIT_OBJECT_0 + 1)
                return false;
            (void)DrainCompletions(jobs);
        }
    }
    return false;
}

bool WaitTerminal(JobScheduler& jobs, OwnerWorkMultiplexer& routes, const Observation& observation) noexcept
{
    const auto deadline = GetTickCount64() + kBoundMs;
    while (!observation.completedCalls)
    {
        if (!Remaining(deadline))
            return false;
        (void)DrainCompletions(jobs);
        if (observation.completedCalls)
            return true;
        if (jobs.HasUiWork() || routes.HasReady())
            continue;
        const HANDLE wake = static_cast<HANDLE>(jobs.WakeHandle());
        if (!wake || WaitForSingleObject(wake, Remaining(deadline)) != WAIT_OBJECT_0)
            return false;
    }
    return true;
}

bool ReserveActualGpu(ID3D11Device* device, bool software, std::uint64_t generation, GpuResidencyBudget& budget,
                      std::uint64_t bytes, GpuResidencyReservation& reservation) noexcept
{
    GpuCapabilities::VideoMemorySnapshot actual{};
    if (FAILED(GpuCapabilities::QueryVideoMemorySnapshot(device, actual)))
        return false;
    const auto& segment =
        software && actual.nonLocal.available && actual.nonLocal.budget != 0 ? actual.nonLocal : actual.local;
    if (!segment.available || !segment.budget)
        return false;
    GpuResidencyAdmission admission = GpuResidencyAdmission::Invalid;
    reservation = budget.ReserveOwner(bytes, {generation, segment.budget, segment.currentUsage}, admission);
    return reservation && admission == GpuResidencyAdmission::Ready && reservation.Bytes() == bytes;
}

void RunScenario(bool cancelIdle, bool cancelGated)
{
    gCaseName = cancelIdle ? "cancel-idle" : cancelGated ? "cancel-gated" : "serial";
    wil::com_ptr_nothrow<ID3D11Device> device;
    wil::com_ptr_nothrow<ID3D11DeviceContext> context;
    constexpr D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
    const HRESULT created = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                                              &level, 1, D3D11_SDK_VERSION, device.put(), nullptr, context.put());
    Expect(SUCCEEDED(created), "warp-real-device-created");
    if (FAILED(created))
        return;

    CanvasDocument source = MakeSource();
    const std::uint64_t consumerBytes = sizeof(Retirement) + sizeof(SamplerProbe) +
                                        SharedPublicationMemory::OwnershipAllowance() +
                                        RecordedDocumentReadbackJob::RequiredMemory(kFirstPixel);
    const auto envelope = UtilityReadbackAdmission::Estimate(source, kTileSide, consumerBytes, true);
    Expect(envelope.has_value() && envelope.value().gpuBytes != 0,
           "real-source-complete-envelope-estimated-before-job-owners");
    if (!envelope)
        return;

    Observation observation(cancelGated, cancelIdle);
    Expect(observation.eventsReady, "bounded-worker-gate-events-created");
    if (!observation.eventsReady)
        return;
    JobScheduler jobs;
    OwnerWorkMultiplexer routes;
    GpuWorkQueue queue;
    GpuCommandPump commands;
    UtilityReadbackOwnerService owners;
    GpuResidencyBudget budget;
    Startup started;
    const bool ready = Start(jobs, routes, queue, commands, owners, device.get(), context.get(), observation, started);
    Expect(ready, "actual-scheduler-release-sink-recorder-warp-queue-command-and-owner-service-started");
    CloseState close{jobs, queue, commands, owners, routes, budget, started};
    if (!ready)
    {
        Close(close);
        return;
    }
    GpuResidencyReservation gpuCharge;
    const bool gpuAdmitted =
        ReserveActualGpu(device.get(), true, queue.DeviceGeneration(), budget, envelope.value().gpuBytes, gpuCharge);
    Expect(gpuAdmitted, "actual-warp-adapter-budget-reserves-complete-graph");
    if (!gpuAdmitted)
    {
        Close(close);
        return;
    }

    JobTicket original{};
    std::unique_ptr<Job> job = std::make_unique<SamplerProbe>(jobs, queue, commands, owners, budget, envelope.value(),
                                                              std::move(gpuCharge), observation, source, original);
    JobOptions options;
    options.kind = JobClass::Selection;
    options.memoryBytes = envelope.value().cpuBytes;
    options.deferredRetirement = true;
    options.reserveRetainedMemory = true;
    options.allowMemoryWaiting = false;
    options.executionLane = JobExecutionLane::Compute;
    auto* const probe = static_cast<SamplerProbe*>(job.get());
    const HRESULT submitted = jobs.Submit(job, options, original);
    Expect(SUCCEEDED(submitted) && !job && jobs.ReservedMemoryBytes() == envelope.value().cpuBytes,
           "one-original-ticket-submitted-with-complete-retained-cpu-admission");
    if (FAILED(submitted))
    {
        job.reset();
        Close(close);
        return;
    }
    const auto initialStats = queue.Statistics();
    if (cancelGated)
    {
        const bool entered = WaitForGate(jobs, routes, observation);
        Expect(entered, "gate-real-detached-compute-worker-after-gpu-map");
        const bool cancelled = jobs.Cancel(original);
        Expect(cancelled, "cancel-original-while-actual-compute-worker-is-held");
        const bool released = SetEvent(observation.release.get()) != FALSE;
        Expect(released, "release-actual-worker-gate");
        Expect(WaitTerminal(jobs, routes, observation), "gated-cancellation-reaches-terminal");
        Expect(observation.gateEntered && observation.gateReleased && observation.gateObservedCancel,
               "actual-worker-wakes-and-observes-original-cancellation");
        const auto gatedStats = queue.Statistics();
        Expect(observation.processCalls == 1 && gatedStats.copies - initialStats.copies == 1 &&
                   gatedStats.maps - initialStats.maps == 1,
               "cancellation-gate-is-after-one-real-copy-map-and-compute-entry");
        Expect(observation.publishedSamples == 0 && observation.terminal.status == JobStatus::Cancelled,
               "cancelled-worker-drops-useful-pixel-without-publication");
        Expect(observation.literalSamplesMatch, "gated-worker-validated-literal-pixel-before-cancellation-dropped-it");
    }
    else
    {
        const bool detached = cancelIdle ? WaitForDetachedCallback(jobs, routes, observation)
                                         : WaitForCount(jobs, routes, observation, 1);
        Expect(detached,
               cancelIdle ? "first-actual-warp-result-held-pending" : "first-actual-warp-sample-acknowledged");
        if (!observation.completedCalls)
        {
            const auto suspendedStats = queue.Statistics();
            Expect(observation.captureCalls == 1 && observation.captureOnOwner && observation.captureRootMatches,
                   "one-owner-capture-of-original-immutable-document");
            Expect(commands.AttachedSourceCountOwnerForTest() == 0 && !commands.HasPending() && !queue.HasPending() &&
                       !queue.HasReady() && !owners.HasReady(),
                   "between-samples-no-attached-source-queued-work-or-ready-owner");
            Expect(jobs.UiReadinessForTest().owner == false,
                   "between-samples-original-job-is-suspended-and-not-owner-ready");

            if (!cancelIdle)
            {
                const JobTicket wrong{original.slot, original.generation + 1};
                const bool wrongRefused = !probe->ContinueAt(wrong, 1, 0);
                const bool outsideRefused = !probe->ContinueAt(original, 2, 0);
                Expect(wrongRefused && outsideRefused && probe->SampleCount() == 1,
                       "wrong-ticket-and-out-of-range-refused-with-first-result-preserved");
                Expect(commands.AttachedSourceCountOwnerForTest() == 0 && !owners.HasReady(),
                       "refusals-leave-original-suspended-and-idle");

                for (unsigned i = 1; i < kPoints.size() && !observation.completedCalls; ++i)
                {
                    const bool resumed = probe->ContinueAt(original, static_cast<unsigned int>(kPoints[i].x),
                                                           static_cast<unsigned int>(kPoints[i].y));
                    Expect(resumed, "continue-next-coordinate-on-original-ticket");
                    if (!resumed || !WaitForCount(jobs, routes, observation, i + 1))
                    {
                        Expect(false, "next-real-copy-map-and-compute-stage-completes");
                        break;
                    }
                    Expect(commands.AttachedSourceCountOwnerForTest() == 0 && !queue.HasPending() &&
                               !queue.HasReady() && !owners.HasReady(),
                           "each-detached-boundary-releases-source-and-copy-work");
                }
                Expect(observation.publishedSamples == kPoints.size() && observation.literalSamplesMatch,
                       "four-serial-one-by-one-gpu-results-match-independent-bgra-literals");
                const auto beforeFinish = queue.Statistics();
                Expect(beforeFinish.copies - initialStats.copies == kPoints.size() &&
                           beforeFinish.maps - initialStats.maps == kPoints.size() &&
                           beforeFinish.copies - suspendedStats.copies == kPoints.size() - 1,
                       "every-sample-used-actual-queued-copy-and-map");
                const bool terminalResume = probe->FinishOriginal(original);
                Expect(terminalResume, "final-original-ticket-resumed-through-resume-detached-readback");
                Expect(WaitTerminal(jobs, routes, observation), "final-original-ticket-completes");
                const auto afterFinish = queue.Statistics();
                Expect(observation.processCalls == kPoints.size() && observation.terminalPasses == 1 &&
                           afterFinish.copies == beforeFinish.copies && afterFinish.maps == beforeFinish.maps,
                       "terminal-resume-completes-without-resampling-or-extra-gpu-copy");
                Expect(observation.terminal.status == JobStatus::Finished && SUCCEEDED(observation.terminal.error),
                       "original-ticket-final-result-is-finished");
            }
            else
            {
                const bool cancelled = jobs.Cancel(original);
                Expect(cancelled, "cancel-original-while-idle-between-samples");
                Expect(WaitTerminal(jobs, routes, observation), "idle-cancellation-resumes-to-terminal");
                Expect(observation.detachedCallbacks == 1 && observation.publishedSamples == 0 &&
                           observation.terminal.status == JobStatus::Cancelled && observation.literalSamplesMatch,
                       "idle-cancellation-drops-real-pending-pixel-without-external-publication");
                const auto after = queue.Statistics();
                Expect(after.copies == suspendedStats.copies && after.maps == suspendedStats.maps &&
                           after.copies - initialStats.copies == 1 && after.maps - initialStats.maps == 1,
                       "idle-cancellation-does-not-repeat-the-real-copy-or-map");
            }
        }
    }
    if (observation.completedCalls)
    {
        Expect(observation.completedCalls == 1 && observation.completedOnOwner, "one-terminal-callback-on-owner");
        Expect(observation.captureCalls == 1 &&
                   observation.detachedCaptureCalls == (cancelIdle || cancelGated ? 1u : 4u),
               "source-captured-once-and-owner-checks-metadata-per-real-output");
        Expect(observation.memoryBoundToOriginal, "terminal-adopts-shared-accounting-bound-to-original-ticket");
        if (cancelGated)
            Expect(observation.processCalls == 1 && observation.workerWasActualCompute && observation.workerWasMta,
                   "cancelled-useful-stage-was-actual-original-compute-on-mta");
        else if (cancelIdle)
            Expect(observation.processCalls == 1 && observation.workerWasActualCompute && observation.workerWasMta,
                   "idle-held-sample-ran-on-original-compute-worker");
        else
            Expect(observation.processCalls == kPoints.size() && observation.workerWasActualCompute &&
                       observation.workerWasMta,
                   "all-four-samples-ran-on-original-compute-worker");
    }
    Close(close);
    Expect(observation.retired.load(std::memory_order_acquire) &&
               observation.retiredOffOwner.load(std::memory_order_relaxed) &&
               observation.retiredMta.load(std::memory_order_relaxed) &&
               observation.accountingLiveAtSinkRetirement.load(std::memory_order_relaxed),
           "transferred-aggregate-retires-off-owner-through-real-sink-with-original-accounting-live");
}
} // namespace

int RunRecordedParentSamplerTests()
{
    gPassed = gFailed = 0;
    RunScenario(false, false);
    RunScenario(true, false);
    RunScenario(false, true);
    std::printf("SUMMARY recorded-parent-sampler passed=%u failed=%u\n", gPassed, gFailed);
    return gFailed == 0 ? 0 : 1;
}
} // namespace RedPrism::CompositionTests
