// Original admitted final stroke: coverage, pixels/history, rows and display share one ticket.
// Normal input activation is separately qualified with the sample pipeline.
#include "Application.h"

#include "GpuCapabilities.h"
#include "GpuRecordingCommands.h"
#include "PreparedLayerRows.h"
#include "StrokeFinishTestGate.h"
#include "StrokeFinishWork.h"
#include "StrokeSampleWork.h"
#include "WarpFinishWork.h"
#include "WarpOpeningWork.h"
#include "WarpPreviewWork.h"
#include "WarpRetirementWork.h"
#include "WarpSampleWork.h"
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
struct LiveRetirementCapture final
{
    std::optional<StrokeFinishInput> input;
    GpuBrushCoverage::StrokeEpoch epoch;
};
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
    std::optional<WarpOpeningInput> warpOpeningInput;
    std::optional<WarpOpeningResult> warpOpeningResult;
    std::shared_ptr<WarpStroke> appliedWarpOpening; // worker releases before exclusive first sample
    std::optional<WarpSampleInput> warpSampleInput;
    std::optional<WarpRetirementInput> warpRetirement;
    std::optional<WarpFinishInput> warpInput;
    std::size_t warpPointCount = 0;
    std::optional<StrokeSampleInput> sampleInput;
    std::optional<BrushStroke::SampleCoverage> sampleCoverage;
    std::vector<RasterTile> changedTiles;
    CanvasRenderer::StrokePreview samplePreview;
    std::unique_ptr<RecordedDisplayGraph> previewCurrent;
    std::unique_ptr<RecordedDisplayGraph> previewCandidate;
    std::unique_ptr<RecordedDisplayGraph> previewPrevious;
    bool previewsDetached = false;
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
    // First worker allocation; exclusively owner-owned after its acknowledgement until terminal retirement.
    // Separate from sample input/coverage/preview fields a producer may be using.
    std::unique_ptr<LiveRetirementCapture> emergency;
    wil::com_ptr_nothrow<ID3D11Device> device;
    GpuCompletionContext commandRetirement;
    GpuCommandStream commands;
    bool commandsInitialized = false;
    bool commandsAuthorized = false;
    bool commandsRetired = false;
    ~FinishPayload()
    {
        if (previewPrevious && !previewPrevious->RetireForWork())
            std::terminate();
        for (auto* graph : {previewCurrent.get(), previewCandidate.get()})
            if (graph && (!previewsDetached || !graph->RetireForWork()))
                std::terminate();
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
                    std::uint64_t retirement, std::uint64_t cpuBytes, GpuResidencyReservation reservation,
                    bool processingSamples = false, bool warpReplay = false, bool warpLive = false) noexcept
        : _owner(owner), _stamp(stamp), _hostOpening(hostOpening), _sessionOpening(sessionOpening),
          _retirement(retirement), _cpuBytes(cpuBytes), _gpuReservation(std::move(reservation)),
          _processingSamples(processingSamples), _warpReplay(warpReplay), _warpLive(warpLive)
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

    void RetainJoinedRetirement(JobScheduler& scheduler, JobTicket ticket) noexcept override
    {
        if (&scheduler != &_owner._jobs || ticket != _ticket)
            std::terminate();
        // Every producer has actually joined. Close may have arrived during first allocation, before the
        // successful Allocate callback retained the accounting alias. Both branches preserve weak owners
        // through the ensuing whole-payload worker retirement without granting useful/publication authority.
        const auto memory = _retirementMemory ? _retirementMemory : _payload ? _payload->memory : nullptr;
        if (memory && !memory->IsBound() && FAILED(memory->BindForRetirement(scheduler, ticket)))
            std::terminate();
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
            DetachPreviewsOwner();
            CaptureLiveRetirementOwner();
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
            if (_processingSamples)
                _payload->emergency = std::make_unique<LiveRetirementCapture>();
            return {};
        case WorkerAction::WarpOpening:
            if (!_payload || !_payload->warpOpeningInput)
                return {JobStatus::Failed, E_UNEXPECTED};
            _payload->warpOpeningResult.emplace(DocumentSession::PrepareWarpOpeningPublication(
                std::move(*_payload->warpOpeningInput), context.cancellation, {}, _payload->memory));
            return _payload->warpOpeningResult->prepared
                       ? HoldSampleBoundary(context, StrokeFinishTestGate::SampleBoundary::OpeningPrepared)
                       : FailureFor(context.cancellation);
        case WorkerAction::WarpOpeningRetire:
            return {}; // RetireWorkerStage drops all consumed opening aliases before next exclusive Take.
        case WorkerAction::WarpLiveSample:
        {
            if (!_payload || !_payload->warpSampleInput ||
                !_payload->warpSampleInput->AppendForWork(context.cancellation))
                return FailureFor(context.cancellation);
            auto* draft = _payload->warpSampleInput->DraftForWork();
            if (!draft)
                return {JobStatus::Failed, E_UNEXPECTED};
            // Changed capture must precede the complete immutable factory consuming dirty flags.
            _payload->changedTiles = draft->TakeChangedTilesForWork(context.cancellation);
            if (!draft->IsValid() || context.cancellation.IsCancelled())
                return FailureFor(context.cancellation);
            _sampleNoPixels = _payload->changedTiles.empty();
            if (!_sampleNoPixels)
            {
                auto& preview = _payload->samplePreview;
                preview.warpSource =
                    PrepareWarpPreviewForWork(*draft, _ownerId, context.cancellation, _payload->memory);
                if (!preview.warpSource)
                    return FailureFor(context.cancellation);
                preview.layerID = _warpTarget;
                preview.width = draft->Width();
                preview.height = draft->Height();
                preview.source = preview.warpSource->source.image;
                preview.transform =
                    LayerTransform{{0, 0}, {static_cast<double>(draft->Width()), static_cast<double>(draft->Height())}};
                _payload->previewCandidate = std::make_unique<RecordedDisplayGraph>(_ownerThread);
            }
            if (_gate)
            {
                _gate->samplesPrepared.fetch_add(1);
                _gate->samplesOffOwner.fetch_add(GetCurrentThreadId() != _ownerThread ? 1u : 0u);
            }
            return HoldSampleBoundary(context, StrokeFinishTestGate::SampleBoundary::Composed);
        }
        case WorkerAction::SamplePrepare:
            if (!_payload || !_payload->sampleInput)
                return {JobStatus::Failed, E_UNEXPECTED};
            _payload->sampleCoverage =
                _payload->sampleInput->DraftForWork().PrepareSampleCoverage(context.cancellation);
            if (_payload->sampleCoverage && _gate)
            {
                _gate->samplesPrepared.fetch_add(1);
                _gate->samplesOffOwner.fetch_add(GetCurrentThreadId() != _ownerThread ? 1u : 0u);
            }
            return _payload->sampleCoverage
                       ? HoldSampleBoundary(context, StrokeFinishTestGate::SampleBoundary::Prepared)
                       : FailureFor(context.cancellation);
        case WorkerAction::SampleCompose:
        {
            if (!_payload || !_payload->sampleInput || !_payload->sampleCoverage ||
                !_payload->sampleInput->DraftForWork().ComposeSampleCoverage(_payload->sampleCoverage.value()))
                return FailureFor(context.cancellation);
            _sampleNoPixels = _payload->sampleCoverage->tiles.empty();
            if (_sampleNoPixels)
                return {}; // Duplicate/outside points advance their sequence without a new preview graph.
            auto& stroke = _payload->sampleInput->DraftForWork();
            auto& preview = _payload->samplePreview;
            preview.layerID = stroke.LayerID();
            preview.mask = stroke.IsMask();
            preview.width = stroke.Width();
            preview.height = stroke.Height();
            preview.transform = stroke.PaintTransform();
            preview.fill = stroke.FillValue();
            const auto& source = stroke.SourceImage();
            if (source.HasStorage())
            {
                preview.source = source;
                preview.sourceX = stroke.SourceRect().x;
                preview.sourceY = stroke.SourceRect().y;
                preview.sourceWidth = stroke.SourceRect().width;
                preview.sourceHeight = stroke.SourceRect().height;
            }
            _payload->changedTiles.clear();
            for (const auto index : stroke.TakeChanged())
            {
                const auto& tile = stroke.Tiles()[index];
                _payload->changedTiles.push_back({tile.rect.x, tile.rect.y, tile.image});
            }
            _payload->previewCandidate = std::make_unique<RecordedDisplayGraph>(_ownerThread);
            return HoldSampleBoundary(context, StrokeFinishTestGate::SampleBoundary::Composed);
        }
        case WorkerAction::SampleRecordPreview:
        {
            if (!_payload || (_warpLive ? !_payload->warpSampleInput : !_payload->sampleInput) ||
                !_payload->previewCandidate || FAILED(_displayRecordingAdmission))
                return {JobStatus::Failed,
                        FAILED(_displayRecordingAdmission) ? _displayRecordingAdmission : E_UNEXPECTED};
            const auto* seed = _payload->previewCurrent ? _payload->previewCurrent.get() : _seedGraph;
            const auto status = _payload->previewCandidate->PrepareStrokeForWork(
                _payload->device.get(),
                _warpLive ? _payload->warpSampleInput->CapturedDocumentForWork()
                          : _payload->sampleInput->CapturedDocumentForWork(),
                _payload->samplePreview, _payload->changedTiles, _payload->displayCommands, context.cancellation, seed);
            return SUCCEEDED(status)
                       ? HoldSampleBoundary(context, StrokeFinishTestGate::SampleBoundary::PreviewRecorded)
                       : FailureResult(status);
        }
        case WorkerAction::SampleRetire:
            // The draft has returned to its owner; this producer owns only the consumed wrapper and old preview.
            return HoldSampleBoundary(context, StrokeFinishTestGate::SampleBoundary::ReturningDraft);
        case WorkerAction::WarpPrepare:
        {
            if (!_payload || !_payload->warpInput ||
                !_payload->warpInput->PrepareReplayForWork(_usesGpu, context.cancellation, _payload->memory))
                return FailureFor(context.cancellation);
            _payload->warpPointCount = _payload->warpInput->PointsForWork().size();
            if (!_payload->warpPointCount)
                return {JobStatus::Failed, E_UNEXPECTED};
            [[fallthrough]];
        }
        case WorkerAction::WarpSamplePrepare:
            _payload->sampleCoverage = _payload->warpInput->PrepareNextSampleForWork(context.cancellation);
            return _payload->sampleCoverage ? JobResult{} : FailureFor(context.cancellation);
        case WorkerAction::WarpSampleCompose:
            if (!_payload || !_payload->warpInput || !_payload->sampleCoverage ||
                !_payload->warpInput->ComposeSampleForWork(*_payload->sampleCoverage))
                return FailureFor(context.cancellation);
            _payload->sampleCoverage.reset(); // Borrow ends after exact mapped-row/command/barrier acknowledgements.
            return {};
        case WorkerAction::Prepare:
        {
            if (!_payload || (_warpReplay ? !_payload->warpInput : !_payload->input))
                return {JobStatus::Failed, E_UNEXPECTED};
            _payload->coverage = _warpReplay
                                     ? _payload->warpInput->PrepareFinishForWork(context.cancellation)
                                     : _payload->input->DraftForWork().PrepareFinalCoverage(context.cancellation);
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
            if (!_payload || (_sampleInFlight ? !_payload->sampleCoverage : !_payload->coverage) ||
                FAILED(_recordingAdmission))
                return {JobStatus::Failed, FAILED(_recordingAdmission) ? _recordingAdmission : E_UNEXPECTED};
            wil::com_ptr_nothrow<ID3D11DeviceContext> deferred;
            HRESULT status = _payload->device->CreateDeferredContext(0, deferred.put());
            if (FAILED(status))
                return {JobStatus::Failed, status};
            GpuRecordingCommands recording;
            status = recording.InitializeForWork(deferred.get(), _payload->commands, context.cancellation);
            if (SUCCEEDED(status))
            {
                if (_sampleInFlight)
                {
                    const auto& sample = _payload->sampleCoverage.value();
                    status =
                        GpuBrushCoverage::RecordEpochForWork(_payload->epoch, deferred.get(), recording, sample.tiles,
                                                             sample.settled, sample.tail, sample.params);
                }
                else
                {
                    const auto& final = _payload->coverage.value();
                    status = GpuBrushCoverage::RecordEpochForWork(_payload->epoch, deferred.get(), recording,
                                                                  final.tiles, final.settled, {}, final.params);
                }
            }
            return status == E_ABORT ? JobResult{JobStatus::Cancelled, E_ABORT}
                   : FAILED(status)  ? JobResult{JobStatus::Failed, status}
                                     : JobResult{};
        }
        case WorkerAction::RetireCommands:
            return {}; // Nonthrowing retirement is in RetireWorkerStage, including cancellation-skipped Run.
        case WorkerAction::Install:
            if (!_payload || !_rows.data || _nextCoverage >= CoverageTiles().size())
                return {JobStatus::Failed, E_UNEXPECTED};
            _installed = _warpReplay
                             ? (_sampleInFlight ? _payload->warpInput->InstallSampleCoverageForWork(
                                                      CoverageTiles()[_nextCoverage].key, {_rows.data, _rows.count})
                                                : _payload->warpInput->InstallFinalCoverageForWork(
                                                      CoverageTiles()[_nextCoverage].key, {_rows.data, _rows.count}))
                         : _sampleInFlight ? _payload->sampleInput->DraftForWork().InstallSampleCoverage(
                                                 CoverageTiles()[_nextCoverage].key, {_rows.data, _rows.count})
                                           : _payload->input->DraftForWork().InstallFinalCoverage(
                                                 CoverageTiles()[_nextCoverage].key, {_rows.data, _rows.count});
            return _installed ? JobResult{} : FailureFor(context.cancellation);
        case WorkerAction::Compose:
            // All exact coverage loans have acknowledged. No worker calls the borrowed engine/context.
            if (_warpReplay)
            {
                if (!_payload || !_payload->warpInput || !_payload->coverage ||
                    !_payload->warpInput->ComposeFinishForWork(*_payload->coverage))
                    return FailureFor(context.cancellation);
                auto completed = _payload->warpInput->TakeReplayPublicationForWork();
                if (!completed)
                    return FailureFor(context.cancellation);
                _payload->input.emplace(std::move(completed.value()));
                _payload->warpInput.reset();
            }
            if (!_payload || !_payload->input || !_payload->coverage ||
                (!_warpReplay && !_payload->input->DraftForWork().ComposeFinalCoverage(*_payload->coverage)))
                return FailureFor(context.cancellation);
            _payload->coverage.reset();
            _payload->epoch.ResetForWork(); // only after actual completion or confirmed removal
            if (!_processingSamples)
                _payload->completion = {}; // A live preview still needs this original last-frame barrier.
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
            return _warpLive ? HoldSampleBoundary(context, StrokeFinishTestGate::SampleBoundary::FinalComposed)
                             : JobResult{};
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
        if (_payload && _workerAction == WorkerAction::SampleRecordPreview)
            _payload->displayCommands.FinishProducerForWork(result.status == JobStatus::Finished    ? S_OK
                                                            : result.status == JobStatus::Cancelled ? E_ABORT
                                                                                                    : result.error);
        if (_payload && _workerAction == WorkerAction::WarpOpeningRetire)
        {
            _payload->appliedWarpOpening.reset();
            _payload->warpOpeningResult.reset();
            _payload->warpOpeningInput.reset();
        }
        if (_payload && _workerAction == WorkerAction::SampleRetire)
        {
            if (_payload->previewPrevious && !_payload->previewPrevious->RetireForWork())
                std::terminate();
            _payload->previewPrevious.reset();
            _payload->sampleInput.reset(); // Successful owner apply already consumed its mutable draft reference.
            _payload->warpSampleInput.reset();
            _payload->sampleCoverage.reset();
            _payload->changedTiles.clear();
            _payload->samplePreview = {};
            // Keep the returned EMPTY epoch directory for the next exact Take; never reset live density here.
        }
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
        if (_workerAction == WorkerAction::RecordDisplay || _workerAction == WorkerAction::SampleRecordPreview)
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
            _retirementMemory = _payload->memory;
            if (_warpReplay)
            {
                if (_owner._gpuWork.DeviceGeneration() != _warpAdmissionGeneration ||
                    _owner._brushEngine.Ready() != _warpCompute)
                    return FailOwner(E_ABORT);
                if (!_owner._committedDisplay.ClaimPublicationSlotOwner(_ticket, _publicationSlot))
                    return FailOwner(E_OUTOFMEMORY);
                _usesGpu = _warpCompute;
                if (_usesGpu &&
                    (_owner._brushEngine.TileCount() || !_owner._brushEngine.CanTakeStrokeEpoch(_payload->epoch)))
                    return FailOwner(E_ABORT);
                HRESULT status = _owner._gpuWork.CaptureCompletionContext(_payload->completion);
                if (SUCCEEDED(status))
                    status = _owner._gpuWork.CaptureCompletionContext(_payload->displayCompletion);
                if (FAILED(status))
                    return FailOwner(status);
                auto input = _owner._session.TakeWarpFinishInput(_sessionOpening);
                if (!input)
                    return FailOwner(E_ABORT);
                _payload->warpInput.emplace(std::move(input.value()));
                _transferred = true;
                if (_usesGpu && !_owner._brushEngine.TakeStrokeEpoch(_payload->epoch))
                    std::terminate();    // Same serialized owner preflight; no intervening callback.
                _gpuAcknowledged = true; // Empty Warp epoch: no submitted use until recording.
                _payload->device = _owner._renderer.Device();
                _payload->immediate = _owner._renderer.Context();
                _sampleInFlight = true;
                return ScheduleWorkerOwner(Phase::WarpPrepared, WorkerAction::WarpPrepare);
            }
            if (_processingSamples)
            {
                _liveRetirement = _payload->emergency.get();
                _payload->device = _owner._renderer.Device();
                _payload->immediate = _owner._renderer.Context();
                auto status = _owner._gpuWork.CaptureCompletionContext(_payload->completion);
                if (SUCCEEDED(status))
                    status = _owner._gpuWork.CaptureCompletionContext(_payload->displayCompletion);
                if (FAILED(status))
                    return FailOwner(status);
                _originalContextCaptured = true;
                CommittedDisplayDescriptor descriptor;
                if (!_owner.DescribeCommittedDisplayOwner(descriptor))
                    return FailOwner(E_ABORT);
                (void)_owner._committedDisplay.AcquirePublishedSeedOwner(descriptor.key, _seed);
                _seedGraph = _seed ? _seed.GraphForWork() : nullptr;
                _payload->clone = std::move(_owner._canvasCloneSample);
                if (_warpLive)
                {
                    const auto* captured = _owner._canvasBrushInput.CapturedContext(_hostOpening);
                    const auto down = _owner._canvasBrushInput.Peek(_hostOpening);
                    if (!captured || !down || down->action != CanvasBrushInput::Action::Down ||
                        _owner._session.HasPendingWarp())
                        return FailOwner(E_ABORT);
                    auto opening = _owner._session.PrepareWarpOpeningInput(
                        {captured->settings, captured->smear, down->sample.point, _ownerId});
                    if (!opening)
                        return FailOwner(E_ABORT);
                    _warpTarget = captured->target;
                    _payload->warpOpeningInput.emplace(std::move(*opening));
                    return ScheduleWorkerOwner(Phase::WarpOpeningPrepared, WorkerAction::WarpOpening);
                }
                return AwaitInputOwner();
            }
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
        case Phase::WarpOpeningPrepared:
        {
            auto* prepared = _payload->warpOpeningResult && _payload->warpOpeningResult->prepared
                                 ? &*_payload->warpOpeningResult->prepared
                                 : nullptr;
            const auto down = _owner._canvasBrushInput.Peek(_hostOpening);
            if (!prepared || !down || down->action != CanvasBrushInput::Action::Down ||
                !_owner._session.ApplyPreparedWarpOpeningIfCurrent(*prepared))
                return FailOwner(E_ABORT);
            _sessionOpening = *_owner._session.PendingWarpOpening();
            _owner._canvasBrushSessionOpening = _sessionOpening;
            _owner._canvasBrushReplayed = true;
            _payload->appliedWarpOpening = prepared->TakeAppliedDraftForOwner();
            // This Down primes the original draft. It is never replayed as an additional dab.
            if (!_payload->appliedWarpOpening || !_owner._canvasBrushInput.Take(_hostOpening))
                std::terminate();
            return ScheduleWorkerOwner(Phase::WarpOpeningRetired, WorkerAction::WarpOpeningRetire);
        }
        case Phase::WarpOpeningRetired:
            return AwaitInputOwner();
        case Phase::Prepare:
        case Phase::SamplePrepared:
        case Phase::WarpPrepared:
            if (_sampleInFlight ? _payload->sampleCoverage->needsGpu : _payload->coverage->needsGpu)
            {
                _recordingAdmission = _payload->commandsInitialized
                                          ? _payload->commands.RestartOwner()
                                          : _payload->commands.InitializeOwner(_owner._jobs, _ticket,
                                                                               _payload->completion.DeviceGeneration());
                if (FAILED(_recordingAdmission))
                    return FailOwner(_recordingAdmission);
                _payload->commandsInitialized = true;
                _payload->commandsAuthorized = false;
                _payload->commandsRetired = false;
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
            return ComposeCoverageOwner();
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
            if (_nextCoverage < CoverageTiles().size())
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
            if (_samplePreviewBarrier)
            {
                _samplePreviewBarrier = false;
                return ScheduleWorkerOwner(Phase::SampleRetired, WorkerAction::SampleRetire);
            }
            return ComposeCoverageOwner();
        case Phase::SampleAvailable:
            return _warpLive ? CaptureNextWarpSampleOwner() : CaptureNextSampleOwner();
        case Phase::SampleComposed:
        {
            if (_sampleNoPixels)
                return PublishSampleOwner(false);
            _displayRecordingAdmission =
                _payload->displayCommandsInitialized
                    ? _payload->displayCommands.RestartOwner()
                    : _payload->displayCommands.InitializeOwner(_owner._jobs, _ticket,
                                                                _payload->displayCompletion.DeviceGeneration());
            if (FAILED(_displayRecordingAdmission))
                return FailOwner(_displayRecordingAdmission);
            _payload->displayCommandsInitialized = true;
            _payload->displayCommandsAuthorized = false;
            _payload->displayCommandsRetired = false;
            _displayRecordingAdmission =
                _owner._recordedCommands.AttachOwner(_payload->displayCommands, _ticket, *this, _displaySource);
            _displayRecordingJoined = false;
            _phase = Phase::SampleRecordingPreview;
            _workerAction = WorkerAction::SampleRecordPreview;
            if (!_owner._jobs.Resume(_ticket, JobExecutionLane::GpuRecording))
                std::terminate();
            return true;
        }
        case Phase::SampleRecordingPreview:
            _phase = Phase::AwaitDisplayCommands;
            _workerAction = WorkerAction::AwaitAck;
            _owner._jobs.NotifyOwnerWork();
            return true;
        case Phase::SampleRetired:
            _sampleInFlight = false;
            if (_gate)
                _gate->samplesAcknowledged.fetch_add(1, std::memory_order_release);
            if (_finalAfterSample)
                return _warpLive ? CaptureWarpFinalOwner() : CaptureFinalOwner();
            return AwaitInputOwner();
        case Phase::WarpSampleComposed:
            ++_warpReplayIndex;
            _nextCoverage = 0;
            if (_warpReplayIndex < _payload->warpPointCount)
                return ScheduleWorkerOwner(Phase::WarpPrepared, WorkerAction::WarpSamplePrepare);
            _sampleInFlight = false;
            return ScheduleWorkerOwner(Phase::Prepare, WorkerAction::Prepare);
        case Phase::Compose:
        {
            if (_payload->result->status == StrokeFinishStatus::Unchanged)
            {
                DiscardFinishOwner();
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
            if (!_seed)
            {
                CommittedDisplayDescriptor descriptor;
                if (!_owner.DescribeCommittedDisplayOwner(descriptor))
                    return FailOwner(E_ABORT);
                (void)_owner._committedDisplay.AcquirePublishedSeedOwner(descriptor.key, _seed);
                _seedGraph = _seed ? _seed.GraphForWork() : nullptr;
            }
            _displayRecordingAdmission =
                _payload->displayCommandsInitialized
                    ? _payload->displayCommands.RestartOwner()
                    : _payload->displayCommands.InitializeOwner(_owner._jobs, _ticket,
                                                                _payload->displayCompletion.DeviceGeneration());
            if (FAILED(_displayRecordingAdmission))
                return FailOwner(_displayRecordingAdmission);
            _payload->displayCommandsInitialized = true;
            _payload->displayCommandsAuthorized = false;
            _payload->displayCommandsRetired = false;
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
            if (_sampleInFlight)
                return PublishSampleOwner(true);
            [[fallthrough]];
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
        if (_phase == Phase::AwaitInput)
            return _owner._canvasBrushInput.PendingCount() != 0 || _pendingDown.has_value() ||
                   _cancellation.IsCancelled() || _owner._canvasBrushCancelledOpening == _hostOpening;
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
        if (_warpReplay && !_terminal && _owner._gpuWork.DeviceGeneration() != _warpAdmissionGeneration)
            _originalDeviceInvalid =
                true; // Original worker/copy acknowledgements remain due; no callback edits payload.
        if (_processingSamples && !_terminal && _originalContextCaptured &&
            (_payload->device.get() != _owner._renderer.Device() ||
             _payload->completion.DeviceGeneration() != _owner._gpuWork.DeviceGeneration()))
            PrepareDeviceReplacementOwner();
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

    void PrepareDeviceReplacementOwner() noexcept
    {
        if (!_processingSamples || _owner._shutdownStarted || _terminal || _originalDeviceInvalid)
            return;
        _originalDeviceInvalid = true;
        // A recorder can still use the immutable preview seed. Exclude later frames now; detach that graph
        // only after its actual recorder acknowledgement, through normal original-stream retirement.
        _owner._acknowledgedStrokePreview = nullptr;
        _owner.InvalidateFrame();
        auto* live = _owner._session.MutableBrush();
        if (live && _liveRetirement)
        {
            const auto* expected = live->ExpectedFinishCoverageEngine();
            if (!expected ||
                (!expected->UsesCpuPermanent() && !_owner._brushEngine.CanTakeStrokeEpoch(_liveRetirement->epoch)))
                std::terminate();
            BrushCoverageEngine* engine = nullptr;
            auto input = _owner._session.TakeStrokeFinishInput(_sessionOpening, engine);
            if (!input || engine != expected)
                std::terminate();
            _liveRetirement->input.emplace(std::move(input.value()));
            _transferred = true;
            if (!expected->UsesCpuPermanent() && !_owner._brushEngine.TakeStrokeEpoch(_liveRetirement->epoch))
                std::terminate();
            if (_liveRetirement->epoch.HasResources())
                _gpuAcknowledged = false;
        }
        // Cancellation never fabricates producer join or original GPU acknowledgement.
        (void)_owner._jobs.Cancel(_ticket);
        _owner._jobs.NotifyOwnerWork();
    }

    void PumpOwner() noexcept
    {
        if (!HasReadyOwner())
            return;
        if (_phase == Phase::AwaitInput)
        {
            (void)ScheduleWorkerOwner(Phase::SampleAvailable, WorkerAction::AwaitAck);
            return;
        }
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
        const auto& tile = CoverageTiles()[_nextCoverage];
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
        // The whole CPU payload has died on its joined worker. A surviving weak control block still needs
        // this original descriptor charge; never manufacture successful publication on failure/cancel.
        if (_retirementMemory && !_retirementMemory->IsBound() &&
            FAILED(_retirementMemory->BindForRetirement(_owner._jobs, _ticket)))
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
            DiscardFinishOwner();
        if (_processingSamples && !_warpLive && !_applied)
            (void)_owner._session.DiscardStrokeSampleIfCurrent(_sessionOpening);
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
        // A GPU callback can publish failure before the recorder's retirement callback observes Current().
        // Device replacement is an immutable original-parent fact, regardless of that callback ordering.
        const auto outcome = _stale || _originalDeviceInvalid || !CurrentRequest() ? HostOutcome::Stale
                             : terminal.status == JobStatus::Cancelled             ? HostOutcome::Cancelled
                             : terminal.status == JobStatus::Failed                ? HostOutcome::Failed
                                                                                   : HostOutcome::Succeeded;
        if (_applied)
            _owner.DocumentChanged(); // Notifications/chrome run after every root has been installed together.
        _owner.ResumeHostHistory(_stamp, outcome, outcome == HostOutcome::Stale ? E_ABORT : terminal.error);
    }

  private:
    std::span<const BrushKernelTile> CoverageTiles() const noexcept
    {
        if (!_payload)
            return {};
        if (_sampleInFlight)
            return _payload->sampleCoverage ? std::span<const BrushKernelTile>(_payload->sampleCoverage->tiles)
                                            : std::span<const BrushKernelTile>{};
        return _payload->coverage ? std::span<const BrushKernelTile>(_payload->coverage->tiles)
                                  : std::span<const BrushKernelTile>{};
    }
    bool AwaitInputOwner() noexcept
    {
        _phase = Phase::AwaitInput;
        _workerAction = WorkerAction::AwaitAck;
        if (_owner._canvasBrushInput.PendingCount() || _pendingDown)
            _owner._jobs.NotifyOwnerWork();
        return true; // Suspended original job; no timer, polling or unchanged self-rearm.
    }
    bool ComposeCoverageOwner() noexcept
    {
        if (_warpReplay && _sampleInFlight)
            return ScheduleWorkerOwner(Phase::WarpSampleComposed, WorkerAction::WarpSampleCompose);
        return _sampleInFlight ? ScheduleWorkerOwner(Phase::SampleComposed, WorkerAction::SampleCompose)
                               : ScheduleWorkerOwner(Phase::Compose, WorkerAction::Compose);
    }
    HRESULT GrowGpuOwner(std::uint64_t total) noexcept
    {
        if (!total)
            return E_INVALIDARG;
        if (_owner._gpuWork.DeviceGeneration() != _payload->completion.DeviceGeneration() ||
            _payload->device.get() != _owner._renderer.Device() || FAILED(_payload->completion.DeviceStatusOwner()))
            return E_ABORT;
        GpuCapabilities::VideoMemorySnapshot snapshot;
        const auto status = GpuCapabilities::QueryVideoMemorySnapshot(_payload->device.get(), snapshot);
        if (FAILED(status))
            return status;
        const auto& segment = _owner._renderer.IsWarp() && snapshot.nonLocal.available && snapshot.nonLocal.budget
                                  ? snapshot.nonLocal
                                  : snapshot.local;
        if (!segment.available || !segment.budget)
            return E_OUTOFMEMORY;
        GpuResidencyAdmission result;
        const GpuResidencySnapshot fresh{_payload->completion.DeviceGeneration(), segment.budget, segment.currentUsage};
        if (_gpuReservation)
            return _owner._graphGpuBudget.IncreaseOwner(_gpuReservation, (std::max)(total, _gpuReservation.Bytes()),
                                                        fresh, result)
                       ? S_OK
                       : E_OUTOFMEMORY;
        _gpuReservation = _owner._graphGpuBudget.ReserveOwner(total, fresh, result);
        return _gpuReservation ? S_OK : E_OUTOFMEMORY;
    }
    bool CaptureNextSampleOwner() noexcept
    {
        const auto* captured = _owner._canvasBrushInput.CapturedContext(_hostOpening);
        auto* stroke = _owner._session.MutableBrush();
        if (!captured || !stroke || _owner._session.PendingBrushOpening() != _sessionOpening)
            return FailOwner(E_ABORT);
        auto event =
            _pendingDown ? std::exchange(_pendingDown, std::nullopt) : _owner._canvasBrushInput.Take(_hostOpening);
        if (!event)
            return AwaitInputOwner();
        if (event->action == CanvasBrushInput::Action::Lost)
            return CaptureFinalOwner();
        if (event->action == CanvasBrushInput::Action::Down)
        {
            _owner._canvasBrushReplayed = true;
            if ((event->sample.modifiers & MK_SHIFT) != 0)
            {
                const auto previous = _owner._session.ShiftLineStart();
                if (previous)
                {
                    _pendingDown = event;
                    _pendingDown->action = CanvasBrushInput::Action::Move;
                    event->sample.point = previous.value();
                }
            }
        }
        _finalAfterSample = event->action == CanvasBrushInput::Action::Up;
        const auto plan = stroke->PlanSampleMemory(event->sample.point);
        const auto& document = std::as_const(_owner._session).Document();
        if (!plan || !document)
            return FailOwner(E_OUTOFMEMORY);
        RasterResidencyPlanOptions options;
        options.deviceGeneration = _payload->completion.DeviceGeneration();
        const auto residency = RasterResidencyAdmission::Estimate(document.value(), options);
        if (!residency.IsValid())
            return FailOwner(E_OUTOFMEMORY);
        // Complete phase maximum. Current and replacement canvases/private grids coexist; old committed
        // roots already have their own admission. No fixed allowance accumulates on revisited samples.
        auto cpu = _retirement;
        auto gpu = residency.GpuBytes();
        const auto gridPixels = static_cast<std::uint64_t>(stroke->Width()) * stroke->Height();
        const auto gridChannels = stroke->IsMask() ? 1u : 4u;
        if (gridPixels > (std::numeric_limits<std::uint64_t>::max)() / (4 * gridChannels) ||
            plan->maximumTiles > (std::numeric_limits<std::uint64_t>::max)() / (256ull * 256 * 8 + 32768))
            return FailOwner(E_OUTOFMEMORY);
        const auto grids = gridPixels * 4 * gridChannels; // <= 2*base per rectangular mip chain, two private grids.
        const auto density = plan->maximumTiles * (256ull * 256 * 8 + 32768);
        if (!AddBytes(gpu, grids) || !AddBytes(gpu, residency.GpuBytes()) || !AddBytes(gpu, density) ||
            !AddBytes(gpu, 128ull * 1024 * 1024) || !AddBytes(cpu, plan->transientCpuBytes) ||
            !AddBytes(cpu, plan->persistentCpuBytes) || !AddBytes(cpu, residency.CpuBytes()) ||
            !AddBytes(cpu, residency.CpuBytes()) || !AddBytes(cpu, 128ull * 1024 * 1024) ||
            !AddBytes(cpu, sizeof(StrokeFinishJob) + sizeof(FinishPayload)) ||
            (_owner._renderer.IsWarp() && !AddBytes(cpu, gpu)))
            return FailOwner(E_OUTOFMEMORY);
        auto status = _owner._jobs.IncreaseReservation(_ticket, (std::max)(cpu, _cpuBytes));
        if (SUCCEEDED(status))
            status = stroke->IncreaseOpeningMemoryForSample(_owner._jobs, plan.value());
        if (SUCCEEDED(status))
            status = GrowGpuOwner(gpu);
        if (FAILED(status))
            return FailOwner(status);
        _cpuBytes = (std::max)(cpu, _cpuBytes);
        _sampleCpuBase = cpu;
        _sampleGpuBase = gpu;
        _sampleEngine = nullptr;
        const auto* expected = stroke->ExpectedFinishCoverageEngine();
        if (!expected)
            return FailOwner(E_ABORT);
        _usesGpu = !expected->UsesCpuPermanent();
        if (_usesGpu && !_owner._brushEngine.CanTakeStrokeEpoch(_payload->epoch))
            return FailOwner(E_ABORT);
        auto input = _owner._session.TakeStrokeSampleInput(_sessionOpening, event->sample.point, _sampleEngine);
        if (!input || !_sampleEngine)
            return FailOwner(E_ABORT);
        _payload->sampleInput.emplace(std::move(input.value()));
        _sampleInFlight = true;
        if (_usesGpu &&
            (_sampleEngine != &_owner._brushEngine || !_owner._brushEngine.TakeStrokeEpoch(_payload->epoch)))
            std::terminate();
        _gpuAcknowledged = !_usesGpu || !_payload->epoch.HasResources();
        _nextCoverage = 0;
        _owner._brushHover = event->sample.point;
        return ScheduleWorkerOwner(Phase::SamplePrepared, WorkerAction::SamplePrepare);
    }
    bool PublishSampleOwner(bool publishPreview) noexcept
    {
        const bool current =
            _warpLive ? _payload->warpSampleInput && _owner._session.IsWarpSampleCurrent(*_payload->warpSampleInput)
                      : _payload->sampleInput && _owner._session.IsStrokeSampleCurrent(*_payload->sampleInput);
        if (!current || (publishPreview && !_payload->previewCandidate) ||
            _payload->device.get() != _owner._renderer.Device() ||
            _payload->immediate.get() != _owner._renderer.Context())
            return FailOwner(E_ABORT);
        if (publishPreview)
        {
            const auto status = _payload->previewCandidate->PublishOwner(_payload->immediate.get());
            if (FAILED(status))
                return FailOwner(status);
        }
        if (!_warpLive && _usesGpu && !_owner._brushEngine.RestoreStrokeEpoch(_payload->epoch))
            return FailOwner(E_ABORT);
        if (_warpLive ? !_owner._session.ApplyWarpSampleIfCurrent(*_payload->warpSampleInput)
                      : !_owner._session.ApplyStrokeSampleIfCurrent(_payload->sampleInput.value(), _sampleEngine))
            std::terminate(); // Exact serialized preflight, no callback or device mutation intervenes.
        _sampleEngine = nullptr;
        if (!publishPreview)
            return ScheduleWorkerOwner(Phase::SampleRetired, WorkerAction::SampleRetire);
        _payload->previewPrevious = std::move(_payload->previewCurrent);
        _payload->previewCurrent = std::move(_payload->previewCandidate);
        if (_gate)
        {
            _gate->previewsPublished.fetch_add(1);
            if (_payload->previewPrevious)
                _gate->previewsReplaced.fetch_add(1);
        }
        _owner._acknowledgedStrokePreview = _payload->previewCurrent.get();
        _owner.InvalidateFrame();
        _owner.SyncBrushCursor();
        if (_payload->previewPrevious && !_payload->previewPrevious->DetachOwner())
            std::terminate();
        // Exclusion of future old frames precedes this original-stream barrier. Recorder/GPU acknowledgements
        // alone do not cover a frame submitted while the stream-retirement worker was still running.
        _samplePreviewBarrier = true;
        _gpuAcknowledged = false;
        _phase = Phase::AwaitRetirementSlot;
        _workerAction = WorkerAction::AwaitAck;
        _owner._jobs.NotifyOwnerWork();
        return true;
    }
    bool CaptureNextWarpSampleOwner() noexcept
    {
        const auto* draft = _owner._session.PendingWarp();
        const auto event = _owner._canvasBrushInput.Peek(_hostOpening);
        if (!draft || _owner._session.PendingWarpOpening() != _sessionOpening)
            return FailOwner(E_ABORT);
        if (!event)
            return AwaitInputOwner();
        if (event->action == CanvasBrushInput::Action::Lost)
        {
            (void)_owner._canvasBrushInput.Take(_hostOpening);
            return CaptureWarpFinalOwner(); // seal only the accepted prefix; no extra terminal dab
        }
        if (event->action == CanvasBrushInput::Action::Down)
            return FailOwner(E_UNEXPECTED);
        const auto plan = draft->PlanSampleMemory(event->sample.point);
        const auto& document = std::as_const(_owner._session).Document();
        if (!plan || !document)
            return FailOwner(E_OUTOFMEMORY);
        RasterResidencyPlanOptions options;
        options.deviceGeneration = _payload->completion.DeviceGeneration();
        const auto residency = RasterResidencyAdmission::Estimate(*document, options);
        const auto metadata = WarpPreviewMetadataMemoryBytes(document->width, document->height);
        if (!residency.IsValid() || !metadata)
            return FailOwner(E_OUTOFMEMORY);
        auto cpu = _retirement;
        auto gpu = residency.GpuBytes();
        const auto pixels = static_cast<std::uint64_t>(draft->Width()) * draft->Height();
        // Two private grid mip chains, two complete document graphs, bounded upload/reader scratch,
        // old/new tile versions, source/history overlap and original recorder/shadow/retirement transport.
        if (!AddBytes(gpu, pixels * 16) || !AddBytes(gpu, residency.GpuBytes()) ||
            !AddBytes(gpu, 128ull * 1024 * 1024) || !AddBytes(cpu, plan->persistentCpuBytes) ||
            !AddBytes(cpu, plan->transientCpuBytes) || !AddBytes(cpu, 2 * metadata) ||
            !AddBytes(cpu, residency.CpuBytes()) || !AddBytes(cpu, residency.CpuBytes()) ||
            !AddBytes(cpu, 128ull * 1024 * 1024) || (_owner._renderer.IsWarp() && !AddBytes(cpu, gpu)))
            return FailOwner(E_OUTOFMEMORY);
        auto status = _owner._jobs.IncreaseReservation(_ticket, (std::max)(cpu, _cpuBytes));
        if (SUCCEEDED(status))
            status = GrowGpuOwner(gpu);
        if (FAILED(status))
            return FailOwner(status); // no sample consumed or draft detached before complete admission
        _cpuBytes = (std::max)(cpu, _cpuBytes);
        _sampleCpuBase = cpu;
        _sampleGpuBase = gpu;
        auto input = _owner._session.TakeWarpSampleInput(_sessionOpening, *plan);
        if (!input)
            return FailOwner(E_ABORT);
        _warpSampleSequence = plan->sequence;
        _payload->warpSampleInput.emplace(std::move(*input));
        const auto accepted = _owner._canvasBrushInput.Take(_hostOpening);
        if (!accepted || accepted->action != event->action)
            std::terminate(); // serialized owner, Peek/Take have no callback between them
        _finalAfterSample = event->action == CanvasBrushInput::Action::Up;
        _sampleInFlight = true;
        _usesGpu = false; // live Warp pixels are CPU work; recorded display uses the original GPU context
        _gpuAcknowledged = true;
        _owner._brushHover = event->sample.point;
        return ScheduleWorkerOwner(Phase::SampleComposed, WorkerAction::WarpLiveSample);
    }
    bool CaptureWarpFinalOwner() noexcept
    {
        const auto* draft = _owner._session.PendingWarp();
        if (!draft || _owner._session.PendingWarpOpening() != _sessionOpening)
            return FailOwner(E_ABORT);
        if (draft->Points().empty())
        {
            _terminal = true;
            _terminalResult = {}; // no document/history publication for a primed-only gesture
            return BeginRetirementOwner();
        }
        const auto plan = _owner._session.PlanWarpFinishMemory(_warpCompute);
        if (!plan || !_owner._committedDisplay.ClaimPublicationSlotOwner(_ticket, _publicationSlot))
            return FailOwner(E_OUTOFMEMORY);
        auto cpu = (std::max)(_sampleCpuBase, _retirement);
        auto gpu = _sampleGpuBase;
        if (!AddBytes(cpu, plan->cpuBytes) || !AddBytes(cpu, 128ull * 1024 * 1024) || !AddBytes(gpu, plan->gpuBytes) ||
            (_owner._renderer.IsWarp() && !AddBytes(cpu, plan->gpuBytes)))
            return FailOwner(E_OUTOFMEMORY);
        auto status = _owner._jobs.IncreaseReservation(_ticket, (std::max)(cpu, _cpuBytes));
        if (SUCCEEDED(status) && gpu)
            status = GrowGpuOwner(gpu);
        if (FAILED(status))
            return FailOwner(status);
        _cpuBytes = (std::max)(cpu, _cpuBytes);
        if (_owner._gpuWork.DeviceGeneration() != _warpAdmissionGeneration ||
            _owner._brushEngine.Ready() != _warpCompute ||
            (_warpCompute &&
             (_owner._brushEngine.TileCount() || !_owner._brushEngine.CanTakeStrokeEpoch(_payload->epoch))))
            return FailOwner(E_ABORT);
        auto input = _owner._session.TakeWarpFinishInput(_sessionOpening);
        if (!input)
            return FailOwner(E_ABORT);
        _payload->warpInput.emplace(std::move(*input));
        _transferred = true;
        _warpReplay = true;
        _usesGpu = _warpCompute;
        if (_usesGpu && !_owner._brushEngine.TakeStrokeEpoch(_payload->epoch))
            std::terminate();
        _gpuAcknowledged = true; // empty replay density; actual submitted uses still get original barriers
        _sampleInFlight = true;
        _finalAfterSample = false;
        _nextCoverage = 0;
        return ScheduleWorkerOwner(Phase::WarpPrepared, WorkerAction::WarpPrepare);
    }
    bool CaptureFinalOwner() noexcept
    {
        auto* stroke = _owner._session.MutableBrush();
        if (!stroke || _owner._session.PendingBrushOpening() != _sessionOpening)
            return FailOwner(E_ABORT);
        const auto plan = stroke->PlanFinishMemory();
        if (!plan || !_owner._committedDisplay.ClaimPublicationSlotOwner(_ticket, _publicationSlot))
            return FailOwner(E_OUTOFMEMORY);
        auto cpu = (std::max)(_sampleCpuBase, _retirement);
        auto gpu = _sampleGpuBase;
        if (!AddBytes(cpu, plan->additionalCpuBytes) || !AddBytes(cpu, plan->retirementCpuBytes) ||
            !AddBytes(cpu, 128ull * 1024 * 1024) || !AddBytes(gpu, plan->additionalGpuBytes) ||
            (_owner._renderer.IsWarp() && !AddBytes(cpu, plan->additionalGpuBytes)))
            return FailOwner(E_OUTOFMEMORY);
        auto status = _owner._jobs.IncreaseReservation(_ticket, (std::max)(cpu, _cpuBytes));
        if (SUCCEEDED(status) && gpu)
            status = GrowGpuOwner(gpu);
        if (FAILED(status))
            return FailOwner(status);
        _cpuBytes = (std::max)(cpu, _cpuBytes);
        if (_owner._session.MutableBrush() != stroke || _owner._session.PendingBrushOpening() != _sessionOpening)
            return FailOwner(E_ABORT);
        auto* expected = stroke->ExpectedFinishCoverageEngine();
        if (!expected)
            return FailOwner(E_ABORT);
        _usesGpu = !expected->UsesCpuPermanent();
        if (_usesGpu && !_owner._brushEngine.CanTakeStrokeEpoch(_payload->epoch))
            return FailOwner(E_ABORT);
        BrushCoverageEngine* engine = nullptr;
        auto input = _owner._session.TakeStrokeFinishInput(_sessionOpening, engine);
        if (!input || engine != expected)
            return FailOwner(E_ABORT);
        _payload->input.emplace(std::move(input.value()));
        _transferred = true;
        if (_usesGpu && !_owner._brushEngine.TakeStrokeEpoch(_payload->epoch))
            std::terminate();
        _gpuAcknowledged = !_usesGpu || !_payload->epoch.HasResources();
        _sampleInFlight = false;
        _nextCoverage = 0;
        _finalAfterSample = false;
        return ScheduleWorkerOwner(Phase::Prepare, WorkerAction::Prepare);
    }
    void CaptureLiveRetirementOwner() noexcept
    {
        if (_warpLive)
        {
            // Called after original useful producers join (or actual Close joins all producers).
            // Updating input stays on this payload; the cache wrapper grants no sample apply authority.
            if (!_payload || !_sessionOpening || _warpReplay || _payload->warpRetirement ||
                _owner._session.PendingWarpOpening() != _sessionOpening)
                return;
            auto retired = _owner._session.TakeWarpRetirementInput(_sessionOpening, _warpSampleSequence);
            if (!retired)
                std::terminate();
            _payload->warpRetirement.emplace(std::move(*retired));
            _transferred = true;
            return;
        }
        // No sample/final allocation or work follows this cleanup capture. The complete live draft's opening
        // lease and parent declaration already cover its sources and retained epoch.
        if (!_processingSamples || !_payload || _payload->input || !_owner._session.PendingBrush() ||
            _owner._session.PendingBrushOpening() != _sessionOpening)
            return;
        const auto* expected = _owner._session.PendingBrush()->ExpectedFinishCoverageEngine();
        if (!expected)
            std::terminate();
        _usesGpu = !expected->UsesCpuPermanent();
        BrushCoverageEngine* engine = nullptr;
        auto input = _owner._session.TakeStrokeFinishInput(_sessionOpening, engine);
        if (!input || engine != expected)
            std::terminate();
        _payload->input.emplace(std::move(input.value()));
        _transferred = true;
        if (_usesGpu && !_owner._brushEngine.TakeStrokeEpoch(_payload->epoch))
            std::terminate();
        if (_payload->epoch.HasResources())
            _gpuAcknowledged = false;
    }
    void DetachPreviewsOwner() noexcept
    {
        if (!_payload || _payload->previewsDetached)
            return;
        if (_owner._acknowledgedStrokePreview == _payload->previewCurrent.get())
        {
            _owner._acknowledgedStrokePreview = nullptr;
            _owner.InvalidateFrame();
        }
        for (auto* graph : {_payload->previewCurrent.get(), _payload->previewCandidate.get()})
            if (graph && !graph->DetachOwner())
                std::terminate();
        if (_payload->previewCurrent)
            _gpuAcknowledged = false; // Last original frame barrier is still due.
        _payload->previewsDetached = true;
    }
    enum class Phase : std::uint8_t
    {
        Allocate,
        WarpOpeningPrepared,
        WarpOpeningRetired,
        AwaitInput,
        SampleAvailable,
        SamplePrepared,
        SampleComposed,
        SampleRecordingPreview,
        SampleRetired,
        WarpPrepared,
        WarpSampleComposed,
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
        WarpOpening,
        WarpOpeningRetire,
        WarpLiveSample,
        SamplePrepare,
        SampleCompose,
        SampleRecordPreview,
        SampleRetire,
        WarpPrepare,
        WarpSamplePrepare,
        WarpSampleCompose,
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
    JobResult HoldSampleBoundary(JobContext& context, StrokeFinishTestGate::SampleBoundary boundary) noexcept
    {
        if (!_gate || _gate->holdSample != boundary ||
            _gate->samplesPrepared.load(std::memory_order_acquire) != _gate->holdSampleNumber)
            return {};
        _gate->sampleBoundaryVisits.fetch_add(1);
        _gate->sampleBoundaryOffOwner.fetch_add(GetCurrentThreadId() != _ownerThread ? 1u : 0u);
        if (!SetEvent(_gate->reached.get()))
            return {JobStatus::Failed, HRESULT_FROM_WIN32(GetLastError())};
        _owner._jobs.NotifyOwnerWork();
        const HANDLE waits[]{_gate->release.get(), context.shutdownEvent};
        const auto waited = WaitForMultipleObjects(2, waits, FALSE, StrokeFinishTestGate::kWaitTimeoutMs);
        if (context.cancellation.IsCancelled() || waited == WAIT_OBJECT_0 + 1)
            return {JobStatus::Cancelled, E_ABORT};
        if (waited != WAIT_OBJECT_0)
            return {JobStatus::Failed, HRESULT_FROM_WIN32(ERROR_TIMEOUT)};
        return _gate->failureResult.value_or(JobResult{});
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
               (!_warpLive || (captured->target == _owner._session.ActiveLayerID() &&
                               captured->mask == _owner._session.IsMaskSelected() && captured->tool == _owner._tool)) &&
               (_warpLive && !_sessionOpening ? !_owner._canvasBrushSessionOpening
                                              : _owner._canvasBrushSessionOpening == _sessionOpening);
    }
    bool Current() const noexcept
    {
        return !_originalDeviceInvalid && CurrentRequest() && _owner._utilityStamp == _stamp &&
               _owner._historyWork.IsCurrent(_stamp) &&
               (!_transferred || _applied ||
                ((_warpReplay || _warpLive) ? _owner._session.PendingWarpOpening()
                                            : _owner._session.PendingBrushOpening()) == _sessionOpening);
    }
    void DiscardFinishOwner() noexcept
    {
        if (_warpReplay || _warpLive)
        {
            if (_warpLive && _warpSampleSequence)
                (void)_owner._session.DiscardWarpSampleIfCurrent(_sessionOpening, *_warpSampleSequence);
            (void)_owner._session.DiscardWarpFinishIfCurrent(_sessionOpening);
        }
        else
            (void)_owner._session.DiscardStrokeFinishIfCurrent(_sessionOpening);
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
        CaptureLiveRetirementOwner();
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
        DetachPreviewsOwner();
        if (_payload && !_gpuAcknowledged)
        {
            _phase = Phase::AwaitRetirementSlot;
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
    std::uint64_t _sessionOpening;
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
    // The original accounting owner outlives payload destruction, allowing exact terminal or joined-Close
    // transfer before the last weak control owner. Contains no pixels, draft, UI or GPU resources.
    std::shared_ptr<SharedPublicationMemory> _retirementMemory;
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
    bool _processingSamples = false;
    bool _warpReplay;
    const bool _warpLive;
    const std::thread::id _ownerId = std::this_thread::get_id();
    Guid _warpTarget;
    std::optional<std::uint64_t> _warpSampleSequence;
    const bool _warpCompute = _owner._brushEngine.Ready();
    const std::uint64_t _warpAdmissionGeneration = _owner._gpuWork.DeviceGeneration();
    std::size_t _warpReplayIndex = 0;
    bool _originalDeviceInvalid = false;
    bool _originalContextCaptured = false;
    LiveRetirementCapture* _liveRetirement = nullptr; // Owner-only descriptor; whole owner retires with payload.
    bool _sampleInFlight = false;
    bool _sampleNoPixels = false;
    bool _finalAfterSample = false;
    bool _samplePreviewBarrier = false;
    BrushCoverageEngine* _sampleEngine = nullptr; // Owner only; no worker accesses this borrow.
    std::optional<CanvasBrushInput::Event> _pendingDown;
    std::uint64_t _sampleCpuBase = 0;
    std::uint64_t _sampleGpuBase = 0;
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
    return _attached && (_owner._strokeFinishCurrent != nullptr || _owner.PixelFinalPublicationPendingOwner());
}
bool Application::StrokeFinishSource::HasReady() const noexcept
{
    return _attached && ((_owner._strokeFinishCurrent && _owner._strokeFinishCurrent->HasReadyOwner()) ||
                         _owner.PixelFinalPublicationReadyOwner());
}
void Application::StrokeFinishSource::Pump() noexcept
{
    if (!_attached)
        return;
    if (_owner._strokeFinishCurrent && _owner._strokeFinishCurrent->HasReadyOwner())
        _owner._strokeFinishCurrent->PumpOwner();
    else if (_owner.PixelFinalPublicationReadyOwner())
        _owner.PumpPixelFinalPublicationOwner();
}
void Application::StrokeFinishSource::DeviceChangedOwner() noexcept
{
    if (_attached && _owner._strokeFinishCurrent)
        _owner._strokeFinishCurrent->DeviceChangedOwner();
}
void Application::StrokeFinishSource::PrepareDeviceReplacementOwner() noexcept
{
    if (_attached && _owner._strokeFinishCurrent)
        _owner._strokeFinishCurrent->PrepareDeviceReplacementOwner();
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
    return StartStrokeProcessing(opening, false);
}

HRESULT Application::StartStrokeSamples(std::uint64_t opening) noexcept
{
    return StartStrokeProcessing(opening, true);
}

HRESULT Application::StartWarpFinish(std::uint64_t opening) noexcept
{
    return StartStrokeProcessing(opening, false, true);
}

HRESULT Application::StartWarpLive(std::uint64_t opening) noexcept
{
    return StartStrokeProcessing(opening, true, false, true);
}

HRESULT Application::StartStrokeProcessing(std::uint64_t opening, bool processingSamples, bool warpReplay,
                                           bool warpLive) noexcept
{
    const auto* captured = _canvasBrushInput.CapturedContext(opening);
    const auto* stroke = _session.PendingBrush();
    if ((warpReplay && processingSamples) || !_committedDisplayInitialized || !_preparedLayerRowsEnabled || !captured ||
        (warpLive ? (_canvasBrushSessionOpening || _session.HasPendingWarp())
                  : ((warpReplay ? !_session.PendingWarp() : !stroke) || !_canvasBrushSessionOpening ||
                     (warpReplay ? _session.PendingWarpOpening() : _session.PendingBrushOpening()) !=
                         _canvasBrushSessionOpening)) ||
        !_activeHostRequest || _utilityTicket || _strokeFinishCurrent ||
        captured->documentSerial != _session.DocumentSerial())
        return E_UNEXPECTED;
    const auto down = warpLive ? _canvasBrushInput.Peek(opening) : std::nullopt;
    if (warpLive && (!down || down->action != CanvasBrushInput::Action::Down || captured->tool != EditorTool::Smear ||
                     captured->mask || captured->smear == SmearMode::Blur))
        return E_ABORT;
    const auto openingBytes = warpLive
                                  ? _session.WarpOpeningMemoryBytes({captured->settings, captured->smear,
                                                                     down->sample.point, std::this_thread::get_id()})
                                  : 0;
    const auto plan = !warpReplay && !warpLive && stroke ? stroke->PlanFinishMemory() : std::nullopt;
    const auto warpPlan = warpReplay ? _session.PlanWarpFinishMemory(_brushEngine.Ready()) : std::nullopt;
    auto retirement = warpReplay && warpPlan ? warpPlan->cpuBytes : _session.RasterEditRetirementMemoryBytes();
    if (warpLive)
    {
        if (!openingBytes || !retirement || openingBytes > UINT64_MAX - retirement)
            return E_OUTOFMEMORY;
        retirement += openingBytes;
    }
    if ((warpLive ? !openingBytes : (warpReplay ? !warpPlan : !plan)) || !retirement)
        return E_OUTOFMEMORY;
    const auto gpu = warpLive ? 0ULL : (warpReplay ? warpPlan->gpuBytes : plan->additionalGpuBytes);
    auto cpu = retirement;
    const auto add = [&](std::uint64_t value) noexcept
    {
        if (value > (std::numeric_limits<std::uint64_t>::max)() - cpu)
            return false;
        cpu += value;
        return true;
    };
    // Private brush pipeline, structured readback, metadata and all three bounded command shadows.
    if ((!warpReplay && !warpLive && (!add(plan->retirementCpuBytes) || !add(plan->additionalCpuBytes))) ||
        !add(128ull * 1024 * 1024) || !add(sizeof(StrokeFinishJob) + sizeof(FinishPayload)) ||
        !add(SharedPublicationMemory::OwnershipAllowance()) || (_renderer.IsWarp() && !add(gpu)))
        return E_OUTOFMEMORY;
    GpuResidencyReservation reservation;
    if (gpu)
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
            gpu, {_gpuWork.DeviceGeneration(), segment.budget, segment.currentUsage}, decision);
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
        auto constructed = std::make_unique<StrokeFinishJob>(
            *this, *stamp, opening, _canvasBrushSessionOpening.value_or(0), retirement, cpu, std::move(reservation),
            processingSamples, warpReplay, warpLive);
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
