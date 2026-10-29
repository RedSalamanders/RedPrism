#include "Application.h"

#include "FinalEditSource.h"
#include "FinalPublicationWork.h"
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
// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4625 4626 5027)
struct PixelFinalPayload final : DeferredReleaseNode
{
    // These accounting aliases die last, after every owned graph/history/row payload.
    std::shared_ptr<SharedPublicationMemory> finalMemory;
    std::shared_ptr<SharedPublicationMemory> temporaryMemory;
    std::optional<FinalEditSource> source;
    std::unique_ptr<DeferredReleaseNode, DestroyFinalParentForWork> parent;
    std::shared_ptr<const PreparedLayerRows> previousRowsAtApply;
    OriginalDisplayPublication::RowInput rows;
    OriginalDisplayPublication::Bounds bounds;
    std::unique_ptr<OriginalDisplayPublication> display;
    static FinalEditSource TakeSource(SelectionPixelFinalWork&& work) noexcept
    {
        return FinalEditSource(std::move(work.closure));
    }
    static FinalEditSource TakeSource(SizeFinalPublicationWork&& work) noexcept
    {
        return FinalEditSource(std::move(work.publication));
    }
    static std::unique_ptr<DeferredReleaseNode, DestroyFinalParentForWork> TakeParent(
        SelectionPixelFinalWork&&) noexcept
    {
        return {};
    }
    static std::unique_ptr<DeferredReleaseNode, DestroyFinalParentForWork> TakeParent(
        SizeFinalPublicationWork&& work) noexcept
    {
        return std::move(work.parent);
    }
    bool originalCaptured = false;
    bool shutdownJoined = false;

    template <class Work>
    explicit PixelFinalPayload(Work&& work)
        : finalMemory(std::move(work.finalMemory)), temporaryMemory(std::move(work.temporaryMemory)),
          source(TakeSource(std::move(work))), parent(TakeParent(std::move(work)))
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

// This stage belongs to its already admitted selection/Size original, never a submitted younger job.
// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4625 4626)
class Application::FinalPublicationJob final : public Job
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
    const FinalPublicationCallbacks _callbacks;
    const std::uint32_t _ownerThread;
    std::uint64_t _cpuBytes;
    std::uint64_t _finalBytes;
    std::uint64_t _displayCpuBytes, _displayGpuBytes;
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

    [[nodiscard]] bool CurrentOwner() const noexcept
    {
        return _payload && _payload->source && _payload->source->IsCurrent(_owner._session) &&
               (!_callbacks.currentOwner || _callbacks.currentOwner(_callbacks.context));
    }
    void SetWaitingOwner(bool value) noexcept
    {
        if (_callbacks.waitingOwner)
            _callbacks.waitingOwner(_callbacks.context, value);
    }
    void CompleteParentOwner(JobResult result) noexcept
    {
        if (_callbacks.completedOwner)
            _callbacks.completedOwner(_callbacks.context, _applied,
                                      _applied                                  ? JobResult{}
                                      : _terminal.status == JobStatus::Finished ? result
                                                                                : _terminal);
    }
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
        auto& value = *static_cast<FinalPublicationJob*>(raw);
        if (!value._owner._shutdownStarted &&
            !value._owner._jobs.RequestUiActionSource(value._owner._continuationAction))
            std::terminate();
    }
    bool BeginRetirement() noexcept
    {
        SetWaitingOwner(false);
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
        auto& value = *static_cast<FinalPublicationJob*>(raw);
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
    template <class Work>
    FinalPublicationJob(Application& owner, FinalPublicationCallbacks callbacks, Work&& work)
        : _owner(owner), _callbacks(callbacks), _ownerThread(work.ownerThread), _cpuBytes(work.remainingCpuBytes),
          _finalBytes(work.finalOutputBytes), _displayCpuBytes(work.displayCpuBytes),
          _displayGpuBytes(work.displayGpuBytes)
    {
        if (!_ownerThread || GetCurrentThreadId() == _ownerThread)
            std::terminate();
        auto payload = std::make_unique<PixelFinalPayload>(std::move(work));
        _payload.reset(payload.release());
        _retirementMemory = _payload->temporaryMemory;
    }
    ~FinalPublicationJob() override
    {
        // All borrowed callback sources already detached. No result-state access after parent Terminal.
        if (_owner._finalPublicationCurrent == this)
            _owner._finalPublicationCurrent = nullptr;
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
            const auto* final = _payload->source->FinalDocument();
            const auto prepared =
                final ? _payload->display->PrepareForWork(*final, std::move(_payload->rows), context.cancellation)
                      : JobResult{JobStatus::Failed, E_UNEXPECTED};
            if (prepared.status == JobStatus::Finished && _callbacks.preparedForWork)
                _callbacks.preparedForWork(_callbacks.context, context.shutdownEvent);
            return prepared;
        }
        case Stage::Record:
            return _terminal.status == JobStatus::Finished ? _payload->display->RecordForWork(context.cancellation)
                                                           : _terminal;
        case Stage::RefreshHistory:
            if (_callbacks.refreshForWork)
                _callbacks.refreshForWork(_callbacks.context);
            return _payload->source->RefreshHistoryResidency(context.cancellation)
                       ? JobResult{}
                       : JobResult{context.cancellation.IsCancelled() ? JobStatus::Cancelled : JobStatus::Failed,
                                   E_ABORT};
        case Stage::Plan:
        case Stage::Capture:
        case Stage::RetireTransport:
        case Stage::RetireGraph:
        case Stage::Destroy:
            return {}; // Actual destruction lives in the unconditional worker hook, including skipped Runs.
        case ::Application::FinalPublicationJob::Stage::AwaitRows:
        case ::Application::FinalPublicationJob::Stage::AwaitSlot:
        case ::Application::FinalPublicationJob::Stage::AwaitTransport:
        case ::Application::FinalPublicationJob::Stage::Intro:
        case ::Application::FinalPublicationJob::Stage::Publish:
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
            _payload->source.reset();
            _payload->parent.reset();
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
            if (!CurrentOwner())
                return Fail(E_ABORT);
            SetWaitingOwner(false);
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
                SetWaitingOwner(true);
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
            if (!CurrentOwner())
                return Fail(E_ABORT);
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
        case ::Application::FinalPublicationJob::Stage::AwaitRows:
        case ::Application::FinalPublicationJob::Stage::AwaitSlot:
        case ::Application::FinalPublicationJob::Stage::AwaitTransport:
        case ::Application::FinalPublicationJob::Stage::Record:
        case ::Application::FinalPublicationJob::Stage::RetireGraph:
        default:
            std::terminate();
        }
    }
    [[nodiscard]] bool HasReadyOwner() const noexcept
    {
        if (_owner._shutdownStarted)
            return false;
        if (_stage == Stage::AwaitRows)
            return _cancellation.IsCancelled() || !CurrentOwner() || RowsReadyOwner() || RowsFailedOwner() ||
                   !CurrentOwner();
        if (_stage == Stage::AwaitSlot)
            return _cancellation.IsCancelled() || !CurrentOwner() ||
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
        CompleteParentOwner(result);
        if (_owner._finalPublicationCurrent == this)
            _owner._finalPublicationCurrent = nullptr;
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
                if (!_payload->display->JoinedShutdownOwner(_owner._finalPublicationShutdownGpu))
                    std::terminate();
            }
            _payload->shutdownJoined = true;
        }
        CompleteParentOwner({JobStatus::Cancelled, E_ABORT});
        SetWaitingOwner(false);
        _shutdownJoined = true;
    }
};
#pragma warning(pop)

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

bool Application::FinalPublicationJob::PlanOwner() noexcept
{
    if (!_owner._selectionPixelFinalPublicationEnabled || !_owner._committedDisplayInitialized ||
        !_owner._recordedCommandsInitialized ||
        (_owner._finalPublicationCurrent && _owner._finalPublicationCurrent != this) || !_payload ||
        !_payload->source || !CurrentOwner())
        return Fail(E_ABORT);
    _owner._finalPublicationCurrent = this;
    if (!RowsReadyOwner())
    {
        if (RowsFailedOwner())
            return Fail(E_FAIL);
        // First-row/collapse refresh is a separately admitted producer. Preserve its semantic item
        // identities by waiting for its actual epoch before capturing previous rows. No new request,
        // capture, reservation growth, periodic wake or younger final-edit job is created here.
        _stage = Stage::AwaitRows;
        SetWaitingOwner(true);
        _owner._jobs.NotifyOwnerWork();
        return true;
    }
    SetWaitingOwner(false);
    const auto* final = _payload->source->FinalDocument();
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
    if (_payload->source->SourceKind() == FinalEditSource::Kind::Size && !_displayCpuBytes && !_displayGpuBytes)
    {
        auto total = _cpuBytes;
        auto output = _finalBytes;
        if (!AddPixelFinalBytes(total, extra) || !AddPixelFinalBytes(output, extra) || output >= total)
            return Fail(E_OUTOFMEMORY);
        // Size exposes no continuing output. Grow only this original from the exact immutable final
        // descriptor, before row/source/device capture or any new graph allocation. Refusal is final.
        const auto grown = static_cast<HRESULT>(_owner._jobs.IncreaseReservation(_original, total));
        if (FAILED(grown))
            return Fail(grown);
        _cpuBytes = total;
        _finalBytes = output;
        _displayCpuBytes = extra;
        _displayGpuBytes = gpu.GpuBytes();
    }
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

bool Application::FinalPublicationJob::PublishOwner() noexcept
{
    if (!_payload || !_payload->source || !CurrentOwner() || _collapseRevision != _owner._session.CollapseRevision())
        return Fail(E_ABORT);
    if (_callbacks.residencyTryOwner)
        _callbacks.residencyTryOwner(_callbacks.context);
    const auto decision = _payload->source->TryAcquireHistoryResidency(_residency);
    if (decision == DocumentHistory::ResidencyValidation::Refresh ||
        decision == DocumentHistory::ResidencyValidation::Busy)
    {
        _residency.Reset();
        return Resume(Stage::RefreshHistory);
    }
    if (decision != DocumentHistory::ResidencyValidation::Ready)
        return Fail(E_ABORT);
    const auto* final = _payload->source->FinalDocument();
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
    if (!_payload->source->ApplyIfCurrent(_owner._session))
        std::terminate(); // Prevalidated under the exact residency permit, without nested dispatch.
    _applied = true;
    if (_callbacks.appliedOwner)
        _callbacks.appliedOwner(_callbacks.context);
    _payload->previousRowsAtApply = std::move(_owner._preparedLayerRows);
    if (!_payload->display->AdoptOwner(_owner._preparedLayerRows, RowsAdopted, this))
        std::terminate();
    _residency.Reset();

    return BeginRetirement();
}

std::unique_ptr<Job> Application::MakePixelFinalForWork(void* host, SelectionOutlineGestureState& result,
                                                        SelectionPixelFinalWork work)
{
    static_assert(sizeof(FinalPublicationJob) + sizeof(PixelFinalPayload) + 4096 <= 16384,
                  "Native wrapper allocation must fit the original pixel-gesture wrapper allowance.");
    if (!host || !work.ownerThread)
        return {};
    // This worker constructs owners only. It never reads the host/session/window/device.
    FinalPublicationCallbacks callbacks;
    callbacks.context = &result;
    callbacks.waitingOwner = [](void* raw, bool waiting) noexcept
    { static_cast<SelectionOutlineGestureState*>(raw)->pixelFinalWaitingForDisplay = waiting; };
    callbacks.residencyTryOwner = [](void* raw) noexcept
    { ++static_cast<SelectionOutlineGestureState*>(raw)->residencyTries; };
    callbacks.refreshForWork = [](void* raw) noexcept
    {
        auto& state = *static_cast<SelectionOutlineGestureState*>(raw);
        ++state.residencyRefreshes;
        state.NotifyWork(SelectionOutlineGestureState::WorkStage::ResidencyRefreshStarted);
    };
    callbacks.preparedForWork = [](void* raw, void* shutdown) noexcept
    {
        auto& state = *static_cast<SelectionOutlineGestureState*>(raw);
        if (state.pixelFinalPreparedForTest)
            state.pixelFinalPreparedForTest(state.workObserver, shutdown);
    };
    callbacks.appliedOwner = [](void* raw) noexcept
    { static_cast<SelectionOutlineGestureState*>(raw)->pixelFinalClosed = true; };
    callbacks.completedOwner = [](void* raw, bool applied, JobResult outcome) noexcept
    {
        auto& state = *static_cast<SelectionOutlineGestureState*>(raw);
        state.pixelFinalClosed = applied;
        state.pixelFinalOutcome = outcome;
    };
    return std::make_unique<FinalPublicationJob>(*static_cast<Application*>(host), callbacks, std::move(work));
}

void Application::PumpFinalPublicationOwner() noexcept
{
    if (!_shutdownStarted && _finalPublicationCurrent)
        _finalPublicationCurrent->AdvanceOwner();
}

bool Application::FinalPublicationPendingOwner() const noexcept
{
    return _finalPublicationCurrent != nullptr;
}

bool Application::FinalPublicationReadyOwner() const noexcept
{
    return _finalPublicationCurrent && _finalPublicationCurrent->HasReadyOwner();
}

std::unique_ptr<Job> Application::MakeSizeFinalForWork(void* host, FinalPublicationCallbacks callbacks,
                                                       SizeFinalPublicationWork&& work)
{
    if (!host || !work.ownerThread || !callbacks.currentOwner || !callbacks.completedOwner)
        return {};
    return std::make_unique<FinalPublicationJob>(*static_cast<Application*>(host), callbacks, std::move(work));
}
