#include "Composition.Tests.RecordedParentPixels.h"

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
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <memory>
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
constexpr int kWidth = 7;
constexpr int kHeight = 5;
constexpr std::uint32_t kTileSide = 3;
constexpr DWORD kDeadlineMs = 10000;
constexpr std::array<std::uint8_t, 4> kBase{20, 40, 60, 128};
constexpr std::array<std::uint8_t, 4> kReplacement{45, 15, 5, 64};
// Literal expected BGRA bytes, authored independently of the composition and sparse-source assembly.
constexpr std::array<std::uint8_t, 140> kExpectedPixels{{
    20, 40, 60, 128, 20, 40, 60, 128, 20, 40, 60, 128, 20, 40, 60, 128, 20, 40, 60, 128, 20, 40, 60, 128,
    20, 40, 60, 128, 20, 40, 60, 128, 20, 40, 60, 128, 20, 40, 60, 128, 20, 40, 60, 128, 20, 40, 60, 128,
    20, 40, 60, 128, 20, 40, 60, 128, 20, 40, 60, 128, 20, 40, 60, 128, 20, 40, 60, 128, 45, 15, 5,  64,
    45, 15, 5,  64,  20, 40, 60, 128, 20, 40, 60, 128, 20, 40, 60, 128, 20, 40, 60, 128, 20, 40, 60, 128,
    45, 15, 5,  64,  45, 15, 5,  64,  20, 40, 60, 128, 20, 40, 60, 128, 20, 40, 60, 128, 20, 40, 60, 128,
    20, 40, 60, 128, 20, 40, 60, 128, 20, 40, 60, 128, 20, 40, 60, 128, 20, 40, 60, 128,
}};
// Independent row-major region truth table; it never drives the parent or supplies an acknowledgement.
constexpr std::array<D3D11_BOX, 6> kExpectedRegions{{
    {0, 0, 0, 3, 3, 1},
    {3, 0, 0, 6, 3, 1},
    {6, 0, 0, 7, 3, 1},
    {0, 3, 0, 3, 5, 1},
    {3, 3, 0, 6, 5, 1},
    {6, 3, 0, 7, 5, 1},
}};
enum class CaseKind : std::uint8_t
{
    Dense,
    Sparse,
    CpuOnly
};
struct CaseLabel final
{
    const char* device;
    const char* name;
};
unsigned gPassed = 0;
unsigned gFailed = 0;

void Expect(CaseLabel label, bool value, const char* name) noexcept
{
    std::printf("%s recorded-parent-pixels.%s.%s.%s\n", value ? "PASS" : "FAIL", label.device, label.name, name);
    gPassed += value ? 1u : 0u;
    gFailed += value ? 0u : 1u;
}

bool InMta() noexcept
{
    APTTYPE type = APTTYPE_CURRENT;
    APTTYPEQUALIFIER qualifier = APTTYPEQUALIFIER_NONE;
    return SUCCEEDED(CoGetApartmentType(&type, &qualifier)) && type == APTTYPE_MTA;
}

CanvasDocument MakeSource(bool sparse)
{
    // Source assembly uses coordinates and separately supplied colors; it never reads the golden table.
    std::array<std::uint8_t, 140> base{};
    for (std::size_t i = 0; i < base.size(); i += 4)
        std::memcpy(base.data() + i, kBase.data(), 4);
    Raster image;
    if (sparse)
    {
        Raster baseRaster = Raster::Make(kWidth, kHeight, RasterFormat::Bgra8888, base.data(), base.size());
        std::array<std::uint8_t, 16> replacement{};
        for (std::size_t i = 0; i < replacement.size(); i += 4)
            std::memcpy(replacement.data() + i, kReplacement.data(), 4);
        Raster replacementRaster = Raster::Make(2, 2, RasterFormat::Bgra8888, replacement.data(), replacement.size());
        image = Raster::MakeSparse(kWidth, kHeight, RasterFormat::Bgra8888, baseRaster, 0, 0,
                                   {RasterTile{3, 2, std::move(replacementRaster)}}, 0);
    }
    else
    {
        for (unsigned y = 2; y < 4; ++y)
            for (unsigned x = 3; x < 5; ++x)
                std::memcpy(base.data() + (static_cast<std::size_t>(y) * kWidth + x) * 4, kReplacement.data(), 4);
        image = Raster::Make(kWidth, kHeight, RasterFormat::Bgra8888, base.data(), base.size());
    }
    CanvasDocument document;
    document.id = Guid::Generate();
    document.width = kWidth;
    document.height = kHeight;
    auto layer = ImageLayer::Blank(L"Recorded parent literal fixture", {kWidth, kHeight});
    layer.transform.origin = {0, 0};
    layer.transform.size = {kWidth, kHeight};
    layer.transform.rotation = 0;
    layer.transform.flipX = false;
    layer.transform.flipY = false;
    layer.transform.sampling = LayerSampling::Nearest;
    layer.opacity = 1;
    layer.blendMode = LayerBlendMode::Normal;
    layer.asset = RasterAsset{std::move(image), {}, L"Recorded parent literal fixture"};
    RecordRoot<ImageLayer>::Builder layers(1);
    if (!layers.Append(std::move(layer)))
        std::terminate();
    auto root = layers.Seal();
    if (!root)
        std::terminate();
    document.layers = std::move(root.value());
    return document;
}

bool ImageMatches(const Raster& image) noexcept
{
    if (image.Width() != kWidth || image.Height() != kHeight || image.Format() != RasterFormat::Bgra8888 ||
        !image.Data() || image.Stride() < static_cast<std::size_t>(kWidth) * 4)
        return false;
    for (unsigned y = 0; y < kHeight; ++y)
        if (std::memcmp(image.Data() + static_cast<std::size_t>(y) * image.Stride(),
                        kExpectedPixels.data() + static_cast<std::size_t>(y) * kWidth * 4, kWidth * 4) != 0)
            return false;
    return true;
}

bool LastPartialRowsMatch(GpuReadbackRows rows) noexcept
{
    if (!rows.data || rows.width != 1 || rows.height != 2 || rows.rowPitch < 4 || rows.deviceGeneration == 0)
        return false;
    for (unsigned y = 0; y < 2; ++y)
        if (std::memcmp(rows.data + static_cast<std::size_t>(y) * rows.rowPitch,
                        kExpectedPixels.data() + (static_cast<std::size_t>(y + 3) * kWidth + 6) * 4, 4) != 0)
            return false;
    return true;
}

bool Memory(void*, MemorySnapshot& snapshot) noexcept
{
    snapshot = {4ull << 30, 4ull << 30};
    return true;
}

struct Observation final
{
    const DWORD ownerThread = GetCurrentThreadId();
    std::atomic<unsigned> entered{0};
    std::atomic<unsigned> recorderEntered{0};
    std::atomic<std::size_t> firstSlot{0};
    std::atomic<std::uint64_t> firstGeneration{0};
    std::atomic<bool> openingCompute{false};
    std::atomic<bool> sameOriginal{true};
    unsigned captureCalls = 0;
    HRESULT captureResult = E_PENDING;
    bool captureOnOwner = false;
    bool captureAdmitted = false;
    bool capturedExistingRoot = false;
    unsigned detachedCaptureCalls = 0;
    unsigned devicePrepareCalls = 0;
    bool devicePreparedOnOwner = false;
    unsigned processCalls = 0;
    DWORD processThread = 0;
    bool processInActualComputeBody = false;
    bool processInMta = false;
    bool originalCpuAdmissionInWorker = false;
    bool cpuImageBuiltOnWorker = false;
    bool pixelsMatched = false;
    bool lastPartialRowsMatched = false;
    bool completedOnOwner = false;
    bool transferDestinationsEmpty = false;
    bool completedPixelsMatched = false;
    bool transferImageStillEmpty = false;
    bool accountingAdopted = false;
    bool deferred = false;
    std::atomic<bool> terminalCallbackEntered{false};
    bool completed = false;
    unsigned completedCalls = 0;
    JobResult result;
    std::atomic<bool> retired{false};
    std::atomic<bool> retiredOffOwner{false};
    std::atomic<bool> retiredInMta{false};
    std::atomic<bool> heldUntilTerminal{false};
    std::atomic<bool> retiringPixelsIntact{false};
    std::atomic<bool> retiringAccountingIntact{false};
};
struct ActualWorkerBody final
{
    Observation* observation = nullptr;
    JobTicket ticket;
    JobExecutionLane lane = JobExecutionLane::Count;
};
thread_local ActualWorkerBody gActualWorkerBody;

void ObserveExecution(void* opaque, JobExecutionNotice notice) noexcept
{
    auto& observation = *static_cast<Observation*>(opaque);
    if (notice.boundary == JobExecutionBoundary::BodyEntered)
    {
        const unsigned previous = observation.entered.fetch_add(1, std::memory_order_relaxed);
        if (previous == 0)
        {
            observation.firstSlot.store(notice.ticket.slot, std::memory_order_relaxed);
            observation.firstGeneration.store(notice.ticket.generation, std::memory_order_relaxed);
            observation.openingCompute.store(notice.lane == JobExecutionLane::Compute, std::memory_order_relaxed);
        }
        else if (notice.ticket.slot != observation.firstSlot.load(std::memory_order_relaxed) ||
                 notice.ticket.generation != observation.firstGeneration.load(std::memory_order_relaxed))
            observation.sameOriginal.store(false, std::memory_order_relaxed);
        if (notice.lane == JobExecutionLane::GpuRecording)
            observation.recorderEntered.fetch_add(1, std::memory_order_relaxed);
        gActualWorkerBody = {&observation, notice.ticket, notice.lane};
    }
    else if (notice.boundary == JobExecutionBoundary::BodyExited)
        gActualWorkerBody = {};
}

#pragma warning(push)
#pragma warning(disable : 4324)
struct Retirement final : DeferredReleaseNode
{
    explicit Retirement(Observation& value, std::uint64_t expected) noexcept
        : observation(value), expectedCpuBytes(expected)
    {
        bytes = expected;
        destroy = [](DeferredReleaseNode* node) noexcept
        {
            auto* self = static_cast<Retirement*>(node);
            auto& observation = self->observation;
            observation.retiredOffOwner.store(GetCurrentThreadId() != observation.ownerThread,
                                              std::memory_order_relaxed);
            observation.retiredInMta.store(InMta(), std::memory_order_relaxed);
            observation.heldUntilTerminal.store(observation.terminalCallbackEntered.load(std::memory_order_acquire),
                                                std::memory_order_relaxed);
            observation.retiringPixelsIntact.store(ImageMatches(self->completedImage), std::memory_order_relaxed);
            observation.retiringAccountingIntact.store(self->memory && self->memory->IsBound() &&
                                                           self->memory->Bytes() == self->expectedCpuBytes &&
                                                           self->memory->ResidentBytes() == self->expectedCpuBytes,
                                                       std::memory_order_relaxed);
            delete self;
            observation.retired.store(true, std::memory_order_release); // After every aggregate owner died.
        };
    }
    Observation& observation;
    const std::uint64_t expectedCpuBytes;
    // Accounting precedes every graph/pixel owner, so it is destroyed last.
    std::shared_ptr<SharedPublicationMemory> memory;
    CanvasDocument document;
    PixelBuffer pixels;
    Raster readbackImage;  // Empty destination for the four-argument transfer.
    Raster completedImage; // Separate owner of the image moved into the terminal callback.
};
#pragma warning(pop)

class Probe final : public RecordedDocumentReadbackJob
{
  public:
    Probe(JobScheduler& scheduler, GpuWorkQueue& queue, GpuCommandPump& commands, UtilityReadbackOwnerService& owners,
          GpuResidencyBudget& budget, UtilityReadbackAdmission::Envelope envelope, GpuResidencyReservation gpuCharge,
          D3D11_BOX region, bool software, bool cpuOnly, const CanvasDocument& captured, Observation& observation,
          JobTicket& original)
        : RecordedDocumentReadbackJob(
              scheduler, queue, commands, owners, budget, envelope, std::move(gpuCharge), region, software,
              cpuOnly ? RecordedReadbackCaptureKind::CpuOnly : RecordedReadbackCaptureKind::Gpu),
          _captured(captured), _cpuOnly(cpuOnly), _observation(observation),
          _aggregate(std::make_unique<Retirement>(observation, envelope.cpuBytes)), _scheduler(scheduler),
          _original(original), _expectedCpuBytes(envelope.cpuBytes)
    {
    }

    ~Probe() override
    {
        if (!_aggregate)
            return;
        // A failed/queued/joined shutdown drops useful completion. Keep any original CPU owners and their
        // accounting on the same worker destruction route as successful completion.
        TransferReadbackOwners(_aggregate->document, _aggregate->pixels, _aggregate->readbackImage, _aggregate->memory);
        if (!TryDeferRelease(*_aggregate, DeferredReleasePolicy::AlwaysOnOwner))
            std::terminate();
        (void)_aggregate.release();
    }

  protected:
    HRESULT CaptureReadbackDocument(CanvasDocument& destination) noexcept override
    {
        ++_observation.captureCalls;
        _observation.captureOnOwner =
            GetCurrentThreadId() == _observation.ownerThread && gActualWorkerBody.observation == nullptr;
        _observation.captureAdmitted = _scheduler.ReservedMemoryBytes() == _expectedCpuBytes;
        if (_cpuOnly)
        {
            _observation.captureResult = S_FALSE;
            return S_FALSE; // Main resumes detached ProcessReadback directly on the original Compute ticket.
        }
        destination = _captured; // Existing immutable source, captured only in this actual admitted owner hook.
        _observation.capturedExistingRoot = destination.layers.Identity() == _captured.layers.Identity();
        _observation.captureResult = S_OK;
        return S_OK;
    }

    bool ProcessReadbackDetached() const noexcept override
    {
        return _cpuOnly;
    }

    HRESULT CaptureDetachedReadback(Raster&) noexcept override
    {
        ++_observation.detachedCaptureCalls;
        return E_UNEXPECTED; // Neither happy path calls this owner hook. In particular, it never allocates.
    }

    JobResult ProcessReadback(Raster& image, JobContext& context) override
    {
        ++_observation.processCalls;
        _observation.processThread = GetCurrentThreadId();
        _observation.processInActualComputeBody = gActualWorkerBody.observation == &_observation &&
                                                  gActualWorkerBody.ticket == _original &&
                                                  gActualWorkerBody.lane == JobExecutionLane::Compute;
        _observation.processInMta = InMta();
        _observation.originalCpuAdmissionInWorker =
            context.reservedMemoryBytes == _expectedCpuBytes && context.deferredRetirement;
        if (!_observation.processInActualComputeBody || !_observation.processInMta ||
            _observation.processThread == _observation.ownerThread || !_observation.originalCpuAdmissionInWorker)
            return {JobStatus::Failed, E_UNEXPECTED};
        if (_cpuOnly)
        {
            image =
                Raster::Make(kWidth, kHeight, RasterFormat::Bgra8888, kExpectedPixels.data(), kExpectedPixels.size());
            _observation.cpuImageBuiltOnWorker = image.HasStorage();
            if (!image.HasStorage())
                return {JobStatus::Failed, E_OUTOFMEMORY};
        }
        else
            _observation.lastPartialRowsMatched = LastPartialRowsMatch(MappedRowsForTest());
        _observation.pixelsMatched = ImageMatches(image);
        return _observation.pixelsMatched ? JobResult{} : JobResult{JobStatus::Failed, E_FAIL};
    }

    HRESULT PrepareReadbackDevice(HRESULT reason) noexcept override
    {
        ++_observation.devicePrepareCalls;
        _observation.devicePreparedOnOwner =
            GetCurrentThreadId() == _observation.ownerThread && gActualWorkerBody.observation == nullptr;
        return _observation.devicePreparedOnOwner ? reason : E_UNEXPECTED;
    }

    void ReadbackCompleted(JobResult result, Raster image, std::uint64_t) noexcept override
    {
        ++_observation.completedCalls;
        _observation.result = result;
        _observation.completedOnOwner = GetCurrentThreadId() == _observation.ownerThread &&
                                        gActualWorkerBody.observation == nullptr && _scheduler.InUiCallback();
        _observation.terminalCallbackEntered.store(true, std::memory_order_release);
        _aggregate->completedImage = std::move(image);
        _observation.completedPixelsMatched = ImageMatches(_aggregate->completedImage);
        _observation.transferDestinationsEmpty = !_aggregate->document.layers.Identity() &&
                                                 !_aggregate->document.selection && !_aggregate->pixels &&
                                                 !_aggregate->readbackImage.HasStorage() && !_aggregate->memory;
        TransferReadbackOwners(_aggregate->document, _aggregate->pixels, _aggregate->readbackImage, _aggregate->memory);
        _observation.transferImageStillEmpty = !_aggregate->readbackImage.HasStorage();
        _observation.accountingAdopted = _aggregate->memory && _aggregate->memory->IsBound() &&
                                         _aggregate->memory->Bytes() == _expectedCpuBytes &&
                                         _aggregate->memory->BoundTo(_scheduler, _original);
        _observation.deferred = TryDeferRelease(*_aggregate, DeferredReleasePolicy::AlwaysOnOwner);
        if (!_observation.deferred)
            std::terminate();       // A missing scheduler sink cannot be replaced with owner-thread destruction.
        (void)_aggregate.release(); // Ownership now belongs exclusively to the actual release sink.
        _observation.completed = true;
    }

  private:
    const CanvasDocument& _captured;
    const bool _cpuOnly;
    Observation& _observation;
    std::unique_ptr<Retirement> _aggregate;
    JobScheduler& _scheduler;
    JobTicket& _original;
    const std::uint64_t _expectedCpuBytes;
};

struct StartupPrefix final
{
    bool jobsInitialized = false;
    bool releaseEnabled = false;
    bool recordersEnabled = false;
    bool routesRegistered = false;
    bool queueAttempted = false; // Failed Initialize can still leave partially registered event waits.
    bool queueInitialized = false;
    bool queueBound = false;
    bool commandsInitialized = false;
    bool serviceInitialized = false;
};

bool StartInfrastructure(CaseLabel label, JobScheduler& jobs, OwnerWorkMultiplexer& routes, GpuWorkQueue& queue,
                         GpuCommandPump& commands, UtilityReadbackOwnerService& owners, ID3D11Device* device,
                         ID3D11DeviceContext* context, Observation& observation, StartupPrefix& prefix) noexcept
{
    prefix.jobsInitialized = SUCCEEDED(jobs.Initialize(1, {}, {Memory, nullptr}, {ObserveExecution, &observation}));
    Expect(label, prefix.jobsInitialized, "startup-scheduler");
    if (!prefix.jobsInitialized)
        return false; // Initialize rolls back its own failed scheduler prefix.
    prefix.releaseEnabled = jobs.EnableDeferredRelease();
    Expect(label, prefix.releaseEnabled, "startup-deferred-release-sink");
    if (!prefix.releaseEnabled)
        return false;
    prefix.recordersEnabled = SUCCEEDED(jobs.EnableGpuRecordingLane());
    Expect(label, prefix.recordersEnabled, "startup-hresult-gpu-recording-lane");
    if (!prefix.recordersEnabled)
        return false;
    prefix.routesRegistered = jobs.RegisterOwnerWork(routes);
    Expect(label, prefix.routesRegistered, "startup-owner-multiplexer");
    if (!prefix.routesRegistered)
        return false;
    prefix.queueAttempted = true;
    prefix.queueInitialized = SUCCEEDED(queue.Initialize(jobs, &routes));
    Expect(label, prefix.queueInitialized, "startup-queue");
    if (!prefix.queueInitialized)
        return false;
    prefix.queueBound = SUCCEEDED(queue.Bind(device, context));
    Expect(label, prefix.queueBound, "startup-queue-device-bind");
    if (!prefix.queueBound)
        return false;
    prefix.commandsInitialized = SUCCEEDED(commands.InitializeOwner(jobs, queue, routes));
    Expect(label, prefix.commandsInitialized, "startup-command-pump");
    if (!prefix.commandsInitialized)
        return false;
    prefix.serviceInitialized = SUCCEEDED(owners.InitializeOwner(jobs, routes));
    Expect(label, prefix.serviceInitialized, "startup-owner-service");
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
    // Jobs.Close has actually joined useful compute/recording producers before invoking this hook.
    if (state.prefix.commandsInitialized)
        state.commands.AcknowledgeJoinedShutdownOwner(); // Destruction acknowledgement, never pixel/GPU success.
    state.joinedAcknowledgment = true;
    state.closeSequence.store(1, std::memory_order_release);
    if (state.prefix.queueAttempted)
        state.queue.CloseAfterWorkers(); // Only queue-close call; before the scheduler's final MTA hook/drain.
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
    // Scheduler drains its actual intrusive release sink after this hook, then joins this final worker.
}

void CloseInfrastructure(CaseLabel label, CloseContext& state) noexcept
{
    if (state.prefix.serviceInitialized)
        state.owners.StopOwner();
    if (state.prefix.commandsInitialized)
        state.commands.StopOwner();
    if (state.prefix.queueAttempted)
        state.queue.StopNotifications();
    if (state.prefix.jobsInitialized)
        state.jobs.Close({JoinedOwnerClose, FinalWorkerClose, &state});
    // Service joined charges cannot be released while the owner/final-worker hooks are active.
    const bool chargesReleased = !state.prefix.serviceInitialized || state.owners.ReleaseJoinedChargesOwner();
    const bool serviceClosed = !state.prefix.serviceInitialized || state.owners.CloseOwner();
    Expect(label,
           !state.prefix.jobsInitialized || (state.joinedOwnerHook && state.joinedAcknowledgment &&
                                             state.queueClosedInOwnerHook && state.commandsClosedInOwnerHook),
           "close-joined-owner-ack-then-queue-then-command-pump");
    Expect(label,
           !state.prefix.jobsInitialized ||
               (state.finalWorkerHook && state.finalWorkerMta && state.finalWorkerThread != state.ownerThread &&
                state.workerSawClosedOwnerInfrastructure),
           "close-real-final-mta-worker-sees-owner-infrastructure-closed");
    Expect(label, chargesReleased, "close-service-joined-charges-released-after-jobs-return");
    Expect(label, serviceClosed, "close-owner-service-after-jobs-return");
    Expect(label, !state.routes.HasPending() && !state.routes.HasReady(),
           "close-only-initialized-prefixes-routes-empty");
    Expect(label,
           !state.jobs.WakeHandle() && state.jobs.ReservedMemoryBytes() == 0 && state.budget.ReservedOwner() == 0 &&
               state.budget.ActiveOwner() == 0,
           "close-actual-cpu-and-gpu-admission-refunded");
    if ((state.prefix.jobsInitialized && !state.commandsClosedInOwnerHook) || !chargesReleased || !serviceClosed)
    {
        std::fflush(stdout);
        std::terminate(); // Named failures reported above; a still-attached owner cannot be destroyed safely.
    }
}

bool ActualGpuSnapshot(ID3D11Device* device, bool software, std::uint64_t generation,
                       GpuResidencySnapshot& result) noexcept
{
    GpuCapabilities::VideoMemorySnapshot snapshot{};
    if (FAILED(GpuCapabilities::QueryVideoMemorySnapshot(device, snapshot)))
        return false;
    const auto& segment =
        software && snapshot.nonLocal.available && snapshot.nonLocal.budget != 0 ? snapshot.nonLocal : snapshot.local;
    if (!segment.available || segment.budget == 0)
        return false;
    result = {generation, segment.budget, segment.currentUsage};
    return true;
}

bool WaitForTerminal(JobScheduler& jobs, OwnerWorkMultiplexer& routes, const Observation& observation) noexcept
{
    const ULONGLONG deadline = GetTickCount64() + kDeadlineMs;
    while (!observation.completed)
    {
        if (GetTickCount64() >= deadline)
            return false;
        (void)DrainCompletions(jobs); // Count only; actual scheduler/owner routes deliver every callback.
        if (observation.completed)
            return true;
        if (GetTickCount64() >= deadline)
            return false;
        if (jobs.HasUiWork() || routes.HasReady())
        {
            if (GetTickCount64() >= deadline) // Bound immediately before every continue.
                return false;
            continue;
        }
        const ULONGLONG now = GetTickCount64();
        if (now >= deadline || !jobs.WakeHandle())
            return false;
        if (WaitForSingleObject(static_cast<HANDLE>(jobs.WakeHandle()), static_cast<DWORD>(deadline - now)) !=
            WAIT_OBJECT_0)
            return false;
    }
    return true;
}

void RunCase(D3D_DRIVER_TYPE driver, CaseKind kind)
{
    const bool software = driver == D3D_DRIVER_TYPE_WARP;
    const bool cpuOnly = kind == CaseKind::CpuOnly;
    const CaseLabel label{software ? "warp" : "hardware", cpuOnly                    ? "cpu-only"
                                                          : kind == CaseKind::Sparse ? "sparse"
                                                                                     : "dense"};
    wil::com_ptr_nothrow<ID3D11Device> device;
    wil::com_ptr_nothrow<ID3D11DeviceContext> context;
    constexpr D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
    const HRESULT created = D3D11CreateDevice(nullptr, driver, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, &level, 1,
                                              D3D11_SDK_VERSION, device.put(), nullptr, context.put());
    if (FAILED(created) && driver == D3D_DRIVER_TYPE_HARDWARE &&
        (created == DXGI_ERROR_UNSUPPORTED || created == DXGI_ERROR_NOT_FOUND))
    {
        std::printf("INFO recorded-parent-pixels.%s.%s.skipped-unsupported-create-device hr=0x%08lx\n", label.device,
                    label.name, static_cast<unsigned long>(created));
        return; // Only this actual device-creation outcome may skip hardware.
    }
    Expect(label, SUCCEEDED(created), "device-create");
    if (FAILED(created))
        return;
    CanvasDocument source = MakeSource(kind == CaseKind::Sparse);
    const auto outputBytes = RecordedDocumentReadbackJob::RequiredMemory({0, 0, 0, kWidth, kHeight, 1});
    const auto envelope =
        UtilityReadbackAdmission::Estimate(source, kTileSide, outputBytes + sizeof(Retirement), software);
    Expect(label, envelope && envelope->tileSide == kTileSide,
           "actual-source-and-preallocated-aggregate-estimated-with-actual-driver");
    if (!envelope)
        return;
    Observation observation;
    JobScheduler jobs;
    OwnerWorkMultiplexer routes;
    GpuWorkQueue queue;
    GpuCommandPump commands;
    UtilityReadbackOwnerService owners;
    GpuResidencyBudget budget;
    StartupPrefix prefix;
    CloseContext close{jobs, queue, commands, owners, routes, budget, prefix, observation.ownerThread};
    if (!StartInfrastructure(label, jobs, routes, queue, commands, owners, device.get(), context.get(), observation,
                             prefix))
    {
        CloseInfrastructure(label, close);
        return;
    }

    GpuResidencyReservation gpuCharge;
    GpuResidencyAdmission admission = GpuResidencyAdmission::Invalid;
    GpuResidencySnapshot snapshot{};
    if (cpuOnly)
        Expect(label, !gpuCharge && budget.ReservedOwner() == 0, "cpu-only-route-has-no-gpu-charge");
    else
    {
        const bool queried = ActualGpuSnapshot(device.get(), software, queue.DeviceGeneration(), snapshot);
        Expect(label, queried, "gpu-actual-adapter-budget-segment-query");
        if (!queried)
        {
            CloseInfrastructure(label, close);
            return;
        }
        gpuCharge = budget.ReserveOwner(envelope->gpuBytes, snapshot, admission);
        const bool admitted = gpuCharge && admission == GpuResidencyAdmission::Ready &&
                              gpuCharge.Bytes() == envelope->gpuBytes && budget.ReservedOwner() == envelope->gpuBytes;
        Expect(label, admitted, "gpu-complete-estimated-charge-pre-reserved");
        if (!admitted)
        {
            gpuCharge.ResetOwner();
            CloseInfrastructure(label, close);
            return;
        }
    }

    JobTicket ticket{};
    std::unique_ptr<Job> job =
        std::make_unique<Probe>(jobs, queue, commands, owners, budget, *envelope, std::move(gpuCharge),
                                D3D11_BOX{0, 0, 0, kWidth, kHeight, 1}, software, cpuOnly, source, observation, ticket);
    JobOptions options;
    options.kind = JobClass::Background;
    options.memoryBytes = envelope->cpuBytes;
    options.deferredRetirement = true;
    options.reserveRetainedMemory = true;
    options.allowMemoryWaiting = false;
    options.executionLane = JobExecutionLane::Compute; // Opening is Compute for every driver and capture kind.
    const HRESULT submitted = jobs.Submit(job, options, ticket); // One Submit; all stages resume this ticket.
    Expect(label, SUCCEEDED(submitted) && !job, "one-original-ticket-with-complete-retained-admission");
    Expect(label, SUCCEEDED(submitted) && jobs.ReservedMemoryBytes() == envelope->cpuBytes,
           "original-complete-cpu-reservation-held-after-submit");
    if (FAILED(submitted))
    {
        job.reset(); // Unsubmitted descriptor owns only its empty aggregate and original owner GPU reservation.
        CloseInfrastructure(label, close);
        return;
    }

    const auto before = queue.Statistics();
    const bool terminal = WaitForTerminal(jobs, routes, observation);
    Expect(label, terminal, "bounded-terminal-wait-with-ready-work-check");
    // On timeout read no plain worker observations until the real close joins those workers.
    if (terminal)
    {
        Expect(label,
               observation.completedCalls == 1 && observation.result.status == JobStatus::Finished &&
                   SUCCEEDED(observation.result.error),
               "actual-terminal-completion-once-success");
        Expect(label, observation.openingCompute.load(std::memory_order_relaxed), "opening-actual-compute-body");
        Expect(label,
               observation.sameOriginal.load(std::memory_order_relaxed) &&
                   observation.firstSlot.load(std::memory_order_relaxed) == ticket.slot &&
                   observation.firstGeneration.load(std::memory_order_relaxed) == ticket.generation,
               "all-actual-worker-body-notices-use-original-ticket");
        Expect(label, observation.captureCalls == 1 && observation.captureOnOwner && observation.captureAdmitted,
               "capture-actual-admitted-owner-hook");
        Expect(label,
               observation.processCalls == 1 && observation.processInActualComputeBody && observation.processInMta &&
                   observation.processThread != observation.ownerThread,
               "process-actual-original-compute-body-and-mta-thread");
        Expect(label, observation.originalCpuAdmissionInWorker, "process-original-full-cpu-reservation-in-job-context");
        Expect(label, observation.pixelsMatched, "worker-independent-literal-140-byte-bgra");
        Expect(label, observation.completedOnOwner, "terminal-actual-owner-completed-callback");
        Expect(label, observation.completedPixelsMatched, "terminal-completed-image-preserves-literal-pixels");
        Expect(label, observation.transferDestinationsEmpty && observation.transferImageStillEmpty,
               "four-owner-transfer-empty-destinations-and-separate-completed-image");
        Expect(label, observation.accountingAdopted, "terminal-adopts-same-original-complete-accounting");
        Expect(label, observation.deferred, "aggregate-enqueued-on-real-scheduler-intrusive-release-sink");
        Expect(label, observation.detachedCaptureCalls == 0, "owner-capture-detached-never-called");
        if (cpuOnly)
        {
            Expect(label, observation.captureResult == S_FALSE && observation.cpuImageBuiltOnWorker,
                   "mandatory-s-false-direct-detached-worker-builds-literal-raster");
            const auto after = queue.Statistics();
            Expect(label,
                   observation.recorderEntered.load(std::memory_order_relaxed) == 0 &&
                       observation.devicePrepareCalls == 0 && after.copies == before.copies &&
                       after.maps == before.maps && after.readinessBarriers == before.readinessBarriers &&
                       commands.AttachedSourceCountOwnerForTest() == 0 && !commands.HasPending() && !queue.HasPending(),
                   "cpu-only-zero-gpu-recording-command-source-copy-map-or-barrier");
        }
        else
        {
            Expect(label,
                   observation.captureResult == S_OK && observation.capturedExistingRoot &&
                       observation.devicePrepareCalls == 1 && observation.devicePreparedOnOwner &&
                       observation.recorderEntered.load(std::memory_order_relaxed) != 0,
                   "gpu-captures-existing-immutable-source-and-uses-actual-recorder");
            Expect(label,
                   queue.Statistics().copies - before.copies == kExpectedRegions.size() &&
                       observation.lastPartialRowsMatched && observation.pixelsMatched,
                   "gpu-six-3x3-region-copies-full-7x5-pixels-and-partial-right-bottom-1x2-lease");
        }
        Expect(label, !routes.HasReady() && !routes.HasPending(), "terminal-owner-routes-settled");
    }
    CloseInfrastructure(label, close);
    Expect(label,
           observation.retired.load(std::memory_order_acquire) &&
               observation.retiredOffOwner.load(std::memory_order_relaxed) &&
               observation.retiredInMta.load(std::memory_order_relaxed),
           "after-real-close-aggregate-destroyed-on-mta-worker");
    Expect(label,
           observation.heldUntilTerminal.load(std::memory_order_relaxed) &&
               observation.retiringPixelsIntact.load(std::memory_order_relaxed) &&
               observation.retiringAccountingIntact.load(std::memory_order_relaxed),
           "result-and-full-accounting-survive-terminal-through-aggregate-destruction");
}
} // namespace

int RunRecordedParentPixelTests()
{
    gPassed = gFailed = 0;
    RunCase(D3D_DRIVER_TYPE_WARP, CaseKind::Dense);
    RunCase(D3D_DRIVER_TYPE_WARP, CaseKind::Sparse);
    RunCase(D3D_DRIVER_TYPE_WARP, CaseKind::CpuOnly);
    RunCase(D3D_DRIVER_TYPE_HARDWARE, CaseKind::Dense);
    RunCase(D3D_DRIVER_TYPE_HARDWARE, CaseKind::Sparse);
    RunCase(D3D_DRIVER_TYPE_HARDWARE, CaseKind::CpuOnly);
    std::printf("SUMMARY recorded-parent-pixels passed=%u failed=%u\n", gPassed, gFailed);
    return gFailed == 0 ? 0 : 1;
}
} // namespace RedPrism::CompositionTests
