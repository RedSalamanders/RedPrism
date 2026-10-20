#include "Application.h"

#include "OriginalDisplayPublication.h"
#include "SelectionOutlineGestureState.h"
#include "SelectionPixelFinalWork.h"

#include <algorithm>
#include <cstdio>
#include <exception>
#include <limits>
#include <memory>
#include <new>
#include <utility>

namespace
{
bool AddPixelFinalBytes(std::uint64_t& value, std::uint64_t extra) noexcept
{
    if (extra > (std::numeric_limits<std::uint64_t>::max)() - value)
        return false;
    value += extra;
    return true;
}

#pragma warning(push)
#pragma warning(disable : 4324)
struct PixelFinalPayload final : DeferredReleaseNode
{
    // These accounting aliases die last, after every owned graph/history/row payload.
    std::shared_ptr<SharedPublicationMemory> finalMemory;
    std::shared_ptr<SharedPublicationMemory> temporaryMemory;
    std::optional<PreparedPixelMoveClosure> closure;
    std::shared_ptr<const PreparedLayerRows> previousRowsAtApply;
    OriginalDisplayPublication::RowInput rows;
    OriginalDisplayPublication::Bounds bounds;
    std::unique_ptr<OriginalDisplayPublication> display;
    bool originalCaptured = false;
    bool shutdownJoined = false;

    explicit PixelFinalPayload(SelectionPixelFinalWork work)
        : finalMemory(std::move(work.finalMemory)), temporaryMemory(std::move(work.temporaryMemory)),
          closure(std::move(work.closure))
    {
        bytes = (std::max)(work.remainingCpuBytes, kDeferredReleaseThreshold);
        destroy = [](DeferredReleaseNode* raw) noexcept
        {
            auto* value = static_cast<PixelFinalPayload*>(raw);
            if (value->display && value->shutdownJoined && value->originalCaptured &&
                !value->display->RetireJoinedShutdownForWork())
                std::terminate();
            const std::unique_ptr<PixelFinalPayload> owned(value);
        };
    }
};
#pragma warning(pop)

struct RetirePixelFinalPayload final
{
    void operator()(PixelFinalPayload* value) const noexcept
    {
        if (value && !TryDeferRelease(*value, DeferredReleasePolicy::AlwaysOnOwner))
            value->destroy(value);
    }
};
} // namespace

// This is a stage object inside SelectionOutlineGestureJob, never a submitted younger job.
class Application::PixelFinalJob final : public Job
{
    enum class Stage
    {
        Intro,
        AllocateDisplay,
        AwaitRows,
        Plan,
        AwaitSlot,
        Capture,
        PrepareDisplay,
        Record,
        AwaitTransport,
        RetireTransport,
        RefreshHistory,
        Publish,
        RetireGraph,
        Destroy
    };
    Application& _owner;
    SelectionOutlineGestureState& _result;
    const std::uint32_t _ownerThread;
    std::uint64_t _cpuBytes;
    std::uint64_t _finalBytes;
    const std::uint64_t _displayCpuBytes, _displayGpuBytes;
    std::uint64_t _collapseRevision = 0;
    Stage _stage = Stage::Intro;
    JobTicket _original;
    CancelToken _cancellation;
    std::unique_ptr<PixelFinalPayload, RetirePixelFinalPayload> _payload;
    std::shared_ptr<SharedPublicationMemory> _retirementMemory;
    DocumentHistory::ResidencyPermit _residency;
    JobResult _terminal;
    bool _applied = false;
    bool _detached = false;
    bool _shutdownJoined = false;

    void AcceptOriginal(JobTicket ticket) noexcept
    {
        if (!ticket.generation || (_original.generation && _original != ticket))
            std::terminate();
        _original = ticket;
    }
    bool Resume(Stage stage, JobExecutionLane lane = JobExecutionLane::Compute) noexcept
    {
        _stage = stage;
        if (!_owner._jobs.Resume(_original, lane))
            std::terminate();
        return true;
    }
    void RememberFailure(JobResult result) noexcept
    {
        if (_terminal.status == JobStatus::Finished && result.status != JobStatus::Finished)
        {
            if (_owner._processingTestProbe)
                std::printf("INFO pixel-final.failure stage=%u status=%u error=%08X cpu=%llu final=%llu "
                            "displayCpu=%llu displayGpu=%llu\n",
                            static_cast<unsigned>(_stage), static_cast<unsigned>(result.status),
                            static_cast<unsigned>(result.error), static_cast<unsigned long long>(_cpuBytes),
                            static_cast<unsigned long long>(_finalBytes),
                            static_cast<unsigned long long>(_displayCpuBytes),
                            static_cast<unsigned long long>(_displayGpuBytes));
            _terminal = result;
        }
    }
    static void Wake(void* raw) noexcept
    {
        auto& value = *static_cast<PixelFinalJob*>(raw);
        if (!value._owner._shutdownStarted &&
            !value._owner._jobs.RequestUiActionSource(value._owner._continuationAction))
            std::terminate();
    }
    bool BeginRetirement() noexcept
    {
        _result.pixelFinalWaitingForDisplay = false;
        _residency.Reset();
        if (_payload && _payload->display && !_detached)
        {
            if (!_payload->display->DetachForRetirementOwner())
                std::terminate();
            _detached = true;
        }
        return Resume(Stage::RetireGraph);
    }
    bool Fail(HRESULT error) noexcept
    {
        RememberFailure({error == E_ABORT ? JobStatus::Cancelled : JobStatus::Failed, error});
        return BeginRetirement();
    }
    [[nodiscard]] bool RowsReadyOwner() const noexcept
    {
        return _owner._preparedLayerRows && _owner._layerRowsPublishedKey &&
               _owner._layerRowsPublishedKey.value() == _owner.CurrentLayerRowsKey();
    }
    [[nodiscard]] bool RowsFailedOwner() const noexcept
    {
        // Retry belongs to the requested epoch. A late old-row failure cannot reject a newer key.
        return _owner._layerRowsRetryAvailable && !_owner._layerRowsTicket && !_owner._layerRowsSource.HasReady() &&
               _owner._layerRowsRequestedKey && _owner._layerRowsRequestedKey.value() == _owner.CurrentLayerRowsKey();
    }
    bool PlanOwner() noexcept;
    bool PublishOwner() noexcept;
    static void RowsAdopted(void* raw) noexcept
    {
        auto& value = *static_cast<PixelFinalJob*>(raw);
        auto& owner = value._owner;
        owner._layerRowsPublishedKey = owner.CurrentLayerRowsKey();
        owner._layerRowsRequestedKey = owner._layerRowsPublishedKey;
        owner._layerRowsRetryAvailable = false;
        owner._layerRowsCacheTicket = value._original;
        // The real chrome consumer must borrow the same prepared epoch before the final GPU callback.
        // Initial source setup already warmed its small captions; same-key sync admits no younger row job.
        owner.SyncPreparedLayerRows();
    }

  public:
    PixelFinalJob(Application& owner, SelectionOutlineGestureState& result, SelectionPixelFinalWork work)
        : _owner(owner), _result(result), _ownerThread(work.ownerThread), _cpuBytes(work.remainingCpuBytes),
          _finalBytes(work.finalOutputBytes), _displayCpuBytes(work.displayCpuBytes),
          _displayGpuBytes(work.displayGpuBytes)
    {
        if (!_ownerThread || GetCurrentThreadId() == _ownerThread)
            std::terminate();
        auto payload = std::make_unique<PixelFinalPayload>(std::move(work));
        _payload.reset(payload.release());
        _retirementMemory = _payload->temporaryMemory;
    }
    ~PixelFinalJob() override
    {
        // All borrowed callback sources already detached. No result-state access after parent Terminal.
        if (_owner._pixelFinalCurrent == this)
            _owner._pixelFinalCurrent = nullptr;
    }
    JobResult Run(JobContext& context) override
    {
        if (GetCurrentThreadId() == _ownerThread ||
            (_stage != Stage::RetireGraph && _stage != Stage::Destroy && context.reservedMemoryBytes != _cpuBytes))
            return {JobStatus::Failed, E_UNEXPECTED};
        switch (_stage)
        {
        case Stage::AllocateDisplay:
            _cancellation = context.cancellation;
            _payload->display =
                std::make_unique<OriginalDisplayPublication>(_ownerThread, _payload->bounds, Wake, this);
            return {};
        case Stage::PrepareDisplay:
        {
            const auto* final = _payload->closure->FinalDocument();
            const auto prepared =
                final ? _payload->display->PrepareForWork(*final, std::move(_payload->rows), context.cancellation)
                      : JobResult{JobStatus::Failed, E_UNEXPECTED};
            if (prepared.status == JobStatus::Finished && _result.pixelFinalPreparedForTest)
                _result.pixelFinalPreparedForTest(_result.workObserver, context.shutdownEvent);
            return prepared;
        }
        case Stage::Record:
            return _terminal.status == JobStatus::Finished ? _payload->display->RecordForWork(context.cancellation)
                                                           : _terminal;
        case Stage::RefreshHistory:
            ++_result.residencyRefreshes;
            _result.NotifyWork(SelectionOutlineGestureState::WorkStage::ResidencyRefreshStarted);
            return _payload->closure->RefreshHistoryResidency(context.cancellation)
                       ? JobResult{}
                       : JobResult{context.cancellation.IsCancelled() ? JobStatus::Cancelled : JobStatus::Failed,
                                   E_ABORT};
        case Stage::Plan:
        case Stage::Capture:
        case Stage::RetireTransport:
        case Stage::RetireGraph:
        case Stage::Destroy:
            return {}; // Actual destruction lives in the unconditional worker hook, including skipped Runs.
        default:
            return {JobStatus::Failed, E_UNEXPECTED};
        }
    }
    void RetireWorkerStage(JobContext& context, JobResult result) noexcept override
    {
        // The parent constructs this delegate in its original closure Run. Its unconditional first
        // worker hook carries that exact cancellation token even before display allocation/planning.
        if (_stage == Stage::Intro)
            _cancellation = context.cancellation;
        if (_stage == Stage::Record)
            _payload->display->FinishRecordingForWork(result);
        else if (_stage == Stage::RetireTransport)
        {
            if (!_payload->display->RetireTransportForWork())
                std::terminate();
        }
        else if (_stage == Stage::RetireGraph)
        {
            if (_payload->display && !_payload->display->RetireGraphForWork())
                std::terminate();
            _payload->closure.reset();
            _payload->previousRowsAtApply.reset();
            _payload->rows = {};
            _payload->finalMemory.reset();
            _payload->temporaryMemory.reset();
        }
        else if (_stage == Stage::Destroy)
            _payload.reset();
    }
    bool DeferRetirement(JobResult result, JobTicket ticket) noexcept override
    {
        AcceptOriginal(ticket);
        if (_stage == Stage::Record)
        {
            if (!_payload->display->RecordingJoinedOwner())
                std::terminate();
            RememberFailure(result);
            const auto error = _payload->display->FailureOwner();
            if (FAILED(error))
                RememberFailure({error == E_ABORT ? JobStatus::Cancelled : JobStatus::Failed, error});
            _stage = Stage::AwaitTransport;
            Wake(this);
            return true;
        }
        if (_stage == Stage::RetireGraph)
        {
            if (_payload->display && _payload->originalCaptured && !_payload->display->FinishRetirementOwner())
                std::terminate();
            return Resume(Stage::Destroy);
        }
        if (_stage == Stage::Destroy)
            return false;
        if (result.status != JobStatus::Finished)
        {
            RememberFailure(result);
            return BeginRetirement();
        }
        return false;
    }
    bool DeferCompletion(JobResult, JobTicket ticket) noexcept override
    {
        AcceptOriginal(ticket);
        switch (_stage)
        {
        case Stage::Intro:
        case Stage::Plan:
            return PlanOwner();
        case Stage::AllocateDisplay:
        case Stage::Capture:
        {
            if (!_owner._session.IsPreparedDeferredPixelMoveClosureCurrent(*_payload->closure))
                return Fail(E_ABORT);
            _result.pixelFinalWaitingForDisplay = false;
            // A failed current-source rebuild has no future readiness to wait for. Preserve the
            // original pixels/history and retire this original, rather than stranding it until Close.
            const HRESULT sourceFailure = _owner.CurrentCommittedDisplayFailureOwner();
            if (!_owner._committedDisplay.HasCurrentPublicationOwner() && !_owner._committedDisplay.HasPending() &&
                FAILED(sourceFailure))
                return Fail(sourceFailure);
            // Recovery may still be preparing/retiring the committed source in this two-cell controller.
            // Keep this exact original suspended until actual source/retirement readiness frees its slot.
            // No source/device capture, memory query, younger job, polling or guard bypass occurs here.
            if (!_owner._committedDisplay.HasCurrentPublicationOwner() || _owner._committedDisplay.HasPending())
            {
                _stage = Stage::AwaitSlot;
                _result.pixelFinalWaitingForDisplay = true;
                _owner._jobs.NotifyOwnerWork();
                return true;
            }
            CommittedDisplayDescriptor descriptor;
            if (!_owner.DescribeCommittedDisplayOwner(descriptor))
                return Fail(E_ABORT);
            const auto status =
                _payload->display->CaptureOwner(_owner._jobs, _original, _owner._gpuWork, _owner._recordedCommands,
                                                _owner._committedDisplay, descriptor);
            _payload->originalCaptured = _payload->display->OriginalOwner().generation != 0;
            return FAILED(status) ? Fail(status) : Resume(Stage::PrepareDisplay);
        }
        case Stage::PrepareDisplay:
        {
            const auto status = _payload->display->AdmitRecordingOwner();
            if (FAILED(status))
            {
                RememberFailure({status == E_ABORT ? JobStatus::Cancelled : JobStatus::Failed, status});
                if (!_payload->display->HasRecorderOwner())
                    return BeginRetirement();
            }
            return Resume(Stage::Record, JobExecutionLane::GpuRecording);
        }
        case Stage::RetireTransport:
        case Stage::RefreshHistory:
        case Stage::Publish:
            return _terminal.status == JobStatus::Finished ? PublishOwner() : BeginRetirement();
        case Stage::Destroy:
            return false;
        default:
            std::terminate();
        }
    }
    [[nodiscard]] bool HasReadyOwner() const noexcept
    {
        if (_owner._shutdownStarted)
            return false;
        if (_stage == Stage::AwaitRows)
            return _cancellation.IsCancelled() || RowsReadyOwner() || RowsFailedOwner() ||
                   !_owner._session.IsPreparedDeferredPixelMoveClosureCurrent(*_payload->closure);
        if (_stage == Stage::AwaitSlot)
            return _cancellation.IsCancelled() ||
                   (!_owner._committedDisplay.HasPending() && (_owner._committedDisplay.HasCurrentPublicationOwner() ||
                                                               FAILED(_owner.CurrentCommittedDisplayFailureOwner())));
        return _stage == Stage::AwaitTransport && _payload && _payload->display->TransportReadyOwner();
    }
    void AdvanceOwner() noexcept
    {
        if (_stage == Stage::AwaitRows)
        {
            if (HasReadyOwner())
                (void)Resume(Stage::Plan); // The same original gets its own successful/skipped acknowledgement.
            return;
        }
        if (_stage == Stage::AwaitSlot)
        {
            if (HasReadyOwner())
                (void)Resume(Stage::Capture); // No-op/skipped worker gives the exact successful capture callback.
            return;
        }
        // Phase first: an unfinished worker may be writing its payload. This action never polls/rearms.
        if (_stage != Stage::AwaitTransport || !_payload || !_payload->display->TransportReadyOwner())
            return;
        if (!_payload->display->AuthorizeTransportRetirementOwner())
            std::terminate();
        (void)Resume(Stage::RetireTransport);
    }
    void Completed(JobResult result) noexcept override
    {
        if (_stage != Stage::Destroy || _payload)
            std::terminate();
        if (_retirementMemory && !_retirementMemory->IsBound() &&
            FAILED(_retirementMemory->BindForRetirement(_owner._jobs, _original)))
            std::terminate();
        _result.pixelFinalClosed = _applied;
        _result.pixelFinalOutcome = _applied                                  ? JobResult{}
                                    : _terminal.status == JobStatus::Finished ? result
                                                                              : _terminal;
        if (_owner._pixelFinalCurrent == this)
            _owner._pixelFinalCurrent = nullptr;
    }
    void RetainJoinedRetirement(JobScheduler& jobs, JobTicket ticket) noexcept override
    {
        AcceptOriginal(ticket);
        if (&jobs != &_owner._jobs || !jobs.InJoinedRetirementCallback(ticket) || !_owner._shutdownStarted ||
            _shutdownJoined)
            std::terminate();
        _residency.Reset();
        if (_retirementMemory && !_retirementMemory->IsBound() &&
            FAILED(_retirementMemory->BindForRetirement(jobs, ticket)))
            std::terminate();
        if (_payload)
        {
            if (_payload->display && _payload->display->OriginalOwner().generation)
            {
                _payload->originalCaptured = true;
                if (!_payload->display->JoinedShutdownOwner(_owner._pixelFinalShutdownGpu))
                    std::terminate();
            }
            _payload->shutdownJoined = true;
        }
        _result.pixelFinalClosed = _applied;
        _result.pixelFinalWaitingForDisplay = false;
        _shutdownJoined = true;
    }
};

bool Application::AddPixelFinalAdmissionOwner(SelectionOutlineGestureMemory& memory, std::uint64_t& cpuBound,
                                              std::uint64_t& gpuBound) const noexcept
{
    const auto& document = std::as_const(_session).Document();
    if (!document || !_preparedLayerRowsEnabled || !_committedDisplayInitialized || !_recordedCommandsInitialized ||
        !_selectionPixelFinalPublicationEnabled)
        return false;
    RasterResidencyPlanOptions options;
    options.deviceGeneration = _gpuWork.DeviceGeneration();
    const auto cpu = RasterResidencyCpuAdmission::Estimate(*document, options);
    const auto gpu = RasterResidencyAdmission::Estimate(*document, options);
    // Pixel closure replaces one existing layer and selection; layer count/canvas/labels do not grow.
    // Its already proved final CPU output bound F covers new raster/mask/selection/source ownership.
    // Thus the future source is bounded by current source+F. Source contributes once to CPU planning,
    // twice to the mip-chain GPU bound, and once to the row graph's retained source. Reserve every
    // remembered collapse ID (<=kMaxLayerCount), even if collapse choices change while the press runs.
    const auto rows = LayerRowsAdmission::Estimate(*document, kMaxLayerCount, _layerRowsHiddenPattern);
    const auto allowance = PreparedLayerRows::OwnershipAllowance(document->layers.size());
    const auto final = memory.limits.finalOutputBytes;
    if (!cpu.IsValid() || !gpu.IsValid() || !rows.IsValid() || !allowance || !final)
        return false;
    auto gpuBytes = gpu.GpuBytes();
    auto cpuBytes = cpu.CpuBytes();
    if (!AddPixelFinalBytes(gpuBytes, final) || !AddPixelFinalBytes(gpuBytes, final) ||
        !AddPixelFinalBytes(cpuBytes, final) || !AddPixelFinalBytes(cpuBytes, rows.Bytes()) ||
        !AddPixelFinalBytes(cpuBytes, final) || !AddPixelFinalBytes(cpuBytes, allowance) ||
        !AddPixelFinalBytes(cpuBytes, sizeof(OriginalDisplayPublication) + 32768) ||
        (_renderer.IsWarp() && !AddPixelFinalBytes(cpuBytes, gpuBytes)))
        return false;
    auto total = memory.limits.totalBytes;
    auto output = final;
    if (!AddPixelFinalBytes(total, cpuBytes) || !AddPixelFinalBytes(output, cpuBytes) || output >= total)
        return false;
    memory.limits.totalBytes = total;
    memory.limits.finalOutputBytes = output;
    cpuBound = cpuBytes;
    gpuBound = gpuBytes;
    return true;
}

bool Application::PixelFinalJob::PlanOwner() noexcept
{
    if (!_owner._selectionPixelFinalPublicationEnabled || !_owner._committedDisplayInitialized ||
        !_owner._recordedCommandsInitialized || (_owner._pixelFinalCurrent && _owner._pixelFinalCurrent != this) ||
        !_payload || !_payload->closure ||
        !_owner._session.IsPreparedDeferredPixelMoveClosureCurrent(*_payload->closure))
        return Fail(E_ABORT);
    _owner._pixelFinalCurrent = this;
    if (!RowsReadyOwner())
    {
        if (RowsFailedOwner())
            return Fail(E_FAIL);
        // First-row/collapse refresh is a separately admitted producer. Preserve its semantic item
        // identities by waiting for its actual epoch before capturing previous rows. No new request,
        // capture, reservation growth, periodic wake or younger final-edit job is created here.
        _stage = Stage::AwaitRows;
        _result.pixelFinalWaitingForDisplay = true;
        _owner._jobs.NotifyOwnerWork();
        return true;
    }
    _result.pixelFinalWaitingForDisplay = false;
    const auto* final = _payload->closure->FinalDocument();
    if (!final)
        return Fail(E_UNEXPECTED);
    RasterResidencyPlanOptions options;
    options.deviceGeneration = _owner._gpuWork.DeviceGeneration();
    const auto cpu = RasterResidencyCpuAdmission::Estimate(*final, options);
    const auto gpu = RasterResidencyAdmission::Estimate(*final, options);
    const auto& collapsed = std::as_const(_owner._session).CollapsedGroupIDs();
    const auto rows = LayerRowsAdmission::Estimate(*final, collapsed.size(), _owner._layerRowsHiddenPattern);
    const auto allowance = PreparedLayerRows::OwnershipAllowance(final->layers.size());
    if (!cpu.IsValid() || !gpu.IsValid() || !rows.IsValid() || !allowance ||
        final->layers.size() > (std::numeric_limits<std::uint64_t>::max)() - _owner._nextLayerRowItemId)
        return Fail(E_OUTOFMEMORY);
    auto extra = cpu.CpuBytes();
    if (!AddPixelFinalBytes(extra, rows.Bytes()) || !AddPixelFinalBytes(extra, allowance) ||
        !AddPixelFinalBytes(extra, sizeof(OriginalDisplayPublication) + 32768) ||
        (_owner._renderer.IsWarp() && !AddPixelFinalBytes(extra, gpu.GpuBytes())))
        return Fail(E_OUTOFMEMORY);
    if (!extra || extra > _displayCpuBytes || gpu.GpuBytes() > _displayGpuBytes || _finalBytes < _displayCpuBytes ||
        _finalBytes >= _cpuBytes)
        return Fail(E_OUTOFMEMORY);
    // Original admission is final: never query memory or grow after a continuing output transferred.
    _payload->bounds = {_displayCpuBytes, _displayGpuBytes};
    _collapseRevision = _owner._session.CollapseRevision();
    try
    {
        _payload->rows.collapsed.assign(collapsed.begin(), collapsed.end());
        _payload->rows.hiddenPattern = _owner._layerRowsHiddenPattern;
        _payload->rows.previous = _owner._preparedLayerRows;
    }
    catch (const std::bad_alloc&)
    {
        return Fail(E_OUTOFMEMORY); // Explicit complete-admission capture; retain partial inputs for worker teardown.
    }
    _payload->rows.documentSerial = _owner._session.DocumentSerial();
    _payload->rows.firstItemId = _owner._nextLayerRowItemId;
    _owner._nextLayerRowItemId += final->layers.size();
    return Resume(Stage::AllocateDisplay);
}

bool Application::PixelFinalJob::PublishOwner() noexcept
{
    if (!_payload || !_payload->closure ||
        !_owner._session.IsPreparedDeferredPixelMoveClosureCurrent(*_payload->closure) ||
        _collapseRevision != _owner._session.CollapseRevision())
        return Fail(E_ABORT);
    ++_result.residencyTries;
    const auto decision = _payload->closure->TryAcquireHistoryResidency(_residency);
    if (decision == DocumentHistory::ResidencyValidation::Refresh ||
        decision == DocumentHistory::ResidencyValidation::Busy)
    {
        _residency.Reset();
        return Resume(Stage::RefreshHistory);
    }
    if (decision != DocumentHistory::ResidencyValidation::Ready)
        return Fail(E_ABORT);
    const auto* final = _payload->closure->FinalDocument();
    if (!final)
        return Fail(E_UNEXPECTED);
    // One original transfer covers final pixels/history/rows/graph; its temporary sibling protects all
    // retiring and expired weak controls. No useful work is admitted after this point.
    HRESULT status = _payload->finalMemory->BindPersistentOutputForPublication(_owner._jobs, _original, _finalBytes,
                                                                               *_payload->temporaryMemory);
    if (SUCCEEDED(status))
        status = _payload->display->PreparePublicationOwner(_payload->finalMemory, *final);
    if (FAILED(status))
        return Fail(status);
    if (!_owner._session.ApplyPreparedDeferredPixelMoveClosureIfCurrent(*_payload->closure))
        std::terminate(); // Prevalidated under the exact residency permit, without nested dispatch.
    _applied = true;
    _payload->previousRowsAtApply = std::move(_owner._preparedLayerRows);
    if (!_payload->display->AdoptOwner(_owner._preparedLayerRows, RowsAdopted, this))
        std::terminate();
    _residency.Reset();
    _result.pixelFinalClosed = true;
    return BeginRetirement();
}

std::unique_ptr<Job> Application::MakePixelFinalForWork(void* host, SelectionOutlineGestureState& result,
                                                        SelectionPixelFinalWork work)
{
    static_assert(sizeof(PixelFinalJob) + sizeof(PixelFinalPayload) + 4096 <= 16384,
                  "Native wrapper allocation must fit the original pixel-gesture wrapper allowance.");
    if (!host || !work.ownerThread)
        return {};
    // This worker constructs owners only. It never reads the host/session/window/device.
    return std::make_unique<PixelFinalJob>(*static_cast<Application*>(host), result, std::move(work));
}

void Application::PumpPixelFinalPublicationOwner() noexcept
{
    if (!_shutdownStarted && _pixelFinalCurrent)
        _pixelFinalCurrent->AdvanceOwner();
}

bool Application::PixelFinalPublicationPendingOwner() const noexcept
{
    return _pixelFinalCurrent != nullptr;
}

bool Application::PixelFinalPublicationReadyOwner() const noexcept
{
    return _pixelFinalCurrent && _pixelFinalCurrent->HasReadyOwner();
}
