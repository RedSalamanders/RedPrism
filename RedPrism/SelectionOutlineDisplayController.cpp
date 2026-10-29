#include "SelectionOutlineDisplayController.h"

#include "GpuCapabilities.h"

#include <exception>
#include <limits>
#include <memory>
#include <new>
#include <utility>

namespace
{
bool Add(std::uint64_t& total, std::uint64_t extra) noexcept
{
    if (extra > (std::numeric_limits<std::uint64_t>::max)() - total)
        return false;
    total += extra;
    return true;
}
JobResult Result(HRESULT result) noexcept
{
    return result == E_ABORT || result == HRESULT_FROM_WIN32(ERROR_CANCELLED) ? JobResult{JobStatus::Cancelled}
           : FAILED(result)                                                   ? JobResult{JobStatus::Failed, result}
                                                                              : JobResult{};
}
} // namespace

// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4626 5027)
class SelectionOutlineDisplayController::OutlineJob final : public Job
{
  public:
    enum class Stage : std::uint8_t
    {
        Opening,
        Preparing,
        Cleanup
    };
    OutlineJob(SelectionOutlineDisplayController& owner, Cell& cell) noexcept : _owner(owner), _cell(cell) {}
    JobResult Run(JobContext& context) override
    {
        if (_stage != Stage::Preparing)
            return {};
        auto memory = SharedPublicationMemory::CreateForWork(_cell.plan.outputBytes);
        PreparedSelectionOutlineRaster raster;
        HRESULT result = PreparedSelectionOutlineRaster::Prepare(std::move(_cell.source), _cell.key.grid,
                                                                 std::move(memory), context.cancellation, {}, raster);
        if (SUCCEEDED(result))
            result = _cell.gpu.CreateOnWorker(std::move(raster), context.cancellation);
        return Result(result);
    }
    void RetireWorkerStage(JobContext&, JobResult result) noexcept override
    {
        if (_stage == Stage::Cleanup || (_stage == Stage::Preparing && result.status != JobStatus::Finished))
            _owner.RetireForWork(_cell);
    }
    bool DeferRetirement(JobResult stageResult, JobTicket ticket) noexcept override
    {
        _cell.ticket = ticket;
        if (stageResult.status != JobStatus::Finished && SUCCEEDED(_cell.terminal))
            _cell.terminal = stageResult.status == JobStatus::Failed ? stageResult.error : E_ABORT;
        if (_stage == Stage::Cleanup || _cell.workerRetired.load(std::memory_order_acquire))
            return false;
        SelectionOutlineDisplayDescriptor current;
        if (_stage == Stage::Opening)
        {
            if (FAILED(_cell.terminal) || !_owner.CurrentOwner(_cell, &current))
            {
                _cell.terminal = FAILED(_cell.terminal) ? _cell.terminal : E_ABORT;
                return false; // No source, context or GPU resource was captured.
            }
            HRESULT result = _owner._queue->CaptureCompletionContext(_cell.completion);
            if (SUCCEEDED(result))
            {
                ID3D11Device* actualDevice = nullptr;
                ID3D11DeviceContext* actualImmediate = nullptr;
                result = _owner._queue->BorrowCurrentContextOwner(_cell.completion, actualDevice, actualImmediate);
                if (SUCCEEDED(result) && (actualDevice != current.device || actualImmediate != current.immediate))
                    result = E_ABORT;
            }
            if (SUCCEEDED(result))
            {
                result = _cell.gpu.CaptureOwner(current.device, current.immediate, current.key.deviceGeneration,
                                                _cell.initialGpu);
                _cell.capturedGpu = SUCCEEDED(result);
            }
            if (SUCCEEDED(result))
            {
                _cell.source = *current.source; // O(1), after the original complete CPU/GPU admission.
                _stage = Stage::Preparing;
            }
            else
            {
                _cell.terminal = result;
                _stage = Stage::Cleanup; // Even a captured context is released on the original worker.
            }
            if (!_owner._jobs->Resume(ticket, JobExecutionLane::Compute))
                std::terminate();
            return true;
        }
        if (_stage == Stage::Preparing && SUCCEEDED(_cell.terminal) && _owner.CurrentOwner(_cell))
            return false; // Exact successful Completed transfers memory and publishes after the real join.
        _cell.terminal = FAILED(_cell.terminal) ? _cell.terminal : E_ABORT;
        _stage = Stage::Cleanup;
        if (!_owner._jobs->Resume(ticket, JobExecutionLane::Compute))
            std::terminate();
        return true;
    }
    void RetainJoinedRetirement(JobScheduler& scheduler, JobTicket ticket) noexcept override
    {
        if ((!_cell.capturedGpu || _cell.workerRetired.load(std::memory_order_acquire)) && !_cell.completion &&
            _cell.source.empty())
            return; // An unadmitted/uncaptured queued stage owns no CPU/device graph to retain.
        if (FAILED(scheduler.RetainRetirementMemory(ticket, _cell.shutdownCpu)))
            std::terminate(); // Descriptor preclaimed the original slot; no new admission or allocation.
    }
    void Completed(JobResult result) noexcept override
    {
        _cell.job = nullptr;
        if (_stage == Stage::Preparing && result.status == JobStatus::Finished && SUCCEEDED(_cell.terminal))
        {
            SelectionOutlineDisplayDescriptor current;
            if (!_owner.CurrentOwner(_cell, &current) || FAILED(_owner._jobs->RetainMemory(_cell.ticket, _cell.cpu)) ||
                FAILED(_cell.gpu.PublishOwner(current.device, current.immediate, current.key.deviceGeneration,
                                              current.key.sourceIdentity, current.key.grid)))
                std::terminate(); // Nonreentrant exact descriptor remains current through this bounded adoption.
            auto* previous = _owner._published;
            _cell.phase = Phase::Cached;
            _owner._published = &_cell;
            _owner._failed.reset();
            _owner._client->SelectionOutlineChangedOwner();
            if (previous)
                _owner.RetireCacheOwner(*previous);
            _owner.WakeOwner();
            return;
        }
        if (_cell.capturedGpu && !_cell.workerRetired.load(std::memory_order_acquire))
            std::terminate();
        const bool report = _cell.terminal != E_ABORT && _owner.CurrentOwner(_cell);
        const auto key = _cell.key;
        const auto error = FAILED(_cell.terminal) ? _cell.terminal : E_FAIL;
        _owner.FinishOwner(_cell);
        if (report)
        {
            _owner._failed = key;
            _owner._client->SelectionOutlineFailedOwner(key, error);
        }
        _owner.WakeOwner();
    }

  private:
    SelectionOutlineDisplayController& _owner;
    Cell& _cell;
    Stage _stage = Stage::Opening;
};
#pragma warning(pop)

SelectionOutlineDisplayController::SelectionOutlineDisplayController() noexcept : _ownerThread(GetCurrentThreadId())
{
    for (auto& cell : _cells)
    {
        cell.owner = this;
        cell.destroy = &RetireCellForWork;
    }
}
SelectionOutlineDisplayController::~SelectionOutlineDisplayController()
{
    if (_jobs || _published || HasPending())
        std::terminate();
}
bool SelectionOutlineDisplayController::IsOwner() const noexcept
{
    return _ownerThread == GetCurrentThreadId();
}
HRESULT SelectionOutlineDisplayController::InitializeOwner(JobScheduler& jobs, GpuWorkQueue& queue,
                                                           OwnerWorkMultiplexer& routes, GpuResidencyBudget& budget,
                                                           SelectionOutlineDisplayClient& client) noexcept
{
    if (!IsOwner() || _jobs || !jobs.WakeHandle() || !queue.DeviceGeneration() || !routes.Attach(*this))
        return E_INVALIDARG;
    _jobs = &jobs;
    _queue = &queue;
    _budget = &budget;
    _routes = &routes;
    _client = &client;
    return S_OK;
}
HRESULT SelectionOutlineDisplayController::EnableLassoDraftOwner(LassoDraftDisplayClient& client) noexcept
{
    if (!IsOwner() || !_jobs || _stopped || _draftEnabled)
        return E_INVALIDARG;
    const HRESULT result = _draft.InitializeOwner(*_jobs, *_queue, *_budget, client);
    if (SUCCEEDED(result))
        _draftEnabled = true;
    return result;
}
void SelectionOutlineDisplayController::RequestLassoDraftOwner() noexcept
{
    if (IsOwner() && _draftEnabled)
        _draft.RequestOwner();
}
PreparedLassoDraftGpu::Views SelectionOutlineDisplayController::BorrowLassoDraftFrameOwner() const noexcept
{
    return IsOwner() && _draftEnabled ? _draft.BorrowFrameOwner() : PreparedLassoDraftGpu::Views{};
}
bool SelectionOutlineDisplayController::DescribeOwner(SelectionOutlineDisplayDescriptor& descriptor) const noexcept
{
    return _jobs && !_stopped && !_suspended && _client->DescribeSelectionOutlineOwner(descriptor) &&
           descriptor.source && !descriptor.source->empty() && descriptor.key.sourceIdentity &&
           descriptor.source->Identity() == descriptor.key.sourceIdentity && descriptor.key.viewEpoch &&
           descriptor.key.deviceGeneration == _queue->DeviceGeneration() && descriptor.device && descriptor.immediate;
}
bool SelectionOutlineDisplayController::CurrentOwner(const Cell& cell,
                                                     SelectionOutlineDisplayDescriptor* output) const noexcept
{
    SelectionOutlineDisplayDescriptor descriptor;
    if (!DescribeOwner(descriptor) || descriptor.key != cell.key)
        return false;
    if (cell.completion)
    {
        ID3D11Device* device = nullptr;
        ID3D11DeviceContext* immediate = nullptr;
        if (FAILED(_queue->BorrowCurrentContextOwner(cell.completion, device, immediate)) ||
            device != descriptor.device || immediate != descriptor.immediate)
            return false;
    }
    if (output)
        *output = descriptor;
    return true;
}
void SelectionOutlineDisplayController::WakeOwner() noexcept
{
    _ready = true;
    _jobs->NotifyOwnerWork();
}
void SelectionOutlineDisplayController::RequestOwner() noexcept
{
    if (IsOwner() && _jobs && !_stopped)
    {
        _requested = true;
        WakeOwner();
    }
}
void SelectionOutlineDisplayController::RetryOwner() noexcept
{
    if (IsOwner() && _draftEnabled)
        _draft.RetryOwner();
    if (!IsOwner() || !_jobs || _stopped)
        return;
    _failed.reset();
    for (auto& cell : _cells)
        if (cell.phase == Phase::RetiringGpu && !cell.waiting)
            cell.recoveryArmed = true;
    RequestOwner();
}
void SelectionOutlineDisplayController::SetSuspendedOwner(bool suspended) noexcept
{
    if (IsOwner() && _draftEnabled)
        _draft.SetSuspendedOwner(suspended);
    if (!IsOwner() || !_jobs || _stopped || suspended == _suspended)
        return;
    _suspended = suspended;
    RequestOwner();
}
PreparedSelectionOutlineGpu::Views SelectionOutlineDisplayController::BorrowFrameOwner(
    unsigned int phase) const noexcept
{
    if (!IsOwner() || !_published || _published->phase != Phase::Cached)
        return {};
    SelectionOutlineDisplayDescriptor descriptor;
    if (!CurrentOwner(*_published, &descriptor))
        return {};
    return _published->gpu.BorrowOwner(descriptor.device, descriptor.immediate, descriptor.key.deviceGeneration,
                                       descriptor.key.sourceIdentity, descriptor.key.grid, phase);
}
void SelectionOutlineDisplayController::BeginOwner(const SelectionOutlineDisplayDescriptor& descriptor) noexcept
{
    Cell* free = nullptr;
    for (auto& cell : _cells)
    {
        if (cell.phase == Phase::Active)
            return;
        if (cell.phase == Phase::Empty)
            free = &cell;
    }
    if (!free || (_failed && *_failed == descriptor.key) || (_published && _published->key == descriptor.key))
        return;
    PreparedSelectionOutlineRaster::MemoryPlan plan;
    HRESULT result = PreparedSelectionOutlineRaster::PlanMemory(descriptor.key.grid, descriptor.source->size(),
                                                                descriptor.source->PointCount(), plan);
    std::uint64_t gpuBytes = 0;
    if (SUCCEEDED(result))
        result = PreparedSelectionOutlineGpu::GpuMemoryBytes(descriptor.key.grid.width, descriptor.key.grid.height,
                                                             gpuBytes);
    auto cpuBytes = plan.totalBytes;
    // Keep the conservative whole creation envelope until cached resource destruction. Software texture
    // backing is charged separately in the original CPU reservation; GPU/domain overlap is retained too.
    if (SUCCEEDED(result) &&
        (!Add(cpuBytes, sizeof(OutlineJob) + 4096) || (descriptor.software && !Add(cpuBytes, gpuBytes))))
        result = E_OUTOFMEMORY;
    GpuCapabilities::VideoMemorySnapshot snapshot;
    if (SUCCEEDED(result))
        result = GpuCapabilities::QueryVideoMemorySnapshot(descriptor.device, snapshot);
    const auto& segment = descriptor.software && snapshot.nonLocal.available && snapshot.nonLocal.budget
                              ? snapshot.nonLocal
                              : snapshot.local;
    if (SUCCEEDED(result) && (!segment.available || !segment.budget))
        result = FAILED(segment.queryResult) ? segment.queryResult : E_OUTOFMEMORY;
    if (SUCCEEDED(result))
    {
        GpuResidencyAdmission admission{};
        free->initialGpu = _budget->ReserveOwner(
            gpuBytes, {descriptor.key.deviceGeneration, segment.budget, segment.currentUsage}, admission);
        if (!free->initialGpu)
            result = E_OUTOFMEMORY;
    }
    if (SUCCEEDED(result))
    {
        free->key = descriptor.key;
        free->plan = plan;
        free->bytes = cpuBytes;
        // Bounded construction/admission boundary. Failure exits this operation before source capture.
        try
        {
            auto concrete = std::make_unique<OutlineJob>(*this, *free);
            auto* raw = concrete.get();
            std::unique_ptr<Job> job = std::move(concrete);
            JobOptions options;
            options.kind = JobClass::Refinement;
            options.memoryBytes = cpuBytes;
            options.allowMemoryWaiting = false;
            options.deferredRetirement = true;
            options.reserveRetainedMemory = true;
            result = _jobs->Submit(job, options, free->ticket);
            if (SUCCEEDED(result))
            {
                free->job = raw;
                free->phase = Phase::Active;
                return;
            }
        }
        catch (const std::bad_alloc&)
        {
            result = E_OUTOFMEMORY;
        }
    }
    free->initialGpu.ResetOwner();
    free->key = {};
    _failed = descriptor.key;
    _client->SelectionOutlineFailedOwner(descriptor.key, result);
}
void SelectionOutlineDisplayController::RetireCacheOwner(Cell& cell) noexcept
{
    if (cell.phase != Phase::Cached)
        return;
    if (_published == &cell)
    {
        _published = nullptr;
        _client->SelectionOutlineChangedOwner();
    }
    if (!cell.gpu.DetachOwner())
        std::terminate();
    cell.detached = true;
    cell.phase = Phase::RetiringGpu;
    cell.recoveryArmed = true;
    WakeOwner();
}
void SelectionOutlineDisplayController::DispatchRetirementOwner(Cell& cell) noexcept
{
    cell.phase = Phase::RetiringWorker;
    cell.deferredWorker = true;
    if (!TryDeferRelease(cell, DeferredReleasePolicy::AlwaysOnOwner))
        std::terminate(); // Startup sink is mandatory, full useful-job admission cannot refuse retirement.
}
void SelectionOutlineDisplayController::RetireForWork(Cell& cell) noexcept
{
    if (IsOwner())
        std::terminate();
    if (cell.capturedGpu && !(cell.detached ? cell.gpu.RetireDetachedOnWorker() : cell.gpu.RetireUnpublishedOnWorker()))
        std::terminate();
    cell.source = {};
    cell.completion = {};
    cell.shutdownCpu.Reset(); // Original joined-Close charge outlives every captured CPU/device owner.
    cell.workerRetired.store(true, std::memory_order_release);
}
void SelectionOutlineDisplayController::RetireCellForWork(DeferredReleaseNode* node) noexcept
{
    auto& cell = *static_cast<Cell*>(node);
    cell.owner->RetireForWork(cell);
    cell.owner->_jobs->NotifyOwnerWork();
}
void SelectionOutlineDisplayController::Cell::GpuCompletionReady(JobTicket original, std::uint64_t generation,
                                                                 HRESULT result) noexcept
{
    if (phase != Phase::RetiringGpu || !waiting || original != ticket || generation != key.deviceGeneration ||
        (result != S_OK && result != E_ABORT))
        std::terminate(); // Only this real captured-stream event authorizes normal cached destruction.
    waiting = false;
    owner->DispatchRetirementOwner(*this);
}
void SelectionOutlineDisplayController::FinishOwner(Cell& cell, bool closed) noexcept
{
    if (cell.capturedGpu && !cell.gpu.CompleteRetirementOwner())
        std::terminate();
    cell.initialGpu.ResetOwner();
    if (!closed && cell.cpu.generation && !_jobs->ReleaseRetainedMemory(cell.cpu))
        std::terminate();
    cell.cpu = {};
    cell.job = nullptr;
    cell.key = {};
    cell.ticket = {};
    cell.plan = {};
    cell.phase = Phase::Empty;
    cell.terminal = S_OK;
    cell.capturedGpu = cell.detached = cell.waiting = cell.recoveryArmed = cell.deferredWorker = false;
    cell.workerRetired.store(false, std::memory_order_relaxed);
}
bool SelectionOutlineDisplayController::HasPending() const noexcept
{
    if (_draftEnabled && _draft.HasPending())
        return true;
    for (const auto& cell : _cells)
        if (cell.phase != Phase::Empty && cell.phase != Phase::Cached)
            return true;
    return false;
}
bool SelectionOutlineDisplayController::HasSelectionReady() const noexcept
{
    if (!IsOwner() || !_jobs || _stopped)
        return false;
    if (_ready)
        return true;
    for (const auto& cell : _cells)
    {
        if (cell.phase == Phase::RetiringWorker && cell.workerRetired.load(std::memory_order_acquire))
            return true;
        if (cell.phase == Phase::RetiringGpu && !cell.waiting && cell.recoveryArmed &&
            (FAILED(cell.completion.DeviceStatusOwner()) || _queue->HasRetirementCompletionSlot(cell.completion)))
            return true;
    }
    return false;
}
bool SelectionOutlineDisplayController::HasReady() const noexcept
{
    return HasSelectionReady() || (_draftEnabled && _draft.HasReady());
}
void SelectionOutlineDisplayController::Pump() noexcept
{
    if (!IsOwner() || !_jobs || _stopped)
        return;
    if (_draftEnabled && _draft.HasReady() && (!_selectionTurn || !HasSelectionReady()))
    {
        _selectionTurn = true;
        _draft.Pump();
        return;
    }
    _selectionTurn = false;
    PumpSelectionOwner();
}
void SelectionOutlineDisplayController::PumpSelectionOwner() noexcept
{
    if (!HasSelectionReady())
        return;
    _ready = false;
    for (auto& cell : _cells)
    {
        if (cell.phase == Phase::RetiringWorker && cell.workerRetired.load(std::memory_order_acquire))
        {
            FinishOwner(cell);
            if (_requested)
                WakeOwner();
            return;
        }
        if (cell.phase == Phase::RetiringGpu && !cell.waiting && cell.recoveryArmed &&
            (FAILED(cell.completion.DeviceStatusOwner()) || _queue->HasRetirementCompletionSlot(cell.completion)))
        {
            cell.recoveryArmed = false;
            if (FAILED(cell.completion.DeviceStatusOwner()))
                DispatchRetirementOwner(cell); // Confirmed removal of this actual original device, destruction only.
            else
                cell.waiting = SUCCEEDED(_queue->EnqueueRetirementCompletion(cell.completion, cell.ticket, cell));
            return; // Refused healthy signal stays charged/dormant until explicit real recovery.
        }
    }
    if (!_requested)
        return;
    SelectionOutlineDisplayDescriptor current;
    const bool described = DescribeOwner(current);
    if (_published && (!described || _published->key != current.key))
        RetireCacheOwner(*_published); // Stale outlines are never presented against a new source/view.
    for (auto& cell : _cells)
        if (cell.phase == Phase::Active && (!described || cell.key != current.key))
            (void)_jobs->Cancel(cell.ticket);
    if (described)
        BeginOwner(current);
    bool active = false;
    for (const auto& cell : _cells)
        active = active || cell.phase == Phase::Active;
    // An admitted replacement or retiring cell reawakens on its real completion. Repeated unchanged
    // settled frames perform no pump, admission, source capture or GPU creation.
    bool selectionPending = false;
    for (const auto& cell : _cells)
        selectionPending = selectionPending || (cell.phase != Phase::Empty && cell.phase != Phase::Cached);
    if (!active && !selectionPending)
        _requested = false;
}
void SelectionOutlineDisplayController::StopOwner() noexcept
{
    if (!IsOwner() || !_jobs || _stopped)
        return;
    if (_draftEnabled)
        _draft.StopOwner();
    _stopped = true;
    _ready = _requested = false;
    _published = nullptr;
    _client->SelectionOutlineChangedOwner();
    for (auto& cell : _cells)
        if (cell.phase == Phase::Active)
            (void)_jobs->Cancel(cell.ticket);
}
void SelectionOutlineDisplayController::JoinedShutdownOwner() noexcept
{
    if (!IsOwner() || !_stopped || _joined)
        std::terminate();
    // Exact scheduler Close hook: all useful producers/waits and future renderer frames have stopped.
    // Destruction-only joined shutdown; no callback or successful pixel publication is manufactured.
    for (auto& cell : _cells)
    {
        if (cell.phase == Phase::Cached)
        {
            if (!cell.gpu.DetachOwner())
                std::terminate();
            cell.detached = true;
        }
        cell.job = nullptr;
    }
    if (_draftEnabled)
        _draft.JoinedShutdownOwner();
    _joined = true;
}
void SelectionOutlineDisplayController::RetireShutdownForWork() noexcept
{
    if (IsOwner() || !_joined)
        std::terminate();
    for (auto& cell : _cells)
        if (cell.phase != Phase::Empty && !cell.deferredWorker && !cell.workerRetired.load(std::memory_order_acquire))
            RetireForWork(cell);
    if (_draftEnabled)
        _draft.RetireShutdownForWork();
}
bool SelectionOutlineDisplayController::CloseAfterWorkersOwner() noexcept
{
    if (!IsOwner() || !_jobs || !_joined)
        return false;
    for (auto& cell : _cells)
    {
        if (cell.phase != Phase::Empty && !cell.workerRetired.load(std::memory_order_acquire))
            return false;
        if (cell.phase != Phase::Empty)
            FinishOwner(cell, true); // Closed scheduler returned its fixed cache-ticket accounting at teardown.
    }
    if (_draftEnabled && !_draft.CloseAfterWorkersOwner())
        return false;
    _draftEnabled = false;
    if (!_routes->Detach(*this))
        return false;
    _routes = nullptr;
    _jobs = nullptr; // Close already detached owner-work routes after the real final worker.
    _queue = nullptr;
    _budget = nullptr;
    _client = nullptr;
    return true;
}
