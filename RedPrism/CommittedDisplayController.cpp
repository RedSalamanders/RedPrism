#include "CommittedDisplayController.h"

#include "AdjustmentDisplayAdmission.h"

#include <exception>
#include <limits>
#include <new>
#include <utility>

namespace
{
constexpr std::uint64_t kGraphFixedCpuBytes = 8ull * 1024 * 1024;
constexpr std::uint64_t kPerLayerContainerBytes = 4096;

bool Add(std::uint64_t& value, std::uint64_t extra) noexcept
{
    if (extra > (std::numeric_limits<std::uint64_t>::max)() - value)
        return false;
    value += extra;
    return true;
}

bool SameVisibleOpening(const CommittedDisplayKey& first, const CommittedDisplayKey& second) noexcept
{
    if (first.purpose == DisplaySourcePurpose::Committed && second.purpose == DisplaySourcePurpose::Committed)
        return first.document.sessionIdentity == second.document.sessionIdentity &&
               first.document.documentSerial == second.document.documentSerial &&
               first.deviceGeneration == second.deviceGeneration;
    return first.purpose == second.purpose && first.opening == second.opening && first.document == second.document &&
           first.deviceGeneration == second.deviceGeneration;
}

bool DescriptorMatches(const CommittedDisplayDescriptor& descriptor, bool pending) noexcept
{
    if (!descriptor.document || !descriptor.device || !descriptor.immediate || !descriptor.key.deviceGeneration)
        return false;
    if (!pending)
        return descriptor.key.purpose == DisplaySourcePurpose::Committed && !descriptor.pending &&
               !descriptor.key.opening && !descriptor.key.publication && !descriptor.key.sourceIdentity &&
               !descriptor.key.lookupIdentity;
    if (!descriptor.pending || descriptor.key.purpose == DisplaySourcePurpose::Committed)
        return false;
    const auto& source = descriptor.pending.value();
    if (source.kind != PendingDisplaySource::Kind::Transform && source.kind != PendingDisplaySource::Kind::Color &&
        source.kind != PendingDisplaySource::Kind::Adjustment)
        return false;
    if (source.kind == PendingDisplaySource::Kind::Adjustment &&
        (!source.original || !source.lookup || !*source.lookup || source.adjustmentSource ||
         source.adjustmentCoverage || source.previewScale != 1))
        return false;
    if (source.kind != PendingDisplaySource::Kind::Adjustment && source.original)
        return false;
    if (source.kind == PendingDisplaySource::Kind::Transform &&
        (source.lookup || source.adjustmentSource || source.adjustmentCoverage || source.previewScale != 1))
        return false;
    const auto purpose = source.kind == PendingDisplaySource::Kind::Transform ? DisplaySourcePurpose::Transform
                         : source.kind == PendingDisplaySource::Kind::Color   ? DisplaySourcePurpose::Color
                                                                              : DisplaySourcePurpose::Adjustment;
    return source.document == descriptor.document && source.authority == descriptor.key.document && source.opening &&
           source.publication && source.opening == descriptor.key.opening &&
           source.publication == descriptor.key.publication && purpose == descriptor.key.purpose &&
           source.sourceIdentity == descriptor.key.sourceIdentity &&
           descriptor.document->layers.Identity() == source.sourceIdentity &&
           (source.lookup && *source.lookup ? source.lookup->get() : nullptr) == descriptor.key.lookupIdentity;
}

bool PreviewEnvelope(const CommittedDisplayDescriptor& descriptor, AdjustmentDisplayAdmission::Envelope& result,
                     std::uint64_t& heldBytes) noexcept
{
    heldBytes = 0;
    if (!descriptor.pending || !descriptor.pending->lookup)
        return true; // The completed CPU document is already covered by the base manifest admission.
    const auto& source = descriptor.pending.value();
    if (!*source.lookup)
        return false;
    const auto& lookup = **source.lookup;
    AdjustmentDisplayAdmission::Descriptor additional;
    additional.kind = lookup.adjustment.kind;
    additional.software = descriptor.software;
    additional.liveAdjustment = source.original != nullptr;
    if (!additional.liveAdjustment)
    {
        if (!source.adjustmentSource || !source.adjustmentSource->HasStorage() || !source.adjustmentCoverage)
            return false;
        additional.width = static_cast<std::uint32_t>(source.adjustmentSource->Width());
        additional.height = static_cast<std::uint32_t>(source.adjustmentSource->Height());
        additional.selectionCoverage = source.adjustmentCoverage->HasStorage();
        if (!Add(heldBytes, source.adjustmentSource->RetirementMemoryBytes()) ||
            !Add(heldBytes, source.adjustmentCoverage->RetirementMemoryBytes()))
            return false;
    }
    const auto measured = AdjustmentDisplayAdmission::Estimate(additional);
    if (!measured || lookup.cube.capacity() > (std::numeric_limits<std::uint64_t>::max)() / sizeof(float) ||
        !Add(heldBytes, sizeof(AdjustmentLookup) + 256) || !Add(heldBytes, lookup.cube.capacity() * sizeof(float)))
        return false;
    for (const auto& channel : lookup.adjustment.curves.channels)
        if (channel.capacity() > (std::numeric_limits<std::uint64_t>::max)() / sizeof(CurvePoint) ||
            !Add(heldBytes, channel.capacity() * sizeof(CurvePoint)))
            return false;
    if (source.original)
        for (const auto& channel : source.original->curves.channels)
            if (channel.size() > (std::numeric_limits<std::uint64_t>::max)() / sizeof(CurvePoint) ||
                !Add(heldBytes, channel.size() * sizeof(CurvePoint)))
                return false;
    result = measured.value();
    return true;
}

JobResult Result(HRESULT status) noexcept
{
    return status == E_ABORT ? JobResult{JobStatus::Cancelled}
           : FAILED(status)  ? JobResult{JobStatus::Failed, status}
                             : JobResult{};
}
} // namespace

#pragma warning(push)
// DeferredReleaseNode intentionally aligns intrusive release records.
#pragma warning(disable : 4324)
struct PreparedCommittedDisplay::Storage final : DeferredReleaseNode
{
    explicit Storage(std::uint32_t originalOwner = 0) noexcept
        : ownerThread(originalOwner ? originalOwner : GetCurrentThreadId()), graph(ownerThread)
    {
    }
    const std::uint32_t ownerThread;
    std::shared_ptr<const SharedPublicationMemory> publicationMemory;
    CommittedDisplayController::Cell* cell = nullptr;
    CanvasDocument source;
    std::optional<CanvasRenderer::AdjustmentPreview> adjustment;
    AdjustmentDisplayAdmission::Envelope previewEnvelope;
    RasterResidencyManifestWork::Handle manifest;
    wil::com_ptr_nothrow<ID3D11Device> device;
    wil::com_ptr_nothrow<ID3D11DeviceContext> immediate;
    GpuCompletionContext captured;
    GpuCommandStream stream;
    RecordedDisplayGraph graph;
    const RecordedDisplayGraph* seed = nullptr;
    std::size_t seedLayers = 0;
    bool software = false;
    bool streamInitialized = false;
    bool streamRetired = false;
    bool streamRetirementAuthorized = false;
    bool producerFinished = false; // Recording worker only, then read after its real job-stage acknowledgement.
    bool graphRetired = false;
    bool detached = false;

    void RetireForWork() noexcept
    {
        if (!graphRetired)
        {
            const bool retired = detached ? graph.RetireForWork() : graph.RetireUnpublishedForWork();
            if (!retired)
                std::terminate();
            graphRetired = true;
        }
        adjustment.reset();
        manifest.reset();
        source = {};
        captured = {};
        immediate.reset();
        device.reset();
    }
};

#pragma warning(pop)

class CommittedDisplayController::DisplayJob final : public Job
{
  public:
    enum class Stage
    {
        Opening,
        Planning,
        Recording,
        TransportRetirement,
        Cleanup
    };
    DisplayJob(CommittedDisplayController& owner, Cell& cell, RasterResidencyCpuAdmission admission,
               std::uint64_t bytes) noexcept
        : _owner(owner), _cell(cell), _admission(admission), _cpuBytes(bytes)
    {
    }
    ~DisplayJob() override
    {
        // Normal final callback or actual Close producer join excludes the worker. A completed cleanup stage
        // may be queued but not drained when Close starts; never leave its deleted payload in the owner cell.
        if (_rootDeleted)
            _cell.root = nullptr;
        _cell.job = nullptr;
    }
    JobResult Run(JobContext& context) override
    {
        if (_stage == Stage::Opening || _stage == Stage::TransportRetirement || _stage == Stage::Cleanup)
            return {};
        auto& root = *_cell.root;
        if (_stage == Stage::Planning)
        {
            root.manifest =
                RasterResidencyManifestWork::Prepare(std::move(root.source), _admission, context.cancellation);
            return root.manifest ? JobResult{} : Result(context.cancellation.IsCancelled() ? E_ABORT : E_FAIL);
        }
        if (FAILED(_recordingAdmission))
            return Result(_recordingAdmission); // Immutable before Resume; owner callbacks never write it.
        HRESULT status = E_OUTOFMEMORY;
        const auto finish = wil::scope_exit(
            [&]() noexcept
            {
                root.stream.FinishProducerForWork(status);
                root.producerFinished = true;
            });
        status = root.adjustment
                     ? root.graph.PrepareAdjustmentForWork(root.device.get(), root.manifest->Plan().Source(),
                                                           root.adjustment.value(), root.stream, context.cancellation,
                                                           root.seed)
                     : root.graph.PrepareForWork(root.device.get(), root.manifest->Plan().Source(), root.stream,
                                                 context.cancellation, root.seed);
        return Result(status);
    }
    void RetireWorkerStage(JobContext&, JobResult result) noexcept override
    {
        auto* root = _cell.root;
        if (!root)
            return;
        if (_stage == Stage::Recording && !root->producerFinished)
        {
            // Cancellation may skip Run entirely. Only this original recorder callback can finish that producer.
            root->stream.FinishProducerForWork(result.status == JobStatus::Failed ? result.error : E_ABORT);
            root->producerFinished = true;
        }
        if (_stage == Stage::TransportRetirement)
        {
            if (!root->stream.RetireForWork())
                std::terminate();
            root->streamRetired = true;
        }
        if (_stage == Stage::Cleanup)
        {
            if (root->streamRetirementAuthorized && !root->streamRetired)
            {
                if (!root->stream.RetireForWork())
                    std::terminate();
                root->streamRetired = true;
            }
            root->RetireForWork();
            delete root; // Original ticket and complete reservation survive until this worker acknowledges.
            _rootDeleted = true;
        }
    }
    bool DeferRetirement(JobResult result, JobTicket ticket) noexcept override
    {
        _cell.ticket = ticket;
        if (_stage == Stage::Cleanup)
            return false;
        if (result.status != JobStatus::Finished && SUCCEEDED(_cell.terminal))
            _cell.terminal = result.status == JobStatus::Failed ? result.error : E_ABORT;
        if (_stage == Stage::Opening)
        {
            if (FAILED(_cell.terminal))
                return false;
            CommittedDisplayDescriptor descriptor;
            if (!_owner.CurrentOwner(_cell, &descriptor))
            {
                _cell.terminal = E_ABORT;
                return false;
            }
            try
            {
                auto root = std::make_unique<Root>();
                root->cell = &_cell;
                root->bytes = _cpuBytes;
                root->destroy = &CommittedDisplayController::DestroyRootForWork;
                root->source = *descriptor.document;
                std::uint64_t heldBytes = 0;
                if (!PreviewEnvelope(descriptor, root->previewEnvelope, heldBytes))
                    std::terminate(); // Revalidated unchanged descriptor after its original complete admission.
                if (descriptor.pending && descriptor.pending->lookup)
                {
                    const auto& source = descriptor.pending.value();
                    root->adjustment.emplace();
                    auto& preview = root->adjustment.value();
                    preview.layerId = source.target;
                    preview.lookup = *source.lookup;
                    preview.previewScale = source.previewScale;
                    if (source.original)
                        preview.liveOriginal = *source.original;
                    else
                    {
                        preview.source = *source.adjustmentSource;
                        preview.coverage = *source.adjustmentCoverage;
                    }
                }
                root->device = descriptor.device;
                root->immediate = descriptor.immediate;
                root->software = descriptor.software;
                _cell.root = root.release();
                if (_owner._published && _owner._published->key.deviceGeneration == _cell.key.deviceGeneration)
                {
                    const auto& seed = *_owner._published->root;
                    _cell.root->seed = &seed.graph;
                    _cell.root->seedLayers = seed.manifest->Plan().Source().layers.size();
                    _cell.seed = _owner._published;
                }
                _stage = Stage::Planning;
                if (!_owner._jobs->Resume(ticket, JobExecutionLane::Compute))
                    std::terminate();
                return true;
            }
            catch (const std::bad_alloc&)
            {
                _cell.terminal = E_OUTOFMEMORY; // Explicit admitted capture boundary; the whole operation fails.
                return false;
            }
        }
        if (_stage == Stage::Recording)
        {
            _cell.recordingJoined = true;
            _cell.seed = nullptr; // Preparation no longer borrows the old graph; its own residents hold their rasters.
            _owner.WakeOwner();   // Actual GPU uses may still be pending; no early source/charge release.
            return true;
        }
        if (_stage == Stage::Planning && SUCCEEDED(_cell.terminal))
        {
            CommittedDisplayDescriptor descriptor;
            _cell.terminal =
                _owner.CurrentOwner(_cell, &descriptor) ? _owner.AdmitRecordingOwner(_cell, descriptor) : E_ABORT;
            if (SUCCEEDED(_cell.terminal) || _cell.root->streamInitialized)
            {
                // Attachment refusal still retires this initialized source through its actual producer hook,
                // join and authorization. No record/command/resource creation occurs on the refused stage.
                _recordingAdmission = _cell.terminal;
                _stage = Stage::Recording;
                if (!_owner._jobs->Resume(ticket, JobExecutionLane::GpuRecording))
                    std::terminate();
                return true;
            }
        }
        if (_stage == Stage::TransportRetirement && SUCCEEDED(_cell.terminal) && result.status == JobStatus::Finished &&
            _owner.CurrentOwner(_cell))
            return false; // Final callback transfers the original charge before publication.
        BeginCleanupOwner();
        return true;
    }
    void BeginTransportRetirementOwner() noexcept
    {
        _stage = Stage::TransportRetirement;
        if (!_owner._jobs->Resume(_cell.ticket, JobExecutionLane::Compute))
            std::terminate();
    }
    void BeginCleanupOwner() noexcept
    {
        _cell.cleanupDispatched = true;
        _cell.seed = nullptr;
        _stage = Stage::Cleanup;
        if (!_owner._jobs->Resume(_cell.ticket, JobExecutionLane::Compute))
            std::terminate();
    }
    void Completed(JobResult result) noexcept override
    {
        _cell.job = nullptr;
        if (_rootDeleted)
            _cell.root = nullptr; // Only after actual original worker acknowledgement.
        if (_stage == Stage::TransportRetirement && result.status == JobStatus::Finished && SUCCEEDED(_cell.terminal) &&
            _owner.CurrentOwner(_cell))
        {
            const HRESULT retained = _owner._jobs->RetainMemory(_cell.ticket, _cell.retained);
            if (FAILED(retained))
                std::terminate(); // Descriptor preclaimed the slot; exact successful callback is its sole transfer.
            _owner.PublishedOwner(_cell);
            return;
        }
        if (_cell.root)
            std::terminate(); // No cancelled/failed final callback may refund a still-owned CPU/GPU graph.
        const bool report = _cell.terminal != E_ABORT && _owner.CurrentOwner(_cell);
        const auto key = _cell.key;
        const auto error = FAILED(_cell.terminal) ? _cell.terminal : E_FAIL;
        _owner.FinishCellOwner(_cell);
        if (report)
        {
            _owner._failed = key;
            _owner._client->CommittedDisplayFailedOwner(key, error);
        }
        _owner.WakeOwner();
    }

  private:
    CommittedDisplayController& _owner;
    Cell& _cell;
    const RasterResidencyCpuAdmission _admission;
    const std::uint64_t _cpuBytes;
    Stage _stage = Stage::Opening;
    HRESULT _recordingAdmission = S_OK;
    bool _rootDeleted = false;
};

CommittedDisplaySeedLease::~CommittedDisplaySeedLease()
{
    if (_controller)
        std::terminate(); // Every original parent must explicitly acknowledge its recorder borrow.
}

bool CommittedDisplayController::AcquirePublishedSeedOwner(CommittedDisplayKey key,
                                                           CommittedDisplaySeedLease& lease) noexcept
{
    if (!IsOwner() || !_jobs || _pending || _stopped || _suspended || lease || !_published ||
        _published->phase != Phase::Cached || _published->retirementRequested || _published->externalBorrow ||
        _published->key != key || !_published->root || !CurrentOwner(*_published) ||
        _published->externalBorrowGeneration == (std::numeric_limits<std::uint64_t>::max)())
        return false;
    auto& cell = *_published;
    ++cell.externalBorrowGeneration; // Never reset on slot reuse; no generation wraps.
    cell.externalBorrow = true;
    lease._controller = this;
    lease._graph = &cell.root->graph;
    lease._cell = static_cast<std::size_t>(&cell - _cells.data());
    lease._generation = cell.externalBorrowGeneration;
    return true;
}

bool CommittedDisplayController::ReleasePublishedSeedOwner(CommittedDisplaySeedLease& lease) noexcept
{
    if (!IsOwner() || lease._controller != this || lease._cell >= _cells.size())
        return false;
    auto& cell = _cells[lease._cell];
    if (!cell.externalBorrow || cell.externalBorrowGeneration != lease._generation || !cell.root ||
        &cell.root->graph != lease._graph || cell.phase != Phase::Cached)
        return false;
    cell.externalBorrow = false;
    lease._controller = nullptr;
    lease._graph = nullptr;
    lease._cell = _cells.size();
    lease._generation = 0;
    if (cell.retirementRequested)
        WakeOwner(); // Real reader transition, not an unchanged polling rearm.
    return true;
}

CommittedDisplayController::CommittedDisplayController() noexcept : _ownerThread(GetCurrentThreadId())
{
    for (auto& cell : _cells)
        cell.owner = this;
}
CommittedDisplayController::~CommittedDisplayController()
{
    if (_routes || HasPending() || _published)
        std::terminate();
}
bool CommittedDisplayController::IsOwner() const noexcept
{
    return GetCurrentThreadId() == _ownerThread;
}
HRESULT CommittedDisplayController::InitializeOwner(JobScheduler& jobs, GpuWorkQueue& queue, GpuCommandPump& commands,
                                                    OwnerWorkMultiplexer& routes, CommittedDisplayClient& client,
                                                    GpuResidencyBudget* sharedBudget, bool pending,
                                                    bool ownsCommandPump) noexcept
{
    if (!IsOwner() || _jobs || !jobs.WakeHandle() || !routes.Attach(*this))
        return E_INVALIDARG;
    _jobs = &jobs;
    _queue = &queue;
    _commands = &commands;
    _routes = &routes;
    _client = &client;
    _gpuBudget = sharedBudget ? sharedBudget : &_ownedGpuBudget;
    _pending = pending;
    _ownsCommandPump = ownsCommandPump;
    return S_OK;
}
RecordedDisplayGraph* CommittedDisplayController::GraphForFrameOwner() const noexcept
{
    if (!IsOwner() || !_jobs || _stopped || _suspended || !_published || !_published->root ||
        _published->retirementRequested || _published->phase != Phase::Cached)
        return nullptr;
    CommittedDisplayDescriptor descriptor;
    if (!_client->DescribeCommittedDisplayOwner(descriptor) || !DescriptorMatches(descriptor, _pending) ||
        !SameVisibleOpening(_published->key, descriptor.key) ||
        descriptor.key.deviceGeneration != _queue->DeviceGeneration() ||
        descriptor.device != _published->root->device.get() ||
        descriptor.immediate != _published->root->immediate.get())
        return nullptr;
    return &_published->root->graph;
}

void CommittedDisplayController::WakeOwner() noexcept
{
    _ready = true;
    if (_jobs && !_stopped)
        _jobs->NotifyOwnerWork();
}
void CommittedDisplayController::SetSuspendedOwner(bool suspended) noexcept
{
    if (!IsOwner() || !_jobs || _stopped || _suspended == suspended)
        return;
    _suspended = suspended;
    if (!suspended)
    {
        _failed.reset(); // A real visibility transition permits one fresh admission, not repeated polling.
        RequestOwner();
        return;
    }
    _requested = false;
    if (_published)
        RetireCacheOwner(*_published);
    for (auto& cell : _cells)
        if (cell.phase == Phase::Active)
        {
            (void)_jobs->Cancel(cell.ticket);
            if (!cell.cleanupDispatched && cell.root && cell.root->streamInitialized)
                cell.root->stream.RequestStopOwner();
        }
    WakeOwner(); // Retirement stays event-driven while no useful display can be produced.
}
void CommittedDisplayController::RequestOwner() noexcept
{
    if (!IsOwner() || !_jobs || _stopped || _suspended)
        return;
    if (_published)
    {
        CommittedDisplayDescriptor descriptor;
        if (!_client->DescribeCommittedDisplayOwner(descriptor) || !DescriptorMatches(descriptor, _pending) ||
            !SameVisibleOpening(_published->key, descriptor.key))
            RetireCacheOwner(*_published); // Exclude old document pixels before the next owner route/frame.
    }
    _requested = true;
    WakeOwner();
}
void CommittedDisplayController::RetryOwner() noexcept
{
    if (IsOwner() && !_stopped)
    {
        _failed.reset();
        RequestOwner();
    }
}
bool CommittedDisplayController::CurrentOwner(const Cell& cell, CommittedDisplayDescriptor* output) const noexcept
{
    CommittedDisplayDescriptor descriptor;
    if (_stopped || _suspended || !_client->DescribeCommittedDisplayOwner(descriptor) || descriptor.key != cell.key ||
        !DescriptorMatches(descriptor, _pending) || descriptor.key.deviceGeneration != _queue->DeviceGeneration())
        return false;
    if (cell.root &&
        (descriptor.device != cell.root->device.get() || descriptor.immediate != cell.root->immediate.get()))
        return false;
    if (output)
        *output = descriptor;
    return true;
}
bool CommittedDisplayController::SeedBorrowedOwner(const Cell& cell) const noexcept
{
    if (cell.externalBorrow)
        return true;
    for (const auto& candidate : _cells)
        if (&candidate != &cell && candidate.phase == Phase::Active && candidate.seed == &cell)
            return true;
    return false;
}
void CommittedDisplayController::BeginOwner(const CommittedDisplayDescriptor& descriptor) noexcept
{
    Cell* available = nullptr;
    for (auto& cell : _cells)
    {
        if (cell.phase == Phase::Active || cell.phase == Phase::Publication)
            return;
        if (cell.phase == Phase::Empty)
            available = &cell;
    }
    if (!available || (_failed && _failed.value() == descriptor.key) ||
        (_published && _published->key == descriptor.key))
        return;
    RasterResidencyPlanOptions options;
    options.deviceGeneration = descriptor.key.deviceGeneration;
    const auto admission = RasterResidencyCpuAdmission::Estimate(*descriptor.document, options);
    auto bytes = admission.CpuBytes();
    AdjustmentDisplayAdmission::Envelope preview;
    std::uint64_t heldBytes = 0;
    if (!DescriptorMatches(descriptor, _pending) || !admission.IsValid() ||
        !PreviewEnvelope(descriptor, preview, heldBytes) || !Add(bytes, preview.additionalCpuBytes) ||
        !Add(bytes, heldBytes) || !Add(bytes, sizeof(Root) + sizeof(DisplayJob) + kGraphFixedCpuBytes))
    {
        _failed = descriptor.key;
        _client->CommittedDisplayFailedOwner(descriptor.key, E_OUTOFMEMORY);
        return;
    }
    try
    {
        available->key = descriptor.key;
        auto constructed = std::make_unique<DisplayJob>(*this, *available, admission, bytes);
        auto* raw = constructed.get();
        std::unique_ptr<Job> job = std::move(constructed);
        JobOptions jobOptions;
        jobOptions.kind = JobClass::Refinement;
        jobOptions.memoryBytes = bytes;
        jobOptions.deferredRetirement = true;
        jobOptions.allowMemoryWaiting = false;
        jobOptions.reserveRetainedMemory = true;
        const HRESULT submitted = _jobs->Submit(job, jobOptions, available->ticket);
        if (FAILED(submitted))
        {
            _failed = descriptor.key;
            _client->CommittedDisplayFailedOwner(descriptor.key, submitted);
            return;
        }
        available->job = raw;
        available->phase = Phase::Active;
    }
    catch (const std::bad_alloc&)
    {
        _failed = descriptor.key;
        _client->CommittedDisplayFailedOwner(descriptor.key, E_OUTOFMEMORY);
    }
}
HRESULT CommittedDisplayController::AdmitRecordingOwner(Cell& cell,
                                                        const CommittedDisplayDescriptor& descriptor) noexcept
{
    auto& root = *cell.root;
    const auto layerCount = root.manifest->Plan().Source().layers.size() + root.seedLayers;
    auto cpu = root.bytes;
    if (layerCount > (std::numeric_limits<std::uint64_t>::max)() / kPerLayerContainerBytes ||
        !Add(cpu, layerCount * kPerLayerContainerBytes) ||
        (root.software && !Add(cpu, root.manifest->LogicalGpuBytes())))
        return E_OUTOFMEMORY;
    HRESULT result = _jobs->IncreaseReservation(cell.ticket, cpu);
    if (FAILED(result) || !CurrentOwner(cell))
        return FAILED(result) ? result : E_ABORT;
    root.bytes = cpu;
    GpuCapabilities::VideoMemorySnapshot snapshot;
    result = GpuCapabilities::QueryVideoMemorySnapshot(root.device.get(), snapshot);
    if (FAILED(result))
        return result;
    // Software DEFAULT textures are additionally charged to physical/commit memory above. The GPU-domain
    // charge still requires a successful actual segment query; unsupported/zero budgets refuse activation.
    const auto& segment =
        root.software && snapshot.nonLocal.available && snapshot.nonLocal.budget ? snapshot.nonLocal : snapshot.local;
    if (!segment.available || !segment.budget)
        return FAILED(segment.queryResult) ? segment.queryResult : E_OUTOFMEMORY;
    GpuResidencyAdmission decision{};
    auto gpu = root.manifest->RequiredGpuBytes();
    if (!Add(gpu, root.previewEnvelope.additionalGpuBytes))
        return E_OUTOFMEMORY;
    cell.gpu =
        _gpuBudget->ReserveOwner(gpu, {cell.key.deviceGeneration, segment.budget, segment.currentUsage}, decision);
    if (!cell.gpu)
        return E_OUTOFMEMORY;
    result = root.stream.InitializeOwner(*_jobs, cell.ticket, cell.key.deviceGeneration);
    if (FAILED(result))
        return result;
    root.streamInitialized = true;
    result = _commands->AttachOwner(root.stream, cell.ticket, cell, cell.source);
    if (FAILED(result))
        return result;
    // Strong device/context capture preceded creation, after initial admission; description revalidation above
    // binds this exact original queue context. Attachment owns the completion references until real retirement.
    return descriptor.device == root.device.get() ? S_OK : E_ABORT;
}
void CommittedDisplayController::Cell::CommandAcknowledgedOwner(JobTicket sourceTicket, GpuCommandLease,
                                                                HRESULT result) noexcept
{
    if (phase != Phase::Active || ticket != sourceTicket)
        std::terminate();
    if (FAILED(result) && SUCCEEDED(terminal))
        terminal = result;
    owner->WakeOwner();
}
void CommittedDisplayController::Cell::CommandSourceFailedOwner(JobTicket sourceTicket, HRESULT result) noexcept
{
    if (phase != Phase::Active || ticket != sourceTicket)
        std::terminate();
    if (SUCCEEDED(terminal))
        terminal = result;
    owner->WakeOwner();
}
void CommittedDisplayController::Cell::GpuCompletionReady(JobTicket sourceTicket, std::uint64_t generation,
                                                          HRESULT) noexcept
{
    if (phase != Phase::RetiringGpu || !waiting || sourceTicket != ticket ||
        generation != root->captured.DeviceGeneration())
        std::terminate();
    waiting = false;
    owner->RetireCacheForWorkOwner(*this); // Actual original-use acknowledgement, including stale E_ABORT.
}
void CommittedDisplayController::PublishedOwner(Cell& cell) noexcept
{
    const HRESULT published = cell.root->graph.PublishOwner(cell.root->immediate.get());
    if (FAILED(published))
    {
        cell.phase = Phase::Cached;
        cell.terminal = published;
        _failed = cell.key;
        RetireCacheOwner(cell);
        _client->CommittedDisplayFailedOwner(cell.key, published);
        return;
    }
    auto* previous = _published;
    cell.phase = Phase::Cached;
    _published = &cell;
    _failed.reset();
    _client->CommittedDisplayPublishedOwner(&cell.root->graph);
    if (previous)
        RetireCacheOwner(*previous); // After consumer swap and after the replacement's real seed-producer join.
    WakeOwner();
}
void CommittedDisplayController::RetireCacheOwner(Cell& cell) noexcept
{
    if (cell.phase != Phase::Cached)
        return;
    if (_published == &cell)
    {
        _published = nullptr;
        _client->CommittedDisplayPublishedOwner(nullptr);
    }
    cell.retirementRequested = true;
    // Stop future frame borrows immediately, even if the replacement still borrows this graph's immutable
    // residency. Its worker retains the guard until join; only then detach and record the last-use barrier.
    if (SeedBorrowedOwner(cell))
        return;
    if (!cell.root->graph.DetachOwner())
        std::terminate();
    cell.root->detached = true;
    cell.phase = Phase::RetiringGpu;
    cell.recoveryArmed = true;
    WakeOwner();
}
void CommittedDisplayController::RetireCacheForWorkOwner(Cell& cell) noexcept
{
    cell.phase = Phase::RetiringWorker;
    // No new job admission is needed for noncancellable retirement. The installed original sink/pool outlive
    // this node. Whole CPU/GPU charges survive actual destruction and its fixed-cell acknowledgement.
    if (!TryDeferRelease(*cell.root, DeferredReleasePolicy::AlwaysOnOwner))
        std::terminate(); // Startup must enable deferred release before this controller can be activated.
}
void CommittedDisplayController::DestroyRootForWork(DeferredReleaseNode* node) noexcept
{
    auto* root = static_cast<Root*>(node);
    auto& cell = *root->cell;
    auto& owner = *cell.owner;
    auto publicationMemory = std::move(root->publicationMemory);
    root->RetireForWork();
    delete root;
    publicationMemory.reset(); // Release the original accounting only after all root storage dies.
    cell.workerRetired.store(true, std::memory_order_release);
    owner._jobs->NotifyOwnerWork();
}
void CommittedDisplayController::FinishCellOwner(Cell& cell) noexcept
{
    if (cell.externalBorrow)
        std::terminate(); // Cannot refund/delete a root still borrowed by an original recorder.
    cell.root = nullptr;
    cell.job = nullptr;
    cell.seed = nullptr;
    cell.gpu.ResetOwner();
    if (cell.retained.generation)
    {
        if (!_jobs->ReleaseRetainedMemory(cell.retained))
            std::terminate();
        cell.retained = {};
    }
    cell.phase = Phase::Empty;
    cell.key = {};
    cell.ticket = {};
    cell.source = {};
    cell.terminal = S_OK;
    cell.recordingJoined = false;
    cell.cleanupDispatched = false;
    cell.retirementRequested = false;
    cell.waiting = false;
    cell.quarantine = false;
    cell.recoveryArmed = false;
    cell.workerRetired.store(false, std::memory_order_relaxed);
}
bool CommittedDisplayController::HasPending() const noexcept
{
    for (const auto& cell : _cells)
        if (cell.phase != Phase::Empty && cell.phase != Phase::Cached)
            return true;
    return false;
}
bool CommittedDisplayController::HasReady() const noexcept
{
    if (!IsOwner() || _stopped)
        return false;
    if (_ready)
        return true;
    for (const auto& cell : _cells)
    {
        if (cell.workerRetired.load(std::memory_order_acquire))
            return true;
        if (cell.phase == Phase::Cached && cell.retirementRequested && !SeedBorrowedOwner(cell))
            return true;
        if (cell.phase == Phase::Active && cell.recordingJoined && !cell.cleanupDispatched && cell.root &&
            cell.root->streamInitialized && cell.root->stream.CanRetireOwner())
            return true;
        if (cell.phase == Phase::RetiringGpu && !cell.waiting && cell.recoveryArmed &&
            (FAILED(cell.root->captured.DeviceStatusOwner()) ||
             _queue->HasRetirementCompletionSlot(cell.root->captured)))
            return true;
    }
    return false;
}
void CommittedDisplayController::Pump() noexcept
{
    if (!HasReady())
        return;
    _ready = false;
    for (auto& cell : _cells)
    {
        if (cell.workerRetired.load(std::memory_order_acquire))
        {
            FinishCellOwner(cell);
            WakeOwner();
            return;
        }
        if (cell.phase == Phase::Cached && cell.retirementRequested && !SeedBorrowedOwner(cell))
        {
            RetireCacheOwner(cell);
            return;
        }
        if (cell.phase == Phase::Active && cell.recordingJoined && !cell.cleanupDispatched &&
            cell.root->streamInitialized && cell.root->stream.CanRetireOwner())
        {
            if ((cell.source.generation && !_commands->DetachOwner(cell.source, cell.root->captured)) ||
                !cell.root->stream.AuthorizeRetirementOwner())
                std::terminate();
            cell.root->streamRetirementAuthorized = true;
            cell.source = {};
            cell.recordingJoined = false; // Consume the acknowledged stage once; transport is now running.
            cell.job->BeginTransportRetirementOwner();
            return;
        }
        if (cell.phase == Phase::RetiringGpu && !cell.waiting && cell.recoveryArmed &&
            (FAILED(cell.root->captured.DeviceStatusOwner()) ||
             _queue->HasRetirementCompletionSlot(cell.root->captured)))
        {
            cell.recoveryArmed = false;
            const HRESULT status = cell.root->captured.DeviceStatusOwner();
            if (FAILED(status))
                RetireCacheForWorkOwner(cell); // Actual original-device removal permits destruction only.
            else
            {
                const HRESULT recorded = _queue->EnqueueRetirementCompletion(cell.root->captured, cell.ticket, cell);
                cell.waiting = SUCCEEDED(recorded);
                cell.quarantine = FAILED(recorded); // Healthy failed signal stays charged and dormant.
            }
            return;
        }
    }
    if (_requested && !_suspended)
    {
        CommittedDisplayDescriptor descriptor;
        const bool described = _client->DescribeCommittedDisplayOwner(descriptor);
        if (_published && (!described || !DescriptorMatches(descriptor, _pending) ||
                           !SameVisibleOpening(_published->key, descriptor.key)))
            RetireCacheOwner(*_published); // A different document/device cannot display the old document's pixels.
        for (auto& cell : _cells)
            if (cell.phase == Phase::Active && (!described || cell.key != descriptor.key))
            {
                (void)_jobs->Cancel(cell.ticket);
                if (!cell.cleanupDispatched && cell.root && cell.root->streamInitialized)
                    cell.root->stream.RequestStopOwner();
            }
        if (!described)
        {
            if (_published)
                RetireCacheOwner(*_published);
            _requested = false;
        }
        else if (descriptor.document && descriptor.device && descriptor.immediate && descriptor.key.deviceGeneration)
            BeginOwner(descriptor);
    }
}
void CommittedDisplayController::DeviceChangedOwner() noexcept
{
    if (!IsOwner() || _stopped)
        return;
    if (_ownsCommandPump)
        _commands->DeviceChangedOwner();
    for (auto& cell : _cells)
        if (cell.phase == Phase::RetiringGpu && cell.quarantine)
            cell.recoveryArmed = true;
    RequestOwner();
}
void CommittedDisplayController::StopOwner() noexcept
{
    if (!IsOwner() || !_jobs || _stopped)
        return;
    _stopped = true;
    _ready = false;
    _requested = false;
    _published = nullptr;
    if (_client)
        _client->CommittedDisplayPublishedOwner(nullptr);
    for (auto& cell : _cells)
        if (cell.phase != Phase::Empty)
        {
            if (cell.job)
                (void)_jobs->Cancel(cell.ticket);
            if (cell.phase != Phase::RetiringWorker && !cell.cleanupDispatched && cell.root &&
                cell.root->streamInitialized)
                cell.root->stream.RequestStopOwner();
        }
    if (_ownsCommandPump)
        _commands->StopOwner();
}
void CommittedDisplayController::JoinedShutdownOwner() noexcept
{
    if (!IsOwner() || !_stopped || _joined)
        std::terminate();
    // Actual scheduler producer joins precede this hook. The pump handles an empty recorder that never ran
    // separately from every submitted use, without delivering callbacks or successful pixels.
    if (_ownsCommandPump)
        _commands->AcknowledgeJoinedShutdownOwner();
    for (auto& cell : _cells)
    {
        if (cell.externalBorrow || cell.phase == Phase::Publication)
            std::terminate(); // External parents must release after their actual producer joins, before this hook.
        if (!cell.root || cell.phase == Phase::RetiringWorker)
            continue; // A queued sink node is consumed by the final worker after this hook.
        if (cell.source.generation)
        {
            if (!_commands->DetachOwner(cell.source, cell.root->captured))
                std::terminate();
            cell.source = {};
        }
        if (cell.root->streamInitialized && !cell.root->streamRetired && !cell.root->streamRetirementAuthorized)
        {
            HRESULT producer = E_PENDING;
            if ((!cell.root->stream.ProducerFinishedOwner(producer) &&
                 !cell.root->stream.AcknowledgeEmptyJoinedShutdownOwner()) ||
                !cell.root->stream.AuthorizeRetirementOwner())
                std::terminate();
            cell.root->streamRetirementAuthorized = true;
        }
        if (!cell.root->detached)
        {
            if (!cell.root->graph.DetachOwner())
                std::terminate();
            cell.root->detached = true;
        }
        cell.job = nullptr;
    }
    _joined = true;
}
void CommittedDisplayController::RetireShutdownForWork() noexcept
{
    if (IsOwner() || !_joined)
        std::terminate();
    for (auto& cell : _cells)
    {
        if (!cell.root || cell.phase == Phase::RetiringWorker)
            continue;
        if (cell.root->streamRetirementAuthorized && !cell.root->streamRetired)
        {
            if (!cell.root->stream.RetireForWork())
                std::terminate();
            cell.root->streamRetired = true;
        }
        auto publicationMemory = std::move(cell.root->publicationMemory);
        cell.root->RetireForWork();
        delete cell.root;
        publicationMemory.reset();
        cell.workerRetired.store(true, std::memory_order_release);
    }
}
bool CommittedDisplayController::CloseAfterWorkersOwner() noexcept
{
    if (!IsOwner() || !_joined)
        return false;
    for (auto& cell : _cells)
    {
        if (cell.externalBorrow || cell.phase == Phase::Publication ||
            (cell.root && !cell.workerRetired.load(std::memory_order_acquire)))
            return false;
        cell.root = nullptr;
        cell.job = nullptr;
        cell.retained = {};
        cell.gpu.ResetOwner();
        cell.phase = Phase::Empty;
    }
    if ((_ownsCommandPump && !_commands->CloseOwner()) || !_routes->Detach(*this))
        return false;
    _routes = nullptr;
    _jobs = nullptr;
    _queue = nullptr;
    _commands = nullptr;
    _client = nullptr;
    return true;
}

PreparedCommittedDisplay::~PreparedCommittedDisplay()
{
    if (_storage)
        std::terminate(); // No implicit partial-graph destruction on the owner or before GPU acknowledgement.
}

std::unique_ptr<PreparedCommittedDisplay> PreparedCommittedDisplay::PrepareForWork(
    std::uint32_t ownerThread, CanvasDocument source, RasterResidencyCpuAdmission admission, std::uint64_t cpuBound,
    ID3D11Device* device, ID3D11DeviceContext* originalContext, GpuCompletionContext captured, CancelToken cancellation)
{
    if (!ownerThread || ownerThread == GetCurrentThreadId() || !admission.IsValid() ||
        cpuBound < admission.CpuBytes() || cpuBound - admission.CpuBytes() < sizeof(Storage) + 4096 || !device ||
        !originalContext || originalContext->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE ||
        !captured.DeviceGeneration() || cancellation.IsCancelled())
        return {};
    wil::com_ptr_nothrow<ID3D11Device> contextDevice;
    originalContext->GetDevice(contextDevice.put());
    if (contextDevice.get() != device)
        return {};
    auto root = std::make_unique<Storage>(ownerThread);
    root->bytes = cpuBound;
    root->device = device;
    root->immediate = originalContext;
    root->captured = std::move(captured);
    root->manifest = RasterResidencyManifestWork::Prepare(std::move(source), admission, cancellation);
    if (!root->manifest)
        return {};
    auto prepared = std::unique_ptr<PreparedCommittedDisplay>(new PreparedCommittedDisplay(root.get()));
    (void)root.release();
    return prepared;
}

const RasterResidencyManifestWork* PreparedCommittedDisplay::ManifestAfterJoin() const noexcept
{
    return _storage ? _storage->manifest.get() : nullptr;
}

HRESULT PreparedCommittedDisplay::RecordForWork(GpuCommandStream& stream, CancelToken cancellation,
                                                const RecordedDisplayGraph* seed) noexcept
{
    if (!_storage || !_storage->manifest || _storage->ownerThread == GetCurrentThreadId())
        return E_INVALIDARG;
    return _storage->graph.PrepareForWork(_storage->device.get(), _storage->manifest->Plan().Source(), stream,
                                          std::move(cancellation), seed);
}

bool PreparedCommittedDisplay::BindMemoryOwner(const std::shared_ptr<const SharedPublicationMemory>& memory) noexcept
{
    if (!_storage || _storage->ownerThread != GetCurrentThreadId() || _storage->publicationMemory || !memory ||
        !memory->IsBound() || memory->Bytes() < _storage->bytes)
        return false;
    _storage->publicationMemory = memory;
    return true;
}

HRESULT PreparedCommittedDisplay::PublishOwner() noexcept
{
    if (!_storage || _storage->ownerThread != GetCurrentThreadId() || !_storage->publicationMemory)
        return E_INVALIDARG;
    return _storage->graph.PublishOwner(_storage->immediate.get());
}

bool PreparedCommittedDisplay::DetachOwner() noexcept
{
    if (!_storage || _storage->ownerThread != GetCurrentThreadId() || !_storage->graph.DetachOwner())
        return false;
    _storage->detached = true;
    return true;
}

bool PreparedCommittedDisplay::RetireForWork() noexcept
{
    if (!_storage || _storage->ownerThread == GetCurrentThreadId())
        return false;
    // Root's ordinary helper asserts the graph's exact worker/phase contract. The parent already joined
    // its recorder, retired every command owner and acknowledged original GPU use; this API implies none.
    auto memory = std::move(_storage->publicationMemory);
    _storage->RetireForWork();
    delete std::exchange(_storage, nullptr);
    memory.reset();
    return true;
}

CommittedDisplayPublicationSlot::~CommittedDisplayPublicationSlot()
{
    if (_owner)
        std::terminate(); // Explicit owner release after the parent actually joins, never a worker destructor.
}

CommittedDisplayController::Cell* CommittedDisplayController::PublicationCellOwner(
    const CommittedDisplayPublicationSlot& slot) noexcept
{
    if (!IsOwner() || slot._owner != this || slot._cell >= _cells.size())
        return nullptr;
    auto& cell = _cells[slot._cell];
    return cell.phase == Phase::Publication && cell.publicationGeneration == slot._generation &&
                   cell.ticket == slot._original
               ? &cell
               : nullptr;
}

const CommittedDisplayController::Cell* CommittedDisplayController::PublicationCellOwner(
    const CommittedDisplayPublicationSlot& slot) const noexcept
{
    if (!IsOwner() || slot._owner != this || slot._cell >= _cells.size())
        return nullptr;
    const auto& cell = _cells[slot._cell];
    return cell.phase == Phase::Publication && cell.publicationGeneration == slot._generation &&
                   cell.ticket == slot._original
               ? &cell
               : nullptr;
}

bool CommittedDisplayController::ClaimPublicationSlotOwner(JobTicket original,
                                                           CommittedDisplayPublicationSlot& slot) noexcept
{
    if (!IsOwner() || !_jobs || _pending || _stopped || _suspended || slot ||
        original.slot >= JobScheduler::kCapacity || !original.generation || !_jobs->InAcknowledgedJobCallback(original))
        return false;
    for (const auto& cell : _cells)
        if (cell.phase == Phase::Active || cell.phase == Phase::Publication)
            return false;
    CommittedDisplayDescriptor descriptor;
    if (!_client->DescribeCommittedDisplayOwner(descriptor) || !descriptor.document || !descriptor.device ||
        !descriptor.immediate || descriptor.key.deviceGeneration != _queue->DeviceGeneration())
        return false;
    for (std::size_t index = 0; index < _cells.size(); ++index)
    {
        auto& cell = _cells[index];
        if (cell.phase != Phase::Empty || cell.publicationGeneration == (std::numeric_limits<std::uint64_t>::max)())
            continue;
        cell.phase = Phase::Publication;
        cell.key = descriptor.key;
        cell.ticket = original;
        ++cell.publicationGeneration;
        slot._owner = this;
        slot._cell = index;
        slot._generation = cell.publicationGeneration;
        slot._original = original;
        return true;
    }
    return false;
}

bool CommittedDisplayController::PublicationSlotCurrentOwner(const CommittedDisplayPublicationSlot& slot) const noexcept
{
    const auto* cell = PublicationCellOwner(slot);
    return cell && _jobs->InAcknowledgedJobCallback(slot._original) && CurrentOwner(*cell);
}

HRESULT CommittedDisplayController::ReservePublicationGpuOwner(CommittedDisplayPublicationSlot& slot,
                                                               ID3D11Device* device, bool software, std::uint64_t bytes,
                                                               GpuResidencyReservation& reservation) noexcept
{
    auto* cell = PublicationCellOwner(slot);
    CommittedDisplayDescriptor descriptor;
    if (!cell || reservation || !bytes || !_jobs->InAcknowledgedJobCallback(slot._original) ||
        !CurrentOwner(*cell, &descriptor) || device != descriptor.device || software != descriptor.software)
        return E_INVALIDARG;
    GpuCapabilities::VideoMemorySnapshot snapshot;
    const HRESULT queried = GpuCapabilities::QueryVideoMemorySnapshot(device, snapshot);
    if (FAILED(queried))
        return queried;
    const auto& segment =
        software && snapshot.nonLocal.available && snapshot.nonLocal.budget ? snapshot.nonLocal : snapshot.local;
    if (!segment.available || !segment.budget)
        return FAILED(segment.queryResult) ? segment.queryResult : E_OUTOFMEMORY;
    GpuResidencyAdmission decision{};
    reservation =
        _gpuBudget->ReserveOwner(bytes, {cell->key.deviceGeneration, segment.budget, segment.currentUsage}, decision);
    return reservation ? S_OK : E_OUTOFMEMORY;
}

bool CommittedDisplayController::ReleasePublicationSlotOwner(CommittedDisplayPublicationSlot& slot) noexcept
{
    auto* cell = PublicationCellOwner(slot);
    if (!cell || cell->root || cell->job || cell->gpu || cell->retained.generation)
        return false;
    cell->phase = Phase::Empty;
    cell->key = {};
    cell->ticket = {};
    slot._owner = nullptr;
    slot._cell = _cells.size();
    slot._generation = 0;
    slot._original = {};
    if (!_stopped)
        WakeOwner();
    return true;
}

bool CommittedDisplayController::AdoptPublicationOwner(CommittedDisplayPublicationSlot& slot,
                                                       PreparedCommittedDisplay& prepared,
                                                       GpuResidencyReservation& reservation) noexcept
{
    auto* cell = PublicationCellOwner(slot);
    auto* root = prepared._storage;
    CommittedDisplayDescriptor descriptor;
    // Publication changed the generation. Recheck session/document identity and the new root itself;
    // do not require the pre-edit generation to equal the final prepared document's current generation.
    if (!cell || !root || !_jobs->InAcknowledgedJobCallback(slot._original) || _stopped || _suspended || cell->root ||
        cell->job || root->ownerThread != GetCurrentThreadId() || !root->publicationMemory ||
        !root->publicationMemory->BoundTo(*_jobs, slot._original) || !reservation ||
        !reservation.BelongsTo(*_gpuBudget) || !root->manifest ||
        reservation.Bytes() < root->manifest->RequiredGpuBytes() ||
        reservation.DeviceGeneration() != cell->key.deviceGeneration ||
        !_client->DescribeCommittedDisplayOwner(descriptor) || !descriptor.document ||
        descriptor.key.document.sessionIdentity != cell->key.document.sessionIdentity ||
        descriptor.key.document.documentSerial != cell->key.document.documentSerial ||
        descriptor.key.document.restoreSerial != cell->key.document.restoreSerial ||
        cell->key.document.generation == (std::numeric_limits<std::uint64_t>::max)() ||
        descriptor.key.document.generation != cell->key.document.generation + 1 ||
        descriptor.key.deviceGeneration != cell->key.deviceGeneration || descriptor.device != root->device.get() ||
        descriptor.immediate != root->immediate.get() ||
        descriptor.document->id != root->manifest->Plan().Source().id ||
        descriptor.document->layers.Identity() != root->manifest->Plan().Source().layers.Identity() ||
        descriptor.document->width != root->manifest->Plan().Source().width ||
        descriptor.document->height != root->manifest->Plan().Source().height ||
        descriptor.document->resolution != root->manifest->Plan().Source().resolution ||
        descriptor.document->selection.has_value() != root->manifest->Plan().Source().selection.has_value() ||
        (descriptor.document->selection &&
         (descriptor.document->selection->antialiased != root->manifest->Plan().Source().selection->antialiased ||
          descriptor.document->selection->loops.Identity() !=
              root->manifest->Plan().Source().selection->loops.Identity())) ||
        !root->graph.IsPublishedOnOwner(descriptor.device, descriptor.immediate))
        return false;
    root->cell = cell;
    root->destroy = &CommittedDisplayController::DestroyRootForWork;
    cell->root = std::exchange(prepared._storage, nullptr);
    cell->gpu = std::move(reservation);
    cell->key = descriptor.key;
    cell->phase = Phase::Cached;
    auto* previous = _published;
    _published = cell;
    _failed.reset();
    slot._owner = nullptr;
    slot._cell = _cells.size();
    slot._generation = 0;
    slot._original = {};
    _client->CommittedDisplayPublishedOwner(&root->graph);
    if (previous)
        RetireCacheOwner(*previous);
    WakeOwner();
    return true;
}

bool CommittedDisplayController::CanAdoptPublicationOwner(const CommittedDisplayPublicationSlot& slot,
                                                          const PreparedCommittedDisplay& prepared,
                                                          const GpuResidencyReservation& reservation,
                                                          const CanvasDocument& finalDocument) const noexcept
{
    const auto* cell = PublicationCellOwner(slot);
    const auto* root = prepared._storage;
    CommittedDisplayDescriptor descriptor;
    if (!cell || !root || !_jobs->InAcknowledgedJobCallback(slot._original) || _stopped || _suspended || cell->root ||
        cell->job || root->ownerThread != GetCurrentThreadId() || !root->publicationMemory ||
        !root->publicationMemory->BoundTo(*_jobs, slot._original) || !reservation ||
        !reservation.BelongsTo(*_gpuBudget) || !root->manifest ||
        reservation.Bytes() < root->manifest->RequiredGpuBytes() ||
        reservation.DeviceGeneration() != cell->key.deviceGeneration ||
        cell->key.document.generation == (std::numeric_limits<std::uint64_t>::max)() ||
        !CurrentOwner(*cell, &descriptor) || descriptor.device != root->device.get() ||
        descriptor.immediate != root->immediate.get() ||
        !root->graph.IsPublishedOnOwner(descriptor.device, descriptor.immediate))
        return false;
    const auto& expected = root->manifest->Plan().Source();
    return finalDocument.id == expected.id && finalDocument.layers.Identity() == expected.layers.Identity() &&
           finalDocument.width == expected.width && finalDocument.height == expected.height &&
           finalDocument.resolution == expected.resolution &&
           finalDocument.selection.has_value() == expected.selection.has_value() &&
           (!finalDocument.selection ||
            (finalDocument.selection->antialiased == expected.selection->antialiased &&
             finalDocument.selection->loops.Identity() == expected.selection->loops.Identity()));
}
