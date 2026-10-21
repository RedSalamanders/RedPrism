#include "Application.h"

#include "../Tests/Support/Support.Tests.DocumentFixture.h"
#include "ChromeHost.h"
#include "DocumentSizeHistory.h"
#include "FinalPublicationWork.h"
#include "Runtime.Tests.SizingWorkTestGate.h"
#include "UiBenchmark.h"
#include "resource.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <limits>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>
#include <variant>

namespace
{
#pragma warning(push)
#pragma warning(disable : 4324)
struct SizingPayload final : DeferredReleaseNode
{
    std::shared_ptr<SharedPublicationMemory> finalMemory;
    std::shared_ptr<SharedPublicationMemory> temporaryMemory;
    std::optional<JobToken> token;
    std::optional<CanvasDocument> source;
    DocumentHistory::StateView history;
    std::optional<Guid> active;
    CanvasDocument result;
    std::optional<PreparedDocumentSize> publication;
    std::shared_ptr<SizingWorkTestGate> gate;
    DWORD ownerThread = 0;
    SizingPayload()
    {
        bytes = kDeferredReleaseThreshold;
        destroy = [](DeferredReleaseNode* node) noexcept
        { const std::unique_ptr<SizingPayload> owned(static_cast<SizingPayload*>(node)); };
    }
    ~SizingPayload()
    {
        if (gate)
        {
            gate->payloadRetiredOffOwner.store(GetCurrentThreadId() != ownerThread);
            gate->payloadRetired.store(true);
        }
    }
};
#pragma warning(pop)
struct RetireSizingPayload final
{
    void operator()(SizingPayload* payload) const noexcept
    {
        if (payload && !TryDeferRelease(*payload))
        {
            const std::unique_ptr<SizingPayload> owned(payload);
        }
    }
};
} // namespace

class Application::SizingJob final : public Job
{
  public:
    SizingJob(Application& owner, HistoryWorkStamp stamp, std::variant<CanvasSizeOptions, ImageSizeOptions> options,
              std::uint64_t retirement, std::uint64_t initialMemory, std::uint64_t outputMemory, bool finalDisplay)
        : _owner(owner), _stamp(stamp), _options(std::move(options)), _invocation(owner._sizeInvocation),
          _generation(owner._session.Generation()), _sourceAuthority(owner._session.CaptureDocumentReadAuthority()),
          _retirement(retirement), _gate(owner._sizingWorkTestGate), _ownerThread(GetCurrentThreadId()),
          _initialMemory(initialMemory), _outputMemory(outputMemory), _finalDisplay(finalDisplay)
    {
    }
    JobResult Run(JobContext& context) override
    {
        if (_finalDelegate)
            return _finalDelegate->Run(context);
        if (context.cancellation.IsCancelled())
            return {JobStatus::Cancelled};
        if (!_payload)
        {
            // Allocate only after original admission, on its first worker. The next successful
            // acknowledgement captures source/token/history into this already constructed payload.
            auto owned = std::make_unique<SizingPayload>();
            owned->bytes = (std::max)(_retirement, kDeferredReleaseThreshold);
            owned->gate = _gate;
            owned->ownerThread = _ownerThread;
            if (_finalDisplay)
            {
                owned->finalMemory = SharedPublicationMemory::CreateForWork(_outputMemory);
                owned->temporaryMemory = SharedPublicationMemory::CreateForWork(_initialMemory);
                _retirementMemory = owned->temporaryMemory;
            }
            if (_gate)
                _gate->payloadAllocatedOffOwner.store(GetCurrentThreadId() != _ownerThread);
            _payload.reset(owned.release());
            return {};
        }
        if (!_captured || !_payload->source || !_payload->token)
            return {JobStatus::Failed, E_UNEXPECTED};
        if (_gate && !_gate->afterPreparation)
        {
            const auto result = PassGate(context);
            if (result.status != JobStatus::Finished)
                return result;
        }
        if (_gate && _gate->throwAllocationFailure)
            throw std::bad_alloc{};
        _status = std::visit(
            [&](const auto& options)
            {
                using Options = std::decay_t<decltype(options)>;
                if constexpr (std::is_same_v<Options, CanvasSizeOptions>)
                    return ResizeCanvas(_payload->source.value(), options, _payload->result, context.cancellation,
                                        _payload->finalMemory);
                else
                    return ResizeImage(_payload->source.value(), options, _payload->result, context.cancellation,
                                       _payload->finalMemory);
            },
            _options);
        if (_gate && _gate->status)
            _status = _gate->status.value();
        if (_status == ResizeStatus::Ok)
        {
            const DocumentHistory::PrepareObserver observer{[](void* context, DocumentHistory::PrepareStage stage)
                                                            {
                                                                auto* gate = static_cast<SizingWorkTestGate*>(context);
                                                                if (gate && gate->historyFailure == stage)
                                                                {
                                                                    gate->historyFaultHits.fetch_add(1);
                                                                    throw std::bad_alloc{};
                                                                }
                                                            },
                                                            _gate.get()};
            _payload->publication = PrepareDocumentSizePublication(
                _payload->source, _payload->active, _payload->token.value(), std::move(_payload->history),
                std::move(_payload->result),
                std::holds_alternative<CanvasSizeOptions>(_options) ? L"Canvas Size" : L"Image Size",
                context.cancellation, _gate ? observer : DocumentHistory::PrepareObserver{}, _payload->finalMemory);
            if (!_payload->publication)
                _status = context.cancellation.IsCancelled() ? ResizeStatus::Cancelled : ResizeStatus::Invalid;
        }
        if (_gate && _gate->afterPreparation)
        {
            const auto result = PassGate(context);
            if (result.status != JobStatus::Finished)
                return result;
        }
        if (!context.cancellation.IsCancelled() && _finalDisplay && _status == ResizeStatus::Ok)
        {
            if (!_payload->publication)
                return {JobStatus::Failed, E_UNEXPECTED};
            _finalDelegate = MakeFinalStageForWork(context.reservedMemoryBytes);
            if (!_finalDelegate)
                return {JobStatus::Failed, E_OUTOFMEMORY};
        }
        return context.cancellation.IsCancelled() || _status == ResizeStatus::Cancelled
                   ? JobResult{JobStatus::Cancelled}
                   : JobResult{};
    }
    void RetireWorkerStage(JobContext& context, JobResult result) noexcept override
    {
        if (_finalDelegate)
        {
            _finalDelegate->RetireWorkerStage(context, result);
            return;
        }
        if (_finalDisplay && _payload &&
            (result.status != JobStatus::Finished || (_captured && _status != ResizeStatus::Ok)))
        {
            // Failure/cancellation before delegate construction can own a full private resize and
            // prepared history. Retire it on this original worker, before terminal owner delivery;
            // a separately admitted release record must not be needed to avoid a large owner free.
            {
                const std::unique_ptr<SizingPayload> retired(_payload.release());
            }
            if (_gate)
                _gate->failureRetiredByOriginalWorker.store(true);
        }
    }
    bool DeferRetirement(JobResult result, JobTicket ticket) noexcept override
    {
        AcceptOriginal(ticket);
        return _finalDelegate && _finalDelegate->DeferRetirement(result, ticket);
    }
    bool DeferCompletion(JobResult result, JobTicket ticket) noexcept override
    {
        AcceptOriginal(ticket);
        if (_finalDelegate)
            return _finalDelegate->DeferCompletion(result, ticket);
        if (_captured || !_payload || result.status != JobStatus::Finished || _owner._shutdownStarted ||
            !_owner._historyWork.IsCurrent(_stamp) || _owner._historyWork.CancelRequested(_stamp) ||
            _owner._sizeInvocation != _invocation || _owner._session.DocumentSerial() != _stamp.documentSerial ||
            _owner._session.Generation() != _generation || !_owner._session.Document())
            return false;
        try
        {

            if (_gate)
            {
                _gate->captureReservationHeld.store(_gate->reservationBytes != 0 &&
                                                    _owner._jobs.ReservedMemoryBytes() >= _gate->reservationBytes);
                if (_gate->throwCaptureFailure)
                    throw std::bad_alloc{};
            }
            _payload->token = _owner._session.CaptureJobToken();
            _payload->source = _owner._session.Document();
            _payload->active = _owner._session.ActiveLayerID();
            _payload->history = _owner._session.history.CaptureState();
            if (_payload->token)
            {
                _captured = true;
                if (_owner._jobs.Resume(ticket))
                    return true;
            }
            _captureError = E_UNEXPECTED;
        }
        catch (const std::bad_alloc&)
        {
            _captureError = E_OUTOFMEMORY;
        }
        return false;
    }
    void Completed(JobResult result) noexcept override
    {
        if (_finalDelegate)
        {
            _finalDelegate->Completed(result);
            result = _finalOutcome;
        }
        if (_retirementMemory && !_retirementMemory->IsBound() &&
            FAILED(_retirementMemory->BindForRetirement(_owner._jobs, _original)))
            std::terminate();
        HostOutcome outcome = result.status == JobStatus::Cancelled ? HostOutcome::Cancelled
                              : result.status == JobStatus::Failed  ? HostOutcome::Failed
                                                                    : HostOutcome::Succeeded;
        std::int32_t error = result.error;
        if (!_finalApplied &&
            (_owner._sizingStamp != _stamp || !_owner._historyWork.IsCurrent(_stamp) ||
             _owner._sizeInvocation != _invocation || _owner._session.DocumentSerial() != _stamp.documentSerial ||
             _owner._session.Generation() != _generation ||
             (_payload && _payload->token &&
              !_owner._session.CanApplyJobResult(_payload->token.value(), _payload->token->sequence)) ||
             (_sourceAuthority && !_owner._session.IsDocumentReadCurrent(_sourceAuthority.value()))))
        {
            outcome = HostOutcome::Stale;
            error = E_ABORT;
        }
        else if (!_finalApplied && _owner._historyWork.CancelRequested(_stamp))
        {
            outcome = HostOutcome::Cancelled;
            error = E_ABORT;
        }
        else if (_captureError && outcome == HostOutcome::Succeeded)
        {
            outcome = HostOutcome::Failed;
            error = _captureError;
        }
        bool changed = _finalApplied;
        if (outcome == HostOutcome::Succeeded)
        {
            switch (_status)
            {
            case ResizeStatus::Ok:
                changed = _finalApplied ||
                          (!_finalDisplay && _payload && _payload->publication &&
                           _owner._session.ApplyPreparedDocumentSizeIfCurrent(_payload->publication.value()));
                if (!changed)
                {
                    outcome = HostOutcome::Stale;
                    error = E_ABORT;
                }
                break;
            case ResizeStatus::Unchanged:
                break;
            case ResizeStatus::TooLarge:
                outcome = HostOutcome::Failed;
                error = HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);
                break;
            case ResizeStatus::Memory:
                outcome = HostOutcome::Failed;
                error = E_OUTOFMEMORY;
                break;
            case ResizeStatus::Cancelled:
                outcome = HostOutcome::Cancelled;
                error = E_ABORT;
                break;
            case ResizeStatus::Invalid:
                outcome = HostOutcome::Failed;
                error = E_INVALIDARG;
                break;
            }
        }
        if (outcome == HostOutcome::Failed)
            _owner._prompts->PostNotice(_owner._window.get(), kNoticeMessage,
                                        error == static_cast<std::int32_t>(HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE))
                                            ? IDS_ERROR_TOO_LARGE
                                            : IDS_ERROR_RESIZE);
        _owner._sizingTicket.reset();
        _owner._sizingStamp.reset();
        if (changed)
        {
            _owner._viewport.followsFit = true;
            _owner.DocumentChanged();
        }
        else
        {
            _owner.SyncChrome();
            _owner.InvalidateFrame();
        }
        (void)_owner._historyWork.Finish(_stamp);
        _owner.ResumeHostHistory(_stamp, outcome, error);
    }

    void RetainJoinedRetirement(JobScheduler& jobs, JobTicket ticket) noexcept override
    {
        AcceptOriginal(ticket);
        if (_finalDelegate)
            _finalDelegate->RetainJoinedRetirement(jobs, ticket);
        else if (_retirementMemory && !_retirementMemory->IsBound() &&
                 FAILED(_retirementMemory->BindForRetirement(jobs, ticket)))
            std::terminate();
    }

  private:
    void AcceptOriginal(JobTicket ticket) noexcept
    {
        if (!ticket.generation || (_original.generation && ticket != _original))
            std::terminate();
        _original = ticket;
    }
    [[nodiscard]] bool CurrentParentOwner() const noexcept
    {
        return !_owner._shutdownStarted && _owner._sizingStamp == _stamp && _owner._historyWork.IsCurrent(_stamp) &&
               !_owner._historyWork.CancelRequested(_stamp) && _owner._sizeInvocation == _invocation &&
               _owner._session.DocumentSerial() == _stamp.documentSerial && _owner._session.Generation() == _generation;
    }
    __declspec(noinline) std::unique_ptr<Job> MakeFinalStageForWork(std::uint64_t memory)
    {
        // Construct callbacks only. They never inspect the borrowed owner until its acknowledged stage.
        FinalPublicationCallbacks callbacks;
        callbacks.context = this;
        callbacks.currentOwner = [](void* raw) noexcept { return static_cast<SizingJob*>(raw)->CurrentParentOwner(); };
        callbacks.waitingOwner = [](void* raw, bool waiting) noexcept
        {
            auto& parent = *static_cast<SizingJob*>(raw);
            parent._finalWaiting = waiting;
            if (parent._gate)
                parent._gate->finalWaitingForDisplay.store(waiting);
        };
        callbacks.appliedOwner = [](void* raw) noexcept
        {
            auto& parent = *static_cast<SizingJob*>(raw);
            parent._finalApplied = true;
            // Fit is part of the same nonfailing first-display handoff, before any GPU publication callback.
            parent._owner._viewport.followsFit = true;
            parent._owner._viewport.Fit(parent._owner.DocumentSize());
        };
        callbacks.completedOwner = [](void* raw, bool applied, JobResult outcome) noexcept
        {
            auto& parent = *static_cast<SizingJob*>(raw);
            parent._finalApplied = applied;
            parent._finalOutcome = outcome;
            // Count only the actual selected original's completed handoff, after rows/GPU publication
            // and worker retirement. The existing benchmark also requires the exact current graph/rows.
            if (applied && parent._owner._uiBenchmark && parent._owner._uiBenchmark->sizeOnly)
                ++parent._owner._uiBenchmark->sizeFinalPublications;
        };
        return Application::MakeSizeFinalForWork(
            &_owner, callbacks,
            SizeFinalPublicationWork{
                _payload->finalMemory, _payload->temporaryMemory, std::move(_payload->publication.value()),
                std::unique_ptr<DeferredReleaseNode, DestroyFinalParentForWork>(_payload.release()), memory,
                _outputMemory, _ownerThread, 0, 0});
    }
    JobResult PassGate(JobContext& context) noexcept
    {
        _gate->visits.fetch_add(1);
        if (!PostThreadMessageW(_ownerThread, SizingWorkTestGate::kMessage, 0, 0))
            return {JobStatus::Failed, HRESULT_FROM_WIN32(GetLastError())};
        const HANDLE handles[]{_gate->release.get(), context.shutdownEvent};
        const auto waited = WaitForMultipleObjects(2, handles, FALSE, 10000);
        if (context.cancellation.IsCancelled() || waited == WAIT_OBJECT_0 + 1)
            return {JobStatus::Cancelled};
        if (waited != WAIT_OBJECT_0)
            return {JobStatus::Failed, HRESULT_FROM_WIN32(ERROR_TIMEOUT)};
        return _gate->result;
    }
    Application& _owner; // owner callbacks only; workers read captured payload values
    const HistoryWorkStamp _stamp;
    const std::variant<CanvasSizeOptions, ImageSizeOptions> _options;
    const std::uint64_t _invocation;
    const std::uint64_t _generation;
    // Descriptor-only source authority is captured before admission, with no image/selection ownership.
    // It still detects restore/session replacement when a failed worker already destroyed its token.
    const std::optional<DocumentReadAuthority> _sourceAuthority;
    const std::uint64_t _retirement;
    const std::shared_ptr<SizingWorkTestGate> _gate;
    const DWORD _ownerThread;
    std::unique_ptr<SizingPayload, RetireSizingPayload> _payload;
    const std::uint64_t _initialMemory, _outputMemory;
    const bool _finalDisplay;
    std::shared_ptr<SharedPublicationMemory> _retirementMemory;
    std::unique_ptr<Job> _finalDelegate;
    JobTicket _original;
    bool _finalApplied = false, _finalWaiting = false;
    JobResult _finalOutcome{};
    bool _captured = false;
    ResizeStatus _status = ResizeStatus::Invalid;
    std::int32_t _captureError = 0;
};

HRESULT Application::StartSizing(std::variant<CanvasSizeOptions, ImageSizeOptions> options) noexcept
{
    if (_sizingTicket || !_session.Document())
        return HRESULT_FROM_WIN32(ERROR_INVALID_STATE);
    const auto ordinal = _activeHostRequest ? std::optional(_activeHostRequest->ordinal) : _historyWork.NextOrdinal();
    if (!ordinal)
        return HRESULT_FROM_WIN32(ERROR_NOT_ENOUGH_QUOTA);
    _sizingStamp = _historyWork.Begin(_session.DocumentSerial(), ordinal.value(), HistoryWorkKind::Commit);
    if (!_sizingStamp && _hostFinishedStamp && _hostFinishedStamp->ordinal == ordinal.value())
        _sizingStamp =
            _historyWork.Continue(_hostFinishedStamp.value(), _session.DocumentSerial(), HistoryWorkKind::Commit);
    if (!_sizingStamp)
        return E_UNEXPECTED;
    HRESULT result = E_INVALIDARG;
    try
    {
        const auto preparation =
            std::visit([&](const auto& size) { return SizingMemoryBytes(_session.Document().value(), size); }, options);
        const auto history = _session.history.EditMemoryBytes(_session.Document());
        auto retirement = _session.DocumentSizeRetirementMemoryBytes();
        auto memory = preparation;
        const auto add = [](std::uint64_t& value, std::uint64_t more) noexcept
        {
            if (more > (std::numeric_limits<std::uint64_t>::max)() - value)
                return false;
            value += more;
            return true;
        };
        if (preparation && history && retirement && add(memory, history) &&
            add(memory, sizeof(SizingJob) + sizeof(SizingPayload)) &&
            memory <= (std::numeric_limits<std::uint64_t>::max)() / 2 && add(retirement, memory * 2))
        {
            const bool finalDisplay = _selectionPixelFinalPublicationEnabled && _preparedLayerRowsEnabled &&
                                      _committedDisplayInitialized && _recordedCommandsInitialized;
            auto outputMemory = memory;
            if (finalDisplay)
            {
                // No continuing output: the original may grow once from the exact final immutable source,
                // before capturing rows or GPU/device owners. Initial admission covers CPU/history and
                // both output controls, plus positive old-source/temporary retirement overlap.
                if (!add(outputMemory, 2 * SharedPublicationMemory::OwnershipAllowance() + 32768) ||
                    !add(memory, outputMemory) || !add(memory, retirement))
                    throw std::bad_alloc{}; // Entire admission fails through the stamp-cleanup boundary below.
            }
            if (_sizingWorkTestGate)
                _sizingWorkTestGate->reservationBytes = memory;
            std::unique_ptr<Job> job = std::make_unique<SizingJob>(*this, _sizingStamp.value(), std::move(options),
                                                                   retirement, memory, outputMemory, finalDisplay);
            JobTicket ticket;
            JobOptions descriptor{JobClass::Commit, _session.DocumentSerial(), memory};
            descriptor.deferredRetirement = finalDisplay;
            descriptor.reserveRetainedMemory = finalDisplay;
            descriptor.allowMemoryWaiting = !finalDisplay;
            result = static_cast<HRESULT>(_jobs.Submit(job, descriptor, ticket));
            if (SUCCEEDED(result))
                _sizingTicket = ticket;
        }
    }
    catch (const std::bad_alloc&)
    {
        result = E_OUTOFMEMORY; // whole request fails without acceptance or live mutation
    }
    if (FAILED(result))
    {
        (void)_historyWork.Finish(_sizingStamp.value());
        _hostFinishedStamp = _sizingStamp;
        _sizingStamp.reset();
        return result;
    }
    if (_activeHostRequest)
    {
        _hostCommitStamp = _sizingStamp;
        _hostAfterPrerequisite = HostStage::Complete;
    }
    return S_OK;
}

int Application::RunSizingWorkSelfTest() noexcept
{
    // The prior fixture ends with document/history/display readers and asynchronous retirement.
    // Join its work, release those readers, then consume their real acknowledgements before capture.
    if (SettleJobs(_jobs, 10000, DispatchMessageThunk, this) != SettleResult::Settled)
    {
        SelfTestFailure(L"The sizing self-test could not settle the preceding fixture.\n");
        return 5;
    }
    _session.ClearDocument();
    _composition.ReleaseDocumentSurfaces();
    DocumentChanged();
    if (SettleJobs(_jobs, 10000, DispatchMessageThunk, this) != SettleResult::Settled)
    {
        SelfTestFailure(L"The sizing self-test could not retire the preceding document readers.\n");
        return 5;
    }
    // The preceding real Copy Merged shortcut leaves its private pixels alive across New Canvas.
    // Sizing must refund its own work while preserving that independent, legitimately charged reader.
    const auto retainedBaseline = _jobs.ReservedMemoryBytes();
    auto clipboardBaseline = _session.Clipboard();
    const auto clipboardSerial = _session.ClipboardSerial();
    const auto clipboardUnchanged = [&]() noexcept
    {
        const auto& current = _session.Clipboard();
        return _session.ClipboardSerial() == clipboardSerial &&
               (clipboardBaseline ? current && current->image == clipboardBaseline->image &&
                                        current->origin == clipboardBaseline->origin
                                  : !current);
    };
    if (!_jobs.IsIdle() || _jobs.ReleasingMemoryBytes() != 0 ||
        (retainedBaseline != 0 && (!clipboardBaseline || !clipboardBaseline->image.HasStorage())))
    {
        std::fprintf(stderr, "INFO sizing-baseline-entry reserved=%llu releasing=%llu idle=%u clipboard=%u\n",
                     static_cast<unsigned long long>(retainedBaseline),
                     static_cast<unsigned long long>(_jobs.ReleasingMemoryBytes()), _jobs.IsIdle() ? 1U : 0U,
                     clipboardBaseline && clipboardBaseline->image.HasStorage() ? 1U : 0U);
        SelfTestFailure(L"The sizing self-test requires settled work and an owned retained clipboard baseline.\n");
        return 5;
    }
    enum class Mode
    {
        CopiedCanvas,
        CopiedImage,
        PanelThenSizeThenUndo,
        CancelBefore,
        CancelAfter,
        Failure,
        Allocation,
        TooLarge,
        Invalid,
        Memory,
        Unchanged,
        Stale,
        Restore,
        Replace,
        Reopen,
        CaptureAllocation,
        HistoryBefore,
        HistoryAfter,
        HistoryEntry,
        HistoryStack,
        HistoryTrim,
        HistoryEpoch
    };
    struct State final
    {
        Application& owner;
        Mode mode;
        ContinuationReceipt receipt;
        Guid revision;
        std::uint64_t retainedBaseline;
        bool held = false;
        bool visited = false;
        bool cancelPending = false;
    };
    const auto dispatch = [](void* context, void* native) noexcept
    {
        auto& state = *static_cast<State*>(context);
        auto& owner = state.owner;
        auto& message = *static_cast<MSG*>(native);
        if (message.message != SizingWorkTestGate::kMessage)
        {
            owner.DispatchQueuedMessage(message);
            return;
        }
        state.visited = true;
        state.held =
            owner._session.Document() && owner._session.Document()->width == 8 &&
            owner._session.Document()->height == 4 && owner._historyWork.Busy() && owner._sizingTicket &&
            owner._jobs.ReservedMemoryBytes() >= state.retainedBaseline &&
            owner._jobs.ReservedMemoryBytes() - state.retainedBaseline == owner._sizingWorkTestGate->reservationBytes &&
            state.receipt.Read().outcome == HostOutcome::Pending;
        if (state.mode == Mode::PanelThenSizeThenUndo)
            state.held = state.held && owner._session.history.UndoName() == L"Levels";
        else
            state.held = state.held && owner._session.history.CurrentRevision() == state.revision;
        if (state.mode == Mode::CancelBefore || state.mode == Mode::CancelAfter)
        {
            if (state.mode == Mode::CancelBefore)
                owner.CancelHostContinuations();
            else
                (void)owner._historyWork.RequestCancel(); // a late Finished callback must still be dropped
            state.cancelPending = state.receipt.Read().outcome == HostOutcome::Pending && owner._historyWork.Busy();
        }
        else if (state.mode == Mode::Stale)
            owner._session.SetOpacity(0.5);
        else if (state.mode == Mode::Restore)
        {
            owner._session.Undo();
            owner._session.Redo(); // same pixels/id/dimensions, but a different restore serial
        }
        else if (state.mode == Mode::Replace)
            owner._session.CreateDocument(3, 2);
        else if (state.mode == Mode::Reopen)
            owner.OpenSizeCard(SizeCardKind::ImageSize);
        else if (state.mode == Mode::HistoryEpoch)
        {
            owner._session.history.Begin(L"Epoch only", owner._session.Document(), owner._session.ActiveLayerID());
            owner._session.history.End(owner._session.Document(), owner._session.ActiveLayerID());
        }
        else
        {
            // These live values must never alter the already selected options or clear a younger card.
            owner._sizeDialog.canvas.Set(23, true);
            owner._sizeDialog.image.Set(23, true);
            owner._sizeDialog.anchor = 0;
            owner._sizeDialog.extension = 4;
            owner._session.SetForegroundColor(0xFF0000FF);
        }
        owner._composition.ReleaseDocumentSurfaces();
        state.held = state.held && owner._composition.CanvasSurfaceBytes() == 0;
        (void)SetEvent(owner._sizingWorkTestGate->release.get());
    };
    for (const auto mode : {Mode::CopiedCanvas,
                            Mode::CopiedImage,
                            Mode::PanelThenSizeThenUndo,
                            Mode::CancelBefore,
                            Mode::CancelAfter,
                            Mode::Failure,
                            Mode::Allocation,
                            Mode::TooLarge,
                            Mode::Invalid,
                            Mode::Memory,
                            Mode::Unchanged,
                            Mode::Stale,
                            Mode::Restore,
                            Mode::Replace,
                            Mode::Reopen,
                            Mode::CaptureAllocation,
                            Mode::HistoryBefore,
                            Mode::HistoryAfter,
                            Mode::HistoryEntry,
                            Mode::HistoryStack,
                            Mode::HistoryTrim,
                            Mode::HistoryEpoch})
    {
        _session.CreateDocument(8, 4);
        (void)InsertPaintedLayer(_session, L"Sizing source", 8, 4, {0, 0}, 0, 0xFF404040);
        const bool historyFailure = mode >= Mode::HistoryBefore && mode <= Mode::HistoryTrim;
        const bool preservesRedo = historyFailure || mode == Mode::CaptureAllocation || mode == Mode::HistoryEpoch;
        if (preservesRedo)
        {
            _session.SetOpacity(0.75);
            _session.Undo();
        }
        _session.history.MarkSaved();
        _session.SetForegroundColor(0xFFFF0000);
        _viewport.followsFit = false;
        DocumentChanged();
        const auto revision = _session.history.CurrentRevision();
        const auto historyCount = _session.history.UndoCount();
        const Raster sourcePixels = _session.ActiveLayer()->asset->image;
        const auto presents = _renderer.PresentCount();
        _sizingWorkTestGate = std::make_shared<SizingWorkTestGate>();
        if (!_sizingWorkTestGate->release)
            return 5;
        _sizingWorkTestGate->afterPreparation = mode != Mode::CancelBefore;
        _sizingWorkTestGate->throwAllocationFailure = mode == Mode::Allocation;
        _sizingWorkTestGate->throwCaptureFailure = mode == Mode::CaptureAllocation;
        if (historyFailure)
        {
            constexpr std::array stages{
                DocumentHistory::PrepareStage::BeforeSnapshot, DocumentHistory::PrepareStage::AfterSnapshot,
                DocumentHistory::PrepareStage::Entry, DocumentHistory::PrepareStage::StackGrowth,
                DocumentHistory::PrepareStage::TrimIndex};
            _sizingWorkTestGate->historyFailure =
                stages[static_cast<unsigned int>(mode) - static_cast<unsigned int>(Mode::HistoryBefore)];
        }
        if (mode == Mode::Allocation || historyFailure)
            _sizingWorkTestGate->afterPreparation = false;
        if (mode == Mode::Failure)
            _sizingWorkTestGate->result = {JobStatus::Failed, E_ACCESSDENIED};
        if (mode == Mode::Invalid)
            _sizingWorkTestGate->status = ResizeStatus::Invalid;
        if (mode == Mode::Memory)
            _sizingWorkTestGate->status = ResizeStatus::Memory;
        const bool image = mode == Mode::CopiedImage;
        OpenSizeCard(image ? SizeCardKind::ImageSize : SizeCardKind::CanvasSize);
        _sizeDialog.anchor = 8;
        _sizeDialog.extension = 1;
        if (image)
        {
            _sizeDialog.image.Set(16, true);
            _sizeDialog.image.Set(8, false);
            _sizeDialog.image.sampling = LayerSampling::Nearest;
            _sizeDialog.image.resolution = 300;
        }
        else if (mode != Mode::Unchanged)
        {
            _sizeDialog.canvas.Set(mode == Mode::TooLarge ? 30000 : 10, true);
            _sizeDialog.canvas.Set(mode == Mode::TooLarge ? 30000 : 6, false);
        }
        if (mode == Mode::PanelThenSizeThenUndo)
        {
            if (!BeginColorEdit(ChromeHost::kCommandLevels))
                return 5;
            OnAdjustAction(AdjustAction::SetInputGamma, 2);
        }
        State state{*this, mode, QueueHostInput(HostSizeApplyInput{_sizeDialog, _sizeInvocation}), revision,
                    retainedBaseline};
        std::optional<ContinuationReceipt> undo;
        if (mode == Mode::PanelThenSizeThenUndo)
            undo = QueueHostInput(HostCommandInput{ChromeHost::kCommandUndo});
        const auto settled = SettleJobs(_jobs, 15000, dispatch, &state);
        const auto actual = state.receipt.Read();
        const auto expected = mode == Mode::CancelBefore || mode == Mode::CancelAfter ? HostOutcome::Cancelled
                              : mode == Mode::Stale || mode == Mode::Restore || mode == Mode::Replace ||
                                      mode == Mode::Reopen || mode == Mode::HistoryEpoch
                                  ? HostOutcome::Stale
                              : mode == Mode::Failure || mode == Mode::Allocation || mode == Mode::TooLarge ||
                                      mode == Mode::Invalid || mode == Mode::Memory ||
                                      mode == Mode::CaptureAllocation || historyFailure
                                  ? HostOutcome::Failed
                                  : HostOutcome::Succeeded;
        const auto gate = _sizingWorkTestGate;
        const bool reached = mode == Mode::CaptureAllocation ? !state.visited : state.visited && state.held;
        bool correct = settled == SettleResult::Settled && reached && actual.outcome == expected &&
                       !_historyWork.Busy() && !_sizingTicket && !_sizingStamp &&
                       _jobs.ReservedMemoryBytes() == retainedBaseline && _jobs.ReleasingMemoryBytes() == 0 &&
                       clipboardUnchanged() && _renderer.PresentCount() == presents &&
                       gate->captureReservationHeld.load() && gate->payloadAllocatedOffOwner.load() &&
                       gate->payloadRetired.load() && gate->payloadRetiredOffOwner.load();
        if (preservesRedo)
            correct = correct && _session.history.UndoCount() == historyCount && _session.history.CanRedo() &&
                      _session.history.RedoName() == L"Layer Opacity" && _session.ActiveLayer()->opacity == 1 &&
                      _session.ActiveLayer()->asset->image == sourcePixels;
        if (historyFailure)
            correct = correct && gate->historyFaultHits.load() == 1;
        if (mode == Mode::CopiedCanvas)
        {
            const auto& document = _session.Document().value();
            correct = correct && document.width == 10 && document.height == 6 && document.layers.size() == 2 &&
                      document.layers.back().transform.origin == Vec2{2, 2} && _viewport.followsFit &&
                      _session.history.UndoCount() == historyCount + 1 && _session.history.UndoName() == L"Canvas Size";
            std::array<std::uint8_t, 4> pixel{};
            if (document.layers.size() == 2 && document.layers.front().asset)
                document.layers.front().asset->image.ReadRegion(0, 0, 1, 1, pixel.data(), 4, 0);
            correct = correct && pixel == std::array<std::uint8_t, 4>{0, 0, 255, 255};
            _session.Undo();
            _session.Redo();
            correct = correct && _session.Document()->width == 10 && _session.history.UndoName() == L"Canvas Size";
        }
        else if (mode == Mode::CopiedImage)
            correct = correct && _session.Document()->width == 16 && _session.Document()->height == 8 &&
                      _session.Document()->resolution == 300 &&
                      _session.ActiveLayer()->transform.sampling == LayerSampling::Nearest &&
                      _session.ActiveLayer()->asset->image.Width() == 16 &&
                      _session.history.UndoName() == L"Image Size" && _session.history.UndoCount() == historyCount + 1;
        else if (mode == Mode::PanelThenSizeThenUndo)
            correct = correct && undo->Read().outcome == HostOutcome::Succeeded && _session.Document()->width == 8 &&
                      _session.history.UndoName() == L"Levels" && _session.history.RedoName() == L"Canvas Size";
        else if (mode == Mode::Replace)
            correct = correct && _session.Document()->width == 3 && _session.Document()->height == 2;
        else
        {
            correct =
                correct && _session.Document()->width == 8 && _session.Document()->height == 4 && !_viewport.followsFit;
            if (mode == Mode::Stale)
                correct = correct && _session.ActiveLayer()->opacity == 0.5;
            else
                correct = correct && _session.history.CurrentRevision() == revision;
            if (mode == Mode::CancelBefore || mode == Mode::CancelAfter)
                correct = correct && state.cancelPending;
            if (mode == Mode::Reopen)
                correct = correct && SizeCardOpen() && _sizeDialog.kind == SizeCardKind::ImageSize;
            if (mode == Mode::Failure)
                correct = correct && actual.error == E_ACCESSDENIED;
            if (mode == Mode::Allocation || mode == Mode::Memory || mode == Mode::CaptureAllocation || historyFailure)
                correct = correct && actual.error == static_cast<std::int32_t>(E_OUTOFMEMORY);
            if (mode == Mode::TooLarge)
                correct =
                    correct && actual.error == static_cast<std::int32_t>(HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE));
            if (mode == Mode::Invalid)
                correct = correct && actual.error == E_INVALIDARG;
        }
        _sizingWorkTestGate.reset();
        CancelSizeCard();
        std::fprintf(stderr,
                     "INFO sizing-memory-baseline mode=%u reserved=%llu baseline=%llu releasing=%llu clipboard=%u "
                     "settled=%u reached=%u present=%u capture=%u retired=%u offOwner=%u\n",
                     static_cast<unsigned int>(mode), static_cast<unsigned long long>(_jobs.ReservedMemoryBytes()),
                     static_cast<unsigned long long>(retainedBaseline),
                     static_cast<unsigned long long>(_jobs.ReleasingMemoryBytes()), clipboardUnchanged() ? 1U : 0U,
                     settled == SettleResult::Settled ? 1U : 0U, reached ? 1U : 0U,
                     _renderer.PresentCount() == presents ? 1U : 0U, gate->captureReservationHeld.load() ? 1U : 0U,
                     gate->payloadRetired.load() ? 1U : 0U, gate->payloadRetiredOffOwner.load() ? 1U : 0U);
        if (!correct)
        {
            std::fprintf(stderr, "Sizing mode=%u visited=%u held=%u outcome=%u expected=%u error=0x%08x\n",
                         static_cast<unsigned int>(mode), state.visited ? 1U : 0U, state.held ? 1U : 0U,
                         static_cast<unsigned int>(actual.outcome), static_cast<unsigned int>(expected),
                         static_cast<unsigned int>(actual.error));
            SelfTestFailure(
                L"Asynchronous sizing failed immutable input, ordering, cancellation or terminal ownership.\n");
            return 5;
        }
        std::fprintf(stderr, "PASS sizing-history-host.mode=%u\n", static_cast<unsigned int>(mode));
    }
    // Isolate later fixtures with a real subsequent private Copy. Close alone preserves the old clipboard;
    // the copied baseline remains a legitimate reader until we explicitly release it below.
    _session.ClearDocument();
    DocumentFixture replacement;
    replacement.id = Guid::Generate();
    replacement.width = replacement.height = 1;
    constexpr std::array<std::uint8_t, 4> literal{7, 8, 9, 255};
    ImageLayer layer = ImageLayer::Blank(L"Sizing clipboard retirement", {1, 1});
    const Guid active = layer.id;
    layer.asset =
        RasterAsset{Raster::Make(1, 1, RasterFormat::Bgra8888, literal.data(), literal.size()), {}, layer.name};
    replacement.layers.push_back(std::move(layer));
    _session.ReplaceDocument(std::move(replacement).Seal(), active);
    const bool copied = _session.CopySelection();
    _session.ClearDocument();
    // Reference clear can defer metadata destruction and enqueue original memory acknowledgements.
    // Keep the baseline clipboard reader alive while consuming those events before the exact charge observation.
    const bool retirementSettled = SettleJobs(_jobs, 10000, DispatchMessageThunk, this) == SettleResult::Settled;
    const auto& current = _session.Clipboard();
    const bool replaced = retirementSettled && copied && current && current->image.Width() == 1 &&
                          current->image.Height() == 1 && current->origin == Vec2{0, 0} && current->image.Data() &&
                          std::equal(literal.begin(), literal.end(), current->image.Data()) &&
                          _session.ClipboardSerial() == clipboardSerial + 1 &&
                          (!clipboardBaseline || current->image != clipboardBaseline->image) && !_session.Document() &&
                          _session.history.UndoCount() == 0 && !_session.history.CanRedo() &&
                          _jobs.ReservedMemoryBytes() == retainedBaseline && _jobs.ReleasingMemoryBytes() == 0;
    if (!replaced)
    {
        const bool literalMatches = current && current->image.Format() == RasterFormat::Bgra8888 &&
                                    current->image.ByteSize() >= literal.size() && current->image.Data() &&
                                    std::equal(literal.begin(), literal.end(), current->image.Data());
        std::fprintf(stderr,
                     "DIAGNOSTIC sizing-clipboard-retirement settled=%u copied=%u current=%u width=%d height=%d "
                     "origin=%.3f,%.3f literal=%u serial=%llu originalSerial=%llu distinct=%u document=%u "
                     "undo=%llu redo=%u reserved=%llu baseline=%llu releasing=%llu idle=%u\n",
                     retirementSettled ? 1u : 0u, copied ? 1u : 0u, current ? 1u : 0u,
                     current ? current->image.Width() : 0, current ? current->image.Height() : 0,
                     current ? current->origin.x : 0.0, current ? current->origin.y : 0.0, literalMatches ? 1u : 0u,
                     static_cast<unsigned long long>(_session.ClipboardSerial()),
                     static_cast<unsigned long long>(clipboardSerial),
                     !clipboardBaseline || (current && current->image != clipboardBaseline->image) ? 1u : 0u,
                     _session.Document() ? 1u : 0u, static_cast<unsigned long long>(_session.history.UndoCount()),
                     _session.history.CanRedo() ? 1u : 0u, static_cast<unsigned long long>(_jobs.ReservedMemoryBytes()),
                     static_cast<unsigned long long>(retainedBaseline),
                     static_cast<unsigned long long>(_jobs.ReleasingMemoryBytes()), _jobs.IsIdle() ? 1u : 0u);
    }
    std::fprintf(stderr, "%s sizing-clipboard-retirement.actual-private-replacement-keeps-baseline-reader-charge\n",
                 replaced ? "PASS" : "FAIL");
    clipboardBaseline.reset();
    const bool refunded = SettleJobs(_jobs, 10000, DispatchMessageThunk, this) == SettleResult::Settled &&
                          _jobs.ReservedMemoryBytes() == 0 && _jobs.ReleasingMemoryBytes() == 0 && _jobs.IsIdle();
    std::fprintf(stderr, "%s sizing-clipboard-retirement.last-baseline-reader-release-acknowledges-original-refund\n",
                 refunded ? "PASS" : "FAIL");
    return replaced && refunded ? 0 : 5;
}
