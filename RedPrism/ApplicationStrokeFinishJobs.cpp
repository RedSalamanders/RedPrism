// Original admitted final stroke: coverage, pixels/history, rows and display share one ticket.
// Normal input activation is separately qualified with the sample pipeline.
#include "Application.h"

#include "GpuCapabilities.h"
#include "GpuRecordingCommands.h"
#include "PreparedLayerRows.h"
#include "StrokeFinishTestGate.h"
#include "StrokeFinishWork.h"
#include "resource.h"

#include <algorithm>
#include <cassert>
#include <exception>
#include <limits>
#include <memory>
#include <new>
#include <span>
#include <utility>

namespace
{
#pragma warning(push)
#pragma warning(disable : 4324)
struct FinishPayload final : DeferredReleaseNode
{
    // Last member destruction keeps the original complete final-work admission charged.
    std::shared_ptr<SharedPublicationMemory> memory;
    std::shared_ptr<StrokeFinishTestGate> gate;
    // Last member destruction: the complete Clone charge survives every CPU draft/result source borrow.
    std::unique_ptr<CloneReadbackState, CloneReadbackState::Retire> clone;
    std::optional<StrokeFinishInput> input;
    std::optional<BrushStroke::FinalCoverage> coverage;
    std::optional<StrokeFinishResult> result;
    std::unique_ptr<PreparedCommittedDisplay> display;
    std::shared_ptr<PreparedLayerRows> rows;
    std::shared_ptr<const PreparedLayerRows> previousRows;
    std::vector<Guid> collapsed;
    std::wstring hiddenPattern;
    GpuCompletionContext displayCompletion;
    wil::com_ptr_nothrow<ID3D11DeviceContext> immediate;
    GpuCommandStream displayCommands;
    GpuCompletionContext displayCommandRetirement;
    bool displayCommandsInitialized = false;
    bool displayCommandsAuthorized = false;
    bool displayCommandsRetired = false;
    bool displayConsumed = false;
    // Declaration order keeps context alive until the density graph's destructor has finished.
    GpuCompletionContext completion;
    GpuBrushCoverage::StrokeEpoch epoch;
    wil::com_ptr_nothrow<ID3D11Device> device;
    GpuCompletionContext commandRetirement;
    GpuCommandStream commands;
    bool commandsInitialized = false;
    bool commandsAuthorized = false;
    bool commandsRetired = false;
    ~FinishPayload()
    {
        if (displayCommandsInitialized && !displayCommandsRetired && !displayCommands.RetireForWork())
            std::terminate();
        if (display && !displayConsumed && !display->RetireForWork())
            std::terminate();
        // Normal stages or actual Close separately authorize this original stream before final MTA destruction.
        if (commandsInitialized && !commandsRetired && !commands.RetireForWork())
            std::terminate();
    }

    FinishPayload()
    {
        bytes = kDeferredReleaseThreshold;
        destroy = [](DeferredReleaseNode* value) noexcept
        {
            auto* payload = static_cast<FinishPayload*>(value);
            const auto gate = payload->gate;
            delete payload;
            if (gate)
            {
                gate->retiredOffOwner.store(GetCurrentThreadId() != gate->ownerThread);
                gate->retired.store(true, std::memory_order_release);
            }
        };
    }
};
#pragma warning(pop)

struct RetireFinishPayload final
{
    void operator()(FinishPayload* value) const noexcept
    {
        if (value && !TryDeferRelease(*value, DeferredReleasePolicy::AlwaysOnOwner))
            value->destroy(value);
    }
};
} // namespace

class Application::StrokeFinishJob final : public Job,
                                           public GpuCommandSourceReceiver,
                                           public GpuBufferReadbackReceiver,
                                           public GpuCompletionReceiver
{
  public:
    StrokeFinishJob(Application& owner, HistoryWorkStamp stamp, std::uint64_t hostOpening, std::uint64_t sessionOpening,
                    std::uint64_t retirement, std::uint64_t cpuBytes, GpuResidencyReservation reservation) noexcept
        : _owner(owner), _stamp(stamp), _hostOpening(hostOpening), _sessionOpening(sessionOpening),
          _retirement(retirement), _cpuBytes(cpuBytes), _gpuReservation(std::move(reservation))
    {
    }

    ~StrokeFinishJob() override
    {
        if (!_ticket.generation)
        {
            _gpuReservation.ResetOwner(); // Submission refused before any producer or source capture.
            return;
        }
        // Close has joined compute/wait producers. This destructor performs owner detachment only;
        // the still-installed deferred sink sends the complete CPU/epoch/context payload to final worker.
        // JobScheduler destroys wrappers before the application owner hook. Detach this original
        // parent here, while its seed/slot and command source still identify the joined payload.
        if (_owner._shutdownStarted && !_shutdownJoined)
            JoinedShutdownOwner();
        if (_owner._strokeFinishCurrent == this)
            _owner._strokeFinishCurrent = nullptr;
        ReleaseLoanOwner();
        if (_gpuReservation)
        {
            if (!_owner._shutdownStarted || _owner._strokeFinishShutdownGpu)
                std::terminate(); // Normal completion releases only after acknowledged worker retirement.
            _owner._strokeFinishShutdownGpu = std::move(_gpuReservation);
        }
    }

    void JoinedShutdownOwner() noexcept
    {
        if (!_owner._shutdownStarted || _shutdownJoined)
            std::terminate();
        _owner._recordedCommands.AcknowledgeJoinedShutdownOwner();
        ReleaseLoanOwner();
        auto detachStream = [&](GpuCommandStream& stream, bool initialized, bool retired, bool& authorized,
                                GpuCommandSourceLease& source, GpuCompletionContext& capture) noexcept
        {
            if (!initialized || retired)
                return;
            stream.RequestStopOwner();
            HRESULT producer = E_PENDING;
            if (!stream.ProducerFinishedOwner(producer) && !stream.AcknowledgeEmptyJoinedShutdownOwner())
                std::terminate();
            if (source.slot < GpuCommandPump::kCapacity)
            {
                if (!_owner._recordedCommands.DetachOwner(source, capture))
                    std::terminate();
                source = {};
            }
            if (!authorized && !stream.AuthorizeRetirementOwner())
                std::terminate();
            authorized = true;
        };
        if (_payload)
        {
            detachStream(_payload->commands, _payload->commandsInitialized, _payload->commandsRetired,
                         _payload->commandsAuthorized, _commandSource, _payload->commandRetirement);
            detachStream(_payload->displayCommands, _payload->displayCommandsInitialized,
                         _payload->displayCommandsRetired, _payload->displayCommandsAuthorized, _displaySource,
                         _payload->displayCommandRetirement);
            if (_payload->display && !_payload->displayConsumed && !_displayDetached)
            {
                if (!_payload->display->DetachOwner())
                    std::terminate();
                _displayDetached = true;
            }
        }
        if (_seed && !_owner._committedDisplay.ReleasePublishedSeedOwner(_seed))
            std::terminate();
        if (_publicationSlot && !_owner._committedDisplay.ReleasePublicationSlotOwner(_publicationSlot))
            std::terminate();
        _owner._strokeFinishShutdownDisplayGpu = std::move(_displayGpu);
        _shutdownJoined = true;
    }

    void BindOwnerTicket(JobTicket ticket) noexcept
    {
        _ticket = ticket;
    }

    JobResult Run(JobContext& context) override
    {
        if (_cleanupRequested)
            return {}; // RetireWorkerStage runs even if scheduler cancellation skips Run.
        if (context.cancellation.IsCancelled())
            return {JobStatus::Cancelled};
        // Owner phases never cross this boundary. Async callbacks only resume the unchanged AwaitAck action;
        // a producer acknowledgement precedes the owner's next useful action and every payload mutation.
        switch (_workerAction)
        {
        case WorkerAction::Allocate:
            _cancellation = context.cancellation; // Published once by this first producer acknowledgement.
            _payload.reset(std::make_unique<FinishPayload>().release());
            _payload->bytes = (std::max)(_retirement, kDeferredReleaseThreshold);
            _payload->memory = SharedPublicationMemory::CreateForWork();
            _payload->gate = _gate;
            return {};
        case WorkerAction::Prepare:
        {
            if (!_payload || !_payload->input)
                return {JobStatus::Failed, E_UNEXPECTED};
            _payload->coverage = _payload->input->DraftForWork().PrepareFinalCoverage(context.cancellation);
            if (_payload->coverage && _gate)
            {
                _gate->prepares.fetch_add(1);
                _gate->offowner.fetch_add(GetCurrentThreadId() != _ownerThread ? 1u : 0u);
                if (!SetEvent(_gate->reached.get()))
                    return {JobStatus::Failed, HRESULT_FROM_WIN32(GetLastError())};
                _owner._jobs.NotifyOwnerWork();
                if (_gate->holdAfterPreparation)
                {
                    const HANDLE waits[]{_gate->release.get(), context.shutdownEvent};
                    const auto waited = WaitForMultipleObjects(2, waits, FALSE, StrokeFinishTestGate::kWaitTimeoutMs);
                    if (context.cancellation.IsCancelled() || waited == WAIT_OBJECT_0 + 1)
                        return {JobStatus::Cancelled};
                    if (waited != WAIT_OBJECT_0)
                        return {JobStatus::Failed, HRESULT_FROM_WIN32(ERROR_TIMEOUT)};
                    if (_gate->failureResult)
                        return *_gate->failureResult;
                }
            }
            return _payload->coverage ? JobResult{} : FailureFor(context.cancellation);
        }
        case WorkerAction::RecordCoverage:
        {
            if (!_payload || !_payload->coverage || FAILED(_recordingAdmission))
                return {JobStatus::Failed, FAILED(_recordingAdmission) ? _recordingAdmission : E_UNEXPECTED};
            wil::com_ptr_nothrow<ID3D11DeviceContext> deferred;
            HRESULT status = _payload->device->CreateDeferredContext(0, deferred.put());
            if (FAILED(status))
                return {JobStatus::Failed, status};
            GpuRecordingCommands recording;
            status = recording.InitializeForWork(deferred.get(), _payload->commands, context.cancellation);
            if (SUCCEEDED(status))
            {
                const auto& final = *_payload->coverage;
                status = GpuBrushCoverage::RecordEpochForWork(_payload->epoch, deferred.get(), recording, final.tiles,
                                                              final.settled, {}, final.params);
            }
            return status == E_ABORT ? JobResult{JobStatus::Cancelled, E_ABORT}
                   : FAILED(status)  ? JobResult{JobStatus::Failed, status}
                                     : JobResult{};
        }
        case WorkerAction::RetireCommands:
            return {}; // Nonthrowing retirement is in RetireWorkerStage, including cancellation-skipped Run.
        case WorkerAction::Install:
            if (!_payload || !_payload->input || !_payload->coverage || !_rows.data ||
                _nextCoverage >= _payload->coverage->tiles.size())
                return {JobStatus::Failed, E_UNEXPECTED};
            _installed = _payload->input->DraftForWork().InstallFinalCoverage(
                _payload->coverage->tiles[_nextCoverage].key, {_rows.data, _rows.count});
            return _installed ? JobResult{} : FailureFor(context.cancellation);
        case WorkerAction::Compose:
            // All exact coverage loans have acknowledged. No worker calls the borrowed engine/context.
            if (!_payload || !_payload->input || !_payload->coverage ||
                !_payload->input->DraftForWork().ComposeFinalCoverage(*_payload->coverage))
                return FailureFor(context.cancellation);
            _payload->coverage.reset();
            _payload->epoch.ResetForWork(); // only after actual completion or confirmed removal
            _payload->completion = {};
            _payload->result.emplace(DocumentSession::PrepareStrokeFinishPublication(std::move(_payload->input.value()),
                                                                                     context.cancellation, {}));
            _payload->input.reset();
            if (context.cancellation.IsCancelled() || _payload->result->status == StrokeFinishStatus::Cancelled)
                return {JobStatus::Cancelled};
            if (_payload->result->status != StrokeFinishStatus::Ready &&
                _payload->result->status != StrokeFinishStatus::Unchanged)
                return {JobStatus::Failed, _payload->result->status == StrokeFinishStatus::TooLarge
                                               ? HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE)
                                               : E_INVALIDARG};
            return {};
        case WorkerAction::PrepareDisplay:
        {
            auto* publication = Publication();
            const auto* final = publication ? publication->FinalDocument() : nullptr;
            if (!final)
                return {JobStatus::Failed, E_UNEXPECTED};
            _payload->display = PreparedCommittedDisplay::PrepareForWork(
                _ownerThread, *final, _displayAdmission, _cpuBytes, _payload->device.get(), _payload->immediate.get(),
                _payload->displayCompletion, context.cancellation);
            auto rows = LayerRowsWork::Prepare(*final, _payload->collapsed, _payload->hiddenPattern, _rowsAdmission,
                                               context.cancellation);
            _payload->rows = PreparedLayerRows::Create(std::move(rows), _rowBytes, _documentSerial, _firstItemId,
                                                       _payload->previousRows.get());
            return _payload->display && _payload->rows ? JobResult{} : FailureFor(context.cancellation);
        }
        case WorkerAction::RecordDisplay:
        {
            if (!_payload || !_payload->display || FAILED(_displayRecordingAdmission))
                return {JobStatus::Failed,
                        FAILED(_displayRecordingAdmission) ? _displayRecordingAdmission : E_UNEXPECTED};
            const auto* seed = _seedGraph; // Immutable while original parent retains its owner-only lease.
            return FailureResult(
                _payload->display->RecordForWork(_payload->displayCommands, context.cancellation, seed));
        }
        case WorkerAction::RetireDisplayCommands:
            return {};
        case WorkerAction::RefreshHistory:
            return Publication() && Publication()->RefreshHistoryResidency(context.cancellation)
                       ? JobResult{}
                       : FailureFor(context.cancellation);
        case WorkerAction::AwaitAck:
            return {}; // Callback/cancellation acknowledgement only; never reads a borrowed row or payload.
        }
        return {JobStatus::Failed, E_UNEXPECTED};
    }

    void RetireWorkerStage(JobContext&, JobResult result) noexcept override
    {
        if (_payload && _workerAction == WorkerAction::RecordCoverage)
            _payload->commands.FinishProducerForWork(result.status == JobStatus::Finished    ? S_OK
                                                     : result.status == JobStatus::Cancelled ? E_ABORT
                                                                                             : result.error);
        if (_payload && _workerAction == WorkerAction::RetireCommands)
        {
            if (!_payload->commands.RetireForWork())
                std::terminate();
            _payload->commandsRetired = true;
            _payload->commandRetirement = {}; // The actual original capture retires on the worker too.
        }
        if (_payload && _workerAction == WorkerAction::RecordDisplay)
            _payload->displayCommands.FinishProducerForWork(result.status == JobStatus::Finished    ? S_OK
                                                            : result.status == JobStatus::Cancelled ? E_ABORT
                                                                                                    : result.error);
        if (_payload && _workerAction == WorkerAction::RetireDisplayCommands)
        {
            if (!_payload->displayCommands.RetireForWork())
                std::terminate();
            _payload->displayCommandsRetired = true;
            _payload->displayCommandRetirement = {};
        }
        if (!_cleanupRequested)
            return;
        _payload.reset(); // CPU draft, retired roots, Clone lease, captured context and GPU directory die here.
        _retirementAcknowledged = true;
    }

    bool DeferRetirement(JobResult result, JobTicket ticket) noexcept override
    {
        if (ticket != _ticket)
            std::terminate();
        if (_phase == Phase::Quarantined && result.status == JobStatus::Cancelled)
        {
            _cancellationAcknowledged = true;
            if (_gate)
            {
                _gate->cancellationAcknowledgements.fetch_add(1);
                (void)SetEvent(_gate->cancelled.get());
            }
        }
        if (_workerAction == WorkerAction::RecordDisplay)
            _displayRecordingJoined = true;
        if (_workerAction == WorkerAction::RecordCoverage)
            _recordingJoined = true; // The runner has returned and RetireWorkerStage finished the producer.
        if (_loanReading || _terminal || result.status != JobStatus::Finished)
        {
            ReleaseLoanOwner(); // Exact reading/skipped producer has acknowledged before Unmap.
            _loanReading = false;
        }
        if (_terminal)
        {
            if (_retirementAcknowledged)
                return false;
            if (_queuePending)
                return true; // Queue callback owns readiness; no unchanged self-rearm.
            return BeginRetirementOwner();
        }
        if (result.status != JobStatus::Finished || !Current() || _owner._historyWork.CancelRequested(_stamp) ||
            _owner._canvasBrushCancelledOpening == _hostOpening)
        {
            _terminal = true;
            _terminalResult = result.status != JobStatus::Finished ? result : JobResult{JobStatus::Cancelled, E_ABORT};
            _stale = !Current();
            if (_queuePending)
                return true;
            return BeginRetirementOwner();
        }
        return false;
    }

    bool DeferCompletion(JobResult, JobTicket ticket) noexcept override
    {
        if (ticket != _ticket)
            std::terminate();
        if (_terminal || !Current())
            return false;
        switch (_phase)
        {
        case Phase::Allocate:
        {
            if (!_owner._committedDisplay.ClaimPublicationSlotOwner(_ticket, _publicationSlot))
                return FailOwner(E_OUTOFMEMORY);
            const auto* live = _owner._session.PendingBrush();
            const auto* expected = live ? live->ExpectedFinishCoverageEngine() : nullptr;
            if (!expected)
                return FailOwner(E_ABORT);
            _usesGpu = !expected->UsesCpuPermanent();
            // Capture the actual command stream and validate the empty epoch before detaching the live CPU draft.
            if (_usesGpu &&
                (expected != &_owner._brushEngine || !_owner._brushEngine.CanTakeStrokeEpoch(_payload->epoch) ||
                 FAILED(_owner._gpuWork.CaptureCompletionContext(_payload->completion))))
                return FailOwner(E_ABORT);
            BrushCoverageEngine* engine = nullptr;
            auto input = _owner._session.TakeStrokeFinishInput(_sessionOpening, engine);
            if (!input || !engine)
                return FailOwner(E_ABORT);
            _payload->input.emplace(std::move(input.value()));
            _transferred = true;
            if (engine != expected)
                std::terminate(); // Serialized owner preflight and exclusive transfer have no intervening callback.
            if (_usesGpu)
            {
                // Must preserve the exact engine which supplied the live stroke's density.
                if (!_owner._brushEngine.TakeStrokeEpoch(_payload->epoch))
                    std::terminate(); // The same owner just validated this exact empty epoch and original engine.
                _gpuAcknowledged = !_payload->epoch.HasResources();
            }
            else
                _gpuAcknowledged = true;
            _payload->device = _owner._renderer.Device(); // Strong exact-device capture, after admission.
            _payload->immediate = _owner._renderer.Context();
            const HRESULT captured = _owner._gpuWork.CaptureCompletionContext(_payload->displayCompletion);
            if (FAILED(captured))
                return FailOwner(captured);
            _payload->clone = std::move(_owner._canvasCloneSample);
            return ScheduleWorkerOwner(Phase::Prepare, WorkerAction::Prepare);
        }
        case Phase::Prepare:
            if (_payload->coverage->needsGpu)
            {
                _recordingAdmission =
                    _payload->commands.InitializeOwner(_owner._jobs, _ticket, _payload->completion.DeviceGeneration());
                if (FAILED(_recordingAdmission))
                    return FailOwner(_recordingAdmission);
                _payload->commandsInitialized = true;
                _recordingAdmission =
                    _owner._recordedCommands.AttachOwner(_payload->commands, _ticket, *this, _commandSource);
                // Even a refused attach reaches the original recorder stage. Its named retirement callback
                // publishes an unrecorded failed producer; the owner cannot pretend it already joined.
                _recordingJoined = false;
                _gpuAcknowledged = false;
                _phase = Phase::RecordCoverage;
                _workerAction = WorkerAction::RecordCoverage;
                if (!_owner._jobs.Resume(_ticket, JobExecutionLane::GpuRecording))
                    std::terminate();
                return true;
            }
            if (!_gpuAcknowledged)
            {
                _phase = Phase::AwaitRetirementSlot;
                _workerAction = WorkerAction::AwaitAck;
                _owner._jobs.NotifyOwnerWork();
                return true;
            }
            return ScheduleWorkerOwner(Phase::Compose, WorkerAction::Compose);
        case Phase::RecordCoverage:
            _phase = Phase::AwaitCommands;
            _workerAction = WorkerAction::AwaitAck;
            _owner._jobs.NotifyOwnerWork();
            return true;
        case Phase::CommandsRetired:
            _phase = Phase::AwaitCoverageSlot;
            _workerAction = WorkerAction::AwaitAck;
            _owner._jobs.NotifyOwnerWork();
            return true;
        case Phase::AfterCoverage:
            _loanReading = true;
            return ScheduleWorkerOwner(Phase::Install, WorkerAction::Install);
        case Phase::Install:
            if (!_installed)
                return FailOwner(E_UNEXPECTED);
            ++_nextCoverage;
            _installed = false;
            if (_nextCoverage < _payload->coverage->tiles.size())
            {
                _phase = Phase::AwaitCoverageSlot;
                _workerAction = WorkerAction::AwaitAck;
                _owner._jobs.NotifyOwnerWork();
                return true;
            }
            _phase = Phase::AwaitRetirementSlot;
            _workerAction = WorkerAction::AwaitAck;
            _owner._jobs.NotifyOwnerWork();
            return true; // Last real copy/map acknowledgment is followed by the original final-use barrier.
        case Phase::AfterBarrier:
            return ScheduleWorkerOwner(Phase::Compose, WorkerAction::Compose);
        case Phase::Compose:
        {
            if (_payload->result->status == StrokeFinishStatus::Unchanged)
            {
                (void)_owner._session.DiscardStrokeFinishIfCurrent(_sessionOpening);
                _terminal = true;
                return BeginRetirementOwner();
            }
            auto* publication = Publication();
            if (!publication || !_owner._session.IsPreparedStrokeFinishCurrent(*publication))
                return FailOwner(E_ABORT);
            return PrepareDisplayOwner(*publication);
        }
        case Phase::DisplayPrepared:
        {
            const auto* manifest = _payload->display ? _payload->display->ManifestAfterJoin() : nullptr;
            if (!manifest || !_payload->rows || !_owner._committedDisplay.PublicationSlotCurrentOwner(_publicationSlot))
                return FailOwner(E_ABORT);
            auto nextCpu = _cpuBytes;
            if (_owner._renderer.IsWarp() && !AddBytes(nextCpu, manifest->LogicalGpuBytes()))
                return FailOwner(E_OUTOFMEMORY);
            HRESULT status = _owner._jobs.IncreaseReservation(_ticket, nextCpu);
            if (FAILED(status))
                return FailOwner(status);
            _cpuBytes = nextCpu;
            status = _owner._committedDisplay.ReservePublicationGpuOwner(_publicationSlot, _payload->device.get(),
                                                                         _owner._renderer.IsWarp(),
                                                                         manifest->RequiredGpuBytes(), _displayGpu);
            if (FAILED(status))
                return FailOwner(status);
            CommittedDisplayDescriptor descriptor;
            if (!_owner.DescribeCommittedDisplayOwner(descriptor))
                return FailOwner(E_ABORT);
            (void)_owner._committedDisplay.AcquirePublishedSeedOwner(descriptor.key, _seed);
            _seedGraph = _seed ? _seed.GraphForWork() : nullptr;
            _displayRecordingAdmission = _payload->displayCommands.InitializeOwner(
                _owner._jobs, _ticket, _payload->displayCompletion.DeviceGeneration());
            if (FAILED(_displayRecordingAdmission))
                return FailOwner(_displayRecordingAdmission);
            _payload->displayCommandsInitialized = true;
            _displayRecordingAdmission =
                _owner._recordedCommands.AttachOwner(_payload->displayCommands, _ticket, *this, _displaySource);
            _displayRecordingJoined = false;
            _phase = Phase::RecordDisplay;
            _workerAction = WorkerAction::RecordDisplay;
            if (!_owner._jobs.Resume(_ticket, JobExecutionLane::GpuRecording))
                std::terminate();
            return true;
        }
        case Phase::RecordDisplay:
            _phase = Phase::AwaitDisplayCommands;
            _workerAction = WorkerAction::AwaitAck;
            _owner._jobs.NotifyOwnerWork();
            return true;
        case Phase::DisplayCommandsRetired:
        case Phase::RefreshHistory:
        {
            auto* publication = Publication();
            if (!publication || !_owner._session.IsPreparedStrokeFinishCurrent(*publication) ||
                _collapseRevision != _owner._session.CollapseRevision())
                return FailOwner(E_ABORT);
            const auto decision = publication->TryAcquireHistoryResidency(_historyPermit);
            if (decision == DocumentHistory::ResidencyValidation::Refresh)
                return ScheduleWorkerOwner(Phase::RefreshHistory, WorkerAction::RefreshHistory);
            if (decision != DocumentHistory::ResidencyValidation::Ready)
                return FailOwner(E_ABORT);
            // From binding onward only allocation-free original cleanup follows; every useful user joined.
            HRESULT status = _payload->memory->BindForPublication(_owner._jobs, _ticket);
            if (FAILED(status))
                return FailOwner(status);
            if (!_payload->display->BindMemoryOwner(_payload->memory) ||
                FAILED(_payload->rows->BindPublicationMemory(_payload->memory)) ||
                !_payload->rows->SealForPublication())
                std::terminate(); // Exclusive validated nonempty worker owners under complete admitted bound.
            status = _payload->display->PublishOwner();
            if (FAILED(status))
                return FailOwner(status);
            const auto* final = publication->FinalDocument();
            if (!final || !_owner._committedDisplay.CanAdoptPublicationOwner(_publicationSlot, *_payload->display,
                                                                             _displayGpu, *final))
                return FailOwner(E_ABORT);
            // No nested callback/notification occurs between these validated pointer publications.
            if (!_owner._session.ApplyPreparedStrokeFinishIfCurrent(*publication))
                std::terminate();
            _applied = true;
            _payload->previousRows = std::move(_owner._preparedLayerRows);
            _owner._preparedLayerRows = std::move(_payload->rows);
            _owner._layerRowsPublishedKey = _owner.CurrentLayerRowsKey();
            _owner._layerRowsRequestedKey = _owner._layerRowsPublishedKey;
            _owner._layerRowsRetryAvailable = false;
            _owner._layerRowsCacheTicket = _ticket;
            if (!_owner._committedDisplay.AdoptPublicationOwner(_publicationSlot, *_payload->display, _displayGpu))
                std::terminate();
            _payload->displayConsumed = true;
            _terminal = true;
            _terminalResult = {};
            return BeginRetirementOwner();
        }
        default:
            return false;
        }
    }

    bool HasReadyOwner() const noexcept
    {
        if (_owner._shutdownStarted || _queuePending)
            return false;
        if (_phase == Phase::Quarantined)
            return _cancellation.IsCancelled() && !_cancellationAckRequested && !_cancellationAcknowledged;
        // Test owner-only phase first: Allocate/Prepare/Install/Compose may still have a producer writing payload.
        if (_phase == Phase::AwaitDisplayCommands)
            return _displayRecordingJoined && _payload && _payload->displayCommands.CanRetireOwner();
        if (_phase == Phase::AwaitCommands)
            return _recordingJoined && _payload && _payload->commands.CanRetireOwner();
        if (_phase == Phase::AwaitCoverageSlot)
            return _owner._gpuWork.HasReadbackSlot();
        if (_phase == Phase::AwaitRetirementSlot)
            return _payload && _owner._gpuWork.HasRetirementCompletionSlot(_payload->completion);
        return false; // AwaitDisplay/failed recording require their own real event, never busy polling.
    }

    void DeviceChangedOwner() noexcept
    {
        if (_owner._shutdownStarted || _phase != Phase::Quarantined)
            return;
        if (_owner._gpuWork.DeviceGeneration() == _quarantineGeneration &&
            _owner._gpuWork.CompletionRoute() == _quarantineRoute)
            return; // Repeated missing-device/binding notifications cannot turn failure into busy retries.
        // This real binding transition grants one new offer on the held ORIGINAL context. A failed
        // offer returns to dormant quarantine; replacement/cancellation never establishes GPU completion.
        _phase = Phase::AwaitRetirementSlot;
        _owner._jobs.NotifyOwnerWork();
    }

    void PumpOwner() noexcept
    {
        if (!HasReadyOwner())
            return;
        if (_phase == Phase::Quarantined)
        {
            // Cancel marks the suspended record but cannot itself acknowledge a producer. Resume exactly
            // one skipped AwaitAck stage; keep this original phase/charge until real device recovery.
            _cancellationAckRequested = true;
            (void)ResumeOwner();
            return;
        }
        if (_phase == Phase::AwaitDisplayCommands)
        {
            if (_displaySource.slot < GpuCommandPump::kCapacity)
            {
                if (!_owner._recordedCommands.DetachOwner(_displaySource, _payload->displayCommandRetirement))
                    std::terminate();
                _displaySource = {};
            }
            if (!_payload->displayCommands.AuthorizeRetirementOwner())
                std::terminate();
            _payload->displayCommandsAuthorized = true;
            (void)ScheduleWorkerOwner(Phase::DisplayCommandsRetired, WorkerAction::RetireDisplayCommands);
            return;
        }
        if (_phase == Phase::AwaitCommands)
        {
            if (_commandSource.slot < GpuCommandPump::kCapacity)
            {
                if (!_owner._recordedCommands.DetachOwner(_commandSource, _payload->commandRetirement))
                    std::terminate();
                _commandSource = {};
            }
            if (!_payload->commands.AuthorizeRetirementOwner())
                std::terminate();
            _payload->commandsAuthorized = true;
            (void)ScheduleWorkerOwner(Phase::CommandsRetired, WorkerAction::RetireCommands);
            return;
        }
        if (_phase == Phase::AwaitRetirementSlot)
        {
            const HRESULT result = _owner._gpuWork.EnqueueRetirementCompletion(_payload->completion, _ticket, *this);
            if (FAILED(result))
            {
                const auto removed = _payload->completion.DeviceStatusOwner();
                if (FAILED(removed) && removed != HRESULT_FROM_WIN32(ERROR_INVALID_STATE))
                {
                    // Independently confirmed ORIGINAL-device removal. This acknowledges no successful image work.
                    _gpuAcknowledged = true;
                    PublishAsyncFailureOwner(removed);
                    return;
                }
                // Healthy device plus failed registration/signal is never a retirement acknowledgement.
                _phase = Phase::Quarantined;
                _quarantineGeneration = _owner._gpuWork.DeviceGeneration();
                _quarantineRoute = _owner._gpuWork.CompletionRoute();
                if (!_terminal)
                {
                    _terminal = true;
                    _terminalResult = {JobStatus::Failed, result};
                }
                if (_gate)
                {
                    _gate->lastRetirementRefusal.store(result);
                    _gate->quarantines.fetch_add(1);
                    (void)SetEvent(_gate->quarantined.get());
                }
                return;
            }
            _queuePending = true;
            _phase = Phase::AwaitRetirement;
            return;
        }
        const auto& final = _payload->coverage.value();
        const auto& tile = final.tiles[_nextCoverage];
        // Recorder callback and ALL original command batches already acknowledged. Borrow the exact buffer;
        // no shader dispatch, resource creation, byte conversion or immediate READ Map occurs in this pump.
        _gpuAcknowledged = false;
        auto* buffer = GpuBrushCoverage::RecordedEpochBufferOwner(_payload->epoch, _payload->device.get(), tile.key,
                                                                  tile.width, tile.height);
        const HRESULT result =
            buffer ? _owner._gpuWork.EnqueueBufferReadback(buffer, static_cast<unsigned int>(tile.width),
                                                           static_cast<unsigned int>(tile.height), _ticket, *this)
                   : E_UNEXPECTED;
        if (FAILED(result))
        {
            PublishAsyncFailureOwner(result);
            return;
        }
        _queuePending = true;
        _phase = Phase::AwaitCoverage;
    }

    void CommandAcknowledgedOwner(JobTicket ticket, GpuCommandLease command, HRESULT result) noexcept override
    {
        if (ticket != _ticket || command.source != _ticket)
            std::terminate();
        if (FAILED(result) && !_terminal)
        {
            _terminal = true;
            _terminalResult = {JobStatus::Failed, result};
        }
        _owner._jobs.NotifyOwnerWork(); // Never advance/detach a still-running recorder from its callback.
    }
    void CommandSourceFailedOwner(JobTicket ticket, HRESULT result) noexcept override
    {
        if (ticket != _ticket || SUCCEEDED(result))
            std::terminate();
        if (!_terminal)
        {
            _terminal = true;
            _terminalResult = {JobStatus::Failed, result};
        }
        _owner._jobs.NotifyOwnerWork(); // The command pump owns the actual quarantine/recovery acknowledgement.
    }

    void BufferReadbackReady(JobTicket ticket, GpuBufferReadbackValues rows, HRESULT result) noexcept override
    {
        if (ticket != _ticket || !_queuePending || _phase != Phase::AwaitCoverage)
            std::terminate();
        _queuePending = false;
        if (FAILED(result))
        {
            // Pending-readback failure is not proof that this original context's compute has completed.
            PublishAsyncFailureOwner(result);
            return;
        }
        _gpuAcknowledged = true; // Successful mapped read waited for the real preceding copy/event.
        _rows = rows;
        _phase = Phase::AfterCoverage;
        (void)ResumeOwner();
    }

    void GpuCompletionReady(JobTicket ticket, std::uint64_t generation, HRESULT result) noexcept override
    {
        if (ticket != _ticket || !_queuePending || _phase != Phase::AwaitRetirement || !_payload ||
            generation != _payload->completion.DeviceGeneration())
            std::terminate();
        _queuePending = false;
        _gpuAcknowledged = true; // Every admitted completion callback follows the actual original event.
        if (!_terminal && FAILED(result))
        {
            _terminal = true;
            _terminalResult = {JobStatus::Failed, result};
        }
        _phase = Phase::AfterBarrier;
        (void)ResumeOwner();
    }

    void Completed(JobResult result) noexcept override
    {
        if (!_terminal || !_retirementAcknowledged)
            std::terminate();
        const bool cancelled =
            !_applied && (_cancellation.IsCancelled() || _owner._historyWork.CancelRequested(_stamp) ||
                          _owner._canvasBrushCancelledOpening == _hostOpening);
        _historyPermit.Reset();
        _displayGpu.ResetOwner();
        if (_publicationSlot && !_owner._committedDisplay.ReleasePublicationSlotOwner(_publicationSlot))
            std::terminate();
        if (_seed && !_owner._committedDisplay.ReleasePublishedSeedOwner(_seed))
            std::terminate();
        _gpuReservation.ResetOwner(); // No captured epoch/device user or CPU owner remains.
        if (_owner._strokeFinishCurrent == this)
            _owner._strokeFinishCurrent = nullptr;
        if (_transferred && !_applied)
            (void)_owner._session.DiscardStrokeFinishIfCurrent(_sessionOpening);
        _owner._utilityTicket.reset();
        _owner._utilityStamp.reset();
        (void)_owner._historyWork.Finish(_stamp);
        (void)result; // Cleanup cancellation cannot change an already published terminal result.
        auto terminal = cancelled ? JobResult{JobStatus::Cancelled, E_ABORT} : _terminalResult;
        if (terminal.status == JobStatus::Finished && _owner._canvasBrushInput.Overflowed())
        {
            // Capacity loss seals and commits only the accepted prefix, but its original receipt reports the
            // refused sample. Preserve a prior failure/cancellation and the current-request staleness check.
            terminal = {JobStatus::Failed, static_cast<std::int32_t>(HRESULT_FROM_WIN32(ERROR_NOT_ENOUGH_QUOTA))};
        }
        const auto outcome = _stale || !CurrentRequest()               ? HostOutcome::Stale
                             : terminal.status == JobStatus::Cancelled ? HostOutcome::Cancelled
                             : terminal.status == JobStatus::Failed    ? HostOutcome::Failed
                                                                       : HostOutcome::Succeeded;
        if (_applied)
            _owner.DocumentChanged(); // Notifications/chrome run after every root has been installed together.
        _owner.ResumeHostHistory(_stamp, outcome, outcome == HostOutcome::Stale ? E_ABORT : terminal.error);
    }

  private:
    enum class Phase : std::uint8_t
    {
        Allocate,
        Prepare,
        RecordCoverage,
        AwaitCommands,
        CommandsRetired,
        AwaitCoverageSlot,
        AwaitCoverage,
        AfterCoverage,
        Install,
        Compose,
        DisplayPrepared,
        RecordDisplay,
        AwaitDisplayCommands,
        DisplayCommandsRetired,
        RefreshHistory,
        AwaitRetirementSlot,
        AwaitRetirement,
        AfterBarrier,
        Quarantined,
        Retire
    };
    enum class WorkerAction : std::uint8_t
    {
        Allocate,
        Prepare,
        RecordCoverage,
        RetireCommands,
        Install,
        Compose,
        PrepareDisplay,
        RecordDisplay,
        RetireDisplayCommands,
        RefreshHistory,
        AwaitAck
    };
    static bool AddBytes(std::uint64_t& bytes, std::uint64_t extra) noexcept
    {
        if (extra > (std::numeric_limits<std::uint64_t>::max)() - bytes)
            return false;
        bytes += extra;
        return true;
    }
    static JobResult FailureResult(HRESULT status) noexcept
    {
        return status == E_ABORT ? JobResult{JobStatus::Cancelled, status}
               : FAILED(status)  ? JobResult{JobStatus::Failed, status}
                                 : JobResult{};
    }
    bool PrepareDisplayOwner(PreparedStrokeFinish& publication) noexcept
    {
        const auto* final = publication.FinalDocument();
        if (!final || !_owner._committedDisplay.PublicationSlotCurrentOwner(_publicationSlot))
            return FailOwner(E_ABORT);
        RasterResidencyPlanOptions options;
        options.textureLimit = 16384u;
        options.deviceGeneration = _payload->displayCompletion.DeviceGeneration();
        _displayAdmission = RasterResidencyCpuAdmission::Estimate(*final, options);
        const auto& collapsed = std::as_const(_owner._session).CollapsedGroupIDs();
        _rowsAdmission = LayerRowsAdmission::Estimate(*final, collapsed.size(), _owner._layerRowsHiddenPattern);
        const auto allowance = PreparedLayerRows::OwnershipAllowance(final->layers.size());
        _rowBytes = _rowsAdmission.Bytes();
        if (!_displayAdmission.IsValid() || !_rowsAdmission.IsValid() || !allowance || !AddBytes(_rowBytes, allowance))
            return FailOwner(E_OUTOFMEMORY);
        auto total = _cpuBytes;
        if (!AddBytes(total, _displayAdmission.CpuBytes()) || !AddBytes(total, _rowBytes) ||
            !AddBytes(total, 128ull * 1024 * 1024) ||
            final->layers.size() > (std::numeric_limits<std::uint64_t>::max)() - _owner._nextLayerRowItemId)
            return FailOwner(E_OUTOFMEMORY);
        const HRESULT admitted = _owner._jobs.IncreaseReservation(_ticket, total);
        if (FAILED(admitted))
            return FailOwner(admitted);
        _cpuBytes = total;
        _firstItemId = _owner._nextLayerRowItemId;
        _owner._nextLayerRowItemId += final->layers.size();
        _collapseRevision = _owner._session.CollapseRevision();
        try
        {
            _payload->collapsed.assign(collapsed.begin(), collapsed.end());
            _payload->hiddenPattern = _owner._layerRowsHiddenPattern;
            _payload->previousRows = _owner._preparedLayerRows;
        }
        catch (const std::bad_alloc&)
        {
            return FailOwner(E_OUTOFMEMORY); // Explicit admitted input capture; operation fails.
        }
        return ScheduleWorkerOwner(Phase::DisplayPrepared, WorkerAction::PrepareDisplay);
    }
    static JobResult FailureFor(const CancelToken& cancellation) noexcept
    {
        return cancellation.IsCancelled() ? JobResult{JobStatus::Cancelled}
                                          : JobResult{JobStatus::Failed, E_INVALIDARG};
    }
    PreparedStrokeFinish* Publication() noexcept
    {
        return _payload && _payload->result && _payload->result->publication ? &_payload->result->publication.value()
                                                                             : nullptr;
    }
    bool CurrentRequest() const noexcept
    {
        const auto* captured = _owner._canvasBrushInput.CapturedContext(_hostOpening);
        return !_owner._shutdownStarted && captured && captured->documentSerial == _owner._session.DocumentSerial() &&
               _owner._canvasBrushSessionOpening == _sessionOpening;
    }
    bool Current() const noexcept
    {
        return CurrentRequest() && _owner._utilityStamp == _stamp && _owner._historyWork.IsCurrent(_stamp) &&
               (!_transferred || _applied || _owner._session.PendingBrushOpening() == _sessionOpening);
    }
    bool ScheduleWorkerOwner(Phase phase, WorkerAction action) noexcept
    {
        _phase = phase;
        _workerAction = action;
        return ResumeOwner();
    }
    bool ResumeOwner() noexcept
    {
        const bool resumed = _owner._jobs.Resume(_ticket);
        if (!resumed && !_cancellation.IsCancelled())
            std::terminate();
        return true; // Original skipped cancellation stage owns the same external acknowledgement.
    }
    void ReleaseLoanOwner() noexcept
    {
        if (!_rows.data)
            return;
        if (!_owner._gpuWork.ReleaseMapped(_rows.lease))
            std::terminate();
        _rows = {};
    }
    bool FailOwner(HRESULT error) noexcept
    {
        _terminal = true;
        _stale = error == E_ABORT && !Current();
        _terminalResult = {error == E_ABORT ? JobStatus::Cancelled : JobStatus::Failed, error};
        return BeginRetirementOwner();
    }
    void PublishAsyncFailureOwner(HRESULT error) noexcept
    {
        // No worker acknowledgement is implied by a GPU callback or owner-source pump. A cancellation-skipped
        // stage may already be running. Only publish owner terminal facts and resume the unchanged AwaitAck.
        if (!_terminal)
        {
            _terminal = true;
            _terminalResult = {JobStatus::Failed, error};
        }
        (void)ResumeOwner();
    }
    bool BeginRetirementOwner() noexcept
    {
        if (_queuePending || _phase == Phase::Quarantined)
            return true;
        if (_payload && _payload->commandsInitialized && !_payload->commandsRetired)
        {
            _payload->commands.RequestStopOwner();
            if (_commandSource.slot < GpuCommandPump::kCapacity)
                (void)_owner._recordedCommands.StopSourceOwner(_commandSource);
            _phase = Phase::AwaitCommands;
            _workerAction = WorkerAction::AwaitAck;
            _owner._jobs.NotifyOwnerWork();
            return true; // Real producer join and original submitted uses precede any command/epoch retirement.
        }
        if (_payload && !_gpuAcknowledged)
        {
            _phase = Phase::AwaitRetirementSlot;
            _workerAction = WorkerAction::AwaitAck;
            _owner._jobs.NotifyOwnerWork();
            return true;
        }
        if (_payload && _payload->displayCommandsInitialized && !_payload->displayCommandsRetired)
        {
            _payload->displayCommands.RequestStopOwner();
            if (_displaySource.slot < GpuCommandPump::kCapacity)
                (void)_owner._recordedCommands.StopSourceOwner(_displaySource);
            _phase = Phase::AwaitDisplayCommands;
            _workerAction = WorkerAction::AwaitAck;
            _owner._jobs.NotifyOwnerWork();
            return true;
        }
        if (_payload && _payload->display && !_displayDetached && !_applied)
        {
            if (!_payload->display->DetachOwner())
                std::terminate();
            _displayDetached = true;
        }
        _phase = Phase::Retire;
        _cleanupRequested = true; // Owner after producer and external acknowledgement only; callbacks never set it.
        _workerAction = WorkerAction::AwaitAck;
        return ResumeOwner();
    }

    Application& _owner;
    const HistoryWorkStamp _stamp;
    const std::uint64_t _hostOpening;
    const std::uint64_t _sessionOpening;
    const std::uint64_t _retirement;
    const std::shared_ptr<StrokeFinishTestGate> _gate = _owner._strokeFinishTestGate;
    const DWORD _ownerThread = GetCurrentThreadId();
    const std::uint64_t _documentSerial = _owner._session.DocumentSerial();
    std::uint64_t _cpuBytes;
    std::uint64_t _quarantineGeneration = 0;
    GpuCapabilities::CompletionRoute _quarantineRoute = GpuCapabilities::CompletionRoute::None;
    RasterResidencyCpuAdmission _displayAdmission;
    LayerRowsAdmission _rowsAdmission;
    std::uint64_t _rowBytes = 0;
    std::uint64_t _firstItemId = 0;
    std::uint64_t _collapseRevision = 0;
    CommittedDisplayPublicationSlot _publicationSlot;
    CommittedDisplaySeedLease _seed;
    const RecordedDisplayGraph* _seedGraph = nullptr;
    GpuResidencyReservation _displayGpu;
    GpuCommandSourceLease _displaySource;
    HRESULT _displayRecordingAdmission = E_PENDING;
    bool _displayRecordingJoined = false;
    bool _displayDetached = false;
    bool _shutdownJoined = false;
    GpuResidencyReservation _gpuReservation;
    std::unique_ptr<FinishPayload, RetireFinishPayload> _payload;
    DocumentHistory::ResidencyPermit _historyPermit;
    GpuBufferReadbackValues _rows;
    GpuCommandSourceLease _commandSource;
    HRESULT _recordingAdmission = E_PENDING;
    bool _recordingJoined = false;
    CancelToken _cancellation;
    JobTicket _ticket;
    JobResult _terminalResult;
    std::size_t _nextCoverage = 0;
    Phase _phase = Phase::Allocate;
    WorkerAction _workerAction =
        WorkerAction::Allocate; // Written only after producer acknowledgement; never callbacks.
    bool _cleanupRequested = false;
    bool _cancellationAckRequested = false;
    bool _cancellationAcknowledged = false;
    bool _loanReading = false;
    bool _stale = false;
    bool _usesGpu = false;
    bool _transferred = false;
    bool _queuePending = false;
    bool _gpuAcknowledged = true;
    bool _installed = false;
    bool _terminal = false;
    bool _retirementAcknowledged = false;
    bool _applied = false;
};

bool Application::StrokeFinishSource::Start() noexcept
{
    if (_attached || !_owner._ownerWork.Attach(*this))
        return false;
    _attached = true;
    return true;
}
bool Application::StrokeFinishSource::HasPending() const noexcept
{
    return _attached && _owner._strokeFinishCurrent != nullptr;
}
bool Application::StrokeFinishSource::HasReady() const noexcept
{
    return _attached && _owner._strokeFinishCurrent && _owner._strokeFinishCurrent->HasReadyOwner();
}
void Application::StrokeFinishSource::Pump() noexcept
{
    if (HasReady())
        _owner._strokeFinishCurrent->PumpOwner();
}
void Application::StrokeFinishSource::DeviceChangedOwner() noexcept
{
    if (_attached && _owner._strokeFinishCurrent)
        _owner._strokeFinishCurrent->DeviceChangedOwner();
}
void Application::StrokeFinishSource::CloseAfterWorkers() noexcept
{
    if (_attached)
    {
        if (!_owner._ownerWork.Detach(*this))
            std::terminate();
        _attached = false;
    }
}
void Application::PrepareStrokeFinishJoinedShutdownOwner() noexcept
{
    if (_strokeFinishCurrent)
        _strokeFinishCurrent->JoinedShutdownOwner();
    _strokeFinishSource.CloseAfterWorkers();
}

HRESULT Application::StartStrokeFinish(std::uint64_t opening) noexcept
{
    const auto* captured = _canvasBrushInput.CapturedContext(opening);
    const auto* stroke = _session.PendingBrush();
    if (!_committedDisplayInitialized || !_preparedLayerRowsEnabled || !captured || !stroke ||
        !_canvasBrushSessionOpening || _session.PendingBrushOpening() != _canvasBrushSessionOpening ||
        !_activeHostRequest || _utilityTicket || _strokeFinishCurrent ||
        captured->documentSerial != _session.DocumentSerial())
        return E_UNEXPECTED;
    const auto plan = stroke->PlanFinishMemory();
    const auto retirement = _session.RasterEditRetirementMemoryBytes();
    if (!plan || !retirement)
        return E_OUTOFMEMORY;
    auto cpu = retirement;
    const auto add = [&](std::uint64_t value) noexcept
    {
        if (value > (std::numeric_limits<std::uint64_t>::max)() - cpu)
            return false;
        cpu += value;
        return true;
    };
    // Private brush pipeline, structured readback, metadata and all three bounded command shadows.
    if (!add(plan->retirementCpuBytes) || !add(plan->additionalCpuBytes) || !add(128ull * 1024 * 1024) ||
        !add(sizeof(StrokeFinishJob) + sizeof(FinishPayload)) || !add(SharedPublicationMemory::OwnershipAllowance()) ||
        (_renderer.IsWarp() && !add(plan->additionalGpuBytes)))
        return E_OUTOFMEMORY;
    GpuResidencyReservation reservation;
    if (plan->additionalGpuBytes)
    {
        GpuCapabilities::VideoMemorySnapshot snapshot;
        const HRESULT query = GpuCapabilities::QueryVideoMemorySnapshot(_renderer.Device(), snapshot);
        if (FAILED(query))
            return query;
        const auto& segment = _renderer.IsWarp() && snapshot.nonLocal.available && snapshot.nonLocal.budget
                                  ? snapshot.nonLocal
                                  : snapshot.local;
        if (!segment.available || !segment.budget)
            return E_OUTOFMEMORY;
        GpuResidencyAdmission decision;
        reservation = _graphGpuBudget.ReserveOwner(
            plan->additionalGpuBytes, {_gpuWork.DeviceGeneration(), segment.budget, segment.currentUsage}, decision);
        if (!reservation)
            return E_OUTOFMEMORY;
    }
    const auto ordinal = _activeHostRequest->ordinal;
    auto stamp = _historyWork.Begin(_session.DocumentSerial(), ordinal, HistoryWorkKind::Selection);
    if (!stamp && _hostFinishedStamp && _hostFinishedStamp->ordinal == ordinal)
        stamp = _historyWork.Continue(*_hostFinishedStamp, _session.DocumentSerial(), HistoryWorkKind::Selection);
    if (!stamp)
        return E_UNEXPECTED;
    HRESULT status = E_OUTOFMEMORY;
    try
    {
        auto constructed = std::make_unique<StrokeFinishJob>(*this, *stamp, opening, *_canvasBrushSessionOpening,
                                                             retirement, cpu, std::move(reservation));
        auto* original = constructed.get();
        std::unique_ptr<Job> job = std::move(constructed);
        JobTicket ticket;
        status = _jobs.Submit(job, {JobClass::Stroke, _session.DocumentSerial(), cpu, true, false, true}, ticket);
        if (SUCCEEDED(status))
        {
            original->BindOwnerTicket(ticket);
            _strokeFinishCurrent = original;
            _utilityStamp = stamp;
            _utilityTicket = ticket;
            _hostCommitStamp = stamp;
            _hostAfterPrerequisite = HostStage::Complete;
            return S_OK;
        }
    }
    catch (const std::bad_alloc&)
    {
        // Descriptor admission failure: no live draft, source, pixels or history has been transferred.
    }
    (void)_historyWork.Finish(*stamp);
    _hostFinishedStamp = stamp;
    return status;
}
